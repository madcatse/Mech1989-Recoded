#include "presentation/campaign_command_screen.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace mw::presentation {
namespace {
using namespace battle;
using Candidate = std::pair<BattleLanceTargetKind, EntityId>;
constexpr int crewColumns[] = {16, 121, 228};
std::vector<const CombatantSnapshot*> companions(const BattleSnapshot& s, bool eligibleOnly) {
    std::vector<const CombatantSnapshot*> result;
    for (const auto& actor : s.combatants) {
        if (!actor.playerControlled && actor.roster.team == BattleTeam::Player &&
            (!eligibleOnly || battleLanceOrderRecipientEligible(actor))) result.push_back(&actor);
    }
    std::sort(result.begin(), result.end(), [](const auto* a, const auto* b) { return a->id.value < b->id.value; });
    return result;
}
const CombatantSnapshot* selected(const BattleSnapshot& s, const CampaignCommandScreenState& state) {
    auto list = companions(s, true);
    for (auto* actor : list) if (actor->id == state.selectedCompanion) return actor;
    return list.empty() ? nullptr : list.front();
}
std::vector<Candidate> targets(const BattleSnapshot& s) {
    std::vector<Candidate> result;
    for (const auto& actor : s.combatants) if (battleLanceOrderTargetKnown(s, actor.id))
        result.push_back({BattleLanceTargetKind::Combatant, actor.id});
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.second.value < b.second.value; });
    if (battleLanceOrderObjectiveEligible(s)) result.push_back({BattleLanceTargetKind::Objective, {}});
    return result;
}
std::string upper(std::string value) {
    for (char& c : value) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return value;
}
std::string mechName(const std::string& preset) {
    if (preset == "phoenix_hawk") return "PHOENIX HAWK";
    if (preset == "shadow_hawk") return "SHADOW HAWK";
    return upper(preset);
}
const CombatantSnapshot* byId(const BattleSnapshot& s, EntityId id) {
    for (const auto& c : s.combatants) if (c.id == id) return &c;
    return nullptr;
}
bool targetPoint(const BattleSnapshot& s, BattleLanceTargetKind kind, EntityId id, Transform& point) {
    if (kind == BattleLanceTargetKind::Objective && s.objective.valid && !s.objective.depleted) { point = s.objective.transform; return true; }
    if (kind == BattleLanceTargetKind::Combatant && battleLanceOrderTargetKnown(s, id)) {
        point = byId(s, id)->transform; return true;
    }
    return false;
}
std::string targetLabel(const BattleSnapshot& s, const CombatantSnapshot& actor) {
    const auto& ai = actor.replacementAi;
    if (ai.targetKind == BattleReplacementAiTargetKind::Objective) return "BASE";
    if (ai.targetKind == BattleReplacementAiTargetKind::Combatant) {
        const auto* target = byId(s, ai.combatTargetEntityId);
        if (target && battleLanceOrderTargetKnown(s, target->id)) return mechName(target->mechPresetId);
    }
    if (actor.lanceOrder.sequence && actor.lanceOrder.issued.targetKind == BattleLanceTargetKind::WorldPoint) return "LOCATION";
    return "NONE";
}
uint32_t severity(int remaining, int maximum) {
    if (maximum <= 0 || remaining <= 0) return 0xff000000u;
    if (remaining >= maximum) return 0xff00aa00u;
    return remaining * 2 > maximum ? 0xffffff55u : 0xffff5555u;
}
} // namespace

CampaignCommandScreenAction campaignCommandScreenKey(const battle::BattleSnapshot& snapshot,
    CampaignCommandScreenState& state, CampaignCommandKey key, bool repeat) {
    using namespace battle;
    CampaignCommandScreenAction action;
    if (repeat) return action;
    if (key == CampaignCommandKey::Status) { state.page = CampaignCommandPage::Crew; action.status = true; return action; }
    if (key == CampaignCommandKey::Cockpit) { state.page = CampaignCommandPage::Crew; action.cockpit = true; return action; }
    if (key == CampaignCommandKey::Back) {
        if (state.page == CampaignCommandPage::Crew) action.cockpit = true;
        else if (state.page == CampaignCommandPage::Location || state.page == CampaignCommandPage::Target)
            state.page = state.row == 0 ? CampaignCommandPage::Orders : CampaignCommandPage::Crew;
        else state.page = CampaignCommandPage::Crew;
        return action;
    }
    const auto* actor = selected(snapshot, state);
    if (!actor) return action;
    if (actor->id != state.selectedCompanion) {
        state.selectedCompanion = actor->id;
        state.page = CampaignCommandPage::Crew;
    }
    const int direction = key == CampaignCommandKey::Left || key == CampaignCommandKey::Up ? -1 : 1;
    const bool arrow = key != CampaignCommandKey::Enter;
    const auto startTarget = [&] {
        const auto list = targets(snapshot);
        state.page = CampaignCommandPage::Target;
        state.draft.targetKind = list.empty() ? BattleLanceTargetKind::None : list.front().first;
        state.draft.targetEntityId = list.empty() ? EntityId{} : list.front().second;
    };
    const auto confirm = [&] {
        BattleInputCommand command;
        command.tickIndex = snapshot.tickIndex;
        for (const auto& c : snapshot.combatants) if (c.playerControlled) command.entityId = c.id;
        state.draft.recipient = actor->id;
        state.draft.issuer = command.entityId;
        state.draft.issueTickIndex = snapshot.tickIndex;
        command.lanceOrder = state.draft;
        if (battleLanceOrderCommandValid(snapshot, command)) { action.command = command; state.page = CampaignCommandPage::Crew; }
    };
    if (state.page == CampaignCommandPage::Crew) {
        if (key == CampaignCommandKey::Left || key == CampaignCommandKey::Right) {
            const auto list = companions(snapshot, true);
            auto i = std::find(list.begin(), list.end(), actor) - list.begin();
            i = (i + direction + static_cast<ptrdiff_t>(list.size())) % static_cast<ptrdiff_t>(list.size());
            state.selectedCompanion = list[static_cast<size_t>(i)]->id;
        } else if (arrow) state.row = (state.row + direction + 4) % 4;
        else if (state.row == 0) {
            state.page = CampaignCommandPage::Orders;
            state.orderIndex = static_cast<int>(actor->lanceOrder.issued.kind);
        } else if (state.row == 1) {
            state.draft = {};
            state.draft.kind = actor->lanceOrder.sequence ? actor->lanceOrder.issued.kind : BattleLanceOrderKind::ActOnOwn;
            if (state.draft.kind == BattleLanceOrderKind::ActOnOwn || state.draft.kind == BattleLanceOrderKind::AttackEnemy) startTarget();
            else { state.page = CampaignCommandPage::Location; state.draft.targetKind = BattleLanceTargetKind::WorldPoint;
                   state.draft.destination = actor->lanceOrder.issued.destination; }
        } else { state.page = state.row == 2 ? CampaignCommandPage::Damage : CampaignCommandPage::Weapons; state.detailPage = 0; }
    } else if (state.page == CampaignCommandPage::Orders) {
        if (arrow) state.orderIndex = (state.orderIndex + direction + 6) % 6;
        else {
            state.draft = {};
            state.draft.kind = static_cast<BattleLanceOrderKind>(state.orderIndex);
            if (state.draft.kind == BattleLanceOrderKind::ActOnOwn) confirm();
            else if (state.draft.kind == BattleLanceOrderKind::AttackEnemy) startTarget();
            else { state.page = CampaignCommandPage::Location; state.draft.targetKind = BattleLanceTargetKind::WorldPoint;
                   state.draft.destination = actor->transform; state.draft.destination.y = 0; state.draft.destination.headingRadians = 0; }
        }
    } else if (state.page == CampaignCommandPage::Location) {
        if (!arrow) confirm();
        else {
            auto& p = state.draft.destination;
            if (key == CampaignCommandKey::Left || key == CampaignCommandKey::Right) p.x += direction * campaignBattleMapWorldUnitsPerPixel();
            else p.z += direction * campaignBattleMapWorldUnitsPerPixel();
            const auto& b = snapshot.battlefieldBoundary;
            p.x = std::clamp(p.x, b.valid ? b.worldMinX : snapshot.terrain.boundsMinX, b.valid ? b.worldMaxX : snapshot.terrain.boundsMaxX);
            p.z = std::clamp(p.z, b.valid ? b.worldMinZ : snapshot.terrain.boundsMinZ, b.valid ? b.worldMaxZ : snapshot.terrain.boundsMaxZ);
        }
    } else if (state.page == CampaignCommandPage::Target) {
        if (!arrow) {
            if (state.draft.targetKind == BattleLanceTargetKind::Combatant ||
                state.draft.targetKind == BattleLanceTargetKind::Objective) confirm();
        }
        else {
            const auto list = targets(snapshot);
            if (!list.empty()) {
                const Candidate current{state.draft.targetKind, state.draft.targetEntityId};
                auto it = std::find(list.begin(), list.end(), current);
                const int index = it == list.end() ? (direction > 0 ? -1 : 0) : static_cast<int>(it - list.begin());
                const auto& next = list[static_cast<size_t>((index + direction + static_cast<int>(list.size())) % static_cast<int>(list.size()))];
                state.draft.targetKind = next.first; state.draft.targetEntityId = next.second;
            }
        }
    } else if (!arrow) state.page = CampaignCommandPage::Crew;
    else {
        const int pages = state.page == CampaignCommandPage::Damage ? 4 : std::max(1, static_cast<int>((actor->weapons.size() + 5u) / 6u));
        state.detailPage = (state.detailPage + direction + pages) % pages;
    }
    return action;
}

CampaignCommandScreenAction campaignCommandScreenPointer(const battle::BattleSnapshot& snapshot,
    CampaignCommandScreenState& state, CampaignCommandPointerEvent event, int screenX, int screenY) {
    using namespace battle;
    if (event == CampaignCommandPointerEvent::Back)
        return campaignCommandScreenKey(snapshot, state, CampaignCommandKey::Back);
    const auto* actor = selected(snapshot, state);
    if (!actor) return {};
    if (actor->id != state.selectedCompanion) {
        state.selectedCompanion = actor->id;
        state.page = CampaignCommandPage::Crew;
    }
    const bool click = event == CampaignCommandPointerEvent::Select;
    if (click && screenY >= 127 && screenY < 187) {
        const auto list = companions(snapshot, false); // Same slots, including disabled panels, as rendering.
        for (size_t i = 0; i < std::min<size_t>(3, list.size()); ++i) {
            if (screenX < crewColumns[i]-2 || screenX >= crewColumns[i]+80) continue;
            if (!battleLanceOrderRecipientEligible(*list[i])) return {};
            if (state.selectedCompanion != list[i]->id) {
                state.selectedCompanion = list[i]->id;
                state.page = CampaignCommandPage::Crew;
                state.draft = {};
                return {};
            }
            if (state.page == CampaignCommandPage::Orders) {
                if (screenY < 175) {
                    state.orderIndex = (screenY-127)/8;
                    return campaignCommandScreenKey(snapshot, state, CampaignCommandKey::Enter);
                }
            } else if (state.page == CampaignCommandPage::Damage || state.page == CampaignCommandPage::Weapons) {
                return campaignCommandScreenKey(snapshot, state,
                    screenY < 139 ? CampaignCommandKey::Back : CampaignCommandKey::Down);
            } else if (screenY >= 155) {
                state.page = CampaignCommandPage::Crew;
                state.row = (screenY-155)/8;
                return campaignCommandScreenKey(snapshot, state, CampaignCommandKey::Enter);
            }
            return {};
        }
    }
    const auto rect = campaignCockpitCommandMapRect();
    if (screenX < rect.x || screenY < rect.y || screenX >= rect.x+rect.width || screenY >= rect.y+rect.height) return {};
    const auto& b = snapshot.battlefieldBoundary;
    const double centerX = (b.worldMinX+b.worldMaxX)/2;
    const double centerZ = (b.worldMinZ+b.worldMaxZ)/2;
    if (state.page == CampaignCommandPage::Location) {
        state.draft.destination.x = std::clamp(centerX + (screenX-rect.x-rect.width/2)*campaignBattleMapWorldUnitsPerPixel(),
            b.valid ? b.worldMinX : snapshot.terrain.boundsMinX, b.valid ? b.worldMaxX : snapshot.terrain.boundsMaxX);
        state.draft.destination.z = std::clamp(centerZ + (screenY-rect.y-rect.height/2)*campaignBattleMapWorldUnitsPerPixel(),
            b.valid ? b.worldMinZ : snapshot.terrain.boundsMinZ, b.valid ? b.worldMaxZ : snapshot.terrain.boundsMaxZ);
        if (click) return campaignCommandScreenKey(snapshot, state, CampaignCommandKey::Enter);
        return {};
    }
    // Four virtual pixels around a visible icon; nearest first, stable target/id order on ties.
    const auto hitDistance = [&](const Transform& point) {
        const int x = rect.width/2 + static_cast<int>(std::lround((point.x-centerX)/campaignBattleMapWorldUnitsPerPixel()));
        const int y = rect.height/2 + static_cast<int>(std::lround((point.z-centerZ)/campaignBattleMapWorldUnitsPerPixel()));
        if (x < 0 || y < 0 || x >= rect.width || y >= rect.height) return 17;
        const int dx = screenX-rect.x-x, dy = screenY-rect.y-y;
        return dx*dx+dy*dy;
    };
    if (state.page == CampaignCommandPage::Target) {
        int nearest = 17;
        std::optional<Candidate> hit;
        for (const auto& candidate : targets(snapshot)) {
            Transform point;
            if (!targetPoint(snapshot, candidate.first, candidate.second, point)) continue;
            const int d = hitDistance(point);
            if (d < nearest) { nearest = d; hit = candidate; }
        }
        if (hit) {
            state.draft.targetKind = hit->first;
            state.draft.targetEntityId = hit->second;
            if (click) return campaignCommandScreenKey(snapshot, state, CampaignCommandKey::Enter);
        }
    } else if (click && state.page == CampaignCommandPage::Crew) {
        int nearest = 17;
        for (const auto* companion : companions(snapshot, true)) {
            const int d = hitDistance(companion->transform);
            if (d < nearest) { nearest = d; state.selectedCompanion = companion->id; }
        }
    }
    return {};
}

CampaignCommandMapOverlay campaignCommandMapOverlay(const battle::BattleSnapshot& snapshot, const CampaignCommandScreenState& state) {
    using namespace battle;
    CampaignCommandMapOverlay result;
    const auto* actor = selected(snapshot, state);
    if (!actor) return result;
    result.from = actor->transform;
    result.bgra = 0xff55ff55u; // Original image0375: bright green target line.
    result.targeting = state.page == CampaignCommandPage::Location || state.page == CampaignCommandPage::Target;
    if (state.page == CampaignCommandPage::Location) { result.to = state.draft.destination; result.visible = true; }
    else if (state.page == CampaignCommandPage::Target) result.visible = targetPoint(snapshot, state.draft.targetKind, state.draft.targetEntityId, result.to);
    else if (actor->replacementAi.targetKind != BattleReplacementAiTargetKind::None) result.visible = targetPoint(snapshot,
        actor->replacementAi.targetKind == BattleReplacementAiTargetKind::Combatant ? BattleLanceTargetKind::Combatant : BattleLanceTargetKind::Objective,
        actor->replacementAi.combatTargetEntityId, result.to);
    else if (actor->replacementAi.active) { result.visible = true; result.to = actor->replacementAi.movementDestination; }
    return result;
}

std::vector<uint32_t> campaignCommandOverlayPixels(const CampaignCommandMapOverlay& overlay,
    double centerX, double centerZ, int width, int height) {
    if (width <= 0 || height <= 0) return {};
    std::vector<uint32_t> result(static_cast<size_t>(width) * height);
    if (!overlay.visible) return result;
    const auto point = [&](const battle::Transform& p) {
        return std::pair<int,int>{width/2 + static_cast<int>(std::lround((p.x-centerX)/campaignBattleMapWorldUnitsPerPixel())),
            height/2 + static_cast<int>(std::lround((p.z-centerZ)/campaignBattleMapWorldUnitsPerPixel()))};
    };
    const auto put = [&](int x, int y, uint32_t color) {
        if (x >= 0 && x < width && y >= 0 && y < height) result[static_cast<size_t>(y)*width+x] = color;
    };
    auto [x,y] = point(overlay.from);
    const auto [tx,ty] = point(overlay.to);
    const int dx=std::abs(tx-x), sx=x<tx?1:-1, dy=-std::abs(ty-y), sy=y<ty?1:-1;
    int error=dx+dy;
    // Battlefield coordinates are bounded by the command validator. Rasterize
    // in virtual pixels so WGL and visual-smoke output use identical geometry.
    while (true) {
        put(x,y,overlay.bgra);
        if (x==tx && y==ty) break;
        const int e=2*error;
        if(e>=dy){error+=dy;x+=sx;}
        if(e<=dx){error+=dx;y+=sy;}
    }
    if (overlay.targeting) for (int i=-3;i<=3;++i) {
        put(tx+i,ty-3,0xffffff55u); put(tx+i,ty+3,0xffffff55u);
        put(tx-3,ty+i,0xffffff55u); put(tx+3,ty+i,0xffffff55u);
    }
    return result;
}

void composeCampaignCommandPanels(std::vector<uint32_t>& pixels, const CampaignCockpitFont& font,
    const battle::BattleSnapshot& snapshot, const CampaignCommandScreenState& state,
    const std::vector<CampaignCommandCrewName>& names) {
    using namespace battle;
    if (pixels.size() != 320u * 200u || font.width <= 0 || font.height <= 0) return;
    const auto put = [&](int x, int y, uint32_t color) { if (x >= 0 && x < 320 && y >= 0 && y < 200) pixels[static_cast<size_t>(y) * 320u + x] = color; };
    const auto rect = [&](int x, int y, int w, int h, uint32_t color) { for (int j = y; j < y+h; ++j) for (int i = x; i < x+w; ++i) put(i,j,color); };
    const auto text = [&](int x, int y, std::string value, uint32_t color) {
        value = upper(value).substr(0, 13);
        if (color == 0xff000000u) rect(x, y, static_cast<int>(value.size()) * font.width, font.height, 0xffaa0000u);
        for (char c : value) {
            if (font.contains(c)) for (int row = 0; row < font.height; ++row) {
                const auto bits = font.rows[static_cast<size_t>(static_cast<unsigned char>(c) - font.firstCode) * font.height + row];
                for (int col = 0; col < font.width; ++col) if (bits & (1u << (7-col))) put(x+col,y+row,color);
            }
            x += font.width;
        }
    };
    const auto frame = [&](int x, int y, int w, int h) {
        rect(x,y,w,1,0xffffff55u); rect(x,y+h-1,w,1,0xffffff55u);
        rect(x,y,1,h,0xffffff55u); rect(x+w-1,y,1,h,0xffffff55u);
    };
    const auto list = companions(snapshot, false);
    const auto* focus = selected(snapshot, state);
    for (size_t index = 0; index < std::min<size_t>(3, list.size()); ++index) {
        const auto& actor = *list[index];
        const int x = crewColumns[index];
        rect(x-2,127,82,60,0xff000000u);
        const bool activePanel = focus && focus->id == actor.id;
        const auto marker = campaignBattleMapPlayerMarker(campaignBattlePlayerLanceSlot(actor.roster.sourceSlot, false),
            CampaignBattlePresentationMode::CockpitCommandMap, snapshot.terrain.environmentId);
        if (activePanel && state.page == CampaignCommandPage::Orders) {
            for (int i=0;i<6;++i) text(x,129+i*8,battleLanceOrderLabel(static_cast<BattleLanceOrderKind>(i)),0xffffffffu);
            frame(x-2,127+state.orderIndex*8,81,9); continue;
        }
        if (activePanel && (state.page == CampaignCommandPage::Damage || state.page == CampaignCommandPage::Weapons)) {
            text(x,129,state.page == CampaignCommandPage::Damage ? "DAMAGE" : "WEAPONS",0xffffffffu);
            if (state.page == CampaignCommandPage::Weapons) {
                for (size_t i=static_cast<size_t>(state.detailPage)*6u; i<actor.weapons.size() && i<static_cast<size_t>(state.detailPage+1)*6u; ++i) {
                    const auto& w=actor.weapons[i];
                    const std::string ammo = w.ammunitionOwnership == BattleWeaponAmmunitionOwnership::EnergyNoAmmunition
                        ? "" : " " + (w.ammunitionStateKnown ? std::to_string(w.ammunitionRemaining) : "?");
                    const std::string label = w.displayName.substr(0, std::max(1, 10-static_cast<int>(ammo.size()))) + " " + w.locationId + ammo;
                    const uint32_t conditionColors[]={0xff00aa00u,0xffffff55u,0xffff5555u,0xff000000u};
                    text(x,141+static_cast<int>(i%6u)*8,label,
                        conditionColors[w.functional ? std::min<int>(3,w.condition) : 3]);
                }
            } else {
                static constexpr const char* armorNames[]={"RA","LA","RL","LL","HD","CT","RC","RT","LT"};
                static constexpr const char* componentNames[]={"ENGINE","GYROS","SENSORS","LIFE SUPP","HEAT SINKS","LA ACT","RA ACT","LL ACT","RL ACT","JUMP JETS"};
                for (int i=state.detailPage*6;i<std::min(19,(state.detailPage+1)*6);++i) {
                    if (i<9) {
                        const auto& a=actor.detailedDamage.armorSections[static_cast<size_t>(i)];
                        // Armor enum has a rear CT entry; internal CT is shared.
                        const int internal=i==6 ? 5 : i>6 ? i-1 : i;
                        const auto& b=actor.detailedDamage.internalSections[static_cast<size_t>(internal)];
                        text(x,141+i%6*8,std::string(armorNames[i])+" "+std::to_string(a.armorRemaining)+"/"+std::to_string(a.armorMaximum)+" "+std::to_string(b.structureRemaining),
                            severity(a.armorRemaining+b.structureRemaining,a.armorMaximum+b.structureMaximum));
                    } else {
                        const auto& c=actor.detailedDamage.criticalComponents[static_cast<size_t>(i-9)];
                        const uint32_t colors[]={0xff00aa00u,0xffffff55u,0xffff5555u,0xff000000u};
                        text(x,141+i%6*8,componentNames[i-9],colors[c.functional ? std::min<int>(3,c.condition) : 3]);
                    }
                }
            }
            continue;
        }
        std::string name="CREW "+std::to_string(index+1);
        for (const auto& n : names) if (n.sourceSlot==actor.roster.sourceSlot) name=n.name;
        text(x,129,name.substr(0,11),marker.bgra);
        for (size_t i=0;i<9;++i) if(marker.pixels[i]) put(x+66+static_cast<int>(i%3),131+static_cast<int>(i/3),marker.bgra);
        text(x,137,mechName(actor.mechPresetId),marker.bgra);
        std::string status = actor.missionStatus == CombatantMissionStatus::Escaped ? "ESCAPED" : actor.mechDestroyed ? "DESTROYED" :
            actor.missionStatus != CombatantMissionStatus::Active ? "DISABLED" : actor.replacementAi.retreating ? "FLEEING" :
            actor.lanceOrder.sequence ? battleLanceOrderStatusLabel(actor.lanceOrder) :
            actor.replacementAi.targetKind==BattleReplacementAiTargetKind::Objective ? "CHARGING BASE" :
            actor.replacementAi.targetKind==BattleReplacementAiTargetKind::Combatant ? "CHARGING" : "UNENGAGED";
        text(x,145,status,marker.bgra);
        const bool editing = activePanel && (state.page == CampaignCommandPage::Location || state.page == CampaignCommandPage::Target);
        text(x,157,battleLanceOrderLabel(editing ? state.draft.kind : actor.lanceOrder.issued.kind),0xffffffffu);
        std::string target = targetLabel(snapshot,actor);
        if (editing) {
            if (state.page == CampaignCommandPage::Location) target="LOCATION";
            else if (state.draft.targetKind==BattleLanceTargetKind::Objective) target="BASE";
            else {
                const auto* candidate=byId(snapshot,state.draft.targetEntityId);
                target=candidate && battleLanceOrderTargetKnown(snapshot,candidate->id) ? mechName(candidate->mechPresetId) : "NO TARGETS";
            }
        }
        text(x,165,target,0xffffffffu);
        int remaining=0,maximum=0;
        for(const auto& a:actor.detailedDamage.armorSections){ remaining+=a.armorRemaining; maximum+=a.armorMaximum; }
        for(const auto& a:actor.detailedDamage.internalSections){ remaining+=a.structureRemaining; maximum+=a.structureMaximum; }
        text(x,173,"DAMAGE",severity(remaining,maximum));
        int condition=0;
        for (const auto& weapon : actor.weapons) condition=std::max(condition,weapon.functional ? std::min<int>(3,weapon.condition) : 3);
        const uint32_t weaponColors[]={0xff00aa00u,0xffffff55u,0xffff5555u,0xff000000u};
        text(x,181,"WEAPONS",weaponColors[condition]);
        if(activePanel) frame(x-2,155+state.row*8,81,9);
    }
}
} // namespace mw::presentation
