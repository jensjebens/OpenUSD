#!/pxrpythonsubst
#
# Copyright 2024 NVIDIA Corporation
# SPDX-License-Identifier: Apache-2.0

"""Tests for the usdsolidtessellate CLI and tessellation correctness.

Runs the CLI against the 10 fixtures in fixtures/ and checks vertex
counts, winding (signed volume), and mesh properties.

Vertex-count baselines were validated to be identical on two independent
configurations: Linux GCC 13 + OCCT 7.8.1 and Windows MSVC (VS 2026) +
OCCT 8.0.0 (vcpkg). Curved-surface counts are deterministic for a given
OCCT meshing algorithm; if a future OCCT changes them, update the
baselines deliberately.
"""

from pxr import Usd, UsdGeom
import math
import os
import subprocess
import tempfile
import unittest


# Expected total vertex counts per fixture (regression baseline).
EXPECTED_VERT_COUNTS = {
    'testCone.usda': 466,
    'testCube.usda': 24,
    'testCubeWithHole.usda': 138,
    'testCylinder.usda': 328,
    'testFilletedCube.usda': 512,
    'testFilletedCubeWithHole.usda': 626,
    'testPlane.usda': 4,
    'testPlaneWithHole.usda': 29,
    'testSphere.usda': 653,
    'testTwoBoxes.usda': 48,
}

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

# Closed solids: signed volume must be positive (outward winding).
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
    """Fixtures live next to the installed test (ctest) or the script."""
    for base in (os.getcwd(),
                 os.path.dirname(os.path.abspath(__file__))):
        d = os.path.join(base, 'fixtures')
        if os.path.isdir(d):
            return d
        d = os.path.join(base, 'testUsdSolidTessellation', 'fixtures')
        if os.path.isdir(d):
            return d
    raise RuntimeError("fixtures directory not found")


def _get_tessellator_bin():
    """Find the usdsolidtessellate binary."""
    env_bin = os.environ.get('USDSOLIDTESSELLATE_BIN')
    if env_bin and os.path.isfile(env_bin):
        return env_bin
    from shutil import which
    path_bin = which('usdsolidtessellate')
    if path_bin:
        return path_bin
    return 'usdsolidtessellate'


def _run_tessellator(input_path, output_path, prim_path):
    result = subprocess.run(
        [_get_tessellator_bin(), input_path, output_path, prim_path],
        capture_output=True, text=True, timeout=120)
    return result.returncode, result.stdout, result.stderr


def _tessellate_fixture(fixture_name):
    input_path = os.path.join(_get_fixtures_dir(), fixture_name)
    prim_path = PRIM_PATHS[fixture_name]
    fd, output_path = tempfile.mkstemp(suffix='.usda')
    os.close(fd)
    rc, stdout, stderr = _run_tessellator(input_path, output_path, prim_path)
    if rc != 0:
        os.unlink(output_path)
        raise RuntimeError(
            f"Tessellation failed for {fixture_name}: {stdout} {stderr}")
    stage = Usd.Stage.Open(output_path)
    os.unlink(output_path)
    return stage


def _meshes(stage):
    return [UsdGeom.Mesh(p) for p in stage.TraverseAll()
            if p.GetTypeName() == 'Mesh']


def _signed_volume(points, face_vertex_counts, face_vertex_indices):
    """Signed volume of a triangle mesh (positive = outward winding)."""
    volume = 0.0
    idx = 0
    for count in face_vertex_counts:
        if count != 3:
            idx += count
            continue
        v0 = points[face_vertex_indices[idx]]
        v1 = points[face_vertex_indices[idx + 1]]
        v2 = points[face_vertex_indices[idx + 2]]
        volume += (v0[0] * (v1[1] * v2[2] - v1[2] * v2[1]) +
                   v0[1] * (v1[2] * v2[0] - v1[0] * v2[2]) +
                   v0[2] * (v1[0] * v2[1] - v1[1] * v2[0]))
        idx += count
    return volume / 6.0


class TestTessellationVertexCounts(unittest.TestCase):
    """Each fixture produces the validated vertex count (regression)."""

    def _check_fixture(self, fixture_name):
        stage = _tessellate_fixture(fixture_name)
        total_verts = sum(len(m.GetPointsAttr().Get())
                          for m in _meshes(stage))
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
    """Signed volume is positive for closed solids.

    Winding and normals are driven by the authored
    faceuse:orientationType (first faceuse of each face's pair = the
    outward side), so outward orientation is exact, not heuristic.
    """

    def _check_winding(self, fixture_name):
        stage = _tessellate_fixture(fixture_name)
        meshes = _meshes(stage)
        self.assertTrue(meshes, f"{fixture_name}: no meshes produced")
        for mesh in meshes:
            vol = _signed_volume(mesh.GetPointsAttr().Get(),
                                 mesh.GetFaceVertexCountsAttr().Get(),
                                 mesh.GetFaceVertexIndicesAttr().Get())
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
    """Output meshes have correct properties."""

    def test_NormalsPresent(self):
        stage = _tessellate_fixture('testSphere.usda')
        for mesh in _meshes(stage):
            normals = mesh.GetNormalsAttr().Get()
            points = mesh.GetPointsAttr().Get()
            self.assertIsNotNone(normals)
            self.assertEqual(len(normals), len(points))

    def test_NormalsUnitLength(self):
        stage = _tessellate_fixture('testCube.usda')
        for mesh in _meshes(stage):
            for n in mesh.GetNormalsAttr().Get():
                length = math.sqrt(n[0]**2 + n[1]**2 + n[2]**2)
                self.assertAlmostEqual(length, 1.0, places=4)

    def test_SubdivisionSchemeNone(self):
        stage = _tessellate_fixture('testCube.usda')
        for mesh in _meshes(stage):
            self.assertEqual(mesh.GetSubdivisionSchemeAttr().Get(), 'none')

    def test_FaceCountsNonZero(self):
        for fixture_name in EXPECTED_VERT_COUNTS:
            stage = _tessellate_fixture(fixture_name)
            total_faces = sum(len(m.GetFaceVertexCountsAttr().Get())
                              for m in _meshes(stage))
            self.assertGreater(total_faces, 0,
                               f"{fixture_name}: no faces produced")


class TestKnownDefects(unittest.TestCase):
    """Documented defects, tracked as expected failures.

    When one of these unexpectedly passes, the defect has been fixed:
    promote it to a regular test.
    """

    @unittest.expectedFailure
    def test_TrimmedFaceAreaComplete(self):
        """Multi-loop (hole-punctured) faces should mesh their full area.

        Known defect: faces trimmed via 3D-edge-curve wires (no authored
        curveUv pcurves) triangulate to a small fraction of their true
        area on BSpline surfaces — the projected UV wire is malformed.
        For testCubeWithHole's z=10 face the meshed area is ~12.5 of the
        expected ~71.7 (10x10 minus a radius-3 hole). Fix: implement the
        schema's authored curveUv (pcurve) trimming path.
        """
        stage = _tessellate_fixture('testCubeWithHole.usda')
        area = 0.0
        for mesh in _meshes(stage):
            points = mesh.GetPointsAttr().Get()
            fvc = mesh.GetFaceVertexCountsAttr().Get()
            fvi = mesh.GetFaceVertexIndicesAttr().Get()
            idx = 0
            for count in fvc:
                tri = [points[fvi[idx + k]] for k in range(count)]
                idx += count
                if count != 3:
                    continue
                if not all(abs(p[2] - 10.0) < 1e-6 for p in tri):
                    continue
                ux, uy, uz = (tri[1][0] - tri[0][0],
                              tri[1][1] - tri[0][1],
                              tri[1][2] - tri[0][2])
                vx, vy, vz = (tri[2][0] - tri[0][0],
                              tri[2][1] - tri[0][1],
                              tri[2][2] - tri[0][2])
                cx, cy, cz = (uy * vz - uz * vy,
                              uz * vx - ux * vz,
                              ux * vy - uy * vx)
                area += 0.5 * math.sqrt(cx * cx + cy * cy + cz * cz)
        expected = 100.0 - math.pi * 9.0  # 10x10 face minus r=3 hole
        self.assertGreater(area, 0.9 * expected,
                           f"punctured face area {area:.2f} << {expected:.2f}")


class TestTessellationErrorHandling(unittest.TestCase):
    """Bad inputs return errors without crashing."""

    def test_NonexistentPrimPath(self):
        input_path = os.path.join(_get_fixtures_dir(), 'testCube.usda')
        fd, output_path = tempfile.mkstemp(suffix='.usda')
        os.close(fd)
        rc, _, _ = _run_tessellator(input_path, output_path,
                                    '/World/DoesNotExist')
        if os.path.exists(output_path):
            os.unlink(output_path)
        self.assertNotEqual(rc, 0)

    def test_NonexistentInputFile(self):
        fd, output_path = tempfile.mkstemp(suffix='.usda')
        os.close(fd)
        rc, _, _ = _run_tessellator('does_not_exist_xyz.usda',
                                    output_path, '/World/Cube')
        if os.path.exists(output_path):
            os.unlink(output_path)
        self.assertNotEqual(rc, 0)


if __name__ == '__main__':
    unittest.main()
