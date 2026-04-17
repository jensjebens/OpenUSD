#!/usr/bin/env python3
"""
Phase A3: Verify the modified USDRT population plugin loads in Kit.
Run via Kit --exec to check extension loading and plugin availability.
"""
import sys

print("=" * 60)
print("PHASE A3: Kit Plugin Verification")
print("=" * 60)

# Check what extensions are enabled
try:
    import omni.ext
    mgr = omni.ext.get_extension_manager()
    exts = [e["id"] for e in mgr.get_extensions() if e.get("enabled")]
    print(f"\nEnabled extensions: {len(exts)}")
    for ext in sorted(exts):
        print(f"  - {ext}")
    
    # Check for USDRT/population extensions
    usdrt_exts = [e for e in exts if "usdrt" in e.lower() or "population" in e.lower()]
    if usdrt_exts:
        print(f"\n✓ USDRT/population extensions found: {usdrt_exts}")
    else:
        print("\n⚠ No USDRT/population extensions loaded")
except Exception as e:
    print(f"Extension check failed: {e}")

# Check if omni.usd is available
try:
    import omni.usd
    ctx = omni.usd.get_context()
    print(f"\n✓ omni.usd context available: {ctx}")
except Exception as e:
    print(f"✗ omni.usd not available: {e}")

# Check if usdrt is available
try:
    import usdrt
    print(f"✓ usdrt module available")
except ImportError:
    print("⚠ usdrt module not available (expected if hydra.usdrt_delegate not loaded)")

# Check Carbonite plugin loading
try:
    import carb
    framework = carb.get_framework()
    print(f"\n✓ Carbonite framework available")
except Exception as e:
    print(f"✗ Carbonite framework: {e}")

print("\n" + "=" * 60)
print("Verification complete")
print("=" * 60)

import omni.kit.app
omni.kit.app.get_app().post_quit()
