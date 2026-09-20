StructuredBuffer<uint> centerChunk : register(t0);
StructuredBuffer<uint> negXChunk : register(t1);
StructuredBuffer<uint> posXChunk : register(t2);
StructuredBuffer<uint> negZChunk : register(t3);
StructuredBuffer<uint> posZChunk : register(t4);
StructuredBuffer<uint> negXNegZChunk : register(t5);
StructuredBuffer<uint> negXPosZChunk : register(t6);
StructuredBuffer<uint> posXNegZChunk : register(t7);
StructuredBuffer<uint> posXPosZChunk : register(t8);
RWStructuredBuffer<uint> paddedVoxels : register(u0);

uint readPacked(uint source, uint index)
{
    uint packed = 0u;
    uint word = index >> 1;
    if (source == 0u) packed = centerChunk[word];
    else if (source == 1u) packed = negXChunk[word];
    else if (source == 2u) packed = posXChunk[word];
    else if (source == 3u) packed = negZChunk[word];
    else if (source == 4u) packed = posZChunk[word];
    else if (source == 5u) packed = negXNegZChunk[word];
    else if (source == 6u) packed = negXPosZChunk[word];
    else if (source == 7u) packed = posXNegZChunk[word];
    else packed = posXPosZChunk[word];
    return (packed >> ((index & 1u) * 16u)) & 65535u;
}

[numthreads(8,8,8)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 18u || id.y >= 386u || id.z >= 18u) return;
    int3 p = int3(id) - 1;
    uint outputIndex = id.x + 18u * (id.z + 18u * id.y);
    if (p.y < 0 || p.y >= 384) { paddedVoxels[outputIndex] = 0u; return; }

    uint source = 0u;
    if (p.x < 0 && p.z < 0) { source=5u; p.x=15; p.z=15; }
    else if (p.x < 0 && p.z >= 16) { source=6u; p.x=15; p.z=0; }
    else if (p.x >= 16 && p.z < 0) { source=7u; p.x=0; p.z=15; }
    else if (p.x >= 16 && p.z >= 16) { source=8u; p.x=0; p.z=0; }
    else if (p.x < 0) { source=1u; p.x=15; }
    else if (p.x >= 16) { source=2u; p.x=0; }
    else if (p.z < 0) { source=3u; p.z=15; }
    else if (p.z >= 16) { source=4u; p.z=0; }
    uint sourceIndex = (uint)p.x + 16u * ((uint)p.z + 16u * (uint)p.y);
    paddedVoxels[outputIndex] = readPacked(source, sourceIndex);
}
