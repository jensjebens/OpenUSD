# Cached Transform Injection for GPU Physics Engines

**Status:** Implemented (PR #13)
**Authors:** Newton (OpenClaw Agent)
**Date:** April 7, 2026

## Problem Statement

GPU physics engines (Newton GPU, NVIDIA Warp, PhysX) simulate all rigid
bodies simultaneously on the GPU, then bulk-transfer the resulting
transforms (body_q) from device to host memory. This computational model
is fundamentally incompatible with OpenExec's per-prim evaluation pattern,
where each prim's transform is computed independently through the
computation DAG.

Three constraints make per-prim exec evaluation non-viable for GPU physics:

### 1. GPU Bulk Transfer (All-or-Nothing)

GPU physics solvers (XPBD, MuJoCo, Featherstone) operate on the entire
simulation state as a single CUDA kernel dispatch. The solver output is a
Warp CUDA buffer (`state.body_q`) containing quaternion poses for ALL
bodies. Transferring this to CPU happens as one `body_q.numpy()` call.

There is no way to evaluate "just the transform for prim X" without
running the full solver step. Exec's per-prim pull model would require
either:
- Running the full GPU solver for every prim query (N solver steps per
  frame for N bodies — catastrophically expensive), or
- Caching the solver output and serving it... which is exactly what the
  static transform cache does.

### 2. Python GIL Constraints

Newton GPU's Python API (`newton.Model`, `newton.Solver`) holds the GIL
during solver operations. Hydra's TBB workers call `GetPrim()` from
multiple threads. Acquiring the GIL from a TBB worker thread is:
- Expensive (contention with other TBB workers)
- Deadlock-prone (GIL + TBB task scheduling)
- Architecturally wrong (blocks all TBB workers on a single Python call)

The cache pattern solves this: Python writes transforms on the main
thread (GIL held naturally), then TBB workers read from the immutable
shared_ptr cache without touching Python.

### 3. Exec's Cache Invalidation Model

Exec caches computation results and invalidates when declared inputs
change. Physics transforms change every frame as a *side effect* of
stepping the solver — there are no USD attribute changes to trigger
invalidation. This means exec would serve stale transforms from its cache
indefinitely.

## The Static Transform Cache Pattern

```
Python (main thread, GIL held):
    solver.step()
    body_q = state.body_q.numpy()
    SetCachedTransforms([(path, matrix) for each body])

TBB Workers (no GIL):
    GetCachedTransform(primPath) → std::optional<GfMatrix4d>
```

### Thread Safety

The cache uses a **copy-on-write shared_ptr swap** pattern:

- **Writes** (from Python main thread): copy the map, modify, atomically
  swap the shared_ptr under a mutex.
- **Reads** (from TBB workers): grab the shared_ptr under a mutex (one
  atomic increment), then read the immutable map without locks.
- **Batch writes** (`SetCachedTransforms`): build a new map, swap once.
  This is the preferred API — it avoids N copy-on-write cycles.

The cache is effectively lock-free on the read path for the common case
(single atomic shared_ptr copy).

### Formalizing as an HdExec Pattern

This pattern should be formalized as a supported "external data injection"
mechanism in HdExec. Other GPU simulation engines have the same need:

| Engine | Language | GPU Transfer | Same Constraints |
|--------|----------|-------------|------------------|
| Newton GPU | Python/CUDA | body_q.numpy() | ✓ GIL, bulk |
| NVIDIA Warp | Python/CUDA | wp.launch() | ✓ GIL, bulk |
| PhysX (GPU) | C++/CUDA | PxScene::fetchResults() | ✓ bulk |
| Flex | C++/CUDA | NvFlexMap() | ✓ bulk |

Proposed API surface (already implemented):

```cpp
// Batch-set transforms (preferred: one swap per frame).
static void SetCachedTransforms(
    const std::vector<std::pair<SdfPath, GfMatrix4d>> &transforms);

// Query (thread-safe, lock-free read).
static std::optional<GfMatrix4d> GetCachedTransform(
    const SdfPath &primPath);
```

## Post-Flattening Ancestor Walk

When a parent Xform prim has a cached physics transform (M_sim) but its
child Mesh does not, the child's flattened world transform is stale. The
ancestor walk recomposes:

```
M_child_new = M_child_input × inv(M_ancestor_input) × M_sim
```

### Safety: Non-Invertible Transforms

`inv(M_ancestor_input)` is undefined when the ancestor has a
non-invertible transform (e.g., zero scale → determinant = 0). The
implementation guards against this:

```cpp
double det = ancestorOriginal.GetDeterminant();
if (GfIsClose(det, 0.0, 1e-12)) {
    // Skip walk; child retains stale input transform.
    // One frame of visual lag is better than NaN.
    break;
}
```

This is tested in `_TestAncestorWalkDegenerateTransform`.

### Multiple Ancestors

The walk finds the **nearest** ancestor with a cached transform and
breaks. In physics, the body prim is typically the direct parent of
renderables (or one level up via an Xform grouping). Nested physics
bodies at multiple hierarchy levels would each have their own cached
transforms, and the nearest-ancestor policy correctly handles this.

### Cycle Safety

The ancestor walk reads `_GetInputSceneIndex()->GetPrim(ancestor)` —
never `this->GetPrim(ancestor)`. Since the input scene index is
upstream, and we walk strictly toward the root (GetParentPath), cycles
are impossible.

## GetPrimPath() on HdFlattenedDataSourceProvider::Context

The physics xform flattening provider (`HdExecPhysicsXformProvider`)
needs to know which prim it's computing for, in order to look up the
static transform cache by SdfPath. This required adding:

```cpp
// In flattenedDataSourceProvider.h, Context class:
const SdfPath &GetPrimPath() const { return _primPath; }
```

**Why this is the minimal necessary change:**

- `_primPath` is already a private member of `Context` (passed to
  constructor). We're exposing it as a read-only accessor — 4 lines.
- Alternative: derive the path from `GetInputDataSource()` by walking
  the container hierarchy, but this is fragile and requires assumptions
  about data source structure.
- Alternative: pass the path through a side channel (e.g., thread-local
  or a custom Context subclass), but this is more invasive than a
  simple accessor.
- The accessor is generally useful — any provider that needs
  path-dependent behavior (LOD, visibility, domain-specific overrides)
  benefits from it.

**Recommendation:** propose this as an upstream addition to
`HdFlattenedDataSourceProvider::Context`. It's a natural extension
alongside `GetInputPrimType()` (added in upstream commit 03ad3351e).

## Dirty Mechanism

### Current State (Temporary)

`AdvanceGlobalTime()` explicitly dirties cached-transform prims and their
descendants by calling `_SendPrimsDirtied()`. This triggers Storm to
re-query `GetPrim()` for affected prims, picking up the new transforms.

The UsdView plugin previously used a session layer xformOp poke to force
upstream dirty signals through UsdImagingStageSceneIndex. **This has been
removed.** The current approach uses direct dirty notification from the
HdExec filter, which is cleaner because:

1. No session layer pollution (the poke wrote a meaningless xformOp value
   that would confuse other scene index filters).
2. No dependency on UsdImagingStageSceneIndex's time-change codepath.
3. The dirty is scoped precisely to physics prims + their descendants.

### Future: Pre-Flattening Plugin Insertion

The ideal long-term solution is a plugin-discoverable insertion point
**before** HdFlatteningSceneIndex (step 6 in the chain). This would
allow a physics scene index filter to inject transforms at the input
level, letting flattening handle parent→child propagation naturally.

This is tracked as an upstream issue on PR #1 (OpenExec scene index
infrastructure). The current post-flattening approach with ancestor walk
is a correct workaround, but it duplicates the parent→child composition
logic that flattening already implements.

When the pre-flattening hook becomes available:
1. Physics transforms would be injected as local-space overrides.
2. Flattening would compose parent→child automatically.
3. The ancestor walk code would be removed.
4. Dirty signals from the pre-flattening filter would propagate through
   flattening's existing invalidation mechanism.
