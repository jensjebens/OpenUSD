# End-to-End Results: MetricsAPI → OpenExec → Hydra

**Date:** 2026-03-26  
**USD Version:** 26.3 (built from `jjebens/metrics-api-core` branch)  
**Machine:** 128-core x86_64, NVIDIA L40, Ubuntu 22.04

## Summary

Unit-aware value resolution works end-to-end through the full Hydra 2 pipeline:

```
UsdGeomMetricsAPI (prim-level schema, metrics:metersPerUnit)
  → execMetricsUnits (OpenExec computation: computeUnitAwareLocalToWorldTransform)
    → HdExecComputedTransformSceneIndex (generic exec→Hydra bridge)
      → HdFlatteningSceneIndex (accumulates corrected local transforms into world)
        → Render delegate receives correct world-space transforms
```

### Test Results

| Prim | Raw Local | MetricsAPI | Corrected Local | Parent | World (flattened) | Status |
|------|-----------|------------|-----------------|--------|-------------------|--------|
| `/Factory/CmRobot` | `(100, 0, 50)` | `mPU=0.01` | `(1, 0, 0.5)` | `(10, 0, 0)` | **`(11, 0, 0.5)`** | ✅ |
| `/Factory/MeterTable` | `(5, 0, 0)` | none | `(5, 0, 0)` | `(10, 0, 0)` | **`(15, 0, 0)`** | ✅ |

### Additional Standalone Tests (execMetricsUnits)

| Prim | Raw | mPU | Corrected | Status |
|------|-----|-----|-----------|--------|
| `CmBox` | `(100, 200, 300)` | 0.01 | `(1, 2, 3)` | ✅ |
| `MBox` | `(5, 10, 15)` | 1.0 | `(5, 10, 15)` | ✅ |
| `NoMetrics` | `(7, 8, 9)` | none | computation not found | ✅ |

## Architecture

### Three independent components, three branches

#### 1. `jjebens/metrics-api-core` — UsdGeomMetricsAPI + DimensionalRegistry

Real USD applied API schemas declaring prim-level unit context:

```usda
def Xform "CmRobot" (apiSchemas = ["GeomMetricsAPI"]) {
    double metrics:metersPerUnit = 0.01
    token metrics:upAxis = "Y"
}
```

- `UsdGeomMetricsAPI` — `metrics:metersPerUnit`, `metrics:upAxis`
- `UsdPhysicsMetricsAPI` — `metrics:kilogramsPerUnit`
- `UsdMetricsDimensionalRegistry` — plugin-discoverable L/M/T exponents
- `UsdMetricsGetEffectiveMetersPerUnit()` — ancestor walk resolution
- Full C++ implementation with Python bindings and tests

Values inherit down the hierarchy via ancestor walk, falling back to
stage-level layer metadata, then USD defaults. Aligns with MetricsAPI
direction proposed in PR #45.

#### 2. `execMetricsUnits` — OpenExec computation plugin

Registers `computeUnitAwareLocalToWorldTransform` on `UsdMetricsGeomMetricsAPI`:

```cpp
EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA(UsdMetricsGeomMetricsAPI)
{
    self.PrimComputation(computeUnitAwareLocalToWorldTransform)
        .Callback<GfMatrix4d>(&_ComputeUnitAwareL2W)
        .Inputs(
            Computation<GfMatrix4d>(computeLocalToWorldTransform),
            AttributeValue<double>(metrics:metersPerUnit)
        );
}
```

The computation:
1. Reads `computeLocalToWorldTransform` from `execGeom` (cross-schema, same prim)
2. Reads `metrics:metersPerUnit` from `GeomMetricsAPI` attribute
3. Scales translation component by `primMPU / stageMPU`
4. Returns corrected `GfMatrix4d`

Only runs on prims with `GeomMetricsAPI` applied. No manual attribute authoring needed.

#### 3. `HdExecComputedTransformSceneIndex` — Generic exec→Hydra bridge

From `feature/exec-hydra-scene-filter`. A `HdSingleInputFilteringSceneIndexBase` that:

1. Checks each prim for applied API schemas that have exec computations
2. Evaluates the computation via `ExecUsdSystem`
3. Overlays the result as an `HdXformSchema` data source
4. Downstream `HdFlatteningSceneIndex` accumulates corrected local transforms

```cpp
auto filter = HdExecComputedTransformSceneIndex::New(
    inputSceneIndex, stage, execSystem,
    {TfToken("computeUnitAwareLocalToWorldTransform")},
    /* resetXformStack = */ false);
```

Generic — works with any OpenExec computation that produces `GfMatrix4d`.

## Key Findings

### 1. MetricsAPI via real USD schemas works
Prim-level `metrics:metersPerUnit` as an `AttributeValue<double>` input to
OpenExec is clean and efficient. No `customData` hacks, no `PrimIndex` walks,
no layer metadata workarounds.

### 2. OpenExec caching handles everything
No custom caching needed in the scene index. `ExecUsdSystem` caches computed
values and invalidates on attribute changes automatically.

### 3. The xform overlay must be keyed correctly
`HdExecComputedTransformSceneIndex` originally overlaid the xform container
directly on the prim data source. It must be wrapped under the `"xform"` key:
```cpp
HdRetainedContainerDataSource::New(
    HdXformSchemaTokens->xform,    // key
    xformContainerDataSource)       // value: {matrix, resetXformStack}
```
Without this, `HdFlatteningSceneIndex` reads the original (uncorrected) xform.

### 4. Exec plugin schema registration
`EXEC_REGISTER_COMPUTATIONS_FOR_SCHEMA` stringifies its argument and calls
`TfType::FindByName()`. Must use the full TfType name
(`UsdMetricsGeomMetricsAPI`), not the schema identifier (`GeomMetricsAPI`).

The schema's `plugInfo.json` must include:
```json
"Exec": {
    "Schemas": {
        "UsdMetricsGeomMetricsAPI": {
            "allowsPluginComputations": true
        }
    }
}
```

### 5. execGeom xformOp:transform bug
`execGeom/xformable.cpp` had `xformOps:transform` (plural 's') instead of
`xformOp:transform`. Fixed by Pixar on `dev` (cc5ca4d812, 2026-03-23) and
independently by us. Our fork carries the same fix.

## Bugs Found and Fixed

### In `HdExecComputedTransformSceneIndex` (from `feature/exec-hydra-scene-filter`)

1. **Xform overlay key missing** — `_CreateExecXformDataSource` returned the
   xform container directly, but the overlay needed it wrapped under
   `HdXformSchemaTokens->xform`. Without this, the flattening scene index
   read the original data source.

2. **`_HasExecComputation` unreliable** — Used `BuildRequest` as a probe,
   which had state-dependent behavior. Replaced with applied schema check
   (`prim.GetAppliedSchemas()` for `"GeomMetricsAPI"`).

### In `execGeom` (Pixar code, fixed upstream)

3. **`xformOps:transform` → `xformOp:transform`** — Wrong attribute name
   token caused `computeLocalToWorldTransform` to return identity for any
   standard `UsdGeomXformable`-authored transform.

## Files

### execMetricsUnits/ (new OpenExec computation)
- `execMetricsUnits.cpp` — computation implementation
- `resources/plugInfo.json` — exec schema registration
- `CMakeLists.txt` — standalone build
- `testExecMetricsUnits.cpp` — standalone exec test (3 cases)

### hdExec_src/ (built from `feature/exec-hydra-scene-filter`, with fixes)
- `execComputedTransformSceneIndex.cpp` — scene index filter (fixed overlay + schema check)
- `execComputedTransformSceneIndex.h` — public API
- `sceneIndexPlugin.cpp/.h` — Hydra plugin registration
- `api.h`, `plugInfo.json`

### Test files
- `testEndToEnd.cpp` — full Hydra pipeline test via `UsdImagingCreateSceneIndices`
- `diagExec.cpp` — diagnostic tool for exec system debugging

## How to Reproduce

```bash
# Build USD from jjebens/metrics-api-core
python3 OpenUSD/build_scripts/build_usd.py --no-examples --no-tutorials --no-docs usd-install

# Build metricsApiCore
cd usd-install/build/OpenUSD
cmake . -DPXR_BUILD_EXAMPLES=ON
cmake --build . --target usdMetricsApi -j$(nproc)

# Build execMetricsUnits
cd execMetricsUnits && mkdir build && cd build
cmake .. -Dpxr_DIR=../../usd-install -DCMAKE_PREFIX_PATH=../../usd-install
make -j$(nproc)

# Build hdExec
cd hdExec_src && mkdir build && cd build
cmake .. -Dpxr_DIR=../../usd-install -DCMAKE_PREFIX_PATH=../../usd-install
make -j$(nproc)

# Run tests
export LD_LIBRARY_PATH=usd-install/lib:hdExec_src/build:$LD_LIBRARY_PATH
export PXR_PLUGINPATH_NAME=execMetricsUnits/build

./execMetricsUnits/build/testExecMetricsUnits   # standalone exec test
./testEndToEnd                                    # full Hydra pipeline test
```

## What's Next

1. **Push `execMetricsUnits` to `jjebens/units-aware-value-resolution`** — rebase onto `metrics-api-core`
2. **Push `HdExecComputedTransformSceneIndex` fixes** back to `feature/exec-hydra-scene-filter`
3. **Stage-level mPU via OpenExec** — currently hardcoded to 1.0; should read from stage
4. **upAxis correction** — add Y↔Z rotation
5. **Camera + light + physics attributes** — extend via dimensional registry
6. **usdrecord visual test** — once auto-loading plugin gets stage access
7. **Update Appendix D** in proposal with new architecture
