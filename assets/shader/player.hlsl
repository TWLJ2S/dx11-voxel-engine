struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint bone : MATID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float3 modelPosition : TEXCOORD2;
    nointerpolation uint boneId : BONEID;
    nointerpolation uint firstPerson : FIRSTPERSON;
    nointerpolation float visibility : VISIBILITY;
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

cbuffer PlayerBoneBuffer : register(b6)
{
    matrix boneTransforms[6];
    uint meshPart;
    uint3 bonePadding;
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
	float daylightPadding;
};

VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    uint bone = meshPart == 1u ? 1u : input.bone;
    float4 animatedPosition = meshPart == 2u
        ? float4(input.position, 1.0)
        : mul(float4(input.position, 1.0), boneTransforms[bone]);
    float4 worldPosition = mul(animatedPosition, world);
    output.position = mul(mul(worldPosition, view), projection);
    output.worldPosition = worldPosition.xyz;
    output.modelPosition = animatedPosition.xyz;
    output.boneId = bone;
    output.firstPerson = meshPart == 2u ? 1u : 0u;
    float3 animatedNormal = meshPart == 2u ? input.normal : mul(input.normal, (float3x3)boneTransforms[bone]);
    output.normal = normalize(mul(animatedNormal, (float3x3)world));
    output.uv = input.uv;
    bool firstPersonNeckCap = meshPart == 3u && bone == 0u &&
        input.normal.y > 0.5 && input.position.y >= 1.49;
    output.visibility = firstPersonNeckCap ? 0.0 : 1.0;
    return output;
}

Texture2D playerSkin : register(t0);
SamplerState skinSampler : register(s0);
Texture2DArray<float> celestialShadowMap : register(t6);
SamplerComparisonState celestialShadowSampler : register(s2);

cbuffer CelestialShadowBuffer : register(b8)
{
	matrix celestialViewProjection[3];
	float4 cascadeSplits;
	float2 celestialShadowTexelSize;
	float celestialShadowEnabled;
	float celestialShadowPadding;
	float4 cascadeTexelWorld;
};

float celestialVisibility(float3 worldPosition, float3 normal)
{
	if (celestialShadowEnabled < 0.5) return 1.0;
	float2 planar = worldPosition.xz - cameraPosition.xz;
	float distance = length(planar);
	uint cascade = 2u;
	if (distance < cascadeSplits.x) cascade = 0u;
	else if (distance < cascadeSplits.y) cascade = 1u;
	float3 lightDir = sunIntensity >= moonIntensity ? sunDirection : moonDirection;
	float3 samplePosition = worldPosition + normal * 0.03 + lightDir * 0.015;
	float4 projected = mul(float4(samplePosition, 1.0), celestialViewProjection[cascade]);
	float3 ndc = projected.xyz / max(projected.w, 0.0001);
	float2 uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
	if (any(uv < 0.001) || any(uv > 0.999) || ndc.z <= 0.0 || ndc.z >= 1.0) {
		if (cascade >= 2u) return 0.0;
		cascade += 1u;
		projected = mul(float4(samplePosition, 1.0), celestialViewProjection[cascade]);
		ndc = projected.xyz / max(projected.w, 0.0001);
		uv = float2(ndc.x * 0.5 + 0.5, -ndc.y * 0.5 + 0.5);
		if (any(uv < 0.001) || any(uv > 0.999) || ndc.z <= 0.0 || ndc.z >= 1.0)
			return 0.0;
	}
	float receiverZ = ndc.z - (cascade == 0u ? 0.0014 : 0.0007);
	const float2 poisson[8] = {
		float2(-0.326, -0.406), float2(-0.840, -0.074),
		float2(-0.696,  0.457), float2(-0.203,  0.621),
		float2( 0.271,  0.696), float2( 0.715,  0.458),
		float2( 0.869, -0.118), float2( 0.473, -0.542)
	};
	const float angle = frac(sin(dot(floor(worldPosition.xz * 4.0),
		float2(12.9898, 78.233))) * 43758.5453) * 6.28318531;
	const float2 rotation = float2(cos(angle), sin(angle));
	const float radius = 3.75 * (1.0 + 0.18 * (float)cascade);
	float visibility = celestialShadowMap.SampleCmpLevelZero(
		celestialShadowSampler, float3(uv, cascade), receiverZ);
	[unroll] for (uint tap = 0u; tap < 8u; ++tap)
	{
		const float2 offset = float2(
			poisson[tap].x * rotation.x - poisson[tap].y * rotation.y,
			poisson[tap].x * rotation.y + poisson[tap].y * rotation.x);
		visibility += celestialShadowMap.SampleCmpLevelZero(
			celestialShadowSampler,
			float3(uv + offset * celestialShadowTexelSize * radius, cascade),
			receiverZ);
	}
	return visibility / 9.0;
}

float playerHash(float2 value)
{
	return frac(sin(dot(value, float2(127.1, 311.7))) * 43758.5453);
}

float playerNoise(float2 value)
{
	float2 cell = floor(value);
	float2 blend = frac(value);
	blend = blend * blend * (3.0 - 2.0 * blend);
	return lerp(
		lerp(playerHash(cell), playerHash(cell + float2(1, 0)), blend.x),
		lerp(playerHash(cell + float2(0, 1)), playerHash(cell + 1.0), blend.x),
		blend.y);
}

float playerCloudVisibility(float3 worldPosition)
{
	if (sunDirection.y <= 0.035 || sunIntensity <= 0.001) return 1.0;
	float2 cloudPoint = worldPosition.xz + sunDirection.xz *
		((135.0 - worldPosition.y) / max(sunDirection.y, 0.035));
	float2 p = (cloudPoint + float2(abs(waterTime) * 2.15, abs(waterTime) * 0.72)) / 420.0;
	float2 baseP = p;
	float value = 0.0, weight = 0.52;
	[unroll] for (uint octave = 0u; octave < 4u; ++octave) {
		value += playerNoise(p) * weight;
		p = mul(p, float2x2(1.62, 1.18, -1.18, 1.62)) + 17.7;
		weight *= 0.49;
	}
	float threshold = lerp(0.665, 0.405, saturate(cloudCoverage));
	float thicknessVariation = playerNoise(baseP * 0.43 - 19.2);
	float opacity = smoothstep(threshold - 0.055, threshold + 0.13,
		value - (1.0 - cloudDensity) * 0.08);
	opacity *= lerp(0.28, 1.0, thicknessVariation * thicknessVariation);
	return 1.0 - opacity * saturate(cloudShadowStrength);
}

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
	bands *= 0.40 + 0.60 * playerNoise(shaftUv * 1.65 + 2.7);
	bands *= 0.55 + 0.45 * playerNoise(shaftUv * 0.55 - 1.3);
	bands = smoothstep(0.22, 0.78, bands);
	const float optical = saturate(1.0 - exp(-distance * densityScale));
	return lightColor * lightIntensity * phase * bands * optical;
}

float4 pixelMain(VSOutput input) : SV_TARGET
{
    clip(input.visibility - 0.5);
	float2 distortedUv = input.uv;
	if (waterTime < 0.0)
	{
		const float motion = saturate(length(waterMotion.xyz) * 0.22);
		const float phase = dot(input.worldPosition, float3(0.71, 0.43, 0.57)) + abs(waterTime) * 2.4;
		distortedUv += float2(sin(phase), cos(phase * 1.31)) * (0.0015 + motion * 0.0060);
	}
	float4 albedo = playerSkin.Sample(skinSampler, distortedUv);
    clip(albedo.a - (1.0 / 255.0));

	if (input.firstPerson != 0u)
	{
		float3 n = normalize(input.normal);
		float wrap = saturate(dot(n, normalize(float3(0.28, 0.82, 0.50))) * 0.55 + 0.50);
		return float4(albedo.rgb * wrap, albedo.a);
	}

    float3 normal = normalize(input.normal);
    const float diffuseSun = max(dot(normal, sunDirection), 0.0) * sunIntensity;
	const float diffuseMoon = max(dot(normal, moonDirection), 0.0) * moonIntensity;
    const float daylightAmount = saturate((ambientIntensity - 0.055) / 0.48);
    const float3 ambientColor = lerp(
        float3(0.55, 0.62, 0.92),
        float3(1.00, 0.97, 0.88),
        daylightAmount) * ambientIntensity;
	const float directVisibility = lerp(
		0.24, 1.0, celestialVisibility(input.worldPosition, normal));
	float3 finalColor = albedo.rgb * (ambientColor * lerp(0.38, 1.0, directVisibility) +
		(sunColor * diffuseSun * playerCloudVisibility(input.worldPosition) +
			moonColor * diffuseMoon) * directVisibility);
	if (waterTime < 0.0)
	{
		// Only fog the player while the body is under the free surface.
		const float freeSurface = cameraPosition.y + 1.5;
		if (input.worldPosition.y < freeSurface)
		{
			const float distance = length(cameraPosition - input.worldPosition);
			const float towardSurface = saturate(
				(freeSurface - cameraPosition.y) / max(distance, 0.001));
			const float3 fogColor = float3(0.02, 0.14, 0.20);
			const float3 extinction = float3(0.045, 0.022, 0.012) *
				lerp(1.35, 0.28, towardSurface * towardSurface);
			const float3 transmission = exp(-extinction * distance);
			const float3 viewRay = normalize(input.worldPosition - cameraPosition);
			const float depthBoost = 0.55 + 1.15 * (1.0 - transmission.g);
			const float3 sunShaft = tyndallInScatter(
				viewRay, sunDirection, sunColor, sunIntensity,
				input.worldPosition, distance, 0.045) * depthBoost * 1.85;
			const float3 absorbed = finalColor * transmission + fogColor * (1.0 - transmission) +
				sunShaft * float3(0.78, 0.94, 1.05);
			finalColor = lerp(fogColor, absorbed,
				1.0 - smoothstep(48.0, 110.0, distance) * (1.0 - towardSurface * 0.65));
		}
	}
    return float4(finalColor, albedo.a);
}
