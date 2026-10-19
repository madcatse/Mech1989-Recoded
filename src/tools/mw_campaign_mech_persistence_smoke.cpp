// Exercise the real campaign Save/Restore, repair and result handlers without
// creating a window. The friend declaration exposes no production UI shortcut.
#include "../main.cpp"
#include <iostream>
#include <set>

namespace {
using namespace mw;
using namespace mw::battle;
size_t checks = 0;
std::optional<std::pair<fs::path,fs::path>> gamRoundTrip;
void check(bool ok, const std::string& message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const std::string& message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
BattleStartParams fixture(const std::string& preset) {
    BattleStartParams p;
    p.constrainToTerrainBounds = false;
    p.deterministicCombatRuntimeEnabled = true;
    p.individualWeaponRuntimeEnabled = true;
    p.mechSystemsSnapshotEnabled = true;
    p.playerMechPresetId = preset;
    BattleCombatantLaunchState launch;
    launch.mechPresetId = preset;
    launch.roster.team = BattleTeam::Player;
    launch.roster.sourceSlot = "player:0";
    launch.startTransform = {10000,0,10000,0};
    launch.persistentMechState.valid = true;
    launch.persistentMechState.provenance = "persistence_smoke";
    launch.persistentMechState.armorPercent = 100;
    launch.persistentMechState.heatSinksTotal = launch.persistentMechState.heatSinksWorking = 10;
    launch.ammunitionStateValid = true;
    const auto ammo = mech3d::catalogAmmunitionProfile(preset);
    for (size_t i=0;i<6;++i) launch.ammunitionByPool[i] = ammo.maximumByPool[i];
    p.playerLaunchState = launch;
    p.playerRoster = launch.roster;
    return p;
}
uint64_t exactHash(const CombatantSnapshot& c) {
    return persistentMechDamageFingerprint(*campaignMechStateFromCombatant(c).exactDamage);
}
CombatantSnapshot redeploy(const CombatantSnapshot& c, const BattlePersistentMechState& state) {
    auto p = fixture(c.mechPresetId);
    p.playerLaunchState->persistentMechState = state;
    return BattleWorld::create(p).snapshot().combatants.at(0);
}
void catalogRoundTrips() {
    for (const auto& entry : mech3d::mechCatalogEntries()) {
        const auto pristine = BattleWorld::create(fixture(entry.presetId)).snapshot().combatants.at(0);
        check(validatePersistentMechDamage(campaignMechStateFromCombatant(pristine),entry.presetId), "invalid pristine catalog state");
        for (size_t part=0;part<9;++part) {
            auto c = pristine;
            auto& a = c.detailedDamage.armorSections[part];
            a.armorRemaining--; a.battleDamage++;
            auto state = campaignMechStateFromCombatant(c);
            const auto hash = persistentMechDamageFingerprint(*state.exactDamage);
            check(!battlePersistentMechStateIsPristine(state), "single point damage hidden");
            for (int repeat=0;repeat<5;++repeat) {
                const auto next = redeploy(c,state);
                check(exactHash(next)==hash, "armor rounded during repeated deployments: "+entry.presetId);
                state = campaignMechStateFromCombatant(next);
            }
            repairCampaignMechDamage(state,entry.presetId,CampaignMechRepairKind::ArmorSection,part);
            check(state.exactDamage->detailed.armorSections[part].armorRemaining==a.armorMaximum, "armor repair did not restore points");
        }
        for (size_t part=0;part<8;++part) {
            auto c = pristine;
            c.detailedDamage.internalSections[part].structureRemaining--;
            c.detailedDamage.internalSections[part].battleDamage++;
            check(exactHash(redeploy(c,campaignMechStateFromCombatant(c)))==exactHash(c), "internal points rounded");
        }
        auto c=pristine;
        for (size_t i=0;i<10;++i) {
            auto& crit=c.detailedDamage.criticalComponents[i];
            crit.battleDamage=1; crit.condition=1;
            if (crit.workingCount) --crit.workingCount;
            crit.functional=crit.totalCount==0 || crit.workingCount!=0;
            c.detailedDamage.installedWeapons[i].condition=2;
            c.detailedDamage.installedWeapons[i].functional=false;
            c.detailedDamage.installedWeapons[i].battleDamage=2;
        }
        c.detailedDamage.originalJumpJetCriticalDestroyedMask=5;
        c.detailedDamage.criticalAttempts=8; c.detailedDamage.criticalHitsApplied=5;
        c.detailedDamage.ammunitionCriticalHits[5]=2;
        c.detailedDamage.unresolvedCriticalSelections=1;
        c.detailedDamage.unresolvedCriticalDamage=1;
        c.detailedDamage.criticalResolutionPending=true;
        auto state=campaignMechStateFromCombatant(c);
        check(exactHash(redeploy(c,state))==exactHash(c), "critical/weapon/diagnostic state lost");
        for (size_t i=0;i<10;++i) {
            auto repaired=state;
            repairCampaignMechDamage(repaired,entry.presetId,CampaignMechRepairKind::CriticalComponent,i);
            check(repaired.exactDamage->detailed.criticalComponents[i].functional, "critical repair failed");
            check(repaired.exactDamage->detailed.installedWeapons[0].battleDamage==2, "critical repair healed unrelated weapon");
            repaired=state;
            repairCampaignMechDamage(repaired,entry.presetId,CampaignMechRepairKind::Weapon,i);
            check(repaired.exactDamage->detailed.installedWeapons[i].functional, "weapon repair failed");
            check(repaired.exactDamage->detailed.criticalComponents[0].battleDamage==1, "weapon repair healed engine");
        }
        repairCampaignMechDamage(state,entry.presetId,CampaignMechRepairKind::All);
        check(battlePersistentMechStateIsPristine(state), "full repair left hidden damage");
        auto invalid=campaignMechStateFromCombatant(pristine);
        ++invalid.exactDamage->detailed.armorSections[0].armorRemaining;
        check(!validatePersistentMechDamage(invalid,entry.presetId), "invalid remaining points accepted");
        rejects([&]{redeploy(c,invalid);}, "invalid entry should fail closed");
    }
    auto p=fixture("locust");
    auto world=BattleWorld::create(p);
    world.destroyComponent(world.playerEntityId(),5);
    auto arm=world.snapshot().combatants.at(0);
    check(exactHash(redeploy(arm,campaignMechStateFromCombatant(arm)))==exactHash(arm), "destroyed/hidden/disabled limb lost");
    world.destroyComponent(world.playerEntityId(),4);
    auto wreck=world.snapshot().combatants.at(0);
    check(redeploy(wreck,campaignMechStateFromCombatant(wreck)).mechDestroyed, "wreck revived on campaign entry");
    auto cockpit=BattleWorld::create(p).snapshot().combatants.at(0);
    cockpit.detailedDamage.originalCockpitCriticalFlag=true;
    cockpit.mechDestroyed=true; cockpit.missionStatus=CombatantMissionStatus::Destroyed;
    auto state=campaignMechStateFromCombatant(cockpit);
    check(state.armorDamage[4]!=0, "cockpit damage has no lab repair target");
    repairCampaignMechDamage(state,"locust",CampaignMechRepairKind::ArmorSection,4);
    check(!redeploy(cockpit,state).mechDestroyed, "repaired cockpit remained destroyed");
    cockpit.mechDestroyed=false; cockpit.missionStatus=CombatantMissionStatus::Escaped;
    cockpit.detailedDamage.originalCockpitCriticalFlag=false;
    check(redeploy(cockpit,campaignMechStateFromCombatant(cockpit)).missionStatus==CombatantMissionStatus::Active, "escape latched across missions");
    cockpit.detailedDamage.criticalComponents[0].functional=false;
    cockpit.detailedDamage.criticalComponents[0].condition=3;
    cockpit.detailedDamage.criticalComponents[0].workingCount=0;
    cockpit.detailedDamage.criticalComponents[0].battleDamage=3;
    cockpit.detailedDamage.armorSections[0].armorRemaining--;
    cockpit.detailedDamage.armorSections[0].battleDamage++;
    cockpit.missionStatus=CombatantMissionStatus::Disabled;
    check(redeploy(cockpit,campaignMechStateFromCombatant(cockpit)).missionStatus==CombatantMissionStatus::Disabled,
        "disabled state lost at ordinary campaign entry");
    auto extended=prepareNextMissionLaunchState(cockpit,{});
    check(extended.minimalRepairApplied && extended.persistentMechState.exactDamage->detailed.criticalComponents[0].functional,
        "extended minimal repair failed");
    check(extended.persistentMechState.exactDamage->detailed.armorSections[0].armorRemaining==cockpit.detailedDamage.armorSections[0].armorRemaining,
        "extended minimal repair healed unrelated armor");
    // Critical condition 2 can be nonfunctional in battle. It must not revive
    // when repairing something unrelated in the campaign lab.
    cockpit.detailedDamage.criticalComponents[3].condition=2;
    cockpit.detailedDamage.criticalComponents[3].functional=false;
    cockpit.mechDestroyed=true;
    state=campaignMechStateFromCombatant(cockpit);
    repairCampaignMechDamage(state,"locust",CampaignMechRepairKind::Weapon,0);
    check(state.exactDamage->mechDestroyed, "life support revived by weapon repair");
}

void saveCodecAndReplay() {
    auto p=fixture("locust");
    auto c=BattleWorld::create(p).snapshot().combatants.at(0);
    c.detailedDamage.armorSections[0].armorRemaining--;
    c.detailedDamage.armorSections[0].battleDamage++;
    auto state=campaignMechStateFromCombatant(c);
    std::vector<uint8_t> gam(0x718,0x5a);
    const auto original=gam;
    encodeOriginalGamMechDamage(gam,0x102,state);
    check(gam.size()==0x718, "original GAM size changed");
    for (size_t i=0;i<gam.size();++i)
        if (i<0x102 || i>=0x102+29) check(gam[i]==original[i], "mech codec overwrote unknown GAM bytes");
    auto legacy=decodeOriginalGamMechDamage(gam,0x102,p.playerLaunchState->persistentMechState);
    check(!legacy.exactDamage && legacy.armorDamage==state.armorDamage, "legacy GAM decode changed");
    const auto extra=encodeCampaignMechSave(gam,{{"locust",state.exactDamage}});
    auto restored=decodeCampaignMechSave(gam,extra);
    check(persistentMechDamageFingerprint(*restored[0].damage)==persistentMechDamageFingerprint(*state.exactDamage), "sidecar lost exact damage");
    for (size_t i=0;i<extra.size();++i) {
        auto corrupt=extra; corrupt[i]^=1;
        rejects([&]{decodeCampaignMechSave(gam,corrupt);}, "corrupt companion accepted");
    }
    auto changedGam=gam; changedGam[4]^=1;
    rejects([&]{decodeCampaignMechSave(changedGam,extra);}, "stale companion accepted");
    rejects([&]{decodeCampaignMechSave(gam,{});}, "empty companion accepted");
    p.playerLaunchState->persistentMechState=state;
    BattleReplay replay;
    BattleInputCommand move; move.tickIndex=0; move.throttle=.5;
    replay.commands.push_back(move);
    auto parsed=parseBattleReplay(serializeBattleReplay(replay));
    const auto first=runBattleReplay(p,parsed,10);
    p.playerLaunchState->persistentMechState.exactDamage=restored[0].damage;
    const auto second=runBattleReplay(p,parsed,10);
    check(battleSnapshotFingerprint(first)==battleSnapshotFingerprint(second), "saved-state replay differs");
    auto world=BattleWorld::create(p);
    move.entityId=world.playerEntityId(); world.enqueueInput(move);
    for (int tick=0;tick<10;++tick) {
        const auto before=world.snapshot(); world.tick();
        for (double alpha : {0.,.25,.5,1.})
            static_cast<void>(campaignBattleInterpolatedRenderSceneFromSnapshots(before,world.snapshot(),alpha,
                CampaignBattlePresentationMode::Cockpit,"Sorted Original Files"));
    }
    check(battleSnapshotFingerprint(second)==battleSnapshotFingerprint(world.snapshot()), "render cadence changed exact-state replay");
    auto mutated=second; mutated.combatants[0].ammunitionByPool[5]--;
    check(battleSnapshotFingerprint(mutated)!=battleSnapshotFingerprint(second), "shared ammo absent from snapshot fingerprint");
    auto changed=state; changed.exactDamage->detailed.armorSections[0].armorRemaining--;
    check(battlePersistentMechStateFingerprint(changed)!=battlePersistentMechStateFingerprint(state), "exact state absent from entry fingerprint");
}

void weaponDamageRoundTrip() {
    auto p=fixture("locust");
    p.stationaryTargetHitDiagnosticEnabled=true;
    p.weaponTargetPolicy=BattleWeaponTargetPolicy::SelectedScannerTargetProvisional;
    auto target=fixture("shadow_hawk").playerLaunchState.value();
    target.roster.team=BattleTeam::Opposing; target.roster.sourceSlot="target";
    target.startTransform.z+=100;
    p.combatantLaunchStates={target};
    auto world=BattleWorld::create(p);
    const auto before=world.snapshot();
    BattleInputCommand fire;
    fire.entityId=world.playerEntityId(); fire.cycleTargetScan=true; fire.fireWeapon=true;
    fire.selectWeaponInstanceId=2; // actual machine-gun slot, shared MG pool
    world.enqueueInput(fire); world.tick();
    const auto after=world.snapshot();
    check(after.lastShot.hit && after.lastShot.armorDamageApplied>0, "real weapon pipeline did not damage test target");
    check(after.combatants[0].ammunitionByPool[5]==before.combatants[0].ammunitionByPool[5]-1, "real weapon did not consume shared ammo");
    check(exactHash(redeploy(after.combatants[1],campaignMechStateFromCombatant(after.combatants[1])))==exactHash(after.combatants[1]),
        "real weapon damage did not survive deployment");
}
}

void runCampaignMechPersistenceSmoke() {
    if (gamRoundTrip) {
        App app(findResourceRoot());app.loadResources();
        const auto input=readFile(gamRoundTrip->first);
        app.loadGameFromFile(gamRoundTrip->first);
        check(app.gamRawStateValid_ && app.gamRawState_==input,"round-trip actual Restore failed");
        check(app.saveCurrentGame(gamRoundTrip->second),"round-trip actual Save failed");
        return;
    }
    using namespace mw::battle;
    App app(findResourceRoot());
    app.loadResources();
    app.ownedMechs_.clear();
    const App::ChassisId chassis[] = {App::ChassisId::Locust,App::ChassisId::Jenner,
        App::ChassisId::PhoenixHawk,App::ChassisId::Battlemaster,App::ChassisId::Warhammer};
    for (auto id : chassis) {
        auto mech=App::makeMech(id);
        mech.ammoPacks=App::ammunitionCapacityForMech(mech);
        app.finalizeLoadedMech(mech);
        app.ownedMechs_.push_back(mech);
    }
    app.playerWealth_=10000000;
    const auto unassigned=App::battlePersistentMechStateFromOwnedMech(app.ownedMechs_[4],"test");
    const std::vector<std::string> presets={"locust","jenner","phoenix_hawk","battlemaster","warhammer"};
    auto p=fixture("locust");
    p.playerLaunchState->persistentMechState=App::battlePersistentMechStateFromOwnedMech(app.ownedMechs_[0],"entry");
    std::vector<CampaignBattlePersistentRosterBinding> bindings={{"player:0",0,0}};
    for (size_t i=1;i<4;++i) {
        auto ally=fixture(presets[i]).playerLaunchState.value();
        ally.persistentMechState=App::battlePersistentMechStateFromOwnedMech(app.ownedMechs_[i],"entry");
        ally.roster.sourceSlot="player:"+std::to_string(i);
        ally.startTransform.x+=1500*i;
        p.playerAlliedLaunchStates.push_back(ally);
        bindings.push_back({ally.roster.sourceSlot,static_cast<int>(i),static_cast<int>(i)});
    }
    auto enemy=fixture("rifleman").playerLaunchState.value();
    enemy.roster.team=BattleTeam::Opposing; enemy.roster.sourceSlot="enemy:0";
    p.combatantLaunchStates.push_back(enemy);
    auto final=BattleWorld::create(p).snapshot();
    for (size_t i=0;i<4;++i) {
        auto& c=final.combatants[i];
        c.detailedDamage.armorSections[i].armorRemaining-=static_cast<uint16_t>(i+2);
        c.detailedDamage.armorSections[i].battleDamage=static_cast<int>(i+2);
        for (int& count : c.ammunitionByPool) if (count) --count;
    }
    // Identity comes from sourceSlot, never current combatant vector order.
    std::reverse(final.combatants.begin(),final.combatants.end());
    app.acceptedBattlePersistenceReport_=campaignBattlePersistenceReportFromSnapshot(final,bindings,4);
    app.acceptedBattleMechReturnPlan_=campaignMechReturnPlan(app.acceptedBattlePersistenceReport_,presets);
    check(app.acceptedBattleMechReturnPlan_.valid && app.acceptedBattleMechReturnPlan_.entries.size()==4, "lance return incomplete");
    auto bad=app.acceptedBattlePersistenceReport_;
    bad.combatants[1].ownedMechIndex=bad.combatants[0].ownedMechIndex;
    check(!campaignMechReturnPlan(bad,presets).valid, "duplicate owned identity accepted");
    bad=app.acceptedBattlePersistenceReport_; bad.allMissionParticipantsLaunched=false;
    check(!campaignMechReturnPlan(bad,presets).valid, "partial lance return accepted");
    auto wrong=presets; wrong[0]="jenner";
    check(!campaignMechReturnPlan(app.acceptedBattlePersistenceReport_,wrong).valid, "wrong chassis identity accepted");
    app.acceptedBattleStateGuard_.before=app.captureCampaignPersistentState();
    app.acceptedBattleStateGuard_.after=app.acceptedBattleStateGuard_.before;
    app.acceptedBattleStateGuard_.valid=app.acceptedBattleStateGuard_.unchanged=true;
    ++app.playerWealth_;
    const auto changed=app.captureCampaignPersistentState();
    const auto stale=app.commitAcceptedBattleConsequencePlan();
    check(!stale.committed && changed.fingerprint==app.captureCampaignPersistentState().fingerprint,
        "stale campaign precondition mutated mechs");
    --app.playerWealth_;
    // Verification failure must roll back every participant together.
    auto ready=app.acceptedBattleMechReturnPlan_;
    app.acceptedBattleMechReturnPlan_.entries[0].ammunitionByPool[0]=255; // no AC/5 on this Locust
    const auto beforeRollback=app.captureCampaignPersistentState();
    const auto rolledBack=app.commitAcceptedBattleConsequencePlan();
    check(!rolledBack.committed && beforeRollback.fingerprint==app.captureCampaignPersistentState().fingerprint,
        "failed roundtrip left a partial campaign mutation");
    app.acceptedBattleMechReturnPlan_=ready;
    // A final-battle/defeat result may have no money award. Damage still commits.
    auto receipt=app.commitAcceptedBattleConsequencePlan();
    check(receipt.committed && receipt.verified && receipt.saveRoundTripVerified,
        "real campaign commit failed: "+receipt.reason);
    check(battlePersistentMechStateFingerprint(unassigned)==battlePersistentMechStateFingerprint(
        App::battlePersistentMechStateFromOwnedMech(app.ownedMechs_[4],"test")), "unassigned mech changed");
    for (const auto& returned : app.acceptedBattleMechReturnPlan_.entries) {
        const auto& mech=app.ownedMechs_[returned.ownedMechIndex];
        check(persistentMechDamageFingerprint(*mech.exactDamage)==persistentMechDamageFingerprint(*returned.state.exactDamage), "wrong owned mech got damage");
        check(mech.ammoPacks==returned.ammunitionByPool, "campaign ammo was reloaded");
        check(mech.repairCost>0 && mech.armorPercent<100, "lab cannot see returned damage");
    }
    const auto committed=app.captureCampaignPersistentState();
    const auto duplicate=app.commitAcceptedBattleConsequencePlan();
    check(duplicate.duplicateRejected && !duplicate.committed && committed.fingerprint==app.captureCampaignPersistentState().fingerprint, "duplicate result changed campaign");
    // The real entry adapter uses the retained exact state.
    for (size_t i=0;i<4;++i) {
        auto state=App::battlePersistentMechStateFromOwnedMech(app.ownedMechs_[i],"entry");
        auto reentry=fixture(presets[i]); reentry.playerLaunchState->persistentMechState=state;
        check(exactHash(BattleWorld::create(reentry).snapshot().combatants[0])==persistentMechDamageFingerprint(*state.exactDamage), "campaign reentry rounded damage");
    }
    // Save only inside a new isolated smoke directory. No user save is touched.
    const auto dir=fs::path("outputs/campaign_damage_audit")/("smoke_"+std::to_string(GetCurrentProcessId())+"_"+std::to_string(GetTickCount64()));
    check(fs::create_directories(dir), "smoke save directory already exists");
    const auto save=dir/"ROUNDTRIP.GAM";
    check(app.saveCurrentGame(save), "real save handler failed");
    const auto gam=readFile(save), extra=readFile(save.wstring()+L".mwstate");
    check(gam.size()==0x718, "live save changed original file size");
    App loaded(findResourceRoot()); loaded.loadResources(); loaded.loadGameFromFile(save);
    check(loaded.ownedMechs_.size()==5 && loaded.ownedMechs_[0].exactDamage.has_value(), "real restore missed sidecar");
    for (size_t i=0;i<4;++i)
        check(persistentMechDamageFingerprint(*loaded.ownedMechs_[i].exactDamage)==persistentMechDamageFingerprint(*app.ownedMechs_[i].exactDamage), "real restore lost damage");
    check(loaded.saveCurrentGame(save), "save pair overwrite failed");
    const auto validGam=readFile(save), validExtra=readFile(save.wstring()+L".mwstate");
    rejects([&]{writeCampaignSaveFiles(save,validGam,{});}, "invalid extra overwrote save");
    check(readFile(save)==validGam && readFile(save.wstring()+L".mwstate")==validExtra, "failed save mutated files");
    const auto directoryTarget=dir/"DIRECTORY.GAM";
    fs::create_directory(directoryTarget);
    rejects([&]{writeCampaignSaveFiles(directoryTarget,validGam,validExtra);}, "directory treated as save file");
    check(fs::is_directory(directoryTarget), "directory destination modified");
    // The same original payload also loads without our additional data.
    const auto legacyPath=dir/"LEGACY.GAM";
    { std::ofstream out(legacyPath,std::ios::binary); out.write(reinterpret_cast<const char*>(gam.data()),gam.size()); }
    loaded.loadGameFromFile(legacyPath);
    check(loaded.ownedMechs_.size()==5 && !loaded.ownedMechs_[0].exactDamage, "original-only save not supported");
    loaded.loadGameFromFile(save);
    const auto loadGuard=loaded.captureCampaignPersistentState();
    const fs::path pendingExtra=save.wstring()+L".mwstate.mw-new";
    { std::ofstream pending(pendingExtra); pending << "interrupted save"; }
    loaded.loadGameFromFile(save);
    check(loaded.captureCampaignPersistentState().fingerprint==loadGuard.fingerprint && loaded.status_.find(L"Load failed")!=std::wstring::npos,
        "interrupted first-save transaction silently loaded as legacy");
    fs::remove(pendingExtra);
    auto corrupt=validExtra; corrupt.back()^=1;
    { std::ofstream out(save.wstring()+L".mwstate",std::ios::binary|std::ios::trunc); out.write(reinterpret_cast<const char*>(corrupt.data()),corrupt.size()); }
    loaded.loadGameFromFile(save);
    check(loaded.captureCampaignPersistentState().fingerprint==loadGuard.fingerprint && loaded.status_.find(L"Load failed")!=std::wstring::npos,
        "bad sidecar partially loaded a campaign");
    // Repair uses the same 2D handlers as the real lab and changes exact points.
    app.repairSession_.rng={0,0}; // Source-valid successful availability draws.
    app.repairSession_.unavailable.fill(0);
    app.selectedMechIndex_=0; app.repairSelectionIndex_=0;
    app.repairSelectedMechTarget();
    check(app.ownedMechs_[0].armorPercent==100 && app.ownedMechs_[0].repairCost==0, "2D individual repair did not repair exact armor");
    app.selectedMechIndex_=1; app.repairAllSelectedMech();
    check(!app.ownedMechs_[1].exactDamage && app.ownedMechs_[1].armorPercent==100, "2D full repair left exact damage");
    auto broken=BattleWorld::create(fixture("jenner")).snapshot().combatants[0];
    auto& engine=broken.detailedDamage.criticalComponents[0];
    engine.condition=3; engine.functional=false; engine.workingCount=0; engine.battleDamage=3;
    broken.missionStatus=CombatantMissionStatus::Disabled;
    App::applyBattlePersistentMechStateToOwnedMech(app.ownedMechs_[1],campaignMechStateFromCombatant(broken));
    app.finalizeLoadedMech(app.ownedMechs_[1]);
    check(app.ownedMechs_[1].condition==App::DamageState::Junk, "disabled mech was functional in 2D");
    app.repairSelectionIndex_=0; app.repairSelectedMechTarget();
    check(app.ownedMechs_[1].condition==App::DamageState::Functional &&
        app.ownedMechs_[1].exactDamage->detailed.criticalComponents[0].functional, "single engine repair left mech nonfunctional");
    // A separately prepared result goes through the actual continue handler.
    app.lastCommittedBattleConsequencePlanFingerprint_=0;
    app.acceptedBattleStateGuard_.after=app.captureCampaignPersistentState();
    app.acceptedBattleStateGuard_.before=app.acceptedBattleStateGuard_.after;
    app.acceptedBattleOutcome_.valid=true;
    app.acceptedBattleOutcome_.terminal=true;
    app.acceptedBattleOutcome_.outcome=CampaignBattleOutcome::Defeat;
    app.acceptedBattleOutcome_.launchSource=CampaignBattleLaunchSource::AcceptedContract;
    app.acknowledgeAcceptedBattleOutcome();
    check(app.state_==ScreenState::MainMenu && !app.acceptedBattleMechReturnPlan_.valid &&
        app.ownedMechs_[0].exactDamage && app.ownedMechs_[0].armorPercent<100,
        "continue handler failed to return battle damage to campaign");
    // Source rounding can produce GAM armor grade zero while the exact layer
    // still holds a missing point. Exercise the actual lab and disk handlers.
    {
        App tiny(findResourceRoot()); tiny.loadResources();
        tiny.ownedMechs_={App::makeMech(App::ChassisId::Locust)}; tiny.selectedMechIndex_=0;
        auto c=BattleWorld::create(fixture("locust")).snapshot().combatants.at(0);
        --c.detailedDamage.armorSections[0].armorRemaining;
        c.detailedDamage.armorSections[0].battleDamage=1;
        auto state=campaignMechStateFromCombatant(c);
        App::applyBattlePersistentMechStateToOwnedMech(tiny.ownedMechs_[0],state);
        tiny.finalizeLoadedMech(tiny.ownedMechs_[0]);
        const auto damageHash=persistentMechDamageFingerprint(*tiny.ownedMechs_[0].exactDamage);
        check(tiny.ownedMechs_[0].armorDamage[0]==0 && tiny.ownedMechs_[0].armorPercent<100 &&
            tiny.totalRepairCost(tiny.ownedMechs_[0])==0,"one-point original grade/quote must retain exact loss");
        const auto guard=tiny.captureCampaignPersistentState().fingerprint;
        tiny.repairAllSelectedMech();
        check(tiny.captureCampaignPersistentState().fingerprint==guard &&
            persistentMechDamageFingerprint(*tiny.ownedMechs_[0].exactDamage)==damageHash,
            "zero-quote repair healed exact damage for free");
        const auto tinySave=dir/"ONEPOINT.GAM";
        check(tiny.saveCurrentGame(tinySave),"one-point actual Save failed");
        tiny.loadGameFromFile(tinySave);
        check(tiny.ownedMechs_[0].exactDamage && tiny.ownedMechs_[0].armorDamage[0]==0 &&
            persistentMechDamageFingerprint(*tiny.ownedMechs_[0].exactDamage)==damageHash,
            "actual Restore lost zero-grade exact damage");
        auto entry=fixture("locust");
        entry.playerLaunchState->persistentMechState=App::battlePersistentMechStateFromOwnedMech(tiny.ownedMechs_[0],"one_point_restored");
        check(exactHash(BattleWorld::create(entry).snapshot().combatants.at(0))==damageHash,
            "actual restored campaign entry healed one point");
    }
    // Live original preflight snapshots, entered through the real GAM/App adapter.
    // Expected points are read from original BTECH DS:4FB6, not our formula.
    {
        App side(findResourceRoot()); side.loadResources();
        constexpr std::array<size_t,9> external{{1,0,3,2,5,6,8,7,4}};
        constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
        for (size_t raw : {size_t(7),size_t(8)}) {
            const auto root=fs::path("research/fixtures/dosbox_side_torso");
            const auto prefix="byte"+std::to_string(raw);
            const auto input=readFile(root/("BYTE"+std::to_string(raw)+".GAM"));
            const auto dos=readFile(root/(prefix+"-battle.ds.bin"));
            side.loadGameFromFile(root/("BYTE"+std::to_string(raw)+".GAM"));
            check(side.ownedMechs_.size()==1 && !side.ownedMechs_[0].exactDamage,"DOS original-only load failed");
            check(side.ownedMechs_[0].armorDamage[raw]==1,"GAM ordinal changed in 2D lab");
            side.selectedMechIndex_=0; side.state_=ScreenState::MechStatus; side.render();
            const auto capture=fs::path("outputs/side_torso_audit")/(prefix+"-cpp-lab.ppm");
            fs::create_directories(capture.parent_path());
            std::ofstream image(capture,std::ios::binary);image<<"P6\n320 200\n255\n";
            for (auto pixel:side.framebuffer_) {const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)};image.write(rgb,3);}
            check(bool(image),"side-torso lab capture failed");
            auto entry=fixture("jenner");
            entry.playerLaunchState->persistentMechState=App::battlePersistentMechStateFromOwnedMech(side.ownedMechs_[0],"live_DOS");
            auto actor=BattleWorld::create(entry).snapshot().combatants.at(0);
            const auto word=[&](size_t offset) {return unsigned(dos.at(offset))+(unsigned(dos.at(offset+1))<<8);};
            for (size_t i=0;i<9;++i)
                check(actor.detailedDamage.armorSections[external[i]].armorRemaining==word(0x4fb6+0x19+i*2),"App entry armor differs from live DOS");
            for (size_t i=0;i<8;++i)
                check(actor.detailedDamage.internalSections[internal[i]].structureRemaining==word(0x4fb6+0x2b+i*2),"App entry internal differs from live DOS");
            auto returned=campaignMechStateFromCombatant(actor);
            App::applyBattlePersistentMechStateToOwnedMech(side.ownedMechs_[0],returned);
            side.finalizeLoadedMech(side.ownedMechs_[0]);
            auto output=input;side.writeRuntimeToGamSave(output);
            check(std::equal(input.begin()+0x116,input.begin()+0x11f,output.begin()+0x116),"App no-hit return changed raw DOS armor");
            const auto hash=persistentMechDamageFingerprint(*returned.exactDamage);
            const auto sideSave=dir/(prefix+".GAM");check(side.saveCurrentGame(sideSave),"side Save failed");
            const auto saved=readFile(sideSave);side.loadGameFromFile(sideSave);
            check(readFile(sideSave)==saved && persistentMechDamageFingerprint(*side.ownedMechs_[0].exactDamage)==hash,"side Restore changed disk or exact points");
            entry.playerLaunchState->persistentMechState=App::battlePersistentMechStateFromOwnedMech(side.ownedMechs_[0],"side_redeploy");
            check(exactHash(BattleWorld::create(entry).snapshot().combatants.at(0))==hash,"side redeployment changed exact points");
            side.selectedMechIndex_=0;side.repairSelectionIndex_=0;side.playerWealth_=10000000;
            const auto quote=side.totalRepairCost(side.ownedMechs_[0]);side.repairSelectedMechTarget();
            check(side.playerWealth_==10000000-quote && side.totalRepairCost(side.ownedMechs_[0])==0 &&
                side.ownedMechs_[0].armorPercent==100,"paid side repair failed");
        }
        // Both sides damaged: original lab clears raw 7 first, i.e. named LEFT.
        auto entry=fixture("jenner");entry.playerLaunchState->persistentMechState.armorDamage[7]=2;
        entry.playerLaunchState->persistentMechState.armorDamage[8]=1;
        auto returned=campaignMechStateFromCombatant(BattleWorld::create(entry).snapshot().combatants.at(0));
        side.ownedMechs_={App::makeMech(App::ChassisId::Jenner)};
        App::applyBattlePersistentMechStateToOwnedMech(side.ownedMechs_[0],returned);side.finalizeLoadedMech(side.ownedMechs_[0]);
        const auto right=returned.exactDamage->detailed.armorSections[7].armorRemaining;
        side.selectedMechIndex_=0;side.repairSelectionIndex_=0;side.repairSelectedMechTarget();
        const auto& repaired=side.ownedMechs_[0];
        check(repaired.armorDamage[7]==0 && repaired.armorDamage[8]==2 &&
            repaired.exactDamage->detailed.armorSections[8].armorRemaining==8 &&
            repaired.exactDamage->detailed.armorSections[7].armorRemaining==right,"first raw side repair healed wrong named side");
        // Force partial full repair: unavailable engine must remain, both paid
        // side armor pools must be restored without discarding exact state.
        returned.exactDamage->detailed.criticalComponents[0].condition=1;returned.engine=1;
        App::applyBattlePersistentMechStateToOwnedMech(side.ownedMechs_[0],returned);side.finalizeLoadedMech(side.ownedMechs_[0]);
        side.repairSession_.unavailable.fill(0);side.repairSession_.unavailable[App::gamChassisId(App::ChassisId::Jenner)*22]=1;
        side.repairAllSelectedMech();
        check(side.ownedMechs_[0].exactDamage && side.ownedMechs_[0].engine==App::DamageState::LightDamage &&
            side.ownedMechs_[0].exactDamage->detailed.armorSections[7].armorRemaining==8 &&
            side.ownedMechs_[0].exactDamage->detailed.armorSections[8].armorRemaining==8,"partial full repaired wrong side or unpaid engine");
        // Existing v1 files have no coarse-order marker. Exact named points are
        // authoritative; Restore reconciles side grades, never swaps points.
        for (const auto& catalog:mech3d::mechCatalogEntries()) {
            auto oldEntry=fixture(catalog.presetId);oldEntry.playerLaunchState->persistentMechState.armorDamage[7]=1;
            oldEntry.playerLaunchState->persistentMechState.armorDamage[8]=2;
            const auto old=campaignMechStateFromCombatant(BattleWorld::create(oldEntry).snapshot().combatants.at(0));
            side.ownedMechs_={App::makeMech(*App::battleChassisForMechPreset(catalog.presetId))};
            App::applyBattlePersistentMechStateToOwnedMech(side.ownedMechs_[0],old);
            auto oldGam=side.baseGamSaveData();side.writeRuntimeToGamSave(oldGam);
            // Reproduce the previous codec's identity mapping, then bind v1.
            std::swap(oldGam[0x11d],oldGam[0x11e]);
            const auto oldExtra=encodeCampaignMechSave(oldGam,{{catalog.presetId,old.exactDamage}});
            const auto oldPath=dir/(catalog.presetId+"-old-v1.GAM");writeCampaignSaveFiles(oldPath,oldGam,oldExtra);
            const auto hash=persistentMechDamageFingerprint(*old.exactDamage);
            side.loadGameFromFile(oldPath);
            check(readFile(oldPath)==oldGam && readFile(oldPath.wstring()+L".mwstate")==oldExtra,"Restore migration wrote files");
            check(side.ownedMechs_[0].armorDamage[7]==2 && side.ownedMechs_[0].armorDamage[8]==1 &&
                persistentMechDamageFingerprint(*side.ownedMechs_[0].exactDamage)==hash,"old v1 migration swapped points or wrong grades");
            const auto fixedPath=dir/(catalog.presetId+"-fixed-v1.GAM");check(side.saveCurrentGame(fixedPath),"migrated Save failed");
            for (int repeat=0;repeat<3;++repeat) {
                side.loadGameFromFile(fixedPath);
                check(side.ownedMechs_[0].armorDamage[7]==2 && side.ownedMechs_[0].armorDamage[8]==1 &&
                    persistentMechDamageFingerprint(*side.ownedMechs_[0].exactDamage)==hash,"migration repeated a side swap");
            }
        }
        // A genuine old build's companion, separately pinned before this work.
        const auto oldPath=fs::path("research/fixtures/btech_armor/PARTIAL.GAM");
        const auto oldBytes=readFile(oldPath),oldExtra=readFile(oldPath.wstring()+L".mwstate");
        const auto old=decodeCampaignMechSave(oldBytes,oldExtra);side.loadGameFromFile(oldPath);
        check(side.ownedMechs_.size()==old.size(),"actual old companion roster failed");
        for (size_t i=0;i<old.size();++i)
            check(persistentMechDamageFingerprint(*side.ownedMechs_[i].exactDamage)==persistentMechDamageFingerprint(*old[i].damage),"actual old companion points changed");
    }
    // Real lab/Save/Restore around the proven component conversion tables.
    {
        App systems(findResourceRoot());systems.loadResources();
        auto componentGam=readFile("research/fixtures/dosbox_side_torso/BYTE7.GAM");
        std::fill(componentGam.begin()+0x102,componentGam.begin()+0x11f,uint8_t(0));
        const std::array<uint8_t,20> raw{{0,2,1,2,0,2,1,2,1,0,3,2,1,0,0,1,2,3,1,2}};
        std::copy(raw.begin(),raw.end(),componentGam.begin()+0x102);systems.applyGamSaveToRuntime(componentGam);
        const auto quote=systems.totalRepairCost(systems.ownedMechs_[0]);
        const auto ammo=systems.ownedMechs_[0].ammoPacks;
        auto entry=fixture("jenner");entry.playerLaunchState->persistentMechState=
            App::battlePersistentMechStateFromOwnedMech(systems.ownedMechs_[0],"components_entry");
        auto state=campaignMechStateFromCombatant(BattleWorld::create(entry).snapshot().combatants.at(0));
        App::applyBattlePersistentMechStateToOwnedMech(systems.ownedMechs_[0],state);
        systems.finalizeLoadedMech(systems.ownedMechs_[0]);
        auto output=componentGam;systems.writeRuntimeToGamSave(output);
        check(std::equal(raw.begin(),raw.end(),output.begin()+0x102) &&
            systems.totalRepairCost(systems.ownedMechs_[0])==quote,"no-hit component return changed GAM or repair quote");
        systems.selectedMechIndex_=0;systems.playerWealth_=10000000;
        systems.repairSession_.rng={0,0};systems.repairSession_.unavailable.fill(0);
        const auto repairWeapon=[&] {
            const auto targets=systems.repairableTargets(systems.ownedMechs_[0]);
            const auto found=std::find_if(targets.begin(),targets.end(),[](const App::RepairTarget& target) {
                return target.kind==App::RepairTargetKind::Weapon && target.index==0;
            });
            check(found!=targets.end(),"component test weapon target missing");
            systems.repairSelectionIndex_=static_cast<size_t>(found-targets.begin());
            systems.repairSelectedMechTarget();
        };
        repairWeapon();
        check(systems.ownedMechs_[0].weapons[0].condition==App::DamageState::HeavyDamage &&
            !systems.ownedMechs_[0].exactDamage->detailed.installedWeapons[0].functional,
            "JUNK -> HEAVY repair enabled weapon too early");
        const auto file=dir/"COMPONENTS.GAM";check(systems.saveCurrentGame(file),"component actual Save failed");
        const auto exact=persistentMechDamageFingerprint(*systems.ownedMechs_[0].exactDamage);
        systems.loadGameFromFile(file);
        check(persistentMechDamageFingerprint(*systems.ownedMechs_[0].exactDamage)==exact &&
            systems.ownedMechs_[0].weapons[0].condition==App::DamageState::HeavyDamage,
            "component actual Restore changed heavy weapon or exact state");
        systems.selectedMechIndex_=0;systems.repairSession_.rng={0,0};repairWeapon();
        check(systems.ownedMechs_[0].weapons[0].condition==App::DamageState::LightDamage &&
            systems.ownedMechs_[0].exactDamage->detailed.installedWeapons[0].functional,
            "HEAVY -> LIGHT repair did not enable weapon");
        check(systems.ownedMechs_[0].gyros==App::DamageState::HeavyDamage &&
            systems.ownedMechs_[0].exactDamage->detailed.criticalComponents[1].condition==1 &&
            systems.ownedMechs_[0].ammoPacks==ammo,"weapon repair changed gyro grade or ammunition");
        auto partiallyRepaired=componentGam;systems.writeRuntimeToGamSave(partiallyRepaired);
        check(std::equal(raw.begin()+15,raw.end(),partiallyRepaired.begin()+0x111),
            "individual repair or Save/Restore changed inactive original weapon bytes");
        // BTECH exit only visits installed weapons, but MW_MAIN full repair
        // scans all 29 fields, including zero-price inactive weapon slots.
        systems.repairAllSelectedMech();
        auto fullyRepaired=componentGam;systems.writeRuntimeToGamSave(fullyRepaired);
        check(std::all_of(fullyRepaired.begin()+0x102,fullyRepaired.begin()+0x11f,[](uint8_t value){return value==0;}) &&
            systems.totalRepairCost(systems.ownedMechs_[0])==0,
            "full repair retained an original condition byte or an active charge");
    }
    // Live incoming hits, followed by the original RETREAT and armor repair.
    // Import observed points/functionality into a snapshot to test conversion;
    // this is NOT a replay/equivalence claim for hit physics or DOS RNG history.
    {
        const auto root=fs::path("research/fixtures/dosbox_damage");
        const auto supplied=readFile(root/"hit-entry.gam");
        const auto returned=readFile(root/"hit-exit-transfer.gam");
        const auto pristine=readFile(root/"hit-pristine.ds.bin");
        const auto damaged=readFile(root/"hit-before-confirm.ds.bin");
        const auto word=[](const auto& bytes,size_t offset) {
            return unsigned(bytes.at(offset))+(unsigned(bytes.at(offset+1))<<8);
        };
        constexpr size_t dosActor=0x4fb6;
        constexpr std::array<size_t,9> external{{1,0,3,2,5,6,8,7,4}};
        constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
        constexpr std::array<size_t,10> critical{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
        App hits(findResourceRoot());hits.loadResources();hits.applyGamSaveToRuntime(supplied);
        auto launch=fixture("jenner");launch.playerLaunchState->persistentMechState=
            App::battlePersistentMechStateFromOwnedMech(hits.ownedMechs_[0],"live_hit_entry");
        launch.playerLaunchState->ammunitionByPool=hits.ownedMechs_[0].ammoPacks;
        auto actor=BattleWorld::create(launch).snapshot().combatants.at(0);
        for (size_t i=0;i<9;++i) {
            auto& a=actor.detailedDamage.armorSections[external[i]];
            check(a.armorRemaining==word(pristine,dosActor+0x19+2*i),"live hit entry armor mismatch");
            a.armorRemaining=static_cast<uint16_t>(word(damaged,dosActor+0x19+2*i));
            a.battleDamage=a.armorMaximum-a.armorRemaining;
        }
        for (size_t i=0;i<8;++i) {
            auto& a=actor.detailedDamage.internalSections[internal[i]];
            check(a.structureRemaining==word(pristine,dosActor+0x2b+2*i),"live hit entry internal mismatch");
            a.structureRemaining=static_cast<uint16_t>(word(damaged,dosActor+0x2b+2*i));
            a.battleDamage=a.structureMaximum-a.structureRemaining;
        }
        for (size_t i=0;i<10;++i) {
            auto& c=actor.detailedDamage.criticalComponents[i];
            const unsigned before=(i==4 || i==9)?c.totalCount-c.workingCount:
                (i>=5 && i<=8)?c.workingCount:c.condition;
            check(before==word(pristine,dosActor+critical[i]),"live hit entry component mismatch");
            const auto after=word(damaged,dosActor+critical[i]);
            if (i>=5 && i<=8) {
                c.workingCount=static_cast<uint8_t>(after);c.functional=after!=0;
                c.battleDamage=static_cast<int>(before-after);
            } else check(before==after,"live hit fixture gained an unsupported component mutation");
        }
        for (size_t i=0;i<5;++i) {
            auto& w=actor.detailedDamage.installedWeapons[i];
            check(w.functional==(pristine[dosActor+0x3b+3*i]!=0),"live hit entry weapon mismatch");
            w.functional=damaged[dosActor+0x3b+3*i]!=0;w.battleDamage=w.functional?0:1;
        }
        for (size_t i=0;i<6;++i) {
            check(unsigned(actor.ammunitionByPool[i])==word(pristine,dosActor+0x59+2*i),"live hit entry ammo mismatch");
            check(word(pristine,dosActor+0x59+2*i)==word(damaged,dosActor+0x59+2*i),"live hit fixture ammo changed");
        }
        const auto state=campaignMechStateFromCombatant(actor);
        check(validatePersistentMechDamage(state,"jenner"),"invalid imported live hit snapshot");
        App::applyBattlePersistentMechStateToOwnedMech(hits.ownedMechs_[0],state);
        hits.finalizeLoadedMech(hits.ownedMechs_[0]);
        auto output=supplied;hits.writeRuntimeToGamSave(output);
        for (size_t i=0x102;i<0x11f;++i)check(output[i]==returned[i],"live damaged RETREAT GAM byte mismatch");
        check(returned[0x107]==1 && returned[0x108]==1 && returned[0x10d]==3 &&
            returned[0x10f]==3 && returned[0x116]==2 && returned[0x117]==2 && returned[0x1f]==2,
            "live incoming-hit fixture lost its distinguishing damage");
        check(std::equal(output.begin()+0x25e,output.begin()+0x2a6,returned.begin()+0x25e) &&
            std::equal(output.begin()+0x2fa,output.begin()+0x342,returned.begin()+0x2fa),"live damaged return changed ammo planes");
        // Actual no-action Save preserves this original disk payload completely.
        hits.loadGameFromFile(root/"dos-hits.GAM");const auto disk=readFile(root/"dos-hits.GAM");
        const auto diskSave=dir/"LIVEHITS.GAM";check(hits.saveCurrentGame(diskSave),"live hits actual Save failed");
        check(readFile(diskSave)==disk && disk==readFile(root/"cpp-hits.GAM") && disk==readFile(root/"dos-hitback.GAM"),
            "damaged DOS -> C++ -> DOS disk round trip differs");
        // Original GAM quantizes arm structure 5 -> 3 on another deployment.
        auto coarse=fixture("jenner");coarse.playerLaunchState->persistentMechState=
            App::battlePersistentMechStateFromOwnedMech(hits.ownedMechs_[0],"live_hit_legacy");
        const auto quantized=BattleWorld::create(coarse).snapshot().combatants.at(0);
        check(quantized.detailedDamage.internalSections[0].structureRemaining==3 &&
            quantized.detailedDamage.internalSections[1].structureRemaining==3,"original-only coarse reentry changed");
        // Our v1 layer keeps the actual observed 5 points and 3/2 working
        // actuators. Original DOS has no companion; do not claim it does.
        const auto repairBefore=readFile(root/"hit-armor-before.gam");
        const auto repairAfter=readFile(root/"hit-armor-after.gam");
        const auto repairDs=readFile(root/"hit-armor-before.ds.bin");
        hits.applyGamSaveToRuntime(repairBefore);
        App::applyBattlePersistentMechStateToOwnedMech(hits.ownedMechs_[0],state);hits.finalizeLoadedMech(hits.ownedMechs_[0]);
        const auto exact=persistentMechDamageFingerprint(*state.exactDamage);
        check(hits.saveCurrentGame(diskSave),"live exact damage Save failed");hits.loadGameFromFile(diskSave);
        check(persistentMechDamageFingerprint(*hits.ownedMechs_[0].exactDamage)==exact,"live exact damage Restore changed points");
        auto precise=fixture("jenner");precise.playerLaunchState->persistentMechState=
            App::battlePersistentMechStateFromOwnedMech(hits.ownedMechs_[0],"live_hit_exact");
        check(exactHash(BattleWorld::create(precise).snapshot().combatants.at(0))==exact,"live exact damage redeployment rounded points");
        hits.selectedMechIndex_=0;
        hits.repairSession_.rng={static_cast<uint16_t>(word(repairDs,0x3f5)),static_cast<uint16_t>(word(repairDs,0x3f7))};
        std::copy_n(repairDs.begin()+0x3a77,220,hits.repairSession_.unavailable.begin());
        check(hits.totalRepairCost(hits.ownedMechs_[0])==111896,"live combat damage total quote differs");
        const auto targets=hits.repairableTargets(hits.ownedMechs_[0]);
        const auto target=std::find_if(targets.begin(),targets.end(),[](const App::RepairTarget& t){return App::repairSelector(t)==10;});
        check(target!=targets.end(),"live armor repair target absent");
        check(hits.repairTargetCost(hits.ownedMechs_[0],*target)==4000,"live combat armor quote differs");
        hits.repairSelectionIndex_=static_cast<size_t>(target-targets.begin());hits.repairSelectedMechTarget();
        auto repaired=repairBefore;hits.writeRuntimeToGamSave(repaired);
        for (size_t i=0x102;i<0x11f;++i)check(repaired[i]==repairAfter[i],"live combat repair damage mismatch");
        check(hits.playerWealth_==1113024 && hits.totalRepairCost(hits.ownedMechs_[0])==107896,"live combat repair payment mismatch");
        for (size_t i=0x31;i<=0x37;++i)check(repaired[i]==repairAfter[i],"live combat repair date mismatch");
        const auto& repairedExact=*hits.ownedMechs_[0].exactDamage;
        check(repairedExact.detailed.armorSections[0].armorRemaining==4 &&
            repairedExact.detailed.internalSections[0].structureRemaining==6 &&
            repairedExact.detailed.armorSections[1].armorRemaining==0 &&
            repairedExact.detailed.internalSections[1].structureRemaining==5,
            "live armor repair changed unpaid exact section or failed to repair paid section");
        check(repairedExact.detailed.criticalComponents[5].workingCount==3 &&
            repairedExact.detailed.criticalComponents[6].workingCount==2 &&
            !repairedExact.detailed.installedWeapons[1].functional &&
            !repairedExact.detailed.installedWeapons[3].functional,"armor repair healed live actuator/weapon damage");
        const auto repairedHash=persistentMechDamageFingerprint(repairedExact);
        check(hits.saveCurrentGame(diskSave),"live repaired Save failed");hits.loadGameFromFile(diskSave);
        check(persistentMechDamageFingerprint(*hits.ownedMechs_[0].exactDamage)==repairedHash,"live repaired Restore changed exact damage");
    }
    // Two original four-actor returns: normal and shuffled owned-slot bindings.
    // Constructed input provenance is pinned with the live DS/GAM captures.
    {
        const auto root=fs::path("research/fixtures/dosbox_lance");
        const auto word=[](const auto& bytes,size_t at) {
            return unsigned(bytes.at(at))+(unsigned(bytes.at(at+1))<<8);
        };
        constexpr std::array<size_t,9> external{{1,0,3,2,5,6,8,7,4}};
        constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
        constexpr std::array<size_t,10> critical{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
        for (const std::string prefix:{"crew","shuffle"}) {
            const auto input=readFile(root/(prefix+"-entry.gam"));
            const auto returned=readFile(root/(prefix+"-exit-transfer.gam"));
            const auto entryDs=readFile(root/(prefix+"-entry.ds.bin"));
            const auto exitDs=readFile(root/(prefix+"-before-confirm.ds.bin"));
            App lance(findResourceRoot());lance.loadResources();lance.applyGamSaveToRuntime(input);
            lance.missionParticipants_=lance.currentMissionParticipants();
            check(lance.missionParticipants_.size()==4,"live lance participant count differs");
            auto params=fixture("jenner");params.setupMetadata.valid=true;params.setupMetadata.playerSlots.resize(4);
            for (size_t i=0;i<4;++i)params.setupMetadata.playerSlots[i].transform={10000+1500.0*i,0,10000,0};
            lance.applyCampaignPlayerLaunchStates(params);
            check(params.playerLaunchState && params.playerAlliedLaunchStates.size()==3,"actual lance launch adapter omitted a participant");
            auto world=BattleWorld::create(params);const auto start=world.snapshot();
            if (prefix=="shuffle") {
                BattleInputCommand fire;fire.entityId=world.playerEntityId();fire.fireWeapon=true;
                fire.selectWeaponInstanceId=5;world.enqueueInput(fire);world.tick();
            }
            auto lanceFinal=world.snapshot();
            std::vector<CampaignBattlePersistentRosterBinding> lanceBindings;
            std::vector<std::string> lancePresets;
            for (const auto& mech:lance.ownedMechs_)lancePresets.push_back(*App::battleMechPresetForChassis(mech.chassis));
            std::vector<uint64_t> expectedExact(lancePresets.size());
            for (size_t i=0;i<4;++i) {
                const auto source="player:"+std::to_string(i);
                const auto slot=word(entryDs,0x4fa6+2*i);
                check(slot==word(input,0xc2+2*i) && slot==word(exitDs,0x4fa6+2*i),"live DOS actor-to-owned binding changed");
                check(lance.missionParticipants_[i].mechIndex==int(slot) && lance.missionParticipants_[i].crewSlot==int(i),"C++ participant order differs from DOS");
                const auto before=std::find_if(start.combatants.begin(),start.combatants.end(),[&](const auto& c){return c.roster.sourceSlot==source;});
                auto current=std::find_if(lanceFinal.combatants.begin(),lanceFinal.combatants.end(),[&](const auto& c){return c.roster.sourceSlot==source;});
                check(before!=start.combatants.end() && current!=lanceFinal.combatants.end() && current->mechPresetId==lancePresets[slot],"live lance chassis identity mismatch");
                const size_t actor=0x4fb6+i*0x70;
                for (size_t n=0;n<9;++n) {
                    auto& a=current->detailedDamage.armorSections[external[n]];
                    const auto previous=word(entryDs,actor+0x19+2*n),observed=word(exitDs,actor+0x19+2*n);
                    check(before->detailedDamage.armorSections[external[n]].armorRemaining==previous,"live lance entry armor mismatch");
                    check(observed<=previous,"live lance fixture unexpectedly healed armor");
                    a.armorRemaining=static_cast<uint16_t>(observed);a.battleDamage=static_cast<int>(previous-observed);
                }
                for (size_t n=0;n<8;++n) {
                    auto& a=current->detailedDamage.internalSections[internal[n]];
                    const auto previous=word(entryDs,actor+0x2b+2*n),observed=word(exitDs,actor+0x2b+2*n);
                    check(before->detailedDamage.internalSections[internal[n]].structureRemaining==previous,"live lance entry internal mismatch");
                    check(observed<=previous,"live lance fixture unexpectedly healed internals");
                    a.structureRemaining=static_cast<uint16_t>(observed);a.battleDamage=static_cast<int>(previous-observed);
                }
                for (size_t n=0;n<10;++n) {
                    const auto& c=before->detailedDamage.criticalComponents[n];
                    const unsigned value=(n==4 || n==9)?c.totalCount-c.workingCount:(n>=5 && n<=8)?c.workingCount:c.condition;
                    check(value==word(entryDs,actor+critical[n]),"live lance entry component mismatch");
                    check(value==word(exitDs,actor+critical[n]),"live lance fixture acquired unsupported component damage");
                }
                for (const auto& mount:mech3d::catalogWeaponMounts(current->mechPresetId)) {
                    const auto slotIndex=mount.slotIndex;const bool working=before->detailedDamage.installedWeapons[slotIndex].functional;
                    check(working==(entryDs[actor+0x3b+slotIndex*3]!=0) && working==(exitDs[actor+0x3b+slotIndex*3]!=0),"live lance weapon functionality mismatch");
                }
                for (size_t pool=0;pool<6;++pool) {
                    check(unsigned(before->ammunitionByPool[pool])==word(entryDs,actor+0x59+2*pool),"live lance entry ammo mismatch");
                    check(unsigned(current->ammunitionByPool[pool])==word(exitDs,actor+0x59+2*pool),"actual C++ shot/unchanged ally ammo differs from DOS");
                }
                expectedExact[slot]=exactHash(*current);lanceBindings.push_back({source,int(slot),int(i)});
            }
            // Reverse snapshot order; duplicate Jenner chassis must still return
            // by sourceSlot -> owned index, including the shuffled player.
            std::reverse(lanceFinal.combatants.begin(),lanceFinal.combatants.end());
            lance.acceptedBattlePersistenceReport_=campaignBattlePersistenceReportFromSnapshot(lanceFinal,lanceBindings,4);
            lance.acceptedBattleMechReturnPlan_=campaignMechReturnPlan(lance.acceptedBattlePersistenceReport_,lancePresets);
            check(lance.acceptedBattleMechReturnPlan_.valid && lance.acceptedBattleMechReturnPlan_.entries.size()==4,"live lance return plan incomplete");
            lance.acceptedBattleStateGuard_.before=lance.captureCampaignPersistentState();
            lance.acceptedBattleStateGuard_.after=lance.acceptedBattleStateGuard_.before;
            lance.acceptedBattleStateGuard_.valid=lance.acceptedBattleStateGuard_.unchanged=true;
            const auto lanceReceipt=lance.commitAcceptedBattleConsequencePlan();
            check(lanceReceipt.committed && lanceReceipt.verified && lanceReceipt.saveRoundTripVerified,"live lance actual commit failed: "+lanceReceipt.reason);
            auto actual=input;lance.writeRuntimeToGamSave(actual);
            for (size_t at=0x102;at<0x25e;++at)check(actual[at]==returned[at],"live lance returned condition byte differs");
            for (size_t at=0x25e;at<0x2a6;++at)check(actual[at]==returned[at],"live lance returned ammo quantity differs");
            for (size_t at=0x2fa;at<0x342;++at)check(actual[at]==returned[at],"live lance changed type descriptors");
            for (size_t at=0xc2;at<0xca;++at)check(actual[at]==input[at],"damage return reassigned a pilot");
            if (prefix=="shuffle") {
                check(!lance.ownedMechs_[4].exactDamage,"unassigned reserve got a battle snapshot");
                check(std::equal(actual.begin()+0x102+4*29,actual.begin()+0x11f+4*29,input.begin()+0x102+4*29),"unassigned Warhammer damage changed");
                check(word(actual,0x25e+8)==13 && word(actual,0x25e+24+8)==177,"unassigned Warhammer ammo changed");
                check(word(actual,0x25e)==23 && word(actual,0x25e+2)==19 && word(actual,0x25e+6)==17,"shuffled ammo written to wrong owned slot");
            }
            const auto exactSave=dir/(prefix+"-lance.GAM");check(lance.saveCurrentGame(exactSave),"live lance actual Save failed");
            lance.loadGameFromFile(exactSave);
            for (const auto& binding:lanceBindings) {
                const auto slot=static_cast<size_t>(binding.ownedMechIndex);const auto& mech=lance.ownedMechs_[slot];
                check(mech.exactDamage && persistentMechDamageFingerprint(*mech.exactDamage)==expectedExact[slot],"live lance Restore swapped/lost exact state");
                auto entry=fixture(lancePresets[slot]);entry.playerLaunchState->persistentMechState=App::battlePersistentMechStateFromOwnedMech(mech,"live_lance_reentry");
                check(exactHash(BattleWorld::create(entry).snapshot().combatants.at(0))==expectedExact[slot],"live lance reentry rounded damage");
            }
            // DOS parent results are a separate input; no financial/time parity claim.
            const auto disk=readFile(root/(prefix+"-dos.GAM"));lance.loadGameFromFile(root/(prefix+"-dos.GAM"));
            const auto resave=dir/(prefix+"-dos.GAM");check(lance.saveCurrentGame(resave) && readFile(resave)==disk,"live multi-mech disk Save changes bytes");
            check(disk==readFile(root/(prefix+"-cpp.GAM")) && disk==readFile(root/(prefix+"-dos-back.GAM")),"live multi-mech DOS -> C++ -> DOS disk round trip mismatch");
        }
        // The legacy crew file has separately documented recruitment repairs.
        // Its ammo type words nevertheless remain opaque on same-chassis Save.
        App legacy(findResourceRoot());legacy.loadResources();
        const auto original=readFile(root/"legacy-crew-dos.GAM");legacy.loadGameFromFile(root/"legacy-crew-dos.GAM");
        const auto legacySave=dir/"LEGACYCREW.GAM";check(legacy.saveCurrentGame(legacySave),"legacy crew Save failed");
        const auto saved=readFile(legacySave),old=readFile(root/"cpp-pre.GAM");
        for (size_t at=0x2fa;at<0x342;++at)check(saved[at]==original[at],"same-chassis Save changed original ammo descriptor");
        for (size_t at:{size_t(0x2fa),size_t(0x2fe),size_t(0x300)})
            check(old[at]!=original[at] && saved[at]==original[at],"legacy descriptor regression lost observed defect");
    }
    // Actual DOS disk saves, including two completed RETREAT paths. No-action
    // whole-file parity is asserted only for this pinned, supported corpus.
    {
        const auto root=fs::path("research/fixtures/dosbox_roundtrip");
        App roundTrip(findResourceRoot());roundTrip.loadResources();
        for (const auto* name:{"dos-first.GAM","dos-retreat.GAM","dos-ammo.GAM"}) {
            const auto original=readFile(root/name);roundTrip.loadGameFromFile(root/name);
            check(roundTrip.gamRawStateValid_ && roundTrip.gamRawState_==original,"live DOS Restore failed");
            const auto output=dir/name;check(roundTrip.saveCurrentGame(output),"live DOS re-Save failed");
            check(readFile(output)==original,"no-action Save changed original disk bytes");
            roundTrip.loadGameFromFile(output);
            check(roundTrip.saveCurrentGame(output) && readFile(output)==original,
                "second Save/Restore changed preserved original bytes");
        }
        check(readFile(root/"dos-first.GAM")==readFile(root/"dos-back.GAM") &&
            readFile(root/"dos-ammo.GAM")==readFile(root/"dos-ammo-back.GAM"),"original cross-engine disk round trip mismatch");
        for (const auto* prefix:{"battle","ammo"}) {
            const auto supplied=readFile(root/(std::string(prefix)+"-entry.gam"));
            const auto returned=readFile(root/(std::string(prefix)+"-exit-transfer.gam"));
            const auto ds=readFile(root/(std::string(prefix)+"-before-confirm.ds.bin"));
            roundTrip.applyGamSaveToRuntime(supplied);
            auto launch=fixture("jenner");launch.playerLaunchState->persistentMechState=
                App::battlePersistentMechStateFromOwnedMech(roundTrip.ownedMechs_[0],"live_retreat_entry");
            launch.playerLaunchState->ammunitionByPool=roundTrip.ownedMechs_[0].ammoPacks;
            auto world=BattleWorld::create(launch);
            if (std::string(prefix)=="ammo") {
                BattleInputCommand fire;fire.entityId=world.playerEntityId();fire.fireWeapon=true;
                fire.selectWeaponInstanceId=1;world.enqueueInput(fire);world.tick();
            }
            const auto actor=world.snapshot().combatants.at(0);
            const auto word=[&](size_t offset) {return unsigned(ds.at(0x4fb6+offset))+(unsigned(ds.at(0x4fb7+offset))<<8);};
            constexpr std::array<size_t,9> external{{1,0,3,2,5,6,8,7,4}};
            constexpr std::array<size_t,8> internal{{1,0,3,2,5,7,6,4}};
            constexpr std::array<size_t,10> critical{{0x11,0x13,0xf,0xd,0xb,0x68,0x6a,0x6c,0x6e,0x17}};
            for (size_t i=0;i<9;++i)check(actor.detailedDamage.armorSections[external[i]].armorRemaining==word(0x19+i*2),"live retreat armor differs");
            for (size_t i=0;i<8;++i)check(actor.detailedDamage.internalSections[internal[i]].structureRemaining==word(0x2b+i*2),"live retreat internals differ");
            for (size_t i=0;i<10;++i) {
                const auto& c=actor.detailedDamage.criticalComponents[i];
                const unsigned value=(i==4 || i==9)?c.totalCount-c.workingCount:(i>=5 && i<=8)?c.workingCount:c.condition;
                check(value==word(critical[i]),"live retreat component differs");
            }
            for (size_t i=0;i<5;++i)check(actor.detailedDamage.installedWeapons[i].functional==(ds[0x4fb6+0x3b+i*3]!=0),"live retreat weapon differs");
            for (size_t i=0;i<6;++i)check(unsigned(actor.ammunitionByPool[i])==word(0x59+i*2),"live retreat ammunition differs");
            const auto state=campaignMechStateFromCombatant(actor);
            App::applyBattlePersistentMechStateToOwnedMech(roundTrip.ownedMechs_[0],state);
            roundTrip.ownedMechs_[0].ammoPacks=actor.ammunitionByPool;
            roundTrip.finalizeLoadedMech(roundTrip.ownedMechs_[0]);
            auto output=supplied;roundTrip.writeRuntimeToGamSave(output);
            check(std::equal(output.begin()+0x102,output.begin()+0x11f,returned.begin()+0x102),"live original returned damage mismatch");
            check(std::equal(output.begin()+0x25e,output.begin()+0x2a6,returned.begin()+0x25e) &&
                std::equal(output.begin()+0x2fa,output.begin()+0x342,returned.begin()+0x2fa),"live original returned ammo planes mismatch");
            check(returned[0x1f]==2,"capture is not original RETREAT result");
        }
        // All catalog loadouts preserve opaque unused WORDs on same-slot Save,
        // but explicit new-slot initialization still clears all three planes.
        for (const auto& catalog:mech3d::mechCatalogEntries()) {
            const auto chassisId=*App::battleChassisForMechPreset(catalog.presetId);
            const auto capacity=App::ammunitionCapacityForMech(App::makeMech(chassisId));
            const auto present=size_t(std::count_if(capacity.begin(),capacity.end(),[](int n){return n>0;}));
            auto original=readFile(root/"dos-first.GAM");
            App::writeU16Le(original,0xea,App::gamChassisId(chassisId));
            encodeOriginalGamOwnedMechAmmunition(original,0,capacity,capacity);
            for (size_t i=0;i<present;++i)
                App::writeU16Le(original,0x2fa+i*24,static_cast<uint16_t>(0xb123+i));
            for (size_t i=present;i<3;++i) {
                App::writeU16Le(original,0x25e+i*24,0xa123);
                App::writeU16Le(original,0x2fa+i*24,0x5a45);
            }
            roundTrip.applyGamSaveToRuntime(original);auto saved=original;roundTrip.writeRuntimeToGamSave(saved);
            for (size_t i=present;i<3;++i)
                check(App::readGamU16Le(saved,0x25e+i*24)==0xa123 && App::readGamU16Le(saved,0x2fa+i*24)==0x5a45,
                    "same-chassis Save erased opaque unused ammo words");
            for (size_t i=0;i<3;++i)
                check(App::readGamU16Le(saved,0x2fa+i*24)==App::readGamU16Le(original,0x2fa+i*24),"same-slot Save rewrote an active/inactive descriptor WORD");
            encodeOriginalGamOwnedMechAmmunition(saved,0,capacity,capacity);
            size_t activeOrdinal=0;
            for (size_t pool=0;pool<capacity.size();++pool)if (capacity[pool]>0)
                check(App::readGamU16Le(saved,0x2fa+24*activeOrdinal++)==pool,"new-slot active descriptor initialization failed");
            for (size_t i=present;i<3;++i)
                check(App::readGamU16Le(saved,0x25e+i*24)==0 && App::readGamU16Le(saved,0x2fa+i*24)==0,
                    "new-slot ammo initialization retained stale words");
        }
    }
    check(readFile(save)==validGam, "campaign transition wrote a save file");
    check(!fs::exists(save.wstring()+L".mw-new") && !fs::exists(save.wstring()+L".mw-old") &&
        !fs::exists(save.wstring()+L".mwstate.mw-new") && !fs::exists(save.wstring()+L".mwstate.mw-old"), "save transaction left temporary files");
}

int main(int argc,char** argv) {
    try {
        if (argc==4 && std::string(argv[1])=="--gam-roundtrip") {
            gamRoundTrip=std::make_pair(fs::path(argv[2]),fs::path(argv[3]));
            runCampaignMechPersistenceSmoke();
            std::cout<<"actual App GAM Restore/Save round trip: ok\n";
            return 0;
        }
        if (argc!=1) throw std::runtime_error("usage: mw_campaign_mech_persistence_smoke [--gam-roundtrip input output]");
        catalogRoundTrips(); saveCodecAndReplay(); weaponDamageRoundTrip(); runCampaignMechPersistenceSmoke();
        std::cout << "mw_campaign_mech_persistence_smoke: ok checks=" << checks << " catalog=8 lance=4 legacy_bytes=1816 exact_sidecar=1 campaign_commit=verified repairs=verified\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
