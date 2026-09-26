#pragma once

#include "mech3d/model_assembly.h"

#include <array>
#include <filesystem>
#include <cstdint>
#include <string>
#include <vector>

namespace mw::mech3d {

enum class MechCatalogAnimationKind {
    BindPose,
    ComponentFrames,
    AssemblyFrames,
};

enum class MechComponentDestroyedAction {
    HideComponent,
    DestroyMech,
};

enum class MechCatalogDestroyedSlotQuality {
    Unverified,
    AssemblyOnly,
    ComponentClean,
};

struct MechComponentDamageRule {
    int componentId = 0;
    MechComponentDestroyedAction destroyedAction = MechComponentDestroyedAction::HideComponent;
    std::string debugLabel;
};

struct MechCatalogEntry {
    std::string presetId;
    std::string debugName;
    std::filesystem::path defaultResourcePath;
    std::vector<std::string> animationIds;
    MechCatalogDestroyedSlotQuality destroyedSlotQuality = MechCatalogDestroyedSlotQuality::Unverified;
};

struct MechCatalogMobility {
    int maxSpeedKph = 0;
    int jumpCapacityMeters = 0;
    int jumpJetCount = 0;
    // Raw BTECH.EXE definition-row word +0x06. Zero means that no supported
    // BTECH 3D chassis row exists; it is not a compatibility speed fallback.
    int originalBtechDefinitionSpeedWord06 = 0;
    // Raw BTECH.EXE definition-row word +0x02, added at half value to the
    // c42e terrain height by the ordinary 87c4 pose transaction.
    int originalBtechDefinitionHeightWord02 = 0;

    bool jumpCapable() const {
        return jumpCapacityMeters > 0;
    }
};

// BTECH owns ten independent weapon slots per mech.  These catalog rows are
// the installed slot/type/location records recovered from MW_MAIN.EXE; runtime
// condition and ammunition remain battle-owned.
struct MechCatalogWeaponMount {
    uint8_t slotIndex = 0;
    std::string weaponTypeId;
    std::string displayName;
    std::string locationId;
    char displayRangeClass = 'M';
    int8_t ammunitionPoolIndex = -1;
    uint16_t originalCooldownUpdateCount = 0;
    uint16_t originalDamage = 0;
    uint16_t originalHeat = 0;
    uint16_t originalCriticalWeight = 0;
    std::array<uint16_t, 4> originalRangeWords{};
    int8_t originalProjectileTypeIndex = -1;
    uint16_t originalProjectileSpeedRaw = 0;
    uint16_t originalProjectileLifetimeUpdates = 0;
    uint16_t originalProjectileDamageClass = 0;
    uint16_t originalProjectileVisualClass = 0;

    bool usesAmmunition() const {
        return ammunitionPoolIndex >= 0;
    }

    uint16_t originalMaximumRangeWord() const {
        return originalRangeWords.back();
    }

    bool usesOriginalProjectilePool() const {
        return originalProjectileTypeIndex >= 0;
    }
};

// BTECH.EXE mech definition row +0xB2/+0xC4. The array orders are the
// original combat-runtime orders documented in PHASE12_WEAPON_EVIDENCE.md,
// not the campaign .GAM byte order.
struct MechCatalogDamageProfile {
    std::array<uint16_t, 9> externalArmorMaximumBtechOrder{};
    std::array<uint16_t, 8> internalStructureMaximumBtechOrder{};
};

struct MechCatalogJumpJetCriticalSlot {
    int8_t originalLocationIndex = -1;
    uint16_t originalWeight = 0;
};

// BTECH.EXE mech definition row +0x9A/+0xA6. Location indices use the
// original internal order LA,RA,LL,RL,CT,LT,RT,HD. Ammunition pools use the
// battle-runtime order AC5,LRM5,SRM2,SRM4,SRM6,MG.
struct MechCatalogCriticalProfile {
    std::array<int8_t, 6> ammunitionLocationBtechOrder{};
    std::array<MechCatalogJumpJetCriticalSlot, 3> jumpJetSlots{};
};

// BTECH.EXE mech definition row +0xD4. Pools use the battle-runtime order
// AC5,LRM5,SRM2,SRM4,SRM6,MG. A non-zero entry is also one present-pool slot
// in the six-plane campaign .GAM ammunition table.
struct MechCatalogAmmunitionProfile {
    std::array<uint16_t, 6> maximumByPool{};

    std::array<int8_t, 6> gamPlaneOrdinalByPool() const {
        std::array<int8_t, 6> result{{-1, -1, -1, -1, -1, -1}};
        int8_t ordinal = 0;
        for (size_t pool = 0; pool < maximumByPool.size(); ++pool) {
            if (maximumByPool[pool] != 0u) {
                result[pool] = ordinal++;
            }
        }
        return result;
    }
};

std::vector<MechCatalogEntry> mechCatalogEntries();
MechModelDefinition makeCatalogMechModelDefinition(const std::string& presetId);
MechCatalogAnimationKind catalogAnimationKind(const std::string& presetId, const std::string& animationId);
ModelAnimationDefinition makeCatalogAnimationDefinition(const std::string& presetId, const std::string& animationId);
ModelAssemblyFrameAnimationDefinition makeCatalogAssemblyFrameAnimationDefinition(
    const std::string& presetId,
    const std::string& animationId);
std::vector<MechComponentDamageRule> catalogComponentDamageRules(const std::string& presetId);
MechComponentDestroyedAction catalogDestroyedActionForComponent(const std::string& presetId, int componentId);
MechCatalogDestroyedSlotQuality catalogDestroyedSlotQuality(const std::string& presetId);
std::filesystem::path catalogDefaultResourcePath(const std::string& presetId);
MechCatalogMobility catalogMobility(const std::string& presetId);
std::vector<MechCatalogWeaponMount> catalogWeaponMounts(
    const std::string& presetId);
MechCatalogDamageProfile catalogDamageProfile(const std::string& presetId);
MechCatalogCriticalProfile catalogCriticalProfile(const std::string& presetId);
MechCatalogAmmunitionProfile catalogAmmunitionProfile(
    const std::string& presetId);
int catalogCockpitComponentId(const std::string& presetId);
float catalogCockpitCameraHeight(
    const std::string& presetId,
    const std::vector<legacy3d::RuntimeRecord>& records,
    const legacy3d::GpuBatchOptions& batchOptions = {});
float catalogHorizontalCollisionRadius(
    const std::string& presetId,
    const std::vector<legacy3d::RuntimeRecord>& records,
    const legacy3d::GpuBatchOptions& batchOptions = {});
std::vector<int> sourceRecordIndices(const MechModelDefinition& definition);

} // namespace mw::mech3d
