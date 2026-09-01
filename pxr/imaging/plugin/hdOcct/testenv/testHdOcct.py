#!/usr/bin/env python3
"""
Test suite for hdOcct: OCCT tessellation plugin for OpenUSD.
Tests both the CLI tool (usdsolidtessellate) and the Hydra rendering path.
"""
import os
import sys
import subprocess
import tempfile
import unittest
from pathlib import Path

# Ensure USD Python modules are available
# The OpenUSD install to test against. No default that could be right for
# someone else's checkout, so this has to be set.
USD_INSTALL = os.environ.get("USD_INSTALL_DIR", "")
REPO_ROOT = Path(__file__).resolve().parents[5]
sys.path.insert(0, os.path.join(USD_INSTALL, "lib", "python"))

from pxr import Usd, UsdGeom, Vt, Gf, Sdf


class TestCliTool(unittest.TestCase):
    """Tests for the usdsolidtessellate command-line tool."""

    TOOL = os.environ.get("USDSOLIDTESSELLATE",
        os.path.join(USD_INSTALL, "bin", "usdsolidtessellate"))
    TURBINE = os.environ.get("TEST_ASSET_TURBINE", "")

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(cls.TOOL):
            raise unittest.SkipTest(f"CLI tool not found: {cls.TOOL}")
        if not os.path.exists(cls.TURBINE):
            raise unittest.SkipTest(f"Test asset not found: {cls.TURBINE}")

    def _run_tool(self, input_usd, output_path, prim_path="/World/Brep0"):
        """Run usdsolidtessellate and return (exit_code, stdout, stderr)."""
        env = os.environ.copy()
        env["LD_LIBRARY_PATH"] = (
            f"{USD_INSTALL}/lib:"
            f"{os.path.join(USD_INSTALL, 'plugin', 'usd')}:"
            + env.get("LD_LIBRARY_PATH", "")
        )
        result = subprocess.run(
            [self.TOOL, input_usd, output_path, prim_path],
            env=env, capture_output=True, text=True, timeout=30
        )
        return result.returncode, result.stdout, result.stderr

    def test_turbine_tessellation_usda(self):
        """Tessellate TurbineFan.usd to USDA format."""
        with tempfile.NamedTemporaryFile(suffix=".usda", delete=False) as f:
            output = f.name
        try:
            rc, stdout, _ = self._run_tool(self.TURBINE, output)
            self.assertEqual(rc, 0, f"Tool failed: {stdout}")
            self.assertIn("25 bodies", stdout)
            self.assertIn("2124 total verts", stdout)
            self.assertTrue(os.path.exists(output))
            self.assertGreater(os.path.getsize(output), 1000)
        finally:
            os.unlink(output)

    def test_turbine_tessellation_usdc(self):
        """Tessellate TurbineFan.usd to USDC (binary crate) format."""
        with tempfile.NamedTemporaryFile(suffix=".usdc", delete=False) as f:
            output = f.name
        try:
            rc, stdout, _ = self._run_tool(self.TURBINE, output)
            self.assertEqual(rc, 0, f"Tool failed: {stdout}")
            self.assertIn("25 bodies", stdout)
            # USDC should be more compact
            self.assertGreater(os.path.getsize(output), 1000)
            self.assertLess(os.path.getsize(output), 200000)  # ~53KB expected
        finally:
            os.unlink(output)

    def test_mesh_validity(self):
        """Verify output meshes have consistent topology."""
        with tempfile.NamedTemporaryFile(suffix=".usdc", delete=False) as f:
            output = f.name
        try:
            rc, _, _ = self._run_tool(self.TURBINE, output)
            self.assertEqual(rc, 0)

            stage = Usd.Stage.Open(output)
            self.assertTrue(stage)

            mesh_count = 0
            total_verts = 0
            for prim in stage.Traverse():
                if prim.GetTypeName() == "Mesh":
                    mesh_count += 1
                    mesh = UsdGeom.Mesh(prim)
                    pts = mesh.GetPointsAttr().Get()
                    fvi = mesh.GetFaceVertexIndicesAttr().Get()
                    fvc = mesh.GetFaceVertexCountsAttr().Get()

                    self.assertIsNotNone(pts)
                    self.assertIsNotNone(fvi)
                    self.assertIsNotNone(fvc)
                    self.assertGreater(len(pts), 0)

                    # All triangles
                    for c in fvc:
                        self.assertEqual(c, 3, "Expected all-triangle mesh")

                    # No out-of-bounds indices
                    max_idx = max(fvi)
                    self.assertLess(max_idx, len(pts),
                        f"Index {max_idx} >= point count {len(pts)} in {prim.GetPath()}")

                    # No unused vertices (compaction check)
                    referenced = set(fvi)
                    self.assertEqual(len(referenced), len(pts),
                        f"{len(pts) - len(referenced)} unreferenced vertices in {prim.GetPath()}")

                    total_verts += len(pts)

            self.assertEqual(mesh_count, 25, "Expected 25 meshes from TurbineFan")
        finally:
            os.unlink(output)

    def test_normals_present(self):
        """Verify tessellated meshes have per-vertex normals."""
        with tempfile.NamedTemporaryFile(suffix=".usdc", delete=False) as f:
            output = f.name
        try:
            rc, _, _ = self._run_tool(self.TURBINE, output)
            self.assertEqual(rc, 0)

            stage = Usd.Stage.Open(output)
            for prim in stage.Traverse():
                if prim.GetTypeName() == "Mesh":
                    mesh = UsdGeom.Mesh(prim)
                    pts = mesh.GetPointsAttr().Get()
                    primvars = UsdGeom.PrimvarsAPI(prim)
                    normals_pv = primvars.GetPrimvar("normals")
                    # Check both primvar normals and built-in normals attr
                    normals = None
                    if normals_pv.IsDefined():
                        normals = normals_pv.Get()
                    else:
                        normals_attr = mesh.GetNormalsAttr()
                        if normals_attr and normals_attr.HasValue():
                            normals = normals_attr.Get()
                    self.assertIsNotNone(normals,
                        f"No normals on {prim.GetPath()}")
                    self.assertEqual(len(normals), len(pts),
                        f"Normals count mismatch on {prim.GetPath()}")
                    # Verify normals are unit-length (tolerance for float)
                    for n in normals[:5]:
                        length = Gf.Vec3f(n).GetLength()
                        self.assertAlmostEqual(length, 1.0, places=2,
                            msg=f"Non-unit normal {n} on {prim.GetPath()}")
                    break  # Just check first mesh
        finally:
            os.unlink(output)

    def test_invalid_prim_path(self):
        """Tool should handle non-existent prim paths gracefully."""
        with tempfile.NamedTemporaryFile(suffix=".usda", delete=False) as f:
            output = f.name
        try:
            rc, stdout, _ = self._run_tool(
                self.TURBINE, output, "/World/NonExistent")
            # Should fail gracefully (non-zero exit or 0 bodies)
            # Exact behavior depends on implementation
            self.assertTrue(
                rc != 0 or "0 bodies" in stdout or "Error" in stdout,
                "Should fail for non-existent prim")
        finally:
            if os.path.exists(output):
                os.unlink(output)


class TestHydraRendering(unittest.TestCase):
    """Tests for the hdOcct Hydra rendering path via usdrecord."""

    TURBINE = os.environ.get("TEST_ASSET_TURBINE", "")
    USDRECORD = os.path.join(
        str(REPO_ROOT), "pxr/usdImaging/bin/usdrecord/usdrecord.py")

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(cls.TURBINE):
            raise unittest.SkipTest(f"Test asset not found: {cls.TURBINE}")
        if not os.path.exists(cls.USDRECORD):
            raise unittest.SkipTest(f"usdrecord not found: {cls.USDRECORD}")
        # Check DISPLAY is set
        if not os.environ.get("DISPLAY"):
            raise unittest.SkipTest("No DISPLAY set (need Xvfb)")

    def _render(self, input_usd, output_jpg, width=960):
        """Render via usdrecord and return (exit_code, stderr)."""
        env = os.environ.copy()
        env.update({
            "LD_LIBRARY_PATH": (
                f"{USD_INSTALL}/lib:"
                f"{os.path.join(USD_INSTALL, 'plugin', 'usd')}:"
                + env.get("LD_LIBRARY_PATH", "")
            ),
            "PYTHONPATH": f"{USD_INSTALL}/lib/python",
            "PXR_PLUGINPATH_NAME": f"{USD_INSTALL}/plugin/usd/",
            "HDGP_INCLUDE_DEFAULT_RESOLVER": "1",
            "QT_QPA_PLATFORM": "offscreen",
        })
        result = subprocess.run(
            [sys.executable, self.USDRECORD,
             "--renderer", "Storm",
             "--imageWidth", str(width),
             input_usd, output_jpg],
            env=env, capture_output=True, text=True, timeout=60
        )
        return result.returncode, result.stderr

    def test_render_produces_image(self):
        """Hydra renders TurbineFan to a non-empty image."""
        with tempfile.NamedTemporaryFile(suffix=".jpg", delete=False) as f:
            output = f.name
        try:
            rc, _ = self._render(self.TURBINE, output)
            self.assertEqual(rc, 0)
            self.assertTrue(os.path.exists(output))
            self.assertGreater(os.path.getsize(output), 10000,
                "Image too small — likely empty/black")
        finally:
            if os.path.exists(output):
                os.unlink(output)

    def test_render_has_content(self):
        """Rendered image contains visible geometry (not all-black)."""
        try:
            from PIL import Image
            import numpy as np
        except ImportError:
            raise unittest.SkipTest("PIL/numpy not available")

        with tempfile.NamedTemporaryFile(suffix=".jpg", delete=False) as f:
            output = f.name
        try:
            rc, _ = self._render(self.TURBINE, output)
            self.assertEqual(rc, 0)

            img = np.array(Image.open(output))
            # Check that there are non-black pixels (turbine geometry)
            non_black = (img.sum(axis=2) > 30).sum()
            self.assertGreater(non_black, 1000,
                "Image appears all-black — tessellation/rendering failed")
        finally:
            if os.path.exists(output):
                os.unlink(output)

    def test_no_vertex_primvar_warnings(self):
        """Render should not produce vertex primvar mismatch warnings."""
        with tempfile.NamedTemporaryFile(suffix=".jpg", delete=False) as f:
            output = f.name
        try:
            rc, stderr = self._render(self.TURBINE, output)
            self.assertEqual(rc, 0)
            self.assertNotIn("_PopulateVertexPrimvars", stderr,
                "Vertex primvar mismatch warning still present")
        finally:
            if os.path.exists(output):
                os.unlink(output)


class TestSchemaRegistration(unittest.TestCase):
    """Tests that the UsdSolid schema is properly registered."""

    def test_brep_array_type_recognized(self):
        """USD runtime recognizes BrepArray as a valid prim type."""
        stage = Usd.Stage.Open(
            "")
        prim = stage.GetPrimAtPath("/World/Brep0")
        self.assertTrue(prim.IsValid())
        self.assertEqual(prim.GetTypeName(), "BrepArray")

    def test_brep_array_is_gprim(self):
        """BrepArray inherits from UsdGeomGprim (has extent, visibility)."""
        stage = Usd.Stage.Open(
            "")
        prim = stage.GetPrimAtPath("/World/Brep0")
        # Should be imageable (Gprim inherits Imageable)
        imageable = UsdGeom.Imageable(prim)
        self.assertTrue(imageable)
        # Should support extent computation
        self.assertTrue(prim.HasAttribute("extent") or
                        prim.GetAttribute("extent").IsValid())


if __name__ == "__main__":
    unittest.main(verbosity=2)
