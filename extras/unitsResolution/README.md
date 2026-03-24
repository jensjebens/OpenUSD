# Units-Aware Value Resolution POC

Proof-of-concept implementation exploring evaluation-time unit resolution
for OpenUSD, as described in the
[Units and Scale in Composed Scenes](https://github.com/jensjebens/OpenUSD-proposals/blob/jjebens/units-and-scale/proposals/units_and_scale/README.md)
proposal.

## Contents

- `src/units_resolver.py` — Python POC: PrimIndex-based unit detection
  and corrective scaling for transforms and physics attributes
- `tests/test_units_resolution.py` — 36 tests across 5 categories
- `testenv/` — Test assets (.usda) with various unit configurations
- `docs/appendix_c_implementation_exploration.md` — Proposal appendix

## C++ OpenExec Plugin

See `extras/exec/examples/unitsResolution/` for the C++ OpenExec
computation that demonstrates unit-aware transform resolution within
USD's computation framework.

## Running the Python tests

```bash
export PYTHONPATH="/path/to/usd/lib/python:$PYTHONPATH"
cd extras/unitsResolution
python3 -m pytest tests/ -v
```

## Key Findings

1. Evaluation-time unit resolution is feasible via OpenExec
2. MetricsAPI (prim-level unit metadata, PR #45) is a hard prerequisite
   for OpenExec integration — layer metadata is invisible to VdfContext
3. Performance overhead is negligible at production scale (~0-5% at 10K prims)
4. Composition is completely unaffected
5. Physics attributes require dimensional analysis (length/mass exponents)

See `docs/appendix_c_implementation_exploration.md` for the full writeup.
