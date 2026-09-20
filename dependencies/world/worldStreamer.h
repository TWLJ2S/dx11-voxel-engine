#pragma once

#include "chunkWorker.h"
#include "world.h"
#include <assetManager/assetManager.h>
#include <renderer/buffer.h>
#include "gpuChunkMesher.h"
#include "gpuTerrainGenerator.h"

#include <DirectXMath.h>
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
		bool _hasGpuMesh = false;
		uint16_t _meshedNeighborMask = 0;
		uint16_t _requestedNeighborMask = 0;
		uint32_t _dirtySectionMask = 0;
		std::vector<blockEmitter> _emitters;
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
		bool _gpuMeshing = false;
		gpuTerrainGenerator _gpuTerrainGenerator;
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
		uint64_t _localizedBlockRevision = 0;
		DirectX::XMINT3 _localizedBlockPosition{};

		static constexpr size_t MAX_MESH_UPLOADS_PER_FRAME = 1;
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
			const blockDefinition* definition = id ? _blocks->get(id) : nullptr;
			return definition && definition->_emission.intensity > 0.0f &&
				definition->_emission.radius > 0.0f;
		}

		void rebuildEmitters(worldChunkGPU& target) const {
			target._emitters.clear();
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
				(!_gpuMeshing || found->second._hasGpuMesh)) return;
			found->second._requestedNeighborMask = desired;
			found->second._dirtySectionMask |= _gpuMeshing
				? ALL_SECTION_MASK : neighborhoodSectionMask(x, z);
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

			for (const candidate& item : candidates)
				request(item.x, item.z);
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
					_localizedBlockRevision = 0;
					requestAffectedRemeshes(removed.x, removed.z);
				}
				else {
					++it;
				}
			}
		}

	public:
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
			if (enableGpuTerrain) {
				try {
					_gpuTerrainGenerator.create(device);
					_gpuTerrain = true;
					_worker.setGpuTerrain(true);
				}
				catch (const std::exception& error) {
					std::cerr << "GPU terrain generation unavailable, using CPU fallback: " << error.what() << '\n';
				}
			}
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

			for (size_t uploaded = 0;
				uploaded < MAX_MESH_UPLOADS_PER_FRAME && _worker.poll(completed);
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
					if ((existing->second._dirtySectionMask & sectionBit) == 0u)
						uploadSectionMesh(existing->second, completed.section, completed.mesh, device, context);
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
				rebuildEmitters(gpuChunk);

				if (!_gpuMeshing)
					uploadSectionMeshes(gpuChunk, completed.sectionMeshes, device, context);

				_chunks.emplace(k, std::move(gpuChunk));
				++_blockRevision;
				_localizedBlockRevision = 0;
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

			const size_t remeshBudget = _gpuMeshing ? 8u : MAX_REMESH_REQUESTS_PER_FRAME;
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
					found->second._dirtySectionMask |= _gpuMeshing
						? ALL_SECTION_MASK : neighborhoodSectionMask(x + dx, z + dz);
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
			ID3D11DeviceContext* context
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
				found->second._dirtySectionMask |= _gpuMeshing ? ALL_SECTION_MASK : sectionMask;
				if (_gpuMeshing) {
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
				target->second._gpuFaces
			);
			target->second._hasGpuMesh = true;
			target->second._dirtySectionMask = 0u;
			target->second._meshedNeighborMask = neighborMask(k.x, k.z);
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

	public:
		bool gpuMeshingEnabled() const { return _gpuMeshing; }
		bool gpuTerrainEnabled() const { return _gpuTerrain; }

		blockId blockAt(int32_t worldX, int32_t worldY, int32_t worldZ) const {
			if (worldY < 0 || worldY >= CHUNK_HEIGHT)
				return 0;
			const int32_t chunkX = floorDiv(worldX, CHUNK_WIDTH);
			const int32_t chunkZ = floorDiv(worldZ, CHUNK_LENGTH);
			const chunk* item = getChunk(chunkX, chunkZ);
			if (!item)
				return 0;
			return item->getBlock(
				static_cast<uint32_t>(worldX - chunkX * CHUNK_WIDTH),
				static_cast<uint32_t>(worldY),
				static_cast<uint32_t>(worldZ - chunkZ * CHUNK_LENGTH)
			);
		}

		bool setBlock(
			int32_t worldX,
			int32_t worldY,
			int32_t worldZ,
			blockId id,
			ID3D11Device* device,
			ID3D11DeviceContext* context
		) {
			if (worldY < 0 || worldY >= CHUNK_HEIGHT || (id != 0 && !_blocks->get(id)))
				return false;

			const int32_t chunkX = floorDiv(worldX, CHUNK_WIDTH);
			const int32_t chunkZ = floorDiv(worldZ, CHUNK_LENGTH);
			auto found = _chunks.find(key(chunkX, chunkZ));
			if (found == _chunks.end() || !found->second._chunk)
				return false;

			const uint32_t localX = static_cast<uint32_t>(worldX - chunkX * CHUNK_WIDTH);
			const uint32_t localZ = static_cast<uint32_t>(worldZ - chunkZ * CHUNK_LENGTH);
			if (found->second._chunk->getBlock(localX, static_cast<uint32_t>(worldY), localZ) == id)
				return false;

			std::shared_ptr<chunk> edited;
			if (found->second._chunk.use_count() == 1)
				edited = std::const_pointer_cast<chunk>(found->second._chunk);
			else
				edited = std::make_shared<chunk>(*found->second._chunk);
			edited->setBlock(localX, static_cast<uint32_t>(worldY), localZ, id);
			found->second._chunk = edited;
			++_blockRevision;
			_localizedBlockRevision = _blockRevision;
			_localizedBlockPosition = { worldX, worldY, worldZ };
			auto& emitters = found->second._emitters;
			emitters.erase(
				std::remove_if(emitters.begin(), emitters.end(), [&](const worldChunkGPU::blockEmitter& emitter) {
					return emitter.position.x == worldX && emitter.position.y == worldY && emitter.position.z == worldZ;
				}),
				emitters.end()
			);
			if (isEmissive(id))
				emitters.push_back({ { worldX, worldY, worldZ }, id });
			if (_gpuMeshing)
				_gpuMesher.updateVoxel(
					context, *edited, found->second._gpuVoxels,
					localX, static_cast<uint32_t>(worldY), localZ);
			forceBlockEditRemeshes(
				chunkX, chunkZ, localX, static_cast<uint32_t>(worldY), localZ,
				device, context
			);
			return true;
		}

		const auto& chunks() const {
			return _chunks;
		}

		uint64_t blockRevision() const { return _blockRevision; }

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
