struct VisibleFace
{
    uint packedPosition;
    uint packedSize;
    uint material;
    uint ao;
    float opacity;
};

StructuredBuffer<VisibleFace> chunkFaces : register(t66);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    nointerpolation uint material : MATID;
    nointerpolation uint aoCorners : AO;
    float opacity : OPACITY;
	float reflectionClipDistance : SV_ClipDistance0;
    float2 climate : CLIMATE;
    float2 shoreDir : SHORE;
};

cbuffer ObjectBuffer : register(b0) { matrix world; };
cbuffer CameraBuffer : register(b1)
{
    matrix view;
    matrix projection;
    float3 cameraPosition;
    float waterTime;
    float4 waterMotion;
    matrix viewProjection;
};

cbuffer PlanarClipBuffer : register(b11)
{
	float reflectionClipHeight;
	float reflectionClipEnabled;
	float2 reflectionClipPadding;
};

cbuffer VoxelLightBuffer : register(b4)
{
    uint voxelLightTableMask;
    uint voxelLightingEnabled;
    uint voxelLightingQuality;
    uint voxelWaterMaterial;
    uint worldSeed;
    float3 voxelLightPadding;
};

// Climate is evaluated with the generator's own value noise so surface colour
// tracks the biome that was actually placed here. Doing it per vertex keeps the
// cost negligible: the fields change over thousands of blocks, far slower than
// any single greedy quad.
uint climateHashValue(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

uint climateHash2(uint seed, int x, int z)
{
    return climateHashValue(seed ^ climateHashValue(asuint(x) + 0x9e3779b9u) ^
        climateHashValue(asuint(z) + 0x85ebca6bu));
}

float climateRandom(uint value)
{
    return (float(value & 0x00ffffffu) / 8388607.5) - 1.0;
}

float climateNoise(float2 p, uint seed)
{
    int2 cell = (int2)floor(p);
    float2 f = frac(p);
    float2 s = f * f * (3.0 - 2.0 * f);
    float a = climateRandom(climateHash2(seed, cell.x, cell.y));
    float b = climateRandom(climateHash2(seed, cell.x + 1, cell.y));
    float c = climateRandom(climateHash2(seed, cell.x, cell.y + 1));
    float d = climateRandom(climateHash2(seed, cell.x + 1, cell.y + 1));
    return lerp(lerp(a, b, s.x), lerp(c, d, s.x), s.y);
}

float climateFractalAniso(float2 world, float frequencyX, float frequencyZ, uint octaves, uint seed)
{
    float2 p = float2(world.x * frequencyX, world.y * frequencyZ);
    float value = 0.0, amplitude = 1.0, total = 0.0;
    [loop] for (uint octave = 0; octave < octaves; ++octave) {
        value += climateNoise(p, seed + octave * 0x9e3779b9u) * amplitude;
        total += amplitude;
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value / total;
}

float climateFractal(float2 world, float frequency, uint octaves, uint seed)
{
    return climateFractalAniso(world, frequency, frequency, octaves, seed);
}

// Mirrors generateColumns in gpuTerrainGeneration.hlsl, then remaps onto the
// 0..1 range using the thresholds the biome classifier uses, so colour and
// biome change in the same place.
float2 climateWarp(float2 world)
{
    return world + float2(
        climateFractalAniso(world, 0.00085, 0.00135, 3, worldSeed + 40u),
        climateFractalAniso(world, 0.00115, 0.00075, 3, worldSeed + 71u)) * 32.0 +
        float2(
            climateFractalAniso(world, 0.0031, 0.0022, 2, worldSeed + 41u),
            climateFractalAniso(world, 0.0026, 0.0034, 2, worldSeed + 72u)) * 12.0;
}

float2 worldClimate(float2 world)
{
    const float2 climate = climateWarp(world);
    const float temperature = climateFractalAniso(climate, 0.00016, 0.00022, 4, worldSeed + 400u);
    const float rainfall = climateFractalAniso(climate, 0.00017, 0.00020, 4, worldSeed + 500u);
    return float2(
        saturate((temperature + 0.28) / 0.76),
        saturate((rainfall + 0.24) / 0.64));
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

float oceanWeightAt(float2 world)
{
    const float continental = climateFractalAniso(climateWarp(world), 0.00018, 0.00026, 5, worldSeed + 100u);
    return 1.0 - saturate((continental + 0.55) / 0.47);
}

// Unit vector pointing toward shore (decreasing ocean weight).
float2 shoreDirection(float2 xz)
{
    const float delta = 6.0;
    const float oXP = oceanWeightAt(xz + float2(delta, 0));
    const float oXM = oceanWeightAt(xz - float2(delta, 0));
    const float oZP = oceanWeightAt(xz + float2(0, delta));
    const float oZM = oceanWeightAt(xz - float2(0, delta));
    float2 towardShore = float2(oXM - oXP, oZM - oZP);
    const float len = length(towardShore);
    if (len < 1e-4)
        return float2(0.328, 0.212);
    towardShore /= len;
    // Near open ocean keep a mild blend with a stable fallback heading.
    const float shoreMask = saturate((oceanWeightAt(xz) - 0.12) / 0.55);
    return normalize(lerp(float2(0.328, 0.212), towardShore, shoreMask));
}

void faceCorners(uint direction, float3 p, float width, float height, out float3 a, out float3 b, out float3 c, out float3 d, out float3 normal)
{
    if (direction == 0u) { a=p; b=p+float3(0,0,width); c=p+float3(0,height,width); d=p+float3(0,height,0); normal=float3(-1,0,0); }
    else if (direction == 1u) { a=p+float3(1,0,width); b=p+float3(1,0,0); c=p+float3(1,height,0); d=p+float3(1,height,width); normal=float3(1,0,0); }
    else if (direction == 2u) { a=p; b=p+float3(width,0,0); c=p+float3(width,0,height); d=p+float3(0,0,height); normal=float3(0,-1,0); }
    else if (direction == 3u) { a=p+float3(width,1,0); b=p+float3(0,1,0); c=p+float3(0,1,height); d=p+float3(width,1,height); normal=float3(0,1,0); }
    else if (direction == 4u) { a=p+float3(width,0,0); b=p; c=p+float3(0,height,0); d=p+float3(width,height,0); normal=float3(0,0,-1); }
    else { a=p+float3(0,0,1); b=p+float3(width,0,1); c=p+float3(width,height,1); d=p+float3(0,height,1); normal=float3(0,0,1); }
}

VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VisibleFace face = chunkFaces[instanceId];
    uint x = face.packedPosition & 15u;
    uint z = (face.packedPosition >> 4) & 15u;
    uint y = (face.packedPosition >> 8) & 511u;
    uint direction = (face.packedPosition >> 17) & 7u;
    float faceWidth = float((face.packedSize & 31u) + 1u);
    float faceHeight = float(((face.packedSize >> 5u) & 31u) + 1u);
    uint fluidLevel = (face.packedSize >> 10u) & 15u;
    uint neighborFluidLevel = (face.packedSize >> 14u) & 7u;

    float3 corners[4];
    float3 normal;
    faceCorners(direction, float3(x,y,z), faceWidth, faceHeight, corners[0], corners[1], corners[2], corners[3], normal);
    if (fluidLevel != 0u) {
        float h00 = float(y) + float((face.ao >> 8u) & 15u) / 8.0;
        float h10 = float(y) + float((face.ao >> 12u) & 15u) / 8.0;
        float h01 = float(y) + float((face.ao >> 16u) & 15u) / 8.0;
        float h11 = float(y) + float((face.ao >> 20u) & 15u) / 8.0;
        if (direction == 3u) {
            corners[0].y = h10; corners[1].y = h00;
            corners[2].y = h01; corners[3].y = h11;
        }
        else if (direction == 0u) { corners[2].y = h01; corners[3].y = h00; }
        else if (direction == 1u) { corners[2].y = h10; corners[3].y = h11; }
        else if (direction == 4u) { corners[2].y = h00; corners[3].y = h10; }
        else if (direction == 5u) { corners[2].y = h11; corners[3].y = h01; }
        if (direction == 3u)
            normal = normalize(cross(corners[1] - corners[0], corners[3] - corners[0]));
    }
    const uint cornerIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    uint corner = cornerIndices[vertexId];
    bool flipV = direction < 2u || direction >= 4u;
    const float2 standardUV[4] = { float2(0,0), float2(1,0), float2(1,1), float2(0,1) };
    const float2 flippedUV[4] = { float2(0,1), float2(1,1), float2(1,0), float2(0,0) };

    float4 worldPosition = mul(float4(corners[corner], 1.0), world);
    const bool upperWaterVertex = normal.y > 0.15 ||
        (abs(normal.y) < 0.15 && corner >= 2u);
    // A water cell with water above has integer-height upper vertices. Keep
    // those cells full-sized and still; only fractional exposed surfaces wave.
    const bool exposedWaterSurface = abs(worldPosition.y - round(worldPosition.y)) > 0.001;
    const bool waterSurface = waterMotion.w < 64.0 &&
        face.material == (uint)(waterMotion.w + 0.5) && upperWaterVertex &&
		exposedWaterSurface;
    const float2 xz = worldPosition.xz;
    const float2 shore = shoreDirection(xz);
    if (waterSurface)
    {
        const float time = abs(waterTime);
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
        // Primary swell rolls toward shore; secondary/chop run along the coast.
        const float waveA = sin(dot(xz, shore) * sA + time * 1.18 * tA + pA);
        const float waveB = sin(dot(xz, along) * sB - time * 0.94 * tB + pB);
        const float waveC = sin(dot(xz, normalize(shore * 0.65 + along * 0.35)) * sC + time * 0.68 * tC + pC);
        const float chop = sin(dot(xz, normalize(shore * 0.80 + along * -0.45)) *
            lerp(0.36, 0.58, waterNoise(xz * 0.076)) + time * 2.05 + pD);
        // Peak displacement remains below the 2-pixel source-surface inset.
        worldPosition.y += (waveA * 0.72 + chop * 0.28) * 0.058 + waveB * 0.034 + waveC * 0.021;
    }
    VSOutput output;
    output.position = mul(mul(worldPosition, view), projection);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(mul(normal, (float3x3)world));
    output.uv = (flipV ? flippedUV[corner] : standardUV[corner]) * float2(faceWidth, faceHeight);
    output.material = face.material;
    if (flipV) {
        uint a0 = face.ao & 3u;
        uint a1 = (face.ao >> 2) & 3u;
        uint a2 = (face.ao >> 4) & 3u;
        uint a3 = (face.ao >> 6) & 3u;
        output.aoCorners = a3 | (a2 << 2) | (a1 << 4) | (a0 << 6);
    }
    else output.aoCorners = face.ao;
    output.opacity = face.opacity;
	output.reflectionClipDistance = reflectionClipEnabled > 0.5
		? worldPosition.y - reflectionClipHeight + 0.01
		: 1.0;
    output.climate = worldClimate(worldPosition.xz);
    output.shoreDir = shore;
    return output;
}
