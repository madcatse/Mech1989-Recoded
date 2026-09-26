#pragma once

#include "mech3d/mech_catalog.h"
#include "mech3d/model_assembly.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mw::mech3d {

// Original runtime armor order recovered from BTECH's nine external armor
// words and the already-registered combat status art.
enum class MechArmorSectionId : uint8_t {
    RightArm = 0,
    LeftArm = 1,
    RightLeg = 2,
    LeftLeg = 3,
    Head = 4,
    CenterTorso = 5,
    CenterRear = 6,
    RightTorso = 7,
    LeftTorso = 8,
    Count = 9,
};

enum class MechCriticalComponentId : uint8_t {
    Engine = 0,
    Gyros = 1,
    Sensors = 2,
    LifeSupport = 3,
    HeatSinks = 4,
    LeftArmActuator = 5,
    RightArmActuator = 6,
    LeftLegActuator = 7,
    RightLegActuator = 8,
    JumpJets = 9,
    Count = 10,
};

struct MechArmorSectionRuntimeState {
    MechArmorSectionId section = MechArmorSectionId::RightArm;
    uint8_t entryDamageLevel = 0;
    uint16_t armorMaximum = 0;
    uint16_t armorRemaining = 0;
    int battleDamage = 0;
};

enum class MechInternalSectionId : uint8_t {
    RightArm = 0,
    LeftArm = 1,
    RightLeg = 2,
    LeftLeg = 3,
    Head = 4,
    CenterTorso = 5,
    RightTorso = 6,
    LeftTorso = 7,
    Count = 8,
};

struct MechInternalSectionRuntimeState {
    MechInternalSectionId section = MechInternalSectionId::RightArm;
    uint16_t structureMaximum = 0;
    uint16_t structureRemaining = 0;
    int battleDamage = 0;
};

struct MechCriticalComponentRuntimeState {
    MechCriticalComponentId component = MechCriticalComponentId::Engine;
    uint8_t condition = 0;
    bool functional = true;
    uint8_t workingCount = 1;
    uint8_t totalCount = 1;
    int battleDamage = 0;
};

struct MechInstalledWeaponDamageState {
    uint8_t slotIndex = 0;
    uint8_t condition = 0;
    bool functional = true;
    int battleDamage = 0;
};

struct MechDetailedDamageState {
    bool valid = false;
    std::string provenance;
    std::array<MechArmorSectionRuntimeState, 9> armorSections{};
    std::array<MechInternalSectionRuntimeState, 8> internalSections{};
    std::array<MechCriticalComponentRuntimeState, 10> criticalComponents{};
    std::array<MechInstalledWeaponDamageState, 10> installedWeapons{};
    bool originalCockpitCriticalFlag = false;
    // The original battle entry activates jump slots in ascending order.
    // A destroyed bit plus current workingCount reconstructs the exact live
    // three-slot set without a parallel jump-jet damage model.
    uint8_t originalJumpJetCriticalDestroyedMask = 0;
    std::array<uint64_t, 6> ammunitionCriticalHits{};
    uint64_t criticalAttempts = 0;
    uint64_t criticalHitsApplied = 0;
    uint64_t unresolvedCriticalSelections = 0;
    bool criticalResolutionPending = false;
    int unresolvedCriticalDamage = 0;
};

struct MechRuntimeState {
    std::string currentAnimationId;
    std::vector<int> destroyedComponentIds;
    std::vector<int> hiddenComponentIds;
    std::vector<int> disabledComponentIds;
    MechDetailedDamageState detailedDamage;
};

struct ResolvedMechRuntimeState {
    std::string requestedAnimationId;
    std::string activeAnimationId;
    bool mechDestroyed = false;
    bool usesAssemblyFrameDeath = false;
    std::vector<int> destroyedComponentIds;
    std::vector<int> hiddenComponentIds;
    std::vector<int> disabledComponentIds;
    MechComponentVisibility renderVisibility;
};

struct ResolvedMechComponentState {
    int componentId = 0;
    bool destroyed = false;
    bool hidden = false;
    bool disabled = false;
    bool visible = true;
};

void setMechRuntimeAnimation(MechRuntimeState& state, const std::string& animationId);
void destroyMechRuntimeComponent(MechRuntimeState& state, int componentId);
void hideMechRuntimeComponent(MechRuntimeState& state, int componentId);
void disableMechRuntimeComponent(MechRuntimeState& state, int componentId);
void clearHiddenMechRuntimeComponents(MechRuntimeState& state);

ResolvedMechRuntimeState resolveMechRuntimeState(
    const MechRuntimeState& state,
    const std::vector<MechComponentDamageRule>& damageRules,
    const std::string& deathAnimationId = "death");

bool isResolvedMechComponentVisible(
    const ModelAssembly& assembly,
    const ResolvedMechRuntimeState& state,
    int componentId);
ResolvedMechComponentState resolveMechComponentState(
    const ModelAssembly& assembly,
    const ResolvedMechRuntimeState& state,
    int componentId);
std::vector<ResolvedMechComponentState> resolveMechComponentStates(
    const ModelAssembly& assembly,
    const ResolvedMechRuntimeState& state);

} // namespace mw::mech3d
