#pragma once

#include "legacy3d/shape_parser.h"

namespace mw::mech3d {

enum class ModelGeometryPatch {
    None,
    BattleMasterBindFeet,
};

// Copies a decoded record and applies only the requested, signature-guarded
// presentation repair. Raw resources, parser output and GpuBatch stay intact.
legacy3d::RuntimeRecord resolveModelGeometryRecord(
    const std::vector<legacy3d::RuntimeRecord>& records,
    int recordIndex,
    ModelGeometryPatch patch);

bool hasOriginalBattleMasterBindFootDefect(
    const std::vector<legacy3d::RuntimeRecord>& records);

} // namespace mw::mech3d
