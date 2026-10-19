#pragma once

#include "battle/battle_module.h"
#include "presentation/campaign_command_screen.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace mw::presentation {

struct CampaignBattleDamageSmokePuff {
    battle::EntityId entityId{};
    mech3d::MechInternalSectionId section = mech3d::MechInternalSectionId::CenterTorso;
    battle::BattleHitPoint anchor;
    battle::BattleHitPoint position;
    int recordIndex = 10;
    float opacity = 0.0f;
    float stageFraction = 0.0f;
};

// Ordered 4x4 coverage, expanded to a 32x32 OpenGL stipple mask. Supported
// pixel sizes are 1, 2, 4, 8; coverage is 0..16 opaque cells, never RGB blending.
std::array<uint8_t,128> campaignBattleDamageSmokeStipple(int coverage, int pixelSize);

struct CampaignBattleGlResourceProbe {
    bool valid = false;
    std::string reason;
    size_t terrainVertexCount = 0;
    size_t terrainTriangleCount = 0;
    size_t terrainObjectCount = 0;
    size_t terrainObjectRecordCount = 0;
    size_t terrainObjectVertexCount = 0;
    size_t terrainObjectTriangleCount = 0;
    bool terrainPaletteLoaded = false;
    std::string terrainPaletteId;
    size_t terrainObjectDitherVertexCount = 0;
    size_t terrainObjectDitherTriangleCount = 0;
    size_t visualCount = 0;
    size_t mechVertexCount = 0;
    size_t mechTriangleCount = 0;
    size_t mechDitherVertexCount = 0;
    size_t mechColoredVertexCount = 0;
    size_t projectileVisualCount = 0;
    size_t projectileVertexCount = 0;
    size_t projectileTriangleCount = 0;
    uint16_t rocketFillEgaColorMask = 0;
    bool projectileRecord0Loaded = false;
    bool projectileRecord1Loaded = false;
    size_t projectileWeaponMountCount = 0;
    size_t projectileMountOffsetCount = 0;
    size_t projectileStraightMountPathCount = 0;
    size_t projectileUnboundAuthoritativePathCount = 0;
    uint64_t projectileMountedPoseFingerprint = 0;
    size_t impactVisualCount = 0;
    size_t impactSpherePrimitiveCount = 0;
    size_t machineGunImpactLinePrimitiveCount = 0;
    size_t machineGunImpactTrianglePrimitiveCount = 0;
    uint8_t impactRecordMask = 0;
    uint8_t machineGunImpactRecordMask = 0;
    uint16_t impactFlatEgaColorMask = 0;
    bool impactStageColorsFlat = true;
    float impactMaximumWorldRadius = 0.0f;
    std::vector<CampaignBattleDamageSmokePuff> damageSmokePuffs;
    size_t damageSmokeSphereCount = 0;
    uint8_t damageSmokeRecordMask = 0;
    uint16_t damageSmokeColorMask = 0;
    size_t beamWeaponMountCount = 0;
    bool playerBeamCockpitBobApplied = false;
    size_t playerCockpitBeamCount = 0;
    size_t playerBeamNearClipCorrectionCount = 0;
    bool playerBeamStartsBeyondNearClip = true;
    bool objectiveLoaded = false;
    size_t objectiveVertexCount = 0;
    uint16_t objectiveFillEgaColorMask = 0;
    bool gpuBatchContractPreserved = false;
};

CampaignBattleGlResourceProbe probeCampaignBattleGlResources(
    const battle::CampaignBattleRenderScenePackage& scene);

constexpr double campaignBattleFarClipDistance() {
    return 160000.0;
}

struct CampaignBattleFarGroundApron {
    bool valid = false;
    double minX = 0.0;
    double maxX = 0.0;
    double minZ = 0.0;
    double maxZ = 0.0;
    double groundY = 0.0;
};

CampaignBattleFarGroundApron campaignBattleFarGroundApron(
    const battle::CampaignBattleRenderTerrainResources& terrain,
    double viewDistance = campaignBattleFarClipDistance());

// Exact EGA colors used by the original battlefield backgrounds.  The biome
// selects the bright sky; BattleSkyMode independently selects black sky.
std::array<float, 3> campaignBattleTerrainGroundRgb(
    const std::optional<int>& environmentId);

std::array<float, 3> campaignBattleTerrainSkyRgb(
    const std::optional<int>& environmentId,
    battle::BattleSkyMode skyMode);

struct CampaignBattleGlCockpitOptions {
    int zoomLevel = 1;
    bool retreatPromptVisible = false;
    std::array<float, 3> hudRgb{85.0f / 255.0f, 1.0f, 1.0f};
    CampaignCommandMapOverlay commandOverlay;
    std::vector<battle::EntityId> knownCommandMapEnemies;
};

struct CampaignBattleExternalCameraOrbit {
    float yawOffsetDegrees = 0.0f;
    float pitchDegrees = 28.0f;
    float distance = 3200.0f;
};

CampaignBattleExternalCameraOrbit campaignBattleExternalCameraAfterDrag(
    CampaignBattleExternalCameraOrbit orbit,
    int deltaX,
    int deltaY);

CampaignBattleExternalCameraOrbit campaignBattleExternalCameraAfterWheel(
    CampaignBattleExternalCameraOrbit orbit,
    int wheelDelta);

struct CampaignBattleGlVirtualViewportRect {
    int x = 0;
    int y = 0;
    int width = 320;
    int height = 200;
};

CampaignBattleGlVirtualViewportRect campaignBattleGlVirtualViewportRect(
    battle::CampaignBattlePresentationMode mode,
    battle::CampaignCockpitFamily cockpitFamily);

bool campaignBattleVisualHiddenInCockpit(
    const battle::CampaignBattleRenderVisualInstance& visual);
int campaignBattleTargetRectangleCount(battle::BattleTeam team);
int campaignBattleObjectiveTargetRectangleCount(bool playerProtected);

class CampaignBattleGlViewport {
public:
    CampaignBattleGlViewport();
    ~CampaignBattleGlViewport();

    CampaignBattleGlViewport(const CampaignBattleGlViewport&) = delete;
    CampaignBattleGlViewport& operator=(const CampaignBattleGlViewport&) = delete;

    bool initialize(HINSTANCE instance, HWND parent);
    void shutdown();
    void setBounds(const RECT& bounds);
    void setVisible(bool visible);
    void setCockpitOptions(const CampaignBattleGlCockpitOptions& options);
    bool updateScene(const battle::CampaignBattleRenderScenePackage& scene);
    void render();
    // Diagnostic readback of the actual Command Map renderer, at current bounds.
    // Does not advance simulation or require a visible desktop window.
    std::vector<uint32_t> captureCommandMapPixels();
    // Read back any scene through the game renderer without presenting/advancing it.
    std::vector<uint32_t> captureScenePixels();

    bool initialized() const;
    bool ready() const;
    const std::string& lastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mw::presentation
