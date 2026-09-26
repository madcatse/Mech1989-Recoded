#include "legacy3d/shape_parser.h"
#include "legacy3d/terrain_parser.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

void printUsage() {
    std::cerr
        << "usage: mw_terrain_dump [--grid TILE.GRD] [--world TILE.WLD] "
           "[--terpck-gi TERPCK.GI] [--terpck-tbl TERPCK.TBL] [--terpck-catalog] [--mesh] "
           "[--snario SNARIO.DAT] [--scenario-index N] "
           "[--scan-terrain SORTED_ORIGINAL_FILES_DIR] [--list-terrain-layouts] "
           "[--terrain-layout-preset NAME] [--terrain-minimap OUT.BMP] [--terrain-minimap-size WxH] "
           "[--terrain-scenario-report OUT_DIR] "
           "[--terrain-grid-probe X,Y] "
           "[--terrain-layout-gap N] [--terrain-cell-size N] [--terrain-object-inset FRACTION] [--terrain-object-scale N] "
           "[--terrain-minimap-rotation none|cw|ccw] "
           "[--terrain-environment-id 0|1|2] "
           "[--terrain-color debug|flat|desert|green|snow|tropical|jungle|arctic|ice] "
           "[--terrain-no-object-mirror-y] "
           "[--terrain-tile-mirror-x] [--terrain-no-tile-mirror-y] "
           "[--terrain-root SORTED_ORIGINAL_FILES_DIR]\n";
}

std::string joinIds(const std::set<uint16_t>& ids) {
    std::ostringstream out;
    bool first = true;
    for (uint16_t id : ids) {
        if (!first) {
            out << ",";
        }
        out << id;
        first = false;
    }
    return out.str();
}

std::string joinSignedIds(const std::set<int16_t>& ids) {
    std::ostringstream out;
    bool first = true;
    for (int16_t id : ids) {
        if (!first) {
            out << ",";
        }
        out << id;
        first = false;
    }
    return out.str();
}

std::string joinPaths(const std::vector<std::string>& names) {
    std::ostringstream out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << names[i];
    }
    return out.str();
}

std::string joinNames(const std::set<std::string>& names) {
    std::ostringstream out;
    bool first = true;
    for (const std::string& name : names) {
        if (!first) {
            out << ",";
        }
        out << name;
        first = false;
    }
    return out.str();
}

std::string formatFloat(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

std::string formatScaleTag(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    std::string text = out.str();
    text.erase(std::remove(text.begin(), text.end(), '.'), text.end());
    return "scale" + text;
}

std::string formatByteList(const std::array<uint8_t, 16>& bytes) {
    std::ostringstream out;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << static_cast<int>(bytes[i]);
    }
    return out.str();
}

std::string joinRefs(const std::set<std::string>& refs) {
    std::ostringstream out;
    bool first = true;
    for (const std::string& ref : refs) {
        if (!first) {
            out << ",";
        }
        out << ref;
        first = false;
    }
    return out.str();
}

struct GridStats {
    size_t uniqueSamples = 0;
    size_t nonzeroSamples = 0;
    size_t blockingSamples = 0;
    std::set<uint16_t> rawValues;
    std::set<uint16_t> detailValues;
    std::array<size_t, 4> colorBandSamples{0, 0, 0, 0};
    std::array<size_t, 8> rawTopBitSamples{0, 0, 0, 0, 0, 0, 0, 0};
    std::map<uint8_t, size_t> rawFrequencies;
    std::map<uint8_t, size_t> detailFrequencies;
};

GridStats computeGridStats(const mw::legacy3d::TerrainGrid& grid) {
    GridStats stats;
    for (const mw::legacy3d::TerrainGridSample& sample : grid.samples) {
        stats.rawValues.insert(sample.rawValue);
        stats.detailValues.insert(sample.detailBits());
        ++stats.rawFrequencies[sample.rawValue];
        ++stats.detailFrequencies[sample.detailBits()];
        ++stats.colorBandSamples[sample.colorBand()];
        ++stats.rawTopBitSamples[static_cast<size_t>(sample.rawValue >> 5u)];
        if (sample.blocking()) {
            ++stats.blockingSamples;
        }
        stats.nonzeroSamples += sample.empty() ? 0u : 1u;
    }
    stats.uniqueSamples = stats.rawValues.size();
    return stats;
}

template <size_t N>
std::string formatCountArray(const std::array<size_t, N>& counts) {
    std::ostringstream out;
    for (size_t i = 0; i < counts.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << i << ":" << counts[i];
    }
    return out.str();
}

std::string formatFrequentBytes(const std::map<uint8_t, size_t>& frequencies, size_t limit = 6) {
    std::vector<std::pair<uint8_t, size_t>> entries(frequencies.begin(), frequencies.end());
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    std::ostringstream out;
    for (size_t i = 0; i < std::min(limit, entries.size()); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << static_cast<int>(entries[i].first) << ":" << entries[i].second;
    }
    return out.str();
}

std::string formatByteValues(const std::map<uint8_t, size_t>& frequencies) {
    std::ostringstream out;
    bool first = true;
    for (const auto& item : frequencies) {
        if (!first) {
            out << ",";
        }
        out << static_cast<int>(item.first);
        first = false;
    }
    return out.str();
}

struct GrdPatchStats {
    size_t id = 0;
    size_t cells = 0;
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();
    uint8_t minRaw = std::numeric_limits<uint8_t>::max();
    uint8_t maxRaw = 0;
    uint8_t minDetail = std::numeric_limits<uint8_t>::max();
    uint8_t maxDetail = 0;
    double sumX = 0.0;
    double sumY = 0.0;
    std::array<size_t, 4> colorBandSamples{0, 0, 0, 0};
    std::map<uint8_t, size_t> rawFrequencies;
    std::map<uint8_t, size_t> detailFrequencies;
    std::set<std::string> tileSlots;
};

std::vector<GrdPatchStats> collectCompositeGrdPatches(
    const std::vector<std::string>& tileNames,
    const std::vector<mw::legacy3d::TerrainGrid>& grids) {
    constexpr int compositeWidth = 174;
    constexpr int compositeHeight = 94;
    if (tileNames.size() != 4 || grids.size() != 4) {
        throw mw::legacy3d::ParseError("GRD patch report requires four scenario tiles");
    }

    auto sampleComposite = [&](int x, int y) -> const mw::legacy3d::TerrainGridSample& {
        const int tileX = x / 87;
        const int tileY = y / 47;
        const size_t tileIndex = static_cast<size_t>(tileY * 2 + tileX);
        return grids[tileIndex].sampleAt(x % 87, y % 47);
    };

    std::vector<uint8_t> visited(static_cast<size_t>(compositeWidth * compositeHeight), 0);
    auto offset = [](int x, int y) -> size_t {
        return static_cast<size_t>(y * compositeWidth + x);
    };

    std::vector<GrdPatchStats> patches;
    std::vector<std::pair<int, int>> stack;
    for (int y = 0; y < compositeHeight; ++y) {
        for (int x = 0; x < compositeWidth; ++x) {
            if (visited[offset(x, y)] != 0 || !sampleComposite(x, y).blocking()) {
                continue;
            }

            GrdPatchStats patch;
            patch.id = patches.size();
            stack.clear();
            stack.emplace_back(x, y);
            visited[offset(x, y)] = 1;

            while (!stack.empty()) {
                const auto [cx, cy] = stack.back();
                stack.pop_back();
                const mw::legacy3d::TerrainGridSample& sample = sampleComposite(cx, cy);
                const int tileX = cx / 87;
                const int tileY = cy / 47;
                const size_t tileIndex = static_cast<size_t>(tileY * 2 + tileX);

                ++patch.cells;
                patch.minX = std::min(patch.minX, cx);
                patch.minY = std::min(patch.minY, cy);
                patch.maxX = std::max(patch.maxX, cx);
                patch.maxY = std::max(patch.maxY, cy);
                patch.minRaw = std::min(patch.minRaw, sample.rawValue);
                patch.maxRaw = std::max(patch.maxRaw, sample.rawValue);
                patch.minDetail = std::min(patch.minDetail, sample.detailBits());
                patch.maxDetail = std::max(patch.maxDetail, sample.detailBits());
                patch.sumX += static_cast<double>(cx);
                patch.sumY += static_cast<double>(cy);
                ++patch.colorBandSamples[sample.colorBand()];
                ++patch.rawFrequencies[sample.rawValue];
                ++patch.detailFrequencies[sample.detailBits()];
                patch.tileSlots.insert(std::to_string(tileIndex) + ":" + tileNames[tileIndex]);

                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const int nx = cx + dx;
                        const int ny = cy + dy;
                        if (nx < 0 || ny < 0 || nx >= compositeWidth || ny >= compositeHeight) {
                            continue;
                        }
                        const size_t nextOffset = offset(nx, ny);
                        if (visited[nextOffset] != 0 || !sampleComposite(nx, ny).blocking()) {
                            continue;
                        }
                        visited[nextOffset] = 1;
                        stack.emplace_back(nx, ny);
                    }
                }
            }

            patches.push_back(std::move(patch));
        }
    }

    std::sort(patches.begin(), patches.end(), [](const GrdPatchStats& a, const GrdPatchStats& b) {
        if (a.cells != b.cells) {
            return a.cells > b.cells;
        }
        if (a.minY != b.minY) {
            return a.minY < b.minY;
        }
        return a.minX < b.minX;
    });
    for (size_t i = 0; i < patches.size(); ++i) {
        patches[i].id = i;
    }
    return patches;
}

uint64_t fnv1a64File(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw mw::legacy3d::ParseError("could not open file for hash: " + path.string());
    }
    uint64_t value = 14695981039346656037ull;
    char byte = 0;
    while (input.get(byte)) {
        value ^= static_cast<uint64_t>(static_cast<uint8_t>(byte));
        value *= 1099511628211ull;
    }
    return value;
}

std::string formatHash(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

struct WorldStats {
    std::set<uint16_t> recordIds;
    std::set<int16_t> unknownAValues;
    std::set<int16_t> unknownBValues;
    int32_t minX = 0;
    int32_t maxX = 0;
    int32_t minZ = 0;
    int32_t maxZ = 0;
    int32_t minY = 0;
    int32_t maxY = 0;
};

struct BatchBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
};

struct TerpckRecordInfo {
    bool present = false;
    size_t sourceVertices = 0;
    int polygons = 0;
    int lines = 0;
    size_t gpuVertices = 0;
    size_t triangles = 0;
    size_t linePairs = 0;
    float width = 0.0f;
    float height = 0.0f;
    float depth = 0.0f;
    std::string profile;
};

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

struct TerrainPalette {
    std::string id;
    Rgb ground;
    Rgb grid;
    Rgb marker;
    Rgb objectDark;
    Rgb objectLight;
    Rgb edge;
};

std::string canonicalTerrainColorMode(std::string id) {
    for (char& ch : id) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    if (id == "tropic" || id == "tropical" || id == "jungle") {
        return "green";
    }
    if (id == "ice" || id == "arctic") {
        return "snow";
    }
    return id;
}

std::string terrainColorModeForEnvironmentId(int environmentId) {
    switch (environmentId) {
    case 0:
        return "desert";
    case 1:
        return "green";
    case 2:
        return "snow";
    default:
        throw mw::legacy3d::ParseError("--terrain-environment-id must be 0, 1, or 2");
    }
}

const TerrainPalette& terrainPaletteById(const std::string& id) {
    static const std::array<TerrainPalette, 5> palettes{{
        {"debug", {0, 176, 0}, {0, 112, 0}, {112, 220, 112}, {104, 144, 40}, {245, 232, 96}, {24, 24, 24}},
        {"flat", {31, 48, 26}, {18, 30, 16}, {84, 116, 68}, {94, 122, 58}, {188, 198, 124}, {18, 18, 18}},
        {"desert", {176, 91, 0}, {122, 62, 0}, {226, 151, 55}, {151, 75, 23}, {239, 178, 72}, {44, 20, 4}},
        {"green", {29, 121, 34}, {16, 76, 23}, {94, 181, 76}, {43, 106, 34}, {147, 198, 93}, {12, 30, 13}},
        {"snow", {202, 231, 230}, {133, 183, 185}, {252, 255, 255}, {104, 154, 167}, {240, 255, 255}, {34, 70, 79}},
    }};
    const auto found = std::find_if(palettes.begin(), palettes.end(), [&id](const TerrainPalette& palette) {
        return palette.id == id;
    });
    if (found == palettes.end()) {
        throw mw::legacy3d::ParseError("unknown terrain color mode: " + id);
    }
    return *found;
}

Rgb paletteObjectColor(const Rgb& source, const TerrainPalette& palette) {
    const float luminance = (static_cast<float>(source.r) * 0.2126f +
                             static_cast<float>(source.g) * 0.7152f +
                             static_cast<float>(source.b) * 0.0722f) / 255.0f;
    const float t = std::clamp(luminance, 0.0f, 1.0f);
    return Rgb{
        static_cast<uint8_t>(std::round(static_cast<float>(palette.objectDark.r) +
                                        (static_cast<float>(palette.objectLight.r) - palette.objectDark.r) * t)),
        static_cast<uint8_t>(std::round(static_cast<float>(palette.objectDark.g) +
                                        (static_cast<float>(palette.objectLight.g) - palette.objectDark.g) * t)),
        static_cast<uint8_t>(std::round(static_cast<float>(palette.objectDark.b) +
                                        (static_cast<float>(palette.objectLight.b) - palette.objectDark.b) * t)),
    };
}

Rgb egaRgb(uint8_t color) {
    switch (color & 0x0fu) {
    case 0:
        return {0, 0, 0};
    case 1:
        return {0, 0, 170};
    case 2:
        return {0, 170, 0};
    case 3:
        return {0, 170, 170};
    case 4:
        return {170, 0, 0};
    case 5:
        return {170, 0, 170};
    case 6:
        return {170, 85, 0};
    case 7:
        return {170, 170, 170};
    case 8:
        return {85, 85, 85};
    case 9:
        return {85, 85, 255};
    case 10:
        return {85, 255, 85};
    case 11:
        return {85, 255, 255};
    case 12:
        return {255, 85, 85};
    case 13:
        return {255, 85, 255};
    case 14:
        return {255, 255, 85};
    default:
        return {255, 255, 255};
    }
}

uint8_t terrainPalBank1Byte(const std::string& id, uint8_t index) {
    static constexpr std::array<uint8_t, 16> desert{{
        0x6fu, 0x84u, 0x76u, 0x44u, 0x86u, 0xe6u, 0x36u, 0x76u,
        0x86u, 0x78u, 0x70u, 0xf6u, 0x8fu, 0x80u, 0x64u, 0x7fu,
    }};
    static constexpr std::array<uint8_t, 16> tropic{{
        0x2au, 0x12u, 0xaau, 0x32u, 0x82u, 0xe2u, 0x32u, 0x72u,
        0x82u, 0x78u, 0x70u, 0xf2u, 0x8fu, 0x80u, 0x62u, 0x7fu,
    }};
    static constexpr std::array<uint8_t, 16> arctic{{
        0xf7u, 0x07u, 0x77u, 0x88u, 0x8fu, 0xffu, 0x38u, 0x7fu,
        0x87u, 0x78u, 0x70u, 0xffu, 0x8fu, 0x37u, 0x83u, 0x7fu,
    }};

    const size_t offset = static_cast<size_t>(index & 0x0fu);
    if (id == "desert") {
        return desert[offset];
    }
    if (id == "snow") {
        return arctic[offset];
    }
    return tropic[offset];
}

Rgb terrainShadeRgb(uint8_t shade, const std::string& terrainColorMode) {
    if (terrainColorMode == "debug" || terrainColorMode == "flat" || shade < 16u) {
        return egaRgb(shade);
    }
    const uint8_t remapped = terrainPalBank1Byte(terrainColorMode, shade & 0x0fu);
    uint8_t color = remapped >> 4u;
    if (color == 1u || color == 3u || color == 5u || color == 9u || color == 11u) {
        color = remapped & 0x0fu;
    }
    return egaRgb(color);
}

Rgb terrainShadeBaseRgb(uint8_t shade, const std::string& terrainColorMode) {
    if (terrainColorMode == "debug" || terrainColorMode == "flat" || shade < 16u) {
        return egaRgb(shade);
    }
    return egaRgb(terrainPalBank1Byte(terrainColorMode, shade & 0x0fu) & 0x0fu);
}

Rgb terrainShadeOverlayRgb(uint8_t shade, const std::string& terrainColorMode) {
    if (terrainColorMode == "debug" || terrainColorMode == "flat" || shade < 16u) {
        return egaRgb(shade);
    }
    const uint8_t remapped = terrainPalBank1Byte(terrainColorMode, shade & 0x0fu);
    uint8_t color = remapped >> 4u;
    if (color == 1u || color == 3u || color == 5u || color == 9u || color == 11u) {
        color = terrainColorMode == "desert" || terrainColorMode == "green" ? 0u : (remapped & 0x0fu);
    } else if (terrainColorMode == "green" && color == 2u) {
        color = 0u;
    }
    return egaRgb(color);
}

struct Raster {
    int width = 0;
    int height = 0;
    std::vector<Rgb> pixels;

    Raster(int w, int h, Rgb fill)
        : width(w), height(h), pixels(static_cast<size_t>(w) * static_cast<size_t>(h), fill) {}

    void set(int x, int y, Rgb rgb) {
        if (x < 0 || y < 0 || x >= width || y >= height) {
            return;
        }
        pixels[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)] = rgb;
    }
};

struct MinimapSize {
    int width = 320;
    int height = 200;
};

struct GridProbeCoord {
    int x = 0;
    int y = 0;
};

enum class MinimapRotation {
    None,
    Clockwise,
    CounterClockwise,
};

struct LayoutResources {
    mw::legacy3d::TerrainMesh mesh;
    mw::legacy3d::GpuBatch objects;
    mw::legacy3d::GpuBatch objectDither;
    size_t wldObjects = 0;
};

BatchBounds computeBatchBounds(const mw::legacy3d::GpuBatch& batch) {
    BatchBounds bounds;
    if (batch.vertices.empty()) {
        return bounds;
    }
    bounds.minX = bounds.maxX = batch.vertices[0];
    bounds.minY = bounds.maxY = batch.vertices[1];
    bounds.minZ = bounds.maxZ = batch.vertices[2];
    for (size_t i = 0; i + 5u < batch.vertices.size(); i += 6u) {
        bounds.minX = std::min(bounds.minX, batch.vertices[i]);
        bounds.minY = std::min(bounds.minY, batch.vertices[i + 1u]);
        bounds.minZ = std::min(bounds.minZ, batch.vertices[i + 2u]);
        bounds.maxX = std::max(bounds.maxX, batch.vertices[i]);
        bounds.maxY = std::max(bounds.maxY, batch.vertices[i + 1u]);
        bounds.maxZ = std::max(bounds.maxZ, batch.vertices[i + 2u]);
    }
    return bounds;
}

std::string terrainShapeProfile(float width, float depth, float height) {
    const float footprintMax = std::max(width, depth);
    const float footprintMin = std::min(width, depth);
    if (footprintMax <= 0.0f) {
        return "empty";
    }
    if (height <= 300.0f) {
        return "flat";
    }
    if (footprintMin / footprintMax < 0.18f) {
        return "thin";
    }
    if (height < footprintMax * 0.18f) {
        return "low";
    }
    if (height > footprintMax * 0.45f) {
        return "tall";
    }
    if (footprintMax > 750.0f) {
        return "broad";
    }
    return "compact";
}

TerpckRecordInfo buildTerpckRecordInfo(const mw::legacy3d::RuntimeRecord& record) {
    const mw::legacy3d::RecordStats stats = mw::legacy3d::recordStats(record);
    const mw::legacy3d::GpuBatch batch = mw::legacy3d::buildGpuBatch(record);
    const BatchBounds bounds = computeBatchBounds(batch);
    TerpckRecordInfo info;
    info.present = true;
    info.sourceVertices = record.vertices.size();
    info.polygons = stats.polygonCount;
    info.lines = stats.lineCount;
    info.gpuVertices = batch.vertices.size() / 6u;
    info.triangles = batch.triangleIndices.size() / 3u;
    info.linePairs = batch.lineIndices.size() / 2u;
    info.width = bounds.maxX - bounds.minX;
    info.height = bounds.maxY - bounds.minY;
    info.depth = bounds.maxZ - bounds.minZ;
    info.profile = terrainShapeProfile(info.width, info.depth, info.height);
    return info;
}

std::map<uint16_t, TerpckRecordInfo> buildTerpckRecordInfoMap(
    const std::vector<mw::legacy3d::RuntimeRecord>& records) {
    std::map<uint16_t, TerpckRecordInfo> infos;
    for (const mw::legacy3d::RuntimeRecord& record : records) {
        if (record.recordIndex >= 0 && record.recordIndex <= 0xffff) {
            infos[static_cast<uint16_t>(record.recordIndex)] = buildTerpckRecordInfo(record);
        }
    }
    return infos;
}

void appendTerrainMesh(
    mw::legacy3d::TerrainMesh& out,
    const mw::legacy3d::TerrainMesh& source,
    float offsetX,
    float offsetZ,
    bool mirrorX,
    bool mirrorY) {
    const bool wasEmpty = out.vertices.empty();
    const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());
    const float localMinX = source.boundsMin[0];
    const float localMaxX = source.boundsMax[0];
    const float localMinZ = source.boundsMin[2];
    const float localMaxZ = source.boundsMax[2];
    for (mw::legacy3d::TerrainMeshVertex vertex : source.vertices) {
        if (mirrorX) {
            vertex.x = localMinX + localMaxX - vertex.x;
        }
        if (mirrorY) {
            vertex.z = localMinZ + localMaxZ - vertex.z;
        }
        vertex.x += offsetX;
        vertex.z += offsetZ;
        out.vertices.push_back(vertex);
    }
    for (uint32_t index : source.indices) {
        out.indices.push_back(baseVertex + index);
    }

    const std::array<float, 3> sourceMin{
        source.boundsMin[0] + offsetX,
        source.boundsMin[1],
        source.boundsMin[2] + offsetZ,
    };
    const std::array<float, 3> sourceMax{
        source.boundsMax[0] + offsetX,
        source.boundsMax[1],
        source.boundsMax[2] + offsetZ,
    };
    if (wasEmpty) {
        out.boundsMin = sourceMin;
        out.boundsMax = sourceMax;
        return;
    }
    for (size_t i = 0; i < 3u; ++i) {
        out.boundsMin[i] = std::min(out.boundsMin[i], sourceMin[i]);
        out.boundsMax[i] = std::max(out.boundsMax[i], sourceMax[i]);
    }
}

void appendGpuBatch(mw::legacy3d::GpuBatch& out, const mw::legacy3d::GpuBatch& source) {
    const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size() / 6u);
    out.vertices.insert(out.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (uint32_t index : source.triangleIndices) {
        out.triangleIndices.push_back(baseVertex + index);
    }
    for (uint32_t index : source.lineIndices) {
        out.lineIndices.push_back(baseVertex + index);
    }
    const int baseGroup = out.groupMatrices.empty() ? 0 : (out.groupMatrices.rbegin()->first + 1);
    for (const auto& item : source.groupMatrices) {
        out.groupMatrices.emplace(baseGroup + item.first, item.second);
    }
}

const mw::legacy3d::RuntimeRecord& selectRecord(
    const std::vector<mw::legacy3d::RuntimeRecord>& records,
    uint16_t recordIndex) {
    const auto it = std::find_if(
        records.begin(),
        records.end(),
        [recordIndex](const mw::legacy3d::RuntimeRecord& record) {
            return record.recordIndex == static_cast<int>(recordIndex);
        });
    if (it == records.end()) {
        throw mw::legacy3d::ParseError("TERPCK record not found: " + std::to_string(recordIndex));
    }
    return *it;
}

std::array<float, 3> toFloatRgb(Rgb rgb) {
    return {
        static_cast<float>(rgb.r) / 255.0f,
        static_cast<float>(rgb.g) / 255.0f,
        static_cast<float>(rgb.b) / 255.0f,
    };
}

std::array<float, 3> transformTerrainShapeVertex(
    mw::legacy3d::Vec3i v,
    float scale,
    const std::string& swizzle) {
    const float x = static_cast<float>(v.x) * scale;
    const float y = static_cast<float>(v.y) * scale;
    const float z = static_cast<float>(v.z) * scale;
    if (swizzle == "xyz") {
        return {x, y, z};
    }
    if (swizzle == "xzy") {
        return {x, z, y};
    }
    if (swizzle == "yxz") {
        return {y, x, z};
    }
    if (swizzle == "yzx") {
        return {y, z, x};
    }
    if (swizzle == "zxy") {
        return {z, x, y};
    }
    if (swizzle == "zyx") {
        return {z, y, x};
    }
    throw mw::legacy3d::ParseError("unsupported vertex swizzle: " + swizzle);
}

std::vector<int> terrainShapeValidIndices(
    const mw::legacy3d::RuntimeRecord& record,
    const mw::legacy3d::RuntimePrimitive& prim,
    const std::string& indexMode) {
    std::vector<int> valid;
    valid.reserve(prim.normalizedIndices.size());
    for (uint8_t rawIndex : prim.normalizedIndices) {
        const int idx = indexMode == "minus1" ? static_cast<int>(rawIndex) - 1 : static_cast<int>(rawIndex);
        if (idx >= 0 && static_cast<size_t>(idx) < record.vertices.size()) {
            valid.push_back(idx);
        }
    }
    return valid;
}

mw::legacy3d::GpuBatch buildTerrainPaletteGpuBatch(
    const mw::legacy3d::RuntimeRecord& record,
    const mw::legacy3d::GpuBatchOptions& options,
    const std::string& terrainColorMode,
    bool overlayColors) {
    mw::legacy3d::GpuBatch batch;
    std::vector<std::array<float, 3>> transformed;
    transformed.reserve(record.vertices.size());
    for (mw::legacy3d::Vec3i v : record.vertices) {
        transformed.push_back(transformTerrainShapeVertex(v, options.scale, options.swizzle));
    }

    auto appendVertex = [&batch](const std::array<float, 3>& p, const std::array<float, 3>& rgb) {
        batch.vertices.push_back(p[0]);
        batch.vertices.push_back(p[1]);
        batch.vertices.push_back(p[2]);
        batch.vertices.push_back(rgb[0]);
        batch.vertices.push_back(rgb[1]);
        batch.vertices.push_back(rgb[2]);
    };

    for (const mw::legacy3d::RuntimePart& part : record.parts) {
        for (const mw::legacy3d::RuntimeCommand& cmd : part.commands) {
            for (const mw::legacy3d::RuntimePrimitive& prim : cmd.primitives) {
                const std::vector<int> valid = terrainShapeValidIndices(record, prim, options.indexMode);
                if (prim.kind.rfind("polygon", 0) != 0 || valid.size() < 3u) {
                    continue;
                }

                const std::array<float, 3> rgb = toFloatRgb(
                    overlayColors ? terrainShadeOverlayRgb(prim.shadeBytes[2], terrainColorMode)
                                  : terrainShadeBaseRgb(prim.shadeBytes[2], terrainColorMode));
                const uint32_t start = static_cast<uint32_t>(batch.vertices.size() / 6u);
                for (int idx : valid) {
                    appendVertex(transformed[static_cast<size_t>(idx)], rgb);
                }
                for (size_t j = 1u; j + 1u < valid.size(); ++j) {
                    batch.triangleIndices.push_back(start);
                    batch.triangleIndices.push_back(start + static_cast<uint32_t>(j));
                    batch.triangleIndices.push_back(start + static_cast<uint32_t>(j + 1u));
                }
            }
        }
    }

    return batch;
}

struct TerrainObjectBatches {
    mw::legacy3d::GpuBatch base;
    mw::legacy3d::GpuBatch dither;
};

void appendPlacedTerrainObject(
    mw::legacy3d::GpuBatch& out,
    const mw::legacy3d::GpuBatch& local,
    mw::legacy3d::TerrainPlacementBounds placementBounds,
    const mw::legacy3d::TerrainObjectPlacement& placement,
    bool rawPlacement,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY) {
    if (local.vertices.empty()) {
        return;
    }
    const BatchBounds localBounds = computeBatchBounds(local);
    const float localCenterX = (localBounds.minX + localBounds.maxX) * 0.5f;
    const float localCenterZ = (localBounds.minZ + localBounds.maxZ) * 0.5f;
    const float placementX = (!rawPlacement && mirrorX) ? (placementBounds.minX + placementBounds.maxX - placement.x) : placement.x;
    const float placementZ = (!rawPlacement && mirrorY) ? (placementBounds.minZ + placementBounds.maxZ - placement.z) : placement.z;
    const float offsetX = placementX - localCenterX;
    const float offsetY = -localBounds.minY;
    const float offsetZ = placementZ - localCenterZ;
    const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size() / 6u);
    for (size_t i = 0; i + 5u < local.vertices.size(); i += 6u) {
        const float vx = local.vertices[i];
        const float vz = mirrorObjectY ? (localCenterZ * 2.0f - local.vertices[i + 2u]) : local.vertices[i + 2u];
        out.vertices.push_back(vx + offsetX);
        out.vertices.push_back(local.vertices[i + 1u] + offsetY);
        out.vertices.push_back(vz + offsetZ);
        out.vertices.push_back(local.vertices[i + 3u]);
        out.vertices.push_back(local.vertices[i + 4u]);
        out.vertices.push_back(local.vertices[i + 5u]);
    }
    for (size_t i = 0; i + 2u < local.triangleIndices.size(); i += 3u) {
        out.triangleIndices.push_back(baseVertex + local.triangleIndices[i]);
        out.triangleIndices.push_back(baseVertex + local.triangleIndices[mirrorObjectY ? (i + 2u) : (i + 1u)]);
        out.triangleIndices.push_back(baseVertex + local.triangleIndices[mirrorObjectY ? (i + 1u) : (i + 2u)]);
    }
}

TerrainObjectBatches buildPlacedTerrainObjectBatches(
    const mw::legacy3d::TerrainWorld& world,
    const std::vector<mw::legacy3d::RuntimeRecord>& terrainRecords,
    mw::legacy3d::TerrainPlacementBounds placementBounds,
    float placementInsetFraction,
    float objectScale,
    float cellSize,
    bool rawPlacement,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY,
    const std::string& colorMode) {
    TerrainObjectBatches out;
    const std::vector<mw::legacy3d::TerrainObjectPlacement> placements = rawPlacement
        ? mw::legacy3d::mapTerrainWorldPlacementsFromRawCoordinates(world, placementBounds, cellSize)
        : mw::legacy3d::normalizeTerrainWorldPlacements(world, placementBounds, placementInsetFraction);
    for (const mw::legacy3d::TerrainObjectPlacement& placement : placements) {
        const mw::legacy3d::RuntimeRecord& record = selectRecord(terrainRecords, placement.recordIndex);
        mw::legacy3d::GpuBatchOptions options;
        options.scale = objectScale;
        options.showEdges = false;
        options.showLines = false;
        const mw::legacy3d::GpuBatch local =
            (colorMode == "debug" || colorMode == "flat")
                ? mw::legacy3d::buildGpuBatch(record, options)
                : buildTerrainPaletteGpuBatch(record, options, colorMode, false);
        appendPlacedTerrainObject(out.base, local, placementBounds, placement, rawPlacement, mirrorX, mirrorY, mirrorObjectY);
        if (colorMode != "debug" && colorMode != "flat") {
            appendPlacedTerrainObject(
                out.dither,
                buildTerrainPaletteGpuBatch(record, options, colorMode, true),
                placementBounds,
                placement,
                rawPlacement,
                mirrorX,
                mirrorY,
                mirrorObjectY);
        }
    }
    return out;
}

mw::legacy3d::GpuBatch buildPlacedTerrainObjectBatch(
    const mw::legacy3d::TerrainWorld& world,
    const std::vector<mw::legacy3d::RuntimeRecord>& terrainRecords,
    mw::legacy3d::TerrainPlacementBounds placementBounds,
    float placementInsetFraction,
    float objectScale,
    float cellSize,
    bool rawPlacement,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY,
    const std::string& colorMode) {
    return buildPlacedTerrainObjectBatches(
               world,
               terrainRecords,
               placementBounds,
               placementInsetFraction,
               objectScale,
               cellSize,
               rawPlacement,
               mirrorX,
               mirrorY,
               mirrorObjectY,
               colorMode)
        .base;
}

LayoutResources buildLayoutResources(
    const std::filesystem::path& root,
    const std::vector<std::string>& tileNames,
    const std::vector<mw::legacy3d::RuntimeRecord>& terrainRecords,
    float cellSize,
    float layoutGap,
    float objectInset,
    float objectScale,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY,
    const std::string& colorMode) {
    (void)layoutGap;
    if (tileNames.size() != 4u) {
        throw mw::legacy3d::ParseError("terrain minimap requires exactly four tile names");
    }
    LayoutResources out;
    std::array<mw::legacy3d::TerrainGrid, 4> layoutGrids;
    for (size_t i = 0; i < tileNames.size(); ++i) {
        layoutGrids[i] = mw::legacy3d::loadTerrainGrid(root / "GRD" / (tileNames[i] + ".GRD"));
    }
    out.mesh = mw::legacy3d::buildTerrainMesh(
        mw::legacy3d::composeTerrainGrid2x2(layoutGrids, "layout:" + joinPaths(tileNames), mirrorX, mirrorY),
        cellSize,
        20.0f);
    const float tileWidth = static_cast<float>(layoutGrids[0].width) * cellSize;
    const float tileDepth = static_cast<float>(layoutGrids[0].height) * cellSize;
    const mw::legacy3d::TerrainPlacementBounds objectPlacementBounds{
        0.0f,
        tileWidth * 2.0f,
        0.0f,
        tileDepth * 2.0f,
    };
    for (size_t i = 0; i < tileNames.size(); ++i) {
        const std::string& name = tileNames[i];
        const mw::legacy3d::TerrainWorld tileWorld =
            mw::legacy3d::loadTerrainWorld(root / "WLD" / (name + ".WLD"));
        const TerrainObjectBatches batches = buildPlacedTerrainObjectBatches(
            tileWorld,
            terrainRecords,
            objectPlacementBounds,
            objectInset,
            objectScale,
            cellSize,
            true,
            mirrorX,
            mirrorY,
            mirrorObjectY,
            colorMode);
        appendGpuBatch(out.objects, batches.base);
        appendGpuBatch(out.objectDither, batches.dither);
        out.wldObjects += tileWorld.objects.size();
    }
    return out;
}

MinimapSize parseMinimapSize(const std::string& text) {
    const size_t xPos = text.find('x');
    const size_t sep = xPos == std::string::npos ? text.find('X') : xPos;
    if (sep == std::string::npos) {
        throw mw::legacy3d::ParseError("terrain minimap size must be WIDTHxHEIGHT");
    }
    MinimapSize size;
    size.width = std::stoi(text.substr(0, sep));
    size.height = std::stoi(text.substr(sep + 1u));
    if (size.width < 16 || size.height < 16 || size.width > 4096 || size.height > 4096) {
        throw mw::legacy3d::ParseError("terrain minimap size out of supported range");
    }
    return size;
}

MinimapRotation parseMinimapRotation(const std::string& text) {
    if (text == "none") {
        return MinimapRotation::None;
    }
    if (text == "cw") {
        return MinimapRotation::Clockwise;
    }
    if (text == "ccw") {
        return MinimapRotation::CounterClockwise;
    }
    throw mw::legacy3d::ParseError("--terrain-minimap-rotation must be none, cw, or ccw");
}

GridProbeCoord parseGridProbeCoord(const std::string& text) {
    const size_t comma = text.find(',');
    if (comma == std::string::npos) {
        throw mw::legacy3d::ParseError("--terrain-grid-probe must be X,Y");
    }
    GridProbeCoord coord;
    coord.x = std::stoi(text.substr(0, comma));
    coord.y = std::stoi(text.substr(comma + 1u));
    return coord;
}

std::string minimapRotationId(MinimapRotation rotation) {
    switch (rotation) {
    case MinimapRotation::None:
        return "none";
    case MinimapRotation::Clockwise:
        return "cw";
    case MinimapRotation::CounterClockwise:
        return "ccw";
    }
    return "none";
}

void writeBmp24(const std::filesystem::path& path, const Raster& raster) {
    const int rowBytes = raster.width * 3;
    const int paddedRowBytes = (rowBytes + 3) & ~3;
    const uint32_t pixelBytes = static_cast<uint32_t>(paddedRowBytes * raster.height);
    const uint32_t fileSize = 14u + 40u + pixelBytes;
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw mw::legacy3d::ParseError("could not write terrain minimap: " + path.string());
    }
    const uint8_t fileHeader[14] = {
        'B', 'M',
        static_cast<uint8_t>(fileSize & 0xffu),
        static_cast<uint8_t>((fileSize >> 8) & 0xffu),
        static_cast<uint8_t>((fileSize >> 16) & 0xffu),
        static_cast<uint8_t>((fileSize >> 24) & 0xffu),
        0, 0, 0, 0,
        54, 0, 0, 0,
    };
    const uint8_t dibHeader[40] = {
        40, 0, 0, 0,
        static_cast<uint8_t>(raster.width & 0xff),
        static_cast<uint8_t>((raster.width >> 8) & 0xff),
        static_cast<uint8_t>((raster.width >> 16) & 0xff),
        static_cast<uint8_t>((raster.width >> 24) & 0xff),
        static_cast<uint8_t>(raster.height & 0xff),
        static_cast<uint8_t>((raster.height >> 8) & 0xff),
        static_cast<uint8_t>((raster.height >> 16) & 0xff),
        static_cast<uint8_t>((raster.height >> 24) & 0xff),
        1, 0,
        24, 0,
        0, 0, 0, 0,
        static_cast<uint8_t>(pixelBytes & 0xffu),
        static_cast<uint8_t>((pixelBytes >> 8) & 0xffu),
        static_cast<uint8_t>((pixelBytes >> 16) & 0xffu),
        static_cast<uint8_t>((pixelBytes >> 24) & 0xffu),
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
        0, 0, 0, 0,
    };
    out.write(reinterpret_cast<const char*>(fileHeader), sizeof(fileHeader));
    out.write(reinterpret_cast<const char*>(dibHeader), sizeof(dibHeader));
    std::vector<uint8_t> row(static_cast<size_t>(paddedRowBytes), 0);
    for (int y = raster.height - 1; y >= 0; --y) {
        std::fill(row.begin(), row.end(), uint8_t{0});
        for (int x = 0; x < raster.width; ++x) {
            const Rgb rgb = raster.pixels[static_cast<size_t>(y) * static_cast<size_t>(raster.width) + static_cast<size_t>(x)];
            const size_t off = static_cast<size_t>(x) * 3u;
            row[off] = rgb.b;
            row[off + 1u] = rgb.g;
            row[off + 2u] = rgb.r;
        }
        out.write(reinterpret_cast<const char*>(row.data()), row.size());
    }
}

float edgeFunction(float ax, float ay, float bx, float by, float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

void drawFilledTriangle(
    Raster& raster,
    float ax,
    float ay,
    float bx,
    float by,
    float cx,
    float cy,
    Rgb rgb) {
    const float minFx = std::floor(std::min({ax, bx, cx}));
    const float maxFx = std::ceil(std::max({ax, bx, cx}));
    const float minFy = std::floor(std::min({ay, by, cy}));
    const float maxFy = std::ceil(std::max({ay, by, cy}));
    const int minX = std::max(0, static_cast<int>(minFx));
    const int maxX = std::min(raster.width - 1, static_cast<int>(maxFx));
    const int minY = std::max(0, static_cast<int>(minFy));
    const int maxY = std::min(raster.height - 1, static_cast<int>(maxFy));
    const float area = edgeFunction(ax, ay, bx, by, cx, cy);
    if (std::fabs(area) < 0.0001f) {
        return;
    }
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float w0 = edgeFunction(bx, by, cx, cy, px, py);
            const float w1 = edgeFunction(cx, cy, ax, ay, px, py);
            const float w2 = edgeFunction(ax, ay, bx, by, px, py);
            if ((w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) || (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f)) {
                raster.set(x, y, rgb);
            }
        }
    }
}

void drawDitheredTriangle(
    Raster& raster,
    float ax,
    float ay,
    float bx,
    float by,
    float cx,
    float cy,
    Rgb rgb) {
    const float minFx = std::floor(std::min({ax, bx, cx}));
    const float maxFx = std::ceil(std::max({ax, bx, cx}));
    const float minFy = std::floor(std::min({ay, by, cy}));
    const float maxFy = std::ceil(std::max({ay, by, cy}));
    const int minX = std::max(0, static_cast<int>(minFx));
    const int maxX = std::min(raster.width - 1, static_cast<int>(maxFx));
    const int minY = std::max(0, static_cast<int>(minFy));
    const int maxY = std::min(raster.height - 1, static_cast<int>(maxFy));
    const float area = edgeFunction(ax, ay, bx, by, cx, cy);
    if (std::fabs(area) < 0.0001f) {
        return;
    }
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if ((((x / 2) + (y / 2)) & 1) == 0) {
                continue;
            }
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float w0 = edgeFunction(bx, by, cx, cy, px, py);
            const float w1 = edgeFunction(cx, cy, ax, ay, px, py);
            const float w2 = edgeFunction(ax, ay, bx, by, px, py);
            if ((w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) || (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f)) {
                raster.set(x, y, rgb);
            }
        }
    }
}

void drawLine(Raster& raster, int x0, int y0, int x1, int y1, Rgb rgb) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (true) {
        raster.set(x0, y0, rgb);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

Rgb batchVertexColor(const mw::legacy3d::GpuBatch& batch, uint32_t index) {
    const size_t off = static_cast<size_t>(index) * 6u;
    const auto toByte = [](float value) -> uint8_t {
        return static_cast<uint8_t>(std::max(0.0f, std::min(1.0f, value)) * 255.0f + 0.5f);
    };
    if (off + 5u >= batch.vertices.size()) {
        return Rgb{230, 230, 230};
    }
    return Rgb{
        toByte(batch.vertices[off + 3u]),
        toByte(batch.vertices[off + 4u]),
        toByte(batch.vertices[off + 5u]),
    };
}

Rgb minimapGroundPatchRgb(uint8_t colorBand) {
    switch (std::min<uint8_t>(colorBand, 3u)) {
    case 0:
        return Rgb{85, 255, 85};
    case 1:
        return Rgb{255, 255, 85};
    case 2:
        return Rgb{255, 85, 85};
    default:
        return Rgb{170, 0, 0};
    }
}

void renderTerrainMinimap(
    const std::filesystem::path& outputPath,
    const std::vector<std::string>& tileNames,
    const LayoutResources& layout,
    MinimapSize size,
    float cellSize,
    float objectScale,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY,
    MinimapRotation rotation,
    const std::string& colorMode,
    std::optional<int> environmentId,
    std::optional<size_t> scenarioIndex) {
    const TerrainPalette& palette = terrainPaletteById(colorMode);
    Raster raster(size.width, size.height, palette.ground);

    BatchBounds bounds;
    bounds.minX = layout.mesh.boundsMin[0];
    bounds.minZ = layout.mesh.boundsMin[2];
    bounds.maxX = layout.mesh.boundsMax[0];
    bounds.maxZ = layout.mesh.boundsMax[2];
    if (!layout.objects.vertices.empty()) {
        const BatchBounds objectBounds = computeBatchBounds(layout.objects);
        bounds.minX = std::min(bounds.minX, objectBounds.minX);
        bounds.minZ = std::min(bounds.minZ, objectBounds.minZ);
        bounds.maxX = std::max(bounds.maxX, objectBounds.maxX);
        bounds.maxZ = std::max(bounds.maxZ, objectBounds.maxZ);
    }

    const float width = std::max(1.0f, bounds.maxX - bounds.minX);
    const float depth = std::max(1.0f, bounds.maxZ - bounds.minZ);
    const bool rotate = rotation != MinimapRotation::None;
    const float projectedWidth = rotate ? depth : width;
    const float projectedDepth = rotate ? width : depth;
    const float margin = 6.0f;
    const float scale = std::min(
        (static_cast<float>(size.width) - margin * 2.0f) / projectedWidth,
        (static_cast<float>(size.height) - margin * 2.0f) / projectedDepth);
    const float drawWidth = projectedWidth * scale;
    const float drawDepth = projectedDepth * scale;
    const float originX = (static_cast<float>(size.width) - drawWidth) * 0.5f;
    const float originY = (static_cast<float>(size.height) - drawDepth) * 0.5f;
    auto mapPoint = [&](float x, float z) -> std::array<float, 2> {
        float projectedX = x - bounds.minX;
        float projectedY = z - bounds.minZ;
        if (rotation == MinimapRotation::Clockwise) {
            projectedX = z - bounds.minZ;
            projectedY = bounds.maxX - x;
        } else if (rotation == MinimapRotation::CounterClockwise) {
            projectedX = bounds.maxZ - z;
            projectedY = x - bounds.minX;
        }
        return {
            originX + projectedX * scale,
            originY + projectedY * scale,
        };
    };

    if (colorMode != "debug") {
        const int cellPixels = std::max(1, static_cast<int>(std::ceil(cellSize * scale)));
        for (const mw::legacy3d::TerrainMeshVertex& vertex : layout.mesh.vertices) {
            if (vertex.rawValue == 0) {
                continue;
            }
            const std::array<float, 2> mapped = mapPoint(vertex.x, vertex.z);
            const int x0 = static_cast<int>(std::floor(mapped[0]));
            const int y0 = static_cast<int>(std::floor(mapped[1]));
            const Rgb rgb = minimapGroundPatchRgb(vertex.colorBand);
            for (int y = 0; y < cellPixels; ++y) {
                for (int x = 0; x < cellPixels; ++x) {
                    raster.set(x0 + x, y0 + y, rgb);
                }
            }
        }
    }

    const float tileWidth = (layout.mesh.boundsMax[0] - layout.mesh.boundsMin[0]) * 0.5f;
    const float tileDepth = (layout.mesh.boundsMax[2] - layout.mesh.boundsMin[2]) * 0.5f;
    auto drawProjectedLine = [&](float x0, float z0, float x1, float z1, Rgb color) {
        const std::array<float, 2> a = mapPoint(x0, z0);
        const std::array<float, 2> b = mapPoint(x1, z1);
        drawLine(
            raster,
            static_cast<int>(std::round(a[0])),
            static_cast<int>(std::round(a[1])),
            static_cast<int>(std::round(b[0])),
            static_cast<int>(std::round(b[1])),
            color);
    };
    drawProjectedLine(
        layout.mesh.boundsMin[0] + tileWidth,
        layout.mesh.boundsMin[2],
        layout.mesh.boundsMin[0] + tileWidth,
        layout.mesh.boundsMax[2],
        palette.grid);
    drawProjectedLine(
        layout.mesh.boundsMin[0],
        layout.mesh.boundsMin[2] + tileDepth,
        layout.mesh.boundsMax[0],
        layout.mesh.boundsMin[2] + tileDepth,
        palette.grid);

    auto drawObjectTriangles = [&](const mw::legacy3d::GpuBatch& batch, bool dithered, bool drawEdges) {
        for (size_t i = 0; i + 2u < batch.triangleIndices.size(); i += 3u) {
            const uint32_t ia = batch.triangleIndices[i];
            const uint32_t ib = batch.triangleIndices[i + 1u];
            const uint32_t ic = batch.triangleIndices[i + 2u];
            const size_t a = static_cast<size_t>(ia) * 6u;
            const size_t b = static_cast<size_t>(ib) * 6u;
            const size_t c = static_cast<size_t>(ic) * 6u;
            if (a + 5u >= batch.vertices.size() ||
                b + 5u >= batch.vertices.size() ||
                c + 5u >= batch.vertices.size()) {
                continue;
            }
            Rgb rgb = batchVertexColor(batch, ia);
            if (colorMode == "flat") {
                rgb = paletteObjectColor(rgb, palette);
            } else if (colorMode == "debug" && rgb.r > 180 && rgb.g > 180 && rgb.b > 180) {
                rgb = palette.objectLight;
            }
            const std::array<float, 2> mappedA = mapPoint(batch.vertices[a], batch.vertices[a + 2u]);
            const std::array<float, 2> mappedB = mapPoint(batch.vertices[b], batch.vertices[b + 2u]);
            const std::array<float, 2> mappedC = mapPoint(batch.vertices[c], batch.vertices[c + 2u]);
            if (dithered) {
                drawDitheredTriangle(
                    raster,
                    mappedA[0],
                    mappedA[1],
                    mappedB[0],
                    mappedB[1],
                    mappedC[0],
                    mappedC[1],
                    rgb);
            } else {
                drawFilledTriangle(
                    raster,
                    mappedA[0],
                    mappedA[1],
                    mappedB[0],
                    mappedB[1],
                    mappedC[0],
                    mappedC[1],
                    rgb);
            }
            if (!drawEdges) {
                continue;
            }
            drawLine(
                raster,
                static_cast<int>(std::round(mappedA[0])),
                static_cast<int>(std::round(mappedA[1])),
                static_cast<int>(std::round(mappedB[0])),
                static_cast<int>(std::round(mappedB[1])),
                palette.edge);
            drawLine(
                raster,
                static_cast<int>(std::round(mappedB[0])),
                static_cast<int>(std::round(mappedB[1])),
                static_cast<int>(std::round(mappedC[0])),
                static_cast<int>(std::round(mappedC[1])),
                palette.edge);
            drawLine(
                raster,
                static_cast<int>(std::round(mappedC[0])),
                static_cast<int>(std::round(mappedC[1])),
                static_cast<int>(std::round(mappedA[0])),
                static_cast<int>(std::round(mappedA[1])),
                palette.edge);
        }
    };
    if (colorMode == "debug") {
        for (const mw::legacy3d::TerrainMeshVertex& vertex : layout.mesh.vertices) {
            if (vertex.rawValue == 0) {
                continue;
            }
            const std::array<float, 2> mapped = mapPoint(vertex.x, vertex.z);
            const int x = static_cast<int>(std::round(mapped[0]));
            const int y = static_cast<int>(std::round(mapped[1]));
            raster.set(x, y, palette.marker);
        }
        drawObjectTriangles(layout.objects, false, false);
        drawObjectTriangles(layout.objectDither, true, false);
        drawObjectTriangles(layout.objects, false, true);
    }

    writeBmp24(outputPath, raster);
    std::cout
        << "terrain_minimap"
        << "\tpath=" << outputPath.string()
        << "\tsize=" << size.width << "x" << size.height
        << "\ttiles=" << joinPaths(tileNames)
        << "\tobjects=" << layout.wldObjects
        << "\ttriangles=" << (layout.objects.triangleIndices.size() / 3u)
        << "\tobject_scale=" << objectScale
        << "\trotation=" << minimapRotationId(rotation)
        << "\twld_placement=raw"
        << "\tcolor=" << colorMode
        << "\tenvironment_id=" << (environmentId.has_value() ? std::to_string(*environmentId) : std::string("none"))
        << "\ttile_mirror_x=" << (mirrorX ? "yes" : "no")
        << "\ttile_mirror_y=" << (mirrorY ? "yes" : "no")
        << "\tobject_mirror_y=" << (mirrorObjectY ? "yes" : "no");
    if (scenarioIndex.has_value()) {
        std::cout << "\tscenario=" << *scenarioIndex;
    }
    std::cout << "\n";
}

WorldStats computeWorldStats(const mw::legacy3d::TerrainWorld& world) {
    WorldStats stats;
    if (world.objects.empty()) {
        return stats;
    }
    stats.minX = stats.maxX = world.objects.front().x;
    stats.minZ = stats.maxZ = world.objects.front().z;
    stats.minY = stats.maxY = world.objects.front().y;
    for (const mw::legacy3d::TerrainWorldObject& object : world.objects) {
        stats.recordIds.insert(object.recordIndex);
        stats.unknownAValues.insert(object.unknownA);
        stats.unknownBValues.insert(object.unknownB);
        stats.minX = std::min(stats.minX, object.x);
        stats.maxX = std::max(stats.maxX, object.x);
        stats.minZ = std::min(stats.minZ, object.z);
        stats.maxZ = std::max(stats.maxZ, object.z);
        stats.minY = std::min(stats.minY, object.y);
        stats.maxY = std::max(stats.maxY, object.y);
    }
    return stats;
}

void dumpGrid(const std::filesystem::path& path, bool meshStats) {
    const mw::legacy3d::TerrainGrid grid = mw::legacy3d::loadTerrainGrid(path);
    const GridStats stats = computeGridStats(grid);

    std::cout
        << "terrain_grid"
        << "\tpath=" << path.string()
        << "\tresource=" << grid.resourceName
        << "\twidth=" << grid.width
        << "\theight=" << grid.height
        << "\tsamples=" << grid.samples.size()
        << "\tunique_samples=" << stats.uniqueSamples
        << "\tnonzero_samples=" << stats.nonzeroSamples
        << "\tblocking_samples=" << stats.blockingSamples
        << "\traw_values=" << stats.rawValues.size()
        << "\tdetail_values=" << stats.detailValues.size()
        << "\n";

    std::cout
        << "terrain_grid_cells"
        << "\tresource=" << grid.resourceName
        << "\tfile_storage=87x47x1"
        << "\tfile_stride=47"
        << "\tcolor_band_samples=" << stats.colorBandSamples[0] << "," << stats.colorBandSamples[1]
        << "," << stats.colorBandSamples[2] << "," << stats.colorBandSamples[3]
        << "\traw_top=" << formatFrequentBytes(stats.rawFrequencies)
        << "\tdetail_top=" << formatFrequentBytes(stats.detailFrequencies)
        << "\tsemantics=blocking_and_palette_band_observed"
        << "\toriginal_xrefs=load_87x47,occupancy_5x5,render_band_raw_shift5"
        << "\n";

    if (meshStats) {
        const mw::legacy3d::TerrainMesh mesh = mw::legacy3d::buildTerrainMesh(grid);
        std::cout
            << "terrain_mesh"
            << "\tvertices=" << mesh.vertices.size()
            << "\tindices=" << mesh.indices.size()
            << "\ttriangles=" << (mesh.indices.size() / 3u)
            << "\tground=flat"
            << "\tbounds_min=" << mesh.boundsMin[0] << "," << mesh.boundsMin[1] << "," << mesh.boundsMin[2]
            << "\tbounds_max=" << mesh.boundsMax[0] << "," << mesh.boundsMax[1] << "," << mesh.boundsMax[2]
            << "\theight_origin=" << mesh.sampleHeightNearest(0.0f, 0.0f)
            << "\n";
    }
}

void dumpWorld(const std::filesystem::path& path) {
    const mw::legacy3d::TerrainWorld world = mw::legacy3d::loadTerrainWorld(path);
    const WorldStats stats = computeWorldStats(world);
    std::cout
        << "terrain_world"
        << "\tpath=" << path.string()
        << "\tresource=" << world.resourceName
        << "\tobjects=" << world.objects.size();
    if (!world.objects.empty()) {
        const mw::legacy3d::TerrainWorldObject& first = world.objects.front();
        std::cout
            << "\tfirst_record=" << first.recordIndex
            << "\tfirst_x=" << first.x
            << "\tfirst_z=" << first.z
            << "\tfirst_y=" << first.y
            << "\tx_range=" << stats.minX << "," << stats.maxX
            << "\tz_range=" << stats.minZ << "," << stats.maxZ
            << "\ty_range=" << stats.minY << "," << stats.maxY
            << "\trecord_ids=" << joinIds(stats.recordIds)
            << "\tunknown_a=" << joinSignedIds(stats.unknownAValues)
            << "\tunknown_b=" << joinSignedIds(stats.unknownBValues);
    }
    std::cout << "\n";
}

void dumpTerpckGi(const std::filesystem::path& path) {
    const mw::legacy3d::TerpckGiResource resource = mw::legacy3d::loadTerpckGiResource(path);
    size_t activeCollisionRecords = 0;
    size_t collisionSubrecords = 0;
    size_t collisionEdges = 0;
    size_t maximumCollisionEdges = 0;
    size_t collisionOriginSubrecords = 0;
    std::map<std::pair<unsigned, unsigned>, std::vector<size_t>>
        collisionRowsByPriorityAndResult;
    for (const mw::legacy3d::TerpckGiCollisionRecord& record : resource.collisionRecords) {
        collisionRowsByPriorityAndResult[
            {static_cast<unsigned>(record.priorityByte5),
             static_cast<unsigned>(record.resultByte6)}]
            .push_back(record.recordIndex);
        if (!record.subrecords.empty()) {
            ++activeCollisionRecords;
        }
        collisionSubrecords += record.subrecords.size();
        for (const mw::legacy3d::TerpckGiCollisionSubrecord& subrecord : record.subrecords) {
            collisionEdges += subrecord.edges.size();
            maximumCollisionEdges = std::max(maximumCollisionEdges, subrecord.edges.size());
            if (mw::legacy3d::terpckGiSubrecordContainsPoint(subrecord, 0, 0)) {
                ++collisionOriginSubrecords;
            }
        }
    }
    std::cout
        << "terpck_gi"
        << "\tpath=" << path.string()
        << "\tpacked_size=" << resource.packedSize
        << "\tdecoded_size=" << resource.decodedSize
        << "\tcollision_records=" << resource.collisionRecords.size()
        << "\tcollision_active=" << activeCollisionRecords
        << "\tcollision_subrecords=" << collisionSubrecords
        << "\tcollision_edges=" << collisionEdges
        << "\tcollision_max_edges=" << maximumCollisionEdges
        << "\tcollision_origin_subrecords=" << collisionOriginSubrecords
        << "\tindex_entries=" << resource.index.size()
        << "\tquad_count=" << resource.quads.size();
    std::cout << "\tcollision_priority_result_rows=";
    bool firstGroup = true;
    for (const auto& [priorityResult, rows] :
         collisionRowsByPriorityAndResult) {
        if (!firstGroup) {
            std::cout << ";";
        }
        firstGroup = false;
        std::cout << priorityResult.first << ":" << priorityResult.second << "=";
        for (size_t index = 0; index < rows.size(); ++index) {
            if (index != 0u) {
                std::cout << ",";
            }
            std::cout << rows[index];
        }
    }
    if (!resource.collisionRecords.empty()) {
        const mw::legacy3d::TerpckGiCollisionRecord& first = resource.collisionRecords.front();
        std::cout
            << "\tfirst_collision="
            << first.extentX << ","
            << first.extentZ << ","
            << static_cast<unsigned>(first.modeByte4) << ","
            << static_cast<unsigned>(first.priorityByte5) << ","
            << static_cast<unsigned>(first.resultByte6) << ","
            << static_cast<unsigned>(first.subrecordCountByte7);
    }
    if (!resource.index.empty()) {
        std::cout
            << "\tfirst_attr=" << resource.index.front().attrOrColor
            << "\tfirst_abs_offset=" << resource.index.front().absOffset
            << "\tlast_slice_length=" << resource.index.back().length;
    }
    std::cout << "\n";
}

void dumpTerpckTbl(const std::filesystem::path& path, bool catalog) {
    const std::vector<mw::legacy3d::RuntimeRecord> records = mw::legacy3d::loadRuntimeShapeRecords(path);
    size_t vertices = 0;
    int polygons = 0;
    int lines = 0;
    for (const mw::legacy3d::RuntimeRecord& record : records) {
        const mw::legacy3d::RecordStats stats = mw::legacy3d::recordStats(record);
        vertices += record.vertices.size();
        polygons += stats.polygonCount;
        lines += stats.lineCount;
    }
    std::cout
        << "terpck_tbl"
        << "\tpath=" << path.string()
        << "\trecords=" << records.size()
        << "\tsource_vertices=" << vertices
        << "\tpolygons=" << polygons
        << "\tlines=" << lines
        << "\n";
    if (!catalog) {
        return;
    }

    for (const mw::legacy3d::RuntimeRecord& record : records) {
        const TerpckRecordInfo info = buildTerpckRecordInfo(record);
        const mw::legacy3d::GpuBatch batch = mw::legacy3d::buildGpuBatch(record);
        const BatchBounds bounds = computeBatchBounds(batch);
        std::cout
            << "terpck_record"
            << "\trecord=" << record.recordIndex
            << "\tsource_vertices=" << info.sourceVertices
            << "\tpolygons=" << info.polygons
            << "\tlines=" << info.lines
            << "\tgpu_vertices=" << info.gpuVertices
            << "\ttriangles=" << info.triangles
            << "\tline_pairs=" << info.linePairs
            << "\tbounds_min=" << formatFloat(bounds.minX) << "," << formatFloat(bounds.minY) << "," << formatFloat(bounds.minZ)
            << "\tbounds_max=" << formatFloat(bounds.maxX) << "," << formatFloat(bounds.maxY) << "," << formatFloat(bounds.maxZ)
            << "\twidth=" << formatFloat(info.width)
            << "\theight=" << formatFloat(info.height)
            << "\tdepth=" << formatFloat(info.depth)
            << "\tprofile=" << info.profile
            << "\n";
    }
}

void collectFilesByStem(
    const std::filesystem::path& dir,
    const std::string& extension,
    std::map<std::string, std::filesystem::path>& out) {
    if (!std::filesystem::is_directory(dir)) {
        throw mw::legacy3d::ParseError("terrain scan directory does not exist: " + dir.string());
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        if (entry.path().extension() == extension) {
            out[entry.path().stem().string()] = entry.path();
        }
    }
}

void dumpTerrainScan(const std::filesystem::path& root, const std::filesystem::path& terpckTblPath) {
    const std::filesystem::path gridDir = root / "GRD";
    const std::filesystem::path worldDir = root / "WLD";
    std::map<std::string, std::filesystem::path> grids;
    std::map<std::string, std::filesystem::path> worlds;
    collectFilesByStem(gridDir, ".GRD", grids);
    collectFilesByStem(worldDir, ".WLD", worlds);

    std::set<std::string> keys;
    for (const auto& item : grids) {
        keys.insert(item.first);
    }
    for (const auto& item : worlds) {
        keys.insert(item.first);
    }

    size_t pairs = 0;
    size_t totalObjects = 0;
    size_t totalNonzeroSamples = 0;
    std::set<uint16_t> allRecordIds;
    std::map<uint16_t, size_t> recordUseCounts;
    std::map<uint16_t, std::set<std::string>> recordTiles;
    std::vector<std::string> missingGrids;
    std::vector<std::string> missingWorlds;

    std::cout
        << "terrain_scan"
        << "\troot=" << root.string()
        << "\tgrids=" << grids.size()
        << "\tworlds=" << worlds.size()
        << "\n";

    for (const std::string& key : keys) {
        const auto gridIt = grids.find(key);
        const auto worldIt = worlds.find(key);
        if (gridIt == grids.end()) {
            missingGrids.push_back(key);
            continue;
        }
        if (worldIt == worlds.end()) {
            missingWorlds.push_back(key);
            continue;
        }

        const mw::legacy3d::TerrainGrid grid = mw::legacy3d::loadTerrainGrid(gridIt->second);
        const mw::legacy3d::TerrainWorld world = mw::legacy3d::loadTerrainWorld(worldIt->second);
        const GridStats gridStats = computeGridStats(grid);
        const WorldStats worldStats = computeWorldStats(world);
        ++pairs;
        totalObjects += world.objects.size();
        totalNonzeroSamples += gridStats.nonzeroSamples;
        allRecordIds.insert(worldStats.recordIds.begin(), worldStats.recordIds.end());
        for (const mw::legacy3d::TerrainWorldObject& object : world.objects) {
            ++recordUseCounts[object.recordIndex];
            recordTiles[object.recordIndex].insert(key);
        }

        std::cout
            << "terrain_pair"
            << "\tresource=" << key
            << "\tgrid_samples=" << grid.samples.size()
            << "\tgrid_unique=" << gridStats.uniqueSamples
            << "\tgrid_nonzero=" << gridStats.nonzeroSamples
            << "\tgrid_blocking=" << gridStats.blockingSamples
            << "\tgrid_raw_values=" << gridStats.rawValues.size()
            << "\tgrid_detail_values=" << gridStats.detailValues.size()
            << "\twld_objects=" << world.objects.size();
        if (!world.objects.empty()) {
            std::cout
                << "\tx_range=" << worldStats.minX << "," << worldStats.maxX
                << "\tz_range=" << worldStats.minZ << "," << worldStats.maxZ
                << "\ty_range=" << worldStats.minY << "," << worldStats.maxY
                << "\trecord_ids=" << joinIds(worldStats.recordIds)
                << "\tunknown_a=" << joinSignedIds(worldStats.unknownAValues)
                << "\tunknown_b=" << joinSignedIds(worldStats.unknownBValues);
        }
        std::cout << "\n";
    }

    std::cout
        << "terrain_scan_summary"
        << "\tpairs=" << pairs
        << "\ttotal_objects=" << totalObjects
        << "\ttotal_nonzero_samples=" << totalNonzeroSamples
        << "\tunique_record_ids=" << allRecordIds.size()
        << "\trecord_ids=" << joinIds(allRecordIds);
    if (!missingGrids.empty()) {
        std::cout << "\tmissing_grids=" << joinPaths(missingGrids);
    }
    if (!missingWorlds.empty()) {
        std::cout << "\tmissing_worlds=" << joinPaths(missingWorlds);
    }
    std::cout << "\n";

    if (!terpckTblPath.empty()) {
        const std::vector<mw::legacy3d::RuntimeRecord> records =
            mw::legacy3d::loadRuntimeShapeRecords(terpckTblPath);
        const std::map<uint16_t, TerpckRecordInfo> infos = buildTerpckRecordInfoMap(records);
        for (uint16_t recordId : allRecordIds) {
            std::cout
                << "terrain_record_usage"
                << "\trecord=" << recordId
                << "\tuses=" << recordUseCounts[recordId]
                << "\ttiles=" << joinNames(recordTiles[recordId]);
            const auto infoIt = infos.find(recordId);
            if (infoIt != infos.end()) {
                const TerpckRecordInfo& info = infoIt->second;
                std::cout
                    << "\tprofile=" << info.profile
                    << "\twidth=" << formatFloat(info.width)
                    << "\theight=" << formatFloat(info.height)
                    << "\tdepth=" << formatFloat(info.depth)
                    << "\tpolygons=" << info.polygons;
            } else {
                std::cout << "\trecord_missing=yes";
            }
            std::cout << "\n";
        }
    }
}

void dumpTerrainLayouts() {
    const std::vector<mw::legacy3d::TerrainArenaLayout>& layouts = mw::legacy3d::terrainArenaLayouts();
    std::cout << "terrain_layouts\tcount=" << layouts.size() << "\n";
    for (const mw::legacy3d::TerrainArenaLayout& layout : layouts) {
        std::cout
            << "terrain_layout"
            << "\tid=" << layout.id
            << "\ttiles=" << joinPaths(layout.tileNames)
            << "\tgap=" << layout.layoutGap
            << "\tinset=" << layout.objectInset
            << "\tdescription=" << layout.description
            << "\n";
    }
}

void dumpTerrainScenario(
    const std::filesystem::path& path,
    std::optional<size_t> scenarioIndex) {
    const std::vector<mw::legacy3d::TerrainScenarioRecord> records =
        mw::legacy3d::loadTerrainScenarioRecords(path);
    size_t validTileRecords = 0;
    size_t terrainCandidates = 0;
    size_t bVariantCandidates = 0;
    std::map<uint8_t, size_t> terrainModeCounts;
    std::map<std::string, std::set<std::string>> tileRefs;
    std::map<uint8_t, std::set<std::string>> baseTileRefs;
    std::array<std::map<uint8_t, size_t>, 12> allTailByteFrequencies;
    std::array<std::map<uint8_t, size_t>, 12> activeTailByteFrequencies;
    for (const mw::legacy3d::TerrainScenarioRecord& record : records) {
        for (size_t byte = 4; byte < record.raw.size(); ++byte) {
            ++allTailByteFrequencies[byte - 4][record.raw[byte]];
        }
        if (record.hasValidTileIds()) {
            ++validTileRecords;
        }
        if (record.isTerrainLayoutCandidate()) {
            ++terrainCandidates;
            for (size_t byte = 4; byte < record.raw.size(); ++byte) {
                ++activeTailByteFrequencies[byte - 4][record.raw[byte]];
            }
            ++terrainModeCounts[record.terrainMode];
            if (record.usesBVariant()) {
                ++bVariantCandidates;
            }
            const std::vector<std::string> tileNames = mw::legacy3d::terrainScenarioTileNames(record);
            for (size_t slot = 0; slot < tileNames.size(); ++slot) {
                std::ostringstream ref;
                ref << record.index << ":" << slot;
                tileRefs[tileNames[slot]].insert(ref.str());
                baseTileRefs[record.tileIds[slot]].insert(ref.str());
            }
        }
    }

    std::ostringstream modeSummary;
    bool firstMode = true;
    for (const auto& item : terrainModeCounts) {
        if (!firstMode) {
            modeSummary << ",";
        }
        modeSummary << static_cast<int>(item.first) << ":" << item.second;
        firstMode = false;
    }

    std::cout
        << "terrain_scenario_file"
        << "\tpath=" << path.string()
        << "\trecords=" << records.size()
        << "\trecord_size=16"
        << "\tvalid_tile_records=" << validTileRecords
        << "\tterrain_candidates=" << terrainCandidates
        << "\tb_variant_candidates=" << bVariantCandidates
        << "\tterrain_modes=" << modeSummary.str()
        << "\n";

    auto dumpRecord = [](const mw::legacy3d::TerrainScenarioRecord& record) {
        const std::vector<std::string> tileNames = mw::legacy3d::terrainScenarioTileNames(record);
        std::cout
            << "terrain_scenario"
            << "\tindex=" << record.index
            << "\ttile_ids=" << static_cast<int>(record.tileIds[0])
            << "," << static_cast<int>(record.tileIds[1])
            << "," << static_cast<int>(record.tileIds[2])
            << "," << static_cast<int>(record.tileIds[3])
            << "\ttiles=" << joinPaths(tileNames)
            << "\tterrain_mode=" << static_cast<int>(record.terrainMode)
            << "\tterrain_enabled=" << (record.terrainEnabled() ? "yes" : "no")
            << "\tb_variant=" << (record.usesBVariant() ? "yes" : "no")
            << "\traw=" << formatByteList(record.raw)
            << "\n";
    };

    if (scenarioIndex.has_value()) {
        const std::optional<mw::legacy3d::TerrainScenarioRecord> record =
            mw::legacy3d::terrainScenarioRecordByIndex(records, *scenarioIndex);
        if (!record.has_value()) {
            throw mw::legacy3d::ParseError("SNARIO scenario index out of range");
        }
        dumpRecord(*record);
        return;
    }

    for (const mw::legacy3d::TerrainScenarioRecord& record : records) {
        if (record.isTerrainLayoutCandidate()) {
            dumpRecord(record);
        }
    }

    for (const auto& item : tileRefs) {
        std::cout
            << "terrain_scenario_tile_usage"
            << "\ttile=" << item.first
            << "\tuses=" << item.second.size()
            << "\trefs=" << joinRefs(item.second)
            << "\n";
    }
    for (const auto& item : baseTileRefs) {
        std::cout
            << "terrain_scenario_base_tile_usage"
            << "\ttile_id=" << static_cast<int>(item.first)
            << "\tuses=" << item.second.size()
            << "\trefs=" << joinRefs(item.second)
            << "\n";
    }
    for (size_t byte = 4; byte < 16; ++byte) {
        std::cout
            << "terrain_scenario_tail_signal"
            << "\tbyte=" << byte
            << "\tall_top=" << formatFrequentBytes(allTailByteFrequencies[byte - 4])
            << "\tactive_top=" << formatFrequentBytes(activeTailByteFrequencies[byte - 4])
            << "\tmeaning=unresolved"
            << "\n";
    }
}

void writeTerrainScenarioReport(
    const std::filesystem::path& outputDirectory,
    const std::filesystem::path& snarioPath,
    const std::filesystem::path& terrainRoot,
    const std::filesystem::path& terpckTblPath,
    MinimapSize minimapSize,
    float cellSize,
    float layoutGap,
    float objectInset,
    float objectScale,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY,
    MinimapRotation minimapRotation,
    const std::string& colorMode,
    std::optional<int> environmentId) {
    const std::vector<mw::legacy3d::TerrainScenarioRecord> scenarios =
        mw::legacy3d::loadTerrainScenarioRecords(snarioPath);
    const std::vector<mw::legacy3d::RuntimeRecord> terrainRecords =
        mw::legacy3d::loadRuntimeShapeRecords(terpckTblPath);
    std::filesystem::create_directories(outputDirectory);
    const std::filesystem::path reportPath = outputDirectory / "terrain_scenario_report.tsv";
    std::ofstream report(reportPath, std::ios::binary | std::ios::trunc);
    if (!report) {
        throw mw::legacy3d::ParseError("could not create terrain scenario report: " + reportPath.string());
    }
    report << "scenario\ttiles\tmode\traw_tail_4_15\tobjects\ttriangles\tcell_size\tobject_scale\tcolor_mode\tenvironment_id\twld_placement\tminimap_rotation\tminimap\tminimap_fnv1a64\tgrd_fnv1a64\twld_fnv1a64\n";

    const std::filesystem::path snarioSignalPath = outputDirectory / "terrain_snario_signal.tsv";
    std::ofstream snarioSignal(snarioSignalPath, std::ios::binary | std::ios::trunc);
    if (!snarioSignal) {
        throw mw::legacy3d::ParseError("could not create terrain SNARIO signal report: " + snarioSignalPath.string());
    }
    snarioSignal
        << "row_type\tscenario\tbyte\ttiles\tterrain_mode\tb_variant\traw_0_15\tactive_values\tactive_top\tall_top\tmeaning\n";

    const std::filesystem::path grdSignalPath = outputDirectory / "terrain_grd_signal.tsv";
    std::ofstream grdSignal(grdSignalPath, std::ios::binary | std::ios::trunc);
    if (!grdSignal) {
        throw mw::legacy3d::ParseError("could not create terrain GRD signal report: " + grdSignalPath.string());
    }
    grdSignal
        << "scenario\ttile\tgrid_size\tfnv1a64\tsamples\tnonzero\tblocking\tunique_raw\tmax_raw\t"
        << "color_band_clamped_counts\traw_shift5_counts\tdetail_top8\traw_top8\n";

    const std::filesystem::path grdPatchSignalPath = outputDirectory / "terrain_grd_patch_signal.tsv";
    std::ofstream grdPatchSignal(grdPatchSignalPath, std::ios::binary | std::ios::trunc);
    if (!grdPatchSignal) {
        throw mw::legacy3d::ParseError("could not create terrain GRD patch signal report: " + grdPatchSignalPath.string());
    }
    grdPatchSignal
        << "scenario\tpatch_id\tcells\tbbox_min\tbbox_max\tbbox_size\tcentroid\t"
        << "tile_slots\traw_min\traw_max\tcolor_band_clamped_counts\tdetail_min\tdetail_max\t"
        << "detail_top8\traw_top8\tneighbor_mode\tmeaning\n";

    size_t written = 0;
    size_t grdRows = 0;
    size_t grdPatchRows = 0;
    std::array<std::map<uint8_t, size_t>, 12> allTailByteFrequencies;
    std::array<std::map<uint8_t, size_t>, 12> activeTailByteFrequencies;
    for (const mw::legacy3d::TerrainScenarioRecord& scenario : scenarios) {
        for (size_t byte = 4; byte < scenario.raw.size(); ++byte) {
            ++allTailByteFrequencies[byte - 4][scenario.raw[byte]];
        }
        if (!scenario.isTerrainLayoutCandidate()) {
            continue;
        }
        for (size_t byte = 4; byte < scenario.raw.size(); ++byte) {
            ++activeTailByteFrequencies[byte - 4][scenario.raw[byte]];
        }
    }
    for (const mw::legacy3d::TerrainScenarioRecord& scenario : scenarios) {
        if (!scenario.isTerrainLayoutCandidate()) {
            continue;
        }
        const std::vector<std::string> tileNames = mw::legacy3d::terrainScenarioTileNames(scenario);
        snarioSignal
            << "active_record\t" << scenario.index << "\t\t" << joinPaths(tileNames) << "\t"
            << static_cast<int>(scenario.terrainMode) << "\t" << (scenario.usesBVariant() ? "yes" : "no") << "\t"
            << formatByteList(scenario.raw) << "\t\t\t\t"
            << "tile_ids_and_mode_only_proven"
            << "\n";
        const LayoutResources layout = buildLayoutResources(
            terrainRoot,
            tileNames,
            terrainRecords,
            cellSize,
            layoutGap,
            objectInset,
            objectScale,
            mirrorX,
            mirrorY,
            mirrorObjectY,
            colorMode);
        const std::filesystem::path minimapPath =
            outputDirectory / ("scenario" + std::to_string(scenario.index) + "_" + formatScaleTag(objectScale) + ".bmp");
        renderTerrainMinimap(
            minimapPath,
            tileNames,
            layout,
            minimapSize,
            cellSize,
            objectScale,
            mirrorX,
            mirrorY,
            mirrorObjectY,
            minimapRotation,
            colorMode,
            environmentId,
            scenario.index);

        std::ostringstream rawTail;
        for (size_t i = 4; i < scenario.raw.size(); ++i) {
            if (i > 4) {
                rawTail << ",";
            }
            rawTail << static_cast<int>(scenario.raw[i]);
        }
        std::ostringstream gridHashes;
        std::ostringstream worldHashes;
        for (size_t i = 0; i < tileNames.size(); ++i) {
            if (i > 0) {
                gridHashes << ",";
                worldHashes << ",";
            }
            gridHashes << formatHash(fnv1a64File(terrainRoot / "GRD" / (tileNames[i] + ".GRD")));
            worldHashes << formatHash(fnv1a64File(terrainRoot / "WLD" / (tileNames[i] + ".WLD")));
        }
        std::vector<mw::legacy3d::TerrainGrid> scenarioGrids;
        scenarioGrids.reserve(tileNames.size());
        for (const std::string& tileName : tileNames) {
            const std::filesystem::path gridPath = terrainRoot / "GRD" / (tileName + ".GRD");
            const mw::legacy3d::TerrainGrid grid = mw::legacy3d::loadTerrainGrid(gridPath);
            const GridStats stats = computeGridStats(grid);
            const uint16_t maxRaw = stats.rawValues.empty() ? 0u : *stats.rawValues.rbegin();
            grdSignal
                << scenario.index << "\t" << tileName << "\t" << grid.width << "x" << grid.height << "\t"
                << formatHash(fnv1a64File(gridPath)) << "\t" << grid.samples.size() << "\t"
                << stats.nonzeroSamples << "\t" << stats.blockingSamples << "\t"
                << stats.uniqueSamples << "\t" << maxRaw << "\t"
                << formatCountArray(stats.colorBandSamples) << "\t"
                << formatCountArray(stats.rawTopBitSamples) << "\t"
                << formatFrequentBytes(stats.detailFrequencies, 8) << "\t"
                << formatFrequentBytes(stats.rawFrequencies, 8) << "\n";
            ++grdRows;
            scenarioGrids.push_back(grid);
        }
        for (const GrdPatchStats& patch : collectCompositeGrdPatches(tileNames, scenarioGrids)) {
            const double centroidX = patch.cells == 0 ? 0.0 : patch.sumX / static_cast<double>(patch.cells);
            const double centroidY = patch.cells == 0 ? 0.0 : patch.sumY / static_cast<double>(patch.cells);
            grdPatchSignal
                << scenario.index << "\t" << patch.id << "\t" << patch.cells << "\t"
                << patch.minX << "," << patch.minY << "\t"
                << patch.maxX << "," << patch.maxY << "\t"
                << (patch.maxX - patch.minX + 1) << "x" << (patch.maxY - patch.minY + 1) << "\t"
                << formatFloat(static_cast<float>(centroidX)) << "," << formatFloat(static_cast<float>(centroidY)) << "\t"
                << joinNames(patch.tileSlots) << "\t"
                << static_cast<int>(patch.minRaw) << "\t" << static_cast<int>(patch.maxRaw) << "\t"
                << formatCountArray(patch.colorBandSamples) << "\t"
                << static_cast<int>(patch.minDetail) << "\t" << static_cast<int>(patch.maxDetail) << "\t"
                << formatFrequentBytes(patch.detailFrequencies, 8) << "\t"
                << formatFrequentBytes(patch.rawFrequencies, 8) << "\t"
                << "8_connected\t"
                << "nonzero_grd_patch_not_heightmap"
                << "\n";
            ++grdPatchRows;
        }
        report << scenario.index << "\t" << joinPaths(tileNames) << "\t"
               << static_cast<int>(scenario.terrainMode) << "\t" << rawTail.str() << "\t"
               << layout.wldObjects << "\t" << (layout.objects.triangleIndices.size() / 3u) << "\t" << cellSize << "\t" << objectScale << "\t"
               << colorMode << "\t" << (environmentId.has_value() ? std::to_string(*environmentId) : std::string("none")) << "\t"
               << "raw\t" << minimapRotationId(minimapRotation) << "\t" << minimapPath.filename().string() << "\t" << formatHash(fnv1a64File(minimapPath)) << "\t"
               << gridHashes.str() << "\t" << worldHashes.str() << "\n";
        ++written;
    }
    for (size_t byte = 4; byte < 16; ++byte) {
        const std::map<uint8_t, size_t>& activeFreq = activeTailByteFrequencies[byte - 4];
        const std::map<uint8_t, size_t>& allFreq = allTailByteFrequencies[byte - 4];
        std::string meaning = "unresolved";
        if (byte == 8) {
            meaning = "terrain_mode_proven";
        }
        snarioSignal
            << "byte_signal\t\t" << byte << "\t\t\t\t\t"
            << formatByteValues(activeFreq) << "\t"
            << formatFrequentBytes(activeFreq, 16) << "\t"
            << formatFrequentBytes(allFreq, 16) << "\t"
            << meaning
            << "\n";
    }
    report.close();
    snarioSignal.close();
    grdSignal.close();
    grdPatchSignal.close();
    std::cout
        << "terrain_scenario_report"
        << "\tpath=" << reportPath.string()
        << "\tscenarios=" << written
        << "\tsnario_signal=" << snarioSignalPath.string()
        << "\tgrd_signal=" << grdSignalPath.string()
        << "\tgrd_rows=" << grdRows
        << "\tgrd_patch_signal=" << grdPatchSignalPath.string()
        << "\tgrd_patch_rows=" << grdPatchRows
        << "\tcolor=" << colorMode
        << "\tenvironment_id=" << (environmentId.has_value() ? std::to_string(*environmentId) : std::string("none"))
        << "\tcell_size=" << cellSize
        << "\twld_placement=raw"
        << "\tminimap_rotation=" << minimapRotationId(minimapRotation)
        << "\tsnario_fnv1a64=" << formatHash(fnv1a64File(snarioPath))
        << "\tterpck_fnv1a64=" << formatHash(fnv1a64File(terpckTblPath))
        << "\treport_fnv1a64=" << formatHash(fnv1a64File(reportPath))
        << "\tsnario_signal_fnv1a64=" << formatHash(fnv1a64File(snarioSignalPath))
        << "\tgrd_signal_fnv1a64=" << formatHash(fnv1a64File(grdSignalPath))
        << "\tgrd_patch_signal_fnv1a64=" << formatHash(fnv1a64File(grdPatchSignalPath))
        << "\n";
}

void dumpTerrainGridProbe(
    const std::filesystem::path& snarioPath,
    size_t scenarioIndex,
    const std::filesystem::path& terrainRoot,
    GridProbeCoord coord) {
    if (coord.x < 0 || coord.y < 0 || coord.x >= 174 || coord.y >= 94) {
        throw mw::legacy3d::ParseError("--terrain-grid-probe coordinate must be inside 0..173,0..93");
    }
    const std::vector<mw::legacy3d::TerrainScenarioRecord> records =
        mw::legacy3d::loadTerrainScenarioRecords(snarioPath);
    const std::optional<mw::legacy3d::TerrainScenarioRecord> record =
        mw::legacy3d::terrainScenarioRecordByIndex(records, scenarioIndex);
    if (!record.has_value() || !record->isTerrainLayoutCandidate()) {
        throw mw::legacy3d::ParseError("--terrain-grid-probe requires an active SNARIO terrain scenario");
    }

    const std::vector<std::string> tileNames = mw::legacy3d::terrainScenarioTileNames(*record);
    std::vector<mw::legacy3d::TerrainGrid> grids;
    grids.reserve(tileNames.size());
    for (const std::string& tileName : tileNames) {
        grids.push_back(mw::legacy3d::loadTerrainGrid(terrainRoot / "GRD" / (tileName + ".GRD")));
    }

    auto sampleComposite = [&](int x, int y) -> std::optional<mw::legacy3d::TerrainGridSample> {
        if (x < 0 || y < 0 || x >= 174 || y >= 94) {
            return std::nullopt;
        }
        const int tileX = x / 87;
        const int tileY = y / 47;
        const size_t tileIndex = static_cast<size_t>(tileY * 2 + tileX);
        const int localX = x % 87;
        const int localY = y % 47;
        return grids[tileIndex].sampleAt(localX, localY);
    };

    const int tileX = coord.x / 87;
    const int tileY = coord.y / 47;
    const size_t tileIndex = static_cast<size_t>(tileY * 2 + tileX);
    const int localX = coord.x % 87;
    const int localY = coord.y % 47;
    const mw::legacy3d::TerrainGridSample center = grids[tileIndex].sampleAt(localX, localY);

    size_t occupancyCount = 0;
    std::optional<int> firstHitDx;
    std::optional<int> firstHitDy;
    std::ostringstream mask;
    std::ostringstream rawWindow;
    for (int dy = -2; dy <= 2; ++dy) {
        if (dy > -2) {
            mask << "/";
            rawWindow << "/";
        }
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx > -2) {
                rawWindow << ",";
            }
            const std::optional<mw::legacy3d::TerrainGridSample> sample =
                sampleComposite(coord.x + dx, coord.y + dy);
            if (!sample.has_value()) {
                mask << " ";
                rawWindow << "out";
                continue;
            }
            rawWindow << static_cast<int>(sample->rawValue);
            if (sample->blocking()) {
                mask << "#";
                ++occupancyCount;
                if (!firstHitDx.has_value()) {
                    firstHitDx = dx;
                    firstHitDy = dy;
                }
            } else {
                mask << ".";
            }
        }
    }

    std::cout
        << "terrain_grid_probe"
        << "\tscenario=" << scenarioIndex
        << "\ttiles=" << joinPaths(tileNames)
        << "\tgrid=" << coord.x << "," << coord.y
        << "\ttile=" << tileNames[tileIndex]
        << "\ttile_slot=" << tileIndex
        << "\tlocal=" << localX << "," << localY
        << "\traw=" << static_cast<int>(center.rawValue)
        << "\tblocking=" << (center.blocking() ? "yes" : "no")
        << "\tband=" << static_cast<int>(center.colorBand())
        << "\tdetail=" << static_cast<int>(center.detailBits())
        << "\toccupancy_5x5=" << occupancyCount
        << "\tfirst_hit_offset="
        << (firstHitDx.has_value() ? std::to_string(*firstHitDx) + "," + std::to_string(*firstHitDy) : "none")
        << "\tmask_5x5=" << mask.str()
        << "\traw_5x5=" << rawWindow.str()
        << "\toriginal_formula=grid_x_minus2_to_plus2_grid_y_minus2_to_plus2_raw_nonzero"
        << "\n";
}

const mw::legacy3d::RuntimeRecord* findRecord(
    const std::vector<mw::legacy3d::RuntimeRecord>& records,
    uint16_t recordIndex) {
    const auto it = std::find_if(
        records.begin(),
        records.end(),
        [recordIndex](const mw::legacy3d::RuntimeRecord& record) {
            return record.recordIndex == static_cast<int>(recordIndex);
        });
    return it == records.end() ? nullptr : &*it;
}

void dumpTerrainLayoutPreset(
    const std::filesystem::path& root,
    const std::string& id,
    const std::filesystem::path& terpckTblPath,
    float cellSize) {
    const mw::legacy3d::TerrainArenaLayout& layout = mw::legacy3d::terrainArenaLayoutById(id);
    const std::vector<mw::legacy3d::RuntimeRecord> terrainRecords =
        terpckTblPath.empty() ? std::vector<mw::legacy3d::RuntimeRecord>{}
                              : mw::legacy3d::loadRuntimeShapeRecords(terpckTblPath);
    size_t totalVertices = 0;
    size_t totalTriangles = 0;
    size_t totalObjects = 0;
    std::set<uint16_t> allRecordIds;
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;

    if (layout.tileNames.size() != 4u) {
        throw mw::legacy3d::ParseError("terrain layout preset dump requires exactly four tiles");
    }
    std::array<mw::legacy3d::TerrainGrid, 4> layoutGrids;
    for (size_t i = 0; i < layout.tileNames.size(); ++i) {
        layoutGrids[i] = mw::legacy3d::loadTerrainGrid(root / "GRD" / (layout.tileNames[i] + ".GRD"));
    }
    const mw::legacy3d::TerrainMesh layoutMesh = mw::legacy3d::buildTerrainMesh(
        mw::legacy3d::composeTerrainGrid2x2(layoutGrids, "layout:" + joinPaths(layout.tileNames), false, false),
        cellSize,
        20.0f);
    totalVertices = layoutMesh.vertices.size();
    totalTriangles = layoutMesh.indices.size() / 3u;
    minX = layoutMesh.boundsMin[0];
    minY = layoutMesh.boundsMin[1];
    minZ = layoutMesh.boundsMin[2];
    maxX = layoutMesh.boundsMax[0];
    maxY = layoutMesh.boundsMax[1];
    maxZ = layoutMesh.boundsMax[2];

    const float tileWidth = static_cast<float>(layoutGrids[0].width) * cellSize;
    const float tileDepth = static_cast<float>(layoutGrids[0].height) * cellSize;
    for (size_t i = 0; i < layout.tileNames.size(); ++i) {
        const std::string& name = layout.tileNames[i];
        const mw::legacy3d::TerrainWorld world = mw::legacy3d::loadTerrainWorld(root / "WLD" / (name + ".WLD"));
        const WorldStats worldStats = computeWorldStats(world);
        const std::vector<mw::legacy3d::TerrainObjectPlacement> placements =
            mw::legacy3d::mapTerrainWorldPlacementsFromRawCoordinates(
                world,
                mw::legacy3d::TerrainPlacementBounds{
                    0.0f,
                    tileWidth * 2.0f,
                    0.0f,
                    tileDepth * 2.0f,
                },
                cellSize);

        totalObjects += world.objects.size();
        allRecordIds.insert(worldStats.recordIds.begin(), worldStats.recordIds.end());

        std::cout
            << "terrain_layout_tile"
            << "\tid=" << layout.id
            << "\tslot=" << i
            << "\ttile=" << name
            << "\twld_objects=" << world.objects.size()
            << "\trecord_ids=" << joinIds(worldStats.recordIds)
            << "\n";

        for (const mw::legacy3d::TerrainObjectPlacement& placement : placements) {
            std::cout
                << "terrain_layout_object"
                << "\tid=" << layout.id
                << "\tslot=" << i
                << "\ttile=" << name
                << "\tobject=" << placement.sourceIndex
                << "\trecord=" << placement.recordIndex
                << "\traw_x=" << placement.rawX
                << "\traw_z=" << placement.rawZ
                << "\tplacement_x=" << formatFloat(placement.x)
                << "\tplacement_z=" << formatFloat(placement.z);
            if (!terrainRecords.empty()) {
                if (const mw::legacy3d::RuntimeRecord* record = findRecord(terrainRecords, placement.recordIndex)) {
                    const mw::legacy3d::RecordStats stats = mw::legacy3d::recordStats(*record);
                    std::cout
                        << "\trecord_vertices=" << record->vertices.size()
                        << "\trecord_polygons=" << stats.polygonCount
                        << "\trecord_lines=" << stats.lineCount;
                } else {
                    std::cout << "\trecord_missing=yes";
                }
            }
            std::cout << "\n";
        }
    }

    std::cout
        << "terrain_layout_summary"
        << "\tid=" << layout.id
        << "\ttiles=" << joinPaths(layout.tileNames)
        << "\tgap=" << layout.layoutGap
        << "\tinset=" << layout.objectInset
        << "\tvertices=" << totalVertices
        << "\ttriangles=" << totalTriangles
        << "\tbounds_min=" << formatFloat(minX) << "," << formatFloat(minY) << "," << formatFloat(minZ)
        << "\tbounds_max=" << formatFloat(maxX) << "," << formatFloat(maxY) << "," << formatFloat(maxZ)
        << "\twld_objects=" << totalObjects
        << "\tunique_record_ids=" << allRecordIds.size()
        << "\trecord_ids=" << joinIds(allRecordIds)
        << "\n";
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path gridPath;
    std::filesystem::path worldPath;
    std::filesystem::path terpckGiPath;
    std::filesystem::path terpckTblPath;
    std::filesystem::path snarioPath;
    std::filesystem::path terrainScanRoot;
    std::filesystem::path terrainRoot = std::filesystem::path("Sorted Original Files");
    std::filesystem::path terrainMinimapPath;
    std::filesystem::path terrainScenarioReportPath;
    std::string terrainLayoutPreset;
    std::string terrainColorMode = "debug";
    std::optional<int> terrainEnvironmentId;
    bool terrainColorExplicit = false;
    MinimapSize terrainMinimapSize;
    std::optional<size_t> scenarioIndex;
    std::optional<GridProbeCoord> terrainGridProbe;
    float terrainLayoutGap = 0.0f;
    float terrainCellSize = 500.0f;
    float terrainObjectInset = 0.18f;
    float terrainObjectScale = 1.0f;
    MinimapRotation terrainMinimapRotation = MinimapRotation::None;
    bool terrainTileMirrorX = false;
    bool terrainTileMirrorY = true;
    bool terrainObjectMirrorY = true;
    bool meshStats = false;
    bool listTerrainLayouts = false;
    bool terpckCatalog = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](std::filesystem::path& out) -> bool {
            if (i + 1 >= argc) {
                return false;
            }
            out = std::filesystem::path(argv[++i]);
            return true;
        };

        if (arg == "--grid") {
            if (!requireValue(gridPath)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--world") {
            if (!requireValue(worldPath)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--terpck-gi") {
            if (!requireValue(terpckGiPath)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--terpck-tbl") {
            if (!requireValue(terpckTblPath)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--snario") {
            if (!requireValue(snarioPath)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--scenario-index") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            scenarioIndex = static_cast<size_t>(std::stoul(argv[++i]));
        } else if (arg == "--terpck-catalog") {
            terpckCatalog = true;
        } else if (arg == "--scan-terrain") {
            if (!requireValue(terrainScanRoot)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--terrain-root") {
            if (!requireValue(terrainRoot)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--list-terrain-layouts") {
            listTerrainLayouts = true;
        } else if (arg == "--terrain-layout-preset") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainLayoutPreset = argv[++i];
        } else if (arg == "--terrain-minimap") {
            if (!requireValue(terrainMinimapPath)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--terrain-scenario-report") {
            if (!requireValue(terrainScenarioReportPath)) {
                printUsage();
                return 2;
            }
        } else if (arg == "--terrain-grid-probe") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainGridProbe = parseGridProbeCoord(argv[++i]);
        } else if (arg == "--terrain-minimap-size") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainMinimapSize = parseMinimapSize(argv[++i]);
        } else if (arg == "--terrain-layout-gap") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            const float requestedGap = std::stof(argv[++i]);
            if (requestedGap < 0.0f) {
                throw mw::legacy3d::ParseError("--terrain-layout-gap must be non-negative");
            }
            terrainLayoutGap = 0.0f;
        } else if (arg == "--terrain-cell-size") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainCellSize = std::stof(argv[++i]);
            if (terrainCellSize <= 0.0f) {
                throw mw::legacy3d::ParseError("--terrain-cell-size must be positive");
            }
        } else if (arg == "--terrain-object-inset") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainObjectInset = std::stof(argv[++i]);
            if (terrainObjectInset < 0.0f || terrainObjectInset >= 0.45f) {
                throw mw::legacy3d::ParseError("--terrain-object-inset must be in range 0.0..0.45");
            }
        } else if (arg == "--terrain-object-scale") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainObjectScale = std::stof(argv[++i]);
            if (terrainObjectScale <= 0.0f) {
                throw mw::legacy3d::ParseError("--terrain-object-scale must be positive");
            }
        } else if (arg == "--terrain-minimap-rotation") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainMinimapRotation = parseMinimapRotation(argv[++i]);
        } else if (arg == "--terrain-environment-id") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainEnvironmentId = std::stoi(argv[++i]);
            if (!terrainColorExplicit) {
                terrainColorMode = terrainColorModeForEnvironmentId(*terrainEnvironmentId);
            } else {
                (void)terrainColorModeForEnvironmentId(*terrainEnvironmentId);
            }
        } else if (arg == "--terrain-color") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            terrainColorMode = canonicalTerrainColorMode(argv[++i]);
            terrainColorExplicit = true;
            (void)terrainPaletteById(terrainColorMode);
        } else if (arg == "--terrain-tile-mirror-x") {
            terrainTileMirrorX = true;
        } else if (arg == "--terrain-no-tile-mirror-y") {
            terrainTileMirrorY = false;
        } else if (arg == "--terrain-no-object-mirror-y") {
            terrainObjectMirrorY = false;
        } else if (arg == "--mesh") {
            meshStats = true;
        } else {
            printUsage();
            return 2;
        }
    }

    if (gridPath.empty() && worldPath.empty() && terpckGiPath.empty() && terpckTblPath.empty() && snarioPath.empty() &&
        terrainScanRoot.empty() && terrainLayoutPreset.empty() && terrainMinimapPath.empty() &&
        terrainScenarioReportPath.empty() && !terrainGridProbe.has_value() && !listTerrainLayouts) {
        printUsage();
        return 2;
    }

    try {
        if (!gridPath.empty()) {
            dumpGrid(gridPath, meshStats);
        }
        if (!worldPath.empty()) {
            dumpWorld(worldPath);
        }
        if (!terpckGiPath.empty()) {
            dumpTerpckGi(terpckGiPath);
        }
        if (!terpckTblPath.empty()) {
            dumpTerpckTbl(terpckTblPath, terpckCatalog);
        }
        if (!snarioPath.empty() && !terrainGridProbe.has_value()) {
            dumpTerrainScenario(snarioPath, scenarioIndex);
        }
        if (terrainGridProbe.has_value()) {
            if (snarioPath.empty() || !scenarioIndex.has_value()) {
                throw mw::legacy3d::ParseError("--terrain-grid-probe requires --snario and --scenario-index");
            }
            dumpTerrainGridProbe(snarioPath, *scenarioIndex, terrainRoot, *terrainGridProbe);
        }
        if (!terrainScanRoot.empty()) {
            dumpTerrainScan(terrainScanRoot, terpckTblPath);
        }
        if (listTerrainLayouts) {
            dumpTerrainLayouts();
        }
        if (!terrainLayoutPreset.empty()) {
            dumpTerrainLayoutPreset(terrainRoot, terrainLayoutPreset, terpckTblPath, terrainCellSize);
        }
        if (!terrainMinimapPath.empty()) {
            if (terpckTblPath.empty()) {
                terpckTblPath = terrainRoot / "TBL" / "viewer8" / "TERPCK.TBL";
            }

            std::vector<std::string> tileNames;
            std::optional<size_t> minimapScenarioIndex;
            if (!terrainLayoutPreset.empty()) {
                const mw::legacy3d::TerrainArenaLayout& layout =
                    mw::legacy3d::terrainArenaLayoutById(terrainLayoutPreset);
                tileNames = layout.tileNames;
                terrainLayoutGap = layout.layoutGap;
                terrainObjectInset = layout.objectInset;
            } else {
                if (snarioPath.empty() || !scenarioIndex.has_value()) {
                    throw mw::legacy3d::ParseError(
                        "--terrain-minimap requires --terrain-layout-preset or --snario with --scenario-index");
                }
                const std::vector<mw::legacy3d::TerrainScenarioRecord> records =
                    mw::legacy3d::loadTerrainScenarioRecords(snarioPath);
                const std::optional<mw::legacy3d::TerrainScenarioRecord> record =
                    mw::legacy3d::terrainScenarioRecordByIndex(records, *scenarioIndex);
                if (!record.has_value() || !record->isTerrainLayoutCandidate()) {
                    throw mw::legacy3d::ParseError("SNARIO scenario index is not an active terrain layout candidate");
                }
                tileNames = mw::legacy3d::terrainScenarioTileNames(*record);
                minimapScenarioIndex = record->index;
            }

            const std::vector<mw::legacy3d::RuntimeRecord> terrainRecords =
                mw::legacy3d::loadRuntimeShapeRecords(terpckTblPath);
            const LayoutResources layout = buildLayoutResources(
                terrainRoot,
                tileNames,
                terrainRecords,
                terrainCellSize,
                terrainLayoutGap,
                terrainObjectInset,
                terrainObjectScale,
                terrainTileMirrorX,
                terrainTileMirrorY,
                terrainObjectMirrorY,
                terrainColorMode);
            renderTerrainMinimap(
                terrainMinimapPath,
                tileNames,
                layout,
                terrainMinimapSize,
                terrainCellSize,
                terrainObjectScale,
                terrainTileMirrorX,
                terrainTileMirrorY,
                terrainObjectMirrorY,
                terrainMinimapRotation,
                terrainColorMode,
                terrainEnvironmentId,
                minimapScenarioIndex);
        }
        if (!terrainScenarioReportPath.empty()) {
            if (snarioPath.empty()) {
                snarioPath = terrainRoot / "DAT" / "SNARIO.DAT";
            }
            if (terpckTblPath.empty()) {
                terpckTblPath = terrainRoot / "TBL" / "viewer8" / "TERPCK.TBL";
            }
            writeTerrainScenarioReport(
                terrainScenarioReportPath,
                snarioPath,
                terrainRoot,
                terpckTblPath,
                terrainMinimapSize,
                terrainCellSize,
                terrainLayoutGap,
                terrainObjectInset,
                terrainObjectScale,
                terrainTileMirrorX,
                terrainTileMirrorY,
                terrainObjectMirrorY,
                terrainMinimapRotation,
                terrainColorMode,
                terrainEnvironmentId);
        }
    } catch (const std::exception& exc) {
        std::cerr << "mw_terrain_dump: " << exc.what() << "\n";
        return 1;
    }

    return 0;
}
