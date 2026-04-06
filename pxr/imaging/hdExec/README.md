# HdExec: OpenExec Scene Index Integration

Generic Hydra 2.0 scene index filter that bridges OpenExec computed values
into the rendering pipeline.

![Newton GPU ant simulation in UsdView via HdExec](../../../../newton_usdview.gif)

*Newton GPU physics (9-body ant, 8 revolute joints) running live in UsdView
through the HdExec → Hydra 2.0 → Storm pipeline.*

## HdExecComputedTransformSceneIndex

A `HdSingleInputFilteringSceneIndexBase` that overlays `HdXformSchema` on
prims that have registered OpenExec transform computations.

### Usage

```cpp
auto execSystem = std::make_shared<ExecUsdSystem>(stage);
auto filter = HdExecComputedTransformSceneIndex::New(
    inputSceneIndex,
    stage,
    execSystem,
    {TfToken("computeSimulatedTransform")},
    /* resetXformStack = */ true);
```

### How It Works

1. For each prim in the scene, the filter checks if any of the configured
   computation tokens produce a valid `ExecUsdRequest` for that prim.
2. If a valid computation exists, the filter overlays an `HdXformSchema`
   data source that evaluates the exec computation on demand.
3. The `SetTime()` method advances the exec system and sends dirty
   notifications for all prims with active computations.

### Supported Computations

Any computation registered via `EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA` that
outputs `GfMatrix4d` can be consumed by this filter. Examples:
- `computeLocalToWorldTransform` (from execGeom, for UsdGeomXformable)
- `computeSimulatedTransform` (from Newton physics POC)

### Scene Index Plugin

The `HdExec_ComputedTransformSceneIndexPlugin` registers with the Hydra
scene index plugin system for all renderers. Currently it acts as a no-op
pass-through since instantiation requires an `ExecUsdSystem` + stage.
Applications create the filter explicitly when setting up their exec system.
