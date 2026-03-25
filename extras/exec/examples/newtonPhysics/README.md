# Newton Physics OpenExec Plugin

A proof-of-concept OpenExec plugin that integrates the
[Newton Dynamics 4](https://github.com/MADEAPPS/newton-dynamics) physics
engine with OpenUSD, enabling real-time rigid-body simulation driven by
`UsdPhysicsRigidBodyAPI` schemas.

## Status

**Phase 0 — Scaffolding.** The plugin structure, build files, stub
implementations, and test assets are in place. No actual physics simulation
occurs yet.

### Roadmap

| Phase | Description |
|-------|-------------|
| 0     | Scaffolding, stubs, test assets (this phase) |
| 1     | Newton Dynamics integration — create ndWorld, map prims to bodies |
| 2     | Type conversions, collision shapes, simulation stepping |
| 3     | OpenExec computation: `computeSimulatedTransform` |
| 4     | USDView integration and interactive playback |

## Directory Structure

```
newtonPhysics/
├── CMakeLists.txt                  ← Build configuration
├── cmake/
│   └── FindNewtonDynamics.cmake    ← CMake find module for Newton 4
├── plugInfo.json                   ← Exec plugin registration
├── README.md                       ← This file
├── newtonTypes.h                   ← Newton↔USD type conversions
├── newtonWorldManager.h/.cpp       ← ndWorld lifecycle management
├── usdToNewtonMapper.h/.cpp        ← USD physics prim → Newton body mapping
├── newtonPhysicsComputations.cpp   ← EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA
└── testenv/
    ├── testPluginLoads.cpp         ← Plugin load/registration test
    ├── testNewtonPhysicsPlugin/    ← Test plugin resources
    ├── fallingBox.usda             ← Single falling box
    ├── stackedBoxes.usda           ← Three stacked boxes
    ├── mixedShapes.usda            ← Sphere, box, capsule
    ├── kinematicAndDynamic.usda    ← Kinematic + dynamic interaction
    └── materialFriction.usda       ← Friction material test
```

## Building

### Without Newton (stub mode)

The plugin skeleton compiles without Newton Dynamics installed.
Newton-dependent code is guarded by `#ifdef NEWTON_DYNAMICS_FOUND`.

Build as part of the OpenUSD build:

```bash
cd OpenUSD
python build_scripts/build_usd.py /path/to/build
```

### With Newton Dynamics

Set `NEWTON_DYNAMICS_ROOT` to your Newton 4 install prefix:

```bash
cmake -DNEWTON_DYNAMICS_ROOT=/path/to/newton4 ...
```

Or enable FetchContent to download Newton automatically:

```bash
cmake -DNEWTON_FETCH_CONTENT=ON ...
```

## Dependencies

- **OpenUSD** (with OpenExec — `exec`, `execUsd`, `vdf`)
- **UsdPhysics** schemas (`usdPhysics`, `usdGeom`)
- **Newton Dynamics 4** (optional for Phase 0)

## License

Licensed under the terms set forth in the LICENSE.txt file available at
https://openusd.org/license.
