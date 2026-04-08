#!/usr/bin/env python3
"""
USD Profiles Codegen — plugInfo.json Generator
===============================================

Reads SimReady markdown specifications (capabilities, requirements, features,
profiles) and generates plugInfo.json with:
  - Capability DAG (predecessors, docstrings)
  - Validator bindings per capability
  - Profile declarations (isProfile: true)

This is the "developer narrative" tool: define capabilities in markdown,
run codegen, get plugInfo.json that the UsdProfiles CapabilityRegistry loads.

Usage:
    # From SimReady Foundation specs
    python3 pluginfo_generator.py \\
        --docs-root /path/to/simready-foundation/nv_core/sr_specs/docs \\
        --output plugInfo.json \\
        --vendor-prefix com.nvidia.simready

    # From a custom spec directory
    python3 pluginfo_generator.py \\
        --capabilities-dir ./my_capabilities \\
        --profiles-dir ./my_profiles \\
        --features-dir ./my_features \\
        --output plugInfo.json \\
        --vendor-prefix com.mycompany.myapp

Or use programmatically:

    from pluginfo_generator import PlugInfoGenerator
    gen = PlugInfoGenerator(docs_root="...", vendor_prefix="com.nvidia.simready")
    gen.generate("plugInfo.json")
"""

from __future__ import annotations

import json
import os
import sys
from dataclasses import dataclass, field
from typing import Any


@dataclass
class CapabilityDef:
    """A capability for plugInfo.json output."""
    id: str
    docstring: str = ""
    predecessors: list[str] = field(default_factory=list)
    validators: list[str] = field(default_factory=list)
    is_profile: bool = False


@dataclass
class PlugInfoGenerator:
    """
    Generates plugInfo.json from SimReady markdown specifications.

    Can parse either:
    1. omniverse-usd-profiles model objects (if available)
    2. A simple directory of markdown files (standalone mode)
    """

    docs_root: str | None = None
    capabilities_dir: str | None = None
    profiles_dir: str | None = None
    features_dir: str | None = None
    vendor_prefix: str = "com.nvidia.simready"
    plugin_name: str = "usdProfiles"
    include_usd_root: bool = True

    def generate(self, output_path: str) -> dict[str, Any]:
        """Generate plugInfo.json and write to output_path."""
        capabilities = self._load_capabilities()
        pluginfo = self._build_pluginfo(capabilities)

        with open(output_path, "w") as f:
            json.dump(pluginfo, f, indent=4)
            f.write("\n")

        return pluginfo

    def _load_capabilities(self) -> list[CapabilityDef]:
        """Load capabilities from markdown specs."""
        try:
            return self._load_from_omni_usd_profiles()
        except ImportError:
            return self._load_from_directory()

    def _load_from_omni_usd_profiles(self) -> list[CapabilityDef]:
        """Load using the omniverse-usd-profiles parser."""
        from omni.usd_profiles.markdown import SpecificationsParser
        from omni.usd_profiles.store import SpecificationsStore

        parser = SpecificationsParser(
            root_dir=self.docs_root,
            capabilities_root=self.capabilities_dir,
            profiles_root=self.profiles_dir,
            features_root=self.features_dir,
        )
        specs = parser.parse()
        store = SpecificationsStore(specs)

        capabilities = []

        # Base vendor capability
        vendor_cap = CapabilityDef(
            id=self.vendor_prefix,
            docstring=f"{self.vendor_prefix} asset capabilities",
            predecessors=["usd"],
        )
        capabilities.append(vendor_cap)

        # Convert each parsed capability
        for cap in store.capabilities:
            cap_id = f"{self.vendor_prefix}.{cap.id}"

            # Collect validator names from requirements
            validators = []
            for req in cap.requirements:
                validator_name = f"{self.vendor_prefix}:{req.code}"
                validators.append(validator_name)

            cap_def = CapabilityDef(
                id=cap_id,
                docstring=cap.description or cap.name or cap.id,
                predecessors=[self.vendor_prefix],
                validators=validators,
            )
            capabilities.append(cap_def)

        # Convert profiles
        for profile in store.profiles:
            profile_id = f"{self.vendor_prefix}.{profile.id}"

            # Profile predecessors are the features/capabilities it includes
            predecessors = []
            for feature in profile.features:
                feat_id = f"{self.vendor_prefix}.{feature.id}"
                predecessors.append(feat_id)

            # If no feature predecessors, link to the vendor root
            if not predecessors:
                predecessors = [self.vendor_prefix]

            cap_def = CapabilityDef(
                id=profile_id,
                docstring=profile.description or profile.name or profile.id,
                predecessors=predecessors,
                is_profile=True,
            )
            capabilities.append(cap_def)

        # Convert features as capabilities too (they're cross-cutting requirement sets)
        for feature in store.features:
            feat_id = f"{self.vendor_prefix}.{feature.id}"

            validators = []
            for req in feature.requirements:
                validator_name = f"{self.vendor_prefix}:{req.code}"
                validators.append(validator_name)

            # Feature predecessors: vendor root + any declared dependencies
            predecessors = [self.vendor_prefix]
            if hasattr(feature, 'dependency') and feature.dependency:
                for dep in feature.dependency:
                    dep_id = f"{self.vendor_prefix}.{dep.id}" if hasattr(dep, 'id') else str(dep)
                    predecessors.append(dep_id)

            cap_def = CapabilityDef(
                id=feat_id,
                docstring=feature.description or feature.name or feature.id,
                predecessors=predecessors,
                validators=validators,
            )
            capabilities.append(cap_def)

        return capabilities

    def _load_from_directory(self) -> list[CapabilityDef]:
        """Fallback: load from a simple directory structure."""
        # For now, return empty — this would parse markdown files directly
        print("WARNING: omni.usd_profiles not available, using directory fallback")
        return []

    def _build_pluginfo(self, capabilities: list[CapabilityDef]) -> dict[str, Any]:
        """Build the plugInfo.json structure."""
        caps_dict: dict[str, Any] = {}

        # Add usd root if requested
        if self.include_usd_root:
            caps_dict["usd"] = {
                "docstring": "Base USD capability, implied by all USD assets",
                "predecessors": []
            }

        # Add all capabilities
        for cap in capabilities:
            entry: dict[str, Any] = {
                "docstring": cap.docstring,
                "predecessors": cap.predecessors,
            }
            if cap.validators:
                entry["validators"] = cap.validators
            if cap.is_profile:
                entry["isProfile"] = True

            caps_dict[cap.id] = entry

        pluginfo = {
            "Plugins": [{
                "Info": {
                    "Capabilities": caps_dict
                },
                "LibraryPath": "@PLUG_INFO_LIBRARY_PATH@",
                "Name": self.plugin_name,
                "ResourcePath": "@PLUG_INFO_RESOURCE_PATH@",
                "Root": "@PLUG_INFO_ROOT@",
                "Type": "library"
            }]
        }

        return pluginfo


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description="Generate plugInfo.json from SimReady markdown specs")
    parser.add_argument("--docs-root", "-f",
                       help="Root docs directory (contains capabilities/, profiles/, features/)")
    parser.add_argument("--capabilities-dir",
                       help="Capabilities directory (if not using --docs-root)")
    parser.add_argument("--profiles-dir",
                       help="Profiles directory")
    parser.add_argument("--features-dir",
                       help="Features directory")
    parser.add_argument("--output", "-o", default="plugInfo.generated.json",
                       help="Output file path (default: plugInfo.generated.json)")
    parser.add_argument("--vendor-prefix", default="com.nvidia.simready",
                       help="Vendor prefix for capability IDs (default: com.nvidia.simready)")
    parser.add_argument("--plugin-name", default="usdProfiles",
                       help="Plugin name in plugInfo.json (default: usdProfiles)")
    parser.add_argument("--demo", action="store_true",
                       help="Generate a demo plugInfo.json from the installed omni.capabilities")

    args = parser.parse_args()

    if args.demo:
        # Generate from the already-installed omni.capabilities as a demo
        _generate_demo(args.output, args.vendor_prefix)
    else:
        gen = PlugInfoGenerator(
            docs_root=args.docs_root,
            capabilities_dir=args.capabilities_dir,
            profiles_dir=args.profiles_dir,
            features_dir=args.features_dir,
            vendor_prefix=args.vendor_prefix,
            plugin_name=args.plugin_name,
        )
        result = gen.generate(args.output)
        caps = result["Plugins"][0]["Info"]["Capabilities"]
        profiles = [k for k, v in caps.items() if v.get("isProfile")]
        validators = sum(len(v.get("validators", [])) for v in caps.values())
        print(f"Generated {args.output}:")
        print(f"  {len(caps)} capabilities")
        print(f"  {len(profiles)} profiles")
        print(f"  {validators} validator bindings")


def _generate_demo(output_path: str, vendor_prefix: str):
    """Generate a demo plugInfo.json from installed omni.capabilities."""
    try:
        from omni.capabilities import Capabilities, Requirements, Profiles, Features
    except ImportError:
        print("ERROR: omni.capabilities not available. Install omniverse-asset-validator.")
        sys.exit(1)

    caps_dict = {
        "usd": {
            "docstring": "Base USD capability",
            "predecessors": []
        },
        vendor_prefix: {
            "docstring": f"{vendor_prefix} capabilities",
            "predecessors": ["usd"]
        }
    }

    # Convert each Capability enum to a plugInfo entry
    for cap in Capabilities:
        cap_id = f"{vendor_prefix}.{cap.id}"
        validators = [
            f"{vendor_prefix}:{req.code}"
            for req in cap.requirements
        ]
        caps_dict[cap_id] = {
            "docstring": cap.id,
            "predecessors": [vendor_prefix],
            "validators": validators,
        }

    # Convert profiles
    for profile in Profiles:
        profile_id = f"{vendor_prefix}.{profile.id}"
        preds = [f"{vendor_prefix}.{cap.id}" for cap in profile.capabilities]
        if not preds:
            preds = [vendor_prefix]
        caps_dict[profile_id] = {
            "docstring": profile.id,
            "predecessors": preds,
            "isProfile": True,
        }

    # Convert features
    for feature in Features:
        feat_id = f"{vendor_prefix}.{feature.id}"
        validators = [
            f"{vendor_prefix}:{req.code}"
            for req in feature.requirements
        ]
        caps_dict[feat_id] = {
            "docstring": feature.id,
            "predecessors": [vendor_prefix],
            "validators": validators,
        }

    pluginfo = {
        "Plugins": [{
            "Info": {
                "Capabilities": caps_dict
            },
            "LibraryPath": "@PLUG_INFO_LIBRARY_PATH@",
            "Name": "usdProfiles",
            "ResourcePath": "@PLUG_INFO_RESOURCE_PATH@",
            "Root": "@PLUG_INFO_ROOT@",
            "Type": "library"
        }]
    }

    with open(output_path, "w") as f:
        json.dump(pluginfo, f, indent=4)
        f.write("\n")

    caps = pluginfo["Plugins"][0]["Info"]["Capabilities"]
    profiles = [k for k, v in caps.items() if v.get("isProfile")]
    validators = sum(len(v.get("validators", [])) for v in caps.values())
    print(f"Generated {output_path} from installed omni.capabilities:")
    print(f"  {len(caps)} capabilities")
    print(f"  {len(profiles)} profiles")
    print(f"  {validators} validator bindings")


if __name__ == "__main__":
    main()
