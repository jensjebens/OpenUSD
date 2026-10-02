//
// Copyright 2024 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//

#include "pxr/base/gf/half.h"
#include "pxr/base/gf/vec2d.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec2h.h"
#include "pxr/base/gf/vec2i.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4d.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/vec4h.h"
#include "pxr/base/gf/vec4i.h"
#include "pxr/base/tf/registryManager.h"
#include "pxr/base/tf/stringUtils.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/sdf/attributeSpec.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/sdf/valueTypeName.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primDefinition.h"
#include "pxr/usd/usd/relationship.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdSolid/brepArray.h"
#include "pxr/usd/usdSolid/tokens.h"
#include "pxr/usdValidation/usdSolidValidators/validatorTokens.h"
#include "pxr/usdValidation/usdValidation/error.h"
#include "pxr/usdValidation/usdValidation/registry.h"
#include "pxr/usdValidation/usdValidation/timeRange.h"
#include "pxr/usdValidation/usdValidation/validator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
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
// authors no positive brep:intersectTol3d. See _FirstAuthoredIntersectTol3d.
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
// _FallbackIntersectTol3d. A per-Brep tolerance would need per-edge Brep
// attribution, which the flat data does not carry; the first Brep's tolerance
// is exact for the common single-Brep case.
double
_FirstAuthoredIntersectTol3d(const UsdSolidBrepArray &brep)
{
    const VtArray<double> tol
        = _Read<double>(brep.GetBrepIntersectTol3dAttr());
    return (!tol.empty() && tol[0] > 0.0 && std::isfinite(tol[0]))
        ? tol[0]
        : _FallbackIntersectTol3d;
}

// Whether shell `i` is a point shell: one that contributes a
// brep:shellPoint:point:position entry. The schema makes shell:pointType
// meaningful only when the shell has no faceuses and no wire edges, so a
// "BrepPointAPI" token on a face or wire shell is ignored. A shell index past
// the end of any of the three arrays is not a point shell; the array sizes are
// BA.080's to report. Mirrors BrepConstants.is_brep_point_shell in
// brep_validator.py (OMPE-106532), which BA.325, BA.583 and BA.710 share.
bool
_IsBrepPointShell(size_t i, const VtArray<TfToken> &pointTypes,
                  const VtArray<unsigned int> &faceuseCounts,
                  const VtArray<unsigned int> &wireEdgeCounts)
{
    static const TfToken brepPointApi("BrepPointAPI");
    return i < pointTypes.size() && i < faceuseCounts.size()
        && i < wireEdgeCounts.size() && pointTypes[i] == brepPointApi
        && faceuseCounts[i] == 0u && wireEdgeCounts[i] == 0u;
}

// ========================================================================== //
// brep_validator.py port                                                     //
// ========================================================================== //
//
// The BA.xxx rules are defined by tools/brep_validator/brep_validator.py in
// the solidmodeling repository, which is the reference implementation. The
// rules in this file are ports of its methods and keep its control flow:
// where Python stops after a structural error, skips a stratum, reports per
// element rather than per attribute, or measures against a partition its own
// helper computed, the port does the same, so the two validators report the
// same findings on the same file. Each ported method names the Python method
// it mirrors.
//
// Python runs every method from one entry point, BrepValidator.CheckPrim. Here
// each registered validator owns a set of rules and runs the methods that can
// produce them on a _BrepChecker, which drops the findings of rules the
// validator does not own. A Python method that reports under several rules
// (_validate_edge_arrays reports seven) therefore runs once per validator that
// owns one of them, and each rule is reported by exactly one validator.

// The number brep_validator.py calls BrepConstants.NUMERICAL_TOLERANCE.
constexpr double _PyNumericalTolerance = 1e-11;

// An attribute value as brep_validator.py sees it.
//
// Python reads values with Usd.Attribute.Get(), which returns what the layer
// authored, not the type the schema declares: an int[] where the schema says
// uint[] reads as integers, a float[] as floats, a string[] as strings, and an
// unregistered type name such as uint2[] as Sdf.UnregisteredValue. The rules
// then duck-type the value, so a wrong-typed array is still counted, summed and
// compared. Reading the typed C++ value instead (a VtArray<unsigned int> from
// an int[]) yields an empty array, and every rule downstream then judges a
// different file from the one Python judged.
//
// Numbers of any width (bool, int, uint, int64, half, float, double) are held
// as doubles with a flag for integral types; str and token elements as
// strings; GfVec2/3/4 of any scalar as tuples. A scalar (non-array) value is
// not a sequence: Python would raise on len() or iteration, and the port
// treats it as absent instead.
class _PyValue
{
public:
    enum class Kind { None, Unregistered, Scalar, Array };
    enum class Elem { Number, String, Tuple, Other };

    // attr.Get(): None when the attribute is invalid or has no value.
    static _PyValue Read(const UsdAttribute &attr)
    {
        _PyValue v;
        VtValue value;
        if (!attr || !attr.Get(&value) || value.IsEmpty()) {
            return v;
        }
        if (value.IsHolding<SdfUnregisteredValue>()) {
            v._kind = Kind::Unregistered;
            return v;
        }
        v._kind = value.IsArrayValued() ? Kind::Array : Kind::Scalar;
        if (v._LoadNumber<bool>(value, true)
            || v._LoadNumber<unsigned char>(value, true)
            || v._LoadNumber<int>(value, true)
            || v._LoadNumber<unsigned int>(value, true)
            || v._LoadNumber<int64_t>(value, true)
            || v._LoadNumber<uint64_t>(value, true)
            || v._LoadNumber<GfHalf>(value, false)
            || v._LoadNumber<float>(value, false)
            || v._LoadNumber<double>(value, false)
            || v._LoadString<std::string>(value)
            || v._LoadString<TfToken>(value)
            || v._LoadTuple<GfVec2d>(value) || v._LoadTuple<GfVec2f>(value)
            || v._LoadTuple<GfVec2h>(value) || v._LoadTuple<GfVec2i>(value)
            || v._LoadTuple<GfVec3d>(value) || v._LoadTuple<GfVec3f>(value)
            || v._LoadTuple<GfVec3h>(value) || v._LoadTuple<GfVec3i>(value)
            || v._LoadTuple<GfVec4d>(value) || v._LoadTuple<GfVec4f>(value)
            || v._LoadTuple<GfVec4h>(value) || v._LoadTuple<GfVec4i>(value)) {
            return v;
        }
        v._elem = Elem::Other;
        v._size = v._kind == Kind::Array ? value.GetArraySize() : 1;
        return v;
    }

    // Python's [].
    static _PyValue EmptyList()
    {
        _PyValue v;
        v._kind = Kind::Array;
        v._elem = Elem::Number;
        return v;
    }

    // A Python list of numbers built by a rule (first_vertex, second_vertex).
    static _PyValue Numbers(std::vector<double> values, bool integral)
    {
        _PyValue v;
        v._kind = Kind::Array;
        v._elem = Elem::Number;
        v._integral = integral;
        v._size = values.size();
        v._num = std::move(values);
        return v;
    }

    bool IsNone() const { return _kind == Kind::None; }
    bool IsUnregistered() const { return _kind == Kind::Unregistered; }
    // Whether Python's len() and iteration work.
    bool IsSequence() const { return _kind == Kind::Array; }
    // len(value); zero for a value that is not a sequence.
    size_t Len() const { return IsSequence() ? _size : 0; }

    // Python truthiness: None and empty arrays are false, an
    // Sdf.UnregisteredValue (a plain object) is true.
    bool Truthy() const
    {
        switch (_kind) {
        case Kind::None:
            return false;
        case Kind::Unregistered:
            return true;
        case Kind::Array:
            return _size > 0;
        case Kind::Scalar:
            if (_elem == Elem::Number) {
                return _num[0] != 0.0;
            }
            if (_elem == Elem::String) {
                return !_str[0].empty();
            }
            return true;
        }
        return false;
    }

    // `value or []`. An unregistered value is truthy and stays.
    _PyValue OrEmpty() const { return Truthy() ? *this : EmptyList(); }
    // `[] if value is None else value`.
    _PyValue NoneToEmpty() const { return IsNone() ? EmptyList() : *this; }

    Elem GetElem() const { return _elem; }
    bool IsNumbers() const { return IsSequence() && _elem == Elem::Number; }
    bool IsStrings() const { return IsSequence() && _elem == Elem::String; }
    bool IsTuples() const { return IsSequence() && _elem == Elem::Tuple; }
    bool Integral() const { return _integral; }

    double Num(size_t i) const { return _num[i]; }
    const std::string &Str(size_t i) const { return _str[i]; }
    size_t Dim() const { return _dim; }
    double Tup(size_t i, size_t c) const { return _num[i * _dim + c]; }

    // int(element). False where Python raises: a string that is not a base-10
    // integer, a NaN or infinite float, a tuple.
    bool ToInt(size_t i, long long *out) const
    {
        if (_elem == Elem::Number) {
            const double x = _num[i];
            if (!std::isfinite(x)) {
                return false;
            }
            *out = static_cast<long long>(std::trunc(x));
            return true;
        }
        if (_elem == Elem::String) {
            return _ParseInt(_str[i], out);
        }
        return false;
    }

    // float(element). False where Python raises.
    bool ToFloat(size_t i, double *out) const
    {
        if (_elem == Elem::Number) {
            *out = _num[i];
            return true;
        }
        if (_elem == Elem::String) {
            const std::string s = TfStringTrim(_str[i]);
            if (s.empty()) {
                return false;
            }
            char *end = nullptr;
            const double x = std::strtod(s.c_str(), &end);
            if (end != s.c_str() + s.size()) {
                return false;
            }
            *out = x;
            return true;
        }
        return false;
    }

    // `element == text`: only a str element can equal a string.
    bool Equals(size_t i, const std::string &text) const
    {
        return _elem == Elem::String && _str[i] == text;
    }

    // str(element), for messages.
    std::string Repr(size_t i) const
    {
        switch (_elem) {
        case Elem::Number:
            return NumRepr(_num[i], _integral);
        case Elem::String:
            return _str[i];
        case Elem::Tuple: {
            std::vector<std::string> parts;
            for (size_t c = 0; c < _dim; ++c) {
                parts.push_back(TfStringPrintf("%g", Tup(i, c)));
            }
            return "(" + TfStringJoin(parts, ", ") + ")";
        }
        case Elem::Other:
            break;
        }
        return "?";
    }

    // Python's repr of a number: integers without a fractional part, floats
    // with one.
    static std::string NumRepr(double x, bool integral)
    {
        if (integral) {
            return TfStringPrintf("%lld", static_cast<long long>(x));
        }
        if (std::isnan(x)) {
            return "nan";
        }
        if (std::isinf(x)) {
            return x > 0 ? "inf" : "-inf";
        }
        if (x == std::trunc(x) && std::abs(x) < 1e16) {
            return TfStringPrintf("%.1f", x);
        }
        return TfStringPrintf("%.17g", x);
    }

private:
    template <class T>
    bool _LoadNumber(const VtValue &value, bool integral)
    {
        if (value.IsHolding<VtArray<T>>()) {
            const VtArray<T> &a = value.UncheckedGet<VtArray<T>>();
            _num.reserve(a.size());
            for (const T &x : a) {
                _num.push_back(_ToDouble(x));
            }
            _size = a.size();
        } else if (value.IsHolding<T>()) {
            _num.push_back(_ToDouble(value.UncheckedGet<T>()));
            _size = 1;
        } else {
            return false;
        }
        _elem = Elem::Number;
        _integral = integral;
        return true;
    }

    template <class T>
    bool _LoadString(const VtValue &value)
    {
        if (value.IsHolding<VtArray<T>>()) {
            const VtArray<T> &a = value.UncheckedGet<VtArray<T>>();
            _str.reserve(a.size());
            for (const T &x : a) {
                _str.push_back(_ToString(x));
            }
            _size = a.size();
        } else if (value.IsHolding<T>()) {
            _str.push_back(_ToString(value.UncheckedGet<T>()));
            _size = 1;
        } else {
            return false;
        }
        _elem = Elem::String;
        return true;
    }

    template <class V>
    bool _LoadTuple(const VtValue &value)
    {
        const size_t dim = V::dimension;
        if (value.IsHolding<VtArray<V>>()) {
            const VtArray<V> &a = value.UncheckedGet<VtArray<V>>();
            _num.reserve(a.size() * dim);
            for (const V &x : a) {
                for (size_t c = 0; c < dim; ++c) {
                    _num.push_back(_ToDouble(x[c]));
                }
            }
            _size = a.size();
        } else if (value.IsHolding<V>()) {
            const V &x = value.UncheckedGet<V>();
            for (size_t c = 0; c < dim; ++c) {
                _num.push_back(_ToDouble(x[c]));
            }
            _size = 1;
        } else {
            return false;
        }
        _elem = Elem::Tuple;
        _dim = dim;
        _integral = std::is_integral<typename V::ScalarType>::value;
        return true;
    }

    template <class T>
    static double _ToDouble(const T &x) { return static_cast<double>(x); }
    static double _ToDouble(const GfHalf &x)
    {
        return static_cast<double>(static_cast<float>(x));
    }
    static double _ToDouble(const bool &x) { return x ? 1.0 : 0.0; }
    static std::string _ToString(const std::string &s) { return s; }
    static std::string _ToString(const TfToken &t) { return t.GetString(); }

    // Python int(str): optional surrounding whitespace, an optional sign, and
    // base-10 digits, which may be grouped with single underscores.
    static bool _ParseInt(const std::string &text, long long *out)
    {
        const std::string s = TfStringTrim(text);
        size_t i = 0;
        bool negative = false;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            negative = s[i] == '-';
            ++i;
        }
        if (i >= s.size()) {
            return false;
        }
        long long value = 0;
        bool lastDigit = false;
        for (; i < s.size(); ++i) {
            const char ch = s[i];
            if (ch >= '0' && ch <= '9') {
                value = value * 10 + (ch - '0');
                lastDigit = true;
            } else if (ch == '_' && lastDigit && i + 1 < s.size()) {
                lastDigit = false;
            } else {
                return false;
            }
        }
        if (!lastDigit) {
            return false;
        }
        *out = negative ? -value : value;
        return true;
    }

    Kind _kind = Kind::None;
    Elem _elem = Elem::Other;
    bool _integral = false;
    size_t _size = 0;
    size_t _dim = 0;
    std::vector<double> _num;
    std::vector<std::string> _str;
};

// Python's floor division of integers, which rounds toward negative infinity.
long long
_PyFloorDiv(long long a, long long b)
{
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

// The [start, end) element range of Python's sequence[start:end] on a sequence
// of length n: negative bounds count from the end, and both are clamped.
std::pair<size_t, size_t>
_PySlice(long long start, long long end, size_t n)
{
    const long long len = static_cast<long long>(n);
    if (start < 0) {
        start = std::max(start + len, 0LL);
    }
    if (end < 0) {
        end = std::max(end + len, 0LL);
    }
    start = std::min(start, len);
    end = std::min(end, len);
    if (end < start) {
        end = start;
    }
    return { static_cast<size_t>(start), static_cast<size_t>(end) };
}

// Python's sequence[i] index on a sequence of length n: a negative index
// counts from the end. False where Python raises IndexError.
bool
_PyIndex(long long i, size_t n, size_t *out)
{
    const long long len = static_cast<long long>(n);
    if (i < 0) {
        i += len;
    }
    if (i < 0 || i >= len) {
        return false;
    }
    *out = static_cast<size_t>(i);
    return true;
}

// [0] followed by the running sums of `counts` (compute_offsets in
// _compute_brep_offsets).
std::vector<long long>
_PyCumulative(const std::vector<long long> &counts)
{
    std::vector<long long> out(1, 0);
    long long running = 0;
    for (const long long c : counts) {
        running += c;
        out.push_back(running);
    }
    return out;
}

// Python's repr of a list of attribute names: ['a', 'b'].
std::string
_PyNameList(const std::vector<std::string> &names)
{
    std::vector<std::string> quoted;
    for (const std::string &n : names) {
        quoted.push_back("'" + n + "'");
    }
    return "[" + TfStringJoin(quoted, ", ") + "]";
}

// The error name a rule reports under unless the finding names another. Each
// rule keeps the name the native validators gave it before the port.
TfToken
_RuleErrorName(const std::string &rule)
{
    const auto *t = &(*UsdSolidValidationErrorNameTokens);
    static const std::unordered_map<std::string, TfToken> names = {
        { "BA.000", t->inconsistentBrepArraySizes },
        { "BA.005", t->missingBrepAttributes },
        { "BA.010", t->nonPositiveIntersectTol3d },
        { "BA.020", t->inconsistentBrepArraySizes },
        { "BA.025", t->invalidExtentOrder },
        { "BA.030", t->invalidExtentOrder },
        { "BA.035", t->invalidExtentOrder },
        { "BA.040", t->brepExtentOutsidePrimExtent },
        { "BA.045", t->brepExtentOutsidePrimExtent },
        { "BA.050", t->brepExtentOutsidePrimExtent },
        { "BA.061", t->invalidAttributeDataType },
        { "BA.065", t->inconsistentRegionArraySizes },
        { "BA.070", t->attributeNotAuthored },
        { "BA.075", t->invalidRegionType },
        { "BA.076", t->invalidAttributeDataType },
        { "BA.080", t->inconsistentShellArraySizes },
        { "BA.085", t->attributeNotAuthored },
        { "BA.090", t->invalidShellPointType },
        { "BA.091", t->invalidAttributeDataType },
        { "BA.100", t->inconsistentFaceuseArraySizes },
        { "BA.105", t->attributeNotAuthored },
        { "BA.110", t->invalidFaceuseOrientationType },
        { "BA.115", t->faceuseFaceIndexOutOfRange },
        { "BA.116", t->invalidAttributeDataType },
        { "BA.120", t->inconsistentFaceArraySizes },
        { "BA.125", t->attributeNotAuthored },
        { "BA.130", t->invalidFaceSurfaceType },
        { "BA.135", t->invalidFaceTrimType },
        { "BA.140", t->invalidFaceLoopCount },
        { "BA.145", t->invalidFaceRangeStructure },
        { "BA.150", t->inconsistentFaceArraySizes },
        { "BA.155", t->degenerateFaceURange },
        { "BA.160", t->degenerateFaceVRange },
        { "BA.161", t->invalidAttributeDataType },
        { "BA.165", t->inconsistentLoopArraySizes },
        { "BA.170", t->attributeNotAuthored },
        { "BA.175", t->loopVertexIndexOutOfRange },
        { "BA.176", t->invalidAttributeDataType },
        { "BA.180", t->inconsistentEdgeuseArraySizes },
        { "BA.185", t->attributeNotAuthored },
        { "BA.190", t->invalidEdgeuseOrientationType },
        { "BA.195", t->invalidEdgeuseRadialEntryType },
        { "BA.196", t->invalidAttributeDataType },
        { "BA.200", t->edgeuseNextRadialIndexOutOfRange },
        { "BA.205", t->edgeuseEdgeIndexOutOfRange },
        { "BA.210", t->inconsistentEdgeArraySizes },
        { "BA.215", t->attributeNotAuthored },
        { "BA.225", t->edgeVertexIndexOutOfRange },
        { "BA.230", t->inconsistentEdgeArraySizes },
        { "BA.235", t->invalidEdgeRangeOrder },
        { "BA.237", t->invalidAttributeDataType },
        { "BA.245", t->invalidEdgeCurveType },
        { "BA.250", t->inconsistentWireEdgeArraySizes },
        { "BA.255", t->attributeNotAuthored },
        { "BA.260", t->invalidWireEdgeCurveType },
        { "BA.265", t->wireEdgeVertexIndexOutOfRange },
        { "BA.270", t->invalidWireEdgeRangeStructure },
        { "BA.275", t->invalidWireEdgeRangeOrder },
        { "BA.290", t->nurbSchemaUsageInconsistent },
        { "BA.291", t->invalidAttributeDataType },
        { "BA.295", t->vertexArraySizeMismatch },
        { "BA.300", t->attributeNotAuthored },
        { "BA.305", t->schemaUsageInconsistent },
        { "BA.310", t->vertexPositionOutsideBrepExtent },
        { "BA.315", t->invalidVertexPointType },
        { "BA.316", t->invalidAttributeDataType },
        { "BA.320", t->vertexPointPositionSizeMismatch },
        { "BA.325", t->shellPointPositionSizeMismatch },
        { "BA.326", t->invalidAttributeDataType },
        { "BA.327", t->invalidAttributeDataType },
        { "BA.330", t->nurbSizeArrayMismatch },
        { "BA.335", t->nurbNonPositiveOrder },
        { "BA.340", t->nurbOrderExceedsVertexCount },
        { "BA.345", t->nurbControlVertexWeightSizeMismatch },
        { "BA.350", t->nurbNonPositiveWeight },
        { "BA.355", t->nurbKnotCountMismatch },
        { "BA.360", t->nurbKnotNotMonotonic },
        { "BA.365", t->controlPointOutsideBrepExtent },
        { "BA.370", t->nurbSchemaUsageInconsistent },
        { "BA.371", t->nurbInvalidDataType },
        { "BA.375", t->nurbSizeArrayMismatch },
        { "BA.380", t->nurbNonPositiveOrder },
        { "BA.385", t->nurbOrderExceedsVertexCount },
        { "BA.390", t->nurbControlVertexWeightSizeMismatch },
        { "BA.395", t->nurbKnotCountMismatch },
        { "BA.400", t->nurbKnotNotMonotonic },
        { "BA.405", t->nurbControlVertexWeightSizeMismatch },
        { "BA.410", t->nurbNonPositiveWeight },
        { "BA.415", t->nurbSchemaUsageInconsistent },
        { "BA.416", t->nurbInvalidDataType },
        { "BA.420", t->nurbSizeArrayMismatch },
        { "BA.425", t->nurbNonPositiveOrder },
        { "BA.430", t->nurbOrderExceedsVertexCount },
        { "BA.435", t->nurbControlVertexWeightSizeMismatch },
        { "BA.440", t->nurbNonPositiveWeight },
        { "BA.445", t->nurbKnotCountMismatch },
        { "BA.450", t->nurbKnotCountMismatch },
        { "BA.455", t->nurbKnotNotMonotonic },
        { "BA.460", t->nurbKnotNotMonotonic },
        { "BA.465", t->controlPointOutsideBrepExtent },
        { "BA.470", t->nurbSchemaUsageInconsistent },
        { "BA.471", t->nurbInvalidDataType },
        { "BA.480", t->inconsistentAnalyticSurfaceCount },
        { "BA.481", t->nonPositiveSurfaceRadius },
        { "BA.482", t->nonUnitSurfaceAxis },
        { "BA.483", t->nonUnitSurfaceRefDirection },
        { "BA.484", t->nonOrthogonalSurfaceAxes },
        { "BA.485", t->schemaUsageInconsistent },
        { "BA.490", t->inconsistentAnalyticSurfaceCount },
        { "BA.491", t->nonUnitSurfaceAxis },
        { "BA.492", t->nonUnitSurfaceRefDirection },
        { "BA.493", t->nonOrthogonalSurfaceAxes },
        { "BA.495", t->schemaUsageInconsistent },
        { "BA.500", t->inconsistentAnalyticSurfaceCount },
        { "BA.501", t->nonPositiveSurfaceRadius },
        { "BA.502", t->nonUnitSurfaceAxis },
        { "BA.503", t->nonUnitSurfaceRefDirection },
        { "BA.504", t->nonOrthogonalSurfaceAxes },
        { "BA.505", t->schemaUsageInconsistent },
        { "BA.510", t->inconsistentAnalyticSurfaceCount },
        { "BA.511", t->nonPositiveSurfaceRadius },
        { "BA.512", t->nonUnitSurfaceAxis },
        { "BA.513", t->nonUnitSurfaceRefDirection },
        { "BA.514", t->nonOrthogonalSurfaceAxes },
        { "BA.515", t->invalidConeSemiAngle },
        { "BA.516", t->schemaUsageInconsistent },
        { "BA.520", t->inconsistentAnalyticSurfaceCount },
        { "BA.521", t->nonPositiveSurfaceRadius },
        { "BA.522", t->nonPositiveSurfaceRadius },
        { "BA.523", t->nonUnitSurfaceAxis },
        { "BA.524", t->nonUnitSurfaceRefDirection },
        { "BA.525", t->nonOrthogonalSurfaceAxes },
        { "BA.526", t->schemaUsageInconsistent },
        { "BA.530", t->analyticCurveArraySizeMismatch },
        { "BA.531", t->analyticCurveNonPositiveRadius },
        { "BA.532", t->analyticCurveAxisNotUnitLength },
        { "BA.533", t->analyticCurveRefDirectionNotUnitLength },
        { "BA.534", t->analyticCurveAxisRefDirectionNotOrthogonal },
        { "BA.540", t->analyticCurveArraySizeMismatch },
        { "BA.541", t->lineDirectionNotUnitLength },
        { "BA.550", t->analyticCurveArraySizeMismatch },
        { "BA.551", t->analyticCurveNonPositiveRadius },
        { "BA.552", t->analyticCurveNonPositiveRadius },
        { "BA.553", t->analyticCurveAxisNotUnitLength },
        { "BA.554", t->analyticCurveRefDirectionNotUnitLength },
        { "BA.555", t->analyticCurveAxisRefDirectionNotOrthogonal },
        { "BA.560", t->surfaceDomainSpanExceeded },
        { "BA.561", t->sphereVDomainOutOfBounds },
        { "BA.562", t->surfaceDomainSpanExceeded },
        { "BA.563", t->surfaceDomainSpanExceeded },
        { "BA.564", t->surfaceDomainSpanExceeded },
        { "BA.565", t->surfaceDomainSpanExceeded },
        { "BA.570", t->edgeRangeSpanExceeded },
        { "BA.571", t->edgeRangeSpanExceeded },
        { "BA.580", t->faceusePairingViolation },
        { "BA.581", t->radialEdgeuseChainNotClosed },
        { "BA.582", t->orphanEdge },
        { "BA.583", t->schemaUsageInconsistent },
        { "BA.590", t->nurbOrderBelowMinimum },
        { "BA.591", t->nurbVertexCountBelowOrder },
        { "BA.600", t->analyticCurveEndpointVertexMismatch },
        { "BA.601", t->analyticCurveEndpointVertexMismatch },
        { "BA.602", t->analyticCurveEndpointVertexMismatch },
        { "BA.610", t->circleVertexRadiusMismatch },
        { "BA.620", t->analyticSurfaceOriginOutsideBrepExtent },
        { "BA.630", t->angularRangeOutsidePrimaryPeriod },
        { "BA.631", t->angularRangeOutsidePrimaryPeriod },
        { "BA.640", t->faceVDomainNotOrdered },
        { "BA.650", t->nurbSizeArrayMismatch },
        { "BA.651", t->nurbOrderBelowMinimum },
        { "BA.652", t->nurbOrderExceedsVertexCount },
        { "BA.653", t->nurbControlVertexWeightSizeMismatch },
        { "BA.654", t->nurbNonPositiveWeight },
        { "BA.655", t->nurbKnotCountMismatch },
        { "BA.656", t->nurbKnotNotMonotonic },
        { "BA.657", t->controlPointOutsideBrepExtent },
        { "BA.658", t->nurbSchemaDataIncomplete },
        { "BA.660", t->nonFiniteFloatArrayValue },
        { "BA.670", t->radialChainEdgeInconsistent },
        { "BA.680", t->geomSubsetIndexOutOfRange },
        { "BA.681", t->geomSubsetIndicesOverlap },
        { "BA.682", t->geomSubsetMaterialBindingTargetMissing },
        { "BA.700", t->regionCountBelowMinimum },
        { "BA.701", t->regionShellCountBelowMinimum },
        { "BA.702", t->shellWithoutContent },
        { "BA.710", t->shellPointPositionOutsideBrepExtent },
        { "BA.720", t->edgeCurveTypeNotExhaustive },
        { "BA.721", t->wireEdgeCurveTypeNotExhaustive },
        { "BA.722", t->faceSurfaceTypeNotExhaustive },
        { "BA.730", t->nurbsEdgeEndpointVertexMismatch },
        { "BA.750", t->uvTrimCurveOutsideFaceDomain },
        { "BA.761", t->fullPeriodFaceNoSeamEdgeuse },
        { "BA.762", t->fullPeriodFaceDomainNotAligned },
        { "BA.763", t->uvLoopNotClosed },
        { "BA.764", t->zeroLengthUvTrimCurve },
        { "BA.765", t->analyticPeriodicDomainOutOfBounds },
    };
    const auto it = names.find(rule);
    return it == names.end() ? TfToken() : it->second;
}

// The partitions brep_validator.py's _compute_brep_offsets returns: for each
// stratum, [0] followed by the running per-Brep totals, so the objects of
// Brep b occupy [s[b], s[b + 1]). Python integers, so a count authored as a
// negative int[] value carries through as Python carries it.
struct _PyOffsets
{
    std::vector<long long> regions, shells, faceuses, faces, loops, edgeuses,
        edges, wireedges, vertices, edgeControlVertices,
        surfaceControlVertices, edge3dNurbsCurves, surfaceNurbs, curveUv;
};

// One BrepArray under one validator: the ported brep_validator.py methods,
// sharing one read of each attribute and one offset computation, reporting
// only the rules the validator owns.
class _BrepChecker
{
public:
    _BrepChecker(const UsdPrim &prim, std::initializer_list<const char *> owned)
        : _prim(prim), _path(prim.GetPath().GetString())
    {
        for (const char *rule : owned) {
            _owned.insert(rule);
        }
    }

    UsdValidationErrorVector TakeErrors() { return std::move(_errors); }

    // _compute_brep_offsets, for the BA.091 it reports while coercing the count
    // arrays. Every other method computes the offsets on first use too, as
    // Python's cached helper does, so calling this first changes nothing else.
    void ComputeBrepOffsets() { _Offsets(); }

    void ValidateBrepExtent();
    void ValidateBrepTols();
    void ValidateBrepArray();
    void ValidateRegionArrays();
    void ValidateShellArrays();
    void ValidateFaceuseArrays();
    void ValidateFaceArrays();
    void ValidateFaceLoopCountMinimum();
    void ValidateFaceusePairing();
    void ValidateFaceRanges();
    void ValidateLoopArrays();
    void ValidateLoopVertexIndex();
    void ValidateEdgeAndEdgeuseAuthorship();
    void ValidateEdgeArrays();
    void ValidateEdgeuseArraysIfAny();
    void ValidateWireEdgeArrays();
    void ValidateRadialEdgeuseClosure();
    void ValidateOrphanEdges();
    void ValidateVertexArrays();
    void ValidatePointPosition();
    void ValidateTopologyGeometryCorrespondence();
    void ValidateAttributeDataTypes();
    void ValidateMinimumTopologyCounts();
    void ValidateTypeCountExhaustive();
    void ValidateRadialChainConsistency();

private:
    // --- reporting ---------------------------------------------------------
    bool _Owns(const char *rule) const { return _owned.count(rule) != 0; }

    void _Report(const char *rule, const TfToken &name,
                 UsdValidationErrorType type, const std::string &message)
    {
        if (!_Owns(rule)) {
            return;
        }
        _errors.emplace_back(name, type, _PrimSites(_prim),
                             TfStringPrintf("[%s] BrepArray <%s>: %s", rule,
                                            _path.c_str(), message.c_str()));
    }

    // _AddFailedCheck.
    void _Fail(const char *rule, const std::string &message)
    {
        _Report(rule, _RuleErrorName(rule), UsdValidationErrorType::Error,
                message);
    }
    void _Fail(const char *rule, const TfToken &name,
               const std::string &message)
    {
        _Report(rule, name, UsdValidationErrorType::Error, message);
    }
    // _AddWarning.
    void _Warn(const char *rule, const std::string &message)
    {
        _Report(rule, _RuleErrorName(rule), UsdValidationErrorType::Warn,
                message);
    }

    // --- attribute access --------------------------------------------------
    UsdAttribute _Attr(const std::string &name) const
    {
        return _prim.GetAttribute(TfToken(name));
    }

    // brep_array.GetAttribute(name).Get(), read once per checker.
    const _PyValue &_Get(const std::string &name)
    {
        auto it = _cache.find(name);
        if (it == _cache.end()) {
            it = _cache.emplace(name, _PyValue::Read(_Attr(name))).first;
        }
        return it->second;
    }

    // BrepConstants.safe_get_attribute: [] for a missing, unregistered or
    // non-sequence value.
    _PyValue _SafeGet(const std::string &name)
    {
        const _PyValue &v = _Get(name);
        return v.IsSequence() ? v : _PyValue::EmptyList();
    }

    // attr.IsAuthored(), on a possibly invalid attribute.
    bool _IsAuthored(const std::string &name) const
    {
        const UsdAttribute a = _Attr(name);
        return a && a.IsAuthored();
    }

    bool _HasAuthoredValue(const std::string &name) const
    {
        const UsdAttribute a = _Attr(name);
        return a && a.HasAuthoredValue();
    }

    // `[int(v) for v in (values or [])]`; false where Python raises.
    static bool _IntList(const _PyValue &value, std::vector<long long> *out)
    {
        out->clear();
        const _PyValue v = value.OrEmpty();
        if (!v.IsSequence()) {
            return false;
        }
        for (size_t i = 0; i < v.Len(); ++i) {
            long long x = 0;
            if (!v.ToInt(i, &x)) {
                return false;
            }
            out->push_back(x);
        }
        return true;
    }

    // sum(values) over a numeric sequence, as a double, and whether every
    // element was numeric (Python raises on anything else).
    static double _Sum(const _PyValue &value)
    {
        double total = 0.0;
        if (value.IsNumbers()) {
            for (size_t i = 0; i < value.Len(); ++i) {
                total += value.Num(i);
            }
        }
        return total;
    }

    // `sum(values) if values else 0` on attr.Get().
    double _SumIfAny(const std::string &name) { return _Sum(_Get(name).OrEmpty()); }

    // --- brep_validator.py helpers -----------------------------------------
    std::pair<_PyValue, size_t> _GetAttrSequence(const std::string &name,
                                                 const char *rule,
                                                 const char *expectedDesc);
    void _ValidateArraySizesAndAuthored(
        const std::vector<std::string> &attributes, const char *rule,
        const double *size, bool requireAuthored);
    void _ValidateAuthorshipOnly(const std::vector<std::string> &attributes,
                                 const char *rule);
    void _ValidateAllowedTokens(const std::string &name,
                                const std::vector<std::string> &allowed,
                                const char *rule, const char *itemName);
    struct _IndexArray
    {
        std::string name;
        _PyValue values;
    };
    void _ValidateIndexingRelationships(const std::vector<double> &counts,
                                        const std::vector<_IndexArray> &arrays,
                                        const std::vector<long long> &offsets,
                                        const char *rule);
    void _ValidateStratumDataTypes(
        const std::vector<std::pair<const char *, const char *>> &attributes,
        const char *rule);
    const _PyOffsets &_Offsets();
    std::vector<long long> _NurbsControlVertexOffsets(
        bool edge, const std::vector<long long> &entityOffsets);
    std::vector<long long> _TypeCountOffsets(
        const char *typeAttr, const char *typeToken,
        const std::vector<long long> &entityOffsets);
    // The Brep whose [offsets[i], offsets[i + 1]) holds `index`, if any.
    static bool _FindBrep(const std::vector<long long> &offsets, double index,
                          size_t *brep)
    {
        for (size_t i = 0; i + 1 < offsets.size(); ++i) {
            if (offsets[i] <= index && index < offsets[i + 1]) {
                *brep = i;
                return true;
            }
        }
        return false;
    }

    UsdPrim _prim;
    std::string _path;
    std::unordered_set<std::string> _owned;
    UsdValidationErrorVector _errors;
    std::unordered_map<std::string, _PyValue> _cache;
    bool _offsetsComputed = false;
    _PyOffsets _offsets;
};

// _get_attr_sequence: the value of an authored attribute as a sequence, and
// its length. A missing attribute, one declared without a value, or one with
// no authored opinion reads as ([], 0) silently; an unregistered type or a
// non-sequence value is reported under `rule` and also reads as ([], 0).
std::pair<_PyValue, size_t>
_BrepChecker::_GetAttrSequence(const std::string &name, const char *rule,
                               const char *expectedDesc)
{
    const UsdAttribute attr = _Attr(name);
    if (!attr || !attr.HasAuthoredValue() || !attr.IsAuthored()) {
        return { _PyValue::EmptyList(), 0 };
    }
    const _PyValue &value = _Get(name);
    if (value.IsUnregistered()) {
        _Fail(rule, TfStringPrintf("%s has an unregistered USD type; "
                                   "expected %s.",
                                   name.c_str(), expectedDesc));
        return { _PyValue::EmptyList(), 0 };
    }
    if (!value.IsSequence()) {
        _Fail(rule, TfStringPrintf("%s is not a valid sequence; expected %s.",
                                   name.c_str(), expectedDesc));
        return { _PyValue::EmptyList(), 0 };
    }
    return { value, value.Len() };
}

// _validate_array_sizes_and_authored: every attribute is authored (when
// required), the authored ones agree on their size, and that size is `size`
// when one is given. One finding per missing attribute, one for any
// disagreement, one for a size mismatch -- not one per attribute.
void
_BrepChecker::_ValidateArraySizesAndAuthored(
    const std::vector<std::string> &attributes, const char *rule,
    const double *size, bool requireAuthored)
{
    std::vector<std::pair<std::string, size_t>> sizes;
    for (const std::string &name : attributes) {
        if (!_IsAuthored(name)) {
            if (requireAuthored) {
                _Fail(rule, TfStringPrintf("%s is not authored in BrepArray.",
                                           name.c_str()));
            }
        } else {
            sizes.emplace_back(
                name, _GetAttrSequence(name, rule, "a valid USD array").second);
        }
    }
    if (sizes.empty() && !requireAuthored) {
        return;
    }
    const auto sizesRepr = [&]() {
        std::vector<std::string> parts;
        for (const auto &s : sizes) {
            parts.push_back(TfStringPrintf("'%s': %zu", s.first.c_str(),
                                           s.second));
        }
        return "{" + TfStringJoin(parts, ", ") + "}";
    };
    bool consistent = true;
    for (const auto &s : sizes) {
        consistent = consistent && s.second == sizes.front().second;
    }
    if (!consistent) {
        _Fail(rule,
              TfStringPrintf("Inconsistent sizes detected across %s "
                             "attributes: %s.",
                             _PyNameList(attributes).c_str(),
                             sizesRepr().c_str()));
    }
    if (size) {
        bool mismatch = false;
        for (const auto &s : sizes) {
            mismatch = mismatch || static_cast<double>(s.second) != *size;
        }
        if (mismatch) {
            _Fail(rule, TfStringPrintf(
                            "Expected size %s does not match actual sizes %s.",
                            _PyValue::NumRepr(*size, *size == std::trunc(*size))
                                .c_str(),
                            sizesRepr().c_str()));
        }
    }
}

// _validate_authorship_only: each attribute has an authored opinion.
void
_BrepChecker::_ValidateAuthorshipOnly(const std::vector<std::string> &attributes,
                                      const char *rule)
{
    for (const std::string &name : attributes) {
        if (!_IsAuthored(name)) {
            _Fail(rule, TfStringPrintf("%s is not authored in BrepArray.",
                                       name.c_str()));
        }
    }
}

// _validate_allowed_tokens: every entry of the attribute is one of `allowed`,
// reported once per attribute, naming every offending index.
void
_BrepChecker::_ValidateAllowedTokens(const std::string &name,
                                     const std::vector<std::string> &allowed,
                                     const char *rule, const char *itemName)
{
    const _PyValue &values = _Get(name);
    if (!values.Truthy() || !values.IsSequence()) {
        return;
    }
    std::vector<size_t> invalid;
    for (size_t i = 0; i < values.Len(); ++i) {
        bool ok = false;
        for (const std::string &a : allowed) {
            ok = ok || values.Equals(i, a);
        }
        if (!ok) {
            invalid.push_back(i);
        }
    }
    if (invalid.empty()) {
        return;
    }
    const std::string allowedRepr = _PyNameList(allowed);
    if (invalid.size() == 1) {
        const size_t i = invalid.front();
        _Fail(rule, TfStringPrintf("%s[%zu] has invalid value '%s' for %s "
                                   "#%zu. Allowed values are %s.",
                                   name.c_str(), i, values.Repr(i).c_str(),
                                   itemName, i, allowedRepr.c_str()));
        return;
    }
    std::vector<std::string> details;
    for (const size_t i : invalid) {
        details.push_back(
            TfStringPrintf("[%zu]='%s'", i, values.Repr(i).c_str()));
    }
    _Fail(rule, TfStringPrintf("%s has invalid values at indices: %s. Allowed "
                               "values are %s.",
                               name.c_str(), TfStringJoin(details, ", ").c_str(),
                               allowedRepr.c_str()));
}

// _validate_indexing_relationships: each index array has one entry per
// counted object, and block b of it (counts[b] entries) indexes inside
// [offsets[b], offsets[b + 1]). A pair-valued entry (int2) is checked
// component by component. An absent array (Python None) is reported even when
// nothing is counted.
void
_BrepChecker::_ValidateIndexingRelationships(
    const std::vector<double> &counts, const std::vector<_IndexArray> &arrays,
    const std::vector<long long> &offsets, const char *rule)
{
    if (counts.empty()) {
        return;
    }
    std::vector<double> blockOffsets(1, 0.0);
    double total = 0.0;
    for (const double c : counts) {
        total += c;
        blockOffsets.push_back(total);
    }
    const size_t numBreps = counts.size();
    const std::string totalRepr = _PyValue::NumRepr(total, total == std::trunc(total));

    for (const _IndexArray &a : arrays) {
        const _PyValue &array = a.values;
        if (!array.Truthy() && total > 0) {
            _Fail(rule, TfStringPrintf("%s is missing or not authored, thus "
                                       "the indexing is not valid.",
                                       a.name.c_str()));
            continue;
        }
        if (array.IsNone()) {
            _Fail(rule, TfStringPrintf("%s is None, thus the indexing is not "
                                       "valid.",
                                       a.name.c_str()));
            continue;
        }
        if (static_cast<double>(array.Len()) != total) {
            _Fail(rule, TfStringPrintf("%s size %zu does not match expected "
                                       "size %s, thus the indexing is not "
                                       "valid.",
                                       a.name.c_str(), array.Len(),
                                       totalRepr.c_str()));
            continue;
        }
        for (size_t b = 0; b < numBreps; ++b) {
            if (b + 1 >= offsets.size()) {
                _Fail(rule, TfStringPrintf("Insufficient offsets for brep_idx "
                                           "%zu. Offsets length: %zu, expected "
                                           "at least %zu",
                                           b, offsets.size(), b + 2));
                continue;
            }
            const double partitionStart = static_cast<double>(offsets[b]);
            const double partitionEnd = static_cast<double>(offsets[b + 1]);
            const auto block = _PySlice(static_cast<long long>(blockOffsets[b]),
                                        static_cast<long long>(blockOffsets[b + 1]),
                                        array.Len());
            for (size_t i = block.first; i < block.second; ++i) {
                if (array.IsTuples() && array.Dim() == 2) {
                    for (size_t c = 0; c < 2; ++c) {
                        const double sub = array.Tup(i, c);
                        if (sub < partitionStart || sub >= partitionEnd) {
                            _Fail(rule, TfStringPrintf(
                                "%s contains invalid index %s (element %zu of "
                                "pair %s) in block #%zu. Expected to be in "
                                "range [%lld, %lld).",
                                a.name.c_str(),
                                _PyValue::NumRepr(sub, array.Integral()).c_str(),
                                c, array.Repr(i).c_str(), b, offsets[b],
                                offsets[b + 1]));
                        }
                    }
                } else if (array.IsNumbers()) {
                    const double idx = array.Num(i);
                    if (idx < partitionStart || idx >= partitionEnd) {
                        _Fail(rule, TfStringPrintf(
                            "%s contains invalid index %s in block #%zu. "
                            "Expected to be in range [%lld, %lld).",
                            a.name.c_str(), array.Repr(i).c_str(), b,
                            offsets[b], offsets[b + 1]));
                    }
                }
                // Any other element type makes Python raise on the comparison;
                // there is nothing to compare here either.
            }
        }
    }
}

// _validate_stratum_data_types: an attribute with an authored value must be
// authored with the type the schema declares for it -- or, for an attribute
// no applied schema declares, the type listed here. The authored type is the
// strongest spec's typeName, which keeps the role (point3d[] vs vector3d[])
// that the held GfVec3d value does not.
void
_BrepChecker::_ValidateStratumDataTypes(
    const std::vector<std::pair<const char *, const char *>> &attributes,
    const char *rule)
{
    const UsdPrimDefinition &primDef = _prim.GetPrimDefinition();
    const TfTokenVector &defined = primDef.GetPropertyNames();
    for (const auto &entry : attributes) {
        const TfToken name(entry.first);
        const UsdAttribute attr = _prim.GetAttribute(name);
        if (!attr || !attr.HasAuthoredValue()) {
            continue;
        }
        const std::string schemaType = attr.GetTypeName().GetAsToken().GetString();
        std::string authoredType;
        for (const SdfPropertySpecHandle &spec :
             attr.GetPropertyStack(UsdTimeCode::Default())) {
            const SdfAttributeSpecHandle attrSpec
                = TfDynamic_cast<SdfAttributeSpecHandle>(spec);
            if (attrSpec && attrSpec->GetTypeName()) {
                authoredType = attrSpec->GetTypeName().GetAsToken().GetString();
                break;
            }
        }
        const std::string actual
            = authoredType.empty() ? schemaType : authoredType;
        const bool schemaDefines
            = std::find(defined.begin(), defined.end(), name) != defined.end();
        const std::string expected = schemaDefines ? schemaType : entry.second;
        if (actual != expected) {
            _Fail(rule, TfStringPrintf(
                "Invalid data type for %s. Schema expects '%s' but got "
                "'%s'.%s",
                entry.first, expected.c_str(), actual.c_str(),
                authoredType.empty()
                    ? ""
                    : TfStringPrintf(" Authored type: '%s'.",
                                     authoredType.c_str()).c_str()));
        }
    }
}

// _compute_nurbs_control_vertex_offsets: per-Brep control-vertex totals. Python
// sums the vertex counts of the curves (or surfaces) whose index falls in each
// Brep's edge (or face) range -- indexing the NURBS record by edge or face
// index, not by NURBS curve index -- and that is reproduced here.
std::vector<long long>
_BrepChecker::_NurbsControlVertexOffsets(
    bool edge, const std::vector<long long> &entityOffsets)
{
    const std::vector<long long> zeros(entityOffsets.size(), 0);
    std::vector<long long> counts;
    if (edge) {
        const _PyValue vc
            = _Get("brep:edge3dNurb:curve3d:nurb:vertexCount").OrEmpty();
        if (!vc.Truthy() || !vc.IsNumbers()) {
            return zeros;
        }
        for (size_t b = 0; b + 1 < entityOffsets.size(); ++b) {
            long long total = 0;
            const long long end
                = std::min(entityOffsets[b + 1],
                           static_cast<long long>(vc.Len()));
            for (long long e = entityOffsets[b]; e < end; ++e) {
                size_t i = 0;
                if (_PyIndex(e, vc.Len(), &i)) {
                    total += static_cast<long long>(vc.Num(i));
                }
            }
            counts.push_back(total);
        }
    } else {
        const _PyValue u
            = _Get("brep:surface:nurb:uVertexCount").OrEmpty();
        const _PyValue v
            = _Get("brep:surface:nurb:vVertexCount").OrEmpty();
        if (!u.Truthy() || !v.Truthy() || !u.IsNumbers() || !v.IsNumbers()) {
            return zeros;
        }
        for (size_t b = 0; b + 1 < entityOffsets.size(); ++b) {
            long long total = 0;
            const long long end = std::min(
                { entityOffsets[b + 1], static_cast<long long>(u.Len()),
                  static_cast<long long>(v.Len()) });
            for (long long f = entityOffsets[b]; f < end; ++f) {
                size_t iu = 0, iv = 0;
                if (_PyIndex(f, u.Len(), &iu) && _PyIndex(f, v.Len(), &iv)) {
                    total += static_cast<long long>(u.Num(iu) * v.Num(iv));
                }
            }
            counts.push_back(total);
        }
    }
    return _PyCumulative(counts);
}

// _compute_edge3d_nurbs_data_offsets / _compute_surface_nurbs_data_offsets:
// per-Brep counts of the edges (faces) whose type is the NURBS type.
std::vector<long long>
_BrepChecker::_TypeCountOffsets(const char *typeAttr, const char *typeToken,
                                const std::vector<long long> &entityOffsets)
{
    const _PyValue types = _Get(typeAttr).OrEmpty();
    if (!types.Truthy()) {
        return std::vector<long long>(entityOffsets.size(), 0);
    }
    std::vector<long long> counts;
    for (size_t b = 0; b + 1 < entityOffsets.size(); ++b) {
        long long count = 0;
        const long long end = std::min(entityOffsets[b + 1],
                                       static_cast<long long>(types.Len()));
        for (long long e = entityOffsets[b]; e < end; ++e) {
            size_t i = 0;
            if (_PyIndex(e, types.Len(), &i) && types.Equals(i, typeToken)) {
                ++count;
            }
        }
        counts.push_back(count);
    }
    return _PyCumulative(counts);
}

// _compute_brep_offsets.
//
// The count arrays are coerced to integers first; any that cannot be is
// reported under BA.091, one finding per attribute, and every partition is
// then empty -- which Python's downstream rules read as "no Brep owns
// anything".
//
// The partitions are not all derived the same way, and Python's choices are
// kept. Regions, shells, faceuses, faces and loops come from the count arrays.
// With one Brep (or none), edges, vertices and edgeuses span their whole
// authored arrays; with more than one, edges and vertices are counted from the
// distinct indices each Brep's edgeuses reach. Faces are faceuses // 2.
const _PyOffsets &
_BrepChecker::_Offsets()
{
    if (_offsetsComputed) {
        return _offsets;
    }
    _offsetsComputed = true;
    _PyOffsets &o = _offsets;

    struct Counts
    {
        const char *name;
        std::vector<long long> values;
        bool ok;
    };
    Counts c[6] = { { "brep:regionCount", {}, true },
                    { "region:shellCount", {}, true },
                    { "shell:faceuseCount", {}, true },
                    { "face:loopCount", {}, true },
                    { "loop:edgeuseCount", {}, true },
                    { "shell:wireEdgeCount", {}, true } };
    bool allOk = true;
    for (Counts &count : c) {
        count.ok = _IntList(_Get(count.name).NoneToEmpty(), &count.values);
        if (!count.ok) {
            _Fail("BA.091", TfStringPrintf("%s has invalid data type; expected "
                                           "integers.",
                                           count.name));
            allOk = false;
        }
    }
    if (!allOk) {
        return o;
    }
    const std::vector<long long> &regionCounts = c[0].values;
    const std::vector<long long> &shellCountsPerRegion = c[1].values;
    const std::vector<long long> &faceuseCountsPerShell = c[2].values;
    const std::vector<long long> &loopCountsPerFace = c[3].values;
    const std::vector<long long> &edgeuseCountsPerLoop = c[4].values;
    const std::vector<long long> &wireedgeCountsPerShell = c[5].values;

    // sum(values[start:end]) with Python slice semantics.
    const auto sliceSum = [](const std::vector<long long> &values,
                             long long start, long long end) {
        const auto r = _PySlice(start, end, values.size());
        long long s = 0;
        for (size_t i = r.first; i < r.second; ++i) {
            s += values[i];
        }
        return s;
    };
    const long long nRegionCounts = static_cast<long long>(regionCounts.size());

    o.regions = _PyCumulative(regionCounts);

    std::vector<long long> shells;
    for (size_t b = 0; b < regionCounts.size(); ++b) {
        const long long start = o.regions[b];
        long long end = o.regions[b + 1];
        const long long n = static_cast<long long>(shellCountsPerRegion.size());
        if (!shellCountsPerRegion.empty() && start < n) {
            end = std::min(end, n);
            shells.push_back(sliceSum(shellCountsPerRegion, start, end));
        } else {
            shells.push_back(0);
        }
    }
    o.shells = _PyCumulative(shells);

    std::vector<long long> faceuses;
    for (size_t b = 0; b < shells.size(); ++b) {
        const long long start = o.shells[b];
        long long end = o.shells[b + 1];
        const long long n = static_cast<long long>(faceuseCountsPerShell.size());
        if (!faceuseCountsPerShell.empty() && start < n) {
            end = std::min(end, n);
            faceuses.push_back(sliceSum(faceuseCountsPerShell, start, end));
        } else {
            faceuses.push_back(0);
        }
    }
    o.faceuses = _PyCumulative(faceuses);

    std::vector<long long> faces;
    for (const long long fu : faceuses) {
        faces.push_back(_PyFloorDiv(fu, 2));
    }
    o.faces = _PyCumulative(faces);

    std::vector<long long> loops;
    if (!loopCountsPerFace.empty()) {
        const long long maxIndex = static_cast<long long>(loopCountsPerFace.size());
        for (size_t b = 0; b < faces.size(); ++b) {
            const long long safeStart = std::min(o.faces[b], maxIndex);
            const long long safeEnd = std::min(o.faces[b + 1], maxIndex);
            loops.push_back(safeStart < safeEnd
                                ? sliceSum(loopCountsPerFace, safeStart, safeEnd)
                                : 0);
        }
    } else {
        loops.assign(faces.empty() ? 1 : faces.size(), 0);
    }
    o.loops = _PyCumulative(loops);

    std::vector<long long> edgeusesPerBrep;
    for (long long b = 0; b < nRegionCounts; ++b) {
        long long total = 0;
        const long long loopStart = o.loops[b];
        const long long loopEnd = std::min(
            o.loops[b + 1], static_cast<long long>(edgeuseCountsPerLoop.size()));
        for (long long lp = loopStart; lp < loopEnd; ++lp) {
            size_t i = 0;
            if (_PyIndex(lp, edgeuseCountsPerLoop.size(), &i)) {
                total += edgeuseCountsPerLoop[i];
            }
        }
        edgeusesPerBrep.push_back(total);
    }
    o.edgeuses = _PyCumulative(edgeusesPerBrep);

    std::vector<long long> wireedges;
    {
        const long long maxIndex
            = static_cast<long long>(wireedgeCountsPerShell.size());
        for (size_t b = 0; b < shells.size(); ++b) {
            const long long safeStart = std::min(o.shells[b], maxIndex);
            const long long safeEnd = std::min(o.shells[b + 1], maxIndex);
            wireedges.push_back(
                safeStart < safeEnd
                    ? sliceSum(wireedgeCountsPerShell, safeStart, safeEnd)
                    : 0);
        }
    }
    o.wireedges = _PyCumulative(wireedges);

    const size_t numBreps = regionCounts.size();
    long long totalEdges = 0, totalEdgeuses = 0;
    if (numBreps > 1) {
        const _PyValue edgeuseEdgeIndices = _Get("edgeuse:edgeIndex").OrEmpty();
        // The distinct values a Brep's edgeuses name, kept as doubles so 5 and
        // 5.0 are one edge, as they are in a Python set.
        std::vector<std::set<double>> edgeSets;
        std::vector<long long> edgeCounts;
        for (size_t b = 0; b < numBreps; ++b) {
            std::set<double> edges;
            if (edgeuseEdgeIndices.IsNumbers()) {
                const auto r = _PySlice(o.edgeuses[b], o.edgeuses[b + 1],
                                        edgeuseEdgeIndices.Len());
                for (size_t i = r.first; i < r.second; ++i) {
                    edges.insert(edgeuseEdgeIndices.Num(i));
                }
            }
            edgeCounts.push_back(static_cast<long long>(edges.size()));
            edgeSets.push_back(std::move(edges));
        }
        o.edges = _PyCumulative(edgeCounts);

        const _PyValue edgeVertexIndices = _SafeGet("edge:vertexIndices");
        const _PyValue wireVertexIndices = _SafeGet("wireEdge:vertexIndices");
        const _PyValue loopVertexIndices = _SafeGet("loop:vertexIndex");
        const _PyValue loopEdgeCounts = _Get("loop:edgeuseCount").OrEmpty();
        const auto addPair = [](const _PyValue &pairs, size_t i,
                                std::set<double> *out) {
            if (pairs.IsTuples() && pairs.Dim() == 2) {
                out->insert(pairs.Tup(i, 0));
                out->insert(pairs.Tup(i, 1));
            }
        };
        std::vector<long long> vertexCounts;
        for (size_t b = 0; b < numBreps; ++b) {
            std::set<double> vertices;
            for (const double e : edgeSets[b]) {
                if (e < static_cast<double>(edgeVertexIndices.Len())
                    && e == std::trunc(e)) {
                    size_t i = 0;
                    if (_PyIndex(static_cast<long long>(e),
                                 edgeVertexIndices.Len(), &i)) {
                        addPair(edgeVertexIndices, i, &vertices);
                    }
                }
            }
            // Python walks the Brep's shells but indexes the per-Brep wire-edge
            // partition with the shell index; kept as is.
            for (long long s = o.shells[b]; s < o.shells[b + 1]; ++s) {
                if (s < static_cast<long long>(o.wireedges.size()) - 1) {
                    size_t si = 0;
                    if (!_PyIndex(s, o.wireedges.size(), &si)
                        || si + 1 >= o.wireedges.size()) {
                        continue;
                    }
                    const long long end = std::min(
                        o.wireedges[si + 1],
                        static_cast<long long>(wireVertexIndices.Len()));
                    for (long long w = o.wireedges[si]; w < end; ++w) {
                        size_t i = 0;
                        if (_PyIndex(w, wireVertexIndices.Len(), &i)) {
                            addPair(wireVertexIndices, i, &vertices);
                        }
                    }
                }
            }
            const long long loopEnd = std::min(
                { o.loops[b + 1],
                  static_cast<long long>(loopVertexIndices.Len()),
                  static_cast<long long>(loopEdgeCounts.Len()) });
            for (long long lp = o.loops[b]; lp < loopEnd; ++lp) {
                size_t i = 0, j = 0;
                if (_PyIndex(lp, loopEdgeCounts.Len(), &i)
                    && _PyIndex(lp, loopVertexIndices.Len(), &j)
                    && loopEdgeCounts.IsNumbers() && loopEdgeCounts.Num(i) == 0
                    && loopVertexIndices.IsNumbers()) {
                    vertices.insert(loopVertexIndices.Num(j));
                }
            }
            vertexCounts.push_back(static_cast<long long>(vertices.size()));
        }
        o.vertices = _PyCumulative(vertexCounts);
    } else {
        totalEdges = static_cast<long long>(_SafeGet("edge:vertexIndices").Len());
        const long long totalVertices
            = static_cast<long long>(_SafeGet("vertex:pointType").Len());
        totalEdgeuses
            = static_cast<long long>(_SafeGet("edgeuse:edgeIndex").Len());
        o.edges = { 0, totalEdges };
        o.vertices = { 0, totalVertices };
        o.edgeuses = { 0, totalEdgeuses };
    }

    const long long totalFaces
        = faces.empty() ? 0 : std::accumulate(faces.begin(), faces.end(), 0LL);
    const std::vector<long long> edgeRange
        = numBreps > 1 ? o.edges : std::vector<long long>{ 0, totalEdges };
    const std::vector<long long> faceRange
        = numBreps > 1 ? o.faces : std::vector<long long>{ 0, totalFaces };
    o.edgeControlVertices = _NurbsControlVertexOffsets(true, edgeRange);
    o.surfaceControlVertices = _NurbsControlVertexOffsets(false, faceRange);
    o.edge3dNurbsCurves = _TypeCountOffsets("edge:curveType",
                                            "BrepCurve3dNurbAPI", edgeRange);
    o.surfaceNurbs = _TypeCountOffsets("face:surfaceType",
                                       "BrepSurfaceNurbAPI", faceRange);
    o.curveUv = numBreps > 1 ? o.edgeuses
                             : std::vector<long long>{ 0, totalEdgeuses };
    return o;
}

// brep_validator.py's isFloatLessThan / isFloatGreaterThan: an ordering that
// ignores single-precision noise (math.isclose with rel_tol 1e-5, abs_tol
// 1e-6), for comparisons against the float3 prim extent.
bool
_PyIsClose(double a, double b)
{
    return std::abs(a - b)
        <= std::max(1e-5 * std::max(std::abs(a), std::abs(b)), 1e-6);
}

bool
_PyIsFloatLessThan(double p, double e)
{
    return p < e && !_PyIsClose(p, e);
}

bool
_PyIsFloatGreaterThan(double p, double e)
{
    return p > e && !_PyIsClose(p, e);
}

// _validate_brep_extent: brep:extent holds one (min, max) corner pair per
// Brep, each pair ordered on every axis (BA.025 X, BA.030 Y, BA.035 Z), and
// each box inside the prim's own extent (BA.040 X, BA.045 Y, BA.050 Z). A
// wrong number of corners is BA.020 and ends the rule.
void
_BrepChecker::ValidateBrepExtent()
{
    const _PyValue &primExtent = _Get("extent");
    const bool havePrimExtent = primExtent.Truthy() && primExtent.IsTuples()
        && primExtent.Len() >= 2 && primExtent.Dim() >= 3;

    const _PyValue brepExtents = _SafeGet("brep:extent");
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    const size_t expected = numBreps * 2;
    if (brepExtents.Len() != expected) {
        _Fail("BA.020", TfStringPrintf("Invalid brep:extent structure. "
                                       "Expected %zu elements (2 * %zu Breps), "
                                       "but got %zu.",
                                       expected, numBreps, brepExtents.Len()));
        return;
    }
    static const char *const axes[3] = { "X", "Y", "Z" };
    static const char *const orderRules[3] = { "BA.025", "BA.030", "BA.035" };
    static const char *const containRules[3] = { "BA.040", "BA.045", "BA.050" };
    for (size_t b = 0; b < numBreps; ++b) {
        if (!brepExtents.IsTuples() || brepExtents.Dim() != 3) {
            // len(point) raises for a scalar element (caught by the rule) and
            // differs from 3 for a 2- or 4-tuple.
            _Fail("BA.020",
                  brepExtents.IsTuples()
                      ? TfStringPrintf("Invalid brep:extent structure for Brep "
                                       "#%zu. Each point must have exactly 3 "
                                       "coordinates (XYZ).",
                                       b)
                      : TfStringPrintf("Invalid brep:extent data type or "
                                       "structure for Brep #%zu. Cannot access "
                                       "extent points.",
                                       b));
            continue;
        }
        const size_t lo = 2 * b, hi = 2 * b + 1;
        for (int a = 0; a < 3; ++a) {
            const double mn = brepExtents.Tup(lo, a);
            const double mx = brepExtents.Tup(hi, a);
            if (mx < mn - _PyNumericalTolerance) {
                _Fail(orderRules[a],
                      TfStringPrintf("Invalid brep:extent %s order for Brep "
                                     "#%zu. %smin (%s) must be <= %smax (%s).",
                                     axes[a], b, axes[a],
                                     _PyValue::NumRepr(mn, false).c_str(),
                                     axes[a],
                                     _PyValue::NumRepr(mx, false).c_str()));
            }
        }
        if (!havePrimExtent) {
            continue;
        }
        for (int a = 0; a < 3; ++a) {
            const double mn = brepExtents.Tup(lo, a);
            const double mx = brepExtents.Tup(hi, a);
            const double pmn = primExtent.Tup(0, a);
            const double pmx = primExtent.Tup(1, a);
            if (_PyIsFloatLessThan(mn, pmn) || _PyIsFloatGreaterThan(mx, pmx)) {
                _Fail(containRules[a],
                      TfStringPrintf("brep:extent for Brep #%zu is outside the "
                                     "prim's %s extent. Brep range: (%g, %g), "
                                     "Prim range: (%g, %g).",
                                     b, axes[a], mn, mx, pmn, pmx));
            }
        }
    }
}

// _validate_brep_tols: every brep:intersectTol3d value is at least
// NUMERICAL_TOLERANCE. A NaN compares false and passes, as in Python.
void
_BrepChecker::ValidateBrepTols()
{
    const _PyValue tol = _Get("brep:intersectTol3d").NoneToEmpty();
    if (!tol.IsNumbers()) {
        return;
    }
    for (size_t i = 0; i < tol.Len(); ++i) {
        if (tol.Num(i) < _PyNumericalTolerance) {
            _Fail("BA.010", TfStringPrintf("brep:intersectTol3d[%zu] must be a "
                                           "positive value.",
                                           i));
        }
    }
}

// _validate_brep_array: the three Brep attributes are authored (BA.005), the
// two per-Brep ones agree in size, and brep:extent holds two corners per Brep
// (BA.000).
void
_BrepChecker::ValidateBrepArray()
{
    _ValidateAuthorshipOnly(
        { "brep:intersectTol3d", "brep:extent", "brep:regionCount" }, "BA.005");
    _ValidateArraySizesAndAuthored({ "brep:intersectTol3d", "brep:regionCount" },
                                   "BA.000", nullptr, false);
    const _PyValue &extent = _Get("brep:extent");
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (!extent.IsNone() && numBreps > 0 && extent.IsSequence()
        && extent.Len() != numBreps * 2) {
        _Fail("BA.000", TfStringPrintf("brep:extent size (%zu) does not match "
                                       "expected size (%zu) for %zu Breps.",
                                       extent.Len(), numBreps * 2, numBreps));
    }
}

// _validate_region_arrays: BA.070 authorship, BA.065 sizes against
// sum(brep:regionCount), BA.075 region:type tokens.
void
_BrepChecker::ValidateRegionArrays()
{
    const double regionCount = _SumIfAny("brep:regionCount");
    const std::vector<std::string> attrs = { "region:shellCount",
                                             "region:type" };
    _ValidateAuthorshipOnly(attrs, "BA.070");
    _ValidateArraySizesAndAuthored(attrs, "BA.065", &regionCount, false);
    _ValidateAllowedTokens("region:type", { "solidRegion", "voidRegion" },
                           "BA.075", "region");
}

// _validate_shell_arrays: BA.085 authorship, BA.080 sizes against
// sum(region:shellCount), BA.090 shell:pointType tokens.
void
_BrepChecker::ValidateShellArrays()
{
    const double shellCount = _SumIfAny("region:shellCount");
    const std::vector<std::string> attrs
        = { "shell:faceuseCount", "shell:wireEdgeCount", "shell:pointType" };
    _ValidateAuthorshipOnly(attrs, "BA.085");
    _ValidateArraySizesAndAuthored(attrs, "BA.080", &shellCount, false);
    _ValidateAllowedTokens("shell:pointType", { "BrepPointAPI", "none" },
                           "BA.090", "shell");
}

// _validate_faceuse_arrays: shell:faceuseCount must coerce to integers
// (BA.091), BA.105 authorship, BA.100 sizes against sum(shell:faceuseCount),
// BA.110 orientation tokens, and BA.115 faceuse:faceIndex inside each Brep's
// face partition. One Brep's faceuse total is the sum over every shell; with
// several Breps it is accumulated region by region.
void
_BrepChecker::ValidateFaceuseArrays()
{
    std::vector<long long> faceuseCounts;
    if (!_IntList(_Get("shell:faceuseCount").OrEmpty(), &faceuseCounts)) {
        _Fail("BA.091", "shell:faceuseCount has invalid data type; expected "
                        "integers.");
        faceuseCounts.clear();
    }
    const double faceuseCount = static_cast<double>(
        std::accumulate(faceuseCounts.begin(), faceuseCounts.end(), 0LL));
    const std::vector<std::string> attrs = { "faceuse:faceIndex",
                                             "faceuse:orientationType" };
    _ValidateAuthorshipOnly(attrs, "BA.105");
    _ValidateArraySizesAndAuthored(attrs, "BA.100", &faceuseCount, false);
    _ValidateAllowedTokens("faceuse:orientationType", { "same", "opposite" },
                           "BA.110", "faceuse");

    const _PyValue regionCounts = _Get("brep:regionCount").OrEmpty();
    std::vector<double> brepFaceuseCounts;
    if (regionCounts.Len() == 1) {
        brepFaceuseCounts.push_back(faceuseCount);
    } else if (regionCounts.IsNumbers()) {
        const _PyValue shellsPerRegion = _Get("region:shellCount").OrEmpty();
        size_t shellIdx = 0;
        long long regionIdx = 0;
        for (size_t b = 0; b < regionCounts.Len(); ++b) {
            long long total = 0;
            const long long regionEnd
                = regionIdx + static_cast<long long>(regionCounts.Num(b));
            for (long long r = regionIdx; r < regionEnd; ++r) {
                size_t ri = 0;
                if (r < static_cast<long long>(shellsPerRegion.Len())
                    && _PyIndex(r, shellsPerRegion.Len(), &ri)
                    && shellsPerRegion.IsNumbers()) {
                    const long long shellsInRegion
                        = static_cast<long long>(shellsPerRegion.Num(ri));
                    for (long long s = 0; s < shellsInRegion; ++s) {
                        if (shellIdx < faceuseCounts.size()) {
                            total += faceuseCounts[shellIdx];
                            ++shellIdx;
                        }
                    }
                }
            }
            brepFaceuseCounts.push_back(static_cast<double>(total));
            regionIdx = regionEnd;
        }
    }
    _ValidateIndexingRelationships(
        brepFaceuseCounts, { { "faceuse:faceIndex", _Get("faceuse:faceIndex") } },
        _Offsets().faces, "BA.115");
}

// _validate_face_arrays: BA.125 authorship; with faces present (counted as
// len(faceuse:faceIndex) // 2), BA.120 sizes of the per-face arrays and
// BA.150 face:range holding two entries per face; with none, every face array
// empty (BA.120). BA.130 / BA.135 tokens either way.
void
_BrepChecker::ValidateFaceArrays()
{
    const std::vector<std::string> standard
        = { "face:loopCount", "face:trimType", "face:surfaceType" };
    const _PyValue faceuseFaceIndices = _Get("faceuse:faceIndex").OrEmpty();
    const double expectedFaces = static_cast<double>(
        faceuseFaceIndices.Truthy() ? faceuseFaceIndices.Len() / 2 : 0);

    std::vector<std::string> all = standard;
    all.push_back("face:range");
    _ValidateAuthorshipOnly(all, "BA.125");
    if (expectedFaces > 0) {
        _ValidateArraySizesAndAuthored(standard, "BA.120", &expectedFaces,
                                       false);
        const size_t ranges = _Get("face:range").OrEmpty().Len();
        if (static_cast<double>(ranges) != expectedFaces * 2) {
            _Fail("BA.150", TfStringPrintf("face:range size mismatch. Expected "
                                           "%zu elements (2 UV pairs per "
                                           "face), but got %zu.",
                                           static_cast<size_t>(expectedFaces) * 2,
                                           ranges));
        }
    } else {
        _ValidateArraySizesAndAuthored(all, "BA.120", &expectedFaces, false);
    }
    _ValidateAllowedTokens(
        "face:surfaceType",
        { "BrepSurfaceNurbAPI", "BrepSurfaceSphereAPI", "BrepSurfacePlaneAPI",
          "BrepSurfaceCylinderAPI", "BrepSurfaceConeAPI",
          "BrepSurfaceTorusAPI" },
        "BA.130", "face");
    _ValidateAllowedTokens("face:trimType", { "rectangular", "general" },
                           "BA.135", "face");
}

// _validate_face_loop_count_minimum (BA.140): every face inside a Brep's face
// partition has at least one loop. Faces past the partition are not checked.
void
_BrepChecker::ValidateFaceLoopCountMinimum()
{
    const _PyValue loopCounts = _Get("face:loopCount").OrEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (!loopCounts.Truthy() || numBreps == 0 || !loopCounts.IsNumbers()) {
        return;
    }
    const std::vector<long long> &faceOffsets = _Offsets().faces;
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= faceOffsets.size()) {
            break;
        }
        const long long end = std::min(
            faceOffsets[b + 1], static_cast<long long>(loopCounts.Len()));
        for (long long f = faceOffsets[b]; f < end; ++f) {
            size_t i = 0;
            if (!_PyIndex(f, loopCounts.Len(), &i)) {
                continue;
            }
            if (loopCounts.Num(i) < 1) {
                _Fail("BA.140", TfStringPrintf("Face #%lld in brep #%zu has "
                                               "loopCount = %s, but each face "
                                               "must have at least one loop.",
                                               f, b,
                                               loopCounts.Repr(i).c_str()));
            }
        }
    }
}

// _validate_faceuse_pairing (BA.580): within each Brep, every face of its face
// partition is named by exactly two of its faceuses.
void
_BrepChecker::ValidateFaceusePairing()
{
    const _PyValue faceIndices = _Get("faceuse:faceIndex").OrEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (!faceIndices.Truthy() || numBreps == 0) {
        return;
    }
    const _PyOffsets &o = _Offsets();
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= o.faceuses.size() || b + 1 >= o.faces.size()) {
            break;
        }
        std::map<double, long long> refCounts;
        const long long fuEnd = std::min(
            o.faceuses[b + 1], static_cast<long long>(faceIndices.Len()));
        for (long long fu = o.faceuses[b]; fu < fuEnd; ++fu) {
            size_t i = 0;
            if (_PyIndex(fu, faceIndices.Len(), &i) && faceIndices.IsNumbers()) {
                ++refCounts[faceIndices.Num(i)];
            }
        }
        for (long long f = o.faces[b]; f < o.faces[b + 1]; ++f) {
            const auto it = refCounts.find(static_cast<double>(f));
            const long long count = it == refCounts.end() ? 0 : it->second;
            if (count != 2) {
                _Fail("BA.580", TfStringPrintf("Face #%lld in brep #%zu is "
                                               "referenced by %lld faceuse(s), "
                                               "expected exactly 2.",
                                               f, b, count));
            }
        }
    }
}

// _validate_face_ranges: BA.145 face:range is double2[] with finite pairs (an
// unregistered type ends the rule), then BA.155 / BA.160 each face's U and V
// intervals are non-degenerate (max >= min + NUMERICAL_TOLERANCE).
//
// The type test reads the attribute's type name, which for this schema
// attribute is always the declared double2[] whatever was authored -- a
// float2[] face:range passes it here as in Python; BA.161 reports the
// authored type.
void
_BrepChecker::ValidateFaceRanges()
{
    const _PyValue &raw = _Get("face:range");
    if (raw.IsUnregistered()) {
        _Fail("BA.145", "face:range has an unregistered USD type; expected "
                        "double2[].");
        return;
    }
    const _PyValue ranges = raw.OrEmpty();
    const std::vector<long long> &faceOffsets = _Offsets().faces;

    const UsdAttribute attr = _Attr("face:range");
    if (attr) {
        const std::string type = attr.GetTypeName().GetAsToken().GetString();
        if (type != "double2[]") {
            _Fail("BA.145", TfStringPrintf(
                "Invalid face:range type. Expected 'double2[]' but got '%s'. "
                "face:range must be double2[] to ensure exactly 2 elements per "
                "range (UV min and max pairs).",
                type.c_str()));
        } else if (ranges.IsTuples()) {
            for (size_t i = 0; i < ranges.Len(); ++i) {
                if (ranges.Dim() != 2) {
                    _Fail("BA.145", TfStringPrintf(
                        "Invalid face:range structure at index %zu. Expected "
                        "exactly 2 elements per range (UV pair) but got %zu "
                        "elements.",
                        i, ranges.Dim()));
                    continue;
                }
                const double u = ranges.Tup(i, 0), v = ranges.Tup(i, 1);
                if (std::isnan(u) || std::isnan(v)) {
                    _Fail("BA.145", TfStringPrintf(
                        "Invalid face:range structure at index %zu. Contains "
                        "NaN values (%s, %s). face:range must contain valid "
                        "numeric UV pairs.",
                        i, _PyValue::NumRepr(u, false).c_str(),
                        _PyValue::NumRepr(v, false).c_str()));
                } else if (std::isinf(u) || std::isinf(v)) {
                    _Fail("BA.145", TfStringPrintf(
                        "Invalid face:range structure at index %zu. Contains "
                        "infinite values (%s, %s). face:range must contain "
                        "finite numeric UV pairs.",
                        i, _PyValue::NumRepr(u, false).c_str(),
                        _PyValue::NumRepr(v, false).c_str()));
                }
            }
        }
    }

    if (!ranges.IsTuples() || ranges.Dim() < 2) {
        return;
    }
    for (size_t i = 0; i + 1 < ranges.Len(); i += 2) {
        const size_t face = i / 2;
        // Python's loop variable keeps the last Brep it tried when no
        // partition holds the face.
        size_t brep = 0;
        size_t local = face;
        if (!_FindBrep(faceOffsets, static_cast<double>(face), &brep)) {
            brep = faceOffsets.size() >= 2 ? faceOffsets.size() - 2 : 0;
        } else {
            local = face - static_cast<size_t>(faceOffsets[brep]);
        }
        const double uMin = ranges.Tup(i, 0), vMin = ranges.Tup(i, 1);
        const double uMax = ranges.Tup(i + 1, 0), vMax = ranges.Tup(i + 1, 1);
        if (uMax < uMin + _PyNumericalTolerance) {
            _Fail("BA.155", TfStringPrintf(
                "Invalid face:range U values (%s, %s) for face #%zu in brep "
                "#%zu. Ensure U range is specified as (Umin, Umax) where Umax "
                "> Umin.",
                _PyValue::NumRepr(uMin, false).c_str(),
                _PyValue::NumRepr(uMax, false).c_str(), local, brep));
        }
        if (vMax < vMin + _PyNumericalTolerance) {
            _Fail("BA.160", TfStringPrintf(
                "Invalid face:range V values (%s, %s) for face #%zu in brep "
                "#%zu. Ensure V range is specified as (Vmin, Vmax) where Vmax "
                "> Vmin.",
                _PyValue::NumRepr(vMin, false).c_str(),
                _PyValue::NumRepr(vMax, false).c_str(), local, brep));
        }
    }
}

// _validate_loop_arrays: BA.170 authorship, BA.165 sizes against
// sum(face:loopCount).
void
_BrepChecker::ValidateLoopArrays()
{
    const double expected = _SumIfAny("face:loopCount");
    const std::vector<std::string> attrs = { "loop:edgeuseCount",
                                             "loop:vertexIndex" };
    _ValidateAuthorshipOnly(attrs, "BA.170");
    _ValidateArraySizesAndAuthored(attrs, "BA.165", &expected, false);
}

// _validate_loop_vertex_index (BA.175): a loop with no edgeuses names a vertex
// inside its Brep's vertex partition, and the loop arrays reach the end of
// each Brep's loop partition.
void
_BrepChecker::ValidateLoopVertexIndex()
{
    const _PyValue loopVertexIndices = _Get("loop:vertexIndex").OrEmpty();
    const _PyValue loopEdgeuseCounts = _Get("loop:edgeuseCount").OrEmpty();
    const long long numVertices
        = static_cast<long long>(_SafeGet("vertex:pointType").Len());
    const _PyOffsets &o = _Offsets();
    const size_t n = std::min(o.loops.size(), o.vertices.size());
    for (size_t b = 0; b + 1 < n; ++b) {
        const long long loopStart = o.loops[b], loopEnd = o.loops[b + 1];
        const long long vStart = o.vertices[b], vEnd = o.vertices[b + 1];
        const long long safeVStart = std::min(vStart, numVertices);
        const long long safeVEnd = std::min(vEnd, numVertices);
        const long long maxLoop = std::min(
            { loopEnd, static_cast<long long>(loopEdgeuseCounts.Len()),
              static_cast<long long>(loopVertexIndices.Len()) });
        if (maxLoop < loopEnd) {
            _Fail("BA.175", TfStringPrintf(
                "loop:edgeuseCount/loop:vertexIndex missing data for Brep[%zu] "
                "(expected loops up to %lld, but only %lld entries are "
                "available).",
                b, loopEnd, maxLoop));
        }
        if (!loopEdgeuseCounts.IsNumbers() || !loopVertexIndices.IsNumbers()) {
            continue;
        }
        for (long long lp = loopStart; lp < maxLoop; ++lp) {
            size_t i = 0, j = 0;
            if (!_PyIndex(lp, loopEdgeuseCounts.Len(), &i)
                || !_PyIndex(lp, loopVertexIndices.Len(), &j)) {
                continue;
            }
            if (loopEdgeuseCounts.Num(i) != 0) {
                continue;
            }
            const double v = loopVertexIndices.Num(j);
            if (v < static_cast<double>(safeVStart)
                || v >= static_cast<double>(safeVEnd)) {
                _Fail("BA.175", TfStringPrintf(
                    "Invalid loop:vertexIndex `%s` for Brep[%zu] when "
                    "loop:edgeuseCount == 0. Expected to be in the vertex "
                    "range [%lld, %lld).",
                    loopVertexIndices.Repr(j).c_str(), b, vStart, vEnd));
            }
        }
    }
}

// CheckPrim's own BA.215 / BA.185 authorship checks for the edge and edgeuse
// families.
void
_BrepChecker::ValidateEdgeAndEdgeuseAuthorship()
{
    _ValidateAuthorshipOnly(
        { "edge:curveType", "edge:vertexIndices", "edge:range" }, "BA.215");
    _ValidateAuthorshipOnly({ "edgeuse:edgeIndex", "edgeuse:orientationType",
                              "edgeuse:nextRadialEUIndex",
                              "edgeuse:thisRadialEntryType" },
                            "BA.185");
}

// _validate_edge_arrays. BA.237 for an unreadable edge:vertexIndices or
// edge:range; BA.210 edge array sizes (with edges present, curveType against
// vertexIndices only) and BA.230 two edge:range entries per edge; BA.245
// curve-type tokens; BA.225 each vertexIndices pair inside its Brep's vertex
// partition -- a malformed pair is reported once and ends the rule, skipping
// the BA.235 check after it; BA.235 each edge:range pair ordered.
void
_BrepChecker::ValidateEdgeArrays()
{
    const std::vector<std::string> standard = { "edge:curveType",
                                                "edge:vertexIndices" };
    const auto vertexIndicesSeq
        = _GetAttrSequence("edge:vertexIndices", "BA.237", "int2[]");
    const _PyValue &edgeVertexIndices = vertexIndicesSeq.first;
    const size_t expectedEdges = vertexIndicesSeq.second;
    const auto rangeSeq = _GetAttrSequence("edge:range", "BA.237", "double[]");
    const _PyValue &edgeRanges = rangeSeq.first;

    if (expectedEdges > 0) {
        _ValidateArraySizesAndAuthored(standard, "BA.210", nullptr, false);
        if (rangeSeq.second != expectedEdges * 2) {
            _Fail("BA.230", TfStringPrintf(
                "Invalid edge:range per-edge structure. Expected exactly 2 "
                "elements per edge (%zu edges x 2 = %zu elements), but got %zu "
                "elements.",
                expectedEdges, expectedEdges * 2, rangeSeq.second));
        }
    } else {
        std::vector<std::string> all = standard;
        all.push_back("edge:range");
        _ValidateArraySizesAndAuthored(all, "BA.210", nullptr, false);
    }
    _ValidateAllowedTokens("edge:curveType",
                           { "BrepCurve3dNurbAPI", "BrepCurve3dCircleAPI",
                             "BrepCurve3dLineAPI", "BrepCurve3dEllipseAPI" },
                           "BA.245", "edge");

    const _PyOffsets &o = _Offsets();
    const auto edgeContext = [&](size_t edge) {
        size_t brep = 0;
        if (o.edges.size() > 1
            && _FindBrep(o.edges, static_cast<double>(edge), &brep)) {
            return TfStringPrintf(" (edge #%zu in brep #%zu)", edge, brep);
        }
        return TfStringPrintf(" (edge #%zu)", edge);
    };

    if (edgeVertexIndices.Truthy()) {
        if (!edgeVertexIndices.IsTuples()) {
            _Fail("BA.225", TfStringPrintf("edge:vertexIndices entry is not a "
                                           "2-element index pair%s.",
                                           edgeContext(0).c_str()));
            return;
        }
        if (edgeVertexIndices.Dim() != 2) {
            _Fail("BA.225", TfStringPrintf("Each entry in edge:vertexIndices "
                                           "must contain exactly two vertex "
                                           "indices%s.",
                                           edgeContext(0).c_str()));
            return;
        }
        _GetAttrSequence("vertex:pointType", "BA.316", "int[]");
        std::vector<double> first, second;
        for (size_t e = 0; e < edgeVertexIndices.Len(); ++e) {
            first.push_back(edgeVertexIndices.Tup(e, 0));
            second.push_back(edgeVertexIndices.Tup(e, 1));
        }
        std::vector<double> edgeCounts;
        for (size_t b = 0; b + 1 < o.edges.size(); ++b) {
            edgeCounts.push_back(
                static_cast<double>(o.edges[b + 1] - o.edges[b]));
        }
        if (edgeCounts.empty()) {
            edgeCounts.push_back(static_cast<double>(edgeVertexIndices.Len()));
        }
        const bool integral = edgeVertexIndices.Integral();
        _ValidateIndexingRelationships(
            edgeCounts,
            { { "first_vertex", _PyValue::Numbers(first, integral) },
              { "second_vertex", _PyValue::Numbers(second, integral) } },
            o.vertices, "BA.225");
    }

    for (size_t i = 0; i + 1 < edgeRanges.Len(); i += 2) {
        double mn = 0.0, mx = 0.0;
        if (!edgeRanges.ToFloat(i, &mn) || !edgeRanges.ToFloat(i + 1, &mx)) {
            _Fail("BA.235", TfStringPrintf("edge:range contains non-numeric "
                                           "values at indices %zu and %zu.",
                                           i, i + 1));
            continue;
        }
        if (mx < mn - _PyNumericalTolerance) {
            size_t brep = 0;
            const std::string ctx
                = (o.edges.size() > 1
                   && _FindBrep(o.edges, static_cast<double>(i / 2), &brep))
                ? TfStringPrintf(" in brep #%zu", brep)
                : std::string();
            _Fail("BA.235", TfStringPrintf(
                "Invalid edge:range order ([%s, %s]) for edge #%zu%s. Ensure "
                "the range is specified as (min, max) where min <= max.",
                edgeRanges.Repr(i).c_str(), edgeRanges.Repr(i + 1).c_str(),
                i / 2, ctx.c_str()));
        }
    }
}

// CheckPrim runs _validate_edgeuse_arrays only when the edgeuse partition is
// non-empty: BA.180 edgeuse array sizes against sum(loop:edgeuseCount),
// BA.190 / BA.195 tokens, BA.205 edgeuse:edgeIndex inside each Brep's edge
// partition and BA.200 edgeuse:nextRadialEUIndex inside its edgeuse partition.
// An unreadable edgeuse:edgeIndex or nextRadialEUIndex is BA.185, an
// unreadable edge:curveType BA.215.
void
_BrepChecker::ValidateEdgeuseArraysIfAny()
{
    const _PyOffsets &o = _Offsets();
    if (o.edgeuses.size() <= 1 || o.edgeuses.back() <= 0) {
        return;
    }
    const double expected = _SumIfAny("loop:edgeuseCount");
    _ValidateArraySizesAndAuthored(
        { "edgeuse:edgeIndex", "edgeuse:orientationType",
          "edgeuse:nextRadialEUIndex", "edgeuse:thisRadialEntryType" },
        "BA.180", &expected, false);
    _ValidateAllowedTokens("edgeuse:orientationType", { "same", "opposite" },
                           "BA.190", "edgeuse");
    _ValidateAllowedTokens("edgeuse:thisRadialEntryType",
                           { "topEntry", "bottomEntry" }, "BA.195", "edgeuse");

    const _PyValue edgeIndex
        = _GetAttrSequence("edgeuse:edgeIndex", "BA.185", "uint[]").first;
    _GetAttrSequence("edge:curveType", "BA.215", "token[]");

    std::vector<double> brepEdgeuseCounts;
    for (size_t b = 0; b + 1 < o.edgeuses.size(); ++b) {
        brepEdgeuseCounts.push_back(
            static_cast<double>(o.edgeuses[b + 1] - o.edgeuses[b]));
    }
    _ValidateIndexingRelationships(brepEdgeuseCounts,
                                   { { "edgeuse:edgeIndex", edgeIndex } },
                                   o.edges, "BA.205");

    const _PyValue nextRadial
        = _GetAttrSequence("edgeuse:nextRadialEUIndex", "BA.185", "uint[]").first;
    _GetAttrSequence("edgeuse:edgeIndex", "BA.185", "uint[]");
    _ValidateIndexingRelationships(
        brepEdgeuseCounts, { { "edgeuse:nextRadialEUIndex", nextRadial } },
        o.edgeuses, "BA.200");
}

// _validate_wireEdge_arrays. With no wire edges declared, every wire-edge
// array must be empty (BA.250). BA.255 all-or-none authorship. With wire
// edges: BA.250 sizes, BA.270 two wireEdge:range entries per wire edge,
// BA.260 tokens, BA.265 wireEdge:vertexIndices per shell block inside the
// vertex partition, and BA.275 each wireEdge:range pair ordered.
//
// BA.265 takes its blocks from shell:wireEdgeCount, one per shell, and
// measures block s against vertex partition s -- a per-Brep partition indexed
// by shell. Python does exactly that, so a shell past the last Brep reports
// "insufficient offsets" here too.
void
_BrepChecker::ValidateWireEdgeArrays()
{
    const double totalWireEdges = _SumIfAny("shell:wireEdgeCount");
    if (totalWireEdges == 0) {
        for (const char *name :
             { "wireEdge:curveType", "wireEdge:range", "wireEdge:vertexIndices" }) {
            const _PyValue values = _Get(name).OrEmpty();
            if (values.Truthy()) {
                std::vector<std::string> parts;
                for (size_t i = 0; i < values.Len(); ++i) {
                    parts.push_back(values.GetElem() == _PyValue::Elem::String
                                        ? "'" + values.Repr(i) + "'"
                                        : values.Repr(i));
                }
                _Fail("BA.250", TfStringPrintf(
                    "Wire edge attribute %s should be empty given "
                    "shell:wireEdgeCount, but contains data: [%s].",
                    name, TfStringJoin(parts, ", ").c_str()));
            }
        }
    }

    const std::vector<std::string> all
        = { "wireEdge:curveType", "wireEdge:vertexIndices", "wireEdge:range" };
    std::vector<std::string> authored, missing;
    for (const std::string &name : all) {
        (_IsAuthored(name) ? authored : missing).push_back(name);
    }
    if (!authored.empty() && !missing.empty()) {
        _Fail("BA.255", TfStringPrintf(
            "Wire edge topology attributes must be authored together or all "
            "omitted. Authored: %s; not authored: %s.",
            _PyNameList(authored).c_str(), _PyNameList(missing).c_str()));
    } else if (totalWireEdges > 0 && !missing.empty()) {
        for (const std::string &name : missing) {
            _Fail("BA.255", TfStringPrintf(
                "%s is not authored in BrepArray but shell:wireEdgeCount "
                "requires %s wire edge(s).",
                name.c_str(),
                _PyValue::NumRepr(totalWireEdges, true).c_str()));
        }
    }

    if (totalWireEdges <= 0) {
        return;
    }
    _ValidateArraySizesAndAuthored({ "wireEdge:curveType",
                                     "wireEdge:vertexIndices" },
                                   "BA.250", &totalWireEdges, false);
    const _PyValue ranges = _Get("wireEdge:range").OrEmpty();
    if (static_cast<double>(ranges.Len()) != totalWireEdges * 2) {
        _Fail("BA.270", TfStringPrintf(
            "Invalid wireEdge:range per-edge structure. Expected exactly 2 "
            "elements per wire edge (%s wire edges x 2 = %s elements), but got "
            "%zu elements.",
            _PyValue::NumRepr(totalWireEdges, true).c_str(),
            _PyValue::NumRepr(totalWireEdges * 2, true).c_str(), ranges.Len()));
    }
    _ValidateAllowedTokens("wireEdge:curveType",
                           { "BrepCurve3dNurbAPI", "BrepCurve3dCircleAPI",
                             "BrepCurve3dLineAPI", "BrepCurve3dEllipseAPI" },
                           "BA.260", "wireEdge");

    std::vector<double> shellCounts;
    const _PyValue wireCounts = _Get("shell:wireEdgeCount");
    if (wireCounts.IsNumbers()) {
        for (size_t i = 0; i < wireCounts.Len(); ++i) {
            shellCounts.push_back(std::trunc(wireCounts.Num(i)));
        }
    }
    const _PyOffsets &o = _Offsets();
    _ValidateIndexingRelationships(
        shellCounts,
        { { "wireEdge:vertexIndices", _Get("wireEdge:vertexIndices") } },
        o.vertices, "BA.265");

    if (!ranges.IsNumbers()) {
        return;
    }
    for (size_t i = 0; i + 1 < ranges.Len(); i += 2) {
        const double mn = ranges.Num(i), mx = ranges.Num(i + 1);
        if (mx < mn - _PyNumericalTolerance) {
            size_t brep = 0;
            const std::string ctx
                = (o.wireedges.size() > 1
                   && _FindBrep(o.wireedges, static_cast<double>(i / 2), &brep))
                ? TfStringPrintf(" in brep #%zu", brep)
                : std::string();
            _Fail("BA.275", TfStringPrintf(
                "Invalid wireEdge:range order ([%s, %s]) for wireEdge #%zu%s. "
                "Ensure the range is specified as (min, max) where min <= max.",
                ranges.Repr(i).c_str(), ranges.Repr(i + 1).c_str(), i / 2,
                ctx.c_str()));
        }
    }
}

// _validate_radial_edgeuse_closure (BA.581): following
// edgeuse:nextRadialEUIndex from each edgeuse of a Brep returns to it within
// as many steps as the Brep has edgeuses.
void
_BrepChecker::ValidateRadialEdgeuseClosure()
{
    const _PyValue nextRadial = _Get("edgeuse:nextRadialEUIndex").OrEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (!nextRadial.Truthy() || numBreps == 0 || !nextRadial.IsNumbers()) {
        return;
    }
    const std::vector<long long> &offsets = _Offsets().edgeuses;
    const long long total = static_cast<long long>(nextRadial.Len());
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= offsets.size()) {
            break;
        }
        const long long euStart = offsets[b], euEnd = offsets[b + 1];
        const long long maxSteps = euEnd - euStart;
        for (long long start = euStart; start < std::min(euEnd, total);
             ++start) {
            long long current = start;
            bool closed = false;
            for (long long step = 0; step < maxSteps; ++step) {
                size_t i = 0;
                if (!_PyIndex(current, nextRadial.Len(), &i)) {
                    break;
                }
                const double next = nextRadial.Num(i);
                if (next >= static_cast<double>(total)) {
                    break;
                }
                if (next == static_cast<double>(start)) {
                    closed = true;
                    break;
                }
                current = static_cast<long long>(next);
            }
            if (!closed) {
                _Fail("BA.581", TfStringPrintf(
                    "Radial chain starting at edgeuse #%lld in brep #%zu does "
                    "not close within %lld steps.",
                    start, b, maxSteps));
            }
        }
    }
}

// _validate_orphan_edges (BA.582): every edge is named by some edgeuse.
void
_BrepChecker::ValidateOrphanEdges()
{
    const _PyValue edgeIndex = _Get("edgeuse:edgeIndex").OrEmpty();
    const size_t totalEdges = _SafeGet("edge:curveType").Len();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (totalEdges == 0 || numBreps == 0) {
        return;
    }
    std::set<double> referenced;
    if (edgeIndex.IsNumbers()) {
        for (size_t i = 0; i < edgeIndex.Len(); ++i) {
            referenced.insert(edgeIndex.Num(i));
        }
    }
    const std::vector<long long> &edgeOffsets = _Offsets().edges;
    for (size_t e = 0; e < totalEdges; ++e) {
        if (referenced.count(static_cast<double>(e))) {
            continue;
        }
        size_t brep = 0;
        const std::string ctx
            = _FindBrep(edgeOffsets, static_cast<double>(e), &brep)
            ? TfStringPrintf(" in brep #%zu", brep)
            : std::string();
        _Fail("BA.582", TfStringPrintf("Edge #%zu%s is not referenced by any "
                                       "edgeuse (orphan edge).",
                                       e, ctx.c_str()));
    }
}

// _validate_vertex_arrays: BA.300 authorship; BA.295 vertex:pointType sized
// one past the highest vertex index edge:vertexIndices names; BA.315 tokens.
void
_BrepChecker::ValidateVertexArrays()
{
    _ValidateAuthorshipOnly({ "vertex:pointType" }, "BA.300");
    const _PyValue pairs = _SafeGet("edge:vertexIndices");
    bool any = false;
    double maxIndex = 0.0;
    if (pairs.IsTuples() && pairs.Dim() >= 2) {
        for (size_t e = 0; e < pairs.Len(); ++e) {
            for (size_t c = 0; c < 2; ++c) {
                const double v = std::trunc(pairs.Tup(e, c));
                if (!any || v > maxIndex) {
                    maxIndex = v;
                    any = true;
                }
            }
        }
    }
    const double expected = any ? maxIndex + 1 : 0.0;
    if (expected > 0) {
        _ValidateArraySizesAndAuthored({ "vertex:pointType" }, "BA.295",
                                       &expected, false);
    } else {
        _ValidateArraySizesAndAuthored({ "vertex:pointType" }, "BA.295",
                                       nullptr, false);
    }
    _ValidateAllowedTokens("vertex:pointType", { "BrepPointAPI" }, "BA.315",
                           "vertex");
}

// _validate_point_position: brep:vertexPoint:point:position holds one point
// per BrepPointAPI vertex (BA.320) and brep:shellPoint:point:position one per
// point shell (BA.325), each checked only when a point is expected or
// positions are authored.
void
_BrepChecker::ValidatePointPosition()
{
    const _PyValue pointTypes = _Get("vertex:pointType").OrEmpty();
    double vertexPoints = 0.0;
    for (size_t i = 0; i < pointTypes.Len(); ++i) {
        vertexPoints += pointTypes.Equals(i, "BrepPointAPI") ? 1 : 0;
    }
    if (vertexPoints > 0
        || _Get("brep:vertexPoint:point:position").OrEmpty().Len() > 0) {
        _ValidateArraySizesAndAuthored({ "brep:vertexPoint:point:position" },
                                       "BA.320", &vertexPoints, true);
    }

    const _PyValue shellPointTypes = _Get("shell:pointType").OrEmpty();
    const _PyValue faceuseCounts = _SafeGet("shell:faceuseCount");
    const _PyValue wireEdgeCounts = _SafeGet("shell:wireEdgeCount");
    double shellPoints = 0.0;
    for (size_t i = 0; i < shellPointTypes.Len(); ++i) {
        long long fu = 0, we = 0;
        if (shellPointTypes.Equals(i, "BrepPointAPI") && i < faceuseCounts.Len()
            && i < wireEdgeCounts.Len() && faceuseCounts.ToInt(i, &fu)
            && wireEdgeCounts.ToInt(i, &we) && fu == 0 && we == 0) {
            shellPoints += 1;
        }
    }
    if (shellPoints > 0
        || _Get("brep:shellPoint:point:position").OrEmpty().Len() > 0) {
        _ValidateArraySizesAndAuthored({ "brep:shellPoint:point:position" },
                                       "BA.325", &shellPoints, true);
    }
}

// _validate_topology_geometry_correspondence: BA.320 each Brep's BrepPointAPI
// vertices have positions left to take, and BA.225 each edge of a Brep's edge
// partition names vertices inside the Brep's vertex partition. An unreadable
// vertex:pointType is BA.316, positions BA.326, edge:vertexIndices BA.237
// (which ends the rule).
void
_BrepChecker::ValidateTopologyGeometryCorrespondence()
{
    const _PyOffsets &o = _Offsets();
    _GetAttrSequence("vertex:pointType", "BA.316", "int[]");
    const _PyValue positions = _GetAttrSequence(
        "brep:vertexPoint:point:position", "BA.326", "point3d[]").first;
    const _PyValue pointTypes = _Get("vertex:pointType").OrEmpty();

    size_t positionIdx = 0;
    for (size_t b = 0; b + 1 < o.vertices.size(); ++b) {
        size_t expected = 0;
        for (long long v = o.vertices[b]; v < o.vertices[b + 1]; ++v) {
            if (v >= 0 && v < static_cast<long long>(pointTypes.Len())
                && pointTypes.Equals(static_cast<size_t>(v), "BrepPointAPI")) {
                ++expected;
            }
        }
        if (expected > 0 && positions.Truthy()) {
            const long long available = static_cast<long long>(positions.Len())
                - static_cast<long long>(positionIdx);
            if (available < static_cast<long long>(expected)) {
                _Fail("BA.320", TfStringPrintf(
                    "Mismatch between vertex topology (%zu BrepPointAPI "
                    "vertices) and geometry (%lld remaining positions) for "
                    "brep #%zu.",
                    expected, available, b));
            }
            positionIdx += expected;
        }
    }

    const _PyValue &rawPairs = _Get("edge:vertexIndices");
    if (rawPairs.IsUnregistered()) {
        _Fail("BA.237", "edge:vertexIndices has an unregistered USD type; "
                        "expected int2[].");
        return;
    }
    const _PyValue pairs = rawPairs.OrEmpty();
    if (!pairs.Truthy() || !pointTypes.Truthy() || !pairs.IsTuples()) {
        return;
    }
    const size_t n = o.edges.size();
    for (size_t b = 0; b + 1 < n; ++b) {
        const long long vStart = b < o.vertices.size() ? o.vertices[b] : 0;
        const long long vEnd = b + 1 < o.vertices.size()
            ? o.vertices[b + 1]
            : static_cast<long long>(pointTypes.Len());
        const double minIndex = static_cast<double>(vStart);
        const double maxIndex = static_cast<double>(vEnd - 1);
        for (long long e = o.edges[b]; e < o.edges[b + 1]; ++e) {
            if (e < 0 || e >= static_cast<long long>(pairs.Len())) {
                continue;
            }
            for (size_t c = 0; c < pairs.Dim(); ++c) {
                const double v = pairs.Tup(static_cast<size_t>(e), c);
                if (v < minIndex || v > maxIndex) {
                    _Fail("BA.225", TfStringPrintf(
                        "Edge #%lld in brep #%zu references invalid vertex "
                        "index %s. Valid range for this brep: [%lld, %lld].",
                        e - o.edges[b], b,
                        _PyValue::NumRepr(v, pairs.Integral()).c_str(), vStart,
                        vEnd - 1));
                }
            }
        }
    }
}

// _validate_attribute_data_types: the authored type of each topology and
// point attribute (BA.061 ... BA.327) and of each NURBS attribute (BA.371,
// BA.416, BA.471).
void
_BrepChecker::ValidateAttributeDataTypes()
{
    _ValidateStratumDataTypes({ { "brep:intersectTol3d", "double[]" },
                                { "brep:extent", "double3[]" },
                                { "brep:regionCount", "uint[]" } },
                              "BA.061");
    _ValidateStratumDataTypes({ { "region:shellCount", "uint[]" },
                                { "region:type", "token[]" } },
                              "BA.076");
    _ValidateStratumDataTypes({ { "shell:faceuseCount", "uint[]" },
                                { "shell:wireEdgeCount", "uint[]" },
                                { "shell:pointType", "token[]" } },
                              "BA.091");
    _ValidateStratumDataTypes({ { "faceuse:faceIndex", "uint[]" },
                                { "faceuse:orientationType", "token[]" } },
                              "BA.116");
    _ValidateStratumDataTypes({ { "face:loopCount", "uint[]" },
                                { "face:trimType", "token[]" },
                                { "face:surfaceType", "token[]" },
                                { "face:range", "double2[]" } },
                              "BA.161");
    _ValidateStratumDataTypes({ { "loop:edgeuseCount", "uint[]" },
                                { "loop:vertexIndex", "uint[]" } },
                              "BA.176");
    _ValidateStratumDataTypes({ { "edgeuse:edgeIndex", "uint[]" },
                                { "edgeuse:orientationType", "token[]" },
                                { "edgeuse:nextRadialEUIndex", "uint[]" },
                                { "edgeuse:thisRadialEntryType", "token[]" } },
                              "BA.196");
    _ValidateStratumDataTypes({ { "edge:curveType", "token[]" },
                                { "edge:vertexIndices", "int2[]" },
                                { "edge:range", "double[]" } },
                              "BA.237");
    _ValidateStratumDataTypes({ { "wireEdge:curveType", "token[]" },
                                { "wireEdge:vertexIndices", "int2[]" },
                                { "wireEdge:range", "double[]" } },
                              "BA.291");
    _ValidateStratumDataTypes({ { "vertex:pointType", "token[]" } }, "BA.316");
    _ValidateStratumDataTypes(
        { { "brep:vertexPoint:point:position", "point3d[]" } }, "BA.326");
    _ValidateStratumDataTypes(
        { { "brep:shellPoint:point:position", "point3d[]" } }, "BA.327");
    _ValidateStratumDataTypes(
        { { "brep:edge3dNurb:curve3d:nurb:order", "uint[]" },
          { "brep:edge3dNurb:curve3d:nurb:vertexCount", "uint[]" },
          { "brep:edge3dNurb:curve3d:nurb:controlVertices", "point3d[]" },
          { "brep:edge3dNurb:curve3d:nurb:weights", "double[]" },
          { "brep:edge3dNurb:curve3d:nurb:knots", "double[]" } },
        "BA.371");
    _ValidateStratumDataTypes(
        { { "brep:curveUv:nurb:order", "uint[]" },
          { "brep:curveUv:nurb:vertexCount", "uint[]" },
          { "brep:curveUv:nurb:controlVertices", "double2[]" },
          { "brep:curveUv:nurb:weights", "double[]" },
          { "brep:curveUv:nurb:knots", "double[]" } },
        "BA.416");
    _ValidateStratumDataTypes(
        { { "brep:surface:nurb:uOrder", "uint[]" },
          { "brep:surface:nurb:vOrder", "uint[]" },
          { "brep:surface:nurb:uVertexCount", "uint[]" },
          { "brep:surface:nurb:vVertexCount", "uint[]" },
          { "brep:surface:nurb:controlVertices", "point3d[]" },
          { "brep:surface:nurb:weights", "double[]" },
          { "brep:surface:nurb:uKnots", "double[]" },
          { "brep:surface:nurb:vKnots", "double[]" } },
        "BA.471");
}

// _validate_minimum_topology_counts: BA.700 every brep:regionCount entry is at
// least 1, BA.701 every region:shellCount entry, and BA.702 (a warning) every
// shell has faceuses, wire edges, or a BrepPointAPI point. Entries that do
// not coerce to integers are skipped (BA.091 reports them).
void
_BrepChecker::ValidateMinimumTopologyCounts()
{
    const _PyValue regionCounts = _SafeGet("brep:regionCount");
    for (size_t i = 0; i < regionCounts.Len(); ++i) {
        long long v = 0;
        if (regionCounts.ToInt(i, &v) && v < 1) {
            _Fail("BA.700", TfStringPrintf("brep:regionCount[%zu] = %s is less "
                                           "than 1.",
                                           i, regionCounts.Repr(i).c_str()));
        }
    }
    const _PyValue shellCounts = _SafeGet("region:shellCount");
    for (size_t i = 0; i < shellCounts.Len(); ++i) {
        long long v = 0;
        if (shellCounts.ToInt(i, &v) && v < 1) {
            _Fail("BA.701", TfStringPrintf("region:shellCount[%zu] = %s is less "
                                           "than 1.",
                                           i, shellCounts.Repr(i).c_str()));
        }
    }
    const _PyValue faceuseCounts = _SafeGet("shell:faceuseCount");
    const _PyValue wireEdgeCounts = _SafeGet("shell:wireEdgeCount");
    const _PyValue pointTypes = _SafeGet("shell:pointType");
    if (faceuseCounts.Len() == 0 || wireEdgeCounts.Len() == 0) {
        return;
    }
    const size_t numShells
        = std::min(faceuseCounts.Len(), wireEdgeCounts.Len());
    for (size_t s = 0; s < numShells; ++s) {
        long long fu = 0, we = 0;
        if (!faceuseCounts.ToInt(s, &fu) || !wireEdgeCounts.ToInt(s, &we)) {
            continue;
        }
        const std::string pt
            = s < pointTypes.Len() ? pointTypes.Repr(s) : std::string("none");
        if (fu == 0 && we == 0 && pt != "BrepPointAPI") {
            _Warn("BA.702", TfStringPrintf("shell #%zu has no content: "
                                           "faceuseCount=0, wireEdgeCount=0, "
                                           "pointType='%s'.",
                                           s, pt.c_str()));
        }
    }
}

// _validate_type_count_exhaustive: every edge:curveType (BA.720),
// wireEdge:curveType (BA.721) and face:surfaceType (BA.722) entry names a
// recognized type.
void
_BrepChecker::ValidateTypeCountExhaustive()
{
    const std::vector<std::string> curveTypes
        = { "BrepCurve3dNurbAPI", "BrepCurve3dCircleAPI", "BrepCurve3dLineAPI",
            "BrepCurve3dEllipseAPI" };
    const std::vector<std::string> surfaceTypes
        = { "BrepSurfaceNurbAPI", "BrepSurfaceSphereAPI", "BrepSurfacePlaneAPI",
            "BrepSurfaceCylinderAPI", "BrepSurfaceConeAPI",
            "BrepSurfaceTorusAPI" };
    struct Item
    {
        const char *attr;
        const std::vector<std::string> *types;
        const char *rule;
        const char *label;
        const char *plural;
    };
    for (const Item &it :
         { Item{ "edge:curveType", &curveTypes, "BA.720", "Edge curveType",
                 "edge" },
           Item{ "wireEdge:curveType", &curveTypes, "BA.721",
                 "WireEdge curveType", "wireEdge" },
           Item{ "face:surfaceType", &surfaceTypes, "BA.722",
                 "Face surfaceType", "face" } }) {
        const _PyValue values = _SafeGet(it.attr);
        if (values.Len() == 0) {
            continue;
        }
        size_t recognized = 0;
        for (size_t i = 0; i < values.Len(); ++i) {
            for (const std::string &t : *it.types) {
                if (values.Repr(i) == t) {
                    ++recognized;
                    break;
                }
            }
        }
        if (recognized != values.Len()) {
            _Fail(it.rule, TfStringPrintf(
                "%s recognized count (%zu) != total %s count (%zu). %zu %ss "
                "have unrecognized types.",
                it.label, recognized, it.plural, values.Len(),
                values.Len() - recognized, it.plural));
        }
    }
}

// _validate_radial_chain_consistency (BA.670): within each Brep,
// edgeuse:nextRadialEUIndex stays inside the Brep's edgeuse partition, every
// radial chain names one edge, and every edge's edgeuses fall in one chain.
// The first violation ends the rule.
void
_BrepChecker::ValidateRadialChainConsistency()
{
    const _PyValue nextRadial = _SafeGet("edgeuse:nextRadialEUIndex");
    const _PyValue edgeIndex = _SafeGet("edgeuse:edgeIndex");
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (nextRadial.Len() == 0 || edgeIndex.Len() == 0 || numBreps == 0) {
        return;
    }
    const size_t total = nextRadial.Len();
    if (edgeIndex.Len() != total) {
        return;
    }
    const std::vector<long long> &offsets = _Offsets().edgeuses;
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= offsets.size()) {
            break;
        }
        const long long euStart = offsets[b], euEnd = offsets[b + 1];
        if (euStart >= euEnd) {
            continue;
        }
        const long long scanEnd = std::min(euEnd, static_cast<long long>(total));
        for (long long eu = euStart; eu < scanEnd; ++eu) {
            size_t i = 0;
            if (!_PyIndex(eu, total, &i)) {
                continue;
            }
            long long next = 0;
            if (!nextRadial.ToInt(i, &next)) {
                _Fail("BA.670", TfStringPrintf(
                    "edgeuse:nextRadialEUIndex at edgeuse #%lld in brep #%zu "
                    "is not a numeric index: '%s'.",
                    eu, b, nextRadial.Repr(i).c_str()));
                return;
            }
            if (next < euStart || next >= euEnd) {
                _Fail("BA.670", TfStringPrintf(
                    "edgeuse:nextRadialEUIndex at edgeuse #%lld in brep #%zu "
                    "references edgeuse #%lld, which is outside this brep's "
                    "edgeuse range [%lld, %lld).",
                    eu, b, next, euStart, euEnd));
                return;
            }
        }

        // _decompose_radial_cycles: walk from each unassigned edgeuse until
        // the walk reaches an assigned one.
        std::vector<std::vector<long long>> cycles;
        std::unordered_map<long long, size_t> cycleOf;
        for (long long start = euStart; start < scanEnd; ++start) {
            if (cycleOf.count(start)) {
                continue;
            }
            std::vector<long long> cycle;
            long long current = start;
            while (!cycleOf.count(current)) {
                if (current < 0 || current >= static_cast<long long>(total)) {
                    break;
                }
                cycle.push_back(current);
                cycleOf[current] = cycles.size();
                long long next = 0;
                if (!nextRadial.ToInt(static_cast<size_t>(current), &next)) {
                    break;
                }
                current = next;
            }
            cycles.push_back(std::move(cycle));
        }

        const auto edgeOf = [&](long long eu) {
            long long e = 0;
            edgeIndex.ToInt(static_cast<size_t>(eu), &e);
            return e;
        };
        const auto listRepr = [](const std::vector<long long> &values) {
            std::vector<std::string> parts;
            for (const long long v : values) {
                parts.push_back(TfStringPrintf("%lld", v));
            }
            return "[" + TfStringJoin(parts, ", ") + "]";
        };
        for (const std::vector<long long> &cycle : cycles) {
            std::set<long long> edges;
            for (const long long eu : cycle) {
                edges.insert(edgeOf(eu));
            }
            if (edges.size() != 1) {
                _Fail("BA.670", TfStringPrintf(
                    "Radial chain %s in brep #%zu references multiple edges "
                    "via edgeuse:edgeIndex (%s); all edgeuses in a radial "
                    "chain must share the same edge.",
                    listRepr(cycle).c_str(), b,
                    listRepr(std::vector<long long>(edges.begin(), edges.end()))
                        .c_str()));
                return;
            }
        }

        // First-seen edge order, as Python's dict keeps insertion order.
        std::vector<long long> edgeOrder;
        std::unordered_map<long long, std::set<size_t>> cyclesOfEdge;
        for (long long eu = euStart; eu < scanEnd; ++eu) {
            const long long e = edgeOf(eu);
            auto it = cyclesOfEdge.find(e);
            if (it == cyclesOfEdge.end()) {
                edgeOrder.push_back(e);
                it = cyclesOfEdge.emplace(e, std::set<size_t>()).first;
            }
            it->second.insert(cycleOf[eu]);
        }
        for (const long long e : edgeOrder) {
            const std::set<size_t> &ids = cyclesOfEdge[e];
            if (ids.size() <= 1) {
                continue;
            }
            std::vector<long long> edgeuses;
            for (long long eu = euStart; eu < scanEnd; ++eu) {
                if (edgeOf(eu) == e) {
                    edgeuses.push_back(eu);
                }
            }
            _Fail("BA.670", TfStringPrintf(
                "Edge #%lld in brep #%zu is referenced by edgeuses %s, but they "
                "fall into %zu separate radial chains "
                "(edgeuse:nextRadialEUIndex). All edgeuses of an edge must "
                "belong to one closed radial chain.",
                e, b, listRepr(edgeuses).c_str(), ids.size()));
            return;
        }
    }
}

// -------------------------------------------------------------------------- //
// BrepArrayStructure                                                         //
// -------------------------------------------------------------------------- //
// Brep-level attributes and minimum counts: BA.000 / BA.005 / BA.010, the
// brep:extent structure and ordering (BA.020 / BA.025 / BA.030 / BA.035), the
// point position sizes (BA.320 / BA.325) and the minimum topology counts
// (BA.700 / BA.701 / BA.702).
UsdValidationErrorVector
_BrepArrayStructure(const UsdPrim &usdPrim,
                    const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.000", "BA.005", "BA.010", "BA.020", "BA.025",
                              "BA.030", "BA.035", "BA.320", "BA.325", "BA.700",
                              "BA.701", "BA.702" });
    c.ValidateBrepExtent();
    c.ValidateBrepTols();
    c.ValidateBrepArray();
    c.ValidatePointPosition();
    c.ValidateTopologyGeometryCorrespondence();
    c.ValidateMinimumTopologyCounts();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayTopology                                                          //
// -------------------------------------------------------------------------- //
// The sizes of the flat-packed topology arrays, each stratum against the
// counts above it (BA.065, 080, 100, 120, 150, 165, 180, 210, 230, 250, 270,
// 295), and the radial chain structure (BA.670).
UsdValidationErrorVector
_BrepArrayTopology(const UsdPrim &usdPrim,
                   const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.065", "BA.080", "BA.100", "BA.120", "BA.150",
                              "BA.165", "BA.180", "BA.210", "BA.230", "BA.250",
                              "BA.270", "BA.295", "BA.670" });
    c.ValidateRegionArrays();
    c.ValidateShellArrays();
    c.ValidateFaceuseArrays();
    c.ValidateFaceArrays();
    c.ValidateLoopArrays();
    c.ValidateEdgeArrays();
    c.ValidateEdgeuseArraysIfAny();
    c.ValidateWireEdgeArrays();
    c.ValidateVertexArrays();
    c.ValidateRadialChainConsistency();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayTokenValues                                                       //
// -------------------------------------------------------------------------- //
// Every token[] topology attribute draws its entries from its allowed set
// (BA.075, 090, 110, 130, 135, 190, 195, 245, 260, 315), one finding per
// attribute naming every offending index.
UsdValidationErrorVector
_BrepArrayTokenValues(const UsdPrim &usdPrim,
                      const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.075", "BA.090", "BA.110", "BA.130", "BA.135",
                              "BA.190", "BA.195", "BA.245", "BA.260",
                              "BA.315" });
    c.ValidateRegionArrays();
    c.ValidateShellArrays();
    c.ValidateFaceuseArrays();
    c.ValidateFaceArrays();
    c.ValidateEdgeArrays();
    c.ValidateEdgeuseArraysIfAny();
    c.ValidateWireEdgeArrays();
    c.ValidateVertexArrays();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayAuthorship                                                        //
// -------------------------------------------------------------------------- //
// Every topology attribute is authored -- any opinion counts, a declaration
// with no value included -- whether or not its family has members (BA.070,
// 085, 105, 125, 170, 185, 215, 300); the wire-edge family is all-or-none
// (BA.255). BA.185 / BA.215 also report an edgeuse:edgeIndex,
// edgeuse:nextRadialEUIndex or edge:curveType that cannot be read as an
// array, as Python does.
UsdValidationErrorVector
_BrepArrayAuthorship(const UsdPrim &usdPrim,
                     const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.070", "BA.085", "BA.105", "BA.125", "BA.170",
                              "BA.185", "BA.215", "BA.255", "BA.300" });
    c.ValidateRegionArrays();
    c.ValidateShellArrays();
    c.ValidateFaceuseArrays();
    c.ValidateFaceArrays();
    c.ValidateLoopArrays();
    c.ValidateEdgeAndEdgeuseAuthorship();
    c.ValidateEdgeuseArraysIfAny();
    c.ValidateWireEdgeArrays();
    c.ValidateVertexArrays();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayDataTypes                                                         //
// -------------------------------------------------------------------------- //
// Each topology attribute and point position is authored with its schema
// type (BA.061, 076, 091, 116, 161, 176, 196, 237, 291, 316, 326, 327).
// BA.091 also reports count arrays that do not coerce to integers, and BA.237
// / BA.316 / BA.326 values Python cannot read as arrays.
UsdValidationErrorVector
_BrepArrayDataTypes(const UsdPrim &usdPrim,
                    const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.061", "BA.076", "BA.091", "BA.116", "BA.161",
                              "BA.176", "BA.196", "BA.237", "BA.291", "BA.316",
                              "BA.326", "BA.327" });
    c.ComputeBrepOffsets();
    c.ValidateFaceuseArrays();
    c.ValidateEdgeArrays();
    c.ValidateTopologyGeometryCorrespondence();
    c.ValidateAttributeDataTypes();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayReferences                                                        //
// -------------------------------------------------------------------------- //
// Cross-reference indices land inside the owning Brep's partition:
// faceuse->face (BA.115), loop->vertex (BA.175), edgeuse->edge (BA.205),
// edgeuse->edgeuse radial (BA.200), edge->vertex (BA.225) and
// wireEdge->vertex (BA.265).
UsdValidationErrorVector
_BrepArrayReferences(const UsdPrim &usdPrim,
                     const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.115", "BA.175", "BA.200", "BA.205", "BA.225",
                              "BA.265" });
    c.ValidateFaceuseArrays();
    c.ValidateLoopVertexIndex();
    c.ValidateEdgeArrays();
    c.ValidateEdgeuseArraysIfAny();
    c.ValidateWireEdgeArrays();
    c.ValidateTopologyGeometryCorrespondence();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayCompleteness                                                      //
// -------------------------------------------------------------------------- //
// Faceuse pairing (BA.580), radial chain closure (BA.581), orphan edges
// (BA.582) and exhaustive curve / surface types (BA.720, BA.721, BA.722).
UsdValidationErrorVector
_BrepArrayCompleteness(const UsdPrim &usdPrim,
                       const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.580", "BA.581", "BA.582", "BA.720", "BA.721",
                              "BA.722" });
    c.ValidateFaceusePairing();
    c.ValidateRadialEdgeuseClosure();
    c.ValidateOrphanEdges();
    c.ValidateTypeCountExhaustive();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayGeomSubsets                                                       //
// -------------------------------------------------------------------------- //
// BA.680 / BA.681 / BA.682 are the only rules in this file whose subject is a
// prim other than the BrepArray. A UsdGeomSubset child partitions the
// BrepArray's Breps (elementType "brep") or its faces (elementType "face") so
// a material can be bound to part of the prim. The three rules check that the
// partition indexes something that exists, that two subsets of one elementType
// do not claim the same element, and that a bound material is on the stage.
// Findings are reported at the BrepArray, which is what the Python
// brep_validator does and what keeps them visible to a prim-gated harness.
UsdValidationErrorVector
_BrepArrayGeomSubsets(const UsdPrim &usdPrim,
                      const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return UsdValidationErrorVector();
    }
    const UsdSolidBrepArray brep(usdPrim);
    const UsdStageWeakPtr stage = usdPrim.GetStage();

    UsdValidationErrorVector errors;

    // Upper bounds for the two element types. face:surfaceType and
    // face:loopCount both have one entry per face; Python falls back to the
    // second when the first is absent so that a BrepArray missing its surface
    // types still bounds a face partition.
    const size_t numBreps
        = _Read<unsigned int>(brep.GetBrepRegionCountAttr()).size();
    size_t numFaces = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr()).size();
    if (numFaces == 0) {
        numFaces = _Read<unsigned int>(brep.GetFaceLoopCountAttr()).size();
    }

    static const TfToken geomSubsetType("GeomSubset");
    static const TfToken elementTypeName("elementType");
    static const TfToken indicesName("indices");
    static const TfToken materialBindingName("material:binding");
    static const TfToken brepElement("brep");
    static const TfToken faceElement("face");

    // Which subset first claimed each index, per elementType. The maps span
    // all children rather than being rebuilt per child, because BA.681 names
    // the earlier claimant of a repeated index.
    std::unordered_map<int, std::string> brepClaimed;
    std::unordered_map<int, std::string> faceClaimed;

    for (const UsdPrim &child : usdPrim.GetAllChildren()) {
        if (child.GetTypeName() != geomSubsetType) {
            continue;
        }
        const UsdAttribute elementTypeAttr
            = child.GetAttribute(elementTypeName);
        if (!elementTypeAttr) {
            continue;
        }
        TfToken elementType;
        elementTypeAttr.Get(&elementType);
        if (elementType != brepElement && elementType != faceElement) {
            continue;
        }
        const UsdAttribute indicesAttr = child.GetAttribute(indicesName);
        if (!indicesAttr) {
            continue;
        }
        VtArray<int> indices;
        if (!indicesAttr.Get(&indices)) {
            continue;
        }

        const bool isBrepSubset = (elementType == brepElement);
        const size_t upperBound = isBrepSubset ? numBreps : numFaces;
        std::unordered_map<int, std::string> &claimed
            = isBrepSubset ? brepClaimed : faceClaimed;
        const std::string childName = child.GetName().GetString();

        // BA.680: indices address [0, upperBound). An upperBound of zero means
        // the BrepArray authors neither count array, so there is nothing to
        // bound the partition against and the range test is skipped. Python
        // reports the first offending index per subset, not every one.
        for (const int index : indices) {
            if (upperBound > 0
                && (index < 0
                    || static_cast<size_t>(index) >= upperBound)) {
                errors.emplace_back(
                    UsdSolidValidationErrorNameTokens
                        ->geomSubsetIndexOutOfRange,
                    UsdValidationErrorType::Error, _PrimSites(usdPrim),
                    TfStringPrintf(
                        "[BA.680] BrepArray <%s>: GeomSubset '%s' has %s index "
                        "%d outside valid range [0, %zu).",
                        usdPrim.GetPath().GetText(), childName.c_str(),
                        elementType.GetText(), index, upperBound));
                break;
            }
        }

        // BA.681: within one elementType an index belongs to at most one
        // subset. Reporting stops at the first repeat in a subset, and the
        // indices after it are left unclaimed, so a later subset that repeats
        // one of them is measured against the first subset that recorded it.
        for (const int index : indices) {
            const auto claim = claimed.find(index);
            if (claim != claimed.end()) {
                errors.emplace_back(
                    UsdSolidValidationErrorNameTokens
                        ->geomSubsetIndicesOverlap,
                    UsdValidationErrorType::Error, _PrimSites(usdPrim),
                    TfStringPrintf(
                        "[BA.681] BrepArray <%s>: GeomSubset '%s': %s index %d "
                        "also appears in subset '%s'.",
                        usdPrim.GetPath().GetText(), childName.c_str(),
                        elementType.GetText(), index,
                        claim->second.c_str()));
                break;
            }
            claimed[index] = childName;
        }

        // BA.682: every material:binding target names a prim on the stage.
        const UsdRelationship materialBinding
            = child.GetRelationship(materialBindingName);
        if (materialBinding) {
            SdfPathVector targets;
            materialBinding.GetTargets(&targets);
            for (const SdfPath &target : targets) {
                if (!stage->GetPrimAtPath(target)) {
                    errors.emplace_back(
                        UsdSolidValidationErrorNameTokens
                            ->geomSubsetMaterialBindingTargetMissing,
                        UsdValidationErrorType::Error, _PrimSites(usdPrim),
                        TfStringPrintf(
                            "[BA.682] BrepArray <%s>: GeomSubset '%s' "
                            "material:binding target '%s' does not exist on "
                            "stage.",
                            usdPrim.GetPath().GetText(), childName.c_str(),
                            target.GetText()));
                }
            }
        }
    }

    return errors;
}

// Rules ported from tools/brep_validator/brep_validator.py whose
// implementations sit further down the file, next to the per-Brep offset
// partition and the tolerance helpers they need. BrepArrayTopology,
// BrepArrayRanges and BrepArrayEdgeCurveVertices, defined below, report them.
void _CheckAngularRangePrimaryPeriod(const UsdPrim &usdPrim,
                                     const UsdSolidBrepArray &brep,
                                     UsdValidationErrorVector *errors);
void _CheckFaceVDomainOrdering(const UsdPrim &usdPrim,
                               const UsdSolidBrepArray &brep,
                               UsdValidationErrorVector *errors);
void _CheckFloatArraysFinite(const UsdPrim &usdPrim,
                             UsdValidationErrorVector *errors);
void _CheckNurbsEdgeEndpointVertices(const UsdPrim &usdPrim,
                                     const UsdSolidBrepArray &brep,
                                     UsdValidationErrorVector *errors);

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

    // BA.140 face loop counts, BA.145 / BA.155 / BA.160 face:range structure
    // and intervals, BA.235 / BA.275 edge and wire-edge range ordering.
    _BrepChecker c(usdPrim,
                   { "BA.140", "BA.145", "BA.155", "BA.160", "BA.235", "BA.275" });
    c.ValidateFaceLoopCountMinimum();
    c.ValidateFaceRanges();
    c.ValidateEdgeArrays();
    c.ValidateWireEdgeArrays();
    UsdValidationErrorVector errors = c.TakeErrors();

    // BA.630 / BA.631: angular parameter maxima stay in the primary period.
    _CheckAngularRangePrimaryPeriod(usdPrim, brep, &errors);

    // BA.640: cylinder and cone faces have an ordered V domain.
    _CheckFaceVDomainOrdering(usdPrim, brep, &errors);

    // BA.660: no floating-point array holds a NaN or an Inf.
    _CheckFloatArraysFinite(usdPrim, &errors);

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
// std::numeric_limits<float>::epsilon() ~ 1.19e-7; a float32 value carries
// up to ~0.5 ulp of quantization, i.e. ~0.6e-7 * magnitude. Extent-
// containment slop adds this so it tracks quantization at large
// coordinates (BA.310/365/465/657).
constexpr double _ExtentFloatRel = 0.6e-7;
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
    // Single-Brep shortcut for the edgeuse partition, matching
    // brep_validator.py, which sizes it as sum(loop:edgeuseCount) over the
    // whole authored array rather than over the derived loop range.
    //
    // Only this stratum. Python takes the shortcut per stratum, not uniformly:
    // the face partition BA.115 validates against stays derived, and widening
    // that one too puts every faceuse index back in range on a prim whose
    // brep:regionCount is zero -- the case Python reports.
    //
    // The two agree on well-formed data. Where the counts disagree with each
    // other they do not: a face:loopCount of zero shrinks the derived loop
    // range, which shrinks the edgeuse range under it, and BA.200 then reports
    // radial indices as outside a range Python never narrowed.
    if (n == 1) {
        const auto total = [](const VtArray<unsigned int> &a) {
            size_t s = 0;
            for (unsigned int v : a) {
                s += v;
            }
            return s;
        };
        o.edgeuse[1] = total(loopEdgeuseCount);
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
    const VtArray<TfToken> edgeCurveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const VtArray<TfToken> wireEdgeCurveType
        = _Read<TfToken>(brep.GetWireEdgeCurveTypeAttr());

    enum class _Driver { Face, Vertex, Edge, WireEdge };
    struct Item {
        const char *schemaToken;  // GetAppliedSchemas() membership token
        const char *driverValue;  // value to count in the driver token array
        const char *presenceAttr; // attribute whose authorship proves data
        const char *ba;
        const char *label;
        _Driver driver;
    };
    static const std::vector<Item> items = {
        { "BrepPointAPI:vertexPoint", "BrepPointAPI",
          "brep:vertexPoint:point:position", "BA.305", "vertexPoint",
          _Driver::Vertex },
        { "BrepSurfaceSphereAPI", "BrepSurfaceSphereAPI",
          "brep:surface:sphere:center", "BA.485", "sphere", _Driver::Face },
        { "BrepSurfacePlaneAPI", "BrepSurfacePlaneAPI",
          "brep:surface:plane:origin", "BA.495", "plane", _Driver::Face },
        { "BrepSurfaceCylinderAPI", "BrepSurfaceCylinderAPI",
          "brep:surface:cylinder:origin", "BA.505", "cylinder", _Driver::Face },
        { "BrepSurfaceConeAPI", "BrepSurfaceConeAPI", "brep:surface:cone:origin",
          "BA.516", "cone", _Driver::Face },
        { "BrepSurfaceTorusAPI", "BrepSurfaceTorusAPI",
          "brep:surface:torus:origin", "BA.526", "torus", _Driver::Face },
        // The curve families. brep_validator.py counts these in
        // edge:curveType and wireEdge:curveType; without them a prim that
        // declares a NURBS wire edge and applies no
        // BrepCurve3dNurbAPI:wireEdge3dNurb satisfied BA.583 vacuously.
        { "BrepCurve3dNurbAPI:edge3dNurb", "BrepCurve3dNurbAPI",
          "brep:edge3dNurb:curve3d:nurb:controlVertices", "BA.583",
          "edge3dNurb", _Driver::Edge },
        { "BrepCurve3dNurbAPI:wireEdge3dNurb", "BrepCurve3dNurbAPI",
          "brep:wireEdge3dNurb:curve3d:nurb:controlVertices", "BA.583",
          "wireEdge3dNurb", _Driver::WireEdge },
    };
    UsdValidationErrorVector errors;
    for (const Item &it : items) {
        const VtArray<TfToken> &driver
            = it.driver == _Driver::Vertex     ? vertexPointType
            : it.driver == _Driver::Edge       ? edgeCurveType
            : it.driver == _Driver::WireEdge   ? wireEdgeCurveType
                                               : faceSurfaceType;
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
                                it.driver == _Driver::Vertex
                                    ? "vertex:pointType"
                                    : it.driver == _Driver::Edge
                                        ? "edge:curveType"
                                        : it.driver == _Driver::WireEdge
                                            ? "wireEdge:curveType"
                                            : "face:surfaceType",
                                count, it.driverValue, it.schemaToken));
        }
    }

    // BA.583 for shell points. shell:pointType names a point only on a point
    // shell (_IsBrepPointShell), so a "BrepPointAPI" token on a face or wire
    // shell neither declares a point nor requires BrepPointAPI:shellPoint.
    {
        const VtArray<TfToken> shellPointType
            = _Read<TfToken>(brep.GetShellPointTypeAttr());
        const VtArray<unsigned int> shellFaceuseCount
            = _Read<unsigned int>(brep.GetShellFaceuseCountAttr());
        const VtArray<unsigned int> shellWireEdgeCount
            = _Read<unsigned int>(brep.GetShellWireEdgeCountAttr());
        size_t pointShells = 0;
        for (size_t i = 0; i < shellPointType.size(); ++i) {
            if (_IsBrepPointShell(i, shellPointType, shellFaceuseCount,
                                  shellWireEdgeCount)) {
                ++pointShells;
            }
        }
        if (pointShells > 0
            && !_HasAppliedSchema(usdPrim,
                                  TfToken("BrepPointAPI:shellPoint"))) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->schemaUsageInconsistent,
                 usdPrim,
                 TfStringPrintf("[BA.583] BrepArray <%s>: shell:pointType "
                                "contains %zu 'BrepPointAPI' occurrence(s), "
                                "but required applied geometry API "
                                "'BrepPointAPI:shellPoint' is absent from "
                                "apiSchemas.",
                                usdPrim.GetPath().GetText(), pointShells));
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
// BrepArrayEdgeCurveVertices                                                 //
// -------------------------------------------------------------------------- //
// BA.730: a NURBS edge evaluated at its authored edge:range endpoints must land
// on the vertices its edge:vertexIndices name, within its Brep's
// brep:intersectTol3d (_CheckNurbsEdgeEndpointVertices, below).
UsdValidationErrorVector
_BrepArrayEdgeCurveVertices(const UsdPrim &usdPrim,
                            const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    const UsdSolidBrepArray brep(usdPrim);
    UsdValidationErrorVector errors;
    _CheckNurbsEdgeEndpointVertices(usdPrim, brep, &errors);
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

// ========================================================================== //
// Deferred rules ported from tools/brep_validator/brep_validator.py:         //
// BA.620, BA.630, BA.631, BA.640, BA.660, BA.670, BA.710, BA.730.            //
//                                                                            //
// They are defined here, rather than inline in the validators that report    //
// them, because they need _ComputeOffsets, _Err, _ReadName and _FloatClose,  //
// all declared above this point. Three of the validators that call them      //
// (BrepArrayTopology, BrepArrayRanges, BrepArrayEdgeCurveVertices) are       //
// defined earlier in the file and reach them through the forward             //
// declarations above BrepArrayTopology.                                      //
// ========================================================================== //

// Renders a Python list literal ("[0, 1, 2]") so a ported message reads the
// same as the brep_validator.py message it came from.
template <class T>
std::string
_FormatIndexList(const std::vector<T> &values)
{
    std::vector<std::string> parts;
    parts.reserve(values.size());
    for (const T v : values) {
        parts.push_back(
            TfStringPrintf("%llu", static_cast<unsigned long long>(v)));
    }
    return "[" + TfStringJoin(parts, ", ") + "]";
}

// "BrepCurve3dCircleAPI" -> "Circle", "BrepSurfaceTorusAPI" -> "Torus": the
// short shape name Python builds with two str.replace() calls when it names the
// offending entity in a message.
std::string
_ShortShapeName(const TfToken &token, const char *prefix)
{
    return TfStringReplace(TfStringReplace(token.GetString(), prefix, ""),
                           "API", "");
}

// -------------------------------------------------------------------------- //
// BA.630 / BA.631  brep-angular-{edge,face}-range-max-primary-period          //
// -------------------------------------------------------------------------- //
// An angular parameter maximum -- a circle or ellipse edge's edge:range max
// (BA.630), or the U maximum of a periodic surface's face:range (BA.631) --
// belongs in the primary period (0, 2*pi]. A value past 2*pi describes a domain
// that wraps more than once around the periodic direction.
//
// The bound is 2*pi +/- 1e-6 (PERIOD_TOL in brep_validator.py). That slack is
// what an under-precision 2*pi needs: a producer that writes 6.2831853072
// overshoots 2*pi = 6.283185307179586 by 2.0e-11 at the eleventh decimal, which
// is a rounding artefact of the decimal literal and not a domain that wraps.
// Three face:range and eight edge:range entries in the staged corpus are
// authored that way, and a hard 2*pi ceiling rejects all of them.
void
_CheckAngularRangePrimaryPeriod(const UsdPrim &usdPrim,
                                const UsdSolidBrepArray &brep,
                                UsdValidationErrorVector *errors)
{
    constexpr double periodTol = 1e-6;

    static const TfToken circleTok("BrepCurve3dCircleAPI");
    static const TfToken ellipseTok("BrepCurve3dEllipseAPI");

    // BA.630: circle and ellipse edges.
    const VtArray<TfToken> curveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const VtArray<double> edgeRange = _Read<double>(brep.GetEdgeRangeAttr());
    if (!curveType.empty() && !edgeRange.empty()
        && edgeRange.size() >= 2 * curveType.size()) {
        for (size_t e = 0; e < curveType.size(); ++e) {
            if (curveType[e] != circleTok && curveType[e] != ellipseTok) {
                continue;
            }
            const double paramMax = edgeRange[2 * e + 1];
            if (paramMax < -periodTol || paramMax > _TwoPi + periodTol) {
                _Err(errors,
                     UsdSolidValidationErrorNameTokens
                         ->angularRangeOutsidePrimaryPeriod,
                     usdPrim,
                     TfStringPrintf(
                        "[BA.630] BrepArray <%s>: %s edge #%zu range max = "
                        "%.6f is outside the primary period (0, 2*pi] = "
                        "(0, %.6f].",
                        usdPrim.GetPath().GetText(),
                        _ShortShapeName(curveType[e], "BrepCurve3d").c_str(), e,
                        paramMax, _TwoPi));
            }
        }
    }

    // BA.631: cylinder, cone, sphere and torus faces, whose U parameter is the
    // angle around the surface axis.
    static const std::vector<TfToken> periodicSurfaces
        = { TfToken("BrepSurfaceCylinderAPI"), TfToken("BrepSurfaceConeAPI"),
            TfToken("BrepSurfaceSphereAPI"), TfToken("BrepSurfaceTorusAPI") };
    const VtArray<TfToken> surfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());
    if (surfaceType.empty() || faceRange.empty()
        || faceRange.size() < 2 * surfaceType.size()) {
        return;
    }
    for (size_t f = 0; f < surfaceType.size(); ++f) {
        if (std::find(periodicSurfaces.begin(), periodicSurfaces.end(),
                      surfaceType[f])
            == periodicSurfaces.end()) {
            continue;
        }
        const double uMax = faceRange[2 * f + 1][0];
        if (uMax < -periodTol || uMax > _TwoPi + periodTol) {
            _Err(errors,
                 UsdSolidValidationErrorNameTokens
                     ->angularRangeOutsidePrimaryPeriod,
                 usdPrim,
                 TfStringPrintf(
                    "[BA.631] BrepArray <%s>: %s face #%zu range U-max = %.6f "
                    "is outside the primary period (0, 2*pi] = (0, %.6f].",
                    usdPrim.GetPath().GetText(),
                    _ShortShapeName(surfaceType[f], "BrepSurface").c_str(), f,
                    uMax, _TwoPi));
        }
    }
}

// -------------------------------------------------------------------------- //
// BA.640  brep-face-v-domain-ordering                                        //
// -------------------------------------------------------------------------- //
// On a cylinder or cone face the V parameter runs along the surface axis, and
// face:range holds (UVmin, UVmax): V-min must not exceed V-max. BA.160
// (BrepArrayRanges) reports the wider degeneracy Vmax <= Vmin on every surface
// family, so a swapped cylinder/cone V domain trips both rules, as it does in
// brep_validator.py.
void
_CheckFaceVDomainOrdering(const UsdPrim &usdPrim,
                          const UsdSolidBrepArray &brep,
                          UsdValidationErrorVector *errors)
{
    static const TfToken cylinderTok("BrepSurfaceCylinderAPI");
    static const TfToken coneTok("BrepSurfaceConeAPI");

    const VtArray<TfToken> surfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());
    if (surfaceType.empty() || faceRange.empty()
        || faceRange.size() < 2 * surfaceType.size()) {
        return;
    }
    const _BrepOffsets off = _ComputeOffsets(brep);

    for (size_t f = 0; f < surfaceType.size(); ++f) {
        if (surfaceType[f] != cylinderTok && surfaceType[f] != coneTok) {
            continue;
        }
        const double vMin = faceRange[2 * f][1];
        const double vMax = faceRange[2 * f + 1][1];
        // Written as the positive test, not its negation: a NaN V bound
        // compares false either way, and brep_validator.py leaves it to BA.660
        // rather than calling it an ordering failure.
        if (!(vMin > vMax)) {
            continue;
        }
        size_t brepIdx = 0;
        size_t localFace = f;
        if (off.ok) {
            for (size_t b = 0; b + 1 < off.face.size(); ++b) {
                if (off.face[b] <= f && f < off.face[b + 1]) {
                    brepIdx = b;
                    localFace = f - off.face[b];
                    break;
                }
            }
        }
        _Err(errors,
             UsdSolidValidationErrorNameTokens->faceVDomainNotOrdered,
             usdPrim,
             TfStringPrintf(
                "[BA.640] BrepArray <%s>: %s face #%zu in brep #%zu has V-min "
                "(%.6f) > V-max (%.6f). V-domain must be ordered "
                "(V-min <= V-max).",
                usdPrim.GetPath().GetText(),
                _ShortShapeName(surfaceType[f], "BrepSurface").c_str(),
                localFace, brepIdx, vMin, vMax));
    }
}

// -------------------------------------------------------------------------- //
// BA.660  brep-float-arrays-finite                                           //
// -------------------------------------------------------------------------- //
// Index of the first element of a floating-point array attribute that holds a
// NaN or an Inf in any component, or -1. The attribute's value type is not
// known statically (one rule covers double[], double2[], double3[], point3d[]
// and vector3d[] attributes), so the held array type is inspected.
long
_FirstNonFiniteIndex(const UsdAttribute &attr)
{
    if (!attr || !attr.HasAuthoredValue()) {
        return -1;
    }
    VtValue value;
    if (!attr.Get(&value)) {
        return -1;
    }

    if (value.IsHolding<VtArray<double>>()) {
        const VtArray<double> &a = value.UncheckedGet<VtArray<double>>();
        for (size_t i = 0; i < a.size(); ++i) {
            if (!std::isfinite(a[i])) {
                return static_cast<long>(i);
            }
        }
    } else if (value.IsHolding<VtArray<float>>()) {
        const VtArray<float> &a = value.UncheckedGet<VtArray<float>>();
        for (size_t i = 0; i < a.size(); ++i) {
            if (!std::isfinite(a[i])) {
                return static_cast<long>(i);
            }
        }
    } else if (value.IsHolding<VtArray<GfVec3d>>()) {
        const VtArray<GfVec3d> &a = value.UncheckedGet<VtArray<GfVec3d>>();
        for (size_t i = 0; i < a.size(); ++i) {
            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(a[i][c])) {
                    return static_cast<long>(i);
                }
            }
        }
    } else if (value.IsHolding<VtArray<GfVec3f>>()) {
        const VtArray<GfVec3f> &a = value.UncheckedGet<VtArray<GfVec3f>>();
        for (size_t i = 0; i < a.size(); ++i) {
            for (int c = 0; c < 3; ++c) {
                if (!std::isfinite(a[i][c])) {
                    return static_cast<long>(i);
                }
            }
        }
    } else if (value.IsHolding<VtArray<GfVec2d>>()) {
        const VtArray<GfVec2d> &a = value.UncheckedGet<VtArray<GfVec2d>>();
        for (size_t i = 0; i < a.size(); ++i) {
            for (int c = 0; c < 2; ++c) {
                if (!std::isfinite(a[i][c])) {
                    return static_cast<long>(i);
                }
            }
        }
    } else if (value.IsHolding<VtArray<GfVec2f>>()) {
        const VtArray<GfVec2f> &a = value.UncheckedGet<VtArray<GfVec2f>>();
        for (size_t i = 0; i < a.size(); ++i) {
            for (int c = 0; c < 2; ++c) {
                if (!std::isfinite(a[i][c])) {
                    return static_cast<long>(i);
                }
            }
        }
    }
    return -1;
}

// A NaN or an Inf anywhere in a floating-point array poisons every downstream
// tolerance comparison silently: NaN compares false against every bound, so a
// rule that asks "is this value out of range" clears it. One finding per
// BrepArray, naming the first offending attribute and index, matches
// brep_validator.py, which stops at the first hit.
void
_CheckFloatArraysFinite(const UsdPrim &usdPrim,
                        UsdValidationErrorVector *errors)
{
    static const std::vector<const char *> floatAttrs = {
        "brep:intersectTol3d",
        "brep:extent",
        "face:range",
        "edge:range",
        "wireEdge:range",
        "brep:edge3dNurb:curve3d:nurb:controlVertices",
        "brep:edge3dNurb:curve3d:nurb:knots",
        "brep:edge3dNurb:curve3d:nurb:weights",
        "brep:wireEdge3dNurb:curve3d:nurb:controlVertices",
        "brep:wireEdge3dNurb:curve3d:nurb:knots",
        "brep:wireEdge3dNurb:curve3d:nurb:weights",
        "brep:curveUv:nurb:controlVertices",
        "brep:curveUv:nurb:knots",
        "brep:curveUv:nurb:weights",
        "brep:surface:nurb:controlVertices",
        "brep:surface:nurb:uKnots",
        "brep:surface:nurb:vKnots",
        "brep:surface:nurb:weights",
        "brep:surface:sphere:center",
        "brep:surface:sphere:axis",
        "brep:surface:sphere:refDirection",
        "brep:surface:sphere:radius",
        "brep:surface:plane:origin",
        "brep:surface:plane:axis",
        "brep:surface:plane:refDirection",
        "brep:surface:cylinder:origin",
        "brep:surface:cylinder:axis",
        "brep:surface:cylinder:refDirection",
        "brep:surface:cylinder:radius",
        "brep:surface:cone:origin",
        "brep:surface:cone:axis",
        "brep:surface:cone:refDirection",
        "brep:surface:cone:radius",
        "brep:surface:cone:semiAngle",
        "brep:surface:torus:origin",
        "brep:surface:torus:axis",
        "brep:surface:torus:refDirection",
        "brep:surface:torus:majorRadius",
        "brep:surface:torus:minorRadius",
        "brep:vertexPoint:point:position",
        "brep:shellPoint:point:position",
        "brep:edge3dCircle:curve3d:circle:center",
        "brep:edge3dCircle:curve3d:circle:axis",
        "brep:edge3dCircle:curve3d:circle:refDirection",
        "brep:edge3dCircle:curve3d:circle:radius",
        "brep:edge3dLine:curve3d:line:origin",
        "brep:edge3dLine:curve3d:line:direction",
        "brep:edge3dEllipse:curve3d:ellipse:center",
        "brep:edge3dEllipse:curve3d:ellipse:axis",
        "brep:edge3dEllipse:curve3d:ellipse:refDirection",
        "brep:edge3dEllipse:curve3d:ellipse:xRadius",
        "brep:edge3dEllipse:curve3d:ellipse:yRadius",
    };

    for (const char *name : floatAttrs) {
        const long index
            = _FirstNonFiniteIndex(usdPrim.GetAttribute(TfToken(name)));
        if (index < 0) {
            continue;
        }
        _Err(errors,
             UsdSolidValidationErrorNameTokens->nonFiniteFloatArrayValue,
             usdPrim,
             TfStringPrintf(
                "[BA.660] BrepArray <%s>: %s[%ld] contains NaN or Inf value.",
                usdPrim.GetPath().GetText(), name, index));
        return;
    }
}

// -------------------------------------------------------------------------- //
// BA.620  brep-analytic-surface-origin-containment                           //
// -------------------------------------------------------------------------- //
// Grows a running box to include a point.
void
_AccumulateBox(const GfVec3d &p, GfVec3d *lo, GfVec3d *hi)
{
    for (int c = 0; c < 3; ++c) {
        (*lo)[c] = std::min((*lo)[c], p[c]);
        (*hi)[c] = std::max((*hi)[c], p[c]);
    }
}

// An analytic surface's origin (a sphere's center) is a placement, not a point
// on the face, so it may legitimately sit outside brep:extent -- a plane whose
// origin is the assembly coordinate system is the common case. The rule
// therefore fires only when the origin sits outside the union of the
// brep:extent boxes expanded by twice that union's diagonal AND the face the
// surface carries, evaluated over its authored face:range, also lies outside
// the expansion. Sphere and torus faces have no evaluation branch in
// brep_validator.py, so for those the origin test decides alone.
void
_CheckAnalyticSurfaceOriginContainment(const UsdPrim &usdPrim,
                                       const UsdSolidBrepArray &brep,
                                       UsdValidationErrorVector *errors)
{
    const VtArray<GfVec3d> extent = _Read<GfVec3d>(brep.GetBrepExtentAttr());
    if (extent.size() < 2) {
        return;
    }

    GfVec3d globalMin(std::numeric_limits<double>::infinity());
    GfVec3d globalMax(-std::numeric_limits<double>::infinity());
    for (size_t i = 0; i + 1 < extent.size(); i += 2) {
        _AccumulateBox(extent[i], &globalMin, &globalMax);
        _AccumulateBox(extent[i + 1], &globalMin, &globalMax);
    }
    const double diag = (globalMax - globalMin).GetLength();
    if (diag < 1e-12) {
        return;
    }
    const double margin = diag * 2.0;

    const VtArray<TfToken> surfaceType
        = _Read<TfToken>(brep.GetFaceSurfaceTypeAttr());
    const VtArray<GfVec2d> faceRange = _Read<GfVec2d>(brep.GetFaceRangeAttr());

    struct Family {
        const char *base;          // brep:surface:<family>:
        const char *originAttr;    // origin, or center for a sphere
        const char *surfaceToken;
        const char *label;
    };
    static const std::vector<Family> families = {
        { "brep:surface:plane:", "brep:surface:plane:origin",
          "BrepSurfacePlaneAPI", "Plane" },
        { "brep:surface:cylinder:", "brep:surface:cylinder:origin",
          "BrepSurfaceCylinderAPI", "Cylinder" },
        { "brep:surface:cone:", "brep:surface:cone:origin",
          "BrepSurfaceConeAPI", "Cone" },
        { "brep:surface:sphere:", "brep:surface:sphere:center",
          "BrepSurfaceSphereAPI", "Sphere" },
        { "brep:surface:torus:", "brep:surface:torus:origin",
          "BrepSurfaceTorusAPI", "Torus" },
    };

    for (const Family &family : families) {
        const VtArray<GfVec3d> origins
            = _ReadName<GfVec3d>(usdPrim, family.originAttr);
        if (origins.empty()) {
            continue;
        }

        // Instances of one surface family are packed in face order, so the i-th
        // origin belongs to the i-th face carrying that surfaceType.
        std::vector<size_t> faceIndices;
        const TfToken token(family.surfaceToken);
        for (size_t f = 0; f < surfaceType.size(); ++f) {
            if (surfaceType[f] == token) {
                faceIndices.push_back(f);
            }
        }
        const std::string label(family.label);

        for (size_t i = 0; i < origins.size(); ++i) {
            const GfVec3d &origin = origins[i];
            bool originInside = true;
            for (int c = 0; c < 3; ++c) {
                if (origin[c] < globalMin[c] - margin
                    || origin[c] > globalMax[c] + margin) {
                    originInside = false;
                    break;
                }
            }
            if (originInside) {
                continue;
            }

            // The origin is out; fall back to where the face actually sits.
            bool faceInside = false;
            if (!faceRange.empty() && i < faceIndices.size()
                && 2 * faceIndices[i] + 1 < faceRange.size()) {
                const size_t fi = faceIndices[i];
                const double uMin = faceRange[2 * fi][0];
                const double vMin = faceRange[2 * fi][1];
                const double uMax = faceRange[2 * fi + 1][0];
                const double vMax = faceRange[2 * fi + 1][1];

                GfVec3d lo(std::numeric_limits<double>::infinity());
                GfVec3d hi(-std::numeric_limits<double>::infinity());
                bool haveBox = false;

                const std::string base(family.base);
                const VtArray<GfVec3d> axis
                    = _ReadName<GfVec3d>(usdPrim, base + "axis");
                const VtArray<GfVec3d> ref
                    = _ReadName<GfVec3d>(usdPrim, base + "refDirection");

                if (label == "Plane") {
                    if (i < axis.size() && i < ref.size()) {
                        const GfVec3d binormal = GfCross(axis[i], ref[i]);
                        const GfVec2d corners[4]
                            = { GfVec2d(uMin, vMin), GfVec2d(uMax, vMin),
                                GfVec2d(uMin, vMax), GfVec2d(uMax, vMax) };
                        for (const GfVec2d &uv : corners) {
                            _AccumulateBox(
                                origin + uv[0] * ref[i] + uv[1] * binormal,
                                &lo, &hi);
                        }
                        haveBox = true;
                    }
                } else if (label == "Cylinder" || label == "Cone") {
                    const bool cone = label == "Cone";
                    const VtArray<double> radius
                        = _ReadName<double>(usdPrim, base + "radius");
                    const VtArray<double> semiAngle = cone
                        ? _ReadName<double>(usdPrim, base + "semiAngle")
                        : VtArray<double>();
                    if (i < axis.size() && i < ref.size() && i < radius.size()
                        && (!cone || i < semiAngle.size())) {
                        const GfVec3d binormal = GfCross(axis[i], ref[i]);
                        const double tanA
                            = cone ? std::tan(semiAngle[i]) : 0.0;
                        const double vs[2] = { vMin, vMax };
                        const double us[3]
                            = { uMin, uMax, (uMin + uMax) / 2.0 };
                        for (const double v : vs) {
                            const double rv = radius[i] + v * tanA;
                            for (const double u : us) {
                                _AccumulateBox(
                                    origin + v * axis[i]
                                        + rv * (std::cos(u) * ref[i]
                                                + std::sin(u) * binormal),
                                    &lo, &hi);
                            }
                        }
                        // Expand by the largest radius so every angular
                        // position is covered, not only the three sampled.
                        const double maxR = cone
                            ? std::max(std::abs(radius[i] + vMin * tanA),
                                       std::abs(radius[i] + vMax * tanA))
                            : radius[i];
                        for (int c = 0; c < 3; ++c) {
                            lo[c] -= maxR;
                            hi[c] += maxR;
                        }
                        haveBox = true;
                    }
                }

                if (haveBox) {
                    faceInside = true;
                    for (int c = 0; c < 3; ++c) {
                        if (hi[c] < globalMin[c] - margin
                            || lo[c] > globalMax[c] + margin) {
                            faceInside = false;
                            break;
                        }
                    }
                }
            }
            if (faceInside) {
                continue;
            }

            _Err(errors,
                 UsdSolidValidationErrorNameTokens
                     ->analyticSurfaceOriginOutsideBrepExtent,
                 usdPrim,
                 TfStringPrintf(
                     "[BA.620] BrepArray <%s>: %s surface #%zu origin/center "
                     "(%.6f, %.6f, %.6f) lies outside the brep extent expanded "
                     "by %.4f (extent: [%.4f, %.4f, %.4f] - "
                     "[%.4f, %.4f, %.4f]).",
                     usdPrim.GetPath().GetText(), family.label, i, origin[0],
                     origin[1], origin[2], margin, globalMin[0], globalMin[1],
                     globalMin[2], globalMax[0], globalMax[1], globalMax[2]));
        }
    }
}

// -------------------------------------------------------------------------- //
// BA.710  brep-shell-point-position-extent-containment                       //
// -------------------------------------------------------------------------- //
// A point shell (_IsBrepPointShell) carries one point, and that point belongs
// to its own Brep, so it is measured against that Brep's brep:extent box rather
// than the union of boxes BA.310 uses for vertices. Shell points are packed in
// shell order across the whole BrepArray, so the walk tracks a running
// shell-point cursor while it partitions shells per Brep; a BrepPointAPI token
// on a face or wire shell is ignored and takes no slot in that packing.
// The comparison carries the single-precision slop of _FloatClose, matching
// isFloatLessThan / isFloatGreaterThan in brep_validator.py.
void
_CheckShellPointContainment(const UsdPrim &usdPrim,
                            const UsdSolidBrepArray &brep,
                            UsdValidationErrorVector *errors)
{
    const VtArray<GfVec3d> shellPositions
        = _ReadName<GfVec3d>(usdPrim, "brep:shellPoint:point:position");
    const VtArray<GfVec3d> extent = _Read<GfVec3d>(brep.GetBrepExtentAttr());
    if (shellPositions.empty() || extent.size() < 2) {
        return;
    }
    const VtArray<unsigned int> regionCount
        = _Read<unsigned int>(brep.GetBrepRegionCountAttr());
    const VtArray<unsigned int> shellCount
        = _Read<unsigned int>(brep.GetRegionShellCountAttr());
    if (regionCount.empty() || shellCount.empty()) {
        return;
    }
    const VtArray<TfToken> shellPointType
        = _Read<TfToken>(brep.GetShellPointTypeAttr());
    const VtArray<unsigned int> shellFaceuseCount
        = _Read<unsigned int>(brep.GetShellFaceuseCountAttr());
    const VtArray<unsigned int> shellWireEdgeCount
        = _Read<unsigned int>(brep.GetShellWireEdgeCountAttr());

    size_t shellOffset = 0;
    size_t regionOffset = 0;
    size_t pointCursor = 0;

    for (size_t b = 0; b < regionCount.size(); ++b) {
        const size_t shellStart = shellOffset;
        for (size_t r = 0; r < regionCount[b]; ++r) {
            const size_t ri = regionOffset + r;
            if (ri < shellCount.size()) {
                shellOffset += shellCount[ri];
            }
        }
        regionOffset += regionCount[b];
        const size_t shellEnd = shellOffset;

        if (2 * b + 1 >= extent.size()) {
            continue;
        }
        const GfVec3d &extMin = extent[2 * b];
        const GfVec3d &extMax = extent[2 * b + 1];

        for (size_t s = shellStart; s < shellEnd; ++s) {
            if (!_IsBrepPointShell(s, shellPointType, shellFaceuseCount,
                                   shellWireEdgeCount)) {
                continue;
            }
            if (pointCursor >= shellPositions.size()) {
                continue;
            }
            const GfVec3d &p = shellPositions[pointCursor];
            bool outside = false;
            for (int c = 0; c < 3; ++c) {
                if ((p[c] < extMin[c] && !_FloatClose(p[c], extMin[c]))
                    || (p[c] > extMax[c] && !_FloatClose(p[c], extMax[c]))) {
                    outside = true;
                    break;
                }
            }
            if (outside) {
                _Err(errors,
                     UsdSolidValidationErrorNameTokens
                         ->shellPointPositionOutsideBrepExtent,
                     usdPrim,
                     TfStringPrintf(
                         "[BA.710] BrepArray <%s>: shellPoint:position[%zu] = "
                         "[%g, %g, %g] in brep #%zu is outside brep extent.",
                         usdPrim.GetPath().GetText(), pointCursor, p[0], p[1],
                         p[2], b));
            }
            ++pointCursor;
        }
    }
}

// -------------------------------------------------------------------------- //
// BA.730  brep-nurbs-edge-endpoint-vertex-consistency                        //
// -------------------------------------------------------------------------- //
// Smallest brep:intersectTol3d BA.730 will measure against. Below it the
// authored tolerance is treated as absent and the edge is reported as
// unvalidatable, matching BrepConstants.NUMERICAL_TOLERANCE in
// brep_validator.py.
constexpr double _MinResolvableTol3d = 1e-11;

// Evaluate a rational B-spline curve at parameter t with de Boor's algorithm.
// Returns false when the curve data is too short to evaluate or the weight sum
// collapses. Ported from _de_boor_evaluate in brep_validator.py, including its
// clamping of t into [knots[order-1], knots[vertexCount]].
bool
_DeBoorEvaluate3d(unsigned int order, const std::vector<double> &knots,
                  const std::vector<GfVec3d> &cvs,
                  const std::vector<double> &weights, double t, GfVec3d *out)
{
    const size_t n = cvs.size();
    if (order < 1 || n < order || knots.size() < n + order
        || weights.size() < n) {
        return false;
    }
    const size_t p = order - 1;

    t = std::max(knots[p], std::min(t, knots[n]));

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
        // t sits at (or numerically at) the far end of the knot domain, where
        // the half-open span test above never matches.
        const double kn = knots[n];
        const double closeTol
            = std::max(1e-12 * std::max(std::abs(t), std::abs(kn)), 1e-14);
        if (std::abs(t - kn) <= closeTol) {
            k = n - 1;
        }
    }

    // Homogeneous control points (w*x, w*y, w*z, w).
    std::vector<std::array<double, 4>> d(p + 1);
    for (size_t j = 0; j <= p; ++j) {
        const size_t idx = k - p + j;
        if (idx >= n) {
            return false;
        }
        const double w = weights[idx];
        d[j] = { cvs[idx][0] * w, cvs[idx][1] * w, cvs[idx][2] * w, w };
    }

    for (size_t r = 1; r <= p; ++r) {
        for (size_t j = p; j >= r; --j) {
            const size_t left = k - p + j;
            const size_t right = left + p - r + 1;
            if (right >= knots.size() || left >= knots.size()) {
                return false;
            }
            const double denom = knots[right] - knots[left];
            const double alpha
                = std::abs(denom) < 1e-30 ? 0.0 : (t - knots[left]) / denom;
            for (int c = 0; c < 4; ++c) {
                d[j][c] = (1.0 - alpha) * d[j - 1][c] + alpha * d[j][c];
            }
        }
    }

    const double w = d[p][3];
    if (std::abs(w) < 1e-30) {
        return false;
    }
    *out = GfVec3d(d[p][0] / w, d[p][1] / w, d[p][2] / w);
    return true;
}

// A NURBS edge evaluated at its authored edge:range endpoints must land on the
// vertices its edge:vertexIndices name, within the Brep's brep:intersectTol3d.
//
// The tolerance is resolved per Brep here rather than through
// _FirstAuthoredIntersectTol3d: brep_validator.py attributes each edge to a
// Brep through the edgeuses that reference it and reads that Brep's tolerance,
// and reports the edge as unvalidatable when no positive tolerance resolves.
// This rule has no reader-side fallback.
void
_CheckNurbsEdgeEndpointVertices(const UsdPrim &usdPrim,
                                const UsdSolidBrepArray &brep,
                                UsdValidationErrorVector *errors)
{
    static const TfToken nurbTok("BrepCurve3dNurbAPI");
    const std::string base = "brep:edge3dNurb:curve3d:nurb:";

    const VtArray<TfToken> curveType
        = _Read<TfToken>(brep.GetEdgeCurveTypeAttr());
    const VtArray<double> edgeRange = _Read<double>(brep.GetEdgeRangeAttr());
    const VtArray<GfVec2i> vertexIndices
        = _Read<GfVec2i>(brep.GetEdgeVertexIndicesAttr());
    const VtArray<GfVec3d> vertexPos
        = _ReadName<GfVec3d>(usdPrim, "brep:vertexPoint:point:position");
    const VtArray<double> intersectTol
        = _Read<double>(brep.GetBrepIntersectTol3dAttr());
    const VtArray<unsigned int> order
        = _ReadName<unsigned int>(usdPrim, base + "order");
    const VtArray<unsigned int> vtxCount
        = _ReadName<unsigned int>(usdPrim, base + "vertexCount");
    const VtArray<GfVec3d> cvs
        = _ReadName<GfVec3d>(usdPrim, base + "controlVertices");
    const VtArray<double> weights
        = _ReadName<double>(usdPrim, base + "weights");
    const VtArray<double> knots = _ReadName<double>(usdPrim, base + "knots");

    if (curveType.empty() || edgeRange.empty() || vertexIndices.empty()
        || vertexPos.empty() || order.empty() || vtxCount.empty()
        || cvs.empty() || weights.empty() || knots.empty()) {
        return;
    }

    // Attribute each edge to the first Brep whose edgeuses reference it.
    const _BrepOffsets off = _ComputeOffsets(brep);
    const VtArray<unsigned int> edgeuseEdgeIndex
        = _Read<unsigned int>(brep.GetEdgeuseEdgeIndexAttr());
    std::unordered_map<unsigned int, size_t> edgeBrepIndex;
    if (off.ok) {
        for (size_t b = 0; b + 1 < off.edgeuse.size(); ++b) {
            const size_t stop
                = std::min(off.edgeuse[b + 1], edgeuseEdgeIndex.size());
            for (size_t eu = off.edgeuse[b]; eu < stop; ++eu) {
                edgeBrepIndex.emplace(edgeuseEdgeIndex[eu], b);
            }
        }
    }

    size_t nurbIdx = 0;
    size_t cvOffset = 0;
    size_t knotOffset = 0;

    for (size_t e = 0; e < curveType.size(); ++e) {
        if (curveType[e] != nurbTok) {
            continue;
        }
        if (nurbIdx >= order.size() || nurbIdx >= vtxCount.size()) {
            break;
        }
        const unsigned int ord = order[nurbIdx];
        const size_t nCv = vtxCount[nurbIdx];
        const size_t nKnots = nCv + ord;

        const bool usable = cvOffset + nCv <= cvs.size()
            && cvOffset + nCv <= weights.size()
            && knotOffset + nKnots <= knots.size()
            && 2 * e + 1 < edgeRange.size() && e < vertexIndices.size();
        if (usable) {
            const std::vector<double> edgeKnots(
                knots.begin() + knotOffset,
                knots.begin() + knotOffset + nKnots);
            const std::vector<GfVec3d> edgeCvs(cvs.begin() + cvOffset,
                                               cvs.begin() + cvOffset + nCv);
            const std::vector<double> edgeWeights(
                weights.begin() + cvOffset, weights.begin() + cvOffset + nCv);

            // Resolve the tolerance of this edge's Brep.
            size_t brepIdx = 0;
            bool haveBrep = false;
            const auto it = edgeBrepIndex.find(static_cast<unsigned int>(e));
            if (it != edgeBrepIndex.end()) {
                brepIdx = it->second;
                haveBrep = true;
            } else if (intersectTol.size() == 1) {
                haveBrep = true;
            }
            const bool haveTol = haveBrep && brepIdx < intersectTol.size()
                && std::isfinite(intersectTol[brepIdx])
                && intersectTol[brepIdx] >= _MinResolvableTol3d;

            if (!haveTol) {
                _Err(errors,
                     UsdSolidValidationErrorNameTokens
                         ->unresolvedEdgeIntersectTol3d,
                     usdPrim,
                     TfStringPrintf(
                         "[BA.730] BrepArray <%s>: NURBS endpoint-to-vertex "
                         "consistency for edge #%zu could not be validated "
                         "because no positive brep:intersectTol3d value could "
                         "be resolved for the edge. The tolerance is missing, "
                         "invalid, or the edge could not be associated with a "
                         "BRep.",
                         usdPrim.GetPath().GetText(), e));
            } else {
                const double tol = intersectTol[brepIdx];
                const double params[2]
                    = { edgeRange[2 * e], edgeRange[2 * e + 1] };
                const int vtxIdx[2]
                    = { vertexIndices[e][0], vertexIndices[e][1] };
                const char *labels[2] = { "start", "end" };
                for (int side = 0; side < 2; ++side) {
                    if (vtxIdx[side] < 0
                        || static_cast<size_t>(vtxIdx[side])
                            >= vertexPos.size()) {
                        continue;
                    }
                    GfVec3d evaluated;
                    if (!_DeBoorEvaluate3d(ord, edgeKnots, edgeCvs, edgeWeights,
                                           params[side], &evaluated)) {
                        continue;
                    }
                    const double dist
                        = (evaluated - vertexPos[vtxIdx[side]]).GetLength();
                    if (dist > tol) {
                        _Err(errors,
                             UsdSolidValidationErrorNameTokens
                                 ->nurbsEdgeEndpointVertexMismatch,
                             usdPrim,
                             TfStringPrintf(
                                 "[BA.730] BrepArray <%s>: NURBS edge #%zu in "
                                 "brep #%zu %s endpoint evaluated at t=%.6f is "
                                 "%.6f from vertex #%d "
                                 "(brep:intersectTol3d[%zu] = %g).",
                                 usdPrim.GetPath().GetText(), e, brepIdx,
                                 labels[side], params[side], dist,
                                 vtxIdx[side], brepIdx, tol));
                        break;
                    }
                }
            }
        }

        ++nurbIdx;
        cvOffset += nCv;
        knotOffset += nKnots;
    }
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
    // slop follows the Brep's own authored 3D tolerance
    // (_FirstAuthoredIntersectTol3d) rather than the former hard-coded 1e-11, which
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
    const double floatRel = _ExtentFloatRel;

    // BA.040 / BA.045 / BA.050: each brep:extent box within the prim's own
    // extent (_validate_brep_extent).
    {
        _BrepChecker c(usdPrim, { "BA.040", "BA.045", "BA.050" });
        c.ValidateBrepExtent();
        errors = c.TakeErrors();
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

    // BA.365 / BA.465: a NURBS control vertex must lie within a brep:extent
    // box. brep_validator.py reports one outside as a failed check, so it is an
    // Error here, even though a control hull may extend past the surface it
    // defines.
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
                                usdPrim.GetPath().GetText(), c));
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
                                usdPrim.GetPath().GetText(), c));
            break;
        }
    }

    // BA.620: an analytic surface's origin, and the face it carries, must not
    // both sit outside the extent expanded by twice its diagonal.
    _CheckAnalyticSurfaceOriginContainment(usdPrim, brep, &errors);

    // BA.710: a shell point lies within its own Brep's brep:extent box.
    _CheckShellPointContainment(usdPrim, brep, &errors);

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
    // Tolerance: the shared authored-tol resolution
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
// Each edge is measured against the tolerance of the Brep that owns it, with no
// fallback.
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

    // --- WireEdge 3D NURBS (instance wireEdge3dNurb) --- //
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
    // BA.650 - BA.658 are the wireEdge counterparts of the edge3d family above
    // (BA.330 - BA.370): the brep:wireEdge3dNurb stratum is sized against the
    // wireEdge:curveType entries naming BrepCurve3dNurbAPI, the way the edge3d
    // stratum is sized against edge:curveType. They run only when a wireEdge
    // actually declares a NURBS curve; a BrepArray with no wire edges authors
    // none of these arrays and the BA.290 checks above cover the schema-usage
    // case on their own.
    if (nWire > 0) {
        const VtArray<GfVec3d> wCv = _ReadName<GfVec3d>(
            usdPrim, "brep:wireEdge3dNurb:curve3d:nurb:controlVertices");
        const VtArray<double> wW = _ReadName<double>(
            usdPrim, "brep:wireEdge3dNurb:curve3d:nurb:weights");
        const VtArray<double> wKn = _ReadName<double>(
            usdPrim, "brep:wireEdge3dNurb:curve3d:nurb:knots");

        // BA.658: the five arrays are read together by every rule below, so a
        // stratum missing any one of them is reported once and the rest of the
        // family is skipped -- a size rule run against an absent array reports
        // the absence a second time under a number that means something else.
        const bool present[5] = { !wO.empty(), !wVC.empty(), !wCv.empty(),
                                  !wW.empty(), !wKn.empty() };
        static const char *const partNames[5]
            = { "order", "vertexCount", "controlVertices", "weights", "knots" };
        const bool complete = present[0] && present[1] && present[2]
            && present[3] && present[4];
        if (!complete) {
            std::vector<std::string> have, missing;
            for (int i = 0; i < 5; ++i) {
                (present[i] ? have : missing).push_back(partNames[i]);
            }
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens->nurbSchemaDataIncomplete,
                 usdPrim,
                 TfStringPrintf("[BA.658] BrepArray <%s>: wireEdge3dNurb data is "
                                "incomplete. Present: [%s]. Missing: [%s].",
                                usdPrim.GetPath().GetText(),
                                TfStringJoin(have, ", ").c_str(),
                                TfStringJoin(missing, ", ").c_str()));
        } else {
            // BA.650: order / vertexCount sized by the BrepCurve3dNurbAPI count.
            if (wO.size() != nWire) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                     usdPrim,
                     TfStringPrintf("[BA.650] BrepArray <%s>: wireEdge3dNurb "
                                    "order size %zu but expected %zu.",
                                    usdPrim.GetPath().GetText(), wO.size(),
                                    nWire));
            }
            if (wVC.size() != nWire) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens->nurbSizeArrayMismatch,
                     usdPrim,
                     TfStringPrintf("[BA.650] BrepArray <%s>: wireEdge3dNurb "
                                    "vertexCount size %zu but expected %zu.",
                                    usdPrim.GetPath().GetText(), wVC.size(),
                                    nWire));
            }

            // BA.651 (order >= 2) and BA.652 (order <= vertexCount) report every
            // offending curve, and the same pass accumulates the control-vertex
            // total BA.653 needs.
            const size_t mWire = std::min(wO.size(), wVC.size());
            size_t expectedWireCv = 0;
            for (size_t i = 0; i < mWire; ++i) {
                if (wO[i] < 2u) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->nurbOrderBelowMinimum,
                         usdPrim,
                         TfStringPrintf("[BA.651] BrepArray <%s>: wireEdge3d "
                                        "order[%zu] = %u must be >= 2 "
                                        "(degree >= 1).",
                                        usdPrim.GetPath().GetText(), i, wO[i]));
                }
                if (wO[i] > wVC[i]) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->nurbOrderExceedsVertexCount,
                         usdPrim,
                         TfStringPrintf("[BA.652] BrepArray <%s>: wireEdge3d "
                                        "order[%zu] = %u exceeds vertexCount %u.",
                                        usdPrim.GetPath().GetText(), i, wO[i],
                                        wVC[i]));
                }
                expectedWireCv += wVC[i];
            }

            // BA.653: controlVertices and weights each hold one entry per
            // control point. Reported per attribute, so a file that gets one of
            // the two right still names the one it got wrong.
            if (wCv.size() != expectedWireCv) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens
                         ->nurbControlVertexWeightSizeMismatch,
                     usdPrim,
                     TfStringPrintf("[BA.653] BrepArray <%s>: wireEdge3dNurb "
                                    "controlVertices size %zu but expected %zu "
                                    "(sum of vertexCount).",
                                    usdPrim.GetPath().GetText(), wCv.size(),
                                    expectedWireCv));
            }
            if (wW.size() != expectedWireCv) {
                _Err(&errors,
                     UsdSolidValidationErrorNameTokens
                         ->nurbControlVertexWeightSizeMismatch,
                     usdPrim,
                     TfStringPrintf("[BA.653] BrepArray <%s>: wireEdge3dNurb "
                                    "weights size %zu but expected %zu (sum of "
                                    "vertexCount).",
                                    usdPrim.GetPath().GetText(), wW.size(),
                                    expectedWireCv));
            }

            // BA.654 (positive weights), BA.655 (knot count) and BA.656 (knots
            // non-decreasing) are the same checks the edge3d stratum runs.
            _CheckNurbWeights(usdPrim, "BA.654", "wireEdge3d", wW, &errors);
            _CheckNurbKnots1D(usdPrim, "BA.655", "BA.656", "wireEdge3d", wO, wVC,
                              wKn, &errors);

            // BA.657: each control vertex lies within one of the brep:extent
            // boxes. Per-point Brep attribution is not derivable from the flat
            // data, so the union of the boxes is used; for a single Brep that is
            // exactly that Brep's box. Reports the first offending control
            // vertex, because the failure this catches -- a stratum indexed
            // against the wrong Brep -- names itself once.
            //
            // Severity follows BA.365 / BA.465, which ask the same question
            // of edge3d and surface control hulls: an Error, as
            // brep_validator.py reports all three as failed checks.
            const VtArray<GfVec3d> wireExtent
                = _Read<GfVec3d>(brep.GetBrepExtentAttr());
            const VtArray<unsigned int> wireRegionCount
                = _Read<unsigned int>(brep.GetBrepRegionCountAttr());
            const size_t numWireBoxes = wireExtent.size() / 2;
            const size_t numWireBreps = wireRegionCount.empty()
                ? numWireBoxes
                : std::min(wireRegionCount.size(), numWireBoxes);
            const double wireSlop
                = std::max(_FirstAuthoredIntersectTol3d(brep), _DomainTol);
            for (size_t c = 0; c < wCv.size() && numWireBreps > 0; ++c) {
                const GfVec3d &p = wCv[c];
                bool inside = false;
                for (size_t b = 0; b < numWireBreps && !inside; ++b) {
                    const GfVec3d &mn = wireExtent[2 * b];
                    const GfVec3d &mx = wireExtent[2 * b + 1];
                    bool within = true;
                    for (int k = 0; k < 3; ++k) {
                        const double lo
                            = mn[k] - wireSlop - _ExtentFloatRel * std::abs(mn[k]);
                        const double hi
                            = mx[k] + wireSlop + _ExtentFloatRel * std::abs(mx[k]);
                        if (p[k] < lo || p[k] > hi) {
                            within = false;
                            break;
                        }
                    }
                    inside = within;
                }
                if (!inside) {
                    _Err(&errors,
                         UsdSolidValidationErrorNameTokens
                             ->controlPointOutsideBrepExtent,
                         usdPrim,
                         TfStringPrintf("[BA.657] BrepArray <%s>: wireEdge3dNurb "
                                        "control vertex %zu (%g, %g, %g) lies "
                                        "outside all brep:extent boxes (NURBS "
                                        "control hulls may legitimately exceed "
                                        "the curve bounds).",
                                        usdPrim.GetPath().GetText(), c, p[0],
                                        p[1], p[2]));
                    break;
                }
            }
        }
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
        // BA.405: one weight per packed UV control vertex, whether or not the
        // weights are authored. A missing or empty weights array against a
        // non-zero control-vertex total is a cardinality failure in its own
        // right, not an exemption (OMPE-106502); an all-sentinel record (every
        // vertexCount zero) expects no weights and passes without any.
        if (cW.size() != expectedCv) {
            _Err(&errors,
                 UsdSolidValidationErrorNameTokens
                     ->nurbControlVertexWeightSizeMismatch,
                 usdPrim,
                 TfStringPrintf("[BA.405] BrepArray <%s>: Invalid size for "
                                "brep:curveUv:nurb:weights. Expected size "
                                "%zu, but got %zu.",
                                usdPrim.GetPath().GetText(), expectedCv,
                                cW.size()));
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
// (or has no seam at all). The repeat is a topological signal, not a proof
// that the repeated edge is geometrically the seam, but brep_validator.py
// reports a face without one as a failed check, so it is an Error here too.
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
                 localFaceIdx, brepIdx, axes.c_str()));
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
        UsdSolidValidatorNameTokens->brepArrayEdgeCurveVertices,
        _BrepArrayEdgeCurveVertices);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayUvTrim, _BrepArrayUvTrim);

    registry.RegisterPluginValidator(
        UsdSolidValidatorNameTokens->brepArrayGeomSubsets,
        _BrepArrayGeomSubsets);
}

PXR_NAMESPACE_CLOSE_SCOPE
