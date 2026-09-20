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
        camera();

        // Recomputes Direction Vectors based on Pitch and Yaw values set by external controllers
        void updateVectors();

        // Recomputes hardware projection matrix based on aspect ratio passed from window
        void updateProjectionMatrix(float aspectRatio, float fovDegrees = 70.0f, float nearPlane = 0.1f, float farPlane = 1000.0f);

        // Recomputes hardware viewing perspective matrix based on current position and direction data
        void updateViewMatrix();

        // Direct Setters for controlling logic classes to manipulate
        void setPosition(const DirectX::XMFLOAT3& pos);
        void setPitch(float p);
        void setYaw(float y);

        // Direct Getters for controlling logic classes to inspect
        const DirectX::XMFLOAT3& getPosition() const;
        const DirectX::XMFLOAT3& getForward() const;
        const DirectX::XMFLOAT3& getRight() const;
        const DirectX::XMFLOAT3& getUp() const;
        float getPitch() const;
        float getYaw() const;

        // Matrix outputs for the constantBuffer/Shader system
        DirectX::XMMATRIX getViewMatrix() const;
        DirectX::XMMATRIX getProjectionMatrix() const;
    };
}
