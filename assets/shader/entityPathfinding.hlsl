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

uint relaxNeighbor(uint best, uint currentCell, int x, int z) {
    if (x < 0 || z < 0 || x >= int(width) || z >= int(height)) return best;
    uint index = uint(z) * width + uint(x);
    uint neighbor = cells[index];
    if ((neighbor & 0x80000000u) == 0u) return best;
    int currentY = int(currentCell & 0xffffu) - 32768;
    int neighborY = int(neighbor & 0xffffu) - 32768;
    if (abs(currentY - neighborY) > 1) return best;
    uint cost = inputCosts[index];
    return cost < INF ? min(best, cost + 1u) : best;
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
    best = relaxNeighbor(best, cell, x - 1, z);
    best = relaxNeighbor(best, cell, x + 1, z);
    best = relaxNeighbor(best, cell, x, z - 1);
    best = relaxNeighbor(best, cell, x, z + 1);
    outputCosts[index] = best;
}
