#include <core/shader.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <Windows.h>

namespace {
	void writeShader(const std::filesystem::path& path, const char* color) {
		std::ofstream shader(path, std::ios::binary | std::ios::trunc);
		shader << "float4 main() : SV_Target { return float4(" << color << ", 1.0); }\n";
	}

	size_t cacheFileCount(const std::filesystem::path& directory) {
		std::error_code error;
		size_t count = 0;
		for (std::filesystem::directory_iterator item(directory, error), end;
			!error && item != end; item.increment(error))
			count += item->is_regular_file(error) && item->path().extension() == ".bin";
		return count;
	}
}

int main() {
	const auto originalTemp = std::filesystem::temp_directory_path();
	const auto unique = std::to_string(
		std::chrono::steady_clock::now().time_since_epoch().count());
	const auto testRoot = originalTemp / ("ac-shader-cache-test-" + unique);
	const auto shaderPath = testRoot / "cacheTest.hlsl";
	const auto cachePath = testRoot / "ac-voxel-engine-shader-cache";
	std::filesystem::create_directories(cachePath);
	SetEnvironmentVariableW(L"TEMP", testRoot.c_str());
	SetEnvironmentVariableW(L"TMP", testRoot.c_str());
	// Version-two caches used revision-dependent filenames. Seed one to verify
	// that the version-three migration removes those otherwise orphaned files.
	{
		std::ofstream legacy(cachePath / "legacy.bin", std::ios::binary);
		const uint32_t magic = 0x53434143u;
		const uint32_t version = 2u;
		legacy.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
		legacy.write(reinterpret_cast<const char*>(&version), sizeof(version));
	}
	writeShader(shaderPath, "1.0, 0.0, 0.0");

	const auto start = std::chrono::steady_clock::now();
	const auto first = ac::compileShaderBytecode(shaderPath.native(), "main", "ps_5_0");
	const auto second = ac::compileShaderBytecode(shaderPath.native(), "main", "ps_5_0");
	if (!first || first->GetBufferSize() == 0u || first.Get() != second.Get()) {
		std::cerr << "shader cache did not return reusable bytecode\n";
		return 1;
	}
	if (cacheFileCount(cachePath) != 1u) {
		std::cerr << "shader cache did not create exactly one stable entry\n";
		return 1;
	}

	const auto originalWriteTime = std::filesystem::last_write_time(shaderPath);
	writeShader(shaderPath, "0.0, 1.0, 0.0");
	// Keep timestamp and byte length unchanged so the content hash itself must
	// invalidate the cache.
	std::filesystem::last_write_time(shaderPath, originalWriteTime);
	const auto updated = ac::compileShaderBytecode(shaderPath.native(), "main", "ps_5_0");
	const auto updatedAgain = ac::compileShaderBytecode(shaderPath.native(), "main", "ps_5_0");
	if (!updated || updated.Get() == first.Get() || updated.Get() != updatedAgain.Get()) {
		std::cerr << "updated shader retained stale in-memory bytecode\n";
		return 1;
	}
	if (cacheFileCount(cachePath) != 1u) {
		std::cerr << "updated shader left stale cache files behind\n";
		return 1;
	}
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - start).count();
	SetEnvironmentVariableW(L"TEMP", originalTemp.c_str());
	SetEnvironmentVariableW(L"TMP", originalTemp.c_str());
	std::error_code cleanupError;
	std::filesystem::remove_all(testRoot, cleanupError);
	std::cout << "shader cache reuse and update cleanup passed in " << elapsed << " ms\n";
	return 0;
}
