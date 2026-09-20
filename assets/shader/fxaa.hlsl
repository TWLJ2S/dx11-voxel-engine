cbuffer FxaaBuffer : register(b0)
{
	float2 texelSize;
	float enabled;
	float padding;
};

Texture2D sceneColor : register(t0);
SamplerState linearClamp : register(s0);

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
	return dot(color, float3(0.299, 0.587, 0.114));
}

float4 pixelMain(PSInput input) : SV_TARGET
{
	float4 centerSample = sceneColor.SampleLevel(linearClamp, input.uv, 0);
	if (enabled < 0.5)
		return centerSample;

	const float2 uv = input.uv;
	const float3 rgbNW = sceneColor.SampleLevel(linearClamp, uv + float2(-1.0, -1.0) * texelSize, 0).rgb;
	const float3 rgbNE = sceneColor.SampleLevel(linearClamp, uv + float2(1.0, -1.0) * texelSize, 0).rgb;
	const float3 rgbSW = sceneColor.SampleLevel(linearClamp, uv + float2(-1.0, 1.0) * texelSize, 0).rgb;
	const float3 rgbSE = sceneColor.SampleLevel(linearClamp, uv + float2(1.0, 1.0) * texelSize, 0).rgb;
	const float3 rgbM = centerSample.rgb;

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

	const float dirReduce = max(
		(lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * (1.0 / 8.0)),
		1.0 / 128.0);
	const float rcpDirMin = rcp(min(abs(dir.x), abs(dir.y)) + dirReduce);
	dir = clamp(dir * rcpDirMin, -8.0, 8.0) * texelSize;

	const float3 rgbA = 0.5 * (
		sceneColor.SampleLevel(linearClamp, uv + dir * (1.0 / 3.0 - 0.5), 0).rgb +
		sceneColor.SampleLevel(linearClamp, uv + dir * (2.0 / 3.0 - 0.5), 0).rgb);
	const float3 rgbB = rgbA * 0.5 + 0.25 * (
		sceneColor.SampleLevel(linearClamp, uv + dir * -0.5, 0).rgb +
		sceneColor.SampleLevel(linearClamp, uv + dir * 0.5, 0).rgb);

	const float lumaB = luma(rgbB);
	if (lumaB < lumaMin || lumaB > lumaMax)
		return float4(rgbA, centerSample.a);
	return float4(rgbB, centerSample.a);
}
