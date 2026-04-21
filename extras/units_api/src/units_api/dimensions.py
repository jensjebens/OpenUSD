from typing import NamedTuple
from enum import Enum


class Dimension(NamedTuple):
    """Dimensional exponents for physical quantities."""
    L: int = 0  # length
    M: int = 0  # mass
    T: int = 0  # time


class AxisTransform(Enum):
    """How an attribute value transforms under an up-axis change.

    NONE:       Scalar or axis-invariant — no correction needed.
    VECTOR3:    3-vector — apply the axis-change rotation matrix.
    AXIS_TOKEN: Token naming an axis ("X", "Y", "Z") — remap through the mapping.
    """
    NONE = "none"
    VECTOR3 = "vector3"
    AXIS_TOKEN = "axisToken"


# Registry: attr_name -> Dimension
DIMENSION_REGISTRY: dict[str, Dimension] = {
    # Transforms / geometry
    "xformOp:translate": Dimension(L=1),
    "xformOp:transform": Dimension(L=1),   # matrix — only translation component scales
    "xformOp:scale": Dimension(),           # unitless ratio
    "points": Dimension(L=1),
    "extent": Dimension(L=1),
    "size": Dimension(L=1),             # UsdGeom.Cube

    # Camera (scene-unit attributes only)
    "focusDistance": Dimension(L=1),
    "clippingRange": Dimension(L=1),
    # focalLength, horizontalAperture are in mm per schema — NOT scene units, excluded

    # Lights
    "inputs:width": Dimension(L=1),
    "inputs:height": Dimension(L=1),
    "inputs:radius": Dimension(L=1),
    "inputs:length": Dimension(L=1),

    # Physics
    "physics:velocity": Dimension(L=1, T=-1),
    "physics:angularVelocity": Dimension(),     # rad/s — unitless wrt length
    "physics:density": Dimension(L=-3, M=1),
    "physics:mass": Dimension(M=1),
    "physics:gravityMagnitude": Dimension(L=1, T=-2),

    # PointInstancer
    "positions": Dimension(L=1),
    "velocities": Dimension(L=1, T=-1),
    "accelerations": Dimension(L=1, T=-2),
    "angularVelocities": Dimension(),           # rad/s — unitless wrt length
    "orientations": Dimension(),                # unitless (quaternion)
    "orientationsf": Dimension(),               # unitless (quaternion)

    # Unitless
    "visibility": Dimension(),
    "purpose": Dimension(),
    "doubleSided": Dimension(),
}


# ---------------------------------------------------------------------------
# Axis transform registry: how each attribute transforms under up-axis change
# ---------------------------------------------------------------------------

AXIS_TRANSFORM_REGISTRY: dict[str, AxisTransform] = {
    # Spatial vectors — rotate
    "xformOp:translate": AxisTransform.VECTOR3,
    "points": AxisTransform.VECTOR3,
    "extent": AxisTransform.VECTOR3,
    "positions": AxisTransform.VECTOR3,
    "velocities": AxisTransform.VECTOR3,
    "accelerations": AxisTransform.VECTOR3,
    "physics:velocity": AxisTransform.VECTOR3,
    "physics:angularVelocity": AxisTransform.VECTOR3,

    # Scalars / magnitudes — no axis correction
    "physics:density": AxisTransform.NONE,
    "physics:mass": AxisTransform.NONE,
    "physics:gravityMagnitude": AxisTransform.NONE,
    "size": AxisTransform.NONE,
    "focusDistance": AxisTransform.NONE,
    "clippingRange": AxisTransform.NONE,

    # Lights — scalar dimensions, not directional
    "inputs:width": AxisTransform.NONE,
    "inputs:height": AxisTransform.NONE,
    "inputs:radius": AxisTransform.NONE,
    "inputs:length": AxisTransform.NONE,

    # Transform matrices handled by xformOp assembly, not per-value rotation
    "xformOp:transform": AxisTransform.NONE,
    "xformOp:scale": AxisTransform.NONE,

    # Unitless / non-spatial
    "visibility": AxisTransform.NONE,
    "purpose": AxisTransform.NONE,
    "doubleSided": AxisTransform.NONE,
    "orientations": AxisTransform.NONE,
    "orientationsf": AxisTransform.NONE,
    "angularVelocities": AxisTransform.NONE,  # rad/s — magnitude, direction implicit
}


def get_axis_transform(attr_name: str) -> AxisTransform | None:
    """Look up axis-transform type for an attribute name.
    Returns None if not in registry (unknown attribute)."""
    return AXIS_TRANSFORM_REGISTRY.get(attr_name)


def axis_rotation_matrix(source_up: str, target_up: str):
    """Return the 3×3 rotation matrix for an up-axis change, or None if same.

    Y→Z: rotate −90° around X.
    Z→Y: rotate +90° around X.
    """
    if source_up == target_up:
        return None
    from pxr import Gf
    if source_up == "Y" and target_up == "Z":
        return Gf.Matrix3d(Gf.Rotation(Gf.Vec3d(1, 0, 0), -90.0))
    if source_up == "Z" and target_up == "Y":
        return Gf.Matrix3d(Gf.Rotation(Gf.Vec3d(1, 0, 0), 90.0))
    return None


def remap_axis_token(token: str, source_up: str, target_up: str) -> str:
    """Remap an axis-name token through an up-axis change.

    For Y↔Z swaps: "Y"↔"Z", "X" stays.
    Returns the token unchanged if axes match or the token isn't an axis name.
    """
    if source_up == target_up:
        return token
    mapping = {}
    if {source_up, target_up} == {"Y", "Z"}:
        mapping = {"Y": "Z", "Z": "Y", "X": "X"}
    return mapping.get(token, token)


def get_dimension(attr_name: str) -> Dimension | None:
    """Look up dimensional exponents for an attribute name.
    Returns None if not in registry (unknown attribute)."""
    return DIMENSION_REGISTRY.get(attr_name)


def conversion_factor(
    source_mpu: float,
    target_mpu: float,
    dimension: Dimension,
    source_kpu: float = 1.0,
    target_kpu: float = 1.0,
) -> float:
    """Calculate the conversion factor for a given dimension.

    For length (L=1):   factor = source_mpu / target_mpu
    For density (L=-3, M=1): factor = (source_mpu/target_mpu)^-3 * (source_kpu/target_kpu)^1
    General: product of (source/target)^exponent for each base unit.

    Time is excluded from the unit system (no secondsPerUnit) so T exponents
    contribute a factor of 1.0.
    """
    factor = 1.0
    if dimension.L != 0:
        factor *= (source_mpu / target_mpu) ** dimension.L
    if dimension.M != 0:
        factor *= (source_kpu / target_kpu) ** dimension.M
    # T exponent: no timePerUnit concept, factor = 1
    return factor
