# usdSolidTessellator

`usdsolidtessellate`, a command-line tool that reads a `BrepArray` prim from a
USD stage, tessellates it with OpenCASCADE Technology (OCCT), and writes the
result as `UsdGeomMesh` prims.

The same tessellation is available inside Hydra without any conversion step: the
`hdOcct` plugin renders `BrepArray` prims directly in any Hydra renderer. This
document covers both, because they share a build and an OCCT dependency.

## Layout

The tool is one file; the library it calls lives with the plugin.

```
extras/usd/usdSolidTessellator/
├── CMakeLists.txt   # builds the usdsolidtessellate executable
└── meshExport.cpp   # argument parsing; calls into hdOcct

pxr/imaging/plugin/hdOcct/
├── brepBuilder.cpp/h            # BrepArray attributes -> OCCT TopoDS_Shape
├── tessellator.cpp/h            # TopoDS_Shape -> triangles and normals
├── meshExporter.cpp/h           # triangles -> UsdGeomMesh
├── meshExportC.cpp              # C entry points used by the CLI
├── brepArrayAdapter.cpp/h       # UsdImaging adapter for the BrepArray prim type
├── hydraGenerativePlugin.cpp/h  # hdGp procedural emitting child mesh prims
├── plugInfo.json                # plugin registration
└── testenv/                     # 31 fixtures covering the schema surface
```

## How the Hydra path works

Tessellation happens in the scene index rather than in a renderer, so it applies
to every Hydra renderer and needs no render delegate.

`plugInfo.json` registers two things:

- `UsdSolidBrepArrayAdapter`, a `UsdImagingInstanceablePrimAdapter` bound to the
  `BrepArray` prim type. It has `UsdPrim` access, so it does the work: reads the
  schema attributes, builds an OCCT shape, tessellates, caches the result as a
  `usdSolidTessellatorData` data source, and reports the prim as a
  `generativeProcedural`.
- `UsdSolidTessellationProceduralPlugin`, an `HdGpGenerativeProceduralPlugin`. It
  reads that cached data and emits one child mesh prim per body.

The `displayName` in `plugInfo.json` is `usdSolidTessellation`, which is the
value the hdGp resolving scene index matches against to find the procedural.

## Build

OCCT 7.5 or newer is required, as shared libraries. Static OCCT is rejected at
configure time: the plugin links OCCT dynamically for LGPL-2.1 compliance, as
documented in `pxr/imaging/plugin/hdOcct/NOTICE.md`.

On Debian and Ubuntu:

```bash
sudo apt install libocct-foundation-dev libocct-modeling-data-dev \
                 libocct-modeling-algorithms-dev libtbb-dev
```

Then build OpenUSD with the plugin enabled:

```bash
python build_scripts/build_usd.py --onetbb --usd-imaging --usdview --python \
    --build-args USD,"-DPXR_BUILD_OCCT_PLUGIN=ON" <install-dir>
```

`--onetbb` is required. Distribution OCCT packages are built against system
oneTBB, and their exported CMake config references it directly. Without the flag
`build_usd.py` builds TBB 2020.3.1 instead, which both fails to link against
OCCT's TBB reference and would put two incompatible TBB major versions in one
process.

`libtbb-dev` is needed for the same reason: OCCT's config references
`libtbb.so`, the development symlink, which the runtime package does not
provide.

### Windows

Validated with Visual Studio 2026 / MSVC and vcpkg OCCT 8.0.0.

```
vcpkg install opencascade:x64-windows
python build_scripts\build_usd.py --onetbb ^
  --build-args USD,"-DPXR_BUILD_OCCT_PLUGIN=ON -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows -DVCPKG_SET_CHARSET_FLAG=OFF" ^
  <install-dir>
```

- The vcpkg toolchain file is required for OCCT discovery, because vcpkg's
  OpenCASCADE config calls `find_dependency(freetype)`.
- `VCPKG_SET_CHARSET_FLAG=OFF` is required; the toolchain's UNICODE defines
  break USD core on MSVC.
- Configure into a fresh build tree. CMake ignores a toolchain file added to an
  existing one.

## Run

Set the environment before running anything:

```bash
export PYTHONPATH=<install-dir>/lib/python<ver>/site-packages
export LD_LIBRARY_PATH=<install-dir>/lib:<install-dir>/plugin/usd
export PATH=<install-dir>/bin:$PATH
export HDGP_INCLUDE_DEFAULT_RESOLVER=1
```

`HDGP_INCLUDE_DEFAULT_RESOLVER=1` is required for the Hydra path. Without it the
hdGp resolving scene index is not added to the chain, so `BrepArray` prims
produce no geometry and the viewport is empty with no error message.

`<install-dir>/plugin/usd` must be on `LD_LIBRARY_PATH`, or `PATH` on Windows.
The CLI links against `hdOcct`, which is installed there rather than in `lib`.

### Viewing

```bash
usdview scene.usda
```

Fixtures under `pxr/imaging/plugin/hdOcct/testenv/testUsdSolidTessellation/fixtures/`
contain no camera or lights. To frame one, reference it from a stage that
supplies a camera, then rely on usdview's default camera light.

### Converting

```bash
usdsolidtessellate <input.usd> <output.usd> [primPath] \
    [--linear-deflection <f>] [--angular-deflection <rad>] [--relative-deflection]
```

- `--linear-deflection` — maximum chord distance between the tessellation and
  the true surface. Default 0.1; 0.02 gives a noticeably finer mesh. With
  `--relative-deflection` it is instead a fraction of the bounding-box diagonal.
- `--angular-deflection` — maximum angle in radians between adjacent facet
  normals. Default 0.5; 0.1 smooths curved silhouettes at close zoom.

`primPath` defaults to the stage's default prim. Fixtures nest `BrepArray` prims
at varying depths, so pass the path explicitly when converting them.

## Known limitations

- **Multi-loop faces are under-tessellated.** Faces with holes mesh a fraction
  of their true area when the source authors no `curveUv` pcurves, because the
  UV wire projected from 3D edge curves is malformed on BSpline surfaces. Tracked
  as an expected-failure test. The fix is the schema's authored `curveUv`
  trimming path.
- **Some faces render unlit.** Face winding is derived from
  `faceuse:orientationType`, and the current logic gets it wrong for faces whose
  `face.Orientation()` and faceuse direction disagree, leaving their normals
  inverted. Correct behaviour is specified by the `edgeuse:orientationType`
  documentation in the `UsdSolid` schema: outer loops wind counter-clockwise and
  inner loops clockwise, seen from the owning faceuse's designated side, with
  material to the left.
- **Seams are not welded.** Faces are tessellated as an unsewn compound, so
  tessellation seams at curved-face junctions remain visible.
- **Materials are not forwarded.** Meshes carry `displayColor` only. `GeomSubset`
  bindings on the source `BrepArray` do not reach the procedural's child prims.
