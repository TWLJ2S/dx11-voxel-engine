#pragma once

#include <DirectXMath.h>
#include "debug.h"

namespace ac {

    class camera {
    private:
        DirectX::XMFLOAT3 _position = { 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 _forward = { 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 _up = { 0.0f, 1.0f, 0.0f };
        DirectX::XMFLOAT3 _right = { 1.0f, 0.0f, 0.0f };

        float _pitch = 0.0f;
        float _yaw = 1.570796f; // Initialized facing down the Z-axis

        DirectX::XMFLOAT4X4 _viewMatrix;
        DirectX::XMFLOAT4X4 _projectionMatrix;

    public:
        camera() {
            DirectX::XMStoreFloat4x4(&_viewMatrix, DirectX::XMMatrixIdentity());
            DirectX::XMStoreFloat4x4(&_projectionMatrix, DirectX::XMMatrixIdentity());
            updateVectors();
        }

        // Recomputes Direction Vectors based on Pitch and Yaw values set by external controllers
        void updateVectors() {
            float cosPitch = cosf(_pitch);
            DirectX::XMFLOAT3 newForward;
            newForward.x = cosPitch * cosf(_yaw);
            newForward.y = sinf(_pitch);
            newForward.z = cosPitch * sinf(_yaw);

            DirectX::XMVECTOR f = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&newForward));
            DirectX::XMStoreFloat3(&_forward, f);

            DirectX::XMVECTOR worldUp = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
            DirectX::XMVECTOR r = DirectX::XMVector3Normalize(DirectX::XMVector3Cross(worldUp, f));
            DirectX::XMVECTOR u = DirectX::XMVector3Cross(f, r);

            DirectX::XMStoreFloat3(&_right, r);
            DirectX::XMStoreFloat3(&_up, u);
        }

        // Recomputes hardware projection matrix based on aspect ratio passed from window
        void updateProjectionMatrix(float aspectRatio, float fovDegrees = 70.0f, float nearPlane = 0.1f, float farPlane = 1000.0f) {
            DirectX::XMStoreFloat4x4(&_projectionMatrix, DirectX::XMMatrixPerspectiveFovLH(
                DirectX::XMConvertToRadians(fovDegrees),
                aspectRatio,
                nearPlane,
                farPlane
            ));
        }

        // Recomputes hardware viewing perspective matrix based on current position and direction data
        void updateViewMatrix() {
            DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&_position);
            DirectX::XMVECTOR f = DirectX::XMLoadFloat3(&_forward);
            DirectX::XMVECTOR u = DirectX::XMLoadFloat3(&_up);

            DirectX::XMVECTOR target = DirectX::XMVectorAdd(pos, f);
            DirectX::XMStoreFloat4x4(&_viewMatrix, DirectX::XMMatrixLookAtLH(pos, target, u));
        }

        // Direct Setters for controlling logic classes to manipulate
        void setPosition(const DirectX::XMFLOAT3& pos) { _position = pos; }
        void setPitch(float p) { _pitch = p; }
        void setYaw(float y) { _yaw = y; }

        // Direct Getters for controlling logic classes to inspect
        const DirectX::XMFLOAT3& getPosition() const { return _position; }
        const DirectX::XMFLOAT3& getForward() const { return _forward; }
        const DirectX::XMFLOAT3& getRight() const { return _right; }
        const DirectX::XMFLOAT3& getUp() const { return _up; }
        float getPitch() const { return _pitch; }
        float getYaw() const { return _yaw; }

        // Matrix outputs for the constantBuffer/Shader system
        DirectX::XMMATRIX getViewMatrix() const { return DirectX::XMLoadFloat4x4(&_viewMatrix); }
        DirectX::XMMATRIX getProjectionMatrix() const { return DirectX::XMLoadFloat4x4(&_projectionMatrix); }
    };
}