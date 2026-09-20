struct Light
{
    float3 position;
    float radius;
    float3 color;
    float intensity;
    float3 halfExtent;
    uint type;
};

StructuredBuffer<Light> lights : register(t64);
TextureCube<float> pointShadowMap : register(t65);
StructuredBuffer<float4> materialEmissions : register(t66);
StructuredBuffer<uint> lightOccluders : register(t67);
struct VoxelLight
{
    int3 position;
    uint packedColor;
};
StructuredBuffer<VoxelLight> voxelLights : register(t68);

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
    float cameraPadding;
};

Texture2D blockTextures[64] : register(t0);
SamplerState sampler0 : register(s0);
SamplerComparisonState shadowSampler : register(s1);
SamplerComparisonState hardShadowSampler : register(s2);

float3 sampleBlockTexture(uint material, float2 uv)
{
	switch (material)
	{
		case 0: return blockTextures[0].Sample(sampler0, uv).rgb;
		case 1: return blockTextures[1].Sample(sampler0, uv).rgb;
		case 2: return blockTextures[2].Sample(sampler0, uv).rgb;
		case 3: return blockTextures[3].Sample(sampler0, uv).rgb;
		case 4: return blockTextures[4].Sample(sampler0, uv).rgb;
		case 5: return blockTextures[5].Sample(sampler0, uv).rgb;
		case 6: return blockTextures[6].Sample(sampler0, uv).rgb;
		case 7: return blockTextures[7].Sample(sampler0, uv).rgb;
		case 8: return blockTextures[8].Sample(sampler0, uv).rgb;
		case 9: return blockTextures[9].Sample(sampler0, uv).rgb;
		case 10: return blockTextures[10].Sample(sampler0, uv).rgb;
		case 11: return blockTextures[11].Sample(sampler0, uv).rgb;
		case 12: return blockTextures[12].Sample(sampler0, uv).rgb;
		case 13: return blockTextures[13].Sample(sampler0, uv).rgb;
		case 14: return blockTextures[14].Sample(sampler0, uv).rgb;
		case 15: return blockTextures[15].Sample(sampler0, uv).rgb;
		case 16: return blockTextures[16].Sample(sampler0, uv).rgb;
		case 17: return blockTextures[17].Sample(sampler0, uv).rgb;
		case 18: return blockTextures[18].Sample(sampler0, uv).rgb;
		case 19: return blockTextures[19].Sample(sampler0, uv).rgb;
		case 20: return blockTextures[20].Sample(sampler0, uv).rgb;
		case 21: return blockTextures[21].Sample(sampler0, uv).rgb;
		case 22: return blockTextures[22].Sample(sampler0, uv).rgb;
		case 23: return blockTextures[23].Sample(sampler0, uv).rgb;
		case 24: return blockTextures[24].Sample(sampler0, uv).rgb;
		case 25: return blockTextures[25].Sample(sampler0, uv).rgb;
		case 26: return blockTextures[26].Sample(sampler0, uv).rgb;
		case 27: return blockTextures[27].Sample(sampler0, uv).rgb;
		case 28: return blockTextures[28].Sample(sampler0, uv).rgb;
		case 29: return blockTextures[29].Sample(sampler0, uv).rgb;
		case 30: return blockTextures[30].Sample(sampler0, uv).rgb;
		case 31: return blockTextures[31].Sample(sampler0, uv).rgb;
		case 32: return blockTextures[32].Sample(sampler0, uv).rgb;
		case 33: return blockTextures[33].Sample(sampler0, uv).rgb;
		case 34: return blockTextures[34].Sample(sampler0, uv).rgb;
		case 35: return blockTextures[35].Sample(sampler0, uv).rgb;
		case 36: return blockTextures[36].Sample(sampler0, uv).rgb;
		case 37: return blockTextures[37].Sample(sampler0, uv).rgb;
		case 38: return blockTextures[38].Sample(sampler0, uv).rgb;
		case 39: return blockTextures[39].Sample(sampler0, uv).rgb;
		case 40: return blockTextures[40].Sample(sampler0, uv).rgb;
		case 41: return blockTextures[41].Sample(sampler0, uv).rgb;
		case 42: return blockTextures[42].Sample(sampler0, uv).rgb;
		case 43: return blockTextures[43].Sample(sampler0, uv).rgb;
		case 44: return blockTextures[44].Sample(sampler0, uv).rgb;
		case 45: return blockTextures[45].Sample(sampler0, uv).rgb;
		case 46: return blockTextures[46].Sample(sampler0, uv).rgb;
		case 47: return blockTextures[47].Sample(sampler0, uv).rgb;
		case 48: return blockTextures[48].Sample(sampler0, uv).rgb;
		case 49: return blockTextures[49].Sample(sampler0, uv).rgb;
		case 50: return blockTextures[50].Sample(sampler0, uv).rgb;
		case 51: return blockTextures[51].Sample(sampler0, uv).rgb;
		case 52: return blockTextures[52].Sample(sampler0, uv).rgb;
		case 53: return blockTextures[53].Sample(sampler0, uv).rgb;
		case 54: return blockTextures[54].Sample(sampler0, uv).rgb;
		case 55: return blockTextures[55].Sample(sampler0, uv).rgb;
		case 56: return blockTextures[56].Sample(sampler0, uv).rgb;
		case 57: return blockTextures[57].Sample(sampler0, uv).rgb;
		case 58: return blockTextures[58].Sample(sampler0, uv).rgb;
		case 59: return blockTextures[59].Sample(sampler0, uv).rgb;
		case 60: return blockTextures[60].Sample(sampler0, uv).rgb;
		case 61: return blockTextures[61].Sample(sampler0, uv).rgb;
		case 62: return blockTextures[62].Sample(sampler0, uv).rgb;
		case 63: return blockTextures[63].Sample(sampler0, uv).rgb;
		default: return float3(1.0, 0.0, 1.0);
	}
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
};

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
    uint voxelLightPadding;
};

cbuffer ChunkLightBuffer : register(b5)
{
    uint chunkLightCount;
    uint3 chunkLightPadding;
    uint4 chunkLightIndices[16];
};

cbuffer DynamicShadowBuffer : register(b6)
{
    matrix dynamicShadowViewProjection;
    float3 dynamicShadowLightPosition;
    float dynamicShadowLightRadius;
	uint dynamicShadowEntityOnly;
	uint3 dynamicShadowPadding;
};

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
    if (lightingQuality == 0u)
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

float pointShadowVisibility(float3 worldPosition, float3 normal, Light light, uint lightIndex)
{
    // Radial depth is continuous at cubemap face boundaries. A small normal
    // offset and slope-aware world-space bias prevent self-shadowing without
    // opening the large gaps caused by the old fixed 0.6-block bias.
    float3 receiverPosition = worldPosition + normal * 0.01;
    float3 fromLight = receiverPosition - light.position;
    float distanceToLight = length(fromLight);
    float3 direction = fromLight / max(distanceToLight, 0.0001);

    float receiverFacing = saturate(dot(normal, -direction));
    float worldBias = lerp(0.045, 0.015, receiverFacing);
    float comparisonDepth = saturate((distanceToLight - worldBias) / light.radius);

    float visibility = pointShadowMap.SampleCmpLevelZero(shadowSampler, direction, comparisonDepth);
    float3 helper = abs(direction.y) < 0.99
        ? float3(0.0, 1.0, 0.0)
        : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(helper, direction));
    float3 bitangent = cross(direction, tangent);
	if (dynamicShadowEntityOnly == 0u)
	{
		// Blocks use their own point-comparison sampler: no linear comparison
		// filtering and no multi-tap PCF blur.
		return pointShadowMap.SampleCmpLevelZero(hardShadowSampler, direction, comparisonDepth);
	}

	// Animated player/entity silhouettes retain a small 5x5 PCF kernel, but use
	// a tighter radius than before so their edges are visibly sharper. Blocks
	// remain on the unfiltered single-sample path above.
	const float entityFilterStep = (2.0 / 1024.0) * 8.0;
	float3 entityDx = tangent * entityFilterStep;
	float3 entityDy = bitangent * entityFilterStep;
	[unroll]
	for (int y = -2; y <= 2; ++y)
	{
		[unroll]
		for (int x = -2; x <= 2; ++x)
		{
			if (x == 0 && y == 0) continue;
			visibility += pointShadowMap.SampleCmpLevelZero(
				shadowSampler,
				normalize(direction + entityDx * (float)x + entityDy * (float)y),
				comparisonDepth
			);
		}
	}
	return visibility / 25.0;
}

float dynamicEntityShadowVisibility(float3 worldPosition, float3 normal)
{
    float distanceToShadowLight = length(worldPosition - dynamicShadowLightPosition);
    if (dynamicShadowLightRadius <= 0.0 || distanceToShadowLight >= dynamicShadowLightRadius)
        return 1.0;

    Light shadowLight;
    shadowLight.position = dynamicShadowLightPosition;
    shadowLight.radius = dynamicShadowLightRadius;
    shadowLight.color = 1.0;
    shadowLight.intensity = 0.0;
    shadowLight.halfExtent = 0.0;
    shadowLight.type = 0u;
    return pointShadowVisibility(worldPosition, normal, shadowLight, 0u);
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

float3 evaluateLightSample(
    Light light,
    uint lightIndex,
    uint sampleIndex,
    uint meshSamplesPerFace,
    float3 surfacePosition,
    float3 surfaceNormal,
    float3 viewDirection,
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

    float visibility = voxelLightVisibility(surfacePosition, surfaceNormal, samplePosition, lightIndex);
    if (visibility < 0.0)
        visibility = pointShadowVisibility(surfacePosition, surfaceNormal, light, lightIndex);
    if (visibility <= 0.001) return 0.0;

    float normalizedDistance = distanceToLight / light.radius;
    float attenuation = saturate(1.0 - normalizedDistance * normalizedDistance * normalizedDistance);
    float3 diffuse = light.color * light.intensity * diffuseFactor;
    float3 halfwayDirection = normalize(lightDirection + viewDirection);
    float specular = pow(max(dot(surfaceNormal, halfwayDirection), 0.0), 16.0);
    return (diffuse + light.color * specular * 0.08) * attenuation * visibility * sampleWeight;
}

float4 main(PSInput input) : SV_TARGET
{
    float3 albedo = sampleBlockTexture(input.material, input.uv);
    float4 materialEmission = materialEmissions[input.material];
    float alpha = input.opacity;
    float3 N = normalize(input.normal);
    float3 V = normalize(cameraPosition - input.worldPosition);

    float ao = lerp(0.55, 1.0, sampleFaceAO(input.aoCorners, input.uv, N) / 3.0);
    float3 ambientLighting = float3(0.65, 0.65, 0.65);
    float3 emittedLighting = sampleVoxelLighting(input.worldPosition, N) * 0.55;
	// Dynamic casters are not represented in the cached voxel-light field.
	// Shadow only the emitted component so the selected real emitter controls
	// the silhouette without incorrectly darkening neutral world ambience.
	const float dynamicShadow = dynamicEntityShadowVisibility(input.worldPosition, N);
	emittedLighting *= lerp(0.20, 1.0, dynamicShadow);
    float3 lighting = (ambientLighting + emittedLighting) * ao;

    for (uint localLight = 0; localLight < chunkLightCount; ++localLight)
    {
        uint i = chunkLightIndex(localLight);
        Light l = lights[i];
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
                input.worldPosition, N, V, sampleWeight
            );
			totalSampleWeight += sampleWeight;
        }

		lighting += lightContribution / max(totalSampleWeight, 0.0001);
    }

    float3 emissive = materialEmission.rgb * materialEmission.a;
    return float4(albedo * saturate(lighting + emissive), alpha);
}
