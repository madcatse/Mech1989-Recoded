#include "mech3d/mech_catalog.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
using namespace mw;
size_t checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
bool sameBatch(const legacy3d::GpuBatch& a, const legacy3d::GpuBatch& b) {
    return a.vertices == b.vertices && a.triangleIndices == b.triangleIndices &&
        a.lineIndices == b.lineIndices && a.groupMatrices == b.groupMatrices;
}
std::array<int, 3> point(legacy3d::Vec3i p) { return {p.x,p.y,p.z}; }
std::array<int, 3> normal(const legacy3d::RuntimeRecord& r, const legacy3d::RuntimePrimitive& p) {
    const auto a = point(r.vertices.at(p.normalizedIndices.at(0)));
    const auto b = point(r.vertices.at(p.normalizedIndices.at(1)));
    const auto c = point(r.vertices.at(p.normalizedIndices.at(2)));
    std::array<int,3> u{},v{};
    for (size_t i = 0; i < 3; ++i) { u[i]=b[i]-a[i]; v[i]=c[i]-a[i]; }
    return {u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
}
void checkFoot(const legacy3d::RuntimeRecord& r, int side) {
    const auto& foot = r.parts.at(2).commands.at(0);
    require(foot.primitives.size() == 5, "foot must retain five authored faces");
    std::set<std::array<int,3>> shinBase, footTop;
    int minZ = 0, maxZ = -10000;
    for (size_t part = 0; part < 2; ++part)
        for (const auto& command : r.parts[part].commands)
            for (const auto& p : command.primitives)
                for (auto index : p.normalizedIndices) {
                    const auto v = r.vertices.at(index);
                    if (v.z == -410) shinBase.insert(point(v));
                }
    for (const auto& p : foot.primitives) {
        require(p.normalizedIndices.size() == 4, "foot face must remain a quad");
        for (auto index : p.normalizedIndices) {
            const auto v = r.vertices.at(index);
            require(side*v.x > 0, "foot crossed to opposite leg");
            minZ = std::min(minZ,int(v.z)); maxZ = std::max(maxZ,int(v.z));
            if (v.z == -410) footTop.insert(point(v));
        }
    }
    require(minZ == -500 && maxZ == -410, "foot ground/ankle heights changed");
    require(footTop.size() == 4 && footTop == shinBase, "foot does not join all four shin corners");
}
void checkSource(const std::vector<legacy3d::RuntimeRecord>& records) {
    using mech3d::ModelGeometryPatch;
    const auto correctedRight = mech3d::resolveModelGeometryRecord(records,2,ModelGeometryPatch::BattleMasterBindFeet);
    const auto correctedLeft = mech3d::resolveModelGeometryRecord(records,3,ModelGeometryPatch::BattleMasterBindFeet);
    checkFoot(correctedRight,-1); checkFoot(correctedLeft,1);
    const auto& originalFoot = records.at(3).parts.at(2).commands.at(0).primitives;
    for (size_t i = 0; i < originalFoot.size(); ++i) {
        const auto& right = correctedRight.parts[2].commands[0].primitives[i];
        const auto& left = correctedLeft.parts[2].commands[0].primitives[i];
        require(right.shadeBytes == originalFoot[i].shadeBytes && left.shadeBytes == originalFoot[i].shadeBytes,
            "foot repair changed original colors");
        require(normal(correctedRight,right) == normal(records[3],originalFoot[i]), "source foot winding changed");
        const auto a = normal(correctedRight,right), b = normal(correctedLeft,left);
        // Quads are trapezoids: compare directions, not triangle areas.
        require(-a[0]*b[0]+a[1]*b[1]+a[2]*b[2] > 0 &&
            (-a[0])*b[1] == a[1]*b[0] && a[1]*b[2] == a[2]*b[1] &&
            a[2]*b[0] == (-a[0])*b[2], "reflected foot has reversed or changed normal");
    }
    for (int index : {2,3}) {
        auto before = records.at(index);
        auto after = index == 2 ? correctedRight : correctedLeft;
        before.parts.resize(2); after.parts.resize(2);
        require(sameBatch(legacy3d::buildGpuBatch(before),legacy3d::buildGpuBatch(after)), "repair changed upper leg geometry");
    }
    auto alreadyPatched = records;
    alreadyPatched[2] = correctedRight; alreadyPatched[3] = correctedLeft;
    require(!mech3d::hasOriginalBattleMasterBindFootDefect(alreadyPatched), "patched asset matched original defect");
    for (int index : {2,3}) require(sameBatch(legacy3d::buildGpuBatch(alreadyPatched[index]),
        legacy3d::buildGpuBatch(mech3d::resolveModelGeometryRecord(alreadyPatched,index,ModelGeometryPatch::BattleMasterBindFeet))),
        "applying patch twice duplicates geometry");

    auto modified = records;
    modified[3].vertices[32].x += 1;
    require(!mech3d::hasOriginalBattleMasterBindFootDefect(modified), "unknown asset revision accepted");
    for (int index : {2,3}) require(sameBatch(legacy3d::buildGpuBatch(modified[index]),
        legacy3d::buildGpuBatch(mech3d::resolveModelGeometryRecord(modified,index,ModelGeometryPatch::BattleMasterBindFeet))),
        "unknown revision was partially repaired");
    modified = records; modified[2].resourceName = "OTHER";
    require(!mech3d::hasOriginalBattleMasterBindFootDefect(modified), "unrelated resource accepted");
}
} // namespace

int main() {
    try {
        using mech3d::ModelGeometryPatch;
        const auto path = mech3d::catalogDefaultResourcePath("battlemaster");
        const auto originalBytes = legacy3d::readFileBytes(path);
        const auto records = legacy3d::loadRuntimeShapeRecords(path);
        require(mech3d::hasOriginalBattleMasterBindFootDefect(records), "expected original base asset fixture");
        require(records[2].parts[2].commands[0].primitiveCount == 0, "raw parser no longer preserves empty foot block");
        checkSource(records);
        const auto definition = mech3d::makeCatalogMechModelDefinition("battlemaster");
        auto rawDefinition = definition; rawDefinition.geometryPatch = ModelGeometryPatch::None;
        const auto raw = mech3d::buildMechRenderInstance(rawDefinition,records,{});
        const auto fixed = mech3d::buildMechRenderInstance(definition,records,{});
        require(fixed.assembly.components.size() == 6 && fixed.ranges.size() == 6, "component identity changed");
        require(fixed.batch.triangleIndices.size() == raw.batch.triangleIndices.size()+30, "repair must add exactly ten triangles");
        require(fixed.assembly.bounds.min.x == raw.assembly.bounds.min.x && fixed.assembly.bounds.min.y == raw.assembly.bounds.min.y &&
            fixed.assembly.bounds.min.z == raw.assembly.bounds.min.z, "overall minimum bounds changed");
        require(fixed.assembly.bounds.max.x == raw.assembly.bounds.max.x && fixed.assembly.bounds.max.y == raw.assembly.bounds.max.y &&
            fixed.assembly.bounds.max.z == raw.assembly.bounds.max.z, "overall maximum bounds changed");
        for (const auto& component : fixed.assembly.components) {
            if (component.sourceRecordIndex != 2 && component.sourceRecordIndex != 3) continue;
            require(component.localBounds.min.y == -500, "patched leg does not reach ground");
            mech3d::MechComponentVisibility hidden{{component.componentId},true};
            require(!mech3d::isComponentVisible(fixed.assembly,hidden,component.componentId), "foot escaped component visibility");
        }
        // Every source record in every other chassis, plus every walk/death
        // record of BattleMaster, must retain its original batch byte-for-byte.
        size_t unchanged = 0;
        for (const auto& entry : mech3d::mechCatalogEntries()) {
            const auto source = legacy3d::loadRuntimeShapeRecords(entry.defaultResourcePath);
            const auto d = mech3d::makeCatalogMechModelDefinition(entry.presetId);
            for (const auto& r : source) {
                if (entry.presetId == "battlemaster" && (r.recordIndex == 2 || r.recordIndex == 3)) continue;
                require(sameBatch(legacy3d::buildGpuBatch(r),legacy3d::buildGpuBatch(
                    mech3d::resolveModelGeometryRecord(source,r.recordIndex,d.geometryPatch))), "unrelated record changed");
                ++unchanged;
            }
        }
        const auto walk = mech3d::makeCatalogAnimationDefinition("battlemaster","walk");
        for (int frame = 0; frame < mech3d::animationFrameCount(walk); ++frame) {
            const auto d = mech3d::applyAnimationFrame(definition,walk,frame);
            auto unpatched = d; unpatched.geometryPatch = ModelGeometryPatch::None;
            require(sameBatch(mech3d::buildMechRenderInstance(d,records,{}).batch,
                mech3d::buildMechRenderInstance(unpatched,records,{}).batch), "walk assembly changed");
        }
        const auto death = mech3d::makeCatalogAssemblyFrameAnimationDefinition("battlemaster","death");
        for (int frame = 0; frame < mech3d::assemblyFrameAnimationFrameCount(death); ++frame)
            require(mech3d::applyAssemblyFrameAnimationFrame(death,frame).geometryPatch == ModelGeometryPatch::None,
                "bind repair leaked into death assembly");
        require(mech3d::hasOriginalBattleMasterBindFootDefect(records), "caller records mutated");
        require(legacy3d::readFileBytes(path) == originalBytes, "original TBL changed");
        std::cout << "mw_battlemaster_feet_smoke: ok checks=" << checks
            << " unchanged_records=" << unchanged << " feet=2 authored_faces=5 added_triangles=10 original_file=unchanged\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "mw_battlemaster_feet_smoke: " << e.what() << '\n';
        return 1;
    }
}
