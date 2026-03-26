# Newton Physics OpenExec Plugin

An OpenExec plugin that integrates
[Newton Dynamics 4](https://github.com/MADEAPPS/newton-dynamics) with
OpenUSD for rigid-body simulation driven by `UsdPhysicsRigidBodyAPI`.

![Falling box collides with ground plane](demo.gif)

*Rendered by `usdrecord` + Storm. Transforms driven live by Newton through
OpenExec → HdExec → Hydra — no baking, no session layer.*

## Architecture

```
Newton World  →  NewtonPhysicsSystem  →  TransformProvider  →  HdExec Filter  →  Hydra
  (ndWorld)       (stores transforms      (callback from          (HdXformSchema    (Storm
   step()          per SdfPath)            plugin to filter)       overlay)           renders)
```

### NewtonPhysicsSystem

Singleton orchestrator:

1. **EnsureInitialized(stage)** — reads `PhysicsScene`, creates `ndWorld`, maps bodies
2. **AdvanceToTime(seconds)** — steps Newton, pulls transforms into cache
3. **GetSimulatedTransform(path)** — returns cached `GfMatrix4d` (thread-safe)

### OpenExec Computation

`computeSimulatedTransform` registered via
`EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdPhysicsRigidBodyAPI)`.
Uses `ExecBuiltinComputations->computePath` to resolve the prim's `SdfPath`,
queries `NewtonPhysicsSystem` for the transform.

### HdExec Scene Index Filter

`HdExecComputedTransformSceneIndex` overlays simulated transforms onto
Hydra's `HdXformSchema`. A `TransformProvider` callback bypasses exec's
computation cache (which doesn't invalidate for side-effect-driven physics).
The Newton plugin registers this callback at load time.

### Body Mapping

`UsdToNewtonMapper` traverses the stage:

| USD Schema | Newton Body |
|-----------|------------|
| `PhysicsRigidBodyAPI` (dynamic) | `ndBodyDynamic` with gravity callback |
| `PhysicsRigidBodyAPI` (kinematic) | `ndBodyKinematic` |
| `PhysicsCollisionAPI` only | `ndBodyKinematic` (static) |

### Collision Shapes

| USD Prim Type | Newton Shape |
|---------------|--------------|
| `UsdGeomCube` | `ndShapeBox` |
| `UsdGeomSphere` | `ndShapeSphere` |
| `UsdGeomCapsule` | `ndShapeCapsule` |
| Other | `ndShapeBox` (1×1×1 fallback) |

### Gravity

Newton 4 applies gravity per-body via `ndBodyNotify::OnApplyExternalForce()`.
Gravity direction and magnitude are read from `UsdPhysicsScene`.

## Directory Structure

```
newtonPhysics/
├── CMakeLists.txt                  Build configuration
├── cmake/FindNewtonDynamics.cmake  CMake find module for Newton 4
├── plugInfo.json                   Exec plugin registration
├── newtonTypes.h                   Newton↔USD type conversions
├── newtonWorldManager.h/.cpp       ndWorld lifecycle
├── usdToNewtonMapper.h/.cpp        USD → Newton body mapping
├── newtonPhysicsSystem.h/.cpp      Central orchestrator (singleton)
├── newtonPhysicsComputations.cpp   OpenExec computation + TransformProvider
└── testenv/
    ├── testPluginLoads.cpp         Plugin load/registration
    ├── testWorldCreation.cpp       World lifecycle
    ├── testBodyMapping.cpp         Body mapping
    ├── testShapeMapping.cpp        Shape type mapping
    ├── testMassProperties.cpp      Mass/density handling
    ├── testFallingBox.cpp          Falling box integration
    ├── testGroundCollision.cpp     Ground collision/settlement
    ├── testMultiBody.cpp           Multi-body/multi-shape
    ├── testMaterialFriction.cpp    Friction properties
    ├── testRestitution.cpp         Restitution properties
    ├── testExecTransformWithPhysics.cpp  Full pipeline integration
    ├── fallingBox.usda             Single falling box
    ├── demoScene.usda              Camera + scene for usdrecord
    ├── stackedBoxes.usda           Three stacked boxes
    ├── mixedShapes.usda            Sphere, box, capsule
    ├── kinematicAndDynamic.usda    Kinematic + dynamic interaction
    └── materialFriction.usda       Friction material test
```

## Usage

```cpp
#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"

// Insert the HdExec filter into the scene index chain
auto physicsFilter = HdExecComputedTransformSceneIndex::New(
    inputSceneIndex, stage, execSystem,
    {TfToken("computeSimulatedTransform")},
    /* resetXformStack = */ true);

// Each frame:
NewtonPhysicsSystem::GetInstance().AdvanceToTime(seconds);
physicsFilter->SetTime(currentTime);
```

## Building

### With Newton Dynamics

```bash
cmake -DNEWTON_DYNAMICS_ROOT=/path/to/newton4 ...
```

### Without Newton (stub mode)

Compiles without Newton installed. Physics code is guarded by
`#ifdef NEWTON_DYNAMICS_FOUND` — stubs return initial transforms.

## Dependencies

- **OpenUSD** (with OpenExec — `exec`, `execUsd`, `vdf`)
- **UsdPhysics** schemas (`usdPhysics`, `usdGeom`)
- **Newton Dynamics 4** (optional)

## Lessons Learned

### Newton Dynamics 4

- **`CollisionUpdate(dt)` before `Update(dt)`**: Broadphase/narrowphase
  must run before the solver. Without it, zero contact pairs → bodies
  pass through each other. Sequence: `CollisionUpdate` → `Update` → `Sync`.

- **Gravity is per-body, not per-world**: Applied via
  `ndBodyNotify::OnApplyExternalForce()` with `F = mass * gravity`.

- **Shapes are shared, bodies own the matrix**: `ndShapeInstance` wraps
  the base shape. `SetMatrix()` on the body controls world placement.

### OpenExec

- **`computePath` is the key builtin**: Use
  `ExecBuiltinComputations->computePath` to get the prim's `SdfPath`
  inside computation callbacks.

- **Exec caches side-effect-driven computations**: If a computation's
  inputs don't change (e.g. `computePath` is constant), exec caches the
  result forever. Physics transforms change every frame but exec doesn't
  know. Fix: `TransformProvider` callback that bypasses exec's cache.

- **`BuildRequest().IsValid()` returns true for prims without the
  schema**: Guard with `prim.HasAPI<T>()` before querying exec.

### Hydra / Scene Index

- **Wrap xform under `"xform"` key**: `HdXformSchema::Builder().Build()`
  returns the inner container. Overlay needs it keyed as `"xform"` via
  `HdRetainedContainerDataSource`.

- **`UniversalSet` for cache eviction**: `CachingSceneIndex` ignores
  locator-specific dirty. Use `HdDataSourceLocatorSet::UniversalSet()`.

- **Bootstrap timing**: The scene index filter auto-bootstraps on stage
  discovery. Listen for both `StageContentsChanged` and `_PrimsAdded` to
  catch the stage regardless of initialization order.

### plugInfo.json

- **No `Type` field** for exec computation plugins — conflicts with exec
  registration. Only `LibraryPath` and `Info`.

- **`loadWithRenderer: GL`** for scene index plugins active during
  `usdrecord` headless rendering.

## License

Licensed under the terms set forth in the LICENSE.txt file available at
https://openusd.org/license.
