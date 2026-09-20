#pragma once

#include <d3d11.h>
#include <DirectXMath.h>

#include <assetManager/assetManager.h>
#include <renderer/buffer.h>
#include <renderer/playerRenderer.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ac {

	struct dynamicEntityHandle {
		uint32_t index = (std::numeric_limits<uint32_t>::max)();
		uint32_t generation = 0;

		explicit operator bool() const {
			return index != (std::numeric_limits<uint32_t>::max)() && generation != 0;
		}
	};

	struct dynamicEntityPart {
		gpuModel* model = nullptr;
		gpuMaterial* material = nullptr;
	};

	enum class dynamicEntityType : uint8_t {
		mesh,
		humanoid
	};

	struct dynamicEntity {
		// model/material keep simple one-part entities inexpensive. Assets with
		// multiple parts are resolved into parts by dynamicRenderer::create.
		gpuModel* model = nullptr;
		gpuMaterial* material = nullptr;
		std::vector<dynamicEntityPart> parts;
		dynamicEntityType type = dynamicEntityType::mesh;

		DirectX::XMFLOAT3 position = {};
		DirectX::XMFLOAT4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
		DirectX::XMFLOAT3 scale = { 1.0f, 1.0f, 1.0f };
		DirectX::XMFLOAT3 velocity = {};
		DirectX::XMFLOAT4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };
		float yaw = 0.0f;
		float pitch = 0.0f;
		humanoidAnimationState humanoidAnimation;

		float boundingRadius = 0.0f;
		float maximumDrawDistance = 0.0f;
		uint32_t renderLayer = 0;
		uint32_t animation = 0;
		bool translucent = false;
		bool visible = true;
		bool grounded = true;
		bool crouching = false;
		bool sprinting = false;
		bool thirdPerson = true;
	};

	enum class dynamicRenderPass : uint8_t {
		opaque,
		translucent
	};

	struct dynamicRenderStats {
		uint32_t submitted = 0;
		uint32_t drawn = 0;
		uint32_t culled = 0;
		uint32_t modelBinds = 0;
		uint32_t materialBinds = 0;
	};

	class dynamicRenderer {
		struct objectData {
			DirectX::XMFLOAT4X4 transform;
		};

		struct entitySlot {
			dynamicEntity entity;
			uint32_t generation = 1;
			bool alive = false;
		};

		struct drawItem {
			dynamicEntity* entity = nullptr;
			gpuModel* model = nullptr;
			gpuMaterial* material = nullptr;
			float distanceSquared = 0.0f;
		};

		ID3D11DeviceContext* _context = nullptr;
		constantBuffer<objectData> _objectBuffer;
		std::vector<entitySlot> _entities;
		std::vector<uint32_t> _freeSlots;
		std::vector<drawItem> _drawItems;
		std::unique_ptr<playerRenderer> _humanoidAsset;

		static float distanceSquared(
			const DirectX::XMFLOAT3& left,
			const DirectX::XMFLOAT3& right
		) {
			const float x = left.x - right.x;
			const float y = left.y - right.y;
			const float z = left.z - right.z;
			return x * x + y * y + z * z;
		}

		static DirectX::XMFLOAT4X4 transformFor(const dynamicEntity& entity) {
			using namespace DirectX;
			XMVECTOR rotation = XMLoadFloat4(&entity.rotation);
			if (XMVectorGetX(XMVector4LengthSq(rotation)) < 1.0e-8f)
				rotation = XMQuaternionIdentity();
			else
				rotation = XMQuaternionNormalize(rotation);

			const XMMATRIX transform =
				XMMatrixScaling(entity.scale.x, entity.scale.y, entity.scale.z) *
				XMMatrixRotationQuaternion(rotation) *
				XMMatrixTranslation(entity.position.x, entity.position.y, entity.position.z);
			XMFLOAT4X4 output;
			XMStoreFloat4x4(&output, XMMatrixTranspose(transform));
			return output;
		}

		entitySlot* find(dynamicEntityHandle handle) {
			if (!handle || handle.index >= _entities.size()) return nullptr;
			entitySlot& slot = _entities[handle.index];
			return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
		}

		const entitySlot* find(dynamicEntityHandle handle) const {
			if (!handle || handle.index >= _entities.size()) return nullptr;
			const entitySlot& slot = _entities[handle.index];
			return slot.alive && slot.generation == handle.generation ? &slot : nullptr;
		}

	public:
		dynamicRenderer(ID3D11Device* device, ID3D11DeviceContext* context)
			: _context(context) {
			if (!device || !context)
				throw std::invalid_argument("dynamicRenderer requires a D3D11 device and context");
			_objectBuffer.create(device);
		}

		dynamicEntityHandle create(const dynamicEntity& entity = {}) {
			uint32_t index;
			if (_freeSlots.empty()) {
				index = static_cast<uint32_t>(_entities.size());
				_entities.emplace_back();
			}
			else {
				index = _freeSlots.back();
				_freeSlots.pop_back();
			}

			entitySlot& slot = _entities[index];
			slot.entity = entity;
			slot.alive = true;
			return { index, slot.generation };
		}

		dynamicEntityHandle create(
			const dynamicAsset& asset,
			modelManager& models,
			materialManager& materials,
			const DirectX::XMFLOAT3& position = {},
			const DirectX::XMFLOAT3& scale = { 1.0f, 1.0f, 1.0f }
		) {
			if (asset._model.empty())
				throw std::invalid_argument("Cannot create a dynamic entity from an asset without models");
			if (asset._material.size() > 1 && asset._material.size() != asset._model.size())
				throw std::invalid_argument(
					"Dynamic asset materials must be empty, contain one shared material, or match its model count");

			dynamicEntity entity{};
			entity.position = position;
			entity.scale = scale;
			entity.rotation = asset._rotationQuaternion;
			entity.animation = asset._animation;
			entity.parts.reserve(asset._model.size());

			for (size_t index = 0; index < asset._model.size(); ++index) {
				model* modelAsset = models.get(asset._model[index]);
				if (!modelAsset)
					throw std::runtime_error(
						"Dynamic asset references missing model " + std::to_string(asset._model[index]));

				gpuMaterial* gpuMaterialAsset = nullptr;
				if (!asset._material.empty()) {
					const uint32_t materialId = asset._material.size() == 1
						? asset._material.front()
						: asset._material[index];
					material* materialAsset = materials.get(materialId);
					if (!materialAsset)
						throw std::runtime_error(
							"Dynamic asset references missing material " + std::to_string(materialId));
					gpuMaterialAsset = &materialAsset->_gpu;
				}

				entity.parts.push_back({ &modelAsset->_gpu, gpuMaterialAsset });
			}
			return create(entity);
		}

		void loadHumanoidAsset(ID3D11Device* device, const std::string& skinPath) {
			if (!device)
				throw std::invalid_argument("Cannot load a humanoid asset without a D3D11 device");
			_humanoidAsset = std::make_unique<playerRenderer>(device, skinPath);
		}

		dynamicEntityHandle createHumanoid(
			const DirectX::XMFLOAT3& eyePosition = {},
			const DirectX::XMFLOAT4& tint = { 1.0f, 1.0f, 1.0f, 1.0f },
			bool thirdPerson = true
		) {
			if (!_humanoidAsset)
				throw std::runtime_error("Load a humanoid dynamic asset before creating humanoid entities");
			dynamicEntity entity{};
			entity.type = dynamicEntityType::humanoid;
			entity.position = eyePosition;
			entity.tint = tint;
			entity.thirdPerson = thirdPerson;
			entity.boundingRadius = 1.1f;
			entity.maximumDrawDistance = 128.0f;
			return create(entity);
		}

		bool setHumanoidPose(
			dynamicEntityHandle handle,
			const DirectX::XMFLOAT3& eyePosition,
			const DirectX::XMFLOAT3& velocity,
			float yaw,
			float pitch,
			bool grounded,
			bool crouching,
			bool sprinting,
			bool thirdPerson
		) {
			dynamicEntity* entity = get(handle);
			if (!entity || entity->type != dynamicEntityType::humanoid) return false;
			entity->position = eyePosition;
			entity->velocity = velocity;
			entity->yaw = yaw;
			entity->pitch = pitch;
			entity->grounded = grounded;
			entity->crouching = crouching;
			entity->sprinting = sprinting;
			entity->thirdPerson = thirdPerson;
			return true;
		}

		bool triggerUseAnimation(dynamicEntityHandle handle) {
			dynamicEntity* entity = get(handle);
			if (!entity || entity->type != dynamicEntityType::humanoid || !_humanoidAsset)
				return false;
			_humanoidAsset->triggerUseAnimation(entity->humanoidAnimation);
			return true;
		}

		bool destroy(dynamicEntityHandle handle) {
			entitySlot* slot = find(handle);
			if (!slot) return false;
			slot->entity = {};
			slot->alive = false;
			++slot->generation;
			if (slot->generation == 0) ++slot->generation;
			_freeSlots.push_back(handle.index);
			return true;
		}

		bool contains(dynamicEntityHandle handle) const { return find(handle) != nullptr; }

		dynamicEntity* get(dynamicEntityHandle handle) {
			entitySlot* slot = find(handle);
			return slot ? &slot->entity : nullptr;
		}

		const dynamicEntity* get(dynamicEntityHandle handle) const {
			const entitySlot* slot = find(handle);
			return slot ? &slot->entity : nullptr;
		}

		void clear() {
			_entities.clear();
			_freeSlots.clear();
			_drawItems.clear();
		}

		size_t size() const { return _entities.size() - _freeSlots.size(); }

		uint32_t renderMeshShadows(
			const DirectX::XMFLOAT3& lightPosition,
			float lightRadius
		) {
			uint32_t drawn = 0;
			for (entitySlot& slot : _entities) {
				dynamicEntity& entity = slot.entity;
				if (!slot.alive || !entity.visible || entity.translucent ||
					entity.type != dynamicEntityType::mesh) continue;
				const float reach = lightRadius + (std::max)(0.0f, entity.boundingRadius);
				if (distanceSquared(entity.position, lightPosition) > reach * reach) continue;

				_objectBuffer.update(_context, { transformFor(entity) });
				_objectBuffer.bindVS(_context, 0);
				if (entity.model) {
					entity.model->bind(_context);
					entity.model->draw(_context);
					++drawn;
				}
				for (const dynamicEntityPart& part : entity.parts) {
					if (!part.model) continue;
					part.model->bind(_context);
					part.model->draw(_context);
					++drawn;
				}
			}
			return drawn;
		}

		uint32_t renderHumanoidShadows(
			const DirectX::XMFLOAT3& lightPosition,
			float lightRadius
		) {
			if (!_humanoidAsset) return 0;
			uint32_t drawn = 0;
			for (entitySlot& slot : _entities) {
				dynamicEntity& entity = slot.entity;
				if (!slot.alive || !entity.visible || entity.type != dynamicEntityType::humanoid)
					continue;
				const float reach = lightRadius + (std::max)(0.0f, entity.boundingRadius);
				if (distanceSquared(entity.position, lightPosition) > reach * reach) continue;
				_humanoidAsset->renderShadow(
					_context,
					entity.position,
					entity.yaw,
					entity.pitch,
					entity.grounded,
					entity.crouching,
					entity.humanoidAnimation
				);
				++drawn;
			}
			return drawn;
		}

		dynamicRenderStats renderHumanoids(
			const DirectX::XMFLOAT3& cameraPosition,
			const DirectX::XMFLOAT4X4& view,
			float deltaTime
		) {
			dynamicRenderStats stats{};
			if (!_humanoidAsset) return stats;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
			_context->RSGetState(previousRasterizer.GetAddressOf());
			for (entitySlot& slot : _entities) {
				dynamicEntity& entity = slot.entity;
				if (!slot.alive || entity.type != dynamicEntityType::humanoid) continue;
				++stats.submitted;
				if (!entity.visible) {
					++stats.culled;
					continue;
				}
				const float distance = distanceSquared(entity.position, cameraPosition);
				const float limit = entity.maximumDrawDistance + (std::max)(0.0f, entity.boundingRadius);
				if (entity.maximumDrawDistance > 0.0f && distance > limit * limit) {
					++stats.culled;
					continue;
				}
				_humanoidAsset->render(
					_context,
					entity.position,
					entity.velocity,
					entity.yaw,
					entity.pitch,
					entity.grounded,
					entity.crouching,
					entity.sprinting,
					entity.thirdPerson,
					view,
					deltaTime,
					entity.humanoidAnimation
				);
				++stats.drawn;
			}
			_context->RSSetState(previousRasterizer.Get());
			return stats;
		}

		dynamicRenderStats render(
			dynamicRenderPass pass,
			const DirectX::XMFLOAT3& cameraPosition
		) {
			dynamicRenderStats stats{};
			_drawItems.clear();
			_drawItems.reserve(size());

			const bool translucentPass = pass == dynamicRenderPass::translucent;
			for (entitySlot& slot : _entities) {
				if (!slot.alive || slot.entity.type != dynamicEntityType::mesh ||
					slot.entity.translucent != translucentPass) continue;
				++stats.submitted;
				if (!slot.entity.visible || (!slot.entity.model && slot.entity.parts.empty())) {
					++stats.culled;
					continue;
				}

				const float distance = distanceSquared(slot.entity.position, cameraPosition);
				if (slot.entity.maximumDrawDistance > 0.0f) {
					const float radius = (std::max)(0.0f, slot.entity.boundingRadius);
					const float limit = slot.entity.maximumDrawDistance + radius;
					if (distance > limit * limit) {
						++stats.culled;
						continue;
					}
				}
				if (slot.entity.parts.empty()) {
					_drawItems.push_back({
						&slot.entity, slot.entity.model, slot.entity.material, distance
					});
				}
				else {
					for (const dynamicEntityPart& part : slot.entity.parts) {
						if (!part.model) continue;
						_drawItems.push_back({ &slot.entity, part.model, part.material, distance });
					}
				}
			}

			std::sort(_drawItems.begin(), _drawItems.end(), [&](const drawItem& left, const drawItem& right) {
				if (left.entity->renderLayer != right.entity->renderLayer)
					return left.entity->renderLayer < right.entity->renderLayer;
				if (translucentPass)
					return left.distanceSquared > right.distanceSquared;
				if (left.material != right.material)
					return std::less<gpuMaterial*>{}(left.material, right.material);
				if (left.model != right.model)
					return std::less<gpuModel*>{}(left.model, right.model);
				return left.distanceSquared < right.distanceSquared;
			});

			_objectBuffer.bindVS(_context, 0);
			gpuModel* boundModel = nullptr;
			gpuMaterial* boundMaterial = nullptr;
			for (const drawItem& item : _drawItems) {
				dynamicEntity& entity = *item.entity;
				if (item.material && item.material != boundMaterial) {
					item.material->bind(_context);
					boundMaterial = item.material;
					++stats.materialBinds;
				}
				if (item.model != boundModel) {
					item.model->bind(_context);
					boundModel = item.model;
					++stats.modelBinds;
				}
				_objectBuffer.update(_context, { transformFor(entity) });
				item.model->draw(_context);
				++stats.drawn;
			}
			return stats;
		}
	};

}
