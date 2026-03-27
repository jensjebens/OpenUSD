# Units Resolution Demo

End-to-end demonstration of unit-aware value resolution through the Hydra 2 pipeline.

## What This Shows

A meter-scale factory stage references assets authored in different unit systems:

| Asset | Source Units | Position (authored) | Position (corrected) |
|-------|-------------|--------------------|--------------------|
| Blue reference cube | meters | (0, 0.5, 0) | (0, 0.5, 0) — no correction |
| Red robot arm | centimeters | (100, 0, 50) cm | (1, 0, 0.5) m |
| Green M10 bolt | millimeters | (10000, 5500, 0) mm | (10, 5.5, 0) m |

## Architecture

```
UsdGeomMetricsAPI (metrics:metersPerUnit on prim)
  → execMetricsUnits (computeUnitAwareLocalToWorldTransform)
    → HdExecComputedTransformSceneIndex (auto-bootstrap)
      → HdFlatteningSceneIndex
        → Storm render delegate
```

## Storm Render

![Factory Demo](factory_demo_render.png)

## Running in usdview

```bash
export USD=/path/to/usd-install
export PATH=$USD/bin:$PATH
export PYTHONPATH=$USD/lib/python:$PYTHONPATH
export LD_LIBRARY_PATH=$USD/lib:$LD_LIBRARY_PATH

cd extras/exec/examples/unitsDemo
usdview factory_demo.usda
```

## Files

- `factory_demo.usda` — Meter-scale factory stage
- `robot_arm_cm.usda` — Centimeter-scale robot arm with `GeomMetricsAPI`
- `bolt_mm.usda` — Millimeter-scale M10 bolt with `GeomMetricsAPI`
- `factory_demo_render.png` — Storm render output

## Related

- [Units and Scale proposal](https://github.com/jensjebens/OpenUSD-proposals/blob/jjebens/units-and-scale/proposals/units_and_scale/README.md)
- [MetricsAPI core](../../usd/metricsApiCore/README.md) — UsdGeomMetricsAPI schemas
- [Exec computation](../metricsUnits/) — OpenExec unit-aware transform computation
- [HdExec scene filter](../../../../pxr/imaging/hdExec/README.md) — Generic exec→Hydra bridge
