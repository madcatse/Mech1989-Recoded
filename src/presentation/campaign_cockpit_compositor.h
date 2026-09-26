#pragma once

#include "battle/battle_module.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mw::presentation {

struct CampaignCockpitRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct CampaignCockpitLayout {
    battle::CampaignCockpitFamily family = battle::CampaignCockpitFamily::Light;
    const char* id = "light";
    CampaignCockpitRect viewport;
    CampaignCockpitRect leftPanel;
    CampaignCockpitRect centerPanel;
    CampaignCockpitRect rightPanel;
    CampaignCockpitRect compassPanel;
};

// Original LIGHT/MEDIUM/HEAVY.SCR weapon-grid geometry. Text is rendered with
// a 5x5 glyph and a one-pixel advance gap; ammunition is right aligned.
struct CampaignCockpitWeaponPanelLayout {
    bool rightSide = false;
    int statusX = 0;
    int firstRowY = 0;
    int statusWidth = 6;
    int statusHeight = 5;
    int nameX = 0;
    int ammunitionRightExclusive = 0;
    int rangeClassX = 0;
    int rowStep = 8;
    size_t secondBankFirstRow = 5;
    int secondBankYOffset = 0;
    size_t rowCapacity = 0;
};

struct CampaignCockpitWeaponTextColors {
    uint32_t nameBgra = 0;
    uint32_t rangeBgra = 0;
};

const CampaignCockpitLayout& campaignCockpitLayout(
    battle::CampaignCockpitFamily family);
const CampaignCockpitWeaponPanelLayout& campaignCockpitWeaponPanelLayout(
    battle::CampaignCockpitFamily family);
int campaignCockpitWeaponPanelRowY(
    const CampaignCockpitWeaponPanelLayout& layout,
    size_t row);
CampaignCockpitWeaponTextColors campaignCockpitWeaponTextColors(
    battle::CampaignCockpitFamily family,
    battle::BattleWeaponReadiness readiness,
    bool selectedTargetWithinMaximumRange = false,
    bool cooldownActive = false);
bool campaignCockpitWeaponRangeIndicatorActive(
    const battle::BattleSnapshot& snapshot,
    const battle::BattleWeaponInstanceState& weapon);

double campaignCockpitDisplayHeadingDegrees(double runtimeHeadingRadians);

CampaignCockpitRect campaignMissionStatusMapRect();
CampaignCockpitRect campaignCockpitCommandMapRect();
CampaignCockpitRect campaignCockpitMinimapRect();

// BTECH.EXE stores the four labels in this order: 500 M, 1000 M, 2000 M,
// 4000 M. Original cockpit captures show the runtime cycle in reverse order.
// The world conversion is capture-calibrated against the proven 500-unit map
// projection; it remains explicitly presentation-only.
constexpr double campaignCockpitRadarWorldUnitsPerMeter() {
    return battle::battleTargetScanWorldUnitsPerMeter();
}

struct CampaignCockpitRadarGeometry {
    CampaignCockpitRect rect;
    int leftX = 0;
    int rightX = 0;
    int topY = 0;
    int apexX = 0;
    int apexY = 0;
    int labelY = 0;
};

struct CampaignCockpitRadarContactProjection {
    bool visible = false;
    int x = 0;
    int y = 0;
    double distanceMeters = 0.0;
};

CampaignCockpitRadarGeometry campaignCockpitRadarGeometry();
CampaignCockpitRadarContactProjection campaignCockpitRadarContactProjection(
    const battle::Transform& player,
    const battle::Transform& contact,
    uint16_t rangeMeters);
std::string campaignCockpitRadarRangeLabel(uint16_t rangeMeters);

// SM_MECHS.BMP contains nine 56x49 records in original catalog order.  The
// cockpit glass exposes the central 53 columns; the side status arrows remain
// part of the original sprite and are clipped by the panel aperture.
struct CampaignCockpitTargetMfdGeometry {
    CampaignCockpitRect rect;
    int sourceX = 0;
};

CampaignCockpitTargetMfdGeometry campaignCockpitTargetMfdGeometry(
    battle::CampaignCockpitFamily family);
int campaignCockpitTargetScanSpriteIndex(std::string_view mechPresetId);
constexpr int campaignCockpitTargetScanObjectiveSpriteIndex() {
    return 8;
}
battle::BattleMechSystemRole campaignCockpitTargetScanPixelRole(
    int mechSpriteIndex,
    int sourceX,
    int sourceY);
std::optional<mech3d::MechArmorSectionId>
campaignCockpitTargetScanPixelArmorSection(
    int mechSpriteIndex,
    int sourceX,
    int sourceY);
battle::BattleMechSystemStatus campaignDetailedDamageDisplayStatus(
    uint8_t entryDamageLevel,
    int battleDamage,
    bool functional = true);
battle::BattleMechSystemStatus campaignDetailedArmorDisplayStatus(
    const mech3d::MechDetailedDamageState& damage,
    mech3d::MechArmorSectionId section);
uint8_t campaignCockpitTargetScanDisplayColorIndex(
    uint8_t sourceColorIndex,
    battle::BattleMechSystemRole role,
    battle::BattleMechSystemStatus status,
    bool wholeMechDestroyed);

uint32_t campaignBattleMapPlayerBlipBgra(
    battle::CampaignBattlePresentationMode mode,
    std::optional<int> environmentId);

enum class CampaignBattleMapPlayerMarkerShape : uint8_t {
    Square,
    H,
    RotatedH,
    Plus,
    Dot,
};

struct CampaignBattleMapPlayerMarker {
    CampaignBattleMapPlayerMarkerShape shape =
        CampaignBattleMapPlayerMarkerShape::Dot;
    uint32_t bgra = 0xffffffffu;
    uint8_t egaIndex = 15u;
    std::array<uint8_t, 9> pixels{};
    bool contrastOutline = false;
};

int campaignBattlePlayerLanceSlot(
    std::string_view rosterSourceSlot,
    bool playerControlled);
CampaignBattleMapPlayerMarker campaignBattleMapPlayerMarker(
    int playerLanceSlot,
    battle::CampaignBattlePresentationMode mode,
    std::optional<int> environmentId);
uint32_t campaignCockpitMinimapPlayerBlipBgra(bool playerControlled);
constexpr double campaignBattleMapWorldUnitsPerPixel() {
    return 500.0;
}

struct CampaignEgaPaletteBank {
    bool valid = false;
    std::string reason;
    size_t bankIndex = 0;
    std::array<uint8_t, 16> colorPairs{};
};

struct CampaignEgaPaletteColors {
    bool valid = false;
    std::string reason;
    std::array<uint32_t, 16> bgra{};
};

CampaignEgaPaletteBank loadCampaignEgaPaletteBank(
    const std::filesystem::path& palettePath,
    size_t bankIndex);

CampaignEgaPaletteColors loadCampaignEgaPaletteColors(
    const std::filesystem::path& palettePath);

struct CampaignCockpitBackdropImage {
    bool valid = false;
    std::string reason;
    std::filesystem::path sourcePath;
    std::filesystem::path palettePath;
    int width = 0;
    int height = 0;
    int codec = 0;
    std::vector<uint32_t> bgraPixels;
};

CampaignCockpitBackdropImage loadCampaignCockpitBackdrop(
    const battle::CampaignBattleRenderCockpitResources& resources);

struct CampaignCockpitSprite {
    int index = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgbaPixels;
};

struct CampaignIndexedSprite {
    int index = 0;
    int width = 0;
    int height = 0;
    int rowStride = 0;
    std::vector<uint8_t> packedPixels;
};

struct CampaignIndexedSpriteArchive {
    bool valid = false;
    std::string reason;
    std::vector<CampaignIndexedSprite> sprites;
};

CampaignIndexedSpriteArchive loadCampaignIndexedSpriteArchive(
    const std::filesystem::path& path);

struct CampaignCockpitFont {
    int width = 0;
    int height = 0;
    int firstCode = 0;
    int glyphCount = 0;
    std::vector<uint8_t> rows;

    bool contains(char ch) const;
};

struct CampaignCockpitDynamicLayers {
    bool valid = false;
    std::string reason;
    std::vector<CampaignCockpitSprite> struts;
    std::vector<CampaignCockpitSprite> widgets;
    std::vector<CampaignCockpitSprite> hudNumbers;
    CampaignCockpitFont font;
};

struct CampaignCockpitSpritePlacement {
    int spriteIndex = 0;
    int x = 0;
    int y = 0;
};

CampaignCockpitDynamicLayers loadCampaignCockpitDynamicLayers(
    const battle::CampaignBattleRenderCockpitResources& resources);

std::vector<CampaignCockpitSpritePlacement> campaignCockpitStrutPlacements(
    battle::CampaignCockpitFamily family);

struct CampaignCockpitHudColor {
    const char* id = "cyan";
    uint32_t bgra = 0xff55ffffu;
    std::array<float, 3> rgb{85.0f / 255.0f, 1.0f, 1.0f};
};

size_t campaignCockpitHudColorCount();
const CampaignCockpitHudColor& campaignCockpitHudColor(size_t index);

struct CampaignCockpitGaugeState {
    int speedX = 0;
    int speedY = 0;
    int forwardBars = 0;
    int reverseBars = 0;
    int heatX = 0;
    int heatBottomY = 0;
    int heatBars = 0;
    bool jumpGaugeVisible = false;
    CampaignCockpitRect jumpGauge;
    CampaignCockpitRect jumpReadyLight;
    int jumpFuelFillHeight = 0;
    int jumpActivationThresholdY = 0;
};

CampaignCockpitGaugeState campaignCockpitGaugeState(
    const battle::CombatantSnapshot& combatant,
    battle::CampaignCockpitFamily family);

struct CampaignCockpitViewportHudLayout {
    int crosshairCenterX = 0;
    int crosshairCenterY = 0;
    int leftLabelX = 0;
    int rightLabelX = 0;
    int labelY = 0;
    bool zoomLabelOnLeft = false;
};

CampaignCockpitViewportHudLayout campaignCockpitViewportHudLayout(
    battle::CampaignCockpitFamily family,
    int aimPitchStep);

struct CampaignCockpitMinimapTerrainSample {
    double worldX = 0.0;
    double worldZ = 0.0;
    uint8_t colorBand = 0;
};

struct CampaignCockpitMinimapTerrain {
    bool valid = false;
    std::string reason;
    std::string provenance;
    size_t driveableSurfacePlacements = 0;
    size_t solidMountainPlacements = 0;
    size_t solidMountainGradientPlacements = 0;
    size_t solidMountainFourBandPlacements = 0;
    std::array<size_t, 4> heightBandSampleCounts{};
    std::vector<CampaignCockpitMinimapTerrainSample> samples;
};

CampaignCockpitMinimapTerrain loadCampaignCockpitMinimapTerrain(
    const battle::CampaignBattleRenderTerrainResources& terrain);

} // namespace mw::presentation
