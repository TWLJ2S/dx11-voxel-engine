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
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
    float3 modelPosition : TEXCOORD2;
    nointerpolation uint boneId : BONEID;
    nointerpolation uint firstPerson : FIRSTPERSON;
    nointerpolation float visibility : VISIBILITY;
};

cbuffer ObjectBuffer : register(b0)
{
    matrix world;
};

cbuffer CameraBuffer : register(b1)
{
    matrix view;
    matrix projection;
    float3 cameraPosition;
    float padding;
};

cbuffer PlayerBoneBuffer : register(b6)
{
    matrix boneTransforms[6];
    uint meshPart;
    uint3 bonePadding;
};

VSOutput vertexMain(VSInput input)
{
    VSOutput output;
    uint bone = meshPart == 1u ? 1u : input.bone;
    float4 animatedPosition = meshPart == 2u
        ? float4(input.position, 1.0)
        : mul(float4(input.position, 1.0), boneTransforms[bone]);
    float4 worldPosition = mul(animatedPosition, world);
    output.position = mul(mul(worldPosition, view), projection);
    output.worldPosition = worldPosition.xyz;
    output.modelPosition = animatedPosition.xyz;
    output.boneId = bone;
    output.firstPerson = meshPart == 3u ? 1u : 0u;
    float3 animatedNormal = meshPart == 2u ? input.normal : mul(input.normal, (float3x3)boneTransforms[bone]);
    output.normal = normalize(mul(animatedNormal, (float3x3)world));
    output.uv = input.uv;
    // In first person the camera sits just beyond the face, so the attached
    // torso and all four limbs can be rendered without enclosing the camera.
    output.visibility = 1.0;
    return output;
}

Texture2D playerSkin : register(t0);
SamplerState skinSampler : register(s0);

float4 pixelMain(VSOutput input) : SV_TARGET
{
    clip(input.visibility - 0.5);
	float4 albedo = playerSkin.Sample(skinSampler, input.uv);
    clip(albedo.a - (1.0 / 255.0));

    float3 normal = normalize(input.normal);
    float skyLight = 0.68 + 0.32 * saturate(dot(normal, normalize(float3(-0.35, 0.85, -0.4))));
    return float4(albedo.rgb * skyLight, albedo.a);
}
