#pragma once

#include <DirectXMath.h>
#include <rapidjson/document.h>

#include <core/settingsStore.h>
#include <core/animationTimeline.h>

namespace ac {

	struct poseVec3 {
		float x = 0.0f;
		float y = 0.0f;
		float z = 0.0f;
	};

	struct poseScale {
		float x = 1.0f;
		float y = 1.0f;
		float z = 1.0f;
	};

	struct armReadyPose {
		float pitch = 0.0f;
		float yaw = 0.0f;
		float roll = 0.0f;
		float chop = 0.0f;
		float chopYaw = 0.0f;
		float chopRoll = 0.0f;
	};

	struct heldItemPose {
		poseVec3 translation{};
		poseVec3 rotationDeg{};
		poseScale scaleFirst{ 1.0f, 1.0f, 1.0f };
		poseScale scaleThird{ 1.0f, 1.0f, 1.0f };
		poseVec3 fist{};
	};

	struct playerPoseConfig {
		struct {
			float armSwing = 0.0f;
			float legSwing = 0.0f;
			float strideCrouch = 0.0f;
			float strideSprint = 0.0f;
			float strideWalk = 0.0f;
			float airborneKeep = 0.0f;
			float airborneMin = 0.0f;
			float poseResponse = 0.0f;
			float airborneStrideMin = 0.0f;
			float referenceSwim = 0.0f;
			float referenceSprint = 0.0f;
			float referenceCrouch = 0.0f;
			float referenceWalk = 0.0f;
			std::vector<animationTimingSegment> timing;
			animationCurve timeCurve;
		} walk;

		struct {
			float enterRate = 0.0f;
			float exitRate = 0.0f;
			float eyePivotBase = 0.0f;
			float eyePivotLift = 0.0f;
			float standFeet = 0.0f;
			float crouchFeet = 0.0f;
			float strideSpeed = 0.0f;
			float strideMin = 0.0f;
			float bodyPitch = 0.0f;
			float headPitch = 0.0f;
			float armPitch = 0.0f;
			float armYaw = 0.0f;
			float armSpread = 0.0f;
			float armSweep = 0.0f;
			float armTuck = 0.0f;
			float holdForward = 0.0f;
			float sweepEnd = 0.0f;
			float holdBack = 0.0f;
			float legPitch = 0.0f;
			float legKick = 0.0f;
			float legRoll = 0.0f;
			float legRollKick = 0.0f;
			float twoHandLimit = 0.0f;
		} swim;

		struct {
			float retrigger = 0.0f;
			float toolDuration = 0.0f;
			float toolPitch = 0.0f;
			float toolRoll = 0.0f;
			float handDuration = 0.0f;
			float handPitch = 0.0f;
			float handRoll = 0.0f;
			std::vector<animationTimingSegment> toolTiming;
			std::vector<animationTimingSegment> handTiming;
			animationCurve toolCurve;
			animationCurve handCurve;
		} use;

		struct {
			float toolDuration = 0.0f;
			float toolPitch = 0.0f;
			float toolRoll = 0.0f;
			float axeDuration = 0.0f;
			float axePitch = 0.0f;
			float axeYaw = 0.0f;
			float axeRoll = 0.0f;
			float axeMoveY = 0.0f;
			float axeMoveZ = 0.0f;
			float swordDuration = 0.0f;
			float swordPitch = 0.0f;
			float swordYaw = 0.0f;
			float swordRoll = 0.0f;
			float swordMoveY = 0.0f;
			float swordMoveZ = 0.0f;
			float handDuration = 0.0f;
			float handPitch = 0.0f;
			float handRoll = 0.0f;
			std::vector<animationTimingSegment> toolTiming;
			std::vector<animationTimingSegment> axeTiming;
			std::vector<animationTimingSegment> swordTiming;
			std::vector<animationTimingSegment> handTiming;
			animationCurve toolCurve;
			animationCurve axeCurve;
			animationCurve swordCurve;
			animationCurve handCurve;
		} attack;

		struct {
			float enterRate = 0.0f;
			float exitRate = 0.0f;
			float torsoPitch = 0.0f;
			float legPitch = 0.0f;
			float upperBodyY = 0.0f;
			float upperBodyZ = 0.0f;
			float legZ = 0.0f;
		} crouch;

		struct {
			float armPitch = 0.0f;
			float legPitch = 0.0f;
		} airborne;

		struct {
			float pitchFollow = 0.0f;
		} head;

		struct {
			float raiseSeconds = 0.0f;
			float lowerSeconds = 0.0f;
			float walkDampen = 0.0f;
			float bob = 0.0f;
		} toolRaise;

		struct {
			armReadyPose rightFirst{};
			armReadyPose rightThird{};
			armReadyPose leftFirst{};
			armReadyPose leftThird{};
		} twoHand;

		heldItemPose tool{};
		heldItemPose rod{};
		heldItemPose cross{};
		heldItemPose sprite{};
		heldItemPose block{};
		heldItemPose slab{};

		struct {
			poseVec3 poseDeg{};
			poseVec3 offset{};
			float swingPitch = 0.0f;
			float swingPitchUse = 0.0f;
			float swingYaw = 0.0f;
			float swingRoll = 0.0f;
			float walkBobPitch = 0.0f;
			float walkBobRoll = 0.0f;
			float idlePitch = 0.0f;
			float idleYaw = 0.0f;
			float idleRoll = 0.0f;
			float idleRate = 0.0f;
			float idle2Rate = 0.0f;
			float idle2Phase = 0.0f;
			float airbornePitch = 0.0f;
			float airborneY = 0.0f;
			float airborneRate = 0.0f;
			float walkX = 0.0f;
			float walkY = 0.0f;
			float swingY = 0.0f;
			float swingZ = 0.0f;
			float idleX = 0.0f;
			float idleY = 0.0f;
			float idleZ = 0.0f;
		} viewmodel;

		struct {
			float faceOffset = 0.0f;
			float swimOffset = 0.0f;
			float radius = 0.0f;
			float hipPivotY = 0.0f;
			float crouchForwardOffset = 0.0f;
			float motionReferenceSpeed = 0.0f;
			float walkBobY = 0.0f;
			float walkBobRoll = 0.0f;
			float sprintFovScale = 0.0f;
			float fovResponse = 0.0f;
			float shakeDecay = 0.0f;
			float motionResponse = 0.0f;
			float tiltResponse = 0.0f;
			float idleClockRate = 0.0f;
			float movementClockRate = 0.0f;
			float walkingRoll = 0.0f;
			float turnRoll = 0.0f;
			float lateralRoll = 0.0f;
			float maximumRoll = 0.0f;
			float forwardPitch = 0.0f;
			float maximumPitchLean = 0.0f;
			float landingPitchRetention = 0.0f;
			float landingRollRetention = 0.0f;
			float thirdPersonDistance = 0.0f;
			float thirdPersonShoulder = 0.0f;
			float thirdPersonHeight = 0.0f;
			float thirdPersonFocusDistance = 0.0f;
			float frontFocusDrop = 0.0f;
		} camera;
	};

	inline float jsonNumber(const rapidjson::Value& object, const char* key, float fallback) {
		(void)fallback;
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsNumber())
			throw std::runtime_error(std::string("player pose: missing numeric '") + key + "'");
		return object[key].GetFloat();
	}

	inline float jsonDegrees(const rapidjson::Value& object, const char* key, float fallbackRadians) {
		(void)fallbackRadians;
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsNumber())
			throw std::runtime_error(std::string("player pose: missing degree value '") + key + "'");
		return DirectX::XMConvertToRadians(object[key].GetFloat());
	}

	inline const rapidjson::Value* jsonChild(const rapidjson::Value& object, const char* key) {
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsObject())
			throw std::runtime_error(std::string("player pose: missing object '") + key + "'");
		return &object[key];
	}

	inline poseVec3 jsonVec3(const rapidjson::Value& object, const char* key, poseVec3 fallback) {
		(void)fallback;
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsArray() || object[key].Size() < 3)
			throw std::runtime_error(std::string("player pose: missing vec3 '") + key + "'");
		const auto& array = object[key];
		if (!array[0].IsNumber() || !array[1].IsNumber() || !array[2].IsNumber())
			throw std::runtime_error(std::string("player pose: vec3 must be numeric '") + key + "'");
		return {
			array[0].GetFloat(), array[1].GetFloat(), array[2].GetFloat()
		};
	}

	inline poseVec3 jsonDegreesVec3(const rapidjson::Value& object, const char* key, poseVec3 fallbackRadians) {
		const poseVec3 degrees = jsonVec3(object, key, {
			fallbackRadians.x * (180.0f / DirectX::XM_PI),
			fallbackRadians.y * (180.0f / DirectX::XM_PI),
			fallbackRadians.z * (180.0f / DirectX::XM_PI)
		});
		return {
			DirectX::XMConvertToRadians(degrees.x),
			DirectX::XMConvertToRadians(degrees.y),
			DirectX::XMConvertToRadians(degrees.z)
		};
	}

	inline poseScale jsonScale(const rapidjson::Value& object, const char* key, poseScale fallback) {
		(void)fallback;
		if (!object.IsObject() || !object.HasMember(key))
			throw std::runtime_error(std::string("player pose: missing scale '") + key + "'");
		const auto& value = object[key];
		if (value.IsNumber()) {
			const float scale = value.GetFloat();
			return { scale, scale, scale };
		}
		if (value.IsArray() && value.Size() >= 3) {
			if (!value[0].IsNumber() || !value[1].IsNumber() || !value[2].IsNumber())
				throw std::runtime_error(std::string("player pose: scale must be numeric '") + key + "'");
			return {
				value[0].GetFloat(), value[1].GetFloat(), value[2].GetFloat()
			};
		}
		throw std::runtime_error(std::string("player pose: invalid scale '") + key + "'");
	}

	inline void loadArmReady(const rapidjson::Value& object, armReadyPose& pose) {
		pose.pitch = jsonDegrees(object, "pitchDeg", pose.pitch);
		pose.yaw = jsonDegrees(object, "yawDeg", pose.yaw);
		pose.roll = jsonDegrees(object, "rollDeg", pose.roll);
		pose.chop = jsonDegrees(object, "chopDeg", pose.chop);
		pose.chopYaw = jsonDegrees(object, "chopYawDeg", pose.chopYaw);
		pose.chopRoll = jsonDegrees(object, "chopRollDeg", pose.chopRoll);
	}

	inline void loadHeldItem(const rapidjson::Value& object, heldItemPose& pose) {
		pose.translation = jsonVec3(object, "translation", pose.translation);
		pose.rotationDeg = jsonDegreesVec3(object, "rotationDeg", pose.rotationDeg);
		pose.scaleFirst = jsonScale(object, "scaleFirstPerson", pose.scaleFirst);
		pose.scaleThird = jsonScale(object, "scaleThirdPerson", pose.scaleThird);
		pose.fist = jsonVec3(object, "fist", pose.fist);
	}

	inline void loadAnimationJson(playerPoseConfig& config, const rapidjson::Document& document) {
		if (const rapidjson::Value* walk = jsonChild(document, "walk")) {
			config.walk.armSwing = jsonDegrees(*walk, "armSwingDeg", config.walk.armSwing);
			config.walk.legSwing = jsonDegrees(*walk, "legSwingDeg", config.walk.legSwing);
			config.walk.strideCrouch = jsonNumber(*walk, "strideCrouch", config.walk.strideCrouch);
			config.walk.strideSprint = jsonNumber(*walk, "strideSprint", config.walk.strideSprint);
			config.walk.strideWalk = jsonNumber(*walk, "strideWalk", config.walk.strideWalk);
			config.walk.airborneKeep = jsonNumber(*walk, "airborneKeep", config.walk.airborneKeep);
			config.walk.airborneMin = jsonNumber(*walk, "airborneMin", config.walk.airborneMin);
			config.walk.poseResponse = jsonNumber(*walk, "poseResponse", config.walk.poseResponse);
			config.walk.airborneStrideMin = jsonNumber(*walk, "airborneStrideMin", config.walk.airborneStrideMin);
			config.walk.referenceSwim = jsonNumber(*walk, "referenceSwim", config.walk.referenceSwim);
			config.walk.referenceSprint = jsonNumber(*walk, "referenceSprint", config.walk.referenceSprint);
			config.walk.referenceCrouch = jsonNumber(*walk, "referenceCrouch", config.walk.referenceCrouch);
			config.walk.referenceWalk = jsonNumber(*walk, "referenceWalk", config.walk.referenceWalk);
			if (walk->HasMember("timing"))
				config.walk.timing = loadAnimationTiming((*walk)["timing"], "player walk animation");
			if (walk->HasMember("timeCurve"))
				config.walk.timeCurve = loadAnimationCurve((*walk)["timeCurve"], "player walk animation");
		}
		if (const rapidjson::Value* swim = jsonChild(document, "swim")) {
			config.swim.enterRate = jsonNumber(*swim, "enterRate", config.swim.enterRate);
			config.swim.exitRate = jsonNumber(*swim, "exitRate", config.swim.exitRate);
			config.swim.eyePivotBase = jsonNumber(*swim, "eyePivotBase", config.swim.eyePivotBase);
			config.swim.eyePivotLift = jsonNumber(*swim, "eyePivotLift", config.swim.eyePivotLift);
			config.swim.standFeet = jsonNumber(*swim, "standFeet", config.swim.standFeet);
			config.swim.crouchFeet = jsonNumber(*swim, "crouchFeet", config.swim.crouchFeet);
			config.swim.strideSpeed = jsonNumber(*swim, "strideSpeed", config.swim.strideSpeed);
			config.swim.strideMin = jsonNumber(*swim, "strideMin", config.swim.strideMin);
			config.swim.bodyPitch = jsonDegrees(*swim, "bodyPitchDeg", config.swim.bodyPitch);
			config.swim.headPitch = jsonDegrees(*swim, "headPitchDeg", config.swim.headPitch);
			config.swim.armPitch = jsonDegrees(*swim, "armPitchDeg", config.swim.armPitch);
			config.swim.armYaw = jsonDegrees(*swim, "armYawDeg", config.swim.armYaw);
			if (swim->HasMember("armHugDeg") && (*swim)["armHugDeg"].IsNumber())
				config.swim.armSpread = jsonDegrees(*swim, "armHugDeg", config.swim.armSpread);
			else
				config.swim.armSpread = jsonDegrees(*swim, "armSpreadDeg", config.swim.armSpread);
			config.swim.armSweep = jsonDegrees(*swim, "armSweepDeg", config.swim.armSweep);
			config.swim.armTuck = jsonDegrees(*swim, "armTuckDeg", config.swim.armTuck);
			config.swim.holdForward = jsonNumber(*swim, "holdForward", config.swim.holdForward);
			config.swim.sweepEnd = jsonNumber(*swim, "sweepEnd", config.swim.sweepEnd);
			config.swim.holdBack = jsonNumber(*swim, "holdBack", config.swim.holdBack);
			config.swim.legPitch = jsonDegrees(*swim, "legPitchDeg", config.swim.legPitch);
			config.swim.legKick = jsonDegrees(*swim, "legKickDeg", config.swim.legKick);
			config.swim.legRoll = jsonDegrees(*swim, "legRollDeg", config.swim.legRoll);
			config.swim.legRollKick = jsonDegrees(*swim, "legRollKickDeg", config.swim.legRollKick);
			config.swim.twoHandLimit = jsonNumber(*swim, "twoHandLimit", config.swim.twoHandLimit);
		}
		if (const rapidjson::Value* use = jsonChild(document, "use")) {
			config.use.retrigger = jsonNumber(*use, "retrigger", config.use.retrigger);
			config.use.toolDuration = jsonNumber(*use, "toolDuration", config.use.toolDuration);
			config.use.toolPitch = jsonDegrees(*use, "toolPitchDeg", config.use.toolPitch);
			config.use.toolRoll = jsonDegrees(*use, "toolRollDeg", config.use.toolRoll);
			config.use.handDuration = jsonNumber(*use, "handDuration", config.use.handDuration);
			config.use.handPitch = jsonDegrees(*use, "handPitchDeg", config.use.handPitch);
			config.use.handRoll = jsonDegrees(*use, "handRollDeg", config.use.handRoll);
			if (use->HasMember("toolTiming"))
				config.use.toolTiming = loadAnimationTiming((*use)["toolTiming"], "player tool-use animation");
			if (use->HasMember("handTiming"))
				config.use.handTiming = loadAnimationTiming((*use)["handTiming"], "player hand-use animation");
			if (use->HasMember("toolCurve"))
				config.use.toolCurve = loadAnimationCurve((*use)["toolCurve"], "player tool-use animation");
			if (use->HasMember("handCurve"))
				config.use.handCurve = loadAnimationCurve((*use)["handCurve"], "player hand-use animation");
		}
		if (const rapidjson::Value* attack = jsonChild(document, "attack")) {
			config.attack.toolDuration = jsonNumber(*attack, "toolDuration", config.attack.toolDuration);
			config.attack.toolPitch = jsonDegrees(*attack, "toolPitchDeg", config.attack.toolPitch);
			config.attack.toolRoll = jsonDegrees(*attack, "toolRollDeg", config.attack.toolRoll);
			config.attack.axeDuration = jsonNumber(*attack, "axeDuration", config.attack.axeDuration);
			config.attack.axePitch = jsonDegrees(*attack, "axePitchDeg", config.attack.axePitch);
			config.attack.axeYaw = jsonDegrees(*attack, "axeYawDeg", config.attack.axeYaw);
			config.attack.axeRoll = jsonDegrees(*attack, "axeRollDeg", config.attack.axeRoll);
			config.attack.axeMoveY = jsonNumber(*attack, "axeMoveY", config.attack.axeMoveY);
			config.attack.axeMoveZ = jsonNumber(*attack, "axeMoveZ", config.attack.axeMoveZ);
			config.attack.swordDuration = jsonNumber(*attack, "swordDuration", config.attack.swordDuration);
			config.attack.swordPitch = jsonDegrees(*attack, "swordPitchDeg", config.attack.swordPitch);
			config.attack.swordYaw = jsonDegrees(*attack, "swordYawDeg", config.attack.swordYaw);
			config.attack.swordRoll = jsonDegrees(*attack, "swordRollDeg", config.attack.swordRoll);
			config.attack.swordMoveY = jsonNumber(*attack, "swordMoveY", config.attack.swordMoveY);
			config.attack.swordMoveZ = jsonNumber(*attack, "swordMoveZ", config.attack.swordMoveZ);
			config.attack.handDuration = jsonNumber(*attack, "handDuration", config.attack.handDuration);
			config.attack.handPitch = jsonDegrees(*attack, "handPitchDeg", config.attack.handPitch);
			config.attack.handRoll = jsonDegrees(*attack, "handRollDeg", config.attack.handRoll);
			if (attack->HasMember("toolTiming"))
				config.attack.toolTiming = loadAnimationTiming((*attack)["toolTiming"], "player tool attack");
			if (attack->HasMember("axeTiming"))
				config.attack.axeTiming = loadAnimationTiming((*attack)["axeTiming"], "player axe attack");
			if (attack->HasMember("swordTiming"))
				config.attack.swordTiming = loadAnimationTiming((*attack)["swordTiming"], "player sword attack");
			if (attack->HasMember("handTiming"))
				config.attack.handTiming = loadAnimationTiming((*attack)["handTiming"], "player hand attack");
			if (attack->HasMember("toolCurve"))
				config.attack.toolCurve = loadAnimationCurve((*attack)["toolCurve"], "player tool attack");
			if (attack->HasMember("axeCurve"))
				config.attack.axeCurve = loadAnimationCurve((*attack)["axeCurve"], "player axe attack");
			if (attack->HasMember("swordCurve"))
				config.attack.swordCurve = loadAnimationCurve((*attack)["swordCurve"], "player sword attack");
			if (attack->HasMember("handCurve"))
				config.attack.handCurve = loadAnimationCurve((*attack)["handCurve"], "player hand attack");
		}
		if (const rapidjson::Value* crouch = jsonChild(document, "crouch")) {
			config.crouch.enterRate = jsonNumber(*crouch, "enterRate", config.crouch.enterRate);
			config.crouch.exitRate = jsonNumber(*crouch, "exitRate", config.crouch.exitRate);
			config.crouch.torsoPitch = jsonDegrees(*crouch, "torsoPitchDeg", config.crouch.torsoPitch);
			config.crouch.legPitch = jsonDegrees(*crouch, "legPitchDeg", config.crouch.legPitch);
			config.crouch.upperBodyY = jsonNumber(*crouch, "upperBodyY", config.crouch.upperBodyY);
			config.crouch.upperBodyZ = jsonNumber(*crouch, "upperBodyZ", config.crouch.upperBodyZ);
			config.crouch.legZ = jsonNumber(*crouch, "legZ", config.crouch.legZ);
		}
		if (const rapidjson::Value* airborne = jsonChild(document, "airborne")) {
			config.airborne.armPitch = jsonDegrees(*airborne, "armPitchDeg", config.airborne.armPitch);
			config.airborne.legPitch = jsonDegrees(*airborne, "legPitchDeg", config.airborne.legPitch);
		}
		if (const rapidjson::Value* head = jsonChild(document, "head"))
			config.head.pitchFollow = jsonNumber(*head, "pitchFollow", config.head.pitchFollow);
		if (const rapidjson::Value* toolRaise = jsonChild(document, "toolRaise")) {
			config.toolRaise.raiseSeconds = jsonNumber(*toolRaise, "raiseSeconds", config.toolRaise.raiseSeconds);
			config.toolRaise.lowerSeconds = jsonNumber(*toolRaise, "lowerSeconds", config.toolRaise.lowerSeconds);
			config.toolRaise.walkDampen = jsonNumber(*toolRaise, "walkDampen", config.toolRaise.walkDampen);
			config.toolRaise.bob = jsonNumber(*toolRaise, "bob", config.toolRaise.bob);
		}
		if (const rapidjson::Value* camera = jsonChild(document, "camera")) {
			config.camera.faceOffset = jsonNumber(*camera, "faceOffset", config.camera.faceOffset);
			config.camera.swimOffset = jsonNumber(*camera, "swimOffset", config.camera.swimOffset);
			config.camera.radius = jsonNumber(*camera, "radius", config.camera.radius);
			config.camera.hipPivotY = jsonNumber(*camera, "hipPivotY", config.camera.hipPivotY);
			config.camera.crouchForwardOffset = jsonNumber(*camera, "crouchForwardOffset", config.camera.crouchForwardOffset);
			config.camera.motionReferenceSpeed = jsonNumber(*camera, "motionReferenceSpeed", config.camera.motionReferenceSpeed);
			config.camera.walkBobY = jsonNumber(*camera, "walkBobY", config.camera.walkBobY);
			config.camera.walkBobRoll = jsonNumber(*camera, "walkBobRoll", config.camera.walkBobRoll);
			config.camera.sprintFovScale = jsonNumber(*camera, "sprintFovScale", config.camera.sprintFovScale);
			config.camera.fovResponse = jsonNumber(*camera, "fovResponse", config.camera.fovResponse);
			config.camera.shakeDecay = jsonNumber(*camera, "shakeDecay", config.camera.shakeDecay);
			config.camera.motionResponse = jsonNumber(*camera, "motionResponse", config.camera.motionResponse);
			config.camera.tiltResponse = jsonNumber(*camera, "tiltResponse", config.camera.tiltResponse);
			config.camera.idleClockRate = jsonNumber(*camera, "idleClockRate", config.camera.idleClockRate);
			config.camera.movementClockRate = jsonNumber(*camera, "movementClockRate", config.camera.movementClockRate);
			config.camera.walkingRoll = jsonNumber(*camera, "walkingRoll", config.camera.walkingRoll);
			config.camera.turnRoll = jsonNumber(*camera, "turnRoll", config.camera.turnRoll);
			config.camera.lateralRoll = jsonNumber(*camera, "lateralRoll", config.camera.lateralRoll);
			config.camera.maximumRoll = jsonNumber(*camera, "maximumRoll", config.camera.maximumRoll);
			config.camera.forwardPitch = jsonNumber(*camera, "forwardPitch", config.camera.forwardPitch);
			config.camera.maximumPitchLean = jsonNumber(*camera, "maximumPitchLean", config.camera.maximumPitchLean);
			config.camera.landingPitchRetention = jsonNumber(*camera, "landingPitchRetention", config.camera.landingPitchRetention);
			config.camera.landingRollRetention = jsonNumber(*camera, "landingRollRetention", config.camera.landingRollRetention);
			config.camera.thirdPersonDistance = jsonNumber(*camera, "thirdPersonDistance", config.camera.thirdPersonDistance);
			config.camera.thirdPersonShoulder = jsonNumber(*camera, "thirdPersonShoulder", config.camera.thirdPersonShoulder);
			config.camera.thirdPersonHeight = jsonNumber(*camera, "thirdPersonHeight", config.camera.thirdPersonHeight);
			config.camera.thirdPersonFocusDistance = jsonNumber(*camera, "thirdPersonFocusDistance", config.camera.thirdPersonFocusDistance);
			config.camera.frontFocusDrop = jsonNumber(*camera, "frontFocusDrop", config.camera.frontFocusDrop);
		}
	}

	inline void loadHeldItemsJson(playerPoseConfig& config, const rapidjson::Document& document) {
		if (const rapidjson::Value* tool = jsonChild(document, "tool"))
			loadHeldItem(*tool, config.tool);
		if (const rapidjson::Value* rod = jsonChild(document, "rod"))
			loadHeldItem(*rod, config.rod);
		if (const rapidjson::Value* cross = jsonChild(document, "cross"))
			loadHeldItem(*cross, config.cross);
		if (const rapidjson::Value* sprite = jsonChild(document, "sprite"))
			loadHeldItem(*sprite, config.sprite);
		if (const rapidjson::Value* block = jsonChild(document, "block"))
			loadHeldItem(*block, config.block);
		if (const rapidjson::Value* slab = jsonChild(document, "slab"))
			loadHeldItem(*slab, config.slab);
		if (const rapidjson::Value* twoHand = jsonChild(document, "twoHand")) {
			if (const rapidjson::Value* right = jsonChild(*twoHand, "right")) {
				if (const rapidjson::Value* first = jsonChild(*right, "firstPerson"))
					loadArmReady(*first, config.twoHand.rightFirst);
				if (const rapidjson::Value* third = jsonChild(*right, "thirdPerson"))
					loadArmReady(*third, config.twoHand.rightThird);
			}
			if (const rapidjson::Value* left = jsonChild(*twoHand, "left")) {
				if (const rapidjson::Value* first = jsonChild(*left, "firstPerson"))
					loadArmReady(*first, config.twoHand.leftFirst);
				if (const rapidjson::Value* third = jsonChild(*left, "thirdPerson"))
					loadArmReady(*third, config.twoHand.leftThird);
			}
		}
		if (const rapidjson::Value* viewmodel = jsonChild(document, "viewmodel")) {
			config.viewmodel.poseDeg = jsonDegreesVec3(*viewmodel, "poseDeg", config.viewmodel.poseDeg);
			config.viewmodel.offset = jsonVec3(*viewmodel, "offset", config.viewmodel.offset);
			config.viewmodel.swingPitch = jsonNumber(*viewmodel, "swingPitch", config.viewmodel.swingPitch);
			config.viewmodel.swingPitchUse = jsonNumber(*viewmodel, "swingPitchUse", config.viewmodel.swingPitchUse);
			config.viewmodel.swingYaw = jsonNumber(*viewmodel, "swingYaw", config.viewmodel.swingYaw);
			config.viewmodel.swingRoll = jsonNumber(*viewmodel, "swingRoll", config.viewmodel.swingRoll);
			config.viewmodel.walkBobPitch = jsonNumber(*viewmodel, "walkBobPitch", config.viewmodel.walkBobPitch);
			config.viewmodel.walkBobRoll = jsonNumber(*viewmodel, "walkBobRoll", config.viewmodel.walkBobRoll);
			config.viewmodel.idlePitch = jsonNumber(*viewmodel, "idlePitch", config.viewmodel.idlePitch);
			config.viewmodel.idleYaw = jsonNumber(*viewmodel, "idleYaw", config.viewmodel.idleYaw);
			config.viewmodel.idleRoll = jsonNumber(*viewmodel, "idleRoll", config.viewmodel.idleRoll);
			config.viewmodel.idleRate = jsonNumber(*viewmodel, "idleRate", config.viewmodel.idleRate);
			config.viewmodel.idle2Rate = jsonNumber(*viewmodel, "idle2Rate", config.viewmodel.idle2Rate);
			config.viewmodel.idle2Phase = jsonNumber(*viewmodel, "idle2Phase", config.viewmodel.idle2Phase);
			config.viewmodel.airbornePitch = jsonNumber(*viewmodel, "airbornePitch", config.viewmodel.airbornePitch);
			config.viewmodel.airborneY = jsonNumber(*viewmodel, "airborneY", config.viewmodel.airborneY);
			config.viewmodel.airborneRate = jsonNumber(*viewmodel, "airborneRate", config.viewmodel.airborneRate);
			config.viewmodel.walkX = jsonNumber(*viewmodel, "walkX", config.viewmodel.walkX);
			config.viewmodel.walkY = jsonNumber(*viewmodel, "walkY", config.viewmodel.walkY);
			config.viewmodel.swingY = jsonNumber(*viewmodel, "swingY", config.viewmodel.swingY);
			config.viewmodel.swingZ = jsonNumber(*viewmodel, "swingZ", config.viewmodel.swingZ);
			config.viewmodel.idleX = jsonNumber(*viewmodel, "idleX", config.viewmodel.idleX);
			config.viewmodel.idleY = jsonNumber(*viewmodel, "idleY", config.viewmodel.idleY);
			config.viewmodel.idleZ = jsonNumber(*viewmodel, "idleZ", config.viewmodel.idleZ);
		}
	}

	inline void loadPlayerPoseFiles(playerPoseConfig& config) {
		rapidjson::Document animation;
		if (!loadJsonFile("assets/player/animation.json", animation))
			throw std::runtime_error("assets/player/animation.json: missing or invalid player animation config");
		loadAnimationJson(config, animation);
		rapidjson::Document held;
		if (!loadJsonFile("assets/player/held_items.json", held))
			throw std::runtime_error("assets/player/held_items.json: missing or invalid held-item config");
		loadHeldItemsJson(config, held);
	}

	inline const playerPoseConfig& playerPose() {
		static const playerPoseConfig config = [] {
			playerPoseConfig loaded{};
			loadPlayerPoseFiles(loaded);
			return loaded;
		}();
		return config;
	}

}
