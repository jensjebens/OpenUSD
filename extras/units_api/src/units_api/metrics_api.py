from pxr import Usd, UsdGeom

# Namespace key used in prim customData to store MetricsAPI values.
_NAMESPACE = "units_api"

# USD defaults (centimeters, Y-up, 1 kg)
_DEFAULT_METERS_PER_UNIT = 0.01
_DEFAULT_UP_AXIS = "Y"
_DEFAULT_KILOGRAMS_PER_UNIT = 1.0


class MetricsAPI:
    """Applied schema simulation for prim-level unit declarations.

    Uses customData["units_api"] on prims to store unit context.
    Ancestor walk provides inheritance; stage-level metadata is the final fallback.
    """

    @staticmethod
    def apply(
        prim: Usd.Prim,
        meters_per_unit: float | None = None,
        up_axis: str | None = None,
        kilograms_per_unit: float | None = None,
    ) -> None:
        """Apply metrics to a prim (stores in customData to simulate applied schema)."""
        existing = dict(prim.GetCustomDataByKey(_NAMESPACE) or {})
        if meters_per_unit is not None:
            existing["metersPerUnit"] = meters_per_unit
        if up_axis is not None:
            existing["upAxis"] = up_axis
        if kilograms_per_unit is not None:
            existing["kilogramsPerUnit"] = kilograms_per_unit
        prim.SetCustomDataByKey(_NAMESPACE, existing)

    @staticmethod
    def has_metrics(prim: Usd.Prim) -> bool:
        """Check if a prim has MetricsAPI applied."""
        data = prim.GetCustomDataByKey(_NAMESPACE)
        return bool(data)

    @staticmethod
    def get_metrics(prim: Usd.Prim) -> dict:
        """Get metrics directly authored on this prim (not inherited)."""
        data = prim.GetCustomDataByKey(_NAMESPACE)
        return dict(data) if data else {}

    @staticmethod
    def get_effective_metrics(prim: Usd.Prim) -> dict:
        """Walk up ancestor chain to find effective metrics context.

        Search order:
          1. The prim itself
          2. Each ancestor up to (and including) the pseudo-root
          3. Stage-level layer metadata
          4. USD defaults (cm, Y-up, 1 kg)

        Returns dict with metersPerUnit, upAxis, kilogramsPerUnit.
        """
        # Walk prim and all ancestors
        current = prim
        while current.IsValid():
            data = current.GetCustomDataByKey(_NAMESPACE)
            if data:
                result = {}
                result["metersPerUnit"] = data.get("metersPerUnit", None)
                result["upAxis"] = data.get("upAxis", None)
                result["kilogramsPerUnit"] = data.get("kilogramsPerUnit", None)
                # Fill missing keys from further up the chain or stage metadata
                if all(v is not None for v in result.values()):
                    return result
                # Some keys missing — fill from higher levels
                parent_metrics = MetricsAPI._stage_or_default_metrics(prim.GetStage())
                return {
                    "metersPerUnit": result["metersPerUnit"] if result["metersPerUnit"] is not None else parent_metrics["metersPerUnit"],
                    "upAxis": result["upAxis"] if result["upAxis"] is not None else parent_metrics["upAxis"],
                    "kilogramsPerUnit": result["kilogramsPerUnit"] if result["kilogramsPerUnit"] is not None else parent_metrics["kilogramsPerUnit"],
                }
            parent = current.GetParent()
            if not parent.IsValid():
                break
            current = parent

        # No ancestor had MetricsAPI — fall back to stage-level metadata
        return MetricsAPI._stage_or_default_metrics(prim.GetStage())

    @staticmethod
    def get_meters_per_unit(prim: Usd.Prim) -> float:
        """Convenience: get effective metersPerUnit for a prim."""
        return MetricsAPI.get_effective_metrics(prim)["metersPerUnit"]

    @staticmethod
    def get_up_axis(prim: Usd.Prim) -> str:
        """Convenience: get effective upAxis for a prim."""
        return MetricsAPI.get_effective_metrics(prim)["upAxis"]

    @staticmethod
    def get_kilograms_per_unit(prim: Usd.Prim) -> float:
        """Convenience: get effective kilogramsPerUnit for a prim."""
        return MetricsAPI.get_effective_metrics(prim)["kilogramsPerUnit"]

    # ------------------------------------------------------------------
    # Internal helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _stage_or_default_metrics(stage: Usd.Stage) -> dict:
        """Read stage-level layer metadata, falling back to USD defaults."""
        mpu = UsdGeom.GetStageMetersPerUnit(stage)
        up = UsdGeom.GetStageUpAxis(stage)
        # UsdPhysics doesn't expose GetStageKilogramsPerUnit in all builds;
        # use a safe fallback.
        try:
            from pxr import UsdPhysics
            kpu = UsdPhysics.GetStageKilogramsPerUnit(stage)
            if kpu == 0.0:
                kpu = _DEFAULT_KILOGRAMS_PER_UNIT
        except (ImportError, AttributeError):
            kpu = _DEFAULT_KILOGRAMS_PER_UNIT

        return {
            "metersPerUnit": mpu if mpu and mpu > 0 else _DEFAULT_METERS_PER_UNIT,
            "upAxis": up if up else _DEFAULT_UP_AXIS,
            "kilogramsPerUnit": kpu,
        }
