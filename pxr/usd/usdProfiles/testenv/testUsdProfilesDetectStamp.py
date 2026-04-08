#!/pxrpythonsubst
#
# Copyright 2024 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

"""Tests for profile detection and asset stamping.

Ref: jensjebens/usd-profiles#16, #17
"""

from __future__ import print_function

import json
import os
import sys
import tempfile
import unittest

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
if _PARENT_DIR not in sys.path:
    sys.path.insert(0, _PARENT_DIR)

from pxr import Usd, UsdGeom, UsdPhysics, UsdProfiles

import usdprofilecheck


class TestProfileDetection(unittest.TestCase):
    """Tests for the detect command."""

    def _make_compliant_stage(self):
        """Create a stage that passes most SimReady checks."""
        stage = Usd.Stage.CreateInMemory()
        stage.SetMetadata("upAxis", "Z")
        stage.SetMetadata("metersPerUnit", 1.0)

        root = stage.DefinePrim("/Root", "Xform")
        stage.SetDefaultPrim(root)

        # Geometry
        geom_scope = stage.DefinePrim("/Root/Geometry", "Scope")
        mesh = UsdGeom.Mesh.Define(stage, "/Root/Geometry/Body")

        # Physics
        rb_prim = stage.DefinePrim("/Root/Physics", "Xform")
        UsdPhysics.RigidBodyAPI.Apply(rb_prim)
        UsdPhysics.CollisionAPI.Apply(mesh.GetPrim())

        return stage

    def _make_empty_stage(self):
        """Create a minimal stage with nothing useful."""
        stage = Usd.Stage.CreateInMemory()
        stage.DefinePrim("/Root", "Xform")
        stage.SetDefaultPrim(stage.GetPrimAtPath("/Root"))
        return stage

    def test_DetectReturnsFeatureResults(self):
        """detect_profile returns per-feature pass/fail results."""
        stage = self._make_compliant_stage()
        result = usdprofilecheck.detect_profile(
            stage, "com.nvidia.simready.prop_robotics_neutral")

        self.assertIn("features", result)
        self.assertIsInstance(result["features"], dict)

        # Should have entries for each feature in the profile
        for feature in ["com.nvidia.simready.geom",
                        "com.nvidia.simready.physics.rigidBodies",
                        "com.nvidia.simready.hierarchy",
                        "com.nvidia.simready.units"]:
            self.assertIn(feature, result["features"],
                          f"Expected feature '{feature}' in detection results")

    def test_DetectReportsValidatorCounts(self):
        """Each feature has validator_count and passed_count."""
        stage = self._make_compliant_stage()
        result = usdprofilecheck.detect_profile(
            stage, "com.nvidia.simready.prop_robotics_neutral")

        for feature_name, feature_result in result["features"].items():
            self.assertIn("validators", feature_result)
            self.assertIn("passed", feature_result)
            self.assertIn("failed", feature_result)
            self.assertIn("status", feature_result)
            self.assertIn(feature_result["status"],
                          ("pass", "partial", "fail"))

    def test_DetectIdentifiesMatchingProfiles(self):
        """detect_profile reports which profiles the asset matches."""
        stage = self._make_compliant_stage()
        result = usdprofilecheck.detect_profile(
            stage, "com.nvidia.simready.prop_robotics_neutral")

        self.assertIn("profile", result)
        self.assertIn("compliant", result)
        self.assertIsInstance(result["compliant"], bool)

    def test_DetectEmptyStageFailsFeatures(self):
        """An empty stage should fail most features."""
        stage = self._make_empty_stage()
        result = usdprofilecheck.detect_profile(
            stage, "com.nvidia.simready.prop_robotics_neutral")

        self.assertFalse(result["compliant"],
                         "Empty stage should not be compliant")

    def test_DetectReportsErrors(self):
        """Failed validators include error details."""
        stage = self._make_empty_stage()
        result = usdprofilecheck.detect_profile(
            stage, "com.nvidia.simready.prop_robotics_neutral")

        # At least some features should have errors
        has_errors = False
        for feature_result in result["features"].values():
            if feature_result.get("errors"):
                has_errors = True
                for err in feature_result["errors"]:
                    self.assertIn("validator", err)
                    self.assertIn("message", err)
                break
        self.assertTrue(has_errors, "Expected some features to have errors")

    def test_DetectJsonOutput(self):
        """Detection result is JSON-serializable."""
        stage = self._make_compliant_stage()
        result = usdprofilecheck.detect_profile(
            stage, "com.nvidia.simready.prop_robotics_neutral")

        # Should not raise
        json_str = json.dumps(result, indent=2)
        parsed = json.loads(json_str)
        self.assertEqual(parsed["profile"], result["profile"])


class TestAssetStamping(unittest.TestCase):
    """Tests for writing ProfileAPI to assets."""

    def test_StampWritesProfileAPI(self):
        """stamp_asset applies ProfileAPI and writes profile + capabilities."""
        stage = Usd.Stage.CreateInMemory()
        stage.SetMetadata("upAxis", "Z")
        stage.SetMetadata("metersPerUnit", 1.0)
        root = stage.DefinePrim("/Root", "Xform")
        stage.SetDefaultPrim(root)

        profile_id = "com.nvidia.simready.prop_robotics_neutral"
        capabilities = [
            "com.nvidia.simready.geom",
            "com.nvidia.simready.units",
        ]

        usdprofilecheck.stamp_asset(stage, profile_id, capabilities)

        # Verify ProfileAPI is applied
        prim = stage.GetDefaultPrim()
        self.assertTrue(prim.HasAPI(UsdProfiles.ProfileAPI))

        # Verify profile attribute
        api = UsdProfiles.ProfileAPI(prim)
        self.assertEqual(api.GetProfileAttr().Get(), profile_id)

        # Verify capabilities
        caps = list(api.GetCapabilitiesAttr().Get())
        self.assertEqual(sorted(caps), sorted(capabilities))

    def test_StampRoundTrip(self):
        """Stamped assets can be read back."""
        stage = Usd.Stage.CreateInMemory()
        root = stage.DefinePrim("/Root", "Xform")
        stage.SetDefaultPrim(root)

        profile_id = "usd.core.v25_05"
        capabilities = ["usd.core", "usd.geom"]

        usdprofilecheck.stamp_asset(stage, profile_id, capabilities)

        # Read back via queries
        effective = UsdProfiles.GetEffectiveProfile(root)
        self.assertEqual(str(effective), profile_id)

    def test_StampToFile(self):
        """stamp_asset can write to a file on disk."""
        with tempfile.NamedTemporaryFile(suffix=".usda", delete=False) as f:
            tmp_path = f.name

        try:
            stage = Usd.Stage.CreateNew(tmp_path)
            root = stage.DefinePrim("/Root", "Xform")
            stage.SetDefaultPrim(root)

            usdprofilecheck.stamp_asset(
                stage, "usd.core.v25_05", ["usd.core"])
            stage.GetRootLayer().Save()

            # Reopen and verify
            stage2 = Usd.Stage.Open(tmp_path)
            prim = stage2.GetDefaultPrim()
            self.assertTrue(prim.HasAPI(UsdProfiles.ProfileAPI))
            api = UsdProfiles.ProfileAPI(prim)
            self.assertEqual(api.GetProfileAttr().Get(), "usd.core.v25_05")
        finally:
            os.unlink(tmp_path)

    def test_StampOnlyPassDoesNotStampFailures(self):
        """With stamp_only_pass=True, non-compliant assets are not stamped."""
        stage = Usd.Stage.CreateInMemory()
        root = stage.DefinePrim("/Root", "Xform")
        stage.SetDefaultPrim(root)

        # Don't stamp if not compliant
        result = usdprofilecheck.stamp_asset(
            stage, "com.nvidia.simready.prop_robotics_neutral", [],
            stamp_only_pass=True, compliant=False)

        self.assertFalse(result, "Should not stamp non-compliant assets")
        self.assertFalse(root.HasAPI(UsdProfiles.ProfileAPI))

    def test_MetadataJsonOutput(self):
        """generate_metadata produces valid JSON with required fields."""
        metadata = usdprofilecheck.generate_metadata(
            asset_path="test_asset.usd",
            profile="com.nvidia.simready.prop_robotics_neutral",
            compliant=True,
            features={
                "com.nvidia.simready.geom": {
                    "status": "pass",
                    "validators": 4,
                    "passed": 4,
                    "failed": 0,
                    "errors": [],
                }
            },
            inherited_capabilities=["usd.geom", "usd.core"],
        )

        # Verify structure
        self.assertEqual(metadata["asset"], "test_asset.usd")
        self.assertEqual(metadata["profile"],
                         "com.nvidia.simready.prop_robotics_neutral")
        self.assertTrue(metadata["compliant"])
        self.assertIn("validatedAt", metadata)
        self.assertIn("features", metadata)
        self.assertIn("capabilities_inherited", metadata)

        # Should be JSON-serializable
        json_str = json.dumps(metadata)
        self.assertIsInstance(json.loads(json_str), dict)


if __name__ == "__main__":
    unittest.main(verbosity=2)
