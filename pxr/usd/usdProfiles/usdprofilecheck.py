#!/usr/bin/env python3
"""
usdprofilecheck — Validate USD assets against capability profiles
=================================================================

A command-line tool that validates USD assets against declared or
specified capability profiles using the UsdValidation framework.

Examples:
    # Validate using the profile authored on the asset
    usdprofilecheck my_asset.usda

    # Validate against a specific profile
    usdprofilecheck --profile com.nvidia.simready.prop_robotics_neutral my_asset.usda

    # Validate multiple assets
    usdprofilecheck asset1.usda asset2.usda asset3.usdz

    # JSON output for CI/CD
    usdprofilecheck --format json my_asset.usda

    # List available profiles
    usdprofilecheck --list-profiles

    # Strict mode — warnings become errors
    usdprofilecheck --strict my_asset.usda
"""

from __future__ import print_function

import argparse
import json
import sys
import os

# Ensure the usdProfiles module directory is on the path
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if _SCRIPT_DIR not in sys.path:
    sys.path.insert(0, _SCRIPT_DIR)

from pxr import Sdf, Usd, UsdProfiles, UsdValidation

import simready_validators


def _register_validators():
    """Ensure SimReady validators are registered."""
    simready_validators.register_simready_validators()


def _get_profile_for_stage(stage):
    """Read the profile from the stage's default prim or first root prim."""
    default_prim = stage.GetDefaultPrim()
    if default_prim and default_prim.IsValid():
        profile = UsdProfiles.GetEffectiveProfile(default_prim)
        if profile:
            return str(profile)

    for prim in stage.GetPseudoRoot().GetChildren():
        profile = UsdProfiles.GetEffectiveProfile(prim)
        if profile:
            return str(profile)

    return None


def _get_validators_for_profile(profile_name):
    """Resolve profile to validators via the CapabilityRegistry."""
    registry = UsdValidation.ValidationRegistry()
    cap_registry = UsdProfiles.CapabilityRegistry.GetInstance()

    if not cap_registry.IsCapability(profile_name):
        return [], []

    validator_names = [
        str(v) for v in cap_registry.GetAllValidatorsForCapability(profile_name)
    ]
    validators = registry.GetOrLoadValidatorsByName(validator_names)
    return validators, validator_names


def _format_error(error):
    """Format a validation error for display."""
    sites = error.GetSites()
    if sites and sites[0].GetPrim() and sites[0].GetPrim().IsValid():
        site = str(sites[0].GetPrim().GetPath())
    elif sites and sites[0].GetLayer():
        site = sites[0].GetLayer().identifier
    else:
        site = "<stage>"

    return {
        "identifier": error.GetIdentifier(),
        "site": site,
        "message": error.GetMessage(),
        "type": str(error.GetType()),
    }


def cmd_validate(args):
    """Validate assets against profiles."""
    _register_validators()

    all_results = []
    exit_code = 0

    for asset_path in args.assets:
        stage = Usd.Stage.Open(asset_path)
        if not stage:
            print(f"ERROR: Cannot open {asset_path}", file=sys.stderr)
            exit_code = 1
            continue

        # Determine profile
        if args.profile:
            profile = args.profile
        else:
            profile = _get_profile_for_stage(stage)
            if not profile:
                if args.format == "text":
                    print(f"SKIP: {asset_path} — no profile authored (use --profile to specify)")
                continue

        # Get validators
        validators, validator_names = _get_validators_for_profile(profile)
        if not validators:
            if args.format == "text":
                print(f"SKIP: {asset_path} — no validators found for profile '{profile}'")
            continue

        # Run validation
        ctx = UsdValidation.ValidationContext(validators)
        errors = ctx.Validate(stage)

        # Filter by strictness
        if args.strict:
            issues = errors  # All errors including warnings
        else:
            issues = [e for e in errors
                      if e.GetType() in (UsdValidation.ValidationErrorType.Error,)]

        result = {
            "asset": asset_path,
            "profile": profile,
            "validators": len(validators),
            "errors": [_format_error(e) for e in errors],
            "passed": len(issues) == 0,
        }
        all_results.append(result)

        if issues:
            exit_code = 1

    # Output
    if args.format == "json":
        json.dump(all_results, sys.stdout, indent=2)
        print()
    elif args.format == "csv":
        print("asset,profile,passed,error_count,errors")
        for r in all_results:
            errs = "; ".join(e["identifier"] for e in r["errors"])
            print(f"{r['asset']},{r['profile']},{r['passed']},{len(r['errors'])},{errs}")
    else:  # text
        for r in all_results:
            print(f"\n{'='*70}")
            print(f"Asset:      {r['asset']}")
            print(f"Profile:    {r['profile']}")
            print(f"Validators: {r['validators']}")

            if r["errors"]:
                print(f"{'─'*70}")
                for i, e in enumerate(r["errors"], 1):
                    print(f"  [{i}] {e['identifier']}")
                    print(f"      Site:    {e['site']}")
                    print(f"      Message: {e['message']}")
                    print()
                status = "FAIL" if not r["passed"] else "WARN"
                print(f"{'='*70}")
                print(f"RESULT: {status} — {len(r['errors'])} issue(s)")
            else:
                print(f"{'='*70}")
                print(f"RESULT: ✅ PASS")

    return exit_code


def cmd_list_profiles(args):
    """List available profiles."""
    _register_validators()
    cap_registry = UsdProfiles.CapabilityRegistry.GetInstance()

    profiles = cap_registry.GetAllProfiles()
    if not profiles:
        print("No profiles registered.")
        return 0

    if args.format == "json":
        result = []
        for p in sorted(str(p) for p in profiles):
            caps = [str(c) for c in cap_registry.GetTransitivePredecessors(p)]
            vals = [str(v) for v in cap_registry.GetAllValidatorsForCapability(p)]
            result.append({
                "profile": p,
                "capabilities": caps,
                "validators": vals,
            })
        json.dump(result, sys.stdout, indent=2)
        print()
    else:
        print("Available profiles:\n")
        for p in sorted(str(p) for p in profiles):
            caps = cap_registry.GetTransitivePredecessors(p)
            vals = cap_registry.GetAllValidatorsForCapability(p)
            print(f"  {p}")
            print(f"    Capabilities: {len(caps)}")
            print(f"    Validators:   {len(vals)}")
            for v in vals:
                print(f"      - {v}")
            print()

    return 0


def cmd_list_capabilities(args):
    """List available capabilities."""
    _register_validators()
    cap_registry = UsdProfiles.CapabilityRegistry.GetInstance()

    capabilities = cap_registry.GetAllCapabilities()
    if not capabilities:
        print("No capabilities registered.")
        return 0

    if args.format == "json":
        result = []
        for c in sorted(str(c) for c in capabilities):
            result.append({
                "capability": c,
                "isProfile": cap_registry.IsProfile(c),
                "predecessors": [str(p) for p in cap_registry.GetPredecessors(c)],
                "validators": [str(v) for v in cap_registry.GetValidators(c)],
                "docstring": cap_registry.GetDocstring(c),
            })
        json.dump(result, sys.stdout, indent=2)
        print()
    else:
        print("Capability DAG:\n")
        for c in sorted(str(c) for c in capabilities):
            tag = " [PROFILE]" if cap_registry.IsProfile(c) else ""
            vals = cap_registry.GetValidators(c)
            preds = cap_registry.GetPredecessors(c)
            print(f"  {c}{tag}")
            if preds:
                print(f"    predecessors: {[str(p) for p in preds]}")
            if vals:
                print(f"    validators:   {[str(v) for v in vals]}")
        print()

    return 0


def main():
    parser = argparse.ArgumentParser(
        prog="usdprofilecheck",
        description="Validate USD assets against capability profiles",
    )
    parser.add_argument("--format", "-f",
                       choices=["text", "json", "csv"],
                       default="text",
                       help="Output format (default: text)")

    subparsers = parser.add_subparsers(dest="command")

    # validate (default)
    validate_parser = subparsers.add_parser("validate",
                                            help="Validate assets against profiles")
    validate_parser.add_argument("assets", nargs="+",
                                help="USD asset path(s) to validate")
    validate_parser.add_argument("--profile", "-p",
                                help="Profile to validate against (default: read from asset)")
    validate_parser.add_argument("--strict", "-s", action="store_true",
                                help="Treat warnings as errors")
    validate_parser.add_argument("--format",
                                choices=["text", "json", "csv"],
                                default="text",
                                help="Output format (default: text)")

    # list-profiles
    lp = subparsers.add_parser("list-profiles", help="List available profiles")
    lp.add_argument("--format", choices=["text", "json"], default="text")

    # list-capabilities
    lc = subparsers.add_parser("list-capabilities", help="List capability DAG")
    lc.add_argument("--format", choices=["text", "json"], default="text")

    # If first arg doesn't match a subcommand, assume validate
    if len(sys.argv) > 1 and sys.argv[1] not in ("validate", "list-profiles", "list-capabilities", "-h", "--help"):
        sys.argv.insert(1, "validate")

    args = parser.parse_args()

    if args.command == "validate":
        return cmd_validate(args)
    elif args.command == "list-profiles":
        return cmd_list_profiles(args)
    elif args.command == "list-capabilities":
        return cmd_list_capabilities(args)

    return 0


if __name__ == "__main__":
    sys.exit(main())
