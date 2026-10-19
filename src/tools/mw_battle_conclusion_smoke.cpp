#include "battle/battle_world.h"
#include "battle/campaign_mech_persistence.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <iterator>
#include <map>
#include <cmath>
#include <limits>

namespace {
using namespace mw::battle;
size_t checks=0;
void check(bool v,const char* message){++checks;if(!v)throw std::runtime_error(message);}
std::vector<uint8_t> bytes(const char* name){std::ifstream f(name,std::ios::binary);if(!f)throw std::runtime_error(name);return {std::istreambuf_iterator<char>(f),{}};}
uint16_t word(const std::vector<uint8_t>& b,size_t p){return static_cast<uint16_t>(b.at(p)|(b.at(p+1)<<8));}
int dosTeamResult(int pa,int ea) {
    const auto in=bytes("research/fixtures/dosbox_conclusion/FIN.BIN"),out=bytes("research/fixtures/dosbox_conclusion/FOUT.BIN");
    for(size_t row=0;row<in.size()/32;++row){const auto p=row*32;
        if(word(in,p)==0 && word(in,p+2)==0 && word(in,p+4)==0 && word(in,p+6)==0 &&
           word(in,p+12)==1 && word(in,p+14)==255 && word(in,p+16)==static_cast<uint16_t>(pa) && word(in,p+24)==static_cast<uint16_t>(ea))
            return out.at(row*2)?0:out.at(row*2+1)?1:-1;
    }
    throw std::runtime_error("missing original team-result case");
}
BattleStartParams setup() {
    BattleStartParams p;p.playerMechPresetId="jenner";p.mechSystemsSnapshotEnabled=true;
    p.deterministicCombatRuntimeEnabled=true;p.individualWeaponRuntimeEnabled=true;
    p.originalMajorSystemRuntimeEnabled=true;p.constrainToTerrainBounds=false;
    p.originalHeatRuntimeEnabled=p.originalProjectileRuntimeEnabled=p.deterministicCollisionRuntimeEnabled=true;
    p.combatAiPolicy=BattleCombatAiPolicy::ReplacementDeterministicCombatAi;
    p.combatConclusionDelaySeconds=p.fatalConclusionDelaySeconds=0;p.playerStartTransform={10000,0,10000,0};
    p.playerRoster.team=BattleTeam::Player;p.playerRoster.sourceSlot="player:0";
    for(int slot=1;slot<8;++slot){BattleCombatantLaunchState a;a.mechPresetId="jenner";
        a.roster.team=slot<4?BattleTeam::Player:BattleTeam::Opposing;
        a.roster.originalLiveObjectSlot=static_cast<uint8_t>(slot);
        a.roster.sourceSlot=(slot<4?"player:":"opposing:")+std::to_string(slot<4?slot:slot-4);
        a.startTransform={10000.+slot*3000.,0,10000.,0};
        if(slot<4)p.playerAlliedLaunchStates.push_back(a);else p.combatantLaunchStates.push_back(a);
    }
    return p;
}
void fatal(BattleStartParams& p,bool cockpit) {
    const auto s=BattleWorld::create(p).snapshot();BattleCombatantLaunchState a;
    a.mechPresetId=p.playerMechPresetId;a.roster=p.playerRoster;a.startTransform=p.playerStartTransform;
    a.persistentMechState=campaignMechStateFromCombatant(s.combatants[0]);
    auto& d=a.persistentMechState.exactDamage->detailed;
    if(cockpit)d.originalCockpitCriticalFlag=true;
    else {auto& c=d.criticalComponents[3];c.condition=2;c.workingCount=0;c.functional=false;}
    p.playerLaunchState=std::move(a);
}
void wonObjective(BattleStartParams& p) {
    p.mission=battleMissionBriefingFromDefinition(*battleMissionDefinitionById(24));
    const auto intent=battleMissionObjectiveBriefingById(24);
    p.setupMetadata.valid=true;p.setupMetadata.dedicatedObjective.valid=true;
    p.deterministicObjectiveRuntimeEnabled=true;p.objective.valid=true;
    p.objective.activeObjectProven=p.objective.transformProven=p.objective.missionIntentProven=true;
    p.objective.damagePolicyProven=true;p.objective.missionTargetKind=intent.targetKind;
    p.objective.missionIntentProvenance=intent.provenance;p.objective.missionIntent=intent.intent;
    p.objective.depleted=true;p.objective.maxDamage=3;p.objective.damage=3;
    p.objective.missionIntentBoundToObjective=true;p.objective.missionSemanticsProven=true;
}
}
int main(){try{
    // Independently measured original IRQ counts, not a chosen host duration.
    for(const char* path:{"research/fixtures/dosbox_conclusion_timer/T300.BIN",
                         "research/fixtures/dosbox_conclusion_timer/T100.BIN",
                         "research/fixtures/dosbox_conclusion_timer/Tmax.BIN"}) {
        const auto dos=bytes(path);check(dos.size()==80,"malformed original PIT timer fixture");
        for(bool cockpit:{false,true})for(double dt:{.01,.1,.25}) {
            const size_t row=(cockpit?4u:3u)*16u;
            const double measuredSeconds=double(word(dos,row+6))*word(dos,row+8)/1193182.0;
            auto p=setup();p.fixedTickSeconds=dt;p.combatConclusionDelaySeconds=.7;
            p.fatalConclusionDelaySeconds=BattleStartParams{}.fatalConclusionDelaySeconds;
            check(std::abs(p.fatalConclusionDelaySeconds-measuredSeconds)<1e-12,"default fatal time differs from actual DOS IRQ budget");
            fatal(p,cockpit);auto w=BattleWorld::create(p);w.tick();const auto frozen=w.snapshot();
            const auto ticks=static_cast<uint64_t>(std::ceil(measuredSeconds/dt));
            check(frozen.conclusionDelay.freezesSimulation && frozen.conclusionDelay.resolveTickIndex-frozen.conclusionDelay.startTickIndex==ticks,
                "fatal timer used ordinary adapter or wrong quantization");
            while(w.tickIndex()<frozen.conclusionDelay.resolveTickIndex) {
                BattleInputCommand input;input.entityId=w.playerEntityId();input.tickIndex=w.tickIndex();input.acknowledgeConclusion=true;
                w.enqueueInput(input);w.tick();check(!w.battleResult(),"fatal timer ended early");
            }
            w.tick();check(w.battleResult() && w.battleResult()->rawResultCode==1,"fatal PIT budget did not end in original raw1");
            auto normalized=w.snapshot();normalized.tickIndex=frozen.tickIndex;normalized.elapsedMs=frozen.elapsedMs;
            normalized.result=frozen.result;normalized.missionRuntimeState=frozen.missionRuntimeState;normalized.conclusionDelay=frozen.conclusionDelay;
            check(battleSnapshotFingerprint(normalized)==battleSnapshotFingerprint(frozen),"fatal timer changed combat export payload");
        }
    }
    for(double invalid:{-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        auto p=setup();p.fatalConclusionDelaySeconds=invalid;bool rejected=false;
        try{BattleWorld::create(p);}catch(const std::exception&){rejected=true;}
        check(rejected,"invalid fatal timing accepted");
    }
    // The DOS corpus feeds actual BIOS words through the original reader and
    // normal input decoder. ASCII27 with another scan is deliberately not Esc.
    size_t keyboardCases=0;
    {const auto in=bytes("research/fixtures/dosbox_conclusion_input/KIN.BIN");
     const auto out=bytes("research/fixtures/dosbox_conclusion_input/KOUT.BIN");
     check(in.size()%20==0 && out.size()==in.size()/20*26,"malformed DOS keyboard fixture");
     for(size_t row=0;row<in.size()/20;++row){const auto ip=row*20,op=row*26;
        if(word(in,ip+2)!=7 || word(in,ip+12)!=1 || word(in,ip+14)!=0x1e || word(in,ip+16))continue;
        const auto flags=word(in,ip);auto p=setup();p.combatConclusionDelaySeconds=2.;
        auto w=BattleWorld::create(p);const auto actors=w.snapshot().combatants;
        for(size_t slot=0;slot<8;++slot)if((slot<4 && (flags&256)) || (slot>=4 && (flags&1)))
            w.setMissionStatus(actors[slot].id,CombatantMissionStatus::Destroyed);
        w.tick();auto replay=w;
        BattleInputCommand input;input.tickIndex=w.tickIndex();input.entityId=w.playerEntityId();
        input.acknowledgeConclusion=word(in,ip+8)==0 && word(in,ip+10)>>8==1;
        const auto text=serializeBattleReplay(BattleReplay{{input}});
        const auto parsed=parseBattleReplay(text);
        check(parsed.commands[0].acknowledgeConclusion==input.acknowledgeConclusion,"replay lost acknowledgment");
        w.enqueueInput(input);replay.enqueueInput(parsed.commands[0]);w.tick();replay.tick();
        check((w.battleResult()?w.battleResult()->rawResultCode:65535)==word(out,op),"keyboard result differs from actual DOS input chain");
        check(battleSnapshotFingerprint(w.snapshot())==battleSnapshotFingerprint(replay.snapshot()),"keyboard replay diverged");
        ++keyboardCases;
     }
     check(keyboardCases==80,"missing DOS keyboard comparisons");}
    for(int version:{1,2,3,4}){
        const std::string base="0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0";
        const auto text="MW_BATTLE_REPLAY "+std::to_string(version)+"\n"+base+
            (version>=2?" 0 0 0":"")+(version>=3?" 0":"")+(version>=4?" 0":"")+"\n";
        check(!parseBattleReplay(text).commands[0].acknowledgeConclusion,"old replay synthesized Escape");
        check(!parseBattleReplay(text).commands[0].confirmRetreat,"old replay synthesized retreat");
    }
    for(const std::string tail:{"", " 2", " 1 trailing"}) {
        bool rejected=false;try{parseBattleReplay("MW_BATTLE_REPLAY 5\n0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"+tail+"\n");}
        catch(const std::exception&){rejected=true;}
        check(rejected,"invalid retreat replay accepted");
    }
    for(int mode=0;mode<6;++mode) {
        auto p=setup();p.combatConclusionDelaySeconds=2.;
        if(mode==5){fatal(p,true);p.fatalConclusionDelaySeconds=2.;}
        auto w=BattleWorld::create(p);
        if(mode==3 || mode==4)
            for(const auto& a:w.snapshot().combatants)
                if((a.roster.team==BattleTeam::Opposing)==(mode==3))
                    w.setMissionStatus(a.id,CombatantMissionStatus::Destroyed);
        w.tick();const auto before=w.snapshot();auto replay=w;
        BattleInputCommand input;input.tickIndex=w.tickIndex();input.entityId=w.playerEntityId();
        input.confirmRetreat=true;input.throttle=1;input.fireWeapon=true;input.standUp=true;
        if(mode==1)input.entityId=before.combatants[1].id;
        if(mode==2)input.tickIndex+=2;
        const auto parsed=parseBattleReplay(serializeBattleReplay(BattleReplay{{input}}));
        check(parsed.commands[0].confirmRetreat,"replay lost confirmed retreat");
        w.enqueueInput(input);replay.enqueueInput(parsed.commands[0]);w.tick();replay.tick();
        check(battleSnapshotFingerprint(w.snapshot())==battleSnapshotFingerprint(replay.snapshot()),"retreat replay diverged");
        check(w.battleResult().has_value()==(mode==0),"retreat ignored input identity/time or pending/fatal result");
        if(mode==0) {
            check(w.battleResult()->rawResultCode==2 && w.battleResult()->state==BattleMissionRuntimeState::Withdraw,"retreat result mismatch");
            auto normalized=w.snapshot();normalized.result=before.result;normalized.missionRuntimeState=before.missionRuntimeState;
            check(battleSnapshotFingerprint(normalized)==battleSnapshotFingerprint(before),"confirmed retreat advanced combat/time/RNG");
            w.tick();check(battleSnapshotFingerprint(w.snapshot())==battleSnapshotFingerprint(replay.snapshot()),"terminal retreat advanced");
        }
    }
    for(const std::string tail:{"", " 2", " 1 trailing"}) {
        bool rejected=false;try{parseBattleReplay("MW_BATTLE_REPLAY 4\n0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0 0"+tail+"\n");}
        catch(const std::exception&){rejected=true;}
        check(rejected,"invalid acknowledgment replay accepted");
    }
    {auto p=setup();p.combatConclusionDelaySeconds=2.;auto w=BattleWorld::create(p);
        BattleInputCommand input;input.entityId=w.playerEntityId();input.acknowledgeConclusion=true;
        w.enqueueInput(input);w.tick();check(!w.battleResult(),"Escape invented an outcome");
        for(const auto& a:w.snapshot().combatants)if(a.roster.team==BattleTeam::Opposing)w.setMissionStatus(a.id,CombatantMissionStatus::Destroyed);
        w.tick();check(!w.battleResult(),"earlier Escape leaked into a later outcome");
        input.tickIndex=w.tickIndex();input.entityId=w.snapshot().combatants[1].id;w.enqueueInput(input);w.tick();
        check(!w.battleResult(),"ally acknowledged commander outcome");
        input.tickIndex=w.tickIndex()+1;input.entityId=w.playerEntityId();w.enqueueInput(input);w.tick();
        check(!w.battleResult(),"future Escape applied early");w.tick();check(w.battleResult().has_value(),"scheduled Escape lost");
    }
    for(bool cockpit:{false,true}){auto p=setup();fatal(p,cockpit);p.fatalConclusionDelaySeconds=2.;auto w=BattleWorld::create(p);
        BattleInputCommand input;input.entityId=w.playerEntityId();input.acknowledgeConclusion=true;
        w.enqueueInput(input);w.tick();check(!w.battleResult() && w.snapshot().conclusionDelay.freezesSimulation,"same-tick Escape overrode fatal priority");
        input.tickIndex=w.tickIndex();w.enqueueInput(input);w.tick();check(!w.battleResult(),"Escape skipped frozen death presentation");
    }
    // Replay changing supplied team aggregates from the original stateful DOS
    // corpus. Host duration2s at0.1s is chosen for20 ticks only in this fixture;
    // it is not evidence of the original wall-clock frame rate.
    size_t sequenceCount=0,sequenceSteps=0;
    {const auto in=bytes("research/fixtures/dosbox_conclusion_sequence/SIN.BIN");
     const auto out=bytes("research/fixtures/dosbox_conclusion_sequence/SOUT.BIN");
     std::map<unsigned,std::vector<size_t>> sequences;
     check(in.size()%32==0 && out.size()==in.size()/32*18,"malformed DOS sequence fixture");
     for(size_t row=0;row<in.size()/32;++row)sequences[word(in,row*32)].push_back(row);
     for(const auto& entry:sequences){
        if(entry.first/72>=5)continue;
        bool selected=true;
        for(auto row:entry.second){const auto p=row*32;
            if(word(in,p+6)!=0 || word(in,p+28)!=1 || word(in,p+30) || word(in,p+24) || word(in,p+26))selected=false;
        }
        if(!selected)continue;
        auto p=setup();p.combatConclusionDelaySeconds=2.;auto w=BattleWorld::create(p);
        const auto actors=w.snapshot().combatants;
        for(auto row:entry.second){const auto ip=row*32,op=row*18;
            for(size_t slot=0;slot<actors.size();++slot)
                w.setMissionStatus(actors[slot].id,word(in,ip+(slot<4?8:10))==65535?
                    CombatantMissionStatus::Destroyed:CombatantMissionStatus::Active);
            w.tick();const auto s=w.snapshot();const auto result=w.battleResult();
            check((result?result->rawResultCode:65535)==word(out,op),"world sequence result differs from original DOS");
            if(!result){const auto flags=word(out,op+14);
                check(s.conclusionDelay.active==(flags!=0),"world lost original result latch");
                if(flags){check(s.conclusionDelay.pendingState==((flags&255)?BattleMissionRuntimeState::Victory:BattleMissionRuntimeState::Defeat),"world changed latched outcome");
                    check(s.conclusionDelay.resolveTickIndex==word(out,op+8)+20u,"world restarted original countdown");}
            }
            ++sequenceSteps;
        }
        ++sequenceCount;
     }
     check(sequenceCount==15 && sequenceSteps==315,"missing DOS team sequence coverage");}
    // Compare the production team result with actual original flags, including
    // simultaneous elimination with a surviving pilot (destroyed is not killed).
    for(int pa:{12,-1})for(int ea:{12,-1}) {
        auto p=setup();auto w=BattleWorld::create(p);const auto s=w.snapshot();
        for(size_t i=0;i<s.combatants.size();++i)if((i<4?pa:ea)==-1)w.setMissionStatus(s.combatants[i].id,CombatantMissionStatus::Destroyed);
        w.tick();const auto r=w.battleResult();const auto dos=dosTeamResult(pa,ea);
        check((r?r->rawResultCode:-1)==dos,"production team outcome differs from original DOS");
    }
    for(bool cockpit:{false,true})for(bool enemiesGone:{false,true})for(bool objectiveWon:{false,true}) {
        auto p=setup();fatal(p,cockpit);
        if(objectiveWon)wonObjective(p);
        auto w=BattleWorld::create(p);const auto s=w.snapshot();
        check(!objectiveWon || s.objective.depleted,"fixture lost simultaneous objective outcome");
        if(enemiesGone)for(size_t i=4;i<8;++i)w.setMissionStatus(s.combatants[i].id,CombatantMissionStatus::Destroyed);
        w.tick();const auto r=w.battleResult();
        check(r && r->state==BattleMissionRuntimeState::Defeat && r->rawResultCode==1 && r->reason=="commander_killed",
            "fatal commander did not override living allies/objective/team victory");
        check(w.snapshot().combatants[1].missionStatus==CombatantMissionStatus::Active,"test lost its surviving ally");
        auto copy=w;const auto before=battleSnapshotFingerprint(w.snapshot());w.tick();copy.runTicks(10);
        check(before==battleSnapshotFingerprint(w.snapshot()) && before==battleSnapshotFingerprint(copy.snapshot()),"terminal world changed on repeat tick/copy");
    }
    {auto p=setup();wonObjective(p);auto w=BattleWorld::create(p);w.tick();
        check(w.battleResult() && w.battleResult()->state==BattleMissionRuntimeState::Victory,"living commander lost objective-win control");}
    for(bool objectiveWin:{false,true}) {
        auto p=setup();wonObjective(p);
        if(!objectiveWin){p.mission=battleMissionBriefingFromDefinition(*battleMissionDefinitionById(2));
            const auto intent=battleMissionObjectiveBriefingById(2);p.objective.missionTargetKind=intent.targetKind;
            p.objective.missionIntent=intent.intent;p.objective.missionIntentProvenance=intent.provenance;}
        auto w=BattleWorld::create(p);const auto s=w.snapshot();
        for(size_t i=0;i<8;++i)if((i<4)==objectiveWin)w.setMissionStatus(s.combatants[i].id,CombatantMissionStatus::Destroyed);
        w.tick();check(w.battleResult() && w.battleResult()->state==BattleMissionRuntimeState::Victory,
            "simultaneous objective/team flags lost original win precedence");
        const auto in=bytes("research/fixtures/dosbox_conclusion_sequence/SIN.BIN");
        const auto out=bytes("research/fixtures/dosbox_conclusion_sequence/SOUT.BIN");
        bool found=false;
        for(size_t row=0;row<in.size()/32;++row)if(static_cast<unsigned>(word(in,row*32))/72u==(objectiveWin?9u:8u) && word(in,row*32+2)==0){
            check(word(out,row*18+14)==257,"original simultaneous objective/team case did not set both flags");found=true;break;}
        check(found,"missing original simultaneous objective/team fixture");
    }
    // Explicit test override retains the loss even with healthy allies.
    {auto p=setup();fatal(p,false);p.fatalConclusionDelaySeconds=.15;auto w=BattleWorld::create(p);
        w.tick();check(!w.battleResult() && w.snapshot().conclusionDelay.pendingState==BattleMissionRuntimeState::Defeat,"fatal presentation delay missing");
        const auto end=w.snapshot().conclusionDelay.resolveTickIndex;
        while(w.tickIndex()<=end)w.tick();check(w.battleResult() && w.battleResult()->reason=="commander_killed","fatal delay lost outcome");}
    // Fatal presentation preserves the entire combat snapshot (including RNG,
    // projectiles, ammunition, heat and pose), not just the commander's damage.
    for(bool cockpit:{false,true}){auto p=setup();fatal(p,cockpit);p.fatalConclusionDelaySeconds=.5;
        auto w=BattleWorld::create(p);w.tick();const auto frozen=w.snapshot();auto replay=w;
        check(frozen.conclusionDelay.freezesSimulation,"fatal presentation did not freeze simulation");
        auto changed=frozen;changed.conclusionDelay.freezesSimulation=false;
        check(battleSnapshotFingerprint(changed)!=battleSnapshotFingerprint(frozen),"freeze state absent from replay fingerprint");
        for(int tick=0;tick<4;++tick){BattleInputCommand input;input.tickIndex=w.tickIndex();input.entityId=frozen.combatants[1].id;
            input.throttle=1;input.fireWeapon=true;input.jumpJetToggle=true;w.enqueueInput(input);w.tick();replay.tick();
            auto current=w.snapshot();check(battleSnapshotFingerprint(current)==battleSnapshotFingerprint(replay.snapshot()),"inputs changed frozen death state");
            current.tickIndex=frozen.tickIndex;current.elapsedMs=frozen.elapsedMs;
            check(battleSnapshotFingerprint(current)==battleSnapshotFingerprint(frozen),"combat/RNG advanced during fatal presentation");}
        w.tick();const auto done=w.snapshot();check(done.result.valid && done.result.reason=="commander_killed","frozen presentation did not finish");
        auto normalized=done;normalized.tickIndex=frozen.tickIndex;normalized.elapsedMs=frozen.elapsedMs;
        normalized.result=frozen.result;normalized.missionRuntimeState=frozen.missionRuntimeState;normalized.conclusionDelay=frozen.conclusionDelay;
        check(battleSnapshotFingerprint(normalized)==battleSnapshotFingerprint(frozen),"fatal finalization changed combat export state");
    }
    {auto p=setup();wonObjective(p);p.combatConclusionDelaySeconds=.3;auto w=BattleWorld::create(p);
        w.tick();const auto start=w.snapshot();check(!w.battleResult() && start.conclusionDelay.active && !start.conclusionDelay.freezesSimulation,"objective skipped normal conclusion window");
        BattleInputCommand drive;drive.tickIndex=w.tickIndex();drive.throttle=1;w.enqueueInput(drive);w.tick();
        check(w.snapshot().combatants[0].transform.z>start.combatants[0].transform.z,"ordinary outcome incorrectly froze battle");
        for(const auto& a:start.combatants)w.setMissionStatus(a.id,CombatantMissionStatus::Destroyed);
        w.runTicks(3);check(w.battleResult() && w.battleResult()->state==BattleMissionRuntimeState::Victory,"late team loss replaced objective flag");}
    {auto p=setup();p.mission=battleMissionBriefingFromDefinition(*battleMissionDefinitionById(11));
        p.objective.valid=p.objective.transformProven=true;p.objective.transform=p.playerStartTransform;
        const auto intent=battleMissionObjectiveBriefingById(11);
        p.objective.missionTargetKind=intent.targetKind;p.objective.missionIntentProvenance=intent.provenance;
        p.objective.missionIntentProven=true;p.objective.missionIntent=intent.intent;
        p.combatConclusionDelaySeconds=.5;auto w=BattleWorld::create(p);const auto start=w.snapshot();
        check(start.retrieval.active,"missing retrieval fixture");
        for(size_t i=0;i<4;++i)w.setMissionStatus(start.combatants[i].id,CombatantMissionStatus::Disabled);
        w.tick();const auto lost=w.snapshot();check(lost.conclusionDelay.pendingState==BattleMissionRuntimeState::Defeat,"missing latched loss before contact");
        w.setMissionStatus(start.combatants[0].id,CombatantMissionStatus::Active);w.tick();const auto contact=w.snapshot();
        check(contact.retrieval.phase==BattleRetrievalPhase::ContactedByPlayer &&
            contact.conclusionDelay.pendingState==BattleMissionRuntimeState::Victory,"contact did not add original direct win flag");
        check(contact.conclusionDelay.startTickIndex==lost.conclusionDelay.startTickIndex &&
            contact.conclusionDelay.resolveTickIndex==lost.conclusionDelay.resolveTickIndex,"contact restarted existing countdown");
        w.runTicks(4);check(w.battleResult() && w.battleResult()->reason=="retrieval_structure_contact" &&
            w.battleResult()->terminalTickIndex==6,"contact failed to win at the original deadline");}
    // All four edges, every allowed-mask combination, with and without fatal
    // pilot damage. Original boundary branch precedes fatal-pilot arbitration.
    for(unsigned edge=0;edge<4;++edge)for(unsigned mask=0;mask<16;++mask)for(bool dead:{false,true}) {
        auto p=setup();auto& b=p.battlefieldBoundary;b.valid=true;b.worldMinX=b.worldMinZ=0;b.worldMaxX=b.worldMaxZ=50000;
        b.originalMinX=b.originalMinZ=0;b.originalMaxX=b.originalMaxZ=50000;
        b.actorExitEncodingProven=b.exitMaskSemanticsProven=b.playerExitOutcomeProven=true;
        b.ordinaryPlayerExitResultCode=2;b.allowedPlayerExitResultCode=0;b.outcomeEvaluationDeferred=false;
        b.playerAllowedExitMask=static_cast<uint8_t>(mask);
        if(edge==0)p.playerStartTransform.z=-1;if(edge==1)p.playerStartTransform.z=50001;
        if(edge==2)p.playerStartTransform.x=-1;if(edge==3)p.playerStartTransform.x=50001;
        if(dead)fatal(p,true);auto w=BattleWorld::create(p);w.tick();const auto r=w.battleResult();const bool allowed=(mask&(1u<<edge))!=0;
        check(r && r->rawResultCode==(allowed?0:2) && r->state==(allowed?BattleMissionRuntimeState::Victory:BattleMissionRuntimeState::Withdraw),"boundary raw code/priority differs");
        check(r->actorExitStatus==-static_cast<int>(edge)-2 && r->exitAllowed==allowed,"boundary mask used wrong table index");
    }
    std::cout<<"mw_battle_conclusion_smoke: ok checks="<<checks<<" DOS_team_cases=4 boundary_cases=128 fatal_cases=8 DOS_sequences="<<sequenceCount<<" DOS_sequence_steps="<<sequenceSteps<<" DOS_keyboard_cases="<<keyboardCases<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
