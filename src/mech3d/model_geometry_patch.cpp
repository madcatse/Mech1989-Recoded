#include "mech3d/model_geometry_patch.h"

#include <algorithm>
#include <stdexcept>

namespace mw::mech3d {
namespace {

const legacy3d::RuntimeRecord* findRecord(
    const std::vector<legacy3d::RuntimeRecord>& records, int index) {
    const auto it = std::find_if(records.begin(), records.end(),
        [index](const auto& record) { return record.recordIndex == index; });
    return it == records.end() ? nullptr : &*it;
}

// FNV-1a of the complete decoded vertex/primitive geometry, including colors.
// Each integer is serialized as a little-endian uint32; pointers and diagnostic
// labels are deliberately excluded. See BATTLEMASTER_BIND_FEET_EVIDENCE.md.
uint64_t geometrySignature(const legacy3d::RuntimeRecord& record) {
    uint64_t hash = UINT64_C(14695981039346656037);
    const auto add = [&hash](uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            hash ^= (value >> (byte * 8)) & 0xffu;
            hash *= UINT64_C(1099511628211);
        }
    };
    add(record.flags); add(record.scaleShift);
    add(static_cast<uint32_t>(record.vertices.size()));
    for (const auto& v : record.vertices) { add(v.x); add(v.y); add(v.z); }
    add(static_cast<uint32_t>(record.parts.size()));
    for (const auto& part : record.parts) {
        add(part.index); add(part.startVertex); add(part.vertexCount); add(part.variantCount);
        add(static_cast<uint32_t>(part.commands.size()));
        for (const auto& command : part.commands) {
            add(command.variantIndex); add(command.commandIndex);
            add(command.distanceThreshold); add(command.commandType); add(command.primitiveCount);
            add(static_cast<uint32_t>(command.primitives.size()));
            for (const auto& primitive : command.primitives) {
                add(primitive.opcode);
                for (const auto color : primitive.shadeBytes) add(color);
                add(primitive.materialOrFlags);
                add(static_cast<uint32_t>(primitive.rawIndices.size()));
                for (const auto index : primitive.rawIndices) add(index);
                add(static_cast<uint32_t>(primitive.normalizedIndices.size()));
                for (const auto index : primitive.normalizedIndices) add(index);
            }
        }
    }
    return hash;
}

} // namespace

bool hasOriginalBattleMasterBindFootDefect(
    const std::vector<legacy3d::RuntimeRecord>& records) {
    const auto* right = findRecord(records, 2);
    const auto* left = findRecord(records, 3);
    return right && left && right->resourceName == "BMAPCK" && left->resourceName == "BMAPCK" &&
        geometrySignature(*right) == UINT64_C(0x354437a90c3f9986) &&
        geometrySignature(*left) == UINT64_C(0x529ce480210a8471);
}

legacy3d::RuntimeRecord resolveModelGeometryRecord(
    const std::vector<legacy3d::RuntimeRecord>& records,
    int recordIndex,
    ModelGeometryPatch patch) {
    const auto* source = findRecord(records, recordIndex);
    if (!source) throw std::runtime_error("assembly source record not found: " + std::to_string(recordIndex));
    auto result = *source;
    if (patch != ModelGeometryPatch::BattleMasterBindFeet || (recordIndex != 2 && recordIndex != 3) ||
        !hasOriginalBattleMasterBindFootDefect(records)) return result;

    const auto& donor = *findRecord(records, 3);
    if (recordIndex == 2) {
        // The negative-X foot is authored in the positive-X leg's record.
        // Move a copy to its matching negative-X shin, preserving the original
        // vertices, winding, part commands and palette indices.
        result.vertices.resize(30);
        result.vertices.insert(result.vertices.end(), donor.vertices.begin() + 30, donor.vertices.end());
        result.parts[2] = donor.parts[2];
        for (auto& part : result.parts) part.vertexCount = static_cast<uint8_t>(result.vertices.size());
    } else {
        // Retain the authored five-face foot design. Reflection also reverses
        // polygon winding so it remains outward-facing after X is mirrored.
        for (size_t i = 30; i < result.vertices.size(); ++i)
            result.vertices[i].x = static_cast<int16_t>(-result.vertices[i].x);
        for (auto& command : result.parts[2].commands) {
            for (auto& primitive : command.primitives) {
                std::reverse(primitive.rawIndices.begin(), primitive.rawIndices.end());
                std::reverse(primitive.normalizedIndices.begin(), primitive.normalizedIndices.end());
            }
        }
    }
    result.parts[2].sortVertex = result.vertices[result.parts[2].startVertex];
    return result;
}

} // namespace mw::mech3d
