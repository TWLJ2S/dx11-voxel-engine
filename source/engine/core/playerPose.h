#pragma once

#include <DirectXMath.h>
#include <rapidjson/document.h>

#include <core/settingsStore.h>

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
		float chopYaw = 0.06f;
		float chopRoll = 0.08f;
	};

	struct heldItemPose {
		poseVec3 translation{};
		poseVec3 rotationDeg{};
		poseScale scaleFirst{ 1.0f, 1.0f, 1.0f };
		poseScale scaleThird{ 1.0f, 1.0f, 1.0f };
		poseVec3 fist{ -0.375f, 0.76f, 0.04f };
	};

	struct playerPoseConfig {
		struct {
			float armSwing = 0.78f;
			float legSwing = 0.72f;
			float strideCrouch = 0.30f;
			float strideSprint = 0.185f;
			float strideWalk = 0.18f;
			float airborneKeep = 0.82f;
			float airborneMin = 0.22f;
			float poseResponse = 7.0f;
			float airborneStrideMin = 0.45f;
			float referenceSwim = 3.4f;
			float referenceSprint = 5.75f;
			float referenceCrouch = 1.6f;
			float referenceWalk = 4.0f;
		} walk;

		struct {
			float enterRate = 3.6f;
			float exitRate = 2.8f;
			float eyePivotBase = 1.625f;
			float eyePivotLift = 0.22f;
			float standFeet = 1.625f;
			float crouchFeet = 1.27f;
			float strideSpeed = 0.28f;
			float strideMin = 0.62f;
			float bodyPitch = 0.04f;
			float headPitch = 0.06f;
			float armPitch = 0.0f;
			float armYaw = 0.10f;
			float armSpread = 0.18f;
			float armSweep = DirectX::XM_PIDIV2;
			float armTuck = 0.104719755f;
			float holdForward = 0.18f;
			float sweepEnd = 0.58f;
			float holdBack = 0.66f;
			float legPitch = 0.08f;
			float legKick = 0.40f;
			float legRoll = 0.08f;
			float legRollKick = 0.10f;
			float twoHandLimit = 0.45f;
		} swim;

		struct {
			float retrigger = 0.05f;
			float toolDuration = 0.42f;
			float toolPitch = 0.85f;
			float toolRoll = 0.22f;
			float handDuration = 0.40f;
			float handPitch = 0.62f;
			float handRoll = 0.18f;
		} use;

		struct {
			float toolDuration = 0.32f;
			float toolPitch = 0.55f;
			float toolRoll = 0.16f;
			float handDuration = 0.28f;
			float handPitch = 1.22f;
			float handRoll = 0.62f;
		} attack;

		struct {
			float torsoPitch = 0.12f;
			float legPitch = -0.20f;
		} crouch;

		struct {
			float armPitch = -0.14f;
			float legPitch = 0.18f;
		} airborne;

		struct {
			float pitchFollow = 0.75f;
		} head;

		struct {
			float raiseSeconds = 0.20f;
			float lowerSeconds = 0.16f;
			float walkDampen = 0.88f;
			float bob = 0.06f;
		} toolRaise;

		struct {
			armReadyPose rightFirst{ -0.72f, 0.18f, 0.24f, 0.22f, 0.06f, 0.08f };
			armReadyPose rightThird{ -0.70f, 0.12f, 0.34f, 0.40f, 0.06f, 0.08f };
			armReadyPose leftFirst{ -0.76f, -0.20f, -0.26f, 0.20f, 0.05f, 0.06f };
			armReadyPose leftThird{ -0.78f, -0.12f, -0.32f, 0.38f, 0.05f, 0.06f };
		} twoHand;

		heldItemPose tool{
			{ 0.22f, 0.22f, 0.0f },
			{ 90.0f, -45.0f, -90.0f },
			{ 1.15f, 1.15f, 1.15f },
			{ 1.28f, 1.28f, 1.28f },
			{ -0.375f, 0.76f, 0.08f }
		};
		heldItemPose rod{
			{ 0.0f, 0.32f, 0.0f },
			{ 90.0f, 0.0f, 0.0f },
			{ 0.28f, 0.72f, 0.28f },
			{ 0.30f, 0.78f, 0.30f },
			{ -0.375f, 0.76f, 0.06f }
		};
		heldItemPose cross{
			{},
			{ 8.0f, -12.0f, 8.0f },
			{ 0.62f, 0.62f, 0.62f },
			{ 0.68f, 0.68f, 0.68f },
			{ -0.375f, 0.76f, 0.04f }
		};
		heldItemPose sprite{
			{},
			{ 10.0f, 0.0f, 0.0f },
			{ 0.62f, 0.62f, 0.62f },
			{ 0.68f, 0.68f, 0.68f },
			{ -0.375f, 0.76f, 0.04f }
		};
		heldItemPose block{
			{},
			{ 75.0f, 45.0f, 0.0f },
			{ 0.48f, 0.48f, 0.48f },
			{ 0.52f, 0.52f, 0.52f },
			{ -0.375f, 0.80f, 0.04f }
		};
		heldItemPose slab{
			{},
			{ 75.0f, 45.0f, 0.0f },
			{ 0.48f, 0.48f, 0.48f },
			{ 0.52f, 0.52f, 0.52f },
			{ -0.375f, 0.80f, 0.04f }
		};

		struct {
			poseVec3 poseDeg{ -1.25663706f, -0.34906585f, 0.27925268f };
			poseVec3 offset{ 0.30f, -0.40f, 0.58f };
			float swingPitch = 0.70f;
			float swingPitchUse = 0.65f;
			float swingYaw = 0.38f;
			float swingRoll = 0.28f;
			float walkBobPitch = 0.10f;
			float walkBobRoll = 0.12f;
			float idlePitch = 0.045f;
			float idleYaw = 0.03f;
			float idleRoll = 0.04f;
			float idleRate = 1.7f;
			float idle2Rate = 2.3f;
			float idle2Phase = 0.8f;
			float airbornePitch = 0.08f;
			float airborneY = 0.03f;
			float airborneRate = 2.5f;
			float walkX = 0.028f;
			float walkY = 0.040f;
			float swingY = 0.20f;
			float swingZ = 0.16f;
			float idleX = 0.010f;
			float idleY = 0.014f;
			float idleZ = 0.008f;
		} viewmodel;

		struct {
			float faceOffset = 0.20f;
			float swimOffset = 0.22f;
			float radius = 0.075f;
		} camera;
	};

	inline float jsonNumber(const rapidjson::Value& object, const char* key, float fallback) {
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsNumber())
			return fallback;
		return object[key].GetFloat();
	}

	inline float jsonDegrees(const rapidjson::Value& object, const char* key, float fallbackRadians) {
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsNumber())
			return fallbackRadians;
		return DirectX::XMConvertToRadians(object[key].GetFloat());
	}

	inline const rapidjson::Value* jsonChild(const rapidjson::Value& object, const char* key) {
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsObject())
			return nullptr;
		return &object[key];
	}

	inline poseVec3 jsonVec3(const rapidjson::Value& object, const char* key, poseVec3 fallback) {
		if (!object.IsObject() || !object.HasMember(key) || !object[key].IsArray() || object[key].Size() < 3)
			return fallback;
		const auto& array = object[key];
		return {
			array[0].IsNumber() ? array[0].GetFloat() : fallback.x,
			array[1].IsNumber() ? array[1].GetFloat() : fallback.y,
			array[2].IsNumber() ? array[2].GetFloat() : fallback.z
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
		if (!object.IsObject() || !object.HasMember(key))
			return fallback;
		const auto& value = object[key];
		if (value.IsNumber()) {
			const float scale = value.GetFloat();
			return { scale, scale, scale };
		}
		if (value.IsArray() && value.Size() >= 3) {
			return {
				value[0].IsNumber() ? value[0].GetFloat() : fallback.x,
				value[1].IsNumber() ? value[1].GetFloat() : fallback.y,
				value[2].IsNumber() ? value[2].GetFloat() : fallback.z
			};
		}
		return fallback;
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
		}
		if (const rapidjson::Value* attack = jsonChild(document, "attack")) {
			config.attack.toolDuration = jsonNumber(*attack, "toolDuration", config.attack.toolDuration);
			config.attack.toolPitch = jsonDegrees(*attack, "toolPitchDeg", config.attack.toolPitch);
			config.attack.toolRoll = jsonDegrees(*attack, "toolRollDeg", config.attack.toolRoll);
			config.attack.handDuration = jsonNumber(*attack, "handDuration", config.attack.handDuration);
			config.attack.handPitch = jsonDegrees(*attack, "handPitchDeg", config.attack.handPitch);
			config.attack.handRoll = jsonDegrees(*attack, "handRollDeg", config.attack.handRoll);
		}
		if (const rapidjson::Value* crouch = jsonChild(document, "crouch")) {
			config.crouch.torsoPitch = jsonDegrees(*crouch, "torsoPitchDeg", config.crouch.torsoPitch);
			config.crouch.legPitch = jsonDegrees(*crouch, "legPitchDeg", config.crouch.legPitch);
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
		if (loadJsonFile("assets/player/animation.json", animation))
			loadAnimationJson(config, animation);
		rapidjson::Document held;
		if (loadJsonFile("assets/player/held_items.json", held))
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
