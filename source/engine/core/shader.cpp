#include "shader.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {
	struct shaderCacheHeader {
		uint32_t magic = 0x53434143u; // "CACS"
		uint32_t version = 3u;
		int64_t sourceTimestamp = 0;
		uint64_t sourceSize = 0;
		uint64_t sourceHash = 0;
		uint64_t stableKeyHash = 0;
		uint32_t compileFlags = 0;
		uint32_t reserved = 0;
		uint64_t bytecodeSize = 0;
		uint64_t bytecodeHash = 0;
	};

	struct memoryShaderCacheEntry {
		int64_t sourceTimestamp = 0;
		uint64_t sourceSize = 0;
		uint64_t sourceHash = 0;
		Microsoft::WRL::ComPtr<ID3DBlob> bytecode;
	};

	uint64_t appendHash(uint64_t hash, const void* bytes, size_t size) {
		const auto* data = static_cast<const uint8_t*>(bytes);
		for (size_t index = 0; index < size; ++index) {
			hash ^= data[index];
			hash *= 1099511628211ull;
		}
		return hash;
	}

	uint64_t hashFile(const std::filesystem::path& path) {
		std::ifstream source(path, std::ios::binary);
		if (!source) throw std::runtime_error("Failed to read shader: " + path.string());
		uint64_t hash = 1469598103934665603ull;
		char buffer[8192];
		while (source.read(buffer, sizeof(buffer)) || source.gcount() > 0)
			hash = appendHash(hash, buffer, static_cast<size_t>(source.gcount()));
		if (!source.eof()) throw std::runtime_error("Failed to hash shader: " + path.string());
		return hash;
	}

	void pruneLegacyShaderCache(const std::filesystem::path& directory) {
		std::error_code error;
		if (!std::filesystem::exists(directory, error) || error) return;
		for (std::filesystem::directory_iterator item(directory, error), end;
			!error && item != end; item.increment(error)) {
			if (!item->is_regular_file(error) || error || item->path().extension() != ".bin")
				continue;
			std::ifstream cache(item->path(), std::ios::binary);
			uint32_t magic = 0;
			uint32_t version = 0;
			if (!cache.read(reinterpret_cast<char*>(&magic), sizeof(magic)) ||
				!cache.read(reinterpret_cast<char*>(&version), sizeof(version)) ||
				magic != shaderCacheHeader{}.magic || version != shaderCacheHeader{}.version) {
				cache.close();
				std::filesystem::remove(item->path(), error);
				error.clear();
			}
		}
	}
}

namespace ac {

	Microsoft::WRL::ComPtr<ID3DBlob> compileShaderBytecode(
		const std::wstring& filePath,
		const std::string& entryPoint,
		const std::string& target,
		const D3D_SHADER_MACRO* macros
	) {
		UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
		compileFlags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

		std::error_code fileError;
		const std::filesystem::path absolutePath = std::filesystem::absolute(filePath, fileError);
		if (fileError) throw std::runtime_error("Failed to resolve shader path");
		const int64_t sourceTimestamp = static_cast<int64_t>(
			std::filesystem::last_write_time(absolutePath, fileError).time_since_epoch().count());
		if (fileError) throw std::runtime_error("Shader file not found: " + absolutePath.string());
		const uint64_t sourceSize = std::filesystem::file_size(absolutePath, fileError);
		if (fileError) throw std::runtime_error("Failed to inspect shader: " + absolutePath.string());
		const uint64_t sourceHash = hashFile(absolutePath);

		// Key relative asset paths by their logical name so the cache is shared
		// between launches from the project directory and copied build output.
		// Source revision data lives in the entry/header rather than the filename,
		// so an update replaces one stable cache file instead of accumulating files.
		std::wstring macroKey;
		for (const D3D_SHADER_MACRO* macro = macros; macro && macro->Name; ++macro) {
			macroKey += L"\n";
			macroKey += std::wstring(macro->Name, macro->Name + std::strlen(macro->Name));
			macroKey += L"=";
			if (macro->Definition)
				macroKey += std::wstring(
					macro->Definition, macro->Definition + std::strlen(macro->Definition));
		}
		const std::wstring stableKey = std::filesystem::path(filePath).lexically_normal().native() + L"\n" +
			std::wstring(entryPoint.begin(), entryPoint.end()) + L"\n" +
			std::wstring(target.begin(), target.end()) + L"\n" + std::to_wstring(compileFlags) +
			macroKey;
		static std::mutex cacheMutex;
		static std::unordered_map<std::wstring, memoryShaderCacheEntry> memoryCache;
		static std::unordered_set<std::wstring> prunedDirectories;
		std::lock_guard<std::mutex> cacheLock(cacheMutex);
		if (const auto cached = memoryCache.find(stableKey); cached != memoryCache.end() &&
			cached->second.sourceTimestamp == sourceTimestamp &&
			cached->second.sourceSize == sourceSize && cached->second.sourceHash == sourceHash)
			return cached->second.bytecode;

		uint64_t cacheHash = 1469598103934665603ull;
		cacheHash = appendHash(cacheHash, stableKey.data(), stableKey.size() * sizeof(wchar_t));
		std::ostringstream cacheName;
		cacheName << std::hex << std::setw(16) << std::setfill('0') << cacheHash << ".bin";
		const std::filesystem::path cacheDirectory =
			std::filesystem::temp_directory_path(fileError) / "ac-voxel-engine-shader-cache";
		const std::filesystem::path cachePath = cacheDirectory / cacheName.str();

		Microsoft::WRL::ComPtr<ID3DBlob> shaderBlob;
		if (!fileError) {
			const std::wstring directoryKey = cacheDirectory.lexically_normal().native();
			if (prunedDirectories.insert(directoryKey).second)
				pruneLegacyShaderCache(cacheDirectory);
			std::ifstream cache(cachePath, std::ios::binary);
			shaderCacheHeader header;
			if (cache.read(reinterpret_cast<char*>(&header), sizeof(header)) &&
				header.magic == shaderCacheHeader{}.magic && header.version == shaderCacheHeader{}.version &&
				header.sourceTimestamp == sourceTimestamp && header.sourceSize == sourceSize &&
				header.sourceHash == sourceHash && header.stableKeyHash == cacheHash &&
				header.compileFlags == compileFlags && header.bytecodeSize > 0u &&
				header.bytecodeSize <= 64ull * 1024ull * 1024ull &&
				SUCCEEDED(D3DCreateBlob(static_cast<SIZE_T>(header.bytecodeSize), &shaderBlob)) &&
				cache.read(static_cast<char*>(shaderBlob->GetBufferPointer()),
					static_cast<std::streamsize>(header.bytecodeSize)) &&
				appendHash(1469598103934665603ull, shaderBlob->GetBufferPointer(),
					shaderBlob->GetBufferSize()) == header.bytecodeHash) {
				memoryCache[stableKey] = {
					sourceTimestamp, sourceSize, sourceHash, shaderBlob
				};
				return shaderBlob;
			}
			cache.close();
			std::filesystem::remove(cachePath, fileError);
			fileError.clear();
			shaderBlob.Reset();
		}

		Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
		const HRESULT result = D3DCompileFromFile(
			absolutePath.c_str(), macros, D3D_COMPILE_STANDARD_FILE_INCLUDE,
			entryPoint.c_str(), target.c_str(), compileFlags, 0, &shaderBlob, &errorBlob);
		if (FAILED(result)) {
			if (errorBlob)
				std::cerr << "HLSL error: " << static_cast<const char*>(errorBlob->GetBufferPointer()) << '\n';
			throw std::runtime_error("Failed to compile shader: " + entryPoint);
		}

		if (!fileError) {
			std::filesystem::create_directories(cacheDirectory, fileError);
			if (!fileError) {
				shaderCacheHeader header;
				header.sourceTimestamp = sourceTimestamp;
				header.sourceSize = sourceSize;
				header.sourceHash = sourceHash;
				header.stableKeyHash = cacheHash;
				header.compileFlags = compileFlags;
				header.bytecodeSize = shaderBlob->GetBufferSize();
				header.bytecodeHash = appendHash(1469598103934665603ull,
					shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
				std::ofstream cache(cachePath, std::ios::binary | std::ios::trunc);
				cache.write(reinterpret_cast<const char*>(&header), sizeof(header));
				cache.write(static_cast<const char*>(shaderBlob->GetBufferPointer()),
					static_cast<std::streamsize>(shaderBlob->GetBufferSize()));
			}
		}

		memoryCache[stableKey] = {
			sourceTimestamp, sourceSize, sourceHash, shaderBlob
		};
		return shaderBlob;
	}

}
