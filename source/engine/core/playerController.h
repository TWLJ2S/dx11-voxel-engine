#pragma once

#include <core/settingsStore.h>

#include <DirectXMath.h>
#include <rapidjson/document.h>

#include <stdexcept>
#include <string>

namespace ac {
	struct playerCollisionTuning {
		float radius = 0.0f;
		float feetBelowEye = 0.0f;
		float headAboveEye = 0.0f;
	};

	struct playerControllerConfig {
		struct {
			playerCollisionTuning standing;
			playerCollisionTuning crouching;
			playerCollisionTuning swimming;
			float stepHeight = 0.0f;
			float maximumMoveStep = 0.0f;
		} collision;
		struct {
			float sensitivity = 0.0f;
			float maximumPitch = 0.0f;
		} look;
		struct {
			float feetSampleOffset = 0.0f;
			float torsoSampleFraction = 0.0f;
			float surfaceSampleOffset = 0.0f;
			float control = 0.0f;
			float swimInputScale = 0.0f;
			float horizontalInputScale = 0.0f;
			float verticalInputScale = 0.0f;
			float swimRetention = 0.0f;
			float wadeRetention = 0.0f;
			float swimVerticalRetention = 0.0f;
			float wadeVerticalRetention = 0.0f;
			float maximumSwimSpeed = 0.0f;
			float maximumWadeSpeed = 0.0f;
			float wadeGravity = 0.0f;
			float minimumVerticalSpeed = 0.0f;
			float maximumVerticalSpeed = 0.0f;
		} water;
		struct {
			float crouchScale = 0.0f;
			float sprintScale = 0.0f;
			float airControl = 0.0f;
			float groundInputRetention = 0.0f;
			float groundStopRetention = 0.0f;
			float airRetention = 0.0f;
			float stopSnapSpeed = 0.0f;
			float walkSpeed = 0.0f;
			float sprintSpeed = 0.0f;
			float crouchSpeed = 0.0f;
		} movement;
		struct {
			float toggleWindow = 0.0f;
			float normalScale = 0.0f;
			float sprintScale = 0.0f;
			float maximumNormalSpeed = 0.0f;
			float maximumSprintSpeed = 0.0f;
			float maximumVerticalSpeed = 0.0f;
			float spectatorNormalScale = 0.0f;
			float spectatorSprintScale = 0.0f;
			float maximumSpectatorSpeed = 0.0f;
			float maximumSpectatorVerticalSpeed = 0.0f;
		} flight;
		struct {
			float velocity = 0.0f;
			float gravity = 0.0f;
			float terminalVelocity = 0.0f;
			uint32_t landingCooldownSubticks = 0;
		} jump;
		struct {
			float initialDuration = 0.0f;
			float initialMomentumRetention = 0.0f;
			float minimumImpactSpeed = 0.0f;
			float maximumImpactSpeed = 0.0f;
			float minimumMomentumLoss = 0.0f;
			float maximumMomentumLoss = 0.0f;
			float minimumDuration = 0.0f;
			float maximumDuration = 0.0f;
		} landing;
	};

	inline float requiredControllerNumber(const rapidjson::Value& object, const char* key,
		const std::string& source) {
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsNumber())
			throw std::runtime_error(source + ": missing numeric '" + key + "'");
		return object[key].GetFloat();
	}

	inline const rapidjson::Value& requiredControllerObject(const rapidjson::Value& object,
		const char* key, const std::string& source) {
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsObject())
			throw std::runtime_error(source + ": missing object '" + key + "'");
		return object[key];
	}

	inline playerCollisionTuning loadCollisionTuning(const rapidjson::Value& value,
		const std::string& source) {
		return {
			requiredControllerNumber(value, "radius", source),
			requiredControllerNumber(value, "feetBelowEye", source),
			requiredControllerNumber(value, "headAboveEye", source)
		};
	}

	inline playerControllerConfig loadPlayerControllerConfig(const std::filesystem::path& path) {
		rapidjson::Document document;
		if (!loadJsonFile(path, document))
			throw std::runtime_error(path.string() + ": missing or invalid player controller config");
		const std::string source = path.string();
		playerControllerConfig result;
		const auto& collision = requiredControllerObject(document, "collision", source);
		result.collision.standing = loadCollisionTuning(requiredControllerObject(collision, "standing", source), source + ": collision.standing");
		result.collision.crouching = loadCollisionTuning(requiredControllerObject(collision, "crouching", source), source + ": collision.crouching");
		result.collision.swimming = loadCollisionTuning(requiredControllerObject(collision, "swimming", source), source + ": collision.swimming");
		result.collision.stepHeight = requiredControllerNumber(collision, "stepHeight", source);
		result.collision.maximumMoveStep = requiredControllerNumber(collision, "maximumMoveStep", source);

		const auto& look = requiredControllerObject(document, "look", source);
		result.look.sensitivity = requiredControllerNumber(look, "sensitivity", source);
		result.look.maximumPitch = DirectX::XMConvertToRadians(requiredControllerNumber(look, "maximumPitchDeg", source));
		const auto& water = requiredControllerObject(document, "water", source);
#define AC_LOAD_FLOAT(section, field) result.section.field = requiredControllerNumber(section, #field, source)
		AC_LOAD_FLOAT(water, feetSampleOffset); AC_LOAD_FLOAT(water, torsoSampleFraction);
		AC_LOAD_FLOAT(water, surfaceSampleOffset); AC_LOAD_FLOAT(water, control);
		AC_LOAD_FLOAT(water, swimInputScale); AC_LOAD_FLOAT(water, horizontalInputScale);
		AC_LOAD_FLOAT(water, verticalInputScale); AC_LOAD_FLOAT(water, swimRetention);
		AC_LOAD_FLOAT(water, wadeRetention); AC_LOAD_FLOAT(water, swimVerticalRetention);
		AC_LOAD_FLOAT(water, wadeVerticalRetention); AC_LOAD_FLOAT(water, maximumSwimSpeed);
		AC_LOAD_FLOAT(water, maximumWadeSpeed); AC_LOAD_FLOAT(water, wadeGravity);
		AC_LOAD_FLOAT(water, minimumVerticalSpeed); AC_LOAD_FLOAT(water, maximumVerticalSpeed);
		const auto& movement = requiredControllerObject(document, "movement", source);
		AC_LOAD_FLOAT(movement, crouchScale); AC_LOAD_FLOAT(movement, sprintScale);
		AC_LOAD_FLOAT(movement, airControl); AC_LOAD_FLOAT(movement, groundInputRetention);
		AC_LOAD_FLOAT(movement, groundStopRetention); AC_LOAD_FLOAT(movement, airRetention);
		AC_LOAD_FLOAT(movement, stopSnapSpeed); AC_LOAD_FLOAT(movement, walkSpeed);
		AC_LOAD_FLOAT(movement, sprintSpeed); AC_LOAD_FLOAT(movement, crouchSpeed);
		const auto& flight = requiredControllerObject(document, "flight", source);
		AC_LOAD_FLOAT(flight, toggleWindow); AC_LOAD_FLOAT(flight, normalScale);
		AC_LOAD_FLOAT(flight, sprintScale); AC_LOAD_FLOAT(flight, maximumNormalSpeed);
		AC_LOAD_FLOAT(flight, maximumSprintSpeed); AC_LOAD_FLOAT(flight, maximumVerticalSpeed);
		AC_LOAD_FLOAT(flight, spectatorNormalScale); AC_LOAD_FLOAT(flight, spectatorSprintScale);
		AC_LOAD_FLOAT(flight, maximumSpectatorSpeed); AC_LOAD_FLOAT(flight, maximumSpectatorVerticalSpeed);
		const auto& jump = requiredControllerObject(document, "jump", source);
		AC_LOAD_FLOAT(jump, velocity); AC_LOAD_FLOAT(jump, gravity); AC_LOAD_FLOAT(jump, terminalVelocity);
		if (!jump.HasMember("landingCooldownSubticks") || !jump["landingCooldownSubticks"].IsUint())
			throw std::runtime_error(source + ": jump.landingCooldownSubticks must be unsigned");
		result.jump.landingCooldownSubticks = jump["landingCooldownSubticks"].GetUint();
		const auto& landing = requiredControllerObject(document, "landing", source);
		AC_LOAD_FLOAT(landing, initialDuration); AC_LOAD_FLOAT(landing, initialMomentumRetention);
		AC_LOAD_FLOAT(landing, minimumImpactSpeed); AC_LOAD_FLOAT(landing, maximumImpactSpeed);
		AC_LOAD_FLOAT(landing, minimumMomentumLoss); AC_LOAD_FLOAT(landing, maximumMomentumLoss);
		AC_LOAD_FLOAT(landing, minimumDuration); AC_LOAD_FLOAT(landing, maximumDuration);
#undef AC_LOAD_FLOAT
		return result;
	}

	inline const playerControllerConfig& playerController() {
		static const playerControllerConfig config =
			loadPlayerControllerConfig("assets/player/controller.json");
		return config;
	}
}
