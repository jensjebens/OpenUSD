#!/pxrpythonsubst
#
# Copyright 2024 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.

from pxr import Usd, UsdSolid, UsdGeom, Vt, Gf, Sdf, Tf
import unittest


class TestUsdSolidBrepArray(unittest.TestCase):
    """Tests for the UsdSolid.BrepArray typed schema."""

    def test_Define(self):
        """BrepArray.Define() creates a valid typed prim."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')
        self.assertTrue(brep)
        self.assertEqual(brep.GetPrim().GetTypeName(), 'BrepArray')

    def test_InheritsGprim(self):
        """BrepArray inherits from Gprim — has extent, purpose, visibility."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')
        prim = brep.GetPrim()

        # Should be castable to Gprim
        gprim = UsdGeom.Gprim(prim)
        self.assertTrue(gprim)

        # Inherited attributes exist
        self.assertTrue(brep.GetExtentAttr())
        self.assertTrue(brep.GetPurposeAttr())
        self.assertTrue(brep.GetVisibilityAttr())
        self.assertTrue(brep.GetDoubleSidedAttr())

    def test_TypeRegistration(self):
        """Schema type is registered and IsA works."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')
        prim = brep.GetPrim()

        # Can construct from prim
        brep2 = UsdSolid.BrepArray(prim)
        self.assertTrue(brep2)

        # Type is concrete and typed (instance methods)
        self.assertTrue(brep.IsConcrete())
        self.assertTrue(brep.IsTyped())
        self.assertFalse(brep.IsAPISchema())

    def test_GetSetRegionCount(self):
        """Author brep:regionCount (UIntArray), read back matches."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')

        attr = brep.GetBrepRegionCountAttr()
        self.assertTrue(attr)

        values = Vt.UIntArray([2, 1, 3])
        attr.Set(values)
        result = attr.Get()
        self.assertEqual(list(result), [2, 1, 3])

    def test_GetSetEdgeVertexIndices(self):
        """Author edge:vertexIndices (Int2Array), read back matches."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')

        attr = brep.GetEdgeVertexIndicesAttr()
        self.assertTrue(attr)

        values = Vt.Vec2iArray([(0, 1), (1, 2), (2, 0)])
        attr.Set(values)
        result = attr.Get()
        self.assertEqual(len(result), 3)
        self.assertEqual(result[0], Gf.Vec2i(0, 1))
        self.assertEqual(result[1], Gf.Vec2i(1, 2))
        self.assertEqual(result[2], Gf.Vec2i(2, 0))

    def test_GetSetFaceSurfaceType(self):
        """Author face:surfaceType (TokenArray with allowed values)."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')

        attr = brep.GetFaceSurfaceTypeAttr()
        self.assertTrue(attr)

        values = Vt.TokenArray([
            'BrepSurfaceNurbAPI',
            'BrepSurfacePlaneAPI',
            'BrepSurfaceCylinderAPI'
        ])
        attr.Set(values)
        result = attr.Get()
        self.assertEqual(list(result), [
            'BrepSurfaceNurbAPI',
            'BrepSurfacePlaneAPI',
            'BrepSurfaceCylinderAPI'
        ])

    def test_GetSetBrepExtent(self):
        """Author brep:extent (Double3Array), read back matches."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')

        attr = brep.GetBrepExtentAttr()
        self.assertTrue(attr)

        # Two corners per brep: XYZmin, XYZmax
        values = Vt.Vec3dArray([
            Gf.Vec3d(-1, -1, -1),
            Gf.Vec3d(1, 1, 1)
        ])
        attr.Set(values)
        result = attr.Get()
        self.assertEqual(len(result), 2)
        self.assertEqual(result[0], Gf.Vec3d(-1, -1, -1))
        self.assertEqual(result[1], Gf.Vec3d(1, 1, 1))

    def test_GetSetEdgeRange(self):
        """Author edge:range (DoubleArray), read back matches."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')

        attr = brep.GetEdgeRangeAttr()
        self.assertTrue(attr)

        # Two doubles per edge: paramMin, paramMax
        values = Vt.DoubleArray([0.0, 1.0, 0.0, 3.14159])
        attr.Set(values)
        result = attr.Get()
        self.assertEqual(len(result), 4)
        self.assertAlmostEqual(result[0], 0.0)
        self.assertAlmostEqual(result[1], 1.0)
        self.assertAlmostEqual(result[3], 3.14159, places=4)

    def test_EmptyArrayDefaults(self):
        """Unset attributes return None (not crash)."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')

        # Unset uniform attributes return None
        self.assertIsNone(brep.GetBrepRegionCountAttr().Get())
        self.assertIsNone(brep.GetEdgeVertexIndicesAttr().Get())
        self.assertIsNone(brep.GetFaceSurfaceTypeAttr().Get())
        self.assertIsNone(brep.GetLoopEdgeuseCountAttr().Get())

    def test_InvalidPrim(self):
        """BrepArray on invalid prim is falsy."""
        invalid = UsdSolid.BrepArray(Usd.Prim())
        self.assertFalse(invalid)

    def test_TimeSampled(self):
        """Attributes work with time samples."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')

        attr = brep.GetBrepRegionCountAttr()
        attr.Set(Vt.UIntArray([1]), Usd.TimeCode(1.0))
        attr.Set(Vt.UIntArray([2, 3]), Usd.TimeCode(2.0))

        result1 = attr.Get(Usd.TimeCode(1.0))
        result2 = attr.Get(Usd.TimeCode(2.0))
        self.assertEqual(list(result1), [1])
        self.assertEqual(list(result2), [2, 3])

    def test_CreateAttr(self):
        """CreateXxxAttr works and produces authored attributes."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')

        # Create with default value
        attr = brep.CreateBrepRegionCountAttr(Vt.UIntArray([5]))
        self.assertTrue(attr)
        self.assertTrue(attr.IsAuthored())
        self.assertEqual(list(attr.Get()), [5])


class TestUsdSolidBrepPointAPI(unittest.TestCase):
    """Tests for the UsdSolid.BrepPointAPI multiple-apply schema."""

    def test_Apply(self):
        """BrepPointAPI.Apply works with allowed instance names."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')
        prim = brep.GetPrim()

        api = UsdSolid.BrepPointAPI.Apply(prim, 'vertexPoint')
        self.assertTrue(api)

    def test_GetSetPointPosition(self):
        """point:position attribute works after API application."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')
        prim = brep.GetPrim()

        api = UsdSolid.BrepPointAPI.Apply(prim, 'vertexPoint')
        attr = api.GetPointPositionAttr()
        self.assertTrue(attr)

        positions = Vt.Vec3dArray([
            Gf.Vec3d(0, 0, 0),
            Gf.Vec3d(1, 0, 0),
            Gf.Vec3d(1, 1, 0),
            Gf.Vec3d(0, 1, 0)
        ])
        attr.Set(positions)
        result = attr.Get()
        self.assertEqual(len(result), 4)
        self.assertEqual(result[0], Gf.Vec3d(0, 0, 0))
        self.assertEqual(result[2], Gf.Vec3d(1, 1, 0))

    def test_MultipleInstances(self):
        """Different instance names (vertexPoint, shellPoint) are independent."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')
        prim = brep.GetPrim()

        vertex_api = UsdSolid.BrepPointAPI.Apply(prim, 'vertexPoint')
        shell_api = UsdSolid.BrepPointAPI.Apply(prim, 'shellPoint')

        vertex_api.GetPointPositionAttr().Set(
            Vt.Vec3dArray([Gf.Vec3d(1, 2, 3)]))
        shell_api.GetPointPositionAttr().Set(
            Vt.Vec3dArray([Gf.Vec3d(4, 5, 6)]))

        # They should be different attributes
        v_result = vertex_api.GetPointPositionAttr().Get()
        s_result = shell_api.GetPointPositionAttr().Get()
        self.assertEqual(v_result[0], Gf.Vec3d(1, 2, 3))
        self.assertEqual(s_result[0], Gf.Vec3d(4, 5, 6))


class TestUsdSolidBrepSurfaceNurbAPI(unittest.TestCase):
    """Tests for BrepSurfaceNurbAPI — representative of surface API schemas."""

    def test_Apply(self):
        """BrepSurfaceNurbAPI can be applied."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')
        prim = brep.GetPrim()

        api = UsdSolid.BrepSurfaceNurbAPI.Apply(prim)
        self.assertTrue(api)

    def test_NurbSurfaceAttributes(self):
        """NURB surface attributes (controlVertices, knots, order) work."""
        stage = Usd.Stage.CreateInMemory()
        brep = UsdSolid.BrepArray.Define(stage, '/TestBrep')
        prim = brep.GetPrim()

        api = UsdSolid.BrepSurfaceNurbAPI.Apply(prim)

        # Control vertices
        cv_attr = api.GetSurfaceControlVerticesAttr()
        self.assertTrue(cv_attr)
        cvs = Vt.Vec3dArray([Gf.Vec3d(i, j, 0)
                             for i in range(4) for j in range(4)])
        cv_attr.Set(cvs)
        self.assertEqual(len(cv_attr.Get()), 16)

        # U knots
        uk_attr = api.GetSurfaceUKnotsAttr()
        self.assertTrue(uk_attr)
        uk_attr.Set(Vt.DoubleArray([0, 0, 0, 0, 1, 1, 1, 1]))
        self.assertEqual(len(uk_attr.Get()), 8)

        # U order
        uo_attr = api.GetSurfaceUOrderAttr()
        self.assertTrue(uo_attr)
        uo_attr.Set(Vt.UIntArray([4]))
        self.assertEqual(list(uo_attr.Get()), [4])


if __name__ == '__main__':
    unittest.main()
