# Newton × OpenExec × UsdView — Integration Findings

## Summary

| Date | Finding | Severity | Status | Commit |
|------|---------|----------|--------|--------|
| 2026-03-30 | UsdView crash: Python TransformProvider called from TBB threads (GIL deadlock → `_Py_FatalError_TstateNULL`) | Critical | Fixed | `eea778e77` |
| 2026-03-30 | ctypes GIL release crashes under PySide6 signal dispatch (SIGSEGV in `PyErr_Occurred`) | Critical | Fixed — replaced with C++ cache | `6d4c5205f` |
| 2026-03-30 | HdExec `AdvanceGlobalTime` doesn't dirty prims without exec computations → transforms not re-pulled | Major | Fixed | `6d4c5205f` |
| 2026-03-30 | Duplicate scene index plugin registration ("" + "GL") → two filter instances in Hydra chain | Minor | Fixed | `eea778e77` |
| 2026-03-30 | Physics Reset doesn't restore original transforms (Hydra cache stale after `ClearAllCachedTransforms`) | Major | Fixed | `77bfd9529` |
| 2026-03-31 | Newton GPU `add_usd()` crashes in `LoadUsdPhysicsFromRange` when sharing UsdStage with Hydra/ExecUsd | Critical | Fixed — use separate stage | `a0a92a042` |
| 2026-03-31 | Newton GPU auto-start from `threading.Thread` → SEGV (USD stage not thread-safe for physics parsing) | Major | Fixed — use `QTimer.singleShot` | N/A (autostart script) |
| 2026-03-31 | xdotool synthetic events don't trigger Qt popup menu actions | Minor | Won't fix — Qt limitation | N/A |

## Architecture Decisions

| Decision | Rationale | Alternatives Considered |
|----------|-----------|------------------------|
| Static C++ transform cache (`SetCachedTransforms`) instead of Python callback | TBB threads can't safely call Python (GIL). Cache avoids Python entirely on the render path. | (A) ctypes GIL release — crashed under PySide6. (D) C++ shim — more complex, same result. |
| Single path through HdExec (no direct stage writes) | Proves OpenExec as the physics integration layer. Composable with other computations (units, etc). Renderer-agnostic. | (B) Direct stage writes — bypasses OpenExec, doesn't prove architecture. (E) GPU buffer sharing — ties to specific renderer, breaks composability. |
| Separate `Usd.Stage.Open()` for Newton parsing | Shared stage crashes when Hydra is concurrently accessing. Separate stage reads same layer data safely. | Locking — would block Hydra. Deferred init — complex. |
| `shared_ptr` swap for cache reads | Lock-free reads on TBB threads during Hydra's parallel rprim sync. Writers (main thread) take a mutex to publish new map. | `std::mutex` on every read — too much contention. `atomic` — can't atomically swap a map. |

## Performance Observations

| Scene | Bodies | Shapes | Device | Init Time | Notes |
|-------|--------|--------|--------|-----------|-------|
| `fallingBox.usda` | 1 | 2 | L40 CUDA | <1s | Basic proof — box falls, collides with ground |
| `multiBody.usda` | 45 | 52 | L40 CUDA | ~2s | Boxes, spheres, capsules, shelves. All primitive shapes. |
| `workbench_settled_test.usda` | 235 | 1411 | L40 CUDA | ~90s | ALAB workbench. CoACD convex decomposition dominates init time. |

## Pipeline Chain (verified working)

```
Newton GPU (Python/CUDA)
  → body_q.numpy() (GPU→CPU, one bulk transfer per frame)
    → Build GfMatrix4d per body (Python, main thread)
      → HdExec.SetCachedTransforms([(path, mat), ...]) (Python→C++)
        → HdExec.AdvanceGlobalTime(timeCode) (dirties prims)
          → stageView.updateGL() (triggers Hydra render)
            → HdExecComputedTransformSceneIndex::GetPrim()
              → GetCachedTransform() (pure C++, no GIL)
                → HdXformSchema overlay
                  → Storm / any Hydra renderer
```

## Open Issues

| Issue | Impact | Notes |
|-------|--------|-------|
| Newton GPU axis convention — `body_q` positions appear Z-up in logs but render correctly | Low | Likely internal coordinate system; quaternion handles mapping. Need to verify with rotated bodies. |
| 90s init time for ALAB workbench | Medium | CoACD convex decomposition runs per-mesh at load time. Could pre-cache decompositions. |
| Grab mode untested interactively | Low | Code reviewed, structurally sound. Needs manual right-click-drag testing in UsdView. |
| `resetXformStack` per-schema — physics uses `true` (world-space) | Info | Correct for rigid body transforms. Units computation uses `false` (local-space). |

## Files

| File | Purpose |
|------|---------|
| `extras/exec/examples/newtonPhysics/usdviewPlugin/__init__.py` | UsdView plugin — Physics menu, QTimer sim loop, SetCachedTransforms |
| `extras/exec/examples/newtonPhysics/python/engine.py` | NewtonEngine wrapper (used by provider.py and newtonRecord.py) |
| `extras/exec/examples/newtonPhysics/python/provider.py` | TransformProvider registration (legacy — plugin now uses cache directly) |
| `pxr/imaging/hdExec/execComputedTransformSceneIndex.h` | Scene index filter header — cache API, provider API |
| `pxr/imaging/hdExec/execComputedTransformSceneIndex.cpp` | Scene index filter impl — cache, providers, auto-bootstrap, dirtying |
| `pxr/imaging/hdExec/wrapExecComputedTransformSceneIndex.cpp` | Python bindings for cache + provider APIs |
| `pxr/imaging/hdExec/sceneIndexPlugin.cpp` | Hydra plugin registration (auto-bootstrap) |
| `extras/exec/examples/newtonPhysics/testenv/fallingBox.usda` | 1-body test scene |
| `extras/exec/examples/newtonPhysics/testenv/multiBody.usda` | 45-body test scene |
| `extras/exec/examples/newtonPhysics/testenv/demoScene.usda` | Demo scene for GIF rendering |

## Branch: `feature/newton-gpu-integration` on `jensjebens/OpenUSD`
