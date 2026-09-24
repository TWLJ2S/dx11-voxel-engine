#pragma once
#include "world.h"
#include <assets/assetManager.h>

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
#include <cstdint>

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
		struct jobCompare {
			bool operator()(const chunkJob& a, const chunkJob& b) const {
				if (a.priority != b.priority) return a.priority > b.priority;
				return a.sequence > b.sequence;
			}
		};

		using jobQueue = std::priority_queue<chunkJob, std::vector<chunkJob>, jobCompare>;

		world* _world = nullptr;
		staticAssetManager* _blocks = nullptr;
		modelManager* _models = nullptr;

		std::vector<std::thread> _threads;
		std::mutex _jobMutex;
		std::mutex _completedMutex;
		std::condition_variable _condition;
		jobQueue _jobs;
		jobQueue _remeshJobs;
		std::queue<completedChunk> _completed;
		std::atomic<bool> _running = false;
		std::atomic<bool> _gpuMeshing = false;
		std::atomic<bool> _gpuTerrain = false;
		int32_t _centerX = 0;
		int32_t _centerZ = 0;
		uint64_t _nextSequence = 0;
		uint32_t _threadCount = 0;

		static int64_t distanceSquared(const DirectX::XMINT3& position, int32_t x, int32_t z) {
			const int64_t dx = static_cast<int64_t>(position.x) - x;
			const int64_t dz = static_cast<int64_t>(position.z) - z;
			return dx * dx + dz * dz;
		}

		static uint32_t choosePoolSize();
		static void nameCurrentThread(uint32_t index, bool preferRemesh);

		void pushCompleted(completedChunk&& result) {
			std::lock_guard<std::mutex> lock(_completedMutex);
			_completed.push(std::move(result));
		}

		void run(uint32_t index) {
			// Every third thread prefers remesh so CPU mesh catch-up does not stall
			// generation. GPU meshing turns every thread into a loader/generator.
			std::unique_ptr<chunkMesher> mesher;
			bool namedRemesh = false;
			nameCurrentThread(index, false);

			while (_running.load(std::memory_order_relaxed)) {
				const bool gpuMesh = _gpuMeshing.load(std::memory_order_relaxed);
				const bool preferRemesh = !gpuMesh && (index % 3u == 0u);
				if (preferRemesh != namedRemesh) {
					nameCurrentThread(index, preferRemesh);
					namedRemesh = preferRemesh;
				}

				chunkJob job;
				{
					std::unique_lock<std::mutex> lock(_jobMutex);
					_condition.wait(lock, [&] {
						return !_jobs.empty() || !_remeshJobs.empty() ||
							!_running.load(std::memory_order_relaxed);
					});
					if (!_running.load(std::memory_order_relaxed)) break;

					const bool takeRemesh = preferRemesh
						? !_remeshJobs.empty()
						: (_jobs.empty() && !_remeshJobs.empty());
					if (takeRemesh) {
						job = std::move(const_cast<chunkJob&>(_remeshJobs.top()));
						_remeshJobs.pop();
					}
					else if (!_jobs.empty()) {
						job = std::move(const_cast<chunkJob&>(_jobs.top()));
						_jobs.pop();
					}
					else {
						continue;
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
					if (!mesher) mesher = std::make_unique<chunkMesher>();
					if (remesh) {
						mesh = mesher->generateSection(
							*data, *_blocks, *_models, job.section,
							job.neighbors[0].get(), job.neighbors[1].get(),
							job.neighbors[2].get(), job.neighbors[3].get(),
							job.neighbors[4].get(), job.neighbors[5].get(),
							job.neighbors[6].get(), job.neighbors[7].get(), &meshing);
					}
					else {
						sectionMeshes = mesher->generateSections(
							*data, *_blocks, *_models,
							job.neighbors[0].get(), job.neighbors[1].get(),
							job.neighbors[2].get(), job.neighbors[3].get(),
							job.neighbors[4].get(), job.neighbors[5].get(),
							job.neighbors[6].get(), job.neighbors[7].get(), &meshing);
					}
				}

				pushCompleted({
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

	public:
		chunkWorker(
			world* world,
			staticAssetManager* blocks,
			modelManager* models,
			uint32_t workerCount = 0
		) : _world(world), _blocks(blocks), _models(models) {
			if (workerCount == 0)
				workerCount = choosePoolSize();
			_threadCount = workerCount;
			start();
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

		void start() {
			bool expected = false;
			if (!_running.compare_exchange_strong(expected, true)) return;
			_threads.reserve(_threadCount);
			for (uint32_t i = 0; i < _threadCount; ++i)
				_threads.emplace_back(&chunkWorker::run, this, i);
		}

		uint32_t threadCount() const { return _threadCount; }

		void clearPending() {
			{
				std::lock_guard<std::mutex> lock(_jobMutex);
				while (!_jobs.empty()) _jobs.pop();
				while (!_remeshJobs.empty()) _remeshJobs.pop();
			}
			std::lock_guard<std::mutex> lock(_completedMutex);
			while (!_completed.empty()) _completed.pop();
		}

		void request(const DirectX::XMINT3& position) {
			{
				std::lock_guard<std::mutex> lock(_jobMutex);
				chunkJob job;
				job.position = position;
				job.priority = distanceSquared(position, _centerX, _centerZ);
				job.sequence = _nextSequence++;
				_jobs.push(std::move(job));
			}
			_condition.notify_one();
		}

		void requestMany(const std::vector<DirectX::XMINT3>& positions) {
			if (positions.empty()) return;
			{
				std::lock_guard<std::mutex> lock(_jobMutex);
				for (const DirectX::XMINT3& position : positions) {
					chunkJob job;
					job.position = position;
					job.priority = distanceSquared(position, _centerX, _centerZ);
					job.sequence = _nextSequence++;
					_jobs.push(std::move(job));
				}
			}
			_condition.notify_all();
		}

		void setGpuMeshing(bool enabled) {
			_gpuMeshing.store(enabled, std::memory_order_relaxed);
			_condition.notify_all();
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
			std::lock_guard<std::mutex> lock(_jobMutex);
			_centerX = x;
			_centerZ = z;
			const int64_t retainDistance = static_cast<int64_t>(retainRadius) * retainRadius;
			jobQueue reprioritized;
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

			jobQueue remesh;
			while (!_remeshJobs.empty()) {
				chunkJob job = std::move(const_cast<chunkJob&>(_remeshJobs.top()));
				_remeshJobs.pop();
				job.priority = distanceSquared(job.position, x, z);
				remesh.push(std::move(job));
			}
			_remeshJobs = std::move(remesh);
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
			job.priority = distanceSquared(job.position, _centerX, _centerZ);
			job.queuedAt = std::chrono::steady_clock::now();

			{
				std::lock_guard<std::mutex> lock(_jobMutex);
				job.sequence = _nextSequence++;
				_remeshJobs.push(std::move(job));
			}
			_condition.notify_one();
		}

		bool poll(completedChunk& result) {
			std::lock_guard<std::mutex> lock(_completedMutex);
			if (_completed.empty()) return false;
			result = std::move(_completed.front());
			_completed.pop();
			return true;
		}
	};

}
