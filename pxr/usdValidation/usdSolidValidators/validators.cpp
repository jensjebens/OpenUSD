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
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

PXR_NAMESPACE_OPEN_SCOPE

namespace {

UsdValidationErrorSites
_PrimSites(const UsdPrim &prim)
{
    return { UsdValidationErrorSite(prim.GetStage(), prim.GetPath()) };
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
    void ValidateCurve3dNurbControlVerticesWeights();
    void ValidateCurve3dNurbOrderVertexCount();
    void ValidateCurve3dKnots();
    void ValidateCurveUvData();
    void ValidateSurfaceControlVerticesWeights();
    void ValidateSurfaceOrdersVertexCounts();
    void ValidateSurfaceKnots();
    void ValidateSchemaConsistency();
    void ValidateNurbsDataCompleteness();
    void ValidateNurbsMathematicalConsistency();
    void ValidateNurbsOrderAndVertexCountValues();
    void ValidateWireEdge3dNurbs();
    void ValidateVertexPositionContainment();
    void ValidateEdge3dNurbsControlPointContainment();
    void ValidateSurfaceNurbsControlPointContainment();
    void ValidateAnalyticSurfaceOriginContainment();
    void ValidateShellPointContainment();
    void ValidateNurbsEdgeEndpointVertex();
    void ValidateAnalyticSurfaces();
    void ValidateAnalyticCurves();
    void ValidateFaceRangeDomainLimits();
    void ValidateEdgeRangeDomainLimits();
    void ValidateEdgeCurveEndpointVertexConsistency();
    void ValidateCircleVertexRadiusConsistency();
    void ValidateAngularRangePrimaryPeriod();
    void ValidateFaceVDomainOrdering();
    void ValidateFloatArraysFinite();
    void ValidateGeomsubsetMaterials();
    void ValidateFullPeriodFaceSeamEdgeuseHeuristic();
    void ValidateAnalyticPeriodicDomainBounds();
    void ValidateFullPeriodFaceDomainAlignment();
    void ValidateUvLoopClosure();
    void ValidateZeroLengthUvTrimCurves();
    void ValidateUvTrimCurveDomainContainment();

private:
    void _ValidateAnalyticFamily(const struct _PyAnalyticFamily &family);
    void _ValidateRequiredGeometryApis();
    void _ValidatePointsInBrepExtent(const char *rule, const _PyValue &points,
                                     const std::vector<long long> &offsets,
                                     bool firstOnly, const char *subject);
    std::unordered_map<long long, size_t> _EdgeBrepIndices();
    bool _EdgeIntersectTolerance(
        long long edge, const std::unordered_map<long long, size_t> &edgeBreps,
        double *tol, size_t *brep);
    void _ReportUnresolvedEdgeTolerance(const char *rule, long long edge,
                                        const char *checkLabel);

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

// Python's `any(y < x - NUMERICAL_TOLERANCE for x, y in zip(k, k[1:]))` over
// values[start, end): a knot slice that decreases by more than the tolerance.
bool
_PyKnotsDecrease(const _PyValue &values, size_t start, size_t end)
{
    for (size_t i = start + 1; i < end; ++i) {
        if (values.Num(i) < values.Num(i - 1) - _PyNumericalTolerance) {
            return true;
        }
    }
    return false;
}

// _validate_curve3d_nurb_control_vertices_weights: BA.345 the edge3dNurb
// weights and control vertices hold sum(vertexCount) entries, BA.350 each
// weight of each Brep's control-vertex span is positive. Unreadable values
// are BA.371 (or BA.345 for weights and control vertices) and end the rule.
void
_BrepChecker::ValidateCurve3dNurbControlVerticesWeights()
{
    const std::string base = "brep:edge3dNurb:curve3d:nurb:";
    const _PyValue vertexCounts = _Get(base + "vertexCount").NoneToEmpty();
    const _PyValue weights = _Get(base + "weights").NoneToEmpty();
    const _PyValue controlVertices
        = _Get(base + "controlVertices").NoneToEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();

    if (vertexCounts.IsUnregistered()) {
        _Fail("BA.371", base + "vertexCount has an unregistered USD type; "
                               "expected uint[].");
        return;
    }
    if (weights.IsUnregistered()) {
        _Fail("BA.371", base + "weights has an unregistered USD type; "
                               "expected double[].");
        return;
    }
    if (!vertexCounts.IsSequence()) {
        _Fail("BA.371", base + "vertexCount is not a valid sequence; "
                               "expected uint[].");
        return;
    }
    if (!weights.IsSequence()) {
        _Fail("BA.345", base + "weights is not a valid sequence; expected "
                               "double[].");
        return;
    }
    if (vertexCounts.Len() > 0 && !vertexCounts.IsNumbers()) {
        _Fail("BA.371", base + "vertexCount contains non-integer data; "
                               "expected uint[].");
        return;
    }
    const double expected = _Sum(vertexCounts);
    if (expected > 0 || weights.Len() > 0) {
        _ValidateArraySizesAndAuthored(
            { base + "weights", base + "controlVertices" }, "BA.345",
            &expected, true);
    }
    if (controlVertices.IsUnregistered()) {
        _Fail("BA.371", base + "controlVertices has an unregistered USD "
                               "type; expected point3d[].");
        return;
    }
    if (!controlVertices.IsSequence()) {
        _Fail("BA.345", TfStringPrintf(
            "%scontrolVertices is not a valid sequence; expected size %s.",
            base.c_str(), _PyValue::NumRepr(expected, true).c_str()));
    } else if (controlVertices.Len() > 0
               && static_cast<double>(controlVertices.Len()) != expected) {
        _Fail("BA.345", TfStringPrintf(
            "Invalid size for %scontrolVertices. Expected %s, but got %zu.",
            base.c_str(), _PyValue::NumRepr(expected, true).c_str(),
            controlVertices.Len()));
    }
    if (!weights.Truthy() || numBreps == 0 || !weights.IsNumbers()) {
        return;
    }
    const std::vector<long long> &offsets = _Offsets().edgeControlVertices;
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= offsets.size()) {
            break;
        }
        const long long end
            = std::min(offsets[b + 1], static_cast<long long>(weights.Len()));
        for (long long w = offsets[b]; w < end; ++w) {
            size_t i = 0;
            if (_PyIndex(w, weights.Len(), &i)
                && weights.Num(i) < _PyNumericalTolerance) {
                _Fail("BA.350", TfStringPrintf("Invalid weight at index #%lld "
                                               "in brep #%zu. Weights must be "
                                               "positive.",
                                               w, b));
            }
        }
    }
}

// _validate_curve3d_nurb_order_vertex_count: BA.330 order and vertexCount hold
// one entry per BrepCurve3dNurbAPI edge (checked when such edges exist or
// orders are authored), then per Brep curve span BA.335 order positive and
// BA.340 order <= vertexCount.
void
_BrepChecker::ValidateCurve3dNurbOrderVertexCount()
{
    const std::string base = "brep:edge3dNurb:curve3d:nurb:";
    const _PyValue types = _Get("edge:curveType").OrEmpty();
    double nurbsEdges = 0;
    for (size_t i = 0; i < types.Len(); ++i) {
        nurbsEdges += types.Equals(i, "BrepCurve3dNurbAPI") ? 1 : 0;
    }
    if (nurbsEdges > 0 || _Get(base + "order").OrEmpty().Len() > 0) {
        _ValidateArraySizesAndAuthored({ base + "order", base + "vertexCount" },
                                       "BA.330", &nurbsEdges, true);
    }
    if (_Get(base + "order").IsUnregistered()) {
        _Fail("BA.330", base + "order has an unregistered USD type; expected "
                               "uint[].");
        return;
    }
    if (_Get(base + "vertexCount").IsUnregistered()) {
        _Fail("BA.330", base + "vertexCount has an unregistered USD type; "
                               "expected uint[].");
        return;
    }
    const _PyValue orders = _Get(base + "order").OrEmpty();
    const _PyValue vertexCounts = _Get(base + "vertexCount").OrEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (!orders.Truthy() || !vertexCounts.Truthy() || numBreps == 0
        || !orders.IsNumbers() || !vertexCounts.IsNumbers()) {
        return;
    }
    const std::vector<long long> &offsets = _Offsets().edge3dNurbsCurves;
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= offsets.size()) {
            break;
        }
        const long long end = std::min(
            { offsets[b + 1], static_cast<long long>(orders.Len()),
              static_cast<long long>(vertexCounts.Len()) });
        for (long long c = offsets[b]; c < end; ++c) {
            size_t i = 0, j = 0;
            if (!_PyIndex(c, orders.Len(), &i)
                || !_PyIndex(c, vertexCounts.Len(), &j)) {
                continue;
            }
            const double order = orders.Num(i);
            const double vc = vertexCounts.Num(j);
            if (order <= 0) {
                _Fail("BA.335", TfStringPrintf(
                    "Invalid %sorder %s for curve3d #%lld in brep #%zu. "
                    "%sorder must be positive.",
                    base.c_str(), orders.Repr(i).c_str(), c, b, base.c_str()));
            } else if (order > vc) {
                _Fail("BA.340", TfStringPrintf(
                    "Invalid %sorder %s for curve3d #%lld in brep #%zu. "
                    "%sorder must not exceed %svertexCount %s.",
                    base.c_str(), orders.Repr(i).c_str(), c, b, base.c_str(),
                    base.c_str(), vertexCounts.Repr(j).c_str()));
            }
        }
    }
}

// _validate_curve3d_knots: per Brep curve span, each curve's slice of the
// packed knots holds vertexCount + order knots (BA.355) and does not decrease
// (BA.360). Unreadable values are BA.371 and end the rule.
void
_BrepChecker::ValidateCurve3dKnots()
{
    const std::string base = "brep:edge3dNurb:curve3d:nurb:";
    if (_Get(base + "knots").IsUnregistered()) {
        _Fail("BA.371", base + "knots has an unregistered USD type; expected "
                               "double[].");
        return;
    }
    const _PyValue knots = _Get(base + "knots").OrEmpty();
    const _PyValue vertexCounts = _Get(base + "vertexCount").OrEmpty();
    const _PyValue orders = _Get(base + "order").OrEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (vertexCounts.IsUnregistered()) {
        _Fail("BA.371", base + "vertexCount has an unregistered USD type; "
                               "expected uint[].");
        return;
    }
    if (orders.IsUnregistered()) {
        _Fail("BA.371", base + "order has an unregistered USD type; expected "
                               "uint[].");
        return;
    }
    if (!vertexCounts.IsSequence()) {
        _Fail("BA.371", base + "vertexCount is not a valid sequence; "
                               "expected uint[].");
        return;
    }
    if (!orders.IsSequence()) {
        _Fail("BA.371", base + "order is not a valid sequence; expected "
                               "uint[].");
        return;
    }
    if (!knots.Truthy() || !vertexCounts.Truthy() || !orders.Truthy()
        || numBreps == 0 || !knots.IsNumbers() || !vertexCounts.IsNumbers()
        || !orders.IsNumbers()) {
        return;
    }
    const std::vector<long long> &offsets = _Offsets().edge3dNurbsCurves;
    long long globalOffset = 0;
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= offsets.size()) {
            break;
        }
        const long long end = std::min(
            { offsets[b + 1], static_cast<long long>(vertexCounts.Len()),
              static_cast<long long>(orders.Len()) });
        for (long long c = offsets[b]; c < end; ++c) {
            size_t i = 0, j = 0;
            if (!_PyIndex(c, vertexCounts.Len(), &i)
                || !_PyIndex(c, orders.Len(), &j)) {
                continue;
            }
            const long long expected = static_cast<long long>(
                vertexCounts.Num(i) + orders.Num(j));
            const auto slice
                = _PySlice(globalOffset, globalOffset + expected, knots.Len());
            const long long got
                = static_cast<long long>(slice.second - slice.first);
            if (got != expected) {
                _Fail("BA.355", TfStringPrintf(
                    "Invalid knot count for curve3d #%lld in brep #%zu. "
                    "Expected %lld knots, but got %lld.",
                    c, b, expected, got));
            }
            if (_PyKnotsDecrease(knots, slice.first, slice.second)) {
                _Fail("BA.360", TfStringPrintf(
                    "Invalid knot ordering for curve3d #%lld in brep #%zu. "
                    "Knots must be non-decreasing.",
                    c, b));
            }
            globalOffset += expected;
        }
    }
}

// _validate_curveUv_data, which runs only when brep:curveUv:nurb:vertexCount
// or :order is authored: BA.375 order and vertexCount hold one entry per
// edgeuse; per Brep edgeuse span (the 0/0 record meaning "no pcurve" is
// skipped) BA.380 order positive and BA.385 order <= vertexCount; BA.390 the
// control vertices hold sum(vertexCount) entries; BA.395 each curve's knot
// slice holds vertexCount + order knots and the slices use every knot, BA.400
// each slice non-decreasing; BA.405 the weights hold sum(vertexCount) entries
// and BA.410 each curve's weights are positive. Unreadable values are BA.416
// and end the rule.
void
_BrepChecker::ValidateCurveUvData()
{
    const std::string base = "brep:curveUv:nurb:";
    if (!_IsAuthored(base + "vertexCount") && !_IsAuthored(base + "order")) {
        return;
    }
    const double edgeuses
        = static_cast<double>(_Get("edgeuse:edgeIndex").OrEmpty().Len());
    _ValidateArraySizesAndAuthored({ base + "vertexCount", base + "order" },
                                   "BA.375", &edgeuses, true);
    const std::vector<long long> &offsets = _Offsets().curveUv;

    const _PyValue vertexCounts = _Get(base + "vertexCount").NoneToEmpty();
    const _PyValue orders = _Get(base + "order").NoneToEmpty();
    const _PyValue controlVertices
        = _Get(base + "controlVertices").NoneToEmpty();
    if (controlVertices.IsUnregistered()) {
        _Fail("BA.416", base + "controlVertices has an unregistered USD type; "
                               "expected double2[].");
        return;
    }
    if (vertexCounts.IsUnregistered()) {
        _Fail("BA.416", base + "vertexCount has an unregistered USD type; "
                               "expected uint[].");
        return;
    }
    if (orders.IsUnregistered()) {
        _Fail("BA.416", base + "order has an unregistered USD type; expected "
                               "uint[].");
        return;
    }
    if (!vertexCounts.IsSequence()) {
        _Fail("BA.416", base + "vertexCount is not a valid sequence; expected "
                               "uint[].");
        return;
    }
    if (!orders.IsSequence()) {
        _Fail("BA.416", base + "order is not a valid sequence; expected "
                               "uint[].");
        return;
    }

    // The curves of Brep b: edgeuses [offsets[b], offsets[b + 1]), numbered
    // from zero within the Brep.
    const auto forEachCurve = [&](const auto &fn) {
        for (size_t b = 0; b + 1 < offsets.size(); ++b) {
            long long local = 0;
            for (long long c = offsets[b]; c < offsets[b + 1]; ++c, ++local) {
                fn(b, local, c);
            }
        }
    };

    if (orders.Truthy() && orders.IsNumbers() && vertexCounts.IsNumbers()) {
        forEachCurve([&](size_t b, long long local, long long c) {
            if (c < 0 || c >= static_cast<long long>(orders.Len())
                || c >= static_cast<long long>(vertexCounts.Len())) {
                return;
            }
            const double order = orders.Num(static_cast<size_t>(c));
            const double vc = vertexCounts.Num(static_cast<size_t>(c));
            if (order == 0 && vc == 0) {
                return;
            }
            if (order <= 0) {
                _Fail("BA.380", TfStringPrintf(
                    "Invalid %sorder %s for curveUv #%lld in brep #%zu. Order "
                    "must be positive.",
                    base.c_str(), orders.Repr(static_cast<size_t>(c)).c_str(),
                    local, b));
            } else if (order > vc) {
                _Fail("BA.385", TfStringPrintf(
                    "Invalid %sorder %s for curveUv #%lld in brep #%zu. Order "
                    "must not exceed vertexCount %s.",
                    base.c_str(), orders.Repr(static_cast<size_t>(c)).c_str(),
                    local, b,
                    vertexCounts.Repr(static_cast<size_t>(c)).c_str()));
            }
        });
    }

    if (vertexCounts.Len() > 0 && !vertexCounts.IsNumbers()) {
        _Fail("BA.416", base + "vertexCount contains non-integer data; "
                               "expected uint[].");
        return;
    }
    const double expectedTotal = _Sum(vertexCounts);
    const std::string expectedRepr
        = _PyValue::NumRepr(expectedTotal, vertexCounts.Integral()
                                               || vertexCounts.Len() == 0);
    if (!controlVertices.IsSequence()) {
        _Fail("BA.390", TfStringPrintf(
            "%scontrolVertices is not a valid sequence; expected size %s.",
            base.c_str(), expectedRepr.c_str()));
    } else if (static_cast<double>(controlVertices.Len()) != expectedTotal) {
        _Fail("BA.390", TfStringPrintf(
            "Invalid size for %scontrolVertices. Expected size %s, but got "
            "%zu.",
            base.c_str(), expectedRepr.c_str(), controlVertices.Len()));
    }

    if (_Get(base + "knots").IsUnregistered()) {
        _Fail("BA.416", base + "knots has an unregistered USD type; expected "
                               "double[].");
        return;
    }
    const _PyValue knots = _Get(base + "knots").OrEmpty();
    if (knots.Truthy() && knots.IsNumbers() && vertexCounts.IsNumbers()
        && orders.IsNumbers()) {
        long long offset = 0;
        forEachCurve([&](size_t b, long long local, long long c) {
            if (c < 0 || c >= static_cast<long long>(vertexCounts.Len())
                || c >= static_cast<long long>(orders.Len())) {
                return;
            }
            const double vc = vertexCounts.Num(static_cast<size_t>(c));
            const double order = orders.Num(static_cast<size_t>(c));
            if (vc == 0 && order == 0) {
                return;
            }
            const long long expected = static_cast<long long>(vc + order);
            const auto slice = _PySlice(offset, offset + expected, knots.Len());
            const long long got
                = static_cast<long long>(slice.second - slice.first);
            if (got != expected) {
                _Fail("BA.395", TfStringPrintf(
                    "Invalid knot count for curveUv #%lld in brep #%zu. "
                    "Expected %lld, but got %lld.",
                    local, b, expected, got));
            }
            if (_PyKnotsDecrease(knots, slice.first, slice.second)) {
                _Fail("BA.400", TfStringPrintf(
                    "Invalid knot ordering for curveUv #%lld in brep #%zu. "
                    "Knots must be non-decreasing.",
                    local, b));
            }
            offset += expected;
        });
        if (offset != static_cast<long long>(knots.Len())) {
            const size_t brep = offsets.size() >= 2 ? offsets.size() - 2 : 0;
            _Fail("BA.395", TfStringPrintf(
                "Invalid packed knot count through brep #%zu. Expected %lld, "
                "but got %zu.",
                brep, offset, knots.Len()));
        }
    }

    if (_Get(base + "weights").IsUnregistered()) {
        _Fail("BA.416", base + "weights has an unregistered USD type; expected "
                               "double[].");
        return;
    }
    const _PyValue weights = _Get(base + "weights").NoneToEmpty();
    if (!weights.IsSequence()) {
        _Fail("BA.405", TfStringPrintf(
            "%sweights is not a valid sequence; expected size %s.",
            base.c_str(), expectedRepr.c_str()));
    } else if (static_cast<double>(weights.Len()) != expectedTotal) {
        _Fail("BA.405", TfStringPrintf(
            "Invalid size for %sweights. Expected size %s, but got %zu.",
            base.c_str(), expectedRepr.c_str(), weights.Len()));
    }
    if (weights.Len() > 0 && weights.IsNumbers() && vertexCounts.IsNumbers()) {
        long long offset = 0;
        forEachCurve([&](size_t b, long long, long long c) {
            if (c < 0 || c >= static_cast<long long>(vertexCounts.Len())) {
                return;
            }
            const long long vc = static_cast<long long>(
                vertexCounts.Num(static_cast<size_t>(c)));
            const auto slice = _PySlice(offset, offset + vc, weights.Len());
            for (size_t i = slice.first; i < slice.second; ++i) {
                if (weights.Num(i) < _PyNumericalTolerance) {
                    _Fail("BA.410", TfStringPrintf(
                        "Invalid weight at index #%zu in brep #%zu. Weights "
                        "must be positive.",
                        i - slice.first, b));
                }
            }
            offset += vc;
        });
    }
}

// _validate_surface_control_vertices_weights: BA.420 the u and v vertex counts
// align, BA.435 weights and control vertices hold sum(u * v) entries, and
// BA.440 each weight of each Brep's control-vertex span is positive.
// Unreadable values are BA.471 and end the rule.
void
_BrepChecker::ValidateSurfaceControlVerticesWeights()
{
    const std::string base = "brep:surface:nurb:";
    const _PyValue &uRaw = _Get(base + "vVertexCount");
    const _PyValue &vRaw = _Get(base + "uVertexCount");
    if (uRaw.IsUnregistered() || vRaw.IsUnregistered()) {
        _Fail("BA.471", base + "uVertexCount/vVertexCount has an unregistered "
                               "USD type; expected uint[].");
        return;
    }
    if (_Get(base + "weights").IsUnregistered()) {
        _Fail("BA.471", base + "weights has an unregistered USD type; "
                               "expected double[].");
        return;
    }
    const _PyValue weights = _Get(base + "weights").NoneToEmpty();
    const _PyValue controlVertices
        = _Get(base + "controlVertices").NoneToEmpty();
    const _PyValue u = uRaw.NoneToEmpty();
    const _PyValue v = vRaw.NoneToEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();

    // sum(u * v for u, v in zip(u, v, strict=True)): a non-sequence fails
    // the zip (BA.471), unequal lengths fail strict (BA.420), non-numeric
    // entries fail the arithmetic (BA.471).
    if (!u.IsSequence() || !v.IsSequence()) {
        _Fail("BA.471", base + "uVertexCount/vVertexCount contain invalid or "
                               "non-integer data; expected uint[] values to "
                               "compute control vertex count.");
        return;
    }
    if (u.Len() != v.Len()) {
        _Fail("BA.420", base + "uVertexCount and vVertexCount have mismatched "
                               "lengths; expected aligned per-surface counts.");
        return;
    }
    if (u.Len() > 0 && (!u.IsNumbers() || !v.IsNumbers())) {
        _Fail("BA.471", base + "uVertexCount/vVertexCount contain invalid or "
                               "non-integer data; expected uint[] values to "
                               "compute control vertex count.");
        return;
    }
    double expected = 0.0;
    for (size_t i = 0; i < u.Len(); ++i) {
        expected += u.Num(i) * v.Num(i);
    }
    if (expected > 0 || weights.Len() > 0) {
        _ValidateArraySizesAndAuthored(
            { base + "weights", base + "controlVertices" }, "BA.435",
            &expected, true);
    }
    if (controlVertices.IsUnregistered()) {
        _Fail("BA.471", base + "controlVertices has an unregistered USD type; "
                               "expected point3d[].");
        return;
    }
    if (!weights.IsSequence()) {
        _Fail("BA.471", base + "weights is not a valid sequence; expected "
                               "double[].");
        return;
    }
    const std::string expectedRepr = _PyValue::NumRepr(expected, true);
    if (!controlVertices.IsSequence()) {
        _Fail("BA.435", TfStringPrintf(
            "%scontrolVertices is not a valid sequence; expected size %s.",
            base.c_str(), expectedRepr.c_str()));
    } else if (expected != 0
               && static_cast<double>(controlVertices.Len()) != expected) {
        _Fail("BA.435", TfStringPrintf(
            "Invalid size for %scontrolVertices. Expected %s, but got %zu.",
            base.c_str(), expectedRepr.c_str(), controlVertices.Len()));
    }
    if (!weights.Truthy() || numBreps == 0 || !weights.IsNumbers()) {
        return;
    }
    const std::vector<long long> &offsets = _Offsets().surfaceControlVertices;
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= offsets.size()) {
            break;
        }
        const long long end
            = std::min(offsets[b + 1], static_cast<long long>(weights.Len()));
        for (long long w = offsets[b]; w < end; ++w) {
            size_t i = 0;
            if (_PyIndex(w, weights.Len(), &i)
                && weights.Num(i) < _PyNumericalTolerance) {
                _Fail("BA.440", TfStringPrintf(
                    "Invalid weight at index #%lld in brep #%zu. Weights in "
                    "%sweights must be positive.",
                    w, b, base.c_str()));
            }
        }
    }
}

// _validate_surface_orders_vertex_counts: BA.420 the four order / vertex-count
// arrays hold one entry per BrepSurfaceNurbAPI face (checked when such faces
// exist or uOrder is authored), then per Brep surface span BA.425 both orders
// positive (a surface failing it skips BA.430) and BA.430 neither exceeding
// its vertex count.
void
_BrepChecker::ValidateSurfaceOrdersVertexCounts()
{
    const std::string base = "brep:surface:nurb:";
    const _PyValue types = _Get("face:surfaceType").OrEmpty();
    double nurbsFaces = 0;
    for (size_t i = 0; i < types.Len(); ++i) {
        nurbsFaces += types.Equals(i, "BrepSurfaceNurbAPI") ? 1 : 0;
    }
    if (nurbsFaces > 0 || _Get(base + "uOrder").OrEmpty().Len() > 0) {
        _ValidateArraySizesAndAuthored(
            { base + "uVertexCount", base + "vVertexCount", base + "uOrder",
              base + "vOrder" },
            "BA.420", &nurbsFaces, true);
    }
    const std::vector<long long> &offsets = _Offsets().surfaceNurbs;
    const _PyValue uOrder = _Get(base + "uOrder").OrEmpty();
    const _PyValue vOrder = _Get(base + "vOrder").OrEmpty();
    const _PyValue uCount = _Get(base + "uVertexCount").OrEmpty();
    const _PyValue vCount = _Get(base + "vVertexCount").OrEmpty();
    if (!uOrder.IsNumbers() || !vOrder.IsNumbers() || !uCount.IsNumbers()
        || !vCount.IsNumbers()) {
        return;
    }
    for (size_t b = 0; b + 1 < offsets.size(); ++b) {
        long long local = 0;
        for (long long s = offsets[b]; s < offsets[b + 1]; ++s, ++local) {
            if (s < 0 || s >= static_cast<long long>(uOrder.Len())
                || s >= static_cast<long long>(vOrder.Len())
                || s >= static_cast<long long>(uCount.Len())
                || s >= static_cast<long long>(vCount.Len())) {
                continue;
            }
            const size_t i = static_cast<size_t>(s);
            if (uOrder.Num(i) <= 0 || vOrder.Num(i) <= 0) {
                _Fail("BA.425", TfStringPrintf(
                    "Invalid order (U: %s, V: %s) for surface #%lld in brep "
                    "#%zu. Orders must be positive.",
                    uOrder.Repr(i).c_str(), vOrder.Repr(i).c_str(), local, b));
                continue;
            }
            if (uOrder.Num(i) > uCount.Num(i) || vOrder.Num(i) > vCount.Num(i)) {
                _Fail("BA.430", TfStringPrintf(
                    "Invalid order (U: %s, V: %s) for surface #%lld in brep "
                    "#%zu. Orders must not exceed vertex counts (U: %s, V: "
                    "%s).",
                    uOrder.Repr(i).c_str(), vOrder.Repr(i).c_str(), local, b,
                    uCount.Repr(i).c_str(), vCount.Repr(i).c_str()));
            }
        }
    }
}

// _validate_surface_knots: per Brep surface span, each surface's slices of
// uKnots and vKnots hold vertexCount + order knots (BA.445 / BA.450) and do
// not decrease (BA.455 / BA.460).
void
_BrepChecker::ValidateSurfaceKnots()
{
    const std::string base = "brep:surface:nurb:";
    const _PyValue uKnots = _Get(base + "uKnots").OrEmpty();
    const _PyValue vKnots = _Get(base + "vKnots").OrEmpty();
    const _PyValue uCount = _Get(base + "uVertexCount").OrEmpty();
    const _PyValue vCount = _Get(base + "vVertexCount").OrEmpty();
    const _PyValue uOrder = _Get(base + "uOrder").OrEmpty();
    const _PyValue vOrder = _Get(base + "vOrder").OrEmpty();
    const std::vector<long long> &offsets = _Offsets().surfaceNurbs;
    if (!uCount.IsNumbers() || !vCount.IsNumbers() || !uOrder.IsNumbers()
        || !vOrder.IsNumbers()) {
        return;
    }
    const bool uNumeric = uKnots.IsNumbers();
    const bool vNumeric = vKnots.IsNumbers();
    long long uOffset = 0, vOffset = 0;
    for (size_t b = 0; b + 1 < offsets.size(); ++b) {
        long long local = 0;
        for (long long s = offsets[b]; s < offsets[b + 1]; ++s, ++local) {
            if (s < 0 || s >= static_cast<long long>(uCount.Len())
                || s >= static_cast<long long>(vCount.Len())
                || s >= static_cast<long long>(uOrder.Len())
                || s >= static_cast<long long>(vOrder.Len())) {
                continue;
            }
            const size_t i = static_cast<size_t>(s);
            const long long expectedU
                = static_cast<long long>(uCount.Num(i) + uOrder.Num(i));
            const long long expectedV
                = static_cast<long long>(vCount.Num(i) + vOrder.Num(i));
            const auto uSlice = _PySlice(uOffset, uOffset + expectedU,
                                         uNumeric ? uKnots.Len() : 0);
            const auto vSlice = _PySlice(vOffset, vOffset + expectedV,
                                         vNumeric ? vKnots.Len() : 0);
            const long long gotU
                = static_cast<long long>(uSlice.second - uSlice.first);
            const long long gotV
                = static_cast<long long>(vSlice.second - vSlice.first);
            if (gotU != expectedU) {
                _Fail("BA.445", TfStringPrintf(
                    "Invalid knot count for surface #%lld in brep #%zu in U "
                    "direction. Expected %lld knots in brep:surface:nurbs:uKnots "
                    "for this slice, but got %lld.",
                    local, b, expectedU, gotU));
            }
            if (gotV != expectedV) {
                _Fail("BA.450", TfStringPrintf(
                    "Invalid knot count for surface #%lld in brep #%zu in V "
                    "direction. Expected %lld knots in brep:surface:nurbs:vKnots "
                    "for this slice, but got %lld.",
                    local, b, expectedV, gotV));
            }
            if (uNumeric
                && _PyKnotsDecrease(uKnots, uSlice.first, uSlice.second)) {
                _Fail("BA.455", TfStringPrintf(
                    "Invalid knot ordering detected in U knot vector for "
                    "surface #%lld in brep #%zu. Values must be "
                    "non-decreasing.",
                    local, b));
            }
            if (vNumeric
                && _PyKnotsDecrease(vKnots, vSlice.first, vSlice.second)) {
                _Fail("BA.460", TfStringPrintf(
                    "Invalid knot ordering detected in V knot vector for "
                    "surface #%lld in brep #%zu. Values must be "
                    "non-decreasing.",
                    local, b));
            }
            uOffset += expectedU;
            vOffset += expectedV;
        }
    }
}

// _validate_required_geometry_apis (BA.583): a curve or surface type used in
// edge:curveType, wireEdge:curveType or face:surfaceType, a BrepPointAPI
// vertex or point shell, and any authored UV pcurve data each require the
// matching applied API schema.
void
_BrepChecker::_ValidateRequiredGeometryApis()
{
    const TfTokenVector applied = _prim.GetAppliedSchemas();
    const auto isApplied = [&](const std::string &api) {
        return std::find(applied.begin(), applied.end(), TfToken(api))
            != applied.end();
    };
    struct Use
    {
        std::string api, attr, token;
        size_t count;
    };
    std::vector<Use> uses;
    static const char *const requirements[][3] = {
        { "vertex:pointType", "BrepPointAPI", "BrepPointAPI:vertexPoint" },
        { "edge:curveType", "BrepCurve3dNurbAPI",
          "BrepCurve3dNurbAPI:edge3dNurb" },
        { "edge:curveType", "BrepCurve3dLineAPI",
          "BrepCurve3dLineAPI:edge3dLine" },
        { "edge:curveType", "BrepCurve3dCircleAPI",
          "BrepCurve3dCircleAPI:edge3dCircle" },
        { "edge:curveType", "BrepCurve3dEllipseAPI",
          "BrepCurve3dEllipseAPI:edge3dEllipse" },
        { "wireEdge:curveType", "BrepCurve3dNurbAPI",
          "BrepCurve3dNurbAPI:wireEdge3dNurb" },
        { "wireEdge:curveType", "BrepCurve3dLineAPI",
          "BrepCurve3dLineAPI:wireEdge3dLine" },
        { "wireEdge:curveType", "BrepCurve3dCircleAPI",
          "BrepCurve3dCircleAPI:wireEdge3dCircle" },
        { "wireEdge:curveType", "BrepCurve3dEllipseAPI",
          "BrepCurve3dEllipseAPI:wireEdge3dEllipse" },
        { "face:surfaceType", "BrepSurfaceNurbAPI", "BrepSurfaceNurbAPI" },
        { "face:surfaceType", "BrepSurfacePlaneAPI", "BrepSurfacePlaneAPI" },
        { "face:surfaceType", "BrepSurfaceCylinderAPI",
          "BrepSurfaceCylinderAPI" },
        { "face:surfaceType", "BrepSurfaceConeAPI", "BrepSurfaceConeAPI" },
        { "face:surfaceType", "BrepSurfaceSphereAPI", "BrepSurfaceSphereAPI" },
        { "face:surfaceType", "BrepSurfaceTorusAPI", "BrepSurfaceTorusAPI" },
    };
    for (const auto &r : requirements) {
        const _PyValue values = _SafeGet(r[0]);
        size_t count = 0;
        for (size_t i = 0; i < values.Len(); ++i) {
            count += values.Equals(i, r[1]) ? 1 : 0;
        }
        if (count > 0) {
            uses.push_back({ r[2], r[0], r[1], count });
        }
    }
    const _PyValue pointTypes = _SafeGet("shell:pointType");
    const _PyValue faceuseCounts = _SafeGet("shell:faceuseCount");
    const _PyValue wireEdgeCounts = _SafeGet("shell:wireEdgeCount");
    size_t pointShells = 0;
    for (size_t i = 0; i < pointTypes.Len(); ++i) {
        long long fu = 0, we = 0;
        if (pointTypes.Equals(i, "BrepPointAPI") && i < faceuseCounts.Len()
            && i < wireEdgeCounts.Len() && faceuseCounts.ToInt(i, &fu)
            && wireEdgeCounts.ToInt(i, &we) && fu == 0 && we == 0) {
            ++pointShells;
        }
    }
    if (pointShells > 0) {
        uses.push_back({ "BrepPointAPI:shellPoint", "shell:pointType",
                         "BrepPointAPI", pointShells });
    }
    for (const Use &use : uses) {
        if (!isApplied(use.api)) {
            _Fail("BA.583", TfStringPrintf(
                "%s contains %zu '%s' occurrence(s), but required applied "
                "geometry API '%s' is absent from apiSchemas.",
                use.attr.c_str(), use.count, use.token.c_str(),
                use.api.c_str()));
        }
    }

    const auto anyNonZero = [](const _PyValue &values) {
        for (size_t i = 0; i < values.Len(); ++i) {
            if (!values.IsNumbers() || values.Num(i) != 0) {
                return true;
            }
        }
        return false;
    };
    const bool hasRecord = anyNonZero(_SafeGet("brep:curveUv:nurb:order"))
        || anyNonZero(_SafeGet("brep:curveUv:nurb:vertexCount"));
    const bool hasPacked
        = _SafeGet("brep:curveUv:nurb:controlVertices").Len() > 0
        || _SafeGet("brep:curveUv:nurb:knots").Len() > 0
        || _SafeGet("brep:curveUv:nurb:weights").Len() > 0;
    if ((hasRecord || hasPacked) && !isApplied("BrepCurveUvNurbAPI")) {
        _Fail("BA.583", "Authored UV NURBS pcurve data requires applied "
                        "geometry API 'BrepCurveUvNurbAPI', but it is absent "
                        "from apiSchemas.");
    }
}

// _validate_schema_consistency: BA.583 (above), then for each NURBS and
// analytic family that the topology uses or the prim applies, both
// directions of "used means data is authored" and "applied means used"
// (BA.370 edge3d NURBS, BA.290 wireEdge3d NURBS, BA.415 UV pcurves, BA.470
// surface NURBS, BA.485 / 495 / 505 / 516 / 526 the analytic surfaces, BA.305
// vertex points). Presence of data is tested on one attribute per family.
void
_BrepChecker::ValidateSchemaConsistency()
{
    _ValidateRequiredGeometryApis();
    const TfTokenVector applied = _prim.GetAppliedSchemas();
    const auto isApplied = [&](const char *api) {
        return std::find(applied.begin(), applied.end(), TfToken(api))
            != applied.end();
    };
    const auto countToken = [&](const char *attr, const char *token) {
        const _PyValue values = _Get(attr).OrEmpty();
        size_t count = 0;
        for (size_t i = 0; i < values.Len(); ++i) {
            count += values.Equals(i, token) ? 1 : 0;
        }
        return count;
    };
    const auto hasData = [&](const char *attr) {
        return _Get(attr).OrEmpty().Truthy();
    };

    struct Family
    {
        const char *rule, *api, *typeAttr, *typeToken, *dataAttr;
        const char *usedDesc;   // "edges with edge:curveType='...'"
        const char *dataDesc;   // "brep:edge3dNurb:curve3d:nurb NURBS data"
        const char *notUsedDesc;
    };
    static const Family families[] = {
        { "BA.370", "BrepCurve3dNurbAPI:edge3dNurb", "edge:curveType",
          "BrepCurve3dNurbAPI", "brep:edge3dNurb:curve3d:nurb:order",
          "edges with edge:curveType='BrepCurve3dNurbAPI'",
          "brep:edge3dNurb:curve3d:nurb NURBS data",
          "no edges use edge:curveType='BrepCurve3dNurbAPI'" },
        { "BA.290", "BrepCurve3dNurbAPI:wireEdge3dNurb", "wireEdge:curveType",
          "BrepCurve3dNurbAPI", "brep:wireEdge3dNurb:curve3d:nurb:order",
          "wireEdges with wireEdge:curveType='BrepCurve3dNurbAPI'",
          "brep:wireEdge3dNurb:curve3d:nurb NURBS data",
          "no wireEdges use wireEdge:curveType='BrepCurve3dNurbAPI'" },
    };
    for (const Family &f : families) {
        const size_t used = countToken(f.typeAttr, f.typeToken);
        if (used > 0 && !hasData(f.dataAttr)) {
            _Fail(f.rule, UsdSolidValidationErrorNameTokens
                              ->nurbSchemaDataIncomplete,
                  TfStringPrintf("Found %zu %s but no %s is authored.", used,
                                 f.usedDesc, f.dataDesc));
        }
        if (isApplied(f.api) && used == 0) {
            _Fail(f.rule, TfStringPrintf("%s appears in apiSchemas but %s.",
                                         f.api, f.notUsedDesc));
        }
    }

    // BA.415: UV pcurves, used by edgeuses rather than by a type token.
    {
        const bool appliedUv = isApplied("BrepCurveUvNurbAPI");
        const size_t edgeuses = _Get("edgeuse:edgeIndex").OrEmpty().Len();
        if (edgeuses > 0 && appliedUv
            && !hasData("brep:curveUv:nurb:order")) {
            _Fail("BA.415", UsdSolidValidationErrorNameTokens
                                ->nurbSchemaDataIncomplete,
                  TfStringPrintf("Found %zu edgeuses but BrepCurveUvNurbAPI is "
                                 "in apiSchemas and no brep:curveUv:nurb NURBS "
                                 "data is authored.",
                                 edgeuses));
        }
        if (appliedUv && edgeuses == 0) {
            _Fail("BA.415", "BrepCurveUvNurbAPI appears in apiSchemas but no "
                            "edgeuses exist.");
        }
    }

    static const Family surfaces[] = {
        { "BA.470", "BrepSurfaceNurbAPI", "face:surfaceType",
          "BrepSurfaceNurbAPI", "brep:surface:nurb:uOrder",
          "faces with face:surfaceType='BrepSurfaceNurbAPI'",
          "brep:surface:nurb NURBS data",
          "no faces use face:surfaceType='BrepSurfaceNurbAPI'" },
        { "BA.485", "BrepSurfaceSphereAPI", "face:surfaceType",
          "BrepSurfaceSphereAPI", "brep:surface:sphere:center",
          "faces with face:surfaceType='BrepSurfaceSphereAPI'",
          "brep:surface:sphere data",
          "no faces use face:surfaceType='BrepSurfaceSphereAPI'" },
        { "BA.495", "BrepSurfacePlaneAPI", "face:surfaceType",
          "BrepSurfacePlaneAPI", "brep:surface:plane:origin",
          "faces with face:surfaceType='BrepSurfacePlaneAPI'",
          "brep:surface:plane data",
          "no faces use face:surfaceType='BrepSurfacePlaneAPI'" },
        { "BA.505", "BrepSurfaceCylinderAPI", "face:surfaceType",
          "BrepSurfaceCylinderAPI", "brep:surface:cylinder:origin",
          "faces with face:surfaceType='BrepSurfaceCylinderAPI'",
          "brep:surface:cylinder data",
          "no faces use face:surfaceType='BrepSurfaceCylinderAPI'" },
        { "BA.516", "BrepSurfaceConeAPI", "face:surfaceType",
          "BrepSurfaceConeAPI", "brep:surface:cone:origin",
          "faces with face:surfaceType='BrepSurfaceConeAPI'",
          "brep:surface:cone data",
          "no faces use face:surfaceType='BrepSurfaceConeAPI'" },
        { "BA.526", "BrepSurfaceTorusAPI", "face:surfaceType",
          "BrepSurfaceTorusAPI", "brep:surface:torus:origin",
          "faces with face:surfaceType='BrepSurfaceTorusAPI'",
          "brep:surface:torus data",
          "no faces use face:surfaceType='BrepSurfaceTorusAPI'" },
        { "BA.305", "BrepPointAPI:vertexPoint", "vertex:pointType",
          "BrepPointAPI", "brep:vertexPoint:point:position",
          "vertices with vertex:pointType='BrepPointAPI'",
          "brep:vertexPoint:point:position data",
          "no vertices use vertex:pointType='BrepPointAPI'" },
    };
    for (const Family &f : surfaces) {
        const size_t used = countToken(f.typeAttr, f.typeToken);
        if (used > 0 && !hasData(f.dataAttr)) {
            _Fail(f.rule,
                  std::string(f.rule) == "BA.470"
                      ? UsdSolidValidationErrorNameTokens
                            ->nurbSchemaDataIncomplete
                      : _RuleErrorName(f.rule),
                  TfStringPrintf("Found %zu %s but no %s is authored.", used,
                                 f.usedDesc, f.dataDesc));
        }
        if (isApplied(f.api) && used == 0) {
            _Fail(f.rule, TfStringPrintf("%s appears in apiSchemas but %s.",
                                         f.api, f.notUsedDesc));
        }
    }
}

// _validate_nurbs_data_completeness (BA.370): a Brep whose edge partition
// holds a BrepCurve3dNurbAPI edge needs all five edge3dNurb arrays non-empty.
void
_BrepChecker::ValidateNurbsDataCompleteness()
{
    const _PyValue types = _Get("edge:curveType").OrEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (!types.Truthy() || numBreps == 0) {
        return;
    }
    const std::string base = "brep:edge3dNurb:curve3d:nurb:";
    static const char *const parts[5]
        = { "order", "vertexCount", "controlVertices", "weights", "knots" };
    std::vector<std::string> missing;
    for (const char *part : parts) {
        if (!_Get(base + part).OrEmpty().Truthy()) {
            missing.push_back(part);
        }
    }
    const std::vector<long long> &edges = _Offsets().edges;
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= edges.size()) {
            break;
        }
        size_t nurbs = 0;
        const long long end
            = std::min(edges[b + 1], static_cast<long long>(types.Len()));
        for (long long e = edges[b]; e < end; ++e) {
            size_t i = 0;
            if (_PyIndex(e, types.Len(), &i)
                && types.Equals(i, "BrepCurve3dNurbAPI")) {
                ++nurbs;
            }
        }
        if (nurbs > 0 && !missing.empty()) {
            _Fail("BA.370", UsdSolidValidationErrorNameTokens
                                ->nurbSchemaDataIncomplete,
                  TfStringPrintf("NURBS curve data is incomplete for brep #%zu. "
                                 "Missing arrays: %s. Required for %zu edges "
                                 "with BrepCurve3dNurbAPI.",
                                 b, _PyNameList(missing).c_str(), nurbs));
        }
    }
}

// _validate_nurbs_mathematical_consistency: a second pass over the edge3d and
// surface records per Brep span -- BA.340 edge order <= vertexCount, BA.355
// the packed knots still hold each curve's vertexCount + order, BA.430 each
// surface order <= its vertex count (U and V reported separately). Python
// reports these alongside the per-rule checks, so a file can carry both.
void
_BrepChecker::ValidateNurbsMathematicalConsistency()
{
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (numBreps == 0) {
        return;
    }
    const _PyOffsets &o = _Offsets();
    const std::string e = "brep:edge3dNurb:curve3d:nurb:";
    const _PyValue orders = _Get(e + "order").OrEmpty();
    const _PyValue vertexCounts = _Get(e + "vertexCount").OrEmpty();
    const _PyValue knots = _Get(e + "knots").OrEmpty();
    const bool edgeNumeric = orders.IsNumbers() && vertexCounts.IsNumbers();

    const auto forEachCurve = [&](const auto &fn) {
        for (size_t b = 0; b < numBreps; ++b) {
            if (b + 1 >= o.edge3dNurbsCurves.size()) {
                break;
            }
            const long long end = std::min(
                { o.edge3dNurbsCurves[b + 1],
                  static_cast<long long>(orders.Len()),
                  static_cast<long long>(vertexCounts.Len()) });
            for (long long c = o.edge3dNurbsCurves[b]; c < end; ++c) {
                size_t i = 0, j = 0;
                if (_PyIndex(c, orders.Len(), &i)
                    && _PyIndex(c, vertexCounts.Len(), &j)) {
                    fn(b, c, i, j);
                }
            }
        }
    };
    if (orders.Truthy() && vertexCounts.Truthy() && edgeNumeric) {
        forEachCurve([&](size_t b, long long c, size_t i, size_t j) {
            if (orders.Num(i) > vertexCounts.Num(j)) {
                _Fail("BA.340", TfStringPrintf(
                    "Edge NURBS curve #%lld in brep #%zu: order (%s) must be "
                    "<= vertexCount (%s).",
                    c, b, orders.Repr(i).c_str(),
                    vertexCounts.Repr(j).c_str()));
            }
        });
    }
    if (orders.Truthy() && vertexCounts.Truthy() && knots.Truthy()
        && edgeNumeric) {
        long long offset = 0;
        const long long numKnots = static_cast<long long>(knots.Len());
        forEachCurve([&](size_t b, long long c, size_t i, size_t j) {
            const long long expected = static_cast<long long>(
                orders.Num(i) + vertexCounts.Num(j));
            if (offset + expected > numKnots) {
                _Fail("BA.355", TfStringPrintf(
                    "Edge NURBS curve #%lld in brep #%zu: insufficient knots. "
                    "Expected %lld, but only %lld remaining.",
                    c, b, expected, numKnots - offset));
            }
            offset += expected;
        });
    }

    const std::string s = "brep:surface:nurb:";
    const _PyValue uOrder = _Get(s + "uOrder").OrEmpty();
    const _PyValue vOrder = _Get(s + "vOrder").OrEmpty();
    const _PyValue uCount = _Get(s + "uVertexCount").OrEmpty();
    const _PyValue vCount = _Get(s + "vVertexCount").OrEmpty();
    if (!uOrder.Truthy() || !uCount.Truthy() || !vOrder.Truthy()
        || !vCount.Truthy() || !uOrder.IsNumbers() || !vOrder.IsNumbers()
        || !uCount.IsNumbers() || !vCount.IsNumbers()) {
        return;
    }
    for (size_t b = 0; b < numBreps; ++b) {
        if (b + 1 >= o.surfaceNurbs.size()) {
            break;
        }
        const long long end = std::min(
            { o.surfaceNurbs[b + 1], static_cast<long long>(uOrder.Len()),
              static_cast<long long>(vOrder.Len()),
              static_cast<long long>(uCount.Len()),
              static_cast<long long>(vCount.Len()) });
        for (long long f = o.surfaceNurbs[b]; f < end; ++f) {
            if (f < 0) {
                continue;
            }
            const size_t i = static_cast<size_t>(f);
            if (uOrder.Num(i) > uCount.Num(i)) {
                _Fail("BA.430", TfStringPrintf(
                    "Surface NURBS #%lld in brep #%zu: uOrder (%s) must be <= "
                    "uVertexCount (%s).",
                    f, b, uOrder.Repr(i).c_str(), uCount.Repr(i).c_str()));
            }
            if (vOrder.Num(i) > vCount.Num(i)) {
                _Fail("BA.430", TfStringPrintf(
                    "Surface NURBS #%lld in brep #%zu: vOrder (%s) must be <= "
                    "vVertexCount (%s).",
                    f, b, vOrder.Repr(i).c_str(), vCount.Repr(i).c_str()));
            }
        }
    }
}

// _validate_nurbs_order_and_vertex_count_values: for each authored order
// array (edge3d, surface U, surface V, curveUv, wireEdge3d), BA.590 the first
// order below 2 and BA.591 the first vertexCount below its order. The curveUv
// 0/0 record ("no pcurve") is exempt from both.
void
_BrepChecker::ValidateNurbsOrderAndVertexCountValues()
{
    struct Pair
    {
        const char *order, *count, *label;
        bool zeroSentinel;
    };
    static const Pair pairs[] = {
        { "brep:edge3dNurb:curve3d:nurb:order",
          "brep:edge3dNurb:curve3d:nurb:vertexCount", "edge3dNurb", false },
        { "brep:surface:nurb:uOrder", "brep:surface:nurb:uVertexCount",
          "surface U", false },
        { "brep:surface:nurb:vOrder", "brep:surface:nurb:vVertexCount",
          "surface V", false },
        { "brep:curveUv:nurb:order", "brep:curveUv:nurb:vertexCount",
          "curveUv", true },
    };
    for (const Pair &p : pairs) {
        if (!_IsAuthored(p.order)) {
            continue;
        }
        const _PyValue orders = _Get(p.order);
        const _PyValue counts
            = _IsAuthored(p.count) ? _Get(p.count) : _PyValue();
        if (!orders.Truthy() || !orders.IsNumbers()) {
            continue;
        }
        const bool countsUsable = counts.Truthy() && counts.IsNumbers();
        for (size_t i = 0; i < orders.Len(); ++i) {
            if (p.zeroSentinel && orders.Num(i) == 0 && countsUsable
                && i < counts.Len() && counts.Num(i) == 0) {
                continue;
            }
            if (orders.Num(i) < 2) {
                _Fail("BA.590", TfStringPrintf(
                    "%s[%zu] = %s is less than 2 (minimum for %s).", p.order, i,
                    orders.Repr(i).c_str(), p.label));
                break;
            }
        }
        if (countsUsable && counts.Len() == orders.Len()) {
            for (size_t i = 0; i < orders.Len(); ++i) {
                if (p.zeroSentinel && orders.Num(i) == 0
                    && counts.Num(i) == 0) {
                    continue;
                }
                if (counts.Num(i) < orders.Num(i)) {
                    _Fail("BA.591", TfStringPrintf(
                        "%s[%zu] = %s is less than %s[%zu] = %s for %s.",
                        p.count, i, counts.Repr(i).c_str(), p.order, i,
                        orders.Repr(i).c_str(), p.label));
                    break;
                }
            }
        }
    }

    const char *weOrderName = "brep:wireEdge3dNurb:curve3d:nurb:order";
    const char *weCountName = "brep:wireEdge3dNurb:curve3d:nurb:vertexCount";
    if (!_IsAuthored(weOrderName)) {
        return;
    }
    const _PyValue orders = _Get(weOrderName);
    const _PyValue counts
        = _IsAuthored(weCountName) ? _Get(weCountName) : _PyValue();
    if (!orders.Truthy() || !orders.IsNumbers()) {
        return;
    }
    for (size_t i = 0; i < orders.Len(); ++i) {
        if (orders.Num(i) < 2) {
            _Fail("BA.590", TfStringPrintf("%s[%zu] = %s is less than 2 "
                                           "(minimum for wireEdge3dNurb).",
                                           weOrderName, i,
                                           orders.Repr(i).c_str()));
            break;
        }
    }
    if (counts.Truthy() && counts.IsNumbers()
        && counts.Len() == orders.Len()) {
        for (size_t i = 0; i < orders.Len(); ++i) {
            if (counts.Num(i) < orders.Num(i)) {
                _Fail("BA.591", TfStringPrintf(
                    "%s[%zu] = %s is less than order[%zu] = %s for "
                    "wireEdge3dNurb.",
                    weCountName, i, counts.Repr(i).c_str(), i,
                    orders.Repr(i).c_str()));
                break;
            }
        }
    }
}

// _validate_wireEdge3d_nurbs (BA.650-BA.658), for a BrepArray whose
// wireEdge:curveType names a NURBS curve: the five wireEdge3dNurb arrays are
// all non-empty (BA.658, which ends the rule otherwise), order and vertexCount
// hold one entry per NURBS wire edge (BA.650), every order is >= 2 (BA.651)
// and <= its vertexCount (BA.652), control vertices and weights hold
// sum(vertexCount) entries (BA.653), the first non-positive weight (BA.654),
// the knots hold sum(vertexCount + order) entries (BA.655), the first
// decrease within each knot vector (BA.656), and the first control vertex
// outside every Brep's extent (BA.657).
void
_BrepChecker::ValidateWireEdge3dNurbs()
{
    const _PyValue types = _SafeGet("wireEdge:curveType");
    size_t nurbs = 0;
    for (size_t i = 0; i < types.Len(); ++i) {
        nurbs += types.Equals(i, "BrepCurve3dNurbAPI") ? 1 : 0;
    }
    if (nurbs == 0) {
        return;
    }
    const std::string base = "brep:wireEdge3dNurb:curve3d:nurb:";
    const _PyValue order = _SafeGet(base + "order");
    const _PyValue count = _SafeGet(base + "vertexCount");
    const _PyValue cvs = _SafeGet(base + "controlVertices");
    const _PyValue weights = _SafeGet(base + "weights");
    const _PyValue knots = _SafeGet(base + "knots");

    const std::pair<const char *, bool> present[5]
        = { { "order", order.Len() > 0 },
            { "vertexCount", count.Len() > 0 },
            { "controlVertices", cvs.Len() > 0 },
            { "weights", weights.Len() > 0 },
            { "knots", knots.Len() > 0 } };
    std::vector<std::string> have, missing;
    for (const auto &p : present) {
        (p.second ? have : missing).push_back(p.first);
    }
    if (!missing.empty()) {
        _Fail("BA.658", TfStringPrintf("WireEdge NURBS data is incomplete. "
                                       "Present: %s. Missing: %s.",
                                       _PyNameList(have).c_str(),
                                       _PyNameList(missing).c_str()));
        return;
    }
    if (order.Len() != nurbs) {
        _Fail("BA.650", TfStringPrintf("wireEdge NURBS order size (%zu) != "
                                       "BrepCurve3dNurbAPI count (%zu).",
                                       order.Len(), nurbs));
    }
    if (count.Len() != nurbs) {
        _Fail("BA.650", TfStringPrintf("wireEdge NURBS vertexCount size (%zu) "
                                       "!= BrepCurve3dNurbAPI count (%zu).",
                                       count.Len(), nurbs));
    }
    const size_t minLen = std::min(order.Len(), count.Len());
    long long expectedCvs = 0, expectedKnots = 0;
    std::vector<long long> orders(minLen), counts(minLen);
    for (size_t i = 0; i < minLen; ++i) {
        order.ToInt(i, &orders[i]);
        count.ToInt(i, &counts[i]);
        if (orders[i] < 2) {
            _Fail("BA.651", TfStringPrintf("wireEdge NURBS order[%zu] = %lld is "
                                           "less than 2.",
                                           i, orders[i]));
        }
        if (orders[i] > counts[i]) {
            _Fail("BA.652", TfStringPrintf("wireEdge NURBS order[%zu] (%lld) "
                                           "exceeds vertexCount[%zu] (%lld).",
                                           i, orders[i], i, counts[i]));
        }
        expectedCvs += counts[i];
        expectedKnots += counts[i] + orders[i];
    }
    if (static_cast<long long>(cvs.Len()) != expectedCvs) {
        _Fail("BA.653", TfStringPrintf("wireEdge NURBS controlVertices size "
                                       "(%zu) != sum of vertexCounts (%lld).",
                                       cvs.Len(), expectedCvs));
    }
    if (static_cast<long long>(weights.Len()) != expectedCvs) {
        _Fail("BA.653", TfStringPrintf("wireEdge NURBS weights size (%zu) != "
                                       "sum of vertexCounts (%lld).",
                                       weights.Len(), expectedCvs));
    }
    for (size_t i = 0; i < weights.Len(); ++i) {
        double w = 0.0;
        if (weights.ToFloat(i, &w) && w <= 0.0) {
            _Fail("BA.654", TfStringPrintf("wireEdge NURBS weights[%zu] = %s "
                                           "is not positive.",
                                           i, weights.Repr(i).c_str()));
            break;
        }
    }
    if (static_cast<long long>(knots.Len()) != expectedKnots) {
        _Fail("BA.655", TfStringPrintf("wireEdge NURBS knots size (%zu) != "
                                       "expected (%lld).",
                                       knots.Len(), expectedKnots));
    }
    long long knotOffset = 0;
    for (size_t i = 0; i < minLen; ++i) {
        const long long knotLen = counts[i] + orders[i];
        if (knotOffset + knotLen > static_cast<long long>(knots.Len())) {
            break;
        }
        for (long long j = 1; j < knotLen; ++j) {
            double a = 0.0, b = 0.0;
            knots.ToFloat(static_cast<size_t>(knotOffset + j), &b);
            knots.ToFloat(static_cast<size_t>(knotOffset + j - 1), &a);
            if (b < a) {
                _Fail("BA.656", TfStringPrintf("wireEdge NURBS knot vector #%zu "
                                               "is not non-decreasing at "
                                               "position %lld.",
                                               i, j));
                break;
            }
        }
        knotOffset += knotLen;
    }

    const _PyValue extent = _SafeGet("brep:extent");
    if (extent.Len() < 2 || cvs.Len() == 0 || !extent.IsTuples()
        || !cvs.IsTuples() || cvs.Dim() < 3) {
        return;
    }
    const size_t numBoxes = extent.Len() / 2;
    const size_t numRegionCounts = _SafeGet("brep:regionCount").Len();
    const size_t numBreps = std::min(
        numRegionCounts > 0 ? numRegionCounts : numBoxes, numBoxes);
    for (size_t c = 0; c < cvs.Len(); ++c) {
        bool inside = false;
        for (size_t b = 0; b < numBreps && !inside; ++b) {
            if (extent.Dim() < 3) {
                continue;
            }
            bool outside = false;
            for (int a = 0; a < 3; ++a) {
                outside = outside
                    || _PyIsFloatLessThan(cvs.Tup(c, a), extent.Tup(2 * b, a))
                    || _PyIsFloatGreaterThan(cvs.Tup(c, a),
                                             extent.Tup(2 * b + 1, a));
            }
            inside = !outside;
        }
        if (!inside) {
            _Fail("BA.657", TfStringPrintf(
                "wireEdge NURBS controlVertices[%zu] = [%s, %s, %s] is outside "
                "all brep extents.",
                c, _PyValue::NumRepr(cvs.Tup(c, 0), false).c_str(),
                _PyValue::NumRepr(cvs.Tup(c, 1), false).c_str(),
                _PyValue::NumRepr(cvs.Tup(c, 2), false).c_str()));
            break;
        }
    }
}

// _de_boor_evaluate: a rational B-spline curve in 3D evaluated at t with de
// Boor's algorithm, t clamped into [knots[order - 1], knots[n]]. False where
// Python returns None: the curve data is too short or the weight sum
// collapses.
bool
_PyDeBoorEvaluate3d(long long order, const std::vector<double> &knots,
                    const std::vector<GfVec3d> &cvs,
                    const std::vector<double> &weights, double t, GfVec3d *out)
{
    const long long n = static_cast<long long>(cvs.size());
    const long long p = order - 1;
    if (order < 1 || n < order
        || static_cast<long long>(knots.size()) < n + order) {
        return false;
    }
    t = std::max(knots[p], std::min(t, knots[n]));
    long long k = p;
    bool found = false;
    for (long long i = p; i < n; ++i) {
        if (knots[i] <= t && t < knots[i + 1]) {
            k = i;
            found = true;
            break;
        }
    }
    if (!found) {
        // math.isclose(t, knots[n], rel_tol=1e-12, abs_tol=1e-14)
        const double kn = knots[n];
        if (std::abs(t - kn)
            <= std::max(1e-12 * std::max(std::abs(t), std::abs(kn)), 1e-14)) {
            k = n - 1;
        }
    }
    std::vector<std::array<double, 4>> d(static_cast<size_t>(p + 1));
    for (long long j = 0; j <= p; ++j) {
        const long long idx = k - p + j;
        if (idx < 0 || idx >= n) {
            return false;
        }
        const double w = weights[idx];
        d[j] = { cvs[idx][0] * w, cvs[idx][1] * w, cvs[idx][2] * w, w };
    }
    for (long long r = 1; r <= p; ++r) {
        for (long long j = p; j >= r; --j) {
            const long long left = k - p + j;
            const long long right = left + p - r + 1;
            if (right >= static_cast<long long>(knots.size())
                || left >= static_cast<long long>(knots.size())) {
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

// Python's str() of a 3-vector, for messages.
std::string
_PyVec3Repr(double x, double y, double z)
{
    return TfStringPrintf("(%g, %g, %g)", x, y, z);
}

// The control vertices (or positions) of [start, end) that lie outside one
// Brep's brep:extent box by more than NUMERICAL_TOLERANCE on some axis, as
// "X: d below min" / "Y: d above max" text for the message.
std::string
_PyDistanceOutside(const _PyValue &points, size_t i, const _PyValue &extent,
                   size_t lo, size_t hi)
{
    static const char *const axes[3] = { "X", "Y", "Z" };
    std::vector<std::string> parts;
    for (size_t a = 0; a < 3; ++a) {
        const double p = points.Tup(i, a);
        const double mn = extent.Tup(lo, a), mx = extent.Tup(hi, a);
        if (p < mn - _PyNumericalTolerance) {
            parts.push_back(TfStringPrintf("%s: %.6g below min", axes[a], mn - p));
        } else if (p > mx + _PyNumericalTolerance) {
            parts.push_back(TfStringPrintf("%s: %.6g above max", axes[a], p - mx));
        }
    }
    return parts.empty() ? "unknown" : TfStringJoin(parts, ", ");
}

bool
_PyOutsideBox(const _PyValue &points, size_t i, const _PyValue &extent,
              size_t lo, size_t hi)
{
    for (size_t a = 0; a < 3; ++a) {
        const double p = points.Tup(i, a);
        if (p < extent.Tup(lo, a) - _PyNumericalTolerance
            || p > extent.Tup(hi, a) + _PyNumericalTolerance) {
            return true;
        }
    }
    return false;
}

// The rule shared by _validate_vertex_position_containment (BA.310) and the
// two control-point containment rules (BA.365, BA.465): the points of each
// Brep's span lie inside that Brep's brep:extent box, within
// NUMERICAL_TOLERANCE. A Brep with no box (brep:extent shorter than the Brep
// count) ends the walk; a span past the partition ends at the array's end.
// `firstOnly` reports the first point outside per Brep, as the control-point
// rules do.
void
_BrepChecker::_ValidatePointsInBrepExtent(const char *rule,
                                          const _PyValue &points,
                                          const std::vector<long long> &offsets,
                                          bool firstOnly,
                                          const char *subject)
{
    const _PyValue extents = _Get("brep:extent").OrEmpty();
    const size_t numBreps = _SafeGet("brep:regionCount").Len();
    if (!points.Truthy() || !extents.Truthy() || numBreps == 0
        || !points.IsTuples() || points.Dim() < 3 || !extents.IsTuples()
        || extents.Dim() < 3) {
        return;
    }
    for (size_t b = 0; b < numBreps; ++b) {
        if (b >= extents.Len() / 2) {
            break;
        }
        const size_t lo = 2 * b, hi = 2 * b + 1;
        const long long start = b < offsets.size() ? offsets[b] : 0;
        const long long end = b + 1 < offsets.size()
            ? offsets[b + 1]
            : static_cast<long long>(points.Len());
        const long long stop
            = std::min(end, static_cast<long long>(points.Len()));
        for (long long p = start; p < stop; ++p) {
            size_t i = 0;
            if (!_PyIndex(p, points.Len(), &i)
                || !_PyOutsideBox(points, i, extents, lo, hi)) {
                continue;
            }
            const std::string pos = _PyVec3Repr(points.Tup(i, 0),
                                                points.Tup(i, 1),
                                                points.Tup(i, 2));
            const std::string box = "["
                + _PyVec3Repr(extents.Tup(lo, 0), extents.Tup(lo, 1),
                              extents.Tup(lo, 2))
                + ", "
                + _PyVec3Repr(extents.Tup(hi, 0), extents.Tup(hi, 1),
                              extents.Tup(hi, 2))
                + "]";
            const std::string distance
                = _PyDistanceOutside(points, i, extents, lo, hi);
            if (firstOnly) {
                _Fail(rule, TfStringPrintf(
                    "%s %s at index %lld lies outside reasonable bounds for "
                    "brep #%zu. Distance outside bounds: %s.",
                    subject, pos.c_str(), p, b, distance.c_str()));
                break;
            }
            _Fail(rule, TfStringPrintf(
                "Vertex position %s for vertex #%lld in brep #%zu lies outside "
                "brep extent bounds %s. Distance outside bounds: %s.",
                pos.c_str(), p, b, box.c_str(), distance.c_str()));
        }
    }
}

// _validate_vertex_position_containment (BA.310): each Brep's vertex
// positions -- its span of the vertex partition -- lie inside its box.
void
_BrepChecker::ValidateVertexPositionContainment()
{
    _ValidatePointsInBrepExtent(
        "BA.310", _Get("brep:vertexPoint:point:position").OrEmpty(),
        _Offsets().vertices, false, nullptr);
}

// _validate_edge3d_nurbs_control_point_containment (BA.365): the first edge3d
// NURBS control vertex outside each Brep's box. The spans come from
// _compute_nurbs_control_vertex_offsets.
void
_BrepChecker::ValidateEdge3dNurbsControlPointContainment()
{
    _ValidatePointsInBrepExtent(
        "BA.365",
        _Get("brep:edge3dNurb:curve3d:nurb:controlVertices").OrEmpty(),
        _Offsets().edgeControlVertices, true, "Edge NURBS control vertex");
}

// _validate_surface_nurbs_control_point_containment (BA.465): the first
// surface NURBS control vertex outside each Brep's box.
void
_BrepChecker::ValidateSurfaceNurbsControlPointContainment()
{
    _ValidatePointsInBrepExtent(
        "BA.465", _Get("brep:surface:nurb:controlVertices").OrEmpty(),
        _Offsets().surfaceControlVertices, true,
        "Surface NURBS control vertex");
}

// _validate_analytic_surface_origin_containment (BA.620): an analytic
// surface's origin (a sphere's center) lies inside the union of the
// brep:extent boxes expanded by twice the union's diagonal -- or, for a plane,
// cylinder or cone, the face it carries does, evaluated over its face:range.
void
_BrepChecker::ValidateAnalyticSurfaceOriginContainment()
{
    const _PyValue extents = _Get("brep:extent").OrEmpty();
    if (extents.Len() < 2 || !extents.IsTuples() || extents.Dim() < 3) {
        return;
    }
    GfVec3d globalMin(std::numeric_limits<double>::infinity());
    GfVec3d globalMax(-std::numeric_limits<double>::infinity());
    for (size_t i = 0; i + 1 < extents.Len(); i += 2) {
        for (int c = 0; c < 3; ++c) {
            globalMin[c] = std::min(globalMin[c], extents.Tup(i, c));
            globalMax[c] = std::max(globalMax[c], extents.Tup(i + 1, c));
        }
    }
    const double diag = (globalMax - globalMin).GetLength();
    if (diag < 1e-12) {
        return;
    }
    const double margin = diag * 2.0;

    const _PyValue surfaceTypes = _SafeGet("face:surfaceType");
    const _PyValue &rawRanges = _Get("face:range");
    const _PyValue faceRanges
        = rawRanges.IsUnregistered() ? _PyValue() : rawRanges;

    struct Family
    {
        const char *originAttr, *token, *label, *base;
    };
    static const Family families[] = {
        { "brep:surface:plane:origin", "BrepSurfacePlaneAPI", "Plane",
          "brep:surface:plane:" },
        { "brep:surface:cylinder:origin", "BrepSurfaceCylinderAPI", "Cylinder",
          "brep:surface:cylinder:" },
        { "brep:surface:cone:origin", "BrepSurfaceConeAPI", "Cone",
          "brep:surface:cone:" },
        { "brep:surface:sphere:center", "BrepSurfaceSphereAPI", "Sphere",
          "brep:surface:sphere:" },
        { "brep:surface:torus:origin", "BrepSurfaceTorusAPI", "Torus",
          "brep:surface:torus:" },
    };
    const auto vec = [](const _PyValue &v, size_t i) {
        return GfVec3d(v.Tup(i, 0), v.Tup(i, 1), v.Tup(i, 2));
    };
    const auto inside = [&](const GfVec3d &lo, const GfVec3d &hi) {
        for (int c = 0; c < 3; ++c) {
            if (hi[c] < globalMin[c] - margin || lo[c] > globalMax[c] + margin) {
                return false;
            }
        }
        return true;
    };
    for (const Family &f : families) {
        const _PyValue origins = _SafeGet(f.originAttr);
        if (origins.Len() == 0 || !origins.IsTuples() || origins.Dim() < 3) {
            continue;
        }
        std::vector<size_t> faceIndices;
        for (size_t fi = 0; fi < surfaceTypes.Len(); ++fi) {
            if (surfaceTypes.Repr(fi) == f.token) {
                faceIndices.push_back(fi);
            }
        }
        const std::string label(f.label);
        const std::string base(f.base);
        for (size_t i = 0; i < origins.Len(); ++i) {
            const GfVec3d origin = vec(origins, i);
            bool originInside = true;
            for (int c = 0; c < 3; ++c) {
                if (origin[c] < globalMin[c] - margin
                    || origin[c] > globalMax[c] + margin) {
                    originInside = false;
                }
            }
            if (originInside) {
                continue;
            }
            bool faceInside = false;
            if (faceRanges.Truthy() && faceRanges.IsTuples()
                && faceRanges.Dim() >= 2 && i < faceIndices.size()
                && faceIndices[i] * 2 + 1 < faceRanges.Len()) {
                const size_t fi = faceIndices[i];
                const double uMin = faceRanges.Tup(2 * fi, 0);
                const double vMin = faceRanges.Tup(2 * fi, 1);
                const double uMax = faceRanges.Tup(2 * fi + 1, 0);
                const double vMax = faceRanges.Tup(2 * fi + 1, 1);
                const _PyValue axes = _SafeGet(base + "axis");
                const _PyValue refs = _SafeGet(base + "refDirection");
                const bool frame = axes.IsTuples() && refs.IsTuples()
                    && axes.Dim() >= 3 && refs.Dim() >= 3 && i < axes.Len()
                    && i < refs.Len();
                GfVec3d lo(std::numeric_limits<double>::infinity());
                GfVec3d hi(-std::numeric_limits<double>::infinity());
                const auto grow = [&](const GfVec3d &p) {
                    for (int c = 0; c < 3; ++c) {
                        lo[c] = std::min(lo[c], p[c]);
                        hi[c] = std::max(hi[c], p[c]);
                    }
                };
                if (label == "Plane" && frame) {
                    const GfVec3d axis = vec(axes, i), ref = vec(refs, i);
                    const GfVec3d binormal = GfCross(axis, ref);
                    for (const double u : { uMin, uMax }) {
                        for (const double v : { vMin, vMax }) {
                            grow(origin + u * ref + v * binormal);
                        }
                    }
                    faceInside = inside(lo, hi);
                } else if ((label == "Cylinder" || label == "Cone") && frame) {
                    const bool cone = label == "Cone";
                    const _PyValue radii = _SafeGet(base + "radius");
                    const _PyValue angles = cone ? _SafeGet(base + "semiAngle")
                                                 : _PyValue::EmptyList();
                    if (radii.IsNumbers() && i < radii.Len()
                        && (!cone || (angles.IsNumbers() && i < angles.Len()))) {
                        const GfVec3d axis = vec(axes, i), ref = vec(refs, i);
                        const GfVec3d binormal = GfCross(axis, ref);
                        const double radius = radii.Num(i);
                        const double tanA = cone ? std::tan(angles.Num(i)) : 0.0;
                        for (const double v : { vMin, vMax }) {
                            const double rv = radius + v * tanA;
                            for (const double u :
                                 { uMin, uMax, (uMin + uMax) / 2.0 }) {
                                grow(origin + v * axis
                                     + rv * (std::cos(u) * ref
                                             + std::sin(u) * binormal));
                            }
                        }
                        const double maxR = cone
                            ? std::max(std::abs(radius + vMin * tanA),
                                       std::abs(radius + vMax * tanA))
                            : radius;
                        for (int c = 0; c < 3; ++c) {
                            lo[c] -= maxR;
                            hi[c] += maxR;
                        }
                        faceInside = inside(lo, hi);
                    }
                }
            }
            if (faceInside) {
                continue;
            }
            _Fail("BA.620", TfStringPrintf(
                "%s surface #%zu origin/center (%.6f, %.6f, %.6f) lies outside "
                "the brep extent expanded by %.4f (extent: [%.4f, %.4f, %.4f] - "
                "[%.4f, %.4f, %.4f]).",
                f.label, i, origin[0], origin[1], origin[2], margin,
                globalMin[0], globalMin[1], globalMin[2], globalMax[0],
                globalMax[1], globalMax[2]));
        }
    }
}

// _validate_shell_point_containment (BA.710): each point shell's position --
// positions are packed in shell order over point shells only -- lies inside
// its own Brep's brep:extent box, by the single-precision comparison.
void
_BrepChecker::ValidateShellPointContainment()
{
    const _PyValue positions = _SafeGet("brep:shellPoint:point:position");
    const _PyValue extent = _SafeGet("brep:extent");
    const _PyValue pointTypes = _SafeGet("shell:pointType");
    const _PyValue faceuseCounts = _SafeGet("shell:faceuseCount");
    const _PyValue wireEdgeCounts = _SafeGet("shell:wireEdgeCount");
    if (positions.Len() == 0 || extent.Len() < 2) {
        return;
    }
    const _PyValue regionCounts = _SafeGet("brep:regionCount");
    const _PyValue shellCounts = _SafeGet("region:shellCount");
    if (regionCounts.Len() == 0 || shellCounts.Len() == 0) {
        return;
    }
    const auto isPointShell = [&](size_t s) {
        long long fu = 0, we = 0;
        return s < pointTypes.Len() && pointTypes.Equals(s, "BrepPointAPI")
            && s < faceuseCounts.Len() && s < wireEdgeCounts.Len()
            && faceuseCounts.ToInt(s, &fu) && wireEdgeCounts.ToInt(s, &we)
            && fu == 0 && we == 0;
    };
    long long shellOffset = 0, regionOffset = 0;
    size_t spIdx = 0;
    for (size_t b = 0; b < regionCounts.Len(); ++b) {
        long long numRegions = 0;
        regionCounts.ToInt(b, &numRegions);
        const long long shellStart = shellOffset;
        for (long long r = 0; r < numRegions; ++r) {
            const long long ri = regionOffset + r;
            long long count = 0;
            if (ri >= 0 && ri < static_cast<long long>(shellCounts.Len())
                && shellCounts.ToInt(static_cast<size_t>(ri), &count)) {
                shellOffset += count;
            }
        }
        regionOffset += numRegions;
        if (2 * b + 1 >= extent.Len() || !extent.IsTuples()
            || extent.Dim() < 3) {
            continue;
        }
        for (long long s = shellStart; s < shellOffset; ++s) {
            if (s < 0 || !isPointShell(static_cast<size_t>(s))) {
                continue;
            }
            if (spIdx >= positions.Len()) {
                continue;
            }
            if (!positions.IsTuples() || positions.Dim() < 3) {
                ++spIdx;
                continue;
            }
            bool outside = false;
            for (int a = 0; a < 3; ++a) {
                const double p = positions.Tup(spIdx, a);
                outside = outside
                    || _PyIsFloatLessThan(p, extent.Tup(2 * b, a))
                    || _PyIsFloatGreaterThan(p, extent.Tup(2 * b + 1, a));
            }
            if (outside) {
                _Fail("BA.710", TfStringPrintf(
                    "shellPoint:position[%zu] = [%s, %s, %s] in brep #%zu is "
                    "outside brep extent.",
                    spIdx,
                    _PyValue::NumRepr(positions.Tup(spIdx, 0), false).c_str(),
                    _PyValue::NumRepr(positions.Tup(spIdx, 1), false).c_str(),
                    _PyValue::NumRepr(positions.Tup(spIdx, 2), false).c_str(),
                    b));
            }
            ++spIdx;
        }
    }
}

// _compute_edge_brep_indices: each edge's Brep is the Brep of the first
// edgeuse (in each Brep's edgeuse partition) that names it.
std::unordered_map<long long, size_t>
_BrepChecker::_EdgeBrepIndices()
{
    std::unordered_map<long long, size_t> out;
    const std::vector<long long> &offsets = _Offsets().edgeuses;
    const _PyValue edgeIndex = _SafeGet("edgeuse:edgeIndex");
    for (size_t b = 0; b + 1 < offsets.size(); ++b) {
        const long long end
            = std::min(offsets[b + 1], static_cast<long long>(edgeIndex.Len()));
        for (long long eu = offsets[b]; eu < end; ++eu) {
            size_t i = 0;
            long long e = 0;
            if (_PyIndex(eu, edgeIndex.Len(), &i) && edgeIndex.ToInt(i, &e)) {
                out.emplace(e, b);
            }
        }
    }
    return out;
}

// _get_edge_intersect_tolerance: the brep:intersectTol3d of the edge's Brep --
// or of the only Brep, when a single tolerance is authored and no edgeuse
// names the edge -- if it is finite and at least NUMERICAL_TOLERANCE.
bool
_BrepChecker::_EdgeIntersectTolerance(
    long long edge, const std::unordered_map<long long, size_t> &edgeBreps,
    double *tol, size_t *brep)
{
    const _PyValue tols = _SafeGet("brep:intersectTol3d");
    if (tols.Len() == 0) {
        return false;
    }
    const auto it = edgeBreps.find(edge);
    size_t b = 0;
    if (it != edgeBreps.end()) {
        b = it->second;
    } else if (tols.Len() != 1) {
        return false;
    }
    double t = 0.0;
    if (b >= tols.Len() || !tols.ToFloat(b, &t) || !std::isfinite(t)
        || t < _PyNumericalTolerance) {
        return false;
    }
    *tol = t;
    *brep = b;
    return true;
}

// _report_unresolved_edge_tolerance.
void
_BrepChecker::_ReportUnresolvedEdgeTolerance(const char *rule, long long edge,
                                             const char *checkLabel)
{
    _Fail(rule, UsdSolidValidationErrorNameTokens->unresolvedEdgeIntersectTol3d,
          TfStringPrintf(
              "%s for edge #%lld could not be validated because no positive "
              "brep:intersectTol3d value could be resolved for the edge. The "
              "tolerance is missing, invalid, or the edge could not be "
              "associated with a BRep.",
              checkLabel, edge));
}

// _validate_nurbs_edge_endpoint_vertex (BA.730): each NURBS edge, evaluated
// with de Boor's algorithm at its two edge:range parameters, lands on the
// vertices its edge:vertexIndices name, within its Brep's tolerance. One
// finding per edge; an edge whose tolerance does not resolve is reported as
// unvalidatable.
void
_BrepChecker::ValidateNurbsEdgeEndpointVertex()
{
    const std::string base = "brep:edge3dNurb:curve3d:nurb:";
    const _PyValue curveTypes = _SafeGet("edge:curveType");
    const _PyValue &ranges = _Get("edge:range");
    const _PyValue pairs = _SafeGet("edge:vertexIndices");
    const _PyValue positions = _SafeGet("brep:vertexPoint:point:position");
    const _PyValue orders = _SafeGet(base + "order");
    const _PyValue counts = _SafeGet(base + "vertexCount");
    const _PyValue cvs = _SafeGet(base + "controlVertices");
    const _PyValue weights = _SafeGet(base + "weights");
    const _PyValue knots = _SafeGet(base + "knots");
    if (curveTypes.Len() == 0 || !ranges.Truthy() || ranges.IsUnregistered()
        || !ranges.IsSequence() || pairs.Len() == 0 || positions.Len() == 0
        || orders.Len() == 0 || counts.Len() == 0 || cvs.Len() == 0
        || weights.Len() == 0 || knots.Len() == 0) {
        return;
    }
    if (!cvs.IsTuples() || cvs.Dim() < 3 || !positions.IsTuples()
        || positions.Dim() < 3 || !pairs.IsTuples() || pairs.Dim() < 2) {
        return;
    }
    const std::unordered_map<long long, size_t> edgeBreps = _EdgeBrepIndices();
    size_t nurbIdx = 0;
    long long cvOffset = 0, knOffset = 0;
    for (size_t e = 0; e < curveTypes.Len(); ++e) {
        if (curveTypes.Repr(e) != "BrepCurve3dNurbAPI") {
            continue;
        }
        if (nurbIdx >= orders.Len() || nurbIdx >= counts.Len()) {
            break;
        }
        long long order = 0, numCvs = 0;
        orders.ToInt(nurbIdx, &order);
        counts.ToInt(nurbIdx, &numCvs);
        const long long numKnots = numCvs + order;
        const auto advance = [&]() {
            ++nurbIdx;
            cvOffset += numCvs;
            knOffset += numKnots;
        };
        if (cvOffset + numCvs > static_cast<long long>(cvs.Len())
            || cvOffset + numCvs > static_cast<long long>(weights.Len())
            || knOffset + numKnots > static_cast<long long>(knots.Len())
            || 2 * e + 1 >= ranges.Len() || e >= pairs.Len()) {
            advance();
            continue;
        }
        std::vector<double> curveKnots, curveWeights;
        std::vector<GfVec3d> curveCvs;
        for (long long k = 0; k < numKnots; ++k) {
            double x = 0.0;
            knots.ToFloat(static_cast<size_t>(knOffset + k), &x);
            curveKnots.push_back(x);
        }
        for (long long j = 0; j < numCvs; ++j) {
            const size_t i = static_cast<size_t>(cvOffset + j);
            curveCvs.emplace_back(cvs.Tup(i, 0), cvs.Tup(i, 1), cvs.Tup(i, 2));
            double w = 0.0;
            weights.ToFloat(i, &w);
            curveWeights.push_back(w);
        }
        double tStart = 0.0, tEnd = 0.0;
        ranges.ToFloat(2 * e, &tStart);
        ranges.ToFloat(2 * e + 1, &tEnd);
        const long long vStart = static_cast<long long>(pairs.Tup(e, 0));
        const long long vEnd = static_cast<long long>(pairs.Tup(e, 1));
        double tol = 0.0;
        size_t brep = 0;
        if (!_EdgeIntersectTolerance(static_cast<long long>(e), edgeBreps, &tol,
                                     &brep)) {
            _ReportUnresolvedEdgeTolerance(
                "BA.730", static_cast<long long>(e),
                "NURBS endpoint-to-vertex consistency");
            advance();
            continue;
        }
        const std::tuple<double, long long, const char *> ends[2]
            = { { tStart, vStart, "start" }, { tEnd, vEnd, "end" } };
        for (const auto &end : ends) {
            size_t vi = 0;
            if (std::get<1>(end) >= static_cast<long long>(positions.Len())
                || !_PyIndex(std::get<1>(end), positions.Len(), &vi)) {
                continue;
            }
            GfVec3d evaluated;
            if (!_PyDeBoorEvaluate3d(order, curveKnots, curveCvs, curveWeights,
                                     std::get<0>(end), &evaluated)) {
                continue;
            }
            const GfVec3d vertex(positions.Tup(vi, 0), positions.Tup(vi, 1),
                                 positions.Tup(vi, 2));
            const double dist = (evaluated - vertex).GetLength();
            if (dist > tol) {
                _Fail("BA.730", TfStringPrintf(
                    "NURBS edge #%zu in brep #%zu %s endpoint evaluated at "
                    "t=%.6f is %.6f from vertex #%lld "
                    "(brep:intersectTol3d[%zu] = %s).",
                    e, brep, std::get<2>(end), std::get<0>(end), dist,
                    std::get<1>(end), brep,
                    _PyValue::NumRepr(tol, false).c_str()));
                break;
            }
        }
        advance();
    }
}

// The unit-length and orthogonality tolerance brep_validator.py uses for
// every analytic axis frame, surface and curve alike.
constexpr double _PyFrameTolerance = 1e-4;
// PERIOD_TOL / DOMAIN_TOL in brep_validator.py's periodic-domain rules.
constexpr double _PyPeriodTolerance = 1e-6;
constexpr double _PyTwoPi = 6.283185307179586;
constexpr double _PyHalfPi = 1.5707963267948966;

// The shared body of _validate_surface_{sphere,plane,cylinder,cone,torus}_data
// and _validate_curve3d_{circle,line,ellipse}_data: with at least one entity
// of the type, every parameter array holds one entry per entity (the size
// rule), the first non-positive (or, for a cone, negative) radius, the first
// axis and refDirection more than 1e-4 from unit length, the first pair more
// than 1e-4 from orthogonal, and for a cone the first semiAngle outside
// (0, pi/2).
struct _PyAnalyticFamily
{
    const char *typeAttr;     // face:surfaceType / edge:curveType / ...
    const char *typeToken;
    const char *prefix;       // "brep:surface:sphere" ...
    const char *countDesc;    // "BrepSurfaceSphereAPI faces" ...
    const char *sizeRule;
    std::vector<const char *> arrays;  // in the order Python lists them
    // (attribute suffix, rule, strictly positive) per radius
    std::vector<std::tuple<const char *, const char *, bool>> radii;
    const char *axisAttr, *axisRule;   // null where the family has none
    const char *refAttr, *refRule;
    const char *orthoRule;
    const char *semiAngleRule;         // cone only
};

void
_BrepChecker::_ValidateAnalyticFamily(const _PyAnalyticFamily &f)
{
    const _PyValue types = _Get(f.typeAttr).OrEmpty();
    size_t count = 0;
    for (size_t i = 0; i < types.Len(); ++i) {
        count += types.Equals(i, f.typeToken) ? 1 : 0;
    }
    if (count == 0) {
        return;
    }
    const std::string prefix(f.prefix);
    for (const char *name : f.arrays) {
        const std::string attr = prefix + ":" + name;
        const size_t len = _Get(attr).OrEmpty().Len();
        if (len != count) {
            _Fail(f.sizeRule, TfStringPrintf(
                "%s size (%zu) does not match number of %s (%zu).",
                attr.c_str(), len, f.countDesc, count));
        }
    }
    for (const auto &radius : f.radii) {
        const std::string attr = prefix + ":" + std::get<0>(radius);
        const _PyValue values = _Get(attr).OrEmpty();
        if (!values.IsNumbers()) {
            continue;
        }
        const bool strict = std::get<2>(radius);
        for (size_t i = 0; i < values.Len(); ++i) {
            const double r = values.Num(i);
            if (strict ? r <= 0.0 : r < 0.0) {
                _Fail(std::get<1>(radius), TfStringPrintf(
                    "%s[%zu] = %s is %s.", attr.c_str(), i,
                    values.Repr(i).c_str(),
                    strict ? "not positive" : "negative"));
                break;
            }
        }
    }
    const auto unit = [&](const char *suffix, const char *rule) {
        if (!suffix) {
            return;
        }
        const std::string attr = prefix + ":" + suffix;
        const _PyValue values = _Get(attr).OrEmpty();
        if (!values.IsTuples() || values.Dim() < 3) {
            return;
        }
        for (size_t i = 0; i < values.Len(); ++i) {
            const double length = std::sqrt(
                values.Tup(i, 0) * values.Tup(i, 0)
                + values.Tup(i, 1) * values.Tup(i, 1)
                + values.Tup(i, 2) * values.Tup(i, 2));
            if (std::abs(length - 1.0) > _PyFrameTolerance) {
                _Fail(rule, TfStringPrintf("%s[%zu] has length %.6f, expected "
                                           "1.0.",
                                           attr.c_str(), i, length));
                break;
            }
        }
    };
    unit(f.axisAttr, f.axisRule);
    unit(f.refAttr, f.refRule);
    if (f.orthoRule) {
        const _PyValue axes = _Get(prefix + ":" + f.axisAttr).OrEmpty();
        const _PyValue refs = _Get(prefix + ":" + f.refAttr).OrEmpty();
        if (axes.IsTuples() && refs.IsTuples() && axes.Dim() >= 3
            && refs.Dim() >= 3) {
            const size_t n = std::min(axes.Len(), refs.Len());
            for (size_t i = 0; i < n; ++i) {
                double dot = 0.0;
                for (size_t c = 0; c < 3; ++c) {
                    dot += axes.Tup(i, c) * refs.Tup(i, c);
                }
                if (std::abs(dot) > _PyFrameTolerance) {
                    _Fail(f.orthoRule, TfStringPrintf(
                        "%s:%s[%zu] and %s[%zu] are not orthogonal (dot "
                        "product = %.6f).",
                        f.prefix, f.axisAttr, i, f.refAttr, i, dot));
                    break;
                }
            }
        }
    }
    if (f.semiAngleRule) {
        const std::string attr = prefix + ":semiAngle";
        const _PyValue values = _Get(attr).OrEmpty();
        if (values.IsNumbers()) {
            for (size_t i = 0; i < values.Len(); ++i) {
                const double a = values.Num(i);
                if (a <= 0.0 || a >= _PyHalfPi) {
                    _Fail(f.semiAngleRule, TfStringPrintf(
                        "%s[%zu] = %s is not in valid range (0, pi/2).",
                        attr.c_str(), i, values.Repr(i).c_str()));
                    break;
                }
            }
        }
    }
}

// _validate_surface_{sphere,plane,cylinder,cone,torus}_data (BA.480-BA.525).
void
_BrepChecker::ValidateAnalyticSurfaces()
{
    using R = std::tuple<const char *, const char *, bool>;
    const _PyAnalyticFamily families[] = {
        { "face:surfaceType", "BrepSurfaceSphereAPI", "brep:surface:sphere",
          "BrepSurfaceSphereAPI faces", "BA.480",
          { "center", "axis", "refDirection", "radius" },
          { R{ "radius", "BA.481", true } }, "axis", "BA.482", "refDirection",
          "BA.483", "BA.484", nullptr },
        { "face:surfaceType", "BrepSurfacePlaneAPI", "brep:surface:plane",
          "BrepSurfacePlaneAPI faces", "BA.490",
          { "origin", "axis", "refDirection" }, {}, "axis", "BA.491",
          "refDirection", "BA.492", "BA.493", nullptr },
        { "face:surfaceType", "BrepSurfaceCylinderAPI", "brep:surface:cylinder",
          "BrepSurfaceCylinderAPI faces", "BA.500",
          { "origin", "axis", "refDirection", "radius" },
          { R{ "radius", "BA.501", true } }, "axis", "BA.502", "refDirection",
          "BA.503", "BA.504", nullptr },
        { "face:surfaceType", "BrepSurfaceConeAPI", "brep:surface:cone",
          "BrepSurfaceConeAPI faces", "BA.510",
          { "origin", "axis", "refDirection", "radius", "semiAngle" },
          { R{ "radius", "BA.511", false } }, "axis", "BA.512",
          "refDirection", "BA.513", "BA.514", "BA.515" },
        { "face:surfaceType", "BrepSurfaceTorusAPI", "brep:surface:torus",
          "BrepSurfaceTorusAPI faces", "BA.520",
          { "origin", "axis", "refDirection", "majorRadius", "minorRadius" },
          { R{ "majorRadius", "BA.521", true },
            R{ "minorRadius", "BA.522", true } },
          "axis", "BA.523", "refDirection", "BA.524", "BA.525", nullptr },
    };
    for (const _PyAnalyticFamily &f : families) {
        _ValidateAnalyticFamily(f);
    }
}

// _validate_curve3d_{circle,line,ellipse}_data (BA.530-BA.555), for edge and
// wireEdge instances alike.
void
_BrepChecker::ValidateAnalyticCurves()
{
    using R = std::tuple<const char *, const char *, bool>;
    for (const bool wire : { false, true }) {
        const char *typeAttr = wire ? "wireEdge:curveType" : "edge:curveType";
        const std::string inst = wire ? "wireEdge3d" : "edge3d";
        const std::string circle = "brep:" + inst + "Circle:curve3d:circle";
        const std::string line = "brep:" + inst + "Line:curve3d:line";
        const std::string ellipse = "brep:" + inst + "Ellipse:curve3d:ellipse";
        const std::string circleDesc = std::string("BrepCurve3dCircleAPI ")
            + "entries in " + typeAttr;
        const std::string lineDesc = std::string("BrepCurve3dLineAPI ")
            + "entries in " + typeAttr;
        const std::string ellipseDesc = std::string("BrepCurve3dEllipseAPI ")
            + "entries in " + typeAttr;
        const _PyAnalyticFamily families[] = {
            { typeAttr, "BrepCurve3dCircleAPI", circle.c_str(),
              circleDesc.c_str(), "BA.530",
              { "center", "axis", "refDirection", "radius" },
              { R{ "radius", "BA.531", true } }, "axis", "BA.532",
              "refDirection", "BA.533", "BA.534", nullptr },
            { typeAttr, "BrepCurve3dLineAPI", line.c_str(), lineDesc.c_str(),
              "BA.540", { "origin", "direction" }, {}, "direction", "BA.541",
              nullptr, nullptr, nullptr, nullptr },
            { typeAttr, "BrepCurve3dEllipseAPI", ellipse.c_str(),
              ellipseDesc.c_str(), "BA.550",
              { "center", "axis", "refDirection", "xRadius", "yRadius" },
              { R{ "xRadius", "BA.551", true }, R{ "yRadius", "BA.552", true } },
              "axis", "BA.553", "refDirection", "BA.554", "BA.555", nullptr },
        };
        for (const _PyAnalyticFamily &f : families) {
            _ValidateAnalyticFamily(f);
        }
    }
}

// _validate_face_range_domain_limits (BA.560-BA.565): an angular face:range
// spans at most 2*pi (+1e-6) -- U on spheres, cylinders, cones and tori, V
// on tori -- and a sphere's V stays within [-pi/2, pi/2].
void
_BrepChecker::ValidateFaceRangeDomainLimits()
{
    const _PyValue types = _SafeGet("face:surfaceType");
    const _PyValue &raw = _Get("face:range");
    if (raw.IsUnregistered()) {
        return;
    }
    const _PyValue ranges = raw.OrEmpty();
    if (types.Len() == 0 || !ranges.Truthy() || !ranges.IsTuples()
        || ranges.Dim() < 2) {
        return;
    }
    const size_t numFaces = types.Len();
    if (ranges.Len() < numFaces * 2) {
        return;
    }
    const std::vector<long long> &faceOffsets = _Offsets().faces;
    for (size_t f = 0; f < numFaces; ++f) {
        const double uMin = ranges.Tup(2 * f, 0), vMin = ranges.Tup(2 * f, 1);
        const double uMax = ranges.Tup(2 * f + 1, 0);
        const double vMax = ranges.Tup(2 * f + 1, 1);
        const double uSpan = uMax - uMin, vSpan = vMax - vMin;
        const std::string type = types.Repr(f);
        size_t brep = 0, local = f;
        if (_FindBrep(faceOffsets, static_cast<double>(f), &brep)) {
            local = f - static_cast<size_t>(faceOffsets[brep]);
        } else {
            brep = 0;
        }
        const auto spanFail = [&](const char *rule, const char *label,
                                  const char *axis, double span, double lo,
                                  double hi) {
            _Fail(rule, TfStringPrintf(
                "%s face #%zu in brep #%zu has %s span %.6f rad which exceeds "
                "2*pi (%.6f). %s range = [%.6f, %.6f].",
                label, local, brep, axis, span, _PyTwoPi, axis, lo, hi));
        };
        if (type == "BrepSurfaceSphereAPI") {
            if (uSpan > _PyTwoPi + _PyPeriodTolerance) {
                spanFail("BA.560", "Sphere", "U", uSpan, uMin, uMax);
            }
            if (vMin < -_PyHalfPi - _PyPeriodTolerance
                || vMax > _PyHalfPi + _PyPeriodTolerance) {
                _Fail("BA.561", TfStringPrintf(
                    "Sphere face #%zu in brep #%zu has V range [%.6f, %.6f] "
                    "rad outside the latitude bounds [-pi/2, pi/2] = [%.6f, "
                    "%.6f].",
                    local, brep, vMin, vMax, -_PyHalfPi, _PyHalfPi));
            }
        } else if (type == "BrepSurfaceCylinderAPI") {
            if (uSpan > _PyTwoPi + _PyPeriodTolerance) {
                spanFail("BA.562", "Cylinder", "U", uSpan, uMin, uMax);
            }
        } else if (type == "BrepSurfaceConeAPI") {
            if (uSpan > _PyTwoPi + _PyPeriodTolerance) {
                spanFail("BA.563", "Cone", "U", uSpan, uMin, uMax);
            }
        } else if (type == "BrepSurfaceTorusAPI") {
            if (uSpan > _PyTwoPi + _PyPeriodTolerance) {
                spanFail("BA.564", "Torus", "U", uSpan, uMin, uMax);
            }
            if (vSpan > _PyTwoPi + _PyPeriodTolerance) {
                spanFail("BA.565", "Torus", "V", vSpan, vMin, vMax);
            }
        }
    }
}

// _validate_edge_range_domain_limits (BA.570 / BA.571): a circle or ellipse
// edge or wire edge spans at most 2*pi (+1e-6) of parameter.
void
_BrepChecker::ValidateEdgeRangeDomainLimits()
{
    const _PyOffsets &o = _Offsets();
    for (const bool wire : { false, true }) {
        const char *kind = wire ? "wireEdge" : "edge";
        const _PyValue types
            = _SafeGet(wire ? "wireEdge:curveType" : "edge:curveType");
        const _PyValue &raw = _Get(wire ? "wireEdge:range" : "edge:range");
        if (raw.IsUnregistered()) {
            continue;
        }
        const _PyValue ranges = raw.OrEmpty();
        if (types.Len() == 0 || !ranges.Truthy()) {
            continue;
        }
        const std::vector<long long> &offsets = wire ? o.wireedges : o.edges;
        const size_t numEdges = types.Len();
        if (ranges.Len() < numEdges * 2) {
            continue;
        }
        for (size_t e = 0; e < numEdges; ++e) {
            double mn = 0.0, mx = 0.0;
            if (!ranges.ToFloat(2 * e, &mn) || !ranges.ToFloat(2 * e + 1, &mx)) {
                continue;
            }
            const double span = mx - mn;
            size_t brep = 0, local = e;
            if (_FindBrep(offsets, static_cast<double>(e), &brep)) {
                local = e - static_cast<size_t>(offsets[brep]);
            } else {
                brep = 0;
            }
            const std::string type = types.Repr(e);
            const char *rule = type == "BrepCurve3dCircleAPI" ? "BA.570"
                : type == "BrepCurve3dEllipseAPI"             ? "BA.571"
                                                              : nullptr;
            if (rule && span > _PyTwoPi + _PyPeriodTolerance) {
                _Fail(rule, TfStringPrintf(
                    "%s %s #%zu in brep #%zu has parameter span %.6f rad which "
                    "exceeds 2*pi (%.6f). Range = [%.6f, %.6f].",
                    std::string(rule) == "BA.570" ? "Circle" : "Ellipse", kind,
                    local, brep, span, _PyTwoPi, mn, mx));
            }
        }
    }
}

// _validate_edge_curve_endpoint_vertex_consistency (BA.600 line, BA.601
// circle, BA.602 ellipse): an analytic edge evaluated at its two edge:range
// parameters lands on the vertices its edge:vertexIndices name, within its
// Brep's tolerance. Instances of a curve family are packed in edge order, so
// each family's cursor advances on every edge of that family. One finding
// per edge.
void
_BrepChecker::ValidateEdgeCurveEndpointVertexConsistency()
{
    const _PyValue types = _SafeGet("edge:curveType");
    const _PyValue &raw = _Get("edge:range");
    if (raw.IsUnregistered()) {
        return;
    }
    const _PyValue ranges = raw.OrEmpty();
    const _PyValue pairs = _SafeGet("edge:vertexIndices");
    const _PyValue positions = _SafeGet("brep:vertexPoint:point:position");
    if (types.Len() == 0 || !ranges.Truthy() || pairs.Len() == 0
        || positions.Len() == 0 || !pairs.IsTuples() || pairs.Dim() < 2
        || !positions.IsTuples() || positions.Dim() < 3) {
        return;
    }
    const size_t numEdges = types.Len();
    if (ranges.Len() < numEdges * 2) {
        return;
    }
    const std::unordered_map<long long, size_t> edgeBreps = _EdgeBrepIndices();
    const auto read3 = [&](const char *name) { return _SafeGet(name); };
    const _PyValue lineOrigin = read3("brep:edge3dLine:curve3d:line:origin");
    const _PyValue lineDir = read3("brep:edge3dLine:curve3d:line:direction");
    const _PyValue cCenter = read3("brep:edge3dCircle:curve3d:circle:center");
    const _PyValue cAxis = read3("brep:edge3dCircle:curve3d:circle:axis");
    const _PyValue cRef = read3("brep:edge3dCircle:curve3d:circle:refDirection");
    const _PyValue cRadius = read3("brep:edge3dCircle:curve3d:circle:radius");
    const _PyValue eCenter = read3("brep:edge3dEllipse:curve3d:ellipse:center");
    const _PyValue eAxis = read3("brep:edge3dEllipse:curve3d:ellipse:axis");
    const _PyValue eRef
        = read3("brep:edge3dEllipse:curve3d:ellipse:refDirection");
    const _PyValue eX = read3("brep:edge3dEllipse:curve3d:ellipse:xRadius");
    const _PyValue eY = read3("brep:edge3dEllipse:curve3d:ellipse:yRadius");
    const auto vec = [](const _PyValue &v, size_t i) {
        return v.IsTuples() && v.Dim() >= 3
            ? GfVec3d(v.Tup(i, 0), v.Tup(i, 1), v.Tup(i, 2))
            : GfVec3d(0.0);
    };
    const auto num = [](const _PyValue &v, size_t i) {
        double x = 0.0;
        v.ToFloat(i, &x);
        return x;
    };

    size_t lineIdx = 0, circleIdx = 0, ellipseIdx = 0;
    for (size_t e = 0; e < numEdges; ++e) {
        const std::string type = types.Repr(e);
        double tMin = 0.0, tMax = 0.0;
        ranges.ToFloat(2 * e, &tMin);
        ranges.ToFloat(2 * e + 1, &tMax);
        if (e >= pairs.Len()) {
            break;
        }
        const long long v0 = static_cast<long long>(pairs.Tup(e, 0));
        const long long v1 = static_cast<long long>(pairs.Tup(e, 1));
        double tol = 0.0;
        size_t brep = 0;
        const bool haveTol = _EdgeIntersectTolerance(
            static_cast<long long>(e), edgeBreps, &tol, &brep);
        const bool isLine = type == "BrepCurve3dLineAPI";
        const bool isCircle = type == "BrepCurve3dCircleAPI";
        const bool isEllipse = type == "BrepCurve3dEllipseAPI";
        const long long numPositions = static_cast<long long>(positions.Len());
        if (v0 < 0 || v0 >= numPositions || v1 < 0 || v1 >= numPositions) {
            lineIdx += isLine ? 1 : 0;
            circleIdx += isCircle ? 1 : 0;
            ellipseIdx += isEllipse ? 1 : 0;
            continue;
        }
        const GfVec3d p0 = vec(positions, static_cast<size_t>(v0));
        const GfVec3d p1 = vec(positions, static_cast<size_t>(v1));
        // Compare the evaluated start and end against the two vertices; the
        // first endpoint out of tolerance is the edge's one finding.
        const auto compare = [&](const char *rule, const char *shape,
                                 const GfVec3d &start, const GfVec3d &end) {
            const std::tuple<double, GfVec3d, GfVec3d, long long, const char *>
                ends[2] = { { tMin, start, p0, v0, "start" },
                            { tMax, end, p1, v1, "end" } };
            for (const auto &x : ends) {
                const GfVec3d &pt = std::get<1>(x), &vp = std::get<2>(x);
                const double dist = (pt - vp).GetLength();
                if (dist > tol) {
                    _Fail(rule, TfStringPrintf(
                        "%s edge #%zu %s point evaluated at t=%.6f is (%.6f, "
                        "%.6f, %.6f), but vertex #%lld is at (%.6f, %.6f, "
                        "%.6f), distance=%.6f exceeds brep:intersectTol3d[%zu] "
                        "= %s.",
                        shape, e, std::get<4>(x), std::get<0>(x), pt[0], pt[1],
                        pt[2], std::get<3>(x), vp[0], vp[1], vp[2], dist, brep,
                        _PyValue::NumRepr(tol, false).c_str()));
                    break;
                }
            }
        };
        if (isLine) {
            if (lineIdx < lineOrigin.Len() && lineIdx < lineDir.Len()) {
                if (!haveTol) {
                    _ReportUnresolvedEdgeTolerance(
                        "BA.600", static_cast<long long>(e),
                        "Line endpoint-to-vertex consistency");
                    ++lineIdx;
                    continue;
                }
                const GfVec3d o = vec(lineOrigin, lineIdx);
                const GfVec3d d = vec(lineDir, lineIdx);
                compare("BA.600", "Line", o + tMin * d, o + tMax * d);
            }
            ++lineIdx;
        } else if (isCircle) {
            if (circleIdx < cCenter.Len() && circleIdx < cAxis.Len()
                && circleIdx < cRef.Len() && circleIdx < cRadius.Len()) {
                if (!haveTol) {
                    _ReportUnresolvedEdgeTolerance(
                        "BA.601", static_cast<long long>(e),
                        "Circle endpoint-to-vertex consistency");
                    ++circleIdx;
                    continue;
                }
                const GfVec3d c = vec(cCenter, circleIdx);
                const GfVec3d ax = vec(cAxis, circleIdx);
                const GfVec3d ref = vec(cRef, circleIdx);
                const double r = num(cRadius, circleIdx);
                const GfVec3d y = GfCross(ax, ref);
                const auto at = [&](double t) {
                    return c + r * (std::cos(t) * ref + std::sin(t) * y);
                };
                compare("BA.601", "Circle", at(tMin), at(tMax));
            }
            ++circleIdx;
        } else if (isEllipse) {
            if (ellipseIdx < eCenter.Len() && ellipseIdx < eAxis.Len()
                && ellipseIdx < eRef.Len() && ellipseIdx < eX.Len()
                && ellipseIdx < eY.Len()) {
                if (!haveTol) {
                    _ReportUnresolvedEdgeTolerance(
                        "BA.602", static_cast<long long>(e),
                        "Ellipse endpoint-to-vertex consistency");
                    ++ellipseIdx;
                    continue;
                }
                const GfVec3d c = vec(eCenter, ellipseIdx);
                const GfVec3d ax = vec(eAxis, ellipseIdx);
                const GfVec3d ref = vec(eRef, ellipseIdx);
                const double xr = num(eX, ellipseIdx);
                const double yr = num(eY, ellipseIdx);
                const GfVec3d y = GfCross(ax, ref);
                const auto at = [&](double t) {
                    return c + xr * std::cos(t) * ref + yr * std::sin(t) * y;
                };
                compare("BA.602", "Ellipse", at(tMin), at(tMax));
            }
            ++ellipseIdx;
        }
    }
}

// _validate_circle_vertex_radius_consistency (BA.610): both vertices of a
// circle edge lie at the circle's radius from its center, within the edge's
// tolerance. One finding per edge.
void
_BrepChecker::ValidateCircleVertexRadiusConsistency()
{
    const _PyValue types = _SafeGet("edge:curveType");
    const _PyValue pairs = _SafeGet("edge:vertexIndices");
    const _PyValue positions = _SafeGet("brep:vertexPoint:point:position");
    const _PyValue centers = _SafeGet("brep:edge3dCircle:curve3d:circle:center");
    const _PyValue radii = _SafeGet("brep:edge3dCircle:curve3d:circle:radius");
    if (types.Len() == 0 || pairs.Len() == 0 || positions.Len() == 0
        || centers.Len() == 0 || radii.Len() == 0 || !pairs.IsTuples()
        || pairs.Dim() < 2 || !positions.IsTuples() || positions.Dim() < 3
        || !centers.IsTuples() || centers.Dim() < 3) {
        return;
    }
    const std::unordered_map<long long, size_t> edgeBreps = _EdgeBrepIndices();
    size_t circleIdx = 0;
    for (size_t e = 0; e < types.Len(); ++e) {
        if (types.Repr(e) != "BrepCurve3dCircleAPI") {
            continue;
        }
        if (circleIdx >= centers.Len() || circleIdx >= radii.Len()
            || e >= pairs.Len()) {
            ++circleIdx;
            continue;
        }
        const GfVec3d center(centers.Tup(circleIdx, 0),
                             centers.Tup(circleIdx, 1),
                             centers.Tup(circleIdx, 2));
        double radius = 0.0;
        radii.ToFloat(circleIdx, &radius);
        double tol = 0.0;
        size_t brep = 0;
        if (!_EdgeIntersectTolerance(static_cast<long long>(e), edgeBreps,
                                     &tol, &brep)) {
            _ReportUnresolvedEdgeTolerance("BA.610", static_cast<long long>(e),
                                           "Circle vertex-radius consistency");
            ++circleIdx;
            continue;
        }
        for (size_t k = 0; k < 2; ++k) {
            const long long v = static_cast<long long>(pairs.Tup(e, k));
            if (v < 0 || v >= static_cast<long long>(positions.Len())) {
                continue;
            }
            const size_t vi = static_cast<size_t>(v);
            const GfVec3d p(positions.Tup(vi, 0), positions.Tup(vi, 1),
                            positions.Tup(vi, 2));
            const double dist = (p - center).GetLength();
            if (std::abs(dist - radius) > tol) {
                _Fail("BA.610", TfStringPrintf(
                    "Circle edge #%zu %s vertex #%lld is at distance %.6f from "
                    "center (%.6f, %.6f, %.6f), but radius is %.6f. Difference "
                    "= %.6f exceeds brep:intersectTol3d[%zu] = %s.",
                    e, k == 0 ? "start" : "end", v, dist, center[0], center[1],
                    center[2], radius, std::abs(dist - radius), brep,
                    _PyValue::NumRepr(tol, false).c_str()));
                break;
            }
        }
        ++circleIdx;
    }
}

// _validate_angular_range_primary_period: BA.630 a circle or ellipse edge's
// range max, and BA.631 a periodic face's U max, lies in [0, 2*pi] (+-1e-6).
void
_BrepChecker::ValidateAngularRangePrimaryPeriod()
{
    const _PyValue curveTypes = _SafeGet("edge:curveType");
    const _PyValue &rawEdge = _Get("edge:range");
    if (!rawEdge.IsUnregistered()) {
        const _PyValue ranges = rawEdge.OrEmpty();
        if (curveTypes.Len() > 0 && ranges.Truthy()
            && ranges.Len() >= curveTypes.Len() * 2) {
            for (size_t e = 0; e < curveTypes.Len(); ++e) {
                const std::string type = curveTypes.Repr(e);
                if (type != "BrepCurve3dCircleAPI"
                    && type != "BrepCurve3dEllipseAPI") {
                    continue;
                }
                double mx = 0.0;
                if (!ranges.ToFloat(2 * e + 1, &mx)) {
                    continue;
                }
                if (mx < -_PyPeriodTolerance
                    || mx > _PyTwoPi + _PyPeriodTolerance) {
                    _Fail("BA.630", TfStringPrintf(
                        "%s edge #%zu range max = %.6f is outside the primary "
                        "period (0, 2*pi] = (0, %.6f].",
                        TfStringReplace(TfStringReplace(type, "BrepCurve3d",
                                                        ""),
                                        "API", "")
                            .c_str(),
                        e, mx, _PyTwoPi));
                }
            }
        }
    }

    const _PyValue surfaceTypes = _SafeGet("face:surfaceType");
    const _PyValue &rawFace = _Get("face:range");
    if (rawFace.IsUnregistered()) {
        return;
    }
    const _PyValue faceRanges = rawFace.OrEmpty();
    if (surfaceTypes.Len() == 0 || !faceRanges.Truthy()
        || faceRanges.Len() < surfaceTypes.Len() * 2 || !faceRanges.IsTuples()
        || faceRanges.Dim() < 1) {
        return;
    }
    for (size_t f = 0; f < surfaceTypes.Len(); ++f) {
        const std::string type = surfaceTypes.Repr(f);
        if (type != "BrepSurfaceCylinderAPI" && type != "BrepSurfaceConeAPI"
            && type != "BrepSurfaceSphereAPI" && type != "BrepSurfaceTorusAPI") {
            continue;
        }
        const double uMax = faceRanges.Tup(2 * f + 1, 0);
        if (uMax < -_PyPeriodTolerance || uMax > _PyTwoPi + _PyPeriodTolerance) {
            _Fail("BA.631", TfStringPrintf(
                "%s face #%zu range U-max = %.6f is outside the primary period "
                "(0, 2*pi] = (0, %.6f].",
                TfStringReplace(TfStringReplace(type, "BrepSurface", ""), "API",
                                "")
                    .c_str(),
                f, uMax, _PyTwoPi));
        }
    }
}

// _validate_face_v_domain_ordering (BA.640): a cylinder or cone face's V-min
// does not exceed its V-max. A NaN bound compares false and passes.
void
_BrepChecker::ValidateFaceVDomainOrdering()
{
    const _PyValue types = _SafeGet("face:surfaceType");
    const _PyValue &raw = _Get("face:range");
    if (raw.IsUnregistered()) {
        return;
    }
    const _PyValue ranges = raw.OrEmpty();
    if (types.Len() == 0 || !ranges.Truthy() || !ranges.IsTuples()
        || ranges.Dim() < 2 || ranges.Len() < types.Len() * 2) {
        return;
    }
    const std::vector<long long> &faceOffsets = _Offsets().faces;
    for (size_t f = 0; f < types.Len(); ++f) {
        const std::string type = types.Repr(f);
        if (type != "BrepSurfaceCylinderAPI" && type != "BrepSurfaceConeAPI") {
            continue;
        }
        const double vMin = ranges.Tup(2 * f, 1);
        const double vMax = ranges.Tup(2 * f + 1, 1);
        if (!(vMin > vMax)) {
            continue;
        }
        size_t brep = 0, local = f;
        if (_FindBrep(faceOffsets, static_cast<double>(f), &brep)) {
            local = f - static_cast<size_t>(faceOffsets[brep]);
        } else {
            brep = 0;
        }
        _Fail("BA.640", TfStringPrintf(
            "%s face #%zu in brep #%zu has V-min (%.6f) > V-max (%.6f). "
            "V-domain must be ordered (V-min <= V-max).",
            TfStringReplace(TfStringReplace(type, "BrepSurface", ""), "API", "")
                .c_str(),
            local, brep, vMin, vMax));
    }
}

// _validate_float_arrays_finite (BA.660): no authored floating-point array
// holds a NaN or an Inf. The first one found is the prim's one finding.
void
_BrepChecker::ValidateFloatArraysFinite()
{
    static const char *const names[] = {
        "brep:intersectTol3d", "brep:extent", "face:range", "edge:range",
        "wireEdge:range", "brep:edge3dNurb:curve3d:nurb:controlVertices",
        "brep:edge3dNurb:curve3d:nurb:knots",
        "brep:edge3dNurb:curve3d:nurb:weights",
        "brep:wireEdge3dNurb:curve3d:nurb:controlVertices",
        "brep:wireEdge3dNurb:curve3d:nurb:knots",
        "brep:wireEdge3dNurb:curve3d:nurb:weights",
        "brep:curveUv:nurb:controlVertices", "brep:curveUv:nurb:knots",
        "brep:curveUv:nurb:weights", "brep:surface:nurb:controlVertices",
        "brep:surface:nurb:uKnots", "brep:surface:nurb:vKnots",
        "brep:surface:nurb:weights", "brep:surface:sphere:center",
        "brep:surface:sphere:axis", "brep:surface:sphere:refDirection",
        "brep:surface:sphere:radius", "brep:surface:plane:origin",
        "brep:surface:plane:axis", "brep:surface:plane:refDirection",
        "brep:surface:cylinder:origin", "brep:surface:cylinder:axis",
        "brep:surface:cylinder:refDirection", "brep:surface:cylinder:radius",
        "brep:surface:cone:origin", "brep:surface:cone:axis",
        "brep:surface:cone:refDirection", "brep:surface:cone:radius",
        "brep:surface:cone:semiAngle", "brep:surface:torus:origin",
        "brep:surface:torus:axis", "brep:surface:torus:refDirection",
        "brep:surface:torus:majorRadius", "brep:surface:torus:minorRadius",
        "brep:vertexPoint:point:position", "brep:shellPoint:point:position",
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
    for (const char *name : names) {
        if (!_IsAuthored(name)) {
            continue;
        }
        const _PyValue &values = _Get(name);
        if (!values.IsSequence()) {
            continue;
        }
        const size_t width = values.IsTuples() ? values.Dim()
            : values.IsNumbers()               ? 1
                                               : 0;
        if (width == 0) {
            continue;
        }
        for (size_t i = 0; i < values.Len(); ++i) {
            for (size_t c = 0; c < width; ++c) {
                const double x = values.IsTuples() ? values.Tup(i, c)
                                                   : values.Num(i);
                if (!std::isfinite(x)) {
                    _Fail("BA.660", TfStringPrintf("%s[%zu] contains NaN or Inf "
                                                   "value.",
                                                   name, i));
                    return;
                }
            }
        }
    }
}

// _validate_geomsubset_materials: for each GeomSubset child with elementType
// "brep" or "face", BA.680 the first index outside [0, count), BA.681 the
// first index another subset of that elementType already claimed, and BA.682
// each material:binding target that is not on the stage.
void
_BrepChecker::ValidateGeomsubsetMaterials()
{
    size_t numBreps = _SafeGet("brep:regionCount").Len();
    size_t numFaces = _SafeGet("face:surfaceType").Len();
    if (numFaces == 0) {
        numFaces = _SafeGet("face:loopCount").Len();
    }
    const UsdStageWeakPtr stage = _prim.GetStage();
    std::map<long long, std::string> brepSeen, faceSeen;
    for (const UsdPrim &child : _prim.GetAllChildren()) {
        if (child.GetTypeName() != TfToken("GeomSubset")) {
            continue;
        }
        const UsdAttribute elementTypeAttr
            = child.GetAttribute(TfToken("elementType"));
        if (!elementTypeAttr) {
            continue;
        }
        const _PyValue elementTypeValue = _PyValue::Read(elementTypeAttr);
        const std::string elementType = elementTypeValue.IsNone()
            ? std::string("None")
            : elementTypeValue.Repr(0);
        if (elementType != "brep" && elementType != "face") {
            continue;
        }
        const UsdAttribute indicesAttr = child.GetAttribute(TfToken("indices"));
        if (!indicesAttr) {
            continue;
        }
        const _PyValue indices = _PyValue::Read(indicesAttr);
        if (indices.IsNone() || !indices.IsNumbers()) {
            continue;
        }
        const bool isBrep = elementType == "brep";
        const size_t upper = isBrep ? numBreps : numFaces;
        std::map<long long, std::string> &seen = isBrep ? brepSeen : faceSeen;
        const std::string name = child.GetName().GetString();
        for (size_t i = 0; i < indices.Len(); ++i) {
            long long idx = 0;
            indices.ToInt(i, &idx);
            if (upper > 0
                && (idx < 0 || idx >= static_cast<long long>(upper))) {
                _Fail("BA.680", TfStringPrintf(
                    "GeomSubset '%s' has %s index %lld outside valid range "
                    "[0, %zu).",
                    name.c_str(), elementType.c_str(), idx, upper));
                break;
            }
        }
        for (size_t i = 0; i < indices.Len(); ++i) {
            long long idx = 0;
            indices.ToInt(i, &idx);
            const auto it = seen.find(idx);
            if (it != seen.end()) {
                _Fail("BA.681", TfStringPrintf(
                    "GeomSubset '%s': %s index %lld also appears in subset "
                    "'%s'.",
                    name.c_str(), elementType.c_str(), idx,
                    it->second.c_str()));
                break;
            }
            seen[idx] = name;
        }
        if (!stage) {
            continue;
        }
        const UsdRelationship binding
            = child.GetRelationship(TfToken("material:binding"));
        if (!binding) {
            continue;
        }
        SdfPathVector targets;
        binding.GetTargets(&targets);
        for (const SdfPath &target : targets) {
            if (!stage->GetPrimAtPath(target)) {
                _Fail("BA.682", TfStringPrintf(
                    "GeomSubset '%s' material:binding target '%s' does not "
                    "exist on stage.",
                    name.c_str(), target.GetText()));
            }
        }
    }
}

// _de_boor_evaluate_2d: a rational B-spline curve in the plane, evaluated as
// _PyDeBoorEvaluate3d evaluates one in space.
bool
_PyDeBoorEvaluate2d(long long order, const std::vector<double> &knots,
                    const std::vector<GfVec2d> &cvs,
                    const std::vector<double> &weights, double t, GfVec2d *out)
{
    std::vector<GfVec3d> lifted;
    lifted.reserve(cvs.size());
    for (const GfVec2d &c : cvs) {
        lifted.emplace_back(c[0], c[1], 0.0);
    }
    GfVec3d p;
    if (!_PyDeBoorEvaluate3d(order, knots, lifted, weights, t, &p)) {
        return false;
    }
    *out = GfVec2d(p[0], p[1]);
    return true;
}

// The periodic surfaces and which face:range axes are angular: U for
// cylinders, cones and spheres, U and V for tori.
std::vector<std::pair<const char *, size_t>>
_PyPeriodicAxes(const std::string &surfaceType)
{
    if (surfaceType == "BrepSurfaceCylinderAPI"
        || surfaceType == "BrepSurfaceConeAPI"
        || surfaceType == "BrepSurfaceSphereAPI") {
        return { { "U", 0 } };
    }
    if (surfaceType == "BrepSurfaceTorusAPI") {
        return { { "U", 0 }, { "V", 1 } };
    }
    return {};
}

std::string
_PySurfaceLabel(const std::string &surfaceType)
{
    return TfStringReplace(TfStringReplace(surfaceType, "BrepSurface", ""),
                           "API", "");
}

// _validate_full_period_face_seam_edgeuse_heuristic (BA.761): a face whose U
// (or a torus's V) spans a full period repeats some edgeuse:edgeIndex within
// its loops -- once per full-period axis -- the topological sign of a seam
// edge used twice. A heuristic: the repeat does not prove the repeated edge
// is the geometric seam.
void
_BrepChecker::ValidateFullPeriodFaceSeamEdgeuseHeuristic()
{
    const _PyValue types = _SafeGet("face:surfaceType");
    const _PyValue &raw = _Get("face:range");
    if (raw.IsUnregistered()) {
        return;
    }
    const _PyValue ranges = raw.OrEmpty();
    const _PyValue loopCounts = _SafeGet("face:loopCount");
    const _PyValue edgeuseCounts = _SafeGet("loop:edgeuseCount");
    const _PyValue edgeIndex = _SafeGet("edgeuse:edgeIndex");
    // edgeuse:edgeIndex need not be populated: a full-period face whose loops
    // carry no edgeuses is the "no seam" case this rule exists for.
    if (types.Len() == 0 || !ranges.Truthy() || loopCounts.Len() == 0
        || !ranges.IsTuples() || ranges.Dim() < 2) {
        return;
    }
    const size_t numFaces
        = std::min({ types.Len(), loopCounts.Len(), ranges.Len() / 2 });
    if (numFaces == 0) {
        return;
    }
    std::vector<long long> loopStarts;
    long long running = 0;
    for (size_t i = 0; i < edgeuseCounts.Len(); ++i) {
        long long c = 0;
        if (!edgeuseCounts.ToInt(i, &c)) {
            return;
        }
        loopStarts.push_back(running);
        running += c;
    }
    const std::vector<long long> &faceOffsets = _Offsets().faces;
    long long loopOffset = 0;
    for (size_t f = 0; f < numFaces; ++f) {
        const std::string type = types.Repr(f);
        long long loopCount = 0;
        if (!loopCounts.ToInt(f, &loopCount)) {
            continue;
        }
        const double uSpan = ranges.Tup(2 * f + 1, 0) - ranges.Tup(2 * f, 0);
        const double vSpan = ranges.Tup(2 * f + 1, 1) - ranges.Tup(2 * f, 1);
        std::vector<std::string> fullAxes;
        for (const auto &axis : _PyPeriodicAxes(type)) {
            const double span = axis.second == 0 ? uSpan : vSpan;
            if (std::abs(span - _PyTwoPi) <= _PyPeriodTolerance) {
                fullAxes.push_back(axis.first);
            }
        }
        if (fullAxes.empty()) {
            loopOffset += std::max(loopCount, 0LL);
            continue;
        }
        if (loopCount <= 0
            || loopOffset + loopCount
                > static_cast<long long>(edgeuseCounts.Len())) {
            loopOffset += std::max(loopCount, 0LL);
            continue;
        }
        std::vector<long long> faceEdges;
        bool complete = true;
        for (long long lp = loopOffset; lp < loopOffset + loopCount; ++lp) {
            const long long start = loopStarts[static_cast<size_t>(lp)];
            long long count = 0;
            edgeuseCounts.ToInt(static_cast<size_t>(lp), &count);
            const long long end = start + count;
            if (end > static_cast<long long>(edgeIndex.Len())) {
                complete = false;
                break;
            }
            for (long long eu = start; eu < end; ++eu) {
                long long e = 0;
                size_t i = 0;
                if (_PyIndex(eu, edgeIndex.Len(), &i)
                    && edgeIndex.ToInt(i, &e)) {
                    faceEdges.push_back(e);
                }
            }
        }
        loopOffset += std::max(loopCount, 0LL);
        if (!complete) {
            continue;
        }
        std::map<long long, size_t> edgeCounts;
        for (const long long e : faceEdges) {
            ++edgeCounts[e];
        }
        size_t seamSignals = 0;
        for (const auto &kv : edgeCounts) {
            seamSignals += kv.second > 1 ? 1 : 0;
        }
        if (seamSignals >= fullAxes.size()) {
            continue;
        }
        size_t brep = 0, local = f;
        if (_FindBrep(faceOffsets, static_cast<double>(f), &brep)) {
            local = f - static_cast<size_t>(faceOffsets[brep]);
        } else {
            brep = 0;
        }
        _Fail("BA.761", TfStringPrintf(
            "%s face #%zu in brep #%zu has a full-period %s domain but no "
            "repeated edgeuse:edgeIndex within the face. Full-period periodic "
            "faces are expected to expose seam-like topology as multiple "
            "edgeuses on the same 3D edge; this is a schema-level heuristic "
            "and does not prove a geometric seam exists.",
            _PySurfaceLabel(type).c_str(), local, brep,
            TfStringJoin(fullAxes, "/").c_str()));
    }
}

// _validate_analytic_periodic_domain_bounds (BA.765): a partial-period
// angular face:range stays inside [0, 2*pi] -- for the U axis only its
// minimum is tested, for a torus's V both ends.
void
_BrepChecker::ValidateAnalyticPeriodicDomainBounds()
{
    const _PyValue types = _SafeGet("face:surfaceType");
    const _PyValue &raw = _Get("face:range");
    if (raw.IsUnregistered()) {
        return;
    }
    const _PyValue ranges = raw.OrEmpty();
    if (types.Len() == 0 || !ranges.Truthy() || !ranges.IsTuples()
        || ranges.Dim() < 2 || ranges.Len() < types.Len() * 2) {
        return;
    }
    for (size_t f = 0; f < types.Len(); ++f) {
        const std::string type = types.Repr(f);
        for (const auto &axis : _PyPeriodicAxes(type)) {
            const double mn = ranges.Tup(2 * f, axis.second);
            const double mx = ranges.Tup(2 * f + 1, axis.second);
            if (std::abs((mx - mn) - _PyTwoPi) <= _PyPeriodTolerance) {
                continue;
            }
            const bool minOut = mn < -_PyPeriodTolerance;
            const bool maxOut
                = axis.second != 0 && mx > _PyTwoPi + _PyPeriodTolerance;
            if (!minOut && !maxOut) {
                continue;
            }
            _Fail("BA.765", TfStringPrintf(
                "%s face #%zu has partial-period %s range [%.6f, %.6f] rad "
                "outside the primary angular domain [0, 2*pi] = [0.000000, "
                "%.6f].",
                _PySurfaceLabel(type).c_str(), f, axis.first, mn, mx,
                _PyTwoPi));
        }
    }
}

// _validate_full_period_face_domain_alignment (BA.762): a full-period
// angular face:range is authored as [0, 2*pi], not an equivalent shifted
// interval.
void
_BrepChecker::ValidateFullPeriodFaceDomainAlignment()
{
    const _PyValue types = _SafeGet("face:surfaceType");
    const _PyValue &raw = _Get("face:range");
    if (raw.IsUnregistered()) {
        return;
    }
    const _PyValue ranges = raw.OrEmpty();
    if (types.Len() == 0 || !ranges.Truthy() || !ranges.IsTuples()
        || ranges.Dim() < 2 || ranges.Len() < types.Len() * 2) {
        return;
    }
    for (size_t f = 0; f < types.Len(); ++f) {
        const std::string type = types.Repr(f);
        for (const auto &axis : _PyPeriodicAxes(type)) {
            const double mn = ranges.Tup(2 * f, axis.second);
            const double mx = ranges.Tup(2 * f + 1, axis.second);
            if (std::abs((mx - mn) - _PyTwoPi) > _PyPeriodTolerance) {
                continue;
            }
            if (std::abs(mn) <= _PyPeriodTolerance
                && std::abs(mx - _PyTwoPi) <= _PyPeriodTolerance) {
                continue;
            }
            _Fail("BA.762", TfStringPrintf(
                "%s face #%zu has a full-period %s range [%.6f, %.6f] rad. "
                "Full-period angular domains must be aligned to [0, 2*pi] = "
                "[0.000000, %.6f].",
                _PySurfaceLabel(type).c_str(), f, axis.first, mn, mx,
                _PyTwoPi));
        }
    }
}

// _validate_uv_loop_closure (BA.763): with UV pcurves authored for every
// edgeuse, each pcurve's end (a de Boor evaluation at its knot-domain end)
// meets the next pcurve's start in its loop, within 1e-6. Any record the rule
// cannot evaluate ends it without a finding; a loop with a "no pcurve"
// sentinel is skipped.
void
_BrepChecker::ValidateUvLoopClosure()
{
    constexpr double closureTol = 1e-6;
    const std::string base = "brep:curveUv:nurb:";
    const _PyValue orders = _SafeGet(base + "order");
    const _PyValue counts = _SafeGet(base + "vertexCount");
    const _PyValue cvs = _SafeGet(base + "controlVertices");
    const _PyValue weights = _SafeGet(base + "weights");
    const _PyValue knots = _SafeGet(base + "knots");
    const _PyValue loopCounts = _SafeGet("face:loopCount");
    const _PyValue edgeuseCounts = _SafeGet("loop:edgeuseCount");
    const _PyValue edgeIndex = _SafeGet("edgeuse:edgeIndex");
    if (orders.Len() == 0 || counts.Len() == 0 || cvs.Len() == 0
        || weights.Len() == 0 || knots.Len() == 0 || loopCounts.Len() == 0
        || edgeuseCounts.Len() == 0 || edgeIndex.Len() == 0) {
        return;
    }
    const size_t numEdgeuses = edgeIndex.Len();
    if (orders.Len() < numEdgeuses || counts.Len() < numEdgeuses) {
        return;
    }
    if (!cvs.IsTuples() || cvs.Dim() < 2) {
        return;
    }
    struct Ends
    {
        bool sentinel = true;
        GfVec2d start, end;
    };
    std::vector<Ends> curveEnds;
    long long cvOffset = 0, knotOffset = 0;
    for (size_t c = 0; c < numEdgeuses; ++c) {
        long long order = 0, numCvs = 0;
        if (!orders.ToInt(c, &order) || !counts.ToInt(c, &numCvs)) {
            return;
        }
        if (order == 0 && numCvs == 0) {
            curveEnds.push_back(Ends());
            continue;
        }
        if (order < 1 || numCvs < order) {
            return;
        }
        const long long numKnots = numCvs + order;
        if (cvOffset + numCvs > static_cast<long long>(cvs.Len())
            || cvOffset + numCvs > static_cast<long long>(weights.Len())
            || knotOffset + numKnots > static_cast<long long>(knots.Len())) {
            return;
        }
        std::vector<double> k, w;
        std::vector<GfVec2d> p;
        for (long long i = 0; i < numKnots; ++i) {
            double x = 0.0;
            knots.ToFloat(static_cast<size_t>(knotOffset + i), &x);
            k.push_back(x);
        }
        for (long long j = 0; j < numCvs; ++j) {
            const size_t i = static_cast<size_t>(cvOffset + j);
            p.emplace_back(cvs.Tup(i, 0), cvs.Tup(i, 1));
            double x = 0.0;
            weights.ToFloat(i, &x);
            w.push_back(x);
        }
        Ends ends;
        ends.sentinel = false;
        if (!_PyDeBoorEvaluate2d(order, k, p, w, k[order - 1], &ends.start)
            || !_PyDeBoorEvaluate2d(order, k, p, w, k[numCvs], &ends.end)) {
            return;
        }
        curveEnds.push_back(ends);
        cvOffset += numCvs;
        knotOffset += numKnots;
    }

    size_t loopIdx = 0;
    long long edgeuseOffset = 0;
    for (size_t f = 0; f < loopCounts.Len(); ++f) {
        long long numLoops = 0;
        if (!loopCounts.ToInt(f, &numLoops)) {
            return;
        }
        for (long long localLoop = 0; localLoop < numLoops; ++localLoop) {
            if (loopIdx >= edgeuseCounts.Len()) {
                return;
            }
            long long numEdgeusesInLoop = 0;
            if (!edgeuseCounts.ToInt(loopIdx, &numEdgeusesInLoop)) {
                return;
            }
            const long long loopStart = edgeuseOffset;
            const long long loopEnd = edgeuseOffset + numEdgeusesInLoop;
            ++loopIdx;
            edgeuseOffset = loopEnd;
            if (numEdgeusesInLoop <= 0) {
                continue;
            }
            if (loopEnd > static_cast<long long>(curveEnds.size())) {
                return;
            }
            if (loopStart < 0) {
                // Only reachable after a negative edgeuse count, where Python
                // slices from the end of the list; there is no loop to close.
                continue;
            }
            bool anySentinel = false;
            for (long long eu = loopStart; eu < loopEnd; ++eu) {
                anySentinel = anySentinel
                    || curveEnds[static_cast<size_t>(eu)].sentinel;
            }
            if (anySentinel) {
                continue;
            }
            for (long long k = 0; k < numEdgeusesInLoop; ++k) {
                const long long eu = loopStart + k;
                const long long next
                    = loopStart + (k + 1) % numEdgeusesInLoop;
                const GfVec2d &end = curveEnds[static_cast<size_t>(eu)].end;
                const GfVec2d &nextStart
                    = curveEnds[static_cast<size_t>(next)].start;
                const double dist = (end - nextStart).GetLength();
                if (dist > closureTol) {
                    _Fail("BA.763", TfStringPrintf(
                        "Face #%zu loop #%lld edgeuse #%lld UV endpoint (%.6f, "
                        "%.6f) does not meet next edgeuse #%lld UV start "
                        "(%.6f, %.6f); gap %.6f exceeds tolerance 1e-06.",
                        f, localLoop, eu, end[0], end[1], next, nextStart[0],
                        nextStart[1], dist));
                }
            }
        }
    }
}

// _validate_zero_length_uv_trim_curves (BA.764): an authored UV pcurve's
// control polygon has a non-zero extent in parameter space.
void
_BrepChecker::ValidateZeroLengthUvTrimCurves()
{
    constexpr double zeroTol = 1e-12;
    const _PyValue orders = _SafeGet("brep:curveUv:nurb:order");
    const _PyValue counts = _SafeGet("brep:curveUv:nurb:vertexCount");
    const _PyValue cvs = _SafeGet("brep:curveUv:nurb:controlVertices");
    if (orders.Len() == 0 || counts.Len() == 0 || cvs.Len() == 0) {
        return;
    }
    long long cvOffset = 0;
    for (size_t c = 0; c < counts.Len(); ++c) {
        if (c >= orders.Len()) {
            return;
        }
        long long order = 0, numCvs = 0;
        if (!orders.ToInt(c, &order) || !counts.ToInt(c, &numCvs)) {
            return;
        }
        if ((order == 0 && numCvs == 0) || numCvs <= 0) {
            continue;
        }
        if (cvOffset + numCvs > static_cast<long long>(cvs.Len())) {
            break;
        }
        const long long first = cvOffset;
        cvOffset += numCvs;
        if (order <= 0 || !cvs.IsTuples() || cvs.Dim() < 2) {
            continue;
        }
        // Python's max() and min() over the list: start from the first
        // element and replace it only on a strict comparison, so a NaN first
        // element survives (and makes the extent NaN, which never fails).
        double uLo = cvs.Tup(static_cast<size_t>(first), 0), uHi = uLo;
        double vLo = cvs.Tup(static_cast<size_t>(first), 1), vHi = vLo;
        for (long long j = first + 1; j < first + numCvs; ++j) {
            const double u = cvs.Tup(static_cast<size_t>(j), 0);
            const double v = cvs.Tup(static_cast<size_t>(j), 1);
            uLo = u < uLo ? u : uLo;
            uHi = u > uHi ? u : uHi;
            vLo = v < vLo ? v : vLo;
            vHi = v > vHi ? v : vHi;
        }
        const double diagonal
            = std::sqrt((uHi - uLo) * (uHi - uLo) + (vHi - vLo) * (vHi - vLo));
        if (diagonal <= zeroTol) {
            _Fail("BA.764", TfStringPrintf(
                "UV trim curve #%zu has collapsed control vertices at (%.6f, "
                "%.6f); control polygon extent %.6e is at or below tolerance "
                "1.0e-12.",
                c, cvs.Tup(static_cast<size_t>(first), 0),
                cvs.Tup(static_cast<size_t>(first), 1), diagonal));
        }
    }
}

// _validate_uv_trim_curve_domain_containment (BA.750): each face's UV pcurve
// control vertices lie within the face's face:range widened by half its span
// (at least half a unit) on each side. The first control vertex outside ends
// the rule.
void
_BrepChecker::ValidateUvTrimCurveDomainContainment()
{
    constexpr double margin = 0.5;
    const _PyValue counts = _SafeGet("brep:curveUv:nurb:vertexCount");
    const _PyValue cvs = _SafeGet("brep:curveUv:nurb:controlVertices");
    const _PyValue &ranges = _Get("face:range");
    const _PyValue loopCounts = _SafeGet("face:loopCount");
    const _PyValue edgeuseCounts = _SafeGet("loop:edgeuseCount");
    if (counts.Len() == 0 || cvs.Len() == 0 || ranges.IsNone()
        || ranges.IsUnregistered() || loopCounts.Len() == 0
        || edgeuseCounts.Len() == 0) {
        return;
    }
    if (!ranges.IsTuples() || ranges.Dim() < 2 || !cvs.IsTuples()
        || cvs.Dim() < 2) {
        return;
    }
    long long loopOffset = 0, euOffset = 0, cvOffset = 0;
    for (size_t f = 0; f < loopCounts.Len(); ++f) {
        if (2 * f + 1 >= ranges.Len()) {
            break;
        }
        const double uMin = ranges.Tup(2 * f, 0), vMin = ranges.Tup(2 * f, 1);
        const double uMax = ranges.Tup(2 * f + 1, 0);
        const double vMax = ranges.Tup(2 * f + 1, 1);
        const double uPad = margin * std::max(std::abs(uMax - uMin), 1.0);
        const double vPad = margin * std::max(std::abs(vMax - vMin), 1.0);
        const double uLo = uMin - uPad, uHi = uMax + uPad;
        const double vLo = vMin - vPad, vHi = vMax + vPad;
        long long numLoops = 0;
        loopCounts.ToInt(f, &numLoops);
        const long long faceEuStart = euOffset;
        for (long long lp = 0; lp < numLoops; ++lp) {
            const long long li = loopOffset + lp;
            long long c = 0;
            if (li >= 0 && li < static_cast<long long>(edgeuseCounts.Len())
                && edgeuseCounts.ToInt(static_cast<size_t>(li), &c)) {
                euOffset += c;
            }
        }
        const long long faceEuEnd = euOffset;
        loopOffset += numLoops;
        const long long stop
            = std::min(faceEuEnd, static_cast<long long>(counts.Len()));
        for (long long eu = faceEuStart; eu < stop; ++eu) {
            if (eu < 0) {
                continue;
            }
            long long numCvs = 0;
            counts.ToInt(static_cast<size_t>(eu), &numCvs);
            for (long long j = 0; j < numCvs; ++j) {
                const long long ci = cvOffset + j;
                if (ci >= static_cast<long long>(cvs.Len())) {
                    break;
                }
                const double u = cvs.Tup(static_cast<size_t>(ci), 0);
                const double v = cvs.Tup(static_cast<size_t>(ci), 1);
                if (u < uLo || u > uHi || v < vLo || v > vHi) {
                    _Fail("BA.750", TfStringPrintf(
                        "Face #%zu edgeuse #%lld UV control vertex [%lld] = "
                        "(%.6f, %.6f) is far outside face UV domain "
                        "[%.4f..%.4f] x [%.4f..%.4f].",
                        f, eu, ci, u, v, uMin, uMax, vMin, vMax));
                    return;
                }
            }
            cvOffset += numCvs;
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
// BrepArrayRanges                                                            //
// -------------------------------------------------------------------------- //
// Parameter ranges: BA.140 face loop counts, BA.145 / BA.155 / BA.160
// face:range structure and intervals, BA.235 / BA.275 edge and wire-edge
// range ordering, BA.630 / BA.631 angular maxima in the primary period,
// BA.640 cylinder and cone V-domain ordering, BA.660 no NaN or Inf in any
// floating-point array.
UsdValidationErrorVector
_BrepArrayRanges(const UsdPrim &usdPrim,
                 const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.140", "BA.145", "BA.155", "BA.160", "BA.235",
                              "BA.275", "BA.630", "BA.631", "BA.640",
                              "BA.660" });
    c.ValidateFaceLoopCountMinimum();
    c.ValidateFaceRanges();
    c.ValidateEdgeArrays();
    c.ValidateWireEdgeArrays();
    c.ValidateAngularRangePrimaryPeriod();
    c.ValidateFaceVDomainOrdering();
    c.ValidateFloatArraysFinite();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayAnalyticSurfaces                                                  //
// -------------------------------------------------------------------------- //
// Analytic surface parameters (plane, cylinder, cone, sphere, torus): array
// sizes per surface type, positive radii, unit-length and orthogonal axis
// frames, cone semiAngle in (0, pi/2) (BA.480-BA.525).
UsdValidationErrorVector
_BrepArrayAnalyticSurfaces(const UsdPrim &usdPrim,
                           const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim,
                   { "BA.480", "BA.481", "BA.482", "BA.483", "BA.484", "BA.490",
                     "BA.491", "BA.492", "BA.493", "BA.500", "BA.501", "BA.502",
                     "BA.503", "BA.504", "BA.510", "BA.511", "BA.512", "BA.513",
                     "BA.514", "BA.515", "BA.520", "BA.521", "BA.522", "BA.523",
                     "BA.524", "BA.525" });
    c.ValidateAnalyticSurfaces();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayAnalyticCurves                                                    //
// -------------------------------------------------------------------------- //
// Analytic curve parameters (circle, line, ellipse; edge and wireEdge):
// array sizes, positive radii, unit-length and orthogonal frames
// (BA.530-BA.555); analytic edge endpoints on their vertices (BA.600, BA.601,
// BA.602) and circle vertices at the radius (BA.610), within each edge's
// Brep's brep:intersectTol3d.
UsdValidationErrorVector
_BrepArrayAnalyticCurves(const UsdPrim &usdPrim,
                         const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim,
                   { "BA.530", "BA.531", "BA.532", "BA.533", "BA.534", "BA.540",
                     "BA.541", "BA.550", "BA.551", "BA.552", "BA.553", "BA.554",
                     "BA.555", "BA.600", "BA.601", "BA.602", "BA.610" });
    c.ValidateAnalyticCurves();
    c.ValidateEdgeCurveEndpointVertexConsistency();
    c.ValidateCircleVertexRadiusConsistency();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArraySpans                                                             //
// -------------------------------------------------------------------------- //
// Periodic domain limits: angular face:range spans at most 2*pi and sphere
// latitude within [-pi/2, pi/2] (BA.560-BA.565); circle and ellipse edge and
// wire-edge parameter spans at most 2*pi (BA.570, BA.571).
UsdValidationErrorVector
_BrepArraySpans(const UsdPrim &usdPrim,
                const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.560", "BA.561", "BA.562", "BA.563", "BA.564",
                              "BA.565", "BA.570", "BA.571" });
    c.ValidateFaceRangeDomainLimits();
    c.ValidateEdgeRangeDomainLimits();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArraySchemaUsage                                                       //
// -------------------------------------------------------------------------- //
// The applied geometry APIs agree with the topology: a used curve, surface or
// point type requires its API (BA.583), and for the analytic surfaces and
// vertex points, use requires data and an applied API requires use (BA.485,
// 495, 505, 516, 526, 305). The NURBS families' equivalents (BA.290, 370, 415,
// 470) report from BrepArrayNurbs.
UsdValidationErrorVector
_BrepArraySchemaUsage(const UsdPrim &usdPrim,
                      const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.305", "BA.485", "BA.495", "BA.505", "BA.516",
                              "BA.526", "BA.583" });
    c.ValidateSchemaConsistency();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayContainment                                                       //
// -------------------------------------------------------------------------- //
// Spatial containment: each brep:extent box inside the prim's extent (BA.040,
// BA.045, BA.050), each Brep's vertex positions (BA.310) and NURBS control
// vertices (BA.365, BA.465) inside its own box, analytic surface origins near
// the extent union (BA.620), and each point shell's position inside its
// Brep's box (BA.710).
UsdValidationErrorVector
_BrepArrayContainment(const UsdPrim &usdPrim,
                      const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.040", "BA.045", "BA.050", "BA.310", "BA.365",
                              "BA.465", "BA.620", "BA.710" });
    c.ValidateBrepExtent();
    c.ValidateVertexPositionContainment();
    c.ValidateEdge3dNurbsControlPointContainment();
    c.ValidateSurfaceNurbsControlPointContainment();
    c.ValidateAnalyticSurfaceOriginContainment();
    c.ValidateShellPointContainment();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayNurbs                                                             //
// -------------------------------------------------------------------------- //
// The NURBS strata -- edge3d (BA.330-371), UV pcurves (BA.375-416), surfaces
// (BA.420-471), wireEdge3d (BA.650-658) -- and the order / vertexCount floors
// shared by all four (BA.590 / BA.591), in the order brep_validator.py runs
// them.
UsdValidationErrorVector
_BrepArrayNurbs(const UsdPrim &usdPrim,
                const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim,
                   { "BA.290", "BA.330", "BA.335", "BA.340", "BA.345", "BA.350",
                     "BA.355", "BA.360", "BA.370", "BA.371", "BA.375", "BA.380",
                     "BA.385", "BA.390", "BA.395", "BA.400", "BA.405", "BA.410",
                     "BA.415", "BA.416", "BA.420", "BA.425", "BA.430", "BA.435",
                     "BA.440", "BA.445", "BA.450", "BA.455", "BA.460", "BA.470",
                     "BA.471", "BA.590", "BA.591", "BA.650", "BA.651", "BA.652",
                     "BA.653", "BA.654", "BA.655", "BA.656", "BA.657",
                     "BA.658" });
    c.ValidateCurve3dNurbControlVerticesWeights();
    c.ValidateCurve3dNurbOrderVertexCount();
    c.ValidateCurve3dKnots();
    c.ValidateCurveUvData();
    c.ValidateSurfaceControlVerticesWeights();
    c.ValidateSurfaceOrdersVertexCounts();
    c.ValidateSurfaceKnots();
    c.ValidateSchemaConsistency();
    c.ValidateNurbsDataCompleteness();
    c.ValidateNurbsMathematicalConsistency();
    c.ValidateAttributeDataTypes();
    c.ValidateNurbsOrderAndVertexCountValues();
    c.ValidateWireEdge3dNurbs();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayEdgeCurveVertices                                                 //
// -------------------------------------------------------------------------- //
// BA.730: a NURBS edge evaluated at its authored edge:range endpoints lands on
// the vertices its edge:vertexIndices name, within its Brep's
// brep:intersectTol3d.
UsdValidationErrorVector
_BrepArrayEdgeCurveVertices(const UsdPrim &usdPrim,
                            const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.730" });
    c.ValidateNurbsEdgeEndpointVertex();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayUvTrim                                                            //
// -------------------------------------------------------------------------- //
// UV trim curves and periodic face domains: BA.750 pcurve control vertices
// near their face's UV domain, BA.761 a full-period face repeats a seam edge,
// BA.762 full-period domains aligned to [0, 2*pi], BA.763 pcurves meet head
// to tail within each loop, BA.764 no pcurve collapses to a point, BA.765
// partial-period domains inside [0, 2*pi].
UsdValidationErrorVector
_BrepArrayUvTrim(const UsdPrim &usdPrim,
                 const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.750", "BA.761", "BA.762", "BA.763", "BA.764",
                              "BA.765" });
    c.ValidateFullPeriodFaceSeamEdgeuseHeuristic();
    c.ValidateAnalyticPeriodicDomainBounds();
    c.ValidateFullPeriodFaceDomainAlignment();
    c.ValidateUvLoopClosure();
    c.ValidateZeroLengthUvTrimCurves();
    c.ValidateUvTrimCurveDomainContainment();
    return c.TakeErrors();
}

// -------------------------------------------------------------------------- //
// BrepArrayGeomSubsets                                                       //
// -------------------------------------------------------------------------- //
// The UsdGeomSubset children of a BrepArray: indices inside the Brep or face
// count (BA.680), no index claimed by two subsets of one elementType
// (BA.681), material:binding targets on the stage (BA.682). Findings are
// reported at the BrepArray, as Python reports them.
UsdValidationErrorVector
_BrepArrayGeomSubsets(const UsdPrim &usdPrim,
                      const UsdValidationTimeRange & /*timeRange*/)
{
    if (!(usdPrim && usdPrim.IsA<UsdSolidBrepArray>())) {
        return {};
    }
    _BrepChecker c(usdPrim, { "BA.680", "BA.681", "BA.682" });
    c.ValidateGeomsubsetMaterials();
    return c.TakeErrors();
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
