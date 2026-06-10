# USD Solid Tessellator Plugin

**Location:** `extras/usd/usdSolidTessellator`

A tessellation plugin for OpenUSD that converts **UsdSolid BrepArray** geometry
(as defined in [USD Proposal #109](https://github.com/PixarAnimationStudios/OpenUSD-proposals/pull/109))
into standard `UsdGeomMesh` triangle meshes using **OpenCascade (OCCT)** as the
geometry kernel.

## Purpose

USD Proposal #109 introduces `UsdSolidBrepArray` — a Boundary Representation
(Brep) schema built on the Radial Edge Data Model. While the schema stores
exact NURBS-based solid geometry, most renderers, physics engines, and game
engines require tessellated triangle meshes. This plugin bridges that gap.

## Architecture

```
┌─────────────────────┐     ┌──────────────────┐     ┌────────────────────┐
│  UsdSolidBrepArray  │────▶│  UsdSolidBrep    │────▶│  UsdSolidTessella  │
│  (flat-packed USD   │     │  Builder         │     │  tor               │
│   attributes)       │     │  (→ TopoDS_Shape)│     │  (→ triangle mesh) │
└─────────────────────┘     └──────────────────┘     └────────────────────┘
                                                              │
                                                              ▼
                                                     ┌────────────────────┐
                                                     │  UsdSolidMesh      │
                                                     │  Exporter          │
                                                     │  (→ UsdGeomMesh)   │
                                                     └────────────────────┘
```

### Components

| Component | Description |
|-----------|-------------|
| `UsdSolidBrepBuilder` | Reads BrepArray attributes and reconstructs OCCT `TopoDS_Shape` topology (vertices → edges → wires → faces → shells → solids) |
| `UsdSolidTessellator` | Runs OCCT `BRepMesh_IncrementalMesh` on the reconstructed shape, extracts triangulation data |
| `UsdSolidMeshExporter` | Writes tessellation results as `UsdGeomMesh` prims with normals, UVs, extent, and `GeomSubset` per-face material groups |

## Schema Mapping (Proposal #109 → OCCT)

| USD BrepArray Concept | OCCT Equivalent |
|----------------------|-----------------|
| `brep:regionCount` | `TopoDS_Solid` / `TopoDS_CompSolid` |
| `region:shellCount` | `TopoDS_Shell` |
| `shell:faceuseCount` | Faces in shell |
| `face:surfaceType` → `BrepSurfaceNurbAPI` | `Geom_BSplineSurface` |
| `edge:curveType` → `BrepCurve3dNurbAPI` | `Geom_BSplineCurve` |
| `BrepCurveUvNurbAPI` (trim curves) | `Geom2d_BSplineCurve` |
| `BrepPointAPI:vertexPoint` | `gp_Pnt` / `TopoDS_Vertex` |
| `edgeuse:orientationType` | `TopAbs_Orientation` |
| `brep:intersectTol3d` | Sewing/healing tolerance |

## Dependencies

- **OpenUSD** (pxr) — USD core libraries
- **OpenCascade Technology (OCCT) 7.7+** — Geometry kernel
  - `TKernel`, `TKMath`, `TKG2d`, `TKG3d`, `TKGeomBase`
  - `TKBRep`, `TKGeomAlgo`, `TKTopAlgo`, `TKMesh`, `TKShHealing`

## Building

```bash
cd OpenUSD
mkdir build && cd build

cmake .. \
  -DCMAKE_PREFIX_PATH="/path/to/usd/install;/path/to/occt/install" \
  -DBUILD_TESTING=ON

cmake --build . --target usdSolidTessellator
ctest -R testUsdSolidTessellator
```

## Usage

### C++ API

```cpp
#include "extras/usd/usdSolidTessellator/lib/tessellator.h"

PXR_NAMESPACE_USING_DIRECTIVE

// Open stage with BrepArray prims
auto stage = UsdStage::Open("model.usda");
UsdPrim brep = stage->GetPrimAtPath(SdfPath("/Assembly/Part1"));

// Configure tessellation
UsdSolidTessellationParams params;
params.linearDeflection = 0.01;   // 10μm chord tolerance
params.angularDeflection = 0.5;   // ~28° max angle
params.computeNormals = true;
params.computeUVs = true;

// Tessellate
UsdSolidTessellator tess;
auto results = tess.Tessellate(brep, params);

// Or write directly to stage
auto meshPaths = tess.TessellateToStage(
    brep, SdfPath("/Meshes/Part1"), params);
```

### Tessellation Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `linearDeflection` | 0.1 | Max chord-to-surface distance |
| `angularDeflection` | 0.5 rad | Max angle between adjacent normals |
| `computeNormals` | true | Generate smooth vertex normals from surface |
| `computeUVs` | true | Generate parametric UV coordinates |
| `mergeBreps` | false | Combine all Breps into single mesh |
| `relativeDeflection` | false | Deflection as fraction of bbox diagonal |

## Supported Geometry (Current)

- ✅ NURBS Surfaces (`BrepSurfaceNurbAPI`)
- ✅ NURBS 3D Curves (`BrepCurve3dNurbAPI`)
- ✅ NURBS 2D Trim Curves (`BrepCurveUvNurbAPI`)
- ✅ Vertex Points (`BrepPointAPI`)

## Hydra Generative Procedural Plugin

The plugin registers a **Hydra Generative Procedural** so that BrepArray prims
are automatically tessellated for any Hydra renderer (Storm, HdPrman, etc.)
without requiring explicit stage-side conversion.

### How It Works

1. When Hydra encounters a prim with `hdGp:proceduralType = "usdSolidTessellation"`,
   the `UsdSolidTessellationProceduralPlugin` is activated.
2. It reads the BrepArray attributes from the scene index data source.
3. OCCT tessellates the Brep topology into triangle meshes.
4. Child mesh prims (`tessellated_mesh_0`, `tessellated_mesh_1`, ...) are
   injected into the Hydra scene index.
5. Any Hydra renderer picks them up as standard mesh prims.

### Authoring for Hydra

To mark a BrepArray prim for Hydra tessellation, author the procedural type:

```usda
def PrelimUsdSolidBrepArray "Part" (
    prepend apiSchemas = ["HydraGenerativeProceduralAPI"]
)
{
    token hdGp:proceduralType = "usdSolidTessellation"

    # Optional tessellation quality controls
    double primvars:tessellation:linearDeflection = 0.01
    double primvars:tessellation:angularDeflection = 0.5
    bool primvars:tessellation:computeNormals = true

    # ... BrepArray topology and geometry attributes ...
}
```

### Plugin Registration (plugInfo.json)

```json
{
    "Info": {
        "Types": {
            "UsdSolidTessellationProceduralPlugin": {
                "bases": ["HdGpGenerativeProceduralPlugin"],
                "proceduralType": "usdSolidTessellation"
            }
        }
    }
}
```

## Planned Extensions

- ⬜ Analytic surfaces (plane, cylinder, cone, sphere, torus)
- ⬜ Analytic curves (line, circle, ellipse)
- ⬜ Wire edge tessellation (as `UsdGeomBasisCurves`)
- ⬜ Multi-region solid decomposition
- ⬜ Async Hydra procedural (progressive tessellation)
- ⬜ LOD generation (multiple deflection levels)
- ⬜ Parallel per-face tessellation

## License

Apache-2.0
