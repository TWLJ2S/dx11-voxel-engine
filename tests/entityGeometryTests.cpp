#include <world/entitySystem.h>
#include <core/animationSystem.h>
#include <core/applicationConfig.h>
#include <core/playerController.h>
#include <core/playerPose.h>

#include <cmath>
#include <iostream>

int main() {
	try {
		ac::entityDefinitionRegistry fixture;
		fixture.load({ "tests/fixtures/entity_geometry/entities.json" });
		const ac::livingEntityDefinition* polyhedron = fixture.get(0u);
		if (!polyhedron || polyhedron->meshes.size() != 1u ||
			polyhedron->meshes.front().vertices.size() != 4u ||
			polyhedron->meshes.front().faces.size() != 4u ||
			polyhedron->collisionHulls.size() != 1u) {
			std::cerr << "polygon model or collision hull was not loaded\n";
			return 1;
		}

		ac::entityDefinitionRegistry registry;
		registry.load({ "assets/entities/entities.json" });
		const ac::livingEntityDefinition* pig = registry.get(0u);
		if (!pig || pig->boxes.size() != 7u || pig->collisionBoxes.size() != 1u ||
			std::abs(pig->boxes[0].maximum.x - pig->boxes[0].minimum.x - 0.625f) > 0.0001f ||
			std::abs(pig->boxes[0].maximum.z - pig->boxes[0].minimum.z - 1.0f) > 0.0001f ||
			pig->maxHealth != 10.0f) {
			std::cerr << "pig model, health, or Minecraft-sized hitbox is incorrect\n";
			return 1;
		}
		const ac::livingEntityDefinition* sheep = registry.get(1u);
		if (!sheep || sheep->boxes.size() < 6u ||
			sheep->boxes[1].textureDimensions.y != 6.0f ||
			sheep->boxes[2].textureDimensions.y != 6.0f ||
			std::abs(sheep->boxes[0].maximum.x - sheep->boxes[0].minimum.x - 0.5f) > 0.0001f ||
			std::abs(sheep->boxes[0].maximum.z - sheep->boxes[0].minimum.z - 1.0f) > 0.0001f ||
			sheep->renderLayers.size() != 1u ||
			!sheep->renderLayers[0].hideWhenSheared ||
			sheep->renderLayers[0].boxes.size() != 6u ||
			!sheep->shearable || sheep->maxHealth != 8.0f ||
			sheep->collisionBoxes.size() != 1u ||
			std::abs(sheep->collisionBoxes[0].maximum.y - 1.3f) > 0.0001f) {
			std::cerr << "sheep base model, wool layer, health, or hitbox is incorrect\n";
			return 1;
		}
		const ac::livingEntityDefinition* zombie = registry.get(2u);
		if (!zombie || zombie->boxes.size() != 6u ||
			zombie->boxes[2].uv.x != 40.0f || zombie->boxes[2].uv.y != 16.0f ||
			zombie->boxes[3].uv.x != 40.0f || zombie->boxes[3].uv.y != 16.0f ||
			zombie->boxes[4].uv.x != 0.0f || zombie->boxes[4].uv.y != 16.0f ||
			zombie->boxes[5].uv.x != 0.0f || zombie->boxes[5].uv.y != 16.0f) {
			std::cerr << "zombie limbs do not use the populated legacy atlas regions\n";
			return 1;
		}
		if (zombie->movementTiming.size() != 4u) {
			std::cerr << "zombie movement timing track was not loaded\n";
			return 1;
		}
		if (std::abs(zombie->attackDamage - 3.0f) > 0.0001f ||
			std::abs(zombie->attackRange - 1.65f) > 0.0001f ||
			std::abs(zombie->attackCooldown - 1.0f) > 0.0001f ||
			std::abs(zombie->knockback - 3.2f) > 0.0001f) {
			std::cerr << "zombie combat tuning was not loaded\n";
			return 1;
		}
		const std::vector<ac::animationTimingSegment> timing = {
			{ 0.4f, 1.0f, ac::animationEasing::linear },
			{ 0.2f, 0.0f, ac::animationEasing::linear },
			{ 0.4f, 1.0f, ac::animationEasing::linear }
		};
		const float pauseStart = ac::remapAnimationPhase(0.41f, timing);
		const float pauseEnd = ac::remapAnimationPhase(0.59f, timing);
		if (std::abs(pauseStart - 0.5f) > 0.0001f ||
			std::abs(pauseEnd - 0.5f) > 0.0001f ||
			ac::remapAnimationPhase(0.8f, timing) <= pauseEnd ||
			ac::remapAnimationTime(1.8f, timing) <= 1.5f) {
			std::cerr << "animation timing pauses or variable speeds are incorrect\n";
			return 1;
		}

		ac::animationCurve curve;
		curve.keys = {
			{ 0.0f, 0.0f, 0.0f, 0.0f, ac::animationInterpolation::cubic },
			{ 1.0f, 1.0f, 0.0f, 0.0f, ac::animationInterpolation::linear }
		};
		curve.preExtrapolation = ac::animationExtrapolation::loop;
		curve.postExtrapolation = ac::animationExtrapolation::pingPong;
		if (std::abs(curve.evaluate(0.5f) - 0.5f) > 0.0001f ||
			std::abs(curve.evaluate(-0.25f) - 0.84375f) > 0.0001f ||
			std::abs(curve.evaluate(1.25f) - 0.84375f) > 0.0001f) {
			std::cerr << "animation curve interpolation or extrapolation is incorrect\n";
			return 1;
		}

		ac::animation clip;
		clip._duration = 1.0f;
		clip._ticksPerSecond = 1.0f;
		clip._loop = false;
		ac::animationChannel channel;
		channel._boneId = 0u;
		ac::keyframe start;
		start._time = 0.0f;
		start._position = { 0.0f, 0.0f, 0.0f };
		start._positionOutTangent = { 0.0f, 0.0f, 0.0f };
		start._interpolation = ac::animationInterpolation::cubic;
		ac::keyframe end;
		end._time = 1.0f;
		end._position = { 2.0f, 0.0f, 0.0f };
		end._positionInTangent = { 0.0f, 0.0f, 0.0f };
		channel._keyframes = { start, end };
		clip._channels.push_back(channel);
		ac::animationPose sampled;
		ac::sampleAnimationClip(clip, 0.5f, sampled, 2u);
		ac::animationPose blended;
		ac::animationPose bindPose(2u);
		const std::vector<float> mask = { 0.5f, 0.0f };
		ac::blendAnimationPoses(bindPose, sampled, 1.0f, blended, &mask);
		if (sampled.size() != 2u || std::abs(sampled[0].position.x - 1.0f) > 0.0001f ||
			std::abs(blended[0].position.x - 0.5f) > 0.0001f ||
			std::abs(blended[1].scale.x - 1.0f) > 0.0001f) {
			std::cerr << "skeletal clip sampling or masked pose blending is incorrect\n";
			return 1;
		}
		const auto& controller = ac::playerController();
		const auto& application = ac::appConfig();
		const auto& pose = ac::playerPose();
		if (std::abs(controller.movement.sprintSpeed - 5.75f) > 0.0001f ||
			std::abs(controller.collision.crouching.feetBelowEye - 1.27f) > 0.0001f ||
			application.blockAliases.at("magma") != "core:magma_block" ||
			application.defaultHotbar.size() != 9u ||
			std::abs(pose.camera.thirdPersonDistance - 3.25f) > 0.0001f ||
			std::abs(pose.camera.motionResponse - 8.0f) > 0.0001f) {
			std::cerr << "runtime JSON tuning configuration was not loaded\n";
			return 1;
		}
		std::cout << "entity polygon model and collision definitions passed\n";
		return 0;
	}
	catch (const std::exception& error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}
