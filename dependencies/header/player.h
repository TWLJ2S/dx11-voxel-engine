#pragma once

#include <DirectXMath.h>

#include <header/window.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace ac {

    namespace dx = DirectX;

    struct cameraData {
        dx::XMFLOAT4X4 _view;
        dx::XMFLOAT4X4 _projection;
        dx::XMFLOAT3 _position;
        float __padding;
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
		bool _justLanded = false;
		uint32_t _jumpCooldownTicks = 0u;
		float _landingDragRemaining = 0.0f;
		float _landingDragDuration = .30f;
		float _landingMomentumRetention = .55f;

		using solidQuery = std::function<bool(int32_t, int32_t, int32_t)>;

		static bool collides(const dx::XMFLOAT3& eye, const solidQuery& solid) {
			// A symmetric AABB allows the axis-separated solver to slide cleanly
			// along block faces and around corners.
			constexpr float radius = 0.30f;
			constexpr float feetBelowEye = 1.62f;
			constexpr float headAboveEye = 0.18f;
			constexpr float epsilon = 0.001f;
			const int32_t minX = static_cast<int32_t>(std::floor(eye.x - radius + epsilon));
			const int32_t maxX = static_cast<int32_t>(std::floor(eye.x + radius - epsilon));
			const int32_t minY = static_cast<int32_t>(std::floor(eye.y - feetBelowEye + epsilon));
			const int32_t maxY = static_cast<int32_t>(std::floor(eye.y + headAboveEye - epsilon));
			const int32_t minZ = static_cast<int32_t>(std::floor(eye.z - radius + epsilon));
			const int32_t maxZ = static_cast<int32_t>(std::floor(eye.z + radius - epsilon));
			for (int32_t z = minZ; z <= maxZ; ++z)
				for (int32_t y = minY; y <= maxY; ++y)
					for (int32_t x = minX; x <= maxX; ++x)
						if (solid(x, y, z)) return true;
			return false;
		}

		void moveAxis(dx::XMFLOAT3& position, float distance, uint32_t axis, const solidQuery& solid) {
			if (std::abs(distance) < 1.0e-7f) return;
			const uint32_t steps = (std::max)(1u, static_cast<uint32_t>(std::ceil(std::abs(distance) / .08f)));
			const float step = distance / static_cast<float>(steps);
			for (uint32_t i = 0; i < steps; ++i) {
				dx::XMFLOAT3 candidate = position;
				if (axis == 0u) candidate.x += step;
				else if (axis == 1u) candidate.y += step;
				else candidate.z += step;
				if (collides(candidate, solid)) {
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

		void update(GLFWwindow* window, const dx::XMFLOAT2& dDir, const dx::XMFLOAT3& friction,
			float speedMul, float dt, const solidQuery& solid, bool spectator = false) {
			_justLanded = false;
            // 1. Update Rotation Angles
			constexpr float maximumViewPitch = 89.0f * (dx::XM_PI / 180.0f);
			_angle.y = std::clamp(
				_angle.y + (dDir.y * _sensitivity),
				-maximumViewPitch,
				 maximumViewPitch
			);

            // Keep yaw within 0 to 2PI range
            _angle.x = fmodf(_angle.x - (dDir.x * _sensitivity), dx::XM_2PI);

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

            // 3. Handle Keyboard Input
            dx::XMVECTOR vInput = dx::XMVectorZero();

            if (glfwGetKey(window, GLFW_KEY_W)) vInput = dx::XMVectorAdd(vInput, vMoveForward);
            if (glfwGetKey(window, GLFW_KEY_S)) vInput = dx::XMVectorSubtract(vInput, vMoveForward);
            if (glfwGetKey(window, GLFW_KEY_A)) vInput = dx::XMVectorSubtract(vInput, vRight);
            if (glfwGetKey(window, GLFW_KEY_D)) vInput = dx::XMVectorAdd(vInput, vRight);

            // Normalize horizontal input so diagonals aren't faster
            if (dx::XMVector3Greater(dx::XMVector3LengthSq(vInput), dx::XMVectorZero())) {
                vInput = dx::XMVector3Normalize(vInput);
            }
			if (spectator) {
				float vertical = 0.0f;
				if (glfwGetKey(window, GLFW_KEY_SPACE)) vertical += 1.0f;
				if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT)) vertical -= 1.0f;
				vInput = dx::XMVectorSetY(vInput, vertical);
			}

            // 4. Physics and "Grounded" Friction
            dx::XMVECTOR vVel = dx::XMLoadFloat3(&_velocity);

			// Ground movement is responsive, but airborne movement preserves
			// takeoff inertia and permits only small course corrections.
			const float movementControl = spectator || _grounded ? 1.0f : 0.15f;
			const dx::XMVECTOR horizontalInput = dx::XMVectorSetY(vInput, 0.0f);
			const bool hasHorizontalInput = dx::XMVectorGetX(dx::XMVector3LengthSq(horizontalInput)) > 1.0e-6f;
			_crouching = !spectator && glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
			_sprinting = !spectator && !_crouching && hasHorizontalInput &&
				glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS;
			const float movementScale = _crouching ? .24f : (_sprinting ? 1.25f : 1.0f);
            if (dx::XMVector3Greater(dx::XMVector3LengthSq(vInput), dx::XMVectorZero())) {
				vVel = dx::XMVectorMultiplyAdd(
					vInput,
					dx::XMVectorReplicate(speedMul * movementScale * movementControl * dt),
					vVel
				);
            }

			// Releasing movement keys applies a strong braking coefficient. From
			// the 7-block/s cap this stops in about 0.6 block, while held input
			// keeps the normal traction curve and top speed.
			const float groundRetentionX = hasHorizontalInput ? friction.x * .30f : 1.0e-5f;
			const float groundRetentionZ = hasHorizontalInput ? friction.z * .30f : 1.0e-5f;
			const float horizontalRetention = spectator ? friction.x : (_grounded ? groundRetentionX : .90f);
            dx::XMVECTOR vFricPower = dx::XMVectorSet(
				std::pow(horizontalRetention, dt),
				spectator ? std::pow(friction.y, dt) : 1.0f,
				std::pow(spectator ? friction.z : (_grounded ? groundRetentionZ : .90f), dt),
                1.0f
            );
            vVel = dx::XMVectorMultiply(vVel, vFricPower);

            // Store back to class member
			dx::XMStoreFloat3(&_velocity, vVel);
			// The post-landing jump cooldown does not begin until the complete
			// landing-drag envelope has finished smoothing out.
			if (!spectator && _landingDragRemaining <= 0.0f && _jumpCooldownTicks > 0u) {
				--_jumpCooldownTicks;
			}
			if (!spectator && _landingDragRemaining > 0.0f) {
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
			const float maximumSpeed = _crouching ? 1.2f : (_sprinting ? 5.75f : 4.0f);
			if (horizontalSpeed > maximumSpeed) {
				const float scale = maximumSpeed / horizontalSpeed;
				_velocity.x *= scale;
				_velocity.z *= scale;
			}

            // 5. Update Position
			dx::XMFLOAT3 position = _camera._gpuData._position;
			if (spectator) {
				_grounded = false;
				_jumpHeld = false;
				position.x += _velocity.x * dt;
				position.y += _velocity.y * dt;
				position.z += _velocity.z * dt;
			}
			else {
				const bool groundedAtFrameStart = _grounded;
				const bool jumpDown = glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS;
				// Holding jump automatically launches again on the first grounded
				// frame, while still preventing any mid-air jump.
				if (jumpDown && _grounded && _landingDragRemaining <= 0.0f &&
					_jumpCooldownTicks == 0u) {
					_velocity.y = 7.5f;
					_grounded = false;
				}
				_jumpHeld = jumpDown;
				_velocity.y += -25.0f * dt;
				// Capture the complete pre-collision velocity. Landing recovery should
				// account for sprinting and lateral momentum as well as fall speed.
				const float impactVelocitySquared =
					_velocity.x * _velocity.x +
					_velocity.y * _velocity.y +
					_velocity.z * _velocity.z;
				_grounded = false;
				moveAxis(position, _velocity.x * dt, 0u, solid);
				moveAxis(position, _velocity.z * dt, 2u, solid);
				moveAxis(position, _velocity.y * dt, 1u, solid);
				if (_grounded && !groundedAtFrameStart) {
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
		bool justLanded() const { return _justLanded; }

		void teleport(const dx::XMFLOAT3& eyePosition) {
			_camera._gpuData._position = eyePosition;
			_velocity = {};
			_grounded = false;
			_crouching = false;
			_sprinting = false;
			_justLanded = false;
			_jumpCooldownTicks = 0u;
			_landingDragRemaining = 0.0f;
			_landingDragDuration = .30f;
			_landingMomentumRetention = .55f;
		}

		void clearVelocity() {
			_velocity = {};
			_crouching = false;
			_sprinting = false;
			_landingDragRemaining = 0.0f;
			_landingDragDuration = .30f;
			_landingMomentumRetention = .55f;
			_jumpCooldownTicks = 0u;
		}

		bool occupiesBlock(int32_t x, int32_t y, int32_t z) const {
			const dx::XMFLOAT3& eye = _camera._gpuData._position;
			constexpr float epsilon = .002f;
			const float minX = eye.x - .30f, maxX = eye.x + .30f;
			const float minY = eye.y - 1.62f, maxY = eye.y + .18f;
			const float minZ = eye.z - .30f, maxZ = eye.z + .30f;
			// Face contact is not volume overlap. The inset permits placing a
			// support block while the feet merely touch its top at a ledge, while
			// still rejecting every block that intersects the player's hitbox.
			return minX + epsilon < static_cast<float>(x + 1) && maxX - epsilon > static_cast<float>(x) &&
				minY + epsilon < static_cast<float>(y + 1) && maxY - epsilon > static_cast<float>(y) &&
				minZ + epsilon < static_cast<float>(z + 1) && maxZ - epsilon > static_cast<float>(z);
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
