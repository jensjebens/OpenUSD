# Newton Physics OpenExec Plugin

A proof-of-concept OpenExec plugin that integrates the
[Newton Dynamics 4](https://github.com/MADEAPPS/newton-dynamics) physics
engine with OpenUSD, enabling real-time rigid-body simulation driven by
`UsdPhysicsRigidBodyAPI` schemas.

## Status

**Phase 3 (Rewritten) — Clean Pipeline.** The plugin now uses a clean
Newton→OpenExec→Hydra pipeline with no session layer. The
`computeSimulatedTransform` OpenExec computation uses the builtin
`computePath` to resolve the prim's `SdfPath`, then queries
`NewtonPhysicsSystem` directly for the simulated transform. The
`HdExecComputedTransformSceneIndex` scene index filter consumes the
computation to overlay transforms onto the Hydra 2.0 data model.

### Roadmap

| Phase | Description | Status |
|-------|-------------|--------|
| 0     | Scaffolding, stubs, test assets | ✅ Done |
| 1     | Newton world lifecycle — ndWorld, stepping, gravity | ✅ Done |
| 2     | USD → Newton body mapping — shapes, mass, kinematic | ✅ Done |
| 3     | Clean pipeline: Newton→OpenExec→Hydra (no session layer) | ✅ Done |
| 3.5   | HdExec scene index filter integration | ✅ Done |
| 4     | Material properties (friction, restitution) | ✅ Done |
| 5     | Demo scene, performance profiling | Planned |

## Directory Structure

```
newtonPhysics/
├── CMakeLists.txt                  ← Build configuration
├── cmake/
│   └── FindNewtonDynamics.cmake    ← CMake find module for Newton 4
├── plugInfo.json                   ← Exec plugin registration
├── README.md                       ← This file
├── newtonBodyNotify.h              ← Custom ndBodyNotify for gravity
├── newtonTypes.h                   ← Newton↔USD type conversions
├── newtonWorldManager.h/.cpp       ← ndWorld lifecycle management
├── usdToNewtonMapper.h/.cpp        ← USD physics prim → Newton body mapping
├── newtonPhysicsSystem.h/.cpp      ← Central orchestrator (singleton)
├── newtonSimulationDriver.h/.cpp   ← DEPRECATED: thin wrapper for compat
├── newtonPhysicsComputations.cpp   ← EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA
└── testenv/
    ├── testPluginLoads.cpp                ← Plugin load/registration test
    ├── testWorldCreation.cpp              ← World lifecycle tests
    ├── testBodyMapping.cpp                ← Body mapping count/kinematic tests
    ├── testShapeMapping.cpp               ← Shape type mapping tests
    ├── testMassProperties.cpp             ← Mass/density handling tests
    ├── testFallingBox.cpp                 ← Falling box integration test
    ├── testGroundCollision.cpp            ← Ground collision/settlement test
    ├── testMultiBody.cpp                  ← Multi-body/multi-shape test
    ├── testExecTransformWithPhysics.cpp   ← HdExec pipeline integration test
    ├── testNewtonPhysicsPlugin/           ← Test plugin resources
    ├── fallingBox.usda                    ← Single falling box
    ├── stackedBoxes.usda                  ← Three stacked boxes
    ├── mixedShapes.usda                   ← Sphere, box, capsule
    ├── kinematicAndDynamic.usda           ← Kinematic + dynamic interaction
    └── materialFriction.usda              ← Friction material test
```

## Architecture

### Clean Pipeline (Phase 3)

The simulation data flows directly from Newton through OpenExec to Hydra,
with no session-layer intermediary:

```
Newton World  →  NewtonPhysicsSystem  →  OpenExec Computation  →  HdExec Filter  →  Hydra
  (ndWorld)       (stores transforms      (computeSimulated        (HdXformSchema    (Storm
   step()          per SdfPath)            Transform via            overlay)           renders
                                           computePath builtin)                        xform)
```

The computation IS the transport — no baking, no session layer.

### Central Orchestrator

`NewtonPhysicsSystem` is a singleton that ties the world manager and
mapper together:

1. **EnsureInitialized(stage)**: Find `PhysicsScene`, create Newton world,
   map all physics bodies
2. **AdvanceToTime(seconds)**: Step the Newton world, then pull all body
   transforms from Newton into the mapper's cached records via
   `UpdateSimulatedTransforms()`
3. **GetSimulatedTransform(path)**: Return the cached `GfMatrix4d` for a
   prim — thread-safe since transforms are updated once per frame before
   any computation evaluates

### OpenExec Computation

`computeSimulatedTransform` is registered via
`EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdPhysicsRigidBodyAPI)`. It takes
two inputs:

- `AttributeValue<bool>("physics:rigidBodyEnabled")` — to check if the
  body is enabled
- `Computation(ExecBuiltinComputations->computePath)` — the builtin that
  returns the prim's `SdfPath`

The callback queries `NewtonPhysicsSystem::GetSimulatedTransform(primPath)`
directly, returning a `GfMatrix4d`.

### HdExec Scene Index Filter (Hydra Pipeline)

The generic `HdExecComputedTransformSceneIndex` (from `pxr/imaging/hdExec/`)
consumes the `computeSimulatedTransform` computation and overlays the
resulting `GfMatrix4d` onto the Hydra `HdXformSchema` for each prim.

**Setting up in a viewer:**

```cpp
#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"
#include "pxr/exec/execUsd/system.h"

// 1. Create the exec system from the stage
auto execSystem = std::make_shared<ExecUsdSystem>(stage);

// 2. Initialize the Newton physics system
NewtonPhysicsSystem &sys = NewtonPhysicsSystem::GetInstance();
sys.EnsureInitialized(stage);

// 3. Insert the HdExec filter into the scene index chain
auto physicsFilter = HdExecComputedTransformSceneIndex::New(
    inputSceneIndex,          // upstream scene index
    stage,                    // USD stage for prim lookups
    execSystem,               // OpenExec system
    {TfToken("computeSimulatedTransform")},  // computation token
    /* resetXformStack = */ true);            // world-space transforms

// 4. Each frame: advance physics, then advance filter time
sys.AdvanceToTime(currentTimeInSeconds);
physicsFilter->SetTime(currentTime);
```

The filter automatically discovers which prims have
`computeSimulatedTransform` available (those with `UsdPhysicsRigidBodyAPI`)
and overlays the exec-computed matrix. Prims without the computation pass
through unchanged.

### NewtonSimulationDriver (DEPRECATED)

`NewtonSimulationDriver` is retained as a thin convenience wrapper around
`NewtonPhysicsSystem` for backward compatibility. It no longer creates
session sublayers — all calls delegate to the physics system singleton.
New code should use `NewtonPhysicsSystem` directly.

### Body Mapping

`UsdToNewtonMapper` traverses the stage and creates Newton bodies:

- **Dynamic bodies**: Prims with `PhysicsRigidBodyAPI` (not kinematic).
  Created as `ndBodyDynamic` with mass and gravity callback.
- **Kinematic bodies**: Prims with `PhysicsRigidBodyAPI` where
  `physics:kinematicEnabled = true`. Created as `ndBodyKinematic`.
- **Static colliders**: Prims with `PhysicsCollisionAPI` but no
  `PhysicsRigidBodyAPI`. Created as `ndBodyKinematic` with zero velocity.

After each Newton step, `UpdateSimulatedTransforms()` iterates all body
records and caches the current `GfMatrix4d` from each Newton body. This
ensures thread-safe reads during computation evaluation.

### Gravity

Newton 4 applies gravity per-body via `ndBodyNotify::OnApplyExternalForce()`.
`NewtonGravityNotify` implements this callback, applying `F = m * g` each step.

### Collision Shapes

| USD Prim Type | Newton Shape |
|---------------|--------------|
| `UsdGeomCube` | `ndShapeBox` |
| `UsdGeomSphere` | `ndShapeSphere` |
| `UsdGeomCapsule` | `ndShapeCapsule` |
| Other | `ndShapeBox` (1×1×1 fallback) |

## Building

### Without Newton (stub mode)

The plugin compiles without Newton Dynamics installed.
Newton-dependent code is guarded by `#ifdef NEWTON_DYNAMICS_FOUND`.

```bash
cd OpenUSD
python build_scripts/build_usd.py /path/to/build
```

### With Newton Dynamics

```bash
cmake -DNEWTON_DYNAMICS_ROOT=/path/to/newton4 ...
```

## Dependencies

- **OpenUSD** (with OpenExec — `exec`, `execUsd`, `vdf`)
- **UsdPhysics** schemas (`usdPhysics`, `usdGeom`)
- **Newton Dynamics 4** (optional — stubs compile without it)

## License

Licensed under the terms set forth in the LICENSE.txt file available at
https://openusd.org/license.
