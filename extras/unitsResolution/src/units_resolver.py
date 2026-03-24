#!/usr/bin/env python3
"""
units_resolver.py — Unit-aware value resolution for USD.

Detects metersPerUnit and upAxis mismatches at composition arc boundaries
and computes corrective transforms for unit-bearing attributes.

This is the Python POC. The production path is a C++ OpenExec computation.
"""

from pxr import Usd, UsdGeom, UsdPhysics, Sdf, Gf, Pcp


# Corrective rotation matrices for upAxis conversion.
_Y_TO_Z_ROTATION = Gf.Matrix4d().SetRotate(
    Gf.Rotation(Gf.Vec3d(1, 0, 0), 90))
_Z_TO_Y_ROTATION = Gf.Matrix4d().SetRotate(
    Gf.Rotation(Gf.Vec3d(1, 0, 0), -90))


# ============================================================================
# Unit dimension exponents for physics attributes.
#
# Each entry maps an attribute name to its length exponent.
# The conversion factor is: value * (mpu_scale ^ length_exponent)
#   where mpu_scale = source_metersPerUnit / stage_metersPerUnit
#
# For attributes that also depend on kilogramsPerUnit:
#   value * (mpu_scale ^ length_exp) * (kpu_scale ^ mass_exp)
#
# This table is the minimum viable set. A production system would derive
# this from schema annotations (which don't exist yet — see proposal).
# ============================================================================

# (length_exponent, mass_exponent)
PHYSICS_UNIT_DIMENSIONS = {
    # Scene-level
    'physics:gravityMagnitude':   (1, 0),    # length/time² → scales with mPU

    # RigidBody
    'physics:velocity':           (1, 0),    # length/time → scales with mPU
    # physics:angularVelocity is radians/time — unitless for length

    # Mass API
    # physics:mass scales with kilogramsPerUnit only (length_exp=0, mass_exp=1)
    'physics:mass':               (0, 1),
    'physics:density':            (-3, 1),   # mass/length³
    'physics:centerOfMass':       (1, 0),    # length
    'physics:diagonalInertia':    (2, 1),    # mass * length²

    # Joints — spatial attributes
    'physics:localPos0':          (1, 0),
    'physics:localPos1':          (1, 0),
    'physics:breakForce':         (1, 1),    # mass * length / time²
    'physics:breakTorque':        (2, 1),    # mass * length² / time²
    'physics:maxDistance':        (1, 0),
    'physics:minDistance':        (1, 0),

    # Drive — linear variants
    'physics:stiffness':          (-1, 1),   # force/length = mass/time²/length? Actually mass/time²
    'physics:damping':            (-1, 1),   # force*time/length = mass/time
    'physics:maxForce':           (1, 1),    # force = mass*length/time²
    'physics:targetPosition':     (1, 0),    # length (for linear joints)
    'physics:targetVelocity':     (1, 0),    # length/time (for linear joints)
}


def get_meters_per_unit(layer):
    """Get metersPerUnit from a layer, defaulting to cm (0.01) per USD convention."""
    mpu = layer.pseudoRoot.GetInfo('metersPerUnit')
    if mpu is None or mpu == 0:
        # USD default is centimeters
        return 0.01
    return mpu


def get_up_axis(layer):
    """Get upAxis from a layer, defaulting to 'Y' per USD convention."""
    up = layer.pseudoRoot.GetInfo('upAxis')
    if up is None or up == '':
        return UsdGeom.Tokens.y
    return up


def get_kilograms_per_unit(layer):
    """Get kilogramsPerUnit from a layer, defaulting to 1.0 (kg)."""
    kpu = layer.pseudoRoot.GetInfo('kilogramsPerUnit')
    if kpu is None or kpu == 0:
        return 1.0
    return kpu


def get_unit_scale_for_prim(prim):
    """
    Determine the unit scale factor that should be applied to a prim's
    values to convert them from their authored units to the stage's units.
    
    Returns the scale factor (referenced_mpu / stage_mpu), or 1.0 if
    no unit mismatch exists.
    
    Walks the PrimIndex node tree to find the first reference or payload
    arc that introduces a unit mismatch.
    """
    stage = prim.GetStage()
    stage_mpu = UsdGeom.GetStageMetersPerUnit(stage)
    
    pcp_prim_index = prim.GetPrimIndex()
    root_node = pcp_prim_index.rootNode
    
    scale = _find_unit_mismatch(root_node, stage_mpu)
    return scale


def get_up_axis_correction_for_prim(prim):
    """
    Determine the upAxis corrective rotation matrix for a prim.
    
    Returns a GfMatrix4d rotation that converts from the referenced asset's
    upAxis convention to the stage's upAxis convention.
    Returns identity if no upAxis mismatch exists.
    """
    stage = prim.GetStage()
    stage_up = UsdGeom.GetStageUpAxis(stage)
    
    pcp_prim_index = prim.GetPrimIndex()
    root_node = pcp_prim_index.rootNode
    
    return _find_up_axis_mismatch(root_node, stage_up)


def _find_unit_mismatch(node, stage_mpu):
    """
    Recursively walk PrimIndex nodes to find the first reference/payload
    arc that introduces a metersPerUnit mismatch.
    """
    for child in node.children:
        if child.arcType in (Pcp.ArcTypeReference, Pcp.ArcTypePayload):
            layer_stack = child.layerStack
            if layer_stack and layer_stack.layers:
                ref_root_layer = layer_stack.layers[0]
                ref_mpu = get_meters_per_unit(ref_root_layer)
                if not _units_are_close(ref_mpu, stage_mpu):
                    return ref_mpu / stage_mpu
        
        result = _find_unit_mismatch(child, stage_mpu)
        if result != 1.0:
            return result
    
    return 1.0


def _find_up_axis_mismatch(node, stage_up):
    """
    Recursively walk PrimIndex nodes to find the first reference/payload
    arc that introduces an upAxis mismatch.
    
    Returns the corrective rotation matrix, or identity if no mismatch.
    """
    for child in node.children:
        if child.arcType in (Pcp.ArcTypeReference, Pcp.ArcTypePayload):
            layer_stack = child.layerStack
            if layer_stack and layer_stack.layers:
                ref_root_layer = layer_stack.layers[0]
                ref_up = get_up_axis(ref_root_layer)
                if ref_up != stage_up:
                    return _get_up_axis_rotation(ref_up, stage_up)
        
        result = _find_up_axis_mismatch(child, stage_up)
        if result != Gf.Matrix4d(1.0):
            return result
    
    return Gf.Matrix4d(1.0)


def _get_up_axis_rotation(source_up, target_up):
    """
    Get the rotation matrix that converts from source_up to target_up.
    
    Only Y↔Z conversions are defined (X-up is not a standard USD convention).
    """
    if source_up == UsdGeom.Tokens.y and target_up == UsdGeom.Tokens.z:
        return Gf.Matrix4d(_Y_TO_Z_ROTATION)
    elif source_up == UsdGeom.Tokens.z and target_up == UsdGeom.Tokens.y:
        return Gf.Matrix4d(_Z_TO_Y_ROTATION)
    else:
        return Gf.Matrix4d(1.0)


def _units_are_close(a, b, tolerance=1e-6):
    """Compare two unit values with tolerance, matching UsdGeom.LinearUnitsAre."""
    if a == 0 or b == 0:
        return a == b
    return abs(a - b) / max(abs(a), abs(b)) < tolerance


def resolve_translate_in_stage_units(prim):
    """
    Resolve a prim's xformOp:translate in the stage's unit system.
    
    Applies both unit scaling AND upAxis rotation if mismatches exist.
    
    Returns the translated value in stage units, or None if no translate exists.
    """
    attr = prim.GetAttribute('xformOp:translate')
    if not attr or not attr.HasValue():
        return None
    
    raw_value = attr.Get()
    
    # Apply upAxis rotation first (rotates the coordinate frame)
    up_correction = get_up_axis_correction_for_prim(prim)
    if up_correction != Gf.Matrix4d(1.0):
        raw_value = up_correction.TransformDir(raw_value)
    
    # Then apply unit scale
    scale = get_unit_scale_for_prim(prim)
    if scale != 1.0:
        raw_value = Gf.Vec3d(raw_value[0] * scale,
                             raw_value[1] * scale,
                             raw_value[2] * scale)
    
    return raw_value


def resolve_xform_in_stage_units(prim):
    """
    Resolve a prim's full local transform in the stage's unit system.
    
    Applies upAxis rotation and unit scaling. The correction order is:
      corrected = upAxisRotation * localXform * unitScale(translation only)
    
    Returns a GfMatrix4d with the corrected local transform.
    """
    xformable = UsdGeom.Xformable(prim)
    if not xformable:
        return Gf.Matrix4d(1.0)
    
    local_xform = xformable.GetLocalTransformation()
    
    up_correction = get_up_axis_correction_for_prim(prim)
    scale = get_unit_scale_for_prim(prim)
    
    if up_correction == Gf.Matrix4d(1.0) and scale == 1.0:
        return local_xform
    
    result = Gf.Matrix4d(local_xform)
    
    # Apply upAxis rotation to the full matrix
    if up_correction != Gf.Matrix4d(1.0):
        result = result * up_correction
    
    # Apply unit scaling to the translation component
    if scale != 1.0:
        translate = result.ExtractTranslation()
        corrected_translate = translate * scale
        result.SetRow3(3, corrected_translate)
    
    return result


def get_mass_scale_for_prim(prim):
    """
    Determine the kilogramsPerUnit scale factor for a prim.
    
    Returns ref_kpu / stage_kpu, or 1.0 if no mismatch.
    """
    stage = prim.GetStage()
    stage_kpu = UsdPhysics.GetStageKilogramsPerUnit(stage)
    
    pcp_prim_index = prim.GetPrimIndex()
    root_node = pcp_prim_index.rootNode
    
    return _find_mass_mismatch(root_node, stage_kpu)


def _find_mass_mismatch(node, stage_kpu):
    """Walk PrimIndex to find kilogramsPerUnit mismatch at arc boundaries."""
    for child in node.children:
        if child.arcType in (Pcp.ArcTypeReference, Pcp.ArcTypePayload):
            layer_stack = child.layerStack
            if layer_stack and layer_stack.layers:
                ref_root_layer = layer_stack.layers[0]
                ref_kpu = get_kilograms_per_unit(ref_root_layer)
                if not _units_are_close(ref_kpu, stage_kpu):
                    return ref_kpu / stage_kpu
        
        result = _find_mass_mismatch(child, stage_kpu)
        if result != 1.0:
            return result
    
    return 1.0


def resolve_physics_attr(prim, attr_name):
    """
    Resolve a physics attribute in the stage's unit system.
    
    Uses the PHYSICS_UNIT_DIMENSIONS table to determine the length and mass
    exponents, then applies:
      corrected = raw * (mpu_scale ^ length_exp) * (kpu_scale ^ mass_exp)
    
    For vector attributes (velocity, centerOfMass, etc.), also applies
    upAxis rotation.
    
    Returns the corrected value, or None if the attribute doesn't exist.
    """
    attr = prim.GetAttribute(attr_name)
    if not attr or not attr.HasValue():
        return None
    
    raw_value = attr.Get()
    
    # Look up unit dimensions
    base_name = attr_name
    if attr_name.startswith('drive:') and ':physics:' in attr_name:
        base_name = 'physics:' + attr_name.split(':physics:')[1]
    
    dimensions = PHYSICS_UNIT_DIMENSIONS.get(base_name)
    if dimensions is None:
        return raw_value
    
    length_exp, mass_exp = dimensions
    
    mpu_scale = get_unit_scale_for_prim(prim)
    kpu_scale = get_mass_scale_for_prim(prim)
    up_correction = get_up_axis_correction_for_prim(prim)
    
    # Compute the combined scale factor
    scale = 1.0
    if mpu_scale != 1.0 and length_exp != 0:
        scale *= mpu_scale ** length_exp
    if kpu_scale != 1.0 and mass_exp != 0:
        scale *= kpu_scale ** mass_exp
    
    # Apply upAxis rotation to vector/point attributes
    is_vector = isinstance(raw_value, (Gf.Vec3f, Gf.Vec3d, Gf.Vec3h))
    if is_vector and up_correction != Gf.Matrix4d(1.0):
        raw_value = Gf.Vec3f(up_correction.TransformDir(Gf.Vec3d(raw_value)))
    
    # Apply scale
    if scale != 1.0:
        if is_vector:
            raw_value = type(raw_value)(
                raw_value[0] * scale,
                raw_value[1] * scale,
                raw_value[2] * scale)
        elif isinstance(raw_value, (float, int)):
            raw_value = raw_value * scale
    
    return raw_value
