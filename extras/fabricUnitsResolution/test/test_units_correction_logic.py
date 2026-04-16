#!/usr/bin/env python3
"""
test_units_correction_logic.py — Standalone tests for unit correction math.

These tests run WITHOUT Kit — pure USD. They verify the correction logic
that will be applied inside ConcurrentXformCache.

The correction formula:
    corrected_local = apply_uniform_scale(raw_local, prim_mpu / stage_mpu)

Where apply_uniform_scale scales the translation component (row 3, cols 0-2)
by the scale factor, leaving rotation/scale components unchanged.

Run: python test_units_correction_logic.py
"""
import os
import sys
import unittest

from pxr import Usd, UsdGeom, Sdf, Gf, Pcp


TESTENV = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'stages')
STAGES_DIR = TESTENV

TOLERANCE = 1e-5


def apply_units_correction(local_matrix, prim_mpu, stage_mpu):
    """Apply unit correction to a local transform matrix.

    This is the Python reference implementation of what ConcurrentXformCache
    will do in C++. It scales the translation component of the matrix by
    (prim_mpu / stage_mpu).
    """
    if abs(prim_mpu - stage_mpu) < 1e-10:
        return local_matrix  # no correction needed

    scale = prim_mpu / stage_mpu
    result = Gf.Matrix4d(local_matrix)
    # Scale translation (row 3, columns 0-2)
    result[3, 0] *= scale
    result[3, 1] *= scale
    result[3, 2] *= scale
    return result


def get_corrected_world_transform(stage, prim):
    """Compute the corrected world transform for a prim.

    Walks up the hierarchy applying unit corrections at each level,
    mimicking what our modified ConcurrentXformCache._GetComputedMatrix() does.
    """
    stage_mpu = UsdGeom.GetStageMetersPerUnit(stage)

    def _get_world(p):
        if not p or not p.GetParent().IsValid():
            return Gf.Matrix4d(1.0)

        xformable = UsdGeom.Xformable(p)
        if not xformable:
            return _get_world(p.GetParent())

        resets = xformable.GetResetXformStack()
        local = xformable.GetLocalTransformation()

        # Determine prim's metersPerUnit from source layer
        prim_mpu = _get_prim_mpu(stage, p)
        if prim_mpu is not None:
            local = apply_units_correction(local, prim_mpu, stage_mpu)

        if resets:
            return local
        else:
            return local * _get_world(p.GetParent())

    return _get_world(prim)


def _get_prim_mpu(stage, prim):
    """Get the metersPerUnit for a prim by inspecting its PrimIndex.

    Walks the composition arcs to find the source layer's metersPerUnit.
    Returns None if it matches the stage (no correction needed).
    """
    stage_mpu = UsdGeom.GetStageMetersPerUnit(stage)
    pi = prim.GetPrimIndex()

    for node in pi.rootNode.children:
        if node.arcType in (Pcp.ArcTypeReference, Pcp.ArcTypePayload):
            src_layer = node.layerStack.layers[0]
            src_pseudo = src_layer.pseudoRoot
            if src_pseudo.HasInfo('metersPerUnit'):
                src_mpu = src_pseudo.GetInfo('metersPerUnit')
                if src_mpu and abs(src_mpu - stage_mpu) > 1e-10:
                    return src_mpu

    # Check if an ancestor was referenced from a different-unit layer
    parent = prim.GetParent()
    if parent and parent.IsValid() and parent.GetPath() != Sdf.Path('/'):
        return _get_prim_mpu(stage, parent)

    return None


class TestUnitsCorrectionLogic(unittest.TestCase):
    """Test the correction formula in pure Python/USD."""

    def test_cm_translate_correction(self):
        """100cm translate → 1m after correction."""
        local = Gf.Matrix4d(1.0)
        local[3, 0] = 100.0
        local[3, 1] = 200.0
        local[3, 2] = 300.0

        corrected = apply_units_correction(local, 0.01, 1.0)
        self.assertAlmostEqual(corrected[3, 0], 1.0, places=5)
        self.assertAlmostEqual(corrected[3, 1], 2.0, places=5)
        self.assertAlmostEqual(corrected[3, 2], 3.0, places=5)

    def test_mm_translate_correction(self):
        """10000mm translate → 10m after correction."""
        local = Gf.Matrix4d(1.0)
        local[3, 0] = 10000.0

        corrected = apply_units_correction(local, 0.001, 1.0)
        self.assertAlmostEqual(corrected[3, 0], 10.0, places=5)

    def test_identity_no_change(self):
        """Same units → no change."""
        local = Gf.Matrix4d(1.0)
        local[3, 0] = 5.0
        local[3, 1] = 3.0

        corrected = apply_units_correction(local, 1.0, 1.0)
        self.assertEqual(corrected, local)

    def test_rotation_preserved(self):
        """Rotation components are NOT scaled — only translation."""
        import math
        angle = math.pi / 4  # 45 degrees
        local = Gf.Matrix4d(1.0)
        local[0, 0] = math.cos(angle)
        local[0, 1] = math.sin(angle)
        local[1, 0] = -math.sin(angle)
        local[1, 1] = math.cos(angle)
        local[3, 0] = 100.0  # cm

        corrected = apply_units_correction(local, 0.01, 1.0)

        # Rotation unchanged
        self.assertAlmostEqual(corrected[0, 0], math.cos(angle), places=10)
        self.assertAlmostEqual(corrected[0, 1], math.sin(angle), places=10)
        # Translation scaled
        self.assertAlmostEqual(corrected[3, 0], 1.0, places=5)

    def test_cm_asset_reference(self):
        """cm_asset.usda referenced into meter stage — full pipeline."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 1.0)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

        ref_prim = stage.DefinePrim('/World/CmRef')
        ref_prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'cm_asset.usda'), '/Box')

        prim = stage.GetPrimAtPath('/World/CmRef')
        world = get_corrected_world_transform(stage, prim)

        self.assertAlmostEqual(world[3, 0], 1.0, places=5)
        self.assertAlmostEqual(world[3, 1], 2.0, places=5)
        self.assertAlmostEqual(world[3, 2], 3.0, places=5)

    def test_mm_asset_reference(self):
        """mm_asset.usda referenced into meter stage."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 1.0)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

        ref_prim = stage.DefinePrim('/World/MmRef')
        ref_prim.GetReferences().AddReference(
            os.path.join(TESTENV, 'mm_asset.usda'), '/Bolt')

        prim = stage.GetPrimAtPath('/World/MmRef')
        world = get_corrected_world_transform(stage, prim)

        self.assertAlmostEqual(world[3, 0], 10.0, places=5)
        self.assertAlmostEqual(world[3, 1], 5.5, places=5)

    def test_nested_hierarchy(self):
        """Parent (100cm) + child (50cm) → parent world X=1m, child world X=1.5m."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.SetStageMetersPerUnit(stage, 1.0)

        cm_layer = Sdf.Layer.CreateAnonymous('.usda')
        cm_layer.pseudoRoot.SetInfo('metersPerUnit', 0.01)

        with Sdf.ChangeBlock():
            root_spec = Sdf.CreatePrimInLayer(cm_layer, '/CmRoot')
            root_spec.typeName = 'Xform'
            root_spec.specifier = Sdf.SpecifierDef

            parent_spec = Sdf.CreatePrimInLayer(cm_layer, '/CmRoot/Parent')
            parent_spec.typeName = 'Xform'
            parent_spec.specifier = Sdf.SpecifierDef
            a = Sdf.AttributeSpec(parent_spec, 'xformOp:translate',
                                  Sdf.ValueTypeNames.Double3)
            a.default = Gf.Vec3d(100, 0, 0)
            o = Sdf.AttributeSpec(parent_spec, 'xformOpOrder',
                                  Sdf.ValueTypeNames.TokenArray)
            o.default = ['xformOp:translate']

            child_spec = Sdf.CreatePrimInLayer(cm_layer, '/CmRoot/Parent/Child')
            child_spec.typeName = 'Xform'
            child_spec.specifier = Sdf.SpecifierDef
            a2 = Sdf.AttributeSpec(child_spec, 'xformOp:translate',
                                   Sdf.ValueTypeNames.Double3)
            a2.default = Gf.Vec3d(50, 0, 0)
            o2 = Sdf.AttributeSpec(child_spec, 'xformOpOrder',
                                   Sdf.ValueTypeNames.TokenArray)
            o2.default = ['xformOp:translate']

        ref = stage.DefinePrim('/World/CmRef')
        ref.GetReferences().AddReference(cm_layer.identifier, '/CmRoot')

        parent = stage.GetPrimAtPath('/World/CmRef/Parent')
        child = stage.GetPrimAtPath('/World/CmRef/Parent/Child')

        parent_world = get_corrected_world_transform(stage, parent)
        child_world = get_corrected_world_transform(stage, child)

        self.assertAlmostEqual(parent_world[3, 0], 1.0, places=5)
        self.assertAlmostEqual(child_world[3, 0], 1.5, places=5)

    def test_resetXformStack(self):
        """resetXformStack child ignores parent, keeps own unit correction."""
        stage_path = os.path.join(STAGES_DIR, 'test_resetXformStack.usda')
        stage = Usd.Stage.Open(stage_path)
        self.assertIsNotNone(stage)

        parent = stage.GetPrimAtPath('/World/CmRef/Parent')
        child = stage.GetPrimAtPath('/World/CmRef/Parent/ResetChild')

        parent_world = get_corrected_world_transform(stage, parent)
        child_world = get_corrected_world_transform(stage, child)

        # Parent: 100cm → 1m
        self.assertAlmostEqual(parent_world[3, 0], 1.0, places=5)
        # ResetChild: !resetXformStack! + 200cm → 2m (ignores parent)
        self.assertAlmostEqual(child_world[3, 0], 2.0, places=5)


if __name__ == '__main__':
    unittest.main(verbosity=2)
