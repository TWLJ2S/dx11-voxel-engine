struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint bone : MATID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
    float2 uv : TEXCOORD0;
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

cbuffer PlayerBoneBuffer : register(b6)
{
    matrix boneTransforms[6];
    uint meshPart;
    uint3 bonePadding;
};

Texture2D playerSkin : register(t0);
SamplerState skinSampler : register(s0);

VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    uint bone = meshPart == 1u ? 1u : input.bone;
    float4 animatedPosition = mul(float4(input.position, 1.0), boneTransforms[bone]);
    float4 worldPosition = mul(animatedPosition, world);
    output.position = mul(worldPosition, lightViewProjection);
    output.worldPosition = worldPosition.xyz;
    output.uv = input.uv;
    return output;
}

float pixelMain(VSOutput input) : SV_DEPTH
{
    float alpha = playerSkin.Sample(skinSampler, input.uv).a;
    clip(alpha - (1.0 / 255.0));
    return saturate(length(input.worldPosition - shadowLightPosition) / shadowLightRadius);
}
