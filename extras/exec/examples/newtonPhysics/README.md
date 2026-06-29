# Newton Physics — Dual-Engine OpenExec Plugin

An OpenExec plugin providing real-time rigid-body simulation in UsdView,
with two GPU-accelerated physics backends:

- **Newton GPU** — XPBD solver (Disney/DeepMind/NVIDIA), in-process via Warp CUDA
- **PhysX 5** — NVIDIA PhysX via `ovphysx`, out-of-process with shared-memory IPC

Both engines drive `UsdPhysicsRigidBodyAPI` prims live through HdExec → Hydra — no baking, no session layer.

![Newton GPU physics in UsdView — live simulation + interactive grab](../../../../newton_usdview.gif)

*UsdView + Storm rendering. Newton GPU (XPBD solver, NVIDIA L40)
drives rigid body transforms live through HdExec → Hydra — no baking, no session layer.*

## Architecture

```
USD Stage
  ├─ Newton GPU (in-process)
  │    ModelBuilder.add_usd(stage) → Model (CUDA)
  │      → solver.step() → state.body_q (Warp buffer)
  │        → body_q.numpy() ─────────────────────┐
  │                                               │
  ├─ PhysX 5 (subprocess, shared memory IPC)      │
  │    ovphysx_worker.py ← subprocess.Popen       │
  │      → physx.step() → pose_binding.read()     │
  │        → shm "ovphysx_poses" (N×7 float32) ──┤
  │                                               │
  └─► PhysicsProvider.get_body_transforms() ◄─────┘
        → HdExec.RegisterTransformProvider
          → HdExecComputedTransformSceneIndex → Hydra Storm
```

The plugin presents a unified `PhysicsProvider` interface. The active
engine is selected at runtime via the Physics menu — switching engines
stops simulation, tears down the old provider, and initializes the new one.

### Newton GPU Provider

`NewtonProvider` wraps Newton's `ModelBuilder` → `Model` → `Solver` → `State`:

1. **`initialize(stage)`** — `ModelBuilder.add_usd(stage)` parses all
   `UsdPhysics` schemas (bodies, shapes, joints, materials)
2. **`step(dt)`** — `model.collide()` + `solver.step()` on GPU
3. **`get_body_transforms()`** — reads `body_q` → converts to `GfMatrix4d`

Newton runs in-process, sharing the same Python interpreter and CUDA context.

#### Solver Backends

8 GPU-accelerated solvers, all with the same `step()` interface:

| Solver | Best for |
|--------|----------|
| `SolverXPBD` | Real-time rigid bodies (default) |
| `SolverMuJoCo` | Robotics, articulations |
| `SolverFeatherstone` | Precise joint dynamics |
| `SolverSemiImplicit` | Fast, simple |
| `SolverVBD` | Deformables |
| `SolverImplicitMPM` | Soft bodies, granular |
| `SolverKamino` | Fluids |
| `SolverStyle3D` | Cloth |

Configure via [Newton USD schemas](https://github.com/newton-physics/newton-usd-schemas):

```usda
def PhysicsScene "PhysicsScene" (
    prepend apiSchemas = ["NewtonSceneAPI", "NewtonXpbdSceneAPI"]
) {
    int newton:timeStepsPerSecond = 1000
    float newton:xpbd:rigidContactRelaxation = 0.8
}
```

### PhysX 5 Provider (ovphysx subprocess)

`PhysXProvider` runs NVIDIA PhysX 5 in a **separate process** to avoid
USD dual-runtime conflicts (ovphysx bundles its own USD). Communication
uses POSIX shared memory for zero-copy, lock-free pose transfer.

#### Shared Memory IPC Layout

Two named shared-memory segments:

**`ovphysx_ctrl`** (28 bytes) — control flags + grab state:

```
byte  0:     kick flag   (parent→worker: request step)
byte  1:     done flag   (worker→parent: poses written)
byte  2:     quit flag   (parent→worker: exit)
byte  3:     (padding)
bytes 4-7:   grab_body_idx  (int32, -1 = no grab)
bytes 8-19:  grab_target    (3× float32: x, y, z)
bytes 20-23: dt             (float32)
bytes 24-27: sim_time       (float32)
```

**`ovphysx_poses`** (N × 7 × 4 bytes) — body poses:

```
N bodies × 7 float32: px, py, pz, qx, qy, qz, qw
```

#### Hot-Loop Protocol

1. Parent writes `dt` + `sim_time` to ctrl, sets `kick=1`
2. Worker spins on `kick` (100μs sleep), steps PhysX (2 substeps), writes poses
3. Worker sets `done=1`
4. Parent reads poses from numpy view over shm (zero-copy)

Grab input is written to ctrl between frames — worker applies velocity
steering before each step. Graceful shutdown via `quit` flag.

### Interactive Picking

Both engines support GPU-accelerated ray casting and spring-damper
force application for interactive body dragging:

- Newton: in-process `begin_pick()` / `update_pick()` / `release_pick()`
- PhysX: grab state written to shm ctrl segment, worker applies forces

## Installation

### Dependencies

- **[Newton](https://github.com/newton-physics/newton)** — `pip install newton` (optional, for Newton engine)
- **[NVIDIA Warp](https://github.com/NVIDIA/warp)** — installed with Newton
- **[ovphysx](https://pypi.org/project/ovphysx/)** — `pip install ovphysx` (optional, for PhysX engine)
- **NVIDIA GPU** (Maxwell+) with CUDA 12+ drivers
- **OpenUSD** with OpenExec + HdExec Python bindings

At least one of Newton or ovphysx must be installed.

### Environment Variables

```bash
# Point UsdView at the plugin's plugInfo.json
export PXR_PLUGINPATH_NAME=/path/to/newtonPhysics/usdviewPlugin:${PXR_PLUGINPATH_NAME}

# Plugin imports from the newtonPhysics package
export PYTHONPATH=/path/to/newtonPhysics:${PYTHONPATH}
```

### Launch

```bash
usdview scene.usda
```

The plugin auto-registers on startup. A **Physics** menu appears in the
viewport menu bar (next to **Lights**).

## Usage

### Physics Menu

Located in the viewport render menu bar (the bar above the viewport,
adjacent to the Lights menu):

| Item | Action |
|------|--------|
| **Simulate** | Toggle simulation on/off (checkable) |
| **Grab Mode** | Enable interactive body dragging |
| **Reset** | Stop simulation, reinitialize engine |
| **Engine →** | Submenu with radio buttons: Newton GPU / PhysX 5 |

### Grab Mode

With Grab Mode enabled, **Shift + Left-Click-Drag** on a rigid body
applies spring-damper forces that pull the body toward the cursor
projected onto the grab plane. Release to drop.

### Engine Selection

The Engine submenu shows radio buttons for each available backend.
Switching engines while simulating will stop, tear down the current
provider, and reinitialize with the new engine. The selected engine
persists for the session.

### Render with usdrecord

```bash
python newtonRecord.py \
  --camera /World/DemoCamera \
  --frames 0:120 \
  --renderer GL \
  scene.usda output/frame.####.png
```

### Python API

```python
from pxr import HdExec, Sdf, Gf

engine = NewtonEngine(device="cuda:0")
engine.initialize(stage, solver_name="xpbd")

def provider(prim_path, time_seconds):
    engine.advance_to_time(time_seconds)
    return engine.get_transform(prim_path)

HdExec.RegisterTransformProvider("newtonGPU", provider)
```

## Performance

Benchmarked on NVIDIA L40 (Ada Lovelace, 48GB):

| Scene | Engine | FPS | Notes |
|-------|--------|-----|-------|
| 1000 rigid boxes | PhysX 5 | ~28 fps | 2 substeps, shm IPC |
| 1000 rigid boxes | Newton GPU | ~24 fps | XPBD, in-process |

The PhysX subprocess architecture decouples physics from the render
thread — the parent never blocks on `step()`. Pose reads are a numpy
view over shared memory (zero-copy). The 100μs spin-wait in the worker
keeps latency sub-frame while avoiding busy-wait CPU burn.

Key factors:
- **2 substeps per frame** — stability/speed tradeoff for stacking
- **Shared memory IPC** — no serialization, no pipe overhead on hot path
- **Async stepping** — parent fires kick and continues; reads last-available poses (≤1 frame latency)

## Directory Structure

```
newtonPhysics/
├── README.md
├── ovphysx_worker.py            PhysX 5 subprocess worker (shm hot loop)
├── python/
│   ├── __init__.py              Newton GPU integration package
│   ├── engine.py                NewtonEngine wrapper
│   ├── provider.py              TransformProvider registration
│   └── newtonRecord.py          usdrecord with Newton GPU
├── usdviewPlugin/
│   ├── __init__.py              UsdView plugin (dual-engine, Physics menu)
│   └── plugInfo.json            UsdView plugin registration
└── testenv/
    ├── fallingBox.usda           Single falling box
    ├── demoScene.usda            Camera + scene for rendering
    ├── stackedBoxes.usda         Three stacked boxes
    ├── thousand_boxes.usda       1000 bodies benchmark scene
    ├── mixedShapes.usda          Sphere, box, capsule
    ├── kinematicAndDynamic.usda  Kinematic + dynamic interaction
    └── materialFriction.usda     Friction material test
```

## License

Licensed under the terms set forth in the LICENSE.txt file available at
https://openusd.org/license.
