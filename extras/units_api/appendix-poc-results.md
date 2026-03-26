# Appendix: Units API Proof of Concept

## Overview

This appendix documents the results of a Python proof-of-concept implementing a units API for OpenUSD. The POC was built to answer a concrete question: is prim-level unit declaration (MetricsAPI, per PR #45) sufficient on its own, or does a complete solution also require per-attribute unit metadata? The answer, supported by empirical measurement across five programmatic test stages, is: both, in layers, with the hybrid approach covering all cases.

---

## Usage Examples

### Declaring Units on Assets

```python
from pxr import Usd, UsdGeom
from units_api import MetricsAPI

# Open a millimeter-scale CAD asset
stage = Usd.Stage.Open("bolt.usd")
root = stage.GetPrimAtPath("/Bolt")

# Declare: "everything under /Bolt is in millimeters"
MetricsAPI.apply(root, meters_per_unit=0.001, up_axis="Z")
```

### Reading Attribute Values in Your Preferred Units

```python
from units_api import UnitsLens

# A bolt shaft with translate = (10, 0, 0) authored in mm
shaft = stage.GetPrimAtPath("/Factory/Equipment/Bolt/Shaft")
translate_attr = shaft.GetAttribute("xformOp:translate")

# "Give me this value in meters"
pos = UnitsLens.get_attr(translate_attr, target_mpu=1.0)
# → Gf.Vec3d(0.01, 0, 0)  — 10 mm = 0.01 m

# Works for derived quantities too
density_attr = robot.GetAttribute("physics:density")
# Density authored as 2700 kg/m³ on a meter-context prim
d = UnitsLens.get_attr(density_attr, target_mpu=0.01)  # "in cm units"
# → 0.0027  — correctly applies L⁻³ dimensional exponent
```

### Authoring in Your Preferred Units

```python
# "I think in meters — place this bolt shaft at 5cm from origin"
UnitsLens.set_attr(translate_attr, Gf.Vec3d(0.05, 0, 0), source_mpu=1.0)
# If prim is in mm → stores (50, 0, 0)

# Transform convenience — same thing, cleaner API
UnitsLens.set_translate(shaft, Gf.Vec3d(0.05, 0, 0), source_mpu=1.0)

# "Where is this prim in the world, in meters?"
world_pos = UnitsLens.get_world_position(shaft, target_mpu=1.0)
# Accounts for full transform stack including assembly corrections
```

### Assembly-Time Correction

```python
from units_api import MetricsAssembler

# Audit a stage: find all unit mismatches without changing anything
mismatches = MetricsAssembler.audit_stage(stage)
for m in mismatches:
    print(f"{m['prim_path']}: {m['source_mpu']} → {m['target_mpu']}, "
          f"scale={m['scale']}")
# /Factory/Equipment/Bolt: 0.001 → 1.0, scale=0.001

# Apply non-destructive corrective transforms at all boundaries
corrections = MetricsAssembler.correct_stage(stage)
# Adds xformOp:scale:metricsCorrection = (0.001, 0.001, 0.001) on Bolt
# Source geometry is untouched
```

### PointInstancer — Thousands of Instances

```python
# A cm-scale forest with tree positions in centimeters
pi = stage.GetPrimAtPath("/Forest/Trees")

# Read all instance positions in meters
positions = UnitsLens.get_attr(pi.GetAttribute("positions"), target_mpu=1.0)
# (100, 0, 0) cm → (1, 0, 0) m — each element of the array is converted

# Velocities respect dimensional exponents
velocities = UnitsLens.get_attr(pi.GetAttribute("velocities"), target_mpu=1.0)
# (5, 0, 0) cm/s → (0.05, 0, 0) m/s  — L¹·T⁻¹

# Orientations are unitless — returned unchanged
orientations = UnitsLens.get_attr(pi.GetAttribute("orientations"), target_mpu=1.0)
# Quaternions pass through, no conversion applied
```

### Custom/Extension Attributes — Per-Attribute Fallback

```python
from units_api import PerAttributeUnits, Dimension

# Pipeline-specific attribute not in any schema registry
flow_attr = pipe.GetAttribute("myPipeline:flowRate")

# Without annotation: UnitsLens returns raw value (can't convert what it can't identify)
raw = UnitsLens.get_attr(flow_attr, target_mpu=1.0)  # → 0.002 (passthrough)

# Annotate it: "this is volume flow rate, L³·T⁻¹, authored in meters"
PerAttributeUnits.annotate(flow_attr, Dimension(L=3, T=-1), meters_per_unit=1.0)

# Now UnitsLens hybrid fallback kicks in
converted = UnitsLens.get_attr(flow_attr, target_mpu=0.01)  # "in cm units"
# → 2000.0  — correctly applies (0.01/1.0)³ = 1e-6 inverse → ×1e6
```

### Matrix Transforms (Skel-Ready)

```python
# xformOp:transform stores a 4×4 matrix
# Only translation is scaled — rotation and scale are unitless
transform_attr = prim.GetAttribute("xformOp:transform")
result = UnitsLens.get_attr(transform_attr, target_mpu=1.0)
# Translation: (100, 0, 0) mm → (0.1, 0, 0) m
# Rotation: 45° around Z → still 45° around Z
# Scale: (2, 2, 2) → still (2, 2, 2)

# Also works for VtMatrix4dArray (e.g. Skel restTransforms)
# via per-attribute annotation for non-registry attributes
```

---

## Architecture

The POC implements three storage layers:

1. **MetricsAPI** — prim-level unit declarations (`customData["units_api"]` on prims)
2. **Dimensional registry** — schema-level attribute exponents (Python dict, compile-time knowledge)
3. **Per-attribute metadata** — self-describing unit annotations (`customData["units"]` on attributes)

Plus two consumer APIs:

- **UnitsLens** — read/write any attribute with unit awareness, using hybrid fallback
- **MetricsAssembler** — non-destructive corrective transforms at reference boundaries

```
┌─────────────────────────────────────────┐
│           Units Lens API                │
│  read_in_units(attr, target_mpu)        │
│  author_in_units(attr, value, src_mpu)  │
│  get_dimension(attr) → Dimension        │
├─────────────────────────────────────────┤
│         MetricsAPI Layer                │
│  apply_metrics(prim, mpu, up_axis)      │
│  get_effective_metrics(prim) → context  │
│  (ancestor walk + layer metadata        │
│   fallback)                             │
├─────────────────────────────────────────┤
│         Dimensional Registry            │
│  schema:attr → Dimension(L=1, M=0, T=0)│
│  e.g. xformOp:translate → L¹           │
│       density → L⁻³·M¹                 │
│       gravity → L¹·T⁻²                 │
├─────────────────────────────────────────┤
│              OpenUSD (pxr)              │
└─────────────────────────────────────────┘
```

---

## Implementation

### MetricsAPI

MetricsAPI simulates USD's proposed applied schema mechanism using `customData["units_api"]` on prims. Authoring calls `MetricsAPI.apply(prim, meters_per_unit=0.001)`. Querying calls `MetricsAPI.get_effective_metrics(prim)`, which walks up the ancestor chain until it finds a prim with MetricsAPI applied, then fills any missing keys from stage-level layer metadata (USD defaults as final fallback).

**Ancestor walk semantics:** The first ancestor with MetricsAPI wins for each key. This means a robot prim at `mpu=1.0` overrides a parent factory at `mpu=0.01`. Each key (`metersPerUnit`, `upAxis`, `kilogramsPerUnit`) is resolved independently, so a prim can declare only `metersPerUnit` and inherit `kilogramsPerUnit` from a higher ancestor.

**What MetricsAPI tells you:** The unit system a subtree was authored in. It does not tell you which attributes are unit-bearing or what their dimensional exponents are. That knowledge lives in the dimensional registry.

### Dimensional Registry

The registry is a Python dict mapping attribute names to `Dimension(L, M, T)` namedtuples. It contains 20 entries covering transforms, camera, lights, and physics attributes.

Key decisions reflected in the registry:
- `focalLength` and `horizontalAperture` are excluded — they are in millimeters per camera schema, not scene units
- `focusDistance`, `clippingRange` are included — scene units
- `xformOp:scale` is in the registry as `Dimension(0,0,0)` (unitless ratio)
- Physics attributes cover density (`L⁻³·M¹`), gravity (`L¹·T⁻²`), velocity (`L¹·T⁻¹`), mass (`M¹`)

The `conversion_factor()` function handles all dimensional exponent combinations: `factor = (source_mpu/target_mpu)^L × (source_kpu/target_kpu)^M`. Time exponents contribute a factor of 1.0 (no `secondsPerUnit` concept in USD).

### Per-Attribute Metadata

Each attribute can carry its own unit annotation in `customData["units"]`:

```usda
float myPipeline:flowRate = 0.002 (
    customData = {
        dictionary units = {
            string dimension = "L3_T-1"
            double metersPerUnit = 1.0
            double kilogramsPerUnit = 1.0
        }
    }
)
```

The compact dimension encoding (`"L1"`, `"L-3_M1"`, `"L1_T-2"`) is round-trippable via `dimension_to_str()` / `str_to_dimension()`. The annotation is self-describing: no external registry is needed to convert an annotated attribute.

`PerAttributeUnits.annotate_prim()` and `annotate_stage()` demonstrate bulk annotation — they still require a registry to bootstrap dimensions for schema attributes, but the result is stored per-attribute, making it self-describing at rest.

### UnitsLens

`UnitsLens.get_attr(attr, target_mpu, target_kpu)` follows the hybrid fallback chain:

1. Look up `attr.GetName()` in the dimensional registry
2. If found → use registry dimension + `MetricsAPI.get_effective_metrics()` for source context → convert
3. If not found → check `PerAttributeUnits.get_annotation(attr)`
4. If annotated → use per-attribute dimension and `metersPerUnit` → convert
5. If not annotated → return raw value (passthrough)

The same chain applies to `set_attr()`. `get_conversion_info()` reports which source was used (`"registry"`, `"per_attribute"`, or `"passthrough"`).

Transform convenience methods (`set_translate`, `get_translate`, `get_world_position`) compose with `MetricsAssembler` for world-space queries. `get_world_position` uses `UsdGeom.XformCache` to evaluate the full transform stack including any corrective `xformOp:scale` applied by the assembler.

### MetricsAssembler

`MetricsAssembler.correct_reference_boundary(prim)` detects unit mismatches at simulated reference boundaries (prim with MetricsAPI applied that has an ancestor also with MetricsAPI applied) and applies a corrective `xformOp:scale:metricsCorrection` (and optionally `xformOp:rotateX:metricsCorrection` for up-axis differences) prepended to the prim's existing xform ops. The operation is non-destructive: the original prim values are unchanged; the correction is expressed as an additional xform op.

`audit_stage()` is the dry-run version — it reports all mismatches without applying any changes.

**Known limitation:** Assembly correction only fixes transform attributes. Derived physical quantities (density, velocity, etc.) on a cross-boundary prim still require UnitsLens for correct conversion.

---

## Test Results

### Conversion Correctness

All derived quantity conversions verified across five programmatic stages:

| Quantity | Authored | Converted | Factor |
|---|---|---|---|
| Density (L⁻³·M¹) | 2700 kg/m³ (m context) | 0.0027 kg/cm³ | (0.01/1)⁻³ = 1e⁻⁶ |
| Gravity (L¹·T⁻²) | 981 cm/s² (cm context) | 9.81 m/s² | (0.01/1)¹ = 0.01 |
| Velocity (L¹·T⁻¹) | (1,0,0) m/s (m context) | (100,0,0) cm/s | (1/0.01)¹ = 100 |
| Mass (M¹) | 10 kg → 0.01 t | via kpu conversion | (1/1000)¹ |
| focalLength | 50 mm | 50 mm (no change) | passthrough |
| focusDistance | 500 cm | 5.0 m | (0.01/1)¹ = 0.01 |
| World position (after assembly) | 10 mm in mm bolt | 0.01 m | assembly scale + lens |

### Approach Comparison

| Metric | MetricsAPI + Registry | Per-Attribute |
|---|---|---|
| Annotations (Stage 4) | 2 prims | 66 attributes (33×) |
| Custom attribute coverage | passthrough | converts correctly |
| Conversion correctness | identical for known attrs | identical for known attrs |
| Requires external registry | yes (for dimensional exponents) | no (self-describing) |
| Storage overhead | minimal | 3 keys × every unit-bearing attr |

### Coverage Analysis

**Covered by the POC:**
- Transforms: `xformOp:translate`, `points`, `extent`, `size`
- Physics: density, gravity, velocity, mass
- Camera spatial: `focusDistance`, `clippingRange`
- Lights spatial: `inputs:width`, `inputs:height`, `inputs:radius`, `inputs:length`
- Custom attributes: any attribute annotated via `PerAttributeUnits.annotate()`

**Not covered (out of scope for POC):**
- Material properties: SSS scatter distance, displacement height
- Texture physical scale (e.g., bump map distance)
- Time dimension: no `secondsPerUnit` in USD — gravity and velocity T-exponents contribute a factor of 1.0

---

## Findings

### Finding 1: Prim-level MetricsAPI is the right primary mechanism

One MetricsAPI annotation per asset root covers all schema attributes in that subtree. Stage 4 (deep nesting with a machine hierarchy) requires exactly 2 prim annotations — Factory and Machine. The per-attribute approach requires 66 annotations for the same stage (33× more). Conversion correctness is identical for all schema-defined attributes. MetricsAPI matches how content is actually authored: entire assets live in one unit system.

### Finding 2: Per-attribute metadata is essential for custom attributes

`MetricsAPI + registry` cannot convert `myPipeline:flowRate` (L³·T⁻¹) — the attribute name is not in any schema registry, so the result is a passthrough. Per-attribute annotation makes the attribute self-describing and fully convertible. This is not a corner case: any pipeline, studio, or extension schema will have custom attributes that no central registry will know about.

### Finding 3: The hybrid approach covers both cases without correctness penalty

The hybrid UnitsLens fallback (registry → per-attribute → passthrough) adds no correctness penalty for known schema attributes — registry entries always take precedence. Custom attributes that are annotated become convertible. Unannotated custom attributes remain passthrough, same as before. The additional code complexity is minimal (a local import and three conditional branches).

### Finding 4: Dimensional exponents are schema-invariant, not per-instance

Every `xformOp:translate` is L¹. Every `physics:density` is M¹·L⁻³. This is a property of the schema definition, not of any individual prim or attribute value. It does not change per-prim and never needs to be stored per-attribute for schema-defined attributes. The right place for this knowledge is a registry or schema declaration, not instance metadata. Exception: custom attributes, where the schema exponents are unknown and per-attribute annotation is the only option.

### Finding 5: Camera attributes expose a mixed-semantics problem

`focalLength` and `horizontalAperture` are in physical millimeters, hardcoded by the UsdGeom.Camera schema. `focusDistance` and `clippingRange` are in scene units. Both appear on the same prim. Any conversion tool must know at attribute-name resolution which category each attribute falls into. The registry handles this correctly by excluding fixed-unit attributes. A prim-level approach alone (MetricsAPI) is insufficient for camera prims without attribute-level knowledge.

### Finding 6: Assembly correction and UnitsLens compose correctly

A corrective `xformOp:scale:metricsCorrection` at a reference boundary, combined with `UnitsLens.get_world_position()` using `UsdGeom.XformCache`, gives correct world-space positions. A 10 mm bolt shaft placed at (10,0,0) in a meter factory returns a world position of (0.01, 0, 0) meters after assembly correction. Neither mechanism alone is sufficient: the assembler handles transforms; the lens handles derived quantities.

---

## Recommendations for the Ecosystem

1. **MetricsAPI (prim-level) should be the primary unit mechanism.** One annotation per asset root, composable via references, survives flattening. Aligns with PR #45.

2. **Schema working groups should declare dimensional exponents for all unit-bearing attributes.** The dimensional registry in this POC demonstrates the pattern. These declarations are schema-level invariants that belong in schema definitions, not per-instance data.

3. **A standardized per-attribute unit annotation format should be defined.** `customData["units"]` works for a POC but is untyped and unvalidated. A proper plugin metadata field (`unitDimension`, `metersPerUnit`) with schema registration would be cleaner and allow tooling to validate annotations.

4. **Conversion tools should handle derived quantities, not just transforms.** The most common existing approach (corrective xformOp:scale at boundaries) only fixes positions. Density, velocity, and gravity authored in a mis-matched unit context will silently carry wrong values without a lens that understands dimensional exponents.

5. **Camera and light schemas should explicitly document which attributes are in scene units vs. fixed physical units.** The current schema documentation is ambiguous on this point. `focalLength` in mm is not obvious from the attribute name alone, and this ambiguity propagates into every conversion tool.

---

## Statistics

| Metric | Value |
|---|---|
| Lines of code (implementation) | ~880 |
| Lines of tests | ~1950 |
| Test count | 115 (all passing) |
| Test stages | 6 programmatic stages covering 8 scenarios |
| USD version | 26.3 |
| Dimensional registry entries | 26 |
| Approaches compared | 3 (MetricsAPI, per-attribute, hybrid) |
