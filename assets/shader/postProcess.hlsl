cbuffer PostBuffer : register(b0)
{
	float4x4 inverseViewProjection;
	float2 texelSize;
	float2 aoTexelSize;
	float3 cameraPosition;
	float aoStrength;
	float3 fogColor;
	float fogDensity;
	float fogHeight;
	float fogHeightFalloff;
	float bloomThreshold;
	float bloomIntensity;
	float exposure;
	float time;
	float nearPlane;
	float weatherWetness;
	float2 weatherSurfaceOrigin;
	float2 weatherSurfaceSize;
};

Texture2D sceneColor : register(t0);
Texture2D sceneDepth : register(t1);
Texture2D aoTexture : register(t2);
Texture2D bloomTexture : register(t3);
StructuredBuffer<int> weatherSurface : register(t4);
SamplerState linearClamp : register(s0);
SamplerState pointClamp : register(s1);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

PSInput vertexMain(uint id : SV_VertexID)
{
	PSInput output;
	output.uv = float2((id << 1) & 2, id & 2);
	output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	return output;
}

float luma(float3 color)
{
	return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float weatherHash(float2 cell)
{
	return frac(sin(dot(cell, float2(127.1, 311.7))) * 43758.5453);
}

float3 reconstructWorld(float2 uv, float depth)
{
	// Camera uses an infinite-far matrix: clip.w = viewZ, depth = 1 - near/viewZ.
	// Treating depth as clip.z with w=1 only works at infinity and shreds nearby meshes.
	float w = nearPlane / max(1.0 - depth, 1e-5);
	float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
	float4 clip = float4(ndc * w, depth * w, w);
	float4 world = mul(clip, inverseViewProjection);
	return world.xyz / max(world.w, 1e-5);
}

float4 gtaoPS(PSInput input) : SV_TARGET
{
	float depth = sceneDepth.SampleLevel(pointClamp, input.uv, 0).r;
	if (depth >= 0.9994)
		return float4(1.0, depth, 0.0, 1.0);

	float3 origin = reconstructWorld(input.uv, depth);
	float dist = length(origin - cameraPosition);
	if (dist < 8.0)
		return float4(1.0, depth, 0.0, 1.0);

	float depthX = sceneDepth.SampleLevel(pointClamp, input.uv + float2(aoTexelSize.x, 0.0), 0).r;
	float depthY = sceneDepth.SampleLevel(pointClamp, input.uv + float2(0.0, aoTexelSize.y), 0).r;
	if (abs(depthX - depth) > 0.012 || abs(depthY - depth) > 0.012)
		return float4(1.0, depth, 0.0, 1.0);

	float3 originX = reconstructWorld(input.uv + float2(aoTexelSize.x, 0.0), depthX);
	float3 originY = reconstructWorld(input.uv + float2(0.0, aoTexelSize.y), depthY);
	float3 normalUn = cross(originY - origin, originX - origin);
	if (dot(normalUn, normalUn) < 1e-8)
		return float4(1.0, depth, 0.0, 1.0);
	float3 normal = normalize(normalUn);
	float3 view = normalize(cameraPosition - origin);
	if (dot(normal, view) < 0.0)
		normal = -normal;

	const int directions = 3;
	const int steps = 2;
	const float radius = 0.95;
	float noise = frac(52.9829189 * frac(dot(input.position.xy, float2(0.06711056, 0.00583715))));
	float ao = 0.0;

	[unroll]
	for (int direction = 0; direction < directions; ++direction)
	{
		float angle = ((float)direction + noise) * 3.14159265 / (float)directions;
		float2 ray = float2(cos(angle), sin(angle)) * aoTexelSize;
		float horizon = 0.0;
		[unroll]
		for (int stepIndex = 1; stepIndex <= steps; ++stepIndex)
		{
			float2 sampleUv = saturate(input.uv + ray * (float)stepIndex * 2.2);
			float sampleDepth = sceneDepth.SampleLevel(pointClamp, sampleUv, 0).r;
			if (sampleDepth >= 0.9994 || abs(sampleDepth - depth) > 0.02)
				continue;
			float3 samplePos = reconstructWorld(sampleUv, sampleDepth);
			float3 delta = samplePos - origin;
			float distance = length(delta);
			if (distance < 0.04)
				continue;
			float attenuation = saturate(1.0 - distance / radius);
			horizon = max(horizon, saturate(dot(normalize(delta), normal)) * attenuation);
		}
		ao += 1.0 - horizon;
	}

	ao = lerp(1.0, pow(saturate(ao / (float)directions), 1.05), saturate((dist - 8.0) / 12.0));
	return float4(ao, depth, 0.0, 1.0);
}

float4 gtaoUpsamplePS(PSInput input) : SV_TARGET
{
	float centerDepth = sceneDepth.SampleLevel(pointClamp, input.uv, 0).r;
	if (centerDepth >= 0.9994)
		return 1.0;

	float ao = 0.0;
	float weight = 0.0;
	[unroll]
	for (int y = -1; y <= 1; ++y)
	{
		[unroll]
		for (int x = -1; x <= 1; ++x)
		{
			float2 sampleUv = saturate(input.uv + float2(x, y) * aoTexelSize);
			float2 packed = aoTexture.SampleLevel(pointClamp, sampleUv, 0).rg;
			float depthWeight = exp(-abs(packed.g - centerDepth) * 420.0);
			ao += packed.r * depthWeight;
			weight += depthWeight;
		}
	}
	return weight > 1e-4 ? ao / weight : 1.0;
}

float4 bloomExtractPS(PSInput input) : SV_TARGET
{
	float3 color = sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb;
	float brightness = luma(color);
	float knee = bloomThreshold * 0.5;
	float soft = brightness - bloomThreshold + knee;
	soft = clamp(soft, 0.0, 2.0 * knee);
	soft = soft * soft / max(4.0 * knee, 1e-4);
	float contribution = max(soft, brightness - bloomThreshold) / max(brightness, 1e-4);
	return float4(color * contribution, 1.0);
}

float4 bloomBlurHPS(PSInput input) : SV_TARGET
{
	const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
	float3 color = sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb * weights[0];
	[unroll]
	for (int i = 1; i < 5; ++i)
	{
		float2 offset = float2(texelSize.x * (float)i * 1.5, 0.0);
		color += sceneColor.SampleLevel(linearClamp, input.uv + offset, 0).rgb * weights[i];
		color += sceneColor.SampleLevel(linearClamp, input.uv - offset, 0).rgb * weights[i];
	}
	return float4(color, 1.0);
}

float4 bloomBlurVPS(PSInput input) : SV_TARGET
{
	const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
	float3 color = sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb * weights[0];
	[unroll]
	for (int i = 1; i < 5; ++i)
	{
		float2 offset = float2(0.0, texelSize.y * (float)i * 1.5);
		color += sceneColor.SampleLevel(linearClamp, input.uv + offset, 0).rgb * weights[i];
		color += sceneColor.SampleLevel(linearClamp, input.uv - offset, 0).rgb * weights[i];
	}
	return float4(color, 1.0);
}

float3 acesFitted(float3 x)
{
	const float a = 2.51;
	const float b = 0.03;
	const float c = 2.43;
	const float d = 0.59;
	const float e = 0.14;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 compositePS(PSInput input) : SV_TARGET
{
	float3 color = sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb;
	float depth = sceneDepth.SampleLevel(pointClamp, input.uv, 0).r;
	float ao = aoTexture.SampleLevel(pointClamp, input.uv, 0).r;
	float aoMix = aoStrength;
	float3 world = 0.0;
	float distance = 0.0;
	const bool haveWorld = depth < 0.9994 &&
		(aoStrength > 0.0 || fogDensity > 0.0 || weatherWetness > 0.001);
	if (haveWorld)
	{
		world = reconstructWorld(input.uv, depth);
		distance = length(world - cameraPosition);
		aoMix *= saturate((distance - 8.0) / 12.0);
	}
	color *= lerp(1.0, ao, aoMix);

	if (haveWorld && weatherWetness > 0.001 &&
		weatherSurfaceSize.x > 0.0 && weatherSurfaceSize.y > 0.0)
	{
		int2 cell = int2(floor(world.xz - weatherSurfaceOrigin));
		if (all(cell >= 0) && cell.x < int(weatherSurfaceSize.x) && cell.y < int(weatherSurfaceSize.y))
		{
			uint surfaceIndex = uint(cell.y) * uint(weatherSurfaceSize.x) + uint(cell.x);
			float exposedSurfaceY = float(weatherSurface[surfaceIndex]);
			float exposed = 1.0 - smoothstep(0.035, 0.16, abs(world.y - exposedSurfaceY));
			float3 dxWorld = ddx(world);
			float3 dyWorld = ddy(world);
			float3 surfaceNormal = normalize(cross(dyWorld, dxWorld));
			float horizontal = smoothstep(0.82, 0.985, abs(surfaceNormal.y));
			// One bounded ellipse may occupy each large tile. Its radius and centre
			// leave a guaranteed dry margin, so neighbouring puddles stay separate.
			const float puddleTileSize = 4.5;
			float2 puddleCell = floor(world.xz / puddleTileSize);
			float seed = weatherHash(puddleCell + 7.3);
			float seedB = weatherHash(puddleCell * 1.91 + 31.7);
			float seedC = weatherHash(puddleCell * 3.17 - 11.4);
			// Eligibility never changes, preventing whole puddles from popping in.
			// Each eligible pool has a stable threshold and grows continuously as
			// accumulated surface wetness rises; drying naturally reverses the curve.
			float eligible = step(0.70, seed);
			float formationStart = lerp(0.045, 0.42, seedB);
			float formation = eligible * smoothstep(
				formationStart, formationStart + 0.34, weatherWetness);
			float2 local = frac(world.xz / puddleTileSize) - 0.5;
			float2 centreOffset = float2(
				weatherHash(puddleCell + 13.1), weatherHash(puddleCell + 47.9)) - 0.5;
			local -= centreOffset * 0.16;
			float angle = seedB * 6.28318531;
			float sineAngle = sin(angle), cosineAngle = cos(angle);
			float2 curvedLocal = float2(
				cosineAngle * local.x - sineAngle * local.y,
				sineAngle * local.x + cosineAngle * local.y);
			// Independent aspect, area, and curve powers produce everything from
			// small round pools to broad, softly lobed ovals without reaching a tile edge.
			float areaScale = lerp(0.48, 1.08, seedC * seedC);
			float2 puddleRadius = float2(
				lerp(0.17, 0.30, seed), lerp(0.11, 0.225, seedB)) * areaScale *
				lerp(0.08, 1.0, formation);
			float2 normalizedPuddle = abs(curvedLocal / puddleRadius);
			float curvePower = lerp(1.55, 3.15, weatherHash(puddleCell + 83.2));
			float curvedDistance = pow(
				pow(normalizedPuddle.x, curvePower) + pow(normalizedPuddle.y, curvePower),
				1.0 / curvePower);
			float edgeAngle = atan2(curvedLocal.y, curvedLocal.x);
			float lobeStrength = lerp(0.012, 0.075, weatherHash(puddleCell - 29.6));
			float edgeVariation = sin(edgeAngle * lerp(2.0, 4.0, seedC) + seed * 6.28318531) * lobeStrength +
				sin(edgeAngle * lerp(5.0, 8.0, seed) - seedB * 6.28318531) * lobeStrength * 0.42;
			float puddleShape = formation *
				(1.0 - smoothstep(0.78, 1.0, curvedDistance + edgeVariation));

			// Small, occasional rings disturb only a small part of each puddle.
			float2 rippleCentre = centreOffset * 0.05;
			float rippleDistance = length(local - rippleCentre);
			float rippleRadius = frac(time * 0.22 + seedB) * 0.105;
			float ripple = (1.0 - smoothstep(0.004, 0.013,
				abs(rippleDistance - rippleRadius))) *
				(1.0 - smoothstep(0.095, 0.125, rippleDistance)) *
				weatherWetness * formation * 0.34;
			// Fade the last eight blocks instead of revealing the boundary of the
			// moving exposed-surface map as a sharp dark square.
			float2 edgeDistance = min(float2(cell), weatherSurfaceSize - 1.0 - float2(cell));
			float coverageFade = smoothstep(0.0, 8.0, min(edgeDistance.x, edgeDistance.y));
			float wet = saturate(exposed * horizontal * puddleShape * coverageFade *
				lerp(0.22, 1.0, weatherWetness));
			float3 viewDirection = normalize(cameraPosition - world);
			float fresnel = pow(1.0 - saturate(abs(dot(viewDirection, surfaceNormal))), 3.0);
			float3 reflectedSky = fogColor * (0.72 + fresnel * 0.72) + ripple * 0.035;
			color = lerp(color * float3(0.72, 0.76, 0.80), reflectedSky, wet * (0.30 + fresnel * 0.46));
			color += ripple * wet * float3(0.025, 0.032, 0.040);
		}
	}

	if (haveWorld && fogDensity > 0.0)
	{
		float heightFog = exp(-fogHeightFalloff * max(world.y - fogHeight, 0.0));
		float fog = saturate(1.0 - exp(-fogDensity * distance * heightFog));
		fog *= smoothstep(18.0, 90.0, distance);
		color = lerp(color, fogColor, fog);
	}

	float3 bloom = bloomTexture.SampleLevel(linearClamp, input.uv, 0).rgb;
	color += bloom * bloomIntensity;
	color = acesFitted(color * max(exposure, 0.05));
	return float4(color, 1.0);
}

float4 smaaEdgePS(PSInput input) : SV_TARGET
{
	float3 color = sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb;
	float center = luma(color);
	float left = luma(sceneColor.SampleLevel(linearClamp, input.uv + float2(-texelSize.x, 0.0), 0).rgb);
	float top = luma(sceneColor.SampleLevel(linearClamp, input.uv + float2(0.0, -texelSize.y), 0).rgb);
	float2 delta = abs(float2(center - left, center - top));
	float2 edges = step(0.08, delta);
	if (dot(edges, 1.0) < 1e-5)
		discard;
	return float4(edges, 0.0, 1.0);
}

float searchLength(float2 uv, float2 offset, float2 axis)
{
	float distance = 0.0;
	[loop]
	for (int i = 1; i <= 12; ++i)
	{
		float2 sampleUv = saturate(uv + offset * (float)i);
		float2 edge = aoTexture.SampleLevel(pointClamp, sampleUv, 0).rg;
		float keep = axis.x > 0.5 ? edge.r : edge.g;
		if (keep < 0.5)
			return (float)i;
		distance = (float)i;
	}
	return distance;
}

float4 smaaBlendPS(PSInput input) : SV_TARGET
{
	float3 color = sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb;
	float2 edge = aoTexture.SampleLevel(pointClamp, input.uv, 0).rg;
	if (dot(edge, 1.0) < 1e-4)
		return float4(color, 1.0);

	float3 blended = color;
	if (edge.g > 0.5)
	{
		float left = searchLength(input.uv, float2(-texelSize.x, 0.0), float2(0.0, 1.0));
		float right = searchLength(input.uv, float2(texelSize.x, 0.0), float2(0.0, 1.0));
		float weight = saturate((left + right) / 16.0) * 0.42;
		float3 north = sceneColor.SampleLevel(linearClamp, input.uv + float2(0.0, -texelSize.y), 0).rgb;
		blended = lerp(blended, north, weight);
	}
	if (edge.r > 0.5)
	{
		float up = searchLength(input.uv, float2(0.0, -texelSize.y), float2(1.0, 0.0));
		float down = searchLength(input.uv, float2(0.0, texelSize.y), float2(1.0, 0.0));
		float weight = saturate((up + down) / 16.0) * 0.42;
		float3 west = sceneColor.SampleLevel(linearClamp, input.uv + float2(-texelSize.x, 0.0), 0).rgb;
		blended = lerp(blended, west, weight);
	}
	return float4(blended, 1.0);
}

float4 fxaaPS(PSInput input) : SV_TARGET
{
	const float3 rgbNW = sceneColor.SampleLevel(linearClamp, input.uv + float2(-1.0, -1.0) * texelSize, 0).rgb;
	const float3 rgbNE = sceneColor.SampleLevel(linearClamp, input.uv + float2(1.0, -1.0) * texelSize, 0).rgb;
	const float3 rgbSW = sceneColor.SampleLevel(linearClamp, input.uv + float2(-1.0, 1.0) * texelSize, 0).rgb;
	const float3 rgbSE = sceneColor.SampleLevel(linearClamp, input.uv + float2(1.0, 1.0) * texelSize, 0).rgb;
	const float3 rgbM = sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb;
	const float lumaNW = luma(rgbNW);
	const float lumaNE = luma(rgbNE);
	const float lumaSW = luma(rgbSW);
	const float lumaSE = luma(rgbSE);
	const float lumaM = luma(rgbM);
	const float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
	const float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));
	float2 dir;
	dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
	dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
	const float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 1.0 / 128.0);
	dir = clamp(dir * rcp(min(abs(dir.x), abs(dir.y)) + dirReduce), -8.0, 8.0) * texelSize;
	const float3 rgbA = 0.5 * (
		sceneColor.SampleLevel(linearClamp, input.uv + dir * (1.0 / 3.0 - 0.5), 0).rgb +
		sceneColor.SampleLevel(linearClamp, input.uv + dir * (2.0 / 3.0 - 0.5), 0).rgb);
	const float3 rgbB = rgbA * 0.5 + 0.25 * (
		sceneColor.SampleLevel(linearClamp, input.uv + dir * -0.5, 0).rgb +
		sceneColor.SampleLevel(linearClamp, input.uv + dir * 0.5, 0).rgb);
	const float lumaB = luma(rgbB);
	if (lumaB < lumaMin || lumaB > lumaMax)
		return float4(rgbA, 1.0);
	return float4(rgbB, 1.0);
}

float4 blitPS(PSInput input) : SV_TARGET
{
	float3 color = sceneColor.SampleLevel(linearClamp, input.uv, 0).rgb;
	return float4(color, 1.0);
}
