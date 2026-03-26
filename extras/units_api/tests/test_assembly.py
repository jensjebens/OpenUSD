"""Tests for MetricsAssembler."""
import pytest
from pxr import Usd, UsdGeom, Gf, Vt
from units_api import MetricsAPI
from units_api.assembly import MetricsAssembler
from test_stages import (
    build_stage1_bolt_in_factory,
    build_stage2_physics,
    build_stage4_deep_nesting,
)


# ---------------------------------------------------------------------------
# compute_corrective_scale
# ---------------------------------------------------------------------------

def test_compute_scale_mm_to_m():
    assert MetricsAssembler.compute_corrective_scale(0.001, 1.0) == pytest.approx(0.001)


def test_compute_scale_cm_to_m():
    assert MetricsAssembler.compute_corrective_scale(0.01, 1.0) == pytest.approx(0.01)


def test_compute_scale_same():
    assert MetricsAssembler.compute_corrective_scale(1.0, 1.0) == pytest.approx(1.0)


# ---------------------------------------------------------------------------
# compute_corrective_rotation
# ---------------------------------------------------------------------------

def test_rotation_y_to_z():
    assert MetricsAssembler.compute_corrective_rotation("Y", "Z") == pytest.approx(-90.0)


def test_rotation_z_to_y():
    assert MetricsAssembler.compute_corrective_rotation("Z", "Y") == pytest.approx(90.0)


def test_rotation_same():
    assert MetricsAssembler.compute_corrective_rotation("Y", "Y") is None
    assert MetricsAssembler.compute_corrective_rotation("Z", "Z") is None


# ---------------------------------------------------------------------------
# apply_corrective_xform
# ---------------------------------------------------------------------------

def test_apply_corrective_scale():
    stage = build_stage1_bolt_in_factory()
    prim = stage.GetPrimAtPath("/Factory/Equipment/Bolt")

    result = MetricsAssembler.apply_corrective_xform(prim, 0.001, 1.0)

    xformable = UsdGeom.Xformable(prim)
    ops = xformable.GetOrderedXformOps()
    scale_op = next(
        (op for op in ops if op.GetOpName() == "xformOp:scale:metricsCorrection"), None
    )
    assert scale_op is not None
    assert scale_op.Get() == Gf.Vec3d(0.001, 0.001, 0.001)
    assert result["scale"] == pytest.approx(0.001)


def test_apply_corrective_with_rotation():
    stage = Usd.Stage.CreateInMemory()
    prim = UsdGeom.Xform.Define(stage, "/Root").GetPrim()

    result = MetricsAssembler.apply_corrective_xform(
        prim, 0.001, 1.0, source_up="Y", target_up="Z"
    )

    xformable = UsdGeom.Xformable(prim)
    op_names = [op.GetOpName() for op in xformable.GetOrderedXformOps()]
    assert "xformOp:scale:metricsCorrection" in op_names
    assert "xformOp:rotateX:metricsCorrection" in op_names
    assert result["rotation"] == pytest.approx(-90.0)


# ---------------------------------------------------------------------------
# correct_reference_boundary
# ---------------------------------------------------------------------------

def test_correct_boundary_stage1():
    stage = build_stage1_bolt_in_factory()
    factory_prim = stage.GetPrimAtPath("/Factory")
    bolt_prim = stage.GetPrimAtPath("/Factory/Equipment/Bolt")

    assert MetricsAssembler.correct_reference_boundary(factory_prim) is None

    result = MetricsAssembler.correct_reference_boundary(bolt_prim)
    assert result is not None
    assert result["prim_path"] == "/Factory/Equipment/Bolt"
    assert result["scale"] == pytest.approx(0.001)


# ---------------------------------------------------------------------------
# correct_stage
# ---------------------------------------------------------------------------

def test_correct_stage1():
    stage = build_stage1_bolt_in_factory()
    corrections = MetricsAssembler.correct_stage(stage)
    assert len(corrections) == 1
    assert corrections[0]["prim_path"] == "/Factory/Equipment/Bolt"


def test_correct_stage4():
    stage = build_stage4_deep_nesting()
    corrections = MetricsAssembler.correct_stage(stage)
    assert len(corrections) == 1
    assert corrections[0]["prim_path"] == "/Factory/Building/CNC_Area/Machine"


# ---------------------------------------------------------------------------
# audit_stage
# ---------------------------------------------------------------------------

def test_audit_stage1():
    stage = build_stage1_bolt_in_factory()
    mismatches = MetricsAssembler.audit_stage(stage)
    assert len(mismatches) == 1
    assert mismatches[0]["prim_path"] == "/Factory/Equipment/Bolt"

    # Verify no corrective ops were added
    bolt_prim = stage.GetPrimAtPath("/Factory/Equipment/Bolt")
    ops = UsdGeom.Xformable(bolt_prim).GetOrderedXformOps()
    assert all("metricsCorrection" not in op.GetOpName() for op in ops)


# ---------------------------------------------------------------------------
# world-space and non-destructive checks
# ---------------------------------------------------------------------------

def test_corrected_world_space():
    stage = build_stage1_bolt_in_factory()
    bolt_prim = stage.GetPrimAtPath("/Factory/Equipment/Bolt")
    MetricsAssembler.apply_corrective_xform(bolt_prim, 0.001, 1.0)

    cache = UsdGeom.XformCache()
    xf = cache.GetLocalToWorldTransform(bolt_prim)
    result = xf.Transform(Gf.Vec3d(10, 0, 0))
    assert result[0] == pytest.approx(0.01)
    assert result[1] == pytest.approx(0.0)
    assert result[2] == pytest.approx(0.0)


def test_correction_is_non_destructive():
    stage = Usd.Stage.CreateInMemory()
    root = UsdGeom.Xform.Define(stage, "/Root")
    MetricsAPI.apply(root.GetPrim(), meters_per_unit=1.0, up_axis="Y")

    mesh = UsdGeom.Mesh.Define(stage, "/Root/Mesh")
    mesh_prim = mesh.GetPrim()
    MetricsAPI.apply(mesh_prim, meters_per_unit=0.001)

    original_points = Vt.Vec3fArray([
        Gf.Vec3f(0, 0, 0),
        Gf.Vec3f(10, 0, 0),
        Gf.Vec3f(10, 10, 0),
    ])
    mesh.GetPointsAttr().Set(original_points)

    MetricsAssembler.apply_corrective_xform(mesh_prim, 0.001, 1.0)

    points_after = mesh.GetPointsAttr().Get()
    assert len(points_after) == 3
    assert points_after[1] == Gf.Vec3f(10, 0, 0)


def test_correction_does_not_fix_density():
    stage = build_stage2_physics()
    robot_prim = stage.GetPrimAtPath("/World/Robot")
    gripper_prim = stage.GetPrimAtPath("/World/Robot/Gripper")

    MetricsAssembler.correct_reference_boundary(robot_prim)

    density_val = gripper_prim.GetAttribute("physics:density").Get()
    assert density_val == pytest.approx(2700.0)
