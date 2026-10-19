// Original instruction fixtures plus actual legacy/exact battle entry and return.
#include "battle/battle_world.h"
#include "battle/campaign_mech_persistence.h"
#include "mech3d/mech_catalog.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool ok,const char* why) { ++checks; if (!ok) throw std::runtime_error(why); }
// Original runtime ordinals are compared after the production GAM codec.
constexpr std::array<size_t,9> runtimeArmor{{1,0,3,2,5,6,8,7,4}};
constexpr std::array<size_t,8> runtimeInternal{{1,0,3,2,5,7,6,4}};
constexpr std::array<size_t,9> originalGam{{1,0,3,2,5,6,7,8,4}};
constexpr std::array<size_t,9> originalInternal{{0,1,2,3,4,4,5,6,7}};
std::array<uint8_t,9> fromHex(const std::string& text) {
    check(text.size()==18,"invalid original armor fixture"); std::array<uint8_t,9> out{};
    for (size_t i=0;i<9;++i) out[i]=static_cast<uint8_t>(std::stoul(text.substr(i*2,2),nullptr,16));
    return out;
}
BattleStartParams params(const std::string& preset) {
    BattleStartParams p; p.playerMechPresetId=preset;
    p.deterministicCombatRuntimeEnabled=true; p.individualWeaponRuntimeEnabled=true;
    BattleCombatantLaunchState launch; launch.mechPresetId=preset;
    launch.roster.team=BattleTeam::Player; launch.roster.sourceSlot="player:0";
    launch.persistentMechState.valid=true;
    launch.persistentMechState.heatSinksTotal=launch.persistentMechState.heatSinksWorking=10;
    p.playerLaunchState=launch; return p;
}
std::vector<uint8_t> bytes(const std::string& path) {
    std::ifstream in(path,std::ios::binary);check(bool(in),"missing old companion fixture");
    return {(std::istreambuf_iterator<char>(in)),{}};
}
void oracle() {
    std::ifstream file("research/fixtures/original_btech_armor.txt");check(bool(file),"missing BTECH armor oracle");
    size_t entries=0,exits=0;std::string line;
    while (std::getline(file,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream in(line);char kind;std::string preset,entryHex,exitHex;
        in>>kind>>preset; auto p=params(preset);
        if (kind=='E') {
            in>>entryHex;const auto raw=fromHex(entryHex);
            auto gam=std::vector<uint8_t>(1816,0);
            std::copy(raw.begin(),raw.end(),gam.begin()+0x116);
            p.playerLaunchState->persistentMechState=decodeOriginalGamMechDamage(gam,0x102,p.playerLaunchState->persistentMechState);
        }
        std::array<uint16_t,9> armor{};std::array<uint16_t,8> internal{};
        for (auto& n:armor) in>>n;
        for (auto& n:internal) in>>n;
        in>>exitHex;check(bool(in),"truncated BTECH armor fixture");const auto raw=fromHex(exitHex);
        auto c=BattleWorld::create(p).snapshot().combatants.at(0);
        const auto profile=mw::mech3d::catalogDamageProfile(preset);
        if (kind=='E') {
            ++entries;
            for (size_t i=0;i<9;++i) check(c.detailedDamage.armorSections[runtimeArmor[i]].armorRemaining==armor[i],"entry external armor differs from BTECH");
            for (size_t i=0;i<8;++i) check(c.detailedDamage.internalSections[runtimeInternal[i]].structureRemaining==internal[i],"entry internal armor/clamp differs from BTECH");
        } else {
            check(kind=='X',"invalid armor fixture kind");++exits;
            // No battleDamage counters: original exit depends on points, not
            // diagnostic history. This also exercises pre-existing exact saves.
            for (size_t i=0;i<9;++i) c.detailedDamage.armorSections[runtimeArmor[i]].armorRemaining=armor[i];
            for (size_t i=0;i<8;++i) c.detailedDamage.internalSections[runtimeInternal[i]].structureRemaining=internal[i];
        }
        const auto state=campaignMechStateFromCombatant(c);
        check(validatePersistentMechDamage(state,preset),"projected exact armor became invalid");
        auto encoded=std::vector<uint8_t>(1816,0xa5);encodeOriginalGamMechDamage(encoded,0x102,state);
        for (size_t i=0;i<9;++i) {
            check(encoded[0x116+i]==raw[i],"raw GAM battle return differs from BTECH");
            check(state.armorDamage[runtimeArmor[i]]==raw[originalGam[i]],"battle return rounding differs from BTECH");
            check(originalCampaignArmorLevel(profile.externalArmorMaximumBtechOrder[i],
                profile.internalStructureMaximumBtechOrder[originalInternal[i]],armor[i],internal[originalInternal[i]])==raw[originalGam[i]],
                "original armor helper differs from instructions");
            check(state.exactDamage->detailed.armorSections[runtimeArmor[i]].armorRemaining==armor[i],"coarse projection changed exact armor");
        }
        for (size_t i=0;i<8;++i) check(state.exactDamage->detailed.internalSections[runtimeInternal[i]].structureRemaining==internal[i],"coarse projection changed exact internal");
        // The lab owns only the paid armor byte; a critical repair must not
        // silently re-quantize unrelated legacy grades or exact points.
        auto repaired=state; const auto grades=repaired.armorDamage;
        repairCampaignMechDamage(repaired,preset,CampaignMechRepairKind::CriticalComponent,0);
        check(repaired.armorDamage==grades,"critical repair changed unrelated armor bill");
    }
    check(entries==760 && exits==2791,"incomplete BTECH armor oracle");
}
void exactAndLegacy() {
    for (const auto& catalog:mw::mech3d::mechCatalogEntries()) {
        auto p=params(catalog.presetId);
        p.playerLaunchState->persistentMechState.armorDamage[2]=3;
        p.playerLaunchState->persistentMechState.armorDamage[3]=3;
        auto c=BattleWorld::create(p).snapshot().combatants.at(0);
        check(c.detailedDamage.internalSections[2].structureRemaining==1 &&
            c.detailedDamage.internalSections[3].structureRemaining==1,"legacy zero legs not clamped");
        auto state=campaignMechStateFromCombatant(c);
        state.exactDamage->detailed.internalSections[2].structureRemaining=0;
        state.exactDamage->detailed.internalSections[3].structureRemaining=0;
        state.exactDamage->mechDestroyed=true;
        const auto hash=persistentMechDamageFingerprint(*state.exactDamage);
        p.playerLaunchState->persistentMechState=state;
        auto wreck=BattleWorld::create(p).snapshot().combatants.at(0);
        check(wreck.mechDestroyed && wreck.detailedDamage.internalSections[2].structureRemaining==0 &&
            wreck.detailedDamage.internalSections[3].structureRemaining==0,"exact wreck revived by legacy clamp");
        check(persistentMechDamageFingerprint(*campaignMechStateFromCombatant(wreck).exactDamage)==hash,"exact wreck altered on entry");
        p=params(catalog.presetId);c=BattleWorld::create(p).snapshot().combatants.at(0);
        --c.detailedDamage.armorSections[0].armorRemaining;c.detailedDamage.armorSections[0].battleDamage=1;
        state=campaignMechStateFromCombatant(c);
        check(state.armorDamage[0]==0 && !battlePersistentMechStateIsPristine(state),"one-point original quantization lost exact damage");
        auto gam=std::vector<uint8_t>(1816,0x5a);encodeOriginalGamMechDamage(gam,0x102,state);
        check(gam[0x116]==0,"GAM has wrong original one-point grade");
        const auto companion=encodeCampaignMechSave(gam,{{catalog.presetId,state.exactDamage}});
        const auto saved=decodeCampaignMechSave(gam,companion);
        state.exactDamage=saved[0].damage;
        const auto exact=persistentMechDamageFingerprint(*state.exactDamage);
        for (int repeat=0;repeat<5;++repeat) {
            p.playerLaunchState->persistentMechState=state;
            state=campaignMechStateFromCombatant(BattleWorld::create(p).snapshot().combatants.at(0));
            check(state.armorDamage[0]==0 && persistentMechDamageFingerprint(*state.exactDamage)==exact,
                "zero-grade exact damage lost on redeployment");
        }
        auto plain=decodeOriginalGamMechDamage(gam,0x102,p.playerLaunchState->persistentMechState);
        check(!plain.exactDamage && plain.armorDamage[0]==0,"plain GAM must keep its original coarse precision");
    }
    const auto gam=bytes("research/fixtures/btech_armor/PARTIAL.GAM");
    const auto companion=bytes("research/fixtures/btech_armor/PARTIAL.GAM.mwstate");
    const auto old=decodeCampaignMechSave(gam,companion);
    check(gam.size()==1816 && old.size()==2 && encodeCampaignMechSave(gam,old)==companion,"previous version-1 companion incompatible");
    for (const auto& item:old) {
        auto p=params(item.mechPresetId);p.playerLaunchState->persistentMechState.exactDamage=item.damage;
        const auto c=BattleWorld::create(p).snapshot().combatants.at(0);
        check(persistentMechDamageFingerprint(*campaignMechStateFromCombatant(c).exactDamage)==persistentMechDamageFingerprint(*item.damage),
            "previous exact companion changed on deployment");
    }
}
}
int main() {try {oracle();exactAndLegacy();std::cout<<"BTECH armor: "<<checks<<" checks; entry=760 exit=2791 catalog=8 old_companion=v1 exact_preserved=1\n";return 0;}
catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}}
