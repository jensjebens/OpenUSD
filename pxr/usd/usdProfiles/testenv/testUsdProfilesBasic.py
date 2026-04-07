#!/pxrpythonsubst
#
# Copyright 2024 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from __future__ import print_function

import sys
import unittest

from pxr import Tf, Usd, UsdProfiles


class TestUsdProfilesBasic(unittest.TestCase):

    # ---------------------------------------------------------------------- #
    # ProfileAPI schema basics
    # ---------------------------------------------------------------------- #

    def test_ProfileAPIApply(self):
        """ProfileAPI can be applied to a prim and round-tripped."""
        stage = Usd.Stage.CreateInMemory()
        prim = stage.DefinePrim("/Root", "Xform")

        self.assertFalse(prim.HasAPI(UsdProfiles.ProfileAPI))

        api = UsdProfiles.ProfileAPI.Apply(prim)
        self.assertTrue(api)
        self.assertTrue(prim.HasAPI(UsdProfiles.ProfileAPI))

    def test_ProfileAPIAttributeRoundTrip(self):
        """profiles:profile and profiles:capabilities can be authored/read."""
        stage = Usd.Stage.CreateInMemory()
        prim = stage.DefinePrim("/Robot", "Xform")
        api = UsdProfiles.ProfileAPI.Apply(prim)

        profile_id = "com.nvidia.simready.prop_robotics_neutral"
        api.GetProfileAttr().Set(profile_id)

        read_back = api.GetProfileAttr().Get()
        self.assertEqual(read_back, profile_id)

    def test_ProfileAPICapabilitiesRoundTrip(self):
        """profiles:capabilities array can be authored and read."""
        stage = Usd.Stage.CreateInMemory()
        prim = stage.DefinePrim("/Asset", "Xform")
        api = UsdProfiles.ProfileAPI.Apply(prim)

        caps = ["com.nvidia.simready.geom",
                "com.nvidia.simready.physics.rigidBodies"]
        api.GetCapabilitiesAttr().Set(caps)

        read_back = list(api.GetCapabilitiesAttr().Get())
        self.assertEqual(sorted(read_back), sorted(caps))

    def test_ProfileAPICanApply(self):
        """CanApply returns True for a valid prim."""
        stage = Usd.Stage.CreateInMemory()
        prim = stage.DefinePrim("/Foo", "Xform")
        result = UsdProfiles.ProfileAPI.CanApply(prim)
        self.assertTrue(result)

    # ---------------------------------------------------------------------- #
    # Tokens
    # ---------------------------------------------------------------------- #

    def test_Tokens(self):
        """Token constants have the expected string values."""
        self.assertEqual(UsdProfiles.Tokens.profilesProfile,
                         "profiles:profile")
        self.assertEqual(UsdProfiles.Tokens.profilesCapabilities,
                         "profiles:capabilities")
        self.assertEqual(UsdProfiles.Tokens.ProfileAPI, "ProfileAPI")

    # ---------------------------------------------------------------------- #
    # CapabilityRegistry
    # ---------------------------------------------------------------------- #

    def test_CapabilityRegistryIsSingleton(self):
        """GetInstance() always returns the same object."""
        reg1 = UsdProfiles.CapabilityRegistry.GetInstance()
        reg2 = UsdProfiles.CapabilityRegistry.GetInstance()
        self.assertIs(reg1, reg2)

    def test_CapabilityRegistryKnownCapabilities(self):
        """Well-known capabilities declared in usdProfiles plugInfo are found."""
        reg = UsdProfiles.CapabilityRegistry.GetInstance()

        # These are declared in pxr/usd/usdProfiles/plugInfo.json.
        for cap in ["usd", "usd.core", "com.nvidia.simready"]:
            self.assertTrue(reg.IsCapability(cap),
                            f"Expected '{cap}' to be a known capability")

    def test_CapabilityRegistryPredecessors(self):
        """Direct predecessors are correctly returned."""
        reg = UsdProfiles.CapabilityRegistry.GetInstance()

        preds = reg.GetPredecessors("usd.core")
        self.assertIn("usd", preds,
                      "usd.core should have 'usd' as a direct predecessor")

    def test_CapabilityRegistryTransitivePredecessors(self):
        """Transitive predecessors are returned correctly."""
        reg = UsdProfiles.CapabilityRegistry.GetInstance()

        transitive = reg.GetTransitivePredecessors(
            "com.nvidia.simready.physics.rigidBodies")

        # Should include com.nvidia.simready.physics and com.nvidia.simready and usd.
        self.assertIn("com.nvidia.simready.physics", transitive)
        self.assertIn("com.nvidia.simready", transitive)
        self.assertIn("usd", transitive)

    def test_CapabilityRegistryIsProfile(self):
        """Capabilities tagged isProfile=true are identified as profiles."""
        reg = UsdProfiles.CapabilityRegistry.GetInstance()

        # Profiles declared in plugInfo.json.
        self.assertTrue(reg.IsProfile("usd.core.v25_05"))
        self.assertTrue(reg.IsProfile("aousd.interchange.v1_0"))
        self.assertTrue(
            reg.IsProfile("com.nvidia.simready.prop_robotics_neutral"))

        # Regular capabilities are not profiles.
        self.assertFalse(reg.IsProfile("usd"))
        self.assertFalse(reg.IsProfile("com.nvidia.simready"))

    def test_CapabilityRegistryGetAllProfiles(self):
        """GetAllProfiles lists only profile-tagged capabilities."""
        reg = UsdProfiles.CapabilityRegistry.GetInstance()
        profiles = reg.GetAllProfiles()

        self.assertIn("usd.core.v25_05", profiles)
        self.assertNotIn("usd", profiles)
        self.assertNotIn("com.nvidia.simready", profiles)

    # ---------------------------------------------------------------------- #
    # ProfileRegistry
    # ---------------------------------------------------------------------- #

    def test_ProfileRegistryIsSingleton(self):
        """GetInstance() always returns the same object."""
        reg1 = UsdProfiles.ProfileRegistry.GetInstance()
        reg2 = UsdProfiles.ProfileRegistry.GetInstance()
        self.assertIs(reg1, reg2)

    def test_ProfileRegistryIsProfile(self):
        """ProfileRegistry.IsProfile delegates to CapabilityRegistry."""
        reg = UsdProfiles.ProfileRegistry.GetInstance()
        self.assertTrue(reg.IsProfile("usd.core.v25_05"))
        self.assertFalse(reg.IsProfile("usd"))

    def test_ProfileRegistryGetProfileCapabilities(self):
        """GetProfileCapabilities returns profile + all transitive predecessors."""
        reg = UsdProfiles.ProfileRegistry.GetInstance()

        caps = reg.GetProfileCapabilities("usd.core.v25_05")
        # Profile itself should be in the list.
        self.assertIn("usd.core.v25_05", caps)
        # Transitive predecessors should be included.
        self.assertIn("usd.core", caps)
        self.assertIn("usd", caps)

    def test_ProfileRegistryGetProfileCapabilitiesNonProfile(self):
        """GetProfileCapabilities returns empty for non-profile capabilities."""
        reg = UsdProfiles.ProfileRegistry.GetInstance()
        caps = reg.GetProfileCapabilities("usd")
        self.assertEqual(caps, [])

    # ---------------------------------------------------------------------- #
    # Queries
    # ---------------------------------------------------------------------- #

    def test_GetEffectiveProfileDirect(self):
        """GetEffectiveProfile returns the authored profile on the prim itself."""
        stage = Usd.Stage.CreateInMemory()
        prim = stage.DefinePrim("/Robot", "Xform")
        api = UsdProfiles.ProfileAPI.Apply(prim)
        api.GetProfileAttr().Set("com.nvidia.simready.prop_robotics_neutral")

        profile = UsdProfiles.GetEffectiveProfile(prim)
        self.assertEqual(profile, "com.nvidia.simready.prop_robotics_neutral")

    def test_GetEffectiveProfileInheritedFromAncestor(self):
        """GetEffectiveProfile resolves to nearest ancestor's profile."""
        stage = Usd.Stage.CreateInMemory()

        parent = stage.DefinePrim("/Parent", "Xform")
        api = UsdProfiles.ProfileAPI.Apply(parent)
        api.GetProfileAttr().Set("usd.core.v25_05")

        child = stage.DefinePrim("/Parent/Child", "Xform")
        grandchild = stage.DefinePrim("/Parent/Child/Grandchild", "Xform")

        self.assertEqual(
            UsdProfiles.GetEffectiveProfile(child), "usd.core.v25_05")
        self.assertEqual(
            UsdProfiles.GetEffectiveProfile(grandchild), "usd.core.v25_05")

    def test_GetEffectiveProfileNone(self):
        """GetEffectiveProfile returns empty token when no ProfileAPI exists."""
        stage = Usd.Stage.CreateInMemory()
        prim = stage.DefinePrim("/Orphan", "Xform")
        self.assertEqual(UsdProfiles.GetEffectiveProfile(prim), "")

    def test_GetEffectiveProfileOverride(self):
        """A child's ProfileAPI overrides the ancestor's."""
        stage = Usd.Stage.CreateInMemory()

        parent = stage.DefinePrim("/Parent", "Xform")
        parent_api = UsdProfiles.ProfileAPI.Apply(parent)
        parent_api.GetProfileAttr().Set("usd.core.v25_05")

        child = stage.DefinePrim("/Parent/Child", "Xform")
        child_api = UsdProfiles.ProfileAPI.Apply(child)
        child_api.GetProfileAttr().Set(
            "com.nvidia.simready.prop_robotics_neutral")

        self.assertEqual(
            UsdProfiles.GetEffectiveProfile(parent), "usd.core.v25_05")
        self.assertEqual(
            UsdProfiles.GetEffectiveProfile(child),
            "com.nvidia.simready.prop_robotics_neutral")

    def test_GetEffectiveCapabilities(self):
        """GetEffectiveCapabilities returns capabilities for the effective profile."""
        stage = Usd.Stage.CreateInMemory()
        prim = stage.DefinePrim("/Root", "Xform")
        api = UsdProfiles.ProfileAPI.Apply(prim)
        api.GetProfileAttr().Set("usd.core.v25_05")

        caps = UsdProfiles.GetEffectiveCapabilities(prim)
        self.assertIn("usd.core.v25_05", caps)
        self.assertIn("usd.core", caps)
        self.assertIn("usd", caps)

    def test_GetEffectiveCapabilitiesEmpty(self):
        """GetEffectiveCapabilities returns [] when no ProfileAPI exists."""
        stage = Usd.Stage.CreateInMemory()
        prim = stage.DefinePrim("/NoPrfoile", "Xform")
        caps = UsdProfiles.GetEffectiveCapabilities(prim)
        self.assertEqual(caps, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
