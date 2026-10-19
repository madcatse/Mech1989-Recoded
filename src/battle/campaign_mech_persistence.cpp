#include "battle/campaign_mech_persistence.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace mw::battle {
namespace {
constexpr std::array<size_t, 9> internalForArmor{{0,1,2,3,4,5,5,6,7}};
constexpr std::array<size_t, 9> externalOrder{{1,0,3,2,5,6,8,7,4}};
constexpr std::array<size_t, 8> internalOrder{{1,0,3,2,5,7,6,4}};
uint64_t bytesHash(const std::vector<uint8_t>& bytes, size_t size) {
    uint64_t hash = 1469598103934665603ull;
    for (size_t i=0; i<size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
    return hash;
}
struct Writer {
    std::vector<uint8_t> bytes;
    void number(uint64_t value, size_t count=8) {
        for (size_t i=0; i<count; ++i) bytes.push_back(static_cast<uint8_t>(value>>(8*i)));
    }
    void string(const std::string& text) {
        number(text.size(),4); bytes.insert(bytes.end(),text.begin(),text.end());
    }
    void ids(const std::vector<int>& values) {
        number(values.size(),4); for (int value : values) number(value,4);
    }
};
struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t position=0;
    uint64_t number(size_t count=8) {
        if (count>bytes.size()-position) throw std::runtime_error("truncated mech-state companion");
        uint64_t value=0;
        for (size_t i=0; i<count; ++i) value |= uint64_t(bytes[position++])<<(8*i);
        return value;
    }
    uint8_t byte() { return static_cast<uint8_t>(number(1)); }
    uint16_t word() { return static_cast<uint16_t>(number(2)); }
    int damage() {
        const auto value=number(4);
        if (value>1000000000u) throw std::runtime_error("invalid saved damage counter");
        return static_cast<int>(value);
    }
    bool boolean() {
        const auto value=byte();
        if (value>1) throw std::runtime_error("invalid saved state flag");
        return value!=0;
    }
    std::string string() {
        const auto count=number(4);
        if (count>4096 || count>bytes.size()-position) throw std::runtime_error("invalid mech-state string");
        std::string result(bytes.begin()+position,bytes.begin()+position+static_cast<size_t>(count));
        position+=static_cast<size_t>(count); return result;
    }
    std::vector<int> ids() {
        const auto count=number(4);
        if (count>32) throw std::runtime_error("invalid saved component list");
        std::vector<int> result;
        for (size_t i=0;i<count;++i) result.push_back(damage());
        return result;
    }
};
void writeDamage(Writer& w, const BattlePersistentDamage& saved) {
    const auto& d=saved.detailed;
    w.number(d.valid,1); w.string(d.provenance);
    for (const auto& a : d.armorSections) {
        w.number(static_cast<uint8_t>(a.section),1); w.number(a.entryDamageLevel,1);
        w.number(a.armorMaximum,2); w.number(a.armorRemaining,2); w.number(a.battleDamage,4);
    }
    for (const auto& a : d.internalSections) {
        w.number(static_cast<uint8_t>(a.section),1); w.number(a.structureMaximum,2);
        w.number(a.structureRemaining,2); w.number(a.battleDamage,4);
    }
    for (const auto& c : d.criticalComponents) {
        w.number(static_cast<uint8_t>(c.component),1); w.number(c.condition,1);
        w.number(c.functional,1); w.number(c.workingCount,1); w.number(c.totalCount,1); w.number(c.battleDamage,4);
    }
    for (const auto& c : d.installedWeapons) {
        w.number(c.slotIndex,1); w.number(c.condition,1); w.number(c.functional,1); w.number(c.battleDamage,4);
    }
    w.number(d.originalCockpitCriticalFlag,1); w.number(d.originalJumpJetCriticalDestroyedMask,1);
    for (auto n : d.ammunitionCriticalHits) w.number(n);
    w.number(d.criticalAttempts); w.number(d.criticalHitsApplied); w.number(d.unresolvedCriticalSelections);
    w.number(d.criticalResolutionPending,1); w.number(d.unresolvedCriticalDamage,4);
    w.number(saved.mechDestroyed,1);
    w.number(saved.mechDisabled,1);
    w.ids(saved.destroyedComponentIds); w.ids(saved.hiddenComponentIds); w.ids(saved.disabledComponentIds);
}
BattlePersistentDamage readDamage(Reader& r) {
    BattlePersistentDamage saved;
    auto& d=saved.detailed;
    d.valid=r.boolean(); d.provenance=r.string();
    for (auto& a : d.armorSections) {
        a.section=static_cast<mech3d::MechArmorSectionId>(r.byte()); a.entryDamageLevel=r.byte();
        a.armorMaximum=r.word(); a.armorRemaining=r.word(); a.battleDamage=r.damage();
    }
    for (auto& a : d.internalSections) {
        a.section=static_cast<mech3d::MechInternalSectionId>(r.byte()); a.structureMaximum=r.word();
        a.structureRemaining=r.word(); a.battleDamage=r.damage();
    }
    for (auto& c : d.criticalComponents) {
        c.component=static_cast<mech3d::MechCriticalComponentId>(r.byte()); c.condition=r.byte();
        c.functional=r.boolean(); c.workingCount=r.byte(); c.totalCount=r.byte(); c.battleDamage=r.damage();
    }
    for (auto& c : d.installedWeapons) {
        c.slotIndex=r.byte(); c.condition=r.byte(); c.functional=r.boolean(); c.battleDamage=r.damage();
    }
    d.originalCockpitCriticalFlag=r.boolean(); d.originalJumpJetCriticalDestroyedMask=r.byte();
    for (auto& n : d.ammunitionCriticalHits) n=r.number();
    d.criticalAttempts=r.number(); d.criticalHitsApplied=r.number(); d.unresolvedCriticalSelections=r.number();
    d.criticalResolutionPending=r.boolean(); d.unresolvedCriticalDamage=r.damage();
    saved.mechDestroyed=r.boolean();
    saved.mechDisabled=r.boolean();
    saved.destroyedComponentIds=r.ids(); saved.hiddenComponentIds=r.ids(); saved.disabledComponentIds=r.ids();
    return saved;
}
std::array<uint8_t*,10> criticalFields(BattlePersistentMechState& state) {
    return {{&state.engine,&state.gyros,&state.sensors,&state.lifeSupport,nullptr,
        &state.leftArmActuator,&state.rightArmActuator,&state.leftLegActuator,&state.rightLegActuator,nullptr}};
}
void projectSummary(BattlePersistentMechState& state, const std::string& preset, bool battleReturn = false) {
    const auto& d=state.exactDamage->detailed;
    if (battleReturn) {
        auto fields=criticalFields(state);
        state.engine=std::min<uint8_t>(3,d.criticalComponents[0].condition);
        constexpr std::array<uint8_t,3> inverse{{0,2,3}};
        for (size_t i=1;i<=3;++i) {
            // Original compares converted entry value, not hit counters.
            // Clamping 3 to 2 is only for older exact replacement states;
            // original gyro/sensor/life-support words have range 0..2.
            const auto current=std::min<uint8_t>(2,d.criticalComponents[i].condition);
            if (originalBtechCriticalCondition(i,*fields[i])!=current) *fields[i]=inverse[current];
        }
        constexpr std::array<uint8_t,5> actuatorInverse{{3,2,1,1,0}};
        for (size_t i=5;i<=8;++i)
            *fields[i]=actuatorInverse[std::min<uint8_t>(4,d.criticalComponents[i].workingCount)];
        state.heatSinksWorking=d.criticalComponents[4].workingCount; state.heatSinksTotal=d.criticalComponents[4].totalCount;
        state.jumpJetsWorking=d.criticalComponents[9].workingCount; state.jumpJetsTotal=d.criticalComponents[9].totalCount;
        // Original only visits installed slots; inactive GAM bytes survive.
        for (const auto& mount:mech3d::catalogWeaponMounts(preset)) {
            auto& grade=state.weaponConditions[mount.slotIndex];
            const bool functional=d.installedWeapons[mount.slotIndex].functional;
            if (originalBtechWeaponFunctional(grade)!=functional) grade=functional?0:3;
        }
    }
    uint32_t remaining=0, maximum=0;
    for (size_t i=0;i<9;++i) {
        const auto& a=d.armorSections[i]; const auto& in=d.internalSections[internalForArmor[i]];
        // BTECH writes every armor byte on battle exit, even without new hits.
        // The +3 numerator bias can hide one lost point in coarse GAM; exact
        // points remain authoritative. Lab repair only changes its paid field.
        if (battleReturn)
            state.armorDamage[i]=originalCampaignArmorLevel(a.armorMaximum,in.structureMaximum,
                a.armorRemaining,in.structureRemaining);
        remaining+=a.armorRemaining; maximum+=a.armorMaximum;
    }
    state.armorPercent=maximum?static_cast<uint8_t>(remaining*100u/maximum):0;
    if (d.originalCockpitCriticalFlag) state.armorDamage[4]=std::max<uint8_t>(1,state.armorDamage[4]);
}
}

uint8_t originalBtechCriticalCondition(size_t componentIndex, uint8_t gamLevel) {
    const auto level=std::min<uint8_t>(3,gamLevel);
    constexpr std::array<uint8_t,4> converted{{0,1,1,2}};
    return componentIndex>=1 && componentIndex<=3 ? converted[level] : level;
}
uint8_t originalBtechActuatorWorkingCount(uint8_t gamLevel) {
    constexpr std::array<uint8_t,4> remaining{{4,3,1,0}};
    return remaining[std::min<uint8_t>(3,gamLevel)];
}
bool originalBtechWeaponFunctional(uint8_t gamLevel) { return gamLevel<2; }

uint8_t originalCampaignArmorLevel(uint16_t armorMaximum, uint16_t structureMaximum,
    uint16_t armorRemaining, uint16_t structureRemaining) {
    const uint32_t maximum=uint32_t(armorMaximum)+structureMaximum;
    if (!maximum) return 0; // Replacement fallback; all eight DOS profiles are nonzero.
    const uint32_t remaining=uint32_t(armorRemaining)+structureRemaining;
    return static_cast<uint8_t>(3u-std::min(3u,(3u*remaining+3u)/maximum));
}

void reconcileExactSideTorsoGrades(BattlePersistentMechState& state) {
    if (!state.exactDamage) return;
    const auto& d=state.exactDamage->detailed;
    for (size_t index : {size_t(7), size_t(8)}) {
        const auto& armor=d.armorSections[index];
        const auto& internal=d.internalSections[internalForArmor[index]];
        state.armorDamage[index]=originalCampaignArmorLevel(armor.armorMaximum,internal.structureMaximum,
            armor.armorRemaining,internal.structureRemaining);
    }
}

BattlePersistentMechState campaignMechStateFromCombatant(const CombatantSnapshot& combatant) {
    auto state=combatant.persistentMechState;
    if (!combatant.detailedDamage.valid) return state;
    state.valid=true;
    state.exactDamage=BattlePersistentDamage{combatant.detailedDamage,combatant.mechDestroyed ||
        combatant.missionStatus==CombatantMissionStatus::Destroyed,
        combatant.missionStatus==CombatantMissionStatus::Disabled,combatant.destroyedComponentIds,
        combatant.hiddenComponentIds,combatant.disabledComponentIds};
    projectSummary(state,combatant.mechPresetId,true);
    return state;
}

bool validatePersistentMechDamage(const BattlePersistentMechState& state, const std::string& preset) {
    if (!state.exactDamage) return true;
    try {
        const auto& saved=*state.exactDamage;
        const auto& d=saved.detailed;
        if (!state.valid || !d.valid || d.originalJumpJetCriticalDestroyedMask>7 || d.unresolvedCriticalDamage<0) return false;
        const auto ordinary=mech3d::catalogDamageProfile(preset);
        // Version1 already stores maxima: accept either complete original
        // profile, while rejecting mixed or arbitrary enlarged sections.
        const bool experienced=d.armorSections[externalOrder[2]].armorMaximum!=ordinary.externalArmorMaximumBtechOrder[2];
        const auto profile=mech3d::catalogDamageProfile(preset,experienced);
        for (size_t i=0;i<9;++i) {
            const auto index=externalOrder[i]; const auto& a=d.armorSections[index];
            if (static_cast<size_t>(a.section)!=index || a.entryDamageLevel>3 || a.battleDamage<0 ||
                a.armorMaximum!=profile.externalArmorMaximumBtechOrder[i] || a.armorRemaining>a.armorMaximum) return false;
        }
        for (size_t i=0;i<8;++i) {
            const auto index=internalOrder[i]; const auto& a=d.internalSections[index];
            if (static_cast<size_t>(a.section)!=index || a.battleDamage<0 ||
                a.structureMaximum!=profile.internalStructureMaximumBtechOrder[i] || a.structureRemaining>a.structureMaximum) return false;
        }
        for (size_t i=0;i<10;++i) {
            const auto& c=d.criticalComponents[i]; const auto& w=d.installedWeapons[i];
            if (static_cast<size_t>(c.component)!=i || c.condition>3 || c.workingCount>c.totalCount || c.battleDamage<0 ||
                w.slotIndex!=i || w.condition>3 || w.battleDamage<0) return false;
        }
        const auto definition=mech3d::makeCatalogMechModelDefinition(preset);
        for (const auto* ids : {&saved.destroyedComponentIds,&saved.hiddenComponentIds,&saved.disabledComponentIds}) {
            std::set<int> unique;
            for (int id : *ids) if (!unique.insert(id).second || std::none_of(definition.components.begin(),definition.components.end(),
                [id](const auto& c) { return c.componentId==id; })) return false;
        }
        return true;
    } catch (const std::exception&) { return false; }
}

static void refreshAfterRepair(BattlePersistentMechState& state, const std::string& preset) {
    auto& saved=*state.exactDamage;
    auto& d=saved.detailed;
    saved.mechDestroyed=!d.internalSections[4].structureRemaining ||
        !d.internalSections[5].structureRemaining || !d.criticalComponents[3].functional;
    mech3d::MechRuntimeState runtime;
    runtime.destroyedComponentIds=saved.destroyedComponentIds;
    runtime.hiddenComponentIds=saved.hiddenComponentIds;
    runtime.disabledComponentIds=saved.disabledComponentIds;
    saved.mechDestroyed = saved.mechDestroyed || mech3d::resolveMechRuntimeState(runtime,
        mech3d::catalogComponentDamageRules(preset)).mechDestroyed;
    saved.mechDisabled = !saved.mechDestroyed && (d.originalCockpitCriticalFlag ||
        !d.internalSections[2].structureRemaining || !d.internalSections[3].structureRemaining ||
        !d.criticalComponents[0].functional || !d.criticalComponents[1].functional);
    projectSummary(state,preset);
}

void serviceOriginalMissionStage(BattleSnapshot& snapshot, std::array<int,6>& hold,
    const std::function<unsigned()>& weaponRepairRoll) {
    std::array<CombatantSnapshot*,4> players{};
    for (auto& actor : snapshot.combatants) {
        if (actor.roster.team != BattleTeam::Player) continue;
        for (size_t slot=0; slot<players.size(); ++slot)
            if (actor.roster.sourceSlot == "player:"+std::to_string(slot)) {
                if (players[slot]) throw std::runtime_error("duplicate stage player slot");
                players[slot]=&actor;
            }
    }
    // 3EAC visits all four present actors round-robin for each ammo pool.
    for (size_t pool=0; pool<hold.size(); ++pool) {
        bool changed=true;
        while (hold[pool]>0 && changed) {
            changed=false;
            for (auto* actor : players) {
                if (!actor || !actor->ammunitionStateValid || !hold[pool]) continue;
                const auto capacity=mech3d::catalogAmmunitionProfile(actor->mechPresetId).maximumByPool[pool];
                if (actor->ammunitionByPool[pool] < capacity) {
                    ++actor->ammunitionByPool[pool]; --hold[pool]; changed=true;
                }
            }
        }
    }
    for (auto* actor : players) {
        if (!actor || !actor->detailedDamage.valid) continue;
        auto state=campaignMechStateFromCombatant(*actor);
        auto& saved=*state.exactDamage;
        auto& d=saved.detailed;
        for (size_t index : {size_t(3),size_t(2),size_t(0),size_t(1)}) {
            auto& c=d.criticalComponents[index];
            c.condition=static_cast<uint8_t>(c.condition*3u/5u);
            c.functional=c.condition < (index==0 ? 3 : 2);
            c.workingCount=c.functional ? c.totalCount : 0;
            c.battleDamage=std::min(c.battleDamage,static_cast<int>(c.condition));
        }
        d.originalCockpitCriticalFlag=false; // word0/1 *3/5
        // Actor+B is the MISSING heat-sink count (entry31E5, exit3AD9),
        // while our exact state stores working units. Actor+17 (missing jets)
        // is untouched by this service routine.
        auto& sinks=d.criticalComponents[4];
        const auto missing=(sinks.totalCount-sinks.workingCount)*3/5;
        sinks.workingCount=static_cast<uint8_t>(sinks.totalCount-missing);
        sinks.functional=sinks.totalCount==0 || sinks.workingCount!=0;
        sinks.battleDamage=std::min(sinks.battleDamage,missing);
        constexpr std::array<uint8_t,5> actuatorInverse{{3,2,1,1,0}};
        for (size_t index=5; index<=8; ++index) {
            auto& c=d.criticalComponents[index];
            c.workingCount=static_cast<uint8_t>(4-(4-c.workingCount)*3/5);
            c.condition=actuatorInverse[c.workingCount]; c.functional=c.workingCount!=0;
            c.battleDamage=std::min(c.battleDamage,4-static_cast<int>(c.workingCount));
        }
        for (const auto& mount : mech3d::catalogWeaponMounts(actor->mechPresetId)) {
            auto& w=d.installedWeapons[mount.slotIndex];
            if (!w.functional) {
                const auto roll=weaponRepairRoll();
                if (roll>=10) throw std::runtime_error("invalid BTECH weapon repair roll");
                if (roll>5) {w.functional=true; w.condition=0; w.battleDamage=0;}
            }
        }
        for (auto& a : d.armorSections) {
            a.armorRemaining+=static_cast<uint16_t>((a.armorMaximum-a.armorRemaining)*3/5);
            a.battleDamage=std::min(a.battleDamage,static_cast<int>(a.armorMaximum-a.armorRemaining));
        }
        for (auto& a : d.internalSections) {
            a.structureRemaining+=static_cast<uint16_t>((a.structureMaximum-a.structureRemaining)*3/5);
            a.battleDamage=std::min(a.battleDamage,static_cast<int>(a.structureMaximum-a.structureRemaining));
        }
        // Replacement model visibility is derived from the repaired structures.
        // Exact points persist without the original GAM exit/re-entry rounding.
        const std::array<const char*,6> labels{{"right_arm","left_arm","right_leg","left_leg","cockpit","torso"}};
        for (const auto& component : mech3d::makeCatalogMechModelDefinition(actor->mechPresetId).components)
            for (size_t i=0;i<labels.size();++i)
                if (d.internalSections[i].structureRemaining && component.debugLabel.find(labels[i])!=std::string::npos)
                    for (auto* ids : {&saved.destroyedComponentIds,&saved.hiddenComponentIds,&saved.disabledComponentIds})
                        ids->erase(std::remove(ids->begin(),ids->end(),component.componentId),ids->end());
        refreshAfterRepair(state,actor->mechPresetId);
        projectSummary(state,actor->mechPresetId,true);
        actor->detailedDamage=d;
        actor->destroyedComponentIds=saved.destroyedComponentIds;
        actor->hiddenComponentIds=saved.hiddenComponentIds;
        actor->disabledComponentIds=saved.disabledComponentIds;
        actor->mechDestroyed=saved.mechDestroyed;
        if (actor->missionStatus!=CombatantMissionStatus::Escaped)
            actor->missionStatus=saved.mechDestroyed ? CombatantMissionStatus::Destroyed :
                saved.mechDisabled ? CombatantMissionStatus::Disabled : CombatantMissionStatus::Active;
        // Keep persistentMechState as the immutable entry record. The returned
        // state is projected from detailedDamage by the normal return path.
    }
}

void repairCampaignMechDamage(BattlePersistentMechState& state, const std::string& preset,
    CampaignMechRepairKind kind, size_t index) {
    if (!state.exactDamage) return; // existing legacy repair path owns these fields
    if (!validatePersistentMechDamage(state,preset)) throw std::runtime_error("cannot repair invalid exact mech state");
    if (kind==CampaignMechRepairKind::All) {
        state.exactDamage.reset();
        state.engine=state.gyros=state.sensors=state.lifeSupport=0;
        state.leftArmActuator=state.rightArmActuator=state.leftLegActuator=state.rightLegActuator=0;
        state.heatSinksWorking=state.heatSinksTotal; state.jumpJetsWorking=state.jumpJetsTotal;
        state.armorPercent=100; state.armorDamage.fill(0); state.weaponConditions.fill(0);
        return;
    }
    auto& saved=*state.exactDamage; auto& d=saved.detailed;
    if (kind==CampaignMechRepairKind::ArmorSection) {
        auto& a=d.armorSections.at(index); auto& in=d.internalSections[internalForArmor.at(index)];
        a.armorRemaining=a.armorMaximum; a.battleDamage=0; a.entryDamageLevel=0;
        in.structureRemaining=in.structureMaximum; in.battleDamage=0; state.armorDamage[index]=0;
        if (index==4) d.originalCockpitCriticalFlag=false;
        const std::array<const char*,9> labels{{"right_arm","left_arm","right_leg","left_leg","cockpit","torso","torso","torso","torso"}};
        for (const auto& c : mech3d::makeCatalogMechModelDefinition(preset).components) {
            if (c.debugLabel.find(labels[index])==std::string::npos) continue;
            for (auto* ids : {&saved.destroyedComponentIds,&saved.hiddenComponentIds,&saved.disabledComponentIds})
                ids->erase(std::remove(ids->begin(),ids->end(),c.componentId),ids->end());
        }
    } else if (kind==CampaignMechRepairKind::CriticalComponent) {
        auto& c=d.criticalComponents.at(index);
        c.condition=0; c.functional=true; c.workingCount=c.totalCount; c.battleDamage=0;
        if (index==9) {d.originalJumpJetCriticalDestroyedMask=0;state.jumpJetsWorking=state.jumpJetsTotal;}
        else if (index==4) state.heatSinksWorking=state.heatSinksTotal;
        else *criticalFields(state).at(index)=0;
    } else {
        auto& w=d.installedWeapons.at(index); w.condition=0; w.functional=true; w.battleDamage=0;
        state.weaponConditions.at(index)=0;
    }
    refreshAfterRepair(state,preset);
}

void repairCampaignMechDamageStep(BattlePersistentMechState& state, const std::string& preset,
    CampaignMechRepairKind kind, size_t index) {
    if (kind!=CampaignMechRepairKind::CriticalComponent && kind!=CampaignMechRepairKind::Weapon)
        throw std::invalid_argument("invalid single-step repair kind");
    if (index>=10) throw std::out_of_range("single-step repair field");
    if (state.exactDamage && !validatePersistentMechDamage(state,preset))
        throw std::runtime_error("cannot repair invalid exact mech state");
    std::array<uint8_t*,10> fields{{&state.engine,&state.gyros,&state.sensors,&state.lifeSupport,
        nullptr,&state.leftArmActuator,&state.rightArmActuator,&state.leftLegActuator,&state.rightLegActuator,nullptr}};
    uint8_t remaining=0;
    if (kind==CampaignMechRepairKind::Weapon) {
        auto& value=state.weaponConditions[index]; if (!value) return; remaining=--value;
    } else if (index==4 || index==9) {
        auto& working=index==4 ? state.heatSinksWorking : state.jumpJetsWorking;
        const auto total=index==4 ? state.heatSinksTotal : state.jumpJetsTotal;
        if (working>=total) return;
        ++working; remaining=static_cast<uint8_t>(total-working);
    } else {
        auto& value=*fields[index]; if (!value) return; remaining=--value;
    }
    if (!state.exactDamage) return;
    auto& d=state.exactDamage->detailed;
    if (kind==CampaignMechRepairKind::Weapon) {
        auto& w=d.installedWeapons[index]; w.condition=remaining; w.functional=originalBtechWeaponFunctional(remaining);
        w.battleDamage=std::min(w.battleDamage,static_cast<int>(remaining));
    } else {
        auto& c=d.criticalComponents[index];
        if (index==4 || index==9) {
            c.workingCount=static_cast<uint8_t>(c.totalCount-remaining); c.condition=0;
            if (index==9 && d.originalJumpJetCriticalDestroyedMask)
                d.originalJumpJetCriticalDestroyedMask &= static_cast<uint8_t>(d.originalJumpJetCriticalDestroyedMask-1u);
        } else {
            c.condition=originalBtechCriticalCondition(index,remaining);
            c.workingCount=index>=5 && index<=8 ? originalBtechActuatorWorkingCount(remaining) :
                static_cast<uint8_t>(c.condition < (index>=1 && index<=3 ? 2u : 3u));
        }
        c.functional=c.totalCount==0 || c.workingCount!=0;
        c.battleDamage=std::min(c.battleDamage,static_cast<int>(remaining));
    }
    refreshAfterRepair(state,preset);
}

bool originalBtechPilotKilled(bool playerControlled, uint16_t lifeSupportCondition,
    uint16_t cockpitCriticalFlag, uint16_t engineCondition) {
    return lifeSupportCondition == 2 || cockpitCriticalFlag != 0 ||
        (!playerControlled && engineCondition == 3);
}

CampaignMechReturnPlan campaignMechReturnPlan(const CampaignBattlePersistenceReport& report,
    const std::vector<std::string>& ownedPresets) {
    CampaignMechReturnPlan plan;
    plan.fingerprint=report.reportFingerprint;
    if (!report.valid || !report.allBindingsMapped || !report.allMissionParticipantsLaunched || report.combatants.empty() ||
        report.combatants.size()!=report.mappedBindingCount || report.mappedBindingCount!=report.requestedBindingCount) {
        plan.reason="damage_return_roster_incomplete"; return plan;
    }
    std::set<size_t> claimed;
    std::set<int> claimedCrew;
    for (const auto& c : report.combatants) {
        if (!c.valid || c.ownedMechIndex<0 || static_cast<size_t>(c.ownedMechIndex)>=ownedPresets.size() ||
            c.crewSlot<0 || c.crewSlot>=4 || !claimedCrew.insert(c.crewSlot).second ||
            c.playerControlled!=(c.crewSlot==0) ||
            ownedPresets[c.ownedMechIndex]!=c.mechPresetId || !claimed.insert(c.ownedMechIndex).second ||
            !c.returnedMechState.exactDamage || !validatePersistentMechDamage(c.returnedMechState,c.mechPresetId) ||
            std::any_of(c.ammunitionByPool.begin(),c.ammunitionByPool.end(),[](int n) { return n<0 || n>255; })) {
            plan.reason="damage_return_identity_or_state_invalid"; plan.entries.clear(); return plan;
        }
        const auto& detail=c.returnedMechState.exactDamage->detailed;
        const bool killed=originalBtechPilotKilled(c.playerControlled,
            detail.criticalComponents[3].condition,detail.originalCockpitCriticalFlag,
            detail.criticalComponents[0].condition);
        plan.entries.push_back({static_cast<size_t>(c.ownedMechIndex),c.mechPresetId,c.returnedMechState,
            c.ammunitionStateValid,c.ammunitionByPool,c.crewSlot,killed});
        plan.killedCrewSlots[static_cast<size_t>(c.crewSlot)]=killed;
    }
    plan.valid=true; plan.reason="exact_campaign_mech_state_return_ready";
    return plan;
}

void encodeOriginalGamMechDamage(std::vector<uint8_t>& gam, size_t offset, const BattlePersistentMechState& s) {
    if (offset>gam.size() || gam.size()-offset<29) throw std::runtime_error("GAM mech record out of range");
    const std::array<uint8_t,10> fields{{s.engine,s.gyros,s.sensors,s.lifeSupport,
        static_cast<uint8_t>(s.heatSinksTotal-std::min(s.heatSinksTotal,s.heatSinksWorking)),s.leftArmActuator,
        s.rightArmActuator,s.leftLegActuator,s.rightLegActuator,
        static_cast<uint8_t>(s.jumpJetsTotal-std::min(s.jumpJetsTotal,s.jumpJetsWorking))}};
    std::copy(fields.begin(),fields.end(),gam.begin()+offset);
    std::copy(s.weaponConditions.begin(),s.weaponConditions.end(),gam.begin()+offset+10);
    for (size_t raw=0;raw<9;++raw) gam[offset+20+raw]=s.armorDamage[originalGamArmorRuntimeIndex(raw)];
}
BattlePersistentMechState decodeOriginalGamMechDamage(const std::vector<uint8_t>& gam,size_t offset,BattlePersistentMechState s) {
    if (offset>gam.size() || gam.size()-offset<29) throw std::runtime_error("GAM mech record out of range");
    auto condition=[&](size_t i) { return std::min<uint8_t>(3,gam[offset+i]); };
    s.valid=true; s.exactDamage.reset();
    s.engine=condition(0); s.gyros=condition(1); s.sensors=condition(2); s.lifeSupport=condition(3);
    s.heatSinksWorking=s.heatSinksTotal-std::min(s.heatSinksTotal,gam[offset+4]);
    s.leftArmActuator=condition(5); s.rightArmActuator=condition(6); s.leftLegActuator=condition(7); s.rightLegActuator=condition(8);
    s.jumpJetsWorking=s.jumpJetsTotal-std::min(s.jumpJetsTotal,gam[offset+9]);
    unsigned sum=0;
    for (size_t i=0;i<10;++i) s.weaponConditions[i]=condition(10+i);
    for (size_t i=0;i<9;++i) { s.armorDamage[originalGamArmorRuntimeIndex(i)]=condition(20+i); sum+=condition(20+i); }
    s.armorPercent=static_cast<uint8_t>((27-sum)*100/27);
    return s;
}

std::vector<uint8_t> encodeCampaignMechSave(const std::vector<uint8_t>& gam,const std::vector<CampaignMechSaveEntry>& mechs) {
    if (gam.size()!=0x718) throw std::runtime_error("invalid original GAM save size");
    if (mechs.size()>12) throw std::runtime_error("too many saved mechs");
    Writer w; w.string("MW_EXACT_MECH_STATE"); w.number(1,4); w.number(bytesHash(gam,gam.size())); w.number(mechs.size(),4);
    for (const auto& mech : mechs) {
        w.string(mech.mechPresetId); w.number(mech.damage.has_value(),1);
        if (mech.damage) {
            BattlePersistentMechState s; s.valid=true; s.exactDamage=mech.damage;
            if (!validatePersistentMechDamage(s,mech.mechPresetId)) throw std::runtime_error("invalid mech state at save");
            writeDamage(w,*mech.damage);
        }
    }
    const auto hash=bytesHash(w.bytes,w.bytes.size()); w.number(hash);
    return w.bytes;
}
std::vector<CampaignMechSaveEntry> decodeCampaignMechSave(const std::vector<uint8_t>& gam,const std::vector<uint8_t>& extra) {
    if (gam.size()!=0x718) throw std::runtime_error("invalid original GAM save size");
    if (extra.size()<8 || extra.size()>1048576) throw std::runtime_error("invalid mech-state companion size");
    Reader checksum{extra,extra.size()-8};
    if (checksum.number()!=bytesHash(extra,extra.size()-8)) throw std::runtime_error("damaged mech-state companion");
    Reader r{extra};
    if (r.string()!="MW_EXACT_MECH_STATE" || r.number(4)!=1 || r.number()!=bytesHash(gam,gam.size()))
        throw std::runtime_error("mech-state companion does not match this GAM save");
    const auto count=r.number(4);
    if (count>12) throw std::runtime_error("invalid saved mech count");
    std::vector<CampaignMechSaveEntry> result;
    for (size_t i=0;i<count;++i) {
        CampaignMechSaveEntry mech; mech.mechPresetId=r.string();
        if (r.boolean()) {
            mech.damage=readDamage(r);
            BattlePersistentMechState s; s.valid=true; s.exactDamage=mech.damage;
            if (!validatePersistentMechDamage(s,mech.mechPresetId)) throw std::runtime_error("invalid saved detailed mech state");
        }
        result.push_back(std::move(mech));
    }
    if (r.position!=extra.size()-8) throw std::runtime_error("trailing mech-state companion data");
    return result;
}

void writeCampaignSaveFiles(const std::filesystem::path& path,const std::vector<uint8_t>& gam,const std::vector<uint8_t>& extra) {
    namespace fs=std::filesystem;
    // Validate before touching an existing save. Backups permit rollback if the
    // second rename fails; a mismatched pair is rejected on load after a crash.
    static_cast<void>(decodeCampaignMechSave(gam,extra));
    const fs::path companion=path.wstring()+L".mwstate";
    const fs::path tempGam=path.wstring()+L".mw-new", tempExtra=companion.wstring()+L".mw-new";
    const fs::path oldGam=path.wstring()+L".mw-old", oldExtra=companion.wstring()+L".mw-old";
    if (fs::exists(oldGam) || fs::exists(oldExtra)) throw std::runtime_error("previous save backup needs recovery");
    if (fs::exists(tempGam) || fs::exists(tempExtra)) throw std::runtime_error("previous save temporary files need recovery");
    for (const auto& target : {path, companion})
        if (fs::exists(target) && !fs::is_regular_file(target)) throw std::runtime_error("save destination is not a regular file");
    const auto write=[](const fs::path& file,const std::vector<uint8_t>& bytes) {
        std::ofstream stream(file,std::ios::binary|std::ios::trunc);
        if (!stream || !stream.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))
            throw std::runtime_error("cannot write campaign save");
        stream.close(); if (!stream) throw std::runtime_error("cannot finish campaign save");
    };
    bool backedGam=false,backedExtra=false,newGam=false,newExtra=false;
    try {
        write(tempGam,gam); write(tempExtra,extra);
        if (fs::exists(path)) { fs::rename(path,oldGam); backedGam=true; }
        if (fs::exists(companion)) { fs::rename(companion,oldExtra); backedExtra=true; }
        fs::rename(tempGam,path); newGam=true;
        fs::rename(tempExtra,companion); newExtra=true;
    } catch (...) {
        std::error_code error;
        if (newGam) fs::remove(path,error);
        if (newExtra) fs::remove(companion,error);
        if (backedGam) fs::rename(oldGam,path,error);
        if (backedExtra) fs::rename(oldExtra,companion,error);
        fs::remove(tempGam,error); fs::remove(tempExtra,error);
        throw;
    }
    std::error_code error;
    if (backedGam) fs::remove(oldGam,error);
    if (backedExtra) fs::remove(oldExtra,error);
}
} // namespace mw::battle
