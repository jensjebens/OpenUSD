# Newton Physics OpenExec Plugin

A proof-of-concept OpenExec plugin that integrates the
[Newton Dynamics 4](https://github.com/MADEAPPS/newton-dynamics) physics
engine with OpenUSD, enabling real-time rigid-body simulation driven by
`UsdPhysicsRigidBodyAPI` schemas.

## Status

**Phase 3.5 — HdExec Integration.** The plugin now has the full
simulation-to-Hydra pipeline working: Newton physics writes transforms to
a session sublayer, the `computeSimulatedTransform` OpenExec computation
reads them back, and the generic `HdExecComputedTransformSceneIndex` scene
index filter consumes the computation to overlay transforms onto the
Hydra 2.0 data model.

### Roadmap

| Phase | Description | Status |
|-------|-------------|--------|
| 0     | Scaffolding, stubs, test assets | ✅ Done |
| 1     | Newton world lifecycle — ndWorld, stepping, gravity | ✅ Done |
| 2     | USD → Newton body mapping — shapes, mass, kinematic | ✅ Done |
| 3     | Simulation stepping, session-layer writeback, OpenExec computation | ✅ Done |
| 3.5   | HdExec scene index filter integration | ✅ Done |
| 4     | Material properties (friction, restitution) | Planned |
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
├── newtonSimulationDriver.h/.cpp   ← Session-layer simulation driver
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

### Simulation Loop

The simulation is driven by `NewtonSimulationDriver`:

1. **Initialize**: Find `PhysicsScene`, create Newton world, map all
   physics bodies, create session sublayer
2. **Step**: Advance Newton world by `dt`, read back simulated transforms
3. **Writeback**: Author `xformOp:translate` to the session sublayer for
   each dynamic body
4. **Consumption**: USDView/Hydra sees the session-layer values via normal
   USD composition; OpenExec reads them via `computeSimulatedTransform`

### Central Orchestrator

`NewtonPhysicsSystem` is a singleton that ties the world manager and
mapper together with lazy initialization and frame stepping. It provides
a simpler interface for contexts that don't need session-layer writeback.

### OpenExec Computation

`computeSimulatedTransform` is registered via
`EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdPhysicsRigidBodyAPI)`. It reads
`physics:rigidBodyEnabled` and `xformOp:translate` as inputs, returning
a `GfMatrix4d`. The actual simulation is driven by the driver — the
computation reads back the session-layer-authored values.

### HdExec Scene Index Filter (Hydra Pipeline)

The generic `HdExecComputedTransformSceneIndex` (from `pxr/imaging/hdExec/`)
consumes the `computeSimulatedTransform` computation and overlays the
resulting `GfMatrix4d` onto the Hydra `HdXformSchema` for each prim.

**Full data flow:**

```
Newton World  →  Session Layer  →  OpenExec Computation  →  HdExec Filter  →  Hydra
  (ndWorld)      (xformOp:translate   (computeSimulated      (HdXformSchema    (renderer
   step()         authored per body)   Transform reads        overlay)           reads
                                       session values)                           xform)
```

**Setting up in a viewer:**

```cpp
#include "pxr/imaging/hdExec/execComputedTransformSceneIndex.h"
#include "pxr/exec/execUsd/system.h"

// 1. Create the exec system from the stage
auto execSystem = std::make_shared<ExecUsdSystem>(stage);

// 2. Initialize the Newton simulation driver
NewtonSimulationDriver driver;
driver.Initialize(stage);

// 3. Insert the HdExec filter into the scene index chain
auto physicsFilter = HdExecComputedTransformSceneIndex::New(
    inputSceneIndex,          // upstream scene index
    stage,                    // USD stage for prim lookups
    execSystem,               // OpenExec system
    {TfToken("computeSimulatedTransform")},  // computation token
    /* resetXformStack = */ true);            // world-space transforms

// 4. Each frame: step Newton, then advance filter time
driver.StepAndWriteBack(dt);
physicsFilter->SetTime(currentTime);
```

The filter automatically discovers which prims have
`computeSimulatedTransform` available (those with `UsdPhysicsRigidBodyAPI`)
and overlays the exec-computed matrix. Prims without the computation pass
through unchanged.

### Body Mapping

`UsdToNewtonMapper` traverses the stage and creates Newton bodies:

- **Dynamic bodies**: Prims with `PhysicsRigidBodyAPI` (not kinematic).
  Created as `ndBodyDynamic` with mass and gravity callback.
- **Kinematic bodies**: Prims with `PhysicsRigidBodyAPI` where
  `physics:kinematicEnabled = true`. Created as `ndBodyKinematic`.
- **Static colliders**: Prims with `PhysicsCollisionAPI` but no
  `PhysicsRigidBodyAPI`. Created as `ndBodyKinematic` with zero velocity.

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
