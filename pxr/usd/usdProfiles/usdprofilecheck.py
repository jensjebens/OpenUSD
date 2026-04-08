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


# ─── Namespace filtering ──────────────────────────────────────────────

def filter_capabilities_by_namespace(namespace, include_predecessors=False):
    """Return capability names matching the given namespace prefix.

    Args:
        namespace: Dot-separated prefix (e.g. "com.nvidia.simready").
        include_predecessors: If True, also include transitive predecessors
            of matching capabilities (even if they don't match the prefix).

    Returns:
        Sorted list of capability name strings.
    """
    reg = UsdProfiles.CapabilityRegistry.GetInstance()
    all_caps = [str(c) for c in reg.GetAllCapabilities()]

    # Filter to those matching the namespace prefix.
    # "usd" should match "usd", "usd.core", "usd.geom" etc.
    # "usd.geom" should match "usd.geom" but not "usd.core".
    matched = set()
    for cap in all_caps:
        if cap == namespace or cap.startswith(namespace + "."):
            matched.add(cap)

    if not matched:
        return []

    if include_predecessors:
        expanded = set(matched)
        for cap in matched:
            for pred in reg.GetTransitivePredecessors(cap):
                expanded.add(str(pred))
        return sorted(expanded)

    return sorted(matched)


# ─── DOT graph generation ─────────────────────────────────────────────

def generate_dot_graph(namespace=None, include_predecessors=False):
    """Generate a Graphviz DOT representation of the capability DAG.

    Args:
        namespace: Optional prefix filter (e.g. "com.nvidia.simready").
        include_predecessors: Include predecessor nodes outside the namespace.

    Returns:
        DOT format string.
    """
    reg = UsdProfiles.CapabilityRegistry.GetInstance()

    # Determine which capabilities to include.
    if namespace:
        caps = filter_capabilities_by_namespace(
            namespace, include_predecessors=include_predecessors)
    else:
        caps = sorted(str(c) for c in reg.GetAllCapabilities())

    cap_set = set(caps)

    lines = ['digraph capability_dag {']
    lines.append('    rankdir=BT;')  # Bottom-to-top: children point to parents
    lines.append('    node [fontname="Helvetica", fontsize=10];')
    lines.append('    edge [color="#666666"];')
    lines.append('')

    # Determine visual style for each namespace
    def _node_style(cap_name):
        validators = reg.GetValidators(cap_name)
        val_count = len(validators)
        is_profile = reg.IsProfile(cap_name)

        label = cap_name
        if val_count == 1:
            label += f"\\n(1 validator)"
        elif val_count > 1:
            label += f"\\n({val_count} validators)"

        # Choose shape and color by type
        if is_profile:
            shape = "doubleoctagon"
            fill = "#E8D5F5"  # Light purple for profiles
        elif cap_name.startswith("com.nvidia.simready"):
            shape = "box"
            fill = "#D4EDDA"  # Light green for SimReady
        elif cap_name.startswith("aousd"):
            shape = "box"
            fill = "#FFF3CD"  # Light yellow for AOUSD
        elif cap_name.startswith("usd"):
            shape = "box"
            fill = "#D1ECF1"  # Light blue for usd.*
        else:
            shape = "box"
            fill = "#F8F9FA"  # Light gray default

        return (f'    "{cap_name}" [label="{label}", shape={shape}, '
                f'style=filled, fillcolor="{fill}"];')

    # Emit nodes
    for cap in caps:
        lines.append(_node_style(cap))

    lines.append('')

    # Emit edges (child -> parent for "inherits from")
    for cap in caps:
        for pred in reg.GetPredecessors(cap):
            pred_str = str(pred)
            if pred_str in cap_set:
                lines.append(f'    "{cap}" -> "{pred_str}";')

    lines.append('}')
    return '\n'.join(lines)


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
    """List available capabilities, optionally filtered by namespace."""
    _register_validators()
    cap_registry = UsdProfiles.CapabilityRegistry.GetInstance()

    # Apply namespace filter if specified.
    if args.namespace:
        cap_names = filter_capabilities_by_namespace(
            args.namespace, include_predecessors=args.include_predecessors)
    else:
        cap_names = sorted(str(c) for c in cap_registry.GetAllCapabilities())

    if not cap_names:
        print("No capabilities found.")
        return 0

    if args.format == "json":
        result = []
        for c in cap_names:
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
        ns_label = f" (namespace: {args.namespace})" if args.namespace else ""
        print(f"Capability DAG{ns_label}:\n")
        for c in cap_names:
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


def cmd_graph(args):
    """Output the capability DAG as a Graphviz DOT graph."""
    _register_validators()

    dot = generate_dot_graph(
        namespace=args.namespace,
        include_predecessors=args.include_predecessors,
    )
    print(dot)
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
    lp.add_argument("--namespace", "-n",
                    help="Filter to capabilities under this prefix")

    # list-capabilities
    lc = subparsers.add_parser("list-capabilities", help="List capability DAG")
    lc.add_argument("--format", choices=["text", "json"], default="text")
    lc.add_argument("--namespace", "-n",
                    help="Filter to capabilities under this prefix")
    lc.add_argument("--include-predecessors", action="store_true",
                    help="Include predecessor nodes outside the namespace")

    # graph
    gp = subparsers.add_parser("graph",
                               help="Output capability DAG as Graphviz DOT")
    gp.add_argument("--namespace", "-n",
                    help="Filter to capabilities under this prefix")
    gp.add_argument("--include-predecessors", action="store_true",
                    help="Include predecessor nodes outside the namespace")
    gp.add_argument("-o", "--output",
                    help="Write DOT to file instead of stdout")

    SUBCOMMANDS = ("validate", "list-profiles", "list-capabilities", "graph",
                   "-h", "--help")

    # If first arg doesn't match a subcommand, assume validate
    if len(sys.argv) > 1 and sys.argv[1] not in SUBCOMMANDS:
        sys.argv.insert(1, "validate")

    args = parser.parse_args()

    if args.command == "validate":
        return cmd_validate(args)
    elif args.command == "list-profiles":
        return cmd_list_profiles(args)
    elif args.command == "list-capabilities":
        return cmd_list_capabilities(args)
    elif args.command == "graph":
        if hasattr(args, 'output') and args.output:
            import contextlib
            with open(args.output, 'w') as f:
                with contextlib.redirect_stdout(f):
                    return cmd_graph(args)
        return cmd_graph(args)

    return 0


if __name__ == "__main__":
    sys.exit(main())
