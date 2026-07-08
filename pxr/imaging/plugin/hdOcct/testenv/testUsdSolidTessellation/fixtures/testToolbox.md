# testToolbox.usda — real-CAD assembly fixture

A production SolidWorks toolbox (AP214 STEP assembly export) converted to
UsdSolid `BrepArray` prims: **11 placed part instances of 6 unique parts**
(base shell, lid shell, 2× lock, 2× lock pin, 3× lid pin, handle) under
per-occurrence `Xform`s whose transforms come from the STEP assembly graph
(`NEXT_ASSEMBLY_USAGE_OCCURRENCE` → `ITEM_DEFINED_TRANSFORMATION` chains,
sub-assembly composition included). 581 analytic faces total (337 planes,
204 cylinders, 21 tori, 10 spheres, 9 NURBS) bounded by 1058 lines, 407
circles, 70 NURBS curves.

Why it matters as a fixture:

- The source STEP contains **zero pcurves** (`PCURVE`/`SURFACE_CURVE` entity
  count: 0) — typical of production CAD exports. Every trim here exercises the
  derive-from-3D-edges path that proposal #109's optional `curveUv` implies
  ("the edge curve defines the model truth").
- Real-world tolerances (axes like `(-1, 2.5e-32, 2.1e-16)`, radius
  `2.4999…956`) — the data that motivated the shared-vertex wire assembly and
  hole-orientation fixes in `brepBuilder.cpp`.
- Per-part `primvars:displayColor` mapped from the STEP `STYLED_ITEM` styling;
  per-prim `extent`/`brep:extent` for viewport framing.

Known residuals (tracked, not asserted by tests): 6 full-period torus/sphere
faces render via the parametric fallback; per-face tessellation cracks remain
unless the env-gated `HDOCCT_SEW=1` sewing path is enabled (which also
guarantees outward orientation per solid).

Provenance: converted by the reference STEP reader
(`step_to_usdsolid.py`, PR #58 references) extended with assembly-graph
resolution; the 1.7 MB source STEP is not committed (available on request).
Not wired into the test suite yet — showcase/regression corpus only; may be
removed if unwanted upstream.
