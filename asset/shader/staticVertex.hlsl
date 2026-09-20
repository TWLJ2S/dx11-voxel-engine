struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    uint material : MATID;
    uint ao : AO;
    float opacity : OPACITY;
};

struct Light
{
    float3 position;
    float radius;
    float3 color;
    float intensity;
    float3 halfExtent;
    uint type;
};

StructuredBuffer<Light> lights : register(t64);
StructuredBuffer<uint> lightOccluders : register(t65);

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    nointerpolation uint material : MATID;
    nointerpolation uint aoCorners : AO;
    float opacity : OPACITY;
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

cbuffer LightBuffer : register(b2)
{
    uint lightCount;
    float3 lightPadding;
};

cbuffer LightOcclusionBuffer : register(b3)
{
    int3 lightOcclusionOrigin;
    uint lightOcclusionSize;
};

bool isLightOccluder(int3 worldVoxel)
{
    int3 local = worldVoxel - lightOcclusionOrigin;
    if (any(local < 0) || any(local >= int3(lightOcclusionSize, lightOcclusionSize, lightOcclusionSize)))
        return false;

    uint index = (uint)local.x + lightOcclusionSize *
        ((uint)local.z + lightOcclusionSize * (uint)local.y);
    return (lightOccluders[index >> 5] & (1u << (index & 31u))) != 0u;
}

float getLightVisibility(float3 surfacePosition, float3 normal)
{
    if (lightCount == 0u)
        return 1.0;

    Light light = lights[0];
    float3 ray = light.position - surfacePosition;
    float rayLengthSquared = dot(ray, ray);
    if (rayLengthSquared >= light.radius * light.radius || dot(normal, ray) <= 0.0)
        return 1.0;

    float3 start = surfacePosition + normal * 0.01;
    ray = light.position - start;
    float rayLength = length(ray);
    if (rayLength <= 0.02)
        return 1.0;

    float3 direction = ray / rayLength;
    int3 voxel = (int3)floor(start);
    int3 endVoxel = (int3)floor(light.position);
    int3 step = int3(
        direction.x >= 0.0 ? 1 : -1,
        direction.y >= 0.0 ? 1 : -1,
        direction.z >= 0.0 ? 1 : -1
    );
    float3 safeDirection = float3(
        abs(direction.x) < 1e-6 ? (direction.x < 0.0 ? -1e-6 : 1e-6) : direction.x,
        abs(direction.y) < 1e-6 ? (direction.y < 0.0 ? -1e-6 : 1e-6) : direction.y,
        abs(direction.z) < 1e-6 ? (direction.z < 0.0 ? -1e-6 : 1e-6) : direction.z
    );
    float3 deltaDistance = abs(1.0 / safeDirection);
    float3 nextBoundary = float3(voxel) + float3(
        step.x > 0 ? 1.0 : 0.0,
        step.y > 0 ? 1.0 : 0.0,
        step.z > 0 ? 1.0 : 0.0
    );
    float3 sideDistance = (nextBoundary - start) / safeDirection;

    [loop]
    for (uint iteration = 0; iteration < 160u; ++iteration)
    {
        float travelled;
        if (sideDistance.x <= sideDistance.y && sideDistance.x <= sideDistance.z)
        {
            travelled = sideDistance.x;
            sideDistance.x += deltaDistance.x;
            voxel.x += step.x;
        }
        else if (sideDistance.y <= sideDistance.z)
        {
            travelled = sideDistance.y;
            sideDistance.y += deltaDistance.y;
            voxel.y += step.y;
        }
        else
        {
            travelled = sideDistance.z;
            sideDistance.z += deltaDistance.z;
            voxel.z += step.z;
        }

        if (travelled >= rayLength - 0.01 || all(voxel == endVoxel))
            return 1.0;
        if (isLightOccluder(voxel))
            return 0.0;
    }

    return 1.0;
}

VSOutput main(VSInput input)
{
    VSOutput output;

    float4 worldPos = mul(float4(input.position, 1.0), world);

    output.position = mul(mul(worldPos, view), projection);
    output.worldPosition = worldPos.xyz;

    output.normal = normalize(mul(input.normal, (float3x3) world));
    output.uv = input.uv;
    output.material = input.material;
    output.aoCorners = input.ao;
    output.opacity = input.opacity;

    return output;
}
