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

Validator-clean (all 11 prims): the converter now authors the completion
families the native `usdSolidValidators` and the SMLib USD reader require, so
the whole file passes the UsdSolidBrep validator suite with zero flags.
Completions applied converter-side (in the fixed base converter's `emit()`):

- `brep:intersectTol3d = [1e-06]` per Brep. Without it the validators fell back
  to a 1e-9 tolerance (too tight for real-CAD vertices, which meet only to
  ~1e-6), firing `EdgeCurveVertexMismatch`; and `InconsistentBrepArraySizes`
  because its size (0) didn't match `brep:regionCount`.
- `face:trimType = ["general", …]`, `loop:vertexIndex = [0, …]`,
  `vertex:pointType = ["BrepPointAPI", …]`, empty `wireEdge:*` arrays — the
  required families whose absence drove `AttributeNotAuthored`,
  `MissingBrepAttributes`, and the `Inconsistent{Face,Loop}ArraySizes` size
  mismatches. `vertex:pointType` also sizes the valid vertex-index range the
  References validator checks `edge:vertexIndices` against, so its absence had
  made every edge index "out of range" (`EdgeVertexIndexOutOfRange`).
- Full-precision (`repr`) pi/2pi in `edge:range` and `face:range`. The old `%g`
  formatting truncated 2π→`6.28319` (> 2π+1e-6) and π/2→`1.5708` (> π/2+1e-6),
  which tripped the analytic domain-span checks `SurfaceDomainSpanExceeded` and
  `SphereVDomainOutOfBounds`.
- `vector3d` role on analytic axis/direction attributes (axis, refDirection,
  line:direction); positions (center/origin/controlVertices) stay `point3d`.

None of the flags were converter indexing bugs — the per-Brep slice/emit
indices were already correct; every flag was a downstream consequence of a
missing family, the missing tolerance, or the truncated domain limits.

Known residuals (tracked, not asserted by tests): the 6 full-period
torus/sphere faces render via the parametric face:range fallback by design —
they carry a full-2π seam rather than a wire, so there is no 3D-edge loop to
trim against, and the parametric path is the correct route for them (not a
defect; see the `test_FullPeriodTorus`/`test_FullPeriodSphereBand` cases in the
52-test suite). Per-face tessellation cracks remain unless the env-gated
`HDOCCT_SEW=1` sewing path is enabled (which also guarantees outward
orientation per solid); sewing stays env-gated because it welds vertices and so
changes fixture vertex counts, which the baseline suite pins. The two most
complex parts (handle, lock body) keep a small geometric boundary-edge fraction
from per-edge-minted vertices when unsewed — the committed baseline is 0.127%
average boundary edges plain / 0.091% with sew, pre-existing and unrelated to
these completions (the completions lowered it from 3–4.5% and removed the
non-manifold artifacts).

Provenance: converted by the reference STEP reader
(`step_to_usdsolid.py`, PR #58 references) extended with assembly-graph
resolution; the 1.7 MB source STEP is not committed (available on request).
Not wired into the test suite yet — showcase/regression corpus only; may be
removed if unwanted upstream.
