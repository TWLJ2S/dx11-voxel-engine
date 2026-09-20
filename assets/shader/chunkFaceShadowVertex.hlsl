struct VisibleFace { uint packedPosition; uint packedSize; uint material; uint ao; float opacity; };
StructuredBuffer<VisibleFace> chunkFaces : register(t66);

struct VSOutput { float4 position : SV_POSITION; float3 worldPosition : POSITION; };
cbuffer ObjectBuffer : register(b0) { matrix world; };
cbuffer PointShadowBuffer : register(b4)
{
    matrix lightViewProjection;
    float3 shadowLightPosition;
    float shadowLightRadius;
};

void faceCorners(uint direction, float3 p, float width, float height, out float3 a, out float3 b, out float3 c, out float3 d)
{
    if (direction == 0u) { a=p; b=p+float3(0,0,width); c=p+float3(0,height,width); d=p+float3(0,height,0); }
    else if (direction == 1u) { a=p+float3(1,0,width); b=p+float3(1,0,0); c=p+float3(1,height,0); d=p+float3(1,height,width); }
    else if (direction == 2u) { a=p; b=p+float3(width,0,0); c=p+float3(width,0,height); d=p+float3(0,0,height); }
    else if (direction == 3u) { a=p+float3(width,1,0); b=p+float3(0,1,0); c=p+float3(0,1,height); d=p+float3(width,1,height); }
    else if (direction == 4u) { a=p+float3(width,0,0); b=p; c=p+float3(0,height,0); d=p+float3(width,height,0); }
    else { a=p+float3(0,0,1); b=p+float3(width,0,1); c=p+float3(width,height,1); d=p+float3(0,height,1); }
}

VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VisibleFace face = chunkFaces[instanceId];
    uint direction = (face.packedPosition >> 17) & 7u;
    float faceWidth = float((face.packedSize & 31u) + 1u);
    float faceHeight = float(((face.packedSize >> 5u) & 31u) + 1u);
    uint fluidLevel = (face.packedSize >> 10u) & 15u;
    uint neighborFluidLevel = (face.packedSize >> 14u) & 7u;
    float3 p = float3(
        face.packedPosition & 15u,
        (face.packedPosition >> 8) & 511u,
        (face.packedPosition >> 4) & 15u);
    float3 corners[4];
    faceCorners(direction, p, faceWidth, faceHeight, corners[0], corners[1], corners[2], corners[3]);
    if (fluidLevel != 0u) {
        float h00 = p.y + float((face.ao >> 8u) & 15u) / 8.0;
        float h10 = p.y + float((face.ao >> 12u) & 15u) / 8.0;
        float h01 = p.y + float((face.ao >> 16u) & 15u) / 8.0;
        float h11 = p.y + float((face.ao >> 20u) & 15u) / 8.0;
        if (direction == 3u) {
            corners[0].y = h10; corners[1].y = h00;
            corners[2].y = h01; corners[3].y = h11;
        }
        else if (direction == 0u) { corners[2].y = h01; corners[3].y = h00; }
        else if (direction == 1u) { corners[2].y = h10; corners[3].y = h11; }
        else if (direction == 4u) { corners[2].y = h00; corners[3].y = h10; }
        else if (direction == 5u) { corners[2].y = h11; corners[3].y = h01; }
    }
    const uint cornerIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    float4 worldPosition = mul(float4(corners[cornerIndices[vertexId]], 1.0), world);
    VSOutput output;
    output.position = mul(worldPosition, lightViewProjection);
    output.worldPosition = worldPosition.xyz;
    return output;
}
