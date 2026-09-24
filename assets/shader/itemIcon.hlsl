cbuffer ObjectBuffer : register(b0)
{
	matrix transform;
};

cbuffer CameraBuffer : register(b1)
{
	matrix view;
	matrix projection;
	float3 cameraPosition;
	float waterTime;
	float4 waterMotion;
	matrix viewProjection;
};

struct MaterialProperties
{
	float4 tintColor;
	uint tintMode;
	uint overlayMaterial;
	uint overlayTintMode;
	uint frameCount;
	float frameTime;
	uint interpolate;
	float roughness;
	float metallic;
	float4 emissionUvBounds;
	uint emissionWarmMask;
	float3 materialPadding;
};

Texture2DArray blockTextures : register(t0);
Texture2D grassColormap : register(t11);
Texture2D foliageColormap : register(t12);
StructuredBuffer<MaterialProperties> materialProperties : register(t13);
SamplerState linearSampler : register(s0);
SamplerState colormapSampler : register(s4);

static const uint TINT_MODE_NONE = 0u;
static const uint TINT_MODE_GRASS = 1u;
static const uint TINT_MODE_FOLIAGE = 2u;
static const uint TINT_MODE_CONSTANT = 3u;

float3 iconMaterialTint(uint mode, float4 constantColor)
{
	if (mode == TINT_MODE_CONSTANT)
		return constantColor.rgb;
	if (mode != TINT_MODE_GRASS && mode != TINT_MODE_FOLIAGE)
		return float3(1.0, 1.0, 1.0);
	// Fixed plains climate — never the player's local biome or voxel light.
	const float2 climate = float2(0.8, 0.4);
	const float2 uv = float2(1.0 - climate.x, 1.0 - climate.y * climate.x) *
		(255.0 / 256.0) + (0.5 / 256.0);
	return mode == TINT_MODE_FOLIAGE
		? foliageColormap.SampleLevel(colormapSampler, uv, 0).rgb
		: grassColormap.SampleLevel(colormapSampler, uv, 0).rgb;
}

struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float2 uv : TEXCOORD0;
	uint material : MATID;
	uint ao : AO;
	float opacity : OPACITY;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float3 normal : NORMAL;
	float2 uv : TEXCOORD0;
	nointerpolation uint material : MATID;
	float opacity : OPACITY;
};

VSOutput vertexMain(VSInput input)
{
	VSOutput output;
	float4 worldPosition = mul(float4(input.position, 1.0f), transform);
	output.position = mul(mul(worldPosition, view), projection);
	output.normal = mul(input.normal, (float3x3)transform);
	output.uv = input.uv;
	output.material = input.material;
	output.opacity = input.opacity;
	return output;
}

float2 iconFrameUv(float2 uv, uint frameCount)
{
	const float frames = max(float(frameCount), 1.0);
	return float2(uv.x, uv.y / frames);
}

float4 pixelMain(VSOutput input) : SV_TARGET
{
	MaterialProperties props = materialProperties[input.material];
	float2 uv = iconFrameUv(input.uv, props.frameCount);

	uint texW = 16u;
	uint texH = 16u;
	uint texN = 1u;
	blockTextures.GetDimensions(texW, texH, texN);
	float2 texel = 1.0 / max(float2((float)texW, (float)texH), float2(1.0, 1.0));
	uv = uv * (1.0 - texel) + texel * 0.5;

	// Lod 0: extruded item edges share a UV and auto-mip gradients go undefined,
	// which samples neighbouring empty texels and makes the mesh look translucent.
	float4 color = blockTextures.SampleLevel(linearSampler, float3(uv, input.material), 0);
	if (props.overlayMaterial != 0xffffffffu)
	{
		float4 overlay = blockTextures.SampleLevel(
			linearSampler, float3(uv, props.overlayMaterial), 0);
		color.rgb = lerp(color.rgb, overlay.rgb, overlay.a);
		color.a = max(color.a, overlay.a);
	}

	if (props.tintMode != TINT_MODE_NONE)
		color.rgb *= iconMaterialTint(props.tintMode, props.tintColor);

	// Unlit original albedo. World/sun/torch lighting must not reach HUD icons.
	color.a *= input.opacity;
	if (color.a < 0.5f)
		discard;
	color.a = 1.0f;
	return color;
}
