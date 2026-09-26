#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mw::battle {

enum class BattleMissionFamily {
    Defense,
    Deathmatch,
    Sprint,
    Retrieval,
    Assault,
    Extended,
};

enum class BattleMissionObjectiveIntent : uint8_t {
    Unknown,
    Protect,
    Destroy,
    Disable,
    Retrieve,
};

struct BattleMissionObjectiveBriefing {
    BattleMissionObjectiveIntent intent = BattleMissionObjectiveIntent::Unknown;
    bool intentProven = false;
    std::string targetKind;
    std::string provenance;
};

struct BattleMissionDefinition {
    uint8_t originalId = 0;
    uint32_t mwMainFileOffset = 0;
    uint32_t loadedOffset = 0;
    BattleMissionFamily family = BattleMissionFamily::Defense;
    bool extended = false;
    std::string_view title;
};

struct BattleMissionBriefing {
    bool valid = false;
    uint8_t originalId = 0;
    BattleMissionFamily family = BattleMissionFamily::Defense;
    bool extended = false;
    std::string title;
};

struct BattleMissionSequencePlan {
    bool valid = false;
    size_t stageCount = 0;
    std::array<uint8_t, 3> missionIds{};
    std::string provenance;
};

const std::array<BattleMissionDefinition, 34>& battleMissionDefinitions();
const char* battleMissionFamilyName(BattleMissionFamily family);
const char* battleMissionObjectiveIntentName(BattleMissionObjectiveIntent intent);
std::optional<BattleMissionDefinition> battleMissionDefinitionById(size_t id);
std::optional<BattleMissionDefinition> battleMissionDefinitionByTitle(std::string_view title);
BattleMissionBriefing battleMissionBriefingFromDefinition(const BattleMissionDefinition& definition);
BattleMissionBriefing defaultBattleMissionBriefing();
BattleMissionObjectiveBriefing battleMissionObjectiveBriefingById(size_t id);
BattleMissionSequencePlan battleExtendedCampaignPlan(uint32_t randomSeed);
BattleMissionSequencePlan battleFinalMissionPlan();
bool battleUneventfulGarrisonDutyRoll(uint32_t randomSeed);
std::string battleMissionTitleToken(const BattleMissionBriefing& briefing);

} // namespace mw::battle
