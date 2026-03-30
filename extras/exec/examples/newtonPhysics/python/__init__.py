# SPDX-License-Identifier: Apache-2.0
#
# Newton GPU physics integration for OpenUSD via OpenExec.
#
# This module wraps the Newton GPU physics engine (newton-physics/newton)
# and registers a TransformProvider with HdExecComputedTransformSceneIndex
# so that physics transforms flow through the OpenExec → Hydra pipeline
# without baking or session layers.
#
# Newton GPU is Python-first and GPU-accelerated via NVIDIA Warp.
# This integration uses ModelBuilder.add_usd() to parse physics schemas
# directly from a USD stage.

"""Newton GPU physics integration for OpenUSD OpenExec pipeline."""

from .engine import NewtonEngine
from .provider import register_provider, unregister_provider

__all__ = [
    "NewtonEngine",
    "register_provider",
    "unregister_provider",
]
