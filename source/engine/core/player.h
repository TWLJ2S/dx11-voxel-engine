#pragma once

#include <DirectXMath.h>

#include <server/simulationServer.h>
#include <world/blockShape.h>
#include <core/playerPose.h>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ac {

    namespace dx = DirectX;

    struct cameraData {
        dx::XMFLOAT4X4 _view;
        dx::XMFLOAT4X4 _projection;
        dx::XMFLOAT3 _position;
        // Absolute value is the water animation clock. A negative value tells
        // world shaders that the rendered camera is below a water surface.
        float _waterTime;
        // Camera/player velocity drives the strength and direction of the
        // underwater lens distortion. Kept as a full register for HLSL CB layout.
        dx::XMFLOAT4 _waterMotion;
		dx::XMFLOAT4X4 _viewProjection;
    };

    struct camera {
        cameraData _gpuData;

        float _fov;
        float _aspectRatio;
        float _nearZ;

        camera(const dx::XMFLOAT3& position, const dx::XMFLOAT3& target, float fov, float aspect, float nearZ)
            : _fov(fov), _aspectRatio(aspect), _nearZ(nearZ)
        {
			_gpuData._position = position;
            updateView(target);
            updateProjection();
        }

		void updateView(
			const dx::XMFLOAT3& target,
			const dx::XMFLOAT3& up = { 0.0f, 1.0f, 0.0f }
		) {
            dx::XMVECTOR vPos = dx::XMLoadFloat3(&_gpuData._position);
            dx::XMVECTOR vTarget = dx::XMLoadFloat3(&target);
			dx::XMVECTOR vUp = dx::XMLoadFloat3(&up);

            // 1. Calculate the vector from Pos to Target
            dx::XMVECTOR vDir = dx::XMVectorSubtract(vTarget, vPos);

            // 2. Calculate the length squared (faster than length)
            dx::XMVECTOR vLenSq = dx::XMVector3LengthSq(vDir);
            float lenSq;
            dx::XMStoreFloat(&lenSq, vLenSq);

            // 3. If they are too close (epsilon), nudge the target forward
            // This prevents the "Forward" vector from becoming {0,0,0}
            if (lenSq < 0.00001f) {
                vTarget = dx::XMVectorAdd(vPos, dx::XMVectorSet(0.0f, 0.0f, 0.1f, 0.0f));
            }

            dx::XMMATRIX mView = dx::XMMatrixLookAtLH(vPos, vTarget, vUp);
            dx::XMStoreFloat4x4(&_gpuData._view, mView);
        }

        cameraData getGPUData() const {
            cameraData data{};

            data._view = _gpuData._view;
            data._projection = _gpuData._projection;
            data._position = _gpuData._position;
            data._waterTime = _gpuData._waterTime;
            data._waterMotion = _gpuData._waterMotion;

            dx::XMStoreFloat4x4(
                &data._view,
                dx::XMMatrixTranspose(
                    dx::XMLoadFloat4x4(&_gpuData._view)
                )
            );

            dx::XMStoreFloat4x4(
                &data._projection,
                dx::XMMatrixTranspose(
                    dx::XMLoadFloat4x4(&_gpuData._projection)
                )
            );
			dx::XMStoreFloat4x4(
				&data._viewProjection,
				dx::XMMatrixTranspose(
					dx::XMLoadFloat4x4(&_gpuData._view) *
					dx::XMLoadFloat4x4(&_gpuData._projection)
				)
			);

            return data;
        }

        void updateProjection() {
            float yScale = 1.0f / tanf(_fov * 0.5f);
            float xScale = yScale / _aspectRatio;

            dx::XMFLOAT4X4 p{};

            p._11 = xScale;
            p._22 = yScale;
            p._33 = 1.0f;
            p._34 = 1.0f;
            p._43 = -_nearZ;

            dx::XMStoreFloat4x4(&_gpuData._projection, dx::XMLoadFloat4x4(&p));
        }
    };

    class player {
    private:
        camera _camera;
        dx::XMFLOAT2 _angle;
        dx::XMFLOAT3 _velocity;
        float _sensitivity;
		bool _grounded = false;
		bool _crouching = false;
		bool _sprinting = false;
		bool _jumpHeld = false;
		bool _flying = false;
		bool _justLanded = false;
		bool _inWater = false;
		bool _submerged = false;
		bool _swimming = false;
		float _swimBlend = 0.0f;
		uint32_t _jumpCooldownTicks = 0u;
		float _landingDragRemaining = 0.0f;
		float _landingDragDuration = .30f;
		float _landingMomentumRetention = .55f;
		float _flightToggleRemaining = 0.0f;

		playerCollisionBox currentBox() const {
			return blendedPlayerBox(_swimBlend, _crouching);
		}

		template<typename Fluid>
		static bool sampleInWater(
			const dx::XMFLOAT3& eye,
			const playerCollisionBox& box,
			Fluid&& fluid
		) {
			return fluid(eye.x, eye.y - box.feetBelowEye + 0.12f, eye.z) ||
				fluid(eye.x, eye.y - box.feetBelowEye * 0.5f, eye.z) ||
				fluid(eye.x, eye.y, eye.z);
		}

		template<typename Solid>
		bool tryApplyPose(
			bool swimming,
			bool crouching,
			dx::XMFLOAT3& eye,
			Solid&& solid
		) {
			if (_swimming == swimming && _crouching == crouching)
				return true;
			const playerCollisionBox from = currentBox();
			const playerCollisionBox to = posePlayerBox(swimming, crouching);
			const float feetY = eye.y - from.feetBelowEye;
			const dx::XMFLOAT3 nextEye{ eye.x, feetY + to.feetBelowEye, eye.z };
			if (collides(nextEye, solid, to))
				return false;
			_swimming = swimming;
			_crouching = crouching;
			return true;
		}

		template<typename Solid>
		static bool collides(
			const dx::XMFLOAT3& eye,
			Solid&& solid,
			const playerCollisionBox& box
		) {
			constexpr float epsilon = 0.001f;
			const int32_t minX = static_cast<int32_t>(std::floor(eye.x - box.radius + epsilon));
			const int32_t maxX = static_cast<int32_t>(std::floor(eye.x + box.radius - epsilon));
			const int32_t minY = static_cast<int32_t>(std::floor(eye.y - box.feetBelowEye + epsilon));
			const int32_t maxY = static_cast<int32_t>(std::floor(eye.y + box.headAboveEye - epsilon));
			const int32_t minZ = static_cast<int32_t>(std::floor(eye.z - box.radius + epsilon));
			const int32_t maxZ = static_cast<int32_t>(std::floor(eye.z + box.radius - epsilon));
			for (int32_t z = minZ; z <= maxZ; ++z)
				for (int32_t y = minY; y <= maxY; ++y)
					for (int32_t x = minX; x <= maxX; ++x)
						if (solid(x, y, z, eye.x, eye.y, eye.z)) return true;
			return false;
		}

		template<typename Solid>
		bool collides(const dx::XMFLOAT3& eye, Solid&& solid) const {
			return collides(eye, solid, currentBox());
		}

		template<typename Solid>
		void moveAxis(
			dx::XMFLOAT3& position,
			float distance,
			uint32_t axis,
			Solid&& solid,
			bool allowStepUp = false
		) {
			if (std::abs(distance) < 1.0e-7f) return;
			const uint32_t steps = (std::max)(1u, static_cast<uint32_t>(std::ceil(std::abs(distance) / .08f)));
			const float step = distance / static_cast<float>(steps);
			for (uint32_t i = 0; i < steps; ++i) {
				dx::XMFLOAT3 candidate = position;
				if (axis == 0u) candidate.x += step;
				else if (axis == 1u) candidate.y += step;
				else candidate.z += step;
				if (collides(candidate, solid)) {
					// Auto-step onto slabs and other short ledges without jumping.
					if (allowStepUp && !_swimming && axis != 1u) {
						constexpr float stepHeight = 0.55f;
						dx::XMFLOAT3 raised = position;
						raised.y += stepHeight;
						dx::XMFLOAT3 raisedMove = candidate;
						raisedMove.y += stepHeight;
						if (!collides(raised, solid) && !collides(raisedMove, solid)) {
							position = raisedMove;
							continue;
						}
					}
					if (axis == 0u) _velocity.x = 0.0f;
					else if (axis == 1u) {
						if (step < 0.0f) _grounded = true;
						_velocity.y = 0.0f;
					}
					else _velocity.z = 0.0f;
					return;
				}
				position = candidate;
			}
		}

    public:
        player(const dx::XMFLOAT3& pos, const dx::XMFLOAT3& target, float fov, float aspect, float nearZ)
            : _camera(pos, target, fov, aspect, nearZ), _angle(0.0f, 0.0f), _velocity(0.0f, 0.0f, 0.0f), _sensitivity(0.0007f)
        {
            dx::XMVECTOR vPos = dx::XMLoadFloat3(&pos);
            dx::XMVECTOR vTarget = dx::XMLoadFloat3(&target);
            dx::XMVECTOR vDir = dx::XMVector3Normalize(dx::XMVectorSubtract(vTarget, vPos));

            dx::XMFLOAT3 dir;
            dx::XMStoreFloat3(&dir, vDir);

            _angle.x = atan2f(dir.z, dir.x); // yaw
            _angle.y = asinf(dir.y);         // pitch
        }

		template<typename Solid, typename Fluid>
		void update(const serverInput& input, const dx::XMFLOAT3& friction,
			float speedMul, float dt, Solid&& solid, Fluid&& fluid,
			bool spectator = false, bool creative = false) {
			_justLanded = false;
            // 1. Update Rotation Angles
			constexpr float maximumViewPitch = 1.55334306f;
			_angle.y = std::clamp(
				_angle.y + (input.lookY * _sensitivity),
				-maximumViewPitch,
				 maximumViewPitch
			);

            // Keep yaw within 0 to 2PI range
            _angle.x = fmodf(_angle.x - (input.lookX * _sensitivity), dx::XM_2PI);

            // Pre-calculate trig for direction vectors
            float cp = cosf(_angle.y);
            float sp = sinf(_angle.y);
            float cy = cosf(_angle.x);
            float sy = sinf(_angle.x);

            // 2. Compute Direction Vectors
            // LookDir is where the camera is actually pointing
            dx::XMVECTOR vLookDir = dx::XMVectorSet(cp * cy, sp, cp * sy, 0.0f);

            // MoveForward is strictly on the XZ plane so looking up/down doesn't slow us down
            dx::XMVECTOR vMoveForward = dx::XMVector3Normalize(dx::XMVectorSet(cy, 0.0f, sy, 0.0f));
            dx::XMVECTOR vUp = dx::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            dx::XMVECTOR vRight = dx::XMVector3Cross(vUp, vMoveForward);
			dx::XMFLOAT3 startPosition = _camera._gpuData._position;
			const playerCollisionBox poseForWater = currentBox();
			const bool feetInWater = !spectator && fluid(
				startPosition.x, startPosition.y - poseForWater.feetBelowEye + 0.12f, startPosition.z);
			const bool torsoInWater = !spectator && fluid(
				startPosition.x,
				startPosition.y - poseForWater.feetBelowEye * 0.5f,
				startPosition.z);
			const bool headInWater = !spectator && fluid(
				startPosition.x, startPosition.y, startPosition.z);
			const bool bodyInWater = feetInWater || torsoInWater || headInWater;
			const bool submerged = headInWater;
			const bool surfaceTouch = _swimming && !spectator && fluid(
				startPosition.x, startPosition.y - 0.85f, startPosition.z);
			const bool inWater = bodyInWater || surfaceTouch;
			_inWater = inWater;
			_submerged = submerged;

			// Creative flight mirrors Minecraft's double-tap jump gesture while
			// remaining distinct from spectator mode: blocks still collide normally.
			const bool jumpPressed = input.jump && !_jumpHeld;
			_flightToggleRemaining = (std::max)(0.0f, _flightToggleRemaining - dt);
			if (!creative || spectator) {
				_flying = false;
				_flightToggleRemaining = 0.0f;
			}
			else if (jumpPressed) {
				if (_flightToggleRemaining > 0.0f) {
					_flying = !_flying;
					_flightToggleRemaining = 0.0f;
					_velocity.y = 0.0f;
					_grounded = false;
					_landingDragRemaining = 0.0f;
					_jumpCooldownTicks = 0u;
				}
				else {
					_flightToggleRemaining = 0.30f;
				}
			}
			const bool flying = creative && _flying && !spectator;
			const bool movementInWater = inWater && !flying;

            // 3. Handle Keyboard Input
            dx::XMVECTOR vInput = dx::XMVectorZero();
			const bool hasMoveInput = input.forward || input.backward || input.left || input.right;
			const bool wantSwim = !spectator && !flying && inWater && !input.crouch &&
				hasMoveInput && input.sprint;
			const bool wantCrouch = !spectator && !flying && !wantSwim && !inWater && input.crouch;
			if (spectator) {
				_swimming = false;
				_crouching = false;
				_swimBlend = 0.0f;
			}
			else {
				const playerCollisionBox poseBefore = currentBox();
				const float feetY = startPosition.y - poseBefore.feetBelowEye;
				tryApplyPose(wantSwim, wantCrouch, startPosition, solid);
				const float blendTarget = _swimming ? 1.0f : 0.0f;
				const float blendRate = _swimming ? playerPose().swim.enterRate : playerPose().swim.exitRate;
				float nextBlend = _swimBlend;
				if (nextBlend < blendTarget)
					nextBlend = (std::min)(blendTarget, nextBlend + blendRate * dt);
				else if (nextBlend > blendTarget)
					nextBlend = (std::max)(blendTarget, nextBlend - blendRate * dt);
				const playerCollisionBox poseAfter = blendedPlayerBox(nextBlend, _crouching);
				dx::XMFLOAT3 blendedEye{
					startPosition.x,
					feetY + poseAfter.feetBelowEye,
					startPosition.z
				};
				// Surface swimming keeps the camera on the waterline instead of
				// dropping the eye into the swim box under the surface.
				if (_swimming && !headInWater)
					blendedEye.y = (std::max)(blendedEye.y, startPosition.y);
				if (collides(blendedEye, solid, poseAfter)) {
					blendedEye.y = feetY + poseBefore.feetBelowEye;
				}
				else {
					_swimBlend = nextBlend;
				}
				startPosition = blendedEye;
			}

			const dx::XMVECTOR waterForward = _swimming ? vLookDir : vMoveForward;
            if (input.forward) vInput = dx::XMVectorAdd(vInput, movementInWater ? waterForward : vMoveForward);
            if (input.backward) vInput = dx::XMVectorSubtract(vInput, movementInWater ? waterForward : vMoveForward);
            if (input.left) vInput = dx::XMVectorSubtract(vInput, vRight);
            if (input.right) vInput = dx::XMVectorAdd(vInput, vRight);

            // Normalize horizontal input so diagonals aren't faster
            if (dx::XMVector3Greater(dx::XMVector3LengthSq(vInput), dx::XMVectorZero())) {
                vInput = dx::XMVector3Normalize(vInput);
            }
			if (spectator || flying) {
				float vertical = 0.0f;
				if (input.jump) vertical += 1.0f;
				if (input.crouch) vertical -= 1.0f;
				vInput = dx::XMVectorSetY(vInput, vertical);
			}
			else if (movementInWater) {
				if (input.jump)
					vInput = dx::XMVectorAdd(vInput, vUp);
				if (input.crouch && !_swimming)
					vInput = dx::XMVectorSubtract(vInput, vUp);
			}

            // 4. Physics and "Grounded" Friction
            dx::XMVECTOR vVel = dx::XMLoadFloat3(&_velocity);

			// Ground movement is responsive, but airborne movement preserves
			// takeoff inertia and permits only small course corrections.
			const float movementControl = spectator || flying || _grounded ? 1.0f :
				(movementInWater ? .82f : .15f);
			const dx::XMVECTOR horizontalInput = dx::XMVectorSetY(vInput, 0.0f);
			const bool hasHorizontalInput = dx::XMVectorGetX(dx::XMVector3LengthSq(horizontalInput)) > 1.0e-6f;
			_sprinting = !spectator && !movementInWater && !_swimming && !_crouching &&
				hasHorizontalInput && input.sprint;
			const float movementScale = spectator ? 1.8f :
				(flying ? (_sprinting ? 3.6f : 1.8f) :
					(_crouching ? .24f : (_sprinting ? 1.25f : 1.0f)));
            if (dx::XMVector3Greater(dx::XMVector3LengthSq(vInput), dx::XMVectorZero())) {
				const dx::XMVECTOR axisScale = _swimming
					? dx::XMVectorReplicate(0.72f)
					: (movementInWater
						? dx::XMVectorSet(.28f, 1.20f, .28f, 0.0f)
						: dx::XMVectorReplicate(movementScale));
				vVel = dx::XMVectorAdd(vVel, dx::XMVectorMultiply(
					vInput,
					dx::XMVectorMultiply(axisScale,
						dx::XMVectorReplicate(speedMul * movementControl * dt))));
            }

			// Releasing movement keys applies a strong braking coefficient. From
			// the 7-block/s cap this stops in about 0.6 block, while held input
			// keeps the normal traction curve and top speed.
			const float groundRetentionX = hasHorizontalInput ? friction.x * .30f : 1.0e-5f;
			const float groundRetentionZ = hasHorizontalInput ? friction.z * .30f : 1.0e-5f;
			const float waterRetention = _swimming ? .05f : .018f;
			const float horizontalRetention = spectator || flying ? friction.x :
				(movementInWater ? waterRetention : (_grounded ? groundRetentionX : .90f));
            dx::XMVECTOR vFricPower = dx::XMVectorSet(
				std::pow(horizontalRetention, dt),
				spectator || flying ? std::pow(friction.y, dt) :
					(movementInWater ? std::pow(_swimming ? .05f : .09f, dt) : 1.0f),
				std::pow(spectator || flying ? friction.z :
					(movementInWater ? waterRetention : (_grounded ? groundRetentionZ : .90f)), dt),
                1.0f
            );
            vVel = dx::XMVectorMultiply(vVel, vFricPower);

            // Store back to class member
			dx::XMStoreFloat3(&_velocity, vVel);
			// The post-landing jump cooldown does not begin until the complete
			// landing-drag envelope has finished smoothing out.
			if (movementInWater) {
				_landingDragRemaining = 0.0f;
				_jumpCooldownTicks = 0u;
			}
			if (!spectator && !flying && !movementInWater &&
				_landingDragRemaining <= 0.0f && _jumpCooldownTicks > 0u) {
				--_jumpCooldownTicks;
			}
			if (!spectator && !flying && _landingDragRemaining > 0.0f) {
				const float previousProgress = 1.0f - _landingDragRemaining /
					(std::max)(_landingDragDuration, .001f);
				const float appliedTime = (std::min)(dt, _landingDragRemaining);
				const float nextRemaining = (std::max)(0.0f, _landingDragRemaining - appliedTime);
				const float nextProgress = 1.0f - nextRemaining /
					(std::max)(_landingDragDuration, .001f);
				auto landingPenaltyWeight = [](float value) {
					value = std::clamp(value, 0.0f, 1.0f);
					// Ease out cubically so the transition-in is short and the slowdown
					// becomes noticeable immediately, while still ending smoothly at 1.
					const float remaining = 1.0f - value;
					return 1.0f - remaining * remaining * remaining;
				};
				// Use cumulative-weight differences so frame rate cannot change the
				// final velocity retention.
				const float landingRetention = std::pow(
					_landingMomentumRetention,
					landingPenaltyWeight(nextProgress) - landingPenaltyWeight(previousProgress)
				);
				_velocity.x *= landingRetention;
				_velocity.z *= landingRetention;
				_landingDragRemaining = nextRemaining;
				if (_landingDragRemaining <= 0.0f) {
					_jumpCooldownTicks = 12u;
				}
			}
			if (_grounded && !hasHorizontalInput) {
				if (std::abs(_velocity.x) < .02f) _velocity.x = 0.0f;
				if (std::abs(_velocity.z) < .02f) _velocity.z = 0.0f;
			}
			const float horizontalSpeed = std::sqrt(_velocity.x * _velocity.x + _velocity.z * _velocity.z);
			if (_swimming) {
				constexpr float maximumSwimSpeed = 3.4f;
				const float swimSpeed = std::sqrt(
					_velocity.x * _velocity.x +
					_velocity.y * _velocity.y +
					_velocity.z * _velocity.z);
				if (swimSpeed > maximumSwimSpeed) {
					const float scale = maximumSwimSpeed / swimSpeed;
					_velocity.x *= scale;
					_velocity.y *= scale;
					_velocity.z *= scale;
				}
			}
			else {
				const float maximumSpeed = spectator ? 12.0f :
					(flying ? (_sprinting ? 21.84f : 10.92f) :
					(movementInWater ? 2.2f :
						(_crouching ? 1.2f : (_sprinting ? 5.75f : 4.0f))));
				if (horizontalSpeed > maximumSpeed) {
					const float scale = maximumSpeed / horizontalSpeed;
					_velocity.x *= scale;
					_velocity.z *= scale;
				}
			}
			if (spectator) _velocity.y = std::clamp(_velocity.y, -12.0f, 12.0f);
			else if (flying) _velocity.y = std::clamp(_velocity.y, -10.92f, 10.92f);

            // 5. Update Position
			dx::XMFLOAT3 position = startPosition;
			if (spectator) {
				_grounded = false;
				_jumpHeld = false;
				_inWater = false;
				_submerged = false;
				_swimming = false;
				_swimBlend = 0.0f;
				position.x += _velocity.x * dt;
				position.y += _velocity.y * dt;
				position.z += _velocity.z * dt;
			}
			else {
				const bool groundedAtFrameStart = _grounded;
				const bool jumpDown = input.jump;
				// Holding jump automatically launches again on the first grounded
				// frame, while still preventing any mid-air jump.
				if (!flying && !movementInWater && jumpDown && _grounded && _landingDragRemaining <= 0.0f &&
					_jumpCooldownTicks == 0u) {
					_velocity.y = 7.5f;
					_grounded = false;
				}
				_jumpHeld = jumpDown;
				if (flying) {
					_landingDragRemaining = 0.0f;
					_jumpCooldownTicks = 0u;
				}
				else if (!movementInWater) {
					_velocity.y += -25.0f * dt;
					_velocity.y = (std::max)(_velocity.y, -40.0f);
				}
				else if (!_swimming)
					_velocity.y += -1.5f * dt;
				if (movementInWater) _velocity.y = std::clamp(_velocity.y, -7.0f, 8.8f);
				// Capture the complete pre-collision velocity. Landing recovery should
				// account for sprinting and lateral momentum as well as fall speed.
				const float impactVelocitySquared =
					_velocity.x * _velocity.x +
					_velocity.y * _velocity.y +
					_velocity.z * _velocity.z;
				_grounded = false;
				moveAxis(position, _velocity.x * dt, 0u, solid, groundedAtFrameStart);
				moveAxis(position, _velocity.z * dt, 2u, solid, groundedAtFrameStart);
				moveAxis(position, _velocity.y * dt, 1u, solid, false);
				if (!spectator) {
					_inWater = sampleInWater(position, currentBox(), fluid) ||
						(_swimming && fluid(position.x, position.y - 0.85f, position.z));
					_submerged = fluid(position.x, position.y, position.z);
				}
				if (flying && _grounded) {
					// Descending onto a block exits flight, matching Creative mode.
					_flying = false;
					_flightToggleRemaining = 0.0f;
				}
				if (!flying && !_inWater && _grounded && !groundedAtFrameStart) {
					_justLanded = true;
					// The separate jump cooldown is armed only when this drag envelope
					// reaches its smoothly eased endpoint.
					_jumpCooldownTicks = 0u;
					// Total impact speed now scales both the strength and duration of the
					// penalty. Fast sprint-jumps therefore need visibly more recovery.
					constexpr float minimumPenaltyVelocitySquared = 2.0f * 2.0f;
					// Reach full severity at 10 blocks/s so ordinary sprint-jumps and hard
					// falls produce a clearly different response from gentle landings.
					constexpr float maximumPenaltyVelocitySquared = 10.0f * 10.0f;
					const float impactSeverity = std::clamp(
						(impactVelocitySquared - minimumPenaltyVelocitySquared) /
						(maximumPenaltyVelocitySquared - minimumPenaltyVelocitySquared),
						0.0f,
						1.0f
					);
					const float momentumLoss = .70f + (.998f - .70f) * impactSeverity;
					_landingMomentumRetention = 1.0f - momentumLoss;
					_landingDragDuration = .15f + (1.00f - .15f) * impactSeverity;
					_landingDragRemaining = _landingDragDuration;
				}
			}
			_camera._gpuData._position = position;
			dx::XMVECTOR vCamPos = dx::XMLoadFloat3(&position);

            // 6. Update Camera Matrices
            // Target is exactly 1 unit in front of the camera position
            dx::XMVECTOR vNewTarget = dx::XMVectorAdd(vCamPos, vLookDir);

            dx::XMFLOAT3 finalTarget;
            dx::XMStoreFloat3(&finalTarget, vNewTarget);

			dx::XMFLOAT3 stableUp;
			dx::XMStoreFloat3(
				&stableUp,
				dx::XMVector3Normalize(dx::XMVector3Cross(vLookDir, vRight))
			);
			_camera.updateView(finalTarget, stableUp);
        }

        camera& getCamera() { return _camera; }

		float getYaw() const { return _angle.x; }
		float getPitch() const { return _angle.y; }
		const dx::XMFLOAT3& getVelocity() const { return _velocity; }
		bool isGrounded() const { return _grounded; }
		bool isCrouching() const { return _crouching; }
		bool isSprinting() const { return _sprinting; }
		bool isFlying() const { return _flying; }
		bool justLanded() const { return _justLanded; }
		bool inWater() const { return _inWater; }
		bool isSubmerged() const { return _submerged; }
		bool isSwimming() const { return _swimming; }
		float swimBlend() const { return _swimBlend; }
		playerCollisionBox collisionBox() const { return currentBox(); }

		void addVelocity(const dx::XMFLOAT3& delta) {
			_velocity.x += delta.x;
			_velocity.y += delta.y;
			_velocity.z += delta.z;
		}

		void teleport(const dx::XMFLOAT3& eyePosition) {
			_camera._gpuData._position = eyePosition;
			_velocity = {};
			_grounded = false;
			_crouching = false;
			_sprinting = false;
			_flying = false;
			_jumpHeld = false;
			_justLanded = false;
			_inWater = false;
			_submerged = false;
			_swimming = false;
			_swimBlend = 0.0f;
			_jumpCooldownTicks = 0u;
			_flightToggleRemaining = 0.0f;
			_landingDragRemaining = 0.0f;
			_landingDragDuration = .30f;
			_landingMomentumRetention = .55f;
		}

		void restoreState(const dx::XMFLOAT3& eyePosition, const dx::XMFLOAT3& velocity,
			float yaw, float pitch, bool grounded, bool flying = false) {
			constexpr float maximumViewPitch = 1.55334306f;
			_camera._gpuData._position = eyePosition;
			_velocity = velocity;
			_angle.x = std::remainder(yaw, dx::XM_2PI);
			_angle.y = std::clamp(pitch, -maximumViewPitch, maximumViewPitch);
			_grounded = grounded;
			_crouching = false;
			_sprinting = false;
			_flying = flying;
			_jumpHeld = false;
			_justLanded = false;
			_inWater = false;
			_submerged = false;
			_swimming = false;
			_swimBlend = 0.0f;
			_jumpCooldownTicks = 0u;
			_flightToggleRemaining = 0.0f;
			_landingDragRemaining = 0.0f;

			const float cp = std::cos(_angle.y);
			const dx::XMFLOAT3 look{
				cp * std::cos(_angle.x),
				std::sin(_angle.y),
				cp * std::sin(_angle.x)
			};
			const dx::XMFLOAT3 target{
				eyePosition.x + look.x,
				eyePosition.y + look.y,
				eyePosition.z + look.z
			};
			const dx::XMVECTOR worldUp = dx::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
			const dx::XMVECTOR forward = dx::XMVectorSet(std::cos(_angle.x), 0.0f, std::sin(_angle.x), 0.0f);
			const dx::XMVECTOR right = dx::XMVector3Normalize(dx::XMVector3Cross(worldUp, forward));
			const dx::XMVECTOR stableUp = dx::XMVector3Normalize(
				dx::XMVector3Cross(dx::XMLoadFloat3(&look), right));
			dx::XMFLOAT3 up{};
			dx::XMStoreFloat3(&up, stableUp);
			_camera.updateView(target, up);
		}

		void clearVelocity() {
			_velocity = {};
			_crouching = false;
			_sprinting = false;
			_flying = false;
			_jumpHeld = false;
			_swimming = false;
			_swimBlend = 0.0f;
			_landingDragRemaining = 0.0f;
			_landingDragDuration = .30f;
			_landingMomentumRetention = .55f;
			_jumpCooldownTicks = 0u;
			_flightToggleRemaining = 0.0f;
		}

		bool occupiesBlock(int32_t x, int32_t y, int32_t z) const {
			return overlapsBox(
				static_cast<float>(x), static_cast<float>(y), static_cast<float>(z),
				static_cast<float>(x + 1), static_cast<float>(y + 1), static_cast<float>(z + 1));
		}

		bool overlapsBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ) const {
			const dx::XMFLOAT3& eye = _camera._gpuData._position;
			const playerCollisionBox box = currentBox();
			constexpr float epsilon = .002f;
			const float pminX = eye.x - box.radius, pmaxX = eye.x + box.radius;
			const float pminY = eye.y - box.feetBelowEye, pmaxY = eye.y + box.headAboveEye;
			const float pminZ = eye.z - box.radius, pmaxZ = eye.z + box.radius;
			return pminX + epsilon < maxX && pmaxX - epsilon > minX &&
				pminY + epsilon < maxY && pmaxY - epsilon > minY &&
				pminZ + epsilon < maxZ && pmaxZ - epsilon > minZ;
		}

		dx::XMFLOAT3 getLookDirection() const {
			const float cosPitch = cosf(_angle.y);
			return {
				cosPitch * cosf(_angle.x),
				sinf(_angle.y),
				cosPitch * sinf(_angle.x)
			};
		}
    };

}
