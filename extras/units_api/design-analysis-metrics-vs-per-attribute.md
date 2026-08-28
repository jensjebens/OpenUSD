# Design Analysis: MetricsAPI (Prim-Level) vs. Per-Attribute Unit Metadata

## The Question

The MetricsAPI proposal (PR #45) moves unit declarations from layer metadata to **prim-level applied schemas**. But there's an alternative: what if we put unit information directly **on each attribute**? Which approach is better, and should we prototype both?

---

## Approach A: MetricsAPI (Prim-Level Applied Schema)

**How it works:**
- Apply `UsdGeomMetricsAPI` to a prim → sets `metersPerUnit`, `upAxis` for that subtree
- Properties **inherit down** the hierarchy until overridden by a descendant with the same schema
- To find the effective units for any prim: walk up ancestors until you find one with MetricsAPI applied
- Fallback to layer metadata for backwards compat

**What it tells you:**
- "Everything under this prim was authored in centimeters"
- A spatial context, not per-attribute semantics

**What it does NOT tell you:**
- Which attributes are unit-bearing
- What dimensional exponents each attribute has (length¹? length⁻³? unitless?)
- You still need an out-of-band registry mapping `schema:attribute → Dimension`

**Pros:**
- Minimal authoring burden — one annotation per subtree, not per attribute
- Composable — survives flattening, references, payloads
- Matches how content is actually authored (entire assets in one unit system)
- Aligns with Pixar's proposal direction (ecosystem alignment)
- Works today with USD's applied schema mechanism
- Low storage overhead

**Cons:**
- Doesn't solve the dimensional analysis problem (still need an attribute registry)
- Requires ancestor traversal at query time (performance concerns at scale)
- Mixed-unit subtrees (rare but possible) would need schema applied at multiple levels
- Can't express "this specific attribute was authored in different units than its prim context"

---

## Approach B: Per-Attribute Unit Metadata

**How it works:**
- Each attribute carries its own unit annotation
- Could be implemented via:
  - `customData` dictionary on the attribute (works today, no schema changes)
  - Plugin metadata field (e.g., `unitDimension`) via `plugInfo.json` (cleaner, typed)
  - Attribute metadata via `SetMetadata()`

**Example in usda:**
```
float physics:density = 1000.0 (
    customData = {
        string unitDimension = "M1_L-3"  # kg/m³
        double metersPerUnit = 1.0
    }
)
```
or with plugin metadata:
```
float physics:density = 1000.0 (
    unitDimension = "M1_L-3"
    metersPerUnit = 1.0
)
```

**What it tells you:**
- Exactly which attributes are unit-bearing (self-describing)
- What dimensional exponents each attribute has
- What unit system the value was authored in
- Everything a conversion tool needs, right on the attribute itself

**Pros:**
- **Self-describing** — no external registry needed to know an attribute's unit semantics
- No ancestor traversal needed — the metadata is right there
- Can handle mixed-unit attributes on the same prim (edge case but real)
- Makes the "attribute audit" concrete — you can see what's annotated and what isn't
- A conversion tool can be fully generic: read dimension + source units → convert
- Survives flattening (attribute metadata composes)

**Cons:**
- **Massive authoring burden** — every unit-bearing attribute on every prim needs annotation
- Enormous storage overhead at scale (thousands of prims × dozens of attributes)
- Who authors this? Schema defaults? Exporters? Every DCC tool needs to be updated
- Redundant — if 10,000 prims all have `translate` in centimeters, that's 10,000 identical annotations vs. one MetricsAPI on the root
- Schema attributes already have well-known semantics — `xformOp:translate` is always a length. The dimension is a property of the schema, not the instance.
- Not aligned with Pixar's direction (ecosystem friction)
- Plugin metadata requires C++ plugin registration (we want Python-first POC)

---

## Approach C: Hybrid (What Actually Makes Sense)

**Insight: These two approaches solve different problems.**

- **MetricsAPI** answers: "What unit system was this subtree authored in?" (the *context*)
- **Per-attribute dimensions** answer: "What are the dimensional exponents of this attribute?" (the *semantics*)

You need BOTH:
1. **MetricsAPI (prim-level)** → provides the unit context (metersPerUnit, kilogramsPerUnit)
2. **Dimensional registry (schema-level)** → provides the dimensional exponents per attribute *type* (not per instance)

The key insight is that **dimensional exponents are a property of the schema, not the data**. Every `xformOp:translate` is length¹. Every `physics:density` is M¹·L⁻³. This doesn't change per-prim — it's invariant. So it belongs in a registry, not on each attribute instance.

What DOES change per-prim (or per-subtree) is the unit system the values are expressed in. That's what MetricsAPI provides.

**BUT** — there IS a case for per-attribute metadata:

- **Custom attributes** that aren't part of any schema → no registry entry exists
- **Override scenarios** where a specific attribute was authored in different units than its prim context (unusual but not impossible)
- **Explicit annotation** for clarity/validation → "yes, this value is in meters, I'm sure"

---

## Recommendation: Prototype Both, But in Layers

### Layer 1: MetricsAPI (prim-level context) — PRIMARY
Implement the Pixar proposal. This is the main mechanism.

### Layer 2: Dimensional Registry (schema-level) — PRIMARY  
A Python dictionary mapping `(schema, attribute_name) → Dimension(L, M, T)`.
This is compile-time knowledge, not per-instance data.

### Layer 3: Per-Attribute Override (optional) — EXPERIMENTAL
Allow `customData` on individual attributes to override the registry dimension or declare a different unit context. This handles edge cases and custom attributes.

### What We Prototype:
1. MetricsAPI with dimensional registry → test the "normal" path
2. Per-attribute `customData` annotations → test the "explicit" path  
3. Compare: authoring ergonomics, query performance, storage overhead, conversion correctness

---

## Test Scenarios to Compare

| Scenario | MetricsAPI + Registry | Per-Attribute |
|---|---|---|
| Simple: cm asset in m stage | ✅ One MetricsAPI on root | ❌ Annotate every attr |
| Derived: density in mixed stage | ✅ Registry says L⁻³, MetricsAPI gives context | ✅ Self-describing |
| Custom attr: `myExtension:pipeRadius` | ❌ Not in registry, needs fallback | ✅ Self-describing |
| 10,000 prims, uniform units | ✅ One annotation | ❌ 10,000 × N annotations |
| Flattened stage | ✅ MetricsAPI survives | ✅ customData survives |
| Query: "what unit is this value in?" | Walk ancestors + registry lookup | Read attribute metadata |
| Mixed units on same prim | ❌ Can't express (rare) | ✅ Each attr has its own |

---

## Bottom Line

MetricsAPI is the right primary mechanism — it matches how content is authored (whole assets in one unit system) and scales well. But we should **also** prototype per-attribute annotations for:
1. Custom/unknown attributes where no registry entry exists
2. Validation (explicit unit intent)
3. Edge cases

The POC should test both and measure the tradeoffs empirically.
