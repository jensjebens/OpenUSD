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

import os, shutil, sys, tempfile, unittest
from pxr import Sdf, Usd, UsdSolid, UsdValidation

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


def _MakeBoxStep(reversedFaces=(), products=None):
    """Return an AP214 STEP file describing the box above, as a string. Each
    face index in reversedFaces is written as a reversed face: its plane's
    normal points into the box and its ADVANCED_FACE same_sense is .F., which
    describes the same box. With products, a list of names, the file holds one
    box per name instead, side by side along x: each the only solid of a part
    whose PRODUCT has that name, and with no name of its own, as an AP214
    assembly's parts are written."""
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
    if products is None:
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
