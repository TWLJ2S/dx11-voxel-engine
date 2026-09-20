struct VSInput
{
	float3 position : POSITION;
	float3 normal : NORMAL;
	float2 uv : TEXCOORD0;
	uint material : MATID;
	uint ao : AO;
	float opacity : OPACITY;
};

cbuffer ObjectBuffer : register(b0) { matrix world; };
cbuffer CelestialShadowBuffer : register(b8)
{
	matrix celestialViewProjection[3];
	float4 cascadeSplits;
	float2 celestialShadowTexelSize;
	float celestialShadowEnabled;
	float celestialShadowPadding;
	float4 cascadeTexelWorld;
};

float4 main(VSInput input) : SV_POSITION
{
	return mul(mul(float4(input.position, 1.0), world), celestialViewProjection[0]);
}
