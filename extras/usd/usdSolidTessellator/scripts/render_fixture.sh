#!/bin/bash
# render_fixture.sh — Render a BrepArray fixture via CLI or Hydra path
#
# Usage:
#   ./render_fixture.sh <fixture.usda> [output.jpg] [--hydra]
#
# Examples:
#   ./render_fixture.sh testenv/fixtures/testCube.usda                    # CLI path
#   ./render_fixture.sh testenv/fixtures/testCube.usda cube.jpg --hydra   # Hydra live path
#
# Environment:
#   USD_INSTALL    — path to USD install dir (default: auto-detected)
#   PYTHON         — python binary (default: auto-detected)
#   IMAGE_WIDTH    — render width in pixels (default: 960)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# PROJECT_DIR is the usd-tessellation root (contains OpenUSD/ and usd-install/)
PROJECT_DIR="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"

# --- Defaults ---
USD_INSTALL="${USD_INSTALL:-$PROJECT_DIR/usd-install}"
IMAGE_WIDTH="${IMAGE_WIDTH:-960}"
HYDRA_MODE=false

# --- Auto-detect Python with pxr ---
if [ -z "${PYTHON:-}" ]; then
    # Prefer system python3.11 that has pxr, fall back to hermes venv
    for candidate in /usr/bin/python3.11 /home/horde/.hermes/hermes-agent/venv/bin/python3.11 python3.11 python3; do
        if [ -x "$candidate" ] 2>/dev/null || command -v "$candidate" &>/dev/null; then
            PYTHON="$candidate"
            break
        fi
    done
fi

# --- usdrecord script (NOT the bin/ wrapper which has import issues) ---
USDRECORD="$PROJECT_DIR/OpenUSD/pxr/usdImaging/bin/usdrecord/usdrecord.py"
if [ ! -f "$USDRECORD" ]; then
    echo "ERROR: usdrecord.py not found at $USDRECORD" >&2
    exit 1
fi

# --- Parse args ---
FIXTURE=""
OUTPUT=""
for arg in "$@"; do
    case "$arg" in
        --hydra) HYDRA_MODE=true ;;
        *)
            if [ -z "$FIXTURE" ]; then
                FIXTURE="$arg"
            elif [ -z "$OUTPUT" ]; then
                OUTPUT="$arg"
            fi
            ;;
    esac
done

if [ -z "$FIXTURE" ]; then
    echo "Usage: $0 <fixture.usda> [output.jpg] [--hydra]" >&2
    exit 1
fi

# Resolve fixture path
if [ ! -f "$FIXTURE" ]; then
    # Try relative to testenv/fixtures/
    FIXTURE="$PROJECT_DIR/OpenUSD/extras/usd/usdSolidTessellator/testenv/fixtures/$FIXTURE"
fi
if [ ! -f "$FIXTURE" ]; then
    echo "ERROR: Fixture not found: $FIXTURE" >&2
    exit 1
fi

FIXTURE="$(realpath "$FIXTURE")"
BASENAME="$(basename "$FIXTURE" .usda)"

# Default output
if [ -z "$OUTPUT" ]; then
    OUTPUT="/tmp/render_${BASENAME}.jpg"
fi

# --- Extract prim path from fixture ---
PRIM_PATH=$(grep 'def BrepArray' "$FIXTURE" | head -1 | sed 's/.*def BrepArray "\([^"]*\)".*/\/World\/\1/')

# --- Camera harness ---
# Wider camera: focalLength 24, positioned at (40,30,40), looking down at -25° pitch
HARNESS="/tmp/harness_render_${BASENAME}.usda"

if [ "$HYDRA_MODE" = true ]; then
    # Hydra path: reference raw BrepArray fixture directly
    ASSET_REF="$FIXTURE"
else
    # CLI path: tessellate first, then render the mesh
    TESS_OUTPUT="/tmp/tess_${BASENAME}.usda"
    env -i HOME="/home/horde" \
        LD_LIBRARY_PATH="$USD_INSTALL/lib" \
        PXR_PLUGINPATH_NAME="$USD_INSTALL/plugin/usd/usdSolid/resources" \
        "$USD_INSTALL/bin/usdsolidtessellate" "$FIXTURE" "$TESS_OUTPUT" "$PRIM_PATH" 2>&1 | grep -v "^Warning:"
    ASSET_REF="$TESS_OUTPUT"
fi

cat > "$HARNESS" <<EOF
#usda 1.0
(
    defaultPrim = "World"
    upAxis = "Y"
)
def Xform "World"
{
    def "Asset" (
        references = @${ASSET_REF}@
    ) {}
    def Camera "Camera"
    {
        float focalLength = 24
        double3 xformOp:translate = (40, 30, 40)
        float3 xformOp:rotateXYZ = (-25, 45, 0)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }
}
EOF

# --- Render environment ---
# Note: HOME must be /home/horde (not the hermes profile home) for .Xauthority
REAL_HOME="/home/horde"
RENDER_ENV=(
    env -i
    HOME="$REAL_HOME"
    DISPLAY=:99
    XAUTHORITY="$REAL_HOME/.Xauthority"
    QT_QPA_PLATFORM=offscreen
    LD_LIBRARY_PATH="$USD_INSTALL/lib"
    PYTHONPATH="$USD_INSTALL/lib/python"
    PXR_PLUGINPATH_NAME="$USD_INSTALL/plugin/usd/hdOcct/resources:$USD_INSTALL/plugin/usd/usdSolid/resources"
    PATH=/usr/bin:/bin
)

if [ "$HYDRA_MODE" = true ]; then
    RENDER_ENV+=(HDGP_INCLUDE_DEFAULT_RESOLVER=1)
fi

"${RENDER_ENV[@]}" "$PYTHON" "$USDRECORD" \
    --renderer Storm \
    --imageWidth "$IMAGE_WIDTH" \
    --camera /World/Camera \
    "$HARNESS" "$OUTPUT" 2>&1 | grep -v "^Warning:"

echo "Rendered: $OUTPUT ($(stat -c%s "$OUTPUT") bytes)"
if [ "$HYDRA_MODE" = true ]; then
    echo "Path: Hydra live (BrepArray → adapter → procedural → Storm)"
else
    echo "Path: CLI (usdsolidtessellate → Mesh → usdrecord)"
fi
