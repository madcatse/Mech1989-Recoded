#include "presentation/campaign_battle_gl_viewport.h"
#include "presentation/campaign_cockpit_compositor.h"

#include "legacy3d/shape_parser.h"
#include "legacy3d/terrain_parser.h"
#include "mech3d/mech_catalog.h"
#include "mech3d/mech_runtime_state.h"
#include "mech3d/model_assembly.h"

#include <gl/GL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mw::presentation {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kCampaignBattleNearClipDistance = 25.0f;
constexpr float kCockpitBeamNearClipMargin = 5.0f;

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct BatchBounds {
    bool valid = false;
    float minX = 0.0f;
    float minY = 0.0f;
    float minZ = 0.0f;
    float maxX = 0.0f;
    float maxY = 0.0f;
    float maxZ = 0.0f;
};

BatchBounds batchBounds(const legacy3d::GpuBatch& batch);

struct LoadedVisual {
    battle::CampaignBattleRenderVisualInstance source;
    mech3d::MechRenderInstance renderInstance;
    mech3d::ResolvedMechRuntimeState runtimeState;
    std::string signature;
};

struct LoadedTerrainGeometry {
    legacy3d::TerrainMesh mesh;
    legacy3d::GpuBatch objectBatch;
    legacy3d::GpuBatch objectDitherBatch;
    size_t objectCount = 0;
    size_t objectRecordCount = 0;
    std::string paletteId;
    bool paletteLoaded = false;
};

struct UploadedCockpitSprite {
    int index = 0;
    int width = 0;
    int height = 0;
    GLuint texture = 0;
};

struct ImpactSpherePrimitive {
    Vec3 center;
    float radius = 0.0f;
    std::array<uint8_t, 3> shadeBytes{};
};

struct ImpactLinePrimitive {
    Vec3 start;
    Vec3 end;
    std::array<uint8_t, 4> shadeBytes{};
};

struct ImpactTrianglePrimitive {
    Vec3 a;
    Vec3 b;
    Vec3 c;
    std::array<uint8_t, 4> shadeBytes{};
};

struct ImpactStageGeometry {
    int recordIndex = -1;
    std::vector<ImpactSpherePrimitive> spheres;
    std::vector<ImpactLinePrimitive> lines;
    std::vector<ImpactTrianglePrimitive> triangles;
};

// The decoded type-1 radii are internally self-consistent with each record's
// extent, but drawing them one-for-one in the replacement world makes the DOS
// impact sequence about twice the observed on-Mech size. Keep the correction
// local to presentation until the original type-1 projection path is decoded.
constexpr float kImpactGeometryWorldScale = 0.5f;

using RecordCache = std::map<std::filesystem::path, std::vector<legacy3d::RuntimeRecord>>;

const std::vector<legacy3d::RuntimeRecord>& recordsForPath(
    RecordCache& cache,
    const std::filesystem::path& path) {
    const auto found = cache.find(path);
    if (found != cache.end()) {
        return found->second;
    }
    return cache.emplace(path, legacy3d::loadRuntimeShapeRecords(path)).first->second;
}

std::string terrainPaletteId(const std::optional<int>& environmentId) {
    if (environmentId.has_value() && *environmentId == 0) {
        return "desert";
    }
    if (environmentId.has_value() && *environmentId == 2) {
        return "snow";
    }
    return "green";
}

legacy3d::GpuBatch buildTerrainPaletteGpuBatch(
    const legacy3d::RuntimeRecord& record,
    const std::string& paletteId,
    bool overlay,
    bool showEdges) {
    legacy3d::GpuBatchOptions options;
    options.showEdges = showEdges;
    options.showLines = showEdges;
    return legacy3d::buildOriginalTerrainPaletteGpuBatch(
        record,
        options,
        paletteId,
        overlay);
}

void appendPlacedTerrainObject(
    legacy3d::GpuBatch& target,
    const legacy3d::GpuBatch& object,
    const BatchBounds& bounds,
    const legacy3d::TerrainObjectPlacement& placement) {
    if (!bounds.valid || object.vertices.empty()) {
        return;
    }
    const float centerX = (bounds.minX + bounds.maxX) * 0.5f;
    const float centerZ = (bounds.minZ + bounds.maxZ) * 0.5f;
    const uint32_t firstVertex = static_cast<uint32_t>(target.vertices.size() / 6u);
    for (size_t offset = 0; offset + 5u < object.vertices.size(); offset += 6u) {
        target.vertices.push_back(object.vertices[offset] + placement.x - centerX);
        target.vertices.push_back(object.vertices[offset + 1u] - bounds.minY);
        // Match the proven Phase 5 layout renderer: mirror local top-down Y
        // (OpenGL Z) around the record center, then reverse triangle winding.
        target.vertices.push_back(
            centerZ * 2.0f - object.vertices[offset + 2u] + placement.z - centerZ);
        target.vertices.push_back(object.vertices[offset + 3u]);
        target.vertices.push_back(object.vertices[offset + 4u]);
        target.vertices.push_back(object.vertices[offset + 5u]);
    }
    for (size_t index = 0; index + 2u < object.triangleIndices.size(); index += 3u) {
        target.triangleIndices.push_back(firstVertex + object.triangleIndices[index]);
        target.triangleIndices.push_back(firstVertex + object.triangleIndices[index + 2u]);
        target.triangleIndices.push_back(firstVertex + object.triangleIndices[index + 1u]);
    }
    for (uint32_t index : object.lineIndices) {
        target.lineIndices.push_back(firstVertex + index);
    }
}

mech3d::ResolvedMechRuntimeState resolvedRuntimeState(
    const battle::CampaignBattleRenderVisualInstance& visual) {
    mech3d::MechRuntimeState state;
    state.currentAnimationId = visual.animationId.empty() ? "idle" : visual.animationId;
    state.destroyedComponentIds = visual.destroyedComponentIds;
    state.hiddenComponentIds = visual.hiddenComponentIds;
    state.disabledComponentIds = visual.disabledComponentIds;
    return mech3d::resolveMechRuntimeState(
        state,
        mech3d::catalogComponentDamageRules(visual.mechPresetId));
}

std::string visualSignature(const battle::CampaignBattleRenderVisualInstance& visual) {
    const mech3d::ResolvedMechRuntimeState runtime = resolvedRuntimeState(visual);
    const std::string animationId = runtime.activeAnimationId.empty() ? "idle" : runtime.activeAnimationId;
    int frame = 0;
    if (animationId != "idle") {
        const mech3d::MechCatalogAnimationKind kind =
            mech3d::catalogAnimationKind(visual.mechPresetId, animationId);
        if (kind == mech3d::MechCatalogAnimationKind::ComponentFrames) {
            const mech3d::ModelAnimationDefinition animation =
                mech3d::makeCatalogAnimationDefinition(visual.mechPresetId, animationId);
            frame = mech3d::animationFrameForElapsedMs(animation, visual.animationElapsedMs);
            if (animationId == "walk" && visual.forwardSpeed < -0.0001) {
                frame = mech3d::animationFrameCount(animation) - 1 - frame;
            }
        } else if (kind == mech3d::MechCatalogAnimationKind::AssemblyFrames) {
            const mech3d::ModelAssemblyFrameAnimationDefinition animation =
                mech3d::makeCatalogAssemblyFrameAnimationDefinition(visual.mechPresetId, animationId);
            frame = mech3d::assemblyFrameAnimationFrameForElapsedMs(
                animation,
                visual.animationElapsedMs);
        }
    }

    std::ostringstream out;
    out << visual.mechPresetId << ':' << animationId << ':' << frame << ':'
        << (visual.forwardSpeed < 0.0 ? "reverse" : "forward");
    for (int id : visual.destroyedComponentIds) {
        out << ":d" << id;
    }
    for (int id : visual.hiddenComponentIds) {
        out << ":h" << id;
    }
    for (int id : visual.disabledComponentIds) {
        out << ":x" << id;
    }
    return out.str();
}

void updateLoadedVisualPresentationPose(LoadedVisual& loaded) {
    const std::string animationId = loaded.runtimeState.activeAnimationId.empty()
                                        ? "idle"
                                        : loaded.runtimeState.activeAnimationId;
    if (loaded.runtimeState.mechDestroyed ||
        (animationId != "idle" &&
         mech3d::catalogAnimationKind(loaded.source.mechPresetId, animationId) ==
             mech3d::MechCatalogAnimationKind::AssemblyFrames)) {
        return;
    }
    mech3d::MechPose pose;
    mech3d::applyPlayerTorsoYawPose(
        pose,
        loaded.renderInstance.assembly,
        static_cast<float>(loaded.source.torsoYawRadians));
    mech3d::updateMechRenderInstancePose(loaded.renderInstance, pose);
}

LoadedVisual loadVisual(
    const battle::CampaignBattleRenderVisualInstance& visual,
    RecordCache& cache) {
    LoadedVisual loaded;
    loaded.source = visual;
    loaded.signature = visualSignature(visual);
    loaded.runtimeState = resolvedRuntimeState(visual);

    const std::vector<legacy3d::RuntimeRecord>& records =
        recordsForPath(cache, visual.modelResourcePath);
    const mech3d::MechModelDefinition bindDefinition =
        mech3d::makeCatalogMechModelDefinition(visual.mechPresetId);
    mech3d::MechModelDefinition frameDefinition = bindDefinition;
    const std::string animationId = loaded.runtimeState.activeAnimationId.empty()
                                        ? "idle"
                                        : loaded.runtimeState.activeAnimationId;
    bool assemblyFrameAnimation = false;
    if (animationId != "idle") {
        const mech3d::MechCatalogAnimationKind kind =
            mech3d::catalogAnimationKind(visual.mechPresetId, animationId);
        if (kind == mech3d::MechCatalogAnimationKind::ComponentFrames) {
            const mech3d::ModelAnimationDefinition animation =
                mech3d::makeCatalogAnimationDefinition(visual.mechPresetId, animationId);
            int frame = mech3d::animationFrameForElapsedMs(animation, visual.animationElapsedMs);
            if (animationId == "walk" && visual.forwardSpeed < -0.0001) {
                frame = mech3d::animationFrameCount(animation) - 1 - frame;
            }
            frameDefinition = mech3d::applyAnimationFrame(bindDefinition, animation, frame);
        } else if (kind == mech3d::MechCatalogAnimationKind::AssemblyFrames) {
            const mech3d::ModelAssemblyFrameAnimationDefinition animation =
                mech3d::makeCatalogAssemblyFrameAnimationDefinition(visual.mechPresetId, animationId);
            const int frame = mech3d::assemblyFrameAnimationFrameForElapsedMs(
                animation,
                visual.animationElapsedMs);
            frameDefinition = mech3d::applyAssemblyFrameAnimationFrame(animation, frame);
            assemblyFrameAnimation = true;
        }
    }

    legacy3d::GpuBatchOptions batchOptions;
    batchOptions.shadeMode = "stored";
    batchOptions.swizzle = "xzy";
    loaded.renderInstance =
        mech3d::buildMechRenderInstance(frameDefinition, records, batchOptions);
    if (!assemblyFrameAnimation) {
        updateLoadedVisualPresentationPose(loaded);
    }
    return loaded;
}

battle::CampaignBattleRenderVisualInstance projectileMountReferenceSource(
    battle::CampaignBattleRenderVisualInstance source) {
    // Projectile launch points must not be reclassified by replacement walk
    // records after launch.  The catalog bind assembly is the stable model
    // identity used by the existing provisional component/torso partition;
    // the projectile's frozen launch transform and torso yaw are applied by
    // weaponMountWorldPoint.
    source.forwardSpeed = 0.0;
    source.torsoYawRadians = 0.0;
    source.animationId = "idle";
    source.animationElapsedMs = 0u;
    source.mechDestroyed = false;
    source.destroyedComponentIds.clear();
    source.hiddenComponentIds.clear();
    source.disabledComponentIds.clear();
    return source;
}

bool componentLabelContains(
    const mech3d::ModelAssemblyComponent& component,
    const std::string& token) {
    return component.debugLabel.find(token) != std::string::npos;
}

const mech3d::ModelAssemblyComponent* componentByLabel(
    const LoadedVisual& visual,
    const std::string& token) {
    const auto found = std::find_if(
        visual.renderInstance.assembly.components.begin(),
        visual.renderInstance.assembly.components.end(),
        [&token](const mech3d::ModelAssemblyComponent& component) {
            return componentLabelContains(component, token) &&
                component.worldBounds.valid;
        });
    return found == visual.renderInstance.assembly.components.end()
        ? nullptr
        : &*found;
}

std::optional<Vec3> weaponMountWorldPoint(
    const LoadedVisual& visual,
    const std::string& locationId,
    const battle::Transform* transformOverride = nullptr,
    std::optional<double> torsoYawOverride = std::nullopt) {
    const mech3d::ModelAssembly& assembly = visual.renderInstance.assembly;
    if (!assembly.bounds.valid || locationId.empty()) {
        return std::nullopt;
    }

    const mech3d::ModelAssemblyComponent* leftArm =
        componentByLabel(visual, "left_arm");
    const mech3d::ModelAssemblyComponent* rightArm =
        componentByLabel(visual, "right_arm");
    const mech3d::ModelAssemblyComponent* torso =
        componentByLabel(visual, "torso");
    const mech3d::ModelAssemblyComponent* cockpit =
        componentByLabel(visual, "cockpit");
    const mech3d::ModelAssemblyComponent* leftLeg =
        componentByLabel(visual, "left_leg");
    const mech3d::ModelAssemblyComponent* rightLeg =
        componentByLabel(visual, "right_leg");

    const mech3d::Bounds* selectedBounds = nullptr;
    if (locationId == "LA" && leftArm != nullptr) {
        selectedBounds = &leftArm->worldBounds;
    } else if (locationId == "RA" && rightArm != nullptr) {
        selectedBounds = &rightArm->worldBounds;
    } else if (locationId == "HD" && cockpit != nullptr) {
        selectedBounds = &cockpit->worldBounds;
    } else if (locationId == "LL" && leftLeg != nullptr) {
        selectedBounds = &leftLeg->worldBounds;
    } else if (locationId == "RL" && rightLeg != nullptr) {
        selectedBounds = &rightLeg->worldBounds;
    }

    const mech3d::Bounds& verticalBounds = selectedBounds != nullptr
        ? *selectedBounds
        : (torso != nullptr ? torso->worldBounds : assembly.bounds);
    float mountX = verticalBounds.center.x;
    if (locationId == "LT" || locationId == "CT" || locationId == "RT") {
        const float width = assembly.bounds.max.x - assembly.bounds.min.x;
        const float lowThirdCenter = assembly.bounds.min.x + width / 6.0f;
        const float highThirdCenter = assembly.bounds.max.x - width / 6.0f;
        const bool leftIsLow = leftArm != nullptr && rightArm != nullptr &&
            leftArm->worldBounds.center.x < rightArm->worldBounds.center.x;
        if (locationId == "LT") {
            mountX = leftIsLow ? lowThirdCenter : highThirdCenter;
        } else if (locationId == "RT") {
            mountX = leftIsLow ? highThirdCenter : lowThirdCenter;
        } else {
            mountX = assembly.bounds.center.x;
        }
    }

    // The model's +Z edge faces the current torso heading.  Use the center of
    // the selected body region at that front edge as the provisional muzzle.
    Vec3 modelPoint{
        mountX - assembly.bounds.center.x,
        verticalBounds.center.y - assembly.bounds.min.y,
        verticalBounds.max.z - assembly.bounds.center.z,
    };
    if (torsoYawOverride.has_value()) {
        const float delta = static_cast<float>(
            *torsoYawOverride - visual.source.torsoYawRadians);
        const float deltaCosine = std::cos(delta);
        const float deltaSine = std::sin(delta);
        const float rotatedX =
            deltaCosine * modelPoint.x + deltaSine * modelPoint.z;
        const float rotatedZ =
            -deltaSine * modelPoint.x + deltaCosine * modelPoint.z;
        modelPoint.x = rotatedX;
        modelPoint.z = rotatedZ;
    }
    const battle::Transform& transform = transformOverride != nullptr
        ? *transformOverride
        : visual.source.transform;
    const float heading = static_cast<float>(transform.headingRadians);
    const float cosine = std::cos(heading);
    const float sine = std::sin(heading);
    return Vec3{
        static_cast<float>(transform.x) +
            cosine * modelPoint.x + sine * modelPoint.z,
        static_cast<float>(transform.y) + modelPoint.y,
        static_cast<float>(transform.z) -
            sine * modelPoint.x + cosine * modelPoint.z,
    };
}

const LoadedVisual* loadedVisualByEntityId(
    const std::vector<LoadedVisual>& visuals,
    battle::EntityId entityId) {
    const auto found = std::find_if(
        visuals.begin(),
        visuals.end(),
        [entityId](const LoadedVisual& visual) {
            return visual.source.entityId == entityId;
        });
    return found == visuals.end() ? nullptr : &*found;
}

battle::CampaignBattleRenderBeamInstance mountedBeamForScene(
    const battle::CampaignBattleRenderScenePackage& scene,
    const std::vector<LoadedVisual>& visuals,
    const battle::CampaignBattleRenderBeamInstance& beam) {
    battle::CampaignBattleRenderBeamInstance mountedBeam = beam;
    const LoadedVisual* shooter = loadedVisualByEntityId(
        visuals, beam.shooterEntityId);
    if (shooter == nullptr) {
        return mountedBeam;
    }
    const std::optional<Vec3> mount = weaponMountWorldPoint(
        *shooter, beam.locationId);
    if (!mount.has_value()) {
        return mountedBeam;
    }
    mountedBeam.start = {mount->x, mount->y, mount->z};
    if (scene.mode == battle::CampaignBattlePresentationMode::Cockpit &&
        shooter->source.playerControlled) {
        mountedBeam.start.y += scene.cockpitCameraBobOffsetY;
        if (scene.camera.valid) {
            // A model-derived muzzle can lie behind the cockpit eye.  The
            // four-sided beam shell then encloses the eye and OpenGL's near
            // plane exposes two disconnected faces, which looks like left
            // and right weapons fired together even though the scene owns
            // only one shot sequence.  Preserve the mount's lateral/vertical
            // placement, but move its hidden depth component just beyond the
            // cockpit near plane.
            const float heading = static_cast<float>(
                scene.camera.transform.headingRadians);
            const float forwardX = std::sin(heading);
            const float forwardZ = std::cos(heading);
            const float eyeX = static_cast<float>(scene.camera.transform.x);
            const float eyeZ = static_cast<float>(scene.camera.transform.z);
            const float forwardDistance =
                (static_cast<float>(mountedBeam.start.x) - eyeX) * forwardX +
                (static_cast<float>(mountedBeam.start.z) - eyeZ) * forwardZ;
            const float minimumDistance =
                kCampaignBattleNearClipDistance + kCockpitBeamNearClipMargin;
            if (forwardDistance < minimumDistance) {
                const float correction = minimumDistance - forwardDistance;
                mountedBeam.start.x += forwardX * correction;
                mountedBeam.start.z += forwardZ * correction;
            }
        }
    }
    return mountedBeam;
}

struct MountedProjectilePose {
    Vec3 position;
    Vec3 direction;
    bool mountResolved = false;
    bool straightMountToTargetPath = false;
};

MountedProjectilePose mountedProjectilePoseForScene(
    const std::vector<LoadedVisual>& visuals,
    const battle::CampaignBattleRenderProjectileInstance& projectile) {
    const auto length = [](Vec3 value) {
        return std::sqrt(
            value.x * value.x + value.y * value.y + value.z * value.z);
    };
    const auto directionFrom = [&length](Vec3 from, Vec3 to) {
        Vec3 delta{to.x - from.x, to.y - from.y, to.z - from.z};
        const float magnitude = length(delta);
        if (magnitude > 0.0001f) {
            delta.x /= magnitude;
            delta.y /= magnitude;
            delta.z /= magnitude;
        }
        return delta;
    };
    MountedProjectilePose pose;
    pose.position = {
        static_cast<float>(projectile.position.x),
        static_cast<float>(projectile.position.y),
        static_cast<float>(projectile.position.z),
    };
    pose.direction = {
        static_cast<float>(projectile.direction.x),
        static_cast<float>(projectile.direction.y),
        static_cast<float>(projectile.direction.z),
    };
    if (!projectile.launchAimOriginValid) {
        return pose;
    }
    std::optional<Vec3> mount;
    if (projectile.launchModelMountValid) {
        mount = Vec3{
            static_cast<float>(projectile.launchModelMount.x),
            static_cast<float>(projectile.launchModelMount.y),
            static_cast<float>(projectile.launchModelMount.z),
        };
    } else if (const LoadedVisual* shooter = loadedVisualByEntityId(
                   visuals, projectile.shooterEntityId)) {
        mount = weaponMountWorldPoint(
            *shooter,
            projectile.locationId,
            &projectile.launchShooterTransform,
            projectile.launchTorsoYawRadians);
    }
    if (!mount.has_value()) {
        return pose;
    }
    pose.mountResolved = true;
    const Vec3 aimOrigin{
        static_cast<float>(projectile.launchAimOrigin.x),
        static_cast<float>(projectile.launchAimOrigin.y),
        static_cast<float>(projectile.launchAimOrigin.z),
    };
    if (!projectile.targetBound) {
        // An unbound projectile has no terminal point: the diagnostic
        // crosshair/terrain intersection is not an impact.  Preserve the
        // authoritative displacement for its complete recovered lifetime,
        // merely translating the launch origin to the frozen model mount.
        pose.position = {
            mount->x + pose.position.x - aimOrigin.x,
            mount->y + pose.position.y - aimOrigin.y,
            mount->z + pose.position.z - aimOrigin.z,
        };
        pose.straightMountToTargetPath = true;
        return pose;
    }
    if (!projectile.visualTargetPointValid) {
        return pose;
    }
    const Vec3 target{
        static_cast<float>(projectile.visualTargetPoint.x),
        static_cast<float>(projectile.visualTargetPoint.y),
        static_cast<float>(projectile.visualTargetPoint.z),
    };
    const float authoritativeRemainingDistance = length(Vec3{
        target.x - pose.position.x,
        target.y - pose.position.y,
        target.z - pose.position.z,
    });
    const float authoritativeTargetDistance = length(Vec3{
        target.x - aimOrigin.x,
        target.y - aimOrigin.y,
        target.z - aimOrigin.z,
    });
    const float mountTargetDistance = length(Vec3{
        target.x - mount->x,
        target.y - mount->y,
        target.z - mount->z,
    });
    if (authoritativeTargetDistance <= 0.0001f ||
        mountTargetDistance <= 0.0001f) {
        return pose;
    }
    // Use distance still remaining, rather than total displacement from the
    // aim origin.  A guided path can be longer than the direct line; using
    // travelled distance therefore reached 100% early and pinned the model
    // at the target while the authoritative projectile was still in flight.
    const float progress = std::clamp(
        1.0f - authoritativeRemainingDistance / authoritativeTargetDistance,
        0.0f,
        1.0f);
    pose.position = {
        mount->x + (target.x - mount->x) * progress,
        mount->y + (target.y - mount->y) * progress,
        mount->z + (target.z - mount->z) * progress,
    };
    pose.direction = directionFrom(*mount, target);
    pose.straightMountToTargetPath = true;
    return pose;
}

LoadedTerrainGeometry loadTerrainGeometry(
    const battle::CampaignBattleRenderTerrainResources& terrain) {
    if (terrain.gridPaths.size() != 4u) {
        throw std::runtime_error("campaign OpenGL terrain requires four GRD tiles");
    }
    if (terrain.worldPaths.size() != terrain.gridPaths.size()) {
        throw std::runtime_error("campaign OpenGL terrain requires one WLD file per GRD tile");
    }
    std::array<legacy3d::TerrainGrid, 4> grids;
    for (size_t i = 0; i < grids.size(); ++i) {
        grids[i] = legacy3d::loadTerrainGrid(terrain.gridPaths[i]);
    }
    const legacy3d::TerrainGrid combined = legacy3d::composeTerrainGrid2x2(
        grids,
        "campaign_snapshot_scene",
        false,
        true);
    const double spanX = terrain.boundsMaxX - terrain.boundsMinX;
    const double spanZ = terrain.boundsMaxZ - terrain.boundsMinZ;
    const double cellX = combined.width > 1 ? spanX / static_cast<double>(combined.width - 1) : 1.0;
    const double cellZ = combined.height > 1 ? spanZ / static_cast<double>(combined.height - 1) : 1.0;
    const float cellSize = static_cast<float>(std::max(1.0, (cellX + cellZ) * 0.5));
    LoadedTerrainGeometry loaded;
    loaded.mesh = legacy3d::buildTerrainMesh(combined, cellSize, 20.0f);

    const std::vector<legacy3d::RuntimeRecord> records =
        legacy3d::loadRuntimeShapeRecords(terrain.terrainShapePath);
    const CampaignEgaPaletteBank palette =
        loadCampaignEgaPaletteBank(terrain.palettePath, 1u);
    if (!palette.valid) {
        throw std::runtime_error(palette.reason);
    }
    loaded.paletteId = terrainPaletteId(terrain.environmentId);
    if (palette.colorPairs != legacy3d::terrainPaletteBank1Bytes(loaded.paletteId)) {
        throw std::runtime_error("campaign terrain PAL bank 1 differs from prototype palette mapping");
    }
    loaded.paletteLoaded = true;
    const legacy3d::TerrainPlacementBounds placementBounds{
        static_cast<float>(terrain.boundsMinX),
        static_cast<float>(terrain.boundsMaxX),
        static_cast<float>(terrain.boundsMinZ),
        static_cast<float>(terrain.boundsMaxZ),
    };
    std::set<uint16_t> recordIndices;
    for (const std::filesystem::path& worldPath : terrain.worldPaths) {
        const legacy3d::TerrainWorld world = legacy3d::loadTerrainWorld(worldPath);
        const std::vector<legacy3d::TerrainObjectPlacement> placements =
            legacy3d::mapTerrainWorldPlacementsFromRawCoordinates(
                world,
                placementBounds,
                cellSize);
        loaded.objectCount += placements.size();
        for (const legacy3d::TerrainObjectPlacement& placement : placements) {
            if (static_cast<size_t>(placement.recordIndex) >= records.size()) {
                throw std::runtime_error("campaign OpenGL terrain object record is out of range");
            }
            recordIndices.insert(placement.recordIndex);
            const legacy3d::RuntimeRecord& record =
                records[static_cast<size_t>(placement.recordIndex)];
            const legacy3d::GpuBatch object = buildTerrainPaletteGpuBatch(
                record,
                loaded.paletteId,
                false,
                true);
            const legacy3d::GpuBatch dither = buildTerrainPaletteGpuBatch(
                record,
                loaded.paletteId,
                true,
                false);
            const BatchBounds bounds = batchBounds(object);
            if (!bounds.valid) {
                continue;
            }
            appendPlacedTerrainObject(loaded.objectBatch, object, bounds, placement);
            appendPlacedTerrainObject(loaded.objectDitherBatch, dither, bounds, placement);
        }
    }
    loaded.objectRecordCount = recordIndices.size();
    return loaded;
}

legacy3d::GpuBatch loadObjectiveBatch(
    const battle::CampaignBattleRenderObjectiveInstance& objective) {
    if (!objective.valid || !objective.staticModel.valid) {
        return {};
    }
    const std::vector<legacy3d::RuntimeRecord> records =
        legacy3d::loadRuntimeShapeRecords(objective.modelResourcePath);
    if (objective.staticModel.recordIndex < 0 ||
        static_cast<size_t>(objective.staticModel.recordIndex) >= records.size()) {
        throw std::runtime_error("campaign OpenGL objective record is out of range");
    }
    legacy3d::GpuBatchOptions options;
    options.shadeMode = "stored";
    options.swizzle = objective.staticModel.swizzle;
    options.scale = static_cast<float>(objective.staticModel.scale);
    return legacy3d::buildGpuBatch(
        records[static_cast<size_t>(objective.staticModel.recordIndex)],
        options);
}

std::string projectileBatchKey(
    const battle::CampaignBattleRenderProjectileInstance& projectile) {
    return projectile.modelResourcePath.string() + ':' +
        std::to_string(projectile.recordIndex);
}

legacy3d::GpuBatch loadProjectileBatch(
    const battle::CampaignBattleRenderProjectileInstance& projectile,
    RecordCache& cache) {
    if (projectile.recordIndex < 0 ||
        projectile.modelResourcePath.empty()) {
        return {};
    }
    const std::vector<legacy3d::RuntimeRecord>& records = recordsForPath(
        cache,
        projectile.modelResourcePath);
    if (static_cast<size_t>(projectile.recordIndex) >= records.size()) {
        throw std::runtime_error(
            "campaign OpenGL projectile record is out of range");
    }
    legacy3d::GpuBatchOptions options;
    options.shadeMode = projectile.recordIndex == 1 ? "ega" : "stored";
    options.swizzle = "xzy";
    return legacy3d::buildGpuBatch(
        records[static_cast<size_t>(projectile.recordIndex)],
        options);
}

std::string impactStageKey(
    const battle::CampaignBattleRenderImpactInstance& impact) {
    return impact.modelResourcePath.string() + ':' +
        std::to_string(impact.recordIndex);
}

ImpactStageGeometry loadImpactStage(
    const battle::CampaignBattleRenderImpactInstance& impact,
    RecordCache& cache) {
    if (impact.recordIndex < 2 || impact.recordIndex > 9 ||
        impact.modelResourcePath.empty()) {
        throw std::runtime_error("campaign OpenGL impact record is invalid");
    }
    const std::vector<legacy3d::RuntimeRecord>& records = recordsForPath(
        cache,
        impact.modelResourcePath);
    if (static_cast<size_t>(impact.recordIndex) >= records.size()) {
        throw std::runtime_error("campaign OpenGL impact record is out of range");
    }
    const legacy3d::RuntimeRecord& record =
        records[static_cast<size_t>(impact.recordIndex)];
    ImpactStageGeometry result;
    result.recordIndex = impact.recordIndex;
    const auto vertexAt = [&record](uint8_t index) {
        if (static_cast<size_t>(index) >= record.vertices.size()) {
            throw std::runtime_error(
                "campaign OpenGL impact primitive vertex is out of range");
        }
        const legacy3d::Vec3i vertex = record.vertices[index];
        return Vec3{
            static_cast<float>(vertex.x) * kImpactGeometryWorldScale,
            static_cast<float>(vertex.z) * kImpactGeometryWorldScale,
            static_cast<float>(vertex.y) * kImpactGeometryWorldScale,
        };
    };
    for (const legacy3d::RuntimePart& part : record.parts) {
        for (const legacy3d::RuntimeCommand& command : part.commands) {
            if (command.commandType == 1u && command.radius != 0u &&
                command.centerVertexIndex < record.vertices.size()) {
                const legacy3d::Vec3i center =
                    record.vertices[command.centerVertexIndex];
                result.spheres.push_back({
                    Vec3{
                        static_cast<float>(center.x) * kImpactGeometryWorldScale,
                        static_cast<float>(center.z) * kImpactGeometryWorldScale,
                        static_cast<float>(center.y) * kImpactGeometryWorldScale,
                    },
                    static_cast<float>(command.radius) * kImpactGeometryWorldScale,
                    command.sphereShadeBytes,
                });
            }
            if (command.commandType != 0u || impact.recordIndex < 8) {
                continue;
            }
            for (const legacy3d::RuntimePrimitive& primitive :
                 command.primitives) {
                const std::vector<uint8_t>& indices =
                    primitive.normalizedIndices;
                if (indices.size() == 2u) {
                    result.lines.push_back({
                        vertexAt(indices[0]),
                        vertexAt(indices[1]),
                        primitive.shadeBytes,
                    });
                } else if (indices.size() >= 3u) {
                    for (size_t index = 1u; index + 1u < indices.size(); ++index) {
                        result.triangles.push_back({
                            vertexAt(indices[0]),
                            vertexAt(indices[index]),
                            vertexAt(indices[index + 1u]),
                            primitive.shadeBytes,
                        });
                    }
                }
            }
        }
    }
    if (result.spheres.empty() && result.lines.empty() &&
        result.triangles.empty()) {
        throw std::runtime_error(
            "campaign OpenGL impact record has no supported primitives");
    }
    return result;
}

BatchBounds batchBounds(const legacy3d::GpuBatch& batch) {
    BatchBounds bounds;
    for (size_t offset = 0; offset + 5u < batch.vertices.size(); offset += 6u) {
        const float x = batch.vertices[offset];
        const float y = batch.vertices[offset + 1u];
        const float z = batch.vertices[offset + 2u];
        if (!bounds.valid) {
            bounds.valid = true;
            bounds.minX = bounds.maxX = x;
            bounds.minY = bounds.maxY = y;
            bounds.minZ = bounds.maxZ = z;
        } else {
            bounds.minX = std::min(bounds.minX, x);
            bounds.minY = std::min(bounds.minY, y);
            bounds.minZ = std::min(bounds.minZ, z);
            bounds.maxX = std::max(bounds.maxX, x);
            bounds.maxY = std::max(bounds.maxY, y);
            bounds.maxZ = std::max(bounds.maxZ, z);
        }
    }
    return bounds;
}

bool validGpuBatch(const legacy3d::GpuBatch& batch) {
    if ((batch.vertices.size() % 6u) != 0u) {
        return false;
    }
    const size_t vertexCount = batch.vertices.size() / 6u;
    return std::all_of(
               batch.triangleIndices.begin(),
               batch.triangleIndices.end(),
               [vertexCount](uint32_t index) { return index < vertexCount; }) &&
           std::all_of(
               batch.lineIndices.begin(),
               batch.lineIndices.end(),
               [vertexCount](uint32_t index) { return index < vertexCount; });
}

Vec3 subtract(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(Vec3 a, Vec3 b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalize(Vec3 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.00001f) {
        return {0.0f, 0.0f, 1.0f};
    }
    return {value.x / length, value.y / length, value.z / length};
}

void applyLookAt(Vec3 eye, Vec3 target) {
    const Vec3 forward = normalize(subtract(target, eye));
    const Vec3 side = normalize(cross(forward, Vec3{0.0f, 1.0f, 0.0f}));
    const Vec3 up = cross(side, forward);
    const GLfloat matrix[16] = {
        side.x, up.x, -forward.x, 0.0f,
        side.y, up.y, -forward.y, 0.0f,
        side.z, up.z, -forward.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    glMultMatrixf(matrix);
    glTranslatef(-eye.x, -eye.y, -eye.z);
}

void drawGpuBatchTriangles(const legacy3d::GpuBatch& batch) {
    glBegin(GL_TRIANGLES);
    for (uint32_t index : batch.triangleIndices) {
        const size_t offset = static_cast<size_t>(index) * 6u;
        if (offset + 5u >= batch.vertices.size()) {
            continue;
        }
        glColor3f(
            batch.vertices[offset + 3u],
            batch.vertices[offset + 4u],
            batch.vertices[offset + 5u]);
        glVertex3f(
            batch.vertices[offset],
            batch.vertices[offset + 1u],
            batch.vertices[offset + 2u]);
    }
    glEnd();
}

void drawGpuBatch(const legacy3d::GpuBatch& batch) {
    drawGpuBatchTriangles(batch);

    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (uint32_t index : batch.lineIndices) {
        const size_t offset = static_cast<size_t>(index) * 6u;
        if (offset + 5u >= batch.vertices.size()) {
            continue;
        }
        glColor3f(
            batch.vertices[offset + 3u],
            batch.vertices[offset + 4u],
            batch.vertices[offset + 5u]);
        glVertex3f(
            batch.vertices[offset],
            batch.vertices[offset + 1u],
            batch.vertices[offset + 2u]);
    }
    glEnd();
}

void drawAc5ProjectileBatch(const legacy3d::GpuBatch& batch) {
    // The DOS capture closes a solid red AC/5 body with a yellow silhouette.
    // OTHPCK geometry is authoritative; these fixed EGA colors reproduce its
    // original draw-state rather than the generic shape-viewer shade mapping.
    glColor3f(170.0f / 255.0f, 0.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    for (uint32_t index : batch.triangleIndices) {
        const size_t offset = static_cast<size_t>(index) * 6u;
        if (offset + 2u >= batch.vertices.size()) {
            continue;
        }
        glVertex3f(
            batch.vertices[offset],
            batch.vertices[offset + 1u],
            batch.vertices[offset + 2u]);
    }
    glEnd();
    glColor3f(1.0f, 1.0f, 85.0f / 255.0f);
    glLineWidth(1.0f);
    glBegin(GL_LINES);
    for (uint32_t index : batch.lineIndices) {
        const size_t offset = static_cast<size_t>(index) * 6u;
        if (offset + 2u >= batch.vertices.size()) {
            continue;
        }
        glVertex3f(
            batch.vertices[offset],
            batch.vertices[offset + 1u],
            batch.vertices[offset + 2u]);
    }
    glEnd();
}

void drawImmediateBeam(
    const battle::CampaignBattleRenderBeamInstance& beam) {
    const Vec3 start{
        static_cast<float>(beam.start.x),
        static_cast<float>(beam.start.y),
        static_cast<float>(beam.start.z),
    };
    const Vec3 end{
        static_cast<float>(beam.end.x),
        static_cast<float>(beam.end.y),
        static_cast<float>(beam.end.z),
    };
    const Vec3 direction = normalize(subtract(end, start));
    Vec3 side = cross(direction, Vec3{0.0f, 1.0f, 0.0f});
    if (side.x * side.x + side.y * side.y + side.z * side.z < 0.0001f) {
        side = cross(direction, Vec3{1.0f, 0.0f, 0.0f});
    }
    side = normalize(side);
    const Vec3 up = normalize(cross(side, direction));
    const float width = static_cast<float>(std::max(1.0, beam.halfWidth));
    const auto offset = [](Vec3 point, Vec3 axis, float scale) {
        return Vec3{
            point.x + axis.x * scale,
            point.y + axis.y * scale,
            point.z + axis.z * scale,
        };
    };
    const std::array<Vec3, 4> base{{
        offset(start, side, width),
        offset(start, up, width),
        offset(start, side, -width),
        offset(start, up, -width),
    }};

    if (beam.color == battle::CampaignBattleBeamColor::PpcCyan) {
        glColor3f(0.0f, 170.0f / 255.0f, 170.0f / 255.0f);
    } else {
        glColor3f(1.0f, 1.0f, 85.0f / 255.0f);
    }
    glBegin(GL_TRIANGLES);
    for (size_t edge = 0; edge < base.size(); ++edge) {
        const Vec3& a = base[edge];
        const Vec3& b = base[(edge + 1u) % base.size()];
        glVertex3f(end.x, end.y, end.z);
        glVertex3f(a.x, a.y, a.z);
        glVertex3f(b.x, b.y, b.z);
    }
    glEnd();
}

const mech3d::ModelAssemblyComponent* findComponent(
    const mech3d::MechRenderInstance& instance,
    int componentId) {
    const auto found = std::find_if(
        instance.assembly.components.begin(),
        instance.assembly.components.end(),
        [componentId](const mech3d::ModelAssemblyComponent& component) {
            return component.componentId == componentId;
        });
    return found == instance.assembly.components.end() ? nullptr : &*found;
}

void drawLoadedVisual(const LoadedVisual& visual) {
    const mech3d::Bounds& bounds = visual.renderInstance.assembly.bounds;
    glPushMatrix();
    glTranslatef(
        static_cast<float>(visual.source.transform.x),
        static_cast<float>(visual.source.transform.y),
        static_cast<float>(visual.source.transform.z));
    glRotatef(
        static_cast<float>(visual.source.transform.headingRadians * 180.0 / kPi),
        0.0f,
        1.0f,
        0.0f);
    if (bounds.valid) {
        glTranslatef(-bounds.center.x, -bounds.min.y, -bounds.center.z);
    }

    for (const mech3d::ComponentRenderRange& range : visual.renderInstance.ranges) {
        if (!mech3d::isResolvedMechComponentVisible(
                visual.renderInstance.assembly,
                visual.runtimeState,
                range.componentId)) {
            continue;
        }
        const mech3d::ModelAssemblyComponent* component =
            findComponent(visual.renderInstance, range.componentId);
        if (component == nullptr) {
            continue;
        }
        glPushMatrix();
        glMultMatrixf(component->worldMatrix.data());

        glBegin(GL_TRIANGLES);
        const size_t triangleEnd = std::min(
            visual.renderInstance.batch.triangleIndices.size(),
            static_cast<size_t>(range.triangleIndexOffset + range.triangleIndexCount));
        for (size_t i = range.triangleIndexOffset; i < triangleEnd; ++i) {
            const uint32_t index = visual.renderInstance.batch.triangleIndices[i];
            const size_t offset = static_cast<size_t>(index) * 6u;
            if (offset + 5u >= visual.renderInstance.batch.vertices.size()) {
                continue;
            }
            glColor3f(
                visual.renderInstance.batch.vertices[offset + 3u],
                visual.renderInstance.batch.vertices[offset + 4u],
                visual.renderInstance.batch.vertices[offset + 5u]);
            glVertex3f(
                visual.renderInstance.batch.vertices[offset],
                visual.renderInstance.batch.vertices[offset + 1u],
                visual.renderInstance.batch.vertices[offset + 2u]);
        }
        glEnd();

        glBegin(GL_LINES);
        const size_t lineEnd = std::min(
            visual.renderInstance.batch.lineIndices.size(),
            static_cast<size_t>(range.lineIndexOffset + range.lineIndexCount));
        for (size_t i = range.lineIndexOffset; i < lineEnd; ++i) {
            const uint32_t index = visual.renderInstance.batch.lineIndices[i];
            const size_t offset = static_cast<size_t>(index) * 6u;
            if (offset + 5u >= visual.renderInstance.batch.vertices.size()) {
                continue;
            }
            glColor3f(
                visual.renderInstance.batch.vertices[offset + 3u],
                visual.renderInstance.batch.vertices[offset + 4u],
                visual.renderInstance.batch.vertices[offset + 5u]);
            glVertex3f(
                visual.renderInstance.batch.vertices[offset],
                visual.renderInstance.batch.vertices[offset + 1u],
                visual.renderInstance.batch.vertices[offset + 2u]);
        }
        glEnd();
        glPopMatrix();
    }
    glPopMatrix();
}

std::array<float, 3> terrainGroundColor(const std::optional<int>& environmentId) {
    if (environmentId.has_value() && *environmentId == 0) {
        return {0.69f, 0.36f, 0.02f};
    }
    if (environmentId.has_value() && *environmentId == 2) {
        return {0.79f, 0.91f, 0.90f};
    }
    return {0.11f, 0.47f, 0.13f};
}

std::array<float, 3> terrainSkyColor(const std::optional<int>& environmentId) {
    if (environmentId.has_value() && *environmentId == 0) {
        return {0.08f, 0.035f, 0.005f};
    }
    if (environmentId.has_value() && *environmentId == 2) {
        return {0.08f, 0.50f, 0.62f};
    }
    return {0.035f, 0.16f, 0.36f};
}

} // namespace

CampaignBattleFarGroundApron campaignBattleFarGroundApron(
    const battle::CampaignBattleRenderTerrainResources& terrain,
    double viewDistance) {
    CampaignBattleFarGroundApron apron;
    if (!terrain.valid ||
        terrain.boundsMinX >= terrain.boundsMaxX ||
        terrain.boundsMinZ >= terrain.boundsMaxZ ||
        !std::isfinite(viewDistance) || viewDistance <= 0.0) {
        return apron;
    }
    const double margin = viewDistance * 1.25;
    apron.valid = true;
    apron.minX = terrain.boundsMinX - margin;
    apron.maxX = terrain.boundsMaxX + margin;
    apron.minZ = terrain.boundsMinZ - margin;
    apron.maxZ = terrain.boundsMaxZ + margin;
    apron.groundY = 0.0;
    return apron;
}

CampaignBattleExternalCameraOrbit campaignBattleExternalCameraAfterDrag(
    CampaignBattleExternalCameraOrbit orbit,
    int deltaX,
    int deltaY) {
    orbit.yawOffsetDegrees -= static_cast<float>(deltaX) * 0.28f;
    orbit.pitchDegrees += static_cast<float>(deltaY) * 0.18f;
    orbit.pitchDegrees = std::clamp(orbit.pitchDegrees, 8.0f, 62.0f);
    return orbit;
}

CampaignBattleExternalCameraOrbit campaignBattleExternalCameraAfterWheel(
    CampaignBattleExternalCameraOrbit orbit,
    int wheelDelta) {
    if (wheelDelta != 0) {
        orbit.distance *= wheelDelta > 0 ? 0.9f : 1.1f;
        orbit.distance = std::clamp(orbit.distance, 900.0f, 8500.0f);
    }
    return orbit;
}

CampaignBattleGlVirtualViewportRect campaignBattleGlVirtualViewportRect(
    battle::CampaignBattlePresentationMode mode,
    battle::CampaignCockpitFamily cockpitFamily) {
    if (mode == battle::CampaignBattlePresentationMode::External) {
        return {0, 0, 320, 200};
    }
    if (mode == battle::CampaignBattlePresentationMode::MissionStatus) {
        const CampaignCockpitRect map = campaignMissionStatusMapRect();
        return {map.x, map.y, map.width, map.height};
    }
    if (mode == battle::CampaignBattlePresentationMode::CockpitCommandMap) {
        const CampaignCockpitRect map = campaignCockpitCommandMapRect();
        return {map.x, map.y, map.width, map.height};
    }
    if (mode == battle::CampaignBattlePresentationMode::Cockpit) {
        const CampaignCockpitRect& viewport = campaignCockpitLayout(cockpitFamily).viewport;
        return {viewport.x, viewport.y, viewport.width, viewport.height};
    }
    return {25, 43, 174, 130};
}

bool campaignBattleVisualHiddenInCockpit(
    const battle::CampaignBattleRenderVisualInstance& visual) {
    // The camera occupies only the controlled mech. Other player-team
    // combatants are ordinary world visuals and must remain visible.
    return visual.playerControlled;
}

int campaignBattleTargetRectangleCount(battle::BattleTeam team) {
    if (team == battle::BattleTeam::Player) {
        return 2;
    }
    return team == battle::BattleTeam::Opposing ? 1 : 0;
}

int campaignBattleObjectiveTargetRectangleCount(bool playerProtected) {
    return playerProtected ? 2 : 1;
}

struct CampaignBattleGlViewport::Impl {
    HINSTANCE instance = nullptr;
    HWND parent = nullptr;
    HWND hwnd = nullptr;
    HDC hdc = nullptr;
    HGLRC hrc = nullptr;
    int width = 1;
    int height = 1;
    bool visible = false;
    bool sceneReady = false;
    std::string error;
    battle::CampaignBattleRenderScenePackage scene;
    legacy3d::TerrainMesh terrainMesh;
    legacy3d::GpuBatch terrainObjectBatch;
    legacy3d::GpuBatch terrainObjectDitherBatch;
    size_t terrainObjectCount = 0;
    size_t terrainObjectRecordCount = 0;
    std::string terrainSignature;
    std::vector<LoadedVisual> visuals;
    std::vector<LoadedVisual> projectileMountReferences;
    std::map<std::string, legacy3d::GpuBatch> projectileBatches;
    std::map<std::string, ImpactStageGeometry> impactStages;
    legacy3d::GpuBatch objectiveBatch;
    BatchBounds objectiveBounds;
    std::string objectiveSignature;
    RecordCache recordCache;
    CampaignCockpitDynamicLayers cockpitLayers;
    std::string cockpitLayerSignature;
    std::string uploadedCockpitLayerSignature;
    std::vector<UploadedCockpitSprite> cockpitStrutTextures;
    std::vector<UploadedCockpitSprite> cockpitWidgetTextures;
    std::vector<UploadedCockpitSprite> cockpitHudNumberTextures;
    CampaignBattleGlCockpitOptions cockpitOptions;
    CampaignBattleExternalCameraOrbit externalCamera;
    uint64_t lastCollisionFlashSequence = 0;
    int collisionFlashFramesRemaining = 0;
    bool externalCameraDragging = false;
    int lastMouseX = 0;
    int lastMouseY = 0;
    CampaignEgaPaletteColors mapPalette;

    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        Impl* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = reinterpret_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self == nullptr) {
            return DefWindowProcW(window, message, wparam, lparam);
        }
        switch (message) {
        case WM_SIZE:
            self->width = std::max(1, static_cast<int>(LOWORD(lparam)));
            self->height = std::max(1, static_cast<int>(HIWORD(lparam)));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_RBUTTONDOWN:
            if (self->scene.mode == battle::CampaignBattlePresentationMode::External) {
                self->externalCameraDragging = true;
                self->lastMouseX = static_cast<short>(LOWORD(lparam));
                self->lastMouseY = static_cast<short>(HIWORD(lparam));
                SetCapture(window);
                return 0;
            }
            break;
        case WM_RBUTTONUP:
            if (self->externalCameraDragging) {
                self->externalCameraDragging = false;
                if (GetCapture() == window) {
                    ReleaseCapture();
                }
                return 0;
            }
            break;
        case WM_MOUSEMOVE:
            if (self->externalCameraDragging &&
                self->scene.mode == battle::CampaignBattlePresentationMode::External) {
                const int x = static_cast<short>(LOWORD(lparam));
                const int y = static_cast<short>(HIWORD(lparam));
                self->externalCamera = campaignBattleExternalCameraAfterDrag(
                    self->externalCamera,
                    x - self->lastMouseX,
                    y - self->lastMouseY);
                self->lastMouseX = x;
                self->lastMouseY = y;
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_MOUSEWHEEL:
            if (self->scene.mode == battle::CampaignBattlePresentationMode::External) {
                self->externalCamera = campaignBattleExternalCameraAfterWheel(
                    self->externalCamera,
                    GET_WHEEL_DELTA_WPARAM(wparam));
                InvalidateRect(window, nullptr, FALSE);
                return 0;
            }
            break;
        case WM_CAPTURECHANGED:
            self->externalCameraDragging = false;
            break;
        case WM_SETFOCUS:
            if (self->parent != nullptr) {
                SetFocus(self->parent);
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            BeginPaint(window, &paint);
            EndPaint(window, &paint);
            self->renderFrame();
            return 0;
        }
        default:
            return DefWindowProcW(window, message, wparam, lparam);
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }

    bool initializeWindow(HINSTANCE appInstance, HWND parentWindow) {
        if (hwnd != nullptr) {
            return true;
        }
        instance = appInstance;
        parent = parentWindow;
        const wchar_t* className = L"MWCampaignBattleGlViewport";
        WNDCLASSW wc{};
        wc.style = CS_OWNDC;
        wc.lpfnWndProc = &Impl::windowProc;
        wc.hInstance = instance;
        wc.lpszClassName = className;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!GetClassInfoW(instance, className, &wc)) {
            wc = {};
            wc.style = CS_OWNDC;
            wc.lpfnWndProc = &Impl::windowProc;
            wc.hInstance = instance;
            wc.lpszClassName = className;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            if (!RegisterClassW(&wc)) {
                error = "could not register campaign OpenGL viewport class";
                return false;
            }
        }

        hwnd = CreateWindowExW(
            WS_EX_NOPARENTNOTIFY,
            className,
            L"",
            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            0,
            0,
            1,
            1,
            parent,
            nullptr,
            instance,
            this);
        if (hwnd == nullptr) {
            error = "could not create campaign OpenGL child viewport";
            return false;
        }

        hdc = GetDC(hwnd);
        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;
        const int format = hdc != nullptr ? ChoosePixelFormat(hdc, &pfd) : 0;
        if (format == 0 || SetPixelFormat(hdc, format, &pfd) == FALSE) {
            error = "could not set campaign OpenGL pixel format";
            return false;
        }
        hrc = wglCreateContext(hdc);
        if (hrc == nullptr || wglMakeCurrent(hdc, hrc) == FALSE) {
            error = "could not create campaign WGL context";
            return false;
        }
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);
        wglMakeCurrent(nullptr, nullptr);
        error.clear();
        return true;
    }

    std::string makeTerrainSignature(
        const battle::CampaignBattleRenderTerrainResources& terrain) const {
        std::ostringstream out;
        out << terrain.scenarioIndex << ':'
            << terrain.boundsMinX << ':' << terrain.boundsMaxX << ':'
            << terrain.boundsMinZ << ':' << terrain.boundsMaxZ << ':'
            << (terrain.environmentId.has_value() ? *terrain.environmentId : -1) << ':'
            << terrain.palettePath.string();
        for (const std::filesystem::path& path : terrain.gridPaths) {
            out << ':' << path.string();
        }
        out << ':' << terrain.terrainShapePath.string();
        for (const std::filesystem::path& path : terrain.worldPaths) {
            out << ':' << path.string();
        }
        return out.str();
    }

    std::string makeObjectiveSignature(
        const battle::CampaignBattleRenderObjectiveInstance& objective) const {
        std::ostringstream out;
        out << objective.valid << ':' << objective.staticModel.valid << ':'
            << objective.modelResourcePath.string() << ':' << objective.staticModel.recordIndex;
        return out.str();
    }

    std::string makeCockpitLayerSignature(
        const battle::CampaignBattleRenderCockpitResources& cockpit) const {
        std::ostringstream out;
        out << static_cast<int>(cockpit.family) << ':'
            << cockpit.palettePath.string() << ':'
            << cockpit.strutsPath.string() << ':'
            << cockpit.widgetsPath.string() << ':'
            << cockpit.hudNumbersPath.string() << ':'
            << cockpit.hudFontPath.string();
        return out.str();
    }

    bool loadScene(const battle::CampaignBattleRenderScenePackage& nextScene) {
        try {
            const std::string nextTerrainSignature = makeTerrainSignature(nextScene.terrain);
            if (terrainSignature != nextTerrainSignature) {
                LoadedTerrainGeometry terrain = loadTerrainGeometry(nextScene.terrain);
                terrainMesh = std::move(terrain.mesh);
                terrainObjectBatch = std::move(terrain.objectBatch);
                terrainObjectDitherBatch = std::move(terrain.objectDitherBatch);
                terrainObjectCount = terrain.objectCount;
                terrainObjectRecordCount = terrain.objectRecordCount;
                mapPalette = loadCampaignEgaPaletteColors(nextScene.terrain.palettePath);
                if (!mapPalette.valid) {
                    throw std::runtime_error(mapPalette.reason);
                }
                terrainSignature = nextTerrainSignature;
            }

            std::map<uint32_t, LoadedVisual> previous;
            for (LoadedVisual& visual : visuals) {
                previous.emplace(visual.source.entityId.value, std::move(visual));
            }
            std::vector<LoadedVisual> nextVisuals;
            nextVisuals.reserve(nextScene.combatantVisuals.size());
            for (const battle::CampaignBattleRenderVisualInstance& visual : nextScene.combatantVisuals) {
                const std::string signature = visualSignature(visual);
                auto found = previous.find(visual.entityId.value);
                if (found != previous.end() && found->second.signature == signature) {
                    found->second.source = visual;
                    updateLoadedVisualPresentationPose(found->second);
                    nextVisuals.push_back(std::move(found->second));
                } else {
                    nextVisuals.push_back(loadVisual(visual, recordCache));
                }
            }
            visuals = std::move(nextVisuals);

            std::map<uint32_t, LoadedVisual> previousMountReferences;
            for (LoadedVisual& visual : projectileMountReferences) {
                previousMountReferences.emplace(
                    visual.source.entityId.value,
                    std::move(visual));
            }
            std::vector<LoadedVisual> nextMountReferences;
            nextMountReferences.reserve(nextScene.combatantVisuals.size());
            for (const battle::CampaignBattleRenderVisualInstance& visual :
                 nextScene.combatantVisuals) {
                const battle::CampaignBattleRenderVisualInstance reference =
                    projectileMountReferenceSource(visual);
                const std::string signature = visualSignature(reference);
                auto found = previousMountReferences.find(
                    reference.entityId.value);
                if (found != previousMountReferences.end() &&
                    found->second.signature == signature) {
                    found->second.source = reference;
                    updateLoadedVisualPresentationPose(found->second);
                    nextMountReferences.push_back(std::move(found->second));
                } else {
                    nextMountReferences.push_back(loadVisual(
                        reference,
                        recordCache));
                }
            }
            projectileMountReferences = std::move(nextMountReferences);

            for (const battle::CampaignBattleRenderProjectileInstance& projectile :
                 nextScene.projectileVisuals) {
                const std::string key = projectileBatchKey(projectile);
                if (projectileBatches.find(key) == projectileBatches.end()) {
                    legacy3d::GpuBatch batch = loadProjectileBatch(
                        projectile,
                        recordCache);
                    if (!validGpuBatch(batch) || batch.vertices.empty()) {
                        throw std::runtime_error(
                            "campaign OpenGL projectile produced no valid GPU vertices");
                    }
                    projectileBatches.emplace(key, std::move(batch));
                }
            }
            for (const battle::CampaignBattleRenderImpactInstance& impact :
                 nextScene.impactVisuals) {
                const std::string key = impactStageKey(impact);
                if (impactStages.find(key) == impactStages.end()) {
                    impactStages.emplace(
                        key,
                        loadImpactStage(impact, recordCache));
                }
            }

            const std::string nextObjectiveSignature = makeObjectiveSignature(nextScene.objective);
            if (objectiveSignature != nextObjectiveSignature) {
                objectiveBatch = loadObjectiveBatch(nextScene.objective);
                objectiveBounds = batchBounds(objectiveBatch);
                objectiveSignature = nextObjectiveSignature;
            }
            if (nextScene.mode == battle::CampaignBattlePresentationMode::Cockpit) {
                const std::string nextCockpitLayerSignature =
                    makeCockpitLayerSignature(nextScene.cockpit);
                if (cockpitLayerSignature != nextCockpitLayerSignature) {
                    cockpitLayers = loadCampaignCockpitDynamicLayers(nextScene.cockpit);
                    if (!cockpitLayers.valid) {
                        throw std::runtime_error(cockpitLayers.reason);
                    }
                    cockpitLayerSignature = nextCockpitLayerSignature;
                    uploadedCockpitLayerSignature.clear();
                }
            }
            if (nextScene.mode == battle::CampaignBattlePresentationMode::Cockpit &&
                nextScene.playerCollisionFlashEvent &&
                nextScene.playerCollisionSequence != 0u &&
                nextScene.playerCollisionSequence != lastCollisionFlashSequence) {
                lastCollisionFlashSequence = nextScene.playerCollisionSequence;
                collisionFlashFramesRemaining = 2;
            }
            scene = nextScene;
            sceneReady = !terrainMesh.vertices.empty() &&
                         visuals.size() == scene.combatantVisuals.size() &&
                         (scene.mode != battle::CampaignBattlePresentationMode::Cockpit ||
                          cockpitLayers.valid);
            error.clear();
            return sceneReady;
        } catch (const std::exception& exception) {
            sceneReady = false;
            error = exception.what();
            return false;
        }
    }

    const LoadedVisual* playerVisual() const {
        const auto controlled = std::find_if(
            visuals.begin(),
            visuals.end(),
            [](const LoadedVisual& visual) {
                return visual.source.playerControlled;
            });
        if (controlled != visuals.end()) {
            return &*controlled;
        }
        const auto fallback = std::find_if(
            visuals.begin(),
            visuals.end(),
            [](const LoadedVisual& visual) {
                return visual.source.team == battle::BattleTeam::Player;
            });
        return fallback == visuals.end() ? nullptr : &*fallback;
    }

    void applyCamera() const {
        if (scene.mode == battle::CampaignBattlePresentationMode::Cockpit && scene.camera.valid) {
            const float heading = static_cast<float>(scene.camera.transform.headingRadians);
            const Vec3 eye{
                static_cast<float>(scene.camera.transform.x),
                static_cast<float>(scene.camera.transform.y + scene.cockpitCameraBobOffsetY),
                static_cast<float>(scene.camera.transform.z),
            };
            const Vec3 target{
                eye.x + std::sin(heading) * 1000.0f,
                eye.y,
                eye.z + std::cos(heading) * 1000.0f,
            };
            applyLookAt(eye, target);
            return;
        }

        const LoadedVisual* player = playerVisual();
        if (player == nullptr) {
            applyLookAt(Vec3{43000.0f, 3200.0f, 19000.0f}, Vec3{43000.0f, 0.0f, 23500.0f});
            return;
        }
        const float bodyHeading = static_cast<float>(player->source.transform.headingRadians);
        const float orbitHeading = bodyHeading +
            externalCamera.yawOffsetDegrees * static_cast<float>(kPi) / 180.0f;
        const float orbitPitch =
            externalCamera.pitchDegrees * static_cast<float>(kPi) / 180.0f;
        const float horizontal = std::cos(orbitPitch) * externalCamera.distance;
        const float forwardX = std::sin(orbitHeading);
        const float forwardZ = std::cos(orbitHeading);
        const Vec3 target{
            static_cast<float>(player->source.transform.x),
            static_cast<float>(player->source.transform.y) + 180.0f,
            static_cast<float>(player->source.transform.z),
        };
        const Vec3 eye{
            target.x - forwardX * horizontal,
            static_cast<float>(player->source.transform.y) +
                std::sin(orbitPitch) * externalCamera.distance,
            target.z - forwardZ * horizontal,
        };
        applyLookAt(eye, target);
    }

    void drawTerrain() const {
        const std::array<float, 3> ground = terrainGroundColor(scene.terrain.environmentId);
        glPushMatrix();
        glTranslatef(
            static_cast<float>(scene.terrain.boundsMinX),
            0.0f,
            static_cast<float>(scene.terrain.boundsMinZ));
        glBegin(GL_TRIANGLES);
        for (uint32_t index : terrainMesh.indices) {
            if (index >= terrainMesh.vertices.size()) {
                continue;
            }
            const legacy3d::TerrainMeshVertex& vertex = terrainMesh.vertices[index];
            // GRD samples remain useful to the original-style maps, but their
            // bit fields are not proven to describe coloured ground patches.
            // Keep the 3D ground uniform and let placed WLD/TERPCK objects carry
            // all visible terrain detail.
            glColor3fv(ground.data());
            glVertex3f(vertex.x, vertex.y, vertex.z);
        }
        glEnd();
        glPopMatrix();
    }

    void drawFarGroundApron() const {
        const CampaignBattleFarGroundApron apron =
            campaignBattleFarGroundApron(scene.terrain);
        if (!apron.valid) {
            return;
        }
        const float terrainMinX = static_cast<float>(scene.terrain.boundsMinX);
        const float terrainMaxX = static_cast<float>(scene.terrain.boundsMaxX);
        const float terrainMinZ = static_cast<float>(scene.terrain.boundsMinZ);
        const float terrainMaxZ = static_cast<float>(scene.terrain.boundsMaxZ);
        const float apronMinX = static_cast<float>(apron.minX);
        const float apronMaxX = static_cast<float>(apron.maxX);
        const float apronMinZ = static_cast<float>(apron.minZ);
        const float apronMaxZ = static_cast<float>(apron.maxZ);
        const float y = static_cast<float>(apron.groundY);
        const std::array<float, 3> ground = terrainGroundColor(scene.terrain.environmentId);
        glColor3fv(ground.data());
        glBegin(GL_QUADS);
        glVertex3f(apronMinX, y, apronMinZ);
        glVertex3f(terrainMinX, y, apronMinZ);
        glVertex3f(terrainMinX, y, apronMaxZ);
        glVertex3f(apronMinX, y, apronMaxZ);

        glVertex3f(terrainMaxX, y, apronMinZ);
        glVertex3f(apronMaxX, y, apronMinZ);
        glVertex3f(apronMaxX, y, apronMaxZ);
        glVertex3f(terrainMaxX, y, apronMaxZ);

        glVertex3f(terrainMinX, y, apronMinZ);
        glVertex3f(terrainMaxX, y, apronMinZ);
        glVertex3f(terrainMaxX, y, terrainMinZ);
        glVertex3f(terrainMinX, y, terrainMinZ);

        glVertex3f(terrainMinX, y, terrainMaxZ);
        glVertex3f(terrainMaxX, y, terrainMaxZ);
        glVertex3f(terrainMaxX, y, apronMaxZ);
        glVertex3f(terrainMinX, y, apronMaxZ);
        glEnd();
    }

    void drawTerrainObjects() const {
        drawGpuBatchTriangles(terrainObjectBatch);
        if (terrainObjectDitherBatch.vertices.empty()) {
            return;
        }
        static const std::array<uint8_t, 128> kCheckerStipple =
            legacy3d::terrainCheckerStipple4x4();
        glEnable(GL_POLYGON_STIPPLE);
        glPolygonStipple(kCheckerStipple.data());
        drawGpuBatchTriangles(terrainObjectDitherBatch);
        glDisable(GL_POLYGON_STIPPLE);
    }

    static uint8_t missionMapAllowedExitMask(
        const battle::CampaignBattleRenderScenePackage& scene) {
        if (!scene.battlefieldBoundary.valid ||
            !scene.battlefieldBoundary.exitMaskSemanticsProven ||
            !scene.setup.valid) {
            return 0;
        }
        uint8_t mask = 0;
        if (scene.setup.playerMode == 1) {
            mask = scene.battlefieldBoundary.playerAllowedExitMask;
        }
        if (scene.setup.opposingMode == 1) {
            mask = scene.battlefieldBoundary.opposingAllowedExitMask;
        }
        return mask;
    }

    static std::array<float, 3> bgraRgb(uint32_t bgra) {
        return {
            static_cast<float>((bgra >> 16u) & 0xffu) / 255.0f,
            static_cast<float>((bgra >> 8u) & 0xffu) / 255.0f,
            static_cast<float>(bgra & 0xffu) / 255.0f,
        };
    }

    void renderCommandMapFrame() const {
        const CampaignCockpitRect mapRect =
            scene.mode == battle::CampaignBattlePresentationMode::MissionStatus
                ? campaignMissionStatusMapRect()
                : campaignCockpitCommandMapRect();
        const int mapWidth = mapRect.width;
        const int mapHeight = mapRect.height;
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_POLYGON_STIPPLE);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(mapWidth),
                static_cast<double>(mapHeight), 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        const int environment = scene.terrain.environmentId.value_or(1);
        const std::array<float, 3> ground =
            environment == 0
                ? std::array<float, 3>{170.0f / 255.0f, 85.0f / 255.0f, 0.0f}
                : environment == 2
                      ? std::array<float, 3>{1.0f, 1.0f, 1.0f}
                      : std::array<float, 3>{0.0f, 170.0f / 255.0f, 0.0f};
        glClearColor(ground[0], ground[1], ground[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const double centerX =
            (scene.battlefieldBoundary.worldMinX +
             scene.battlefieldBoundary.worldMaxX) * 0.5;
        const double centerZ =
            (scene.battlefieldBoundary.worldMinZ +
             scene.battlefieldBoundary.worldMaxZ) * 0.5;
        constexpr double scale = campaignBattleMapWorldUnitsPerPixel();
        const auto mapPoint = [&](double worldX, double worldZ) {
            return std::pair<int, int>{
                mapWidth / 2 + static_cast<int>(std::lround((worldX - centerX) / scale)),
                mapHeight / 2 + static_cast<int>(std::lround((worldZ - centerZ) / scale)),
            };
        };
        const auto drawBatch = [&](const legacy3d::GpuBatch& batch, bool dithered) {
            static const std::array<uint8_t, 128> checker =
                legacy3d::terrainCheckerStipple4x4();
            if (dithered) {
                glEnable(GL_POLYGON_STIPPLE);
                glPolygonStipple(checker.data());
            }
            glBegin(GL_TRIANGLES);
            for (uint32_t index : batch.triangleIndices) {
                const size_t offset = static_cast<size_t>(index) * 6u;
                if (offset + 5u >= batch.vertices.size()) {
                    continue;
                }
                const auto [x, y] = mapPoint(
                    batch.vertices[offset], batch.vertices[offset + 2u]);
                glColor3f(batch.vertices[offset + 3u],
                          batch.vertices[offset + 4u],
                          batch.vertices[offset + 5u]);
                glVertex2i(x, y);
            }
            glEnd();
            if (dithered) {
                glDisable(GL_POLYGON_STIPPLE);
            }
        };
        drawBatch(terrainObjectBatch, false);
        drawBatch(terrainObjectDitherBatch, true);

        const auto drawRect = [&](int x, int y, int rectWidth, int rectHeight,
                                  const std::array<float, 3>& color) {
            const int left = std::max(0, x);
            const int top = std::max(0, y);
            const int right = std::min(mapWidth, x + rectWidth);
            const int bottom = std::min(mapHeight, y + rectHeight);
            if (left >= right || top >= bottom) {
                return;
            }
            glColor3fv(color.data());
            glBegin(GL_QUADS);
            glVertex2i(left, top);
            glVertex2i(right, top);
            glVertex2i(right, bottom);
            glVertex2i(left, bottom);
            glEnd();
        };
        for (const battle::CampaignBattleRenderVisualInstance& visual : scene.combatantVisuals) {
            const auto [x, y] = mapPoint(visual.transform.x, visual.transform.z);
            if (visual.team == battle::BattleTeam::Player ||
                visual.playerControlled) {
                const int lanceSlot = campaignBattlePlayerLanceSlot(
                    visual.rosterSourceSlot,
                    visual.playerControlled);
                const CampaignBattleMapPlayerMarker marker =
                    campaignBattleMapPlayerMarker(
                        lanceSlot,
                        scene.mode,
                        scene.terrain.environmentId);
                const std::array<float, 3> color = bgraRgb(marker.bgra);
                if (marker.contrastOutline) {
                    const std::array<float, 3> outline{0.0f, 0.0f, 0.0f};
                    for (size_t pixel = 0; pixel < marker.pixels.size(); ++pixel) {
                        if (marker.pixels[pixel] == 0u) {
                            continue;
                        }
                        const int markerX =
                            x + static_cast<int>(pixel % 3u) - 1;
                        const int markerY =
                            y + static_cast<int>(pixel / 3u) - 1;
                        drawRect(markerX - 1, markerY - 1, 3, 3, outline);
                    }
                }
                for (size_t pixel = 0; pixel < marker.pixels.size(); ++pixel) {
                    if (marker.pixels[pixel] == 0u) {
                        continue;
                    }
                    drawRect(
                        x + static_cast<int>(pixel % 3u) - 1,
                        y + static_cast<int>(pixel / 3u) - 1,
                        1,
                        1,
                        color);
                }
                continue;
            }
            const std::array<float, 3> color =
                visual.team == battle::BattleTeam::Opposing
                          ? std::array<float, 3>{0.86f, 0.0f, 0.0f}
                          : std::array<float, 3>{0.86f, 0.86f, 0.0f};
            drawRect(x, y, 2, 2, color);
        }
        if (scene.objective.valid) {
            const auto [x, y] = mapPoint(
                scene.objective.transform.x, scene.objective.transform.z);
            drawRect(x, y, 2, 2, {0.0f, 0.86f, 0.86f});
        }

        if (scene.battlefieldBoundary.valid &&
            scene.battlefieldBoundary.mapProjectionProven) {
            const auto [left, top] = mapPoint(
                scene.battlefieldBoundary.worldMinX,
                scene.battlefieldBoundary.worldMinZ);
            const auto [right, bottom] = mapPoint(
                scene.battlefieldBoundary.worldMaxX,
                scene.battlefieldBoundary.worldMaxZ);
            const size_t ordinaryIndex = environment == 0 ? 15u : (environment == 2 ? 8u : 15u);
            const size_t allowedIndex = environment == 0 ? 4u : (environment == 2 ? 14u : 11u);
            const std::array<float, 3> ordinary = bgraRgb(mapPalette.bgra[ordinaryIndex]);
            const std::array<float, 3> allowed = bgraRgb(mapPalette.bgra[allowedIndex]);
            const uint8_t allowedMask = missionMapAllowedExitMask(scene);
            const auto edgeColor = [&](battle::BattlefieldBoundaryEdge edge) -> const std::array<float, 3>& {
                return (allowedMask & battle::battlefieldBoundaryEdgeMask(edge)) != 0u
                           ? allowed
                           : ordinary;
            };
            const int phase = static_cast<int>(scene.authoritativeTickIndex & 7u) - 4;
            const auto drawEdgeClipped = [&](int x, int y, int rectWidth, int rectHeight,
                                             int edgeLeft, int edgeTop,
                                             int edgeWidth, int edgeHeight,
                                             const std::array<float, 3>& color) {
                const int clippedLeft = std::max(x, edgeLeft);
                const int clippedTop = std::max(y, edgeTop);
                const int clippedRight = std::min(x + rectWidth, edgeLeft + edgeWidth);
                const int clippedBottom = std::min(y + rectHeight, edgeTop + edgeHeight);
                if (clippedLeft < clippedRight && clippedTop < clippedBottom) {
                    drawRect(clippedLeft, clippedTop,
                             clippedRight - clippedLeft,
                             clippedBottom - clippedTop, color);
                }
            };
            for (int x = left + phase; x <= right; x += 8) {
                drawEdgeClipped(x, top, 5, 1,
                                left, top, right - left + 1, 1,
                                edgeColor(battle::BattlefieldBoundaryEdge::North));
            }
            for (int x = right - phase; x >= left; x -= 8) {
                drawEdgeClipped(x - 4, bottom, 5, 1,
                                left, bottom, right - left + 1, 1,
                                edgeColor(battle::BattlefieldBoundaryEdge::South));
            }
            for (int y = top + phase - 5; y <= bottom; y += 8) {
                drawEdgeClipped(right, y, 1, 5,
                                right, top, 1, bottom - top + 1,
                                edgeColor(battle::BattlefieldBoundaryEdge::East));
            }
            for (int y = bottom - phase + 5; y >= top; y -= 8) {
                drawEdgeClipped(left, y - 4, 1, 5,
                                left, top, 1, bottom - top + 1,
                                edgeColor(battle::BattlefieldBoundaryEdge::West));
            }
        }
        glEnable(GL_DEPTH_TEST);
    }

    void drawObjective() const {
        if (objectiveBatch.vertices.empty() || !scene.objective.valid) {
            return;
        }
        glPushMatrix();
        glTranslatef(
            static_cast<float>(scene.objective.transform.x),
            static_cast<float>(scene.objective.transform.y),
            static_cast<float>(scene.objective.transform.z));
        glRotatef(
            static_cast<float>(scene.objective.transform.headingRadians * 180.0 / kPi),
            0.0f,
            1.0f,
            0.0f);
        if (objectiveBounds.valid) {
            glTranslatef(
                -(objectiveBounds.minX + objectiveBounds.maxX) * 0.5f,
                -objectiveBounds.minY,
                -(objectiveBounds.minZ + objectiveBounds.maxZ) * 0.5f);
        }
        drawGpuBatch(objectiveBatch);
        glPopMatrix();
    }

    void drawProjectiles() const {
        for (const battle::CampaignBattleRenderProjectileInstance& projectile :
             scene.projectileVisuals) {
            const auto found = projectileBatches.find(
                projectileBatchKey(projectile));
            if (found == projectileBatches.end()) {
                continue;
            }
            const MountedProjectilePose mountedPose =
                mountedProjectilePoseForScene(
                    projectileMountReferences,
                    projectile);
            const Vec3 direction = normalize(mountedPose.direction);
            const float headingDegrees = static_cast<float>(
                std::atan2(direction.x, direction.z) * 180.0 / kPi);
            const float pitchDegrees = static_cast<float>(
                std::asin(std::clamp(direction.y, -1.0f, 1.0f)) *
                180.0 / kPi);
            glPushMatrix();
            glTranslatef(
                mountedPose.position.x,
                mountedPose.position.y,
                mountedPose.position.z);
            glRotatef(headingDegrees, 0.0f, 1.0f, 0.0f);
            glRotatef(-pitchDegrees, 1.0f, 0.0f, 0.0f);
            glRotatef(
                static_cast<float>(projectile.spinDegrees),
                0.0f,
                0.0f,
                1.0f);
            if (projectile.recordIndex == 0) {
                drawAc5ProjectileBatch(found->second);
            } else {
                drawGpuBatch(found->second);
            }
            glPopMatrix();
        }
    }

    static std::array<float, 3> impactEgaRgb(uint8_t index) {
        constexpr float low = 170.0f / 255.0f;
        constexpr float high = 1.0f;
        constexpr float brownGreen = 85.0f / 255.0f;
        switch (index & 0x0fu) {
        case 0: return {0.0f, 0.0f, 0.0f};
        case 1: return {0.0f, 0.0f, low};
        case 2: return {0.0f, low, 0.0f};
        case 3: return {0.0f, low, low};
        case 4: return {low, 0.0f, 0.0f};
        case 5: return {low, 0.0f, low};
        case 6: return {low, brownGreen, 0.0f};
        case 7: return {low, low, low};
        case 8: return {85.0f / 255.0f, 85.0f / 255.0f, 85.0f / 255.0f};
        case 9: return {85.0f / 255.0f, 85.0f / 255.0f, high};
        case 10: return {85.0f / 255.0f, high, 85.0f / 255.0f};
        case 11: return {85.0f / 255.0f, high, high};
        case 12: return {high, 85.0f / 255.0f, 85.0f / 255.0f};
        case 13: return {high, 85.0f / 255.0f, high};
        case 14: return {high, high, 85.0f / 255.0f};
        default: return {high, high, high};
        }
    }

    static void drawImpactSphere(const ImpactSpherePrimitive& sphere) {
        constexpr int stacks = 8;
        constexpr int slices = 16;
        // Across OTHPCK 002..007 the second shade byte is the stable stage
        // colour: white, yellow, red, then grey. The first byte carries the
        // preceding/brighter shade on only part of the records. Treating it as
        // generic directional lighting visibly mixes consecutive phases, which
        // the controlled DOS captures do not show. Preserve all raw bytes in
        // the parsed command, but use one flat stage colour until the original
        // type-1 rasterizer is recovered.
        const std::array<float, 3> color =
            impactEgaRgb(sphere.shadeBytes[1]);
        glColor3fv(color.data());
        for (int stack = 0; stack < stacks; ++stack) {
            const float phi0 = -static_cast<float>(kPi) * 0.5f +
                static_cast<float>(kPi) * stack / stacks;
            const float phi1 = -static_cast<float>(kPi) * 0.5f +
                static_cast<float>(kPi) * (stack + 1) / stacks;
            for (int slice = 0; slice < slices; ++slice) {
                const float theta0 = static_cast<float>(2.0 * kPi) * slice / slices;
                const float theta1 = static_cast<float>(2.0 * kPi) * (slice + 1) / slices;
                glBegin(GL_QUADS);
                for (const std::pair<float, float>& angle : {
                         std::pair<float, float>{phi0, theta0},
                         std::pair<float, float>{phi0, theta1},
                         std::pair<float, float>{phi1, theta1},
                         std::pair<float, float>{phi1, theta0}}) {
                    const float radial = std::cos(angle.first);
                    glVertex3f(
                        sphere.center.x + sphere.radius * radial * std::cos(angle.second),
                        sphere.center.y + sphere.radius * std::sin(angle.first),
                        sphere.center.z + sphere.radius * radial * std::sin(angle.second));
                }
                glEnd();
            }
        }
    }

    static void drawMachineGunImpactStage(
        const ImpactStageGeometry& stage) {
        // OTHPCK 008/009 are planar temporary-effect records.  Keep their
        // authored X/Z plane facing the current camera without feeding the
        // special-case topology through the shared GpuBatch contract.
        GLfloat modelView[16]{};
        glGetFloatv(GL_MODELVIEW_MATRIX, modelView);
        modelView[0] = 1.0f;
        modelView[1] = 0.0f;
        modelView[2] = 0.0f;
        modelView[4] = 0.0f;
        modelView[5] = 1.0f;
        modelView[6] = 0.0f;
        modelView[8] = 0.0f;
        modelView[9] = 0.0f;
        modelView[10] = 1.0f;
        // Keep the planar marks just in front of the contacted surface to
        // avoid depth fighting without turning them into a screen overlay.
        modelView[14] += 2.0f;
        glLoadMatrixf(modelView);

        GLfloat previousLineWidth = 1.0f;
        glGetFloatv(GL_LINE_WIDTH, &previousLineWidth);
        glLineWidth(2.0f);
        glBegin(GL_LINES);
        for (const ImpactLinePrimitive& line : stage.lines) {
            const std::array<float, 3> color =
                impactEgaRgb(line.shadeBytes[2]);
            glColor3fv(color.data());
            glVertex3f(line.start.x, line.start.y, line.start.z);
            glVertex3f(line.end.x, line.end.y, line.end.z);
        }
        glEnd();
        glLineWidth(previousLineWidth);

        const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_CULL_FACE);
        glBegin(GL_TRIANGLES);
        for (const ImpactTrianglePrimitive& triangle : stage.triangles) {
            const std::array<float, 3> color =
                impactEgaRgb(triangle.shadeBytes[2]);
            glColor3fv(color.data());
            glVertex3f(triangle.a.x, triangle.a.y, triangle.a.z);
            glVertex3f(triangle.b.x, triangle.b.y, triangle.b.z);
            glVertex3f(triangle.c.x, triangle.c.y, triangle.c.z);
        }
        glEnd();
        if (cullEnabled == GL_TRUE) {
            glEnable(GL_CULL_FACE);
        }
    }

    void drawImpacts() const {
        for (const battle::CampaignBattleRenderImpactInstance& impact :
             scene.impactVisuals) {
            const auto found = impactStages.find(impactStageKey(impact));
            if (found == impactStages.end()) {
                continue;
            }
            glPushMatrix();
            glTranslatef(
                static_cast<float>(impact.position.x),
                static_cast<float>(impact.position.y),
                static_cast<float>(impact.position.z));
            if (impact.machineGunSequence) {
                drawMachineGunImpactStage(found->second);
            } else {
                for (const ImpactSpherePrimitive& sphere : found->second.spheres) {
                    drawImpactSphere(sphere);
                }
            }
            glPopMatrix();
        }
    }

    void drawImmediateBeams() const {
        for (const battle::CampaignBattleRenderBeamInstance& beam :
             scene.beamVisuals) {
            drawImmediateBeam(mountedBeamForScene(scene, visuals, beam));
        }
    }

    void deleteCockpitTextures() {
        const auto deleteTextures = [](std::vector<UploadedCockpitSprite>& sprites) {
            for (UploadedCockpitSprite& sprite : sprites) {
                if (sprite.texture != 0u) {
                    glDeleteTextures(1, &sprite.texture);
                }
            }
            sprites.clear();
        };
        deleteTextures(cockpitStrutTextures);
        deleteTextures(cockpitWidgetTextures);
        deleteTextures(cockpitHudNumberTextures);
    }

    std::vector<UploadedCockpitSprite> uploadCockpitSprites(
        const std::vector<CampaignCockpitSprite>& sprites) {
        std::vector<UploadedCockpitSprite> uploaded;
        uploaded.reserve(sprites.size());
        for (const CampaignCockpitSprite& source : sprites) {
            UploadedCockpitSprite sprite;
            sprite.index = source.index;
            sprite.width = source.width;
            sprite.height = source.height;
            glGenTextures(1, &sprite.texture);
            glBindTexture(GL_TEXTURE_2D, sprite.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                sprite.width,
                sprite.height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                source.rgbaPixels.data());
            uploaded.push_back(sprite);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        return uploaded;
    }

    void ensureCockpitTextures() {
        if (scene.mode != battle::CampaignBattlePresentationMode::Cockpit ||
            uploadedCockpitLayerSignature == cockpitLayerSignature) {
            return;
        }
        deleteCockpitTextures();
        cockpitStrutTextures = uploadCockpitSprites(cockpitLayers.struts);
        cockpitWidgetTextures = uploadCockpitSprites(cockpitLayers.widgets);
        cockpitHudNumberTextures = uploadCockpitSprites(cockpitLayers.hudNumbers);
        uploadedCockpitLayerSignature = cockpitLayerSignature;
    }

    static const UploadedCockpitSprite* findUploadedSprite(
        const std::vector<UploadedCockpitSprite>& sprites,
        int index) {
        const auto found = std::find_if(
            sprites.begin(),
            sprites.end(),
            [index](const UploadedCockpitSprite& sprite) { return sprite.index == index; });
        return found == sprites.end() ? nullptr : &*found;
    }

    static void drawCockpitSprite(
        const UploadedCockpitSprite& sprite,
        float x,
        float y) {
        glBindTexture(GL_TEXTURE_2D, sprite.texture);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2f(x, y);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2f(x + static_cast<float>(sprite.width), y);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2f(x + static_cast<float>(sprite.width), y + static_cast<float>(sprite.height));
        glTexCoord2f(0.0f, 1.0f);
        glVertex2f(x, y + static_cast<float>(sprite.height));
        glEnd();
    }

    void drawCockpitFontText(
        const std::string& text,
        int x,
        int y) const {
        if (!cockpitLayers.font.contains('A')) {
            return;
        }
        int destinationX = x;
        glBegin(GL_QUADS);
        for (char ch : text) {
            if (cockpitLayers.font.contains(ch)) {
                const size_t glyph = static_cast<size_t>(
                    static_cast<unsigned char>(ch) - cockpitLayers.font.firstCode);
                for (int row = 0; row < cockpitLayers.font.height; ++row) {
                    const uint8_t bits = cockpitLayers.font.rows[
                        glyph * static_cast<size_t>(cockpitLayers.font.height) + row];
                    for (int column = 0; column < cockpitLayers.font.width; ++column) {
                        if (((bits >> (7 - column)) & 1u) == 0u) {
                            continue;
                        }
                        const float left = static_cast<float>(destinationX + column);
                        const float top = static_cast<float>(y + row);
                        glVertex2f(left, top);
                        glVertex2f(left + 1.0f, top);
                        glVertex2f(left + 1.0f, top + 1.0f);
                        glVertex2f(left, top + 1.0f);
                    }
                }
            }
            destinationX += cockpitLayers.font.width;
        }
        glEnd();
    }

    static double normalizedDegrees(double degrees) {
        degrees = std::fmod(degrees, 360.0);
        return degrees < 0.0 ? degrees + 360.0 : degrees;
    }

    static double signedCompassDelta(double value, double center) {
        double delta = normalizedDegrees(value) - normalizedDegrees(center);
        if (delta >= 180.0) {
            delta -= 360.0;
        }
        if (delta < -180.0) {
            delta += 360.0;
        }
        return delta;
    }

    void drawCockpitDynamicLayers() {
        if (scene.mode != battle::CampaignBattlePresentationMode::Cockpit ||
            !cockpitLayers.valid) {
            return;
        }
        const CampaignCockpitLayout& layout = campaignCockpitLayout(scene.cockpit.family);
        const float virtualWidth = static_cast<float>(layout.viewport.width);
        const float virtualHeight = static_cast<float>(layout.viewport.height);

        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, virtualWidth, virtualHeight, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

        for (const battle::CampaignBattleRenderMachineGunFlashInstance& flash :
             scene.machineGunFlashVisuals) {
            if (const UploadedCockpitSprite* sprite =
                    findUploadedSprite(
                        cockpitWidgetTextures, flash.spriteIndex)) {
                // The decoded sprites are full 40/48-pixel muzzle plumes;
                // anchoring them flush to the viewport edge hides most of
                // their flame behind the corner struts.  Captures place the
                // visible plume about twenty virtual pixels inward.
                constexpr float kProvisionalMachineGunFlashInset = 20.0f;
                const float x = flash.rightSide
                    ? virtualWidth - static_cast<float>(sprite->width) -
                        kProvisionalMachineGunFlashInset
                    : kProvisionalMachineGunFlashInset;
                const float y = virtualHeight -
                    static_cast<float>(sprite->height);
                drawCockpitSprite(*sprite, x, y);
            }
        }

        for (const CampaignCockpitSpritePlacement& placement :
             campaignCockpitStrutPlacements(scene.cockpit.family)) {
            if (const UploadedCockpitSprite* sprite =
                    findUploadedSprite(cockpitStrutTextures, placement.spriteIndex)) {
                drawCockpitSprite(
                    *sprite,
                    static_cast<float>(placement.x - layout.viewport.x),
                    static_cast<float>(placement.y - layout.viewport.y));
            }
        }

        const LoadedVisual* player = playerVisual();
        if (player != nullptr) {
            const double bodyHeading =
                campaignCockpitDisplayHeadingDegrees(
                    player->source.transform.headingRadians);
            const double torsoDegrees = player->source.torsoYawRadians * 180.0 / kPi;
            const int topMarkerY =
                scene.cockpit.family == battle::CampaignCockpitFamily::Light ? 11 : 4;
            const int numberY = topMarkerY + 4;
            const int baselineY = numberY + 13;
            const int centerX = layout.viewport.width / 2;
            constexpr int halfWidth = 40;

            glDisable(GL_TEXTURE_2D);
            glColor3fv(cockpitOptions.hudRgb.data());
            glBegin(GL_LINES);
            for (int tick = 0; tick < 72; ++tick) {
                const int tickDegrees = tick * 5;
                const double delta = signedCompassDelta(tickDegrees, bodyHeading);
                if (std::abs(delta) > 40.0) {
                    continue;
                }
                const int x = centerX + static_cast<int>(std::lround(delta));
                const int tickHeight = tickDegrees % 30 == 0 ? 6 : (tickDegrees % 10 == 0 ? 3 : 1);
                glVertex2i(x, baselineY);
                glVertex2i(x, baselineY - tickHeight);
            }
            glVertex2i(centerX - halfWidth, baselineY);
            glVertex2i(centerX + halfWidth, baselineY);
            glEnd();

            glEnable(GL_TEXTURE_2D);
            glColor4f(
                cockpitOptions.hudRgb[0],
                cockpitOptions.hudRgb[1],
                cockpitOptions.hudRgb[2],
                1.0f);
            for (int index = 0; index < 12; ++index) {
                const double delta = signedCompassDelta(index * 30.0, bodyHeading);
                if (std::abs(delta) > 43.0) {
                    continue;
                }
                if (const UploadedCockpitSprite* digits =
                        findUploadedSprite(cockpitHudNumberTextures, index)) {
                    drawCockpitSprite(
                        *digits,
                        static_cast<float>(centerX + static_cast<int>(std::lround(delta)) - digits->width / 2),
                        static_cast<float>(numberY));
                }
            }
            if (const UploadedCockpitSprite* top =
                    findUploadedSprite(cockpitWidgetTextures, 3)) {
                drawCockpitSprite(
                    *top,
                    static_cast<float>(centerX - static_cast<int>(std::lround(torsoDegrees)) - top->width / 2),
                    static_cast<float>(topMarkerY));
            }
            if (const UploadedCockpitSprite* bottom =
                    findUploadedSprite(cockpitWidgetTextures, 2)) {
                drawCockpitSprite(
                    *bottom,
                    static_cast<float>(centerX - bottom->width / 2),
                    static_cast<float>(baselineY + 2));
            }

            const CampaignCockpitViewportHudLayout hudLayout =
                campaignCockpitViewportHudLayout(
                    scene.cockpit.family,
                    player->source.aimPitchStep);
            const int leftX = hudLayout.leftLabelX - layout.viewport.x;
            const int rightX = hudLayout.rightLabelX - layout.viewport.x;
            const int labelY = hudLayout.labelY - layout.viewport.y;
            const std::string zoomText =
                "ZOOM X" + std::to_string(cockpitOptions.zoomLevel);
            glDisable(GL_TEXTURE_2D);
            glColor3fv(cockpitOptions.hudRgb.data());
            if (hudLayout.zoomLabelOnLeft) {
                drawCockpitFontText(zoomText, leftX, labelY);
                drawCockpitFontText("AWS", rightX, labelY);
            } else {
                drawCockpitFontText("AWS", leftX, labelY);
                drawCockpitFontText(zoomText, rightX, labelY);
            }
        }

        if (scene.cockpitReactorShutdownMessageVisible) {
            // BTECH draws the 24-character warning at full-screen (88, 88)
            // with EGA palette index 0x0c.  This child viewport is positioned
            // at layout.viewport, so translate those original coordinates.
            glDisable(GL_TEXTURE_2D);
            glColor4f(1.0f, 85.0f / 255.0f, 85.0f / 255.0f, 1.0f);
            drawCockpitFontText(
                "FUSION REACTOR SHUT DOWN",
                0x58 - layout.viewport.x,
                0x58 - layout.viewport.y);
        }

        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    void drawCockpitTargetRectangle() const {
        if (scene.mode != battle::CampaignBattlePresentationMode::Cockpit ||
            scene.scannedTargetKind ==
                battle::BattleTargetScanTargetKind::None) {
            return;
        }
        battle::Transform targetTransform{};
        double boundsMinX = 0.0;
        double boundsMinY = 0.0;
        double boundsMinZ = 0.0;
        double boundsMaxX = 0.0;
        double boundsMaxY = 0.0;
        double boundsMaxZ = 0.0;
        int rectangleCount = 1;
        if (scene.scannedTargetKind ==
                battle::BattleTargetScanTargetKind::Objective) {
            if (!scene.objective.valid || !objectiveBounds.valid) {
                return;
            }
            targetTransform = scene.objective.transform;
            boundsMinX = objectiveBounds.minX;
            boundsMinY = objectiveBounds.minY;
            boundsMinZ = objectiveBounds.minZ;
            boundsMaxX = objectiveBounds.maxX;
            boundsMaxY = objectiveBounds.maxY;
            boundsMaxZ = objectiveBounds.maxZ;
            rectangleCount = campaignBattleObjectiveTargetRectangleCount(
                scene.objective.playerProtected);
        } else {
            const auto selected = std::find_if(
                visuals.begin(),
                visuals.end(),
                [this](const LoadedVisual& visual) {
                    return visual.source.entityId == scene.scannedTargetEntityId;
                });
            if (selected == visuals.end() ||
                !selected->renderInstance.assembly.bounds.valid) {
                return;
            }
            rectangleCount =
                campaignBattleTargetRectangleCount(selected->source.team);
            targetTransform = selected->source.transform;
            const mech3d::Bounds& bounds =
                selected->renderInstance.assembly.bounds;
            boundsMinX = bounds.min.x;
            boundsMinY = bounds.min.y;
            boundsMinZ = bounds.min.z;
            boundsMaxX = bounds.max.x;
            boundsMaxY = bounds.max.y;
            boundsMaxZ = bounds.max.z;
        }
        if (rectangleCount == 0) {
            return;
        }

        GLdouble modelview[16]{};
        GLdouble projection[16]{};
        GLint viewport[4]{};
        glPushMatrix();
        glTranslatef(
            static_cast<float>(targetTransform.x),
            static_cast<float>(targetTransform.y),
            static_cast<float>(targetTransform.z));
        glRotatef(
            static_cast<float>(
                targetTransform.headingRadians * 180.0 / kPi),
            0.0f,
            1.0f,
            0.0f);
        glTranslatef(
            static_cast<float>(-(boundsMinX + boundsMaxX) * 0.5),
            static_cast<float>(-boundsMinY),
            static_cast<float>(-(boundsMinZ + boundsMaxZ) * 0.5));
        glGetDoublev(GL_MODELVIEW_MATRIX, modelview);
        glPopMatrix();
        glGetDoublev(GL_PROJECTION_MATRIX, projection);
        glGetIntegerv(GL_VIEWPORT, viewport);

        const auto transformPoint = [](const GLdouble* matrix,
                                       const std::array<double, 4>& point) {
            std::array<double, 4> result{};
            for (int row = 0; row < 4; ++row) {
                result[static_cast<size_t>(row)] =
                    matrix[row] * point[0] +
                    matrix[4 + row] * point[1] +
                    matrix[8 + row] * point[2] +
                    matrix[12 + row] * point[3];
            }
            return result;
        };
        const CampaignCockpitLayout& layout =
            campaignCockpitLayout(scene.cockpit.family);
        const double virtualWidth = static_cast<double>(layout.viewport.width);
        const double virtualHeight = static_cast<double>(layout.viewport.height);
        double left = virtualWidth;
        double top = virtualHeight;
        double right = 0.0;
        double bottom = 0.0;
        int projectedCornerCount = 0;
        for (double x : {boundsMinX, boundsMaxX}) {
            for (double y : {boundsMinY, boundsMaxY}) {
                for (double z : {boundsMinZ, boundsMaxZ}) {
                    const std::array<double, 4> eye =
                        transformPoint(modelview, {x, y, z, 1.0});
                    const std::array<double, 4> clip =
                        transformPoint(projection, eye);
                    if (clip[3] <= 0.000001) {
                        continue;
                    }
                    const double ndcX = clip[0] / clip[3];
                    const double ndcY = clip[1] / clip[3];
                    const double windowX = viewport[0] +
                        (ndcX + 1.0) * viewport[2] * 0.5;
                    const double windowY = viewport[1] +
                        (ndcY + 1.0) * viewport[3] * 0.5;
                    const double virtualX =
                        windowX * virtualWidth /
                        static_cast<double>(std::max(1, viewport[2]));
                    const double virtualY =
                        virtualHeight -
                        windowY * virtualHeight /
                            static_cast<double>(std::max(1, viewport[3]));
                    left = std::min(left, virtualX);
                    right = std::max(right, virtualX);
                    top = std::min(top, virtualY);
                    bottom = std::max(bottom, virtualY);
                    ++projectedCornerCount;
                }
            }
        }
        if (projectedCornerCount == 0 || right < 0.0 || left > virtualWidth ||
            bottom < 0.0 || top > virtualHeight) {
            return;
        }
        left = std::clamp(left - 1.0, 0.0, virtualWidth - 1.0);
        right = std::clamp(right + 1.0, 1.0, virtualWidth);
        top = std::clamp(top - 1.0, 0.0, virtualHeight - 1.0);
        bottom = std::clamp(bottom + 1.0, 1.0, virtualHeight);

        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, virtualWidth, virtualHeight, 0.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        glColor3fv(cockpitOptions.hudRgb.data());
        for (int rectangle = 0; rectangle < rectangleCount; ++rectangle) {
            const double inset = static_cast<double>(rectangle * 2);
            const double x0 = std::clamp(left - inset, 0.0, virtualWidth - 1.0);
            const double x1 = std::clamp(right + inset, 1.0, virtualWidth);
            const double y0 = std::clamp(top - inset, 0.0, virtualHeight - 1.0);
            const double y1 = std::clamp(bottom + inset, 1.0, virtualHeight);
            glBegin(GL_QUADS);
            glVertex2d(x0, y0);
            glVertex2d(x1, y0);
            glVertex2d(x1, y0 + 1.0);
            glVertex2d(x0, y0 + 1.0);
            glVertex2d(x0, y1 - 1.0);
            glVertex2d(x1, y1 - 1.0);
            glVertex2d(x1, y1);
            glVertex2d(x0, y1);
            glVertex2d(x0, y0);
            glVertex2d(x0 + 1.0, y0);
            glVertex2d(x0 + 1.0, y1);
            glVertex2d(x0, y1);
            glVertex2d(x1 - 1.0, y0);
            glVertex2d(x1, y0);
            glVertex2d(x1, y1);
            glVertex2d(x1 - 1.0, y1);
            glEnd();
        }
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    void drawCockpitCrosshair() const {
        if (scene.mode != battle::CampaignBattlePresentationMode::Cockpit ||
            cockpitOptions.zoomLevel != 1 ||
            !scene.cockpitCrosshairVisible) {
            return;
        }
        glDisable(GL_DEPTH_TEST);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        const CampaignCockpitLayout& layout = campaignCockpitLayout(scene.cockpit.family);
        glOrtho(
            0.0,
            static_cast<double>(layout.viewport.width),
            static_cast<double>(layout.viewport.height),
            0.0,
            -1.0,
            1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();
        const LoadedVisual* player = playerVisual();
        if (player == nullptr) {
            glPopMatrix();
            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
            glMatrixMode(GL_MODELVIEW);
            glEnable(GL_DEPTH_TEST);
            return;
        }
        const CampaignCockpitViewportHudLayout hudLayout =
            campaignCockpitViewportHudLayout(
                scene.cockpit.family,
                player->source.aimPitchStep);
        const float cx = static_cast<float>(
            hudLayout.crosshairCenterX - layout.viewport.x);
        const float cy = static_cast<float>(
            hudLayout.crosshairCenterY - layout.viewport.y);
        glColor3fv(cockpitOptions.hudRgb.data());
        constexpr float innerOffset = 2.0f;
        constexpr float outerOffset = 6.0f;
        constexpr float segmentLength = 5.0f;
        glBegin(GL_QUADS);
        glVertex2f(cx - outerOffset, cy);
        glVertex2f(cx - outerOffset + segmentLength, cy);
        glVertex2f(cx - outerOffset + segmentLength, cy + 1.0f);
        glVertex2f(cx - outerOffset, cy + 1.0f);
        glVertex2f(cx + innerOffset, cy);
        glVertex2f(cx + innerOffset + segmentLength, cy);
        glVertex2f(cx + innerOffset + segmentLength, cy + 1.0f);
        glVertex2f(cx + innerOffset, cy + 1.0f);
        glVertex2f(cx, cy - outerOffset);
        glVertex2f(cx + 1.0f, cy - outerOffset);
        glVertex2f(cx + 1.0f, cy - outerOffset + segmentLength);
        glVertex2f(cx, cy - outerOffset + segmentLength);
        glVertex2f(cx, cy + innerOffset);
        glVertex2f(cx + 1.0f, cy + innerOffset);
        glVertex2f(cx + 1.0f, cy + innerOffset + segmentLength);
        glVertex2f(cx, cy + innerOffset + segmentLength);
        glEnd();
        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    void renderFrame() {
        if (!visible || !sceneReady || hdc == nullptr || hrc == nullptr) {
            return;
        }
        if (wglMakeCurrent(hdc, hrc) == FALSE) {
            error = "could not activate campaign WGL context";
            return;
        }
        if (scene.mode == battle::CampaignBattlePresentationMode::MissionStatus ||
            scene.mode == battle::CampaignBattlePresentationMode::CockpitCommandMap) {
            renderCommandMapFrame();
            SwapBuffers(hdc);
            wglMakeCurrent(nullptr, nullptr);
            return;
        }
        ensureCockpitTextures();
        glViewport(0, 0, width, height);
        const bool collisionFlash =
            scene.mode == battle::CampaignBattlePresentationMode::Cockpit &&
            collisionFlashFramesRemaining > 0;
        const std::array<float, 3> sky = collisionFlash
            ? std::array<float, 3>{170.0f / 255.0f, 0.0f, 0.0f}
            : terrainSkyColor(scene.terrain.environmentId);
        glClearColor(sky[0], sky[1], sky[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        const double nearZ = kCampaignBattleNearClipDistance;
        const double farZ = campaignBattleFarClipDistance();
        const double aspect = static_cast<double>(std::max(1, width)) /
                              static_cast<double>(std::max(1, height));
        const int zoom = scene.mode == battle::CampaignBattlePresentationMode::Cockpit
                             ? std::clamp(cockpitOptions.zoomLevel, 1, 3)
                             : 1;
        const double top = std::tan(45.0 * kPi / 360.0) * nearZ /
                           static_cast<double>(zoom);
        const double right = top * aspect;
        glFrustum(-right, right, -top, top, nearZ, farZ);

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        applyCamera();
        drawFarGroundApron();
        drawTerrain();
        drawTerrainObjects();
        drawObjective();
        for (const LoadedVisual& visual : visuals) {
            const bool hidePlayer =
                scene.mode == battle::CampaignBattlePresentationMode::Cockpit &&
                campaignBattleVisualHiddenInCockpit(visual.source);
            if (!hidePlayer) {
                drawLoadedVisual(visual);
            }
        }
        drawImmediateBeams();
        drawProjectiles();
        drawImpacts();
        drawCockpitTargetRectangle();
        drawCockpitDynamicLayers();
        drawCockpitCrosshair();
        SwapBuffers(hdc);
        if (collisionFlash) {
            --collisionFlashFramesRemaining;
        }
        wglMakeCurrent(nullptr, nullptr);
    }

    void cleanup() {
        visible = false;
        sceneReady = false;
        if (hrc != nullptr) {
            if (hdc != nullptr) {
                wglMakeCurrent(hdc, hrc);
                deleteCockpitTextures();
            }
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hrc);
            hrc = nullptr;
        }
        if (hdc != nullptr && hwnd != nullptr) {
            ReleaseDC(hwnd, hdc);
            hdc = nullptr;
        }
        if (hwnd != nullptr && IsWindow(hwnd)) {
            DestroyWindow(hwnd);
        }
        hwnd = nullptr;
        parent = nullptr;
    }
};

CampaignBattleGlResourceProbe probeCampaignBattleGlResources(
    const battle::CampaignBattleRenderScenePackage& scene) {
    CampaignBattleGlResourceProbe probe;
    if (!scene.valid) {
        probe.reason = scene.reason.empty() ? "campaign_render_scene_invalid" : scene.reason;
        return probe;
    }
    try {
        const LoadedTerrainGeometry terrain = loadTerrainGeometry(scene.terrain);
        probe.terrainVertexCount = terrain.mesh.vertices.size();
        probe.terrainTriangleCount = terrain.mesh.indices.size() / 3u;
        probe.terrainObjectCount = terrain.objectCount;
        probe.terrainObjectRecordCount = terrain.objectRecordCount;
        probe.terrainObjectVertexCount = terrain.objectBatch.vertices.size() / 6u;
        probe.terrainObjectTriangleCount = terrain.objectBatch.triangleIndices.size() / 3u;
        probe.terrainPaletteLoaded = terrain.paletteLoaded;
        probe.terrainPaletteId = terrain.paletteId;
        probe.terrainObjectDitherVertexCount = terrain.objectDitherBatch.vertices.size() / 6u;
        probe.terrainObjectDitherTriangleCount =
            terrain.objectDitherBatch.triangleIndices.size() / 3u;

        RecordCache cache;
        bool batchesValid = validGpuBatch(terrain.objectBatch) &&
                            validGpuBatch(terrain.objectDitherBatch);
        std::vector<LoadedVisual> loadedVisuals;
        std::vector<LoadedVisual> loadedProjectileMountReferences;
        loadedVisuals.reserve(scene.combatantVisuals.size());
        loadedProjectileMountReferences.reserve(scene.combatantVisuals.size());
        for (const battle::CampaignBattleRenderVisualInstance& visual : scene.combatantVisuals) {
            loadedVisuals.push_back(loadVisual(visual, cache));
            loadedProjectileMountReferences.push_back(loadVisual(
                projectileMountReferenceSource(visual),
                cache));
            const LoadedVisual& loaded = loadedVisuals.back();
            ++probe.visualCount;
            probe.mechVertexCount += loaded.renderInstance.batch.vertices.size() / 6u;
            probe.mechTriangleCount += loaded.renderInstance.batch.triangleIndices.size() / 3u;
            batchesValid = batchesValid && validGpuBatch(loaded.renderInstance.batch);
        }

        for (const battle::CampaignBattleRenderProjectileInstance& projectile :
             scene.projectileVisuals) {
            const legacy3d::GpuBatch loaded = loadProjectileBatch(
                projectile,
                cache);
            ++probe.projectileVisualCount;
            probe.projectileVertexCount += loaded.vertices.size() / 6u;
            probe.projectileTriangleCount +=
                loaded.triangleIndices.size() / 3u;
            probe.projectileRecord0Loaded =
                probe.projectileRecord0Loaded ||
                (projectile.recordIndex == 0 && !loaded.vertices.empty());
            probe.projectileRecord1Loaded =
                probe.projectileRecord1Loaded ||
                (projectile.recordIndex == 1 && !loaded.vertices.empty());
            const LoadedVisual* shooter = loadedVisualByEntityId(
                loadedProjectileMountReferences,
                projectile.shooterEntityId);
            if (shooter != nullptr &&
                weaponMountWorldPoint(
                    *shooter,
                    projectile.locationId,
                    &projectile.launchShooterTransform,
                    projectile.launchTorsoYawRadians).has_value()) {
                ++probe.projectileWeaponMountCount;
            }
            const MountedProjectilePose mountedPose =
                mountedProjectilePoseForScene(
                    loadedProjectileMountReferences,
                    projectile);
            if (probe.projectileMountedPoseFingerprint == 0u) {
                probe.projectileMountedPoseFingerprint =
                    1469598103934665603ull;
            }
            const auto appendMountedPoseFingerprint = [&probe](uint64_t value) {
                for (int byte = 0; byte < 8; ++byte) {
                    probe.projectileMountedPoseFingerprint ^=
                        (value >> (byte * 8)) & 0xffu;
                    probe.projectileMountedPoseFingerprint *=
                        1099511628211ull;
                }
            };
            appendMountedPoseFingerprint(projectile.projectileId);
            for (float value : {
                     mountedPose.position.x,
                     mountedPose.position.y,
                     mountedPose.position.z,
                     mountedPose.direction.x,
                     mountedPose.direction.y,
                     mountedPose.direction.z}) {
                appendMountedPoseFingerprint(static_cast<uint64_t>(
                    static_cast<int64_t>(std::llround(
                        static_cast<double>(value) * 1000.0))));
            }
            const double mountDelta =
                std::abs(static_cast<double>(mountedPose.position.x) -
                         projectile.position.x) +
                std::abs(static_cast<double>(mountedPose.position.y) -
                         projectile.position.y) +
                std::abs(static_cast<double>(mountedPose.position.z) -
                         projectile.position.z);
            if (mountDelta > 0.001) {
                ++probe.projectileMountOffsetCount;
            }
            if (mountedPose.straightMountToTargetPath) {
                ++probe.projectileStraightMountPathCount;
            }
            if (!projectile.targetBound &&
                mountedPose.straightMountToTargetPath) {
                ++probe.projectileUnboundAuthoritativePathCount;
            }
            batchesValid = batchesValid && validGpuBatch(loaded) &&
                !loaded.vertices.empty();
        }

        for (const battle::CampaignBattleRenderBeamInstance& beam :
             scene.beamVisuals) {
            const LoadedVisual* shooter = loadedVisualByEntityId(
                loadedVisuals, beam.shooterEntityId);
            if (shooter != nullptr &&
                weaponMountWorldPoint(*shooter, beam.locationId).has_value()) {
                ++probe.beamWeaponMountCount;
                const std::optional<Vec3> mount = weaponMountWorldPoint(
                    *shooter, beam.locationId);
                const battle::CampaignBattleRenderBeamInstance mounted =
                    mountedBeamForScene(scene, loadedVisuals, beam);
                if (mount.has_value() &&
                    scene.mode == battle::CampaignBattlePresentationMode::Cockpit &&
                    shooter->source.playerControlled) {
                    ++probe.playerCockpitBeamCount;
                    if (std::abs(scene.cockpitCameraBobOffsetY) > 0.001 &&
                        std::abs(
                            mounted.start.y - mount->y -
                            scene.cockpitCameraBobOffsetY) < 0.001) {
                        probe.playerBeamCockpitBobApplied = true;
                    }
                    if (scene.camera.valid) {
                        const float heading = static_cast<float>(
                            scene.camera.transform.headingRadians);
                        const float forwardX = std::sin(heading);
                        const float forwardZ = std::cos(heading);
                        const float rawForwardDistance =
                            (mount->x - static_cast<float>(
                                scene.camera.transform.x)) * forwardX +
                            (mount->z - static_cast<float>(
                                scene.camera.transform.z)) * forwardZ;
                        const float mountedForwardDistance =
                            (static_cast<float>(mounted.start.x) -
                                static_cast<float>(scene.camera.transform.x)) *
                                forwardX +
                            (static_cast<float>(mounted.start.z) -
                                static_cast<float>(scene.camera.transform.z)) *
                                forwardZ;
                        const float minimumDistance =
                            kCampaignBattleNearClipDistance +
                            kCockpitBeamNearClipMargin;
                        if (rawForwardDistance < minimumDistance) {
                            ++probe.playerBeamNearClipCorrectionCount;
                        }
                        probe.playerBeamStartsBeyondNearClip =
                            probe.playerBeamStartsBeyondNearClip &&
                            mountedForwardDistance + 0.001f >= minimumDistance;
                    }
                }
            }
        }

        for (const battle::CampaignBattleRenderImpactInstance& impact :
             scene.impactVisuals) {
            const ImpactStageGeometry stage = loadImpactStage(impact, cache);
            ++probe.impactVisualCount;
            probe.impactSpherePrimitiveCount += stage.spheres.size();
            probe.machineGunImpactLinePrimitiveCount += stage.lines.size();
            probe.machineGunImpactTrianglePrimitiveCount +=
                stage.triangles.size();
            if (!stage.spheres.empty()) {
                const uint8_t stageColor = static_cast<uint8_t>(
                    stage.spheres.front().shadeBytes[1] & 0x0fu);
                for (const ImpactSpherePrimitive& sphere : stage.spheres) {
                    probe.impactMaximumWorldRadius = std::max(
                        probe.impactMaximumWorldRadius,
                        sphere.radius);
                    const uint8_t color = static_cast<uint8_t>(
                        sphere.shadeBytes[1] & 0x0fu);
                    probe.impactFlatEgaColorMask |=
                        static_cast<uint16_t>(1u << color);
                    probe.impactStageColorsFlat =
                        probe.impactStageColorsFlat && color == stageColor;
                }
            }
            if (impact.recordIndex >= 2 && impact.recordIndex <= 7) {
                probe.impactRecordMask |= static_cast<uint8_t>(
                    1u << static_cast<unsigned>(impact.recordIndex - 2));
            } else if (impact.recordIndex >= 8 && impact.recordIndex <= 9) {
                probe.machineGunImpactRecordMask |= static_cast<uint8_t>(
                    1u << static_cast<unsigned>(impact.recordIndex - 8));
            }
        }

        const legacy3d::GpuBatch objective = loadObjectiveBatch(scene.objective);
        probe.objectiveLoaded = !objective.vertices.empty();
        probe.objectiveVertexCount = objective.vertices.size() / 6u;
        batchesValid = batchesValid && validGpuBatch(objective);
        probe.gpuBatchContractPreserved = batchesValid;
        probe.valid =
            !terrain.mesh.vertices.empty() &&
            probe.terrainPaletteLoaded &&
            probe.visualCount == scene.combatantVisuals.size() &&
            probe.projectileVisualCount == scene.projectileVisuals.size() &&
            probe.impactVisualCount == scene.impactVisuals.size() &&
            (!scene.objective.staticModel.valid || probe.objectiveLoaded) &&
            probe.gpuBatchContractPreserved;
        if (!probe.valid) {
            probe.reason = "campaign_gl_resource_probe_incomplete";
        }
    } catch (const std::exception& exception) {
        probe.reason = exception.what();
    }
    return probe;
}

CampaignBattleGlViewport::CampaignBattleGlViewport() : impl_(std::make_unique<Impl>()) {}

CampaignBattleGlViewport::~CampaignBattleGlViewport() {
    shutdown();
}

bool CampaignBattleGlViewport::initialize(HINSTANCE instance, HWND parent) {
    return impl_->initializeWindow(instance, parent);
}

void CampaignBattleGlViewport::shutdown() {
    if (impl_) {
        impl_->cleanup();
    }
}

void CampaignBattleGlViewport::setBounds(const RECT& bounds) {
    if (impl_->hwnd == nullptr) {
        return;
    }
    const int width = std::max(1L, bounds.right - bounds.left);
    const int height = std::max(1L, bounds.bottom - bounds.top);
    MoveWindow(impl_->hwnd, bounds.left, bounds.top, width, height, TRUE);
}

void CampaignBattleGlViewport::setVisible(bool visible) {
    impl_->visible = visible;
    if (impl_->hwnd != nullptr) {
        ShowWindow(impl_->hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }
}

void CampaignBattleGlViewport::setCockpitOptions(
    const CampaignBattleGlCockpitOptions& options) {
    impl_->cockpitOptions = options;
    impl_->cockpitOptions.zoomLevel = std::clamp(options.zoomLevel, 1, 3);
}

bool CampaignBattleGlViewport::updateScene(
    const battle::CampaignBattleRenderScenePackage& scene) {
    if (!scene.valid) {
        impl_->sceneReady = false;
        impl_->error = scene.reason;
        return false;
    }
    return impl_->loadScene(scene);
}

void CampaignBattleGlViewport::render() {
    impl_->renderFrame();
}

bool CampaignBattleGlViewport::initialized() const {
    return impl_->hwnd != nullptr && impl_->hdc != nullptr && impl_->hrc != nullptr;
}

bool CampaignBattleGlViewport::ready() const {
    return initialized() && impl_->sceneReady;
}

const std::string& CampaignBattleGlViewport::lastError() const {
    return impl_->error;
}

} // namespace mw::presentation
