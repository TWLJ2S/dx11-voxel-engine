cbuffer Crosshair : register(b0)
{
	float2 invViewport;
	float thicknessPx;
	float armLengthPx;
	float4 color;
};

struct VertexInput
{
	float2 corner : POSITION; // unit square corner in [-1,1] for each arm segment
	float2 axis : TEXCOORD0;  // (1,0) horizontal arm, (0,1) vertical arm
};

struct PixelInput
{
	float4 position : SV_POSITION;
};

PixelInput vertexMain(VertexInput input)
{
	PixelInput output;
	const float2 halfSize = float2(
		(input.axis.x > 0.5f ? armLengthPx : thicknessPx) * 0.5f,
		(input.axis.y > 0.5f ? armLengthPx : thicknessPx) * 0.5f
	);
	const float2 pixel = input.corner * halfSize;
	const float2 ndc = float2(pixel.x * invViewport.x, -pixel.y * invViewport.y);
	output.position = float4(ndc, 0.0f, 1.0f);
	return output;
}

float4 pixelMain(PixelInput input) : SV_TARGET
{
	return color;
}
