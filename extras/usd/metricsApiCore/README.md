# Metrics API Core

Foundation for unit-aware USD: prim-level unit declarations and a plugin-discoverable dimensional exponent registry.

This branch provides the shared infrastructure that both the [Units API POC](https://github.com/jensjebens/OpenUSD/tree/jjebens/units-api-poc/extras/units_api) and the [evaluation-time unit resolution](https://github.com/jensjebens/OpenUSD/tree/jjebens/units-aware-value-resolution) work depend on.

## What's Here

### 1. UsdGeomMetricsAPI / UsdPhysicsMetricsAPI (Applied Schemas)

Real USD applied API schemas declaring the unit context for a prim's subtree:

```
class "GeomMetricsAPI" (inherits = </APISchemaBase>)
{
    double metrics:metersPerUnit    # 0.001 = mm, 0.01 = cm, 1.0 = m
    token metrics:upAxis            # "Y" or "Z"
}

class "PhysicsMetricsAPI" (inherits = </APISchemaBase>)
{
    double metrics:kilogramsPerUnit  # 1.0 = kg, 0.001 = g
}
```

Values inherit down the hierarchy. Apply to a root prim and every descendant resolves the correct unit context via ancestor walk.

Aligns with the [MetricsAPI proposal (PR #45)](https://github.com/PixarAnimationStudios/OpenUSD-proposals/pull/45).

### 2. Dimensional Exponent Registry (C++ + plugInfo.json)

Maps attribute names to L/M/T exponents via plugin discovery:

```cpp
auto dim = UsdMetricsDimensionalRegistry::GetInstance().GetDimension("physics:density");
// dim->L == -3, dim->M == 1, dim->T == 0
double factor = dim->ComputeConversionFactor(sourceMpu, targetMpu, sourceKpu, targetKpu);
```

Exponents are declared in `plugInfo.json`:

```json
"DimensionalExponents": {
    "physics:density":          { "L": -3, "M": 1 },
    "physics:velocity":         { "L": 1, "T": -1 },
    "physics:gravityMagnitude": { "L": 1, "T": -2 },
    "xformOp:translate":        { "L": 1 },
    "focusDistance":             { "L": 1 }
}
```

Each schema domain ships its own entries. Third parties register via their own `plugInfo.json`. No hardcoded registry.

## Branch Dependency Graph

```
jjebens/metrics-api-core          ← this branch (MetricsAPI + registry)
  ↑ rebased onto by
jjebens/units-api-poc              ← UnitsLens, MetricsAssembler, bake_to_units
  ↑ rebased onto by
jjebens/units-aware-value-resolution ← OpenExec computation + Hydra integration
```

## Building

```bash
cd OpenUSD
mkdir build && cd build
cmake -DPXR_BUILD_EXAMPLES=ON ..
make -j$(nproc)
```

The schema needs `usdGenSchema` to generate C++ bindings. Run:

```bash
cd extras/usd/metricsApiCore
usdGenSchema schema.usda .
```

## Related

- [Units & Scale proposal](https://github.com/jensjebens/OpenUSD-proposals/blob/jjebens/units-and-scale/proposals/units_and_scale/README.md)
- [MetricsAPI proposal (PR #45)](https://github.com/PixarAnimationStudios/OpenUSD-proposals/pull/45)
- [Units API POC (Python)](https://github.com/jensjebens/OpenUSD/tree/jjebens/units-api-poc/extras/units_api)
- [Kit extension](https://github.com/jensjebens/omni-units-api)
