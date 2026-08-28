"""Programmatic test stage builders — shared helpers for other tests."""

from pxr import Usd, UsdGeom, Sdf, Gf, Vt
from units_api import MetricsAPI


def build_stage1_bolt_in_factory() -> Usd.Stage:
    """mm bolt referenced into m factory.

    /Factory                  metersPerUnit=1.0
      /Factory/Floor          Mesh
      /Factory/Equipment
        /Factory/Equipment/Bolt   metersPerUnit=0.001  (simulated reference boundary)
          /Factory/Equipment/Bolt/Shaft   Mesh
          /Factory/Equipment/Bolt/Head    Mesh
    """
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    factory = UsdGeom.Xform.Define(stage, "/Factory")
    MetricsAPI.apply(factory.GetPrim(), meters_per_unit=1.0, up_axis="Y")

    floor = UsdGeom.Mesh.Define(stage, "/Factory/Floor")
    floor.GetPrim().GetReferences()  # just ensure prim exists

    UsdGeom.Xform.Define(stage, "/Factory/Equipment")

    bolt = UsdGeom.Xform.Define(stage, "/Factory/Equipment/Bolt")
    MetricsAPI.apply(bolt.GetPrim(), meters_per_unit=0.001)

    UsdGeom.Mesh.Define(stage, "/Factory/Equipment/Bolt/Shaft")
    UsdGeom.Mesh.Define(stage, "/Factory/Equipment/Bolt/Head")

    return stage


def build_stage4_deep_nesting() -> Usd.Stage:
    """Factory with deep CNC machine hierarchy.

    /Factory                          metersPerUnit=1.0
      /Factory/Building
        /Factory/Building/Hall        Mesh
        /Factory/Building/CNC_Area
          /Factory/Building/CNC_Area/Machine   metersPerUnit=0.001
            /Factory/Building/CNC_Area/Machine/Spindle
              /Factory/Building/CNC_Area/Machine/Spindle/Tool
                /Factory/Building/CNC_Area/Machine/Spindle/Tool/Bit
    """
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    factory = UsdGeom.Xform.Define(stage, "/Factory")
    MetricsAPI.apply(factory.GetPrim(), meters_per_unit=1.0, up_axis="Y")

    UsdGeom.Xform.Define(stage, "/Factory/Building")
    UsdGeom.Mesh.Define(stage, "/Factory/Building/Hall")
    UsdGeom.Xform.Define(stage, "/Factory/Building/CNC_Area")

    machine = UsdGeom.Xform.Define(stage, "/Factory/Building/CNC_Area/Machine")
    MetricsAPI.apply(machine.GetPrim(), meters_per_unit=0.001)

    UsdGeom.Xform.Define(stage, "/Factory/Building/CNC_Area/Machine/Spindle")
    UsdGeom.Xform.Define(stage, "/Factory/Building/CNC_Area/Machine/Spindle/Tool")
    UsdGeom.Mesh.Define(stage, "/Factory/Building/CNC_Area/Machine/Spindle/Tool/Bit")

    return stage


def build_stage2_physics() -> Usd.Stage:
    """Robot arm (meters) in a cm-scale simulation stage with physics.

    /World                            (centimeters, metersPerUnit=0.01)
      /World/PhysicsScene             (gravityMagnitude=981.0 cm/s²)
      /World/Ground                   (Mesh)
      /World/Robot                    (metersPerUnit=1.0 — simulated reference boundary)
        /World/Robot/Base             (RigidBody, mass=50 kg)
        /World/Robot/Arm              (RigidBody, mass=10 kg, velocity=(1,0,0) m/s)
        /World/Robot/Gripper          (RigidBody, mass=2 kg, density=2700 kg/m³)
    """
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, 0.01)  # cm stage

    world = UsdGeom.Xform.Define(stage, "/World")
    MetricsAPI.apply(world.GetPrim(), meters_per_unit=0.01, up_axis="Y",
                     kilograms_per_unit=1.0)

    # PhysicsScene: gravity authored in cm context (cm/s²)
    physics_scene = stage.DefinePrim("/World/PhysicsScene", "Xform")
    grav = physics_scene.CreateAttribute("physics:gravityMagnitude",
                                         Sdf.ValueTypeNames.Float)
    grav.Set(981.0)  # cm/s²

    UsdGeom.Mesh.Define(stage, "/World/Ground")

    # Robot subtree: authored in meters
    robot = UsdGeom.Xform.Define(stage, "/World/Robot")
    MetricsAPI.apply(robot.GetPrim(), meters_per_unit=1.0, kilograms_per_unit=1.0)

    base = stage.DefinePrim("/World/Robot/Base", "Xform")
    base_mass = base.CreateAttribute("physics:mass", Sdf.ValueTypeNames.Float)
    base_mass.Set(50.0)  # kg

    arm = stage.DefinePrim("/World/Robot/Arm", "Xform")
    arm_mass = arm.CreateAttribute("physics:mass", Sdf.ValueTypeNames.Float)
    arm_mass.Set(10.0)  # kg
    arm_vel = arm.CreateAttribute("physics:velocity", Sdf.ValueTypeNames.Float3)
    arm_vel.Set(Gf.Vec3f(1.0, 0.0, 0.0))  # 1 m/s along X

    gripper = stage.DefinePrim("/World/Robot/Gripper", "Xform")
    gripper_mass = gripper.CreateAttribute("physics:mass", Sdf.ValueTypeNames.Float)
    gripper_mass.Set(2.0)  # kg
    gripper_density = gripper.CreateAttribute("physics:density",
                                              Sdf.ValueTypeNames.Float)
    gripper_density.Set(2700.0)  # kg/m³ in meter context

    return stage


def build_stage3_camera_lights() -> Usd.Stage:
    """Film set with camera and lights in a cm-scale stage.

    /Scene                            (centimeters, metersPerUnit=0.01)
      /Scene/Set                      (Mesh)
      /Scene/Camera                   (Camera)
      /Scene/KeyLight                 (RectLight)
    """
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, 0.01)

    scene = UsdGeom.Xform.Define(stage, "/Scene")
    MetricsAPI.apply(scene.GetPrim(), meters_per_unit=0.01, up_axis="Y")

    UsdGeom.Mesh.Define(stage, "/Scene/Set")

    camera = UsdGeom.Camera.Define(stage, "/Scene/Camera")
    camera.GetFocalLengthAttr().Set(50.0)          # mm (fixed unit, not scene units)
    camera.GetFocusDistanceAttr().Set(500.0)        # scene units (cm)
    camera.GetClippingRangeAttr().Set(Gf.Vec2f(1.0, 100000.0))  # scene units (cm)
    camera.GetHorizontalApertureAttr().Set(36.0)    # mm (fixed unit, not scene units)

    # RectLight — inputs:width and inputs:height are in scene units
    rect_light = stage.DefinePrim("/Scene/KeyLight", "Xform")
    width_attr = rect_light.CreateAttribute("inputs:width", Sdf.ValueTypeNames.Float)
    width_attr.Set(100.0)   # 100 cm = 1 m
    height_attr = rect_light.CreateAttribute("inputs:height", Sdf.ValueTypeNames.Float)
    height_attr.Set(100.0)  # 100 cm = 1 m
    intensity_attr = rect_light.CreateAttribute("inputs:intensity",
                                                Sdf.ValueTypeNames.Float)
    intensity_attr.Set(1.0)

    return stage


def build_stage5_custom_attrs() -> Usd.Stage:
    """Pipeline extension with custom attributes in a meter-scale stage.

    /Pipe                             (meters, metersPerUnit=1.0)
      /Pipe/Segment                   (Mesh, custom float pipeline attributes)
    """
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, 1.0)

    pipe = UsdGeom.Xform.Define(stage, "/Pipe")
    MetricsAPI.apply(pipe.GetPrim(), meters_per_unit=1.0, up_axis="Y")

    segment = UsdGeom.Mesh.Define(stage, "/Pipe/Segment")
    seg_prim = segment.GetPrim()

    seg_prim.CreateAttribute("myPipeline:innerRadius",
                             Sdf.ValueTypeNames.Float).Set(0.05)
    seg_prim.CreateAttribute("myPipeline:outerRadius",
                             Sdf.ValueTypeNames.Float).Set(0.06)
    seg_prim.CreateAttribute("myPipeline:flowRate",
                             Sdf.ValueTypeNames.Float).Set(0.002)
    seg_prim.CreateAttribute("myPipeline:pressure",
                             Sdf.ValueTypeNames.Float).Set(101325.0)
    seg_prim.CreateAttribute("myPipeline:roughnessCoeff",
                             Sdf.ValueTypeNames.Float).Set(0.015)

    return stage


def build_stage6_point_instancer() -> Usd.Stage:
    """Centimeter-scale forest with a PointInstancer scattering trees.

    /Forest                         metersPerUnit=0.01  (cm)
      /Forest/TreePrototype         Mesh (a simple tree)
      /Forest/Trees                 PointInstancer
        positions: 3 trees at cm positions
        velocities: wind sway in cm/s
        accelerations: gravity-like in cm/s²
        orientations: unitless quaternions
    """
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    forest = UsdGeom.Xform.Define(stage, "/Forest")
    MetricsAPI.apply(forest.GetPrim(), meters_per_unit=0.01, up_axis="Y")

    # A simple tree prototype
    UsdGeom.Mesh.Define(stage, "/Forest/TreePrototype")

    # PointInstancer with 3 instances
    pi = UsdGeom.PointInstancer.Define(stage, "/Forest/Trees")
    pi.GetProtoIndicesAttr().Set(Vt.IntArray([0, 0, 0]))
    pi.GetPrototypesRel().AddTarget("/Forest/TreePrototype")

    # Positions in cm
    pi.GetPositionsAttr().Set(Vt.Vec3fArray([
        Gf.Vec3f(100, 0, 0),     # 1m from origin
        Gf.Vec3f(500, 0, 200),   # 5m, 2m
        Gf.Vec3f(1000, 0, -300), # 10m, -3m
    ]))

    # Velocities in cm/s (wind sway)
    pi.GetVelocitiesAttr().Set(Vt.Vec3fArray([
        Gf.Vec3f(5, 0, 0),       # 5 cm/s = 0.05 m/s
        Gf.Vec3f(-3, 0, 2),
        Gf.Vec3f(0, 0, -1),
    ]))

    # Accelerations in cm/s²
    pi.GetAccelerationsAttr().Set(Vt.Vec3fArray([
        Gf.Vec3f(0, -981, 0),    # gravity in cm/s²
        Gf.Vec3f(0, -981, 0),
        Gf.Vec3f(0, -981, 0),
    ]))

    # Orientations — unitless
    pi.GetOrientationsAttr().Set(Vt.QuathArray([
        Gf.Quath(1, 0, 0, 0),              # identity
        Gf.Quath(0.707, 0, 0.707, 0),      # 90° around Y
        Gf.Quath(0.866, 0, 0.5, 0),        # 60° around Y
    ]))

    return stage
