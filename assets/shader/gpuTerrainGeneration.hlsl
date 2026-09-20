struct ColumnData
{
    float surfaceHeight;
    float mountainWeight;
    float oceanWeight;
    float riverWeight;
    float aquiferLevel;
    float temperature;
    float moisture;
	float lakeWeight;
	float lakeSurface;
    float erosion;
    uint biome;
	float padding;
};

struct BiomeDefinition
{
    uint topBlock;
    uint underBlock;
    uint floodedTopBlock;
    uint floodedUnderBlock;
    uint soilDepth;
    uint floodedSoilDepth;
    uint waterKind;
    uint flags;
    uint treeKind;
    uint alternateTreeKind;
    uint treeChanceNumerator;
    uint treeChanceDenominator;
    uint alternateChanceNumerator;
    uint alternateChanceDenominator;
    float alpineRockDepth;
    float highlandRockDepth;
};

cbuffer TerrainConstants : register(b0)
{
    int2 chunkOrigin;
    uint worldSeed;
    uint terrainPadding;
    uint AIR_BLOCK;
    uint STONE_BLOCK;
    uint DIRT_BLOCK;
    uint GRASS_BLOCK;
    uint BEDROCK_BLOCK;
    uint WATER_BLOCK;
    uint SAND_BLOCK;
    uint SNOW_BLOCK;
    uint DEEPSLATE_BLOCK;
    uint GRAVEL_BLOCK;
    uint LOG_BLOCK;
    uint LEAVES_BLOCK;
    uint ANDESITE_BLOCK;
    uint GRANITE_BLOCK;
    uint DIORITE_BLOCK;
    uint CALCITE_BLOCK;
    uint TUFF_BLOCK;
    uint SANDSTONE_BLOCK;
    uint RED_SAND_BLOCK;
    uint RED_SANDSTONE_BLOCK;
    uint CLAY_BLOCK;
    uint PACKED_ICE_BLOCK;
    uint ICE_BLOCK;
    uint PODZOL_BLOCK;
    uint COARSE_DIRT_BLOCK;
    uint MOSS_BLOCK;
    uint MUD_BLOCK;
    uint TERRACOTTA_BLOCK;
    uint SPRUCE_LOG_BLOCK;
    uint SPRUCE_LEAVES_BLOCK;
    uint BIRCH_LOG_BLOCK;
    uint BIRCH_LEAVES_BLOCK;
    uint JUNGLE_LOG_BLOCK;
    uint JUNGLE_LEAVES_BLOCK;
    uint ACACIA_LOG_BLOCK;
    uint ACACIA_LEAVES_BLOCK;
    uint DARK_OAK_LOG_BLOCK;
    uint DARK_OAK_LEAVES_BLOCK;
    uint COAL_ORE_BLOCK;
    uint IRON_ORE_BLOCK;
    uint GOLD_ORE_BLOCK;
    uint DIAMOND_ORE_BLOCK;
    uint LAPIS_ORE_BLOCK;
    uint COPPER_ORE_BLOCK;
    uint EMERALD_ORE_BLOCK;
    uint REDSTONE_ORE_BLOCK;
    uint PRISMARINE_BLOCK;
    uint SPONGE_BLOCK;
    uint DEEPSLATE_COAL_ORE_BLOCK;
    uint DEEPSLATE_IRON_ORE_BLOCK;
    uint DEEPSLATE_GOLD_ORE_BLOCK;
    uint DEEPSLATE_DIAMOND_ORE_BLOCK;
    uint DEEPSLATE_LAPIS_ORE_BLOCK;
    uint DEEPSLATE_COPPER_ORE_BLOCK;
    uint DEEPSLATE_EMERALD_ORE_BLOCK;
    uint DEEPSLATE_REDSTONE_ORE_BLOCK;
    BiomeDefinition BIOMES[13];
};

RWStructuredBuffer<ColumnData> outputColumns : register(u0);
StructuredBuffer<ColumnData> inputColumns : register(t0);
RWStructuredBuffer<uint> outputBlocks : register(u0);

static const int ColumnHalo = 1;
static const int ColumnGrid = 18;

int packedColumnIndex(int localX, int localZ)
{
    return (localX + ColumnHalo) + ColumnGrid * (localZ + ColumnHalo);
}

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

float fractal2Aniso(float2 world, float frequencyX, float frequencyZ, uint octaves, uint seed)
{
    float2 p = float2(world.x * frequencyX, world.y * frequencyZ);
    float value = 0.0, amplitude = 1.0, total = 0.0;
    [loop] for (uint octave = 0; octave < octaves; ++octave) {
        value += noise2(p, seed + octave * 0x9e3779b9u) * amplitude;
        total += amplitude; p *= 2.0; amplitude *= 0.5;
    }
    return value / total;
}

float fractal2(float2 world, float frequency, uint octaves, uint seed)
{
    return fractal2Aniso(world, frequency, frequency, octaves, seed);
}

float ridgedFractal2(float2 world, float frequencyX, float frequencyZ, uint octaves, uint seed)
{
    float2 p = float2(world.x * frequencyX, world.y * frequencyZ);
    float value = 0.0, amplitude = 1.0, total = 0.0, weight = 1.0;
    [loop] for (uint octave = 0; octave < octaves; ++octave) {
        float sample = 1.0 - abs(noise2(p, seed + octave * 0x9e3779b9u));
        sample = sample * sample * weight;
        value += sample * amplitude;
        total += amplitude;
        weight = saturate(sample * 2.0);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return total > 0.0 ? value / total : 0.0;
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

static const uint BlockTypeMask = 4095u;
static const uint FluidLevelShift = 12u;
static const uint MaximumFluidLevel = 8u;

uint blockType(uint state) { return state & BlockTypeMask; }

uint fluidLevel(uint state)
{
    if (blockType(state) != WATER_BLOCK) return 0u;
    uint stored = (state >> FluidLevelShift) & 7u;
    return stored == 0u ? MaximumFluidLevel : stored;
}

uint waterState(uint level)
{
    level = clamp(level, 1u, MaximumFluidLevel);
    uint stored = level == MaximumFluidLevel ? 0u : level;
    return WATER_BLOCK | (stored << FluidLevelShift);
}

static const uint WaterNone = 0u;
static const uint WaterOcean = 1u;
static const uint WaterLake = 2u;
static const uint WaterRiver = 3u;
static const uint WaterWetland = 4u;
static const uint TreeNone = 0xffffffffu;
static const uint BiomeGlobalSnow = 1u;
static const uint BiomeAlpineSurface = 2u;
static const uint BiomeHighlandRock = 4u;
static const uint BiomeFloodedSurface = 8u;

void columnWater(ColumnData column, out float surface, out uint kind)
{
    surface = column.aquiferLevel;
    kind = WaterNone;
    uint biomeWater = BIOMES[column.biome].waterKind;
    if (biomeWater == WaterOcean || column.oceanWeight > 0.24) {
        surface = max(surface, 64.0);
        kind = WaterOcean;
    }
    if (column.oceanWeight <= 0.24 &&
        (biomeWater == WaterLake || column.lakeWeight > 0.10)) {
        if (column.lakeSurface >= surface) {
            surface = column.lakeSurface;
            kind = WaterLake;
        }
    }
    if (column.oceanWeight <= 0.24 &&
        column.lakeWeight <= 0.10 &&
        column.riverWeight > 0.10) {
        if (64.0 >= surface) {
            surface = 64.0;
            kind = WaterRiver;
        }
    }
    if (kind == WaterNone && biomeWater == WaterWetland) {
        surface = max(surface, 64.0);
        kind = WaterWetland;
    }
}

void mergeWater(inout float surface, inout uint kind, float neighborSurface, uint neighborKind)
{
    if (neighborSurface > surface + 0.01) {
        surface = neighborSurface;
        kind = neighborKind;
    }
    else if (neighborSurface >= surface - 0.01 &&
        neighborKind != WaterNone &&
        (kind == WaterNone || neighborKind < kind)) {
        surface = max(surface, neighborSurface);
        kind = neighborKind;
    }
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

float entranceSegmentField(
    float3 world,
    float3 start,
    float3 end,
    float startWidth,
    float startHeight,
    float endWidth,
    float endHeight,
    float shapeWave,
    uint style)
{
    float3 direction = end - start;
    float projection = saturate(dot(world - start, direction) /
        max(dot(direction, direction), 0.0001));
    float wave = 1.0 + shapeWave * sin(
        projection * 3.14159265359 * (style == 2u ? 3.0 : 2.0));
    float width = max(1.4, lerp(startWidth, endWidth, projection) * wave);
    float height = max(1.4, lerp(startHeight, endHeight, projection) / max(wave, 0.55));
    float3 offset = world - (start + direction * projection);
    float normalizedDistance = sqrt(
        dot(offset.xz, offset.xz) / (width * width) +
        offset.y * offset.y / (height * height));
    return 1.0 - smoothRange(0.70, 1.10, normalizedDistance);
}

float oneSurfaceEntrance(float3 world, float surfaceHeight, float mountainWeight, int2 cell)
{
    uint entranceHash = hash2(worldSeed + 2100u, cell.x, cell.y);
    if (hashUnit(hashValue(entranceHash)) > 0.28)
        return 0.0;

    const float cellSize = 64.0;
    float mouthX = (float(cell.x) + 0.5) * cellSize +
        (hashUnit(hashValue(entranceHash + 1u)) - 0.5) * 28.0;
    float mouthZ = (float(cell.y) + 0.5) * cellSize +
        (hashUnit(hashValue(entranceHash + 3u)) - 0.5) * 28.0;
    uint style = (uint)(hashUnit(hashValue(entranceHash + 6u)) * 3.9999);
    float sizeA = hashUnit(hashValue(entranceHash + 4u));
    float sizeB = hashUnit(hashValue(entranceHash + 5u));
    float mouthWidth = 4.5 + sizeA * 3.5;
    float mouthHeight = mouthWidth * (0.78 + sizeB * 0.22);
    if (style == 1u) {
        mouthWidth = 3.4 + sizeA * 3.8;
        mouthHeight = 4.5 + sizeB * 5.0;
    }
    else if (style == 2u) {
        mouthWidth = 1.9 + sizeA * 2.3;
        mouthHeight = 7.0 + sizeB * 7.0;
    }
    else if (style == 3u) {
        mouthWidth = 7.0 + sizeA * 5.0;
        mouthHeight = 3.0 + sizeB * 3.2;
    }

    float yaw = hashUnit(hashValue(entranceHash + 7u)) * 6.28318530718;
    float2 heading = float2(cos(yaw), sin(yaw));
    float run = 14.0 + hashUnit(hashValue(entranceHash + 8u)) * 28.0;
    float drop = 12.0 + hashUnit(hashValue(entranceHash + 9u)) * 16.0;
    if (style == 1u) drop = 5.0 + hashUnit(hashValue(entranceHash + 9u)) * 9.0;
    else if (style == 2u) drop = 8.0 + hashUnit(hashValue(entranceHash + 9u)) * 18.0;
    else if (style == 3u) drop = 2.0 + hashUnit(hashValue(entranceHash + 9u)) * 7.0;

    // Mountain portals sit lower on slopes, while scaling the offset by
    // portal height keeps low, broad entrances from being buried entirely.
    float sideDepth = style == 0u ? -2.0 :
        mouthHeight * (0.24 + saturate(mountainWeight) * 0.42) +
        hashUnit(hashValue(entranceHash + 10u)) * 0.75;
    float3 mouth = float3(mouthX, surfaceHeight - sideDepth, mouthZ);
    float3 bend = float3(mouthX + heading.x * run, mouth.y - drop,
        mouthZ + heading.y * run);
    float3 anchor = tunnelAnchor(cell);
    float3 bottom = float3(anchor.x, max(12.0, anchor.y), anchor.z);
    float shaftWidth = 2.7 + hashUnit(hashValue(entranceHash + 11u)) * 2.8;
    float shaftHeight = 2.2 + hashUnit(hashValue(entranceHash + 12u)) * 3.8;
    float pad = max(mouthWidth, mouthHeight) + 2.0;
    if (world.x < min(mouth.x, min(bend.x, bottom.x)) - pad ||
        world.x > max(mouth.x, max(bend.x, bottom.x)) + pad ||
        world.z < min(mouth.z, min(bend.z, bottom.z)) - pad ||
        world.z > max(mouth.z, max(bend.z, bottom.z)) + pad)
        return 0.0;

    float shapeWave = (hashUnit(hashValue(entranceHash + 13u)) - 0.5) * 0.42;
    float field = entranceSegmentField(
        world, mouth, bend, mouthWidth, mouthHeight,
        shaftWidth * 1.20, shaftHeight * 1.20, shapeWave, style);
    field = max(field, entranceSegmentField(
        world, bend, bottom, shaftWidth * 1.20, shaftHeight * 1.20,
        shaftWidth, shaftHeight, -shapeWave * 0.65, style));

    if (style == 0u) {
        float planar = length(world.xz - float2(mouthX, mouthZ));
        float bowl =
            (1.0 - smoothRange(mouthWidth * 0.45, mouthWidth * 1.15, planar)) *
            (1.0 - smoothRange(mouthHeight * 0.55, mouthHeight * 1.30,
                abs(world.y - surfaceHeight)));
        field = max(field, bowl);
    }
    else {
        float2 delta = world.xz - float2(mouthX, mouthZ);
        float along = dot(delta, heading);
        float lateral = dot(delta, float2(-heading.y, heading.x));
        float vertical = world.y - mouth.y;
        float portalDepth = mouthWidth * 1.45;
        float portalDistance = sqrt(
            lateral * lateral / (mouthWidth * mouthWidth) +
            vertical * vertical / (mouthHeight * mouthHeight) +
            along * along / (portalDepth * portalDepth));
        field = max(field, 1.0 - smoothRange(0.68, 1.10, portalDistance));
    }
    field *= smoothRange(8.0, 14.0, world.y);
    field *= 1.0 - smoothRange(surfaceHeight + 2.0, surfaceHeight + 6.0, world.y);
    return field;
}

float surfaceEntranceField(ColumnData column, int3 world)
{
    if (BIOMES[column.biome].waterKind == WaterOcean ||
        BIOMES[column.biome].waterKind == WaterLake)
        return 0.0;
    if (column.oceanWeight > 0.35 || column.riverWeight > 0.20 || column.lakeWeight > 0.16)
        return 0.0;
    if (column.surfaceHeight < 65.0)
        return 0.0;

    int2 cell = (int2)floor(float2(world.x, world.z) / 64.0);
    float field = 0.0;
    [unroll]
    for (int dz = -1; dz <= 1; ++dz)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
            field = max(field, oneSurfaceEntrance(
                float3(world), column.surfaceHeight, column.mountainWeight,
                cell + int2(dx, dz)));
    }
    return field;
}

float densityAt(ColumnData column, int3 world)
{
    float scale = lerp(18.0, 52.0, column.mountainWeight);
    float3 worldPosition = float3(world);
    float3 warp = float3(
        fractal3(worldPosition, float3(0.0034,0.0038,0.0026), 2, worldSeed + 1010u),
        fractal3(worldPosition, float3(0.0037,0.0030,0.0037), 2, worldSeed + 1020u),
        fractal3(worldPosition, float3(0.0026,0.0036,0.0034), 2, worldSeed + 1030u));
    float3 warpedPosition = worldPosition + warp * float3(14.0,10.0,18.0);
    float terrainShape = fractal3(
        warpedPosition, float3(0.0085,0.0110,0.0085), 3, worldSeed + 1100u);
    float detailShape = fractal3(
        warpedPosition, float3(0.0240,0.0300,0.0240), 2, worldSeed + 1200u);
    float ridgeShape = 1.0 - abs(fractal3(
        warpedPosition, float3(0.0110,0.0160,0.0110), 3, worldSeed + 1250u));

    // Climate/column height is only the large-scale vertical bias. The actual
    // solid boundary is this domain-warped XYZ isosurface.
    float density = ((column.surfaceHeight - world.y) / scale) * 0.68;
    density += terrainShape * lerp(0.20,0.58,column.mountainWeight);
    density += detailShape * lerp(0.06,0.20,column.mountainWeight);
    density += (ridgeShape * 2.0 - 1.0) * column.mountainWeight * 0.22;
    float depth = column.surfaceHeight - world.y;
    float caveWindow = smoothRange(4.0,22.0,depth) * smoothRange(2.0,12.0,world.y) *
        (1.0 - smoothRange(200.0,280.0,world.y));
    if (caveWindow > 0.0) {
        float cave = max(
            smoothRange(0.47,0.68,abs(fractal3(world, float3(0.014,0.019,0.014), 3, worldSeed + 1300u))),
            connectedTunnelField(world));
        density = lerp(density, min(density,-0.8), cave * caveWindow);
    }
    float entrance = surfaceEntranceField(column, world);
    if (entrance > 0.0)
        density = lerp(density, min(density, -0.85), entrance);
    density = lerp(1.35, density, smoothRange(0.0,7.0,world.y));
    density = lerp(density, -1.0, smoothRange(374.0,383.0,world.y));
    return density;
}

void fillBiomeWeights(
    float height,
    float mountain,
    float oceanWeight,
    float lakeWeight,
    float temperature,
    float moisture,
    out float weights[13])
{
    [unroll] for (uint i = 0u; i < 13u; ++i) weights[i] = 0.0;
    float ocean = saturate(oceanWeight);
    float land = 1.0 - smoothRange(0.42, 0.64, ocean);
    float alpine = max(smoothRange(0.40, 0.78, mountain), smoothRange(130.0, 190.0, height));
    float arid = smoothRange(0.18, 0.62, temperature - moisture * 0.72);
    float highland = smoothRange(0.14, 0.55, mountain) * (1.0 - alpine);
    float hot = smoothRange(0.08, 0.48, temperature);
    float cold = 1.0 - smoothRange(-0.20, 0.06, temperature);
    float wet = smoothRange(-0.06, 0.30, moisture);
    float dry = 1.0 - wet;
    float lowland = 1.0 - smoothRange(0.18, 0.52, mountain);
    float beach = (1.0 - alpine) * smoothRange(0.10, 0.26, ocean) * (1.0 - smoothRange(0.42, 0.64, ocean));
    float lake = (1.0 - alpine) * land * smoothRange(0.14, 0.32, lakeWeight);
    weights[4] = smoothRange(0.42, 0.64, ocean);
    weights[10] = beach * 2.4;
    weights[12] = lake * 2.8;
    weights[3] = land * alpine;
    weights[11] = land * cold * dry * (1.0 - alpine * 0.7);
    weights[6] = land * cold * wet * (1.0 - alpine);
    weights[2] = land * arid * hot * dry * (1.0 - alpine);
    weights[8] = land * hot * dry * (1.0 - arid * 0.55) * lowland * (1.0 - alpine);
    weights[9] = land * hot * wet * lowland * (1.0 - alpine);
    weights[7] = land * wet * lowland * (1.0 - hot) * (1.0 - alpine) * (1.0 - smoothRange(72.0, 84.0, height)) * (1.0 - lake);
    weights[1] = land * highland;
    weights[5] = land * wet * (1.0 - cold) * (1.0 - hot * 0.45) * (1.0 - alpine);
    weights[0] = land * (1.0 - alpine) * (1.0 - arid) * (1.0 - wet * 0.4) * (1.0 - cold * 0.45) * (1.0 - beach) * (1.0 - lake);
    weights[5] *= (1.0 - lake);
    weights[9] *= (1.0 - lake);
    weights[8] *= (1.0 - lake);
    weights[6] *= (1.0 - lake);
    weights[11] *= (1.0 - lake);
    weights[1] *= (1.0 - lake);
    float total = 0.0;
    [unroll] for (uint j = 0u; j < 13u; ++j) {
        if (weights[j] < 0.045) weights[j] = 0.0;
        total += weights[j];
    }
    if (total < 0.0001) {
        weights[4] = ocean;
        weights[0] = land;
        total = 1.0;
    }
    [unroll] for (uint k = 0u; k < 13u; ++k)
        weights[k] /= total;
}

uint dominantBiome(float weights[13])
{
    uint best = 0u;
    float bestWeight = -1.0;
    [unroll] for (uint i = 0u; i < 13u; ++i) {
        if (weights[i] > bestWeight) {
            bestWeight = weights[i];
            best = i;
        }
    }
    return best;
}

float snowLineY(float temperature)
{
    return lerp(110.0, 185.0, smoothRange(-0.55, 0.55, temperature));
}

[numthreads(8, 8, 1)]
void generateColumns(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 18 || id.y >= 18) return;
    int2 world = chunkOrigin + int2(id.x, id.y) - 1;
    float2 climate = world + float2(
        fractal2Aniso(world, 0.00085, 0.00135, 3, worldSeed + 40u),
        fractal2Aniso(world, 0.00115, 0.00075, 3, worldSeed + 71u)) * 32.0 +
        float2(
            fractal2Aniso(world, 0.0031, 0.0022, 2, worldSeed + 41u),
            fractal2Aniso(world, 0.0026, 0.0034, 2, worldSeed + 72u)) * 12.0;
    float continentalSmooth = fractal2Aniso(climate, 0.00018, 0.00026, 5, worldSeed + 100u);
    float continentalRidged = ridgedFractal2(climate, 0.00022, 0.00014, 4, worldSeed + 105u) * 2.0 - 1.0;
    float continental = continentalSmooth * 0.68 + continentalRidged * 0.32;
    float erosion = fractal2Aniso(climate, 0.00038, 0.00028, 5, worldSeed + 200u);
    float weirdness =
        fractal2Aniso(climate, 0.00072, 0.00048, 4, worldSeed + 300u) * 0.55 +
        (ridgedFractal2(climate, 0.00095, 0.00155, 4, worldSeed + 305u) * 2.0 - 1.0) * 0.45;
    float temperature = fractal2Aniso(climate, 0.00016, 0.00022, 4, worldSeed + 400u);
    float moisture = fractal2Aniso(climate, 0.00017, 0.00020, 4, worldSeed + 500u);
    float inland = smoothRange(-0.18,0.32,continental);
    float ridge = saturate(1.0 - abs(abs(weirdness) * 3.0 - 2.0));
    float mountain = saturate(inland * (1.0-smoothRange(-0.45,0.42,erosion)) * smoothRange(0.32,0.78,ridge));
    float ocean = 1.0 - smoothRange(-0.80, 0.16, continental);
    float riverNoise = abs(fractal2Aniso(world, 0.0022, 0.0031, 4, worldSeed + 600u));
    float river = (1.0-smoothRange(0.010,0.038,riverNoise)) * smoothRange(-0.20,0.03,continental) * (1.0-mountain*0.75);
    float lakeField = ridgedFractal2(world, 0.0026, 0.0012, 4, worldSeed + 650u);
    float lakeMask = fractal2Aniso(world, 0.0010, 0.0017, 3, worldSeed + 660u);
    float lake = smoothRange(0.62,0.86,lakeField) * smoothRange(0.08,0.38,lakeMask) *
        smoothRange(-0.08,0.28,continental) * (1.0-mountain) * (1.0-river) *
        (1.0 - smoothRange(0.22, 0.36, ocean));
    float lakeSurface = 68.0 + fractal2Aniso(world,0.0011,0.0007,2,worldSeed+675u)*4.0;
    float baseHeight;
    if (continental < -0.70) baseHeight = lerp(6.0, 28.0, smoothRange(-1.0, -0.70, continental));
    else if (continental < -0.35) baseHeight = lerp(28.0, 52.0, smoothRange(-0.70, -0.35, continental));
    else if (continental < -0.05) baseHeight = lerp(52.0, 64.0, smoothRange(-0.35, -0.05, continental));
    else if (continental < 0.25) baseHeight = lerp(64.0, 86.0, smoothRange(-0.05, 0.25, continental));
    else if (continental < 0.55) baseHeight = lerp(86.0, 112.0, smoothRange(0.25, 0.55, continental));
    else baseHeight = lerp(112.0, 138.0, smoothRange(0.55, 1.0, continental));
    float relief = 1.0 - smoothRange(-0.20, 0.55, erosion);
    float aridness = smoothRange(0.15, 0.55, temperature - moisture * 0.72);
    float dryness = 1.0 - smoothRange(-0.08, 0.28, moisture);
    float flatClimate = max(aridness, dryness * (1.0 - aridness * 0.35));
    float lowlandFlat = saturate((1.0 - mountain) * smoothRange(-0.20, 0.50, erosion) * lerp(0.45, 1.0, flatClimate));
    float height = baseHeight + fractal2Aniso(world,0.0065,0.0105,4,worldSeed+700u) * lerp(4.0,22.0,relief) * lerp(1.0,0.08,lowlandFlat);
    height += fractal2Aniso(world,0.0032,0.0020,3,worldSeed+730u) * lerp(-6.0,8.0,inland) * lerp(1.0,0.10,lowlandFlat);
    height += fractal2Aniso(world,0.024,0.017,2,worldSeed+800u) * lerp(0.6,3.5,mountain) * lerp(1.0,0.25,lowlandFlat);
    float rangeRidges = ridgedFractal2(world, 0.00135, 0.00225, 5, worldSeed + 760u);
    height += mountain * lerp(28.0,105.0,ridge);
    height += mountain * rangeRidges * lerp(18.0,78.0,ridge);
    float flatTarget = lerp(66.0, 74.0, inland * 0.55);
    height = lerp(height, flatTarget, lowlandFlat * 0.62 * (1.0 - ocean));
    float shoreBlend = smoothRange(0.04, 0.58, ocean);
    float deepBlend = smoothRange(0.32, 0.90, ocean);
    float shelfHeight = lerp(68.0, 64.0, shoreBlend);
    height = lerp(height, shelfHeight, shoreBlend * (1.0 - deepBlend * 0.85));
    height = lerp(height, baseHeight, deepBlend);
    height = lerp(height,60.0,river*0.88);
    float lakeShore = smoothRange(0.10, 0.36, lake);
    float lakeDeep = smoothRange(0.32, 0.72, lake);
    height = lerp(height, lakeSurface - 1.5, lakeShore * (1.0 - lakeDeep));
    height = lerp(height, lakeSurface - 6.0, lakeDeep);
    float wetlandWant = (1.0 - mountain) * (1.0 - ocean) * (1.0 - lake) * (1.0 - river) *
        smoothRange(-0.06, 0.30, moisture) * (1.0 - smoothRange(0.08, 0.48, temperature));
    height = lerp(height, 62.5, saturate(wetlandWant * 1.65));

    ColumnData column;
    column.surfaceHeight = clamp(height,4.0,374.0);
    column.mountainWeight = mountain;
    column.oceanWeight = ocean;
    column.riverWeight = river;
    float aquiferRegion = fractal2(world,0.0011,2,worldSeed+925u);
    column.aquiferLevel = aquiferRegion > 0.28
        ? 8.0 + fractal2(world,0.0028,2,worldSeed+900u)*10.0
        : -1.0;
    column.temperature = temperature;
    column.moisture = moisture;
    column.lakeWeight = lake;
    column.lakeSurface = lakeSurface;
    column.erosion = erosion;
    column.padding = 0.0;
    float weights[13];
    fillBiomeWeights(column.surfaceHeight, mountain, ocean, lake, temperature, moisture, weights);
    column.biome = dominantBiome(weights);
    if (column.oceanWeight > 0.24) {
        if (column.surfaceHeight >= 64.0 && column.oceanWeight < 0.50)
            column.biome = 10u;
        else
            column.biome = 4u;
    }
    else if (column.lakeWeight > 0.10)
        column.biome = 12u;
    outputColumns[id.x + 18u * id.y] = column;
}

float oreTriangle(int y, int lo, int peak, int hi)
{
    if (y <= lo || y >= hi) return 0.0;
    if (y == peak) return 1.0;
    if (y < peak) return float(y - lo) / float(peak - lo);
    return float(hi - y) / float(hi - peak);
}

int floorDivInt(int value, int divisor)
{
    int quotient = value / divisor;
    if (value % divisor < 0) --quotient;
    return quotient;
}

bool inLithologyBlob(uint salt, int3 world, int cellXZ, int cellY, float spawnChance, float radiusMin, float radiusMax)
{
    if (spawnChance <= 0.0) return false;
    int cx = floorDivInt(world.x, cellXZ);
    int cy = floorDivInt(world.y, cellY);
    int cz = floorDivInt(world.z, cellXZ);
    uint cell = hash3(worldSeed + salt, cx, cy, cz);
    if (float(cell % 10000u) >= spawnChance * 10000.0) return false;

    float ox = float((cell >> 8) % 997u) / 997.0;
    float oy = float((cell >> 18) % 991u) / 991.0;
    float oz = float((cell >> 28) % 983u) / 983.0;
    float stretch = 0.55 + 0.90 * float((cell >> 4) % 1000u) / 1000.0;
    float rx = radiusMin + (radiusMax - radiusMin) * float((cell >> 8) % 1000u) / 1000.0;
    float ry = (radiusMin * 0.55 + (radiusMax - radiusMin) * 0.45 * float((cell >> 14) % 1000u) / 1000.0) * stretch;
    float rz = radiusMin + (radiusMax - radiusMin) * float((cell >> 20) % 1000u) / 1000.0;

    float3 center = float3(
        float(cx * cellXZ) + ox * float(cellXZ),
        float(cy * cellY) + oy * float(cellY),
        float(cz * cellXZ) + oz * float(cellXZ));
    float3 d = (float3(world) + 0.5 - center) / max(float3(rx, ry, rz), float3(0.35, 0.25, 0.35));
    float edgeNoise = float(hash3(cell + 91u, world.x, world.y, world.z) % 1000u) / 1000.0;
    return dot(d, d) < 1.0 + (edgeNoise - 0.5) * 0.45;
}

bool tryVeinOre(uint salt, int3 world, float density, float peakChance, int cellXZ, int cellY, float radiusMin, float radiusMax)
{
    return inLithologyBlob(salt, world, cellXZ, cellY, density * peakChance * 9.5, radiusMin, radiusMax);
}

uint lithologyAt(int3 world, uint host)
{
    if (host != STONE_BLOCK) return host;
    if (world.y > 180) {
        if (inLithologyBlob(101u, world, 16, 10, 0.032, 2.5, 5.5)) return ANDESITE_BLOCK;
        if (inLithologyBlob(102u, world, 16, 10, 0.030, 2.4, 5.2)) return GRANITE_BLOCK;
        if (inLithologyBlob(103u, world, 16, 10, 0.028, 2.3, 5.0)) return DIORITE_BLOCK;
        if (world.y > 120 && inLithologyBlob(105u, world, 18, 8, 0.014, 2.0, 4.2)) return CALCITE_BLOCK;
        return STONE_BLOCK;
    }
    if (tryVeinOre(21u, world, oreTriangle(world.y, 1, 8, 28), 0.0035, 8, 5, 1.3, 3.0)) return DIAMOND_ORE_BLOCK;
    if (tryVeinOre(22u, world, oreTriangle(world.y, 1, 12, 36), 0.0060, 8, 5, 1.5, 3.4)) return REDSTONE_ORE_BLOCK;
    if (tryVeinOre(23u, world, oreTriangle(world.y, 4, 22, 56), 0.0050, 9, 6, 1.5, 3.3)) return GOLD_ORE_BLOCK;
    if (tryVeinOre(24u, world, oreTriangle(world.y, 8, 28, 64), 0.0045, 8, 5, 1.4, 3.1)) return LAPIS_ORE_BLOCK;
    if (tryVeinOre(25u, world, oreTriangle(world.y, 8, 48, 110), 0.0160, 9, 6, 1.9, 4.2)) return IRON_ORE_BLOCK;
    if (tryVeinOre(26u, world, oreTriangle(world.y, 24, 64, 130), 0.0120, 9, 6, 1.8, 4.0)) return COPPER_ORE_BLOCK;
    if (tryVeinOre(27u, world, oreTriangle(world.y, 32, 90, 180), 0.0220, 10, 7, 2.0, 4.5)) return COAL_ORE_BLOCK;
    if (world.y > 90 && tryVeinOre(28u, world, oreTriangle(world.y, 90, 140, 220), 0.0040, 10, 7, 1.4, 3.2)) return EMERALD_ORE_BLOCK;
    if (inLithologyBlob(101u, world, 16, 10, 0.032, 2.5, 5.5)) return ANDESITE_BLOCK;
    if (inLithologyBlob(102u, world, 16, 10, 0.030, 2.4, 5.2)) return GRANITE_BLOCK;
    if (inLithologyBlob(103u, world, 16, 10, 0.028, 2.3, 5.0)) return DIORITE_BLOCK;
    if (inLithologyBlob(104u, world, 18, 10, 0.018, 2.0, 4.5)) return TUFF_BLOCK;
    if (world.y > 96 && inLithologyBlob(105u, world, 18, 8, 0.014, 2.0, 4.2)) return CALCITE_BLOCK;
    return STONE_BLOCK;
}

uint waterBedTop(uint kind, int3 world, out uint underBlock, out uint soilDepth)
{
    if (kind == WaterLake) {
        int cellX = (int)floor(float(world.x) * 0.00015);
        int cellZ = (int)floor(float(world.z) * 0.00015);
        bool dirt = (hash2(worldSeed + 2111u, cellX, cellZ) & 1u) != 0u;
        underBlock = dirt ? DIRT_BLOCK : GRAVEL_BLOCK;
        soilDepth = 4u;
        return underBlock;
    }
    if (kind == WaterRiver) {
        underBlock = GRAVEL_BLOCK;
        soilDepth = 4u;
        return GRAVEL_BLOCK;
    }
    bool gravel = (hash2(worldSeed + 2100u, 0, 0) & 1u) != 0u;
    underBlock = gravel ? GRAVEL_BLOCK : SAND_BLOCK;
    soilDepth = 5u;
    return underBlock;
}

uint surfaceBlock(
    ColumnData column,
    int3 world,
    uint random,
    uint waterKind,
    bool flooded,
    out uint underBlock,
    out uint soilDepth)
{
    uint biome = column.biome;
    BiomeDefinition definition = BIOMES[biome];
    soilDepth = definition.soilDepth;
    underBlock = definition.underBlock;
    float snowLine = snowLineY(column.temperature);
    uint top = definition.topBlock;

    if ((flooded && waterKind == WaterOcean) || definition.waterKind == WaterOcean)
        return waterBedTop(WaterOcean, world, underBlock, soilDepth);
    if ((flooded && waterKind == WaterLake) || definition.waterKind == WaterLake)
        return waterBedTop(WaterLake, world, underBlock, soilDepth);
    if (flooded && waterKind == WaterRiver)
        return waterBedTop(WaterRiver, world, underBlock, soilDepth);

    if ((definition.flags & BiomeFloodedSurface) != 0u &&
        (flooded || waterKind == WaterWetland)) {
        top = definition.floodedTopBlock;
        underBlock = definition.floodedUnderBlock;
        soilDepth = definition.floodedSoilDepth;
    }
    if ((definition.flags & BiomeAlpineSurface) != 0u) {
        if (world.y >= snowLine) { underBlock = SNOW_BLOCK; top = SNOW_BLOCK; }
        else if (world.y >= snowLine - definition.alpineRockDepth) {
            underBlock = STONE_BLOCK;
            top = STONE_BLOCK;
        }
    }

    if (!flooded && (definition.flags & BiomeGlobalSnow) != 0u) {
        if (world.y >= snowLine) {
            underBlock = SNOW_BLOCK;
            soilDepth = 2u;
            top = SNOW_BLOCK;
        }
        else if ((definition.flags & BiomeHighlandRock) != 0u &&
            world.y >= snowLine - definition.highlandRockDepth) {
            underBlock = STONE_BLOCK;
            soilDepth = 2u;
            top = STONE_BLOCK;
        }
    }
    return top;
}

[numthreads(8, 8, 8)]
void generateBlocks(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= 16 || id.y >= 384 || id.z >= 16) return;
    uint index = id.x + 16u * (id.z + 16u * id.y);
    ColumnData column = inputColumns[packedColumnIndex((int)id.x, (int)id.z)];
    int3 world = int3(chunkOrigin.x + id.x, id.y, chunkOrigin.y + id.z);
    float waterSurface;
    uint waterKind;
    columnWater(column, waterSurface, waterKind);
    [unroll] for (int dz = -1; dz <= 1; ++dz) {
        [unroll] for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) continue;
            int nx = (int)id.x + dx;
            int nz = (int)id.z + dz;
            float neighborSurface;
            uint neighborKind;
            columnWater(inputColumns[packedColumnIndex(nx, nz)], neighborSurface, neighborKind);
            mergeWater(waterSurface, waterKind, neighborSurface, neighborKind);
        }
    }
    float density = densityAt(column, world);
    uint block = AIR_BLOCK;
    if (density > 0.0) {
        // Exposure only matters for near-surface cover. Deep stone skips the
        // second densityAt, which dominates generateBlocks cost.
        float depth = column.surfaceHeight - id.y;
        bool exposed = false;
        if (depth < 12.0)
            exposed = densityAt(column, world + int3(0,1,0)) <= 0.0;
        block = STONE_BLOCK;
        uint random = hash3(worldSeed+1600u,world.x,world.y,world.z);
        uint underBlock = DIRT_BLOCK;
        uint soilDepth = 4u;
        bool flooded = float(id.y) + 1.0 < waterSurface;
        uint cover = surfaceBlock(column, world, random, waterKind, flooded, underBlock, soilDepth);
        if (exposed && depth < 12.0)
            block = cover;
        else if (depth < float(soilDepth) && id.y > 40u)
            block = underBlock;
        if (block == STONE_BLOCK)
            block = lithologyAt(world, block);
    }
    else {
        float fill = waterSurface - float(id.y);
        if (fill >= 1.0)
            block = waterState(MaximumFluidLevel);
        else if (fill > 0.0)
            block = waterState((uint)clamp(ceil(fill * MaximumFluidLevel), 1.0, 8.0));
    }
    if (id.y == 0 || (id.y < 5 && hash3(worldSeed+1800u,world.x,world.y,world.z)%5u >= id.y)) block = BEDROCK_BLOCK;
    outputBlocks[index] = block;
}

static const int FeatureSpacing = 12;
static const int FeatureMargin = 5;
static const int SeaLevel = 63;

bool isTreeGroundBlock(uint state)
{
    uint type = blockType(state);
    return type == GRASS_BLOCK || type == DIRT_BLOCK || type == PODZOL_BLOCK ||
        type == COARSE_DIRT_BLOCK || type == MOSS_BLOCK || type == MUD_BLOCK ||
        type == SAND_BLOCK;
}

bool isTreeLeafBlock(uint state)
{
    uint type = blockType(state);
    return type == LEAVES_BLOCK || type == SPRUCE_LEAVES_BLOCK ||
        type == BIRCH_LEAVES_BLOCK || type == JUNGLE_LEAVES_BLOCK ||
        type == ACACIA_LEAVES_BLOCK || type == DARK_OAK_LEAVES_BLOCK;
}

uint blockAt(int x, int y, int z)
{
    if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 0 || y >= 384) return AIR_BLOCK;
    return outputBlocks[x + 16u * (z + 16u * y)];
}

int findTreeGround(int localX, int localZ)
{
    ColumnData column = inputColumns[packedColumnIndex(localX, localZ)];
    int startY = min(382, (int)ceil(column.surfaceHeight) + 2);
    [loop] for (int y = startY; y >= SeaLevel + 1; --y) {
        uint ground = blockAt(localX, y, localZ);
        uint above = blockAt(localX, y + 1, localZ);
        if (!isTreeGroundBlock(ground)) continue;
        if (blockType(above) == WATER_BLOCK) continue;
        if (above != AIR_BLOCK && !isTreeLeafBlock(above)) continue;

        bool supported = true;
        [loop] for (int depth = 0; depth < 3; ++depth) {
            uint below = blockAt(localX, y - depth, localZ);
            if (below == AIR_BLOCK || blockType(below) == WATER_BLOCK) {
                supported = false;
                break;
            }
        }
        if (!supported) continue;
        return y;
    }
    return -1;
}

void setLeaf(int x, int y, int z, uint leafId)
{
    if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 1 || y >= 384) return;
    uint index = x + 16u * (z + 16u * y);
    uint previous;
    InterlockedCompareExchange(outputBlocks[index], AIR_BLOCK, leafId, previous);
}

void setLog(int x, int y, int z, uint logId)
{
    if (x < 0 || x >= 16 || z < 0 || z >= 16 || y < 1 || y >= 384) return;
    uint index = x + 16u * (z + 16u * y);
    uint ignored;
    InterlockedExchange(outputBlocks[index], logId, ignored);
}

void placeSquareCanopyLayer(int cx, int y, int cz, int radius, uint leafId)
{
    [loop] for (int dz = -radius; dz <= radius; ++dz)
        [loop] for (int dx = -radius; dx <= radius; ++dx) {
            if (abs(dx) == radius && abs(dz) == radius) continue;
            setLeaf(cx + dx, y, cz + dz, leafId);
        }
}

void placeLayeredCanopy(int cx, int cz, int trunkTop, uint leafId, int layerCount, int4 layers[5])
{
    [loop] for (int layer = 0; layer < layerCount; ++layer)
        placeSquareCanopyLayer(cx, trunkTop + layers[layer].x, cz, layers[layer].y, leafId);
}

[numthreads(8, 8, 1)]
void placeTrees(uint3 id : SV_DispatchThreadID)
{
    int minCellX = floorDivInt(chunkOrigin.x - FeatureMargin, FeatureSpacing);
    int maxCellX = floorDivInt(chunkOrigin.x + 15 + FeatureMargin, FeatureSpacing);
    int minCellZ = floorDivInt(chunkOrigin.y - FeatureMargin, FeatureSpacing);
    int maxCellZ = floorDivInt(chunkOrigin.y + 15 + FeatureMargin, FeatureSpacing);
    int cellX = minCellX + (int)id.x;
    int cellZ = minCellZ + (int)id.y;
    if (cellX > maxCellX || cellZ > maxCellZ) return;

    uint featureRandom = hash2(worldSeed + 1900u, cellX, cellZ);
    int treeX = cellX * FeatureSpacing + 2 + int(featureRandom % 6u);
    int treeZ = cellZ * FeatureSpacing + 2 + int((featureRandom >> 8) % 6u);
    int localX = treeX - chunkOrigin.x;
    int localZ = treeZ - chunkOrigin.y;
    if (localX < 0 || localX >= 16 || localZ < 0 || localZ >= 16) return;

    ColumnData column = inputColumns[packedColumnIndex(localX, localZ)];
    BiomeDefinition definition = BIOMES[column.biome];
    if (definition.treeKind == TreeNone || definition.treeChanceNumerator == 0u ||
        ((featureRandom >> 16) % definition.treeChanceDenominator) >= definition.treeChanceNumerator)
        return;
    uint kind = definition.treeKind;
    if (definition.alternateTreeKind != TreeNone && definition.alternateChanceNumerator > 0u &&
        ((featureRandom >> 24) % definition.alternateChanceDenominator) < definition.alternateChanceNumerator)
        kind = definition.alternateTreeKind;
    uint variantCount = ((definition.flags >> 8u) & 3u) + 1u;
    uint variant = ((featureRandom >> 30) & 3u) % variantCount;
    if (column.surfaceHeight <= float(SeaLevel + 1) ||
        column.riverWeight > 0.65 || column.lakeWeight > 0.20) return;

    int groundY = findTreeGround(localX, localZ);
    if (groundY < 0) return;
    float snowLine = snowLineY(column.temperature);
    if (float(groundY) >= snowLine - 4.0) return;
    if ((definition.flags & BiomeAlpineSurface) != 0u &&
        float(groundY) >= snowLine - definition.alpineRockDepth - 2.0) return;

    uint height = 5u;
    uint logId = LOG_BLOCK;
    uint leafId = LEAVES_BLOCK;
    if (kind == 1u) { logId = BIRCH_LOG_BLOCK; leafId = BIRCH_LEAVES_BLOCK; height = 6u + ((featureRandom >> 20) & 3u); }
    else if (kind == 2u) { logId = SPRUCE_LOG_BLOCK; leafId = SPRUCE_LEAVES_BLOCK; height = 7u + ((featureRandom >> 20) & 5u); }
    else if (kind == 3u) { logId = JUNGLE_LOG_BLOCK; leafId = JUNGLE_LEAVES_BLOCK; height = 9u + ((featureRandom >> 20) & 5u); }
    else if (kind == 4u) { logId = ACACIA_LOG_BLOCK; leafId = ACACIA_LEAVES_BLOCK; height = 5u + ((featureRandom >> 20) & 2u); }
    else if (kind == 5u) { logId = DARK_OAK_LOG_BLOCK; leafId = DARK_OAK_LEAVES_BLOCK; height = 4u + ((featureRandom >> 20) & 1u); }
    else height = 4u + ((featureRandom >> 20) & 3u);
    if (variant == 1u) height += 2u;
    else if (variant == 2u) height += (kind == 2u || kind == 3u) ? 3u : 1u;
    else if (variant == 3u && kind == 4u) height += 2u;

    int trunkTop = groundY + int(height);
    if (trunkTop + 4 >= 384) return;

    bool thickJungle = kind == 3u && variant == 3u;
    bool darkOakTrunk = kind == 5u;
    if (darkOakTrunk) {
        bool stableFootprint = true;
        [loop] for (int dx = 0; dx < 2 && stableFootprint; ++dx)
            [loop] for (int dz = 0; dz < 2; ++dz)
                if (findTreeGround(localX + dx, localZ + dz) != groundY)
                    stableFootprint = false;
        if (!stableFootprint) return;
    }

    [loop] for (int y = groundY + 1; y <= trunkTop; ++y) {
        if (darkOakTrunk) {
            setLog(localX, y, localZ, logId);
            setLog(localX + 1, y, localZ, logId);
            setLog(localX, y, localZ + 1, logId);
            setLog(localX + 1, y, localZ + 1, logId);
        } else {
            setLog(localX, y, localZ, logId);
            if (thickJungle) {
                setLog(localX + 1, y, localZ, logId);
                setLog(localX, y, localZ + 1, logId);
                setLog(localX + 1, y, localZ + 1, logId);
            }
        }
    }

    int branchX = ((featureRandom >> 13) & 1u) != 0u ? 1 : -1;
    int branchZ = ((featureRandom >> 14) & 1u) != 0u ? 1 : -1;

    if (kind == 2u) {
        int layers = variant == 1u ? 5 : (variant == 2u ? 6 : 4);
        int baseRadius = variant == 1u ? 2 : (variant == 2u ? 4 : 3);
        [loop] for (int layer = 0; layer < layers; ++layer) {
            int radius = max(1, baseRadius - layer / 2);
            int y = trunkTop - (layers - 1 - layer);
            placeSquareCanopyLayer(localX, y, localZ, radius, leafId);
        }
        setLeaf(localX, trunkTop + 1, localZ, leafId);
        setLeaf(localX, trunkTop + 2, localZ, leafId);
        if (variant == 3u) {
            setLog(localX + branchX, trunkTop - 3, localZ, logId);
            setLog(localX + branchX * 2, trunkTop - 2, localZ, logId);
            placeSquareCanopyLayer(localX + branchX * 2, trunkTop - 1, localZ, 2, leafId);
        }
    }
    else if (kind == 4u) {
        int reach = variant == 0u ? 0 : (variant == 3u ? 3 : 2);
        int canopyX = localX + branchX * reach;
        int canopyZ = localZ + (variant == 2u ? 0 : branchZ * reach);
        [loop] for (int step = 1; step <= reach; ++step)
            setLog(localX + branchX * step, trunkTop - reach + step,
                localZ + (variant == 2u ? 0 : branchZ * step), logId);
        int radius = variant == 1u ? 4 : 3;
        [loop] for (int dy = -1; dy <= 1; ++dy) {
            int r = dy == 0 ? radius : radius - 1;
            [loop] for (int dz = -r; dz <= r; ++dz)
                [loop] for (int dx = -r; dx <= r; ++dx) {
                    if (dx * dx + dz * dz > r * r) continue;
                    setLeaf(canopyX + dx, trunkTop + dy, canopyZ + dz, leafId);
                }
        }
        if (variant == 2u) {
            setLog(localX - branchX, trunkTop - 2, localZ + branchZ, logId);
            setLog(localX - branchX * 2, trunkTop - 1, localZ + branchZ * 2, logId);
            placeSquareCanopyLayer(localX - branchX * 2, trunkTop,
                localZ + branchZ * 2, 2, leafId);
        }
    }
    else if (kind == 3u) {
        int canopyRadius = variant == 1u ? 2 : (variant == 2u ? 4 : 3);
        int4 jungleLayers[5] = {
            int4(2, max(1, canopyRadius - 1), 0, 0),
            int4(1, canopyRadius, 0, 0), int4(0, canopyRadius, 0, 0),
            int4(-1, max(1, canopyRadius - 1), 0, 0), int4(-2, 1, 0, 0)
        };
        int canopyX = thickJungle ? localX + 1 : localX;
        int canopyZ = thickJungle ? localZ + 1 : localZ;
        placeLayeredCanopy(canopyX, canopyZ, trunkTop, leafId, 5, jungleLayers);
        if (variant >= 2u) {
            [unroll] for (int step = 1; step <= 2; ++step)
                setLog(localX + branchX * step, trunkTop - 4 + step,
                    localZ + branchZ * step, logId);
            placeSquareCanopyLayer(localX + branchX * 2, trunkTop - 1,
                localZ + branchZ * 2, variant == 2u ? 2 : 1, leafId);
        }
    }
    else if (kind == 5u) {
        int crownRadius = variant == 1u ? 4 : 3;
        int4 darkOakLayers[5] = {
            int4(1, variant == 2u ? 3 : 2, 0, 0),
            int4(0, crownRadius, 0, 0), int4(-1, crownRadius, 0, 0),
            int4(-2, max(2, crownRadius - 1), 0, 0), int4(-3, 1, 0, 0)
        };
        placeLayeredCanopy(localX + 1, localZ + 1, trunkTop, leafId, 5, darkOakLayers);
        if (variant >= 2u) {
            setLog(localX + 1 + branchX, trunkTop - 2, localZ + 1, logId);
            setLog(localX + 1 + branchX * 2, trunkTop - 1, localZ + 1, logId);
            placeSquareCanopyLayer(localX + 1 + branchX * 2, trunkTop,
                localZ + 1, 2, leafId);
        }
    }
    else if (kind == 1u) {
        int radius = variant == 2u ? 3 : 2;
        int4 birchLayers[5] = {
            int4(1, variant == 1u ? 1 : 2, 0, 0), int4(0, radius, 0, 0),
            int4(-1, radius, 0, 0), int4(-2, 1, 0, 0), int4(0, 0, 0, 0)
        };
        placeLayeredCanopy(localX, localZ, trunkTop, leafId, 4, birchLayers);
        if (variant == 3u) {
            setLog(localX + branchX, trunkTop - 2, localZ + branchZ, logId);
            placeSquareCanopyLayer(localX + branchX, trunkTop - 1,
                localZ + branchZ, 1, leafId);
        }
    }
    else {
        int oakRadius = variant == 1u ? 3 : 2;
        int4 oakLayers[5] = {
            int4(1, variant == 2u ? 2 : 1, 0, 0), int4(0, oakRadius, 0, 0),
            int4(-1, oakRadius, 0, 0), int4(-2, variant == 1u ? 2 : 1, 0, 0),
            int4(0, 0, 0, 0)
        };
        placeLayeredCanopy(localX, localZ, trunkTop, leafId, 4, oakLayers);
        if (variant >= 2u) {
            int branchCount = variant == 3u ? 2 : 1;
            [loop] for (int branch = 0; branch < branchCount; ++branch) {
                int bx = branch == 0 ? branchX : -branchX;
                int bz = branch == 0 ? branchZ : -branchZ;
                setLog(localX + bx, trunkTop - 2 - branch, localZ + bz, logId);
                setLog(localX + bx * 2, trunkTop - 1 - branch, localZ + bz * 2, logId);
                placeSquareCanopyLayer(localX + bx * 2, trunkTop - branch,
                    localZ + bz * 2, 1, leafId);
            }
        }
    }
}
