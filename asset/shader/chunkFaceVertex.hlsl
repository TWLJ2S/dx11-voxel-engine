struct VisibleFace
{
    uint packedPosition;
    uint packedSize;
    uint material;
    uint ao;
    float opacity;
};

StructuredBuffer<VisibleFace> chunkFaces : register(t66);

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

cbuffer ObjectBuffer : register(b0) { matrix world; };
cbuffer CameraBuffer : register(b1)
{
    matrix view;
    matrix projection;
    float3 cameraPosition;
    float padding;
};

void faceCorners(uint direction, float3 p, float width, float height, out float3 a, out float3 b, out float3 c, out float3 d, out float3 normal)
{
    if (direction == 0u) { a=p; b=p+float3(0,0,width); c=p+float3(0,height,width); d=p+float3(0,height,0); normal=float3(-1,0,0); }
    else if (direction == 1u) { a=p+float3(1,0,width); b=p+float3(1,0,0); c=p+float3(1,height,0); d=p+float3(1,height,width); normal=float3(1,0,0); }
    else if (direction == 2u) { a=p; b=p+float3(width,0,0); c=p+float3(width,0,height); d=p+float3(0,0,height); normal=float3(0,-1,0); }
    else if (direction == 3u) { a=p+float3(width,1,0); b=p+float3(0,1,0); c=p+float3(0,1,height); d=p+float3(width,1,height); normal=float3(0,1,0); }
    else if (direction == 4u) { a=p+float3(width,0,0); b=p; c=p+float3(0,height,0); d=p+float3(width,height,0); normal=float3(0,0,-1); }
    else { a=p+float3(0,0,1); b=p+float3(width,0,1); c=p+float3(width,height,1); d=p+float3(0,height,1); normal=float3(0,0,1); }
}

VSOutput main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    VisibleFace face = chunkFaces[instanceId];
    uint x = face.packedPosition & 15u;
    uint z = (face.packedPosition >> 4) & 15u;
    uint y = (face.packedPosition >> 8) & 255u;
    uint direction = (face.packedPosition >> 16) & 7u;
    float faceWidth = float((face.packedSize & 31u) + 1u);
    float faceHeight = float(((face.packedSize >> 5u) & 31u) + 1u);

    float3 corners[4];
    float3 normal;
    faceCorners(direction, float3(x,y,z), faceWidth, faceHeight, corners[0], corners[1], corners[2], corners[3], normal);
    const uint cornerIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    uint corner = cornerIndices[vertexId];
    bool flipV = direction < 2u || direction >= 4u;
    const float2 standardUV[4] = { float2(0,0), float2(1,0), float2(1,1), float2(0,1) };
    const float2 flippedUV[4] = { float2(0,1), float2(1,1), float2(1,0), float2(0,0) };

    float4 worldPosition = mul(float4(corners[corner], 1.0), world);
    VSOutput output;
    output.position = mul(mul(worldPosition, view), projection);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(mul(normal, (float3x3)world));
    output.uv = (flipV ? flippedUV[corner] : standardUV[corner]) * float2(faceWidth, faceHeight);
    output.material = face.material;
    if (flipV) {
        uint a0 = face.ao & 3u;
        uint a1 = (face.ao >> 2) & 3u;
        uint a2 = (face.ao >> 4) & 3u;
        uint a3 = (face.ao >> 6) & 3u;
        output.aoCorners = a3 | (a2 << 2) | (a1 << 4) | (a0 << 6);
    }
    else output.aoCorners = face.ao;
    output.opacity = face.opacity;
    return output;
}
