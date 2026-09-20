cbuffer CameraBuffer : register(b1)
{
	matrix view;
	matrix projection;
	float3 cameraPosition;
	float waterTime;
	float4 waterMotion;
	matrix viewProjection;
};

Texture2DArray particleTextures : register(t0);
SamplerState particleSampler : register(s0);

struct VSInput
{
	float3 position : POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
	uint textureIndex : TEXINDEX;
};

struct PSInput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
	nointerpolation uint textureIndex : TEXINDEX;
};

PSInput vertexMain(VSInput input)
{
	PSInput output;
	output.position = mul(mul(float4(input.position, 1.0), view), projection);
	output.color = input.color;
	output.uv = input.uv;
	output.textureIndex = input.textureIndex;
	return output;
}

float4 pixelMain(PSInput input) : SV_TARGET
{
	float4 texel = particleTextures.Sample(particleSampler, float3(input.uv, input.textureIndex));
	float alpha = texel.a * input.color.a;
	clip(alpha - 0.02);
	float3 color = texel.rgb * input.color.rgb;
	return float4(color, alpha);
}
