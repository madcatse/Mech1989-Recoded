// Bounded integration test. The commander imports the pinned Phase15 terminal
// actor; ally losses and the permuted roster are controlled runtime inputs.
// This is not a newly observed DOS battle or a replay of its random history.
#include "../main.cpp"
#include <iostream>
#include <set>

namespace {
using namespace mw::battle;
size_t checks=0;
fs::path dosReturn;
void check(bool ok,const std::string& why) {++checks;if(!ok)throw std::runtime_error(why);}
std::string name(char prefix,size_t i) {
    std::ostringstream s;s<<prefix<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<i<<".GAM";return s.str();
}
void writeBytes(const fs::path& p,const std::vector<uint8_t>& bytes) {
    std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    check(bool(f),"cannot write isolated fixture");
}
uint64_t exactHash(const CombatantSnapshot& c) {
    return persistentMechDamageFingerprint(*campaignMechStateFromCombatant(c).exactDamage);
}
void perWeaponReplay() {
    std::set<std::string> covered;
    for(const auto& catalog:mw::mech3d::mechCatalogEntries())for(const auto& mount:mw::mech3d::catalogWeaponMounts(catalog.presetId)) {
        if(!covered.insert(mount.weaponTypeId).second)continue;
        const auto tag=" weapon="+mount.weaponTypeId;
        BattleStartParams p;p.constrainToTerrainBounds=false;p.fixedTickSeconds=.05;
        p.deterministicCombatRuntimeEnabled=true;p.individualWeaponRuntimeEnabled=true;
        p.mechSystemsSnapshotEnabled=true;p.originalProjectileRuntimeEnabled=true;p.originalHeatRuntimeEnabled=true;
        p.weaponTargetPolicy=BattleWeaponTargetPolicy::CrosshairRayVisibleCombatantProvisional;
        p.playerMechPresetId=catalog.presetId;
        BattleCombatantLaunchState player;player.mechPresetId=catalog.presetId;
        player.roster.team=BattleTeam::Player;player.roster.sourceSlot="player:0";
        player.startTransform={1000,0,1000,0};player.persistentMechState.valid=true;
        player.persistentMechState.armorPercent=100;player.persistentMechState.heatSinksTotal=10;
        player.persistentMechState.heatSinksWorking=10;player.ammunitionStateValid=true;
        const auto capacity=mw::mech3d::catalogAmmunitionProfile(catalog.presetId);
        for(size_t pool=0;pool<6;++pool)player.ammunitionByPool[pool]=capacity.maximumByPool[pool];
        p.playerLaunchState=player;
        auto target=player;target.mechPresetId="locust";target.roster.team=BattleTeam::Opposing;
        target.roster.sourceSlot="enemy:0";target.startTransform={1000,0,1400,3.14159265358979323846};
        p.combatantLaunchStates={target};
        // Controlled torso mesh, through the real crosshair and projectile
        // paths. Full catalog geometry has independent laser/visual tests.
        const auto profile=[](const std::string& preset) {
            BattleMechHitProfile h;h.valid=true;h.mechPresetId=preset;h.aimOriginHeight=100;
            h.cockpitViewportHeight=100;h.neutralCrosshairY=50;h.geometryFingerprint=0x5048313657454150ull;
            h.provenance="phase16_controlled_torso_mesh";
            h.triangles.push_back({{-30,90,0},{30,90,0},{30,110,0},1,mw::mech3d::MechArmorSectionId::CenterTorso,true});
            h.triangles.push_back({{-30,90,0},{30,110,0},{-30,110,0},1,mw::mech3d::MechArmorSectionId::CenterTorso,true});
            return h;
        };
        p.mechHitProfiles={profile(catalog.presetId)};
        if(catalog.presetId!="locust")p.mechHitProfiles.push_back(profile("locust"));
        auto world=BattleWorld::create(p);const auto before=world.snapshot();
        BattleInputCommand fire;fire.selectWeaponInstanceId=mount.slotIndex+1;fire.fireWeapon=true;fire.toggleAutomaticWeapons=true;
        BattleReplay replay;replay.commands={fire};
        fire.entityId=world.playerEntityId();world.enqueueInput(fire);world.tick();
        const auto launch=world.snapshot();const auto& shooter=launch.combatants.front();
        check(shooter.selectedWeaponInstanceId==static_cast<uint32_t>(mount.slotIndex)+1u && shooter.lastFireRequestAccepted && shooter.weaponShotsFired==1,"selection/fire failed"+tag);
        check(shooter.heat.lastWeaponHeatAdded==shooter.weapons[mount.slotIndex].originalHeat,"weapon heat differs"+tag);
        auto expectedAmmo=before.combatants.front().ammunitionByPool;
        const auto pool=shooter.weapons[mount.slotIndex].ammunitionPoolIndex;if(pool>=0)--expectedAmmo[pool];
        check(shooter.ammunitionByPool==expectedAmmo && shooter.weapons[mount.slotIndex].cooldownTicksRemaining>0,"ammo/cooldown differs"+tag);
        while(!world.snapshot().projectiles.empty() && world.tickIndex()<100)world.tick();
        const auto hit=world.snapshot();
        check(hit.lastShot.hit && hit.lastShot.damageApplied>0 && hit.projectiles.empty() &&
            exactHash(hit.combatants[1])!=exactHash(before.combatants[1]),"weapon did not deliver actual damage"+tag);
        const auto parsed=parseBattleReplay(serializeBattleReplay(replay));
        const auto replayed=runBattleReplay(p,parsed,world.tickIndex());
        check(battleSnapshotFingerprint(replayed)==battleSnapshotFingerprint(hit),"per-weapon replay changed"+tag);
        auto rendered=BattleWorld::create(p);rendered.enqueueInput(fire);
        for(size_t tick=0;tick<world.tickIndex();++tick) {
            const auto a=rendered.snapshot();rendered.tick();
            if(tick%2==0)for(double alpha:{0.,.5,1.})static_cast<void>(campaignBattleInterpolatedRenderSceneFromSnapshots(
                a,rendered.snapshot(),alpha,CampaignBattlePresentationMode::Cockpit,"Sorted Original Files"));
        }
        check(battleSnapshotFingerprint(rendered.snapshot())==battleSnapshotFingerprint(hit),"weapon render cadence changed state"+tag);
    }
    check(covered==std::set<std::string>{"small_laser","medium_laser","large_laser","ppc","machine_gun","ac5","lrm5","srm2","srm4","srm6"},
        "per-weapon validation omitted a supported type");
}
constexpr std::array<size_t,9> external{{1,0,3,2,5,6,8,7,4}};
constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
constexpr std::array<size_t,10> critical{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
void importActor(CombatantSnapshot& actor,const std::vector<uint8_t>& ds,size_t slot) {
    const size_t base=0x4fb6+slot*0x70;
    auto& d=actor.detailedDamage;
    for(size_t n=0;n<9;++n) {
        auto& a=d.armorSections[external[n]];
        a.armorRemaining=readU16Le(ds,base+0x19+n*2);
        check(a.armorRemaining<=a.armorMaximum,"captured armor outside model capacity");
        a.battleDamage=a.armorMaximum-a.armorRemaining;
    }
    for(size_t n=0;n<8;++n) {
        auto& a=d.internalSections[internal[n]];
        a.structureRemaining=readU16Le(ds,base+0x2b+n*2);
        check(a.structureRemaining<=a.structureMaximum,"captured internal outside model capacity");
        a.battleDamage=a.structureMaximum-a.structureRemaining;
    }
    for(size_t n=0;n<10;++n) {
        auto& c=d.criticalComponents[n];const auto value=readU16Le(ds,base+critical[n]);
        if(n==4 || n==9) {
            check(value<=c.totalCount,"captured destroyed count outside capacity: actor="+
                std::to_string(slot)+" component="+std::to_string(n));
            c.workingCount=static_cast<uint8_t>(c.totalCount-value);
        } else if(n>=5 && n<=8) c.workingCount=static_cast<uint8_t>(value);
        else {c.condition=static_cast<uint8_t>(value);c.workingCount=static_cast<uint8_t>(value<(n==0?3:2));}
        c.functional=c.totalCount==0 || c.workingCount!=0;
    }
    d.originalCockpitCriticalFlag=readU16Le(ds,base+0x15)!=0;
    for(const auto& mount:mw::mech3d::catalogWeaponMounts(actor.mechPresetId))
        d.installedWeapons[mount.slotIndex].functional=ds.at(base+0x3b+3*mount.slotIndex)!=0;
    for(size_t n=0;n<6;++n) actor.ammunitionByPool[n]=readU16Le(ds,base+0x59+2*n);
}

}

void runPhase16ValidationSmoke() {
    const fs::path root="outputs/phase16_validation";
    fs::create_directories(root);
    if(!dosReturn.empty()) {
        App loaded(findResourceRoot());loaded.loadResources();
        for(size_t i=0;i<4;++i) {
            const auto input=dosReturn/name('I',i),output=dosReturn/name('O',i);
            const auto gam=readFile(input),extra=readFile(input.wstring()+L".mwstate");
            check(readFile(output)==gam,"original DOS re-save changed bytes");
            loaded.loadGameFromFile(output);
            check(loaded.gamRawState_==gam && !loaded.contractAccepted_,"last DOS -> App leg failed or resurrected contract");
            check(std::none_of(loaded.ownedMechs_.begin(),loaded.ownedMechs_.end(),[](const auto& m){return m.exactDamage.has_value();}),
                "original-only final leg retained old exact data");
            const auto legacy=dosReturn/name('L',i);
            check(loaded.saveCurrentGame(legacy) && readFile(legacy)==gam,"DOS-only final App Save changed bytes");
            // Original file procedures preserve the GAM checksum, so the
            // matching companion remains valid. A changed GAM would be stale.
            const auto paired=dosReturn/name('E',i);
            writeBytes(paired,readFile(output));writeBytes(paired.wstring()+L".mwstate",extra);
            const auto expected=decodeCampaignMechSave(gam,extra);
            loaded.loadGameFromFile(paired);
            check(loaded.ownedMechs_.size()==expected.size(),"returned exact roster changed");
            for(size_t n=0;n<expected.size();++n) {
                const auto& actual=loaded.ownedMechs_[n].exactDamage;
                check(bool(actual)==bool(expected[n].damage),"returned exact presence changed");
                if(actual)check(persistentMechDamageFingerprint(*actual)==persistentMechDamageFingerprint(*expected[n].damage),
                    "returned exact points changed");
            }
            const auto stable=loaded.captureCampaignPersistentState().fingerprint;
            const auto money=loaded.playerWealth_;
            loaded.dismissMissionDebrief();
            check(loaded.playerWealth_==money && loaded.captureCampaignPersistentState().fingerprint==stable,
                "restored result was settled twice");
            const auto saved=dosReturn/name('R',i);
            check(loaded.saveCurrentGame(saved) && readFile(saved)==gam && readFile(saved.wstring()+L".mwstate")==extra,
                "final paired App Save changed GAM or companion");
        }
        std::cout<<"mw_phase16_validation_smoke: ok checks="<<checks<<" dos_return_cases=4 bytes=1816 legacy_and_exact=yes\n";
        return;
    }
    const auto dir=root/("cycle_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));
    check(fs::create_directories(dir),"cycle directory already exists");
    const auto source=readFile("research/fixtures/dosbox_live_victory/live-entry.gam");
    const auto terminal=readFile("research/fixtures/dosbox_live_victory/live-final.ds.bin");
    for(size_t variant=0;variant<2;++variant) {
        App app(findResourceRoot());app.loadResources();app.applyGamSaveToRuntime(source);
        check(app.contractAccepted_ && app.acceptedContract_.originalMissionId==1,"ordinary entry context missing");
        if(variant) {
            const auto old=app.ownedMechs_;
            for(size_t n=0;n<4;++n)app.ownedMechs_[n]=old[std::array<size_t,4>{3,0,2,1}[n]];
        }
        auto reserve=App::makeMech(App::ChassisId::Warhammer);
        reserve.assignedCrewSlot=-1;reserve.ammoPacks=App::ammunitionCapacityForMech(reserve);
        app.finalizeLoadedMech(reserve);app.ownedMechs_.push_back(reserve);
        const auto mechHashes=[&] {
            std::vector<uint64_t> hashes;
            for(const auto& m:app.ownedMechs_)hashes.push_back(battlePersistentMechStateFingerprint(App::battlePersistentMechStateFromOwnedMech(m,"validation")));
            return hashes;
        };
        const auto reserveHash=mechHashes().back();
        const auto checkpoint=dir/("GUARD"+std::to_string(variant)+".GAM");
        check(app.saveCurrentGame(checkpoint),"initial checkpoint Save failed");
        const auto diskGuard=readFile(checkpoint),extraGuard=readFile(checkpoint.wstring()+L".mwstate");
        app.beginAcceptedContractMissionSequence();app.missionParticipants_=app.currentMissionParticipants();
        check(app.missionParticipants_.size()==4,"four participant launch missing");
        const auto participants=app.missionParticipants_;
        BattleStartParams p;p.constrainToTerrainBounds=false;p.deterministicCombatRuntimeEnabled=true;
        p.individualWeaponRuntimeEnabled=true;p.mechSystemsSnapshotEnabled=true;
        p.setupMetadata.valid=true;p.setupMetadata.playerMode=4;p.setupMetadata.playerSlots.resize(4);
        for(size_t n=0;n<4;++n)p.setupMetadata.playerSlots[n].transform={10000+1500.*n,0,10000,0};
        app.applyCampaignPlayerLaunchStates(p);
        auto enemy=*p.playerLaunchState;enemy.roster.team=BattleTeam::Opposing;enemy.roster.sourceSlot="enemy:0";
        enemy.roster.originalLiveObjectSlot=uint8_t{4};enemy.startTransform={50000,0,50000,0};
        p.combatantLaunchStates.push_back(enemy);p.contract=app.battleContractMetadataFromOffer(app.acceptedContract_);
        app.acceptedBattleContract_=p.contract;
        check(p.combatConclusionDelaySeconds==5.,"ordinary conclusion delay changed");
        auto world=BattleWorld::create(p);const auto start=world.snapshot();
        world.setMissionStatus(start.combatants.back().id,CombatantMissionStatus::Destroyed);
        world.tick();check(!world.battleResult(),"ordinary result bypassed delay");
        for(size_t tick=0;tick<100 && !world.battleResult();++tick)world.tick();
        check(world.battleResult() && world.battleResult()->rawResultCode==0,"ordinary runtime result missing");
        auto final=world.snapshot();
        importActor(final.combatants.front(),terminal,0);
        // Separate controlled ally losses exercise every participant's exact
        // data, CT/side sections and shared ammo in the same transaction.
        for(size_t n=1;n<4;++n) {
            auto& c=final.combatants[n];
            auto& armor=c.detailedDamage.armorSections[std::array<size_t,3>{5,7,8}[n-1]];
            const auto loss=std::min<uint16_t>(armor.armorRemaining,static_cast<uint16_t>(n+2));
            armor.armorRemaining-=loss;armor.battleDamage+=loss;
            for(auto& ammo:c.ammunitionByPool)if(ammo>0)--ammo;
        }
        std::vector<uint64_t> expected(5);
        std::vector<std::array<int,6>> ammunition(5);
        for(size_t n=0;n<4;++n) {
            const auto slot=static_cast<size_t>(participants[n].mechIndex);
            expected[slot]=exactHash(final.combatants[n]);ammunition[slot]=final.combatants[n].ammunitionByPool;
        }
        auto& runtime=app.campaignBattleRuntime_;runtime.world=std::move(world);runtime.startSnapshot=start;
        std::reverse(final.combatants.begin(),final.combatants.end());runtime.currentPresentationSnapshot=final;
        runtime.presentationSnapshotsValid=true;runtime.missionParticipantCount=4;
        runtime.persistentStateBefore=app.captureCampaignPersistentState();
        for(size_t n=0;n<4;++n)runtime.persistentRosterBindings.push_back({"player:"+std::to_string(n),participants[n].mechIndex,static_cast<int>(n)});
        app.missionSequence_.employerPayoutPercent=uint16_t{100};app.missionSequence_.campaignModuleReturned=true;
        app.repairSession_.rng={5703,6536};app.finalizeCampaignBattleRuntimeOutcome();
        check(app.acceptedBattleMechReturnPlan_.valid && app.acceptedBattleMechReturnPlan_.entries.size()==4,"finalize lost return bindings");
        const auto good=app.acceptedBattleMechReturnPlan_;
        const auto before=app.captureCampaignPersistentState().fingerprint;
        ++app.playerWealth_;
        const auto staleBefore=app.captureCampaignPersistentState().fingerprint;
        check(!app.commitAcceptedBattleConsequencePlan().committed && app.captureCampaignPersistentState().fingerprint==staleBefore,
            "stale precondition mutated campaign");
        --app.playerWealth_;
        app.acceptedBattleMechReturnPlan_.valid=false;
        check(!app.commitAcceptedBattleConsequencePlan().committed && app.captureCampaignPersistentState().fingerprint==before,
            "invalid four-actor plan mutated campaign");
        app.acceptedBattleMechReturnPlan_=good;
        auto& invalid=app.acceptedBattleMechReturnPlan_.entries.back();
        const auto capacity=App::ammunitionCapacityForMech(app.ownedMechs_[invalid.ownedMechIndex]);
        const auto empty=std::find(capacity.begin(),capacity.end(),0);
        check(empty!=capacity.end(),"test needs an absent pool");
        invalid.ammunitionByPool[static_cast<size_t>(empty-capacity.begin())]=255;
        const auto rolled=app.commitAcceptedBattleConsequencePlan();
        check(!rolled.committed && rolled.reason=="campaign_consequence_commit_verification_failed" &&
            app.captureCampaignPersistentState().fingerprint==before,"verification failure left partial result mutation: "+rolled.reason);
        app.acceptedBattleMechReturnPlan_=good;
        const auto committed=app.commitAcceptedBattleConsequencePlan();
        check(committed.committed && committed.verified && committed.saveRoundTripVerified,"combined result commit failed: "+committed.reason);
        const auto once=app.captureCampaignPersistentState().fingerprint;
        check(app.commitAcceptedBattleConsequencePlan().duplicateRejected && app.captureCampaignPersistentState().fingerprint==once,"duplicate changed campaign");
        app.dismissMissionDebrief();check(app.state_==ScreenState::MainMenu && !app.contractAccepted_,"Continue did not finish result");
        for(const auto& actor:participants) {
            const auto slot=static_cast<size_t>(actor.mechIndex);const auto& m=app.ownedMechs_[slot];
            check(m.exactDamage && persistentMechDamageFingerprint(*m.exactDamage)==expected[slot] && m.ammoPacks==ammunition[slot],"result swapped exact state/ammo");
        }
        check(mechHashes().back()==reserveHash,"result changed reserve");
        const auto pairCheck=[&](size_t index) {
            const auto beforeHashes=mechHashes();const auto path=dir/name('I',index);
            auto bytes=app.baseGamSaveData();app.writeRuntimeToGamSave(bytes);
            check(app.saveCurrentGame(path),"cycle Save failed");
            const auto extra=readFile(path.wstring()+L".mwstate");
            app.loadGameFromFile(path);
            check(mechHashes()==beforeHashes && app.gamRawState_==bytes && !app.contractAccepted_,"cycle Restore lost state");
            for(const auto& actor:participants)check(app.ownedMechs_[actor.mechIndex].ammoPacks==ammunition[actor.mechIndex],"repair/Restore reloaded ammunition");
            check(app.saveCurrentGame(path) && readFile(path)==bytes && readFile(path.wstring()+L".mwstate")==extra,"repeat pair Save changed bytes");
            app.missionParticipants_=app.currentMissionParticipants();auto entry=p;app.applyCampaignPlayerLaunchStates(entry);
            const auto redeployed=BattleWorld::create(entry).snapshot();
            for(size_t n=0;n<4;++n) {
                const auto& m=app.ownedMechs_[participants[n].mechIndex];
                check(m.exactDamage && exactHash(redeployed.combatants[n])==persistentMechDamageFingerprint(*m.exactDamage),"reentry rounded/swapped exact damage");
            }
        };
        pairCheck(variant*2);
        app.selectedMechIndex_=static_cast<size_t>(participants[0].mechIndex);
        auto others=mechHashes();
        const auto targets=app.repairableTargets(app.ownedMechs_[app.selectedMechIndex_]);
        const auto armor=std::find_if(targets.begin(),targets.end(),[](const auto& t){return App::repairSelector(t)==10;});
        check(armor!=targets.end(),"captured commander has no armor repair target");
        const auto quote=app.repairTargetCost(app.ownedMechs_[app.selectedMechIndex_],*armor);
        const auto money=app.playerWealth_;app.repairSelectionIndex_=static_cast<size_t>(armor-targets.begin());
        app.repairSelectedMechTarget();
        check(app.playerWealth_==money-quote,"individual armor payment differs");
        auto& commander=app.ownedMechs_[app.selectedMechIndex_];
        const auto engine=commander.exactDamage->detailed.criticalComponents[0];
        check(engine.condition==1,"unpaid engine damage was healed");
        app.repairSession_.rng={0,0};app.repairSession_.unavailable.fill(0);
        app.repairSession_.unavailable[App::gamChassisId(commander.chassis)*22]=1;
        const auto fullQuote=app.totalRepairCost(commander);
        const auto funds=app.playerWealth_;
        app.repairAllSelectedMech();
        check(commander.exactDamage && commander.exactDamage->detailed.criticalComponents[0].condition==engine.condition &&
            commander.exactDamage->detailed.criticalComponents[0].workingCount==engine.workingCount,"partial repair healed unavailable engine");
        check(app.playerWealth_==funds-fullQuote+app.totalRepairCost(commander) && app.totalRepairCost(commander)>0,"partial refund mismatch");
        const auto after=mechHashes();
        for(size_t n=0;n<after.size();++n)if(n!=app.selectedMechIndex_)check(after[n]==others[n],"repair changed another actor/reserve");
        check(readFile(checkpoint)==diskGuard && readFile(checkpoint.wstring()+L".mwstate")==extraGuard,"transition/repair autosaved");
        pairCheck(variant*2+1);
        const auto stale=dir/("STALE"+std::to_string(variant)+".GAM");
        writeBytes(stale,readFile(dir/name('I',variant*2+1)));
        writeBytes(stale.wstring()+L".mwstate",readFile((dir/name('I',variant*2)).wstring()+L".mwstate"));
        const auto repairGuard=app.captureCampaignPersistentState().fingerprint;
        app.loadGameFromFile(stale);
        check(app.status_.find(L"Load failed:")==0 && app.captureCampaignPersistentState().fingerprint==repairGuard,
            "pre-repair companion partially restored the post-repair campaign");
    }
    perWeaponReplay();
    std::ofstream(root/"latest-cycle-dir.txt")<<dir.generic_string()<<'\n';
    std::cout<<"mw_phase16_validation_smoke: ok checks="<<checks<<" cycles=2 participants=4 reserve=1 pair_saves=4 rollback=yes duplicate=yes repairs=yes weapon_types=10\n";
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string(argv[1])=="--dos-return")dosReturn=argv[2];
        else if(argc!=1)throw std::runtime_error("usage: mw_phase16_validation_smoke [--dos-return directory]");
        runPhase16ValidationSmoke();return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
