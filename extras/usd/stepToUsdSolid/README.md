# stepToUsdSolid — STEP → UsdSolid B-rep importer

## Overview

`stepToUsdSolid.py` converts a **STEP** file (ISO 10303-21/-42, the common CAD
interchange format) into a **UsdSolid** B-rep stage. It reads the STEP part or
assembly and writes one `Xform` + `BrepArray` prim per solid, authored through the
`pxr.UsdSolid` schema API (Proposal #109).

It is pure Python plus USD — no CAD kernel (no OpenCASCADE, no SMLib) is involved.
That makes it a self-contained way to produce B-rep test data for the schema, the
validator, and the tessellator: a STEP file becomes a `BrepArray` you can feed
straight into `usdSolidTessellator` (this same PR) to render.

```
python stepToUsdSolid.py part.step part.usdc
python stepToUsdSolid.py assembly.step assembly.usda --up-axis Z
```

Output format follows the extension: `.usda` for text, `.usdc` for a binary crate.

## What it handles

The converter reads whatever the STEP file contains and does the right thing with
it — there are no modes or presets to choose. It covers:

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

Each STEP solid maps to a top-level prim in world coordinates (flat multi-body).
Assembly instancing — `NEXT_ASSEMBLY_USAGE_OCCURRENCE` placement transforms — is not
handled; a nested assembly comes through as its constituent solids in world space.

This is a **reference / sample importer**, in the spirit of the Gaussian-splat
`py3dgsPlyToUsd.py` sample: enough to exercise the schema end to end and to generate
test assets, not a production STEP exporter.

## Requirements

- A USD build with the **UsdSolid** schema (this repository), so `from pxr import
  UsdSolid` resolves and `BrepArray` is a registered type.
- No third-party Python packages: the STEP reader uses only the standard library.

## Round trip

```
python stepToUsdSolid.py part.step part.usdc   # STEP -> UsdSolid B-rep
usdsolidtessellate part.usdc part_mesh.usdc    # UsdSolid B-rep -> Mesh (usdSolidTessellator)
```
