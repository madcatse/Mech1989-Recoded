#include "battle/battle_world.h"
#include "mech3d/mech_catalog.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

// Test the production routine directly without adding a public test-only API.
namespace mw::battle {
void applyDetailedCombatantDamage(const BattleStartParams&, OriginalBtechRng&, Combatant&, Combatant&,
                                 int, BattleShotDiagnostic&);
}
using namespace mw::battle;
namespace {
size_t checks=0;
void check(bool ok,const char* message) { ++checks; if(!ok) throw std::runtime_error(message); }
void oracle() {
    std::ifstream file("research/fixtures/original_btech_collision_damage.txt");
    check(file.good(),"missing collision damage oracle");
    std::string line; size_t rows=0;
    while(std::getline(file,line)) {
        if(line.empty() || line[0]=='#') continue;
        if(line[0]!='C') continue;
        std::istringstream input(line);char kind;int a,b,sa,sb,h,status;
        input>>kind>>a>>b>>sa>>sb>>h>>status;
        const auto calls=originalCollisionDamageCalls(a,b,static_cast<int16_t>(sa),
            static_cast<int16_t>(sb),static_cast<uint16_t>(h),0);
        for(const auto& call:calls) {
            int actor,section,damage;input>>actor>>section>>damage;
            check(call.actor==actor && call.externalArmor==section && call.damage==damage,
                  "collision arguments differ from original instructions");
        }
        int fa,fb;input>>fa>>fb;
        const auto fall=originalCollisionFall(a,b,static_cast<int16_t>(sa),
            static_cast<int16_t>(sb),static_cast<uint16_t>(h),0,status);
        check(fall.moverFalls==bool(fa) && fall.otherFalls==bool(fb),"fall decision changed");
        check(!input.fail(),"malformed collision fixture");++rows;
    }
    check(rows==23040,"incomplete collision fixture");
}
void zeroDamage() {
    const char* models[]={"locust","jenner","phoenix_hawk","shadow_hawk",
        "rifleman","warhammer","marauder","battlemaster"};
    for(const auto* model:models) {
        BattleStartParams params;params.playerMechPresetId=model;
        const auto initial=BattleWorld::create(params).snapshot().combatants.at(0);
        for(size_t section=0;section<9;++section) for(int armor:{0,1}) for(int structure:{0,1}) {
            Combatant target,attacker; target.id={2};attacker.id={1};target.mechPresetId=model;
            target.mechRuntime.detailedDamage=initial.detailedDamage;
            auto& damage=target.mechRuntime.detailedDamage;
            for(auto& a:damage.armorSections)a.armorRemaining=static_cast<uint16_t>(armor);
            for(auto& s:damage.internalSections)s.structureRemaining=static_cast<uint16_t>(structure);
            const auto before=damage;
            BattleShotDiagnostic shot;shot.armorSection=static_cast<mw::mech3d::MechArmorSectionId>(section);
            OriginalBtechRng rng; const auto beforeRng=rng;
            applyDetailedCombatantDamage(params,rng,attacker,target,0,shot);
            check(rng==beforeRng,"zero hit consumed shared RNG");
            check(shot.damageApplied==0 && !shot.criticalResolutionAttempted &&
                  shot.criticalHitsApplied==0,"zero damage rolled a critical");
            check(damage.criticalAttempts==before.criticalAttempts &&
                  damage.criticalHitsApplied==before.criticalHitsApplied,"zero changed critical counters");
            for(size_t i=0;i<9;++i)check(damage.armorSections[i].armorRemaining==before.armorSections[i].armorRemaining &&
                damage.armorSections[i].battleDamage==before.armorSections[i].battleDamage,"zero changed armor");
            for(size_t i=0;i<8;++i)check(damage.internalSections[i].structureRemaining==before.internalSections[i].structureRemaining &&
                damage.internalSections[i].battleDamage==before.internalSections[i].battleDamage,"zero changed internal");
            check(target.mechRuntime.destroyedComponentIds.empty(),"zero destroyed a part");
        }
        // A positive hit exactly equal to armor must retain the original critical call.
        Combatant target,attacker;target.id={2};attacker.id={1};target.mechPresetId=model;
        target.mechRuntime.detailedDamage=initial.detailedDamage;
        const auto ct=mw::mech3d::MechArmorSectionId::CenterTorso;
        target.mechRuntime.detailedDamage.armorSections[static_cast<size_t>(ct)].armorRemaining=1;
        BattleShotDiagnostic shot;shot.armorSection=ct;
        OriginalBtechRng rng;
        applyDetailedCombatantDamage(params,rng,attacker,target,1,shot);
        check(shot.criticalResolutionAttempted && shot.armorDamageApplied==1 &&
              shot.internalDamageApplied==0,"positive armor equality lost its critical check");
    }
}
}
int main() { try { oracle();zeroDamage();std::cout<<"mw_collision_damage_smoke: ok checks="<<checks<<'\n';return 0; }
    catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; } }
