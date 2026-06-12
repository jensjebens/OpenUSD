# usdSolidTessellator — Hydra Generative Procedural for BrepArray

## Overview

This plugin tessellates **BrepArray** prims (from the UsdSolid schema / Proposal #109)
into renderable triangle meshes at runtime via OpenUSD's **hdGp** generative procedural
architecture. No pre-baked meshes needed — drop a `.usd` file containing BrepArray prims
into any Hydra viewport and they render as tessellated geometry.

**Status:** Phase 1 complete. Live Hydra tessellation works. Known issue: hub ring
faces have mixed winding (faceuse orientation logic needs per-body shell analysis).

---

## Architecture

```
BrepArray prim (USD stage)
    │
    ├─ BrepArrayAdapter (usdImaging adapter)
    │   ├─ Reads BrepArray attributes
    │   ├─ Calls BrepBuilder → OCCT TopoDS_Shape
    │   ├─ Calls Tessellator → triangles + normals
    │   ├─ Injects mesh data as "usdSolidTessellatorData" data source
    │   └─ Reports prim type as "generativeProcedural"
    │
    └─ UsdSolidTessellationProcedural (hdGp plugin)
        ├─ Reads pre-computed mesh data from adapter's data source
        ├─ Creates child mesh prims (one per body)
        └─ Sets displayColor, topology, normals, points
```

### Key Design Decisions

1. **Adapter does the heavy lifting** — has access to UsdPrim (stage data).
   The procedural only maps pre-computed data to Hydra mesh schema.

2. **Schema registration via generatedSchema.usda** — required so
   `UsdImagingStageSceneIndex` recognizes `BrepArray` as a concrete type
   and calls our adapter.

3. **displayName in plugInfo.json = "usdSolidTessellation"** — the hdGp
   resolving scene index matches the `hdGp:proceduralType` primvar value
   against this string to find our procedural plugin.

4. **No material binding** — meshes get `displayColor` (bright grey) for
   Storm camera-light shading. Material pass-through is Phase 3 work.

5. **Untrimmed surface path** — when BrepCurveUvNurbAPI (pcurves) are absent,
   faces are built as untrimmed NURBS surfaces. Faces with multiple loops
   (trim boundaries we can't reconstruct) are skipped to avoid oversized
   rectangular artifacts.

---

## Building

### Standalone (out-of-tree, recommended for dev)

```bash
cd /home/horde/projects/usd-tessellation
mkdir -p build-plugin && cd build-plugin
cmake -DCMAKE_PREFIX_PATH=../usd-install \
      -DCMAKE_INSTALL_PREFIX=../usd-install \
      ../<path-to-OpenUSD>
cmake --build . --target usdSolidTessellator -j$(nproc)
cp extras/usd/usdSolidTessellator/usdSolidTessellator.so \
   ../usd-install/plugin/usd/usdSolidTessellator.so
```

### Dependencies

- OpenUSD 0.26.8+ (built from source with Python, imaging, hdGp)
- OpenCASCADE 7.5 (`apt install libocct-*-dev`)
- CMake 3.20+

---

## Running

### Environment Variables

```bash
export LD_LIBRARY_PATH=/home/horde/projects/usd-tessellation/usd-install/lib:$LD_LIBRARY_PATH
export PYTHONPATH=/home/horde/projects/usd-tessellation/usd-install/lib/python
export PXR_PLUGINPATH_NAME=/home/horde/projects/usd-tessellation/usd-install/plugin/usd/
export HDGP_INCLUDE_DEFAULT_RESOLVER=1
export DISPLAY=:99
export QT_QPA_PLATFORM=offscreen
```

### Render Command (usdrecord)

```bash
python3 /home/horde/projects/usd-tessellation/OpenUSD/pxr/usdImaging/bin/usdrecord/usdrecord.py \
  --renderer Storm \
  --imageWidth 1920 \
  --camera /World/Camera \
  scene.usda output.jpg
```

**Notes:**
- Do NOT use `--disableCameraLight` — scene distant lights don't work on
  llvmpipe/Xvfb software rendering. Camera headlamp is the only working
  light source in this environment.
- Use `.jpg` output to avoid transparent-background-appearing-as-white issue
  with `.png` on some viewers.
- `HDGP_INCLUDE_DEFAULT_RESOLVER=1` is required for the procedural to fire.

### Minimal Scene File

```usda
#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)

def Xform "World"
{
    def "Brep0" (
        references = @/path/to/BrepArray.usd@</World/Brep0>
    )
    {
    }

    def Camera "Camera"
    {
        float focalLength = 35
        float horizontalAperture = 36
        double3 xformOp:translate = (60, 30, 60)
        float3 xformOp:rotateXYZ = (-20, 45, 0)
        token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }
}
```

---

## How It Works — Data Flow

### 1. BrepBuilder (`brepBuilder.cpp`)

Reads BrepArray attributes from USD prim:
- `brep:surface:nurb:*` — NURBS surface control points, knots, orders
- `face:loopCount`, `loop:edgeuseCount` — topology
- `faceuse:orientationType` — normal direction ("same" = outward, "opposite" = flip)
- `brep:edge:nurb:*` — 3D edge curves (used in pcurve path)
- `region:*`, `shell:*` — body/shell structure

Constructs OCCT `TopoDS_Shape` per body:
- Builds `Geom_BSplineSurface` for each face
- **No-pcurve path:** `BRepBuilderAPI_MakeFace(surface, tol)` → untrimmed face
- **Pcurve path (TODO):** Full edge wires with `ShapeFix_Shape` pcurve projection
- Skips multi-loop faces (trim boundaries without pcurves → rectangular artifacts)
- Applies `face.Reverse()` for faces where faceuse says "opposite"
- Sews faces into solid via `BRepBuilderAPI_Sewing`

### 2. Tessellator (`tessellator.cpp`)

Runs `BRepMesh_IncrementalMesh` on each body shape:
- Default deflection: 0.01 (linear), 0.5 (angular)
- Extracts vertices, triangle indices, normals from `Poly_Triangulation`
- Normals computed from parametric surface `D1()` derivatives
- **Winding convention:** `!isReversed` → swap(n1,n3) and reverse normal
  (empirically correct for Storm's front-face convention)

### 3. Adapter (`brepArrayAdapter.cpp`)

- Registered via plugInfo.json: `primTypeName = "BrepArray"`
- `GetImagingSubprimType()` → returns `generativeProcedural`
- Pre-tessellates on first call, caches result as data source
- Injects `hdGp:proceduralType = "usdSolidTessellation"` primvar

### 4. Procedural (`hydraGenerativePlugin.cpp`)

- Registered via `displayName = "usdSolidTessellation"` in plugInfo.json
- `_Tessellate()` reads mesh data from adapter's data source
- `Update()` returns child prim map: `tessellated_mesh_0..N`
- `GetChildPrim()` builds Hydra data source per mesh:
  - MeshTopology (faceVertexCounts, faceVertexIndices)
  - Primvars: points (vertex), normals (vertex), displayColor (constant)

---

## Known Issues

1. **`_MarkRprimDirty` errors** — cosmetic. Legacy UsdImagingDelegate tries to
   dirty `/World/Brep0` which doesn't exist as an rprim. Non-fatal.

2. **Hub ring normals mixed** — body 0 faces 3-5 have "opposite" faceuse
   orientation. The current fix reverses those faces, but the winding swap
   logic (`!isReversed`) was calibrated for the blade bodies. Need per-face
   winding logic that considers both `face.Orientation()` AND faceuse direction.

3. **No pcurve trimming** — faces with multiple loops (e.g., circular end-caps)
   are skipped. Full trim support requires projecting 3D edges onto surface UV
   space (OCCT `ShapeFix_Shape` + `FixAddPCurveMode`). Attempted but produced
   broken topology — needs investigation.

4. **Scene lights don't work** — Storm on llvmpipe/Xvfb doesn't process
   DistantLight prims. Camera headlamp is the only working illumination.

5. **Material binding** — BrepArray prims in TurbineFan.usd have GeomSubsets
   with material bindings (`brushed_titanium`, `machined_steel_grey`), but
   these materials live at `/World/Materials/*` which is out-of-scope when
   referencing just the Brep0 prim. Need to either reference the full stage
   or forward material bindings to procedural children.

---

## Test Asset

`TurbineFan.usd` (652KB binary crate):
- 25 bodies (1 hub + 24 blades)
- 150 NURBS surfaces, 442 edges
- No UV trim curves (BrepCurveUvNurbAPI absent)
- 2 materials: `brushed_titanium`, `machined_steel_grey`
- Extent: x=[-6,12] y=[-35,35] z=[-35,35]

Current tessellation: 148 faces → ~1988 vertices (skipping 2 end-cap faces)
at deflection 0.01.

---

## File Map

```
extras/usd/usdSolidTessellator/
├── CMakeLists.txt              # Build rules (standalone out-of-tree)
├── brepArrayAdapter.cpp/h      # UsdImaging adapter: BrepArray → procedural
├── brepBuilder.cpp/h           # USD BrepArray → OCCT TopoDS_Shape
├── hydraGenerativePlugin.cpp/h # hdGp procedural: mesh data → Hydra prims
├── meshExporter.cpp/h          # Mesh → USD (for standalone CLI, Phase 2)
├── module.cpp                  # Python module stub (placeholder)
├── plugInfo.json               # Plugin registration metadata
├── tessellator.cpp/h           # OCCT shape → triangles + normals
└── resources/
    └── plugInfo.json           # Runtime-discoverable copy
```

---

## Next Steps

- **Phase 2:** Standalone CLI `usdsolidtessellate` (C++ binary)
- **Phase 3:** Material pass-through, GeomSubsets, per-body caching, thread safety
- **Phase 4:** Tests, docs, upstream PR to PixarAnimationStudios/OpenUSD

---

## Git Info

- Fork: `github.com/jensjebens/OpenUSD`
- Branch: `feature/usd-solid-tessellator`
- Remotes: `origin` = PixarAnimationStudios, `fork` = jensjebens
- Upstream: `412f38c66` (dev branch, 2026-06-09)

## Windows build (validated: VS 2026 / MSVC, vcpkg OCCT 8.0.0)

```
vcpkg install opencascade:x64-windows
python build_scripts\build_usd.py ^
  --build-args USD,"-DPXR_BUILD_OCCT_PLUGIN=ON -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_SET_CHARSET_FLAG=OFF" ^
  <install-dir>
```

Notes:
- The vcpkg toolchain file is required for OCCT discovery (vcpkg's
  OpenCASCADE config needs find_dependency(freetype)).
- `VCPKG_SET_CHARSET_FLAG=OFF` is required; the toolchain's UNICODE
  defines break USD core on MSVC.
- Configure in a fresh build tree (CMake ignores a toolchain file added
  to an existing one).

## Runtime environment

`<install>/plugin/usd` must be on `PATH` (Windows) / `LD_LIBRARY_PATH`
(Linux): the hdOcct library the CLI links against is installed there.

## Known limitations

- Multi-loop (hole-punctured) faces mesh a fraction of their true area
  when fixtures author no `curveUv` pcurves (the UV wire projected from
  3D edge curves is malformed on BSpline surfaces). Tracked as an
  expected-failure test; the fix is the schema's authored `curveUv`
  trimming path.
- Faces are tessellated as an unsewn compound; tessellation seams at
  curved-face junctions are not welded.
