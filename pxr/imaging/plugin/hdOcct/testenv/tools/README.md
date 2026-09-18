# Fixture generation tools

The tessellation test fixtures in `../testUsdSolidTessellation/fixtures/` are
**generated from source**, not hand-authored, so their geometry — and in
particular the 2D trim `curveUv` pcurves — is guaranteed consistent with each
face's NURBS surface parameterization. (Hand-authored trim curves are easy to
get subtly wrong: a pcurve in the wrong UV convention silently meshes a hole to
the wrong size.)

## `brepExport.cpp` — OCCT → UsdSolid BrepArray

Builds a parametric OCCT solid (box, filleted box, plane sheet — each with a
circular through-hole) and writes it as a flat-packed `BrepArray` (`.usda`),
extracting NURBS surfaces, 3D edge curves, and the authored trim pcurves via
`BRep_Tool::CurveOnSurface`. Every loop is emitted CCW in UV so the builder's
outer-bounds / inner-reverse trimming is always correct.

This is also the seed of the STEP→UsdSolid producer: the same extraction path
applies to any `TopoDS_Shape` (e.g. a STEP import), not just these primitives.

Build (links only OpenCASCADE — no USD dependency, it emits text):

```bash
OCC=/path/to/occt-install
g++ -std=c++17 -O2 brepExport.cpp \
  -I"$OCC/include/opencascade" -L"$OCC/lib" -Wl,-rpath,"$OCC/lib" \
  -lTKBRep -lTKMath -lTKernel -lTKG2d -lTKG3d -lTKGeomBase -lTKGeomAlgo \
  -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKFillet -lTKOffset -lTKShHealing \
  -o brepExport
```

Regenerate:

```bash
F=../testUsdSolidTessellation/fixtures
./brepExport plane    $F/testPlaneWithHole.usda
./brepExport cube     $F/testCubeWithHole.usda
./brepExport filleted $F/testFilletedCubeWithHole.usda
```

## `gen_depressed_plane.py` — analytic depressed-plane sheet

Emits `testDepressedPlane.usda`: a 20×20 sheet with a radius-3 cylindrical
pocket of depth 5 (open top, cylindrical wall, flat bottom cap). Authored
directly (no OCCT) as a minimal multi-face trimmed-surface example.

```bash
python3 gen_depressed_plane.py ../testUsdSolidTessellation/fixtures/testDepressedPlane.usda
```

## Regression baselines

After regenerating, update `EXPECTED_VERT_COUNTS` in
`../testUsdSolidTessellation.py` if the OCCT meshing output changes. The
area/volume assertions in `TestTrimmedFaceArea` and `TestTessellationWindingOrder`
are geometry-based and OCCT-meshing-independent.
