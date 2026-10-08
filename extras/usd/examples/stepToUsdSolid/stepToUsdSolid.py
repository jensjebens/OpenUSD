#!/usr/bin/env python
# SPDX-License-Identifier: Apache-2.0
"""
stepToUsdSolid -- convert a STEP (ISO 10303-21/-42) file to a UsdSolid B-rep stage.

A reference importer for the UsdSolid schema: it reads a STEP part or assembly and
writes one Xform + BrepArray prim per body (solid or sheet), authored through the
pxr UsdSolid schema API. It is pure Python plus USD -- no CAD kernel is involved -- so it
doubles as a self-contained way to produce B-rep test data for the schema and its
validators.

    stepToUsdSolid input.stp output.usdc

The converter has no modes to choose. It handles planar/cylindrical/conical/
spherical/toroidal analytic surfaces, line/circle/ellipse curves (bare or wrapped in
SURFACE_CURVE / SEAM_CURVE), and NURBS surfaces and curves; lowers swept surfaces
(linear extrusion, revolution) to NURBS; resolves void shells (BREP_WITH_VOIDS /
ORIENTED_CLOSED_SHELL), each cavity's sides from the volume its face normals
enclose, and vertex loops; converts sheet bodies
(SHELL_BASED_SURFACE_MODEL) beside solids; derives the parametric UV window of each
analytic face from its trimming edges (a NURBS face takes its surface's knot
domain); and reads per-body / per-face colors from STEP styled items. The
plane-angle unit, the length unit (from the context of the bodies'
representations) and the tolerance are read from the file.

Scope: an assembly comes through as one Xform per NEXT_ASSEMBLY_USAGE_OCCURRENCE
placement, with the part's bodies as BrepArray children; a body no placement
reaches, like every body of a file with no assembly structure, is a top-level
prim in its own coordinates. A body that is its part's only body is named after
the part's STEP PRODUCT, and a body of a part with several records the part in
its customData (body_products); every name is made a valid, unique USD name
(product_names, _prim_name). This is a
reference/sample importer, like the gsplat ply-to-usd sample -- not a production
STEP importer.
"""
import re, math
from collections import Counter
from pxr import Usd, UsdGeom, UsdSolid, Vt, Gf, Sdf

# ================================================================ ISO 10303-21 parse
def parse_step(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    m = re.search(r"\bDATA\s*;(.*?)\bENDSEC\s*;", text, flags=re.S)
    body = m.group(1) if m else text
    entities = {}
    for rec in re.findall(r"#(\d+)\s*=\s*(.*?);\s*(?=#\d+\s*=|\Z)", body, flags=re.S):
        entities[int(rec[0])] = _parse_instance(rec[1].strip())
    return entities

def _parse_instance(s):
    s = s.strip()
    if s.startswith("("):                      # complex (multi-type) instance
        inner = s[1:_match_paren(s, 0)]
        subs, i, n = [], 0, len(inner)
        while i < n:
            m = re.match(r"\s*([A-Za-z_0-9]+)\s*\(", inner[i:])
            if not m:
                i += 1
                continue
            popen = i + m.end() - 1
            pclose = _match_paren(inner, popen)
            argstr = inner[popen + 1:pclose]
            subs.append((m.group(1), [_coerce(x.strip()) for x in _split_top(argstr)]))
            i = pclose + 1
        return ("__COMPLEX__", subs)
    mt = re.match(r"([A-Z0-9_]+)\s*\((.*)\)\s*$", s, flags=re.S)
    if not mt:
        return (None, [])
    return (mt.group(1), [_coerce(a.strip()) for a in _split_top(mt.group(2))])

def _match_paren(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "(": depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0: return j
    return len(s) - 1

def _split_top(s):
    out, depth, buf, inq = [], 0, [], False
    for c in s:
        if c == "'":
            inq = not inq
            buf.append(c)
        elif inq: buf.append(c)
        elif c == "(":
            depth += 1
            buf.append(c)
        elif c == ")":
            depth -= 1
            buf.append(c)
        elif c == "," and depth == 0:
            out.append("".join(buf))
            buf = []
        else: buf.append(c)
    if buf: out.append("".join(buf))
    return out

def _coerce(tok):
    tok = tok.strip()
    if not tok: return None
    if tok.startswith("#"): return ("ref", int(tok[1:]))
    if tok.startswith("'") and tok.endswith("'"): return tok[1:-1]
    if tok.startswith("(") and tok.endswith(")"):
        return [_coerce(x.strip()) for x in _split_top(tok[1:-1])]
    if tok.startswith(".") and tok.endswith("."): return ("enum", tok[1:-1])
    if tok in ("*", "$"): return tok
    try:
        return float(tok) if re.search(r"[.eE]", tok) else int(tok)
    except ValueError:
        return tok

# ================================================================ vector helpers
def vsub(a, b): return tuple(a[i]-b[i] for i in range(3))
def vdot(a, b): return sum(a[i]*b[i] for i in range(3))
def vnorm(a):
    n = math.sqrt(vdot(a, a)) or 1.0
    return tuple(c/n for c in a)
def vcross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])

_HALF_PI = math.pi / 2.0
_TWO_PI = 2.0 * math.pi

# ---------------------------------------------------------------- tolerances
# Every tolerance the reader uses, in one place. These are geometric decisions,
# not tuning knobs: each says what counts as "the same" at a particular scale.
#
# PERIOD_TOL     how far past 2*pi an angular bound may sit and still count as
#                the primary period. An exporter writing 2*pi as 6.2831853072
#                overshoots by 2e-11; a domain that genuinely wraps twice is
#                nowhere near this. Matches PERIOD_TOL in brep_validator.py.
# DEGENERATE_TOL a parameter span at or below this is a point, not an interval.
# COINCIDENT_TOL two positions this close are the same vertex, in model units.
# SNAP_TOL       a NURBS knot or weight within this of a round value is that
#                value; keeps rational arcs exactly rational.
# PAD_FRAC       how far a derived face window is widened past its boundary
# PAD_MIN        samples, so a face is not trimmed exactly through a vertex.
PERIOD_TOL = 1e-6
DEGENERATE_TOL = 1e-9
COINCIDENT_TOL = 1e-12
SNAP_TOL = 1e-6
PAD_FRAC = 0.02
PAD_MIN = 1e-4
# MIN_INTERSECT_TOL floors the tolerance derived from a file's own
# UNCERTAINTY_MEASURE. Real CAD vertices meet to about 1e-6 of model scale;
# a file declaring far tighter agreement than it delivers would otherwise
# make every endpoint check fail. It is a length in millimetres, scaled to the
# file's length unit (derive_tolerance), as is Config's NURBS end-weld floor.
MIN_INTERSECT_TOL = 5e-4        # millimetres

class Reader:
    def __init__(self, ents): self.e = ents
    def get(self, ref): return self.e.get(ref[1]) if isinstance(ref, tuple) and ref[0]=="ref" else None
    def typ(self, ref):
        t = self.get(ref)
        return t[0] if t else None
    def args(self, ref):
        t = self.get(ref)
        return t[1] if t else []
    def find(self, *types): return [i for i,(t,_) in self.e.items() if t in types]
    def point(self, ref): return tuple(float(x) for x in self.args(ref)[1])
    def direction(self, ref): return tuple(float(x) for x in self.args(ref)[1])
    def placement(self, ref):
        a = self.args(ref)
        o = self.point(a[1])
        z = self.direction(a[2]) if len(a)>2 and isinstance(a[2],tuple) else (0,0,1)
        x = self.direction(a[3]) if len(a)>3 and isinstance(a[3],tuple) else (1,0,0)
        return o, vnorm(z), vnorm(x)

# ================================================================ Config (hook points)
class Config:
    """Per-file conversion parameters, all read from the STEP file itself.

    There are no behavior modes. Every correctness fix in this reader is applied
    unconditionally; each one is a no-op when the STEP construct it handles is
    absent (a file with no swept surfaces never lowers one, a file with no void
    shells never resolves one, and so on). The things that legitimately vary from
    file to file are the plane-angle unit, the length unit and the tolerance
    ladder, all derived from the file (detect_angle_scale,
    detect_meters_per_unit, derive_tolerance). Absolute lengths are stated in
    millimetres and scaled to the file's unit (``mm``).
    """
    def __init__(self, angle_scale=1.0, intersect_tol=1e-6, meters_per_unit=0.001,
                 accuracy=None):
        self.angle_scale = angle_scale        # raw STEP plane-angle -> radians
        self.intersect_tol = intersect_tol    # brep:intersectTol3d
        self.edge_degen_tol = intersect_tol   # degenerate-edge stub tolerance
        # Model units per millimetre: the lengths below are stated in mm.
        self.mm = 0.001 / float(meters_per_unit or 0.001)
        # NURBS endpoint weld: an authored hull end this close to its vertex is
        # moved onto it. Never less than the accuracy the file declares
        # (accuracy, model units): points the producer calls the same are.
        self.inv_accept = max(1e-3 * self.mm, intersect_tol * 10.0, accuracy or 0.0)
        # Distance at which an authored control hull end is deemed already at a
        # vertex (skip the trim). A fixed geometric snap, independent of the
        # per-file tolerance, so which edges get hull-trimmed stays stable.
        self.nurb_snap_tol = SNAP_TOL


# ================================================================ plane-angle unit
def detect_angle_scale(rd, ents):
    """Plane-angle unit scale (raw -> radians). NX/ST-Developer files declare a
    DEGREE CONVERSION_BASED_UNIT and author cone semi-angles in DEGREES even though
    a RADIAN SI_UNIT is also present. Returns the conversion factor."""
    for i, (t, a) in ents.items():
        if t == "__COMPLEX__":
            names = [s[0] for s in a]
            if "CONVERSION_BASED_UNIT" in names and "PLANE_ANGLE_UNIT" in names:
                subs = dict(a)
                cbu = subs["CONVERSION_BASED_UNIT"]
                nm = (cbu[0] or "").upper()
                meas = rd.get(cbu[1]) if isinstance(cbu[1], tuple) else None
                if meas and meas[0] == "PLANE_ANGLE_MEASURE_WITH_UNIT":
                    mstr = meas[1][0]
                    try:
                        return float(mstr[mstr.index("(")+1:mstr.rindex(")")])
                    except Exception:
                        pass
                if "DEGREE" in nm:
                    return math.pi/180.0
    return 1.0

_SI_PREFIX = {"EXA": 1e18, "PETA": 1e15, "TERA": 1e12, "GIGA": 1e9, "MEGA": 1e6,
              "KILO": 1e3, "HECTO": 1e2, "DECA": 1e1, "DECI": 1e-1, "CENTI": 1e-2,
              "MILLI": 1e-3, "MICRO": 1e-6, "NANO": 1e-9, "PICO": 1e-12,
              "FEMTO": 1e-15, "ATTO": 1e-18}

#: The representations a body (MANIFOLD_SOLID_BREP, BREP_WITH_VOIDS,
#: SHELL_BASED_SURFACE_MODEL) hangs off; each names its context, and the
#: context its units.
_BODY_REPRESENTATIONS = ("ADVANCED_BREP_SHAPE_REPRESENTATION",
                         "MANIFOLD_SURFACE_SHAPE_REPRESENTATION", "SHAPE_REPRESENTATION")
_BODY_TYPES = ("MANIFOLD_SOLID_BREP", "BREP_WITH_VOIDS", "SHELL_BASED_SURFACE_MODEL")

def _measure_value(x):
    """The number in a typed measure parameter, LENGTH_MEASURE(25.4) as the
    parser keeps it, or a bare number; None otherwise."""
    if isinstance(x, (int, float)):
        return float(x)
    if isinstance(x, str) and "(" in x:
        try:
            return float(x[x.index("(") + 1:x.rindex(")")])
        except ValueError:
            return None
    return None

def length_unit_mpu(rd, ref, depth=0):
    """Metres per unit of the LENGTH_UNIT at ref: an SI metre with its prefix,
    or a CONVERSION_BASED_UNIT through the measure it names (an inch is read
    as 25.4 of the millimetre it is defined against, never assumed from its
    name). None for anything else."""
    e = rd.get(ref)
    if not e or e[0] != "__COMPLEX__" or depth > 8:
        return None
    subs = dict(e[1])
    if "LENGTH_UNIT" not in subs:
        return None
    si = subs.get("SI_UNIT")
    if si and si[-1] == ("enum", "METRE"):
        pre = si[0]
        return _SI_PREFIX.get(pre[1]) if isinstance(pre, tuple) else 1.0
    cbu = subs.get("CONVERSION_BASED_UNIT")
    if cbu and len(cbu) > 1 and isinstance(cbu[1], tuple):
        m = rd.get(cbu[1])
        margs = None
        if m and m[0] in ("LENGTH_MEASURE_WITH_UNIT", "MEASURE_WITH_UNIT"):
            margs = m[1]
        elif m and m[0] == "__COMPLEX__":
            margs = dict(m[1]).get("MEASURE_WITH_UNIT")
        if margs and len(margs) > 1:
            v = _measure_value(margs[0])
            base = length_unit_mpu(rd, margs[1], depth + 1)
            if v and base:
                return v * base
    return None

def _context_length_mpu(rd, ctx):
    """Metres per unit of the length unit a representation context assigns
    (GLOBAL_UNIT_ASSIGNED_CONTEXT), or None."""
    units = _complex_parts(rd, ctx).get("GLOBAL_UNIT_ASSIGNED_CONTEXT")
    for u in (units[0] if units and isinstance(units[0], list) else []):
        mpu = length_unit_mpu(rd, u)
        if mpu:
            return mpu
    return None

def detect_meters_per_unit(rd, ents, body_ids=None):
    """Metres per model length unit: the length unit of the context of the
    representations that hold the bodies converted (body_ids; every body of
    the file when None). A unit the file declares for anything else, such as
    the metre of a material density, does not count. Where those
    representations disagree, the first body's wins, with a warning: each part
    would need its own scale. Without a body representation that names one,
    the length unit of the file's geometric contexts, if they agree. None
    where neither gives a unit."""
    if body_ids is None:
        body_ids = rd.find(*_BODY_TYPES)
    ids = set(body_ids)
    found = []
    for r in rd.find(*_BODY_REPRESENTATIONS):
        a = rd.args(("ref", r))
        items = a[1] if len(a) > 1 and isinstance(a[1], list) else []
        if len(a) > 2 and isinstance(a[2], tuple) and any(
                isinstance(x, tuple) and x[0] == "ref" and x[1] in ids for x in items):
            mpu = _context_length_mpu(rd, a[2])
            if mpu:
                found.append(mpu)
    if not found:
        found = sorted({m for i, (t, a) in ents.items()
                        if t == "__COMPLEX__" and any(n == "GEOMETRIC_REPRESENTATION_CONTEXT"
                                                      for n, _ in a)
                        for m in [_context_length_mpu(rd, ("ref", i))] if m})
        if len(found) != 1:
            return None
    if len(set(found)) > 1:
        print(f"  warning: the bodies' representations declare different length units "
              f"({', '.join(f'{m:g} m' for m in sorted(set(found)))}); using the first, "
              f"{found[0]:g} m")
    return found[0]

def detect_length_uncertainty(rd, ents, meters_per_unit=None):
    """File's declared length uncertainty in model units, the producer's real
    endpoint-agreement tolerance: its UNCERTAINTY_MEASURE_WITH_UNIT, converted
    from the length unit it names when that is not the model's
    (meters_per_unit). Returns (value, source_str) or (None,'absent')."""
    best = None
    for i, (t, a) in ents.items():
        if t == "UNCERTAINTY_MEASURE_WITH_UNIT":
            raw = a[0]
            val = None
            if isinstance(raw, str) and "(" in raw:
                try: val = float(raw[raw.index("(")+1:raw.rindex(")")])
                except Exception: val = None
            elif isinstance(raw, (int, float)):
                val = float(raw)
            if val is not None and val > 0:
                umpu = length_unit_mpu(rd, a[1]) if len(a) > 1 and isinstance(a[1], tuple) else None
                if umpu and meters_per_unit:
                    val = val * umpu / float(meters_per_unit)
                nm = ""
                for x in a[1:]:
                    if isinstance(x, str) and "ACCURACY" in x.upper():
                        nm = x
                if best is None or nm:
                    best = (val, "UNCERTAINTY_MEASURE_WITH_UNIT" + (":"+nm if nm else ""))
    return best if best else (None, "absent")


# ================================================================ knot expansion
def expand_knots(knots, mults):
    out = []
    for k, m in zip(knots, mults):
        out += [float(k)]*int(m)
    return out

# ================================================================ swept lowering
def _lower_curve_to_nurb_local(rd, cref, ulo=None, uhi=None):
    """Lower a 3D curve to LOCAL-space NURBS poles (Martin Watt's LowerCurveToNurbLocal
    plus a CIRCLE/ELLIPSE rational-quadratic branch). Returns a poles dict or None."""
    t = rd.typ(cref)
    a = rd.args(cref)
    if t == "LINE":
        o = rd.point(a[1])
        vec = rd.args(a[2])
        d = vnorm(rd.direction(vec[1]))
        lo = 0.0 if ulo is None else ulo
        hi = 1.0 if uhi is None else uhi
        if not (hi > lo): lo, hi = 0.0, 1.0
        p0 = tuple(o[k] + lo*d[k] for k in range(3))
        p1 = tuple(o[k] + hi*d[k] for k in range(3))
        return dict(order=2, poles=[p0, p1], weights=[1.0, 1.0], knots=[lo, lo, hi, hi], plo=lo, phi=hi)
    if t in ("CIRCLE", "ELLIPSE"):
        o, z, x = rd.placement(a[1])
        y = vcross(z, x)
        rx = float(a[2])
        ry = float(a[2]) if t == "CIRCLE" else float(a[3])
        a0 = 0.0 if ulo is None else ulo
        a1 = 2*math.pi if uhi is None else uhi
        if not (a1 > a0): a0, a1 = 0.0, 2*math.pi
        return _arc_poles(o, x, y, rx, ry, a0, a1)
    if t in ("B_SPLINE_CURVE_WITH_KNOTS", "__COMPLEX__"):
        cg = bspline_curve(rd, cref)
        return dict(order=cg["order"], poles=cg["controlVertices"], weights=cg["weights"],
                    knots=cg["knots"], plo=cg["knots"][0], phi=cg["knots"][-1])
    return None

def _arc_poles(center, xdir, ydir, rx, ry, a0, a1):
    """Rational quadratic NURBS poles for an (elliptical) arc a0..a1."""
    def on(ang):
        return tuple(center[k] + rx*math.cos(ang)*xdir[k] + ry*math.sin(ang)*ydir[k] for k in range(3))
    span = a1 - a0
    nseg = max(1, int(math.ceil(span/(math.pi/2) - DEGENERATE_TOL)))
    seg = span/nseg
    hw = math.cos(seg/2.0)
    poles, weights = [], []
    for i in range(2*nseg + 1):
        s = i // 2
        if i % 2 == 0:
            poles.append(on(a0 + s*seg))
            weights.append(1.0)
        else:
            am = a0 + (s + 0.5)*seg
            p = tuple(center[k] + (rx/hw)*math.cos(am)*xdir[k] + (ry/hw)*math.sin(am)*ydir[k]
                      for k in range(3))
            poles.append(p)
            weights.append(hw)
    knots = [a0, a0, a0]
    for s in range(1, nseg):
        k = a0 + s*seg
        knots += [k, k]
    knots += [a1, a1, a1]
    return dict(order=3, poles=poles, weights=weights, knots=knots, plo=a0, phi=a1)

def lower_extrusion(rd, ref, vlo=0.0, vhi=1.0):
    a = rd.args(ref)
    basis = _lower_curve_to_nurb_local(rd, a[1])
    if basis is None: return None
    vec = rd.args(a[2])
    d = rd.direction(vec[1])
    mag = float(vec[2])
    dirv = tuple(d[k]*mag for k in range(3))
    nU = len(basis["poles"])
    cps, wts = [], []
    for iu in range(nU):
        p = basis["poles"][iu]
        w = basis["weights"][iu]
        cps.append(tuple(p[k] + vlo*dirv[k] for k in range(3)))
        cps.append(tuple(p[k] + vhi*dirv[k] for k in range(3)))
        wts += [w, w]
    return dict(nurb=True, uOrder=basis["order"], vOrder=2, uVertexCount=nU, vVertexCount=2,
                uKnots=basis["knots"], vKnots=[vlo, vlo, vhi, vhi],
                controlVertices=cps, weights=wts)

def lower_revolution(rd, ref, a0=0.0, a1=2*math.pi):
    a = rd.args(ref)
    basis = _lower_curve_to_nurb_local(rd, a[1])
    if basis is None: return None
    ap = rd.args(a[2])
    axp = rd.point(ap[1])
    axd = vnorm(rd.direction(ap[2])) if len(ap) > 2 and isinstance(ap[2], tuple) else (0.,0.,1.)
    span = a1 - a0
    nseg = max(1, int(math.ceil(span/(math.pi/2) - DEGENERATE_TOL)))
    seg = span/nseg
    hw = math.cos(seg/2.0)
    nbV = len(basis["poles"])
    nbU = 2*nseg + 1
    foot, radial = [], []
    for p in basis["poles"]:
        t = vdot(vsub(p, axp), axd)
        f = tuple(axp[k] + t*axd[k] for k in range(3))
        foot.append(f)
        radial.append(vsub(p, f))
    ang, arcw, scale = [], [], []
    for iu in range(nbU):
        s = iu // 2
        if iu % 2 == 0:
            ang.append(a0 + s*seg)
            arcw.append(1.0)
            scale.append(1.0)
        else:
            ang.append(a0 + (s + 0.5)*seg)
            arcw.append(hw)
            scale.append(1.0/hw)
    def rot(rvec, angle):
        c, s = math.cos(angle), math.sin(angle)
        cx = vcross(axd, rvec)
        return tuple(c*rvec[k] + s*cx[k] for k in range(3))
    cps, wts = [], []
    for iu in range(nbU):
        for iv in range(nbV):
            rp = rot(radial[iv], ang[iu])
            pole = tuple(foot[iv][k] + rp[k]*scale[iu] for k in range(3))
            cps.append(pole)
            wts.append(arcw[iu]*basis["weights"][iv])
    uK = [a0, a0, a0]
    for s in range(1, nseg):
        k = a0 + s*seg
        uK += [k, k]
    uK += [a1, a1, a1]
    return dict(nurb=True, uOrder=3, vOrder=basis["order"], uVertexCount=nbU, vVertexCount=nbV,
                uKnots=uK, vKnots=basis["knots"], controlVertices=cps, weights=wts)

# ================================================================ geometry extraction
def surface_geom(rd, ref, cfg, fpts=None):
    """(token, dict-of-arrays) for a face surface. cfg selects cone unit-context
    and swept-surface lowering. fpts (the face's boundary vertices and edge
    samples) bound a linear extrusion along its vector (PCURVEs are not read)."""
    t = rd.typ(ref)
    a = rd.args(ref)
    if t == "PLANE":
        o, z, x = rd.placement(a[1])
        return ("BrepSurfacePlaneAPI", dict(origin=o, axis=z, refDirection=x))
    if t == "CYLINDRICAL_SURFACE":
        o, z, x = rd.placement(a[1])
        return ("BrepSurfaceCylinderAPI", dict(origin=o, axis=z, refDirection=x, radius=float(a[2])))
    if t == "CONICAL_SURFACE":
        o, z, x = rd.placement(a[1])
        sa = float(a[3]) * cfg.angle_scale
        if sa < 0.0:
            z = tuple(-c for c in z)
            sa = -sa
        return ("BrepSurfaceConeAPI", dict(origin=o, axis=z, refDirection=x, radius=float(a[2]), semiAngle=sa))
    if t == "SPHERICAL_SURFACE":
        o, z, x = rd.placement(a[1])
        return ("BrepSurfaceSphereAPI", dict(center=o, axis=z, refDirection=x, radius=float(a[2])))
    if t == "TOROIDAL_SURFACE":
        o, z, x = rd.placement(a[1])
        return ("BrepSurfaceTorusAPI", dict(origin=o, axis=z, refDirection=x, majorRadius=float(a[2]), minorRadius=float(a[3])))
    if t == "SURFACE_OF_LINEAR_EXTRUSION":
        # face:range is the lowered surface's whole knot domain, so the surface
        # has to reach every boundary point: P = curve(u) + v*vector bounds v by
        # the points' and the profile hull's extents along the vector.
        vlo, vhi = 0.0, 1.0
        vec = rd.args(a[2])
        d = vnorm(rd.direction(vec[1]))
        mag = float(vec[2])
        if fpts:
            hs = [vdot(p, d) for p in fpts]
            base = _lower_curve_to_nurb_local(rd, a[1])
            if base and base["poles"]:
                hb = [vdot(p, d) for p in base["poles"]]
                vlo, vhi = _pad((min(hs) - max(hb))/mag, (max(hs) - min(hb))/mag, None)
                if not (vhi > vlo + DEGENERATE_TOL):
                    vlo, vhi = 0.0, 1.0
        g = lower_extrusion(rd, ref, vlo, vhi)
        if g is not None:
            return ("BrepSurfaceNurbAPI", g)
        return ("BrepSurfaceNurbAPI", lower_extrusion(rd, ref) or bspline_surface(rd, ref))
    if t == "SURFACE_OF_REVOLUTION":
        g = lower_revolution(rd, ref)
        if g is not None:
            return ("BrepSurfaceNurbAPI", g)
    if t in ("B_SPLINE_SURFACE_WITH_KNOTS", "__COMPLEX__"):
        return ("BrepSurfaceNurbAPI", bspline_surface(rd, ref))
    return ("BrepSurfaceNurbAPI", bspline_surface(rd, ref))

def bspline_surface(rd, ref):
    weights = None
    if rd.typ(ref) == "__COMPLEX__":
        subs = dict(rd.args(ref))
        base, wk = subs["B_SPLINE_SURFACE"], subs["B_SPLINE_SURFACE_WITH_KNOTS"]
        ud, vd, grid = int(base[0]), int(base[1]), base[2]
        u_mults = [int(x) for x in wk[0]]
        v_mults = [int(x) for x in wk[1]]
        u_knots = [float(x) for x in wk[2]]
        v_knots = [float(x) for x in wk[3]]
        rat = subs.get("RATIONAL_B_SPLINE_SURFACE")
        if rat: weights = [float(w) for row in rat[0] for w in row]
    else:
        a = rd.args(ref)
        ud, vd, grid = int(a[1]), int(a[2]), a[3]
        u_mults = [int(x) for x in a[8]]
        v_mults = [int(x) for x in a[9]]
        u_knots = [float(x) for x in a[10]]
        v_knots = [float(x) for x in a[11]]
    cps = [rd.point(p) for row in grid for p in row]
    nU, nV = len(grid), len(grid[0])
    uK, vK = expand_knots(u_knots, u_mults), expand_knots(v_knots, v_mults)
    if weights is None: weights = [1.0]*(nU*nV)
    return dict(nurb=True, uOrder=ud+1, vOrder=vd+1, uVertexCount=nU, vVertexCount=nV,
                uKnots=uK, vKnots=vK, controlVertices=cps, weights=weights)

def curve_geom(rd, ref):
    t = rd.typ(ref)
    a = rd.args(ref)
    # OCCT-based writers (cadquery, FreeCAD, ...) wrap an edge's 3-D curve in a
    # SURFACE_CURVE or SEAM_CURVE that also carries its parameter-space curves.
    # The first attribute is the 3-D curve; the pcurves are not needed here.
    if t in ("SURFACE_CURVE", "SEAM_CURVE", "INTERSECTION_CURVE",
             "BOUNDED_SURFACE_CURVE"):
        return curve_geom(rd, a[1])
    if t == "LINE":
        o = rd.point(a[1])
        vec = rd.args(a[2])
        d = vnorm(rd.direction(vec[1]))
        return ("BrepCurve3dLineAPI", dict(origin=o, direction=d))
    if t == "CIRCLE":
        o, z, x = rd.placement(a[1])
        return ("BrepCurve3dCircleAPI", dict(center=o, axis=z, refDirection=x, radius=float(a[2])))
    if t == "ELLIPSE":
        o, z, x = rd.placement(a[1])
        return ("BrepCurve3dEllipseAPI", dict(center=o, axis=z, refDirection=x, xRadius=float(a[2]), yRadius=float(a[3])))
    if t in ("B_SPLINE_CURVE_WITH_KNOTS", "__COMPLEX__"):
        return ("BrepCurve3dNurbAPI", bspline_curve(rd, ref))
    return ("BrepCurve3dNurbAPI", bspline_curve(rd, ref))

def bspline_curve(rd, ref):
    weights = None
    if rd.typ(ref) == "__COMPLEX__":
        subs = dict(rd.args(ref))
        base, wk = subs["B_SPLINE_CURVE"], subs["B_SPLINE_CURVE_WITH_KNOTS"]
        deg = int(base[0])
        cps = [rd.point(p) for p in base[1]]
        mults = [int(x) for x in wk[0]]
        knots = [float(x) for x in wk[1]]
        rat = subs.get("RATIONAL_B_SPLINE_CURVE")
        if rat: weights = [float(w) for w in rat[0]]
    else:
        a = rd.args(ref)
        deg = int(a[1])
        cps = [rd.point(p) for p in a[2]]
        mults = [int(x) for x in a[6]]
        knots = [float(x) for x in a[7]]
    if weights is None: weights = [1.0]*len(cps)
    K = expand_knots(knots, mults)
    return dict(nurb=True, order=deg+1, vertexCount=len(cps), knots=K, controlVertices=cps, weights=weights)

# ================================================================ edge:range
def _bump(a, b):
    return (a, b) if b > a + COINCIDENT_TOL else (a, a + _TWO_PI)

def _primary_period(a0, a1):
    """Shift an angular (start, end) pair by whole periods so the end lands in
    the primary period (0, 2*pi], which is what BA.630 requires of a circle or
    ellipse edge:range max.

    math.atan2 returns (-pi, pi], so a half of every circle STEP describes comes
    out with a negative parameter. Shifting by a whole period is exact for a
    circle and leaves the span unchanged. The start may stay negative: that is
    how an arc crossing the seam is expressed, and the requirement bounds only
    the max."""
    two = 2 * math.pi
    k = math.ceil(a1 / two) - 1
    return (a0 - k * two, a1 - k * two)

def _ellipse_edge_range(cg, ps, pe):
    """Eccentric-angle range for an ELLIPSE edge (OCCT Geom_Ellipse param, what the
    validator inverts). Returns (t0,t1) with t1>t0."""
    c = cg["center"]
    xh = cg["refDirection"]
    ax = cg["axis"]
    yh = vcross(ax, xh)
    rx = cg["xRadius"]
    ry = cg["yRadius"]
    def ecc(p):
        d = vsub(p, c)
        return math.atan2(vdot(d, yh) / ry, vdot(d, xh) / rx)
    a0, a1 = ecc(ps), ecc(pe)
    if a1 <= a0 + COINCIDENT_TOL:
        a1 += 2 * math.pi
    return _primary_period(a0, a1)

def edge_range(ctok, cg, ps, pe):
    if ctok == "BrepCurve3dLineAPI":
        o, d = cg["origin"], cg["direction"]
        ts, te = vdot(vsub(ps, o), d), vdot(vsub(pe, o), d)
        return _bump(min(ts, te), max(ts, te))
    if ctok == "BrepCurve3dEllipseAPI":
        return _ellipse_edge_range(cg, ps, pe)
    if ctok == "BrepCurve3dCircleAPI":
        c, ax, u = cg["center"], cg["axis"], cg["refDirection"]
        v = vcross(ax, u)
        ang = lambda p: math.atan2(vdot(vsub(p, c), v), vdot(vsub(p, c), u))
        a0, a1 = ang(ps), ang(pe)
        if a1 <= a0: a1 += 2*math.pi
        return _primary_period(*_bump(a0, a1))
    if cg.get("nurb"):
        return (cg["knots"][0], cg["knots"][-1])
    return (0.0, 1.0)

# ================================================================ NURBS edge hull-trim
def _deboor_rational(order, knots, cps, weights, t):
    p = order - 1
    n = len(cps)
    lo, hi = knots[p], knots[n]
    if t < lo: t = lo
    if t > hi: t = hi
    if t >= knots[n]: k = n - 1
    else:
        k = p
        while k < n - 1 and knots[k + 1] <= t: k += 1
    d = []
    for j in range(p + 1):
        i = k - p + j
        w = weights[i]
        cp = cps[i]
        d.append([cp[0]*w, cp[1]*w, cp[2]*w, w])
    dprev = None
    for r in range(1, p + 1):
        if r == p: dprev = [row[:] for row in d]
        for j in range(p, r - 1, -1):
            i = k - p + j
            denom = knots[i + p - r + 1] - knots[i]
            alpha = 0.0 if denom == 0.0 else (t - knots[i]) / denom
            a = 1.0 - alpha
            d[j] = [a * d[j - 1][m] + alpha * d[j][m] for m in range(4)]
    Cw = d[p]
    w0 = Cw[3] or 1.0
    C = (Cw[0]/w0, Cw[1]/w0, Cw[2]/w0)
    span = knots[k + 1] - knots[k - p + 1] if p > 0 else 0.0
    if p > 0 and span != 0.0 and dprev is not None:
        dCw = [p * (dprev[p][m] - dprev[p - 1][m]) / span for m in range(4)]
    else:
        dCw = [0.0, 0.0, 0.0, 0.0]
    wp = dCw[3]
    Cd = ((dCw[0] - C[0]*wp)/w0, (dCw[1] - C[1]*wp)/w0, (dCw[2] - C[2]*wp)/w0)
    return C, Cd

def _invert_nurbs(cg, target, seed=None):
    order = cg["order"]
    knots = cg["knots"]
    cps = cg["controlVertices"]
    wts = cg["weights"]
    p = order - 1
    lo, hi = knots[p], knots[len(cps)]
    if not (hi > lo): return (lo, float("inf"))
    def C(t): return _deboor_rational(order, knots, cps, wts, t)
    def d2(t):
        pt, _ = C(t)
        return sum((pt[m] - target[m]) ** 2 for m in range(3))
    N = 128
    samples = []
    for i in range(N + 1):
        t = lo + (hi - lo) * i / N
        samples.append((d2(t), t))
    samples.sort()
    seeds = [s[1] for s in samples[:5]]
    if seed is not None: seeds.insert(0, seed)
    best_t, best_r = lo, float("inf")
    for st in seeds:
        t = st
        for _ in range(30):
            pt, der = C(t)
            r = [pt[m] - target[m] for m in range(3)]
            f = sum(r[m] * der[m] for m in range(3))
            h = (hi - lo) * SNAP_TOL or DEGENERATE_TOL
            _, der2 = C(min(hi, t + h))
            cpp = [(der2[m] - der[m]) / h for m in range(3)]
            fp = sum(der[m]*der[m] for m in range(3)) + sum(r[m]*cpp[m] for m in range(3))
            if abs(fp) < 1e-18: break
            dt = f / fp
            t -= dt
            if t < lo: t = lo
            if t > hi: t = hi
            if abs(dt) < 1e-13: break
        pt, _ = C(t)
        resid = math.sqrt(sum((pt[m] - target[m]) ** 2 for m in range(3)))
        if resid < best_r:
            best_r = resid
            best_t = t
    step = (hi - lo) / N
    ct = samples[0][1]
    a, c = max(lo, ct - step), min(hi, ct + step)
    gr = 0.6180339887498949
    x1 = c - gr*(c - a)
    x2 = a + gr*(c - a)
    f1, f2 = d2(x1), d2(x2)
    for _ in range(60):
        if f1 < f2:
            c, x2, f2 = x2, x1, f1
            x1 = c - gr*(c - a)
            f1 = d2(x1)
        else:
            a, x1, f1 = x1, x2, f2
            x2 = a + gr*(c - a)
            f2 = d2(x2)
        if c - a < 1e-14: break
    tg = 0.5*(a + c)
    rg = math.sqrt(d2(tg))
    if rg < best_r:
        best_r = rg
        best_t = tg
    return (best_t, best_r)

def _insert_knot(order, knots, hcps, u, r):
    p = order - 1
    n = len(hcps) - 1
    m = n + p + 1
    if u >= knots[n + 1]: k = n
    else:
        k = p
        while k < n and knots[k + 1] <= u: k += 1
    s = sum(1 for kv in knots if kv == u)
    r = min(r, p - s)
    if r <= 0: return list(knots), [row[:] for row in hcps]
    nk = list(knots[:k + 1]) + [u]*r + list(knots[k + 1:])
    nq = [None]*(n + r + 1)
    for i in range(0, k - p + 1): nq[i] = hcps[i][:]
    for i in range(k - s, n + 1): nq[i + r] = hcps[i][:]
    R = [hcps[k - p + i][:] for i in range(0, p - s + 1)]
    L = 0
    for j in range(1, r + 1):
        L = k - p + j
        for i in range(0, p - j - s + 1):
            denom = knots[L + i + p - j + 1] - knots[L + i]
            alpha = 0.0 if denom == 0.0 else (u - knots[L + i]) / denom
            R[i] = [alpha*R[i + 1][c] + (1.0 - alpha)*R[i][c] for c in range(4)]
        nq[L] = R[0][:]
        nq[k + r - j - s] = R[p - j - s][:]
    for i in range(L + 1, k - s): nq[i] = R[i - L][:]
    return nk, nq

def _trim_nurbs_curve(cg, t0, t1):
    order = cg["order"]
    p = order - 1
    knots = list(cg["knots"])
    hcps = [[cg["controlVertices"][i][0]*cg["weights"][i],
             cg["controlVertices"][i][1]*cg["weights"][i],
             cg["controlVertices"][i][2]*cg["weights"][i], cg["weights"][i]]
            for i in range(len(cg["controlVertices"]))]
    lo, hi = min(t0, t1), max(t0, t1)
    if not (hi > lo): return None
    for u in (lo, hi):
        s = sum(1 for kv in knots if kv == u)
        need = p - s
        if need > 0: knots, hcps = _insert_knot(order, knots, hcps, u, need)
    i0 = sum(1 for kv in knots if kv < lo) - 1
    i1 = sum(1 for kv in knots if kv <= hi) - order
    if i0 < 0: i0 = 0
    if i1 >= len(hcps): i1 = len(hcps) - 1
    if i1 < i0: return None
    sub_h = hcps[i0:i1 + 1]
    nsub = len(sub_h)
    interior = [kv for kv in knots if lo < kv < hi]
    subk = [lo]*order + interior + [hi]*order
    if len(subk) != nsub + order: return None
    cps = []
    wts = []
    for h in sub_h:
        w = h[3] or 1.0
        cps.append((h[0]/w, h[1]/w, h[2]/w))
        wts.append(w)
    return dict(nurb=True, order=order, vertexCount=nsub, knots=subk, controlVertices=cps, weights=wts)

def _reverse_nurbs(cg):
    kn = cg["knots"]
    lo, hi = kn[0], kn[-1]
    rk = [lo + hi - k for k in reversed(kn)]
    return dict(nurb=True, order=cg["order"], vertexCount=cg["vertexCount"],
                knots=rk, controlVertices=list(reversed(cg["controlVertices"])),
                weights=list(reversed(cg["weights"])))

def process_nurbs_edge(cg, ps, pe, cfg):
    """Invert authored endpoint vertices and trim the control hull to that sub-span
    so the emitted curve's hull ends coincide with the vertices. Returns (geom,range)."""
    order = cg["order"]
    knots = cg["knots"]
    cps = cg["controlVertices"]
    p = order - 1
    dlo, dhi = knots[p], knots[len(cps)]
    tol = cfg.nurb_snap_tol
    if (math.dist(cps[0], ps) <= tol and math.dist(cps[-1], pe) <= tol):
        return cg, (dlo, dhi)
    if math.dist(ps, pe) < tol:
        return cg, (dlo, dhi)
    t0, r0 = _invert_nurbs(cg, ps)
    t1, r1 = _invert_nurbs(cg, pe)
    INV_ACCEPT = cfg.inv_accept
    if r0 <= INV_ACCEPT and r1 <= INV_ACCEPT and abs(t1 - t0) > DEGENERATE_TOL:
        trimmed = _trim_nurbs_curve(cg, t0, t1)
        if trimmed is not None:
            h0, hN = trimmed["controlVertices"][0], trimmed["controlVertices"][-1]
            if math.dist(h0, ps) > math.dist(h0, pe):
                trimmed = _reverse_nurbs(trimmed)
            cv = trimmed["controlVertices"]
            if math.dist(cv[0], ps) <= INV_ACCEPT and math.dist(cv[-1], pe) <= INV_ACCEPT:
                spread = max(math.dist(cv[c], cv[0]) for c in range(1, len(cv))) if len(cv) > 1 else 0.0
                if spread <= 2.0 * cfg.edge_degen_tol:
                    return cg, (dlo, dhi)
                cv[0] = tuple(ps)
                cv[-1] = tuple(pe)
                tk = trimmed["knots"]
                return trimmed, (tk[order - 1], tk[trimmed["vertexCount"]])
    return cg, (dlo, dhi)

# ================================================================ face:range (H2)
def _uframe(sg, key_o="origin"):
    o = sg.get(key_o) or sg.get("center") or sg.get("origin")
    z = vnorm(sg["axis"])
    x = vnorm(sg["refDirection"])
    y = vcross(z, x)
    return o, x, y, z

def _wrap_span(angles):
    a = sorted(x % _TWO_PI for x in angles)
    if len(a) == 1: return a[0], a[0]
    best_gap = -1.0
    gi = 0
    for i in range(len(a)):
        nxt = a[(i + 1) % len(a)] + (_TWO_PI if i == len(a) - 1 else 0.0)
        gap = nxt - a[i]
        if gap > best_gap:
            best_gap = gap
            gi = i
    lo = a[(gi + 1) % len(a)]
    hi = a[gi] + (_TWO_PI if gi != len(a) - 1 else 0.0)
    if hi < lo: hi += _TWO_PI
    return lo, hi

def _pad(lo, hi, natural, frac=PAD_FRAC, absmin=PAD_MIN):
    span = hi - lo
    p = max(absmin, frac * span)
    lo -= p
    hi += p
    if natural is not None:
        nlo, nhi = natural
        if lo < nlo: lo = nlo
        if hi > nhi: hi = nhi
    return lo, hi

def _pad_angular(lo, hi):
    """Widen a periodic-direction window, without pushing it out of the primary
    period.

    The padding exists so a face is not trimmed exactly through its boundary
    vertices. A window that ends at the seam has nothing to gain from it there
    and everything to lose: 2% of the span past 2*pi turns a window that sits
    inside the period into one that appears to straddle the seam. On a
    production assembly export that accounted for 130 of the 139 out-of-period
    windows BA.631 and BA.765 reported. Clamp when the unpadded window is inside
    the period; a window that genuinely straddles the seam is left alone."""
    natural = ((0.0, _TWO_PI)
               if (lo >= -DEGENERATE_TOL and hi <= _TWO_PI + DEGENERATE_TOL)
               else None)
    lo, hi = _pad(lo, hi, natural)
    span = hi - lo
    if span >= _TWO_PI - PAD_MIN:
        return 0.0, _TWO_PI
    return lo, hi

_SEAM_EPS = PERIOD_TOL
def _full_period(lo, hi):
    """True when a periodic-direction boundary-vertex span means 'full period'.

    Two cases: the span covers ~2*pi, OR it collapses to a single angle. A
    boundary whose vertices ALL sit at one angle is a seam-only boundary (the
    face is bounded by closed circles whose single start=end vertex lies on
    the seam), i.e. the face wraps the whole period -- e.g. bearing pins,
    bores, hose corrugation rings. Authoring the collapsed sliver instead
    leaves the renderer's parametric fallback rebuilding an invisible wedge, so
    the pin or bore vanishes. A genuine thin wedge always has two distinct
    vertex angles -- the narrowest fillets production CAD produces span around
    0.009 rad, orders above _SEAM_EPS -- so the collapse test cannot mistake
    one for a seam."""
    span = hi - lo
    return span >= _TWO_PI - PERIOD_TOL or span < _SEAM_EPS

def _base_face_range(stok, sg, fverts):
    """The flat (natural-period) face:range from step_to_usdsolid_fixed.

    A plane's window is padded like a cylinder's height: the samples along a
    boundary arc stop short of its extreme by up to its sagitta between
    samples (0.055 mm on a 125 mm circle), and a window that stops there cuts
    the face. SMLib then integrates such a face short by far more than the
    sliver cut off: 18.5 mm2 on the gripper flange's top, whose sliver is
    0.55 mm2."""
    if stok == "BrepSurfacePlaneAPI" and fverts:
        o, u = sg["origin"], sg["refDirection"]
        v = vcross(sg["axis"], sg["refDirection"])
        us = [vdot(vsub(p, o), u) for p in fverts]
        vs = [vdot(vsub(p, o), v) for p in fverts]
        return (_pad(*_bump(min(us), max(us)), None), _pad(*_bump(min(vs), max(vs)), None))
    if sg.get("nurb"):
        return ((sg["uKnots"][0], sg["uKnots"][-1]), (sg["vKnots"][0], sg["vKnots"][-1]))
    if stok in ("BrepSurfaceCylinderAPI", "BrepSurfaceConeAPI") and fverts:
        o, ax = sg["origin"], sg["axis"]
        hs = [vdot(vsub(p, o), ax) for p in fverts]
        return ((0.0, 2*math.pi), _bump(min(hs), max(hs)))
    if stok == "BrepSurfaceSphereAPI":
        return ((0.0, 2*math.pi), (-math.pi/2, math.pi/2))
    if stok == "BrepSurfaceTorusAPI":
        return ((0.0, 2*math.pi), (0.0, 2*math.pi))
    return ((0.0, 1.0), (0.0, 1.0))

def _loop_windings(loop_pts, ang):
    """Net winding (radians) of each ordered boundary loop under the periodic
    angle function ang(point). Consecutive deltas are unwrapped to (-pi, pi],
    which is safe at the edge-sampling densities used here. A loop that winds
    the full period (|W| ~ 2*pi) visits EVERY angle, so the face's window must
    cover the whole period regardless of where the sampled footprint's largest
    gap happens to fall (closed NURBS rim curves under-sample badly enough that
    _wrap_span alone can leave a spurious 0.3 rad notch)."""
    out = []
    for pts in loop_pts or []:
        if len(pts) < 3:
            out.append(0.0)
            continue
        W = 0.0
        prev = None
        for p in pts:
            a = ang(p)
            if prev is not None:
                dl = a - prev
                while dl > math.pi: dl -= _TWO_PI
                while dl < -math.pi: dl += _TWO_PI
                W += dl
            prev = a
        out.append(W)
    return out

def _wraps_period(loop_pts, ang):
    return any(abs(W) > math.pi for W in _loop_windings(loop_pts, ang))

def _sphere_pole_claim(windings, sense):
    """Which pole (if any) a sphere face CONTAINS, from the net longitude winding
    of its ordered boundary loops. A boundary loop whose traversal winds the full
    u period (|W| ~ 2*pi) encircles a pole; the boundary footprint alone can never
    reach that pole (it is INTERIOR), so the v window must be extended to it.
    STEP loops are CCW about the face normal (material on the left); walking +u
    with the surface normal outward puts higher latitude on the left, so W>0
    claims the +axis pole when the face normal equals the surface normal
    (same_sense=T), the -axis pole otherwise. A band's two rim loops wind
    oppositely and cancel. Returns +1 (+axis pole), -1 (-axis pole), 0 (none or
    conflicting). The case this has to get right is a sphere divided by a wavy
    seam into two faces: they claim opposite poles and tile the sphere between
    them."""
    claims = set()
    for W in windings:
        if W > math.pi:
            claims.add(1 if sense else -1)
        elif W < -math.pi:
            claims.add(-1 if sense else 1)
    return claims.pop() if len(claims) == 1 else 0

def rebase_periodic_u(stok, sg, rng):
    """Re-parameterize a periodic surface so its face's U window starts at zero.

    A face whose angular window happens to begin just before the seam comes out
    of _wrap_span as, say, [6.220, 9.488] -- a perfectly ordinary partial face
    that straddles u = 2*pi. BA.765 requires a partial-period domain to stay
    inside the primary period, and the alternative reading, splitting the face
    at the seam, means minting a seam edge and its vertices, splitting every
    boundary edge that crosses u = 0, and rebuilding the radial chains through
    the new edgeuses.

    None of that is needed. The reference direction of a cylinder, cone, sphere
    or torus is arbitrary: rotating it about the axis by the window's start
    angle describes the same surface with the window at [0, span]. Each face
    carries its own surface record, so this affects nothing else. Only a face
    whose window genuinely exceeds a full period would need splitting, and such
    a face is already the full-period case.

    Returns the (possibly rotated) surface dict and the rebased range."""
    if stok not in ("BrepSurfaceCylinderAPI", "BrepSurfaceConeAPI",
                    "BrepSurfaceSphereAPI", "BrepSurfaceTorusAPI"):
        return sg, rng
    (ulo, uhi), v = rng
    if -PERIOD_TOL <= ulo and uhi <= _TWO_PI + PERIOD_TOL:
        return sg, rng
    span = uhi - ulo
    if span >= _TWO_PI - PERIOD_TOL:
        return sg, rng                      # full period: nothing to rebase
    z = vnorm(sg["axis"])
    x = vnorm(sg["refDirection"])
    x = vnorm(vsub(x, tuple(vdot(x, z) * z[k] for k in range(3))))
    y = vcross(z, x)
    c, sn = math.cos(ulo), math.sin(ulo)
    sg = dict(sg)
    sg["refDirection"] = vnorm(tuple(c * x[k] + sn * y[k] for k in range(3)))
    return sg, ((0.0, span), v)

def face_range(stok, sg, fverts, loop_pts=None, sense=True):
    """The face's UV window (face:range). An analytic face's window is taken from
    the boundary's actual UV footprint; a NURBS face (a B-spline surface or a
    lowered swept surface) takes its surface's whole knot domain. loop_pts:
    ordered per-loop boundary sample chains, used for periodic-direction winding
    promotion and sphere pole containment; sense is the ADVANCED_FACE same_sense
    flag. With no boundary samples there is nothing to project, so fall back to
    the natural period."""
    if not fverts:
        return _base_face_range(stok, sg, fverts)
    if stok == "BrepSurfacePlaneAPI":
        return _base_face_range(stok, sg, fverts)
    if stok in ("BrepSurfaceCylinderAPI", "BrepSurfaceConeAPI"):
        o, x, y, z = _uframe(sg)
        us = []
        vs = []
        uang = lambda p: math.atan2(vdot(vsub(p, o), y), vdot(vsub(p, o), x))
        for p in fverts:
            us.append(uang(p))
            vs.append(vdot(vsub(p, o), z))
        ulo, uhi = _wrap_span(us)
        if _wraps_period(loop_pts, uang) or _full_period(ulo, uhi): ulo, uhi = 0.0, _TWO_PI
        else: ulo, uhi = _pad_angular(ulo, uhi)
        vlo, vhi = _pad(min(vs), max(vs), None)
        # A cone's radius shrinks to nothing at its apex and would go negative
        # past it, so the apex is the natural floor for v the way 2*pi is for a
        # periodic u. _pad widens a window by a fraction of its span, which is
        # enough on a face that reaches the tip to push v through it: SMLib's
        # SmCone::CreateCanonical rejects the surface outright, and the body
        # does not import at all. v is authored AXIAL, so the apex sits at
        # -radius/tan(semiAngle).
        if stok == "BrepSurfaceConeAPI":
            t = math.tan(sg["semiAngle"])
            if abs(t) > DEGENERATE_TOL:
                apex = -sg["radius"] / t
                if t > 0.0:
                    vlo = max(vlo, apex)
                else:
                    vhi = min(vhi, apex)
                if vhi <= vlo:
                    vlo, vhi = _bump(min(vs), max(vs))
        return ((ulo, uhi), (vlo, vhi))
    if stok == "BrepSurfaceSphereAPI":
        c = sg["center"]
        z = vnorm(sg["axis"])
        x = vnorm(sg["refDirection"])
        y = vcross(z, x)
        us = []
        vs = []
        for p in fverts:
            d = vsub(p, c)
            lat = math.asin(max(-1.0, min(1.0, vdot(vnorm(d), z))))
            lon = math.atan2(vdot(d, y), vdot(d, x))
            us.append(lon)
            vs.append(lat)
        lonf = lambda p: math.atan2(vdot(vsub(p, c), y), vdot(vsub(p, c), x))
        Ws = _loop_windings(loop_pts, lonf)
        pole = _sphere_pole_claim(Ws, sense)
        wraps = any(abs(W) > math.pi for W in Ws)
        ulo, uhi = _wrap_span(us)
        if pole or wraps or _full_period(ulo, uhi): ulo, uhi = 0.0, _TWO_PI
        else: ulo, uhi = _pad_angular(ulo, uhi)
        vlo, vhi = _pad(min(vs), max(vs), (-math.pi/2, math.pi/2))
        if pole > 0: vhi = _HALF_PI
        elif pole < 0: vlo = -_HALF_PI
        return ((ulo, uhi), (vlo, vhi))
    if stok == "BrepSurfaceTorusAPI":
        o = sg["origin"]
        z = vnorm(sg["axis"])
        x = vnorm(sg["refDirection"])
        y = vcross(z, x)
        Rmaj = sg["majorRadius"]
        us = []
        vs = []
        uang = lambda p: math.atan2(vdot(vsub(p, o), y), vdot(vsub(p, o), x))
        def vang(p):
            u = uang(p)
            cu, su = math.cos(u), math.sin(u)
            rad_dir = (cu*x[0] + su*y[0], cu*x[1] + su*y[1], cu*x[2] + su*y[2])
            tube_c = (o[0] + Rmaj*rad_dir[0], o[1] + Rmaj*rad_dir[1], o[2] + Rmaj*rad_dir[2])
            dt = vsub(p, tube_c)
            return math.atan2(vdot(dt, z), vdot(dt, rad_dir))
        for p in fverts:
            us.append(uang(p))
            vs.append(vang(p))
        ulo, uhi = _wrap_span(us)
        vlo, vhi = _wrap_span(vs)
        full_u = _wraps_period(loop_pts, uang) or _full_period(ulo, uhi)
        full_v = _wraps_period(loop_pts, vang) or _full_period(vlo, vhi)
        if full_u: ulo, uhi = 0.0, _TWO_PI
        else: ulo, uhi = _pad_angular(ulo, uhi)
        if full_v: vlo, vhi = 0.0, _TWO_PI
        else: vlo, vhi = _pad_angular(vlo, vhi)
        return ((ulo, uhi), (vlo, vhi))
    return _base_face_range(stok, sg, fverts)

# ================================================================ boundary-edge sampling (H2b)
def _edge_interior_samples(edge, verts, n_nurb=9):
    """Evenly spaced interior 3D points ON a boundary edge's curve over the edge's
    authored parameter range. face:range used to see only the boundary VERTICES;
    whenever those under-sample the boundary (a closed wavy NURBS edge with a
    single seam vertex, arcs whose vertices straddle the u-seam, full rim circles)
    the UV footprint collapsed to a sliver or picked the seam COMPLEMENT notch.
    Sampling along the edges populates the true UV window. Analytic arcs get
    span-scaled counts (a full rim circle yields enough distinct angles that
    _pad_angular's own >=2*pi-1e-4 full-period promotion fires); NURBS curves are
    capped at n_nurb de Boor evaluations. Curve types with no cheap evaluator
    contribute nothing (their endpoints are already in the vertex set)."""
    ctok = edge["ctok"]
    cg = edge["geom"]
    t0, t1 = edge["rng"]
    if not (math.isfinite(t0) and math.isfinite(t1) and t1 > t0):
        return []
    if ctok == "BrepCurve3dLineAPI":
        # interior lerp between the authored endpoint vertices (equivalent to the
        # arc-length parametrization and immune to _bump's degenerate-closed-line
        # 2*pi range fallback)
        s, e = edge["v"]
        ps, pe = verts[s], verts[e]
        return [tuple(ps[k] + (pe[k]-ps[k]) * i / 10.0 for k in range(3)) for i in range(1, 10)]
    if ctok in ("BrepCurve3dCircleAPI", "BrepCurve3dEllipseAPI"):
        c = cg["center"]
        z = vnorm(cg["axis"])
        x = vnorm(cg["refDirection"])
        y = vcross(z, x)
        span = t1 - t0
        # 0.06 rad step: the inscribed-polygon sagitta (r*step^2/8, the amount a
        # plane/height window can under-cover an arc bulge between samples) stays
        # below ~5e-4*r; also keeps _wrap_span gaps well under genuine notches.
        n = max(9, min(128, int(span / 0.06) + 1))
        ts = [t0 + span * i / (n + 1) for i in range(1, n + 1)]
        if ctok == "BrepCurve3dCircleAPI":
            r = cg["radius"]
            return [tuple(c[k] + r*(math.cos(t)*x[k] + math.sin(t)*y[k]) for k in range(3)) for t in ts]
        rx, ry = cg["xRadius"], cg["yRadius"]
        # rng is the eccentric angle: P(t) = c + rx*cos(t)*x + ry*sin(t)*y
        return [tuple(c[k] + rx*math.cos(t)*x[k] + ry*math.sin(t)*y[k] for k in range(3)) for t in ts]
    if cg.get("nurb"):
        kn = cg["knots"]
        cps = cg["controlVertices"]
        lo, hi = kn[cg["order"] - 1], kn[len(cps)]
        a, b = max(t0, lo), min(t1, hi)
        if not (b > a):
            return []
        # Scale with hull complexity. A closed 34-pole wavy boundary winding a
        # whole sphere leaves 1.6 rad longitude gaps at 9 uniform samples, which
        # under-covers the boundary itself. Bounded at 48.
        n = max(n_nurb, min(48, len(cps)))
        pts = []
        for i in range(1, n + 1):
            t = a + (b - a) * i / (n + 1)
            C, _ = _deboor_rational(cg["order"], kn, cps, cg["weights"], t)
            pts.append(C)
        return pts
    return []

# ================================================================ shell resolution
#: Every shell a body's boundary may name.
_SHELL_TYPES = ("CLOSED_SHELL", "OPEN_SHELL", "ORIENTED_CLOSED_SHELL", "ORIENTED_OPEN_SHELL")

def _resolve_shell_faces(rd, sh_ref):
    """Return the ADVANCED_FACE refs for a shell, unwrapping an
    ORIENTED_CLOSED_SHELL (a void boundary) or an ORIENTED_OPEN_SHELL to its
    base shell. The orientation flag is not applied here: a sheet's shell
    carries both faceuses of every face (pack_regions), so in UsdSolid a
    reversed open shell is the same sheet."""
    if rd.typ(sh_ref) in ("ORIENTED_CLOSED_SHELL", "ORIENTED_OPEN_SHELL"):
        base = rd.args(sh_ref)[2]
        return rd.args(base)[1]
    return rd.args(sh_ref)[1]

def _reverse_loops_of_reversed_faces(faces, loops, edgeuses):
    """Reverse, in place, every loop of each face whose ADVANCED_FACE
    same_sense is .F.: its edgeuses run in the opposite order, each flipped.

    UsdSolid winds a face's loops as seen from its `same` faceuse, the side its
    surface normal points to (edgeuse:orientationType). STEP winds them about
    the face normal, which on a reversed face points the other way, so the
    loops of a reversed face run backwards until they are turned here; the
    face's outward faceuse becomes `opposite` to match (pack_regions). Returns
    how many faces were reversed."""
    li = eo = count = 0
    for face in faces:
        rev = not face["sense"]
        for _ in range(face["loopCount"]):
            n = loops[li]
            if rev and n:
                edgeuses[eo:eo + n] = [
                    dict(eu, orient="opposite" if eu["orient"] == "same" else "same")
                    for eu in reversed(edgeuses[eo:eo + n])]
            li += 1
            eo += n
        count += rev
    return count


def _outer_loop_first(loop_specs, floop_edges, edgeuses, edges, verts, esamples):
    """Put a face's outer loop first when its STEP bounds don't name it.

    Rule 424 makes a face's first loop its outer loop. STEP names that loop with
    FACE_OUTER_BOUND, but the entity is optional: the KUKA KR 640 authors each of
    its 2,729 multi-loop faces with FACE_BOUND only, in an order that lists a hole
    first on 1,050 of them, and SMLib fails to tessellate 475 such planar faces.
    The outer loop encloses the others, so it is the loop whose boundary samples
    span the largest box; on a tie the file's order stands. The face's edgeuses
    are the last ones authored, a block per loop, and move with their loop."""
    def extent(eis):
        pts = []
        for ei in eis:
            if ei not in esamples:
                esamples[ei] = _edge_interior_samples(edges[ei], verts)
            s, t = edges[ei]["v"]
            pts += [verts[s]] + esamples[ei] + [verts[t]]
        if not pts:
            return 0.0
        return math.dist([min(p[k] for p in pts) for k in range(3)],
                         [max(p[k] for p in pts) for k in range(3)])

    ext = [extent(eis) for eis, _ in floop_edges]
    k = max(range(len(ext)), key=ext.__getitem__)
    if k == 0 or ext[k] <= ext[0] * (1 + 1e-9):
        return loop_specs, floop_edges
    order = [k] + [j for j in range(len(ext)) if j != k]
    start = len(edgeuses) - sum(n for n, _ in loop_specs)
    blocks, pos = [], start
    for n, _ in loop_specs:
        blocks.append(edgeuses[pos:pos + n])
        pos += n
    edgeuses[start:] = [eu for j in order for eu in blocks[j]]
    return [loop_specs[j] for j in order], [floop_edges[j] for j in order]

# ================================================================ topology + geometry
def extract_brep(rd, cfg, solid_refs=None):
    """Read one or more STEP solids into the radial-edge topology + geometry the
    UsdSolid BrepArray schema stores. If solid_refs is None, discovers all
    top-level solids; pass a single ref to extract one body of an assembly. cfg
    carries the per-file plane-angle scale and tolerances."""
    if solid_refs is None:
        solid_refs = rd.find(*_BODY_TYPES)
    if not solid_refs:
        raise SystemExit("no MANIFOLD_SOLID_BREP / shell model found")
    vmap, emap = {}, {}
    verts, edges, edgeuses, loops, faces = [], [], [], [], []
    loop_vidx = []
    brep_faces = []
    sheet = []      # per body: every shell open (a SHELL_BASED_SURFACE_MODEL)
    esamples = {}   # edge index -> cached interior boundary samples (H2b)
    esense = {}     # edge index -> its EDGE_CURVE's same_sense

    def vidx(ref):
        vid = ref[1]
        if vid not in vmap:
            vmap[vid] = len(verts)
            verts.append(rd.point(rd.args(ref)[1]))
        return vmap[vid]

    def eidx(ec):
        eid = ec[1]
        if eid not in emap:
            a = rd.args(ec)
            s, e = vidx(a[1]), vidx(a[2])
            ctok, cg = curve_geom(rd, a[3])
            same_sense = (len(a) <= 4) or (a[4] == ("enum", "T"))
            if not same_sense: s, e = e, s
            if ctok == "BrepCurve3dLineAPI":
                # Re-fit the line through its two authored vertices so the curve
                # passes exactly through the edge's endpoints, with the parameter
                # range running start -> end. A STEP LINE's own origin/direction
                # can miss the welded vertices by the file's tolerance, and its
                # direction may even run end -> start. (The NURBS path below does
                # the equivalent by trimming its hull to the vertices.) A
                # degenerate zero-length edge keeps the STEP geometry.
                ps, pe = verts[s], verts[e]
                length = math.dist(ps, pe)
                if length > cfg.edge_degen_tol:
                    cg = dict(origin=tuple(ps), direction=vnorm(vsub(pe, ps)))
                    rng = (0.0, length)
                else:
                    rng = edge_range(ctok, cg, ps, pe)
            elif ctok == "BrepCurve3dNurbAPI" and cg.get("nurb"):
                # process_nurbs_edge self-gates: it only trims when the authored
                # hull ends don't already sit on the vertices (within nurb_snap_tol).
                cg, rng = process_nurbs_edge(cg, verts[s], verts[e], cfg)
            else:
                rng = edge_range(ctok, cg, verts[s], verts[e])
            emap[eid] = len(edges)
            esense[len(edges)] = same_sense
            edges.append(dict(v=(s, e), ctok=ctok, geom=cg, rng=rng))
        return emap[eid]

    def walk_loop(loop_ref):
        # Returns (edgeuse_count, edge_indices, vertex_loop_index). VERTEX_LOOP is a
        # degenerate single-vertex loop -> loop:vertexIndex.
        n, eis, vi = 0, [], 0
        lt = rd.typ(loop_ref)
        if lt == "VERTEX_LOOP":
            vi = vidx(rd.args(loop_ref)[1])
            return n, eis, vi
        for oe in rd.args(loop_ref)[1]:
            a = rd.args(oe)
            ei = eidx(a[3])
            # The flag is relative to the edge, which runs start to end; the
            # USD edge runs along its curve (eidx), which is the other way
            # round when the EDGE_CURVE's same_sense is .F.
            same = (a[4] == ("enum", "T")) == esense[ei]
            edgeuses.append(dict(edge=ei, orient="same" if same else "opposite"))
            n += 1
            eis.append(ei)
        return n, eis, vi

    for solid in solid_refs:
        sa = rd.args(("ref", solid))
        shell_refs = []
        for x in sa[1:]:
            if isinstance(x, tuple) and x[0] == "ref" and rd.typ(x) in _SHELL_TYPES:
                shell_refs.append(x)
            elif isinstance(x, list):
                shell_refs += [y for y in x if isinstance(y, tuple) and y[0] == "ref"]
        solid_faces = []            # one list of face indices per STEP shell
        for sh in shell_refs:
            solid_faces.append([])
            for fref in _resolve_shell_faces(rd, sh):
                fa = rd.args(fref)          # ADVANCED_FACE(name,(bounds),surface,same_sense)
                sense = not (len(fa) > 3 and fa[3] == ("enum", "F"))
                bounds = sorted(fa[1], key=lambda b: 0 if rd.typ(b)=="FACE_OUTER_BOUND" else 1)
                lc = 0
                fv = set()
                floop_edges = []
                loop_specs = []
                for b in bounds:
                    # Loop winding comes from the edge same_sense flags, the edgeuse
                    # orientations and the bound's own orientation flag: a .F. bound
                    # traverses its EDGE_LOOP backwards (the KUKA KR 640 authors
                    # 252), so its edgeuses run in reverse order, each flipped. A
                    # reversed face's loops are turned once all loops are built
                    # (_reverse_loops_of_reversed_faces).
                    ba = rd.args(b)
                    n, eis, vi = walk_loop(ba[1])
                    if n and len(ba) > 2 and ba[2] == ("enum", "F"):
                        edgeuses[-n:] = [dict(eu, orient="opposite" if eu["orient"] == "same" else "same")
                                         for eu in reversed(edgeuses[-n:])]
                        eis = eis[::-1]
                    loop_specs.append((n, vi))
                    lc += 1
                    for ei in eis: fv.update(edges[ei]["v"])
                    floop_edges.append((eis, [eu["orient"] for eu in edgeuses[-n:]] if n else []))
                    if vi: fv.add(vi)
                if lc > 1 and not any(rd.typ(b) == "FACE_OUTER_BOUND" for b in bounds):
                    loop_specs, floop_edges = _outer_loop_first(
                        loop_specs, floop_edges, edgeuses, edges, verts, esamples)
                fverts = [verts[i] for i in fv]
                # The face:range footprint projects SAMPLED boundary-edge points, not
                # boundary vertices alone -- vertex-only footprints collapse to
                # slivers or select the seam-complement branch whenever the vertices
                # under-sample the boundary (a closed wavy NURBS edge with a single
                # seam vertex, arcs straddling the u-seam, full rim circles). floop_pts
                # keeps the samples CHAINED in loop order so the sphere branch can
                # detect pole containment by longitude winding.
                fsamples = list(fverts)
                floop_pts = []
                for eis, orients in floop_edges:
                    lp = []
                    for ei, orient in zip(eis, orients):
                        if ei not in esamples:
                            esamples[ei] = _edge_interior_samples(edges[ei], verts)
                        s, t = edges[ei]["v"]
                        seg = [verts[s]] + esamples[ei] + [verts[t]]
                        lp += seg if orient == "same" else seg[::-1]
                    if lp: floop_pts.append(lp)
                for ei in dict.fromkeys(ei for eis, _ in floop_edges for ei in eis):
                    fsamples += esamples[ei]
                stok, sg = surface_geom(rd, fa[2], cfg, fsamples)
                rng = face_range(stok, sg, fsamples, loop_pts=floop_pts,
                                 sense=sense)
                sg, rng = rebase_periodic_u(stok, sg, rng)
                # A curved edge with no samples would stand in as its chord.
                chords = any(not esamples[ei] and edges[ei]["ctok"] != "BrepCurve3dLineAPI"
                             for eis, _ in floop_edges for ei in eis)
                flux = None if chords else face_flux(stok, sg, sense, floop_pts)
                for n, vi in loop_specs:
                    loops.append(n)
                    loop_vidx.append(vi)
                fi = len(faces)
                faces.append(dict(loopCount=lc, stok=stok, geom=sg, sense=sense,
                                  rng=rng, flux=flux))
                solid_faces[-1].append(fi)
        brep_faces.append(solid_faces)
        # A surface model is a sheet whatever its shells are: a CLOSED_SHELL in
        # it bounds no material (ISO 10303-42 shell_based_surface_model).
        sheet.append(rd.typ(("ref", solid)) == "SHELL_BASED_SURFACE_MODEL")

    dropped = _drop_subtolerance_edges(verts, edges, edgeuses, loops, loop_vidx,
                                       faces, cfg)
    seams = _synthesize_rim_seams(verts, edges, edgeuses, loops, loop_vidx,
                                  faces, cfg)
    # The seams were minted in STEP's winding, so reversed faces turn last.
    reversed_faces = _reverse_loops_of_reversed_faces(faces, loops, edgeuses)

    by_edge = {}
    for i, eu in enumerate(edgeuses): by_edge.setdefault(eu["edge"], []).append(i)
    for ei, g in by_edge.items():
        for k, idx in enumerate(g):
            edgeuses[idx]["next"] = g[(k+1) % len(g)]
            # A use running along the edge curve enters the radial order from
            # the top, one running against it from the bottom, as SMLib's own
            # exporter writes them. Alternating by position instead fails on a
            # reversed face, whose use of an edge can run the same way as its
            # neighbour's.
            edgeuses[idx]["entry"] = "topEntry" if edgeuses[idx]["orient"] == "same" else "bottomEntry"

    return dict(verts=verts, edges=edges, edgeuses=edgeuses, loops=loops, faces=faces,
                brep_faces=brep_faces, sheet=sheet, by_edge=by_edge, loop_vidx=loop_vidx,
                dropped_edges=dropped, seam_edges=seams, reversed_faces=reversed_faces)

# ================================================================ cavity orientation
# A cavity's walls are an ORIENTED_CLOSED_SHELL around a base CLOSED_SHELL.
# ISO 10303-42 has the base shell's face normals point out of the cavity,
# into the material (5.5.28: a closed shell's normals point from its finite
# region to the infinite one), and the flag .F. (6.4.3; AP242's WR5 forbids
# .T.). Writers differ: Open CASCADE and NX write exactly that, Spatial
# InterOp writes .T. around the same base shell, and Onshape writes .F.
# around a base shell whose normals point into the cavity. The flag decides
# nothing, so pack_regions reads the side from the geometry: the sign of the
# volume the face normals enclose.
#
# That volume is a third of the flux of the position vector x through the
# shell, summed over its faces. face_flux takes each face's flux about a
# point p where (x - p).N is simple on its surface, as term + p.A, A being
# the face's vector area, which its boundary alone fixes. On a plane, and on
# a cone about its apex, (x - p).N is zero. On a cylinder, a cone band, a
# sphere and a torus, Green's theorem in the surface's parameters turns its
# integral into one round the face's loops, sampled as extract_brep samples
# them and in STEP's order, counter-clockwise about the face normal.

def _vector_area(loop_pts, ref):
    """The integral of the face normal over a face whose loops are loop_pts:
    half the sum of p x dp round them, taken about ref, which changes nothing
    but the size of the products."""
    ax = ay = az = 0.0
    for pts in loop_pts:
        n = len(pts)
        for i in range(n):
            p, q = vsub(pts[i], ref), vsub(pts[(i + 1) % n], ref)
            ax += p[1]*q[2] - p[2]*q[1]
            ay += p[2]*q[0] - p[0]*q[2]
            az += p[0]*q[1] - p[1]*q[0]
    return (0.5*ax, 0.5*ay, 0.5*az)

def _unwrap(vals):
    out = [vals[0]]
    for a in vals[1:]:
        out.append(out[-1] + (a - out[-1] + math.pi) % _TWO_PI - math.pi)
    return out

def _loop_coords(loop_pts, uv, periodic):
    """Each loop closed (its first sample again at its end) and mapped by uv
    to (us, vs), the coordinates periodic names unwrapped along the loop.
    None when a step between samples turns more than 2 radians, too coarse to
    unwrap."""
    out = []
    for pts in loop_pts:
        if len(pts) < 2:
            continue
        cs = [uv(p) for p in pts + [pts[0]]]
        us, vs = [c[0] for c in cs], [c[1] for c in cs]
        if periodic[0]: us = _unwrap(us)
        if periodic[1]: vs = _unwrap(vs)
        for xs, per in ((us, periodic[0]), (vs, periodic[1])):
            if per and any(abs(xs[i + 1] - xs[i]) > 2.0 for i in range(len(xs) - 1)):
                return None
        out.append((us, vs))
    return out

def _int_du(loops, f):
    """The loops' integral of f(v) du, by the trapezoid rule."""
    return sum(0.5 * (f(vs[i]) + f(vs[i + 1])) * (us[i + 1] - us[i])
               for us, vs in loops for i in range(len(us) - 1))

def _windings(loops, k):
    return [round((c[k][-1] - c[k][0]) / _TWO_PI) for c in loops]

def _wrapped(area0, whole):
    """How many whole surfaces to add to an area integrated in a chart that
    the face may wrap past: the count that brings it into (0, whole], a face
    that comes to nothing in the chart counting as the whole surface."""
    k = -math.floor(area0 / whole)
    if area0 + k * whole <= 1e-9 * whole:
        k += 1
    return k

def face_flux(stok, sg, sense, loop_pts):
    """The flux of the position vector through a face, as (term, p, A): term
    + p.A, with p a point near the face and A its vector area. None for a
    surface this does not integrate: a NURBS surface that is not flat, or a
    torus whose loops wind round both of its circles."""
    sigma = 1.0 if sense else -1.0
    pts = [q for lp in loop_pts for q in lp]
    if stok == "BrepSurfaceNurbAPI":
        cps = sg.get("controlVertices") or []
        if len(cps) < 3:
            return None
        p0 = cps[0]
        p1 = max(cps, key=lambda q: math.dist(q, p0))
        n = max((vcross(vsub(p1, p0), vsub(q, p0)) for q in cps),
                key=lambda c: vdot(c, c))
        size = math.dist(p1, p0)
        if not size or vdot(n, n) <= (1e-12 * size * size) ** 2:
            return None
        n = vnorm(n)
        if any(abs(vdot(vsub(q, p0), n)) > 1e-7 * size for q in cps):
            return None
        return 0.0, p0, _vector_area(loop_pts, p0)
    if stok == "BrepSurfacePlaneAPI":
        return 0.0, sg["origin"], _vector_area(loop_pts, sg["origin"])
    if stok in ("BrepSurfaceCylinderAPI", "BrepSurfaceConeAPI"):
        o, x, y, z = _uframe(sg)
        R = sg["radius"]
        t = math.tan(sg["semiAngle"]) if stok == "BrepSurfaceConeAPI" else 0.0
        if not pts:
            return None
        hs = [vdot(vsub(q, o), z) for q in pts]
        if abs(t) > 1e-12:
            # About the apex when the face may reach it: its loops wind round
            # the axis (a cap), or it lies within ten of its own sizes of it.
            apex = tuple(o[k] - R / t * z[k] for k in range(3))
            lo, hi = [min(c) for c in zip(*pts)], [max(c) for c in zip(*pts)]
            diam = math.dist(lo, hi)
            uang = lambda q: math.atan2(vdot(vsub(q, o), y), vdot(vsub(q, o), x))
            loops = _loop_coords(loop_pts, lambda q: (uang(q), 0.0), (True, False))
            if (loops is None or any(_windings(loops, 0))
                    or min(math.dist(q, apex) for q in pts) <= 10.0 * diam):
                return 0.0, apex, _vector_area(loop_pts, apex)
        # About the axis point at the face's mid-height: (x - p).N dA is
        # R(R + v tan a) du dv there, a cylinder's r^2 du dv.
        m = 0.5 * (min(hs) + max(hs))
        p = tuple(o[k] + m * z[k] for k in range(3))
        R += m * t
        loops = _loop_coords(
            loop_pts, lambda q: (math.atan2(vdot(vsub(q, p), y), vdot(vsub(q, p), x)),
                                 vdot(vsub(q, p), z)), (True, False))
        if loops is None:
            return None
        term = -R * _int_du(loops, lambda v: R * v + 0.5 * t * v * v)
        return term, p, _vector_area(loop_pts, p)
    if stok == "BrepSurfaceSphereAPI":
        c, r = sg["center"], sg["radius"]
        A = _vector_area(loop_pts, c)
        whole = 4.0 * math.pi * r * r
        if not pts:
            return sigma * r * whole, c, A
        # Integrate in a frame whose poles keep away from the face's loops.
        _, x, y, z = _uframe(sg, "center")
        dirs = [vnorm(vsub(q, c)) for q in pts]
        cands = [z, x, y] + [vnorm(tuple(a * x[k] + b * y[k] + e * z[k] for k in range(3)))
                             for a, b, e in ((1, 1, 1), (1, -1, 1), (-1, 1, 1), (1, 1, -1))]
        ax = max(cands, key=lambda a: min(1.0 - abs(vdot(d, a)) for d in dirs))
        ref = x if abs(vdot(x, ax)) < 0.9 else y
        fx = vnorm(vsub(ref, tuple(vdot(ref, ax) * ax[k] for k in range(3))))
        fy = vcross(ax, fx)
        def uv(q):
            d = vnorm(vsub(q, c))
            return (math.atan2(vdot(d, fy), vdot(d, fx)),
                    math.asin(max(-1.0, min(1.0, vdot(d, ax)))))
        loops = _loop_coords(loop_pts, uv, (True, False))
        if loops is None:
            return None
        # Area from -(r^2 (sin v + 1)) du round the loops: zero at the frame's
        # south pole, and the whole sphere added when the face holds its north.
        area0 = -sigma * _int_du(loops, lambda v: r * r * (math.sin(v) + 1.0))
        area = area0 + _wrapped(area0, whole) * whole
        if not 0.0 < area <= whole * (1.0 + 1e-6):
            return None
        return sigma * r * area, c, A
    if stok == "BrepSurfaceTorusAPI":
        o, x, y, z = _uframe(sg)
        R, r = sg["majorRadius"], sg["minorRadius"]
        if not R > r > 0.0:
            return None
        A = _vector_area(loop_pts, o)
        whole, flux_whole = 4.0 * math.pi ** 2 * R * r, 6.0 * math.pi ** 2 * R * r * r
        if not pts:
            return sigma * flux_whole, o, A
        def uv(q):
            d = vsub(q, o)
            u = math.atan2(vdot(d, y), vdot(d, x))
            e = (math.cos(u) * x[0] + math.sin(u) * y[0], math.cos(u) * x[1] + math.sin(u) * y[1],
                 math.cos(u) * x[2] + math.sin(u) * y[2])
            t = vsub(d, tuple(R * e[k] for k in range(3)))
            return u, math.atan2(vdot(t, z), vdot(t, e))
        loops = _loop_coords(loop_pts, uv, (True, True))
        if loops is None:
            return None
        # (x - o).N dA = g(v) du dv, g = r (R + r cos v)(R cos v + r); the
        # area element is a(v) du dv, a = r (R + r cos v).
        g = lambda v: r * (R + r * math.cos(v)) * (R * math.cos(v) + r)
        if not any(_windings(loops, 1)):
            G = lambda v: r * ((R * R + r * r) * math.sin(v)
                               + R * r * (1.5 * v + 0.25 * math.sin(2.0 * v)))
            area0 = -sigma * _int_du(loops, lambda v: r * (R * v + r * math.sin(v)))
            term = -_int_du(loops, G)
        elif not any(_windings(loops, 0)):
            # Loops round the tube: integrate u f(v) dv instead.
            dv = lambda f: sum(0.5 * (us[i] * f(vs[i]) + us[i + 1] * f(vs[i + 1]))
                               * (vs[i + 1] - vs[i])
                               for us, vs in loops for i in range(len(us) - 1))
            area0 = sigma * dv(lambda v: r * (R + r * math.cos(v)))
            term = dv(g)
        else:
            return None
        return term + sigma * _wrapped(area0, whole) * flux_whole, o, A
    return None

def shell_volume(b, face_ids):
    """The volume a closed shell's faces enclose, positive when their normals
    (surface normal and same_sense) point out of it. None when a face's flux
    is unknown, or when the sum is too small against its parts to trust."""
    total = gross = 0.0
    p0 = None
    for fi in face_ids:
        flux = b["faces"][fi].get("flux")
        if flux is None:
            return None
        term, p, A = flux
        p0 = p if p0 is None else p0
        part = vdot(vsub(p, p0), A)
        total += term + part
        gross += abs(term) + abs(part)
    if not gross or abs(total) < 1e-3 * gross:
        return None
    return total / 3.0

# ================================================================ region packing
def pack_regions(b):
    """Void-included radial-edge form: regions, their shells, and the two
    faceuses of every face.

    A STEP body's first shell bounds the solid; the rest are the walls of its
    internal cavities. The proposal packs that as one region per enclosed
    volume plus the infinite exterior: a cube with one spherical void has
    regionCount 3 and region:shellCount [1, 2, 1] -- the infinite void, the
    solid carrying an outer shell and one inner shell, then the cavity. A
    manifold body with no voids is the degenerate case of that, [1, 1].

    Each face contributes one faceuse to the region on either side of it: the
    outer shell faces the infinite void and the solid, a cavity wall faces the
    solid and that cavity. Which side of a cavity wall is the cavity's comes
    from the volume its face normals enclose (shell_volume), whatever the
    ORIENTED_CLOSED_SHELL's flag says: a positive volume puts the cavity
    against the normals. When the volume cannot be had (a curved NURBS face),
    the wall is read as ISO 10303-42 writes it, normals into the material.
    b["cavities"] records, per body, how many cavities each way was read.

    A sheet body (a SHELL_BASED_SURFACE_MODEL, of open shells as NAPA Designer
    exports a plate, or of closed ones) encloses no material: the proposal supports it and exempts
    its shells from the closed-solid rule. It packs as the infinite void
    alone, each shell holding both faceuses of each of its faces.
    """
    regionCount, regionType, regionShellCount, shellFaceuseCount = [], [], [], []
    fuFaceIndex, fuOrient = [], []
    cavities = b["cavities"] = []

    def emit_shell(face_ids, outward):
        """One faceuse per face, on the side facing the region being built.

        faceuse:orientationType names a side of the surface, so the two
        faceuses of a face are always one `same` and one `opposite`. A face
        whose ADVANCED_FACE same_sense is .F. has its surface normal pointing
        into the solid, so its outward side is `opposite`. Its loops are
        reversed to match (_reverse_loops_of_reversed_faces): flipping the
        sides alone leaves the two uses of a shared edge in different shells,
        8,236 "Edgeuse/RadialEdgeuse pair not attached to same Shell" failures
        on the KUKA export."""
        shellFaceuseCount.append(len(face_ids))
        for fi in face_ids:
            fuFaceIndex.append(fi)
            side = outward == b["faces"][fi]["sense"]
            fuOrient.append("same" if side else "opposite")

    for k, shells in enumerate(b["brep_faces"]):
        if (b.get("sheet") or [False] * (k + 1))[k]:
            cavities.append(dict(byVolume=0, byRule=0))
            regionCount.append(1)
            regionType.append("voidRegion")
            regionShellCount.append(len(shells))
            for sh in shells:
                shellFaceuseCount.append(2 * len(sh))
                for fi in sh:
                    fuFaceIndex += [fi, fi]
                    fuOrient += ["same", "opposite"]
            continue
        outer, voids = shells[0], shells[1:]
        regionCount.append(2 + len(voids))
        regionType += ["voidRegion", "solidRegion"] + ["voidRegion"] * len(voids)
        regionShellCount += [1, 1 + len(voids)] + [1] * len(voids)

        # True where a cavity wall's normals point into the material.
        vols = [shell_volume(b, v) for v in voids]
        into = [True if vol is None else vol > 0.0 for vol in vols]
        cavities.append(dict(byVolume=sum(vol is not None for vol in vols),
                             byRule=sum(vol is None for vol in vols)))

        emit_shell(outer, True)                 # infinite exterior void
        emit_shell(outer, False)                # solid: its outer shell
        for v, w in zip(voids, into):
            emit_shell(v, w)                    # solid: one inner shell per cavity
        for v, w in zip(voids, into):
            emit_shell(v, not w)                # each cavity, seen from inside

    return dict(regionCount=regionCount, regionType=regionType,
                regionShellCount=regionShellCount, shellFaceuseCount=shellFaceuseCount,
                fuFaceIndex=fuFaceIndex, fuOrient=fuOrient)

# ================================================================ self-check
def _edge_in_tolerance_sphere(edge, verts, tol):
    """True when the whole 3D curve fits inside a sphere of radius tol.

    Proposal rule 7.ii forbids such an edge. Testing containment rather than
    endpoint separation matters: a nearly-closed arc also brings its two
    vertices within tol of each other while enclosing the full circle."""
    pts = [verts[edge["v"][0]], verts[edge["v"][1]]] \
        + _edge_interior_samples(edge, verts)
    cx = [sum(p[k] for p in pts) / len(pts) for k in range(3)]
    return all(math.dist(p, cx) <= tol for p in pts)

def _drop_subtolerance_edges(verts, edges, edgeuses, loops, loop_vidx, faces, cfg):
    """Weld out every edge that rule 7.ii forbids, and report how many.

    A tolerant modeller emits an edge wherever its own topology has one,
    including where the two ends have already closed to within the file's
    declared tolerance -- an NX export of a production robot carries 149 of
    them. The two vertices are the same point as far as the file is concerned,
    so welding them and dropping the edge from its loops closes the boundary
    chain again and leaves the face's shape untouched.

    Runs before the radial chains are built, so nextRadialEUIndex and the
    top/bottom entry alternation are computed on the surviving edgeuses."""
    dead = {i for i, e in enumerate(edges)
            if e["v"][0] != e["v"][1]
            and _edge_in_tolerance_sphere(e, verts, cfg.edge_degen_tol)}
    if not dead:
        return 0

    # Weld each dead edge's two vertices onto the lower index, following chains
    # so that three mutually-coincident vertices collapse to one.
    parent = list(range(len(verts)))
    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a
    for i in dead:
        a, b = (find(x) for x in edges[i]["v"])
        if a != b: parent[max(a, b)] = min(a, b)

    vkeep = sorted({find(v) for v in range(len(verts))})
    vnew = {old: n for n, old in enumerate(vkeep)}
    vmap = [vnew[find(v)] for v in range(len(verts))]
    # Site the survivor at the centroid of the vertices it absorbs, not at one of
    # them: every adjacent curve then moves by half the collapsed edge's length
    # instead of one of them moving by all of it.
    members = {}
    for v in range(len(verts)): members.setdefault(find(v), []).append(v)
    verts[:] = [tuple(sum(verts[m][k] for m in members[v]) / len(members[v])
                      for k in range(3)) for v in vkeep]

    # Drop the dead edgeuses loop by loop, so each loop's count stays in step
    # with the flat edgeuse list the schema packs them into.
    kept_eu, off = [], 0
    for li, n in enumerate(loops):
        survivors = [eu for eu in edgeuses[off:off + n] if eu["edge"] not in dead]
        off += n
        loops[li] = len(survivors)
        kept_eu += survivors
    edgeuses[:] = kept_eu

    ekeep = [i for i in range(len(edges)) if i not in dead]
    emap = {old: n for n, old in enumerate(ekeep)}
    edges[:] = [edges[i] for i in ekeep]
    for e in edges: e["v"] = (vmap[e["v"][0]], vmap[e["v"][1]])
    for eu in edgeuses: eu["edge"] = emap[eu["edge"]]
    loop_vidx[:] = [vmap[v] for v in loop_vidx]

    # Re-fit the surviving line edges through their moved vertices, the same
    # way eidx fits them when it first reads them. A welded vertex shifts by
    # half the collapsed edge's length, which is enough on its own to push an
    # already-marginal endpoint past brep:intersectTol3d. Circles and NURBS
    # would need their parameter range re-solved instead, which is healing
    # proper and is left to a healer.
    for e in edges:
        if e["ctok"] != "BrepCurve3dLineAPI":
            continue
        ps, pe = verts[e["v"][0]], verts[e["v"][1]]
        length = math.dist(ps, pe)
        if length > cfg.edge_degen_tol:
            e["geom"] = dict(origin=tuple(ps), direction=vnorm(vsub(pe, ps)))
            e["rng"] = (0.0, length)

    # A loop emptied by the weld is a single point: author it as a vertex loop
    # (rule 428) rather than leaving a loop with no edgeuses (BA.145).
    off = 0
    for li, n in enumerate(loops):
        if n == 0 and not loop_vidx[li]:
            loop_vidx[li] = vmap[find(0)] if not verts else loop_vidx[li]
    return len(dead)

def _synthesize_rim_seams(verts, edges, edgeuses, loops, loop_vidx, faces, cfg):
    """Author the seam edge rule 5.iv wants on a full-period cylinder or cone face.

    STEP exports a full revolution as a face that closes on itself: no seam,
    and the boundary split across two loops, because STEP has no rule requiring
    one. UsdSolid requires a single outer loop carrying a seam edge twice, so a
    translator has to mint what was never written.

    Handles a face with two loops where at least one is a single closed rim.
    The seam is the surface's own ruling -- a straight line for a cylinder and
    for a cone alike -- from that rim's vertex to where the other loop's chain
    begins, and the joined loop runs round the rim, up the seam, round the other
    loop, and back down it. Because the other loop is traversed from its own
    first vertex, no rotation of its edgeuses is needed and nothing is split.

    A ruling only exists where both ends sit at the same angle about the axis.
    Where they do not, the rim's vertex is slid around its own circle to meet
    the other, which re-parameterizes the circle without changing its shape.
    That is available only while the vertex belongs to no other edge and no
    earlier seam already ends on it -- two coaxial faces can share one rim.

    Returns the number of faces given a seam."""
    TWO = 2 * math.pi
    vuse = Counter()
    for e in edges:
        a, b = e["v"]
        vuse[a] += 1
        if b != a: vuse[b] += 1

    def angle_about(p, c, z, x):
        d = vsub(p, c)
        d = vsub(d, tuple(vdot(d, z) * z[k] for k in range(3)))
        return math.atan2(vdot(d, vcross(z, x)), vdot(d, x))

    def start_vertex(eu):
        v = edges[eu["edge"]]["v"]
        return v[0] if eu["orient"] == "same" else v[1]

    out_loops, out_vidx, out_eus = [], [], []
    li = eo = 0
    seamed = 0
    pinned = set()          # rim vertices an already-minted seam ends on
    for face in faces:
        n = face["loopCount"]
        block = []
        for _ in range(n):
            k = loops[li]
            block.append((k, loop_vidx[li], edgeuses[eo:eo + k]))
            eo += k; li += 1

        def keep():
            for k, vi, eus in block:
                out_loops.append(k); out_vidx.append(vi); out_eus.extend(eus)

        if (face["stok"] not in ("BrepSurfaceCylinderAPI", "BrepSurfaceConeAPI")
                or n != 2 or any(k < 1 for k, _, _ in block)
                or abs((face["rng"][0][1] - face["rng"][0][0]) - TWO) > 1e-4):
            keep(); continue

        rims = [i for i, (k, _, eus) in enumerate(block)
                if k == 1 and (lambda v: v[0] == v[1])(edges[eus[0]["edge"]]["v"])]
        if not rims or block[0][2][0]["edge"] == block[1][2][0]["edge"]:
            keep(); continue

        sg = face["geom"]
        z, x, o = vnorm(sg["axis"]), vnorm(sg["refDirection"]), sg["origin"]
        pick = None
        for ri in rims:
            oi = 1 - ri
            e_r = block[ri][2][0]["edge"]
            v_r = edges[e_r]["v"][0]
            v_t = start_vertex(block[oi][2][0])
            if v_t == v_r:
                continue
            off = abs((angle_about(verts[v_r], o, z, x)
                       - angle_about(verts[v_t], o, z, x) + math.pi) % TWO - math.pi)
            if off <= 1e-9:
                pick = (ri, oi, e_r, v_r, v_t, False); break
            if (edges[e_r]["ctok"] == "BrepCurve3dCircleAPI"
                    and vuse[v_r] <= 1 and v_r not in pinned):
                pick = (ri, oi, e_r, v_r, v_t, True); break
        if pick is None:
            keep(); continue
        ri, oi, e_r, v_r, v_t, slide = pick

        if slide:
            cg = edges[e_r]["geom"]
            c, cz, cx, r = cg["center"], vnorm(cg["axis"]), vnorm(cg["refDirection"]), cg["radius"]
            rad = vsub(verts[v_t], o)
            rad = vnorm(vsub(rad, tuple(vdot(rad, z) * z[k] for k in range(3))))
            verts[v_r] = tuple(c[k] + r * rad[k] for k in range(3))
            t = angle_about(verts[v_r], c, cz, cx)
            edges[e_r]["rng"] = _primary_period(t, t + TWO)

        p0, p1 = verts[v_r], verts[v_t]
        length = math.dist(p0, p1)
        if length <= cfg.edge_degen_tol:
            keep(); continue
        seam = len(edges)
        edges.append(dict(v=(v_r, v_t), ctok="BrepCurve3dLineAPI",
                          geom=dict(origin=tuple(p0), direction=vnorm(vsub(p1, p0))),
                          rng=(0.0, length)))
        others = block[oi][2]
        out_eus.extend([block[ri][2][0], dict(edge=seam, orient="same")]
                       + others + [dict(edge=seam, orient="opposite")])
        out_loops.append(3 + len(others)); out_vidx.append(0)
        face["loopCount"] = 1
        pinned.update((v_r, v_t))
        seamed += 1

    loops[:] = out_loops
    loop_vidx[:] = out_vidx
    edgeuses[:] = out_eus
    return seamed

def self_check(b):
    errs = []
    nv, ne, neu = len(b["verts"]), len(b["edges"]), len(b["edgeuses"])
    for ei, e in enumerate(b["edges"]):
        if not all(0 <= x < nv for x in e["v"]): errs.append(f"edge {ei} vertex OOB")
    for i, eu in enumerate(b["edgeuses"]):
        if not 0 <= eu["edge"] < ne: errs.append(f"edgeuse {i} edgeIndex OOB")
        if not 0 <= eu["next"] < neu: errs.append(f"edgeuse {i} nextRadial OOB")
    if sorted(eu["next"] for eu in b["edgeuses"]) != list(range(neu)): errs.append("nextRadial not a permutation")
    if sum(b["loops"]) != neu: errs.append("sum(loop:edgeuseCount) != #edgeuses")
    if sum(f["loopCount"] for f in b["faces"]) != len(b["loops"]): errs.append("sum(face:loopCount) != #loops")
    reg = pack_regions(b)
    if sum(reg["shellFaceuseCount"]) != len(reg["fuFaceIndex"]): errs.append("sum(shell:faceuseCount) != #faceuses")
    if len(reg["fuFaceIndex"]) != 2*len(b["faces"]): errs.append("#faceuses != 2 x #faces")
    if any(not (0 <= fi < len(b["faces"])) for fi in reg["fuFaceIndex"]): errs.append("faceuse:faceIndex OOB")
    uses = (1, 2) if any(b.get("sheet") or []) else (2,)   # a sheet's free edges have one
    bad = {ei: len(g) for ei, g in b["by_edge"].items() if len(g) not in uses}
    if bad: errs.append(f"non-manifold edges: {dict(list(bad.items())[:4])}")
    for e in b["edges"]:
        g = e["geom"]
        if g.get("nurb") and len(g["knots"]) != g["order"] + g["vertexCount"]:
            errs.append("edge NURBS len(knots) != order+vertexCount")
    for f in b["faces"]:
        g = f["geom"]
        if g.get("nurb"):
            if len(g["uKnots"]) != g["uOrder"]+g["uVertexCount"]: errs.append("surf uKnots size")
            if len(g["vKnots"]) != g["vOrder"]+g["vVertexCount"]: errs.append("surf vKnots size")
    return errs

# ================================================================ extent
def extent_hull(b):
    """Points whose box bounds every edge and face of the brep dict b: its
    vertices; every NURBS control vertex (a NURBS curve or surface with
    positive weights lies in its control hull, and BrepArrayContainment
    checks the control vertices against the extent); the rational poles of
    each circle and ellipse edge over its range, which hold the arc the same
    way; and the box of each sphere and torus face, whose surface can bulge
    past its boundary. A plane, cylinder or cone face lies within the hull of
    its boundary edges, so these cover it."""
    hull = list(b["verts"])
    for e in b["edges"]:
        g = e["geom"]
        if g.get("nurb"):
            hull += list(g["controlVertices"])
        elif e["ctok"] in ("BrepCurve3dCircleAPI", "BrepCurve3dEllipseAPI") and e.get("rng"):
            z, x = vnorm(g["axis"]), vnorm(g["refDirection"])
            y = vcross(z, x)
            rx = g.get("radius", g.get("xRadius"))
            ry = g.get("radius", g.get("yRadius"))
            a0, a1 = e["rng"]
            if a1 > a0:
                hull += _arc_poles(g["center"], x, y, rx, ry, a0, a1)["poles"]
    for f in b["faces"]:
        g, stok = f["geom"], f["stok"]
        if g.get("nurb"):
            hull += list(g["controlVertices"])
        elif stok in ("BrepSurfaceSphereAPI", "BrepSurfaceTorusAPI"):
            o = g.get("center", g.get("origin"))
            z, x = vnorm(g["axis"]), vnorm(g["refDirection"])
            y = vcross(z, x)
            r = g["radius"] if stok == "BrepSurfaceSphereAPI" else g["majorRadius"] + g["minorRadius"]
            h = g["radius"] if stok == "BrepSurfaceSphereAPI" else g["minorRadius"]
            hull += [tuple(o[k] + sx * r * x[k] + sy * r * y[k] + sz * h * z[k] for k in range(3))
                     for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
    return hull

def local_extent(verts):
    if not verts: return ((0., 0., 0.), (0., 0., 0.))
    mn = [min(v[k] for v in verts) for k in range(3)]
    mx = [max(v[k] for v in verts) for k in range(3)]
    return (tuple(mn), tuple(mx))

# ================================================================ USD authoring (pxr)
def _v3d(triples): return Vt.Vec3dArray([Gf.Vec3d(p[0], p[1], p[2]) for p in triples])
def _v3f(triples): return Vt.Vec3fArray([Gf.Vec3f(p[0], p[1], p[2]) for p in triples])
def _dbl(xs): return Vt.DoubleArray([float(x) for x in xs])
def _ui(xs): return Vt.UIntArray([int(x) for x in xs])
def _tok(xs): return Vt.TokenArray(list(xs))

def author_brep(stage, path, b, cfg, face_colors=None):
    """Author one BrepArray prim from an extracted brep dict, using the typed
    UsdSolid schema classes. Topology arrays live on the BrepArray; geometry lives
    on the applied surface/curve/point API schemas. face_colors (optional): one
    color per face, authored as a uniform displayColor primvar."""
    ba = UsdSolid.BrepArray.Define(stage, path)
    prim = ba.GetPrim()
    reg = pack_regions(b)
    faces = b["faces"]
    edges = b["edges"]
    verts = b["verts"]
    nshell = len(reg["shellFaceuseCount"])
    nfaces = len(faces)
    nverts = len(verts)
    nloops = len(b["loops"])

    # ---- topology (on the BrepArray) ----
    ba.CreateBrepIntersectTol3dAttr(_dbl([cfg.intersect_tol]))
    ba.CreateBrepRegionCountAttr(_ui(reg["regionCount"]))
    ba.CreateRegionShellCountAttr(_ui(reg["regionShellCount"]))
    ba.CreateRegionTypeAttr(_tok(reg["regionType"]))
    ba.CreateShellFaceuseCountAttr(_ui(reg["shellFaceuseCount"]))
    ba.CreateShellWireEdgeCountAttr(_ui([0] * nshell))
    ba.CreateShellPointTypeAttr(_tok(["none"] * nshell))
    ba.CreateFaceuseFaceIndexAttr(_ui(reg["fuFaceIndex"]))
    ba.CreateFaceuseOrientationTypeAttr(_tok(reg["fuOrient"]))
    ba.CreateFaceLoopCountAttr(_ui([f["loopCount"] for f in faces]))
    ba.CreateFaceSurfaceTypeAttr(_tok([f["stok"] for f in faces]))
    ba.CreateFaceTrimTypeAttr(_tok(["general"] * nfaces))
    frange = []
    for f in faces:
        (umin, umax), (vmin, vmax) = f["rng"]
        frange += [Gf.Vec2d(umin, vmin), Gf.Vec2d(umax, vmax)]
    ba.CreateFaceRangeAttr(Vt.Vec2dArray(frange))
    ba.CreateLoopEdgeuseCountAttr(_ui(b["loops"]))
    lv = b.get("loop_vidx")
    ba.CreateLoopVertexIndexAttr(_ui(lv if (lv and any(lv) and len(lv) == nloops) else [0] * nloops))
    ba.CreateEdgeuseEdgeIndexAttr(_ui([eu["edge"] for eu in b["edgeuses"]]))
    ba.CreateEdgeuseNextRadialEUIndexAttr(_ui([eu["next"] for eu in b["edgeuses"]]))
    ba.CreateEdgeuseOrientationTypeAttr(_tok([eu["orient"] for eu in b["edgeuses"]]))
    ba.CreateEdgeuseThisRadialEntryTypeAttr(_tok([eu["entry"] for eu in b["edgeuses"]]))
    ba.CreateEdgeVertexIndicesAttr(Vt.Vec2iArray([Gf.Vec2i(e["v"][0], e["v"][1]) for e in edges]))
    ba.CreateEdgeCurveTypeAttr(_tok([e["ctok"] for e in edges]))
    erange = []
    for e in edges:
        erange += [float(e["rng"][0]), float(e["rng"][1])]
    ba.CreateEdgeRangeAttr(_dbl(erange))
    ba.CreateVertexPointTypeAttr(_tok(["BrepPointAPI"] * nverts))
    ba.CreateWireEdgeCurveTypeAttr(_tok([]))
    ba.CreateWireEdgeVertexIndicesAttr(Vt.Vec2iArray([]))
    ba.CreateWireEdgeRangeAttr(_dbl([]))

    # ---- vertex positions (multi-apply BrepPointAPI:vertexPoint) ----
    UsdSolid.BrepPointAPI.Apply(prim, "vertexPoint").CreatePointPositionAttr(_v3d(verts))

    # ---- 3D curve geometry, packed per type in edge order ----
    def cg_of(tok): return [e["geom"] for e in edges if e["ctok"] == tok]
    lines = cg_of("BrepCurve3dLineAPI")
    if lines:
        ln = UsdSolid.BrepCurve3dLineAPI.Apply(prim, "edge3dLine")
        ln.CreateCurve3dLineOriginAttr(_v3d([g["origin"] for g in lines]))
        ln.CreateCurve3dLineDirectionAttr(_v3d([g["direction"] for g in lines]))
    circ = cg_of("BrepCurve3dCircleAPI")
    if circ:
        cc = UsdSolid.BrepCurve3dCircleAPI.Apply(prim, "edge3dCircle")
        cc.CreateCurve3dCircleCenterAttr(_v3d([g["center"] for g in circ]))
        cc.CreateCurve3dCircleAxisAttr(_v3d([g["axis"] for g in circ]))
        cc.CreateCurve3dCircleRefDirectionAttr(_v3d([g["refDirection"] for g in circ]))
        cc.CreateCurve3dCircleRadiusAttr(_dbl([g["radius"] for g in circ]))
    ell = cg_of("BrepCurve3dEllipseAPI")
    if ell:
        ec = UsdSolid.BrepCurve3dEllipseAPI.Apply(prim, "edge3dEllipse")
        ec.CreateCurve3dEllipseCenterAttr(_v3d([g["center"] for g in ell]))
        ec.CreateCurve3dEllipseAxisAttr(_v3d([g["axis"] for g in ell]))
        ec.CreateCurve3dEllipseRefDirectionAttr(_v3d([g["refDirection"] for g in ell]))
        ec.CreateCurve3dEllipseXRadiusAttr(_dbl([g["xRadius"] for g in ell]))
        ec.CreateCurve3dEllipseYRadiusAttr(_dbl([g["yRadius"] for g in ell]))
    nurbc = cg_of("BrepCurve3dNurbAPI")
    if nurbc:
        nc = UsdSolid.BrepCurve3dNurbAPI.Apply(prim, "edge3dNurb")
        nc.CreateCurve3dOrderAttr(_ui([g["order"] for g in nurbc]))
        nc.CreateCurve3dVertexCountAttr(_ui([g["vertexCount"] for g in nurbc]))
        nc.CreateCurve3dKnotsAttr(_dbl([k for g in nurbc for k in g["knots"]]))
        nc.CreateCurve3dControlVerticesAttr(_v3d([p for g in nurbc for p in g["controlVertices"]]))
        nc.CreateCurve3dWeightsAttr(_dbl([w for g in nurbc for w in g["weights"]]))

    # ---- surface geometry, packed per type in face order ----
    def sg_of(tok): return [f["geom"] for f in faces if f["stok"] == tok]
    pl = sg_of("BrepSurfacePlaneAPI")
    if pl:
        sp = UsdSolid.BrepSurfacePlaneAPI.Apply(prim)
        sp.CreateSurfacePlaneOriginAttr(_v3d([g["origin"] for g in pl]))
        sp.CreateSurfacePlaneAxisAttr(_v3d([g["axis"] for g in pl]))
        sp.CreateSurfacePlaneRefDirectionAttr(_v3d([g["refDirection"] for g in pl]))
    cyl = sg_of("BrepSurfaceCylinderAPI")
    if cyl:
        sc = UsdSolid.BrepSurfaceCylinderAPI.Apply(prim)
        sc.CreateSurfaceCylinderOriginAttr(_v3d([g["origin"] for g in cyl]))
        sc.CreateSurfaceCylinderAxisAttr(_v3d([g["axis"] for g in cyl]))
        sc.CreateSurfaceCylinderRefDirectionAttr(_v3d([g["refDirection"] for g in cyl]))
        sc.CreateSurfaceCylinderRadiusAttr(_dbl([g["radius"] for g in cyl]))
    cone = sg_of("BrepSurfaceConeAPI")
    if cone:
        sco = UsdSolid.BrepSurfaceConeAPI.Apply(prim)
        sco.CreateSurfaceConeOriginAttr(_v3d([g["origin"] for g in cone]))
        sco.CreateSurfaceConeAxisAttr(_v3d([g["axis"] for g in cone]))
        sco.CreateSurfaceConeRefDirectionAttr(_v3d([g["refDirection"] for g in cone]))
        sco.CreateSurfaceConeRadiusAttr(_dbl([g["radius"] for g in cone]))
        sco.CreateSurfaceConeSemiAngleAttr(_dbl([g["semiAngle"] for g in cone]))
    sph = sg_of("BrepSurfaceSphereAPI")
    if sph:
        ss = UsdSolid.BrepSurfaceSphereAPI.Apply(prim)
        ss.CreateSurfaceSphereCenterAttr(_v3d([g["center"] for g in sph]))
        ss.CreateSurfaceSphereAxisAttr(_v3d([g["axis"] for g in sph]))
        ss.CreateSurfaceSphereRefDirectionAttr(_v3d([g["refDirection"] for g in sph]))
        ss.CreateSurfaceSphereRadiusAttr(_dbl([g["radius"] for g in sph]))
    tor = sg_of("BrepSurfaceTorusAPI")
    if tor:
        st = UsdSolid.BrepSurfaceTorusAPI.Apply(prim)
        st.CreateSurfaceTorusOriginAttr(_v3d([g["origin"] for g in tor]))
        st.CreateSurfaceTorusAxisAttr(_v3d([g["axis"] for g in tor]))
        st.CreateSurfaceTorusRefDirectionAttr(_v3d([g["refDirection"] for g in tor]))
        st.CreateSurfaceTorusMajorRadiusAttr(_dbl([g["majorRadius"] for g in tor]))
        st.CreateSurfaceTorusMinorRadiusAttr(_dbl([g["minorRadius"] for g in tor]))
    ns = sg_of("BrepSurfaceNurbAPI")
    if ns:
        sn = UsdSolid.BrepSurfaceNurbAPI.Apply(prim)
        sn.CreateSurfaceUOrderAttr(_ui([g["uOrder"] for g in ns]))
        sn.CreateSurfaceVOrderAttr(_ui([g["vOrder"] for g in ns]))
        sn.CreateSurfaceUVertexCountAttr(_ui([g["uVertexCount"] for g in ns]))
        sn.CreateSurfaceVVertexCountAttr(_ui([g["vVertexCount"] for g in ns]))
        sn.CreateSurfaceUKnotsAttr(_dbl([k for g in ns for k in g["uKnots"]]))
        sn.CreateSurfaceVKnotsAttr(_dbl([k for g in ns for k in g["vKnots"]]))
        sn.CreateSurfaceControlVerticesAttr(_v3d([p for g in ns for p in g["controlVertices"]]))
        sn.CreateSurfaceWeightsAttr(_dbl([w for g in ns for w in g["weights"]]))

    # ---- extent + optional per-face color ----
    mn, mx = local_extent(extent_hull(b))
    ba.CreateBrepExtentAttr(_v3d([mn, mx]))
    ba.CreateExtentAttr(_v3f([mn, mx]))
    if face_colors and len(face_colors) == nfaces and len(set(face_colors)) > 1:
        pv = UsdGeom.PrimvarsAPI(prim).CreatePrimvar(
            "displayColor", Sdf.ValueTypeNames.Color3fArray, UsdGeom.Tokens.uniform)
        pv.Set(_v3f(face_colors))
    return ba

# ================================================================ per-file setup
def derive_tolerance(rd, ents, meters_per_unit=0.001):
    """The brep:intersectTol3d for a file, in its model units: the producer's
    declared endpoint agreement (UNCERTAINTY_MEASURE) divided by 20 and floored
    at MIN_INTERSECT_TOL. When the file declares no uncertainty, fall back to
    1e-5 x the model bounding-box diagonal, then to 1e-3 mm. The millimetre
    floors scale with the file's length unit (meters_per_unit). Returns (tol,
    uncertainty, source_string)."""
    mm = 0.001 / float(meters_per_unit or 0.001)       # model units per millimetre
    unc, usrc = detect_length_uncertainty(rd, ents, meters_per_unit)
    if unc is None:
        pts = [rd.point(("ref", i)) for i, (t, a) in ents.items()
               if t == "CARTESIAN_POINT" and len(a) > 1 and isinstance(a[1], list) and len(a[1]) == 3]
        if pts:
            mn = [min(p[k] for p in pts) for k in range(3)]
            mx = [max(p[k] for p in pts) for k in range(3)]
            diag = math.sqrt(sum((mx[k] - mn[k]) ** 2 for k in range(3)))
            unc = 1e-5 * diag
            usrc = f"fallback 1e-5*bboxDiag({diag:.0f})"
        else:
            unc = 1e-3 * mm
            usrc = "fallback default"
    return max(MIN_INTERSECT_TOL * mm, unc / 20.0), unc, usrc

# ================================================================ assembly graph
def _a2p_matrix(rd, ref):
    """The 4x4 of an AXIS2_PLACEMENT_3D, columns (x, y, z, origin)."""
    o, z, x = rd.placement(ref)
    x = vnorm(vsub(x, tuple(vdot(x, z) * z[k] for k in range(3))))
    y = vcross(z, x)
    return (x, y, z, o)

def _mat_mul(A, B):
    """Compose two (x, y, z, origin) frames: apply B, then A."""
    ax, ay, az, ao = A
    def rot(v):
        return tuple(ax[k]*v[0] + ay[k]*v[1] + az[k]*v[2] for k in range(3))
    bx, by, bz, bo = B
    ro = rot(bo)
    return (rot(bx), rot(by), rot(bz), tuple(ao[k] + ro[k] for k in range(3)))

_IDENTITY = ((1.0,0.0,0.0), (0.0,1.0,0.0), (0.0,0.0,1.0), (0.0,0.0,0.0))

def _frame_inverse(F):
    """The inverse of a rigid (x, y, z, origin) frame: rotation R transposed,
    origin -R^T o."""
    x, y, z, o = F
    return (tuple((x[k], y[k], z[k]) for k in range(3))
            + ((-vdot(x, o), -vdot(y, o), -vdot(z, o)),))

def _is_identity(M, tol=1e-12):
    return all(abs(M[i][k] - _IDENTITY[i][k]) <= tol for i in range(4) for k in range(3))

def _idt_matrix(rd, idt_ref, rep1_is_parent):
    """The placement an ITEM_DEFINED_TRANSFORMATION gives a child: its item in
    the child's representation mapped onto its item in the parent's,
    P(parent item) * P(child item)^-1. transform_item_1 lies in rep_1 and
    transform_item_2 in rep_2 (ISO 10303-43), so rep1_is_parent says which item
    is the parent's. None when the operator is not two AXIS2_PLACEMENT_3Ds."""
    a = rd.args(idt_ref) if rd.typ(idt_ref) == "ITEM_DEFINED_TRANSFORMATION" else []
    frames = [x for x in a if isinstance(x, tuple) and x[0] == "ref"
              and rd.typ(x) == "AXIS2_PLACEMENT_3D"]
    if len(frames) < 2:
        return None
    item1, item2 = _a2p_matrix(rd, frames[0]), _a2p_matrix(rd, frames[1])
    parent, child = (item1, item2) if rep1_is_parent else (item2, item1)
    return _mat_mul(parent, _frame_inverse(child))

def _complex_parts(rd, ref):
    """The sub-entities of a complex instance, as {TYPE: args}."""
    t = rd.get(ref)
    if not t or t[0] != "__COMPLEX__":
        return {}
    return {name: args for name, args in t[1]}

def _representations_by_definition(rd):
    """{product_definition_id: [representation_id, ...]}: the shape each
    SHAPE_DEFINITION_REPRESENTATION gives a part or assembly through its
    PRODUCT_DEFINITION_SHAPE."""
    out = {}
    for sdr in rd.find("SHAPE_DEFINITION_REPRESENTATION"):
        a = rd.args(("ref", sdr))
        if len(a) < 2 or not (isinstance(a[0], tuple) and isinstance(a[1], tuple)):
            continue
        pds = rd.args(a[0])
        d = pds[2] if len(pds) > 2 else None
        if isinstance(d, tuple) and rd.typ(d) in (
                "PRODUCT_DEFINITION", "PRODUCT_DEFINITION_WITH_ASSOCIATED_DOCUMENTS"):
            out.setdefault(d[1], []).append(a[1][1])
    return out

def _same_space(rd):
    """A function from a representation id to a class id, equal for
    representations a plain SHAPE_REPRESENTATION_RELATIONSHIP joins: a part's
    SHAPE_REPRESENTATION and the ADVANCED_BREP_SHAPE_REPRESENTATION that holds
    its solids share one coordinate system."""
    parent = {}
    def find(r):
        while parent.get(r, r) != r:
            r = parent[r]
        return r
    for rel in rd.find("SHAPE_REPRESENTATION_RELATIONSHIP"):
        refs = [x[1] for x in rd.args(("ref", rel)) if isinstance(x, tuple) and x[0] == "ref"]
        if len(refs) >= 2:
            a, b = find(refs[0]), find(refs[1])
            if a != b:
                parent[a] = b
    return find

def assembly_placements(rd):
    """Resolve NEXT_ASSEMBLY_USAGE_OCCURRENCE (NAUO) placements into a flat list
    of (shape_representation_ref, name, matrix), one per placed part or
    subassembly, with matrices composed down the assembly tree.

    The chain an AP214 export writes is

        CONTEXT_DEPENDENT_SHAPE_REPRESENTATION( #rr, #pds )
        #rr  =( REPRESENTATION_RELATIONSHIP( '', '', #rep_1, #rep_2 )
                REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION( #idt )
                SHAPE_REPRESENTATION_RELATIONSHIP( ) )
        #pds =  PRODUCT_DEFINITION_SHAPE( '', '', #nauo )
        #nauo = NEXT_ASSEMBLY_USAGE_OCCURRENCE( id, '', '', #parent_pd, #child_pd, $ )

    Writers disagree on which of rep_1 and rep_2 is the parent's: Open CASCADE
    writes the child's as rep_1, as the CAx-IF recommends, and SolidWorks the
    parent's. The NAUO settles it: SHAPE_DEFINITION_REPRESENTATION names each
    product definition's representation. A relationship that matches neither
    way is read the CAx-IF way.

    An occurrence is placed once, by the first CONTEXT_DEPENDENT_SHAPE_REPRESENTATION
    that names it, and a root's own bodies once, however many of its
    SHAPE_DEFINITION_REPRESENTATIONs reach them.

    Returns [] when the file has no assembly structure, which is the single-part
    case and leaves the caller's flat path untouched."""
    reps = _representations_by_definition(rd)
    space = _same_space(rd)
    edges = []          # (parent_pd, child_pd, child_rep, matrix, occurrence name)
    seen = set()        # occurrences placed already: one placement each
    for cd in rd.find("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION"):
        a = rd.args(("ref", cd))
        rr = a[0] if a and isinstance(a[0], tuple) else None
        pds = a[1] if len(a) > 1 and isinstance(a[1], tuple) else None
        if rr is None or pds is None:
            continue
        parts = _complex_parts(rd, rr)
        rel = parts.get("REPRESENTATION_RELATIONSHIP")
        wt = parts.get("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION")
        if not rel or not wt:
            continue
        srs = [x for x in rel if isinstance(x, tuple) and x[0] == "ref"]
        nauo = next((x for x in rd.args(pds)
                     if isinstance(x, tuple) and x[0] == "ref"
                     and rd.typ(x) == "NEXT_ASSEMBLY_USAGE_OCCURRENCE"), None)
        pdrefs = [x for x in rd.args(nauo) if isinstance(x, tuple) and x[0] == "ref"] \
            if nauo is not None else []
        if len(srs) < 2 or len(pdrefs) < 2 or nauo[1] in seen:
            continue
        seen.add(nauo[1])
        parent_pd, child_pd = pdrefs[0][1], pdrefs[1][1]
        rep1, rep2 = space(srs[0][1]), space(srs[1][1])
        child_reps = {space(r) for r in reps.get(child_pd, [])}
        parent_reps = {space(r) for r in reps.get(parent_pd, [])}
        rep1_is_parent = rep1 not in child_reps and (rep2 in child_reps or rep1 in parent_reps)
        idt = next((x for x in wt if isinstance(x, tuple) and x[0] == "ref"), None)
        M = _idt_matrix(rd, idt, rep1_is_parent) if idt is not None else None
        if M is None:
            print(f"  warning: CONTEXT_DEPENDENT_SHAPE_REPRESENTATION #{cd} places "
                  f"its part with no ITEM_DEFINED_TRANSFORMATION of two "
                  f"AXIS2_PLACEMENT_3Ds; the part is left unmoved")
            M = _IDENTITY
        child_rep = srs[0][1] if not rep1_is_parent else srs[1][1]
        edges.append((parent_pd, child_pd, child_rep, M, _product_name(rd, pdrefs[1])))
    if not edges:
        return []

    children = {}
    for parent, child, rep, M, name in edges:
        children.setdefault(parent, []).append((child, rep, M, name))
    all_children = {c for _, c, _, _, _ in edges}
    roots = [p for p in children if p not in all_children]

    out = []
    def walk(pd, M, prefix, chain):
        for child, rep, Mc, name in children.get(pd, []):
            if child in chain:          # a cyclic file; stop rather than recurse forever
                continue
            label = name or f"part{len(out)}"
            path = f"{prefix}___{label}" if prefix else label
            Mw = _mat_mul(M, Mc)
            out.append((rep, path, Mw))
            walk(child, Mw, path, chain | {child})
    bodies_of = solids_by_representation(rd)
    for r in roots:
        # A root's own bodies, once, however its SHAPE_DEFINITION_REPRESENTATIONs
        # reach them. Largest first, a representation is dropped when the kept
        # ones already hold all its bodies, and a body-less one when a kept one
        # shares its coordinate space. The kept ones stay in file order.
        cands = reps.get(r, [])
        keep, held = [], set()
        for rep in sorted(cands, key=lambda x: -len(bodies_of.get(x, ()))):   # largest first
            bodies = set(bodies_of.get(rep, ()))
            if bodies and bodies <= held:
                continue
            if not bodies and any(space(k) == space(rep) for k in keep):
                continue
            held |= bodies
            keep.append(rep)
        out += [(rep, _product_name(rd, ("ref", r)), _IDENTITY) for rep in cands if rep in keep]
        walk(r, _IDENTITY, "", {r})
    return out

def _product_name(rd, ref, raw=False):
    """The readable name behind a PRODUCT_DEFINITION, via PRODUCT_DEFINITION_FORMATION:
    the PRODUCT's name, else its id, as a prim name (raw: as the file writes it)."""
    seen = set()
    stack = [ref]
    while stack:
        r = stack.pop()
        if not isinstance(r, tuple) or r[1] in seen:
            continue
        seen.add(r[1])
        t = rd.typ(r)
        if t == "PRODUCT":
            a = rd.args(r)
            for nm in (a[1] if len(a) > 1 else "", a[0] if a else ""):  # name, then id
                if isinstance(nm, str) and _prim_name(nm):
                    return nm.strip() if raw else _prim_name(nm)
        for x in rd.args(r):
            if isinstance(x, tuple) and x[0] == "ref":
                stack.append(x)
    return ""

def _prim_name(nm):
    """A USD prim name from a STEP name: ASCII letters, digits and underscores,
    never a leading digit; '' when nothing is left."""
    nm = re.sub(r"[^A-Za-z0-9_]+", "_", nm if isinstance(nm, str) else "").strip("_")
    return "_" + nm if nm[:1].isdigit() else nm

def _unique_name(nm, used):
    """nm, or nm_1, nm_2, ... the first that is not in the set used; adds it."""
    out, k = nm, 0
    while out in used:
        k += 1
        out = f"{nm}_{k}"
    used.add(out)
    return out

def product_names(rd):
    """{solid_ref: name} for every solid that is the only solid of a part, named
    after the part's PRODUCT. SHAPE_DEFINITION_REPRESENTATION ties a part's
    PRODUCT_DEFINITION_SHAPE to the shape representation that holds its solids
    (solids_by_representation). A part with several solids names none of them:
    one product name cannot tell them apart, and they keep their own."""
    srmap = solids_by_representation(rd)
    out = {}
    for sdr in rd.find("SHAPE_DEFINITION_REPRESENTATION"):
        a = rd.args(("ref", sdr))
        if len(a) < 2 or not (isinstance(a[0], tuple) and isinstance(a[1], tuple)):
            continue
        solids = srmap.get(a[1][1], [])
        pds = rd.args(a[0])
        definition = pds[2] if len(pds) > 2 else None
        if len(solids) != 1 or not isinstance(definition, tuple) \
                or rd.typ(definition) == "NEXT_ASSEMBLY_USAGE_OCCURRENCE":
            continue
        nm = _product_name(rd, definition)
        if nm:
            out.setdefault(solids[0], nm)
    return out

def body_products(rd):
    """{body_ref: (product, bodies)} for every body a part's shape holds: the
    PRODUCT name (else id) as the file writes it, through the same
    SHAPE_DEFINITION_REPRESENTATIONs as product_names, and how many bodies
    that part holds."""
    srmap = solids_by_representation(rd)
    out = {}
    for sdr in rd.find("SHAPE_DEFINITION_REPRESENTATION"):
        a = rd.args(("ref", sdr))
        if len(a) < 2 or not (isinstance(a[0], tuple) and isinstance(a[1], tuple)):
            continue
        pds = rd.args(a[0])
        definition = pds[2] if len(pds) > 2 else None
        if not isinstance(definition, tuple) \
                or rd.typ(definition) == "NEXT_ASSEMBLY_USAGE_OCCURRENCE":
            continue
        label = _product_name(rd, definition, raw=True)
        bodies = srmap.get(a[1][1], []) if label else []
        for b in bodies:
            out.setdefault(b, (label, len(bodies)))
    return out

def solids_by_representation(rd):
    """{shape_representation_ref: [body_ref, ...]}: the solids and sheets each
    representation holds, pooled over every representation a plain
    SHAPE_REPRESENTATION_RELATIONSHIP joins to it (_same_space). A part's
    bodies hang off an ADVANCED_BREP_SHAPE_REPRESENTATION or a
    MANIFOLD_SURFACE_SHAPE_REPRESENTATION, while the assembly graph names the
    part's plain SHAPE_REPRESENTATION; a part may join several. The bodies keep
    file order, so the result does not depend on the order of the
    relationships."""
    own = {}
    for sr in rd.find(*_BODY_REPRESENTATIONS):
        items = []
        for x in rd.args(("ref", sr)):
            if isinstance(x, tuple) and x[0] == "ref":
                items.append(x[1])
            elif isinstance(x, list):
                items += [y[1] for y in x
                          if isinstance(y, tuple) and y[0] == "ref"]
        sol = [i for i in items if rd.typ(("ref", i)) in _BODY_TYPES]
        if sol:
            own[sr] = sol
    space = _same_space(rd)
    pooled = {}
    for sr in sorted(own):
        cls = pooled.setdefault(space(sr), [])
        cls += [b for b in own[sr] if b not in cls]
    reps = set(own)
    for rel in rd.find("SHAPE_REPRESENTATION_RELATIONSHIP"):
        reps.update(x[1] for x in rd.args(("ref", rel)) if isinstance(x, tuple) and x[0] == "ref")
    return {r: pooled[space(r)] for r in reps if pooled.get(space(r))}

def solid_name(rd, solid_ref, i, products=None):
    """The prim name of solid i: its part's PRODUCT name when it is the part's
    only solid (product_names), else the solid's own STEP name, else body_<i>."""
    nm = (products or {}).get(solid_ref) or _prim_name(rd.args(("ref", solid_ref))[0])
    return nm if nm else f"body_{i}"

def resolve_colors(rd, ents, solid_refs):
    """Per-solid body color and per-face color overrides, read from STEP styled
    items. A body-level style (the styled item targets the solid) wins outright;
    otherwise the most common face color is the body color. OVER_RIDING styles beat
    plain ones at the same target. Returns (body_colors_by_solid_index,
    {face_ref: color})."""
    sidx = {s: k for k, s in enumerate(solid_refs)}
    face_to_solid = {}
    for s in solid_refs:
        sa = ents[s][1]
        shells = []
        for x in sa[1:]:
            if isinstance(x, tuple) and x[0] == "ref":
                shells.append(x[1])
            elif isinstance(x, list):
                shells += [y[1] for y in x if isinstance(y, tuple) and y[0] == "ref"]
        for sh in shells:
            t = ents[sh][0]
            if t in ("ORIENTED_CLOSED_SHELL", "ORIENTED_OPEN_SHELL"):
                sh = ents[sh][1][2][1]
            elif t not in ("CLOSED_SHELL", "OPEN_SHELL"):
                continue
            for fref in ents[sh][1][1]:
                face_to_solid[fref[1]] = sidx[s]
    _PREDEF = {"black": (0.0, 0.0, 0.0), "white": (1.0, 1.0, 1.0),
               "red": (1.0, 0.0, 0.0), "green": (0.0, 1.0, 0.0),
               "blue": (0.0, 0.0, 1.0), "yellow": (1.0, 1.0, 0.0),
               "magenta": (1.0, 0.0, 1.0), "cyan": (0.0, 1.0, 1.0)}
    def dig_colour(node, depth=0):
        if depth > 12: return None
        if isinstance(node, tuple) and node[0] == "ref":
            e = ents.get(node[1])
            if not e: return None
            if e[0] == "COLOUR_RGB":
                return (round(float(e[1][1]), 4), round(float(e[1][2]), 4), round(float(e[1][3]), 4))
            if e[0] in ("DRAUGHTING_PRE_DEFINED_COLOUR", "PRE_DEFINED_COLOUR"):
                nm = (e[1][0] or "").lower() if e[1] else ""
                return _PREDEF.get(nm)
            for x in e[1]:
                c = dig_colour(x, depth + 1)
                if c: return c
        elif isinstance(node, list):
            for x in node:
                c = dig_colour(x, depth + 1)
                if c: return c
        return None
    body_level = {}
    tally = {}
    face_col = {}
    for i, (t, a) in ents.items():
        if t not in ("STYLED_ITEM", "OVER_RIDING_STYLED_ITEM"): continue
        item = a[2] if len(a) > 2 else None
        if not (isinstance(item, tuple) and item[0] == "ref"): continue
        tgt = item[1]
        col = dig_colour(a[1])
        if col is None: continue
        if tgt in sidx:
            si = sidx[tgt]
            is_ov = (t == "OVER_RIDING_STYLED_ITEM")
            prev = body_level.get(si)
            if prev is None or (is_ov and not prev[0]):
                body_level[si] = (is_ov, col)
        si = face_to_solid.get(tgt)
        if si is not None:
            is_ov = (t == "OVER_RIDING_STYLED_ITEM")
            prevf = face_col.get(tgt)
            if prevf is None or (is_ov and not prevf[0]):
                face_col[tgt] = (is_ov, col)
            tally.setdefault(si, Counter())[col] += 1
    out = {}
    for si in range(len(solid_refs)):
        if si in body_level:
            out[si] = body_level[si][1]
        elif si in tally:
            out[si] = tally[si].most_common(1)[0][0]
    return out, {ref: c for ref, (_ov, c) in face_col.items()}

def face_refs_for_solid(rd, solid):
    """ADVANCED_FACE refs of one solid in extract_brep's exact face order, so a
    per-face color list lines up with the authored faces."""
    sa = rd.args(("ref", solid))
    shell_refs = []
    for x in sa[1:]:
        if isinstance(x, tuple) and x[0] == "ref" and rd.typ(x) in _SHELL_TYPES:
            shell_refs.append(x)
        elif isinstance(x, list):
            shell_refs += [y for y in x if isinstance(y, tuple) and y[0] == "ref"]
    refs = []
    for sh in shell_refs:
        refs += list(_resolve_shell_faces(rd, sh))
    return refs

# ================================================================ convert + CLI
def _usd_matrix(M):
    """A frame (x, y, z, origin) as a USD row-vector matrix4d."""
    x, y, z, o = M
    return Gf.Matrix4d(x[0], x[1], x[2], 0.0,
                       y[0], y[1], y[2], 0.0,
                       z[0], z[1], z[2], 0.0,
                       o[0], o[1], o[2], 1.0)

def _emit_assembly(stage, rd, cfg, placed, srmap, colors, face_col, solids, verbose, used,
                   products=None):
    """One Xform per placement, carrying the composed transform unless it is
    the identity, with that part's bodies as BrepArray children: ``brep`` for a
    part with one body, ``brep_<j>`` for several. convert passes every body no
    assembly placement reaches as an identity placement of its own.

    Geometry stays in the part's own coordinate system, where the STEP authored
    it; the placement is the only thing that moves. Each placement authors its
    own copy of the part's BrepArrays. The Xform takes the bodies' colour when
    they share one; otherwise each BrepArray takes its own. ``products``
    (body_products) records, on a lone body that is one of several of its
    part, the part it belongs to (customData stepToUsdSolid:product), since
    its prim cannot be named after it."""
    sidx = {sref: k for k, sref in enumerate(solids)}
    for sr, nm, M in placed:
        name = _unique_name(_prim_name(nm) or f"part_{sr}", used)
        xf = UsdGeom.Xform.Define(stage, f"/World/{name}")
        if not _is_identity(M):
            xf.AddTransformOp().Set(_usd_matrix(M))
        part_solids = srmap[sr]
        body_cols = [colors.get(sidx.get(sref, -1)) for sref in part_solids]
        common = body_cols[0] if all(c == body_cols[0] for c in body_cols) else None
        if common:
            cpv = UsdGeom.PrimvarsAPI(xf.GetPrim()).CreatePrimvar(
                "displayColor", Sdf.ValueTypeNames.Color3fArray, UsdGeom.Tokens.constant)
            cpv.Set(Vt.Vec3fArray([Gf.Vec3f(*common)]))
        label, nbodies = (products or {}).get(part_solids[0], (None, 0)) \
            if len(part_solids) == 1 else (None, 0)
        if label and nbodies > 1:
            xf.GetPrim().SetCustomDataByKey("stepToUsdSolid:product", label)
        for j, sref in enumerate(part_solids):
            b = extract_brep(rd, cfg, [sref])
            body = body_cols[j]
            fcolors = None
            frefs = face_refs_for_solid(rd, sref)
            if len(frefs) == len(b["faces"]):
                base = body or (0.6, 0.6, 0.6)
                fcolors = [face_col.get(fr[1], base) for fr in frefs]
            child = "brep" if len(part_solids) == 1 else f"brep_{j}"
            path = f"/World/{name}/{child}"
            author_brep(stage, path, b, cfg, face_colors=fcolors)
            if body and common is None:
                cpv = UsdGeom.PrimvarsAPI(stage.GetPrimAtPath(path)).CreatePrimvar(
                    "displayColor", Sdf.ValueTypeNames.Color3fArray, UsdGeom.Tokens.constant)
                if not cpv.HasAuthoredValue():
                    cpv.Set(Vt.Vec3fArray([Gf.Vec3f(*body)]))
            if verbose:
                errs = self_check(b)
                cav = b.get("cavities") or []
                nrule = sum(c["byRule"] for c in cav)
                ncav = nrule + sum(c["byVolume"] for c in cav)
                print(f"  {name + '/' + child:32} faces={len(b['faces']):5} "
                      f"verts={len(b['verts']):6} "
                      f"selfcheck={'OK' if not errs else str(len(errs)) + 'err'}"
                      + (f" cavities={ncav}" if ncav else "")
                      + (f" ({nrule} read by ISO 10303-42's rule)" if nrule else ""))

def convert(inp, out, up_axis="Z", meters_per_unit=None, verbose=True):
    """Convert one STEP file to a UsdSolid stage: one Xform + BrepArray prim per
    body, solids and sheets (SHELL_BASED_SURFACE_MODEL) alike, or for an
    assembly one Xform per placement with its part's bodies as BrepArray
    children, under a /World Xform (_emit_assembly writes both). A body's Xform
    is named after its part's PRODUCT when the part has that one body
    (solid_name). Output format follows the extension (.usda text or .usdc
    binary crate). metersPerUnit is ``meters_per_unit``, else the file's length
    unit (detect_meters_per_unit), else 0.001; the tolerances follow it."""
    with open(inp, errors="replace") as f:
        ents = parse_step(f.read())
    rd = Reader(ents)
    angle_scale = detect_angle_scale(rd, ents)
    # Solids and sheets alike, in file order.
    solids = rd.find(*_BODY_TYPES)
    if not solids:
        raise SystemExit(f"{inp}: no MANIFOLD_SOLID_BREP / BREP_WITH_VOIDS solids "
                         "or SHELL_BASED_SURFACE_MODEL sheets found")
    if meters_per_unit is None:
        meters_per_unit = detect_meters_per_unit(rd, ents, solids) or 0.001
    tol, unc, usrc = derive_tolerance(rd, ents, meters_per_unit)
    cfg = Config(angle_scale=angle_scale, intersect_tol=tol, meters_per_unit=meters_per_unit,
                 accuracy=unc if usrc.startswith("UNCERTAINTY") else None)
    colors, face_col = resolve_colors(rd, ents, solids)
    if verbose:
        unit = "degrees" if abs(angle_scale - math.pi / 180) < PERIOD_TOL else "radians"
        nsheet = sum(1 for x in solids if rd.typ(("ref", x)) == "SHELL_BASED_SURFACE_MODEL")
        print(f"[{inp}] {len(ents)} entities, {len(solids) - nsheet} solid(s), "
              f"{nsheet} sheet(s); plane-angle unit = {unit}; "
              f"metersPerUnit = {meters_per_unit:g}; "
              f"intersectTol3d = {tol:g} model units ({usrc})")

    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z if up_axis.upper() == "Z" else UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, meters_per_unit)
    world = UsdGeom.Xform.Define(stage, "/World")
    stage.SetDefaultPrim(world.GetPrim())

    placements = assembly_placements(rd)
    srmap = solids_by_representation(rd)
    placed = [(sr, nm, M) for sr, nm, M in placements if srmap.get(sr)]
    nplaced = len(placed)
    if placed and verbose:
        uniq = len({sr for sr, _, _ in placed})
        print(f"  assembly: {len(placed)} placements of {uniq} unique part(s), "
              f"{sum(len(srmap[sr]) for sr, _, _ in placed)} bodies")
    # A body no placement reaches stays where its own representation puts it:
    # an identity placement of its own, named by solid_name.
    placed_solids = {s for sr, _, _ in placed for s in srmap[sr]}
    names = product_names(rd)
    loose = [(i, s) for i, s in enumerate(solids) if s not in placed_solids]
    if placed and loose and verbose:
        print(f"  {len(loose)} body(ies) outside the assembly, left unplaced")
    for i, s in loose:
        srmap[("loose", s)] = [s]
        placed.append((("loose", s), solid_name(rd, s, i, names), _IDENTITY))
    _emit_assembly(stage, rd, cfg, placed, srmap, colors, face_col, solids, verbose,
                   set(), products=body_products(rd))

    stage.Export(out)
    if verbose:
        nbreps = sum(len(srmap[sr]) for sr, _, _ in placed)
        print(f"wrote {out} ({nbreps} brep prim(s)"
              + (f", {nplaced} placement(s))" if nplaced else ")"))

def main():
    import argparse
    ap = argparse.ArgumentParser(
        description="Convert a STEP (ISO 10303-21/-42) file to a UsdSolid B-rep stage.")
    ap.add_argument("input", help="input STEP file (.stp / .step)")
    ap.add_argument("output", help="output USD file (.usd / .usda / .usdc)")
    ap.add_argument("--up-axis", choices=["Y", "Z"], default="Z",
                    help="stage up axis (default Z)")
    ap.add_argument("--meters-per-unit", type=float, default=None,
                    help="stage metersPerUnit (default: the file's length unit, "
                         "else 0.001, i.e. millimetres)")
    ap.add_argument("-q", "--quiet", action="store_true", help="suppress per-solid output")
    args = ap.parse_args()
    convert(args.input, args.output, args.up_axis, args.meters_per_unit, verbose=not args.quiet)

if __name__ == "__main__":
    main()
