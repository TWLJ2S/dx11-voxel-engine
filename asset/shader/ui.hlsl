cbuffer UiFrame : register(b0)
{
	float2 viewportSize;
	float2 padding;
};

Texture2D uiTexture : register(t0);
SamplerState uiSampler : register(s0);

struct VertexInput
{
	float2 position : POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};

struct PixelInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 color : COLOR0;
};

PixelInput vertexMain(VertexInput input)
{
	PixelInput output;
	float2 normalized = input.position / viewportSize;
	output.position = float4(normalized.x * 2.0f - 1.0f, 1.0f - normalized.y * 2.0f, 0.0f, 1.0f);
	output.uv = input.uv;
	output.color = input.color;
	return output;
}

float4 pixelMain(PixelInput input) : SV_TARGET
{
	return uiTexture.Sample(uiSampler, input.uv) * input.color;
}
