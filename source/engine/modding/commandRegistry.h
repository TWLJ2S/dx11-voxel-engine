#pragma once

#include "resourceId.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ac::modding {

	class gameApi;

	class commandOutput {
	public:
		virtual ~commandOutput() = default;
		virtual void reply(std::string_view message) = 0;
	};

	struct commandContext {
		gameApi& api;
		commandOutput& output;
		resourceId command;
		std::vector<std::string> arguments;
	};

	using commandHandler = std::function<void(commandContext&)>;
	using commandCompleter = std::function<std::vector<std::string>(const commandContext&)>;

	struct commandDefinition {
		resourceId id;
		std::string description;
		std::string usage;
		std::vector<std::string> aliases;
		commandHandler execute;
		commandCompleter complete;
	};

	enum class commandExecution {
		notFound,
		executed,
		failed
	};

	class commandRegistry final {
		std::unordered_map<resourceId, commandDefinition, resourceIdHash> _commands;
		std::unordered_map<std::string, resourceId> _aliases;

		static std::string normalizeAlias(std::string value) {
			if (!value.empty() && value.front() == '/') value.erase(value.begin());
			for (char& character : value)
				character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
			if (value.empty() || value.find_first_of(" \t\r\n") != std::string::npos)
				throw std::invalid_argument("Command aliases must be a single non-empty token");
			return value;
		}

		static std::vector<std::string> tokenize(std::string_view line) {
			std::vector<std::string> result;
			std::string token;
			bool quoted = false;
			bool escaped = false;
			for (const char character : line) {
				if (escaped) {
					token.push_back(character);
					escaped = false;
				}
				else if (character == '\\') escaped = true;
				else if (character == '"') quoted = !quoted;
				else if (!quoted && std::isspace(static_cast<unsigned char>(character))) {
					if (!token.empty()) {
						result.push_back(std::move(token));
						token.clear();
					}
				}
				else token.push_back(character);
			}
			if (escaped) token.push_back('\\');
			if (!token.empty()) result.push_back(std::move(token));
			return result;
		}

	public:
		void add(commandDefinition definition) {
			if (!definition.execute)
				throw std::invalid_argument("Command has no handler: " + definition.id.string());
			if (_commands.contains(definition.id))
				throw std::runtime_error("Duplicate command: " + definition.id.string());

			std::vector<std::string> aliases = definition.aliases;
			aliases.push_back(std::string(definition.id.path()));
			for (std::string& alias : aliases)
				alias = normalizeAlias(std::move(alias));
			std::sort(aliases.begin(), aliases.end());
			aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());
			for (const std::string& alias : aliases) {
				if (_aliases.contains(alias))
					throw std::runtime_error("Duplicate command alias: " + alias);
			}
			for (const std::string& alias : aliases) _aliases.emplace(alias, definition.id);
			_commands.emplace(definition.id, std::move(definition));
		}

		commandExecution execute(std::string_view line, gameApi& api, commandOutput& output) const {
			if (!line.empty() && line.front() == '/') line.remove_prefix(1u);
			std::vector<std::string> tokens = tokenize(line);
			if (tokens.empty()) return commandExecution::notFound;
			const std::string alias = normalizeAlias(std::move(tokens.front()));
			const auto aliasEntry = _aliases.find(alias);
			if (aliasEntry == _aliases.end()) return commandExecution::notFound;
			const auto commandEntry = _commands.find(aliasEntry->second);
			if (commandEntry == _commands.end()) return commandExecution::notFound;
			tokens.erase(tokens.begin());
			commandContext context{ api, output, commandEntry->first, std::move(tokens) };
			try {
				commandEntry->second.execute(context);
				return commandExecution::executed;
			}
			catch (const std::exception& error) {
				output.reply(std::string("command failed: ") + error.what());
				return commandExecution::failed;
			}
		}

		std::vector<std::string> aliases() const {
			std::vector<std::string> result;
			result.reserve(_aliases.size());
			for (const auto& [alias, id] : _aliases) {
				(void)id;
				result.push_back(alias);
			}
			std::sort(result.begin(), result.end());
			return result;
		}

		void removeOwner(std::string_view ownerNamespace) {
			for (auto iterator = _commands.begin(); iterator != _commands.end();) {
				if (iterator->first.nameSpace() == ownerNamespace) iterator = _commands.erase(iterator);
				else ++iterator;
			}
			for (auto iterator = _aliases.begin(); iterator != _aliases.end();) {
				if (iterator->second.nameSpace() == ownerNamespace) iterator = _aliases.erase(iterator);
				else ++iterator;
			}
		}
	};
}
