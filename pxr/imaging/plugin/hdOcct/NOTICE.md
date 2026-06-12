# NOTICE — Third-Party Dependency: OpenCASCADE Technology (OCCT)

## hdOcct Plugin — LGPL-2.1 Compliance

The `hdOcct` imaging plugin and `usdsolidtessellate` CLI tool optionally link
against [OpenCASCADE Technology (OCCT)](https://dev.opencascade.org/), which
is licensed under the **GNU Lesser General Public License v2.1 (LGPL-2.1)**.

### Compliance Strategy

This project complies with LGPL-2.1 via **dynamic linking only**:

1. **No OCCT source code is included** in this repository.
2. **No OCCT libraries are statically linked** into any binary.
3. The `hdOcct` shared library (`.so`/`.dylib`/`.dll`) links against
   system-installed OCCT shared libraries at runtime.
4. Users can substitute their own LGPL-compliant OCCT build by providing
   compatible shared libraries at the expected ABI version.

### Build Requirements

When `PXR_BUILD_OCCT_PLUGIN=ON`:
- OCCT ≥ 7.5 shared libraries must be available on the system.
- On Debian/Ubuntu: `apt install libocct-*-dev`
- On macOS (Homebrew): `brew install opencascade`
- On Windows: Download from https://dev.opencascade.org/release

### OCCT Modules Used

The following OCCT toolkit shared libraries are linked:
- `TKernel` — Foundation classes
- `TKMath` — Mathematical algorithms
- `TKG2d`, `TKG3d` — 2D/3D geometry
- `TKGeomBase` — Geometric base classes
- `TKBRep` — Boundary representation
- `TKGeomAlgo` — Geometric algorithms
- `TKTopAlgo` — Topological algorithms
- `TKShHealing` — Shape healing (for pcurve derivation)
- `TKMesh` — Meshing/tessellation

### Rights Under LGPL-2.1

Users of the `hdOcct` plugin retain the right to:
- Replace the OCCT shared libraries with modified versions
- Use the plugin with any LGPL-2.1-compatible OCCT build
- Receive the OCCT source code (available at https://dev.opencascade.org/)

### This Plugin's License

The `hdOcct` plugin source code itself (all files in `pxr/imaging/plugin/hdOcct/`
and `extras/usd/usdSolidTessellator/`) is licensed under the Apache License 2.0,
consistent with the rest of OpenUSD.
