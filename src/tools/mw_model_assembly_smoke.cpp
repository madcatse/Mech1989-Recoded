#include "legacy3d/shape_parser.h"
#include "mech3d/mech_catalog.h"
#include "mech3d/mech_runtime_state.h"
#include "mech3d/model_assembly.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

bool isIdentity(const mw::mech3d::Mat4f& matrix) {
    return matrix == mw::mech3d::identityMatrix();
}

float matrixTranslationX(const mw::mech3d::Mat4f& matrix) {
    return matrix[12];
}

float matrixRotationYSign(const mw::mech3d::Mat4f& matrix) {
    return matrix[8];
}

float matrixTranslationY(const mw::mech3d::Mat4f& matrix) {
    return matrix[13];
}

bool nearZero(float value) {
    return std::abs(value) < 0.001f;
}

bool sameVec3(mw::mech3d::Vec3f a, mw::mech3d::Vec3f b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

const mw::mech3d::MechComponentDefinition& findDefinitionComponent(
    const mw::mech3d::MechModelDefinition& definition,
    int componentId) {
    for (const mw::mech3d::MechComponentDefinition& component : definition.components) {
        if (component.componentId == componentId) {
            return component;
        }
    }
    throw std::runtime_error("definition component not found");
}

const mw::mech3d::ModelAssemblyComponent& findAssemblyComponent(
    const mw::mech3d::ModelAssembly& assembly,
    int componentId) {
    for (const mw::mech3d::ModelAssemblyComponent& component : assembly.components) {
        if (component.componentId == componentId) {
            return component;
        }
    }
    throw std::runtime_error("assembly component not found");
}

const mw::mech3d::MechCatalogEntry& findCatalogEntry(
    const std::vector<mw::mech3d::MechCatalogEntry>& entries,
    const std::string& presetId) {
    for (const mw::mech3d::MechCatalogEntry& entry : entries) {
        if (entry.presetId == presetId) {
            return entry;
        }
    }
    throw std::runtime_error("catalog entry not found");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::filesystem::path resourcePath =
            argc > 1 ? std::filesystem::path(argv[1])
                     : std::filesystem::path("Sorted Original Files") / "TBL" / "viewer8" / "JENPCK.TBL";
        const std::filesystem::path marauderResourcePath =
            argc > 2 ? std::filesystem::path(argv[2]) : resourcePath.parent_path() / "MARPCK.TBL";
        const std::filesystem::path locustResourcePath =
            argc > 3 ? std::filesystem::path(argv[3]) : resourcePath.parent_path() / "LOCPCK.TBL";
        const std::vector<mw::mech3d::MechCatalogEntry> catalogEntries = mw::mech3d::mechCatalogEntries();
        require(catalogEntries.size() == 8u, "catalog entry count mismatch");
        const mw::mech3d::MechCatalogEntry& battlemasterEntry = findCatalogEntry(catalogEntries, "battlemaster");
        const mw::mech3d::MechCatalogEntry& warhammerEntry = findCatalogEntry(catalogEntries, "warhammer");
        const mw::mech3d::MechCatalogEntry& marauderEntry = findCatalogEntry(catalogEntries, "marauder");
        const mw::mech3d::MechCatalogEntry& locustEntry = findCatalogEntry(catalogEntries, "locust");
        const mw::mech3d::MechCatalogEntry& riflemanEntry = findCatalogEntry(catalogEntries, "rifleman");
        const mw::mech3d::MechCatalogEntry& jennerEntry = findCatalogEntry(catalogEntries, "jenner");
        const mw::mech3d::MechCatalogEntry& phoenixHawkEntry = findCatalogEntry(catalogEntries, "phoenix_hawk");
        const mw::mech3d::MechCatalogEntry& shadowHawkEntry = findCatalogEntry(catalogEntries, "shadow_hawk");
        require(
            battlemasterEntry.defaultResourcePath.filename() == std::filesystem::path("BMAPCK.TBL"),
            "Battlemaster catalog resource mismatch");
        require(
            warhammerEntry.defaultResourcePath.filename() == std::filesystem::path("HAMPCK.TBL"),
            "Warhammer catalog resource mismatch");
        require(
            marauderEntry.defaultResourcePath.filename() == std::filesystem::path("MARPCK.TBL"),
            "Marauder catalog resource mismatch");
        require(
            locustEntry.defaultResourcePath.filename() == std::filesystem::path("LOCPCK.TBL"),
            "Locust catalog resource mismatch");
        require(
            riflemanEntry.defaultResourcePath.filename() == std::filesystem::path("RIFPCK.TBL"),
            "Rifleman catalog resource mismatch");
        require(
            jennerEntry.defaultResourcePath.filename() == std::filesystem::path("JENPCK.TBL"),
            "Jenner catalog resource mismatch");
        require(
            phoenixHawkEntry.defaultResourcePath.filename() == std::filesystem::path("PHAPCK.TBL"),
            "Phoenix Hawk catalog resource mismatch");
        require(
            shadowHawkEntry.defaultResourcePath.filename() == std::filesystem::path("SHAPCK.TBL"),
            "Shadow Hawk catalog resource mismatch");
        require(
            battlemasterEntry.animationIds == std::vector<std::string>{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"},
            "Battlemaster catalog animation id list mismatch");
        require(
            warhammerEntry.animationIds == std::vector<std::string>{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"},
            "Warhammer catalog animation id list mismatch");
        require(
            marauderEntry.animationIds == std::vector<std::string>{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"},
            "Marauder catalog animation id list mismatch");
        require(
            locustEntry.animationIds == std::vector<std::string>{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"},
            "Locust catalog animation id list mismatch");
        require(
            jennerEntry.animationIds ==
                std::vector<std::string>{"idle", "walk", "death_forward_slot_probe", "death_backward_slot_probe", "death", "death_forward", "death_backward"},
            "Jenner catalog animation id list mismatch");
        require(
            phoenixHawkEntry.animationIds ==
                std::vector<std::string>{"idle", "walk", "death_forward_slot_probe", "death_backward_slot_probe", "death", "death_forward", "death_backward"},
            "Phoenix Hawk catalog animation id list mismatch");
        require(
            shadowHawkEntry.animationIds ==
                std::vector<std::string>{"idle", "walk", "death_forward_slot_probe", "death_backward_slot_probe", "death", "death_forward", "death_backward"},
            "Shadow Hawk catalog animation id list mismatch");
        require(
            battlemasterEntry.destroyedSlotQuality == mw::mech3d::MechCatalogDestroyedSlotQuality::ComponentClean,
            "Battlemaster catalog entry slot quality mismatch");
        require(
            warhammerEntry.destroyedSlotQuality == mw::mech3d::MechCatalogDestroyedSlotQuality::ComponentClean,
            "Warhammer catalog entry slot quality mismatch");
        require(
            marauderEntry.destroyedSlotQuality == mw::mech3d::MechCatalogDestroyedSlotQuality::AssemblyOnly,
            "Marauder catalog entry slot quality mismatch");
        require(
            locustEntry.destroyedSlotQuality == mw::mech3d::MechCatalogDestroyedSlotQuality::ComponentClean,
            "Locust catalog entry slot quality mismatch");
        require(
            riflemanEntry.destroyedSlotQuality == mw::mech3d::MechCatalogDestroyedSlotQuality::AssemblyOnly,
            "Rifleman catalog entry slot quality mismatch");
        require(
            jennerEntry.destroyedSlotQuality == mw::mech3d::MechCatalogDestroyedSlotQuality::ComponentClean,
            "Jenner catalog entry slot quality mismatch");
        require(
            phoenixHawkEntry.destroyedSlotQuality == mw::mech3d::MechCatalogDestroyedSlotQuality::ComponentClean,
            "Phoenix Hawk catalog entry slot quality mismatch");
        require(
            shadowHawkEntry.destroyedSlotQuality == mw::mech3d::MechCatalogDestroyedSlotQuality::ComponentClean,
            "Shadow Hawk catalog entry slot quality mismatch");
        const mw::mech3d::MechModelDefinition definition =
            mw::mech3d::makeJennerBaselineModelDefinition();
        require(definition.debugName == "jenner_baseline_records_0_5", "definition debug name mismatch");
        require(definition.components.size() == 6u, "definition component count mismatch");
        for (size_t i = 0; i < definition.components.size(); ++i) {
            require(definition.components[i].sourceRecordIndex == static_cast<int>(i), "Jenner baseline must preserve source record order");
        }
        require(findDefinitionComponent(definition, 0).parentComponentId == -1, "torso must be root");
        require(findDefinitionComponent(definition, 0).sourceRecordIndex == 4, "torso source record mismatch");
        require(findDefinitionComponent(definition, 0).debugLabel == "jenner_torso_record_4", "torso debug label mismatch");
        require(findDefinitionComponent(definition, 1).parentComponentId == 0, "cockpit parent mismatch");
        require(findDefinitionComponent(definition, 1).sourceRecordIndex == 5, "cockpit source record mismatch");
        require(findDefinitionComponent(definition, 2).sourceRecordIndex == 2, "right leg candidate source record mismatch");
        require(findDefinitionComponent(definition, 3).sourceRecordIndex == 3, "left leg candidate source record mismatch");
        require(findDefinitionComponent(definition, 4).sourceRecordIndex == 0, "arm pod candidate A source record mismatch");
        require(findDefinitionComponent(definition, 5).sourceRecordIndex == 1, "arm pod candidate B source record mismatch");
        for (const mw::mech3d::MechComponentDefinition& component : definition.components) {
            require(isIdentity(component.localMatrix), "initial component local matrix must be identity");
            require(!component.debugLabel.empty(), "component debug label must be present");
        }

        const std::vector<mw::legacy3d::RuntimeRecord> records = mw::legacy3d::loadRuntimeShapeRecords(resourcePath);
        mw::legacy3d::GpuBatchOptions batchOptions;
        const mw::mech3d::MechRenderInstance instance =
            mw::mech3d::buildMechRenderInstance(definition, records, batchOptions);

        require(instance.assembly.components.size() == definition.components.size(), "assembly component count mismatch");
        require(instance.ranges.size() == definition.components.size(), "render range count mismatch");
        require(instance.assembly.bounds.valid, "assembly bounds must be valid");
        require(instance.batch.vertices.size() / 6u == 444u, "assembled vertex count changed");
        require(instance.batch.triangleIndices.size() / 3u == 108u, "assembled triangle count changed");
        require(instance.batch.lineIndices.size() / 2u == 222u, "assembled line pair count changed");

        for (size_t i = 0; i < instance.assembly.components.size(); ++i) {
            const mw::mech3d::ModelAssemblyComponent& component = instance.assembly.components[i];
            const mw::mech3d::ComponentRenderRange& range = instance.ranges[i];
            require(component.worldBounds.valid, "component world bounds must be valid");
            require(isIdentity(component.localMatrix), "assembled component local matrix must start as identity");
            require(isIdentity(component.worldMatrix), "assembled component world matrix must start as identity");
            require(component.componentId == range.componentId, "component/range id mismatch");
            require(component.sourceRecordIndex == range.sourceRecordIndex, "component/range source record mismatch");
            require(range.vertexCount > 0, "component render range must contain vertices");
        }

        mw::mech3d::MechPose pose;
        pose.componentLocalMatrices.assign(definition.components.size(), mw::mech3d::identityMatrix());
        pose.componentLocalMatrices[5] = mw::mech3d::translationMatrix(mw::mech3d::Vec3f{10.0f, 0.0f, 0.0f});
        const mw::mech3d::ModelAssembly posedAssembly =
            mw::mech3d::buildModelAssembly(definition, records, batchOptions, pose);
        const mw::mech3d::ModelAssemblyComponent& posedTorso = findAssemblyComponent(posedAssembly, 0);
        const mw::mech3d::ModelAssemblyComponent& posedCockpit = findAssemblyComponent(posedAssembly, 1);
        const mw::mech3d::ModelAssemblyComponent& bindCockpit = findAssemblyComponent(instance.assembly, 1);
        require(matrixTranslationX(posedTorso.worldMatrix) == 0.0f, "torso pose should remain at bind transform");
        require(matrixTranslationX(posedCockpit.worldMatrix) == 10.0f, "cockpit pose translation not applied");
        require(
            posedCockpit.worldBounds.center.x == bindCockpit.worldBounds.center.x + 10.0f,
            "cockpit world bounds must follow pose transform");

        const mw::mech3d::MechRenderInstance posedInstance =
            mw::mech3d::buildMechRenderInstance(definition, records, batchOptions, pose);
        require(posedInstance.batch.vertices == instance.batch.vertices, "pose must not bake transforms into render vertices");
        require(posedInstance.batch.triangleIndices == instance.batch.triangleIndices, "pose must not change triangle indices");
        require(posedInstance.batch.lineIndices == instance.batch.lineIndices, "pose must not change line indices");

        mw::mech3d::MechRenderInstance updatedInstance = instance;
        mw::mech3d::updateMechRenderInstancePose(updatedInstance, pose);
        const mw::mech3d::ModelAssemblyComponent& updatedCockpit = findAssemblyComponent(updatedInstance.assembly, 1);
        require(updatedInstance.batch.vertices == instance.batch.vertices, "pose-only update must leave render vertices untouched");
        require(matrixTranslationX(updatedCockpit.worldMatrix) == 10.0f, "pose-only update did not update world matrix");

        mw::mech3d::MechPose overridePose;
        mw::mech3d::setComponentPoseLocalMatrix(
            overridePose,
            1,
            mw::mech3d::rotateAroundPivotYMatrix(mw::mech3d::Vec3f{}, 0.25f));
        mw::mech3d::MechRenderInstance overrideInstance = instance;
        mw::mech3d::updateMechRenderInstancePose(overrideInstance, overridePose);
        const mw::mech3d::ModelAssemblyComponent& overrideCockpit = findAssemblyComponent(overrideInstance.assembly, 1);
        const mw::mech3d::ModelAssemblyComponent& overrideTorso = findAssemblyComponent(overrideInstance.assembly, 0);
        require(overrideInstance.batch.vertices == instance.batch.vertices, "component-id pose override must leave render vertices untouched");
        require(matrixRotationYSign(overrideCockpit.worldMatrix) > 0.0f, "component-id pose override did not rotate cockpit");
        require(isIdentity(overrideTorso.worldMatrix), "component-id pose override must not affect torso");

        const mw::mech3d::Mat4f pivotRotation =
            mw::mech3d::rotateAroundPivotZMatrix(mw::mech3d::Vec3f{10.0f, 0.0f, 0.0f}, 0.25f);
        require(!isIdentity(pivotRotation), "pivot rotation helper must produce a transform");
        require(matrixTranslationY(pivotRotation) != 0.0f, "non-zero pivot rotation should create a translation term");

        bool emptyRejected = false;
        try {
            (void)mw::mech3d::makeRecordListModelDefinition({}, "empty");
        } catch (const std::runtime_error&) {
            emptyRejected = true;
        }
        require(emptyRejected, "empty definition input must be rejected");

        bool missingRejected = false;
        try {
            const mw::mech3d::MechModelDefinition missing =
                mw::mech3d::makeRecordListModelDefinition({99999}, "missing");
            (void)mw::mech3d::buildMechRenderInstance(missing, records, batchOptions);
        } catch (const std::runtime_error&) {
            missingRejected = true;
        }
        require(missingRejected, "missing source record must be rejected");

        const std::vector<mw::legacy3d::RuntimeRecord> marauderRecords =
            mw::legacy3d::loadRuntimeShapeRecords(marauderResourcePath);
        const std::vector<mw::legacy3d::RuntimeRecord> locustRecords =
            mw::legacy3d::loadRuntimeShapeRecords(locustResourcePath);
        const mw::mech3d::MechModelDefinition marauderLegBind =
            mw::mech3d::makeRecordListModelDefinition({22}, "marauder_leg_sequence_probe");
        const mw::mech3d::ModelAnimationDefinition marauderLegAnimation{
            "marauder_leg_records_22_25",
            {mw::mech3d::ComponentFrameSequence{0, {22, 23, 24, 25}, 180, "marauder_leg_walk_probe"}},
        };
        require(mw::mech3d::animationFrameCount(marauderLegAnimation) == 4, "Marauder leg animation frame count mismatch");
        require(mw::mech3d::animationFrameForElapsedMs(marauderLegAnimation, 0) == 0, "animation time frame 0 mismatch");
        require(mw::mech3d::animationFrameForElapsedMs(marauderLegAnimation, 180) == 1, "animation time frame 1 mismatch");
        require(mw::mech3d::animationFrameForElapsedMs(marauderLegAnimation, 720) == 0, "animation time wrap mismatch");

        std::vector<mw::mech3d::MechRenderInstance> legFrames;
        for (int frame = 0; frame < 4; ++frame) {
            const mw::mech3d::MechModelDefinition frameDefinition =
                mw::mech3d::applyAnimationFrame(marauderLegBind, marauderLegAnimation, frame);
            require(frameDefinition.components.size() == 1u, "Marauder frame definition component count mismatch");
            require(frameDefinition.components[0].componentId == 0, "Marauder frame component id mismatch");
            require(frameDefinition.components[0].sourceRecordIndex == 22 + frame, "Marauder frame source record mismatch");

            mw::mech3d::MechRenderInstance frameInstance =
                mw::mech3d::buildMechRenderInstance(frameDefinition, marauderRecords, batchOptions);
            require(frameInstance.ranges.size() == 1u, "Marauder frame render range count mismatch");
            require(frameInstance.ranges[0].sourceRecordIndex == 22 + frame, "Marauder frame render range source mismatch");
            require(!frameInstance.batch.vertices.empty(), "Marauder frame must contain vertices");
            require(!frameInstance.batch.triangleIndices.empty(), "Marauder frame must contain triangles");
            legFrames.push_back(std::move(frameInstance));
        }
        require(legFrames[0].batch.vertices != legFrames[1].batch.vertices, "Marauder record-frame animation must swap geometry");

        const mw::mech3d::MechModelDefinition marauderTwoLimbBind =
            mw::mech3d::makeRecordListModelDefinition({22, 18}, "marauder_two_limb_sequence_probe");
        const mw::mech3d::ModelAnimationDefinition marauderTwoLimbAnimation{
            "marauder_two_limb_records",
            {
                mw::mech3d::ComponentFrameSequence{0, {22, 23, 24, 25}, 180, "marauder_leg_a"},
                mw::mech3d::ComponentFrameSequence{1, {18, 19, 20, 21}, 180, "marauder_limb_b"},
            },
        };
        require(
            mw::mech3d::animationFrameCount(marauderTwoLimbAnimation) == 4,
            "Marauder multi-sequence animation frame count mismatch");
        for (int frame = 0; frame < 4; ++frame) {
            const mw::mech3d::MechModelDefinition frameDefinition =
                mw::mech3d::applyAnimationFrame(marauderTwoLimbBind, marauderTwoLimbAnimation, frame);
            require(frameDefinition.components.size() == 2u, "Marauder multi-frame definition component count mismatch");
            require(frameDefinition.components[0].sourceRecordIndex == 22 + frame, "Marauder first sequence source mismatch");
            require(frameDefinition.components[1].sourceRecordIndex == 18 + frame, "Marauder second sequence source mismatch");

            const mw::mech3d::MechRenderInstance frameInstance =
                mw::mech3d::buildMechRenderInstance(frameDefinition, marauderRecords, batchOptions);
            require(frameInstance.ranges.size() == 2u, "Marauder multi-frame render range count mismatch");
            require(frameInstance.ranges[0].componentId == 0, "Marauder first range component mismatch");
            require(frameInstance.ranges[1].componentId == 1, "Marauder second range component mismatch");
            require(frameInstance.ranges[0].sourceRecordIndex == 22 + frame, "Marauder first range source mismatch");
            require(frameInstance.ranges[1].sourceRecordIndex == 18 + frame, "Marauder second range source mismatch");
            require(!frameInstance.batch.vertices.empty(), "Marauder multi-frame must contain vertices");
        }

        const mw::mech3d::MechModelDefinition catalogJenner =
            mw::mech3d::makeCatalogMechModelDefinition("jenner");
        const mw::mech3d::ModelAnimationDefinition jennerWalk =
            mw::mech3d::makeCatalogAnimationDefinition("jenner", "walk");
        const mw::mech3d::ModelAnimationDefinition jennerForwardDeathSlotProbe =
            mw::mech3d::makeCatalogAnimationDefinition("jenner", "death_forward_slot_probe");
        const mw::mech3d::ModelAnimationDefinition jennerBackwardDeathSlotProbe =
            mw::mech3d::makeCatalogAnimationDefinition("jenner", "death_backward_slot_probe");
        const mw::mech3d::ModelAssemblyFrameAnimationDefinition jennerDeath =
            mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition("jenner", "death");
        const mw::mech3d::ModelAssemblyFrameAnimationDefinition jennerForwardDeath =
            mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition("jenner", "death_forward");
        const mw::mech3d::ModelAssemblyFrameAnimationDefinition jennerBackwardDeath =
            mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition("jenner", "death_backward");
        require(catalogJenner.debugName == "jenner_model_mapping_v1", "Jenner catalog debug name mismatch");
        require(
            mw::mech3d::sourceRecordIndices(catalogJenner) == std::vector<int>{0, 1, 2, 3, 4, 5},
            "Jenner catalog bind records mismatch");
        require(findDefinitionComponent(catalogJenner, 1).debugLabel == "jenner_left_arm_bind_record_0", "Jenner left arm label mismatch");
        require(findDefinitionComponent(catalogJenner, 5).debugLabel == "jenner_right_arm_bind_record_1", "Jenner right arm label mismatch");
        require(findDefinitionComponent(catalogJenner, 4).debugLabel == "jenner_left_leg_bind_record_2", "Jenner left leg label mismatch");
        require(findDefinitionComponent(catalogJenner, 0).debugLabel == "jenner_right_leg_bind_record_3", "Jenner right leg label mismatch");
        require(jennerWalk.sequences.size() == 6u, "Jenner catalog walk sequence count mismatch");
        require(jennerWalk.sequences[0].componentId == 1, "Jenner catalog left arm component mismatch");
        require(jennerWalk.sequences[0].sourceRecordIndices == std::vector<int>{6, 7, 8, 9, 10, 11, 12, 13}, "Jenner catalog left arm walk sequence mismatch");
        require(jennerWalk.sequences[1].componentId == 5, "Jenner catalog right arm component mismatch");
        require(jennerWalk.sequences[1].sourceRecordIndices == std::vector<int>{14, 15, 16, 17, 18, 19, 20, 21}, "Jenner catalog right arm walk sequence mismatch");
        require(jennerWalk.sequences[2].componentId == 4, "Jenner catalog left leg component mismatch");
        require(jennerWalk.sequences[2].sourceRecordIndices == std::vector<int>{22, 23, 24, 25, 26, 27, 28, 29}, "Jenner catalog left leg walk sequence mismatch");
        require(jennerWalk.sequences[3].componentId == 0, "Jenner catalog right leg component mismatch");
        require(jennerWalk.sequences[3].sourceRecordIndices == std::vector<int>{30, 31, 32, 33, 34, 35, 36, 37}, "Jenner catalog right leg walk sequence mismatch");
        require(mw::mech3d::animationFrameCount(jennerWalk) == 8, "Jenner catalog walk frame count mismatch");
        require(
            mw::mech3d::catalogAnimationKind("jenner", "walk") == mw::mech3d::MechCatalogAnimationKind::ComponentFrames,
            "Jenner walk must be a component-frame animation");
        require(
            mw::mech3d::catalogAnimationKind("jenner", "death_forward_slot_probe") ==
                mw::mech3d::MechCatalogAnimationKind::ComponentFrames,
            "Jenner forward death slot probe must be a component-frame animation");
        require(
            mw::mech3d::catalogAnimationKind("jenner", "death_backward") ==
                mw::mech3d::MechCatalogAnimationKind::AssemblyFrames,
            "Jenner backward death must be an assembly-frame animation");
        require(jennerForwardDeath.frames.size() == 6u, "Jenner forward death frame count mismatch");
        require(jennerDeath.frames.size() == jennerForwardDeath.frames.size(), "Jenner death alias must use forward death");
        require(
            jennerForwardDeath.componentIds ==
                std::vector<int>{1, 5, 4, 0, 2, 3},
            "Jenner forward death must preserve catalog component identities");
        require(jennerForwardDeath.frames[0].sourceRecordIndices == std::vector<int>{59, 65, 71, 77, 83, 89}, "Jenner forward death standing frame records mismatch");
        require(jennerForwardDeath.frames[5].sourceRecordIndices == std::vector<int>{54, 60, 66, 72, 78, 84}, "Jenner forward death settled frame records mismatch");
        require(jennerBackwardDeath.frames.size() == 5u, "Jenner backward death frame count mismatch");
        require(jennerBackwardDeath.frames[0].sourceRecordIndices == std::vector<int>{90, 95, 100, 105, 110, 115}, "Jenner backward death frame 0 records mismatch");
        require(jennerBackwardDeath.frames[4].sourceRecordIndices == std::vector<int>{94, 99, 104, 109, 114, 119}, "Jenner backward death frame 4 records mismatch");
        require(
            mw::mech3d::animationFrameCount(jennerForwardDeathSlotProbe) == 6,
            "Jenner forward death slot probe frame count mismatch");
        require(
            mw::mech3d::animationFrameCount(jennerBackwardDeathSlotProbe) == 5,
            "Jenner backward death slot probe frame count mismatch");
        const mw::mech3d::MechModelDefinition jennerDeathFrame =
            mw::mech3d::applyAssemblyFrameAnimationFrame(
                jennerForwardDeath, 5);
        require(
            mw::mech3d::sourceRecordIndices(jennerDeathFrame) ==
                std::vector<int>{54, 60, 66, 72, 78, 84} &&
                jennerDeathFrame.components[0].componentId == 1 &&
                jennerDeathFrame.components[1].componentId == 5 &&
                jennerDeathFrame.components[2].componentId == 4 &&
                jennerDeathFrame.components[3].componentId == 0 &&
                jennerDeathFrame.components[4].componentId == 2 &&
                jennerDeathFrame.components[5].componentId == 3,
            "Jenner settled death frame lost component-to-record identity");
        mw::mech3d::MechRuntimeState jennerDetachedLegState;
        mw::mech3d::destroyMechRuntimeComponent(
            jennerDetachedLegState, 4);
        mw::mech3d::hideMechRuntimeComponent(
            jennerDetachedLegState, 4);
        const auto resolvedJennerDetachedLeg =
            mw::mech3d::resolveMechRuntimeState(
                jennerDetachedLegState,
                mw::mech3d::catalogComponentDamageRules("jenner"));
        const auto jennerDeathInstance = mw::mech3d::buildMechRenderInstance(
            jennerDeathFrame, records, batchOptions);
        require(
            !mw::mech3d::isResolvedMechComponentVisible(
                jennerDeathInstance.assembly,
                resolvedJennerDetachedLeg,
                4) &&
                mw::mech3d::isResolvedMechComponentVisible(
                    jennerDeathInstance.assembly,
                    resolvedJennerDetachedLeg,
                    2) &&
                mw::mech3d::isResolvedMechComponentVisible(
                    jennerDeathInstance.assembly,
                    resolvedJennerDetachedLeg,
                    3),
            "a detached Jenner leg must not hide torso or cockpit death geometry");
        require(
            jennerForwardDeathSlotProbe.sequences[0].sourceRecordIndices == std::vector<int>{54, 55, 56, 57, 58, 59},
            "Jenner forward left arm death slot records mismatch");
        require(
            jennerBackwardDeathSlotProbe.sequences[5].sourceRecordIndices == std::vector<int>{115, 116, 117, 118, 119},
            "Jenner backward cockpit death slot records mismatch");
        require(
            mw::mech3d::catalogDestroyedActionForComponent("jenner", 0) ==
                mw::mech3d::MechComponentDestroyedAction::DestroyMech,
            "Jenner right leg destruction must kill the mech");
        require(
            mw::mech3d::catalogDestroyedActionForComponent("jenner", 1) ==
                mw::mech3d::MechComponentDestroyedAction::HideComponent,
            "Jenner left arm destruction should not kill the mech");

        const mw::mech3d::MechModelDefinition catalogMarauder =
            mw::mech3d::makeCatalogMechModelDefinition("marauder");
        const mw::mech3d::ModelAnimationDefinition catalogWalk =
            mw::mech3d::makeCatalogAnimationDefinition("marauder", "walk");
        const mw::mech3d::ModelAnimationDefinition catalogDestroyedSlotProbe =
            mw::mech3d::makeCatalogAnimationDefinition("marauder", "destroyed_slot_probe");
        const mw::mech3d::ModelAssemblyFrameAnimationDefinition catalogDestroyed =
            mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition("marauder", "damage_or_destroyed");
        require(catalogMarauder.debugName == "marauder_model_mapping_v1", "Marauder catalog debug name mismatch");
        require(
            mw::mech3d::catalogDestroyedSlotQuality("marauder") ==
                mw::mech3d::MechCatalogDestroyedSlotQuality::AssemblyOnly,
            "Marauder destroyed slots must be marked assembly-only");
        require(
            mw::mech3d::sourceRecordIndices(catalogMarauder) == std::vector<int>{0, 1, 2, 3, 4, 5},
            "Marauder catalog bind records mismatch");
        require(findDefinitionComponent(catalogMarauder, 1).debugLabel == "marauder_right_arm_bind_record_0", "Marauder right arm label mismatch");
        require(findDefinitionComponent(catalogMarauder, 5).debugLabel == "marauder_left_arm_bind_record_1", "Marauder left arm label mismatch");
        require(findDefinitionComponent(catalogMarauder, 4).debugLabel == "marauder_right_leg_bind_record_2", "Marauder right leg label mismatch");
        require(findDefinitionComponent(catalogMarauder, 0).debugLabel == "marauder_left_leg_bind_record_3", "Marauder left leg label mismatch");
        require(
            sameVec3(findDefinitionComponent(catalogMarauder, 1).localPivot, mw::mech3d::Vec3f{-194.5f, 112.5f, 24.0f}),
            "Marauder right arm local pivot mismatch");
        require(
            sameVec3(findDefinitionComponent(catalogMarauder, 5).localPivot, mw::mech3d::Vec3f{194.5f, 112.5f, 7.5f}),
            "Marauder left arm local pivot mismatch");
        require(
            sameVec3(findDefinitionComponent(catalogMarauder, 4).localPivot, mw::mech3d::Vec3f{-134.0f, -111.0f, 77.5f}),
            "Marauder right leg local pivot mismatch");
        require(
            sameVec3(findDefinitionComponent(catalogMarauder, 0).localPivot, mw::mech3d::Vec3f{133.5f, -124.0f, 77.5f}),
            "Marauder left leg local pivot mismatch");
        require(
            findDefinitionComponent(catalogMarauder, 1).defaultPoseAxis == mw::mech3d::ComponentPoseAxis::Y,
            "Marauder right arm default pose axis mismatch");
        require(
            findDefinitionComponent(catalogMarauder, 4).defaultPoseAxis == mw::mech3d::ComponentPoseAxis::X,
            "Marauder right leg default pose axis mismatch");
        require(
            findDefinitionComponent(catalogMarauder, 2).defaultPoseAxis == mw::mech3d::ComponentPoseAxis::Y,
            "Marauder torso default pose axis mismatch");
        require(catalogWalk.sequences.size() == 6u, "Marauder catalog walk sequence count mismatch");
        require(catalogWalk.sequences[0].componentId == 1, "Marauder catalog right arm component mismatch");
        require(catalogWalk.sequences[0].sourceRecordIndices == std::vector<int>{6, 7, 8, 9, 10, 11, 12, 13}, "Marauder catalog right arm walk sequence mismatch");
        require(catalogWalk.sequences[1].componentId == 5, "Marauder catalog left arm component mismatch");
        require(catalogWalk.sequences[1].sourceRecordIndices == std::vector<int>{14, 15, 16, 17, 18, 19, 20, 21}, "Marauder catalog left arm walk sequence mismatch");
        require(catalogWalk.sequences[2].componentId == 4, "Marauder catalog right leg component mismatch");
        require(catalogWalk.sequences[2].sourceRecordIndices == std::vector<int>{22, 23, 24, 25, 26, 27, 28, 29}, "Marauder catalog right leg walk sequence mismatch");
        require(catalogWalk.sequences[3].componentId == 0, "Marauder catalog left leg component mismatch");
        require(catalogWalk.sequences[3].sourceRecordIndices == std::vector<int>{30, 31, 32, 33, 34, 35, 36, 37}, "Marauder catalog left leg walk sequence mismatch");
        require(catalogWalk.sequences[4].componentId == 2, "Marauder catalog torso component mismatch");
        require(catalogWalk.sequences[4].sourceRecordIndices == std::vector<int>{38, 39, 40, 41, 42, 43, 44, 45}, "Marauder catalog torso walk sequence mismatch");
        require(catalogWalk.sequences[5].componentId == 3, "Marauder catalog cockpit component mismatch");
        require(catalogWalk.sequences[5].sourceRecordIndices == std::vector<int>{46, 47, 48, 49, 50, 51, 52, 53}, "Marauder catalog cockpit walk sequence mismatch");
        require(mw::mech3d::animationFrameCount(catalogWalk) == 8, "Marauder catalog walk frame count mismatch");
        require(
            mw::mech3d::catalogAnimationKind("marauder", "walk") == mw::mech3d::MechCatalogAnimationKind::ComponentFrames,
            "Marauder walk must be a component-frame animation");
        require(
            mw::mech3d::catalogAnimationKind("marauder", "destroyed_slot_probe") ==
                mw::mech3d::MechCatalogAnimationKind::ComponentFrames,
            "Marauder destroyed slot probe must be a component-frame animation");
        require(
            mw::mech3d::catalogAnimationKind("marauder", "death") == mw::mech3d::MechCatalogAnimationKind::AssemblyFrames,
            "Marauder death must be an assembly-frame animation");
        require(catalogDestroyedSlotProbe.sequences.size() == 6u, "Marauder destroyed slot probe sequence count mismatch");
        require(
            mw::mech3d::animationFrameCount(catalogDestroyedSlotProbe) == 5,
            "Marauder destroyed slot probe frame count mismatch");
        require(
            catalogDestroyedSlotProbe.sequences[0].sourceRecordIndices == std::vector<int>{54, 55, 56, 57, 58},
            "Marauder right arm destroyed slot records mismatch");
        require(
            catalogDestroyedSlotProbe.sequences[5].sourceRecordIndices == std::vector<int>{79, 80, 81, 82, 83},
            "Marauder cockpit destroyed slot records mismatch");
        require(catalogDestroyed.frames.size() == 5u, "Marauder catalog destroyed frame count mismatch");
        require(catalogDestroyed.frames[0].sourceRecordIndices == std::vector<int>{54, 59, 64, 69, 74, 79}, "Marauder destroyed frame 0 records mismatch");
        require(catalogDestroyed.frames[1].sourceRecordIndices == std::vector<int>{55, 60, 65, 70, 75, 80}, "Marauder destroyed frame 1 records mismatch");
        require(catalogDestroyed.frames[4].sourceRecordIndices == std::vector<int>{58, 63, 68, 73, 78, 83}, "Marauder destroyed frame 4 records mismatch");
        require(
            mw::mech3d::assemblyFrameAnimationFrameCount(catalogDestroyed) == 5,
            "Marauder destroyed assembly-frame count mismatch");
        const mw::mech3d::MechModelDefinition destroyedFrame0 =
            mw::mech3d::applyAssemblyFrameAnimationFrame(catalogDestroyed, 0);
        require(
            mw::mech3d::sourceRecordIndices(destroyedFrame0) == std::vector<int>{54, 59, 64, 69, 74, 79},
            "Marauder destroyed frame 0 definition records mismatch");
        require(
            mw::mech3d::catalogDestroyedActionForComponent("marauder", 4) ==
                mw::mech3d::MechComponentDestroyedAction::DestroyMech,
            "Marauder right leg destruction must kill the mech");
        require(
            mw::mech3d::catalogDestroyedActionForComponent("marauder", 0) ==
                mw::mech3d::MechComponentDestroyedAction::DestroyMech,
            "Marauder left leg destruction must kill the mech");
        require(
            mw::mech3d::catalogDestroyedActionForComponent("marauder", 1) ==
                mw::mech3d::MechComponentDestroyedAction::HideComponent,
            "Marauder right arm destruction should not kill the mech");
        require(
            mw::mech3d::catalogDestroyedActionForComponent("marauder", 5) ==
                mw::mech3d::MechComponentDestroyedAction::HideComponent,
            "Marauder left arm destruction should not kill the mech");

        const mw::mech3d::MechRenderInstance catalogMarauderInstance =
            mw::mech3d::buildMechRenderInstance(catalogMarauder, marauderRecords, batchOptions);
        mw::mech3d::MechComponentVisibility hiddenRightArm;
        hiddenRightArm.hiddenComponentIds = {1};
        require(
            !mw::mech3d::isComponentVisible(catalogMarauderInstance.assembly, hiddenRightArm, 1),
            "hidden right arm should be invisible");
        require(
            mw::mech3d::isComponentVisible(catalogMarauderInstance.assembly, hiddenRightArm, 2),
            "hidden right arm must not hide torso");
        mw::mech3d::MechComponentVisibility hiddenTorso;
        hiddenTorso.hiddenComponentIds = {2};
        require(
            !mw::mech3d::isComponentVisible(catalogMarauderInstance.assembly, hiddenTorso, 3),
            "hidden torso should hide cockpit descendant");

        mw::mech3d::MechRuntimeState runtimeState;
        mw::mech3d::setMechRuntimeAnimation(runtimeState, "walk");
        mw::mech3d::destroyMechRuntimeComponent(runtimeState, 1);
        const mw::mech3d::ResolvedMechRuntimeState resolvedArmDamage =
            mw::mech3d::resolveMechRuntimeState(runtimeState, mw::mech3d::catalogComponentDamageRules("marauder"));
        require(!resolvedArmDamage.mechDestroyed, "destroyed Marauder arm must not kill the mech");
        require(resolvedArmDamage.activeAnimationId == "walk", "destroyed Marauder arm must keep current animation");
        require(
            !mw::mech3d::isResolvedMechComponentVisible(catalogMarauderInstance.assembly, resolvedArmDamage, 1),
            "destroyed Marauder arm must resolve to hidden");
        require(
            mw::mech3d::isResolvedMechComponentVisible(catalogMarauderInstance.assembly, resolvedArmDamage, 2),
            "destroyed Marauder arm must not hide torso");

        mw::mech3d::destroyMechRuntimeComponent(runtimeState, 4);
        const mw::mech3d::ResolvedMechRuntimeState resolvedLegDamage =
            mw::mech3d::resolveMechRuntimeState(runtimeState, mw::mech3d::catalogComponentDamageRules("marauder"));
        require(resolvedLegDamage.mechDestroyed, "destroyed Marauder leg must kill the mech");
        require(resolvedLegDamage.activeAnimationId == "death", "destroyed Marauder leg must switch to death animation");
        require(resolvedLegDamage.usesAssemblyFrameDeath, "Marauder death must use assembly-frame rendering");

        mw::mech3d::MechRuntimeState debugHiddenState;
        mw::mech3d::hideMechRuntimeComponent(debugHiddenState, 2);
        const mw::mech3d::ResolvedMechRuntimeState resolvedDebugHidden =
            mw::mech3d::resolveMechRuntimeState(debugHiddenState, mw::mech3d::catalogComponentDamageRules("marauder"));
        const std::vector<mw::mech3d::ResolvedMechComponentState> componentStates =
            mw::mech3d::resolveMechComponentStates(catalogMarauderInstance.assembly, resolvedDebugHidden);
        require(componentStates.size() == catalogMarauderInstance.assembly.components.size(), "resolved component state count mismatch");
        require(
            !mw::mech3d::isResolvedMechComponentVisible(catalogMarauderInstance.assembly, resolvedDebugHidden, 3),
            "runtime hidden torso must hide cockpit descendant");

        const mw::mech3d::MechModelDefinition catalogLocust =
            mw::mech3d::makeCatalogMechModelDefinition("locust");
        const mw::mech3d::ModelAnimationDefinition locustWalk =
            mw::mech3d::makeCatalogAnimationDefinition("locust", "walk");
        const mw::mech3d::ModelAnimationDefinition locustDestroyedSlotProbe =
            mw::mech3d::makeCatalogAnimationDefinition("locust", "destroyed_slot_probe");
        const mw::mech3d::ModelAssemblyFrameAnimationDefinition locustDestroyed =
            mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition("locust", "damage_or_destroyed");
        require(catalogLocust.debugName == "locust_model_mapping_v1", "Locust catalog debug name mismatch");
        require(
            mw::mech3d::catalogDestroyedSlotQuality("locust") ==
                mw::mech3d::MechCatalogDestroyedSlotQuality::ComponentClean,
            "Locust destroyed slots must be marked component-clean");
        require(
            mw::mech3d::sourceRecordIndices(catalogLocust) == std::vector<int>{0, 1, 2, 3, 4, 5},
            "Locust catalog bind records mismatch");
        require(findDefinitionComponent(catalogLocust, 1).debugLabel == "locust_right_arm_bind_record_0", "Locust right arm label mismatch");
        require(findDefinitionComponent(catalogLocust, 5).debugLabel == "locust_left_arm_bind_record_1", "Locust left arm label mismatch");
        require(findDefinitionComponent(catalogLocust, 4).debugLabel == "locust_right_leg_bind_record_2", "Locust right leg label mismatch");
        require(findDefinitionComponent(catalogLocust, 0).debugLabel == "locust_left_leg_bind_record_3", "Locust left leg label mismatch");
        require(
            sameVec3(findDefinitionComponent(catalogLocust, 1).localPivot, mw::mech3d::Vec3f{-141.0f, 240.0f, -3.5f}),
            "Locust right arm local pivot mismatch");
        require(
            sameVec3(findDefinitionComponent(catalogLocust, 5).localPivot, mw::mech3d::Vec3f{138.0f, 240.0f, -27.5f}),
            "Locust left arm local pivot mismatch");
        require(
            sameVec3(findDefinitionComponent(catalogLocust, 4).localPivot, mw::mech3d::Vec3f{-101.0f, -53.5f, -38.0f}),
            "Locust right leg local pivot mismatch");
        require(
            sameVec3(findDefinitionComponent(catalogLocust, 0).localPivot, mw::mech3d::Vec3f{100.0f, -75.0f, 15.0f}),
            "Locust left leg local pivot mismatch");
        require(
            findDefinitionComponent(catalogLocust, 1).defaultPoseAxis == mw::mech3d::ComponentPoseAxis::Y,
            "Locust right arm default pose axis mismatch");
        require(
            findDefinitionComponent(catalogLocust, 4).defaultPoseAxis == mw::mech3d::ComponentPoseAxis::X,
            "Locust right leg default pose axis mismatch");
        require(
            findDefinitionComponent(catalogLocust, 2).defaultPoseAxis == mw::mech3d::ComponentPoseAxis::Y,
            "Locust torso default pose axis mismatch");
        require(locustWalk.sequences.size() == 6u, "Locust catalog walk sequence count mismatch");
        require(locustWalk.sequences[0].componentId == 1, "Locust catalog right arm component mismatch");
        require(locustWalk.sequences[0].sourceRecordIndices == std::vector<int>{6, 7, 8, 9, 10, 11, 12, 13}, "Locust catalog right arm walk sequence mismatch");
        require(locustWalk.sequences[1].componentId == 5, "Locust catalog left arm component mismatch");
        require(locustWalk.sequences[1].sourceRecordIndices == std::vector<int>{14, 15, 16, 17, 18, 19, 20, 21}, "Locust catalog left arm walk sequence mismatch");
        require(locustWalk.sequences[2].componentId == 4, "Locust catalog right leg component mismatch");
        require(locustWalk.sequences[2].sourceRecordIndices == std::vector<int>{22, 23, 24, 25, 26, 27, 28, 29}, "Locust catalog right leg walk sequence mismatch");
        require(locustWalk.sequences[3].componentId == 0, "Locust catalog left leg component mismatch");
        require(locustWalk.sequences[3].sourceRecordIndices == std::vector<int>{30, 31, 32, 33, 34, 35, 36, 37}, "Locust catalog left leg walk sequence mismatch");
        require(mw::mech3d::animationFrameCount(locustWalk) == 8, "Locust catalog walk frame count mismatch");
        require(
            mw::mech3d::catalogAnimationKind("locust", "walk") == mw::mech3d::MechCatalogAnimationKind::ComponentFrames,
            "Locust walk must be a component-frame animation");
        require(
            mw::mech3d::catalogAnimationKind("locust", "destroyed_slot_probe") ==
                mw::mech3d::MechCatalogAnimationKind::ComponentFrames,
            "Locust destroyed slot probe must be a component-frame animation");
        require(
            mw::mech3d::catalogAnimationKind("locust", "death") == mw::mech3d::MechCatalogAnimationKind::AssemblyFrames,
            "Locust death must be an assembly-frame animation");
        require(locustDestroyedSlotProbe.sequences.size() == 6u, "Locust destroyed slot probe sequence count mismatch");
        require(
            mw::mech3d::animationFrameCount(locustDestroyedSlotProbe) == 5,
            "Locust destroyed slot probe frame count mismatch");
        require(
            locustDestroyedSlotProbe.sequences[0].sourceRecordIndices == std::vector<int>{54, 55, 56, 57, 58},
            "Locust right arm destroyed slot records mismatch");
        require(
            locustDestroyedSlotProbe.sequences[5].sourceRecordIndices == std::vector<int>{79, 80, 81, 82, 83},
            "Locust cockpit destroyed slot records mismatch");
        require(locustDestroyed.frames.size() == 5u, "Locust destroyed frame count mismatch");
        require(locustDestroyed.frames[0].sourceRecordIndices == std::vector<int>{54, 59, 64, 69, 74, 79}, "Locust destroyed frame 0 records mismatch");
        require(locustDestroyed.frames[4].sourceRecordIndices == std::vector<int>{58, 63, 68, 73, 78, 83}, "Locust destroyed frame 4 records mismatch");
        const mw::mech3d::MechRenderInstance catalogLocustInstance =
            mw::mech3d::buildMechRenderInstance(catalogLocust, locustRecords, batchOptions);
        require(catalogLocustInstance.assembly.components.size() == 6u, "Locust render instance component count mismatch");
        require(!catalogLocustInstance.batch.vertices.empty(), "Locust render instance must contain vertices");
        mw::mech3d::MechRuntimeState locustRuntimeState;
        mw::mech3d::setMechRuntimeAnimation(locustRuntimeState, "walk");
        mw::mech3d::destroyMechRuntimeComponent(locustRuntimeState, 5);
        const mw::mech3d::ResolvedMechRuntimeState locustArmDamage =
            mw::mech3d::resolveMechRuntimeState(locustRuntimeState, mw::mech3d::catalogComponentDamageRules("locust"));
        require(!locustArmDamage.mechDestroyed, "destroyed Locust arm must not kill the mech");
        require(
            !mw::mech3d::isResolvedMechComponentVisible(catalogLocustInstance.assembly, locustArmDamage, 5),
            "destroyed Locust arm must resolve to hidden");
        mw::mech3d::destroyMechRuntimeComponent(locustRuntimeState, 4);
        const mw::mech3d::ResolvedMechRuntimeState locustLegDamage =
            mw::mech3d::resolveMechRuntimeState(locustRuntimeState, mw::mech3d::catalogComponentDamageRules("locust"));
        require(locustLegDamage.mechDestroyed, "destroyed Locust leg must kill the mech");
        require(locustLegDamage.activeAnimationId == "death", "destroyed Locust leg must switch to death animation");

        const std::array<std::pair<const char*, float>, 8> expectedCockpitHeights{{
            {"battlemaster", 950.0f},
            {"warhammer", 708.0f},
            {"marauder", 674.5f},
            {"locust", 380.0f},
            {"rifleman", 480.0f},
            {"jenner", 450.0f},
            {"phoenix_hawk", 565.0f},
            {"shadow_hawk", 570.0f},
        }};
        size_t playerTorsoYawPresetCount = 0;
        size_t catalogCockpitHeightPresetCount = 0;
        std::ostringstream catalogCockpitHeightDiagnostics;
        catalogCockpitHeightDiagnostics << std::fixed << std::setprecision(1);
        for (const mw::mech3d::MechCatalogEntry& entry : catalogEntries) {
            const std::filesystem::path presetResourcePath =
                resourcePath.parent_path() / entry.defaultResourcePath.filename();
            const std::vector<mw::legacy3d::RuntimeRecord> presetRecords =
                mw::legacy3d::loadRuntimeShapeRecords(presetResourcePath);
            const mw::mech3d::MechModelDefinition presetDefinition =
                mw::mech3d::makeCatalogMechModelDefinition(entry.presetId);
            const mw::mech3d::MechRenderInstance bindInstance =
                mw::mech3d::buildMechRenderInstance(presetDefinition, presetRecords, batchOptions);
            const auto deathAnimation =
                mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition(
                    entry.presetId, "death");
            std::vector<int> expectedDeathComponentIds;
            expectedDeathComponentIds.reserve(
                presetDefinition.components.size());
            for (const auto& component : presetDefinition.components) {
                expectedDeathComponentIds.push_back(component.componentId);
            }
            require(
                deathAnimation.componentIds == expectedDeathComponentIds,
                "catalog death animation lost component identities: " +
                    entry.presetId);
            const auto finalDeathDefinition =
                mw::mech3d::applyAssemblyFrameAnimationFrame(
                    deathAnimation,
                    mw::mech3d::assemblyFrameAnimationFrameCount(
                        deathAnimation) - 1);
            for (size_t componentIndex = 0;
                 componentIndex < finalDeathDefinition.components.size();
                 ++componentIndex) {
                require(
                    finalDeathDefinition.components[componentIndex].
                            componentId ==
                        expectedDeathComponentIds[componentIndex],
                    "catalog final death frame remapped a component: " +
                        entry.presetId);
            }

            const int cockpitComponentId = mw::mech3d::catalogCockpitComponentId(entry.presetId);
            require(cockpitComponentId == 3, "catalog cockpit component id mismatch");
            require(
                findDefinitionComponent(presetDefinition, cockpitComponentId).debugLabel.find("_cockpit_") !=
                    std::string::npos,
                "catalog cockpit component label mismatch");
            const auto expectedCockpitHeight = std::find_if(
                expectedCockpitHeights.begin(),
                expectedCockpitHeights.end(),
                [&entry](const std::pair<const char*, float>& expected) {
                    return entry.presetId == expected.first;
                });
            require(expectedCockpitHeight != expectedCockpitHeights.end(), "missing expected cockpit height");
            const float cockpitHeight =
                mw::mech3d::catalogCockpitCameraHeight(entry.presetId, presetRecords, batchOptions);
            require(
                std::abs(cockpitHeight - expectedCockpitHeight->second) < 0.001f,
                "catalog cockpit height mismatch: " + entry.presetId);
            if (catalogCockpitHeightPresetCount > 0) {
                catalogCockpitHeightDiagnostics << ',';
            }
            catalogCockpitHeightDiagnostics << entry.presetId << ':' << cockpitHeight;
            ++catalogCockpitHeightPresetCount;

            mw::mech3d::MechPose torsoYawPose;
            mw::mech3d::applyPlayerTorsoYawPose(torsoYawPose, bindInstance.assembly, 0.7330383f);
            require(!torsoYawPose.componentLocalOverrides.empty(), "player torso yaw pose must add overrides");

            mw::mech3d::MechRenderInstance torsoYawInstance = bindInstance;
            mw::mech3d::updateMechRenderInstancePose(torsoYawInstance, torsoYawPose);
            require(torsoYawInstance.batch.vertices == bindInstance.batch.vertices, "player torso yaw must not rewrite vertices");
            require(torsoYawInstance.batch.triangleIndices == bindInstance.batch.triangleIndices, "player torso yaw must not rewrite triangles");
            require(torsoYawInstance.batch.lineIndices == bindInstance.batch.lineIndices, "player torso yaw must not rewrite lines");
            require(
                matrixRotationYSign(findAssemblyComponent(torsoYawInstance.assembly, 2).worldMatrix) > 0.6f,
                "player torso yaw must rotate torso");
            require(
                matrixRotationYSign(findAssemblyComponent(torsoYawInstance.assembly, 3).worldMatrix) > 0.6f,
                "player torso yaw must rotate cockpit with torso");
            require(
                nearZero(matrixRotationYSign(findAssemblyComponent(torsoYawInstance.assembly, 0).worldMatrix)),
                "player torso yaw must keep first leg on body heading");
            require(
                nearZero(matrixRotationYSign(findAssemblyComponent(torsoYawInstance.assembly, 4).worldMatrix)),
                "player torso yaw must keep second leg on body heading");
            ++playerTorsoYawPresetCount;
        }

        std::cout
            << "mw_model_assembly_smoke: ok"
            << " resource=" << resourcePath.string()
            << " components=" << instance.assembly.components.size()
            << " marauder_leg_frames=" << mw::mech3d::animationFrameCount(marauderLegAnimation)
            << " marauder_multi_frames=" << mw::mech3d::animationFrameCount(marauderTwoLimbAnimation)
            << " marauder_catalog_walk_frames=" << mw::mech3d::animationFrameCount(catalogWalk)
            << " marauder_catalog_destroyed_slot_probe_frames=" << mw::mech3d::animationFrameCount(catalogDestroyedSlotProbe)
            << " marauder_catalog_destroyed_frames=" << mw::mech3d::assemblyFrameAnimationFrameCount(catalogDestroyed)
            << " locust_catalog_walk_frames=" << mw::mech3d::animationFrameCount(locustWalk)
            << " locust_catalog_destroyed_slot_probe_frames=" << mw::mech3d::animationFrameCount(locustDestroyedSlotProbe)
            << " locust_catalog_destroyed_frames=" << mw::mech3d::assemblyFrameAnimationFrameCount(locustDestroyed)
            << " player_torso_yaw_presets=" << playerTorsoYawPresetCount
            << " catalog_cockpit_height_presets=" << catalogCockpitHeightPresetCount
            << " catalog_cockpit_heights=" << catalogCockpitHeightDiagnostics.str()
            << " runtime_state=ok"
            << " gpu_vertices=" << (instance.batch.vertices.size() / 6u)
            << " gpu_triangles=" << (instance.batch.triangleIndices.size() / 3u)
            << " gpu_line_pairs=" << (instance.batch.lineIndices.size() / 2u)
            << "\n";
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "mw_model_assembly_smoke: " << exc.what() << "\n";
        return 1;
    }
}
