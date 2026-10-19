#pragma once
#include "presentation/campaign_cockpit_compositor.h"

namespace mw::presentation {
enum class CampaignCommandPage : uint8_t { Crew, Orders, Location, Target, Damage, Weapons };
enum class CampaignCommandKey : uint8_t { Left, Right, Up, Down, Enter, Back, Status, Cockpit };
enum class CampaignCommandPointerEvent : uint8_t { Move, Select, Back };
struct CampaignCommandScreenState {
    battle::EntityId selectedCompanion{};
    CampaignCommandPage page = CampaignCommandPage::Crew;
    int row = 0; // order, target, damage, weapons
    int orderIndex = 0;
    int detailPage = 0;
    battle::BattleLanceOrderCommand draft;
};
struct CampaignCommandScreenAction {
    bool cockpit = false;
    bool status = false;
    std::optional<battle::BattleInputCommand> command;
};
struct CampaignCommandMapOverlay {
    bool visible = false;
    bool targeting = false;
    battle::Transform from;
    battle::Transform to;
    uint32_t bgra = 0xffffff55u;
};
struct CampaignCommandCrewName {
    std::string sourceSlot;
    std::string name;
};
CampaignCommandScreenAction campaignCommandScreenKey(
    const battle::BattleSnapshot& snapshot, CampaignCommandScreenState& state,
    CampaignCommandKey key, bool repeat = false);
// Coordinates are original 320x200 screen pixels, after viewport/letterbox mapping.
// Pointer movement edits only the draft; Select uses the keyboard confirmation path.
CampaignCommandScreenAction campaignCommandScreenPointer(
    const battle::BattleSnapshot& snapshot, CampaignCommandScreenState& state,
    CampaignCommandPointerEvent event, int screenX, int screenY);
CampaignCommandMapOverlay campaignCommandMapOverlay(
    const battle::BattleSnapshot& snapshot, const CampaignCommandScreenState& state);
std::vector<uint32_t> campaignCommandOverlayPixels(
    const CampaignCommandMapOverlay& overlay, double centerX, double centerZ,
    int width, int height);
void composeCampaignCommandPanels(std::vector<uint32_t>& pixels,
    const CampaignCockpitFont& font, const battle::BattleSnapshot& snapshot,
    const CampaignCommandScreenState& state,
    const std::vector<CampaignCommandCrewName>& names = {});
} // namespace mw::presentation
