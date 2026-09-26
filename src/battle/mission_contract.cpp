#include "battle/mission_contract.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace mw::battle {
namespace {

using Mission = BattleMissionDefinition;

constexpr std::array<Mission, 34> kMissionDefinitions{{
    {0, 0x01176Bu, 0x01136Bu, BattleMissionFamily::Defense, false, "GARRISON DUTY"},
    {1, 0x01177Bu, 0x01137Bu, BattleMissionFamily::Defense, false, "GENERAL SECURITY DUTY"},
    {2, 0x011793u, 0x011393u, BattleMissionFamily::Defense, false, "DEFENSE OF A WATER FACTORY"},
    {3, 0x0117B0u, 0x0113B0u, BattleMissionFamily::Defense, false, "DEFENSE OF A WEAPONS FACTORY"},
    {4, 0x0117CFu, 0x0113CFu, BattleMissionFamily::Defense, false, "DEFENSE OF A FUEL DUMP"},
    {5, 0x0117E8u, 0x0113E8u, BattleMissionFamily::Defense, false, "DEFENSE OF FIELD COM UNIT"},
    {6, 0x011804u, 0x011404u, BattleMissionFamily::Defense, false, "DEFENSE OF A SUPPLY DEPOT"},
    {7, 0x011820u, 0x011420u, BattleMissionFamily::Defense, false, "DEFENSE OF LANDING FACILITIES"},
    {8, 0x011840u, 0x011440u, BattleMissionFamily::Deathmatch, false, "SUPPRESSION OF REBELLION"},
    {9, 0x01185Bu, 0x01145Bu, BattleMissionFamily::Sprint, false, "TEMPORARY RELIEF OF FORCES"},
    {10, 0x011878u, 0x011478u, BattleMissionFamily::Retrieval, false, "RESCUE OF A KIDNAP VICTIM"},
    {11, 0x011894u, 0x011494u, BattleMissionFamily::Retrieval, false, "RESCUE OF HOSTAGES"},
    {12, 0x0118A9u, 0x0114A9u, BattleMissionFamily::Retrieval, false, "RETRIEVAL OF STOLEN PROPERTY"},
    {13, 0x0118C8u, 0x0114C8u, BattleMissionFamily::Retrieval, false, "RETRIEVAL OF CAPTURED MECHS"},
    {14, 0x0118E6u, 0x0114E6u, BattleMissionFamily::Extended, true, "AN EXTENDED OFFENSIVE CAMPAIGN"},
    {15, 0x011907u, 0x011507u, BattleMissionFamily::Extended, true, "AN EXTENDED DEFENSIVE CAMPAIGN"},
    {16, 0x011928u, 0x011528u, BattleMissionFamily::Deathmatch, false, "A PLANETARY ASSUALT"},
    {17, 0x01193Eu, 0x01153Eu, BattleMissionFamily::Extended, true, "AN EXTENDED SIEGE CAMPAIGN"},
    {18, 0x01195Bu, 0x01155Bu, BattleMissionFamily::Sprint, false, "RELIEF OF ENGAGED FORCES"},
    {19, 0x011976u, 0x011576u, BattleMissionFamily::Deathmatch, false, "A RECONNAISSANCE RAID"},
    {20, 0x01198Eu, 0x01158Eu, BattleMissionFamily::Deathmatch, false, "A DIVERSIONARY RAID"},
    {21, 0x0119A4u, 0x0115A4u, BattleMissionFamily::Sprint, false, "CONTAINMENT OF SECURITY FORCES"},
    {22, 0x0119C5u, 0x0115C5u, BattleMissionFamily::Assault, false, "DESTRUCTION OF A WATER FACTORY"},
    {23, 0x0119E6u, 0x0115E6u, BattleMissionFamily::Assault, false, "DISABLING OF A WEAPONS FACTORY"},
    {24, 0x011A07u, 0x011607u, BattleMissionFamily::Assault, false, "DESTRUCTION OF A FUEL DUMP"},
    {25, 0x011A24u, 0x011624u, BattleMissionFamily::Assault, false, "DESTRUCTION OF AN AMMO DUMP"},
    {26, 0x011A42u, 0x011642u, BattleMissionFamily::Assault, false, "DISABLING OF A FIELD COM CENTER"},
    {27, 0x011A64u, 0x011664u, BattleMissionFamily::Deathmatch, false, "ELIMINATION OF GARRISON FORCES"},
    {28, 0x011A85u, 0x011685u, BattleMissionFamily::Assault, false, "DESTROYING STOLEN PROTOTYPES"},
    {29, 0x011AA4u, 0x0116A4u, BattleMissionFamily::Assault, false, "DESTRUCTION OF MECH FACILITIES"},
    {30, 0x011AC5u, 0x0116C5u, BattleMissionFamily::Deathmatch, false, "ELIMINATION OF SECURITY FORCES"},
    {31, 0x011AE6u, 0x0116E6u, BattleMissionFamily::Assault, false, "DESTRUCTION OF PORT FACILITIES"},
    {32, 0x011B07u, 0x011707u, BattleMissionFamily::Retrieval, false, "CAPTURE OF AMMO AND MECH PARTS"},
    {33, 0x011B28u, 0x011728u, BattleMissionFamily::Retrieval, false, "PARTICIPATING IN HOSTAGE RAID"},
}};

char normalizeChar(char ch) {
    if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    return ' ';
}

std::string normalizedTitle(std::string_view title) {
    std::string result(title.begin(), title.end());
    std::transform(result.begin(), result.end(), result.begin(), normalizeChar);
    result.erase(
        std::unique(result.begin(), result.end(), [](char a, char b) {
            return a == ' ' && b == ' ';
        }),
        result.end());
    while (!result.empty() && result.front() == ' ') {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back() == ' ') {
        result.pop_back();
    }
    return result;
}

} // namespace

const std::array<BattleMissionDefinition, 34>& battleMissionDefinitions() {
    return kMissionDefinitions;
}

const char* battleMissionFamilyName(BattleMissionFamily family) {
    switch (family) {
    case BattleMissionFamily::Defense:
        return "Defense";
    case BattleMissionFamily::Deathmatch:
        return "Deathmatch";
    case BattleMissionFamily::Sprint:
        return "Sprint";
    case BattleMissionFamily::Retrieval:
        return "Retrieval";
    case BattleMissionFamily::Assault:
        return "Assault";
    case BattleMissionFamily::Extended:
        return "Extended";
    }
    return "Unknown";
}

const char* battleMissionObjectiveIntentName(BattleMissionObjectiveIntent intent) {
    switch (intent) {
    case BattleMissionObjectiveIntent::Unknown:
        return "unknown";
    case BattleMissionObjectiveIntent::Protect:
        return "protect";
    case BattleMissionObjectiveIntent::Destroy:
        return "destroy";
    case BattleMissionObjectiveIntent::Disable:
        return "disable";
    case BattleMissionObjectiveIntent::Retrieve:
        return "retrieve";
    }
    return "unknown";
}

BattleMissionObjectiveBriefing battleMissionObjectiveBriefingById(size_t id) {
    BattleMissionObjectiveBriefing briefing;
    briefing.provenance = "mw_main_exact_mission_title:wiki_scenario_semantics";
    switch (id) {
    case 0:
        briefing.intent = BattleMissionObjectiveIntent::Protect;
        briefing.targetKind = "garrison_structure";
        break;
    case 1:
        briefing.intent = BattleMissionObjectiveIntent::Protect;
        briefing.targetKind = "security_structure";
        break;
    case 2:
        briefing.intent = BattleMissionObjectiveIntent::Protect;
        briefing.targetKind = "water_factory";
        break;
    case 3:
        briefing.intent = BattleMissionObjectiveIntent::Protect;
        briefing.targetKind = "weapons_factory";
        break;
    case 4:
        briefing.intent = BattleMissionObjectiveIntent::Protect;
        briefing.targetKind = "fuel_dump";
        break;
    case 5:
        briefing.intent = BattleMissionObjectiveIntent::Protect;
        briefing.targetKind = "field_com_unit";
        break;
    case 6:
        briefing.intent = BattleMissionObjectiveIntent::Protect;
        briefing.targetKind = "supply_depot";
        break;
    case 7:
        briefing.intent = BattleMissionObjectiveIntent::Protect;
        briefing.targetKind = "landing_facilities";
        break;
    case 10:
        briefing.intent = BattleMissionObjectiveIntent::Retrieve;
        briefing.targetKind = "kidnap_victim";
        break;
    case 11:
        briefing.intent = BattleMissionObjectiveIntent::Retrieve;
        briefing.targetKind = "hostages";
        break;
    case 12:
        briefing.intent = BattleMissionObjectiveIntent::Retrieve;
        briefing.targetKind = "stolen_property";
        break;
    case 13:
        briefing.intent = BattleMissionObjectiveIntent::Retrieve;
        briefing.targetKind = "captured_mechs";
        break;
    case 22:
        briefing.intent = BattleMissionObjectiveIntent::Destroy;
        briefing.targetKind = "water_factory";
        break;
    case 23:
        briefing.intent = BattleMissionObjectiveIntent::Disable;
        briefing.targetKind = "weapons_factory";
        break;
    case 24:
        briefing.intent = BattleMissionObjectiveIntent::Destroy;
        briefing.targetKind = "fuel_dump";
        break;
    case 25:
        briefing.intent = BattleMissionObjectiveIntent::Destroy;
        briefing.targetKind = "ammo_dump";
        break;
    case 26:
        briefing.intent = BattleMissionObjectiveIntent::Disable;
        briefing.targetKind = "field_com_center";
        break;
    case 28:
        briefing.intent = BattleMissionObjectiveIntent::Destroy;
        briefing.targetKind = "stolen_prototypes";
        break;
    case 29:
        briefing.intent = BattleMissionObjectiveIntent::Destroy;
        briefing.targetKind = "mech_facilities";
        break;
    case 31:
        briefing.intent = BattleMissionObjectiveIntent::Destroy;
        briefing.targetKind = "port_facilities";
        break;
    case 32:
        briefing.intent = BattleMissionObjectiveIntent::Retrieve;
        briefing.targetKind = "ammo_and_mech_parts";
        break;
    case 33:
        briefing.intent = BattleMissionObjectiveIntent::Retrieve;
        briefing.targetKind = "hostage_raid";
        break;
    default:
        briefing.provenance.clear();
        return briefing;
    }
    briefing.intentProven = true;
    return briefing;
}

BattleMissionSequencePlan battleExtendedCampaignPlan(uint32_t randomSeed) {
    BattleMissionSequencePlan plan;
    std::array<uint8_t, 31> singleMissionIds{};
    size_t singleMissionCount = 0u;
    for (const BattleMissionDefinition& definition : kMissionDefinitions) {
        if (definition.extended ||
            definition.family == BattleMissionFamily::Extended) {
            continue;
        }
        if (singleMissionCount >= singleMissionIds.size()) {
            return plan;
        }
        singleMissionIds[singleMissionCount++] = definition.originalId;
    }
    if (singleMissionCount != singleMissionIds.size()) {
        return plan;
    }

    uint32_t state = randomSeed ^ 0x4D573343u;
    for (size_t i = 0; i < plan.missionIds.size(); ++i) {
        state = state * 1664525u + 1013904223u;
        plan.missionIds[i] =
            singleMissionIds[state % singleMissionIds.size()];
    }
    plan.valid = true;
    plan.stageCount = 3u;
    plan.provenance =
        "wiki_extended_campaign:three_random_single_missions:lcg_seeded";
    return plan;
}

BattleMissionSequencePlan battleFinalMissionPlan() {
    BattleMissionSequencePlan plan;
    plan.valid = true;
    plan.stageCount = 2u;
    plan.missionIds[0] = 8u;
    plan.missionIds[1] = 12u;
    plan.provenance =
        "wiki_final_battle:deathmatch_then_retrieval";
    return plan;
}

bool battleUneventfulGarrisonDutyRoll(uint32_t randomSeed) {
    const uint32_t mixed =
        (randomSeed ^ 0x554E4556u) * 1664525u + 1013904223u;
    return (mixed & 0x1fu) == 0u;
}

std::optional<BattleMissionDefinition> battleMissionDefinitionById(size_t id) {
    if (id >= kMissionDefinitions.size()) {
        return std::nullopt;
    }
    return kMissionDefinitions[id];
}

std::optional<BattleMissionDefinition> battleMissionDefinitionByTitle(std::string_view title) {
    const std::string wanted = normalizedTitle(title);
    const auto found = std::find_if(kMissionDefinitions.begin(), kMissionDefinitions.end(), [&](const Mission& mission) {
        return normalizedTitle(mission.title) == wanted;
    });
    if (found == kMissionDefinitions.end()) {
        return std::nullopt;
    }
    return *found;
}

BattleMissionBriefing battleMissionBriefingFromDefinition(const BattleMissionDefinition& definition) {
    BattleMissionBriefing briefing;
    briefing.valid = true;
    briefing.originalId = definition.originalId;
    briefing.family = definition.family;
    briefing.extended = definition.extended;
    briefing.title = std::string(definition.title);
    return briefing;
}

BattleMissionBriefing defaultBattleMissionBriefing() {
    const std::optional<BattleMissionDefinition> definition = battleMissionDefinitionById(10u);
    if (!definition) {
        throw std::runtime_error("default battle mission definition is missing");
    }
    return battleMissionBriefingFromDefinition(*definition);
}

std::string battleMissionTitleToken(const BattleMissionBriefing& briefing) {
    std::string token = briefing.title;
    if (token.empty()) {
        token = "NONE";
    }
    for (char& ch : token) {
        if (std::isalnum(static_cast<unsigned char>(ch)) != 0) {
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        } else {
            ch = '_';
        }
    }
    token.erase(
        std::unique(token.begin(), token.end(), [](char a, char b) {
            return a == '_' && b == '_';
        }),
        token.end());
    while (!token.empty() && token.front() == '_') {
        token.erase(token.begin());
    }
    while (!token.empty() && token.back() == '_') {
        token.pop_back();
    }
    return token.empty() ? "NONE" : token;
}

} // namespace mw::battle
