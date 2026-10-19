#pragma once

#include <functional>

#include "battle/battle_module.h"

namespace mw::battle {

// BTECH 3815/3EAC/3F7B: victory at stage0/1 only. The caller owns the
// RAM hold and the RNG stream (one 0..9 roll per broken installed weapon).
void serviceOriginalMissionStage(BattleSnapshot& snapshot, std::array<int,6>& hold,
    const std::function<unsigned()>& weaponRepairRoll);

// GAM/MW_MAIN ordinal 7 enters BTECH external 6 (runtime LeftTorso),
// ordinal 8 enters external 7 (RightTorso). This permutation is its own inverse.
constexpr size_t originalGamArmorRuntimeIndex(size_t index) {
    return index == 7 ? 8 : index == 8 ? 7 : index;
}
// Exact v1 points keep their named runtime sides. Reconcile only the two coarse
// side grades after Restore, including companions saved before the ordinal fix.
void reconcileExactSideTorsoGrades(BattlePersistentMechState& state);

// Original BTECH entry tables: DS:08C8 (gyro/sensor/life support),
// DS:08D0 (four actuators), DS:08C0 (installed weapon functional byte).
uint8_t originalBtechCriticalCondition(size_t componentIndex, uint8_t gamLevel);
uint8_t originalBtechActuatorWorkingCount(uint8_t gamLevel);
bool originalBtechWeaponFunctional(uint8_t gamLevel);

// Original BTECH armor exit rounding, proven for all eight catalog profiles.
uint8_t originalCampaignArmorLevel(uint16_t armorMaximum, uint16_t structureMaximum,
    uint16_t armorRemaining, uint16_t structureRemaining);

BattlePersistentMechState campaignMechStateFromCombatant(const CombatantSnapshot& combatant);
bool validatePersistentMechDamage(const BattlePersistentMechState& state, const std::string& preset);
uint64_t persistentMechDamageFingerprint(const BattlePersistentDamage& damage);
void encodeOriginalGamMechDamage(std::vector<uint8_t>& gam, size_t offset, const BattlePersistentMechState& state);
BattlePersistentMechState decodeOriginalGamMechDamage(
    const std::vector<uint8_t>& gam, size_t offset, BattlePersistentMechState chassisDefaults);

enum class CampaignMechRepairKind { ArmorSection, CriticalComponent, Weapon, All };
void repairCampaignMechDamage(
    BattlePersistentMechState& state, const std::string& preset,
    CampaignMechRepairKind kind, size_t index = 0);

// One successful MW_MAIN repair step. Exact runtime reconstruction is a
// replacement repair policy using the proven BTECH entry conversion tables.
void repairCampaignMechDamageStep(BattlePersistentMechState& state,
    const std::string& preset, CampaignMechRepairKind kind, size_t index);

// BTECH 3BE9..3C50: actor 0 survives engine-only destruction. Output flags
// use launch crew bindings, not owned-mech slots. No injury state exists.
bool originalBtechPilotKilled(bool playerControlled, uint16_t lifeSupportCondition,
    uint16_t cockpitCriticalFlag, uint16_t engineCondition);

struct CampaignMechReturnEntry {
    size_t ownedMechIndex = 0;
    std::string mechPresetId;
    BattlePersistentMechState state;
    bool ammunitionStateValid = false;
    std::array<int, 6> ammunitionByPool{};
    int crewSlot = -1;
    bool pilotKilled = false;
};
struct CampaignMechReturnPlan {
    bool valid = false;
    std::string reason;
    uint64_t fingerprint = 0;
    std::vector<CampaignMechReturnEntry> entries;
    std::array<bool, 4> killedCrewSlots{};
};
CampaignMechReturnPlan campaignMechReturnPlan(
    const CampaignBattlePersistenceReport& report,
    const std::vector<std::string>& ownedPresets);

// Exact state is saved alongside the unchanged original GAM payload. The
// companion is bound to those bytes; stale/corrupt data must never be applied.
struct CampaignMechSaveEntry {
    std::string mechPresetId;
    std::optional<BattlePersistentDamage> damage;
};
std::vector<uint8_t> encodeCampaignMechSave(
    const std::vector<uint8_t>& gam, const std::vector<CampaignMechSaveEntry>& mechs);
std::vector<CampaignMechSaveEntry> decodeCampaignMechSave(
    const std::vector<uint8_t>& gam, const std::vector<uint8_t>& extra);
void writeCampaignSaveFiles(
    const std::filesystem::path& path, const std::vector<uint8_t>& gam,
    const std::vector<uint8_t>& extra);

} // namespace mw::battle
