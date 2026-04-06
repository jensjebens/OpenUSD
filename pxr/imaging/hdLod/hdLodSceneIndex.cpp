//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#include "pxr/imaging/hdLod/hdLodSceneIndex.h"

#include "pxr/imaging/hd/dataSource.h"
#include "pxr/imaging/hd/dataSourceTypeDefs.h"
#include "pxr/imaging/hd/retainedDataSource.h"
#include "pxr/imaging/hd/sceneIndex.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/visibilitySchema.h"
#include "pxr/imaging/hd/xformSchema.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    // LodGroupAPI relationship name
    (lodItems)

    // LodDistanceHeuristicAPI attribute base names
    // Attributes follow the pattern:
    //   lod:Heuristic:<domain>:distance:minThresholds
    //   lod:Heuristic:<domain>:distance:maxThresholds
    ((lodHeuristicPrefix, "lod:Heuristic"))
    ((distanceMinSuffix, "distance:minThresholds"))
    ((distanceMaxSuffix, "distance:maxThresholds"))

    // apiSchemas token for reading schema metadata
    (apiSchemas)

    // Schema names
    ((lodGroupAPI, "LodGroupAPI"))
    ((lodItemAPI,  "LodItemAPI"))
    ((lodDistanceHeuristicAPI, "LodDistanceHeuristicAPI"))

    // Render settings active camera
    (renderSettings)
    (activeCamera)
    ((camera, "camera"))
);

// ---------------------------------------------------------------------------
// _InvisibleDataSource
//
// Wraps an existing prim data source and overrides the visibility schema to
// return false.
// ---------------------------------------------------------------------------
namespace {

class _InvisibleDataSource : public HdContainerDataSource
{
public:
    HD_DECLARE_DATASOURCE(_InvisibleDataSource);

    _InvisibleDataSource(HdContainerDataSourceHandle const &input)
        : _input(input)
    {
        // Build the invisible visibility container once.
        _visibilityDs = HdVisibilitySchema::BuildRetained(
            HdRetainedTypedSampledDataSource<bool>::New(false));
    }

    TfTokenVector GetNames() override
    {
        if (!_input) {
            return { HdVisibilitySchemaTokens->visibility };
        }
        TfTokenVector names = _input->GetNames();
        const TfToken &visToken = HdVisibilitySchemaTokens->visibility;
        if (std::find(names.begin(), names.end(), visToken) == names.end()) {
            names.push_back(visToken);
        }
        return names;
    }

    HdDataSourceBaseHandle Get(const TfToken &name) override
    {
        if (name == HdVisibilitySchemaTokens->visibility) {
            return _visibilityDs;
        }
        if (_input) {
            return _input->Get(name);
        }
        return nullptr;
    }

private:
    HdContainerDataSourceHandle _input;
    HdContainerDataSourceHandle _visibilityDs;
};

HD_DECLARE_DATASOURCE_HANDLES(_InvisibleDataSource);

} // anonymous namespace

// ---------------------------------------------------------------------------
// HdLodSceneIndex
// ---------------------------------------------------------------------------

/* static */
HdLodSceneIndexRefPtr
HdLodSceneIndex::New(const HdSceneIndexBaseRefPtr &inputSceneIndex)
{
    return TfCreateRefPtr(new HdLodSceneIndex(inputSceneIndex));
}

HdLodSceneIndex::HdLodSceneIndex(
    const HdSceneIndexBaseRefPtr &inputSceneIndex)
    : HdSingleInputFilteringSceneIndexBase(inputSceneIndex)
{
    _RebuildCache();
    _EvaluateLod();
}

HdSceneIndexPrim
HdLodSceneIndex::GetPrim(const SdfPath &primPath) const
{
    HdSceneIndexPrim prim = _GetInputSceneIndex()->GetPrim(primPath);
    if (_hiddenRenderables.count(primPath)) {
        prim.dataSource =
            _InvisibleDataSource::New(prim.dataSource);
    }
    return prim;
}

SdfPathVector
HdLodSceneIndex::GetChildPrimPaths(const SdfPath &primPath) const
{
    return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
}

void
HdLodSceneIndex::_PrimsAdded(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::AddedPrimEntries &entries)
{
    // Rebuild cache whenever prims are added, as new LodGroup/LodItem prims
    // may have appeared.
    _RebuildCache();
    _EvaluateLod();
    _SendPrimsAdded(entries);
}

void
HdLodSceneIndex::_PrimsRemoved(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::RemovedPrimEntries &entries)
{
    // Clean up any removed paths from cache structures.
    for (const auto &entry : entries) {
        const SdfPath &path = entry.primPath;
        _lodGroups.erase(path);
        _descendantCache.erase(path);
        _hiddenRenderables.erase(path);
    }
    _RebuildCache();
    _EvaluateLod();
    _SendPrimsRemoved(entries);
}

void
HdLodSceneIndex::_PrimsDirtied(
    const HdSceneIndexBase &sender,
    const HdSceneIndexObserver::DirtiedPrimEntries &entries)
{
    // Re-evaluate LOD if xform or relevant attributes may have changed.
    bool needsReevaluation = false;
    for (const auto &entry : entries) {
        // Re-evaluate if any xform is dirtied (camera may have moved)
        // or if a lod attribute is dirtied.
        for (const auto &locator : entry.dirtyLocators) {
            const TfToken &first = locator.IsEmpty()
                ? TfToken()
                : locator.GetFirstElement();
            if (first == HdXformSchemaTokens->xform ||
                first == TfToken("primvars") ||
                first == TfToken("lod")) {
                needsReevaluation = true;
                break;
            }
        }
        if (needsReevaluation) {
            break;
        }
    }

    if (needsReevaluation) {
        _EvaluateLod();
    }

    _SendPrimsDirtied(entries);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

bool
HdLodSceneIndex::_IsRenderable(const TfToken &primType)
{
    // HdPrimTypeIsGprim covers all Rprim geometry prim types (mesh,
    // basisCurves, points, volume, implicits, etc.).
    return HdPrimTypeIsGprim(primType);
}

void
HdLodSceneIndex::_CollectRenderables(
    const SdfPath &primPath,
    std::vector<SdfPath> &out) const
{
    const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();
    HdSceneIndexPrim prim = input->GetPrim(primPath);
    if (_IsRenderable(prim.primType)) {
        out.push_back(primPath);
    }
    for (const SdfPath &child : input->GetChildPrimPaths(primPath)) {
        _CollectRenderables(child, out);
    }
}

// Helper: check if a token vector data source (apiSchemas) contains a token.
static bool
_HasApiSchema(const HdContainerDataSourceHandle &ds, const TfToken &schema)
{
    if (!ds) {
        return false;
    }
    HdDataSourceBaseHandle apiDs = ds->Get(_tokens->apiSchemas);
    if (!apiDs) {
        return false;
    }
    HdTokenArrayDataSourceHandle tokenArrayDs =
        HdTokenArrayDataSource::Cast(apiDs);
    if (!tokenArrayDs) {
        return false;
    }
    VtArray<TfToken> schemas = tokenArrayDs->GetTypedValue(0.0f);
    for (const TfToken &t : schemas) {
        // Match prefix: token may be "LodGroupAPI" or "LodGroupAPI:instance"
        if (t == schema ||
            TfStringStartsWith(t.GetString(), schema.GetString() + ":")) {
            return true;
        }
    }
    return false;
}

// Helper: get a float array attribute from a prim's data source.
// USD attributes typically come through as primvars or as top-level keys.
// We try both the top-level key and the "primvars:<name>" key.
static VtArray<float>
_GetFloatArrayAttr(const HdContainerDataSourceHandle &ds,
                   const TfToken &attrName)
{
    if (!ds) {
        return {};
    }
    // Try direct key first
    HdDataSourceBaseHandle attrDs = ds->Get(attrName);
    if (!attrDs) {
        // Try primvars:<name>
        HdDataSourceBaseHandle primvarsDs = ds->Get(TfToken("primvars"));
        if (HdContainerDataSourceHandle pvContainer =
                HdContainerDataSource::Cast(primvarsDs)) {
            HdDataSourceBaseHandle pv = pvContainer->Get(attrName);
            if (HdContainerDataSourceHandle pvCont =
                    HdContainerDataSource::Cast(pv)) {
                attrDs = pvCont->Get(TfToken("primvarValue"));
                if (!attrDs) {
                    attrDs = pvCont->Get(TfToken("value"));
                }
            }
        }
    }
    if (!attrDs) {
        return {};
    }
    HdFloatArrayDataSourceHandle floatArrayDs =
        HdFloatArrayDataSource::Cast(attrDs);
    if (floatArrayDs) {
        return floatArrayDs->GetTypedValue(0.0f);
    }
    return {};
}

// Helper: get SdfPath array attribute (for relationships like lodItems).
static SdfPathVector
_GetPathArrayAttr(const HdContainerDataSourceHandle &ds,
                  const TfToken &attrName)
{
    if (!ds) {
        return {};
    }
    HdDataSourceBaseHandle attrDs = ds->Get(attrName);
    if (!attrDs) {
        return {};
    }
    // Relationships come through as SdfPathArray typed data source
    using PathArrayDs = HdTypedSampledDataSource<VtArray<SdfPath>>;
    if (PathArrayDs::Handle pathDs = PathArrayDs::Cast(attrDs)) {
        VtArray<SdfPath> paths = pathDs->GetTypedValue(0.0f);
        return SdfPathVector(paths.begin(), paths.end());
    }
    return {};
}

void
HdLodSceneIndex::_RebuildCache()
{
    _lodGroups.clear();
    _descendantCache.clear();

    const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();

    // Walk the entire scene to find LodGroup prims.
    // We do a BFS/DFS from the pseudo-root.
    std::vector<SdfPath> toVisit;
    toVisit.push_back(SdfPath::AbsoluteRootPath());

    while (!toVisit.empty()) {
        SdfPath path = toVisit.back();
        toVisit.pop_back();

        HdSceneIndexPrim prim = input->GetPrim(path);

        if (_HasApiSchema(prim.dataSource, _tokens->lodGroupAPI)) {
            // Read lodItems relationship
            SdfPathVector lodItems =
                _GetPathArrayAttr(prim.dataSource, _tokens->lodItems);
            if (!lodItems.empty()) {
                _lodGroups[path] = std::move(lodItems);
            }
        }

        for (const SdfPath &child : input->GetChildPrimPaths(path)) {
            toVisit.push_back(child);
        }
    }

    // For each LodItem across all groups, collect renderable descendants.
    for (const auto &groupEntry : _lodGroups) {
        for (const SdfPath &itemPath : groupEntry.second) {
            if (_descendantCache.find(itemPath) == _descendantCache.end()) {
                std::vector<SdfPath> renderables;
                // Collect renderables under this lod item but skip any sub-
                // LodGroup prims (they handle themselves hierarchically).
                for (const SdfPath &child :
                        input->GetChildPrimPaths(itemPath)) {
                    _CollectRenderables(child, renderables);
                }
                _descendantCache[itemPath] = std::move(renderables);
            }
        }
    }
}

GfVec3d
HdLodSceneIndex::_GetCameraPosition() const
{
    const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();

    // Try to find camera path from render settings if not already cached.
    if (_cameraPath.IsEmpty()) {
        // Look for a renderSettings prim under /Render or root.
        SdfPathVector rsChildren =
            input->GetChildPrimPaths(SdfPath::AbsoluteRootPath());
        for (const SdfPath &child : rsChildren) {
            HdSceneIndexPrim prim = input->GetPrim(child);
            if (prim.primType == TfToken("renderSettings")) {
                if (prim.dataSource) {
                    HdDataSourceBaseHandle camDs =
                        prim.dataSource->Get(_tokens->activeCamera);
                    if (!camDs) {
                        camDs = prim.dataSource->Get(_tokens->camera);
                    }
                    if (camDs) {
                        using PathDs = HdTypedSampledDataSource<SdfPath>;
                        if (PathDs::Handle pd = PathDs::Cast(camDs)) {
                            _cameraPath = pd->GetTypedValue(0.0f);
                        }
                    }
                }
                break;
            }
        }
    }

    if (!_cameraPath.IsEmpty()) {
        HdSceneIndexPrim camPrim = input->GetPrim(_cameraPath);
        if (camPrim.dataSource) {
            HdXformSchema xformSchema =
                HdXformSchema::GetFromParent(camPrim.dataSource);
            if (HdMatrixDataSourceHandle matDs = xformSchema.GetMatrix()) {
                GfMatrix4d mat = matDs->GetTypedValue(0.0f);
                return mat.ExtractTranslation();
            }
        }
    }

    return GfVec3d(0.0, 0.0, 0.0);
}

void
HdLodSceneIndex::_EvaluateLod()
{
    const HdSceneIndexBaseRefPtr &input = _GetInputSceneIndex();
    GfVec3d cameraPos = _GetCameraPosition();

    std::unordered_set<SdfPath, SdfPath::Hash> newHidden;

    for (const auto &groupEntry : _lodGroups) {
        const SdfPath &groupPath = groupEntry.first;
        const std::vector<SdfPath> &lodItems = groupEntry.second;

        if (lodItems.empty()) {
            continue;
        }

        // Get the world position of the group prim for distance computation.
        GfVec3d groupPos(0.0, 0.0, 0.0);
        {
            HdSceneIndexPrim groupPrim = input->GetPrim(groupPath);
            if (groupPrim.dataSource) {
                HdXformSchema xformSchema =
                    HdXformSchema::GetFromParent(groupPrim.dataSource);
                if (HdMatrixDataSourceHandle matDs = xformSchema.GetMatrix()) {
                    groupPos = matDs->GetTypedValue(0.0f).ExtractTranslation();
                }
            }
        }

        double distance = (cameraPos - groupPos).GetLength();

        // Find the active LOD item based on distance thresholds.
        // Default: use the last LOD item (lowest detail) if no thresholds match.
        int activeIndex = static_cast<int>(lodItems.size()) - 1;

        for (int i = 0; i < static_cast<int>(lodItems.size()); ++i) {
            const SdfPath &itemPath = lodItems[i];
            HdSceneIndexPrim itemPrim = input->GetPrim(itemPath);
            if (!itemPrim.dataSource) {
                continue;
            }

            // Look for LodDistanceHeuristicAPI on the item prim.
            // Try to find any distance:minThresholds / maxThresholds attribute.
            // Attribute names follow pattern:
            //   lod:Heuristic:<domain>:distance:minThresholds
            // We try a few common forms, or just look up all names.
            TfTokenVector names = itemPrim.dataSource->GetNames();
            VtArray<float> minThresholds, maxThresholds;

            for (const TfToken &name : names) {
                const std::string &ns = name.GetString();
                if (TfStringStartsWith(ns,
                        _tokens->lodHeuristicPrefix.GetString())) {
                    if (TfStringEndsWith(ns,
                            _tokens->distanceMinSuffix.GetString())) {
                        minThresholds = _GetFloatArrayAttr(
                            itemPrim.dataSource, name);
                    } else if (TfStringEndsWith(ns,
                            _tokens->distanceMaxSuffix.GetString())) {
                        maxThresholds = _GetFloatArrayAttr(
                            itemPrim.dataSource, name);
                    }
                }
            }

            // Also check inside primvars for these attributes.
            if (minThresholds.empty() || maxThresholds.empty()) {
                HdDataSourceBaseHandle pvDs =
                    itemPrim.dataSource->Get(TfToken("primvars"));
                if (HdContainerDataSourceHandle pvCont =
                        HdContainerDataSource::Cast(pvDs)) {
                    TfTokenVector pvNames = pvCont->GetNames();
                    for (const TfToken &name : pvNames) {
                        const std::string &ns = name.GetString();
                        if (TfStringStartsWith(ns,
                                _tokens->lodHeuristicPrefix.GetString())) {
                            HdDataSourceBaseHandle pvEntry = pvCont->Get(name);
                            if (HdContainerDataSourceHandle pvc =
                                    HdContainerDataSource::Cast(pvEntry)) {
                                HdDataSourceBaseHandle valDs =
                                    pvc->Get(TfToken("primvarValue"));
                                if (!valDs) {
                                    valDs = pvc->Get(TfToken("value"));
                                }
                                if (valDs) {
                                    HdFloatArrayDataSourceHandle faDs =
                                        HdFloatArrayDataSource::Cast(valDs);
                                    if (faDs) {
                                        if (TfStringEndsWith(ns,
                                                _tokens->distanceMinSuffix
                                                    .GetString())) {
                                            minThresholds =
                                                faDs->GetTypedValue(0.0f);
                                        } else if (TfStringEndsWith(ns,
                                                _tokens->distanceMaxSuffix
                                                    .GetString())) {
                                            maxThresholds =
                                                faDs->GetTypedValue(0.0f);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            if (!minThresholds.empty() && !maxThresholds.empty()) {
                // Use the first pair of thresholds for this LOD item.
                // minThresholds[0] is the near (min) distance to activate this
                // LOD, maxThresholds[0] is the far (max) distance.
                // Hysteresis: activate when distance < maxThreshold,
                //             deactivate when distance > minThreshold.
                float minT = minThresholds[0];
                float maxT = maxThresholds[0];
                if (distance >= static_cast<double>(minT) &&
                    distance <  static_cast<double>(maxT)) {
                    activeIndex = i;
                    break;
                }
            } else {
                // No thresholds: first item is always active by default.
                activeIndex = 0;
                break;
            }
        }

        // Hide all non-active items' renderables.
        for (int i = 0; i < static_cast<int>(lodItems.size()); ++i) {
            if (i == activeIndex) {
                continue;
            }
            const SdfPath &itemPath = lodItems[i];
            auto cacheIt = _descendantCache.find(itemPath);
            if (cacheIt != _descendantCache.end()) {
                for (const SdfPath &rPath : cacheIt->second) {
                    newHidden.insert(rPath);
                }
            }
        }
    }

    // Determine what changed and send dirty notices.
    HdSceneIndexObserver::DirtiedPrimEntries dirtyEntries;
    static const HdDataSourceLocatorSet visLocators{
        HdVisibilitySchema::GetDefaultLocator()};

    // Prims that were hidden and are now visible (or vice versa).
    for (const SdfPath &path : _hiddenRenderables) {
        if (!newHidden.count(path)) {
            dirtyEntries.push_back({path, visLocators});
        }
    }
    for (const SdfPath &path : newHidden) {
        if (!_hiddenRenderables.count(path)) {
            dirtyEntries.push_back({path, visLocators});
        }
    }

    _hiddenRenderables = std::move(newHidden);

    if (!dirtyEntries.empty()) {
        _SendPrimsDirtied(dirtyEntries);
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
