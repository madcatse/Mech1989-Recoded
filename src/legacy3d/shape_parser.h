#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace mw::legacy3d {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Vec3i {
    int16_t x = 0;
    int16_t y = 0;
    int16_t z = 0;
};

struct PointerEntry {
    uint16_t offset = 0;
    uint16_t segment = 0;
    uint32_t linear = 0;
};

struct RuntimePrimitive {
    int partIndex = 0;
    int commandIndex = 0;
    int primitiveIndex = 0;
    uint16_t descriptorOffset = 0;
    uint8_t opcode = 0;
    std::array<uint8_t, 4> shadeBytes{};
    uint8_t materialOrFlags = 0;
    uint16_t indexListOffset = 0;
    std::vector<uint8_t> rawIndices;
    std::vector<uint8_t> normalizedIndices;
    std::string kind;
};

struct RuntimeCommand {
    int partIndex = 0;
    int variantIndex = 0;
    int commandIndex = 0;
    uint16_t offset = 0;
    uint8_t distanceThreshold = 0;
    uint8_t commandType = 0;
    uint16_t primitiveCount = 0;
    uint16_t primitiveDescriptorPtr = 0;
    int16_t groupWord = 0;
    uint16_t radius = 0;
    uint8_t centerVertexIndex = 0;
    std::array<uint8_t, 3> sphereShadeBytes{};
    std::vector<RuntimePrimitive> primitives;
};

struct RuntimePart {
    int index = 0;
    uint16_t descriptorOffset = 0;
    uint8_t startVertex = 0;
    uint8_t vertexCount = 0;
    uint16_t vertexTablePtr = 0;
    uint16_t variantCount = 0;
    uint16_t variantTablePtr = 0;
    Vec3i sortVertex{};
    std::vector<RuntimeCommand> commands;
};

struct RuntimeRecord {
    std::string resourceName;
    int recordIndex = 0;
    uint16_t recordOffset = 0;
    uint16_t recordSegment = 0;
    uint32_t segmentBase = 0;
    uint8_t flags = 0;
    uint8_t scaleShift = 0;
    uint16_t extentOrRadius = 0;
    uint16_t partCount = 0;
    uint16_t partDescriptorPtr = 0;
    std::vector<Vec3i> vertices;
    std::vector<RuntimePart> parts;
};

struct RecordStats {
    int polygonCount = 0;
    int lineCount = 0;
    int primitiveCount = 0;
};

struct GpuBatchStats {
    int sourceVertexCount = 0;
    int batchVertexCount = 0;
    int triangleCount = 0;
    int triangleIndexCount = 0;
    int linePairCount = 0;
    int lineIndexCount = 0;
    int polygonCount = 0;
    int colorCount = 0;
};

struct GpuBatchOptions {
    std::string indexMode = "raw";
    std::string shadeMode = "stored";
    std::string swizzle = "xzy";
    float scale = 1.0f;
    bool showLines = true;
    bool showEdges = true;
    std::array<float, 3> edgeRgb{0.02f, 0.02f, 0.02f};
};

struct GpuBatch {
    std::vector<float> vertices; // packed float32 x,y,z,r,g,b rows
    std::vector<uint32_t> triangleIndices;
    std::vector<uint32_t> lineIndices;
    std::map<int, std::array<float, 16>> groupMatrices;
};

std::vector<uint8_t> readFileBytes(const std::filesystem::path& path);
std::vector<uint8_t> unpackDynamixBlock(const std::vector<uint8_t>& input);
std::vector<PointerEntry> parsePointerTable(const std::vector<uint8_t>& buf);
std::vector<RuntimeRecord> loadRuntimeShapeRecords(const std::filesystem::path& path);
RecordStats recordStats(const RuntimeRecord& record, const std::string& indexMode = "raw");
GpuBatchStats estimateGpuBatchStats(
    const RuntimeRecord& record,
    const std::string& indexMode = "raw",
    const std::string& shadeMode = "stored",
    bool showLines = true,
    bool showEdges = true);
GpuBatch buildGpuBatch(const RuntimeRecord& record, const GpuBatchOptions& options = {});

} // namespace mw::legacy3d
