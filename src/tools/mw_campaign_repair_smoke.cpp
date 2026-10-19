// Original-instruction oracle plus actual campaign repair/Save/Restore handlers.
#include "../main.cpp"
#include <iostream>
namespace {
using namespace mw::battle;
size_t checks=0;
bool compareLiveDosbox=false;
void check(bool ok,const std::string& message) { ++checks; if (!ok) throw std::runtime_error(message); }
OriginalMechCondition fromHex(const std::string& hex) {
    check(hex.size()==58,"bad repair fixture record"); OriginalMechCondition result{};
    for (size_t i=0;i<29;++i) result[i]=static_cast<uint8_t>(std::stoul(hex.substr(i*2,2),nullptr,16));
    return result;
}
void oracle(const OriginalRepairTables& t) {
    std::ifstream file("research/fixtures/original_repairs.txt"); check(bool(file),"missing instruction oracle");
    std::string line; size_t quotes=0,steps=0,factors=0;
    while (std::getline(file,line)) {
        if (line.empty() || line[0]=='#') continue;
        std::istringstream in(line); char kind; in>>kind;
        if (kind=='Q') {
            uint16_t chassis,factor; std::string hex; uint32_t total;
            in>>chassis>>factor>>hex>>total; const auto condition=fromHex(hex);
            check(originalRepairTotalCost(t,chassis,factor,condition)==total,"original total mismatch");
            for (size_t selector=0;selector<11u+t.weaponCounts[chassis];++selector) {
                uint32_t cost; in>>cost;
                check(originalRepairUnitCost(t,chassis,factor,selector,condition)==cost,"original unit mismatch");
            }
            ++quotes;
        } else if (kind=='S') {
            size_t selector; std::string before,after; in>>selector>>before>>after;
            auto condition=fromHex(before); check(applyOriginalRepairStep(condition,selector),"step refused");
            check(condition==fromHex(after),"original mutation mismatch"); ++steps;
        } else if (kind=='G') {
            unsigned tier; uint16_t lo,hi,factor,finalLo,finalHi;
            in>>tier>>lo>>hi>>factor>>finalLo>>finalHi; OriginalContractRng rng{lo,hi};
            check(generateOriginalServiceFactor(static_cast<uint8_t>(tier),rng)==factor,"original factor mismatch");
            check(rng.word3f5==finalLo && rng.word3f7==finalHi,"factor RNG consumption mismatch"); ++factors;
        } else throw std::runtime_error("unknown oracle record");
        check(bool(in),"truncated repair oracle");
    }
    check(quotes==440 && steps==84 && factors==128,"incomplete repair oracle");
    {
        std::ifstream availability("research/fixtures/original_repair_availability.txt");
        check(bool(availability),"missing availability oracle");
        size_t count=0;
        while (std::getline(availability,line)) {
            if (line.empty() || line[0]=='#') continue;
            std::istringstream in(line); char mode; uint16_t chassis,tier,lo,hi,cache;
            size_t selector; uint32_t money,quote,afterMoney; uint16_t finalLo,finalHi,ticks;
            std::string beforeHex,afterHex,cacheHex;
            in>>mode>>chassis>>tier>>selector>>lo>>hi>>cache>>money>>beforeHex>>quote
              >>afterMoney>>afterHex>>cacheHex>>finalLo>>finalHi>>ticks;
            check(bool(in) && cacheHex.size()==440,"bad availability fixture");
            auto condition=fromHex(beforeHex); OriginalRepairSession session;
            session.rng={lo,hi}; session.unavailable.fill(static_cast<uint8_t>(cache));
            const auto cost=mode=='S' ? originalRepairUnitCost(t,chassis,39,selector,condition) :
                originalRepairTotalCost(t,chassis,39,condition);
            check(cost==quote,"availability oracle quote mismatch");
            uint16_t elapsed=0;
            if (mode=='F') {
                if (cost && money>=cost) {
                    money-=cost;
                    applyOriginalFullRepair(t,session,chassis,static_cast<uint8_t>(tier),condition);
                    money+=originalRepairTotalCost(t,chassis,39,condition);
                }
            } else {
                const size_t field=selector>10 ? selector-1 : selector;
                const bool damaged=selector==10 ? std::any_of(condition.begin()+20,condition.end(),[](uint8_t v){return v!=0;}) : condition[field]!=0;
                if (damaged && (selector==10 || originalRepairAvailable(t,session,chassis,static_cast<uint8_t>(tier),field)) && money>=cost) {
                    elapsed=originalRepairStepTicks(0,selector,condition); money-=cost;
                    applyOriginalRepairStep(condition,selector);
                }
            }
            check(condition==fromHex(afterHex) && money==afterMoney && elapsed==ticks,"original availability/payment/mutation mismatch");
            check(session.rng.word3f5==finalLo && session.rng.word3f7==finalHi,"original availability RNG consumption mismatch");
            for (size_t i=0;i<220;++i)
                check(session.unavailable[i]==std::stoul(cacheHex.substr(i*2,2),nullptr,16),"original failure cache mismatch");
            ++count;
        }
        check(count==1440,"incomplete availability oracle");
    }
    check(t.components[0][3]==810 && t.components[1][9]==1600,"WORD prices truncated");
    OriginalMechCondition c{}; c[3]=1;c[4]=1;c[6]=1;
    c[20]=c[21]=c[22]=c[23]=c[24]=1;
    check(originalRepairTotalCost(t,2,42,c)==54306,"DATA6 documented Jenner quote");
    check(originalRepairUnitCost(t,2,42,3,c)==23643,"DATA6 life support quote");
    check(originalRepairUnitCost(t,2,42,6,c)==3663,"DATA7 right actuator quote");
}
}
void runCampaignRepairSmoke() {
    using namespace mw::battle;
    App app(findResourceRoot()); app.loadResources(); check(!app.mwMainData_.empty(),"resources");
    // Existing success-path tests use a real absorbing zero RNG seed. The
    // independent availability oracle below covers nonzero seeds and failures.
    app.repairSession_.rng={0,0};
    if (compareLiveDosbox) {
        // Untouched GAM payloads read from a running DOSBox Staging 0.83.0.
        // Provenance and observed quotes: docs/PHASE15_DOSBOX_LIVE_REPAIR_EVIDENCE.md.
        const auto fixture=fs::path("research/fixtures/dosbox_repair");
        const auto load=[&](const char* name) {
            std::ifstream in(fixture/name,std::ios::binary);
            check(bool(in),"missing live DOS fixture");
            std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
            check(bytes.size()==0x718,"invalid live GAM size"); return bytes;
        };
        const auto loadSession=[&](const char* label) {
            std::ifstream in(fixture/(std::string(label)+"-before.session.txt"));
            std::string hex; in>>app.repairSession_.rng.word3f5>>app.repairSession_.rng.word3f7>>hex;
            check(bool(in) && hex.size()==440,"missing live unsaved repair context");
            for (size_t i=0;i<220;++i)
                app.repairSession_.unavailable[i]=static_cast<uint8_t>(std::stoul(hex.substr(i*2,2),nullptr,16));
        };
        const auto before=load("sensor-before.gam"), after=load("sensor-after.gam");
        app.applyGamSaveToRuntime(before);
        loadSession("sensor");
        check(app.ownedMechs_.size()==1,"live owned mech import");
        check(app.serviceFactor_==39 && app.playerWealth_==1159000,"live factor/wealth import");
        app.selectedMechIndex_=0;
        const auto targets=app.repairableTargets(app.ownedMechs_[0]);
        const auto found=std::find_if(targets.begin(),targets.end(),[](const App::RepairTarget& t) {
            return App::repairSelector(t)==2; // Sensors, selected in the DOS menu.
        });
        check(found!=targets.end(),"live sensors missing from repair menu");
        app.repairSelectionIndex_=static_cast<size_t>(found-targets.begin());
        const auto unit=app.repairTargetCost(app.ownedMechs_[0],*found);
        const auto total=app.totalRepairCost(app.ownedMechs_[0]);
        check(unit==32301 && total==81272,"live original quote mismatch");
        const auto ammo=app.ownedMechs_[0].ammoPacks;
        app.repairSelectedMechTarget();
        auto actual=before; app.writeRuntimeToGamSave(actual);
        check(app.playerWealth_==1126699,"live original payment mismatch");
        check(std::equal(actual.begin()+0x102,actual.begin()+0x11f,after.begin()+0x102),
            "live original 29-byte repaired condition mismatch");
        check(app.ownedMechs_[0].ammoPacks==ammo,"live repair changed ammunition");
        check(before[0x31]==2 && after[0x31]==3,"live original one-tick observation changed");
        // The originally observed calendar divergence is now a parity assertion.
        const int replacementTicks=actual[0x31]-before[0x31];
        check(replacementTicks==1 && actual[0x37]==after[0x37],"live repair calendar mismatch");
        const auto out=fs::path("outputs/dosbox_audit"); fs::create_directories(out);
        std::ofstream gam(out/"replacement-sensor-after.gam",std::ios::binary);
        gam.write(reinterpret_cast<const char*>(actual.data()),static_cast<std::streamsize>(actual.size()));
        check(bool(gam),"write replacement comparison GAM");
        std::ofstream report(out/"comparison.json");
        report<<"{\n  \"original_unit_quote\": 32301,\n  \"replacement_unit_quote\": "<<unit
              <<",\n  \"original_total_quote\": 81272,\n  \"replacement_total_quote\": "<<total
              <<",\n  \"original_wealth_after\": 1126699,\n  \"replacement_wealth_after\": "<<app.playerWealth_
              <<",\n  \"condition_record_equal\": true,\n  \"original_half_day_ticks\": 1,\n  \"replacement_half_day_ticks\": "
              <<replacementTicks<<",\n  \"scope\": \"one successful sensor repair; no visual or full campaign parity claim\"\n}\n";
        check(bool(report),"write live comparison report");
        std::cout<<"DOSBox live fixture: unit="<<unit<<" total="<<total
                 <<" condition=match; calendar ticks original=1 replacement="<<replacementTicks<<'\n';
        // Captured unsaved cache/RNG context is supplied explicitly. DOS dialog
        // waits consume RNG, so final global RNG words are not a parity claim.
        struct LiveCase { const char* name; size_t selector; uint32_t quote; };
        const std::array<LiveCase,7> cases={{{"armor-light",10,6000}, {"armor-junk",10,6000},
            {"right-leg",8,6327}, {"weapon-ra",12,20979},
            {"weapon-la",15,20979}, {"full",21,60285}, {"fresh-full",21,31976}}};
        std::ofstream extended(out/"extended-comparison.json"); extended<<"{\n  \"cases\": [\n";
        size_t differences=0;
        for (size_t i=0;i<cases.size();++i) {
            const auto& item=cases[i];
            const auto input=load((std::string(item.name)+"-before.gam").c_str());
            const auto original=load((std::string(item.name)+"-after.gam").c_str());
            app.applyGamSaveToRuntime(input); loadSession(item.name); app.selectedMechIndex_=0;
            const auto candidates=app.repairableTargets(app.ownedMechs_[0]);
            if (item.selector==21) {
                check(app.totalRepairCost(app.ownedMechs_[0])==item.quote,"live full quote mismatch");
                app.repairAllSelectedMech();
            } else {
                const auto target=std::find_if(candidates.begin(),candidates.end(),[&](const App::RepairTarget& t) {
                    return App::repairSelector(t)==item.selector;
                });
                check(target!=candidates.end(),"extended live target absent");
                check(app.repairTargetCost(app.ownedMechs_[0],*target)==item.quote,"extended live quote mismatch");
                app.repairSelectionIndex_=static_cast<size_t>(target-candidates.begin());
                app.repairSelectedMechTarget();
            }
            auto replacement=input; app.writeRuntimeToGamSave(replacement);
            const auto originalMoney=App::readGamU32Le(original,App::kGamOffsetMoney);
            const bool conditionMatches=std::equal(replacement.begin()+0x102,replacement.begin()+0x11f,original.begin()+0x102);
            const bool dateMatches=replacement[0x31]==original[0x31] && replacement[0x33]==original[0x33] &&
                App::readGamU16Le(replacement,0x35)==App::readGamU16Le(original,0x35) && replacement[0x37]==original[0x37];
            const bool matches=conditionMatches && dateMatches && app.playerWealth_==originalMoney;
            check(matches,std::string("live repair differs: ")+item.name);
            if (!matches) ++differences;
            extended<<"    {\"case\": \""<<item.name<<"\", \"parity\": "<<(matches ? "true" : "false")
                <<", \"original_money\": "<<originalMoney<<", \"replacement_money\": "<<app.playerWealth_
                <<", \"original_ticks\": "<<int(original[0x31]-input[0x31])
                <<", \"replacement_ticks\": "<<int(replacement[0x31]-input[0x31])<<"}"<<(i+1<cases.size() ? "," : "")<<'\n';
            std::cout<<"DOSBox "<<item.name<<": "<<(matches ? "MATCH" : "DIFF")<<'\n';
        }
        extended<<"  ],\n  \"unresolved_cases\": "<<differences<<"\n}\n";
        check(bool(extended),"write extended live comparison");
        return;
    }
    oracle(app.repairTables_);
    // Execute independent instruction-derived calendar cases through the real
    // campaign calendar. UI, board contents and DOS wage/desertion policy are
    // outside this date-arithmetic oracle; source boundaries are in the ledger.
    {
        std::ifstream file("research/fixtures/original_repair_time.txt");
        check(bool(file),"missing original repair time oracle");
        std::string line; size_t count=0, elapsedCases=0;
        while (std::getline(file,line)) {
            if (line.empty() || line[0]=='#') continue;
            std::istringstream in(line); size_t slot, selector;
            std::string hex, afterHex; unsigned money, unit, cache;
            int day, month, year, period;
            int ax, cx, nextDay, nextMonth, nextYear, nextPeriod; unsigned nextMoney;
            in>>slot>>selector>>hex>>money>>unit>>day>>month>>year>>period>>cache
              >>ax>>cx>>nextDay>>nextMonth>>nextYear>>nextPeriod>>nextMoney>>afterHex;
            check(bool(in),"truncated original repair time fixture");
            auto condition=fromHex(hex);
            if (ax>=0) {
                const auto ticks=originalRepairStepTicks(slot,selector,condition);
                check(ticks==ax,"original repair AX time mismatch");
                app.currentMonthDayCounter_=static_cast<int>(day);
                app.currentMonth_=static_cast<int>(month); app.currentYear_=static_cast<int>(year);
                app.currentPeriodic14DayCounter_=static_cast<int>(period);
                app.advanceCampaignDays(ticks);
                check(app.currentMonthDayCounter_==nextDay && app.currentMonth_==nextMonth &&
                    app.currentYear_==nextYear && app.currentPeriodic14DayCounter_==nextPeriod,
                    "original repair calendar date/period mismatch");
                check(applyOriginalRepairStep(condition,selector) && condition==fromHex(afterHex),
                    "calendar repair mutation mismatch");
                ++elapsedCases;
            } else {
                check(hex==afterHex && money==nextMoney && day==nextDay && month==nextMonth &&
                    year==nextYear && period==nextPeriod,"original refusal advanced calendar or charged");
            }
            ++count;
        }
        check(count==2016 && elapsedCases>0,"incomplete original repair time oracle");
    }
    app.serviceFactor_=42;
    for (size_t chassis=0;chassis<10;++chassis) {
        const auto id=static_cast<App::ChassisId>(chassis);
        for (bool exact : {false,true}) {
            const auto preset=App::battleMechPresetForChassis(id);
            if (exact && !preset) continue;
            for (size_t selector=0;selector<11u+app.repairTables_.weaponCounts[chassis];++selector) {
                auto mech=App::makeMech(id);
                OriginalMechCondition record{};
                const size_t field=selector==10 ? 20 : selector<10 ? selector : selector-1;
                record[field]=3;
                if (selector==4) record[field]=static_cast<uint8_t>(mech.heatSinksTotal);
                if (selector==9) record[field]=static_cast<uint8_t>(mech.jumpJetsTotal);
                if (!record[field]) continue;
                App::readGamMechRecord(std::vector<uint8_t>(record.begin(),record.end()),0,mech);
                if (exact) {
                    BattleStartParams p; p.playerMechPresetId=*preset;
                    p.deterministicCombatRuntimeEnabled=true; p.individualWeaponRuntimeEnabled=true;
                    BattleCombatantLaunchState launch; launch.mechPresetId=*preset;
                    launch.roster.team=BattleTeam::Player; launch.roster.sourceSlot="player:0";
                    launch.persistentMechState=App::battlePersistentMechStateFromOwnedMech(mech,"repair_test");
                    p.playerLaunchState=launch;
                    const auto c=BattleWorld::create(p).snapshot().combatants.at(0);
                    App::applyBattlePersistentMechStateToOwnedMech(mech,campaignMechStateFromCombatant(c));
                }
                app.ownedMechs_={mech}; app.selectedMechIndex_=0; app.repairSelectionIndex_=0;
                app.finalizeLoadedMech(app.ownedMechs_[0]);
                const auto total=app.totalRepairCost(app.ownedMechs_[0]);
                app.playerWealth_=100000000;
                const auto initialWealth=app.playerWealth_;
                const auto ammo=mech.ammoPacks;
                size_t iterations=0;
                while (!app.repairableTargets(app.ownedMechs_[0]).empty()) {
                    check(++iterations<=20,"repair step failed to converge");
                    auto expected=App::repairCondition(app.ownedMechs_[0]);
                    const auto target=app.repairableTargets(app.ownedMechs_[0]).at(0);
                    const auto cost=app.repairTargetCost(app.ownedMechs_[0],target);
                    applyOriginalRepairStep(expected,App::repairSelector(target));
                    const auto wealth=app.playerWealth_;
                    const auto oldExact=app.ownedMechs_[0].exactDamage;
                    app.repairSelectedMechTarget();
                    check(App::repairCondition(app.ownedMechs_[0])==expected,"handler did not take exactly one step");
                    check(app.playerWealth_==wealth-cost,"handler payment mismatch");
                    check(app.ownedMechs_[0].ammoPacks==ammo,"repair changed ammunition");
                    if (oldExact && selector!=10) {
                        check(app.ownedMechs_[0].exactDamage.has_value(),"step discarded exact state");
                        const auto& now=app.ownedMechs_[0].exactDamage->detailed;
                        for (size_t i=0;i<9;++i)
                            check(now.armorSections[i].armorRemaining==oldExact->detailed.armorSections[i].armorRemaining,"step touched armor points");
                    }
                }
                check(initialWealth-app.playerWealth_==total,"sum of successful steps differs from full quote");
                const auto guard=app.captureCampaignPersistentState().fingerprint;
                app.repairSelectedMechTarget(); app.repairAllSelectedMech();
                check(app.captureCampaignPersistentState().fingerprint==guard,"pristine repair charged or mutated");
            }
        }
    }
    // End-to-end handler ordering at calendar boundaries; no shared helper is
    // used to derive the literal expected elapsed ticks in these scenarios.
    for (size_t slot : {size_t(0),size_t(9)}) {
        for (size_t selector : {size_t(2),size_t(4),size_t(10)}) {
            App boundary(findResourceRoot()); boundary.loadResources();
            auto mech=App::makeMech(App::ChassisId::Jenner);
            OriginalMechCondition record{};
            record[selector==10 ? 23 : selector]=selector==4 ? 5 : 3;
            if (selector==10) record[28]=1;
            App::readGamMechRecord(std::vector<uint8_t>(record.begin(),record.end()),0,mech);
            boundary.ownedMechs_.assign(slot+1,App::makeMech(App::ChassisId::Jenner));
            boundary.ownedMechs_[slot]=mech; boundary.selectedMechIndex_=slot;
            boundary.repairSelectionIndex_=0; boundary.serviceFactor_=39;
            boundary.playerWealth_=10000000;
            boundary.repairSession_.rng={0,0};
            for (auto& member : boundary.crewMembers_) member.hired=false;
            boundary.currentMonthDayCounter_=59; boundary.currentMonth_=11;
            boundary.currentYear_=3024; boundary.currentPeriodic14DayCounter_=13;
            const int elapsed=(selector==4 ? 5 : 3)+(slot==9 && selector!=10 ? 256 : 0);
            const auto cost=boundary.repairTargetCost(mech,boundary.repairableTargets(mech).front());
            boundary.repairSelectedMechTarget();
            check(boundary.currentMonthDayCounter_==(59+elapsed)%60 &&
                boundary.currentMonth_==((11+(59+elapsed)/60)%12) && boundary.currentYear_==3025 &&
                boundary.currentPeriodic14DayCounter_==(13+elapsed)%14,"handler calendar boundary");
            check(boundary.playerWealth_==10000000-cost,"boundary payment");
            record[selector==10 ? 23 : selector]=selector==10 ? 0 : record[selector]-1;
            check(App::repairCondition(boundary.ownedMechs_[slot])==record,"boundary mutation");
        }
    }
    {
        App payroll(findResourceRoot()); payroll.loadResources();
        auto mech=App::makeMech(App::ChassisId::Jenner); mech.sensors=App::DamageState::LightDamage;
        payroll.ownedMechs_={mech}; payroll.selectedMechIndex_=0; payroll.repairSelectionIndex_=0;
        payroll.serviceFactor_=39;
        payroll.repairSession_.rng={0,0};
        payroll.recruitPilots_[0].unavailable=0;
        check(payroll.hireRecruit(0),"payroll fixture cannot hire Elli");
        payroll.currentMonthDayCounter_=59; payroll.currentMonth_=11; payroll.currentYear_=3024;
        payroll.currentPeriodic14DayCounter_=13; payroll.playerWealth_=32306;
        payroll.repairSelectedMechTarget();
        check(payroll.ownedMechs_[0].sensors==App::DamageState::Functional && payroll.playerWealth_==5 &&
            payroll.crewGamState_.unpaidCrewPayroll==400 && payroll.crewPayrollNotice_==mw::battle::CrewPayrollNotice::Warning,
            "repair must pay before original payroll shortage warning, retaining the remaining money");
        check(payroll.currentYear_==3025 && payroll.currentMonth_==0 && payroll.currentMonthDayCounter_==0,
            "repair did not apply year rollover");
    }
    // The real UI consumes refusal dismissal without repeating the repair.
    {
        App ui(findResourceRoot()); ui.loadResources();
        auto mech=App::makeMech(App::ChassisId::Jenner);
        mech.sensors=App::DamageState::LightDamage;
        auto other=App::makeMech(App::ChassisId::Battlemaster);
        other.sensors=App::DamageState::LightDamage;
        ui.ownedMechs_={mech,mech,other}; ui.playerWealth_=10000000;
        ui.selectedMechIndex_=0; ui.repairSelectionIndex_=0;
        ui.changeState(ScreenState::MechRepairStatus);
        ui.repairSession_.unavailable[App::gamChassisId(mech.chassis)*22+2]=1;
        ui.repairSession_.rng={0,0};
        const auto unchanged=ui.captureCampaignPersistentState().fingerprint;
        ui.handleKey(VK_RETURN);
        check(ui.repairNotice_==L"HAVE TO WAIT FOR PARTS" &&
            ui.captureCampaignPersistentState().fingerprint==unchanged,"cached refusal UI transaction");
        ui.render(); const auto pixels=ui.framebuffer_; ui.render();
        check(ui.framebuffer_==pixels && ui.repairSession_.rng.word3f5==0 &&
            ui.repairSession_.rng.word3f7==0,"refusal rendering consumed RNG");
        const auto capture=fs::path("outputs/repair_availability_audit/refusal.ppm");
        fs::create_directories(capture.parent_path());
        std::ofstream image(capture,std::ios::binary); image<<"P6\n320 200\n255\n";
        for (auto pixel:pixels) { const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)}; image.write(rgb,3); }
        check(bool(image),"refusal UI capture");
        ui.handleKey(VK_RETURN);
        check(ui.repairNotice_.empty() && ui.captureCampaignPersistentState().fingerprint==unchanged,
            "modal dismissal repeated repair");
        ui.selectedMechIndex_=1; ui.handleKey(VK_RETURN);
        check(!ui.repairNotice_.empty() && ui.ownedMechs_[1].sensors==App::DamageState::LightDamage,
            "cache must be shared by chassis");
        ui.handleMouseClick(1,1,false);
        check(ui.repairNotice_.empty(),"mouse failed to dismiss refusal");
        ui.selectedMechIndex_=2; ui.handleKey(VK_RETURN);
        check(ui.ownedMechs_[2].sensors==App::DamageState::Functional,"cache leaked to different chassis");
        const auto cache=ui.repairSession_.unavailable;
        for (auto screen:{ScreenState::MechStatus,ScreenState::MechReviewList,ScreenState::MechLabMenu}) {
            ui.changeState(screen); check(ui.repairSession_.unavailable==cache,"submenu reset availability");
        }
        ui.advanceCampaignDays(14);
        check(ui.repairSession_.unavailable==cache,"calendar reset availability");
        ui.repairSession_.rng={132,1};
        ui.changeState(ScreenState::MainMenu);
        check(ui.repairSession_.unavailable==std::array<uint8_t,220>{} &&
            ui.repairSession_.rng.word3f5==132 && ui.repairSession_.rng.word3f7==1,
            "planet return must clear cache without reseeding");
        ui.selectedMechIndex_=0; ui.repairSelectionIndex_=0; ui.playerWealth_=0;
        ui.repairSession_.rng={1004,1}; // Original next R(100) is 100: all tiers refuse.
        const auto beforeFunds=ui.captureCampaignPersistentState().fingerprint;
        ui.repairAllSelectedMech();
        check(ui.repairSession_.rng.word3f5==1004 && ui.repairSession_.rng.word3f7==1 &&
            ui.repairSession_.unavailable==std::array<uint8_t,220>{},"unaffordable bulk consumed availability");
        ui.repairSelectedMechTarget();
        check(ui.repairSession_.unavailable[App::gamChassisId(mech.chassis)*22+2]==1 &&
            (ui.repairSession_.rng.word3f5!=1004 || ui.repairSession_.rng.word3f7!=1) &&
            ui.captureCampaignPersistentState().fingerprint==beforeFunds,
            "selected availability must precede funds check without charge or time");
        ui.initializeRepairSession();
        check(ui.repairSession_.unavailable==std::array<uint8_t,220>{},"new campaign retained failed parts");
    }
    // With guaranteed availability, insufficient funds leave persistent state unchanged.
    auto damaged=App::makeMech(App::ChassisId::Battlemaster);
    damaged.engine=App::DamageState::Junk; damaged.weapons[0].condition=App::DamageState::HeavyDamage;
    damaged.armorDamage[0]=3; damaged.armorDamage[8]=1; damaged.heatSinksWorking-=3;
    BattleStartParams mixedParams; mixedParams.playerMechPresetId="battlemaster";
    mixedParams.deterministicCombatRuntimeEnabled=true; mixedParams.individualWeaponRuntimeEnabled=true;
    BattleCombatantLaunchState mixedLaunch; mixedLaunch.mechPresetId="battlemaster";
    mixedLaunch.roster.team=BattleTeam::Player; mixedLaunch.roster.sourceSlot="player:0";
    mixedLaunch.persistentMechState=App::battlePersistentMechStateFromOwnedMech(damaged,"mixed_repair_test");
    mixedParams.playerLaunchState=mixedLaunch;
    App::applyBattlePersistentMechStateToOwnedMech(damaged,
        campaignMechStateFromCombatant(BattleWorld::create(mixedParams).snapshot().combatants.at(0)));
    {
        App partial(findResourceRoot()); partial.loadResources(); partial.serviceFactor_=42;
        partial.ownedMechs_={damaged,damaged}; partial.selectedMechIndex_=0;
        for (auto& mech:partial.ownedMechs_) partial.finalizeLoadedMech(mech);
        partial.playerWealth_=10000000; partial.repairSession_.rng={0,0};
        const auto cacheBase=App::gamChassisId(damaged.chassis)*22;
        partial.repairSession_.unavailable[cacheBase]=1;
        partial.repairSession_.unavailable[cacheBase+10]=1;
        const auto prior=*partial.ownedMechs_[0].exactDamage;
        const auto ammo=partial.ownedMechs_[0].ammoPacks;
        const auto otherCondition=App::repairCondition(partial.ownedMechs_[1]);
        const auto otherExact=persistentMechDamageFingerprint(*partial.ownedMechs_[1].exactDamage);
        const auto date=std::array<int,4>{partial.currentMonthDayCounter_,partial.currentMonth_,
            partial.currentYear_,partial.currentPeriodic14DayCounter_};
        const auto quoted=partial.totalRepairCost(partial.ownedMechs_[0]);
        partial.repairAllSelectedMech();
        const auto residual=partial.totalRepairCost(partial.ownedMechs_[0]);
        check(residual>0 && residual<quoted && partial.playerWealth_==10000000-quoted+residual,
            "partial full repair residual refund");
        auto expected=App::repairCondition(damaged);
        for (size_t field=0;field<expected.size();++field) if (field!=0 && field!=10) expected[field]=0;
        check(App::repairCondition(partial.ownedMechs_[0])==expected,"partial full paid fields");
        check(partial.ownedMechs_[0].exactDamage.has_value(),"partial full discarded exact damage");
        const auto after=*partial.ownedMechs_[0].exactDamage;
        auto isolated=after;
        isolated.detailed.criticalComponents[0]=prior.detailed.criticalComponents[0];
        isolated.detailed.installedWeapons[0]=prior.detailed.installedWeapons[0];
        check(persistentMechDamageFingerprint(isolated)==persistentMechDamageFingerprint(after),
            "unavailable exact critical/weapon was changed");
        check(partial.ownedMechs_[0].ammoPacks==ammo &&
            App::repairCondition(partial.ownedMechs_[1])==otherCondition &&
            persistentMechDamageFingerprint(*partial.ownedMechs_[1].exactDamage)==otherExact,
            "partial full changed ammo or nonparticipant");
        check(date==std::array<int,4>{partial.currentMonthDayCounter_,partial.currentMonth_,
            partial.currentYear_,partial.currentPeriodic14DayCounter_},"full repair advanced calendar");
        check(partial.repairNotice_==L"NOT ALL PARTS WERE AVAILABLE.","partial full result text");
        partial.state_=ScreenState::MechStatus; partial.render();
        const auto capture=fs::path("outputs/repair_availability_audit/partial-full.ppm");
        fs::create_directories(capture.parent_path());
        std::ofstream image(capture,std::ios::binary); image<<"P6\n320 200\n255\n";
        for (auto pixel:partial.framebuffer_) { const char rgb[]={char(pixel>>16),char(pixel>>8),char(pixel)}; image.write(rgb,3); }
        check(bool(image),"partial UI capture");
        const auto cache=partial.repairSession_.unavailable;
        const auto path=fs::path("outputs/repair_availability_audit/PARTIAL.GAM");
        check(partial.saveCurrentGame(path),"partial full save");
        check(partial.repairSession_.unavailable==cache && partial.repairSession_.rng.word3f5==0 &&
            partial.repairSession_.rng.word3f7==0,"saving changed repair session");
        partial.loadGameFromFile(path);
        check(partial.ownedMechs_[0].exactDamage &&
            persistentMechDamageFingerprint(*partial.ownedMechs_[0].exactDamage)==persistentMechDamageFingerprint(after) &&
            partial.totalRepairCost(partial.ownedMechs_[0])==residual && fs::file_size(path)==1816,
            "partial full GAM/companion round trip");
        check(partial.repairSession_.unavailable==std::array<uint8_t,220>{},"load serialized stale failure cache");
    }
    app.ownedMechs_={damaged}; app.finalizeLoadedMech(app.ownedMechs_[0]); app.playerWealth_=0;
    auto guard=app.captureCampaignPersistentState().fingerprint;
    app.repairSelectedMechTarget(); app.repairAllSelectedMech();
    check(app.captureCampaignPersistentState().fingerprint==guard,"unaffordable repair mutated state");
    const auto total=app.totalRepairCost(app.ownedMechs_[0]); app.playerWealth_=total;
    app.repairAllSelectedMech(); check(app.playerWealth_==0 && app.totalRepairCost(app.ownedMechs_[0])==0,"full payment/reset");
    // Existing GAM word, including original zero, survives import/export unchanged.
    auto gam=app.baseGamSaveData(); app.writeRuntimeToGamSave(gam);
    for (uint16_t factor : {uint16_t(0),uint16_t(18),uint16_t(42),uint16_t(79)}) {
        App::writeU16Le(gam,0x2f,factor); app.applyGamSaveToRuntime(gam);
        check(app.serviceFactor_==factor,"GAM factor import");
        auto saved=gam; app.writeRuntimeToGamSave(saved);
        check(saved.size()==0x718 && App::readGamU16Le(saved,0x2f)==factor,"GAM factor export");
    }
    app.serviceFactor_=42; app.generateCurrentMechMarket();
    app.advanceCampaignDays(14); check(app.serviceFactor_==42,"market refresh rerolled factor");
    app.pendingTravelPlanetIndex_=0; app.pendingTravelDays_=0; app.pendingTravelCost_=0; app.completeTravel();
    const auto tier=app.currentPlanetEconomyTier();
    check(app.serviceFactor_>=tier*18 && app.serviceFactor_<=tier*18+7,"arrival factor range");
    // Partial repair retains its remaining bill across explicit GAM + companion save.
    app.ownedMechs_={damaged}; app.finalizeLoadedMech(app.ownedMechs_[0]); app.playerWealth_=10000000;
    app.repairSession_.rng={0,0};
    app.repairSession_.unavailable.fill(0);
    const auto beforeStep=*app.ownedMechs_[0].exactDamage;
    app.repairSelectedMechTarget();
    const auto afterStep=*app.ownedMechs_[0].exactDamage;
    check(app.ownedMechs_[0].engine==App::DamageState::HeavyDamage,"junk engine was fully repaired");
    auto untouched=afterStep;
    untouched.detailed.criticalComponents[0]=beforeStep.detailed.criticalComponents[0];
    untouched.mechDestroyed=beforeStep.mechDestroyed; untouched.mechDisabled=beforeStep.mechDisabled;
    check(persistentMechDamageFingerprint(untouched)==persistentMechDamageFingerprint(beforeStep),
        "paid engine step changed another damaged field");
    const auto output=fs::path("outputs/repair_audit/PARTIAL.GAM"); fs::create_directories(output.parent_path());
    check(app.saveCurrentGame(output),"partial repair save");
    App restored(findResourceRoot()); restored.loadResources(); restored.loadGameFromFile(output);
    check(restored.ownedMechs_[0].exactDamage &&
        persistentMechDamageFingerprint(*restored.ownedMechs_[0].exactDamage)==persistentMechDamageFingerprint(afterStep),
        "partial repair exact-state companion round trip");
    check(restored.currentMonthDayCounter_==app.currentMonthDayCounter_ && restored.currentMonth_==app.currentMonth_ &&
        restored.currentYear_==app.currentYear_ && restored.currentPeriodic14DayCounter_==app.currentPeriodic14DayCounter_,
        "repair calendar save/restore");
    check(restored.serviceFactor_==app.serviceFactor_ && App::repairCondition(restored.ownedMechs_[0])==App::repairCondition(app.ownedMechs_[0]) &&
        restored.ownedMechs_[0].repairCost==app.ownedMechs_[0].repairCost,"partial repair restore/quote");
}
int main(int argc,char** argv) { try {
    if (argc==2 && std::string(argv[1])=="--dosbox-compare") compareLiveDosbox=true;
    else if (argc!=1) throw std::runtime_error("usage: mw_campaign_repair_smoke [--dosbox-compare]");
    runCampaignRepairSmoke(); std::cout<<"campaign repair: "<<checks<<" checks passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; } }
