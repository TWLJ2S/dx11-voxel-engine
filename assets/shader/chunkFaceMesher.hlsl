struct BlockInfo
{
    uint flags;
    uint material0;
    uint material1;
    uint material2;
    uint material3;
    uint material4;
    uint material5;
    float opacity;
};

struct VisibleFace
{
    uint packedPosition;
    uint packedSize;
    uint material;
    uint ao;
    float opacity;
};

StructuredBuffer<uint> voxels : register(t0);
StructuredBuffer<BlockInfo> blocks : register(t1);
AppendStructuredBuffer<VisibleFace> opaqueFaces : register(u0);
AppendStructuredBuffer<VisibleFace> translucentFaces : register(u1);

groupshared uint faceMask[256];

static const uint Width = 16;
static const uint Height = 384;
static const uint Length = 16;
static const uint PaddedWidth = Width + 2;
static const uint PaddedLength = Length + 2;
static const uint BlockTypeMask = 4095u;
static const uint FluidLevelShift = 12u;
static const uint MaximumFluidLevel = 8u;

cbuffer MesherConstants : register(b0)
{
    uint occupiedSubchunks;
    uint3 occupiedPadding;
};

uint blockType(uint state) { return state & BlockTypeMask; }

uint fluidLevel(uint state)
{
    if (blockType(state) != 5u) return 0u;
    uint stored = (state >> FluidLevelShift) & 7u;
    return stored == 0u ? MaximumFluidLevel : stored;
}

uint voxelAt(int3 position)
{
    uint3 padded = uint3(position.x + 1, position.y + 1, position.z + 1);
    return voxels[padded.x + PaddedWidth * (padded.z + PaddedLength * padded.y)];
}

uint fluidCornerLevel(int3 position, uint cornerX, uint cornerZ)
{
    int startX = position.x + int(cornerX) - 1;
    int startZ = position.z + int(cornerZ) - 1;
    uint total = 0u;
    uint samples = 0u;
    [unroll] for (int dz = 0; dz <= 1; ++dz) {
        [unroll] for (int dx = 0; dx <= 1; ++dx) {
            int3 samplePosition = int3(startX + dx, position.y, startZ + dz);
            if (fluidLevel(voxelAt(samplePosition + int3(0, 1, 0))) > 0u)
                return MaximumFluidLevel;
            uint level = fluidLevel(voxelAt(samplePosition));
            if (level != 0u) {
                // Lower exposed source surfaces by 2/16 of a block. Water
                // stacked above still takes the full-height early return.
                total += level == MaximumFluidLevel ? MaximumFluidLevel - 1u : level;
                ++samples;
            }
        }
    }
    return samples == 0u ? 0u : (total + samples / 2u) / samples;
}

uint faceMaterial(BlockInfo block, uint direction)
{
    if (direction == 0) return block.material0;
    if (direction == 1) return block.material1;
    if (direction == 2) return block.material2;
    if (direction == 3) return block.material3;
    if (direction == 4) return block.material4;
    return block.material5;
}

bool visible(uint state, BlockInfo block, uint direction, int3 neighborPosition, out uint neighborFluidLevel)
{
    uint neighborState = voxelAt(neighborPosition);
    uint neighborId = blockType(neighborState);
    neighborFluidLevel = fluidLevel(neighborState);
    if (neighborId == 0u)
        return true;

    BlockInfo neighbor = blocks[neighborId];
    bool transparent = (block.flags & 4u) != 0;
    bool cutout = (block.flags & 16u) != 0;
    bool neighborTransparent = (neighbor.flags & 4u) != 0;
    bool neighborCutout = (neighbor.flags & 16u) != 0;
    bool neighborOccludes = (neighbor.flags & 2u) != 0;
    if (transparent) {
        if (neighborTransparent && neighborId == blockType(state))
            return false;
        // Never emit water/glass against occluding solids. Those quads sit on the
        // same plane as the opaque face and z-fight once translucent alpha is 1.
        if (!neighborTransparent && neighborOccludes)
            return false;
        return true;
    }
    if (cutout) {
        if (neighborCutout)
            return false;
        return neighborTransparent || !neighborOccludes;
    }
    return neighborTransparent || neighborCutout || !neighborOccludes;
}

bool aoCube(int3 position)
{
    uint id = blockType(voxelAt(position));
    if (id == 0u)
        return false;
    uint flags = blocks[id].flags;
    return (flags & 2u) != 0 && (flags & 4u) == 0;
}

uint vertexAO(int3 base, int3 sideOffset, int3 tangentOffset)
{
    bool side1 = aoCube(base + sideOffset);
    bool side2 = aoCube(base + tangentOffset);
    if (side1 && side2)
        return 0u;
    bool corner = aoCube(base + sideOffset + tangentOffset);
    return 3u - (uint)side1 - (uint)side2 - (uint)corner;
}

uint4 faceAO(int3 p, uint direction)
{
    if (direction == 0u) {
        int3 b = p + int3(-1, 0, 0);
        return uint4(
            vertexAO(b, int3(0,-1,0), int3(0,0,-1)),
            vertexAO(b, int3(0,-1,0), int3(0,0, 1)),
            vertexAO(b, int3(0, 1,0), int3(0,0, 1)),
            vertexAO(b, int3(0, 1,0), int3(0,0,-1)));
    }
    if (direction == 1u) {
        int3 b = p + int3(1, 0, 0);
        return uint4(
            vertexAO(b, int3(0,-1,0), int3(0,0, 1)),
            vertexAO(b, int3(0,-1,0), int3(0,0,-1)),
            vertexAO(b, int3(0, 1,0), int3(0,0,-1)),
            vertexAO(b, int3(0, 1,0), int3(0,0, 1)));
    }
    if (direction == 2u) {
        int3 b = p + int3(0, -1, 0);
        return uint4(
            vertexAO(b, int3(-1,0,0), int3(0,0,-1)),
            vertexAO(b, int3( 1,0,0), int3(0,0,-1)),
            vertexAO(b, int3( 1,0,0), int3(0,0, 1)),
            vertexAO(b, int3(-1,0,0), int3(0,0, 1)));
    }
    if (direction == 3u) {
        int3 b = p + int3(0, 1, 0);
        return uint4(
            vertexAO(b, int3( 1,0,0), int3(0,0,-1)),
            vertexAO(b, int3(-1,0,0), int3(0,0,-1)),
            vertexAO(b, int3(-1,0,0), int3(0,0, 1)),
            vertexAO(b, int3( 1,0,0), int3(0,0, 1)));
    }
    if (direction == 4u) {
        int3 b = p + int3(0, 0, -1);
        return uint4(
            vertexAO(b, int3( 1,0,0), int3(0,-1,0)),
            vertexAO(b, int3(-1,0,0), int3(0,-1,0)),
            vertexAO(b, int3(-1,0,0), int3(0, 1,0)),
            vertexAO(b, int3( 1,0,0), int3(0, 1,0)));
    }
    int3 b = p + int3(0, 0, 1);
    return uint4(
        vertexAO(b, int3(-1,0,0), int3(0,-1,0)),
        vertexAO(b, int3( 1,0,0), int3(0,-1,0)),
        vertexAO(b, int3( 1,0,0), int3(0, 1,0)),
        vertexAO(b, int3(-1,0,0), int3(0, 1,0)));
}

[numthreads(16, 16, 1)]
void main(uint3 groupId : SV_GroupID, uint3 groupThreadId : SV_GroupThreadID)
{
    const int3 offsets[6] = {
        int3(-1, 0, 0), int3(1, 0, 0),
        int3(0, -1, 0), int3(0, 1, 0),
        int3(0, 0, -1), int3(0, 0, 1)
    };
    uint section = groupId.x;
    uint direction = groupId.y;
    if (section >= 24u || direction >= 6u) return;
    if (((occupiedSubchunks >> section) & 1u) == 0u) return;
    uint localU = groupThreadId.x;
    uint localV = groupThreadId.y;
    uint maskIndex = localU + 16u * localV;
    uint yBase = section * 16u;

    [loop]
    for (uint slice = 0u; slice < 16u; ++slice)
    {
        uint key = 0u;
        int3 position;
        if (direction < 2u) position = int3(slice, yBase + localV, localU);
        else if (direction < 4u) position = int3(localU, yBase + slice, localV);
        else position = int3(localU, yBase + localV, slice);

        uint state = position.y < int(Height) ? voxelAt(position) : 0u;
        uint id = blockType(state);
        if (id != 0u) {
            BlockInfo block = blocks[id];
            uint neighborLevel = 0u;
            if ((block.flags & 9u) == 9u && visible(state, block, direction, position + offsets[direction], neighborLevel)) {
                uint4 ao = faceAO(position, direction);
                uint packedAO = ao.x | (ao.y << 2) | (ao.z << 4) | (ao.w << 6);
                // id:12 | ao:8 | fluid:4 | neighborFluid:3 | translucent:1
                key = id |
                    (packedAO << 12u) |
                    ((fluidLevel(state) & 15u) << 20u) |
                    ((neighborLevel & 7u) << 24u) |
                    (((block.flags & 4u) != 0u ? 1u : 0u) << 27u);
            }
        }
        faceMask[maskIndex] = key;
        GroupMemoryBarrierWithGroupSync();

        if (maskIndex == 0u) {
            for (uint v = 0u; v < 16u; ++v) {
                for (uint u = 0u; u < 16u;) {
                    uint first = u + 16u * v;
                    uint faceKey = faceMask[first];
                    if (faceKey == 0u) { ++u; continue; }
                    uint packedAO = (faceKey >> 12u) & 255u;
                    uint currentFluidLevel = (faceKey >> 20u) & 15u;
                    uint neighborFluidLevel = (faceKey >> 24u) & 7u;
                    uint faceId = faceKey & BlockTypeMask;
                    bool uniformAO = (packedAO & 3u) == ((packedAO >> 2u) & 3u) &&
                        (packedAO & 3u) == ((packedAO >> 4u) & 3u) &&
                        (packedAO & 3u) == ((packedAO >> 6u) & 3u);
                    uint width = 1u;
                    uint height = 1u;
                    if (uniformAO && currentFluidLevel == 0u) {
                        while (u + width < 16u && faceMask[first + width] == faceKey) ++width;
                        bool grow = true;
                        while (v + height < 16u && grow) {
                            for (uint x = 0u; x < width; ++x)
                                if (faceMask[u + x + 16u * (v + height)] != faceKey) { grow = false; break; }
                            if (grow) ++height;
                        }
                    }

                    int3 origin;
                    if (direction < 2u) origin = int3(slice, yBase + v, u);
                    else if (direction < 4u) origin = int3(u, yBase + slice, v);
                    else origin = int3(u, yBase + v, slice);
                    VisibleFace face;
                    face.packedPosition = uint(origin.x) | (uint(origin.z) << 4u) |
                        (uint(origin.y) << 8u) | (direction << 17u);
                    face.packedSize = (width - 1u) | ((height - 1u) << 5u) |
                        (currentFluidLevel << 10u) | (neighborFluidLevel << 14u);
                    face.material = faceMaterial(blocks[faceId], direction);
                    if (currentFluidLevel != 0u) {
                        uint h00 = fluidCornerLevel(origin, 0u, 0u);
                        uint h10 = fluidCornerLevel(origin, 1u, 0u);
                        uint h01 = fluidCornerLevel(origin, 0u, 1u);
                        uint h11 = fluidCornerLevel(origin, 1u, 1u);
                        face.ao = packedAO | (h00 << 8u) | (h10 << 12u) |
                            (h01 << 16u) | (h11 << 20u);
                    }
                    else face.ao = packedAO;
                    face.opacity = blocks[faceId].opacity;
                    if (((faceKey >> 27u) & 1u) != 0u) translucentFaces.Append(face);
                    else opaqueFaces.Append(face);

                    for (uint yy = 0u; yy < height; ++yy)
                        for (uint xx = 0u; xx < width; ++xx)
                            faceMask[u + xx + 16u * (v + yy)] = 0u;
                    u += width;
                }
            }
        }
        GroupMemoryBarrierWithGroupSync();
    }
}
