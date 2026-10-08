# stepToUsdSolid — STEP → UsdSolid B-rep importer

## Overview

`stepToUsdSolid.py` converts a **STEP** file (ISO 10303-21/-42, the common CAD
interchange format) into a **UsdSolid** B-rep stage, authored through the
`pxr.UsdSolid` schema API. The schema is proposed at
https://github.com/PixarAnimationStudios/OpenUSD-proposals/pull/109.

It is pure Python plus USD — no CAD kernel is involved. That makes it a
self-contained way to produce B-rep test data for the schema and its validators:
a STEP file becomes a `BrepArray` that `usdSolidValidators` can check without any
tessellator in the loop.

```
python stepToUsdSolid.py part.step part.usdc
python stepToUsdSolid.py assembly.step assembly.usda --up-axis Z
```

Output format follows the extension: `.usda` for text, `.usdc` for a binary crate.

## Supported STEP constructs

The converter takes no modes or presets; it converts every construct below that
the file contains:

- **Analytic surfaces** — plane, cylinder, cone, sphere, torus — and **NURBS**
  surfaces, authored to the matching `BrepSurface*API`.
- **Analytic curves** — line, circle, ellipse — and **NURBS** curves, bare or
  wrapped in `SURFACE_CURVE` / `SEAM_CURVE`.
- **Swept surfaces** — linear extrusion and revolution — lowered to NURBS with
  exact rational-arc control points.
- **Void shells** (`BREP_WITH_VOIDS` / `ORIENTED_CLOSED_SHELL`) and **vertex loops**.
- **Sheet bodies** (`SHELL_BASED_SURFACE_MODEL`, as NAPA Designer exports a plate),
  beside any solids the file holds: the infinite void is the only region, each
  shell holds both faceuses of its faces, and a free edge's one edgeuse is its own
  radial next. The proposal exempts such shells from the closed-solid rule. A
  surface model's shells are sheets whether open or closed, and an
  `ORIENTED_OPEN_SHELL` is read through to its base shell.
- **Face UV windows** (`face:range`): an analytic face's is derived from its
  trimming edges; a NURBS face, including a lowered swept surface, takes its
  surface's knot domain.
- **Colors** — per-body and per-face `displayColor` read from STEP styled items.

The plane-angle unit (degrees vs radians), the length unit and the intersection
tolerance are read from the file. The length unit is the one the context of the
bodies' representations assigns, so a unit the file declares for something else
(a density's metre) does not count; an inch or other `CONVERSION_BASED_UNIT` is
read through its conversion factor. It becomes the stage's `metersPerUnit` unless
`--meters-per-unit` is given. The tolerance is the file's `UNCERTAINTY_MEASURE`
over 20, in the file's unit, with a bounding-box fallback; its floors are stated
in millimetres and scaled to that unit.

`brep:extent` bounds the geometry: the vertices, every NURBS control vertex, the
rational poles of each circle and ellipse edge, and the box of each sphere and
torus face. It is loose where a face trims a larger surface.

## Scope

An assembly comes through as one `Xform` per placement, with the transform composed
down the `NEXT_ASSEMBLY_USAGE_OCCURRENCE` chains and the part's solids as `BrepArray`
children. A placement maps the child's `ITEM_DEFINED_TRANSFORMATION` item onto the
parent's. Writers disagree on which of a `REPRESENTATION_RELATIONSHIP`'s two
representations is the parent's: Open CASCADE names the child's first, SolidWorks
the parent's. The importer takes the parent from the occurrence itself, through the
`SHAPE_DEFINITION_REPRESENTATION` that names each product definition's
representation. Geometry stays in the part's own coordinate system, each placement
authors its own copy of the part's `BrepArray`s, and an identity placement authors
no transform. A part's bodies are every body its representations hold, pooled
over the representations a plain `SHAPE_REPRESENTATION_RELATIONSHIP` joins; an
occurrence is placed once. A body that no placement reaches, like every body of a
file with no assembly structure, becomes a top-level prim in its own coordinates.
When a part's bodies differ in colour, each `BrepArray` takes its own.

A solid's prim is named after the STEP `PRODUCT` of the part it belongs to, so an
assembly's parts keep their names (`adapter_plate`, `jaw_left`). Names become
valid USD identifiers (ASCII letters, digits and underscores, no leading digit),
and a repeated name gets `_1`, `_2`. A part with several bodies keeps the bodies'
own names, since one product name cannot tell them apart; a body with no name
is `body_<i>`. Each such body records its part in `customData`
(`stepToUsdSolid:product`, the `PRODUCT` name as the file writes it), so a
consumer can gather a part's bodies again.

This is a **reference / sample importer**, in the spirit of the Gaussian-splat
`py3dgsPlyToUsd.py` sample under `extras/imaging/examples/hdParticleField`: enough to
exercise the schema end to end and to generate test assets, not a production STEP
importer.

### Optional features

Two of the features above are conveniences. Either can be dropped without touching
the B-rep path:

- **Color** (`resolve_colors`, ~80 lines) reads `STYLED_ITEM` chains to author
  `displayColor`. Color is not part of a boundary representation; it is here so
  converted assets are legible in a viewer.
- **Assembly placement** (`assembly_placements` and its helpers, ~250 lines) composes
  `NEXT_ASSEMBLY_USAGE_OCCURRENCE` transforms into `Xform` prims. Assembly structure is
  a USD composition concern, not a `UsdSolid` one.

Together they are about 330 lines. They are kept because they make the output usable
on production CAD assemblies. A reviewer who wants this sample smaller should take
these first, ahead of anything that reads geometry.

## Where STEP and UsdSolid disagree

STEP and UsdSolid do not accept the same B-reps, so a translator has to convert
between the two rule sets rather than copy topology across. Four differences show
up on almost any production CAD file.

**Sub-tolerance edges are removed.** UsdSolid rule 7.ii forbids an edge whose
curve fits inside a sphere of radius `brep:intersectTol3d`. A tolerant modeller
emits one wherever its own topology has an edge, including where the two ends
have already closed to within the file's declared tolerance -- an NX export of a
KUKA KR 640 carries 149. The importer welds the two vertices to their centroid,
drops the edge from its loops, and re-fits the adjacent line edges through the
moved vertex. Adjacent circles and NURBS curves keep their geometry, so an
endpoint that was already close to the tolerance limit can cross it after the
weld; re-solving their parameter ranges is healing proper and belongs in a
healer, not here.

**The outer loop is put first.** The proposal's rule 424 makes a face's first
loop its outer loop. STEP names that loop with `FACE_OUTER_BOUND`, which is
optional: the KUKA KR 640 writes each of its 2,729 multi-loop faces with
`FACE_BOUND` only and lists a hole first on 1,050 of them. Where no bound is named
outer, the importer puts first the loop whose boundary spans the largest box. A
bound whose orientation flag is `.F.` runs its edge loop backwards, as STEP
defines it; the KUKA has 252.

**A reversed face is turned around.** STEP winds a face's loops about the face
normal, and an `ADVANCED_FACE` whose `same_sense` is `.F.` has a face normal
opposite its surface's. UsdSolid winds loops as seen from the surface-normal side
and names the outward side with the faceuse. On a reversed face the importer
reverses the loops and makes the outward faceuse `opposite`; each edgeuse enters
the radial order from the top when it runs along its edge curve and from the
bottom otherwise, as SMLib's own exporter writes them. The KUKA KR 640 has 4,374
reversed faces.

**Seam edges are synthesised for cylinders and cones.** UsdSolid rule 5.iv
requires a face to have a single outer loop carrying a seam edge; STEP does not,
and exports a full revolution as a face that closes on itself with its boundary
split across two loops. Where one of those loops is a single closed rim, the
importer mints the surface's own ruling from that rim's vertex to where the
other loop's chain begins and joins the two into one loop -- round the rim, up
the seam, round the other loop, and back down it. The other loop is traversed
from its own first vertex, so nothing is rotated and no edge is split.

A ruling only exists where both ends sit at the same angle about the axis.
Where they do not, the rim's vertex is slid around its own circle to meet the
other, which re-parameterizes the circle without changing its shape. That is
available only while the vertex belongs to no other edge and no earlier seam
ends on it, since two coaxial faces can share one rim.

Spheres and tori are left alone. A sphere face here always contains a pole, so
its seam is a meridian running to a vertex the file does not yet carry; a torus
needs its seam at v = 0 on the tube, the same fixed parameter origin behind the
open question on BA.765. On the KUKA export this authors 1,114 seams and takes
BA.761 from 2,041 findings to 927, of which 812 are tori and 52 are spheres.

## Requirements

- A USD build with the **UsdSolid** schema (this repository), so `from pxr import
  UsdSolid` resolves and `BrepArray` is a registered type.
- No third-party Python packages: the STEP reader uses only the standard library.

## Checking the output

```
python stepToUsdSolid.py part.step part.usdc   # STEP -> UsdSolid B-rep
usdchecker part.usdc                           # runs the usdSolid validators
```

`testenv/testStepToUsdSolid.py` does this end to end on a box it generates
itself, and asserts that `usdSolidValidators` reports nothing. A box exercises
neither difference above; a production CAD file will report findings, and the
seam gap is the reason.

A validator establishes that the B-rep is well formed. Establishing that the shape
is correct needs a tessellator, which this sample does not include.
