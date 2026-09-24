#ifndef TERRAIN_FAST
#define TERRAIN_FAST 0
#endif

struct Light
{
    float3 position;
    float radius;
    float3 color;
    float intensity;
    float3 halfExtent;
    uint type;
};

// Block pack is one Texture2DArray at t0. Remaining terrain resources use fixed
// slots so pack growth never collides with the SM5.0 t127 SRV ceiling.
Texture2DArray blockTextures : register(t0);
StructuredBuffer<Light> lights : register(t1);
TextureCubeArray<float> pointShadowMaps : register(t2);
StructuredBuffer<float4> materialEmissions : register(t3);
StructuredBuffer<uint> lightOccluders : register(t4);
struct VoxelLight
{
    int3 position;
    uint packedColor;
};
StructuredBuffer<VoxelLight> voxelLights : register(t5);
Texture2DArray<float> celestialShadowMap : register(t6);
SamplerComparisonState celestialShadowSampler : register(s2);
Texture2D<float4> opaqueSceneColor : register(t7);
Texture2D<float> opaqueSceneDepth : register(t8);
StructuredBuffer<uint> reflectionVoxels : register(t9);
Texture2D<float4> planarReflectionTexture : register(t10);
SamplerState refractionSampler : register(s3);

// Vanilla-style climate colour ramps. Both are indexed by
// (1 - temperature, 1 - rainfall * temperature), matching the layout of the
// grass/foliage colormaps shipped in the resource pack.
Texture2D<float4> grassColormap : register(t11);
Texture2D<float4> foliageColormap : register(t12);
SamplerState colormapSampler : register(s4);

#define TINT_MODE_NONE 0u
#define TINT_MODE_GRASS 1u
#define TINT_MODE_FOLIAGE 2u
#define TINT_MODE_CONSTANT 3u
#define NO_OVERLAY_MATERIAL 0xffffffffu

struct MaterialProperties
{
    float4 tintColor;      // rgb constant tint, a = tint strength
    uint tintMode;
    uint overlayMaterial;  // NO_OVERLAY_MATERIAL when the face has no overlay
    uint overlayTintMode;
    uint frameCount;       // 1 = static; tall strips use N vertical frames
    float frameTime;       // seconds per frame (Minecraft ticks / 20)
    uint interpolate;      // blend between consecutive frames
    float roughness;
    float metallic;
	float4 emissionUvBounds;
	uint emissionWarmMask;
	float3 materialPadding;
};
StructuredBuffer<MaterialProperties> materialProperties : register(t13);

cbuffer LightBuffer : register(b2)
{
    uint lightCount;
    float3 padding;
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

cbuffer ReflectionVolumeBuffer : register(b9)
{
	int3 reflectionVolumeOrigin;
	uint reflectionVolumeSize;
};

cbuffer PlanarReflectionBuffer : register(b10)
{
	matrix planarReflectionViewProjection;
	float planarReflectionPlaneHeight;
	float planarReflectionEnabled;
	float2 planarReflectionPadding;
};

SamplerState sampler0 : register(s0);
SamplerComparisonState shadowSampler : register(s1);

// Greedy faces tile UVs across 0..N. WRAP at every integer samples the opposite
// texture edge (often a dark outline) and shows up as a black grid up close.
// Inset half a texel inside each tile and keep the original UV derivatives so
// mip selection stays continuous across the face.
float2 insetRepeatUv(float2 uv)
{
	float2 tile = frac(uv);
	return tile * 0.9375 + 0.03125;
}

float3 sampleBlockTexture(uint material, float2 uv)
{
	return blockTextures.SampleGrad(sampler0, float3(insetRepeatUv(uv), material), ddx(uv), ddy(uv)).rgb;
}

float4 sampleBlockTextureRGBA(uint material, float2 uv)
{
	return blockTextures.SampleGrad(sampler0, float3(insetRepeatUv(uv), material), ddx(uv), ddy(uv));
}

float sampleBlockTextureAlpha(uint material, float2 uv)
{
	return blockTextures.SampleGrad(sampler0, float3(insetRepeatUv(uv), material), ddx(uv), ddy(uv)).a;
}

float2 animatedFrameUv(float2 uv, uint frame, uint frameCount)
{
	const float frameHeight = 1.0 / float(max(frameCount, 1u));
	return float2(frac(uv.x), (float(frame) + frac(uv.y)) * frameHeight);
}

// Tall Minecraft strips are N stacked square frames. Remap the face UV into the
// current frame (and optionally blend to the next) using waterTime as the clock.
float3 sampleAnimatedTexture(uint material, float2 uv, MaterialProperties props)
{
	if (props.frameCount <= 1u)
		return sampleBlockTexture(material, uv);
	const float phase = abs(waterTime) / max(props.frameTime, 0.0001);
	const uint frameA = (uint)floor(phase) % props.frameCount;
	const float3 colorA = sampleBlockTexture(material, animatedFrameUv(uv, frameA, props.frameCount));
	if (props.interpolate == 0u)
		return colorA;
	const uint frameB = (frameA + 1u) % props.frameCount;
	const float3 colorB = sampleBlockTexture(material, animatedFrameUv(uv, frameB, props.frameCount));
	return lerp(colorA, colorB, frac(phase));
}

float4 sampleAnimatedTextureRGBA(uint material, float2 uv, MaterialProperties props)
{
	if (props.frameCount <= 1u)
		return sampleBlockTextureRGBA(material, uv);
	const float phase = abs(waterTime) / max(props.frameTime, 0.0001);
	const uint frameA = (uint)floor(phase) % props.frameCount;
	const float4 colorA = sampleBlockTextureRGBA(material, animatedFrameUv(uv, frameA, props.frameCount));
	if (props.interpolate == 0u)
		return colorA;
	const uint frameB = (frameA + 1u) % props.frameCount;
	const float4 colorB = sampleBlockTextureRGBA(material, animatedFrameUv(uv, frameB, props.frameCount));
	return lerp(colorA, colorB, frac(phase));
}

float sampleAnimatedTextureAlpha(uint material, float2 uv, MaterialProperties props)
{
	return sampleAnimatedTextureRGBA(material, uv, props).a;
}
bool reflectionVoxelInside(int3 voxel)
{
	int3 local = voxel - reflectionVolumeOrigin;
	return all(local >= 0) && all(local < int3(
		reflectionVolumeSize, reflectionVolumeSize, reflectionVolumeSize));
}

uint reflectionVoxelAt(int3 voxel)
{
	if (!reflectionVoxelInside(voxel)) return 0u;
	uint3 local = (uint3)(voxel - reflectionVolumeOrigin);
	return reflectionVoxels[local.x + reflectionVolumeSize *
		(local.z + reflectionVolumeSize * local.y)];
}

bool traceVoxelReflection(
	float3 surfacePosition,
	float3 surfaceNormal,
	float3 direction,
	out uint hitMaterial,
	out float3 hitPosition,
	out float3 hitNormal,
	out float hitEmissionScale)
{
	hitMaterial = 0u;
	hitPosition = 0.0;
	hitNormal = 0.0;
	hitEmissionScale = 0.0;
	const float3 start = surfacePosition + surfaceNormal * 0.012 + direction * 0.018;
	int3 voxel = (int3)floor(start);
	if (!reflectionVoxelInside(voxel)) return false;

	const int3 step = int3(
		direction.x >= 0.0 ? 1 : -1,
		direction.y >= 0.0 ? 1 : -1,
		direction.z >= 0.0 ? 1 : -1);
	const float3 safeDirection = float3(
		abs(direction.x) < 1e-6 ? (direction.x < 0.0 ? -1e-6 : 1e-6) : direction.x,
		abs(direction.y) < 1e-6 ? (direction.y < 0.0 ? -1e-6 : 1e-6) : direction.y,
		abs(direction.z) < 1e-6 ? (direction.z < 0.0 ? -1e-6 : 1e-6) : direction.z);
	const float3 deltaDistance = abs(1.0 / safeDirection);
	const float3 nextBoundary = float3(voxel) + float3(
		step.x > 0 ? 1.0 : 0.0,
		step.y > 0 ? 1.0 : 0.0,
		step.z > 0 ? 1.0 : 0.0);
	float3 sideDistance = (nextBoundary - start) / safeDirection;

	[loop]
	for (uint iteration = 0u; iteration < 64u; ++iteration)
	{
		const float nearest = min(sideDistance.x, min(sideDistance.y, sideDistance.z));
		const bool crossX = abs(sideDistance.x - nearest) < 1e-5;
		const bool crossY = abs(sideDistance.y - nearest) < 1e-5;
		const bool crossZ = abs(sideDistance.z - nearest) < 1e-5;
		if (crossX) { voxel.x += step.x; sideDistance.x += deltaDistance.x; }
		if (crossY) { voxel.y += step.y; sideDistance.y += deltaDistance.y; }
		if (crossZ) { voxel.z += step.z; sideDistance.z += deltaDistance.z; }
		if (!reflectionVoxelInside(voxel)) return false;

		const uint packed = reflectionVoxelAt(voxel);
		if ((packed & 0x80000000u) != 0u)
		{
			hitMaterial = packed & 0xFFFFu;
			hitEmissionScale = float((packed >> 16u) & 15u) / 15.0;
			hitPosition = start + direction * nearest;
			hitNormal = crossX ? float3(-step.x, 0.0, 0.0) :
				(crossY ? float3(0.0, -step.y, 0.0) : float3(0.0, 0.0, -step.z));
			return true;
		}
	}
	return false;
}

float2 reflectionHitUv(float3 position, float3 normal)
{
	if (abs(normal.x) > 0.5) return position.zy;
	if (abs(normal.y) > 0.5) return position.xz;
	return position.xy;
}

float3 reconstructViewPosition(float2 uv, float sceneDepth)
{
	const float depthDenominator = sceneDepth - projection._33;
	const float viewZ = projection._43 /
		(abs(depthDenominator) > 0.000001 ? depthDenominator : -0.000001);
	const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
	return float3(
		ndc.x * viewZ / max(abs(projection._11), 0.0001),
		ndc.y * viewZ / max(abs(projection._22), 0.0001),
		viewZ);
}

float3 reconstructWorldPosition(float2 uv, float sceneDepth)
{
	const float3 viewPosition = reconstructViewPosition(uv, sceneDepth);
	// view maps world to view as mul(world, view). Its 3x3 is camera rotation R,
	// so viewPos * R^T is mul(R, viewPos). FXC SM5 has no inverse() intrinsic.
	return cameraPosition + mul((float3x3)view, viewPosition);
}

// Advance a screen-space transmission ray to the captured opaque surface.
// This must not use a fixed world-space distance: at long view distances a
// short endpoint projects almost onto the translucent fragment and makes the
// refraction disappear. Infinite-far depth has no finite sky position, so sky
// pixels use a far point on the refracted direction instead.
float transmissionRayTravel(
	float3 viewSurface,
	float3 viewDirection,
	float capturedDepth)
{
	if (viewDirection.z <= 0.0001)
		return 0.0;
	if (capturedDepth >= 0.999999)
	{
		const float targetViewZ = viewSurface.z +
			max(2048.0, abs(viewSurface.z) * 8.0);
		return max((targetViewZ - viewSurface.z) / viewDirection.z, 0.0);
	}
	const float depthDenominator = capturedDepth - projection._33;
	if (abs(depthDenominator) <= 0.0000001)
		return 0.0;
	const float capturedViewZ = projection._43 / depthDenominator;
	return max((capturedViewZ - viewSurface.z) / viewDirection.z, 0.0);
}

bool reflectionDepthDelta(
	float3 viewSurface,
	float3 viewDirection,
	float travel,
	out float2 uv,
	out float depthDelta)
{
	uv = 0.0;
	depthDelta = -100000.0;
	const float3 rayPosition = viewSurface + viewDirection * travel;
	if (rayPosition.z <= 0.02) return false;
	const float4 clip = mul(float4(rayPosition, 1.0), projection);
	if (clip.w <= 0.0001) return false;
	const float2 ndc = clip.xy / clip.w;
	uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
	if (any(uv <= 0.002) || any(uv >= 0.998)) return false;
	const float sceneDepth = opaqueSceneDepth.SampleLevel(
		refractionSampler, uv, 0.0).r;
	if (sceneDepth >= 0.99999) return false;
	const float depthDenominator = sceneDepth - projection._33;
	if (abs(depthDenominator) <= 0.000001) return false;
	const float sceneViewZ = projection._43 / depthDenominator;
	depthDelta = rayPosition.z - sceneViewZ;
	return sceneViewZ > viewSurface.z + 0.02;
}

bool traceScreenReflection(
	float3 viewSurface,
	float3 viewDirection,
	float2 sceneSize,
	out float2 hitUv,
	out float confidence,
	uint stepCount,
	float stride)
{
	hitUv = 0.0;
	confidence = 0.0;
	float previousTravel = 0.04 + 0.04 * stride;
	float previousDelta = -100000.0;
	bool previousValid = false;
	float travel = previousTravel;
	[loop]
	for (uint stepIndex = 1u; stepIndex <= stepCount; ++stepIndex)
	{
		travel += (0.20 + (float)stepIndex * 0.085) * stride;
		float2 uv;
		float depthDelta;
		const bool valid = reflectionDepthDelta(
			viewSurface, viewDirection, travel, uv, depthDelta);
		if (!valid)
		{
			previousTravel = travel;
			previousValid = false;
			continue;
		}

		if ((previousValid && previousDelta < 0.0 && depthDelta >= 0.0) ||
			(!previousValid && depthDelta >= 0.0 && depthDelta < 0.10))
		{
			float lowerTravel = previousValid ? previousTravel : max(0.08, travel - 0.35);
			float upperTravel = travel;
			float2 refinedUv = uv;
			float refinedDelta = depthDelta;
			[unroll]
			for (uint refinement = 0u; refinement < 4u; ++refinement)
			{
				const float middleTravel = (lowerTravel + upperTravel) * 0.5;
				float2 middleUv;
				float middleDelta;
				if (reflectionDepthDelta(
					viewSurface, viewDirection, middleTravel, middleUv, middleDelta) &&
					middleDelta >= 0.0)
				{
					upperTravel = middleTravel;
					refinedUv = middleUv;
					refinedDelta = middleDelta;
				}
				else lowerTravel = middleTravel;
			}
			const float thickness = 0.10 + upperTravel * 0.018;
			if (refinedDelta <= thickness)
			{
				const float edgeDistance = min(
					min(refinedUv.x, 1.0 - refinedUv.x),
					min(refinedUv.y, 1.0 - refinedUv.y));
				const float edgeFade = max(
					0.012, 4.0 / max(min(sceneSize.x, sceneSize.y), 1.0));
				hitUv = refinedUv;
				confidence = smoothstep(edgeFade * 0.25, edgeFade, edgeDistance) *
					(1.0 - (float)(stepIndex - 1u) / max((float)stepCount + 6.0, 1.0)) *
					(1.0 - saturate(refinedDelta / max(thickness, 0.0001)) * 0.35);
				return confidence > 0.0001;
			}
		}
		previousTravel = travel;
		previousDelta = depthDelta;
		previousValid = true;
	}
	return false;
}

float waterHash(float2 value)
{
    return frac(sin(dot(value, float2(127.1, 311.7))) * 43758.5453);
}

float waterNoise(float2 value)
{
    float2 cell = floor(value);
    float2 blend = frac(value);
    blend = blend * blend * (3.0 - 2.0 * blend);
    return lerp(
        lerp(waterHash(cell), waterHash(cell + float2(1, 0)), blend.x),
        lerp(waterHash(cell + float2(0, 1)), waterHash(cell + 1.0), blend.x),
        blend.y);
}

// 丁达尔效应: forward Mie scatter + irregular shaft banding along the light axis.
float3 tyndallInScatter(
    float3 viewRay,
    float3 lightDir,
    float3 lightColor,
    float lightIntensity,
    float3 worldPos,
    float distance,
    float densityScale)
{
    const float align = saturate(dot(viewRay, lightDir));
    const float phase = pow(align, 7.0) * 0.85 + pow(align, 2.2) * 0.45;
    if (phase * lightIntensity < 0.001)
        return 0.0;

    const float3 lateral = viewRay - lightDir * dot(viewRay, lightDir);
    const float2 shaftUv = worldPos.xz * 0.038 + lateral.xz * (distance * 0.09);
    float bands =
        0.50 + 0.50 * sin(dot(shaftUv, float2(19.3, -13.1)) + worldPos.y * 0.18);
    bands *= 0.40 + 0.60 * waterNoise(shaftUv * 1.65 + 2.7);
    bands *= 0.55 + 0.45 * waterNoise(shaftUv * 0.55 - 1.3);
    bands = smoothstep(0.22, 0.78, bands);

    const float optical = saturate(1.0 - exp(-distance * densityScale));
    return lightColor * lightIntensity * phase * bands * optical;
}

struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    nointerpolation uint material : MATID;
    nointerpolation uint aoCorners : AO;
    float opacity : OPACITY;
    // Temperature and rainfall, evaluated per vertex from the same noise the
    // terrain generator uses. The fields change over thousands of blocks, so
    // interpolating across a greedy quad is exact enough and costs nothing.
    float2 climate : CLIMATE;
    float2 shoreDir : SHORE;
};

// Vanilla indexes the colormaps with (1 - temperature) across and
// (1 - rainfall * temperature) down. Sampling bilinearly instead of per-texel
// keeps wide biome transitions free of stair-stepping.
float3 climateColormapColor(uint mode, float2 climate, float altitude)
{
    const float cooled = saturate(climate.x - max(altitude - 80.0, 0.0) * 0.0032);
    const float rainfall = saturate(climate.y) * cooled;
    const float2 uv = float2(1.0 - cooled, 1.0 - rainfall) * (255.0 / 256.0) + (0.5 / 256.0);
    return mode == TINT_MODE_FOLIAGE
        ? foliageColormap.SampleLevel(colormapSampler, uv, 0).rgb
        : grassColormap.SampleLevel(colormapSampler, uv, 0).rgb;
}

float3 materialTint(uint mode, float4 constantColor, float2 climate, float altitude)
{
    float3 tint = float3(1.0, 1.0, 1.0);
    if (mode == TINT_MODE_CONSTANT)
        tint = constantColor.rgb;
    else if (mode == TINT_MODE_GRASS || mode == TINT_MODE_FOLIAGE)
        tint = climateColormapColor(mode, climate, altitude);
    return tint;
}

cbuffer LightOcclusionBuffer : register(b3)
{
    int4 lightOcclusionOrigins[64];
    uint lightOcclusionSize;
    float3 lightOcclusionPadding;
};

cbuffer VoxelLightBuffer : register(b4)
{
    uint voxelLightTableMask;
    uint voxelLightingEnabled;
    uint lightingQuality;
    uint waterMaterial;
    uint worldSeed;
    float3 biomePadding;
};

cbuffer ChunkLightBuffer : register(b5)
{
    uint chunkLightCount;
    uint3 chunkLightPadding;
    uint4 chunkLightIndices[16];
};

cbuffer DynamicShadowBuffer : register(b6)
{
    float4 dynamicShadowLights[8];
	float4 dynamicShadowRadiance[8];
    uint4 dynamicShadowMetadata[8];
	uint dynamicShadowLightCount;
	uint3 dynamicShadowPadding;
};

cbuffer DaylightBuffer : register(b7)
{
    float3 sunDirection;
    float sunIntensity;
    float3 sunColor;
    float ambientIntensity;
    float3 daylightSkyColor;
    float dayPhase;
	float3 moonDirection;
	float moonIntensity;
	float3 moonColor;
	uint celestialLightIndex;
	float cloudCoverage;
	float cloudDensity;
	float cloudShadowStrength;
	float cloudTime;
};

float cloudShadowHash(float2 p)
{
	p = frac(p * float2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return frac(p.x * p.y);
}

float cloudShadowNoise(float2 p)
{
	float2 cell = floor(p), local = frac(p);
	local = local * local * (3.0 - 2.0 * local);
	return lerp(lerp(cloudShadowHash(cell), cloudShadowHash(cell + float2(1, 0)), local.x),
		lerp(cloudShadowHash(cell + float2(0, 1)), cloudShadowHash(cell + 1.0), local.x), local.y);
}

float cloudSunVisibility(float3 worldPosition)
{
	if (sunDirection.y <= 0.035 || sunIntensity <= 0.001) return 1.0;
	float2 cloudPoint = worldPosition.xz + sunDirection.xz *
		((135.0 - worldPosition.y) / max(sunDirection.y, 0.035));
	float2 p = (cloudPoint + float2(cloudTime * 2.15, cloudTime * 0.72)) / 420.0;
	float2 baseP = p;
	float value = 0.0, weight = 0.52;
	[unroll] for (uint octave = 0u; octave < 4u; ++octave) {
		value += cloudShadowNoise(p) * weight;
		p = mul(p, float2x2(1.62, 1.18, -1.18, 1.62)) + 17.7;
		weight *= 0.49;
	}
	float threshold = lerp(0.665, 0.405, saturate(cloudCoverage));
	float thicknessVariation = cloudShadowNoise(baseP * 0.43 - 19.2);
	float opacity = smoothstep(threshold - 0.055, threshold + 0.13,
		value - (1.0 - cloudDensity) * 0.08);
	opacity *= lerp(0.28, 1.0, thicknessVariation * thicknessVariation);
	float phase = frac(cloudTime / 110.0 + cloudShadowNoise(baseP * 0.31 + float2(13.7, -8.4)));
	float moisture = smoothstep(0.12, 0.34, phase) * (1.0 - smoothstep(0.72, 0.96, phase));
	opacity *= lerp(0.42, 1.22, moisture);
	return 1.0 - opacity * saturate(cloudShadowStrength);
}

cbuffer CelestialShadowBuffer : register(b8)
{
	matrix celestialViewProjection[3];
	float4 cascadeSplits;
	float2 celestialShadowTexelSize;
	float celestialShadowEnabled;
	float celestialShadowPadding;
	float4 cascadeTexelWorld;
};

float sampleCelestialCascade(float3 samplePosition, uint cascade)
{
	float4 projected = mul(float4(samplePosition, 1.0), celestialViewProjection[cascade]);
	float3 ndc = projected.xyz / max(projected.w, 0.0001);
	float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
	if (any(uv < 0.001) || any(uv > 0.999) || ndc.z <= 0.0 || ndc.z >= 1.0)
		return -1.0;

	float receiverZ = ndc.z - (cascade == 0u ? 0.0014 : 0.0007);
#if TERRAIN_FAST
	// A small cross kernel keeps the fast terrain path from producing a hard,
	// staircase-like shadow boundary while retaining far fewer taps than PCF.
	const float2 fastOffsets[4] = {
		float2(-1.0, 0.0), float2(1.0, 0.0),
		float2(0.0, -1.0), float2(0.0, 1.0)
	};
	float fastVisibility = celestialShadowMap.SampleCmpLevelZero(
		celestialShadowSampler, float3(uv, cascade), receiverZ);
	[unroll] for (uint tap = 0u; tap < 4u; ++tap)
		fastVisibility += celestialShadowMap.SampleCmpLevelZero(
			celestialShadowSampler,
			float3(uv + fastOffsets[tap] * celestialShadowTexelSize * 2.8, cascade),
			receiverZ);
	return fastVisibility / 5.0;
#else
	if (lightingQuality == 0u)
	{
		const float2 lowOffsets[4] = {
			float2(-1.0, 0.0), float2(1.0, 0.0),
			float2(0.0, -1.0), float2(0.0, 1.0)
		};
		float lowVisibility = celestialShadowMap.SampleCmpLevelZero(
			celestialShadowSampler, float3(uv, cascade), receiverZ);
		[unroll] for (uint tap = 0u; tap < 4u; ++tap)
			lowVisibility += celestialShadowMap.SampleCmpLevelZero(
				celestialShadowSampler,
				float3(uv + lowOffsets[tap] * celestialShadowTexelSize * 2.8, cascade),
				receiverZ);
		return lowVisibility / 5.0;
	}

	const float2 poisson[12] = {
		float2(-0.326, -0.406), float2(-0.840, -0.074),
		float2(-0.696,  0.457), float2(-0.203,  0.621),
		float2( 0.271,  0.696), float2( 0.715,  0.458),
		float2( 0.869, -0.118), float2( 0.473, -0.542),
		float2( 0.056, -0.892), float2(-0.514, -0.780),
		float2( 0.987,  0.281), float2(-0.934,  0.348)
	};
	// Rotate a disk-shaped kernel per small world-space cell. Medium quality uses
	// eight taps and high quality twelve; the hardware comparison sampler makes
	// every tap bilinear, producing a smooth penumbra without a separate blur pass.
	const float angle = frac(sin(dot(floor(samplePosition.xz * 4.0),
		float2(12.9898, 78.233))) * 43758.5453) * 6.28318531;
	const float2 rotation = float2(cos(angle), sin(angle));
	const float qualityRadius = lightingQuality >= 2u ? 5.25 : 3.75;
	const float cascadeScale = 1.0 + 0.18 * (float)cascade;
	const float filterRadius = qualityRadius * cascadeScale;
	const uint tapCount = lightingQuality >= 2u ? 12u : 8u;
	float visibility = celestialShadowMap.SampleCmpLevelZero(
		celestialShadowSampler, float3(uv, cascade), receiverZ);
	[loop] for (uint tap = 0u; tap < tapCount; ++tap)
	{
		const float2 offset = float2(
			poisson[tap].x * rotation.x - poisson[tap].y * rotation.y,
			poisson[tap].x * rotation.y + poisson[tap].y * rotation.x);
		float2 sampleUv = uv + offset * celestialShadowTexelSize * filterRadius;
		visibility += celestialShadowMap.SampleCmpLevelZero(
			celestialShadowSampler, float3(sampleUv, cascade), receiverZ);
	}
	return visibility / (float)(tapCount + 1u);
#endif
}

float celestialShadowVisibility(float3 worldPosition, float3 normal)
{
	if (celestialShadowEnabled < 0.5) return 1.0;
	float2 planar = worldPosition.xz - cameraPosition.xz;
	float distance = length(planar);
	uint cascade = 2u;
	if (distance < cascadeSplits.x) cascade = 0u;
	else if (distance < cascadeSplits.y) cascade = 1u;

	float3 lightDir = sunIntensity >= moonIntensity ? sunDirection : moonDirection;
	// Keep the receiver on this side of the face. A texel-sized push along the
	// normal punches 1-block cave roofs and lights the floor from the sun.
	float3 samplePosition = worldPosition + normal * 0.04 + lightDir * 0.02;

	float visibility = sampleCelestialCascade(samplePosition, cascade);
	if (visibility < 0.0 && cascade < 2u)
		visibility = sampleCelestialCascade(samplePosition, cascade + 1u);
	if (visibility < 0.0 && cascade < 1u)
		visibility = sampleCelestialCascade(samplePosition, 2u);

#if !TERRAIN_FAST
	// Cross-fade cascades near their split instead of exposing a moving seam.
	// Only pixels in the narrow transition band pay for the second lookup.
	if (lightingQuality != 0u && cascade < 2u && visibility >= 0.0)
	{
		const float split = cascade == 0u ? cascadeSplits.x : cascadeSplits.y;
		const float blend = saturate((distance - split * 0.88) / max(split * 0.12, 0.001));
		if (blend > 0.0)
		{
			const float nextVisibility = sampleCelestialCascade(samplePosition, cascade + 1u);
			if (nextVisibility >= 0.0)
				visibility = lerp(visibility, nextVisibility, blend);
		}
	}
#endif
	// Outside every cascade: assume occluded so caves and tunnels stay dark
	// instead of picking up full celestial light.
	return visibility < 0.0 ? 0.0 : visibility;
}

uint voxelLightHash(int3 position)
{
    uint hash = (uint)position.x * 73856093u ^
        (uint)position.y * 19349663u ^
        (uint)position.z * 83492791u;
    hash ^= hash >> 16u;
    return hash;
}

float3 unpackVoxelLight(uint packed)
{
    return float3(packed & 255u, (packed >> 8u) & 255u, (packed >> 16u) & 255u) * (2.0 / 255.0);
}

float3 voxelLightAt(int3 position)
{
    if (voxelLightingEnabled == 0u) return 0.0;
    uint slot = voxelLightHash(position) & voxelLightTableMask;
    [loop]
    for (uint probe = 0u; probe < 24u; ++probe)
    {
        VoxelLight entry = voxelLights[slot];
        if (entry.packedColor == 0u) return 0.0;
        if (all(entry.position == position)) return unpackVoxelLight(entry.packedColor);
        slot = (slot + 1u) & voxelLightTableMask;
    }
    return 0.0;
}

float3 sampleVoxelLighting(float3 worldPosition, float3 normal)
{
    // Move to the air side of the face, then trilinearly interpolate voxel
    // centers. This gives propagated block light smooth transitions without
    // remeshing geometry when an emitter changes.
    float3 grid = worldPosition + normal * 0.51 - 0.5;
    int3 base = (int3)floor(grid);
    float3 blend = frac(grid);
    if (lightingQuality == 0u || TERRAIN_FAST != 0)
        return voxelLightAt(base + int3(
            blend.x >= 0.5 ? 1 : 0,
            blend.y >= 0.5 ? 1 : 0,
            blend.z >= 0.5 ? 1 : 0));
    float3 c000 = voxelLightAt(base);
    float3 c100 = voxelLightAt(base + int3(1, 0, 0));
    float3 c010 = voxelLightAt(base + int3(0, 1, 0));
    float3 c110 = voxelLightAt(base + int3(1, 1, 0));
    float3 c001 = voxelLightAt(base + int3(0, 0, 1));
    float3 c101 = voxelLightAt(base + int3(1, 0, 1));
    float3 c011 = voxelLightAt(base + int3(0, 1, 1));
    float3 c111 = voxelLightAt(base + int3(1, 1, 1));
    float3 lower = lerp(lerp(c000, c100, blend.x), lerp(c010, c110, blend.x), blend.y);
    float3 upper = lerp(lerp(c001, c101, blend.x), lerp(c011, c111, blend.x), blend.y);
    return lerp(lower, upper, blend.z);
}

uint chunkLightIndex(uint localIndex)
{
    uint4 group = chunkLightIndices[localIndex >> 2u];
    uint component = localIndex & 3u;
    if (component == 0u) return group.x;
    if (component == 1u) return group.y;
    if (component == 2u) return group.z;
    return group.w;
}

float sampleFaceAO(uint packedCorners, float2 uv, float3 normal)
{
    float ao00 = float(packedCorners & 3u);
    float ao10 = float((packedCorners >> 2) & 3u);
    float ao11 = float((packedCorners >> 4) & 3u);
    float ao01 = float((packedCorners >> 6) & 3u);

    // A four-corner gradient on a vertical one-block face reads as a small
    // diagonal triangle. Use the same energy as a flat per-face value there;
    // horizontal surfaces retain smooth corner AO.
    if (abs(normal.y) < 0.5)
        return (ao00 + ao10 + ao11 + ao01) * 0.25;

    float2 localUV = saturate(uv);
    return lerp(
        lerp(ao00, ao10, localUV.x),
        lerp(ao01, ao11, localUV.x),
        localUV.y
    );
}

float pointShadowVisibilityFromMap(
    float3 worldPosition,
    float3 normal,
    Light light,
    uint shadowIndex,
    bool entityOnly)
{
    // Radial depth is continuous at cubemap face boundaries. A small normal
    // offset and slope-aware world-space bias prevent self-shadowing without
    // opening the large gaps caused by the old fixed 0.6-block bias.
    float3 receiverPosition = worldPosition + normal * 0.01;
    float3 fromLight = receiverPosition - light.position;
    float distanceToLight = length(fromLight);
    float3 direction = fromLight / max(distanceToLight, 0.0001);

    float receiverFacing = saturate(dot(normal, -direction));
    // Radial depth is already linear, so only a sub-voxel comparison bias is
    // needed. Larger values visibly detach shadows from block faces.
    float worldBias = lerp(0.012, 0.004, receiverFacing);
    float comparisonDepth = saturate((distanceToLight - worldBias) / light.radius);

    float visibility = pointShadowMaps.SampleCmpLevelZero(
        shadowSampler, float4(direction, (float)shadowIndex), comparisonDepth);
#if TERRAIN_FAST
	return visibility;
#else
    float3 helper = abs(direction.y) < 0.99
        ? float3(0.0, 1.0, 0.0)
        : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(helper, direction));
    float3 bitangent = cross(direction, tangent);
	const float2 poisson[12] = {
		float2(-0.326, -0.406), float2(-0.840, -0.074),
		float2(-0.696,  0.457), float2(-0.203,  0.621),
		float2( 0.271,  0.696), float2( 0.715,  0.458),
		float2( 0.869, -0.118), float2( 0.473, -0.542),
		float2( 0.056, -0.892), float2(-0.514, -0.780),
		float2( 0.987,  0.281), float2(-0.934,  0.348)
	};
	const float angle = frac(sin(dot(
		floor(worldPosition * 4.0), float3(12.9898, 78.233, 37.719))) *
		43758.5453) * 6.28318531;
	const float2 rotation = float2(cos(angle), sin(angle));
	// Widen the angular kernel away from the emitter to approximate the growing
	// penumbra of a finite-sized torch or glowing block. No blocker-search pass is
	// needed, so this remains bounded at nine taps on medium quality.
	const float normalizedDistance = saturate(distanceToLight / max(light.radius, 0.001));
	const float qualityRadius = lightingQuality >= 2u ? 6.0 : 4.2;
	const float entityScale = entityOnly ? 1.20 : 1.0;
	const float filterStep = lerp(1.4, qualityRadius, normalizedDistance * normalizedDistance) *
		entityScale / 512.0;
	const uint tapCount = lightingQuality >= 2u ? 12u : 8u;
	[loop]
	for (uint tap = 0u; tap < tapCount; ++tap)
	{
		const float2 offset = float2(
			poisson[tap].x * rotation.x - poisson[tap].y * rotation.y,
			poisson[tap].x * rotation.y + poisson[tap].y * rotation.x);
		const float3 sampleDirection = normalize(direction +
			tangent * (offset.x * filterStep) + bitangent * (offset.y * filterStep));
		visibility += pointShadowMaps.SampleCmpLevelZero(
			shadowSampler, float4(sampleDirection, (float)shadowIndex), comparisonDepth);
	}
	return visibility / (float)(tapCount + 1u);
#endif
}

float pointShadowVisibility(float3 worldPosition, float3 normal, Light light, uint lightIndex)
{
	[unroll]
	for (uint shadowIndex = 0u; shadowIndex < 8u; ++shadowIndex)
	{
		if (shadowIndex >= dynamicShadowLightCount) break;
		if (dynamicShadowMetadata[shadowIndex].x == lightIndex)
			return pointShadowVisibilityFromMap(worldPosition, normal, light, shadowIndex, false);
	}
	return -1.0;
}

float3 dynamicEntityShadowVisibility(float3 worldPosition, float3 normal)
{
	// Accumulate each emitter independently. A player can therefore cast a
	// distinct silhouette from every selected light, while unoccluded lights
	// naturally fill rather than deleting the other lights' shadows.
	float3 visibleRadiance = 0.0;
	float3 totalRadiance = 0.0;
	[unroll]
	for (uint shadowIndex = 0u; shadowIndex < 8u; ++shadowIndex)
	{
		if (shadowIndex >= dynamicShadowLightCount) break;
		if (dynamicShadowMetadata[shadowIndex].y == 0u) continue;
		float4 source = dynamicShadowLights[shadowIndex];
		float3 toLight = source.xyz - worldPosition;
		float distanceToShadowLight = length(toLight);
		if (source.w <= 0.0 || distanceToShadowLight >= source.w) continue;
		float diffuse = saturate(dot(normal, toLight / max(distanceToShadowLight, 0.0001)));
		if (diffuse <= 0.0) continue;
		float normalizedDistance = distanceToShadowLight / source.w;
		float attenuation = saturate(1.0 - normalizedDistance * normalizedDistance);
		attenuation *= attenuation;
		float4 sourceRadiance = dynamicShadowRadiance[shadowIndex];
		float3 weight = max(sourceRadiance.rgb * sourceRadiance.a * attenuation * diffuse, 0.0);
		if (all(weight <= 0.0001)) continue;

		Light shadowLight;
		shadowLight.position = source.xyz;
		shadowLight.radius = source.w;
		shadowLight.color = 1.0;
		shadowLight.intensity = 0.0;
		shadowLight.halfExtent = 0.0;
		shadowLight.type = 0u;
		float visibility = pointShadowVisibilityFromMap(
			worldPosition, normal, shadowLight, shadowIndex, true);
		visibleRadiance += weight * visibility;
		totalRadiance += weight;
	}
	return float3(
		totalRadiance.r > 0.0001 ? visibleRadiance.r / totalRadiance.r : 1.0,
		totalRadiance.g > 0.0001 ? visibleRadiance.g / totalRadiance.g : 1.0,
		totalRadiance.b > 0.0001 ? visibleRadiance.b / totalRadiance.b : 1.0);
}

bool voxelIsOccluder(int3 worldVoxel, uint lightIndex)
{
    int3 lightOcclusionOrigin = lightOcclusionOrigins[lightIndex].xyz;
    int3 local = worldVoxel - lightOcclusionOrigin;
    uint localIndex = (uint)local.x + lightOcclusionSize *
        ((uint)local.z + lightOcclusionSize * (uint)local.y);
    uint index = lightIndex * lightOcclusionSize * lightOcclusionSize * lightOcclusionSize + localIndex;
    return (lightOccluders[index >> 5] & (1u << (index & 31u))) != 0u;
}

bool voxelInsideVolume(int3 worldVoxel, uint lightIndex)
{
    int3 lightOcclusionOrigin = lightOcclusionOrigins[lightIndex].xyz;
    int3 local = worldVoxel - lightOcclusionOrigin;
    return all(local >= 0) && all(local < int3(lightOcclusionSize, lightOcclusionSize, lightOcclusionSize));
}

float3 lightSamplePosition(Light light, float3 surfacePosition, uint sampleIndex)
{
    if (light.type == 0u)
        return light.position;

    const float2 samplePattern[9] = {
        float2( 0.0,  0.0),
        float2(-0.7, -0.7), float2( 0.0, -0.7), float2( 0.7, -0.7),
        float2(-0.7,  0.0),                       float2( 0.7,  0.0),
        float2(-0.7,  0.7), float2( 0.0,  0.7), float2( 0.7,  0.7)
    };
    float2 pattern = samplePattern[min(sampleIndex, 8u)];
    float3 fromCenter = surfacePosition - light.position;
    float3 absoluteDirection = abs(fromCenter);

    if (absoluteDirection.x >= absoluteDirection.y && absoluteDirection.x >= absoluteDirection.z)
        return light.position + float3(
            fromCenter.x >= 0.0 ? light.halfExtent.x : -light.halfExtent.x,
            pattern.x * light.halfExtent.y,
            pattern.y * light.halfExtent.z
        );
    if (absoluteDirection.y >= absoluteDirection.z)
        return light.position + float3(
            pattern.x * light.halfExtent.x,
            fromCenter.y >= 0.0 ? light.halfExtent.y : -light.halfExtent.y,
            pattern.y * light.halfExtent.z
        );
    return light.position + float3(
        pattern.x * light.halfExtent.x,
        pattern.y * light.halfExtent.y,
        fromCenter.z >= 0.0 ? light.halfExtent.z : -light.halfExtent.z
    );
}

void meshLightSample(
    Light light,
    uint sampleIndex,
    uint samplesPerFace,
    out float3 position,
    out float3 sampleNormal)
{
    const float2 samplePattern[3] = {
        float2( 0.0,  0.0),
        float2(-0.68, -0.68), float2( 0.68,  0.68)
    };
    uint face = min(sampleIndex / samplesPerFace, 5u);
    uint localSample = sampleIndex % samplesPerFace;
    uint patternIndex = samplesPerFace == 1u ? 0u : localSample + 1u;
    float2 pattern = samplePattern[patternIndex];

    if (face == 0u) {
        sampleNormal = float3(-1.0, 0.0, 0.0);
        position = light.position + float3(-light.halfExtent.x, pattern.x * light.halfExtent.y, pattern.y * light.halfExtent.z);
    }
    else if (face == 1u) {
        sampleNormal = float3(1.0, 0.0, 0.0);
        position = light.position + float3(light.halfExtent.x, pattern.x * light.halfExtent.y, pattern.y * light.halfExtent.z);
    }
    else if (face == 2u) {
        sampleNormal = float3(0.0, -1.0, 0.0);
        position = light.position + float3(pattern.x * light.halfExtent.x, -light.halfExtent.y, pattern.y * light.halfExtent.z);
    }
    else if (face == 3u) {
        sampleNormal = float3(0.0, 1.0, 0.0);
        position = light.position + float3(pattern.x * light.halfExtent.x, light.halfExtent.y, pattern.y * light.halfExtent.z);
    }
    else if (face == 4u) {
        sampleNormal = float3(0.0, 0.0, -1.0);
        position = light.position + float3(pattern.x * light.halfExtent.x, pattern.y * light.halfExtent.y, -light.halfExtent.z);
    }
    else {
        sampleNormal = float3(0.0, 0.0, 1.0);
        position = light.position + float3(pattern.x * light.halfExtent.x, pattern.y * light.halfExtent.y, light.halfExtent.z);
    }
}

float voxelLightVisibility(float3 worldPosition, float3 normal, float3 samplePosition, uint lightIndex)
{
    float3 start = worldPosition + normal * 0.01;
    int3 voxel = (int3)floor(start);
    int3 endVoxel = (int3)floor(samplePosition);
    if (!voxelInsideVolume(voxel, lightIndex) || !voxelInsideVolume(endVoxel, lightIndex))
        return -1.0;

    float3 ray = samplePosition - start;
    float rayLength = length(ray);
    if (rayLength <= 0.02)
        return 1.0;
    float3 direction = ray / rayLength;
    int3 step = int3(
        direction.x >= 0.0 ? 1 : -1,
        direction.y >= 0.0 ? 1 : -1,
        direction.z >= 0.0 ? 1 : -1
    );
    float3 safeDirection = float3(
        abs(direction.x) < 1e-6 ? (direction.x < 0.0 ? -1e-6 : 1e-6) : direction.x,
        abs(direction.y) < 1e-6 ? (direction.y < 0.0 ? -1e-6 : 1e-6) : direction.y,
        abs(direction.z) < 1e-6 ? (direction.z < 0.0 ? -1e-6 : 1e-6) : direction.z
    );
    float3 deltaDistance = abs(1.0 / safeDirection);
    float3 nextBoundary = float3(voxel) + float3(
        step.x > 0 ? 1.0 : 0.0,
        step.y > 0 ? 1.0 : 0.0,
        step.z > 0 ? 1.0 : 0.0
    );
    float3 sideDistance = (nextBoundary - start) / safeDirection;

    [loop]
    for (uint iteration = 0; iteration < 64u; ++iteration)
    {
        float nearest = min(sideDistance.x, min(sideDistance.y, sideDistance.z));
        if (nearest >= rayLength - 0.01)
            return 1.0;

        bool crossX = abs(sideDistance.x - nearest) < 1e-5;
        bool crossY = abs(sideDistance.y - nearest) < 1e-5;
        bool crossZ = abs(sideDistance.z - nearest) < 1e-5;
        if (crossX) { voxel.x += step.x; sideDistance.x += deltaDistance.x; }
        if (crossY) { voxel.y += step.y; sideDistance.y += deltaDistance.y; }
        if (crossZ) { voxel.z += step.z; sideDistance.z += deltaDistance.z; }
        if (all(voxel == endVoxel))
            return 1.0;
        if (voxelIsOccluder(voxel, lightIndex))
            return 0.0;
    }
    return 1.0;
}

uint lightSampleCount(Light light, uint meshSamplesPerFace)
{
    if (light.type == 0u) return 1u;
    return light.type == 2u ? 6u * meshSamplesPerFace : 9u;
}

bool lightAffectsSurface(Light light, float3 surfacePosition)
{
    float3 extent = light.type == 0u ? float3(0.0, 0.0, 0.0) : light.halfExtent;
    float3 distanceFromEmitter = max(abs(surfacePosition - light.position) - extent, 0.0);
    return dot(distanceFromEmitter, distanceFromEmitter) < light.radius * light.radius;
}

// Compact Cook-Torrance GGX BRDF. This replaces the fixed-power Blinn lobe:
// rough blocks now have broad, faint highlights, smooth blocks have tight ones,
// and metals reflect their own colour without also producing diffuse energy.
float3 fresnelSchlick(float cosineTheta, float3 f0)
{
	const float oneMinus = 1.0 - saturate(cosineTheta);
	const float oneMinus2 = oneMinus * oneMinus;
	return f0 + (1.0 - f0) * oneMinus2 * oneMinus2 * oneMinus;
}

float distributionGGX(float nDotH, float roughness)
{
	const float alpha = max(roughness * roughness, 0.045);
	const float alpha2 = alpha * alpha;
	const float denominator = nDotH * nDotH * (alpha2 - 1.0) + 1.0;
	return alpha2 / max(3.14159265 * denominator * denominator, 0.0001);
}

float geometrySmithGGX(float nDotV, float nDotL, float roughness)
{
	const float k = (roughness + 1.0) * (roughness + 1.0) * 0.125;
	const float view = nDotV / lerp(nDotV, 1.0, k);
	const float light = nDotL / lerp(nDotL, 1.0, k);
	return view * light;
}

float3 evaluateDirectBRDF(
	float3 albedo,
	float roughness,
	float metallic,
	float3 normal,
	float3 viewDirection,
	float3 lightDirection,
	float3 radiance)
{
	const float nDotL = saturate(dot(normal, lightDirection));
	const float nDotV = saturate(dot(normal, viewDirection));
	if (nDotL <= 0.0 || nDotV <= 0.0) return 0.0;

	const float3 halfway = normalize(viewDirection + lightDirection);
	const float nDotH = saturate(dot(normal, halfway));
	const float vDotH = saturate(dot(viewDirection, halfway));
	const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, metallic);
	const float3 fresnel = fresnelSchlick(vDotH, f0);
	const float distribution = distributionGGX(nDotH, roughness);
	const float geometry = geometrySmithGGX(nDotV, nDotL, roughness);
	const float3 specular = distribution * geometry * fresnel /
		max(4.0 * nDotV * nDotL, 0.001);
	const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) *
		albedo * 0.318309886;
	return (diffuse + specular) * radiance * nDotL;
}

float3 evaluateLightSample(
    Light light,
    uint lightIndex,
    uint sampleIndex,
    uint meshSamplesPerFace,
    float3 surfacePosition,
    float3 surfaceNormal,
    float3 viewDirection,
	float3 albedo,
	float roughness,
	float metallic,
    out float sampleWeight)
{
    float3 samplePosition;
    sampleWeight = 1.0;
    if (light.type == 2u) {
        float3 sampleNormal;
        meshLightSample(light, sampleIndex, meshSamplesPerFace, samplePosition, sampleNormal);
        float3 fromEmitter = surfacePosition - samplePosition;
        sampleWeight = max(dot(sampleNormal, normalize(fromEmitter)), 0.0);
        if (sampleWeight <= 0.0001) return 0.0;
    }
    else {
        samplePosition = lightSamplePosition(light, surfacePosition, sampleIndex);
    }

    float3 toLight = samplePosition - surfacePosition;
    float distanceSquared = dot(toLight, toLight);
    if (distanceSquared >= light.radius * light.radius) return 0.0;

    float distanceToLight = sqrt(distanceSquared);
    float3 lightDirection = toLight / max(distanceToLight, 0.0001);
    float diffuseFactor = max(dot(surfaceNormal, lightDirection), 0.0);
    if (diffuseFactor <= 0.0) return 0.0;

    float visibility = 1.0;
	// A cubemap is centred on one point, so it is valid only for a point emitter.
	// Area-face and mesh samples originate at different positions and must each
	// retain their own world-space DDA result. Skip DDA when a cubemap already
	// covers this light — the 64-step walk is otherwise wasted.
	if (light.type == 0u)
	{
		float mappedVisibility = pointShadowVisibility(
			surfacePosition, surfaceNormal, light, lightIndex);
		visibility = mappedVisibility >= 0.0
			? mappedVisibility
			: voxelLightVisibility(surfacePosition, surfaceNormal, samplePosition, lightIndex);
	}
#if !TERRAIN_FAST
	else
	{
		visibility = voxelLightVisibility(
			surfacePosition, surfaceNormal, samplePosition, lightIndex);
	}
#endif
	// Keep occluded lights readable instead of crushing their contribution to black.
	visibility = lerp(0.18, 1.0, visibility);

    float normalizedDistance = distanceToLight / light.radius;
    float attenuation = saturate(1.0 - normalizedDistance * normalizedDistance * normalizedDistance);
	return evaluateDirectBRDF(
		albedo, roughness, metallic, surfaceNormal, viewDirection,
		lightDirection, light.color * light.intensity) *
		attenuation * visibility * sampleWeight;
}

float4 main(PSInput input) : SV_TARGET
{
    const bool isWater = input.material == waterMaterial;
    const float animationTime = abs(waterTime);
    float2 materialUv = input.uv;
    float waveA = 0.0;
    float waveB = 0.0;
    float waveC = 0.0;
    if (waterTime < 0.0)
    {
        // Distort texture coordinates rather than clip-space vertices. This
        // preserves watertight terrain edges and eliminates black T-junction
        // gaps on large, greedily meshed ocean floors.
        const float motion = saturate(length(waterMotion.xyz) * 0.22);
        const float distortionPhase =
            dot(input.worldPosition, float3(0.71, 0.43, 0.57)) + animationTime * 2.4;
        materialUv += float2(sin(distortionPhase), cos(distortionPhase * 1.31)) *
            (0.0020 + motion * 0.0090);
    }
    if (isWater)
    {
        const float2 xz = input.worldPosition.xz;
        const float2 shore = length(input.shoreDir) > 0.1 ? normalize(input.shoreDir) : float2(0.328, 0.212);
        const float2 along = float2(-shore.y, shore.x);
        const float pA = waterNoise(xz * 0.044 + 1.7) * 6.28318531;
        const float pB = waterNoise(xz * 0.036 + 4.3) * 6.28318531;
        const float pC = waterNoise(xz * 0.068 + 8.1) * 6.28318531;
        const float pD = waterNoise(xz * 0.092 + 2.9) * 6.28318531;
        const float sA = lerp(0.33, 0.51, waterNoise(xz * 0.028 + 0.5));
        const float sB = lerp(0.31, 0.53, waterNoise(xz * 0.032 + 1.5));
        const float sC = lerp(0.30, 0.54, waterNoise(xz * 0.024 + 2.5));
        const float tA = lerp(0.85, 1.25, waterNoise(xz * 0.05 + 3.1));
        const float tB = lerp(0.80, 1.30, waterNoise(xz * 0.055 + 5.7));
        const float tC = lerp(0.75, 1.35, waterNoise(xz * 0.048 + 7.3));
        const float baseA = sin(dot(xz, shore) * sA + animationTime * 1.18 * tA + pA);
        const float chop = sin(dot(xz, normalize(shore * 0.80 + along * -0.45)) *
            lerp(0.36, 0.58, waterNoise(xz * 0.076)) + animationTime * 2.05 + pD);
        waveA = baseA * 0.72 + chop * 0.28;
        waveB = sin(dot(xz, along) * sB - animationTime * 0.94 * tB + pB);
        waveC = sin(dot(xz, normalize(shore * 0.65 + along * 0.35)) * sC + animationTime * 0.68 * tC + pC);
        if (input.normal.y > 0.15)
        {
            // World-space tiling; frame animation (not UV scroll) drives the look
            // when the still strip is a tall Minecraft atlas.
            materialUv = input.worldPosition.xz * 0.55 +
                float2(waveA + waveC * 0.55, waveB - waveC * 0.45) * 0.058;
        }
        else
        {
            materialUv = input.uv * 0.70 +
                float2(animationTime * 0.030, -animationTime * 0.38);
        }
    }

    const MaterialProperties properties = materialProperties[input.material];
    const bool climateTinted = !isWater &&
        (properties.tintMode != TINT_MODE_NONE ||
            properties.overlayMaterial != NO_OVERLAY_MATERIAL);
    const float2 surfaceClimate = input.climate;
    if (climateTinted && properties.tintMode != TINT_MODE_CONSTANT)
    {
        // A small world-space warp keeps a tinted surface from reading as one
        // flat repeat of the same 16x16 tile across a whole biome.
        const float2 shift = float2(
            waterNoise(input.worldPosition.xz * 0.21 + surfaceClimate.x * 8.0),
            waterNoise(input.worldPosition.xz * 0.19 + surfaceClimate.y * 11.0 + 4.0)) * 2.0 - 1.0;
        materialUv += shift * 0.045;
    }

    float4 albedoSample = sampleAnimatedTextureRGBA(input.material, materialUv, properties);
    float3 albedo = albedoSample.rgb;
	const bool redstoneDustSurface = (input.aoCorners & 0x2000u) != 0u;
	const float redstoneStrength = float((input.aoCorners >> 9u) & 0xfu) / 15.0;
	if (redstoneDustSurface)
	{
		const float poweredRed = smoothstep(0.0, 1.0, redstoneStrength);
		albedo *= float3(
			0.28 + 0.72 * poweredRed,
			0.012 + 0.105 * poweredRed * poweredRed,
			0.008 + 0.032 * poweredRed);
	}
    // Door/trapdoor window masks apply only to the two broad panel faces.
    // Their thickness faces carry bit 8 in the packed AO value and stay solid.
    const bool forceSolidPanelEdge = (input.aoCorners & 0x100u) != 0u;
    if (!isWater && input.opacity >= 0.999 && !forceSolidPanelEdge)
        clip(albedoSample.a - 0.08);
    if (climateTinted)
    {
        const float altitude = input.worldPosition.y;
        if (properties.tintMode != TINT_MODE_NONE)
        {
            const float3 tint = materialTint(
                properties.tintMode, properties.tintColor, surfaceClimate, altitude);
            albedo *= lerp(float3(1.0, 1.0, 1.0), tint, properties.tintColor.a);
        }
        if (properties.overlayMaterial != NO_OVERLAY_MATERIAL)
        {
            // Grass-like sides keep an untinted base and composite a tinted
            // cutout over it, the way the vanilla side overlay works.
            const MaterialProperties overlayProps =
                materialProperties[properties.overlayMaterial];
            const float4 overlay = sampleAnimatedTextureRGBA(
                properties.overlayMaterial, materialUv, overlayProps);
            const float3 overlayTint = materialTint(
                properties.overlayTintMode, properties.tintColor, surfaceClimate, altitude);
            albedo = lerp(albedo, overlay.rgb * overlayTint, overlay.a);
        }
    }
    float4 materialEmission = materialEmissions[input.material];
	const float2 emissionUv = frac(materialUv);
	const float4 emissionBounds = properties.emissionUvBounds;
	materialEmission.a *= step(emissionBounds.x, emissionUv.x) *
		step(emissionBounds.y, emissionUv.y) * step(emissionUv.x, emissionBounds.z) *
		step(emissionUv.y, emissionBounds.w);
	if (properties.emissionWarmMask == 1u)
		materialEmission.a *= smoothstep(0.45, 0.72, albedo.r) *
			smoothstep(0.12, 0.30, albedo.g) *
			(1.0 - smoothstep(0.35, 0.55, albedo.b));
	if (properties.emissionWarmMask == 2u)
		materialEmission.a *= smoothstep(0.32, 0.62, dot(albedo, float3(0.2126, 0.7152, 0.0722)));
	if ((input.aoCorners & 0x4000u) != 0u)
		materialEmission.a *= float((input.aoCorners >> 9u) & 0xfu) / 15.0;
	if (redstoneDustSurface)
	{
		materialEmission = float4(
			1.0, 0.035 + 0.12 * redstoneStrength, 0.015,
			0.62 * redstoneStrength * redstoneStrength);
	}
    float alpha = input.opacity;
    float3 N = normalize(input.normal);
    const float roughness = saturate(properties.roughness);
    const float metallic = saturate(properties.metallic);
    if (isWater && input.normal.y > 0.15)
        N = normalize(N + float3(
            (waveA + waveC * 0.45) * 0.145,
            0.0,
            (waveB - waveC * 0.35) * 0.145));
    // Do not derive normals from ddx/ddy of point-filtered albedo. Nearby
    // texel edges make those derivatives binary, which lights and shadows
    // as flickering black stripes when the camera moves.
    float3 V = normalize(cameraPosition - input.worldPosition);

    // Approximate a participating water volume at the boundary. This adds
    // wavelength-dependent absorption and in-scattering without a ray-march,
    // extra texture lookup, or additional render pass.
    if (isWater)
    {
        const float grazing = max(abs(dot(N, V)), 0.16);
        float opticalDepth = (input.normal.y > 0.15 ? 0.48 : 0.90) / grazing;
        opticalDepth += min(length(cameraPosition - input.worldPosition) * 0.012, 1.25);
        const float3 transmission = exp(-float3(0.65, 0.22, 0.10) * opticalDepth);
        const float3 scattering = float3(0.025, 0.20, 0.28) * (1.0 - transmission);
        albedo = albedo * transmission + scattering;
        alpha = clamp(1.0 - exp(-0.78 * opticalDepth), 0.42, 0.84);
    }

    float ao = lerp(0.55, 1.0, sampleFaceAO(input.aoCorners, input.uv, N) / 3.0);
    // Metals keep more of their own tint in crevices; harsh AO was greying iron/gold.
    const float shadingAo = lerp(ao, lerp(0.82, 1.0, ao), metallic);
    const float daylightAmount = saturate((ambientIntensity - 0.055) / 0.48);
    const float3 ambientColor = lerp(
        float3(0.55, 0.62, 0.92),
        float3(1.00, 0.97, 0.88),
        daylightAmount);
	float celestialVisibility = celestialShadowVisibility(input.worldPosition, N);
	Light celestialProxy = lights[celestialLightIndex];
	if (celestialProxy.type == 3u && celestialProxy.radius > 0.0 &&
		length(input.worldPosition - celestialProxy.position) < celestialProxy.radius)
	{
		float mappedCelestialVisibility = pointShadowVisibility(
			input.worldPosition, N, celestialProxy, celestialLightIndex);
		if (mappedCelestialVisibility >= 0.0)
			celestialVisibility = min(celestialVisibility, mappedCelestialVisibility);
	}
	celestialVisibility = lerp(0.24, 1.0, celestialVisibility);
	// A cheap hemispherical environment model gives upward faces cool sky fill
	// and downward faces a dim, warm ground bounce. It avoids the flat look of a
	// directionless ambient constant while remaining a handful of ALU operations.
	const float skyFacing = saturate(N.y * 0.5 + 0.5);
	const float3 skyFill = ambientColor * lerp(
		float3(0.26, 0.22, 0.18),
		lerp(float3(0.62, 0.72, 1.0), daylightSkyColor, 0.35),
		skyFacing);
	// Sky fill only where celestial light reaches. Enclosed caves retain a very
	// small neutral floor and are primarily lit by the propagated voxel field.
	float3 ambientLighting = skyFill * lerp(0.08, ambientIntensity, celestialVisibility);
	// The visible water boundary receives emissive objects through the planar
	// reflection below. Applying the voxel-light field to this surface as well
	// creates a yellow wave-shaped light pool that looks like a displaced copy of
	// the emitter, but is not constrained by the reflection intersection.
	float3 emittedLighting = isWater
		? 0.0
		: sampleVoxelLighting(input.worldPosition, N) * 0.55;
	// Dynamic casters are not represented in the cached voxel-light field.
	// Shadow only the emitted component so the selected real emitter controls
	// the silhouette without incorrectly darkening neutral world ambience.
	const float3 dynamicShadow = dynamicEntityShadowVisibility(input.worldPosition, N);
	emittedLighting *= lerp(0.55, 1.0, dynamicShadow);
	float3 indirectLighting = (ambientLighting + emittedLighting) * shadingAo;
	float3 directLighting = 0.0;

	// Celestial illumination has no radius or distance attenuation: every loaded
	// chunk receives the same sun/moon direction. The camera-centred proxy exists
	// only to provide detailed nearby cubemap shadows.
	directLighting += evaluateDirectBRDF(
		albedo, roughness, metallic, N, V, sunDirection,
		sunColor * sunIntensity) * celestialVisibility * cloudSunVisibility(input.worldPosition) * shadingAo;
	directLighting += evaluateDirectBRDF(
		albedo, roughness, metallic, N, V, moonDirection,
		moonColor * moonIntensity) * celestialVisibility * shadingAo;

#if !TERRAIN_FAST
    for (uint localLight = 0; localLight < chunkLightCount; ++localLight)
    {
		// Do not paint an unconstrained block-light lobe onto the reflective water
		// boundary. The planar pass contains the correctly placed emitter image.
		if (isWater) continue;
        uint i = chunkLightIndex(localLight);
        Light l = lights[i];
		if (l.type == 3u) continue;
        if (!lightAffectsSurface(l, input.worldPosition)) continue;

        // Nearby emitters use two rays per face for soft shadows. Distant
        // emitters use one centered ray per face, where the difference is
        // much less visible but the saved occlusion work is substantial.
        const float meshDistance = length(input.worldPosition - l.position);
        const uint meshSamplesPerFace = l.type == 2u && lightingQuality != 0u &&
            (lightingQuality == 2u || meshDistance < 8.0) ? 2u : 1u;
        const uint sampleCount = lightSampleCount(l, meshSamplesPerFace);
        float3 lightContribution = 0.0;
		float totalSampleWeight = 0.0;

        [loop]
        for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
        {
            float sampleWeight;
			lightContribution += evaluateLightSample(
                l, i, sampleIndex, meshSamplesPerFace,
				input.worldPosition, N, V, albedo,
				// Water receives emitter reflections through its dedicated planar path;
				// keep the direct lobe fully rough to avoid a displaced duplicate streak.
				isWater ? 1.0 : roughness,
				isWater ? 0.0 : metallic,
				sampleWeight
            );
			totalSampleWeight += sampleWeight;
        }

		directLighting += lightContribution / max(totalSampleWeight, 0.0001);
    }
#endif

    float3 emissive = materialEmission.rgb * materialEmission.a;
	// Keep this path in HDR. The previous saturate happened before bloom and
	// erased both bright emitter cores and natural highlight roll-off.
    const float3 diffuseAlbedo = albedo * (1.0 - metallic);
    float3 finalColor = diffuseAlbedo * indirectLighting + directLighting + emissive;
    if (!isWater)
    {
        const float shininess = lerp(18.0, 96.0, saturate(1.0 - roughness));
        const float3 reflection = reflect(-V, N);
        const float skyLift = saturate(reflection.y * 0.55 + 0.45);
        float3 envColor = lerp(
            daylightSkyColor * 0.22,
            daylightSkyColor,
            skyLift);
        envColor += sunColor * pow(saturate(dot(reflection, sunDirection)), shininess) *
            sunIntensity * 1.35 * celestialVisibility;
        // Tint environment by albedo so reflections stay iron-grey / gold-yellow.
        const float3 metalEnv = envColor * lerp(float3(1.0, 1.0, 1.0), albedo, metallic);
        finalColor += metalEnv * metallic * (0.10 + 0.34 * (1.0 - roughness)) *
            shadingAo * lerp(0.15, 1.0, celestialVisibility);
    }

    // A wavelength-aware single-aperture diffraction approximation. It changes
    // transmitted light at the boundary itself instead of drawing a displaced
    // second copy of the scene. Shape enters through the surface normal and
    // viewing angle; wavelength-dependent phase provides compact dispersion.
    if (input.opacity < 0.999)
    {
        const float cosineTheta = max(abs(dot(N, V)), 0.08);
        const float sineTheta = sqrt(saturate(1.0 - cosineTheta * cosineTheta));
        const float thickness = saturate(1.0 - input.opacity);
        const float3 wavelength = float3(0.650, 0.510, 0.475);
        const float virtualAperture = lerp(0.42, 1.10, thickness);
        const float3 beta = 3.14159265 * virtualAperture * sineTheta / wavelength;
        const float3 sincValue = sin(beta) / max(abs(beta), 0.0001);
        const float3 diffractionEnvelope = sincValue * sincValue;

        // Optical path and refractive index vary by wavelength, producing the
        // subtle spectral ordering seen at real translucent boundaries.
        const float3 refractiveIndex = isWater
            ? float3(1.331, 1.336, 1.340)
            : float3(1.505, 1.515, 1.526);
        const float pathLength = lerp(0.18, 0.72, thickness) / cosineTheta;
        const float3 phase = 6.28318531 * pathLength * (refractiveIndex - 1.0) / wavelength;
        const float3 interference = 0.5 + 0.5 * cos(phase);
        const float3 spectralResponse = diffractionEnvelope * interference;
        const float spectralMean = dot(spectralResponse, float3(0.333333, 0.333333, 0.333333));
        // The camera volume is authoritative for water. At grazing angles a
        // perturbed wave normal can otherwise flip the sign and incorrectly
        // turn an above-water ray into an exiting/TIR ray.
        const bool entering = isWater ? waterTime >= 0.0 : dot(N, V) >= 0.0;
        const float edgeStrength = thickness * sineTheta * sineTheta *
            (isWater ? (entering ? 0.055 : 0.12) : 0.16);
        finalColor *= 1.0 + (spectralResponse - spectralMean) * edgeStrength;
        finalColor += spectralResponse * edgeStrength * 0.035;

        // Snell refraction and exact unpolarized dielectric Fresnel reflection. Flipping the normal
        // for an inside-to-outside ray gives the correct eta ratio; HLSL's
        // refract returns a zero vector beyond the critical angle, which is the
        // total-internal-reflection condition.
        float3 opticalNormal = entering ? N : -N;
        if (isWater)
        {
            const float grazingStability = 1.0 - smoothstep(
                0.035, 0.18, abs(dot(opticalNormal, V)));
            const float3 geometricWaterNormal = entering
                ? float3(0.0, 1.0, 0.0) : float3(0.0, -1.0, 0.0);
            opticalNormal = normalize(lerp(
                opticalNormal, geometricWaterNormal, grazingStability * 0.72));
        }
        const float viewCosine = saturate(dot(opticalNormal, V));
        const float ior = isWater ? 1.336 : 1.515;
        const float incidentIor = entering ? 1.0 : ior;
        const float transmittedIor = entering ? ior : 1.0;
        const float eta = incidentIor / transmittedIor;
        const float3 incidentDirection = -V;
        const float3 reflectedDirection = normalize(reflect(incidentDirection, opticalNormal));
        const float3 refractedDirection = refract(incidentDirection, opticalNormal, eta);
        const bool totalInternalReflection = dot(refractedDirection, refractedDirection) < 0.000001;
		const float3 cameraToSurface = input.worldPosition - cameraPosition;
		const float planeRayDenominator = cameraToSurface.y;
		const float planeIntersection = abs(planeRayDenominator) > 0.0001
			? (planarReflectionPlaneHeight - cameraPosition.y) / planeRayDenominator
			: 1.0;
		const bool waterPlanarSurface = isWater && entering && input.normal.y > 0.15 &&
			planarReflectionEnabled > 0.5 &&
			cameraPosition.y >= planarReflectionPlaneHeight &&
			abs(input.worldPosition.y - planarReflectionPlaneHeight) < 0.85 &&
			planeIntersection > 0.0 && planeIntersection < 64.0;
		// Recover the point on the flat reflection plane that belongs to this
		// fragment's camera ray. Merely replacing worldPosition.y projects the
		// animated wave vertically; at grazing angles that selects a different
		// screen ray and can put a nearby object's reflection behind the object.
		const float3 reflectionSurfacePosition = waterPlanarSurface
			? cameraPosition + cameraToSurface * planeIntersection
			: input.worldPosition;
		// Waves remain an optical-normal effect for Fresnel and glints. Geometry
		// fallbacks start on the actual plane and reflect around its stable normal,
		// otherwise a tilted wave can send an emissive hit behind its source.
		const float3 geometricReflectionNormal = waterPlanarSurface
			? float3(0.0, 1.0, 0.0)
			: opticalNormal;
		const float3 traceIncidentDirection = waterPlanarSurface
			? normalize(reflectionSurfacePosition - cameraPosition)
			: incidentDirection;
		const float3 tracedReflectionDirection = normalize(reflect(
			traceIncidentDirection, geometricReflectionNormal));

        const float transmittedSineSquared = eta * eta *
            (1.0 - viewCosine * viewCosine);
        const float transmittedCosine = sqrt(max(0.0, 1.0 - transmittedSineSquared));
        const float perpendicularNumerator =
            incidentIor * viewCosine - transmittedIor * transmittedCosine;
        const float perpendicularDenominator = max(
            incidentIor * viewCosine + transmittedIor * transmittedCosine, 0.0001);
        const float parallelNumerator =
            transmittedIor * viewCosine - incidentIor * transmittedCosine;
        const float parallelDenominator = max(
            transmittedIor * viewCosine + incidentIor * transmittedCosine, 0.0001);
        const float fresnel = 0.5 * (
            perpendicularNumerator * perpendicularNumerator /
                (perpendicularDenominator * perpendicularDenominator) +
            parallelNumerator * parallelNumerator /
                (parallelDenominator * parallelDenominator));
        const float reflectionWeight = totalInternalReflection ? 1.0 : fresnel;

        const float reflectionElevation = saturate(reflectedDirection.y * 0.5 + 0.5);
        float3 reflectionColor = lerp(
            daylightSkyColor * 0.18,
            daylightSkyColor,
            pow(reflectionElevation, 0.65));
        const float sunGlint = pow(saturate(dot(reflectedDirection, sunDirection)), 384.0) *
            sunIntensity * celestialVisibility;
        if (isWater && !waterPlanarSurface)
        {
            // Enclosed water has no sky hemisphere. Keep a dim cave fill until a
            // voxel or SSR hit replaces it; the surface sky colour reads as a
            // hole to daylight under stone.
            reflectionColor = ambientColor * 0.12;
        }
        else
            reflectionColor += sunColor * sunGlint * 1.4;
        if (waterTime < 0.0)
            reflectionColor *= float3(0.42, 0.78, 0.90);

		// Screen-space data cannot represent geometry behind the camera. Trace the
		// reflected world ray through a camera-local voxel volume first, so misses
		// have a stable world-space result rather than a mirrored screen image.
		// Planar open-sky water already captures the mirrored scene; a voxel hit
		// there is a second, misplaced copy of nearby emitters.
		uint reflectedMaterial = 0u;
		float reflectedEmissionScale = 0.0;
		float3 reflectedHitPosition = 0.0;
		float3 reflectedHitNormal = 0.0;
		if ((!isWater || !waterPlanarSurface) && traceVoxelReflection(
			reflectionSurfacePosition, geometricReflectionNormal, tracedReflectionDirection,
			reflectedMaterial, reflectedHitPosition, reflectedHitNormal, reflectedEmissionScale) &&
			dot(reflectedHitPosition - reflectionSurfacePosition,
				geometricReflectionNormal) > 0.08)
		{
			// Voxel-traced hits carry no interpolated climate, so reuse this
			// surface's. Reflections are always local enough for that to hold.
			const MaterialProperties reflectedProperties =
				materialProperties[reflectedMaterial];
			const float2 reflectedUv = reflectionHitUv(reflectedHitPosition, reflectedHitNormal);
			const float3 reflectedAlbedo = sampleAnimatedTexture(
				reflectedMaterial, reflectedUv,
				reflectedProperties) *
				lerp(float3(1.0, 1.0, 1.0),
					materialTint(reflectedProperties.tintMode, reflectedProperties.tintColor,
						input.climate, reflectedHitPosition.y),
					reflectedProperties.tintMode == TINT_MODE_NONE
						? 0.0 : reflectedProperties.tintColor.a);
			const float reflectedDaylight = saturate(
				dot(reflectedHitNormal, sunDirection)) * sunIntensity * celestialVisibility;
			const float3 reflectedLighting = ambientColor *
				lerp(0.08, ambientIntensity, celestialVisibility) +
				sunColor * reflectedDaylight * 0.82;
			float4 reflectedEmission = materialEmissions[reflectedMaterial];
			reflectedEmission.a *= reflectedEmissionScale;
			const float2 repeatedReflectedUv = frac(reflectedUv);
			const float4 reflectedBounds = reflectedProperties.emissionUvBounds;
			reflectedEmission.a *= step(reflectedBounds.x, repeatedReflectedUv.x) *
				step(reflectedBounds.y, repeatedReflectedUv.y) *
				step(repeatedReflectedUv.x, reflectedBounds.z) *
				step(repeatedReflectedUv.y, reflectedBounds.w);
			if (reflectedProperties.emissionWarmMask == 1u)
				reflectedEmission.a *= smoothstep(0.45, 0.72, reflectedAlbedo.r) *
					smoothstep(0.12, 0.30, reflectedAlbedo.g) *
					(1.0 - smoothstep(0.35, 0.55, reflectedAlbedo.b));
			if (reflectedProperties.emissionWarmMask == 2u)
				reflectedEmission.a *= smoothstep(0.32, 0.62,
					dot(reflectedAlbedo, float3(0.2126, 0.7152, 0.0722)));
			reflectionColor = reflectedAlbedo * (
				saturate(reflectedLighting + 0.12) +
				reflectedEmission.rgb * reflectedEmission.a);
		}

        const float3 refractionElevation = totalInternalReflection
            ? 0.0 : saturate(refractedDirection.y * 0.5 + 0.5);
        const float3 refractionAmbient = lerp(
            float3(0.025, 0.10, 0.13),
            daylightSkyColor * 0.58,
            refractionElevation);
        float3 refractionTint = float3(0.78, 0.94, 1.00);
        if (!isWater)
        {
            // Stained glass dyes the transmitted scene/light; clear glass stays nearly neutral.
            float3 glassFilter = saturate(albedo);
            if (properties.tintMode == TINT_MODE_CONSTANT)
            {
                const float dye = saturate(properties.tintColor.a * 1.45);
                glassFilter = lerp(
                    glassFilter,
                    saturate(properties.tintColor.rgb),
                    dye);
            }
            refractionTint = lerp(float3(0.94, 0.97, 1.00), glassFilter, 0.94);
        }
        float3 refractedColor = finalColor * refractionTint +
            refractionAmbient * thickness * 0.12;
		float glassReflectionContrast = 1.25;
		float glassSsrConfidence = 0.0;
		if (isWater)
		{
			uint sceneWidth;
			uint sceneHeight;
			opaqueSceneColor.GetDimensions(sceneWidth, sceneHeight);
			const float2 sceneSize = max(float2(sceneWidth, sceneHeight), 1.0);
			const float2 screenUv = input.position.xy / sceneSize;
            const float3 viewNormal = normalize(mul(opticalNormal, (float3x3) view));
            const float3 viewSurface = mul(float4(input.worldPosition, 1.0), view).xyz;
            const float3 viewIncident = normalize(viewSurface);
			const float3 viewTraceSurface = mul(float4(reflectionSurfacePosition, 1.0), view).xyz;
			const float3 viewTraceReflected = normalize(mul(
				tracedReflectionDirection, (float3x3) view));
			const float grazingAmount = 1.0 - saturate(dot(viewNormal, -viewIncident));
			float3 viewRefracted = refract(viewIncident, viewNormal, eta);
			bool usedPlanarReflection = false;

			// Project the geometric plane point, not the animated SV_Position. The
			// reflection capture and the displaced mesh do not share screen coordinates
			// at grazing angles.
			if (waterPlanarSurface)
			{
				const float3 planePoint = reflectionSurfacePosition;
				const float4 reflectionClip = mul(
					float4(planePoint, 1.0), planarReflectionViewProjection);
				if (reflectionClip.w > 0.0001)
				{
					const float2 planarNdc = reflectionClip.xy / reflectionClip.w;
					const float2 planarUv = float2(
						planarNdc.x * 0.5 + 0.5, -planarNdc.y * 0.5 + 0.5);
					const float planarEdge = min(
						min(planarUv.x, 1.0 - planarUv.x),
						min(planarUv.y, 1.0 - planarUv.y));
					const float planarConfidence = smoothstep(0.002, 0.045, planarEdge);
					if (planarConfidence > 0.0001)
					{
						const float2 safePlanarUv = clamp(planarUv, 0.002, 0.998);
						uint reflectionWidth;
						uint reflectionHeight;
						uint reflectionMipCount;
						planarReflectionTexture.GetDimensions(
							0, reflectionWidth, reflectionHeight, reflectionMipCount);
						// Warp UVs around the physical plane hit. Grazing view-space
						// normal offsets jump to a different camera ray and put the
						// object image behind its source, so only the wave field is used
						// and the amplitude shrinks toward the horizon.
						const float2 waveWarp = float2(waveA - waveC, waveB + waveC) *
							lerp(0.0075, 0.0020, grazingAmount);
						const float2 warpedUv = clamp(safePlanarUv + waveWarp, 0.002, 0.998);
						const float waveRoughness = saturate(
							(abs(waveA) + abs(waveB) + abs(waveC)) * 0.35);
						const float roughnessLod = min(
							lerp(0.0, 1.35, waveRoughness),
							max((float)reflectionMipCount - 1.0, 0.0));
						const float3 planarSharp = planarReflectionTexture.SampleGrad(
							refractionSampler, warpedUv, ddx(warpedUv), ddy(warpedUv)).rgb;
						const float3 planarSoft = planarReflectionTexture.SampleLevel(
							refractionSampler, warpedUv, roughnessLod).rgb;
						const float3 planarColor = lerp(planarSharp, planarSoft, 0.35);
						// Once the planar pass is valid it is the authoritative geometric
						// reflection. Do not retain an emissive voxel/SSR ghost underneath it.
						reflectionColor = planarColor;
						usedPlanarReflection = true;
					}
				}
			}

			float2 hitUv = 0.0;
			float hitConfidence = 0.0;
			// SSR is only a fallback for water. Blending its grazing-angle hit over
			// a valid planar image duplicated the object on the far side of the water.
			if (!usedPlanarReflection && traceScreenReflection(
				viewTraceSurface, viewTraceReflected, sceneSize, hitUv, hitConfidence,
				16u, 1.0))
            {
				// When the planar pass is the intended image, an SSR hit is the
				// same object sampled on a different ray and appears behind it.
				if (!waterPlanarSurface)
				{
					const float2 warpedHitUv = clamp(
						hitUv + float2(waveA - waveC, waveB + waveC) *
							lerp(0.0075, 0.0020, grazingAmount),
						0.002, 0.998);
					const float3 ssrColor = opaqueSceneColor.SampleLevel(
						refractionSampler, warpedHitUv, 0.0).rgb;
					reflectionColor = lerp(
						reflectionColor, ssrColor,
						0.72 * hitConfidence);
				}
            }
			const float opaqueDepth = opaqueSceneDepth.SampleLevel(
				refractionSampler, screenUv, 0.0).r;
			// For the row-vector LH projection used by the renderer,
			// ndcDepth = P33 + P43/viewZ. Recover the opaque seabed depth and
			// advance the Snell-refracted ray to that plane.
			const float refractedTravel = transmissionRayTravel(
				viewSurface, viewRefracted, opaqueDepth);
			const float3 refractedViewPosition = viewSurface + viewRefracted * refractedTravel;
			const float4 refractedClip = mul(float4(refractedViewPosition, 1.0), projection);
			const float2 refractedNdc = refractedClip.xy / max(refractedClip.w, 0.0001);
			const float2 rawRefractedUv = float2(
				refractedNdc.x * 0.5 + 0.5,
				-refractedNdc.y * 0.5 + 0.5);
			const float2 rawRefractionOffset = rawRefractedUv - screenUv;
			// Preserve a visible optical bend at long range. The same physical
			// displacement shrinks below one screen pixel as the water recedes.
			float2 refractionPixels = rawRefractionOffset * sceneSize;
			const float refractionPixelLength = length(refractionPixels);
			if (refractionPixelLength > 0.0001)
			{
				const float minimumPixels = lerp(0.45, 1.35, grazingAmount);
				const float maximumPixels = min(sceneSize.x, sceneSize.y) * 0.12;
				refractionPixels *= clamp(
					refractionPixelLength, minimumPixels, maximumPixels) /
					refractionPixelLength;
			}
			float2 refractedUv = screenUv + refractionPixels / sceneSize;
			refractedUv += float2(waveA - waveC, waveB + waveC) *
				lerp(0.0035, 0.0010, grazingAmount);
			refractedUv = clamp(refractedUv, 0.002, 0.998);

			// Do not pull an opaque foreground object through a farther water pixel.
			// The old unchecked displaced lookup sampled the glowstone above the
			// water and displayed it as a detached copy toward the horizon.
			float refractedSampleDepth = opaqueSceneDepth.SampleLevel(
				refractionSampler, refractedUv, 0.0).r;
			float refractedSampleDepthDenominator =
				refractedSampleDepth - projection._33;
			float refractedSampleViewZ = projection._43 /
				(abs(refractedSampleDepthDenominator) > 0.000001
					? refractedSampleDepthDenominator : -0.000001);
			bool refractedSky = refractedSampleDepth >= 0.999999;
			bool validRefractedSample = refractedSky ||
				refractedSampleViewZ > viewSurface.z + 0.001;
			// If the full grazing offset crosses a foreground silhouette, shorten
			// it until it still lands behind the water. Falling straight back to
			// screenUv made both very near and horizon geometry lose refraction.
			[unroll]
			for (uint transmissionAttempt = 0u;
				transmissionAttempt < 3u && !validRefractedSample;
				++transmissionAttempt)
			{
				refractedUv = lerp(screenUv, refractedUv, 0.5);
				refractedSampleDepth = opaqueSceneDepth.SampleLevel(
					refractionSampler, refractedUv, 0.0).r;
				refractedSampleDepthDenominator =
					refractedSampleDepth - projection._33;
				refractedSampleViewZ = projection._43 /
					(abs(refractedSampleDepthDenominator) > 0.000001
						? refractedSampleDepthDenominator : -0.000001);
				refractedSky = refractedSampleDepth >= 0.999999;
				validRefractedSample = refractedSky ||
					refractedSampleViewZ > viewSurface.z + 0.001;
			}
			const float2 transmissionUv = validRefractedSample
				? refractedUv : screenUv;
			float3 sceneTransmission;
			if (!entering && refractedSky)
			{
				// Looking out into clear depth: reconstruct sky rather than
				// trusting a capture that may have been volume-tinted.
				const float elevation = saturate(refractedDirection.y * 0.5 + 0.5);
				sceneTransmission = lerp(
					daylightSkyColor * 0.35,
					daylightSkyColor,
					pow(elevation, 0.65));
				sceneTransmission += sunColor * sunIntensity *
					pow(saturate(dot(normalize(refractedDirection), sunDirection)), 256.0) * 1.2;
			}
			else
			{
				sceneTransmission = opaqueSceneColor.SampleLevel(
					refractionSampler, transmissionUv, 0.0).rgb;
			}
			refractedColor = sceneTransmission * refractionTint +
				refractionAmbient * thickness * 0.10;
			// Soft underwater exit blur only when looking out through the free
			// surface. Keep it mild so the air-side image stays readable.
			if (waterTime < 0.0 && !entering)
			{
				const float exitBlur = saturate(0.15 + reflectionWeight * 0.55);
				const float2 blurStep = (0.35 + exitBlur * 0.9) / sceneSize;
				const float2 blurPattern[5] = {
					float2(0, 0), float2(1, 0), float2(-1, 0), float2(0, 1), float2(0, -1)
				};
				float3 blurred = 0.0;
				[unroll] for (uint tap = 0u; tap < 5u; ++tap)
					blurred += opaqueSceneColor.SampleLevel(
						refractionSampler,
						clamp(transmissionUv + blurPattern[tap] * blurStep, 0.002, 0.998),
						0.0).rgb;
				refractedColor = lerp(
					refractedColor,
					(blurred / 5.0) * refractionTint + refractionAmbient * thickness * 0.10,
					exitBlur * 0.35);
			}
			alpha = 1.0;
		}
		else
		{
			// Glass uses the captured opaque scene for a reflected image and for
			// what is seen through the pane. Keep those two lookups separate.
			uint sceneWidth;
			uint sceneHeight;
			opaqueSceneColor.GetDimensions(sceneWidth, sceneHeight);
			const float2 sceneSize = max(float2(sceneWidth, sceneHeight), 1.0);
			const float2 screenUv = input.position.xy / sceneSize;
			const float3 viewSurface = mul(float4(input.worldPosition, 1.0), view).xyz;
			const float3 viewNormal = normalize(mul(opticalNormal, (float3x3)view));
			const float3 viewIncident = normalize(viewSurface);
			float3 viewRefracted = refract(viewIncident, viewNormal, eta);
			const float glassPattern = sampleAnimatedTextureAlpha(
				input.material, materialUv, properties);

			const float opaqueDepth = opaqueSceneDepth.SampleLevel(
				refractionSampler, screenUv, 0.0).r;
			const float refractedTravel = transmissionRayTravel(
				viewSurface, viewRefracted, opaqueDepth);
			const float3 refractedViewPosition = viewSurface + viewRefracted * refractedTravel;
			const float4 refractedClip = mul(float4(refractedViewPosition, 1.0), projection);
			const float2 refractedNdc = refractedClip.xy / max(refractedClip.w, 0.0001);
			const float2 rawRefractedUv = float2(
				refractedNdc.x * 0.5 + 0.5,
				-refractedNdc.y * 0.5 + 0.5);
			const float2 rawRefractionOffset = rawRefractedUv - screenUv;
			float2 refractedUv = screenUv + rawRefractionOffset *
				min(1.0, 0.018 / max(length(rawRefractionOffset), 0.0001));
			refractedUv = clamp(refractedUv, 0.002, 0.998);
			const float refractedSampleDepth = opaqueSceneDepth.SampleLevel(
				refractionSampler, refractedUv, 0.0).r;
			const float refractedSampleDepthDenominator =
				refractedSampleDepth - projection._33;
			const float refractedSampleViewZ = projection._43 /
				(abs(refractedSampleDepthDenominator) > 0.000001
					? refractedSampleDepthDenominator : -0.000001);
			const bool validGlassTransmission =
				refractedSampleDepth >= 0.99999 ||
				refractedSampleViewZ > viewSurface.z + 0.02;
			const float2 transmissionUv = validGlassTransmission ? refractedUv : screenUv;
			const float3 sceneTransmission = opaqueSceneColor.SampleLevel(
				refractionSampler, transmissionUv, 0.0).rgb;
			const float3 clearGlassTransmission = sceneTransmission * refractionTint;
			// Keep most of the dyed transmission; only a little frosted glass pattern.
			refractedColor = lerp(
				clearGlassTransmission, refractedColor,
				saturate(glassPattern * 0.35));
			const float backgroundLuminance = dot(
				sceneTransmission, float3(0.2126, 0.7152, 0.0722));
			const float backgroundDarkness = 1.0 -
				smoothstep(0.05, 0.48, backgroundLuminance);
			glassReflectionContrast = 1.08 + backgroundDarkness * 0.35;

			const float3 viewReflected = normalize(mul(reflectedDirection, (float3x3)view));
			float2 reflectedUv = 0.0;
			float reflectionConfidence = 0.0;
			if (traceScreenReflection(
				viewSurface, viewReflected, sceneSize,
				reflectedUv, reflectionConfidence, 24u, 0.62))
			{
				const float hitDepth = opaqueSceneDepth.SampleLevel(
					refractionSampler, reflectedUv, 0.0).r;
				const float3 hitWorld = reconstructWorldPosition(reflectedUv, hitDepth);
				const float reflectiveSide = dot(
					hitWorld - input.worldPosition, opticalNormal);
				// Keep the image on the camera side of the pane. A screen-space
				// march that crosses the glass samples the transmitted scene.
				if (reflectiveSide > 0.08 &&
					distance(reflectedUv, screenUv) > 0.004)
				{
					const float2 blurStep = 0.90 / sceneSize;
					const float2 blurPattern[9] = {
						float2(0,0), float2(1,0), float2(-1,0), float2(0,1), float2(0,-1),
						float2(0.707,0.707), float2(-0.707,0.707),
						float2(0.707,-0.707), float2(-0.707,-0.707)
					};
					float3 blurredReflection = opaqueSceneColor.SampleLevel(
						refractionSampler, reflectedUv, 0.0).rgb * 4.0;
					[unroll] for (uint tap = 1u; tap < 9u; ++tap)
						blurredReflection += opaqueSceneColor.SampleLevel(
							refractionSampler,
							clamp(reflectedUv + blurPattern[tap] * blurStep, 0.002, 0.998),
							0.0).rgb;
					// Replace the voxel fallback instead of mixing a second copy.
					reflectionColor = blurredReflection / 12.0;
					glassSsrConfidence = reflectionConfidence *
						saturate(reflectiveSide * 3.0);
				}
			}
			reflectionColor *= 1.08 + backgroundDarkness * 0.22;
		}
		if (isWater)
		{
			// Looking straight down has almost no Fresnel; a hard 14% sheen plus
			// translucent blending over the already-refracted scene read as a
			// second water sheet. Keep the sheen for grazing angles only.
			const float nadir = saturate((viewCosine - 0.55) / 0.40);
			const float reflectionOverlay = lerp(
				lerp(0.02, 0.14, 1.0 - nadir),
				0.78,
				reflectionWeight);
			finalColor = lerp(refractedColor, reflectionColor, reflectionOverlay);
			// Scene transmission already lives in refractedColor. Opaque coverage
			// stops the fixed-function blend from compositing that image twice.
			alpha = 1.0;
		}
		else
		{
			// Glass layers a Fresnel reflection over transmission. Face-on panes
			// stay mostly clear; grazing angles and a confident SSR hit raise the
			// overlay so nearby objects read in the glass without a milky wash.
			const float reflectionOverlay = saturate(
				(lerp(0.07, 0.70, reflectionWeight) + glassSsrConfidence * 0.20) *
				glassReflectionContrast);
			finalColor = refractedColor * (1.0 - reflectionOverlay * 0.18) +
				reflectionColor * reflectionOverlay;
			// This color already contains the captured scene transmission. Give it
			// near-opaque coverage so fixed-function alpha blending does not apply a
			// second strong transmission pass and wash the reflection back out.
			alpha = max(alpha, lerp(0.88, 0.96, reflectionOverlay));
		}
    }

    // Submerged volume fog only for geometry that is actually under the free
	// surface. Air-side terrain and the exit dielectric must stay clear so
	// looking out of the water can see shore and sky.
    if (waterTime < 0.0)
    {
		const bool waterInterface = isWater;
		const bool lookingOut = waterInterface && dot(N, V) < 0.0;
		// Prefer the tracked free-surface height; fall back near the camera if
		// no water plane was found this frame.
		const bool hasFreeSurface =
			planarReflectionEnabled > 0.5 || abs(planarReflectionPlaneHeight) > 0.5;
		const float freeSurface = hasFreeSurface
			? planarReflectionPlaneHeight
			: cameraPosition.y + 2.0;
		const bool belowSurface = input.worldPosition.y < freeSurface - 0.02;
		if (lookingOut)
		{
			// The water underside already holds refraction / TIR / diffraction.
		}
		else if (belowSurface || waterInterface)
		{
			const float waterDistance = length(cameraPosition - input.worldPosition);
			const float towardSurface = saturate(
				(freeSurface - cameraPosition.y) / max(waterDistance, 0.001));
			const float3 baseExtinction = float3(0.045, 0.022, 0.012);
			const float3 extinction = baseExtinction *
				lerp(1.35, 0.28, towardSurface * towardSurface);
			const float3 volumeTransmission = exp(-extinction * waterDistance);
			const float3 waterFog = float3(0.02, 0.14, 0.20);
			const float3 viewRay = normalize(input.worldPosition - cameraPosition);
			const float depthBoost = 0.55 + 1.15 * (1.0 - volumeTransmission.g);
			const float towardBoost = saturate(1.0 - towardSurface * 0.20);
			// 丁达尔光束: sun/moon shafts through the water volume.
			const float3 sunShaft = tyndallInScatter(
				viewRay, sunDirection, sunColor, sunIntensity,
				input.worldPosition, waterDistance, 0.045) *
				depthBoost * towardBoost * 1.85 * celestialVisibility;
			const float3 moonShaft = tyndallInScatter(
				viewRay, moonDirection, moonColor, moonIntensity * 0.75,
				input.worldPosition, waterDistance, 0.030) *
				depthBoost * towardBoost * 0.90 * celestialVisibility;
			float3 absorbedColor = finalColor * volumeTransmission +
				waterFog * (1.0 - volumeTransmission) +
				(sunShaft + moonShaft) * float3(0.78, 0.94, 1.05);
			const float distanceVisibility = 1.0 - smoothstep(48.0, 110.0, waterDistance) *
				(1.0 - towardSurface * 0.65);
			finalColor = lerp(waterFog, absorbedColor, distanceVisibility);
		}
    }
    else
    {
        // Mild aerial Tyndall / crepuscular shafts when looking toward the sun.
        const float airDistance = length(cameraPosition - input.worldPosition);
        if (airDistance > 12.0 && sunIntensity > 0.05)
        {
            const float3 viewRay = normalize(input.worldPosition - cameraPosition);
            const float haze = smoothstep(12.0, 140.0, airDistance) *
                saturate(sunIntensity);
            finalColor += tyndallInScatter(
                viewRay, sunDirection, sunColor, sunIntensity,
                input.worldPosition, airDistance, 0.008) * haze * 0.22 *
                celestialVisibility;
        }
    }
    return float4(finalColor, alpha);
}
