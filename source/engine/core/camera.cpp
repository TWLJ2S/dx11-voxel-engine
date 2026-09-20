#include "camera.h"

#include <cmath>

namespace ac {
	camera::camera() {
		DirectX::XMStoreFloat4x4(&_viewMatrix, DirectX::XMMatrixIdentity());
		DirectX::XMStoreFloat4x4(&_projectionMatrix, DirectX::XMMatrixIdentity());
		updateVectors();
	}

	void camera::updateVectors() {
		const float cosPitch = std::cos(_pitch);
		const DirectX::XMFLOAT3 newForward{
			cosPitch * std::cos(_yaw),
			std::sin(_pitch),
			cosPitch * std::sin(_yaw)
		};
		const DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&newForward));
		const DirectX::XMVECTOR worldUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
		const DirectX::XMVECTOR right = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(worldUp, forward));
		DirectX::XMStoreFloat3(&_forward, forward);
		DirectX::XMStoreFloat3(&_right, right);
		DirectX::XMStoreFloat3(&_up, DirectX::XMVector3Cross(forward, right));
	}

	void camera::updateProjectionMatrix(float aspectRatio, float fovDegrees, float nearPlane, float farPlane) {
			DirectX::XMStoreFloat4x4(&_projectionMatrix, DirectX::XMMatrixPerspectiveFovLH(
			DirectX::XMConvertToRadians(fovDegrees), aspectRatio, nearPlane, farPlane));
	}

	void camera::updateViewMatrix() {
		const DirectX::XMVECTOR position = DirectX::XMLoadFloat3(&_position);
		const DirectX::XMVECTOR forward = DirectX::XMLoadFloat3(&_forward);
		const DirectX::XMVECTOR up = DirectX::XMLoadFloat3(&_up);
		DirectX::XMStoreFloat4x4(
			&_viewMatrix,
			DirectX::XMMatrixLookAtLH(position, DirectX::XMVectorAdd(position, forward), up));
	}

	void camera::setPosition(const DirectX::XMFLOAT3& position) { _position = position; }
	void camera::setPitch(float pitch) { _pitch = pitch; }
	void camera::setYaw(float yaw) { _yaw = yaw; }
	const DirectX::XMFLOAT3& camera::getPosition() const { return _position; }
	const DirectX::XMFLOAT3& camera::getForward() const { return _forward; }
	const DirectX::XMFLOAT3& camera::getRight() const { return _right; }
	const DirectX::XMFLOAT3& camera::getUp() const { return _up; }
	float camera::getPitch() const { return _pitch; }
	float camera::getYaw() const { return _yaw; }
	DirectX::XMMATRIX camera::getViewMatrix() const { return DirectX::XMLoadFloat4x4(&_viewMatrix); }
	DirectX::XMMATRIX camera::getProjectionMatrix() const { return DirectX::XMLoadFloat4x4(&_projectionMatrix); }
}
