#include "mech3d/model_assembly.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace mw::mech3d {
namespace {

const legacy3d::RuntimeRecord& selectRecord(const std::vector<legacy3d::RuntimeRecord>& records, int recordIndex) {
    for (const legacy3d::RuntimeRecord& record : records) {
        if (record.recordIndex == recordIndex) {
            return record;
        }
    }
    throw std::runtime_error("assembly source record not found: " + std::to_string(recordIndex));
}

Vec3f transformPoint(const Mat4f& matrix, Vec3f point) {
    return Vec3f{
        matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
        matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14],
    };
}

Mat4f multiply(Mat4f a, Mat4f b) {
    Mat4f result{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) {
                value += a[static_cast<size_t>(k * 4 + row)] * b[static_cast<size_t>(col * 4 + k)];
            }
            result[static_cast<size_t>(col * 4 + row)] = value;
        }
    }
    return result;
}

Bounds boundsFromGpuBatch(const legacy3d::GpuBatch& batch) {
    Bounds bounds;
    if (batch.vertices.empty()) {
        return bounds;
    }

    bounds.valid = true;
    bounds.min = Vec3f{batch.vertices[0], batch.vertices[1], batch.vertices[2]};
    bounds.max = bounds.min;
    for (size_t i = 0; i + 5 < batch.vertices.size(); i += 6) {
        const Vec3f point{batch.vertices[i], batch.vertices[i + 1], batch.vertices[i + 2]};
        bounds.min.x = std::min(bounds.min.x, point.x);
        bounds.min.y = std::min(bounds.min.y, point.y);
        bounds.min.z = std::min(bounds.min.z, point.z);
        bounds.max.x = std::max(bounds.max.x, point.x);
        bounds.max.y = std::max(bounds.max.y, point.y);
        bounds.max.z = std::max(bounds.max.z, point.z);
    }

    bounds.center = Vec3f{
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f,
    };
    float radius = 1.0f;
    for (size_t i = 0; i + 5 < batch.vertices.size(); i += 6) {
        const float dx = batch.vertices[i] - bounds.center.x;
        const float dy = batch.vertices[i + 1] - bounds.center.y;
        const float dz = batch.vertices[i + 2] - bounds.center.z;
        radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    bounds.radius = radius;
    return bounds;
}

Bounds transformBounds(const Bounds& source, const Mat4f& matrix) {
    if (!source.valid) {
        return source;
    }

    Bounds result;
    result.valid = true;
    result.min = Vec3f{
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
        std::numeric_limits<float>::max(),
    };
    result.max = Vec3f{
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
        std::numeric_limits<float>::lowest(),
    };

    const Vec3f corners[] = {
        {source.min.x, source.min.y, source.min.z},
        {source.max.x, source.min.y, source.min.z},
        {source.min.x, source.max.y, source.min.z},
        {source.max.x, source.max.y, source.min.z},
        {source.min.x, source.min.y, source.max.z},
        {source.max.x, source.min.y, source.max.z},
        {source.min.x, source.max.y, source.max.z},
        {source.max.x, source.max.y, source.max.z},
    };
    for (Vec3f corner : corners) {
        const Vec3f point = transformPoint(matrix, corner);
        result.min.x = std::min(result.min.x, point.x);
        result.min.y = std::min(result.min.y, point.y);
        result.min.z = std::min(result.min.z, point.z);
        result.max.x = std::max(result.max.x, point.x);
        result.max.y = std::max(result.max.y, point.y);
        result.max.z = std::max(result.max.z, point.z);
    }

    result.center = Vec3f{
        (result.min.x + result.max.x) * 0.5f,
        (result.min.y + result.max.y) * 0.5f,
        (result.min.z + result.max.z) * 0.5f,
    };
    float radius = 1.0f;
    for (Vec3f corner : corners) {
        const Vec3f point = transformPoint(matrix, corner);
        const float dx = point.x - result.center.x;
        const float dy = point.y - result.center.y;
        const float dz = point.z - result.center.z;
        radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    result.radius = radius;
    return result;
}

Bounds combineBounds(const std::vector<ModelAssemblyComponent>& components) {
    Bounds result;
    for (const ModelAssemblyComponent& component : components) {
        if (!component.worldBounds.valid) {
            continue;
        }
        if (!result.valid) {
            result = component.worldBounds;
            continue;
        }
        result.min.x = std::min(result.min.x, component.worldBounds.min.x);
        result.min.y = std::min(result.min.y, component.worldBounds.min.y);
        result.min.z = std::min(result.min.z, component.worldBounds.min.z);
        result.max.x = std::max(result.max.x, component.worldBounds.max.x);
        result.max.y = std::max(result.max.y, component.worldBounds.max.y);
        result.max.z = std::max(result.max.z, component.worldBounds.max.z);
    }

    if (!result.valid) {
        return result;
    }

    result.center = Vec3f{
        (result.min.x + result.max.x) * 0.5f,
        (result.min.y + result.max.y) * 0.5f,
        (result.min.z + result.max.z) * 0.5f,
    };
    float radius = 1.0f;
    for (const ModelAssemblyComponent& component : components) {
        if (!component.worldBounds.valid) {
            continue;
        }
        const Vec3f corners[] = {
            {component.worldBounds.min.x, component.worldBounds.min.y, component.worldBounds.min.z},
            {component.worldBounds.max.x, component.worldBounds.min.y, component.worldBounds.min.z},
            {component.worldBounds.min.x, component.worldBounds.max.y, component.worldBounds.min.z},
            {component.worldBounds.max.x, component.worldBounds.max.y, component.worldBounds.min.z},
            {component.worldBounds.min.x, component.worldBounds.min.y, component.worldBounds.max.z},
            {component.worldBounds.max.x, component.worldBounds.min.y, component.worldBounds.max.z},
            {component.worldBounds.min.x, component.worldBounds.max.y, component.worldBounds.max.z},
            {component.worldBounds.max.x, component.worldBounds.max.y, component.worldBounds.max.z},
        };
        for (Vec3f corner : corners) {
            const float dx = corner.x - result.center.x;
            const float dy = corner.y - result.center.y;
            const float dz = corner.z - result.center.z;
            radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    }
    result.radius = radius;
    return result;
}

Mat4f makeIdentityMatrix() {
    Mat4f matrix{};
    matrix[0] = 1.0f;
    matrix[5] = 1.0f;
    matrix[10] = 1.0f;
    matrix[15] = 1.0f;
    return matrix;
}

void appendBindBatch(
    legacy3d::GpuBatch& target,
    std::vector<ComponentRenderRange>& ranges,
    const legacy3d::GpuBatch& source,
    const ModelAssemblyComponent& component,
    int groupOffset,
    int groupStride) {
    ComponentRenderRange range;
    range.componentId = component.componentId;
    range.sourceRecordIndex = component.sourceRecordIndex;
    range.vertexOffset = static_cast<uint32_t>(target.vertices.size() / 6u);
    range.triangleIndexOffset = static_cast<uint32_t>(target.triangleIndices.size());
    range.lineIndexOffset = static_cast<uint32_t>(target.lineIndices.size());

    for (size_t i = 0; i + 5 < source.vertices.size(); i += 6) {
        target.vertices.push_back(source.vertices[i]);
        target.vertices.push_back(source.vertices[i + 1]);
        target.vertices.push_back(source.vertices[i + 2]);
        target.vertices.push_back(source.vertices[i + 3]);
        target.vertices.push_back(source.vertices[i + 4]);
        target.vertices.push_back(source.vertices[i + 5]);
    }

    for (uint32_t index : source.triangleIndices) {
        target.triangleIndices.push_back(range.vertexOffset + index);
    }
    for (uint32_t index : source.lineIndices) {
        target.lineIndices.push_back(range.vertexOffset + index);
    }

    range.vertexCount = static_cast<uint32_t>(source.vertices.size() / 6u);
    range.triangleIndexCount = static_cast<uint32_t>(source.triangleIndices.size());
    range.lineIndexCount = static_cast<uint32_t>(source.lineIndices.size());
    ranges.push_back(range);

    for (const auto& entry : source.groupMatrices) {
        target.groupMatrices.emplace(groupOffset + entry.first, entry.second);
    }
    if (source.groupMatrices.empty()) {
        target.groupMatrices.emplace(groupOffset, makeIdentityMatrix());
    }

    for (int group = 0; group < groupStride; ++group) {
        target.groupMatrices.try_emplace(groupOffset + group, makeIdentityMatrix());
    }
}

bool labelContainsToken(std::string label, const std::string& token) {
    std::transform(
        label.begin(),
        label.end(),
        label.begin(),
        [](unsigned char value) {
            return static_cast<char>(std::tolower(value));
        });
    return label.find(token) != std::string::npos;
}

bool isTorsoComponent(const ModelAssemblyComponent& component) {
    return labelContainsToken(component.debugLabel, "torso");
}

bool isLegComponent(const ModelAssemblyComponent& component) {
    return labelContainsToken(component.debugLabel, "leg");
}

} // namespace

Mat4f identityMatrix() {
    return makeIdentityMatrix();
}

Mat4f translationMatrix(Vec3f offset) {
    Mat4f matrix = identityMatrix();
    matrix[12] = offset.x;
    matrix[13] = offset.y;
    matrix[14] = offset.z;
    return matrix;
}

Mat4f rotationXMatrix(float radians) {
    Mat4f matrix = identityMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix[5] = c;
    matrix[6] = s;
    matrix[9] = -s;
    matrix[10] = c;
    return matrix;
}

Mat4f rotationYMatrix(float radians) {
    Mat4f matrix = identityMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix[0] = c;
    matrix[2] = -s;
    matrix[8] = s;
    matrix[10] = c;
    return matrix;
}

Mat4f rotationZMatrix(float radians) {
    Mat4f matrix = identityMatrix();
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    matrix[0] = c;
    matrix[1] = s;
    matrix[4] = -s;
    matrix[5] = c;
    return matrix;
}

Mat4f multiplyMatrix(Mat4f a, Mat4f b) {
    return multiply(a, b);
}

Mat4f rotateAroundPivotMatrix(Vec3f pivot, Mat4f rotation) {
    return multiply(translationMatrix(pivot), multiply(rotation, translationMatrix(Vec3f{-pivot.x, -pivot.y, -pivot.z})));
}

Mat4f rotateAroundPivotXMatrix(Vec3f pivot, float radians) {
    return rotateAroundPivotMatrix(pivot, rotationXMatrix(radians));
}

Mat4f rotateAroundPivotYMatrix(Vec3f pivot, float radians) {
    return rotateAroundPivotMatrix(pivot, rotationYMatrix(radians));
}

Mat4f rotateAroundPivotZMatrix(Vec3f pivot, float radians) {
    return rotateAroundPivotMatrix(pivot, rotationZMatrix(radians));
}

void setComponentPoseLocalMatrix(MechPose& pose, int componentId, Mat4f localMatrix) {
    for (ComponentPoseOverride& entry : pose.componentLocalOverrides) {
        if (entry.componentId == componentId) {
            entry.localMatrix = localMatrix;
            return;
        }
    }
    pose.componentLocalOverrides.push_back(ComponentPoseOverride{componentId, localMatrix});
}

void applyPlayerTorsoYawPose(MechPose& pose, const ModelAssembly& assembly, float yawRadians) {
    if (assembly.components.empty() || std::abs(yawRadians) <= 0.0001f) {
        return;
    }

    const ModelAssemblyComponent* torso = nullptr;
    for (const ModelAssemblyComponent& component : assembly.components) {
        if (isTorsoComponent(component)) {
            torso = &component;
            break;
        }
    }
    if (torso == nullptr) {
        return;
    }

    setComponentPoseLocalMatrix(
        pose,
        torso->componentId,
        rotateAroundPivotYMatrix(torso->localPivot, yawRadians));

    const Mat4f legCounterRotation = rotateAroundPivotYMatrix(torso->localPivot, -yawRadians);
    for (const ModelAssemblyComponent& component : assembly.components) {
        if (component.parentComponentId == torso->componentId && isLegComponent(component)) {
            setComponentPoseLocalMatrix(pose, component.componentId, legCounterRotation);
        }
    }
}

MechModelDefinition makeRecordListModelDefinition(const std::vector<int>& recordIndices, const std::string& debugName) {
    if (recordIndices.empty()) {
        throw std::runtime_error("mech model definition requires at least one source record");
    }

    MechModelDefinition definition;
    definition.debugName = debugName;
    definition.components.reserve(recordIndices.size());
    for (size_t i = 0; i < recordIndices.size(); ++i) {
        MechComponentDefinition component;
        component.componentId = static_cast<int>(i);
        component.parentComponentId = -1;
        component.sourceRecordIndex = recordIndices[i];
        component.localMatrix = identityMatrix();
        component.localPivot = Vec3f{};

        std::ostringstream label;
        label << "record_" << recordIndices[i];
        component.debugLabel = label.str();
        definition.components.push_back(component);
    }
    return definition;
}

MechModelDefinition makeJennerBaselineModelDefinition() {
    constexpr int torso = 0;
    constexpr int cockpit = 1;
    constexpr int rightLegCandidate = 2;
    constexpr int leftLegCandidate = 3;
    constexpr int armPodA = 4;
    constexpr int armPodB = 5;

    MechModelDefinition definition;
    definition.debugName = "jenner_baseline_records_0_5";
    definition.components = {
        MechComponentDefinition{armPodA, torso, 0, identityMatrix(), Bounds{}, Vec3f{}, "jenner_arm_pod_candidate_a_record_0"},
        MechComponentDefinition{armPodB, torso, 1, identityMatrix(), Bounds{}, Vec3f{}, "jenner_arm_pod_candidate_b_record_1"},
        MechComponentDefinition{rightLegCandidate, torso, 2, identityMatrix(), Bounds{}, Vec3f{}, "jenner_right_leg_candidate_record_2"},
        MechComponentDefinition{leftLegCandidate, torso, 3, identityMatrix(), Bounds{}, Vec3f{}, "jenner_left_leg_candidate_record_3"},
        MechComponentDefinition{torso, -1, 4, identityMatrix(), Bounds{}, Vec3f{}, "jenner_torso_record_4"},
        MechComponentDefinition{cockpit, torso, 5, identityMatrix(), Bounds{}, Vec3f{}, "jenner_cockpit_record_5"},
    };
    return definition;
}

int animationFrameCount(const ModelAnimationDefinition& animation) {
    int count = 0;
    for (const ComponentFrameSequence& sequence : animation.sequences) {
        if (sequence.sourceRecordIndices.empty()) {
            throw std::runtime_error("component frame sequence has no source records");
        }
        count = std::max(count, static_cast<int>(sequence.sourceRecordIndices.size()));
    }
    if (count <= 0) {
        throw std::runtime_error("model animation definition has no frames");
    }
    return count;
}

int animationFrameForElapsedMs(const ModelAnimationDefinition& animation, int elapsedMs) {
    return animationFrameForElapsedMs(
        animation,
        static_cast<uint64_t>(std::max(0, elapsedMs)));
}

int animationFrameForElapsedMs(const ModelAnimationDefinition& animation, uint64_t elapsedMs) {
    const int frameCount = animationFrameCount(animation);
    const int frameDurationMs = animation.sequences.front().frameDurationMs;
    if (frameDurationMs <= 0) {
        throw std::runtime_error("component frame sequence duration must be positive");
    }
    return static_cast<int>(
        (elapsedMs / static_cast<uint64_t>(frameDurationMs)) %
        static_cast<uint64_t>(frameCount));
}

MechModelDefinition applyAnimationFrame(
    const MechModelDefinition& bindDefinition,
    const ModelAnimationDefinition& animation,
    int frameIndex) {
    const int frameCount = animationFrameCount(animation);
    const int normalizedFrame = ((frameIndex % frameCount) + frameCount) % frameCount;

    MechModelDefinition frameDefinition = bindDefinition;
    for (const ComponentFrameSequence& sequence : animation.sequences) {
        if (sequence.sourceRecordIndices.empty()) {
            throw std::runtime_error("component frame sequence has no source records");
        }
        const int recordIndex =
            sequence.sourceRecordIndices[static_cast<size_t>(normalizedFrame % static_cast<int>(sequence.sourceRecordIndices.size()))];

        const auto componentIt = std::find_if(
            frameDefinition.components.begin(),
            frameDefinition.components.end(),
            [&sequence](const MechComponentDefinition& component) {
                return component.componentId == sequence.componentId;
            });
        if (componentIt == frameDefinition.components.end()) {
            throw std::runtime_error("animation sequence component not found in model definition");
        }
        componentIt->sourceRecordIndex = recordIndex;
        if (!sequence.debugLabel.empty()) {
            std::ostringstream label;
            label << sequence.debugLabel << "_record_" << recordIndex << "_frame_" << normalizedFrame;
            componentIt->debugLabel = label.str();
        }
    }
    return frameDefinition;
}

int assemblyFrameAnimationFrameCount(const ModelAssemblyFrameAnimationDefinition& animation) {
    if (animation.frames.empty()) {
        throw std::runtime_error("assembly-frame animation definition has no frames");
    }
    for (const AssemblyFrameDefinition& frame : animation.frames) {
        if (frame.sourceRecordIndices.empty()) {
            throw std::runtime_error("assembly-frame animation frame has no source records");
        }
    }
    return static_cast<int>(animation.frames.size());
}

int assemblyFrameAnimationFrameForElapsedMs(const ModelAssemblyFrameAnimationDefinition& animation, int elapsedMs) {
    return assemblyFrameAnimationFrameForElapsedMs(
        animation,
        static_cast<uint64_t>(std::max(0, elapsedMs)));
}

int assemblyFrameAnimationFrameForElapsedMs(
    const ModelAssemblyFrameAnimationDefinition& animation,
    uint64_t elapsedMs) {
    const int frameCount = assemblyFrameAnimationFrameCount(animation);
    if (animation.frameDurationMs <= 0) {
        throw std::runtime_error("assembly-frame animation duration must be positive");
    }
    return static_cast<int>(
        (elapsedMs / static_cast<uint64_t>(animation.frameDurationMs)) %
        static_cast<uint64_t>(frameCount));
}

MechModelDefinition applyAssemblyFrameAnimationFrame(
    const ModelAssemblyFrameAnimationDefinition& animation,
    int frameIndex) {
    const int frameCount = assemblyFrameAnimationFrameCount(animation);
    const int normalizedFrame = ((frameIndex % frameCount) + frameCount) % frameCount;
    const AssemblyFrameDefinition& frame = animation.frames[static_cast<size_t>(normalizedFrame)];
    const std::string frameLabel = frame.debugLabel.empty()
                                      ? "assembly_frame_" + std::to_string(normalizedFrame)
                                      : frame.debugLabel;
    MechModelDefinition definition =
        makeRecordListModelDefinition(frame.sourceRecordIndices, animation.debugName + "_" + frameLabel);
    if (!animation.componentIds.empty()) {
        if (animation.componentIds.size() != definition.components.size()) {
            throw std::runtime_error(
                "assembly-frame animation component identity count mismatch");
        }
        for (size_t i = 0; i < definition.components.size(); ++i) {
            definition.components[i].componentId = animation.componentIds[i];
        }
    }
    for (size_t i = 0; i < definition.components.size(); ++i) {
        std::ostringstream label;
        label << frameLabel << "_slot_" << i << "_component_"
              << definition.components[i].componentId << "_record_"
              << definition.components[i].sourceRecordIndex;
        definition.components[i].debugLabel = label.str();
    }
    return definition;
}

void updateModelAssemblyPose(ModelAssembly& assembly, const MechPose& pose) {
    if (assembly.definition.components.size() != assembly.components.size()) {
        throw std::runtime_error("model assembly definition/component count mismatch");
    }
    if (!pose.componentLocalMatrices.empty() && pose.componentLocalMatrices.size() != assembly.components.size()) {
        throw std::runtime_error("mech pose component count does not match model assembly");
    }

    for (size_t i = 0; i < assembly.components.size(); ++i) {
        const MechComponentDefinition& source = assembly.definition.components[i];
        ModelAssemblyComponent& component = assembly.components[i];
        Mat4f poseLocalMatrix = identityMatrix();
        if (!pose.componentLocalMatrices.empty()) {
            poseLocalMatrix = pose.componentLocalMatrices[i];
        }
        for (const ComponentPoseOverride& overrideMatrix : pose.componentLocalOverrides) {
            if (overrideMatrix.componentId == source.componentId) {
                poseLocalMatrix = overrideMatrix.localMatrix;
                break;
            }
        }

        component.componentId = source.componentId;
        component.parentComponentId = source.parentComponentId;
        component.sourceRecordIndex = source.sourceRecordIndex;
        component.localMatrix = multiply(source.localMatrix, poseLocalMatrix);
        component.localPivot = source.localPivot;
        component.defaultPoseAxis = source.defaultPoseAxis;
        component.worldMatrix = makeIdentityMatrix();
        component.worldBounds = Bounds{};
        component.pivot = Vec3f{};
    }

    std::vector<bool> resolved(assembly.components.size(), false);
    size_t remaining = assembly.components.size();
    while (remaining > 0) {
        bool progressed = false;
        for (size_t i = 0; i < assembly.components.size(); ++i) {
            ModelAssemblyComponent& component = assembly.components[i];
            if (resolved[i]) {
                continue;
            }
            if (component.parentComponentId < 0) {
                component.worldMatrix = component.localMatrix;
            } else {
                const auto parentIt = std::find_if(
                    assembly.components.begin(),
                    assembly.components.end(),
                    [&component](const ModelAssemblyComponent& candidate) {
                        return candidate.componentId == component.parentComponentId;
                    });
                if (parentIt == assembly.components.end()) {
                    throw std::runtime_error("component parent not found in model definition");
                }
                const size_t parentIndex = static_cast<size_t>(std::distance(assembly.components.begin(), parentIt));
                if (!resolved[parentIndex]) {
                    continue;
                }
                component.worldMatrix = multiply(parentIt->worldMatrix, component.localMatrix);
            }

            component.worldBounds = transformBounds(component.localBounds, component.worldMatrix);
            component.pivot = transformPoint(component.worldMatrix, component.localPivot);
            resolved[i] = true;
            --remaining;
            progressed = true;
        }
        if (!progressed) {
            throw std::runtime_error("component parent graph contains a cycle or unresolved parent");
        }
    }
    assembly.bounds = combineBounds(assembly.components);
}

void updateMechRenderInstancePose(MechRenderInstance& instance, const MechPose& pose) {
    updateModelAssemblyPose(instance.assembly, pose);
}

bool isComponentVisible(
    const ModelAssembly& assembly,
    const MechComponentVisibility& visibility,
    int componentId) {
    auto isHidden = [&visibility](int id) {
        return std::find(visibility.hiddenComponentIds.begin(), visibility.hiddenComponentIds.end(), id) !=
               visibility.hiddenComponentIds.end();
    };
    if (isHidden(componentId)) {
        return false;
    }
    if (!visibility.hideDescendants) {
        return true;
    }

    int currentId = componentId;
    for (size_t depth = 0; depth < assembly.components.size(); ++depth) {
        const auto componentIt = std::find_if(
            assembly.components.begin(),
            assembly.components.end(),
            [currentId](const ModelAssemblyComponent& component) {
                return component.componentId == currentId;
            });
        if (componentIt == assembly.components.end() || componentIt->parentComponentId < 0) {
            return true;
        }
        if (isHidden(componentIt->parentComponentId)) {
            return false;
        }
        currentId = componentIt->parentComponentId;
    }
    return true;
}

ModelAssembly buildModelAssembly(
    const MechModelDefinition& definition,
    const std::vector<legacy3d::RuntimeRecord>& records,
    const legacy3d::GpuBatchOptions& batchOptions,
    const MechPose& pose) {
    if (definition.components.empty()) {
        throw std::runtime_error("mech model definition has no components");
    }
    if (!pose.componentLocalMatrices.empty() && pose.componentLocalMatrices.size() != definition.components.size()) {
        throw std::runtime_error("mech pose component count does not match model definition");
    }

    ModelAssembly assembly;
    assembly.definition = definition;
    assembly.components.reserve(definition.components.size());
    for (size_t i = 0; i < definition.components.size(); ++i) {
        const MechComponentDefinition& source = definition.components[i];
        const legacy3d::RuntimeRecord& record = selectRecord(records, source.sourceRecordIndex);
        const legacy3d::GpuBatch componentBatch = legacy3d::buildGpuBatch(record, batchOptions);

        ModelAssemblyComponent component;
        component.componentId = source.componentId;
        component.parentComponentId = source.parentComponentId;
        component.sourceRecordIndex = source.sourceRecordIndex;
        component.localMatrix = source.localMatrix;
        component.localBounds = boundsFromGpuBatch(componentBatch);
        component.localPivot = source.localPivot;
        component.debugLabel = source.debugLabel;
        component.defaultPoseAxis = source.defaultPoseAxis;
        assembly.components.push_back(component);
    }

    updateModelAssemblyPose(assembly, pose);
    return assembly;
}

MechRenderInstance buildMechRenderInstance(
    const MechModelDefinition& definition,
    const std::vector<legacy3d::RuntimeRecord>& records,
    const legacy3d::GpuBatchOptions& batchOptions,
    const MechPose& pose) {
    MechRenderInstance instance;
    instance.assembly = buildModelAssembly(definition, records, batchOptions, pose);

    int groupOffset = 0;
    for (const ModelAssemblyComponent& component : instance.assembly.components) {
        const legacy3d::RuntimeRecord& record = selectRecord(records, component.sourceRecordIndex);
        const legacy3d::GpuBatch sourceBatch = legacy3d::buildGpuBatch(record, batchOptions);
        int maxGroup = -1;
        for (const auto& entry : sourceBatch.groupMatrices) {
            maxGroup = std::max(maxGroup, entry.first);
        }
        const int groupStride = std::max<int>({1, record.partCount, maxGroup + 1});
        appendBindBatch(instance.batch, instance.ranges, sourceBatch, component, groupOffset, groupStride);
        groupOffset += groupStride;
    }

    return instance;
}

} // namespace mw::mech3d
