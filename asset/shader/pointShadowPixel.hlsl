struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
};

cbuffer PointShadowBuffer : register(b4)
{
    matrix lightViewProjection;
    float3 shadowLightPosition;
    float shadowLightRadius;
};

float main(PSInput input) : SV_DEPTH
{
    return saturate(length(input.worldPosition - shadowLightPosition) / shadowLightRadius);
}
