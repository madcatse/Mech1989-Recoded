// Exercises original instruction fixtures and the real campaign/UI/save handlers.
// Normal CTest is RAM-only; --visual writes isolated audit captures, never saves.
#include "../main.cpp"
#include <iostream>
#include <set>

namespace {
using namespace mw::battle;
size_t checks = 0;
bool visual = false;
void check(bool value, const std::string& reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}
template<class F> void rejects(F action, const char* reason) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, reason);
}
void capture(const char* name, const std::vector<uint32_t>& pixels) {
    if (!visual) return;
    fs::create_directories("outputs/recruitment_audit");
    std::ofstream out(std::string("outputs/recruitment_audit/")+name+".ppm", std::ios::binary);
    out << "P6\n320 200\n255\n";
    for (auto pixel : pixels) {
        const char rgb[] = {static_cast<char>(pixel >> 16), static_cast<char>(pixel >> 8), static_cast<char>(pixel)};
        out.write(rgb, 3);
    }
}
std::vector<uint8_t> encoded(const OriginalRecruitmentState& state) {
    std::vector<uint8_t> gam(0x718);
    encodeOriginalRecruitment(gam, state);
    return gam;
}
bool isRecruitByte(size_t p) {
    return (p >= 0x77 && p <= 0xa0) || (p >= 0xd6 && p <= 0xe7) ||
        (p >= 0x5f0 && p <= 0x619) || (p >= 0x61b && p <= 0x644) || (p >= 0x646 && p <= 0x66f);
}
void oracleChecks(const OriginalRecruitmentState& initial) {
    std::ifstream fixture("research/fixtures/original_recruitment.txt");
    check(static_cast<bool>(fixture), "missing original recruitment fixtures");
    std::string line;
    size_t rows = 0, limitedRows = 0;
    while (std::getline(fixture, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream input(line);
        unsigned tier, reputation, profile, crewCount, lo, hi, finalLo, finalHi, count, limited;
        std::array<uint16_t,3> ids{};
        OriginalRecruitBoard expected{};
        input >> tier >> reputation >> profile >> crewCount >> ids[0] >> ids[1] >> ids[2]
            >> lo >> hi >> finalLo >> finalHi >> count;
        for (auto& id : expected) input >> id;
        input >> limited;
        check(static_cast<bool>(input), "malformed recruitment fixture");
        auto pilots = initial.pilots;
        for (size_t i = 0; i < pilots.size(); ++i) {
            if (profile == 1) {
                const auto old = pilots[i].gunnery;
                pilots[i].gunnery = static_cast<uint8_t>(std::min(3, old + (i % 3 == 0 ? 1 : 0)));
                if (old != pilots[i].gunnery) pilots[i].piloting = pilots[i].gunnery;
                pilots[i].experience = static_cast<uint8_t>(i*7 % 16);
                pilots[i].unavailable = i % 7 == 0 ? 1 : 0;
            } else if (profile == 2) pilots[i].unavailable = 1;
        }
        const std::vector<uint16_t> crew(ids.begin(), ids.begin()+crewCount);
        for (auto id : crew) pilots[id-1].unavailable = 1;
        OriginalContractRng rng{static_cast<uint16_t>(lo),static_cast<uint16_t>(hi)};
        bool actualLimited = false;
        const auto actual = generateOriginalRecruitBoard(pilots, static_cast<uint8_t>(tier),
            static_cast<uint8_t>(reputation), crew, rng, &actualLimited);
        const auto context = " at original case " + std::to_string(++rows);
        check(actual == expected, "original candidate order differs"+context);
        check(originalRecruitBoardCount(actual) == count, "original candidate count differs"+context);
        check(rng.word3f5 == finalLo && rng.word3f7 == finalHi, "original RNG differs"+context);
        check(actualLimited == (limited != 0), "safety limit differs"+context);
        limitedRows += limited != 0 ? 1 : 0;
        std::set<unsigned> portraits;
        for (auto id : crew) portraits.insert((id-1)/2);
        for (auto id : actual) if (id) {
            check(id <= 42 && pilots[id-1].unavailable == 0, "unavailable candidate selected");
            check(portraits.insert((id-1)/2).second, "repeated portrait in board or hired crew");
            check(pilots[id-1].gunnery == reputation || pilots[id-1].gunnery+1u == reputation,
                "candidate violates reputation gate");
        }
    }
    check(rows == 1552, "incomplete original recruitment fixtures");
    std::cout << "original cases=" << rows << " safety_limited=" << limitedRows << '\n';
}
}

void runCampaignRecruitmentSmoke() {
    App app(findResourceRoot());
    app.loadResources();
    auto initial = app.originalRecruitmentState();
    oracleChecks(initial);
    // Catalog gate is asymmetric, independent of piloting and player gunnery.
    const size_t eligibleCounts[] = {7,20,27,22};
    for (unsigned r = 0; r < 4; ++r) {
        size_t eligible = 0;
        for (const auto& p : initial.pilots) eligible += p.gunnery == r || p.gunnery+1u == r ? 1 : 0;
        check(eligible == eligibleCounts[r], "original eligible catalog size");
    }
    OriginalContractRng rng{0x1234,0xabcd};
    rejects([&]{generateOriginalRecruitBoard(initial.pilots,0,0,{},rng);}, "invalid tier accepted");
    rejects([&]{generateOriginalRecruitBoard(initial.pilots,1,4,{},rng);}, "invalid reputation accepted");
    rejects([&]{generateOriginalRecruitBoard(initial.pilots,1,0,{43},rng);}, "invalid companion accepted");
    auto broken = initial; broken.pilots[0].gunnery = 4;
    rejects([&]{encoded(broken);}, "invalid skill encoded");
    broken = initial; broken.board[0] = 43;
    rejects([&]{encoded(broken);}, "invalid board encoded");
    auto badGam = encoded(initial); badGam[0xd8] = 43;
    const auto beforeInvalid = app.captureCampaignPersistentState().fingerprint;
    rejects([&]{app.applyGamSaveToRuntime(badGam);}, "invalid recruit save accepted");
    check(app.captureCampaignPersistentState().fingerprint == beforeInvalid, "invalid save mutated campaign");
    std::vector<uint8_t> sentinels(0x718);
    for (size_t i = 0; i < sentinels.size(); ++i) sentinels[i] = static_cast<uint8_t>(i*17+3);
    auto saved = sentinels;
    initial.board = {1,0,5,0,21,0,0,0};
    encodeOriginalRecruitment(saved,initial);
    for (size_t i = 0; i < saved.size(); ++i)
        if (!isRecruitByte(i)) check(saved[i] == sentinels[i], "recruit codec touched unrelated GAM byte");
    check(encoded(decodeOriginalRecruitment(saved)) == encoded(initial), "recruit codec roundtrip");
    saved[0xd6] = 7; // legacy replacement count, not a request to reroll
    check(decodeOriginalRecruitment(saved).board == initial.board, "count repair rerolled candidates");

    // Every world uses original planet category; neither population nor menu rendering chooses pilots.
    for (size_t planet = 0; planet < app.planets_.size(); ++planet) {
        app.currentPlanetIndex_ = static_cast<int>(planet);
        const auto beforeRng=app.repairSession_.rng;
        app.generateCurrentRecruitBoard();
        check(originalRecruitBoardCount(app.recruitBoard_) <= 10u-2u*app.currentPlanetEconomyTier(), "planet count bound");
        auto board = app.recruitBoard_;
        app.repairSession_.rng=beforeRng;
        app.generateCurrentRecruitBoard();
        check(app.recruitBoard_ == board, "same campaign inputs and shared RNG state differ");
    }
    app.changeState(ScreenState::BarMenu);
    app.recruitBoard_ = {1,0,5,0,21,0,0,0};
    app.currentPeriodic14DayCounter_ = 0; app.currentMonthDayCounter_ = 0;
    const auto wealth = app.playerWealth_;
    app.openRecruitDialog();
    check(app.activeRecruitIndex_ == 0 && app.currentMonthDayCounter_ == 1, "offer order/time cost");
    auto board = app.recruitBoard_;
    app.render(); capture("offer",app.framebuffer_); app.render();
    check(app.recruitBoard_ == board && app.currentMonthDayCounter_ == 1, "render changes recruitment");
    app.handleBarMenuClick(101,113); // NO
    check(app.recruitBoard_[0] == 0 && app.hiredCrewCount() == 1, "mouse decline did not consume only current offer");
    app.openRecruitDialog();
    check(app.activeRecruitIndex_ == 4, "board hole changed next candidate");
    app.recruitChoiceIndex_ = 0; app.handleKey(VK_RETURN);
    check(app.recruitPilots_[4].unavailable == 1 && app.hiredCrewCount() == 2 && app.recruitBoard_[2] == 0,
        "keyboard hire/availability/consumption");
    check(app.playerWealth_ == wealth, "hire charged upfront money");
    check(!app.hireRecruit(4) && !app.hireRecruit(5) && !app.hireRecruit(42), "duplicate/portrait/invalid direct hire");
    app.crewMembers_[1].missionExperience = 2;
    app.improveCrewMemberAfterMission(app.crewMembers_[1],false);
    check(app.recruitPilots_[4].gunnerySkill == 1 && app.crewMembers_[1].wage == 400,
        "promotion did not retain original hire wage");
    auto promotedSave = app.baseGamSaveData(); app.writeRuntimeToGamSave(promotedSave);
    check(App::readGamU16Le(promotedSave,0xa4) == 0 && App::readGamU16Le(promotedSave,0xac) == 1,
        "hire wage rank was confused with promoted live gunnery");
    app.applyGamSaveToRuntime(promotedSave);
    check(app.crewMembers_[1].wage == 400 && app.recruitPilots_[4].gunnerySkill == 1,
        "promoted hire wage changed after load");
    app.dismissCrewSlot(1);
    check(app.recruitPilots_[4].unavailable == 0 && app.recruitPilots_[4].gunnerySkill == 1,
        "dismissal lost skills or failed to release availability");
    check(app.hireRecruit(4) && app.crewMembers_[1].wage == 600, "rehire did not use improved skills/wage");
    check(app.hireRecruit(0) && app.hireRecruit(20), "could not fill three companion slots");
    app.changeState(ScreenState::BarMenu); app.recruitBoard_ = {23,0,0,0,0,0,0,0};
    app.openRecruitDialog(); app.recruitChoiceIndex_ = 0; app.activateRecruitChoice();
    check(app.barDialogState_ == BarDialogState::CrewFull && app.recruitBoard_[0] == 0 &&
        app.recruitPilots_[22].unavailable == 0, "full roster offer consumption/availability");
    app.render(); capture("crew_full",app.framebuffer_);
    app.closeBarDialog(); app.openRecruitDialog();
    const auto date = app.currentMonthDayCounter_;
    check(app.barDialogState_ == BarDialogState::NoCandidates, "empty board message");
    app.render(); capture("empty",app.framebuffer_); app.closeBarDialog(); app.openRecruitDialog();
    check(app.currentMonthDayCounter_ == date, "empty board interview charged time");

    // A promoted but dismissed pilot, absent unavailable pilot, holes and hire wages survive a fresh App.
    app.dismissCrewSlot(1);
    app.recruitPilots_[40].unavailable = 1;
    app.recruitPilots_[4].missionExperience = 9;
    app.recruitBoard_ = {0,5,0,23,0,0,0,0};
    auto gam = app.baseGamSaveData(); app.writeRuntimeToGamSave(gam);
    const auto expected = encoded(app.originalRecruitmentState());
    App restored(findResourceRoot()); restored.loadResources(); restored.applyGamSaveToRuntime(gam);
    check(encoded(restored.originalRecruitmentState()) == expected, "fresh App rerolled/lost pilot save state");
    auto again = gam; restored.writeRuntimeToGamSave(again);
    for (size_t i = 0; i < gam.size(); ++i) if (isRecruitByte(i)) check(gam[i] == again[i], "original recruit field roundtrip");
    check(gam.size() == 0x718, "changed original GAM size");
    check(restored.recruitPilots_[4].gunnerySkill == 1 && restored.recruitPilots_[4].missionExperience == 9 &&
        restored.recruitPilots_[40].unavailable == 1, "inactive pilot state lost");
    const auto fingerprint = restored.captureCampaignPersistentState().fingerprint;
    restored.recruitBoard_[1] = 0;
    check(restored.captureCampaignPersistentState().fingerprint != fingerprint, "board absent from fingerprint");
    restored.recruitBoard_[1] = 5; restored.recruitPilots_[40].unavailable = 0;
    check(restored.captureCampaignPersistentState().fingerprint != fingerprint, "availability absent from fingerprint");
    restored.applyGamSaveToRuntime(gam);
    app.currentPeriodic14DayCounter_ = restored.currentPeriodic14DayCounter_ = 13;
    restored.repairSession_.rng=app.repairSession_.rng; // Unsaved process state is supplied explicitly.
    app.advanceCampaignDays(1); restored.advanceCampaignDays(1);
    check(app.recruitBoard_ == restored.recruitBoard_, "refresh after save/load differs");
    board = restored.recruitBoard_;
    restored.advanceCampaignDays(1);
    check(restored.recruitBoard_ == board, "board refreshed before calendar threshold");

    // Interview crossing weekly boundary keeps the selected old pilot and consumes the original slot.
    restored.resetCrewRoster();
    for (auto& p : restored.recruitPilots_) p.unavailable = 0;
    restored.changeState(ScreenState::BarMenu);
    restored.recruitBoard_ = {0,0,1,0,0,0,0,0}; restored.currentPeriodic14DayCounter_ = 13;
    restored.openRecruitDialog();
    check(restored.activeRecruitIndex_ == 0 && restored.activeRecruitBoardSlot_ == 2 &&
        restored.currentPeriodic14DayCounter_ == 0, "weekly interview ordering");
    restored.activateRecruitChoice();
    check(restored.crewMembers_[1].recruitIndex == 0 && restored.recruitBoard_[2] == 0, "weekly offer commit consumed wrong slot");
    // Legacy zero arrays/flags and stale count recover without filling empty slots.
    auto legacy = gam;
    std::fill(legacy.begin()+0x77,legacy.begin()+0xa1,uint8_t{0});
    std::fill(legacy.begin()+0x5f0,legacy.begin()+0x61a,uint8_t{0});
    std::fill(legacy.begin()+0x61b,legacy.begin()+0x645,uint8_t{0});
    std::fill(legacy.begin()+0xd8,legacy.begin()+0xe8,uint8_t{0}); legacy[0xd6] = 7;
    restored.applyGamSaveToRuntime(legacy);
    check(originalRecruitBoardCount(restored.recruitBoard_) == 0 && restored.recruitPilots_[41].gunnerySkill == 3,
        "legacy skill repair or empty board preservation");
    for (size_t c = 1; c < restored.crewMembers_.size(); ++c)
        if (restored.crewMembers_[c].hired)
            check(restored.recruitPilots_[restored.crewMembers_[c].recruitIndex].unavailable == 1, "legacy active hire flag missing");

    // Actual travel and transaction handlers, including rollback and duplicate result protection.
    restored.recruitBoard_ = {};
    restored.pendingTravelPlanetIndex_ = (restored.currentPlanetIndex_+1)%static_cast<int>(restored.planets_.size());
    restored.pendingTravelDays_ = 1; restored.pendingTravelCost_ = 0;
    restored.completeTravel();
    board = restored.recruitBoard_;
    check(originalRecruitBoardCount(board) > 0, "arrival did not refresh recruitment");
    restored.recruitBoard_ = {};
    restored.currentYear_ = 3029;
    restored.acceptedBattleOutcome_.launchSource = CampaignBattleLaunchSource::AcceptedContract;
    restored.acceptedBattleConsequencePlan_.valid = restored.acceptedBattleConsequencePlan_.commitEligible = true;
    restored.acceptedBattleConsequencePlan_.planFingerprint = 0x524543;
    restored.acceptedBattleConsequencePlan_.campaignDateTicks = 14;
    restored.acceptedBattleConsequencePlan_.payment = 1000;
    restored.acceptedBattleStateGuard_.valid = restored.acceptedBattleStateGuard_.unchanged = true;
    const auto ammo = restored.ownedMechs_[0].ammoPacks;
    restored.ownedMechs_[0].ammoPacks[0] = 100000;
    restored.acceptedBattleStateGuard_.after = restored.captureCampaignPersistentState();
    const auto beforeRollback = encoded(restored.originalRecruitmentState());
    check(!restored.commitAcceptedBattleConsequencePlan().committed &&
        encoded(restored.originalRecruitmentState()) == beforeRollback, "failed mission commit did not roll back recruits");
    restored.ownedMechs_[0].ammoPacks = ammo;
    restored.acceptedBattleStateGuard_.after = restored.captureCampaignPersistentState();
    check(restored.commitAcceptedBattleConsequencePlan().committed && originalRecruitBoardCount(restored.recruitBoard_) > 0,
        "mission result did not refresh recruitment");
    const auto afterCommit = encoded(restored.originalRecruitmentState());
    check(restored.commitAcceptedBattleConsequencePlan().duplicateRejected && encoded(restored.originalRecruitmentState()) == afterCommit,
        "duplicate result rerolled recruits");
    restored.recruitBoard_ = {};
    restored.acceptedContract_.priceK = 100; restored.acceptedContract_.originalMissionId = 1;
    restored.resolveUneventfulGarrisonDuty();
    check(originalRecruitBoardCount(restored.recruitBoard_) > 0, "garrison result did not refresh recruits");
    std::cout << "mw_campaign_recruitment_smoke: ok checks=" << checks << " planets=" << app.planets_.size()
        << " hire_decline_full_dismiss_save_calendar_travel_result=yes\n";
}

int main(int argc, char** argv) {
    visual = argc > 1 && std::string(argv[1]) == "--visual";
    try { runCampaignRecruitmentSmoke(); return 0; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
