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

cbuffer ObjectBuffer : register(b0)
{
    matrix world;
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
    const float shoreMask = saturate((oceanWeightAt(xz) - 0.12) / 0.55);
    return normalize(lerp(float2(0.328, 0.212), towardShore, shoreMask));
}

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.position, 1.0), world);
    const bool upperWaterVertex = input.normal.y > 0.15 ||
        (abs(input.normal.y) < 0.15 && input.uv.y < 0.001);
    // Stacked water is meshed at an exact integer block ceiling. Only an
    // exposed surface is inset/sloped to a fractional height and may wave.
    const bool exposedWaterSurface = abs(worldPos.y - round(worldPos.y)) > 0.001;
    const bool waterSurface = waterMotion.w < 64.0 &&
        input.material == (uint)(waterMotion.w + 0.5) && upperWaterVertex &&
		exposedWaterSurface;
    const float2 xz = worldPos.xz;
    const float2 shore = shoreDirection(xz);
    if (waterSurface)
    {
        const float time = abs(waterTime);
        const float2 along = float2(-shore.y, shore.x);
        // Independent phase/speed per octave so patches do not pulse in lockstep.
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
        const float waveA = sin(dot(xz, shore) * sA + time * 1.18 * tA + pA);
        const float waveB = sin(dot(xz, along) * sB - time * 0.94 * tB + pB);
        const float waveC = sin(dot(xz, normalize(shore * 0.65 + along * 0.35)) * sC + time * 0.68 * tC + pC);
        const float chop = sin(dot(xz, normalize(shore * 0.80 + along * -0.45)) *
            lerp(0.36, 0.58, waterNoise(xz * 0.076)) + time * 2.05 + pD);
        // Peak displacement remains below the 2-pixel source-surface inset.
        worldPos.y += (waveA * 0.72 + chop * 0.28) * 0.058 + waveB * 0.034 + waveC * 0.021;
    }

    output.position = mul(mul(worldPos, view), projection);
    output.worldPosition = worldPos.xyz;

    output.normal = normalize(mul(input.normal, (float3x3) world));
    output.uv = input.uv;
    output.material = input.material;
    output.aoCorners = input.ao;
    output.opacity = input.opacity;
	output.reflectionClipDistance = reflectionClipEnabled > 0.5
		? worldPos.y - reflectionClipHeight + 0.01
		: 1.0;
    output.climate = worldClimate(worldPos.xz);
    output.shoreDir = shore;

    return output;
}
