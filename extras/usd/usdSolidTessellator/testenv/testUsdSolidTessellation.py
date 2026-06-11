#!/pxrpythonsubst
#
# Copyright 2024 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

"""Tests for the usdSolidTessellator CLI tool and tessellation correctness.

Requires the usdsolidtessellate binary to be built and available.
Uses the 10 test fixtures in testenv/fixtures/.
"""

from pxr import Usd, UsdGeom, Vt, Gf
import os
import subprocess
import tempfile
import unittest
import math


# Expected vertex counts per fixture (regression baseline).
# These are stable because the fixtures are deterministic OCCT geometry.
EXPECTED_VERT_COUNTS = {
    'testCone.usda': 466,
    'testCube.usda': 24,
    'testCubeWithHole.usda': 82,
    'testCylinder.usda': 328,
    'testFilletedCube.usda': 344,
    'testFilletedCubeWithHole.usda': 268,
    'testPlane.usda': 4,
    'testPlaneWithHole.usda': 33,
    'testSphere.usda': 653,
    'testTwoBoxes.usda': 48,
}

# Prim paths in each fixture
PRIM_PATHS = {
    'testCone.usda': '/World/Cone',
    'testCube.usda': '/World/Cube',
    'testCubeWithHole.usda': '/World/CubeWithHole',
    'testCylinder.usda': '/World/Cylinder',
    'testFilletedCube.usda': '/World/FilletedCube',
    'testFilletedCubeWithHole.usda': '/World/FilletedCubeWithHole',
    'testPlane.usda': '/World/Plane',
    'testPlaneWithHole.usda': '/World/PlaneWithHole',
    'testSphere.usda': '/World/Sphere',
    'testTwoBoxes.usda': '/World/TwoBoxes',
}

# Closed solids (should have positive signed volume)
CLOSED_SOLIDS = [
    'testCone.usda',
    'testCube.usda',
    'testCubeWithHole.usda',
    'testCylinder.usda',
    'testFilletedCube.usda',
    'testFilletedCubeWithHole.usda',
    'testSphere.usda',
    'testTwoBoxes.usda',
]


def _get_fixtures_dir():
    """Return path to the fixtures directory."""
    return os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        'fixtures')


def _get_tessellator_bin():
    """Find the usdsolidtessellate binary."""
    # Check common locations
    candidates = [
        os.path.join(os.environ.get('USD_INSTALL_ROOT', ''), 'bin',
                     'usdsolidtessellate'),
        os.path.join(os.path.dirname(os.path.abspath(__file__)),
                     '..', '..', '..', '..', '..', 'usd-install', 'bin',
                     'usdsolidtessellate'),
    ]
    # Also check PATH
    from shutil import which
    path_bin = which('usdsolidtessellate')
    if path_bin:
        candidates.insert(0, path_bin)

    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c

    # Fallback: use the env var that CTest would set
    return os.environ.get('USDSOLIDTESSELLATE_BIN', 'usdsolidtessellate')


def _run_tessellator(input_path, output_path, prim_path):
    """Run the tessellator CLI and return (returncode, stdout, stderr)."""
    bin_path = _get_tessellator_bin()
    env = os.environ.copy()
    result = subprocess.run(
        [bin_path, input_path, output_path, prim_path],
        env=env, capture_output=True, text=True, timeout=30)
    return result.returncode, result.stdout, result.stderr


def _tessellate_fixture(fixture_name):
    """Tessellate a fixture and return the output stage."""
    fixtures_dir = _get_fixtures_dir()
    input_path = os.path.join(fixtures_dir, fixture_name)
    prim_path = PRIM_PATHS[fixture_name]

    with tempfile.NamedTemporaryFile(suffix='.usda', delete=False) as f:
        output_path = f.name

    rc, stdout, stderr = _run_tessellator(input_path, output_path, prim_path)
    if rc != 0:
        raise RuntimeError(
            f"Tessellation failed for {fixture_name}: {stdout} {stderr}")

    stage = Usd.Stage.Open(output_path)
    os.unlink(output_path)
    return stage


def _signed_volume(points, face_vertex_counts, face_vertex_indices):
    """Compute signed volume of a triangle mesh (positive = outward normals)."""
    volume = 0.0
    idx = 0
    for count in face_vertex_counts:
        if count != 3:
            # Skip non-triangles for volume calc
            idx += count
            continue
        i0 = face_vertex_indices[idx]
        i1 = face_vertex_indices[idx + 1]
        i2 = face_vertex_indices[idx + 2]
        v0 = points[i0]
        v1 = points[i1]
        v2 = points[i2]
        # Signed volume contribution = v0 . (v1 x v2) / 6
        volume += (v0[0] * (v1[1] * v2[2] - v1[2] * v2[1]) +
                   v0[1] * (v1[2] * v2[0] - v1[0] * v2[2]) +
                   v0[2] * (v1[0] * v2[1] - v1[1] * v2[0]))
        idx += count
    return volume / 6.0


class TestTessellationVertexCounts(unittest.TestCase):
    """Each fixture produces the expected vertex count (regression)."""

    def _check_fixture(self, fixture_name):
        stage = _tessellate_fixture(fixture_name)
        meshes = [UsdGeom.Mesh(p) for p in stage.TraverseAll()
                  if p.GetTypeName() == 'Mesh']
        total_verts = sum(
            len(m.GetPointsAttr().Get()) for m in meshes)
        expected = EXPECTED_VERT_COUNTS[fixture_name]
        self.assertEqual(total_verts, expected,
                         f"{fixture_name}: expected {expected} verts, "
                         f"got {total_verts}")

    def test_Cube(self):
        self._check_fixture('testCube.usda')

    def test_Sphere(self):
        self._check_fixture('testSphere.usda')

    def test_Cylinder(self):
        self._check_fixture('testCylinder.usda')

    def test_Cone(self):
        self._check_fixture('testCone.usda')

    def test_CubeWithHole(self):
        self._check_fixture('testCubeWithHole.usda')

    def test_FilletedCube(self):
        self._check_fixture('testFilletedCube.usda')

    def test_FilletedCubeWithHole(self):
        self._check_fixture('testFilletedCubeWithHole.usda')

    def test_Plane(self):
        self._check_fixture('testPlane.usda')

    def test_PlaneWithHole(self):
        self._check_fixture('testPlaneWithHole.usda')

    def test_TwoBoxes(self):
        self._check_fixture('testTwoBoxes.usda')


class TestTessellationWindingOrder(unittest.TestCase):
    """Signed volume is positive for all closed solids (correct winding)."""

    def _check_winding(self, fixture_name):
        stage = _tessellate_fixture(fixture_name)
        meshes = [UsdGeom.Mesh(p) for p in stage.TraverseAll()
                  if p.GetTypeName() == 'Mesh']
        self.assertTrue(len(meshes) > 0,
                        f"{fixture_name}: no meshes produced")

        for mesh in meshes:
            points = mesh.GetPointsAttr().Get()
            fvc = mesh.GetFaceVertexCountsAttr().Get()
            fvi = mesh.GetFaceVertexIndicesAttr().Get()
            vol = _signed_volume(points, fvc, fvi)
            self.assertGreater(vol, 0,
                               f"{fixture_name}: signed volume {vol} <= 0 "
                               f"(bad winding)")

    def test_Cube(self):
        self._check_winding('testCube.usda')

    def test_Sphere(self):
        self._check_winding('testSphere.usda')

    def test_Cylinder(self):
        self._check_winding('testCylinder.usda')

    def test_Cone(self):
        self._check_winding('testCone.usda')

    def test_CubeWithHole(self):
        self._check_winding('testCubeWithHole.usda')

    def test_TwoBoxes(self):
        self._check_winding('testTwoBoxes.usda')


class TestTessellationMeshProperties(unittest.TestCase):
    """Output meshes have correct properties (normals, subdivision scheme)."""

    def test_NormalsPresent(self):
        """All meshes have normals with correct length."""
        stage = _tessellate_fixture('testSphere.usda')
        meshes = [UsdGeom.Mesh(p) for p in stage.TraverseAll()
                  if p.GetTypeName() == 'Mesh']
        for mesh in meshes:
            normals = mesh.GetNormalsAttr().Get()
            points = mesh.GetPointsAttr().Get()
            self.assertIsNotNone(normals,
                                 "Normals attribute is None")
            self.assertEqual(len(normals), len(points),
                             "Normals count != points count")

    def test_NormalsUnitLength(self):
        """All normals are approximately unit length."""
        stage = _tessellate_fixture('testCube.usda')
        meshes = [UsdGeom.Mesh(p) for p in stage.TraverseAll()
                  if p.GetTypeName() == 'Mesh']
        for mesh in meshes:
            normals = mesh.GetNormalsAttr().Get()
            for n in normals:
                length = math.sqrt(n[0]**2 + n[1]**2 + n[2]**2)
                self.assertAlmostEqual(length, 1.0, places=4,
                                       msg=f"Normal {n} not unit length")

    def test_SubdivisionSchemeNone(self):
        """Output meshes have subdivisionScheme = 'none'."""
        stage = _tessellate_fixture('testCube.usda')
        meshes = [UsdGeom.Mesh(p) for p in stage.TraverseAll()
                  if p.GetTypeName() == 'Mesh']
        for mesh in meshes:
            scheme = mesh.GetSubdivisionSchemeAttr().Get()
            self.assertEqual(scheme, 'none',
                             f"Expected 'none', got '{scheme}'")

    def test_FaceCountsNonZero(self):
        """All fixtures produce at least one face."""
        for fixture_name in EXPECTED_VERT_COUNTS:
            stage = _tessellate_fixture(fixture_name)
            meshes = [UsdGeom.Mesh(p) for p in stage.TraverseAll()
                      if p.GetTypeName() == 'Mesh']
            total_faces = sum(
                len(m.GetFaceVertexCountsAttr().Get()) for m in meshes)
            self.assertGreater(total_faces, 0,
                               f"{fixture_name}: no faces produced")

    def test_ShapeFixApplied(self):
        """Cube produces 24 verts (all 6 faces tessellated, not 16)."""
        # This is a regression test for the ShapeFix_Shape fix.
        # Without ShapeFix, BRepMesh skips faces without pcurves.
        stage = _tessellate_fixture('testCube.usda')
        meshes = [UsdGeom.Mesh(p) for p in stage.TraverseAll()
                  if p.GetTypeName() == 'Mesh']
        total_verts = sum(len(m.GetPointsAttr().Get()) for m in meshes)
        # 24 = 6 faces × 4 verts each (triangulated: 12 triangles)
        self.assertEqual(total_verts, 24,
                         f"Expected 24 verts (6 faces), got {total_verts}. "
                         f"ShapeFix may not be applied.")


class TestTessellationErrorHandling(unittest.TestCase):
    """Error handling: bad inputs don't crash."""

    def test_NonexistentPrimPath(self):
        """Nonexistent prim path returns error code, no crash."""
        fixtures_dir = _get_fixtures_dir()
        input_path = os.path.join(fixtures_dir, 'testCube.usda')
        with tempfile.NamedTemporaryFile(suffix='.usda', delete=False) as f:
            output_path = f.name

        rc, stdout, stderr = _run_tessellator(
            input_path, output_path, '/World/DoesNotExist')
        os.unlink(output_path) if os.path.exists(output_path) else None
        self.assertNotEqual(rc, 0,
                            "Expected non-zero return for bad prim path")

    def test_NonexistentInputFile(self):
        """Nonexistent input file returns error code, no crash."""
        with tempfile.NamedTemporaryFile(suffix='.usda', delete=False) as f:
            output_path = f.name

        rc, stdout, stderr = _run_tessellator(
            '/tmp/does_not_exist_xyz.usda', output_path, '/World/Cube')
        os.unlink(output_path) if os.path.exists(output_path) else None
        self.assertNotEqual(rc, 0,
                            "Expected non-zero return for missing input file")


if __name__ == '__main__':
    unittest.main()
