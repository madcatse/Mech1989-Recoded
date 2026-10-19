#include "battle/campaign_contracts.h"
#include "battle/battle_opposition.h"

#include <algorithm>
#include <stdexcept>

namespace mw::battle {
namespace {
uint16_t word(const std::vector<uint8_t>& bytes, size_t p) {
    return static_cast<uint16_t>(bytes.at(p) | (bytes.at(p + 1) << 8));
}
void put(std::vector<uint8_t>& bytes, size_t p, uint16_t value) {
    bytes.at(p) = static_cast<uint8_t>(value);
    bytes.at(p + 1) = static_cast<uint8_t>(value >> 8);
}
constexpr size_t kData = 0x8d00;
constexpr size_t kRetryBudget = 65536;
bool validOffer(const OriginalContractTables& t, const OriginalContractOffer& o) {
    return o.missionIndex < t.missions.size() && o.targetPlanetIndex < t.targetRatings.size() &&
        o.category < 8 && o.missionClass == t.missions[o.missionIndex].missionClass &&
        o.opposition[0] + o.opposition[1] + o.opposition[2] <= 12 &&
        o.salvage <= 100 && o.advance <= 100 && o.salvageCeiling <= 100 && o.advanceCeiling <= 100;
}
}

uint16_t OriginalContractRng::inclusive(uint16_t maximum) {
    uint16_t ax = word3f7, bx = word3f5;
    bool carry = false;
    for (int i = 0; i < 2; ++i) {
        const bool next = (ax & 1u) != 0;
        ax = static_cast<uint16_t>((ax >> 1) | (carry ? 0x8000u : 0u));
        carry = next;
    }
    for (int i = 0; i < 5; ++i) {
        const bool next = (bx & 0x8000u) != 0;
        bx = static_cast<uint16_t>((bx << 1) | (carry ? 1u : 0u));
        carry = next;
    }
    word3f5 = bx;
    word3f7 = ax ^ bx;
    return static_cast<uint16_t>((uint32_t{word3f7} * static_cast<uint16_t>(maximum + 1u)) >> 16);
}

OriginalContractTables decodeOriginalContractTables(const std::vector<uint8_t>& exe) {
    OriginalContractTables t;
    for (size_t h = 0; h < 5; ++h) {
        const size_t direct = kData + word(exe, kData + 0x8e90 + h * 2);
        bool any = false;
        for (size_t c = 0; c < 8; ++c) {
            const auto limit = exe.at(kData + 0xa02 + h * 8 + c);
            if (limit > 4) throw std::runtime_error("invalid original contract class table");
            t.categoryClasses[h][c] = limit;
            any |= limit != 0;
            for (size_t i = 0; i < 8; ++i) {
                const auto target = exe.at(direct + c * 8 + i);
                if (limit != 0 && (target == 0 || target > 146))
                    throw std::runtime_error("invalid original contract direct target");
                t.directTargets[h][c][i] = target;
            }
        }
        if (!any) throw std::runtime_error("empty original contract category table");
        t.targetStarts[h] = word(exe, kData + 0x655c + h * 2);
        t.targetMaximums[h] = word(exe, kData + 0x6566 + h * 2);
        if (t.targetStarts[h] + t.targetMaximums[h] >= 146)
            throw std::runtime_error("invalid original contract planet range");
    }
    for (size_t i = 0; i < 146; ++i)
        t.targetRatings[i] = exe.at(kData + word(exe, kData + 0x6570 + i * 2) + 3);
    size_t n = 0;
    // The title catalog is ordered by class 1,2,4,3 in the executable.
    for (size_t c : {1u, 2u, 4u, 3u}) {
        const size_t bucket = kData + word(exe, kData + 0x8a1b + c * 2);
        const size_t count = exe.at(kData + 0x8a16 + c) + 1u;
        for (size_t i = 0; i < count; ++i) {
            if (n >= t.missions.size()) throw std::runtime_error("oversize original mission table");
            auto& m = t.missions[n];
            m.descriptor = word(exe, bucket + i * 2);
            m.id = exe.at(kData + m.descriptor);
            m.missionClass = static_cast<uint8_t>(c);
            m.extended = m.id == 15 || m.id == 16 || m.id == 18;
            if (m.id == 0 || std::any_of(t.missions.begin(), t.missions.begin() + n,
                    [&](const auto& prior) { return prior.id == m.id; }))
                throw std::runtime_error("invalid original mission descriptor");
            t.classBuckets[c].push_back(n++);
        }
    }
    if (n != t.missions.size()) throw std::runtime_error("incomplete original mission table");
    return t;
}

ContractCalendarPolicy originalContractCalendarPolicy(int year, int month, int dayCounter) {
    if (year >= 3030) return {75, false};
    if (year > 3028 || (year == 3028 && (month > 7 || (month == 7 && dayCounter >= 40))))
        return {50, true};
    return {};
}

OriginalContractBoard generateOriginalContractBoard(
    const OriginalContractTables& t, const ContractGenerationContext& c, OriginalContractRng& rng) {
    OriginalContractBoard board;
    const auto policy = originalContractCalendarPolicy(c.year, c.month, c.dayCounter);
    board.classFourThreshold = policy.classFourThreshold;
    const auto invalid = [&]() {
        board.offers.clear(); board.status = ContractBoardStatus::Invalid; return board;
    };
    if (c.employerHouse >= 5 || c.forceScore < 0 || c.forceScore > 150 ||
        (rng.word3f5 == 0 && rng.word3f7 == 0)) return invalid();
    if (!policy.guaranteedOffers && rng.inclusive(100) <= 3) return board;
    const size_t count = policy.guaranteedOffers ? rng.inclusive(3) + 3u : rng.inclusive(4) + 2u;
    if (static_cast<uint16_t>(c.negativeStanding) >= 8) {
        board.status = ContractBoardStatus::HouseRefused; return board;
    }
    for (size_t slot = 0; slot < count; ++slot) {
        OriginalContractOffer o;
        int score = c.forceScore;
        if (slot % 3 == 1) score += score / 2;
        if (slot % 3 == 2) score /= 2;
        auto opposition = mwMainContractOppositionCounts(score, 0);
        if (opposition[0] == 4) opposition = mwMainContractOppositionCounts(score,
            static_cast<uint8_t>(rng.inclusive(10)));
        std::copy(opposition.begin(), opposition.end(), o.opposition.begin());
        o.priceCeiling = static_cast<uint16_t>(mwMainContractPriceCeilingK(score, c.employerHouse, rng.inclusive(9)));
        o.price = static_cast<uint16_t>(mwMainContractOpeningPriceK(o.priceCeiling));
        const auto terms = mwMainContractTerms(c.employerHouse, c.positiveStanding, c.negativeStanding);
        o.salvage = static_cast<uint16_t>(terms.salvageOpeningPercent);
        o.advance = static_cast<uint16_t>(terms.advanceOpeningPercent);
        o.salvageCeiling = static_cast<uint16_t>(terms.salvageCeilingPercent);
        o.advanceCeiling = static_cast<uint16_t>(terms.advanceCeilingPercent);
        size_t budget = kRetryBudget;
        bool chosen = false;
        while (!chosen && budget > 0) {
            unsigned steps = rng.inclusive(10) + 1u;
            size_t category = 0;
            do {
                if (--budget == 0) return invalid();
                if (t.categoryClasses[c.employerHouse][category] && --steps == 0) break;
                category = (category + 1u) % 8u;
            } while (true);
            o.category = static_cast<uint8_t>(category);
            uint8_t missionClass = static_cast<uint8_t>(rng.inclusive(
                (t.categoryClasses[c.employerHouse][category] - 1u) * 4u) / 4u + 1u);
            while (budget > 0) {
                --budget;
                if (policy.classFourThreshold && rng.inclusive(100) >= policy.classFourThreshold) missionClass = 4;
                const auto& bucket = t.classBuckets.at(missionClass);
                if (bucket.empty()) return invalid();
                o.missionIndex = bucket.at(rng.inclusive(static_cast<uint16_t>((bucket.size() - 1) * 2)) / 2);
                if (std::any_of(board.offers.begin(), board.offers.end(), [&](const auto& prior) {
                        return prior.missionIndex == o.missionIndex; })) break; // original restarts category
                const auto& mission = t.missions.at(o.missionIndex);
                if (mission.extended && (c.crewCount < 2 || c.ownedMechCount < 2)) {
                    missionClass = 3; continue; // original reruns the war override
                }
                o.missionClass = missionClass;
                if (mission.extended) {
                    for (auto& amount : o.opposition) amount *= 3;
                    o.priceCeiling *= 3;
                    o.price = static_cast<uint16_t>((o.priceCeiling / 4 / 10) * 10);
                }
                chosen = true;
                break;
            }
        }
        if (!chosen) return invalid();
        if (o.missionClass == 1 || o.category >= 5) {
            const uint8_t order = t.directTargets[c.employerHouse][o.category][rng.inclusive(7)];
            if (order == 0 || order > 146) return invalid();
            o.targetPlanetIndex = order - 1u;
        } else {
            bool found = false;
            for (size_t attempt = 0; attempt < kRetryBudget; ++attempt) {
                const auto index = t.targetStarts[o.category] + rng.inclusive(t.targetMaximums[o.category]);
                if (index >= t.targetRatings.size()) return invalid();
                if (t.targetRatings[index] != 0 && t.targetRatings[index] >= o.missionClass) {
                    o.targetPlanetIndex = static_cast<uint16_t>(index); found = true; break;
                }
            }
            if (!found) return invalid();
        }
        if (!validOffer(t, o)) return invalid();
        board.offers.push_back(o);
    }
    board.status = ContractBoardStatus::Ready;
    return board;
}

void encodeOriginalContractBoard(std::vector<uint8_t>& gam,
    const OriginalContractTables& t, const OriginalContractBoard& board) {
    if (gam.size() != 0x718 || board.offers.size() > 6)
        throw std::runtime_error("invalid original contract save size/count");
    for (const auto& o : board.offers)
        if (!validOffer(t, o)) throw std::runtime_error("invalid original contract offer");
    put(gam, 0x500, static_cast<uint16_t>(board.offers.size()));
    gam[0x70a] = board.classFourThreshold;
    for (size_t i = 0; i < board.offers.size(); ++i) {
        const auto& o = board.offers[i];
        const auto& m = t.missions[o.missionIndex];
        const std::array<uint16_t, 13> fields{{m.descriptor, o.targetPlanetIndex, o.category, o.missionClass,
            o.opposition[0], o.opposition[1], o.opposition[2], o.price, o.salvage, o.advance,
            o.priceCeiling, o.salvageCeiling, o.advanceCeiling}};
        for (size_t f = 0; f < fields.size(); ++f) put(gam, 0x502 + f * 12 + i * 2, fields[f]);
        put(gam, 0x5e3 + i * 2, m.id);
    }
}

OriginalContractBoard decodeOriginalContractBoard(const std::vector<uint8_t>& gam,
    const OriginalContractTables& t) {
    if (gam.size() != 0x718 || word(gam, 0x500) > 6)
        throw std::runtime_error("invalid original contract save size/count");
    OriginalContractBoard b;
    b.classFourThreshold = gam[0x70a];
    if (b.classFourThreshold != 0 && b.classFourThreshold != 50 && b.classFourThreshold != 75)
        throw std::runtime_error("invalid original contract date phase");
    for (size_t i = 0; i < word(gam, 0x500); ++i) {
        const auto pointer = word(gam, 0x502 + i * 2), id = word(gam, 0x5e3 + i * 2);
        const auto m = std::find_if(t.missions.begin(), t.missions.end(), [&](const auto& mission) {
            return mission.descriptor == pointer && mission.id == id;
        });
        if (m == t.missions.end()) throw std::runtime_error("unknown saved original mission descriptor");
        OriginalContractOffer o;
        o.missionIndex = static_cast<size_t>(m - t.missions.begin());
        o.targetPlanetIndex = word(gam, 0x50e + i * 2);
        const auto category = word(gam, 0x51a + i * 2), missionClass = word(gam, 0x526 + i * 2);
        if (category >= 8 || missionClass < 1 || missionClass > 4)
            throw std::runtime_error("invalid saved original mission category/class");
        o.category = static_cast<uint8_t>(category); o.missionClass = static_cast<uint8_t>(missionClass);
        for (size_t j = 0; j < 3; ++j) o.opposition[j] = word(gam, 0x532 + j * 12 + i * 2);
        o.price = word(gam, 0x556 + i * 2); o.salvage = word(gam, 0x562 + i * 2); o.advance = word(gam, 0x56e + i * 2);
        o.priceCeiling = word(gam, 0x57a + i * 2); o.salvageCeiling = word(gam, 0x586 + i * 2); o.advanceCeiling = word(gam, 0x592 + i * 2);
        if (!validOffer(t, o) || std::any_of(b.offers.begin(), b.offers.end(), [&](const auto& prior) {
                return prior.missionIndex == o.missionIndex; }))
            throw std::runtime_error("invalid saved original contract offer");
        b.offers.push_back(o);
    }
    if (!b.offers.empty()) b.status = ContractBoardStatus::Ready;
    return b;
}
} // namespace mw::battle
