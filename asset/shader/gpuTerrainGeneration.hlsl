struct ColumnData
{
    float surfaceHeight;
    float mountainWeight;
    float oceanWeight;
    float riverWeight;
    float aquiferLevel;
    float temperature;
    float moisture;
    uint biome;
};

cbuffer TerrainConstants : register(b0)
{
    int2 chunkOrigin;
    uint worldSeed;
    uint terrainPadding;
};

RWStructuredBuffer<ColumnData> outputColumns : register(u0);
StructuredBuffer<ColumnData> inputColumns : register(t0);
RWStructuredBuffer<uint> outputBlocks : register(u0);

uint hashValue(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

uint hash2(uint seed, int x, int z)
{
    return hashValue(seed ^ hashValue(asuint(x) + 0x9e3779b9u) ^ hashValue(asuint(z) + 0x85ebca6bu));
}

uint hash3(uint seed, int x, int y, int z)
{
    return hashValue(hash2(seed, x, z) ^ hashValue(asuint(y) + 0xc2b2ae35u));
}

float randomSigned(uint value)
{
    return (float(value & 0x00ffffffu) / 8388607.5) - 1.0;
}

float fadeNoise(float v) { return v * v * (3.0 - 2.0 * v); }

float noise2(float2 p, uint seed)
{
    int2 cell = (int2)floor(p);
    float2 f = frac(p);
    float2 s = float2(fadeNoise(f.x), fadeNoise(f.y));
    float a = randomSigned(hash2(seed, cell.x, cell.y));
    float b = randomSigned(hash2(seed, cell.x + 1, cell.y));
    float c = randomSigned(hash2(seed, cell.x, cell.y + 1));
    float d = randomSigned(hash2(seed, cell.x + 1, cell.y + 1));
    return lerp(lerp(a, b, s.x), lerp(c, d, s.x), s.y);
}

float noise3(float3 p, uint seed)
{
    int3 cell = (int3)floor(p);
    float3 f = frac(p);
    float3 s = float3(fadeNoise(f.x), fadeNoise(f.y), fadeNoise(f.z));
    float lower = lerp(
        lerp(randomSigned(hash3(seed,cell.x,cell.y,cell.z)), randomSigned(hash3(seed,cell.x+1,cell.y,cell.z)), s.x),
        lerp(randomSigned(hash3(seed,cell.x,cell.y,cell.z+1)), randomSigned(hash3(seed,cell.x+1,cell.y,cell.z+1)), s.x), s.z);
    float upper = lerp(
        lerp(randomSigned(hash3(seed,cell.x,cell.y+1,cell.z)), randomSigned(hash3(seed,cell.x+1,cell.y+1,cell.z)), s.x),
        lerp(randomSigned(hash3(seed,cell.x,cell.y+1,cell.z+1)), randomSigned(hash3(seed,cell.x+1,cell.y+1,cell.z+1)), s.x), s.z);
    return lerp(lower, upper, s.y);
}

float fractal2(float2 world, float frequency, uint octaves, uint seed)
{
    float2 p = world * frequency;
    float value = 0.0, amplitude = 1.0, total = 0.0;
    [loop] for (uint octave = 0; octave < octaves; ++octave) {
        value += noise2(p, seed + octave * 0x9e3779b9u) * amplitude;
        total += amplitude; p *= 2.0; amplitude *= 0.5;
    }
    return value / total;
}

float fractal3(float3 world, float3 frequency, uint octaves, uint seed)
{
    float3 p = world * frequency;
    float value = 0.0, amplitude = 1.0, total = 0.0;
    [loop] for (uint octave = 0; octave < octaves; ++octave) {
        value += noise3(p, seed + octave * 0xc2b2ae35u) * amplitude;
        total += amplitude; p *= 2.0; amplitude *= 0.5;
    }
    return value / total;
}

float smoothRange(float low, float high, float value)
{
    float t = saturate((value - low) / (high - low));
    return t * t * (3.0 - 2.0 * t);
}

float hashUnit(uint value)
{
    return float(value & 0x00ffffffu) / 16777215.0;
}

float3 tunnelAnchor(int2 grid)
{
    const float cellSize = 64.0;
    uint anchorHash = hash2(worldSeed + 1600u, grid.x, grid.y);
    return float3(
        (float(grid.x) + 0.5) * cellSize + (hashUnit(hashValue(anchorHash + 1u)) - 0.5) * 38.0,
        12.0 + hashUnit(hashValue(anchorHash + 2u)) * 36.0,
        (float(grid.y) + 0.5) * cellSize + (hashUnit(hashValue(anchorHash + 3u)) - 0.5) * 38.0);
}

float distanceToTunnelSegment(float3 position, float3 segmentStart, float3 segmentEnd)
{
    float3 direction = segmentEnd - segmentStart;
    float projection = saturate(dot(position - segmentStart, direction) /
        max(dot(direction, direction), 0.0001));
    return length(position - (segmentStart + direction * projection));
}

void tunnelControls(
    float3 start,
    float3 end,
    int2 origin,
    uint direction,
    out float3 firstControl,
    out float3 secondControl)
{
    uint curveHash = hash2(worldSeed + 1800u + direction * 37u, origin.x, origin.y);
    firstControl = lerp(start, end, 1.0 / 3.0) + float3(
        hashUnit(hashValue(curveHash + 1u)) - 0.5,
        hashUnit(hashValue(curveHash + 2u)) - 0.5,
        hashUnit(hashValue(curveHash + 3u)) - 0.5) * float3(48.0, 40.0, 48.0);
    secondControl = lerp(start, end, 2.0 / 3.0) + float3(
        hashUnit(hashValue(curveHash + 4u)) - 0.5,
        hashUnit(hashValue(curveHash + 5u)) - 0.5,
        hashUnit(hashValue(curveHash + 6u)) - 0.5) * float3(48.0, 40.0, 48.0);
}

float3 cubicTunnelPoint(
    float3 start,
    float3 firstControl,
    float3 secondControl,
    float3 end,
    float amount)
{
    float inverse = 1.0 - amount;
    return inverse * inverse * inverse * start +
        3.0 * inverse * inverse * amount * firstControl +
        3.0 * inverse * amount * amount * secondControl +
        amount * amount * amount * end;
}

float curvedTunnelField(
    float3 position,
    float3 start,
    float3 end,
    int2 origin,
    uint direction)
{
    const uint curveSteps = 8u;
    float3 firstControl;
    float3 secondControl;
    tunnelControls(start, end, origin, direction, firstControl, secondControl);
    uint shapeHash = hash2(worldSeed + 1700u + direction * 53u, origin.x, origin.y);
    float startWidth = 3.0 + hashUnit(hashValue(shapeHash + 1u)) * 4.5;
    float endWidth = 3.0 + hashUnit(hashValue(shapeHash + 2u)) * 4.5;
    float startHeight = 1.8 + hashUnit(hashValue(shapeHash + 3u)) * 4.4;
    float endHeight = 1.8 + hashUnit(hashValue(shapeHash + 4u)) * 4.4;
    float widthWave = (hashUnit(hashValue(shapeHash + 5u)) - 0.5) * 3.5;
    float heightWave = (hashUnit(hashValue(shapeHash + 6u)) - 0.5) * 3.0;
    float3 previous = start;
    float field = 0.0;
    [unroll]
    for (uint step = 1u; step <= curveSteps; ++step)
    {
        float segmentEnd = float(step) / float(curveSteps);
        float3 current = cubicTunnelPoint(start, firstControl, secondControl, end, segmentEnd);
        float3 directionVector = current - previous;
        float projection = saturate(dot(position - previous, directionVector) /
            max(dot(directionVector, directionVector), 0.0001));
        float amount = (float(step - 1u) + projection) / float(curveSteps);
        float wave = sin(amount * 3.14159265359);
        float horizontalRadius = max(2.5, lerp(startWidth, endWidth, amount) + wave * widthWave);
        float verticalRadius = max(1.6, lerp(startHeight, endHeight, amount) + wave * heightWave);
        float3 offset = position - (previous + directionVector * projection);
        float normalizedDistance = sqrt(
            dot(offset.xz, offset.xz) / (horizontalRadius * horizontalRadius) +
            offset.y * offset.y / (verticalRadius * verticalRadius));
        field = max(field, 1.0 - smoothRange(0.72, 1.08, normalizedDistance));
        previous = current;
    }
    return field;
}

float connectedTunnelField(int3 world)
{
    const float cellSize = 64.0;
    int2 cell = (int2)floor(float2(world.x, world.z) / cellSize);
    float3 samplePosition = float3(world);
    float3 center = tunnelAnchor(cell);
    float3 east = tunnelAnchor(cell + int2(1, 0));
    float3 west = tunnelAnchor(cell - int2(1, 0));
    float3 north = tunnelAnchor(cell + int2(0, 1));
    float3 south = tunnelAnchor(cell - int2(0, 1));
    float field = 0.0;
    field = max(field, curvedTunnelField(samplePosition, center, east, cell, 0u));
    field = max(field, curvedTunnelField(samplePosition, center, north, cell, 1u));
    field = max(field, curvedTunnelField(samplePosition, west, center, cell - int2(1, 0), 0u));
    field = max(field, curvedTunnelField(samplePosition, south, center, cell - int2(0, 1), 1u));
    return field;
}

float densityAt(ColumnData column, int3 world)
{
    float scale = lerp(15.0, 28.0, column.mountainWeight);
    float density = (column.surfaceHeight - world.y) / scale;
    density += fractal3(world, float3(0.0062,0.0085,0.0062), 4, worldSeed + 1100u) *
        lerp(0.32, 0.88, column.mountainWeight);
    density += fractal3(world, float3(0.018,0.015,0.018), 2, worldSeed + 1200u) *
        column.mountainWeight * 0.26;
    float depth = column.surfaceHeight - world.y;
    float caveWindow = smoothRange(5.0,18.0,depth) * smoothRange(4.0,16.0,world.y) *
        (1.0 - smoothRange(150.0,220.0,world.y));
    float cave = max(
        smoothRange(0.47,0.68,abs(fractal3(world, float3(0.014,0.019,0.014), 3, worldSeed + 1300u))),
        connectedTunnelField(world));
    density = lerp(density, min(density,-0.8), cave * caveWindow);
    density = lerp(1.35, density, smoothRange(0.0,7.0,world.y));
    density = lerp(density, -1.0, smoothRange(245.0,254.0,world.y));
    return density;
}

[numthreads(8, 8, 1)]
void generateColumns(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 16 || id.y >= 16) return;
    int2 world = chunkOrigin + int2(id.x, id.y);
    float continental = fractal2(world, 0.00072, 4, worldSeed + 100u);
    float erosion = fractal2(world, 0.00165, 4, worldSeed + 200u);
    float weirdness = fractal2(world, 0.0032, 3, worldSeed + 300u);
    float temperature = fractal2(world, 0.00058, 3, worldSeed + 400u);
    float moisture = fractal2(world, 0.00066, 3, worldSeed + 500u);
    float inland = smoothRange(-0.18,0.32,continental);
    float ridge = saturate(1.0 - abs(abs(weirdness) * 3.0 - 2.0));
    float mountain = saturate(inland * (1.0-smoothRange(-0.45,0.42,erosion)) * smoothRange(0.10,0.82,ridge));
    float ocean = 1.0 - smoothRange(-0.46,-0.12,continental);
    float riverNoise = abs(fractal2(world, 0.00145, 3, worldSeed + 600u));
    float river = (1.0-smoothRange(0.018,0.070,riverNoise)) * smoothRange(-0.20,0.03,continental) * (1.0-mountain*0.75);
    float baseHeight = 66.0 + continental * 30.0;
    float height = baseHeight + fractal2(world,0.0075,4,worldSeed+700u) * lerp(3.0,13.0,1.0-smoothRange(-0.25,0.55,erosion));
    height += mountain * mountain * 82.0;
    height = lerp(height,60.0,river*0.88);

    ColumnData column;
    column.surfaceHeight = clamp(height,8.0,245.0);
    column.mountainWeight = mountain;
    column.oceanWeight = ocean;
    column.riverWeight = river;
    float aquiferRegion = fractal2(world,0.0011,2,worldSeed+925u);
    column.aquiferLevel = aquiferRegion > 0.28
        ? 14.0 + fractal2(world,0.0028,2,worldSeed+900u)*7.0
        : -1.0;
    column.temperature = temperature;
    column.moisture = moisture;
    float alpine = max(smoothRange(0.55,0.88,mountain), smoothRange(120.0,165.0,column.surfaceHeight));
    float arid = smoothRange(0.18,0.62,temperature-moisture*0.72);
    if (column.surfaceHeight < 62.0 || ocean > 0.58) column.biome = 4u;
    else if (alpine > 0.52) column.biome = 3u;
    else if (arid > 0.54) column.biome = 2u;
    else if (smoothRange(0.18,0.62,mountain) > 0.40) column.biome = 1u;
    else if (moisture > 0.08) column.biome = 5u;
    else column.biome = 0u;
    outputColumns[id.x + 16u * id.y] = column;
}

[numthreads(8, 8, 8)]
void generateBlocks(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 16 || id.y >= 255 || id.z >= 16) return;
    uint index = id.x + 16u * (id.z + 16u * id.y);
    ColumnData column = inputColumns[id.x + 16u * id.z];
    int3 world = int3(chunkOrigin.x + id.x, id.y, chunkOrigin.y + id.z);
    float density = densityAt(column, world);
    uint block = 0u;
    if (density > 0.0) {
        bool exposed = densityAt(column, world + int3(0,1,0)) <= 0.0;
        float depth = column.surfaceHeight - id.y;
        block = 1u;
        if (exposed && depth < 12.0) {
            uint random = hash3(worldSeed+1600u,world.x,world.y,world.z);
            if (id.y <= 63) block = (random % 7u == 0u) ? 9u : 6u;
            else if (column.biome == 2u) block = 6u;
            else if (column.biome == 3u && id.y >= 116 && column.temperature < 0.30) block = 7u;
            else if (column.biome == 1u && column.mountainWeight > 0.68 && (random&3u)!=0u) block = 1u;
            else block = 3u;
        }
        else if (depth < 5.0 && id.y > 63) block = column.biome == 2u ? 6u : 2u;
        if (id.y < 32 && block == 1u) block = 8u;
    }
    else if (id.y <= 63 &&
        ((column.surfaceHeight < 63.0 && float(id.y) >= column.surfaceHeight - 1.0) ||
            float(id.y) <= column.aquiferLevel)) block = 5u;
    if (id.y == 0 || (id.y < 5 && hash3(worldSeed+1800u,world.x,world.y,world.z)%5u >= id.y)) block = 4u;
    outputBlocks[index] = block;
}

[numthreads(1, 1, 1)]
void placeTrees(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 4 || id.z >= 4) return;
    uint columnIndex = (id.x*4u+2u) + 16u*(id.z*4u+2u);
    ColumnData column = inputColumns[columnIndex];
    if (column.biome != 5u || column.surfaceHeight <= 64.0 || column.riverWeight > 0.65) return;
    uint random = hash2(worldSeed+1900u,chunkOrigin.x/8+(int)id.x,chunkOrigin.y/8+(int)id.z);
    if ((random & 3u) != 0u) return;
    uint x = id.x*4u+2u;
    uint z = id.z*4u+2u;
    uint ground = (uint)clamp(round(column.surfaceHeight),2.0,247.0);
    uint height = 4u + ((random>>8)&1u);
    [loop] for (uint y=ground+1u; y<=ground+height; ++y)
        outputBlocks[x + 16u*(z + 16u*y)] = 10u;
    uint crown=ground+height;
    [loop] for (int dy=-2; dy<=1; ++dy)
        [loop] for (int dz=-2; dz<=2; ++dz)
            [loop] for (int dx=-2; dx<=2; ++dx) {
                int3 p=int3(x+dx,crown+dy,z+dz);
                if (p.x<0||p.x>=16||p.z<0||p.z>=16||p.y<1||p.y>=255) continue;
                if (abs(dx)==2&&abs(dz)==2) continue;
                uint at=p.x+16u*(p.z+16u*p.y);
                uint previous;
                InterlockedCompareExchange(outputBlocks[at],0u,11u,previous);
            }
}
