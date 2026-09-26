#include "mech3d/mech_catalog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string_view>

namespace mw::mech3d {
namespace {

bool matchesPreset(const std::string& requested, const std::string& canonical) {
    return requested == canonical;
}

Vec3f v(float x, float y, float z) {
    return Vec3f{x, y, z};
}

struct CatalogComponentSpec {
    int componentId = 0;
    int parentComponentId = -1;
    int bindRecordIndex = 0;
    Vec3f localPivot{};
    const char* label = "";
    ComponentPoseAxis defaultPoseAxis = ComponentPoseAxis::Unknown;
    MechComponentDestroyedAction destroyedAction = MechComponentDestroyedAction::HideComponent;
};

struct CatalogMechSpec {
    const char* presetId = "";
    const char* debugName = "";
    const char* tblName = "";
    MechCatalogDestroyedSlotQuality destroyedSlotQuality = MechCatalogDestroyedSlotQuality::AssemblyOnly;
    std::array<CatalogComponentSpec, 6> components{};
    std::array<const char*, 8> animationIds{};
    size_t animationIdCount = 0;
    std::array<const char*, 4> assemblyFrameAnimationIds{};
    size_t assemblyFrameAnimationIdCount = 0;
    std::array<const char*, 3> slotProbeAnimationIds{};
    size_t slotProbeAnimationIdCount = 0;
    std::array<int, 2> deathFirstRecords{};
    std::array<int, 2> deathFrameCounts{};
    size_t deathAnimationCount = 0;
};

const std::array<CatalogMechSpec, 8> kCatalogMechs{{
    CatalogMechSpec{
        "battlemaster",
        "Battlemaster model mapping v1",
        "BMAPCK.TBL",
        MechCatalogDestroyedSlotQuality::ComponentClean,
        {{
            CatalogComponentSpec{1, 2, 0, v(0.0f, 0.0f, 0.0f), "right_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{5, 2, 1, v(0.0f, 0.0f, 0.0f), "left_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{4, 2, 2, v(0.0f, 0.0f, 0.0f), "right_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{0, 2, 3, v(0.0f, 0.0f, 0.0f), "left_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{2, -1, 4, v(0.0f, 0.0f, 0.0f), "torso", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{3, 2, 5, v(0.0f, 0.0f, 0.0f), "cockpit", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
        }},
        {{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"}},
        6,
        {{"damage_or_destroyed", "destroyed", "death"}},
        3,
        {{"destroyed_slot_probe"}},
        1,
        {{54}},
        {{5}},
        1,
    },
    CatalogMechSpec{
        "warhammer",
        "Warhammer model mapping v1",
        "HAMPCK.TBL",
        MechCatalogDestroyedSlotQuality::ComponentClean,
        {{
            CatalogComponentSpec{1, 2, 0, v(0.0f, 0.0f, 0.0f), "right_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{5, 2, 1, v(0.0f, 0.0f, 0.0f), "left_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{4, 2, 2, v(0.0f, 0.0f, 0.0f), "right_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{0, 2, 3, v(0.0f, 0.0f, 0.0f), "left_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{2, -1, 4, v(0.0f, 0.0f, 0.0f), "torso", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{3, 2, 5, v(0.0f, 0.0f, 0.0f), "cockpit", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
        }},
        {{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"}},
        6,
        {{"damage_or_destroyed", "destroyed", "death"}},
        3,
        {{"destroyed_slot_probe"}},
        1,
        {{54}},
        {{5}},
        1,
    },
    CatalogMechSpec{
        "marauder",
        "Marauder model mapping v1",
        "MARPCK.TBL",
        MechCatalogDestroyedSlotQuality::AssemblyOnly,
        {{
            CatalogComponentSpec{1, 2, 0, v(-194.5f, 112.5f, 24.0f), "right_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{5, 2, 1, v(194.5f, 112.5f, 7.5f), "left_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{4, 2, 2, v(-134.0f, -111.0f, 77.5f), "right_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{0, 2, 3, v(133.5f, -124.0f, 77.5f), "left_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{2, -1, 4, v(6.5f, 292.5f, 123.0f), "torso", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{3, 2, 5, v(-28.0f, 217.5f, 216.0f), "cockpit", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
        }},
        {{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"}},
        6,
        {{"damage_or_destroyed", "destroyed", "death"}},
        3,
        {{"destroyed_slot_probe"}},
        1,
        {{54}},
        {{5}},
        1,
    },
    CatalogMechSpec{
        "locust",
        "Locust model mapping v1",
        "LOCPCK.TBL",
        MechCatalogDestroyedSlotQuality::ComponentClean,
        {{
            CatalogComponentSpec{1, 2, 0, v(-141.0f, 240.0f, -3.5f), "right_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{5, 2, 1, v(138.0f, 240.0f, -27.5f), "left_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{4, 2, 2, v(-101.0f, -53.5f, -38.0f), "right_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{0, 2, 3, v(100.0f, -75.0f, 15.0f), "left_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{2, -1, 4, v(0.0f, 195.0f, 17.0f), "torso", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{3, 2, 5, v(11.5f, 120.0f, 59.0f), "cockpit", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
        }},
        {{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"}},
        6,
        {{"damage_or_destroyed", "destroyed", "death"}},
        3,
        {{"destroyed_slot_probe"}},
        1,
        {{54}},
        {{5}},
        1,
    },
    CatalogMechSpec{
        "rifleman",
        "Rifleman model mapping v1",
        "RIFPCK.TBL",
        MechCatalogDestroyedSlotQuality::AssemblyOnly,
        {{
            CatalogComponentSpec{1, 2, 0, v(0.0f, 0.0f, 0.0f), "right_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{5, 2, 1, v(0.0f, 0.0f, 0.0f), "left_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{4, 2, 2, v(0.0f, 0.0f, 0.0f), "right_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{0, 2, 3, v(0.0f, 0.0f, 0.0f), "left_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{2, -1, 4, v(0.0f, 0.0f, 0.0f), "torso", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{3, 2, 5, v(0.0f, 0.0f, 0.0f), "cockpit", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
        }},
        {{"idle", "walk", "destroyed_slot_probe", "damage_or_destroyed", "destroyed", "death"}},
        6,
        {{"damage_or_destroyed", "destroyed", "death"}},
        3,
        {{"destroyed_slot_probe"}},
        1,
        {{54}},
        {{5}},
        1,
    },
    CatalogMechSpec{
        "jenner",
        "Jenner model mapping v1",
        "JENPCK.TBL",
        MechCatalogDestroyedSlotQuality::ComponentClean,
        {{
            CatalogComponentSpec{1, 2, 0, v(0.0f, 0.0f, 0.0f), "left_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{5, 2, 1, v(0.0f, 0.0f, 0.0f), "right_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{4, 2, 2, v(0.0f, 0.0f, 0.0f), "left_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{0, 2, 3, v(0.0f, 0.0f, 0.0f), "right_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{2, -1, 4, v(0.0f, 0.0f, 0.0f), "torso", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{3, 2, 5, v(0.0f, 0.0f, 0.0f), "cockpit", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
        }},
        {{"idle", "walk", "death_forward_slot_probe", "death_backward_slot_probe", "death", "death_forward", "death_backward"}},
        7,
        {{"death", "death_forward", "death_backward"}},
        3,
        {{"death_forward_slot_probe", "death_backward_slot_probe"}},
        2,
        {{54, 90}},
        {{6, 5}},
        2,
    },
    CatalogMechSpec{
        "phoenix_hawk",
        "Phoenix Hawk model mapping v1",
        "PHAPCK.TBL",
        MechCatalogDestroyedSlotQuality::ComponentClean,
        {{
            CatalogComponentSpec{1, 2, 0, v(0.0f, 0.0f, 0.0f), "left_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{5, 2, 1, v(0.0f, 0.0f, 0.0f), "right_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{4, 2, 2, v(0.0f, 0.0f, 0.0f), "left_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{0, 2, 3, v(0.0f, 0.0f, 0.0f), "right_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{2, -1, 4, v(0.0f, 0.0f, 0.0f), "torso", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{3, 2, 5, v(0.0f, 0.0f, 0.0f), "cockpit", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
        }},
        {{"idle", "walk", "death_forward_slot_probe", "death_backward_slot_probe", "death", "death_forward", "death_backward"}},
        7,
        {{"death", "death_forward", "death_backward"}},
        3,
        {{"death_forward_slot_probe", "death_backward_slot_probe"}},
        2,
        {{54, 90}},
        {{6, 5}},
        2,
    },
    CatalogMechSpec{
        "shadow_hawk",
        "Shadow Hawk model mapping v1",
        "SHAPCK.TBL",
        MechCatalogDestroyedSlotQuality::ComponentClean,
        {{
            CatalogComponentSpec{1, 2, 0, v(0.0f, 0.0f, 0.0f), "left_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{5, 2, 1, v(0.0f, 0.0f, 0.0f), "right_arm", ComponentPoseAxis::Y, MechComponentDestroyedAction::HideComponent},
            CatalogComponentSpec{4, 2, 2, v(0.0f, 0.0f, 0.0f), "left_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{0, 2, 3, v(0.0f, 0.0f, 0.0f), "right_leg", ComponentPoseAxis::X, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{2, -1, 4, v(0.0f, 0.0f, 0.0f), "torso", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
            CatalogComponentSpec{3, 2, 5, v(0.0f, 0.0f, 0.0f), "cockpit", ComponentPoseAxis::Y, MechComponentDestroyedAction::DestroyMech},
        }},
        {{"idle", "walk", "death_forward_slot_probe", "death_backward_slot_probe", "death", "death_forward", "death_backward"}},
        7,
        {{"death", "death_forward", "death_backward"}},
        3,
        {{"death_forward_slot_probe", "death_backward_slot_probe"}},
        2,
        {{54, 90}},
        {{6, 5}},
        2,
    },
}};

struct CatalogMobilitySpec {
    const char* presetId = "";
    MechCatalogMobility mobility;
};

struct CatalogWeaponSpec {
    const char* typeId = "";
    const char* displayName = "";
    const char* locationId = "";
    char displayRangeClass = 'M';
    int8_t ammunitionPoolIndex = -1;
};

struct CatalogWeaponLoadout {
    const char* presetId = "";
    std::array<CatalogWeaponSpec, 10> weapons{};
    size_t weaponCount = 0;
};

// MW_MAIN.EXE weapon/status strings (the WPN/LOC table) preserve the ordered
// installed slots. BTECH.EXE DS:0EEE supplies the ammunition indirection:
// -1 is energy/no-ammunition, otherwise the pool at runtime +0x59.
constexpr int8_t kAmmoAc5 = 0;
constexpr int8_t kAmmoLrm5 = 1;
constexpr int8_t kAmmoSrm2 = 2;
constexpr int8_t kAmmoSrm4 = 3;
constexpr int8_t kAmmoSrm6 = 4;
constexpr int8_t kAmmoMachineGun = 5;

// BTECH.EXE table base 0x27926 / DS:0EEC, row +0x04. The fire path copies this word
// into the installed slot cooldown and FUN_1000_9b45 moves it one count
// toward zero on every original battle update.
constexpr uint16_t originalCooldownUpdateCount(const char* typeId) {
    return
        std::string_view(typeId) == "machine_gun" ? 2u :
        (std::string_view(typeId) == "ac5" ||
         std::string_view(typeId) == "lrm5" ||
         std::string_view(typeId) == "srm2" ||
         std::string_view(typeId) == "srm4" ||
         std::string_view(typeId) == "srm6") ? 15u : 30u;
}

struct OriginalWeaponDefinitionFields {
    uint16_t damage = 0;
    uint16_t heat = 0;
    uint16_t criticalWeight = 0;
    std::array<uint16_t, 4> rangeWords{};
};

struct OriginalProjectileDefinitionFields {
    int8_t typeIndex = -1;
    uint16_t speedRaw = 0;
    uint16_t lifetimeUpdates = 0;
    uint16_t damageClass = 0;
    uint16_t visualClass = 0;
};

// BTECH.EXE DS:232E maps weapon rows to the five projectile rows. DS:0E6E
// then supplies raw speed, damage class, lifetime-update count and visual
// class. Energy weapons and MG carry -1 and never enter the projectile pool.
constexpr OriginalProjectileDefinitionFields originalProjectileDefinitionFields(
    std::string_view typeId) {
    return typeId == "ac5" ? OriginalProjectileDefinitionFields{0, 60, 57, 5, 0} :
           typeId == "lrm5" ? OriginalProjectileDefinitionFields{1, 40, 67, 6, 1} :
           typeId == "srm2" ? OriginalProjectileDefinitionFields{2, 50, 28, 7, 1} :
           typeId == "srm4" ? OriginalProjectileDefinitionFields{3, 50, 28, 8, 1} :
           typeId == "srm6" ? OriginalProjectileDefinitionFields{4, 50, 28, 9, 1} :
           OriginalProjectileDefinitionFields{};
}

// BTECH.EXE DS:0EEC, ten 0x16-byte rows in original token order.  Only the
// fields with closed runtime cross-references are named here; the two trailing
// words remain deliberately absent until projectile/effect semantics close.
constexpr OriginalWeaponDefinitionFields originalWeaponDefinitionFields(
    std::string_view typeId) {
    return typeId == "small_laser" ? OriginalWeaponDefinitionFields{3, 80, 1, {0, 2, 3, 4}} :
           typeId == "medium_laser" ? OriginalWeaponDefinitionFields{5, 240, 1, {0, 4, 7, 10}} :
           typeId == "large_laser" ? OriginalWeaponDefinitionFields{8, 640, 2, {0, 6, 11, 16}} :
           typeId == "machine_gun" ? OriginalWeaponDefinitionFields{2, 0, 1, {0, 2, 3, 4}} :
           typeId == "ppc" ? OriginalWeaponDefinitionFields{10, 800, 3, {3, 7, 13, 19}} :
           typeId == "ac5" ? OriginalWeaponDefinitionFields{5, 80, 4, {3, 7, 13, 19}} :
           typeId == "lrm5" ? OriginalWeaponDefinitionFields{5, 160, 1, {6, 8, 15, 22}} :
           typeId == "srm2" ? OriginalWeaponDefinitionFields{2, 160, 1, {0, 4, 7, 10}} :
           typeId == "srm4" ? OriginalWeaponDefinitionFields{4, 240, 1, {0, 4, 7, 10}} :
           typeId == "srm6" ? OriginalWeaponDefinitionFields{6, 320, 2, {0, 4, 7, 10}} :
           OriginalWeaponDefinitionFields{};
}

const std::array<CatalogWeaponLoadout, 8> kCatalogWeaponLoadouts{{
    {"locust", {{{"medium_laser", "M LAS", "CT", 'M', -1}, {"machine_gun", "MG", "RA", 'S', kAmmoMachineGun}, {"machine_gun", "MG", "LA", 'S', kAmmoMachineGun}}}, 3},
    {"jenner", {{{"srm4", "SRM4", "CT", 'M', kAmmoSrm4}, {"medium_laser", "M LAS", "RA", 'M', -1}, {"medium_laser", "M LAS", "RA", 'M', -1}, {"medium_laser", "M LAS", "LA", 'M', -1}, {"medium_laser", "M LAS", "LA", 'M', -1}}}, 5},
    {"phoenix_hawk", {{{"large_laser", "L LAS", "RA", 'L', -1}, {"medium_laser", "M LAS", "RA", 'M', -1}, {"medium_laser", "M LAS", "LA", 'M', -1}, {"machine_gun", "MG", "LA", 'S', kAmmoMachineGun}, {"machine_gun", "MG", "RA", 'S', kAmmoMachineGun}}}, 5},
    {"shadow_hawk", {{{"ac5", "AC/5", "LT", 'L', kAmmoAc5}, {"lrm5", "LRM5", "RT", 'L', kAmmoLrm5}, {"srm2", "SRM2", "HD", 'M', kAmmoSrm2}, {"medium_laser", "M LAS", "RA", 'M', -1}}}, 4},
    {"rifleman", {{{"large_laser", "L LAS", "RA", 'L', -1}, {"large_laser", "L LAS", "LA", 'L', -1}, {"ac5", "AC/5", "RA", 'L', kAmmoAc5}, {"ac5", "AC/5", "LA", 'L', kAmmoAc5}, {"medium_laser", "M LAS", "RT", 'M', -1}, {"medium_laser", "M LAS", "LT", 'M', -1}}}, 6},
    {"warhammer", {{{"ppc", "PPC", "RA", 'L', -1}, {"ppc", "PPC", "LA", 'L', -1}, {"srm6", "SRM6", "RT", 'M', kAmmoSrm6}, {"medium_laser", "M LAS", "RT", 'M', -1}, {"medium_laser", "M LAS", "LT", 'M', -1}, {"small_laser", "S LAS", "RT", 'S', -1}, {"small_laser", "S LAS", "LT", 'S', -1}, {"machine_gun", "MG", "RT", 'S', kAmmoMachineGun}, {"machine_gun", "MG", "LT", 'S', kAmmoMachineGun}}}, 9},
    {"marauder", {{{"ppc", "PPC", "RA", 'L', -1}, {"ppc", "PPC", "LA", 'L', -1}, {"medium_laser", "M LAS", "RA", 'M', -1}, {"medium_laser", "M LAS", "LA", 'M', -1}, {"ac5", "AC/5", "RT", 'L', kAmmoAc5}}}, 5},
    {"battlemaster", {{{"ppc", "PPC", "RA", 'L', -1}, {"medium_laser", "M LAS", "RT", 'M', -1}, {"medium_laser", "M LAS", "RT", 'M', -1}, {"medium_laser", "M LAS", "RT", 'M', -1}, {"machine_gun", "MG", "LA", 'S', kAmmoMachineGun}, {"machine_gun", "MG", "LA", 'S', kAmmoMachineGun}, {"srm6", "SRM6", "LT", 'M', kAmmoSrm6}, {"medium_laser", "M LAS", "LT", 'M', -1}, {"medium_laser", "M LAS", "LT", 'M', -1}, {"medium_laser", "M LAS", "LT", 'M', -1}}}, 10},
}};

struct CatalogDamageProfileSpec {
    const char* presetId = "";
    MechCatalogDamageProfile profile;
};

// Unpacked BTECH.EXE DS:1178, eight 0xE0-byte rows. External order is
// LA,RA,LL,RL,CT,rear-CT,LT,RT,HD; internal order is LA,RA,LL,RL,CT,LT,RT,HD.
const std::array<CatalogDamageProfileSpec, 8> kCatalogDamageProfiles{{
    {"locust", {{{4, 4, 8, 8, 10, 6, 8, 8, 16}}, {{3, 3, 4, 4, 6, 5, 5, 6}}}},
    {"jenner", {{{4, 4, 6, 6, 10, 11, 8, 8, 14}}, {{6, 6, 8, 8, 11, 8, 8, 6}}}},
    {"phoenix_hawk", {{{10, 10, 15, 15, 23, 13, 18, 18, 18}}, {{7, 7, 11, 11, 14, 11, 11, 9}}}},
    {"shadow_hawk", {{{16, 16, 16, 16, 23, 20, 18, 18, 27}}, {{3, 3, 4, 4, 6, 5, 5, 9}}}},
    {"rifleman", {{{15, 15, 12, 12, 22, 8, 15, 15, 24}}, {{10, 10, 14, 14, 20, 14, 14, 12}}}},
    {"warhammer", {{{20, 20, 15, 15, 22, 25, 17, 17, 36}}, {{11, 11, 15, 15, 22, 15, 15, 12}}}},
    {"marauder", {{{22, 22, 18, 18, 35, 26, 17, 17, 36}}, {{12, 12, 16, 16, 23, 16, 16, 12}}}},
    {"battlemaster", {{{24, 24, 26, 26, 40, 27, 28, 28, 36}}, {{14, 14, 18, 18, 27, 18, 18, 12}}}},
}};

struct CatalogCriticalProfileSpec {
    const char* presetId = "";
    MechCatalogCriticalProfile profile;
};

// Unpacked BTECH.EXE DS:1178, eight 0xE0-byte rows. Ammunition locations are
// the six signed words at +0x9A. Jump-jet entries are the three signed
// location/unsigned-weight word pairs at +0xA6. A location of -1 is absent.
const std::array<CatalogCriticalProfileSpec, 8> kCatalogCriticalProfiles{{
    {"locust", {{{-1, -1, -1, -1, -1, 4}}, {{{-1, 0}, {-1, 0}, {-1, 0}}}}},
    {"jenner", {{{-1, -1, -1, 6, -1, -1}}, {{{6, 2}, {5, 2}, {4, 1}}}}},
    {"phoenix_hawk", {{{-1, -1, -1, -1, -1, 4}}, {{{6, 3}, {5, 3}, {-1, 0}}}}},
    {"shadow_hawk", {{{5, 6, 7, 4, -1, -1}}, {{{5, 1}, {6, 1}, {4, 1}}}}},
    {"rifleman", {{{4, -1, -1, -1, -1, -1}}, {{{-1, 0}, {-1, 0}, {-1, 0}}}}},
    {"warhammer", {{{-1, -1, -1, -1, 6, 4}}, {{{-1, 0}, {-1, 0}, {-1, 0}}}}},
    {"marauder", {{{5, -1, -1, -1, -1, -1}}, {{{-1, 0}, {-1, 0}, {-1, 0}}}}},
    {"battlemaster", {{{-1, -1, -1, -1, 5, 5}}, {{{-1, 0}, {-1, 0}, {-1, 0}}}}},
}};

struct CatalogAmmunitionProfileSpec {
    const char* presetId = "";
    MechCatalogAmmunitionProfile profile;
};

// Unpacked BTECH.EXE DS:1178, eight 0xE0-byte rows, six unsigned words at
// row +0xD4. FUN_1000_3577 copies them directly to runtime object +0x59.
const std::array<CatalogAmmunitionProfileSpec, 8>
    kCatalogAmmunitionProfiles{{
        {"locust", {{{0, 0, 0, 0, 0, 200}}}},
        {"jenner", {{{0, 0, 0, 25, 0, 0}}}},
        {"phoenix_hawk", {{{0, 0, 0, 0, 0, 200}}}},
        {"shadow_hawk", {{{20, 24, 50, 0, 0, 0}}}},
        {"rifleman", {{{20, 0, 0, 0, 0, 0}}}},
        {"warhammer", {{{0, 0, 0, 0, 15, 200}}}},
        {"marauder", {{{20, 0, 0, 0, 0, 0}}}},
        {"battlemaster", {{{0, 0, 0, 0, 30, 200}}}},
    }};

// MW_MAIN.EXE file offset 0x00B865 stores speed and jump capacity by chassis.
// The final two fields are BTECH.EXE definition-row words +0x06 and +0x02
// from the eight EXEPACK-decoded DS:1178 rows. Jump-jet counts follow the original
// repair/status definitions; Jenner's count of three is independently
// confirmed by controlled original-game save edits. Wasp and Wolverine have
// no supported BTECH 3D row and intentionally keep raw word zero.
const std::array<CatalogMobilitySpec, 10> kCatalogMobility{{
    CatalogMobilitySpec{"locust", {129, 0, 0, 72, 500}},
    CatalogMobilitySpec{"wasp", {95, 180, 6, 0, 0}},
    CatalogMobilitySpec{"jenner", {118, 150, 3, 66, 600}},
    CatalogMobilitySpec{"phoenix_hawk", {97, 180, 6, 54, 700}},
    CatalogMobilitySpec{"shadow_hawk", {86, 90, 3, 48, 700}},
    CatalogMobilitySpec{"wolverine", {86, 150, 5, 0, 0}},
    CatalogMobilitySpec{"rifleman", {64, 0, 0, 36, 800}},
    CatalogMobilitySpec{"warhammer", {64, 0, 0, 36, 800}},
    CatalogMobilitySpec{"marauder", {64, 0, 0, 36, 900}},
    CatalogMobilitySpec{"battlemaster", {64, 0, 0, 36, 960}},
}};

const CatalogMechSpec& catalogSpec(const std::string& presetId) {
    const auto it = std::find_if(
        kCatalogMechs.begin(),
        kCatalogMechs.end(),
        [&presetId](const CatalogMechSpec& spec) {
            return matchesPreset(presetId, spec.presetId);
        });
    if (it == kCatalogMechs.end()) {
        throw std::runtime_error("unknown mech preset: " + presetId);
    }
    return *it;
}

std::string specPrefix(const CatalogMechSpec& spec) {
    return std::string(spec.presetId) + "_";
}

std::vector<int> contiguousRecords(int firstRecord, int count) {
    std::vector<int> records;
    records.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        records.push_back(firstRecord + i);
    }
    return records;
}

std::string destroyedActionLabel(MechComponentDestroyedAction action) {
    return action == MechComponentDestroyedAction::DestroyMech ? "kills_mech" : "can_be_disabled";
}

size_t assemblyFrameAnimationIndex(const CatalogMechSpec& spec, const std::string& animationId) {
    for (size_t i = 0; i < spec.assemblyFrameAnimationIdCount; ++i) {
        if (animationId == spec.assemblyFrameAnimationIds[i]) {
            if (spec.deathAnimationCount <= 1) {
                return 0;
            }
            if (animationId == "death_backward" || animationId == "second_death") {
                return 1;
            }
            return 0;
        }
    }
    throw std::runtime_error("unknown assembly-frame mech animation: " + std::string(spec.presetId) + "/" + animationId);
}

size_t slotProbeAnimationIndex(const CatalogMechSpec& spec, const std::string& animationId) {
    for (size_t i = 0; i < spec.slotProbeAnimationIdCount; ++i) {
        if (animationId == spec.slotProbeAnimationIds[i]) {
            return i;
        }
    }
    throw std::runtime_error("unknown component-frame mech animation: " + std::string(spec.presetId) + "/" + animationId);
}

MechModelDefinition makeSpecModelDefinition(const CatalogMechSpec& spec) {
    MechModelDefinition definition;
    definition.debugName = std::string(spec.presetId) + "_model_mapping_v1";
    definition.components.reserve(spec.components.size());
    for (const CatalogComponentSpec& componentSpec : spec.components) {
        MechComponentDefinition component;
        component.componentId = componentSpec.componentId;
        component.parentComponentId = componentSpec.parentComponentId;
        component.sourceRecordIndex = componentSpec.bindRecordIndex;
        component.localMatrix = identityMatrix();
        component.localPivot = componentSpec.localPivot;
        component.debugLabel =
            specPrefix(spec) + componentSpec.label + "_bind_record_" + std::to_string(componentSpec.bindRecordIndex);
        component.defaultPoseAxis = componentSpec.defaultPoseAxis;
        definition.components.push_back(component);
    }
    return definition;
}

ModelAnimationDefinition makeSpecWalkAnimationDefinition(const CatalogMechSpec& spec) {
    ModelAnimationDefinition animation;
    animation.debugName = std::string(spec.presetId) + "_walk";
    animation.sequences.reserve(spec.components.size());
    for (size_t i = 0; i < spec.components.size(); ++i) {
        const CatalogComponentSpec& componentSpec = spec.components[i];
        animation.sequences.push_back(ComponentFrameSequence{
            componentSpec.componentId,
            contiguousRecords(6 + static_cast<int>(i) * 8, 8),
            180,
            specPrefix(spec) + componentSpec.label + "_walk",
        });
    }
    return animation;
}

ModelAssemblyFrameAnimationDefinition makeSpecDestroyedAnimationDefinition(const CatalogMechSpec& spec, size_t deathIndex) {
    if (deathIndex >= spec.deathAnimationCount) {
        throw std::runtime_error("death animation index out of range");
    }
    const int firstRecord = spec.deathFirstRecords[deathIndex];
    const int frameCount = spec.deathFrameCounts[deathIndex];
    ModelAssemblyFrameAnimationDefinition animation;
    animation.debugName = std::string(spec.presetId) + "_death_" + std::to_string(deathIndex);
    animation.frameDurationMs = 180;
    animation.componentIds.reserve(spec.components.size());
    for (const CatalogComponentSpec& componentSpec : spec.components) {
        animation.componentIds.push_back(componentSpec.componentId);
    }
    animation.frames.reserve(static_cast<size_t>(frameCount));
    for (int frame = 0; frame < frameCount; ++frame) {
        // In the 120-record family the first death bank is stored from the
        // settled forward pose back toward the standing pose.  Playback must
        // traverse that bank in reverse; the second/backward bank and all
        // 84-record death banks are already stored in playback order.
        const int sourceFrame =
            spec.deathAnimationCount > 1 && deathIndex == 0
                ? frameCount - 1 - frame
                : frame;
        std::vector<int> frameRecords;
        frameRecords.reserve(spec.components.size());
        for (size_t slot = 0; slot < spec.components.size(); ++slot) {
            frameRecords.push_back(
                firstRecord + static_cast<int>(slot) * frameCount +
                sourceFrame);
        }
        animation.frames.push_back(AssemblyFrameDefinition{
            frameRecords,
            specPrefix(spec) + "death_" + std::to_string(deathIndex) +
                "_frame_" + std::to_string(frame) + "_source_" +
                std::to_string(sourceFrame),
        });
    }
    return animation;
}

ModelAnimationDefinition makeSpecDestroyedSlotProbeAnimationDefinition(const CatalogMechSpec& spec, size_t deathIndex) {
    if (deathIndex >= spec.deathAnimationCount) {
        throw std::runtime_error("death animation index out of range");
    }
    const int firstRecord = spec.deathFirstRecords[deathIndex];
    const int frameCount = spec.deathFrameCounts[deathIndex];
    ModelAnimationDefinition animation;
    animation.debugName = std::string(spec.presetId) + "_death_" + std::to_string(deathIndex) + "_slot_probe";
    animation.sequences.reserve(spec.components.size());
    for (size_t slot = 0; slot < spec.components.size(); ++slot) {
        const CatalogComponentSpec& componentSpec = spec.components[slot];
        animation.sequences.push_back(ComponentFrameSequence{
            componentSpec.componentId,
            contiguousRecords(firstRecord + static_cast<int>(slot) * frameCount, frameCount),
            180,
            specPrefix(spec) + componentSpec.label + "_destroyed_slot",
        });
    }
    return animation;
}

std::vector<MechComponentDamageRule> makeSpecDamageRules(const CatalogMechSpec& spec) {
    std::vector<MechComponentDamageRule> rules;
    rules.reserve(spec.components.size());
    for (const CatalogComponentSpec& componentSpec : spec.components) {
        rules.push_back(MechComponentDamageRule{
            componentSpec.componentId,
            componentSpec.destroyedAction,
            specPrefix(spec) + componentSpec.label + "_" + destroyedActionLabel(componentSpec.destroyedAction),
        });
    }
    return rules;
}

} // namespace

std::vector<MechCatalogEntry> mechCatalogEntries() {
    std::vector<MechCatalogEntry> entries;
    entries.reserve(kCatalogMechs.size());
    for (const CatalogMechSpec& spec : kCatalogMechs) {
        std::vector<std::string> animationIds;
        animationIds.reserve(spec.animationIdCount);
        for (size_t i = 0; i < spec.animationIdCount; ++i) {
            animationIds.push_back(spec.animationIds[i]);
        }
        entries.push_back(MechCatalogEntry{
            spec.presetId,
            spec.debugName,
            std::filesystem::path("Sorted Original Files") / "TBL" / "viewer8" / spec.tblName,
            animationIds,
            spec.destroyedSlotQuality,
        });
    }
    return entries;
}

MechModelDefinition makeCatalogMechModelDefinition(const std::string& presetId) {
    return makeSpecModelDefinition(catalogSpec(presetId));
}

MechCatalogAnimationKind catalogAnimationKind(const std::string& presetId, const std::string& animationId) {
    const CatalogMechSpec& spec = catalogSpec(presetId);
    if (animationId == "idle") {
        return MechCatalogAnimationKind::BindPose;
    }
    if (animationId == "walk") {
        return MechCatalogAnimationKind::ComponentFrames;
    }
    for (size_t i = 0; i < spec.slotProbeAnimationIdCount; ++i) {
        if (animationId == spec.slotProbeAnimationIds[i]) {
            return MechCatalogAnimationKind::ComponentFrames;
        }
    }
    for (size_t i = 0; i < spec.assemblyFrameAnimationIdCount; ++i) {
        if (animationId == spec.assemblyFrameAnimationIds[i]) {
            return MechCatalogAnimationKind::AssemblyFrames;
        }
    }
    if (animationId == "destroyed" && spec.deathAnimationCount == 1) {
        return MechCatalogAnimationKind::AssemblyFrames;
    }
    throw std::runtime_error("unknown mech animation: " + presetId + "/" + animationId);
}

ModelAnimationDefinition makeCatalogAnimationDefinition(const std::string& presetId, const std::string& animationId) {
    const CatalogMechSpec& spec = catalogSpec(presetId);
    if (animationId == "idle") {
        return {};
    }
    if (animationId == "walk") {
        return makeSpecWalkAnimationDefinition(spec);
    }
    for (size_t i = 0; i < spec.slotProbeAnimationIdCount; ++i) {
        if (animationId == spec.slotProbeAnimationIds[i]) {
            return makeSpecDestroyedSlotProbeAnimationDefinition(spec, i);
        }
    }
    throw std::runtime_error("unknown component-frame mech animation: " + presetId + "/" + animationId);
}

ModelAssemblyFrameAnimationDefinition makeCatalogAssemblyFrameAnimationDefinition(
    const std::string& presetId,
    const std::string& animationId) {
    const CatalogMechSpec& spec = catalogSpec(presetId);
    if (animationId == "idle") {
        return {};
    }
    if (animationId == "destroyed" && spec.deathAnimationCount == 1) {
        return makeSpecDestroyedAnimationDefinition(spec, 0);
    }
    for (size_t i = 0; i < spec.assemblyFrameAnimationIdCount; ++i) {
        if (animationId == spec.assemblyFrameAnimationIds[i]) {
            return makeSpecDestroyedAnimationDefinition(spec, assemblyFrameAnimationIndex(spec, animationId));
        }
    }
    throw std::runtime_error("unknown assembly-frame mech animation: " + presetId + "/" + animationId);
}

std::vector<MechComponentDamageRule> catalogComponentDamageRules(const std::string& presetId) {
    return makeSpecDamageRules(catalogSpec(presetId));
}

MechComponentDestroyedAction catalogDestroyedActionForComponent(const std::string& presetId, int componentId) {
    const std::vector<MechComponentDamageRule> rules = catalogComponentDamageRules(presetId);
    const auto it = std::find_if(
        rules.begin(),
        rules.end(),
        [componentId](const MechComponentDamageRule& rule) {
            return rule.componentId == componentId;
        });
    if (it == rules.end()) {
        return MechComponentDestroyedAction::HideComponent;
    }
    return it->destroyedAction;
}

MechCatalogDestroyedSlotQuality catalogDestroyedSlotQuality(const std::string& presetId) {
    for (const MechCatalogEntry& entry : mechCatalogEntries()) {
        if (matchesPreset(presetId, entry.presetId)) {
            return entry.destroyedSlotQuality;
        }
    }
    throw std::runtime_error("unknown mech preset: " + presetId);
}

std::filesystem::path catalogDefaultResourcePath(const std::string& presetId) {
    for (const MechCatalogEntry& entry : mechCatalogEntries()) {
        if (matchesPreset(presetId, entry.presetId)) {
            return entry.defaultResourcePath;
        }
    }
    throw std::runtime_error("unknown mech preset: " + presetId);
}

MechCatalogMobility catalogMobility(const std::string& presetId) {
    const auto it = std::find_if(
        kCatalogMobility.begin(),
        kCatalogMobility.end(),
        [&presetId](const CatalogMobilitySpec& spec) {
            return matchesPreset(presetId, spec.presetId);
        });
    if (it == kCatalogMobility.end()) {
        throw std::runtime_error("unknown mech mobility preset: " + presetId);
    }
    return it->mobility;
}

std::vector<MechCatalogWeaponMount> catalogWeaponMounts(
    const std::string& presetId) {
    const auto it = std::find_if(
        kCatalogWeaponLoadouts.begin(),
        kCatalogWeaponLoadouts.end(),
        [&presetId](const CatalogWeaponLoadout& loadout) {
            return matchesPreset(presetId, loadout.presetId);
        });
    if (it == kCatalogWeaponLoadouts.end()) {
        throw std::runtime_error("unknown mech weapon preset: " + presetId);
    }
    std::vector<MechCatalogWeaponMount> result;
    result.reserve(it->weaponCount);
    for (size_t slot = 0; slot < it->weaponCount; ++slot) {
        const CatalogWeaponSpec& weapon = it->weapons[slot];
        const OriginalWeaponDefinitionFields definition =
            originalWeaponDefinitionFields(weapon.typeId);
        const OriginalProjectileDefinitionFields projectile =
            originalProjectileDefinitionFields(weapon.typeId);
        result.push_back(MechCatalogWeaponMount{
            static_cast<uint8_t>(slot),
            weapon.typeId,
            weapon.displayName,
            weapon.locationId,
            weapon.displayRangeClass,
            weapon.ammunitionPoolIndex,
            originalCooldownUpdateCount(weapon.typeId),
            definition.damage,
            definition.heat,
            definition.criticalWeight,
            definition.rangeWords,
            projectile.typeIndex,
            projectile.speedRaw,
            projectile.lifetimeUpdates,
            projectile.damageClass,
            projectile.visualClass,
        });
    }
    return result;
}

MechCatalogDamageProfile catalogDamageProfile(const std::string& presetId) {
    const auto it = std::find_if(
        kCatalogDamageProfiles.begin(),
        kCatalogDamageProfiles.end(),
        [&presetId](const CatalogDamageProfileSpec& spec) {
            return matchesPreset(presetId, spec.presetId);
        });
    if (it == kCatalogDamageProfiles.end()) {
        throw std::runtime_error("unknown mech damage profile: " + presetId);
    }
    return it->profile;
}

MechCatalogCriticalProfile catalogCriticalProfile(
    const std::string& presetId) {
    const auto it = std::find_if(
        kCatalogCriticalProfiles.begin(),
        kCatalogCriticalProfiles.end(),
        [&presetId](const CatalogCriticalProfileSpec& spec) {
            return matchesPreset(presetId, spec.presetId);
        });
    if (it == kCatalogCriticalProfiles.end()) {
        throw std::runtime_error("unknown mech critical profile: " + presetId);
    }
    return it->profile;
}

MechCatalogAmmunitionProfile catalogAmmunitionProfile(
    const std::string& presetId) {
    const auto it = std::find_if(
        kCatalogAmmunitionProfiles.begin(),
        kCatalogAmmunitionProfiles.end(),
        [&presetId](const CatalogAmmunitionProfileSpec& spec) {
            return matchesPreset(presetId, spec.presetId);
        });
    if (it == kCatalogAmmunitionProfiles.end()) {
        throw std::runtime_error(
            "unknown mech ammunition profile: " + presetId);
    }
    return it->profile;
}

int catalogCockpitComponentId(const std::string& presetId) {
    const CatalogMechSpec& spec = catalogSpec(presetId);
    const auto cockpit = std::find_if(
        spec.components.begin(),
        spec.components.end(),
        [](const CatalogComponentSpec& component) {
            return std::string(component.label) == "cockpit";
        });
    if (cockpit == spec.components.end()) {
        throw std::runtime_error("catalog mech has no cockpit component: " + presetId);
    }
    return cockpit->componentId;
}

float catalogCockpitCameraHeight(
    const std::string& presetId,
    const std::vector<legacy3d::RuntimeRecord>& records,
    const legacy3d::GpuBatchOptions& batchOptions) {
    const MechRenderInstance instance =
        buildMechRenderInstance(makeCatalogMechModelDefinition(presetId), records, batchOptions);
    if (!instance.assembly.bounds.valid) {
        throw std::runtime_error("catalog mech assembly has no valid bounds: " + presetId);
    }

    const int cockpitComponentId = catalogCockpitComponentId(presetId);
    const auto cockpit = std::find_if(
        instance.assembly.components.begin(),
        instance.assembly.components.end(),
        [cockpitComponentId](const ModelAssemblyComponent& component) {
            return component.componentId == cockpitComponentId;
        });
    if (cockpit == instance.assembly.components.end() || !cockpit->worldBounds.valid) {
        throw std::runtime_error("catalog mech has no bounded cockpit component: " + presetId);
    }

    const float height = cockpit->worldBounds.center.y - instance.assembly.bounds.min.y;
    if (!(height > 0.0f) || !std::isfinite(height)) {
        throw std::runtime_error("catalog mech cockpit height is invalid: " + presetId);
    }
    return height;
}

float catalogHorizontalCollisionRadius(
    const std::string& presetId,
    const std::vector<legacy3d::RuntimeRecord>& records,
    const legacy3d::GpuBatchOptions& batchOptions) {
    const MechRenderInstance instance =
        buildMechRenderInstance(makeCatalogMechModelDefinition(presetId), records, batchOptions);
    if (!instance.assembly.bounds.valid) {
        throw std::runtime_error("catalog mech assembly has no valid collision bounds: " + presetId);
    }
    const float width = instance.assembly.bounds.max.x - instance.assembly.bounds.min.x;
    const float depth = instance.assembly.bounds.max.z - instance.assembly.bounds.min.z;
    const float radius = std::max(width, depth) * 0.5f;
    if (!(radius > 0.0f) || !std::isfinite(radius)) {
        throw std::runtime_error("catalog mech horizontal collision radius is invalid: " + presetId);
    }
    return radius;
}

std::vector<int> sourceRecordIndices(const MechModelDefinition& definition) {
    std::vector<int> records;
    records.reserve(definition.components.size());
    for (const MechComponentDefinition& component : definition.components) {
        records.push_back(component.sourceRecordIndex);
    }
    return records;
}

} // namespace mw::mech3d
