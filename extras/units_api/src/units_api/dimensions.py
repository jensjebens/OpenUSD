from typing import NamedTuple


class Dimension(NamedTuple):
    """Dimensional exponents for physical quantities."""
    L: int = 0  # length
    M: int = 0  # mass
    T: int = 0  # time


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
