#pragma once

#include "chunkWorker.h"
#include "world.h"
#include <assets/assetManager.h>
#include <renderer/buffer.h>
#include "gpuChunkMesher.h"
#include "gpuTerrainGenerator.h"
#include "waterPhysics.h"
#include "fallingBlockPhysics.h"
#include "redstonePhysics.h"

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <bit>
#include <iostream>
#include <optional>
#include <functional>

namespace ac {

	struct worldChunkGPU {
		struct blockEmitter {
			DirectX::XMINT3 position;
			blockId id = 0;
		};

		// Published chunks are immutable snapshots. Worker jobs retain shared
		// references, so unloading or replacing a chunk never races a remesh.
		std::shared_ptr<const chunk> _chunk;
		gpuModel _opaque;
		gpuModel _translucent;
		std::array<gpuModel, CHUNK_SUBCHUNKS> _opaqueSections;
		std::array<gpuModel, CHUNK_SUBCHUNKS> _translucentSections;
		gpuChunkFaceMesh _gpuFaces;
		gpuChunkVoxelBuffer _gpuVoxels;
		DirectX::XMFLOAT4X4 _worldTransform{};
		bool _hasGpuMesh = false;
		uint16_t _meshedNeighborMask = 0;
		uint16_t _requestedNeighborMask = 0;
		uint32_t _dirtySectionMask = 0;
		int _solidMinY = 0;
		int _solidMaxY = CHUNK_HEIGHT;
		std::vector<blockEmitter> _emitters;
		// Bumped whenever _emitters changes so light consumers can cache their
		// per-chunk conversion instead of rescanning every loaded chunk.
		uint64_t _emitterRevision = 1;

		void bakeWorldTransform() {
			if (!_chunk) return;
			const auto& position = _chunk->_position;
			DirectX::XMStoreFloat4x4(
				&_worldTransform,
				DirectX::XMMatrixTranspose(DirectX::XMMatrixTranslation(
					static_cast<float>(position.x * CHUNK_WIDTH),
					static_cast<float>(position.y * CHUNK_HEIGHT),
					static_cast<float>(position.z * CHUNK_LENGTH))));
		}
	};

	struct worldChunkKey {
		int32_t x = 0;
		int32_t z = 0;

		bool operator==(const worldChunkKey& o) const {
			return x == o.x && z == o.z;
		}
	};

	struct worldChunkKeyHash {
		size_t operator()(const worldChunkKey& k) const {
			const size_t x = std::hash<int32_t>{}(k.x);
			const size_t z = std::hash<int32_t>{}(k.z);
			return x ^ (z << 1);
		}
	};

	class worldStreamer {
	private:
		world* _world = nullptr;
		staticAssetManager* _blocks = nullptr;
		modelManager* _models = nullptr;
		chunkWorker _worker;
		gpuChunkMesherPrototype _gpuMesher;
		chunkMesher _detailMesher;
		bool _gpuMeshing = false;
		gpuTerrainGenerator _gpuTerrainGenerator;
		bool _gpuTerrainReady = false;
		bool _gpuTerrain = false;

		std::unordered_map<worldChunkKey, worldChunkGPU, worldChunkKeyHash> _chunks;
		std::unordered_set<worldChunkKey, worldChunkKeyHash> _requested;
		std::unordered_set<worldChunkKey, worldChunkKeyHash> _remesh;
		std::unordered_set<worldChunkKey, worldChunkKeyHash> _remeshInFlight;
		std::unordered_map<worldChunkKey, std::chrono::steady_clock::time_point, worldChunkKeyHash> _editRemeshDue;
		int _streamRadius = 8;
		int _unloadRadius = 10;
		int32_t _playerX = INT32_MIN;
		int32_t _playerZ = INT32_MIN;
		uint64_t _blockRevision = 0;
		uint64_t _shadowRevision = 0;
		uint64_t _localizedBlockRevision = 0;
		DirectX::XMINT3 _localizedBlockPosition{};
		// Rolling log of individually edited cells. Consumers that cache
		// per-region work ask for the cells touched since the revision they last
		// processed, so a single edit never invalidates the whole world.
		struct blockChangeRecord {
			uint64_t revision = 0;
			DirectX::XMINT3 position{};
		};
		static constexpr size_t MAX_BLOCK_CHANGE_HISTORY = 8192u;
		std::deque<blockChangeRecord> _blockChangeHistory;
		// Oldest revision the history can still answer for. Bulk events
		// (chunk load/unload) reset this so callers fall back to a full rebuild.
		uint64_t _blockChangeHistoryBase = 0;
		waterPhysics _waterPhysics;
		fallingBlockPhysics _fallingPhysics;
		redstonePhysics _redstonePhysics;
		using beforeBlockChange = std::function<bool(
			int32_t, int32_t, int32_t, blockId, blockId&)>;
		using afterBlockChange = std::function<void(
			int32_t, int32_t, int32_t, blockId, blockId)>;
		beforeBlockChange _beforeBlockChange;
		afterBlockChange _afterBlockChange;

		static constexpr size_t MAX_MESH_UPLOADS_PER_FRAME = 1;
		static constexpr size_t MAX_GPU_TERRAIN_UPLOADS_PER_FRAME = 3;
		static constexpr size_t MAX_REMESH_REQUESTS_PER_FRAME = 2;
		static constexpr uint32_t ALL_SECTION_MASK = (1u << CHUNK_SUBCHUNKS) - 1u;

		static worldChunkKey key(int32_t x, int32_t z) {
			return { x, z };
		}

		static int32_t worldToChunk(float v, int32_t size) {
			return static_cast<int32_t>(
				std::floor(v / static_cast<float>(size))
				);
		}

		const chunk* getChunk(int32_t x, int32_t z) const {
			auto it = _chunks.find(key(x, z));
			return it == _chunks.end() ? nullptr : it->second._chunk.get();
		}

		static bool containsWater(const subchunk& section) {
			if (section.isSingle())
				return blockType(section.singleBlock()) == WATER_BLOCK_TYPE;
			return std::any_of(section.palette().begin(), section.palette().end(),
				[](blockId state) { return blockType(state) == WATER_BLOCK_TYPE; });
		}

		void seedRedstone(const worldChunkKey& center) {
			const auto component = [this](blockId state) {
				const auto* definition = _blocks->get(blockType(state));
				return isRedstoneComponent(state) || (definition &&
					definition->_behavior.redstone != blockRedstoneBehavior::none);
			};
			// Resume saved circuits and reconcile devices up to two blocks across
			// a newly loaded boundary (including comparator reads through a solid).
			for (int dz = -1; dz <= 1; ++dz) for (int dx = -1; dx <= 1; ++dx) {
				if (dx && dz) continue;
				const auto* source = getChunk(center.x + dx, center.z + dz);
				if (!source) continue;
				for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
					const auto& data = source->_subchunks[section];
					if (data.isSingle() ? !component(data.singleBlock()) :
						!std::any_of(data.palette().begin(), data.palette().end(), component)) continue;
					const int minX = dx < 0 ? CHUNK_WIDTH - 2 : 0;
					const int maxX = dx > 0 ? 2 : CHUNK_WIDTH;
					const int minZ = dz < 0 ? CHUNK_LENGTH - 2 : 0;
					const int maxZ = dz > 0 ? 2 : CHUNK_LENGTH;
					for (int y = section * SUBCHUNK_HEIGHT;
						y < (std::min)(int((section + 1) * SUBCHUNK_HEIGHT), int(CHUNK_HEIGHT)); ++y)
						for (int z = minZ; z < maxZ; ++z) for (int x = minX; x < maxX; ++x) {
							const int wx = (center.x + dx) * CHUNK_WIDTH + x;
							const int wz = (center.z + dz) * CHUNK_LENGTH + z;
							if (component(blockAt(wx, y, wz))) _redstonePhysics.enqueue(wx, y, wz);
						}
				}
			}
		}

		void seedWaterBoundaries(const worldChunkKey& center) {
			constexpr int32_t directions[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
			const chunk* source = getChunk(center.x, center.z);
			if (!source) return;
			for (const auto& direction : directions) {
				const chunk* neighbor = getChunk(center.x + direction[0], center.z + direction[1]);
				if (!neighbor) continue;
				for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
					if (!containsWater(source->_subchunks[section]) &&
						!containsWater(neighbor->_subchunks[section])) continue;
					const int32_t startY = static_cast<int32_t>(section * SUBCHUNK_HEIGHT);
					const int32_t endY = (std::min)(startY + SUBCHUNK_HEIGHT, CHUNK_HEIGHT);
					for (int32_t y = startY; y < endY; ++y) {
						for (int32_t offset = 0; offset < CHUNK_WIDTH; ++offset) {
						const int32_t x = center.x * CHUNK_WIDTH +
							(direction[0] < 0 ? 0 : (direction[0] > 0 ? CHUNK_WIDTH - 1 : offset));
						const int32_t z = center.z * CHUNK_LENGTH +
							(direction[1] < 0 ? 0 : (direction[1] > 0 ? CHUNK_LENGTH - 1 : offset));
						const int32_t neighborX = x + direction[0];
						const int32_t neighborZ = z + direction[1];
						const blockId centerState = blockAt(x, y, z);
						const blockId neighborState = blockAt(neighborX, y, neighborZ);
						if ((fluidLevel(centerState) > 0u || fluidLevel(neighborState) > 0u) &&
							centerState != neighborState) {
							_waterPhysics.enqueue(x, y, z);
							_waterPhysics.enqueue(neighborX, y, neighborZ);
						}
						}
					}
				}
			}
		}

		void request(int32_t x, int32_t z) {
			const worldChunkKey k = key(x, z);
			if (_chunks.find(k) != _chunks.end()) return;
			if (!_requested.insert(k).second) return;
			_worker.request({ x, 0, z });
		}

		uint16_t neighborMask(int32_t x, int32_t z) const {
			uint16_t mask = 0;
			if (getChunk(x - 1, z)) mask |= 1u << 0;
			if (getChunk(x + 1, z)) mask |= 1u << 1;
			if (getChunk(x, z - 1)) mask |= 1u << 2;
			if (getChunk(x, z + 1)) mask |= 1u << 3;
			if (getChunk(x - 1, z - 1)) mask |= 1u << 4;
			if (getChunk(x - 1, z + 1)) mask |= 1u << 5;
			if (getChunk(x + 1, z - 1)) mask |= 1u << 6;
			if (getChunk(x + 1, z + 1)) mask |= 1u << 7;
			return mask;
		}

		uint32_t neighborhoodSectionMask(int32_t x, int32_t z) const {
			uint32_t mask = 0u;
			for (int32_t dz = -1; dz <= 1; ++dz) {
				for (int32_t dx = -1; dx <= 1; ++dx) {
					const chunk* source = getChunk(x + dx, z + dz);
					if (!source) continue;
					for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section)
						if (!source->_subchunks[section].isEmpty()) mask |= 1u << section;
				}
			}
			return mask;
		}

		bool isEmissive(blockId id) const {
			id = normalizeBlockState(id);
			if (isRedstoneWire(id) && redstonePower(id) > 0u)
				return true;
			if ((isRedstoneLamp(id) || isRedstoneTorch(id)) && !isBlockPowered(id))
				return false;
			const blockDefinition* definition = id
				? _blocks->get(blockType(visualBlockState(id))) : nullptr;
			return definition && definition->_emission.intensity > 0.0f &&
				definition->_emission.radius > 0.0f;
		}

		// Records a single edited cell so cache holders can invalidate only the
		// regions that actually moved.
		void recordBlockChange(int32_t worldX, int32_t worldY, int32_t worldZ) {
			_blockChangeHistory.push_back({ _blockRevision, { worldX, worldY, worldZ } });
			while (_blockChangeHistory.size() > MAX_BLOCK_CHANGE_HISTORY) {
				_blockChangeHistoryBase = _blockChangeHistory.front().revision;
				_blockChangeHistory.pop_front();
			}
		}

		// Chunk load/unload replaces whole regions at once. Drop the log so
		// consumers rebuild fully instead of trusting a partial list.
		void invalidateBlockChangeHistory() {
			_blockChangeHistory.clear();
			_blockChangeHistoryBase = _blockRevision;
		}

		void rebuildEmitters(worldChunkGPU& target) const {
			target._emitters.clear();
			++target._emitterRevision;
			if (!target._chunk) return;

			const chunk& source = *target._chunk;
			for (uint32_t subchunkIndex = 0; subchunkIndex < CHUNK_SUBCHUNKS; ++subchunkIndex) {
				const subchunk& section = source._subchunks[subchunkIndex];
				bool containsEmitter = section.isSingle()
					? isEmissive(section.singleBlock())
					: std::any_of(section.palette().begin(), section.palette().end(),
						[this](blockId id) { return isEmissive(id); });
				if (!containsEmitter) continue;

				const uint32_t yStart = subchunkIndex * SUBCHUNK_HEIGHT;
				const uint32_t yEnd = std::min(yStart + SUBCHUNK_HEIGHT, static_cast<uint32_t>(CHUNK_HEIGHT));
				for (uint32_t y = yStart; y < yEnd; ++y) {
					for (uint32_t z = 0; z < CHUNK_LENGTH; ++z) {
						for (uint32_t x = 0; x < CHUNK_WIDTH; ++x) {
							const blockId id = source.getBlock(x, y, z);
							if (!isEmissive(id)) continue;
							target._emitters.push_back({
								{
									source._position.x * CHUNK_WIDTH + static_cast<int32_t>(x),
									static_cast<int32_t>(y),
									source._position.z * CHUNK_LENGTH + static_cast<int32_t>(z)
								},
								id
							});
						}
					}
				}
			}
		}

		void requestRemeshIfNeighborhoodChanged(int32_t x, int32_t z) {
			auto found = _chunks.find(key(x, z));
			if (found == _chunks.end()) return;
			const uint16_t desired = neighborMask(x, z);
			if (found->second._requestedNeighborMask == desired &&
				found->second._meshedNeighborMask == desired &&
				(!_gpuMeshing || found->second._hasGpuMesh)) return;
			found->second._requestedNeighborMask = desired;
			found->second._dirtySectionMask |= neighborhoodSectionMask(x, z);
			_remesh.insert(found->first);
		}

		void requestAffectedRemeshes(int32_t x, int32_t z) {
			for (int32_t dz = -1; dz <= 1; ++dz)
				for (int32_t dx = -1; dx <= 1; ++dx)
					requestRemeshIfNeighborhoodChanged(x + dx, z + dz);
		}

		void requestArea() {
			struct candidate {
				int32_t distanceSquared;
				int32_t x;
				int32_t z;
			};

			std::vector<candidate> candidates;
			candidates.reserve(static_cast<size_t>(_streamRadius * 2 + 1) * (_streamRadius * 2 + 1));

			for (int32_t z = _playerZ - _streamRadius; z <= _playerZ + _streamRadius; ++z) {
				for (int32_t x = _playerX - _streamRadius; x <= _playerX + _streamRadius; ++x) {
					const int32_t dx = x - _playerX;
					const int32_t dz = z - _playerZ;
					const int32_t distanceSquared = dx * dx + dz * dz;
					if (distanceSquared > _streamRadius * _streamRadius) continue;
					candidates.push_back({ distanceSquared, x, z });
				}
			}

			std::sort(candidates.begin(), candidates.end(), [](const candidate& a, const candidate& b) {
				return a.distanceSquared < b.distanceSquared;
				});

			std::vector<DirectX::XMINT3> batch;
			batch.reserve(candidates.size());
			for (const candidate& item : candidates) {
				const worldChunkKey k = key(item.x, item.z);
				if (_chunks.find(k) != _chunks.end()) continue;
				if (!_requested.insert(k).second) continue;
				batch.push_back({ item.x, 0, item.z });
			}
			_worker.requestMany(batch);
		}

		void unload() {
			for (auto it = _chunks.begin(); it != _chunks.end();) {
				const int32_t dx = it->first.x - _playerX;
				const int32_t dz = it->first.z - _playerZ;

				if (dx * dx + dz * dz > _unloadRadius * _unloadRadius) {
					const worldChunkKey removed = it->first;

					if (it->second._chunk && it->second._chunk->_dirty)
						_world->saveChunk(*it->second._chunk);

					_remesh.erase(it->first);
					_editRemeshDue.erase(it->first);
					it = _chunks.erase(it);
					++_blockRevision;
					++_shadowRevision;
					_localizedBlockRevision = 0;
					invalidateBlockChangeHistory();
					requestAffectedRemeshes(removed.x, removed.z);
				}
				else {
					++it;
				}
			}
		}

	public:
		static DirectX::BoundingBox chunkSolidBounds(const worldChunkGPU& worldChunk) {
			return solidChunkBounds(worldChunk);
		}
		static DirectX::BoundingBox chunkSectionBounds(const worldChunkGPU& worldChunk, uint32_t section) {
			return sectionBounds(worldChunk, section);
		}

		worldStreamer(
			world* world,
			staticAssetManager* blocks,
			modelManager* models,
			ID3D11Device* device,
			bool enableGpuTerrain = false,
			bool enableGpuMeshing = false,
			int streamRadius = 8,
			int unloadRadius = 10
		) : _world(world), _blocks(blocks), _models(models),
			_worker(world, blocks, models),
			_streamRadius(streamRadius),
			_unloadRadius(unloadRadius) {
			_chunks.reserve(512);
			_requested.reserve(512);
			_remesh.reserve(512);
			_remeshInFlight.reserve(512);
			_editRemeshDue.reserve(64);
			_redstonePhysics.setConductorQuery([this](blockId state) {
				const blockId type = blockType(state);
				if (type == 0u || isRedstoneComponent(type)) return false;
				const blockDefinition* definition = _blocks->get(type);
				if (definition && definition->_behavior.redstone != blockRedstoneBehavior::none)
					return false;
				return definition && definition->_solid && definition->_occludes &&
					definition->_model == MODEL_CUBE;
			});
			_redstonePhysics.setBehaviorQuery([this](blockId state) {
				const blockDefinition* definition = _blocks->get(blockType(state));
				return definition
					? definition->_behavior.redstone
					: blockRedstoneBehavior::none;
			});
			_redstonePhysics.setOutputQuery([this](blockId state) {
				const blockDefinition* definition = _blocks->get(blockType(state));
				if (!definition || definition->_behavior.redstoneOutput == 0u) return uint8_t{ 0 };
				const blockRedstoneBehavior role = definition->_behavior.redstone;
				if ((role == blockRedstoneBehavior::torch ||
					role == blockRedstoneBehavior::switchSource) && !isBlockPowered(state))
					return uint8_t{ 0 };
				return definition->_behavior.redstoneOutput;
			});
			if (enableGpuMeshing) {
				try {
					_gpuMesher.create(device, *blocks);
					_gpuMeshing = true;
					_worker.setGpuMeshing(true);
				}
				catch (const std::exception& error) {
					std::cerr << "GPU chunk meshing unavailable, using CPU fallback: " << error.what() << '\n';
				}
			}
			// The compute generator does not yet reproduce terrainGenerator's
			// 64-bit noise field exactly. Mixing the two backends makes newly
			// streamed chunks cut vertically through saved terrain, so generation
			// stays on the canonical background CPU worker until parity is proven.
			(void)enableGpuTerrain;
			_gpuTerrainReady = false;
			_gpuTerrain = false;
			_worker.setGpuTerrain(false);
		}

		~worldStreamer() {
			stop();
		}

		void stop() {
			_worker.stop();

			for (auto& pair : _chunks) {
				if (pair.second._chunk && pair.second._chunk->_dirty)
					_world->saveChunk(*pair.second._chunk);
			}
		}

		void saveLoadedChunks() {
			for (auto& pair : _chunks) {
				if (pair.second._chunk && pair.second._chunk->_dirty)
					_world->saveChunk(*pair.second._chunk);
			}
		}

		void suspendForResourceReload() {
			saveLoadedChunks();
			_worker.stop();
			_worker.clearPending();
		}

		void resumeAfterResourceReload(
			ID3D11DeviceContext* context,
			bool blockDefinitionsChanged
		) {
			if (blockDefinitionsChanged && _gpuMeshing)
				_gpuMesher.reloadDefinitions(context, *_blocks);
			discardAllChunks();
			_worker.start();
		}

		void unloadAllChunks(bool saveDirty) {
			_worker.clearPending();
			if (saveDirty)
				saveLoadedChunks();
			discardAllChunks();
		}

		// Drop all loaded chunks and pending jobs without writing them. Used when
		// starting a brand-new world so old terrain cannot leak into the save.
		void discardAllChunks() {
			_worker.clearPending();
			_chunks.clear();
			_requested.clear();
			_remesh.clear();
			_editRemeshDue.clear();
			_fallingPhysics.clear();
			_redstonePhysics.clear();
			_playerX = INT32_MIN;
			_playerZ = INT32_MIN;
			++_blockRevision;
			++_shadowRevision;
			_localizedBlockRevision = 0;
			invalidateBlockChangeHistory();
		}

		void update(
			const DirectX::XMFLOAT3& position,
			ID3D11Device* device,
			ID3D11DeviceContext* context
		) {
			const int32_t cx = worldToChunk(position.x, CHUNK_WIDTH);
			const int32_t cz = worldToChunk(position.z, CHUNK_LENGTH);

			if (cx != _playerX || cz != _playerZ) {
				_playerX = cx;
				_playerZ = cz;
				for (const auto& cancelled : _worker.setGenerationCenter(cx, cz, _streamRadius))
					_requested.erase(key(cancelled.x, cancelled.z));
				requestArea();
				unload();
			}

			completedChunk completed;

			const size_t uploadBudget = _gpuTerrain
				? MAX_GPU_TERRAIN_UPLOADS_PER_FRAME
				: MAX_MESH_UPLOADS_PER_FRAME;
			for (size_t uploaded = 0;
				uploaded < uploadBudget && _worker.poll(completed);
				++uploaded) {
				const worldChunkKey k = key(
					completed.position.x,
					completed.position.z
				);

				if (completed.remesh) {
					_remeshInFlight.erase(k);
					auto existing = _chunks.find(k);
					if (existing == _chunks.end())
						continue;

					const uint32_t sectionBit = 1u << completed.section;
					if ((existing->second._dirtySectionMask & sectionBit) == 0u) {
						uploadSectionMesh(existing->second, completed.section, completed.mesh, device, context);
						refreshSolidBounds(existing->second);
					}
					existing->second._meshedNeighborMask = completed.neighborMask;
					if (existing->second._dirtySectionMask != 0u)
						_remesh.insert(k);
					continue;
				}

				_requested.erase(k);

				const int32_t dx = k.x - _playerX;
				const int32_t dz = k.z - _playerZ;
				if (dx * dx + dz * dz > _unloadRadius * _unloadRadius)
					continue;

				if (_chunks.find(k) != _chunks.end())
					continue;

				if (_gpuTerrain && completed.generated && !completed.data) {
					const auto generationStart = std::chrono::steady_clock::now();
					try {
						completed.data = _gpuTerrainGenerator.generate(context, completed.position, _world->seed());
					}
					catch (const std::exception& error) {
						std::cerr << "GPU terrain chunk failed, using CPU fallback: " << error.what() << '\n';
						auto fallback = std::make_shared<chunk>();
						fallback->_position = completed.position;
						_world->generateChunk(*fallback);
						completed.data = std::move(fallback);
					}
					completed.loadOrGenerateNanoseconds += static_cast<uint64_t>(
						std::chrono::duration_cast<std::chrono::nanoseconds>(
							std::chrono::steady_clock::now() - generationStart
						).count()
					);
				}

				worldChunkGPU gpuChunk;
				gpuChunk._chunk = std::move(completed.data);
				gpuChunk.bakeWorldTransform();
				rebuildEmitters(gpuChunk);

				if (!_gpuMeshing) {
					uploadSectionMeshes(gpuChunk, completed.sectionMeshes, device, context);
					refreshSolidBounds(gpuChunk);
				}

				_chunks.emplace(k, std::move(gpuChunk));
				++_blockRevision;
				++_shadowRevision;
				_localizedBlockRevision = 0;
				invalidateBlockChangeHistory();
				if (_gpuMeshing) {
					auto inserted = _chunks.find(k);
					_gpuMesher.uploadVoxelBuffer(device, context, *inserted->second._chunk, inserted->second._gpuVoxels);
				}
				else if (completed.generated && completed.sectionMeshes[0]._opaque._vertex.empty() &&
					completed.sectionMeshes[0]._translucent._vertex.empty()) {
					auto inserted = _chunks.find(k);
					inserted->second._dirtySectionMask = neighborhoodSectionMask(k.x, k.z);
					_remesh.insert(k);
				}

				// The worker's initial mesh has no neighbor data. Refresh the new
				// chunk and every existing AO/visibility neighbor whose eight-cell
				// neighborhood signature changed.
				requestAffectedRemeshes(k.x, k.z);
				// Generated interiors are stable until a nearby event occurs. Only
				// reconcile the newly available border with adjacent chunks.
				seedWaterBoundaries(k);
				seedRedstone(k);
			}

			const auto remeshNow = std::chrono::steady_clock::now();
			for (auto pending = _editRemeshDue.begin(); pending != _editRemeshDue.end();) {
				if (pending->second > remeshNow) {
					++pending;
					continue;
				}
				_remesh.insert(pending->first);
				pending = _editRemeshDue.erase(pending);
			}

			const size_t remeshBudget = _gpuMeshing ? 16u : MAX_REMESH_REQUESTS_PER_FRAME;
			for (size_t rebuilt = 0; rebuilt < remeshBudget && !_remesh.empty(); ++rebuilt) {
				auto nearest = _remesh.end();
				int64_t nearestDistance = INT64_MAX;
				for (auto candidate = _remesh.begin(); candidate != _remesh.end(); ++candidate) {
					if (_remeshInFlight.find(*candidate) != _remeshInFlight.end())
						continue;
					const int64_t dx = static_cast<int64_t>(candidate->x) - _playerX;
					const int64_t dz = static_cast<int64_t>(candidate->z) - _playerZ;
					const int64_t distance = dx * dx + dz * dz;
					if (distance < nearestDistance) {
						nearestDistance = distance;
						nearest = candidate;
					}
				}
				if (nearest == _remesh.end())
					break;

				const worldChunkKey k = *nearest;
				_remesh.erase(nearest);
				if (_gpuMeshing)
					buildGpuMesh(k, device, context);
				else
					queueRemesh(k);
			}

		}

		void forceAffectedRemeshes(int32_t x, int32_t z) {
			for (int32_t dz = -1; dz <= 1; ++dz) {
				for (int32_t dx = -1; dx <= 1; ++dx) {
					auto found = _chunks.find(key(x + dx, z + dz));
					if (found == _chunks.end() || !found->second._chunk)
						continue;
					found->second._requestedNeighborMask = neighborMask(x + dx, z + dz);
					found->second._dirtySectionMask |= neighborhoodSectionMask(x + dx, z + dz);
					_remesh.insert(found->first);
				}
			}
		}

		void forceBlockEditRemeshes(
			int32_t chunkX,
			int32_t chunkZ,
			uint32_t localX,
			uint32_t localY,
			uint32_t localZ,
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			bool immediateGpuRemesh
		) {
			const uint32_t section = localY / SUBCHUNK_HEIGHT;
			uint32_t sectionMask = 1u << section;
			if (localY % SUBCHUNK_HEIGHT == 0u && section > 0u)
				sectionMask |= 1u << (section - 1u);
			if (localY % SUBCHUNK_HEIGHT + 1u == SUBCHUNK_HEIGHT && section + 1u < CHUNK_SUBCHUNKS)
				sectionMask |= 1u << (section + 1u);
			auto schedule = [&](int32_t x, int32_t z) {
				auto found = _chunks.find(key(x, z));
				if (found == _chunks.end() || !found->second._chunk) return;
				found->second._requestedNeighborMask = neighborMask(x, z);
			found->second._dirtySectionMask |= sectionMask;
				if (_gpuMeshing && immediateGpuRemesh) {
					// Block interaction happens before rendering. Rebuild its GPU mesh
					// now so the indirect draw in this frame sees the edit instead of
					// making it wait behind terrain and neighborhood remesh work.
					_editRemeshDue.erase(found->first);
					_remesh.erase(found->first);
					buildGpuMesh(found->first, device, context);
					return;
				}
				_editRemeshDue.try_emplace(
					found->first,
					std::chrono::steady_clock::now() + std::chrono::milliseconds(50)
				);
			};
			schedule(chunkX, chunkZ);
			const int32_t edgeX = localX == 0u ? -1 : (localX + 1u == CHUNK_WIDTH ? 1 : 0);
			const int32_t edgeZ = localZ == 0u ? -1 : (localZ + 1u == CHUNK_LENGTH ? 1 : 0);
			if (edgeX != 0) schedule(chunkX + edgeX, chunkZ);
			if (edgeZ != 0) schedule(chunkX, chunkZ + edgeZ);
			if (edgeX != 0 && edgeZ != 0) schedule(chunkX + edgeX, chunkZ + edgeZ);
		}

		static int32_t floorDiv(int32_t value, int32_t divisor) {
			const int64_t wide = value;
			return wide >= 0
				? static_cast<int32_t>(wide / divisor)
				: static_cast<int32_t>(-((-wide + divisor - 1) / divisor));
		}

	private:
		void buildGpuMesh(const worldChunkKey& k, ID3D11Device* device, ID3D11DeviceContext* context) {
			auto target = _chunks.find(k);
			if (target == _chunks.end() || !target->second._chunk) return;
			auto voxels=[&](int32_t x,int32_t z)->const gpuChunkVoxelBuffer* { auto found=_chunks.find(key(x,z)); return found==_chunks.end()?nullptr:&found->second._gpuVoxels; };
			const std::array<const gpuChunkVoxelBuffer*, 8> neighbors = {
				voxels(k.x-1,k.z),voxels(k.x+1,k.z),voxels(k.x,k.z-1),voxels(k.x,k.z+1),
				voxels(k.x-1,k.z-1),voxels(k.x-1,k.z+1),voxels(k.x+1,k.z-1),voxels(k.x+1,k.z+1)
			};
			_gpuMesher.buildMeshGpu(
				device, context, target->second._gpuVoxels, neighbors,
				target->second._gpuFaces,
				gpuChunkMesherPrototype::occupiedSubchunkMask(*target->second._chunk)
			);

			auto snapshot = [&](int32_t x, int32_t z) -> const chunk* {
				auto found = _chunks.find(key(x, z));
				return found == _chunks.end() ? nullptr : found->second._chunk.get();
			};
			uint32_t doorMask = target->second._dirtySectionMask;
			if (doorMask == 0u || !target->second._hasGpuMesh)
				doorMask = (1u << CHUNK_SUBCHUNKS) - 1u;
			const chunkSectionMeshes doorMeshes = _detailMesher.generateDoorSections(
				*target->second._chunk, *_blocks, *_models,
				snapshot(k.x - 1, k.z), snapshot(k.x + 1, k.z),
				snapshot(k.x, k.z - 1), snapshot(k.x, k.z + 1),
				snapshot(k.x - 1, k.z - 1), snapshot(k.x - 1, k.z + 1),
				snapshot(k.x + 1, k.z - 1), snapshot(k.x + 1, k.z + 1),
				doorMask
			);
			for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
				if (((doorMask >> section) & 1u) == 0u) continue;
				uploadSectionMesh(target->second, section, doorMeshes[section], device, context);
			}

			target->second._hasGpuMesh = true;
			target->second._dirtySectionMask = 0u;
			target->second._meshedNeighborMask = neighborMask(k.x, k.z);
			refreshSolidBounds(target->second);
		}

		static void refreshSolidBounds(worldChunkGPU& target) {
			if (!target._chunk) {
				target._solidMinY = 0;
				target._solidMaxY = CHUNK_HEIGHT;
				return;
			}
			int minY = CHUNK_HEIGHT;
			int maxY = 0;
			for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
				if (target._chunk->isSubchunkEmpty(section) &&
					target._opaqueSections[section]._index.count() == 0 &&
					target._translucentSections[section]._index.count() == 0)
					continue;
				minY = (std::min)(minY, static_cast<int>(section * SUBCHUNK_HEIGHT));
				maxY = (std::max)(maxY, static_cast<int>((section + 1u) * SUBCHUNK_HEIGHT));
			}
			if (minY >= maxY) {
				minY = 0;
				maxY = 1;
			}
			target._solidMinY = minY;
			target._solidMaxY = maxY;
		}

		static DirectX::BoundingBox solidChunkBounds(const worldChunkGPU& worldChunk) {
			const auto& position = worldChunk._chunk->_position;
			const float minY = static_cast<float>(worldChunk._solidMinY);
			const float maxY = static_cast<float>(worldChunk._solidMaxY);
			return DirectX::BoundingBox(
				{
					static_cast<float>(position.x * CHUNK_WIDTH) + CHUNK_WIDTH * 0.5f,
					(minY + maxY) * 0.5f,
					static_cast<float>(position.z * CHUNK_LENGTH) + CHUNK_LENGTH * 0.5f
				},
				{
					CHUNK_WIDTH * 0.5f,
					(std::max)((maxY - minY) * 0.5f, 0.5f),
					CHUNK_LENGTH * 0.5f
				}
			);
		}

		static DirectX::BoundingBox sectionBounds(const worldChunkGPU& worldChunk, uint32_t section) {
			const auto& position = worldChunk._chunk->_position;
			const float minY = static_cast<float>(section * SUBCHUNK_HEIGHT);
			const float maxY = minY + static_cast<float>(SUBCHUNK_HEIGHT);
			return DirectX::BoundingBox(
				{
					static_cast<float>(position.x * CHUNK_WIDTH) + CHUNK_WIDTH * 0.5f,
					(minY + maxY) * 0.5f,
					static_cast<float>(position.z * CHUNK_LENGTH) + CHUNK_LENGTH * 0.5f
				},
				{ CHUNK_WIDTH * 0.5f, SUBCHUNK_HEIGHT * 0.5f, CHUNK_LENGTH * 0.5f }
			);
		}

		static void uploadSectionMesh(worldChunkGPU& target, uint32_t section, const chunkMesh& mesh, ID3D11Device* device, ID3D11DeviceContext* context) {
			target._opaqueSections[section]._vertex.upload(device, context, mesh._opaque._vertex);
			target._opaqueSections[section]._index.upload(device, context, mesh._opaque._index);
			target._translucentSections[section]._vertex.upload(device, context, mesh._translucent._vertex);
			target._translucentSections[section]._index.upload(device, context, mesh._translucent._index);
		}

		static void uploadSectionMeshes(worldChunkGPU& target, const chunkSectionMeshes& meshes, ID3D11Device* device, ID3D11DeviceContext* context) {
			for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section)
				uploadSectionMesh(target, section, meshes[section], device, context);
		}

		void queueRemesh(const worldChunkKey& k) {
			auto it = _chunks.find(k);
			if (it == _chunks.end() || !it->second._chunk) return;
			if (!_remeshInFlight.insert(k).second) return;
			if (it->second._dirtySectionMask == 0u) {
				_remeshInFlight.erase(k);
				return;
			}
			const uint32_t section = static_cast<uint32_t>(std::countr_zero(it->second._dirtySectionMask));
			it->second._dirtySectionMask &= ~(1u << section);

			auto snapshot = [&](int32_t x, int32_t z) -> std::shared_ptr<const chunk> {
				auto found = _chunks.find(key(x, z));
				return found == _chunks.end() ? nullptr : found->second._chunk;
			};
			const auto negX = snapshot(k.x - 1, k.z);
			const auto posX = snapshot(k.x + 1, k.z);
			const auto negZ = snapshot(k.x, k.z - 1);
			const auto posZ = snapshot(k.x, k.z + 1);
			const auto negXNegZ = snapshot(k.x - 1, k.z - 1);
			const auto negXPosZ = snapshot(k.x - 1, k.z + 1);
			const auto posXNegZ = snapshot(k.x + 1, k.z - 1);
			const auto posXPosZ = snapshot(k.x + 1, k.z + 1);

			_worker.requestRemesh(
				it->second._chunk,
				{
				negX,
				posX,
				negZ,
				posZ,
				negXNegZ,
				negXPosZ,
				posXNegZ,
				posXPosZ
				},
				section
			);
		}

		std::optional<blockId> readBlockOptional(int32_t worldX, int32_t worldY, int32_t worldZ) const {
			if (worldY < 0 || worldY >= CHUNK_HEIGHT) return blockId{ 0u };
			const int32_t chunkX = floorDiv(worldX, CHUNK_WIDTH);
			const int32_t chunkZ = floorDiv(worldZ, CHUNK_LENGTH);
			const chunk* source = getChunk(chunkX, chunkZ);
			if (!source) return std::nullopt;
			return source->getBlock(
				static_cast<uint32_t>(worldX - chunkX * CHUNK_WIDTH),
				static_cast<uint32_t>(worldY),
				static_cast<uint32_t>(worldZ - chunkZ * CHUNK_LENGTH));
		}

		redstonePhysics::blockReader physicsBlockReader() const {
			return [this](int32_t worldX, int32_t worldY, int32_t worldZ) {
				return readBlockOptional(worldX, worldY, worldZ);
			};
		}

		fallingBlockPhysics::gravityQuery gravityBlockQuery() const {
			return [this](blockId id) {
				const blockDefinition* definition = _blocks->get(blockType(id));
				return definition && definition->_gravity;
			};
		}

	public:
		bool gpuMeshingEnabled() const { return _gpuMeshing; }
		bool gpuTerrainEnabled() const { return _gpuTerrain; }
		bool gpuTerrainAvailable() const { return _gpuTerrainReady; }
		int streamRadius() const { return _streamRadius; }
		bool chunkLoaded(int32_t x, int32_t z) const { return getChunk(x, z) != nullptr; }
		std::optional<blockId> tryBlockAt(int32_t worldX, int32_t worldY, int32_t worldZ) const {
			return readBlockOptional(worldX, worldY, worldZ);
		}
		void setBlockChangeHooks(beforeBlockChange before, afterBlockChange after) {
			_beforeBlockChange = std::move(before);
			_afterBlockChange = std::move(after);
		}

		void setGpuTerrainEnabled(bool enabled) {
			_gpuTerrain = enabled && _gpuTerrainReady;
			_worker.setGpuTerrain(_gpuTerrain);
		}

		void setViewDistance(int radius) {
			_streamRadius = std::clamp(radius, 3, 16);
			_unloadRadius = _streamRadius + 2;
			_playerX = INT32_MIN;
		}

		struct blockReadCache {
			int32_t chunkX = INT32_MIN;
			int32_t chunkZ = INT32_MIN;
			const chunk* item = nullptr;
		};

		blockId blockAt(int32_t worldX, int32_t worldY, int32_t worldZ) const {
			blockReadCache cache;
			return blockAt(worldX, worldY, worldZ, cache);
		}

		blockId blockAt(int32_t worldX, int32_t worldY, int32_t worldZ, blockReadCache& cache) const {
			if (worldY < 0 || worldY >= CHUNK_HEIGHT)
				return 0;
			const int32_t chunkX = floorDiv(worldX, CHUNK_WIDTH);
			const int32_t chunkZ = floorDiv(worldZ, CHUNK_LENGTH);
			if (cache.chunkX != chunkX || cache.chunkZ != chunkZ) {
				cache.chunkX = chunkX;
				cache.chunkZ = chunkZ;
				cache.item = getChunk(chunkX, chunkZ);
			}
			if (!cache.item)
				return 0;
			return cache.item->getBlock(
				static_cast<uint32_t>(worldX - chunkX * CHUNK_WIDTH),
				static_cast<uint32_t>(worldY),
				static_cast<uint32_t>(worldZ - chunkZ * CHUNK_LENGTH)
			);
		}

		uint8_t waterLevelAt(int32_t worldX, int32_t worldY, int32_t worldZ) const {
			return fluidLevel(blockAt(worldX, worldY, worldZ), WATER_BLOCK_TYPE);
		}

		uint8_t redstoneInputPowerAt(int32_t worldX, int32_t worldY, int32_t worldZ) const {
			return _redstonePhysics.inputPowerAt(
				physicsBlockReader(), worldX, worldY, worldZ);
		}

		size_t updateWaterPhysics(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			size_t updateBudget = 256u
		) {
			const auto write = [this, device, context](int32_t x, int32_t y, int32_t z, blockId state) {
				return setBlock(x, y, z, state, device, context, false, false, false, true);
			};
			return _waterPhysics.step(updateBudget, physicsBlockReader(), write);
		}

		size_t updateRedstonePhysics(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			size_t updateBudget = 256u,
			bool advanceTick = true
		) {
			const auto write = [this, device, context](int32_t x, int32_t y, int32_t z, blockId state) {
				// Coalesce the section remeshes required by dust tint changes.
				return setBlock(x, y, z, state, device, context, false, false, false, false);
			};
			return _redstonePhysics.step(
				updateBudget, physicsBlockReader(), write, advanceTick);
		}

		void setRedstonePoweredDeviceUpdate(redstonePhysics::poweredDeviceUpdate update) {
			_redstonePhysics.setPoweredDeviceUpdate(std::move(update));
		}
		void setRedstoneAnalogQuery(redstonePhysics::analogQuery query) {
			_redstonePhysics.setAnalogQuery(std::move(query));
		}
		void setRedstoneSupportRules(redstonePhysics::conductorQuery support, redstonePhysics::dropDevice drop) {
			_redstonePhysics.setSupportRules(std::move(support), std::move(drop));
		}

		fallingBlockPhysics::blockReader fallingBlockReader() {
			return [this](int32_t worldX, int32_t worldY, int32_t worldZ)
				-> std::optional<blockId> {
				if (worldY < 0) return blockId{ 1u }; // treat void floor as support
				if (worldY >= CHUNK_HEIGHT) return blockId{ 0u };
				const int32_t chunkX = floorDiv(worldX, CHUNK_WIDTH);
				const int32_t chunkZ = floorDiv(worldZ, CHUNK_LENGTH);
				const chunk* source = getChunk(chunkX, chunkZ);
				if (!source) return std::nullopt;
				return source->getBlock(
					static_cast<uint32_t>(worldX - chunkX * CHUNK_WIDTH),
					static_cast<uint32_t>(worldY),
					static_cast<uint32_t>(worldZ - chunkZ * CHUNK_LENGTH));
			};
		}

		fallingBlockPhysics::blockWriter fallingBlockWriter(
			ID3D11Device* device,
			ID3D11DeviceContext* context
		) {
			return [this, device, context](int32_t x, int32_t y, int32_t z, blockId state) {
				// Always remesh immediately so falling entities never double-draw
				// against stale voxel geometry (start) or pop in late (land).
				return setBlock(x, y, z, state, device, context, true, true, true, false);
			};
		}

		fallingBlockPhysics::supportQuery fallingBlockSupport() {
			return [this](blockId id) {
				if (id == 0u) return false;
				if (blockType(id) == WATER_BLOCK_TYPE) return false;
				const blockDefinition* definition = _blocks->get(blockType(id));
				return definition && definition->_solid;
			};
		}

		size_t updateFallingPhysics(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			size_t updateBudget = 128u
		) {
			const auto read = fallingBlockReader();
			const auto write = fallingBlockWriter(device, context);
			const auto isGravity = [this](blockId id) {
				const blockDefinition* definition = _blocks->get(blockType(id));
				return definition && definition->_gravity;
			};
			const auto canSupport = fallingBlockSupport();
			return _fallingPhysics.step(updateBudget, read, write, isGravity, canSupport);
		}

		size_t updateFallingEntities(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			float deltaTime,
			const fallingBlockPhysics::occupyQuery& occupy = {},
			std::vector<fallingBlockEntity>* crushed = nullptr
		) {
			if (_fallingPhysics.entities().empty()) return 0;
			return _fallingPhysics.updateEntities(
				deltaTime,
				fallingBlockReader(),
				fallingBlockWriter(device, context),
				gravityBlockQuery(),
				fallingBlockSupport(),
				occupy,
				crushed);
		}

		const std::vector<fallingBlockEntity>& fallingEntities() const {
			return _fallingPhysics.entities();
		}

		std::vector<fallingBlockEntity>& fallingEntities() {
			return _fallingPhysics.entities();
		}

		bool setWaterLevel(
			int32_t worldX,
			int32_t worldY,
			int32_t worldZ,
			uint8_t level,
			ID3D11Device* device,
			ID3D11DeviceContext* context
		) {
			return setBlock(
				worldX,
				worldY,
				worldZ,
				withFluidLevel(WATER_BLOCK_TYPE, level),
				device,
				context);
		}

		bool setBlock(
			int32_t worldX,
			int32_t worldY,
			int32_t worldZ,
			blockId id,
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			bool notifyWater = true,
			bool immediateGpuRemesh = true,
			bool notifyRedstone = true,
			bool notifyFalling = true
		) {
			if (worldY < 0 || worldY >= CHUNK_HEIGHT || (id != 0 && !_blocks->get(blockType(id))))
				return false;

			const int32_t chunkX = floorDiv(worldX, CHUNK_WIDTH);
			const int32_t chunkZ = floorDiv(worldZ, CHUNK_LENGTH);
			auto found = _chunks.find(key(chunkX, chunkZ));
			if (found == _chunks.end() || !found->second._chunk)
				return false;

			const uint32_t localX = static_cast<uint32_t>(worldX - chunkX * CHUNK_WIDTH);
			const uint32_t localZ = static_cast<uint32_t>(worldZ - chunkZ * CHUNK_LENGTH);
			const blockId previousId = found->second._chunk->getBlock(
				localX, static_cast<uint32_t>(worldY), localZ);
			if (_beforeBlockChange &&
				!_beforeBlockChange(worldX, worldY, worldZ, previousId, id))
				return false;
			if (id != 0 && !_blocks->get(blockType(id))) return false;
			if (previousId == id)
				return false;
			// Most power-only changes do not alter geometry. Redstone dust is the
			// exception: strength is packed into its detail-mesh vertices for tinting.
			const blockId powerMask = REDSTONE_POWER_MASK << REDSTONE_POWER_SHIFT;
			const bool meshRelevantChanged =
				((previousId ^ id) & ~powerMask) != 0u ||
				redstoneWireVisualStateChanged(previousId, id) ||
				(isRedstoneLamp(id) && redstonePower(previousId) != redstonePower(id));
			const auto castsWorldShadow = [this](blockId state) {
				if (state == 0u) return false;
				const blockDefinition* definition = _blocks->get(blockType(state));
				return definition && definition->_occludes &&
					definition->_renderMode != RENDER_MODE_TRANSPARENT;
			};

			std::shared_ptr<chunk> edited;
			if (found->second._chunk.use_count() == 1)
				edited = std::const_pointer_cast<chunk>(found->second._chunk);
			else
				edited = std::make_shared<chunk>(*found->second._chunk);
			edited->setBlock(localX, static_cast<uint32_t>(worldY), localZ, id);
			found->second._chunk = edited;
			++_blockRevision;
			if (castsWorldShadow(previousId) || castsWorldShadow(id))
				++_shadowRevision;
			_localizedBlockRevision = _blockRevision;
			_localizedBlockPosition = { worldX, worldY, worldZ };
			recordBlockChange(worldX, worldY, worldZ);
			auto& emitters = found->second._emitters;
			const size_t emittersBefore = emitters.size();
			emitters.erase(
				std::remove_if(emitters.begin(), emitters.end(), [&](const worldChunkGPU::blockEmitter& emitter) {
					return emitter.position.x == worldX && emitter.position.y == worldY && emitter.position.z == worldZ;
				}),
				emitters.end()
			);
			const bool removedEmitter = emitters.size() != emittersBefore;
			const bool addedEmitter = isEmissive(id);
			if (addedEmitter)
				emitters.push_back({ { worldX, worldY, worldZ }, id });
			if (removedEmitter || addedEmitter)
				++found->second._emitterRevision;
			if (meshRelevantChanged) {
				if (_gpuMeshing)
					_gpuMesher.updateVoxel(
						context, *edited, found->second._gpuVoxels,
						localX, static_cast<uint32_t>(worldY), localZ);
				forceBlockEditRemeshes(
					chunkX, chunkZ, localX, static_cast<uint32_t>(worldY), localZ,
					device, context, immediateGpuRemesh
				);
				const auto read = physicsBlockReader();
				if (notifyWater)
					_waterPhysics.notifyBlockChanged(worldX, worldY, worldZ, read, true);
				if (notifyFalling)
					_fallingPhysics.notifyBlockChanged(
						worldX, worldY, worldZ, fallingBlockReader(), gravityBlockQuery(), true);
			}
			if (notifyRedstone)
				_redstonePhysics.notifyBlockChanged(
					worldX, worldY, worldZ, previousId, id, physicsBlockReader(), true);
			if (_afterBlockChange)
				_afterBlockChange(worldX, worldY, worldZ, previousId, id);
			return true;
		}

		const auto& chunks() const {
			return _chunks;
		}

		uint64_t blockRevision() const { return _blockRevision; }
		uint64_t shadowRevision() const { return _shadowRevision; }

		// Cells edited after `revision`. Returns false when the log no longer
		// reaches that far back, meaning the caller must rebuild everything.
		bool blockChangesSince(
			uint64_t revision,
			std::vector<DirectX::XMINT3>& positions
		) const {
			positions.clear();
			if (revision == _blockRevision)
				return true;
			if (revision < _blockChangeHistoryBase)
				return false;
			for (const blockChangeRecord& record : _blockChangeHistory) {
				if (record.revision > revision)
					positions.push_back(record.position);
			}
			return true;
		}

		bool localizedBlockChange(uint64_t revision, DirectX::XMINT3& position) const {
			if (revision == 0u || revision != _localizedBlockRevision)
				return false;
			position = _localizedBlockPosition;
			return true;
		}

		auto& chunks() {
			return _chunks;
		}
	};

}
