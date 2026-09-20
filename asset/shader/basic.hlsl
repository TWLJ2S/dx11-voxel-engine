cbuffer TransformBuffer : register(b0)
{
    float4x4 modelMatrix;
    float4x4 viewMatrix;
    float4x4 projectionMatrix;
};

struct VSInput
{
    float3 position : POSITION;
    float2 uv       : TEXCOORD;
    uint   texId    : TEXID;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

VSOutput vsMain(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.position, 1.0f), modelMatrix);
    float4 viewPos  = mul(worldPos, viewMatrix);
    output.position = mul(viewPos, projectionMatrix);
    output.uv       = input.uv;

    return output;
}

float4 psMain(VSOutput input) : SV_TARGET
{
    return float4(0.85, 0.55, 0.25, 1.0);
}
