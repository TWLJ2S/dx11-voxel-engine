struct VSInput {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    uint bone : BONEID;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};

cbuffer ObjectBuffer : register(b0) {
    matrix world;
    uint textured;
    float hurtAmount;
    float2 objectPadding;
};
cbuffer CameraBuffer : register(b1) {
    matrix view;
    matrix projection;
    float3 cameraPosition;
    float waterTime;
    float4 waterMotion;
    matrix viewProjection;
};
cbuffer EntityBones : register(b6) { matrix bones[8]; };
cbuffer DaylightBuffer : register(b7) {
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
Texture2D entityTexture : register(t0);
SamplerState entitySampler : register(s0);

VSOutput vertexMain(VSInput input) {
    VSOutput output;
    float4 animated = mul(float4(input.position, 1.0), bones[min(input.bone, 7u)]);
    float4 worldPosition = mul(animated, world);
    output.position = mul(mul(worldPosition, view), projection);
    output.normal = normalize(mul(mul(input.normal, (float3x3)bones[min(input.bone, 7u)]), (float3x3)world));
    output.color = input.color;
    output.uv = input.uv;
    output.worldPosition = worldPosition.xyz;
    return output;
}

float entityCloudHash(float2 p) {
    p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y);
}
float entityCloudNoise(float2 p) {
    float2 c = floor(p), f = frac(p); f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(entityCloudHash(c), entityCloudHash(c + float2(1,0)), f.x),
        lerp(entityCloudHash(c + float2(0,1)), entityCloudHash(c + 1.0), f.x), f.y);
}
float entityCloudVisibility(float3 worldPosition) {
    if (sunDirection.y <= 0.035 || sunIntensity <= 0.001) return 1.0;
    float2 cloudPoint = worldPosition.xz + sunDirection.xz * ((135.0 - worldPosition.y) / max(sunDirection.y, 0.035));
    float2 p = (cloudPoint + float2(cloudTime * 2.15, cloudTime * 0.72)) / 420.0;
    float2 baseP = p;
    float value = 0.0, weight = 0.52;
    [unroll] for (uint octave = 0u; octave < 4u; ++octave) {
        value += entityCloudNoise(p) * weight;
        p = mul(p, float2x2(1.62, 1.18, -1.18, 1.62)) + 17.7; weight *= 0.49;
    }
    float threshold = lerp(0.665, 0.405, saturate(cloudCoverage));
    float thicknessVariation = entityCloudNoise(baseP * 0.43 - 19.2);
    float opacity = smoothstep(threshold - 0.055, threshold + 0.13, value - (1.0 - cloudDensity) * 0.08);
    opacity *= lerp(0.28, 1.0, thicknessVariation * thicknessVariation);
    float phase = frac(cloudTime / 110.0 + entityCloudNoise(baseP * 0.31 + float2(13.7, -8.4)));
    float moisture = smoothstep(0.12, 0.34, phase) * (1.0 - smoothstep(0.72, 0.96, phase));
    opacity *= lerp(0.42, 1.22, moisture);
    return 1.0 - opacity * saturate(cloudShadowStrength);
}

float4 pixelMain(VSOutput input) : SV_TARGET {
    float3 n = normalize(input.normal);
    float3 keyDirection = sunIntensity >= moonIntensity ? sunDirection : moonDirection;
    float keyStrength = max(sunIntensity, moonIntensity);
    float3 keyColor = sunIntensity >= moonIntensity ? sunColor : moonColor;
    float diffuse = max(dot(n, keyDirection), 0.0);
    float hemi = n.y * 0.5 + 0.5;
    float cloudVisibility = sunIntensity >= moonIntensity ? entityCloudVisibility(input.worldPosition) : 1.0;
    float3 light = ambientIntensity * lerp(0.70, 1.12, hemi) + keyColor * diffuse * keyStrength * cloudVisibility;
    float4 albedo = input.color;
    if (textured != 0)
        albedo *= entityTexture.Sample(entitySampler, input.uv);
    clip(albedo.a - 0.01);
    albedo.rgb = lerp(albedo.rgb, float3(1.0, 0.13, 0.13),
        saturate(hurtAmount) * 0.68);
    return float4(albedo.rgb * max(light, 0.12), albedo.a);
}
