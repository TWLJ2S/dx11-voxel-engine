StructuredBuffer<uint> inputCosts : register(t0);
StructuredBuffer<uint> cells : register(t1);
RWStructuredBuffer<uint> outputCosts : register(u0);

cbuffer PathConstants : register(b0) {
    uint width;
    uint height;
    uint cellCount;
    uint unused;
};

static const uint INF = 0x3fffffffu;

bool canStep(uint currentCell, int x, int z) {
    if (x < 0 || z < 0 || x >= int(width) || z >= int(height)) return false;
    uint neighbor = cells[uint(z) * width + uint(x)];
    if ((neighbor & 0x80000000u) == 0u) return false;
    int currentY = int(currentCell & 0xffffu) - 32768;
    int neighborY = int(neighbor & 0xffffu) - 32768;
    return abs(currentY - neighborY) <= 1;
}

uint relaxNeighbor(uint best, uint currentCell, int currentX, int currentZ,
    int x, int z, uint moveCost) {
    if (x < 0 || z < 0 || x >= int(width) || z >= int(height)) return best;
    uint index = uint(z) * width + uint(x);
    if (!canStep(currentCell, x, z)) return best;
    int dx = x - currentX;
    int dz = z - currentZ;
    // A diagonal may not cut through the touching corners of two solid cells.
    if (dx != 0 && dz != 0 &&
        (!canStep(currentCell, currentX + dx, currentZ) ||
         !canStep(currentCell, currentX, currentZ + dz))) return best;
    uint cost = inputCosts[index];
    return cost < INF ? min(best, cost + moveCost) : best;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID) {
    uint index = dispatchId.x;
    if (index >= cellCount) return;
    uint cell = cells[index];
    if ((cell & 0x80000000u) == 0u) {
        outputCosts[index] = INF;
        return;
    }
    int x = int(index % width);
    int z = int(index / width);
    uint best = inputCosts[index];
    best = relaxNeighbor(best, cell, x, z, x - 1, z, 10u);
    best = relaxNeighbor(best, cell, x, z, x + 1, z, 10u);
    best = relaxNeighbor(best, cell, x, z, x, z - 1, 10u);
    best = relaxNeighbor(best, cell, x, z, x, z + 1, 10u);
    best = relaxNeighbor(best, cell, x, z, x - 1, z - 1, 14u);
    best = relaxNeighbor(best, cell, x, z, x + 1, z - 1, 14u);
    best = relaxNeighbor(best, cell, x, z, x - 1, z + 1, 14u);
    best = relaxNeighbor(best, cell, x, z, x + 1, z + 1, 14u);
    outputCosts[index] = best;
}
