#pragma once

#include "resourceId.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ac::modding {

	enum class eventResult : uint8_t {
		continueDispatch,
		stopDispatch,
		cancel
	};

	struct engineStartingEvent {};
	struct registriesMutableEvent {};
	struct registriesFrozenEvent {};
	struct engineStartedEvent {};
	struct engineStoppingEvent {};
	struct worldOpenedEvent { uint64_t seed = 0; };
	struct worldSavingEvent {};
	struct worldSavedEvent {};
	struct worldClosedEvent {};
	struct resourcesReloadedEvent {};

	struct serverTickEvent {
		uint64_t tick = 0;
		uint8_t subtick = 0;
		float deltaTime = 0.0f;
	};

	struct blockPosition {
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;
	};

	struct blockChangingEvent {
		blockPosition position;
		uint32_t previousState = 0;
		uint32_t nextState = 0;
	};

	struct blockChangedEvent {
		blockPosition position;
		uint32_t previousState = 0;
		uint32_t currentState = 0;
	};

	using eventSubscription = uint64_t;

	class gameEventBus final {
		struct slot {
			eventSubscription token = 0;
			resourceId owner;
			int32_t priority = 0;
			uint64_t sequence = 0;
			std::function<eventResult(void*)> invoke;
		};

		std::unordered_map<std::type_index, std::vector<slot>> _slots;
		eventSubscription _nextToken = 1;
		uint64_t _nextSequence = 0;

	public:
		template<typename Event>
		eventSubscription subscribe(
			resourceId owner,
			std::function<eventResult(Event&)> handler,
			int32_t priority = 0
		) {
			const eventSubscription token = _nextToken++;
			auto& slots = _slots[std::type_index(typeid(Event))];
			slots.push_back({
				token,
				std::move(owner),
				priority,
				_nextSequence++,
				[handler = std::move(handler)](void* event) {
					return handler(*static_cast<Event*>(event));
				}
			});
			std::stable_sort(slots.begin(), slots.end(), [](const slot& left, const slot& right) {
				if (left.priority != right.priority) return left.priority > right.priority;
				return left.sequence < right.sequence;
			});
			return token;
		}

		template<typename Event>
		eventResult publish(Event& event) {
			const auto found = _slots.find(std::type_index(typeid(Event)));
			if (found == _slots.end()) return eventResult::continueDispatch;
			for (const slot& item : found->second) {
				const eventResult result = item.invoke(&event);
				if (result != eventResult::continueDispatch) return result;
			}
			return eventResult::continueDispatch;
		}

		template<typename Event>
		eventResult publish(Event&& event) {
			return publish(event);
		}

		bool unsubscribe(eventSubscription token) {
			for (auto& [type, slots] : _slots) {
				(void)type;
				const size_t before = slots.size();
				slots.erase(std::remove_if(slots.begin(), slots.end(),
					[&](const slot& item) { return item.token == token; }), slots.end());
				if (slots.size() != before) return true;
			}
			return false;
		}

		void removeOwner(const resourceId& owner) {
			for (auto& [type, slots] : _slots) {
				(void)type;
				slots.erase(std::remove_if(slots.begin(), slots.end(),
					[&](const slot& item) { return item.owner == owner; }), slots.end());
			}
		}
	};
}
