# Units API — Proof of Concept

A Python companion library for OpenUSD that provides unit-aware reading and authoring of attribute values. Built to validate the design ideas in the [Units & Scale proposal](https://github.com/jensjebens/OpenUSD-proposals/blob/jjebens/units-and-scale/proposals/units_and_scale/README.md) and the [MetricsAPI proposal (PR #45)](https://github.com/PixarAnimationStudios/OpenUSD-proposals/pull/45).

## The Problem

USD preserves authored numbers exactly through composition but doesn't reconcile units. A `translate = 100` means 100 millimeters in a CAD asset but 100 meters in a factory stage. Current mechanisms (`metersPerUnit` layer metadata, Metrics Assembler) handle transforms but miss derived quantities — density, gravity, camera attributes, light dimensions — producing silently wrong results.

## What This POC Demonstrates

Three complementary mechanisms, tested empirically against each other:

| Layer | What it answers | How |
|---|---|---|
| **MetricsAPI** | "What unit system is this subtree in?" | Prim-level `customData`, ancestor inheritance, layer metadata fallback |
| **Dimensional Registry** | "What are this attribute's dimensional exponents?" | Schema-level mapping: `physics:density → L⁻³·M¹` |
| **Per-Attribute Metadata** | "What about custom/extension attributes?" | Self-describing `customData` on individual attributes |

Plus two consumer APIs:

- **UnitsLens** — get/set any attribute value with unit conversion
- **MetricsAssembler** — non-destructive corrective `xformOp:scale` at reference boundaries

## Quick Start

```bash
# Requires: Python 3.10+, OpenUSD (pxr), numpy (optional, for performance)
cd extras/units_api
PYTHONPATH=src:tests python3 -m pytest tests/ -v
```

## Usage

### Declare units on an asset

```python
from units_api import MetricsAPI

# "Everything under /Bolt is in millimeters"
MetricsAPI.apply(bolt_prim, meters_per_unit=0.001, up_axis="Z")
```

### Read values in your preferred units

```python
from units_api import UnitsLens

# translate = (10, 0, 0) authored in mm → read in meters
pos = UnitsLens.get_attr(translate_attr, target_mpu=1.0)
# → Gf.Vec3d(0.01, 0, 0)

# Derived quantities use dimensional exponents automatically
density = UnitsLens.get_attr(density_attr, target_mpu=0.01)
# 2700 kg/m³ → 0.0027 kg/cm³  (L⁻³ exponent applied)
```

### Author in your preferred units

```python
# "Place this at 5cm, I'm thinking in meters"
UnitsLens.set_translate(shaft, Gf.Vec3d(0.05, 0, 0), source_mpu=1.0)
# Prim is in mm → stores (50, 0, 0)
```

### Time samples and animation curves

```python
# Bulk read all keyframes, conversion factor resolved once
samples = UnitsLens.get_time_samples(anim_attr, target_mpu=1.0)

# Bezier spline: values and tangent slopes scale, widths (time) preserved
converted_curve = UnitsLens.get_spline(focus_attr, target_mpu=1.0)
```

### PointInstancer

```python
# 100k instance positions — numpy fast-path: 1.3ms
positions = UnitsLens.get_attr(pi.GetAttribute("positions"), target_mpu=1.0)
# (100, 0, 0) cm → (1, 0, 0) m

# Velocities use L¹·T⁻¹ exponent
velocities = UnitsLens.get_attr(pi.GetAttribute("velocities"), target_mpu=1.0)
# (5, 0, 0) cm/s → (0.05, 0, 0) m/s
```

### Assembly-time correction

```python
from units_api import MetricsAssembler

# Audit: find all unit mismatches (dry run)
mismatches = MetricsAssembler.audit_stage(stage)

# Fix: add corrective xformOp:scale at reference boundaries
corrections = MetricsAssembler.correct_stage(stage)
```

### Custom attributes (hybrid fallback)

```python
from units_api import PerAttributeUnits, Dimension

# Registry doesn't know myPipeline:flowRate — annotate it
PerAttributeUnits.annotate(flow_attr, Dimension(L=3, T=-1), meters_per_unit=1.0)

# Now UnitsLens converts it automatically
flow = UnitsLens.get_attr(flow_attr, target_mpu=0.01)  # → converted via L³·T⁻¹
```

## API Reference

### MetricsAPI — Prim-Level Unit Declarations

```python
MetricsAPI.apply(prim, meters_per_unit, up_axis, kilograms_per_unit)
MetricsAPI.has_metrics(prim) → bool
MetricsAPI.get_metrics(prim) → dict          # direct (not inherited)
MetricsAPI.get_effective_metrics(prim) → dict # inherited + fallback
MetricsAPI.get_meters_per_unit(prim) → float
MetricsAPI.get_up_axis(prim) → str
MetricsAPI.get_kilograms_per_unit(prim) → float
```

### UnitsLens — Unit-Aware Get/Set

```python
# Attributes (any type: scalar, vector, array, matrix)
UnitsLens.get_attr(attr, target_mpu=1.0, target_kpu=1.0, time=None)
UnitsLens.set_attr(attr, value, source_mpu=1.0, source_kpu=1.0, time=None)

# Time samples (bulk — resolves conversion factor once)
UnitsLens.get_time_samples(attr, target_mpu=1.0, target_kpu=1.0) → [(time, value), ...]
UnitsLens.set_time_samples(attr, samples, source_mpu=1.0, source_kpu=1.0)

# Animation curves (bezier splines — scales values + tangent slopes)
UnitsLens.get_spline(attr, target_mpu=1.0, target_kpu=1.0) → Ts.Spline
UnitsLens.set_spline(attr, spline, source_mpu=1.0, source_kpu=1.0)

# Transforms
UnitsLens.get_translate(prim, target_mpu=1.0, time=None) → Gf.Vec3d
UnitsLens.set_translate(prim, value, source_mpu=1.0, time=None)
UnitsLens.set_scale(prim, value, time=None)

# World-space queries
UnitsLens.get_in_meters(attr, time=None)
UnitsLens.get_local_transform(prim, target_mpu=1.0, time=None) → Gf.Matrix4d
UnitsLens.get_world_position(prim, target_mpu=1.0, time=None) → Gf.Vec3d

# Utilities
UnitsLens.get_conversion_info(attr) → dict
UnitsLens.clear_cache()
```

### MetricsAssembler — Assembly-Time Correction

```python
MetricsAssembler.audit_stage(stage) → [dict, ...]
MetricsAssembler.correct_stage(stage) → [dict, ...]
MetricsAssembler.correct_reference_boundary(prim) → dict | None
MetricsAssembler.apply_corrective_xform(prim, source_mpu, target_mpu, source_up, target_up)
MetricsAssembler.compute_corrective_scale(source_mpu, target_mpu) → float
MetricsAssembler.compute_corrective_rotation(source_up, target_up) → float | None
MetricsAssembler.bake_to_units(stage, target_mpu=1.0, target_kpu=1.0, edit_target=None) → dict
```

#### bake_to_units

Converts all unit-bearing attribute values to target units by writing overs
to a session sublayer. Unlike `correct_stage()` which only fixes transforms
at reference boundaries, `bake_to_units()` rewrites every unit-bearing value
(transforms, physics, cameras, lights, custom attributes) so downstream
consumers see correct numbers with plain `attr.Get()`.

```python
from units_api import MetricsAPI, MetricsAssembler

# Open a mm-scale stage
stage = Usd.Stage.Open("factory_mm.usda")
MetricsAPI.apply(stage.GetDefaultPrim(), meters_per_unit=0.001)

# Bake to meters — non-destructive, creates override layer
result = MetricsAssembler.bake_to_units(stage, target_mpu=1.0)
print(f"Converted {result['attrs_converted']} attrs, {result['time_samples_converted']} time samples")

# Now plain USD reads return correct values — no UnitsLens needed
density = stage.GetPrimAtPath("/Body").GetAttribute("physics:density").Get()
# → 7800.0 (kg/m³)

gravity = stage.GetPrimAtPath("/Scene").GetAttribute("physics:gravityMagnitude").Get()
# → 9.81 (m/s²)

# Original layer untouched. Delete the session sublayer to revert.
```

### PerAttributeUnits — Self-Describing Attribute Metadata

```python
PerAttributeUnits.annotate(attr, dimension, meters_per_unit=1.0, kilograms_per_unit=1.0)
PerAttributeUnits.get_annotation(attr) → dict | None
PerAttributeUnits.has_annotation(attr) → bool
PerAttributeUnits.get_attr(attr, target_mpu=1.0, target_kpu=1.0, time=None)
PerAttributeUnits.set_attr(attr, value, source_mpu=1.0, source_kpu=1.0, time=None)
PerAttributeUnits.annotate_prim(prim, meters_per_unit, kilograms_per_unit=1.0, registry=None)
PerAttributeUnits.annotate_stage(stage, meters_per_unit, kilograms_per_unit=1.0, registry=None)
```

## Dimensional Registry

26 entries covering transforms, camera, lights, physics, and PointInstancer:

| Attribute | Dimension | Exponents |
|---|---|---|
| `xformOp:translate`, `points`, `extent`, `size` | length | L¹ |
| `xformOp:transform` | length (translation only) | L¹ |
| `focusDistance`, `clippingRange` | length | L¹ |
| `inputs:width`, `inputs:height`, `inputs:radius` | length | L¹ |
| `positions` (PointInstancer) | length | L¹ |
| `velocities`, `physics:velocity` | velocity | L¹·T⁻¹ |
| `accelerations`, `physics:gravityMagnitude` | acceleration | L¹·T⁻² |
| `physics:density` | density | L⁻³·M¹ |
| `physics:mass` | mass | M¹ |
| `xformOp:scale`, `orientations`, `angularVelocities` | unitless | — |

**Not in registry** (fixed units per schema): `focalLength` (mm), `horizontalAperture` (mm)

## Key Findings

1. **MetricsAPI (prim-level) is the right primary mechanism** — 2 annotations vs 66 for the same stage (33× less overhead), identical correctness for schema attributes.

2. **Per-attribute metadata is essential for custom attributes** — registry can't convert `myPipeline:flowRate`; per-attribute annotation makes it self-describing.

3. **Dimensional exponents are schema-invariant** — every `xformOp:translate` is L¹, every `physics:density` is M¹·L⁻³. This never changes per-prim. Belongs in schema definitions, not per-instance data.

4. **Camera attributes have mixed semantics** — `focalLength` (fixed mm) vs `focusDistance` (scene units) on the same prim. Both approaches need attribute-level knowledge.

5. **Assembly correction + lens compose correctly** — corrective `xformOp:scale` for transforms + UnitsLens for derived quantities = complete coverage.

6. **Animation curves require tangent slope scaling** — for bezier splines, values and slopes scale by the same factor; tangent widths (time) are preserved.

## Performance

| Operation | Time |
|---|---|
| Scalar `get_attr` | 8 µs (cached) |
| 100k Vec3f array | 1.3 ms (numpy) |
| 240-frame time samples | 1.1 ms (bulk) |
| Bezier spline conversion | <0.1 ms |

## Statistics

| Metric | Value |
|---|---|
| Implementation | ~950 lines |
| Tests | ~2000 lines |
| Test count | 133 |
| Test stages | 6 programmatic |
| USD version | 26.3 |

## Related Proposals

- [Units & Scale](https://github.com/jensjebens/OpenUSD-proposals/blob/jjebens/units-and-scale/proposals/units_and_scale/README.md) — problem statement and design principles
- [MetricsAPI / Revise Layer Metadata (PR #45)](https://github.com/PixarAnimationStudios/OpenUSD-proposals/pull/45) — prim-level unit declarations

## License

Same as OpenUSD. See [LICENSE.txt](../../LICENSE.txt).
