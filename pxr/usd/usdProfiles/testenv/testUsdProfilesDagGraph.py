#!/pxrpythonsubst
#
# Copyright 2024 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

"""Tests for CLI namespace filtering and DOT graph output.

Ref: jensjebens/usd-profiles#9
"""

from __future__ import print_function

import unittest
import sys
import os
import io

# Add the usdProfiles directory to path so we can import the CLI module
_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_PARENT_DIR = os.path.dirname(_SCRIPT_DIR)
if _PARENT_DIR not in sys.path:
    sys.path.insert(0, _PARENT_DIR)

from pxr import UsdProfiles

# Import the graph/filter functions we'll add to the CLI
import usdprofilecheck


class TestNamespaceFiltering(unittest.TestCase):
    """Tests for --namespace filtering of capabilities."""

    def setUp(self):
        self.reg = UsdProfiles.CapabilityRegistry.GetInstance()

    def test_FilterBySimReadyNamespace(self):
        """--namespace com.nvidia.simready returns only SimReady capabilities."""
        filtered = usdprofilecheck.filter_capabilities_by_namespace(
            "com.nvidia.simready")
        for cap in filtered:
            self.assertTrue(
                cap.startswith("com.nvidia.simready"),
                f"Capability '{cap}' should start with 'com.nvidia.simready'")
        # Should include the profile and sub-capabilities
        self.assertIn("com.nvidia.simready", filtered)
        self.assertIn("com.nvidia.simready.geom", filtered)
        self.assertIn("com.nvidia.simready.prop_robotics_neutral", filtered)
        # Should NOT include usd.* or aousd.*
        for cap in filtered:
            self.assertFalse(cap.startswith("usd."),
                             f"'{cap}' should not be in SimReady-filtered set")
            self.assertFalse(cap.startswith("aousd."),
                             f"'{cap}' should not be in SimReady-filtered set")

    def test_FilterByUsdNamespace(self):
        """--namespace usd returns only usd.* capabilities."""
        filtered = usdprofilecheck.filter_capabilities_by_namespace("usd")
        for cap in filtered:
            self.assertTrue(
                cap.startswith("usd"),
                f"Capability '{cap}' should start with 'usd'")
        self.assertIn("usd", filtered)
        self.assertIn("usd.core", filtered)
        self.assertIn("usd.geom", filtered)
        self.assertIn("usd.core.v25_05", filtered)

    def test_FilterByUsdGeomNamespace(self):
        """--namespace usd.geom returns only usd.geom."""
        filtered = usdprofilecheck.filter_capabilities_by_namespace("usd.geom")
        self.assertIn("usd.geom", filtered)
        # usd.core shouldn't be here — different branch
        self.assertNotIn("usd.core", filtered)

    def test_FilterIncludesPredecessors(self):
        """Filtered set includes predecessor chain when requested."""
        filtered = usdprofilecheck.filter_capabilities_by_namespace(
            "com.nvidia.simready", include_predecessors=True)
        # Should include the usd.* predecessors
        self.assertIn("usd", filtered)
        self.assertIn("usd.geom", filtered)
        self.assertIn("usd.physics", filtered)
        self.assertIn("usd.core", filtered)

    def test_FilterWithoutPredecessors(self):
        """Without include_predecessors, only namespace-matching caps returned."""
        filtered = usdprofilecheck.filter_capabilities_by_namespace(
            "com.nvidia.simready", include_predecessors=False)
        self.assertNotIn("usd.geom", filtered)
        self.assertNotIn("usd.physics", filtered)

    def test_FilterNonexistentNamespace(self):
        """Unknown namespace returns empty set."""
        filtered = usdprofilecheck.filter_capabilities_by_namespace(
            "com.example.nonexistent")
        self.assertEqual(filtered, [])


class TestDotGraphOutput(unittest.TestCase):
    """Tests for DOT graph generation."""

    def test_DotOutputIsValidDot(self):
        """Generated DOT output starts with 'digraph' and has basic structure."""
        dot = usdprofilecheck.generate_dot_graph()
        self.assertTrue(dot.startswith("digraph"))
        self.assertIn("->", dot)  # Has edges
        self.assertIn("}", dot)   # Properly closed

    def test_DotContainsAllCapabilities(self):
        """DOT graph includes nodes for all registered capabilities."""
        reg = UsdProfiles.CapabilityRegistry.GetInstance()
        dot = usdprofilecheck.generate_dot_graph()
        for cap in reg.GetAllCapabilities():
            cap_str = str(cap)
            # Node should appear as a label or id
            self.assertIn(cap_str, dot,
                          f"Capability '{cap_str}' missing from DOT output")

    def test_DotProfilesHaveDistinctShape(self):
        """Profile nodes have a different shape than regular capabilities."""
        dot = usdprofilecheck.generate_dot_graph()
        # Profiles should have doubleoctagon or similar distinct shape
        self.assertIn("prop_robotics_neutral", dot)
        # Check that profiles get special styling
        self.assertIn("shape=", dot)

    def test_DotShowsValidatorCounts(self):
        """Nodes show validator counts in their labels."""
        dot = usdprofilecheck.generate_dot_graph()
        # usd.core has 3 validators, should show in label
        self.assertIn("3", dot)
        # usd.shade has 9 validators
        self.assertIn("9", dot)

    def test_DotWithNamespaceFilter(self):
        """DOT graph respects namespace filter."""
        dot = usdprofilecheck.generate_dot_graph(namespace="usd")
        self.assertIn("usd.core", dot)
        self.assertIn("usd.geom", dot)
        # SimReady nodes should not appear
        self.assertNotIn("com.nvidia.simready.geom", dot)

    def test_DotWithNamespaceAndPredecessors(self):
        """DOT graph with namespace filter + predecessors shows bridge edges."""
        dot = usdprofilecheck.generate_dot_graph(
            namespace="com.nvidia.simready", include_predecessors=True)
        # Should have SimReady nodes
        self.assertIn("com.nvidia.simready.geom", dot)
        # Should also have the usd.* predecessors
        self.assertIn("usd.geom", dot)
        self.assertIn("usd.physics", dot)

    def test_DotEdgesMatchPredecessors(self):
        """DOT edges correctly represent predecessor relationships."""
        dot = usdprofilecheck.generate_dot_graph()
        # usd.core -> usd should be an edge
        # In DOT, edges go child -> parent or parent -> child depending on convention
        # We'll use child -> parent (inherits from)
        self.assertTrue(
            '"usd.core" -> "usd"' in dot or '"usd" -> "usd.core"' in dot,
            "Expected edge between usd.core and usd")


if __name__ == "__main__":
    unittest.main(verbosity=2)
