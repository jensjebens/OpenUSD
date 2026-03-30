# Dynamic Spatial Ownership — OpenExec Computation

OpenExec computation plugin for Dynamic Spatial Ownership (DSO).
Objects follow their carriers via a relationship-driven computation
instead of hierarchy changes, preserving stable namespace paths.

## How It Works

`computeEffectiveWorldTransform` is registered on `DsoDynamicOwnershipAPI`:

```cpp
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(DsoDynamicOwnershipAPI)
{
    self.PrimComputation(computeEffectiveWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeEffectiveWorldTransform)
        .Inputs(
            AttributeValue<TfToken>(ownershipMode),
            AttributeValue<GfMatrix4d>(localOffset),
            AttributeValue<int>(activeCarrierIndex),
            Relationship(carriers)
                .TargetedObjects<GfMatrix4d>(computeLocalToWorldTransform)
                .InputName(carrierWorldTransforms),
            // ... fallback hierarchical inputs
        );
}
```

When `ownershipMode == "carrier"`:
- Reads `activeCarrierIndex` (time-sampled) to select from `carriers` relationship
- Reads the active carrier's `computeLocalToWorldTransform` via cross-prim access
- Computes: `localOffset * carrierWorldTransform`
- No relationship authoring at runtime, no OpenExec recompilation

When `ownershipMode == "authored"`:
- Falls back to standard hierarchical transform

When `ownershipMode == "physics"`:
- The HdExec filter's `TransformProvider` registry handles this
- Physics engines (Newton, PhysX) register providers that bypass exec cache

## Schema

`DsoDynamicOwnershipAPI` (applied, codeless via `skipCodeGeneration=true`):

```usda
rel dynamicOwnership:carriers          # all potential carriers, pre-authored
int dynamicOwnership:activeCarrierIndex  # time-sampled, selects active carrier
matrix4d dynamicOwnership:localOffset    # time-sampled, keyframed at switch points
token dynamicOwnership:ownershipMode     # "carrier" | "physics" | "authored"
```

Schema resources are in `extras/usd/dso/`.

## Hydra Integration

This plugin uses the **shared** `HdExecComputedTransformSceneIndex` from
`pxr/imaging/hdExec/` — the same filter used by the Units Resolution and
Newton Physics plugins. No bespoke scene index filter is needed.

The computation token `computeEffectiveWorldTransform` is registered in
`hdExec/sceneIndexPlugin.cpp` alongside `computeSimulatedTransform` and
`computeUnitAwareLocalToWorldTransform`.

## Building

```bash
cmake -DPXR_DIR=/path/to/usd/install ..
make
```

## Testing

```bash
# Standalone computation test
./testDsoComputation test_stage.usda

# HdExec integration test (proves shared filter works)
./testDsoHdExec test_stage.usda
```

## Related

- [HdExec README](../../../../pxr/imaging/hdExec/README.md) — shared scene index filter
- [DSO proposal](https://github.com/jensjebens/OpenUSD-proposals/blob/jjebens/dynamic-spatial-ownership/proposals/dynamic_spatial_ownership/README.md)
- [DSO POC repo](https://github.com/jensjebens/DSO_POC) — full POC with Kit extension, scale tests, and more
