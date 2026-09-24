#define _SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING
#include "VoxelApplication.h"

#include <core/window.h>
#include <core/graphics.h>
#include <core/applicationPipeline.h>
#include <core/player.h>
#include <core/debug.h>
#include <core/shader.h>
#include <core/texture.h>
#include <core/settingsStore.h>
#include <core/applicationConfig.h>
#include <assets/assetManager.h>
#include <assets/contentPack.h>
#include <server/simulationServer.h>
#include <modding/gameApiAdapters.h>
#include <modding/modHost.h>
#include <modding/wasmtimeBackend.h>
#include <world/worldStreamer.h>
#include <world/gpuChunkMesher.h>

#include <renderer/buffer.h>
#include <renderer/scenePostProcess.h>
#include <renderer/gpuFrameTimer.h>
#include <renderer/light.h>
#include <renderer/staticRenderer.h>
#include <renderer/dynamicRenderer.h>
#include <renderer/particleSystem.h>
#include <ui/uiInputState.h>
#include <ui/nuklearHost.h>
#include <renderer/blockBreakOverlay.h>
#include <renderer/crosshairRenderer.h>
#include <ui/uiScreens.h>
#include <ui/itemIconAtlas.h>
#include <audio/audioMixer.h>
#include <world/blockEntities.h>
#include <world/blockShape.h>
#include <world/fallingBlockRenderer.h>
#include <world/itemEntities.h>
#include <world/entitySystem.h>
#include <world/gameplay.h>
#include <world/worldSaves.h>
#include <world/weatherSystem.h>

#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <wrl/client.h>

#include <memory>
#include <vector>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <sstream>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace dx = DirectX;

namespace {
	bool diagnosticFrameCaptureEnabled() {
		char* captureFrame = nullptr;
		size_t captureFrameLength = 0;
		_dupenv_s(&captureFrame, &captureFrameLength, "AC_CAPTURE_FRAME");
		const bool enabled = captureFrame != nullptr;
		std::free(captureFrame);
		return enabled;
	}

	constexpr uint32_t REFLECTION_VOLUME_SIZE = 32u;
	constexpr uint32_t REFLECTION_VOLUME_VOXELS =
		REFLECTION_VOLUME_SIZE * REFLECTION_VOLUME_SIZE * REFLECTION_VOLUME_SIZE;
	// Covers four chunks in every direction. The previous 32x32 map produced a
	// visible wet/dark square only one chunk away from the camera.
	constexpr uint32_t WEATHER_SURFACE_SIZE = 128u;
	constexpr uint32_t WEATHER_SURFACE_CELLS = WEATHER_SURFACE_SIZE * WEATHER_SURFACE_SIZE;

	struct reflectionVolumeData {
		dx::XMINT3 origin{};
		uint32_t size = REFLECTION_VOLUME_SIZE;
	};

	struct planarReflectionData {
		dx::XMFLOAT4X4 viewProjection{};
		float planeHeight = 0.0f;
		float enabled = 0.0f;
		dx::XMFLOAT2 padding{};
	};

	struct planarClipData {
		float planeHeight = 0.0f;
		float enabled = 0.0f;
		dx::XMFLOAT2 padding{};
	};

	constexpr int HUD_HOTBAR_COUNT = 9;
	constexpr int HUD_INV_COLUMNS = 9;
	constexpr int HUD_INV_ROWS = 3;
	constexpr int HUD_INV_SLOTS = HUD_INV_COLUMNS * HUD_INV_ROWS; // 27 storage
	constexpr int HUD_CHOOSER_COLUMNS = 9;
	constexpr int HUD_CHOOSER_ROWS = 5; // matches creative tab_items.png
	constexpr int HUD_CHOOSER_SLOTS = HUD_CHOOSER_COLUMNS * HUD_CHOOSER_ROWS;
	constexpr int HUD_CHEST_COLUMNS = 9;
	constexpr int HUD_CHEST_ROWS = 3;
	constexpr int HUD_CHEST_SLOTS = HUD_CHEST_COLUMNS * HUD_CHEST_ROWS;
	const ac::applicationConfig& APPLICATION_CONFIG = ac::appConfig();
	const std::vector<int>& FPS_LIMITS = APPLICATION_CONFIG.fpsLimits;
	const std::vector<int>& VIEW_DISTANCES = APPLICATION_CONFIG.viewDistances;
	const std::vector<int>& FOV_DEGREES = APPLICATION_CONFIG.fovDegrees;
	const std::vector<int>& RENDER_SCALES = APPLICATION_CONFIG.renderScales;
	const std::vector<float>& DAY_CYCLE_SCALES = APPLICATION_CONFIG.dayCycleScales;
	const float DAY_LENGTH_SECONDS = APPLICATION_CONFIG.dayLengthSeconds;
	const float DOUBLE_CLICK_SECONDS = APPLICATION_CONFIG.inventoryDoubleClickSeconds;
	const float FOOTSTEP_SPEED_THRESHOLD = APPLICATION_CONFIG.footstepSpeedThreshold;
	const std::string& USER_SETTINGS_PATH = APPLICATION_CONFIG.userSettingsPath;
	ac::blockId BLOCK_CHEST = 0;
	ac::blockId BLOCK_FURNACE = 0;
	ac::blockId BLOCK_BLAST_FURNACE = 0;
	ac::blockId BLOCK_FURNACE_LIT = 0;
	ac::blockId BLOCK_BLAST_FURNACE_LIT = 0;
	ac::blockId BLOCK_OAK_DOOR = 0;
	ac::blockId BLOCK_OAK_SLAB = 0;
	ac::blockId BLOCK_OAK_STAIRS = 0;
	ac::blockId BLOCK_OAK_FENCE = 0;
	ac::blockId BLOCK_MAGMA = 0;
	const std::string& blockAlias(const char* key) {
		const auto found = APPLICATION_CONFIG.blockAliases.find(key);
		if (found == APPLICATION_CONFIG.blockAliases.end())
			throw std::runtime_error(std::string("assets/application.json: missing block alias '") + key + "'");
		return found->second;
	}
	void bindBlockAliases(const ac::staticAssetManager& blocks) {
		ac::WATER_BLOCK_TYPE = blocks.getId(blockAlias("water"));
		ac::BLOCK_CRAFTING_TABLE = blocks.getId(blockAlias("craftingTable"));
		ac::BLOCK_REDSTONE = blocks.getId(blockAlias("redstone"));
		ac::BLOCK_REDSTONE_BLOCK = blocks.getId(blockAlias("redstoneBlock"));
		ac::BLOCK_REDSTONE_WIRE = blocks.getId(blockAlias("redstoneWire"));
		ac::BLOCK_REDSTONE_LAMP = blocks.getId(blockAlias("redstoneLamp"));
		ac::BLOCK_REDSTONE_LAMP_ON = blocks.getId(blockAlias("redstoneLampOn"));
		ac::BLOCK_REDSTONE_TORCH = blocks.getId(blockAlias("redstoneTorch"));
		ac::BLOCK_REDSTONE_TORCH_OFF = blocks.getId(blockAlias("redstoneTorchOff"));
		ac::BLOCK_LEVER = blocks.getId(blockAlias("lever"));
		ac::BLOCK_LEVER_ON = blocks.getId(blockAlias("leverOn"));
		ac::BLOCK_REPEATER = blocks.getId(blockAlias("repeater"));
		ac::BLOCK_COMPARATOR = blocks.getId(blockAlias("comparator"));
		ac::BLOCK_OBSERVER = blocks.getId(blockAlias("observer"));
		BLOCK_CHEST = blocks.getId(blockAlias("chest"));
		BLOCK_FURNACE = blocks.getId(blockAlias("furnace"));
		BLOCK_BLAST_FURNACE = blocks.getId(blockAlias("blastFurnace"));
		BLOCK_FURNACE_LIT = blocks.getId(blockAlias("furnaceLit"));
		BLOCK_BLAST_FURNACE_LIT = blocks.getId(blockAlias("blastFurnaceLit"));
		BLOCK_OAK_DOOR = blocks.getId(blockAlias("oakDoor"));
		BLOCK_OAK_SLAB = blocks.getId(blockAlias("oakSlab"));
		BLOCK_OAK_STAIRS = blocks.getId(blockAlias("oakStairs"));
		BLOCK_OAK_FENCE = blocks.getId(blockAlias("oakFence"));
		BLOCK_MAGMA = blocks.getId(blockAlias("magma"));
	}
	constexpr int HUD_CRAFT2_SLOTS = 4;
	constexpr int HUD_CRAFT3_SLOTS = 9;
	constexpr int HUD_VITAL_PIPS = 10;

	inline bool isDoorDef(const ac::blockDefinition* definition) {
		return definition && (definition->_interaction == ac::blockInteraction::door ||
			definition->_behavior.placement == ac::blockPlacementBehavior::door);
	}
	inline ac::blockUseBehavior effectiveUse(const ac::blockDefinition* definition) {
		if (!definition) return ac::blockUseBehavior::none;
		if (definition->_behavior.use != ac::blockUseBehavior::automatic)
			return definition->_behavior.use;
		switch (definition->_interaction) {
		case ac::blockInteraction::door:
		case ac::blockInteraction::trapdoor:
		case ac::blockInteraction::fenceGate: return ac::blockUseBehavior::toggleOpen;
		case ac::blockInteraction::lever: return ac::blockUseBehavior::togglePowered;
		case ac::blockInteraction::chest: return ac::blockUseBehavior::container;
		case ac::blockInteraction::crafting: return ac::blockUseBehavior::crafting;
		default: return ac::blockUseBehavior::none;
		}
	}
	inline ac::blockPlacementBehavior effectivePlacement(const ac::blockDefinition* definition) {
		if (!definition) return ac::blockPlacementBehavior::standard;
		if (definition->_behavior.placement != ac::blockPlacementBehavior::automatic)
			return definition->_behavior.placement;
		if (definition->_interaction == ac::blockInteraction::door)
			return ac::blockPlacementBehavior::door;
		if (definition->_model == ac::MODEL_TRAPDOOR)
			return ac::blockPlacementBehavior::trapdoor;
		if (definition->_model == ac::MODEL_LADDER)
			return ac::blockPlacementBehavior::wallOnly;
		if (definition->_model == ac::MODEL_TORCH || definition->_model == ac::MODEL_LEVER)
			return ac::blockPlacementBehavior::wallOrFloor;
		if (ac::usesPlacementFacing(definition->_model))
			return ac::blockPlacementBehavior::horizontalFacing;
		return ac::blockPlacementBehavior::standard;
	}
	inline bool usesChestEntity(const ac::blockDefinition* definition) {
		if (!definition) return false;
		if (definition->_behavior.entity != ac::blockEntityBehavior::automatic)
			return definition->_behavior.entity == ac::blockEntityBehavior::chest;
		return definition->_interaction == ac::blockInteraction::chest;
	}
	inline bool isDoorTopName(const std::string& name) {
		return name.find("_door_top") != std::string::npos;
	}
	inline bool isDoorOpenName(const std::string& name) {
		return name.size() >= 5 && name.compare(name.size() - 5, 5, "_open") == 0;
	}
	inline std::string doorToggleName(const std::string& name) {
		if (isDoorOpenName(name))
			return name.substr(0, name.size() - 5);
		return name + "_open";
	}
	inline std::string doorBottomName(const std::string& name) {
		const size_t pos = name.find("_door_top");
		if (pos == std::string::npos)
			return name;
		std::string out = name.substr(0, pos) + "_door";
		if (isDoorOpenName(name))
			out += "_open";
		return out;
	}
	inline std::string doorTopName(const std::string& name) {
		if (isDoorTopName(name))
			return name;
		if (isDoorOpenName(name))
			return name.substr(0, name.size() - 5) + "_top_open";
		return name + "_top";
	}
	inline bool isDoorPlaceBase(const ac::blockDefinition* definition) {
		return isDoorDef(definition) &&
			definition->_model == ac::MODEL_DOOR &&
			!isDoorTopName(definition->_name) &&
			!isDoorOpenName(definition->_name);
	}
	struct voxelCoordinate {
		int32_t x = 0;
		int32_t y = 0;
		int32_t z = 0;

		bool operator==(const voxelCoordinate& other) const {
			return x == other.x && y == other.y && z == other.z;
		}
	};

	struct voxelCoordinateHash {
		size_t operator()(const voxelCoordinate& position) const {
			uint32_t hash = static_cast<uint32_t>(position.x) * 73856093u ^
				static_cast<uint32_t>(position.y) * 19349663u ^
				static_cast<uint32_t>(position.z) * 83492791u;
			hash ^= hash >> 16u;
			return hash;
		}
	};

	uint32_t packVoxelRadiance(const dx::XMFLOAT3& color) {
		auto channel = [](float value) {
			return static_cast<uint32_t>(std::lround(std::clamp(value * 127.5f, 0.0f, 255.0f)));
		};
		return channel(color.x) | (channel(color.y) << 8u) | (channel(color.z) << 16u);
	}

	uint32_t maxPackedRadiance(uint32_t left, uint32_t right) {
		const uint32_t r = std::max(left & 255u, right & 255u);
		const uint32_t g = std::max((left >> 8u) & 255u, (right >> 8u) & 255u);
		const uint32_t b = std::max((left >> 16u) & 255u, (right >> 16u) & 255u);
		return r | (g << 8u) | (b << 16u);
	}

	// Prefer a clearly brighter path, or a more saturated one when brightness is
	// similar, so untinted light going around stained glass does not fully wash
	// out the filtered color via per-channel max.
	uint32_t combinePackedRadiance(uint32_t left, uint32_t right) {
		if (left == 0u) return right;
		if (right == 0u) return left;
		auto channel = [](uint32_t packed, uint32_t shift) {
			return (packed >> shift) & 255u;
		};
		auto luminance = [&](uint32_t packed) {
			return channel(packed, 0u) * 2u + channel(packed, 8u) * 3u + channel(packed, 16u);
		};
		auto saturation = [&](uint32_t packed) {
			const int r = static_cast<int>(channel(packed, 0u));
			const int g = static_cast<int>(channel(packed, 8u));
			const int b = static_cast<int>(channel(packed, 16u));
			return static_cast<uint32_t>(std::max({ r, g, b }) - std::min({ r, g, b }));
		};
		const uint32_t lumL = luminance(left);
		const uint32_t lumR = luminance(right);
		const uint32_t satL = saturation(left);
		const uint32_t satR = saturation(right);
		if (lumL > (lumR * 6u) / 5u && lumL > lumR + 10u) return left;
		if (lumR > (lumL * 6u) / 5u && lumR > lumL + 10u) return right;
		if (satL >= satR + 14u && lumL * 5u >= lumR * 4u) return left;
		if (satR >= satL + 14u && lumR * 5u >= lumL * 4u) return right;
		return maxPackedRadiance(left, right);
	}

	struct staticPipeline {
		ac::shaderProgram program;
		ac::shaderProgram faceProgram;
		ac::shaderProgram faceFastProgram;
		ac::shaderProgram programFast;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		Microsoft::WRL::ComPtr<ID3D11BlendState> opaqueBlend;
		Microsoft::WRL::ComPtr<ID3D11BlendState> translucentBlend;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> opaqueDepth;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> translucentDepth;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> translucentCullBack;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> translucentCullNone;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> cutoutCullNone;

		staticPipeline(ID3D11Device* device) {
			program.initVertexShader(
				device,
				L"assets/shader/staticVertex.hlsl",
				"main",
				"vs_5_0"
			);
			program.initPixelShader(
				device,
				L"assets/shader/staticPixel.hlsl",
				"main",
				"ps_5_0"
			);
			{
				const D3D_SHADER_MACRO fastMacros[] = {
					{ "TERRAIN_FAST", "1" },
					{ nullptr, nullptr }
				};
				programFast.initVertexShader(
					device, L"assets/shader/staticVertex.hlsl", "main", "vs_5_0");
				programFast.initPixelShader(
					device, L"assets/shader/staticPixel.hlsl", "main", "ps_5_0", fastMacros);
			}
			faceProgram.initVertexShader(device, L"assets/shader/chunkFaceVertex.hlsl", "main", "vs_5_0");
			faceProgram.initPixelShader(device, L"assets/shader/staticPixel.hlsl", "main", "ps_5_0");
			faceFastProgram.initVertexShader(device, L"assets/shader/chunkFaceVertex.hlsl", "main", "vs_5_0");
			{
				const D3D_SHADER_MACRO fastMacros[] = {
					{ "TERRAIN_FAST", "1" },
					{ nullptr, nullptr }
				};
				faceFastProgram.initPixelShader(
					device, L"assets/shader/staticPixel.hlsl", "main", "ps_5_0", fastMacros);
			}

			program.initInputLayout(device, {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"NORMAL",	 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _normal),	 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,	 0, offsetof(ac::vertex, _uv),		 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"MATID",	 0, DXGI_FORMAT_R32_UINT,		 0, offsetof(ac::vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"AO",		 0, DXGI_FORMAT_R32_UINT,		 0, offsetof(ac::vertex, _ao),		 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"OPACITY",  0, DXGI_FORMAT_R32_FLOAT,		 0, offsetof(ac::vertex, _opacity),  D3D11_INPUT_PER_VERTEX_DATA, 0}
			});
			programFast.initInputLayout(device, {
				{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"NORMAL",	 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _normal),	 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,	 0, offsetof(ac::vertex, _uv),		 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"MATID",	 0, DXGI_FORMAT_R32_UINT,		 0, offsetof(ac::vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"AO",		 0, DXGI_FORMAT_R32_UINT,		 0, offsetof(ac::vertex, _ao),		 D3D11_INPUT_PER_VERTEX_DATA, 0},
				{"OPACITY",  0, DXGI_FORMAT_R32_FLOAT,		 0, offsetof(ac::vertex, _opacity),  D3D11_INPUT_PER_VERTEX_DATA, 0}
			});

			D3D11_SAMPLER_DESC d{};
			d.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			d.AddressU = d.AddressV = d.AddressW =
				D3D11_TEXTURE_ADDRESS_WRAP;
			d.MaxLOD = 0.0f;

			DX_CHECK(
				device->CreateSamplerState(
					&d,
					sampler.GetAddressOf()
				)
			);

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, opaqueBlend.GetAddressOf()));

			blend.RenderTarget[0].BlendEnable = TRUE;
			blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
			blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			DX_CHECK(device->CreateBlendState(&blend, translucentBlend.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = TRUE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX_CHECK(device->CreateDepthStencilState(&depth, opaqueDepth.GetAddressOf()));

			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			DX_CHECK(device->CreateDepthStencilState(&depth, translucentDepth.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_BACK;
			rasterizer.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(
				&rasterizer, translucentCullBack.GetAddressOf()));
			// Underwater, the free-surface underside must draw so exit refraction,
			// TIR, and diffraction are visible when looking out.
			rasterizer.CullMode = D3D11_CULL_NONE;
			DX_CHECK(device->CreateRasterizerState(
				&rasterizer, translucentCullNone.GetAddressOf()));
			// Thin cutout models (doors) need both sides so they stay visible at
			// grazing angles and read as solid slabs rather than one-sided cards.
			DX_CHECK(device->CreateRasterizerState(
				&rasterizer, cutoutCullNone.GetAddressOf()));
		}

		void bind(ID3D11DeviceContext* ctx) {
			program.bindShaders(ctx);
			bindOpaqueState(ctx);
		}

		void bindFaces(ID3D11DeviceContext* ctx) {
			faceProgram.bindShaders(ctx);
			bindOpaqueState(ctx);
		}

		void bindFacesFast(ID3D11DeviceContext* ctx) {
			faceFastProgram.bindShaders(ctx);
			bindOpaqueState(ctx);
		}

		void bindCutoutDetail(ID3D11DeviceContext* ctx) {
			program.bindShaders(ctx);
			ctx->PSSetSamplers(0, 1, sampler.GetAddressOf());
			ctx->OMSetBlendState(opaqueBlend.Get(), nullptr, 0xffffffff);
			ctx->OMSetDepthStencilState(opaqueDepth.Get(), 0);
			ctx->RSSetState(cutoutCullNone.Get());
		}

		void bindOpaqueState(ID3D11DeviceContext* ctx) {
			ctx->PSSetSamplers(
				0,
				1,
				sampler.GetAddressOf()
			);
			ctx->OMSetBlendState(opaqueBlend.Get(), nullptr, 0xffffffff);
			ctx->OMSetDepthStencilState(opaqueDepth.Get(), 0);
			ctx->RSSetState(translucentCullBack.Get());
		}

		void bindTranslucent(ID3D11DeviceContext* ctx, bool underwater = false) {
			ctx->OMSetBlendState(translucentBlend.Get(), nullptr, 0xffffffff);
			ctx->OMSetDepthStencilState(translucentDepth.Get(), 0);
			ctx->RSSetState(underwater
				? translucentCullNone.Get()
				: translucentCullBack.Get());
		}

		void bindTranslucentFaces(ID3D11DeviceContext* ctx, bool underwater = false, bool fast = false) {
			(fast ? faceFastProgram : faceProgram).bindShaders(ctx);
			ctx->PSSetSamplers(0, 1, sampler.GetAddressOf());
			bindTranslucent(ctx, underwater);
		}

		void bindTranslucentModels(ID3D11DeviceContext* ctx, bool underwater = false) {
			program.bindShaders(ctx);
			ctx->PSSetSamplers(0, 1, sampler.GetAddressOf());
			bindTranslucent(ctx, underwater);
		}
	};

	struct skyRenderer {
		ac::shaderProgram program;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;

		void create(ID3D11Device* device) {
			program.initVertexShader(device, L"assets/shader/sky.hlsl", "vertexMain", "vs_5_0");
			program.initPixelShader(device, L"assets/shader/sky.hlsl", "pixelMain", "ps_5_0");

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = FALSE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D11_COMPARISON_ALWAYS;
			DX_CHECK(device->CreateDepthStencilState(&depth, depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&rasterizer, rasterizerState.GetAddressOf()));
		}

		void render(ID3D11DeviceContext* context) const {
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizer;
			context->RSGetState(oldRasterizer.GetAddressOf());
			program.bindShaders(context);
			context->OMSetDepthStencilState(depthState.Get(), 0);
			context->RSSetState(rasterizerState.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->Draw(3, 0);
			context->RSSetState(oldRasterizer.Get());
		}
	};

	struct sceneRefractionBuffer {
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderView;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depthTexture;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> depthShaderView;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		UINT width = 0;
		UINT height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

		void create(ID3D11Device* device) {
			D3D11_SAMPLER_DESC description{};
			// Trilinear filtering is also used by the planar-reflection mip chain.
			// Keeping the sampler shared avoids another state change in the
			// translucent pass while still smoothing transitions between levels.
			description.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			description.AddressU = description.AddressV = description.AddressW =
				D3D11_TEXTURE_ADDRESS_CLAMP;
			description.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&description, sampler.GetAddressOf()));
		}

		void capture(
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			ID3D11RenderTargetView* target,
			ID3D11DepthStencilView* depthTarget
		) {
			ID3D11ShaderResourceView* nullResources[2] = { nullptr, nullptr };
			context->PSSetShaderResources(ac::SRV_OPAQUE_SCENE_COLOR, 2, nullResources);
			if (!target) return;

			Microsoft::WRL::ComPtr<ID3D11Resource> resource;
			target->GetResource(resource.GetAddressOf());
			Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
			if (FAILED(resource.As(&source))) return;
			D3D11_TEXTURE2D_DESC sourceDescription{};
			source->GetDesc(&sourceDescription);
			if (sourceDescription.SampleDesc.Count != 1) return;

			if (!texture || width != sourceDescription.Width || height != sourceDescription.Height ||
				format != sourceDescription.Format) {
				texture.Reset();
				shaderView.Reset();
				depthTexture.Reset();
				depthShaderView.Reset();
				D3D11_TEXTURE2D_DESC copyDescription = sourceDescription;
				copyDescription.Usage = D3D11_USAGE_DEFAULT;
				copyDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				copyDescription.CPUAccessFlags = 0;
				copyDescription.MiscFlags = 0;
				copyDescription.MipLevels = 1;
				copyDescription.ArraySize = 1;
				DX_CHECK(device->CreateTexture2D(&copyDescription, nullptr, texture.GetAddressOf()));
				DX_CHECK(device->CreateShaderResourceView(texture.Get(), nullptr, shaderView.GetAddressOf()));
				width = sourceDescription.Width;
				height = sourceDescription.Height;
				format = sourceDescription.Format;
			}
			context->CopyResource(texture.Get(), source.Get());

			if (depthTarget) {
				Microsoft::WRL::ComPtr<ID3D11Resource> depthResource;
				depthTarget->GetResource(depthResource.GetAddressOf());
				Microsoft::WRL::ComPtr<ID3D11Texture2D> sourceDepth;
				if (SUCCEEDED(depthResource.As(&sourceDepth))) {
					D3D11_TEXTURE2D_DESC depthDescription{};
					sourceDepth->GetDesc(&depthDescription);
					if (!depthTexture) {
						D3D11_TEXTURE2D_DESC copyDepth = depthDescription;
						copyDepth.Format = DXGI_FORMAT_R24G8_TYPELESS;
						copyDepth.BindFlags = D3D11_BIND_SHADER_RESOURCE;
						copyDepth.CPUAccessFlags = 0;
						copyDepth.MiscFlags = 0;
						copyDepth.MipLevels = 1;
						copyDepth.ArraySize = 1;
						DX_CHECK(device->CreateTexture2D(&copyDepth, nullptr, depthTexture.GetAddressOf()));
						D3D11_SHADER_RESOURCE_VIEW_DESC depthResourceDescription{};
						depthResourceDescription.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
						depthResourceDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
						depthResourceDescription.Texture2D.MipLevels = 1;
						DX_CHECK(device->CreateShaderResourceView(
							depthTexture.Get(), &depthResourceDescription, depthShaderView.GetAddressOf()));
					}
					if (depthTexture)
						context->CopyResource(depthTexture.Get(), sourceDepth.Get());
				}
			}
		}

		void bind(ID3D11DeviceContext* context) const {
			context->PSSetShaderResources(ac::SRV_OPAQUE_SCENE_COLOR, 1, shaderView.GetAddressOf());
			context->PSSetShaderResources(ac::SRV_OPAQUE_SCENE_DEPTH, 1, depthShaderView.GetAddressOf());
			context->PSSetSamplers(3, 1, sampler.GetAddressOf());
		}
	};

	struct motionBlurPass {
		struct constants {
			dx::XMFLOAT4X4 previousViewProjection{};
			dx::XMFLOAT4X4 inverseViewProjection{};
			dx::XMFLOAT2 texelSize{ 1.0f, 1.0f };
			float blurStrength = 0.0f;
			float enabled = 0.0f;
		};

		ac::shaderProgram program;
		ac::constantBuffer<constants> buffer;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;
		dx::XMFLOAT4X4 previousViewProjection{};
		bool hasHistory = false;

		void create(ID3D11Device* device) {
			program.initVertexShader(device, L"assets/shader/motionBlur.hlsl", "vertexMain", "vs_5_0");
			program.initPixelShader(device, L"assets/shader/motionBlur.hlsl", "pixelMain", "ps_5_0");
			buffer.create(device);
			D3D11_SAMPLER_DESC samplerDescription{};
			samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDescription.AddressU = samplerDescription.AddressV = samplerDescription.AddressW =
				D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&samplerDescription, sampler.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = FALSE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			DX_CHECK(device->CreateDepthStencilState(&depth, depthState.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, blendState.GetAddressOf()));
		}

		void render(
			ID3D11DeviceContext* context,
			sceneRefractionBuffer& scene,
			const dx::XMFLOAT4X4& view,
			const dx::XMFLOAT4X4& projection,
			float strength
		) {
			if (!scene.shaderView || !scene.depthShaderView) return;

			const dx::XMMATRIX viewMatrix = dx::XMLoadFloat4x4(&view);
			const dx::XMMATRIX projectionMatrix = dx::XMLoadFloat4x4(&projection);
			const dx::XMMATRIX viewProjection = dx::XMMatrixMultiply(viewMatrix, projectionMatrix);
			dx::XMFLOAT4X4 currentViewProjection{};
			dx::XMStoreFloat4x4(&currentViewProjection, viewProjection);

			constants data{};
			const dx::XMMATRIX previous =
				hasHistory ? dx::XMLoadFloat4x4(&previousViewProjection) : viewProjection;
			dx::XMStoreFloat4x4(
				&data.previousViewProjection,
				dx::XMMatrixTranspose(previous));
			dx::XMStoreFloat4x4(
				&data.inverseViewProjection,
				dx::XMMatrixTranspose(dx::XMMatrixInverse(nullptr, viewProjection)));
			data.texelSize = {
				scene.width > 0 ? 1.0f / static_cast<float>(scene.width) : 1.0f,
				scene.height > 0 ? 1.0f / static_cast<float>(scene.height) : 1.0f
			};
			data.blurStrength = std::clamp(strength, 0.0f, 1.25f);
			data.enabled = (hasHistory && data.blurStrength > 0.02f) ? 1.0f : 0.0f;
			buffer.update(context, data);

			program.bindShaders(context);
			buffer.bindPS(context, 0);
			ID3D11ShaderResourceView* views[2] = {
				scene.shaderView.Get(),
				scene.depthShaderView.Get()
			};
			context->PSSetShaderResources(0, 2, views);
			context->PSSetSamplers(0, 1, sampler.GetAddressOf());
			context->OMSetDepthStencilState(depthState.Get(), 0);
			context->OMSetBlendState(blendState.Get(), nullptr, 0xffffffff);
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->Draw(3, 0);

			ID3D11ShaderResourceView* nullViews[2] = { nullptr, nullptr };
			context->PSSetShaderResources(0, 2, nullViews);
			previousViewProjection = currentViewProjection;
			hasHistory = true;
		}
	};

	struct fxaaPass {
		struct constants {
			dx::XMFLOAT2 texelSize{ 1.0f, 1.0f };
			float enabled = 1.0f;
			float padding = 0.0f;
		};

		ac::shaderProgram program;
		ac::constantBuffer<constants> buffer;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
		Microsoft::WRL::ComPtr<ID3D11BlendState> blendState;

		void create(ID3D11Device* device) {
			program.initVertexShader(device, L"assets/shader/fxaa.hlsl", "vertexMain", "vs_5_0");
			program.initPixelShader(device, L"assets/shader/fxaa.hlsl", "pixelMain", "ps_5_0");
			buffer.create(device);
			D3D11_SAMPLER_DESC samplerDescription{};
			samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			samplerDescription.AddressU = samplerDescription.AddressV = samplerDescription.AddressW =
				D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&samplerDescription, sampler.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = FALSE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			DX_CHECK(device->CreateDepthStencilState(&depth, depthState.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, blendState.GetAddressOf()));
		}

		void render(ID3D11DeviceContext* context, sceneRefractionBuffer& scene) {
			if (!scene.shaderView) return;

			constants data{};
			data.texelSize = {
				scene.width > 0 ? 1.0f / static_cast<float>(scene.width) : 1.0f,
				scene.height > 0 ? 1.0f / static_cast<float>(scene.height) : 1.0f
			};
			data.enabled = 1.0f;
			buffer.update(context, data);

			program.bindShaders(context);
			buffer.bindPS(context, 0);
			ID3D11ShaderResourceView* view = scene.shaderView.Get();
			context->PSSetShaderResources(0, 1, &view);
			context->PSSetSamplers(0, 1, sampler.GetAddressOf());
			context->OMSetDepthStencilState(depthState.Get(), 0);
			context->OMSetBlendState(blendState.Get(), nullptr, 0xffffffff);
			context->IASetInputLayout(nullptr);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			context->Draw(3, 0);

			ID3D11ShaderResourceView* nullView = nullptr;
			context->PSSetShaderResources(0, 1, &nullView);
		}
	};

	struct planarReflectionTarget {
		Microsoft::WRL::ComPtr<ID3D11Texture2D> color;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderView;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> depth;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> reflectedRasterizer;
		UINT width = 0;
		UINT height = 0;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
		D3D11_VIEWPORT previousViewport{};

		void ensure(ID3D11Device* device, UINT requestedWidth, UINT requestedHeight) {
			requestedWidth = (std::max)(requestedWidth, 1u);
			requestedHeight = (std::max)(requestedHeight, 1u);
			if (color && width == requestedWidth && height == requestedHeight) return;
			color.Reset(); target.Reset(); shaderView.Reset();
			depth.Reset(); depthView.Reset();
			width = requestedWidth;
			height = requestedHeight;

			D3D11_TEXTURE2D_DESC colorDescription{};
			colorDescription.Width = width;
			colorDescription.Height = height;
			// Allocate the complete mip chain. It is regenerated after every planar
			// pass and supplies the soft, enlarged reflection footprint without
			// throwing away the sharp correctly positioned centre image.
			colorDescription.MipLevels = 0;
			colorDescription.ArraySize = 1;
			colorDescription.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			colorDescription.SampleDesc.Count = 1;
			colorDescription.Usage = D3D11_USAGE_DEFAULT;
			colorDescription.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			colorDescription.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
			DX_CHECK(device->CreateTexture2D(&colorDescription, nullptr, color.GetAddressOf()));
			DX_CHECK(device->CreateRenderTargetView(color.Get(), nullptr, target.GetAddressOf()));
			DX_CHECK(device->CreateShaderResourceView(color.Get(), nullptr, shaderView.GetAddressOf()));

			D3D11_TEXTURE2D_DESC depthDescription = colorDescription;
			depthDescription.MipLevels = 1;
			depthDescription.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			depthDescription.MiscFlags = 0;
			DX_CHECK(device->CreateTexture2D(&depthDescription, nullptr, depth.GetAddressOf()));
			DX_CHECK(device->CreateDepthStencilView(depth.Get(), nullptr, depthView.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizerDescription{};
			rasterizerDescription.FillMode = D3D11_FILL_SOLID;
			// Reflection changes handedness, so the original back faces become the
			// reflected front faces.
			rasterizerDescription.CullMode = D3D11_CULL_FRONT;
			rasterizerDescription.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(
				&rasterizerDescription, reflectedRasterizer.ReleaseAndGetAddressOf()));
		}

		void begin(ID3D11Device* device, ID3D11DeviceContext* context, UINT targetWidth, UINT targetHeight) {
			ID3D11ShaderResourceView* nullReflection = nullptr;
			context->PSSetShaderResources(ac::SRV_PLANAR_REFLECTION, 1, &nullReflection);
			ensure(device, targetWidth, targetHeight);
			previousTarget.Reset();
			previousDepth.Reset();
			previousRasterizer.Reset();
			context->OMGetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.GetAddressOf());
			context->RSGetState(previousRasterizer.GetAddressOf());
			UINT viewportCount = 1;
			context->RSGetViewports(&viewportCount, &previousViewport);
			ID3D11RenderTargetView* reflectionTarget = target.Get();
			context->OMSetRenderTargets(1, &reflectionTarget, depthView.Get());
			D3D11_VIEWPORT viewport{};
			viewport.Width = static_cast<float>(width);
			viewport.Height = static_cast<float>(height);
			viewport.MaxDepth = 1.0f;
			context->RSSetViewports(1, &viewport);
			context->RSSetState(reflectedRasterizer.Get());
			const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			context->ClearRenderTargetView(target.Get(), clear);
			context->ClearDepthStencilView(depthView.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
		}

		void end(ID3D11DeviceContext* context) {
			ID3D11RenderTargetView* restoredTarget = previousTarget.Get();
			context->OMSetRenderTargets(1, &restoredTarget, previousDepth.Get());
			context->RSSetViewports(1, &previousViewport);
			context->RSSetState(previousRasterizer.Get());
			// The reflection RTV is no longer bound, so Direct3D can safely filter
			// mip 0 down through the complete chain before the water samples it.
			context->GenerateMips(shaderView.Get());
			previousTarget.Reset();
			previousDepth.Reset();
			previousRasterizer.Reset();
		}

		void bind(ID3D11DeviceContext* context) const {
			context->PSSetShaderResources(ac::SRV_PLANAR_REFLECTION, 1, shaderView.GetAddressOf());
		}
	};

	struct pointShadowMap {
		static constexpr UINT resolution = 512;
		static constexpr UINT maximumLights = 8;
		static constexpr uint32_t entityOnlyLight = UINT32_MAX;

		struct shadowData {
			dx::XMFLOAT4X4 viewProjection;
			dx::XMFLOAT3 lightPosition;
			float lightRadius;
			uint32_t entityOnly;
			dx::XMUINT3 padding{};
		};

		struct receiverData {
			std::array<dx::XMFLOAT4, maximumLights> lights{};
			std::array<dx::XMFLOAT4, maximumLights> radiance{};
			std::array<dx::XMUINT4, maximumLights> metadata{};
			uint32_t count = 0;
			dx::XMUINT3 padding{};
		};

		ac::shaderProgram program;
		ac::shaderProgram faceProgram;
		ac::constantBuffer<shadowData> buffer;
		ac::constantBuffer<receiverData> receiverBuffer;
		ac::constantBuffer<ac::objectData> faceObjectBuffer;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		std::array<Microsoft::WRL::ComPtr<ID3D11DepthStencilView>, maximumLights * 6> depthViews;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderView;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
		receiverData receivers{};

		pointShadowMap(ID3D11Device* device) {
			program.initVertexShader(device, L"assets/shader/pointShadowVertex.hlsl", "main", "vs_5_0");
			program.initPixelShader(device, L"assets/shader/pointShadowPixel.hlsl", "main", "ps_5_0");
			faceProgram.initVertexShader(device, L"assets/shader/chunkFaceShadowVertex.hlsl", "main", "vs_5_0");
			faceProgram.initPixelShader(device, L"assets/shader/pointShadowPixel.hlsl", "main", "ps_5_0");
			program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ac::vertex, _uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "MATID", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(ac::vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "AO", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(ac::vertex, _ao), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "OPACITY", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(ac::vertex, _opacity), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			buffer.create(device);
			receiverBuffer.create(device);
			faceObjectBuffer.create(device);

			D3D11_TEXTURE2D_DESC textureDesc{};
			textureDesc.Width = resolution;
			textureDesc.Height = resolution;
			textureDesc.MipLevels = 1;
			textureDesc.ArraySize = maximumLights * 6;
			textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			textureDesc.SampleDesc.Count = 1;
			textureDesc.Usage = D3D11_USAGE_DEFAULT;
			textureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			textureDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
			DX_CHECK(device->CreateTexture2D(&textureDesc, nullptr, texture.GetAddressOf()));

			for (UINT slice = 0; slice < maximumLights * 6; ++slice) {
				D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
				viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
				viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				viewDesc.Texture2DArray.MipSlice = 0;
				viewDesc.Texture2DArray.FirstArraySlice = slice;
				viewDesc.Texture2DArray.ArraySize = 1;
				DX_CHECK(device->CreateDepthStencilView(texture.Get(), &viewDesc, depthViews[slice].GetAddressOf()));
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC resourceDesc{};
			resourceDesc.Format = DXGI_FORMAT_R32_FLOAT;
			resourceDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
			resourceDesc.TextureCubeArray.MostDetailedMip = 0;
			resourceDesc.TextureCubeArray.MipLevels = 1;
			resourceDesc.TextureCubeArray.First2DArrayFace = 0;
			resourceDesc.TextureCubeArray.NumCubes = maximumLights;
			DX_CHECK(device->CreateShaderResourceView(texture.Get(), &resourceDesc, shaderView.GetAddressOf()));

			D3D11_SAMPLER_DESC samplerDesc{};
			samplerDesc.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			samplerDesc.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
			samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&samplerDesc, sampler.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depthDesc{};
			depthDesc.DepthEnable = TRUE;
			depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
			DX_CHECK(device->CreateDepthStencilState(&depthDesc, depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizerDesc{};
			rasterizerDesc.FillMode = D3D11_FILL_SOLID;
			rasterizerDesc.CullMode = D3D11_CULL_BACK;
			rasterizerDesc.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&rasterizerDesc, rasterizerState.GetAddressOf()));
		}

		bool render(
			ID3D11DeviceContext* context,
			ac::staticRenderer& renderer,
			ac::dynamicRenderer& dynamicRenderer,
			std::unordered_map<ac::worldChunkKey, ac::worldChunkGPU, ac::worldChunkKeyHash>& chunks,
			const ac::gpuLight& light,
			uint32_t sourceLightIndex,
			bool includeWorldCasters = true
		) {
			if (receivers.count >= maximumLights || light.radius <= 0.0f)
				return false;
			const UINT shadowIndex = receivers.count++;
			receivers.lights[shadowIndex] = {
				light.position.x, light.position.y, light.position.z, light.radius
			};
			receivers.radiance[shadowIndex] = {
				light.color.x, light.color.y, light.color.z, light.intensity
			};
			receivers.metadata[shadowIndex] = {
				sourceLightIndex, includeWorldCasters ? 0u : 1u, 0u, 0u
			};
			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth;
			context->OMGetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.GetAddressOf());
			UINT viewportCount = 1;
			D3D11_VIEWPORT previousViewport{};
			context->RSGetViewports(&viewportCount, &previousViewport);
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
			context->RSGetState(previousRasterizer.GetAddressOf());

			ID3D11ShaderResourceView* nullResource = nullptr;
			context->PSSetShaderResources(ac::SRV_POINT_SHADOWS, 1, &nullResource);
			D3D11_VIEWPORT shadowViewport{ 0.0f, 0.0f, static_cast<float>(resolution), static_cast<float>(resolution), 0.0f, 1.0f };
			context->RSSetViewports(1, &shadowViewport);
			context->RSSetState(rasterizerState.Get());
			context->OMSetDepthStencilState(depthState.Get(), 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			program.bindShaders(context);
			// Write radial distance rather than face-projected depth. Radial depth
			// stays continuous when filtered samples cross cubemap face boundaries.

			const dx::XMVECTOR eye = dx::XMLoadFloat3(&light.position);
			const dx::XMFLOAT3 directions[6] = {
				{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
				{ 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
			};
			const dx::XMFLOAT3 upVectors[6] = {
				{ 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 },
				{ 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 }
			};
			const dx::XMMATRIX projection = dx::XMMatrixPerspectiveFovLH(
				dx::XM_PIDIV2,
				1.0f,
				0.01f,
				light.radius
			);

			for (UINT face = 0; face < 6; ++face) {
				const UINT depthSlice = shadowIndex * 6 + face;
				context->OMSetRenderTargets(0, nullptr, depthViews[depthSlice].Get());
				context->ClearDepthStencilView(depthViews[depthSlice].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

				const dx::XMMATRIX view = dx::XMMatrixLookToLH(
					eye,
					dx::XMLoadFloat3(&directions[face]),
					dx::XMLoadFloat3(&upVectors[face])
				);
				dx::BoundingFrustum localShadowFrustum;
				dx::BoundingFrustum worldShadowFrustum;
				dx::BoundingFrustum::CreateFromMatrix(localShadowFrustum, projection);
				localShadowFrustum.Transform(
					worldShadowFrustum,
					dx::XMMatrixInverse(nullptr, view)
				);
				shadowData data{};
				dx::XMStoreFloat4x4(&data.viewProjection, dx::XMMatrixTranspose(view * projection));
				data.lightPosition = light.position;
				data.lightRadius = light.radius;
				data.entityOnly = includeWorldCasters ? 0u : 1u;
				buffer.update(context, data);
				buffer.bindVS(context, 4);
				buffer.bindPS(context, 4);

				if (includeWorldCasters) for (auto& pair : chunks) {
					ac::worldChunkGPU& worldChunk = pair.second;
					if (!worldChunk._chunk)
						continue;
					bool hasOpaqueSection = false;
					for (const ac::gpuModel& section : worldChunk._opaqueSections)
						hasOpaqueSection |= section._index.count() != 0;
					if (worldChunk._gpuFaces.count(false) == 0 && !hasOpaqueSection)
						continue;

					const auto& position = worldChunk._chunk->_position;
					const float minX = static_cast<float>(position.x * CHUNK_WIDTH);
					const float minZ = static_cast<float>(position.z * CHUNK_LENGTH);
					const float closestX = std::clamp(light.position.x, minX, minX + CHUNK_WIDTH);
					const float closestY = std::clamp(
						light.position.y,
						static_cast<float>(worldChunk._solidMinY),
						static_cast<float>(worldChunk._solidMaxY));
					const float closestZ = std::clamp(light.position.z, minZ, minZ + CHUNK_LENGTH);
					const float dx = closestX - light.position.x;
					const float dy = closestY - light.position.y;
					const float dz = closestZ - light.position.z;
					if (dx * dx + dy * dy + dz * dz > light.radius * light.radius)
						continue;
					if (worldShadowFrustum.Contains(
							ac::worldStreamer::chunkSolidBounds(worldChunk)) == dx::DISJOINT)
						continue;

					dx::XMFLOAT4X4 transform = worldChunk._worldTransform;
					if (worldChunk._hasGpuMesh) {
						renderer.render();
						faceProgram.bindShaders(context);
						faceObjectBuffer.update(context, { transform });
						faceObjectBuffer.bindVS(context, 0);
						worldChunk._gpuFaces.draw(context, false);
						bool drewDetail = false;
						for (ac::gpuModel& section : worldChunk._opaqueSections) {
							if (section._index.count() == 0) continue;
							if (!drewDetail) {
								program.bindShaders(context);
								drewDetail = true;
							}
							renderer.submit({ &section, nullptr, transform });
						}
						if (drewDetail)
							renderer.render();
					}
					else {
						program.bindShaders(context);
						for (ac::gpuModel& section : worldChunk._opaqueSections)
							if (section._index.count() != 0)
								renderer.submit({ &section, nullptr, transform });
					}
				}
				renderer.render();
				// Generic dynamic meshes use the regular point-shadow vertex format.
				// Humanoids then bind their skinned shadow shader so animated limbs and
				// alpha-cutout skin layers cast their actual silhouettes.
				program.bindShaders(context);
				dynamicRenderer.renderMeshShadows(light.position, light.radius);
				dynamicRenderer.renderHumanoidShadows(light.position, light.radius);
				ID3D11ShaderResourceView* nullFace = nullptr;
				context->VSSetShaderResources(66, 1, &nullFace);
			}

			context->OMSetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.Get());
			context->RSSetViewports(1, &previousViewport);
			context->RSSetState(previousRasterizer.Get());
			return true;
		}

		void bind(ID3D11DeviceContext* context) {
			context->PSSetShaderResources(ac::SRV_POINT_SHADOWS, 1, shaderView.GetAddressOf());
			context->PSSetSamplers(1, 1, sampler.GetAddressOf());
			receiverBuffer.update(context, receivers);
			receiverBuffer.bindPS(context, 6);
		}

		void disable(ID3D11DeviceContext* context) {
			receivers = {};
			receiverBuffer.update(context, receivers);
		}
	};

	// One orthographic shadow pass covers the visible world for the infinitely
	// distant sun or moon. Unlike the small point-light cubemaps, this also keeps
	// direct celestial light out of roofs, tunnels, and deep caves.
	struct directionalShadowMap {
		static constexpr UINT cascadeCount = 3;
		static constexpr UINT resolution = 2048;
		static constexpr float nearCoverage = 32.0f;
		static constexpr float middleCoverage = 112.0f;

		struct shadowData {
			dx::XMFLOAT4X4 viewProjection[3]{};
			dx::XMFLOAT4 cascadeSplits{ 14.0f, 50.0f, 3.0f, 0.0f };
			dx::XMFLOAT2 texelSize{ 1.0f / resolution, 1.0f / resolution };
			float enabled = 0.0f;
			float padding = 0.0f;
			dx::XMFLOAT4 cascadeTexelWorld{ 0.0f, 0.0f, 0.0f, 0.0f };
		};

		ac::shaderProgram program;
		ac::shaderProgram faceProgram;
		ac::constantBuffer<shadowData> buffer;
		ac::constantBuffer<ac::objectData> faceObjectBuffer;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilView> depthView[3];
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> shaderView;
		Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizerState;
		dx::XMFLOAT3 lastCameraPosition{};
		dx::XMFLOAT3 lastLightDirection{};
		dx::XMFLOAT4X4 cachedCascadeMatrices[3]{};
		uint64_t lastBlockRevision = UINT64_MAX;
		float lastShadowDistance = -1.0f;
		uint32_t updateSerial = 0;
		std::chrono::steady_clock::time_point lastRenderTime{};
		bool valid = false;

		void create(ID3D11Device* device) {
			program.initVertexShader(device, L"assets/shader/directionalShadowVertex.hlsl", "main", "vs_5_0");
			faceProgram.initVertexShader(device, L"assets/shader/directionalFaceShadowVertex.hlsl", "main", "vs_5_0");
			program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _position), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(ac::vertex, _normal), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(ac::vertex, _uv), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "MATID", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(ac::vertex, _material), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "AO", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(ac::vertex, _ao), D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "OPACITY", 0, DXGI_FORMAT_R32_FLOAT, 0, offsetof(ac::vertex, _opacity), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			buffer.create(device);
			faceObjectBuffer.create(device);

			D3D11_TEXTURE2D_DESC textureDescription{};
			textureDescription.Width = resolution;
			textureDescription.Height = resolution;
			textureDescription.MipLevels = 1;
			textureDescription.ArraySize = cascadeCount;
			textureDescription.Format = DXGI_FORMAT_R32_TYPELESS;
			textureDescription.SampleDesc.Count = 1;
			textureDescription.Usage = D3D11_USAGE_DEFAULT;
			textureDescription.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
			DX_CHECK(device->CreateTexture2D(&textureDescription, nullptr, texture.GetAddressOf()));

			for (UINT cascade = 0; cascade < cascadeCount; ++cascade) {
				D3D11_DEPTH_STENCIL_VIEW_DESC depthDescription{};
				depthDescription.Format = DXGI_FORMAT_D32_FLOAT;
				depthDescription.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
				depthDescription.Texture2DArray.MipSlice = 0;
				depthDescription.Texture2DArray.FirstArraySlice = cascade;
				depthDescription.Texture2DArray.ArraySize = 1;
				DX_CHECK(device->CreateDepthStencilView(
					texture.Get(), &depthDescription, depthView[cascade].GetAddressOf()));
			}
			D3D11_SHADER_RESOURCE_VIEW_DESC resourceDescription{};
			resourceDescription.Format = DXGI_FORMAT_R32_FLOAT;
			resourceDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			resourceDescription.Texture2DArray.MipLevels = 1;
			resourceDescription.Texture2DArray.ArraySize = cascadeCount;
			DX_CHECK(device->CreateShaderResourceView(texture.Get(), &resourceDescription, shaderView.GetAddressOf()));

			D3D11_SAMPLER_DESC samplerDescription{};
			samplerDescription.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
			samplerDescription.AddressU = samplerDescription.AddressV = samplerDescription.AddressW =
				D3D11_TEXTURE_ADDRESS_BORDER;
			samplerDescription.BorderColor[0] = samplerDescription.BorderColor[1] =
				samplerDescription.BorderColor[2] = samplerDescription.BorderColor[3] = 0.0f;
			samplerDescription.ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL;
			samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
			DX_CHECK(device->CreateSamplerState(&samplerDescription, sampler.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depthDescriptionState{};
			depthDescriptionState.DepthEnable = TRUE;
			depthDescriptionState.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
			depthDescriptionState.DepthFunc = D3D11_COMPARISON_LESS;
			DX_CHECK(device->CreateDepthStencilState(&depthDescriptionState, depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizerDescription{};
			rasterizerDescription.FillMode = D3D11_FILL_SOLID;
			rasterizerDescription.CullMode = D3D11_CULL_BACK;
			rasterizerDescription.DepthClipEnable = TRUE;
			rasterizerDescription.DepthBias = 750;
			rasterizerDescription.SlopeScaledDepthBias = 3.5f;
			rasterizerDescription.DepthBiasClamp = 0.008f;
			DX_CHECK(device->CreateRasterizerState(&rasterizerDescription, rasterizerState.GetAddressOf()));
		}

		void render(
			ID3D11DeviceContext* context,
			ac::staticRenderer& renderer,
			std::unordered_map<ac::worldChunkKey, ac::worldChunkGPU, ac::worldChunkKeyHash>& chunks,
			const dx::XMFLOAT3& cameraPosition,
			const dx::XMFLOAT3& lightDirection,
			float intensity,
			uint64_t blockRevision,
			float shadowDistance
		) {
			if (intensity <= .001f) {
				shadowData disabled{};
				buffer.update(context, disabled);
				valid = false;
				return;
			}
			shadowDistance = std::max(shadowDistance, middleCoverage * 0.5f);
			const std::array<float, cascadeCount> coverages{
				nearCoverage, middleCoverage, shadowDistance * 2.0f
			};
			const float cameraDx = cameraPosition.x - lastCameraPosition.x;
			const float cameraDz = cameraPosition.z - lastCameraPosition.z;
			const float directionSimilarity = lightDirection.x * lastLightDirection.x +
				lightDirection.y * lastLightDirection.y + lightDirection.z * lastLightDirection.z;
			const float rebuildDistance = nearCoverage * 0.08f;
			const bool distanceChanged = std::abs(shadowDistance - lastShadowDistance) > 0.01f;
			const bool invalidated = !valid ||
				cameraDx * cameraDx + cameraDz * cameraDz > rebuildDistance * rebuildDistance ||
				directionSimilarity < .99995f || blockRevision != lastBlockRevision ||
				distanceChanged;
			if (!invalidated) return;
			const auto now = std::chrono::steady_clock::now();
			const auto minimumInterval = blockRevision != lastBlockRevision
				? std::chrono::milliseconds(90)
				: std::chrono::milliseconds(33);
			if (valid && now - lastRenderTime < minimumInterval) return;

			const dx::XMVECTOR direction = dx::XMVector3Normalize(dx::XMLoadFloat3(&lightDirection));
			const dx::XMVECTOR look = dx::XMVectorNegate(direction);
			const dx::XMVECTOR up = std::abs(lightDirection.y) > .96f
				? dx::XMVectorSet(0, 0, 1, 0)
				: dx::XMVectorSet(0, 1, 0, 0);

			Microsoft::WRL::ComPtr<ID3D11RenderTargetView> previousTarget;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilView> previousDepth;
			context->OMGetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.GetAddressOf());
			UINT viewportCount = 1;
			D3D11_VIEWPORT previousViewport{};
			context->RSGetViewports(&viewportCount, &previousViewport);
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> previousRasterizer;
			context->RSGetState(previousRasterizer.GetAddressOf());

			ID3D11ShaderResourceView* nullResource = nullptr;
			context->PSSetShaderResources(ac::SRV_CELESTIAL_SHADOW, 1, &nullResource);
			context->RSSetState(rasterizerState.Get());
			context->OMSetDepthStencilState(depthState.Get(), 0);
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			D3D11_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(resolution), static_cast<float>(resolution), 0.0f, 1.0f };
			context->RSSetViewports(1, &viewport);

			shadowData data{};
			data.enabled = 1.0f;
			data.texelSize = { 1.0f / resolution, 1.0f / resolution };
			data.cascadeSplits = { coverages[0] * 0.45f, coverages[1] * 0.45f, static_cast<float>(cascadeCount), 0.0f };
			data.cascadeTexelWorld = {
				coverages[0] / static_cast<float>(resolution),
				coverages[1] / static_cast<float>(resolution),
				coverages[2] / static_cast<float>(resolution),
				0.0f
			};

			dx::XMFLOAT4X4 cascadeMatrices[3]{};
			dx::XMFLOAT3 shadowCenter = cameraPosition;
			++updateSerial;
			const bool worldChanged = blockRevision != lastBlockRevision;
			for (UINT cascade = 0; cascade < cascadeCount; ++cascade) {
				const bool rebuildCascade = !valid || worldChanged || distanceChanged || cascade == 0u ||
					(cascade == 1u && (updateSerial % 2u) == 0u) ||
					(cascade == 2u && (updateSerial % 4u) == 0u);
				const float coverage = coverages[cascade];
				const float texelWorld = coverage / static_cast<float>(resolution);
				dx::XMFLOAT3 targetWorld{
					cameraPosition.x,
					CHUNK_HEIGHT * .5f,
					cameraPosition.z
				};
				dx::XMVECTOR target = dx::XMLoadFloat3(&targetWorld);
				dx::XMVECTOR eye = dx::XMVectorMultiplyAdd(
					direction, dx::XMVectorReplicate(512.0f), target);
				dx::XMMATRIX view = dx::XMMatrixLookToLH(eye, look, up);
				const dx::XMVECTOR viewCenter = dx::XMVector3TransformCoord(target, view);
				const float snappedX = std::floor(dx::XMVectorGetX(viewCenter) / texelWorld) * texelWorld;
				const float snappedY = std::floor(dx::XMVectorGetY(viewCenter) / texelWorld) * texelWorld;
				const dx::XMVECTOR snappedView = dx::XMVectorSet(
					snappedX, snappedY, dx::XMVectorGetZ(viewCenter), 1.0f);
				target = dx::XMVector3TransformCoord(snappedView, dx::XMMatrixInverse(nullptr, view));
				dx::XMStoreFloat3(&targetWorld, target);
				shadowCenter = { targetWorld.x, CHUNK_HEIGHT * .5f, targetWorld.z };
				eye = dx::XMVectorMultiplyAdd(
					direction, dx::XMVectorReplicate(512.0f), target);
				view = dx::XMMatrixLookToLH(eye, look, up);
				const dx::XMMATRIX projection = dx::XMMatrixOrthographicLH(coverage, coverage, .1f, 1024.0f);
				dx::XMStoreFloat4x4(&cascadeMatrices[cascade], dx::XMMatrixTranspose(view * projection));
				if (!rebuildCascade) {
					cascadeMatrices[cascade] = cachedCascadeMatrices[cascade];
					continue;
				}
				data.viewProjection[0] = cascadeMatrices[cascade];
				buffer.update(context, data);
				buffer.bindVS(context, 8);

				context->OMSetRenderTargets(0, nullptr, depthView[cascade].Get());
				context->ClearDepthStencilView(depthView[cascade].Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);

				const float cullExtent = coverage * 0.5f + CHUNK_WIDTH;
				for (auto& pair : chunks) {
					ac::worldChunkGPU& worldChunk = pair.second;
					if (!worldChunk._chunk) continue;
					const auto& position = worldChunk._chunk->_position;
					const float minimumX = static_cast<float>(position.x * CHUNK_WIDTH);
					const float minimumZ = static_cast<float>(position.z * CHUNK_LENGTH);
					const float centerX = minimumX + CHUNK_WIDTH * .5f;
					const float centerZ = minimumZ + CHUNK_LENGTH * .5f;
					if (std::abs(centerX - shadowCenter.x) > cullExtent ||
						std::abs(centerZ - shadowCenter.z) > cullExtent)
						continue;

					dx::XMFLOAT4X4 transform = worldChunk._worldTransform;
					if (worldChunk._hasGpuMesh && worldChunk._gpuFaces.count(false) != 0) {
						renderer.render();
						faceProgram.bindShaders(context);
						faceObjectBuffer.update(context, { transform });
						faceObjectBuffer.bindVS(context, 0);
						worldChunk._gpuFaces.draw(context, false);
						if (cascade <= 1) {
							bool drewDetail = false;
							for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
								ac::gpuModel& mesh = worldChunk._opaqueSections[section];
								if (mesh._index.count() == 0) continue;
								if (!drewDetail) {
									program.bindShaders(context);
									drewDetail = true;
								}
								renderer.submit({ &mesh, nullptr, transform });
							}
							if (drewDetail)
								renderer.render();
						}
					}
					else if (!worldChunk._hasGpuMesh) {
						program.bindShaders(context);
						for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
							ac::gpuModel& mesh = worldChunk._opaqueSections[section];
							if (mesh._index.count() != 0)
								renderer.submit({ &mesh, nullptr, transform });
						}
					}
				}
				renderer.render();
			}

			data.viewProjection[0] = cascadeMatrices[0];
			data.viewProjection[1] = cascadeMatrices[1];
			data.viewProjection[2] = cascadeMatrices[2];
			cachedCascadeMatrices[0] = cascadeMatrices[0];
			cachedCascadeMatrices[1] = cascadeMatrices[1];
			cachedCascadeMatrices[2] = cascadeMatrices[2];
			buffer.update(context, data);
			lastCameraPosition = cameraPosition;
			lastLightDirection = lightDirection;
			lastBlockRevision = blockRevision;
			lastShadowDistance = shadowDistance;
			lastRenderTime = now;
			valid = true;

			context->OMSetRenderTargets(1, previousTarget.GetAddressOf(), previousDepth.Get());
			context->RSSetViewports(1, &previousViewport);
			context->RSSetState(previousRasterizer.Get());
		}

		void bind(ID3D11DeviceContext* context) {
			context->PSSetShaderResources(ac::SRV_CELESTIAL_SHADOW, 1, shaderView.GetAddressOf());
			context->PSSetSamplers(2, 1, sampler.GetAddressOf());
			buffer.bindPS(context, 8);
		}

		void disable(ID3D11DeviceContext* context) {
			shadowData disabled{};
			buffer.update(context, disabled);
			valid = false;
		}
	};

	class blockOutlineRenderer {
	private:
		struct outlineVertex {
			dx::XMFLOAT3 position;
		};

		struct outlineData {
			dx::XMFLOAT4X4 worldViewProjection;
			dx::XMFLOAT4 color;
		};

		ac::shaderProgram _program;
		ac::constantBuffer<outlineData> _constantBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer> _indexBuffer;
		Microsoft::WRL::ComPtr<ID3D11BlendState> _blendState;
		Microsoft::WRL::ComPtr<ID3D11DepthStencilState> _depthState;
		Microsoft::WRL::ComPtr<ID3D11RasterizerState> _rasterizerState;

	public:
		void create(ID3D11Device* device) {
			_program.initVertexShader(device, L"assets/shader/blockOutline.hlsl", "vertexMain", "vs_5_0");
			_program.initPixelShader(device, L"assets/shader/blockOutline.hlsl", "pixelMain", "ps_5_0");
			_program.initInputLayout(device, {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(outlineVertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0 }
			});
			_constantBuffer.create(device);

			constexpr std::array<outlineVertex, 8> vertices = {
				outlineVertex{ { 0.0f, 0.0f, 0.0f } }, outlineVertex{ { 1.0f, 0.0f, 0.0f } },
				outlineVertex{ { 1.0f, 1.0f, 0.0f } }, outlineVertex{ { 0.0f, 1.0f, 0.0f } },
				outlineVertex{ { 0.0f, 0.0f, 1.0f } }, outlineVertex{ { 1.0f, 0.0f, 1.0f } },
				outlineVertex{ { 1.0f, 1.0f, 1.0f } }, outlineVertex{ { 0.0f, 1.0f, 1.0f } }
			};
			constexpr std::array<uint16_t, 24> indices = {
				0, 1, 1, 2, 2, 3, 3, 0,
				4, 5, 5, 6, 6, 7, 7, 4,
				0, 4, 1, 5, 2, 6, 3, 7
			};

			D3D11_BUFFER_DESC vertexDescription{};
			vertexDescription.ByteWidth = sizeof(vertices);
			vertexDescription.Usage = D3D11_USAGE_IMMUTABLE;
			vertexDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
			D3D11_SUBRESOURCE_DATA vertexData{ vertices.data() };
			DX_CHECK(device->CreateBuffer(&vertexDescription, &vertexData, _vertexBuffer.GetAddressOf()));

			D3D11_BUFFER_DESC indexDescription{};
			indexDescription.ByteWidth = sizeof(indices);
			indexDescription.Usage = D3D11_USAGE_IMMUTABLE;
			indexDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
			D3D11_SUBRESOURCE_DATA indexData{ indices.data() };
			DX_CHECK(device->CreateBuffer(&indexDescription, &indexData, _indexBuffer.GetAddressOf()));

			D3D11_BLEND_DESC blend{};
			blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
			DX_CHECK(device->CreateBlendState(&blend, _blendState.GetAddressOf()));

			D3D11_DEPTH_STENCIL_DESC depth{};
			depth.DepthEnable = TRUE;
			depth.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
			depth.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
			DX_CHECK(device->CreateDepthStencilState(&depth, _depthState.GetAddressOf()));

			D3D11_RASTERIZER_DESC rasterizer{};
			rasterizer.FillMode = D3D11_FILL_SOLID;
			rasterizer.CullMode = D3D11_CULL_NONE;
			rasterizer.DepthClipEnable = TRUE;
			DX_CHECK(device->CreateRasterizerState(&rasterizer, _rasterizerState.GetAddressOf()));
		}

		void render(
			ID3D11DeviceContext* context,
			const dx::XMINT3& block,
			const dx::XMFLOAT3& localMin,
			const dx::XMFLOAT3& localMax,
			const dx::XMFLOAT4X4& view,
			const dx::XMFLOAT4X4& projection
		) {
			Microsoft::WRL::ComPtr<ID3D11InputLayout> oldLayout;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldVertexBuffer;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldIndexBuffer;
			Microsoft::WRL::ComPtr<ID3D11Buffer> oldConstantBuffer;
			Microsoft::WRL::ComPtr<ID3D11VertexShader> oldVertexShader;
			Microsoft::WRL::ComPtr<ID3D11PixelShader> oldPixelShader;
			Microsoft::WRL::ComPtr<ID3D11GeometryShader> oldGeometryShader;
			Microsoft::WRL::ComPtr<ID3D11HullShader> oldHullShader;
			Microsoft::WRL::ComPtr<ID3D11DomainShader> oldDomainShader;
			Microsoft::WRL::ComPtr<ID3D11BlendState> oldBlendState;
			Microsoft::WRL::ComPtr<ID3D11DepthStencilState> oldDepthState;
			Microsoft::WRL::ComPtr<ID3D11RasterizerState> oldRasterizerState;
			D3D11_PRIMITIVE_TOPOLOGY oldTopology{};
			DXGI_FORMAT oldIndexFormat{};
			UINT oldVertexStride = 0;
			UINT oldVertexOffset = 0;
			UINT oldIndexOffset = 0;
			UINT oldSampleMask = 0;
			UINT oldStencilReference = 0;
			float oldBlendFactor[4]{};

			context->IAGetInputLayout(oldLayout.GetAddressOf());
			context->IAGetPrimitiveTopology(&oldTopology);
			context->IAGetVertexBuffers(0, 1, oldVertexBuffer.GetAddressOf(), &oldVertexStride, &oldVertexOffset);
			context->IAGetIndexBuffer(oldIndexBuffer.GetAddressOf(), &oldIndexFormat, &oldIndexOffset);
			context->VSGetConstantBuffers(0, 1, oldConstantBuffer.GetAddressOf());
			context->VSGetShader(oldVertexShader.GetAddressOf(), nullptr, nullptr);
			context->PSGetShader(oldPixelShader.GetAddressOf(), nullptr, nullptr);
			context->GSGetShader(oldGeometryShader.GetAddressOf(), nullptr, nullptr);
			context->HSGetShader(oldHullShader.GetAddressOf(), nullptr, nullptr);
			context->DSGetShader(oldDomainShader.GetAddressOf(), nullptr, nullptr);
			context->OMGetBlendState(oldBlendState.GetAddressOf(), oldBlendFactor, &oldSampleMask);
			context->OMGetDepthStencilState(oldDepthState.GetAddressOf(), &oldStencilReference);
			context->RSGetState(oldRasterizerState.GetAddressOf());

			const float sx = std::max(0.001f, localMax.x - localMin.x);
			const float sy = std::max(0.001f, localMax.y - localMin.y);
			const float sz = std::max(0.001f, localMax.z - localMin.z);
			const dx::XMMATRIX world = dx::XMMatrixScaling(sx * 1.006f, sy * 1.006f, sz * 1.006f) *
				dx::XMMatrixTranslation(
					static_cast<float>(block.x) + localMin.x - sx * 0.003f,
					static_cast<float>(block.y) + localMin.y - sy * 0.003f,
					static_cast<float>(block.z) + localMin.z - sz * 0.003f
				);
			outlineData data{};
			dx::XMStoreFloat4x4(
				&data.worldViewProjection,
				dx::XMMatrixTranspose(world * dx::XMLoadFloat4x4(&view) * dx::XMLoadFloat4x4(&projection))
			);
			data.color = { 0.65f, 0.88f, 1.0f, 1.0f };
			_constantBuffer.update(context, data);
			_constantBuffer.bindVS(context, 0);
			_program.bindShaders(context);
			context->GSSetShader(nullptr, nullptr, 0);
			context->HSSetShader(nullptr, nullptr, 0);
			context->DSSetShader(nullptr, nullptr, 0);
			context->OMSetBlendState(_blendState.Get(), nullptr, 0xffffffffu);
			context->OMSetDepthStencilState(_depthState.Get(), 0);
			context->RSSetState(_rasterizerState.Get());
			context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
			const UINT stride = sizeof(outlineVertex);
			const UINT offset = 0;
			ID3D11Buffer* vertexBuffer = _vertexBuffer.Get();
			context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
			context->IASetIndexBuffer(_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
			context->DrawIndexed(24, 0, 0);

			ID3D11Buffer* oldVertexBufferPointer = oldVertexBuffer.Get();
			ID3D11Buffer* oldConstantBufferPointer = oldConstantBuffer.Get();
			context->IASetInputLayout(oldLayout.Get());
			context->IASetPrimitiveTopology(oldTopology);
			context->IASetVertexBuffers(0, 1, &oldVertexBufferPointer, &oldVertexStride, &oldVertexOffset);
			context->IASetIndexBuffer(oldIndexBuffer.Get(), oldIndexFormat, oldIndexOffset);
			context->VSSetConstantBuffers(0, 1, &oldConstantBufferPointer);
			context->VSSetShader(oldVertexShader.Get(), nullptr, 0);
			context->PSSetShader(oldPixelShader.Get(), nullptr, 0);
			context->GSSetShader(oldGeometryShader.Get(), nullptr, 0);
			context->HSSetShader(oldHullShader.Get(), nullptr, 0);
			context->DSSetShader(oldDomainShader.Get(), nullptr, 0);
			context->OMSetBlendState(oldBlendState.Get(), oldBlendFactor, oldSampleMask);
			context->OMSetDepthStencilState(oldDepthState.Get(), oldStencilReference);
			context->RSSetState(oldRasterizerState.Get());
		}

		void render(
			ID3D11DeviceContext* context,
			const dx::XMINT3& block,
			const dx::XMFLOAT4X4& view,
			const dx::XMFLOAT4X4& projection
		) {
			render(context, block, { 0, 0, 0 }, { 1, 1, 1 }, view, projection);
		}
	};

	class voxelPipeline final : public ac::applicationPipeline {
	private:
		ac::window& _window;
		ac::graphicsContext& _graphicsSettings;
		ID3D11Device* _device;
		ID3D11DeviceContext* _context;
		staticPipeline& _pipeline;
		pointShadowMap& _shadowMap;
		directionalShadowMap _celestialShadow;
		ac::staticRenderer& _renderer;
		ac::textureLibrary& _textures;
		ac::blockTextureSet& _blockTextures;
		ac::staticAssetManager& _blocks;
		ac::modelManager& _models;
		ac::contentPackSet& _contentPacks;
		ac::itemIconAtlas _itemIcons;
		ac::world& _world;
		ac::worldStreamer& _streamer;
		ac::modding::callbackBlockRegistryPort _modBlocks;
		ac::modding::callbackWorldPort _modWorld;
		ac::modding::callbackEntityPort _modEntities;
		ac::modding::callbackPresentationPort _modPresentation;
		ac::modding::callbackUiPort _modUi;
		ac::modding::directoryStoragePort _modStorage;
		ac::modding::callbackRegistryCatalogPort _modRegistries;
		ac::modding::callbackNetworkPort _modNetwork;
		ac::modding::streamLogPort _modLog;
		ac::modding::gameModHost _modHost;
		ac::player& _player;
		ac::worldStreamer::blockReadCache _blockReadCache;
		std::vector<ac::fallingBlockEntity> _crushedFallingScratch;
		std::vector<ac::fallingBlockEntity> _visibleFallingScratch;
		std::vector<ac::itemEntity> _visibleItemScratch;
		struct scoredChunkLight {
			uint32_t index = 0;
			float score = 0.0f;
		};
		std::vector<scoredChunkLight> _chunkLightRankScratch;
		struct shadowCandidate {
			const ac::gpuLight* light = nullptr;
			uint32_t sourceLightIndex = 0;
			bool includeWorldCasters = false;
			float score = 0.0f;
		};
		std::vector<shadowCandidate> _shadowCandidateScratch;
		struct translucentChunk {
			ac::gpuModel* _model = nullptr;
			ac::gpuChunkFaceMesh* _gpuFaces = nullptr;
			dx::XMFLOAT4X4 _transform = {};
			dx::XMINT3 _chunkPosition = {};
			float _distanceSquared = 0.0f;
			bool _farLod = false;
		};
		std::vector<translucentChunk> _translucentScratch;
		struct visibleTerrainChunk {
			ac::worldChunkGPU* chunk = nullptr;
			dx::XMFLOAT4X4 transform{};
			dx::XMINT3 position{};
			bool farLod = false;
		};
		std::vector<visibleTerrainChunk> _visibleTerrainScratch;
		uint32_t _renderFrameCounter = 0;
		dx::XMFLOAT3 _lastReflectionEye{ 1.0e30f, 1.0e30f, 1.0e30f };
		ac::simulationServer _server;
		ac::dynamicRenderer& _dynamicRenderer;
		ac::dynamicEntityHandle _playerEntity;
		ac::constantBuffer<ac::cameraData>& _cameraBuffer;
		ac::structuredBuffer<ac::gpuLight>& _lights;
		std::vector<ac::gpuLight>& _lightData;
		ac::constantBuffer<ac::lightBufferData>& _lightBuffer;
		ac::structuredBuffer<uint32_t>& _lightOccluders;
		std::vector<uint32_t>& _lightOccluderData;
		ac::constantBuffer<ac::lightOcclusionBufferData>& _lightOcclusionBuffer;
		ac::structuredBuffer<ac::gpuVoxelLight> _voxelLights;
		ac::constantBuffer<ac::voxelLightBufferData> _voxelLightBuffer;
		ac::constantBuffer<ac::chunkLightBufferData> _chunkLightBuffer;
		ac::constantBuffer<ac::daylightBufferData> _daylightBuffer;
		ac::structuredBuffer<int32_t> _weatherSurfaces;
		std::vector<int32_t> _weatherSurfaceData =
			std::vector<int32_t>(WEATHER_SURFACE_CELLS, INT32_MIN);
		dx::XMINT2 _weatherSurfaceOrigin{ INT32_MAX, INT32_MAX };
		float _weatherSurfaceRefresh = 0.0f;
		float _weatherWetness = 0.0f;
		ac::structuredBuffer<uint32_t> _reflectionVoxels;
		ac::constantBuffer<reflectionVolumeData> _reflectionVolumeBuffer;
		std::vector<uint32_t> _reflectionVoxelData =
			std::vector<uint32_t>(REFLECTION_VOLUME_VOXELS);
		dx::XMINT3 _reflectionVolumeOrigin{ INT32_MAX, INT32_MAX, INT32_MAX };
		uint64_t _reflectionVolumeRevision = UINT64_MAX;
		std::chrono::steady_clock::time_point _lastReflectionVolumeBuild{};
		float _reflectionPlaneHeight = 0.0f;
		bool _reflectionPlaneAvailable = false;
		std::vector<ac::gpuVoxelLight> _voxelLightTable =
			std::vector<ac::gpuVoxelLight>(ac::VOXEL_LIGHT_TABLE_SIZE);
		std::vector<ac::gpuVoxelLight> _voxelLightScratch =
			std::vector<ac::gpuVoxelLight>(ac::VOXEL_LIGHT_TABLE_SIZE);
		ac::constantBuffer<ac::objectData> _gpuObjectBuffer;
		ac::gpuFrameTimer _lightingTimer;
		blockOutlineRenderer _outlineRenderer;
		ac::blockBreakOverlayRenderer _breakOverlay;
		ac::crosshairRenderer _crosshair;
		ac::nuklearHost _nuklear;
		ac::texture _guiWidgets;
		ac::texture _guiIcons;
		ac::texture _guiInventory;
		ac::texture _guiCrafting;
		ac::texture _guiChest;
		ac::texture _guiFurnace;
		ac::texture _guiCreativeItems;
		ac::texture _guiCreativeInv;
		bool _guiReady = false;
		skyRenderer _skyRenderer;
		sceneRefractionBuffer _sceneRefraction;
		motionBlurPass _motionBlur;
		fxaaPass _fxaa;
		ac::scenePostProcess _scenePost;
		ac::scenePostSettings _postSettings;
		planarReflectionTarget _planarReflection;
		ac::constantBuffer<planarReflectionData> _planarReflectionDataBuffer;
		ac::constantBuffer<planarClipData> _planarClipBuffer;
		ac::uiInputState _uiInput;
		std::array<ac::blockId, HUD_HOTBAR_COUNT> _hotbarBlocks{};
		std::array<uint32_t, HUD_HOTBAR_COUNT> _hotbarCounts{};
		std::array<ac::blockId, HUD_INV_SLOTS> _inventoryBlocks{};
		std::array<uint32_t, HUD_INV_SLOTS> _inventoryCounts{};
		std::vector<ac::blockId> _chooserCatalog;
		std::vector<ac::blockId> _chooserFiltered;
		std::array<char, 48> _chooserSearchBuf{};
		bool _chooserFilterActive = false;
		bool _chooserScrollDragging = false;
		bool _chooserSearchFocus = false;
		struct nk_rect _chooserSearchRect = {};
		int _chooserScroll = 0;
		std::string _consoleBuffer;
		static constexpr int CONSOLE_LINE_CHARS = static_cast<int>(UINT16_MAX);
		std::vector<char> _consoleEditBuf = std::vector<char>(CONSOLE_LINE_CHARS + 1, '\0');
		bool _consoleFocusEdit = false;
		bool _consoleCursorToEnd = false;
		std::string _consoleTabStem;
		std::vector<std::string> _consoleTabMatches;
		int _consoleTabIndex = -1;
		std::array<std::string, 32> _consoleHistory{};
		size_t _consoleHistoryCount = 0;
		ac::audioMixer _audio;
		ac::soundVoiceHandle _weatherWindVoice{};
		ac::soundVoiceHandle _weatherPrecipitationVoice{};
		ac::weatherSound _weatherPrecipitationSound = ac::weatherSound::rain;
		float _weatherWindGain = 0.0f;
		float _weatherPrecipitationGain = 0.0f;
		float _previousWeatherLightning = 0.0f;
		ac::particleSystem _particles;
		ac::weatherSystem _weather;
		ac::fallingBlockRenderer _fallingBlocks;
		ac::itemEntitySystem _items;
		std::unique_ptr<ac::livingEntitySystem> _livingEntities;
		ac::blockEntityStore _blockEntities{ std::filesystem::path{} };
		ac::gameScreen _screen = ac::gameScreen::mainMenu;
		ac::gameScreen _settingsReturnScreen = ac::gameScreen::paused;
		bool _worldSessionOpen = false;
		std::vector<ac::savedWorldInfo> _worldList;
		int _selectedWorldIndex = 0;
		int _confirmDeleteIndex = -1;
		std::array<char, 64> _newWorldName{};
		std::array<char, 24> _newWorldSeed{};
		bool _createWorldNameFocus = false;
		std::string _worldMenuError;
		std::string _lastWorldPath;
		float _autosaveTimer = 0.0f;
		dx::XMINT3 _openChestBlock{};
		dx::XMINT3 _openFurnaceBlock{};
		float _walkBobPhase = 0.0f;
		float _sprintFov = 0.0f;
		float _cameraShake = 0.0f;
		float _footstepTimer = 0.0f;
		float _fpsElapsed = 0.0f;
		uint32_t _fpsFrameCount = 0;
		bool _vsyncEnabled = true;
		bool _fxaaEnabled = true;
		bool _motionBlurEnabled = true;
		bool _shadowsEnabled = true;
		uint32_t _lightingQuality = 1u;
		bool _voxelLightingHasData = false;
		dx::XMINT3 _boundChunkLights{ INT32_MIN, INT32_MIN, INT32_MIN };
		bool _chunkLightsBound = false;
		float _waterAnimationTime = 0.0f;
		uint32_t _waterMaterial = UINT32_MAX;
		bool _cameraUnderwater = false;
		float _dayTimeSeconds = APPLICATION_CONFIG.initialDayTimeSeconds;
		size_t _sunLightIndex = 0u;
		dx::XMFLOAT3 _celestialDirection{ 0.0f, 1.0f, 0.0f };
		float _celestialIntensity = 0.0f;
		dx::XMFLOAT3 _fogSkyColor{ 0.25f, 0.45f, 0.72f };
		size_t _fpsLimitIndex = 0;
		size_t _viewDistanceIndex = 0;
		size_t _fovIndex = 0;
		size_t _renderScaleIndex = 0;
		size_t _dayCycleIndex = 0;
		enum class settingsTab { user, world };
		settingsTab _settingsTab = settingsTab::user;
		ac::blockId _cursorItem = 0;
		uint32_t _cursorCount = 0;
		enum class inventoryTab { chooser, storage };
		inventoryTab _inventoryTab = inventoryTab::chooser;
		bool _inventoryPaintActive = false;
		bool _inventoryPaintOnes = false; // RMB drag: one per slot
		ac::blockId _inventoryPaintItem = 0;
		uint32_t _inventoryPaintPool = 0;
		std::vector<int> _inventoryPaintSlots;
		std::array<ac::blockId, HUD_INV_SLOTS + HUD_HOTBAR_COUNT> _inventoryPaintBaseId{};
		std::array<uint32_t, HUD_INV_SLOTS + HUD_HOTBAR_COUNT> _inventoryPaintBaseCount{};
		int _lastInvClickSlot = -1;
		float _lastInvClickTime = -10.0f;
		ac::blockId _lastInvClickItem = 0;
		int _selectedHotbarSlot = 0;
		ac::blockId _selectedBlock = 0;
		bool _inventoryOpen = false;
		ac::cameraData _cameraData{};
		dx::XMFLOAT4X4 _renderView{};
		dx::XMFLOAT4X4 _renderProjection{};
		dx::XMFLOAT3 _renderAimOrigin{};
		dx::XMFLOAT3 _renderAimDirection{ 0.0f, 0.0f, 1.0f };
		bool _renderAimValid = false;
		dx::XMINT3 _renderedTargetBlock{};
		dx::XMINT3 _renderedTargetAdjacent{};
		ac::blockId _renderedTargetId = 0;
		float _renderedTargetDistance = 0.0f;
		bool _renderedTargetValid = false;
		float _crosshairColor[3] = { 245.0f / 255.0f, 204.0f / 255.0f, 92.0f / 255.0f };
		bool _crosshairColorValid = false;
		Microsoft::WRL::ComPtr<ID3D11Texture2D> _crosshairStaging;
		float _bodycamRoll = 0.0f;
		float _bodycamPitchLean = 0.0f;
		float _bodycamMotion = 0.0f;
		float _bodycamClock = 0.0f;
		float _bodycamForwardLag = 0.0f;
		float _bodycamVerticalLag = 0.0f;
		float _crouchCameraOffset = 0.0f;
		float _previousBodycamYaw = 0.0f;
		bool _bodycamInitialized = false;
		bool _spectatorMode = false;
		ac::gameMode _gameMode = ac::gameMode::survival;
		ac::gameMode _previousGameMode = ac::gameMode::survival;
		ac::recipeBook _recipes;
		dx::XMINT3 _breakTarget{};
		float _breakProgress = 0.0f;
		float _breakSecondsNeeded = 0.0f;
		bool _breakActive = false;
		float _toolPickupRemaining = 0.0f;
		bool _toolPickupArmed = false;
		float _health = ac::PLAYER_MAX_HEALTH;
		float _hunger = ac::PLAYER_MAX_HUNGER;
		float _saturation = ac::PLAYER_MAX_SATURATION;
		float _exhaustion = 0.0f;
		float _air = ac::PLAYER_MAX_AIR;
		float _fallDistance = 0.0f;
		float _fallMaxFeetY = 0.0f;
		bool _fallActive = false;
		bool _wasInWater = false;
		float _healthRegenTimer = 0.0f;
		float _starveTimer = 0.0f;
		float _eatTime = 0.0f;
		bool _eating = false;
		bool _wasGrounded = true;
		float _drownTimer = 0.0f;
		float _magmaTimer = 0.0f;
		float _hurtCooldown = 0.0f;
		std::array<ac::blockId, HUD_CRAFT2_SLOTS> _craft2Blocks{};
		std::array<uint32_t, HUD_CRAFT2_SLOTS> _craft2Counts{};
		std::array<ac::blockId, HUD_CRAFT3_SLOTS> _craft3Blocks{};
		std::array<uint32_t, HUD_CRAFT3_SLOTS> _craft3Counts{};
		bool _spawnPending = false;
		uint32_t _spawnSearchStep = 0;
		bool _spawnLandFound = false;
		dx::XMINT2 _spawnLandChunk{};
		bool _restoredPlayerPendingValidation = false;
		uint64_t _lightOcclusionSignature = UINT64_MAX;
		uint64_t _lightOcclusionBlockRevision = UINT64_MAX;
		uint64_t _blockLightingRevision = UINT64_MAX;
		uint64_t _pendingBlockLightingRevision = UINT64_MAX;
		std::chrono::steady_clock::time_point _pendingBlockLightingSince{};
		std::chrono::steady_clock::time_point _lastBlockLightingBuild{};
		std::vector<ac::gpuLight> _propagatedBlockSources;
		std::vector<ac::gpuLight> _pendingBlockSources;
		bool _pendingBlockLightingAffected = false;
		bool _pendingBlockLightingStreaming = false;

		struct lightClusterCell {
			int32_t x = 0;
			int32_t z = 0;
			bool operator==(const lightClusterCell& other) const {
				return x == other.x && z == other.z;
			}
		};
		struct lightClusterCellHash {
			size_t operator()(const lightClusterCell& cell) const {
				return static_cast<size_t>(cell.x) * 73856093u ^
					static_cast<size_t>(cell.z) * 19349663u;
			}
		};
		std::unordered_map<lightClusterCell, std::vector<uint32_t>, lightClusterCellHash> _lightClusters;
		size_t _lightClusterCount = SIZE_MAX;

		struct chunkLightSourceCache {
			uint64_t emitterRevision = UINT64_MAX;
			std::vector<ac::gpuLight> lights;
		};
		mutable std::unordered_map<
			ac::worldChunkKey, chunkLightSourceCache, ac::worldChunkKeyHash>
			_chunkLightSources;

		// One entry per emitter: the cells it lit and how strongly. Reused until
		// either the emitter itself or the geometry inside its reach changes.
		struct voxelLightSourceKey {
			int32_t x = 0;
			int32_t y = 0;
			int32_t z = 0;
			uint32_t signature = 0;

			bool operator==(const voxelLightSourceKey& other) const {
				return x == other.x && y == other.y && z == other.z &&
					signature == other.signature;
			}
		};
		struct voxelLightSourceKeyHash {
			size_t operator()(const voxelLightSourceKey& key) const {
				uint32_t hash = static_cast<uint32_t>(key.x) * 73856093u ^
					static_cast<uint32_t>(key.y) * 19349663u ^
					static_cast<uint32_t>(key.z) * 83492791u ^
					key.signature * 2654435761u;
				hash ^= hash >> 16u;
				return hash;
			}
		};
		struct voxelLightContribution {
			int32_t radius = 0;
			std::vector<std::pair<voxelCoordinate, uint32_t>> cells;
		};
		std::unordered_map<
			voxelLightSourceKey, voxelLightContribution, voxelLightSourceKeyHash>
			_voxelLightContributions;
		size_t _voxelLightCachedCells = 0;
		// Roughly 32 MB of cached contributions before falling back to
		// recomputing every emitter, so dense light farms cannot grow unbounded.
		static constexpr size_t MAX_VOXEL_LIGHT_CACHED_CELLS = 2u * 1024u * 1024u;
		uint64_t _voxelLightGeometryRevision = UINT64_MAX;

		void updateReflectionVolume() {
			const dx::XMFLOAT3 camera = _player.getCamera()._gpuData._position;
			const auto snappedOrigin = [](float coordinate) {
				const int32_t voxel = static_cast<int32_t>(std::floor(coordinate));
				constexpr int32_t step = 4;
				const int32_t snapped = voxel >= 0
					? (voxel / step) * step
					: -(((-voxel + step - 1) / step) * step);
				return snapped - static_cast<int32_t>(REFLECTION_VOLUME_SIZE / 2u);
			};
			const dx::XMINT3 origin{
				snappedOrigin(camera.x),
				std::clamp(
					snappedOrigin(camera.y),
					0,
					static_cast<int32_t>(CHUNK_HEIGHT - REFLECTION_VOLUME_SIZE)),
				snappedOrigin(camera.z)
			};
			const uint64_t revision = _streamer.blockRevision();
			const bool moved = origin.x != _reflectionVolumeOrigin.x ||
				origin.y != _reflectionVolumeOrigin.y || origin.z != _reflectionVolumeOrigin.z;
			if (!moved && revision == _reflectionVolumeRevision) return;

			const auto now = std::chrono::steady_clock::now();
			if (!moved && _lastReflectionVolumeBuild.time_since_epoch().count() != 0 &&
				now - _lastReflectionVolumeBuild < std::chrono::milliseconds(250))
				return;

			float nearestWaterDistanceSquared = (std::numeric_limits<float>::max)();
			float nearestWaterHeight = 0.0f;
			for (uint32_t y = 0; y < REFLECTION_VOLUME_SIZE; ++y) {
				for (uint32_t z = 0; z < REFLECTION_VOLUME_SIZE; ++z) {
					for (uint32_t x = 0; x < REFLECTION_VOLUME_SIZE; ++x) {
						const ac::blockId state = _streamer.blockAt(
							origin.x + static_cast<int32_t>(x),
							origin.y + static_cast<int32_t>(y),
							origin.z + static_cast<int32_t>(z));
						uint32_t packed = 0u;
						if (state != 0u) {
							if (ac::blockType(state) == ac::WATER_BLOCK_TYPE) {
								const int32_t worldX = origin.x + static_cast<int32_t>(x);
								const int32_t worldY = origin.y + static_cast<int32_t>(y);
								const int32_t worldZ = origin.z + static_cast<int32_t>(z);
								const ac::blockId above = _streamer.blockAt(worldX, worldY + 1, worldZ);
								// A submerged water cell is not a reflecting plane. Selecting one made
								// deep oceans reflect around their floor instead of their visible surface.
								if (ac::blockType(above) != ac::WATER_BLOCK_TYPE) {
									const uint8_t fluidLevel = ac::fluidLevel(state);
									const float visibleFill = fluidLevel == ac::MAX_FLUID_LEVEL
										? static_cast<float>(ac::MAX_FLUID_LEVEL - 1u) /
											static_cast<float>(ac::MAX_FLUID_LEVEL)
										: ac::fluidHeight(state);
									const float surfaceHeight = static_cast<float>(worldY) + visibleFill;
									bool enclosed = false;
									int airRun = 0;
									const int32_t maxScanY = (std::min)(
										worldY + 40, static_cast<int32_t>(CHUNK_HEIGHT - 1));
									for (int32_t scanY = worldY + 1; scanY <= maxScanY; ++scanY) {
										const ac::blockId overhead = _streamer.blockAt(
											worldX, scanY, worldZ);
										if (overhead == 0u) {
											if (++airRun >= 6) break;
											continue;
										}
										if (ac::blockType(overhead) == ac::WATER_BLOCK_TYPE) {
											airRun = 0;
											continue;
										}
										const ac::blockDefinition* overheadDefinition =
											_blocks.get(ac::blockType(overhead));
										if (overheadDefinition &&
											overheadDefinition->_occludes &&
											overheadDefinition->_renderMode != ac::RENDER_MODE_TRANSPARENT &&
											overheadDefinition->_renderMode != ac::RENDER_MODE_CUTOUT) {
											enclosed = true;
											break;
										}
										airRun = 0;
									}
									// Cave and aquifer surfaces must not drive the mirrored
									// sky pass. That atlas always composites the sky first, which
									// makes underground water reflect daylight.
									if (!enclosed) {
										const float dx = static_cast<float>(worldX) + .5f - camera.x;
										const float dy = surfaceHeight - camera.y;
										const float dz = static_cast<float>(worldZ) + .5f - camera.z;
										const float distanceSquared = dx * dx + dy * dy + dz * dz;
										if (distanceSquared < nearestWaterDistanceSquared) {
											nearestWaterDistanceSquared = distanceSquared;
											nearestWaterHeight = surfaceHeight;
										}
									}
								}
							}
							const ac::blockDefinition* definition = _blocks.get(ac::blockType(ac::visualBlockState(state)));
							if (definition && definition->_occludes &&
								definition->_renderMode != ac::RENDER_MODE_TRANSPARENT) {
								packed = 0x80000000u |
									(definition->materialForFace(ac::BLOCK_FACE_UP) & 0xFFFFu);
								packed |= (ac::isRedstoneLamp(state) ? ac::redstonePower(state) : 15u) << 16u;
							}
						}
						_reflectionVoxelData[x + REFLECTION_VOLUME_SIZE *
							(z + REFLECTION_VOLUME_SIZE * y)] = packed;
					}
				}
			}
			_reflectionVoxels.update(
				_context, _reflectionVoxelData.data(), REFLECTION_VOLUME_VOXELS);
			_reflectionVolumeBuffer.update(_context, { origin, REFLECTION_VOLUME_SIZE });
			_reflectionVolumeOrigin = origin;
			_reflectionVolumeRevision = revision;
			_lastReflectionVolumeBuild = now;
			_reflectionPlaneAvailable = nearestWaterDistanceSquared < 48.0f * 48.0f;
			_reflectionPlaneHeight = nearestWaterHeight;
		}

		void renderPlanarWaterReflection(bool gpuFaces) {
			if (!_reflectionPlaneAvailable) {
				_planarReflectionDataBuffer.update(_context, {});
				_planarReflectionDataBuffer.bindPS(_context, 10);
				_planarClipBuffer.update(_context, {});
				_planarClipBuffer.bindVS(_context, 11);
				return;
			}

			// Underwater we still need the free-surface height for volume fog and
			// exit optics, but the mirrored planar atlas must stay disabled.
			if (_cameraUnderwater) {
				planarReflectionData reflectionData{};
				reflectionData.planeHeight = _reflectionPlaneHeight;
				reflectionData.enabled = 0.0f;
				_planarReflectionDataBuffer.update(_context, reflectionData);
				_planarReflectionDataBuffer.bindPS(_context, 10);
				_planarClipBuffer.update(_context, { _reflectionPlaneHeight, 0.0f });
				_planarClipBuffer.bindVS(_context, 11);
				return;
			}

			++_renderFrameCounter;
			const dx::XMFLOAT3 eye = _cameraData._position;
			const float eyeDx = eye.x - _lastReflectionEye.x;
			const float eyeDy = eye.y - _lastReflectionEye.y;
			const float eyeDz = eye.z - _lastReflectionEye.z;
			const bool eyeMoved = eyeDx * eyeDx + eyeDy * eyeDy + eyeDz * eyeDz > 0.12f;
			if (!eyeMoved && (_renderFrameCounter & 1u) != 0u && _planarReflection.shaderView) {
				_planarReflectionDataBuffer.bindPS(_context, 10);
				_planarClipBuffer.update(_context, {});
				_planarClipBuffer.bindVS(_context, 11);
				_planarReflection.bind(_context);
				return;
			}
			_lastReflectionEye = eye;

			const dx::XMMATRIX mainView = dx::XMLoadFloat4x4(&_renderView);
			const dx::XMMATRIX projection = dx::XMLoadFloat4x4(&_renderProjection);
			const dx::XMFLOAT3 reflectedEye{
				_cameraData._position.x,
				2.0f * _reflectionPlaneHeight - _cameraData._position.y,
				_cameraData._position.z
			};
			// Reflect world space into the original camera instead of rebuilding a
			// LookTo basis. This preserves the exact projection of every point on the
			// water plane and prevents the reflected image from reversing or sliding
			// to the far side of the reflected object.
			const dx::XMVECTOR reflectionPlane = dx::XMVectorSet(
				0.0f, 1.0f, 0.0f, -_reflectionPlaneHeight);
			const dx::XMMATRIX reflectionTransform = dx::XMMatrixReflect(reflectionPlane);
			const dx::XMMATRIX reflectedView = reflectionTransform * mainView;

			planarReflectionData reflectionData{};
			dx::XMStoreFloat4x4(
				&reflectionData.viewProjection,
				dx::XMMatrixTranspose(reflectedView * projection));
			reflectionData.planeHeight = _reflectionPlaneHeight;
			reflectionData.enabled = 1.0f;
			_planarReflectionDataBuffer.update(_context, reflectionData);
			_planarReflectionDataBuffer.bindPS(_context, 10);
			_planarClipBuffer.update(_context, { _reflectionPlaneHeight, 1.0f });
			_planarClipBuffer.bindVS(_context, 11);

			_planarReflection.begin(
				_device, _context,
				(std::max)(1u, (_scenePost.width ? _scenePost.width : static_cast<UINT>((std::max)(_window.getWidth(), 1))) / 2u),
				(std::max)(1u, (_scenePost.height ? _scenePost.height : static_cast<UINT>((std::max)(_window.getHeight(), 1))) / 2u));

			ac::cameraData reflectedCamera = _cameraData;
			reflectedCamera._position = reflectedEye;
			dx::XMStoreFloat4x4(
				&reflectedCamera._view, dx::XMMatrixTranspose(reflectedView));
			dx::XMStoreFloat4x4(
				&reflectedCamera._projection, dx::XMMatrixTranspose(projection));
			dx::XMStoreFloat4x4(
				&reflectedCamera._viewProjection,
				dx::XMMatrixTranspose(reflectedView * projection));
			_cameraBuffer.update(_context, reflectedCamera);
			_cameraBuffer.bindVS(_context, 1);
			_cameraBuffer.bindPS(_context, 1);

			_skyRenderer.render(_context);
			if (gpuFaces) _pipeline.bindFacesFast(_context);
			else _pipeline.bind(_context);
			_shadowMap.bind(_context);
			_celestialShadow.bind(_context);
			_blockTextures.bind(_context);
			_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			dx::BoundingFrustum reflectedFrustum;
			dx::BoundingFrustum reflectedWorldFrustum;
			dx::BoundingFrustum::CreateFromMatrix(reflectedFrustum, projection);
			reflectedFrustum.Transform(
				reflectedWorldFrustum, dx::XMMatrixInverse(nullptr, reflectedView));
			for (auto& pair : _streamer.chunks()) {
				ac::worldChunkGPU& worldChunk = pair.second;
				if (!worldChunk._chunk) continue;
				const auto& position = worldChunk._chunk->_position;
				if (reflectedWorldFrustum.Contains(
						ac::worldStreamer::chunkSolidBounds(worldChunk)) == dx::DISJOINT)
					continue;

				dx::XMFLOAT4X4 transform = worldChunk._worldTransform;
				_gpuObjectBuffer.update(_context, { transform });
				_gpuObjectBuffer.bindVS(_context, 0);
				if (worldChunk._hasGpuMesh && worldChunk._gpuFaces.count(false) != 0)
					worldChunk._gpuFaces.draw(_context, false);
				else if (!worldChunk._hasGpuMesh) {
					for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
						ac::gpuModel& mesh = worldChunk._opaqueSections[section];
						if (mesh._index.count() == 0) continue;
						if (reflectedWorldFrustum.Contains(
								ac::worldStreamer::chunkSectionBounds(worldChunk, section)) == dx::DISJOINT)
							continue;
						mesh.bind(_context);
						mesh.draw(_context);
					}
				}
			}
			_renderer.render();
			_planarReflection.end(_context);

			_cameraBuffer.update(_context, _cameraData);
			_cameraBuffer.bindVS(_context, 1);
			_cameraBuffer.bindPS(_context, 1);
			_planarClipBuffer.update(_context, {});
			_planarClipBuffer.bindVS(_context, 11);
			_planarReflection.bind(_context);
		}

		void updateDaylight(float deltaTime) {
			_dayTimeSeconds = std::fmod(
				_dayTimeSeconds + (std::min)((std::max)(deltaTime, 0.0f), .1f) *
					DAY_CYCLE_SCALES[_dayCycleIndex],
				DAY_LENGTH_SECONDS);
			const float phase = _dayTimeSeconds / DAY_LENGTH_SECONDS;
			const float angle = phase * dx::XM_2PI;
			const float elevation = std::sin(angle);
			const dx::XMVECTOR sunVector = dx::XMVector3Normalize(dx::XMVectorSet(
				std::cos(angle), elevation, .28f * std::cos(angle * .73f), 0.0f));
			dx::XMFLOAT3 sunDirection;
			dx::XMStoreFloat3(&sunDirection, sunVector);

			const float daylight = std::clamp((elevation + .10f) / .22f, 0.0f, 1.0f);
			const float solarStrength = daylight *
				std::pow((std::max)(elevation, 0.0f), .32f);
			const float warmBlend = std::clamp(elevation / .48f, 0.0f, 1.0f);
			const dx::XMFLOAT3 sunColor{
				1.0f,
				.48f + .48f * warmBlend,
				.24f + .60f * warmBlend
			};
			const dx::XMFLOAT3 moonDirection{
				-sunDirection.x, -sunDirection.y, -sunDirection.z
			};
			const float moonStrength = (1.0f - daylight) *
				std::pow((std::max)(-elevation, 0.0f), .28f) * .24f;
			const dx::XMFLOAT3 moonColor{ .42f, .55f, .78f };
			const dx::XMFLOAT3 nightSky{ .006f, .010f, .030f };
			const dx::XMFLOAT3 daySky{ .25f, .45f, .72f };
			const dx::XMFLOAT3 skyColor{
				nightSky.x + (daySky.x - nightSky.x) * daylight,
				nightSky.y + (daySky.y - nightSky.y) * daylight,
				nightSky.z + (daySky.z - nightSky.z) * daylight
			};
			const float overcast = _weather.state().overcast;
			const float frontOvercast = _weather.frontOvercast();
			const float flash = _weather.state().lightning;
			const dx::XMFLOAT3 cloudSky{
				0.045f + daylight * 0.105f,
				0.060f + daylight * 0.125f,
				0.085f + daylight * 0.155f
			};
			const float cloudBlend = std::clamp(overcast * 0.88f, 0.0f, 0.88f);
			const dx::XMFLOAT3 overcastSky{
				skyColor.x + (cloudSky.x - skyColor.x) * cloudBlend,
				skyColor.y + (cloudSky.y - skyColor.y) * cloudBlend,
				skyColor.z + (cloudSky.z - skyColor.z) * cloudBlend
			};
			const dx::XMFLOAT3 weatherSky{
				std::min(1.0f, overcastSky.x + flash * 0.85f),
				std::min(1.0f, overcastSky.y + flash * 0.88f),
				std::min(1.0f, overcastSky.z + flash * 1.0f)
			};
			const float weatherSun = 1.0f - 0.76f * overcast + flash * 0.48f;
			const float ambient = (.055f + daylight * .48f) * (1.0f - 0.48f * overcast) + flash * 0.38f;
			_daylightBuffer.update(_context, {
				sunDirection,
				solarStrength * 1.35f * weatherSun,
				sunColor,
				ambient,
				weatherSky,
				phase,
				moonDirection,
				moonStrength * (1.0f - 0.4f * overcast),
				moonColor,
				static_cast<uint32_t>(_sunLightIndex),
				0.34f + frontOvercast * 0.52f,
				0.72f + frontOvercast * 0.58f,
				0.28f + frontOvercast * 0.42f,
				_weather.clock()
			});
			_fogSkyColor = weatherSky;

			const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
			ac::gpuLight& sun = _lightData[_sunLightIndex];
			const bool useMoon = moonStrength > solarStrength * 1.35f;
			const dx::XMFLOAT3 shadowDirection = useMoon ? moonDirection : sunDirection;
			const dx::XMFLOAT3 shadowColor = useMoon ? moonColor : sunColor;
			const float shadowIntensity = (useMoon ? moonStrength : solarStrength * 1.35f) * weatherSun;
			_celestialDirection = shadowDirection;
			_celestialIntensity = shadowIntensity;
			sun.position = {
				eye.x + shadowDirection.x * 18.0f,
				eye.y + shadowDirection.y * 18.0f,
				eye.z + shadowDirection.z * 18.0f
			};
			sun.radius = shadowIntensity > .001f ? 44.0f : 0.0f;
			sun.color = shadowColor;
			sun.intensity = shadowIntensity;
			sun.halfExtent = {};
			sun.type = ac::GPU_LIGHT_DIRECTIONAL_PROXY;
			_lights.update(_context, _lightData.data(), static_cast<UINT>(_lightData.size()));
			_lightBuffer.update(_context, {
				static_cast<uint32_t>(_lightData.size()), { 0, 0, 0 }
			});
			setClearColor(weatherSky.x, weatherSky.y, weatherSky.z, 1.0f);
		}

		void stopWeatherAudio() {
			_audio.stopVoice(_weatherWindVoice);
			_audio.stopVoice(_weatherPrecipitationVoice);
			_weatherWindGain = 0.0f;
			_weatherPrecipitationGain = 0.0f;
			_previousWeatherLightning = 0.0f;
		}

		bool playerIsOpenToSky(const dx::XMFLOAT3& eye) const {
			const int32_t x = static_cast<int32_t>(std::floor(eye.x));
			const int32_t z = static_cast<int32_t>(std::floor(eye.z));
			const int32_t firstY = std::clamp(
				static_cast<int32_t>(std::floor(eye.y)) + 1, 0, CHUNK_HEIGHT);
			for (int32_t y = firstY; y < CHUNK_HEIGHT; ++y) {
				const ac::blockId state = _streamer.blockAt(x, y, z);
				if (state == 0u) continue;
				const ac::blockDefinition* definition = _blocks.get(ac::blockType(state));
				if (definition && (definition->_solid || definition->_occludes))
					return false;
			}
			return true;
		}

		void updateWeatherAudio(
			float deltaTime,
			const ac::weatherState& weather,
			const dx::XMFLOAT3& eye
		) {
			const bool openToSky = playerIsOpenToSky(eye);
			const float shelter = _cameraUnderwater ? 0.07f : openToSky ? 1.0f : 0.22f;
			float windTarget = 0.035f;
			float precipitationTarget = 0.0f;
			ac::weatherSound precipitationSound = ac::weatherSound::rain;
			switch (weather.kind) {
			case ac::weatherKind::rain:
				windTarget = 0.07f + weather.intensity * 0.13f;
				precipitationTarget = weather.intensity * 0.62f;
				break;
			case ac::weatherKind::storm:
				windTarget = 0.14f + weather.intensity * 0.30f;
				precipitationTarget = weather.intensity * 0.86f;
				break;
			case ac::weatherKind::snow:
				windTarget = 0.10f + weather.intensity * 0.23f;
				precipitationTarget = weather.intensity * 0.25f;
				precipitationSound = ac::weatherSound::snow;
				break;
			default:
				break;
			}
			windTarget *= shelter;
			precipitationTarget *= shelter;

			const float blend = 1.0f - std::exp(-std::clamp(deltaTime, 0.0f, 0.1f) * 3.2f);
			_weatherWindGain += (windTarget - _weatherWindGain) * blend;
			_weatherPrecipitationGain +=
				(precipitationTarget - _weatherPrecipitationGain) * blend;

			if (!_weatherWindVoice && _weatherWindGain > 0.002f)
				_weatherWindVoice = _audio.playWeatherLoop(ac::weatherSound::wind, _weatherWindGain);
			else if (_weatherWindVoice &&
				!_audio.setVoiceVolume(_weatherWindVoice, _weatherWindGain))
				_weatherWindVoice = {};

			if (_weatherPrecipitationVoice &&
				precipitationSound != _weatherPrecipitationSound) {
				_audio.stopVoice(_weatherPrecipitationVoice);
				_weatherPrecipitationGain = precipitationTarget;
			}
			_weatherPrecipitationSound = precipitationSound;
			if (!_weatherPrecipitationVoice && _weatherPrecipitationGain > 0.002f) {
				_weatherPrecipitationVoice = _audio.playWeatherLoop(
					_weatherPrecipitationSound, _weatherPrecipitationGain);
			}
			else if (_weatherPrecipitationVoice &&
				!_audio.setVoiceVolume(
					_weatherPrecipitationVoice, _weatherPrecipitationGain)) {
				_weatherPrecipitationVoice = {};
			}
			if (_weatherPrecipitationVoice && precipitationTarget <= 0.0f &&
				_weatherPrecipitationGain < 0.002f) {
				_audio.stopVoice(_weatherPrecipitationVoice);
				_weatherPrecipitationGain = 0.0f;
			}

			if (weather.lightning > 0.55f && _previousWeatherLightning <= 0.55f) {
				const float angle = _weather.clock() * 1.731f;
				const ac::soundPos thunder = ac::soundPos::at(
					eye.x + std::cos(angle) * 22.0f,
					eye.y + 18.0f,
					eye.z + std::sin(angle) * 22.0f,
					112.0f);
				_audio.playWeatherThunder(thunder, _cameraUnderwater ? 0.24f : openToSky ? 1.0f : 0.48f);
			}
			_previousWeatherLightning = weather.lightning;
		}

		void updateWeather(float deltaTime) {
			_weather.setBiomeConfig(_world.terrainBiomes());
			ac::TERRAIN_BIOME biome = _world.terrainBiomes().id("plains");
			float temperature = 0.0f;
			float surfaceHeight = 72.0f;
			const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
			if (const ac::terrainGenerator* generator = _world.generator()) {
				const int32_t worldX = static_cast<int32_t>(std::floor(eye.x));
				const int32_t worldZ = static_cast<int32_t>(std::floor(eye.z));
				const ac::biomeSample sample = generator->sampleBiome(worldX, worldZ);
				biome = sample._biome;
				temperature = sample._temperature;
			}
			surfaceHeight = eye.y;
			_weather.update(deltaTime, biome, temperature, surfaceHeight, eye.x, eye.z);
			const ac::weatherState weather = _weather.state();
			updateWeatherAudio(deltaTime, weather, eye);
			const bool liquidPrecipitation =
				weather.kind == ac::weatherKind::rain || weather.kind == ac::weatherKind::storm;
			const float localRain = liquidPrecipitation ? weather.intensity : 0.0f;
			if (localRain > 0.035f)
				_weatherWetness = std::min(1.0f,
					_weatherWetness + deltaTime * (0.025f + localRain * 0.16f));
			else {
				// Pools linger beneath a retreating cloud, then evaporate more quickly
				// as the sky clears. Snow does not replenish liquid surface water.
				const float evaporation = 0.008f + (1.0f - weather.overcast) * 0.012f;
				_weatherWetness = std::max(0.0f, _weatherWetness - deltaTime * evaporation);
			}
			_postSettings.weatherFog = 1.0f + weather.overcast * 2.15f;
			_postSettings.weatherExposure = 1.0f - weather.overcast * 0.26f + weather.lightning * 0.38f;
			_postSettings.weatherWetness = _weatherWetness;
			if (_weatherWetness > 0.001f) {
				_weatherSurfaceRefresh -= deltaTime;
				// Move the larger map in eight-block increments. This avoids rebuilding
				// 16k exposed-surface samples for every single block the player crosses.
				constexpr int32_t weatherOriginStep = 8;
				const int32_t snappedX = static_cast<int32_t>(std::floor(eye.x / weatherOriginStep)) * weatherOriginStep;
				const int32_t snappedZ = static_cast<int32_t>(std::floor(eye.z / weatherOriginStep)) * weatherOriginStep;
				const int32_t originX = snappedX -
					static_cast<int32_t>(WEATHER_SURFACE_SIZE / 2u);
				const int32_t originZ = snappedZ -
					static_cast<int32_t>(WEATHER_SURFACE_SIZE / 2u);
				if (_weatherSurfaceRefresh <= 0.0f || originX != _weatherSurfaceOrigin.x ||
					originZ != _weatherSurfaceOrigin.y) {
					_weatherSurfaceOrigin = { originX, originZ };
					for (uint32_t z = 0; z < WEATHER_SURFACE_SIZE; ++z) {
						for (uint32_t x = 0; x < WEATHER_SURFACE_SIZE; ++x) {
							int32_t top = INT32_MIN;
							const int32_t worldX = originX + static_cast<int32_t>(x);
							const int32_t worldZ = originZ + static_cast<int32_t>(z);
							for (int32_t y = CHUNK_HEIGHT - 1; y >= 0; --y) {
								const ac::blockId state = _streamer.blockAt(worldX, y, worldZ);
								if (state == 0u) continue;
								const ac::blockDefinition* definition = _blocks.get(ac::blockType(state));
								if (!definition || (!definition->_solid && !definition->_occludes)) continue;
								top = y + 1;
								break;
							}
							_weatherSurfaceData[z * WEATHER_SURFACE_SIZE + x] = top;
						}
					}
					_weatherSurfaces.update(
						_context, _weatherSurfaceData.data(), WEATHER_SURFACE_CELLS);
					_weatherSurfaceRefresh = 1.25f;
				}
				_postSettings.weatherSurfaceOrigin = {
					static_cast<float>(_weatherSurfaceOrigin.x),
					static_cast<float>(_weatherSurfaceOrigin.y)
				};
				_postSettings.weatherSurfaceSize = {
					static_cast<float>(WEATHER_SURFACE_SIZE),
					static_cast<float>(WEATHER_SURFACE_SIZE)
				};
			}
			else {
				_postSettings.weatherSurfaceSize = {};
			}
			if (weather.intensity > 0.05f && !_cameraUnderwater) {
				_particles.emitPrecipitation(
					eye,
					weather.kind == ac::weatherKind::snow,
					weather.kind == ac::weatherKind::storm,
					weather.intensity,
					deltaTime,
					_weather.clock(),
					[this, eye](float px, float py, float pz, float& collisionY) {
						// Precipitation belongs to the cloud above this exact column,
						// rather than to one global weather switch around the player.
						if (_weather.precipitationAt(px, pz) <= 0.045f)
							return false;
						const int32_t x = static_cast<int32_t>(std::floor(px));
						const int32_t z = static_cast<int32_t>(std::floor(pz));
						const int32_t spawnY = std::clamp(
							static_cast<int32_t>(std::floor(py)), 0, CHUNK_HEIGHT - 1);
						auto blocksWeather = [this, x, z](int32_t y) {
							const ac::blockId state = _streamer.blockAt(x, y, z);
							if (state == 0u) return false;
							const ac::blockDefinition* definition = _blocks.get(ac::blockType(state));
							return definition && (definition->_solid || definition->_occludes);
						};
						// A particle may only be born in a column open to the sky. This
						// prevents rain and snow appearing inside tall caves or buildings.
						for (int32_t y = spawnY; y < CHUNK_HEIGHT; ++y)
							if (blocksWeather(y)) return false;
						collisionY = -(std::numeric_limits<float>::max)();
						const int32_t lowerY = std::max(0,
							static_cast<int32_t>(std::floor(eye.y)) - 14);
						for (int32_t y = spawnY - 1; y >= lowerY; --y) {
							if (!blocksWeather(y)) continue;
							collisionY = static_cast<float>(y + 1);
							break;
						}
						return true;
					});
			}
		}

		std::vector<uint8_t> _blockOcclusionLookup = std::vector<uint8_t>(256, 2u);
		bool _cursorCaptured = false;
		// F5 cycles: first person, rear shoulder, front-facing third person.
		uint8_t _cameraMode = 0u;
		static constexpr uint64_t PLAYER_SAVE_ID = 1u;


		bool isSpectator() const { return _gameMode == ac::gameMode::spectator; }
		bool isCreative() const { return _gameMode == ac::gameMode::creative; }
		bool isSurvival() const { return _gameMode == ac::gameMode::survival; }

		void syncSpectatorFlag() {
			_spectatorMode = isSpectator();
		}

		const char* gameModeName(ac::gameMode mode) const {
			switch (mode) {
			case ac::gameMode::creative: return "creative";
			case ac::gameMode::spectator: return "spectator";
			default: return "survival";
			}
		}

		void setGameMode(ac::gameMode mode, bool announce = true) {
			if (mode != ac::gameMode::spectator)
				_previousGameMode = mode;
			_gameMode = mode;
			syncSpectatorFlag();
			_player.clearVelocity();
			if (isSpectator()) _cameraMode = 0u;
			_bodycamInitialized = false;
			_breakActive = false;
			_breakProgress = 0.0f;
			_toolPickupRemaining = 0.0f;
			_toolPickupArmed = false;
			if (announce)
				pushConsoleMessage(std::string("gamemode ") + gameModeName(mode));
		}

		void toggleFlyMode() {
			if (isSpectator())
				setGameMode(_previousGameMode == ac::gameMode::spectator
					? ac::gameMode::survival : _previousGameMode);
			else {
				_previousGameMode = _gameMode;
				setGameMode(ac::gameMode::spectator);
			}
		}

		void cancelEat() {
			_eating = false;
			_eatTime = 0.0f;
		}

		void cancelBreak() {
			_breakActive = false;
			_breakProgress = 0.0f;
			_breakSecondsNeeded = 0.0f;
		}

		void addExhaustion(float amount) {
			if (!isSurvival() || amount <= 0.0f) return;
			_exhaustion += amount;
			while (_exhaustion >= ac::PLAYER_EXHAUSTION_UNIT) {
				_exhaustion -= ac::PLAYER_EXHAUSTION_UNIT;
				if (_saturation > 0.0f)
					_saturation = (std::max)(0.0f, _saturation - 1.0f);
				else
					_hunger = (std::max)(0.0f, _hunger - 1.0f);
			}
		}

		const ac::blockDefinition* selectedItemDef() const {
			return _blocks.get(ac::blockType(_selectedBlock));
		}

		float heldEntityDamage() const {
			const ac::blockDefinition* held = selectedItemDef();
			if (!held || _hotbarCounts[_selectedHotbarSlot] == 0) return 1.0f;
			const std::string& name = held->_name;
			if (name == "wooden_sword" || name == "golden_sword") return 4.0f;
			if (name == "stone_sword") return 5.0f;
			if (name == "iron_sword") return 6.0f;
			if (name == "diamond_sword") return 7.0f;
			if (name == "wooden_axe" || name == "golden_axe") return 7.0f;
			if (name == "stone_axe" || name == "iron_axe" || name == "diamond_axe") return 9.0f;
			if (held->isMiningTool()) return 2.0f;
			return 1.0f;
		}

		float heldEntityKnockback() const {
			const ac::blockDefinition* held = selectedItemDef();
			if (!held || _hotbarCounts[_selectedHotbarSlot] == 0) return 1.8f;
			if (held->heldStyle() == ac::heldItemStyle::sword) return 3.4f;
			if (held->heldStyle() == ac::heldItemStyle::axe) return 3.0f;
			return held->isMiningTool() ? 2.5f : 2.0f;
		}

		bool canEatHeldFood() const {
			if (!isSurvival() || _spawnPending) return false;
			const ac::blockDefinition* held = selectedItemDef();
			if (!held || !held->isFood() || _hotbarCounts[_selectedHotbarSlot] == 0)
				return false;
			if (_hunger < ac::PLAYER_MAX_HUNGER) return true;
			return held->_name == "golden_apple";
		}

		void finishEating() {
			const ac::blockDefinition* held = selectedItemDef();
			if (!held || !held->isFood()) {
				cancelEat();
				return;
			}
			if (!consumeSelected(1)) {
				cancelEat();
				return;
			}
			_hunger = (std::min)(ac::PLAYER_MAX_HUNGER, _hunger + held->_foodHunger);
			_saturation = (std::min)(
				(std::min)(ac::PLAYER_MAX_SATURATION, _hunger),
				_saturation + held->_foodSaturation);
			if (held->_name == "golden_apple")
				_health = (std::min)(ac::PLAYER_MAX_HEALTH, _health + 4.0f);
			_dynamicRenderer.triggerUseAnimation(_playerEntity);
			cancelEat();
		}

		bool updateEating(float dt, bool holdingUse) {
			if (!holdingUse || !canEatHeldFood() || _breakActive) {
				cancelEat();
				return false;
			}
			if (!_eating) {
				_eating = true;
				_eatTime = 0.0f;
				_dynamicRenderer.triggerUseAnimation(_playerEntity);
			}
			_eatTime += dt;
			if (std::fmod(_eatTime, 0.40f) < dt)
				_dynamicRenderer.triggerUseAnimation(_playerEntity);
			if (_eatTime >= ac::PLAYER_EAT_SECONDS)
				finishEating();
			return true;
		}



		bool applyPlayerDamage(float amount, bool respectCooldown = true) {
			if (!isSurvival() || amount <= 0.0f || _spawnPending) return false;
			if (respectCooldown && _hurtCooldown > 0.0f) return false;
			_health = (std::max)(0.0f, _health - amount);
			_hurtCooldown = ac::PLAYER_HURT_COOLDOWN;
			cancelEat();
			addCameraShake(0.10f + amount * 0.02f);
			_audio.playHurt(ac::soundPos::at(
				_player.getCamera()._gpuData._position.x,
				_player.getCamera()._gpuData._position.y,
				_player.getCamera()._gpuData._position.z,
				12.0f));
			const bool survived = _health > 0.0f;
			if (!survived)
				respawnAfterDeath();
			return survived;
		}

		void respawnAfterDeath() {
			pushConsoleMessage("You died");
			_health = ac::PLAYER_MAX_HEALTH;
			_hunger = ac::PLAYER_MAX_HUNGER;
			_saturation = ac::PLAYER_MAX_SATURATION;
			_exhaustion = 0.0f;
			_air = ac::PLAYER_MAX_AIR;
			resetFallTracking();
			_hurtCooldown = 0.0f;
			_starveTimer = 0.0f;
			cancelEat();
			cancelBreak();
			_player.clearVelocity();
			_wasInWater = false;
			const std::optional<dx::XMFLOAT3> spawn = initialSpawnPoint();
			if (spawn)
				_player.teleport(*spawn);
			else
				_player.teleport({ 0.0f, 120.0f, 0.0f });
			resetSpawnSearch();
			_restoredPlayerPendingValidation = false;
			persistPlayerInventory();
		}

		void resetFallTracking() {
			_fallDistance = 0.0f;
			_fallMaxFeetY = 0.0f;
			_fallActive = false;
		}

		void updateFallTracking(float) {
			if (!isSurvival() || _spawnPending || isSpectator()) {
				resetFallTracking();
				return;
			}
			const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
			const float feetY = eye.y - _player.collisionBox().feetBelowEye;
			if (_player.inWater() || _player.isSwimming()) {
				resetFallTracking();
				return;
			}
			if (!_player.isGrounded()) {
				if (!_fallActive) {
					_fallActive = true;
					_fallMaxFeetY = feetY;
				}
				else if (feetY > _fallMaxFeetY) {
					_fallMaxFeetY = feetY;
				}
				_fallDistance = (std::max)(0.0f, _fallMaxFeetY - feetY);
				return;
			}
			if (_fallActive && _player.justLanded()) {
				const float fallen = (std::max)(_fallDistance, _fallMaxFeetY - feetY);
				const float damage = ac::fallDamageFromDistance(fallen);
				if (damage > 0.0f) {
					applyPlayerDamage(damage, false);
					_audio.playFall(damage >= 4.0f, ac::soundPos::at(eye.x, feetY, eye.z, 14.0f));
				}
			}
			resetFallTracking();
		}

		void applyWaterCurrent(float dt) {
			if (isSpectator() || _spawnPending || dt <= 0.0f) return;
			if (_player.isFlying()) return;
			if (!_player.inWater()) return;
			const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
			const ac::playerCollisionBox box = _player.collisionBox();
			const ac::fluidFlow flow = ac::waterFlowAt(
				static_cast<int32_t>(std::floor(eye.x)),
				static_cast<int32_t>(std::floor(eye.y - box.feetBelowEye * 0.4f)),
				static_cast<int32_t>(std::floor(eye.z)),
				[this](int32_t x, int32_t y, int32_t z) {
					return _streamer.blockAt(x, y, z, _blockReadCache);
				});
			const float horizontal = _player.isSwimming() ? 1.6f : 2.7f;
			_player.addVelocity({
				flow.x * horizontal * dt,
				flow.y * 1.3f * dt,
				flow.z * horizontal * dt
			});
		}

		bool fallingBlockHitsPlayer(const ac::fallingBlockEntity& entity) const {
			return _player.overlapsBox(
				static_cast<float>(entity.x),
				entity.y,
				static_cast<float>(entity.z),
				static_cast<float>(entity.x + 1),
				entity.y + 1.0f,
				static_cast<float>(entity.z + 1));
		}

		float fallingBlockHurtAmount(const ac::fallingBlockEntity& entity) const {
			const float fallen = (std::max)(0.0f, entity.originY - entity.y);
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(entity.id));
			const std::string name = definition ? definition->_name : std::string{};
			const bool heavy = name.find("anvil") != std::string::npos ||
				name.find("dripstone") != std::string::npos;
			return ac::fallingBlockDamage(fallen, heavy);
		}

		void hurtFromFallingBlock(ac::fallingBlockEntity& entity, bool landingHit = false) {
			if (entity.hurtPlayer) return;
			if (!landingHit && entity.vy >= 0.0f) return;
			if (!landingHit && !fallingBlockHitsPlayer(entity)) return;
			const float damage = fallingBlockHurtAmount(entity);
			if (damage <= 0.0f) return;
			entity.hurtPlayer = true;
			applyPlayerDamage(damage);
		}

		void updateFallingBlockDamage() {
			if (!isSurvival() || _spawnPending || isSpectator()) return;
			for (ac::fallingBlockEntity& entity : _streamer.fallingEntities())
				hurtFromFallingBlock(entity);
		}

		void updateVitals(float dt) {
			if (_hurtCooldown > 0.0f)
				_hurtCooldown = (std::max)(0.0f, _hurtCooldown - dt);
			if (!isSurvival() || _spawnPending || dt <= 0.0f)
				return;

			if (_player.isSubmerged()) {
				_air = (std::max)(0.0f, _air - dt);
				if (_air <= 0.0f) {
					_drownTimer += dt;
					if (_drownTimer >= 1.0f) {
						applyPlayerDamage(1.0f);
						_drownTimer -= 1.0f;
					}
				}
			}
			else {
				_air = (std::min)(ac::PLAYER_MAX_AIR, _air + dt * 4.0f);
				_drownTimer = 0.0f;
			}

			const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
			const ac::blockId footing = footingBlockAt(eye);
			if (ac::blockType(footing) == BLOCK_MAGMA && _player.isGrounded()) {
				_magmaTimer += dt;
				if (_magmaTimer >= 1.0f) {
					applyPlayerDamage(1.0f);
					_magmaTimer -= 1.0f;
				}
			}
			else {
				_magmaTimer = 0.0f;
			}

			if (_player.isSprinting()) {
				const dx::XMFLOAT3 vel = _player.getVelocity();
				const float speed = std::sqrt(vel.x * vel.x + vel.z * vel.z);
				addExhaustion(0.10f * speed * dt);
			}
			if (_player.isSwimming()) {
				const dx::XMFLOAT3 vel = _player.getVelocity();
				const float speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
				addExhaustion(0.01f * speed * dt);
			}
			const bool grounded = _player.isGrounded();
			if (_wasGrounded && !grounded && _player.getVelocity().y > 1.0f)
				addExhaustion(_player.isSprinting() ? 0.20f : 0.05f);
			_wasGrounded = grounded;

			if (_hunger >= 18.0f && _health < ac::PLAYER_MAX_HEALTH) {
				_healthRegenTimer += dt;
				const float interval = (_hunger >= ac::PLAYER_MAX_HUNGER && _saturation > 0.0f)
					? ac::PLAYER_REGEN_SATURATED : ac::PLAYER_REGEN_HUNGRY;
				if (_healthRegenTimer >= interval) {
					_health = (std::min)(ac::PLAYER_MAX_HEALTH, _health + 1.0f);
					addExhaustion(6.0f);
					_healthRegenTimer = 0.0f;
				}
			}
			else {
				_healthRegenTimer = 0.0f;
			}

			if (_hunger <= 0.0f && _health > 0.0f) {
				_starveTimer += dt;
				if (_starveTimer >= 4.0f) {
					applyPlayerDamage(1.0f);
					_starveTimer = 0.0f;
				}
			}
			else {
				_starveTimer = 0.0f;
			}
		}

		void dumpCraftGrid(ac::blockId* blocks, uint32_t* counts, int n) {
			for (int i = 0; i < n; ++i) {
				if (blocks[i] != 0 && counts[i] > 0) {
					const uint32_t stored = addToHotbar(blocks[i], counts[i]);
					if (stored < counts[i]) {
						const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
						spawnItemDrop(blocks[i], counts[i] - stored,
							eye.x, eye.y - 0.4f, eye.z, 0.0f, 1.0f, 0.0f);
					}
				}
				blocks[i] = 0;
				counts[i] = 0;
			}
		}

		ac::blockId craftResultId(const ac::blockId* grid, int width, int height, uint32_t& outCount) const {
			outCount = 0;
			const ac::craftMatch recipe = _recipes.match(grid, width, height);
			if (!recipe) return 0;
			outCount = recipe.resultCount;
			return recipe.result;
		}

		bool craftOnce(ac::blockId* blocks, uint32_t* counts, int width, int height) {
			const int n = width * height;
			std::vector<ac::blockId> grid(static_cast<size_t>(n), 0);
			for (int i = 0; i < n; ++i)
				grid[static_cast<size_t>(i)] = counts[i] > 0 ? blocks[i] : 0;
			const ac::craftMatch recipe = _recipes.match(grid.data(), width, height);
			if (!recipe) return false;
			if (recipe.resultCount > itemStackLimit(recipe.result)) return false;
			if (_cursorItem != 0 && _cursorItem != recipe.result) return false;
			if (_cursorItem == recipe.result &&
				_cursorCount + recipe.resultCount > itemStackLimit(recipe.result))
				return false;
			for (int i = 0; i < n; ++i) {
				if (grid[static_cast<size_t>(i)] == 0) continue;
				if (counts[i] == 0) return false;
				--counts[i];
				if (counts[i] == 0) blocks[i] = 0;
			}
			if (_cursorItem == 0) {
				_cursorItem = recipe.result;
				_cursorCount = recipe.resultCount;
			}
			else {
				_cursorCount += recipe.resultCount;
			}
			playUiClick();
			return true;
		}

		void craftSlotClick(ac::blockId& id, uint32_t& count, bool right) {
			if (right) {
				if (_cursorItem == 0) {
					if (id == 0 || count == 0) return;
					const uint32_t half = (count + 1u) / 2u;
					_cursorItem = id;
					_cursorCount = half;
					count -= half;
					if (count == 0) id = 0;
				}
				else {
					if (id != 0 && id != _cursorItem) return;
					if (id == 0) { id = _cursorItem; count = 0; }
					if (count >= itemStackLimit(_cursorItem)) return;
					++count;
					--_cursorCount;
					if (_cursorCount == 0) _cursorItem = 0;
				}
				playUiClick();
				return;
			}
			if (_cursorItem == 0) {
				if (id == 0) return;
				_cursorItem = id;
				_cursorCount = count;
				id = 0;
				count = 0;
			}
			else if (id != 0 && id != _cursorItem) {
				std::swap(id, _cursorItem);
				std::swap(count, _cursorCount);
			}
			else {
				if (id == 0) { id = _cursorItem; count = 0; }
				const uint32_t space = itemStackLimit(_cursorItem) > count
					? itemStackLimit(_cursorItem) - count : 0u;
				const uint32_t move = (std::min)(space, _cursorCount);
				count += move;
				_cursorCount -= move;
				if (_cursorCount == 0) _cursorItem = 0;
			}
			playUiClick();
		}




		void finishBlockBreak(const dx::XMINT3& block, ac::blockId brokenId) {
			const ac::blockId brokenType = ac::blockType(brokenId);
			emitBlockBurst(block, brokenId, 28, 4.2f);
			_audio.playDig(materialForBlock(brokenId),
				ac::soundPos::block(block.x, block.y, block.z, 24.0f));
			addCameraShake(0.08f);
			const ac::blockDefinition* brokenDef = _blocks.get(brokenType);
			const ac::blockId held = _selectedBlock;
			const bool creative = isCreative();
			addExhaustion(0.005f);
			_server.schedule([this, block, brokenId, brokenType, held, creative, brokenDef]() {
				auto dropAt = [this, block](ac::blockId dropId, uint32_t count) {
					if (dropId == 0 || count == 0 || dropId == ac::WATER_BLOCK_TYPE) return;
					spawnItemDrop(
						dropId, count,
						static_cast<float>(block.x) + 0.5f,
						static_cast<float>(block.y) + 0.35f,
						static_cast<float>(block.z) + 0.5f,
						((static_cast<float>(block.x) * 0.37f) - std::floor(block.x * 0.37f)) * 0.6f - 0.3f,
						0.35f,
						((static_cast<float>(block.z) * 0.53f) - std::floor(block.z * 0.53f)) * 0.6f - 0.3f,
						0.15f);
				};
				auto maybeDrop = [&](ac::blockId dropId, uint32_t count = 1u) {
					if (creative) return;
					if (ac::canHarvestDrop(brokenDef, _blocks.get(ac::blockType(held)), creative))
						dropAt(dropId, count);
				};
				if (isDoorType(brokenType)) {
					const int32_t bottomY = (ac::isUpperHalf(brokenId) || isDoorTopType(brokenId))
						? block.y - 1 : block.y;
					const int32_t topY = bottomY + 1;
					_streamer.setBlock(block.x, bottomY, block.z, 0, _device, _context);
					_streamer.setBlock(block.x, topY, block.z, 0, _device, _context);
					_blockEntities.removeAt(block.x, bottomY, block.z);
					_blockEntities.removeAt(block.x, topY, block.z);
					_streamer.updateWaterPhysics(_device, _context, 24u);
					const ac::blockId dropId = brokenDef && brokenDef->_behavior.dropSpecified
						? brokenDef->_behavior.drop : doorPlaceBaseId(brokenType);
					const uint32_t dropCount = brokenDef && brokenDef->_behavior.dropSpecified
						? brokenDef->_behavior.dropCount : 1u;
					maybeDrop(dropId, dropCount);
					return;
				}
				if (_streamer.setBlock(block.x, block.y, block.z, 0, _device, _context)) {
					_blockEntities.removeAt(block.x, block.y, block.z);
					_streamer.updateWaterPhysics(_device, _context, 24u);
					const bool specified = brokenDef && brokenDef->_behavior.dropSpecified;
					const ac::blockId dropId = specified
						? brokenDef->_behavior.drop
						: ac::blockType(ac::normalizeBlockState(brokenType));
					const uint32_t dropCount = specified ? brokenDef->_behavior.dropCount : 1u;
					maybeDrop(dropId, dropCount);
				}
			});
		}

		ac::serverInput captureServerInput() const {
			ac::serverInput input;
			if (diagnosticFrameCaptureEnabled()) return input;
			if (ac::gameScreenBlocksWorldInput(_screen) || _spawnPending || !_cursorCaptured)
				return input;

			input.lookX = static_cast<float>(_window.getDeltaCursorX());
			input.lookY = static_cast<float>(_window.getDeltaCursorY());
			input.forward = _window.getKey(GLFW_KEY_W);
			input.backward = _window.getKey(GLFW_KEY_S);
			input.left = _window.getKey(GLFW_KEY_A);
			input.right = _window.getKey(GLFW_KEY_D);
			input.jump = _window.getKey(GLFW_KEY_SPACE);
			input.crouch = _window.getKey(GLFW_KEY_LEFT_SHIFT);
			input.sprint = _window.getKey(GLFW_KEY_LEFT_CONTROL) &&
				(!isSurvival() || _hunger > 6.0f);
			return input;
		}

		void capturePlayerState() {
			if (!_worldSessionOpen || _spawnPending) return;
			ac::savedEntity state;
			state.id = PLAYER_SAVE_ID;
			state.kind = ac::savedEntityKind::player;
			state.position = _player.getCamera()._gpuData._position;
			state.velocity = _player.getVelocity();
			state.yaw = _player.getYaw();
			state.pitch = _player.getPitch();
			state.flags = (_player.isGrounded() ? 1u : 0u)
				| (isSpectator() ? 2u : 0u)
				| (isCreative() ? 16u : 0u)
				| (_player.isFlying() ? 32u : 0u);
			state.variant = _cameraMode;
			_world.upsertEntity(state);
			persistPlayerInventory();
			_items.saveToWorld(_world);
		}

		bool restorePlayerState() {
			const std::optional<ac::savedEntity> state = _world.entity(PLAYER_SAVE_ID);
			if (!state || state->kind != ac::savedEntityKind::player) return false;
			const auto finite3 = [](const dx::XMFLOAT3& value) {
				return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
			};
			if (!finite3(state->position) || !finite3(state->velocity) ||
				!std::isfinite(state->yaw) || !std::isfinite(state->pitch) ||
				std::abs(state->position.x) > 10'000'000.0f ||
				std::abs(state->position.y) > 10'000'000.0f ||
				std::abs(state->position.z) > 10'000'000.0f) return false;

			if ((state->flags & 2u) != 0u)
				_gameMode = ac::gameMode::spectator;
			else if ((state->flags & 16u) != 0u)
				_gameMode = ac::gameMode::creative;
			else
				_gameMode = ac::gameMode::survival;
			if (_gameMode != ac::gameMode::spectator)
				_previousGameMode = _gameMode;
			syncSpectatorFlag();
			_cameraMode = state->variant <= 2u ? static_cast<uint8_t>(state->variant) : 0u;
			_player.restoreState(state->position, state->velocity, state->yaw, state->pitch,
				(state->flags & 1u) != 0u,
				isCreative() && (state->flags & 32u) != 0u);
			_restoredPlayerPendingValidation = !_spectatorMode;
			_spawnPending = !_spectatorMode;
			if (_spawnPending) resetSpawnSearch();
			loadAndApplyPlayerInventory();
			return true;
		}

		bool solidBlock(int32_t x, int32_t y, int32_t z) const {
			if (y < 0) return true;
			const ac::blockId id = _streamer.blockAt(x, y, z);
			if (id == 0u) return false;
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
			return definition && definition->_solid;
		}

		bool isFullCubeBlock(int32_t x, int32_t y, int32_t z) const {
			if (y < 0) return false;
			const ac::blockId id = _streamer.blockAt(x, y, z);
			if (id == 0u) return false;
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
			return definition &&
				definition->_solid &&
				definition->_occludes &&
				definition->_model == ac::MODEL_CUBE;
		}

		bool isWaterBlock(int32_t x, int32_t y, int32_t z) const {
			return ac::blockType(_streamer.blockAt(x, y, z)) == ac::WATER_BLOCK_TYPE;
		}

		ac::stairNeighborInfo stairNeighborAt(int32_t x, int32_t y, int32_t z) const {
			ac::stairNeighborInfo info{};
			const ac::blockId id = _streamer.blockAt(x, y, z);
			if (id == 0u) return info;
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
			if (!definition || definition->_model != ac::MODEL_STAIRS) return info;
			info.isStairs = true;
			info.facing = ac::blockFacing(id);
			return info;
		}

		ac::stairShape stairShapeAt(int32_t x, int32_t y, int32_t z, ac::blockId state) const {
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(state));
			if (!definition || definition->_model != ac::MODEL_STAIRS)
				return ac::stairShape::straight;
			const ac::stairNeighborInfo neighbors[4] = {
				stairNeighborAt(x, y, z + 1),
				stairNeighborAt(x + 1, y, z),
				stairNeighborAt(x, y, z - 1),
				stairNeighborAt(x - 1, y, z)
			};
			return ac::resolveStairShape(ac::blockFacing(state), neighbors);
		}

		bool fenceConnectsNeighborAt(int32_t x, int32_t y, int32_t z) const {
			const ac::blockId id = _streamer.blockAt(x, y, z);
			if (id == 0u) return false;
			return ac::fenceConnectsTo(_blocks.get(ac::blockType(id)));
		}

		bool paneConnectsNeighborAt(int32_t x, int32_t y, int32_t z) const {
			const ac::blockId id = _streamer.blockAt(x, y, z);
			if (id == 0u) return false;
			return ac::paneConnectsTo(_blocks.get(ac::blockType(id)));
		}

		ac::fenceConnections fenceConnectionsAt(int32_t x, int32_t y, int32_t z, ac::blockId state) const {
			ac::fenceConnections links{};
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(state));
			if (!definition) return links;
			if (definition->_model == ac::MODEL_FENCE) {
				links.north = fenceConnectsNeighborAt(x, y, z - 1);
				links.west = fenceConnectsNeighborAt(x - 1, y, z);
				links.south = fenceConnectsNeighborAt(x, y, z + 1);
				links.east = fenceConnectsNeighborAt(x + 1, y, z);
			}
			else if (definition->_model == ac::MODEL_PANE) {
				links.north = paneConnectsNeighborAt(x, y, z - 1);
				links.west = paneConnectsNeighborAt(x - 1, y, z);
				links.south = paneConnectsNeighborAt(x, y, z + 1);
				links.east = paneConnectsNeighborAt(x + 1, y, z);
			}
			return links;
		}

		size_t collisionBoxesAt(
			int32_t x, int32_t y, int32_t z,
			ac::blockId state,
			ac::blockAABB* boxes,
			size_t capacity
		) const {
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(state));
			if (!definition) return 0;
			return ac::collisionBoxesFor(
				definition, state, boxes, capacity,
				definition->_model == ac::MODEL_STAIRS ? stairShapeAt(x, y, z, state) : ac::stairShape::straight,
				(definition->_model == ac::MODEL_FENCE || definition->_model == ac::MODEL_PANE)
					? fenceConnectionsAt(x, y, z, state)
					: ac::fenceConnections{});
		}

		// Selection can hit non-solid interactive shapes (open doors) that players
		// walk through. Fluids stay unselectable so targeting continues behind them.
		size_t selectionBoxesAt(
			int32_t x, int32_t y, int32_t z,
			ac::blockId state,
			ac::blockAABB* boxes,
			size_t capacity
		) const {
			const ac::blockId type = ac::blockType(state);
			if (type == 0 || type == ac::WATER_BLOCK_TYPE) return 0;
			const ac::blockDefinition* definition = _blocks.get(type);
			if (!definition || !boxes || capacity == 0) return 0;
			if (definition->_solid)
				return collisionBoxesAt(x, y, z, state, boxes, capacity);
			if (ac::isRedstoneWire(type)) {
				boxes[0] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.125f, 1.0f };
				return 1;
			}
			if (ac::isRedstoneTorch(type) || definition->_model == ac::MODEL_TORCH) {
				boxes[0] = ac::torchSelectionBox(state);
				return 1;
			}
			if (ac::isDiode(type)) {
				boxes[0] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.125f, 1.0f };
				return 1;
			}
			if (ac::isLever(type)) {
				boxes[0] = ac::leverSelectionBox(state);
				return 1;
			}
			if (definition && definition->_model == ac::MODEL_CROSS) {
				boxes[0] = { 0.15f, 0.0f, 0.15f, 0.85f, 0.90f, 0.85f };
				return 1;
			}
			if (definition && definition->_model == ac::MODEL_THIN) {
				boxes[0] = { 0.0f, 0.0f, 0.0f, 1.0f, ac::THIN_HEIGHT, 1.0f };
				return 1;
			}
			if (!ac::isDetailModel(definition->_model))
				return 0;
			uint32_t selectionModel = definition->_model;
			if (selectionModel == ac::MODEL_DOOR || selectionModel == ac::MODEL_DOOR_OPEN)
				selectionModel = ac::isBlockOpen(state) ? ac::MODEL_DOOR_OPEN : ac::MODEL_DOOR;
			else if (selectionModel == ac::MODEL_TRAPDOOR)
				selectionModel = ac::isBlockOpen(state) ? ac::MODEL_TRAPDOOR_OPEN : ac::MODEL_TRAPDOOR;
			else if (selectionModel == ac::MODEL_FENCE_GATE)
				selectionModel = ac::isBlockOpen(state) ? ac::MODEL_FENCE_GATE_OPEN : ac::MODEL_FENCE_GATE;
			return ac::collisionBoxesForModel(
				selectionModel,
				ac::blockFacing(state),
				boxes,
				capacity,
				stairShapeAt(x, y, z, state),
				fenceConnectionsAt(x, y, z, state));
		}

		size_t solidItemBoxesAt(
			int32_t x, int32_t y, int32_t z,
			ac::blockAABB* boxes,
			size_t capacity
		) const {
			if (y < 0) {
				if (!boxes || capacity == 0) return 0;
				boxes[0] = { 0, 0, 0, 1, 1, 1 };
				return 1;
			}
			const ac::blockId id = _streamer.blockAt(x, y, z);
			if (id == 0u) return 0;
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
			if (!definition) return 0;
			return collisionBoxesAt(x, y, z, id, boxes, capacity);
		}

		bool isDoorType(ac::blockId type) const {
			return isDoorDef(_blocks.get(ac::blockType(type)));
		}
		bool isDoorTopType(ac::blockId type) const {
			if (ac::isUpperHalf(type)) return true;
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(type));
			return definition && isDoorTopName(definition->_name);
		}
		bool isDoorOpenType(ac::blockId type) const {
			if (ac::isBlockOpen(type)) return true;
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(type));
			return definition && isDoorOpenName(definition->_name);
		}
		ac::blockId resolveDoorNamed(const std::string& name) const {
			try {
				return static_cast<ac::blockId>(_blocks.getId(name));
			}
			catch (...) {
				return 0;
			}
		}
		ac::blockId doorPlaceBaseId(ac::blockId type) const {
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(type));
			if (!definition) return type;
			std::string name = doorBottomName(definition->_name);
			if (isDoorOpenName(name))
				name = name.substr(0, name.size() - 5);
			const ac::blockId resolved = resolveDoorNamed(name);
			return resolved != 0 ? resolved : type;
		}
		ac::blockId doorToggleId(ac::blockId type) const {
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(type));
			if (!definition) return type;
			const ac::blockId resolved = resolveDoorNamed(doorToggleName(definition->_name));
			return resolved != 0 ? resolved : type;
		}
		ac::blockId doorBottomId(ac::blockId type) const {
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(type));
			if (!definition) return type;
			const ac::blockId resolved = resolveDoorNamed(doorBottomName(definition->_name));
			return resolved != 0 ? resolved : type;
		}
		ac::blockId doorTopId(ac::blockId type) const {
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(type));
			if (!definition) return type;
			const ac::blockId resolved = resolveDoorNamed(doorTopName(definition->_name));
			return resolved != 0 ? resolved : type;
		}

		bool isInternalItemVariant(ac::blockId id) const {
			id = ac::blockType(id);
			if (id == 0u || ac::isBlockStateAlias(id)) return true;
			const ac::blockDefinition* candidate = _blocks.get(id);
			if (!candidate) return true;
			if (candidate->_name.ends_with("_lit") && candidate->_behavior.dropSpecified &&
				candidate->_behavior.drop != id) return true;
			for (uint32_t ownerId : _blocks.ids()) {
				if (ownerId == id) continue;
				const ac::blockDefinition* owner = _blocks.get(ownerId);
				if (!owner || owner->_name.size() >= candidate->_name.size()) continue;
				if (candidate->_name.compare(0, owner->_name.size(), owner->_name) != 0 ||
					candidate->_name[owner->_name.size()] != '_')
					continue;
				std::array<std::string, ac::BLOCK_FACE_COUNT> unique{};
				size_t uniqueCount = 0u;
				for (uint32_t face = 0; face < ac::BLOCK_FACE_COUNT; ++face) {
					const std::string& texture = owner->textureForFace(face);
					bool seen = false;
					for (size_t i = 0; i < uniqueCount; ++i)
						seen = seen || unique[i] == texture;
					if (!seen) unique[uniqueCount++] = texture;
				}
				if (uniqueCount > 1u) return true;
			}
			return false;
		}

		void setHotbarSlot(int slot, ac::blockId id, uint32_t count) {
			if (slot < 0 || slot >= HUD_HOTBAR_COUNT) return;
			if (id == 0 || count == 0) {
				_hotbarBlocks[slot] = 0;
				_hotbarCounts[slot] = 0;
			}
			else {
				_hotbarBlocks[slot] = id;
				_hotbarCounts[slot] = (std::min)(count, itemStackLimit(id));
			}
			if (slot == _selectedHotbarSlot)
				_selectedBlock = _hotbarBlocks[slot];
		}

		uint32_t addToHotbar(ac::blockId id, uint32_t count) {
			if (id == 0 || count == 0) return 0;
			const uint32_t limit = itemStackLimit(id);
			uint32_t remaining = count;
			auto tryHotbar = [&](int slot) {
				if (remaining == 0) return;
				if (_hotbarBlocks[slot] == id && _hotbarCounts[slot] < limit) {
					const uint32_t space = limit - _hotbarCounts[slot];
					const uint32_t take = (std::min)(space, remaining);
					_hotbarCounts[slot] += take;
					remaining -= take;
				}
			};
			tryHotbar(_selectedHotbarSlot);
			for (int slot = 0; slot < HUD_HOTBAR_COUNT; ++slot)
				tryHotbar(slot);
			for (int slot = 0; slot < HUD_HOTBAR_COUNT && remaining > 0; ++slot) {
				if (_hotbarBlocks[slot] == 0) {
					const uint32_t take = (std::min)(limit, remaining);
					setHotbarSlot(slot, id, take);
					remaining -= take;
				}
			}
			for (int slot = 0; slot < HUD_INV_SLOTS && remaining > 0; ++slot) {
				if (_inventoryBlocks[slot] == id && _inventoryCounts[slot] < limit) {
					const uint32_t space = limit - _inventoryCounts[slot];
					const uint32_t take = (std::min)(space, remaining);
					_inventoryCounts[slot] += take;
					remaining -= take;
				}
			}
			for (int slot = 0; slot < HUD_INV_SLOTS && remaining > 0; ++slot) {
				if (_inventoryBlocks[slot] == 0) {
					const uint32_t take = (std::min)(limit, remaining);
					_inventoryBlocks[slot] = id;
					_inventoryCounts[slot] = take;
					remaining -= take;
				}
			}
			_selectedBlock = _hotbarBlocks[_selectedHotbarSlot];
			return count - remaining;
		}

		bool consumeSelected(uint32_t amount = 1) {
			if (_selectedBlock == 0 || _hotbarCounts[_selectedHotbarSlot] < amount)
				return false;
			_hotbarCounts[_selectedHotbarSlot] -= amount;
			if (_hotbarCounts[_selectedHotbarSlot] == 0) {
				_hotbarBlocks[_selectedHotbarSlot] = 0;
				_selectedBlock = 0;
			}
			return true;
		}

		void spawnItemDrop(
			ac::blockId id,
			uint32_t count,
			float x, float y, float z,
			float vx, float vy, float vz,
			float pickupDelay = ac::ITEM_PICKUP_DELAY
		) {
			if (isDoorType(ac::blockType(id)))
				id = doorPlaceBaseId(id);
			_items.spawn(id, count, x, y, z, vx, vy, vz, pickupDelay);
		}

		void dropSelectedItem(bool entireStack) {
			if (_screen != ac::gameScreen::playing || _spectatorMode) return;
			const ac::blockId id = _hotbarBlocks[_selectedHotbarSlot];
			const uint32_t available = _hotbarCounts[_selectedHotbarSlot];
			if (id == 0 || available == 0) return;
			const uint32_t amount = entireStack ? available : 1u;
			const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
			const dx::XMFLOAT3 look = _player.getLookDirection();
			const float speed = 4.5f;
			spawnItemDrop(
				id, amount,
				eye.x + look.x * 0.45f,
				eye.y - 0.25f,
				eye.z + look.z * 0.45f,
				look.x * speed,
				look.y * speed + 1.2f,
				look.z * speed,
				ac::ITEM_PICKUP_DELAY);
			consumeSelected(amount);
			_audio.play(ac::soundId::uiClick, 0.55f);
		}

		void updateItemEntities(float deltaTime) {
			auto sampleBlock = [this](int32_t x, int32_t y, int32_t z) {
				return _streamer.blockAt(x, y, z);
			};
			_items.update(
				deltaTime,
				[this](int32_t x, int32_t y, int32_t z, ac::blockAABB* boxes, size_t capacity) {
					return solidItemBoxesAt(x, y, z, boxes, capacity);
				},
				[this](float x, float y, float z) {
					const int32_t bx = static_cast<int32_t>(std::floor(x));
					const int32_t by = static_cast<int32_t>(std::floor(y));
					const int32_t bz = static_cast<int32_t>(std::floor(z));
					const ac::blockId state = _streamer.blockAt(bx, by, bz);
					return ac::blockType(state) == ac::WATER_BLOCK_TYPE &&
						y - static_cast<float>(by) < ac::fluidHeight(state);
				},
				[&](float x, float y, float z) {
					return ac::waterFlowAt(
						static_cast<int32_t>(std::floor(x)),
						static_cast<int32_t>(std::floor(y)),
						static_cast<int32_t>(std::floor(z)),
						sampleBlock);
				});

			if (_spectatorMode || _spawnPending) return;
			const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
			const float feetY = eye.y - 0.9f;
			_items.tryPickup(eye.x, feetY, eye.z,
				[this](ac::blockId id, uint32_t count) {
					const uint32_t taken = addToHotbar(id, count);
					if (taken > 0)
						_audio.play(ac::soundId::uiClick, 0.4f);
					return taken;
				});
		}

		ac::soundMaterial materialForBlock(ac::blockId id) const {
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
			if (!definition) return ac::soundMaterial::stone;
			return ac::soundMaterialForBlock(definition->_soundMaterial);
		}

		// Block the player is standing on (under the feet), never a thin neighbor
		// sharing the feet cell such as a fence or glass pane.
		ac::blockId footingBlockAt(const dx::XMFLOAT3& eye) const {
			const float feetBelowEye = _player.collisionBox().feetBelowEye;
			const float footY = eye.y - feetBelowEye;
			const int32_t x = static_cast<int32_t>(std::floor(eye.x));
			const int32_t z = static_cast<int32_t>(std::floor(eye.z));
			const int32_t startY = static_cast<int32_t>(std::floor(footY - 0.001f));
			for (int32_t y = startY; y >= startY - 2 && y >= 0; --y) {
				const ac::blockId id = _streamer.blockAt(x, y, z);
				if (id == 0u) continue;
				const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
				if (!definition || !definition->_solid) continue;
				ac::blockAABB boxes[8];
				const size_t count = collisionBoxesAt(x, y, z, id, boxes, 8);
				if (count == 0) continue;
				const float ox = static_cast<float>(x);
				const float oy = static_cast<float>(y);
				const float oz = static_cast<float>(z);
				for (size_t i = 0; i < count; ++i) {
					const ac::blockAABB& box = boxes[i];
					const float top = oy + box.maxY;
					if (footY < top - 0.02f || footY > top + 0.55f)
						continue;
					if (eye.x < ox + box.minX - 0.001f || eye.x > ox + box.maxX + 0.001f)
						continue;
					if (eye.z < oz + box.minZ - 0.001f || eye.z > oz + box.maxZ + 0.001f)
						continue;
					return id;
				}
			}
			const ac::blockId fallback = _streamer.blockAt(x, startY, z);
			if (fallback != 0u) return fallback;
			return startY > 0 ? _streamer.blockAt(x, startY - 1, z) : 0u;
		}

		bool playerPositionClear(const dx::XMFLOAT3& eye) const {
			const ac::playerCollisionBox box = _player.collisionBox();
			constexpr float epsilon = .002f;
			const int32_t minX = static_cast<int32_t>(std::floor(eye.x - box.radius + epsilon));
			const int32_t maxX = static_cast<int32_t>(std::floor(eye.x + box.radius - epsilon));
			const int32_t minY = static_cast<int32_t>(std::floor(eye.y - box.feetBelowEye + epsilon));
			const int32_t maxY = static_cast<int32_t>(std::floor(eye.y + box.headAboveEye - epsilon));
			const int32_t minZ = static_cast<int32_t>(std::floor(eye.z - box.radius + epsilon));
			const int32_t maxZ = static_cast<int32_t>(std::floor(eye.z + box.radius - epsilon));
			for (int32_t z = minZ; z <= maxZ; ++z)
				for (int32_t y = minY; y <= maxY; ++y)
					for (int32_t x = minX; x <= maxX; ++x) {
						if (y < 0) return false;
						const ac::blockId id = _streamer.blockAt(x, y, z);
						if (id == 0u) continue;
						const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
						if (ac::eyeCollidesWithCell(
								eye.x, eye.y, eye.z, x, y, z, definition, id,
								stairShapeAt(x, y, z, id),
								fenceConnectionsAt(x, y, z, id),
								box))
							return false;
					}
			return true;
		}

		static dx::XMINT2 spawnSearchChunk(uint32_t step) {
			int32_t x = 0;
			int32_t z = 0;
			int32_t dx = 0;
			int32_t dz = -1;
			for (uint32_t i = 0; i < step; ++i) {
				if (x == z || (x < 0 && x == -z) || (x > 0 && x == 1 - z)) {
					const int32_t turned = dx;
					dx = -dz;
					dz = turned;
				}
				x += dx;
				z += dz;
			}
			return { x, z };
		}

		void resetSpawnSearch() {
			_spawnPending = true;
			_spawnSearchStep = 0;
			_spawnLandFound = false;
			_spawnLandChunk = {};
		}

		std::optional<dx::XMFLOAT3> columnSpawnPoint(int32_t x, int32_t z) const {
			int32_t startY = CHUNK_HEIGHT - 3;
			if (const ac::terrainGenerator* generator = _world.generator()) {
				const int32_t surface = static_cast<int32_t>(
					std::floor(generator->peekSurfaceHeight(x, z)));
				startY = std::clamp(surface + 4, 4, CHUNK_HEIGHT - 3);
			}
			for (int32_t y = startY; y >= 0; --y) {
				if (!isFullCubeBlock(x, y, z)) continue;
				if (isWaterBlock(x, y + 1, z) || isWaterBlock(x, y + 2, z))
					return std::nullopt;
				if (solidBlock(x, y + 1, z) || solidBlock(x, y + 2, z))
					return std::nullopt;
				return dx::XMFLOAT3{
					static_cast<float>(x) + .5f,
					static_cast<float>(y) + 1.0f + ac::standingPlayerBox().feetBelowEye,
					static_cast<float>(z) + .5f
				};
			}
			return std::nullopt;
		}

		std::optional<dx::XMFLOAT3> initialSpawnPoint() const {
			const dx::XMFLOAT3 current = _player.getCamera()._gpuData._position;
			const int32_t chunkX = static_cast<int32_t>(std::floor(current.x / static_cast<float>(CHUNK_WIDTH)));
			const int32_t chunkZ = static_cast<int32_t>(std::floor(current.z / static_cast<float>(CHUNK_LENGTH)));
			if (!_streamer.chunkLoaded(chunkX, chunkZ))
				return std::nullopt;

			const int32_t originX = chunkX * CHUNK_WIDTH;
			const int32_t originZ = chunkZ * CHUNK_LENGTH;
			const int32_t centerX = originX + CHUNK_WIDTH / 2;
			const int32_t centerZ = originZ + CHUNK_LENGTH / 2;
			if (const std::optional<dx::XMFLOAT3> center = columnSpawnPoint(centerX, centerZ))
				return center;
			for (int32_t step = 1; step <= 8; ++step) {
				for (int32_t z = centerZ - step; z <= centerZ + step; ++z) {
					for (int32_t x = centerX - step; x <= centerX + step; ++x) {
						if (x < originX || x >= originX + CHUNK_WIDTH ||
							z < originZ || z >= originZ + CHUNK_LENGTH)
							continue;
						if (std::abs(x - centerX) != step && std::abs(z - centerZ) != step)
							continue;
						if (const std::optional<dx::XMFLOAT3> spawn = columnSpawnPoint(x, z))
							return spawn;
					}
				}
			}
			return std::nullopt;
		}

		void updateDynamicEntities() {
			const dx::XMFLOAT3 playerPosition = _player.getCamera()._gpuData._position;
			_dynamicRenderer.setHumanoidPose(
				_playerEntity,
				playerPosition,
				_player.getVelocity(),
				_player.getYaw(),
				_player.getPitch(),
				_player.isGrounded(),
				_player.isCrouching(),
				_player.isSprinting(),
				_player.isSwimming(),
				_cameraMode != 0u,
				_player.swimBlend(),
				_player.crouchBlend()
			);
			if (ac::dynamicEntity* entity = _dynamicRenderer.get(_playerEntity)) {
				entity->visible = !_spectatorMode && !_spawnPending;
				const uint32_t count = _hotbarCounts[_selectedHotbarSlot];
				entity->heldStyle = ac::heldItemStyleFor(selectedItemDef(), count);
				entity->humanoidAnimation.toolUsing =
					(entity->heldStyle == ac::heldItemStyle::tool ||
						entity->heldStyle == ac::heldItemStyle::axe) &&
					_screen == ac::gameScreen::playing &&
					_cursorCaptured &&
					!isSpectator() &&
					_uiInput.mouseDown(GLFW_MOUSE_BUTTON_LEFT);
			}
		}

		bool cameraPositionClear(const dx::XMFLOAT3& position, float radius = 0.18f) const {
			// Treat touching a voxel face as clear. Without this inset, a camera
			// exactly flush with a wall or ceiling repeatedly changed obstruction
			// state as floating-point movement settled against the block.
			constexpr float epsilon = .001f;
			const int32_t minX = static_cast<int32_t>(std::floor(position.x - radius + epsilon));
			const int32_t minY = static_cast<int32_t>(std::floor(position.y - radius + epsilon));
			const int32_t minZ = static_cast<int32_t>(std::floor(position.z - radius + epsilon));
			const int32_t maxX = static_cast<int32_t>(std::floor(position.x + radius - epsilon));
			const int32_t maxY = static_cast<int32_t>(std::floor(position.y + radius - epsilon));
			const int32_t maxZ = static_cast<int32_t>(std::floor(position.z + radius - epsilon));
			for (int32_t z = minZ; z <= maxZ; ++z) {
				for (int32_t y = minY; y <= maxY; ++y) {
					for (int32_t x = minX; x <= maxX; ++x) {
						const ac::blockId id = _streamer.blockAt(x, y, z);
						if (id == 0u) continue;
						const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
						if (definition && definition->_occludes)
							return false;
					}
				}
			}
			return true;
		}

		dx::XMFLOAT3 unobstructedCameraPosition(
			const dx::XMFLOAT3& anchor,
			const dx::XMFLOAT3& desired,
			float radius = 0.18f
		) const {
			const dx::XMVECTOR start = dx::XMLoadFloat3(&anchor);
			const dx::XMVECTOR offset = dx::XMVectorSubtract(dx::XMLoadFloat3(&desired), start);
			const float distance = dx::XMVectorGetX(dx::XMVector3Length(offset));
			if (distance <= 0.001f) return anchor;

			const uint32_t steps = (std::max)(1u, static_cast<uint32_t>(std::ceil(distance / 0.01f)));
			dx::XMFLOAT3 safe = anchor;
			for (uint32_t step = 1u; step <= steps; ++step) {
				const float amount = static_cast<float>(step) / static_cast<float>(steps);
				dx::XMFLOAT3 candidate;
				dx::XMStoreFloat3(&candidate, dx::XMVectorMultiplyAdd(offset, dx::XMVectorReplicate(amount), start));
				if (!cameraPositionClear(candidate, radius)) break;
				safe = candidate;
			}
			return safe;
		}

		void rebuildVoxelLighting(const std::vector<ac::gpuLight>& sources) {
			std::unordered_map<voxelCoordinate, uint32_t, voxelCoordinateHash> radiance;
			radiance.reserve(sources.size() * 8192u);

			const auto occludes = [&](const voxelCoordinate& position) {
				const ac::blockId id = _streamer.blockAt(position.x, position.y, position.z);
				if (id == 0u) return false;
				const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
				return definition && definition->_occludes && definition->_emission.intensity <= 0.0f;
			};
			const auto isWater = [&](const voxelCoordinate& position) {
				return ac::blockType(_streamer.blockAt(
					position.x, position.y, position.z)) == ac::WATER_BLOCK_TYPE;
			};
			// Stained glass / panes filter light by dye color.
			const auto stainedGlassTransmission = [&](const voxelCoordinate& position) {
				const ac::blockId id = _streamer.blockAt(position.x, position.y, position.z);
				if (id == 0u) return dx::XMFLOAT3{ 1.0f, 1.0f, 1.0f };
				return ac::glassLightDye(_blocks.get(ac::blockType(id)));
			};
			// DDA light ray: blocked by occluders, otherwise multiplies transmission
			// for every stained-glass cell the ray passes through.
			const auto traceLightRay = [&](const dx::XMFLOAT3& start, const dx::XMFLOAT3& end) {
				dx::XMFLOAT3 transmission{ 1.0f, 1.0f, 1.0f };
				voxelCoordinate voxel{
					static_cast<int32_t>(std::floor(start.x)),
					static_cast<int32_t>(std::floor(start.y)),
					static_cast<int32_t>(std::floor(start.z))
				};
				const voxelCoordinate endVoxel{
					static_cast<int32_t>(std::floor(end.x)),
					static_cast<int32_t>(std::floor(end.y)),
					static_cast<int32_t>(std::floor(end.z))
				};
				const dx::XMFLOAT3 ray{ end.x - start.x, end.y - start.y, end.z - start.z };
				const int32_t stepX = ray.x >= 0.0f ? 1 : -1;
				const int32_t stepY = ray.y >= 0.0f ? 1 : -1;
				const int32_t stepZ = ray.z >= 0.0f ? 1 : -1;
				const float infinity = (std::numeric_limits<float>::infinity)();
				const float deltaX = std::abs(ray.x) > 1e-6f ? std::abs(1.0f / ray.x) : infinity;
				const float deltaY = std::abs(ray.y) > 1e-6f ? std::abs(1.0f / ray.y) : infinity;
				const float deltaZ = std::abs(ray.z) > 1e-6f ? std::abs(1.0f / ray.z) : infinity;
				float nextX = std::abs(ray.x) > 1e-6f
					? ((stepX > 0 ? static_cast<float>(voxel.x + 1) : static_cast<float>(voxel.x)) - start.x) / ray.x
					: infinity;
				float nextY = std::abs(ray.y) > 1e-6f
					? ((stepY > 0 ? static_cast<float>(voxel.y + 1) : static_cast<float>(voxel.y)) - start.y) / ray.y
					: infinity;
				float nextZ = std::abs(ray.z) > 1e-6f
					? ((stepZ > 0 ? static_cast<float>(voxel.z + 1) : static_cast<float>(voxel.z)) - start.z) / ray.z
					: infinity;

				for (uint32_t iteration = 0; iteration < 64u && !(voxel == endVoxel); ++iteration) {
					const float nearest = std::min({ nextX, nextY, nextZ });
					if (std::abs(nextX - nearest) < 1e-5f) { voxel.x += stepX; nextX += deltaX; }
					if (std::abs(nextY - nearest) < 1e-5f) { voxel.y += stepY; nextY += deltaY; }
					if (std::abs(nextZ - nearest) < 1e-5f) { voxel.z += stepZ; nextZ += deltaZ; }
					if (voxel == endVoxel) return transmission;
					if (occludes(voxel))
						return dx::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
					const dx::XMFLOAT3 glass = stainedGlassTransmission(voxel);
					transmission.x *= glass.x;
					transmission.y *= glass.y;
					transmission.z *= glass.z;
				}
				return transmission;
			};

			const auto emitterVisibility = [&](const ac::gpuLight& source, const voxelCoordinate& target) {
				const dx::XMFLOAT3 receiver{
					static_cast<float>(target.x) + 0.5f,
					static_cast<float>(target.y) + 0.5f,
					static_cast<float>(target.z) + 0.5f
				};
				const dx::XMFLOAT3 fromEmitter{
					receiver.x - source.position.x,
					receiver.y - source.position.y,
					receiver.z - source.position.z
				};
				const dx::XMFLOAT3 absolute{
					std::abs(fromEmitter.x), std::abs(fromEmitter.y), std::abs(fromEmitter.z)
				};
				std::array<dx::XMFLOAT3, 5> samples{};
				// Sample the authored emitter volume. The previous fixed 0.34 offset
				// made narrow emitters such as torches behave like most of a block.
				const float ux = source.halfExtent.x * 0.68f;
				const float uy = source.halfExtent.y * 0.68f;
				const float uz = source.halfExtent.z * 0.68f;
				if (absolute.x >= absolute.y && absolute.x >= absolute.z) {
					const float face = source.position.x + (fromEmitter.x >= 0.0f ? source.halfExtent.x : -source.halfExtent.x);
					samples = { dx::XMFLOAT3{face, source.position.y, source.position.z},
						dx::XMFLOAT3{face, source.position.y-uy, source.position.z-uz}, dx::XMFLOAT3{face, source.position.y+uy, source.position.z+uz},
						dx::XMFLOAT3{face, source.position.y-uy, source.position.z+uz}, dx::XMFLOAT3{face, source.position.y+uy, source.position.z-uz} };
				}
				else if (absolute.y >= absolute.z) {
					const float face = source.position.y + (fromEmitter.y >= 0.0f ? source.halfExtent.y : -source.halfExtent.y);
					samples = { dx::XMFLOAT3{source.position.x, face, source.position.z},
						dx::XMFLOAT3{source.position.x-ux, face, source.position.z-uz}, dx::XMFLOAT3{source.position.x+ux, face, source.position.z+uz},
						dx::XMFLOAT3{source.position.x-ux, face, source.position.z+uz}, dx::XMFLOAT3{source.position.x+ux, face, source.position.z-uz} };
				}
				else {
					const float face = source.position.z + (fromEmitter.z >= 0.0f ? source.halfExtent.z : -source.halfExtent.z);
					samples = { dx::XMFLOAT3{source.position.x, source.position.y, face},
						dx::XMFLOAT3{source.position.x-ux, source.position.y-uy, face}, dx::XMFLOAT3{source.position.x+ux, source.position.y+uy, face},
						dx::XMFLOAT3{source.position.x-ux, source.position.y+uy, face}, dx::XMFLOAT3{source.position.x+ux, source.position.y-uy, face} };
				}
				const uint32_t sampleCount = _lightingQuality == 0u ? 1u : 3u;
				dx::XMFLOAT3 transmissionSum{ 0.0f, 0.0f, 0.0f };
				for (uint32_t sample = 0; sample < sampleCount; ++sample) {
					const dx::XMFLOAT3 sampleTransmission = traceLightRay(receiver, samples[sample]);
					transmissionSum.x += sampleTransmission.x;
					transmissionSum.y += sampleTransmission.y;
					transmissionSum.z += sampleTransmission.z;
				}
				const float inv = 1.0f / static_cast<float>(sampleCount);
				// Tiny uncolored floor so fully shadowed areas are not pure black;
				// colored glass still dominates whenever a ray gets through.
				return dx::XMFLOAT3{
					0.05f + transmissionSum.x * inv * 0.95f,
					0.05f + transmissionSum.y * inv * 0.95f,
					0.05f + transmissionSum.z * inv * 0.95f
				};
			};

			struct propagationNode {
				voxelCoordinate position;
				uint16_t distance = 0;
				dx::XMFLOAT3 transmission{ 1.0f, 1.0f, 1.0f };
				float score = 1.0f;
			};
			constexpr std::array<voxelCoordinate, 6> directions = {
				voxelCoordinate{-1, 0, 0}, voxelCoordinate{1, 0, 0},
				voxelCoordinate{0, -1, 0}, voxelCoordinate{0, 1, 0},
				voxelCoordinate{0, 0, -1}, voxelCoordinate{0, 0, 1}
			};

			// Propagate a single emitter in isolation so the result can be cached
			// and replayed while nothing inside its reach changes.
			std::unordered_map<voxelCoordinate, uint32_t, voxelCoordinateHash> sourceRadiance;
			const auto propagateSource = [&](const ac::gpuLight& source, int radius) {
				sourceRadiance.clear();
				const voxelCoordinate origin = {
					static_cast<int32_t>(std::floor(source.position.x)),
					static_cast<int32_t>(std::floor(source.position.y)),
					static_cast<int32_t>(std::floor(source.position.z))
				};
				std::vector<propagationNode> queue;
				queue.reserve(static_cast<size_t>(radius * radius * radius * 2));
				std::unordered_map<voxelCoordinate, float, voxelCoordinateHash> strongestPath;
				strongestPath.reserve(queue.capacity());
				queue.push_back({ origin, 0u, { 1.0f, 1.0f, 1.0f }, 1.0f });
				strongestPath.emplace(origin, 1.0f);

				for (size_t head = 0; head < queue.size(); ++head) {
					const propagationNode node = queue[head];
					const auto strongest = strongestPath.find(node.position);
					if (strongest != strongestPath.end() && node.score + .0001f < strongest->second)
						continue;
					if (node.distance > radius || (node.distance != 0u && occludes(node.position)))
						continue;

					const float normalized = static_cast<float>(node.distance) / static_cast<float>(radius);
					// Quadratic decay keeps the configured reach but makes glowstone lose
					// intensity much more clearly as distance increases.
					const float remainingLight = (std::max)(0.0f, 1.0f - normalized);
					const float attenuation = std::pow(remainingLight, 1.65f);
					const dx::XMFLOAT3 visibility = node.distance == 0u
						? dx::XMFLOAT3{ 1.0f, 1.0f, 1.0f }
						: emitterVisibility(source, node.position);
					const uint32_t packed = packVoxelRadiance({
						source.color.x * source.intensity * attenuation * visibility.x * node.transmission.x,
						source.color.y * source.intensity * attenuation * visibility.y * node.transmission.y,
						source.color.z * source.intensity * attenuation * visibility.z * node.transmission.z
					});
					if (packed != 0u) {
						auto [entry, inserted] = sourceRadiance.emplace(node.position, packed);
						if (!inserted) entry->second = combinePackedRadiance(entry->second, packed);
					}

					if (node.distance == radius) continue;
					const bool currentWater = isWater(node.position);
					for (const voxelCoordinate& direction : directions) {
						voxelCoordinate next = {
							node.position.x + direction.x,
							node.position.y + direction.y,
							node.position.z + direction.z
						};
						if (next.y < 0 || next.y >= CHUNK_HEIGHT)
							continue;
						const bool nextWater = isWater(next);
						dx::XMFLOAT3 stepTransmission = nextWater
							? dx::XMFLOAT3{ .82f, .91f, .96f }
							: dx::XMFLOAT3{ 1.0f, 1.0f, 1.0f };
						if (currentWater != nextWater) {
							const dx::XMFLOAT3 interfaceTransmission = nextWater
								? dx::XMFLOAT3{ .72f, .84f, .90f }
								: dx::XMFLOAT3{ .80f, .88f, .93f };
							stepTransmission.x *= interfaceTransmission.x;
							stepTransmission.y *= interfaceTransmission.y;
							stepTransmission.z *= interfaceTransmission.z;
						}
						const dx::XMFLOAT3 glassTransmission = stainedGlassTransmission(next);
						stepTransmission.x *= glassTransmission.x;
						stepTransmission.y *= glassTransmission.y;
						stepTransmission.z *= glassTransmission.z;
						const dx::XMFLOAT3 nextTransmission{
							node.transmission.x * stepTransmission.x,
							node.transmission.y * stepTransmission.y,
							node.transmission.z * stepTransmission.z
						};
						const uint16_t nextDistance = static_cast<uint16_t>(node.distance + 1u);
						const float distanceRemaining = (std::max)(0.0f,
							1.0f - static_cast<float>(nextDistance) / static_cast<float>(radius));
						const float nextScore = (std::max)({
							nextTransmission.x, nextTransmission.y, nextTransmission.z }) * distanceRemaining;
						auto [path, inserted] = strongestPath.emplace(next, nextScore);
						if (!inserted && path->second >= nextScore * .995f)
							continue;
						if (!inserted) path->second = nextScore;
						queue.push_back({ next, nextDistance, nextTransmission, nextScore });
					}
				}
			};

			// Emitter identity is its cell plus its light parameters, so a wire
			// changing power or a torch going out misses the cache on its own.
			const auto sourceSignature = [](const ac::gpuLight& light) {
				uint32_t hash = 2166136261u;
				const auto mix = [&hash](float value) {
					uint32_t bits = 0;
					std::memcpy(&bits, &value, sizeof(bits));
					hash = (hash ^ bits) * 16777619u;
				};
				mix(light.radius);
				mix(light.color.x);
				mix(light.color.y);
				mix(light.color.z);
				mix(light.intensity);
				mix(light.halfExtent.x);
				mix(light.halfExtent.y);
				mix(light.halfExtent.z);
				return (hash ^ light.type) * 16777619u;
			};

			// Only emitters whose reach covers an edited cell need repropagating.
			// Without a usable change log every cached contribution is suspect.
			const uint64_t geometryRevision = _streamer.blockRevision();
			std::vector<dx::XMINT3> editedCells;
			const bool incremental = _voxelLightGeometryRevision != UINT64_MAX &&
				_streamer.blockChangesSince(_voxelLightGeometryRevision, editedCells);
			if (!incremental || _voxelLightCachedCells > MAX_VOXEL_LIGHT_CACHED_CELLS) {
				_voxelLightContributions.clear();
				_voxelLightCachedCells = 0;
			}
			else if (!editedCells.empty()) {
				for (auto entry = _voxelLightContributions.begin();
					entry != _voxelLightContributions.end();) {
					const voxelLightSourceKey& key = entry->first;
					const int32_t reach = entry->second.radius + 1;
					const bool touched = std::any_of(
						editedCells.begin(), editedCells.end(),
						[&](const dx::XMINT3& cell) {
							return std::abs(cell.x - key.x) <= reach &&
								std::abs(cell.y - key.y) <= reach &&
								std::abs(cell.z - key.z) <= reach;
						});
					if (!touched) {
						entry = std::next(entry);
						continue;
					}
					_voxelLightCachedCells -= entry->second.cells.size();
					entry = _voxelLightContributions.erase(entry);
				}
			}
			_voxelLightGeometryRevision = geometryRevision;

			std::unordered_set<voxelLightSourceKey, voxelLightSourceKeyHash> liveSources;
			liveSources.reserve(sources.size() * 2u);
			for (const ac::gpuLight& source : sources) {
				const int radius = std::max(1, static_cast<int>(std::ceil(source.radius)));
				const voxelLightSourceKey key{
					static_cast<int32_t>(std::floor(source.position.x)),
					static_cast<int32_t>(std::floor(source.position.y)),
					static_cast<int32_t>(std::floor(source.position.z)),
					sourceSignature(source)
				};
				if (!liveSources.insert(key).second)
					continue;
				auto cached = _voxelLightContributions.find(key);
				if (cached == _voxelLightContributions.end()) {
					propagateSource(source, radius);
					voxelLightContribution contribution;
					contribution.radius = radius;
					contribution.cells.assign(sourceRadiance.begin(), sourceRadiance.end());
					_voxelLightCachedCells += contribution.cells.size();
					cached = _voxelLightContributions
						.emplace(key, std::move(contribution)).first;
				}
				for (const auto& [position, packed] : cached->second.cells) {
					auto [entry, inserted] = radiance.emplace(position, packed);
					if (!inserted) entry->second = combinePackedRadiance(entry->second, packed);
				}
			}

			for (auto entry = _voxelLightContributions.begin();
				entry != _voxelLightContributions.end();) {
				if (liveSources.find(entry->first) != liveSources.end()) {
					entry = std::next(entry);
					continue;
				}
				_voxelLightCachedCells -= entry->second.cells.size();
				entry = _voxelLightContributions.erase(entry);
			}

			std::fill(_voxelLightScratch.begin(), _voxelLightScratch.end(), ac::gpuVoxelLight{});
			const uint32_t tableMask = ac::VOXEL_LIGHT_TABLE_SIZE - 1u;
			for (const auto& [position, packedColor] : radiance) {
				uint32_t slot = static_cast<uint32_t>(voxelCoordinateHash{}(position)) & tableMask;
				for (uint32_t probe = 0; probe < 24u; ++probe) {
					ac::gpuVoxelLight& target = _voxelLightScratch[slot];
					if (target.packedColor == 0u) {
						target.position = { position.x, position.y, position.z };
						target.packedColor = packedColor;
						break;
					}
					slot = (slot + 1u) & tableMask;
				}
			}

			auto differs = [](const ac::gpuVoxelLight& left, const ac::gpuVoxelLight& right) {
				return left.position.x != right.position.x || left.position.y != right.position.y ||
					left.position.z != right.position.z || left.packedColor != right.packedColor;
			};
			size_t changed = 0;
			for (size_t index = 0; index < _voxelLightTable.size(); ++index)
				changed += differs(_voxelLightTable[index], _voxelLightScratch[index]);
			if (changed > _voxelLightTable.size() / 4u) {
				_voxelLights.update(_context, _voxelLightScratch.data(), ac::VOXEL_LIGHT_TABLE_SIZE);
			}
			else if (changed != 0u) {
				constexpr size_t MAX_UNCHANGED_GAP = 32u;
				std::vector<std::pair<UINT, UINT>> ranges;
				ranges.reserve(128);
				size_t index = 0;
				while (index < _voxelLightTable.size()) {
					while (index < _voxelLightTable.size() &&
						!differs(_voxelLightTable[index], _voxelLightScratch[index])) ++index;
					if (index == _voxelLightTable.size()) break;
					const size_t first = index++;
					size_t lastChanged = first;
					while (index < _voxelLightTable.size()) {
						if (differs(_voxelLightTable[index], _voxelLightScratch[index]))
							lastChanged = index;
						else if (index - lastChanged > MAX_UNCHANGED_GAP)
							break;
						++index;
					}
					const UINT count = static_cast<UINT>(lastChanged - first + 1u);
					ranges.emplace_back(static_cast<UINT>(first), count);
					if (ranges.size() > 128u) break;
					index = lastChanged + 1u;
				}
				if (ranges.size() > 128u) {
					_voxelLights.update(_context, _voxelLightScratch.data(), ac::VOXEL_LIGHT_TABLE_SIZE);
				}
				else {
					for (const auto& [first, count] : ranges)
						_voxelLights.updateRange(_context, _voxelLightScratch.data() + first, first, count);
				}
			}
			_voxelLightTable.swap(_voxelLightScratch);
			_voxelLightingHasData = !radiance.empty();
			_voxelLightBuffer.update(_context, {
				tableMask,
				_voxelLightingHasData ? 1u : 0u,
				_lightingQuality,
				_waterMaterial,
				static_cast<uint32_t>(_world.seed()),
				{}
			});
		}

		void buildChunkLightSources(
			const ac::worldChunkGPU& worldChunk,
			std::vector<ac::gpuLight>& out
		) const {
			out.clear();
			for (const auto& emitter : worldChunk._emitters) {
				const ac::blockId type = ac::blockType(emitter.id);
				if (ac::isRedstoneWire(type)) {
					const uint8_t power = ac::redstonePower(emitter.id);
					if (power == 0u) continue;
					const float t = static_cast<float>(power) / 15.0f;
					out.push_back({
						{ static_cast<float>(emitter.position.x) + 0.5f,
						  static_cast<float>(emitter.position.y) + 0.05f,
						  static_cast<float>(emitter.position.z) + 0.5f },
						1.5f + t * 6.5f,
						{ 1.0f, 0.15f + 0.25f * t, 0.08f },
						0.08f + 0.55f * t,
						{ 0.35f, 0.35f, 0.35f }, ac::GPU_LIGHT_MESH
					});
					continue;
				}
				const ac::blockDefinition* definition = _blocks.get(
					ac::blockType(ac::visualBlockState(emitter.id)));
				if (!definition) continue;
				const ac::blockLight& emission = definition->_emission;
				const float signalBrightness = ac::isRedstoneLamp(emitter.id)
					? static_cast<float>(ac::redstonePower(emitter.id)) / 15.0f : 1.0f;
				dx::XMFLOAT3 local = ac::isWallAttached(emitter.id) && emission.wallPositionSpecified
					? emission.wallPosition : emission.position;
				dx::XMFLOAT3 halfExtent = emission.halfExtent;
				const uint32_t turns = ac::isWallAttached(emitter.id)
					? ac::wallTorchYawTurns(ac::blockFacing(emitter.id))
					: ac::facingToYawTurns(ac::blockFacing(emitter.id));
				float localX = local.x - 0.5f;
				float localZ = local.z - 0.5f;
				for (uint32_t turn = 0; turn < turns; ++turn) {
					const float rotatedX = localZ;
					localZ = -localX;
					localX = rotatedX;
				}
				local.x = localX + 0.5f;
				local.z = localZ + 0.5f;
				if ((turns & 1u) != 0u)
					std::swap(halfExtent.x, halfExtent.z);
				out.push_back({
					{ static_cast<float>(emitter.position.x) + local.x,
					  static_cast<float>(emitter.position.y) + local.y,
					  static_cast<float>(emitter.position.z) + local.z },
					emission.radius, emission.color, emission.intensity * signalBrightness,
					halfExtent, ac::GPU_LIGHT_MESH
				});
			}
		}

		// Emitter lists only move for the chunk that was edited, so keep each
		// chunk's converted lights and re-convert just the ones that changed.
		void collectBlockLightSources(std::vector<ac::gpuLight>& sources) const {
			const auto& chunks = _streamer.chunks();
			for (auto entry = _chunkLightSources.begin(); entry != _chunkLightSources.end();) {
				entry = chunks.find(entry->first) == chunks.end()
					? _chunkLightSources.erase(entry)
					: std::next(entry);
			}

			size_t total = 0;
			for (const auto& [chunkKey, worldChunk] : chunks) {
				chunkLightSourceCache& cached = _chunkLightSources[chunkKey];
				if (cached.emitterRevision != worldChunk._emitterRevision) {
					buildChunkLightSources(worldChunk, cached.lights);
					cached.emitterRevision = worldChunk._emitterRevision;
				}
				total += cached.lights.size();
			}

			sources.clear();
			sources.reserve(total);
			for (const auto& [chunkKey, cached] : _chunkLightSources) {
				(void)chunkKey;
				sources.insert(sources.end(), cached.lights.begin(), cached.lights.end());
			}
		}

		static bool blockLightCanReach(const ac::gpuLight& light, const dx::XMINT3& block) {
			const float distance =
				std::abs(light.position.x - (static_cast<float>(block.x) + 0.5f)) +
				std::abs(light.position.y - (static_cast<float>(block.y) + 0.5f)) +
				std::abs(light.position.z - (static_cast<float>(block.z) + 0.5f));
			return distance <= light.radius + 1.0f;
		}

		void updateBlockLights() {
			const uint64_t revision = _streamer.blockRevision();
			if (revision == _blockLightingRevision)
				return;
			const auto now = std::chrono::steady_clock::now();
			if (revision != _pendingBlockLightingRevision) {
				const bool hadPending = _pendingBlockLightingRevision != _blockLightingRevision;
				if (!hadPending) _pendingBlockLightingSince = now;
				_pendingBlockLightingRevision = revision;
				collectBlockLightSources(_pendingBlockSources);
				dx::XMINT3 localizedPosition{};
				const bool localizedEdit = _streamer.localizedBlockChange(revision, localizedPosition);
				if (localizedEdit) {
					auto reaches = [&](const std::vector<ac::gpuLight>& sources) {
						return std::any_of(sources.begin(), sources.end(), [&](const ac::gpuLight& light) {
							return blockLightCanReach(light, localizedPosition);
						});
					};
					_pendingBlockLightingAffected |= reaches(_pendingBlockSources) || reaches(_propagatedBlockSources);
				}
				else {
					_pendingBlockLightingAffected = true;
					_pendingBlockLightingStreaming = true;
				}
			}
			if (!_pendingBlockLightingAffected) {
				_blockLightingRevision = revision;
				_pendingBlockLightingRevision = revision;
				_propagatedBlockSources = _pendingBlockSources;
				return;
			}
			const auto delay = _pendingBlockLightingStreaming
				? std::chrono::milliseconds(400) : std::chrono::milliseconds(75);
			const auto elapsedFrom = _pendingBlockLightingStreaming
				? _pendingBlockLightingSince : _lastBlockLightingBuild;
			if (elapsedFrom.time_since_epoch().count() != 0 && now - elapsedFrom < delay)
				return;

			_blockLightingRevision = revision;
			_pendingBlockLightingRevision = revision;
			_lastBlockLightingBuild = now;
			_propagatedBlockSources = _pendingBlockSources;
			_pendingBlockLightingAffected = false;
			_pendingBlockLightingStreaming = false;

			// Block emitters use the cached propagated field. The separate dynamic
			// list remains available for moving point and mesh lights.
			if (!_lightData.empty())
				_lights.update(_context, _lightData.data(), static_cast<UINT>(_lightData.size()));
			_lightBuffer.update(_context, {
				static_cast<uint32_t>(_lightData.size()),
				{ 0, 0, 0 }
			});
			if (_lightData.empty())
				_chunkLightBuffer.update(_context, {});
			_chunkLightsBound = false;
			rebuildLightClusters();
			rebuildVoxelLighting(_propagatedBlockSources);
		}

		void rebuildLightClusters() {
			_lightClusters.clear();
			_lightClusterCount = _lightData.size();
			for (uint32_t index = 0; index < _lightData.size(); ++index) {
				const ac::gpuLight& light = _lightData[index];
				if (light.type == ac::GPU_LIGHT_DIRECTIONAL_PROXY || light.radius <= 0.0f)
					continue;
				const float extra = light.type == ac::GPU_LIGHT_POINT
					? 0.0f
					: (std::max)({ light.halfExtent.x, light.halfExtent.y, light.halfExtent.z });
				const float reach = light.radius + extra;
				const int32_t minX = static_cast<int32_t>(std::floor((light.position.x - reach) / CHUNK_WIDTH));
				const int32_t maxX = static_cast<int32_t>(std::floor((light.position.x + reach) / CHUNK_WIDTH));
				const int32_t minZ = static_cast<int32_t>(std::floor((light.position.z - reach) / CHUNK_LENGTH));
				const int32_t maxZ = static_cast<int32_t>(std::floor((light.position.z + reach) / CHUNK_LENGTH));
				for (int32_t z = minZ; z <= maxZ; ++z) {
					for (int32_t x = minX; x <= maxX; ++x)
						_lightClusters[{ x, z }].push_back(index);
				}
			}
		}

		ac::chunkLightBufferData chunkLightsFor(const dx::XMINT3& chunkPosition) {
			ac::chunkLightBufferData result{};
			if (_lightData.empty()) return result;
			if (_lightClusterCount != _lightData.size())
				rebuildLightClusters();

			const auto found = _lightClusters.find({ chunkPosition.x, chunkPosition.z });
			if (found == _lightClusters.end() || found->second.empty())
				return result;

			const dx::XMFLOAT3 center = {
				static_cast<float>(chunkPosition.x * CHUNK_WIDTH) + CHUNK_WIDTH * 0.5f,
				static_cast<float>(chunkPosition.y * CHUNK_HEIGHT) + CHUNK_HEIGHT * 0.5f,
				static_cast<float>(chunkPosition.z * CHUNK_LENGTH) + CHUNK_LENGTH * 0.5f
			};
			const uint32_t maximumLights = _lightingQuality == 0u ? 16u :
				(_lightingQuality == 1u ? 32u : 64u);
			_chunkLightRankScratch.clear();
			_chunkLightRankScratch.reserve(found->second.size());
			for (uint32_t index : found->second) {
				if (index >= _lightData.size()) continue;
				const ac::gpuLight& light = _lightData[index];
				if (light.type == ac::GPU_LIGHT_DIRECTIONAL_PROXY)
					continue;
				const float dx = light.position.x - center.x;
				const float dy = light.position.y - center.y;
				const float dz = light.position.z - center.z;
				_chunkLightRankScratch.push_back({
					index,
					light.intensity * light.radius / (1.0f + dx * dx + dy * dy + dz * dz)
				});
			}
			if (_chunkLightRankScratch.size() > maximumLights) {
				std::partial_sort(
					_chunkLightRankScratch.begin(),
					_chunkLightRankScratch.begin() + static_cast<std::ptrdiff_t>(maximumLights),
					_chunkLightRankScratch.end(),
					[](const scoredChunkLight& a, const scoredChunkLight& b) { return a.score > b.score; });
				_chunkLightRankScratch.resize(maximumLights);
			}

			uint32_t* indices = reinterpret_cast<uint32_t*>(result.indices.data());
			for (const scoredChunkLight& light : _chunkLightRankScratch) {
				if (result.count >= maximumLights) break;
				indices[result.count++] = light.index;
			}
			return result;
		}

		void bindChunkLights(const dx::XMINT3& chunkPosition) {
			if (_lightData.empty()) return;
			if (_chunkLightsBound &&
				_boundChunkLights.x == chunkPosition.x &&
				_boundChunkLights.y == chunkPosition.y &&
				_boundChunkLights.z == chunkPosition.z) {
				_chunkLightBuffer.bindPS(_context, 5);
				return;
			}
			_chunkLightBuffer.update(_context, chunkLightsFor(chunkPosition));
			_chunkLightBuffer.bindPS(_context, 5);
			_boundChunkLights = chunkPosition;
			_chunkLightsBound = true;
		}

		ID3D11ShaderResourceView* blockIcon(ac::blockId id) const {
			if (_itemIcons.ready()) {
				const auto uv = _itemIcons.uvFor(id);
				if (uv.valid) return _itemIcons.srv();
			}
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
			if (!definition)
				return nullptr;
			ac::texture* item = _blockTextures.get(definition->materialForFace(ac::BLOCK_FACE_UP));
			return item ? item->_shaderResourceView.Get() : nullptr;
		}






		static size_t nearestIndex(const int* values, size_t count, int target) {
			size_t best = 0;
			int bestDelta = (std::numeric_limits<int>::max)();
			for (size_t i = 0; i < count; ++i) {
				const int delta = std::abs(values[i] - target);
				if (delta < bestDelta) {
					bestDelta = delta;
					best = i;
				}
			}
			return best;
		}

		std::filesystem::path worldSettingsPath() const {
			return _world.path() / "world_settings.json";
		}

		std::filesystem::path playerInventoryPath() const {
			return _world.path() / "player_inventory.json";
		}

		void persistUserSettings() {
			ac::userSettingsData settings{};
			settings.vsync = _vsyncEnabled;
			settings.fpsLimit = FPS_LIMITS[_fpsLimitIndex];
			settings.lightingQuality = _lightingQuality;
			settings.gpuTerrain = _streamer.gpuTerrainEnabled();
			settings.gpuEntityPathfinding = _livingEntities && _livingEntities->gpuEnabled();
			settings.viewDistance = VIEW_DISTANCES[_viewDistanceIndex];
			settings.fxaa = _postSettings.antialiasing != 0;
			settings.motionBlur = _motionBlurEnabled;
			settings.shadows = _shadowsEnabled;
			settings.fov = FOV_DEGREES[_fovIndex];
			settings.antialiasing = _postSettings.antialiasing;
			settings.bloom = _postSettings.bloom;
			settings.gtao = _postSettings.gtao;
			settings.fog = _postSettings.fog;
			settings.renderScale = RENDER_SCALES[_renderScaleIndex];
			settings.lastWorld = _lastWorldPath;
			ac::saveUserSettings(USER_SETTINGS_PATH, settings);
		}

		void persistWorldSettings() {
			if (!_worldSessionOpen || !_world.isOpen())
				return;
			ac::worldSettingsData settings{};
			settings.dayCycleIndex = static_cast<uint32_t>(_dayCycleIndex);
			settings.weatherClock = _weather.clock();
			ac::saveWorldSettings(worldSettingsPath(), settings);
		}

		void persistPlayerInventory() {
			if (!_worldSessionOpen || !_world.isOpen())
				return;
			ac::playerInventoryData inventory{};
			for (size_t i = 0; i < inventory.hotbar.size() && i < _hotbarBlocks.size(); ++i) {
				inventory.hotbar[i] = _world.persistentBlockState(_hotbarBlocks[i]);
				inventory.counts[i] = _hotbarCounts[i];
			}
			for (size_t i = 0; i < inventory.inventory.size() && i < _inventoryBlocks.size(); ++i) {
				inventory.inventory[i] = _world.persistentBlockState(_inventoryBlocks[i]);
				inventory.inventoryCounts[i] = _inventoryCounts[i];
			}
			inventory.selectedSlot = static_cast<uint32_t>(std::clamp(
				_selectedHotbarSlot, 0, HUD_HOTBAR_COUNT - 1));
			inventory.health = _health;
			inventory.hunger = _hunger;
			inventory.saturation = _saturation;
			inventory.air = _air;
			ac::savePlayerInventory(playerInventoryPath(), inventory);
		}

		void applyDefaultHotbar() {
			std::vector<ac::blockId> defaultHotbar;
			defaultHotbar.reserve(APPLICATION_CONFIG.defaultHotbar.size());
			for (const std::string& name : APPLICATION_CONFIG.defaultHotbar)
				defaultHotbar.push_back(_blocks.getId(name));
			for (size_t slot = 0; slot < _hotbarBlocks.size(); ++slot) {
				_hotbarBlocks[slot] = slot < defaultHotbar.size() ? defaultHotbar[slot] : 0;
				_hotbarCounts[slot] = _hotbarBlocks[slot] != 0 ? itemStackLimit(_hotbarBlocks[slot]) : 0;
			}
			_selectedHotbarSlot = 0;
			_selectedBlock = _hotbarBlocks[0];
		}

		void applyPlayerInventory(const ac::playerInventoryData& inventory) {
			bool any = false;
			for (uint32_t id : inventory.hotbar)
				if (id != 0) any = true;
			for (uint32_t id : inventory.inventory)
				if (id != 0) any = true;
			if (!any)
				return;

			for (size_t i = 0; i < _hotbarBlocks.size() && i < inventory.hotbar.size(); ++i) {
				const uint32_t id = _world.remapSavedBlockState(inventory.hotbar[i]);
				if (id == 0 || _blocks.get(ac::blockType(id))) {
					_hotbarBlocks[i] = static_cast<ac::blockId>(id);
					uint32_t count = i < inventory.counts.size() ? inventory.counts[i] : 0;
					if (_hotbarBlocks[i] == 0)
						count = 0;
					else if (count == 0)
						count = itemStackLimit(_hotbarBlocks[i]);
					_hotbarCounts[i] = (std::min)(count, itemStackLimit(_hotbarBlocks[i]));
				}
				else {
					_hotbarBlocks[i] = 0;
					_hotbarCounts[i] = 0;
				}
			}
			for (size_t i = 0; i < _inventoryBlocks.size() && i < inventory.inventory.size(); ++i) {
				const uint32_t id = _world.remapSavedBlockState(inventory.inventory[i]);
				if (id == 0 || _blocks.get(ac::blockType(id))) {
					_inventoryBlocks[i] = static_cast<ac::blockId>(id);
					uint32_t count = i < inventory.inventoryCounts.size() ? inventory.inventoryCounts[i] : 0;
					if (_inventoryBlocks[i] == 0)
						count = 0;
					else if (count == 0)
						count = itemStackLimit(_inventoryBlocks[i]);
					_inventoryCounts[i] = (std::min)(count, itemStackLimit(_inventoryBlocks[i]));
				}
				else {
					_inventoryBlocks[i] = 0;
					_inventoryCounts[i] = 0;
				}
			}
			_selectedHotbarSlot = static_cast<int>((std::min)(
				inventory.selectedSlot, static_cast<uint32_t>(HUD_HOTBAR_COUNT - 1)));
			_selectedBlock = _hotbarBlocks[_selectedHotbarSlot];
			_health = std::clamp(inventory.health, 0.0f, ac::PLAYER_MAX_HEALTH);
			_hunger = std::clamp(inventory.hunger, 0.0f, ac::PLAYER_MAX_HUNGER);
			_saturation = std::clamp(inventory.saturation, 0.0f,
				(std::min)(ac::PLAYER_MAX_SATURATION, _hunger));
			_air = std::clamp(inventory.air, 0.0f, ac::PLAYER_MAX_AIR);
		}

		void loadAndApplyPlayerInventory() {
			applyPlayerInventory(ac::loadPlayerInventory(playerInventoryPath()));
		}

		void applyLoadedUserSettings(const ac::userSettingsData& settings) {
			_vsyncEnabled = settings.vsync;
			_graphicsSettings.setVsync(_vsyncEnabled);
			_fpsLimitIndex = nearestIndex(
				FPS_LIMITS.data(), FPS_LIMITS.size(), settings.fpsLimit);
			_lightingQuality = (std::min)(settings.lightingQuality, 2u);
			if (_streamer.gpuTerrainAvailable())
				_streamer.setGpuTerrainEnabled(settings.gpuTerrain);
			if (_livingEntities)
				_livingEntities->setGpuEnabled(settings.gpuEntityPathfinding);
			_viewDistanceIndex = nearestIndex(
				VIEW_DISTANCES.data(), VIEW_DISTANCES.size(), settings.viewDistance);
			_streamer.setViewDistance(VIEW_DISTANCES[_viewDistanceIndex]);
			_postSettings.antialiasing = settings.antialiasing;
			_fxaaEnabled = _postSettings.antialiasing != 0;
			_motionBlurEnabled = settings.motionBlur;
			_shadowsEnabled = settings.shadows;
			_fovIndex = nearestIndex(FOV_DEGREES.data(), FOV_DEGREES.size(), settings.fov);
			_postSettings.bloom = settings.bloom;
			_postSettings.gtao = settings.gtao;
			_postSettings.fog = settings.fog;
			_renderScaleIndex = nearestIndex(
				RENDER_SCALES.data(), RENDER_SCALES.size(), settings.renderScale);
			_postSettings.renderScalePercent = RENDER_SCALES[_renderScaleIndex];
			_lastWorldPath = settings.lastWorld;
			applyFpsLimit();
			applyLightingQuality();
			applyBaseFov();
		}

		void loadAndApplyUserSettings() {
			applyLoadedUserSettings(ac::loadUserSettings(USER_SETTINGS_PATH));
		}

		void loadAndApplyWorldSettings() {
			const ac::worldSettingsData settings = ac::loadWorldSettings(worldSettingsPath());
			_dayCycleIndex = (std::min)(static_cast<size_t>(settings.dayCycleIndex), DAY_CYCLE_SCALES.size() - 1);
			_weather.reset(_world.seed(), settings.weatherClock);
		}

		void applyLightingQuality() {
			_voxelLightBuffer.update(_context, {
				ac::VOXEL_LIGHT_TABLE_SIZE - 1u,
				_voxelLightingHasData ? 1u : 0u,
				_lightingQuality,
				_waterMaterial,
				static_cast<uint32_t>(_world.seed()),
				{}
			});
			_chunkLightsBound = false;
		}

		void applyFpsLimit() {
			const int limit = FPS_LIMITS[_fpsLimitIndex];
			if (limit == 0) _window.setFocusedDeltaTimeLimit(0);
			else _window.setFocusedFpsLimit(static_cast<UINT>(limit));
		}

		float baseFovRadians() const {
			return dx::XMConvertToRadians(static_cast<float>(FOV_DEGREES[_fovIndex]));
		}

		void applyBaseFov() {
			_sprintFov = baseFovRadians();
			_player.getCamera()._fov = _sprintFov;
			_player.getCamera().updateProjection();
		}



		void applyCursorCapture(bool capture) {
			_window.setInputMode(
				GLFW_CURSOR,
				capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL
			);
			_window.setUpdateDeltaCursor(capture);
			_cursorCaptured = capture;
			syncNuklearMouse();
		}

		void syncNuklearMouse() {
			if (!_nuklear.ready()) return;
			const int logicalW = (std::max)(_window.getLogicalWidth(), 1);
			const int logicalH = (std::max)(_window.getLogicalHeight(), 1);
			_nuklear.setMouseScale(
				static_cast<float>(_window.getWidth()) / static_cast<float>(logicalW),
				static_cast<float>(_window.getHeight()) / static_cast<float>(logicalH));
			if (_cursorCaptured) return;
			float mx = 0.0f, my = 0.0f;
			SDL_GetMouseState(&mx, &my);
			_nuklear.syncMouse(mx, my);
		}

		void sampleCrosshairFromFramebuffer(float deltaTime) {
			IDXGISwapChain1* swapChain = _graphicsSettings.getSwapChain();
			if (!swapChain) return;

			Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
			if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
				return;

			D3D11_TEXTURE2D_DESC desc{};
			backBuffer->GetDesc(&desc);
			if (desc.Width == 0 || desc.Height == 0) return;

			constexpr UINT sampleSize = 3;
			if (_crosshairStaging) {
				D3D11_TEXTURE2D_DESC stagingDesc{};
				_crosshairStaging->GetDesc(&stagingDesc);
				if (stagingDesc.Format != desc.Format ||
					stagingDesc.Width != sampleSize ||
					stagingDesc.Height != sampleSize)
					_crosshairStaging.Reset();
			}
			if (!_crosshairStaging) {
				D3D11_TEXTURE2D_DESC stagingDesc{};
				stagingDesc.Width = sampleSize;
				stagingDesc.Height = sampleSize;
				stagingDesc.MipLevels = 1;
				stagingDesc.ArraySize = 1;
				stagingDesc.Format = desc.Format;
				stagingDesc.SampleDesc.Count = 1;
				stagingDesc.Usage = D3D11_USAGE_STAGING;
				stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				if (FAILED(_device->CreateTexture2D(&stagingDesc, nullptr, &_crosshairStaging)))
					return;
			}

			const UINT srcX = desc.Width / 2u;
			const UINT srcY = desc.Height / 2u;
			const UINT left = srcX > 0u ? srcX - 1u : 0u;
			const UINT top = srcY > 0u ? srcY - 1u : 0u;
			D3D11_BOX box{};
			box.left = left;
			box.top = top;
			box.front = 0;
			box.right = (std::min)(left + sampleSize, desc.Width);
			box.bottom = (std::min)(top + sampleSize, desc.Height);
			box.back = 1;
			_context->CopySubresourceRegion(
				_crosshairStaging.Get(), 0, 0, 0, 0,
				backBuffer.Get(), 0, &box);

			D3D11_MAPPED_SUBRESOURCE mapped{};
			if (FAILED(_context->Map(_crosshairStaging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
				return;
			const uint8_t* pixels = static_cast<const uint8_t*>(mapped.pData);
			const UINT copiedW = box.right - box.left;
			const UINT copiedH = box.bottom - box.top;
			float sum[3] = {};
			float count = 0.0f;
			for (UINT y = 0; y < copiedH; ++y) {
				const uint8_t* row = pixels + y * mapped.RowPitch;
				for (UINT x = 0; x < copiedW; ++x) {
					const uint8_t* px = row + x * 4u;
					sum[0] += px[0];
					sum[1] += px[1];
					sum[2] += px[2];
					count += 1.0f;
				}
			}
			_context->Unmap(_crosshairStaging.Get(), 0);
			if (count < 1.0f) return;

			float target[3] = {
				1.0f - (sum[0] / count) / 255.0f,
				1.0f - (sum[1] / count) / 255.0f,
				1.0f - (sum[2] / count) / 255.0f
			};
			const float luma = 0.30f * target[0] + 0.59f * target[1] + 0.11f * target[2];
			const float mid = 1.0f - std::clamp(std::fabs(luma - 0.50f) / 0.22f, 0.0f, 1.0f);
			const float lift = mid * mid;
			target[0] += (1.0f - target[0]) * lift;
			target[1] += (1.0f - target[1]) * lift;
			target[2] += (1.0f - target[2]) * lift;

			const float dt = std::clamp(deltaTime, 0.0f, 0.1f);
			const float blend = _crosshairColorValid
				? 1.0f - std::exp(-dt * 7.0f)
				: 1.0f;
			_crosshairColor[0] += (target[0] - _crosshairColor[0]) * blend;
			_crosshairColor[1] += (target[1] - _crosshairColor[1]) * blend;
			_crosshairColor[2] += (target[2] - _crosshairColor[2]) * blend;
			_crosshairColorValid = true;
		}

		void pushConsoleMessage(const std::string& message) {
			if (_consoleHistoryCount < _consoleHistory.size()) {
				_consoleHistory[_consoleHistoryCount++] = message;
				return;
			}
			for (size_t i = 1; i < _consoleHistory.size(); ++i)
				_consoleHistory[i - 1] = std::move(_consoleHistory[i]);
			_consoleHistory.back() = message;
		}


		void setConsoleLine(const std::string& text) {
			const size_t n = (std::min)(text.size(), static_cast<size_t>(CONSOLE_LINE_CHARS));
			if (n > 0)
				std::memcpy(_consoleEditBuf.data(), text.data(), n);
			_consoleEditBuf[n] = '\0';
			_consoleBuffer.assign(_consoleEditBuf.data(), n);
		}

		void openConsole(bool prefSlash) {
			setConsoleLine(prefSlash ? "/" : "");
			_consoleFocusEdit = true;
			_consoleCursorToEnd = true;
			_consoleTabStem.clear();
			_consoleTabMatches.clear();
			_consoleTabIndex = -1;
			_window.setTextInputEnabled(true);
			_window.consumeTextInput();
			setScreen(ac::gameScreen::console);
		}

		void closeConsole() {
			_window.setTextInputEnabled(false);
			_consoleBuffer.clear();
			_consoleEditBuf[0] = 0;
			_consoleFocusEdit = false;
			_consoleCursorToEnd = false;
			_consoleTabStem.clear();
			_consoleTabMatches.clear();
			_consoleTabIndex = -1;
			if (_screen == ac::gameScreen::console)
				setScreen(ac::gameScreen::playing);
		}

		void resetConsoleTabComplete() {
			_consoleTabStem.clear();
			_consoleTabMatches.clear();
			_consoleTabIndex = -1;
		}

		std::vector<std::string> consoleCommandNames() const {
			std::vector<std::string> result{
				"help", "pos", "tp", "seed", "fly", "gamemode", "gm",
				"time", "weather", "biome", "locate", "give", "clear", "reload"
			};
			for (const std::string& alias : _modHost.registeredCommands().aliases())
				result.push_back(alias);
			std::sort(result.begin(), result.end());
			result.erase(std::unique(result.begin(), result.end()), result.end());
			return result;
		}

		static std::vector<std::string> biomeCommandNames() {
			return {
				"plains", "highlands", "desert", "alpine", "ocean", "forest",
				"taiga", "wetland", "savanna", "jungle", "beach", "tundra", "lake"
			};
		}

		bool locateNearestBiome(
			ac::TERRAIN_BIOME want,
			int32_t& outX,
			int32_t& outY,
			int32_t& outZ
		) const {
			const ac::terrainGenerator* generator = _world.generator();
			if (!generator) return false;
			const auto& p = _player.getCamera()._gpuData._position;
			const int32_t originX = static_cast<int32_t>(std::floor(p.x));
			const int32_t originZ = static_cast<int32_t>(std::floor(p.z));
			auto matches = [&](int32_t x, int32_t z) {
				return generator->sampleBiome(x, z)._biome == want;
			};

			int32_t foundX = originX;
			int32_t foundZ = originZ;
			bool found = matches(originX, originZ);
			constexpr int step = 48;
			constexpr int maxRing = 64;
			for (int ring = 1; !found && ring <= maxRing; ++ring) {
				const int32_t dist = ring * step;
				for (int i = -ring; i < ring && !found; ++i) {
					const int32_t along = i * step;
					const int32_t samples[4][2] = {
						{ originX + along, originZ - dist },
						{ originX + dist, originZ + along },
						{ originX - along, originZ + dist },
						{ originX - dist, originZ - along }
					};
					for (const auto& sample : samples) {
						if (matches(sample[0], sample[1])) {
							foundX = sample[0];
							foundZ = sample[1];
							found = true;
							break;
						}
					}
				}
			}
			if (!found) return false;

			int32_t bestX = foundX;
			int32_t bestZ = foundZ;
			int64_t bestD =
				(static_cast<int64_t>(foundX) - originX) * (static_cast<int64_t>(foundX) - originX) +
				(static_cast<int64_t>(foundZ) - originZ) * (static_cast<int64_t>(foundZ) - originZ);
			for (int dz = -step; dz <= step; dz += 8) {
				for (int dx = -step; dx <= step; dx += 8) {
					const int32_t x = foundX + dx;
					const int32_t z = foundZ + dz;
					if (!matches(x, z)) continue;
					const int64_t dxp = static_cast<int64_t>(x) - originX;
					const int64_t dzp = static_cast<int64_t>(z) - originZ;
					const int64_t d = dxp * dxp + dzp * dzp;
					if (d < bestD) {
						bestD = d;
						bestX = x;
						bestZ = z;
					}
				}
			}
			outX = bestX;
			outZ = bestZ;
			outY = static_cast<int32_t>(std::floor(generator->peekSurfaceHeight(bestX, bestZ) + 1.5f));
			return true;
		}

		void loadRecipesFromPacks(
			const ac::contentPackSet& packs,
			const ac::staticAssetManager& blocks,
			ac::recipeBook& target
		) const {
			bool loadedAny = false;
			for (const auto& [pack, recipePath] : packs.paths("recipes")) {
				(void)pack;
				const bool loaded = target.load(
					recipePath,
					[&blocks](const std::string& name) -> std::optional<ac::blockId> {
						try { return static_cast<ac::blockId>(blocks.getId(name)); }
						catch (...) { return std::nullopt; }
					},
					[&blocks](const std::function<void(ac::blockId, const std::string&)>& visit) {
						for (uint32_t id : blocks.ids()) {
							const ac::blockDefinition* definition = blocks.get(id);
							if (definition)
								visit(static_cast<ac::blockId>(id), definition->_name);
						}
					},
					loadedAny);
				if (!loaded)
					throw std::runtime_error("No valid recipes loaded from " + recipePath.string());
				loadedAny = true;
			}
			if (!loadedAny)
				throw std::runtime_error("No content pack provides 'recipes'");
		}

		std::vector<std::string> reloadAreaNames() const {
			std::vector<std::string> result = _contentPacks.kinds();
			for (const ac::contentPack& pack : _contentPacks.ordered())
				if (pack.entrypoints.wasm || pack.entrypoints.native) {
					result.push_back("code");
					break;
				}
			return result;
		}

		std::vector<std::string> resolveReloadAreas(
			const std::vector<std::string>& arguments,
			const ac::contentPackSet& packs
		) const {
			std::vector<std::string> available = packs.kinds();
			for (const ac::contentPack& pack : packs.ordered())
				if (pack.entrypoints.wasm || pack.entrypoints.native) {
					available.push_back("code");
					break;
				}
			std::unordered_map<std::string, std::string> canonical;
			for (const std::string& area : available) {
				std::string lower = area;
				for (char& c : lower)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				canonical.emplace(std::move(lower), area);
			}
			if (arguments.empty()) return available;

			std::vector<std::string> result;
			for (std::string argument : arguments) {
				while (!argument.empty() && argument.back() == ',') argument.pop_back();
				for (char& c : argument)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (argument == "all") return available;
				const auto found = canonical.find(argument);
				if (found == canonical.end()) {
					std::string message = "unknown reload area '" + argument + "'. available:";
					for (const std::string& area : available) message += " " + area;
					throw std::runtime_error(message);
				}
				if (std::find(result.begin(), result.end(), found->second) == result.end())
					result.push_back(found->second);
			}
			return result;
		}

		void reloadResources(const std::vector<std::string>& arguments) {
			ac::contentPackSet stagedPacks =
				ac::contentPackSet::discover("assets/pack.json", "mods");
			const std::vector<std::string> areas = resolveReloadAreas(arguments, stagedPacks);
			const auto selected = [&areas](const char* area) {
				return std::find(areas.begin(), areas.end(), area) != areas.end();
			};
			static const std::unordered_set<std::string> supported{
				"models", "blocks", "recipes", "entities", "registries",
				"terrainBlocks", "biomes", "code"
			};
			for (const std::string& area : areas)
				if (!supported.contains(area))
					throw std::runtime_error(
						"reload area '" + area + "' is declared by pack.json but has no loader");

			std::optional<ac::modelManager> stagedModels;
			if (selected("models")) {
				stagedModels.emplace();
				for (const auto& [pack, path] : stagedPacks.paths("models")) {
					(void)pack;
					stagedModels->load(path.string());
				}
			}
			ac::modelManager& candidateModels = stagedModels ? *stagedModels : _models;

			std::optional<ac::staticAssetManager> stagedBlocks;
			std::optional<ac::textureLibrary> stagedTextures;
			std::optional<ac::blockTextureSet> stagedBlockTextures;
			std::optional<ac::itemIconAtlas> stagedItemIcons;
			if (selected("blocks")) {
				stagedBlocks.emplace();
				for (const auto& [pack, path] : stagedPacks.paths("blocks"))
					stagedBlocks->load(path.string(), pack->id);
				stagedBlocks->validateReferences(candidateModels);
				ac::registerBlockStateVariants(*stagedBlocks);
				for (uint32_t id : _blocks.ids()) {
					if (stagedBlocks->persistentName(id) != _blocks.persistentName(id))
						throw std::runtime_error(
							"blocks reload changes or removes runtime id " + std::to_string(id) +
							" ('" + _blocks.persistentName(id) + "')");
				}
				for (const auto& [alias, name] : APPLICATION_CONFIG.blockAliases) {
					(void)alias;
					stagedBlocks->getId(name);
				}
				stagedTextures.emplace();
				stagedBlockTextures.emplace();
				stagedBlockTextures->load(
					_device, *stagedTextures, stagedBlocks->texturePaths());
				stagedBlockTextures->loadEmissions(_device, *stagedBlocks);
				stagedBlockTextures->loadMaterialProperties(_device, *stagedBlocks);
				stagedBlockTextures->loadColormaps(
					_device, *stagedTextures,
					"assets/textures/colormap/grass.png",
					"assets/textures/colormap/foliage.png");
				stagedItemIcons.emplace();
				stagedItemIcons->bake(
					_device, _context, *stagedBlocks, candidateModels, *stagedBlockTextures);
			}
			else if (stagedModels) {
				_blocks.validateReferences(candidateModels);
			}
			const ac::staticAssetManager& candidateBlocks = stagedBlocks ? *stagedBlocks : _blocks;
			if (stagedModels && !stagedItemIcons) {
				stagedItemIcons.emplace();
				stagedItemIcons->bake(
					_device, _context, _blocks, candidateModels, _blockTextures);
			}

			std::optional<ac::recipeBook> stagedRecipes;
			if (selected("recipes")) {
				stagedRecipes.emplace();
				loadRecipesFromPacks(stagedPacks, candidateBlocks, *stagedRecipes);
			}

			std::unique_ptr<ac::livingEntitySystem> stagedEntities;
			if (selected("entities")) {
				std::vector<std::filesystem::path> files;
				for (const auto& [pack, path] : stagedPacks.paths("entities")) {
					(void)pack;
					files.push_back(path);
				}
				if (files.empty())
					throw std::runtime_error("No content pack provides 'entities'");
				const bool gpuPaths = _livingEntities && _livingEntities->gpuEnabled();
				stagedEntities = std::make_unique<ac::livingEntitySystem>(
					_device, _context, files);
				stagedEntities->setGpuEnabled(gpuPaths);
			}

			std::optional<ac::terrainBlockPalette> stagedTerrainBlocks;
			std::optional<ac::terrainBiomeConfig> stagedBiomes;
			if (selected("terrainBlocks"))
				stagedTerrainBlocks = ac::terrainBlockPalette::load(
					stagedPacks.singleton("terrainBlocks"), candidateBlocks);
			if (selected("biomes"))
				stagedBiomes = ac::terrainBiomeConfig::load(
					stagedPacks.singleton("biomes"), candidateBlocks);

			const bool geometryChanged = stagedModels.has_value() || stagedBlocks.has_value();
			const bool worldGenerationChanged =
				stagedTerrainBlocks.has_value() || stagedBiomes.has_value();
			bool workersSuspended = false;
			try {
				if (geometryChanged || worldGenerationChanged) {
					_streamer.suspendForResourceReload();
					workersSuspended = true;
				}
				if (stagedModels) _models = std::move(*stagedModels);
				if (stagedBlocks) {
					_blocks = std::move(*stagedBlocks);
					_textures = std::move(*stagedTextures);
					_blockTextures = std::move(*stagedBlockTextures);
					bindBlockAliases(_blocks);
					_world.configureContentRegistry(_blocks);
					if (const ac::blockDefinition* water = _blocks.get(ac::WATER_BLOCK_TYPE))
						_waterMaterial = water->materialForFace(ac::BLOCK_FACE_UP);
					rebuildChooserCatalog();
				}
				if (stagedItemIcons) _itemIcons = std::move(*stagedItemIcons);
				if (stagedRecipes) _recipes = std::move(*stagedRecipes);
				if (selected("entities")) _livingEntities = std::move(stagedEntities);
				if (stagedTerrainBlocks || stagedBiomes) {
					ac::terrainBlockPalette blocks = stagedTerrainBlocks
						? std::move(*stagedTerrainBlocks) : _world.terrainBlocks();
					ac::terrainBiomeConfig biomes = stagedBiomes
						? std::move(*stagedBiomes) : _world.terrainBiomes();
					_world.configureTerrain(std::move(blocks), std::move(biomes));
				}
				_contentPacks = std::move(stagedPacks);
				if (selected("registries") || selected("code")) {
					_modHost.stop();
					_modHost.start(_contentPacks);
				}
				if (workersSuspended) {
					_streamer.resumeAfterResourceReload(_context, stagedBlocks.has_value());
					workersSuspended = false;
				}
			}
			catch (...) {
				if (workersSuspended)
					_streamer.resumeAfterResourceReload(_context, stagedBlocks.has_value());
				throw;
			}

			std::string message = "reloaded:";
			for (const std::string& area : areas) message += " " + area;
			pushConsoleMessage(message);
			_modHost.events().publish(ac::modding::resourcesReloadedEvent{});
		}

		void applyConsoleTabComplete() {
			std::string line = _consoleEditBuf.data();
			while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
				line.erase(line.begin());

			// Determine the token being completed (after last space), and whether
			// we are completing a command name or a /give block argument.
			const bool hasSlash = !line.empty() && line.front() == '/';
			std::string prefix = line;
			std::string command;
			bool completingArg = false;
			if (hasSlash) {
				const size_t space = line.find(' ');
				if (space == std::string::npos) {
					prefix = line.substr(1);
					completingArg = false;
				}
				else {
					command = line.substr(1, space - 1);
					for (char& c : command)
						c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					prefix = line.substr(space + 1);
					completingArg = true;
				}
			}

			const bool sameStem = !_consoleTabMatches.empty() && prefix == _consoleTabStem;
			if (!sameStem) {
				_consoleTabMatches.clear();
				_consoleTabIndex = -1;
				_consoleTabStem = prefix;
				std::string needle = prefix;
				for (char& c : needle)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

				if (!completingArg) {
					for (const std::string& name : consoleCommandNames()) {
						if (needle.empty() || name.rfind(needle, 0) == 0)
							_consoleTabMatches.push_back(name);
					}
				}
				else if (command == "give" || command == "gamemode" || command == "gm" ||
					command == "time" || command == "weather" || command == "locate" ||
					command == "reload") {
					if (command == "give") {
						for (uint32_t id : _blocks.ids()) {
							const ac::blockDefinition* definition = _blocks.get(id);
							if (!definition || definition->_name.empty() ||
								isInternalItemVariant(static_cast<ac::blockId>(id))) continue;
							std::string name = definition->_name;
							std::string lower = name;
							for (char& c : lower)
								c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
							if (needle.empty() || lower.rfind(needle, 0) == 0)
								_consoleTabMatches.push_back(name);
						}
					}
					else if (command == "gamemode" || command == "gm") {
						for (const char* mode : { "survival", "creative", "spectator" }) {
							std::string name = mode;
							if (needle.empty() || name.rfind(needle, 0) == 0)
								_consoleTabMatches.push_back(name);
						}
					}
					else if (command == "time") {
						for (const char* t : { "day", "night", "noon", "midnight", "sunrise", "sunset" }) {
							std::string name = t;
							if (needle.empty() || name.rfind(needle, 0) == 0)
								_consoleTabMatches.push_back(name);
						}
					}
					else if (command == "weather") {
						for (const char* t : { "clear", "rain", "storm", "thunder", "snow", "auto" }) {
							std::string name = t;
							if (needle.empty() || name.rfind(needle, 0) == 0)
								_consoleTabMatches.push_back(name);
						}
					}
					else if (command == "locate") {
						const size_t restSpace = prefix.find(' ');
						if (restSpace == std::string::npos) {
							const std::string token = "biome";
							if (needle.empty() || token.rfind(needle, 0) == 0)
								_consoleTabMatches.push_back(token);
						}
						else {
							std::string biomeNeedle = prefix.substr(restSpace + 1);
							for (char& c : biomeNeedle)
								c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
							for (const std::string& name : biomeCommandNames()) {
								if (biomeNeedle.empty() || name.rfind(biomeNeedle, 0) == 0)
									_consoleTabMatches.push_back(name);
							}
						}
					}
					else if (command == "reload") {
						std::string areaNeedle = needle;
						const size_t lastSpace = areaNeedle.find_last_of(" \t");
						if (lastSpace != std::string::npos)
							areaNeedle = areaNeedle.substr(lastSpace + 1);
						for (const std::string& area : reloadAreaNames()) {
							std::string lower = area;
							for (char& c : lower)
								c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
							if (areaNeedle.empty() || lower.rfind(areaNeedle, 0) == 0)
								_consoleTabMatches.push_back(area);
						}
					}
				}
				std::sort(_consoleTabMatches.begin(), _consoleTabMatches.end());
				_consoleTabMatches.erase(
					std::unique(_consoleTabMatches.begin(), _consoleTabMatches.end()),
					_consoleTabMatches.end());
			}

			if (_consoleTabMatches.empty()) {
				pushConsoleMessage("no matches");
				return;
			}

			_consoleTabIndex = (_consoleTabIndex + 1) % static_cast<int>(_consoleTabMatches.size());
			const std::string& pick = _consoleTabMatches[static_cast<size_t>(_consoleTabIndex)];
			std::string completed;
			if (!completingArg) {
				completed = "/" + pick;
			}
			else {
				const size_t firstSpace = line.find(' ');
				const size_t secondSpace = line.find(' ', firstSpace + 1);
				if (command == "reload" && secondSpace != std::string::npos) {
					const size_t lastSpace = line.find_last_of(" \t");
					completed = line.substr(0, lastSpace + 1) + pick;
				}
				else if (command == "locate" && secondSpace != std::string::npos)
					completed = line.substr(0, secondSpace + 1) + pick;
				else
					completed = line.substr(0, firstSpace + 1) + pick;
			}
			setConsoleLine(completed);
			_consoleTabStem = (command == "locate" && completingArg &&
				line.find(' ', line.find(' ') + 1) != std::string::npos)
				? (std::string("biome ") + pick)
				: pick;
			_consoleFocusEdit = true;
			_consoleCursorToEnd = true;
			if (_consoleTabMatches.size() > 1) {
				char msg[128];
				std::snprintf(msg, sizeof(msg), "tab %d/%zu: %s",
					_consoleTabIndex + 1, _consoleTabMatches.size(), pick.c_str());
				pushConsoleMessage(msg);
			}
		}

		std::optional<ac::blockId> resolveBlockArgument(const std::string& token) const {
			if (token.empty())
				return std::nullopt;
			char* end = nullptr;
			const unsigned long numeric = std::strtoul(token.c_str(), &end, 10);
			if (end != token.c_str() && *end == '\0' && numeric <= ac::BLOCK_TYPE_MASK) {
				const auto id = static_cast<ac::blockId>(numeric);
				if (id == 0 || _blocks.get(id))
					return id;
				return std::nullopt;
			}
			for (uint32_t id : _blocks.ids()) {
				const ac::blockDefinition* definition = _blocks.get(id);
				if (definition && definition->_name == token)
					return static_cast<ac::blockId>(id);
			}
			return std::nullopt;
		}

		void executeConsoleCommand(const std::string& line) {
			std::string trimmed = line;
			while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t'))
				trimmed.erase(trimmed.begin());
			while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t'))
				trimmed.pop_back();
			if (trimmed.empty())
				return;

			pushConsoleMessage("> " + trimmed);
			if (trimmed.front() != '/') {
				pushConsoleMessage("Commands start with /. Try /help");
				return;
			}

			ac::modding::callbackCommandOutput modOutput([this](std::string_view message) {
				pushConsoleMessage(std::string(message));
			});
			const ac::modding::commandExecution modCommand =
				_modHost.commands().execute(trimmed, _modHost, modOutput);
			if (modCommand != ac::modding::commandExecution::notFound) return;

			std::istringstream stream(trimmed.substr(1));
			std::string command;
			stream >> command;
			for (char& c : command)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

			auto fail = [&](const std::string& message) {
				pushConsoleMessage(message);
			};

			if (command == "help") {
				pushConsoleMessage("/tp x y z  /pos  /seed [n]  /fly");
				pushConsoleMessage("/gamemode survival|creative|spectator");
				pushConsoleMessage("/time day|night|noon|N  /weather clear|rain|storm|snow|auto");
				pushConsoleMessage("/biome  /locate biome name");
				pushConsoleMessage("/give id|name  /clear  /reload [area ...]  /help");
				pushConsoleMessage("Q drop item, Ctrl+Q drop stack");
				return;
			}

			if (command == "reload") {
				std::vector<std::string> areas;
				std::string area;
				while (stream >> area) areas.push_back(std::move(area));
				try {
					reloadResources(areas);
				}
				catch (const std::exception& error) {
					fail(std::string("reload failed: ") + error.what());
				}
				return;
			}

			if (command == "pos") {
				const auto& p = _player.getCamera()._gpuData._position;
				char buffer[96];
				std::snprintf(buffer, sizeof(buffer), "pos %.2f %.2f %.2f", p.x, p.y, p.z);
				pushConsoleMessage(buffer);
				return;
			}

			if (command == "tp") {
				float x = 0.0f, y = 0.0f, z = 0.0f;
				if (!(stream >> x >> y >> z)) {
					fail("usage: /tp <x> <y> <z>");
					return;
				}
				_spawnPending = false;
				_restoredPlayerPendingValidation = false;
				_player.teleport({ x, y, z });
				_player.clearVelocity();
				char buffer[96];
				std::snprintf(buffer, sizeof(buffer), "teleported to %.1f %.1f %.1f", x, y, z);
				pushConsoleMessage(buffer);
				return;
			}

			if (command == "seed") {
				uint64_t seed = 0;
				if (stream >> seed) {
					const std::string worldName = ac::loadWorldLevelInfo(_world.path()).name;
					_streamer.discardAllChunks();
					_world.recreate(seed);
					_blockEntities.reset();
					_items.clear();
					_player.clearVelocity();
					_player.teleport({ 0.0f, 120.0f, 0.0f });
					resetSpawnSearch();
					_weather.reset(_world.seed());
					_restoredPlayerPendingValidation = false;
					_voxelLightBuffer.update(_context, {
						ac::VOXEL_LIGHT_TABLE_SIZE - 1u,
						0u,
						_lightingQuality,
						_waterMaterial,
						static_cast<uint32_t>(_world.seed()),
						{}
					});
					pushConsoleMessage("world recreated with seed " + std::to_string(_world.seed()));
					applyDefaultHotbar();
					persistWorldSettings();
					persistPlayerInventory();
					_items.saveToWorld(_world);
					ac::savedWorldInfo info;
					info.name = worldName.empty() ? _world.path().filename().string() : worldName;
					info.path = _world.path();
					info.seed = _world.seed();
					info.created = ac::currentUnixTime();
					info.lastPlayed = info.created;
					info.dayTimeSeconds = std::fmod(
						(std::max)(_dayTimeSeconds, 0.0f), DAY_LENGTH_SECONDS);
					ac::saveWorldLevelInfo(info);
				}
				else {
					pushConsoleMessage("seed " + std::to_string(_world.seed()));
				}
				return;
			}

			if (command == "fly") {
				toggleFlyMode();
				pushConsoleMessage(isSpectator() ? "fly enabled" : "fly disabled");
				return;
			}

			if (command == "gamemode" || command == "gm") {
				std::string mode;
				if (!(stream >> mode)) {
					fail("usage: /gamemode survival|creative|spectator");
					return;
				}
				for (char& c : mode)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				ac::gameMode next = _gameMode;
				if (mode == "spectator" || mode == "sp" || mode == "3" || mode == "fly")
					next = ac::gameMode::spectator;
				else if (mode == "creative" || mode == "c" || mode == "1")
					next = ac::gameMode::creative;
				else if (mode == "survival" || mode == "s" || mode == "0" || mode == "walk")
					next = ac::gameMode::survival;
				else {
					fail("unknown mode (survival|creative|spectator)");
					return;
				}
				setGameMode(next);
				return;
			}

			if (command == "time") {
				std::string arg;
				if (!(stream >> arg)) {
					char buffer[64];
					std::snprintf(buffer, sizeof(buffer), "time %.1f / %.0f",
						_dayTimeSeconds, DAY_LENGTH_SECONDS);
					pushConsoleMessage(buffer);
					return;
				}
				for (char& c : arg)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (arg == "day" || arg == "noon")
					_dayTimeSeconds = DAY_LENGTH_SECONDS * 0.25f;
				else if (arg == "night" || arg == "midnight")
					_dayTimeSeconds = DAY_LENGTH_SECONDS * 0.75f;
				else if (arg == "sunrise" || arg == "dawn")
					_dayTimeSeconds = 0.0f;
				else if (arg == "sunset" || arg == "dusk")
					_dayTimeSeconds = DAY_LENGTH_SECONDS * 0.5f;
				else {
					char* end = nullptr;
					const float value = std::strtof(arg.c_str(), &end);
					if (end == arg.c_str() || *end != '\0') {
						fail("usage: /time day|night|noon|N");
						return;
					}
					_dayTimeSeconds = std::fmod(std::max(value, 0.0f), DAY_LENGTH_SECONDS);
				}
				char buffer[64];
				std::snprintf(buffer, sizeof(buffer), "time set to %.1f", _dayTimeSeconds);
				pushConsoleMessage(buffer);
				return;
			}

			if (command == "weather") {
				std::string arg;
				if (!(stream >> arg)) {
					char buffer[64];
					std::snprintf(buffer, sizeof(buffer), "weather %s",
						ac::weatherSystem::name(_weather.state().kind));
					pushConsoleMessage(buffer);
					return;
				}
				for (char& c : arg)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (arg == "clear" || arg == "sun")
					_weather.setForced(ac::weatherKind::clear);
				else if (arg == "rain")
					_weather.setForced(ac::weatherKind::rain);
				else if (arg == "storm" || arg == "thunder")
					_weather.setForced(ac::weatherKind::storm);
				else if (arg == "snow")
					_weather.setForced(ac::weatherKind::snow);
				else if (arg == "auto" || arg == "cycle")
					_weather.clearForced();
				else {
					fail("usage: /weather clear|rain|storm|snow|auto");
					return;
				}
				pushConsoleMessage(std::string("weather ") + arg);
				updateWeather(0.0f);
				return;
			}

			if (command == "biome") {
				const auto& p = _player.getCamera()._gpuData._position;
				const int32_t worldX = static_cast<int32_t>(std::floor(p.x));
				const int32_t worldZ = static_cast<int32_t>(std::floor(p.z));
				ac::TERRAIN_BIOME biome = _world.terrainBiomes().id("plains");
				if (const ac::terrainGenerator* generator = _world.generator())
					biome = generator->sampleBiome(worldX, worldZ)._biome;
				char buffer[128];
				std::snprintf(buffer, sizeof(buffer), "biome %s  weather %s%s",
					_world.terrainBiomes().displayName(biome),
					ac::weatherSystem::name(_weather.state().kind),
					_weather.forced() ? " (forced)" : "");
				pushConsoleMessage(buffer);
				return;
			}

			if (command == "locate") {
				std::string kind;
				std::string name;
				stream >> kind >> name;
				for (char& c : kind)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (kind.empty()) {
					fail("usage: /locate biome <name>");
					std::string available = "biomes:";
					for (const auto& biome : _world.terrainBiomes().definitions)
						available += " " + biome.name;
					fail(available);
					return;
				}
				std::string biomeToken = kind;
				if (kind == "biome") {
					if (name.empty()) {
						fail("usage: /locate biome <name>");
						return;
					}
					biomeToken = name;
				}
				ac::TERRAIN_BIOME want = _world.terrainBiomes().id("plains");
				if (!_world.terrainBiomes().parse(biomeToken.c_str(), want)) {
					fail("unknown biome '" + biomeToken + "'");
					return;
				}
				int32_t x = 0, y = 0, z = 0;
				if (!locateNearestBiome(want, x, y, z)) {
					fail(std::string("could not find ") + _world.terrainBiomes().displayName(want) + " nearby");
					return;
				}
				const auto& p = _player.getCamera()._gpuData._position;
				const float dx = static_cast<float>(x) - p.x;
				const float dz = static_cast<float>(z) - p.z;
				const float dist = std::sqrt(dx * dx + dz * dz);
				char buffer[160];
				std::snprintf(buffer, sizeof(buffer),
					"located %s at %d %d %d (%.0fm)",
					_world.terrainBiomes().displayName(want), x, y, z, dist);
				pushConsoleMessage(buffer);
				return;
			}

			if (command == "give") {
				if (!isCreative()) {
					fail("/give requires creative mode");
					return;
				}
				std::string token;
				if (!(stream >> token)) {
					fail("usage: /give <id|name>");
					return;
				}
				const auto block = resolveBlockArgument(token);
				if (!block || *block == 0) {
					fail("unknown block '" + token + "'");
					return;
				}
				_hotbarBlocks[_selectedHotbarSlot] = *block;
				_hotbarCounts[_selectedHotbarSlot] = itemStackLimit(_hotbarBlocks[_selectedHotbarSlot]);
				_selectedBlock = *block;
				const ac::blockDefinition* definition = _blocks.get(*block);
				pushConsoleMessage(
					"gave " + (definition ? definition->_name : std::to_string(*block)) +
					" x64 to hotbar " + std::to_string(_selectedHotbarSlot + 1));
				return;
			}

			if (command == "clear") {
				_hotbarBlocks.fill(0);
				_hotbarCounts.fill(0);
				_inventoryBlocks.fill(0);
				_inventoryCounts.fill(0);
				_cursorItem = 0;
				_cursorCount = 0;
				_selectedBlock = 0;
				pushConsoleMessage("inventory cleared");
				return;
			}

			fail("unknown command. Try /help");
		}

		void updateConsole() {
			// Console input is handled by Nuklear.
			if (_screen != ac::gameScreen::console)
				return;
		}

		void setScreen(ac::gameScreen screen) {
			const bool leavingCurrent = screen != _screen;
			if (_screen == ac::gameScreen::settings && leavingCurrent) {
				persistUserSettings();
				persistWorldSettings();
			}
			if (_screen == ac::gameScreen::chest && leavingCurrent) {
				_audio.play(ac::soundId::chestClose,
					ac::soundPos::block(_openChestBlock.x, _openChestBlock.y, _openChestBlock.z, 20.0f));
				endInventoryPaint();
				returnCursorToInventory();
				_blockEntities.save();
			}
			if (_screen == ac::gameScreen::furnace && leavingCurrent) {
				endInventoryPaint();
				returnCursorToInventory();
				_blockEntities.save();
			}
			if (_screen == ac::gameScreen::inventory && leavingCurrent) {
				endInventoryPaint();
				dumpCraftGrid(_craft2Blocks.data(), _craft2Counts.data(), HUD_CRAFT2_SLOTS);
				returnCursorToInventory();
				_chooserSearchFocus = false;
			}
			if (_screen == ac::gameScreen::crafting && leavingCurrent) {
				endInventoryPaint();
				dumpCraftGrid(_craft3Blocks.data(), _craft3Counts.data(), HUD_CRAFT3_SLOTS);
				returnCursorToInventory();
			}
			if (_screen == ac::gameScreen::console && screen != ac::gameScreen::console)
				_window.setTextInputEnabled(false);
			_screen = screen;
			_inventoryOpen =
				screen == ac::gameScreen::inventory || screen == ac::gameScreen::chest ||
				screen == ac::gameScreen::crafting || screen == ac::gameScreen::furnace;
			if (screen == ac::gameScreen::inventory && !isCreative())
				_inventoryTab = inventoryTab::storage;
			updateTextInput();
			applyCursorCapture(ac::gameScreenCapturesCursor(screen));
		}

		void updateTextInput() {
			_window.setTextInputEnabled(
				_screen == ac::gameScreen::console ||
				_screen == ac::gameScreen::createWorld ||
				(_screen == ac::gameScreen::inventory && isCreative() &&
					_inventoryTab == inventoryTab::chooser));
		}

		void setInventoryOpen(bool open) {
			if (open && !isCreative()) {
				_inventoryTab = inventoryTab::storage;
			}
			setScreen(open ? ac::gameScreen::inventory : ac::gameScreen::playing);
		}

		void playUiClick() {
			_audio.play(ac::soundId::uiClick, 0.85f);
		}

		void addCameraShake(float amount) {
			_cameraShake = (std::min)(_cameraShake + amount, 0.42f);
		}

		dx::XMFLOAT4 particleColorFor(ac::blockId id) const {
			const ac::blockId type = ac::blockType(id);
			if (type == BLOCK_CHEST) return { 0.72f, 0.48f, 0.22f, 1.0f };
			if (isDoorType(type))
				return { 0.62f, 0.44f, 0.22f, 1.0f };
			if (type == ac::WATER_BLOCK_TYPE) return { 0.20f, 0.55f, 0.95f, 0.95f };
			if (type == 1) return { 0.55f, 0.55f, 0.55f, 1.0f }; // stone
			if (type == 2) return { 0.45f, 0.30f, 0.18f, 1.0f }; // dirt
			if (type == 3) return { 0.30f, 0.62f, 0.22f, 1.0f }; // grass
			if (type == 6) return { 0.86f, 0.78f, 0.48f, 1.0f }; // sand
			if (type == 7) return { 0.92f, 0.95f, 1.0f, 1.0f }; // snow
			if (type == 8) return { 0.28f, 0.28f, 0.32f, 1.0f }; // deepslate
			if (type == 19 || type == 45) return { 0.78f, 0.78f, 0.82f, 1.0f }; // iron
			if (type == 20 || type == 46) return { 0.95f, 0.78f, 0.22f, 1.0f }; // gold
			if (type == 21 || type == 47) return { 0.45f, 0.85f, 0.92f, 1.0f }; // diamond
			return { 0.62f, 0.58f, 0.52f, 1.0f };
		}

		void emitBlockBurst(const dx::XMINT3& block, ac::blockId id, int count, float speed) {
			_particles.emitBurst(
				{
					static_cast<float>(block.x) + 0.5f,
					static_cast<float>(block.y) + 0.5f,
					static_cast<float>(block.z) + 0.5f
				},
				particleColorFor(id),
				count,
				speed,
				0.75f,
				0.16f
			);
		}

		void touchWorldLevelInfo() {
			if (!_world.isOpen())
				return;
			ac::savedWorldInfo info = ac::loadWorldLevelInfo(_world.path());
			info.path = _world.path();
			info.seed = _world.seed();
			info.dayTimeSeconds = std::fmod(
				(std::max)(_dayTimeSeconds, 0.0f), DAY_LENGTH_SECONDS);
			const int64_t now = ac::currentUnixTime();
			if (info.created <= 0)
				info.created = now;
			info.lastPlayed = now;
			if (info.name.empty())
				info.name = _world.path().filename().string();
			ac::saveWorldLevelInfo(info);
		}

		void saveCurrentWorld(bool flushChunks = true) {
			if (!_worldSessionOpen || !_world.isOpen())
				return;
			_modHost.events().publish(ac::modding::worldSavingEvent{});
			capturePlayerState();
			_items.saveToWorld(_world);
			_blockEntities.save();
			persistWorldSettings();
			persistPlayerInventory();
			if (flushChunks)
				_streamer.saveLoadedChunks();
			_world.saveWorld();
			touchWorldLevelInfo();
			_lastWorldPath = _world.path().string();
			persistUserSettings();
			_autosaveTimer = 0.0f;
			_modHost.events().publish(ac::modding::worldSavedEvent{});
		}

		void resetPlayerForNewWorld() {
			stopWeatherAudio();
			_dayCycleIndex = 2;
			_dayTimeSeconds = 600.0f;
			_gameMode = ac::gameMode::survival;
			_previousGameMode = ac::gameMode::survival;
			syncSpectatorFlag();
			_health = ac::PLAYER_MAX_HEALTH;
			_hunger = ac::PLAYER_MAX_HUNGER;
			_saturation = ac::PLAYER_MAX_SATURATION;
			_exhaustion = 0.0f;
			_air = ac::PLAYER_MAX_AIR;
			resetFallTracking();
			_hotbarBlocks.fill(0);
			_hotbarCounts.fill(0);
			_inventoryBlocks.fill(0);
			_inventoryCounts.fill(0);
			_selectedBlock = 0;
			_selectedHotbarSlot = 0;
			_cameraMode = 0;
			_bodycamInitialized = false;
			_player.clearVelocity();
			_player.teleport({ 0.0f, 120.0f, 0.0f });
			_wasInWater = false;
			resetSpawnSearch();
			_restoredPlayerPendingValidation = false;
			_weather.reset(_world.seed());
			_weatherWetness = 0.0f;
			_weatherSurfaceRefresh = 0.0f;
		}

		void bindOpenWorld() {
			_blockEntities.setPath(_world.path());
			_lastWorldPath = _world.path().string();
			_worldSessionOpen = true;
			_modHost.events().publish(ac::modding::worldOpenedEvent{ _world.seed() });
			if (_livingEntities) _livingEntities->clear();
			_autosaveTimer = 0.0f;
			_voxelLightBuffer.update(_context, {
				ac::VOXEL_LIGHT_TABLE_SIZE - 1u,
				0u,
				_lightingQuality,
				_waterMaterial,
				static_cast<uint32_t>(_world.seed()),
				{}
			});
			persistUserSettings();
		}

		void refreshWorldList() {
			_worldList = ac::listSavedWorlds();
			if (_worldList.empty())
				_selectedWorldIndex = 0;
			else if (_selectedWorldIndex < 0 || _selectedWorldIndex >= static_cast<int>(_worldList.size()))
				_selectedWorldIndex = 0;
			_confirmDeleteIndex = -1;
		}

		void openWorldSelect(bool clearError = true) {
			refreshWorldList();
			if (clearError)
				_worldMenuError.clear();
			if (!_lastWorldPath.empty()) {
				for (int index = 0; index < static_cast<int>(_worldList.size()); ++index) {
					if (_worldList[static_cast<size_t>(index)].path == std::filesystem::path(_lastWorldPath)) {
						_selectedWorldIndex = index;
						break;
					}
				}
			}
			setScreen(ac::gameScreen::worldSelect);
		}

		void closeWorldToMenu() {
			stopWeatherAudio();
			saveCurrentWorld(true);
			_streamer.unloadAllChunks(false);
			_items.clear();
			if (_livingEntities) _livingEntities->clear();
			_blockEntities.clearMemory();
			_world.closeSession();
			_worldSessionOpen = false;
			_modHost.events().publish(ac::modding::worldClosedEvent{});
			_spawnPending = false;
			_restoredPlayerPendingValidation = false;
			openWorldSelect();
		}

		void saveAndQuitToDesktop() {
			saveCurrentWorld(true);
			_window.requestClose();
		}

		void playSelectedWorld() {
			if (_selectedWorldIndex < 0 || _selectedWorldIndex >= static_cast<int>(_worldList.size()))
				return;
			loadExistingWorld(_worldList[static_cast<size_t>(_selectedWorldIndex)].path);
		}

		void loadExistingWorld(const std::filesystem::path& path) {
			_worldMenuError.clear();
			if (_worldSessionOpen) {
				saveCurrentWorld(true);
				_modHost.events().publish(ac::modding::worldClosedEvent{});
			}
			_streamer.unloadAllChunks(false);
			_items.clear();
			if (_livingEntities) _livingEntities->clear();
			if (!_world.open(path.string())) {
				_worldMenuError = "Failed to open world.";
				_worldSessionOpen = false;
				return;
			}
			const ac::savedWorldInfo savedInfo = ac::loadWorldLevelInfo(_world.path());
			_dayTimeSeconds = std::fmod(
				(std::max)(savedInfo.dayTimeSeconds, 0.0f), DAY_LENGTH_SECONDS);
			bindOpenWorld();
			loadAndApplyWorldSettings();
			if (!restorePlayerState()) {
				resetSpawnSearch();
				_restoredPlayerPendingValidation = false;
				loadAndApplyPlayerInventory();
			}
			_items.loadFromWorld(_world);
			touchWorldLevelInfo();
			setScreen(ac::gameScreen::playing);
		}

		void beginCreateWorldScreen() {
			refreshWorldList();
			const std::string name = ac::uniqueWorldDisplayName("New World", _worldList);
			std::snprintf(_newWorldName.data(), _newWorldName.size(), "%s", name.c_str());
			_newWorldSeed[0] = '\0';
			_createWorldNameFocus = true;
			_worldMenuError.clear();
			setScreen(ac::gameScreen::createWorld);
		}

		void createWorldFromMenu() {
			std::string name = _newWorldName.data();
			while (!name.empty() && name.front() == ' ')
				name.erase(name.begin());
			while (!name.empty() && name.back() == ' ')
				name.pop_back();
			if (name.empty()) {
				_worldMenuError = "Enter a world name.";
				return;
			}
			uint64_t seed = static_cast<uint64_t>(
				std::chrono::steady_clock::now().time_since_epoch().count());
			const std::string seedText = _newWorldSeed.data();
			if (!seedText.empty()) {
				char* end = nullptr;
				const unsigned long long parsed = std::strtoull(seedText.c_str(), &end, 10);
				if (end == seedText.c_str() || *end != '\0') {
					_worldMenuError = "Seed must be a number, or leave it blank.";
					return;
				}
				seed = static_cast<uint64_t>(parsed);
			}

			if (_worldSessionOpen) {
				saveCurrentWorld(true);
				_modHost.events().publish(ac::modding::worldClosedEvent{});
			}
			_streamer.unloadAllChunks(false);
			_items.clear();
			if (_livingEntities) _livingEntities->clear();
			const std::filesystem::path folder = ac::uniqueWorldDirectory(name);
			if (!_world.create(folder.string(), seed)) {
				_worldMenuError = "Could not create the world folder.";
				_worldSessionOpen = false;
				return;
			}
			_dayTimeSeconds = 600.0f;
			ac::savedWorldInfo info;
			info.name = name;
			info.path = folder;
			info.seed = seed;
			info.created = ac::currentUnixTime();
			info.lastPlayed = info.created;
			info.dayTimeSeconds = _dayTimeSeconds;
			ac::saveWorldLevelInfo(info);
			bindOpenWorld();
			resetPlayerForNewWorld();
			persistWorldSettings();
			persistPlayerInventory();
			_items.saveToWorld(_world);
			setScreen(ac::gameScreen::playing);
		}

		void deleteSelectedWorld() {
			if (_selectedWorldIndex < 0 || _selectedWorldIndex >= static_cast<int>(_worldList.size()))
				return;
			const ac::savedWorldInfo info = _worldList[static_cast<size_t>(_selectedWorldIndex)];
			std::error_code error;
			std::filesystem::remove_all(info.path, error);
			if (_lastWorldPath == info.path.string()) {
				_lastWorldPath.clear();
				persistUserSettings();
			}
			refreshWorldList();
		}

		void enterPlayingFromMenu(bool newWorld) {
			if (newWorld)
				beginCreateWorldScreen();
			else
				openWorldSelect();
		}



		static constexpr int INV_TOTAL_SLOTS = HUD_INV_SLOTS + HUD_HOTBAR_COUNT;
		uint32_t itemStackLimit(ac::blockId id) const {
			const auto* definition = _blocks.get(ac::blockType(id));
			return definition ? definition->maxStack() : ac::ITEM_MAX_STACK;
		}

		bool inventorySlotGet(int index, ac::blockId& id, uint32_t& count) const {
			if (index < 0 || index >= INV_TOTAL_SLOTS) return false;
			if (index < HUD_INV_SLOTS) {
				id = _inventoryBlocks[index];
				count = _inventoryCounts[index];
			}
			else {
				const int hot = index - HUD_INV_SLOTS;
				id = _hotbarBlocks[hot];
				count = _hotbarCounts[hot];
			}
			return true;
		}

		void inventorySlotSet(int index, ac::blockId id, uint32_t count) {
			if (index < 0 || index >= INV_TOTAL_SLOTS) return;
			if (id == 0 || count == 0) {
				id = 0;
				count = 0;
			}
			count = (std::min)(count, itemStackLimit(id));
			if (index < HUD_INV_SLOTS) {
				_inventoryBlocks[index] = id;
				_inventoryCounts[index] = count;
			}
			else {
				const int hot = index - HUD_INV_SLOTS;
				_hotbarBlocks[hot] = id;
				_hotbarCounts[hot] = count;
				if (hot == _selectedHotbarSlot)
					_selectedBlock = id;
			}
		}




		void setInventoryTab(inventoryTab tab) {
			if (_inventoryTab == tab) return;
			endInventoryPaint();
			_chooserSearchFocus = false;
			_inventoryTab = tab;
			updateTextInput();
		}

		const std::vector<ac::blockId>& chooserItems() const {
			return _chooserFilterActive ? _chooserFiltered : _chooserCatalog;
		}

		int chooserMaxScroll() const {
			const int rows = (static_cast<int>(chooserItems().size()) + HUD_CHOOSER_COLUMNS - 1) /
				HUD_CHOOSER_COLUMNS;
			return (std::max)(0, rows - HUD_CHOOSER_ROWS);
		}

		void clampChooserScroll() {
			_chooserScroll = (std::max)(0, (std::min)(_chooserScroll, chooserMaxScroll()));
		}

		void applyChooserFilter() {
			std::string query = _chooserSearchBuf.data();
			for (char& ch : query)
				ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
			while (!query.empty() && query.front() == ' ')
				query.erase(query.begin());
			while (!query.empty() && query.back() == ' ')
				query.pop_back();
			_chooserFilterActive = !query.empty();
			_chooserFiltered.clear();
			if (_chooserFilterActive) {
				_chooserFiltered.reserve(_chooserCatalog.size());
				for (ac::blockId id : _chooserCatalog) {
					std::string name = _blocks.getName(id);
					for (char& ch : name)
						ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					if (name.find(query) != std::string::npos)
						_chooserFiltered.push_back(id);
				}
			}
			clampChooserScroll();
		}

		void rebuildChooserCatalog() {
			_chooserCatalog.clear();
			auto category = [](const std::string& name) -> int {
				if (name.find("log") != std::string::npos || name.find("plank") != std::string::npos ||
					name.find("wood") != std::string::npos || name.find("door") != std::string::npos ||
					name.find("fence") != std::string::npos || name.find("slab") != std::string::npos ||
					name.find("stairs") != std::string::npos) return 1;
				if (name.find("ore") != std::string::npos || name.find("coal") != std::string::npos ||
					name.find("iron") != std::string::npos || name.find("gold") != std::string::npos ||
					name.find("diamond") != std::string::npos || name.find("lapis") != std::string::npos ||
					name.find("redstone") != std::string::npos || name.find("copper") != std::string::npos ||
					name.find("emerald") != std::string::npos) return 2;
				if (name.find("glass") != std::string::npos || name.find("pane") != std::string::npos)
					return 3;
				if (name.find("stone") != std::string::npos || name.find("deepslate") != std::string::npos ||
					name.find("cobble") != std::string::npos || name.find("brick") != std::string::npos ||
					name.find("andesite") != std::string::npos || name.find("diorite") != std::string::npos ||
					name.find("granite") != std::string::npos) return 4;
				if (name.find("dirt") != std::string::npos || name.find("grass") != std::string::npos ||
					name.find("sand") != std::string::npos || name.find("gravel") != std::string::npos ||
					name.find("clay") != std::string::npos || name.find("snow") != std::string::npos ||
					name.find("ice") != std::string::npos || name.find("water") != std::string::npos)
					return 0;
				if (name.find("leaf") != std::string::npos || name.find("sapling") != std::string::npos ||
					name.find("flower") != std::string::npos || name.find("mushroom") != std::string::npos ||
					name.find("vine") != std::string::npos || name.find("moss") != std::string::npos)
					return 5;
				return 6;
			};
			std::vector<std::pair<int, ac::blockId>> sorted;
			std::unordered_set<std::string> seenNames;
			for (uint32_t id : _blocks.ids()) {
				if (id == 0) continue;
				if (isInternalItemVariant(static_cast<ac::blockId>(id)))
					continue;
				const std::string name = _blocks.getName(id);
				if (!seenNames.insert(name).second) continue;
				sorted.push_back({ category(name), static_cast<ac::blockId>(id) });
			}
			std::sort(sorted.begin(), sorted.end(), [&](const auto& a, const auto& b) {
				if (a.first != b.first) return a.first < b.first;
				return _blocks.getName(a.second) < _blocks.getName(b.second);
			});
			_chooserCatalog.reserve(sorted.size());
			for (const auto& entry : sorted)
				_chooserCatalog.push_back(entry.second);
			_chooserScroll = 0;
			_chooserSearchBuf.fill(0);
			_chooserFilterActive = false;
			_chooserFiltered.clear();
		}

		ac::blockId chooserCatalogAt(size_t visibleSlot) const {
			const auto& items = chooserItems();
			const size_t index = static_cast<size_t>(_chooserScroll) * HUD_CHOOSER_COLUMNS + visibleSlot;
			if (index >= items.size()) return 0;
			return items[index];
		}


		float inventoryNowSeconds() const {
			using clock = std::chrono::steady_clock;
			return std::chrono::duration<float>(clock::now().time_since_epoch()).count();
		}

		bool inventoryShiftHeld() const {
			return _uiInput.keyDown(GLFW_KEY_LEFT_SHIFT) || _uiInput.keyDown(GLFW_KEY_RIGHT_SHIFT);
		}

		uint32_t transferStackToRange(int from, int rangeBegin, int rangeEnd) {
			ac::blockId id = 0;
			uint32_t count = 0;
			inventorySlotGet(from, id, count);
			if (id == 0 || count == 0) return 0;
			uint32_t remaining = count;
			inventorySlotSet(from, 0, 0);
			const uint32_t limit = itemStackLimit(id);
			for (int pass = 0; pass < 2 && remaining > 0; ++pass) {
				for (int i = rangeBegin; i < rangeEnd && remaining > 0; ++i) {
					ac::blockId slotId = 0;
					uint32_t slotCount = 0;
					inventorySlotGet(i, slotId, slotCount);
					if (pass == 0) {
						if (slotId != id || slotCount >= limit) continue;
						const uint32_t space = limit - slotCount;
						const uint32_t move = (std::min)(space, remaining);
						inventorySlotSet(i, id, slotCount + move);
						remaining -= move;
					}
					else {
						if (slotId != 0) continue;
						const uint32_t move = (std::min)(limit, remaining);
						inventorySlotSet(i, id, move);
						remaining -= move;
					}
				}
			}
			if (remaining > 0)
				inventorySlotSet(from, id, remaining);
			return count - remaining;
		}

		void shiftTransferSlot(int index) {
			if (index < 0 || index >= INV_TOTAL_SLOTS) return;
			if (index < HUD_INV_SLOTS)
				transferStackToRange(index, HUD_INV_SLOTS, INV_TOTAL_SLOTS);
			else
				transferStackToRange(index, 0, HUD_INV_SLOTS);
		}

		void shiftTransferAllOfType(int index) {
			ac::blockId id = 0;
			uint32_t count = 0;
			inventorySlotGet(index, id, count);
			if (id == 0) return;
			const bool fromStorage = index < HUD_INV_SLOTS;
			const int srcBegin = fromStorage ? 0 : HUD_INV_SLOTS;
			const int srcEnd = fromStorage ? HUD_INV_SLOTS : INV_TOTAL_SLOTS;
			const int dstBegin = fromStorage ? HUD_INV_SLOTS : 0;
			const int dstEnd = fromStorage ? INV_TOTAL_SLOTS : HUD_INV_SLOTS;
			for (int i = srcBegin; i < srcEnd; ++i) {
				ac::blockId slotId = 0;
				uint32_t slotCount = 0;
				inventorySlotGet(i, slotId, slotCount);
				if (slotId == id)
					transferStackToRange(i, dstBegin, dstEnd);
			}
		}

		uint32_t addStackToChest(ac::chestInventory& inventory, ac::blockId id, uint32_t count) {
			if (id == 0 || count == 0) return 0;
			const uint32_t limit = itemStackLimit(id);
			uint32_t remaining = count;
			for (size_t i = 0; i < inventory.itemIds.size() && remaining > 0; ++i) {
				if (inventory.itemIds[i] != id || inventory.counts[i] >= limit)
					continue;
				const uint32_t move = (std::min)(limit - inventory.counts[i], remaining);
				inventory.counts[i] += move;
				remaining -= move;
			}
			for (size_t i = 0; i < inventory.itemIds.size() && remaining > 0; ++i) {
				if (inventory.itemIds[i] != 0) continue;
				const uint32_t move = (std::min)(limit, remaining);
				inventory.itemIds[i] = id;
				inventory.counts[i] = move;
				remaining -= move;
			}
			return count - remaining;
		}

		void shiftTransferInventoryToChest(ac::chestInventory& inventory, int index) {
			ac::blockId id = 0;
			uint32_t count = 0;
			if (!inventorySlotGet(index, id, count) || id == 0 || count == 0) return;
			const uint32_t moved = addStackToChest(inventory, id, count);
			inventorySlotSet(index, id, count - moved);
		}

		void shiftTransferChestToInventory(ac::chestInventory& inventory, int index) {
			if (index < 0 || index >= static_cast<int>(inventory.itemIds.size())) return;
			const ac::blockId id = static_cast<ac::blockId>(inventory.itemIds[index]);
			const uint32_t count = inventory.counts[index];
			if (id == 0 || count == 0) return;
			const uint32_t moved = addToHotbar(id, count);
			inventory.counts[index] = count - moved;
			if (inventory.counts[index] == 0)
				inventory.itemIds[index] = 0;
		}

		bool furnaceAccepts(int slot, ac::blockId id) const {
			return id != 0 && (slot == ac::furnaceInventory::input ||
				slot == ac::furnaceInventory::fuel);
		}

		uint32_t addStackToFurnace(ac::furnaceInventory& furnace, int slot,
			ac::blockId id, uint32_t count) {
			if (!furnaceAccepts(slot, id) || count == 0) return 0;
			const size_t i = static_cast<size_t>(slot);
			if (furnace.itemIds[i] != 0 && furnace.itemIds[i] != id) return 0;
			const uint32_t space = itemStackLimit(id) > furnace.counts[i]
				? itemStackLimit(id) - furnace.counts[i] : 0u;
			const uint32_t moved = (std::min)(space, count);
			if (moved) {
				furnace.itemIds[i] = id;
				furnace.counts[i] += moved;
			}
			return moved;
		}

		void shiftTransferInventoryToFurnace(ac::furnaceInventory& furnace, int index) {
			ac::blockId id = 0;
			uint32_t count = 0;
			if (!inventorySlotGet(index, id, count) || id == 0 || count == 0) return;
			int slot = _recipes.fuelSeconds(id) > 0.0f && !_recipes.smelting(id)
				? ac::furnaceInventory::fuel : ac::furnaceInventory::input;
			const uint32_t moved = addStackToFurnace(furnace, slot, id, count);
			inventorySlotSet(index, id, count - moved);
		}

		void shiftTransferFurnaceToInventory(ac::furnaceInventory& furnace, int slot) {
			if (slot < 0 || slot >= 3) return;
			const ac::blockId id = static_cast<ac::blockId>(furnace.itemIds[slot]);
			const uint32_t count = furnace.counts[slot];
			const uint32_t moved = addToHotbar(id, count);
			furnace.counts[slot] -= moved;
			if (furnace.counts[slot] == 0) furnace.itemIds[slot] = 0;
		}

		void furnaceSlotClick(ac::furnaceInventory& furnace, int slot, bool rightClick) {
			if (slot < 0 || slot >= 3) return;
			if (inventoryShiftHeld() && _cursorItem == 0) {
				shiftTransferFurnaceToInventory(furnace, slot);
				playUiClick();
				return;
			}
			const size_t i = static_cast<size_t>(slot);
			ac::blockId id = static_cast<ac::blockId>(furnace.itemIds[i]);
			uint32_t count = furnace.counts[i];
			if (_cursorItem == 0) {
				if (id == 0 || count == 0) return;
				const uint32_t take = rightClick ? (count + 1u) / 2u : count;
				_cursorItem = id;
				_cursorCount = take;
				furnace.counts[i] -= take;
				if (furnace.counts[i] == 0) furnace.itemIds[i] = 0;
				playUiClick();
				return;
			}
			if (slot == ac::furnaceInventory::output || !furnaceAccepts(slot, _cursorItem)) return;
			if (id != 0 && id != _cursorItem) {
				if (rightClick) return;
				const ac::blockId cursor = _cursorItem;
				_cursorItem = static_cast<ac::blockId>(furnace.itemIds[i]);
				furnace.itemIds[i] = cursor;
				std::swap(furnace.counts[i], _cursorCount);
			}
			else {
				const uint32_t move = rightClick ? 1u : _cursorCount;
				const uint32_t added = addStackToFurnace(furnace, slot, _cursorItem, move);
				_cursorCount -= added;
				if (_cursorCount == 0) _cursorItem = 0;
			}
			playUiClick();
		}

		void gatherInventoryItem(int target) {
			ac::blockId slotId = 0;
			uint32_t slotCount = 0;
			inventorySlotGet(target, slotId, slotCount);
			const ac::blockId id = _cursorItem != 0 ? _cursorItem : slotId;
			if (id == 0 || (_cursorItem != 0 && _cursorItem != id)) return;

			if (_cursorItem == 0) {
				_cursorItem = id;
				_cursorCount = slotCount;
				inventorySlotSet(target, 0, 0);
			}
			for (int i = 0; i < INV_TOTAL_SLOTS && _cursorCount < itemStackLimit(id); ++i) {
				inventorySlotGet(i, slotId, slotCount);
				if (slotId != id || slotCount == 0) continue;
				const uint32_t move = (std::min)(itemStackLimit(id) - _cursorCount, slotCount);
				_cursorCount += move;
				inventorySlotSet(i, slotId, slotCount - move);
			}
		}


		void returnCursorToInventory() {
			if (_cursorItem == 0 || _cursorCount == 0) {
				_cursorItem = 0;
				_cursorCount = 0;
				return;
			}
			const uint32_t taken = addToHotbar(_cursorItem, _cursorCount);
			_cursorCount -= taken;
			if (_cursorCount == 0)
				_cursorItem = 0;
			else {
				for (int i = 0; i < HUD_INV_SLOTS && _cursorCount > 0; ++i) {
					if (_inventoryBlocks[i] == _cursorItem && _inventoryCounts[i] < itemStackLimit(_cursorItem)) {
						const uint32_t space = itemStackLimit(_cursorItem) - _inventoryCounts[i];
						const uint32_t move = (std::min)(space, _cursorCount);
						_inventoryCounts[i] += move;
						_cursorCount -= move;
					}
				}
				for (int i = 0; i < HUD_INV_SLOTS && _cursorCount > 0; ++i) {
					if (_inventoryBlocks[i] == 0) {
						const uint32_t move = (std::min)(itemStackLimit(_cursorItem), _cursorCount);
						_inventoryBlocks[i] = _cursorItem;
						_inventoryCounts[i] = move;
						_cursorCount -= move;
					}
				}
				if (_cursorCount == 0)
					_cursorItem = 0;
			}
			if (_cursorItem != 0 && _cursorCount > 0 && _worldSessionOpen) {
				const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
				spawnItemDrop(_cursorItem, _cursorCount,
					eye.x, eye.y - 0.4f, eye.z, 0.0f, 1.0f, 0.0f);
				_cursorItem = 0;
				_cursorCount = 0;
			}
		}

		void endInventoryPaint() {
			_inventoryPaintActive = false;
			_inventoryPaintOnes = false;
			_inventoryPaintSlots.clear();
			_inventoryPaintItem = 0;
			_inventoryPaintPool = 0;
		}

		void redistributeInventoryPaint() {
			if (!_inventoryPaintActive || _inventoryPaintItem == 0 || _inventoryPaintSlots.empty())
				return;
			for (int index : _inventoryPaintSlots)
				inventorySlotSet(index, _inventoryPaintBaseId[index], _inventoryPaintBaseCount[index]);

			uint32_t pool = _inventoryPaintPool;
			const ac::blockId id = _inventoryPaintItem;
			std::vector<int> valid;
			valid.reserve(_inventoryPaintSlots.size());
			for (int index : _inventoryPaintSlots) {
				ac::blockId slotId = 0;
				uint32_t slotCount = 0;
				inventorySlotGet(index, slotId, slotCount);
				if (slotId == 0 || slotId == id)
					valid.push_back(index);
			}
			if (valid.empty()) {
				_cursorItem = id;
				_cursorCount = pool;
				return;
			}

			if (_inventoryPaintOnes) {
				for (int index : valid) {
					if (pool == 0) break;
					ac::blockId slotId = 0;
					uint32_t slotCount = 0;
					inventorySlotGet(index, slotId, slotCount);
					if (slotId == id && slotCount >= itemStackLimit(id)) continue;
					if (slotId == 0)
						inventorySlotSet(index, id, 1);
					else
						inventorySlotSet(index, id, slotCount + 1);
					--pool;
				}
				_cursorItem = pool > 0 ? id : 0;
				_cursorCount = pool;
				return;
			}

			const uint32_t each = pool / static_cast<uint32_t>(valid.size());
			uint32_t remain = pool % static_cast<uint32_t>(valid.size());
			for (int index : valid) {
				ac::blockId slotId = 0;
				uint32_t slotCount = 0;
				inventorySlotGet(index, slotId, slotCount);
				uint32_t add = each + (remain > 0 ? 1u : 0u);
				if (remain > 0) --remain;
				const uint32_t space = itemStackLimit(id) > (slotId == id ? slotCount : 0u)
					? itemStackLimit(id) - (slotId == id ? slotCount : 0u) : 0u;
				add = (std::min)(add, space);
				if (slotId == 0)
					inventorySlotSet(index, id, add);
				else
					inventorySlotSet(index, id, slotCount + add);
				pool -= add;
			}
			_cursorItem = pool > 0 ? id : 0;
			_cursorCount = pool;
		}

		void beginInventoryPaint(int index, bool ones) {
			ac::blockId slotId = 0;
			uint32_t slotCount = 0;
			inventorySlotGet(index, slotId, slotCount);
			if (_cursorItem == 0 || _cursorCount == 0) return;
			if (slotId != 0 && slotId != _cursorItem) return;

			_inventoryPaintActive = true;
			_inventoryPaintOnes = ones;
			_inventoryPaintItem = _cursorItem;
			_inventoryPaintPool = _cursorCount;
			_inventoryPaintSlots.clear();
			for (int i = 0; i < INV_TOTAL_SLOTS; ++i) {
				ac::blockId id = 0;
				uint32_t count = 0;
				inventorySlotGet(i, id, count);
				_inventoryPaintBaseId[i] = id;
				_inventoryPaintBaseCount[i] = count;
			}
			_cursorItem = 0;
			_cursorCount = 0;
			_inventoryPaintSlots.push_back(index);
			redistributeInventoryPaint();
		}

		void inventoryPaintEnter(int index) {
			if (!_inventoryPaintActive) return;
			for (int existing : _inventoryPaintSlots)
				if (existing == index) return;
			ac::blockId slotId = 0;
			uint32_t slotCount = 0;
			inventorySlotGet(index, slotId, slotCount);
			if (slotId != 0 && slotId != _inventoryPaintItem) return;
			_inventoryPaintSlots.push_back(index);
			redistributeInventoryPaint();
		}

		void chooserPick(size_t visibleSlot, bool single) {
			const ac::blockId id = chooserCatalogAt(visibleSlot);
			if (id == 0) return;
			if (inventoryShiftHeld()) {
				uint32_t remaining = single ? 1u : itemStackLimit(id);
				remaining -= addToHotbar(id, remaining);
				playUiClick();
				return;
			}
			if (_cursorItem != 0 && _cursorItem != id) {
				_cursorItem = id;
				_cursorCount = single ? 1u : itemStackLimit(id);
			}
			else if (_cursorItem == id) {
				if (single)
					_cursorCount = (std::min)(_cursorCount + 1u, itemStackLimit(id));
				else
					_cursorCount = itemStackLimit(id);
			}
			else {
				_cursorItem = id;
				_cursorCount = single ? 1u : itemStackLimit(id);
			}
			playUiClick();
		}

		void inventoryLeftClick(int index) {
			ac::blockId slotId = 0;
			uint32_t slotCount = 0;
			inventorySlotGet(index, slotId, slotCount);

			const float now = inventoryNowSeconds();
			const bool doubleClick = index == _lastInvClickSlot &&
				(now - _lastInvClickTime) <= DOUBLE_CLICK_SECONDS;
			const ac::blockId clickType = slotId != 0 ? slotId :
				(_cursorItem != 0 ? _cursorItem : _lastInvClickItem);
			_lastInvClickSlot = index;
			_lastInvClickTime = now;
			if (clickType != 0)
				_lastInvClickItem = clickType;

			if (doubleClick && inventoryShiftHeld() && clickType != 0) {
				// Ensure the clicked slot still carries the type for range scan.
				if (slotId == 0 && _cursorItem == clickType) {
					inventorySlotSet(index, _cursorItem, _cursorCount);
					_cursorItem = 0;
					_cursorCount = 0;
				}
				else if (slotId == 0) {
					for (int i = 0; i < INV_TOTAL_SLOTS; ++i) {
						ac::blockId id = 0;
						uint32_t count = 0;
						inventorySlotGet(i, id, count);
						if (id == clickType) {
							shiftTransferAllOfType(i);
							playUiClick();
							return;
						}
					}
				}
				shiftTransferAllOfType(index);
				playUiClick();
				return;
			}
			if (doubleClick && clickType != 0) {
				gatherInventoryItem(index);
				playUiClick();
				return;
			}
			if (inventoryShiftHeld() && slotId != 0 && _cursorItem == 0) {
				shiftTransferSlot(index);
				playUiClick();
				return;
			}

			if (_cursorItem == 0) {
				if (slotId == 0) return;
				_cursorItem = slotId;
				_cursorCount = slotCount;
				inventorySlotSet(index, 0, 0);
				playUiClick();
				return;
			}

			if (slotId != 0 && slotId != _cursorItem) {
				inventorySlotSet(index, _cursorItem, _cursorCount);
				_cursorItem = slotId;
				_cursorCount = slotCount;
				playUiClick();
				return;
			}

			beginInventoryPaint(index, false);
			playUiClick();
		}

		void inventoryRightClick(int index) {
			ac::blockId slotId = 0;
			uint32_t slotCount = 0;
			inventorySlotGet(index, slotId, slotCount);

			if (_cursorItem == 0) {
				if (slotId == 0 || slotCount == 0) return;
				const uint32_t half = (slotCount + 1u) / 2u;
				_cursorItem = slotId;
				_cursorCount = half;
				inventorySlotSet(index, slotId, slotCount - half);
				playUiClick();
				return;
			}

			if (slotId != 0 && slotId != _cursorItem) return;
			beginInventoryPaint(index, true);
			playUiClick();
		}



		struct nk_image itemNkImage(ac::blockId id) const {
			if (id == 0)
				return nk_image_id(0);
			const auto uv = _itemIcons.uvFor(id);
			if (uv.valid && _itemIcons.srv()) {
				return ac::nuklearHost::subImageFromSrv(
					_itemIcons.srv(),
					static_cast<int>(_itemIcons.atlasWidth()),
					static_cast<int>(_itemIcons.atlasHeight()),
					uv.uvMin.x, uv.uvMin.y, uv.uvMax.x, uv.uvMax.y);
			}
			const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
			if (!definition)
				return nk_image_id(0);
			ac::texture* face = _blockTextures.get(definition->materialForFace(ac::BLOCK_FACE_UP));
			if (!face || !face->_shaderResourceView)
				face = _blockTextures.get(definition->materialForFace(ac::BLOCK_FACE_WEST));
			if (!face || !face->_shaderResourceView)
				return nk_image_id(0);
			return ac::nuklearHost::imageFromSrv(
				face->_shaderResourceView.Get(),
				static_cast<int>(face->_width),
				static_cast<int>(face->_height));
		}

		struct nk_image guiSubImage(const ac::texture& tex, int x, int y, int w, int h) const {
			if (!tex._shaderResourceView)
				return nk_image_id(0);
			const float aw = static_cast<float>(tex._width);
			const float ah = static_cast<float>(tex._height);
			return ac::nuklearHost::subImageFromSrv(
				tex._shaderResourceView.Get(),
				static_cast<int>(tex._width),
				static_cast<int>(tex._height),
				static_cast<float>(x) / aw,
				static_cast<float>(y) / ah,
				static_cast<float>(x + w) / aw,
				static_cast<float>(y + h) / ah);
		}

		static int guiTextureScale(const ac::texture& tex, int atlasLogical = 256) {
			if (tex._width >= static_cast<UINT>(atlasLogical) &&
				tex._width % static_cast<UINT>(atlasLogical) == 0)
				return static_cast<int>(tex._width / static_cast<UINT>(atlasLogical));
			return 1;
		}

		float guiScale(float panelW, float panelH, float winW, float winH) const {
			const float fit = (std::min)((winW * 0.92f) / panelW, (winH * 0.88f) / panelH);
			return (std::clamp)(fit, 2.0f, 4.0f);
		}

		void drawGuiPanel(struct nk_command_buffer* canvas, const ac::texture& tex,
			float ox, float oy, float scale, int srcX, int srcY, int srcW, int srcH) const {
			if (!canvas) return;
			const struct nk_rect dst = nk_rect(
				ox, oy, static_cast<float>(srcW) * scale, static_cast<float>(srcH) * scale);
			if (!tex._shaderResourceView) {
				nk_fill_rect(canvas, dst, 0, nk_rgb(198, 198, 198));
				nk_stroke_rect(canvas, dst, 0, 2.0f, nk_rgb(80, 80, 80));
				return;
			}
			const int ts = guiTextureScale(tex);
			struct nk_image img = guiSubImage(tex, srcX * ts, srcY * ts, srcW * ts, srcH * ts);
			nk_draw_image(canvas, dst, &img, nk_rgb(255, 255, 255));
		}

		void drawHudStatIcons(
			struct nk_command_buffer* canvas,
			float originX,
			float originY,
			float scale,
			float value,
			bool fromRight
		) const {
			if (!canvas || !_guiIcons._shaderResourceView) return;
			constexpr int kPips = 10;
			constexpr int kIcon = 9;
			const float filled = std::clamp(value, 0.0f, 20.0f);
			const int containerU = 16;
			const int fullU = 52;
			const int halfU = 61;
			const int rowV = fromRight ? 27 : 0;
			for (int i = 0; i < kPips; ++i) {
				const float x = fromRight
					? originX - static_cast<float>(kIcon + i * 8) * scale
					: originX + static_cast<float>(i * 8) * scale;
				drawGuiPanel(canvas, _guiIcons, x, originY, scale, containerU, rowV, kIcon, kIcon);
				const float pip = filled - static_cast<float>(i) * 2.0f;
				if (pip >= 2.0f)
					drawGuiPanel(canvas, _guiIcons, x, originY, scale, fullU, rowV, kIcon, kIcon);
				else if (pip >= 1.0f)
					drawGuiPanel(canvas, _guiIcons, x, originY, scale, halfU, rowV, kIcon, kIcon);
			}
		}

		void drawHudAirBubbles(
			struct nk_command_buffer* canvas,
			float originX,
			float originY,
			float scale,
			float air
		) const {
			if (!canvas || !_guiIcons._shaderResourceView) return;
			constexpr int kPips = 10;
			constexpr int kIcon = 9;
			const float filled = std::clamp(air, 0.0f, ac::PLAYER_MAX_AIR);
			if (filled <= 0.0f) return;
			constexpr int fullU = 16;
			constexpr int popU = 24;
			constexpr int rowV = 18;
			for (int i = 0; i < kPips; ++i) {
				const float pip = filled - static_cast<float>(i);
				if (pip <= 0.0f) continue;
				const float x = originX - static_cast<float>(kIcon + i * 8) * scale;
				if (pip >= 1.0f)
					drawGuiPanel(canvas, _guiIcons, x, originY, scale, fullU, rowV, kIcon, kIcon);
				else
					drawGuiPanel(canvas, _guiIcons, x, originY, scale, popU, rowV, kIcon, kIcon);
			}
		}

		int drawItemSlotAbs(
			struct nk_context* ctx,
			float x, float y, float size,
			int /*id*/, ac::blockId item, uint32_t count,
			bool* hovered = nullptr
		) {
			struct nk_rect bounds = nk_rect(x, y, size, size);
			// Texture cells are 18px with a 16px icon; expand the hit box to the cell.
			const float hitPad = size * (2.0f / 16.0f);
			struct nk_rect hit = nk_rect(x - hitPad * 0.5f, y - hitPad * 0.5f,
				size + hitPad, size + hitPad);
			const bool hover = nk_input_is_mouse_hovering_rect(&ctx->input, hit) != 0;
			if (hovered)
				*hovered = hover;
			int result = 0;
			if (hover) {
				if (nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT))
					result = 1;
				else if (nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_RIGHT))
					result = 2;
			}
			struct nk_command_buffer* canvas = nk_window_get_canvas(ctx);
			if (canvas && hover)
				nk_fill_rect(canvas, bounds, 0, nk_rgba(255, 255, 255, 50));
			const struct nk_image img = itemNkImage(item);
			if (item != 0 && canvas) {
				const float pad = size * 0.0625f;
				struct nk_rect icon = nk_rect(x + pad, y + pad, size - pad * 2.0f, size - pad * 2.0f);
				if (img.handle.ptr) {
					nk_draw_image(canvas, icon, &img, nk_rgb(255, 255, 255));
				}
				else {
					const unsigned id = static_cast<unsigned>(ac::blockType(item));
					nk_fill_rect(canvas, icon, 0, nk_rgb(
						static_cast<int>((id * 67u) % 140u + 70u),
						static_cast<int>((id * 29u) % 140u + 70u),
						static_cast<int>((id * 47u) % 140u + 70u)));
				}
			}
			if (item != 0 && count > 1 && canvas) {
				char buf[16];
				std::snprintf(buf, sizeof(buf), "%u", count);
				nk_draw_text(canvas, nk_rect(x + size - 14.0f, y + size - 12.0f, 20.0f, 14.0f),
					buf, (int)std::strlen(buf), ctx->style.font, nk_rgba(0, 0, 0, 0), nk_rgb(255, 255, 255));
			}
			return result;
		}

		void drawContainerGui(struct nk_context* ctx, float winW, float winH) {
			const bool creativeItems =
				_screen == ac::gameScreen::inventory && isCreative() &&
				_inventoryTab == inventoryTab::chooser;
			const bool creativeStorage =
				_screen == ac::gameScreen::inventory && isCreative() &&
				_inventoryTab == inventoryTab::storage;
			const bool inventoryScreen = _screen == ac::gameScreen::inventory;

			auto handleInvSlot = [&](int index, int click, bool hovered) {
				if (click == 1 && _screen == ac::gameScreen::chest && inventoryShiftHeld()) {
					ac::chestInventory& chest =
						_blockEntities.chestAt(_openChestBlock.x, _openChestBlock.y, _openChestBlock.z);
					shiftTransferInventoryToChest(chest, index);
					playUiClick();
				}
				else if (click == 1 && _screen == ac::gameScreen::furnace && inventoryShiftHeld()) {
					ac::furnaceInventory& furnace = _blockEntities.furnaceAt(
						_openFurnaceBlock.x, _openFurnaceBlock.y, _openFurnaceBlock.z);
					shiftTransferInventoryToFurnace(furnace, index);
					playUiClick();
				}
				else if (click == 1) inventoryLeftClick(index);
				else if (click == 2) inventoryRightClick(index);
				if (hovered && _inventoryPaintActive)
					inventoryPaintEnter(index);
			};

				auto handleChestSlot = [&](ac::chestInventory& inventory, int i, int click) {
				ac::blockId id = static_cast<ac::blockId>(inventory.itemIds[i]);
				uint32_t c = inventory.counts[i];
				if (click == 1) {
					if (inventoryShiftHeld() && !_cursorItem) {
						shiftTransferChestToInventory(inventory, i);
						playUiClick();
						return;
					}
					if (!_cursorItem) {
						if (id) {
							_cursorItem = id;
							_cursorCount = (std::max)(1u, c);
							inventory.itemIds[i] = 0;
							inventory.counts[i] = 0;
							playUiClick();
						}
					} else if (!id || id == _cursorItem) {
						const uint32_t limit = itemStackLimit(_cursorItem);
						const uint32_t space = limit > c ? limit - c : 0u;
						const uint32_t move = (std::min)(space, _cursorCount);
						if (move) {
							inventory.itemIds[i] = _cursorItem;
							inventory.counts[i] = c + move;
							_cursorCount -= move;
							if (!_cursorCount) _cursorItem = 0;
							playUiClick();
						}
					} else {
						inventory.itemIds[i] = _cursorItem;
						inventory.counts[i] = _cursorCount;
						_cursorItem = id;
						_cursorCount = c;
						playUiClick();
					}
				} else if (click == 2) {
					if (!_cursorItem) {
						if (id && c) {
							const uint32_t half = (c + 1u) / 2u;
							_cursorItem = id;
							_cursorCount = half;
							inventory.counts[i] = c - half;
							if (inventory.counts[i] == 0) inventory.itemIds[i] = 0;
							playUiClick();
						}
				} else if ((!id || id == _cursorItem) && c < itemStackLimit(_cursorItem)) {
						inventory.itemIds[i] = _cursorItem;
						inventory.counts[i] = c + 1;
						if (--_cursorCount == 0) _cursorItem = 0;
						playUiClick();
					}
				}
			};

			auto drawTab = [&](struct nk_command_buffer* canvas, float x, float y, float w, float h,
				const char* label, bool selected) -> bool {
				const struct nk_rect rect = nk_rect(x, y, w, h);
				const bool hover = nk_input_is_mouse_hovering_rect(&ctx->input, rect) != 0;
				if (canvas) {
					nk_fill_rect(canvas, rect, 0, selected ? nk_rgb(198, 198, 198) : nk_rgb(90, 90, 90));
					nk_stroke_rect(canvas, rect, 0, 1.0f, nk_rgb(40, 40, 40));
					nk_draw_text(canvas, nk_rect(x + 8.0f, y + 4.0f, w - 16.0f, h - 6.0f),
						label, (int)std::strlen(label), ctx->style.font,
						nk_rgba(0, 0, 0, 0), selected ? nk_rgb(20, 20, 20) : nk_rgb(230, 230, 230));
				}
				return hover && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT);
			};

			struct nk_style_window saved = ctx->style.window;
			ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
			ctx->style.window.background = nk_rgba(0, 0, 0, 0);
			ctx->style.window.border = 0;
			ctx->style.window.padding = nk_vec2(0, 0);
			ctx->style.window.spacing = nk_vec2(0, 0);

			ac::blockId hoveredItem = 0;
			if (nk_begin(ctx, "inventory_root", nk_rect(0, 0, winW, winH), NK_WINDOW_NO_SCROLLBAR)) {
				struct nk_command_buffer* canvas = nk_window_get_canvas(ctx);
				if (canvas)
					nk_fill_rect(canvas, nk_rect(0, 0, winW, winH), 0, nk_rgba(0, 0, 0, 150));

				const bool creativePanel = creativeItems || creativeStorage;
				const float panelW = creativePanel ? 195.0f : 176.0f;
				const float panelH = creativePanel ? 136.0f : 166.0f;
				const float scale = guiScale(panelW, panelH, winW, winH);
				const float ox = (winW - panelW * scale) * 0.5f;
				const float oy = (winH - panelH * scale) * 0.5f;
				const float slotPx = 16.0f * scale;

				if (inventoryScreen && isCreative()) {
					const float tabH = 22.0f * scale;
					const float tabW = panelW * scale * 0.5f;
					const float tabY = oy - tabH - 2.0f * scale;
					if (drawTab(canvas, ox, tabY, tabW, tabH, "Items",
						_inventoryTab == inventoryTab::chooser)) {
						setInventoryTab(inventoryTab::chooser);
						playUiClick();
					}
					if (drawTab(canvas, ox + tabW, tabY, tabW, tabH, "Inventory",
						_inventoryTab == inventoryTab::storage)) {
						setInventoryTab(inventoryTab::storage);
						playUiClick();
					}
				}

				if (creativeItems) {
					drawGuiPanel(canvas, _guiCreativeItems, ox, oy, scale, 0, 0, 195, 136);

					const struct nk_rect searchRect = nk_rect(
						ox + 82.0f * scale, oy + 6.0f * scale, 89.0f * scale, 14.0f * scale);
					_chooserSearchRect = searchRect;
					if (_chooserSearchFocus) {
						if (nk_input_is_key_pressed(&ctx->input, NK_KEY_BACKSPACE) ||
							nk_input_is_key_pressed(&ctx->input, NK_KEY_DEL)) {
							const size_t n = std::strlen(_chooserSearchBuf.data());
							if (n > 0) {
								_chooserSearchBuf[n - 1] = 0;
								applyChooserFilter();
							}
						}
						if (ctx->input.keyboard.text_len > 0) {
							size_t n = std::strlen(_chooserSearchBuf.data());
							for (int i = 0; i < ctx->input.keyboard.text_len && n + 1 < _chooserSearchBuf.size(); ++i) {
								const char ch = ctx->input.keyboard.text[i];
								if (ch >= 32)
									_chooserSearchBuf[n++] = ch;
							}
							_chooserSearchBuf[n] = 0;
							applyChooserFilter();
						}
					}
					if (canvas) {
						const char* query = _chooserSearchBuf.data();
						nk_fill_rect(canvas, searchRect, 1.0f,
							_chooserSearchFocus ? nk_rgb(242, 242, 242) : nk_rgb(224, 224, 224));
						nk_stroke_rect(canvas, searchRect, 0, 1.0f,
							_chooserSearchFocus ? nk_rgb(45, 85, 160) : nk_rgb(85, 85, 85));
						if (query && query[0]) {
							nk_draw_text(canvas, searchRect, query, (int)std::strlen(query),
								ctx->style.font, nk_rgba(0, 0, 0, 0), nk_rgb(32, 32, 32));
						}
						else {
							const char* placeholder = "Search items...";
							nk_draw_text(canvas, searchRect, placeholder, (int)std::strlen(placeholder),
								ctx->style.font, nk_rgba(0, 0, 0, 0), nk_rgb(105, 105, 105));
						}
					}

					if (_uiInput.scrollDelta.y != 0.0f && !_chooserScrollDragging) {
						const int steps = (std::max)(1, (int)std::lround(std::abs(_uiInput.scrollDelta.y)));
						_chooserScroll += (_uiInput.scrollDelta.y > 0.0f) ? -steps : steps;
						clampChooserScroll();
					}

					const auto& items = chooserItems();
					const int start = _chooserScroll * HUD_CHOOSER_COLUMNS;
					for (int row = 0; row < HUD_CHOOSER_ROWS; ++row) {
						for (int col = 0; col < HUD_CHOOSER_COLUMNS; ++col) {
							const int i = row * HUD_CHOOSER_COLUMNS + col;
							const int catalog = start + i;
							ac::blockId id = 0;
							if (catalog >= 0 && catalog < static_cast<int>(items.size()))
								id = items[static_cast<size_t>(catalog)];
							const float sx = ox + (9.0f + static_cast<float>(col) * 18.0f) * scale;
							const float sy = oy + (18.0f + static_cast<float>(row) * 18.0f) * scale;
							bool hovered = false;
							const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 1000 + i, id, 1, &hovered);
							if (hovered && id != 0)
								hoveredItem = id;
							if (click == 1 && id != 0) chooserPick(static_cast<size_t>(i), false);
							else if (click == 2 && id != 0) chooserPick(static_cast<size_t>(i), true);
						}
					}
					for (int i = 0; i < HUD_HOTBAR_COUNT; ++i) {
						bool hovered = false;
						const float sx = ox + (9.0f + static_cast<float>(i) * 18.0f) * scale;
						const float sy = oy + 112.0f * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 5000 + i,
							_hotbarBlocks[i], _hotbarCounts[i], &hovered);
						if (hovered && _hotbarBlocks[i] != 0)
							hoveredItem = _hotbarBlocks[i];
						handleInvSlot(HUD_INV_SLOTS + i, click, hovered);
					}

					const float barX = ox + 175.0f * scale;
					const float barY = oy + 18.0f * scale;
					const float barW = 12.0f * scale;
					const float barH = 90.0f * scale;
					if (canvas) {
						nk_fill_rect(canvas, nk_rect(barX, barY, barW, barH), 0, nk_rgb(55, 55, 55));
						const int maxScroll = chooserMaxScroll();
						const float thumbH = maxScroll <= 0
							? barH
							: (std::max)(12.0f * scale, barH * (static_cast<float>(HUD_CHOOSER_ROWS) /
								static_cast<float>(HUD_CHOOSER_ROWS + maxScroll)));
						const float thumbY = maxScroll <= 0
							? barY
							: barY + (barH - thumbH) * (static_cast<float>(_chooserScroll) /
								static_cast<float>(maxScroll));
						nk_fill_rect(canvas, nk_rect(barX + 1.0f, thumbY, barW - 2.0f, thumbH),
							0, nk_rgb(198, 198, 198));
						const struct nk_rect barHit = nk_rect(barX, barY, barW, barH);
						const bool barHover = nk_input_is_mouse_hovering_rect(&ctx->input, barHit) != 0;
						if (barHover && nk_input_is_mouse_pressed(&ctx->input, NK_BUTTON_LEFT))
							_chooserScrollDragging = true;
						if (!ctx->input.mouse.buttons[NK_BUTTON_LEFT].down)
							_chooserScrollDragging = false;
						if (_chooserScrollDragging && maxScroll > 0 && barH > thumbH) {
							const float rel = std::clamp(
								(ctx->input.mouse.pos.y - barY - thumbH * 0.5f) / (barH - thumbH),
								0.0f, 1.0f);
							_chooserScroll = static_cast<int>(std::lround(rel * static_cast<float>(maxScroll)));
							clampChooserScroll();
						}
					}
				}
				else if (creativeStorage) {
					drawGuiPanel(canvas, _guiCreativeInv, ox, oy, scale, 0, 0, 195, 136);
					for (int i = 0; i < HUD_INV_SLOTS; ++i) {
						bool hovered = false;
						const int col = i % HUD_INV_COLUMNS;
						const int row = i / HUD_INV_COLUMNS;
						const float sx = ox + (8.0f + static_cast<float>(col) * 18.0f) * scale;
						const float sy = oy + (54.0f + static_cast<float>(row) * 18.0f) * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 4000 + i,
							_inventoryBlocks[i], _inventoryCounts[i], &hovered);
						if (hovered && _inventoryBlocks[i] != 0)
							hoveredItem = _inventoryBlocks[i];
						handleInvSlot(i, click, hovered);
					}
					for (int i = 0; i < HUD_HOTBAR_COUNT; ++i) {
						bool hovered = false;
						const float sx = ox + (8.0f + static_cast<float>(i) * 18.0f) * scale;
						const float sy = oy + 112.0f * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 5000 + i,
							_hotbarBlocks[i], _hotbarCounts[i], &hovered);
						if (hovered && _hotbarBlocks[i] != 0)
							hoveredItem = _hotbarBlocks[i];
						handleInvSlot(HUD_INV_SLOTS + i, click, hovered);
					}
				}
				else if (inventoryScreen) {
					drawGuiPanel(canvas, _guiInventory, ox, oy, scale, 0, 0, 176, 166);
					static const int craft2Pos[4][2] = { {98,18},{116,18},{98,36},{116,36} };
					for (int i = 0; i < HUD_CRAFT2_SLOTS; ++i) {
						const float sx = ox + static_cast<float>(craft2Pos[i][0]) * scale;
						const float sy = oy + static_cast<float>(craft2Pos[i][1]) * scale;
						bool hovered = false;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 2100 + i,
							_craft2Blocks[i], _craft2Counts[i], &hovered);
						if (hovered && _craft2Blocks[i] != 0)
							hoveredItem = _craft2Blocks[i];
						if (click == 1 || click == 2)
							craftSlotClick(_craft2Blocks[i], _craft2Counts[i], click == 2);
					}
					{
						uint32_t resultCount = 0;
						const ac::blockId result = craftResultId(_craft2Blocks.data(), 2, 2, resultCount);
						const float sx = ox + 154.0f * scale;
						const float sy = oy + 28.0f * scale;
						bool hovered = false;
						if (drawItemSlotAbs(ctx, sx, sy, slotPx, 9200, result, resultCount, &hovered) == 1 && result != 0)
							craftOnce(_craft2Blocks.data(), _craft2Counts.data(), 2, 2);
						if (hovered && result != 0)
							hoveredItem = result;
					}
					for (int i = 0; i < HUD_INV_SLOTS; ++i) {
						bool hovered = false;
						const int col = i % HUD_INV_COLUMNS;
						const int row = i / HUD_INV_COLUMNS;
						const float sx = ox + (8.0f + static_cast<float>(col) * 18.0f) * scale;
						const float sy = oy + (84.0f + static_cast<float>(row) * 18.0f) * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 4000 + i,
							_inventoryBlocks[i], _inventoryCounts[i], &hovered);
						if (hovered && _inventoryBlocks[i] != 0)
							hoveredItem = _inventoryBlocks[i];
						handleInvSlot(i, click, hovered);
					}
					for (int i = 0; i < HUD_HOTBAR_COUNT; ++i) {
						bool hovered = false;
						const float sx = ox + (8.0f + static_cast<float>(i) * 18.0f) * scale;
						const float sy = oy + 142.0f * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 5000 + i,
							_hotbarBlocks[i], _hotbarCounts[i], &hovered);
						if (hovered && _hotbarBlocks[i] != 0)
							hoveredItem = _hotbarBlocks[i];
						handleInvSlot(HUD_INV_SLOTS + i, click, hovered);
					}
				}
				else if (_screen == ac::gameScreen::crafting) {
					drawGuiPanel(canvas, _guiCrafting, ox, oy, scale, 0, 0, 176, 166);
					for (int i = 0; i < HUD_CRAFT3_SLOTS; ++i) {
						const int col = i % 3;
						const int row = i / 3;
						const float sx = ox + (30.0f + static_cast<float>(col) * 18.0f) * scale;
						const float sy = oy + (17.0f + static_cast<float>(row) * 18.0f) * scale;
						bool hovered = false;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 2000 + i,
							_craft3Blocks[i], _craft3Counts[i], &hovered);
						if (hovered && _craft3Blocks[i] != 0)
							hoveredItem = _craft3Blocks[i];
						if (click == 1 || click == 2)
							craftSlotClick(_craft3Blocks[i], _craft3Counts[i], click == 2);
					}
					{
						uint32_t resultCount = 0;
						const ac::blockId result = craftResultId(_craft3Blocks.data(), 3, 3, resultCount);
						const float sx = ox + 124.0f * scale;
						const float sy = oy + 35.0f * scale;
						const float resultSize = 24.0f * scale;
						bool hovered = false;
						if (drawItemSlotAbs(ctx, sx, sy, resultSize, 9300, result, resultCount, &hovered) == 1 && result != 0)
							craftOnce(_craft3Blocks.data(), _craft3Counts.data(), 3, 3);
						if (hovered && result != 0)
							hoveredItem = result;
					}
					for (int i = 0; i < HUD_INV_SLOTS; ++i) {
						bool hovered = false;
						const int col = i % HUD_INV_COLUMNS;
						const int row = i / HUD_INV_COLUMNS;
						const float sx = ox + (8.0f + static_cast<float>(col) * 18.0f) * scale;
						const float sy = oy + (84.0f + static_cast<float>(row) * 18.0f) * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 4000 + i,
							_inventoryBlocks[i], _inventoryCounts[i], &hovered);
						if (hovered && _inventoryBlocks[i] != 0)
							hoveredItem = _inventoryBlocks[i];
						handleInvSlot(i, click, hovered);
					}
					for (int i = 0; i < HUD_HOTBAR_COUNT; ++i) {
						bool hovered = false;
						const float sx = ox + (8.0f + static_cast<float>(i) * 18.0f) * scale;
						const float sy = oy + 142.0f * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 5000 + i,
							_hotbarBlocks[i], _hotbarCounts[i], &hovered);
						if (hovered && _hotbarBlocks[i] != 0)
							hoveredItem = _hotbarBlocks[i];
						handleInvSlot(HUD_INV_SLOTS + i, click, hovered);
					}
				}
				else if (_screen == ac::gameScreen::furnace) {
					drawGuiPanel(canvas, _guiFurnace, ox, oy, scale, 0, 0, 176, 166);
					ac::furnaceInventory& furnace = _blockEntities.furnaceAt(
						_openFurnaceBlock.x, _openFurnaceBlock.y, _openFurnaceBlock.z);
					static const int furnacePos[3][2] = { {56,17}, {56,53}, {116,35} };
					for (int i = 0; i < 3; ++i) {
						const float sx = ox + static_cast<float>(furnacePos[i][0]) * scale;
						const float sy = oy + static_cast<float>(furnacePos[i][1]) * scale;
						bool hovered = false;
						const ac::blockId item = static_cast<ac::blockId>(furnace.itemIds[i]);
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 9400 + i,
							item, furnace.counts[i], &hovered);
						if (hovered && item != 0) hoveredItem = item;
						if (click == 1 || click == 2) furnaceSlotClick(furnace, i, click == 2);
					}
					if (canvas) {
						const float burn = furnace.burnTotal > 0.0f
							? std::clamp(furnace.burnRemaining / furnace.burnTotal, 0.0f, 1.0f) : 0.0f;
						const ac::smeltingRecipe* recipe = _recipes.smelting(
							static_cast<ac::blockId>(furnace.itemIds[ac::furnaceInventory::input]));
						const float progress = recipe && recipe->seconds > 0.0f
							? std::clamp(furnace.cookProgress / recipe->seconds, 0.0f, 1.0f) : 0.0f;
						if (burn > 0.0f)
							nk_fill_rect(canvas, nk_rect(ox + 57.0f * scale,
								oy + (50.0f - 13.0f * burn) * scale, 14.0f * scale,
								13.0f * burn * scale), 0, nk_rgb(255, 144, 32));
						if (progress > 0.0f)
							nk_fill_rect(canvas, nk_rect(ox + 79.0f * scale, oy + 34.0f * scale,
								24.0f * progress * scale, 16.0f * scale), 0, nk_rgb(224, 224, 224));
					}
					for (int i = 0; i < HUD_INV_SLOTS; ++i) {
						bool hovered = false;
						const int col = i % HUD_INV_COLUMNS;
						const int row = i / HUD_INV_COLUMNS;
						const float sx = ox + (8.0f + static_cast<float>(col) * 18.0f) * scale;
						const float sy = oy + (84.0f + static_cast<float>(row) * 18.0f) * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 4000 + i,
							_inventoryBlocks[i], _inventoryCounts[i], &hovered);
						if (hovered && _inventoryBlocks[i] != 0) hoveredItem = _inventoryBlocks[i];
						handleInvSlot(i, click, hovered);
					}
					for (int i = 0; i < HUD_HOTBAR_COUNT; ++i) {
						bool hovered = false;
						const float sx = ox + (8.0f + static_cast<float>(i) * 18.0f) * scale;
						const float sy = oy + 142.0f * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 5000 + i,
							_hotbarBlocks[i], _hotbarCounts[i], &hovered);
						if (hovered && _hotbarBlocks[i] != 0) hoveredItem = _hotbarBlocks[i];
						handleInvSlot(HUD_INV_SLOTS + i, click, hovered);
					}
				}
				else if (_screen == ac::gameScreen::chest) {
					drawGuiPanel(canvas, _guiChest, ox, oy, scale, 0, 0, 176, 71);
					drawGuiPanel(canvas, _guiChest, ox, oy + 71.0f * scale, scale, 0, 126, 176, 95);
					ac::chestInventory& inventory =
						_blockEntities.chestAt(_openChestBlock.x, _openChestBlock.y, _openChestBlock.z);
					for (int i = 0; i < HUD_CHEST_SLOTS; ++i) {
						const int col = i % HUD_CHEST_COLUMNS;
						const int row = i / HUD_CHEST_COLUMNS;
						const float sx = ox + (8.0f + static_cast<float>(col) * 18.0f) * scale;
						const float sy = oy + (18.0f + static_cast<float>(row) * 18.0f) * scale;
						bool hovered = false;
						const ac::blockId item = static_cast<ac::blockId>(inventory.itemIds[i]);
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 3000 + i,
							item, inventory.counts[i], &hovered);
						if (hovered && item != 0)
							hoveredItem = item;
						handleChestSlot(inventory, i, click);
					}
					for (int i = 0; i < HUD_INV_SLOTS; ++i) {
						bool hovered = false;
						const int col = i % HUD_INV_COLUMNS;
						const int row = i / HUD_INV_COLUMNS;
						const float sx = ox + (8.0f + static_cast<float>(col) * 18.0f) * scale;
						const float sy = oy + (84.0f + static_cast<float>(row) * 18.0f) * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 4000 + i,
							_inventoryBlocks[i], _inventoryCounts[i], &hovered);
						if (hovered && _inventoryBlocks[i] != 0)
							hoveredItem = _inventoryBlocks[i];
						handleInvSlot(i, click, hovered);
					}
					for (int i = 0; i < HUD_HOTBAR_COUNT; ++i) {
						bool hovered = false;
						const float sx = ox + (8.0f + static_cast<float>(i) * 18.0f) * scale;
						const float sy = oy + 142.0f * scale;
						const int click = drawItemSlotAbs(ctx, sx, sy, slotPx, 5000 + i,
							_hotbarBlocks[i], _hotbarCounts[i], &hovered);
						if (hovered && _hotbarBlocks[i] != 0)
							hoveredItem = _hotbarBlocks[i];
						handleInvSlot(HUD_INV_SLOTS + i, click, hovered);
					}
				}

				const struct nk_vec2 m = ctx->input.mouse.pos;
				if (canvas && _cursorItem != 0) {
					const struct nk_image img = itemNkImage(_cursorItem);
					const struct nk_rect held = nk_rect(m.x - 12, m.y - 12, 28, 28);
					if (img.handle.ptr)
						nk_draw_image(canvas, held, &img, nk_rgb(255, 255, 255));
					else
						nk_fill_rect(canvas, held, 0, nk_rgb(180, 180, 80));
					if (_cursorCount > 1) {
						char buf[16];
						std::snprintf(buf, sizeof(buf), "%u", _cursorCount);
						nk_draw_text(canvas, nk_rect(m.x + 6, m.y + 8, 24, 14),
							buf, (int)std::strlen(buf), ctx->style.font, nk_rgba(0, 0, 0, 0), nk_rgb(255, 255, 255));
					}
				}
				else if (canvas && hoveredItem != 0) {
					const std::string name = _blocks.getName(hoveredItem);
					if (!name.empty()) {
						const float tw = 12.0f + static_cast<float>(name.size()) * 7.0f;
						const struct nk_rect tip = nk_rect(m.x + 14.0f, m.y + 16.0f, tw, 18.0f);
						nk_fill_rect(canvas, tip, 0, nk_rgba(16, 16, 16, 220));
						nk_stroke_rect(canvas, tip, 0, 1.0f, nk_rgb(40, 40, 40));
						nk_draw_text(canvas, nk_rect(tip.x + 4, tip.y + 2, tip.w - 6, 14),
							name.c_str(), (int)name.size(), ctx->style.font,
							nk_rgba(0, 0, 0, 0), nk_rgb(255, 255, 255));
					}
				}
			}
			nk_end(ctx);
			ctx->style.window = saved;

			if (_inventoryPaintActive) {
				const bool held = _inventoryPaintOnes
					? (ctx->input.mouse.buttons[NK_BUTTON_RIGHT].down != 0)
					: (ctx->input.mouse.buttons[NK_BUTTON_LEFT].down != 0);
				if (!held)
					endInventoryPaint();
			}
		}

		void drawNuklearGui() {
			if (!_nuklear.ready()) return;
			struct nk_context* ctx = _nuklear.ctx();
			const float winW = static_cast<float>(_window.getWidth());
			const float winH = static_cast<float>(_window.getHeight());

			auto centered = [&](float w, float h) {
				return nk_rect((winW - w) * 0.5f, (winH - h) * 0.5f, w, h);
			};

			auto pushTransparentWindow = [&]() {
				struct nk_style_window saved = ctx->style.window;
				ctx->style.window.fixed_background = nk_style_item_color(nk_rgba(0, 0, 0, 0));
				ctx->style.window.background = nk_rgba(0, 0, 0, 0);
				ctx->style.window.border = 0;
				ctx->style.window.padding = nk_vec2(0, 0);
				ctx->style.window.spacing = nk_vec2(0, 0);
				return saved;
			};

			// Playing HUD hotbar (hidden while container GUIs are open — those include their own)
			if (_screen == ac::gameScreen::playing) {
				const float scale = (std::clamp)(winH / 240.0f, 2.0f, 3.0f);
				const float barW = 182.0f * scale;
				const float barH = 22.0f * scale;
				const float barX = (winW - barW) * 0.5f;
				const float barY = winH - barH - 4.0f * scale;
				const struct nk_style_window savedWin = pushTransparentWindow();
				if (nk_begin(ctx, "hud_hotbar", nk_rect(barX - 4.0f * scale, barY - 4.0f * scale, barW + 8.0f * scale, barH + 8.0f * scale),
					NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND)) {
					struct nk_command_buffer* canvas = nk_window_get_canvas(ctx);
					if (_guiReady && canvas) {
						drawGuiPanel(canvas, _guiWidgets, barX, barY, scale, 0, 0, 182, 22);
						const float selX = barX - scale + static_cast<float>(_selectedHotbarSlot) * 20.0f * scale;
						const float selY = barY - scale;
						drawGuiPanel(canvas, _guiWidgets, selX, selY, scale, 0, 22, 24, 24);
						for (int i = 0; i < HUD_HOTBAR_COUNT; ++i) {
							const float sx = barX + (3.0f + static_cast<float>(i) * 20.0f) * scale;
							const float sy = barY + 3.0f * scale;
							const float icon = 16.0f * scale;
							const struct nk_image img = itemNkImage(_hotbarBlocks[i]);
							if (_hotbarBlocks[i] && img.handle.ptr)
								nk_draw_image(canvas, nk_rect(sx, sy, icon, icon), &img, nk_rgb(255, 255, 255));
							if (_hotbarCounts[i] > 1) {
								char buf[16];
								std::snprintf(buf, sizeof(buf), "%u", _hotbarCounts[i]);
								nk_draw_text(canvas, nk_rect(sx + icon - 12.0f, sy + icon - 10.0f, 18, 12),
									buf, (int)std::strlen(buf), ctx->style.font, nk_rgba(0, 0, 0, 0), nk_rgb(255, 255, 255));
							}
						}
					}
				}
				nk_end(ctx);
				ctx->style.window = savedWin;

				if (isSurvival()) {
					const struct nk_style_window savedVitals = pushTransparentWindow();
					const bool showAir = _player.isSubmerged() || _air < ac::PLAYER_MAX_AIR;
					const float vitalsH = 10.0f * scale;
					const float airH = showAir ? 10.0f * scale : 0.0f;
					if (nk_begin(ctx, "hud_vitals", nk_rect(barX, barY - vitalsH - airH - 2.0f * scale, barW, vitalsH + airH),
						NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT | NK_WINDOW_BACKGROUND)) {
						struct nk_command_buffer* canvas = nk_window_get_canvas(ctx);
						if (canvas && _guiIcons._shaderResourceView) {
							const float iconY = barY - vitalsH - 1.0f * scale;
							drawHudStatIcons(canvas, barX, iconY, scale, _health, false);
							drawHudStatIcons(canvas, barX + barW, iconY, scale, _hunger, true);
							if (showAir)
								drawHudAirBubbles(canvas, barX + barW, iconY - 10.0f * scale, scale, _air);
						}
					}
					nk_end(ctx);
					ctx->style.window = savedVitals;
				}
			}

			if (_screen == ac::gameScreen::mainMenu) {
				const bool hasContinue = !_lastWorldPath.empty() &&
					std::filesystem::exists(std::filesystem::path(_lastWorldPath) / "world.dat");
				if (nk_begin(ctx, "Main Menu", centered(360, hasContinue ? 300.0f : 250.0f),
					NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
					nk_layout_row_dynamic(ctx, 40, 1);
					if (hasContinue && nk_button_label(ctx, "Continue")) {
						playUiClick();
						loadExistingWorld(_lastWorldPath);
						if (!_worldSessionOpen)
							openWorldSelect(false);
					}
					if (nk_button_label(ctx, "Singleplayer")) { playUiClick(); openWorldSelect(); }
					if (nk_button_label(ctx, "Settings")) {
						playUiClick();
						_settingsReturnScreen = ac::gameScreen::mainMenu;
						_settingsTab = settingsTab::user;
						setScreen(ac::gameScreen::settings);
					}
					if (nk_button_label(ctx, "Quit")) { playUiClick(); _window.requestClose(); }
				}
				nk_end(ctx);
			}

			if (_screen == ac::gameScreen::worldSelect) {
				if (nk_begin(ctx, "Select World", centered(520, 460),
					NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
					nk_layout_row_dynamic(ctx, 18, 1);
					if (_worldList.empty())
						nk_label(ctx, "No saved worlds yet.", NK_TEXT_LEFT);
					else
						nk_label(ctx, "Choose a world to play.  Up/Down selects, Enter plays.", NK_TEXT_LEFT);
					nk_layout_row_dynamic(ctx, 250, 1);
					if (nk_group_begin(ctx, "world_list", NK_WINDOW_BORDER)) {
						for (int index = 0; index < static_cast<int>(_worldList.size()); ++index) {
							const ac::savedWorldInfo& info = _worldList[static_cast<size_t>(index)];
							char line[192];
							std::snprintf(line, sizeof(line), "%s%s  seed %llu  %s",
								index == _selectedWorldIndex ? "> " : "  ",
								info.name.c_str(),
								static_cast<unsigned long long>(info.seed),
								ac::formatWorldTime(info.lastPlayed).c_str());
							nk_layout_row_dynamic(ctx, 28, 1);
							if (nk_button_label(ctx, line)) {
								playUiClick();
								if (_selectedWorldIndex == index && _confirmDeleteIndex < 0)
									playSelectedWorld();
								else {
									_selectedWorldIndex = index;
									_confirmDeleteIndex = -1;
								}
							}
						}
						nk_group_end(ctx);
					}
					if (!_worldMenuError.empty()) {
						nk_layout_row_dynamic(ctx, 18, 1);
						nk_label_colored(ctx, _worldMenuError.c_str(), NK_TEXT_LEFT, nk_rgb(220, 80, 80));
					}
					nk_layout_row_dynamic(ctx, 36, 2);
					if (nk_button_label(ctx, "Play") && !_worldList.empty()) {
						playUiClick();
						playSelectedWorld();
					}
					if (nk_button_label(ctx, "Create New")) {
						playUiClick();
						beginCreateWorldScreen();
					}
					nk_layout_row_dynamic(ctx, 36, 2);
					if (_confirmDeleteIndex == _selectedWorldIndex && !_worldList.empty()) {
						if (nk_button_label(ctx, "Confirm Delete")) {
							playUiClick();
							deleteSelectedWorld();
						}
						if (nk_button_label(ctx, "Cancel")) {
							playUiClick();
							_confirmDeleteIndex = -1;
						}
					}
					else {
						if (nk_button_label(ctx, "Delete") && !_worldList.empty()) {
							playUiClick();
							_confirmDeleteIndex = _selectedWorldIndex;
						}
						if (nk_button_label(ctx, "Back")) {
							playUiClick();
							setScreen(ac::gameScreen::mainMenu);
						}
					}
				}
				nk_end(ctx);
			}

			if (_screen == ac::gameScreen::createWorld) {
				if (nk_begin(ctx, "Create World", centered(440, 280),
					NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
					nk_layout_row_dynamic(ctx, 20, 1);
					nk_label(ctx, "World name", NK_TEXT_LEFT);
					nk_layout_row_dynamic(ctx, 28, 1);
					if (_createWorldNameFocus) {
						nk_edit_focus(ctx, NK_EDIT_DEFAULT);
						_createWorldNameFocus = false;
					}
					nk_edit_string_zero_terminated(
						ctx, NK_EDIT_FIELD, _newWorldName.data(),
						static_cast<int>(_newWorldName.size()), nk_filter_default);
					nk_layout_row_dynamic(ctx, 20, 1);
					nk_label(ctx, "Seed (blank = random)", NK_TEXT_LEFT);
					nk_layout_row_dynamic(ctx, 28, 1);
					nk_edit_string_zero_terminated(
						ctx, NK_EDIT_FIELD, _newWorldSeed.data(),
						static_cast<int>(_newWorldSeed.size()), nk_filter_default);
					if (!_worldMenuError.empty()) {
						nk_layout_row_dynamic(ctx, 18, 1);
						nk_label_colored(ctx, _worldMenuError.c_str(), NK_TEXT_LEFT, nk_rgb(220, 80, 80));
					}
					nk_layout_row_dynamic(ctx, 40, 2);
					if (nk_button_label(ctx, "Create")) {
						playUiClick();
						createWorldFromMenu();
					}
					if (nk_button_label(ctx, "Cancel")) {
						playUiClick();
						openWorldSelect();
					}
				}
				nk_end(ctx);
			}

			if (_screen == ac::gameScreen::paused) {
				if (nk_begin(ctx, "Paused", centered(320, 280),
					NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_NO_SCROLLBAR)) {
					nk_layout_row_dynamic(ctx, 40, 1);
					if (nk_button_label(ctx, "Resume")) { playUiClick(); setScreen(ac::gameScreen::playing); }
					if (nk_button_label(ctx, "Save")) { playUiClick(); saveCurrentWorld(); }
					if (nk_button_label(ctx, "Settings")) {
						playUiClick();
						_settingsReturnScreen = ac::gameScreen::paused;
						_settingsTab = settingsTab::user;
						setScreen(ac::gameScreen::settings);
					}
					if (nk_button_label(ctx, "Save & Quit")) { playUiClick(); closeWorldToMenu(); }
				}
				nk_end(ctx);
			}

			if (_screen == ac::gameScreen::settings) {
				if (nk_begin(ctx, "Settings", centered(440, 640),
					NK_WINDOW_BORDER | NK_WINDOW_TITLE)) {
					nk_layout_row_dynamic(ctx, 32, 2);
					if (nk_button_label(ctx, _settingsTab == settingsTab::user ? "[ Video ]" : "Video")) {
						_settingsTab = settingsTab::user;
						playUiClick();
					}
					if (_worldSessionOpen) {
						if (nk_button_label(ctx, _settingsTab == settingsTab::world ? "[ World ]" : "World")) {
							_settingsTab = settingsTab::world;
							playUiClick();
						}
					}
					else {
						nk_label(ctx, "World (in game only)", NK_TEXT_CENTERED);
					}

					if (_settingsTab == settingsTab::user || !_worldSessionOpen) {
					nk_layout_row_dynamic(ctx, 24, 1);
					nk_label(ctx, "Video", NK_TEXT_LEFT);
					nk_bool vsync = _vsyncEnabled;
					if (nk_checkbox_label(ctx, "VSync", &vsync)) {
						_vsyncEnabled = vsync != 0;
						_graphicsSettings.setVsync(_vsyncEnabled);
						playUiClick();
					}
					{
						const char* aaNames[] = { "Off", "FXAA", "SMAA" };
						char aa[64];
						std::snprintf(aa, sizeof(aa), "Antialiasing: %s", aaNames[_postSettings.antialiasing % 3]);
						nk_label(ctx, aa, NK_TEXT_LEFT);
					}
					if (nk_button_label(ctx, "Cycle Antialiasing")) {
						_postSettings.antialiasing = (_postSettings.antialiasing + 1) % 3;
						_fxaaEnabled = _postSettings.antialiasing != 0;
						playUiClick();
					}
					nk_bool bloom = _postSettings.bloom;
					if (nk_checkbox_label(ctx, "Bloom / HDR Tonemap", &bloom)) {
						_postSettings.bloom = bloom != 0;
						playUiClick();
					}
					nk_bool gtao = _postSettings.gtao;
					if (nk_checkbox_label(ctx, "GTAO Ambient Occlusion", &gtao)) {
						_postSettings.gtao = gtao != 0;
						playUiClick();
					}
					nk_bool fog = _postSettings.fog;
					if (nk_checkbox_label(ctx, "Distance / Height Fog", &fog)) {
						_postSettings.fog = fog != 0;
						playUiClick();
					}
					nk_bool motionBlur = _motionBlurEnabled;
					if (nk_checkbox_label(ctx, "Motion Blur", &motionBlur)) {
						_motionBlurEnabled = motionBlur != 0;
						playUiClick();
					}
					nk_bool shadows = _shadowsEnabled;
					if (nk_checkbox_label(ctx, "Shadows", &shadows)) {
						_shadowsEnabled = shadows != 0;
						if (!_shadowsEnabled) {
							_celestialShadow.disable(_context);
							_shadowMap.disable(_context);
						}
						playUiClick();
					}
					if (_streamer.gpuTerrainAvailable()) {
						nk_bool gpuTerrain = _streamer.gpuTerrainEnabled();
						if (nk_checkbox_label(ctx, "GPU Terrain", &gpuTerrain)) {
							_streamer.setGpuTerrainEnabled(gpuTerrain != 0);
							playUiClick();
						}
					}
					else {
						nk_label(ctx, "GPU Terrain: unavailable", NK_TEXT_LEFT);
					}
					if (_livingEntities && _livingEntities->gpuAvailable()) {
						nk_bool gpuPaths = _livingEntities->gpuEnabled();
						if (nk_checkbox_label(ctx, "GPU Entity Pathfinding", &gpuPaths)) {
							_livingEntities->setGpuEnabled(gpuPaths != 0);
							playUiClick();
						}
					}
					else {
						nk_label(ctx, "GPU Entity Pathfinding: unavailable", NK_TEXT_LEFT);
					}

					char scaleLabel[64];
					std::snprintf(scaleLabel, sizeof(scaleLabel), "Render Scale: %d%%", RENDER_SCALES[_renderScaleIndex]);
					nk_label(ctx, scaleLabel, NK_TEXT_LEFT);
					nk_layout_row_dynamic(ctx, 26, 2);
					if (nk_button_label(ctx, "- Scale") && _renderScaleIndex > 0) {
						--_renderScaleIndex;
						_postSettings.renderScalePercent = RENDER_SCALES[_renderScaleIndex];
						playUiClick();
					}
					if (nk_button_label(ctx, "+ Scale") && _renderScaleIndex + 1 < RENDER_SCALES.size()) {
						++_renderScaleIndex;
						_postSettings.renderScalePercent = RENDER_SCALES[_renderScaleIndex];
						playUiClick();
					}

					{
						char fps[64];
						const int limit = FPS_LIMITS[_fpsLimitIndex];
						std::snprintf(fps, sizeof(fps), "FPS Limit: %s", limit == 0 ? "Unlimited" : std::to_string(limit).c_str());
						nk_label(ctx, fps, NK_TEXT_LEFT);
					}
					nk_layout_row_dynamic(ctx, 28, 2);
					if (nk_button_label(ctx, "- FPS") && _fpsLimitIndex > 0) { --_fpsLimitIndex; applyFpsLimit(); playUiClick(); }
					if (nk_button_label(ctx, "+ FPS") && _fpsLimitIndex + 1 < FPS_LIMITS.size()) { ++_fpsLimitIndex; applyFpsLimit(); playUiClick(); }

					nk_layout_row_dynamic(ctx, 26, 1);
					char fovLabel[64];
					std::snprintf(fovLabel, sizeof(fovLabel), "Field of View: %d", FOV_DEGREES[_fovIndex]);
					nk_label(ctx, fovLabel, NK_TEXT_LEFT);
					nk_layout_row_dynamic(ctx, 28, 2);
					if (nk_button_label(ctx, "- FOV") && _fovIndex > 0) {
						--_fovIndex; applyBaseFov(); playUiClick();
					}
					if (nk_button_label(ctx, "+ FOV") && _fovIndex + 1 < FOV_DEGREES.size()) {
						++_fovIndex; applyBaseFov(); playUiClick();
					}

					nk_layout_row_dynamic(ctx, 26, 1);
					const char* lightingNames[] = { "Fast", "Balanced", "Fancy" };
					char light[64];
					std::snprintf(light, sizeof(light), "Lighting: %s", lightingNames[_lightingQuality % 3u]);
					nk_label(ctx, light, NK_TEXT_LEFT);
					if (nk_button_label(ctx, "Cycle Lighting")) {
						_lightingQuality = (_lightingQuality + 1u) % 3u;
						applyLightingQuality();
						playUiClick();
					}
					char view[64];
					std::snprintf(view, sizeof(view), "View Distance: %d", VIEW_DISTANCES[_viewDistanceIndex]);
					nk_label(ctx, view, NK_TEXT_LEFT);
					nk_layout_row_dynamic(ctx, 28, 2);
					if (nk_button_label(ctx, "- View") && _viewDistanceIndex > 0) {
						--_viewDistanceIndex; _streamer.setViewDistance(VIEW_DISTANCES[_viewDistanceIndex]); playUiClick();
					}
					if (nk_button_label(ctx, "+ View") && _viewDistanceIndex + 1 < VIEW_DISTANCES.size()) {
						++_viewDistanceIndex; _streamer.setViewDistance(VIEW_DISTANCES[_viewDistanceIndex]); playUiClick();
					}
					}
					else {
					nk_layout_row_dynamic(ctx, 28, 1);
					nk_label(ctx, "World", NK_TEXT_LEFT);
					const char* daySpeedNames[] = { "Paused", "Slow", "Normal", "Fast" };
					char daySpeed[64];
					std::snprintf(daySpeed, sizeof(daySpeed), "Day Cycle: %s", daySpeedNames[_dayCycleIndex % 4u]);
					nk_label(ctx, daySpeed, NK_TEXT_LEFT);
					if (nk_button_label(ctx, "Cycle Day Speed")) {
						_dayCycleIndex = (_dayCycleIndex + 1) % DAY_CYCLE_SCALES.size(); playUiClick();
					}
					}
					nk_layout_row_dynamic(ctx, 36, 1);
					if (nk_button_label(ctx, "Done")) {
						playUiClick();
						setScreen(_settingsReturnScreen);
					}
				}
				nk_end(ctx);
			}

			if (_screen == ac::gameScreen::console) {
				if (_uiInput.keyPressed(GLFW_KEY_TAB))
					applyConsoleTabComplete();

				if (nk_begin(ctx, "Console", nk_rect(24, 24, std::min(640.0f, winW - 48.0f), 300),
					NK_WINDOW_BORDER | NK_WINDOW_TITLE | NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE)) {
					nk_layout_row_dynamic(ctx, 180, 1);
					if (nk_group_begin(ctx, "history", NK_WINDOW_BORDER)) {
						nk_layout_row_dynamic(ctx, 16, 1);
						const size_t start = _consoleHistoryCount > 12
							? _consoleHistoryCount - 12
							: 0;
						for (size_t i = start; i < _consoleHistoryCount; ++i)
							nk_label(ctx, _consoleHistory[i].c_str(), NK_TEXT_LEFT);
						nk_group_end(ctx);
					}
					nk_layout_row_dynamic(ctx, 28, 1);
					if (_consoleFocusEdit) {
						nk_edit_focus(ctx, NK_EDIT_DEFAULT);
						_consoleFocusEdit = false;
					}
					if (_consoleCursorToEnd && ctx->current) {
						const int bytes = (int)std::strlen(_consoleEditBuf.data());
						const int runes = nk_utf_len(_consoleEditBuf.data(), bytes);
						ctx->current->edit.cursor = runes;
						ctx->current->edit.sel_start = runes;
						ctx->current->edit.sel_end = runes;
						_consoleCursorToEnd = false;
					}
					const nk_flags editFlags = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_GOTO_END_ON_ACTIVATE;
					const nk_flags edit = nk_edit_string_zero_terminated(
						ctx, editFlags, _consoleEditBuf.data(),
						static_cast<int>(_consoleEditBuf.size()), nk_filter_default);
					if (std::strcmp(_consoleEditBuf.data(), _consoleBuffer.c_str()) != 0) {
						_consoleBuffer = _consoleEditBuf.data();
						resetConsoleTabComplete();
					}
					if ((edit & NK_EDIT_COMMITED) ||
						nk_input_is_key_pressed(&ctx->input, NK_KEY_ENTER)) {
						_consoleBuffer = _consoleEditBuf.data();
						if (!_consoleBuffer.empty()) {
							executeConsoleCommand(_consoleBuffer);
							setConsoleLine("");
							resetConsoleTabComplete();
							_consoleFocusEdit = true;
							_consoleCursorToEnd = true;
						}
					}
					nk_layout_row_dynamic(ctx, 18, 1);
					nk_label_colored(ctx, "Tab: autocomplete   Enter: run   Esc: close", NK_TEXT_LEFT, nk_rgb(184, 153, 87));
				}
				nk_end(ctx);
			}

			if (_screen == ac::gameScreen::inventory ||
				_screen == ac::gameScreen::crafting ||
				_screen == ac::gameScreen::chest ||
				_screen == ac::gameScreen::furnace) {
				drawContainerGui(ctx, winW, winH);
			}
		}


		struct blockRayHit {
			dx::XMINT3 block{};
			dx::XMINT3 adjacent{};
			ac::blockId id = 0;
			float distance = 0.0f;
		};

		std::optional<blockRayHit> raycastBlock(float reach = 5.0f) const {
			const dx::XMFLOAT3 origin = _player.getCamera()._gpuData._position;
			const dx::XMFLOAT3 direction = _player.getLookDirection();
			if (reach <= 0.0f)
				return std::nullopt;

			dx::XMINT3 cell{
				static_cast<int32_t>(std::floor(origin.x)),
				static_cast<int32_t>(std::floor(origin.y)),
				static_cast<int32_t>(std::floor(origin.z))
			};
			dx::XMINT3 previous = cell;

			const int stepX = direction.x >= 0.0f ? 1 : -1;
			const int stepY = direction.y >= 0.0f ? 1 : -1;
			const int stepZ = direction.z >= 0.0f ? 1 : -1;

			const float tDeltaX = std::abs(direction.x) < 1.0e-8f
				? std::numeric_limits<float>::infinity()
				: std::abs(1.0f / direction.x);
			const float tDeltaY = std::abs(direction.y) < 1.0e-8f
				? std::numeric_limits<float>::infinity()
				: std::abs(1.0f / direction.y);
			const float tDeltaZ = std::abs(direction.z) < 1.0e-8f
				? std::numeric_limits<float>::infinity()
				: std::abs(1.0f / direction.z);

			const float nextBoundaryX = static_cast<float>(cell.x) + (stepX > 0 ? 1.0f : 0.0f);
			const float nextBoundaryY = static_cast<float>(cell.y) + (stepY > 0 ? 1.0f : 0.0f);
			const float nextBoundaryZ = static_cast<float>(cell.z) + (stepZ > 0 ? 1.0f : 0.0f);

			float tMaxX = std::abs(direction.x) < 1.0e-8f
				? std::numeric_limits<float>::infinity()
				: (nextBoundaryX - origin.x) / direction.x;
			float tMaxY = std::abs(direction.y) < 1.0e-8f
				? std::numeric_limits<float>::infinity()
				: (nextBoundaryY - origin.y) / direction.y;
			float tMaxZ = std::abs(direction.z) < 1.0e-8f
				? std::numeric_limits<float>::infinity()
				: (nextBoundaryZ - origin.z) / direction.z;

			float traveled = 0.0f;
			for (int iteration = 0; iteration < 128 && traveled <= reach; ++iteration) {
				const ac::blockId id = _streamer.blockAt(cell.x, cell.y, cell.z);
				if (id != 0) {
					ac::blockAABB boxes[8];
					const size_t count = selectionBoxesAt(
						cell.x, cell.y, cell.z, id, boxes, 8);
					if (count != 0) {
						bool hitShape = false;
						float bestT = reach + 1.0f;
						const float ox = static_cast<float>(cell.x);
						const float oy = static_cast<float>(cell.y);
						const float oz = static_cast<float>(cell.z);
						for (size_t i = 0; i < count; ++i) {
							ac::blockAABB world = boxes[i];
							world.minX += ox; world.maxX += ox;
							world.minY += oy; world.maxY += oy;
							world.minZ += oz; world.maxZ += oz;
							float t = 0.0f;
							if (ac::rayAabb(
									origin.x, origin.y, origin.z,
									direction.x, direction.y, direction.z,
									world, t) &&
								t >= 0.0f && t <= reach && t < bestT) {
								bestT = t;
								hitShape = true;
							}
						}
						if (hitShape)
							return blockRayHit{ cell, previous, id, bestT };
					}
				}

				previous = cell;
				if (tMaxX < tMaxY) {
					if (tMaxX < tMaxZ) {
						traveled = tMaxX;
						tMaxX += tDeltaX;
						cell.x += stepX;
					}
					else {
						traveled = tMaxZ;
						tMaxZ += tDeltaZ;
						cell.z += stepZ;
					}
				}
				else if (tMaxY < tMaxZ) {
					traveled = tMaxY;
					tMaxY += tDeltaY;
					cell.y += stepY;
				}
				else {
					traveled = tMaxZ;
					tMaxZ += tDeltaZ;
					cell.z += stepZ;
				}
			}
			return std::nullopt;
		}

		void updateBlockInteraction(float dt) {
			if (_screen != ac::gameScreen::playing || !_cursorCaptured) {
				cancelBreak();
				_toolPickupRemaining = 0.0f;
				_toolPickupArmed = false;
				return;
			}

			const std::optional<blockRayHit> hit = _renderedTargetValid
				? std::optional<blockRayHit>{ blockRayHit{
					_renderedTargetBlock,
					_renderedTargetAdjacent,
					_renderedTargetId,
					_renderedTargetDistance
				} }
				: raycastBlock();
			const dx::XMFLOAT3 interactionOrigin = _player.getCamera()._gpuData._position;
			const dx::XMFLOAT3 interactionDirection = _player.getLookDirection();
			const std::optional<ac::entityRayHit> entityHit = _livingEntities
				? _livingEntities->raycast(interactionOrigin, interactionDirection, 5.0f)
				: std::nullopt;
			const bool entityIsClosest = entityHit && (!hit || entityHit->distance < hit->distance);

			const bool lmbDown = _uiInput.mouseDown(GLFW_MOUSE_BUTTON_LEFT);
			const bool lmbPressed = _uiInput.mousePressed(GLFW_MOUSE_BUTTON_LEFT);
			const bool rmbPressed = _uiInput.mousePressed(GLFW_MOUSE_BUTTON_RIGHT);
			const bool rmbDown = _uiInput.mouseDown(GLFW_MOUSE_BUTTON_RIGHT);
			const ac::heldItemStyle heldStyle = ac::heldItemStyleFor(
				selectedItemDef(), _hotbarCounts[_selectedHotbarSlot]);
			// Swords slash immediately; axes and the other mining tools retain their
			// short pickup into the raised/tool-ready pose.
			const bool holdingTool = heldStyle == ac::heldItemStyle::tool ||
				heldStyle == ac::heldItemStyle::axe;

			bool pickupJustFinished = false;
			if (!lmbDown) {
				cancelBreak();
				_toolPickupRemaining = 0.0f;
				_toolPickupArmed = false;
			}
			else if (!isSpectator()) {
				if (holdingTool) {
					if (!_toolPickupArmed) {
						_toolPickupArmed = true;
						_toolPickupRemaining = 0.20f;
					}
					if (_toolPickupRemaining > 0.0f) {
						_toolPickupRemaining -= dt;
						if (_toolPickupRemaining <= 0.0f) {
							_toolPickupRemaining = 0.0f;
							pickupJustFinished = true;
						}
					}
				}
				else {
					_toolPickupArmed = false;
					_toolPickupRemaining = 0.0f;
				}
				if (!holdingTool || _toolPickupRemaining <= 0.0f)
					_dynamicRenderer.triggerAttackAnimation(_playerEntity);
			}

			const bool waitingPickup = holdingTool && _toolPickupRemaining > 0.0f;

			const bool strikeEntity = entityIsClosest && !isSpectator() &&
				((!holdingTool && lmbPressed) || pickupJustFinished);
			if (strikeEntity && _livingEntities) {
				const float damage = heldEntityDamage();
				const float knockback = heldEntityKnockback();
				const ac::blockDefinition* weapon = selectedItemDef();
				const bool swordSweep = weapon && weapon->heldStyle() == ac::heldItemStyle::sword;
				const auto targetPosition = _livingEntities->entity(entityHit->entityIndex)->position;
				const bool hitPrimary = _livingEntities->damage(entityHit->entityIndex, damage,
					interactionDirection, knockback);
				if (hitPrimary) {
					_audio.playHurt(ac::soundPos::at(targetPosition.x, targetPosition.y + 0.9f,
						targetPosition.z));
					_particles.emitBurst({ targetPosition.x, targetPosition.y + 0.9f, targetPosition.z },
						{ 0.95f, 0.21f, 0.16f, 0.9f }, 10, 2.0f, 0.34f, 0.07f);
				}
				if (hitPrimary && swordSweep) {
					const dx::XMFLOAT3 sweepOrigin{
						interactionOrigin.x,
						interactionOrigin.y - _player.collisionBox().feetBelowEye,
						interactionOrigin.z
					};
					const size_t swept = _livingEntities->damageInArc(
						sweepOrigin, interactionDirection, 3.15f, 0.42f,
						(std::max)(1.0f, damage * 0.35f), knockback * 0.72f,
						[this](int32_t x, int32_t y, int32_t z) { return solidBlock(x, y, z); });
					if (swept > 0) {
						_particles.emitBurst(
							{ sweepOrigin.x + interactionDirection.x * 1.45f,
							  sweepOrigin.y + 1.05f,
							  sweepOrigin.z + interactionDirection.z * 1.45f },
							{ 0.86f, 0.90f, 0.96f, 0.9f }, 12, 2.8f, 0.30f, 0.075f);
					}
				}
				if (hitPrimary)
					addExhaustion(0.1f);
				cancelBreak();
			}

			if (lmbDown && entityIsClosest) {
				cancelBreak();
			}
			else if (lmbDown && hit && !isSpectator()) {
				const dx::XMINT3 block = hit->block;
				const ac::blockId brokenId = hit->id;
				const ac::blockId brokenType = ac::blockType(brokenId);
				const ac::blockDefinition* brokenDef = _blocks.get(brokenType);
				const ac::blockDefinition* heldDef = selectedItemDef();
				const float hardness = ac::blockHardness(brokenDef);
				if (isSurvival() && hardness < 0.0f) {
					cancelBreak();
				}
				else if (waitingPickup) {
					// Let the two-hand pickup finish before cracks or a break.
				}
				else if (isCreative()) {
					if (lmbPressed || pickupJustFinished) {
						finishBlockBreak(block, brokenId);
						cancelBreak();
					}
				}
				else {
					const float needed = ac::miningSeconds(brokenDef, heldDef, false);
					if (!_breakActive ||
						block.x != _breakTarget.x || block.y != _breakTarget.y || block.z != _breakTarget.z) {
						_breakActive = true;
						_breakTarget = block;
						_breakProgress = 0.0f;
					}
					_breakSecondsNeeded = needed;
					if (_breakSecondsNeeded <= 0.05f) {
						finishBlockBreak(block, brokenId);
						cancelBreak();
					}
					else {
						_breakProgress += dt / (std::max)(0.05f, _breakSecondsNeeded);
						if (_breakProgress >= 1.0f) {
							finishBlockBreak(block, brokenId);
							cancelBreak();
						}
					}
				}
			}
			else {
				cancelBreak();
			}

			if (lmbDown)
				cancelEat();

			if (!rmbPressed && updateEating(dt, rmbDown))
				return;
			if (!rmbPressed)
				return;
			if (entityIsClosest && _livingEntities && !isSpectator()) {
				_dynamicRenderer.triggerUseAnimation(_playerEntity);
				const ac::blockDefinition* held = selectedItemDef();
				const ac::livingEntity* target = _livingEntities->entity(entityHit->entityIndex);
				const dx::XMFLOAT3 dropPosition = target ? target->position : dx::XMFLOAT3{};
				if (held && held->_name == "shears" &&
					_livingEntities->shear(entityHit->entityIndex)) {
					const ac::blockId wool = _blocks.getId("white_wool");
					spawnItemDrop(wool, 2u,
						dropPosition.x, dropPosition.y + 0.55f, dropPosition.z,
						0.0f, 1.2f, 0.0f, 0.35f);
					_audio.play(ac::soundId::uiClick,
						ac::soundPos::at(dropPosition.x, dropPosition.y + 0.6f, dropPosition.z, 16.0f),
						0.75f);
				}
				return;
			}
			if (!hit) {
				if (updateEating(dt, true))
					return;
				return;
			}
			_dynamicRenderer.triggerUseAnimation(_playerEntity);

			const bool sneaking = _window.getKey(GLFW_KEY_LEFT_SHIFT) ||
				_window.getKey(GLFW_KEY_RIGHT_SHIFT) ||
				_player.isCrouching();
			const ac::blockId targetType = ac::blockType(hit->id);
			const ac::blockDefinition* targetDef = _blocks.get(targetType);

			// Sneaking: skip block interactions so place/break still work on doors, chests, etc.
			if (!sneaking) {
				if (isDoorType(targetType)) {
					const dx::XMINT3 block = hit->block;
					const bool upper = ac::isUpperHalf(hit->id) || isDoorTopType(hit->id);
					const int32_t bottomY = upper ? block.y - 1 : block.y;
					const int32_t topY = bottomY + 1;
					_audio.play(ac::isBlockOpen(hit->id)
						? ac::soundId::doorClose
						: ac::soundId::door,
						ac::soundPos::block(hit->block.x, hit->block.y, hit->block.z, 16.0f));
					_server.schedule([this, block, bottomY, topY]() {
						const ac::blockId existing = _streamer.blockAt(block.x, bottomY, block.z);
						if (!isDoorType(existing)) return;
						const bool open = !ac::isBlockOpen(existing);
						const bool powered = ac::isBlockPowered(existing);
						const uint32_t facing = ac::blockFacing(existing);
						const ac::blockId bottomType = doorPlaceBaseId(existing);
						const ac::blockId topType = doorTopId(bottomType);
						_streamer.setBlock(
							block.x, bottomY, block.z,
							ac::withUpperHalf(ac::withBlockPowered(ac::withBlockOpen(ac::withFacing(bottomType, facing), open), powered), false),
							_device, _context, true, false);
						_streamer.setBlock(
							block.x, topY, block.z,
							ac::withUpperHalf(ac::withBlockPowered(ac::withBlockOpen(ac::withFacing(topType, facing), open), powered), true),
							_device, _context, true, true);
						_streamer.updateWaterPhysics(_device, _context, 24u);
					});
					return;
				}
				if (effectiveUse(targetDef) == ac::blockUseBehavior::configureDiode) {
					const dx::XMINT3 block = hit->block;
					_audio.play(ac::soundId::uiClick, ac::soundPos::block(block.x, block.y, block.z, 12.0f), 0.7f);
					_server.schedule([this, block]() {
						const auto state = _streamer.blockAt(block.x, block.y, block.z);
						if (!ac::isDiode(state)) return;
						const auto next = ac::blockType(state) == ac::BLOCK_REPEATER
							? ac::withRepeaterDelay(state, ac::repeaterDelay(state) % 4u + 1u)
							: state ^ (1u << 28u);
						_streamer.setBlock(block.x, block.y, block.z, next, _device, _context);
					});
					return;
				}
				if (effectiveUse(targetDef) == ac::blockUseBehavior::togglePowered) {
					const dx::XMINT3 block = hit->block;
					const ac::blockId next = ac::withBlockPowered(
						hit->id, !ac::isBlockPowered(hit->id));
					_audio.play(ac::soundId::uiClick,
						ac::soundPos::block(hit->block.x, hit->block.y, hit->block.z, 12.0f), 0.7f);
					_server.schedule([this, block, next]() {
						_streamer.setBlock(block.x, block.y, block.z, next, _device, _context);
					});
					return;
				}
				if (effectiveUse(targetDef) == ac::blockUseBehavior::toggleOpen) {
					const dx::XMINT3 block = hit->block;
					const ac::blockId next = ac::withBlockOpen(
						hit->id, !ac::isBlockOpen(hit->id));
					_audio.play(ac::isBlockOpen(hit->id)
						? ac::soundId::doorClose : ac::soundId::door,
						ac::soundPos::block(block.x, block.y, block.z, 14.0f));
					_server.schedule([this, block, next]() {
						_streamer.setBlock(
							block.x, block.y, block.z, next,
							_device, _context, true, true);
						_streamer.updateWaterPhysics(_device, _context, 24u);
					});
					return;
				}
				if (effectiveUse(targetDef) == ac::blockUseBehavior::container ||
					targetType == BLOCK_CHEST) {
					_openChestBlock = hit->block;
					_blockEntities.chestAt(hit->block.x, hit->block.y, hit->block.z);
					_audio.play(ac::soundId::chestOpen,
						ac::soundPos::block(hit->block.x, hit->block.y, hit->block.z, 20.0f));
					setScreen(ac::gameScreen::chest);
					return;
				}
				if (targetType == BLOCK_FURNACE || targetType == BLOCK_BLAST_FURNACE ||
					targetType == BLOCK_FURNACE_LIT || targetType == BLOCK_BLAST_FURNACE_LIT) {
					_openFurnaceBlock = hit->block;
					_blockEntities.furnaceAt(hit->block.x, hit->block.y, hit->block.z);
					setScreen(ac::gameScreen::furnace);
					return;
				}
				if (effectiveUse(targetDef) == ac::blockUseBehavior::crafting ||
					targetType == ac::BLOCK_CRAFTING_TABLE) {
					setScreen(ac::gameScreen::crafting);
					return;
				}
			}

			if (isSpectator()) return;
			if (updateEating(dt, true))
				return;
			ac::blockId placeBlock = ac::normalizeBlockState(_selectedBlock);
			if (const ac::blockDefinition* selected = _blocks.get(ac::blockType(placeBlock));
				selected && selected->_behavior.placeAs != 0)
				placeBlock = selected->_behavior.placeAs;
			if (ac::isNonPlaceableItem(_blocks.get(placeBlock))) return;
			if (placeBlock != 0 && (isCreative() || _hotbarCounts[_selectedHotbarSlot] > 0)) {
				const dx::XMINT3 adjacent = hit->adjacent;
				const dx::XMINT3 support = hit->block;
				const ac::blockId block = placeBlock;
				const dx::XMFLOAT3 look = _player.getLookDirection();
				const uint32_t lookFacing = ac::facingFromLook(look.x, look.z);
				const int32_t faceDx = adjacent.x - support.x;
				const int32_t faceDy = adjacent.y - support.y;
				const int32_t faceDz = adjacent.z - support.z;
				const bool creative = isCreative();
				// The target can change between the raycast and the scheduled command.
				// Reject it both now and at the authoritative placement point so a
				// queued placement can never replace an existing block.
				if (_streamer.blockAt(adjacent.x, adjacent.y, adjacent.z) != 0)
					return;
				_server.schedule([this, adjacent, support, block, lookFacing, faceDx, faceDy, faceDz, creative]() {
					if (_streamer.blockAt(adjacent.x, adjacent.y, adjacent.z) != 0)
						return;
					const ac::blockDefinition* placing = _blocks.get(ac::blockType(block));
					const ac::blockPlacementBehavior placement = effectivePlacement(placing);
					const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
					ac::blockId oriented = block;
					if (ac::isDiode(block)) {
						const auto below = _streamer.blockAt(adjacent.x, adjacent.y - 1, adjacent.z);
						const auto* supportDefinition = _blocks.get(ac::blockType(below));
						if (!supportDefinition || !supportDefinition->_solid || !supportDefinition->_occludes) return;
					}
					if (ac::isRedstoneWire(block)) {
						oriented = ac::withFacing(block, lookFacing);
					}
					else if (placement == ac::blockPlacementBehavior::wallOnly) {
						if (faceDy != 0) return;
						oriented = ac::withFacing(
							block, ac::facingFromHitDelta(faceDx, faceDy, faceDz));
					}
					else if (placement == ac::blockPlacementBehavior::trapdoor) {
						const uint32_t hingeFacing = faceDy == 0
							? ac::facingFromHitDelta(faceDx, faceDy, faceDz)
							: lookFacing;
						oriented = ac::withBlockOpen(
							ac::withFacing(block, hingeFacing), false);
					}
					else if (placement == ac::blockPlacementBehavior::wallOrFloor) {
						if (faceDy == 0 && (faceDx != 0 || faceDz != 0)) {
							const uint32_t wallFacing = ac::facingFromHitDelta(faceDx, faceDy, faceDz);
							oriented = ac::withWallAttached(
								ac::withFacing(block, wallFacing), true);
						}
						else if (faceDy < 0) {
							return;
						}
						else {
							oriented = ac::withWallAttached(
								ac::withFacing(block, lookFacing), false);
						}
						if ((placing && placing->_behavior.poweredOnPlace) || ac::isRedstoneTorch(block))
							oriented = ac::withBlockPowered(oriented, true);
					}
					else if (placement == ac::blockPlacementBehavior::horizontalFacing ||
						(placing && (ac::usesPlacementFacing(placing->_model) ||
						ac::blockUsesFacingState(block)))) {
						oriented = ac::withFacing(block, lookFacing);
					}
					if (!_spectatorMode &&
						ac::playerOverlapsBlock(
							eye.x, eye.y, eye.z,
							adjacent.x, adjacent.y, adjacent.z, placing, oriented,
							0.002f,
							stairShapeAt(adjacent.x, adjacent.y, adjacent.z, oriented),
							fenceConnectionsAt(adjacent.x, adjacent.y, adjacent.z, oriented),
							_player.collisionBox()))
						return;
					if (placement == ac::blockPlacementBehavior::door || isDoorPlaceBase(placing) ||
						(placing && placing->_interaction == ac::blockInteraction::door &&
							!ac::isUpperHalf(oriented))) {
						const ac::blockId bottomType = doorPlaceBaseId(oriented);
						const ac::blockId topType = doorTopId(bottomType);
						const ac::blockId bottomId = ac::withUpperHalf(
							ac::withBlockOpen(ac::withFacing(bottomType, lookFacing), false), false);
						const ac::blockId topId = ac::withUpperHalf(
							ac::withBlockOpen(ac::withFacing(topType, lookFacing), false), true);
						if (adjacent.y + 1 >= CHUNK_HEIGHT) return;
						if (_streamer.blockAt(adjacent.x, adjacent.y + 1, adjacent.z) != 0)
							return;
						const ac::blockDefinition* topDef = _blocks.get(topType);
						if (!topDef) return;
						if (!_spectatorMode &&
							ac::playerOverlapsBlock(
								eye.x, eye.y, eye.z,
								adjacent.x, adjacent.y + 1, adjacent.z, topDef, topId,
								0.002f, ac::stairShape::straight, {}, _player.collisionBox()))
							return;
						if (!_streamer.setBlock(
								adjacent.x, adjacent.y, adjacent.z,
								bottomId,
								_device, _context, true, false))
							return;
						if (!_streamer.setBlock(
								adjacent.x, adjacent.y + 1, adjacent.z,
								topId,
								_device, _context, true, true)) {
							_streamer.setBlock(
								adjacent.x, adjacent.y, adjacent.z,
								0,
								_device, _context, true, true);
							return;
						}
						emitBlockBurst(adjacent, block, 14, 2.2f);
						_audio.playPlace(materialForBlock(block),
							ac::soundPos::block(adjacent.x, adjacent.y, adjacent.z, 24.0f));
						_streamer.updateWaterPhysics(_device, _context, 24u);
						if (!creative) consumeSelected(1);
						return;
					}
					if (_streamer.setBlock(adjacent.x, adjacent.y, adjacent.z, oriented, _device, _context)) {
						if (usesChestEntity(placing))
							_blockEntities.chestAt(adjacent.x, adjacent.y, adjacent.z);
						else if (ac::blockType(oriented) == BLOCK_FURNACE ||
							ac::blockType(oriented) == BLOCK_BLAST_FURNACE ||
							ac::blockType(oriented) == BLOCK_FURNACE_LIT ||
							ac::blockType(oriented) == BLOCK_BLAST_FURNACE_LIT)
							_blockEntities.furnaceAt(adjacent.x, adjacent.y, adjacent.z);
						emitBlockBurst(adjacent, block, 14, 2.2f);
						_audio.playPlace(materialForBlock(block),
							ac::soundPos::block(adjacent.x, adjacent.y, adjacent.z, 24.0f));
						_streamer.updateWaterPhysics(_device, _context, 24u);
						if (!creative) consumeSelected(1);
					}
				});
			}
		}

		void updateLightOcclusionVolumes() {
			ac::lightOcclusionBufferData volumeData{};

			uint64_t signature = static_cast<uint64_t>(_lightData.size());
			for (const ac::gpuLight& light : _lightData) {
				signature = signature * 0x100000001b3ull ^
					static_cast<uint64_t>(static_cast<uint32_t>(
						static_cast<int32_t>(std::floor(light.position.x))));
				signature = signature * 0x100000001b3ull ^
					static_cast<uint64_t>(static_cast<uint32_t>(
						static_cast<int32_t>(std::floor(light.position.y))));
				signature = signature * 0x100000001b3ull ^
					static_cast<uint64_t>(static_cast<uint32_t>(
						static_cast<int32_t>(std::floor(light.position.z))));
				signature = signature * 0x100000001b3ull ^ light.type;
			}

			const uint64_t revision = _streamer.blockRevision();
			if (signature == _lightOcclusionSignature &&
				revision == _lightOcclusionBlockRevision) {
				return;
			}

			const size_t lightLimit = (std::min)(
				_lightData.size(), static_cast<size_t>(ac::LIGHT_OCCLUSION_LIGHTS));
			std::vector<uint8_t> rebuild(lightLimit, 0u);

			dx::XMINT3 changedBlock{};
			const bool lightsUnchanged = signature == _lightOcclusionSignature;
			const bool localizedEdit = lightsUnchanged &&
				_streamer.localizedBlockChange(revision, changedBlock);

			if (localizedEdit) {
				for (size_t lightIndex = 0; lightIndex < lightLimit; ++lightIndex) {
					const ac::gpuLight& light = _lightData[lightIndex];
					if (light.type == ac::GPU_LIGHT_DIRECTIONAL_PROXY || light.radius <= 0.0f)
						continue;
					if (blockLightCanReach(light, changedBlock))
						rebuild[lightIndex] = 1u;
				}
			}
			else {
				std::fill(rebuild.begin(), rebuild.end(), static_cast<uint8_t>(1u));
			}

			const auto blockOccludesLight = [this](uint32_t id) -> bool {
				const uint32_t type = ac::blockType(id);
				if (type >= _blockOcclusionLookup.size()) {
					const ac::blockDefinition* definition = _blocks.get(type);
					return definition && definition->_occludes &&
						definition->_renderMode != ac::RENDER_MODE_TRANSPARENT;
				}
				uint8_t& entry = _blockOcclusionLookup[type];
				if (entry != 2u)
					return entry != 0u;
				const ac::blockDefinition* definition = _blocks.get(type);
				entry = (definition && definition->_occludes &&
					definition->_renderMode != ac::RENDER_MODE_TRANSPARENT) ? 1u : 0u;
				return entry != 0u;
			};

			constexpr int32_t half = static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE / 2u);
			for (size_t lightIndex = 0; lightIndex < lightLimit; ++lightIndex) {
				const ac::gpuLight& light = _lightData[lightIndex];
				if (light.type == ac::GPU_LIGHT_DIRECTIONAL_PROXY || light.radius <= 0.0f) {
					volumeData.origins[lightIndex] = { 0, 0, 0, 0 };
					rebuild[lightIndex] = 0u;
					continue;
				}
				const dx::XMINT3 origin{
					static_cast<int32_t>(std::floor(light.position.x)) - half,
					static_cast<int32_t>(std::floor(light.position.y)) - half,
					static_cast<int32_t>(std::floor(light.position.z)) - half
				};
				volumeData.origins[lightIndex] = { origin.x, origin.y, origin.z, 0 };
				if (rebuild[lightIndex]) {
					const size_t wordOffset = lightIndex * ac::LIGHT_OCCLUSION_WORDS;
					std::fill_n(
						_lightOccluderData.begin() + static_cast<std::ptrdiff_t>(wordOffset),
						ac::LIGHT_OCCLUSION_WORDS,
						0u);
				}
			}

			const bool anyRebuild = std::any_of(
				rebuild.begin(), rebuild.end(),
				[](uint8_t value) { return value != 0u; });
			if (anyRebuild) {
				for (auto& pair : _streamer.chunks()) {
					const int32_t chunkX = pair.first.x;
					const int32_t chunkZ = pair.first.z;
					const ac::chunk* source = pair.second._chunk.get();
					if (!source) continue;

					const int32_t worldMinX = chunkX * CHUNK_WIDTH;
					const int32_t worldMinZ = chunkZ * CHUNK_LENGTH;
					const int32_t worldMaxX = worldMinX + CHUNK_WIDTH;
					const int32_t worldMaxZ = worldMinZ + CHUNK_LENGTH;

					for (size_t lightIndex = 0; lightIndex < lightLimit; ++lightIndex) {
						if (!rebuild[lightIndex]) continue;
						const dx::XMINT3 origin{
							volumeData.origins[lightIndex].x,
							volumeData.origins[lightIndex].y,
							volumeData.origins[lightIndex].z
						};
						const int32_t endX = origin.x + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE);
						const int32_t endY = origin.y + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE);
						const int32_t endZ = origin.z + static_cast<int32_t>(ac::LIGHT_OCCLUSION_SIZE);

						const int32_t minX = (std::max)(origin.x, worldMinX);
						const int32_t maxX = (std::min)(endX, worldMaxX);
						const int32_t minY = (std::max)(origin.y, 0);
						const int32_t maxY = (std::min)(endY, CHUNK_HEIGHT);
						const int32_t minZ = (std::max)(origin.z, worldMinZ);
						const int32_t maxZ = (std::min)(endZ, worldMaxZ);
						if (minX >= maxX || minY >= maxY || minZ >= maxZ)
							continue;

						for (int32_t y = minY; y < maxY;) {
							const uint32_t section = static_cast<uint32_t>(y) / SUBCHUNK_HEIGHT;
							if (source->isSubchunkEmpty(section)) {
								y = static_cast<int32_t>((section + 1u) * SUBCHUNK_HEIGHT);
								continue;
							}
							const int32_t sectionEnd = (std::min)(
								maxY, static_cast<int32_t>((section + 1u) * SUBCHUNK_HEIGHT));
							for (; y < sectionEnd; ++y) {
								for (int32_t z = minZ; z < maxZ; ++z) {
									for (int32_t x = minX; x < maxX; ++x) {
										const uint32_t localX = static_cast<uint32_t>(x - worldMinX);
										const uint32_t localY = static_cast<uint32_t>(y);
										const uint32_t localZ = static_cast<uint32_t>(z - worldMinZ);
										const ac::blockId id = source->getBlock(localX, localY, localZ);
										if (id == 0u || !blockOccludesLight(id))
											continue;
										const uint32_t lx = static_cast<uint32_t>(x - origin.x);
										const uint32_t ly = static_cast<uint32_t>(y - origin.y);
										const uint32_t lz = static_cast<uint32_t>(z - origin.z);
										const uint32_t localIndex = lx + ac::LIGHT_OCCLUSION_SIZE *
											(lz + ac::LIGHT_OCCLUSION_SIZE * ly);
										_lightOccluderData[lightIndex * ac::LIGHT_OCCLUSION_WORDS + (localIndex >> 5)] |=
											(1u << (localIndex & 31u));
									}
								}
							}
						}
					}
				}

				_lightOccluders.update(
					_context,
					_lightOccluderData.data(),
					static_cast<UINT>(_lightOccluderData.size()));
			}

			_lightOcclusionBuffer.update(_context, volumeData);
			_lightOcclusionSignature = signature;
			_lightOcclusionBlockRevision = revision;
		}

		void updateHudInput() {
			// Hotbar selection only — all panels are drawn with Nuklear.
			for (int slot = 0; slot < HUD_HOTBAR_COUNT; ++slot) {
				if (!(_screen == ac::gameScreen::inventory && _chooserSearchFocus) &&
					_window.getKeyPress(GLFW_KEY_1 + slot))
					_selectedHotbarSlot = slot;
			}
			if (_screen == ac::gameScreen::playing) {
				const float scrollY = static_cast<float>(_window.getScrollY());
				if (scrollY != 0.0f) {
					const int wheelSteps = std::max(1, static_cast<int>(std::lround(std::abs(scrollY))));
					const int direction = scrollY > 0.0f ? -1 : 1;
					_selectedHotbarSlot = (_selectedHotbarSlot + direction * wheelSteps) % HUD_HOTBAR_COUNT;
					if (_selectedHotbarSlot < 0) _selectedHotbarSlot += HUD_HOTBAR_COUNT;
				}
			}
			_selectedBlock = _hotbarBlocks[_selectedHotbarSlot];
			if (_screen != ac::gameScreen::inventory &&
				_screen != ac::gameScreen::chest &&
				_screen != ac::gameScreen::crafting &&
				_screen != ac::gameScreen::furnace)
				endInventoryPaint();
		}

		bool onUpdate(const ac::frameContext& frame) override {
			_uiInput.update(_window);
			syncNuklearMouse();
			updateHudInput();
			_particles.update(frame.deltaTime);
			const bool freezeSimulation = ac::gameScreenFreezesSimulation(_screen);
			if (!freezeSimulation && _worldSessionOpen)
				updateWeather(frame.deltaTime);
			if (!freezeSimulation)
				updateDaylight(frame.deltaTime);
			if (_screen == ac::gameScreen::playing && _uiInput.keyPressed(GLFW_KEY_F5)) {
				_cameraMode = static_cast<uint8_t>((_cameraMode + 1u) % 3u);
				_bodycamInitialized = false;
			}
			if (_screen == ac::gameScreen::playing && _uiInput.keyPressed(GLFW_KEY_F6)) {
				toggleFlyMode();
			}
			bool suppressWorldClick = false;

			// Prime streaming at the requested start chunk, then activate physics
			// only after a safe terrain-relative spawn can be resolved.
			if (_worldSessionOpen && _spawnPending) {
				dx::XMFLOAT3 savedPosition = _player.getCamera()._gpuData._position;
				if (_restoredPlayerPendingValidation) {
					_streamer.update(savedPosition, _device, _context);
					const int32_t chunkX = static_cast<int32_t>(
						std::floor(savedPosition.x / static_cast<float>(CHUNK_WIDTH)));
					const int32_t chunkZ = static_cast<int32_t>(
						std::floor(savedPosition.z / static_cast<float>(CHUNK_LENGTH)));
					if (_streamer.chunkLoaded(chunkX, chunkZ) && playerPositionClear(savedPosition)) {
						_restoredPlayerPendingValidation = false;
						_spawnPending = false;
					}
				}

				if (_spawnPending && !_spawnLandFound && !_restoredPlayerPendingValidation) {
					if (ac::terrainGenerator* generator = _world.generator()) {
						constexpr uint32_t probesPerFrame = 16u;
						for (uint32_t i = 0; i < probesPerFrame && !_spawnLandFound; ++i) {
							const uint32_t step = _spawnSearchStep++;
							const int32_t stride = step < 96u ? 12 : 2;
							const dx::XMINT2 ring = spawnSearchChunk(step < 96u ? step : step - 96u);
							const int32_t chunkX = ring.x * stride;
							const int32_t chunkZ = ring.y * stride;
							const int32_t worldX = chunkX * CHUNK_WIDTH + CHUNK_WIDTH / 2;
							const int32_t worldZ = chunkZ * CHUNK_LENGTH + CHUNK_LENGTH / 2;
							if (!generator->isLandSpawnColumn(worldX, worldZ))
								continue;
							_spawnLandFound = true;
							_spawnLandChunk = { chunkX, chunkZ };
							const float surface = generator->peekSurfaceHeight(worldX, worldZ);
							_player.teleport({
								static_cast<float>(worldX) + 0.5f,
								surface + 4.0f,
								static_cast<float>(worldZ) + 0.5f
							});
						}
					}
					else {
						_spawnLandFound = true;
						_spawnLandChunk = spawnSearchChunk(_spawnSearchStep);
					}
				}

				if (_spawnPending && _spawnLandFound) {
					const float hoverY = _player.getCamera()._gpuData._position.y;
					_player.teleport({
						static_cast<float>(_spawnLandChunk.x * CHUNK_WIDTH) + 8.0f,
						hoverY > 8.0f ? hoverY : 120.0f,
						static_cast<float>(_spawnLandChunk.y * CHUNK_LENGTH) + 8.0f
					});
					_streamer.update(_player.getCamera()._gpuData._position, _device, _context);
					if (const std::optional<dx::XMFLOAT3> terrainSpawn = initialSpawnPoint()) {
						_player.teleport(*terrainSpawn);
						_restoredPlayerPendingValidation = false;
						_spawnPending = false;
					}
					else if (_streamer.chunkLoaded(_spawnLandChunk.x, _spawnLandChunk.y)) {
						_spawnLandFound = false;
					}
				}
				else if (_spawnPending) {
					_streamer.update(_player.getCamera()._gpuData._position, _device, _context);
				}
			}

			if (_uiInput.keyPressed(GLFW_KEY_ESCAPE)) {
				if (_screen == ac::gameScreen::inventory && _chooserSearchFocus) {
					_chooserSearchFocus = false;
					suppressWorldClick = true;
				}
				else if (_screen == ac::gameScreen::inventory) {
					setInventoryOpen(false);
					suppressWorldClick = true;
				}
				else if (_screen == ac::gameScreen::chest ||
					_screen == ac::gameScreen::crafting ||
					_screen == ac::gameScreen::furnace ||
					_screen == ac::gameScreen::console) {
					if (_screen == ac::gameScreen::console)
						closeConsole();
					else
						setScreen(ac::gameScreen::playing);
					suppressWorldClick = true;
				}
				else if (_screen == ac::gameScreen::createWorld) {
					openWorldSelect();
					suppressWorldClick = true;
				}
				else if (_screen == ac::gameScreen::worldSelect) {
					if (_confirmDeleteIndex >= 0)
						_confirmDeleteIndex = -1;
					else
						setScreen(ac::gameScreen::mainMenu);
					suppressWorldClick = true;
				}
				else if (_screen == ac::gameScreen::settings) {
					setScreen(_settingsReturnScreen);
					suppressWorldClick = true;
				}
				else if (_screen == ac::gameScreen::playing) {
					saveCurrentWorld();
					setScreen(ac::gameScreen::paused);
					suppressWorldClick = true;
				}
				else if (_screen == ac::gameScreen::paused) {
					setScreen(ac::gameScreen::playing);
					suppressWorldClick = true;
				}
			}
			else if (_screen == ac::gameScreen::inventory && _chooserSearchFocus) {
				// Text editing owns the keyboard until Escape or an outside click.
				// Consume shortcut edges now so they cannot fire after focus leaves.
				suppressWorldClick = true;
			}
			else if ((_screen == ac::gameScreen::inventory ||
				_screen == ac::gameScreen::crafting ||
				_screen == ac::gameScreen::chest ||
				_screen == ac::gameScreen::furnace) && _uiInput.keyPressed(GLFW_KEY_E)) {
				setInventoryOpen(false);
				suppressWorldClick = true;
			}
			else if (_screen == ac::gameScreen::worldSelect &&
				(_uiInput.keyPressed(GLFW_KEY_UP) || _uiInput.keyPressed(GLFW_KEY_DOWN))) {
				if (!_worldList.empty()) {
					const int direction = _uiInput.keyPressed(GLFW_KEY_UP) ? -1 : 1;
					_selectedWorldIndex = (_selectedWorldIndex + direction +
						static_cast<int>(_worldList.size())) % static_cast<int>(_worldList.size());
					_confirmDeleteIndex = -1;
					playUiClick();
				}
				suppressWorldClick = true;
			}
			else if (_screen == ac::gameScreen::worldSelect &&
				_uiInput.keyPressed(GLFW_KEY_ENTER) && _confirmDeleteIndex < 0 && !_worldList.empty()) {
				playUiClick();
				playSelectedWorld();
				suppressWorldClick = true;
			}
			else if (_screen == ac::gameScreen::worldSelect &&
				_uiInput.keyPressed(GLFW_KEY_DELETE) && !_worldList.empty()) {
				_confirmDeleteIndex = _selectedWorldIndex;
				playUiClick();
				suppressWorldClick = true;
			}
			else if (_screen == ac::gameScreen::createWorld && _uiInput.keyPressed(GLFW_KEY_ENTER)) {
				playUiClick();
				createWorldFromMenu();
				suppressWorldClick = true;
			}
			else if (_screen == ac::gameScreen::paused && _uiInput.keyPressed(GLFW_KEY_ENTER)) {
				playUiClick();
				setScreen(ac::gameScreen::playing);
				suppressWorldClick = true;
			}
			else if (_screen == ac::gameScreen::playing && _uiInput.keyPressed(GLFW_KEY_E)) {
				setInventoryOpen(true);
				suppressWorldClick = true;
			}
			else if (_screen == ac::gameScreen::playing &&
				(_uiInput.keyPressed(GLFW_KEY_T) || _uiInput.keyPressed(GLFW_KEY_GRAVE_ACCENT))) {
				openConsole(false);
				suppressWorldClick = true;
			}
			else if (_screen == ac::gameScreen::playing && _uiInput.keyPressed(GLFW_KEY_SLASH)) {
				openConsole(true);
				suppressWorldClick = true;
			}

			updateConsole();

			if (!suppressWorldClick && !freezeSimulation)
				updateBlockInteraction(frame.deltaTime);

			if (_screen == ac::gameScreen::playing && _cursorCaptured &&
				!freezeSimulation && _window.getKeyPress(GLFW_KEY_Q)) {
				const bool entireStack =
					_window.getKey(GLFW_KEY_LEFT_CONTROL) || _window.getKey(GLFW_KEY_RIGHT_CONTROL);
				dropSelectedItem(entireStack);
			}

			if (!freezeSimulation) {
				_blockEntities.updateFurnaces(
					frame.deltaTime,
					[this](uint32_t input, uint32_t& result, uint32_t& count, float& seconds) {
						const ac::smeltingRecipe* recipe = _recipes.smelting(static_cast<ac::blockId>(input));
						if (!recipe) return false;
						result = recipe->result;
						count = recipe->resultCount;
						seconds = recipe->seconds;
						return true;
					},
					[this](uint32_t fuel) { return _recipes.fuelSeconds(static_cast<ac::blockId>(fuel)); },
					[this](const ac::blockPosKey& position, bool burning) {
						const ac::blockId state = _streamer.blockAt(position.x, position.y, position.z);
						const ac::blockId type = ac::blockType(state);
						ac::blockId wanted = 0;
						if (type == BLOCK_FURNACE || type == BLOCK_FURNACE_LIT)
							wanted = burning ? BLOCK_FURNACE_LIT : BLOCK_FURNACE;
						else if (type == BLOCK_BLAST_FURNACE || type == BLOCK_BLAST_FURNACE_LIT)
							wanted = burning ? BLOCK_BLAST_FURNACE_LIT : BLOCK_BLAST_FURNACE;
						if (wanted != 0 && wanted != type) {
							const ac::blockId next = wanted | (state & ~ac::BLOCK_TYPE_MASK);
							_streamer.setBlock(position.x, position.y, position.z,
								next, _device, _context, true, true);
						}
					});
				_server.submitInput(captureServerInput());
				_server.advance(frame.deltaTime,
					[this](const ac::serverStep& step, const ac::serverInput& input) {
						_modHost.publishTick(step.tick, step.subtick, step.deltaTime);
						if (!_spawnPending) {
							_player.update(
								input,
								{ 0.05f, 0.05f, 0.05f },
								19.0f,
								step.deltaTime,
								[this](int32_t x, int32_t y, int32_t z, float eyeX, float eyeY, float eyeZ) {
									if (y < 0) return true;
									const ac::blockId id = _streamer.blockAt(x, y, z, _blockReadCache);
									if (id == 0u) return false;
									const ac::blockDefinition* definition = _blocks.get(ac::blockType(id));
									if (!definition || !definition->_solid) return false;
									if (definition->_model == ac::MODEL_CUBE)
										return ac::eyeCollidesWithCell(
											eyeX, eyeY, eyeZ, x, y, z, definition, id,
											ac::stairShape::straight, {}, _player.collisionBox());
									return ac::eyeCollidesWithCell(
										eyeX, eyeY, eyeZ, x, y, z, definition, id,
										stairShapeAt(x, y, z, id),
										fenceConnectionsAt(x, y, z, id),
										_player.collisionBox());
								},
								[this](float x, float y, float z) {
									const int32_t blockY = static_cast<int32_t>(std::floor(y));
									const ac::blockId state = _streamer.blockAt(
										static_cast<int32_t>(std::floor(x)),
										blockY,
										static_cast<int32_t>(std::floor(z)),
										_blockReadCache);
									return ac::blockType(state) == ac::WATER_BLOCK_TYPE &&
										y - static_cast<float>(blockY) < ac::fluidHeight(state);
								},
								_spectatorMode,
								isCreative()
							);
							updateFallTracking(step.deltaTime);
							applyWaterCurrent(step.deltaTime);
						}

						// Advance exactly one bounded water wave per 32 Hz game tick. This is
						// half the previous propagation rate; visual flow remains frame-based.
						if (step.subtick == 0u) {
							_streamer.updateWaterPhysics(_device, _context, 128u);
							_streamer.updateFallingPhysics(_device, _context, 24u);
							// Redstone runs once on the authoritative game-tick boundary.
							// The large bounded pass lets ordinary circuits settle in the same
							// tick instead of changing speed with frame timing or circuit size.
							_streamer.updateRedstonePhysics(_device, _context, 65536u);
						}

						if (!diagnosticFrameCaptureEnabled() && !_spawnPending &&
							step.subtick == ac::simulationServer::SUBTICKS_PER_TICK - 1u &&
							step.tick > 0u && step.tick % (ac::simulationServer::TICKS_PER_SECOND * 5u) == 0u) {
							capturePlayerState();
							_items.saveToWorld(_world);
							_blockEntities.save();
							_world.saveEntityData();
						}
					});
			}

			_crushedFallingScratch.clear();
			_streamer.updateFallingEntities(
				_device,
				_context,
				frame.deltaTime,
				[this](int32_t x, int32_t y, int32_t z) {
					return !isSpectator() && _player.occupiesBlock(x, y, z);
				},
				&_crushedFallingScratch);
			updateFallingBlockDamage();
			for (ac::fallingBlockEntity& crushed : _crushedFallingScratch) {
				hurtFromFallingBlock(crushed, true);
				if (crushed.id != 0)
					spawnItemDrop(
						crushed.id, 1,
						static_cast<float>(crushed.x) + 0.5f,
						crushed.y + 0.35f,
						static_cast<float>(crushed.z) + 0.5f,
						((static_cast<float>(crushed.x) * 0.37f) - std::floor(crushed.x * 0.37f)) * 0.6f - 0.3f,
						0.35f,
						((static_cast<float>(crushed.z) * 0.53f) - std::floor(crushed.z * 0.53f)) * 0.6f - 0.3f,
						0.15f);
			}
			if (!freezeSimulation)
				updateItemEntities(frame.deltaTime);
			if (!freezeSimulation)
				updateVitals(frame.deltaTime);
			if (!freezeSimulation && _worldSessionOpen && !_spawnPending && _livingEntities) {
				const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
				const dx::XMFLOAT3 feet{
					eye.x, eye.y - _player.collisionBox().feetBelowEye, eye.z
				};
				_livingEntities->update(
					frame.deltaTime,
					feet,
					[this](int32_t x, int32_t y, int32_t z) { return solidBlock(x, y, z); },
					[this](const ac::livingEntity& attacker, float damage, float knockback) {
						if (!applyPlayerDamage(damage)) return;
						const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
						float dx = eye.x - attacker.position.x;
						float dz = eye.z - attacker.position.z;
						const float length = std::sqrt(dx * dx + dz * dz);
						if (length > 0.0001f) { dx /= length; dz /= length; }
						_player.addVelocity({ dx * knockback, 2.15f, dz * knockback });
					});
			}

			if (_screen == ac::gameScreen::playing && !_spectatorMode) {
				const dx::XMFLOAT3 velocity = _player.getVelocity();
				const bool inWater = _player.inWater() || _player.isSwimming();
				if (inWater && !_wasInWater && !_spawnPending) {
					const float impact = std::abs(velocity.y);
					const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
					_audio.playSplash(ac::soundPos::at(eye.x, eye.y, eye.z, 18.0f),
						impact > 8.0f ? 0.95f : 0.62f);
				}
				_wasInWater = inWater;

				if (inWater) {
					const float swimSpeed = std::sqrt(
						velocity.x * velocity.x +
						velocity.y * velocity.y +
						velocity.z * velocity.z);
					if (swimSpeed > 0.55f) {
						_footstepTimer -= frame.deltaTime;
						if (_footstepTimer <= 0.0f) {
							const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
							_audio.playSwim(ac::soundPos::at(eye.x, eye.y, eye.z, 14.0f),
								_player.isSwimming() ? 0.48f : 0.32f);
							_footstepTimer = (std::max)(0.28f, 0.55f - swimSpeed * 0.04f);
						}
					}
					else {
						_footstepTimer = 0.0f;
					}
				}
				else if (_player.isGrounded()) {
					const float horizontalSpeed = std::sqrt(
						velocity.x * velocity.x + velocity.z * velocity.z);
					if (horizontalSpeed > FOOTSTEP_SPEED_THRESHOLD) {
						_footstepTimer -= frame.deltaTime;
						if (_footstepTimer <= 0.0f) {
							const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
							const dx::XMFLOAT3 feet{
								eye.x,
								eye.y - _player.collisionBox().feetBelowEye,
								eye.z
							};
							const ac::blockId ground = footingBlockAt(eye);
							_audio.playStep(materialForBlock(ground),
								ac::soundPos::at(feet.x, feet.y, feet.z, 12.0f));
							_particles.emitTrail(
								feet,
								velocity,
								particleColorFor(ground != 0u ? ground : 2u),
								horizontalSpeed > 5.0f ? 6 : 3,
								1.1f + horizontalSpeed * 0.08f
							);
							_footstepTimer = (std::max)(0.22f, 0.42f - horizontalSpeed * 0.035f);
						}
					}
					else {
						_footstepTimer = 0.0f;
					}
				}
				else {
					_footstepTimer = 0.0f;
				}
			}
			else {
				_footstepTimer = 0.0f;
				_wasInWater = _player.inWater();
			}

			if (_player.justLanded() && !_player.inWater() && !_player.isSwimming()) {
				addCameraShake(0.14f);
				const dx::XMFLOAT3 feet{
					_player.getCamera()._gpuData._position.x,
					_player.getCamera()._gpuData._position.y - _player.collisionBox().feetBelowEye,
					_player.getCamera()._gpuData._position.z
				};
				_particles.emitBurst(feet, { 0.58f, 0.52f, 0.42f, 0.9f }, 14, 2.8f, 0.45f, 0.12f);
			}

			updateDynamicEntities();

			if (_worldSessionOpen) {
				if (_screen == ac::gameScreen::playing && !freezeSimulation) {
					_autosaveTimer += frame.deltaTime;
					if (_autosaveTimer >= 30.0f)
						saveCurrentWorld();
				}
				_streamer.update(_player.getCamera()._gpuData._position, _device, _context);
				updateReflectionVolume();
				updateBlockLights();
				if (!_lightData.empty())
					updateLightOcclusionVolumes();
			}

			{
				const dx::XMFLOAT3 eye = _player.getCamera()._gpuData._position;
				const dx::XMFLOAT3 look = _player.getLookDirection();
				_audio.setListener(eye.x, eye.y, eye.z, look.x, look.y, look.z);
			}

			return true;
		}

		void onRender(const ac::frameContext& frame) override {
			// Visual time is frame-rate independent. Clamp very long frames so a
			// breakpoint or window drag cannot make the surface visibly jump.
			_waterAnimationTime = std::fmod(
				_waterAnimationTime + (std::min)(frame.deltaTime, .1f),
				4096.0f);
			_lightingTimer.begin(_context);
			_shadowMap.disable(_context);
			if (!_spawnPending && _shadowsEnabled) {
				_celestialShadow.render(
					_context,
					_renderer,
					_streamer.chunks(),
					_player.getCamera()._gpuData._position,
					_celestialDirection,
					_celestialIntensity,
					_streamer.shadowRevision(),
					static_cast<float>(_streamer.streamRadius() * CHUNK_WIDTH));
				_shadowCandidateScratch.clear();
				const UINT maxPointShadows = _lightingQuality == 0u ? 2u :
					(_lightingQuality == 1u ? 4u : 6u);
				const dx::XMFLOAT3& playerEye = _player.getCamera()._gpuData._position;
				const auto addCandidate = [&](const ac::gpuLight& source, uint32_t sourceIndex, bool worldCasters) {
					const float x = playerEye.x - source.position.x;
					const float y = playerEye.y - source.position.y;
					const float z = playerEye.z - source.position.z;
					const float distanceSquared = x * x + y * y + z * z;
					const float reach = source.radius + 1.1f;
					if (source.radius <= 0.0f || source.intensity <= 0.0f || distanceSquared > reach * reach)
						return;
					worldCasters = worldCasters && distanceSquared < 20.0f * 20.0f;
					const float score = source.type == ac::GPU_LIGHT_DIRECTIONAL_PROXY
						? -1.0f
						: distanceSquared / ((std::max)(
							source.intensity * source.radius * source.radius, .001f));
					const shadowCandidate candidate{ &source, sourceIndex, worldCasters, score };
					if (_shadowCandidateScratch.size() < maxPointShadows) {
						_shadowCandidateScratch.push_back(candidate);
						return;
					}
					auto weakest = std::max_element(
						_shadowCandidateScratch.begin(), _shadowCandidateScratch.end(),
						[](const shadowCandidate& left, const shadowCandidate& right) {
							return left.score < right.score;
						});
					if (score < weakest->score)
						*weakest = candidate;
				};

				for (uint32_t index = 0; index < _lightData.size(); ++index) {
					if (_lightData[index].type == ac::GPU_LIGHT_POINT ||
						_lightData[index].type == ac::GPU_LIGHT_DIRECTIONAL_PROXY)
						addCandidate(
							_lightData[index], index,
							_lightData[index].type != ac::GPU_LIGHT_DIRECTIONAL_PROXY);
				}
				const std::vector<ac::gpuLight>& blockSources = !_propagatedBlockSources.empty()
					? _propagatedBlockSources
					: _pendingBlockSources;
				for (const ac::gpuLight& source : blockSources)
					addCandidate(source, pointShadowMap::entityOnlyLight, false);

				std::sort(_shadowCandidateScratch.begin(), _shadowCandidateScratch.end(), [](const shadowCandidate& left, const shadowCandidate& right) {
					return left.score < right.score;
				});
				for (const shadowCandidate& candidate : _shadowCandidateScratch) {
					if (!_shadowMap.render(
						_context,
						_renderer,
						_dynamicRenderer,
						_streamer.chunks(),
						*candidate.light,
						candidate.sourceLightIndex,
						candidate.includeWorldCasters))
						break;
				}
			}
			else {
				_celestialShadow.disable(_context);
			}

			const bool gpuFaces = _streamer.gpuMeshingEnabled();
			if (gpuFaces) _pipeline.bindFaces(_context);
			else _pipeline.bind(_context);
			_shadowMap.bind(_context);
			_celestialShadow.bind(_context);

			_blockTextures.bind(_context);

			const dx::XMFLOAT3 playerEye = _player.getCamera()._gpuData._position;
			const float crouchBlend = std::clamp(_player.crouchBlend(), 0.0f, 1.0f);
			const ac::playerCollisionBox physicalBox = _player.collisionBox();
			const auto& crouchPose = ac::playerPose().crouch;
			const auto& cameraPose = ac::playerPose().camera;
			const float hipPivotY = cameraPose.hipPivotY;
			const float standingEyeHeight = ac::standingPlayerBox().feetBelowEye;
			const float crouchAngle = crouchPose.torsoPitch * crouchBlend;
			// Follow the same hip-centred transform as the rendered torso/head. The
			// collision eye sits lower than the model's eyes in a crouch, which was
			// making the first-person camera look out through the chest.
			const float visualFeetBelowEye = hipPivotY +
				(standingEyeHeight - hipPivotY) * std::cos(crouchAngle) +
				crouchPose.upperBodyY * crouchBlend;
			const float swimKeep = 1.0f - std::clamp(_player.swimBlend(), 0.0f, 1.0f);
			_crouchCameraOffset = (visualFeetBelowEye - physicalBox.feetBelowEye) * swimKeep;
				const float crouchEyeOffset = _crouchCameraOffset;
			const dx::XMFLOAT3 look = _player.getLookDirection();
			const float sneakForward = crouchBlend * cameraPose.crouchForwardOffset *
				(!_spectatorMode && _cameraMode == 0u ? 1.0f : 0.0f);
			const float dt = (std::max)(frame.deltaTime, .0001f);
			const dx::XMFLOAT3 velocity = _player.getVelocity();
			const float horizontalSpeed = std::sqrt(velocity.x * velocity.x + velocity.z * velocity.z);
			const float groundedMotion = (!_spectatorMode && _player.isGrounded())
				? (std::min)(horizontalSpeed / cameraPose.motionReferenceSpeed, 1.0f)
				: 0.0f;
			const auto& walkPose = ac::playerPose().walk;
			const float cameraStride = _player.isCrouching()
				? walkPose.strideCrouch
				: (_player.isSprinting() ? walkPose.strideSprint : walkPose.strideWalk);
			// Keep first-person bob on the same distance-driven cycle as the
			// third-person limbs instead of running a faster independent oscillator.
			_walkBobPhase += dt * horizontalSpeed * cameraStride * dx::XM_2PI;
			const float walkBobY = (!_spectatorMode && _cameraMode == 0u)
				? std::sin(_walkBobPhase) * cameraPose.walkBobY * groundedMotion
				: 0.0f;
			const float walkBobRoll = (!_spectatorMode && _cameraMode == 0u)
				? std::cos(_walkBobPhase * 0.5f) * cameraPose.walkBobRoll * groundedMotion
				: 0.0f;
			const float targetSprintFov = (!_spectatorMode && (
				(_player.isSprinting() && (groundedMotion > 0.2f || _player.isFlying())) ||
				_player.isSwimming()))
				? baseFovRadians() * cameraPose.sprintFovScale
				: baseFovRadians();
			_sprintFov += (targetSprintFov - _sprintFov) *
				(1.0f - std::exp(-cameraPose.fovResponse * dt));
			_player.getCamera()._fov = _sprintFov;
			_player.getCamera().updateProjection();
			if (_cameraShake > 0.0f)
				_cameraShake = (std::max)(0.0f, _cameraShake - dt * cameraPose.shakeDecay);
			const float shake = _cameraShake;
			const float shakeYaw = shake * std::sin(_walkBobPhase * 17.0f) * 0.035f;
			const float shakePitch = shake * std::cos(_walkBobPhase * 13.0f) * 0.028f;
			const dx::XMFLOAT3 visualPlayerEye = {
				playerEye.x + look.x * sneakForward + shake * std::sin(_walkBobPhase * 11.0f) * 0.03f,
				playerEye.y + crouchEyeOffset + walkBobY + shake * std::cos(_walkBobPhase * 9.0f) * 0.04f,
				playerEye.z + look.z * sneakForward + shake * std::cos(_walkBobPhase * 7.0f) * 0.03f
			};
			_cameraData._position = visualPlayerEye;

			_cameraData._view =
				_player.getCamera()._gpuData._view;
			if (!_spectatorMode && _cameraMode == 0u) {
				const float yaw = _player.getYaw() + shakeYaw;
				if (!_bodycamInitialized) {
					_previousBodycamYaw = yaw;
					_bodycamInitialized = true;
				}
				float yawDelta = yaw - _previousBodycamYaw;
				while (yawDelta > dx::XM_PI) yawDelta -= dx::XM_2PI;
				while (yawDelta < -dx::XM_PI) yawDelta += dx::XM_2PI;
				_previousBodycamYaw = yaw;

				const dx::XMVECTOR worldUp = dx::XMVectorSet(0, 1, 0, 0);
				// Keep the bodycam basis horizontal. Building the right axis from an
				// almost-vertical look vector becomes unstable near the pitch clamp.
				const dx::XMFLOAT3 bodyForward = { std::cos(yaw), 0.0f, std::sin(yaw) };
				const dx::XMVECTOR bodyForwardVector = dx::XMLoadFloat3(&bodyForward);
				const dx::XMVECTOR rightVector = dx::XMVector3Normalize(dx::XMVector3Cross(worldUp, bodyForwardVector));
				dx::XMFLOAT3 right;
				dx::XMStoreFloat3(&right, rightVector);
				const float speed = horizontalSpeed;
				const float targetMotion = (std::min)(speed / cameraPose.motionReferenceSpeed, 1.0f);
				const float response = 1.0f - std::exp(-cameraPose.motionResponse * dt);
				const float tiltResponse = 1.0f - std::exp(-cameraPose.tiltResponse * dt);
				_bodycamMotion += (targetMotion - _bodycamMotion) * response;
				_bodycamClock += dt * (cameraPose.idleClockRate +
					_bodycamMotion * cameraPose.movementClockRate);

				const float lateralSpeed = velocity.x * right.x + velocity.z * right.z;
				const float forwardSpeed = velocity.x * bodyForward.x + velocity.z * bodyForward.z;
				const float turnRate = std::clamp(yawDelta / dt, -5.0f, 5.0f);
				const float walkingRoll = std::sin(_bodycamClock) * cameraPose.walkingRoll * _bodycamMotion + walkBobRoll;
				const float targetRoll = std::clamp(-turnRate * cameraPose.turnRoll -
					lateralSpeed * cameraPose.lateralRoll + walkingRoll,
					-cameraPose.maximumRoll, cameraPose.maximumRoll);
				_bodycamRoll += (targetRoll - _bodycamRoll) * tiltResponse;
				float targetPitchLean = std::clamp(
					-forwardSpeed * cameraPose.forwardPitch + shakePitch,
					-cameraPose.maximumPitchLean,
					cameraPose.maximumPitchLean
				);
				// The input pitch is clamped in player.h, but bodycam lean is applied
				// afterward. Clamp their sum as well so jumping or moving cannot rotate
				// the final view through vertical and flip its up/right basis.
				// The user-facing vertical camera angle is stored as pitch. Clamp the
				// final composed angle (input plus bodycam lean) to +/-90 degrees in
				// radians, with a tiny inset to keep LookTo's basis non-singular.
				constexpr float maximumRenderedPitch = dx::XM_PIDIV2 - .001f;
				const float playerPitch = _player.getPitch();
				targetPitchLean = std::clamp(
					targetPitchLean,
					-maximumRenderedPitch - playerPitch,
					 maximumRenderedPitch - playerPitch
				);
				_bodycamPitchLean += (targetPitchLean - _bodycamPitchLean) * tiltResponse;
				_bodycamPitchLean = std::clamp(
					_bodycamPitchLean,
					-maximumRenderedPitch - playerPitch,
					 maximumRenderedPitch - playerPitch
				);
				if (_player.justLanded()) {
					_bodycamPitchLean *= cameraPose.landingPitchRetention;
					_bodycamRoll *= cameraPose.landingRollRetention;
				}

				// Keep the lens and its near plane inside the player's 0.30-block
				// collision hull. The old 0.29 offset plus a 0.005 probe could cross a
				// wall face through floating-point/near-plane spread when looking down.
				const dx::XMFLOAT3 faceForward = { std::cos(yaw), 0.0f, std::sin(yaw) };
				const float swim = std::clamp(_player.swimBlend(), 0.0f, 1.0f);
				const float swimSmooth = swim * swim * (3.0f - 2.0f * swim);
				const float standKeep = 1.0f - swimSmooth;
				const dx::XMFLOAT3 desiredEye = {
					visualPlayerEye.x + faceForward.x * cameraPose.faceOffset * standKeep + look.x * cameraPose.swimOffset * swimSmooth,
					visualPlayerEye.y + look.y * cameraPose.swimOffset * swimSmooth,
					visualPlayerEye.z + faceForward.z * cameraPose.faceOffset * standKeep + look.z * cameraPose.swimOffset * swimSmooth
				};
				const dx::XMFLOAT3 eye = unobstructedCameraPosition(
					visualPlayerEye, desiredEye, cameraPose.radius);
				// Reconstruct from the final scalar angle instead of rotating a vector
				// that is already almost vertical. This guarantees that motion lean can
				// never cross the pole and invert the rendered camera.
				const float finalPitch = std::clamp(
					playerPitch + _bodycamPitchLean,
					-maximumRenderedPitch,
					 maximumRenderedPitch);
				const float finalCosPitch = std::cos(finalPitch);
				const dx::XMVECTOR leanedLook = dx::XMVector3Normalize(dx::XMVectorSet(
					finalCosPitch * std::cos(yaw),
					std::sin(finalPitch),
					finalCosPitch * std::sin(yaw),
					0.0f));
				// Cross with the stable horizontal right axis to get a valid up vector
				// even at the vertical limit. Fade cosmetic roll near that limit because
				// roll becomes a disorienting yaw/spin when the view points straight down.
				const dx::XMVECTOR cameraUp = dx::XMVector3Normalize(
					dx::XMVector3Cross(leanedLook, rightVector));
				const float verticalRollScale = std::clamp(std::abs(finalCosPitch) / .20f, 0.0f, 1.0f);
				const dx::XMVECTOR tiltedUp = dx::XMVector3Rotate(
					cameraUp,
					dx::XMQuaternionRotationAxis(leanedLook, _bodycamRoll * verticalRollScale)
				);
				_cameraData._position = eye;
				dx::XMStoreFloat4x4(
					&_cameraData._view,
					dx::XMMatrixLookToLH(dx::XMLoadFloat3(&eye), leanedLook, tiltedUp)
				);
			}
			else if (!_spectatorMode) {
				const bool frontView = _cameraMode == 2u;
				const dx::XMVECTOR up = dx::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
				const dx::XMVECTOR rightVector = dx::XMVector3Normalize(
					dx::XMVector3Cross(up, dx::XMLoadFloat3(&look))
				);
				dx::XMFLOAT3 right;
				dx::XMStoreFloat3(&right, rightVector);
				const dx::XMFLOAT3 desiredEye = {
					visualPlayerEye.x + look.x * (frontView ? cameraPose.thirdPersonDistance : -cameraPose.thirdPersonDistance) +
						right.x * (frontView ? 0.0f : cameraPose.thirdPersonShoulder),
					visualPlayerEye.y + look.y * (frontView ? cameraPose.thirdPersonDistance : -cameraPose.thirdPersonDistance) +
						cameraPose.thirdPersonHeight,
					visualPlayerEye.z + look.z * (frontView ? cameraPose.thirdPersonDistance : -cameraPose.thirdPersonDistance) +
						right.z * (frontView ? 0.0f : cameraPose.thirdPersonShoulder)
				};
				const dx::XMFLOAT3 eye = unobstructedCameraPosition(visualPlayerEye, desiredEye);
				const dx::XMFLOAT3 focus = {
					visualPlayerEye.x + look.x * (frontView ? 0.0f : cameraPose.thirdPersonFocusDistance),
					visualPlayerEye.y + look.y * (frontView ? 0.0f : cameraPose.thirdPersonFocusDistance) -
						(frontView ? cameraPose.frontFocusDrop : 0.0f),
					visualPlayerEye.z + look.z * (frontView ? 0.0f : cameraPose.thirdPersonFocusDistance)
				};
				_cameraData._position = eye;
				dx::XMStoreFloat4x4(
					&_cameraData._view,
					dx::XMMatrixLookAtLH(
						dx::XMLoadFloat3(&eye),
						dx::XMLoadFloat3(&focus),
						up
					)
				);
			}

			_cameraData._projection =
				_player.getCamera()._gpuData._projection;
			_renderView = _cameraData._view;
			_renderProjection = _cameraData._projection;
			if (!_spectatorMode && _cameraMode != 0u) {
				// An offset third-person camera is only a viewer. Interaction belongs
				// to the player, so cast from the physical eye along the player's aim.
				_renderAimOrigin = playerEye;
				_renderAimDirection = look;
			}
			else {
				// First-person/spectator interaction follows the final rendered view,
				// including bodycam lean, roll, and the offset lens position so the
				// selection ray matches the on-screen crosshair.
				const dx::XMMATRIX inverseRenderView = dx::XMMatrixInverse(
					nullptr,
					dx::XMLoadFloat4x4(&_renderView)
				);
				dx::XMStoreFloat3(
					&_renderAimDirection,
					dx::XMVector3Normalize(inverseRenderView.r[2])
				);
				dx::XMStoreFloat3(&_renderAimOrigin, inverseRenderView.r[3]);
			}
			_renderAimValid = true;

			dx::BoundingFrustum viewFrustum;
			dx::BoundingFrustum worldFrustum;
			// The render projection is infinite-far. CreateFromMatrix treats that
			// far plane as Inf/NaN and can reject the nearest chunks.
			const float cullFar = static_cast<float>(VIEW_DISTANCES[_viewDistanceIndex]) * 16.0f * 1.75f;
			dx::BoundingFrustum::CreateFromMatrix(
				viewFrustum,
				dx::XMMatrixPerspectiveFovLH(
					_player.getCamera()._fov,
					_player.getCamera()._aspectRatio,
					_player.getCamera()._nearZ,
					cullFar)
			);
			viewFrustum.Transform(
				worldFrustum,
				dx::XMMatrixInverse(nullptr, dx::XMLoadFloat4x4(&_cameraData._view))
			);

			const dx::XMMATRIX viewMatrix = dx::XMLoadFloat4x4(&_cameraData._view);
			const dx::XMMATRIX projectionMatrix = dx::XMLoadFloat4x4(&_cameraData._projection);
			dx::XMStoreFloat4x4(&_cameraData._view, dx::XMMatrixTranspose(viewMatrix));
			dx::XMStoreFloat4x4(&_cameraData._projection, dx::XMMatrixTranspose(projectionMatrix));
			// HLSL mul(v, M) plus the per-matrix transpose means the combined
			// matrix must be Transpose(view * projection), not T(view)*T(projection).
			dx::XMStoreFloat4x4(
				&_cameraData._viewProjection,
				dx::XMMatrixTranspose(viewMatrix * projectionMatrix));

			const int cameraBlockX = static_cast<int>(std::floor(_cameraData._position.x));
			const int cameraBlockY = static_cast<int>(std::floor(_cameraData._position.y));
			const int cameraBlockZ = static_cast<int>(std::floor(_cameraData._position.z));
			const ac::blockId cameraBlock = _streamer.blockAt(
				cameraBlockX, cameraBlockY, cameraBlockZ);
			_cameraUnderwater =
				ac::blockType(cameraBlock) == ac::WATER_BLOCK_TYPE &&
				_cameraData._position.y - static_cast<float>(cameraBlockY) <
					ac::fluidHeight(cameraBlock);
			_cameraData._waterTime = _cameraUnderwater
				? -(std::max)(_waterAnimationTime, .0001f)
				: _waterAnimationTime;
			const dx::XMFLOAT3 waterMotion = _player.getVelocity();
			_cameraData._waterMotion = {
				waterMotion.x, waterMotion.y, waterMotion.z,
				static_cast<float>(_waterMaterial)
			};

			_cameraBuffer.update(_context, _cameraData);
			_cameraBuffer.bindVS(_context, 1);
			_cameraBuffer.bindPS(_context, 1);

			_lights.bindPS(_context, ac::SRV_LIGHTS);
			_lightBuffer.bindPS(_context, 2);
			_lightOccluders.bindPS(_context, ac::SRV_LIGHT_OCCLUDERS);
			_lightOcclusionBuffer.bindPS(_context, 3);
			_voxelLights.bindPS(_context, ac::SRV_VOXEL_LIGHTS);
			_voxelLightBuffer.bindPS(_context, 4);
			// The terrain vertex shaders derive biome climate from the seed.
			_voxelLightBuffer.bindVS(_context, 4);
			_chunkLightBuffer.bindPS(_context, 5);
			_daylightBuffer.bindPS(_context, 7);
			_reflectionVoxels.bindPS(_context, ac::SRV_REFLECTION_VOXELS);
			_reflectionVolumeBuffer.bindPS(_context, 9);

			const UINT sceneWidth = (std::max)(1u,
				static_cast<UINT>(_window.getWidth()) * static_cast<UINT>(RENDER_SCALES[_renderScaleIndex]) / 100u);
			const UINT sceneHeight = (std::max)(1u,
				static_cast<UINT>(_window.getHeight()) * static_cast<UINT>(RENDER_SCALES[_renderScaleIndex]) / 100u);
			_scenePost.ensure(_device, sceneWidth, sceneHeight);
			_scenePost.beginScene(_context);

			renderPlanarWaterReflection(gpuFaces);

			// Paint the infinitely distant sky after the camera constants are ready,
			// then restore the terrain pipeline for opaque chunk rendering.
			_skyRenderer.render(_context);
			_shadowMap.bind(_context);
			_celestialShadow.bind(_context);
			_blockTextures.bind(_context);

			_context->IASetPrimitiveTopology(
				D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST
			);

			_translucentScratch.clear();
			_translucentScratch.reserve(_streamer.chunks().size() * 2u);
			_visibleTerrainScratch.clear();
			_visibleTerrainScratch.reserve(_streamer.chunks().size());

			for (auto& pair : _streamer.chunks()) {
				ac::worldChunkGPU& worldChunk = pair.second;
				if (!worldChunk._chunk)
					continue;
				auto& position = worldChunk._chunk->_position;
				const dx::BoundingBox chunkBounds = ac::worldStreamer::chunkSolidBounds(worldChunk);
				if (worldFrustum.Contains(chunkBounds) == dx::DISJOINT)
					continue;

				_visibleTerrainScratch.push_back({ &worldChunk, worldChunk._worldTransform, position, false });

				if (worldChunk._hasGpuMesh && worldChunk._gpuFaces.count(true) != 0) {
					const float centerX = chunkBounds.Center.x;
					const float centerY = chunkBounds.Center.y;
					const float centerZ = chunkBounds.Center.z;
					const float dx = centerX - _cameraData._position.x;
					const float dy = centerY - _cameraData._position.y;
					const float dz = centerZ - _cameraData._position.z;
					_translucentScratch.push_back({
						nullptr,
						&worldChunk._gpuFaces,
						worldChunk._worldTransform,
						position,
						dx * dx + dy * dy + dz * dz,
						false
					});
				}
				{
					const float centerX = chunkBounds.Center.x;
					const float centerZ = chunkBounds.Center.z;
					for (uint32_t sectionIndex = 0; sectionIndex < CHUNK_SUBCHUNKS; ++sectionIndex) {
						ac::gpuModel& section = worldChunk._translucentSections[sectionIndex];
						if (section._index.count() == 0) continue;
						if (worldFrustum.Contains(
								ac::worldStreamer::chunkSectionBounds(worldChunk, sectionIndex)) == dx::DISJOINT)
							continue;
						const float centerY = sectionIndex * SUBCHUNK_HEIGHT + SUBCHUNK_HEIGHT * 0.5f;
						const float dx = centerX - _cameraData._position.x;
						const float dy = centerY - _cameraData._position.y;
						const float dz = centerZ - _cameraData._position.z;
						_translucentScratch.push_back({
							&section, nullptr, worldChunk._worldTransform, position,
							dx * dx + dy * dy + dz * dz,
							false
						});
					}
				}
			}

			if (gpuFaces) _pipeline.bindFaces(_context);
			else _pipeline.bind(_context);
			_shadowMap.bind(_context);
			_celestialShadow.bind(_context);
			_blockTextures.bind(_context);
			for (const visibleTerrainChunk& draw : _visibleTerrainScratch) {
				ac::worldChunkGPU& worldChunk = *draw.chunk;
				bindChunkLights(draw.position);
				_gpuObjectBuffer.update(_context, { draw.transform });
				_gpuObjectBuffer.bindVS(_context, 0);
				if (worldChunk._hasGpuMesh && worldChunk._gpuFaces.count(false) != 0) {
					worldChunk._gpuFaces.draw(_context, false);
					bool drewDetail = false;
					for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
						ac::gpuModel& mesh = worldChunk._opaqueSections[section];
						if (mesh._index.count() == 0) continue;
						if (worldFrustum.Contains(
								ac::worldStreamer::chunkSectionBounds(worldChunk, section)) == dx::DISJOINT)
							continue;
						if (!drewDetail) {
							_pipeline.bindCutoutDetail(_context);
							drewDetail = true;
						}
						mesh.bind(_context);
						mesh.draw(_context);
					}
					if (drewDetail)
						_pipeline.bindFaces(_context);
				}
				else if (!worldChunk._hasGpuMesh) {
					for (uint32_t section = 0; section < CHUNK_SUBCHUNKS; ++section) {
						ac::gpuModel& mesh = worldChunk._opaqueSections[section];
						if (mesh._index.count() == 0) continue;
						if (worldFrustum.Contains(
								ac::worldStreamer::chunkSectionBounds(worldChunk, section)) == dx::DISJOINT)
							continue;
						mesh.bind(_context);
						mesh.draw(_context);
					}
				}
			}

			if (!_streamer.fallingEntities().empty()) {
				const float maxDistance = static_cast<float>(
					_streamer.streamRadius() * CHUNK_WIDTH + CHUNK_WIDTH);
				const float maxDistanceSq = maxDistance * maxDistance;
				_visibleFallingScratch.clear();
				_visibleFallingScratch.reserve(_streamer.fallingEntities().size());
				for (const ac::fallingBlockEntity& entity : _streamer.fallingEntities()) {
					const float worldX = entity.px != 0.0f || entity.pz != 0.0f
						? entity.px : static_cast<float>(entity.x) + 0.5f;
					const float worldZ = entity.px != 0.0f || entity.pz != 0.0f
						? entity.pz : static_cast<float>(entity.z) + 0.5f;
					const float dx = worldX - _cameraData._position.x;
					const float dy = entity.y + 0.5f - _cameraData._position.y;
					const float dz = worldZ - _cameraData._position.z;
					if (dx * dx + dy * dy + dz * dz > maxDistanceSq)
						continue;
					const dx::BoundingBox bounds(
						{ worldX, entity.y + 0.5f, worldZ },
						{ 0.5f, 0.5f, 0.5f });
					if (worldFrustum.Contains(bounds) == dx::DISJOINT)
						continue;
					_visibleFallingScratch.push_back(entity);
				}
				if (!_visibleFallingScratch.empty()) {
					_pipeline.bind(_context);
					_fallingBlocks.draw(
						_context,
						_gpuObjectBuffer,
						_blocks,
						_visibleFallingScratch);
				}
			}

			if (!_items.entities().empty()) {
				const float maxDistance = static_cast<float>(
					_streamer.streamRadius() * CHUNK_WIDTH + CHUNK_WIDTH);
				const float maxDistanceSq = maxDistance * maxDistance;
				_visibleItemScratch.clear();
				_visibleItemScratch.reserve(_items.entities().size());
				for (const ac::itemEntity& entity : _items.entities()) {
					const float dx = entity.x - _cameraData._position.x;
					const float dy = entity.y - _cameraData._position.y;
					const float dz = entity.z - _cameraData._position.z;
					if (dx * dx + dy * dy + dz * dz > maxDistanceSq)
						continue;
					const dx::BoundingBox bounds(
						{ entity.x, entity.y + ac::ITEM_SIZE * 0.5f, entity.z },
						{ ac::ITEM_SIZE, ac::ITEM_SIZE, ac::ITEM_SIZE });
					if (worldFrustum.Contains(bounds) == dx::DISJOINT)
						continue;
					_visibleItemScratch.push_back(entity);
				}
				if (!_visibleItemScratch.empty()) {
					_pipeline.bind(_context);
					_fallingBlocks.drawItems(
						_context,
						_gpuObjectBuffer,
						_blocks,
						_visibleItemScratch,
						_dayTimeSeconds,
						ac::ITEM_SIZE);
				}
			}

			_renderer.render();
			_dynamicRenderer.renderHumanoids(
				_player.getCamera()._gpuData._position,
				_renderView,
				frame.deltaTime
			);
			if (_worldSessionOpen && _livingEntities) {
				_cameraBuffer.bindVS(_context, 1);
				_daylightBuffer.bindPS(_context, 7);
				_livingEntities->render(_context, _player.getCamera()._gpuData._position);
			}
			// Dynamic assets use their own shader and samplers. Restore the world
			// pipeline before drawing water and other translucent blocks.
			if (gpuFaces) _pipeline.bindFaces(_context);
			else _pipeline.bind(_context);
			_shadowMap.bind(_context);
			_celestialShadow.bind(_context);
			_blockTextures.bind(_context);
			if (const ac::dynamicEntity* entity = _dynamicRenderer.get(_playerEntity)) {
				if (entity->visible && entity->heldItemReady && entity->heldStyle != ac::heldItemStyle::none) {
					const ac::blockDefinition* held = selectedItemDef();
					if (held && _hotbarCounts[_selectedHotbarSlot] > 0) {
						_pipeline.bind(_context);
						_blockTextures.bind(_context);
						_fallingBlocks.drawHeld(
							_context,
							_gpuObjectBuffer,
							held,
							entity->heldStyle,
							entity->heldItemWorld,
							false);
						if (gpuFaces) _pipeline.bindFaces(_context);
						else _pipeline.bind(_context);
						_blockTextures.bind(_context);
					}
				}
			}
			_sceneRefraction.capture(
				_device,
				_context,
				_scenePost.hdrTarget.Get(),
				_scenePost.depthTarget.Get());
			_sceneRefraction.bind(_context);

			std::sort(_translucentScratch.begin(), _translucentScratch.end(), [](const translucentChunk& a, const translucentChunk& b) {
				return a._distanceSquared > b._distanceSquared;
				});

			enum class translucentDrawMode { none, faces, models };
			translucentDrawMode mode = translucentDrawMode::none;
			for (const translucentChunk& draw : _translucentScratch) {
				bindChunkLights(draw._chunkPosition);
				if (draw._gpuFaces) {
					if (mode != translucentDrawMode::faces) {
						_pipeline.bindTranslucentFaces(_context, _cameraUnderwater, false);
						mode = translucentDrawMode::faces;
					}
					_gpuObjectBuffer.update(_context, { draw._transform });
					_gpuObjectBuffer.bindVS(_context, 0);
					draw._gpuFaces->draw(_context, true);
				}
				else if (draw._model) {
					if (mode != translucentDrawMode::models) {
						_pipeline.bindTranslucentModels(_context, _cameraUnderwater);
						mode = translucentDrawMode::models;
					}
					_gpuObjectBuffer.update(_context, { draw._transform });
					_gpuObjectBuffer.bindVS(_context, 0);
					draw._model->bind(_context);
					draw._model->draw(_context);
				}
			}
			_renderer.render();
			_lightingTimer.end(_context);
			ID3D11ShaderResourceView* nullFace = nullptr;
			_context->VSSetShaderResources(66, 1, &nullFace);
			_renderedTargetValid = false;
			if (_screen == ac::gameScreen::playing && _cursorCaptured) {
				if (const std::optional<blockRayHit> target = raycastBlock()) {
					_renderedTargetBlock = target->block;
					_renderedTargetAdjacent = target->adjacent;
					_renderedTargetId = target->id;
					_renderedTargetDistance = target->distance;
					_renderedTargetValid = true;
					ac::blockAABB boxes[8];
					dx::XMINT3 outlineBlock = target->block;
					ac::blockId outlineId = target->id;
					// Doors are two blocks tall — outline both halves from the bottom.
					if (isDoorType(ac::blockType(target->id))) {
						if (ac::isUpperHalf(target->id) || isDoorTopType(target->id)) {
							outlineBlock.y -= 1;
							outlineId = _streamer.blockAt(
								outlineBlock.x, outlineBlock.y, outlineBlock.z);
							if (!isDoorType(ac::blockType(outlineId)))
								outlineId = target->id;
						}
					}
					const size_t count = selectionBoxesAt(
						outlineBlock.x, outlineBlock.y, outlineBlock.z,
						outlineId, boxes, 8);
					if (count == 0) {
						_outlineRenderer.render(
							_context, outlineBlock, _renderView, _renderProjection);
					}
					else {
						const bool tallDoor = isDoorType(ac::blockType(outlineId));
						for (size_t i = 0; i < count; ++i) {
							_outlineRenderer.render(
								_context,
								outlineBlock,
								{ boxes[i].minX, boxes[i].minY, boxes[i].minZ },
								{
									boxes[i].maxX,
									tallDoor ? boxes[i].maxY + 1.0f : boxes[i].maxY,
									boxes[i].maxZ
								},
								_renderView,
								_renderProjection);
						}
					}
					if (_breakActive && _breakProgress > 0.0f &&
						outlineBlock.x == _breakTarget.x &&
						outlineBlock.y == _breakTarget.y &&
						outlineBlock.z == _breakTarget.z) {
						if (count == 0) {
							_breakOverlay.render(
								_context, outlineBlock, { 0, 0, 0 }, { 1, 1, 1 },
								_breakProgress, _renderView, _renderProjection);
						}
						else {
							const bool tallDoorBreak = isDoorType(ac::blockType(outlineId));
							for (size_t i = 0; i < count; ++i) {
								_breakOverlay.render(
									_context,
									outlineBlock,
									{ boxes[i].minX, boxes[i].minY, boxes[i].minZ },
									{
										boxes[i].maxX,
										tallDoorBreak ? boxes[i].maxY + 1.0f : boxes[i].maxY,
										boxes[i].maxZ
									},
									_breakProgress,
									_renderView,
									_renderProjection);
							}
						}
					}
				}
			}
			{
				// DirectXMath view matrix stores camera basis in columns:
				// right = (_11,_21,_31), up = (_12,_22,_32).
				const dx::XMFLOAT3 cameraRight{
					_renderView._11, _renderView._21, _renderView._31
				};
				const dx::XMFLOAT3 cameraUp{
					_renderView._12, _renderView._22, _renderView._32
				};
				_particles.draw(
					_context,
					_cameraBuffer.get(),
					cameraRight,
					cameraUp
				);
			}
			{
				const dx::XMFLOAT3 velocity = _player.getVelocity();
				const float speed = std::sqrt(
					velocity.x * velocity.x +
					velocity.y * velocity.y +
					velocity.z * velocity.z);
				const float blurStrength =
					(_motionBlurEnabled && _screen == ac::gameScreen::playing)
						? std::clamp(speed * 0.045f + _cameraShake * 0.55f, 0.0f, 0.85f)
						: 0.0f;
				if (blurStrength > 0.001f) {
					_sceneRefraction.capture(
						_device,
						_context,
						_scenePost.hdrTarget.Get(),
						_scenePost.depthTarget.Get());
					_motionBlur.render(
						_context,
						_sceneRefraction,
						_renderView,
						_renderProjection,
						blurStrength
					);
				}
				_weatherSurfaces.bindPS(_context, 4);
				_scenePost.resolve(
					_context,
					_graphicsSettings.getRenderTarget(),
					static_cast<UINT>((std::max)(_window.getWidth(), 1)),
					static_cast<UINT>((std::max)(_window.getHeight(), 1)),
					_renderView,
					_renderProjection,
					_cameraData._position,
					_fogSkyColor,
					_waterAnimationTime,
					_player.getCamera()._nearZ,
					_postSettings);
				ID3D11ShaderResourceView* nullWeatherSurface = nullptr;
				_context->PSSetShaderResources(4, 1, &nullWeatherSurface);
			}
			if (_window.getWindowCallBack().frameUpdate)
				_nuklear.resize(_context, _window.getWidth(), _window.getHeight());
			_nuklear.endInput();
			if (_screen == ac::gameScreen::playing)
				sampleCrosshairFromFramebuffer(frame.deltaTime);
			// HUD/UI always draws without scene depth so place/break particles can't clip it.
			{
				ID3D11RenderTargetView* rtv = _graphicsSettings.getRenderTarget();
				_context->OMSetRenderTargets(1, &rtv, nullptr);
			}
			drawNuklearGui();
			_nuklear.render(_context);
			if (_screen == ac::gameScreen::playing) {
				ID3D11RenderTargetView* rtv = _graphicsSettings.getRenderTarget();
				_context->OMSetRenderTargets(1, &rtv, nullptr);
				_crosshair.render(
					_context,
					static_cast<float>(_window.getWidth()),
					static_cast<float>(_window.getHeight()),
					_crosshairColor[0],
					_crosshairColor[1],
					_crosshairColor[2]);
			}
			_nuklear.beginInput();
		}

		void onShutdown() override {
			_modHost.stop();
			_streamer.setBlockChangeHooks({}, {});
			stopWeatherAudio();
			// Automated frame captures are read-only diagnostics. They can end while
			// spawn validation or synthetic window input is still active, so persisting
			// that transient state would move the user's player or alter the camera.
			if (diagnosticFrameCaptureEnabled()) {
				_streamer.stop();
				_window.setEventHook(nullptr);
				_nuklear.destroy();
				_audio.destroy();
				return;
			}
			saveCurrentWorld(true);
			persistUserSettings();
			_streamer.stop();
			_window.setEventHook(nullptr);
			_nuklear.destroy();
			_audio.destroy();
		}

	public:
		voxelPipeline(
			ac::window& window,
			ac::graphicsContext& graphics,
			ID3D11Device* device,
			ID3D11DeviceContext* context,
			staticPipeline& pipeline,
			pointShadowMap& shadowMap,
			ac::staticRenderer& renderer,
			ac::textureLibrary& textures,
			ac::blockTextureSet& blockTextures,
			ac::staticAssetManager& blocks,
			ac::modelManager& models,
			ac::contentPackSet& contentPacks,
			ac::world& world,
			ac::worldStreamer& streamer,
			ac::player& player,
			ac::dynamicRenderer& dynamicRenderer,
			ac::constantBuffer<ac::cameraData>& cameraBuffer,
			ac::structuredBuffer<ac::gpuLight>& lights,
			std::vector<ac::gpuLight>& lightData,
			ac::constantBuffer<ac::lightBufferData>& lightBuffer,
			ac::structuredBuffer<uint32_t>& lightOccluders,
			std::vector<uint32_t>& lightOccluderData,
			ac::constantBuffer<ac::lightOcclusionBufferData>& lightOcclusionBuffer
		) :
			applicationPipeline(window, graphics),
			_window(window),
			_graphicsSettings(graphics),
			_device(device),
			_context(context),
			_pipeline(pipeline),
			_shadowMap(shadowMap),
			_renderer(renderer),
			_textures(textures),
			_blockTextures(blockTextures),
			_blocks(blocks),
			_models(models),
			_contentPacks(contentPacks),
			_world(world),
			_streamer(streamer),
			_modBlocks(
				[this](const ac::modding::resourceId& id) -> std::optional<uint32_t> {
					try { return _blocks.getId(id.string()); }
					catch (const std::exception&) { return std::nullopt; }
				},
				[this](uint32_t id) -> std::optional<ac::modding::blockDescription> {
					const ac::blockDefinition* definition = _blocks.get(id);
					if (!definition) return std::nullopt;
					return ac::modding::blockDescription{
						ac::modding::resourceId(_blocks.persistentName(id)),
						id,
						definition->_solid,
						definition->_item,
						definition->_hardness
					};
				},
				[this](uint32_t id) -> std::optional<ac::modding::resourceId> {
					const std::string name = _blocks.persistentName(id);
					return name.empty()
						? std::nullopt
						: std::optional<ac::modding::resourceId>(ac::modding::resourceId(name));
				}),
			_modWorld(
				[this](ac::modding::blockPosition position) -> std::optional<uint32_t> {
					const std::optional<ac::blockId> state =
						_streamer.tryBlockAt(position.x, position.y, position.z);
					return state ? std::optional<uint32_t>(*state) : std::nullopt;
				},
				[this](ac::modding::blockPosition position, uint32_t state,
					const ac::modding::worldWriteOptions& options) {
					return _streamer.setBlock(
						position.x, position.y, position.z,
						static_cast<ac::blockId>(state), _device, _context,
						options.notifyFluids, options.immediateRemesh,
						options.notifyRedstone, options.notifyFallingBlocks);
				}),
			_modEntities(
				[this](const ac::modding::entitySpawnRequest& request)
					-> std::optional<ac::modding::entityId> {
					if (!_livingEntities) return std::nullopt;
					return _livingEntities->spawnNamed(request.type.string(),
						{ request.position.x, request.position.y, request.position.z }, request.yaw);
				},
				[this](ac::modding::entityId id)
					-> std::optional<ac::modding::entityDescription> {
					if (!_livingEntities) return std::nullopt;
					const ac::livingEntity* value = _livingEntities->entityById(id);
					if (!value) return std::nullopt;
					const std::string_view type = _livingEntities->definitionName(value->definition);
					if (type.empty()) return std::nullopt;
					uint32_t flags = 0;
					if (value->grounded) flags |= 1u;
					if (value->sheared) flags |= 2u;
					return ac::modding::entityDescription{
						value->id,
						ac::modding::resourceId(std::string(type)),
						{ value->position.x, value->position.y, value->position.z },
						{ value->horizontalVelocity.x, value->verticalVelocity,
							value->horizontalVelocity.y },
						value->yaw, 0.0f, value->health, flags
					};
				},
				[this]() -> std::vector<ac::modding::entityId> {
					return _livingEntities ? _livingEntities->entityIds()
						: std::vector<ac::modding::entityId>{};
				},
				[this](ac::modding::entityId id, ac::modding::float3 position) {
					return _livingEntities && _livingEntities->teleport(
						id, { position.x, position.y, position.z });
				},
				[this](ac::modding::entityId id, float amount,
					ac::modding::float3 direction, float knockback) {
					return _livingEntities && _livingEntities->damageById(id, amount,
						{ direction.x, direction.y, direction.z }, knockback);
				},
				[this](ac::modding::entityId id) {
					return _livingEntities && _livingEntities->remove(id);
				}),
			_modPresentation([this](const ac::modding::particleBurst& burst) {
				_particles.emitBurst(
					{ burst.position.x, burst.position.y, burst.position.z },
					{ burst.color.red, burst.color.green, burst.color.blue, burst.color.alpha },
					static_cast<int>((std::min)(burst.count, 256u)),
					std::clamp(burst.speed, 0.0f, 64.0f),
					std::clamp(burst.lifetime, 0.01f, 30.0f),
					std::clamp(burst.size, 0.001f, 4.0f));
				return true;
			}),
			_modUi(
				[this]() { return std::string(ac::gameScreenName(_screen)); },
				[this](std::string_view message) { pushConsoleMessage(std::string(message)); }),
			_modStorage([this]() { return _world.path(); }),
			_modRegistries(
				[this](const ac::modding::resourceId& registry,
					const ac::modding::resourceId& entry) -> std::optional<uint32_t> {
					if (registry.string() == "core:blocks") {
						try { return _blocks.getId(entry.string()); }
						catch (...) { return std::nullopt; }
					}
					if (registry.string() == "core:entities" && _livingEntities)
						return _livingEntities->definitionId(entry.string());
					return std::nullopt;
				},
				[this](const ac::modding::resourceId& registry, uint32_t handle)
					-> std::optional<ac::modding::resourceId> {
					if (registry.string() == "core:blocks") {
						const std::string name = _blocks.persistentName(handle);
						return name.empty() ? std::nullopt :
							std::optional<ac::modding::resourceId>(ac::modding::resourceId(name));
					}
					if (registry.string() == "core:entities" && _livingEntities) {
						const std::string_view name = _livingEntities->definitionName(handle);
						return name.empty() ? std::nullopt :
							std::optional<ac::modding::resourceId>(
								ac::modding::resourceId(std::string(name)));
					}
					return std::nullopt;
				},
				[this](const ac::modding::resourceId& registry) -> uint32_t {
					if (registry.string() == "core:blocks")
						return static_cast<uint32_t>(_blocks.ids().size());
					if (registry.string() == "core:entities" && _livingEntities)
						return _livingEntities->definitionCount();
					return 0u;
				}),
			_modNetwork([]() { return false; },
				[](const ac::modding::resourceId&, std::span<const uint8_t>) { return false; }),
			_modHost(_modBlocks, _modWorld, _modLog),
			_player(player),
			_dynamicRenderer(dynamicRenderer),
			_cameraBuffer(cameraBuffer),
			_lights(lights),
			_lightData(lightData),
			_lightBuffer(lightBuffer),
			_lightOccluders(lightOccluders),
			_lightOccluderData(lightOccluderData),
			_lightOcclusionBuffer(lightOcclusionBuffer) {
			_streamer.setBlockChangeHooks(
				[this](int32_t x, int32_t y, int32_t z, ac::blockId previous, ac::blockId& next) {
					ac::modding::blockChangingEvent event{ { x, y, z }, previous, next };
					const ac::modding::eventResult result = _modHost.events().publish(event);
					next = static_cast<ac::blockId>(event.nextState);
					return result != ac::modding::eventResult::cancel;
				},
				[this](int32_t x, int32_t y, int32_t z, ac::blockId previous, ac::blockId current) {
					ac::modding::blockChangedEvent event{ { x, y, z }, previous, current };
					_modHost.events().publish(event);
				});
			_modHost.installPorts({
				&_modEntities, &_modPresentation, &_modUi, &_modStorage,
				&_modRegistries, &_modNetwork
			});
			_modHost.addRuntime(std::make_unique<ac::modding::wasmModRuntime>(
				std::make_unique<ac::modding::wasmtimeBackend>()));
			_blockEntities.configureIdTranslation(
				[this](uint32_t id) { return _world.remapSavedBlockState(id); },
				[this](uint32_t id) { return _world.persistentBlockState(id); });
			{
				std::vector<std::filesystem::path> entityFiles;
				for (const auto& [pack, path] : contentPacks.paths("entities")) {
					(void)pack;
					entityFiles.push_back(path);
				}
				if (!entityFiles.empty()) {
					try {
						_livingEntities = std::make_unique<ac::livingEntitySystem>(
							device, context, entityFiles);
					}
					catch (const std::exception& error) {
						std::cerr << "Entity system initialization failed: " << error.what() << '\n';
					}
				}
			}

			_gpuObjectBuffer.create(device);
			_lightingTimer.create(device);
			_voxelLights.create(device, ac::VOXEL_LIGHT_TABLE_SIZE, _voxelLightTable.data());
			_voxelLightBuffer.create(device);
			if (const ac::blockDefinition* water = _blocks.get(ac::WATER_BLOCK_TYPE))
				_waterMaterial = water->materialForFace(ac::BLOCK_FACE_UP);
			_voxelLightBuffer.update(context, {
				ac::VOXEL_LIGHT_TABLE_SIZE - 1u,
				0u,
				_lightingQuality,
				_waterMaterial,
				static_cast<uint32_t>(_world.seed()),
				{}
			});
			_chunkLightBuffer.create(device);
			_chunkLightBuffer.update(context, {});
			_daylightBuffer.create(device);
			_weatherSurfaces.create(
				device, WEATHER_SURFACE_CELLS, _weatherSurfaceData.data());
			_reflectionVoxels.create(
				device, REFLECTION_VOLUME_VOXELS, _reflectionVoxelData.data());
			_reflectionVolumeBuffer.create(device);
			_reflectionVolumeBuffer.update(context, {});
			_planarReflectionDataBuffer.create(device);
			_planarReflectionDataBuffer.update(context, {});
			_planarClipBuffer.create(device);
			_planarClipBuffer.update(context, {});
			_celestialShadow.create(device);
			_streamer.setRedstoneSupportRules([this](ac::blockId state) {
				const auto* support = _blocks.get(ac::blockType(state));
				return support && support->_solid && support->_occludes;
			}, [this](int32_t x, int32_t y, int32_t z, ac::blockId item) {
				spawnItemDrop(item, 1u, x + 0.5f, y + 0.15f, z + 0.5f, 0.0f, 0.8f, 0.0f);
			});
			_streamer.setRedstoneAnalogQuery([this](int32_t x, int32_t y, int32_t z) -> std::optional<uint8_t> {
				const auto state = _streamer.blockAt(x, y, z);
				const auto type = ac::blockType(state);
				const auto* definition = _blocks.get(type);
				const bool container = usesChestEntity(definition) || type == BLOCK_FURNACE ||
					type == BLOCK_BLAST_FURNACE || type == BLOCK_FURNACE_LIT || type == BLOCK_BLAST_FURNACE_LIT;
				if (!container) return std::nullopt;
				return _blockEntities.analogSignalAt(x, y, z,
					[this](uint32_t item) { return itemStackLimit(item); }).value_or(0u);
			});
			_streamer.setRedstonePoweredDeviceUpdate(
				[this](int32_t x, int32_t y, int32_t z, ac::blockId state, uint8_t power)
					-> std::optional<ac::blockId> {
					const ac::blockId type = ac::blockType(state);
					const ac::blockDefinition* definition = _blocks.get(type);
					const bool singleBlockToggle = definition && (
						definition->_interaction == ac::blockInteraction::trapdoor ||
						definition->_interaction == ac::blockInteraction::fenceGate);
					if (singleBlockToggle) {
						const bool powered = power > 0u;
						if (powered == ac::isBlockPowered(state))
							return std::nullopt;
						return ac::withBlockPowered(
							ac::withBlockOpen(state, powered), powered);
					}
					if (!isDoorType(type))
						return std::nullopt;
					if (ac::isUpperHalf(state) || isDoorTopType(state))
						return std::nullopt;
					const int32_t bottomY = y;
					const int32_t topY = bottomY + 1;
					const uint8_t best = (std::max)(
						power, _streamer.redstoneInputPowerAt(x, topY, z));
					const ac::blockId bottomExisting = _streamer.blockAt(x, bottomY, z);
					if (!isDoorType(bottomExisting) || ac::isUpperHalf(bottomExisting))
						return std::nullopt;
					const bool powered = best > 0u;
					if (powered == ac::isBlockPowered(bottomExisting))
						return std::nullopt;
					const uint32_t facing = ac::blockFacing(bottomExisting);
					const ac::blockId bottomType = doorPlaceBaseId(bottomExisting);
					const ac::blockId topType = doorTopId(bottomType);
					_streamer.setBlock(
						x, bottomY, z,
						ac::withUpperHalf(ac::withBlockPowered(ac::withBlockOpen(ac::withFacing(bottomType, facing), powered), powered), false),
						_device, _context, false, false);
					_streamer.setBlock(
						x, topY, z,
						ac::withUpperHalf(ac::withBlockPowered(ac::withBlockOpen(ac::withFacing(topType, facing), powered), powered), true),
						_device, _context, false, false);
					return std::nullopt;
				});
			_sunLightIndex = _lightData.size();
			_lightData.push_back({});
			updateDaylight(0.0f);
			_skyRenderer.create(device);
			_sceneRefraction.create(device);
			_motionBlur.create(device);
			_fxaa.create(device);
			_scenePost.create(device);
			_outlineRenderer.create(device);
			_breakOverlay.create(device);
			_crosshair.create(device);
			_nuklear.create(device, context, _window.getWidth(), _window.getHeight());
			_nuklear.beginInput();
			syncNuklearMouse();
			_window.setEventHook([this](const SDL_Event& event) {
				if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
					_screen == ac::gameScreen::inventory && isCreative() &&
					_inventoryTab == inventoryTab::chooser) {
					// Resolve focus before onUpdate dispatches inventory shortcuts.
					const auto& r = _chooserSearchRect;
					const float mx = event.button.x * static_cast<float>(_window.getWidth()) /
						static_cast<float>((std::max)(_window.getLogicalWidth(), 1));
					const float my = event.button.y * static_cast<float>(_window.getHeight()) /
						static_cast<float>((std::max)(_window.getLogicalHeight(), 1));
					const bool inside = mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
					if (!inside) _chooserSearchFocus = false;
					else if (event.button.button == SDL_BUTTON_LEFT) _chooserSearchFocus = true;
				}
				if (_screen == ac::gameScreen::console &&
					(event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
					event.key.key == SDLK_TAB) {
					return;
				}
				_nuklear.processEvent(event);
			});
			auto loadGui = [device](ac::texture& dst, const char* path) {
				try {
					dst = ac::loadUiTextureFromFile(device, path);
					return dst._shaderResourceView != nullptr;
				}
				catch (const std::exception& error) {
					std::cerr << "Failed to load Minecraft GUI texture " << path << ": "
						<< error.what() << '\n';
					return false;
				}
			};
			_guiReady = loadGui(_guiWidgets, "assets/textures/gui/widgets.png");
			loadGui(_guiIcons, "assets/textures/gui/icons.png");
			loadGui(_guiInventory, "assets/textures/gui/container/inventory.png");
			loadGui(_guiCrafting, "assets/textures/gui/container/crafting_table.png");
			loadGui(_guiChest, "assets/textures/gui/container/generic_54.png");
			loadGui(_guiFurnace, "assets/textures/gui/container/furnace.png");
			loadGui(_guiCreativeItems,
				"assets/textures/gui/container/creative_inventory/tab_items.png");
			loadGui(_guiCreativeInv,
				"assets/textures/gui/container/creative_inventory/tab_inventory.png");
			// legacy uiRenderer removed
			_particles.create(device);
			_fallingBlocks.create(device, &_models);
			_audio.create();
			try {
				_itemIcons.bake(device, context, blocks, models, blockTextures);
			}
			catch (const std::exception& error) {
				std::cerr << "Item icon atlas bake failed: " << error.what() << '\n';
			}
			rebuildChooserCatalog();
			{
				bool loadedAny = false;
				for (const auto& [pack, recipePath] : contentPacks.paths("recipes")) {
					const bool loaded = _recipes.load(
						recipePath,
					[this](const std::string& name) -> std::optional<ac::blockId> {
						try { return static_cast<ac::blockId>(_blocks.getId(name)); }
						catch (...) { return std::nullopt; }
					},
					[this](const std::function<void(ac::blockId, const std::string&)>& visit) {
						for (uint32_t id : _blocks.ids()) {
							const ac::blockDefinition* definition = _blocks.get(id);
							if (definition)
								visit(static_cast<ac::blockId>(id), definition->_name);
						}
					},
					loadedAny);
					if (!loaded)
						std::cerr << "No valid recipes loaded from " << recipePath << '\n';
					loadedAny = loadedAny || loaded;
				}
			}
			loadAndApplyUserSettings();
			persistUserSettings();
			setClearColor(0.0f, 0.0f, 0.0f, 0.0f);
			_playerEntity = _dynamicRenderer.createHumanoid({}, { 1.0f, 1.0f, 1.0f, 1.0f }, false);
			setScreen(ac::gameScreen::mainMenu);
			_modHost.start(contentPacks);
		}
	};
}

int runVoxelApplication() {
	ac::window window(
		1280,
		720,
		"DX11 Open World"
	);

	ac::graphicsContext gfx(
		window.getHwnd(),
		window.getWidth(),
		window.getHeight(),
#ifdef _DEBUG
		true
#else
		false
#endif
	);

	if (FAILED(gfx.getResult())) {
		return -1;
	}

	auto* device = gfx.getDevice();
	auto* context = gfx.getContext();

	staticPipeline pipeline(device);
	pointShadowMap shadowMap(device);
	ac::staticRenderer renderer(device, context);

	ac::textureLibrary textures;

	ac::contentPackSet contentPacks;
	try {
		contentPacks = ac::contentPackSet::discover("assets/pack.json", "mods");
		for (const ac::contentPack& pack : contentPacks.ordered())
			std::cout << "Loaded content pack " << pack.id << " " << pack.version << '\n';
	}
	catch (const std::exception& error) {
		std::cerr << "Failed to discover content packs: " << error.what() << '\n';
		return -1;
	}

	ac::modelManager models;
	try {
		for (const auto& [pack, path] : contentPacks.paths("models"))
			models.load(path.string());
	}
	catch (const std::exception& error) {
		std::cerr << "Failed to load models: " << error.what() << '\n';
		return -1;
	}

	ac::staticAssetManager blocks;

	try {
		for (const auto& [pack, path] : contentPacks.paths("blocks"))
			blocks.load(path.string(), pack->id);
		blocks.validateReferences(models);
		bindBlockAliases(blocks);
		ac::registerBlockStateVariants(blocks);
	}
	catch (const std::exception& error) {
		std::cerr << "Failed to load block registry: " << error.what() << '\n';
		return -1;
	}

	ac::blockTextureSet blockTextures;
	try {
		blockTextures.load(device, textures, blocks.texturePaths());
		blockTextures.loadEmissions(device, blocks);
		blockTextures.loadMaterialProperties(device, blocks);
		blockTextures.loadColormaps(
			device,
			textures,
			"assets/textures/colormap/grass.png",
			"assets/textures/colormap/foliage.png"
		);
	}
	catch (const std::exception& error) {
		std::cerr << "Failed to load block textures: " << error.what() << '\n';
		return -1;
	}

	ac::world world;
	try {
		world.configureContentRegistry(blocks);
		world.configureTerrain(
			ac::terrainBlockPalette::load(contentPacks.singleton("terrainBlocks"), blocks),
			ac::terrainBiomeConfig::load(contentPacks.singleton("biomes"), blocks));
	}
	catch (const std::exception& error) {
		std::cerr << "Failed to load terrain configuration: " << error.what() << '\n';
		return -1;
	}

	// The streamer currently keeps terrain on its canonical background CPU path
	// so saved and newly generated chunks always use the same world function.
	// Keep the preference plumbing for a future bit-identical GPU implementation.
	char* cpuTerrainValue = nullptr;
	size_t cpuTerrainLength = 0;
	_dupenv_s(&cpuTerrainValue, &cpuTerrainLength, "AC_CPU_TERRAIN");
	const bool enableGpuTerrain = cpuTerrainValue == nullptr;
	std::free(cpuTerrainValue);
	// GPU meshing is the normal renderer path. Keeping it behind an opt-in
	// environment variable made ordinary Release launches silently use the
	// deferred CPU remesh queue, which is visible as placement latency.
	char* cpuMeshingValue = nullptr;
	size_t cpuMeshingLength = 0;
	_dupenv_s(&cpuMeshingValue, &cpuMeshingLength, "AC_CPU_MESHING");
	const bool enableGpuMeshing = cpuMeshingValue == nullptr;
	std::free(cpuMeshingValue);
	ac::worldStreamer streamer(
		&world,
		&blocks,
		&models,
		device,
		enableGpuTerrain,
		enableGpuMeshing,
		8,
		10
	);

	ac::player player(
		{ 8.0f, 100.0f, -12.0f },
		{ 0, 0, 0 },
		1.48352986f,
		window.getAspect(),
		0.01f
	);
	ac::dynamicRenderer dynamicAssets(device, context);
	dynamicAssets.loadHumanoidAsset(device, "assets/textures/entity/player.png");

	ac::constantBuffer<ac::cameraData> cameraBuffer;
	cameraBuffer.create(device);

	ac::structuredBuffer<ac::gpuLight> lights;
	lights.create(device, 1024);

	std::vector<ac::gpuLight> lightData;

	ac::constantBuffer<ac::lightBufferData> lightBuffer;
	lightBuffer.create(device);

	lightBuffer.update(
		context,
		{ 0, { 0, 0, 0 } }
	);

	std::vector<uint32_t> lightOccluderData(ac::LIGHT_OCCLUSION_TOTAL_WORDS, 0u);
	ac::structuredBuffer<uint32_t> lightOccluders;
	lightOccluders.create(
		device,
		ac::LIGHT_OCCLUSION_TOTAL_WORDS,
		lightOccluderData.data()
	);

	ac::constantBuffer<ac::lightOcclusionBufferData> lightOcclusionBuffer;
	lightOcclusionBuffer.create(device);
	lightOcclusionBuffer.update(context, ac::lightOcclusionBufferData{});

	voxelPipeline application(
		window,
		gfx,
		device,
		context,
		pipeline,
		shadowMap,
		renderer,
		textures,
		blockTextures,
		blocks,
		models,
		contentPacks,
		world,
		streamer,
		player,
		dynamicAssets,
		cameraBuffer,
		lights,
		lightData,
		lightBuffer,
		lightOccluders,
		lightOccluderData,
		lightOcclusionBuffer
	);

	const int result = application.run();

	return result;
}
