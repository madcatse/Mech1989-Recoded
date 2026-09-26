#include "mech3d/mech_runtime_state.h"

#include <algorithm>

namespace mw::mech3d {
namespace {

bool containsId(const std::vector<int>& ids, int componentId) {
    return std::find(ids.begin(), ids.end(), componentId) != ids.end();
}

void appendUniqueId(std::vector<int>& ids, int componentId) {
    if (!containsId(ids, componentId)) {
        ids.push_back(componentId);
    }
}

std::vector<int> uniqueIds(const std::vector<int>& ids) {
    std::vector<int> result;
    result.reserve(ids.size());
    for (int id : ids) {
        appendUniqueId(result, id);
    }
    return result;
}

MechComponentDestroyedAction destroyedActionForComponent(
    const std::vector<MechComponentDamageRule>& damageRules,
    int componentId) {
    const auto it = std::find_if(
        damageRules.begin(),
        damageRules.end(),
        [componentId](const MechComponentDamageRule& rule) {
            return rule.componentId == componentId;
        });
    if (it == damageRules.end()) {
        return MechComponentDestroyedAction::HideComponent;
    }
    return it->destroyedAction;
}

} // namespace

void setMechRuntimeAnimation(MechRuntimeState& state, const std::string& animationId) {
    state.currentAnimationId = animationId;
}

void destroyMechRuntimeComponent(MechRuntimeState& state, int componentId) {
    appendUniqueId(state.destroyedComponentIds, componentId);
}

void hideMechRuntimeComponent(MechRuntimeState& state, int componentId) {
    appendUniqueId(state.hiddenComponentIds, componentId);
}

void disableMechRuntimeComponent(MechRuntimeState& state, int componentId) {
    appendUniqueId(state.disabledComponentIds, componentId);
}

void clearHiddenMechRuntimeComponents(MechRuntimeState& state) {
    state.hiddenComponentIds.clear();
}

ResolvedMechRuntimeState resolveMechRuntimeState(
    const MechRuntimeState& state,
    const std::vector<MechComponentDamageRule>& damageRules,
    const std::string& deathAnimationId) {
    ResolvedMechRuntimeState resolved;
    resolved.requestedAnimationId = state.currentAnimationId;
    resolved.activeAnimationId = state.currentAnimationId;
    resolved.destroyedComponentIds = uniqueIds(state.destroyedComponentIds);
    resolved.hiddenComponentIds = uniqueIds(state.hiddenComponentIds);
    resolved.disabledComponentIds = uniqueIds(state.disabledComponentIds);

    for (int componentId : resolved.destroyedComponentIds) {
        const MechComponentDestroyedAction action = destroyedActionForComponent(damageRules, componentId);
        if (action == MechComponentDestroyedAction::DestroyMech) {
            resolved.mechDestroyed = true;
        } else {
            appendUniqueId(resolved.hiddenComponentIds, componentId);
            appendUniqueId(resolved.disabledComponentIds, componentId);
        }
    }

    if (resolved.mechDestroyed) {
        resolved.activeAnimationId = deathAnimationId;
        resolved.usesAssemblyFrameDeath = true;
        // Explicitly detached limbs remain absent while the assembly-frame
        // death sequence plays and after it reaches its final pose.
        resolved.renderVisibility.hiddenComponentIds =
            resolved.hiddenComponentIds;
        for (int componentId : resolved.disabledComponentIds) {
            appendUniqueId(
                resolved.renderVisibility.hiddenComponentIds,
                componentId);
        }
    } else {
        resolved.renderVisibility.hiddenComponentIds = resolved.hiddenComponentIds;
        for (int componentId : resolved.disabledComponentIds) {
            appendUniqueId(resolved.renderVisibility.hiddenComponentIds, componentId);
        }
    }
    resolved.renderVisibility.hideDescendants = true;
    return resolved;
}

bool isResolvedMechComponentVisible(
    const ModelAssembly& assembly,
    const ResolvedMechRuntimeState& state,
    int componentId) {
    return isComponentVisible(assembly, state.renderVisibility, componentId);
}

ResolvedMechComponentState resolveMechComponentState(
    const ModelAssembly& assembly,
    const ResolvedMechRuntimeState& state,
    int componentId) {
    ResolvedMechComponentState componentState;
    componentState.componentId = componentId;
    componentState.destroyed = containsId(state.destroyedComponentIds, componentId);
    componentState.hidden = containsId(state.hiddenComponentIds, componentId);
    componentState.disabled = containsId(state.disabledComponentIds, componentId);
    componentState.visible = isResolvedMechComponentVisible(assembly, state, componentId);
    return componentState;
}

std::vector<ResolvedMechComponentState> resolveMechComponentStates(
    const ModelAssembly& assembly,
    const ResolvedMechRuntimeState& state) {
    std::vector<ResolvedMechComponentState> components;
    components.reserve(assembly.components.size());
    for (const ModelAssemblyComponent& component : assembly.components) {
        components.push_back(resolveMechComponentState(assembly, state, component.componentId));
    }
    return components;
}

} // namespace mw::mech3d
