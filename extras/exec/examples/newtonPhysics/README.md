# Newton Physics OpenExec Plugin

A proof-of-concept OpenExec plugin that integrates the
[Newton Dynamics 4](https://github.com/MADEAPPS/newton-dynamics) physics
engine with OpenUSD, enabling real-time rigid-body simulation driven by
`UsdPhysicsRigidBodyAPI` schemas.

![Falling box demo — a rigid body falls under gravity and collides with a ground plane](demo.gif)

*Rendered by `usdrecord` + Storm. Newton Dynamics 4 drives the rigid body transform live through the OpenExec → HdExec → Hydra pipeline — no baking, no session layer.*

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
| 5     | Demo scene, usdrecord rendering | ✅ Done |

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
    ├── materialFriction.usda              ← Friction material test
    └── demoScene.usda                     ← Camera + scene for usdrecord GIF
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

## Lessons Learned

Hard-won notes from building this POC. Most of these aren't documented
anywhere — they fell out of debugging the full Newton → OpenExec → Hydra
pipeline end-to-end.

### Newton Dynamics 4

- **`CollisionUpdate()` before `Update()`**: Newton's broadphase and
  narrowphase collision detection runs in `CollisionUpdate(dt)`, which
  must be called *before* `Update(dt)` (the solver step). Without it,
  the solver sees zero contact pairs and bodies pass through each other.
  The step sequence is: `CollisionUpdate(dt)` → `Update(dt)` → `Sync()`.

- **Gravity is per-body, not per-world**: Newton 4 doesn't store gravity
  on `ndWorld`. You apply it per-body via a custom `ndBodyNotify` subclass
  that implements `OnApplyExternalForce()` with `F = mass * gravity`.

- **Shapes are shared, bodies own the matrix**: Create `ndShapeInstance`
  wrappers around the base `ndShape`. The body's `SetMatrix()` controls
  world-space placement; the shape defines local geometry only.

### OpenExec Integration

- **`computePath` is the key builtin**: The `EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA`
  callback doesn't receive the prim directly. Use `ExecBuiltinComputations->computePath`
  as an input to get the `SdfPath` of the prim being evaluated.

- **Lazy initialization from computation callbacks**: The first time a
  computation fires, the physics world may not exist yet. The callback
  needs to lazy-init from the global stage (via `SetGlobalStage()` /
  `GetGlobalStage()`) rather than assuming setup happened elsewhere.

- **`BuildRequest().IsValid()` lies**: The exec system's `BuildRequest()`
  can return `IsValid() = true` for prims that don't actually have the
  relevant API schema. Always guard with `prim.HasAPI<T>()` before
  querying exec, or you'll get compilation failures for prims like
  cameras and static colliders that don't have `UsdPhysicsRigidBodyAPI`.

### Hydra / Scene Index

- **Xform overlay must be keyed as `"xform"`**: `HdXformSchema::Builder().Build()`
  returns the inner container (matrix + resetXformStack). When overlaying
  onto prim data, you must wrap it under the `"xform"` key with
  `HdRetainedContainerDataSource::New(HdXformSchemaTokens->xform, ...)`.
  Without this, Storm silently ignores your transform.

- **`UniversalSet` for cache eviction**: When dirtying computed prims on
  time change, use `HdDataSourceLocatorSet::UniversalSet()` rather than
  just `HdXformSchema::GetDefaultLocator()`. The `CachingSceneIndex`
  upstream of Storm only evicts its cache on container-level dirty
  signals — locator-specific dirty can be silently dropped.

- **`SetGlobalStage` timing matters**: The `HdExec` scene index filter
  auto-bootstraps when it first sees a stage. In `usdrecord`, the stage
  arrives via `UsdNotice::StageContentsChanged` during population. If
  you miss that notice, the filter spins on `_TryBootstrap: no global
  stage yet` until something else sets it. The fix was listening for
  `_PrimsAdded` as a second bootstrap trigger.

### plugInfo.json

- **No `Type` field in the computation plugin**: Unlike Hydra renderer
  plugins, an OpenExec computation plugin registered with
  `EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA` must *not* have a `Type`
  field in `plugInfo.json` — it conflicts with the exec registration
  machinery. Only `LibraryPath` and `Info` are needed.

- **`loadWithRenderer: GL`** for scene index plugins: If your scene
  index plugin needs to be active during `usdrecord` (which defaults
  to the GL/Storm renderer), set `"loadWithRenderer": "GL"` in the
  hdExec `plugInfo.json`. Without it, the plugin never loads in
  headless rendering contexts.

### General

- **Session layer is a dead end for physics**: The initial approach
  (Phase 3 v1) wrote simulated transforms into a session sublayer each
  frame. This technically worked but was architecturally wrong — it
  fights the exec/Hydra data model where computations *are* the
  transport. The rewrite to direct computation queries was cleaner,
  faster, and composable.

- **Test at every layer**: Unit tests at the Newton wrapper level
  (`testWorldCreation`, `testBodyMapping`) caught issues long before
  the full pipeline was wired. The `testExecTransformWithPhysics`
  integration test proved the pipeline without needing a GPU. Only
  `usdrecord` needed GL.

## License

Licensed under the terms set forth in the LICENSE.txt file available at
https://openusd.org/license.
