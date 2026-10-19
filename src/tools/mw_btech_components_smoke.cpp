// Expected values come from pinned original BTECH instructions, not C++ tables.
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
void check(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
std::vector<uint8_t> unhex(const std::string& text) {
    check(text.size()==40,"bad original component fixture hex");std::vector<uint8_t> bytes;
    for(size_t i=0;i<20;++i)bytes.push_back(static_cast<uint8_t>(std::stoul(text.substr(i*2,2),nullptr,16)));
    return bytes;
}
BattleStartParams params(const std::string& preset) {
    BattleStartParams p;p.playerMechPresetId=preset;p.deterministicCombatRuntimeEnabled=true;
    p.individualWeaponRuntimeEnabled=true;
    BattleCombatantLaunchState entry;entry.mechPresetId=preset;
    entry.roster.team=BattleTeam::Player;entry.roster.sourceSlot="player:0";
    entry.persistentMechState.valid=true;entry.ammunitionStateValid=true;
    const auto ammo=mw::mech3d::catalogAmmunitionProfile(preset).maximumByPool;
    std::copy(ammo.begin(),ammo.end(),entry.ammunitionByPool.begin());
    p.playerLaunchState=entry;return p;
}
uint8_t runtime(const mw::mech3d::MechDetailedDamageState& d,size_t i) {
    if(i>=10)return d.installedWeapons[i-10].functional?1:0;
    const auto& c=d.criticalComponents[i];
    if(i==4 || i==9)return static_cast<uint8_t>(c.totalCount-c.workingCount);
    return i>=5 && i<=8 ? c.workingCount : c.condition;
}
void setRuntime(mw::mech3d::MechDetailedDamageState& d,size_t i,uint8_t value) {
    if(i>=10) {d.installedWeapons[i-10].functional=value!=0;return;}
    auto& c=d.criticalComponents[i];
    if(i==4 || i==9)c.workingCount=static_cast<uint8_t>(c.totalCount-value);
    else if(i>=5 && i<=8)c.workingCount=value;
    else {c.condition=value;c.workingCount=static_cast<uint8_t>(value<(i==0?3:2));}
    c.functional=c.totalCount==0 || c.workingCount!=0;
}
void oracle() {
    std::ifstream in("research/fixtures/original_btech_components.txt");check(bool(in),"missing component oracle");
    size_t entries=0,exits=0;std::string line;
    while(std::getline(in,line)) {
        if(line.empty()||line[0]=='#')continue;
        std::istringstream row(line);char kind;std::string preset,rawHex,outHex;unsigned heat,jets;
        row>>kind>>preset>>heat>>jets>>rawHex;
        std::array<unsigned,20> words{};for(auto& word:words)row>>word;
        row>>outHex;check(bool(row),"truncated component oracle");
        const auto raw=unhex(rawHex),expected=unhex(outHex);
        auto gam=std::vector<uint8_t>(1816,0xa5);std::copy(raw.begin(),raw.end(),gam.begin()+0x102);
        std::fill(gam.begin()+0x116,gam.begin()+0x11f,uint8_t(0));
        auto p=params(preset);auto& entry=p.playerLaunchState->persistentMechState;
        entry.heatSinksTotal=static_cast<uint8_t>(heat);entry.jumpJetsTotal=static_cast<uint8_t>(jets);
        entry=decodeOriginalGamMechDamage(gam,0x102,entry);
        auto c=BattleWorld::create(p).snapshot().combatants.at(0);
        if(kind=='E') {
            ++entries;
            for(size_t i=0;i<20;++i)if(words[i]!=255)check(runtime(c.detailedDamage,i)==words[i],"component entry differs from BTECH");
            for(const auto& weapon:c.weapons)
                check(weapon.functional==(words[10+weapon.slotIndex]!=0),"weapon runtime differs from original installation byte");
        } else {
            check(kind=='X',"bad fixture type");++exits;
            for(size_t i=0;i<20;++i)if(words[i]!=255)setRuntime(c.detailedDamage,i,static_cast<uint8_t>(words[i]));
        }
        auto state=campaignMechStateFromCombatant(c);
        check(validatePersistentMechDamage(state,preset),"source component projection became invalid");
        auto encoded=gam;encodeOriginalGamMechDamage(encoded,0x102,state);
        for(size_t i=0;i<20;++i)check(encoded[0x102+i]==expected[i],"component exit differs from original instructions");
        check(std::equal(encoded.begin(),encoded.begin()+0x102,gam.begin()) &&
              std::equal(encoded.begin()+0x11f,encoded.end(),gam.begin()+0x11f),"component codec changed unrelated GAM bytes");
        const auto exact=persistentMechDamageFingerprint(*state.exactDamage);
        p.playerLaunchState->persistentMechState=state;
        auto again=campaignMechStateFromCombatant(BattleWorld::create(p).snapshot().combatants.at(0));
        check(persistentMechDamageFingerprint(*again.exactDamage)==exact,"component exact state changed on redeployment");
        // Armor repair must not normalize an unchanged LIGHT/HEAVY component,
        // a disabled HEAVY weapon or an inactive original weapon byte.
        const auto fields=encoded;
        repairCampaignMechDamage(state,preset,CampaignMechRepairKind::ArmorSection,0);
        encodeOriginalGamMechDamage(encoded,0x102,state);
        check(std::equal(encoded.begin()+0x102,encoded.begin()+0x116,fields.begin()+0x102),"unrelated repair changed component grades");
    }
    check(entries==1088 && exits==1944,"incomplete component oracle");
}
void liveCaptures() {
    const auto read=[](const std::string& path) {
        std::ifstream in(path,std::ios::binary);check(bool(in),"missing live component capture");
        return std::vector<uint8_t>{std::istreambuf_iterator<char>(in),{}};
    };
    constexpr std::array<size_t,10> offsets{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
    for(int index=1;index<=2;++index) {
        const auto root=std::string("research/fixtures/dosbox_components/");
        const auto gam=read(root+"CMP"+std::to_string(index)+".GAM");
        const auto ds=read(root+"cmp"+std::to_string(index)+"-battle.ds.bin");
        const auto transfer=read(root+"cmp"+std::to_string(index)+"-battle.gam");
        check(gam.size()==1816 && ds.size()==65536 && transfer.size()==1816,"live component capture size");
        check(std::equal(gam.begin()+0x102,gam.begin()+0x11f,transfer.begin()+0x102),"live transfer changed input condition record");
        auto p=params("jenner");auto& state=p.playerLaunchState->persistentMechState;
        state.heatSinksTotal=10;state.jumpJetsTotal=3;state=decodeOriginalGamMechDamage(gam,0x102,state);
        const auto c=BattleWorld::create(p).snapshot().combatants.at(0);
        for(size_t i=0;i<15;++i) {
            const size_t address=0x4fb6+(i<10?offsets[i]:0x3b+(i-10)*3);
            const unsigned expected=ds[address]+(i<10?(unsigned(ds[address+1])<<8):0u);
            check(runtime(c.detailedDamage,i)==expected,"replacement component differs from live DOS preflight");
        }
        auto returned=gam;encodeOriginalGamMechDamage(returned,0x102,campaignMechStateFromCombatant(c));
        check(std::equal(gam.begin()+0x102,gam.begin()+0x116,returned.begin()+0x102),"live input no-hit return changed grades");
    }
}
void previousCompanion() {
    // Earlier v1 saves can contain runtime condition 3 for sensors and a
    // functional HEAVY weapon. Exact records remain authoritative on Restore.
    auto p=params("jenner");
    auto state=campaignMechStateFromCombatant(BattleWorld::create(p).snapshot().combatants.at(0));
    state.sensors=3;state.weaponConditions[0]=2;
    auto& oldSensor=state.exactDamage->detailed.criticalComponents[2];
    oldSensor.condition=3;oldSensor.functional=false;oldSensor.workingCount=0;oldSensor.battleDamage=2;
    auto& oldWeapon=state.exactDamage->detailed.installedWeapons[0];
    oldWeapon.condition=2;oldWeapon.functional=true;oldWeapon.battleDamage=1;
    auto gam=std::vector<uint8_t>(1816,0);encodeOriginalGamMechDamage(gam,0x102,state);
    const auto extra=encodeCampaignMechSave(gam,{{"jenner",state.exactDamage}});
    const auto restored=decodeCampaignMechSave(gam,extra);
    const auto hash=persistentMechDamageFingerprint(*state.exactDamage);
    check(restored.size()==1 && persistentMechDamageFingerprint(*restored[0].damage)==hash,
        "previous v1 component records were reinterpreted by the codec");
    state=decodeOriginalGamMechDamage(gam,0x102,state);state.exactDamage=restored[0].damage;
    p.playerLaunchState->persistentMechState=state;
    const auto actor=BattleWorld::create(p).snapshot().combatants.at(0);
    check(actor.detailedDamage.criticalComponents[2].condition==3 && actor.weapons[0].functional,
        "previous exact entry was replaced by current legacy tables");
    auto returned=campaignMechStateFromCombatant(actor);
    check(persistentMechDamageFingerprint(*returned.exactDamage)==hash &&
        returned.sensors==3 && returned.weaponConditions[0]==0,
        "previous exact exit lost state or failed its explicit coarse compatibility guard");
    repairCampaignMechDamageStep(returned,"jenner",CampaignMechRepairKind::CriticalComponent,2);
    check(returned.sensors==2 && returned.exactDamage->detailed.criticalComponents[2].condition==1 &&
        returned.exactDamage->detailed.criticalComponents[2].functional &&
        returned.exactDamage->detailed.installedWeapons[0].condition==2 &&
        returned.exactDamage->detailed.installedWeapons[0].functional,
        "paid sensor repair did not reconcile only the selected previous-v1 field");
}
void repairs() {
    for(const auto& catalog:mw::mech3d::mechCatalogEntries()) {
        for(size_t field=0;field<20;++field)for(uint8_t level=1;level<=3;++level) {
            if(field==4 || field==9)continue;
            if(field>=10 && field-10>=mw::mech3d::catalogWeaponMounts(catalog.presetId).size())continue;
            auto gam=std::vector<uint8_t>(1816,0);gam[0x102+field]=level;
            auto p=params(catalog.presetId);auto& entry=p.playerLaunchState->persistentMechState;
            entry.heatSinksTotal=10;entry=decodeOriginalGamMechDamage(gam,0x102,entry);
            auto state=campaignMechStateFromCombatant(BattleWorld::create(p).snapshot().combatants.at(0));
            auto before=state;auto encoded=gam;encodeOriginalGamMechDamage(encoded,0x102,state);
            repairCampaignMechDamageStep(state,catalog.presetId,field<10?CampaignMechRepairKind::CriticalComponent:CampaignMechRepairKind::Weapon,field<10?field:field-10);
            auto after=encoded;encodeOriginalGamMechDamage(after,0x102,state);
            check(after[0x102+field]==level-1,"paid component step is not one GAM grade");
            after[0x102+field]=encoded[0x102+field];check(after==encoded,"paid component repair altered unrelated fields");
            const unsigned remaining=level-1;
            const unsigned critical[]={0,1,1,2};const unsigned actuators[]={4,3,1,0};
            const unsigned expected=field==0?remaining:field<4?critical[remaining]:field<10?actuators[remaining]:remaining<2;
            check(runtime(state.exactDamage->detailed,field)==expected,"paid repair did not use original entry conversion");
            auto isolated=*state.exactDamage;
            if(field<10)isolated.detailed.criticalComponents[field]=before.exactDamage->detailed.criticalComponents[field];
            else isolated.detailed.installedWeapons[field-10]=before.exactDamage->detailed.installedWeapons[field-10];
            isolated.mechDestroyed=before.exactDamage->mechDestroyed;isolated.mechDisabled=before.exactDamage->mechDisabled;
            check(persistentMechDamageFingerprint(isolated)==persistentMechDamageFingerprint(*before.exactDamage),"paid repair changed unrelated exact damage");
        }
    }
    // Actual firing: original LIGHT permits a shot; HEAVY/JUNK block it.
    for(uint8_t level=0;level<4;++level) {
        auto p=params("jenner");p.playerLaunchState->persistentMechState.weaponConditions[0]=level;
        auto world=BattleWorld::create(p);BattleInputCommand trigger;trigger.entityId=world.playerEntityId();
        trigger.fireWeapon=true;trigger.selectWeaponInstanceId=1;world.enqueueInput(trigger);world.tick();
        const auto c=world.snapshot().combatants.at(0);
        check(c.weapons[0].functional==(level<2),"weapon entry availability regression");
        check((c.weapons[0].cooldownTicksRemaining>0)==(level<2),"actual fire disagrees with original functional byte");
    }
}
}
int main() {try {oracle();liveCaptures();previousCompanion();repairs();std::cout<<"BTECH components: "<<checks<<" checks; entry=1088 exit=1944 catalog=8 live=2 repairs=verified firing=verified\n";return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
