#include "battle/battle_module.h"
#include "battle/battle_world.h"
#include "battle/battle_setup.h"
#include "legacy3d/shape_parser.h"
#include "legacy3d/terrain_parser.h"
#include "mech3d/mech_catalog.h"
#include "mech3d/mech_runtime_state.h"
#include "mech3d/model_assembly.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <gl/GL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#endif
#ifndef GL_ELEMENT_ARRAY_BUFFER
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#endif
#ifndef GL_STATIC_DRAW
#define GL_STATIC_DRAW 0x88E4
#endif

using GLsizeiptr = ptrdiff_t;
using GlGenBuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlBindBuffer = void(APIENTRY*)(GLenum, GLuint);
using GlBufferData = void(APIENTRY*)(GLenum, GLsizeiptr, const void*, GLenum);
using GlDeleteBuffers = void(APIENTRY*)(GLsizei, const GLuint*);

namespace {

constexpr int kVirtualScreenWidth = 320;
constexpr int kVirtualScreenHeight = 200;
constexpr int kBattleViewerDefaultClientWidth = 1120;
constexpr int kBattleViewerDefaultClientHeight = 820;
constexpr int kCockpitViewerClientWidth = kVirtualScreenWidth * 4;
constexpr int kCockpitViewerClientHeight = kVirtualScreenHeight * 5;
enum class BattleCommandMapScreen {
    None,
    MissionStatus,
    CockpitCommand,
};

struct Options {
    std::filesystem::path resourcePath = std::filesystem::path("Sorted Original Files") / "TBL" / "viewer8" / "JENPCK.TBL";
    int recordIndex = 0;
    bool recordIndexExplicit = false;
    std::string shadeMode = "stored";
    bool smoke = false;
    bool listMechPresets = false;
    bool listBattleMissions = false;
    bool listTerrainArenas = false;
    bool listTerrainLayouts = false;
    bool listTerrainScenarios = false;
    bool resourcePathExplicit = false;
    bool wire = false;
    bool perspective = true;
    bool assembled = false;
    std::string mechPreset;
    std::string animationName;
    std::vector<int> assemblyRecordIndices;
    bool animationEnabled = false;
    int animationComponentId = 0;
    int animationFrameMs = 180;
    std::vector<int> animationRecordIndices;
    std::vector<mw::mech3d::ComponentFrameSequence> animationSequences;
    std::vector<int> hiddenComponentIds;
    std::vector<int> destroyedComponentIds;
    std::filesystem::path terrainRoot = std::filesystem::path("Sorted Original Files");
    std::string terrainArena;
    std::string terrainLayoutPreset;
    std::vector<std::string> terrainLayoutNames;
    std::filesystem::path terrainScenarioPath;
    std::optional<size_t> terrainScenarioIndex;
    int terrainScenarioMode = -1;
    float terrainLayoutGap = 0.0f;
    bool terrainLayoutGapExplicit = false;
    float terrainCellSize = 500.0f;
    float terrainObjectInset = 0.12f;
    bool terrainObjectInsetExplicit = false;
    float terrainObjectScale = 1.0f;
    bool terrainTileMirrorX = false;
    bool terrainTileMirrorY = true;
    bool terrainObjectMirrorY = true;
    std::string terrainColorMode = "debug";
    bool terrainColorExplicit = false;
    std::string terrainSkyMode = "auto";
    std::optional<int> terrainEnvironmentId;
    std::filesystem::path terrainGridPath;
    std::filesystem::path terrainWorldPath;
    std::filesystem::path terrainShapePath;
    bool terrainEnabled = false;
    bool terrainWorldEnabled = false;
    std::optional<uint64_t> battleSimTicks;
    bool battleDrive = false;
    bool battleCockpit = false;
    bool battleCommandMap = false;
    BattleCommandMapScreen battleCommandMapScreen = BattleCommandMapScreen::None;
    mw::battle::BattleMissionBriefing battleMission = mw::battle::defaultBattleMissionBriefing();
    std::string battleMissionSource = "viewer_default";
    bool battleOriginalPlacement = false;
    std::optional<mw::battle::OriginalBattlefieldSetup> battleSetup;
    std::string battleCockpitFamily = "auto";
    bool battleCockpitExternalCamera = false;
    bool battleCockpitBob = true;
    bool battleDriveReplayMatch = false;
    int battleDriveScenarioStep = 0;
    std::optional<std::pair<double, double>> battleStartWorld;
    std::optional<std::pair<double, double>> battleStartGrid;
    std::optional<std::pair<double, double>> battleEnemyStartWorld;
    std::optional<std::pair<double, double>> battleEnemyStartGrid;
    std::optional<std::pair<double, double>> battleCommandMapTargetWorld;
    std::optional<std::pair<double, double>> battleCommandMapTargetGrid;
    std::optional<double> battleStartHeadingRadians;
    float battleCameraYawOffsetDegrees = 0.0f;
    float battleCameraPitchDegrees = 28.0f;
    float battleCameraDistance = 3200.0f;
    double battleDriveMaxForwardSpeed = mw::battle::battleDrivePrototypeTuning().maxForwardSpeed;
    double battleDriveMaxReverseSpeed = mw::battle::battleDrivePrototypeTuning().maxReverseSpeed;
    double battleDriveAcceleration = mw::battle::battleDrivePrototypeTuning().acceleration;
    double battleDriveDeceleration = mw::battle::battleDrivePrototypeTuning().deceleration;
    double battleDriveTurnRateRadians = mw::battle::battleDrivePrototypeTuning().maxTurnRateRadians;
    std::optional<mw::battle::BattleWorld> battleWorld;
    std::optional<mw::battle::BattleSnapshot> battleSnapshot;
};

bool battleCommandMapActive(const Options& options) {
    return options.battleCommandMapScreen != BattleCommandMapScreen::None;
}

bool battleInteractiveMode(const Options& options) {
    return options.battleDrive || options.battleCockpit || options.battleCommandMap;
}

const char* battleViewModeName(const Options& options) {
    if (options.battleCommandMapScreen == BattleCommandMapScreen::MissionStatus) {
        return "mission_status";
    }
    if (options.battleCommandMapScreen == BattleCommandMapScreen::CockpitCommand) {
        return "command_map";
    }
    if (options.battleCockpit) {
        return "cockpit";
    }
    if (options.battleDrive) {
        return "drive";
    }
    return "snapshot";
}

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct ScreenRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct BattlefieldBoundaryPaletteIndices {
    uint8_t ordinary = 15;
    uint8_t allowed = 11;
};

constexpr ScreenRect kMissionStatusMapInnerRect{5, 15, 197, 107};
constexpr ScreenRect kCockpitCommandMapInnerRect{62, 7, 197, 107};

struct CockpitHudState {
    double headingDegrees = 0.0;
    double bodyHeadingDegrees = 0.0;
    std::string compass = "N";
    double speed = 0.0;
    double targetSpeed = 0.0;
    double maxForwardSpeed = 1.0;
    double maxReverseSpeed = 1.0;
    double torsoDegrees = 0.0;
    int aimPitchStep = 0;
    bool boundaryContact = false;
    bool jumpJetsEnabled = false;
    bool jumpCapable = false;
    bool jumpJetReady = false;
    bool jumpJetThrusting = false;
    bool airborne = false;
    bool hardLanding = false;
    double jumpFuel = 1.5;
    double jumpMaxFuel = 1.5;
    double jumpActivationFuel = 0.975;
    double verticalSpeed = 0.0;
    std::string scenario = "-";
    std::string mode = "cockpit";
    std::string camera = "cockpit";
};

struct CockpitSystemIndicatorDamage {
    bool sensors = false;
    bool gyros = false;
    bool engines = false;
    bool lifeSupport = false;
};

struct CockpitHudColor {
    const char* id = "cyan";
    std::array<float, 3> rgb{85.0f / 255.0f, 1.0f, 1.0f};
};

struct LightCockpitSpeedGaugeState {
    int forwardBars = 0;
    int reverseBars = 0;
};

enum class CockpitFamily {
    Light,
    Medium,
    Heavy,
};

struct CockpitLayout {
    CockpitFamily family = CockpitFamily::Light;
    const char* id = "light";
    const char* scrName = "LIGHT.SCR";
    ScreenRect viewport{};
    ScreenRect leftPanel{};
    ScreenRect centerPanel{};
    ScreenRect rightPanel{};
    ScreenRect compassPanel{};
    ScreenRect leftStrut{};
    ScreenRect rightStrut{};
    int compassTopMarkerOffsetY = 11;
    int crosshairCenterY = kVirtualScreenHeight / 2;
};

struct CockpitStrutPlacement {
    int spriteIndex = 0;
    int x = 0;
    int y = 0;
};

constexpr CockpitLayout kCockpitLayouts[] = {
    CockpitLayout{
        CockpitFamily::Light,
        "light",
        "LIGHT.SCR",
        ScreenRect{11, 11, 297, 92},
        ScreenRect{8, 112, 94, 66},
        ScreenRect{115, 116, 90, 68},
        ScreenRect{213, 116, 95, 66},
        ScreenRect{64, 103, 192, 12},
        ScreenRect{0, 74, 11, 45},
        ScreenRect{308, 74, 12, 45},
        11,
        62,
    },
    CockpitLayout{
        CockpitFamily::Medium,
        "medium",
        "MEDIUM.SCR",
        ScreenRect{9, 0, 301, 103},
        ScreenRect{3, 110, 104, 70},
        ScreenRect{113, 116, 94, 66},
        ScreenRect{214, 110, 104, 70},
        ScreenRect{64, 103, 192, 12},
        ScreenRect{0, 0, 9, 103},
        ScreenRect{310, 0, 10, 103},
        4,
        51,
    },
    CockpitLayout{
        CockpitFamily::Heavy,
        "heavy",
        "HEAVY.SCR",
        ScreenRect{0, 0, 320, 103},
        ScreenRect{4, 113, 106, 75},
        ScreenRect{120, 117, 72, 67},
        ScreenRect{206, 112, 111, 76},
        ScreenRect{96, 103, 128, 12},
        ScreenRect{0, 0, 0, 0},
        ScreenRect{0, 0, 0, 0},
        4,
        51,
    },
};

constexpr int kLightCockpitSpeedGaugeX = 236;
constexpr int kLightCockpitSpeedGaugeY = 177;
constexpr int kLightCockpitSpeedGaugeBarHeight = 7;
constexpr int kLightCockpitSpeedGaugeBarStride = 2;
constexpr int kLightCockpitSpeedGaugeBarCount = 31;
constexpr int kLightCockpitSpeedGaugeZeroIndex = 9;
constexpr int kLightCockpitSpeedGaugeMaxForwardBars = 16;
constexpr double kBattleDriveThrottleStep = 1.0 / static_cast<double>(kLightCockpitSpeedGaugeMaxForwardBars);
constexpr int kHeavyCockpitSpeedGaugeX = 22;
constexpr int kHeavyCockpitSpeedGaugeY = 186;
constexpr int kLightCockpitJumpGaugeRightInset = 9;
constexpr int kLightCockpitJumpGaugeTopInset = 16;
constexpr int kLightCockpitJumpGaugeFuelWidth = 6;
constexpr int kLightCockpitJumpGaugeHighlightWidth = 1;
constexpr int kLightCockpitJumpGaugeHeight = 40;
constexpr int kLightCockpitJumpReadyLightRightInset = 19;
constexpr int kLightCockpitJumpReadyLightTopInset = 5;
constexpr ScreenRect kLightCockpitMinimapRect{120, 117, 72, 67};
constexpr int kLightCockpitCrosshairCenterY = 62;
constexpr int kLightCockpitCrosshairStepPixels = 3;
constexpr int kLightCockpitCrosshairSegmentLength = 5;
constexpr int kLightCockpitCrosshairCenterGap = 3;
constexpr int kBattleCockpitMinZoomLevel = 1;
constexpr int kBattleCockpitMaxZoomLevel = 3;

constexpr std::array<int, 6> kLightCockpitStrutOverlayIndices{{0, 1, 2, 3, 4, 11}};
constexpr std::array<int, 5> kMediumCockpitStrutOverlayIndices{{5, 7, 6, 8, 11}};
constexpr std::array<int, 3> kHeavyCockpitStrutOverlayIndices{{9, 10, 11}};

Vec3 subtract(Vec3 a, Vec3 b) {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 cross(Vec3 a, Vec3 b) {
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

Vec3 normalize(Vec3 value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.00001f) {
        return Vec3{0.0f, 0.0f, 1.0f};
    }
    return Vec3{value.x / length, value.y / length, value.z / length};
}

struct TerrainPalette {
    std::string id;
    std::array<float, 3> skyRgb;
    std::array<float, 3> groundRgb;
    std::array<float, 3> gridRgb;
    std::array<float, 3> objectDarkRgb;
    std::array<float, 3> objectLightRgb;
    std::array<float, 3> edgeRgb;
};

using TerrainShadePalette = std::array<std::array<float, 3>, 32>;

const TerrainPalette& terrainPaletteById(const std::string& id) {
    static const std::array<TerrainPalette, 5> palettes{{
        {"debug", {0.035f, 0.035f, 0.040f}, {0.12f, 0.19f, 0.10f}, {0.02f, 0.02f, 0.02f}, {0.35f, 0.42f, 0.18f}, {0.96f, 0.91f, 0.38f}, {0.015f, 0.015f, 0.015f}},
        {"flat", {0.035f, 0.035f, 0.040f}, {0.12f, 0.19f, 0.10f}, {0.02f, 0.02f, 0.02f}, {0.37f, 0.48f, 0.23f}, {0.74f, 0.78f, 0.49f}, {0.015f, 0.015f, 0.015f}},
        {"desert", {0.008f, 0.008f, 0.006f}, {0.69f, 0.36f, 0.00f}, {0.28f, 0.12f, 0.01f}, {0.59f, 0.29f, 0.09f}, {0.94f, 0.70f, 0.28f}, {0.08f, 0.03f, 0.005f}},
        {"green", {0.035f, 0.16f, 0.36f}, {0.11f, 0.47f, 0.13f}, {0.025f, 0.17f, 0.04f}, {0.17f, 0.42f, 0.13f}, {0.58f, 0.78f, 0.36f}, {0.02f, 0.08f, 0.025f}},
        {"snow", {0.08f, 0.50f, 0.62f}, {0.79f, 0.91f, 0.90f}, {0.35f, 0.62f, 0.66f}, {0.38f, 0.57f, 0.61f}, {0.90f, 0.99f, 1.00f}, {0.05f, 0.17f, 0.20f}},
    }};
    const auto found = std::find_if(palettes.begin(), palettes.end(), [&id](const TerrainPalette& palette) {
        return palette.id == id;
    });
    if (found == palettes.end()) {
        throw std::runtime_error("unknown terrain color mode: " + id);
    }
    return *found;
}

std::array<float, 3> commandMapGroundRgb(const std::string& terrainColorMode) {
    if (terrainColorMode == "desert") {
        return {170.0f / 255.0f, 85.0f / 255.0f, 0.0f};
    }
    if (terrainColorMode == "snow") {
        return {1.0f, 1.0f, 1.0f};
    }
    return {0.0f, 170.0f / 255.0f, 0.0f};
}

const char* commandMapGroundName(const std::string& terrainColorMode) {
    if (terrainColorMode == "desert") {
        return "ega_desert";
    }
    if (terrainColorMode == "snow") {
        return "ega_snow";
    }
    return "ega_green";
}

uint8_t terrainPalBank1Byte(const std::string& id, uint8_t index) {
    static constexpr std::array<uint8_t, 16> desert{{
        0x6fu, 0x84u, 0x76u, 0x44u, 0x86u, 0xe6u, 0x36u, 0x76u,
        0x86u, 0x78u, 0x70u, 0xf6u, 0x8fu, 0x80u, 0x64u, 0x7fu,
    }};
    static constexpr std::array<uint8_t, 16> tropic{{
        0x2au, 0x12u, 0xaau, 0x32u, 0x82u, 0xe2u, 0x32u, 0x72u,
        0x82u, 0x78u, 0x70u, 0xf2u, 0x8fu, 0x80u, 0x62u, 0x7fu,
    }};
    static constexpr std::array<uint8_t, 16> arctic{{
        0xf7u, 0x07u, 0x77u, 0x88u, 0x8fu, 0xffu, 0x38u, 0x7fu,
        0x87u, 0x78u, 0x70u, 0xffu, 0x8fu, 0x37u, 0x83u, 0x7fu,
    }};

    const size_t offset = static_cast<size_t>(index & 0x0fu);
    if (id == "desert") {
        return desert[offset];
    }
    if (id == "snow") {
        return arctic[offset];
    }
    return tropic[offset];
}

std::array<float, 3> egaRgbFloat(uint8_t color) {
    switch (color & 0x0fu) {
    case 0:
        return {0.0f, 0.0f, 0.0f};
    case 1:
        return {0.0f, 0.0f, 170.0f / 255.0f};
    case 2:
        return {0.0f, 170.0f / 255.0f, 0.0f};
    case 3:
        return {0.0f, 170.0f / 255.0f, 170.0f / 255.0f};
    case 4:
        return {170.0f / 255.0f, 0.0f, 0.0f};
    case 5:
        return {170.0f / 255.0f, 0.0f, 170.0f / 255.0f};
    case 6:
        return {170.0f / 255.0f, 85.0f / 255.0f, 0.0f};
    case 7:
        return {170.0f / 255.0f, 170.0f / 255.0f, 170.0f / 255.0f};
    case 8:
        return {85.0f / 255.0f, 85.0f / 255.0f, 85.0f / 255.0f};
    case 9:
        return {85.0f / 255.0f, 85.0f / 255.0f, 1.0f};
    case 10:
        return {85.0f / 255.0f, 1.0f, 85.0f / 255.0f};
    case 11:
        return {85.0f / 255.0f, 1.0f, 1.0f};
    case 12:
        return {1.0f, 85.0f / 255.0f, 85.0f / 255.0f};
    case 13:
        return {1.0f, 85.0f / 255.0f, 1.0f};
    case 14:
        return {1.0f, 1.0f, 85.0f / 255.0f};
    default:
        return {1.0f, 1.0f, 1.0f};
    }
}

std::array<float, 3> terrainShadeRgb(uint8_t shade, const std::string& terrainColorMode) {
    if (terrainColorMode == "debug" || terrainColorMode == "flat" || shade < 16u) {
        return egaRgbFloat(shade);
    }

    // TERPCK polygons commonly carry A,A,B,B shade pairs; values 16..31 line up
    // with the terrain-specific PAL:EGA bank-1 remap rather than grayscale light.
    const uint8_t remapped = terrainPalBank1Byte(terrainColorMode, shade & 0x0fu);
    uint8_t color = remapped >> 4u;
    if (color == 1u || color == 3u || color == 5u || color == 9u || color == 11u) {
        color = remapped & 0x0fu;
    }
    return egaRgbFloat(color);
}

std::array<float, 3> terrainShadeBaseRgb(uint8_t shade, const std::string& terrainColorMode) {
    if (terrainColorMode == "debug" || terrainColorMode == "flat" || shade < 16u) {
        return egaRgbFloat(shade);
    }
    return egaRgbFloat(terrainPalBank1Byte(terrainColorMode, shade & 0x0fu) & 0x0fu);
}

std::array<float, 3> terrainShadeOverlayRgb(uint8_t shade, const std::string& terrainColorMode) {
    if (terrainColorMode == "debug" || terrainColorMode == "flat" || shade < 16u) {
        return egaRgbFloat(shade);
    }
    const uint8_t remapped = terrainPalBank1Byte(terrainColorMode, shade & 0x0fu);
    uint8_t color = remapped >> 4u;
    if (color == 1u || color == 3u || color == 5u || color == 9u || color == 11u) {
        color = terrainColorMode == "desert" || terrainColorMode == "green" ? 0u : (remapped & 0x0fu);
    } else if (terrainColorMode == "green" && color == 2u) {
        color = 0u;
    }
    return egaRgbFloat(color);
}

std::array<float, 3> terrainObjectColor(const std::array<float, 3>& source, const TerrainPalette& palette) {
    const float luminance = std::clamp(source[0] * 0.2126f + source[1] * 0.7152f + source[2] * 0.0722f, 0.0f, 1.0f);
    return {
        palette.objectDarkRgb[0] + (palette.objectLightRgb[0] - palette.objectDarkRgb[0]) * luminance,
        palette.objectDarkRgb[1] + (palette.objectLightRgb[1] - palette.objectDarkRgb[1]) * luminance,
        palette.objectDarkRgb[2] + (palette.objectLightRgb[2] - palette.objectDarkRgb[2]) * luminance,
    };
}

std::array<float, 3> terrainSkyColor(const std::string& skyMode, const TerrainPalette& palette) {
    if (skyMode == "auto") {
        return palette.skyRgb;
    }
    if (skyMode == "black") {
        return {0.0f, 0.0f, 0.0f};
    }
    if (skyMode == "blue") {
        return {0.08f, 0.50f, 0.62f};
    }
    if (skyMode == "white") {
        return {1.0f, 1.0f, 1.0f};
    }
    throw std::runtime_error("unknown terrain sky mode: " + skyMode);
}

struct TerrainGroundPatchStats {
    size_t vertices = 0;
    size_t triangles = 0;
    std::array<size_t, 4> verticesByBand{0, 0, 0, 0};
    std::array<size_t, 4> trianglesByBand{0, 0, 0, 0};
};

bool terrainObjectEdgesVisible(const std::string& terrainColorMode, bool wire) {
    return wire || terrainColorMode == "debug";
}

std::array<float, 3> cockpitMinimapGroundPatchRgb(uint8_t colorBand) {
    switch (std::min<uint8_t>(colorBand, 3u)) {
    case 0:
        return {85.0f / 255.0f, 1.0f, 85.0f / 255.0f};
    case 1:
        return {1.0f, 1.0f, 85.0f / 255.0f};
    case 2:
        return {1.0f, 85.0f / 255.0f, 85.0f / 255.0f};
    default:
        return {170.0f / 255.0f, 0.0f, 0.0f};
    }
}

TerrainGroundPatchStats terrainGroundPatchStats(const mw::legacy3d::TerrainMesh& mesh) {
    TerrainGroundPatchStats stats;
    for (const mw::legacy3d::TerrainMeshVertex& vertex : mesh.vertices) {
        if (vertex.rawValue == 0u) {
            continue;
        }
        ++stats.vertices;
        ++stats.verticesByBand[std::min<size_t>(vertex.colorBand, 3u)];
    }
    for (size_t i = 0; i + 2u < mesh.indices.size(); i += 3u) {
        const uint32_t ia = mesh.indices[i];
        const uint32_t ib = mesh.indices[i + 1u];
        const uint32_t ic = mesh.indices[i + 2u];
        if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size()) {
            continue;
        }
        const mw::legacy3d::TerrainMeshVertex& a = mesh.vertices[ia];
        const mw::legacy3d::TerrainMeshVertex& b = mesh.vertices[ib];
        const mw::legacy3d::TerrainMeshVertex& c = mesh.vertices[ic];
        if (a.rawValue == 0u && b.rawValue == 0u && c.rawValue == 0u) {
            continue;
        }
        uint8_t band = 0;
        if (a.rawValue != 0u) {
            band = std::max(band, a.colorBand);
        }
        if (b.rawValue != 0u) {
            band = std::max(band, b.colorBand);
        }
        if (c.rawValue != 0u) {
            band = std::max(band, c.colorBand);
        }
        ++stats.triangles;
        ++stats.trianglesByBand[std::min<size_t>(band, 3u)];
    }
    return stats;
}

struct Bounds {
    Vec3 min{};
    Vec3 max{};
    Vec3 center{};
    float radius = 1.0f;
};

struct GlFunctions {
    GlGenBuffers glGenBuffers = nullptr;
    GlBindBuffer glBindBuffer = nullptr;
    GlBufferData glBufferData = nullptr;
    GlDeleteBuffers glDeleteBuffers = nullptr;
};

struct GpuMesh {
    GLuint vbo = 0;
    GLuint triEbo = 0;
    GLuint lineEbo = 0;
    GLsizei triangleIndexCount = 0;
    GLsizei lineIndexCount = 0;
    int vertexCount = 0;
    int triangleCount = 0;
    int linePairCount = 0;
};

struct CockpitScrImage {
    int width = 0;
    int height = 0;
    int codec = 0;
    std::string decodedLayout;
    std::string paletteName = "EGA";
    std::vector<uint8_t> rgba;
};

struct CockpitBmpSprite {
    int index = 0;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

struct CockpitSpriteTexture {
    int index = 0;
    int width = 0;
    int height = 0;
    std::array<float, 3> fillRgb{1.0f, 1.0f, 1.0f};
    GLuint texture = 0;
};

struct CockpitFont {
    int width = 0;
    int height = 0;
    int firstCode = 0;
    int glyphCount = 0;
    std::vector<uint8_t> rows;
};

std::array<float, 3> cockpitSpriteCenterRgb(const CockpitBmpSprite& sprite) {
    if (sprite.width <= 0 || sprite.height <= 0 || sprite.rgba.empty()) {
        return {1.0f, 1.0f, 1.0f};
    }
    const int x = sprite.width / 2;
    const int y = sprite.height / 2;
    const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(sprite.width) + static_cast<size_t>(x)) * 4u;
    if (offset + 2u >= sprite.rgba.size()) {
        return {1.0f, 1.0f, 1.0f};
    }
    return {
        static_cast<float>(sprite.rgba[offset + 0u]) / 255.0f,
        static_cast<float>(sprite.rgba[offset + 1u]) / 255.0f,
        static_cast<float>(sprite.rgba[offset + 2u]) / 255.0f,
    };
}

enum class PoseDemoAxis {
    X,
    Y,
    Z,
};

enum class PosePivotMode {
    Definition,
    BoundsCenter,
    BoundsBottom,
    BoundsTop,
};

std::wstring widenAscii(const std::string& text) {
    return std::wstring(text.begin(), text.end());
}

std::string narrowPath(const std::filesystem::path& path) {
    return path.string();
}

std::string normalizeId(std::string text) {
    for (char& ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

const CockpitLayout& cockpitLayoutByFamily(CockpitFamily family) {
    for (const CockpitLayout& layout : kCockpitLayouts) {
        if (layout.family == family) {
            return layout;
        }
    }
    return kCockpitLayouts[0];
}

const ScreenRect& cockpitJumpGaugeAnchorPanel(CockpitFamily family) {
    const CockpitFamily anchorFamily = family == CockpitFamily::Medium ? CockpitFamily::Light : family;
    return cockpitLayoutByFamily(anchorFamily).rightPanel;
}

ScreenRect cockpitJumpGaugeRect(CockpitFamily family) {
    const ScreenRect& panel = cockpitJumpGaugeAnchorPanel(family);
    return ScreenRect{
        panel.x + panel.width - kLightCockpitJumpGaugeRightInset,
        panel.y + kLightCockpitJumpGaugeTopInset,
        kLightCockpitJumpGaugeFuelWidth + kLightCockpitJumpGaugeHighlightWidth,
        kLightCockpitJumpGaugeHeight,
    };
}

ScreenRect cockpitJumpReadyLightRect(CockpitFamily family) {
    const ScreenRect& panel = cockpitJumpGaugeAnchorPanel(family);
    return ScreenRect{
        panel.x + panel.width - kLightCockpitJumpReadyLightRightInset + 12,
        panel.y + kLightCockpitJumpReadyLightTopInset + 3,
        3,
        3,
    };
}

bool cockpitFamilyShowsJumpGauge(CockpitFamily family) {
    return family != CockpitFamily::Heavy;
}

std::vector<CockpitStrutPlacement> cockpitStrutPlacements(CockpitFamily family) {
    const ScreenRect viewport = cockpitLayoutByFamily(family).viewport;
    const int centerX = viewport.x + viewport.width / 2;
    if (family == CockpitFamily::Heavy) {
        return {
            CockpitStrutPlacement{kHeavyCockpitStrutOverlayIndices[0], viewport.x, viewport.y},
            CockpitStrutPlacement{kHeavyCockpitStrutOverlayIndices[1], viewport.x + viewport.width - 32, viewport.y},
            CockpitStrutPlacement{kHeavyCockpitStrutOverlayIndices[2], centerX - 52 - 3, viewport.y + viewport.height - 3},
        };
    }
    if (family == CockpitFamily::Medium) {
        return {
            CockpitStrutPlacement{kMediumCockpitStrutOverlayIndices[0], viewport.x - 1, viewport.y},
            CockpitStrutPlacement{kMediumCockpitStrutOverlayIndices[1], viewport.x + viewport.width - 16 - 1 + 3, viewport.y},
            CockpitStrutPlacement{kMediumCockpitStrutOverlayIndices[2], viewport.x + 2 - 3, viewport.y + viewport.height - 14},
            CockpitStrutPlacement{kMediumCockpitStrutOverlayIndices[3], viewport.x + viewport.width - 16 + 2, viewport.y + viewport.height - 14},
            CockpitStrutPlacement{kMediumCockpitStrutOverlayIndices[4], centerX - 52 - 3, viewport.y + viewport.height - 3},
        };
    }
    return {
        CockpitStrutPlacement{kLightCockpitStrutOverlayIndices[0], viewport.x - 3, viewport.y},
        CockpitStrutPlacement{kLightCockpitStrutOverlayIndices[1], viewport.x + viewport.width - 24 + 4, viewport.y},
        CockpitStrutPlacement{kLightCockpitStrutOverlayIndices[2], viewport.x - 3, viewport.y + viewport.height - 30},
        CockpitStrutPlacement{kLightCockpitStrutOverlayIndices[3], viewport.x + viewport.width - 40 + 4, viewport.y + viewport.height - 30},
        CockpitStrutPlacement{kLightCockpitStrutOverlayIndices[4], centerX - 40 + 3, viewport.y},
        CockpitStrutPlacement{kLightCockpitStrutOverlayIndices[5], centerX - 52 - 3, viewport.y + viewport.height - 3},
    };
}

std::string canonicalCockpitFamily(std::string id) {
    id = normalizeId(std::move(id));
    if (id == "default") {
        return "auto";
    }
    if (id == "lt") {
        return "light";
    }
    if (id == "med") {
        return "medium";
    }
    if (id == "hvy") {
        return "heavy";
    }
    return id;
}

CockpitFamily cockpitFamilyForPreset(const std::string& presetId) {
    const std::string id = normalizeId(presetId);
    if (id == "locust" || id == "jenner" || id == "wasp") {
        return CockpitFamily::Light;
    }
    if (id == "phoenix_hawk" || id == "shadow_hawk" || id == "wolverine") {
        return CockpitFamily::Medium;
    }
    return CockpitFamily::Heavy;
}

bool cockpitFamilyUsesSharedHud(CockpitFamily family) {
    return family == CockpitFamily::Light || family == CockpitFamily::Medium || family == CockpitFamily::Heavy;
}

CockpitFamily resolveCockpitFamily(const Options& options) {
    const std::string family = canonicalCockpitFamily(options.battleCockpitFamily);
    if (family == "auto") {
        return cockpitFamilyForPreset(options.mechPreset);
    }
    if (family == "light") {
        return CockpitFamily::Light;
    }
    if (family == "medium") {
        return CockpitFamily::Medium;
    }
    if (family == "heavy") {
        return CockpitFamily::Heavy;
    }
    throw std::runtime_error("--battle-cockpit-family must be auto, light, medium, or heavy");
}

const CockpitLayout& cockpitLayoutForOptions(const Options& options) {
    return cockpitLayoutByFamily(resolveCockpitFamily(options));
}

const char* cockpitPaletteNameForOptions(const Options& options) {
    std::optional<int> environmentId = options.terrainEnvironmentId;
    if (options.battleSnapshot.has_value() && options.battleSnapshot->terrain.environmentId.has_value()) {
        environmentId = options.battleSnapshot->terrain.environmentId;
    }
    if (environmentId.has_value()) {
        switch (*environmentId) {
        case 0:
            return "DESERT.PAL";
        case 2:
            return "ARCTIC.PAL";
        default:
            return "TROPIC.PAL";
        }
    }
    return "TROPIC.PAL";
}

BattlefieldBoundaryPaletteIndices battlefieldBoundaryPaletteIndices(const Options& options) {
    std::optional<int> environmentId = options.terrainEnvironmentId;
    if (options.battleSnapshot.has_value() && options.battleSnapshot->terrain.environmentId.has_value()) {
        environmentId = options.battleSnapshot->terrain.environmentId;
    }
    switch (environmentId.value_or(1)) {
    case 0:
        return BattlefieldBoundaryPaletteIndices{15, 4};
    case 2:
        return BattlefieldBoundaryPaletteIndices{8, 14};
    default:
        return BattlefieldBoundaryPaletteIndices{15, 11};
    }
}

BattlefieldBoundaryPaletteIndices battlefieldBoundarySourcePaletteIndices(const Options& options) {
    std::optional<int> environmentId = options.terrainEnvironmentId;
    if (options.battleSnapshot.has_value() && options.battleSnapshot->terrain.environmentId.has_value()) {
        environmentId = options.battleSnapshot->terrain.environmentId;
    }
    switch (environmentId.value_or(1)) {
    case 0:
        return BattlefieldBoundaryPaletteIndices{11, 4};
    case 2:
        return BattlefieldBoundaryPaletteIndices{10, 14};
    default:
        return BattlefieldBoundaryPaletteIndices{11, 15};
    }
}

uint8_t battlefieldMapAllowedExitMask(const mw::battle::BattleSnapshot& snapshot) {
    if (!snapshot.battlefieldBoundary.valid ||
        !snapshot.battlefieldBoundary.exitMaskSemanticsProven ||
        !snapshot.setup.valid) {
        return 0;
    }

    uint8_t mask = 0;
    if (snapshot.setup.playerMode == 1) {
        mask = snapshot.battlefieldBoundary.playerAllowedExitMask;
    }
    if (snapshot.setup.opposingMode == 1) {
        mask = snapshot.battlefieldBoundary.opposingAllowedExitMask;
    }
    return mask;
}

double headingDegrees(double radians) {
    constexpr double pi = 3.14159265358979323846;
    double degrees = std::fmod(radians * 180.0 / pi, 360.0);
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

double cockpitDisplayHeadingDegrees(double radians) {
    constexpr double pi = 3.14159265358979323846;
    return headingDegrees(pi - radians);
}

double normalizeDegrees(double degrees) {
    degrees = std::fmod(degrees, 360.0);
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return degrees;
}

double signedCompassDeltaDegrees(double valueDegrees, double centerDegrees) {
    double delta = normalizeDegrees(valueDegrees) - normalizeDegrees(centerDegrees);
    if (delta >= 180.0) {
        delta -= 360.0;
    }
    if (delta < -180.0) {
        delta += 360.0;
    }
    return delta;
}

int compassMajorIndexForHeading(double headingDegreesValue) {
    const int rounded = static_cast<int>(std::floor((normalizeDegrees(headingDegreesValue) + 15.0) / 30.0)) % 12;
    return rounded < 0 ? rounded + 12 : rounded;
}

std::string compassMajorLabel(int index) {
    const int degrees = ((index % 12) + 12) % 12 * 30;
    std::ostringstream out;
    out << std::setw(3) << std::setfill('0') << degrees;
    return out.str();
}

const std::vector<CockpitHudColor>& cockpitHudColorCycle() {
    static const std::vector<CockpitHudColor> colors{
        {"green", {0.0f, 0.67f, 0.0f}},
        {"black", {0.0f, 0.0f, 0.0f}},
        {"red", {0.67f, 0.0f, 0.0f}},
        {"cyan", {85.0f / 255.0f, 1.0f, 1.0f}},
        {"brown", {0.67f, 0.33f, 0.0f}},
        {"gray", {0.67f, 0.67f, 0.67f}},
        {"darkgray", {0.33f, 0.33f, 0.33f}},
        {"blue", {0.0f, 0.0f, 0.67f}},
        {"brightgreen", {85.0f / 255.0f, 1.0f, 85.0f / 255.0f}},
        {"brightcyan", {85.0f / 255.0f, 1.0f, 1.0f}},
        {"lightred", {1.0f, 85.0f / 255.0f, 85.0f / 255.0f}},
        {"brightmagenta", {1.0f, 85.0f / 255.0f, 1.0f}},
        {"yellow", {1.0f, 1.0f, 85.0f / 255.0f}},
        {"white", {1.0f, 1.0f, 1.0f}},
        {"black2", {0.0f, 0.0f, 0.0f}},
        {"darkblue", {0.0f, 0.0f, 0.33f}},
        {"darkgreen", {0.0f, 0.33f, 0.0f}},
    };
    return colors;
}

constexpr size_t kDefaultCockpitHudColorIndex = 3u;

std::string compassCardinal(double degrees) {
    static const std::array<const char*, 8> labels{{"N", "NE", "E", "SE", "S", "SW", "W", "NW"}};
    const int index = static_cast<int>(std::floor((degrees + 22.5) / 45.0)) % static_cast<int>(labels.size());
    return labels[static_cast<size_t>(index)];
}

const mw::battle::CombatantSnapshot* primaryCombatantSnapshot(const mw::battle::BattleSnapshot& snapshot) {
    const auto it = std::find_if(
        snapshot.combatants.begin(),
        snapshot.combatants.end(),
        [](const mw::battle::CombatantSnapshot& combatant) {
            return combatant.playerControlled;
        });
    if (it != snapshot.combatants.end()) {
        return &*it;
    }
    if (snapshot.combatants.empty()) {
        return nullptr;
    }
    return &snapshot.combatants.front();
}

CockpitHudState cockpitHudStateFromOptions(const Options& options) {
    CockpitHudState state;
    state.mode = battleViewModeName(options);
    if (options.battleCockpit) {
        state.camera = options.battleCockpitExternalCamera ? "external" : "cockpit";
    } else if (options.battleDrive) {
        state.camera = "follow";
    }
    if (options.terrainScenarioIndex.has_value()) {
        state.scenario = std::to_string(*options.terrainScenarioIndex);
    }
    if (!options.battleSnapshot.has_value()) {
        return state;
    }

    const mw::battle::CombatantSnapshot* combatant = primaryCombatantSnapshot(*options.battleSnapshot);
    if (combatant != nullptr) {
        state.bodyHeadingDegrees = cockpitDisplayHeadingDegrees(combatant->transform.headingRadians);
        state.headingDegrees = state.bodyHeadingDegrees;
        state.speed = combatant->forwardSpeed;
        state.targetSpeed = combatant->targetForwardSpeed;
        state.maxForwardSpeed = combatant->maxForwardSpeed;
        state.maxReverseSpeed = combatant->maxReverseSpeed;
        state.torsoDegrees = combatant->torsoYawRadians * 180.0 / 3.14159265358979323846;
        state.aimPitchStep = combatant->aimPitchStep;
        state.boundaryContact = combatant->boundaryContact;
        state.jumpJetsEnabled = combatant->jumpJetsEnabled;
        state.jumpCapable = combatant->jumpCapable;
        state.jumpJetReady = combatant->jumpJetReady;
        state.jumpJetThrusting = combatant->jumpJetThrusting;
        state.airborne = combatant->airborne;
        state.hardLanding = combatant->hardLanding;
        state.jumpFuel = combatant->jumpFuel;
        state.jumpMaxFuel = combatant->jumpMaxFuel;
        state.jumpActivationFuel = combatant->jumpActivationFuel;
        state.verticalSpeed = combatant->verticalSpeed;
    }
    if (options.battleSnapshot->camera.valid) {
        state.headingDegrees = cockpitDisplayHeadingDegrees(options.battleSnapshot->camera.transform.headingRadians);
    }
    state.compass = compassCardinal(state.headingDegrees);
    return state;
}

LightCockpitSpeedGaugeState lightCockpitSpeedGaugeState(const CockpitHudState& hud) {
    LightCockpitSpeedGaugeState state;
    const double scaleSpeed = std::max(1.0, hud.maxForwardSpeed);
    const auto filledBars = [scaleSpeed](double speed) {
        const double normalized = std::clamp(std::abs(speed) / scaleSpeed, 0.0, 1.0);
        return static_cast<int>(std::floor(normalized * kLightCockpitSpeedGaugeMaxForwardBars + 0.000001));
    };
    if (hud.speed > 0.0001) {
        state.forwardBars = std::min(
            kLightCockpitSpeedGaugeBarCount - kLightCockpitSpeedGaugeZeroIndex - 1,
            filledBars(hud.speed));
    } else if (hud.speed < -0.0001) {
        state.reverseBars = std::min(kLightCockpitSpeedGaugeZeroIndex, filledBars(hud.speed));
    }
    return state;
}

bool containsComponentId(const std::vector<int>& ids, int componentId) {
    return std::find(ids.begin(), ids.end(), componentId) != ids.end();
}

CockpitSystemIndicatorDamage cockpitSystemIndicatorDamageFromSnapshot(
    const std::optional<mw::battle::BattleSnapshot>& snapshot) {
    CockpitSystemIndicatorDamage damage;
    if (!snapshot.has_value()) {
        return damage;
    }
    const mw::battle::CombatantSnapshot* player = primaryCombatantSnapshot(*snapshot);
    if (player == nullptr) {
        return damage;
    }

    auto componentDamaged = [&](int componentId) {
        return containsComponentId(player->destroyedComponentIds, componentId) ||
               containsComponentId(player->disabledComponentIds, componentId);
    };

    // Provisional 3D-component bridge until 2D mech system state is handed into BattleSnapshot.
    constexpr int kTorsoComponentId = 2;
    constexpr int kCockpitComponentId = 3;
    if (componentDamaged(kCockpitComponentId)) {
        damage.sensors = true;
        damage.lifeSupport = true;
    }
    if (componentDamaged(kTorsoComponentId)) {
        damage.gyros = true;
        damage.engines = true;
    }
    return damage;
}

std::string cockpitSystemDamageSpec(const CockpitSystemIndicatorDamage& damage) {
    std::vector<std::string> labels;
    if (damage.sensors) {
        labels.push_back("S");
    }
    if (damage.gyros) {
        labels.push_back("G");
    }
    if (damage.engines) {
        labels.push_back("E");
    }
    if (damage.lifeSupport) {
        labels.push_back("L");
    }
    if (labels.empty()) {
        return "none";
    }
    std::ostringstream out;
    for (size_t index = 0; index < labels.size(); ++index) {
        if (index != 0u) {
            out << ",";
        }
        out << labels[index];
    }
    return out.str();
}

std::string rectSpec(const ScreenRect& rect) {
    std::ostringstream out;
    out << rect.x << "," << rect.y << "," << rect.width << "," << rect.height;
    return out.str();
}

void printUsage() {
    std::cerr
        << "usage: mw_battle_viewer [--list-mech-presets] [--list-battle-missions] [--list-terrain-arenas] [--list-terrain-layouts] [--list-terrain-scenarios] [--smoke] [--resource PATH] [--record N] [--shade-mode stored|ega|lit] [--wire] [--ortho] [--assembled] [--assembly-records 0,1,2] [--mech-preset PRESET] [--animation walk|death|destroyed_slot_probe|death_forward|death_backward] [--destroy-component N] [--hide-component N] [--animation-records 22,23,24,25] [--animation-component N] [--animation-ms N] [--animation-sequence 0:22,23,24,25:180] [--terrain-arena TILE0] [--terrain-layout-preset control_2x2] [--terrain-layout TILE0,TILE1,TILE8,TILE9] [--terrain-scenario-index N] [--terrain-scenario-file SNARIO.DAT] [--terrain-layout-gap N] [--terrain-cell-size N] [--terrain-object-inset FRACTION] [--terrain-object-scale N] [--terrain-no-object-mirror-y] [--terrain-tile-mirror-x] [--terrain-no-tile-mirror-y] [--terrain-root DIR] [--terrain-environment-id 0|1|2] [--terrain-color debug|flat|desert|green|snow|tropical|jungle|arctic|ice] [--terrain-sky auto|black|blue|white] [--battle-sim-ticks N] [--battle-drive] [--battle-cockpit] [--battle-command-map] [--battle-mission-id N] [--battle-placement manual|original] [--battle-view drive|cockpit|command-map|mission-status] [--battle-cockpit-family auto|light|medium|heavy] [--battle-cockpit-camera cockpit|external] [--battle-cockpit-bob on|off] [--battle-drive-scenario-step N] [--battle-start X,Z] [--battle-start-grid X,Y] [--battle-enemy-start X,Z] [--battle-enemy-start-grid X,Y] [--battle-command-map-target X,Z] [--battle-command-map-target-grid X,Y] [--battle-heading-deg DEG] [--battle-camera-yaw-deg DEG] [--battle-camera-pitch-deg DEG] [--battle-camera-distance N] [--battle-drive-max-forward-speed N] [--battle-drive-max-reverse-speed N] [--battle-drive-accel N] [--battle-drive-decel N] [--battle-drive-turn-rate-deg-per-sec DEG] [--terrain-grid TILE.GRD] [--terrain-world TILE.WLD --terrain-shapes TERPCK.TBL]\n";
}

std::string canonicalTerrainColorMode(std::string id) {
    id = normalizeId(std::move(id));
    if (id == "tropic" || id == "tropical" || id == "jungle") {
        return "green";
    }
    if (id == "ice" || id == "arctic") {
        return "snow";
    }
    return id;
}

std::string terrainColorModeForEnvironmentId(int environmentId) {
    switch (environmentId) {
    case 0:
        return "desert";
    case 1:
        return "green";
    case 2:
        return "snow";
    default:
        throw std::runtime_error("--terrain-environment-id must be 0, 1, or 2");
    }
}

std::string canonicalTerrainSkyMode(std::string id) {
    id = normalizeId(std::move(id));
    if (id == "default" || id == "palette") {
        return "auto";
    }
    if (id == "dark" || id == "night") {
        return "black";
    }
    if (id == "cyan" || id == "sky") {
        return "blue";
    }
    return id;
}

int terrainArenaNumber(const std::string& name) {
    const std::string prefix = "TILE";
    if (name.size() <= prefix.size() || name.substr(0, prefix.size()) != prefix) {
        return 1000000;
    }
    size_t pos = prefix.size();
    while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos])) != 0) {
        ++pos;
    }
    if (pos == prefix.size()) {
        return 1000000;
    }
    return std::stoi(name.substr(prefix.size(), pos - prefix.size()));
}

bool terrainArenaLess(const std::string& a, const std::string& b) {
    const int numberA = terrainArenaNumber(a);
    const int numberB = terrainArenaNumber(b);
    if (numberA != numberB) {
        return numberA < numberB;
    }
    return a < b;
}

std::vector<std::string> discoverTerrainArenaNames(const std::filesystem::path& root) {
    const std::filesystem::path gridDir = root / "GRD";
    const std::filesystem::path worldDir = root / "WLD";
    if (!std::filesystem::is_directory(gridDir)) {
        throw std::runtime_error("terrain GRD directory not found: " + gridDir.string());
    }
    if (!std::filesystem::is_directory(worldDir)) {
        throw std::runtime_error("terrain WLD directory not found: " + worldDir.string());
    }

    std::set<std::string> gridNames;
    std::set<std::string> worldNames;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(gridDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".GRD") {
            gridNames.insert(entry.path().stem().string());
        }
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(worldDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".WLD") {
            worldNames.insert(entry.path().stem().string());
        }
    }

    std::vector<std::string> names;
    for (const std::string& name : gridNames) {
        if (worldNames.find(name) != worldNames.end()) {
            names.push_back(name);
        }
    }
    std::sort(names.begin(), names.end(), terrainArenaLess);
    return names;
}

void resolveTerrainArenaPaths(Options& options) {
    if (options.terrainArena.empty()) {
        return;
    }
    if (options.terrainGridPath.empty()) {
        options.terrainGridPath = options.terrainRoot / "GRD" / (options.terrainArena + ".GRD");
    }
    if (options.terrainWorldPath.empty()) {
        options.terrainWorldPath = options.terrainRoot / "WLD" / (options.terrainArena + ".WLD");
    }
    if (options.terrainShapePath.empty()) {
        options.terrainShapePath = options.terrainRoot / "TBL" / "viewer8" / "TERPCK.TBL";
    }
}

void resolveTerrainLayoutPreset(Options& options) {
    if (options.terrainLayoutPreset.empty()) {
        return;
    }
    if (!options.terrainLayoutNames.empty()) {
        throw std::runtime_error("--terrain-layout-preset cannot be combined with --terrain-layout");
    }
    const mw::legacy3d::TerrainArenaLayout& layout =
        mw::legacy3d::terrainArenaLayoutById(options.terrainLayoutPreset);
    options.terrainLayoutNames = layout.tileNames;
    if (!options.terrainLayoutGapExplicit) {
        options.terrainLayoutGap = layout.layoutGap;
    }
    if (!options.terrainObjectInsetExplicit) {
        options.terrainObjectInset = layout.objectInset;
    }
    options.terrainEnabled = true;
    options.terrainWorldEnabled = true;
}

std::vector<size_t> discoverTerrainScenarioIndices(const std::filesystem::path& path) {
    const std::vector<mw::legacy3d::TerrainScenarioRecord> records =
        mw::legacy3d::loadTerrainScenarioRecords(path);
    std::vector<size_t> indices;
    for (const mw::legacy3d::TerrainScenarioRecord& record : records) {
        if (record.isTerrainLayoutCandidate()) {
            indices.push_back(record.index);
        }
    }
    return indices;
}

void applyTerrainScenarioRecord(Options& options, const mw::legacy3d::TerrainScenarioRecord& record) {
    options.terrainScenarioIndex = record.index;
    options.terrainLayoutNames = mw::legacy3d::terrainScenarioTileNames(record);
    options.terrainScenarioMode = static_cast<int>(record.terrainMode);
    if (!options.terrainLayoutGapExplicit) {
        options.terrainLayoutGap = 0.0f;
    }
    if (!options.terrainObjectInsetExplicit) {
        options.terrainObjectInset = 0.18f;
    }
    options.terrainEnabled = true;
    options.terrainWorldEnabled = true;
}

void applyTerrainScenarioIndex(Options& options, size_t scenarioIndex) {
    if (options.terrainScenarioPath.empty()) {
        options.terrainScenarioPath = options.terrainRoot / "DAT" / "SNARIO.DAT";
    }
    const std::vector<mw::legacy3d::TerrainScenarioRecord> records =
        mw::legacy3d::loadTerrainScenarioRecords(options.terrainScenarioPath);
    const std::optional<mw::legacy3d::TerrainScenarioRecord> record =
        mw::legacy3d::terrainScenarioRecordByIndex(records, scenarioIndex);
    if (!record.has_value()) {
        throw std::runtime_error("--terrain-scenario-index is out of range for " + options.terrainScenarioPath.string());
    }
    if (!record->isTerrainLayoutCandidate()) {
        throw std::runtime_error("--terrain-scenario-index does not reference an active terrain layout candidate: " +
                                 std::to_string(scenarioIndex));
    }
    options.terrainLayoutNames.clear();
    options.terrainLayoutPreset.clear();
    applyTerrainScenarioRecord(options, *record);
}

void applyBattleDriveScenarioStep(Options& options) {
    if (options.battleDriveScenarioStep == 0) {
        return;
    }
    if (!options.terrainScenarioIndex.has_value()) {
        throw std::runtime_error("--battle-drive-scenario-step requires --terrain-scenario-index or --battle-drive defaults");
    }
    if (options.terrainScenarioPath.empty()) {
        options.terrainScenarioPath = options.terrainRoot / "DAT" / "SNARIO.DAT";
    }
    const std::vector<size_t> indices = discoverTerrainScenarioIndices(options.terrainScenarioPath);
    const auto found = std::find(indices.begin(), indices.end(), *options.terrainScenarioIndex);
    if (indices.empty() || found == indices.end()) {
        throw std::runtime_error("--battle-drive-scenario-step requires an active terrain scenario");
    }
    const int count = static_cast<int>(indices.size());
    int next = static_cast<int>(found - indices.begin()) + options.battleDriveScenarioStep;
    next %= count;
    if (next < 0) {
        next += count;
    }
    applyTerrainScenarioIndex(options, indices[static_cast<size_t>(next)]);
}

void resolveTerrainScenarioLayout(Options& options) {
    if (!options.terrainScenarioIndex.has_value()) {
        return;
    }
    if (!options.terrainArena.empty()) {
        throw std::runtime_error("--terrain-scenario-index cannot be combined with --terrain-arena");
    }
    if (!options.terrainLayoutNames.empty() || !options.terrainLayoutPreset.empty()) {
        throw std::runtime_error("--terrain-scenario-index cannot be combined with --terrain-layout or --terrain-layout-preset");
    }
    if (options.terrainScenarioPath.empty()) {
        options.terrainScenarioPath = options.terrainRoot / "DAT" / "SNARIO.DAT";
    }
    applyTerrainScenarioIndex(options, *options.terrainScenarioIndex);
}

void printTerrainArenaList(const std::filesystem::path& root) {
    const std::vector<std::string> names = discoverTerrainArenaNames(root);
    std::cout << "terrain arenas: " << names.size() << "\n";
    for (const std::string& name : names) {
        const std::filesystem::path gridPath = root / "GRD" / (name + ".GRD");
        const std::filesystem::path worldPath = root / "WLD" / (name + ".WLD");
        const mw::legacy3d::TerrainGrid grid = mw::legacy3d::loadTerrainGrid(gridPath);
        const mw::legacy3d::TerrainWorld world = mw::legacy3d::loadTerrainWorld(worldPath);
        size_t nonzero = 0;
        for (const mw::legacy3d::TerrainGridSample& sample : grid.samples) {
            if (!sample.empty()) {
                ++nonzero;
            }
        }
        std::cout << name
                  << "\tgrid_nonzero=" << nonzero
                  << "\twld_objects=" << world.objects.size()
                  << "\n";
    }
}

void printTerrainLayoutList() {
    const std::vector<mw::legacy3d::TerrainArenaLayout>& layouts = mw::legacy3d::terrainArenaLayouts();
    std::cout << "terrain layouts: " << layouts.size() << "\n";
    for (const mw::legacy3d::TerrainArenaLayout& layout : layouts) {
        std::ostringstream tiles;
        for (size_t i = 0; i < layout.tileNames.size(); ++i) {
            if (i > 0) {
                tiles << ",";
            }
            tiles << layout.tileNames[i];
        }
        std::cout << layout.id
                  << "\ttiles=" << tiles.str()
                  << "\tgap=" << layout.layoutGap
                  << "\tinset=" << layout.objectInset
                  << "\tdescription=" << layout.description
                  << "\n";
    }
}

void printTerrainScenarioList(const std::filesystem::path& path) {
    const std::vector<mw::legacy3d::TerrainScenarioRecord> records =
        mw::legacy3d::loadTerrainScenarioRecords(path);
    size_t candidates = 0;
    std::map<std::string, std::set<std::string>> tileRefs;
    for (const mw::legacy3d::TerrainScenarioRecord& record : records) {
        if (record.isTerrainLayoutCandidate()) {
            ++candidates;
            const std::vector<std::string> tileNames = mw::legacy3d::terrainScenarioTileNames(record);
            for (size_t slot = 0; slot < tileNames.size(); ++slot) {
                std::ostringstream ref;
                ref << record.index << ":" << slot;
                tileRefs[tileNames[slot]].insert(ref.str());
            }
        }
    }
    std::cout << "terrain scenarios: " << candidates << " source=" << path.string() << "\n";
    for (const mw::legacy3d::TerrainScenarioRecord& record : records) {
        if (!record.isTerrainLayoutCandidate()) {
            continue;
        }
        const std::vector<std::string> tileNames = mw::legacy3d::terrainScenarioTileNames(record);
        std::ostringstream tiles;
        for (size_t i = 0; i < tileNames.size(); ++i) {
            if (i > 0) {
                tiles << ",";
            }
            tiles << tileNames[i];
        }
        std::cout << "scenario=" << record.index
                  << "\ttiles=" << tiles.str()
                  << "\tmode=" << static_cast<int>(record.terrainMode)
                  << "\tb_variant=" << (record.usesBVariant() ? "yes" : "no")
                  << "\n";
    }
    for (const auto& item : tileRefs) {
        std::ostringstream refs;
        bool first = true;
        for (const std::string& ref : item.second) {
            if (!first) {
                refs << ",";
            }
            refs << ref;
            first = false;
        }
        std::cout << "tile_usage=" << item.first
                  << "\tuses=" << item.second.size()
                  << "\trefs=" << refs.str()
                  << "\n";
    }
}

int parseInt(const std::string& text) {
    size_t used = 0;
    const int value = std::stoi(text, &used, 10);
    if (used != text.size()) {
        throw std::runtime_error("invalid integer: " + text);
    }
    return value;
}

float parseFloat(const std::string& text) {
    size_t used = 0;
    const float value = std::stof(text, &used);
    if (used != text.size()) {
        throw std::runtime_error("invalid number: " + text);
    }
    return value;
}

bool parseOnOff(const std::string& text, const std::string& optionName) {
    const std::string value = normalizeId(text);
    if (value == "on" || value == "yes" || value == "true" || value == "1") {
        return true;
    }
    if (value == "off" || value == "no" || value == "false" || value == "0") {
        return false;
    }
    throw std::runtime_error(optionName + " requires on or off");
}

std::pair<double, double> parseFloatPair(const std::string& text, const std::string& optionName) {
    std::vector<double> values;
    std::stringstream input(text);
    std::string part;
    while (std::getline(input, part, ',')) {
        part.erase(std::remove_if(part.begin(), part.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), part.end());
        if (part.empty()) {
            continue;
        }
        values.push_back(static_cast<double>(parseFloat(part)));
    }
    if (values.size() != 2u) {
        throw std::runtime_error(optionName + " requires two comma-separated numbers");
    }
    return {values[0], values[1]};
}

std::vector<int> parseRecordList(const std::string& text) {
    std::vector<int> indices;
    std::stringstream input(text);
    std::string part;
    while (std::getline(input, part, ',')) {
        part.erase(std::remove_if(part.begin(), part.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), part.end());
        if (part.empty()) {
            continue;
        }
        const int value = parseInt(part);
        if (value < 0) {
            throw std::runtime_error("record index must be non-negative: " + part);
        }
        indices.push_back(value);
    }
    if (indices.empty()) {
        throw std::runtime_error("record list is empty");
    }
    return indices;
}

std::vector<std::string> parseTerrainNameList(const std::string& text) {
    std::vector<std::string> names;
    std::stringstream input(text);
    std::string part;
    while (std::getline(input, part, ',')) {
        part.erase(std::remove_if(part.begin(), part.end(), [](unsigned char ch) { return std::isspace(ch) != 0; }), part.end());
        if (!part.empty()) {
            names.push_back(part);
        }
    }
    if (names.empty()) {
        throw std::runtime_error("terrain name list is empty");
    }
    return names;
}

std::string joinStringList(const std::vector<std::string>& values) {
    std::ostringstream out;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << values[i];
    }
    return out.str();
}

std::string battleSetupSlotGridList(const std::vector<mw::battle::BattleSetupSlotMetadata>& slots) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(2);
    for (size_t i = 0; i < slots.size(); ++i) {
        if (i > 0) {
            out << ";";
        }
        out << slots[i].slotIndex << ":" << slots[i].gridX << "," << slots[i].gridY;
    }
    return out.str();
}

mw::mech3d::ComponentFrameSequence parseAnimationSequence(const std::string& text) {
    const size_t firstColon = text.find(':');
    if (firstColon == std::string::npos) {
        throw std::runtime_error("--animation-sequence must be component:records[:ms]");
    }
    const size_t secondColon = text.find(':', firstColon + 1);

    const std::string componentText = text.substr(0, firstColon);
    const std::string recordsText = secondColon == std::string::npos
                                        ? text.substr(firstColon + 1)
                                        : text.substr(firstColon + 1, secondColon - firstColon - 1);
    const std::string msText = secondColon == std::string::npos ? "" : text.substr(secondColon + 1);

    mw::mech3d::ComponentFrameSequence sequence;
    sequence.componentId = parseInt(componentText);
    if (sequence.componentId < 0) {
        throw std::runtime_error("--animation-sequence component must be non-negative");
    }
    sequence.sourceRecordIndices = parseRecordList(recordsText);
    sequence.frameDurationMs = msText.empty() ? 180 : parseInt(msText);
    if (sequence.frameDurationMs <= 0) {
        throw std::runtime_error("--animation-sequence duration must be positive");
    }
    sequence.debugLabel = "cli_component_" + std::to_string(sequence.componentId) + "_record_frame";
    return sequence;
}

std::string joinRecordList(const std::vector<int>& indices) {
    std::ostringstream out;
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << indices[i];
    }
    return out.str();
}

std::string lightCockpitStrutOverlaySpec() {
    return joinRecordList(std::vector<int>(kLightCockpitStrutOverlayIndices.begin(), kLightCockpitStrutOverlayIndices.end()));
}

std::string mediumCockpitStrutOverlaySpec() {
    return joinRecordList(std::vector<int>(kMediumCockpitStrutOverlayIndices.begin(), kMediumCockpitStrutOverlayIndices.end()));
}

std::string heavyCockpitStrutOverlaySpec() {
    return joinRecordList(std::vector<int>(kHeavyCockpitStrutOverlayIndices.begin(), kHeavyCockpitStrutOverlayIndices.end()));
}

std::string cockpitStrutOverlaySpec(CockpitFamily family) {
    if (family == CockpitFamily::Light) {
        return lightCockpitStrutOverlaySpec();
    }
    if (family == CockpitFamily::Medium) {
        return mediumCockpitStrutOverlaySpec();
    }
    if (family == CockpitFamily::Heavy) {
        return heavyCockpitStrutOverlaySpec();
    }
    return "none";
}

std::string cockpitStrutPlacementSpec(CockpitFamily family) {
    const std::vector<CockpitStrutPlacement> placements = cockpitStrutPlacements(family);
    std::ostringstream out;
    for (size_t i = 0; i < placements.size(); ++i) {
        if (i > 0) {
            out << "|";
        }
        out << placements[i].spriteIndex << "@" << placements[i].x << "," << placements[i].y;
    }
    return out.str();
}

void validateCockpitLayoutSmokeContracts() {
    const ScreenRect lightGauge = cockpitJumpGaugeRect(CockpitFamily::Light);
    const ScreenRect mediumGauge = cockpitJumpGaugeRect(CockpitFamily::Medium);
    const ScreenRect lightReady = cockpitJumpReadyLightRect(CockpitFamily::Light);
    const ScreenRect mediumReady = cockpitJumpReadyLightRect(CockpitFamily::Medium);
    const bool sharedJumpCoordinates =
        lightGauge.x == 299 && lightGauge.y == 132 &&
        mediumGauge.x == lightGauge.x && mediumGauge.y == lightGauge.y &&
        mediumGauge.width == lightGauge.width && mediumGauge.height == lightGauge.height &&
        lightReady.x == 301 && lightReady.y == 124 &&
        mediumReady.x == lightReady.x && mediumReady.y == lightReady.y;
    if (!sharedJumpCoordinates) {
        throw std::runtime_error("light/medium cockpit jump indicator coordinates changed");
    }
    if (!cockpitFamilyShowsJumpGauge(CockpitFamily::Light) ||
        !cockpitFamilyShowsJumpGauge(CockpitFamily::Medium) ||
        cockpitFamilyShowsJumpGauge(CockpitFamily::Heavy)) {
        throw std::runtime_error("cockpit jump indicator family visibility changed");
    }

    const std::vector<CockpitStrutPlacement> mediumStruts = cockpitStrutPlacements(CockpitFamily::Medium);
    if (mediumStruts.size() != 5u ||
        mediumStruts[1].spriteIndex != 7 || mediumStruts[1].x != 296 || mediumStruts[1].y != 0 ||
        mediumStruts[2].spriteIndex != 6 || mediumStruts[2].x != 8 || mediumStruts[2].y != 89) {
        throw std::runtime_error("medium cockpit adjusted corner coordinates changed");
    }
}

std::string formatTerrainBounds(const mw::legacy3d::TerrainMesh& mesh) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << mesh.boundsMin[0] << "," << mesh.boundsMin[1] << "," << mesh.boundsMin[2]
        << ":" << mesh.boundsMax[0] << "," << mesh.boundsMax[1] << "," << mesh.boundsMax[2];
    return out.str();
}

std::string describeAnimationSequences(const Options& options) {
    if (!options.animationName.empty()) {
        return options.mechPreset + "/" + options.animationName;
    }
    if (!options.animationSequences.empty()) {
        std::ostringstream out;
        for (size_t i = 0; i < options.animationSequences.size(); ++i) {
            const mw::mech3d::ComponentFrameSequence& sequence = options.animationSequences[i];
            if (i > 0) {
                out << ";";
            }
            out << sequence.componentId << ":" << joinRecordList(sequence.sourceRecordIndices) << ":" << sequence.frameDurationMs;
        }
        return out.str();
    }
    if (!options.animationRecordIndices.empty()) {
        std::ostringstream out;
        out << options.animationComponentId << ":" << joinRecordList(options.animationRecordIndices) << ":" << options.animationFrameMs;
        return out.str();
    }
    return "";
}

mw::mech3d::MechRuntimeState buildCliRuntimeState(const Options& options) {
    mw::mech3d::MechRuntimeState state;
    if (options.animationEnabled && !options.animationName.empty()) {
        mw::mech3d::setMechRuntimeAnimation(state, options.animationName);
    }
    for (int componentId : options.destroyedComponentIds) {
        mw::mech3d::destroyMechRuntimeComponent(state, componentId);
    }
    for (int componentId : options.hiddenComponentIds) {
        mw::mech3d::hideMechRuntimeComponent(state, componentId);
    }
    return state;
}

std::vector<mw::mech3d::MechComponentDamageRule> cliDamageRules(const Options& options) {
    if (options.mechPreset.empty()) {
        return {};
    }
    return mw::mech3d::catalogComponentDamageRules(options.mechPreset);
}

mw::mech3d::ResolvedMechRuntimeState resolveCliRuntimeState(const Options& options) {
    return mw::mech3d::resolveMechRuntimeState(buildCliRuntimeState(options), cliDamageRules(options));
}

void applyResolvedRuntimeStateToOptions(Options& options) {
    if (options.mechPreset.empty()) {
        return;
    }
    const mw::mech3d::ResolvedMechRuntimeState resolved = resolveCliRuntimeState(options);
    if (!resolved.activeAnimationId.empty() && resolved.activeAnimationId != options.animationName) {
        options.animationName = resolved.activeAnimationId;
        options.animationEnabled = true;
        options.assembled = true;
    }
}

std::string formatVec3(mw::mech3d::Vec3f value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1)
        << value.x << "," << value.y << "," << value.z;
    return out.str();
}

mw::mech3d::Vec3f transformPoint(const mw::mech3d::Mat4f& matrix, mw::mech3d::Vec3f point) {
    return mw::mech3d::Vec3f{
        matrix[0] * point.x + matrix[4] * point.y + matrix[8] * point.z + matrix[12],
        matrix[1] * point.x + matrix[5] * point.y + matrix[9] * point.z + matrix[13],
        matrix[2] * point.x + matrix[6] * point.y + matrix[10] * point.z + matrix[14],
    };
}

std::string poseDemoAxisName(PoseDemoAxis axis) {
    switch (axis) {
    case PoseDemoAxis::X:
        return "x";
    case PoseDemoAxis::Y:
        return "y";
    case PoseDemoAxis::Z:
        return "z";
    }
    return "?";
}

std::string componentPoseAxisName(mw::mech3d::ComponentPoseAxis axis) {
    switch (axis) {
    case mw::mech3d::ComponentPoseAxis::Unknown:
        return "?";
    case mw::mech3d::ComponentPoseAxis::X:
        return "x";
    case mw::mech3d::ComponentPoseAxis::Y:
        return "y";
    case mw::mech3d::ComponentPoseAxis::Z:
        return "z";
    }
    return "?";
}

PoseDemoAxis toPoseDemoAxis(mw::mech3d::ComponentPoseAxis axis, PoseDemoAxis fallback) {
    switch (axis) {
    case mw::mech3d::ComponentPoseAxis::X:
        return PoseDemoAxis::X;
    case mw::mech3d::ComponentPoseAxis::Y:
        return PoseDemoAxis::Y;
    case mw::mech3d::ComponentPoseAxis::Z:
        return PoseDemoAxis::Z;
    case mw::mech3d::ComponentPoseAxis::Unknown:
        return fallback;
    }
    return fallback;
}

std::string posePivotModeName(PosePivotMode mode) {
    switch (mode) {
    case PosePivotMode::Definition:
        return "definition";
    case PosePivotMode::BoundsCenter:
        return "bounds_center";
    case PosePivotMode::BoundsBottom:
        return "bounds_bottom";
    case PosePivotMode::BoundsTop:
        return "bounds_top";
    }
    return "?";
}

std::string shortComponentLabel(const mw::mech3d::ModelAssemblyComponent& component) {
    std::string label = component.debugLabel.empty()
                            ? "component_" + std::to_string(component.componentId)
                            : component.debugLabel;
    const std::array<std::string, 2> mechPrefixes{{"marauder_", "locust_"}};
    for (const std::string& prefix : mechPrefixes) {
        if (label.rfind(prefix, 0) == 0) {
            label.erase(0, prefix.size());
            break;
        }
    }

    const std::array<std::string, 4> suffixMarkers{{
        "_bind_record_",
        "_walk_record_",
        "_destroyed_slot_record_",
        "_record_",
    }};
    for (const std::string& marker : suffixMarkers) {
        const size_t pos = label.find(marker);
        if (pos != std::string::npos && pos > 0) {
            label.erase(pos);
            break;
        }
    }
    return label;
}

std::string componentStateLabel(
    const mw::mech3d::ResolvedMechRuntimeState& runtimeState,
    const mw::mech3d::ResolvedMechComponentState& componentState) {
    if (runtimeState.usesAssemblyFrameDeath) {
        return "death_slot";
    }
    std::vector<std::string> parts;
    if (componentState.destroyed) {
        parts.push_back("destroyed");
    }
    if (componentState.disabled) {
        parts.push_back("disabled");
    }
    if (componentState.hidden) {
        parts.push_back("hidden");
    }
    if (!componentState.visible) {
        parts.push_back("not_visible");
    }
    if (parts.empty()) {
        return "visible";
    }
    std::ostringstream out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out << "+";
        }
        out << parts[i];
    }
    return out.str();
}

std::string destroyedSlotQualityLabel(mw::mech3d::MechCatalogDestroyedSlotQuality quality) {
    switch (quality) {
    case mw::mech3d::MechCatalogDestroyedSlotQuality::Unverified:
        return "unverified";
    case mw::mech3d::MechCatalogDestroyedSlotQuality::AssemblyOnly:
        return "assembly_only";
    case mw::mech3d::MechCatalogDestroyedSlotQuality::ComponentClean:
        return "component_clean";
    }
    return "?";
}

void printMechPresetList() {
    std::cout << "mw_battle_viewer mech presets:\n";
    for (const mw::mech3d::MechCatalogEntry& entry : mw::mech3d::mechCatalogEntries()) {
        std::cout << "  " << entry.presetId
                  << " resource=" << narrowPath(entry.defaultResourcePath)
                  << " death_slots=" << destroyedSlotQualityLabel(entry.destroyedSlotQuality);
        if (!entry.animationIds.empty()) {
            std::cout << " animations=";
            for (size_t i = 0; i < entry.animationIds.size(); ++i) {
                if (i > 0) {
                    std::cout << ",";
                }
                std::cout << entry.animationIds[i];
            }
        }
        std::cout << "\n";
    }
}

void printBattleMissionList() {
    std::cout << "mw_battle_viewer battle missions:\n";
    for (const mw::battle::BattleMissionDefinition& mission : mw::battle::battleMissionDefinitions()) {
        std::cout
            << "  " << static_cast<int>(mission.originalId)
            << " family=" << mw::battle::battleMissionFamilyName(mission.family)
            << " extended=" << (mission.extended ? "yes" : "no")
            << " mw_main_offset=0x" << std::hex << std::uppercase << mission.mwMainFileOffset
            << std::nouppercase << std::dec
            << " title=" << mission.title
            << "\n";
    }
}

mw::battle::BattleReplay viewerSmokeBattleReplay() {
    mw::battle::BattleReplay replay;
    replay.commands = {
        mw::battle::BattleInputCommand{0, {}, 1.0, 0.0},
        mw::battle::BattleInputCommand{2, {}, 1.0, 0.0, 7},
        mw::battle::BattleInputCommand{4, {}, 1.0, 0.5},
        mw::battle::BattleInputCommand{8, {}, 0.0, 0.0},
    };
    return replay;
}

mw::battle::BattleReplay viewerDriveBattleReplay() {
    mw::battle::BattleReplay replay;
    replay.commands = {
        mw::battle::BattleInputCommand{0, {}, 1.0, 0.0},
        mw::battle::BattleInputCommand{6, {}, 1.0, 0.7},
        mw::battle::BattleInputCommand{12, {}, 1.0, 0.7, -4},
        mw::battle::BattleInputCommand{18, {}, 0.45, -0.5},
        mw::battle::BattleInputCommand{26, {}, 0.0, 0.0},
    };
    return replay;
}

std::pair<double, double> gridToWorldPair(const std::pair<double, double>& grid, float cellSize) {
    return {
        grid.first * static_cast<double>(cellSize),
        grid.second * static_cast<double>(cellSize),
    };
}

std::pair<double, double> commandMapTargetWorld(const Options& options) {
    if (options.battleCommandMapTargetWorld.has_value()) {
        return *options.battleCommandMapTargetWorld;
    }
    if (options.battleCommandMapTargetGrid.has_value()) {
        return gridToWorldPair(*options.battleCommandMapTargetGrid, options.terrainCellSize);
    }
    if (options.battleSetup.has_value()) {
        const mw::battle::Transform& target = options.battleSetup->targetTransform;
        return {target.x, target.z};
    }
    return gridToWorldPair({136.0, 70.0}, options.terrainCellSize);
}

mw::battle::BattleObjectiveState battleObjectiveFromOptions(const Options& options) {
    if (options.battleSetup.has_value() &&
        !options.battleCommandMapTargetWorld.has_value() &&
        !options.battleCommandMapTargetGrid.has_value()) {
        return options.battleSetup->objective;
    }

    const std::pair<double, double> targetWorld = commandMapTargetWorld(options);
    mw::battle::BattleObjectiveState objective;
    objective.valid = true;
    objective.role = "target";
    objective.provenance = "manual_diagnostic";
    objective.sourceSlot = "manual";
    objective.transformProven = false;
    objective.activeObjectProven = false;
    objective.missionSemanticsProven = false;
    objective.damagePolicyProven = false;
    objective.damageSuppressed = false;
    objective.depletionPolicyProven = false;
    objective.depletionSetsPlayerWinCondition = false;
    objective.depletionSetsPlayerLossCondition = false;
    objective.depletionResultCode = -1;
    objective.transform = mw::battle::Transform{targetWorld.first, 0.0, targetWorld.second, 0.0};
    objective.gridX = targetWorld.first / static_cast<double>(options.terrainCellSize);
    objective.gridY = targetWorld.second / static_cast<double>(options.terrainCellSize);
    objective.staticModel = mw::battle::originalBattleObjectiveStaticModel();
    return objective;
}

std::pair<double, double> snapshotObjectiveWorldOrFallback(const Options& options) {
    if (options.battleSnapshot.has_value() && options.battleSnapshot->objective.valid) {
        return {options.battleSnapshot->objective.transform.x, options.battleSnapshot->objective.transform.z};
    }
    return commandMapTargetWorld(options);
}

std::optional<double> headingRadiansToward(double fromX, double fromZ, double toX, double toZ) {
    const double dx = toX - fromX;
    const double dz = toZ - fromZ;
    if (std::abs(dx) < 0.0001 && std::abs(dz) < 0.0001) {
        return std::nullopt;
    }
    return std::atan2(dx, dz);
}

const char* battleCommandMapScreenName(BattleCommandMapScreen screen) {
    switch (screen) {
    case BattleCommandMapScreen::MissionStatus:
        return "mission_status";
    case BattleCommandMapScreen::CockpitCommand:
        return "cockpit_command";
    case BattleCommandMapScreen::None:
        break;
    }
    return "none";
}

std::string battleCommandMapScrName(BattleCommandMapScreen screen) {
    if (screen == BattleCommandMapScreen::MissionStatus) {
        return "STATUS.SCR";
    }
    return "MAP.SCR";
}

ScreenRect battleCommandMapRect(BattleCommandMapScreen screen) {
    if (screen == BattleCommandMapScreen::MissionStatus) {
        return kMissionStatusMapInnerRect;
    }
    return kCockpitCommandMapInnerRect;
}

void applyBattleDriveDefaults(Options& options) {
    if (!battleInteractiveMode(options)) {
        return;
    }
    if (options.mechPreset.empty()) {
        options.mechPreset = "locust";
        options.assembled = true;
    }
    if (!options.terrainScenarioIndex.has_value()) {
        options.terrainScenarioIndex = 2u;
    }
    options.terrainEnabled = true;
    options.terrainWorldEnabled = true;
}

void resolveOriginalBattlefieldSetup(Options& options) {
    if (!options.battleOriginalPlacement) {
        return;
    }
    if (!options.battleSimTicks.has_value() && !battleInteractiveMode(options)) {
        return;
    }
    if (!options.terrainScenarioIndex.has_value()) {
        throw std::runtime_error("--battle-placement original requires --terrain-scenario-index or battle defaults");
    }
    if (options.terrainScenarioPath.empty()) {
        options.terrainScenarioPath = options.terrainRoot / "DAT" / "SNARIO.DAT";
    }
    options.battleSetup = mw::battle::decodeOriginalBattlefieldSetup(
        options.terrainScenarioPath,
        *options.terrainScenarioIndex,
        options.battleMission.originalId,
        static_cast<double>(options.terrainCellSize));
    options.terrainScenarioIndex = options.battleSetup->scenarioIndex;
    options.terrainScenarioMode = static_cast<int>(options.battleSetup->terrainMode);
    options.terrainLayoutNames = options.battleSetup->tileNames;
    options.terrainLayoutPreset.clear();
    options.terrainEnabled = true;
    options.terrainWorldEnabled = true;
    if (!options.terrainLayoutGapExplicit) {
        options.terrainLayoutGap = 0.0f;
    }
    if (!options.terrainObjectInsetExplicit) {
        options.terrainObjectInset = 0.18f;
    }
}

double catalogCockpitCameraHeight(const Options& options) {
    const std::vector<mw::legacy3d::RuntimeRecord> records =
        mw::legacy3d::loadRuntimeShapeRecords(options.resourcePath);
    return static_cast<double>(
        mw::mech3d::catalogCockpitCameraHeight(options.mechPreset, records));
}

mw::battle::BattleStartParams battleStartParamsFromOptions(const Options& options) {
    mw::battle::BattleStartParams params;
    params.mission = options.battleMission;
    params.terrainScenarioPath = options.terrainScenarioPath;
    params.terrainScenarioIndex = *options.terrainScenarioIndex;
    params.terrainEnvironmentId = options.terrainEnvironmentId;
    params.terrainCellSize = static_cast<double>(options.terrainCellSize);
    params.playerMechPresetId = options.mechPreset;
    params.playerRoster.team = mw::battle::BattleTeam::Player;
    params.playerRoster.provenance = options.battleSetup.has_value()
                                         ? "original_battlefield_setup"
                                         : "viewer_launch";
    params.playerRoster.sourceSlot = options.battleSetup.has_value() ? "player:0" : "player:manual";
    params.cockpitCameraHeight = catalogCockpitCameraHeight(options);
    if (options.battleSetup.has_value()) {
        params.playerStartTransform = options.battleSetup->playerStartTransform;
        params.setupMetadata = options.battleSetup->metadata;
        params.battlefieldBoundary = options.battleSetup->battlefieldBoundary;
    }
    if (options.battleCommandMap || options.battleOriginalPlacement) {
        params.objective = battleObjectiveFromOptions(options);
    }
    if (options.battleStartWorld.has_value() && options.battleStartGrid.has_value()) {
        throw std::runtime_error("--battle-start cannot be combined with --battle-start-grid");
    }
    if (options.battleStartWorld.has_value()) {
        params.playerStartTransform.x = options.battleStartWorld->first;
        params.playerStartTransform.z = options.battleStartWorld->second;
    } else if (options.battleStartGrid.has_value()) {
        params.playerStartTransform.x = options.battleStartGrid->first * static_cast<double>(options.terrainCellSize);
        params.playerStartTransform.z = options.battleStartGrid->second * static_cast<double>(options.terrainCellSize);
    }
    if (options.battleStartHeadingRadians.has_value()) {
        params.playerStartTransform.headingRadians = *options.battleStartHeadingRadians;
    } else if (options.battleCommandMap || options.battleOriginalPlacement) {
        const std::pair<double, double> targetWorld =
            params.objective.valid
                ? std::pair<double, double>{params.objective.transform.x, params.objective.transform.z}
                : commandMapTargetWorld(options);
        const std::optional<double> targetHeading = headingRadiansToward(
            params.playerStartTransform.x,
            params.playerStartTransform.z,
            targetWorld.first,
            targetWorld.second);
        if (targetHeading.has_value()) {
            params.playerStartTransform.headingRadians = *targetHeading;
        }
    }
    if (!options.destroyedComponentIds.empty()) {
        mw::battle::BattleCombatantLaunchState launchState;
        launchState.mechPresetId = options.mechPreset;
        launchState.startTransform = params.playerStartTransform;
        launchState.destroyedComponentIds = options.destroyedComponentIds;
        params.playerLaunchState = launchState;
    }
    if (options.battleEnemyStartWorld.has_value() && options.battleEnemyStartGrid.has_value()) {
        throw std::runtime_error("--battle-enemy-start cannot be combined with --battle-enemy-start-grid");
    }
    if (options.battleCommandMap || options.battleOriginalPlacement) {
        mw::battle::BattleCombatantLaunchState enemyLaunch;
        enemyLaunch.mechPresetId = "locust";
        enemyLaunch.roster.team = mw::battle::BattleTeam::Opposing;
        enemyLaunch.roster.provenance = options.battleSetup.has_value()
                                             ? "original_battlefield_setup"
                                             : "viewer_explicit_launch";
        enemyLaunch.roster.sourceSlot = "opposing:0";
        std::pair<double, double> enemyWorld = gridToWorldPair({124.0, 69.0}, options.terrainCellSize);
        if (options.battleSetup.has_value()) {
            enemyWorld = {
                options.battleSetup->enemyStartTransform.x,
                options.battleSetup->enemyStartTransform.z,
            };
        }
        if (options.battleEnemyStartGrid.has_value()) {
            enemyWorld = gridToWorldPair(*options.battleEnemyStartGrid, options.terrainCellSize);
        }
        if (options.battleEnemyStartWorld.has_value()) {
            enemyWorld = *options.battleEnemyStartWorld;
        }
        enemyLaunch.startTransform = options.battleSetup.has_value()
                                         ? options.battleSetup->enemyStartTransform
                                         : mw::battle::Transform{enemyWorld.first, 0.0, enemyWorld.second, 0.0};
        enemyLaunch.startTransform.x = enemyWorld.first;
        enemyLaunch.startTransform.z = enemyWorld.second;
        params.combatantLaunchStates.push_back(enemyLaunch);
    }
    if (battleInteractiveMode(options)) {
        params.fixedTickSeconds = mw::battle::battleDrivePrototypeTuning().fixedTickSeconds;
        params.maxForwardSpeed = options.battleDriveMaxForwardSpeed;
        params.maxReverseSpeed = options.battleDriveMaxReverseSpeed;
        params.acceleration = options.battleDriveAcceleration;
        params.deceleration = options.battleDriveDeceleration;
        params.maxTurnRateRadians = options.battleDriveTurnRateRadians;
    }
    return params;
}

void resolveBattleSimulationSnapshot(Options& options) {
    if (!options.battleSimTicks.has_value() && !battleInteractiveMode(options)) {
        return;
    }
    if (options.mechPreset.empty()) {
        throw std::runtime_error("battle simulation requires --mech-preset");
    }
    if (!options.terrainScenarioIndex.has_value()) {
        throw std::runtime_error("battle simulation requires --terrain-scenario-index");
    }
    if (options.terrainScenarioPath.empty()) {
        options.terrainScenarioPath = options.terrainRoot / "DAT" / "SNARIO.DAT";
    }

    const mw::battle::BattleStartParams params = battleStartParamsFromOptions(options);
    mw::battle::BattleWorld battleWorld = mw::battle::BattleWorld::create(params);
    if (options.battleSimTicks.has_value()) {
        const mw::battle::BattleReplay replay =
            battleInteractiveMode(options) ? viewerDriveBattleReplay() : viewerSmokeBattleReplay();
        for (uint64_t tick = 0; tick < *options.battleSimTicks; ++tick) {
            for (mw::battle::BattleInputCommand command : replay.commands) {
                if (command.tickIndex != tick) {
                    continue;
                }
                if (!mw::battle::isValid(command.entityId)) {
                    command.entityId = battleWorld.playerEntityId();
                }
                battleWorld.enqueueInput(command);
            }
            battleWorld.tick();
        }
        if (battleInteractiveMode(options)) {
            const mw::battle::BattleSnapshot replaySnapshot =
                mw::battle::runBattleReplay(params, replay, *options.battleSimTicks);
            options.battleDriveReplayMatch =
                mw::battle::battleSnapshotFingerprint(replaySnapshot) ==
                mw::battle::battleSnapshotFingerprint(battleWorld.snapshot());
        }
    }
    options.battleWorld = battleWorld;
    options.battleSnapshot = options.battleWorld->snapshot();

    if (!options.battleSnapshot->combatants.empty()) {
        const mw::battle::CombatantSnapshot& combatant = options.battleSnapshot->combatants.front();
        if (!combatant.activeAnimationId.empty()) {
            options.animationName = normalizeId(combatant.activeAnimationId);
            options.animationEnabled = true;
            options.assembled = true;
        }
    }
}

Options parseArgs(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list-mech-presets") {
            options.listMechPresets = true;
        } else if (arg == "--list-battle-missions") {
            options.listBattleMissions = true;
        } else if (arg == "--list-terrain-arenas") {
            options.listTerrainArenas = true;
        } else if (arg == "--list-terrain-layouts") {
            options.listTerrainLayouts = true;
        } else if (arg == "--list-terrain-scenarios") {
            options.listTerrainScenarios = true;
        } else if (arg == "--smoke") {
            options.smoke = true;
        } else if (arg == "--battle-drive") {
            options.battleDrive = true;
            options.battleCockpit = false;
        } else if (arg == "--battle-cockpit") {
            options.battleCockpit = true;
            options.battleDrive = false;
        } else if (arg == "--battle-command-map") {
            options.battleCommandMap = true;
            options.battleCommandMapScreen = BattleCommandMapScreen::MissionStatus;
            options.battleDrive = false;
        } else if (arg == "--battle-mission-id") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-mission-id requires a mission table index");
            }
            const int missionId = parseInt(argv[i]);
            if (missionId < 0) {
                throw std::runtime_error("--battle-mission-id must be non-negative");
            }
            const std::optional<mw::battle::BattleMissionDefinition> mission =
                mw::battle::battleMissionDefinitionById(static_cast<size_t>(missionId));
            if (!mission) {
                throw std::runtime_error("--battle-mission-id is outside the recovered 0..33 mission table");
            }
            options.battleMission = mw::battle::battleMissionBriefingFromDefinition(*mission);
            options.battleMissionSource = "cli_id";
        } else if (arg == "--battle-placement") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-placement requires manual or original");
            }
            const std::string value = argv[i];
            if (value == "manual") {
                options.battleOriginalPlacement = false;
            } else if (value == "original") {
                options.battleOriginalPlacement = true;
            } else {
                throw std::runtime_error("--battle-placement must be manual or original");
            }
        } else if (arg == "--battle-view") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-view requires drive, cockpit, or command-map");
            }
            const std::string mode = normalizeId(argv[i]);
            if (mode == "drive") {
                options.battleDrive = true;
                options.battleCockpit = false;
                options.battleCommandMap = false;
                options.battleCommandMapScreen = BattleCommandMapScreen::None;
            } else if (mode == "cockpit") {
                options.battleCockpit = true;
                options.battleDrive = false;
                options.battleCommandMap = false;
                options.battleCommandMapScreen = BattleCommandMapScreen::None;
            } else if (mode == "command_map" || mode == "command-map") {
                options.battleCommandMap = true;
                options.battleCommandMapScreen = BattleCommandMapScreen::CockpitCommand;
                options.battleDrive = false;
                options.battleCockpit = false;
            } else if (mode == "mission_status" || mode == "mission-status") {
                options.battleCommandMap = true;
                options.battleCommandMapScreen = BattleCommandMapScreen::MissionStatus;
                options.battleDrive = false;
                options.battleCockpit = false;
            } else {
                throw std::runtime_error("--battle-view must be drive, cockpit, command-map, or mission-status");
            }
        } else if (arg == "--battle-cockpit-family") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-cockpit-family requires auto, light, medium, or heavy");
            }
            options.battleCockpitFamily = canonicalCockpitFamily(argv[i]);
            if (options.battleCockpitFamily != "auto" &&
                options.battleCockpitFamily != "light" &&
                options.battleCockpitFamily != "medium" &&
                options.battleCockpitFamily != "heavy") {
                throw std::runtime_error("--battle-cockpit-family must be auto, light, medium, or heavy");
            }
        } else if (arg == "--battle-cockpit-camera") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-cockpit-camera requires cockpit or external");
            }
            const std::string camera = normalizeId(argv[i]);
            if (camera == "cockpit") {
                options.battleCockpitExternalCamera = false;
            } else if (camera == "external") {
                options.battleCockpitExternalCamera = true;
            } else {
                throw std::runtime_error("--battle-cockpit-camera must be cockpit or external");
            }
        } else if (arg == "--battle-cockpit-bob") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-cockpit-bob requires on or off");
            }
            options.battleCockpitBob = parseOnOff(argv[i], "--battle-cockpit-bob");
        } else if (arg == "--resource") {
            if (++i >= argc) {
                throw std::runtime_error("--resource requires a path");
            }
            options.resourcePath = std::filesystem::path(argv[i]);
            options.resourcePathExplicit = true;
        } else if (arg == "--record") {
            if (++i >= argc) {
                throw std::runtime_error("--record requires an index");
            }
            options.recordIndex = parseInt(argv[i]);
            options.recordIndexExplicit = true;
        } else if (arg == "--shade-mode") {
            if (++i >= argc) {
                throw std::runtime_error("--shade-mode requires a value");
            }
            options.shadeMode = argv[i];
            if (options.shadeMode != "stored" && options.shadeMode != "ega" && options.shadeMode != "lit") {
                throw std::runtime_error("unsupported shade mode: " + options.shadeMode);
            }
        } else if (arg == "--wire") {
            options.wire = true;
        } else if (arg == "--ortho") {
            options.perspective = false;
        } else if (arg == "--assembled") {
            options.assembled = true;
        } else if (arg == "--assembly-records") {
            if (++i >= argc) {
                throw std::runtime_error("--assembly-records requires a comma-separated list");
            }
            options.assemblyRecordIndices = parseRecordList(argv[i]);
            options.assembled = true;
        } else if (arg == "--mech-preset") {
            if (++i >= argc) {
                throw std::runtime_error("--mech-preset requires a preset id");
            }
            options.mechPreset = normalizeId(argv[i]);
            options.assembled = true;
        } else if (arg == "--animation") {
            if (++i >= argc) {
                throw std::runtime_error("--animation requires an animation id");
            }
            options.animationName = normalizeId(argv[i]);
            options.animationEnabled = true;
            options.assembled = true;
        } else if (arg == "--animation-records") {
            if (++i >= argc) {
                throw std::runtime_error("--animation-records requires a comma-separated list");
            }
            options.animationRecordIndices = parseRecordList(argv[i]);
            options.animationEnabled = true;
            options.assembled = true;
        } else if (arg == "--animation-component") {
            if (++i >= argc) {
                throw std::runtime_error("--animation-component requires an index");
            }
            options.animationComponentId = parseInt(argv[i]);
            if (options.animationComponentId < 0) {
                throw std::runtime_error("--animation-component must be non-negative");
            }
        } else if (arg == "--animation-ms") {
            if (++i >= argc) {
                throw std::runtime_error("--animation-ms requires a positive duration");
            }
            options.animationFrameMs = parseInt(argv[i]);
            if (options.animationFrameMs <= 0) {
                throw std::runtime_error("--animation-ms must be positive");
            }
        } else if (arg == "--animation-sequence") {
            if (++i >= argc) {
                throw std::runtime_error("--animation-sequence requires component:records[:ms]");
            }
            options.animationSequences.push_back(parseAnimationSequence(argv[i]));
            options.animationEnabled = true;
            options.assembled = true;
        } else if (arg == "--hide-component") {
            if (++i >= argc) {
                throw std::runtime_error("--hide-component requires a component id");
            }
            const int componentId = parseInt(argv[i]);
            if (componentId < 0) {
                throw std::runtime_error("--hide-component must be non-negative");
            }
            options.hiddenComponentIds.push_back(componentId);
            options.assembled = true;
        } else if (arg == "--destroy-component") {
            if (++i >= argc) {
                throw std::runtime_error("--destroy-component requires a component id");
            }
            const int componentId = parseInt(argv[i]);
            if (componentId < 0) {
                throw std::runtime_error("--destroy-component must be non-negative");
            }
            options.destroyedComponentIds.push_back(componentId);
            options.assembled = true;
        } else if (arg == "--terrain-root") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-root requires a path");
            }
            options.terrainRoot = std::filesystem::path(argv[i]);
        } else if (arg == "--terrain-arena") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-arena requires a TILE resource name");
            }
            options.terrainArena = argv[i];
            options.terrainEnabled = true;
            options.terrainWorldEnabled = true;
        } else if (arg == "--terrain-layout-preset") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-layout-preset requires a layout id");
            }
            options.terrainLayoutPreset = normalizeId(argv[i]);
        } else if (arg == "--terrain-layout") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-layout requires four comma-separated TILE resource names");
            }
            options.terrainLayoutNames = parseTerrainNameList(argv[i]);
            if (options.terrainLayoutNames.size() != 4u) {
                throw std::runtime_error("--terrain-layout requires exactly four TILE resource names");
            }
            options.terrainEnabled = true;
            options.terrainWorldEnabled = true;
        } else if (arg == "--terrain-scenario-index") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-scenario-index requires an index");
            }
            const int index = parseInt(argv[i]);
            if (index < 0) {
                throw std::runtime_error("--terrain-scenario-index must be non-negative");
            }
            options.terrainScenarioIndex = static_cast<size_t>(index);
        } else if (arg == "--terrain-scenario-file") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-scenario-file requires a SNARIO.DAT path");
            }
            options.terrainScenarioPath = std::filesystem::path(argv[i]);
        } else if (arg == "--terrain-layout-gap") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-layout-gap requires a number");
            }
            const float requestedGap = parseFloat(argv[i]);
            if (requestedGap < 0.0f) {
                throw std::runtime_error("--terrain-layout-gap must be non-negative");
            }
            options.terrainLayoutGap = 0.0f;
            options.terrainLayoutGapExplicit = false;
        } else if (arg == "--terrain-cell-size") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-cell-size requires a positive number");
            }
            options.terrainCellSize = parseFloat(argv[i]);
            if (options.terrainCellSize <= 0.0f) {
                throw std::runtime_error("--terrain-cell-size must be positive");
            }
        } else if (arg == "--terrain-object-inset") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-object-inset requires a fraction");
            }
            options.terrainObjectInset = parseFloat(argv[i]);
            if (options.terrainObjectInset < 0.0f || options.terrainObjectInset >= 0.45f) {
                throw std::runtime_error("--terrain-object-inset must be in range 0.0..0.45");
            }
            options.terrainObjectInsetExplicit = true;
        } else if (arg == "--terrain-object-scale") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-object-scale requires a positive number");
            }
            options.terrainObjectScale = parseFloat(argv[i]);
            if (options.terrainObjectScale <= 0.0f) {
                throw std::runtime_error("--terrain-object-scale must be positive");
            }
        } else if (arg == "--terrain-tile-mirror-x") {
            options.terrainTileMirrorX = true;
        } else if (arg == "--terrain-no-tile-mirror-y") {
            options.terrainTileMirrorY = false;
        } else if (arg == "--terrain-no-object-mirror-y") {
            options.terrainObjectMirrorY = false;
        } else if (arg == "--terrain-environment-id") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-environment-id requires 0, 1, or 2");
            }
            const int environmentId = parseInt(argv[i]);
            options.terrainEnvironmentId = environmentId;
            if (!options.terrainColorExplicit) {
                options.terrainColorMode = terrainColorModeForEnvironmentId(environmentId);
            } else {
                (void)terrainColorModeForEnvironmentId(environmentId);
            }
        } else if (arg == "--battle-sim-ticks") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-sim-ticks requires a non-negative tick count");
            }
            const int ticks = parseInt(argv[i]);
            if (ticks < 0) {
                throw std::runtime_error("--battle-sim-ticks must be non-negative");
            }
            options.battleSimTicks = static_cast<uint64_t>(ticks);
        } else if (arg == "--battle-drive-scenario-step") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-drive-scenario-step requires an integer step");
            }
            options.battleDriveScenarioStep = parseInt(argv[i]);
        } else if (arg == "--battle-start") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-start requires X,Z");
            }
            options.battleStartWorld = parseFloatPair(argv[i], "--battle-start");
        } else if (arg == "--battle-start-grid") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-start-grid requires X,Y");
            }
            options.battleStartGrid = parseFloatPair(argv[i], "--battle-start-grid");
        } else if (arg == "--battle-enemy-start") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-enemy-start requires X,Z");
            }
            options.battleEnemyStartWorld = parseFloatPair(argv[i], "--battle-enemy-start");
            options.battleCommandMap = true;
            if (!battleCommandMapActive(options)) {
                options.battleCommandMapScreen = BattleCommandMapScreen::MissionStatus;
            }
        } else if (arg == "--battle-enemy-start-grid") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-enemy-start-grid requires X,Y");
            }
            options.battleEnemyStartGrid = parseFloatPair(argv[i], "--battle-enemy-start-grid");
            options.battleCommandMap = true;
            if (!battleCommandMapActive(options)) {
                options.battleCommandMapScreen = BattleCommandMapScreen::MissionStatus;
            }
        } else if (arg == "--battle-command-map-target") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-command-map-target requires X,Z");
            }
            options.battleCommandMapTargetWorld = parseFloatPair(argv[i], "--battle-command-map-target");
            options.battleCommandMap = true;
            if (!battleCommandMapActive(options)) {
                options.battleCommandMapScreen = BattleCommandMapScreen::MissionStatus;
            }
        } else if (arg == "--battle-command-map-target-grid") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-command-map-target-grid requires X,Y");
            }
            options.battleCommandMapTargetGrid = parseFloatPair(argv[i], "--battle-command-map-target-grid");
            options.battleCommandMap = true;
            if (!battleCommandMapActive(options)) {
                options.battleCommandMapScreen = BattleCommandMapScreen::MissionStatus;
            }
        } else if (arg == "--battle-heading-deg") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-heading-deg requires a number");
            }
            constexpr double pi = 3.14159265358979323846;
            options.battleStartHeadingRadians = (180.0 - static_cast<double>(parseFloat(argv[i]))) * pi / 180.0;
        } else if (arg == "--battle-camera-yaw-deg") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-camera-yaw-deg requires a number");
            }
            options.battleCameraYawOffsetDegrees = parseFloat(argv[i]);
        } else if (arg == "--battle-camera-pitch-deg") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-camera-pitch-deg requires a number");
            }
            options.battleCameraPitchDegrees = parseFloat(argv[i]);
            if (options.battleCameraPitchDegrees < 8.0f || options.battleCameraPitchDegrees > 62.0f) {
                throw std::runtime_error("--battle-camera-pitch-deg must be in range 8..62");
            }
        } else if (arg == "--battle-camera-distance") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-camera-distance requires a positive number");
            }
            options.battleCameraDistance = parseFloat(argv[i]);
            if (options.battleCameraDistance < 900.0f || options.battleCameraDistance > 8500.0f) {
                throw std::runtime_error("--battle-camera-distance must be in range 900..8500");
            }
        } else if (arg == "--battle-drive-max-forward-speed") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-drive-max-forward-speed requires a positive number");
            }
            options.battleDriveMaxForwardSpeed = parseFloat(argv[i]);
            if (options.battleDriveMaxForwardSpeed <= 0.0) {
                throw std::runtime_error("--battle-drive-max-forward-speed must be positive");
            }
        } else if (arg == "--battle-drive-max-reverse-speed") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-drive-max-reverse-speed requires a positive number");
            }
            options.battleDriveMaxReverseSpeed = parseFloat(argv[i]);
            if (options.battleDriveMaxReverseSpeed <= 0.0) {
                throw std::runtime_error("--battle-drive-max-reverse-speed must be positive");
            }
        } else if (arg == "--battle-drive-accel") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-drive-accel requires a positive number");
            }
            options.battleDriveAcceleration = parseFloat(argv[i]);
            if (options.battleDriveAcceleration <= 0.0) {
                throw std::runtime_error("--battle-drive-accel must be positive");
            }
        } else if (arg == "--battle-drive-decel") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-drive-decel requires a positive number");
            }
            options.battleDriveDeceleration = parseFloat(argv[i]);
            if (options.battleDriveDeceleration <= 0.0) {
                throw std::runtime_error("--battle-drive-decel must be positive");
            }
        } else if (arg == "--battle-drive-turn-rate-deg-per-sec") {
            if (++i >= argc) {
                throw std::runtime_error("--battle-drive-turn-rate-deg-per-sec requires a positive number");
            }
            constexpr double pi = 3.14159265358979323846;
            const double degreesPerSecond = parseFloat(argv[i]);
            if (degreesPerSecond <= 0.0) {
                throw std::runtime_error("--battle-drive-turn-rate-deg-per-sec must be positive");
            }
            options.battleDriveTurnRateRadians = degreesPerSecond * pi / 180.0;
        } else if (arg == "--terrain-color") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-color requires debug, flat, desert, green, snow, tropical/jungle, or arctic/ice");
            }
            options.terrainColorMode = canonicalTerrainColorMode(argv[i]);
            options.terrainColorExplicit = true;
            (void)terrainPaletteById(options.terrainColorMode);
        } else if (arg == "--terrain-sky") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-sky requires auto, black, blue, or white");
            }
            options.terrainSkyMode = canonicalTerrainSkyMode(argv[i]);
            (void)terrainSkyColor(options.terrainSkyMode, terrainPaletteById(options.terrainColorMode));
        } else if (arg == "--terrain-grid") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-grid requires a path");
            }
            options.terrainGridPath = std::filesystem::path(argv[i]);
            options.terrainEnabled = true;
        } else if (arg == "--terrain-world") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-world requires a path");
            }
            options.terrainWorldPath = std::filesystem::path(argv[i]);
            options.terrainWorldEnabled = true;
            options.terrainEnabled = true;
        } else if (arg == "--terrain-shapes") {
            if (++i >= argc) {
                throw std::runtime_error("--terrain-shapes requires a path");
            }
            options.terrainShapePath = std::filesystem::path(argv[i]);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    applyBattleDriveDefaults(options);
    resolveTerrainArenaPaths(options);
    resolveTerrainScenarioLayout(options);
    applyBattleDriveScenarioStep(options);
    resolveTerrainLayoutPreset(options);
    if (!options.terrainArena.empty() && !options.terrainLayoutNames.empty()) {
        throw std::runtime_error("--terrain-arena cannot be combined with --terrain-layout or --terrain-layout-preset");
    }
    if (options.battleCommandMapTargetWorld.has_value() && options.battleCommandMapTargetGrid.has_value()) {
        throw std::runtime_error("--battle-command-map-target cannot be combined with --battle-command-map-target-grid");
    }
    if (!options.terrainLayoutNames.empty() && options.terrainShapePath.empty()) {
        options.terrainShapePath = options.terrainRoot / "TBL" / "viewer8" / "TERPCK.TBL";
    }
    resolveOriginalBattlefieldSetup(options);
    if (!options.mechPreset.empty()) {
        const mw::mech3d::MechModelDefinition definition =
            mw::mech3d::makeCatalogMechModelDefinition(options.mechPreset);
        if (!options.resourcePathExplicit) {
            options.resourcePath = mw::mech3d::catalogDefaultResourcePath(options.mechPreset);
        }
        if (!options.recordIndexExplicit && !definition.components.empty()) {
            options.recordIndex = definition.components.front().sourceRecordIndex;
        }
        if (options.assemblyRecordIndices.empty()) {
            options.assemblyRecordIndices = mw::mech3d::sourceRecordIndices(definition);
        }
    } else if (!options.animationName.empty()) {
        throw std::runtime_error("--animation requires --mech-preset");
    }
    resolveBattleSimulationSnapshot(options);
    if (!options.destroyedComponentIds.empty() && options.mechPreset.empty()) {
        throw std::runtime_error("--destroy-component requires --mech-preset");
    }
    if (options.terrainWorldEnabled && options.terrainShapePath.empty()) {
        throw std::runtime_error("--terrain-world requires --terrain-shapes");
    }
    if (options.terrainWorldEnabled && options.terrainGridPath.empty() && options.terrainLayoutNames.empty()) {
        throw std::runtime_error("--terrain-world requires --terrain-grid");
    }
    applyResolvedRuntimeStateToOptions(options);
    if (!options.animationName.empty() && (!options.animationSequences.empty() || !options.animationRecordIndices.empty())) {
        throw std::runtime_error("--animation cannot be combined with explicit animation record flags");
    }
    if (options.animationEnabled && options.assemblyRecordIndices.empty()) {
        if (!options.animationSequences.empty()) {
            for (const mw::mech3d::ComponentFrameSequence& sequence : options.animationSequences) {
                options.assemblyRecordIndices.push_back(sequence.sourceRecordIndices.front());
            }
        } else if (!options.animationRecordIndices.empty()) {
            options.assemblyRecordIndices.push_back(options.animationRecordIndices.front());
        }
    }
    return options;
}

const mw::legacy3d::RuntimeRecord& selectRecord(const std::vector<mw::legacy3d::RuntimeRecord>& records, int recordIndex) {
    for (const auto& record : records) {
        if (record.recordIndex == recordIndex) {
            return record;
        }
    }
    throw std::runtime_error("record not found: " + std::to_string(recordIndex));
}

struct LoadedBattleObjectiveModel {
    std::filesystem::path path;
    mw::legacy3d::GpuBatch batch;
};

std::optional<LoadedBattleObjectiveModel> loadBattleObjectiveModelAsset(
    const Options& options,
    const mw::legacy3d::GpuBatchOptions& baseOptions) {
    if (!options.battleSnapshot.has_value() ||
        !options.battleSnapshot->objective.valid ||
        !options.battleSnapshot->objective.staticModel.valid) {
        return std::nullopt;
    }

    const mw::battle::BattleStaticModelRef& model = options.battleSnapshot->objective.staticModel;
    if (model.resourceName.empty() || model.recordIndex < 0 || model.scale <= 0.0) {
        throw std::runtime_error("battle objective static model metadata is invalid");
    }

    LoadedBattleObjectiveModel loaded;
    loaded.path = options.terrainRoot / "TBL" / "viewer8" / model.resourceName;
    const std::vector<mw::legacy3d::RuntimeRecord> records =
        mw::legacy3d::loadRuntimeShapeRecords(loaded.path);
    mw::legacy3d::GpuBatchOptions modelOptions = baseOptions;
    modelOptions.swizzle = model.swizzle;
    modelOptions.scale = static_cast<float>(model.scale);
    loaded.batch = mw::legacy3d::buildGpuBatch(selectRecord(records, model.recordIndex), modelOptions);
    if (loaded.batch.vertices.empty()) {
        throw std::runtime_error("battle objective static model produced no GPU vertices");
    }
    return loaded;
}

Vec3 transformTerrainShapeVertex(mw::legacy3d::Vec3i v, float scale, const std::string& swizzle) {
    const float x = static_cast<float>(v.x) * scale;
    const float y = static_cast<float>(v.y) * scale;
    const float z = static_cast<float>(v.z) * scale;
    if (swizzle == "xyz") {
        return Vec3{x, y, z};
    }
    if (swizzle == "xzy") {
        return Vec3{x, z, y};
    }
    if (swizzle == "yxz") {
        return Vec3{y, x, z};
    }
    if (swizzle == "yzx") {
        return Vec3{y, z, x};
    }
    if (swizzle == "zxy") {
        return Vec3{z, x, y};
    }
    if (swizzle == "zyx") {
        return Vec3{z, y, x};
    }
    throw std::runtime_error("unsupported vertex swizzle: " + swizzle);
}

std::vector<int> terrainShapeValidIndices(
    const mw::legacy3d::RuntimeRecord& record,
    const mw::legacy3d::RuntimePrimitive& prim,
    const std::string& indexMode) {
    std::vector<int> valid;
    valid.reserve(prim.normalizedIndices.size());
    for (uint8_t rawIndex : prim.normalizedIndices) {
        const int idx = indexMode == "minus1" ? static_cast<int>(rawIndex) - 1 : static_cast<int>(rawIndex);
        if (idx >= 0 && static_cast<size_t>(idx) < record.vertices.size()) {
            valid.push_back(idx);
        }
    }
    return valid;
}

mw::mech3d::MechModelDefinition buildCliAssemblyDefinition(const Options& options) {
    if (!options.mechPreset.empty()) {
        return mw::mech3d::makeCatalogMechModelDefinition(options.mechPreset);
    }
    if (options.assemblyRecordIndices.empty()) {
        throw std::runtime_error("assembled mode requires at least one record");
    }
    if (options.assemblyRecordIndices == std::vector<int>{0, 1, 2, 3, 4, 5}) {
        return mw::mech3d::makeJennerBaselineModelDefinition();
    }
    return mw::mech3d::makeRecordListModelDefinition(options.assemblyRecordIndices, "cli_assembly_records");
}

mw::mech3d::ModelAnimationDefinition buildCliAnimationDefinition(const Options& options) {
    if (!options.animationEnabled) {
        return {};
    }
    if (!options.animationName.empty()) {
        if (options.animationName == "idle") {
            return {};
        }
        if (mw::mech3d::catalogAnimationKind(options.mechPreset, options.animationName) ==
            mw::mech3d::MechCatalogAnimationKind::ComponentFrames) {
            return mw::mech3d::makeCatalogAnimationDefinition(options.mechPreset, options.animationName);
        }
        return {};
    }
    if (options.animationRecordIndices.empty() && options.animationSequences.empty()) {
        throw std::runtime_error("animation mode requires at least one source record");
    }

    mw::mech3d::ModelAnimationDefinition animation;
    animation.debugName = "cli_record_frame_animation";
    if (!options.animationSequences.empty()) {
        animation.sequences = options.animationSequences;
    } else {
        animation.sequences.push_back(mw::mech3d::ComponentFrameSequence{
            options.animationComponentId,
            options.animationRecordIndices,
            options.animationFrameMs,
            "cli_component_" + std::to_string(options.animationComponentId) + "_record_frame",
        });
    }
    return animation;
}

mw::mech3d::ModelAssemblyFrameAnimationDefinition buildCliAssemblyFrameAnimationDefinition(const Options& options) {
    if (!options.animationEnabled || options.animationName.empty()) {
        return {};
    }
    if (options.animationName == "idle") {
        return {};
    }
    if (mw::mech3d::catalogAnimationKind(options.mechPreset, options.animationName) ==
        mw::mech3d::MechCatalogAnimationKind::AssemblyFrames) {
        return mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition(options.mechPreset, options.animationName);
    }
    return {};
}

std::vector<int> smokeSourceIndices(const Options& options) {
    if (options.assembled && !options.animationName.empty() && options.animationName != "idle" &&
        mw::mech3d::catalogAnimationKind(options.mechPreset, options.animationName) ==
            mw::mech3d::MechCatalogAnimationKind::AssemblyFrames) {
        const mw::mech3d::ModelAssemblyFrameAnimationDefinition animation =
            mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition(options.mechPreset, options.animationName);
        if (!animation.frames.empty()) {
            return animation.frames.front().sourceRecordIndices;
        }
    }
    return options.assembled ? options.assemblyRecordIndices : std::vector<int>{options.recordIndex};
}

Bounds computeBounds(const mw::legacy3d::GpuBatch& batch) {
    Bounds bounds;
    if (batch.vertices.empty()) {
        return bounds;
    }
    bounds.min = Vec3{batch.vertices[0], batch.vertices[1], batch.vertices[2]};
    bounds.max = bounds.min;
    for (size_t i = 0; i + 5 < batch.vertices.size(); i += 6) {
        const Vec3 p{batch.vertices[i], batch.vertices[i + 1], batch.vertices[i + 2]};
        bounds.min.x = std::min(bounds.min.x, p.x);
        bounds.min.y = std::min(bounds.min.y, p.y);
        bounds.min.z = std::min(bounds.min.z, p.z);
        bounds.max.x = std::max(bounds.max.x, p.x);
        bounds.max.y = std::max(bounds.max.y, p.y);
        bounds.max.z = std::max(bounds.max.z, p.z);
    }
    bounds.center = Vec3{
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

struct TerrainObjectBatches {
    mw::legacy3d::GpuBatch base;
    mw::legacy3d::GpuBatch dither;
};

void appendPlacedTerrainObject(
    mw::legacy3d::GpuBatch& out,
    const mw::legacy3d::GpuBatch& objectBatch,
    const Bounds& groundBounds,
    const mw::legacy3d::TerrainObjectPlacement& placement,
    bool rawPlacement,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY) {
    if (objectBatch.vertices.empty()) {
        return;
    }

    const Bounds localBounds = computeBounds(objectBatch);
    const float localCenterX = localBounds.center.x;
    const float localCenterZ = localBounds.center.z;
    const float placementX = (!rawPlacement && mirrorX) ? (groundBounds.min.x + groundBounds.max.x - placement.x) : placement.x;
    const float placementZ = (!rawPlacement && mirrorY) ? (groundBounds.min.z + groundBounds.max.z - placement.z) : placement.z;
    const float offsetX = placementX - localCenterX;
    const float offsetY = -localBounds.min.y;
    const float offsetZ = placementZ - localCenterZ;
    const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size() / 6u);

    for (size_t i = 0; i + 5 < objectBatch.vertices.size(); i += 6u) {
        const float vx = objectBatch.vertices[i];
        const float vz = mirrorObjectY ? (localCenterZ * 2.0f - objectBatch.vertices[i + 2u]) : objectBatch.vertices[i + 2u];
        out.vertices.push_back(vx + offsetX);
        out.vertices.push_back(objectBatch.vertices[i + 1u] + offsetY);
        out.vertices.push_back(vz + offsetZ);
        out.vertices.push_back(objectBatch.vertices[i + 3u]);
        out.vertices.push_back(objectBatch.vertices[i + 4u]);
        out.vertices.push_back(objectBatch.vertices[i + 5u]);
    }
    for (size_t i = 0; i + 2u < objectBatch.triangleIndices.size(); i += 3u) {
        out.triangleIndices.push_back(baseVertex + objectBatch.triangleIndices[i]);
        out.triangleIndices.push_back(baseVertex + objectBatch.triangleIndices[mirrorObjectY ? (i + 2u) : (i + 1u)]);
        out.triangleIndices.push_back(baseVertex + objectBatch.triangleIndices[mirrorObjectY ? (i + 1u) : (i + 2u)]);
    }
    for (uint32_t index : objectBatch.lineIndices) {
        out.lineIndices.push_back(baseVertex + index);
    }
}

TerrainObjectBatches buildTerrainObjectBatches(
    const mw::legacy3d::TerrainWorld& world,
    const std::vector<mw::legacy3d::RuntimeRecord>& terrainRecords,
    const mw::legacy3d::GpuBatchOptions& batchOptions,
    const Bounds& groundBounds,
    float placementInsetFraction,
    float objectScale,
    float cellSize,
    bool rawPlacement,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY,
    const std::string& terrainColorMode) {
    TerrainObjectBatches out;
    if (world.objects.empty()) {
        return out;
    }

    const mw::legacy3d::TerrainPlacementBounds placementBounds{
        groundBounds.min.x,
        groundBounds.max.x,
        groundBounds.min.z,
        groundBounds.max.z,
    };
    const std::vector<mw::legacy3d::TerrainObjectPlacement> placements = rawPlacement
        ? mw::legacy3d::mapTerrainWorldPlacementsFromRawCoordinates(world, placementBounds, cellSize)
        : mw::legacy3d::normalizeTerrainWorldPlacements(world, placementBounds, placementInsetFraction);

    for (const mw::legacy3d::TerrainObjectPlacement& placement : placements) {
        const mw::legacy3d::RuntimeRecord& record = selectRecord(terrainRecords, placement.recordIndex);
        mw::legacy3d::GpuBatchOptions objectOptions = batchOptions;
        objectOptions.scale *= objectScale;
        const mw::legacy3d::GpuBatch objectBatch =
            (terrainColorMode == "debug" || terrainColorMode == "flat")
                ? mw::legacy3d::buildGpuBatch(record, objectOptions)
                : mw::legacy3d::buildOriginalTerrainPaletteGpuBatch(
                      record,
                      objectOptions,
                      terrainColorMode,
                      false);
        appendPlacedTerrainObject(out.base, objectBatch, groundBounds, placement, rawPlacement, mirrorX, mirrorY, mirrorObjectY);

        if (terrainColorMode != "debug" && terrainColorMode != "flat") {
            mw::legacy3d::GpuBatchOptions ditherOptions = objectOptions;
            ditherOptions.showEdges = false;
            ditherOptions.showLines = false;
            appendPlacedTerrainObject(
                out.dither,
                mw::legacy3d::buildOriginalTerrainPaletteGpuBatch(
                    record,
                    ditherOptions,
                    terrainColorMode,
                    true),
                groundBounds,
                placement,
                rawPlacement,
                mirrorX,
                mirrorY,
                mirrorObjectY);
        }
    }

    return out;
}

mw::legacy3d::GpuBatch buildTerrainObjectBatch(
    const mw::legacy3d::TerrainWorld& world,
    const std::vector<mw::legacy3d::RuntimeRecord>& terrainRecords,
    const mw::legacy3d::GpuBatchOptions& batchOptions,
    const Bounds& groundBounds,
    float placementInsetFraction,
    float objectScale,
    float cellSize,
    bool rawPlacement,
    bool mirrorX,
    bool mirrorY,
    bool mirrorObjectY,
    const std::string& terrainColorMode) {
    return buildTerrainObjectBatches(
               world,
               terrainRecords,
               batchOptions,
               groundBounds,
               placementInsetFraction,
               objectScale,
               cellSize,
               rawPlacement,
               mirrorX,
               mirrorY,
               mirrorObjectY,
               terrainColorMode)
        .base;
}

Bounds toViewerBounds(const mw::mech3d::Bounds& source) {
    Bounds bounds;
    bounds.min = Vec3{source.min.x, source.min.y, source.min.z};
    bounds.max = Vec3{source.max.x, source.max.y, source.max.z};
    bounds.center = Vec3{source.center.x, source.center.y, source.center.z};
    bounds.radius = source.radius;
    return bounds;
}

Bounds terrainMeshBounds(const mw::legacy3d::TerrainMesh& mesh) {
    Bounds bounds;
    bounds.min = Vec3{mesh.boundsMin[0], mesh.boundsMin[1], mesh.boundsMin[2]};
    bounds.max = Vec3{mesh.boundsMax[0], mesh.boundsMax[1], mesh.boundsMax[2]};
    bounds.center = Vec3{
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f,
    };
    const float dx = bounds.max.x - bounds.center.x;
    const float dy = bounds.max.y - bounds.center.y;
    const float dz = bounds.max.z - bounds.center.z;
    bounds.radius = std::max(1.0f, std::sqrt(dx * dx + dy * dy + dz * dz));
    return bounds;
}

Bounds translatedBounds(Bounds bounds, Vec3 offset) {
    bounds.min.x += offset.x;
    bounds.min.y += offset.y;
    bounds.min.z += offset.z;
    bounds.max.x += offset.x;
    bounds.max.y += offset.y;
    bounds.max.z += offset.z;
    bounds.center.x += offset.x;
    bounds.center.y += offset.y;
    bounds.center.z += offset.z;
    return bounds;
}

struct LoadedBattleCombatantVisual {
    mw::battle::EntityId entityId{};
    std::string mechPresetId;
    std::filesystem::path resourcePath;
    mw::mech3d::MechRenderInstance renderInstance;
    Bounds placementBounds{};
};

int combatantAnimationElapsedMs(const mw::battle::CombatantSnapshot& combatant) {
    return combatant.walkAnimationElapsedMs > static_cast<uint64_t>(std::numeric_limits<int>::max())
               ? std::numeric_limits<int>::max()
               : static_cast<int>(combatant.walkAnimationElapsedMs);
}

LoadedBattleCombatantVisual loadBattleCombatantVisual(
    const mw::battle::CombatantSnapshot& combatant,
    const mw::legacy3d::GpuBatchOptions& batchOptions) {
    LoadedBattleCombatantVisual loaded;
    loaded.entityId = combatant.id;
    loaded.mechPresetId = combatant.mechPresetId;
    loaded.resourcePath = mw::mech3d::catalogDefaultResourcePath(combatant.mechPresetId);

    const std::vector<mw::legacy3d::RuntimeRecord> records =
        mw::legacy3d::loadRuntimeShapeRecords(loaded.resourcePath);
    const mw::mech3d::MechModelDefinition bindDefinition =
        mw::mech3d::makeCatalogMechModelDefinition(combatant.mechPresetId);
    mw::mech3d::MechModelDefinition frameDefinition = bindDefinition;
    const std::string animationId = normalizeId(combatant.activeAnimationId);
    bool assemblyFrameAnimation = false;
    if (!animationId.empty() && animationId != "idle") {
        const mw::mech3d::MechCatalogAnimationKind kind =
            mw::mech3d::catalogAnimationKind(combatant.mechPresetId, animationId);
        if (kind == mw::mech3d::MechCatalogAnimationKind::ComponentFrames) {
            const mw::mech3d::ModelAnimationDefinition animation =
                mw::mech3d::makeCatalogAnimationDefinition(combatant.mechPresetId, animationId);
            int frame = mw::mech3d::animationFrameForElapsedMs(animation, combatantAnimationElapsedMs(combatant));
            if (animationId == "walk" && combatant.forwardSpeed < -0.0001) {
                frame = mw::mech3d::animationFrameCount(animation) - 1 - frame;
            }
            frameDefinition = mw::mech3d::applyAnimationFrame(bindDefinition, animation, frame);
        } else if (kind == mw::mech3d::MechCatalogAnimationKind::AssemblyFrames) {
            const mw::mech3d::ModelAssemblyFrameAnimationDefinition animation =
                mw::mech3d::makeCatalogAssemblyFrameAnimationDefinition(combatant.mechPresetId, animationId);
            const int frame = mw::mech3d::assemblyFrameAnimationFrameForElapsedMs(
                animation,
                combatantAnimationElapsedMs(combatant));
            frameDefinition = mw::mech3d::applyAssemblyFrameAnimationFrame(animation, frame);
            assemblyFrameAnimation = true;
        }
    }

    loaded.renderInstance = mw::mech3d::buildMechRenderInstance(frameDefinition, records, batchOptions);
    if (!combatant.mechDestroyed && !assemblyFrameAnimation && std::abs(combatant.torsoYawRadians) > 0.0001) {
        mw::mech3d::MechPose pose;
        mw::mech3d::applyPlayerTorsoYawPose(
            pose,
            loaded.renderInstance.assembly,
            static_cast<float>(combatant.torsoYawRadians));
        mw::mech3d::updateMechRenderInstancePose(loaded.renderInstance, pose);
    }

    const mw::mech3d::MechRenderInstance placementInstance =
        mw::mech3d::buildMechRenderInstance(bindDefinition, records, batchOptions);
    loaded.placementBounds = toViewerBounds(placementInstance.assembly.bounds);
    return loaded;
}

std::vector<LoadedBattleCombatantVisual> buildBattleNonPlayerCombatantVisuals(
    const Options& options,
    const mw::legacy3d::GpuBatchOptions& batchOptions) {
    std::vector<LoadedBattleCombatantVisual> visuals;
    if (!options.battleSnapshot.has_value()) {
        return visuals;
    }
    for (const mw::battle::CombatantSnapshot& combatant : options.battleSnapshot->combatants) {
        if (!combatant.playerControlled) {
            visuals.push_back(loadBattleCombatantVisual(combatant, batchOptions));
        }
    }
    return visuals;
}

Bounds insetGroundBounds(Bounds bounds, float insetX, float insetZ) {
    const float width = std::max(0.0f, bounds.max.x - bounds.min.x);
    const float depth = std::max(0.0f, bounds.max.z - bounds.min.z);
    const float clampedX = std::min(std::max(0.0f, insetX), width * 0.45f);
    const float clampedZ = std::min(std::max(0.0f, insetZ), depth * 0.45f);
    bounds.min.x += clampedX;
    bounds.max.x -= clampedX;
    bounds.min.z += clampedZ;
    bounds.max.z -= clampedZ;
    bounds.center = Vec3{
        (bounds.min.x + bounds.max.x) * 0.5f,
        (bounds.min.y + bounds.max.y) * 0.5f,
        (bounds.min.z + bounds.max.z) * 0.5f,
    };
    const float dx = bounds.max.x - bounds.center.x;
    const float dy = bounds.max.y - bounds.center.y;
    const float dz = bounds.max.z - bounds.center.z;
    bounds.radius = std::max(1.0f, std::sqrt(dx * dx + dy * dy + dz * dz));
    return bounds;
}

Bounds mergeBounds(Bounds a, Bounds b) {
    Bounds out;
    out.min = Vec3{
        std::min(a.min.x, b.min.x),
        std::min(a.min.y, b.min.y),
        std::min(a.min.z, b.min.z),
    };
    out.max = Vec3{
        std::max(a.max.x, b.max.x),
        std::max(a.max.y, b.max.y),
        std::max(a.max.z, b.max.z),
    };
    out.center = Vec3{
        (out.min.x + out.max.x) * 0.5f,
        (out.min.y + out.max.y) * 0.5f,
        (out.min.z + out.max.z) * 0.5f,
    };

    float radius = 1.0f;
    const std::array<Vec3, 8> corners{{
        {out.min.x, out.min.y, out.min.z},
        {out.max.x, out.min.y, out.min.z},
        {out.min.x, out.max.y, out.min.z},
        {out.max.x, out.max.y, out.min.z},
        {out.min.x, out.min.y, out.max.z},
        {out.max.x, out.min.y, out.max.z},
        {out.min.x, out.max.y, out.max.z},
        {out.max.x, out.max.y, out.max.z},
    }};
    for (Vec3 p : corners) {
        const float dx = p.x - out.center.x;
        const float dy = p.y - out.center.y;
        const float dz = p.z - out.center.z;
        radius = std::max(radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    out.radius = radius;
    return out;
}

void mirrorTerrainMeshX(mw::legacy3d::TerrainMesh& mesh) {
    const float minX = mesh.boundsMin[0];
    const float maxX = mesh.boundsMax[0];
    for (mw::legacy3d::TerrainMeshVertex& vertex : mesh.vertices) {
        vertex.x = minX + maxX - vertex.x;
    }
}

void mirrorTerrainMeshY(mw::legacy3d::TerrainMesh& mesh) {
    const float minZ = mesh.boundsMin[2];
    const float maxZ = mesh.boundsMax[2];
    for (mw::legacy3d::TerrainMeshVertex& vertex : mesh.vertices) {
        vertex.z = minZ + maxZ - vertex.z;
    }
}

void appendTerrainMesh(
    mw::legacy3d::TerrainMesh& out,
    mw::legacy3d::TerrainMesh source,
    float offsetX,
    float offsetZ,
    bool mirrorX,
    bool mirrorY) {
    const bool wasEmpty = out.vertices.empty();
    const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size());
    if (mirrorX) {
        mirrorTerrainMeshX(source);
    }
    if (mirrorY) {
        mirrorTerrainMeshY(source);
    }
    for (mw::legacy3d::TerrainMeshVertex vertex : source.vertices) {
        vertex.x += offsetX;
        vertex.z += offsetZ;
        out.vertices.push_back(vertex);
    }
    for (uint32_t index : source.indices) {
        out.indices.push_back(baseVertex + index);
    }

    const std::array<float, 3> sourceMin{
        source.boundsMin[0] + offsetX,
        source.boundsMin[1],
        source.boundsMin[2] + offsetZ,
    };
    const std::array<float, 3> sourceMax{
        source.boundsMax[0] + offsetX,
        source.boundsMax[1],
        source.boundsMax[2] + offsetZ,
    };
    if (wasEmpty) {
        out.boundsMin = sourceMin;
        out.boundsMax = sourceMax;
        return;
    }
    for (size_t i = 0; i < 3u; ++i) {
        out.boundsMin[i] = std::min(out.boundsMin[i], sourceMin[i]);
        out.boundsMax[i] = std::max(out.boundsMax[i], sourceMax[i]);
    }
}

void appendGpuBatch(mw::legacy3d::GpuBatch& out, const mw::legacy3d::GpuBatch& source) {
    const uint32_t baseVertex = static_cast<uint32_t>(out.vertices.size() / 6u);
    out.vertices.insert(out.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (uint32_t index : source.triangleIndices) {
        out.triangleIndices.push_back(baseVertex + index);
    }
    for (uint32_t index : source.lineIndices) {
        out.lineIndices.push_back(baseVertex + index);
    }
    const int baseGroup = out.groupMatrices.empty() ? 0 : (out.groupMatrices.rbegin()->first + 1);
    for (const auto& item : source.groupMatrices) {
        out.groupMatrices.emplace(baseGroup + item.first, item.second);
    }
}

FARPROC loadGlProc(const char* name) {
    FARPROC proc = wglGetProcAddress(name);
    if (proc == nullptr || proc == reinterpret_cast<FARPROC>(1) || proc == reinterpret_cast<FARPROC>(2) ||
        proc == reinterpret_cast<FARPROC>(3) || proc == reinterpret_cast<FARPROC>(-1)) {
        HMODULE module = GetModuleHandleW(L"opengl32.dll");
        proc = module ? GetProcAddress(module, name) : nullptr;
    }
    if (!proc) {
        throw std::runtime_error(std::string("OpenGL function unavailable: ") + name);
    }
    return proc;
}

GlFunctions loadGlFunctions() {
    GlFunctions gl;
    gl.glGenBuffers = reinterpret_cast<GlGenBuffers>(loadGlProc("glGenBuffers"));
    gl.glBindBuffer = reinterpret_cast<GlBindBuffer>(loadGlProc("glBindBuffer"));
    gl.glBufferData = reinterpret_cast<GlBufferData>(loadGlProc("glBufferData"));
    gl.glDeleteBuffers = reinterpret_cast<GlDeleteBuffers>(loadGlProc("glDeleteBuffers"));
    return gl;
}

void checkGl(const char* where) {
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::ostringstream oss;
        oss << where << " OpenGL error 0x" << std::hex << err;
        throw std::runtime_error(oss.str());
    }
}

std::string safeStem(const std::filesystem::path& path) {
    std::string stem = path.stem().string();
    for (char& ch : stem) {
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
        if (!ok) {
            ch = '_';
        }
    }
    return stem.empty() ? "resource" : stem;
}

void writeBmp24(const std::filesystem::path& path, int width, int height, const std::vector<uint8_t>& rgbBottomUp) {
    const int rowBytes = width * 3;
    const int paddedRowBytes = (rowBytes + 3) & ~3;
    const uint32_t pixelBytes = static_cast<uint32_t>(paddedRowBytes * height);
    const uint32_t fileSize = 14u + 40u + pixelBytes;

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("could not write screenshot: " + path.string());
    }

    const uint8_t fileHeader[14] = {
        'B',
        'M',
        static_cast<uint8_t>(fileSize & 0xffu),
        static_cast<uint8_t>((fileSize >> 8) & 0xffu),
        static_cast<uint8_t>((fileSize >> 16) & 0xffu),
        static_cast<uint8_t>((fileSize >> 24) & 0xffu),
        0,
        0,
        0,
        0,
        54,
        0,
        0,
        0,
    };
    out.write(reinterpret_cast<const char*>(fileHeader), sizeof(fileHeader));

    auto writeU32 = [&out](uint32_t value) {
        const uint8_t bytes[4] = {
            static_cast<uint8_t>(value & 0xffu),
            static_cast<uint8_t>((value >> 8) & 0xffu),
            static_cast<uint8_t>((value >> 16) & 0xffu),
            static_cast<uint8_t>((value >> 24) & 0xffu),
        };
        out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    };
    auto writeI32 = [&writeU32](int32_t value) {
        writeU32(static_cast<uint32_t>(value));
    };
    auto writeU16 = [&out](uint16_t value) {
        const uint8_t bytes[2] = {
            static_cast<uint8_t>(value & 0xffu),
            static_cast<uint8_t>((value >> 8) & 0xffu),
        };
        out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    };

    writeU32(40);
    writeI32(width);
    writeI32(height);
    writeU16(1);
    writeU16(24);
    writeU32(0);
    writeU32(pixelBytes);
    writeI32(2835);
    writeI32(2835);
    writeU32(0);
    writeU32(0);

    std::vector<uint8_t> row(static_cast<size_t>(paddedRowBytes), 0);
    for (int y = 0; y < height; ++y) {
        const size_t srcRow = static_cast<size_t>(y * rowBytes);
        for (int x = 0; x < width; ++x) {
            const size_t src = srcRow + static_cast<size_t>(x * 3);
            const size_t dst = static_cast<size_t>(x * 3);
            row[dst + 0] = rgbBottomUp[src + 2];
            row[dst + 1] = rgbBottomUp[src + 1];
            row[dst + 2] = rgbBottomUp[src + 0];
        }
        out.write(reinterpret_cast<const char*>(row.data()), row.size());
    }
}

uint32_t readU32Le(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4u > data.size()) {
        throw std::runtime_error("unexpected end of tagged resource");
    }
    return static_cast<uint32_t>(data[offset]) |
        (static_cast<uint32_t>(data[offset + 1u]) << 8u) |
        (static_cast<uint32_t>(data[offset + 2u]) << 16u) |
        (static_cast<uint32_t>(data[offset + 3u]) << 24u);
}

bool isKnownTaggedResource(const std::vector<uint8_t>& data, size_t offset) {
    if (offset + 4u > data.size()) {
        return false;
    }
    const std::string tag(reinterpret_cast<const char*>(data.data() + offset), 4u);
    return tag == "SCR:" || tag == "BIN:" || tag == "INF:" || tag == "BMP:" ||
        tag == "PAL:" || tag == "FNT:" || tag == "EGA:" || tag == "CGA:" ||
        tag == "IBM:" || tag == "SND:";
}

bool findTaggedPayload(
    const std::vector<uint8_t>& data,
    const std::string& wantedTag,
    size_t start,
    size_t limit,
    size_t& payloadOffset,
    size_t& payloadSize) {
    size_t offset = start;
    while (offset + 8u <= limit && isKnownTaggedResource(data, offset)) {
        const std::string tag(reinterpret_cast<const char*>(data.data() + offset), 4u);
        const uint32_t rawLength = readU32Le(data, offset + 4u);
        const size_t size = static_cast<size_t>(rawLength & 0x7fffffffu);
        const bool nested = (rawLength & 0x80000000u) != 0u;
        const size_t currentPayloadOffset = offset + 8u;
        const size_t endOffset = currentPayloadOffset + size;
        if (endOffset > limit || endOffset > data.size()) {
            throw std::runtime_error("tagged resource payload exceeds file size");
        }
        if (tag == wantedTag) {
            payloadOffset = currentPayloadOffset;
            payloadSize = size;
            return true;
        }
        if (nested && findTaggedPayload(data, wantedTag, currentPayloadOffset, endOffset, payloadOffset, payloadSize)) {
            return true;
        }
        offset = endOffset;
    }
    return false;
}

std::array<std::array<uint8_t, 3>, 16> defaultEgaPalette() {
    return {{
        {{0x00, 0x00, 0x00}},
        {{0x00, 0x00, 0xaa}},
        {{0x00, 0xaa, 0x00}},
        {{0x00, 0xaa, 0xaa}},
        {{0xaa, 0x00, 0x00}},
        {{0xaa, 0x00, 0xaa}},
        {{0xaa, 0x55, 0x00}},
        {{0xaa, 0xaa, 0xaa}},
        {{0x55, 0x55, 0x55}},
        {{0x55, 0x55, 0xff}},
        {{0x55, 0xff, 0x55}},
        {{0x55, 0xff, 0xff}},
        {{0xff, 0x55, 0x55}},
        {{0xff, 0x55, 0xff}},
        {{0xff, 0xff, 0x55}},
        {{0xff, 0xff, 0xff}},
    }};
}

std::array<std::array<uint8_t, 3>, 16> loadPalEgaBank0LowPalette(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not open cockpit PAL: " + path.string());
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t payloadOffset = 0;
    size_t payloadSize = 0;
    if (!findTaggedPayload(data, "EGA:", 0u, data.size(), payloadOffset, payloadSize)) {
        throw std::runtime_error("cockpit PAL has no EGA chunk: " + path.string());
    }
    if (payloadSize != 128u) {
        throw std::runtime_error("cockpit PAL EGA chunk is not 128 bytes: " + path.string());
    }

    const std::array<std::array<uint8_t, 3>, 16> ega = defaultEgaPalette();
    std::array<std::array<uint8_t, 3>, 16> palette{};
    for (size_t colorIndex = 0; colorIndex < palette.size(); ++colorIndex) {
        const uint8_t raw = data[payloadOffset + colorIndex * 2u];
        palette[colorIndex] = ega[raw & 0x0fu];
    }
    return palette;
}

class LsbBitReader {
public:
    explicit LsbBitReader(const std::vector<uint8_t>& data) : data_(data) {}

    std::optional<int> read(int width) {
        if (bitIndex_ + static_cast<size_t>(width) > data_.size() * 8u) {
            return std::nullopt;
        }
        int value = 0;
        for (int index = 0; index < width; ++index) {
            const uint8_t byte = data_[bitIndex_ / 8u];
            const size_t bitOffset = bitIndex_ % 8u;
            value |= static_cast<int>((byte >> bitOffset) & 1u) << index;
            ++bitIndex_;
        }
        return value;
    }

    size_t consumedBytes() const {
        return (bitIndex_ + 7u) / 8u;
    }

private:
    const std::vector<uint8_t>& data_;
    size_t bitIndex_ = 0;
};

std::vector<uint8_t> decompressBinCodec1(const std::vector<uint8_t>& body, size_t expectedSize) {
    std::vector<uint8_t> output;
    output.reserve(expectedSize);
    size_t index = 0;
    while (index < body.size()) {
        const uint8_t command = body[index++];
        if ((command & 0x80u) != 0u) {
            const size_t count = command & 0x7fu;
            if (index >= body.size()) {
                throw std::runtime_error("SCR codec 1 run is missing value byte");
            }
            output.insert(output.end(), count, body[index++]);
        } else {
            const size_t count = command;
            if (index + count > body.size()) {
                throw std::runtime_error("SCR codec 1 literal exceeds input");
            }
            output.insert(output.end(), body.begin() + static_cast<std::ptrdiff_t>(index), body.begin() + static_cast<std::ptrdiff_t>(index + count));
            index += count;
        }
    }
    if (output.size() != expectedSize) {
        throw std::runtime_error("SCR codec 1 decoded size mismatch");
    }
    return output;
}

std::vector<uint8_t> decompressBinCodec2(const std::vector<uint8_t>& body, size_t expectedSize) {
    LsbBitReader reader(body);
    int width = 9;
    constexpr int maxWidth = 12;
    int nextCode = 257;
    std::map<int, std::vector<uint8_t>> dictionary;
    for (int index = 0; index < 256; ++index) {
        dictionary.emplace(index, std::vector<uint8_t>{static_cast<uint8_t>(index)});
    }

    std::vector<uint8_t> output;
    output.reserve(expectedSize);
    std::vector<uint8_t> previous;
    bool hasPrevious = false;
    while (true) {
        const std::optional<int> maybeCode = reader.read(width);
        if (!maybeCode.has_value()) {
            break;
        }
        const int code = *maybeCode;
        std::vector<uint8_t> current;
        const auto found = dictionary.find(code);
        if (found != dictionary.end()) {
            current = found->second;
        } else if (hasPrevious && code == nextCode) {
            current = previous;
            current.push_back(previous.front());
        } else {
            throw std::runtime_error("SCR codec 2 bad LZW code");
        }

        output.insert(output.end(), current.begin(), current.end());
        if (hasPrevious) {
            std::vector<uint8_t> entry = previous;
            entry.push_back(current.front());
            dictionary.emplace(nextCode, std::move(entry));
            ++nextCode;
            if (nextCode >= (1 << width) && width < maxWidth) {
                ++width;
            }
        }
        previous = std::move(current);
        hasPrevious = true;
    }
    if (reader.consumedBytes() != body.size()) {
        throw std::runtime_error("SCR codec 2 did not consume the whole body");
    }
    if (output.size() != expectedSize) {
        throw std::runtime_error("SCR codec 2 decoded size mismatch");
    }
    return output;
}

std::vector<uint8_t> decodeBinPayload(const std::vector<uint8_t>& payload, int& codec) {
    if (payload.size() < 5u) {
        throw std::runtime_error("SCR BIN payload is too small");
    }
    codec = payload[0];
    const size_t decodedSize = static_cast<size_t>(readU32Le(payload, 1u));
    std::vector<uint8_t> body(payload.begin() + 5, payload.end());
    if (codec == 1) {
        return decompressBinCodec1(body, decodedSize);
    }
    if (codec == 2) {
        return decompressBinCodec2(body, decodedSize);
    }
    throw std::runtime_error("unsupported SCR BIN codec");
}

std::pair<std::vector<int>, std::vector<int>> decodeBmpInfDimensions(const std::vector<uint8_t>& payload) {
    if (payload.size() < 6u || (payload.size() % 2u) != 0u) {
        throw std::runtime_error("BMP INF payload has invalid size");
    }
    const auto readU16 = [&payload](size_t offset) {
        if (offset + 2u > payload.size()) {
            throw std::runtime_error("BMP INF read past end");
        }
        return static_cast<int>(payload[offset]) | (static_cast<int>(payload[offset + 1u]) << 8);
    };
    const int count = readU16(0u);
    if (count <= 0 || payload.size() != 2u + static_cast<size_t>(count) * 4u) {
        throw std::runtime_error("BMP INF dimensions do not match record count");
    }
    std::vector<int> widths;
    std::vector<int> heights;
    widths.reserve(static_cast<size_t>(count));
    heights.reserve(static_cast<size_t>(count));
    for (int index = 0; index < count; ++index) {
        const int width = readU16(2u + static_cast<size_t>(index) * 2u);
        const int height = readU16(2u + static_cast<size_t>(count + index) * 2u);
        if (width <= 0 || width > 640 || height <= 0 || height > 400) {
            throw std::runtime_error("BMP INF contains invalid dimensions");
        }
        widths.push_back(width);
        heights.push_back(height);
    }
    return {widths, heights};
}

std::vector<CockpitBmpSprite> loadCockpitBmpSprites(
    const std::filesystem::path& path,
    const std::filesystem::path& palettePath,
    uint8_t transparentColorIndex) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not open cockpit BMP: " + path.string());
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t infOffset = 0;
    size_t infSize = 0;
    if (!findTaggedPayload(data, "INF:", 0u, data.size(), infOffset, infSize)) {
        throw std::runtime_error("cockpit BMP has no INF chunk: " + path.string());
    }
    std::vector<uint8_t> infPayload(data.begin() + static_cast<std::ptrdiff_t>(infOffset), data.begin() + static_cast<std::ptrdiff_t>(infOffset + infSize));
    const auto [widths, heights] = decodeBmpInfDimensions(infPayload);

    size_t binOffset = 0;
    size_t binSize = 0;
    if (!findTaggedPayload(data, "BIN:", 0u, data.size(), binOffset, binSize)) {
        throw std::runtime_error("cockpit BMP has no BIN chunk: " + path.string());
    }
    std::vector<uint8_t> binPayload(data.begin() + static_cast<std::ptrdiff_t>(binOffset), data.begin() + static_cast<std::ptrdiff_t>(binOffset + binSize));
    int codec = 0;
    const std::vector<uint8_t> decoded = decodeBinPayload(binPayload, codec);
    (void)codec;

    const std::array<std::array<uint8_t, 3>, 16> palette = loadPalEgaBank0LowPalette(palettePath);
    std::vector<CockpitBmpSprite> sprites;
    sprites.reserve(widths.size());
    size_t srcOffset = 0;
    for (size_t spriteIndex = 0; spriteIndex < widths.size(); ++spriteIndex) {
        const int width = widths[spriteIndex];
        const int height = heights[spriteIndex];
        const size_t rowStride = static_cast<size_t>((width + 1) / 2);
        const size_t recordSize = rowStride * static_cast<size_t>(height);
        if (srcOffset + recordSize > decoded.size()) {
            throw std::runtime_error("cockpit BMP decoded data is shorter than INF dimensions require");
        }

        CockpitBmpSprite sprite;
        sprite.index = static_cast<int>(spriteIndex);
        sprite.width = width;
        sprite.height = height;
        sprite.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
        size_t pixelIndex = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const uint8_t packed = decoded[srcOffset + static_cast<size_t>(y) * rowStride + static_cast<size_t>(x / 2)];
                const uint8_t colorIndex = (x % 2 == 0) ? ((packed >> 4u) & 0x0fu) : (packed & 0x0fu);
                const std::array<uint8_t, 3>& rgb = palette[colorIndex];
                const size_t dst = pixelIndex * 4u;
                sprite.rgba[dst + 0u] = rgb[0];
                sprite.rgba[dst + 1u] = rgb[1];
                sprite.rgba[dst + 2u] = rgb[2];
                sprite.rgba[dst + 3u] = colorIndex == transparentColorIndex ? 0x00u : 0xffu;
                ++pixelIndex;
            }
        }
        sprites.push_back(std::move(sprite));
        srcOffset += recordSize;
    }
    if (srcOffset != decoded.size()) {
        throw std::runtime_error("cockpit BMP decoded data has trailing bytes");
    }
    return sprites;
}

CockpitFont loadCockpitFont(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not open cockpit FNT: " + path.string());
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<uint8_t> payload;
    size_t payloadOffset = 0;
    size_t payloadSize = 0;
    if (data.size() >= 8u && isKnownTaggedResource(data, 0u) &&
        findTaggedPayload(data, "FNT:", 0u, data.size(), payloadOffset, payloadSize)) {
        payload.assign(data.begin() + static_cast<std::ptrdiff_t>(payloadOffset), data.begin() + static_cast<std::ptrdiff_t>(payloadOffset + payloadSize));
    } else {
        payload = std::move(data);
    }
    if (payload.size() < 4u) {
        throw std::runtime_error("cockpit FNT payload is too small: " + path.string());
    }

    CockpitFont font;
    font.width = payload[0];
    font.height = payload[1];
    font.firstCode = payload[2];
    font.glyphCount = payload[3];
    const size_t expectedSize = 4u + static_cast<size_t>(font.glyphCount) * static_cast<size_t>(font.height);
    if (font.width <= 0 || font.width > 8 || font.height <= 0 || font.height > 16 ||
        font.glyphCount <= 0 || expectedSize != payload.size()) {
        throw std::runtime_error("cockpit FNT payload has unsupported layout: " + path.string());
    }
    font.rows.assign(payload.begin() + 4, payload.end());
    return font;
}

CockpitScrImage loadCockpitScrImage(const std::filesystem::path& path, const std::filesystem::path& palettePath) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("could not open cockpit SCR: " + path.string());
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    size_t payloadOffset = 0;
    size_t payloadSize = 0;
    if (!findTaggedPayload(data, "BIN:", 0u, data.size(), payloadOffset, payloadSize)) {
        throw std::runtime_error("cockpit SCR has no BIN chunk: " + path.string());
    }

    std::vector<uint8_t> payload(data.begin() + static_cast<std::ptrdiff_t>(payloadOffset), data.begin() + static_cast<std::ptrdiff_t>(payloadOffset + payloadSize));
    int codec = 0;
    const std::vector<uint8_t> decoded = decodeBinPayload(payload, codec);

    constexpr int width = 320;
    constexpr int height = 200;
    constexpr size_t packedSize = static_cast<size_t>((width + 1) / 2) * static_cast<size_t>(height);
    if (decoded.size() != packedSize) {
        throw std::runtime_error("cockpit SCR decoded layout is not packed 320x200 4bpp: " + path.string());
    }

    const std::array<std::array<uint8_t, 3>, 16> palette = loadPalEgaBank0LowPalette(palettePath);

    CockpitScrImage image;
    image.width = width;
    image.height = height;
    image.codec = codec;
    image.decodedLayout = "packed_4bpp_high_low_nibbles";
    image.paletteName = palettePath.filename().string();
    image.rgba.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u);
    size_t pixelIndex = 0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const uint8_t packed = decoded[pixelIndex / 2u];
            const uint8_t colorIndex = (pixelIndex % 2u == 0u) ? ((packed >> 4u) & 0x0fu) : (packed & 0x0fu);
            const std::array<uint8_t, 3>& rgb = palette[colorIndex];
            const size_t dst = pixelIndex * 4u;
            image.rgba[dst + 0u] = rgb[0];
            image.rgba[dst + 1u] = rgb[1];
            image.rgba[dst + 2u] = rgb[2];
            image.rgba[dst + 3u] = 0xffu;
            ++pixelIndex;
        }
    }
    return image;
}

class BattleViewerApp {
public:
    BattleViewerApp(Options options, std::vector<mw::legacy3d::RuntimeRecord> records)
        : options_(std::move(options)), records_(std::move(records)) {
        battleCameraYawOffsetDegrees_ = options_.battleCameraYawOffsetDegrees;
        battleCameraPitchDegrees_ = options_.battleCameraPitchDegrees;
        battleCameraDistance_ = options_.battleCameraDistance;
        if (!options_.terrainArena.empty()) {
            terrainArenaNames_ = discoverTerrainArenaNames(options_.terrainRoot);
            const auto it = std::find(terrainArenaNames_.begin(), terrainArenaNames_.end(), options_.terrainArena);
            if (it != terrainArenaNames_.end()) {
                currentTerrainArenaPos_ = static_cast<size_t>(std::distance(terrainArenaNames_.begin(), it));
            }
        }
        if (options_.terrainScenarioIndex.has_value()) {
            terrainScenarioIndices_ = discoverTerrainScenarioIndices(options_.terrainScenarioPath);
            const auto it = std::find(terrainScenarioIndices_.begin(), terrainScenarioIndices_.end(), *options_.terrainScenarioIndex);
            if (it != terrainScenarioIndices_.end()) {
                currentTerrainScenarioPos_ = static_cast<size_t>(std::distance(terrainScenarioIndices_.begin(), it));
            }
        }
        if (options_.terrainWorldEnabled) {
            terrainRecords_ = mw::legacy3d::loadRuntimeShapeRecords(options_.terrainShapePath);
        }
        loadTerrainResources();
        loadBattleObjectiveModel();
        loadBattleNonPlayerCombatantVisuals();
        selectInitialRecord();
        rebuildBatch();
    }

    int run() {
        createWindow();
        createContext();
        gl_ = loadGlFunctions();
        uploadCockpitBackdrop();
        uploadCockpitStruts();
        uploadCockpitWidgets();
        uploadCockpitHudNumbers();
        loadCockpitHudFont();
        uploadMesh();
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        MSG msg{};
        LARGE_INTEGER freq{};
        LARGE_INTEGER prev{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&prev);
        double accumulator = 0.0;
        constexpr double tick = 1.0 / 60.0;

        bool running = true;
        while (running) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    running = false;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            LARGE_INTEGER now{};
            QueryPerformanceCounter(&now);
            const double dt = static_cast<double>(now.QuadPart - prev.QuadPart) / static_cast<double>(freq.QuadPart);
            prev = now;
            accumulator += std::min(dt, 0.25);
            while (accumulator >= tick) {
                pollInput(static_cast<float>(tick));
                updateRecordFrameAnimation(static_cast<float>(tick));
                updatePoseDemo(static_cast<float>(tick));
                accumulator -= tick;
            }
            render();
            Sleep(1);
        }

        destroyGlObjects();
        return 0;
    }

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        BattleViewerApp* app = reinterpret_cast<BattleViewerApp*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            app = reinterpret_cast<BattleViewerApp*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (app) {
            return app->handleMessage(hwnd, msg, wparam, lparam);
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        switch (msg) {
        case WM_SIZE:
            width_ = std::max(1, static_cast<int>(LOWORD(lparam)));
            height_ = std::max(1, static_cast<int>(HIWORD(lparam)));
            return 0;
        case WM_LBUTTONDOWN:
            dragging_ = true;
            lastMouseX_ = GET_X_LPARAM(lparam);
            lastMouseY_ = GET_Y_LPARAM(lparam);
            SetCapture(hwnd);
            return 0;
        case WM_LBUTTONUP:
            dragging_ = false;
            ReleaseCapture();
            return 0;
        case WM_RBUTTONDOWN:
            if (battleCameraControlActive()) {
                battleCameraDragging_ = true;
                lastMouseX_ = GET_X_LPARAM(lparam);
                lastMouseY_ = GET_Y_LPARAM(lparam);
                SetCapture(hwnd);
                return 0;
            }
            panning_ = true;
            lastMouseX_ = GET_X_LPARAM(lparam);
            lastMouseY_ = GET_Y_LPARAM(lparam);
            SetCapture(hwnd);
            return 0;
        case WM_MBUTTONDOWN:
            panning_ = true;
            lastMouseX_ = GET_X_LPARAM(lparam);
            lastMouseY_ = GET_Y_LPARAM(lparam);
            SetCapture(hwnd);
            return 0;
        case WM_RBUTTONUP:
            battleCameraDragging_ = false;
            if (!panning_) {
                ReleaseCapture();
            }
            return 0;
        case WM_MBUTTONUP:
            panning_ = false;
            ReleaseCapture();
            return 0;
        case WM_MOUSEMOVE:
            if (battleCameraDragging_) {
                const int x = GET_X_LPARAM(lparam);
                const int y = GET_Y_LPARAM(lparam);
                battleCameraYawOffsetDegrees_ -= static_cast<float>(x - lastMouseX_) * 0.28f;
                battleCameraPitchDegrees_ += static_cast<float>(y - lastMouseY_) * 0.18f;
                battleCameraPitchDegrees_ = std::max(8.0f, std::min(62.0f, battleCameraPitchDegrees_));
                lastMouseX_ = x;
                lastMouseY_ = y;
            } else if (dragging_) {
                const int x = GET_X_LPARAM(lparam);
                const int y = GET_Y_LPARAM(lparam);
                yaw_ += static_cast<float>(x - lastMouseX_) * 0.35f;
                pitch_ += static_cast<float>(y - lastMouseY_) * 0.35f;
                pitch_ = std::max(-89.0f, std::min(89.0f, pitch_));
                lastMouseX_ = x;
                lastMouseY_ = y;
            } else if (panning_) {
                const int x = GET_X_LPARAM(lparam);
                const int y = GET_Y_LPARAM(lparam);
                const float panScale = (bounds_.radius * 2.0f) / static_cast<float>(std::max(1, height_));
                panX_ += static_cast<float>(x - lastMouseX_) * panScale;
                panY_ -= static_cast<float>(y - lastMouseY_) * panScale;
                lastMouseX_ = x;
                lastMouseY_ = y;
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (battleCameraControlActive()) {
                battleCameraDistance_ *= GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 0.9f : 1.1f;
                battleCameraDistance_ = std::max(900.0f, std::min(8500.0f, battleCameraDistance_));
                updateTitle();
                return 0;
            }
            distance_ *= GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? 0.9f : 1.1f;
            distance_ = std::max(bounds_.radius * 0.8f, std::min(bounds_.radius * 12.0f, distance_));
            return 0;
        case WM_KEYDOWN:
            if (battleCommandMapActive(options_) && (wparam == 'C' || wparam == VK_RETURN)) {
                closeBattleCommandMap();
                return 0;
            }
            if (!battleCommandMapActive(options_) && options_.battleCockpit && wparam == 'C') {
                options_.battleCommandMap = true;
                options_.battleCommandMapScreen = BattleCommandMapScreen::CockpitCommand;
                cockpitBackdropReady_ = false;
                uploadCockpitBackdrop();
                updateTitle();
                return 0;
            }
            if (wparam == VK_ESCAPE) {
                if (battleInteractiveMode(options_)) {
                    return 0;
                }
                PostQuitMessage(0);
                return 0;
            }
            if (wparam == 'R') {
                if (battleInteractiveMode(options_)) {
                    return 0;
                }
                yaw_ = -30.0f;
                pitch_ = 18.0f;
                distance_ = bounds_.radius * 3.2f;
                panX_ = 0.0f;
                panY_ = 0.0f;
                return 0;
            }
            if (wparam == VK_F1) {
                showOverlay_ = !showOverlay_;
                return 0;
            }
            if (wparam == VK_F2) {
                showComponentDebug_ = !showComponentDebug_;
                updateTitle();
                return 0;
            }
            if (wparam == VK_F3) {
                if (options_.battleDrive) {
                    battleFollowCamera_ = !battleFollowCamera_;
                    updateTitle();
                    return 0;
                }
                if (options_.battleCockpit) {
                    options_.battleCockpitExternalCamera = !options_.battleCockpitExternalCamera;
                    updateTitle();
                    return 0;
                }
                poseDemo_ = !poseDemo_;
                if (!poseDemo_) {
                    poseTime_ = 0.0f;
                    applyPoseDemo();
                }
                updateTitle();
                return 0;
            }
            if (wparam == VK_F4) {
                cyclePoseDemoTarget();
                return 0;
            }
            if (wparam == VK_F5) {
                cyclePosePivotMode();
                return 0;
            }
            if (wparam == VK_F6) {
                cyclePoseDemoAxis();
                return 0;
            }
            if (wparam == VK_F7) {
                toggleHiddenComponent(poseDemoComponentId_);
                return 0;
            }
            if (wparam == VK_F8) {
                options_.hiddenComponentIds.clear();
                updateTitle();
                return 0;
            }
            if (wparam == VK_SPACE) {
                if (battleInteractiveMode(options_)) {
                    return 0;
                }
                animationPaused_ = !animationPaused_;
                updateTitle();
                return 0;
            }
            if (wparam == VK_F9) {
                stepAnimationFrame(-1);
                return 0;
            }
            if (wparam == VK_F10) {
                stepAnimationFrame(1);
                return 0;
            }
            if (wparam == VK_F11) {
                cyclePoseDemoAmplitude();
                return 0;
            }
            if (wparam == 'O') {
                if (battleInteractiveMode(options_)) {
                    return 0;
                }
                options_.perspective = !options_.perspective;
                updateTitle();
                return 0;
            }
            if (wparam == 'A') {
                if (battleInteractiveMode(options_)) {
                    alignBattleTorsoToFeet();
                    return 0;
                }
                if (options_.assemblyRecordIndices.empty()) {
                    options_.assemblyRecordIndices.push_back(options_.recordIndex);
                }
                options_.assembled = !options_.assembled;
                rebuildAndUpload(false);
                return 0;
            }
            if (wparam == 'S') {
                if (battleInteractiveMode(options_)) {
                    return 0;
                }
                cycleShadeMode();
                return 0;
            }
            if (wparam == 'W') {
                if (battleInteractiveMode(options_)) {
                    return 0;
                }
                toggleWire();
                return 0;
            }
            if (wparam == 'T') {
                if (battleInteractiveMode(options_)) {
                    return 0;
                }
                toggleTerrainColorMode();
                return 0;
            }
            if (wparam == 'G') {
                terrainGridEdgesVisible_ = !terrainGridEdgesVisible_;
                updateTitle();
                return 0;
            }
            if (wparam == 'K') {
                toggleTerrainSkyMode();
                return 0;
            }
            if (wparam == 'P') {
                if (battleInteractiveMode(options_)) {
                    animationPaused_ = !animationPaused_;
                    updateTitle();
                    return 0;
                }
                const int repeatCount = std::max(1, static_cast<int>(LOWORD(lparam)));
                screenshotRequests_ = std::min(screenshotRequests_ + repeatCount, 32);
                return 0;
            }
            if (wparam == VK_F12) {
                const int repeatCount = std::max(1, static_cast<int>(LOWORD(lparam)));
                screenshotRequests_ = std::min(screenshotRequests_ + repeatCount, 32);
                return 0;
            }
            if (wparam == 'Q' && battleInteractiveMode(options_)) {
                PostQuitMessage(0);
                return 0;
            }
            if (wparam == 'H' && options_.battleCockpit) {
                cycleCockpitHudColor();
                return 0;
            }
            if (wparam == 'Z' && options_.battleCockpit) {
                cycleBattleCockpitZoom();
                return 0;
            }
            if (wparam == 'B' && options_.battleCockpit) {
                options_.battleCockpitBob = !options_.battleCockpitBob;
                updateTitle();
                return 0;
            }
            if (wparam == 'J' && battleInteractiveMode(options_)) {
                if ((lparam & (LPARAM{1} << 30)) == 0) {
                    pendingJumpJetToggle_ = true;
                }
                return 0;
            }
            if (wparam == 'N' && queueBattleAimPitchInput(1)) {
                return 0;
            }
            if (wparam == 'M' && queueBattleAimPitchInput(-1)) {
                return 0;
            }
            if (handleBattleDriveOriginalNoopKey(wparam)) {
                return 0;
            }
            if (wparam == VK_UP && battleInteractiveMode(options_)) {
                if (battleJumpControlActive()) {
                    return 0;
                }
                adjustBattleDriveThrottle(kBattleDriveThrottleStep);
                return 0;
            }
            if (wparam == VK_DOWN && battleInteractiveMode(options_)) {
                if (battleJumpControlActive()) {
                    return 0;
                }
                adjustBattleDriveThrottle(-kBattleDriveThrottleStep);
                return 0;
            }
            if (wparam == VK_PRIOR) {
                if (switchBattleDriveTerrainScenario(-1)) {
                    return 0;
                }
                switchTerrainArena(-1);
                return 0;
            }
            if (wparam == VK_NEXT) {
                if (switchBattleDriveTerrainScenario(1)) {
                    return 0;
                }
                switchTerrainArena(1);
                return 0;
            }
            if (wparam == VK_OEM_COMMA) {
                if (queueBattleTorsoYawInput(1)) {
                    return 0;
                }
                if (applyBattleTorsoYawInput(1)) {
                    return 0;
                }
            }
            if (wparam == VK_OEM_PERIOD) {
                if (queueBattleTorsoYawInput(-1)) {
                    return 0;
                }
                if (applyBattleTorsoYawInput(-1)) {
                    return 0;
                }
            }
            if (wparam == VK_OEM_4) {
                switchRecord(-1);
                return 0;
            }
            if (wparam == VK_OEM_6) {
                switchRecord(1);
                return 0;
            }
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wparam, lparam);
        }
    }

    void createWindow() {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        const wchar_t* className = L"MW1989BattleViewerGL";
        WNDCLASSW wc{};
        wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &BattleViewerApp::wndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        wc.lpszClassName = className;
        RegisterClassW(&wc);

        const bool fixedVirtualWindow = options_.battleCockpit || options_.battleCommandMap;
        const DWORD windowStyle = fixedVirtualWindow
            ? (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)
            : WS_OVERLAPPEDWINDOW;
        const int clientWidth = fixedVirtualWindow ? kCockpitViewerClientWidth : kBattleViewerDefaultClientWidth;
        const int clientHeight = fixedVirtualWindow ? kCockpitViewerClientHeight : kBattleViewerDefaultClientHeight;
        RECT windowRect{0, 0, clientWidth, clientHeight};
        AdjustWindowRect(&windowRect, windowStyle, FALSE);

        hwnd_ = CreateWindowExW(
            0,
            className,
            L"MechWarrior 1989 C++ OpenGL Battle Viewer",
            windowStyle,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            windowRect.right - windowRect.left,
            windowRect.bottom - windowRect.top,
            nullptr,
            nullptr,
            instance,
            this);
        if (!hwnd_) {
            throw std::runtime_error("CreateWindowExW failed");
        }
        RECT clientRect{};
        GetClientRect(hwnd_, &clientRect);
        width_ = std::max(1, static_cast<int>(clientRect.right - clientRect.left));
        height_ = std::max(1, static_cast<int>(clientRect.bottom - clientRect.top));
        distance_ = bounds_.radius * 3.2f;
    }

    bool battleCameraControlActive() const {
        return (options_.battleDrive && battleFollowCamera_) ||
               (options_.battleCockpit && options_.battleCockpitExternalCamera);
    }

    double battleViewportProjectionZoom() const {
        if (!options_.battleCockpit || options_.battleCockpitExternalCamera) {
            return 1.0;
        }
        return static_cast<double>(std::clamp(
            battleCockpitZoomLevel_,
            kBattleCockpitMinZoomLevel,
            kBattleCockpitMaxZoomLevel));
    }

    const char* battleRuntimeCameraModeName() const {
        if (options_.battleCockpit) {
            return options_.battleCockpitExternalCamera ? "external" : "cockpit";
        }
        if (options_.battleDrive) {
            return battleFollowCamera_ ? "follow" : "orbit";
        }
        return "orbit";
    }

    void applyLookAt(Vec3 eye, Vec3 target, Vec3 up) {
        const Vec3 forward = normalize(subtract(target, eye));
        const Vec3 side = normalize(cross(forward, up));
        const Vec3 cameraUp = cross(side, forward);
        const GLfloat matrix[16] = {
            side.x, cameraUp.x, -forward.x, 0.0f,
            side.y, cameraUp.y, -forward.y, 0.0f,
            side.z, cameraUp.z, -forward.z, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        glMultMatrixf(matrix);
        glTranslatef(-eye.x, -eye.y, -eye.z);
    }

    void applyOrbitCamera() {
        glTranslatef(panX_, panY_, -distance_);
        glRotatef(pitch_, 1.0f, 0.0f, 0.0f);
        glRotatef(yaw_, 0.0f, 1.0f, 0.0f);
        glTranslatef(-bounds_.center.x, -bounds_.center.y, -bounds_.center.z);
    }

    void applyFollowCamera() {
        const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot();
        if (combatant == nullptr) {
            applyOrbitCamera();
            return;
        }

        const float bodyHeading = static_cast<float>(combatant->transform.headingRadians);
        const float orbitHeading =
            bodyHeading + battleCameraYawOffsetDegrees_ * 3.14159265358979323846f / 180.0f;
        const float orbitPitch = battleCameraPitchDegrees_ * 3.14159265358979323846f / 180.0f;
        const float forwardX = std::sin(orbitHeading);
        const float forwardZ = std::cos(orbitHeading);
        const float horizontalDistance = std::cos(orbitPitch) * battleCameraDistance_;
        const Vec3 player{
            static_cast<float>(combatant->transform.x),
            static_cast<float>(combatant->transform.y),
            static_cast<float>(combatant->transform.z),
        };
        const Vec3 eye{
            player.x - forwardX * horizontalDistance,
            player.y + std::sin(orbitPitch) * battleCameraDistance_,
            player.z - forwardZ * horizontalDistance,
        };
        const Vec3 target{
            player.x,
            player.y + 180.0f,
            player.z,
        };
        applyLookAt(eye, target, Vec3{0.0f, 1.0f, 0.0f});
    }

    double battleCockpitBobSpeedRatio(const mw::battle::CombatantSnapshot& combatant) const {
        return mw::battle::battleCockpitBobSpeedRatio(
            combatant.forwardSpeed,
            combatant.maxForwardSpeed,
            combatant.maxReverseSpeed);
    }

    double battleCockpitBobPhaseElapsedMs(const mw::battle::CombatantSnapshot& combatant) const {
        const double speedRatio = battleCockpitBobSpeedRatio(combatant);
        const double residualMs = battleDriveAccumulator_ * 1000.0 * speedRatio;
        return static_cast<double>(combatant.walkAnimationElapsedMs) + residualMs;
    }

    float battleCockpitBobOffsetWorldUnits() const {
        if (!options_.battleCockpit || options_.battleCockpitExternalCamera || !options_.battleCockpitBob) {
            return 0.0f;
        }
        const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot();
        if (combatant == nullptr) {
            return 0.0f;
        }
        return static_cast<float>(mw::battle::battleCockpitBobOffsetWorldUnits(
            combatant->forwardSpeed,
            combatant->maxForwardSpeed,
            combatant->maxReverseSpeed,
            battleCockpitBobPhaseElapsedMs(*combatant),
            combatant->mechDestroyed));
    }

    void applyCockpitCamera() {
        if (!options_.battleSnapshot.has_value() || !options_.battleSnapshot->camera.valid) {
            applyOrbitCamera();
            return;
        }

        const mw::battle::CameraSnapshot& camera = options_.battleSnapshot->camera;
        const float heading = static_cast<float>(camera.transform.headingRadians);
        const float bobOffsetY = battleCockpitBobOffsetWorldUnits();
        const Vec3 eye{
            static_cast<float>(camera.transform.x),
            static_cast<float>(camera.transform.y) + bobOffsetY,
            static_cast<float>(camera.transform.z),
        };
        const Vec3 target{
            eye.x + std::sin(heading) * 1000.0f,
            eye.y,
            eye.z + std::cos(heading) * 1000.0f,
        };
        applyLookAt(eye, target, Vec3{0.0f, 1.0f, 0.0f});
    }

    void createContext() {
        hdc_ = GetDC(hwnd_);
        if (!hdc_) {
            throw std::runtime_error("GetDC failed");
        }

        PIXELFORMATDESCRIPTOR pfd{};
        pfd.nSize = sizeof(pfd);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 24;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;

        const int pixelFormat = ChoosePixelFormat(hdc_, &pfd);
        if (pixelFormat == 0 || SetPixelFormat(hdc_, pixelFormat, &pfd) == FALSE) {
            throw std::runtime_error("could not set OpenGL pixel format");
        }
        hrc_ = wglCreateContext(hdc_);
        if (!hrc_ || wglMakeCurrent(hdc_, hrc_) == FALSE) {
            throw std::runtime_error("could not create WGL context");
        }

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDisable(GL_CULL_FACE);
        createOverlayFont();
        checkGl("createContext");
    }

    void createOverlayFont() {
        fontBase_ = glGenLists(96);
        if (fontBase_ == 0) {
            throw std::runtime_error("glGenLists failed for overlay font");
        }

        HFONT font = CreateFontA(
            15,
            0,
            0,
            0,
            FW_NORMAL,
            FALSE,
            FALSE,
            FALSE,
            ANSI_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            FF_DONTCARE | FIXED_PITCH,
            "Consolas");
        if (!font) {
            throw std::runtime_error("CreateFontA failed for overlay font");
        }

        HGDIOBJ previous = SelectObject(hdc_, font);
        const BOOL ok = wglUseFontBitmapsA(hdc_, 32, 96, fontBase_);
        SelectObject(hdc_, previous);
        DeleteObject(font);
        if (ok == FALSE) {
            throw std::runtime_error("wglUseFontBitmapsA failed for overlay font");
        }
    }

    void loadTerrainResources() {
        terrainMesh_ = mw::legacy3d::TerrainMesh{};
        terrainWorld_ = mw::legacy3d::TerrainWorld{};
        terrainObjectBatch_ = mw::legacy3d::GpuBatch{};
        terrainObjectDitherBatch_ = mw::legacy3d::GpuBatch{};
        if (!options_.terrainEnabled) {
            return;
        }
        if (!options_.terrainLayoutNames.empty()) {
            if (terrainRecords_.empty()) {
                terrainRecords_ = mw::legacy3d::loadRuntimeShapeRecords(options_.terrainShapePath);
            }
            terrainWorld_.resourceName = "layout:" + joinStringList(options_.terrainLayoutNames);
            if (options_.terrainLayoutNames.size() != 4u) {
                throw std::runtime_error("terrain layout rendering requires exactly four tiles");
            }
            std::array<mw::legacy3d::TerrainGrid, 4> layoutGrids;
            for (size_t i = 0; i < options_.terrainLayoutNames.size(); ++i) {
                layoutGrids[i] = mw::legacy3d::loadTerrainGrid(
                    options_.terrainRoot / "GRD" / (options_.terrainLayoutNames[i] + ".GRD"));
            }
            terrainMesh_ = mw::legacy3d::buildTerrainMesh(
                mw::legacy3d::composeTerrainGrid2x2(
                    layoutGrids,
                    terrainWorld_.resourceName,
                    options_.terrainTileMirrorX,
                    options_.terrainTileMirrorY),
                options_.terrainCellSize,
                20.0f);
            const float tileWidth = static_cast<float>(layoutGrids[0].width) * options_.terrainCellSize;
            const float tileDepth = static_cast<float>(layoutGrids[0].height) * options_.terrainCellSize;
            const Bounds terrainBounds = terrainMeshBounds(terrainMesh_);
            const Bounds placementBounds = Bounds{
                Vec3{0.0f, terrainBounds.min.y, 0.0f},
                Vec3{tileWidth * 2.0f, terrainBounds.max.y, tileDepth * 2.0f},
                Vec3{tileWidth, (terrainBounds.min.y + terrainBounds.max.y) * 0.5f, tileDepth},
                std::max(1.0f, std::sqrt(tileWidth * tileWidth + tileDepth * tileDepth)),
            };
            for (size_t i = 0; i < options_.terrainLayoutNames.size(); ++i) {
                const std::string& name = options_.terrainLayoutNames[i];
                const mw::legacy3d::TerrainWorld tileWorld =
                    mw::legacy3d::loadTerrainWorld(options_.terrainRoot / "WLD" / (name + ".WLD"));
                const TerrainObjectBatches batches = buildTerrainObjectBatches(
                    tileWorld,
                    terrainRecords_,
                    batchOptions(),
                    placementBounds,
                    options_.terrainObjectInset,
                    options_.terrainObjectScale,
                    options_.terrainCellSize,
                    true,
                    options_.terrainTileMirrorX,
                    options_.terrainTileMirrorY,
                    options_.terrainObjectMirrorY,
                    options_.terrainColorMode);
                appendGpuBatch(terrainObjectBatch_, batches.base);
                appendGpuBatch(terrainObjectDitherBatch_, batches.dither);
                terrainWorld_.objects.insert(
                    terrainWorld_.objects.end(),
                    tileWorld.objects.begin(),
                    tileWorld.objects.end());
            }
            return;
        }

        terrainMesh_ = mw::legacy3d::buildTerrainMesh(
            mw::legacy3d::loadTerrainGrid(options_.terrainGridPath),
            options_.terrainCellSize,
            20.0f);
        if (options_.terrainTileMirrorX) {
            mirrorTerrainMeshX(terrainMesh_);
        }
        if (options_.terrainTileMirrorY) {
            mirrorTerrainMeshY(terrainMesh_);
        }
        if (!options_.terrainWorldEnabled) {
            return;
        }

        terrainWorld_ = mw::legacy3d::loadTerrainWorld(options_.terrainWorldPath);
        if (terrainRecords_.empty()) {
            terrainRecords_ = mw::legacy3d::loadRuntimeShapeRecords(options_.terrainShapePath);
        }
                const TerrainObjectBatches batches = buildTerrainObjectBatches(
                    terrainWorld_,
                    terrainRecords_,
                    batchOptions(),
                    terrainMeshBounds(terrainMesh_),
                    options_.terrainObjectInset,
                    options_.terrainObjectScale,
                    options_.terrainCellSize,
                    false,
                    options_.terrainTileMirrorX,
                    options_.terrainTileMirrorY,
                    options_.terrainObjectMirrorY,
                    options_.terrainColorMode);
                terrainObjectBatch_ = batches.base;
                terrainObjectDitherBatch_ = batches.dither;
    }

    void switchTerrainArena(int delta) {
        if (terrainArenaNames_.empty()) {
            return;
        }
        const int count = static_cast<int>(terrainArenaNames_.size());
        int next = static_cast<int>(currentTerrainArenaPos_) + delta;
        if (next < 0) {
            next = count - 1;
        } else if (next >= count) {
            next = 0;
        }
        currentTerrainArenaPos_ = static_cast<size_t>(next);
        options_.terrainArena = terrainArenaNames_[currentTerrainArenaPos_];
        options_.terrainGridPath = options_.terrainRoot / "GRD" / (options_.terrainArena + ".GRD");
        options_.terrainWorldPath = options_.terrainRoot / "WLD" / (options_.terrainArena + ".WLD");
        options_.terrainShapePath = options_.terrainRoot / "TBL" / "viewer8" / "TERPCK.TBL";
        options_.terrainEnabled = true;
        options_.terrainWorldEnabled = true;
        loadTerrainResources();
        rebuildAndUpload(true);
    }

    bool switchBattleDriveTerrainScenario(int delta) {
        if (!battleInteractiveMode(options_) || terrainScenarioIndices_.empty() || !options_.battleWorld.has_value()) {
            return false;
        }
        const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot();
        if (combatant != nullptr && !options_.battleOriginalPlacement) {
            options_.battleStartWorld = {combatant->transform.x, combatant->transform.z};
            options_.battleStartGrid.reset();
            options_.battleStartHeadingRadians = combatant->transform.headingRadians;
        }

        const int count = static_cast<int>(terrainScenarioIndices_.size());
        int next = static_cast<int>(currentTerrainScenarioPos_) + delta;
        next %= count;
        if (next < 0) {
            next += count;
        }
        currentTerrainScenarioPos_ = static_cast<size_t>(next);
        applyTerrainScenarioIndex(options_, terrainScenarioIndices_[currentTerrainScenarioPos_]);
        if (options_.battleOriginalPlacement) {
            options_.battleSetup.reset();
            resolveOriginalBattlefieldSetup(options_);
        }
        if (options_.terrainShapePath.empty()) {
            options_.terrainShapePath = options_.terrainRoot / "TBL" / "viewer8" / "TERPCK.TBL";
        }
        loadTerrainResources();

        battleDriveThrottleCommand_ = 0.0;
        pendingDriveTorsoYawStepDelta_ = 0;
        battleDriveAccumulator_ = 0.0;
        options_.battleWorld = mw::battle::BattleWorld::create(battleStartParamsFromOptions(options_));
        options_.battleSnapshot = options_.battleWorld->snapshot();
        loadBattleNonPlayerCombatantVisuals();
        rebuildAndUpload(false);
        updateTitle();
        return true;
    }

    void closeBattleCommandMap() {
        if (!battleCommandMapActive(options_)) {
            return;
        }

        const bool enteringBattleFromMissionStatus =
            options_.battleCommandMapScreen == BattleCommandMapScreen::MissionStatus &&
            !options_.battleCockpit;
        options_.battleCommandMapScreen = BattleCommandMapScreen::None;
        if (enteringBattleFromMissionStatus) {
            options_.battleCommandMap = false;
            options_.battleCockpit = true;
            if (!cockpitStrutsReady_) {
                uploadCockpitStruts();
            }
            if (!cockpitWidgetsReady_) {
                uploadCockpitWidgets();
            }
            if (!cockpitHudNumbersReady_) {
                uploadCockpitHudNumbers();
            }
            if (!cockpitHudFont_.has_value()) {
                loadCockpitHudFont();
            }
        }
        cockpitBackdropReady_ = false;
        uploadCockpitBackdrop();
        updateTitle();
    }

    void uploadMesh() {
        if (mesh_.vbo == 0) {
            gl_.glGenBuffers(1, &mesh_.vbo);
        }
        if (mesh_.triEbo == 0) {
            gl_.glGenBuffers(1, &mesh_.triEbo);
        }
        if (mesh_.lineEbo == 0) {
            gl_.glGenBuffers(1, &mesh_.lineEbo);
        }

        gl_.glBindBuffer(GL_ARRAY_BUFFER, mesh_.vbo);
        gl_.glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(batch_.vertices.size() * sizeof(float)),
            batch_.vertices.empty() ? nullptr : batch_.vertices.data(),
            GL_STATIC_DRAW);

        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_.triEbo);
        gl_.glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(batch_.triangleIndices.size() * sizeof(uint32_t)),
            batch_.triangleIndices.empty() ? nullptr : batch_.triangleIndices.data(),
            GL_STATIC_DRAW);

        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_.lineEbo);
        gl_.glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(batch_.lineIndices.size() * sizeof(uint32_t)),
            batch_.lineIndices.empty() ? nullptr : batch_.lineIndices.data(),
            GL_STATIC_DRAW);

        mesh_.vertexCount = static_cast<int>(batch_.vertices.size() / 6u);
        mesh_.triangleIndexCount = static_cast<GLsizei>(batch_.triangleIndices.size());
        mesh_.lineIndexCount = static_cast<GLsizei>(batch_.lineIndices.size());
        mesh_.triangleCount = static_cast<int>(batch_.triangleIndices.size() / 3u);
        mesh_.linePairCount = static_cast<int>(batch_.lineIndices.size() / 2u);
        updateTitle();
        checkGl("uploadMesh");
    }

    void uploadCockpitBackdrop() {
        if (!options_.battleCockpit && !options_.battleCommandMap) {
            return;
        }

        const std::string scrName = battleCommandMapActive(options_)
                                        ? battleCommandMapScrName(options_.battleCommandMapScreen)
                                        : std::string(cockpitLayoutForOptions(options_).scrName);
        cockpitBackdropImage_ = loadCockpitScrImage(
            options_.terrainRoot / "SCR" / scrName,
            options_.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options_));
        const auto palette = loadPalEgaBank0LowPalette(
            options_.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options_));
        const BattlefieldBoundaryPaletteIndices boundaryIndices = battlefieldBoundaryPaletteIndices(options_);
        const auto toFloatRgb = [](const std::array<uint8_t, 3>& rgb) {
            return std::array<float, 3>{
                static_cast<float>(rgb[0]) / 255.0f,
                static_cast<float>(rgb[1]) / 255.0f,
                static_cast<float>(rgb[2]) / 255.0f,
            };
        };
        battlefieldBoundaryOrdinaryRgb_ = toFloatRgb(palette[boundaryIndices.ordinary]);
        battlefieldBoundaryAllowedRgb_ = toFloatRgb(palette[boundaryIndices.allowed]);
        if (cockpitTexture_ == 0) {
            glGenTextures(1, &cockpitTexture_);
        }
        glBindTexture(GL_TEXTURE_2D, cockpitTexture_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA,
            cockpitBackdropImage_.width,
            cockpitBackdropImage_.height,
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            cockpitBackdropImage_.rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);
        cockpitBackdropReady_ = true;
        checkGl("uploadCockpitBackdrop");
    }

    void uploadCockpitStruts() {
        if (!options_.battleCockpit) {
            return;
        }
        if (!cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
            return;
        }

        const std::vector<CockpitBmpSprite> sprites = loadCockpitBmpSprites(
            options_.terrainRoot / "BMP" / "STRUTS.BMP",
            options_.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options_),
            0u);
        cockpitStrutTextures_.clear();
        cockpitStrutTextures_.reserve(sprites.size());
        for (const CockpitBmpSprite& sprite : sprites) {
            CockpitSpriteTexture texture;
            texture.index = sprite.index;
            texture.width = sprite.width;
            texture.height = sprite.height;
            glGenTextures(1, &texture.texture);
            glBindTexture(GL_TEXTURE_2D, texture.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                texture.width,
                texture.height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                sprite.rgba.data());
            cockpitStrutTextures_.push_back(texture);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        cockpitStrutsReady_ = true;
        checkGl("uploadCockpitStruts");
    }

    void uploadCockpitWidgets() {
        if (!options_.battleCockpit) {
            return;
        }

        const std::vector<CockpitBmpSprite> sprites = loadCockpitBmpSprites(
            options_.terrainRoot / "BMP" / "COCKPIT.BMP",
            options_.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options_),
            0u);
        cockpitWidgetTextures_.clear();
        cockpitWidgetTextures_.reserve(sprites.size());
        for (const CockpitBmpSprite& sprite : sprites) {
            CockpitSpriteTexture texture;
            texture.index = sprite.index;
            texture.width = sprite.width;
            texture.height = sprite.height;
            texture.fillRgb = cockpitSpriteCenterRgb(sprite);
            glGenTextures(1, &texture.texture);
            glBindTexture(GL_TEXTURE_2D, texture.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                texture.width,
                texture.height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                sprite.rgba.data());
            cockpitWidgetTextures_.push_back(texture);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        cockpitWidgetsReady_ = true;
        checkGl("uploadCockpitWidgets");
    }

    void uploadCockpitHudNumbers() {
        if (!options_.battleCockpit) {
            return;
        }
        if (!cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
            return;
        }

        const std::vector<CockpitBmpSprite> sprites = loadCockpitBmpSprites(
            options_.terrainRoot / "BMP" / "HUD_NUMS.BMP",
            options_.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options_),
            0u);
        cockpitHudNumberTextures_.clear();
        cockpitHudNumberTextures_.reserve(sprites.size());
        for (const CockpitBmpSprite& sprite : sprites) {
            CockpitSpriteTexture texture;
            texture.index = sprite.index;
            texture.width = sprite.width;
            texture.height = sprite.height;
            glGenTextures(1, &texture.texture);
            glBindTexture(GL_TEXTURE_2D, texture.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                GL_RGBA,
                texture.width,
                texture.height,
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                sprite.rgba.data());
            cockpitHudNumberTextures_.push_back(texture);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        cockpitHudNumbersReady_ = true;
        checkGl("uploadCockpitHudNumbers");
    }

    void loadCockpitHudFont() {
        if (!options_.battleCockpit && !options_.battleCommandMap) {
            return;
        }
        cockpitHudFont_ = loadCockpitFont(options_.terrainRoot / "FNT" / "6X6B.FNT");
    }

    mw::legacy3d::GpuBatchOptions batchOptions() const {
        mw::legacy3d::GpuBatchOptions options;
        options.shadeMode = options_.shadeMode;
        options.edgeRgb = options_.wire ? std::array<float, 3>{0.86f, 0.86f, 0.86f}
                                        : std::array<float, 3>{0.02f, 0.02f, 0.02f};
        return options;
    }

    void loadBattleObjectiveModel() {
        battleObjectiveModelBatch_ = mw::legacy3d::GpuBatch{};
        battleObjectiveModelBounds_.reset();
        const std::optional<LoadedBattleObjectiveModel> loaded =
            loadBattleObjectiveModelAsset(options_, batchOptions());
        if (!loaded.has_value()) {
            return;
        }
        battleObjectiveModelBatch_ = loaded->batch;
        battleObjectiveModelBounds_ = computeBounds(battleObjectiveModelBatch_);
    }

    void loadBattleNonPlayerCombatantVisuals() {
        battleNonPlayerCombatantVisuals_ =
            buildBattleNonPlayerCombatantVisuals(options_, batchOptions());
    }

    void selectInitialRecord() {
        for (size_t i = 0; i < records_.size(); ++i) {
            if (records_[i].recordIndex == options_.recordIndex) {
                currentRecordPos_ = i;
                return;
            }
        }
        throw std::runtime_error("record not found: " + std::to_string(options_.recordIndex));
    }

    void rebuildBatch() {
        const mw::legacy3d::RuntimeRecord& record = records_[currentRecordPos_];
        options_.recordIndex = record.recordIndex;
        battlePlacementBounds_.reset();
        if (options_.assembled) {
            if (options_.assemblyRecordIndices.empty()) {
                options_.assemblyRecordIndices.push_back(options_.recordIndex);
            }
            bindDefinition_ = buildCliAssemblyDefinition(options_);
            animationDefinition_ = buildCliAnimationDefinition(options_);
            assemblyFrameAnimationDefinition_ = buildCliAssemblyFrameAnimationDefinition(options_);
            animationElapsedMs_ = 0;
            if (options_.battleSnapshot.has_value() && !options_.battleSnapshot->combatants.empty()) {
                const uint64_t snapshotElapsed = options_.battleSnapshot->combatants.front().walkAnimationElapsedMs;
                animationElapsedMs_ = snapshotElapsed > static_cast<uint64_t>(std::numeric_limits<int>::max())
                                          ? std::numeric_limits<int>::max()
                                          : static_cast<int>(snapshotElapsed);
            }
            animationFrameIndex_ = 0;
            if (options_.animationEnabled && (hasComponentFrameAnimation() || hasAssemblyFrameAnimation())) {
                animationFrameIndex_ = displayAnimationFrameForElapsedMs(animationElapsedMs_);
            }
            const mw::mech3d::MechModelDefinition frameDefinition = currentFrameDefinition();
            renderInstance_ = mw::mech3d::buildMechRenderInstance(frameDefinition, records_, batchOptions());
            batch_ = renderInstance_.batch;
            bounds_ = toViewerBounds(renderInstance_.assembly.bounds);
            if (options_.battleSnapshot.has_value()) {
                const mw::mech3d::MechRenderInstance placementInstance =
                    mw::mech3d::buildMechRenderInstance(bindDefinition_, records_, batchOptions());
                if (placementInstance.assembly.bounds.valid) {
                    battlePlacementBounds_ = toViewerBounds(placementInstance.assembly.bounds);
                }
            }
            applyPoseDemo();
        } else {
            bindDefinition_ = mw::mech3d::MechModelDefinition{};
            animationDefinition_ = mw::mech3d::ModelAnimationDefinition{};
            assemblyFrameAnimationDefinition_ = mw::mech3d::ModelAssemblyFrameAnimationDefinition{};
            renderInstance_ = mw::mech3d::MechRenderInstance{};
            batch_ = mw::legacy3d::buildGpuBatch(record, batchOptions());
            bounds_ = computeBounds(batch_);
        }
        updateTerrainPlacement();
    }

    void switchRecord(int delta) {
        if (records_.empty()) {
            return;
        }
        options_.assembled = false;
        const int count = static_cast<int>(records_.size());
        int next = static_cast<int>(currentRecordPos_) + delta;
        if (next < 0) {
            next = count - 1;
        } else if (next >= count) {
            next = 0;
        }
        currentRecordPos_ = static_cast<size_t>(next);
        rebuildAndUpload(true);
    }

    void rebuildAndUpload(bool resetDistance) {
        rebuildBatch();
        if (resetDistance) {
            distance_ = bounds_.radius * 3.2f;
            panX_ = 0.0f;
            panY_ = 0.0f;
        }
        uploadMesh();
    }

    mw::mech3d::MechModelDefinition currentFrameDefinition() const {
        if (hasAssemblyFrameAnimation()) {
            return mw::mech3d::applyAssemblyFrameAnimationFrame(assemblyFrameAnimationDefinition_, animationFrameIndex_);
        }
        if (!hasComponentFrameAnimation()) {
            return bindDefinition_;
        }
        return mw::mech3d::applyAnimationFrame(bindDefinition_, animationDefinition_, animationFrameIndex_);
    }

    bool hasComponentFrameAnimation() const {
        return !animationDefinition_.sequences.empty();
    }

    bool hasAssemblyFrameAnimation() const {
        return !assemblyFrameAnimationDefinition_.frames.empty();
    }

    int currentAnimationFrameCount() const {
        if (hasAssemblyFrameAnimation()) {
            return mw::mech3d::assemblyFrameAnimationFrameCount(assemblyFrameAnimationDefinition_);
        }
        if (hasComponentFrameAnimation()) {
            return mw::mech3d::animationFrameCount(animationDefinition_);
        }
        return 0;
    }

    int animationFrameForElapsedMs(int elapsedMs) const {
        if (hasAssemblyFrameAnimation()) {
            return mw::mech3d::assemblyFrameAnimationFrameForElapsedMs(assemblyFrameAnimationDefinition_, elapsedMs);
        }
        return mw::mech3d::animationFrameForElapsedMs(animationDefinition_, elapsedMs);
    }

    bool shouldReverseBattleWalkAnimation() const {
        if (!hasComponentFrameAnimation()) {
            return false;
        }
        const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot();
        if (combatant == nullptr) {
            return false;
        }
        return combatant->activeAnimationId == "walk" && combatant->forwardSpeed < -0.0001;
    }

    bool applyBattleSnapshotAnimationId(const mw::battle::CombatantSnapshot& combatant) {
        if (combatant.activeAnimationId.empty()) {
            return false;
        }
        const std::string activeAnimationId = normalizeId(combatant.activeAnimationId);
        if (activeAnimationId == options_.animationName) {
            return false;
        }
        options_.animationName = activeAnimationId;
        options_.animationEnabled = true;
        options_.assembled = true;
        animationFrameIndex_ = 0;
        rebuildBatch();
        uploadMesh();
        return true;
    }

    int displayAnimationFrameForElapsedMs(int elapsedMs) const {
        int frame = animationFrameForElapsedMs(elapsedMs);
        if (shouldReverseBattleWalkAnimation()) {
            const int frameCount = currentAnimationFrameCount();
            if (frameCount > 0) {
                frame = frameCount - 1 - frame;
            }
        }
        return frame;
    }

    std::string animationRuntimeLabel() const {
        return hasAssemblyFrameAnimation() ? "assembly-frame animation" : "record-frame animation";
    }

    void updateRecordFrameAnimation(float dt) {
        if (battleInteractiveMode(options_)) {
            return;
        }
        if (!options_.animationEnabled || !options_.assembled || (!hasComponentFrameAnimation() && !hasAssemblyFrameAnimation())) {
            return;
        }
        if (animationPaused_) {
            return;
        }
        animationElapsedMs_ += static_cast<int>(dt * 1000.0f);
        const int nextFrame = displayAnimationFrameForElapsedMs(animationElapsedMs_);
        if (nextFrame == animationFrameIndex_) {
            return;
        }

        setAnimationFrameIndex(nextFrame);
    }

    void stepAnimationFrame(int delta) {
        if (!options_.animationEnabled || !options_.assembled || (!hasComponentFrameAnimation() && !hasAssemblyFrameAnimation())) {
            return;
        }
        const int frameCount = currentAnimationFrameCount();
        if (frameCount <= 0) {
            return;
        }
        int nextFrame = animationFrameIndex_ + delta;
        if (nextFrame < 0) {
            nextFrame = frameCount - 1;
        } else if (nextFrame >= frameCount) {
            nextFrame = 0;
        }
        animationPaused_ = true;
        setAnimationFrameIndex(nextFrame);
    }

    void setAnimationFrameIndex(int nextFrame) {
        animationFrameIndex_ = nextFrame;
        const mw::mech3d::MechModelDefinition frameDefinition = currentFrameDefinition();
        renderInstance_ = mw::mech3d::buildMechRenderInstance(frameDefinition, records_, batchOptions());
        batch_ = renderInstance_.batch;
        bounds_ = toViewerBounds(renderInstance_.assembly.bounds);
        updateTerrainPlacement();
        uploadMesh();
        applyPoseDemo();
    }

    const mw::battle::CombatantSnapshot* primaryBattleCombatantSnapshot() const {
        if (!options_.battleSnapshot.has_value() || options_.battleSnapshot->combatants.empty()) {
            return nullptr;
        }
        return primaryCombatantSnapshot(*options_.battleSnapshot);
    }

    bool queueBattleTorsoYawInput(int delta) {
        if (!battleInteractiveMode(options_) || !options_.battleWorld.has_value()) {
            return false;
        }
        pendingDriveTorsoYawStepDelta_ += delta;
        return true;
    }

    bool queueBattleAimPitchInput(int delta) {
        if (!battleInteractiveMode(options_) || !options_.battleWorld.has_value()) {
            return false;
        }
        pendingDriveAimPitchStepDelta_ += delta;
        return true;
    }

    bool battleJumpControlActive() const {
        if (!battleInteractiveMode(options_)) {
            return false;
        }
        const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot();
        return pendingJumpJetToggle_ ||
               (combatant != nullptr &&
                (combatant->jumpJetsEnabled || combatant->airborne || combatant->transform.y > 0.001));
    }

    bool handleBattleDriveOriginalNoopKey(WPARAM key) {
        if (!battleInteractiveMode(options_)) {
            return false;
        }
        if ((key >= '0' && key <= '9') ||
            key == VK_TAB ||
            key == VK_RETURN ||
            key == VK_SPACE ||
            key == 'C' ||
            key == 'D' ||
            key == 'E' ||
            key == 'O' ||
            key == 'R' ||
            key == 'S' ||
            key == 'T' ||
            key == 'U' ||
            key == 'W' ||
            key == 'Z') {
            return true;
        }
        return false;
    }

    void adjustBattleDriveThrottle(double delta) {
        if (!battleInteractiveMode(options_)) {
            return;
        }
        battleDriveThrottleCommand_ = std::max(-1.0, std::min(1.0, battleDriveThrottleCommand_ + delta));
        updateTitle();
    }

    void alignBattleTorsoToFeet() {
        const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot();
        if (combatant == nullptr) {
            return;
        }
        queueBattleTorsoYawInput(-combatant->torsoYawStep);
    }

    bool applyBattleTorsoYawInput(int delta) {
        if (battleInteractiveMode(options_) || !options_.battleWorld.has_value()) {
            return false;
        }

        mw::battle::BattleInputCommand command;
        command.tickIndex = options_.battleWorld->tickIndex();
        command.entityId = options_.battleWorld->playerEntityId();
        command.torsoYawStepDelta = delta;
        options_.battleWorld->enqueueInput(command);
        options_.battleWorld->tick();
        options_.battleSnapshot = options_.battleWorld->snapshot();

        if (const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot()) {
            animationElapsedMs_ = static_cast<int>(combatant->walkAnimationElapsedMs);
            if (applyBattleSnapshotAnimationId(*combatant)) {
                updateTitle();
                return true;
            }
            if (options_.animationEnabled && options_.assembled && currentAnimationFrameCount() > 0) {
                const int nextFrame = displayAnimationFrameForElapsedMs(animationElapsedMs_);
                if (nextFrame != animationFrameIndex_) {
                    setAnimationFrameIndex(nextFrame);
                    updateTitle();
                    return true;
                }
            }
        }
        applyPoseDemo();
        updateTitle();
        return true;
    }

    void updateTerrainPlacement() {
        mechDrawOffset_ = Vec3{};
        mechHeadingRadians_ = 0.0f;
        battleMechTransformActive_ = false;
        if (!options_.terrainEnabled || terrainMesh_.vertices.empty()) {
            return;
        }

        const Bounds mechBounds = currentMechLocalBounds();
        Bounds terrainBounds = terrainMeshBounds(terrainMesh_);
        if (!terrainObjectBatch_.vertices.empty()) {
            terrainBounds = mergeBounds(terrainBounds, computeBounds(terrainObjectBatch_));
        }
        float targetX = terrainBounds.center.x;
        float targetY = 0.0f;
        float targetZ = terrainBounds.center.z;
        if (const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot()) {
            targetX = static_cast<float>(combatant->transform.x);
            targetY = static_cast<float>(combatant->transform.y);
            targetZ = static_cast<float>(combatant->transform.z);
            mechHeadingRadians_ = static_cast<float>(combatant->transform.headingRadians);
            battleMechTransformActive_ = true;
        }
        const float groundY = terrainMesh_.sampleHeightNearest(targetX, targetZ);
        mechDrawOffset_ = Vec3{
            targetX - mechBounds.center.x,
            groundY + targetY - mechBounds.min.y,
            targetZ - mechBounds.center.z,
        };
        bounds_ = mergeBounds(terrainBounds, translatedBounds(mechBounds, mechDrawOffset_));
        if (battleObjectiveModelBounds_.has_value() &&
            options_.battleSnapshot.has_value() &&
            options_.battleSnapshot->objective.valid) {
            const mw::battle::BattleObjectiveState& objective = options_.battleSnapshot->objective;
            const Bounds& modelBounds = *battleObjectiveModelBounds_;
            const float objectiveX = static_cast<float>(objective.transform.x);
            const float objectiveZ = static_cast<float>(objective.transform.z);
            const float objectiveGroundY = objective.staticModel.groundAligned
                                               ? terrainMesh_.sampleHeightNearest(objectiveX, objectiveZ)
                                               : 0.0f;
            const Vec3 modelOffset{
                objectiveX - modelBounds.center.x,
                objectiveGroundY + static_cast<float>(objective.transform.y) - modelBounds.min.y,
                objectiveZ - modelBounds.center.z,
            };
            bounds_ = mergeBounds(bounds_, translatedBounds(modelBounds, modelOffset));
        }
        for (const LoadedBattleCombatantVisual& visual : battleNonPlayerCombatantVisuals_) {
            if (!options_.battleSnapshot.has_value()) {
                break;
            }
            const auto combatantIt = std::find_if(
                options_.battleSnapshot->combatants.begin(),
                options_.battleSnapshot->combatants.end(),
                [&visual](const mw::battle::CombatantSnapshot& combatant) {
                    return combatant.id == visual.entityId && !combatant.playerControlled;
                });
            if (combatantIt == options_.battleSnapshot->combatants.end()) {
                continue;
            }
            const float visualX = static_cast<float>(combatantIt->transform.x);
            const float visualZ = static_cast<float>(combatantIt->transform.z);
            const float visualGroundY = terrainMesh_.sampleHeightNearest(visualX, visualZ);
            const Vec3 visualOffset{
                visualX - visual.placementBounds.center.x,
                visualGroundY + static_cast<float>(combatantIt->transform.y) - visual.placementBounds.min.y,
                visualZ - visual.placementBounds.center.z,
            };
            bounds_ = mergeBounds(bounds_, translatedBounds(visual.placementBounds, visualOffset));
        }
    }

    Bounds currentMechLocalBounds() const {
        if (battlePlacementBounds_.has_value()) {
            return *battlePlacementBounds_;
        }
        if (options_.assembled && renderInstance_.assembly.bounds.valid) {
            return toViewerBounds(renderInstance_.assembly.bounds);
        }
        return computeBounds(batch_);
    }

    void updatePoseDemo(float dt) {
        if (!poseDemo_ || !options_.assembled || renderInstance_.assembly.components.empty()) {
            return;
        }
        poseTime_ += dt;
        applyPoseDemo();
    }

    void applyPoseDemo() {
        if (!options_.assembled || renderInstance_.assembly.components.empty()) {
            return;
        }

        mw::mech3d::MechPose pose;
        applyBattleSnapshotPose(pose);
        if (poseDemo_) {
            const mw::mech3d::ModelAssemblyComponent* target = findRenderComponent(poseDemoComponentId_);
            if (target != nullptr) {
                const float yawRadians = std::sin(poseTime_ * 2.0f) * poseDemoAmplitudeRadians_;
                mw::mech3d::setComponentPoseLocalMatrix(
                    pose,
                    target->componentId,
                    poseDemoMatrix(*target, yawRadians));
            }
        }
        mw::mech3d::updateMechRenderInstancePose(renderInstance_, pose);
    }

    void applyBattleSnapshotPose(mw::mech3d::MechPose& pose) const {
        const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot();
        if (combatant == nullptr) {
            return;
        }
        if (combatant->mechDestroyed || std::abs(combatant->torsoYawRadians) <= 0.0001) {
            return;
        }
        if (hasAssemblyFrameAnimation()) {
            return;
        }

        mw::mech3d::applyPlayerTorsoYawPose(
            pose,
            renderInstance_.assembly,
            static_cast<float>(combatant->torsoYawRadians));
    }

    mw::mech3d::Mat4f poseDemoMatrix(const mw::mech3d::ModelAssemblyComponent& component, float radians) const {
        const mw::mech3d::Vec3f pivot = poseDemoPivot(component);
        switch (poseDemoAxis_) {
        case PoseDemoAxis::X:
            return mw::mech3d::rotateAroundPivotXMatrix(pivot, radians);
        case PoseDemoAxis::Y:
            return mw::mech3d::rotateAroundPivotYMatrix(pivot, radians);
        case PoseDemoAxis::Z:
            return mw::mech3d::rotateAroundPivotZMatrix(pivot, radians);
        }
        return mw::mech3d::identityMatrix();
    }

    mw::mech3d::Vec3f poseDemoPivot(const mw::mech3d::ModelAssemblyComponent& component) const {
        switch (posePivotMode_) {
        case PosePivotMode::Definition:
            return component.localPivot;
        case PosePivotMode::BoundsCenter:
            return component.localBounds.center;
        case PosePivotMode::BoundsBottom:
            return mw::mech3d::Vec3f{component.localBounds.center.x, component.localBounds.center.y, component.localBounds.min.z};
        case PosePivotMode::BoundsTop:
            return mw::mech3d::Vec3f{component.localBounds.center.x, component.localBounds.center.y, component.localBounds.max.z};
        }
        return component.localPivot;
    }

    void cyclePoseDemoTarget() {
        if (!options_.assembled || renderInstance_.assembly.components.empty()) {
            return;
        }
        const auto it = std::find_if(
            renderInstance_.assembly.components.begin(),
            renderInstance_.assembly.components.end(),
            [this](const mw::mech3d::ModelAssemblyComponent& component) {
                return component.componentId == poseDemoComponentId_;
            });
        if (it == renderInstance_.assembly.components.end() || std::next(it) == renderInstance_.assembly.components.end()) {
            poseDemoComponentId_ = renderInstance_.assembly.components.front().componentId;
        } else {
            poseDemoComponentId_ = std::next(it)->componentId;
        }
        if (const mw::mech3d::ModelAssemblyComponent* target = findRenderComponent(poseDemoComponentId_)) {
            poseDemoAxis_ = toPoseDemoAxis(target->defaultPoseAxis, poseDemoAxis_);
        }
        poseTime_ = 0.0f;
        applyPoseDemo();
        updateTitle();
    }

    void cyclePosePivotMode() {
        switch (posePivotMode_) {
        case PosePivotMode::Definition:
            posePivotMode_ = PosePivotMode::BoundsCenter;
            break;
        case PosePivotMode::BoundsCenter:
            posePivotMode_ = PosePivotMode::BoundsBottom;
            break;
        case PosePivotMode::BoundsBottom:
            posePivotMode_ = PosePivotMode::BoundsTop;
            break;
        case PosePivotMode::BoundsTop:
            posePivotMode_ = PosePivotMode::Definition;
            break;
        }
        poseTime_ = 0.0f;
        applyPoseDemo();
        updateTitle();
    }

    void cyclePoseDemoAxis() {
        switch (poseDemoAxis_) {
        case PoseDemoAxis::X:
            poseDemoAxis_ = PoseDemoAxis::Y;
            break;
        case PoseDemoAxis::Y:
            poseDemoAxis_ = PoseDemoAxis::Z;
            break;
        case PoseDemoAxis::Z:
            poseDemoAxis_ = PoseDemoAxis::X;
            break;
        }
        poseTime_ = 0.0f;
        applyPoseDemo();
        updateTitle();
    }

    void cyclePoseDemoAmplitude() {
        if (poseDemoAmplitudeRadians_ > 0.35f) {
            poseDemoAmplitudeRadians_ = 0.25f;
        } else if (poseDemoAmplitudeRadians_ > 0.15f) {
            poseDemoAmplitudeRadians_ = 0.12f;
        } else {
            poseDemoAmplitudeRadians_ = 0.45f;
        }
        poseTime_ = 0.0f;
        applyPoseDemo();
        updateTitle();
    }

    int poseDemoAmplitudeDegrees() const {
        return static_cast<int>(poseDemoAmplitudeRadians_ * 180.0f / 3.14159265358979323846f + 0.5f);
    }

    void toggleHiddenComponent(int componentId) {
        const auto it = std::find(options_.hiddenComponentIds.begin(), options_.hiddenComponentIds.end(), componentId);
        if (it == options_.hiddenComponentIds.end()) {
            options_.hiddenComponentIds.push_back(componentId);
        } else {
            options_.hiddenComponentIds.erase(it);
        }
        updateTitle();
    }

    void cycleShadeMode() {
        if (options_.shadeMode == "stored") {
            options_.shadeMode = "lit";
        } else if (options_.shadeMode == "lit") {
            options_.shadeMode = "ega";
        } else {
            options_.shadeMode = "stored";
        }
        loadTerrainResources();
        rebuildAndUpload(false);
    }

    void toggleWire() {
        options_.wire = !options_.wire;
        loadTerrainResources();
        rebuildAndUpload(false);
    }

    void toggleTerrainColorMode() {
        static const std::array<std::string, 5> modes{{"debug", "flat", "desert", "green", "snow"}};
        const auto found = std::find(modes.begin(), modes.end(), options_.terrainColorMode);
        const size_t next = found == modes.end() ? 0u : (static_cast<size_t>(found - modes.begin()) + 1u) % modes.size();
        options_.terrainColorMode = modes[next];
        loadTerrainResources();
        rebuildAndUpload(false);
        updateTitle();
    }

    void toggleTerrainSkyMode() {
        static const std::array<std::string, 4> modes{{"auto", "black", "blue", "white"}};
        const auto found = std::find(modes.begin(), modes.end(), options_.terrainSkyMode);
        const size_t next = found == modes.end() ? 0u : (static_cast<size_t>(found - modes.begin()) + 1u) % modes.size();
        options_.terrainSkyMode = modes[next];
        updateTitle();
    }

    void pollInput(float dt) {
        updateBattleDriveSimulation(dt);
        if (battleInteractiveMode(options_)) {
            return;
        }

        const float turn = 90.0f * dt;
        if ((GetAsyncKeyState(VK_LEFT) & 0x8000) != 0) {
            yaw_ -= turn;
        }
        if ((GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0) {
            yaw_ += turn;
        }
        if ((GetAsyncKeyState(VK_UP) & 0x8000) != 0) {
            pitch_ = std::max(-89.0f, pitch_ - turn);
        }
        if ((GetAsyncKeyState(VK_DOWN) & 0x8000) != 0) {
            pitch_ = std::min(89.0f, pitch_ + turn);
        }
        if ((GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) != 0 || (GetAsyncKeyState(VK_ADD) & 0x8000) != 0) {
            distance_ = std::max(bounds_.radius * 0.8f, distance_ * (1.0f - dt));
        }
        if ((GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0 || (GetAsyncKeyState(VK_SUBTRACT) & 0x8000) != 0) {
            distance_ = std::min(bounds_.radius * 12.0f, distance_ * (1.0f + dt));
        }
    }

    void updateBattleDriveSimulation(float dt) {
        if (!battleInteractiveMode(options_) || !options_.battleWorld.has_value() || animationPaused_) {
            return;
        }

        battleDriveAccumulator_ += dt;
        const double fixedTick = options_.battleWorld->fixedTickSeconds();
        while (battleDriveAccumulator_ + 0.000001 >= fixedTick) {
            const bool left = (GetAsyncKeyState(VK_LEFT) & 0x8000) != 0;
            const bool right = (GetAsyncKeyState(VK_RIGHT) & 0x8000) != 0;
            const bool jumpControl = battleJumpControlActive();
            const bool jumpForwardThrust = jumpControl && (GetAsyncKeyState(VK_UP) & 0x8000) != 0;
            const bool jumpVerticalThrust = jumpControl && (GetAsyncKeyState(VK_DOWN) & 0x8000) != 0;

            mw::battle::BattleInputCommand command;
            command.tickIndex = options_.battleWorld->tickIndex();
            command.entityId = options_.battleWorld->playerEntityId();
            command.throttle = battleDriveThrottleCommand_;
            command.turn = (left ? 1.0 : 0.0) + (right ? -1.0 : 0.0);
            command.torsoYawStepDelta = pendingDriveTorsoYawStepDelta_;
            pendingDriveTorsoYawStepDelta_ = 0;
            command.aimPitchStepDelta = pendingDriveAimPitchStepDelta_;
            pendingDriveAimPitchStepDelta_ = 0;
            command.jumpJetToggle = pendingJumpJetToggle_;
            pendingJumpJetToggle_ = false;
            command.jumpForwardThrust = jumpForwardThrust;
            command.jumpVerticalThrust = jumpVerticalThrust;
            options_.battleWorld->enqueueInput(command);
            options_.battleWorld->tick();
            options_.battleSnapshot = options_.battleWorld->snapshot();
            applyBattleSnapshotAnimationAndPlacement();
            battleDriveAccumulator_ -= fixedTick;
        }
    }

    void applyBattleSnapshotAnimationAndPlacement() {
        const mw::battle::CombatantSnapshot* combatant = primaryBattleCombatantSnapshot();
        if (combatant == nullptr) {
            return;
        }

        animationElapsedMs_ = combatant->walkAnimationElapsedMs > static_cast<uint64_t>(std::numeric_limits<int>::max())
                                  ? std::numeric_limits<int>::max()
                                  : static_cast<int>(combatant->walkAnimationElapsedMs);
        if (applyBattleSnapshotAnimationId(*combatant)) {
            updateTitle();
            return;
        }
        if (options_.animationEnabled && options_.assembled && currentAnimationFrameCount() > 0) {
            const int nextFrame = displayAnimationFrameForElapsedMs(animationElapsedMs_);
            if (nextFrame != animationFrameIndex_) {
                setAnimationFrameIndex(nextFrame);
                updateTitle();
                return;
            }
        }
        updateTerrainPlacement();
        applyPoseDemo();
        updateTitle();
    }

    ScreenRect virtualScreenRect(int x, int y, int width, int height) const {
        const double sx = std::max(1, width_) / static_cast<double>(kVirtualScreenWidth);
        const double sy = std::max(1, height_) / static_cast<double>(kVirtualScreenHeight);
        if (width <= 0 || height <= 0) {
            return ScreenRect{
                static_cast<int>(std::lround(static_cast<double>(x) * sx)),
                static_cast<int>(std::lround(static_cast<double>(y) * sy)),
                0,
                0,
            };
        }
        return ScreenRect{
            static_cast<int>(std::lround(static_cast<double>(x) * sx)),
            static_cast<int>(std::lround(static_cast<double>(y) * sy)),
            std::max(1, static_cast<int>(std::lround(static_cast<double>(width) * sx))),
            std::max(1, static_cast<int>(std::lround(static_cast<double>(height) * sy))),
        };
    }

    ScreenRect virtualScreenRect(ScreenRect rect) const {
        return virtualScreenRect(rect.x, rect.y, rect.width, rect.height);
    }

    ScreenRect battleWorldViewportRect() const {
        if (!options_.battleCockpit || options_.battleCockpitExternalCamera) {
            return ScreenRect{0, 0, std::max(1, width_), std::max(1, height_)};
        }
        return virtualScreenRect(cockpitLayoutForOptions(options_).viewport);
    }

    void renderCockpitBackdrop() {
        if (!cockpitBackdropReady_ || cockpitTexture_ == 0) {
            return;
        }

        glViewport(0, 0, width_, height_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_POLYGON_STIPPLE);
        glDisable(GL_SCISSOR_TEST);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(std::max(1, width_)), static_cast<double>(std::max(1, height_)), 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, cockpitTexture_);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2i(0, 0);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2i(width_, 0);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2i(width_, height_);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2i(0, height_);
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    const CockpitSpriteTexture* cockpitStrutTexture(int index) const {
        const auto found = std::find_if(
            cockpitStrutTextures_.begin(),
            cockpitStrutTextures_.end(),
            [index](const CockpitSpriteTexture& texture) {
                return texture.index == index;
            });
        return found == cockpitStrutTextures_.end() ? nullptr : &*found;
    }

    const CockpitSpriteTexture* cockpitWidgetTexture(int index) const {
        const auto found = std::find_if(
            cockpitWidgetTextures_.begin(),
            cockpitWidgetTextures_.end(),
            [index](const CockpitSpriteTexture& texture) {
                return texture.index == index;
            });
        return found == cockpitWidgetTextures_.end() ? nullptr : &*found;
    }

    const CockpitSpriteTexture* cockpitHudNumberTexture(int index) const {
        const auto found = std::find_if(
            cockpitHudNumberTextures_.begin(),
            cockpitHudNumberTextures_.end(),
            [index](const CockpitSpriteTexture& texture) {
                return texture.index == index;
            });
        return found == cockpitHudNumberTextures_.end() ? nullptr : &*found;
    }

    void drawCockpitSprite(const CockpitSpriteTexture& sprite, int virtualX, int virtualY) {
        const ScreenRect rect = virtualScreenRect(virtualX, virtualY, sprite.width, sprite.height);
        if (rect.width <= 0 || rect.height <= 0 || sprite.texture == 0) {
            return;
        }
        glBindTexture(GL_TEXTURE_2D, sprite.texture);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f);
        glVertex2i(rect.x, rect.y);
        glTexCoord2f(1.0f, 0.0f);
        glVertex2i(rect.x + rect.width, rect.y);
        glTexCoord2f(1.0f, 1.0f);
        glVertex2i(rect.x + rect.width, rect.y + rect.height);
        glTexCoord2f(0.0f, 1.0f);
        glVertex2i(rect.x, rect.y + rect.height);
        glEnd();
    }

    void drawCockpitSpriteTinted(
        const CockpitSpriteTexture& sprite,
        int virtualX,
        int virtualY,
        const std::array<float, 3>& rgb) {
        glColor4f(rgb[0], rgb[1], rgb[2], 1.0f);
        drawCockpitSprite(sprite, virtualX, virtualY);
    }

    void drawCockpitFontGlyph(
        char ch,
        int virtualX,
        int virtualY,
        const std::array<float, 3>& rgb) {
        if (!cockpitHudFont_.has_value()) {
            return;
        }
        const CockpitFont& font = *cockpitHudFont_;
        const int code = static_cast<unsigned char>(ch);
        const int glyphIndex = code - font.firstCode;
        if (glyphIndex < 0 || glyphIndex >= font.glyphCount) {
            return;
        }
        const size_t glyphOffset = static_cast<size_t>(glyphIndex) * static_cast<size_t>(font.height);
        for (int y = 0; y < font.height; ++y) {
            const uint8_t row = font.rows[glyphOffset + static_cast<size_t>(y)];
            for (int x = 0; x < font.width; ++x) {
                const int bit = 7 - x;
                if (((row >> bit) & 1u) != 0u) {
                    fillScreenRect(virtualScreenRect(virtualX + x, virtualY + y, 1, 1), rgb);
                }
            }
        }
    }

    void drawCockpitFontGlyph(char ch, int virtualX, int virtualY) {
        drawCockpitFontGlyph(ch, virtualX, virtualY, {0.0f, 0.0f, 0.0f});
    }

    void drawCockpitFontText(
        const std::string& text,
        int virtualX,
        int virtualY,
        const std::array<float, 3>& rgb) {
        if (!cockpitHudFont_.has_value()) {
            return;
        }
        const int advance = cockpitHudFont_->width;
        int x = virtualX;
        for (char ch : text) {
            drawCockpitFontGlyph(ch, x, virtualY, rgb);
            x += advance;
        }
    }

    void renderCockpitStruts() {
        const CockpitFamily family = resolveCockpitFamily(options_);
        if (!cockpitStrutsReady_ || cockpitStrutTextures_.empty() || !cockpitFamilyUsesSharedHud(family)) {
            return;
        }

        const std::vector<CockpitStrutPlacement> placements = cockpitStrutPlacements(family);

        glViewport(0, 0, width_, height_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_POLYGON_STIPPLE);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(std::max(1, width_)), static_cast<double>(std::max(1, height_)), 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        for (const CockpitStrutPlacement& placement : placements) {
            if (const CockpitSpriteTexture* sprite = cockpitStrutTexture(placement.spriteIndex)) {
                drawCockpitSprite(*sprite, placement.x, placement.y);
            }
        }
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    void renderCockpitSystemIndicators() {
        if (!cockpitWidgetsReady_ || !cockpitHudFont_.has_value() || !cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
            return;
        }
        const CockpitSpriteTexture* okTile = cockpitWidgetTexture(0);
        if (okTile == nullptr) {
            return;
        }

        struct Indicator {
            int x = 0;
            int y = 0;
            char label = ' ';
            bool damaged = false;
        };
        const CockpitSystemIndicatorDamage damage = cockpitSystemIndicatorDamageFromSnapshot(options_.battleSnapshot);
        const std::array<Indicator, 4> indicators{{
            Indicator{136, 107, 'S', damage.sensors},
            Indicator{146, 107, 'G', damage.gyros},
            Indicator{156, 107, 'E', damage.engines},
            Indicator{166, 107, 'L', damage.lifeSupport},
        }};
        constexpr std::array<float, 3> kDamagedRgb{1.0f, 85.0f / 255.0f, 85.0f / 255.0f};

        glViewport(0, 0, width_, height_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_POLYGON_STIPPLE);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(std::max(1, width_)), static_cast<double>(std::max(1, height_)), 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        for (const Indicator& indicator : indicators) {
            fillScreenRect(
                virtualScreenRect(indicator.x, indicator.y, okTile->width, okTile->height),
                indicator.damaged ? kDamagedRgb : okTile->fillRgb);
        }
        glDisable(GL_TEXTURE_2D);

        for (const Indicator& indicator : indicators) {
            drawCockpitFontGlyph(indicator.label, indicator.x + 1, indicator.y + 1);
        }

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    void render() {
        wglMakeCurrent(hdc_, hrc_);
        glViewport(0, 0, width_, height_);
        const TerrainPalette& palette = terrainPaletteById(options_.terrainColorMode);
        const std::array<float, 3> skyRgb = terrainSkyColor(options_.terrainSkyMode, palette);
        glClearColor(skyRgb[0], skyRgb[1], skyRgb[2], 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        if (battleCommandMapActive(options_)) {
            renderCommandMapScreen();
            checkGl("render_command_map");
            if (screenshotRequests_ > 0) {
                saveScreenshot();
                --screenshotRequests_;
            }
            SwapBuffers(hdc_);
            return;
        }
        const bool cockpitCompositorVisible = options_.battleCockpit && !options_.battleCockpitExternalCamera;
        if (cockpitCompositorVisible) {
            renderCockpitBackdrop();
        }

        const ScreenRect worldViewport = battleWorldViewportRect();
        glViewport(
            worldViewport.x,
            std::max(0, height_ - worldViewport.y - worldViewport.height),
            worldViewport.width,
            worldViewport.height);
        if (cockpitCompositorVisible) {
            const GLint viewportY = std::max(0, height_ - worldViewport.y - worldViewport.height);
            glEnable(GL_SCISSOR_TEST);
            glScissor(worldViewport.x, viewportY, worldViewport.width, worldViewport.height);
            glClearColor(skyRgb[0], skyRgb[1], skyRgb[2], 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glDisable(GL_SCISSOR_TEST);
        }
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        const double aspect = std::max(1, worldViewport.width) / static_cast<double>(std::max(1, worldViewport.height));
        const double nearZ = battleInteractiveMode(options_) ? 25.0 : std::max(1.0, static_cast<double>(bounds_.radius) * 0.05);
        const double farZ = std::max(1000.0, static_cast<double>(bounds_.radius) * (battleInteractiveMode(options_) ? 30.0 : 20.0));
        if (options_.perspective) {
            const double top = std::tan(45.0 * 3.14159265358979323846 / 360.0) * nearZ /
                               battleViewportProjectionZoom();
            const double right = top * aspect;
            glFrustum(-right, right, -top, top, nearZ, farZ);
        } else {
            const double resetDistance = std::max(1.0, static_cast<double>(bounds_.radius) * 3.2);
            const double zoomScale = std::max(0.25, static_cast<double>(distance_) / resetDistance);
            const double halfY = std::max(1.0, static_cast<double>(bounds_.radius) * 1.35 * zoomScale);
            const double halfX = halfY * aspect;
            glOrtho(-halfX, halfX, -halfY, halfY, nearZ, farZ);
        }

        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        if (options_.battleCockpit) {
            if (options_.battleCockpitExternalCamera) {
                applyFollowCamera();
            } else {
                applyCockpitCamera();
            }
        } else if (options_.battleDrive && battleFollowCamera_) {
            applyFollowCamera();
        } else {
            applyOrbitCamera();
        }

        renderTerrain();
        renderTerrainObjects();
        renderBattleObjectiveModel();
        renderBattleNonPlayerCombatants();

        gl_.glBindBuffer(GL_ARRAY_BUFFER, mesh_.vbo);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 6 * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<const void*>(0));
        glColorPointer(3, GL_FLOAT, 6 * static_cast<GLsizei>(sizeof(float)), reinterpret_cast<const void*>(3 * sizeof(float)));

        const bool hidePrimaryMech = options_.battleCockpit && !options_.battleCockpitExternalCamera;
        if (!hidePrimaryMech) {
            glPushMatrix();
            applyMechWorldTransform();
            if (options_.assembled && renderInstance_.usesComponentMatrices) {
                renderAssembledMesh();
            } else {
                if (!options_.wire && mesh_.triangleIndexCount > 0) {
                    gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_.triEbo);
                    glDrawElements(GL_TRIANGLES, mesh_.triangleIndexCount, GL_UNSIGNED_INT, reinterpret_cast<const void*>(0));
                }
                if (mesh_.lineIndexCount > 0) {
                    glLineWidth(options_.wire ? 1.5f : 1.0f);
                    gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_.lineEbo);
                    glDrawElements(GL_LINES, mesh_.lineIndexCount, GL_UNSIGNED_INT, reinterpret_cast<const void*>(0));
                }
            }
            glPopMatrix();
        }

        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        if (!hidePrimaryMech) {
            glPushMatrix();
            applyMechWorldTransform();
            renderComponentDebug();
            glPopMatrix();
        }
        if (cockpitCompositorVisible) {
            glViewport(0, 0, width_, height_);
            renderCockpitStruts();
            renderCockpitSystemIndicators();
            renderCockpitCompositor();
        }
        if (!options_.battleCockpitExternalCamera) {
            renderOverlay();
        }
        checkGl("render");
        if (screenshotRequests_ > 0) {
            saveScreenshot();
            --screenshotRequests_;
        }
        SwapBuffers(hdc_);
    }

    void fillScreenRect(const ScreenRect& rect, std::array<float, 3> rgb) {
        if (rect.width <= 0 || rect.height <= 0) {
            return;
        }
        glColor3f(rgb[0], rgb[1], rgb[2]);
        glBegin(GL_QUADS);
        glVertex2i(rect.x, rect.y);
        glVertex2i(rect.x + rect.width, rect.y);
        glVertex2i(rect.x + rect.width, rect.y + rect.height);
        glVertex2i(rect.x, rect.y + rect.height);
        glEnd();
    }

    void strokeScreenRect(const ScreenRect& rect, std::array<float, 3> rgb) {
        if (rect.width <= 0 || rect.height <= 0) {
            return;
        }
        glColor3f(rgb[0], rgb[1], rgb[2]);
        glBegin(GL_LINE_LOOP);
        glVertex2i(rect.x, rect.y);
        glVertex2i(rect.x + rect.width, rect.y);
        glVertex2i(rect.x + rect.width, rect.y + rect.height);
        glVertex2i(rect.x, rect.y + rect.height);
        glEnd();
    }

    void fillScreenMeter(
        const ScreenRect& rect,
        double normalized,
        std::array<float, 3> fillRgb,
        std::array<float, 3> backRgb) {
        fillScreenRect(rect, backRgb);
        ScreenRect fill = rect;
        fill.width = std::max(0, static_cast<int>(std::lround(static_cast<double>(rect.width) * std::clamp(normalized, 0.0, 1.0))));
        if (fill.width > 0) {
            fillScreenRect(fill, fillRgb);
        }
    }

    void drawVerticalTick(int x, int y0, int y1, std::array<float, 3> rgb) {
        glColor3f(rgb[0], rgb[1], rgb[2]);
        glBegin(GL_LINES);
        glVertex2i(x, y0);
        glVertex2i(x, y1);
        glEnd();
    }

    const CockpitHudColor& currentCockpitHudColor() const {
        const std::vector<CockpitHudColor>& colors = cockpitHudColorCycle();
        return colors[cockpitHudColorIndex_ % colors.size()];
    }

    void cycleCockpitHudColor() {
        const std::vector<CockpitHudColor>& colors = cockpitHudColorCycle();
        cockpitHudColorIndex_ = (cockpitHudColorIndex_ + 1u) % colors.size();
        updateTitle();
    }

    void cycleBattleCockpitZoom() {
        ++battleCockpitZoomLevel_;
        if (battleCockpitZoomLevel_ > kBattleCockpitMaxZoomLevel) {
            battleCockpitZoomLevel_ = kBattleCockpitMinZoomLevel;
        }
        updateTitle();
    }

    void renderCockpitCompass(const CockpitHudState& hud) {
        if (!cockpitHudNumbersReady_ || !cockpitWidgetsReady_ || !cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
            return;
        }

        const CockpitLayout& layout = cockpitLayoutForOptions(options_);
        const ScreenRect viewport = layout.viewport;
        const CockpitHudColor& hudColor = currentCockpitHudColor();
        constexpr double kPixelsPerDegree = 30.0 / 30.0;
        constexpr int kCompassHalfWidth = 40;
        const int centerX = viewport.x + viewport.width / 2;
        const int topMarkerY = viewport.y + layout.compassTopMarkerOffsetY;
        const int numberY = topMarkerY + 4;
        const int baselineY = numberY + 13;
        const int baselineLeft = centerX - kCompassHalfWidth;
        const int baselineRight = centerX + kCompassHalfWidth;

        glDisable(GL_TEXTURE_2D);
        for (int tick = 0; tick < 72; ++tick) {
            const int tickDegrees = tick * 5;
            const double delta = signedCompassDeltaDegrees(static_cast<double>(tickDegrees), hud.bodyHeadingDegrees);
            if (std::abs(delta) > 40.0) {
                continue;
            }
            const int x = centerX + static_cast<int>(std::lround(delta * kPixelsPerDegree));
            if (x < baselineLeft || x > baselineRight) {
                continue;
            }
            int tickHeight = 1;
            if ((tickDegrees % 30) == 0) {
                tickHeight = 6;
            } else if ((tickDegrees % 10) == 0) {
                tickHeight = 3;
            }
            fillScreenRect(virtualScreenRect(x, baselineY - tickHeight, 1, tickHeight), hudColor.rgb);
        }
        fillScreenRect(virtualScreenRect(baselineLeft, baselineY, kCompassHalfWidth * 2 + 1, 1), hudColor.rgb);

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int index = 0; index < 12; ++index) {
            const double angle = static_cast<double>(index * 30);
            const double delta = signedCompassDeltaDegrees(angle, hud.bodyHeadingDegrees);
            if (std::abs(delta) > 43.0) {
                continue;
            }
            if (const CockpitSpriteTexture* digits = cockpitHudNumberTexture(index)) {
                const int x = centerX + static_cast<int>(std::lround(delta * kPixelsPerDegree)) - digits->width / 2;
                drawCockpitSpriteTinted(*digits, x, numberY, hudColor.rgb);
            }
        }

        if (const CockpitSpriteTexture* topMarker = cockpitWidgetTexture(3)) {
            const int x = centerX - static_cast<int>(std::lround(hud.torsoDegrees * kPixelsPerDegree)) - topMarker->width / 2;
            drawCockpitSpriteTinted(*topMarker, x, topMarkerY, hudColor.rgb);
        }
        if (const CockpitSpriteTexture* bottomMarker = cockpitWidgetTexture(2)) {
            drawCockpitSpriteTinted(*bottomMarker, centerX - bottomMarker->width / 2, baselineY + 2, hudColor.rgb);
        }
        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glDisable(GL_BLEND);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    }

    void renderLightCockpitCrosshair(const CockpitHudState& hud) {
        if (!cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
            return;
        }
        if (battleCockpitZoomLevel_ != kBattleCockpitMinZoomLevel) {
            return;
        }

        const CockpitLayout& layout = cockpitLayoutForOptions(options_);
        const int centerX = layout.viewport.x + layout.viewport.width / 2;
        const int centerY = layout.crosshairCenterY - hud.aimPitchStep * kLightCockpitCrosshairStepPixels;
        constexpr int innerOffset = (kLightCockpitCrosshairCenterGap + 1) / 2;
        constexpr int outerOffset = innerOffset + kLightCockpitCrosshairSegmentLength - 1;
        const CockpitHudColor& hudColor = currentCockpitHudColor();

        glDisable(GL_TEXTURE_2D);
        fillScreenRect(
            virtualScreenRect(
                centerX - outerOffset,
                centerY,
                kLightCockpitCrosshairSegmentLength,
                1),
            hudColor.rgb);
        fillScreenRect(
            virtualScreenRect(
                centerX + innerOffset,
                centerY,
                kLightCockpitCrosshairSegmentLength,
                1),
            hudColor.rgb);
        fillScreenRect(
            virtualScreenRect(
                centerX,
                centerY - outerOffset,
                1,
                kLightCockpitCrosshairSegmentLength),
            hudColor.rgb);
        fillScreenRect(
            virtualScreenRect(
                centerX,
                centerY + innerOffset,
                1,
                kLightCockpitCrosshairSegmentLength),
            hudColor.rgb);
    }

    void renderLightCockpitSpeedGauge(const CockpitHudState& hud) {
        if (!cockpitBackdropReady_ || !cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
            return;
        }

        const CockpitFamily family = resolveCockpitFamily(options_);
        const int gaugeX = family == CockpitFamily::Heavy ? kHeavyCockpitSpeedGaugeX : kLightCockpitSpeedGaugeX;
        const int gaugeY = family == CockpitFamily::Heavy ? kHeavyCockpitSpeedGaugeY : kLightCockpitSpeedGaugeY;
        const LightCockpitSpeedGaugeState gauge = lightCockpitSpeedGaugeState(hud);
        constexpr std::array<float, 3> kZeroRgb{1.0f, 1.0f, 85.0f / 255.0f};
        constexpr std::array<float, 3> kZeroHighlightRgb{1.0f, 1.0f, 1.0f};
        constexpr std::array<float, 3> kForwardRgb{0.0f, 0.85f, 0.0f};
        constexpr std::array<float, 3> kForwardHighlightRgb{85.0f / 255.0f, 1.0f, 85.0f / 255.0f};
        constexpr std::array<float, 3> kReverseRgb{0.67f, 0.0f, 0.0f};
        constexpr std::array<float, 3> kReverseHighlightRgb{1.0f, 85.0f / 255.0f, 85.0f / 255.0f};

        glDisable(GL_TEXTURE_2D);
        const auto drawBar = [&](int barIndex, std::array<float, 3> bodyRgb, std::array<float, 3> highlightRgb) {
            const int x = gaugeX + barIndex * kLightCockpitSpeedGaugeBarStride;
            fillScreenRect(virtualScreenRect(x, gaugeY, 1, 1), highlightRgb);
            fillScreenRect(
                virtualScreenRect(
                    x,
                    gaugeY + 1,
                    1,
                    kLightCockpitSpeedGaugeBarHeight - 1),
                bodyRgb);
        };
        for (int i = 1; i <= gauge.reverseBars; ++i) {
            drawBar(kLightCockpitSpeedGaugeZeroIndex - i, kReverseRgb, kReverseHighlightRgb);
        }
        for (int i = 1; i <= gauge.forwardBars; ++i) {
            drawBar(kLightCockpitSpeedGaugeZeroIndex + i, kForwardRgb, kForwardHighlightRgb);
        }
        drawBar(kLightCockpitSpeedGaugeZeroIndex, kZeroRgb, kZeroHighlightRgb);
    }

    void renderCockpitJumpGauge(const CockpitHudState& hud) {
        const CockpitFamily family = resolveCockpitFamily(options_);
        if (!cockpitBackdropReady_ || !cockpitFamilyUsesSharedHud(family) || !cockpitFamilyShowsJumpGauge(family)) {
            return;
        }

        const ScreenRect gaugeRect = cockpitJumpGaugeRect(family);
        const ScreenRect readyLightRect = cockpitJumpReadyLightRect(family);
        const int gaugeX = gaugeRect.x;
        const int gaugeY = gaugeRect.y;
        constexpr int kGaugeWidth = kLightCockpitJumpGaugeFuelWidth + kLightCockpitJumpGaugeHighlightWidth;
        constexpr std::array<float, 3> kFuelRgb{0.28f, 0.32f, 1.0f};
        constexpr std::array<float, 3> kFuelHighlightRgb{0.25f, 1.0f, 1.0f};
        constexpr std::array<float, 3> kThresholdDarkRgb{0.52f, 0.0f, 0.0f};
        constexpr std::array<float, 3> kThresholdLightRgb{1.0f, 0.12f, 0.12f};
        constexpr std::array<float, 3> kReadyRedDarkRgb{0.65f, 0.0f, 0.0f};
        constexpr std::array<float, 3> kReadyRedHighlightRgb{1.0f, 0.22f, 0.22f};

        glDisable(GL_TEXTURE_2D);

        const double fuelCapacity = std::max(0.001, hud.jumpMaxFuel);
        const int fillHeight = std::clamp(
            static_cast<int>(std::lround(std::clamp(hud.jumpFuel / fuelCapacity, 0.0, 1.0) * kLightCockpitJumpGaugeHeight)),
            0,
            kLightCockpitJumpGaugeHeight);
        if (fillHeight > 0) {
            fillScreenRect(
                virtualScreenRect(
                    gaugeX,
                    gaugeY + kLightCockpitJumpGaugeHeight - fillHeight,
                    kLightCockpitJumpGaugeFuelWidth,
                    fillHeight),
                kFuelRgb);
            fillScreenRect(
                virtualScreenRect(
                    gaugeX + kLightCockpitJumpGaugeFuelWidth,
                    gaugeY + kLightCockpitJumpGaugeHeight - fillHeight,
                    kLightCockpitJumpGaugeHighlightWidth,
                    fillHeight),
                kFuelHighlightRgb);
        }

        const int thresholdY =
            gaugeY + kLightCockpitJumpGaugeHeight -
            std::clamp(
                static_cast<int>(std::lround(std::clamp(hud.jumpActivationFuel / fuelCapacity, 0.0, 1.0) * kLightCockpitJumpGaugeHeight)),
                0,
                kLightCockpitJumpGaugeHeight);
        fillScreenRect(
            virtualScreenRect(
                gaugeX - 1,
                thresholdY,
                kGaugeWidth + 1,
                1),
            kThresholdDarkRgb);
        fillScreenRect(
            virtualScreenRect(
                gaugeX + kGaugeWidth,
                thresholdY,
                1,
                1),
            kThresholdLightRgb);
        if (!hud.jumpJetReady) {
            const int lampX = readyLightRect.x;
            const int lampY = readyLightRect.y;
            for (int y = 0; y < 3; ++y) {
                for (int x = 0; x < 3; ++x) {
                    const bool highlight = (x == 2 && y <= 1) || (x == 1 && y == 0);
                    fillScreenRect(
                        virtualScreenRect(lampX + x, lampY + y, 1, 1),
                        highlight ? kReadyRedHighlightRgb : kReadyRedDarkRgb);
                }
            }
        }
    }

    void renderLightCockpitMinimap() {
        if (!cockpitBackdropReady_ || !cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
            return;
        }

        const mw::battle::CombatantSnapshot* player = primaryBattleCombatantSnapshot();
        if (player == nullptr) {
            return;
        }

        constexpr std::array<float, 3> kMapBackRgb{0.0f, 0.58f, 0.0f};
        constexpr std::array<float, 3> kPlayerRgb{0.0f, 0.0f, 0.0f};
        constexpr std::array<float, 3> kEnemyRgb{0.86f, 0.0f, 0.0f};
        constexpr std::array<float, 3> kDisabledEnemyRgb{0.42f, 0.0f, 0.0f};
        constexpr std::array<float, 3> kTargetRgb{0.0f, 0.86f, 0.86f};

        glDisable(GL_TEXTURE_2D);
        fillScreenRect(virtualScreenRect(kLightCockpitMinimapRect), kMapBackRgb);

        const int centerX = kLightCockpitMinimapRect.x + kLightCockpitMinimapRect.width / 2;
        const int centerY = kLightCockpitMinimapRect.y + kLightCockpitMinimapRect.height / 2;
        const double worldUnitsPerMapPixel = std::max(1.0f, options_.terrainCellSize);
        const auto inMap = [](int x, int y) {
            return x >= kLightCockpitMinimapRect.x &&
                   x < kLightCockpitMinimapRect.x + kLightCockpitMinimapRect.width &&
                   y >= kLightCockpitMinimapRect.y &&
                   y < kLightCockpitMinimapRect.y + kLightCockpitMinimapRect.height;
        };
        const auto mapPointF = [&](double worldX, double worldZ) {
            const double x = static_cast<double>(centerX) +
                             (worldX - player->transform.x) / worldUnitsPerMapPixel;
            const double y = static_cast<double>(centerY) +
                             (worldZ - player->transform.z) / worldUnitsPerMapPixel;
            return std::pair<double, double>{x, y};
        };
        const auto mapPoint = [&](double worldX, double worldZ) {
            const auto [fx, fy] = mapPointF(worldX, worldZ);
            const int x = static_cast<int>(std::lround(fx));
            const int y = static_cast<int>(std::lround(fy));
            return std::pair<int, int>{x, y};
        };
        const auto drawMapPixel = [&](int x, int y, std::array<float, 3> rgb) {
            if (inMap(x, y)) {
                fillScreenRect(virtualScreenRect(x, y, 1, 1), rgb);
            }
        };

        if (!terrainMesh_.vertices.empty()) {
            for (const mw::legacy3d::TerrainMeshVertex& vertex : terrainMesh_.vertices) {
                if (vertex.rawValue == 0u) {
                    continue;
                }
                const std::array<float, 3> rgb = cockpitMinimapGroundPatchRgb(vertex.colorBand);
                const auto [x, y] = mapPoint(vertex.x, vertex.z);
                drawMapPixel(x, y, rgb);
            }
        }

        if (options_.battleSnapshot.has_value()) {
            for (const mw::battle::CombatantSnapshot& combatant : options_.battleSnapshot->combatants) {
                if (combatant.roster.team != mw::battle::BattleTeam::Opposing) {
                    continue;
                }
                const auto [x, y] = mapPoint(combatant.transform.x, combatant.transform.z);
                const bool disabled = combatant.missionStatus != mw::battle::CombatantMissionStatus::Active || combatant.mechDestroyed;
                drawMapPixel(x, y, disabled ? kDisabledEnemyRgb : kEnemyRgb);
            }
            if (options_.battleSnapshot->objective.valid) {
                const auto [x, y] = mapPoint(
                    options_.battleSnapshot->objective.transform.x,
                    options_.battleSnapshot->objective.transform.z);
                drawMapPixel(x, y, kTargetRgb);
            }
        }
        drawMapPixel(centerX, centerY, kPlayerRgb);
        if (options_.battleSnapshot.has_value() &&
            options_.battleSnapshot->battlefieldBoundary.valid &&
            options_.battleSnapshot->battlefieldBoundary.mapProjectionProven) {
            const mw::battle::BattlefieldBoundaryState& boundary =
                options_.battleSnapshot->battlefieldBoundary;
            const auto [left, top] = mapPoint(boundary.worldMinX, boundary.worldMinZ);
            const auto [right, bottom] = mapPoint(boundary.worldMaxX, boundary.worldMaxZ);
            renderBattlefieldBoundaryDashes(
                left,
                right,
                top,
                bottom,
                kLightCockpitMinimapRect,
                battlefieldMapAllowedExitMask(*options_.battleSnapshot),
                options_.battleSnapshot->tickIndex);
        }
    }

    void renderCommandMapScreen() {
        renderCockpitBackdropRaster();

        glViewport(0, 0, width_, height_);
        gl_.glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_POLYGON_STIPPLE);
        glDisable(GL_SCISSOR_TEST);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(std::max(1, width_)), static_cast<double>(std::max(1, height_)), 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        const ScreenRect mapRect = battleCommandMapRect(options_.battleCommandMapScreen);
        fillScreenRect(virtualScreenRect(mapRect), commandMapGroundRgb(options_.terrainColorMode));

        Bounds mapBounds{Vec3{0.0f, 0.0f, 0.0f}, Vec3{86500.0f, 0.0f, 46500.0f}, Vec3{}, 1.0f};
        bool hasMapBounds = false;
        if (!terrainMesh_.vertices.empty()) {
            mapBounds = terrainMeshBounds(terrainMesh_);
            hasMapBounds = true;
        }
        if (!terrainObjectBatch_.vertices.empty()) {
            const Bounds objectBounds = computeBounds(terrainObjectBatch_);
            mapBounds = hasMapBounds ? mergeBounds(mapBounds, objectBounds) : objectBounds;
            hasMapBounds = true;
        }
        if (!terrainObjectDitherBatch_.vertices.empty()) {
            const Bounds ditherBounds = computeBounds(terrainObjectDitherBatch_);
            mapBounds = hasMapBounds ? mergeBounds(mapBounds, ditherBounds) : ditherBounds;
        }
        const bool useSnapshotBoundary =
            options_.battleSnapshot.has_value() &&
            options_.battleSnapshot->battlefieldBoundary.valid &&
            options_.battleSnapshot->battlefieldBoundary.mapProjectionProven;
        const double spanX = std::max(1.0, static_cast<double>(mapBounds.max.x - mapBounds.min.x));
        const double spanZ = std::max(1.0, static_cast<double>(mapBounds.max.z - mapBounds.min.z));
        const auto mapPoint = [&](double worldX, double worldZ) {
            if (useSnapshotBoundary) {
                const mw::battle::BattlefieldBoundaryState& boundary =
                    options_.battleSnapshot->battlefieldBoundary;
                const double boundaryCenterX = (boundary.worldMinX + boundary.worldMaxX) * 0.5;
                const double boundaryCenterZ = (boundary.worldMinZ + boundary.worldMaxZ) * 0.5;
                const double worldUnitsPerMapPixel = std::max(1.0f, options_.terrainCellSize);
                const int x = mapRect.x + mapRect.width / 2 +
                              static_cast<int>(std::lround((worldX - boundaryCenterX) / worldUnitsPerMapPixel));
                const int y = mapRect.y + mapRect.height / 2 +
                              static_cast<int>(std::lround((worldZ - boundaryCenterZ) / worldUnitsPerMapPixel));
                return std::pair<int, int>{x, y};
            }
            const double normalizedX = (worldX - static_cast<double>(mapBounds.min.x)) / spanX;
            const double normalizedY = (worldZ - static_cast<double>(mapBounds.min.z)) / spanZ;
            const int x = mapRect.x +
                          static_cast<int>(std::lround(normalizedX * static_cast<double>(mapRect.width - 1)));
            const int y = mapRect.y +
                          static_cast<int>(std::lround(normalizedY * static_cast<double>(mapRect.height - 1)));
            return std::pair<int, int>{x, y};
        };
        const auto drawVirtualPixel = [&](int x, int y, std::array<float, 3> rgb) {
            if (x >= mapRect.x &&
                x < mapRect.x + mapRect.width &&
                y >= mapRect.y &&
                y < mapRect.y + mapRect.height) {
                fillScreenRect(virtualScreenRect(x, y, 1, 1), rgb);
            }
        };
        const auto drawBlip = [&](double worldX, double worldZ, std::array<float, 3> rgb) {
            const auto [x, y] = mapPoint(worldX, worldZ);
            drawVirtualPixel(x, y, rgb);
            drawVirtualPixel(x + 1, y, rgb);
            drawVirtualPixel(x, y + 1, rgb);
            drawVirtualPixel(x + 1, y + 1, rgb);
        };
        const auto drawObjectTriangles = [&](const mw::legacy3d::GpuBatch& batch, bool dithered) {
            static const std::array<uint8_t, 128> kCheckerStipple =
                mw::legacy3d::terrainCheckerStipple4x4();
            if (dithered) {
                glEnable(GL_POLYGON_STIPPLE);
                glPolygonStipple(kCheckerStipple.data());
            }
            glBegin(GL_TRIANGLES);
            for (size_t i = 0; i + 2u < batch.triangleIndices.size(); i += 3u) {
                std::array<size_t, 3> offsets{};
                bool validTriangle = true;
                for (size_t corner = 0; corner < 3u; ++corner) {
                    const uint32_t index = batch.triangleIndices[i + corner];
                    const size_t off = static_cast<size_t>(index) * 6u;
                    if (off + 5u >= batch.vertices.size()) {
                        validTriangle = false;
                        break;
                    }
                    offsets[corner] = off;
                }
                if (!validTriangle) {
                    continue;
                }
                for (const size_t off : offsets) {
                    const auto [x, y] = mapPoint(batch.vertices[off], batch.vertices[off + 2u]);
                    const std::array<float, 3> rgb{
                        batch.vertices[off + 3u],
                        batch.vertices[off + 4u],
                        batch.vertices[off + 5u],
                    };
                    glColor3f(rgb[0], rgb[1], rgb[2]);
                    const ScreenRect screen = virtualScreenRect(x, y, 1, 1);
                    glVertex2i(screen.x, screen.y);
                }
            }
            glEnd();
            if (dithered) {
                glDisable(GL_POLYGON_STIPPLE);
            }
        };

        if (useSnapshotBoundary) {
            const ScreenRect actualMapRect = virtualScreenRect(mapRect);
            glEnable(GL_SCISSOR_TEST);
            glScissor(
                actualMapRect.x,
                std::max(0, height_ - actualMapRect.y - actualMapRect.height),
                actualMapRect.width,
                actualMapRect.height);
        }

        drawObjectTriangles(terrainObjectBatch_, false);
        drawObjectTriangles(terrainObjectDitherBatch_, true);

        constexpr std::array<float, 3> kPlayerRgb{1.0f, 1.0f, 1.0f};
        constexpr std::array<float, 3> kEnemyRgb{0.86f, 0.0f, 0.0f};
        constexpr std::array<float, 3> kNeutralRgb{0.86f, 0.86f, 0.0f};
        constexpr std::array<float, 3> kTargetRgb{0.0f, 0.86f, 0.86f};
        if (options_.battleSnapshot.has_value()) {
            for (const mw::battle::CombatantSnapshot& combatant : options_.battleSnapshot->combatants) {
                const std::array<float, 3> rgb =
                    combatant.roster.team == mw::battle::BattleTeam::Player
                        ? kPlayerRgb
                        : combatant.roster.team == mw::battle::BattleTeam::Opposing
                              ? kEnemyRgb
                              : kNeutralRgb;
                drawBlip(
                    combatant.transform.x,
                    combatant.transform.z,
                    rgb);
            }
        }
        const std::pair<double, double> targetWorld = snapshotObjectiveWorldOrFallback(options_);
        drawBlip(targetWorld.first, targetWorld.second, kTargetRgb);
        if (useSnapshotBoundary) {
            const mw::battle::BattlefieldBoundaryState& boundary =
                options_.battleSnapshot->battlefieldBoundary;
            const auto [left, top] = mapPoint(boundary.worldMinX, boundary.worldMinZ);
            const auto [right, bottom] = mapPoint(boundary.worldMaxX, boundary.worldMaxZ);
            renderBattlefieldBoundaryDashes(
                left,
                right,
                top,
                bottom,
                mapRect,
                battlefieldMapAllowedExitMask(*options_.battleSnapshot),
                options_.battleSnapshot->tickIndex);
            glDisable(GL_SCISSOR_TEST);
        }
        glDisable(GL_POLYGON_STIPPLE);

        if (options_.battleCommandMapScreen == BattleCommandMapScreen::MissionStatus) {
            const mw::battle::BattleMissionBriefing& mission =
                options_.battleSnapshot.has_value() && options_.battleSnapshot->mission.valid
                    ? options_.battleSnapshot->mission
                    : options_.battleMission;
            if (mission.valid) {
                constexpr std::array<float, 3> kMissionTextRgb{1.0f, 1.0f, 85.0f / 255.0f};
                drawCockpitFontText("YOUR NEXT MISSION:", 24, 146, kMissionTextRgb);
                drawCockpitFontText(mission.title, 32, 164, kMissionTextRgb);
            }
        }

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    void renderCockpitBackdropRaster() {
        if (!cockpitBackdropReady_ || cockpitBackdropImage_.rgba.empty()) {
            return;
        }

        glViewport(0, 0, width_, height_);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_LIGHTING);
        glDisable(GL_TEXTURE_2D);
        glDisable(GL_POLYGON_STIPPLE);
        glDisable(GL_SCISSOR_TEST);
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(std::max(1, width_)), static_cast<double>(std::max(1, height_)), 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        const auto pixelRgb = [&](int x, int y) {
            const size_t offset =
                (static_cast<size_t>(y) * static_cast<size_t>(cockpitBackdropImage_.width) + static_cast<size_t>(x)) * 4u;
            return std::array<uint8_t, 3>{{
                cockpitBackdropImage_.rgba[offset + 0u],
                cockpitBackdropImage_.rgba[offset + 1u],
                cockpitBackdropImage_.rgba[offset + 2u],
            }};
        };
        const auto toFloatRgb = [](const std::array<uint8_t, 3>& rgb) {
            return std::array<float, 3>{{
                static_cast<float>(rgb[0]) / 255.0f,
                static_cast<float>(rgb[1]) / 255.0f,
                static_cast<float>(rgb[2]) / 255.0f,
            }};
        };

        for (int y = 0; y < cockpitBackdropImage_.height; ++y) {
            int runStart = 0;
            std::array<uint8_t, 3> runRgb = pixelRgb(0, y);
            for (int x = 1; x <= cockpitBackdropImage_.width; ++x) {
                const bool endOfRow = x == cockpitBackdropImage_.width;
                const std::array<uint8_t, 3> rgb = endOfRow ? runRgb : pixelRgb(x, y);
                if (endOfRow || rgb != runRgb) {
                    fillScreenRect(virtualScreenRect(runStart, y, x - runStart, 1), toFloatRgb(runRgb));
                    runStart = x;
                    runRgb = rgb;
                }
            }
        }

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    void renderCockpitCompositor() {
        gl_.glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_POLYGON_STIPPLE);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(std::max(1, width_)), static_cast<double>(std::max(1, height_)), 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        const CockpitLayout& layout = cockpitLayoutForOptions(options_);
        const ScreenRect viewport = battleWorldViewportRect();
        const std::array<float, 3> shellDark{0.015f, 0.018f, 0.020f};
        const std::array<float, 3> shellMid{0.075f, 0.080f, 0.085f};
        const std::array<float, 3> shellPanel{0.12f, 0.12f, 0.10f};
        const std::array<float, 3> shellLine{0.42f, 0.45f, 0.37f};
        const std::array<float, 3> shellAccent{0.72f, 0.70f, 0.42f};

        if (!cockpitBackdropReady_) {
            fillScreenRect(ScreenRect{0, 0, width_, viewport.y}, shellDark);
            fillScreenRect(ScreenRect{0, viewport.y, viewport.x, viewport.height}, shellMid);
            fillScreenRect(
                ScreenRect{viewport.x + viewport.width, viewport.y, width_ - viewport.x - viewport.width, viewport.height},
                shellMid);
            fillScreenRect(
                ScreenRect{0, viewport.y + viewport.height, width_, height_ - viewport.y - viewport.height},
                shellDark);
            strokeScreenRect(viewport, shellLine);
            strokeScreenRect(ScreenRect{viewport.x - 4, viewport.y - 4, viewport.width + 8, viewport.height + 8}, shellAccent);
        }

        const ScreenRect leftPanel = virtualScreenRect(layout.leftPanel);
        const ScreenRect centerPanel = virtualScreenRect(layout.centerPanel);
        const ScreenRect rightPanel = virtualScreenRect(layout.rightPanel);
        const ScreenRect compassPanel = virtualScreenRect(layout.compassPanel);
        for (const ScreenRect& panel : {leftPanel, centerPanel, rightPanel, compassPanel}) {
            if (!cockpitBackdropReady_) {
                fillScreenRect(panel, shellPanel);
                strokeScreenRect(panel, shellLine);
            }
        }

        const ScreenRect leftStrut = virtualScreenRect(layout.leftStrut);
        const ScreenRect rightStrut = virtualScreenRect(layout.rightStrut);
        if (!cockpitBackdropReady_) {
            fillScreenRect(leftStrut, shellMid);
            fillScreenRect(rightStrut, shellMid);
            strokeScreenRect(leftStrut, shellLine);
            strokeScreenRect(rightStrut, shellLine);
        }

        if (fontBase_ != 0) {
            const CockpitHudState hud = cockpitHudStateFromOptions(options_);
            const std::array<float, 3> meterBack{0.025f, 0.028f, 0.025f};
            const std::array<float, 3> meterGreen{0.38f, 0.76f, 0.26f};
            const std::array<float, 3> meterAmber{0.86f, 0.72f, 0.22f};
            const std::array<float, 3> meterRed{0.72f, 0.16f, 0.12f};
            const CockpitHudColor& hudColor = currentCockpitHudColor();

            if (!cockpitBackdropReady_) {
                fillScreenMeter(
                    ScreenRect{leftPanel.x + 8, leftPanel.y + 32, leftPanel.width - 16, 6},
                    std::abs(hud.speed) / std::max(1.0, options_.battleDriveMaxForwardSpeed),
                    meterGreen,
                    meterBack);
                fillScreenMeter(
                    ScreenRect{leftPanel.x + 8, leftPanel.y + 42, leftPanel.width - 16, 5},
                    std::abs(hud.targetSpeed) / std::max(1.0, options_.battleDriveMaxForwardSpeed),
                    meterAmber,
                    meterBack);
                fillScreenMeter(
                    ScreenRect{centerPanel.x + 10, centerPanel.y + 36, centerPanel.width - 20, 6},
                    (hud.torsoDegrees + 42.0) / 84.0,
                    meterAmber,
                    meterBack);
                if (hud.boundaryContact) {
                    fillScreenRect(ScreenRect{centerPanel.x + 10, centerPanel.y + 46, centerPanel.width - 20, 5}, meterRed);
                }
            }

            glColor3f(hudColor.rgb[0], hudColor.rgb[1], hudColor.rgb[2]);
            std::ostringstream compass;
            if (cockpitBackdropReady_) {
                renderLightCockpitMinimap();
                renderCockpitCompass(hud);
                renderLightCockpitCrosshair(hud);
                renderLightCockpitSpeedGauge(hud);
                renderCockpitJumpGauge(hud);
                const ScreenRect virtualViewport = layout.viewport;
                std::ostringstream zoomText;
                zoomText << "ZOOM X" << battleCockpitZoomLevel_;
                const int leftHudLabelX = virtualViewport.x + std::max(18, virtualViewport.width / 6);
                const int rightHudLabelX = virtualViewport.x + virtualViewport.width -
                                           (layout.family == CockpitFamily::Heavy ? 87 : std::max(72, virtualViewport.width / 4)) -
                                           12;
                if (layout.family == CockpitFamily::Heavy) {
                    drawCockpitFontText(zoomText.str(), leftHudLabelX, virtualViewport.y + virtualViewport.height - 7, hudColor.rgb);
                    drawCockpitFontText("AWS", rightHudLabelX, virtualViewport.y + virtualViewport.height - 7, hudColor.rgb);
                } else {
                    drawCockpitFontText("AWS", leftHudLabelX, virtualViewport.y + virtualViewport.height - 7, hudColor.rgb);
                    drawCockpitFontText(zoomText.str(), rightHudLabelX, virtualViewport.y + virtualViewport.height - 7, hudColor.rgb);
                }
            } else {
                const ScreenRect headingPanel = compassPanel;
                const int compassMid = headingPanel.x + headingPanel.width / 2;
                for (int tickOffset = -3; tickOffset <= 3; ++tickOffset) {
                    const int x = compassMid + tickOffset * (headingPanel.width / 8);
                    drawVerticalTick(x, headingPanel.y + 8, headingPanel.y + headingPanel.height, tickOffset == 0 ? meterGreen : shellLine);
                }
                compass << "COMPASS " << hud.compass
                        << " HDG " << std::setw(3) << std::setfill('0') << static_cast<int>(std::lround(hud.headingDegrees)) % 360;
                drawOverlayText(compassPanel.x + 8, compassPanel.y + 10, compass.str());
            }

            std::ostringstream left;
            left << "SPD " << std::fixed << std::setprecision(0) << hud.speed
                 << " TGT " << hud.targetSpeed;
            if (!cockpitBackdropReady_ || !cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
                drawOverlayText(leftPanel.x + 8, leftPanel.y + (cockpitBackdropReady_ ? 10 : 18), left.str());
            }

            std::ostringstream center;
            center << "TRS " << std::fixed << std::setprecision(0) << hud.torsoDegrees
                   << " BND " << (hud.boundaryContact ? "HIT" : "OK");
            if (!cockpitBackdropReady_ || !cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
                drawOverlayText(centerPanel.x + 8, centerPanel.y + (cockpitBackdropReady_ ? 10 : 18), center.str());
            }

            std::ostringstream right;
            right << "SCN " << hud.scenario
                  << " CAM " << hud.camera;
            if (!cockpitBackdropReady_ || !cockpitFamilyUsesSharedHud(resolveCockpitFamily(options_))) {
                drawOverlayText(rightPanel.x + 8, rightPanel.y + (cockpitBackdropReady_ ? 10 : 18), right.str());
            }
        }

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
        glEnable(GL_DEPTH_TEST);
    }

    void applyMechWorldTransform() {
        if (!battleMechTransformActive_) {
            glTranslatef(mechDrawOffset_.x, mechDrawOffset_.y, mechDrawOffset_.z);
            return;
        }
        const Bounds mechBounds = currentMechLocalBounds();
        glTranslatef(
            mechDrawOffset_.x + mechBounds.center.x,
            mechDrawOffset_.y + mechBounds.min.y,
            mechDrawOffset_.z + mechBounds.center.z);
        glRotatef(mechHeadingRadians_ * 180.0f / 3.14159265358979323846f, 0.0f, 1.0f, 0.0f);
        glTranslatef(-mechBounds.center.x, -mechBounds.min.y, -mechBounds.center.z);
    }

    void renderAssembledMesh() {
        for (const mw::mech3d::ComponentRenderRange& range : renderInstance_.ranges) {
            if (!isRenderComponentVisible(range.componentId)) {
                continue;
            }
            const mw::mech3d::ModelAssemblyComponent* component = findRenderComponent(range.componentId);
            if (component == nullptr) {
                continue;
            }

            glPushMatrix();
            glMultMatrixf(component->worldMatrix.data());
            if (!options_.wire && range.triangleIndexCount > 0) {
                gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_.triEbo);
                glDrawElements(
                    GL_TRIANGLES,
                    static_cast<GLsizei>(range.triangleIndexCount),
                    GL_UNSIGNED_INT,
                    indexOffset(range.triangleIndexOffset));
            }
            if (range.lineIndexCount > 0) {
                glLineWidth(options_.wire ? 1.5f : 1.0f);
                gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_.lineEbo);
                glDrawElements(
                    GL_LINES,
                    static_cast<GLsizei>(range.lineIndexCount),
                    GL_UNSIGNED_INT,
                    indexOffset(range.lineIndexOffset));
            }
            glPopMatrix();
        }
    }

    void renderTerrain() {
        if (!options_.terrainEnabled || terrainMesh_.vertices.empty()) {
            return;
        }

        const TerrainPalette& palette = terrainPaletteById(options_.terrainColorMode);
        glBegin(GL_TRIANGLES);
        for (uint32_t index : terrainMesh_.indices) {
            if (index >= terrainMesh_.vertices.size()) {
                continue;
            }
            const mw::legacy3d::TerrainMeshVertex& v = terrainMesh_.vertices[index];
            if (options_.terrainColorMode == "flat") {
                glColor3f(palette.groundRgb[0], palette.groundRgb[1], palette.groundRgb[2]);
            } else if (options_.terrainColorMode != "debug") {
                glColor3f(palette.groundRgb[0], palette.groundRgb[1], palette.groundRgb[2]);
            } else {
                const float band = static_cast<float>(v.colorBand) / 3.0f;
                const float detail = static_cast<float>(v.detailBits) / 31.0f;
                glColor3f(0.13f + band * 0.28f, 0.18f + detail * 0.35f, 0.09f + band * 0.30f);
            }
            glVertex3f(v.x, v.y, v.z);
        }
        glEnd();

        if (!terrainGridEdgesVisible_) {
            return;
        }

        glDisable(GL_POLYGON_STIPPLE);
        glColor3f(palette.gridRgb[0], palette.gridRgb[1], palette.gridRgb[2]);
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        for (size_t i = 0; i + 2 < terrainMesh_.indices.size(); i += 3) {
            const uint32_t tri[3] = {
                terrainMesh_.indices[i],
                terrainMesh_.indices[i + 1u],
                terrainMesh_.indices[i + 2u],
            };
            for (int edge = 0; edge < 3; ++edge) {
                const uint32_t a = tri[edge];
                const uint32_t b = tri[(edge + 1) % 3];
                if (a >= terrainMesh_.vertices.size() || b >= terrainMesh_.vertices.size()) {
                    continue;
                }
                const mw::legacy3d::TerrainMeshVertex& va = terrainMesh_.vertices[a];
                const mw::legacy3d::TerrainMeshVertex& vb = terrainMesh_.vertices[b];
                glVertex3f(va.x, va.y + 1.0f, va.z);
                glVertex3f(vb.x, vb.y + 1.0f, vb.z);
            }
        }
        glEnd();
    }

    void renderTerrainObjects() {
        if (!options_.terrainWorldEnabled || terrainObjectBatch_.vertices.empty()) {
            return;
        }

        const TerrainPalette& palette = terrainPaletteById(options_.terrainColorMode);
        auto drawBatch = [&](const mw::legacy3d::GpuBatch& batch, bool remapFlatColor) {
            glBegin(GL_TRIANGLES);
            for (uint32_t index : batch.triangleIndices) {
                const size_t off = static_cast<size_t>(index) * 6u;
                if (off + 5u >= batch.vertices.size()) {
                    continue;
                }
                const std::array<float, 3> source{
                    batch.vertices[off + 3u],
                    batch.vertices[off + 4u],
                    batch.vertices[off + 5u],
                };
                const std::array<float, 3> rgb = remapFlatColor ? terrainObjectColor(source, palette) : source;
                glColor3f(rgb[0], rgb[1], rgb[2]);
                glVertex3f(batch.vertices[off], batch.vertices[off + 1u], batch.vertices[off + 2u]);
            }
            glEnd();
        };

        drawBatch(terrainObjectBatch_, options_.terrainColorMode == "flat");
        if (!terrainObjectDitherBatch_.vertices.empty()) {
            static const std::array<uint8_t, 128> kCheckerStipple =
                mw::legacy3d::terrainCheckerStipple4x4();
            glEnable(GL_POLYGON_STIPPLE);
            glPolygonStipple(kCheckerStipple.data());
            drawBatch(terrainObjectDitherBatch_, false);
            glDisable(GL_POLYGON_STIPPLE);
        }

        if (!terrainObjectEdgesVisible(options_.terrainColorMode, options_.wire)) {
            return;
        }

        glDisable(GL_POLYGON_STIPPLE);
        glColor3f(palette.edgeRgb[0], palette.edgeRgb[1], palette.edgeRgb[2]);
        glLineWidth(1.0f);
        glBegin(GL_LINES);
        for (uint32_t index : terrainObjectBatch_.lineIndices) {
            const size_t off = static_cast<size_t>(index) * 6u;
            if (off + 2u >= terrainObjectBatch_.vertices.size()) {
                continue;
            }
            glVertex3f(
                terrainObjectBatch_.vertices[off],
                terrainObjectBatch_.vertices[off + 1u],
                terrainObjectBatch_.vertices[off + 2u]);
        }
        glEnd();
    }

    void renderBattlefieldBoundaryDashes(
        int left,
        int right,
        int top,
        int bottom,
        const ScreenRect& clip,
        uint8_t allowedExitMask,
        uint64_t tickIndex) {
        if (left > right || top > bottom || clip.width <= 0 || clip.height <= 0) {
            return;
        }

        const auto edgeRgb = [&](mw::battle::BattlefieldBoundaryEdge edge) {
            return (allowedExitMask & mw::battle::battlefieldBoundaryEdgeMask(edge)) != 0u
                       ? battlefieldBoundaryAllowedRgb_
                       : battlefieldBoundaryOrdinaryRgb_;
        };
        const auto drawClipped = [this, &clip](
                                     int x,
                                     int y,
                                     int width,
                                     int height,
                                     const ScreenRect& edgeClip,
                                     std::array<float, 3> rgb) {
            const int clippedLeft = std::max({x, clip.x, edgeClip.x});
            const int clippedTop = std::max({y, clip.y, edgeClip.y});
            const int clippedRight = std::min({x + width, clip.x + clip.width, edgeClip.x + edgeClip.width});
            const int clippedBottom = std::min({y + height, clip.y + clip.height, edgeClip.y + edgeClip.height});
            if (clippedLeft < clippedRight && clippedTop < clippedBottom) {
                fillScreenRect(
                    virtualScreenRect(
                        clippedLeft,
                        clippedTop,
                        clippedRight - clippedLeft,
                        clippedBottom - clippedTop),
                    rgb);
            }
        };

        const int phase = static_cast<int>(tickIndex & 7u) - 4;
        const ScreenRect topEdge{left, top, right - left + 1, 1};
        const ScreenRect bottomEdge{left, bottom, right - left + 1, 1};
        const ScreenRect westEdge{left, top, 1, bottom - top + 1};
        const ScreenRect eastEdge{right, top, 1, bottom - top + 1};
        for (int x = left + phase; x <= right; x += 8) {
            drawClipped(x, top, 5, 1, topEdge, edgeRgb(mw::battle::BattlefieldBoundaryEdge::North));
        }
        for (int x = right - phase; x >= left; x -= 8) {
            drawClipped(x - 4, bottom, 5, 1, bottomEdge, edgeRgb(mw::battle::BattlefieldBoundaryEdge::South));
        }
        for (int y = top + phase - 5; y <= bottom; y += 8) {
            drawClipped(right, y, 1, 5, eastEdge, edgeRgb(mw::battle::BattlefieldBoundaryEdge::East));
        }
        for (int y = bottom - phase + 5; y >= top; y -= 8) {
            drawClipped(left, y - 4, 1, 5, westEdge, edgeRgb(mw::battle::BattlefieldBoundaryEdge::West));
        }
    }

    void renderBattleObjectiveModel() {
        if (battleObjectiveModelBatch_.vertices.empty() ||
            !battleObjectiveModelBounds_.has_value() ||
            !options_.battleSnapshot.has_value() ||
            !options_.battleSnapshot->objective.valid) {
            return;
        }

        const mw::battle::BattleObjectiveState& objective = options_.battleSnapshot->objective;
        const Bounds& modelBounds = *battleObjectiveModelBounds_;
        const float objectiveX = static_cast<float>(objective.transform.x);
        const float objectiveZ = static_cast<float>(objective.transform.z);
        const float groundY = objective.staticModel.groundAligned && !terrainMesh_.vertices.empty()
                                  ? terrainMesh_.sampleHeightNearest(objectiveX, objectiveZ)
                                  : 0.0f;

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_POLYGON_STIPPLE);
        glPushMatrix();
        glTranslatef(
            objectiveX,
            groundY + static_cast<float>(objective.transform.y),
            objectiveZ);
        glRotatef(
            static_cast<float>(objective.transform.headingRadians * 180.0 / 3.14159265358979323846),
            0.0f,
            1.0f,
            0.0f);
        glTranslatef(-modelBounds.center.x, -modelBounds.min.y, -modelBounds.center.z);

        if (!options_.wire) {
            glBegin(GL_TRIANGLES);
            for (uint32_t index : battleObjectiveModelBatch_.triangleIndices) {
                const size_t off = static_cast<size_t>(index) * 6u;
                if (off + 5u >= battleObjectiveModelBatch_.vertices.size()) {
                    continue;
                }
                glColor3f(
                    battleObjectiveModelBatch_.vertices[off + 3u],
                    battleObjectiveModelBatch_.vertices[off + 4u],
                    battleObjectiveModelBatch_.vertices[off + 5u]);
                glVertex3f(
                    battleObjectiveModelBatch_.vertices[off],
                    battleObjectiveModelBatch_.vertices[off + 1u],
                    battleObjectiveModelBatch_.vertices[off + 2u]);
            }
            glEnd();
        }

        glLineWidth(options_.wire ? 1.5f : 1.0f);
        glBegin(GL_LINES);
        for (uint32_t index : battleObjectiveModelBatch_.lineIndices) {
            const size_t off = static_cast<size_t>(index) * 6u;
            if (off + 5u >= battleObjectiveModelBatch_.vertices.size()) {
                continue;
            }
            glColor3f(
                battleObjectiveModelBatch_.vertices[off + 3u],
                battleObjectiveModelBatch_.vertices[off + 4u],
                battleObjectiveModelBatch_.vertices[off + 5u]);
            glVertex3f(
                battleObjectiveModelBatch_.vertices[off],
                battleObjectiveModelBatch_.vertices[off + 1u],
                battleObjectiveModelBatch_.vertices[off + 2u]);
        }
        glEnd();
        glPopMatrix();
    }

    void renderBattleNonPlayerCombatants() {
        if (battleNonPlayerCombatantVisuals_.empty() || !options_.battleSnapshot.has_value()) {
            return;
        }

        glDisable(GL_TEXTURE_2D);
        glDisable(GL_POLYGON_STIPPLE);
        for (const LoadedBattleCombatantVisual& visual : battleNonPlayerCombatantVisuals_) {
            const auto combatantIt = std::find_if(
                options_.battleSnapshot->combatants.begin(),
                options_.battleSnapshot->combatants.end(),
                [&visual](const mw::battle::CombatantSnapshot& combatant) {
                    return combatant.id == visual.entityId && !combatant.playerControlled;
                });
            if (combatantIt == options_.battleSnapshot->combatants.end()) {
                continue;
            }

            const float worldX = static_cast<float>(combatantIt->transform.x);
            const float worldZ = static_cast<float>(combatantIt->transform.z);
            const float groundY = !terrainMesh_.vertices.empty()
                                      ? terrainMesh_.sampleHeightNearest(worldX, worldZ)
                                      : 0.0f;
            glPushMatrix();
            glTranslatef(worldX, groundY + static_cast<float>(combatantIt->transform.y), worldZ);
            glRotatef(
                static_cast<float>(combatantIt->transform.headingRadians * 180.0 / 3.14159265358979323846),
                0.0f,
                1.0f,
                0.0f);
            glTranslatef(
                -visual.placementBounds.center.x,
                -visual.placementBounds.min.y,
                -visual.placementBounds.center.z);

            for (const mw::mech3d::ComponentRenderRange& range : visual.renderInstance.ranges) {
                if (std::find(
                        combatantIt->hiddenComponentIds.begin(),
                        combatantIt->hiddenComponentIds.end(),
                        range.componentId) != combatantIt->hiddenComponentIds.end()) {
                    continue;
                }
                const auto componentIt = std::find_if(
                    visual.renderInstance.assembly.components.begin(),
                    visual.renderInstance.assembly.components.end(),
                    [&range](const mw::mech3d::ModelAssemblyComponent& component) {
                        return component.componentId == range.componentId;
                    });
                if (componentIt == visual.renderInstance.assembly.components.end()) {
                    continue;
                }

                glPushMatrix();
                glMultMatrixf(componentIt->worldMatrix.data());
                if (!options_.wire) {
                    glBegin(GL_TRIANGLES);
                    const size_t triangleEnd = std::min(
                        visual.renderInstance.batch.triangleIndices.size(),
                        static_cast<size_t>(range.triangleIndexOffset + range.triangleIndexCount));
                    for (size_t i = range.triangleIndexOffset; i < triangleEnd; ++i) {
                        const uint32_t index = visual.renderInstance.batch.triangleIndices[i];
                        const size_t off = static_cast<size_t>(index) * 6u;
                        if (off + 5u >= visual.renderInstance.batch.vertices.size()) {
                            continue;
                        }
                        glColor3f(
                            visual.renderInstance.batch.vertices[off + 3u],
                            visual.renderInstance.batch.vertices[off + 4u],
                            visual.renderInstance.batch.vertices[off + 5u]);
                        glVertex3f(
                            visual.renderInstance.batch.vertices[off],
                            visual.renderInstance.batch.vertices[off + 1u],
                            visual.renderInstance.batch.vertices[off + 2u]);
                    }
                    glEnd();
                }

                glLineWidth(options_.wire ? 1.5f : 1.0f);
                glBegin(GL_LINES);
                const size_t lineEnd = std::min(
                    visual.renderInstance.batch.lineIndices.size(),
                    static_cast<size_t>(range.lineIndexOffset + range.lineIndexCount));
                for (size_t i = range.lineIndexOffset; i < lineEnd; ++i) {
                    const uint32_t index = visual.renderInstance.batch.lineIndices[i];
                    const size_t off = static_cast<size_t>(index) * 6u;
                    if (off + 5u >= visual.renderInstance.batch.vertices.size()) {
                        continue;
                    }
                    glColor3f(
                        visual.renderInstance.batch.vertices[off + 3u],
                        visual.renderInstance.batch.vertices[off + 4u],
                        visual.renderInstance.batch.vertices[off + 5u]);
                    glVertex3f(
                        visual.renderInstance.batch.vertices[off],
                        visual.renderInstance.batch.vertices[off + 1u],
                        visual.renderInstance.batch.vertices[off + 2u]);
                }
                glEnd();
                glPopMatrix();
            }
            glPopMatrix();
        }
    }

    const mw::mech3d::ModelAssemblyComponent* findRenderComponent(int componentId) const {
        const auto it = std::find_if(
            renderInstance_.assembly.components.begin(),
            renderInstance_.assembly.components.end(),
            [componentId](const mw::mech3d::ModelAssemblyComponent& component) {
                return component.componentId == componentId;
            });
        return it == renderInstance_.assembly.components.end() ? nullptr : &*it;
    }

    mw::mech3d::ResolvedMechRuntimeState resolvedRuntimeState() const {
        return resolveCliRuntimeState(options_);
    }

    bool isRenderComponentVisible(int componentId) const {
        return mw::mech3d::isResolvedMechComponentVisible(renderInstance_.assembly, resolvedRuntimeState(), componentId);
    }

    const void* indexOffset(uint32_t indexOffset) const {
        return reinterpret_cast<const void*>(static_cast<uintptr_t>(indexOffset) * sizeof(uint32_t));
    }

    void renderComponentDebug() {
        if (!options_.assembled || !showComponentDebug_) {
            return;
        }

        gl_.glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glLineWidth(1.5f);

        for (const mw::mech3d::ModelAssemblyComponent& component : renderInstance_.assembly.components) {
            if (!isRenderComponentVisible(component.componentId)) {
                continue;
            }
            const std::array<float, 3> rgb = componentDebugRgb(component.componentId);
            drawDebugBounds(component.worldBounds, rgb);
            drawDebugPivot(component.pivot, std::max(6.0f, component.worldBounds.radius * 0.08f), rgb);
        }

        glLineWidth(1.0f);
        glEnable(GL_DEPTH_TEST);
    }

    std::array<float, 3> componentDebugRgb(int componentId) const {
        static constexpr std::array<std::array<float, 3>, 6> palette{{
            {1.00f, 0.22f, 0.18f},
            {0.18f, 0.74f, 1.00f},
            {0.35f, 0.95f, 0.35f},
            {1.00f, 0.72f, 0.20f},
            {0.95f, 0.38f, 0.95f},
            {0.82f, 0.90f, 1.00f},
        }};
        return palette[static_cast<size_t>(std::abs(componentId) % static_cast<int>(palette.size()))];
    }

    void drawDebugBounds(const mw::mech3d::Bounds& bounds, std::array<float, 3> rgb) {
        if (!bounds.valid) {
            return;
        }

        const mw::mech3d::Vec3f p000{bounds.min.x, bounds.min.y, bounds.min.z};
        const mw::mech3d::Vec3f p100{bounds.max.x, bounds.min.y, bounds.min.z};
        const mw::mech3d::Vec3f p010{bounds.min.x, bounds.max.y, bounds.min.z};
        const mw::mech3d::Vec3f p110{bounds.max.x, bounds.max.y, bounds.min.z};
        const mw::mech3d::Vec3f p001{bounds.min.x, bounds.min.y, bounds.max.z};
        const mw::mech3d::Vec3f p101{bounds.max.x, bounds.min.y, bounds.max.z};
        const mw::mech3d::Vec3f p011{bounds.min.x, bounds.max.y, bounds.max.z};
        const mw::mech3d::Vec3f p111{bounds.max.x, bounds.max.y, bounds.max.z};

        glColor3f(rgb[0], rgb[1], rgb[2]);
        glBegin(GL_LINES);
        drawDebugLine(p000, p100);
        drawDebugLine(p100, p110);
        drawDebugLine(p110, p010);
        drawDebugLine(p010, p000);
        drawDebugLine(p001, p101);
        drawDebugLine(p101, p111);
        drawDebugLine(p111, p011);
        drawDebugLine(p011, p001);
        drawDebugLine(p000, p001);
        drawDebugLine(p100, p101);
        drawDebugLine(p110, p111);
        drawDebugLine(p010, p011);
        glEnd();
    }

    void drawDebugPivot(mw::mech3d::Vec3f pivot, float size, std::array<float, 3> rgb) {
        glColor3f(std::min(1.0f, rgb[0] + 0.25f), std::min(1.0f, rgb[1] + 0.25f), std::min(1.0f, rgb[2] + 0.25f));
        glBegin(GL_LINES);
        drawDebugLine(mw::mech3d::Vec3f{pivot.x - size, pivot.y, pivot.z}, mw::mech3d::Vec3f{pivot.x + size, pivot.y, pivot.z});
        drawDebugLine(mw::mech3d::Vec3f{pivot.x, pivot.y - size, pivot.z}, mw::mech3d::Vec3f{pivot.x, pivot.y + size, pivot.z});
        drawDebugLine(mw::mech3d::Vec3f{pivot.x, pivot.y, pivot.z - size}, mw::mech3d::Vec3f{pivot.x, pivot.y, pivot.z + size});
        glEnd();
    }

    void drawDebugLine(mw::mech3d::Vec3f a, mw::mech3d::Vec3f b) {
        glVertex3f(a.x, a.y, a.z);
        glVertex3f(b.x, b.y, b.z);
    }

    void renderOverlay() {
        if (!showOverlay_ || fontBase_ == 0) {
            return;
        }

        gl_.glBindBuffer(GL_ARRAY_BUFFER, 0);
        gl_.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_POLYGON_STIPPLE);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, static_cast<double>(std::max(1, width_)), static_cast<double>(std::max(1, height_)), 0.0, -1.0, 1.0);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        const TerrainPalette& overlayPalette = terrainPaletteById(options_.terrainColorMode);
        const std::array<float, 3> overlaySkyRgb = terrainSkyColor(options_.terrainSkyMode, overlayPalette);
        const float overlaySkyLuminance =
            overlaySkyRgb[0] * 0.2126f + overlaySkyRgb[1] * 0.7152f + overlaySkyRgb[2] * 0.0722f;
        if (overlaySkyLuminance > 0.70f) {
            glColor3f(0.02f, 0.28f, 0.04f);
        } else {
            glColor3f(0.86f, 0.90f, 0.94f);
        }
        int y = 20;
        drawOverlayText(
            12,
            y,
            battleInteractiveMode(options_)
            ? (options_.battleCockpit
                   ? "Cockpit: Up/Down speed | Left/Right turn | PgUp/PgDn scenario | RMB freelook | B bob | G grid | K sky | </> torso | A align | P pause | Q quit | F3 camera | F12 screenshot"
                   : "Drive: Up/Down speed | Left/Right turn | PgUp/PgDn scenario | RMB freelook | G grid | K sky | </> torso | A align | P pause | Q quit | F3 camera | F12 screenshot")
                : "F1 overlay | F2 component debug | F3 pose demo | F4 target | F5 pivot | F6 axis | F7 hide target | F8 clear hidden | Space pause | F9/F10 step | F11 pose amp | </> torso | PgUp/PgDn arena | T terrain color | K sky | G grid | [/] record | A assembled | S shade | W wire | O projection | P/F12 screenshot | R reset");
        y += 18;
        const mw::mech3d::ResolvedMechRuntimeState runtimeState = resolvedRuntimeState();

        std::ostringstream status;
        status << narrowPath(options_.resourcePath.filename())
               << " record #" << options_.recordIndex
               << (options_.assembled ? " assembled" : "")
               << " vbo=" << mesh_.vertexCount
               << " tri=" << mesh_.triangleCount
               << " line=" << mesh_.linePairCount
               << " shade=" << options_.shadeMode
               << (options_.wire ? " wire" : " solid")
               << " projection=" << (options_.perspective ? "perspective" : "ortho")
               << " component_debug=" << (showComponentDebug_ ? "on" : "off")
               << " pose_demo=" << (poseDemo_ ? "on" : "off")
               << " pose_amp=" << poseDemoAmplitudeDegrees() << "deg"
               << " anim_pause=" << (animationPaused_ ? "on" : "off")
               << " target=" << poseDemoComponentId_
               << " axis=" << poseDemoAxisName(poseDemoAxis_)
               << " pivot=" << posePivotModeName(posePivotMode_)
               << " hidden=" << (runtimeState.hiddenComponentIds.empty() ? "-" : joinRecordList(runtimeState.hiddenComponentIds))
               << " disabled=" << (runtimeState.disabledComponentIds.empty() ? "-" : joinRecordList(runtimeState.disabledComponentIds))
               << " destroyed=" << (runtimeState.destroyedComponentIds.empty() ? "-" : joinRecordList(runtimeState.destroyedComponentIds));
        drawOverlayText(12, y, status.str());

        if (options_.terrainEnabled) {
            y += 18;
            std::ostringstream terrainStatus;
            if (!options_.terrainLayoutNames.empty()) {
                terrainStatus << "terrain_layout=" << joinStringList(options_.terrainLayoutNames);
                if (options_.terrainScenarioIndex.has_value()) {
                    terrainStatus << " scenario=" << *options_.terrainScenarioIndex
                                  << " mode=" << options_.terrainScenarioMode;
                }
                if (!options_.terrainLayoutPreset.empty()) {
                    terrainStatus << " preset=" << options_.terrainLayoutPreset;
                }
                terrainStatus << " gap=" << std::fixed << std::setprecision(0) << options_.terrainLayoutGap;
            } else {
                terrainStatus << "terrain=" << narrowPath(options_.terrainGridPath.filename());
            }
            if (!options_.terrainArena.empty()) {
                terrainStatus << " arena=" << options_.terrainArena
                              << " " << (currentTerrainArenaPos_ + 1u) << "/" << terrainArenaNames_.size();
            }
            terrainStatus
                          << " vertices=" << terrainMesh_.vertices.size()
                          << " triangles=" << (terrainMesh_.indices.size() / 3u)
                          << " wld_objects=" << terrainWorld_.objects.size()
                          << " terpck_tri=" << (terrainObjectBatch_.triangleIndices.size() / 3u)
                          << " terpck_edges="
                          << (terrainObjectEdgesVisible(options_.terrainColorMode, options_.wire) ? "on" : "off")
                          << " color=" << options_.terrainColorMode
                          << " grd_patches=off"
                          << " sky=" << options_.terrainSkyMode
                          << " cell=" << std::fixed << std::setprecision(0) << options_.terrainCellSize
                          << " inset=" << std::fixed << std::setprecision(2) << options_.terrainObjectInset
                          << " scale=" << std::fixed << std::setprecision(2) << options_.terrainObjectScale
                          << " mirror_x=" << (options_.terrainTileMirrorX ? "yes" : "no")
                          << " mirror_y=" << (options_.terrainTileMirrorY ? "yes" : "no")
                          << " obj_mirror_y=" << (options_.terrainObjectMirrorY ? "yes" : "no")
                          << " grid_edges=" << (terrainGridEdgesVisible_ ? "on" : "off")
                          << " mech_offset=" << std::fixed << std::setprecision(1)
                          << mechDrawOffset_.x << "," << mechDrawOffset_.y << "," << mechDrawOffset_.z;
            drawOverlayText(12, y, terrainStatus.str());
        }

        if (options_.assembled) {
            y += 18;
            drawOverlayText(12, y, "assembly records: " + joinRecordList(options_.assemblyRecordIndices));

            if (options_.animationEnabled) {
                y += 18;
                std::ostringstream animationStatus;
                animationStatus << animationRuntimeLabel() << " active=" << (runtimeState.activeAnimationId.empty() ? "-" : runtimeState.activeAnimationId)
                                << " requested=" << (runtimeState.requestedAnimationId.empty() ? "-" : runtimeState.requestedAnimationId)
                                << " sequences=" << describeAnimationSequences(options_)
                                << " frame=" << animationFrameIndex_ + 1
                                << "/" << currentAnimationFrameCount()
                                << " elapsed_ms=" << animationElapsedMs_
                                << " paused=" << (animationPaused_ ? "yes" : "no");
                if (!options_.mechPreset.empty()) {
                    animationStatus << " death_slots="
                                    << destroyedSlotQualityLabel(mw::mech3d::catalogDestroyedSlotQuality(options_.mechPreset));
                }
                if (runtimeState.usesAssemblyFrameDeath) {
                    animationStatus << " death_render=assembly_frame";
                }
                drawOverlayText(12, y, animationStatus.str());
            }

            if (const mw::battle::CombatantSnapshot* battleCombatant = primaryBattleCombatantSnapshot()) {
                y += 18;
                std::ostringstream battleStatus;
                battleStatus << "battle"
                             << "_" << battleViewModeName(options_)
                             << " tick=" << options_.battleSnapshot->tickIndex
                             << " speed=" << std::fixed << std::setprecision(1) << battleCombatant->forwardSpeed
                             << " target=" << battleCombatant->targetForwardSpeed
                             << " speed_cmd=" << battleDriveThrottleCommand_
                             << " body_yaw=" << std::fixed << std::setprecision(1)
                             << (battleCombatant->transform.headingRadians * 180.0 / 3.14159265358979323846)
                             << "deg"
                             << " torso_step=" << battleCombatant->torsoYawStep
                             << " torso_yaw=" << std::fixed << std::setprecision(1)
                             << (battleCombatant->torsoYawRadians * 180.0 / 3.14159265358979323846)
                             << "deg"
                             << " bounds=" << (battleCombatant->boundaryContact ? "touch" : "clear")
                             << " camera_mode=" << battleRuntimeCameraModeName()
                             << " cam_yaw=" << std::fixed << std::setprecision(0) << battleCameraYawOffsetDegrees_
                             << " cam_pitch=" << battleCameraPitchDegrees_
                             << " cam_dist=" << battleCameraDistance_;
                if (options_.battleCockpit) {
                    battleStatus << " bob=" << (options_.battleCockpitBob ? "on" : "off")
                                 << " bob_y=" << std::fixed << std::setprecision(1) << battleCockpitBobOffsetWorldUnits();
                }
                battleStatus << " pos=" << std::fixed << std::setprecision(0)
                             << battleCombatant->transform.x << "," << battleCombatant->transform.z;
                if (options_.battleSnapshot->camera.valid) {
                    battleStatus << " camera_heading=" << std::fixed << std::setprecision(1)
                                 << (options_.battleSnapshot->camera.transform.headingRadians * 180.0 / 3.14159265358979323846)
                                 << "deg";
                }
                drawOverlayText(12, y, battleStatus.str());
            }

            y += 18;
            std::ostringstream assemblyStatus;
            assemblyStatus << "components=" << renderInstance_.assembly.components.size()
                           << " bounds center=" << formatVec3(renderInstance_.assembly.bounds.center)
                           << " r=" << std::fixed << std::setprecision(1) << renderInstance_.assembly.bounds.radius;
            drawOverlayText(12, y, assemblyStatus.str());

            const mw::mech3d::ModelAssemblyComponent* targetComponent = findRenderComponent(poseDemoComponentId_);
            if (targetComponent != nullptr) {
                const mw::mech3d::Vec3f targetPivotLocal = poseDemoPivot(*targetComponent);
                const mw::mech3d::Vec3f targetPivotWorld = transformPoint(targetComponent->worldMatrix, targetPivotLocal);
                y += 18;
                std::ostringstream targetStatus;
                targetStatus << "target " << shortComponentLabel(*targetComponent)
                             << " id=" << targetComponent->componentId
                             << " pivot_mode=" << posePivotModeName(posePivotMode_)
                             << " axis=" << poseDemoAxisName(poseDemoAxis_)
                             << " default_axis=" << componentPoseAxisName(targetComponent->defaultPoseAxis)
                             << " pivot_local=" << formatVec3(targetPivotLocal)
                             << " pivot_world=" << formatVec3(targetPivotWorld);
                drawOverlayText(12, y, targetStatus.str());
            }

            const size_t visibleComponents = std::min<size_t>(renderInstance_.assembly.components.size(), 6u);
            for (size_t i = 0; i < visibleComponents; ++i) {
                const mw::mech3d::ModelAssemblyComponent& component = renderInstance_.assembly.components[i];
                const mw::mech3d::ResolvedMechComponentState componentState =
                    mw::mech3d::resolveMechComponentState(renderInstance_.assembly, runtimeState, component.componentId);
                y += 18;
                std::ostringstream componentStatus;
                componentStatus << "  " << shortComponentLabel(component)
                                << " id=" << component.componentId
                                << " src=" << component.sourceRecordIndex
                                << " state=" << componentStateLabel(runtimeState, componentState)
                                << " axis=" << componentPoseAxisName(component.defaultPoseAxis)
                                << " c=" << formatVec3(component.worldBounds.center)
                                << " r=" << std::fixed << std::setprecision(1) << component.worldBounds.radius
                                << " pivot=" << formatVec3(component.pivot);
                drawOverlayText(12, y, componentStatus.str());
            }
        }

        if (!lastScreenshot_.empty()) {
            y += 18;
            drawOverlayText(12, y, "screenshot: " + narrowPath(std::filesystem::path(lastScreenshot_).filename()));
        }

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        glEnable(GL_DEPTH_TEST);
    }

    void drawOverlayText(int x, int y, const std::string& text) {
        glRasterPos2i(x, y);
        glListBase(fontBase_ - 32);
        glCallLists(static_cast<GLsizei>(text.size()), GL_UNSIGNED_BYTE, reinterpret_cast<const GLubyte*>(text.c_str()));
    }

    void saveScreenshot() {
        const int width = std::max(1, width_);
        const int height = std::max(1, height_);
        std::vector<uint8_t> pixels(static_cast<size_t>(width * height * 3));
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadBuffer(GL_BACK);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
        checkGl("screenshot");

        std::filesystem::path dir = std::filesystem::path("screenshots") / "cpp_battle_viewer";
        std::filesystem::create_directories(dir);
        const std::filesystem::path path = nextScreenshotPath(dir);
        writeBmp24(path, width, height, pixels);
        lastScreenshot_ = path.string();
        updateTitle();
    }

    std::filesystem::path nextScreenshotPath(const std::filesystem::path& dir) {
        std::ostringstream prefix;
        prefix << safeStem(options_.resourcePath)
               << "_record_" << std::setw(3) << std::setfill('0') << options_.recordIndex
               << (options_.assembled ? "_assembled" : "")
               << "_" << options_.shadeMode
               << (options_.wire ? "_wire" : "_solid")
               << (options_.perspective ? "_persp" : "_ortho")
               << "_";

        for (int attempt = 0; attempt < 10000; ++attempt) {
            const int index = screenshotCounter_++;
            std::ostringstream name;
            name << prefix.str() << std::setw(3) << std::setfill('0') << index << ".bmp";
            std::filesystem::path candidate = dir / name.str();
            if (!std::filesystem::exists(candidate)) {
                return candidate;
            }
        }

        std::ostringstream name;
        name << prefix.str() << GetTickCount64() << ".bmp";
        return dir / name.str();
    }

    void updateTitle() {
        const mw::mech3d::ResolvedMechRuntimeState runtimeState = resolvedRuntimeState();
        std::ostringstream title;
        title << "MechWarrior 1989 C++ OpenGL Battle Viewer - "
              << narrowPath(options_.resourcePath.filename())
              << " #" << options_.recordIndex
              << " vbo=" << mesh_.vertexCount
              << " tri=" << mesh_.triangleCount
              << " line=" << mesh_.linePairCount
              << " shade=" << options_.shadeMode
              << (options_.wire ? " wire" : " solid")
              << (options_.perspective ? " perspective" : " ortho");
        if (options_.assembled) {
            title << " assembled=" << joinRecordList(options_.assemblyRecordIndices);
        }
        if (options_.terrainEnabled) {
            if (!options_.terrainLayoutNames.empty()) {
                title << " terrain=layout layout=" << joinStringList(options_.terrainLayoutNames);
                if (options_.terrainScenarioIndex.has_value()) {
                    title << " scenario=" << *options_.terrainScenarioIndex
                          << " mode=" << options_.terrainScenarioMode;
                }
                if (!options_.terrainLayoutPreset.empty()) {
                    title << " preset=" << options_.terrainLayoutPreset;
                }
                title << " gap=" << std::fixed << std::setprecision(0) << options_.terrainLayoutGap;
            } else {
                title << " terrain=" << narrowPath(options_.terrainGridPath.filename());
                if (!options_.terrainArena.empty()) {
                    title << " arena=" << options_.terrainArena;
                }
            }
            title << " terrain_color=" << options_.terrainColorMode
                  << " terpck_edges="
                  << (terrainObjectEdgesVisible(options_.terrainColorMode, options_.wire) ? "on" : "off")
                  << " grd_patches=off"
                  << " terrain_sky=" << options_.terrainSkyMode
                  << " cell=" << std::fixed << std::setprecision(0) << options_.terrainCellSize
                  << " inset=" << std::fixed << std::setprecision(2) << options_.terrainObjectInset
                  << " scale=" << std::fixed << std::setprecision(2) << options_.terrainObjectScale
                  << " mirror_x=" << (options_.terrainTileMirrorX ? "yes" : "no")
                  << " mirror_y=" << (options_.terrainTileMirrorY ? "yes" : "no")
                  << " obj_mirror_y=" << (options_.terrainObjectMirrorY ? "yes" : "no")
                  << " grid_edges=" << (terrainGridEdgesVisible_ ? "on" : "off");
            if (options_.terrainEnvironmentId.has_value()) {
                title << " env_id=" << *options_.terrainEnvironmentId;
            }
        }
        if (options_.animationEnabled) {
            title << " anim=" << describeAnimationSequences(options_)
                  << " frame=" << (animationFrameIndex_ + 1);
            if (!options_.mechPreset.empty()) {
                title << " death_slots="
                      << destroyedSlotQualityLabel(mw::mech3d::catalogDestroyedSlotQuality(options_.mechPreset));
            }
            if (animationPaused_) {
                title << " paused";
            }
        }
        if (runtimeState.mechDestroyed) {
            title << " death";
        }
        if (const mw::battle::CombatantSnapshot* battleCombatant = primaryBattleCombatantSnapshot()) {
            title << " battle_tick=" << options_.battleSnapshot->tickIndex
                  << " torso_step=" << battleCombatant->torsoYawStep
                  << " speed=" << std::fixed << std::setprecision(1) << battleCombatant->forwardSpeed
                  << " x=" << battleCombatant->transform.x
                  << " z=" << battleCombatant->transform.z
                  << " heading=" << (battleCombatant->transform.headingRadians * 180.0 / 3.14159265358979323846)
                  << "deg"
                  << " boundary=" << (battleCombatant->boundaryContact ? "touch" : "clear");
        }
        if (options_.battleDrive) {
            title << " drive camera=" << (battleFollowCamera_ ? "follow" : "orbit")
                  << " cam_yaw=" << std::fixed << std::setprecision(0) << battleCameraYawOffsetDegrees_
                  << " cam_pitch=" << battleCameraPitchDegrees_
                  << " cam_dist=" << battleCameraDistance_;
        }
        if (options_.battleCommandMap) {
            title << " command_map=" << battleCommandMapScreenName(options_.battleCommandMapScreen);
        }
        if (options_.battleCockpit) {
            const CockpitLayout& layout = cockpitLayoutForOptions(options_);
            title << " cockpit family=" << layout.id
                  << " scr=" << layout.scrName
                  << " camera=" << battleRuntimeCameraModeName()
                  << " zoom=x" << battleCockpitZoomLevel_
                  << " bob=" << (options_.battleCockpitBob ? "on" : "off")
                  << " hud_color=" << currentCockpitHudColor().id
                  << " viewport=" << rectSpec(layout.viewport);
        }
        if (!runtimeState.destroyedComponentIds.empty()) {
            title << " destroyed=" << joinRecordList(runtimeState.destroyedComponentIds);
        }
        if (!runtimeState.hiddenComponentIds.empty()) {
            title << " hidden=" << joinRecordList(runtimeState.hiddenComponentIds);
        }
        if (!runtimeState.disabledComponentIds.empty()) {
            title << " disabled=" << joinRecordList(runtimeState.disabledComponentIds);
        }
        if (poseDemo_) {
            title << " pose-demo target=" << poseDemoComponentId_
                  << " axis=" << poseDemoAxisName(poseDemoAxis_)
                  << " pivot=" << posePivotModeName(posePivotMode_)
                  << " amp=" << poseDemoAmplitudeDegrees() << "deg";
        }
        if (!lastScreenshot_.empty()) {
            title << " screenshot=" << narrowPath(std::filesystem::path(lastScreenshot_).filename());
        }
        SetWindowTextW(hwnd_, widenAscii(title.str()).c_str());
    }

    void destroyGlObjects() {
        if (gl_.glDeleteBuffers) {
            if (mesh_.vbo) {
                gl_.glDeleteBuffers(1, &mesh_.vbo);
            }
            if (mesh_.triEbo) {
                gl_.glDeleteBuffers(1, &mesh_.triEbo);
            }
            if (mesh_.lineEbo) {
                gl_.glDeleteBuffers(1, &mesh_.lineEbo);
            }
        }
        if (fontBase_ != 0) {
            glDeleteLists(fontBase_, 96);
            fontBase_ = 0;
        }
        if (cockpitTexture_ != 0) {
            glDeleteTextures(1, &cockpitTexture_);
            cockpitTexture_ = 0;
            cockpitBackdropReady_ = false;
        }
        for (CockpitSpriteTexture& texture : cockpitStrutTextures_) {
            if (texture.texture != 0) {
                glDeleteTextures(1, &texture.texture);
                texture.texture = 0;
            }
        }
        cockpitStrutTextures_.clear();
        cockpitStrutsReady_ = false;
        for (CockpitSpriteTexture& texture : cockpitWidgetTextures_) {
            if (texture.texture != 0) {
                glDeleteTextures(1, &texture.texture);
                texture.texture = 0;
            }
        }
        cockpitWidgetTextures_.clear();
        cockpitWidgetsReady_ = false;
        for (CockpitSpriteTexture& texture : cockpitHudNumberTextures_) {
            if (texture.texture != 0) {
                glDeleteTextures(1, &texture.texture);
                texture.texture = 0;
            }
        }
        cockpitHudNumberTextures_.clear();
        cockpitHudNumbersReady_ = false;
        cockpitHudFont_.reset();
        if (hrc_) {
            wglMakeCurrent(nullptr, nullptr);
            wglDeleteContext(hrc_);
            hrc_ = nullptr;
        }
        if (hwnd_ && hdc_) {
            ReleaseDC(hwnd_, hdc_);
            hdc_ = nullptr;
        }
    }

    Options options_;
    std::vector<mw::legacy3d::RuntimeRecord> records_;
    size_t currentRecordPos_ = 0;
    mw::legacy3d::GpuBatch batch_;
    mw::mech3d::MechModelDefinition bindDefinition_;
    mw::mech3d::ModelAnimationDefinition animationDefinition_;
    mw::mech3d::ModelAssemblyFrameAnimationDefinition assemblyFrameAnimationDefinition_;
    mw::mech3d::MechRenderInstance renderInstance_;
    mw::legacy3d::TerrainMesh terrainMesh_;
    mw::legacy3d::TerrainWorld terrainWorld_;
    mw::legacy3d::GpuBatch terrainObjectBatch_;
    mw::legacy3d::GpuBatch terrainObjectDitherBatch_;
    mw::legacy3d::GpuBatch battleObjectiveModelBatch_;
    std::optional<Bounds> battleObjectiveModelBounds_;
    std::vector<LoadedBattleCombatantVisual> battleNonPlayerCombatantVisuals_;
    std::vector<mw::legacy3d::RuntimeRecord> terrainRecords_;
    std::vector<std::string> terrainArenaNames_;
    size_t currentTerrainArenaPos_ = 0;
    std::vector<size_t> terrainScenarioIndices_;
    size_t currentTerrainScenarioPos_ = 0;
    Bounds bounds_;
    std::optional<Bounds> battlePlacementBounds_;
    Vec3 mechDrawOffset_{};
    float mechHeadingRadians_ = 0.0f;
    bool battleMechTransformActive_ = false;
    GlFunctions gl_{};
    GpuMesh mesh_{};
    CockpitScrImage cockpitBackdropImage_{};
    std::array<float, 3> battlefieldBoundaryOrdinaryRgb_{0.33f, 1.0f, 1.0f};
    std::array<float, 3> battlefieldBoundaryAllowedRgb_{1.0f, 1.0f, 1.0f};
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hrc_ = nullptr;
    int width_ = 1;
    int height_ = 1;
    float yaw_ = -30.0f;
    float pitch_ = 18.0f;
    float distance_ = 1000.0f;
    float panX_ = 0.0f;
    float panY_ = 0.0f;
    bool dragging_ = false;
    bool panning_ = false;
    bool showOverlay_ = true;
    bool showComponentDebug_ = true;
    bool terrainGridEdgesVisible_ = true;
    bool poseDemo_ = false;
    int poseDemoComponentId_ = 1;
    PoseDemoAxis poseDemoAxis_ = PoseDemoAxis::Y;
    PosePivotMode posePivotMode_ = PosePivotMode::Definition;
    float poseDemoAmplitudeRadians_ = 0.45f;
    float poseTime_ = 0.0f;
    int animationElapsedMs_ = 0;
    int animationFrameIndex_ = 0;
    bool animationPaused_ = false;
    bool battleFollowCamera_ = true;
    bool battleCameraDragging_ = false;
    float battleCameraYawOffsetDegrees_ = 0.0f;
    float battleCameraPitchDegrees_ = 28.0f;
    float battleCameraDistance_ = 3200.0f;
    double battleDriveAccumulator_ = 0.0;
    double battleDriveThrottleCommand_ = 0.0;
    int battleCockpitZoomLevel_ = kBattleCockpitMinZoomLevel;
    int pendingDriveTorsoYawStepDelta_ = 0;
    int pendingDriveAimPitchStepDelta_ = 0;
    bool pendingJumpJetToggle_ = false;
    int screenshotRequests_ = 0;
    GLuint fontBase_ = 0;
    GLuint cockpitTexture_ = 0;
    bool cockpitBackdropReady_ = false;
    std::vector<CockpitSpriteTexture> cockpitStrutTextures_;
    bool cockpitStrutsReady_ = false;
    std::vector<CockpitSpriteTexture> cockpitWidgetTextures_;
    bool cockpitWidgetsReady_ = false;
    std::vector<CockpitSpriteTexture> cockpitHudNumberTextures_;
    bool cockpitHudNumbersReady_ = false;
    std::optional<CockpitFont> cockpitHudFont_;
    size_t cockpitHudColorIndex_ = kDefaultCockpitHudColorIndex;
    int lastMouseX_ = 0;
    int lastMouseY_ = 0;
    int screenshotCounter_ = 0;
    std::string lastScreenshot_;
};

int runSmoke(
    const Options& options,
    const std::vector<mw::legacy3d::RuntimeRecord>& records,
    const mw::legacy3d::GpuBatch& batch) {
    validateCockpitLayoutSmokeContracts();
    const mw::mech3d::ResolvedMechRuntimeState runtimeState = resolveCliRuntimeState(options);
    mw::legacy3d::GpuBatchOptions objectiveBatchOptions;
    objectiveBatchOptions.shadeMode = options.shadeMode;
    objectiveBatchOptions.edgeRgb = options.wire ? std::array<float, 3>{0.86f, 0.86f, 0.86f}
                                                 : std::array<float, 3>{0.02f, 0.02f, 0.02f};
    const std::optional<LoadedBattleObjectiveModel> objectiveModel =
        loadBattleObjectiveModelAsset(options, objectiveBatchOptions);
    const std::vector<LoadedBattleCombatantVisual> nonPlayerVisuals =
        buildBattleNonPlayerCombatantVisuals(options, objectiveBatchOptions);
    const std::vector<int> sourceIndices = smokeSourceIndices(options);
    size_t sourceVertices = 0;
    size_t sourceParts = 0;
    int rawPolygons = 0;
    int rawLines = 0;
    for (int recordIndex : sourceIndices) {
        const mw::legacy3d::RuntimeRecord& record = selectRecord(records, recordIndex);
        const mw::legacy3d::RecordStats stats = mw::legacy3d::recordStats(record);
        sourceVertices += record.vertices.size();
        sourceParts += record.parts.size();
        rawPolygons += stats.polygonCount;
        rawLines += stats.lineCount;
    }
    std::cout
        << "mw_battle_viewer smoke: ok"
        << " resource=" << narrowPath(options.resourcePath)
        << " record=" << options.recordIndex;
    if (options.assembled) {
        std::cout << " assembled=" << joinRecordList(sourceIndices);
    }
    if (!options.mechPreset.empty()) {
        std::cout << " preset=" << options.mechPreset
                  << " death_slots="
                  << destroyedSlotQualityLabel(mw::mech3d::catalogDestroyedSlotQuality(options.mechPreset));
    }
    if (!options.destroyedComponentIds.empty()) {
        std::cout << " destroyed_components=" << joinRecordList(options.destroyedComponentIds);
    }
    if (!runtimeState.hiddenComponentIds.empty()) {
        std::cout << " hidden_components=" << joinRecordList(runtimeState.hiddenComponentIds);
    }
    if (!runtimeState.disabledComponentIds.empty()) {
        std::cout << " disabled_components=" << joinRecordList(runtimeState.disabledComponentIds);
    }
    if (runtimeState.mechDestroyed) {
        std::cout << " mech_destroyed=yes";
    }
    if (options.animationEnabled) {
        std::cout
            << " animation_sequences=" << describeAnimationSequences(options)
            << " active_animation=" << (runtimeState.activeAnimationId.empty() ? "-" : runtimeState.activeAnimationId);
    }
    if (options.battleSnapshot.has_value()) {
        std::cout
            << " battle_snapshot=yes"
            << " battle_drive=" << (options.battleDrive ? "yes" : "no")
            << " battle_cockpit=" << (options.battleCockpit ? "yes" : "no")
            << " battle_view=" << battleViewModeName(options)
            << " battle_ticks=" << options.battleSnapshot->tickIndex
            << " battle_elapsed_ms=" << options.battleSnapshot->elapsedMs
            << " battle_hash=" << std::hex << mw::battle::battleSnapshotFingerprint(*options.battleSnapshot) << std::dec;
        if (battleInteractiveMode(options) && options.battleSimTicks.has_value()) {
            std::cout << " battle_replay_match=" << (options.battleDriveReplayMatch ? "yes" : "no");
            if (options.battleDrive) {
                std::cout << " battle_drive_replay_match=" << (options.battleDriveReplayMatch ? "yes" : "no");
            }
        }
        if (options.battleSnapshot->mission.valid) {
            const mw::battle::BattleMissionBriefing& mission = options.battleSnapshot->mission;
            std::cout
                << " battle_mission=yes"
                << " battle_mission_source=" << options.battleMissionSource
                << " battle_mission_catalog=MW_MAIN.EXE:34_titles"
                << " battle_mission_id=" << static_cast<int>(mission.originalId)
                << " battle_mission_family=" << mw::battle::battleMissionFamilyName(mission.family)
                << " battle_mission_extended=" << (mission.extended ? "yes" : "no")
                << " battle_mission_title=" << mw::battle::battleMissionTitleToken(mission);
        } else {
            std::cout << " battle_mission=no";
        }
        std::cout << " battle_placement="
                  << (options.battleOriginalPlacement ? "original_btech_snario" : "manual");
        if (options.battleSnapshot->setup.valid) {
            const mw::battle::BattleSetupMetadata& setup = options.battleSnapshot->setup;
            const mw::battle::BattleSetupSlotMetadata& playerPos = setup.playerSlots.front();
            const mw::battle::BattleSetupSlotMetadata& enemyPos = setup.opposingSlots.front();
            const mw::battle::BattleObjectiveState& objective = options.battleSnapshot->objective;
            const double targetGridX = objective.valid
                ? objective.gridX
                : setup.opposingSlots[setup.objectiveOpposingSlotIndex].gridX;
            const double targetGridY = objective.valid
                ? objective.gridY
                : setup.opposingSlots[setup.objectiveOpposingSlotIndex].gridY;
            std::cout
                << " battle_setup=original_battlefield"
                << " battle_setup_provenance=" << setup.provenance
                << " battle_setup_tiles=" << joinStringList(setup.tileNames)
                << " battle_setup_terrain_mode=" << static_cast<int>(setup.terrainMode)
                << " battle_setup_player_slot=" << setup.playerSlotIndex
                << " battle_setup_opposing_slots=" << setup.opposingSlots.size()
                << " battle_setup_objective_slot=" << setup.objectiveSourceSlot
                << " battle_placement_seed=" << setup.scenarioIndex
                << " battle_briefing_mission_id=" << setup.briefingMissionId
                << " battle_handoff_mission_id=" << static_cast<int>(setup.handoffMissionId)
                << " battle_initial_placement_selector=" << setup.initialPlacementSelector
                << " battle_placement_selector=" << setup.missionSelector
                << " battle_placement_selector_runtime_resolved="
                << (setup.placementSelectorRuntimeResolved ? "yes" : "no")
                << " battle_placement_selector_provenance=" << setup.missionSelectorProvenance
                << " battle_placement_player_mode=" << setup.playerMode
                << " battle_placement_opposing_mode=" << setup.opposingMode
                << " battle_placement_player_bank=" << static_cast<int>(setup.playerBankId)
                << " battle_placement_opposing_bank=" << static_cast<int>(setup.opposingBankId)
                << std::fixed << std::setprecision(2)
                << " battle_placement_player_grid=" << playerPos.gridX << "," << playerPos.gridY
                << " battle_placement_enemy_grid=" << enemyPos.gridX << "," << enemyPos.gridY
                << " battle_placement_target_grid=" << targetGridX << "," << targetGridY
                << " battle_placement_player_slots=" << battleSetupSlotGridList(setup.playerSlots)
                << " battle_placement_opposing_slots=" << battleSetupSlotGridList(setup.opposingSlots)
                << std::setprecision(4)
                << " battle_setup_initial_facing=" << setup.initialPlayerTransform.headingRadians
                << std::defaultfloat;
            if (setup.dedicatedObjective.valid) {
                const mw::battle::BattleSetupObjectiveMetadata& dedicated = setup.dedicatedObjective;
                std::cout
                    << " battle_setup_dedicated_objective=yes"
                    << " battle_setup_dedicated_objective_provenance=" << dedicated.provenance
                    << " battle_setup_dedicated_objective_side="
                    << (dedicated.sourceSide == 0 ? "player" : "opposing")
                    << " battle_setup_dedicated_objective_mode=" << dedicated.placementMode
                    << " battle_setup_dedicated_objective_bank=" << static_cast<int>(dedicated.bankId)
                    << " battle_setup_dedicated_objective_offset=" << dedicated.coordinateOffset
                    << " battle_setup_dedicated_objective_target_index=" << dedicated.legacyTargetIndex
                    << " battle_setup_dedicated_objective_record=" << dedicated.resourceRecordIndex
                    << std::fixed << std::setprecision(2)
                    << " battle_setup_dedicated_objective_grid=" << dedicated.gridX << "," << dedicated.gridY
                    << std::defaultfloat;
            } else {
                std::cout << " battle_setup_dedicated_objective=no";
            }
        }
        if (options.battleSnapshot->battlefieldBoundary.valid) {
            const mw::battle::BattlefieldBoundaryState& boundary =
                options.battleSnapshot->battlefieldBoundary;
            std::cout
                << " battle_perimeter=yes"
                << " battle_perimeter_provenance=" << boundary.provenance
                << " battle_perimeter_original_x=" << boundary.originalMinX << ".." << boundary.originalMaxX
                << " battle_perimeter_original_z=" << boundary.originalMinZ << ".." << boundary.originalMaxZ
                << std::fixed << std::setprecision(2)
                << " battle_perimeter_world_x=" << boundary.worldMinX << ".." << boundary.worldMaxX
                << " battle_perimeter_world_z=" << boundary.worldMinZ << ".." << boundary.worldMaxZ
                << std::defaultfloat
                << " battle_perimeter_map_projection=" << (boundary.mapProjectionProven ? "proven" : "unproven")
                << " battle_perimeter_exit_status=-2:north,-3:south,-4:west,-5:east"
                << " battle_perimeter_exit_bits=1:north,2:south,4:west,8:east"
                << " battle_perimeter_player_allowed_mask=" << static_cast<int>(boundary.playerAllowedExitMask)
                << " battle_perimeter_opposing_allowed_mask=" << static_cast<int>(boundary.opposingAllowedExitMask)
                << " battle_perimeter_side_completion_exit_policy="
                << (boundary.sideCompletionExitPolicyProven ? "proven" : "unproven")
                << " battle_perimeter_ordinary_player_result=" << boundary.ordinaryPlayerExitResultCode
                << " battle_perimeter_allowed_player_result=" << boundary.allowedPlayerExitResultCode
                << " battle_perimeter_outcome_evaluator="
                << (boundary.outcomeEvaluationDeferred ? "deferred" : "active");
        } else {
            std::cout << " battle_perimeter=no";
        }
        if (options.battleSnapshot->objective.valid) {
            const mw::battle::BattleObjectiveState& objective = options.battleSnapshot->objective;
            std::cout
                << " battle_objective=yes"
                << " battle_objective_role=" << objective.role
                << " battle_objective_provenance=" << objective.provenance
                << " battle_objective_slot=" << objective.sourceSlot
                << " battle_objective_mission_intent="
                << mw::battle::battleMissionObjectiveIntentName(objective.missionIntent)
                << " battle_objective_mission_intent_proven=" << (objective.missionIntentProven ? "yes" : "no")
                << " battle_objective_mission_target="
                << (objective.missionTargetKind.empty() ? "unknown" : objective.missionTargetKind)
                << " battle_objective_mission_intent_provenance="
                << (objective.missionIntentProvenance.empty() ? "none" : objective.missionIntentProvenance)
                << " battle_objective_mission_intent_bound="
                << (objective.missionIntentBoundToObjective ? "yes" : "no")
                << " battle_objective_transform_proven=" << (objective.transformProven ? "yes" : "no")
                << " battle_objective_active_object_proven=" << (objective.activeObjectProven ? "yes" : "no")
                << " battle_objective_mission_semantics_proven=" << (objective.missionSemanticsProven ? "yes" : "no")
                << " battle_objective_damage_policy_proven=" << (objective.damagePolicyProven ? "yes" : "no")
                << " battle_objective_damage_suppressed=" << (objective.damageSuppressed ? "yes" : "no")
                << " battle_objective_depletion_policy_proven=" << (objective.depletionPolicyProven ? "yes" : "no")
                << " battle_objective_depletion_sets_player_win=" << (objective.depletionSetsPlayerWinCondition ? "yes" : "no")
                << " battle_objective_depletion_sets_player_loss=" << (objective.depletionSetsPlayerLossCondition ? "yes" : "no")
                << " battle_objective_depletion_result_code=" << objective.depletionResultCode
                << std::fixed << std::setprecision(2)
                << " battle_objective_grid=" << objective.gridX << "," << objective.gridY
                << std::setprecision(4)
                << " battle_objective_x=" << objective.transform.x
                << " battle_objective_z=" << objective.transform.z
                << std::defaultfloat;
            if (objective.staticModel.valid) {
                std::cout
                    << " battle_objective_model=" << objective.staticModel.resourceName
                    << ":" << objective.staticModel.recordIndex
                    << " battle_objective_model_provenance=" << objective.staticModel.provenance
                    << " battle_objective_model_animation=" << (objective.staticModel.animated ? "animated" : "static")
                    << " battle_objective_model_swizzle=" << objective.staticModel.swizzle
                    << " battle_objective_model_scale=" << objective.staticModel.scale
                    << " battle_objective_model_ground=" << (objective.staticModel.groundAligned ? "terrain_sample" : "transform_y");
                if (objectiveModel.has_value()) {
                    std::cout
                        << " battle_objective_model_gpu_vertices=" << objectiveModel->batch.vertices.size() / 6u
                        << " battle_objective_model_gpu_triangles=" << objectiveModel->batch.triangleIndices.size() / 3u
                        << " battle_objective_model_gpu_line_pairs=" << objectiveModel->batch.lineIndices.size() / 2u;
                }
            }
        } else {
            std::cout << " battle_objective=no";
        }
        if (!options.battleSnapshot->combatants.empty()) {
            const mw::battle::CombatantSnapshot& combatant = options.battleSnapshot->combatants.front();
            std::cout
                << " battle_preset=" << combatant.mechPresetId
                << " battle_team=" << mw::battle::battleTeamName(combatant.roster.team)
                << " battle_faction="
                << (combatant.roster.factionHouseName.empty() ? "none" : combatant.roster.factionHouseName)
                << " battle_roster_provenance=" << combatant.roster.provenance
                << " battle_roster_slot=" << combatant.roster.sourceSlot
                << " battle_max_speed_kph=" << combatant.originalMaxSpeedKph
                << " battle_max_forward_world=" << combatant.maxForwardSpeed
                << " battle_max_reverse_world=" << combatant.maxReverseSpeed
                << " battle_active_animation=" << combatant.activeAnimationId
                << " battle_walk_ms=" << combatant.walkAnimationElapsedMs
                << " battle_boundary=" << (combatant.boundaryContact ? "yes" : "no")
                << " battle_airborne=" << (combatant.airborne ? "yes" : "no")
                << " battle_jumpjets=" << (combatant.jumpJetsEnabled ? "on" : "off")
                << " battle_jump_capable=" << (combatant.jumpCapable ? "yes" : "no")
                << " battle_jump_capacity_m=" << combatant.jumpCapacityMeters
                << " battle_jump_jet_count=" << combatant.jumpJetCount
                << " battle_jump_ready=" << (combatant.jumpJetReady ? "yes" : "no")
                << " battle_jump_thrust=" << (combatant.jumpJetThrusting ? "yes" : "no")
                << " battle_jump_fuel=" << combatant.jumpFuel
                << " battle_vertical_speed=" << combatant.verticalSpeed
                << " battle_hard_landing=" << (combatant.hardLanding ? "yes" : "no")
                << " battle_landing_impact_speed=" << combatant.landingImpactSpeed
                << " battle_knockdown_candidate=" << (combatant.knockdownCandidate ? "yes" : "no")
                << " battle_torso_step=" << combatant.torsoYawStep
                << " battle_aim_pitch_step=" << combatant.aimPitchStep
                << std::fixed << std::setprecision(4)
                << " battle_torso_yaw=" << combatant.torsoYawRadians
                << " battle_x=" << combatant.transform.x
                << " battle_z=" << combatant.transform.z
                << " battle_heading=" << combatant.transform.headingRadians
                << " battle_speed=" << combatant.forwardSpeed
                << " battle_target_speed=" << combatant.targetForwardSpeed
                << std::defaultfloat;
        }
        size_t playerCombatants = 0;
        size_t nonPlayerCombatants = 0;
        size_t opposingCombatants = 0;
        size_t neutralCombatants = 0;
        for (const mw::battle::CombatantSnapshot& combatant : options.battleSnapshot->combatants) {
            if (combatant.playerControlled) {
                ++playerCombatants;
            } else {
                ++nonPlayerCombatants;
            }
            if (combatant.roster.team == mw::battle::BattleTeam::Opposing) {
                ++opposingCombatants;
            } else if (combatant.roster.team == mw::battle::BattleTeam::Neutral) {
                ++neutralCombatants;
            }
        }
        std::cout
            << " battle_combatants=" << options.battleSnapshot->combatants.size()
            << " battle_player_combatants=" << playerCombatants
            << " battle_nonplayer_combatants=" << nonPlayerCombatants
            << " battle_opposing_combatants=" << opposingCombatants
            << " battle_neutral_combatants=" << neutralCombatants;
        if (nonPlayerCombatants > 0) {
            const auto enemyIt = std::find_if(
                options.battleSnapshot->combatants.begin(),
                options.battleSnapshot->combatants.end(),
                [](const mw::battle::CombatantSnapshot& combatant) {
                    return combatant.roster.team == mw::battle::BattleTeam::Opposing;
                });
            if (enemyIt != options.battleSnapshot->combatants.end()) {
                std::cout
                    << " battle_enemy_team=" << mw::battle::battleTeamName(enemyIt->roster.team)
                    << " battle_enemy_faction="
                    << (enemyIt->roster.factionHouseName.empty() ? "none" : enemyIt->roster.factionHouseName)
                    << " battle_enemy_roster_provenance=" << enemyIt->roster.provenance
                    << " battle_enemy_roster_slot=" << enemyIt->roster.sourceSlot
                    << std::fixed << std::setprecision(4)
                    << " battle_enemy_x=" << enemyIt->transform.x
                    << " battle_enemy_z=" << enemyIt->transform.z
                    << std::defaultfloat;
            }
        }
        size_t nonPlayerVisualVertices = 0;
        size_t nonPlayerVisualTriangles = 0;
        std::ostringstream nonPlayerVisualPresets;
        for (size_t i = 0; i < nonPlayerVisuals.size(); ++i) {
            const LoadedBattleCombatantVisual& visual = nonPlayerVisuals[i];
            if (i > 0) {
                nonPlayerVisualPresets << ";";
            }
            nonPlayerVisualPresets << visual.entityId.value << ":" << visual.mechPresetId;
            nonPlayerVisualVertices += visual.renderInstance.batch.vertices.size() / 6u;
            nonPlayerVisualTriangles += visual.renderInstance.batch.triangleIndices.size() / 3u;
        }
        std::cout
            << " battle_world_combatant_visuals=battle_snapshot_combatants_v0"
            << " battle_nonplayer_visuals=" << nonPlayerVisuals.size()
            << " battle_nonplayer_visual_presets="
            << (nonPlayerVisuals.empty() ? "none" : nonPlayerVisualPresets.str())
            << " battle_nonplayer_visual_gpu_vertices=" << nonPlayerVisualVertices
            << " battle_nonplayer_visual_gpu_triangles=" << nonPlayerVisualTriangles;
        if (options.battleCommandMap) {
            const BattleCommandMapScreen screen = battleCommandMapActive(options)
                                                      ? options.battleCommandMapScreen
                                                      : BattleCommandMapScreen::MissionStatus;
            const std::string scrName = battleCommandMapScrName(screen);
            const ScreenRect mapRect = battleCommandMapRect(screen);
            const CockpitScrImage backdrop = loadCockpitScrImage(
                options.terrainRoot / "SCR" / scrName,
                options.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options));
            const std::pair<double, double> targetWorld = snapshotObjectiveWorldOrFallback(options);
            const bool commandMapPerimeter =
                options.battleSnapshot->battlefieldBoundary.valid &&
                options.battleSnapshot->battlefieldBoundary.mapProjectionProven;
            const BattlefieldBoundaryPaletteIndices boundaryIndices =
                battlefieldBoundaryPaletteIndices(options);
            const BattlefieldBoundaryPaletteIndices boundarySourceIndices =
                battlefieldBoundarySourcePaletteIndices(options);
            const uint8_t allowedExitMask =
                commandMapPerimeter ? battlefieldMapAllowedExitMask(*options.battleSnapshot) : 0;
            std::cout
                << " battle_command_map=yes"
                << " battle_command_map_screen=" << battleCommandMapScreenName(screen)
                << " battle_command_map_scr=" << scrName
                << " battle_command_map_backdrop=scr"
                << " battle_command_map_backdrop_renderer=raster_scr"
                << " battle_command_map_palette=" << backdrop.paletteName << ":ega_bank0_low"
                << " battle_command_map_backdrop_codec=" << backdrop.codec
                << " battle_command_map_backdrop_size=" << backdrop.width << "x" << backdrop.height
                << " battle_command_map_rect=" << rectSpec(mapRect)
                << " battle_command_map_mission_text="
                << (screen == BattleCommandMapScreen::MissionStatus ? "STATUS.SCR:YOUR_NEXT_MISSION" : "hidden")
                << " battle_command_map_orientation=north_up"
                << " battle_command_map_projection=absolute_battlefield"
                << " battle_command_map_ground=" << commandMapGroundName(options.terrainColorMode)
                << " battle_command_map_terrain=TERPCK:polygons+dither"
                << " battle_command_map_green_dither=ground_contrast_black"
                << " battle_command_map_fit="
                << (commandMapPerimeter ? "snapshot_btech_perimeter" : "terrain_mesh_plus_terpck_bounds")
                << " battle_command_map_clip=" << (commandMapPerimeter ? "map_rect" : "none")
                << " battle_command_map_perimeter="
                << (commandMapPerimeter ? "runtime_dashed:snapshot_boundary" : "none")
                << " battle_command_map_perimeter_scale="
                << (commandMapPerimeter ? "one_grd_cell_per_virtual_pixel" : "none")
                << " battle_command_map_perimeter_palette_indices="
                << static_cast<int>(boundarySourceIndices.ordinary) << ","
                << static_cast<int>(boundarySourceIndices.allowed)
                << " battle_command_map_perimeter_visual_indices="
                << static_cast<int>(boundaryIndices.ordinary) << ","
                << static_cast<int>(boundaryIndices.allowed)
                << " battle_command_map_perimeter_segment_clip=boundary_edges"
                << " battle_command_map_perimeter_exit_mask=" << static_cast<int>(allowedExitMask)
                << " battle_command_map_perimeter_phase="
                << (commandMapPerimeter ? "snapshot_tick_mod8" : "none")
                << " battle_command_map_blips=battle_snapshot_combatants_v0"
                << " battle_command_map_player_blip=white"
                << " battle_command_map_player_spawn_aim="
                << (options.battleStartHeadingRadians.has_value() ? "explicit" : "target")
                << " battle_command_map_enemy_blip=red"
                << " battle_command_map_target_blip=cyan:snapshot_objective"
                << " battle_command_map_target_provenance="
                << (options.battleSnapshot->objective.valid ? options.battleSnapshot->objective.provenance : "fallback_diagnostic")
                << std::fixed << std::setprecision(4)
                << " battle_command_map_target_x=" << targetWorld.first
                << " battle_command_map_target_z=" << targetWorld.second
                << std::defaultfloat;
        }
        if (options.battleSnapshot->camera.valid) {
            const mw::battle::CameraSnapshot& camera = options.battleSnapshot->camera;
            std::cout
                << " battle_camera=yes"
                << " battle_camera_entity=" << camera.attachedEntityId.value
                << std::fixed << std::setprecision(4)
                << " battle_camera_x=" << camera.transform.x
                << " battle_camera_y=" << camera.transform.y
                << " battle_camera_z=" << camera.transform.z
                << " battle_camera_heading=" << camera.transform.headingRadians
                << " battle_camera_forward=" << camera.localForwardOffset
                << " battle_camera_height=" << camera.localHeight
                << " battle_camera_height_source=catalog_cockpit_bounds_center"
                << std::defaultfloat;
        }
        if (options.battleDrive) {
            constexpr double pi = 3.14159265358979323846;
            std::cout
                << " battle_view_camera=follow"
                << std::fixed << std::setprecision(4)
                << " battle_view_camera_yaw=" << options.battleCameraYawOffsetDegrees
                << " battle_view_camera_pitch=" << options.battleCameraPitchDegrees
                << " battle_view_camera_distance=" << options.battleCameraDistance
                << " battle_drive_max_forward=" << options.battleDriveMaxForwardSpeed
                << " battle_drive_max_reverse=" << options.battleDriveMaxReverseSpeed
                << " battle_drive_accel=" << options.battleDriveAcceleration
                << " battle_drive_decel=" << options.battleDriveDeceleration
                << " battle_drive_turn_rate_deg=" << (options.battleDriveTurnRateRadians * 180.0 / pi)
                << std::defaultfloat;
        }
        if (options.battleCockpit) {
            const CockpitHudState hud = cockpitHudStateFromOptions(options);
            const CockpitLayout& layout = cockpitLayoutForOptions(options);
            std::cout
                << " battle_view_camera=" << (options.battleCockpitExternalCamera ? "external" : "cockpit")
                << std::fixed << std::setprecision(4)
                << " battle_view_camera_yaw=" << options.battleCameraYawOffsetDegrees
                << " battle_view_camera_pitch=" << options.battleCameraPitchDegrees
                << " battle_view_camera_distance=" << options.battleCameraDistance
                << std::defaultfloat
                << " battle_cockpit_family=" << layout.id
                << " battle_cockpit_scr=" << layout.scrName;
            if (options.battleCockpitExternalCamera) {
                std::cout
                    << " battle_cockpit_backdrop=hidden"
                    << " battle_cockpit_struts=hidden"
                    << " battle_cockpit_system_indicators=hidden"
                    << " battle_compositor_viewport=full"
                    << " battle_compositor_panels=hidden";
            } else {
                const CockpitScrImage backdrop = loadCockpitScrImage(
                    options.terrainRoot / "SCR" / layout.scrName,
                    options.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options));
                std::cout
                    << " battle_cockpit_backdrop=scr"
                    << " battle_cockpit_palette=" << backdrop.paletteName << ":ega_bank0_low"
                    << " battle_cockpit_backdrop_codec=" << backdrop.codec
                    << " battle_cockpit_backdrop_layout=" << backdrop.decodedLayout
                    << " battle_cockpit_backdrop_size=" << backdrop.width << "x" << backdrop.height
                    << " battle_compositor_viewport=" << rectSpec(layout.viewport)
                    << " battle_compositor_panels="
                    << rectSpec(layout.leftPanel) << ";"
                    << rectSpec(layout.centerPanel) << ";"
                    << rectSpec(layout.rightPanel) << ";"
                    << rectSpec(layout.compassPanel);
                if (cockpitFamilyUsesSharedHud(layout.family)) {
                    const std::vector<CockpitBmpSprite> struts = loadCockpitBmpSprites(
                        options.terrainRoot / "BMP" / "STRUTS.BMP",
                        options.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options),
                        0u);
                    std::cout
                        << " battle_cockpit_struts=STRUTS.BMP:"
                        << cockpitStrutOverlaySpec(layout.family)
                        << " battle_cockpit_strut_placements=" << cockpitStrutPlacementSpec(layout.family)
                        << " battle_cockpit_strut_records=" << struts.size()
                        << " battle_cockpit_strut_transparency=index0";
                    const std::vector<CockpitBmpSprite> widgets = loadCockpitBmpSprites(
                        options.terrainRoot / "BMP" / "COCKPIT.BMP",
                        options.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options),
                        0u);
                    const std::vector<CockpitBmpSprite> hudNumbers = loadCockpitBmpSprites(
                        options.terrainRoot / "BMP" / "HUD_NUMS.BMP",
                        options.terrainRoot / "PAL" / cockpitPaletteNameForOptions(options),
                        0u);
                    const CockpitFont font = loadCockpitFont(options.terrainRoot / "FNT" / "6X6B.FNT");
                    const CockpitSystemIndicatorDamage systemDamage =
                        cockpitSystemIndicatorDamageFromSnapshot(options.battleSnapshot);
                    const CockpitHudColor& defaultHudColor = cockpitHudColorCycle()[kDefaultCockpitHudColorIndex];
                    const LightCockpitSpeedGaugeState speedGauge =
                        lightCockpitSpeedGaugeState(hud);
                    const BattlefieldBoundaryPaletteIndices boundaryIndices =
                        battlefieldBoundaryPaletteIndices(options);
                    std::cout
                        << " battle_cockpit_system_indicators=COCKPIT.BMP:0-fill:S,G,E,L"
                        << " battle_cockpit_widget_records=" << widgets.size()
                        << " battle_cockpit_indicator_font=6X6B.FNT:"
                        << font.width << "x" << font.height
                        << ":first=" << font.firstCode
                        << ":glyphs=" << font.glyphCount
                        << " battle_cockpit_system_damage=" << cockpitSystemDamageSpec(systemDamage)
                        << " battle_cockpit_system_damage_source=battle_snapshot_components_v0"
                        << " battle_cockpit_compass=HUD_NUMS.BMP:" << hudNumbers.size() << ":30deg"
                        << " battle_cockpit_compass_markers=COCKPIT.BMP:3,2"
                        << " battle_cockpit_compass_center=" << compassMajorLabel(compassMajorIndexForHeading(hud.bodyHeadingDegrees))
                        << " battle_cockpit_compass_heading_deg=" << (static_cast<int>(std::lround(hud.bodyHeadingDegrees)) % 360)
                        << " battle_cockpit_torso_marker_deg=" << static_cast<int>(std::lround(hud.torsoDegrees))
                        << " battle_cockpit_zoom=1"
                        << " battle_cockpit_zoom_key=Z"
                        << " battle_cockpit_zoom_projection=frustum_scale"
                        << " battle_cockpit_bob=" << (options.battleCockpitBob ? "vertical_camera_y" : "off")
                        << " battle_cockpit_bob_key=B"
                        << " battle_cockpit_bob_cycle_ms="
                        << static_cast<int>(mw::battle::battleCockpitBobPrototypeTuning().cycleMs)
                        << " battle_cockpit_bob_max_y="
                        << static_cast<int>(mw::battle::battleCockpitBobPrototypeTuning().maxWorldUnits)
                        << " battle_cockpit_hud_text_font=6X6B.FNT:AWS,ZOOM"
                        << " battle_cockpit_crosshair=" << layout.scrName << ":4x5_gap3_step3"
                        << " battle_cockpit_crosshair_range=-9..9"
                        << " battle_cockpit_crosshair_step=" << hud.aimPitchStep
                        << " battle_cockpit_crosshair_visible=yes"
                        << " battle_cockpit_minimap=" << layout.scrName << ":center_mfd"
                        << " battle_cockpit_minimap_rect=" << rectSpec(kLightCockpitMinimapRect)
                        << " battle_cockpit_minimap_orientation=north_up"
                        << " battle_cockpit_minimap_projection=status_map_aligned"
                        << " battle_cockpit_minimap_center=player"
                        << " battle_cockpit_minimap_terrain=GRD:raw_band"
                        << " battle_cockpit_minimap_grd=visible"
                        << " battle_cockpit_minimap_blips=battle_snapshot_combatants_v0"
                        << " battle_cockpit_minimap_player_blip=black"
                        << " battle_cockpit_minimap_enemy_blip=red"
                        << " battle_cockpit_minimap_target_blip="
                        << (options.battleSnapshot->objective.valid ? "cyan:snapshot_objective" : "none")
                        << " battle_cockpit_minimap_perimeter="
                        << (options.battleSnapshot->battlefieldBoundary.valid &&
                                    options.battleSnapshot->battlefieldBoundary.mapProjectionProven
                                ? "runtime_dashed:snapshot_boundary"
                                : "none")
                        << " battle_cockpit_minimap_perimeter_exit_mask="
                        << static_cast<int>(battlefieldMapAllowedExitMask(*options.battleSnapshot))
                        << " battle_cockpit_minimap_perimeter_visual_indices="
                        << static_cast<int>(boundaryIndices.ordinary) << ","
                        << static_cast<int>(boundaryIndices.allowed)
                        << " battle_cockpit_minimap_perimeter_segment_clip=boundary_edges"
                        << " battle_cockpit_speed_indicator=" << layout.scrName << ":bars"
                        << " battle_cockpit_speed_indicator_zero=10"
                        << " battle_cockpit_speed_indicator_green=" << speedGauge.forwardBars
                        << " battle_cockpit_speed_indicator_red=" << speedGauge.reverseBars;
                    if (cockpitFamilyShowsJumpGauge(layout.family)) {
                        std::cout
                            << " battle_cockpit_jump_gauge=" << layout.scrName << ":fuel_vertical"
                            << " battle_cockpit_jump_gauge_rect=" << rectSpec(cockpitJumpGaugeRect(layout.family))
                            << " battle_cockpit_jump_gauge_key=J"
                            << " battle_cockpit_jump_ready_light=LIGHT.SCR:default_green,red_pixels"
                            << " battle_cockpit_jump_ready_rect=" << rectSpec(cockpitJumpReadyLightRect(layout.family));
                    } else {
                        std::cout
                            << " battle_cockpit_jump_gauge=none"
                            << " battle_cockpit_jump_ready_light=none";
                    }
                    std::cout
                        << " battle_cockpit_hud_color=" << defaultHudColor.id
                        << " battle_cockpit_hud_color_key=H";
                } else {
                    std::cout
                        << " battle_cockpit_struts=none"
                        << " battle_cockpit_system_indicators=none";
                }
            }
            std::cout
                << " battle_hud_heading_deg=" << (static_cast<int>(std::lround(hud.headingDegrees)) % 360)
                << " battle_hud_compass=" << hud.compass
                << std::fixed << std::setprecision(0)
                << " battle_hud_speed=" << hud.speed
                << " battle_hud_target_speed=" << hud.targetSpeed
                << " battle_hud_max_speed=" << hud.maxForwardSpeed
                << " battle_hud_torso_deg=" << hud.torsoDegrees
                << " battle_hud_aim_pitch_step=" << hud.aimPitchStep
                << " battle_hud_zoom=1"
                << " battle_hud_jump_fuel=" << hud.jumpFuel
                << " battle_hud_jump_capable=" << (hud.jumpCapable ? "yes" : "no")
                << " battle_hud_jump_ready=" << (hud.jumpJetReady ? "yes" : "no")
                << " battle_hud_airborne=" << (hud.airborne ? "yes" : "no")
                << std::defaultfloat << std::setprecision(6)
                << " battle_hud_boundary=" << (hud.boundaryContact ? "contact" : "clear")
                << " battle_hud_scenario=" << hud.scenario
                << " battle_hud_mode=" << hud.mode
                << " battle_hud_camera=" << hud.camera;
        }
    }
    if (options.terrainEnabled) {
        mw::legacy3d::TerrainMesh terrainMesh;
        mw::legacy3d::TerrainWorld terrainWorld;
        mw::legacy3d::GpuBatch terrainObjectBatch;
        std::vector<mw::legacy3d::RuntimeRecord> terrainRecords;
        mw::legacy3d::GpuBatchOptions terrainBatchOptions;
        terrainBatchOptions.shadeMode = options.shadeMode;
        terrainBatchOptions.edgeRgb = options.wire ? std::array<float, 3>{0.86f, 0.86f, 0.86f}
                                                   : std::array<float, 3>{0.02f, 0.02f, 0.02f};
        if (options.terrainWorldEnabled) {
            terrainRecords = mw::legacy3d::loadRuntimeShapeRecords(options.terrainShapePath);
        }
        if (!options.terrainLayoutNames.empty()) {
            terrainWorld.resourceName = "layout:" + joinStringList(options.terrainLayoutNames);
            if (options.terrainLayoutNames.size() != 4u) {
                throw std::runtime_error("terrain layout smoke requires exactly four tiles");
            }
            std::array<mw::legacy3d::TerrainGrid, 4> layoutGrids;
            for (size_t i = 0; i < options.terrainLayoutNames.size(); ++i) {
                layoutGrids[i] = mw::legacy3d::loadTerrainGrid(
                    options.terrainRoot / "GRD" / (options.terrainLayoutNames[i] + ".GRD"));
            }
            terrainMesh = mw::legacy3d::buildTerrainMesh(
                mw::legacy3d::composeTerrainGrid2x2(
                    layoutGrids,
                    terrainWorld.resourceName,
                    options.terrainTileMirrorX,
                    options.terrainTileMirrorY),
                options.terrainCellSize,
                20.0f);
            const float tileWidth = static_cast<float>(layoutGrids[0].width) * options.terrainCellSize;
            const float tileDepth = static_cast<float>(layoutGrids[0].height) * options.terrainCellSize;
            const Bounds terrainBounds = terrainMeshBounds(terrainMesh);
            const Bounds placementBounds = Bounds{
                Vec3{0.0f, terrainBounds.min.y, 0.0f},
                Vec3{tileWidth * 2.0f, terrainBounds.max.y, tileDepth * 2.0f},
                Vec3{tileWidth, (terrainBounds.min.y + terrainBounds.max.y) * 0.5f, tileDepth},
                std::max(1.0f, std::sqrt(tileWidth * tileWidth + tileDepth * tileDepth)),
            };
            for (size_t i = 0; i < options.terrainLayoutNames.size(); ++i) {
                const std::string& name = options.terrainLayoutNames[i];
                const mw::legacy3d::TerrainWorld tileWorld =
                    mw::legacy3d::loadTerrainWorld(options.terrainRoot / "WLD" / (name + ".WLD"));
                appendGpuBatch(
                    terrainObjectBatch,
                    buildTerrainObjectBatch(
                        tileWorld,
                        terrainRecords,
                        terrainBatchOptions,
                        placementBounds,
                        options.terrainObjectInset,
                        options.terrainObjectScale,
                        options.terrainCellSize,
                        true,
                        options.terrainTileMirrorX,
                        options.terrainTileMirrorY,
                        options.terrainObjectMirrorY,
                        options.terrainColorMode));
                terrainWorld.objects.insert(
                    terrainWorld.objects.end(),
                    tileWorld.objects.begin(),
                    tileWorld.objects.end());
            }
        } else {
            terrainMesh = mw::legacy3d::buildTerrainMesh(
                mw::legacy3d::loadTerrainGrid(options.terrainGridPath),
                options.terrainCellSize,
                20.0f);
            if (options.terrainTileMirrorX) {
                mirrorTerrainMeshX(terrainMesh);
            }
            if (options.terrainTileMirrorY) {
                mirrorTerrainMeshY(terrainMesh);
            }
            if (options.terrainWorldEnabled) {
                terrainWorld = mw::legacy3d::loadTerrainWorld(options.terrainWorldPath);
                terrainObjectBatch = buildTerrainObjectBatch(
                    terrainWorld,
                    terrainRecords,
                    terrainBatchOptions,
                    terrainMeshBounds(terrainMesh),
                    options.terrainObjectInset,
                    options.terrainObjectScale,
                    options.terrainCellSize,
                    false,
                    options.terrainTileMirrorX,
                    options.terrainTileMirrorY,
                    options.terrainObjectMirrorY,
                    options.terrainColorMode);
            }
        }
        const TerrainGroundPatchStats groundPatchStats = terrainGroundPatchStats(terrainMesh);
        std::cout
            << " terrain=" << (!options.terrainLayoutNames.empty()
                                  ? std::string("layout:") + joinStringList(options.terrainLayoutNames)
                                  : narrowPath(options.terrainGridPath))
            << " terrain_vertices=" << terrainMesh.vertices.size()
            << " terrain_triangles=" << (terrainMesh.indices.size() / 3u)
            << " terrain_bounds=" << formatTerrainBounds(terrainMesh)
            << " terrain_ground_patches=none"
            << " terrain_ground_patch_vertices=" << groundPatchStats.vertices
            << " terrain_ground_patch_triangles=" << groundPatchStats.triangles
            << " terrain_ground_patch_vertex_bands="
            << groundPatchStats.verticesByBand[0] << ","
            << groundPatchStats.verticesByBand[1] << ","
            << groundPatchStats.verticesByBand[2] << ","
            << groundPatchStats.verticesByBand[3]
            << " terrain_ground_patch_triangle_bands="
            << groundPatchStats.trianglesByBand[0] << ","
            << groundPatchStats.trianglesByBand[1] << ","
            << groundPatchStats.trianglesByBand[2] << ","
            << groundPatchStats.trianglesByBand[3]
            << " terrain_color=" << options.terrainColorMode
            << " terrain_sky=" << options.terrainSkyMode
            << " terrain_cell_size=" << options.terrainCellSize
            << " terrain_object_inset=" << options.terrainObjectInset
            << " terrain_object_scale=" << options.terrainObjectScale
            << " terrain_tile_mirror_x=" << (options.terrainTileMirrorX ? "yes" : "no")
            << " terrain_tile_mirror_y=" << (options.terrainTileMirrorY ? "yes" : "no")
            << " terrain_object_mirror_y=" << (options.terrainObjectMirrorY ? "yes" : "no");
        if (options.terrainEnvironmentId.has_value()) {
            std::cout << " terrain_environment_id=" << *options.terrainEnvironmentId;
        }
        if (!options.terrainLayoutNames.empty()) {
            if (options.terrainScenarioIndex.has_value()) {
                std::cout << " terrain_scenario_index=" << *options.terrainScenarioIndex
                          << " terrain_scenario_mode=" << options.terrainScenarioMode;
            }
            if (!options.terrainLayoutPreset.empty()) {
                std::cout << " terrain_layout_preset=" << options.terrainLayoutPreset;
            }
            std::cout << " terrain_layout_gap=" << options.terrainLayoutGap;
        }
        if (options.terrainWorldEnabled) {
            std::cout
                << " terrain_world=" << (!options.terrainLayoutNames.empty()
                                            ? std::string("layout")
                                            : narrowPath(options.terrainWorldPath))
                << " terrain_shapes=" << narrowPath(options.terrainShapePath)
                << " wld_objects=" << terrainWorld.objects.size()
                << " terpck_vertices=" << (terrainObjectBatch.vertices.size() / 6u)
                << " terpck_triangles=" << (terrainObjectBatch.triangleIndices.size() / 3u)
                << " terrain_object_edges="
                << (terrainObjectEdgesVisible(options.terrainColorMode, options.wire) ? "visible" : "hidden");
        }
    }
    std::cout
        << " source_vertices=" << sourceVertices
        << " parts=" << sourceParts
        << " raw_poly=" << rawPolygons
        << " raw_line=" << rawLines
        << " gpu_vertices=" << (batch.vertices.size() / 6u)
        << " gpu_triangles=" << (batch.triangleIndices.size() / 3u)
        << " gpu_line_pairs=" << (batch.lineIndices.size() / 2u)
        << " shade=" << options.shadeMode
        << " mode=" << (options.wire ? "wire" : "solid")
        << " projection=" << (options.perspective ? "perspective" : "ortho")
        << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        Options options = parseArgs(argc, argv);
        if (options.listMechPresets) {
            printMechPresetList();
            return 0;
        }
        if (options.listBattleMissions) {
            printBattleMissionList();
            return 0;
        }
        if (options.listTerrainArenas) {
            printTerrainArenaList(options.terrainRoot);
            return 0;
        }
        if (options.listTerrainLayouts) {
            printTerrainLayoutList();
            return 0;
        }
        if (options.listTerrainScenarios) {
            if (options.terrainScenarioPath.empty()) {
                options.terrainScenarioPath = options.terrainRoot / "DAT" / "SNARIO.DAT";
            }
            printTerrainScenarioList(options.terrainScenarioPath);
            return 0;
        }
        mw::legacy3d::GpuBatchOptions batchOptions;
        batchOptions.shadeMode = options.shadeMode;
        batchOptions.edgeRgb = options.wire ? std::array<float, 3>{0.86f, 0.86f, 0.86f}
                                            : std::array<float, 3>{0.02f, 0.02f, 0.02f};

        std::vector<mw::legacy3d::RuntimeRecord> records = mw::legacy3d::loadRuntimeShapeRecords(options.resourcePath);
        const mw::legacy3d::RuntimeRecord& record = selectRecord(records, options.recordIndex);
        if (options.assembled && options.assemblyRecordIndices.empty()) {
            options.assemblyRecordIndices.push_back(options.recordIndex);
        }
        mw::legacy3d::GpuBatch batch;
        if (options.assembled) {
            const mw::mech3d::MechModelDefinition bindDefinition = buildCliAssemblyDefinition(options);
            const mw::mech3d::ModelAnimationDefinition animationDefinition = buildCliAnimationDefinition(options);
            const mw::mech3d::ModelAssemblyFrameAnimationDefinition assemblyFrameAnimationDefinition =
                buildCliAssemblyFrameAnimationDefinition(options);
            int battleFrameIndex = 0;
            if (options.battleSnapshot.has_value() && !options.battleSnapshot->combatants.empty()) {
                const uint64_t snapshotElapsed = options.battleSnapshot->combatants.front().walkAnimationElapsedMs;
                const int elapsedMs = snapshotElapsed > static_cast<uint64_t>(std::numeric_limits<int>::max())
                                          ? std::numeric_limits<int>::max()
                                          : static_cast<int>(snapshotElapsed);
                if (!assemblyFrameAnimationDefinition.frames.empty()) {
                    battleFrameIndex =
                        mw::mech3d::assemblyFrameAnimationFrameForElapsedMs(assemblyFrameAnimationDefinition, elapsedMs);
                } else if (options.animationEnabled && !animationDefinition.sequences.empty()) {
                    battleFrameIndex = mw::mech3d::animationFrameForElapsedMs(animationDefinition, elapsedMs);
                }
            }
            const mw::mech3d::MechModelDefinition frameDefinition =
                !assemblyFrameAnimationDefinition.frames.empty()
                    ? mw::mech3d::applyAssemblyFrameAnimationFrame(assemblyFrameAnimationDefinition, battleFrameIndex)
                    : (options.animationEnabled && !animationDefinition.sequences.empty()
                           ? mw::mech3d::applyAnimationFrame(bindDefinition, animationDefinition, battleFrameIndex)
                           : bindDefinition);
            batch = mw::mech3d::buildMechRenderInstance(frameDefinition, records, batchOptions).batch;
        } else {
            batch = mw::legacy3d::buildGpuBatch(record, batchOptions);
        }

        if (options.smoke) {
            return runSmoke(options, records, batch);
        }

        BattleViewerApp app(std::move(options), std::move(records));
        return app.run();
    } catch (const std::exception& exc) {
        printUsage();
        std::cerr << "mw_battle_viewer: " << exc.what() << "\n";
        return 1;
    }
}
