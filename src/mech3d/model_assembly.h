#pragma once

#include "legacy3d/shape_parser.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mw::mech3d {

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct Bounds {
    Vec3f min{};
    Vec3f max{};
    Vec3f center{};
    float radius = 1.0f;
    bool valid = false;
};

using Mat4f = std::array<float, 16>;

enum class ComponentPoseAxis {
    Unknown,
    X,
    Y,
    Z,
};

struct MechComponentDefinition {
    int componentId = 0;
    int parentComponentId = -1;
    int sourceRecordIndex = 0;
    Mat4f localMatrix{};
    Bounds localBounds{};
    Vec3f localPivot{};
    std::string debugLabel;
    ComponentPoseAxis defaultPoseAxis = ComponentPoseAxis::Unknown;
};

struct MechModelDefinition {
    std::string debugName;
    std::vector<MechComponentDefinition> components;
};

struct ComponentPoseOverride {
    int componentId = 0;
    Mat4f localMatrix{};
};

struct MechPose {
    std::vector<Mat4f> componentLocalMatrices;
    std::vector<ComponentPoseOverride> componentLocalOverrides;
};

struct MechComponentVisibility {
    std::vector<int> hiddenComponentIds;
    bool hideDescendants = true;
};

struct ComponentFrameSequence {
    int componentId = 0;
    std::vector<int> sourceRecordIndices;
    int frameDurationMs = 180;
    std::string debugLabel;
};

struct ModelAnimationDefinition {
    std::string debugName;
    std::vector<ComponentFrameSequence> sequences;
};

struct AssemblyFrameDefinition {
    std::vector<int> sourceRecordIndices;
    std::string debugLabel;
};

struct ModelAssemblyFrameAnimationDefinition {
    std::string debugName;
    std::vector<int> componentIds;
    std::vector<AssemblyFrameDefinition> frames;
    int frameDurationMs = 180;
};

struct ModelAssemblyComponent {
    int componentId = 0;
    int parentComponentId = -1;
    int sourceRecordIndex = 0;
    Mat4f localMatrix{};
    Mat4f worldMatrix{};
    Bounds localBounds{};
    Bounds worldBounds{};
    Vec3f localPivot{};
    Vec3f pivot{};
    std::string debugLabel;
    ComponentPoseAxis defaultPoseAxis = ComponentPoseAxis::Unknown;
};

struct ModelAssembly {
    MechModelDefinition definition;
    std::vector<ModelAssemblyComponent> components;
    Bounds bounds{};
};

struct ComponentRenderRange {
    int componentId = 0;
    int sourceRecordIndex = 0;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    uint32_t triangleIndexOffset = 0;
    uint32_t triangleIndexCount = 0;
    uint32_t lineIndexOffset = 0;
    uint32_t lineIndexCount = 0;
};

struct MechRenderInstance {
    ModelAssembly assembly;
    legacy3d::GpuBatch batch; // bind/local component geometry; draw with ComponentRenderRange + worldMatrix
    std::vector<ComponentRenderRange> ranges;
    bool usesComponentMatrices = true;
};

Mat4f identityMatrix();
Mat4f translationMatrix(Vec3f offset);
Mat4f rotationXMatrix(float radians);
Mat4f rotationYMatrix(float radians);
Mat4f rotationZMatrix(float radians);
Mat4f multiplyMatrix(Mat4f a, Mat4f b);
Mat4f rotateAroundPivotMatrix(Vec3f pivot, Mat4f rotation);
Mat4f rotateAroundPivotXMatrix(Vec3f pivot, float radians);
Mat4f rotateAroundPivotYMatrix(Vec3f pivot, float radians);
Mat4f rotateAroundPivotZMatrix(Vec3f pivot, float radians);
void setComponentPoseLocalMatrix(MechPose& pose, int componentId, Mat4f localMatrix);
void applyPlayerTorsoYawPose(MechPose& pose, const ModelAssembly& assembly, float yawRadians);
MechModelDefinition makeRecordListModelDefinition(const std::vector<int>& recordIndices, const std::string& debugName);
MechModelDefinition makeJennerBaselineModelDefinition();
int animationFrameCount(const ModelAnimationDefinition& animation);
int animationFrameForElapsedMs(const ModelAnimationDefinition& animation, int elapsedMs);
int animationFrameForElapsedMs(const ModelAnimationDefinition& animation, uint64_t elapsedMs);
MechModelDefinition applyAnimationFrame(
    const MechModelDefinition& bindDefinition,
    const ModelAnimationDefinition& animation,
    int frameIndex);
int assemblyFrameAnimationFrameCount(const ModelAssemblyFrameAnimationDefinition& animation);
int assemblyFrameAnimationFrameForElapsedMs(const ModelAssemblyFrameAnimationDefinition& animation, int elapsedMs);
int assemblyFrameAnimationFrameForElapsedMs(
    const ModelAssemblyFrameAnimationDefinition& animation,
    uint64_t elapsedMs);
MechModelDefinition applyAssemblyFrameAnimationFrame(
    const ModelAssemblyFrameAnimationDefinition& animation,
    int frameIndex);
void updateModelAssemblyPose(ModelAssembly& assembly, const MechPose& pose = {});
void updateMechRenderInstancePose(MechRenderInstance& instance, const MechPose& pose = {});
bool isComponentVisible(
    const ModelAssembly& assembly,
    const MechComponentVisibility& visibility,
    int componentId);
ModelAssembly buildModelAssembly(
    const MechModelDefinition& definition,
    const std::vector<legacy3d::RuntimeRecord>& records,
    const legacy3d::GpuBatchOptions& batchOptions,
    const MechPose& pose = {});
MechRenderInstance buildMechRenderInstance(
    const MechModelDefinition& definition,
    const std::vector<legacy3d::RuntimeRecord>& records,
    const legacy3d::GpuBatchOptions& batchOptions,
    const MechPose& pose = {});

} // namespace mw::mech3d
