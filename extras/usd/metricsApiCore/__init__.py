from pxr import Plug

# Register this plugin so plugInfo.json is discovered.
Plug.Registry().GetAllPlugins()

# Schema-generated classes will be available after usdGenSchema.
# For now, expose the dimensional registry Python wrapper.
try:
    from . import _usdMetricsApi
    from ._usdMetricsApi import *
except ImportError:
    # Not yet built — schema generation hasn't run
    pass
