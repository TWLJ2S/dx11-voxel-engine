#pragma once

#include "resourceId.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ac::modding {

	using runtimeHandle = uint32_t;
	inline constexpr runtimeHandle invalidRuntimeHandle =
		std::numeric_limits<runtimeHandle>::max();

	// A registry is mutable only during mod loading. freeze() sorts by stable id
	// and produces deterministic dense handles for hot-path access.
	template<typename T>
	class runtimeRegistry final {
		struct stagedEntry {
			resourceId id;
			T value;
		};

		std::vector<stagedEntry> _staged;
		std::vector<resourceId> _names;
		std::vector<T> _values;
		std::unordered_map<resourceId, runtimeHandle, resourceIdHash> _handles;
		bool _frozen = false;

		void requireMutable() const {
			if (_frozen) throw std::logic_error("Registry is frozen");
		}

	public:
		bool frozen() const noexcept { return _frozen; }

		void add(resourceId id, T value) {
			requireMutable();
			if (std::any_of(_staged.begin(), _staged.end(), [&](const stagedEntry& entry) {
				return entry.id == id;
			})) throw std::runtime_error("Duplicate registry entry: " + id.string());
			_staged.push_back({ std::move(id), std::move(value) });
		}

		void replace(const resourceId& id, T value) {
			requireMutable();
			const auto found = std::find_if(_staged.begin(), _staged.end(),
				[&](const stagedEntry& entry) { return entry.id == id; });
			if (found == _staged.end())
				throw std::runtime_error("Registry entry not found: " + id.string());
			found->value = std::move(value);
		}

		T* edit(const resourceId& id) {
			requireMutable();
			const auto found = std::find_if(_staged.begin(), _staged.end(),
				[&](const stagedEntry& entry) { return entry.id == id; });
			return found == _staged.end() ? nullptr : &found->value;
		}

		void freeze() {
			if (_frozen) return;
			std::sort(_staged.begin(), _staged.end(), [](const stagedEntry& left, const stagedEntry& right) {
				return left.id < right.id;
			});
			_names.reserve(_staged.size());
			_values.reserve(_staged.size());
			for (stagedEntry& entry : _staged) {
				const runtimeHandle handle = static_cast<runtimeHandle>(_values.size());
				_handles.emplace(entry.id, handle);
				_names.push_back(std::move(entry.id));
				_values.push_back(std::move(entry.value));
			}
			_staged.clear();
			_staged.shrink_to_fit();
			_frozen = true;
		}

		runtimeHandle handle(const resourceId& id) const noexcept {
			const auto found = _handles.find(id);
			return found == _handles.end() ? invalidRuntimeHandle : found->second;
		}

		const T* get(runtimeHandle handle) const noexcept {
			return handle < _values.size() ? &_values[handle] : nullptr;
		}

		const T* get(const resourceId& id) const noexcept { return get(handle(id)); }
		const resourceId* id(runtimeHandle handle) const noexcept {
			return handle < _names.size() ? &_names[handle] : nullptr;
		}
		size_t size() const noexcept { return _frozen ? _values.size() : _staged.size(); }
	};
}
