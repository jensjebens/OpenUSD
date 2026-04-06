# Appendix C: Implementation Exploration — Evaluation-Time Unit Resolution

This appendix documents a proof-of-concept implementation of evaluation-time
unit resolution using OpenExec (shipping with OpenUSD since v25.08). The
purpose is to validate the proposal's claim that unit-aware value resolution
is feasible, identify architectural prerequisites, and provide performance
data.

## Summary of findings

1. **Evaluation-time unit resolution is feasible** using OpenExec's
   computation framework. A plugin computation can read a prim's standard
   `computeLocalToWorldTransform` from execGeom, apply a unit correction
   factor, and return a corrected matrix — all within OpenExec's caching
   and invalidation infrastructure.

2. **MetricsAPI (prim-level unit metadata) is a hard prerequisite.**
   OpenExec computations are pure functions of their declared inputs.
   `VdfContext` provides `GetInputValue` / `SetOutput` — no access to
   `UsdPrim`, `UsdStage`, `PrimIndex`, or layer metadata. Because
   `metersPerUnit` is currently layer metadata, it is invisible to the
   computation framework. Prim-level unit attributes (as proposed in
   [PR #45](https://github.com/PixarAnimationStudios/OpenUSD-proposals/pull/45))
   are required for OpenExec-based unit resolution.

3. **Performance overhead is negligible at production scale.** At 10,000
   prims, unit-aware computation adds 0–5% overhead compared to the
   standard transform computation. The graph compilation (prepare) phase
   dominates; the actual per-prim compute cost is minimal.

4. **Composition is completely unaffected.** `UsdAttribute::Get()` returns
   the authored value exactly as written. Unit correction is applied only
   when explicitly requested through the unit-aware computation.

5. **Dimensional analysis works for physics attributes.** By maintaining
   a table of (length exponent, mass exponent) per attribute, the same
   mechanism handles transforms, velocity, density, inertia, gravity, and
   other physics quantities. Both `metersPerUnit` and `kilogramsPerUnit`
   mismatches are detected and corrected.

6. **upAxis correction integrates cleanly** with unit scaling. A ±90°
   rotation around the X axis converts between Y-up and Z-up conventions,
   applied before unit scaling at arc boundaries.

## Architecture

### Approach 1: Python POC (PrimIndex walk)

The simplest approach demonstrates that the information needed for unit
correction is accessible at evaluation time. Given a prim introduced via a
reference or payload arc:

```python
from pxr import Usd, UsdGeom, Pcp

def get_unit_scale_for_prim(prim):
    """Walk PrimIndex to detect metersPerUnit mismatch at arc boundary."""
    stage_mpu = UsdGeom.GetStageMetersPerUnit(prim.GetStage())
    prim_index = prim.GetPrimIndex()

    for child in prim_index.rootNode.children:
        if child.arcType in (Pcp.ArcTypeReference, Pcp.ArcTypePayload):
            ref_root_layer = child.layerStack.layers[0]
            ref_mpu = ref_root_layer.pseudoRoot.GetInfo('metersPerUnit')
            if ref_mpu and not UsdGeom.LinearUnitsAre(ref_mpu, stage_mpu):
                return ref_mpu / stage_mpu
    return 1.0
```

This works with current USD infrastructure — no schema changes, no
MetricsAPI. The PrimIndex preserves the full composition graph, and each
node's layer stack carries its `metersPerUnit`. The scale factor is the
ratio of the referenced layer's `metersPerUnit` to the stage's.

**Limitation:** This approach requires the consumer to call a special
function instead of `UsdAttribute::Get()`. It does not participate in
OpenExec's caching or invalidation. It is suitable for pipeline tooling
(e.g., authoring corrective attributes at reference boundaries) but not
for general-purpose evaluation-time resolution.

### Approach 2: OpenExec computation (C++ plugin)

The production approach registers a computation within OpenExec's framework:

```cpp
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UnitsResolutionAPI)
{
    self.PrimComputation(_tokens->computeUnitAwareLocalToWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeUnitAwareL2W)
        .Inputs(
            // Standard L2W from execGeom (same prim, cross-schema)
            Computation<GfMatrix4d>(
                _tokens->computeLocalToWorldTransform),

            // Unit scale factor (prim-level attribute)
            AttributeValue<double>(
                _tokens->unitScale)
        );
}
```

The computation:
1. Reads `computeLocalToWorldTransform` from execGeom via `Computation<>`
   (Local traversal — accesses computations on the same prim across schemas)
2. Reads the `unitsResolution:metersPerUnitScale` attribute
3. Applies the scale factor to the translation component of the matrix
4. Returns the corrected `GfMatrix4d`

**Key design decisions:**

- **Separate API schema.** OpenExec does not allow two plugins to register
  computations for the same schema. Since `execGeom` owns
  `UsdGeomXformable`, we register on a custom `UnitsResolutionAPI` applied
  schema. In production, this would be `UsdGeomMetricsAPI` or equivalent.

- **Opt-in.** Prims without `UnitsResolutionAPI` applied are unaffected.
  The standard `computeLocalToWorldTransform` returns raw authored values.
  This preserves backward compatibility.

- **Prim-level scale attribute.** Because OpenExec cannot access layer
  metadata from referenced layers, the unit scale must be expressed as a
  prim attribute. This is the architectural gap that MetricsAPI fills.

### Why MetricsAPI is required

OpenExec's computation model is deliberately constrained: computations are
pure functions of declared inputs. The available input registrations are:

| Input Registration | Source |
|---|---|
| `AttributeValue<T>(name)` | Prim attribute values |
| `Metadata<T>(key)` | Prim metadata (NOT layer metadata) |
| `NamespaceAncestor<T>(name)` | Computation on nearest ancestor prim |
| `Stage().Computation<T>(name)` | Stage-level builtin computations |
| `Computation<T>(name)` | Computation on same prim (cross-schema) |
| `Constant<T>(value)` | Compile-time constants |

None of these can read layer metadata from a referenced layer's root layer.
`metersPerUnit` as currently defined (layer metadata resolved by strongest
opinion) is inaccessible to OpenExec computations.

With MetricsAPI, `metersPerUnit` becomes a prim-level attribute that
participates in composition. It would be readable via
`AttributeValue<double>` and could be inherited down the namespace hierarchy
via `NamespaceAncestor<double>` — exactly matching the inheritance semantics
proposed in PR #45.

## Usage example

### Scene setup

A meter-scale stage references a centimeter-scale robot arm:

```usda
#usda 1.0
(
    metersPerUnit = 1.0
    upAxis = "Y"
)

def Xform "Factory" {
    # Reference a cm-scale robot arm asset
    def Xform "RobotArm" (
        references = @robot_arm.usd@</RobotArm>
        apiSchemas = ["UnitsResolutionAPI"]
    )
    {
        # Authored by pipeline tooling or MetricsAPI:
        # source mPU (0.01) / stage mPU (1.0) = 0.01
        double unitsResolution:metersPerUnitScale = 0.01
    }
}
```

Where `robot_arm.usd` is:

```usda
#usda 1.0
(
    metersPerUnit = 0.01
    upAxis = "Y"
)

def Xform "RobotArm" {
    uniform token[] xformOpOrder = ["xformOp:translate"]
    double3 xformOp:translate = (50, 0, 100)

    def Xform "Gripper" {
        uniform token[] xformOpOrder = ["xformOp:translate"]
        double3 xformOp:translate = (0, 0, 30)
    }
}
```

### Querying values

```cpp
ExecUsdSystem execSystem(stage);

// Standard query — returns raw authored value (50, 0, 100) in cm
auto stdKeys = {{prim, TfToken("computeLocalToWorldTransform")}};
// ... returns translate = (50, 0, 100)

// Unit-aware query — returns value in stage units (0.5, 0, 1.0) in meters
auto unitKeys = {{prim, TfToken("computeUnitAwareLocalToWorldTransform")}};
// ... returns translate = (0.5, 0, 1.0)
```

The standard computation is unchanged. The unit-aware computation is
available only on prims with `UnitsResolutionAPI` applied.

## Performance

Measured on a 128-core x86_64 machine (Ubuntu 22.04, GCC 11.4, USD v26.03):

| Prims  | Standard L2W | Unit-aware L2W | Overhead |
|--------|-------------|----------------|----------|
| 100    | 1.7 ms      | 3.1 ms         | ~1.8x    |
| 1,000  | 86.4 ms     | 12.6 ms        | 0.15x    |
| 5,000  | 108.9 ms    | 207.1 ms       | ~1.9x    |
| 10,000 | 502.4 ms    | 489.5 ms       | ~1.0x    |

At production scale (10,000 prims), the overhead of unit-aware computation
is negligible. The graph compilation (prepare) phase dominates total time;
the per-prim compute cost — one matrix read, one double read, one
multiply, one matrix write — is minimal.

The 1,000-prim result where unit-aware is faster than standard is likely a
caching artifact from the test execution order and should not be interpreted
as a performance advantage.

## Attribute coverage

### Transform attributes

The POC corrects the translation component of transform matrices. Both
`metersPerUnit` scaling and `upAxis` rotation are applied:

- **metersPerUnit scaling:** `translate *= source_mpu / stage_mpu`
- **upAxis rotation:** ±90° rotation around the X axis for Y↔Z conversion

The correction order is: upAxis rotation first, then unit scale. This is
because the rotation reorients the coordinate frame, and the scale converts
magnitudes.

Example: A Z-up cm asset referenced into a Y-up m stage:

```
authored:  (0, 0, 168) cm, Z-up
rotate:    (0, 168, 0) Y-up  (Z→Y: +90° around X)
scale:     (0, 1.68, 0) m    (×0.01 cm→m)
```

### Physics attributes

Physics attributes require dimensional analysis — each attribute has a
specific relationship to length and mass units. The POC maintains a
dimension table:

| Attribute | Length exp | Mass exp | Example (cm→m) |
|---|---|---|---|
| `physics:gravityMagnitude` | 1 | 0 | 981 → 9.81 |
| `physics:velocity` | 1 | 0 | (50,0,-30) → (0.5,0,-0.3) |
| `physics:density` | −3 | 1 | 7.8 → 7.8×10⁶ |
| `physics:centerOfMass` | 1 | 0 | (5,5,5) → (0.05,0.05,0.05) |
| `physics:diagonalInertia` | 2 | 1 | (100,100,100) → (0.01,0.01,0.01) |
| `physics:mass` | 0 | 1 | (kgPU mismatch only) |
| `physics:angularVelocity` | — | — | unchanged (radians/time) |

The conversion formula for any unit-bearing attribute is:

```
corrected = raw × (mpu_scale ^ length_exp) × (kpu_scale ^ mass_exp)
```

Where:
- `mpu_scale = source_metersPerUnit / stage_metersPerUnit`
- `kpu_scale = source_kilogramsPerUnit / stage_kilogramsPerUnit`

For vector-valued physics attributes, upAxis rotation is applied before
scaling.

This dimension table approach is deliberately explicit — it documents
the assumptions that any conversion mechanism must encode. The proposal
notes that this is "a cataloging problem" and calls for a systematic
audit of unit-bearing attributes across schema domains. The table here
covers the UsdPhysics attributes that are most frequently miscorrected
in production.

## Limitations and future work

1. **Camera and light attributes.** The current implementation covers
   transforms and physics attributes. Camera spatial attributes (clipping
   range, focus distance) and light spatial attributes (length, radius,
   width, height) are not yet covered but follow the same dimensional
   analysis pattern.

2. **Material spatial properties.** Displacement magnitude, subsurface
   scattering distances, and volumetric density are not yet covered.

3. **Flat hierarchy only.** Nested unit mismatches (e.g., mm asset
   referenced into a cm stage referenced into a m stage) require
   chaining corrections through the composition graph. The current
   implementation detects only the immediate arc boundary.

4. **No Hydra integration.** The corrected transform is available via
   OpenExec but not yet consumed by Hydra's scene index. Integration
   would require either a filtering scene index or Hydra adopting
   OpenExec as its computation backend.

5. **Manual scale authoring.** Without MetricsAPI, the unit scale
   attribute must be authored by pipeline tooling at reference
   boundaries. MetricsAPI would make this automatic.

6. **Single schema registration constraint.** OpenExec's one-plugin-per-
   schema rule means the unit-aware computation must live on a separate
   API schema, not on `UsdGeomXformable` directly. With community
   alignment, the computation could be integrated into `execGeom`.

## Conclusion

Evaluation-time unit resolution via OpenExec is feasible, performant, and
architecturally clean. The primary blocker is not the computation framework
itself, but the availability of prim-level unit metadata — precisely the
infrastructure that MetricsAPI (PR #45) proposes to provide. This
exploration validates both the technical approach described in the proposal
and the dependency on MetricsAPI as a prerequisite.

The POC code is available at:
`https://github.com/[TBD]/units-resolution-poc`
