#pragma once
#include "world.h"
#include <assetManager/assetManager.h>

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <atomic>
#include <vector>
#include <algorithm>
#include <array>
#include <chrono>

namespace ac {

	struct chunkJob {
		DirectX::XMINT3 position;
		std::shared_ptr<const chunk> data;
		std::array<std::shared_ptr<const chunk>, 8> neighbors;
		int64_t priority = 0;
		uint64_t sequence = 0;
		uint64_t snapshotNanoseconds = 0;
		uint16_t neighborMask = 0;
		uint32_t section = UINT32_MAX;
		std::chrono::steady_clock::time_point queuedAt = std::chrono::steady_clock::now();

		bool isRemesh() const { return data != nullptr; }
	};

	struct completedChunk {
		DirectX::XMINT3 position;
		std::shared_ptr<const chunk> data;
		chunkMesh mesh;
		chunkSectionMeshes sectionMeshes;
		bool remesh = false;
		bool generated = false;
		uint64_t queueNanoseconds = 0;
		uint64_t loadOrGenerateNanoseconds = 0;
		uint64_t snapshotNanoseconds = 0;
		chunkMeshTimings meshing;
		uint16_t neighborMask = 0;
		uint32_t section = UINT32_MAX;
	};

	class chunkWorker {
	private:
		struct generationJobCompare {
			bool operator()(const chunkJob& a, const chunkJob& b) const {
				if (a.priority != b.priority) return a.priority > b.priority;
				return a.sequence > b.sequence;
			}
		};
		world* _world = nullptr;
		staticAssetManager* _blocks = nullptr;
		modelManager* _models = nullptr;

		std::vector<std::thread> _threads;
		std::mutex _mutex;
		std::condition_variable _condition;
		std::priority_queue<chunkJob, std::vector<chunkJob>, generationJobCompare> _jobs;
		std::queue<chunkJob> _remeshJobs;
		std::queue<completedChunk> _completed;
		std::atomic<bool> _running = false;
		std::atomic<bool> _gpuMeshing = false;
		std::atomic<bool> _gpuTerrain = false;
		int32_t _centerX = 0;
		int32_t _centerZ = 0;
		uint64_t _nextSequence = 0;

		static int64_t distanceSquared(const DirectX::XMINT3& position, int32_t x, int32_t z) {
			const int64_t dx = static_cast<int64_t>(position.x) - x;
			const int64_t dz = static_cast<int64_t>(position.z) - z;
			return dx * dx + dz * dz;
		}

		void run() {
			uint32_t consecutiveRemeshes = 0;
			// Reuse the large voxel cache and greedy mask for the lifetime of this
			// worker instead of allocating and retaining one mesher per chunk.
			chunkMesher mesher;
			while (_running) {
				chunkJob job;

				{
					std::unique_lock<std::mutex> lock(_mutex);
					_condition.wait(lock, [&] {
						return !_jobs.empty() || !_remeshJobs.empty() || !_running;
						});

					if (!_running) break;

					// Border meshes should be refreshed promptly, but after two
					// consecutive remeshes always allow generation to make progress.
					if (!_remeshJobs.empty() && (_jobs.empty() || consecutiveRemeshes < 1)) {
						job = std::move(_remeshJobs.front());
						_remeshJobs.pop();
						++consecutiveRemeshes;
					}
					else {
						job = std::move(const_cast<chunkJob&>(_jobs.top()));
						_jobs.pop();
						consecutiveRemeshes = 0;
					}
				}

				const auto workStart = std::chrono::steady_clock::now();
				const bool remesh = job.isRemesh();
				bool generated = false;
				std::shared_ptr<const chunk> data;
				if (remesh) data = std::move(job.data);
				else if (_gpuTerrain.load(std::memory_order_relaxed)) {
					auto loaded = std::make_shared<chunk>();
					if (_world->loadChunk(*loaded, job.position)) data = std::move(loaded);
					else generated = true;
				}
				else data = std::make_shared<chunk>(_world->loadOrGenerate(job.position, &generated));
				const auto meshStart = std::chrono::steady_clock::now();
				chunkMeshTimings meshing;

				chunkMesh mesh;
				chunkSectionMeshes sectionMeshes;
				if (data && !_gpuMeshing.load(std::memory_order_relaxed)) {
					if (remesh) {
						mesh = mesher.generateSection(
							*data, *_blocks, *_models, job.section,
							job.neighbors[0].get(), job.neighbors[1].get(),
							job.neighbors[2].get(), job.neighbors[3].get(),
							job.neighbors[4].get(), job.neighbors[5].get(),
							job.neighbors[6].get(), job.neighbors[7].get(), &meshing);
					}
					else {
						sectionMeshes = mesher.generateSections(
							*data, *_blocks, *_models,
							job.neighbors[0].get(), job.neighbors[1].get(),
							job.neighbors[2].get(), job.neighbors[3].get(),
							job.neighbors[4].get(), job.neighbors[5].get(),
							job.neighbors[6].get(), job.neighbors[7].get(), &meshing);
					}
				}

				{
					std::lock_guard<std::mutex> lock(_mutex);
					_completed.push({
						job.position,
						remesh ? nullptr : std::move(data),
						std::move(mesh),
						std::move(sectionMeshes),
						remesh,
						generated,
						static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(workStart - job.queuedAt).count()),
						static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(meshStart - workStart).count()),
						job.snapshotNanoseconds,
						meshing,
						job.neighborMask,
						job.section
						});
				}
			}
		}

	public:
		chunkWorker(
			world* world,
			staticAssetManager* blocks,
			modelManager* models,
			uint32_t workerCount = 0
		) : _world(world), _blocks(blocks), _models(models) {
			if (workerCount == 0) {
				const uint32_t hardwareThreads = std::thread::hardware_concurrency();
				workerCount = std::clamp(
					hardwareThreads > 1 ? hardwareThreads - 1 : 1u,
					1u,
					4u
				);
			}

			_running = true;
			_threads.reserve(workerCount);

			for (uint32_t i = 0; i < workerCount; ++i)
				_threads.emplace_back(&chunkWorker::run, this);
		}

		~chunkWorker() {
			stop();
		}

		void stop() {
			if (!_running.exchange(false)) return;
			_condition.notify_all();

			for (std::thread& thread : _threads)
				if (thread.joinable())
					thread.join();

			_threads.clear();
		}

		void request(const DirectX::XMINT3& position) {
			{
				std::lock_guard<std::mutex> lock(_mutex);
				chunkJob job;
				job.position = position;
				job.priority = distanceSquared(position, _centerX, _centerZ);
				job.sequence = _nextSequence++;
				_jobs.push(std::move(job));
			}
			_condition.notify_one();
		}

		void setGpuMeshing(bool enabled) {
			_gpuMeshing.store(enabled, std::memory_order_relaxed);
		}

		void setGpuTerrain(bool enabled) {
			_gpuTerrain.store(enabled, std::memory_order_relaxed);
		}

		std::vector<DirectX::XMINT3> setGenerationCenter(
			int32_t x,
			int32_t z,
			int32_t retainRadius
		) {
			std::vector<DirectX::XMINT3> cancelled;
			std::lock_guard<std::mutex> lock(_mutex);
			_centerX = x;
			_centerZ = z;
			const int64_t retainDistance = static_cast<int64_t>(retainRadius) * retainRadius;
			decltype(_jobs) reprioritized;
			while (!_jobs.empty()) {
				chunkJob job = std::move(const_cast<chunkJob&>(_jobs.top()));
				_jobs.pop();
				job.priority = distanceSquared(job.position, x, z);
				if (job.priority > retainDistance)
					cancelled.push_back(job.position);
				else
					reprioritized.push(std::move(job));
			}
			_jobs = std::move(reprioritized);
			return cancelled;
		}

		void requestRemesh(
			std::shared_ptr<const chunk> data,
			const std::array<std::shared_ptr<const chunk>, 8>& neighbors,
			uint32_t section
		) {
			const auto snapshotStart = std::chrono::steady_clock::now();
			chunkJob job;
			job.position = data->_position;
			job.data = std::move(data);
			job.section = section;
			for (size_t i = 0; i < neighbors.size(); ++i)
				if (neighbors[i]) {
					job.neighborMask |= static_cast<uint16_t>(1u << i);
					job.neighbors[i] = neighbors[i];
				}
			job.snapshotNanoseconds = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::nanoseconds>(
					std::chrono::steady_clock::now() - snapshotStart
				).count()
			);
			job.queuedAt = std::chrono::steady_clock::now();

			{
				std::lock_guard<std::mutex> lock(_mutex);
				_remeshJobs.push(std::move(job));
			}
			_condition.notify_one();
		}

		bool poll(completedChunk& result) {
			std::lock_guard<std::mutex> lock(_mutex);

			if (_completed.empty()) return false;

			result = std::move(_completed.front());
			_completed.pop();
			return true;
		}
	};

}
