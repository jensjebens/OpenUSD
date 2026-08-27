# testToolbox.usda — real-CAD assembly fixture

A production SolidWorks toolbox (AP214 STEP assembly export) converted to
UsdSolid `BrepArray` prims by `extras/usd/stepToUsdSolid`. Ten assembly
placements of six unique parts — base shell (two solids), lid shell, handle,
2x lock, 2x lock pin, 3x lid pin — under per-placement `Xform`s whose
transforms are composed down the `NEXT_ASSEMBLY_USAGE_OCCURRENCE` chains,
sub-assembly nesting included. Eleven `BrepArray` prims in all.

Geometry stays in each part's own coordinate system, where the STEP authored
it; only the placement moves. The three identical lid pins are three placements
of one part's surfaces, not three copies.

Placed totals: 631 faces (359 planes, 232 cylinders, 21 tori, 10 spheres, 9
NURBS) bounded by 1104 lines, 463 circles and 70 NURBS curves. Across the six
unique parts that is the STEP file's 581 `ADVANCED_FACE`s exactly.

Why it matters as a fixture:

- The source STEP contains **zero pcurves** (`PCURVE` / `SURFACE_CURVE` count:
  0) — typical of production CAD. Every trim exercises the derive-from-3D-edges
  path that proposal #109's optional `curveUv` implies.
- It contains **no seam edges** either: of its 235 faces on periodic surfaces,
  none repeats an `EDGE_CURVE` within a loop, and no `EDGE_CURVE` is used more
  than twice anywhere. This is the evidence that a full-period periodic face
  does not need a seam edgeuse, which is what `BA.761` treats as a heuristic
  signal rather than a requirement.
- Real-world tolerances (axes like `(-1, 2.5e-32, 2.1e-16)`, radius
  `2.4999...956`) — the data that motivated the shared-vertex wire assembly and
  hole-orientation fixes in `brepBuilder.cpp`.
- Per-part `primvars:displayColor` from the STEP `STYLED_ITEM` styling;
  per-prim `extent` / `brep:extent` for viewport framing.

## Validator state

Eleven findings, all on one part:

- `BA.631` x9 (Error) — faces whose U window genuinely crosses `u = 2*pi`.
  `BA.765` requires a partial-period domain to stay inside the primary period,
  so these want splitting at the seam, as `testFullPeriodTorusSeamSplit` and
  `testFullPeriodSphereBandSeamSplit` demonstrate against their unsplit twins.
  Open.
- `BA.761` x2 (Warn) — the full-period seam-edgeuse heuristic. Advisory; see
  above.

All eleven prims tessellate through OCCT.

## Regenerating

    python extras/usd/stepToUsdSolid/stepToUsdSolid.py toolbox.step \
        testToolbox.usda --up-axis Y

The 1.7 MB source STEP is not committed (available on request).
