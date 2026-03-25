# Newton Physics OpenExec Plugin

A proof-of-concept OpenExec plugin that integrates the
[Newton Dynamics 4](https://github.com/MADEAPPS/newton-dynamics) physics
engine with OpenUSD, enabling real-time rigid-body simulation driven by
`UsdPhysicsRigidBodyAPI` schemas.

## Status

**Phase 2 — USD → Newton Body Mapping.** The plugin now maps USD physics
prims to Newton Dynamics bodies. Supports box, sphere, and capsule shapes;
dynamic, kinematic, and static bodies; mass from `PhysicsMassAPI` (explicit
mass and density); and per-body gravity via `ndBodyNotify`.

### Roadmap

| Phase | Description | Status |
|-------|-------------|--------|
| 0     | Scaffolding, stubs, test assets | ✅ Done |
| 1     | Newton world lifecycle — ndWorld, stepping, gravity | ✅ Done |
| 2     | USD → Newton body mapping — shapes, mass, kinematic | ✅ Done |
| 3     | OpenExec computation: `computeSimulatedTransform` | Planned |
| 4     | USDView integration and interactive playback | Planned |

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
├── newtonPhysicsComputations.cpp   ← EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA
└── testenv/
    ├── testPluginLoads.cpp         ← Plugin load/registration test
    ├── testWorldCreation.cpp       ← World lifecycle tests
    ├── testBodyMapping.cpp         ← Body mapping count/kinematic tests
    ├── testShapeMapping.cpp        ← Shape type mapping tests
    ├── testMassProperties.cpp      ← Mass/density handling tests
    ├── testNewtonPhysicsPlugin/    ← Test plugin resources
    ├── fallingBox.usda             ← Single falling box
    ├── stackedBoxes.usda           ← Three stacked boxes
    ├── mixedShapes.usda            ← Sphere, box, capsule
    ├── kinematicAndDynamic.usda    ← Kinematic + dynamic interaction
    └── materialFriction.usda       ← Friction material test
```

## Architecture

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
