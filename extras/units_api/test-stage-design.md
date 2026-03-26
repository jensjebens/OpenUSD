# Test Stage Design

## Philosophy

Each test stage targets a specific real-world scenario from the proposals.
Stages are created programmatically (not files) so tests are self-contained.

---

## Stage 1: The Simple Case — Single Reference, Mismatched Units

**Scenario:** A millimeter-scale CAD bolt referenced into a meter-scale factory.

```
/Factory                          (meters, metersPerUnit=1.0)
  /Factory/Floor                  (Mesh, translate=(0,0,0))
  /Factory/Equipment
    /Factory/Equipment/Bolt       (reference → bolt.usd, metersPerUnit=0.001)
      /Factory/Equipment/Bolt/Shaft   (Mesh, points in mm)
      /Factory/Equipment/Bolt/Head    (Mesh, points in mm)
```

**Tests:**
- Read bolt shaft length → should return value in meters (not mm)
- Read floor position → already in meters, no conversion
- Verify MetricsAPI inheritance: Bolt subtree inherits mm context
- Verify factory root is meter context

**Attribute coverage:** transforms, mesh extents (length¹)

---

## Stage 2: Physics Simulation — Derived Quantities

**Scenario:** Robot arm (meters) in a cm-scale simulation stage with physics.

```
/World                            (centimeters, metersPerUnit=0.01)
  /World/PhysicsScene             (gravityMagnitude=981.0 — cm/s²)
  /World/Ground                   (Mesh)
  /World/Robot                    (reference → robot.usd, metersPerUnit=1.0)
    /World/Robot/Base             (RigidBody, mass=50kg)
    /World/Robot/Arm              (RigidBody, mass=10kg)
      /World/Robot/Arm/Joint      (RevoluteJoint)
    /World/Robot/Gripper          (RigidBody, mass=2kg, density=2700 kg/m³)
```

**Tests:**
- Read gravity in world units (cm/s²) vs. canonical (m/s²)
- Read density: 2700 kg/m³ → what is it in the cm-scale stage? (2700 kg/m³ = 0.0027 kg/cm³... but density scales as M·L⁻³)
- Read velocity attribute in world units vs. source units
- Verify dimensional analysis: density ≠ length, needs L⁻³ exponent

**Attribute coverage:** gravity (L·T⁻²), density (M·L⁻³), velocity (L·T⁻¹), mass (M)

---

## Stage 3: Camera & Lights — The Incomplete Coverage Problem

**Scenario:** Film set with camera and lights. Tests the attributes that Metrics Assembler misses.

```
/Scene                            (centimeters, metersPerUnit=0.01)
  /Scene/Set                      (Mesh, a room)
  /Scene/Camera                   (Camera)
    focalLength = 50              (mm — special: "tenths of scene unit" per schema)
    focusDistance = 500            (scene units = 500 cm = 5m)
    clippingRange = (1, 100000)   (scene units)
    horizontalAperture = 36       (mm — special per schema)
  /Scene/KeyLight                 (RectLight)
    inputs:width = 100            (scene units = 100 cm = 1m)
    inputs:height = 100           (scene units)
    inputs:intensity = 1.0        (physically: power per area, scales with L²)
```

**Tests:**
- Read focusDistance in meters → 5.0
- Read focalLength → should NOT be converted (it's in mm per schema, not scene units)
- Read horizontalAperture → should NOT be converted (mm per schema)
- Read light width in meters → 1.0
- Read light intensity: how does it scale? (normalize=true vs false changes semantics)
- Verify that the system correctly distinguishes "scene unit" attrs from "fixed unit" attrs

**Attribute coverage:** camera spatial (mixed!), light spatial (L¹), light intensity (L²)

---

## Stage 4: Multi-Level Nesting — The Factory Floor

**Scenario:** Factory assembling CAD parts from multiple sources. Tests ancestor walk.

```
/Factory                          (meters, metersPerUnit=1.0)
  /Factory/Building               (Xform)
    /Factory/Building/Hall        (Mesh, 200m × 100m)
    /Factory/Building/CNC_Area
      /Factory/Building/CNC_Area/Machine   (ref → cnc.usd, metersPerUnit=0.001, mm)
        /Factory/Building/CNC_Area/Machine/Spindle
          /Factory/Building/CNC_Area/Machine/Spindle/Tool
            /Factory/Building/CNC_Area/Machine/Spindle/Tool/Bit  (tiny geometry)
```

**Tests:**
- Query effective metersPerUnit at /Factory → 1.0
- Query effective metersPerUnit at /Factory/Building/Hall → 1.0 (inherited)
- Query effective metersPerUnit at .../Machine → 0.001 (overridden)
- Query effective metersPerUnit at .../Tool/Bit → 0.001 (inherited from Machine)
- Ancestor walk depth: 6 levels deep, only 2 MetricsAPI boundaries
- Read Tool/Bit extent in factory units (meters)

**Attribute coverage:** transforms (L¹), extents (L¹), deep hierarchy

---

## Stage 5: Per-Attribute Metadata — The Custom Attribute Case

**Scenario:** Pipeline extension with custom attributes that aren't in any schema.

```
/Pipe                             (meters, metersPerUnit=1.0)
  /Pipe/Segment                   (Mesh)
    custom float myPipeline:innerRadius = 0.05
    custom float myPipeline:outerRadius = 0.06
    custom float myPipeline:flowRate = 0.002      (m³/s → L³·T⁻¹)
    custom float myPipeline:pressure = 101325     (Pa = kg/(m·s²) → M·L⁻¹·T⁻²)
    custom float myPipeline:roughnessCoeff = 0.015  (unitless!)
```

**Tests:**
- With dimensional registry: can't convert flowRate/pressure (not in registry)
- With per-attribute customData: can annotate dimensions → conversion works
- Verify unitless attributes are left alone
- Compare: which approach handles unknown attributes?

**Attribute coverage:** custom attributes, unitless values, exotic dimensions

---

## Stage 6: Flattened Stage — Survival Test

**Scenario:** Take Stage 1, flatten it, verify units information survives.

**Tests:**
- MetricsAPI applied schema → survives flattening ✓
- Layer metadata → lost on referenced layers ✗
- Per-attribute customData → survives flattening ✓
- Can we still convert correctly post-flatten with each approach?

---

## Stage 7: Competing Approaches — Head-to-Head

**Scenario:** Same physical setup, encoded three ways:

**7a:** Layer metadata only (status quo)
**7b:** MetricsAPI (prim-level) + dimensional registry
**7c:** Per-attribute customData annotations

Run identical conversion queries on all three. Compare:
- Correctness of results
- Lines of code to set up
- Storage overhead (serialized size)
- Query performance (time to resolve units for N attributes)

---

## Dimensional Exponents Reference (for test assertions)

| Attribute | Dimension | Exponents (L, M, T) |
|---|---|---|
| translate, points, extent | length | (1, 0, 0) |
| size (cube) | length | (1, 0, 0) |
| focusDistance, clippingRange | length | (1, 0, 0) |
| inputs:width, inputs:height (lights) | length | (1, 0, 0) |
| focalLength, horizontalAperture | NONE (fixed mm) | special |
| physics:velocity | length/time | (1, 0, -1) |
| physics:gravityMagnitude | length/time² | (1, 0, -2) |
| physics:density | mass/length³ | (-3, 1, 0) |
| physics:mass | mass | (0, 1, 0) |
| inputs:intensity (light, non-normalized) | 1/length² | (-2, 0, 0) |
| roughnessCoeff, booleans, tokens | unitless | (0, 0, 0) |
| area | length² | (2, 0, 0) |
| volume, flowRate | length³ or length³/time | (3, 0, 0) or (3, 0, -1) |
