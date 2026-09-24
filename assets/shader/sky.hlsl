struct SkyVertex
{
	float4 position : SV_POSITION;
	float2 ndc : TEXCOORD0;
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

SkyVertex vertexMain(uint vertexId : SV_VertexID)
{
	SkyVertex output;
	const float2 positions[3] = {
		float2(-1.0, -1.0), float2(-1.0, 3.0), float2(3.0, -1.0)
	};
	output.ndc = positions[vertexId];
	output.position = float4(output.ndc, 0.999999, 1.0);
	return output;
}

float starField(float3 ray)
{
	float3 cell = floor(ray * 620.0);
	float hash = frac(sin(dot(cell, float3(12.9898, 78.233, 39.425))) * 43758.5453);
	return hash > 0.9975 ? pow((hash - 0.9975) / 0.0025, 3.0) : 0.0;
}

float cloudHash(float2 p)
{
	p = frac(p * float2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return frac(p.x * p.y);
}

float cloudNoise(float2 p)
{
	float2 cell = floor(p);
	float2 local = frac(p);
	local = local * local * (3.0 - 2.0 * local);
	return lerp(lerp(cloudHash(cell), cloudHash(cell + float2(1, 0)), local.x),
		lerp(cloudHash(cell + float2(0, 1)), cloudHash(cell + 1.0), local.x), local.y);
}

float cloudFbm(float2 p)
{
	float value = 0.0;
	float weight = 0.52;
	[unroll] for (uint octave = 0u; octave < 5u; ++octave) {
		value += cloudNoise(p) * weight;
		p = mul(p, float2x2(1.62, 1.18, -1.18, 1.62)) + 17.7;
		weight *= 0.49;
	}
	return value;
}

float cloudMoisture(float2 worldXZ)
{
	float2 coordinate = (worldXZ + float2(cloudTime * 2.15, cloudTime * 0.72)) / 420.0;
	float phaseOffset = cloudNoise(coordinate * 0.31 + float2(13.7, -8.4));
	float phase = frac(cloudTime / 110.0 + phaseOffset);
	return smoothstep(0.12, 0.34, phase) * (1.0 - smoothstep(0.72, 0.96, phase));
}

float cloudShape(float2 worldXZ, float heightFraction)
{
	const float time = cloudTime;
	float2 wind = float2(time * 2.15, time * 0.72);
	float2 coordinate = (worldXZ + wind) / 420.0 + heightFraction * float2(0.031, -0.019);
	float broad = cloudFbm(coordinate);
	float detail = cloudFbm(coordinate * 2.75 + 31.4) * 0.22;
	float thicknessVariation = cloudNoise(coordinate * 0.43 - 19.2);
	float threshold = lerp(0.665, 0.405, saturate(cloudCoverage));
	float shape = saturate((broad + detail - threshold) * 5.2 * cloudDensity);
	// Large low-frequency variation creates both thin, fog-like veils and
	// optically dense cloud cores within the same weather front.
	shape *= lerp(0.34, 1.28, thicknessVariation * thicknessVariation);
	float vertical = smoothstep(0.0, 0.14, heightFraction) *
		smoothstep(1.0, 0.60, heightFraction);
	return saturate(shape * vertical);
}

float4 traceClouds(float3 ray, float daylight)
{
	if (ray.y <= 0.018) return 0.0;
	const float bottom = 112.0;
	const float top = 158.0;
	float nearDistance = max(0.0, (bottom - cameraPosition.y) / ray.y);
	float farDistance = max(nearDistance, (top - cameraPosition.y) / ray.y);
	if (nearDistance > 18000.0 || farDistance <= 0.0) return 0.0;
	farDistance = min(farDistance, 18000.0);
	float4 accumulated = 0.0;
	float3 sunDir = normalize(sunDirection);
	[unroll] for (uint sampleIndex = 0u; sampleIndex < 6u; ++sampleIndex) {
		float fraction = (sampleIndex + 0.5) / 6.0;
		float distance = lerp(nearDistance, farDistance, fraction);
		float3 samplePosition = cameraPosition + ray * distance;
		float heightFraction = saturate((samplePosition.y - bottom) / (top - bottom));
		float density = cloudShape(samplePosition.xz, heightFraction);
		float moisture = cloudMoisture(samplePosition.xz);
		float towardSun = cloudShape(samplePosition.xz + sunDir.xz * 32.0,
			saturate(heightFraction + sunDir.y * 0.10));
		float lightThrough = exp(-towardSun * lerp(0.75, 3.2, towardSun));
		float topLight = 0.42 + 0.50 * heightFraction;
		float3 dayCloud = lerp(float3(0.34, 0.38, 0.43), float3(1.02, 1.00, 0.94),
			saturate(lightThrough * 0.72 + topLight * 0.28));
		float3 nightCloud = float3(0.025, 0.033, 0.060) * (0.62 + lightThrough * 0.38);
		float3 sampleColor = lerp(nightCloud, dayCloud, daylight);
		// Charged clouds carry a dark water-heavy base. Once their rain phase has
		// passed they return toward a bright, thin white cloud.
		float3 wetCloud = lerp(float3(0.075, 0.090, 0.115), float3(0.30, 0.32, 0.34), daylight);
		sampleColor = lerp(sampleColor * 1.08, wetCloud, moisture * 0.82);
		float sunEdge = pow(saturate(dot(ray, sunDir)), 18.0) * lightThrough * daylight;
		sampleColor += sunColor * sunEdge * 0.38;
		// Beer-Lambert-style extinction keeps wispy edges translucent while a
		// dense stack reaches full opacity and can completely obscure the sun.
		float opticalDepth = density * lerp(0.10, 1.45, density) * cloudDensity *
			lerp(0.42, 1.22, moisture);
		float alpha = (1.0 - exp(-opticalDepth * 1.28)) * (1.0 - accumulated.a);
		accumulated.rgb += sampleColor * alpha;
		accumulated.a += alpha;
	}
	accumulated *= smoothstep(0.018, 0.095, ray.y);
	return accumulated;
}

float4 pixelMain(SkyVertex input) : SV_TARGET
{
	float3 viewRay = normalize(float3(
		input.ndc.x / projection._11,
		input.ndc.y / projection._22,
		1.0));
	float3 worldRay = normalize(mul(viewRay, transpose((float3x3)view)));

	const float3 sunDir = normalize(sunDirection);
	const float elevation = sunDir.y;
	// Warm dusk band around the horizon when the sun is low; cool daytime blue
	// once it clears the horizon.
	const float dusk = saturate(1.0 - abs(elevation) / 0.28);
	const float dawnWarmth = dusk * saturate(1.0 - elevation * 2.5);
	const float daylight = saturate((ambientIntensity - 0.055) / 0.48);
	const float horizon = pow(saturate(1.0 - abs(worldRay.y)), 2.4);

	float3 dayZenith = daylightSkyColor * lerp(0.55, 1.0, saturate(worldRay.y * 0.7 + 0.3));
	float3 nightZenith = float3(0.01, 0.015, 0.045);
	float3 zenith = lerp(nightZenith, dayZenith, daylight);
	zenith = lerp(zenith, float3(0.18, 0.22, 0.48), dawnWarmth * 0.40);

	float3 dayHorizon = float3(0.55, 0.70, 0.88);
	float3 nightHorizon = float3(0.012, 0.018, 0.050);
	float3 warmHorizon = float3(1.00, 0.42, 0.10);
	float3 horizonColor = lerp(nightHorizon, dayHorizon, daylight);
	horizonColor = lerp(horizonColor, warmHorizon, dawnWarmth);

	float3 color = lerp(zenith, horizonColor, horizon * lerp(0.55, 0.85, dawnWarmth));
	// Soft orange veil across the lower sky at sunrise/sunset.
	color = lerp(color, warmHorizon * 0.65, dawnWarmth * horizon * 0.55);

	const float sunDot = dot(worldRay, sunDir);
	// Much larger solar disc (~6x previous angular radius).
	const float sunDisc = smoothstep(0.9920, 0.9975, sunDot);
	const float sunGlow = pow(saturate(sunDot), 36.0) * saturate(sunIntensity);
	const float3 warmSun = lerp(sunColor, float3(1.0, 0.55, 0.18), dawnWarmth);
	color += warmSun * (sunDisc * 6.5 * saturate(sunIntensity * 2.0) + sunGlow * 0.85);

	const float moonDot = dot(worldRay, normalize(moonDirection));
	const float moonDisc = smoothstep(0.9975, 0.9992, moonDot);
	const float crater = 0.82 + 0.18 * sin(worldRay.x * 1730.0 + sin(worldRay.y * 1190.0));
	color += moonColor * moonDisc * crater * saturate(moonIntensity * 5.0) * 2.2;
	color += starField(worldRay) * (1.0 - daylight) * float3(0.72, 0.82, 1.0);

	const float4 clouds = traceClouds(worldRay, daylight);
	color = color * (1.0 - clouds.a) + clouds.rgb;

	return float4(color, 1.0);
}
