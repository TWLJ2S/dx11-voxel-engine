cbuffer BlockBreakOverlay : register(b0)
{
	matrix worldViewProjection;
	float stageIndex;
	float3 padding;
};

Texture2DArray destroyStages : register(t0);
SamplerState destroySampler : register(s0);

struct VertexInput
{
	float3 position : POSITION;
	float2 uv : TEXCOORD0;
};

struct PixelInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

PixelInput vertexMain(VertexInput input)
{
	PixelInput output;
	output.position = mul(float4(input.position, 1.0f), worldViewProjection);
	output.uv = input.uv;
	return output;
}

float4 pixelMain(PixelInput input) : SV_TARGET
{
	const float stage = clamp(floor(stageIndex + 0.001f), 0.0f, 9.0f);
	float4 color = destroyStages.Sample(destroySampler, float3(input.uv, stage));
	// Keep cracks readable without fully hiding the block.
	color.a *= 0.92f;
	clip(color.a - 0.02f);
	return color;
}
