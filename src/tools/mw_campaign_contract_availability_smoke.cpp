// Tests real campaign handlers in memory; no window or user save is changed.
#include "../main.cpp"
#include <iostream>
#include <set>

namespace {
using namespace mw::battle;
size_t checks = 0;
void check(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    bool rejected = false;
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected, message);
}
std::vector<uint8_t> bytes(const OriginalContractTables& t, const OriginalContractBoard& b) {
    std::vector<uint8_t> result(0x718, 0);
    encodeOriginalContractBoard(result, t, b);
    return result;
}
OriginalContractBoard generate(const OriginalContractTables& t, const ContractGenerationContext& c, uint32_t seed) {
    OriginalContractRng r{static_cast<uint16_t>(seed), static_cast<uint16_t>(seed >> 16)};
    return generateOriginalContractBoard(t, c, r);
}
void instructionOracleChecks(const OriginalContractTables& t) {
    std::ifstream file("research/fixtures/original_contract_boards.txt");
    check(static_cast<bool>(file), "missing original instruction fixtures");
    std::string line;
    size_t row = 0;
    while (std::getline(file,line)) {
        if (line.empty() || line[0] == '#') continue;
        ++row;
        std::istringstream input(line);
        unsigned house, crew, owned, chassis, reputation, lo, hi, count, threshold, finalLo, finalHi;
        ContractGenerationContext c;
        input >> house >> c.year >> c.month >> c.dayCounter >> crew >> owned >> chassis >> reputation
            >> c.positiveStanding >> c.negativeStanding >> lo >> hi >> count >> threshold >> finalLo >> finalHi;
        check(static_cast<bool>(input), "malformed original instruction fixture");
        c.employerHouse = static_cast<uint8_t>(house);
        c.crewCount = static_cast<uint16_t>(crew); c.ownedMechCount = static_cast<uint16_t>(owned);
        c.forceScore = mwMainContractForceScore(static_cast<uint16_t>(reputation),
            static_cast<uint16_t>(std::min(crew,owned) * mwMainContractMechStrength(chassis)));
        OriginalContractRng rng{static_cast<uint16_t>(lo),static_cast<uint16_t>(hi)};
        const auto b = generateOriginalContractBoard(t,c,rng);
        if (b.offers.size() != count || b.classFourThreshold != threshold ||
            rng.word3f5 != finalLo || rng.word3f7 != finalHi)
            throw std::runtime_error("original instruction count/phase/RNG mismatch at case " + std::to_string(row));
        const auto encoded = bytes(t,b);
        for (size_t i = 0; i < count; ++i) for (size_t field = 0; field < 14; ++field) {
            unsigned expected = 0; input >> expected;
            const size_t offset = (field == 13 ? 0x5e3 : 0x502 + field * 12) + i * 2;
            const unsigned actual = encoded[offset] | (encoded[offset+1] << 8);
            if (!input || actual != expected)
                throw std::runtime_error("original instruction offer mismatch at case " + std::to_string(row) +
                    " slot " + std::to_string(i) + " field " + std::to_string(field));
            ++checks;
        }
    }
    check(row == 200, "incomplete original instruction fixtures");
}
void pureChecks(const std::vector<uint8_t>& exe, const OriginalContractTables& t, const fs::path& root) {
    instructionOracleChecks(t);
    OriginalContractRng rng{0x1234, 0x5678};
    const uint16_t maximums[] = {100,4,10,9,12,100,28,7};
    const uint16_t values[] = {32,1,6,4,5,10,5,6};
    const uint16_t states[] = {0x531f,0x44f3,0x97a1,0x7648,0x699f,0x1bd0,0x301c,0xd104};
    for (size_t i = 0; i < 8; ++i) {
        check(rng.inclusive(maximums[i]) == values[i], "8086 inclusive RNG sequence");
        check(rng.word3f7 == states[i], "8086 RNG state");
    }
    check(originalContractCalendarPolicy(3028,7,39).classFourThreshold == 0, "war starts too soon");
    check(originalContractCalendarPolicy(3028,7,40).guaranteedOffers, "August 21 war start missed");
    check(originalContractCalendarPolicy(3029,11,59).classFourThreshold == 50, "war end too soon");
    check(!originalContractCalendarPolicy(3030,0,0).guaranteedOffers &&
        originalContractCalendarPolicy(3030,0,0).classFourThreshold == 75, "post-war policy");
    check(t.classBuckets[1].size() == 10 && t.classBuckets[2].size() == 4 &&
        t.classBuckets[3].size() == 15 && t.classBuckets[4].size() == 5, "original class buckets");
    check(t.missions[26].id == 27 && t.missions[27].id == 30 && t.missions[33].id == 36, "descriptor ID gap lost");
    check(t.targetStarts == std::array<uint16_t,5>{{0,30,60,90,115}} &&
        t.targetMaximums == std::array<uint16_t,5>{{30,30,30,25,30}}, "inclusive target ranges");
    for (const auto start : t.targetStarts) check(t.targetRatings[start] == 0, "capital target rating");

    std::set<size_t> seenMissions;
    std::set<size_t> seenEarlyCounts, seenWarCounts, seenLateCounts;
    bool extended = false, highExtendedCeiling = false, inaccessibleTarget = false;
    const auto planets = loadPlanetRecords(root / L"MW_MAIN.EXE");
    for (uint8_t house = 0; house < 5; ++house) {
        for (int phase = 0; phase < 3; ++phase) {
            ContractGenerationContext c;
            c.employerHouse = house;
            c.year = phase == 0 ? 3024 : phase == 1 ? 3029 : 3030;
            c.forceScore = 150; c.crewCount = c.ownedMechCount = 4;
            for (uint32_t seed = 1; seed <= 512; ++seed) {
                const uint32_t mixed = seed * 0x9e3779b9u;
                const auto b = generate(t,c,mixed);
                check(b.status != ContractBoardStatus::Invalid, "valid original board rejected");
                (phase == 0 ? seenEarlyCounts : phase == 1 ? seenWarCounts : seenLateCounts).insert(b.offers.size());
                check(phase == 1 ? b.offers.size() >= 3 && b.offers.size() <= 6 :
                    b.offers.empty() || (b.offers.size() >= 2 && b.offers.size() <= 6), "offer count outside original bounds");
                std::set<size_t> unique;
                for (const auto& o : b.offers) {
                    check(unique.insert(o.missionIndex).second, "duplicate mission on board");
                    seenMissions.insert(o.missionIndex);
                    check(t.categoryClasses[house][o.category] != 0, "forbidden target category");
                    if (o.missionClass == 1 || o.category >= 5) {
                        const auto& row = t.directTargets[house][o.category];
                        check(std::find(row.begin(),row.end(),o.targetPlanetIndex+1u) != row.end(), "wrong direct target");
                    } else {
                        check(o.targetPlanetIndex >= t.targetStarts[o.category] &&
                            o.targetPlanetIndex <= t.targetStarts[o.category] + t.targetMaximums[o.category] &&
                            t.targetRatings[o.targetPlanetIndex] >= o.missionClass, "wrong target rating/range");
                    }
                    for (const auto& p : planets) if (p.tableOrder == o.targetPlanetIndex + 1u && !p.contractAvailableFlag)
                        inaccessibleTarget = true;
                    if (t.missions[o.missionIndex].extended) {
                        extended = true;
                        highExtendedCeiling |= o.priceCeiling > 9990;
                        check(o.price == (o.priceCeiling / 4 / 10)*10, "extended opening payment");
                        for (const auto n : o.opposition) check(n % 3 == 0, "extended opposition not tripled");
                    }
                }
                if (seed < 10) {
                    check(bytes(t,b) == bytes(t,generate(t,c,mixed)), "generation not deterministic");
                    check(bytes(t,b) == bytes(t,decodeOriginalContractBoard(bytes(t,b),t)), "board roundtrip changed");
                }
            }
        }
    }
    check(seenMissions.size() == 34 && extended && highExtendedCeiling && inaccessibleTarget, "missing mission/extended/target coverage");
    check(seenEarlyCounts == std::set<size_t>{0,2,3,4,5,6} && seenWarCounts == std::set<size_t>{3,4,5,6} &&
        seenLateCounts == seenEarlyCounts, "count endpoint or empty probability unreachable");
    for (uint16_t crew : std::array<uint16_t,2>{{1,2}}) for (uint16_t mechs : std::array<uint16_t,2>{{1,2}}) {
        bool seenExtended = false;
        ContractGenerationContext c; c.year = 3029; c.crewCount = crew; c.ownedMechCount = mechs;
        for (uint32_t seed = 1; seed < 200; ++seed) {
            const auto b = generate(t,c,seed * 0x85ebca6bu);
            check(b.status == ContractBoardStatus::Ready, "extended fallback rejected board");
            for (const auto& o : b.offers) seenExtended |= t.missions[o.missionIndex].extended;
        }
        check(seenExtended == (crew >= 2 && mechs >= 2), "extended crew/mech eligibility");
    }
    ContractGenerationContext c; c.year = 3029; c.negativeStanding = 8;
    check(generate(t,c,123).status == ContractBoardStatus::HouseRefused, "House refusal threshold");
    c.negativeStanding = 7;
    check(generate(t,c,123).status == ContractBoardStatus::Ready, "House refuses too early");
    check(generate(t,c,0).status == ContractBoardStatus::Invalid, "zero RNG stream not bounded");
    auto malformed = t; for (auto& row : malformed.categoryClasses) row.fill(0);
    check(generate(malformed,c,123).status == ContractBoardStatus::Invalid, "empty categories must fail closed");
    auto badExe = exe; badExe[0x8d00 + 0xa02] = 5;
    rejects([&]{decodeOriginalContractTables(badExe);}, "malformed class table accepted");

    for (const auto name : {L"234.GAM", L"FIGHTTST.GAM", L"CREW5.GAM", L"CREW6.GAM"}) {
        const auto original = readFile(root / name);
        const auto board = decodeOriginalContractBoard(original,t);
        auto rewritten = original; encodeOriginalContractBoard(rewritten,t,board);
        check(rewritten == original, "original GAM board roundtrip changed bytes");
    }
    auto saved = readFile(root / L"234.GAM");
    saved[0x500] = 7; rejects([&]{decodeOriginalContractBoard(saved,t);}, "oversize saved board accepted");
    saved = readFile(root / L"234.GAM"); saved[0x50e] = 255;
    rejects([&]{decodeOriginalContractBoard(saved,t);}, "unknown saved target accepted");
    saved = readFile(root / L"234.GAM"); saved[0x5e3] = 28;
    rejects([&]{decodeOriginalContractBoard(saved,t);}, "mismatched descriptor/ID accepted");
    saved = readFile(root / L"234.GAM");
    const auto preserved = saved; OriginalContractBoard empty;
    empty.classFourThreshold = saved[0x70a]; encodeOriginalContractBoard(saved,t,empty);
    for (size_t i = 0; i < saved.size(); ++i) if (i != 0x500 && i != 0x501)
        check(saved[i] == preserved[i], "empty board overwrote stale slots or unrelated GAM bytes");
}
}

void runCampaignContractAvailabilitySmoke() {
    App app(findResourceRoot()); app.loadResources();
    check(!app.planets_.empty() && app.status_.find(L"failed") == std::wstring::npos, "campaign resources failed");
    const auto tables = decodeOriginalContractTables(app.mwMainData_);
    pureChecks(app.mwMainData_,tables,app.resourceRoot_);
    app.currentYear_ = 3029; app.currentMonth_ = 0; app.currentMonthDayCounter_ = 0;
    app.currentPeriodic14DayCounter_ = 0;
    // Every planet: office accessibility is independent from its target rating.
    for (size_t i = 0; i < app.planets_.size(); ++i) {
        app.currentPlanetIndex_ = static_cast<int>(i);
        check(app.currentPlanetContractsAvailable() == (app.planets_[i].contractAvailableFlag != 0), "planet office availability");
        app.generateCurrentPlanetContracts();
        const auto before = bytes(tables,app.originalContractBoardForSave(tables));
        for (int repeat = 0; repeat < 3; ++repeat) {
            app.openContractNegotiation(); app.renderContractNegotiation();
            check(bytes(tables,app.originalContractBoardForSave(tables)) == before, "Request/render rerolled board");
        }
        check(app.contractNegotiationUnavailable_ == !app.currentPlanetContractsAvailable(), "office menu gate differs");
        for (const auto& o : app.activeContracts_)
            check(o.targetPlanetIndex >= 0 && !o.targetPlanet.empty() && o.employerHouse == app.planets_[i].houseId,
                "live target/employer mapping");
    }
    app.currentPlanetIndex_ = 0;
    app.generateCurrentPlanetContracts();
    const auto initial = bytes(tables,app.originalContractBoardForSave(tables));
    app.advanceCampaignDays(13);
    check(bytes(tables,app.originalContractBoardForSave(tables)) == initial, "board refreshed before 14 ticks");
    app.terminateContractNegotiations(); app.openContractNegotiation();
    check(app.activeContracts_.empty() && app.contractNegotiationUnavailable_, "rejected negotiation rerolled on Request");
    app.advanceCampaignDays(1); app.openContractNegotiation();
    check(!app.activeContracts_.empty() && !app.contractNegotiationUnavailable_ && app.currentPeriodic14DayCounter_ == 0,
        "calendar refresh did not lift rejection");
    const auto afterRefresh = bytes(tables,app.originalContractBoardForSave(tables));
    app.advanceCampaignDays(0);
    check(bytes(tables,app.originalContractBoardForSave(tables)) == afterRefresh, "zero time changed board");
    app.terminateContractNegotiations();
    app.pendingTravelPlanetIndex_ = 31; app.pendingTravelDays_ = 0; app.pendingTravelCost_ = 0;
    app.completeTravel();
    check(!app.activeContracts_.empty() && app.currentPlanetIndex_ == 31, "arrival did not refresh contracts");
    for (const auto& o : app.activeContracts_) check(o.employerHouse == app.currentPlanetHouseId(), "arrival generated for old House");
    app.houseNegativeCounters_[app.currentPlanetHouseId()] = 8;
    const auto boardBeforeRefusal = bytes(tables,app.originalContractBoardForSave(tables));
    app.openContractNegotiation(); app.renderContractNegotiation();
    check(app.contractNegotiationUnavailable_ && app.currentContractHouseRefuses(), "House refusal not displayed");
    check(bytes(tables,app.originalContractBoardForSave(tables)) == boardBeforeRefusal, "refusal view mutated board");
    app.houseNegativeCounters_[app.currentPlanetHouseId()] = 0;

    // Original GAM import, explicit save representation, repeated menus, and next refresh.
    const auto original = readFile(app.resourceRoot_ / L"234.GAM");
    app.applyGamSaveToRuntime(original);
    const auto expected = bytes(tables,decodeOriginalContractBoard(original,tables));
    check(bytes(tables,app.originalContractBoardForSave(tables)) == expected, "live original board import differs");
    // Raw entry 146 is a second Ander's Moon with different map coordinates.
    // It has rating zero and is absent from all direct target tables. Never
    // silently reinterpret it as the earlier, same-named UI planet.
    check(app.planetIndexFromTableOrder(146) == -1, "duplicate-name raw target silently remapped");
    auto unrepresented = original;
    unrepresented[0x50e] = 145; unrepresented[0x50f] = 0;
    const auto beforeInvalidImport = app.captureCampaignPersistentState();
    rejects([&]{app.applyGamSaveToRuntime(unrepresented);}, "unrepresented raw target accepted");
    check(app.captureCampaignPersistentState().fingerprint == beforeInvalidImport.fingerprint,
        "invalid target import partially mutated campaign");
    auto saved = original; app.writeRuntimeToGamSave(saved);
    check(bytes(tables,decodeOriginalContractBoard(saved,tables)) == expected, "live save lost original offers");
    App restored(findResourceRoot()); restored.loadResources(); restored.applyGamSaveToRuntime(saved);
    restored.openContractNegotiation();
    check(bytes(tables,restored.originalContractBoardForSave(tables)) == expected, "restored Request rerolled offers");
    restored.repairSession_.rng=app.repairSession_.rng; // Save does not contain the process RNG.
    app.advanceCampaignDays(14); restored.advanceCampaignDays(14);
    check(bytes(tables,restored.originalContractBoardForSave(tables)) == bytes(tables,app.originalContractBoardForSave(tables)),
        "future generation depends on unsaved visit serial");

    // Production result transaction: board refresh is rolled back on verification failure.
    app.currentYear_ = 3029; app.generateCurrentPlanetContracts();
    app.acceptedBattleOutcome_.launchSource = CampaignBattleLaunchSource::AcceptedContract;
    app.acceptedBattleConsequencePlan_.valid = app.acceptedBattleConsequencePlan_.commitEligible = true;
    app.acceptedBattleConsequencePlan_.planFingerprint = 0x4211;
    app.acceptedBattleConsequencePlan_.campaignDateTicks = 14;
    app.acceptedBattleConsequencePlan_.payment = 1000;
    app.acceptedBattleStateGuard_.valid = app.acceptedBattleStateGuard_.unchanged = true;
    app.terminateContractNegotiations();
    // An impossible persisted ammo count forces real save-roundtrip verification to fail.
    const auto oldAmmo = app.ownedMechs_[0].ammoPacks;
    app.ownedMechs_[0].ammoPacks[0] = 100000;
    app.acceptedBattleStateGuard_.after = app.captureCampaignPersistentState();
    const auto beforeRollback = app.captureCampaignPersistentState();
    const auto failed = app.commitAcceptedBattleConsequencePlan();
    check(!failed.committed && app.captureCampaignPersistentState().fingerprint == beforeRollback.fingerprint &&
        app.activeContracts_.empty(), "failed battle result did not roll back contract refresh");
    app.ownedMechs_[0].ammoPacks = oldAmmo;
    app.acceptedBattleStateGuard_.after = app.captureCampaignPersistentState();
    const auto committed = app.commitAcceptedBattleConsequencePlan();
    check(committed.committed && !app.activeContracts_.empty(), "successful mission did not refresh contracts");
    const auto afterCommit = app.captureCampaignPersistentState();
    check(app.commitAcceptedBattleConsequencePlan().duplicateRejected &&
        app.captureCampaignPersistentState().fingerprint == afterCommit.fingerprint, "duplicate result rerolled board");
    app.terminateContractNegotiations();
    app.acceptedContract_.priceK = 100; app.acceptedContract_.originalMissionId = 1;
    app.resolveUneventfulGarrisonDuty();
    check(!app.activeContracts_.empty(), "uneventful garrison did not refresh board");
    std::cout << "mw_campaign_contract_availability_smoke: ok checks=" << checks
        << " planets=" << app.planets_.size() << " missions=34 original_gam=4 menu_stable=yes calendar_travel_result=yes\n";
}

int main() {
    try { runCampaignContractAvailabilitySmoke(); return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
