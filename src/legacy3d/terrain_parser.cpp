#include "terrain_parser.h"

#include "shape_parser.h"

#include <algorithm>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mw::legacy3d {
namespace {

constexpr int kTerrainGridWidth = 87;
constexpr int kTerrainGridHeight = 47;
constexpr size_t kTerrainGridSampleSize = 1;
constexpr size_t kTerrainWorldRecordSize = 14;
constexpr size_t kTerrainScenarioRecordSize = 16;
constexpr size_t kTerrainScenarioTileCount = 4;
constexpr size_t kTerpckGiIndexCount = 30;
constexpr size_t kTerpckGiIndexBase = 0x0078;
constexpr size_t kTerpckGiQuadBase = 0x0530;
constexpr float kTerrainRawXOrigin = 44160.0f;
constexpr float kTerrainRawZTop = 24000.0f;
constexpr float kTerrainRawCellSize = 512.0f;

uint16_t u16le(const std::vector<uint8_t>& buf, size_t off) {
    if (off + 1 >= buf.size()) {
        throw ParseError("u16 read out of range");
    }
    return static_cast<uint16_t>(buf[off] | (static_cast<uint16_t>(buf[off + 1]) << 8));
}

int16_t s16le(const std::vector<uint8_t>& buf, size_t off) {
    return static_cast<int16_t>(u16le(buf, off));
}

void requireDecodedRange(
    const std::vector<uint8_t>& decoded,
    size_t offset,
    size_t length,
    const char* description) {
    if (offset > decoded.size() || length > decoded.size() - offset) {
        throw ParseError(std::string("TERPCK.GI ") + description + " is out of range");
    }
}

size_t dosLinearOffset(uint16_t offset, uint16_t segment) {
    return static_cast<size_t>(segment) * 16u + static_cast<size_t>(offset);
}

int16_t signedWordDifference(int16_t lhs, int16_t rhs) {
    const uint16_t difference = static_cast<uint16_t>(lhs) - static_cast<uint16_t>(rhs);
    if (difference <= 0x7fffu) {
        return static_cast<int16_t>(difference);
    }
    return static_cast<int16_t>(static_cast<int32_t>(difference) - 0x10000);
}

int16_t signedWord(uint16_t value) {
    if (value <= 0x7fffu) {
        return static_cast<int16_t>(value);
    }
    return static_cast<int16_t>(static_cast<int32_t>(value) - 0x10000);
}

int32_t signedDword(uint32_t value) {
    if (value <= 0x7fffffffu) {
        return static_cast<int32_t>(value);
    }
    return -1 - static_cast<int32_t>(0xffffffffu - value);
}

int32_t wrappedDwordDifference(int32_t lhs, int32_t rhs) {
    return signedDword(static_cast<uint32_t>(lhs) - static_cast<uint32_t>(rhs));
}

int32_t arithmeticShiftRightDword(int32_t value, uint8_t shift) {
    uint32_t bits = static_cast<uint32_t>(value);
    for (uint16_t count = 0; count < shift; ++count) {
        bits = (bits >> 1u) | (bits & 0x80000000u);
    }
    return signedDword(bits);
}

uint32_t originalDwordMagnitudeBits(int32_t value) {
    const uint32_t bits = static_cast<uint32_t>(value);
    return value < 0 ? 0u - bits : bits;
}

bool originalMagnitudeWithinWordExtent(uint32_t magnitude, uint16_t extent) {
    const int16_t magnitudeHigh = signedWord(static_cast<uint16_t>(magnitude >> 16u));
    const int16_t extentWord = signedWord(extent);
    const int16_t extentHigh = extentWord < 0 ? static_cast<int16_t>(-1) : static_cast<int16_t>(0);
    if (magnitudeHigh != extentHigh) {
        return magnitudeHigh < extentHigh;
    }
    return static_cast<uint16_t>(magnitude) <= extent;
}

int32_t signedFixed32(uint16_t low, int16_t high) {
    return (static_cast<int32_t>(high) << 16) | static_cast<int32_t>(low);
}

std::string stemString(const std::filesystem::path& path) {
    return path.stem().string();
}

std::array<float, 3> terrainEgaRgb(uint8_t color) {
    static constexpr std::array<std::array<float, 3>, 16> ega{{
        {{0.0f, 0.0f, 0.0f}}, {{0.0f, 0.0f, 170.0f / 255.0f}},
        {{0.0f, 170.0f / 255.0f, 0.0f}}, {{0.0f, 170.0f / 255.0f, 170.0f / 255.0f}},
        {{170.0f / 255.0f, 0.0f, 0.0f}}, {{170.0f / 255.0f, 0.0f, 170.0f / 255.0f}},
        {{170.0f / 255.0f, 85.0f / 255.0f, 0.0f}}, {{170.0f / 255.0f, 170.0f / 255.0f, 170.0f / 255.0f}},
        {{85.0f / 255.0f, 85.0f / 255.0f, 85.0f / 255.0f}}, {{85.0f / 255.0f, 85.0f / 255.0f, 1.0f}},
        {{85.0f / 255.0f, 1.0f, 85.0f / 255.0f}}, {{85.0f / 255.0f, 1.0f, 1.0f}},
        {{1.0f, 85.0f / 255.0f, 85.0f / 255.0f}}, {{1.0f, 85.0f / 255.0f, 1.0f}},
        {{1.0f, 1.0f, 85.0f / 255.0f}}, {{1.0f, 1.0f, 1.0f}},
    }};
    return ega[static_cast<size_t>(color & 0x0fu)];
}

std::array<float, 3> terrainPaletteShadeRgb(
    uint8_t shade,
    const std::string& paletteId,
    bool overlayColors) {
    if (paletteId == "debug" || paletteId == "flat" || shade < 16u) {
        return terrainEgaRgb(shade);
    }
    const uint8_t pair = terrainPaletteBank1Bytes(paletteId)[shade & 0x0fu];
    if (!overlayColors) {
        return terrainEgaRgb(pair & 0x0fu);
    }
    uint8_t color = pair >> 4u;
    if (color == 1u || color == 3u || color == 5u || color == 9u || color == 11u) {
        color = paletteId == "desert" || paletteId == "green"
                    ? 0u
                    : static_cast<uint8_t>(pair & 0x0fu);
    } else if (paletteId == "green" && color == 2u) {
        color = 0u;
    }
    return terrainEgaRgb(color);
}

std::vector<int> terrainShapeValidIndices(
    const RuntimeRecord& record,
    const RuntimePrimitive& primitive,
    const std::string& indexMode) {
    std::vector<int> valid;
    valid.reserve(primitive.normalizedIndices.size());
    for (uint8_t rawIndex : primitive.normalizedIndices) {
        const int index = indexMode == "minus1"
                              ? static_cast<int>(rawIndex) - 1
                              : static_cast<int>(rawIndex);
        if (index >= 0 && static_cast<size_t>(index) < record.vertices.size()) {
            valid.push_back(index);
        }
    }
    return valid;
}

std::array<float, 3> transformTerrainShapeVertex(
    Vec3i vertex,
    float scale,
    const std::string& swizzle) {
    const float x = static_cast<float>(vertex.x) * scale;
    const float y = static_cast<float>(vertex.y) * scale;
    const float z = static_cast<float>(vertex.z) * scale;
    if (swizzle == "xyz") {
        return {x, y, z};
    }
    if (swizzle == "xzy") {
        return {x, z, y};
    }
    if (swizzle == "zyx") {
        return {z, y, x};
    }
    throw std::runtime_error("unsupported terrain vertex swizzle: " + swizzle);
}

} // namespace

bool TerrainGridSample::empty() const {
    return rawValue == 0;
}

bool TerrainGridSample::blocking() const {
    return rawValue != 0;
}

uint8_t TerrainGridSample::colorBand() const {
    return std::min<uint8_t>(3u, static_cast<uint8_t>(rawValue >> 5u));
}

uint8_t TerrainGridSample::detailBits() const {
    return static_cast<uint8_t>(rawValue & 0x1fu);
}

bool TerrainScenarioRecord::terrainEnabled() const {
    // BTECH derives the scenario seed as context[6] % 20.  The first twenty
    // SNARIO records are therefore all terrain records; raw byte 8 may be
    // zero and is a mode/selector byte, not an enable flag.  Records after
    // index 19 belong to the coordinate-bank payload interpreted elsewhere.
    return index < 20u;
}

bool TerrainScenarioRecord::usesBVariant() const {
    return terrainMode == 1;
}

bool TerrainScenarioRecord::hasValidTileIds() const {
    return std::all_of(
        tileIds.begin(),
        tileIds.end(),
        [](uint8_t tileId) {
            return tileId <= 15;
        });
}

bool TerrainScenarioRecord::isTerrainLayoutCandidate() const {
    return terrainEnabled() && hasValidTileIds();
}

const TerrainGridSample& TerrainGrid::sampleAt(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) {
        throw ParseError("terrain grid sample out of range");
    }
    return samples[static_cast<size_t>(x) * static_cast<size_t>(height) + static_cast<size_t>(y)];
}

float TerrainMesh::sampleHeightNearest(float x, float z) const {
    if (vertices.empty()) {
        throw ParseError("cannot sample empty terrain mesh");
    }
    const TerrainMeshVertex* best = &vertices.front();
    float bestDistance = std::numeric_limits<float>::max();
    for (const TerrainMeshVertex& vertex : vertices) {
        const float dx = vertex.x - x;
        const float dz = vertex.z - z;
        const float d2 = dx * dx + dz * dz;
        if (d2 < bestDistance) {
            bestDistance = d2;
            best = &vertex;
        }
    }
    return best->y;
}

const std::vector<TerrainArenaLayout>& terrainArenaLayouts() {
    static const std::vector<TerrainArenaLayout> layouts{
        TerrainArenaLayout{
            "control_2x2",
            {"TILE0", "TILE1", "TILE8", "TILE9"},
            0.0f,
            0.18f,
            "First 2x2 terrain control arena using a continuous composite GRD mesh",
        },
    };
    return layouts;
}

const TerrainArenaLayout& terrainArenaLayoutById(const std::string& id) {
    const std::vector<TerrainArenaLayout>& layouts = terrainArenaLayouts();
    const auto it = std::find_if(
        layouts.begin(),
        layouts.end(),
        [&id](const TerrainArenaLayout& layout) {
            return layout.id == id;
        });
    if (it == layouts.end()) {
        throw ParseError("unknown terrain arena layout: " + id);
    }
    return *it;
}

std::vector<TerrainObjectPlacement> normalizeTerrainWorldPlacements(
    const TerrainWorld& world,
    TerrainPlacementBounds bounds,
    float insetFraction) {
    if (insetFraction < 0.0f || insetFraction >= 0.5f) {
        throw ParseError("terrain placement inset fraction must be in range 0.0..0.5");
    }
    std::vector<TerrainObjectPlacement> placements;
    placements.reserve(world.objects.size());
    if (world.objects.empty()) {
        return placements;
    }

    int32_t minWorldX = world.objects.front().x;
    int32_t maxWorldX = world.objects.front().x;
    int32_t minWorldZ = world.objects.front().z;
    int32_t maxWorldZ = world.objects.front().z;
    for (const TerrainWorldObject& object : world.objects) {
        minWorldX = std::min(minWorldX, object.x);
        maxWorldX = std::max(maxWorldX, object.x);
        minWorldZ = std::min(minWorldZ, object.z);
        maxWorldZ = std::max(maxWorldZ, object.z);
    }

    const float marginX = (bounds.maxX - bounds.minX) * insetFraction;
    const float marginZ = (bounds.maxZ - bounds.minZ) * insetFraction;
    const float minGroundX = bounds.minX + marginX;
    const float maxGroundX = bounds.maxX - marginX;
    const float minGroundZ = bounds.minZ + marginZ;
    const float maxGroundZ = bounds.maxZ - marginZ;
    const float centerGroundX = (bounds.minX + bounds.maxX) * 0.5f;
    const float centerGroundZ = (bounds.minZ + bounds.maxZ) * 0.5f;
    const float spanX = static_cast<float>(std::max<int32_t>(1, maxWorldX - minWorldX));
    const float spanZ = static_cast<float>(std::max<int32_t>(1, maxWorldZ - minWorldZ));

    for (size_t i = 0; i < world.objects.size(); ++i) {
        const TerrainWorldObject& object = world.objects[i];
        TerrainObjectPlacement placement;
        placement.sourceIndex = i;
        placement.recordIndex = object.recordIndex;
        placement.rawX = object.x;
        placement.rawZ = object.z;
        if (maxWorldX == minWorldX) {
            placement.x = centerGroundX;
        } else {
            const float t = static_cast<float>(object.x - minWorldX) / spanX;
            placement.x = minGroundX + t * (maxGroundX - minGroundX);
        }
        if (maxWorldZ == minWorldZ) {
            placement.z = centerGroundZ;
        } else {
            const float t = static_cast<float>(object.z - minWorldZ) / spanZ;
            placement.z = minGroundZ + t * (maxGroundZ - minGroundZ);
        }
        placements.push_back(placement);
    }
    return placements;
}

std::vector<TerrainObjectPlacement> mapTerrainWorldPlacementsFromRawCoordinates(
    const TerrainWorld& world,
    TerrainPlacementBounds bounds,
    float cellSize) {
    if (cellSize <= 0.0f) {
        throw ParseError("terrain raw placement cell size must be positive");
    }
    std::vector<TerrainObjectPlacement> placements;
    placements.reserve(world.objects.size());
    for (size_t i = 0; i < world.objects.size(); ++i) {
        const TerrainWorldObject& object = world.objects[i];
        TerrainObjectPlacement placement;
        placement.sourceIndex = i;
        placement.recordIndex = object.recordIndex;
        placement.rawX = object.x;
        placement.rawZ = object.z;
        placement.x = bounds.minX + ((static_cast<float>(object.x) + kTerrainRawXOrigin) / kTerrainRawCellSize) * cellSize;
        placement.z = bounds.minZ + ((kTerrainRawZTop - static_cast<float>(object.z)) / kTerrainRawCellSize) * cellSize;
        placements.push_back(placement);
    }
    return placements;
}

TerrainGrid loadTerrainGrid(const std::filesystem::path& path) {
    const std::vector<uint8_t> bytes = readFileBytes(path);
    const size_t expected = static_cast<size_t>(kTerrainGridWidth) *
                            static_cast<size_t>(kTerrainGridHeight) *
                            kTerrainGridSampleSize;
    if (bytes.size() != expected) {
        std::ostringstream oss;
        oss << "unexpected terrain grid size for " << path.string()
            << ": got " << bytes.size() << ", expected " << expected;
        throw ParseError(oss.str());
    }

    TerrainGrid grid;
    grid.resourceName = stemString(path);
    grid.width = kTerrainGridWidth;
    grid.height = kTerrainGridHeight;
    grid.samples.reserve(static_cast<size_t>(grid.width) * static_cast<size_t>(grid.height));
    for (uint8_t byte : bytes) {
        grid.samples.push_back(TerrainGridSample{byte});
    }
    return grid;
}

TerrainWorld loadTerrainWorld(const std::filesystem::path& path) {
    const std::vector<uint8_t> bytes = readFileBytes(path);
    if ((bytes.size() % kTerrainWorldRecordSize) != 0) {
        throw ParseError("terrain world file size is not divisible by 14 bytes: " + path.string());
    }

    TerrainWorld world;
    world.resourceName = stemString(path);
    world.objects.reserve(bytes.size() / kTerrainWorldRecordSize);
    for (size_t off = 0; off < bytes.size(); off += kTerrainWorldRecordSize) {
        TerrainWorldObject object;
        object.recordIndex = u16le(bytes, off);
        object.rawXLow = u16le(bytes, off + 2);
        object.rawXHigh = s16le(bytes, off + 4);
        object.rawZLow = u16le(bytes, off + 6);
        object.rawZHigh = s16le(bytes, off + 8);
        object.rawYLow = u16le(bytes, off + 10);
        object.rawYHigh = s16le(bytes, off + 12);
        object.unknownA = static_cast<int16_t>(object.rawYLow);
        object.unknownB = object.rawYHigh;
        object.x = signedFixed32(object.rawXLow, object.rawXHigh);
        object.z = signedFixed32(object.rawZLow, object.rawZHigh);
        object.y = signedFixed32(object.rawYLow, object.rawYHigh);
        world.objects.push_back(object);
    }
    return world;
}

std::vector<TerrainScenarioRecord> loadTerrainScenarioRecords(const std::filesystem::path& path) {
    const std::vector<uint8_t> bytes = readFileBytes(path);
    if ((bytes.size() % kTerrainScenarioRecordSize) != 0) {
        throw ParseError("terrain scenario file size is not divisible by 16 bytes: " + path.string());
    }

    std::vector<TerrainScenarioRecord> records;
    records.reserve(bytes.size() / kTerrainScenarioRecordSize);
    for (size_t off = 0; off < bytes.size(); off += kTerrainScenarioRecordSize) {
        TerrainScenarioRecord record;
        record.index = records.size();
        for (size_t i = 0; i < kTerrainScenarioRecordSize; ++i) {
            record.raw[i] = bytes[off + i];
        }
        for (size_t i = 0; i < kTerrainScenarioTileCount; ++i) {
            record.tileIds[i] = bytes[off + i];
        }
        record.terrainMode = bytes[off + 8u];
        records.push_back(record);
    }
    return records;
}

std::optional<TerrainScenarioRecord> terrainScenarioRecordByIndex(
    const std::vector<TerrainScenarioRecord>& records,
    size_t index) {
    if (index >= records.size()) {
        return std::nullopt;
    }
    return records[index];
}

std::vector<std::string> terrainScenarioTileNames(const TerrainScenarioRecord& record) {
    std::vector<std::string> names;
    names.reserve(kTerrainScenarioTileCount);
    for (size_t i = 0; i < record.tileIds.size(); ++i) {
        const uint8_t tileId = record.tileIds[i];
        if (tileId > 15) {
            std::ostringstream oss;
            oss << "SNARIO terrain tile id out of range at record "
                << record.index << " slot " << i << ": " << static_cast<int>(tileId);
            throw ParseError(oss.str());
        }
        std::ostringstream name;
        name << "TILE" << static_cast<int>(tileId);
        if (record.usesBVariant()) {
            name << "B";
        }
        names.push_back(name.str());
    }
    return names;
}

TerrainGrid composeTerrainGrid2x2(
    const std::array<TerrainGrid, 4>& grids,
    const std::string& resourceName,
    bool mirrorTileX,
    bool mirrorTileY) {
    if (grids[0].width <= 0 || grids[0].height <= 0) {
        throw ParseError("cannot compose empty terrain grid");
    }
    for (const TerrainGrid& grid : grids) {
        if (grid.width != grids[0].width || grid.height != grids[0].height ||
            grid.samples.size() != static_cast<size_t>(grid.width * grid.height)) {
            throw ParseError("terrain grids in 2x2 composition have mismatched dimensions");
        }
    }

    TerrainGrid combined;
    combined.resourceName = resourceName;
    combined.width = grids[0].width * 2;
    combined.height = grids[0].height * 2;
    combined.samples.assign(
        static_cast<size_t>(combined.width) * static_cast<size_t>(combined.height),
        TerrainGridSample{});

    for (size_t tile = 0; tile < grids.size(); ++tile) {
        const TerrainGrid& grid = grids[tile];
        const int baseX = static_cast<int>(tile % 2u) * grid.width;
        const int baseY = static_cast<int>(tile / 2u) * grid.height;
        for (int y = 0; y < grid.height; ++y) {
            for (int x = 0; x < grid.width; ++x) {
                const int sourceX = mirrorTileX ? (grid.width - 1 - x) : x;
                const int sourceY = mirrorTileY ? (grid.height - 1 - y) : y;
                combined.samples[static_cast<size_t>(baseX + x) * static_cast<size_t>(combined.height) +
                                 static_cast<size_t>(baseY + y)] =
                    grid.sampleAt(sourceX, sourceY);
            }
        }
    }
    return combined;
}

TerpckGiResource loadTerpckGiResource(const std::filesystem::path& path) {
    const std::vector<uint8_t> packed = readFileBytes(path);
    const std::vector<uint8_t> decoded = unpackDynamixBlock(packed);
    if (decoded.size() < kTerpckGiQuadBase || ((decoded.size() - kTerpckGiQuadBase) % 8u) != 0) {
        throw ParseError("TERPCK.GI decoded payload does not match expected terrain-vector layout");
    }

    TerpckGiResource resource;
    resource.resourceName = stemString(path);
    resource.packedSize = packed.size();
    resource.decodedSize = decoded.size();

    size_t pointerTableOffset = 0;
    for (size_t recordIndex = 0;; ++recordIndex) {
        requireDecodedRange(decoded, pointerTableOffset, 4u, "far-pointer table");
        const uint16_t recordOffset = u16le(decoded, pointerTableOffset);
        const uint16_t recordSegment = u16le(decoded, pointerTableOffset + 2u);
        pointerTableOffset += 4u;
        if (recordOffset == 0u && recordSegment == 0u) {
            break;
        }

        const size_t recordLinearOffset = dosLinearOffset(recordOffset, recordSegment);
        requireDecodedRange(decoded, recordLinearOffset, 10u, "collision record");

        TerpckGiCollisionRecord record;
        record.recordIndex = recordIndex;
        record.pointer.offset = recordOffset;
        record.pointer.segment = recordSegment;
        record.pointer.linearOffset = static_cast<uint32_t>(recordLinearOffset);
        record.extentX = u16le(decoded, recordLinearOffset);
        record.extentZ = u16le(decoded, recordLinearOffset + 2u);
        record.modeByte4 = decoded[recordLinearOffset + 4u];
        record.priorityByte5 = decoded[recordLinearOffset + 5u];
        record.resultByte6 = decoded[recordLinearOffset + 6u];
        record.subrecordCountByte7 = decoded[recordLinearOffset + 7u];
        record.subrecordTableOffset = u16le(decoded, recordLinearOffset + 8u);
        const size_t subrecordTableLinearOffset =
            dosLinearOffset(record.subrecordTableOffset, recordSegment);
        record.subrecordTableLinearOffset = static_cast<uint32_t>(subrecordTableLinearOffset);
        requireDecodedRange(
            decoded,
            subrecordTableLinearOffset,
            static_cast<size_t>(record.subrecordCountByte7) * 16u,
            "collision subrecord table");
        record.subrecords.reserve(record.subrecordCountByte7);

        for (size_t subrecordIndex = 0;
             subrecordIndex < record.subrecordCountByte7;
             ++subrecordIndex) {
            const size_t subrecordLinearOffset = subrecordTableLinearOffset + subrecordIndex * 16u;
            TerpckGiCollisionSubrecord subrecord;
            subrecord.linearOffset = static_cast<uint32_t>(subrecordLinearOffset);
            subrecord.word0 = s16le(decoded, subrecordLinearOffset);
            subrecord.word2 = s16le(decoded, subrecordLinearOffset + 2u);
            subrecord.word4 = s16le(decoded, subrecordLinearOffset + 4u);
            subrecord.heightScaleByte6 = decoded[subrecordLinearOffset + 6u];
            subrecord.byte7 = decoded[subrecordLinearOffset + 7u];
            subrecord.byte8 = decoded[subrecordLinearOffset + 8u];
            subrecord.edgeCount = decoded[subrecordLinearOffset + 9u];
            if (subrecord.edgeCount > 0x20u) {
                throw ParseError("TERPCK.GI collision subrecord exceeds original 32-edge limit");
            }
            subrecord.edgeTableOffset = u16le(decoded, subrecordLinearOffset + 10u);
            subrecord.baseHeightWord = s16le(decoded, subrecordLinearOffset + 12u);
            subrecord.planeOffset = u16le(decoded, subrecordLinearOffset + 14u);

            const size_t edgeTableLinearOffset =
                dosLinearOffset(subrecord.edgeTableOffset, recordSegment);
            subrecord.edgeTableLinearOffset = static_cast<uint32_t>(edgeTableLinearOffset);
            requireDecodedRange(
                decoded,
                edgeTableLinearOffset,
                static_cast<size_t>(subrecord.edgeCount) * 8u,
                "half-plane edge table");
            subrecord.edges.reserve(subrecord.edgeCount);
            for (size_t edgeIndex = 0; edgeIndex < subrecord.edgeCount; ++edgeIndex) {
                const size_t edgeOffset = edgeTableLinearOffset + edgeIndex * 8u;
                subrecord.edges.push_back(TerpckGiHalfPlaneEdge{
                    s16le(decoded, edgeOffset),
                    s16le(decoded, edgeOffset + 2u),
                    s16le(decoded, edgeOffset + 4u),
                    s16le(decoded, edgeOffset + 6u),
                });
            }

            const size_t planeLinearOffset =
                dosLinearOffset(subrecord.planeOffset, recordSegment);
            subrecord.planeLinearOffset = static_cast<uint32_t>(planeLinearOffset);
            requireDecodedRange(decoded, planeLinearOffset, 8u, "plane tuple");
            subrecord.plane = TerpckGiPlaneTuple{
                s16le(decoded, planeLinearOffset),
                s16le(decoded, planeLinearOffset + 2u),
                s16le(decoded, planeLinearOffset + 4u),
                s16le(decoded, planeLinearOffset + 6u),
            };
            record.subrecords.push_back(std::move(subrecord));
        }
        resource.collisionRecords.push_back(std::move(record));
    }
    if (resource.collisionRecords.empty()) {
        throw ParseError("TERPCK.GI collision far-pointer table is empty");
    }

    // Keep the earlier provisional views stable for tools that still display
    // them. The collisionRecords catalog above is the caller-proven layout.
    resource.index.reserve(kTerpckGiIndexCount);

    for (size_t i = 0; i < kTerpckGiIndexCount; ++i) {
        const size_t off = i * 4u;
        TerpckGiIndexEntry entry;
        entry.attrOrColor = u16le(decoded, off);
        entry.relOffset = u16le(decoded, off + 2);
        const size_t absOffset = kTerpckGiIndexBase + static_cast<size_t>(entry.relOffset);
        if (absOffset >= kTerpckGiQuadBase) {
            throw ParseError("TERPCK.GI slice offset reaches quad pool");
        }
        entry.absOffset = static_cast<uint16_t>(absOffset);
        resource.index.push_back(entry);
    }

    for (size_t i = 0; i < resource.index.size(); ++i) {
        const size_t next = (i + 1u < resource.index.size())
                                ? static_cast<size_t>(resource.index[i + 1u].absOffset)
                                : kTerpckGiQuadBase;
        if (next < resource.index[i].absOffset) {
            throw ParseError("TERPCK.GI slice offsets are not monotonic");
        }
        resource.index[i].length = static_cast<uint16_t>(next - resource.index[i].absOffset);
    }

    resource.quads.reserve((decoded.size() - kTerpckGiQuadBase) / 8u);
    for (size_t off = kTerpckGiQuadBase; off < decoded.size(); off += 8u) {
        resource.quads.push_back(TerpckGiQuad{
            s16le(decoded, off),
            s16le(decoded, off + 2),
            s16le(decoded, off + 4),
            s16le(decoded, off + 6),
        });
    }
    return resource;
}

bool terpckGiSubrecordContainsPoint(
    const TerpckGiCollisionSubrecord& subrecord,
    int16_t pointX,
    int16_t pointZ) {
    if (subrecord.edges.empty() || subrecord.edges.size() > 0x20u) {
        return false;
    }
    for (const TerpckGiHalfPlaneEdge& edge : subrecord.edges) {
        const int16_t deltaX = signedWordDifference(pointX, edge.anchorX);
        const int16_t deltaZ = signedWordDifference(pointZ, edge.anchorZ);
        const int64_t expression =
            static_cast<int64_t>(edge.coefficientA) * static_cast<int64_t>(deltaX) +
            static_cast<int64_t>(edge.coefficientB) * static_cast<int64_t>(deltaZ);
        if (expression > 0) {
            return false;
        }
    }
    return true;
}

TerpckGiPlanarQueryResult terpckGiQueryRecordPlanar(
    const TerpckGiCollisionRecord& record,
    int32_t queryX,
    int32_t queryZ,
    int32_t objectX,
    int32_t objectZ,
    uint8_t scaleShift,
    std::optional<uint8_t> cachedSubrecordIndex) {
    TerpckGiPlanarQueryResult result;
    result.rawDeltaX = wrappedDwordDifference(queryX, objectX);
    result.rawDeltaZ = wrappedDwordDifference(queryZ, objectZ);
    result.scaledDeltaX = arithmeticShiftRightDword(result.rawDeltaX, scaleShift);
    result.scaledDeltaZ = arithmeticShiftRightDword(result.rawDeltaZ, scaleShift);
    result.absoluteScaledX = originalDwordMagnitudeBits(result.scaledDeltaX);
    result.absoluteScaledZ = originalDwordMagnitudeBits(result.scaledDeltaZ);
    result.withinExtentX = originalMagnitudeWithinWordExtent(result.absoluteScaledX, record.extentX);
    if (!result.withinExtentX) {
        return result;
    }
    result.withinExtentZ = originalMagnitudeWithinWordExtent(result.absoluteScaledZ, record.extentZ);
    if (!result.withinExtentZ) {
        return result;
    }
    result.broadPhaseAccepted = true;
    if (record.subrecords.empty()) {
        return result;
    }
    if (record.modeByte4 != 0u) {
        result.modeBypassedNarrowPhase = true;
        result.subrecordIndex = 0u;
        return result;
    }

    const int16_t pointX = signedWord(static_cast<uint16_t>(result.scaledDeltaX));
    const int16_t pointZ = signedWord(static_cast<uint16_t>(result.scaledDeltaZ));
    if (cachedSubrecordIndex.has_value()) {
        const size_t cachedIndex = *cachedSubrecordIndex;
        if (cachedIndex >= record.subrecords.size()) {
            throw ParseError("TERPCK.GI cached collision subrecord index is out of range");
        }
        result.cachedSubrecordTested = true;
        ++result.testedSubrecords;
        if (terpckGiSubrecordContainsPoint(record.subrecords[cachedIndex], pointX, pointZ)) {
            result.cachedSubrecordAccepted = true;
            result.subrecordIndex = cachedIndex;
            return result;
        }
    }
    for (size_t index = 0; index < record.subrecords.size(); ++index) {
        ++result.testedSubrecords;
        if (terpckGiSubrecordContainsPoint(record.subrecords[index], pointX, pointZ)) {
            result.subrecordIndex = index;
            return result;
        }
    }
    return result;
}

int16_t terpckGiSubrecordHeightWord(
    const TerpckGiCollisionSubrecord& subrecord,
    int16_t pointX,
    int16_t pointZ) {
    const int16_t deltaX = signedWordDifference(subrecord.plane.word4, pointX);
    const int16_t deltaZ = signedWordDifference(subrecord.plane.word6, pointZ);
    uint32_t expressionBits = 0u;
    if (subrecord.plane.word0 != 0) {
        expressionBits += static_cast<uint32_t>(
            static_cast<int32_t>(subrecord.plane.word0) * static_cast<int32_t>(deltaX));
    }
    if (subrecord.plane.word2 != 0) {
        expressionBits += static_cast<uint32_t>(
            static_cast<int32_t>(subrecord.plane.word2) * static_cast<int32_t>(deltaZ));
    }

    int16_t heightOffset = 0;
    if (expressionBits != 0u) {
        const uint32_t scaledExpression =
            expressionBits * static_cast<uint32_t>(subrecord.heightScaleByte6);
        heightOffset = signedWord(static_cast<uint16_t>(scaledExpression >> 16u));
    }
    return signedWord(
        static_cast<uint16_t>(subrecord.baseHeightWord) + static_cast<uint16_t>(heightOffset));
}

int32_t terpckGiScaleHeightToWorld(
    int16_t localHeightWord,
    uint8_t scaleShift,
    int32_t objectY) {
    uint32_t scaledBits = static_cast<uint32_t>(static_cast<int32_t>(localHeightWord));
    for (uint16_t count = 0; count < scaleShift; ++count) {
        scaledBits <<= 1u;
    }
    return signedDword(scaledBits + static_cast<uint32_t>(objectY));
}

TerpckGiContactCorrection terpckGiContactCorrection(
    const TerpckGiCollisionSubrecord& subrecord,
    uint8_t liveByte15,
    uint8_t liveByte17,
    uint8_t liveByte19) {
    const auto desiredByte = [&subrecord](uint8_t difference) {
        const uint8_t folded = difference >= 0x80u
                                   ? static_cast<uint8_t>(0u - difference)
                                   : difference;
        const uint16_t factor = static_cast<uint16_t>(0x40u - folded);
        const uint16_t product = static_cast<uint16_t>(
            static_cast<uint32_t>(subrecord.byte7) * static_cast<uint32_t>(factor));
        return static_cast<uint8_t>(product >> 6u);
    };
    const uint8_t difference0 = static_cast<uint8_t>(subrecord.byte8 - liveByte19);
    const uint8_t difference1 =
        static_cast<uint8_t>(subrecord.byte8 - liveByte19 - 0x40u);

    TerpckGiContactCorrection result;
    result.desiredByte0 = desiredByte(difference0);
    result.desiredByte1 = desiredByte(difference1);
    const auto boundedWord = [](uint8_t desired, uint8_t current) {
        const int desiredSigned = desired <= 0x7fu ? desired : static_cast<int>(desired) - 0x100;
        const int currentSigned = current <= 0x7fu ? current : static_cast<int>(current) - 0x100;
        if (desiredSigned < currentSigned) {
            return static_cast<int16_t>(-0x0100);
        }
        if (desiredSigned > currentSigned) {
            return static_cast<int16_t>(0x0100);
        }
        return static_cast<int16_t>(0);
    };
    result.controlWord30 = boundedWord(result.desiredByte0, liveByte15);
    result.controlWord32 = boundedWord(result.desiredByte1, liveByte17);
    return result;
}

TerrainMesh buildTerrainMesh(const TerrainGrid& grid, float cellSize, float heightScale) {
    (void)heightScale;
    if (grid.width <= 0 || grid.height <= 0 || grid.samples.size() != static_cast<size_t>(grid.width * grid.height)) {
        throw ParseError("invalid terrain grid dimensions");
    }

    TerrainMesh mesh;
    mesh.vertices.reserve(grid.samples.size());
    mesh.indices.reserve(static_cast<size_t>(grid.width - 1) * static_cast<size_t>(grid.height - 1) * 6u);

    mesh.boundsMin = {
        0.0f,
        std::numeric_limits<float>::max(),
        0.0f,
    };
    mesh.boundsMax = {
        static_cast<float>(grid.width - 1) * cellSize,
        std::numeric_limits<float>::lowest(),
        static_cast<float>(grid.height - 1) * cellSize,
    };

    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            const TerrainGridSample& sample = grid.sampleAt(x, y);
            TerrainMeshVertex vertex;
            vertex.x = static_cast<float>(x) * cellSize;
            vertex.y = 0.0f;
            vertex.z = static_cast<float>(y) * cellSize;
            vertex.rawValue = sample.rawValue;
            vertex.colorBand = sample.colorBand();
            vertex.detailBits = sample.detailBits();
            mesh.boundsMin[1] = std::min(mesh.boundsMin[1], vertex.y);
            mesh.boundsMax[1] = std::max(mesh.boundsMax[1], vertex.y);
            mesh.vertices.push_back(vertex);
        }
    }

    for (int y = 0; y + 1 < grid.height; ++y) {
        for (int x = 0; x + 1 < grid.width; ++x) {
            const uint32_t a = static_cast<uint32_t>(y * grid.width + x);
            const uint32_t b = a + 1u;
            const uint32_t c = static_cast<uint32_t>((y + 1) * grid.width + x);
            const uint32_t d = c + 1u;
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(b);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }

    return mesh;
}

std::array<uint8_t, 16> terrainPaletteBank1Bytes(const std::string& paletteId) {
    static constexpr std::array<uint8_t, 16> desert{{
        0x6fu, 0x84u, 0x76u, 0x44u, 0x86u, 0xe6u, 0x36u, 0x76u,
        0x86u, 0x78u, 0x70u, 0xf6u, 0x8fu, 0x80u, 0x64u, 0x7fu,
    }};
    static constexpr std::array<uint8_t, 16> green{{
        0x2au, 0x12u, 0xaau, 0x32u, 0x82u, 0xe2u, 0x32u, 0x72u,
        0x82u, 0x78u, 0x70u, 0xf2u, 0x8fu, 0x80u, 0x62u, 0x7fu,
    }};
    static constexpr std::array<uint8_t, 16> snow{{
        0xf7u, 0x07u, 0x77u, 0x88u, 0x8fu, 0xffu, 0x38u, 0x7fu,
        0x87u, 0x78u, 0x70u, 0xffu, 0x8fu, 0x37u, 0x83u, 0x7fu,
    }};
    if (paletteId == "desert") {
        return desert;
    }
    if (paletteId == "snow") {
        return snow;
    }
    return green;
}

GpuBatch buildOriginalTerrainPaletteGpuBatch(
    const RuntimeRecord& record,
    const GpuBatchOptions& options,
    const std::string& paletteId,
    bool overlayColors) {
    GpuBatch batch;
    std::vector<std::array<float, 3>> transformed;
    transformed.reserve(record.vertices.size());
    for (Vec3i vertex : record.vertices) {
        transformed.push_back(transformTerrainShapeVertex(vertex, options.scale, options.swizzle));
    }
    const auto appendVertex = [&batch](
                                  const std::array<float, 3>& vertex,
                                  const std::array<float, 3>& rgb) {
        batch.vertices.push_back(vertex[0]);
        batch.vertices.push_back(vertex[1]);
        batch.vertices.push_back(vertex[2]);
        batch.vertices.push_back(rgb[0]);
        batch.vertices.push_back(rgb[1]);
        batch.vertices.push_back(rgb[2]);
    };
    const std::array<float, 3> lineRgb{0.94f, 0.94f, 0.94f};
    for (const RuntimePart& part : record.parts) {
        for (const RuntimeCommand& command : part.commands) {
            for (const RuntimePrimitive& primitive : command.primitives) {
                const std::vector<int> valid =
                    terrainShapeValidIndices(record, primitive, options.indexMode);
                if (valid.size() < 2u) {
                    continue;
                }
                if (primitive.kind == "line") {
                    if (options.showLines && options.showEdges) {
                        const uint32_t first = static_cast<uint32_t>(batch.vertices.size() / 6u);
                        for (int index : valid) {
                            appendVertex(transformed[static_cast<size_t>(index)], lineRgb);
                        }
                        for (size_t index = 0; index + 1u < valid.size(); ++index) {
                            batch.lineIndices.push_back(first + static_cast<uint32_t>(index));
                            batch.lineIndices.push_back(first + static_cast<uint32_t>(index + 1u));
                        }
                    }
                    continue;
                }
                if (primitive.kind.rfind("polygon", 0) != 0 || valid.size() < 3u) {
                    continue;
                }
                const std::array<float, 3> rgb = terrainPaletteShadeRgb(
                    primitive.shadeBytes[2],
                    paletteId,
                    overlayColors);
                const uint32_t first = static_cast<uint32_t>(batch.vertices.size() / 6u);
                for (int index : valid) {
                    appendVertex(transformed[static_cast<size_t>(index)], rgb);
                }
                for (size_t index = 1u; index + 1u < valid.size(); ++index) {
                    batch.triangleIndices.push_back(first);
                    batch.triangleIndices.push_back(first + static_cast<uint32_t>(index));
                    batch.triangleIndices.push_back(first + static_cast<uint32_t>(index + 1u));
                }
                if (options.showEdges) {
                    const uint32_t firstEdge = static_cast<uint32_t>(batch.vertices.size() / 6u);
                    for (int index : valid) {
                        appendVertex(transformed[static_cast<size_t>(index)], options.edgeRgb);
                    }
                    for (size_t index = 0; index < valid.size(); ++index) {
                        batch.lineIndices.push_back(firstEdge + static_cast<uint32_t>(index));
                        batch.lineIndices.push_back(
                            firstEdge + static_cast<uint32_t>((index + 1u) % valid.size()));
                    }
                }
            }
        }
    }
    return batch;
}

std::array<uint8_t, 128> terrainCheckerStipple4x4() {
    std::array<uint8_t, 128> pattern{};
    for (size_t row = 0; row < 32u; ++row) {
        const uint8_t value = ((row / 4u) % 2u) == 0u ? 0x0fu : 0xf0u;
        for (size_t column = 0; column < 4u; ++column) {
            pattern[row * 4u + column] = value;
        }
    }
    return pattern;
}

} // namespace mw::legacy3d
