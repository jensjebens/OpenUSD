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
- **Analytic curves** — line, circle, ellipse — and **NURBS** curves.
- **Swept surfaces** — linear extrusion and revolution — lowered to NURBS with
  exact rational-arc control points.
- **Void shells** (`BREP_WITH_VOIDS` / `ORIENTED_CLOSED_SHELL`) and **vertex loops**.
- **Face UV windows** (`face:range`) derived from each face's trimming edges.
- **Colors** — per-body and per-face `displayColor` read from STEP styled items.

The plane-angle unit (degrees vs radians) and the intersection tolerance are read
from the file (from its `UNCERTAINTY_MEASURE`, with a bounding-box fallback).

## Scope

An assembly comes through as one `Xform` per placement, with the transform composed
down the `NEXT_ASSEMBLY_USAGE_OCCURRENCE` chains and the part's solids as `BrepArray`
children. Geometry stays in the part's own coordinate system, so a part placed several
times has one set of authored surfaces rather than one per placement. A file with no
assembly structure maps each solid to a top-level prim in world coordinates.

This is a **reference / sample importer**, in the spirit of the Gaussian-splat
`py3dgsPlyToUsd.py` sample under `extras/imaging/examples/hdParticleField`: enough to
exercise the schema end to end and to generate test assets, not a production STEP
exporter.

### Optional features

Two of the features above are conveniences. Either can be dropped without touching
the B-rep path:

- **Color** (`resolve_colors`, ~80 lines) reads `STYLED_ITEM` chains to author
  `displayColor`. Color is not part of a boundary representation; it is here so
  converted assets are legible in a viewer.
- **Assembly placement** (`assembly_placements` and its helpers, ~190 lines) composes
  `NEXT_ASSEMBLY_USAGE_OCCURRENCE` transforms into `Xform` prims. Assembly structure is
  a USD composition concern, not a `UsdSolid` one.

Together they are about 270 lines. They are kept because they make the output usable
on production CAD assemblies. A reviewer who wants this sample smaller should take
these first, ahead of anything that reads geometry.

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
itself, and asserts that `usdSolidValidators` reports nothing.

A validator establishes that the B-rep is well formed. Establishing that the shape
is correct needs a tessellator, which this sample does not include.
