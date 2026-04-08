//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/usd/usdProfiles/capabilityRegistry.h"

#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnostic.h"
#include "pxr/base/tf/instantiateSingleton.h"
#include "pxr/base/tf/token.h"

#include <algorithm>
#include <set>

PXR_NAMESPACE_OPEN_SCOPE

TF_INSTANTIATE_SINGLETON(UsdProfilesCapabilityRegistry);

UsdProfilesCapabilityRegistry::UsdProfilesCapabilityRegistry()
{
    _LoadFromPlugins();
}

/* static */
UsdProfilesCapabilityRegistry&
UsdProfilesCapabilityRegistry::GetInstance()
{
    return TfSingleton<UsdProfilesCapabilityRegistry>::GetInstance();
}

void
UsdProfilesCapabilityRegistry::_LoadFromPlugins()
{
    PlugRegistry& plugReg = PlugRegistry::GetInstance();
    PlugPluginPtrVector allPlugins = plugReg.GetAllPlugins();

    for (const PlugPluginPtr& plugin : allPlugins) {
        // Plugin metadata is the "Info" dict from plugInfo.json
        // (the Plug framework strips the outer Plugins[]/Info wrapper).
        const JsObject& metadata = plugin->GetMetadata();
        const JsObject::const_iterator capsIt = metadata.find("Capabilities");
        if (capsIt == metadata.end() || !capsIt->second.IsObject()) {
            continue;
        }

        const JsObject& caps = capsIt->second.GetJsObject();
        for (const auto& entry : caps) {
            const std::string& capIdStr = entry.first;
            if (!entry.second.IsObject()) {
                TF_WARN("UsdProfilesCapabilityRegistry: Capability entry '%s' "
                        "in plugin '%s' is not an object; skipping.",
                        capIdStr.c_str(), plugin->GetName().c_str());
                continue;
            }

            const TfToken capId(capIdStr);
            _CapabilityInfo info;
            info.id = capId;

            const JsObject& capObj = entry.second.GetJsObject();

            // docstring
            const JsObject::const_iterator docIt = capObj.find("docstring");
            if (docIt != capObj.end() && docIt->second.IsString()) {
                info.docstring = docIt->second.GetString();
            }

            // predecessors
            const JsObject::const_iterator predIt = capObj.find("predecessors");
            if (predIt != capObj.end() && predIt->second.IsArray()) {
                for (const JsValue& predVal : predIt->second.GetJsArray()) {
                    if (predVal.IsString()) {
                        info.predecessors.push_back(TfToken(predVal.GetString()));
                    }
                }
            }

            // isProfile
            const JsObject::const_iterator profileIt = capObj.find("isProfile");
            if (profileIt != capObj.end() && profileIt->second.IsBool()) {
                info.isProfile = profileIt->second.GetBool();
            }

            // validators
            const JsObject::const_iterator valIt = capObj.find("validators");
            if (valIt != capObj.end() && valIt->second.IsArray()) {
                for (const JsValue& valVal : valIt->second.GetJsArray()) {
                    if (valVal.IsString()) {
                        info.validators.push_back(TfToken(valVal.GetString()));
                    }
                }
            }

            if (_capabilities.count(capId)) {
                TF_WARN("UsdProfilesCapabilityRegistry: Duplicate capability "
                        "'%s' from plugin '%s'; ignoring duplicate.",
                        capIdStr.c_str(), plugin->GetName().c_str());
            } else {
                _capabilities[capId] = std::move(info);
            }
        }
    }
}

bool
UsdProfilesCapabilityRegistry::IsCapability(const TfToken& id) const
{
    return _capabilities.count(id) > 0;
}

bool
UsdProfilesCapabilityRegistry::IsProfile(const TfToken& id) const
{
    const auto it = _capabilities.find(id);
    return it != _capabilities.end() && it->second.isProfile;
}

TfTokenVector
UsdProfilesCapabilityRegistry::GetPredecessors(const TfToken& id) const
{
    const auto it = _capabilities.find(id);
    if (it == _capabilities.end()) {
        return {};
    }
    return it->second.predecessors;
}

TfTokenVector
UsdProfilesCapabilityRegistry::GetTransitivePredecessors(const TfToken& id) const
{
    if (!IsCapability(id)) {
        return {};
    }

    // BFS over the predecessor DAG.
    TfTokenVector result;
    std::set<TfToken> visited;
    std::vector<TfToken> queue;

    // Seed the queue with direct predecessors.
    for (const TfToken& pred : GetPredecessors(id)) {
        if (visited.insert(pred).second) {
            queue.push_back(pred);
        }
    }

    while (!queue.empty()) {
        TfToken current = queue.back();
        queue.pop_back();
        result.push_back(current);

        for (const TfToken& pred : GetPredecessors(current)) {
            if (visited.insert(pred).second) {
                queue.push_back(pred);
            }
        }
    }

    return result;
}

std::string
UsdProfilesCapabilityRegistry::GetDocstring(const TfToken& id) const
{
    const auto it = _capabilities.find(id);
    if (it == _capabilities.end()) {
        return {};
    }
    return it->second.docstring;
}

TfTokenVector
UsdProfilesCapabilityRegistry::GetAllCapabilities() const
{
    TfTokenVector result;
    result.reserve(_capabilities.size());
    for (const auto& entry : _capabilities) {
        result.push_back(entry.first);
    }
    return result;
}

TfTokenVector
UsdProfilesCapabilityRegistry::GetAllProfiles() const
{
    TfTokenVector result;
    for (const auto& entry : _capabilities) {
        if (entry.second.isProfile) {
            result.push_back(entry.first);
        }
    }
    return result;
}

TfTokenVector
UsdProfilesCapabilityRegistry::GetValidators(const TfToken& id) const
{
    const auto it = _capabilities.find(id);
    if (it == _capabilities.end()) {
        return {};
    }
    return it->second.validators;
}

TfTokenVector
UsdProfilesCapabilityRegistry::GetAllValidatorsForCapability(
    const TfToken& id) const
{
    if (!IsCapability(id)) {
        return {};
    }

    // Collect validators from this capability and all transitive predecessors.
    std::set<TfToken> seen;
    TfTokenVector result;

    // Add validators from the capability itself.
    for (const TfToken& v : GetValidators(id)) {
        if (seen.insert(v).second) {
            result.push_back(v);
        }
    }

    // Add validators from all transitive predecessors.
    for (const TfToken& pred : GetTransitivePredecessors(id)) {
        for (const TfToken& v : GetValidators(pred)) {
            if (seen.insert(v).second) {
                result.push_back(v);
            }
        }
    }

    return result;
}

PXR_NAMESPACE_CLOSE_SCOPE
