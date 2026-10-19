#include "../main.cpp"
#include "audio/pc_speaker.h"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool v,const std::string& s) { ++checks;if(!v)throw std::runtime_error(s); }
BattleStartParams fixture(const std::string& preset) {
    BattleStartParams p;p.playerMechPresetId=preset;
    p.terrainScenarioPath="Sorted Original Files/DAT/SNARIO.DAT";
    p.terrainScenarioIndex=2;p.terrainEnvironmentId=1;p.playerStartTransform={60000,0,30000,0};
    p.fixedTickSeconds=.05;p.deterministicCombatRuntimeEnabled=p.individualWeaponRuntimeEnabled=true;
    p.originalProjectileRuntimeEnabled=p.originalHeatRuntimeEnabled=p.originalMajorSystemRuntimeEnabled=true;
    p.weaponTargetPolicy=BattleWeaponTargetPolicy::CrosshairRayVisibleCombatantProvisional;
    BattleCombatantLaunchState player;player.mechPresetId=preset;player.startTransform=p.playerStartTransform;
    player.persistentMechState.valid=true;player.persistentMechState.armorPercent=100;
    player.persistentMechState.heatSinksTotal=player.persistentMechState.heatSinksWorking=10;
    player.ammunitionStateValid=true;player.ammunitionByPool.fill(100);p.playerLaunchState=player;
    auto enemy=player;enemy.roster.team=BattleTeam::Opposing;enemy.startTransform={10000,0,10000,0};
    p.combatantLaunchStates={enemy};return p;
}
void key(BattleWorld& w,unsigned k) {
    auto c=*mw::presentation::campaignCockpitWeaponKey(k,false);c.entityId=w.playerEntityId();c.tickIndex=w.tickIndex();w.enqueueInput(c);
}
void wav(const fs::path& p,const std::vector<int16_t>& samples) {
    std::ofstream f(p,std::ios::binary);
    auto n=[&](uint32_t value,int bytes){while(bytes--){f.put(static_cast<char>(value));value>>=8;}};
    f.write("RIFF",4);n(36+static_cast<uint32_t>(samples.size()*2),4);f.write("WAVEfmt ",8);
    n(16,4);n(1,2);n(2,2);n(48000,4);n(192000,4);n(4,2);n(16,2);
    f.write("data",4);n(static_cast<uint32_t>(samples.size()*2),4);
    f.write(reinterpret_cast<const char*>(samples.data()),static_cast<std::streamsize>(samples.size()*2));
    check(bool(f),"write event playback preview");
}
}
void runSpeakerSceneSmoke() {
    using mw::presentation::battleSpeakerRequests;
    App app(findResourceRoot());app.loadResources();app.soundEnabled_=true;
    app.changeState(ScreenState::MissionBattleStub);
    const fs::path output="outputs/audio_event_fixes";fs::create_directories(output);
    const auto bank=mw::audio::SpeakerBank::load("Original/BTECHSND.SND");
    for(const auto& entry:mw::mech3d::mechCatalogEntries()) {
        const auto animation=mw::mech3d::makeCatalogAnimationDefinition(entry.presetId,"walk");
        check(animation.sequences.front().frameDurationMs==180 &&
            mw::mech3d::animationFrameCount(animation)==8,"step timing follows catalog walk");
        for(double throttle:{1.0,-1.0,.25}) {
            auto world=BattleWorld::create(fixture(entry.presetId));
            size_t footsteps=0;
            mw::audio::PcSpeaker vm(bank);std::vector<int16_t> pcm;
            for(int tick=0;tick<200;++tick) {
                const auto before=world.snapshot();
                BattleInputCommand c;c.entityId=world.playerEntityId();c.tickIndex=world.tickIndex();c.throttle=throttle;
                world.enqueueInput(c);world.tick();const auto after=world.snapshot();
                const auto fingerprint=battleSnapshotFingerprint(after);
                const auto cues=battleSpeakerRequests(before,after);
                for(const auto& cue:cues) {if(cue.effect==11) {++footsteps;check(cue.priority==1,"original step priority");}vm.play(cue.effect,cue.priority);}
                const auto count=app.speaker_.requestCount();app.updateBattleAudio(before,after);
                check(app.speaker_.requestCount()==count+cues.size(),"App queues walking cues");
                check(battleSnapshotFingerprint(world.snapshot())==fingerprint,"steps do not change simulation/RNG");
                std::array<int16_t,4800> block{};vm.render(block.data(),2400);
                if(entry.presetId=="jenner" && throttle==1.0) pcm.insert(pcm.end(),block.begin(),block.end());
            }
            check(footsteps>0,"audible forward/reverse/slow footsteps: "+entry.presetId);
            if(!pcm.empty()) {
                int peak=0;for(auto sample:pcm)peak=std::max(peak,std::abs(int(sample)));
                check(peak>100 && peak<32767,"walking renders audible unclipped PCM");
                wav(output/"walking-jenner.wav",pcm);
            }
        }
    }
    // Exercise completed ticks from the same AI/damage path as campaign combat.
    for (const std::string enemyPreset : {"locust", "jenner", "warhammer", "shadow_hawk"}) {
        auto p=fixture("battlemaster");
        p.combatAiPolicy=BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
        p.deterministicCollisionRuntimeEnabled=true;
        p.mission=battleMissionBriefingFromDefinition(*battleMissionDefinitionById(8));
        p.combatantLaunchStates[0].mechPresetId=enemyPreset;
        p.combatantLaunchStates[0].roster.gunnerySkill=3;
        p.combatantLaunchStates[0].startTransform={60000,0,32000,3.141592653589793};
        auto world=BattleWorld::create(p);
        size_t hits=0,damageCues=0,misses=0,incoming=0;
        mw::audio::PcSpeaker vm(bank);std::vector<int16_t> pcm;
        for(int tick=0;tick<800 && !world.battleResult();++tick) {
            auto before=world.snapshot();world.tick();auto after=world.snapshot();
            const auto cues=battleSpeakerRequests(before,after);
            size_t contacts=0;
            for(const auto& shot:after.shotEvents)
                if(shot.targetEntityId==world.playerEntityId() && shot.shooterEntityId!=world.playerEntityId()) {
                    if(shot.hit && (shot.deliveryState==BattleShotDeliveryState::Immediate || shot.deliveryState==BattleShotDeliveryState::ProjectileImpact)) {++hits;++contacts;}
                    else ++misses;
                }
            size_t tickCues=0;
            for(const auto& cue:cues) {if(cue.effect==7) {++damageCues;++tickCues;check(cue.priority==40,"incoming hit priority above weapons");}
                if(cue.effect==8)++incoming; vm.play(cue.effect,cue.priority);}
            check(tickCues>=contacts,"every actual contact reaches sound on its own tick");
            const auto count=app.speaker_.requestCount();app.updateBattleAudio(before,after);
            check(app.speaker_.requestCount()==count+cues.size(),"App delivers incoming fire cues");
            std::array<int16_t,4800> block{};vm.render(block.data(),2400);
            pcm.insert(pcm.end(),block.begin(),block.end());
        }
        std::cout<<"incoming "<<enemyPreset<<" hits="<<hits<<" damage_cues="<<damageCues<<" warnings="<<incoming<<" other_shots="<<misses<<'\n';
        check(hits>0 && damageCues>=hits,"actual enemy damage has a cue: "+enemyPreset);
        if(enemyPreset=="shadow_hawk")check(incoming>0,"actual incoming ballistic warning");
        int peak=0;for(auto sample:pcm)peak=std::max(peak,std::abs(int(sample)));
        check(peak>100 && peak<32767,"actual incoming event stream renders audible unclipped PCM");
        wav(output/("incoming-"+enemyPreset+".wav"),pcm);
    }
    for(const auto& entry:mw::mech3d::mechCatalogEntries()) {
        auto world=BattleWorld::create(fixture(entry.presetId));const auto before=world.snapshot();
        key(world,VK_SPACE);key(world,VK_TAB);world.tick();const auto after=world.snapshot();
        const auto fingerprint=battleSnapshotFingerprint(after);
        const auto cues=battleSpeakerRequests(before,after);
        check(cues.size()==after.combatants.front().weapons.size(),"every accepted salvo weapon: "+entry.presetId);
        for(size_t i=0;i<cues.size();++i) {
            const auto& id=after.combatants.front().weapons[i].weaponTypeId;
            const int expected=id=="ppc"?4:id=="ac5"?2:(id=="srm2"||id=="srm4"||id=="srm6"||id=="lrm5")?3:1;
            check(cues[i].effect==expected && cues[i].priority==10,"weapon cue: "+id);
        }
        const auto n=app.speaker_.requestCount();app.updateBattleAudio(before,after);
        check(app.speaker_.requestCount()==n+cues.size(),"App delivers entire salvo");
        // A player contact must remain audible while our own AWS salvo fills
        // all four speaker slots, including a footstep and incoming warning.
        auto overlapBefore=before,overlapAfter=after;
        overlapBefore.combatants[0].activeAnimationId=overlapAfter.combatants[0].activeAnimationId="walk";
        overlapBefore.combatants[0].walkAnimationElapsedMs=539;
        overlapAfter.combatants[0].walkAnimationElapsedMs=540;
        BattleShotDiagnostic incoming;incoming.valid=true;incoming.targetKind=BattleShotTargetKind::Combatant;
        incoming.shooterEntityId=before.combatants[1].id;incoming.targetEntityId=before.combatants[0].id;
        incoming.tickIndex=before.tickIndex;incoming.deliveryState=BattleShotDeliveryState::ProjectileInFlight;
        overlapAfter.shotEvents.push_back(incoming);
        BattleImpactEvent impact;impact.valid=true;impact.targetKind=BattleShotTargetKind::Combatant;
        impact.shooterEntityId=incoming.shooterEntityId;impact.targetEntityId=incoming.targetEntityId;
        impact.deliveryState=BattleShotDeliveryState::Immediate;impact.impactTickIndex=before.tickIndex;
        overlapAfter.impactEvents.push_back(impact);
        const auto overlapCues=battleSpeakerRequests(overlapBefore,overlapAfter);
        check(overlapCues.size()==cues.size()+3,"salvo plus step, incoming warning and player contact");
        mw::audio::PcSpeaker mixed(bank),damageOnly(bank);
        for(const auto& cue:overlapCues)mixed.play(cue.effect,cue.priority);
        damageOnly.play(7,40);
        std::vector<int16_t> mixedPcm(19200*2),damagePcm(mixedPcm.size());
        mixed.render(mixedPcm.data(),19200);damageOnly.render(damagePcm.data(),19200);
        check(mixedPcm==damagePcm,"player hit audibly preempts our entire salvo: "+entry.presetId);
        check(battleSnapshotFingerprint(after)==fingerprint,"audio changed simulation/RNG");
        check(battleSpeakerRequests(after,after).empty(),"paused/repeated snapshot");
        key(world,VK_TAB);world.tick();check(battleSpeakerRequests(after,world.snapshot()).empty(),"rejected cooldown has no sound");
        app.soundEnabled_=false;app.updateBattleAudio(before,after);
        check(app.speaker_.requestCount()==n+cues.size() && app.speaker_.stopped(),"SOUND OFF");
        app.soundEnabled_=true;
    }
    auto before=BattleWorld::create(fixture("jenner")).snapshot(),after=before;after.tickIndex=before.tickIndex+1;
    auto expect=[&](std::initializer_list<std::pair<int,int>> expected) {
        const auto cues=battleSpeakerRequests(before,after);check(cues.size()==expected.size(),"event count");
        size_t i=0;for(auto [effect,priority]:expected) { check(cues[i].effect==effect && cues[i].priority==priority,"event identity");++i; }
    };
    after.combatants[0].jumpJetThrusting=true;expect({{14,15}});after.combatants[0].jumpJetThrusting=false;
    after.combatants[0].activeAnimationId=before.combatants[0].activeAnimationId="walk";
    before.combatants[0].walkAnimationElapsedMs=539;after.combatants[0].walkAnimationElapsedMs=540;expect({{11,1}});
    before.combatants[0].walkAnimationElapsedMs=540;after.combatants[0].walkAnimationElapsedMs=541;expect({});
    before.combatants[0].walkAnimationElapsedMs=1259;after.combatants[0].walkAnimationElapsedMs=1260;expect({{11,1}});
    after.combatants[0].airborne=true;expect({});after.combatants[0].airborne=false;
    after.combatants[0].activeAnimationId="fall";expect({});
    after.combatants[0].activeAnimationId="idle";expect({});
    after.combatants[0].activeAnimationId="walk";after.combatants[0].mechDestroyed=true;expect({});
    after.combatants[0]=before.combatants[0];after.combatants[0].walkAnimationElapsedMs=0;expect({});
    before.combatants[0].walkAnimationElapsedMs=0;
    BattleShotDiagnostic hit;hit.valid=hit.hit=true;hit.result=BattleShotResult::Hit;
    hit.targetKind=BattleShotTargetKind::Combatant;hit.deliveryState=BattleShotDeliveryState::Immediate;
    hit.shooterEntityId=before.combatants[0].id;hit.targetEntityId=before.combatants[1].id;hit.impactTickIndex=before.tickIndex;
    after.shotEvents={hit};expect({{5,5}});
    std::swap(hit.shooterEntityId,hit.targetEntityId);after.shotEvents={hit};expect({{7,40}});
    hit.deliveryState=BattleShotDeliveryState::ProjectileImpact;after.shotEvents={hit};expect({{5,5},{7,40}});
    after.shotEvents.push_back(hit);expect({{5,5},{7,40},{5,5},{7,40}});
    hit.result=BattleShotResult::HitTerrain;hit.targetKind=BattleShotTargetKind::Terrain;
    hit.deliveryState=BattleShotDeliveryState::ProjectileTerrainImpact;after.shotEvents={hit};expect({});
    hit.targetKind=BattleShotTargetKind::Combatant;hit.targetEntityId=before.combatants[0].id;
    hit.shooterEntityId=before.combatants[1].id;hit.deliveryState=BattleShotDeliveryState::ProjectileInFlight;
    hit.tickIndex=before.tickIndex;after.shotEvents={hit};expect({{8,15}});
    hit.tickIndex=after.tickIndex;after.shotEvents={hit};expect({});
    BattleImpactEvent contact;contact.valid=true;contact.targetKind=BattleShotTargetKind::Combatant;
    contact.targetEntityId=before.combatants[0].id;contact.shooterEntityId=before.combatants[1].id;
    contact.deliveryState=BattleShotDeliveryState::Immediate;contact.impactTickIndex=before.tickIndex;
    after.shotEvents.clear();after.impactEvents={contact};expect({{7,40}});
    hit.hit=true;hit.result=BattleShotResult::Hit;hit.impactTickIndex=contact.impactTickIndex;hit.deliveryState=contact.deliveryState;
    after.shotEvents={hit};expect({{7,40}}); // Both histories describe one hit.
    ++before.tickIndex;++after.tickIndex;expect({}); // Flash history cannot replay old contacts.
    --before.tickIndex;--after.tickIndex;
    after.shotEvents.clear();contact.impactTickIndex=after.tickIndex;after.impactEvents={contact};expect({});
    after.impactEvents.clear();
    after.shotEvents.clear();after.combatants[0].detailedDamage.armorSections[0].battleDamage+=3;expect({{7,40}});
    after.combatants[0]=before.combatants[0];
    before.combatants[0].majorSystems.enabled=after.combatants[0].majorSystems.enabled=true;
    after.combatants[0].majorSystems.sensors=static_cast<BattleMechSystemStatus>(1);
    after.combatants[0].majorSystems.engine=static_cast<BattleMechSystemStatus>(2);expect({{10,60},{10,60}});
    after.combatants[0]=before.combatants[0];
    after.conclusionDelay.active=after.conclusionDelay.freezesSimulation=true;expect({{5,100}});
    before=after;++after.tickIndex;after.combatants[0].jumpJetThrusting=true;expect({});
    app.speaker_.play(0,10);app.changeState(ScreenState::MissionDebrief);check(app.speaker_.stopped(),"battle exit clears looping effect");
    app.speaker_.play(9,10);app.applyGamSaveToRuntime(readFile("research/fixtures/dosbox_story/start.gam"));
    check(app.speaker_.stopped(),"Restore clears old effects");
    std::cout<<"mw_speaker_scene_smoke: ok "<<checks<<" checks;8 real salvo fixtures and event routing\n";
}
int main() { try{runSpeakerSceneSmoke();return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
