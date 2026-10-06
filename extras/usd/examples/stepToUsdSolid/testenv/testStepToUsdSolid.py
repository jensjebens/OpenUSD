#!/pxrpythonsubst
#
# Copyright 2026 Pixar
#
# Licensed under the terms set forth in the LICENSE.txt file available at
# https://openusd.org/license.
#
# Round-trips a STEP file through stepToUsdSolid and validates the result with
# usdSolidValidators. No CAD kernel is involved on either side: the converter is
# pure Python, and the validators read the authored arrays rather than
# evaluating surfaces. The STEP input is generated here so the test carries no
# binary fixture.

import math, os, shutil, sys, tempfile, unittest
from pxr import Gf, Sdf, Usd, UsdGeom, UsdSolid, UsdValidation

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import stepToUsdSolid

# A box of side S with a corner at the origin: 8 vertices, 12 line edges and 6
# planar faces in one closed shell. Each face loop is listed counter-clockwise
# seen from outside, which is the winding STEP's FACE_OUTER_BOUND implies and
# the winding BrepArray requires of an outer loop.
_S = 10.0
_V = [(0,0,0), (_S,0,0), (_S,_S,0), (0,_S,0),
      (0,0,_S), (_S,0,_S), (_S,_S,_S), (0,_S,_S)]
_TOP = 1   # index into _FACES of the z = S face
_FACES = [([0,3,2,1], (0,0,-1), (1,0,0)),    # z = 0
          ([4,5,6,7], (0,0, 1), (1,0,0)),    # z = S
          ([0,1,5,4], (0,-1,0), (1,0,0)),    # y = 0
          ([1,2,6,5], (1,0, 0), (0,1,0)),    # x = S
          ([2,3,7,6], (0,1, 0), (-1,0,0)),   # y = S
          ([3,0,4,7], (-1,0,0), (0,-1,0))]   # x = 0

# An assembly of the box: rig holds arm and one box, arm holds two boxes. Each
# row is (parent, child, parent item, child item), the two placements an
# ITEM_DEFINED_TRANSFORMATION maps, each (origin, axis, ref_direction) in its
# own product's coordinates. Arm is turned a quarter turn about z and moved;
# the direct box's own item is turned and moved too, so it has to be inverted.
_IDENTITY_ITEM = ((0, 0, 0), (0, 0, 1), (1, 0, 0))
_ASSEMBLY = [("rig", "arm", ((100, 0, 0), (0, 0, 1), (0, 1, 0)), _IDENTITY_ITEM),
             ("arm", "box", ((0, 20, 0), (0, 0, 1), (1, 0, 0)), _IDENTITY_ITEM),
             ("arm", "box", ((0, 0, 30), (0, -1, 0), (1, 0, 0)), _IDENTITY_ITEM),
             ("rig", "box", ((-50, 0, 0), (0, 0, 1), (1, 0, 0)),
              ((5, 5, 0), (0, 0, 1), (0, 1, 0)))]


def _MakeBoxStep(reversedFaces=(), products=None, rep1IsParent=None):
    """Return an AP214 STEP file describing the box above, as a string. Each
    face index in reversedFaces is written as a reversed face: its plane's
    normal points into the box and its ADVANCED_FACE same_sense is .F., which
    describes the same box. With products, a list of names, the file holds one
    box per name instead, side by side along x: each the only solid of a part
    whose PRODUCT has that name, and with no name of its own, as an AP214
    assembly's parts are written. With rep1IsParent, True or False, the file
    is the _ASSEMBLY above instead, its REPRESENTATION_RELATIONSHIPs naming the
    parent's representation first (as SolidWorks writes them) or the child's
    (as Open CASCADE does)."""
    rows, state = [], {"n": 0}

    def emit(text):
        state["n"] += 1
        rows.append("#%d=%s;" % (state["n"], text))
        return state["n"]

    def num(x):
        return ("%.10f" % x).rstrip("0").rstrip(".") or "0."

    def point(t):
        return "CARTESIAN_POINT('',(%s,%s,%s))" % tuple(num(c) for c in t)

    def direction(t):
        return "DIRECTION('',(%s,%s,%s))" % tuple(num(c) for c in t)

    def unit(a, b):
        d = [b[k] - a[k] for k in range(3)]
        m = sum(c * c for c in d) ** 0.5
        return tuple(c / m for c in d)

    def box(dx, solidName):
        return _EmitBox(emit, point, direction, unit, reversedFaces, dx, solidName)

    wcs = emit("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)"
               % (emit(point((0, 0, 0))), emit(direction((0, 0, 1))),
                  emit(direction((1, 0, 0)))))
    lengthUnit = emit("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.))")
    angleUnit = emit("(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))")
    solidUnit = emit("(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT())")
    # The converter derives brep:intersectTol3d from this value.
    tol = emit("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-7),#%d,"
               "'distance_accuracy_value','')" % lengthUnit)
    context = emit("(GEOMETRIC_REPRESENTATION_CONTEXT(3)"
                   "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#%d))"
                   "GLOBAL_UNIT_ASSIGNED_CONTEXT((#%d,#%d,#%d))"
                   "REPRESENTATION_CONTEXT('',''))"
                   % (tol, lengthUnit, angleUnit, solidUnit))
    if rep1IsParent is not None:
        _EmitAssembly(emit, point, direction, box, wcs, context, rep1IsParent)
    elif products is None:
        emit("ADVANCED_BREP_SHAPE_REPRESENTATION('Box',(#%d,#%d),#%d)"
             % (wcs, box(0.0, "Box"), context))
    else:
        app = emit("APPLICATION_CONTEXT('core data for automotive mechanical "
                   "design processes')")
        for k, name in enumerate(products):
            shape = emit("ADVANCED_BREP_SHAPE_REPRESENTATION('',(#%d,#%d),#%d)"
                         % (wcs, box(2 * _S * k, ""), context))
            product = emit("PRODUCT('%s','%s','',(#%d))" % (
                name, name, emit("PRODUCT_CONTEXT('',#%d,'mechanical')" % app)))
            formation = emit("PRODUCT_DEFINITION_FORMATION('','',#%d)" % product)
            definition = emit("PRODUCT_DEFINITION('design','',#%d,#%d)" % (
                formation,
                emit("PRODUCT_DEFINITION_CONTEXT('part definition',#%d,'design')" % app)))
            emit("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (
                emit("PRODUCT_DEFINITION_SHAPE('','',#%d)" % definition), shape))

    return ("ISO-10303-21;\n"
            "HEADER;\n"
            "FILE_DESCRIPTION(('UsdSolid stepToUsdSolid test box'),'2;1');\n"
            "FILE_NAME('box.step','2026-01-01T00:00:00',(''),(''),'','','');\n"
            "FILE_SCHEMA(('AUTOMOTIVE_DESIGN { 1 0 10303 214 3 1 1 }'));\n"
            "ENDSEC;\nDATA;\n%s\nENDSEC;\nEND-ISO-10303-21;\n"
            % "\n".join(rows))


def _EmitBox(emit, point, direction, unit, reversedFaces, dx, solidName):
    """Emit the box above, moved dx along x, as a MANIFOLD_SOLID_BREP named
    solidName; return its id."""
    V = [(x + dx, y, z) for x, y, z in _V]
    pt = {i: emit(point(v)) for i, v in enumerate(V)}
    vtx = {i: emit("VERTEX_POINT('',#%d)" % pt[i]) for i in range(len(V))}

    # One EDGE_CURVE per unordered vertex pair, then an ORIENTED_EDGE per
    # (edge, sense). Sharing the curve is what makes the two faces either side
    # of an edge refer to one BrepArray edge rather than two coincident ones.
    curve, oriented = {}, {}
    for loop, _, _ in _FACES:
        for k in range(len(loop)):
            i, j = loop[k], loop[(k + 1) % len(loop)]
            key = (min(i, j), max(i, j))
            if key not in curve:
                a, b = key
                vec = emit("VECTOR('',#%d,1.)" % emit(direction(unit(V[a], V[b]))))
                line = emit("LINE('',#%d,#%d)" % (pt[a], vec))
                curve[key] = emit("EDGE_CURVE('',#%d,#%d,#%d,.T.)"
                                  % (vtx[a], vtx[b], line))
            sense = (i, j) == key
            if (key, sense) not in oriented:
                oriented[(key, sense)] = emit(
                    "ORIENTED_EDGE('',*,*,#%d,.%s.)"
                    % (curve[key], "T" if sense else "F"))

    faces = []
    for index, (loop, normal, udir) in enumerate(_FACES):
        reverse = index in reversedFaces
        if reverse:
            normal = tuple(-c for c in normal)
        oes = []
        for k in range(len(loop)):
            i, j = loop[k], loop[(k + 1) % len(loop)]
            key = (min(i, j), max(i, j))
            oes.append(oriented[(key, (i, j) == key)])
        edgeLoop = emit("EDGE_LOOP('',(%s))" % ",".join("#%d" % o for o in oes))
        bound = emit("FACE_OUTER_BOUND('',#%d,.T.)" % edgeLoop)
        placement = emit("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)"
                         % (pt[loop[0]], emit(direction(normal)),
                            emit(direction(udir))))
        plane = emit("PLANE('',#%d)" % placement)
        faces.append(emit("ADVANCED_FACE('',(#%d),#%d,.%s.)"
                          % (bound, plane, "F" if reverse else "T")))

    shell = emit("CLOSED_SHELL('',(%s))" % ",".join("#%d" % f for f in faces))
    return emit("MANIFOLD_SOLID_BREP('%s',#%d)" % (solidName, shell))


def _EmitAssembly(emit, point, direction, box, wcs, context, rep1IsParent):
    """Emit _ASSEMBLY: a product, definition and shape representation per
    product, then one NEXT_ASSEMBLY_USAGE_OCCURRENCE with its
    CONTEXT_DEPENDENT_SHAPE_REPRESENTATION per row."""
    app = emit("APPLICATION_CONTEXT('core data for automotive mechanical "
               "design processes')")

    def place(item):
        o, z, x = item
        return emit("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)"
                    % (emit(point(o)), emit(direction(z)), emit(direction(x))))

    items = [(place(p), place(c)) for _, _, p, c in _ASSEMBLY]
    own = {"rig": [wcs], "arm": [wcs], "box": [wcs]}
    for (parent, child, _, _), (pi, ci) in zip(_ASSEMBLY, items):
        own[parent].append(pi)
        own[child].append(ci)
    listed = lambda ids: ",".join("#%d" % i for i in ids)
    rep = {n: emit("SHAPE_REPRESENTATION('%s',(%s),#%d)" % (n, listed(own[n]), context))
           for n in ("rig", "arm")}
    rep["box"] = emit("ADVANCED_BREP_SHAPE_REPRESENTATION('box',(%s),#%d)"
                      % (listed(own["box"] + [box(0.0, "")]), context))
    pd = {}
    for n in ("rig", "arm", "box"):
        product = emit("PRODUCT('%s','%s','',(#%d))" % (
            n, n, emit("PRODUCT_CONTEXT('',#%d,'mechanical')" % app)))
        pd[n] = emit("PRODUCT_DEFINITION('design','',#%d,#%d)" % (
            emit("PRODUCT_DEFINITION_FORMATION('','',#%d)" % product),
            emit("PRODUCT_DEFINITION_CONTEXT('part definition',#%d,'design')" % app)))
        emit("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (
            emit("PRODUCT_DEFINITION_SHAPE('','',#%d)" % pd[n]), rep[n]))
    for k, ((parent, child, _, _), (pi, ci)) in enumerate(zip(_ASSEMBLY, items)):
        nauo = emit("NEXT_ASSEMBLY_USAGE_OCCURRENCE('%d','','',#%d,#%d,$)"
                    % (k + 1, pd[parent], pd[child]))
        # transform_item_1 lies in rep_1, transform_item_2 in rep_2.
        reps, its = (rep[parent], rep[child]), (pi, ci)
        if not rep1IsParent:
            reps, its = reps[::-1], its[::-1]
        idt = emit("ITEM_DEFINED_TRANSFORMATION('','',#%d,#%d)" % its)
        rr = emit("(REPRESENTATION_RELATIONSHIP('','',#%d,#%d)"
                  "REPRESENTATION_RELATIONSHIP_WITH_TRANSFORMATION(#%d)"
                  "SHAPE_REPRESENTATION_RELATIONSHIP())" % (reps + (idt,)))
        emit("CONTEXT_DEPENDENT_SHAPE_REPRESENTATION(#%d,#%d)"
             % (rr, emit("PRODUCT_DEFINITION_SHAPE('','',#%d)" % nauo)))


def _SolidValidators():
    registry = UsdValidation.ValidationRegistry()
    names = [m.name for m in registry.GetAllValidatorMetadata()
             if m.name.startswith("usdSolidValidators:")]
    return registry.GetOrLoadValidatorsByName(names)


class TestStepToUsdSolid(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls._dir = tempfile.mkdtemp()
        stepPath = os.path.join(cls._dir, "box.step")
        with open(stepPath, "w") as f:
            f.write(_MakeBoxStep())
        cls._usdPath = os.path.join(cls._dir, "box.usda")
        stepToUsdSolid.convert(stepPath, cls._usdPath, verbose=False)
        cls._stage = Usd.Stage.Open(cls._usdPath)
        cls.assertTrue(cls._stage, "converter produced no stage")
        # The same box with its top face (index 1) written reversed.
        reversedStep = os.path.join(cls._dir, "box_reversed.step")
        with open(reversedStep, "w") as f:
            f.write(_MakeBoxStep(reversedFaces=(_TOP,)))
        cls._reversedPath = os.path.join(cls._dir, "box_reversed.usda")
        stepToUsdSolid.convert(reversedStep, cls._reversedPath, verbose=False)
        cls._reversedStage = Usd.Stage.Open(cls._reversedPath)

    def _Brep(self, stage=None):
        stage = stage or self._stage
        breps = [p for p in stage.Traverse() if p.IsA(UsdSolid.BrepArray)]
        self.assertEqual(len(breps), 1, "expected exactly one BrepArray")
        return UsdSolid.BrepArray(breps[0])

    def _Topology(self, stage):
        """Per face: its outward faceuse side and its loops, each a list of
        (edge index, edgeuse orientation); and every edgeuse's orientation
        with its radial entry."""
        prim = self._Brep(stage).GetPrim()
        get = lambda name: list(prim.GetAttribute(name).Get())
        loopCount, euCount = get("face:loopCount"), get("loop:edgeuseCount")
        euEdge, euOrient = get("edgeuse:edgeIndex"), get("edgeuse:orientationType")
        fuFace, fuOrient = get("faceuse:faceIndex"), get("faceuse:orientationType")
        outward = {}
        for k in range(get("shell:faceuseCount")[0]):   # the exterior shell
            outward[fuFace[k]] = fuOrient[k]
        faces, li, eo = [], 0, 0
        for fi, count in enumerate(loopCount):
            loops = []
            for _ in range(count):
                n = euCount[li]
                loops.append([(euEdge[eo + j], euOrient[eo + j]) for j in range(n)])
                li, eo = li + 1, eo + n
            faces.append((outward[fi], loops))
        return faces, list(zip(euOrient, get("edgeuse:thisRadialEntryType")))

    def test_TopologyCounts(self):
        """The six ADVANCED_FACEs and eight VERTEX_POINTs survive the
        conversion, and the twelve EDGE_CURVEs are shared rather than
        duplicated per face."""
        brep = self._Brep()
        self.assertEqual(len(brep.GetFaceSurfaceTypeAttr().Get()), 6)
        self.assertEqual(len(brep.GetVertexPointTypeAttr().Get()), 8)
        self.assertEqual(len(brep.GetEdgeVertexIndicesAttr().Get()), 12)

    def test_ToleranceFromStep(self):
        """brep:intersectTol3d comes from the file's
        UNCERTAINTY_MEASURE_WITH_UNIT, not from a hard-coded default."""
        tol = self._Brep().GetBrepIntersectTol3dAttr().Get()
        self.assertEqual(len(tol), 1, "one tolerance per Brep")
        self.assertGreater(tol[0], 0.0)

    def test_ValidatorsFindNothing(self):
        """The authored BrepArray satisfies every usdSolid validator."""
        validators = _SolidValidators()
        self.assertTrue(validators, "usdSolidValidators are not registered")
        errors = UsdValidation.ValidationContext(validators).Validate(self._stage)
        self.assertEqual(
            [], list(errors),
            "\n".join("%s: %s" % (e.GetName(), e.GetMessage()) for e in errors))

    def test_ReversedFace(self):
        """A face whose same_sense is .F. has its surface normal pointing into
        the solid. Its outward faceuse is `opposite` and its loop is the plain
        box's loop reversed, which is how SMLib's own exporter writes a
        reversed face; the other faces are unchanged."""
        plain, _ = self._Topology(self._stage)
        rev, _ = self._Topology(self._reversedStage)
        self.assertEqual(plain[_TOP][0], "same")
        self.assertEqual(rev[_TOP][0], "opposite")
        flip = {"same": "opposite", "opposite": "same"}
        self.assertEqual(rev[_TOP][1],
                         [[(e, flip[o]) for e, o in reversed(loop)]
                          for loop in plain[_TOP][1]])
        for fi in range(len(plain)):
            if fi != _TOP:
                self.assertEqual(rev[fi], plain[fi], "face %d changed" % fi)

    def test_RadialEntryFollowsOrientation(self):
        """Each edgeuse enters the radial order from the top when it runs
        along its edge curve and from the bottom otherwise, in both boxes. On
        the reversed face's edges both uses run the same way, so alternating
        the entry by position would be wrong there."""
        for stage in (self._stage, self._reversedStage):
            _, uses = self._Topology(stage)
            self.assertEqual(
                [], [(o, e) for o, e in uses
                     if e != ("topEntry" if o == "same" else "bottomEntry")])

    def test_ReversedFaceValidators(self):
        """The reversed box satisfies every usdSolid validator too."""
        errors = UsdValidation.ValidationContext(
            _SolidValidators()).Validate(self._reversedStage)
        self.assertEqual(
            [], list(errors),
            "\n".join("%s: %s" % (e.GetName(), e.GetMessage()) for e in errors))

    def test_BodiesNamedAfterProducts(self):
        """A solid that is the only solid of a part is named after the part's
        PRODUCT, made a valid prim name and unique. The box above, which no
        part holds, keeps its own name."""
        stepPath = os.path.join(self._dir, "parts.step")
        with open(stepPath, "w") as f:
            f.write(_MakeBoxStep(products=[
                "adapter plate", "adapter plate", "2nd jaw", "jaw (left)"]))
        usdPath = os.path.join(self._dir, "parts.usda")
        stepToUsdSolid.convert(stepPath, usdPath, verbose=False)
        stage = Usd.Stage.Open(usdPath)
        names = [p.GetParent().GetName() for p in stage.Traverse()
                 if p.IsA(UsdSolid.BrepArray)]
        self.assertEqual(
            names, ["adapter_plate", "adapter_plate_1", "_2nd_jaw", "jaw_left"])
        self.assertTrue(all(Sdf.Path.IsValidIdentifier(n) for n in names))
        self.assertEqual(self._Brep().GetPrim().GetParent().GetName(), "Box")

    def test_AssemblyPlacements(self):
        """Each placement of an assembly's part lands where its chain of
        NEXT_ASSEMBLY_USAGE_OCCURRENCEs puts it, whichever representation the
        writer names first: the child's item is mapped onto the parent's, and
        a subassembly's placement carries down to its parts. A part placed
        three times authors three bodies."""
        def axes(item):
            o, z, x = (Gf.Vec3d(*v) for v in item)
            z = z.GetNormalized()
            x = (x - z * Gf.Dot(x, z)).GetNormalized()
            return o, x, Gf.Cross(z, x), z

        def place(row, p):
            # into the child item's frame, then out of the parent item's
            _, _, parentItem, childItem = row
            o, x, y, z = axes(childItem)
            q = Gf.Vec3d(*p) - o
            local = Gf.Vec3d(Gf.Dot(x, q), Gf.Dot(y, q), Gf.Dot(z, q))
            o, x, y, z = axes(parentItem)
            return o + x * local[0] + y * local[1] + z * local[2]

        arm, inArm1, inArm2, direct = _ASSEMBLY
        expected = {"arm___box": [place(arm, place(inArm1, v)) for v in _V],
                    "arm___box_1": [place(arm, place(inArm2, v)) for v in _V],
                    "box": [place(direct, v) for v in _V]}
        for rep1IsParent in (True, False):
            stepPath = os.path.join(self._dir, "assembly_%d.step" % rep1IsParent)
            with open(stepPath, "w") as f:
                f.write(_MakeBoxStep(rep1IsParent=rep1IsParent))
            usdPath = os.path.join(self._dir, "assembly_%d.usda" % rep1IsParent)
            stepToUsdSolid.convert(stepPath, usdPath, verbose=False)
            stage = Usd.Stage.Open(usdPath)
            cache = UsdGeom.XformCache()
            got = {}
            for prim in stage.Traverse():
                if prim.IsA(UsdSolid.BrepArray):
                    M = cache.GetLocalToWorldTransform(prim)
                    got[prim.GetParent().GetName()] = [
                        M.Transform(p) for p in
                        prim.GetAttribute("brep:vertexPoint:point:position").Get()]
            self.assertEqual(sorted(got), sorted(expected), "rep1IsParent=%s" % rep1IsParent)
            for name, points in expected.items():
                self.assertEqual(len(got[name]), len(points))
                worst = max(min((g - p).GetLength() for g in got[name]) for p in points)
                self.assertLess(worst, 1e-9, "%s, rep1IsParent=%s" % (name, rep1IsParent))

    def test_PlaneWindowHoldsItsCircle(self):
        """A plane face's window (face:range) holds its whole boundary, not
        only the points sampled along it. A circle of radius 125 sampled every
        0.06 rad reaches only v = 124.945 at its samples; a window that stops
        there cuts the face, and SMLib then measures a flange's volume 5e-4
        short."""
        r = 125.0
        edge = {"ctok": "BrepCurve3dCircleAPI", "rng": (0.0, 2 * math.pi), "v": (0, 0),
                "geom": {"center": (0.0, 0.0, 0.0), "axis": (0.0, 0.0, 1.0),
                         "refDirection": (1.0, 0.0, 0.0), "radius": r}}
        verts = [(r, 0.0, 0.0)]
        samples = verts + stepToUsdSolid._edge_interior_samples(edge, verts)
        plane = {"origin": (0.0, 0.0, 0.0), "axis": (0.0, 0.0, 1.0),
                 "refDirection": (1.0, 0.0, 0.0)}
        (ulo, uhi), (vlo, vhi) = stepToUsdSolid.face_range(
            "BrepSurfacePlaneAPI", plane, samples)
        self.assertLessEqual(max(ulo, vlo), -r)
        self.assertGreaterEqual(min(uhi, vhi), r)

    def test_ValidatorsCatchCorruption(self):
        """A guard on the check above: point one edge at a vertex that does not
        exist and the same suite must report it. Without this, a suite that
        silently failed to load would still pass test_ValidatorsFindNothing."""
        # Open a copy: Usd.Stage.Open caches by resolved path, so mutating the
        # stage opened from self._usdPath would corrupt it for every other test.
        corrupt = os.path.join(self._dir, "corrupt.usda")
        shutil.copyfile(self._usdPath, corrupt)
        stage = Usd.Stage.Open(corrupt)
        brep = UsdSolid.BrepArray(
            [p for p in stage.Traverse() if p.IsA(UsdSolid.BrepArray)][0])
        attr = brep.GetEdgeVertexIndicesAttr()
        pairs = list(attr.Get())
        pairs[0] = type(pairs[0])(9999, pairs[0][1])
        attr.Set(type(attr.Get())(pairs))
        errors = UsdValidation.ValidationContext(_SolidValidators()).Validate(stage)
        self.assertTrue(errors, "out-of-range vertex index went unreported")


if __name__ == "__main__":
    unittest.main(verbosity=2)
