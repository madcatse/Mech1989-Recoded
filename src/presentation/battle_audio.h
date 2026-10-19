#pragma once
#include "audio/speaker_player.h"
#include "battle/battle_world.h"
namespace mw::presentation {
// Presentation consumes completed simulation ticks, never input/render cadence.
std::vector<audio::SpeakerRequest> battleSpeakerRequests(
    const battle::BattleSnapshot& before,const battle::BattleSnapshot& after);
}
