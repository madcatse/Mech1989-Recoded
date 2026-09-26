#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mw::legacy3d {

struct RuntimeRecord;
struct GpuBatchOptions;
struct GpuBatch;

struct TerrainGridSample {
    uint8_t rawValue = 0;

    bool empty() const;
    bool blocking() const;
    uint8_t colorBand() const;
    uint8_t detailBits() const;
};

struct TerrainGrid {
    std::string resourceName;
    int width = 0;
    int height = 0;
    std::vector<TerrainGridSample> samples;

    const TerrainGridSample& sampleAt(int x, int y) const;
};

struct TerrainWorldObject {
    uint16_t recordIndex = 0;
    uint16_t rawXLow = 0;
    int16_t rawXHigh = 0;
    uint16_t rawZLow = 0;
    int16_t rawZHigh = 0;
    uint16_t rawYLow = 0;
    int16_t rawYHigh = 0;
    int32_t y = 0;
    // Pre-Phase-13 aliases retained for diagnostic output compatibility.
    int16_t unknownA = 0;
    int16_t unknownB = 0;
    int32_t x = 0;
    int32_t z = 0;
};

struct TerrainWorld {
    std::string resourceName;
    std::vector<TerrainWorldObject> objects;
};

struct TerrainArenaLayout {
    std::string id;
    std::vector<std::string> tileNames;
    float layoutGap = 0.0f;
    float objectInset = 0.0f;
    std::string description;
};

struct TerrainScenarioRecord {
    size_t index = 0;
    std::array<uint8_t, 4> tileIds{0, 0, 0, 0};
    uint8_t terrainMode = 0;
    std::array<uint8_t, 16> raw{};

    bool terrainEnabled() const;
    bool usesBVariant() const;
    bool hasValidTileIds() const;
    bool isTerrainLayoutCandidate() const;
};

struct TerrainPlacementBounds {
    float minX = 0.0f;
    float maxX = 0.0f;
    float minZ = 0.0f;
    float maxZ = 0.0f;
};

struct TerrainObjectPlacement {
    size_t sourceIndex = 0;
    uint16_t recordIndex = 0;
    int32_t rawX = 0;
    int32_t rawZ = 0;
    float x = 0.0f;
    float z = 0.0f;
};

struct TerpckGiIndexEntry {
    uint16_t attrOrColor = 0;
    uint16_t relOffset = 0;
    uint16_t absOffset = 0;
    uint16_t length = 0;
};

struct TerpckGiQuad {
    int16_t a = 0;
    int16_t b = 0;
    int16_t c = 0;
    int16_t d = 0;
};

struct TerpckGiFarPointer {
    uint16_t offset = 0;
    uint16_t segment = 0;
    uint32_t linearOffset = 0;
};

struct TerpckGiHalfPlaneEdge {
    int16_t coefficientA = 0;
    int16_t coefficientB = 0;
    int16_t anchorX = 0;
    int16_t anchorZ = 0;
};

struct TerpckGiPlaneTuple {
    int16_t word0 = 0;
    int16_t word2 = 0;
    int16_t word4 = 0;
    int16_t word6 = 0;
};

struct TerpckGiCollisionSubrecord {
    uint32_t linearOffset = 0;
    int16_t word0 = 0;
    int16_t word2 = 0;
    int16_t word4 = 0;
    uint8_t heightScaleByte6 = 0;
    uint8_t byte7 = 0;
    uint8_t byte8 = 0;
    uint8_t edgeCount = 0;
    uint16_t edgeTableOffset = 0;
    uint32_t edgeTableLinearOffset = 0;
    int16_t baseHeightWord = 0;
    uint16_t planeOffset = 0;
    uint32_t planeLinearOffset = 0;
    std::vector<TerpckGiHalfPlaneEdge> edges;
    TerpckGiPlaneTuple plane;
};

struct TerpckGiCollisionRecord {
    size_t recordIndex = 0;
    TerpckGiFarPointer pointer;
    uint16_t extentX = 0;
    uint16_t extentZ = 0;
    uint8_t modeByte4 = 0;
    uint8_t priorityByte5 = 0;
    uint8_t resultByte6 = 0;
    uint8_t subrecordCountByte7 = 0;
    uint16_t subrecordTableOffset = 0;
    uint32_t subrecordTableLinearOffset = 0;
    std::vector<TerpckGiCollisionSubrecord> subrecords;
};

struct TerpckGiPlanarQueryResult {
    int32_t rawDeltaX = 0;
    int32_t rawDeltaZ = 0;
    int32_t scaledDeltaX = 0;
    int32_t scaledDeltaZ = 0;
    uint32_t absoluteScaledX = 0;
    uint32_t absoluteScaledZ = 0;
    bool withinExtentX = false;
    bool withinExtentZ = false;
    bool broadPhaseAccepted = false;
    bool modeBypassedNarrowPhase = false;
    bool cachedSubrecordTested = false;
    bool cachedSubrecordAccepted = false;
    size_t testedSubrecords = 0;
    std::optional<size_t> subrecordIndex;
};

struct TerpckGiContactCorrection {
    uint8_t desiredByte0 = 0;
    uint8_t desiredByte1 = 0;
    int16_t controlWord30 = 0;
    int16_t controlWord32 = 0;
};

struct TerpckGiResource {
    std::string resourceName;
    size_t packedSize = 0;
    size_t decodedSize = 0;
    std::vector<TerpckGiCollisionRecord> collisionRecords;
    // Provisional pre-Phase-13 views retained for diagnostic compatibility.
    // They are not the authoritative collision interpretation.
    std::vector<TerpckGiIndexEntry> index;
    std::vector<TerpckGiQuad> quads;
};

struct TerrainMeshVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    uint8_t rawValue = 0;
    uint8_t colorBand = 0;
    uint8_t detailBits = 0;
};

struct TerrainMesh {
    std::vector<TerrainMeshVertex> vertices;
    std::vector<uint32_t> indices;
    std::array<float, 3> boundsMin{0.0f, 0.0f, 0.0f};
    std::array<float, 3> boundsMax{0.0f, 0.0f, 0.0f};

    float sampleHeightNearest(float x, float z) const;
};

TerrainGrid loadTerrainGrid(const std::filesystem::path& path);
TerrainWorld loadTerrainWorld(const std::filesystem::path& path);
std::vector<TerrainScenarioRecord> loadTerrainScenarioRecords(const std::filesystem::path& path);
std::optional<TerrainScenarioRecord> terrainScenarioRecordByIndex(
    const std::vector<TerrainScenarioRecord>& records,
    size_t index);
std::vector<std::string> terrainScenarioTileNames(const TerrainScenarioRecord& record);
TerpckGiResource loadTerpckGiResource(const std::filesystem::path& path);
bool terpckGiSubrecordContainsPoint(
    const TerpckGiCollisionSubrecord& subrecord,
    int16_t pointX,
    int16_t pointZ);
TerpckGiPlanarQueryResult terpckGiQueryRecordPlanar(
    const TerpckGiCollisionRecord& record,
    int32_t queryX,
    int32_t queryZ,
    int32_t objectX,
    int32_t objectZ,
    uint8_t scaleShift,
    std::optional<uint8_t> cachedSubrecordIndex = std::nullopt);
int16_t terpckGiSubrecordHeightWord(
    const TerpckGiCollisionSubrecord& subrecord,
    int16_t pointX,
    int16_t pointZ);
int32_t terpckGiScaleHeightToWorld(
    int16_t localHeightWord,
    uint8_t scaleShift,
    int32_t objectY);
TerpckGiContactCorrection terpckGiContactCorrection(
    const TerpckGiCollisionSubrecord& subrecord,
    uint8_t liveByte15,
    uint8_t liveByte17,
    uint8_t liveByte19);
TerrainMesh buildTerrainMesh(const TerrainGrid& grid, float cellSize = 1.0f, float heightScale = 1.0f);
TerrainGrid composeTerrainGrid2x2(
    const std::array<TerrainGrid, 4>& grids,
    const std::string& resourceName,
    bool mirrorTileX = false,
    bool mirrorTileY = false);
const std::vector<TerrainArenaLayout>& terrainArenaLayouts();
const TerrainArenaLayout& terrainArenaLayoutById(const std::string& id);
std::vector<TerrainObjectPlacement> normalizeTerrainWorldPlacements(
    const TerrainWorld& world,
    TerrainPlacementBounds bounds,
    float insetFraction);
std::vector<TerrainObjectPlacement> mapTerrainWorldPlacementsFromRawCoordinates(
    const TerrainWorld& world,
    TerrainPlacementBounds bounds,
    float cellSize);
std::array<uint8_t, 16> terrainPaletteBank1Bytes(const std::string& paletteId);
GpuBatch buildOriginalTerrainPaletteGpuBatch(
    const RuntimeRecord& record,
    const GpuBatchOptions& options,
    const std::string& paletteId,
    bool overlayColors);
std::array<uint8_t, 128> terrainCheckerStipple4x4();

} // namespace mw::legacy3d
