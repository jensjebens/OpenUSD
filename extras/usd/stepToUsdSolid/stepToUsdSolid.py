#!/usr/bin/env python
# SPDX-License-Identifier: Apache-2.0
"""
stepToUsdSolid -- convert a STEP (ISO 10303-21/-42) file to a UsdSolid B-rep stage.

A reference importer for the UsdSolid schema: it reads a STEP part or assembly and
writes one Xform + BrepArray prim per solid, authored through the pxr UsdSolid
schema API. It is pure Python plus USD -- no CAD kernel (no OpenCASCADE, no SMLib)
in the loop -- so it doubles as a self-contained way to produce B-rep test data for
the schema, the validator, and the tessellator.

    stepToUsdSolid input.stp output.usdc

The converter reads whatever the STEP file contains and does the right thing with
it, with no modes to choose. It handles planar/cylindrical/conical/spherical/
toroidal analytic surfaces and NURBS surfaces and curves; lowers swept surfaces
(linear extrusion, revolution) to NURBS; resolves void shells (BREP_WITH_VOIDS /
ORIENTED_CLOSED_SHELL) and vertex loops; derives the parametric UV window of each
face from its trimming edges; and reads per-body / per-face colors from STEP styled
items. The plane-angle unit and the tolerance are read from the file.

Scope: it maps each STEP solid to a top-level prim in world coordinates (flat
multi-body). Assembly instancing (NAUO/CDSR placement transforms) is not handled.
This is a reference/sample importer, like the gsplat ply-to-usd sample -- not a
production STEP exporter.
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
# make every endpoint check fail.
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
    shells never resolves one, and so on). The only things that legitimately vary
    from file to file are the plane-angle unit and the tolerance ladder, and both
    are derived from the file (see detect_angle_scale / derive_tolerance).
    """
    def __init__(self, angle_scale=1.0, intersect_tol=1e-6):
        self.angle_scale = angle_scale        # raw STEP plane-angle -> radians
        self.intersect_tol = intersect_tol    # brep:intersectTol3d
        self.edge_degen_tol = intersect_tol   # degenerate-edge stub tolerance
        self.inv_accept = max(1e-3, intersect_tol * 10.0)  # NURBS endpoint weld
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

def detect_length_uncertainty(rd, ents):
    """File's declared length uncertainty in model units (mm), the producer's real
    endpoint-agreement tolerance. Returns (value, source_str) or (None,'absent')."""
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
    prof = dict(order=basis["order"], knots=basis["knots"],
                controlVertices=basis["poles"], weights=basis["weights"], nurb=True)
    return dict(nurb=True, uOrder=basis["order"], vOrder=2, uVertexCount=nU, vVertexCount=2,
                uKnots=basis["knots"], vKnots=[vlo, vlo, vhi, vhi],
                controlVertices=cps, weights=wts,
                _uspan=(basis["plo"], basis["phi"]), _vspan=(vlo, vhi),
                _swept="ext", _prof=prof, _dir=dirv,
                _porigin=basis["poles"][0], _profdom=(basis["plo"], basis["phi"]))

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
    prof = dict(order=basis["order"], knots=basis["knots"],
                controlVertices=basis["poles"], weights=basis["weights"], nurb=True)
    return dict(nurb=True, uOrder=3, vOrder=basis["order"], uVertexCount=nbU, vVertexCount=nbV,
                uKnots=uK, vKnots=basis["knots"], controlVertices=cps, weights=wts,
                _uspan=(a0, a1), _vspan=(basis["plo"], basis["phi"]),
                _swept="rev", _prof=prof, _axp=axp, _axd=axd,
                _uarc=(a0, a1), _profdom=(basis["plo"], basis["phi"]))

# ================================================================ geometry extraction
def surface_geom(rd, ref, cfg, fverts=None):
    """(token, dict-of-arrays) for a face surface. cfg selects cone unit-context
    and swept-surface lowering. fverts (face boundary vertices) is required for
    the swept-surface param bounds (no PCURVE in these files)."""
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
        vlo, vhi = 0.0, 1.0
        vec = rd.args(a[2])
        d = vnorm(rd.direction(vec[1]))
        mag = float(vec[2])
        if fverts:
            hs = [vdot(v, d) for v in fverts]
            base = _lower_curve_to_nurb_local(rd, a[1])
            if base and base["poles"]:
                b0 = vdot(base["poles"][0], d)
                vlo = (min(hs) - b0)/mag
                vhi = (max(hs) - b0)/mag
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
    """The flat (natural-period) face:range from step_to_usdsolid_fixed."""
    if stok == "BrepSurfacePlaneAPI" and fverts:
        o, u = sg["origin"], sg["refDirection"]
        v = vcross(sg["axis"], sg["refDirection"])
        us = [vdot(vsub(p, o), u) for p in fverts]
        vs = [vdot(vsub(p, o), v) for p in fverts]
        return (_bump(min(us), max(us)), _bump(min(vs), max(vs)))
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

def _invert_profile_coarse(prof, target, nscan=48):
    order = prof["order"]
    knots = prof["knots"]
    cps = prof["controlVertices"]
    wts = prof["weights"]
    p = order - 1
    lo, hi = knots[p], knots[len(cps)]
    if not (hi > lo): return lo
    best = 1e30
    bt = lo
    for i in range(nscan + 1):
        t = lo + (hi - lo) * i / nscan
        pt, _ = _deboor_rational(order, knots, cps, wts, t)
        dd = (pt[0]-target[0])**2 + (pt[1]-target[1])**2 + (pt[2]-target[2])**2
        if dd < best:
            best = dd
            bt = t
    return bt

def _swept_face_range(sg, fverts, loop_pts=None):
    if not fverts or len(fverts) < 2: return None
    uo, vo = sg["uOrder"], sg["vOrder"]
    nU, nV = sg["uVertexCount"], sg["vVertexCount"]
    uK, vK = sg["uKnots"], sg["vKnots"]
    udom = (uK[uo-1], uK[nU])
    vdom = (vK[vo-1], vK[nV])
    du = udom[1]-udom[0]
    dv = vdom[1]-vdom[0]
    if not (du > 0 and dv > 0): return None
    prof = sg["_prof"]
    kind = sg["_swept"]
    us = []
    vs = []
    if kind == "ext":
        d = sg["_dir"]
        dmag2 = sum(c*c for c in d)
        if dmag2 <= 0: return None
        o = sg["_porigin"]
        for p in fverts:
            disp = vsub(p, o)
            fv = vdot(disp, d) / dmag2
            vs.append(fv)
            foot = tuple(p[k] - fv*d[k] for k in range(3))
            us.append(_invert_profile_coarse(prof, foot))
    else:
        axp = sg["_axp"]
        axd = sg["_axd"]
        a0, a1 = sg["_uarc"]
        def rot(vec, angle):
            c, s = math.cos(angle), math.sin(angle)
            cx = vcross(axd, vec)
            return tuple(c*vec[k] + s*cx[k] for k in range(3))
        mag = lambda w: math.sqrt(vdot(w, w))
        p0 = prof["controlVertices"][0]
        t0 = vdot(vsub(p0, axp), axd)
        rad0 = vsub(p0, tuple(axp[k]+t0*axd[k] for k in range(3)))
        if mag(rad0) < DEGENERATE_TOL:
            for pp in prof["controlVertices"]:
                tt = vdot(vsub(pp, axp), axd)
                rr = vsub(pp, tuple(axp[k]+tt*axd[k] for k in range(3)))
                if mag(rr) > DEGENERATE_TOL:
                    rad0 = rr
                    break
        xref = vnorm(rad0)
        yref = vcross(axd, xref)
        for p in fverts:
            foot_t = vdot(vsub(p, axp), axd)
            radial = vsub(p, tuple(axp[k]+foot_t*axd[k] for k in range(3)))
            ang = math.atan2(vdot(radial, yref), vdot(radial, xref))
            us.append(ang)
            back = tuple(axp[k] + foot_t*axd[k] + rot(radial, -ang)[k] for k in range(3))
            vs.append(_invert_profile_coarse(prof, back))
    if kind == "rev":
        ulo, uhi = _wrap_span(us)
        a0, a1 = sg["_uarc"]
        def uangf(p):
            foot_t = vdot(vsub(p, axp), axd)
            radial = vsub(p, tuple(axp[k] + foot_t*axd[k] for k in range(3)))
            return math.atan2(vdot(radial, yref), vdot(radial, xref))
        # the lowered surface has a HARD u domain (its knot vector): a footprint
        # that wraps past a domain end means the face's UV image is two disjoint
        # strips at the domain seam, and clipping to one of them silently drops
        # the other, so promote to the full domain instead.
        seam_cross = (uhi > udom[1] + DEGENERATE_TOL) or (ulo < udom[0] - DEGENERATE_TOL)
        full = (seam_cross or _wraps_period(loop_pts, uangf)
                or (uhi - ulo >= (a1 - a0) - PERIOD_TOL) or _full_period(ulo, uhi))
        if full: ulo, uhi = udom
        else:
            pu = du * PAD_FRAC + PAD_MIN
            ulo = max(udom[0], ulo - pu)
            uhi = min(udom[1], uhi + pu)
        pv = max(dv * PAD_FRAC, dv / 48.0) + PAD_MIN
    else:
        pu = max(du * PAD_FRAC, du / 48.0) + PAD_MIN
        ulo = max(udom[0], min(us) - pu)
        uhi = min(udom[1], max(us) + pu)
        pv = dv * PAD_FRAC + PAD_MIN
    vlo = max(vdom[0], min(vs) - pv)
    vhi = min(vdom[1], max(vs) + pv)
    if not (uhi > ulo and vhi > vlo): return None
    if (uhi - ulo) >= 0.98*du and (vhi - vlo) >= 0.98*dv: return None
    return ((ulo, uhi), (vlo, vhi))

def _eval_nurb_surface(sg, u, v):
    uo, vo = sg["uOrder"], sg["vOrder"]
    nU, nV = sg["uVertexCount"], sg["vVertexCount"]
    uK, vK = sg["uKnots"], sg["vKnots"]
    cps = sg["controlVertices"]
    wts = sg["weights"]
    def basis(kn, order, n, t):
        p = order - 1
        lo, hi = kn[p], kn[n]
        if t < lo: t = lo
        if t > hi: t = hi
        if t >= kn[n]: span = n - 1
        else:
            span = p
            while span < n - 1 and kn[span + 1] <= t: span += 1
        N = [0.0]*(p + 1)
        N[0] = 1.0
        left = [0.0]*(p + 1)
        right = [0.0]*(p + 1)
        for j in range(1, p + 1):
            left[j] = t - kn[span + 1 - j]
            right[j] = kn[span + j] - t
            saved = 0.0
            for r in range(j):
                denom = right[r + 1] + left[j - r]
                temp = N[r] / denom if denom != 0 else 0.0
                N[r] = saved + right[r + 1] * temp
                saved = left[j - r] * temp
            N[j] = saved
        return span, N
    us, Nu = basis(uK, uo, nU, u)
    vs, Nv = basis(vK, vo, nV, v)
    pu = uo - 1
    pv = vo - 1
    x = y = z = w = 0.0
    for i in range(pu + 1):
        ui = us - pu + i
        for j in range(pv + 1):
            vj = vs - pv + j
            idx = ui * nV + vj
            b = Nu[i] * Nv[j] * wts[idx]
            cp = cps[idx]
            x += b*cp[0]
            y += b*cp[1]
            z += b*cp[2]
            w += b
    if w == 0: w = 1.0
    return (x/w, y/w, z/w)

def _nurb_face_range(sg, fverts, coarse=10):
    uo, vo = sg["uOrder"], sg["vOrder"]
    nU, nV = sg["uVertexCount"], sg["vVertexCount"]
    uK, vK = sg["uKnots"], sg["vKnots"]
    ulo, uhi = uK[uo - 1], uK[nU]
    vlo, vhi = vK[vo - 1], vK[nV]
    if not (uhi > ulo and vhi > vlo): return None
    nu = max(coarse, uo + 1)
    nv = max(coarse, vo + 1)
    grid = []
    for iu in range(nu + 1):
        gu = ulo + (uhi - ulo) * iu / nu
        row = []
        for iv in range(nv + 1):
            gv = vlo + (vhi - vlo) * iv / nv
            row.append((gu, gv, _eval_nurb_surface(sg, gu, gv)))
        grid.append(row)
    us = []
    vs = []
    for p in fverts:
        best = 1e30
        bu = ulo
        bv = vlo
        for row in grid:
            for (gu, gv, gp) in row:
                dd = (gp[0]-p[0])**2 + (gp[1]-p[1])**2 + (gp[2]-p[2])**2
                if dd < best:
                    best = dd
                    bu = gu
                    bv = gv
        us.append(bu)
        vs.append(bv)
    if not us: return None
    du = (uhi - ulo)
    dv = (vhi - vlo)
    pu = du / nu
    pv = dv / nv
    rulo = max(ulo, min(us) - pu)
    ruhi = min(uhi, max(us) + pu)
    rvlo = max(vlo, min(vs) - pv)
    rvhi = min(vhi, max(vs) + pv)
    if not (ruhi > rulo and rvhi > rvlo): return None
    if (ruhi - rulo) >= 0.98 * du and (rvhi - rvlo) >= 0.98 * dv: return None
    return ((rulo, ruhi), (rvlo, rvhi))

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

def face_range(stok, sg, fverts, loop_pts=None, sense=True, nverts=0):
    """The face's UV window (face:range), taken from the boundary's actual UV
    footprint. loop_pts: ordered per-loop boundary sample chains, used for
    periodic-direction winding promotion and sphere pole containment; sense is
    the ADVANCED_FACE same_sense flag; nverts = how many leading entries of
    fverts are true boundary vertices (kept through sample capping). With no
    boundary samples there is nothing to project, so fall back to the natural
    period."""
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
    if sg.get("nurb"):
        pts = _cap_face_samples(fverts, nverts)
        if sg.get("_swept"):
            r = _swept_face_range(sg, pts, loop_pts)
            if r is not None: return r
        r = _nurb_face_range(sg, pts)
        if r is not None: return r
        return _base_face_range(stok, sg, fverts)
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

def _cap_samples(pts, cap=400):
    """Uniform subsample so the NURBS grid-search / profile-inversion paths stay
    bounded on faces with hundreds of boundary edges."""
    if len(pts) <= cap:
        return pts
    step = (len(pts) - 1) / (cap - 1)
    return [pts[int(round(i * step))] for i in range(cap)]

def _cap_face_samples(pts, nverts, cap=400):
    """Cap for the NURBS paths, ALWAYS retaining the boundary vertices (the head
    of the sample list). Uniform subsampling over the whole list can drop the
    very vertex that pins a UV extreme: a corner vertex at v = 1.0 falling out
    of the cap shrinks the authored window to 0.9."""
    if len(pts) <= cap:
        return pts
    nv = min(max(nverts, 0), cap // 2)
    return pts[:nv] + _cap_samples(pts[nv:], cap - nv)

# ================================================================ shell resolution
def _resolve_shell_faces(rd, sh_ref):
    """Return the ADVANCED_FACE refs for a shell, unwrapping an
    ORIENTED_CLOSED_SHELL (a void boundary) to its base CLOSED_SHELL."""
    if rd.typ(sh_ref) == "ORIENTED_CLOSED_SHELL":
        base = rd.args(sh_ref)[2]
        return rd.args(base)[1]
    return rd.args(sh_ref)[1]

# ================================================================ topology + geometry
def extract_brep(rd, cfg, solid_refs=None):
    """Read one or more STEP solids into the radial-edge topology + geometry the
    UsdSolid BrepArray schema stores. If solid_refs is None, discovers all
    top-level solids; pass a single ref to extract one body of an assembly. cfg
    carries the per-file plane-angle scale and tolerances."""
    if solid_refs is None:
        solid_refs = rd.find("MANIFOLD_SOLID_BREP", "BREP_WITH_VOIDS") or rd.find("SHELL_BASED_SURFACE_MODEL")
    if not solid_refs:
        raise SystemExit("no MANIFOLD_SOLID_BREP / shell model found")
    vmap, emap = {}, {}
    verts, edges, edgeuses, loops, faces = [], [], [], [], []
    loop_vidx = []
    brep_faces = []
    esamples = {}   # edge index -> cached interior boundary samples (H2b)

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
            same = (a[4] == ("enum", "T"))
            edgeuses.append(dict(edge=ei, orient="same" if same else "opposite"))
            n += 1
            eis.append(ei)
        return n, eis, vi

    for solid in solid_refs:
        sa = rd.args(("ref", solid))
        shell_refs = []
        shell_types = ("CLOSED_SHELL", "OPEN_SHELL", "ORIENTED_CLOSED_SHELL")
        for x in sa[1:]:
            if isinstance(x, tuple) and x[0] == "ref" and rd.typ(x) in shell_types:
                shell_refs.append(x)
            elif isinstance(x, list):
                shell_refs += [y for y in x if isinstance(y, tuple) and y[0] == "ref"]
        solid_faces = []
        for sh in shell_refs:
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
                    # orientations, and the face same_sense above. The separate
                    # FACE_BOUND/FACE_OUTER_BOUND orientation flag is assumed .T.
                    # across the CAD exporters tested here; a producer that authors
                    # a .F. bound would need it honored, which belongs in the winding
                    # logic (not a mode) if that case ever turns up.
                    ba = rd.args(b)
                    n, eis, vi = walk_loop(ba[1])
                    loop_specs.append((n, vi))
                    lc += 1
                    for ei in eis: fv.update(edges[ei]["v"])
                    floop_edges.append((eis, [eu["orient"] for eu in edgeuses[-n:]] if n else []))
                    if vi: fv.add(vi)
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
                stok, sg = surface_geom(rd, fa[2], cfg, fverts)
                rng = face_range(stok, sg, fsamples, loop_pts=floop_pts,
                                 sense=sense, nverts=len(fverts))
                sg, rng = rebase_periodic_u(stok, sg, rng)
                for n, vi in loop_specs:
                    loops.append(n)
                    loop_vidx.append(vi)
                fi = len(faces)
                faces.append(dict(loopCount=lc, stok=stok, geom=sg, sense=sense,
                                  rng=rng))
                solid_faces.append(fi)
        brep_faces.append(solid_faces)

    by_edge = {}
    for i, eu in enumerate(edgeuses): by_edge.setdefault(eu["edge"], []).append(i)
    for ei, g in by_edge.items():
        for k, idx in enumerate(g):
            edgeuses[idx]["next"] = g[(k+1) % len(g)]
            edgeuses[idx]["entry"] = "topEntry" if k % 2 == 0 else "bottomEntry"

    return dict(verts=verts, edges=edges, edgeuses=edgeuses, loops=loops, faces=faces,
                brep_faces=brep_faces, by_edge=by_edge, loop_vidx=loop_vidx)

# ================================================================ region packing
def pack_regions(b):
    """Void-included radial-edge form (#68/#71): each solid -> one Brep with a
    voidRegion+solidRegion pair; the two faceuses of each face grouped by shell."""
    regionCount, regionType, regionShellCount, shellFaceuseCount = [], [], [], []
    fuFaceIndex, fuOrient = [], []
    for solid_faces in b["brep_faces"]:
        N = len(solid_faces)
        regionCount.append(2)
        regionType += ["voidRegion", "solidRegion"]
        regionShellCount += [1, 1]
        shellFaceuseCount += [N, N]
        for fi in solid_faces:
            fuFaceIndex.append(fi)
            fuOrient.append("same" if b["faces"][fi]["sense"] else "opposite")
        for fi in solid_faces:
            fuFaceIndex.append(fi)
            fuOrient.append("opposite" if b["faces"][fi]["sense"] else "same")
    return dict(regionCount=regionCount, regionType=regionType,
                regionShellCount=regionShellCount, shellFaceuseCount=shellFaceuseCount,
                fuFaceIndex=fuFaceIndex, fuOrient=fuOrient)

# ================================================================ self-check
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
    bad = {ei: len(g) for ei, g in b["by_edge"].items() if len(g) != 2}
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
    mn, mx = local_extent(verts)
    ba.CreateBrepExtentAttr(_v3d([mn, mx]))
    ba.CreateExtentAttr(_v3f([mn, mx]))
    if face_colors and len(face_colors) == nfaces and len(set(face_colors)) > 1:
        pv = UsdGeom.PrimvarsAPI(prim).CreatePrimvar(
            "displayColor", Sdf.ValueTypeNames.Color3fArray, UsdGeom.Tokens.uniform)
        pv.Set(_v3f(face_colors))
    return ba

# ================================================================ per-file setup
def derive_tolerance(rd, ents):
    """The brep:intersectTol3d for a file: the producer's declared endpoint
    agreement (UNCERTAINTY_MEASURE) divided by 20 and floored at MIN_INTERSECT_TOL. When the
    file declares no uncertainty, fall back to 1e-5 x the model bounding-box
    diagonal, then to 1e-3. Returns (tol, uncertainty, source_string)."""
    unc, usrc = detect_length_uncertainty(rd, ents)
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
            unc = 1e-3
            usrc = "fallback default"
    return max(MIN_INTERSECT_TOL, unc / 20.0), unc, usrc

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

def _idt_matrix(rd, idt_ref):
    """ITEM_DEFINED_TRANSFORMATION maps its first placement onto its second, so
    the placement is `to * from^-1`."""
    a = rd.args(idt_ref)
    frames = [x for x in a if isinstance(x, tuple) and x[0] == "ref"
              and rd.typ(x) == "AXIS2_PLACEMENT_3D"]
    if len(frames) < 2:
        return _IDENTITY
    F = _a2p_matrix(rd, frames[0])
    T = _a2p_matrix(rd, frames[1])
    fx, fy, fz, fo = F
    inv_rot = ((fx[0], fx[1], fx[2]), (fy[0], fy[1], fy[2]), (fz[0], fz[1], fz[2]))
    inv_o = tuple(-(inv_rot[0][k]*fo[0] + inv_rot[1][k]*fo[1] + inv_rot[2][k]*fo[2])
                  for k in range(3))
    Finv = (tuple(inv_rot[k][0] for k in range(3)),
            tuple(inv_rot[k][1] for k in range(3)),
            tuple(inv_rot[k][2] for k in range(3)), inv_o)
    return _mat_mul(T, Finv)

def _complex_parts(rd, ref):
    """The sub-entities of a complex instance, as {TYPE: args}."""
    t = rd.get(ref)
    if not t or t[0] != "__COMPLEX__":
        return {}
    return {name: args for name, args in t[1]}

def assembly_placements(rd):
    """Resolve NEXT_ASSEMBLY_USAGE_OCCURRENCE placements into a flat list of
    (shape_representation_ref, name, matrix) with matrices composed down the
    assembly tree.

    The chain an AP214 export writes is

        CONTEXT_DEPENDENT_SHAPE_REPRESENTATION( #rr, #pds )
        #rr  =( REPRESENTATION_RELATIONSHIP( '', '', #child_sr, #parent_sr )
                REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION( #idt )
                SHAPE_REPRESENTATION_RELATIONSHIP( ) )
        #pds =  PRODUCT_DEFINITION_SHAPE( '', '', #nauo )

    Returns [] when the file has no assembly structure, which is the single-part
    case and leaves the caller's flat path untouched."""
    edges = []          # (parent_sr, child_sr, matrix, occurrence name)
    for cd in rd.find("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION"):
        a = rd.args(("ref", cd))
        rr = a[0] if a and isinstance(a[0], tuple) else None
        pds = a[1] if len(a) > 1 and isinstance(a[1], tuple) else None
        if rr is None:
            continue
        parts = _complex_parts(rd, rr)
        rel = parts.get("REPRESENTATION_RELATIONSHIP")
        wt = parts.get("REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION")
        if not rel or not wt:
            continue
        srs = [x for x in rel if isinstance(x, tuple) and x[0] == "ref"]
        if len(srs) < 2:
            continue
        parent_sr, child_sr = srs[0], srs[1]
        idt = next((x for x in wt if isinstance(x, tuple) and x[0] == "ref"), None)
        M = _idt_matrix(rd, idt) if idt is not None else _IDENTITY
        name = ""
        if pds is not None:
            nauo = next((x for x in rd.args(pds)
                         if isinstance(x, tuple) and x[0] == "ref"
                         and rd.typ(x) == "NEXT_ASSEMBLY_USAGE_OCCURRENCE"), None)
            if nauo is not None:
                pdrefs = [x for x in rd.args(nauo)
                          if isinstance(x, tuple) and x[0] == "ref"]
                if len(pdrefs) > 1:
                    name = _product_name(rd, pdrefs[1])
        edges.append((parent_sr[1], child_sr[1], M, name))
    if not edges:
        return []

    children = {}
    for parent, child, M, name in edges:
        children.setdefault(parent, []).append((child, M, name))
    all_children = {c for _, c, _, _ in edges}
    roots = [p for p in children if p not in all_children]

    out = []
    def walk(sr, M, prefix):
        kids = children.get(sr)
        if not kids:
            out.append((sr, prefix, M))
            return
        for child, Mc, name in kids:
            label = name or f"part{len(out)}"
            walk(child, _mat_mul(M, Mc), f"{prefix}___{label}" if prefix else label)
    for r in roots:
        walk(r, _IDENTITY, "")
    return out

def _product_name(rd, ref):
    """The readable name behind a PRODUCT_DEFINITION, via PRODUCT_DEFINITION_FORMATION."""
    seen = set()
    stack = [ref]
    while stack:
        r = stack.pop()
        if not isinstance(r, tuple) or r[1] in seen:
            continue
        seen.add(r[1])
        t = rd.typ(r)
        if t == "PRODUCT":
            nm = rd.args(r)[1] if len(rd.args(r)) > 1 else ""
            if isinstance(nm, str) and nm.strip():
                return _sanitize(nm)
        for x in rd.args(r):
            if isinstance(x, tuple) and x[0] == "ref":
                stack.append(x)
    return ""

def _sanitize(nm):
    nm = "".join(c if (c.isalnum() or c == "_") else "_" for c in (nm or "")).strip("_")
    return nm

def solids_by_representation(rd):
    """{shape_representation_ref: [solid_ref, ...]}. A part's geometry hangs off
    an ADVANCED_BREP_SHAPE_REPRESENTATION; the assembly graph names the plain
    SHAPE_REPRESENTATION, and SHAPE_REPRESENTATION_RELATIONSHIP joins the two."""
    out = {}
    for sr in rd.find("ADVANCED_BREP_SHAPE_REPRESENTATION", "SHAPE_REPRESENTATION"):
        items = []
        for x in rd.args(("ref", sr)):
            if isinstance(x, tuple) and x[0] == "ref":
                items.append(x[1])
            elif isinstance(x, list):
                items += [y[1] for y in x
                          if isinstance(y, tuple) and y[0] == "ref"]
        sol = [i for i in items
               if rd.typ(("ref", i)) in ("MANIFOLD_SOLID_BREP", "BREP_WITH_VOIDS")]
        if sol:
            out[sr] = sol
    for rel in rd.find("SHAPE_REPRESENTATION_RELATIONSHIP"):
        refs = [x[1] for x in rd.args(("ref", rel))
                if isinstance(x, tuple) and x[0] == "ref"]
        if len(refs) < 2:
            continue
        a, b = refs[0], refs[1]
        if a in out and b not in out:
            out[b] = out[a]
        elif b in out and a not in out:
            out[a] = out[b]
    return out

def solid_name(rd, solid_ref, i):
    nm = rd.args(("ref", solid_ref))[0]
    nm = "".join(c if (c.isalnum() or c == "_") else "_" for c in (nm or "")).strip("_")
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
            if t == "ORIENTED_CLOSED_SHELL":
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
        if isinstance(x, tuple) and x[0] == "ref" and rd.typ(x) in ("CLOSED_SHELL", "OPEN_SHELL", "ORIENTED_CLOSED_SHELL"):
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

def _emit_assembly(stage, rd, cfg, placed, srmap, colors, face_col, solids, verbose):
    """One Xform per assembly placement, carrying the composed transform, with
    that part's solids as BrepArray children.

    Geometry stays in the part's own coordinate system, where the STEP authored
    it; the placement is the only thing that moves. That keeps a part placed
    several times -- a fastener repeated across an assembly -- to one set of
    authored surfaces per part instead of one per placement."""
    sidx = {sref: k for k, sref in enumerate(solids)}
    used = {}
    for sr, nm, M in placed:
        name = _sanitize(nm) or f"part_{sr}"
        if name in used:
            used[name] += 1
            name = f"{name}_{used[name]}"
        else:
            used[name] = 0
        xf = UsdGeom.Xform.Define(stage, f"/World/{name}")
        xf.AddTransformOp().Set(_usd_matrix(M))
        part_solids = srmap[sr]
        body = colors.get(sidx.get(part_solids[0], -1))
        if body:
            cpv = UsdGeom.PrimvarsAPI(xf.GetPrim()).CreatePrimvar(
                "displayColor", Sdf.ValueTypeNames.Color3fArray, UsdGeom.Tokens.constant)
            cpv.Set(Vt.Vec3fArray([Gf.Vec3f(*body)]))
        for j, sref in enumerate(part_solids):
            b = extract_brep(rd, cfg, [sref])
            fcolors = None
            frefs = face_refs_for_solid(rd, sref)
            if len(frefs) == len(b["faces"]):
                base = body or (0.6, 0.6, 0.6)
                fcolors = [face_col.get(fr[1], base) for fr in frefs]
            child = "brep" if len(part_solids) == 1 else f"brep_{j}"
            author_brep(stage, f"/World/{name}/{child}", b, cfg, face_colors=fcolors)
            if verbose:
                print(f"  {name}/{child:<8} faces={len(b['faces']):5} "
                      f"verts={len(b['verts']):6}")

def convert(inp, out, up_axis="Z", meters_per_unit=0.001, verbose=True):
    """Convert one STEP file to a UsdSolid stage: one Xform + BrepArray prim per
    solid, under a /World Xform. Output format follows the extension (.usda text or
    .usdc binary crate)."""
    ents = parse_step(open(inp, errors="replace").read())
    rd = Reader(ents)
    angle_scale = detect_angle_scale(rd, ents)
    tol, unc, usrc = derive_tolerance(rd, ents)
    cfg = Config(angle_scale=angle_scale, intersect_tol=tol)
    solids = rd.find("MANIFOLD_SOLID_BREP", "BREP_WITH_VOIDS")
    if not solids:
        raise SystemExit(f"{inp}: no MANIFOLD_SOLID_BREP / BREP_WITH_VOIDS solids found")
    colors, face_col = resolve_colors(rd, ents, solids)
    if verbose:
        unit = "degrees" if abs(angle_scale - math.pi / 180) < PERIOD_TOL else "radians"
        print(f"[{inp}] {len(ents)} entities, {len(solids)} solid(s); "
              f"plane-angle unit = {unit}; intersectTol3d = {tol:g} mm ({usrc})")

    stage = Usd.Stage.CreateInMemory()
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z if up_axis.upper() == "Z" else UsdGeom.Tokens.y)
    UsdGeom.SetStageMetersPerUnit(stage, meters_per_unit)
    world = UsdGeom.Xform.Define(stage, "/World")
    stage.SetDefaultPrim(world.GetPrim())

    placements = assembly_placements(rd)
    srmap = solids_by_representation(rd) if placements else {}
    placed = [(sr, nm, M) for sr, nm, M in placements if srmap.get(sr)]
    if placed:
        if verbose:
            uniq = len({sr for sr, _, _ in placed})
            print(f"  assembly: {len(placed)} placements of {uniq} unique part(s), "
                  f"{sum(len(srmap[sr]) for sr, _, _ in placed)} solid(s)")
        _emit_assembly(stage, rd, cfg, placed, srmap, colors, face_col, solids,
                       verbose)
        stage.Export(out)
        if verbose:
            print(f"wrote {out} ({len(placed)} placement(s))")
        return

    used = {}
    for i, s in enumerate(solids):
        b = extract_brep(rd, cfg, [s])
        name = solid_name(rd, s, i)
        if name in used:
            used[name] += 1
            name = f"{name}_{used[name]}"
        else:
            used[name] = 0
        xf = UsdGeom.Xform.Define(stage, f"/World/{name}")
        body = colors.get(i)
        if body:
            cpv = UsdGeom.PrimvarsAPI(xf.GetPrim()).CreatePrimvar(
                "displayColor", Sdf.ValueTypeNames.Color3fArray, UsdGeom.Tokens.constant)
            cpv.Set(Vt.Vec3fArray([Gf.Vec3f(*body)]))
        fcolors = None
        frefs = face_refs_for_solid(rd, s)
        if len(frefs) == len(b["faces"]):
            base = body or (0.6, 0.6, 0.6)
            fcolors = [face_col.get(fr[1], base) for fr in frefs]
        author_brep(stage, f"/World/{name}/brep", b, cfg, face_colors=fcolors)
        if verbose:
            errs = self_check(b)
            print(f"  [{i:3}] {name:24} faces={len(b['faces']):5} verts={len(b['verts']):6} "
                  f"selfcheck={'OK' if not errs else str(len(errs)) + 'err'}")

    stage.Export(out)
    if verbose:
        print(f"wrote {out} ({len(solids)} brep prim(s))")

def main():
    import argparse
    ap = argparse.ArgumentParser(
        description="Convert a STEP (ISO 10303-21/-42) file to a UsdSolid B-rep stage.")
    ap.add_argument("input", help="input STEP file (.stp / .step)")
    ap.add_argument("output", help="output USD file (.usd / .usda / .usdc)")
    ap.add_argument("--up-axis", choices=["Y", "Z"], default="Z",
                    help="stage up axis (default Z)")
    ap.add_argument("--meters-per-unit", type=float, default=0.001,
                    help="stage metersPerUnit (default 0.001, i.e. STEP millimetres)")
    ap.add_argument("-q", "--quiet", action="store_true", help="suppress per-solid output")
    args = ap.parse_args()
    convert(args.input, args.output, args.up_axis, args.meters_per_unit, verbose=not args.quiet)

if __name__ == "__main__":
    main()
