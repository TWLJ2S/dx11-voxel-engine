#pragma once

#include <cctype>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ac::modding {

	// Stable content identity used at API and persistence boundaries. Runtime
	// systems may resolve it to a compact integer handle after registries freeze.
	class resourceId final {
		std::string _value;

		static bool validNamespaceCharacter(unsigned char value) {
			return std::islower(value) || std::isdigit(value) || value == '_' ||
				value == '-' || value == '.';
		}

		static bool validPathCharacter(unsigned char value) {
			return validNamespaceCharacter(value) || value == '/';
		}

		static void validate(std::string_view value) {
			const size_t separator = value.find(':');
			if (separator == std::string_view::npos || separator == 0u ||
				separator + 1u >= value.size() || value.find(':', separator + 1u) != std::string_view::npos)
				throw std::invalid_argument("Resource ids must use namespace:path: " + std::string(value));

			for (size_t index = 0; index < separator; ++index)
				if (!validNamespaceCharacter(static_cast<unsigned char>(value[index])))
					throw std::invalid_argument("Invalid resource namespace: " + std::string(value));
			for (size_t index = separator + 1u; index < value.size(); ++index)
				if (!validPathCharacter(static_cast<unsigned char>(value[index])))
					throw std::invalid_argument("Invalid resource path: " + std::string(value));
		}

	public:
		resourceId() = default;

		explicit resourceId(std::string value) : _value(std::move(value)) {
			validate(_value);
		}

		static resourceId qualify(std::string_view packNamespace, std::string_view value) {
			return value.find(':') == std::string_view::npos
				? resourceId(std::string(packNamespace) + ":" + std::string(value))
				: resourceId(std::string(value));
		}

		const std::string& string() const noexcept { return _value; }
		std::string_view nameSpace() const noexcept {
			return std::string_view(_value).substr(0u, _value.find(':'));
		}
		std::string_view path() const noexcept {
			const size_t separator = _value.find(':');
			return separator == std::string::npos
				? std::string_view{}
				: std::string_view(_value).substr(separator + 1u);
		}
		bool empty() const noexcept { return _value.empty(); }

		friend bool operator==(const resourceId&, const resourceId&) = default;
		friend auto operator<=>(const resourceId&, const resourceId&) = default;
	};

	struct resourceIdHash final {
		size_t operator()(const resourceId& value) const noexcept {
			return std::hash<std::string>{}(value.string());
		}
	};
}
