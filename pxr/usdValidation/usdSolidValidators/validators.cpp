//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/base/gf/vec2d.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/valueTypeName.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usdSolid/brepArray.h"
#include "pxr/usd/usdSolid/tokens.h"
#include "pxr/usdValidation/usdSolidValidators/validatorTokens.h"
#include "pxr/usdValidation/usdValidation/error.h"
#include "pxr/usdValidation/usdValidation/registry.h"
#include "pxr/usdValidation/usdValidation/timeRange.h"
#include "pxr/usdValidation/usdValidation/validator.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

// Tolerance used for unit-length and orthogonality checks on analytic
// axis frames. A frame is a unit-vector triad regardless of whether it defines
// a surface (cylinder/cone/sphere/torus/plane axis+refDirection) or a curve
// (circle/ellipse axis+refDirection): the same 1e-6 bound applies to both so a
// producer that authors a conformant frame is not flagged on one shape family
// and cleared on another. (Cross-reference: the curve-frame checks in
// _CheckCurveUnit / _CheckCurveOrtho reuse this constant -- see BA.53x/54x/55x.)
constexpr double _FrameTol = 1e-6;
// pi/2, used to bound cone semiAngle.
constexpr double _HalfPi = 1.5707963267948966;
// Fallback intersection tolerance (a 3D length) used only when a BrepArray
// authors no positive brep:intersectTol3d. Real CAD producers carry positional
// noise on the order of a micron (STEP files commonly declare uncertainty
// ~1e-6..1e-5 model units), so the older 1e-9 default was tighter than any real
// producer and turned benign endpoint/degeneracy noise into systematic BA.230 /
// BA.240 false positives on float-pathed real files. 1e-6 is the reader-side
// analogue of the builder's weld floor policy max(1e-4, 10*tol) in
// brepBuilder.cpp: both express "how far apart two points may be before we treat
// them as distinct"; the builder is deliberately looser (it must weld a mesh),
// the validator deliberately tighter (it only reports, and conformant assets
// author their own tol so this fallback never fires on them). See also the
// tolerance-policy note in docs/usdsolid-debt-register.md.
// (_FirstAuthoredIntersectTol3d(), below _Read, packages the "authored tol else
// this fallback" resolution the tolerance-aware rules share.)
constexpr double _FallbackIntersectTol3d = 1e-6;

UsdValidationErrorSites
_PrimSites(const UsdPrim &prim)
{
    return { UsdValidationErrorSite(prim.GetStage(), prim.GetPath()) };
}

template <class T>
VtArray<T>
_Read(const UsdAttribute &attr)
{
    VtArray<T> value;
    if (attr) {
        attr.Get(&value);
    }
    return value;
}

// The first authored, finite, positive brep:intersectTol3d, or the reader-side
// _FallbackIntersectTol3d. Centralizes the fallback expression that BA.230,
// BA.240 and BA.375 all need (previously
// "(!tol.empty() && tol[0] > 0.0) ? tol[0] : 1e-9" copy-pasted at three sites,
// each with the fallback magnitude unexplained). A per-Brep tolerance would need
// per-edge Brep attribution, which the flat data does not carry; the first
// Brep's tolerance is exact for the common single-Brep case.
double
_FirstAuthoredIntersectTol3d(const UsdSolidBrepArray &brep)
{
    const VtArray<double> tol
        = _Read<double>(brep.GetBrepIntersectTol3dAttr());
    return (!tol.empty() && tol[0] > 0.0 && std::isfinite(tol[0]))
        ? tol[0]
        : _FallbackIntersectTol3d;
}

size_t
_Sum(const VtArray<unsigned int> &values)
{
    size_t sum = 0;
    for (const unsigned int v : values) {
        sum += v;
    }
    return sum;
}

void
_CheckSize(const UsdPrim &prim, const char *attrName, size_t actual,
           size_t expected, const std::string &expectedDesc,
           const TfToken &errorName, UsdValidationErrorVector *errors)
{
    if (actual != expected) {
        errors->emplace_back(
            errorName, UsdValidationErrorType::Error, _PrimSites(prim),
            TfStringPrintf(
                "BrepArray <%s>: attribute %s has size %zu but expected %zu "
                "(%s).",
                prim.GetPath().GetText(), attrName, actual, expected,
                expectedDesc.c_str()));
    }
}

// Renders an allowed-token list the way the Python brep_validator prints it
// ("['solidRegion', 'voidRegion']"), so the native message for a token-validity
// rule reads the same as its Python counterpart. The allowed sets below are
// vectors because the order they are written in reaches the reader in this
// tail; a std::set<TfToken> would sort them lexicographically and the two
// validators would print the same set in two different orders.
std::string
_FormatAllowedTokens(const std::vector<TfToken> &allowed)
{
    std::vector<std::string> quoted;
    quoted.reserve(allowed.size());
    for (const TfToken &token : allowed) {
        quoted.push_back(TfStringPrintf("'%s'", token.GetText()));
    }
    return "[" + TfStringJoin(quoted, ", ") + "]";
}

// The token-validity rules BA.075 / BA.090 / BA.110 / BA.130 / BA.135 /
// BA.190 / BA.195 / BA.245 / BA.260 / BA.315 all reduce to "every entry of this
// token[] attribute is drawn from this fixed set". Python reports one finding
// per attribute (naming every offending index) instead of one finding per
// offending index; this reproduces that grouping so a file with N bad tokens in
// one attribute produces one finding here and one finding there.
void
_CheckAllowedTokens(const UsdPrim &prim, const VtArray<TfToken> &values,
                    const std::vector<TfToken> &allowed, const char *ruleId,
                    const char *attrName, const char *itemName,
                    const TfToken &errorName,
                    UsdValidationErrorVector *errors)
{
    std::vector<size_t> invalid;
    for (size_t i = 0; i < values.size(); ++i) {
        if (std::find(allowed.begin(), allowed.end(), values[i])
            == allowed.end()) {
            invalid.push_back(i);
        }
    }
    if (invalid.empty()) {
        return;
    }

    const std::string allowedDesc = _FormatAllowedTokens(allowed);

    if (invalid.size() == 1) {
        const size_t i = invalid.front();
        errors->emplace_back(
            errorName, UsdValidationErrorType::Error, _PrimSites(prim),
            TfStringPrintf(
                "[%s] BrepArray <%s>: %s[%zu] has invalid value '%s' for %s "
                "#%zu. Allowed values are %s.",
                ruleId, prim.GetPath().GetText(), attrName, i,
                values[i].GetText(), itemName, i, allowedDesc.c_str()));
        return;
    }

    std::vector<std::string> details;
    details.reserve(invalid.size());
    for (const size_t i : invalid) {
        details.push_back(TfStringPrintf("[%zu]='%s'", i,
                                         values[i].GetText()));
    }
    errors->emplace_back(
        errorName, UsdValidationErrorType::Error, _PrimSites(prim),
        TfStringPrintf(
            "[%s] BrepArray <%s>: %s has invalid values at indices: %s. "
            "Allowed values are %s.",
            ruleId, prim.GetPath().GetText(), attrName,
            TfStringJoin(details, ", ").c_str(), allowedDesc.c_str()));
}

// -------------------------------------------------------------------------- //
// BrepArrayStructure                                                         //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayStructure(const UsdPrim &usdPrim,
                    const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    const UsdAttribute tolAttr = brep.GetBrepIntersectTol3dAttr();
    const UsdAttribute extentAttr = brep.GetBrepExtentAttr();
    const UsdAttribute regionCountAttr = brep.GetBrepRegionCountAttr();

    UsdValidationErrorVector errors;

    // BA.005: required Brep schema attributes must be authored.
    std::vector<std::string> missing;
    if (!tolAttr.HasAuthoredValue()) {
        missing.emplace_back("brep:intersectTol3d");
    }
    if (!extentAttr.HasAuthoredValue()) {
        missing.emplace_back("brep:extent");
    }
    if (!regionCountAttr.HasAuthoredValue()) {
        missing.emplace_back("brep:regionCount");
    }
    if (!missing.empty()) {
        errors.emplace_back(
            UsdSolidValidationErrorNameTokens->missingBrepAttributes,
            UsdValidationErrorType::Error, _PrimSites(usdPrim),
            TfStringPrintf(
                "BrepArray <%s> is missing required brep attribute(s): %s.",
                usdPrim.GetPath().GetText(),
                TfStringJoin(missing, ", ").c_str()));
    }

    const VtArray<double> tol = _Read<double>(tolAttr);
    const VtArray<GfVec3d> extent = _Read<GfVec3d>(extentAttr);
    const VtArray<unsigned int> regionCount
        = _Read<unsigned int>(regionCountAttr);

    // BA.000 / BA.020: array sizes must be consistent with the number of
    // Breps. brep:regionCount and brep:intersectTol3d have one entry per Brep;
    // brep:extent has two (min, max corner) entries per Brep.
    const size_t numBreps = regionCount.size();
    if (tol.size() != numBreps || extent.size() != 2 * numBreps) {
        errors.emplace_back(
            UsdSolidValidationErrorNameTokens->inconsistentBrepArraySizes,
            UsdValidationErrorType::Error, _PrimSites(usdPrim),
            TfStringPrintf(
                "BrepArray <%s>: brep-level array sizes are inconsistent. For "
                "%zu Brep(s) (brep:regionCount size), expected "
                "brep:intersectTol3d size %zu (got %zu) and brep:extent size "
                "%zu (got %zu).",
                usdPrim.GetPath().GetText(), numBreps, numBreps, tol.size(),
                2 * numBreps, extent.size()));
    }

    // BA.010: brep:intersectTol3d values must be positive and finite. A
    // non-finite tolerance (NaN/Inf) silently breaks every tolerance-based rule
    // downstream: NaN fails every comparison (so an authored NaN slips past the
    // <= 0.0 test here), and the shared tolerance resolution
    // (_FirstAuthoredIntersectTol3d, used by BA.230/240/375) would carry a
    // poisoned tolerance into endpoint/degeneracy checks -- which is why that
    // helper additionally requires std::isfinite before accepting the authored
    // value. Flag the finiteness violation
    // explicitly and separately from the non-positive case (a NaN is neither
    // "positive" nor "<= 0.0", so the ordering test alone cannot catch it).
    for (size_t i = 0; i < tol.size(); ++i) {
        if (!std::isfinite(tol[i])) {
            errors.emplace_back(
                UsdSolidValidationErrorNameTokens->nonFiniteIntersectTol3d,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "BrepArray <%s>: brep:intersectTol3d[%zu] = %g is not "
                    "finite; the intersection tolerance must be a finite "
                    "positive number.",
                    usdPrim.GetPath().GetText(), i, tol[i]));
        } else if (tol[i] <= 0.0) {
            errors.emplace_back(
                UsdSolidValidationErrorNameTokens->nonPositiveIntersectTol3d,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "BrepArray <%s>: brep:intersectTol3d[%zu] = %g is not "
                    "positive.",
                    usdPrim.GetPath().GetText(), i, tol[i]));
        }
    }

    // BA.025 / BA.030 / BA.035: each brep:extent bounding box corner pair must
    // be ordered min <= max on every axis.
    const char *const axisNames[3] = { "X", "Y", "Z" };
    for (size_t box = 0; 2 * box + 1 < extent.size(); ++box) {
        const GfVec3d &mn = extent[2 * box];
        const GfVec3d &mx = extent[2 * box + 1];
        for (int a = 0; a < 3; ++a) {
            if (mn[a] > mx[a]) {
                errors.emplace_back(
                    UsdSolidValidationErrorNameTokens->invalidExtentOrder,
                    UsdValidationErrorType::Error, _PrimSites(usdPrim),
                    TfStringPrintf(
                        "BrepArray <%s>: brep:extent for Brep %zu has %smin "
                        "(%g) > %smax (%g).",
                        usdPrim.GetPath().GetText(), box, axisNames[a],
                        mn[a], axisNames[a], mx[a]));
            }
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayTopology                                                          //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayTopology(const UsdPrim &usdPrim,
                   const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    const UsdAttribute regionCountAttr = brep.GetBrepRegionCountAttr();
    if (!regionCountAttr.HasAuthoredValue()) {
        // The BrepArrayStructure validator reports the missing attribute; we
        // cannot derive the topology sizes without it.
        return {};
    }

    UsdValidationErrorVector errors;

    const VtArray<unsigned int> regionCount
        = _Read<unsigned int>(regionCountAttr);

    // Region (BA.065): sized by sum(brep:regionCount).
    const size_t numRegions = _Sum(regionCount);
    const VtArray<unsigned int> regionShellCount
        = _Read<unsigned int>(brep.GetRegionShellCountAttr());
    const VtArray<TfToken> regionType
        = _Read<TfToken>(brep.GetRegionTypeAttr());
    const std::string regionsDesc
        = TfStringPrintf("sum of brep:regionCount = %zu", numRegions);
    _CheckSize(usdPrim, "region:shellCount", regionShellCount.size(),
               numRegions, regionsDesc,
               UsdSolidValidationErrorNameTokens->inconsistentRegionArraySizes,
               &errors);
    _CheckSize(usdPrim, "region:type", regionType.size(), numRegions,
               regionsDesc,
               UsdSolidValidationErrorNameTokens->inconsistentRegionArraySizes,
               &errors);

    // Shell (BA.080): sized by sum(region:shellCount).
    const size_t numShells = _Sum(regionShellCount);
    const VtArray<unsigned int> shellFaceuseCount
        = _Read<unsigned int>(brep.GetShellFaceuseCountAttr());
    const VtArray<unsigned int> shellWireEdgeCount
        = _Read<unsigned int>(brep.GetShellWireEdgeCountAttr());
    const VtArray<TfToken> shellPointType
        = _Read<TfToken>(brep.GetShellPointTypeAttr());
    const std::string shellsDesc
        = TfStringPrintf("sum of region:shellCount = %zu", numShells);
    _CheckSize(usdPrim, "shell:faceuseCount", shellFaceuseCount.size(),
               numShells, shellsDesc,
               UsdSolidValidationErrorNameTokens->inconsistentShellArraySizes,
               &errors);
    _CheckSize(usdPrim, "shell:wireEdgeCount", shellWireEdgeCount.size(),
               numShells, shellsDesc,
               UsdSolidValidationErrorNameTokens->inconsistentShellArraySizes,
               &errors);
    _CheckSize(usdPrim, "shell:pointType", shellPointType.size(), numShells,
               shellsDesc,
               UsdSolidValidationErrorNameTokens->inconsistentShellArraySizes,
               &errors);

    // Faceuse (BA.100): sized by sum(shell:faceuseCount).
    const size_t numFaceuses = _Sum(shellFaceuseCount);
    const VtArray<unsigned int> faceuseFaceIndex
        = _Read<unsigned int>(brep.GetFaceuseFaceIndexAttr());
    const VtArray<TfToken> faceuseOrientationType
        = _Read<TfToken>(brep.GetFaceuseOrientationTypeAttr());
    const std::string faceusesDesc
        = TfStringPrintf("sum of shell:faceuseCount = %zu", numFaceuses);
    _CheckSize(usdPrim, "faceuse:faceIndex", faceuseFaceIndex.size(),
               numFaceuses, faceusesDesc,
               UsdSolidValidationErrorNameTokens
                   ->inconsistentFaceuseArraySizes,
               &errors);
    _CheckSize(usdPrim, "faceuse:orientationType",
               faceuseOrientationType.size(), numFaceuses, faceusesDesc,
               UsdSolidValidationErrorNameTokens
                   ->inconsistentFaceuseArraySizes,
               &errors);

    // Face (BA.120 / BA.150): faceuses come in pairs, so there are
    // numFaceuses / 2 faces. face:range has two (UVmin, UVmax) entries per
    // face.
    const size_t numFaces = numFaceuses / 2;
    const VtArray<unsigned int> faceLoopCount
        = _Read<unsigned int>(brep.GetFaceLoopCountAttr());
    const VtArray<TfToken> faceSurfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const VtArray<TfToken> faceTrimType
        = _Read<TfToken>(brep.GetFaceTrimTypeAttr());
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());
    const std::string facesDesc
        = TfStringPrintf("faceuse count / 2 = %zu", numFaces);
    _CheckSize(usdPrim, "face:loopCount", faceLoopCount.size(), numFaces,
               facesDesc,
               UsdSolidValidationErrorNameTokens->inconsistentFaceArraySizes,
               &errors);
    _CheckSize(usdPrim, "face:surfaceType", faceSurfaceType.size(), numFaces,
               facesDesc,
               UsdSolidValidationErrorNameTokens->inconsistentFaceArraySizes,
               &errors);
    _CheckSize(usdPrim, "face:trimType", faceTrimType.size(), numFaces,
               facesDesc,
               UsdSolidValidationErrorNameTokens->inconsistentFaceArraySizes,
               &errors);
    _CheckSize(usdPrim, "face:range", faceRange.size(), 2 * numFaces,
               TfStringPrintf("2 * number of faces = %zu", 2 * numFaces),
               UsdSolidValidationErrorNameTokens->inconsistentFaceArraySizes,
               &errors);

    // Loop (BA.165): sized by sum(face:loopCount).
    const size_t numLoops = _Sum(faceLoopCount);
    const VtArray<unsigned int> loopEdgeuseCount
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());
    const VtArray<unsigned int> loopVertexIndex
        = _Read<unsigned int>(brep.GetLoopVertexIndexAttr());
    const std::string loopsDesc
        = TfStringPrintf("sum of face:loopCount = %zu", numLoops);
    _CheckSize(usdPrim, "loop:edgeuseCount", loopEdgeuseCount.size(), numLoops,
               loopsDesc,
               UsdSolidValidationErrorNameTokens->inconsistentLoopArraySizes,
               &errors);
    _CheckSize(usdPrim, "loop:vertexIndex", loopVertexIndex.size(), numLoops,
               loopsDesc,
               UsdSolidValidationErrorNameTokens->inconsistentLoopArraySizes,
               &errors);

    // Edgeuse (BA.180): sized by sum(loop:edgeuseCount).
    const size_t numEdgeuses = _Sum(loopEdgeuseCount);
    const std::string edgeusesDesc
        = TfStringPrintf("sum of loop:edgeuseCount = %zu", numEdgeuses);
    _CheckSize(usdPrim, "edgeuse:edgeIndex",
               _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr()).size(),
               numEdgeuses, edgeusesDesc,
               UsdSolidValidationErrorNameTokens
                   ->inconsistentEdgeuseArraySizes,
               &errors);
    _CheckSize(usdPrim, "edgeuse:orientationType",
               _Read<TfToken>(brep.GetEdgeuseOrientationTypeAttr()).size(),
               numEdgeuses, edgeusesDesc,
               UsdSolidValidationErrorNameTokens
                   ->inconsistentEdgeuseArraySizes,
               &errors);
    _CheckSize(usdPrim, "edgeuse:nextRadialEUIndex",
               _Read<unsigned int>(
                   brep.GetEdgeuseNextRadialEUIndexAttr()).size(),
               numEdgeuses, edgeusesDesc,
               UsdSolidValidationErrorNameTokens
                   ->inconsistentEdgeuseArraySizes,
               &errors);
    _CheckSize(usdPrim, "edgeuse:thisRadialEntryType",
               _Read<TfToken>(
                   brep.GetEdgeuseThisRadialEntryTypeAttr()).size(),
               numEdgeuses, edgeusesDesc,
               UsdSolidValidationErrorNameTokens
                   ->inconsistentEdgeuseArraySizes,
               &errors);

    // Edge (BA.210): the number of edges is defined by edge:curveType. The
    // remaining edge arrays must match, and edge:range has two entries per
    // edge.
    const VtArray<TfToken> edgeCurveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const size_t numEdges = edgeCurveType.size();
    _CheckSize(usdPrim, "edge:vertexIndices",
               _Read<GfVec2i>(brep.GetEdgeVertexIndicesAttr()).size(),
               numEdges,
               TfStringPrintf("number of edges = %zu", numEdges),
               UsdSolidValidationErrorNameTokens->inconsistentEdgeArraySizes,
               &errors);
    _CheckSize(usdPrim, "edge:range",
               _Read<double>(brep.GetEdgeRangeAttr()).size(), 2 * numEdges,
               TfStringPrintf("2 * number of edges = %zu", 2 * numEdges),
               UsdSolidValidationErrorNameTokens->inconsistentEdgeArraySizes,
               &errors);

    // WireEdge (BA.250): sized by sum(shell:wireEdgeCount).
    const size_t numWireEdges = _Sum(shellWireEdgeCount);
    const std::string wireEdgesDesc
        = TfStringPrintf("sum of shell:wireEdgeCount = %zu", numWireEdges);
    _CheckSize(usdPrim, "wireEdge:curveType",
               _Read<TfToken>(brep.GetWireEdgeCurveTypeAttr()).size(),
               numWireEdges, wireEdgesDesc,
               UsdSolidValidationErrorNameTokens
                   ->inconsistentWireEdgeArraySizes,
               &errors);
    _CheckSize(usdPrim, "wireEdge:vertexIndices",
               _Read<GfVec2i>(brep.GetWireEdgeVertexIndicesAttr()).size(),
               numWireEdges, wireEdgesDesc,
               UsdSolidValidationErrorNameTokens
                   ->inconsistentWireEdgeArraySizes,
               &errors);
    _CheckSize(usdPrim, "wireEdge:range",
               _Read<double>(brep.GetWireEdgeRangeAttr()).size(),
               2 * numWireEdges,
               TfStringPrintf("2 * sum of shell:wireEdgeCount = %zu",
                              2 * numWireEdges),
               UsdSolidValidationErrorNameTokens
                   ->inconsistentWireEdgeArraySizes,
               &errors);

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayTokenValues                                                       //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayTokenValues(const UsdPrim &usdPrim,
                      const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    // Each set is listed in the order the Python brep_validator declares it.
    // The membership of all ten sets is taken from the Python implementation
    // and agrees entry-for-entry with the allowedTokens metadata on
    // pxr/usd/usdSolid/schema.usda.
    static const std::vector<TfToken> regionTypes
        = { TfToken("solidRegion"), TfToken("voidRegion") };
    static const std::vector<TfToken> shellPointTypes
        = { TfToken("BrepPointAPI"), TfToken("none") };
    static const std::vector<TfToken> orientationTypes
        = { TfToken("same"), TfToken("opposite") };
    static const std::vector<TfToken> surfaceTypes
        = { TfToken("BrepSurfaceNurbAPI"), TfToken("BrepSurfaceSphereAPI"),
            TfToken("BrepSurfacePlaneAPI"), TfToken("BrepSurfaceCylinderAPI"),
            TfToken("BrepSurfaceConeAPI"), TfToken("BrepSurfaceTorusAPI") };
    static const std::vector<TfToken> trimTypes
        = { TfToken("rectangular"), TfToken("general") };
    static const std::vector<TfToken> radialEntryTypes
        = { TfToken("topEntry"), TfToken("bottomEntry") };
    static const std::vector<TfToken> curveTypes
        = { TfToken("BrepCurve3dNurbAPI"), TfToken("BrepCurve3dCircleAPI"),
            TfToken("BrepCurve3dLineAPI"),
            TfToken("BrepCurve3dEllipseAPI") };
    static const std::vector<TfToken> vertexPointTypes
        = { TfToken("BrepPointAPI") };

    UsdValidationErrorVector errors;

    // BA.075: region:type.
    _CheckAllowedTokens(usdPrim, _Read<TfToken>(brep.GetRegionTypeAttr()),
                        regionTypes, "BA.075", "region:type", "region",
                        UsdSolidValidationErrorNameTokens->invalidRegionType,
                        &errors);
    // BA.090: shell:pointType. 'none' is allowed: a shell that carries no
    // representative point authors the token rather than omitting the entry,
    // so the array stays parallel to the other shell arrays.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetShellPointTypeAttr()), shellPointTypes,
        "BA.090", "shell:pointType", "shell",
        UsdSolidValidationErrorNameTokens->invalidShellPointType, &errors);
    // BA.110: faceuse:orientationType.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetFaceuseOrientationTypeAttr()),
        orientationTypes, "BA.110", "faceuse:orientationType", "faceuse",
        UsdSolidValidationErrorNameTokens->invalidFaceuseOrientationType,
        &errors);
    // BA.130: face:surfaceType.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetFaceSurfaceTypeAttr()), surfaceTypes,
        "BA.130", "face:surfaceType", "face",
        UsdSolidValidationErrorNameTokens->invalidFaceSurfaceType, &errors);
    // BA.135: face:trimType.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetFaceTrimTypeAttr()), trimTypes,
        "BA.135", "face:trimType", "face",
        UsdSolidValidationErrorNameTokens->invalidFaceTrimType, &errors);
    // BA.190: edgeuse:orientationType.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetEdgeuseOrientationTypeAttr()),
        orientationTypes, "BA.190", "edgeuse:orientationType", "edgeuse",
        UsdSolidValidationErrorNameTokens->invalidEdgeuseOrientationType,
        &errors);
    // BA.195: edgeuse:thisRadialEntryType.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetEdgeuseThisRadialEntryTypeAttr()),
        radialEntryTypes, "BA.195", "edgeuse:thisRadialEntryType", "edgeuse",
        UsdSolidValidationErrorNameTokens->invalidEdgeuseRadialEntryType,
        &errors);
    // BA.245: edge:curveType.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetEdgeCurveTypeAttr()), curveTypes,
        "BA.245", "edge:curveType", "edge",
        UsdSolidValidationErrorNameTokens->invalidEdgeCurveType, &errors);
    // BA.260: wireEdge:curveType. Python reaches this check only when
    // sum(shell:wireEdgeCount) > 0; the guard makes no difference here because
    // an empty wireEdge:curveType has nothing to reject, and a BrepArray that
    // authors wire-edge curve types while declaring no wire edges is exactly
    // the case worth reporting.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetWireEdgeCurveTypeAttr()), curveTypes,
        "BA.260", "wireEdge:curveType", "wireEdge",
        UsdSolidValidationErrorNameTokens->invalidWireEdgeCurveType, &errors);
    // BA.315: vertex:pointType. Unlike shell:pointType there is no 'none':
    // every vertex has a point.
    _CheckAllowedTokens(
        usdPrim, _Read<TfToken>(brep.GetVertexPointTypeAttr()),
        vertexPointTypes, "BA.315", "vertex:pointType", "vertex",
        UsdSolidValidationErrorNameTokens->invalidVertexPointType, &errors);

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayRanges                                                            //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayRanges(const UsdPrim &usdPrim,
                 const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    UsdValidationErrorVector errors;

    // BA.140: every face must have at least one loop.
    const VtArray<unsigned int> faceLoopCount
        = _Read<unsigned int>(brep.GetFaceLoopCountAttr());
    for (size_t i = 0; i < faceLoopCount.size(); ++i) {
        if (faceLoopCount[i] < 1) {
            errors.emplace_back(
                UsdSolidValidationErrorNameTokens->invalidFaceLoopCount,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "BrepArray <%s>: face:loopCount[%zu] = 0; each face must "
                    "have at least one loop.",
                    usdPrim.GetPath().GetText(), i));
        }
    }

    // BA.155 / BA.160: face:range is stored as (UVmin, UVmax) pairs; the U and
    // V intervals must each be non-degenerate (max > min).
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());
    for (size_t face = 0; 2 * face + 1 < faceRange.size(); ++face) {
        const GfVec2d &uvMin = faceRange[2 * face];
        const GfVec2d &uvMax = faceRange[2 * face + 1];
        if (uvMax[0] <= uvMin[0]) {
            errors.emplace_back(
                UsdSolidValidationErrorNameTokens->degenerateFaceURange,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "BrepArray <%s>: face:range for face %zu has degenerate U "
                    "interval (Umin %g >= Umax %g).",
                    usdPrim.GetPath().GetText(), face, uvMin[0], uvMax[0]));
        }
        if (uvMax[1] <= uvMin[1]) {
            errors.emplace_back(
                UsdSolidValidationErrorNameTokens->degenerateFaceVRange,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "BrepArray <%s>: face:range for face %zu has degenerate V "
                    "interval (Vmin %g >= Vmax %g).",
                    usdPrim.GetPath().GetText(), face, uvMin[1], uvMax[1]));
        }
    }

    // BA.235: each edge:range (min, max) pair must be ordered.
    const VtArray<double> edgeRange = _Read<double>(brep.GetEdgeRangeAttr());
    for (size_t edge = 0; 2 * edge + 1 < edgeRange.size(); ++edge) {
        if (edgeRange[2 * edge] > edgeRange[2 * edge + 1]) {
            errors.emplace_back(
                UsdSolidValidationErrorNameTokens->invalidEdgeRangeOrder,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "BrepArray <%s>: edge:range for edge %zu is not ordered "
                    "(min %g > max %g).",
                    usdPrim.GetPath().GetText(), edge, edgeRange[2 * edge],
                    edgeRange[2 * edge + 1]));
        }
    }

    // BA.275: each wireEdge:range (min, max) pair must be ordered.
    const VtArray<double> wireEdgeRange
        = _Read<double>(brep.GetWireEdgeRangeAttr());
    for (size_t edge = 0; 2 * edge + 1 < wireEdgeRange.size(); ++edge) {
        if (wireEdgeRange[2 * edge] > wireEdgeRange[2 * edge + 1]) {
            errors.emplace_back(
                UsdSolidValidationErrorNameTokens->invalidWireEdgeRangeOrder,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "BrepArray <%s>: wireEdge:range for wireEdge %zu is not "
                    "ordered (min %g > max %g).",
                    usdPrim.GetPath().GetText(), edge,
                    wireEdgeRange[2 * edge], wireEdgeRange[2 * edge + 1]));
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayFaceOuterLoop                                                     //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayFaceOuterLoop(const UsdPrim &usdPrim,
                        const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    UsdValidationErrorVector errors;

    const VtArray<unsigned int> faceLoopCount
        = _Read<unsigned int>(brep.GetFaceLoopCountAttr());
    const VtArray<unsigned int> loopEdgeuseCount
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());

    // BA.145 (proposal #109 rule 424/425): each face has a single outer loop,
    // and the first loop listed is that outer loop; seam edges are required, so
    // the outer loop must contain at least one edgeuse. A loop with zero
    // edgeuses (a degenerate vertex-loop, rule 428) is legal only as a
    // non-first (inner) loop. Faces whose FIRST loop has loop:edgeuseCount == 0
    // are edgeless periodic surfaces (a cylinder/sphere/cone authored as a
    // single seamless NURBS patch) and are flagged. Loops beyond the first are
    // not examined here (a degenerate inner vertex-loop is allowed).
    //
    // Loops are stored contiguously: face f owns the loops in the half-open
    // range [loopCursor, loopCursor + face:loopCount[f]); the first of those is
    // the outer loop.
    size_t loopCursor = 0;
    for (size_t f = 0; f < faceLoopCount.size(); ++f) {
        const unsigned int nlp = faceLoopCount[f];
        if (nlp == 0) {
            // No loops at all: BrepArrayRanges (BA.140) reports this; there is
            // no outer loop to examine.
            continue;
        }
        const size_t outerLoop = loopCursor;
        if (outerLoop < loopEdgeuseCount.size()
            && loopEdgeuseCount[outerLoop] == 0u) {
            errors.emplace_back(
                UsdSolidValidationErrorNameTokens->faceOuterLoopNoEdges,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "[BA.145] BrepArray <%s>: face %zu has an outer loop "
                    "(loop %zu, the first loop of the face) with "
                    "loop:edgeuseCount = 0; a face's outer loop must contain "
                    "at least one edgeuse (seam edges are required). A "
                    "zero-edgeuse vertex-loop is legal only as a degenerate "
                    "inner (non-first) loop. This face is an edgeless periodic "
                    "surface (e.g. a cylinder/sphere/cone authored as a single "
                    "seamless NURBS patch).",
                    usdPrim.GetPath().GetText(), f, outerLoop));
        }
        loopCursor += nlp;
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayAnalyticSurfaces                                                  //
// -------------------------------------------------------------------------- //

// A radius-like parameter, its rule number, and whether it must be strictly
// positive (false => non-negative is sufficient, as for cone apex radius).
struct _RadiusParam {
    TfToken attr;
    const char *name;
    bool strictlyPositive;
    const char *ba;              // BA.481/501/511/521/522
};

// Description of one analytic surface type's parameter attributes, together
// with the rule numbers that govern them. Each surface family has its own
// numbering for the same four checks (size, axis unit length, refDirection unit
// length, axis/refDirection orthogonality), so the numbers travel with the
// description instead of being spelled out at each emit site.
struct _SurfaceDesc {
    TfToken faceSurfaceType;     // face:surfaceType token value
    const char *label;           // human-readable surface type
    TfToken originAttr;          // surface position (plane/cylinder/cone/torus
                                 // origin, sphere center)
    const char *originName;
    TfToken axisAttr;            // surface frame axis (unit)
    const char *axisName;
    TfToken refDirAttr;          // surface frame reference direction (unit)
    const char *refDirName;
    std::vector<_RadiusParam> radii;
    TfToken semiAngleAttr;       // empty token unless a cone
    const char *semiAngleName;
    const char *baSize;          // BA.480/490/500/510/520
    const char *baAxisUnit;      // BA.482/491/502/512/523
    const char *baRefDirUnit;    // BA.483/492/503/513/524
    const char *baOrtho;         // BA.484/493/504/514/525
};

// Array-size check for one analytic surface parameter. Mirrors _CheckSize but
// tags the message with the surface family's own rule number, so a size failure
// on a cone attributes to BA.510 and the same failure on a torus to BA.520.
void
_CheckSurfaceParamSize(const UsdPrim &usdPrim, const char *ba,
                       const char *attrName, size_t actual, size_t expected,
                       const std::string &expectedDesc,
                       UsdValidationErrorVector *errors)
{
    if (actual != expected) {
        errors->emplace_back(
            UsdSolidValidationErrorNameTokens
                ->inconsistentAnalyticSurfaceCount,
            UsdValidationErrorType::Error, _PrimSites(usdPrim),
            TfStringPrintf(
                "[%s] BrepArray <%s>: attribute %s has size %zu but expected "
                "%zu (%s).",
                ba, usdPrim.GetPath().GetText(), attrName, actual, expected,
                expectedDesc.c_str()));
    }
}

void
_CheckAnalyticSurface(const UsdPrim &usdPrim, const _SurfaceDesc &desc,
                      size_t count, UsdValidationErrorVector *errors)
{
    const VtArray<GfVec3d> origin
        = _Read<GfVec3d>(usdPrim.GetAttribute(desc.originAttr));
    const VtArray<GfVec3d> axis
        = _Read<GfVec3d>(usdPrim.GetAttribute(desc.axisAttr));
    const VtArray<GfVec3d> refDir
        = _Read<GfVec3d>(usdPrim.GetAttribute(desc.refDirAttr));

    const std::string countDesc = TfStringPrintf(
        "number of faces with face:surfaceType '%s' = %zu",
        desc.faceSurfaceType.GetText(), count);

    // BA.480/490/500/510/520: parameter array sizes must match the face count.
    _CheckSurfaceParamSize(usdPrim, desc.baSize, desc.originName,
                           origin.size(), count, countDesc, errors);
    _CheckSurfaceParamSize(usdPrim, desc.baSize, desc.axisName, axis.size(),
                           count, countDesc, errors);
    _CheckSurfaceParamSize(usdPrim, desc.baSize, desc.refDirName,
                           refDir.size(), count, countDesc, errors);

    // BA.481/501/511/521/522: radius positivity (or non-negativity).
    for (const _RadiusParam &radius : desc.radii) {
        const VtArray<double> values
            = _Read<double>(usdPrim.GetAttribute(radius.attr));
        _CheckSurfaceParamSize(usdPrim, desc.baSize, radius.name,
                               values.size(), count, countDesc, errors);
        for (size_t i = 0; i < values.size(); ++i) {
            const bool bad = radius.strictlyPositive ? (values[i] <= 0.0)
                                                     : (values[i] < 0.0);
            if (bad) {
                errors->emplace_back(
                    UsdSolidValidationErrorNameTokens
                        ->nonPositiveSurfaceRadius,
                    UsdValidationErrorType::Error, _PrimSites(usdPrim),
                    TfStringPrintf(
                        "[%s] BrepArray <%s>: %s surface %s[%zu] = %g must be "
                        "%s.",
                        radius.ba, usdPrim.GetPath().GetText(), desc.label,
                        radius.name, i, values[i],
                        radius.strictlyPositive ? "positive"
                                                : "non-negative"));
            }
        }
    }

    // BA.482/491/502/512/523: axis must be unit length.
    for (size_t i = 0; i < axis.size(); ++i) {
        if (std::abs(axis[i].GetLength() - 1.0) > _FrameTol) {
            errors->emplace_back(
                UsdSolidValidationErrorNameTokens->nonUnitSurfaceAxis,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "[%s] BrepArray <%s>: %s surface %s[%zu] is not unit "
                    "length (length %g).",
                    desc.baAxisUnit, usdPrim.GetPath().GetText(), desc.label,
                    desc.axisName, i, axis[i].GetLength()));
        }
    }
    // BA.483/492/503/513/524: refDirection must be unit length.
    for (size_t i = 0; i < refDir.size(); ++i) {
        if (std::abs(refDir[i].GetLength() - 1.0) > _FrameTol) {
            errors->emplace_back(
                UsdSolidValidationErrorNameTokens->nonUnitSurfaceRefDirection,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "[%s] BrepArray <%s>: %s surface %s[%zu] is not unit "
                    "length (length %g).",
                    desc.baRefDirUnit, usdPrim.GetPath().GetText(), desc.label,
                    desc.refDirName, i, refDir[i].GetLength()));
        }
    }

    // BA.484/493/504/514/525: axis and refDirection must be orthogonal.
    const size_t frameCount = std::min(axis.size(), refDir.size());
    for (size_t i = 0; i < frameCount; ++i) {
        const double dot = GfDot(axis[i], refDir[i]);
        if (std::abs(dot) > _FrameTol) {
            errors->emplace_back(
                UsdSolidValidationErrorNameTokens->nonOrthogonalSurfaceAxes,
                UsdValidationErrorType::Error, _PrimSites(usdPrim),
                TfStringPrintf(
                    "[%s] BrepArray <%s>: %s surface %s and %s at index %zu "
                    "are not orthogonal (dot product %g).",
                    desc.baOrtho, usdPrim.GetPath().GetText(), desc.label,
                    desc.axisName, desc.refDirName, i, dot));
        }
    }

    // BA.515: cone semiAngle must lie in the open interval (0, pi/2).
    if (!desc.semiAngleAttr.IsEmpty()) {
        const VtArray<double> semiAngle
            = _Read<double>(usdPrim.GetAttribute(desc.semiAngleAttr));
        _CheckSurfaceParamSize(usdPrim, desc.baSize, desc.semiAngleName,
                               semiAngle.size(), count, countDesc, errors);
        for (size_t i = 0; i < semiAngle.size(); ++i) {
            if (semiAngle[i] <= 0.0 || semiAngle[i] >= _HalfPi) {
                errors->emplace_back(
                    UsdSolidValidationErrorNameTokens->invalidConeSemiAngle,
                    UsdValidationErrorType::Error, _PrimSites(usdPrim),
                    TfStringPrintf(
                        "[BA.515] BrepArray <%s>: %s surface %s[%zu] = %g must "
                        "lie in the open interval (0, pi/2).",
                        usdPrim.GetPath().GetText(), desc.label,
                        desc.semiAngleName, i, semiAngle[i]));
            }
        }
    }
}

UsdValidationErrorVector
_BrepArrayAnalyticSurfaces(const UsdPrim &usdPrim,
                           const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    const VtArray<TfToken> faceSurfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());

    const std::vector<_SurfaceDesc> descs = {
        { TfToken("BrepSurfacePlaneAPI"), "plane",
          UsdSolidTokens->brepSurfacePlaneOrigin, "brep:surface:plane:origin",
          UsdSolidTokens->brepSurfacePlaneAxis, "brep:surface:plane:axis",
          UsdSolidTokens->brepSurfacePlaneRefDirection,
          "brep:surface:plane:refDirection",
          {},
          TfToken(), nullptr,
          "BA.490", "BA.491", "BA.492", "BA.493" },
        { TfToken("BrepSurfaceCylinderAPI"), "cylinder",
          UsdSolidTokens->brepSurfaceCylinderOrigin,
          "brep:surface:cylinder:origin",
          UsdSolidTokens->brepSurfaceCylinderAxis,
          "brep:surface:cylinder:axis",
          UsdSolidTokens->brepSurfaceCylinderRefDirection,
          "brep:surface:cylinder:refDirection",
          { { UsdSolidTokens->brepSurfaceCylinderRadius,
              "brep:surface:cylinder:radius", true, "BA.501" } },
          TfToken(), nullptr,
          "BA.500", "BA.502", "BA.503", "BA.504" },
        { TfToken("BrepSurfaceConeAPI"), "cone",
          UsdSolidTokens->brepSurfaceConeOrigin, "brep:surface:cone:origin",
          UsdSolidTokens->brepSurfaceConeAxis, "brep:surface:cone:axis",
          UsdSolidTokens->brepSurfaceConeRefDirection,
          "brep:surface:cone:refDirection",
          { { UsdSolidTokens->brepSurfaceConeRadius,
              "brep:surface:cone:radius", false, "BA.511" } },
          UsdSolidTokens->brepSurfaceConeSemiAngle,
          "brep:surface:cone:semiAngle",
          "BA.510", "BA.512", "BA.513", "BA.514" },
        { TfToken("BrepSurfaceSphereAPI"), "sphere",
          UsdSolidTokens->brepSurfaceSphereCenter, "brep:surface:sphere:center",
          UsdSolidTokens->brepSurfaceSphereAxis, "brep:surface:sphere:axis",
          UsdSolidTokens->brepSurfaceSphereRefDirection,
          "brep:surface:sphere:refDirection",
          { { UsdSolidTokens->brepSurfaceSphereRadius,
              "brep:surface:sphere:radius", true, "BA.481" } },
          TfToken(), nullptr,
          "BA.480", "BA.482", "BA.483", "BA.484" },
        { TfToken("BrepSurfaceTorusAPI"), "torus",
          UsdSolidTokens->brepSurfaceTorusOrigin, "brep:surface:torus:origin",
          UsdSolidTokens->brepSurfaceTorusAxis, "brep:surface:torus:axis",
          UsdSolidTokens->brepSurfaceTorusRefDirection,
          "brep:surface:torus:refDirection",
          { { UsdSolidTokens->brepSurfaceTorusMajorRadius,
              "brep:surface:torus:majorRadius", true, "BA.521" },
            { UsdSolidTokens->brepSurfaceTorusMinorRadius,
              "brep:surface:torus:minorRadius", true, "BA.522" } },
          TfToken(), nullptr,
          "BA.520", "BA.523", "BA.524", "BA.525" },
    };

    UsdValidationErrorVector errors;
    for (const _SurfaceDesc &desc : descs) {
        size_t count = 0;
        for (const TfToken &type : faceSurfaceType) {
            if (type == desc.faceSurfaceType) {
                ++count;
            }
        }
        // Skip work for surface types that are not used and have no authored
        // parameters; _CheckAnalyticSurface emits a size error if parameters
        // are authored without matching faces.
        const VtArray<GfVec3d> axis
            = _Read<GfVec3d>(usdPrim.GetAttribute(desc.axisAttr));
        if (count == 0 && axis.empty()) {
            continue;
        }
        _CheckAnalyticSurface(usdPrim, desc, count, &errors);
    }

    return errors;
}

// ========================================================================== //
// Shared support for the deferred-rule validators                            //
// ========================================================================== //

constexpr double _NurbsTol = 1e-11;   // BA.3xx/4xx weight/knot ordering tolerance
constexpr double _DomainTol = 1e-6;   // BA.56x/57x span tolerance
// (Curve axis-frame unit/orthogonality checks now share the surface _FrameTol
// (1e-6); the former _CurveEps=1e-4 was retired -- register row 16. Analytic
// edge degeneracy now measures arc length against brep:intersectTol3d instead
// of a fixed curve epsilon -- register row 13.)
constexpr double _TwoPi = 6.283185307179586;

void
_Err(UsdValidationErrorVector *errors, const TfToken &name,
     const UsdPrim &prim, const std::string &msg,
     UsdValidationErrorType severity = UsdValidationErrorType::Error)
{
    errors->emplace_back(name, severity, _PrimSites(prim), msg);
}

template <class T>
VtArray<T>
_ReadName(const UsdPrim &prim, const std::string &name)
{
    return _Read<T>(prim.GetAttribute(TfToken(name)));
}

bool
_IsAuthored(const UsdPrim &prim, const std::string &name)
{
    const UsdAttribute a = prim.GetAttribute(TfToken(name));
    return a && a.HasAuthoredValue();
}

bool
_HasAppliedSchema(const UsdPrim &prim, const TfToken &schemaToken)
{
    for (const TfToken &s : prim.GetAppliedSchemas()) {
        if (s == schemaToken) {
            return true;
        }
    }
    return false;
}

size_t
_CountToken(const VtArray<TfToken> &arr, const TfToken &tok)
{
    size_t c = 0;
    for (const TfToken &t : arr) {
        if (t == tok) {
            ++c;
        }
    }
    return c;
}

// Per-Brep prefix-offset partitions. Each vector has length numBreps+1 and
// holds cumulative counts so that the objects of Brep ii occupy the half-open
// index range [arr[ii], arr[ii+1]) in the corresponding flat array. (Objects
// related to a single Brep are stored consecutively.)
//
// Only the levels that have an explicit per-Brep *count* array are tracked
// here, so every partition below is exact for any number of Breps. There is no
// per-Brep count for edges or vertices, so those cannot be partitioned
// reliably from the flat data (a reference-derived partition mis-attributes
// non-contiguous or orphaned entities); index-range checks on edges/vertices
// therefore validate against global bounds, and containment uses the union of
// all brep:extent boxes.
struct _BrepOffsets {
    size_t numBreps = 0;
    std::vector<size_t> region, shell, faceuse, face, loop, edgeuse, wireEdge;
    bool ok = false;
};

_BrepOffsets
_ComputeOffsets(const UsdSolidBrepArray &brep)
{
    _BrepOffsets o;
    const VtArray<unsigned int> regionCount
        = _Read<unsigned int>(brep.GetBrepRegionCountAttr());
    const VtArray<unsigned int> regionShellCount
        = _Read<unsigned int>(brep.GetRegionShellCountAttr());
    const VtArray<unsigned int> shellFaceuseCount
        = _Read<unsigned int>(brep.GetShellFaceuseCountAttr());
    const VtArray<unsigned int> shellWireEdgeCount
        = _Read<unsigned int>(brep.GetShellWireEdgeCountAttr());
    const VtArray<unsigned int> faceLoopCount
        = _Read<unsigned int>(brep.GetFaceLoopCountAttr());
    const VtArray<unsigned int> loopEdgeuseCount
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());

    const size_t n = regionCount.size();
    o.numBreps = n;
    if (n == 0) {
        return o;
    }

    const auto sumRange
        = [](const VtArray<unsigned int> &a, size_t lo, size_t hi) {
              size_t s = 0;
              for (size_t i = lo; i < hi && i < a.size(); ++i) {
                  s += a[i];
              }
              return s;
          };

    o.region.assign(n + 1, 0);
    for (size_t b = 0; b < n; ++b) {
        o.region[b + 1] = o.region[b] + regionCount[b];
    }
    o.shell.assign(n + 1, 0);
    for (size_t b = 0; b < n; ++b) {
        o.shell[b + 1]
            = o.shell[b] + sumRange(regionShellCount, o.region[b], o.region[b + 1]);
    }
    o.faceuse.assign(n + 1, 0);
    o.wireEdge.assign(n + 1, 0);
    for (size_t b = 0; b < n; ++b) {
        o.faceuse[b + 1]
            = o.faceuse[b] + sumRange(shellFaceuseCount, o.shell[b], o.shell[b + 1]);
        o.wireEdge[b + 1]
            = o.wireEdge[b]
            + sumRange(shellWireEdgeCount, o.shell[b], o.shell[b + 1]);
    }
    o.face.assign(n + 1, 0);
    for (size_t b = 0; b < n; ++b) {
        o.face[b + 1] = o.face[b] + (o.faceuse[b + 1] - o.faceuse[b]) / 2;
    }
    o.loop.assign(n + 1, 0);
    for (size_t b = 0; b < n; ++b) {
        o.loop[b + 1]
            = o.loop[b] + sumRange(faceLoopCount, o.face[b], o.face[b + 1]);
    }
    o.edgeuse.assign(n + 1, 0);
    for (size_t b = 0; b < n; ++b) {
        o.edgeuse[b + 1]
            = o.edgeuse[b] + sumRange(loopEdgeuseCount, o.loop[b], o.loop[b + 1]);
    }
    o.ok = true;
    return o;
}

// --- NURBS stratum helpers (shared by surface / edge3d / curveUv) --------- //

void
_CheckNurbOrderPositive(const UsdPrim &prim, const char *ba, const char *label,
                        const VtArray<unsigned int> &order,
                        const VtArray<unsigned int> &vtxCount,
                        bool allowZeroSentinel,
                        UsdValidationErrorVector *errors)
{
    for (size_t i = 0; i < order.size(); ++i) {
        const unsigned int vc = i < vtxCount.size() ? vtxCount[i] : 0u;
        if (allowZeroSentinel && order[i] == 0u && vc == 0u) {
            continue;
        }
        if (order[i] == 0u) {
            _Err(errors, UsdSolidValidationErrorNameTokens->nurbNonPositiveOrder,
                 prim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s order[%zu] = 0 must be "
                                "positive.",
                                ba, prim.GetPath().GetText(), label, i));
        }
    }
}

void
_CheckNurbOrderLEVtx(const UsdPrim &prim, const char *ba, const char *label,
                     const VtArray<unsigned int> &order,
                     const VtArray<unsigned int> &vtxCount,
                     bool allowZeroSentinel, UsdValidationErrorVector *errors)
{
    (void)allowZeroSentinel; // sentinel subsumed by the order==0 skip below
    const size_t m = std::min(order.size(), vtxCount.size());
    for (size_t i = 0; i < m; ++i) {
        // order == 0 cannot violate order > vertexCount; it (incl. the all-zero
        // curveUv sentinel) is handled by the order-positivity check.
        if (order[i] == 0u) {
            continue;
        }
        if (order[i] > vtxCount[i]) {
            _Err(errors,
                 UsdSolidValidationErrorNameTokens->nurbOrderExceedsVertexCount,
                 prim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s order[%zu] = %u exceeds "
                                "vertexCount %u.",
                                ba, prim.GetPath().GetText(), label, i, order[i],
                                vtxCount[i]));
        }
    }
}

void
_CheckNurbOrderMin2(const UsdPrim &prim, const char *label,
                    const VtArray<unsigned int> &order,
                    const VtArray<unsigned int> &vtxCount,
                    bool allowZeroSentinel, UsdValidationErrorVector *errors)
{
    for (size_t i = 0; i < order.size(); ++i) {
        const unsigned int vc = i < vtxCount.size() ? vtxCount[i] : 0u;
        if (allowZeroSentinel && order[i] == 0u && vc == 0u) {
            continue;
        }
        if (order[i] < 2u) {
            _Err(errors, UsdSolidValidationErrorNameTokens->nurbOrderBelowMinimum,
                 prim,
                 TfStringPrintf("[BA.590] BrepArray <%s>: %s order[%zu] = %u must "
                                "be >= 2 (degree >= 1).",
                                prim.GetPath().GetText(), label, i, order[i]));
            break;
        }
    }
}

void
_CheckNurbVtxGEOrder(const UsdPrim &prim, const char *label,
                     const VtArray<unsigned int> &order,
                     const VtArray<unsigned int> &vtxCount,
                     bool allowZeroSentinel, UsdValidationErrorVector *errors)
{
    if (order.size() != vtxCount.size()) {
        return;
    }
    for (size_t i = 0; i < order.size(); ++i) {
        if (allowZeroSentinel && order[i] == 0u && vtxCount[i] == 0u) {
            continue;
        }
        if (vtxCount[i] < order[i]) {
            _Err(errors,
                 UsdSolidValidationErrorNameTokens->nurbVertexCountBelowOrder,
                 prim,
                 TfStringPrintf("[BA.591] BrepArray <%s>: %s vertexCount[%zu] = "
                                "%u is less than order %u.",
                                prim.GetPath().GetText(), label, i, vtxCount[i],
                                order[i]));
            break;
        }
    }
}

void
_CheckNurbWeights(const UsdPrim &prim, const char *ba, const char *label,
                  const VtArray<double> &weights,
                  UsdValidationErrorVector *errors)
{
    for (size_t i = 0; i < weights.size(); ++i) {
        if (weights[i] < _NurbsTol) {
            _Err(errors, UsdSolidValidationErrorNameTokens->nurbNonPositiveWeight,
                 prim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s weight[%zu] = %g must be "
                                "positive.",
                                ba, prim.GetPath().GetText(), label, i,
                                weights[i]));
        }
    }
}

// Knot vector size + monotonicity for a single (order, vertexCount) direction.
// Only validated when the knot array is authored (non-empty), matching the
// reference which gates these checks on present knot data.
void
_CheckNurbKnots1D(const UsdPrim &prim, const char *baSize, const char *baMono,
                  const char *label, const VtArray<unsigned int> &order,
                  const VtArray<unsigned int> &vtxCount,
                  const VtArray<double> &knots, UsdValidationErrorVector *errors)
{
    if (knots.empty()) {
        return;
    }
    const size_t m = std::min(order.size(), vtxCount.size());
    size_t expected = 0;
    for (size_t i = 0; i < m; ++i) {
        expected += static_cast<size_t>(order[i]) + vtxCount[i];
    }
    if (knots.size() != expected) {
        _Err(errors, UsdSolidValidationErrorNameTokens->nurbKnotCountMismatch,
             prim,
             TfStringPrintf("[%s] BrepArray <%s>: %s knot vector size %zu but "
                            "expected %zu (sum of order + vertexCount).",
                            baSize, prim.GetPath().GetText(), label,
                            knots.size(), expected));
    }
    size_t off = 0;
    for (size_t i = 0; i < m; ++i) {
        const size_t cnt = static_cast<size_t>(order[i]) + vtxCount[i];
        if (off + cnt > knots.size()) {
            break;
        }
        for (size_t k = 1; k < cnt; ++k) {
            if (knots[off + k] < knots[off + k - 1] - _NurbsTol) {
                _Err(errors,
                     UsdSolidValidationErrorNameTokens->nurbKnotNotMonotonic,
                     prim,
                     TfStringPrintf("[%s] BrepArray <%s>: %s knot vector %zu is "
                                    "not non-decreasing.",
                                    baMono, prim.GetPath().GetText(), label, i));
                break;
            }
        }
        off += cnt;
    }
}

// An authored attribute's *defined* type (UsdAttribute::GetTypeName) is the
// schema type for builtin attributes, so it cannot reveal a wrong authored
// type; and the held C++ value type cannot distinguish role-aliased types such
// as point3d[] vs vector3d[] (both VtArray<GfVec3d>). Inspect the strongest
// authored attribute spec's typeName, which preserves the authored role.
bool
_AuthoredTypeIn(const UsdPrim &prim, const char *attr,
                const std::vector<SdfValueTypeName> &acceptable,
                std::string *got)
{
    const UsdAttribute a = prim.GetAttribute(TfToken(attr));
    if (!a || !a.HasAuthoredValue()) {
        return false;
    }
    for (const SdfPropertySpecHandle &spec :
         a.GetPropertyStack(UsdTimeCode::Default())) {
        const SdfAttributeSpecHandle attrSpec
            = TfDynamic_cast<SdfAttributeSpecHandle>(spec);
        if (!attrSpec || !attrSpec->GetTypeName()) {
            continue;
        }
        // Strongest authored typeName wins.
        const SdfValueTypeName authored = attrSpec->GetTypeName();
        for (const SdfValueTypeName &ok : acceptable) {
            if (authored == ok) {
                return false;
            }
        }
        if (got) {
            *got = authored.GetAsToken().GetString();
        }
        return true;
    }
    return false;
}

bool
_AuthoredTypeMismatch(const UsdPrim &prim, const char *attr,
                      const SdfValueTypeName &expected, std::string *got)
{
    return _AuthoredTypeIn(prim, attr, { expected }, got);
}

void
_CheckNurbType(const UsdPrim &prim, const char *ba, const char *attr,
               const SdfValueTypeName &expected,
               UsdValidationErrorVector *errors)
{
    std::string got;
    if (_AuthoredTypeMismatch(prim, attr, expected, &got)) {
        _Err(errors, UsdSolidValidationErrorNameTokens->nurbInvalidDataType,
             prim,
             TfStringPrintf("[%s] BrepArray <%s>: attribute %s has type '%s' but "
                            "expected '%s'.",
                            ba, prim.GetPath().GetText(), attr, got.c_str(),
                            expected.GetAsToken().GetText()));
    }
}

// --- Analytic curve helpers (BA.53x/54x/55x) ------------------------------ //

void
_CheckCurveArraySize(const UsdPrim &prim, const char *ba, const char *shape,
                     const char *inst, const char *attr, size_t actual,
                     size_t expected, UsdValidationErrorVector *errors)
{
    if (actual != expected) {
        _Err(errors,
             UsdSolidValidationErrorNameTokens->analyticCurveArraySizeMismatch,
             prim,
             TfStringPrintf("[%s] BrepArray <%s>: %s %s %s size %zu but expected "
                            "%zu.",
                            ba, prim.GetPath().GetText(), shape, inst, attr,
                            actual, expected));
    }
}

void
_CheckCurveUnit(const UsdPrim &prim, const char *ba, const char *shape,
                const char *inst, const char *attr, const TfToken &errTok,
                const VtArray<GfVec3d> &vecs, UsdValidationErrorVector *errors)
{
    // Curve axis frames use the SAME unit-length tolerance as surface axis
    // frames (_FrameTol, 1e-6): a unit-vector check is a unit-vector check
    // regardless of the shape family, and the previous 1e-4 curve tolerance let
    // a circle/ellipse frame drift 100x further before flagging than the
    // identical cylinder/cone frame (register row 16).
    for (size_t i = 0; i < vecs.size(); ++i) {
        if (std::abs(vecs[i].GetLength() - 1.0) > _FrameTol) {
            _Err(errors, errTok, prim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s %s %s[%zu] is not unit "
                                "length (length %g).",
                                ba, prim.GetPath().GetText(), shape, inst, attr,
                                i, vecs[i].GetLength()));
            break;
        }
    }
}

void
_CheckCurveOrtho(const UsdPrim &prim, const char *ba, const char *shape,
                 const char *inst, const VtArray<GfVec3d> &axis,
                 const VtArray<GfVec3d> &ref, UsdValidationErrorVector *errors)
{
    // Orthogonality uses _FrameTol (1e-6) for the same reason as the unit-length
    // check above: identical frame checks share one tolerance (register row 16).
    const size_t m = std::min(axis.size(), ref.size());
    for (size_t i = 0; i < m; ++i) {
        if (std::abs(GfDot(axis[i], ref[i])) > _FrameTol) {
            _Err(errors,
                 UsdSolidValidationErrorNameTokens
                     ->analyticCurveAxisRefDirectionNotOrthogonal,
                 prim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s %s axis and "
                                "refDirection at index %zu are not orthogonal "
                                "(dot %g).",
                                ba, prim.GetPath().GetText(), shape, inst, i,
                                GfDot(axis[i], ref[i])));
            break;
        }
    }
}

void
_CheckCurveRadii(const UsdPrim &prim, const char *ba, const char *shape,
                 const char *inst, const char *attr, const VtArray<double> &r,
                 UsdValidationErrorVector *errors)
{
    for (size_t i = 0; i < r.size(); ++i) {
        if (r[i] <= 0.0) {
            _Err(errors,
                 UsdSolidValidationErrorNameTokens->analyticCurveNonPositiveRadius,
                 prim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s %s %s[%zu] = %g must be "
                                "positive.",
                                ba, prim.GetPath().GetText(), shape, inst, attr,
                                i, r[i]));
            break;
        }
    }
}

void
_CheckCircleInstance(const UsdPrim &prim, const char *inst, size_t count,
                     UsdValidationErrorVector *errors)
{
    if (count == 0) {
        return;
    }
    const std::string b = std::string("brep:") + inst + ":curve3d:circle:";
    const VtArray<GfVec3d> center = _ReadName<GfVec3d>(prim, b + "center");
    const VtArray<GfVec3d> axis = _ReadName<GfVec3d>(prim, b + "axis");
    const VtArray<GfVec3d> ref = _ReadName<GfVec3d>(prim, b + "refDirection");
    const VtArray<double> radius = _ReadName<double>(prim, b + "radius");
    _CheckCurveArraySize(prim, "BA.530", "circle", inst, "center",
                         center.size(), count, errors);
    _CheckCurveArraySize(prim, "BA.530", "circle", inst, "axis", axis.size(),
                         count, errors);
    _CheckCurveArraySize(prim, "BA.530", "circle", inst, "refDirection",
                         ref.size(), count, errors);
    _CheckCurveArraySize(prim, "BA.530", "circle", inst, "radius", radius.size(),
                         count, errors);
    _CheckCurveRadii(prim, "BA.531", "circle", inst, "radius", radius, errors);
    _CheckCurveUnit(prim, "BA.532", "circle", inst, "axis",
                    UsdSolidValidationErrorNameTokens->analyticCurveAxisNotUnitLength,
                    axis, errors);
    _CheckCurveUnit(
        prim, "BA.533", "circle", inst, "refDirection",
        UsdSolidValidationErrorNameTokens->analyticCurveRefDirectionNotUnitLength,
        ref, errors);
    _CheckCurveOrtho(prim, "BA.534", "circle", inst, axis, ref, errors);
}

void
_CheckLineInstance(const UsdPrim &prim, const char *inst, size_t count,
                   UsdValidationErrorVector *errors)
{
    if (count == 0) {
        return;
    }
    const std::string b = std::string("brep:") + inst + ":curve3d:line:";
    const VtArray<GfVec3d> origin = _ReadName<GfVec3d>(prim, b + "origin");
    const VtArray<GfVec3d> direction = _ReadName<GfVec3d>(prim, b + "direction");
    _CheckCurveArraySize(prim, "BA.540", "line", inst, "origin", origin.size(),
                         count, errors);
    _CheckCurveArraySize(prim, "BA.540", "line", inst, "direction",
                         direction.size(), count, errors);
    _CheckCurveUnit(prim, "BA.541", "line", inst, "direction",
                    UsdSolidValidationErrorNameTokens->lineDirectionNotUnitLength,
                    direction, errors);
}

void
_CheckEllipseInstance(const UsdPrim &prim, const char *inst, size_t count,
                      UsdValidationErrorVector *errors)
{
    if (count == 0) {
        return;
    }
    const std::string b = std::string("brep:") + inst + ":curve3d:ellipse:";
    const VtArray<GfVec3d> center = _ReadName<GfVec3d>(prim, b + "center");
    const VtArray<GfVec3d> axis = _ReadName<GfVec3d>(prim, b + "axis");
    const VtArray<GfVec3d> ref = _ReadName<GfVec3d>(prim, b + "refDirection");
    const VtArray<double> xRadius = _ReadName<double>(prim, b + "xRadius");
    const VtArray<double> yRadius = _ReadName<double>(prim, b + "yRadius");
    _CheckCurveArraySize(prim, "BA.550", "ellipse", inst, "center",
                         center.size(), count, errors);
    _CheckCurveArraySize(prim, "BA.550", "ellipse", inst, "axis", axis.size(),
                         count, errors);
    _CheckCurveArraySize(prim, "BA.550", "ellipse", inst, "refDirection",
                         ref.size(), count, errors);
    _CheckCurveArraySize(prim, "BA.550", "ellipse", inst, "xRadius",
                         xRadius.size(), count, errors);
    _CheckCurveArraySize(prim, "BA.550", "ellipse", inst, "yRadius",
                         yRadius.size(), count, errors);
    _CheckCurveRadii(prim, "BA.551", "ellipse", inst, "xRadius", xRadius, errors);
    _CheckCurveRadii(prim, "BA.552", "ellipse", inst, "yRadius", yRadius, errors);
    _CheckCurveUnit(prim, "BA.553", "ellipse", inst, "axis",
                    UsdSolidValidationErrorNameTokens->analyticCurveAxisNotUnitLength,
                    axis, errors);
    _CheckCurveUnit(
        prim, "BA.554", "ellipse", inst, "refDirection",
        UsdSolidValidationErrorNameTokens->analyticCurveRefDirectionNotUnitLength,
        ref, errors);
    _CheckCurveOrtho(prim, "BA.555", "ellipse", inst, axis, ref, errors);
}

// -------------------------------------------------------------------------- //
// BrepArrayAuthorship                                                        //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayAuthorship(const UsdPrim &usdPrim,
                     const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    struct Item {
        const char *attr;
        const char *family;
        const char *ba;
    };
    static const std::vector<Item> items = {
        { "region:shellCount", "region", "BA.070" },
        { "region:type", "region", "BA.070" },
        { "shell:faceuseCount", "shell", "BA.085" },
        { "shell:wireEdgeCount", "shell", "BA.085" },
        { "shell:pointType", "shell", "BA.085" },
        { "faceuse:faceIndex", "faceuse", "BA.105" },
        { "faceuse:orientationType", "faceuse", "BA.105" },
        { "face:loopCount", "face", "BA.125" },
        { "face:surfaceType", "face", "BA.125" },
        { "face:trimType", "face", "BA.125" },
        { "face:range", "face", "BA.125" },
        { "loop:edgeuseCount", "loop", "BA.170" },
        { "loop:vertexIndex", "loop", "BA.170" },
        { "edgeuse:edgeIndex", "edgeuse", "BA.185" },
        { "edgeuse:orientationType", "edgeuse", "BA.185" },
        { "edgeuse:nextRadialEUIndex", "edgeuse", "BA.185" },
        { "edgeuse:thisRadialEntryType", "edgeuse", "BA.185" },
        { "edge:curveType", "edge", "BA.215" },
        { "edge:vertexIndices", "edge", "BA.215" },
        { "edge:range", "edge", "BA.215" },
        { "wireEdge:curveType", "wireEdge", "BA.255" },
        { "wireEdge:vertexIndices", "wireEdge", "BA.255" },
        { "wireEdge:range", "wireEdge", "BA.255" },
        { "vertex:pointType", "vertex", "BA.300" },
    };
    // Lenient authorship: require a family's attributes only when that family's
    // entities actually exist (derived from the structural count arrays). A
    // face-only solid need not author the wireEdge:* / point families, a wire
    // body need not author the face families, and so on -- matching the schema's
    // support for point/wire/sheet/solid bodies and USD's "author what you use"
    // idiom. region and shell always apply to a BrepArray.
    const UsdSolidBrepArray brep(usdPrim);
    const auto sumU = [](const VtArray<unsigned int> &a) {
        size_t s = 0;
        for (unsigned int v : a) {
            s += v;
        }
        return s;
    };
    const VtArray<unsigned int> shellFaceuseCount
        = _Read<unsigned int>(brep.GetShellFaceuseCountAttr());
    const VtArray<unsigned int> shellWireEdgeCount
        = _Read<unsigned int>(brep.GetShellWireEdgeCountAttr());
    const VtArray<unsigned int> loopEdgeuseCount
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());
    const size_t numFaceuses = sumU(shellFaceuseCount);
    const size_t numWireEdges = sumU(shellWireEdgeCount);
    const size_t numEdgeuses = sumU(loopEdgeuseCount);
    size_t numPointShells = 0;
    {
        const size_t ns
            = std::min(shellFaceuseCount.size(), shellWireEdgeCount.size());
        for (size_t s = 0; s < ns; ++s) {
            if (shellFaceuseCount[s] == 0u && shellWireEdgeCount[s] == 0u) {
                ++numPointShells;
            }
        }
    }
    const bool hasFaces = numFaceuses > 0;
    const bool hasEdgeuses = numEdgeuses > 0;
    const bool hasWire = numWireEdges > 0;
    const bool hasVerts = hasEdgeuses || hasWire || numPointShells > 0;
    const auto familyRequired = [&](const char *fam) -> bool {
        const std::string f(fam);
        if (f == "faceuse" || f == "face" || f == "loop") {
            return hasFaces;
        }
        if (f == "edgeuse" || f == "edge") {
            return hasEdgeuses;
        }
        if (f == "wireEdge") {
            return hasWire;
        }
        if (f == "vertex") {
            return hasVerts;
        }
        return true; // region, shell: always present on a BrepArray.
    };
    UsdValidationErrorVector errors;
    for (const Item &it : items) {
        if (familyRequired(it.family) && !_IsAuthored(usdPrim, it.attr)) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->attributeNotAuthored,
                 usdPrim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s attribute %s is not "
                                "authored.",
                                it.ba, usdPrim.GetPath().GetText(), it.family,
                                it.attr));
        }
    }
    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayDataTypes                                                         //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayDataTypes(const UsdPrim &usdPrim,
                    const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    struct Item {
        const char *attr;
        SdfValueTypeName type;
        const char *ba;
        bool vecRole = false; // accept any double-precision 3-vector role
    };
    const std::vector<Item> items = {
        { "brep:intersectTol3d", SdfValueTypeNames->DoubleArray, "BA.061" },
        { "brep:extent", SdfValueTypeNames->Double3Array, "BA.061" },
        { "brep:regionCount", SdfValueTypeNames->UIntArray, "BA.061" },
        { "region:shellCount", SdfValueTypeNames->UIntArray, "BA.076" },
        { "region:type", SdfValueTypeNames->TokenArray, "BA.076" },
        { "shell:faceuseCount", SdfValueTypeNames->UIntArray, "BA.091" },
        { "shell:wireEdgeCount", SdfValueTypeNames->UIntArray, "BA.091" },
        { "shell:pointType", SdfValueTypeNames->TokenArray, "BA.091" },
        { "faceuse:faceIndex", SdfValueTypeNames->UIntArray, "BA.116" },
        { "faceuse:orientationType", SdfValueTypeNames->TokenArray, "BA.116" },
        { "face:loopCount", SdfValueTypeNames->UIntArray, "BA.161" },
        { "face:surfaceType", SdfValueTypeNames->TokenArray, "BA.161" },
        { "face:trimType", SdfValueTypeNames->TokenArray, "BA.161" },
        { "face:range", SdfValueTypeNames->Double2Array, "BA.161" },
        { "loop:edgeuseCount", SdfValueTypeNames->UIntArray, "BA.176" },
        { "loop:vertexIndex", SdfValueTypeNames->UIntArray, "BA.176" },
        { "edgeuse:edgeIndex", SdfValueTypeNames->UIntArray, "BA.196" },
        { "edgeuse:orientationType", SdfValueTypeNames->TokenArray, "BA.196" },
        { "edgeuse:nextRadialEUIndex", SdfValueTypeNames->UIntArray, "BA.196" },
        { "edgeuse:thisRadialEntryType", SdfValueTypeNames->TokenArray,
          "BA.196" },
        { "edge:curveType", SdfValueTypeNames->TokenArray, "BA.237" },
        { "edge:vertexIndices", SdfValueTypeNames->Int2Array, "BA.237" },
        { "edge:range", SdfValueTypeNames->DoubleArray, "BA.237" },
        { "wireEdge:curveType", SdfValueTypeNames->TokenArray, "BA.291" },
        { "wireEdge:vertexIndices", SdfValueTypeNames->Int2Array, "BA.291" },
        { "wireEdge:range", SdfValueTypeNames->DoubleArray, "BA.291" },
        { "vertex:pointType", SdfValueTypeNames->TokenArray, "BA.316" },
        { "brep:vertexPoint:point:position", SdfValueTypeNames->Point3dArray,
          "BA.326", true },
        { "brep:shellPoint:point:position", SdfValueTypeNames->Point3dArray,
          "BA.327", true },
        // BA.062: analytic geometry attribute types. A production STEP->UsdSolid
        // conversion authored analytic axes/positions with wrong roles/precision
        // (e.g. float3[] instead of a double-precision 3-vector) and the scalar
        // parameters with wrong scalar types; those "type" mistakes previously
        // sailed through with no data-type diagnostic (a wrong-typed axis reads
        // back as an empty GfVec3d array, so only a misleading
        // InconsistentAnalyticSurfaceCount fired, if anything). The vec3-role
        // families (origin/center/axis/refDirection) use the same lenient
        // point3d/vector3d/double3 policy as the position attributes above: the
        // goal is catching float-precision or non-3-vector mistakes, not role
        // churn. The scalar families (radii, semiAngle) must be double[].
        //
        // NURBS surface/edge families are intentionally omitted here: their data
        // types are already owned by BrepArrayNurbs (_CheckNurbType, BA.471 /
        // BA.371 / BA.416). Only authored attributes are checked (absence is the
        // Authorship validator's job).
        // --- analytic surfaces: plane / cylinder / cone / sphere / torus ---
        { "brep:surface:plane:origin", SdfValueTypeNames->Point3dArray,
          "BA.062", true },
        { "brep:surface:plane:axis", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:plane:refDirection", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:cylinder:origin", SdfValueTypeNames->Point3dArray,
          "BA.062", true },
        { "brep:surface:cylinder:axis", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:cylinder:refDirection", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:cylinder:radius", SdfValueTypeNames->DoubleArray,
          "BA.062" },
        { "brep:surface:cone:origin", SdfValueTypeNames->Point3dArray,
          "BA.062", true },
        { "brep:surface:cone:axis", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:cone:refDirection", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:cone:radius", SdfValueTypeNames->DoubleArray,
          "BA.062" },
        { "brep:surface:cone:semiAngle", SdfValueTypeNames->DoubleArray,
          "BA.062" },
        { "brep:surface:sphere:center", SdfValueTypeNames->Point3dArray,
          "BA.062", true },
        { "brep:surface:sphere:axis", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:sphere:refDirection", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:sphere:radius", SdfValueTypeNames->DoubleArray,
          "BA.062" },
        { "brep:surface:torus:origin", SdfValueTypeNames->Point3dArray,
          "BA.062", true },
        { "brep:surface:torus:axis", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:torus:refDirection", SdfValueTypeNames->Vector3dArray,
          "BA.062", true },
        { "brep:surface:torus:majorRadius", SdfValueTypeNames->DoubleArray,
          "BA.062" },
        { "brep:surface:torus:minorRadius", SdfValueTypeNames->DoubleArray,
          "BA.062" },
        // --- analytic 3D curves: line / circle / ellipse (edge3d + wireEdge3d) ---
        { "brep:edge3dLine:curve3d:line:origin", SdfValueTypeNames->Point3dArray,
          "BA.062", true },
        { "brep:edge3dLine:curve3d:line:direction",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:wireEdge3dLine:curve3d:line:origin",
          SdfValueTypeNames->Point3dArray, "BA.062", true },
        { "brep:wireEdge3dLine:curve3d:line:direction",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:edge3dCircle:curve3d:circle:center",
          SdfValueTypeNames->Point3dArray, "BA.062", true },
        { "brep:edge3dCircle:curve3d:circle:axis",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:edge3dCircle:curve3d:circle:refDirection",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:edge3dCircle:curve3d:circle:radius",
          SdfValueTypeNames->DoubleArray, "BA.062" },
        { "brep:wireEdge3dCircle:curve3d:circle:center",
          SdfValueTypeNames->Point3dArray, "BA.062", true },
        { "brep:wireEdge3dCircle:curve3d:circle:axis",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:wireEdge3dCircle:curve3d:circle:refDirection",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:wireEdge3dCircle:curve3d:circle:radius",
          SdfValueTypeNames->DoubleArray, "BA.062" },
        { "brep:edge3dEllipse:curve3d:ellipse:center",
          SdfValueTypeNames->Point3dArray, "BA.062", true },
        { "brep:edge3dEllipse:curve3d:ellipse:axis",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:edge3dEllipse:curve3d:ellipse:refDirection",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:edge3dEllipse:curve3d:ellipse:xRadius",
          SdfValueTypeNames->DoubleArray, "BA.062" },
        { "brep:edge3dEllipse:curve3d:ellipse:yRadius",
          SdfValueTypeNames->DoubleArray, "BA.062" },
        { "brep:wireEdge3dEllipse:curve3d:ellipse:center",
          SdfValueTypeNames->Point3dArray, "BA.062", true },
        { "brep:wireEdge3dEllipse:curve3d:ellipse:axis",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:wireEdge3dEllipse:curve3d:ellipse:refDirection",
          SdfValueTypeNames->Vector3dArray, "BA.062", true },
        { "brep:wireEdge3dEllipse:curve3d:ellipse:xRadius",
          SdfValueTypeNames->DoubleArray, "BA.062" },
        { "brep:wireEdge3dEllipse:curve3d:ellipse:yRadius",
          SdfValueTypeNames->DoubleArray, "BA.062" },
    };
    // point3d / vector3d / double3 all carry GfVec3d; the SimReady producer
    // authors positions as vector3d[] while the schema declares point3d[], so
    // accept any of these role-aliases rather than rejecting valid output.
    const std::vector<SdfValueTypeName> vec3Roles
        = { SdfValueTypeNames->Point3dArray, SdfValueTypeNames->Vector3dArray,
            SdfValueTypeNames->Double3Array };
    UsdValidationErrorVector errors;
    for (const Item &it : items) {
        std::string got;
        const bool mismatch
            = it.vecRole ? _AuthoredTypeIn(usdPrim, it.attr, vec3Roles, &got)
                         : _AuthoredTypeMismatch(usdPrim, it.attr, it.type, &got);
        if (mismatch) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->invalidAttributeDataType,
                 usdPrim,
                 TfStringPrintf("[%s] BrepArray <%s>: attribute %s has type '%s' "
                                "but expected '%s'.",
                                it.ba, usdPrim.GetPath().GetText(), it.attr,
                                got.c_str(), it.type.GetAsToken().GetText()));
        }
    }
    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArraySchemaUsage                                                       //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArraySchemaUsage(const UsdPrim &usdPrim,
                      const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    const VtArray<TfToken> faceSurfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const VtArray<TfToken> vertexPointType
        = _Read<TfToken>(brep.GetVertexPointTypeAttr());

    struct Item {
        const char *schemaToken;  // GetAppliedSchemas() membership token
        const char *driverValue;  // value to count in the driver token array
        const char *presenceAttr; // attribute whose authorship proves data
        const char *ba;
        const char *label;
        bool isVertexDriver;      // drive off vertex:pointType, else face:surfaceType
    };
    static const std::vector<Item> items = {
        { "BrepPointAPI:vertexPoint", "BrepPointAPI",
          "brep:vertexPoint:point:position", "BA.305", "vertexPoint", true },
        { "BrepSurfaceSphereAPI", "BrepSurfaceSphereAPI",
          "brep:surface:sphere:center", "BA.485", "sphere", false },
        { "BrepSurfacePlaneAPI", "BrepSurfacePlaneAPI",
          "brep:surface:plane:origin", "BA.495", "plane", false },
        { "BrepSurfaceCylinderAPI", "BrepSurfaceCylinderAPI",
          "brep:surface:cylinder:origin", "BA.505", "cylinder", false },
        { "BrepSurfaceConeAPI", "BrepSurfaceConeAPI", "brep:surface:cone:origin",
          "BA.516", "cone", false },
        { "BrepSurfaceTorusAPI", "BrepSurfaceTorusAPI",
          "brep:surface:torus:origin", "BA.526", "torus", false },
    };
    UsdValidationErrorVector errors;
    for (const Item &it : items) {
        const VtArray<TfToken> &driver
            = it.isVertexDriver ? vertexPointType : faceSurfaceType;
        const size_t count = _CountToken(driver, TfToken(it.driverValue));
        const bool hasData = _IsAuthored(usdPrim, it.presenceAttr);
        const bool applied
            = _HasAppliedSchema(usdPrim, TfToken(it.schemaToken));
        if (count > 0 && !hasData) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->schemaUsageInconsistent,
                 usdPrim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s usage is declared but "
                                "no %s data is authored.",
                                it.ba, usdPrim.GetPath().GetText(), it.label,
                                it.presenceAttr));
        }
        if (applied && count == 0) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->schemaUsageInconsistent,
                 usdPrim,
                 TfStringPrintf("[%s] BrepArray <%s>: %s is in apiSchemas but no "
                                "%s usage is declared.",
                                it.ba, usdPrim.GetPath().GetText(),
                                it.schemaToken, it.label));
        }
        // BA.583: usage declared but the API schema never applied. The two
        // checks above are each conditioned on the schema being present or the
        // usage being absent, so a prim that declares usage and applies nothing
        // satisfies both vacuously while reading as empty in any consumer that
        // resolves geometry through HasAPI.
        if (count > 0 && !applied) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->schemaUsageInconsistent,
                 usdPrim,
                 TfStringPrintf("[BA.583] BrepArray <%s>: %s contains %zu '%s' "
                                "occurrence(s), but required applied geometry "
                                "API '%s' is absent from apiSchemas.",
                                usdPrim.GetPath().GetText(),
                                it.isVertexDriver ? "vertex:pointType"
                                                  : "face:surfaceType",
                                count, it.driverValue, it.schemaToken));
        }
    }

    // BA.583, second clause. UV pcurves have no topology type-token array of
    // their own, so presence is inferred from the packed record: a non-zero
    // order or vertexCount for any edgeuse means pcurve data is authored, and
    // that requires BrepCurveUvNurbAPI. Data authored without the schema is
    // unreachable exactly as above.
    {
        const VtArray<unsigned int> uvOrder
            = _Read<unsigned int>(usdPrim.GetAttribute(
                TfToken("brep:curveUv:nurb:order")));
        const VtArray<unsigned int> uvVertexCount
            = _Read<unsigned int>(usdPrim.GetAttribute(
                TfToken("brep:curveUv:nurb:vertexCount")));

        bool hasUvRecord = false;
        for (const unsigned int v : uvOrder) {
            if (v != 0) { hasUvRecord = true; break; }
        }
        if (!hasUvRecord) {
            for (const unsigned int v : uvVertexCount) {
                if (v != 0) { hasUvRecord = true; break; }
            }
        }

        if (hasUvRecord
            && !_HasAppliedSchema(usdPrim, TfToken("BrepCurveUvNurbAPI"))) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->schemaUsageInconsistent,
                 usdPrim,
                 TfStringPrintf("[BA.583] BrepArray <%s>: authored UV NURBS "
                                "pcurve data requires BrepCurveUvNurbAPI, which "
                                "is absent from apiSchemas.",
                                usdPrim.GetPath().GetText()));
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayReferences                                                        //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayReferences(const UsdPrim &usdPrim,
                     const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    const _BrepOffsets off = _ComputeOffsets(brep);
    if (!off.ok) {
        return {};
    }
    const size_t n = off.numBreps;
    UsdValidationErrorVector errors;

    // BA.115: faceuse:faceIndex within owning Brep's face partition.
    const VtArray<unsigned int> faceIndex
        = _Read<unsigned int>(brep.GetFaceuseFaceIndexAttr());
    for (size_t b = 0; b < n; ++b) {
        for (size_t fu = off.faceuse[b];
             fu < off.faceuse[b + 1] && fu < faceIndex.size(); ++fu) {
            if (faceIndex[fu] < off.face[b] || faceIndex[fu] >= off.face[b + 1]) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->faceuseFaceIndexOutOfRange,
                     usdPrim,
                     TfStringPrintf("[BA.115] BrepArray <%s>: faceuse:faceIndex"
                                    "[%zu] = %u is outside Brep %zu's face range "
                                    "[%zu, %zu).",
                                    usdPrim.GetPath().GetText(), fu,
                                    faceIndex[fu], b, off.face[b],
                                    off.face[b + 1]));
            }
        }
    }

    // BA.205: edgeuse:edgeIndex must reference a valid edge. Edges have no
    // per-Brep count array, so validate against the global edge range; for a
    // single Brep this is exactly that Brep's range.
    const VtArray<unsigned int> edgeIndex
        = _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr());
    const size_t numEdges = _Read<TfToken>(brep.GetEdgeCurveTypeAttr()).size();
    for (size_t eu = 0; eu < edgeIndex.size(); ++eu) {
        if (edgeIndex[eu] >= numEdges) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->edgeuseEdgeIndexOutOfRange,
                 usdPrim,
                 TfStringPrintf("[BA.205] BrepArray <%s>: edgeuse:edgeIndex[%zu] "
                                "= %u is out of range [0, %zu).",
                                usdPrim.GetPath().GetText(), eu, edgeIndex[eu],
                                numEdges));
        }
    }

    // BA.200: edgeuse:nextRadialEUIndex within owning Brep's edgeuse partition.
    const VtArray<unsigned int> nextRadial
        = _Read<unsigned int>(brep.GetEdgeuseNextRadialEUIndexAttr());
    const size_t totalEu = nextRadial.size();
    for (size_t b = 0; b < n; ++b) {
        for (size_t eu = off.edgeuse[b];
             eu < off.edgeuse[b + 1] && eu < nextRadial.size(); ++eu) {
            if (nextRadial[eu] >= totalEu || nextRadial[eu] < off.edgeuse[b]
                || nextRadial[eu] >= off.edgeuse[b + 1]) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens
                         ->edgeuseNextRadialIndexOutOfRange,
                     usdPrim,
                     TfStringPrintf("[BA.200] BrepArray <%s>: "
                                    "edgeuse:nextRadialEUIndex[%zu] = %u is "
                                    "outside Brep %zu's edgeuse range [%zu, %zu).",
                                    usdPrim.GetPath().GetText(), eu,
                                    nextRadial[eu], b, off.edgeuse[b],
                                    off.edgeuse[b + 1]));
            }
        }
    }

    // Vertices also have no per-Brep count array; validate vertex-index
    // references against the global vertex range (exact for a single Brep).
    const size_t vSize
        = _Read<TfToken>(brep.GetVertexPointTypeAttr()).size();

    // BA.175: a loop with no edgeuses must reference a valid vertex.
    const VtArray<unsigned int> loopEdgeuseCount
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());
    const VtArray<unsigned int> loopVertexIndex
        = _Read<unsigned int>(brep.GetLoopVertexIndexAttr());
    for (size_t lp = 0;
         lp < loopEdgeuseCount.size() && lp < loopVertexIndex.size(); ++lp) {
        if (loopEdgeuseCount[lp] == 0u && loopVertexIndex[lp] >= vSize) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->loopVertexIndexOutOfRange,
                 usdPrim,
                 TfStringPrintf("[BA.175] BrepArray <%s>: loop:vertexIndex[%zu] = "
                                "%u (loop with no edgeuses) is out of range "
                                "[0, %zu).",
                                usdPrim.GetPath().GetText(), lp,
                                loopVertexIndex[lp], vSize));
        }
    }

    // BA.225: both components of each edge:vertexIndices pair must be valid.
    const VtArray<GfVec2i> edgeVtx
        = _Read<GfVec2i>(brep.GetEdgeVertexIndicesAttr());
    for (size_t e = 0; e < edgeVtx.size(); ++e) {
        for (int k = 0; k < 2; ++k) {
            const long long c = edgeVtx[e][k];
            if (c < 0 || c >= static_cast<long long>(vSize)) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->edgeVertexIndexOutOfRange,
                     usdPrim,
                     TfStringPrintf("[BA.225] BrepArray <%s>: edge:vertexIndices"
                                    "[%zu][%d] = %lld is out of range [0, %zu).",
                                    usdPrim.GetPath().GetText(), e, k, c, vSize));
            }
        }
    }

    // BA.265: both components of each wireEdge:vertexIndices pair must be valid.
    const VtArray<GfVec2i> wireVtx
        = _Read<GfVec2i>(brep.GetWireEdgeVertexIndicesAttr());
    for (size_t w = 0; w < wireVtx.size(); ++w) {
        for (int k = 0; k < 2; ++k) {
            const long long c = wireVtx[w][k];
            if (c < 0 || c >= static_cast<long long>(vSize)) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens
                         ->wireEdgeVertexIndexOutOfRange,
                     usdPrim,
                     TfStringPrintf("[BA.265] BrepArray <%s>: wireEdge:"
                                    "vertexIndices[%zu][%d] = %lld is out of "
                                    "range [0, %zu).",
                                    usdPrim.GetPath().GetText(), w, k, c, vSize));
            }
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayCompleteness                                                      //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayCompleteness(const UsdPrim &usdPrim,
                       const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    const _BrepOffsets off = _ComputeOffsets(brep);
    if (!off.ok) {
        return {};
    }
    const size_t n = off.numBreps;
    UsdValidationErrorVector errors;

    // BA.580: each face referenced by exactly two faceuses within its Brep.
    const VtArray<unsigned int> faceIndex
        = _Read<unsigned int>(brep.GetFaceuseFaceIndexAttr());
    if (!faceIndex.empty()) {
        for (size_t b = 0; b < n; ++b) {
            std::unordered_map<unsigned int, int> refCount;
            for (size_t fu = off.faceuse[b];
                 fu < off.faceuse[b + 1] && fu < faceIndex.size(); ++fu) {
                ++refCount[faceIndex[fu]];
            }
            for (size_t f = off.face[b]; f < off.face[b + 1]; ++f) {
                const auto it = refCount.find(static_cast<unsigned int>(f));
                const int c = it == refCount.end() ? 0 : it->second;
                if (c != 2) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->faceusePairingViolation,
                         usdPrim,
                         TfStringPrintf("[BA.580] BrepArray <%s>: face %zu in "
                                        "Brep %zu is referenced by %d faceuses "
                                        "(expected exactly 2).",
                                        usdPrim.GetPath().GetText(), f, b, c));
                }
            }
        }
    }

    // BA.581: radial edgeuse chains must close into cycles.
    const VtArray<unsigned int> nextRadial
        = _Read<unsigned int>(brep.GetEdgeuseNextRadialEUIndexAttr());
    const size_t totalEu = nextRadial.size();
    if (totalEu > 0) {
        for (size_t b = 0; b < n; ++b) {
            const size_t euEnd = std::min(off.edgeuse[b + 1], totalEu);
            for (size_t start = std::min(off.edgeuse[b], totalEu);
                 start < euEnd; ++start) {
                const size_t maxSteps = off.edgeuse[b + 1] - off.edgeuse[b];
                size_t cur = start;
                bool closed = false;
                for (size_t step = 0; step < maxSteps; ++step) {
                    if (cur >= totalEu) {
                        break;
                    }
                    const unsigned int nxt = nextRadial[cur];
                    if (nxt >= totalEu) {
                        break;
                    }
                    if (nxt == start) {
                        closed = true;
                        break;
                    }
                    cur = nxt;
                }
                if (!closed) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->radialEdgeuseChainNotClosed,
                         usdPrim,
                         TfStringPrintf("[BA.581] BrepArray <%s>: radial edgeuse "
                                        "chain starting at edgeuse %zu does not "
                                        "close.",
                                        usdPrim.GetPath().GetText(), start));
                }
            }
        }
    }

    // BA.582: every edge must be referenced by at least one edgeuse. Iterate the
    // authored edge array directly (the per-Brep offsets undercount orphans).
    const VtArray<TfToken> edgeCurveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const VtArray<unsigned int> edgeIndex
        = _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr());
    if (!edgeCurveType.empty()) {
        std::unordered_set<unsigned int> referenced(edgeIndex.begin(),
                                                    edgeIndex.end());
        for (size_t e = 0; e < edgeCurveType.size(); ++e) {
            if (referenced.find(static_cast<unsigned int>(e))
                == referenced.end()) {
                _Err(&errors, UsdSolidValidationErrorNameTokens->orphanEdge,
                     usdPrim,
                     TfStringPrintf("[BA.582] BrepArray <%s>: edge %zu is not "
                                    "referenced by any edgeuse (orphan edge).",
                                    usdPrim.GetPath().GetText(), e));
            }
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArraySolidClosure                                                      //
// -------------------------------------------------------------------------- //
// A region declared "solidRegion" bounds a finite volume, so its shell(s) must
// be CLOSED: every non-degenerate edge on a solid shell must be shared by at
// least two edgeuses whose radial ring actually links them. A single-use
// (laminar / identity-ring) boundary edge on a solid shell means the surface is
// open -- surface soup or an open sheet mislabeled as a solid. The same
// single-use edges on a voidRegion-only (sheet/wire/point) model are legal and
// are NOT flagged. CLOSED-LOOP edges (start vertex == end vertex -- a
// full-circle rim or a seam ring, e.g. at the corner singularities of a
// filleted solid) are exempt from the shared-edge count: they are full-length
// legal edges, NOT rule-381 degenerate edges. Zero-length (degenerate) edges
// are never exempted anywhere -- BA.230 (_BrepArrayDegenerateEdges) flags them
// as errors, measured against brep:intersectTol3d per proposal rule 381.
// (BA.590/BA.591 are new checks; reconcile numbering with brep_validator.py.)
UsdValidationErrorVector
_BrepArraySolidClosure(const UsdPrim &usdPrim,
                       const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    const _BrepOffsets off = _ComputeOffsets(brep);
    if (!off.ok) {
        return {};
    }
    const size_t n = off.numBreps;
    UsdValidationErrorVector errors;

    const VtArray<TfToken> regionType
        = _Read<TfToken>(brep.GetRegionTypeAttr());
    const VtArray<unsigned int> regionShellCount
        = _Read<unsigned int>(brep.GetRegionShellCountAttr());
    const VtArray<unsigned int> shellFaceuseCount
        = _Read<unsigned int>(brep.GetShellFaceuseCountAttr());
    const VtArray<unsigned int> faceuseFaceIndex
        = _Read<unsigned int>(brep.GetFaceuseFaceIndexAttr());
    const VtArray<unsigned int> faceLoopCount
        = _Read<unsigned int>(brep.GetFaceLoopCountAttr());
    const VtArray<unsigned int> loopEdgeuseCount
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());
    const VtArray<unsigned int> edgeuseEdgeIndex
        = _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr());
    const VtArray<unsigned int> nextRadial
        = _Read<unsigned int>(brep.GetEdgeuseNextRadialEUIndexAttr());
    const VtArray<GfVec2i> edgeVtx
        = _Read<GfVec2i>(brep.GetEdgeVertexIndicesAttr());

    static const TfToken solidRegionTok("solidRegion");

    for (size_t b = 0; b < n; ++b) {
        // 1. Which faces of this Brep lie on a solidRegion shell?
        std::unordered_set<unsigned int> solidFaces;
        size_t shellCursor = off.shell[b];
        size_t faceuseCursor = off.faceuse[b];
        for (size_t r = off.region[b]; r < off.region[b + 1]; ++r) {
            const bool isSolid
                = r < regionType.size() && regionType[r] == solidRegionTok;
            const unsigned int nsh
                = r < regionShellCount.size() ? regionShellCount[r] : 0u;
            for (unsigned int s = 0; s < nsh; ++s) {
                const size_t shellIdx = shellCursor + s;
                const unsigned int nfu = shellIdx < shellFaceuseCount.size()
                    ? shellFaceuseCount[shellIdx] : 0u;
                for (unsigned int k = 0; k < nfu; ++k) {
                    const size_t fu = faceuseCursor + k;
                    if (isSolid && fu < faceuseFaceIndex.size()) {
                        solidFaces.insert(faceuseFaceIndex[fu]);
                    }
                }
                faceuseCursor += nfu;
            }
            shellCursor += nsh;
        }
        if (solidFaces.empty()) {
            continue;   // pure sheet / wire / point body: nothing to enforce.
        }

        // 2. Map each edgeuse of this Brep to its owning face; accumulate, per
        //    edge, its referencing edgeuses and whether any belongs to a solid
        //    face. edgeuse -> loop (loop:edgeuseCount) -> face (face:loopCount).
        std::unordered_map<unsigned int, std::vector<size_t>> edgeToEUs;
        std::unordered_set<unsigned int> solidEdges;
        size_t loopCursor = off.loop[b];
        size_t euCursor = off.edgeuse[b];
        for (size_t f = off.face[b]; f < off.face[b + 1]; ++f) {
            const bool faceIsSolid
                = solidFaces.count(static_cast<unsigned int>(f)) > 0;
            const unsigned int nlp
                = f < faceLoopCount.size() ? faceLoopCount[f] : 0u;
            for (unsigned int lk = 0; lk < nlp; ++lk) {
                const size_t lp = loopCursor + lk;
                const unsigned int neu
                    = lp < loopEdgeuseCount.size() ? loopEdgeuseCount[lp] : 0u;
                for (unsigned int ek = 0; ek < neu; ++ek) {
                    const size_t eu = euCursor + ek;
                    if (eu >= edgeuseEdgeIndex.size()) {
                        continue;
                    }
                    const unsigned int e = edgeuseEdgeIndex[eu];
                    edgeToEUs[e].push_back(eu);
                    if (faceIsSolid) {
                        solidEdges.insert(e);
                    }
                }
                euCursor += neu;
            }
            loopCursor += nlp;
        }

        // 3. Enforce closure on every solid edge.
        const bool ringAuthored = nextRadial.size() >= off.edgeuse[b + 1];
        for (const unsigned int e : solidEdges) {
            // A closed-loop edge (start vertex == end vertex: full-circle rim
            // or seam ring) is a legal single-use seam on a closed analytic
            // patch; exempt it from the shared-edge count. This is NOT a
            // rule-381 degenerate (zero-length) edge -- those are errors,
            // flagged by BA.230.
            if (e < edgeVtx.size() && edgeVtx[e][0] == edgeVtx[e][1]) {
                continue;
            }
            const std::vector<size_t> &eus = edgeToEUs[e];

            // BA.590: a solid edge must be shared (>= 2 edgeuses). A single-use
            // edge is an open boundary -> the solid shell is not closed.
            if (eus.size() < 2) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->solidShellOpenEdge,
                     usdPrim,
                     TfStringPrintf(
                         "[BA.590] BrepArray <%s>: edge %u on a solidRegion "
                         "shell of Brep %zu is referenced by %zu edgeuse(s) "
                         "(expected >= 2); a solid shell must be closed (no "
                         "single-use boundary edges). This is an open surface "
                         "or sheet mislabeled as a solid.",
                         usdPrim.GetPath().GetText(), e, b, eus.size()));
                continue;
            }

            // BA.591: the radial ring of a solid edge must form one cycle that
            // visits exactly the edgeuses referencing that edge. Only checked
            // when the ring is authored; an absent ring is reported by
            // BrepArrayAuthorship.
            if (!ringAuthored) {
                continue;
            }
            std::unordered_set<size_t> expected(eus.begin(), eus.end());
            std::unordered_set<size_t> visited;
            size_t cur = eus.front();
            for (size_t step = 0; step < expected.size() + 1; ++step) {
                if (cur >= nextRadial.size() || !visited.insert(cur).second) {
                    break;
                }
                cur = nextRadial[cur];
            }
            if (visited != expected) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens
                         ->solidShellBrokenRadialRing,
                     usdPrim,
                     TfStringPrintf(
                         "[BA.591] BrepArray <%s>: the radial ring of edge %u "
                         "on a solidRegion shell of Brep %zu does not link its "
                         "%zu edgeuses into a single cycle (an identity/self "
                         "ring leaves the shared faces unconnected).",
                         usdPrim.GetPath().GetText(), e, b, eus.size()));
            }
        }
    }

    return errors;
}

// ========================================================================== //
// Shared edge-geometry support (degenerate edges + curve-endpoint checks)    //
// ========================================================================== //

// One edge/wireEdge instance's resolved 3D geometry, enough to decide
// degeneracy and to recover the curve's start/end points. Populated per
// curve family (NURBS control hull, or analytic line/circle/ellipse).
struct _EdgeGeom {
    bool hasGeom = false;       // geometry for this edge was found
    // NURBS: the control-vertex hull for this edge (in curve order).
    bool isNurb = false;
    std::vector<GfVec3d> cvs;
    // Analytic: the curve's arc length over its authored parameter span (a raw
    // length, no threshold applied here so the caller can measure it against the
    // Brep's brep:intersectTol3d consistently with the NURBS branch -- register
    // row 13). Negative when unset (NURBS edges, or an unresolved analytic edge).
    double arcLen = -1.0;
    // Curve endpoints in curve-parametric order (start = param min, end = max).
    // Valid whenever hasGeom is true.
    GfVec3d start{ 0, 0, 0 };
    GfVec3d end{ 0, 0, 0 };
};

// Resolve every edge (or wireEdge, when wire==true) of a BrepArray to its 3D
// geometry. NURBS edges pull their control hull from the edge3dNurb /
// wireEdge3dNurb strata (a clamped curve passes through its first and last
// control vertex, so those are its endpoints). Analytic edges pull line /
// circle / ellipse parameters and evaluate the curve at the authored edge:range
// endpoints. Instances of one curve family are packed in edge order, matching
// the BrepArrayAnalyticCurves / BrepArrayNurbs conventions.
std::vector<_EdgeGeom>
_ResolveEdgeGeom(const UsdPrim &prim, const UsdSolidBrepArray &brep, bool wire)
{
    const std::string inst = wire ? "wireEdge" : "edge";
    const VtArray<TfToken> curveType = wire
        ? _Read<TfToken>(brep.GetWireEdgeCurveTypeAttr())
        : _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const VtArray<double> range = wire
        ? _Read<double>(brep.GetWireEdgeRangeAttr())
        : _Read<double>(brep.GetEdgeRangeAttr());
    const size_t numE = curveType.size();
    std::vector<_EdgeGeom> geom(numE);

    static const TfToken nurbTok("BrepCurve3dNurbAPI");
    static const TfToken lineTok("BrepCurve3dLineAPI");
    static const TfToken circleTok("BrepCurve3dCircleAPI");
    static const TfToken ellipseTok("BrepCurve3dEllipseAPI");

    // --- NURBS hull: per-edge control vertices sliced by vertexCount. --- //
    const std::string nurbBase
        = std::string("brep:") + (wire ? "wireEdge3dNurb" : "edge3dNurb")
        + ":curve3d:nurb:";
    const VtArray<unsigned int> nVC
        = _ReadName<unsigned int>(prim, nurbBase + "vertexCount");
    const VtArray<GfVec3d> nCv
        = _ReadName<GfVec3d>(prim, nurbBase + "controlVertices");
    size_t nurbCursor = 0;
    size_t nurbInst = 0;

    // --- Analytic parameter arrays (packed per curve family, edge order). --- //
    const std::string lineBase
        = std::string("brep:") + inst + "3dLine:curve3d:line:";
    const VtArray<GfVec3d> lineOrigin
        = _ReadName<GfVec3d>(prim, lineBase + "origin");
    const VtArray<GfVec3d> lineDir
        = _ReadName<GfVec3d>(prim, lineBase + "direction");
    size_t lineInst = 0;

    const std::string circleBase
        = std::string("brep:") + inst + "3dCircle:curve3d:circle:";
    const VtArray<GfVec3d> circleCenter
        = _ReadName<GfVec3d>(prim, circleBase + "center");
    const VtArray<GfVec3d> circleAxis
        = _ReadName<GfVec3d>(prim, circleBase + "axis");
    const VtArray<GfVec3d> circleRef
        = _ReadName<GfVec3d>(prim, circleBase + "refDirection");
    const VtArray<double> circleRadius
        = _ReadName<double>(prim, circleBase + "radius");
    size_t circleInst = 0;

    const std::string ellipseBase
        = std::string("brep:") + inst + "3dEllipse:curve3d:ellipse:";
    const VtArray<GfVec3d> ellipseCenter
        = _ReadName<GfVec3d>(prim, ellipseBase + "center");
    const VtArray<GfVec3d> ellipseAxis
        = _ReadName<GfVec3d>(prim, ellipseBase + "axis");
    const VtArray<GfVec3d> ellipseRef
        = _ReadName<GfVec3d>(prim, ellipseBase + "refDirection");
    const VtArray<double> ellipseX
        = _ReadName<double>(prim, ellipseBase + "xRadius");
    const VtArray<double> ellipseY
        = _ReadName<double>(prim, ellipseBase + "yRadius");
    size_t ellipseInst = 0;

    // Evaluate a conic (circle/ellipse) point:
    //   center + cos(t)*xr*ref + sin(t)*yr*(axis x ref).
    const auto conicAt = [](const GfVec3d &center, const GfVec3d &axis,
                            const GfVec3d &ref, double xr, double yr,
                            double t) {
        const GfVec3d yDir = GfCross(axis, ref);
        return center + std::cos(t) * xr * ref + std::sin(t) * yr * yDir;
    };

    for (size_t e = 0; e < numE; ++e) {
        _EdgeGeom &g = geom[e];
        const double t0 = 2 * e < range.size() ? range[2 * e] : 0.0;
        const double t1 = 2 * e + 1 < range.size() ? range[2 * e + 1] : 0.0;
        const TfToken &ct = curveType[e];
        if (ct == nurbTok) {
            const unsigned int vc
                = nurbInst < nVC.size() ? nVC[nurbInst] : 0u;
            ++nurbInst;
            if (vc >= 1 && nurbCursor + vc <= nCv.size()) {
                g.hasGeom = true;
                g.isNurb = true;
                g.cvs.assign(nCv.begin() + nurbCursor,
                             nCv.begin() + nurbCursor + vc);
                g.start = g.cvs.front();
                g.end = g.cvs.back();
            }
            nurbCursor += vc;
        } else if (ct == lineTok) {
            const size_t i = lineInst++;
            if (i < lineOrigin.size() && i < lineDir.size()) {
                g.hasGeom = true;
                g.start = lineOrigin[i] + t0 * lineDir[i];
                g.end = lineOrigin[i] + t1 * lineDir[i];
                // Arc length = |span| * |direction|; measured against
                // brep:intersectTol3d by the caller (register row 13).
                g.arcLen = std::abs((t1 - t0) * lineDir[i].GetLength());
            }
        } else if (ct == circleTok) {
            const size_t i = circleInst++;
            if (i < circleCenter.size() && i < circleAxis.size()
                && i < circleRef.size() && i < circleRadius.size()) {
                g.hasGeom = true;
                const double r = circleRadius[i];
                g.start = conicAt(circleCenter[i], circleAxis[i], circleRef[i],
                                  r, r, t0);
                g.end = conicAt(circleCenter[i], circleAxis[i], circleRef[i],
                                r, r, t1);
                // Exact circular arc length = r * |span|.
                g.arcLen = std::abs(r * (t1 - t0));
            }
        } else if (ct == ellipseTok) {
            const size_t i = ellipseInst++;
            if (i < ellipseCenter.size() && i < ellipseAxis.size()
                && i < ellipseRef.size() && i < ellipseX.size()
                && i < ellipseY.size()) {
                g.hasGeom = true;
                g.start = conicAt(ellipseCenter[i], ellipseAxis[i],
                                  ellipseRef[i], ellipseX[i], ellipseY[i], t0);
                g.end = conicAt(ellipseCenter[i], ellipseAxis[i],
                                ellipseRef[i], ellipseX[i], ellipseY[i], t1);
                // Upper-bound arc length = max(xRadius, yRadius) * |span|. The
                // larger semi-axis bounds the true elliptic arc from above, so
                // an edge only registers as degenerate when even that bound is
                // below tolerance -- an eccentric ellipse (xr >> yr) with a
                // short span past the minor axis is never falsely collapsed
                // (register row 13; previously used the mean radius).
                const double maxR = std::max(ellipseX[i], ellipseY[i]);
                g.arcLen = std::abs(maxR * (t1 - t0));
            }
        }
    }
    return geom;
}

// -------------------------------------------------------------------------- //
// BrepArrayDegenerateEdges                                                   //
// -------------------------------------------------------------------------- //
// Proposal rule 381: "degenerate geometry is not allowed, where degeneracy is
// measured against tolerance." An edge is degenerate when its 3D curve has no
// extent: for a NURBS curve, every control vertex coincides (Umhoefer: "we
// should be able to catch these degenerate edges because all of the control
// points are equal"); for an analytic curve, the arc length falls below
// tolerance. BOTH branches now measure against the same Brep tolerance
// (brep:intersectTol3d, with the shared reader fallback) -- the analytic branch
// previously used a hard-coded 1e-4 curve epsilon while the NURBS branch used
// the Brep tolerance, so the same physical gap was called degenerate on one
// curve family and healthy on another (register row 13). Both edge3d* and
// wireEdge3d* instances are checked. (BA.230.)
UsdValidationErrorVector
_BrepArrayDegenerateEdges(const UsdPrim &usdPrim,
                          const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    // Tolerance: the first authored brep:intersectTol3d, else the shared reader
    // fallback (see _FirstAuthoredIntersectTol3d).
    const double tol3d = _FirstAuthoredIntersectTol3d(brep);

    UsdValidationErrorVector errors;

    for (int wirePass = 0; wirePass < 2; ++wirePass) {
        const bool wire = wirePass == 1;
        const char *label = wire ? "wireEdge" : "edge";
        const std::vector<_EdgeGeom> geom
            = _ResolveEdgeGeom(usdPrim, brep, wire);
        for (size_t e = 0; e < geom.size(); ++e) {
            const _EdgeGeom &g = geom[e];
            if (!g.hasGeom) {
                continue;   // no resolvable geometry (reported elsewhere).
            }
            bool degenerate = false;
            if (g.isNurb) {
                // All control vertices equal within tolerance => the curve
                // collapses to a point.
                degenerate = true;
                for (size_t c = 1; c < g.cvs.size(); ++c) {
                    if ((g.cvs[c] - g.cvs[0]).GetLength() > tol3d) {
                        degenerate = false;
                        break;
                    }
                }
                if (g.cvs.size() < 2) {
                    // A single control vertex is a point, i.e. degenerate.
                    degenerate = true;
                }
            } else {
                // Analytic: the curve's arc length (an upper bound for the
                // ellipse) is degenerate when it does not exceed the Brep
                // tolerance. arcLen < 0 means the analytic parameters were not
                // resolvable, which is reported elsewhere.
                degenerate = g.arcLen >= 0.0 && g.arcLen <= tol3d;
            }
            if (degenerate) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->degenerateEdge, usdPrim,
                     TfStringPrintf(
                         "[BA.230] BrepArray <%s>: %s %zu is degenerate (its 3D "
                         "curve has no extent within brep:intersectTol3d = %g; "
                         "%s). Degenerate geometry is not allowed (proposal rule "
                         "381).",
                         usdPrim.GetPath().GetText(), label, e, tol3d,
                         g.isNurb ? "all NURBS control vertices are equal"
                                  : TfStringPrintf(
                                        "the analytic curve arc length %g is "
                                        "within tolerance",
                                        g.arcLen).c_str()));
            }
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayEdgeCurveVertices                                                 //
// -------------------------------------------------------------------------- //
// Proposal rule 434: "the curve runs from the start vertex to the end vertex."
// An edge's authored vertexIndices name the start and end vertices; the edge's
// 3D curve, evaluated at its parametric endpoints, must reach those vertex
// positions in that order. This catches edge:vertexIndices authored in
// topological (loop-traversal) order rather than curve-parametric order -- a
// reversed edge whose curve start actually lands on the "end" vertex. Distances
// are measured against brep:intersectTol3d. Degenerate edges (start vertex ==
// end vertex) are exempt: their two endpoints coincide, so orientation is
// meaningless (and rule 381 / BA.230 already reports them). (BA.240.)
UsdValidationErrorVector
_BrepArrayEdgeCurveVertices(const UsdPrim &usdPrim,
                            const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    const VtArray<GfVec3d> vpos
        = _ReadName<GfVec3d>(usdPrim, "brep:vertexPoint:point:position");
    if (vpos.empty()) {
        // Without vertex positions the endpoints cannot be checked; the
        // vertexPoint data is required by BrepArraySchemaUsage when declared.
        return {};
    }

    const double tol3d = _FirstAuthoredIntersectTol3d(brep);

    UsdValidationErrorVector errors;

    struct Pass {
        bool wire;
        const char *label;
        VtArray<GfVec2i> vtxIdx;
    };
    const std::vector<Pass> passes = {
        { false, "edge", _Read<GfVec2i>(brep.GetEdgeVertexIndicesAttr()) },
        { true, "wireEdge",
          _Read<GfVec2i>(brep.GetWireEdgeVertexIndicesAttr()) },
    };

    for (const Pass &p : passes) {
        const std::vector<_EdgeGeom> geom
            = _ResolveEdgeGeom(usdPrim, brep, p.wire);
        const size_t numE = std::min(geom.size(), p.vtxIdx.size());
        for (size_t e = 0; e < numE; ++e) {
            const _EdgeGeom &g = geom[e];
            if (!g.hasGeom) {
                continue;
            }
            const int vs = p.vtxIdx[e][0];
            const int ve = p.vtxIdx[e][1];
            if (vs < 0 || ve < 0 || vs >= static_cast<int>(vpos.size())
                || ve >= static_cast<int>(vpos.size())) {
                continue;   // out-of-range indices are reported by References.
            }
            if (vs == ve) {
                continue;   // degenerate edge: exempt (see BA.230).
            }
            const GfVec3d &pStart = vpos[vs];
            const GfVec3d &pEnd = vpos[ve];
            const double dForward = (g.start - pStart).GetLength()
                + (g.end - pEnd).GetLength();
            const double dReverse = (g.start - pEnd).GetLength()
                + (g.end - pStart).GetLength();
            // The endpoints match the authored order when the curve start is at
            // the start vertex and the curve end is at the end vertex.
            const bool matchesForward = (g.start - pStart).GetLength() <= tol3d
                && (g.end - pEnd).GetLength() <= tol3d;
            if (matchesForward) {
                continue;
            }
            // Distinguish a mere reversal (curve runs end->start) from a genuine
            // geometry/topology mismatch, for a clearer message.
            const bool reversed = (g.start - pEnd).GetLength() <= tol3d
                && (g.end - pStart).GetLength() <= tol3d;
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->edgeCurveVertexMismatch,
                 usdPrim,
                 TfStringPrintf(
                     "[BA.240] BrepArray <%s>: %s %zu curve endpoints do not "
                     "match its vertexIndices (%d, %d) within "
                     "brep:intersectTol3d = %g. %s The curve must run from the "
                     "start vertex to the end vertex (proposal rule 434); "
                     "vertexIndices appear to be authored in topological rather "
                     "than curve-parametric order.",
                     usdPrim.GetPath().GetText(), p.label, e, vs, ve, tol3d,
                     reversed
                         ? "The curve runs from the end vertex to the start "
                           "vertex (endpoints are swapped)."
                         : TfStringPrintf(
                               "Forward endpoint error %g, reversed %g.",
                               dForward, dReverse)
                               .c_str()));
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayContainment                                                       //
// -------------------------------------------------------------------------- //
bool
_FloatClose(double a, double b)
{
    return std::abs(a - b)
        <= std::max(1e-5 * std::max(std::abs(a), std::abs(b)), 1e-6);
}

UsdValidationErrorVector
_BrepArrayContainment(const UsdPrim &usdPrim,
                      const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    const VtArray<GfVec3d> extent = _Read<GfVec3d>(brep.GetBrepExtentAttr());
    const size_t numBoxes = extent.size() / 2;
    UsdValidationErrorVector errors;

    // Containment slop for BA.310/365/465: a vertex or control point may sit a
    // tolerance outside a brep:extent box without being a real violation. The
    // slop follows the intersectTol3d ladder used by BA.240 -- the Brep's own
    // authored 3D tolerance -- rather than the former hard-coded 1e-11, which
    // was tighter than float32 round-off. Real CAD is frequently authored on a
    // float path (the prim's `extent` is float3, and brep:extent corners are
    // commonly float-derived), so a double vertex compared to a float-quantized
    // box overshot 1e-11 routinely and turned BA.310 into a hard-Error false
    // positive (register row 14). _DomainTol (1e-6) floors the slop so an asset
    // that authors an over-tight tolerance is still judged against at least the
    // domain tolerance; a per-corner relative float32 term (~1.2e-7 * |corner|)
    // is added so the slop tracks the quantization at large coordinates.
    const double tol3d = _FirstAuthoredIntersectTol3d(brep);
    const double baseSlop = std::max(tol3d, _DomainTol);
    // std::numeric_limits<float>::epsilon() ~ 1.19e-7; a float32 value carries
    // up to ~0.5 ulp of quantization, i.e. ~0.6e-7 * magnitude.
    const double floatRel = 0.6e-7;

    // BA.040/045/050: each brep:extent box within the prim's extent.
    const VtArray<GfVec3f> primExtent = _ReadName<GfVec3f>(usdPrim, "extent");
    if (primExtent.size() >= 2) {
        const char *const axisNames[3] = { "X", "Y", "Z" };
        const char *const baCodes[3] = { "BA.040", "BA.045", "BA.050" };
        for (size_t b = 0; b < numBoxes; ++b) {
            const GfVec3d &mn = extent[2 * b];
            const GfVec3d &mx = extent[2 * b + 1];
            for (int a = 0; a < 3; ++a) {
                const double pmn = primExtent[0][a];
                const double pmx = primExtent[1][a];
                if ((mn[a] < pmn && !_FloatClose(mn[a], pmn))
                    || (mx[a] > pmx && !_FloatClose(mx[a], pmx))) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->brepExtentOutsidePrimExtent,
                         usdPrim,
                         TfStringPrintf("[%s] BrepArray <%s>: brep:extent for "
                                        "Brep %zu exceeds the prim's %s extent "
                                        "[%g, %g] (box [%g, %g]).",
                                        baCodes[a], usdPrim.GetPath().GetText(),
                                        b, axisNames[a], pmn, pmx, mn[a], mx[a]));
                }
            }
        }
    }

    if (numBoxes == 0) {
        return errors;
    }

    // A point is contained if it lies within ANY brep:extent box (within
    // tolerance). Per-point Brep attribution is not derivable from the flat
    // data for vertices/control points (no per-Brep count array), so the union
    // is used; for a single Brep this is exactly that Brep's box.
    const auto insideAnyExtent = [&](const GfVec3d &p) {
        for (size_t b = 0; b < numBoxes; ++b) {
            const GfVec3d &mn = extent[2 * b];
            const GfVec3d &mx = extent[2 * b + 1];
            bool inside = true;
            for (int k = 0; k < 3; ++k) {
                // Per-axis slop = tolerance ladder + a float32-quantization term
                // scaled to the box corner's magnitude (register row 14).
                const double loSlop
                    = baseSlop + floatRel * std::abs(mn[k]);
                const double hiSlop
                    = baseSlop + floatRel * std::abs(mx[k]);
                if (p[k] < mn[k] - loSlop || p[k] > mx[k] + hiSlop) {
                    inside = false;
                    break;
                }
            }
            if (inside) {
                return true;
            }
        }
        return false;
    };

    // BA.310: vertex positions lie on the solid boundary, so they must be
    // within a brep:extent box (Error; reports every offending vertex).
    const VtArray<GfVec3d> vpos
        = _ReadName<GfVec3d>(usdPrim, "brep:vertexPoint:point:position");
    for (size_t v = 0; v < vpos.size(); ++v) {
        if (!insideAnyExtent(vpos[v])) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens
                     ->vertexPositionOutsideBrepExtent,
                 usdPrim,
                 TfStringPrintf("[BA.310] BrepArray <%s>: vertex position %zu "
                                "lies outside all brep:extent boxes.",
                                usdPrim.GetPath().GetText(), v));
        }
    }

    // BA.365 / BA.465: NURBS control hulls can legitimately extend beyond the
    // surface (and hence the extent) for rational/curved geometry, so a control
    // point outside the extent is reported as a Warning, not an Error. One
    // finding per stratum keeps the output quiet on valid curved breps.
    const VtArray<GfVec3d> edgeCv = _ReadName<GfVec3d>(
        usdPrim, "brep:edge3dNurb:curve3d:nurb:controlVertices");
    for (size_t c = 0; c < edgeCv.size(); ++c) {
        if (!insideAnyExtent(edgeCv[c])) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->controlPointOutsideBrepExtent,
                 usdPrim,
                 TfStringPrintf("[BA.365] BrepArray <%s>: edge3dNurb control "
                                "vertex %zu lies outside all brep:extent boxes "
                                "(NURBS control hulls may legitimately exceed the "
                                "surface bounds).",
                                usdPrim.GetPath().GetText(), c),
                 UsdValidationErrorType::Warn);
            break;
        }
    }
    const VtArray<GfVec3d> surfCv
        = _ReadName<GfVec3d>(usdPrim, "brep:surface:nurb:controlVertices");
    for (size_t c = 0; c < surfCv.size(); ++c) {
        if (!insideAnyExtent(surfCv[c])) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->controlPointOutsideBrepExtent,
                 usdPrim,
                 TfStringPrintf("[BA.465] BrepArray <%s>: surface NURBS control "
                                "vertex %zu lies outside all brep:extent boxes "
                                "(NURBS control hulls may legitimately exceed the "
                                "surface bounds).",
                                usdPrim.GetPath().GetText(), c),
                 UsdValidationErrorType::Warn);
            break;
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArraySpans                                                             //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArraySpans(const UsdPrim &usdPrim,
                const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    UsdValidationErrorVector errors;

    // BA.560-565: analytic surface face:range domain limits.
    const VtArray<TfToken> faceSurfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());
    const size_t numFaces = faceSurfaceType.size();
    if (faceRange.size() >= 2 * numFaces) {
        const TfToken sphere("BrepSurfaceSphereAPI");
        const TfToken cylinder("BrepSurfaceCylinderAPI");
        const TfToken cone("BrepSurfaceConeAPI");
        const TfToken torus("BrepSurfaceTorusAPI");
        for (size_t fi = 0; fi < numFaces; ++fi) {
            const GfVec2d &uvMin = faceRange[2 * fi];
            const GfVec2d &uvMax = faceRange[2 * fi + 1];
            const double uSpan = uvMax[0] - uvMin[0];
            const double vSpan = uvMax[1] - uvMin[1];
            const TfToken &t = faceSurfaceType[fi];
            if (t == sphere) {
                if (uSpan > _TwoPi + _DomainTol) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->surfaceDomainSpanExceeded,
                         usdPrim,
                         TfStringPrintf("[BA.560] BrepArray <%s>: sphere face %zu "
                                        "U span %g exceeds 2*pi.",
                                        usdPrim.GetPath().GetText(), fi, uSpan));
                }
                if (uvMin[1] < -_HalfPi - _DomainTol
                    || uvMax[1] > _HalfPi + _DomainTol) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->sphereVDomainOutOfBounds,
                         usdPrim,
                         TfStringPrintf("[BA.561] BrepArray <%s>: sphere face %zu "
                                        "V range [%g, %g] is outside "
                                        "[-pi/2, pi/2].",
                                        usdPrim.GetPath().GetText(), fi, uvMin[1],
                                        uvMax[1]));
                }
            } else if (t == cylinder && uSpan > _TwoPi + _DomainTol) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->surfaceDomainSpanExceeded,
                     usdPrim,
                     TfStringPrintf("[BA.562] BrepArray <%s>: cylinder face %zu U "
                                    "span %g exceeds 2*pi.",
                                    usdPrim.GetPath().GetText(), fi, uSpan));
            } else if (t == cone && uSpan > _TwoPi + _DomainTol) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->surfaceDomainSpanExceeded,
                     usdPrim,
                     TfStringPrintf("[BA.563] BrepArray <%s>: cone face %zu U "
                                    "span %g exceeds 2*pi.",
                                    usdPrim.GetPath().GetText(), fi, uSpan));
            } else if (t == torus) {
                if (uSpan > _TwoPi + _DomainTol) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->surfaceDomainSpanExceeded,
                         usdPrim,
                         TfStringPrintf("[BA.564] BrepArray <%s>: torus face %zu "
                                        "U span %g exceeds 2*pi.",
                                        usdPrim.GetPath().GetText(), fi, uSpan));
                }
                if (vSpan > _TwoPi + _DomainTol) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->surfaceDomainSpanExceeded,
                         usdPrim,
                         TfStringPrintf("[BA.565] BrepArray <%s>: torus face %zu "
                                        "V span %g exceeds 2*pi.",
                                        usdPrim.GetPath().GetText(), fi, vSpan));
                }
            }
        }
    }

    // BA.570/571: circle/ellipse edge & wireEdge parameter spans. A periodic
    // (circle/ellipse) 3D curve is 2*pi-periodic, so an edge's parameter span
    // (range[max] - range[min]) must not exceed one full period plus tolerance.
    // A real STEP->UsdSolid conversion authored truncated-pi domains that were
    // only caught at the face (surface) level (BA.560-565); the edge-parametric
    // span must be bounded too. Reversed ranges (max < min, hence a negative
    // span) are owned by BrepArrayRanges (BA.235/BA.275, InvalidEdgeRangeOrder /
    // InvalidWireEdgeRangeOrder) and are not re-flagged here.
    //
    // Tolerance: mirror BA.240 -- use the shared authored-tol resolution
    // (_FirstAuthoredIntersectTol3d), floored at the analytic domain tolerance
    // so the bound is never tighter than the surface-domain checks above (a
    // benign floating-point overshoot must not become a false positive on an
    // otherwise-conformant full-period edge). intersectTol3d is a 3D length used
    // here as a parametric slop; the _DomainTol floor keeps the comparison
    // meaningful for either interpretation.
    const double tol3d = _FirstAuthoredIntersectTol3d(brep);
    const double spanAllow = std::max(tol3d, _DomainTol);
    const TfToken circle("BrepCurve3dCircleAPI");
    const TfToken ellipse("BrepCurve3dEllipseAPI");
    struct Kind {
        VtArray<TfToken> curveType;
        VtArray<double> range;
        const char *label;
    };
    const std::vector<Kind> kinds = {
        { _Read<TfToken>(brep.GetEdgeCurveTypeAttr()),
          _Read<double>(brep.GetEdgeRangeAttr()), "edge" },
        { _Read<TfToken>(brep.GetWireEdgeCurveTypeAttr()),
          _Read<double>(brep.GetWireEdgeRangeAttr()), "wireEdge" },
    };
    for (const Kind &kind : kinds) {
        const size_t numE = kind.curveType.size();
        if (kind.range.size() < 2 * numE) {
            continue;
        }
        for (size_t ei = 0; ei < numE; ++ei) {
            const double span = kind.range[2 * ei + 1] - kind.range[2 * ei];
            if (kind.curveType[ei] == circle && span > _TwoPi + spanAllow) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->edgeRangeSpanExceeded,
                     usdPrim,
                     TfStringPrintf("[BA.570] BrepArray <%s>: circle %s %zu "
                                    "parameter span %g exceeds one period "
                                    "(2*pi) within tolerance %g.",
                                    usdPrim.GetPath().GetText(), kind.label, ei,
                                    span, spanAllow));
            } else if (kind.curveType[ei] == ellipse
                       && span > _TwoPi + spanAllow) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->edgeRangeSpanExceeded,
                     usdPrim,
                     TfStringPrintf("[BA.571] BrepArray <%s>: ellipse %s %zu "
                                    "parameter span %g exceeds one period "
                                    "(2*pi) within tolerance %g.",
                                    usdPrim.GetPath().GetText(), kind.label, ei,
                                    span, spanAllow));
            }
        }
    }

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayAnalyticCurves                                                    //
// -------------------------------------------------------------------------- //

// The smallest brep:intersectTol3d an edge check will accept as a usable
// distance bound. A tolerance below it, or a non-finite one, leaves the edge
// without a bound, which BA.600/601/602/610 report rather than treating as
// "everything passes". Matches BrepConstants.NUMERICAL_TOLERANCE in the Python
// brep_validator, whose _get_edge_intersect_tolerance applies the same floor.
constexpr double _MinUsableEdgeTol = 1e-11;

// One edge's resolved intersection tolerance, and which Brep supplied it.
struct _EdgeTol {
    bool resolved = false;
    double tol = 0.0;
    size_t brepIdx = 0;
};

// Resolve a per-edge brep:intersectTol3d for every edge. Unlike
// _FirstAuthoredIntersectTol3d, which takes Brep 0's tolerance for the whole
// prim, this attributes each edge to its own Brep: _ComputeOffsets partitions
// the flat edgeuse array per Brep and edgeuse:edgeIndex names the edge each
// edgeuse uses, so an edge belongs to the Brep of the first edgeuse that
// references it. An edge that no edgeuse references stays unattributed, except
// when a single tolerance is authored, where there is only one value it could
// take. There is no reader-side fallback here: these rules report an
// unresolvable tolerance instead of substituting one.
std::vector<_EdgeTol>
_ResolveEdgeTolerances(const UsdSolidBrepArray &brep, size_t numEdges)
{
    std::vector<_EdgeTol> out(numEdges);
    const VtArray<double> tols
        = _Read<double>(brep.GetBrepIntersectTol3dAttr());
    if (tols.empty() || numEdges == 0) {
        return out;
    }

    constexpr size_t unattributed = static_cast<size_t>(-1);
    std::vector<size_t> edgeBrep(numEdges, unattributed);
    const _BrepOffsets offsets = _ComputeOffsets(brep);
    if (offsets.ok) {
        const VtArray<unsigned int> edgeuseEdgeIndex
            = _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr());
        for (size_t b = 0; b + 1 < offsets.edgeuse.size(); ++b) {
            const size_t hi
                = std::min(offsets.edgeuse[b + 1], edgeuseEdgeIndex.size());
            for (size_t eu = offsets.edgeuse[b]; eu < hi; ++eu) {
                const size_t e = edgeuseEdgeIndex[eu];
                if (e < numEdges && edgeBrep[e] == unattributed) {
                    edgeBrep[e] = b;
                }
            }
        }
    }

    for (size_t e = 0; e < numEdges; ++e) {
        size_t b = edgeBrep[e];
        if (b == unattributed && tols.size() == 1) {
            b = 0;
        }
        if (b == unattributed || b >= tols.size()) {
            continue;
        }
        if (!std::isfinite(tols[b]) || tols[b] < _MinUsableEdgeTol) {
            continue;
        }
        out[e] = { true, tols[b], b };
    }
    return out;
}

void
_ReportUnresolvedEdgeTol(const UsdPrim &prim, const char *ba,
                         const char *checkLabel, size_t edgeIdx,
                         UsdValidationErrorVector *errors)
{
    _Err(errors,
         UsdSolidValidationErrorNameTokens->unresolvedEdgeIntersectTol3d, prim,
         TfStringPrintf(
             "[%s] BrepArray <%s>: %s for edge #%zu could not be validated "
             "because no positive brep:intersectTol3d value could be resolved "
             "for the edge. The tolerance is missing, invalid, or the edge "
             "could not be associated with a Brep.",
             ba, prim.GetPath().GetText(), checkLabel, edgeIdx));
}

// BA.600/601/602: an analytic edge's 3D curve, evaluated at the two authored
// edge:range parameters, must reach the positions of the two vertices named by
// edge:vertexIndices, in that order, within the edge's brep:intersectTol3d.
// The requirement set numbers this per curve family: a line edge attributes to
// BA.600, a circle edge to BA.601, an ellipse edge to BA.602. It covers edges
// only; there is no wireEdge equivalent.
//
// Instances of one curve family are packed in edge order, so the family cursors
// advance on every edge of that family even when the edge itself is skipped.
//
// BA.240 (BrepArrayEdgeCurveVertices) tests the same relation, but across every
// edge and wireEdge including NURBS, against Brep 0's tolerance with a
// reader-side fallback, and exempts an edge whose two vertices coincide. These
// rules are narrower and stricter: analytic edges only, each against the
// tolerance of the Brep that owns it, no fallback and no exemption. A file that
// fails one usually fails the other.
void
_CheckAnalyticEdgeEndpointVertices(const UsdPrim &prim,
                                   const UsdSolidBrepArray &brep,
                                   const std::vector<_EdgeTol> &edgeTol,
                                   UsdValidationErrorVector *errors)
{
    const VtArray<TfToken> curveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const VtArray<double> range = _Read<double>(brep.GetEdgeRangeAttr());
    const VtArray<GfVec2i> vtxIdx
        = _Read<GfVec2i>(brep.GetEdgeVertexIndicesAttr());
    const VtArray<GfVec3d> vpos
        = _ReadName<GfVec3d>(prim, "brep:vertexPoint:point:position");

    const size_t numEdges = curveType.size();
    if (numEdges == 0 || range.empty() || vtxIdx.empty() || vpos.empty()
        || edgeTol.size() < numEdges) {
        return;
    }
    // A short edge:range cannot supply both parameters for every edge; the size
    // itself is BrepArrayStructure's report (BA.115).
    if (range.size() < 2 * numEdges) {
        return;
    }

    const std::string lineBase = "brep:edge3dLine:curve3d:line:";
    const VtArray<GfVec3d> lineOrigin
        = _ReadName<GfVec3d>(prim, lineBase + "origin");
    const VtArray<GfVec3d> lineDir
        = _ReadName<GfVec3d>(prim, lineBase + "direction");

    const std::string circleBase = "brep:edge3dCircle:curve3d:circle:";
    const VtArray<GfVec3d> circleCenter
        = _ReadName<GfVec3d>(prim, circleBase + "center");
    const VtArray<GfVec3d> circleAxis
        = _ReadName<GfVec3d>(prim, circleBase + "axis");
    const VtArray<GfVec3d> circleRef
        = _ReadName<GfVec3d>(prim, circleBase + "refDirection");
    const VtArray<double> circleRadius
        = _ReadName<double>(prim, circleBase + "radius");

    const std::string ellipseBase = "brep:edge3dEllipse:curve3d:ellipse:";
    const VtArray<GfVec3d> ellipseCenter
        = _ReadName<GfVec3d>(prim, ellipseBase + "center");
    const VtArray<GfVec3d> ellipseAxis
        = _ReadName<GfVec3d>(prim, ellipseBase + "axis");
    const VtArray<GfVec3d> ellipseRef
        = _ReadName<GfVec3d>(prim, ellipseBase + "refDirection");
    const VtArray<double> ellipseX
        = _ReadName<double>(prim, ellipseBase + "xRadius");
    const VtArray<double> ellipseY
        = _ReadName<double>(prim, ellipseBase + "yRadius");

    static const TfToken lineTok("BrepCurve3dLineAPI");
    static const TfToken circleTok("BrepCurve3dCircleAPI");
    static const TfToken ellipseTok("BrepCurve3dEllipseAPI");

    // center + cos(t)*xr*ref + sin(t)*yr*(axis x ref).
    const auto conicAt = [](const GfVec3d &center, const GfVec3d &axis,
                            const GfVec3d &ref, double xr, double yr,
                            double t) {
        return center + std::cos(t) * xr * ref
            + std::sin(t) * yr * GfCross(axis, ref);
    };

    // Compare a curve's two endpoints against the edge's two vertices; at most
    // one error per edge, naming the first endpoint that is out of tolerance.
    const auto compare
        = [&](const char *ba, const char *shape, size_t e, double t0,
              double t1, const GfVec3d &start, const GfVec3d &end, int v0,
              int v1, double tol, size_t brepIdx) {
              const double ts[2] = { t0, t1 };
              const GfVec3d pts[2] = { start, end };
              const int vs[2] = { v0, v1 };
              const char *names[2] = { "start", "end" };
              for (int k = 0; k < 2; ++k) {
                  const GfVec3d &vp = vpos[vs[k]];
                  const double dist = (pts[k] - vp).GetLength();
                  if (dist <= tol) {
                      continue;
                  }
                  _Err(errors,
                       UsdSolidValidationErrorNameTokens
                           ->analyticCurveEndpointVertexMismatch,
                       prim,
                       TfStringPrintf(
                           "[%s] BrepArray <%s>: %s edge #%zu %s point "
                           "evaluated at t=%.6f is (%.6f, %.6f, %.6f), but "
                           "vertex #%d is at (%.6f, %.6f, %.6f); distance "
                           "%.6f exceeds brep:intersectTol3d[%zu] = %g.",
                           ba, prim.GetPath().GetText(), shape, e, names[k],
                           ts[k], pts[k][0], pts[k][1], pts[k][2], vs[k],
                           vp[0], vp[1], vp[2], dist, brepIdx, tol));
                  break;
              }
          };

    size_t lineInst = 0;
    size_t circleInst = 0;
    size_t ellipseInst = 0;

    for (size_t e = 0; e < numEdges; ++e) {
        const TfToken &ct = curveType[e];
        const bool isLine = (ct == lineTok);
        const bool isCircle = (ct == circleTok);
        const bool isEllipse = (ct == ellipseTok);
        if (!isLine && !isCircle && !isEllipse) {
            continue;
        }

        const double t0 = range[2 * e];
        const double t1 = range[2 * e + 1];
        const bool haveVerts = e < vtxIdx.size()
            && vtxIdx[e][0] >= 0 && vtxIdx[e][1] >= 0
            && vtxIdx[e][0] < static_cast<int>(vpos.size())
            && vtxIdx[e][1] < static_cast<int>(vpos.size());
        const int v0 = haveVerts ? vtxIdx[e][0] : 0;
        const int v1 = haveVerts ? vtxIdx[e][1] : 0;
        const _EdgeTol &et = edgeTol[e];

        if (isLine) {
            const size_t i = lineInst++;
            if (i >= lineOrigin.size() || i >= lineDir.size()) {
                continue;
            }
            // Out-of-range vertex indices are BrepArrayReferences' report
            // (BA.200); an edge that has none is out of this rule's reach, so
            // it is not held against the tolerance either.
            if (!haveVerts) {
                continue;
            }
            if (!et.resolved) {
                _ReportUnresolvedEdgeTol(prim, "BA.600",
                                         "line endpoint-to-vertex consistency",
                                         e, errors);
                continue;
            }
            compare("BA.600", "line", e, t0, t1,
                    lineOrigin[i] + t0 * lineDir[i],
                    lineOrigin[i] + t1 * lineDir[i], v0, v1, et.tol,
                    et.brepIdx);
        } else if (isCircle) {
            const size_t i = circleInst++;
            if (i >= circleCenter.size() || i >= circleAxis.size()
                || i >= circleRef.size() || i >= circleRadius.size()) {
                continue;
            }
            if (!haveVerts) {
                continue;
            }
            if (!et.resolved) {
                _ReportUnresolvedEdgeTol(
                    prim, "BA.601", "circle endpoint-to-vertex consistency", e,
                    errors);
                continue;
            }
            const double r = circleRadius[i];
            compare("BA.601", "circle", e, t0, t1,
                    conicAt(circleCenter[i], circleAxis[i], circleRef[i], r, r,
                            t0),
                    conicAt(circleCenter[i], circleAxis[i], circleRef[i], r, r,
                            t1),
                    v0, v1, et.tol, et.brepIdx);
        } else {
            const size_t i = ellipseInst++;
            if (i >= ellipseCenter.size() || i >= ellipseAxis.size()
                || i >= ellipseRef.size() || i >= ellipseX.size()
                || i >= ellipseY.size()) {
                continue;
            }
            if (!haveVerts) {
                continue;
            }
            if (!et.resolved) {
                _ReportUnresolvedEdgeTol(
                    prim, "BA.602", "ellipse endpoint-to-vertex consistency",
                    e, errors);
                continue;
            }
            compare("BA.602", "ellipse", e, t0, t1,
                    conicAt(ellipseCenter[i], ellipseAxis[i], ellipseRef[i],
                            ellipseX[i], ellipseY[i], t0),
                    conicAt(ellipseCenter[i], ellipseAxis[i], ellipseRef[i],
                            ellipseX[i], ellipseY[i], t1),
                    v0, v1, et.tol, et.brepIdx);
        }
    }
}

// BA.610: both vertices of a circle edge must lie at the circle's authored
// radius from its center, within the edge's brep:intersectTol3d. This needs
// only the center and radius, so it still applies where BA.601 cannot reach --
// a circle whose axis or refDirection is missing, or an edge whose edge:range
// is too short to evaluate -- and it judges each vertex on its own rather than
// requiring both to be in range.
void
_CheckCircleVertexRadius(const UsdPrim &prim, const UsdSolidBrepArray &brep,
                         const std::vector<_EdgeTol> &edgeTol,
                         UsdValidationErrorVector *errors)
{
    const VtArray<TfToken> curveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const VtArray<GfVec2i> vtxIdx
        = _Read<GfVec2i>(brep.GetEdgeVertexIndicesAttr());
    const VtArray<GfVec3d> vpos
        = _ReadName<GfVec3d>(prim, "brep:vertexPoint:point:position");
    const std::string circleBase = "brep:edge3dCircle:curve3d:circle:";
    const VtArray<GfVec3d> center
        = _ReadName<GfVec3d>(prim, circleBase + "center");
    const VtArray<double> radius
        = _ReadName<double>(prim, circleBase + "radius");

    if (curveType.empty() || vtxIdx.empty() || vpos.empty() || center.empty()
        || radius.empty() || edgeTol.size() < curveType.size()) {
        return;
    }

    static const TfToken circleTok("BrepCurve3dCircleAPI");
    size_t circleInst = 0;
    for (size_t e = 0; e < curveType.size(); ++e) {
        if (curveType[e] != circleTok) {
            continue;
        }
        const size_t i = circleInst++;
        if (i >= center.size() || i >= radius.size() || e >= vtxIdx.size()) {
            continue;
        }
        const _EdgeTol &et = edgeTol[e];
        if (!et.resolved) {
            _ReportUnresolvedEdgeTol(prim, "BA.610",
                                     "circle vertex-radius consistency", e,
                                     errors);
            continue;
        }
        const int vs[2] = { vtxIdx[e][0], vtxIdx[e][1] };
        const char *names[2] = { "start", "end" };
        for (int k = 0; k < 2; ++k) {
            if (vs[k] < 0 || vs[k] >= static_cast<int>(vpos.size())) {
                continue;
            }
            const GfVec3d &v = vpos[vs[k]];
            const double dist = (v - center[i]).GetLength();
            const double diff = std::abs(dist - radius[i]);
            if (diff <= et.tol) {
                continue;
            }
            _Err(errors,
                 UsdSolidValidationErrorNameTokens->circleVertexRadiusMismatch,
                 prim,
                 TfStringPrintf(
                     "[BA.610] BrepArray <%s>: circle edge #%zu %s vertex #%d "
                     "is at distance %.6f from center (%.6f, %.6f, %.6f), but "
                     "radius is %.6f; difference %.6f exceeds "
                     "brep:intersectTol3d[%zu] = %g.",
                     prim.GetPath().GetText(), e, names[k], vs[k], dist,
                     center[i][0], center[i][1], center[i][2], radius[i], diff,
                     et.brepIdx, et.tol));
            break;
        }
    }
}

UsdValidationErrorVector
_BrepArrayAnalyticCurves(const UsdPrim &usdPrim,
                         const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    const VtArray<TfToken> edgeCurveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const VtArray<TfToken> wireCurveType
        = _Read<TfToken>(brep.GetWireEdgeCurveTypeAttr());

    const TfToken circle("BrepCurve3dCircleAPI");
    const TfToken line("BrepCurve3dLineAPI");
    const TfToken ellipse("BrepCurve3dEllipseAPI");

    UsdValidationErrorVector errors;

    _CheckCircleInstance(usdPrim, "edge3dCircle",
                         _CountToken(edgeCurveType, circle), &errors);
    _CheckCircleInstance(usdPrim, "wireEdge3dCircle",
                         _CountToken(wireCurveType, circle), &errors);
    _CheckLineInstance(usdPrim, "edge3dLine",
                       _CountToken(edgeCurveType, line), &errors);
    _CheckLineInstance(usdPrim, "wireEdge3dLine",
                       _CountToken(wireCurveType, line), &errors);
    _CheckEllipseInstance(usdPrim, "edge3dEllipse",
                          _CountToken(edgeCurveType, ellipse), &errors);
    _CheckEllipseInstance(usdPrim, "wireEdge3dEllipse",
                          _CountToken(wireCurveType, ellipse), &errors);

    // BA.600/601/602 and BA.610 relate the analytic curve parameters above to
    // the edge's vertices, so both need the edge's own tolerance.
    const std::vector<_EdgeTol> edgeTol
        = _ResolveEdgeTolerances(brep, edgeCurveType.size());
    _CheckAnalyticEdgeEndpointVertices(usdPrim, brep, edgeTol, &errors);
    _CheckCircleVertexRadius(usdPrim, brep, edgeTol, &errors);

    return errors;
}

// -------------------------------------------------------------------------- //
// BrepArrayNurbs                                                             //
// -------------------------------------------------------------------------- //
UsdValidationErrorVector
_BrepArrayNurbs(const UsdPrim &usdPrim,
                const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    const SdfValueTypeName uintA = SdfValueTypeNames->UIntArray;
    const SdfValueTypeName dblA = SdfValueTypeNames->DoubleArray;
    const SdfValueTypeName dbl2A = SdfValueTypeNames->Double2Array;
    const SdfValueTypeName p3A = SdfValueTypeNames->Point3dArray;

    UsdValidationErrorVector errors;

    // --- Surface NURBS (single-apply, no instance segment) --- //
    const VtArray<TfToken> faceSurfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const size_t nSurf
        = _CountToken(faceSurfaceType, TfToken("BrepSurfaceNurbAPI"));
    const VtArray<unsigned int> uVC
        = _ReadName<unsigned int>(usdPrim, "brep:surface:nurb:uVertexCount");
    const VtArray<unsigned int> vVC
        = _ReadName<unsigned int>(usdPrim, "brep:surface:nurb:vVertexCount");
    const VtArray<unsigned int> uO
        = _ReadName<unsigned int>(usdPrim, "brep:surface:nurb:uOrder");
    const VtArray<unsigned int> vO
        = _ReadName<unsigned int>(usdPrim, "brep:surface:nurb:vOrder");
    if (nSurf > 0 || !uO.empty()) {
        if (uVC.size() != nSurf) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.420] BrepArray <%s>: brep:surface:nurb:"
                                "uVertexCount size %zu but expected %zu.",
                                usdPrim.GetPath().GetText(), uVC.size(), nSurf));
        }
        if (vVC.size() != nSurf) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.420] BrepArray <%s>: brep:surface:nurb:"
                                "vVertexCount size %zu but expected %zu.",
                                usdPrim.GetPath().GetText(), vVC.size(), nSurf));
        }
        if (uO.size() != nSurf) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.420] BrepArray <%s>: brep:surface:nurb:uOrder "
                                "size %zu but expected %zu.",
                                usdPrim.GetPath().GetText(), uO.size(), nSurf));
        }
        if (vO.size() != nSurf) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.420] BrepArray <%s>: brep:surface:nurb:vOrder "
                                "size %zu but expected %zu.",
                                usdPrim.GetPath().GetText(), vO.size(), nSurf));
        }
        _CheckNurbOrderPositive(usdPrim, "BA.425", "surface U", uO, uVC, false,
                                &errors);
        _CheckNurbOrderPositive(usdPrim, "BA.425", "surface V", vO, vVC, false,
                                &errors);
        _CheckNurbOrderLEVtx(usdPrim, "BA.430", "surface U", uO, uVC, false,
                             &errors);
        _CheckNurbOrderLEVtx(usdPrim, "BA.430", "surface V", vO, vVC, false,
                             &errors);
        _CheckNurbOrderMin2(usdPrim, "surface U", uO, uVC, false, &errors);
        _CheckNurbOrderMin2(usdPrim, "surface V", vO, vVC, false, &errors);
        _CheckNurbVtxGEOrder(usdPrim, "surface U", uO, uVC, false, &errors);
        _CheckNurbVtxGEOrder(usdPrim, "surface V", vO, vVC, false, &errors);

        const VtArray<GfVec3d> cv
            = _ReadName<GfVec3d>(usdPrim, "brep:surface:nurb:controlVertices");
        const VtArray<double> w
            = _ReadName<double>(usdPrim, "brep:surface:nurb:weights");
        size_t expectedCv = 0;
        const size_t ms = std::min(uVC.size(), vVC.size());
        for (size_t i = 0; i < ms; ++i) {
            expectedCv += static_cast<size_t>(uVC[i]) * vVC[i];
        }
        if (cv.size() != expectedCv || w.size() != expectedCv) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens
                     ->nurbControlVertexWeightSizeMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.435] BrepArray <%s>: surface controlVertices "
                                "(%zu) / weights (%zu) size but expected %zu "
                                "(sum of uVertexCount*vVertexCount).",
                                usdPrim.GetPath().GetText(), cv.size(), w.size(),
                                expectedCv));
        }
        _CheckNurbWeights(usdPrim, "BA.440", "surface", w, &errors);
        _CheckNurbKnots1D(
            usdPrim, "BA.445", "BA.455", "surface U", uO, uVC,
            _ReadName<double>(usdPrim, "brep:surface:nurb:uKnots"), &errors);
        _CheckNurbKnots1D(
            usdPrim, "BA.450", "BA.460", "surface V", vO, vVC,
            _ReadName<double>(usdPrim, "brep:surface:nurb:vKnots"), &errors);
        if (nSurf > 0 && uO.empty()) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->nurbSchemaDataIncomplete,
                 usdPrim,
                 TfStringPrintf("[BA.470] BrepArray <%s>: faces declare "
                                "BrepSurfaceNurbAPI but no brep:surface:nurb data "
                                "is authored.",
                                usdPrim.GetPath().GetText()));
        }
        if (_HasAppliedSchema(usdPrim, TfToken("BrepSurfaceNurbAPI"))
            && nSurf == 0) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->nurbSchemaUsageInconsistent,
                 usdPrim,
                 TfStringPrintf("[BA.470] BrepArray <%s>: BrepSurfaceNurbAPI is in "
                                "apiSchemas but no face uses "
                                "face:surfaceType=BrepSurfaceNurbAPI.",
                                usdPrim.GetPath().GetText()));
        }
        _CheckNurbType(usdPrim, "BA.471", "brep:surface:nurb:uOrder", uintA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.471", "brep:surface:nurb:vOrder", uintA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.471", "brep:surface:nurb:uVertexCount", uintA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.471", "brep:surface:nurb:vVertexCount", uintA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.471", "brep:surface:nurb:controlVertices",
                       p3A, &errors);
        _CheckNurbType(usdPrim, "BA.471", "brep:surface:nurb:weights", dblA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.471", "brep:surface:nurb:uKnots", dblA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.471", "brep:surface:nurb:vKnots", dblA,
                       &errors);
    }

    // --- Edge 3D NURBS (multi-apply instance edge3dNurb) --- //
    const VtArray<TfToken> edgeCurveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const size_t nEdge
        = _CountToken(edgeCurveType, TfToken("BrepCurve3dNurbAPI"));
    const VtArray<unsigned int> eO
        = _ReadName<unsigned int>(usdPrim, "brep:edge3dNurb:curve3d:nurb:order");
    const VtArray<unsigned int> eVC = _ReadName<unsigned int>(
        usdPrim, "brep:edge3dNurb:curve3d:nurb:vertexCount");
    if (nEdge > 0 || !eO.empty()) {
        if (eO.size() != nEdge) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.330] BrepArray <%s>: edge3dNurb order size "
                                "%zu but expected %zu.",
                                usdPrim.GetPath().GetText(), eO.size(), nEdge));
        }
        if (eVC.size() != nEdge) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.330] BrepArray <%s>: edge3dNurb vertexCount "
                                "size %zu but expected %zu.",
                                usdPrim.GetPath().GetText(), eVC.size(), nEdge));
        }
        _CheckNurbOrderPositive(usdPrim, "BA.335", "edge3d", eO, eVC, false,
                                &errors);
        _CheckNurbOrderLEVtx(usdPrim, "BA.340", "edge3d", eO, eVC, false,
                             &errors);
        _CheckNurbOrderMin2(usdPrim, "edge3d", eO, eVC, false, &errors);
        _CheckNurbVtxGEOrder(usdPrim, "edge3d", eO, eVC, false, &errors);

        const VtArray<GfVec3d> eCv = _ReadName<GfVec3d>(
            usdPrim, "brep:edge3dNurb:curve3d:nurb:controlVertices");
        const VtArray<double> eW
            = _ReadName<double>(usdPrim, "brep:edge3dNurb:curve3d:nurb:weights");
        size_t expectedCv = 0;
        for (unsigned int c : eVC) {
            expectedCv += c;
        }
        if (eCv.size() != expectedCv || eW.size() != expectedCv) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens
                     ->nurbControlVertexWeightSizeMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.345] BrepArray <%s>: edge3dNurb "
                                "controlVertices (%zu) / weights (%zu) size but "
                                "expected %zu (sum of vertexCount).",
                                usdPrim.GetPath().GetText(), eCv.size(),
                                eW.size(), expectedCv));
        }
        _CheckNurbWeights(usdPrim, "BA.350", "edge3d", eW, &errors);
        _CheckNurbKnots1D(
            usdPrim, "BA.355", "BA.360", "edge3d", eO, eVC,
            _ReadName<double>(usdPrim, "brep:edge3dNurb:curve3d:nurb:knots"),
            &errors);
        if (nEdge > 0 && eO.empty()) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->nurbSchemaDataIncomplete,
                 usdPrim,
                 TfStringPrintf("[BA.370] BrepArray <%s>: edges declare "
                                "BrepCurve3dNurbAPI but no brep:edge3dNurb data is "
                                "authored.",
                                usdPrim.GetPath().GetText()));
        }
        if (_HasAppliedSchema(usdPrim,
                              TfToken("BrepCurve3dNurbAPI:edge3dNurb"))
            && nEdge == 0) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->nurbSchemaUsageInconsistent,
                 usdPrim,
                 TfStringPrintf("[BA.370] BrepArray <%s>: "
                                "BrepCurve3dNurbAPI:edge3dNurb is in apiSchemas "
                                "but no edge uses edge:curveType="
                                "BrepCurve3dNurbAPI.",
                                usdPrim.GetPath().GetText()));
        }
        _CheckNurbType(usdPrim, "BA.371", "brep:edge3dNurb:curve3d:nurb:order",
                       uintA, &errors);
        _CheckNurbType(usdPrim, "BA.371",
                       "brep:edge3dNurb:curve3d:nurb:vertexCount", uintA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.371",
                       "brep:edge3dNurb:curve3d:nurb:controlVertices", p3A,
                       &errors);
        _CheckNurbType(usdPrim, "BA.371", "brep:edge3dNurb:curve3d:nurb:weights",
                       dblA, &errors);
        _CheckNurbType(usdPrim, "BA.371", "brep:edge3dNurb:curve3d:nurb:knots",
                       dblA, &errors);
    }

    // --- WireEdge 3D NURBS (instance wireEdge3dNurb): schema usage + 590/591 - //
    const VtArray<TfToken> wireCurveType
        = _Read<TfToken>(brep.GetWireEdgeCurveTypeAttr());
    const size_t nWire
        = _CountToken(wireCurveType, TfToken("BrepCurve3dNurbAPI"));
    const VtArray<unsigned int> wO = _ReadName<unsigned int>(
        usdPrim, "brep:wireEdge3dNurb:curve3d:nurb:order");
    const VtArray<unsigned int> wVC = _ReadName<unsigned int>(
        usdPrim, "brep:wireEdge3dNurb:curve3d:nurb:vertexCount");
    if (nWire > 0 && wO.empty()) {
        _Err(&errors,
             UsdSolidValidationErrorNameTokens->nurbSchemaDataIncomplete, usdPrim,
             TfStringPrintf("[BA.290] BrepArray <%s>: wireEdges declare "
                            "BrepCurve3dNurbAPI but no brep:wireEdge3dNurb data is "
                            "authored.",
                            usdPrim.GetPath().GetText()));
    }
    if (_HasAppliedSchema(usdPrim, TfToken("BrepCurve3dNurbAPI:wireEdge3dNurb"))
        && nWire == 0) {
        _Err(&errors,
             UsdSolidValidationErrorNameTokens->nurbSchemaUsageInconsistent,
             usdPrim,
             TfStringPrintf("[BA.290] BrepArray <%s>: "
                            "BrepCurve3dNurbAPI:wireEdge3dNurb is in apiSchemas "
                            "but no wireEdge uses wireEdge:curveType="
                            "BrepCurve3dNurbAPI.",
                            usdPrim.GetPath().GetText()));
    }
    if (!wO.empty()) {
        _CheckNurbOrderMin2(usdPrim, "wireEdge3d", wO, wVC, false, &errors);
        _CheckNurbVtxGEOrder(usdPrim, "wireEdge3d", wO, wVC, false, &errors);
    }

    // --- Curve UV NURBS (single-apply, one trim curve per edgeuse) --- //
    const size_t euCount
        = _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr()).size();
    const VtArray<unsigned int> cO
        = _ReadName<unsigned int>(usdPrim, "brep:curveUv:nurb:order");
    const VtArray<unsigned int> cVC
        = _ReadName<unsigned int>(usdPrim, "brep:curveUv:nurb:vertexCount");
    // BA.415 (schema-usage) is independent of whether curveUv value data is
    // authored: "API applied but no data / no edgeuses" is itself the failure.
    {
        const bool appliedUv
            = _HasAppliedSchema(usdPrim, TfToken("BrepCurveUvNurbAPI"));
        if (appliedUv && euCount > 0 && cO.empty()) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->nurbSchemaDataIncomplete,
                 usdPrim,
                 TfStringPrintf("[BA.415] BrepArray <%s>: BrepCurveUvNurbAPI is in "
                                "apiSchemas but no brep:curveUv:nurb data is "
                                "authored.",
                                usdPrim.GetPath().GetText()));
        }
        if (appliedUv && euCount == 0) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->nurbSchemaUsageInconsistent,
                 usdPrim,
                 TfStringPrintf("[BA.415] BrepArray <%s>: BrepCurveUvNurbAPI is in "
                                "apiSchemas but no edgeuses exist.",
                                usdPrim.GetPath().GetText()));
        }
    }
    const bool runUv = _IsAuthored(usdPrim, "brep:curveUv:nurb:order")
        || _IsAuthored(usdPrim, "brep:curveUv:nurb:vertexCount");
    if (runUv) {
        if (cO.size() != euCount) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.375] BrepArray <%s>: brep:curveUv:nurb:order "
                                "size %zu but expected %zu (edgeuse count).",
                                usdPrim.GetPath().GetText(), cO.size(), euCount));
        }
        if (cVC.size() != euCount) {
            _Err(&errors, UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.375] BrepArray <%s>: "
                                "brep:curveUv:nurb:vertexCount size %zu but "
                                "expected %zu (edgeuse count).",
                                usdPrim.GetPath().GetText(), cVC.size(),
                                euCount));
        }
        _CheckNurbOrderPositive(usdPrim, "BA.380", "curveUv", cO, cVC, true,
                                &errors);
        _CheckNurbOrderLEVtx(usdPrim, "BA.385", "curveUv", cO, cVC, true,
                             &errors);
        _CheckNurbOrderMin2(usdPrim, "curveUv", cO, cVC, true, &errors);
        _CheckNurbVtxGEOrder(usdPrim, "curveUv", cO, cVC, true, &errors);

        const VtArray<GfVec2d> cCv
            = _ReadName<GfVec2d>(usdPrim, "brep:curveUv:nurb:controlVertices");
        const VtArray<double> cW
            = _ReadName<double>(usdPrim, "brep:curveUv:nurb:weights");
        size_t expectedCv = 0;
        for (unsigned int c : cVC) {
            expectedCv += c;
        }
        if (cCv.size() != expectedCv) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens
                     ->nurbControlVertexWeightSizeMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.390] BrepArray <%s>: curveUv controlVertices "
                                "size %zu but expected %zu (sum of vertexCount).",
                                usdPrim.GetPath().GetText(), cCv.size(),
                                expectedCv));
        }
        if (!cW.empty() && cW.size() != expectedCv) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens
                     ->nurbControlVertexWeightSizeMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.405] BrepArray <%s>: curveUv weights size %zu "
                                "but expected %zu (sum of vertexCount).",
                                usdPrim.GetPath().GetText(), cW.size(),
                                expectedCv));
        }
        _CheckNurbWeights(usdPrim, "BA.410", "curveUv", cW, &errors);
        _CheckNurbKnots1D(usdPrim, "BA.395", "BA.400", "curveUv", cO, cVC,
                          _ReadName<double>(usdPrim, "brep:curveUv:nurb:knots"),
                          &errors);
        _CheckNurbType(usdPrim, "BA.416", "brep:curveUv:nurb:order", uintA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.416", "brep:curveUv:nurb:vertexCount", uintA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.416", "brep:curveUv:nurb:controlVertices",
                       dbl2A, &errors);
        _CheckNurbType(usdPrim, "BA.416", "brep:curveUv:nurb:knots", dblA,
                       &errors);
        _CheckNurbType(usdPrim, "BA.416", "brep:curveUv:nurb:weights", dblA,
                       &errors);
    }

    return errors;
}

// ========================================================================== //
// BrepArrayUvTrim                                                            //
// ========================================================================== //
// The trim / winding family: BA.750, BA.761, BA.762, BA.763, BA.764, BA.765.
// These are the rules that read the UV (parameter-space) trim curves in the
// brep:curveUv:nurb stratum together with the periodic face domains in
// face:range, so they are grouped into one validator that resolves the
// curveUv arrays once.

// Period tolerance shared by the periodic-domain rules (BA.761/762/765) and
// closure tolerance for BA.763; both are 1e-6 in the Python validator.
constexpr double _UvTrimPeriodTol = 1e-6;
constexpr double _UvClosureTol = 1e-6;
// A UV trim curve whose control polygon spans no more than this has collapsed.
constexpr double _UvZeroLengthTol = 1e-12;
// BA.750 allows a trim curve to stray half a domain span (or half a unit,
// whichever is larger) outside face:range before it is reported.
constexpr double _UvDomainMargin = 0.5;

// "BrepSurfaceCylinderAPI" -> "Cylinder": the label the Python messages use.
std::string
_SurfaceLabel(const TfToken &surfaceType)
{
    std::string s = surfaceType.GetString();
    static const std::string prefix = "BrepSurface";
    static const std::string suffix = "API";
    if (s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0) {
        s.erase(0, prefix.size());
    }
    if (s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0) {
        s.erase(s.size() - suffix.size());
    }
    return s;
}

struct _Uv2 {
    double u = 0.0;
    double v = 0.0;
};

// Evaluate a rational 2D B-spline at parameter t with de Boor's algorithm.
// Ported from BrepValidator._de_boor_evaluate_2d: the control vertices are
// lifted to homogeneous (w*u, w*v, w), the knot span containing t is found by
// linear scan, p rounds of corner cutting collapse the local hull to the
// point, and the result is projected back. Returns false wherever the Python
// returns None (malformed sizing, out-of-range span index, zero weight), which
// the callers treat as "cannot evaluate" rather than "invalid".
bool
_DeBoorEvaluate2d(unsigned int order, const std::vector<double> &knots,
                  const std::vector<GfVec2d> &cvs,
                  const std::vector<double> &weights, double t, _Uv2 *out)
{
    const size_t n = cvs.size();
    if (order < 1 || n < order || knots.size() < n + order
        || weights.size() < n) {
        return false;
    }
    const size_t p = order - 1;
    t = std::max(knots[p], std::min(t, knots[n]));

    // Knot span index. Python's for/else means the clamped-end fallback below
    // applies only when no span strictly contains t, which is the t == knots[n]
    // case at the curve's far end.
    size_t k = p;
    bool found = false;
    for (size_t i = p; i < n; ++i) {
        if (knots[i] <= t && t < knots[i + 1]) {
            k = i;
            found = true;
            break;
        }
    }
    if (!found) {
        const double b = knots[n];
        const double bound
            = std::max(1e-12 * std::max(std::abs(t), std::abs(b)), 1e-14);
        if (std::abs(t - b) <= bound) {
            k = n - 1;
        }
    }

    // Homogeneous de Boor points (w*u, w*v, w).
    struct _H3 {
        double c[3];
    };
    std::vector<_H3> d(p + 1);
    for (size_t j = 0; j <= p; ++j) {
        const ptrdiff_t idx
            = static_cast<ptrdiff_t>(k) - static_cast<ptrdiff_t>(p)
            + static_cast<ptrdiff_t>(j);
        if (idx < 0 || static_cast<size_t>(idx) >= n) {
            return false;
        }
        const double w = weights[idx];
        d[j].c[0] = cvs[idx][0] * w;
        d[j].c[1] = cvs[idx][1] * w;
        d[j].c[2] = w;
    }

    for (size_t r = 1; r <= p; ++r) {
        for (size_t j = p; j >= r; --j) {
            const ptrdiff_t left
                = static_cast<ptrdiff_t>(k) - static_cast<ptrdiff_t>(p)
                + static_cast<ptrdiff_t>(j);
            const ptrdiff_t right = left + static_cast<ptrdiff_t>(p)
                - static_cast<ptrdiff_t>(r) + 1;
            if (left < 0 || right < 0
                || static_cast<size_t>(left) >= knots.size()
                || static_cast<size_t>(right) >= knots.size()) {
                return false;
            }
            const double denom = knots[right] - knots[left];
            const double alpha = std::abs(denom) < 1e-30
                ? 0.0
                : (t - knots[left]) / denom;
            for (int c = 0; c < 3; ++c) {
                d[j].c[c] = (1.0 - alpha) * d[j - 1].c[c] + alpha * d[j].c[c];
            }
        }
    }

    const double w = d[p].c[2];
    if (std::abs(w) < 1e-30) {
        return false;
    }
    out->u = d[p].c[0] / w;
    out->v = d[p].c[1] / w;
    return true;
}

// --- BA.750: UV trim curve domain containment ---------------------------- //
// Every UV control vertex of a face's trim curves should sit within the face's
// own face:range, widened by _UvDomainMargin. Like the Python rule this stops
// at the first offending control vertex: the failure mode it catches (a whole
// pcurve stratum indexed against the wrong face) produces hundreds of hits from
// one cause, and one is enough to name it.
void
_CheckUvTrimDomainContainment(const UsdPrim &usdPrim,
                              const UsdSolidBrepArray &brep,
                              UsdValidationErrorVector *errors)
{
    const VtArray<unsigned int> uvVc
        = _ReadName<unsigned int>(usdPrim, "brep:curveUv:nurb:vertexCount");
    const VtArray<GfVec2d> uvCvs
        = _ReadName<GfVec2d>(usdPrim, "brep:curveUv:nurb:controlVertices");
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());
    const VtArray<unsigned int> loopCounts
        = _Read<unsigned int>(brep.GetFaceLoopCountAttr());
    const VtArray<unsigned int> euCounts
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());

    if (uvVc.empty() || uvCvs.empty() || faceRange.empty()
        || loopCounts.empty() || euCounts.empty()) {
        return;
    }

    const size_t numFaces = loopCounts.size();
    size_t loopOffset = 0;
    size_t euOffset = 0;
    size_t cvOffset = 0;

    for (size_t faceIdx = 0; faceIdx < numFaces; ++faceIdx) {
        if (2 * faceIdx + 1 >= faceRange.size()) {
            break;
        }
        const GfVec2d &uvMin = faceRange[2 * faceIdx];
        const GfVec2d &uvMax = faceRange[2 * faceIdx + 1];
        const double uMin = uvMin[0], vMin = uvMin[1];
        const double uMax = uvMax[0], vMax = uvMax[1];
        const double uPad
            = _UvDomainMargin * std::max(std::abs(uMax - uMin), 1.0);
        const double vPad
            = _UvDomainMargin * std::max(std::abs(vMax - vMin), 1.0);
        const double uLo = uMin - uPad, uHi = uMax + uPad;
        const double vLo = vMin - vPad, vHi = vMax + vPad;

        const size_t nLoops = loopCounts[faceIdx];
        const size_t faceEuStart = euOffset;
        for (size_t lp = 0; lp < nLoops; ++lp) {
            const size_t lpIdx = loopOffset + lp;
            if (lpIdx < euCounts.size()) {
                euOffset += euCounts[lpIdx];
            }
        }
        const size_t faceEuEnd = euOffset;
        loopOffset += nLoops;

        const size_t euLimit = std::min(faceEuEnd, uvVc.size());
        for (size_t euIdx = faceEuStart; euIdx < euLimit; ++euIdx) {
            const size_t nCv = uvVc[euIdx];
            for (size_t j = 0; j < nCv; ++j) {
                const size_t ci = cvOffset + j;
                if (ci >= uvCvs.size()) {
                    break;
                }
                const double uVal = uvCvs[ci][0];
                const double vVal = uvCvs[ci][1];
                if (uVal < uLo || uVal > uHi || vVal < vLo || vVal > vHi) {
                    _Err(errors,
                         UsdSolidValidationErrorNameTokens
                             ->uvTrimCurveOutsideFaceDomain,
                         usdPrim,
                         TfStringPrintf(
                             "[BA.750] BrepArray <%s>: face #%zu edgeuse #%zu "
                             "UV control vertex [%zu] = (%.6f, %.6f) is far "
                             "outside face UV domain [%.4f..%.4f] x "
                             "[%.4f..%.4f].",
                             usdPrim.GetPath().GetText(), faceIdx, euIdx, ci,
                             uVal, vVal, uMin, uMax, vMin, vMax));
                    return;
                }
            }
            cvOffset += nCv;
        }
    }
}

// --- BA.761: full-period face seam edgeuse heuristic ---------------------- //
// A cylinder / cone / sphere face whose U domain covers a full 2*pi (or a torus
// face full in U or V) closes on itself, so its loop should walk the seam edge
// twice: one 3D edge, two edgeuses, hence a repeated edgeuse:edgeIndex within
// the face. A face with no repeat has authored the seam as two separate edges
// (or has no seam at all). Reported as a Warning: the repeat is a topological
// signal, not a proof that the repeated edge is geometrically the seam.
void
_CheckFullPeriodFaceSeamEdgeuse(const UsdPrim &usdPrim,
                                const UsdSolidBrepArray &brep,
                                UsdValidationErrorVector *errors)
{
    const VtArray<TfToken> faceSurfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());
    const VtArray<unsigned int> faceLoopCount
        = _Read<unsigned int>(brep.GetFaceLoopCountAttr());
    const VtArray<unsigned int> loopEdgeuseCount
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());
    // edgeuse:edgeIndex is deliberately not required to be non-empty. A
    // full-period face whose loops carry no edgeuses at all is exactly the "no
    // seam" case this rule exists to flag; gating on a populated stream would
    // silence it. Every read below is bounds-checked against the stream size.
    const VtArray<unsigned int> edgeuseEdgeIndex
        = _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr());

    if (faceSurfaceType.empty() || faceRange.empty() || faceLoopCount.empty()) {
        return;
    }

    const size_t numFaces = std::min(
        { faceSurfaceType.size(), faceLoopCount.size(), faceRange.size() / 2 });
    if (numFaces == 0) {
        return;
    }

    std::vector<size_t> loopEdgeuseStart(loopEdgeuseCount.size(), 0);
    size_t running = 0;
    for (size_t i = 0; i < loopEdgeuseCount.size(); ++i) {
        loopEdgeuseStart[i] = running;
        running += loopEdgeuseCount[i];
    }

    static const TfToken sphereTok("BrepSurfaceSphereAPI");
    static const TfToken cylinderTok("BrepSurfaceCylinderAPI");
    static const TfToken coneTok("BrepSurfaceConeAPI");
    static const TfToken torusTok("BrepSurfaceTorusAPI");

    const _BrepOffsets offsets = _ComputeOffsets(brep);

    size_t loopOffset = 0;
    for (size_t faceIdx = 0; faceIdx < numFaces; ++faceIdx) {
        const TfToken &stype = faceSurfaceType[faceIdx];
        const size_t loopCount = faceLoopCount[faceIdx];
        const GfVec2d &uvMin = faceRange[2 * faceIdx];
        const GfVec2d &uvMax = faceRange[2 * faceIdx + 1];
        const double uSpan = uvMax[0] - uvMin[0];
        const double vSpan = uvMax[1] - uvMin[1];

        std::vector<std::string> fullPeriodAxes;
        if (stype == sphereTok || stype == cylinderTok || stype == coneTok) {
            if (std::abs(uSpan - _TwoPi) <= _UvTrimPeriodTol) {
                fullPeriodAxes.push_back("U");
            }
        } else if (stype == torusTok) {
            if (std::abs(uSpan - _TwoPi) <= _UvTrimPeriodTol) {
                fullPeriodAxes.push_back("U");
            }
            if (std::abs(vSpan - _TwoPi) <= _UvTrimPeriodTol) {
                fullPeriodAxes.push_back("V");
            }
        }

        if (fullPeriodAxes.empty()) {
            loopOffset += loopCount;
            continue;
        }
        if (loopCount == 0 || loopOffset + loopCount > loopEdgeuseCount.size()) {
            loopOffset += loopCount;
            continue;
        }

        std::vector<unsigned int> faceEdgeIndices;
        bool topologyComplete = true;
        for (size_t lp = loopOffset; lp < loopOffset + loopCount; ++lp) {
            const size_t start = loopEdgeuseStart[lp];
            const size_t end = start + loopEdgeuseCount[lp];
            if (end > edgeuseEdgeIndex.size()) {
                topologyComplete = false;
                break;
            }
            for (size_t eu = start; eu < end; ++eu) {
                faceEdgeIndices.push_back(edgeuseEdgeIndex[eu]);
            }
        }
        loopOffset += loopCount;

        if (!topologyComplete) {
            continue;
        }

        std::unordered_map<unsigned int, size_t> edgeCounts;
        for (const unsigned int e : faceEdgeIndices) {
            ++edgeCounts[e];
        }
        size_t seamSignals = 0;
        for (const auto &kv : edgeCounts) {
            if (kv.second > 1) {
                ++seamSignals;
            }
        }
        if (seamSignals >= fullPeriodAxes.size()) {
            continue;
        }

        size_t brepIdx = 0;
        size_t localFaceIdx = faceIdx;
        for (size_t bi = 0; bi + 1 < offsets.face.size(); ++bi) {
            if (offsets.face[bi] <= faceIdx && faceIdx < offsets.face[bi + 1]) {
                brepIdx = bi;
                localFaceIdx = faceIdx - offsets.face[bi];
                break;
            }
        }

        std::string axes = fullPeriodAxes[0];
        for (size_t a = 1; a < fullPeriodAxes.size(); ++a) {
            axes += "/" + fullPeriodAxes[a];
        }

        _Err(errors,
             UsdSolidValidationErrorNameTokens->fullPeriodFaceNoSeamEdgeuse,
             usdPrim,
             TfStringPrintf(
                 "[BA.761] BrepArray <%s>: %s face #%zu in brep #%zu has a "
                 "full-period %s domain but no repeated edgeuse:edgeIndex "
                 "within the face. Full-period periodic faces are expected to "
                 "expose seam-like topology as multiple edgeuses on the same "
                 "3D edge; this is a schema-level heuristic and does not prove "
                 "a geometric seam exists.",
                 usdPrim.GetPath().GetText(), _SurfaceLabel(stype).c_str(),
                 localFaceIdx, brepIdx, axes.c_str()),
             UsdValidationErrorType::Warn);
    }
}

// --- BA.762 / BA.765: analytic periodic domain placement ------------------ //
// Both rules read the angular axes of an analytic periodic face:range.
// BA.762 covers the full-period case: a 2*pi span must be authored as
// [0, 2*pi], not an equivalent shifted interval such as [-pi, pi].
// BA.765 covers everything else: a partial-period span must lie inside
// [0, 2*pi]. The U axis of every periodic surface, and the V axis of a torus,
// are the angular ones; a cylinder or cone V is a length and a sphere V is a
// latitude, so neither is checked here.
void
_CheckAnalyticPeriodicDomains(const UsdPrim &usdPrim,
                              const UsdSolidBrepArray &brep,
                              UsdValidationErrorVector *errors)
{
    const VtArray<TfToken> faceSurfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());
    if (faceSurfaceType.empty() || faceRange.empty()) {
        return;
    }
    const size_t numFaces = faceSurfaceType.size();
    if (faceRange.size() < numFaces * 2) {
        return;
    }

    static const TfToken sphereTok("BrepSurfaceSphereAPI");
    static const TfToken cylinderTok("BrepSurfaceCylinderAPI");
    static const TfToken coneTok("BrepSurfaceConeAPI");
    static const TfToken torusTok("BrepSurfaceTorusAPI");

    // (axis label, component index, "wrapping" axis). The U axis of every
    // periodic surface wraps, so a U-max past 2*pi is the wrapped continuation
    // of the same sweep and is not reported by BA.765; a torus V-max past 2*pi
    // is.
    struct _Axis {
        const char *name;
        int index;
        bool uAxis;
    };

    static const _Axis uAxisOnly[] = { { "U", 0, true } };
    static const _Axis uAndVAxes[] = { { "U", 0, true }, { "V", 1, false } };

    for (size_t faceIdx = 0; faceIdx < numFaces; ++faceIdx) {
        const TfToken &stype = faceSurfaceType[faceIdx];
        const _Axis *axes = nullptr;
        size_t numAxes = 0;
        if (stype == cylinderTok || stype == coneTok || stype == sphereTok) {
            axes = uAxisOnly;
            numAxes = 1;
        } else if (stype == torusTok) {
            axes = uAndVAxes;
            numAxes = 2;
        } else {
            continue;
        }

        const GfVec2d &uvMin = faceRange[2 * faceIdx];
        const GfVec2d &uvMax = faceRange[2 * faceIdx + 1];
        const std::string label = _SurfaceLabel(stype);

        for (size_t a = 0; a < numAxes; ++a) {
            const _Axis &axis = axes[a];
            const double paramMin = uvMin[axis.index];
            const double paramMax = uvMax[axis.index];
            const double span = paramMax - paramMin;

            if (std::abs(span - _TwoPi) <= _UvTrimPeriodTol) {
                // BA.762: full period, must be the primary [0, 2*pi] interval.
                if (std::abs(paramMin) <= _UvTrimPeriodTol
                    && std::abs(paramMax - _TwoPi) <= _UvTrimPeriodTol) {
                    continue;
                }
                _Err(errors,
                     UsdSolidValidationErrorNameTokens
                         ->fullPeriodFaceDomainNotAligned,
                     usdPrim,
                     TfStringPrintf(
                         "[BA.762] BrepArray <%s>: %s face #%zu has a "
                         "full-period %s range [%.6f, %.6f] rad. Full-period "
                         "angular domains must be aligned to [0, 2*pi] = "
                         "[0.000000, %.6f].",
                         usdPrim.GetPath().GetText(), label.c_str(), faceIdx,
                         axis.name, paramMin, paramMax, _TwoPi));
                continue;
            }

            // BA.765: partial period, must stay inside [0, 2*pi].
            const bool minOutOfRange = paramMin < -_UvTrimPeriodTol;
            const bool maxOutOfRange = !axis.uAxis
                && paramMax > _TwoPi + _UvTrimPeriodTol;
            if (!minOutOfRange && !maxOutOfRange) {
                continue;
            }
            _Err(errors,
                 UsdSolidValidationErrorNameTokens
                     ->analyticPeriodicDomainOutOfBounds,
                 usdPrim,
                 TfStringPrintf(
                     "[BA.765] BrepArray <%s>: %s face #%zu has partial-period "
                     "%s range [%.6f, %.6f] rad outside the primary angular "
                     "domain [0, 2*pi] = [0.000000, %.6f].",
                     usdPrim.GetPath().GetText(), label.c_str(), faceIdx,
                     axis.name, paramMin, paramMax, _TwoPi));
        }
    }
}

// --- BA.763: UV loop closure --------------------------------------------- //
// Each loop's pcurves must run head to tail in parameter space: the UV point
// one pcurve ends at is the UV point the next one starts at, and the last wraps
// back to the first. The endpoints come from evaluating each pcurve with de
// Boor at its own parametric ends (knots[order-1] and knots[vertexCount]), not
// from its first and last control vertex, so a periodic or non-clamped pcurve
// is measured at the point the curve actually reaches.
//
// Known behaviour on conformant assets: a loop bounded by a degenerate
// parameter line reports a gap here. Where a sphere face reaches a pole, or a
// NURBS patch has a control row collapsed to a point, the whole V = const line
// is one 3D point, no edge exists along it, and so no pcurve is authored for
// it. The two pcurves either side jump in U with V pinned at the degenerate
// parameter. The gap is real in parameter space and the rule has no local
// signal that separates it from an open loop: every adjacent pcurve pair in a
// loop shares a vertex, so a shared-vertex test would silence the rule
// outright.
void
_CheckUvLoopClosure(const UsdPrim &usdPrim, const UsdSolidBrepArray &brep,
                    UsdValidationErrorVector *errors)
{
    const VtArray<unsigned int> orderVals
        = _ReadName<unsigned int>(usdPrim, "brep:curveUv:nurb:order");
    const VtArray<unsigned int> vcVals
        = _ReadName<unsigned int>(usdPrim, "brep:curveUv:nurb:vertexCount");
    const VtArray<GfVec2d> cvVals
        = _ReadName<GfVec2d>(usdPrim, "brep:curveUv:nurb:controlVertices");
    const VtArray<double> knVals
        = _ReadName<double>(usdPrim, "brep:curveUv:nurb:knots");
    const VtArray<unsigned int> faceLoopCounts
        = _Read<unsigned int>(brep.GetFaceLoopCountAttr());
    const VtArray<unsigned int> loopEdgeuseCounts
        = _Read<unsigned int>(brep.GetLoopEdgeuseCountAttr());
    const VtArray<unsigned int> edgeuseEdgeIndices
        = _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr());

    if (orderVals.empty() || vcVals.empty() || cvVals.empty() || knVals.empty()
        || faceLoopCounts.empty() || loopEdgeuseCounts.empty()
        || edgeuseEdgeIndices.empty()) {
        return;
    }

    // brep:curveUv:nurb:weights is optional: an omitted array means a
    // non-rational curve, every weight 1.0, which is how the schema, the
    // converter and OpenCASCADE all read it. Requiring it authored would skip
    // the check on exactly the assets that need it.
    std::vector<double> weightsAll;
    {
        const VtArray<double> authored
            = _ReadName<double>(usdPrim, "brep:curveUv:nurb:weights");
        if (authored.empty()) {
            weightsAll.assign(cvVals.size(), 1.0);
        } else {
            weightsAll.assign(authored.begin(), authored.end());
        }
    }

    const size_t numEdgeuses = edgeuseEdgeIndices.size();
    if (orderVals.size() < numEdgeuses || vcVals.size() < numEdgeuses) {
        return;
    }

    struct _Endpoints {
        bool valid = false;
        _Uv2 start;
        _Uv2 end;
    };

    std::vector<_Endpoints> curveEndpoints;
    curveEndpoints.reserve(numEdgeuses);
    size_t cvOffset = 0;
    size_t knotOffset = 0;
    for (size_t curveIdx = 0; curveIdx < numEdgeuses; ++curveIdx) {
        const unsigned int order = orderVals[curveIdx];
        const unsigned int nCv = vcVals[curveIdx];

        // A face with no authored pcurve for this edgeuse: skip it without
        // advancing the control-vertex or knot cursors.
        if (order == 0 && nCv == 0) {
            curveEndpoints.push_back(_Endpoints());
            continue;
        }
        if (order < 1 || nCv < order) {
            return;
        }

        const size_t nKnots = static_cast<size_t>(nCv) + order;
        if (cvOffset + nCv > cvVals.size()
            || knotOffset + nKnots > knVals.size()) {
            return;
        }
        if (cvOffset + nCv > weightsAll.size()) {
            weightsAll.resize(cvOffset + nCv, 1.0);
        }

        const std::vector<double> knots(knVals.begin() + knotOffset,
                                        knVals.begin() + knotOffset + nKnots);
        const std::vector<GfVec2d> cvs(cvVals.begin() + cvOffset,
                                       cvVals.begin() + cvOffset + nCv);
        const std::vector<double> weights(weightsAll.begin() + cvOffset,
                                          weightsAll.begin() + cvOffset + nCv);
        const double tStart = knots[order - 1];
        const double tEnd = knots[nCv];

        _Endpoints ep;
        if (!_DeBoorEvaluate2d(order, knots, cvs, weights, tStart, &ep.start)
            || !_DeBoorEvaluate2d(order, knots, cvs, weights, tEnd, &ep.end)) {
            return;
        }
        ep.valid = true;
        curveEndpoints.push_back(ep);
        cvOffset += nCv;
        knotOffset += nKnots;
    }

    size_t loopIdx = 0;
    size_t edgeuseOffset = 0;
    for (size_t faceIdx = 0; faceIdx < faceLoopCounts.size(); ++faceIdx) {
        const size_t nLoops = faceLoopCounts[faceIdx];
        for (size_t localLoopIdx = 0; localLoopIdx < nLoops; ++localLoopIdx) {
            if (loopIdx >= loopEdgeuseCounts.size()) {
                return;
            }
            const size_t nEdgeuses = loopEdgeuseCounts[loopIdx];
            const size_t loopStart = edgeuseOffset;
            const size_t loopEnd = edgeuseOffset + nEdgeuses;
            ++loopIdx;
            edgeuseOffset = loopEnd;

            if (nEdgeuses == 0) {
                continue;
            }
            if (loopEnd > curveEndpoints.size()) {
                return;
            }

            bool allValid = true;
            for (size_t i = loopStart; i < loopEnd; ++i) {
                if (!curveEndpoints[i].valid) {
                    allValid = false;
                    break;
                }
            }
            if (!allValid) {
                continue;
            }

            for (size_t local = 0; local < nEdgeuses; ++local) {
                const size_t edgeuseIdx = loopStart + local;
                const size_t nextLocal = (local + 1) % nEdgeuses;
                const size_t nextEdgeuseIdx = loopStart + nextLocal;
                const _Uv2 &uvEnd = curveEndpoints[edgeuseIdx].end;
                const _Uv2 &nextUvStart = curveEndpoints[nextEdgeuseIdx].start;
                const double du = uvEnd.u - nextUvStart.u;
                const double dv = uvEnd.v - nextUvStart.v;
                const double dist = std::sqrt(du * du + dv * dv);
                if (dist > _UvClosureTol) {
                    _Err(errors,
                         UsdSolidValidationErrorNameTokens->uvLoopNotClosed,
                         usdPrim,
                         TfStringPrintf(
                             "[BA.763] BrepArray <%s>: face #%zu loop #%zu "
                             "edgeuse #%zu UV endpoint (%.6f, %.6f) does not "
                             "meet next edgeuse #%zu UV start (%.6f, %.6f); "
                             "gap %.6f exceeds tolerance 1e-06.",
                             usdPrim.GetPath().GetText(), faceIdx,
                             localLoopIdx, edgeuseIdx, uvEnd.u, uvEnd.v,
                             nextEdgeuseIdx, nextUvStart.u, nextUvStart.v,
                             dist));
                }
            }
        }
    }
}

// --- BA.764: zero-length UV trim curve ------------------------------------ //
// A pcurve whose control vertices all coincide trims nothing; the face boundary
// it belongs to has a hole in parameter space. Measured on the control polygon
// extent, which bounds the curve from above, so a curve only registers as
// collapsed when even that bound vanishes.
void
_CheckZeroLengthUvTrimCurves(const UsdPrim &usdPrim,
                             UsdValidationErrorVector *errors)
{
    const VtArray<unsigned int> uvOrders
        = _ReadName<unsigned int>(usdPrim, "brep:curveUv:nurb:order");
    const VtArray<unsigned int> uvVc
        = _ReadName<unsigned int>(usdPrim, "brep:curveUv:nurb:vertexCount");
    const VtArray<GfVec2d> uvCvs
        = _ReadName<GfVec2d>(usdPrim, "brep:curveUv:nurb:controlVertices");

    if (uvOrders.empty() || uvVc.empty() || uvCvs.empty()) {
        return;
    }

    size_t cvOffset = 0;
    for (size_t curveIdx = 0; curveIdx < uvVc.size(); ++curveIdx) {
        if (curveIdx >= uvOrders.size()) {
            return;
        }
        const unsigned int order = uvOrders[curveIdx];
        const size_t nCv = uvVc[curveIdx];
        if (nCv == 0) {
            continue;
        }
        if (cvOffset + nCv > uvCvs.size()) {
            // The flat control-vertex stream is truncated: stop scanning.
            // Continuing here would leave cvOffset unadvanced and let a later,
            // smaller vertexCount re-slice the same tail, which reports
            // collapsed curves that are not there.
            break;
        }

        double uLo = uvCvs[cvOffset][0], uHi = uLo;
        double vLo = uvCvs[cvOffset][1], vHi = vLo;
        for (size_t j = 1; j < nCv; ++j) {
            uLo = std::min(uLo, uvCvs[cvOffset + j][0]);
            uHi = std::max(uHi, uvCvs[cvOffset + j][0]);
            vLo = std::min(vLo, uvCvs[cvOffset + j][1]);
            vHi = std::max(vHi, uvCvs[cvOffset + j][1]);
        }
        const double firstU = uvCvs[cvOffset][0];
        const double firstV = uvCvs[cvOffset][1];
        cvOffset += nCv;

        if (order == 0) {
            continue;
        }

        const double uExtent = uHi - uLo;
        const double vExtent = vHi - vLo;
        const double diagonal
            = std::sqrt(uExtent * uExtent + vExtent * vExtent);
        if (diagonal <= _UvZeroLengthTol) {
            _Err(errors,
                 UsdSolidValidationErrorNameTokens->zeroLengthUvTrimCurve,
                 usdPrim,
                 TfStringPrintf(
                     "[BA.764] BrepArray <%s>: UV trim curve #%zu has collapsed "
                     "control vertices at (%.6f, %.6f); control polygon extent "
                     "%.6e is at or below tolerance %.1e.",
                     usdPrim.GetPath().GetText(), curveIdx, firstU, firstV,
                     diagonal, _UvZeroLengthTol));
        }
    }
}

UsdValidationErrorVector
_BrepArrayUvTrim(const UsdPrim &usdPrim,
                 const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);

    UsdValidationErrorVector errors;
    _CheckUvTrimDomainContainment(usdPrim, brep, &errors);
    _CheckFullPeriodFaceSeamEdgeuse(usdPrim, brep, &errors);
    _CheckAnalyticPeriodicDomains(usdPrim, brep, &errors);
    _CheckUvLoopClosure(usdPrim, brep, &errors);
    _CheckZeroLengthUvTrimCurves(usdPrim, &errors);
    return errors;
}

} // anonymous namespace

TF_REGISTRY_FUNCTION(UsdValidationRegistry)
{
    UsdValidationRegistry &registry = UsdValidationRegistry::GetInstance();

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayStructure, _BrepArrayStructure);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayTopology, _BrepArrayTopology);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayTokenValues,
        _BrepArrayTokenValues);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayRanges, _BrepArrayRanges);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayFaceOuterLoop,
        _BrepArrayFaceOuterLoop);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayAnalyticSurfaces,
        _BrepArrayAnalyticSurfaces);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayAuthorship, _BrepArrayAuthorship);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayDataTypes, _BrepArrayDataTypes);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArraySchemaUsage,
        _BrepArraySchemaUsage);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayReferences, _BrepArrayReferences);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayCompleteness,
        _BrepArrayCompleteness);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayContainment,
        _BrepArrayContainment);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArraySpans, _BrepArraySpans);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayAnalyticCurves,
        _BrepArrayAnalyticCurves);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayNurbs, _BrepArrayNurbs);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArraySolidClosure,
        _BrepArraySolidClosure);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayDegenerateEdges,
        _BrepArrayDegenerateEdges);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayEdgeCurveVertices,
        _BrepArrayEdgeCurveVertices);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayUvTrim, _BrepArrayUvTrim);
}

PXR_NAMESPACE_CLOSE_SCOPE
