cbuffer BlockOutline : register(b0)
{
	matrix worldViewProjection;
	float4 outlineColor;
};

struct VertexInput
{
	float3 position : POSITION;
};

struct PixelInput
{
	float4 position : SV_POSITION;
};

PixelInput vertexMain(VertexInput input)
{
	PixelInput output;
	output.position = mul(float4(input.position, 1.0f), worldViewProjection);
	return output;
}

float4 pixelMain(PixelInput input) : SV_TARGET
{
	return outlineColor;
}
