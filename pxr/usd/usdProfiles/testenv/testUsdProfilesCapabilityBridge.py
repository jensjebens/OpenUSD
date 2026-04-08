#!/pxrpythonsubst
#
# Copyright 2024 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

"""Tests for the usd.* capability bridge layer.

Phase 2.5: Verifies that Pixar's built-in validators are mapped to usd.*
capabilities and that SimReady profiles inherit them via the DAG.

Ref: jensjebens/OpenUSD#32
"""

from __future__ import print_function

import unittest

from pxr import UsdProfiles


class TestUsdCapabilityBridge(unittest.TestCase):
    """Tests for the usd.* → Pixar validator bridge."""

    def setUp(self):
        self.reg = UsdProfiles.CapabilityRegistry.GetInstance()

    # ---------------------------------------------------------------------- #
    # usd.* capabilities exist
    # ---------------------------------------------------------------------- #

    def test_UsdCapabilitiesExist(self):
        """All canonical usd.* capabilities are registered."""
        expected = [
            "usd",
            "usd.core",
            "usd.geom",
            "usd.shade",
            "usd.physics",
            "usd.skel",
            "usd.utils",
            "usd.core.v25_05",
        ]
        for cap in expected:
            self.assertTrue(
                self.reg.IsCapability(cap),
                f"Expected '{cap}' to be a registered capability")

    # ---------------------------------------------------------------------- #
    # usd.* capabilities have correct predecessor relationships
    # ---------------------------------------------------------------------- #

    def test_UsdGeomPredecessors(self):
        """usd.geom has 'usd' as predecessor."""
        preds = self.reg.GetPredecessors("usd.geom")
        self.assertIn("usd", preds)

    def test_UsdShadePredecessors(self):
        """usd.shade has 'usd' as predecessor."""
        preds = self.reg.GetPredecessors("usd.shade")
        self.assertIn("usd", preds)

    def test_UsdPhysicsPredecessors(self):
        """usd.physics has 'usd' as predecessor."""
        preds = self.reg.GetPredecessors("usd.physics")
        self.assertIn("usd", preds)

    def test_UsdSkelPredecessors(self):
        """usd.skel has 'usd' as predecessor."""
        preds = self.reg.GetPredecessors("usd.skel")
        self.assertIn("usd", preds)

    def test_UsdUtilsPredecessors(self):
        """usd.utils has 'usd' as predecessor."""
        preds = self.reg.GetPredecessors("usd.utils")
        self.assertIn("usd", preds)

    def test_UsdCoreV2505Predecessors(self):
        """usd.core.v25_05 has all usd.* domain capabilities as predecessors."""
        preds = self.reg.GetPredecessors("usd.core.v25_05")
        for expected in ["usd.core", "usd.geom", "usd.shade",
                         "usd.physics", "usd.skel", "usd.utils"]:
            self.assertIn(expected, preds,
                          f"usd.core.v25_05 should have '{expected}' "
                          f"as a direct predecessor")

    # ---------------------------------------------------------------------- #
    # usd.* capabilities have Pixar validators attached
    # ---------------------------------------------------------------------- #

    def test_UsdCoreValidators(self):
        """usd.core declares the 3 core validators."""
        validators = self.reg.GetValidators("usd.core")
        expected = [
            "usdValidation:CompositionErrorTest",
            "usdValidation:StageMetadataChecker",
            "usdValidation:AttributeTypeMismatch",
        ]
        for v in expected:
            self.assertIn(v, validators,
                          f"usd.core should declare validator '{v}'")
        self.assertEqual(len(validators), 3)

    def test_UsdGeomValidators(self):
        """usd.geom declares the 4 geom validators."""
        validators = self.reg.GetValidators("usd.geom")
        expected = [
            "usdGeomValidators:EncapsulationChecker",
            "usdGeomValidators:StageMetadataChecker",
            "usdGeomValidators:SubsetFamilies",
            "usdGeomValidators:SubsetParentIsImageable",
        ]
        for v in expected:
            self.assertIn(v, validators,
                          f"usd.geom should declare validator '{v}'")
        self.assertEqual(len(validators), 4)

    def test_UsdShadeValidators(self):
        """usd.shade declares the 9 shade validators."""
        validators = self.reg.GetValidators("usd.shade")
        expected = [
            "usdShadeValidators:EncapsulationMaterialValidator",
            "usdShadeValidators:EncapsulationRulesValidator",
            "usdShadeValidators:MaterialBindingApiAppliedValidator",
            "usdShadeValidators:MaterialBindingCollectionValidator",
            "usdShadeValidators:MaterialBindingRelationships",
            "usdShadeValidators:NormalMapTextureValidator",
            "usdShadeValidators:ShaderSdrCompliance",
            "usdShadeValidators:SubsetMaterialBindFamilyName",
            "usdShadeValidators:SubsetsMaterialBindFamily",
        ]
        for v in expected:
            self.assertIn(v, validators,
                          f"usd.shade should declare validator '{v}'")
        self.assertEqual(len(validators), 9)

    def test_UsdPhysicsValidators(self):
        """usd.physics declares the 4 physics validators."""
        validators = self.reg.GetValidators("usd.physics")
        expected = [
            "usdPhysicsValidators:RigidBodyChecker",
            "usdPhysicsValidators:ColliderChecker",
            "usdPhysicsValidators:ArticulationChecker",
            "usdPhysicsValidators:PhysicsJointChecker",
        ]
        for v in expected:
            self.assertIn(v, validators,
                          f"usd.physics should declare validator '{v}'")
        self.assertEqual(len(validators), 4)

    def test_UsdSkelValidators(self):
        """usd.skel declares the 2 skel validators."""
        validators = self.reg.GetValidators("usd.skel")
        expected = [
            "usdSkelValidators:SkelBindingApiAppliedValidator",
            "usdSkelValidators:SkelBindingApiValidator",
        ]
        for v in expected:
            self.assertIn(v, validators,
                          f"usd.skel should declare validator '{v}'")
        self.assertEqual(len(validators), 2)

    def test_UsdUtilsValidators(self):
        """usd.utils declares the 5 utils validators."""
        validators = self.reg.GetValidators("usd.utils")
        expected = [
            "usdUtilsValidators:FileExtensionValidator",
            "usdUtilsValidators:MissingReferenceValidator",
            "usdUtilsValidators:PackageEncapsulationValidator",
            "usdUtilsValidators:RootPackageValidator",
            "usdUtilsValidators:UsdzPackageValidator",
        ]
        for v in expected:
            self.assertIn(v, validators,
                          f"usd.utils should declare validator '{v}'")
        self.assertEqual(len(validators), 5)

    # ---------------------------------------------------------------------- #
    # Profile-level aggregation
    # ---------------------------------------------------------------------- #

    def test_UsdCoreV2505AllValidators(self):
        """usd.core.v25_05 profile aggregates all 27 Pixar validators."""
        validators = self.reg.GetAllValidatorsForCapability("usd.core.v25_05")
        # 3 core + 4 geom + 9 shade + 4 physics + 2 skel + 5 utils = 27
        self.assertEqual(
            len(validators), 27,
            f"Expected 27 Pixar validators for usd.core.v25_05, "
            f"got {len(validators)}: {sorted(validators)}")

    def test_SimReadyProfileInheritsPixarValidators(self):
        """SimReady prop profile inherits Pixar validators via DAG."""
        validators = self.reg.GetAllValidatorsForCapability(
            "com.nvidia.simready.prop_robotics_neutral")

        # SimReady-specific validators (10)
        simready_validators = [
            "com.nvidia.simready:VG.MESH.001",
            "com.nvidia.simready:VG.014",
            "com.nvidia.simready:VG.027",
            "com.nvidia.simready:VG.025",
            "com.nvidia.simready:RB.003",
            "com.nvidia.simready:RB.005",
            "com.nvidia.simready:RB.COL.001",
            "com.nvidia.simready:HI.001",
            "com.nvidia.simready:HI.004",
            "com.nvidia.simready:UN.001",
        ]

        for v in simready_validators:
            self.assertIn(v, validators,
                          f"SimReady profile should include '{v}'")

        # Pixar validators from the bridge
        pixar_validators = [
            # From usd.core (via com.nvidia.simready.units → usd.core)
            "usdValidation:CompositionErrorTest",
            "usdValidation:StageMetadataChecker",
            "usdValidation:AttributeTypeMismatch",
            # From usd.geom (via com.nvidia.simready.geom → usd.geom)
            "usdGeomValidators:EncapsulationChecker",
            "usdGeomValidators:StageMetadataChecker",
            "usdGeomValidators:SubsetFamilies",
            "usdGeomValidators:SubsetParentIsImageable",
            # From usd.physics (via com.nvidia.simready.physics → usd.physics)
            "usdPhysicsValidators:RigidBodyChecker",
            "usdPhysicsValidators:ColliderChecker",
            "usdPhysicsValidators:ArticulationChecker",
            "usdPhysicsValidators:PhysicsJointChecker",
        ]

        for v in pixar_validators:
            self.assertIn(v, validators,
                          f"SimReady profile should inherit Pixar validator "
                          f"'{v}' via the usd.* bridge")

    def test_SimReadyProfileValidatorCount(self):
        """SimReady prop profile has correct total validator count.

        10 SimReady + Pixar validators inherited via:
          com.nvidia.simready.geom → usd.geom (4)
          com.nvidia.simready.physics → usd.physics (4)
          com.nvidia.simready.units → usd.core (3)
          com.nvidia.simready → usd (0)
        Total Pixar: 4 + 4 + 3 = 11 unique (usd has no validators)
        Total: 10 + 11 = 21
        """
        validators = self.reg.GetAllValidatorsForCapability(
            "com.nvidia.simready.prop_robotics_neutral")
        # SimReady inherits from usd.geom, usd.physics, usd.core via DAG
        # but NOT usd.shade, usd.skel, usd.utils (those aren't predecessors)
        self.assertEqual(
            len(validators), 21,
            f"Expected 21 validators (10 SimReady + 11 Pixar), "
            f"got {len(validators)}: {sorted(validators)}")

    def test_NoDuplicateValidators(self):
        """No duplicate validators in aggregated results."""
        validators = self.reg.GetAllValidatorsForCapability(
            "com.nvidia.simready.prop_robotics_neutral")
        self.assertEqual(
            len(validators), len(set(validators)),
            "Duplicate validators detected in aggregated results")

    # ---------------------------------------------------------------------- #
    # SimReady → usd.* predecessor wiring
    # ---------------------------------------------------------------------- #

    def test_SimReadyGeomInheritsUsdGeom(self):
        """com.nvidia.simready.geom has usd.geom as a predecessor."""
        preds = self.reg.GetPredecessors("com.nvidia.simready.geom")
        self.assertIn("usd.geom", preds,
                      "simready.geom should inherit from usd.geom")

    def test_SimReadyPhysicsInheritsUsdPhysics(self):
        """com.nvidia.simready.physics has usd.physics as a predecessor."""
        preds = self.reg.GetPredecessors("com.nvidia.simready.physics")
        self.assertIn("usd.physics", preds,
                      "simready.physics should inherit from usd.physics")

    def test_SimReadyUnitsInheritsUsdCore(self):
        """com.nvidia.simready.units has usd.core as a predecessor."""
        preds = self.reg.GetPredecessors("com.nvidia.simready.units")
        self.assertIn("usd.core", preds,
                      "simready.units should inherit from usd.core")

    def test_SimReadyTransitivelyReachesUsd(self):
        """SimReady profile transitively reaches 'usd' root."""
        transitive = self.reg.GetTransitivePredecessors(
            "com.nvidia.simready.prop_robotics_neutral")
        self.assertIn("usd", transitive)
        self.assertIn("usd.geom", transitive)
        self.assertIn("usd.physics", transitive)
        self.assertIn("usd.core", transitive)


if __name__ == "__main__":
    unittest.main(verbosity=2)
