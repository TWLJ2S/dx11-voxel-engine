struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint material : MATID;
    uint ao : AO;
    float opacity : OPACITY;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
};

cbuffer ObjectBuffer : register(b0)
{
    matrix world;
};

cbuffer PointShadowBuffer : register(b4)
{
    matrix lightViewProjection;
    float3 shadowLightPosition;
    float shadowLightRadius;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPosition = mul(float4(input.position, 1.0), world);
    output.position = mul(worldPosition, lightViewProjection);
    output.worldPosition = worldPosition.xyz;
    return output;
}
