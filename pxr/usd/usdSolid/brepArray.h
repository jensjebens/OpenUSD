//
// Copyright 2016 Pixar
//
// Licensed under the terms set forth in the LICENSE.txt file available at
// https://openusd.org/license.
//
#ifndef USDSOLID_GENERATED_BREPARRAY_H
#define USDSOLID_GENERATED_BREPARRAY_H

/// \file usdSolid/brepArray.h

#include "pxr/pxr.h"
#include ".//api.h"
#include "pxr/usd/usdGeom/gprim.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/stage.h"
#include ".//tokens.h"

#include "pxr/base/vt/value.h"

#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

#include "pxr/base/tf/token.h"
#include "pxr/base/tf/type.h"

PXR_NAMESPACE_OPEN_SCOPE

class SdfAssetPath;

// -------------------------------------------------------------------------- //
// BREPARRAY                                                                  //
// -------------------------------------------------------------------------- //

/// \class UsdSolidBrepArray
///
/// Solid boundary representation models (Breps) rigorously partition space into regions by connecting sets of surfaces into region boundaries.  Regions are the set of points that can be connected by curves of any shape that don't cross boundaries.  
/// The boundaries between regions must be watertight to prevent the points of each region from being connectable to one another.  Manifold solid objects partition space into one solid region and one or more void regions. 
/// Non-manifold objects can partition space from one to any number of regions, where every point in space classifies to one of the model's regions.  
/// In the world of geometric modeling, where mathematical approximations of shape are rife, gaps between adjacent surfaces are common. 
/// In this model, the connections of adjacent surfaces are explicit objects that can fill the gaps and create the necessary partition of space.
/// 
/// This model is comprised of 3 parts: shapes, topology objects, and special connectivity objects called "uses." For a thorough description of this model, see the Solid Models USD Proposal.
/// 
/// Rules and restrictions on topology and geometry are listed in the proposal. They will be migrated to this schema when the AOUSD Geometry WG is aligned on a design.
/// 
/// For compact storage of the radial edge data model redundant elements are removed from the flattened representation.  There are no attributes for Vertexuses and Loopuses.  
/// The lists of Edgeuses represents pairs, so the arrays size() are half the number of Edgeuses in the Brep model.
/// 
/// Objects related to a single Brep must be consecutive in the BrepArray. For example, if Brep_1 has a total of 3 shells, the Brep_2 startShellIndx for its first region would be the number 3.  
/// Another example, the Edges of Brep_i are the brep:edgeCount[ii] consecutive Edges starting at SUM(brep:edgeCount[n]), for n in [0,ii).  
///
class UsdSolidBrepArray : public UsdGeomGprim
{
public:
    /// Compile time constant representing what kind of schema this class is.
    ///
    /// \sa UsdSchemaKind
    static const UsdSchemaKind schemaKind = UsdSchemaKind::ConcreteTyped;

    /// Construct a UsdSolidBrepArray on UsdPrim \p prim .
    /// Equivalent to UsdSolidBrepArray::Get(prim.GetStage(), prim.GetPath())
    /// for a \em valid \p prim, but will not immediately throw an error for
    /// an invalid \p prim
    explicit UsdSolidBrepArray(const UsdPrim& prim=UsdPrim())
        : UsdGeomGprim(prim)
    {
    }

    /// Construct a UsdSolidBrepArray on the prim held by \p schemaObj .
    /// Should be preferred over UsdSolidBrepArray(schemaObj.GetPrim()),
    /// as it preserves SchemaBase state.
    explicit UsdSolidBrepArray(const UsdSchemaBase& schemaObj)
        : UsdGeomGprim(schemaObj)
    {
    }

    /// Destructor.
    USDSOLID_API
    virtual ~UsdSolidBrepArray();

    /// Return a vector of names of all pre-declared attributes for this schema
    /// class and all its ancestor classes.  Does not include attributes that
    /// may be authored by custom/extended methods of the schemas involved.
    USDSOLID_API
    static const TfTokenVector &
    GetSchemaAttributeNames(bool includeInherited=true);

    /// Return a UsdSolidBrepArray holding the prim adhering to this
    /// schema at \p path on \p stage.  If no prim exists at \p path on
    /// \p stage, or if the prim at that path does not adhere to this schema,
    /// return an invalid schema object.  This is shorthand for the following:
    ///
    /// \code
    /// UsdSolidBrepArray(stage->GetPrimAtPath(path));
    /// \endcode
    ///
    USDSOLID_API
    static UsdSolidBrepArray
    Get(const UsdStagePtr &stage, const SdfPath &path);

    /// Attempt to ensure a \a UsdPrim adhering to this schema at \p path
    /// is defined (according to UsdPrim::IsDefined()) on this stage.
    ///
    /// If a prim adhering to this schema at \p path is already defined on this
    /// stage, return that prim.  Otherwise author an \a SdfPrimSpec with
    /// \a specifier == \a SdfSpecifierDef and this schema's prim type name for
    /// the prim at \p path at the current EditTarget.  Author \a SdfPrimSpec s
    /// with \p specifier == \a SdfSpecifierDef and empty typeName at the
    /// current EditTarget for any nonexistent, or existing but not \a Defined
    /// ancestors.
    ///
    /// The given \a path must be an absolute prim path that does not contain
    /// any variant selections.
    ///
    /// If it is impossible to author any of the necessary PrimSpecs, (for
    /// example, in case \a path cannot map to the current UsdEditTarget's
    /// namespace) issue an error and return an invalid \a UsdPrim.
    ///
    /// Note that this method may return a defined prim whose typeName does not
    /// specify this schema class, in case a stronger typeName opinion overrides
    /// the opinion at the current EditTarget.
    ///
    USDSOLID_API
    static UsdSolidBrepArray
    Define(const UsdStagePtr &stage, const SdfPath &path);

protected:
    /// Returns the kind of schema this class belongs to.
    ///
    /// \sa UsdSchemaKind
    USDSOLID_API
    UsdSchemaKind _GetSchemaKind() const override;

private:
    // needs to invoke _GetStaticTfType.
    friend class UsdSchemaRegistry;
    USDSOLID_API
    static const TfType &_GetStaticTfType();

    static bool _IsTypedSchema();

    // override SchemaBase virtuals.
    USDSOLID_API
    const TfType &_GetTfType() const override;

public:
    // --------------------------------------------------------------------- //
    // BREPINTERSECTTOL3D 
    // --------------------------------------------------------------------- //
    /// Max distance at which two objects intersect and min distance at which two points are distinct. size() = number of Breps. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] brep:intersectTol3d` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetBrepIntersectTol3dAttr() const;

    /// See GetBrepIntersectTol3dAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateBrepIntersectTol3dAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BREPEXTENT 
    // --------------------------------------------------------------------- //
    /// Brep_ii's bounding box corner pts {XYZmin, XYZmax}. size() = 2 * number of Breps. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double3[] brep:extent` |
    /// | C++ Type | VtArray<GfVec3d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Double3Array |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetBrepExtentAttr() const;

    /// See GetBrepExtentAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateBrepExtentAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // BREPREGIONCOUNT 
    // --------------------------------------------------------------------- //
    /// Number of Regions in this Brep, INCLUDING the infinite exterior void region, which is always the first region of each Brep. A closed manifold solid therefore has regionCount = 2 (the infinite void region + the interior solid region). size() = Number of Breps. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] brep:regionCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetBrepRegionCountAttr() const;

    /// See GetBrepRegionCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateBrepRegionCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // REGIONSHELLCOUNT 
    // --------------------------------------------------------------------- //
    /// Region_ii's number of Shells.  1st shell = outerShell, subsequent shells = innerShells. size() = number of regions. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] region:shellCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetRegionShellCountAttr() const;

    /// See GetRegionShellCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateRegionShellCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // REGIONTYPE 
    // --------------------------------------------------------------------- //
    /// solidRegion = region_ii points are in the Brep. voidRegion = region_ii points are out of the Brep. The first region of each Brep is always the infinite exterior voidRegion (consistent with brep:regionCount counting it). size() = number of regions. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] region:type` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | solidRegion, voidRegion |
    USDSOLID_API
    UsdAttribute GetRegionTypeAttr() const;

    /// See GetRegionTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateRegionTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SHELLFACEUSECOUNT 
    // --------------------------------------------------------------------- //
    /// Shell_ii's number of faceuses. size() = number of Shells 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] shell:faceuseCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetShellFaceuseCountAttr() const;

    /// See GetShellFaceuseCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateShellFaceuseCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SHELLWIREEDGECOUNT 
    // --------------------------------------------------------------------- //
    /// Shell_ii's number of connected wireEdges. size() = number of Shells 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] shell:wireEdgeCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetShellWireEdgeCountAttr() const;

    /// See GetShellWireEdgeCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateShellWireEdgeCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // SHELLPOINTTYPE 
    // --------------------------------------------------------------------- //
    /// Shell_ii's point type when shell:facuseCount[ii] and shell:wireEdgeCount[ii] are 0, else ignored. 
    /// BrepPointAPI = Shell_ii's shape is a point in the ShellPoint array. 
    /// size() = number of Shells. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] shell:pointType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | BrepPointAPI, none |
    USDSOLID_API
    UsdAttribute GetShellPointTypeAttr() const;

    /// See GetShellPointTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateShellPointTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // FACEUSEFACEINDEX 
    // --------------------------------------------------------------------- //
    /// Faceuse_ii's face index into face arrays. size() = number of faceuses = 2 x number of faces.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] faceuse:faceIndex` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetFaceuseFaceIndexAttr() const;

    /// See GetFaceuseFaceIndexAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateFaceuseFaceIndexAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // FACEUSEORIENTATIONTYPE 
    // --------------------------------------------------------------------- //
    /// same     = the side of the face in the direction pointed to by the face's surface normal.
    /// opposite = the opposite side of the face. size() = number of faceuses = 2 x number of faces.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] faceuse:orientationType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | same, opposite |
    USDSOLID_API
    UsdAttribute GetFaceuseOrientationTypeAttr() const;

    /// See GetFaceuseOrientationTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateFaceuseOrientationTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // FACELOOPCOUNT 
    // --------------------------------------------------------------------- //
    /// face_ii's number of Loops.  1st loop = outerLoop, subsequent loops = innerLoops. size() = number of faces.
    /// Outer vs inner is identified structurally by stored order: the FIRST loop is the single
    /// outer loop (proposal Rule 424) and the rest are inner (hole) loops. Some producers' native
    /// formats (e.g. OCCT .brep) do not flag the outer wire and instead detect it geometrically
    /// (largest UV-domain bounding-box area); in this schema outer-first is a convention, so a
    /// producer emits the outer loop first and a consumer need not detect it. The loops' winding
    /// (outer CCW / inner CW as seen from the owning faceuse's designated side) follows from
    /// edgeuse:orientationType; see edgeuse:orientationType for the full rule. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] face:loopCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetFaceLoopCountAttr() const;

    /// See GetFaceLoopCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateFaceLoopCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // FACESURFACETYPE 
    // --------------------------------------------------------------------- //
    /// BrepSurface APIs define the surface geometry for each face.
    /// BrepSurfaceNurbAPI     = face_ii shape is a 3d NURB function (see BrepSurfaceNurbAPI).
    /// BrepSurfacePlaneAPI    = face_ii shape is an analytic 3d plane (see BrepSurfacePlaneAPI).
    /// BrepSurfaceCylinderAPI = face_ii shape is an analytic 3d cylinder (see BrepSurfaceCylinderAPI).
    /// BrepSurfaceConeAPI     = face_ii shape is an analytic 3d cone (see BrepSurfaceConeAPI).
    /// BrepSurfaceSphereAPI   = face_ii shape is an analytic 3d sphere (see BrepSurfaceSphereAPI).
    /// BrepSurfaceTorusAPI    = face_ii shape is an analytic 3d torus (see BrepSurfaceTorusAPI).
    /// size() = number of faces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] face:surfaceType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | BrepSurfaceNurbAPI, BrepSurfacePlaneAPI, BrepSurfaceCylinderAPI, BrepSurfaceConeAPI, BrepSurfaceSphereAPI, BrepSurfaceTorusAPI |
    USDSOLID_API
    UsdAttribute GetFaceSurfaceTypeAttr() const;

    /// See GetFaceSurfaceTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateFaceSurfaceTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // FACETRIMTYPE 
    // --------------------------------------------------------------------- //
    /// rectangular = face_ii's outerLoop is a rectangle in the face's parameter space consisting of 4 isoparameter UVTrimCurves. 
    /// general     = face_ii's outerLoop is any other shape. size() = number of faces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] face:trimType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | rectangular, general |
    USDSOLID_API
    UsdAttribute GetFaceTrimTypeAttr() const;

    /// See GetFaceTrimTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateFaceTrimTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // FACERANGE 
    // --------------------------------------------------------------------- //
    /// face_ii's domain range corner pts {UVmin, UVmax}. size() = 2 * number of faces. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double2[] face:range` |
    /// | C++ Type | VtArray<GfVec2d> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Double2Array |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetFaceRangeAttr() const;

    /// See GetFaceRangeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateFaceRangeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // LOOPEDGEUSECOUNT 
    // --------------------------------------------------------------------- //
    /// Loop_ii's number of head-to-tail connected edgeuses. size() = number of Loops.
    /// The edgeuses are stored in traversal order; that order plus each edgeuse:orientationType
    /// fixes the loop's winding (outer loop CCW / inner loop CW as seen from the owning faceuse's
    /// designated side, material on the left). See edgeuse:orientationType for the full rule. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] loop:edgeuseCount` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetLoopEdgeuseCountAttr() const;

    /// See GetLoopEdgeuseCountAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateLoopEdgeuseCountAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // LOOPVERTEXINDEX 
    // --------------------------------------------------------------------- //
    /// Loop_ii's vertex index when loop:edgeuseCount[ii] == 0, else ignored.
    /// loop:vertexIndex is required because vertex can be shared with EdgeVertices and wireEdgeVertices.
    /// A loop with loop:edgeuseCount[ii] == 0 is a degenerate VERTEX-LOOP: a loop consisting of the single vertex named here.
    /// This is the representation for a surface singularity (a cone apex, a sphere pole, a fillet corner) -- such a point is
    /// authored as a vertex-loop (Rule 428), NEVER as a zero-length degenerate edge (which is forbidden; see edge:curveType, Rule 381).
    /// size() = number of Loops.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] loop:vertexIndex` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetLoopVertexIndexAttr() const;

    /// See GetLoopVertexIndexAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateLoopVertexIndexAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // EDGEUSEEDGEINDEX 
    // --------------------------------------------------------------------- //
    /// Edgeuse_ii's edge index into edge arrays. size() = Number of one-sided edge_to_face connections. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] edgeuse:edgeIndex` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetEdgeuseEdgeIndexAttr() const;

    /// See GetEdgeuseEdgeIndexAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateEdgeuseEdgeIndexAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // EDGEUSEORIENTATIONTYPE 
    // --------------------------------------------------------------------- //
    /// same     = edgeuse's UVTrimCurve runs in the same direction as the edge's curve and
    /// represents the owning edge's binormal side connecting to a face..
    /// opposite = edgeuse's UVTrimCurve runs in the opposite direction and
    /// represents the edge's other side connecting to a face.
    /// size() = Number of one-sided edge_to_face connections.
    /// 
    /// Loop winding convention. edgeuse:orientationType together with the stored edgeuse order
    /// and the owning faceuse:orientationType fixes each loop's winding; no separate winding
    /// attribute exists or is needed. Walk a loop's edgeuses in stored order, applying each
    /// edgeuse's orientationType to get the direction its UVTrimCurve is traced. Seen from the
    /// side of the surface designated by the owning faceuse (the surface-normal side for a
    /// 'same' faceuse, the opposite side for an 'opposite' faceuse), the face's material lies to
    /// the LEFT of that traversal. The geometric consequence in UV parameter space, viewed from
    /// that designated side: the outer loop (the first loop of the face; see face:loopCount)
    /// winds counter-clockwise (CCW) and every inner (hole) loop winds clockwise (CW). For a
    /// face's void-region faceuse the surface is seen from the opposite side, so the winding
    /// mirrors (outer CW / inner CCW as seen from the void side); the "material on the left"
    /// rule still holds because the viewing side flipped.
    /// 
    /// Producers MUST author edgeuse order and orientationType so this holds. Consumers MAY rely
    /// on it and need not re-orient loops to assemble holes.
    /// 
    /// Why this is stated. NVIDIA's SMLib OCCT/PRC-to-USD converters (OCCT_BREP_IMPORT,
    /// BREP_PRC_SM) and OCCT itself key hole assembly off explicit orientation flags
    /// (OCCT Forward/Reversed map to this orientationType), never off geometric winding, and they
    /// expect outer-CCW / inner-CW ("material on the left"), consistent with the STEP norm. SMLib
    /// as a consumer drops holed faces when inner loops are authored CCW. Fixing the winding as
    /// the deterministic consequence of orientationType lets any consumer assemble holes without
    /// re-orienting loops or detecting the outer loop geometrically. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] edgeuse:orientationType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | same, opposite |
    USDSOLID_API
    UsdAttribute GetEdgeuseOrientationTypeAttr() const;

    /// See GetEdgeuseOrientationTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateEdgeuseOrientationTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // EDGEUSENEXTRADIALEUINDEX 
    // --------------------------------------------------------------------- //
    /// index of the nextRadialEdgeuse in a right-hand-rule traversal around the edgeuse's edge. size() = Number of one-sided edge_to_face connections.
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform uint[] edgeuse:nextRadialEUIndex` |
    /// | C++ Type | VtArray<unsigned int> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->UIntArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetEdgeuseNextRadialEUIndexAttr() const;

    /// See GetEdgeuseNextRadialEUIndexAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateEdgeuseNextRadialEUIndexAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // EDGEUSETHISRADIALENTRYTYPE 
    // --------------------------------------------------------------------- //
    /// - Each BrepArray edgeuse represents a {TopEdgeuse BotEdgeuse} mated pair connecting one side of an edge to the top and bottom sides of a face.
    /// - Edges bounding faces mostly connect to the face on just one side of the edge and are represented by just one BrepArray edgeuse, but
    /// seam edges that connect closed surfaces and strut edges representing cracks in a face connect to the same face twice and have two BrepArray edgeuses.
    /// - A right-hand-rule traversal around this edgeuse's edge, orders the edge-face connetions into a series of {face entry side, face exit side} pairs
    /// represented as a set of {entryEdgeuse, exitEdgeuse} pairs as: 
    /// RadialEdgeList = { edgeuse[1st]:{entryTopOrBotEdgeuse, exitBotOrTopEdgeuse}, ..., edgeuse[nth]:{entryTopOrBotEdgeuse, exitBotOrTopEdgeuse} } 
    /// - topEntry    = The radial edge traversal enters this edgeuse's face from the top and exits from the bottom as:{ edgeuse[this]:{entryTopEdgeuse, exitBotEdgeuse} }.
    /// bottomEntry = The radial edge traversal enters and exits this edgeuse's face in the opposite direction as:{ edgeuse[this]:{entryBotEdgeuse, exitTopEdgeuse} }.
    /// - size() = Number of one-sided edge_to_face connections. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] edgeuse:thisRadialEntryType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | topEntry, bottomEntry |
    USDSOLID_API
    UsdAttribute GetEdgeuseThisRadialEntryTypeAttr() const;

    /// See GetEdgeuseThisRadialEntryTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateEdgeuseThisRadialEntryTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // EDGECURVETYPE 
    // --------------------------------------------------------------------- //
    /// BrepCurve3d APIs define the 3d curve geometry for each edge.
    /// BrepCurve3dNurbAPI      = shape for edge_ii is a 3d NURB function (see BrepCurve3dNurbAPI).
    /// BrepCurve3dLineAPI      = shape for edge_ii is an analytic 3d line (see BrepCurve3dLineAPI).
    /// BrepCurve3dCircleAPI    = shape for edge_ii is an analytic 3d circle (see BrepCurve3dCircleAPI).
    /// BrepCurve3dEllipseAPI   = shape for edge_ii is an analytic 3d ellipse (see BrepCurve3dEllipseAPI).
    /// An edge must NOT be degenerate: its 3d curve must have non-zero length and its control vertices must not all be equal,
    /// measured against brep:intersectTol3d (Rule 381). A surface singularity (a cone apex, a sphere pole, a fillet corner)
    /// is NEVER represented as a zero-length edge; it is represented as a degenerate VERTEX-LOOP -- a loop consisting of a single
    /// vertex (Rule 428; see loop:vertexIndex) -- or as a converging vertex where real edges meet. Interop: the native validator
    /// (BA.230) now enforces no-degenerate-edges; the singularity-representation convention was previously implicit.
    /// size() = Number of edges. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] edge:curveType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | BrepCurve3dNurbAPI, BrepCurve3dLineAPI, BrepCurve3dCircleAPI, BrepCurve3dEllipseAPI |
    USDSOLID_API
    UsdAttribute GetEdgeCurveTypeAttr() const;

    /// See GetEdgeCurveTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateEdgeCurveTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // EDGERANGE 
    // --------------------------------------------------------------------- //
    /// Edge_ii's domain interval bounds {paramMin, paramMax}. size() = 2 * number of Edges. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] edge:range` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetEdgeRangeAttr() const;

    /// See GetEdgeRangeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateEdgeRangeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // EDGEVERTEXINDICES 
    // --------------------------------------------------------------------- //
    /// Edge_ii's vertexIndices = {startVertexIndex, endVertexIndex}.
    /// where Vertex_startVertexIndex:position = Edge_ii:Curve(Edge:Range(0)).
    /// Vertex_endVertexIndex:position   = Edge_ii:Curve(Edge:Range(1)).
    /// The pair is ordered by the 3d curve's PARAMETRIC direction, NOT by topological edge orientation: vertexIndices[0]
    /// is the vertex at the curve's parametric START (Edge:Range(0)) and vertexIndices[1] is the vertex at its END
    /// (Edge:Range(1)); the curve runs start -> end. Producers must swap the pair for edges whose topological orientation
    /// is reversed relative to the curve, so that the authored endpoints always agree with the curve's evaluated start/end.
    /// (Rule 434.) Interop: the native validator (BA.240) and SMLib require the authored endpoints to match the 3d curve's
    /// evaluated start/end within brep:intersectTol3d; ordering by topological edge orientation instead corrupts curved-edge
    /// reconstruction (an observed SMLib round-trip failure).
    /// When a Brep has edges, edge:vertexIndices are required because vertices can be shared with loopVertices and wireEdgeVertices.
    /// (note: edge:vertexIndices is defined as int2 because uint2 is not a USD value type; both components are conceptually uint and must be non-negative.)
    /// size() = number of Edges. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int2[] edge:vertexIndices` |
    /// | C++ Type | VtArray<GfVec2i> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int2Array |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetEdgeVertexIndicesAttr() const;

    /// See GetEdgeVertexIndicesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateEdgeVertexIndicesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // WIREEDGECURVETYPE 
    // --------------------------------------------------------------------- //
    /// BrepCurve3d APIs define the 3d curve geometry for each wireEdge.
    /// BrepCurve3dNurbAPI      = shape for wireEdge_ii is a 3d NURB function (see BrepCurve3dNurbAPI).
    /// BrepCurve3dLineAPI      = shape for wireEdge_ii is an analytic 3d line (see BrepCurve3dLineAPI).
    /// BrepCurve3dCircleAPI    = shape for wireEdge_ii is an analytic 3d circle (see BrepCurve3dCircleAPI).
    /// BrepCurve3dEllipseAPI   = shape for wireEdge_ii is an analytic 3d ellipse (see BrepCurve3dEllipseAPI).
    /// size() = Number of wireEdges. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] wireEdge:curveType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | BrepCurve3dNurbAPI, BrepCurve3dLineAPI, BrepCurve3dCircleAPI, BrepCurve3dEllipseAPI |
    USDSOLID_API
    UsdAttribute GetWireEdgeCurveTypeAttr() const;

    /// See GetWireEdgeCurveTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateWireEdgeCurveTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // WIREEDGERANGE 
    // --------------------------------------------------------------------- //
    /// WireEdge_ii's domain interval bounds {paramMin, paramMax}. size() = 2 * number of WireEdges. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform double[] wireEdge:range` |
    /// | C++ Type | VtArray<double> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->DoubleArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetWireEdgeRangeAttr() const;

    /// See GetWireEdgeRangeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateWireEdgeRangeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // WIREEDGEVERTEXINDICES 
    // --------------------------------------------------------------------- //
    /// WireEdge_ii's vertexIndices = {startVertexIndex, EndVertexIndex}.
    /// where Vertex_startVertexIndex:position = Edge_ii:Curve(Edge:Range(0)).
    /// Vertex_EndVertexIndex:position   = Edge_ii:Curve(Edge:Range(1)).
    /// As with edge:vertexIndices, the pair is ordered by the 3d curve's PARAMETRIC direction, NOT by topological orientation:
    /// vertexIndices[0] is the vertex at the curve's parametric START and vertexIndices[1] the vertex at its END (the curve runs
    /// start -> end); producers must swap the pair for reversed wireEdges so the authored endpoints agree with the curve's
    /// evaluated start/end within brep:intersectTol3d. (Rule 434; see edge:vertexIndices for the interop rationale.)
    /// When a Brep has wireEdges, wireEdge:vertexIndices are required because vertices can be shared with loopVertices and edgeVertices (a Brep with no wireEdges authors no wireEdge:* arrays).
    /// (note: wireEdge:vertexIndices is defined as int2 because uint2 is not a USD value type; both components are conceptually uint and must be non-negative.)
    /// size() = number of Edges. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform int2[] wireEdge:vertexIndices` |
    /// | C++ Type | VtArray<GfVec2i> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->Int2Array |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    USDSOLID_API
    UsdAttribute GetWireEdgeVertexIndicesAttr() const;

    /// See GetWireEdgeVertexIndicesAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateWireEdgeVertexIndicesAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // --------------------------------------------------------------------- //
    // VERTEXPOINTTYPE 
    // --------------------------------------------------------------------- //
    /// BrepPointAPI = shape for vertex_ii is a point in the pointType array. size() = number of Vertices. 
    ///
    /// | ||
    /// | -- | -- |
    /// | Declaration | `uniform token[] vertex:pointType` |
    /// | C++ Type | VtArray<TfToken> |
    /// | \ref Usd_Datatypes "Usd Type" | SdfValueTypeNames->TokenArray |
    /// | \ref SdfVariability "Variability" | SdfVariabilityUniform |
    /// | \ref UsdSolidTokens "Allowed Values" | BrepPointAPI |
    USDSOLID_API
    UsdAttribute GetVertexPointTypeAttr() const;

    /// See GetVertexPointTypeAttr(), and also 
    /// \ref Usd_Create_Or_Get_Property for when to use Get vs Create.
    /// If specified, author \p defaultValue as the attribute's default,
    /// sparsely (when it makes sense to do so) if \p writeSparsely is \c true -
    /// the default for \p writeSparsely is \c false.
    USDSOLID_API
    UsdAttribute CreateVertexPointTypeAttr(VtValue const &defaultValue = VtValue(), bool writeSparsely=false) const;

public:
    // ===================================================================== //
    // Feel free to add custom code below this line, it will be preserved by 
    // the code generator. 
    //
    // Just remember to: 
    //  - Close the class declaration with }; 
    //  - Close the namespace with PXR_NAMESPACE_CLOSE_SCOPE
    //  - Close the include guard with #endif
    // ===================================================================== //
    // --(BEGIN CUSTOM CODE)--
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
