# UsdProfiles — USD Profiles Implementation

An implementation of the [USD-Profiles proposal](https://github.com/PixarAnimationStudios/OpenUSD-proposals/tree/main/proposals/profiles) providing declarative capability taxonomy for OpenUSD assets.

## Overview

UsdProfiles lets applications and assets declare, discover, and validate functional capabilities through a layered system:

- **Capabilities** — atomic units of functionality organized in a directed acyclic graph (DAG)
- **Profiles** — tagged capability nodes representing coherent sets of functionality
- **ProfileAPI** — applied schema for authoring profiles on USD prims
- **Queries** — functions to resolve effective profiles through prim hierarchies

## Quick Start

```python
from pxr import Usd, UsdGeom, UsdProfiles

# Create a stage
stage = Usd.Stage.CreateNew("my_asset.usda")
root = UsdGeom.Xform.Define(stage, "/MyRobot")

# Apply a profile to the root prim
api = UsdProfiles.ProfileAPI.Apply(root.GetPrim())
api.GetProfileAttr().Set("com.nvidia.simready.prop_robotics_neutral")

# Child prims inherit the profile automatically
arm = UsdGeom.Xform.Define(stage, "/MyRobot/Arm")

# Query the effective profile (walks ancestors)
profile = UsdProfiles.GetEffectiveProfile(arm.GetPrim())
# → "com.nvidia.simready.prop_robotics_neutral"

# Query effective capabilities (profile + transitive predecessors)
caps = UsdProfiles.GetEffectiveCapabilities(root.GetPrim())
# → ["com.nvidia.simready.prop_robotics_neutral",
#     "com.nvidia.simready.geom",
#     "com.nvidia.simready.physics.rigidBodies",
#     "com.nvidia.simready.physics",
#     "com.nvidia.simready",
#     "usd"]

stage.Save()
```

The resulting USD file:

```usda
#usda 1.0

def Xform "MyRobot" (
    prepend apiSchemas = ["ProfileAPI"]
)
{
    token profiles:profile = "com.nvidia.simready.prop_robotics_neutral"

    def Xform "Arm"
    {
    }
}
```

## Capability Registry

Capabilities are declared in `plugInfo.json` and loaded automatically at startup:

```json
{
    "Plugins": [{
        "Info": {
            "Capabilities": {
                "usd": {
                    "docstring": "Base USD capability",
                    "predecessors": []
                },
                "com.nvidia.simready": {
                    "docstring": "SimReady asset capabilities",
                    "predecessors": ["usd"]
                },
                "com.nvidia.simready.geom": {
                    "docstring": "SimReady geometry",
                    "predecessors": ["com.nvidia.simready"]
                },
                "com.nvidia.simready.prop_robotics_neutral": {
                    "docstring": "Neutral robotics prop profile",
                    "predecessors": [
                        "com.nvidia.simready.geom",
                        "com.nvidia.simready.physics.rigidBodies"
                    ],
                    "isProfile": true
                }
            }
        }
    }]
}
```

Query the registry:

```python
registry = UsdProfiles.CapabilityRegistry.GetInstance()

# List all capabilities and profiles
registry.GetAllCapabilities()   # → all registered capability tokens
registry.GetAllProfiles()       # → capabilities tagged as profiles

# Navigate the DAG
registry.GetPredecessors("com.nvidia.simready.geom")
# → ["com.nvidia.simready"]

registry.GetTransitivePredecessors("com.nvidia.simready.prop_robotics_neutral")
# → ["com.nvidia.simready.geom", "com.nvidia.simready.physics.rigidBodies",
#     "com.nvidia.simready.physics", "com.nvidia.simready", "usd"]

# Check membership
registry.IsCapability("com.nvidia.simready.geom")  # → True
registry.IsProfile("com.nvidia.simready.prop_robotics_neutral")  # → True
```

## Resolution Semantics

The ProfileAPI follows "nearest ancestor wins" resolution:

```
/Warehouse         ← ProfileAPI: "com.nvidia.simready.prop_robotics_neutral"
  /ShelfUnit_A     ← no ProfileAPI → inherits from /Warehouse
    /Frame         ← no ProfileAPI → inherits from /Warehouse
  /RobotArm        ← ProfileAPI: "com.nvidia.simready.prop_robotics_neutral"
    /Gripper       ← no ProfileAPI → inherits from /RobotArm
```

```python
# Ancestor resolution
UsdProfiles.GetEffectiveProfile(stage.GetPrimAtPath("/Warehouse/ShelfUnit_A/Frame"))
# → "com.nvidia.simready.prop_robotics_neutral" (inherited from /Warehouse)
```

## Capability Naming Convention

Following the [Pixar proposal](https://github.com/PixarAnimationStudios/OpenUSD-proposals/tree/main/proposals/profiles), capabilities use reverse domain notation:

| Prefix | Owner | Examples |
|--------|-------|---------|
| `usd.*` | OpenUSD / AOUSD | `usd.core.v25_05` |
| `aousd.*` | Alliance for OpenUSD | `aousd.interchange.v1_0` |
| `com.nvidia.simready.*` | NVIDIA SimReady | `com.nvidia.simready.geom`, `com.nvidia.simready.physics.rigidBodies` |
| `com.example.*` | Your organization | `com.example.myapp.feature` |

All vendor capabilities must transitively inherit from `usd`.

## Demo

Run the included demo script for a full walkthrough:

```bash
python3 pxr/usd/usdProfiles/demo_profiles.py
```

## API Reference

### UsdProfiles.ProfileAPI

Applied API schema with two attributes:

| Attribute | Type | Description |
|-----------|------|-------------|
| `profiles:profile` | `token` | Profile identifier (reverse domain notation) |
| `profiles:capabilities` | `token[]` | Additional capability declarations |

### UsdProfiles.CapabilityRegistry

Singleton registry loaded from `plugInfo.json`:

| Method | Returns | Description |
|--------|---------|-------------|
| `GetInstance()` | `CapabilityRegistry` | Singleton accessor |
| `IsCapability(id)` | `bool` | Check if id is registered |
| `IsProfile(id)` | `bool` | Check if id is a profile |
| `GetPredecessors(id)` | `[token]` | Direct predecessors in DAG |
| `GetTransitivePredecessors(id)` | `[token]` | Full transitive closure |
| `GetDocstring(id)` | `str` | Capability description |
| `GetAllCapabilities()` | `[token]` | All registered capabilities |
| `GetAllProfiles()` | `[token]` | All registered profiles |

### Free Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `GetEffectiveProfile(prim)` | `token` | Walk ancestors for nearest ProfileAPI |
| `GetEffectiveCapabilities(prim)` | `[token]` | Profile capabilities + explicit capabilities |

## Building

```bash
# Full build (recommended)
python3 build_scripts/build_usd.py --no-imaging --no-tools /path/to/install

# The usdProfiles module builds automatically as part of pxr/usd
```

## References

- [USD-Profiles Proposal](https://github.com/PixarAnimationStudios/OpenUSD-proposals/tree/main/proposals/profiles)
- [Unification Plan](https://github.com/jensjebens/usd-profiles/blob/master/plans/unification-plan.md)
- [Capability Mapping (CSV)](https://github.com/jensjebens/usd-profiles/blob/master/research/phase1-capability-mapping.csv)
