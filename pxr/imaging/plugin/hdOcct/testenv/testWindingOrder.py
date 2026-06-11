#!/usr/bin/env python3
"""
Winding order ground truth test for hdOcct tessellator.

Uses OCCT's canonical convention (from StdPrs_ShadedShape.cxx and
IVtkOCC_ShapeMesher.cxx):
  - Normal flip: if face.Orientation() == TopAbs_REVERSED → reverse normal
  - Winding swap: if face.Orientation() == TopAbs_REVERSED → swap n2, n3

Ground truth assertion: for a closed solid (cube, sphere, cylinder),
the geometric normal derived from winding order (cross product of two
triangle edges) should point OUTWARD from the solid's centroid.

This test is independent of shading — it verifies topological correctness
of the tessellation, which is a prerequisite for correct rendering.
"""
import os
import sys
import tempfile
import unittest
import numpy as np

_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))

USD_INSTALL = os.environ.get("USD_INSTALL_DIR", "")
if USD_INSTALL:
    sys.path.insert(0, os.path.join(USD_INSTALL, "lib", "python"))

from pxr import Usd, UsdGeom, Gf


class TestWindingOrder(unittest.TestCase):
    """
    Verify that tessellation produces outward-facing triangles for
    closed solids by checking that the geometric normal (from winding)
    points away from the body centroid.

    Ground truth: OCCT StdPrs_ShadedShape.cxx, IVtkOCC_ShapeMesher.cxx
    Both swap (n2, n3) when face.Orientation() == TopAbs_REVERSED.
    """

    TOOL = os.environ.get("USDSOLIDTESSELLATE", "usdsolidtessellate")
    # Use the test cube — a convex solid where all normals must point outward
    CUBE_ASSET = os.environ.get("TEST_CUBE_ASSET",
        os.path.join(_SCRIPT_DIR, "fixtures", "testCube.usda"))
    CUBE_PRIM_PATH = "/World/Cube"

    @classmethod
    def setUpClass(cls):
        if not os.path.exists(cls.CUBE_ASSET):
            raise unittest.SkipTest(f"Cube asset not found: {cls.CUBE_ASSET}")

    def _tessellate_to_stage(self, input_usd, prim_path=None):
        """Tessellate via CLI and return (stage, list_of_mesh_prims)."""
        import subprocess
        with tempfile.NamedTemporaryFile(suffix=".usdc", delete=False) as f:
            output = f.name
        try:
            env = os.environ.copy()
            cmd = [self.TOOL, input_usd, output]
            if prim_path:
                cmd.append(prim_path)
            result = subprocess.run(
                cmd,
                env=env, capture_output=True, text=True, timeout=30
            )
            self.assertEqual(result.returncode, 0,
                f"Tessellation failed: {result.stderr}")
            stage = Usd.Stage.Open(output)
            meshes = []
            for prim in stage.Traverse():
                if prim.IsA(UsdGeom.Mesh):
                    meshes.append(prim)
            return stage, meshes
        except Exception:
            if os.path.exists(output):
                os.unlink(output)
            raise

    def _get_mesh_data(self, mesh_prim):
        """Extract points and face vertex indices from a mesh prim."""
        mesh = UsdGeom.Mesh(mesh_prim)
        points = np.array(mesh.GetPointsAttr().Get(), dtype=np.float64)
        fvc = np.array(mesh.GetFaceVertexCountsAttr().Get())
        fvi = np.array(mesh.GetFaceVertexIndicesAttr().Get())
        return points, fvc, fvi

    def _compute_triangle_normals(self, points, fvi):
        """
        Compute geometric normals from triangle winding order.
        Returns (normals, centroids) arrays — one per triangle.
        """
        assert len(fvi) % 3 == 0, "Expected all triangles"
        n_tris = len(fvi) // 3
        normals = np.zeros((n_tris, 3))
        centroids = np.zeros((n_tris, 3))

        for i in range(n_tris):
            i0, i1, i2 = fvi[i*3], fvi[i*3+1], fvi[i*3+2]
            p0, p1, p2 = points[i0], points[i1], points[i2]
            # Cross product of edges gives geometric normal
            edge1 = p1 - p0
            edge2 = p2 - p0
            normal = np.cross(edge1, edge2)
            mag = np.linalg.norm(normal)
            if mag > 1e-10:
                normals[i] = normal / mag
            centroids[i] = (p0 + p1 + p2) / 3.0

        return normals, centroids

    def test_cube_winding_outward(self):
        """
        For a unit cube: ALL triangle geometric normals (from winding)
        must point outward from the body centroid.

        This is the strongest possible winding-order test for a convex solid.
        The dot product of (triangle_centroid - body_centroid) with the
        geometric normal must be POSITIVE for every triangle.
        """
        stage, meshes = self._tessellate_to_stage(
            self.CUBE_ASSET, prim_path=self.CUBE_PRIM_PATH)
        self.assertGreater(len(meshes), 0, "No meshes produced")

        total_tris = 0
        total_outward = 0
        total_inward = 0

        # Compute body centroid across all meshes
        all_points = []
        for mesh_prim in meshes:
            points, fvc, fvi = self._get_mesh_data(mesh_prim)
            all_points.append(points)
        body_centroid = np.vstack(all_points).mean(axis=0)

        for mesh_prim in meshes:
            points, fvc, fvi = self._get_mesh_data(mesh_prim)
            # Verify all faces are triangles
            self.assertTrue(np.all(fvc == 3),
                f"Non-triangle face in {mesh_prim.GetPath()}")

            normals, centroids = self._compute_triangle_normals(points, fvi)
            n_tris = len(normals)
            total_tris += n_tris

            for i in range(n_tris):
                # Vector from body centroid to triangle centroid
                to_surface = centroids[i] - body_centroid
                # Dot with geometric normal: positive = outward
                dot = np.dot(normals[i], to_surface)
                if dot > 0:
                    total_outward += 1
                else:
                    total_inward += 1

        # For a convex solid (cube), ALL triangles must face outward
        outward_ratio = total_outward / total_tris if total_tris > 0 else 0
        self.assertEqual(total_inward, 0,
            f"WINDING ORDER ERROR: {total_inward}/{total_tris} triangles "
            f"have inward-facing geometric normals (outward ratio: "
            f"{outward_ratio:.1%}). Expected 100% outward for convex solid.")

    def test_cube_normals_consistent_with_winding(self):
        """
        Vertex normals stored in the mesh should be consistent with the
        geometric normal derived from winding order.

        For a correctly-wound mesh, the dot product of each stored vertex
        normal with the triangle's geometric normal (from winding) should
        be positive.
        """
        stage, meshes = self._tessellate_to_stage(
            self.CUBE_ASSET, prim_path=self.CUBE_PRIM_PATH)
        self.assertGreater(len(meshes), 0)

        total_consistent = 0
        total_checked = 0

        for mesh_prim in meshes:
            mesh = UsdGeom.Mesh(mesh_prim)
            points, fvc, fvi = self._get_mesh_data(mesh_prim)
            # Get stored normals (from normals attr or primvar)
            stored_normals = mesh.GetNormalsAttr().Get()
            if stored_normals is None:
                from pxr import UsdGeom as UG
                pv_api = UG.PrimvarsAPI(mesh_prim)
                npv = pv_api.GetPrimvar("normals")
                if npv and npv.IsDefined():
                    stored_normals = npv.Get()
            if stored_normals is None:
                continue  # Skip meshes without normals

            stored_normals = np.array(stored_normals, dtype=np.float64)
            geo_normals, _ = self._compute_triangle_normals(points, fvi)
            n_tris = len(fvi) // 3

            for i in range(n_tris):
                i0 = fvi[i*3]
                # Compare stored vertex normal with geometric normal
                if i0 < len(stored_normals):
                    dot = np.dot(stored_normals[i0], geo_normals[i])
                    total_checked += 1
                    if dot > 0:
                        total_consistent += 1

        if total_checked > 0:
            consistency = total_consistent / total_checked
            self.assertGreater(consistency, 0.95,
                f"Stored normals inconsistent with winding: "
                f"{consistency:.1%} agree (expected >95%)")

    def test_turbine_winding_outward(self):
        """
        For TurbineFan (compound of 25 bodies): verify winding order is
        consistent with stored normals. This is the robust test — centroid-
        based outward checks fail for thin bodies (fan blades) where the
        centroid lies on the face plane.

        Ground truth: geometric normal from cross(edge1, edge2) should
        agree (positive dot) with stored vertex normal for >95% of tris.
        """
        turbine = os.environ.get("TEST_ASSET_TURBINE")
        if not turbine or not os.path.exists(turbine):
            raise unittest.SkipTest("TEST_ASSET_TURBINE not set")

        stage, meshes = self._tessellate_to_stage(
            turbine, prim_path="/World/Brep0")
        self.assertGreater(len(meshes), 0)

        total_agree = 0
        total_checked = 0

        for mesh_prim in meshes:
            mesh = UsdGeom.Mesh(mesh_prim)
            points, fvc, fvi = self._get_mesh_data(mesh_prim)
            stored_normals = mesh.GetNormalsAttr().Get()
            if stored_normals is None:
                continue
            stored_normals = np.array(stored_normals, dtype=np.float64)
            n_tris = len(fvi) // 3

            for i in range(n_tris):
                i0, i1, i2 = fvi[i*3], fvi[i*3+1], fvi[i*3+2]
                p0, p1, p2 = points[i0], points[i1], points[i2]
                geo_normal = np.cross(p1 - p0, p2 - p0)
                mag = np.linalg.norm(geo_normal)
                if mag < 1e-10:
                    continue
                geo_normal /= mag
                if i0 < len(stored_normals):
                    dot = np.dot(geo_normal, stored_normals[i0])
                    total_checked += 1
                    if dot > 0:
                        total_agree += 1

        agreement = total_agree / total_checked if total_checked > 0 else 0
        self.assertGreater(agreement, 0.95,
            f"Winding inconsistent with normals: {agreement:.1%} agree "
            f"(expected >95%). {total_checked - total_agree} of "
            f"{total_checked} triangles have winding opposite to normals.")


if __name__ == "__main__":
    unittest.main()
