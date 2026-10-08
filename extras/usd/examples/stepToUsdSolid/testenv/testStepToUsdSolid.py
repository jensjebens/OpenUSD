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


def _MakeBoxStep(reversedFaces=(), products=None, rep1IsParent=None, unit="mm",
                 densityMetre=False, surfaceModel=False, solidWorks=False, rootBox=None,
                 curvesAgainst=False):
    """Return an AP214 STEP file describing the box above, as a string. Each
    face index in reversedFaces is written as a reversed face: its plane's
    normal points into the box and its ADVANCED_FACE same_sense is .F., which
    describes the same box. With products, a list of names, the file holds one
    box per name instead, side by side along x: each the only solid of a part
    whose PRODUCT has that name, and with no name of its own, as an AP214
    assembly's parts are written. With rep1IsParent, True or False, the file
    is the _ASSEMBLY above instead, its REPRESENTATION_RELATIONSHIPs naming the
    parent's representation first (as SolidWorks writes them) or the child's
    (as Open CASCADE does); with solidWorks too, each part's
    SHAPE_DEFINITION_REPRESENTATION names a plain SHAPE_REPRESENTATION that a
    SHAPE_REPRESENTATION_RELATIONSHIP joins to the
    ADVANCED_BREP_SHAPE_REPRESENTATION holding its solid, every representation
    in a context of its own (ISO 10303-43 WR1), as SolidWorks writes a part.
    unit "inch" writes the length unit as an inch CONVERSION_BASED_UNIT of
    25.4 millimetres. densityMetre adds a metre LENGTH_UNIT in a context no
    shape uses, as a file declaring a density in kg/m3 does. surfaceModel
    writes the single box as a SHELL_BASED_SURFACE_MODEL of its CLOSED_SHELL
    under a MANIFOLD_SURFACE_SHAPE_REPRESENTATION. rootBox gives the
    assembly's root a box of its own, reached through two
    SHAPE_DEFINITION_REPRESENTATIONs: "joined", one to the root's
    SHAPE_REPRESENTATION and one to an ADVANCED_BREP_SHAPE_REPRESENTATION a
    SHAPE_REPRESENTATION_RELATIONSHIP joins to it; "unjoined", the same solid
    listed in both, with no relationship. curvesAgainst writes the edges
    between vertices whose indices sum to an odd number (eight of the
    twelve) the other way round, end vertex first with same_sense .F., so
    that each one's LINE runs against it; their ORIENTED_EDGEs flip to
    match, which leaves every loop running as before."""
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

    def unitVector(a, b):
        d = [b[k] - a[k] for k in range(3)]
        m = sum(c * c for c in d) ** 0.5
        return tuple(c / m for c in d)

    def box(dx, solidName):
        return _EmitBox(emit, point, direction, unitVector, reversedFaces, dx, solidName,
                        surfaceModel=surfaceModel, curvesAgainst=curvesAgainst)

    wcs = emit("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)"
               % (emit(point((0, 0, 0))), emit(direction((0, 0, 1))),
                  emit(direction((1, 0, 0)))))
    lengthUnit = emit("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT(.MILLI.,.METRE.))")
    if unit == "inch":
        lengthUnit = emit("(CONVERSION_BASED_UNIT('INCH',#%d)LENGTH_UNIT()NAMED_UNIT(#%d))" % (
            emit("LENGTH_MEASURE_WITH_UNIT(LENGTH_MEASURE(25.4),#%d)" % lengthUnit),
            emit("DIMENSIONAL_EXPONENTS(1.,0.,0.,0.,0.,0.,0.)")))
    if densityMetre:
        metre = emit("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT($,.METRE.))")
        emit("DERIVED_UNIT((#%d))" % emit("DERIVED_UNIT_ELEMENT(#%d,-3.)" % metre))
        emit("(GLOBAL_UNIT_ASSIGNED_CONTEXT((#%d))REPRESENTATION_CONTEXT('material',''))"
             % metre)
    angleUnit = emit("(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))")
    solidUnit = emit("(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT())")
    # The converter derives brep:intersectTol3d from this value.
    tol = emit("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-7),#%d,"
               "'distance_accuracy_value','')" % lengthUnit)
    def newContext():
        return emit("(GEOMETRIC_REPRESENTATION_CONTEXT(3)"
                    "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#%d))"
                    "GLOBAL_UNIT_ASSIGNED_CONTEXT((#%d,#%d,#%d))"
                    "REPRESENTATION_CONTEXT('',''))"
                    % (tol, lengthUnit, angleUnit, solidUnit))
    context = newContext()
    if rep1IsParent is not None:
        _EmitAssembly(emit, point, direction, box, wcs, context, rep1IsParent,
                      newContext=newContext if solidWorks else None, rootBox=rootBox)
    elif products is None:
        emit("%s('Box',(#%d,#%d),#%d)"
             % ("MANIFOLD_SURFACE_SHAPE_REPRESENTATION" if surfaceModel
                else "ADVANCED_BREP_SHAPE_REPRESENTATION", wcs, box(0.0, "Box"), context))
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


def _EmitBox(emit, point, direction, unit, reversedFaces, dx, solidName, surfaceModel=False,
             curvesAgainst=False):
    """Emit the box above, moved dx along x, as a MANIFOLD_SOLID_BREP named
    solidName (surfaceModel: a SHELL_BASED_SURFACE_MODEL of its CLOSED_SHELL);
    return its id. curvesAgainst: see _MakeBoxStep."""
    V = [(x + dx, y, z) for x, y, z in _V]
    pt = {i: emit(point(v)) for i, v in enumerate(V)}
    vtx = {i: emit("VERTEX_POINT('',#%d)" % pt[i]) for i in range(len(V))}

    # One EDGE_CURVE per unordered vertex pair, then an ORIENTED_EDGE per
    # (edge, sense). Sharing the curve is what makes the two faces either side
    # of an edge refer to one BrepArray edge rather than two coincident ones.
    curve, oriented = {}, {}
    against = lambda key: curvesAgainst and sum(key) % 2 == 1
    along = lambda i, key: i == (key[1] if against(key) else key[0])
    for loop, _, _ in _FACES:
        for k in range(len(loop)):
            i, j = loop[k], loop[(k + 1) % len(loop)]
            key = (min(i, j), max(i, j))
            if key not in curve:
                a, b = key
                vec = emit("VECTOR('',#%d,1.)" % emit(direction(unit(V[a], V[b]))))
                line = emit("LINE('',#%d,#%d)" % (pt[a], vec))
                if against(key):
                    curve[key] = emit("EDGE_CURVE('',#%d,#%d,#%d,.F.)"
                                      % (vtx[b], vtx[a], line))
                else:
                    curve[key] = emit("EDGE_CURVE('',#%d,#%d,#%d,.T.)"
                                      % (vtx[a], vtx[b], line))
            sense = along(i, key)
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
            oes.append(oriented[(key, along(i, key))])
        edgeLoop = emit("EDGE_LOOP('',(%s))" % ",".join("#%d" % o for o in oes))
        bound = emit("FACE_OUTER_BOUND('',#%d,.T.)" % edgeLoop)
        placement = emit("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)"
                         % (pt[loop[0]], emit(direction(normal)),
                            emit(direction(udir))))
        plane = emit("PLANE('',#%d)" % placement)
        faces.append(emit("ADVANCED_FACE('',(#%d),#%d,.%s.)"
                          % (bound, plane, "F" if reverse else "T")))

    shell = emit("CLOSED_SHELL('',(%s))" % ",".join("#%d" % f for f in faces))
    if surfaceModel:
        return emit("SHELL_BASED_SURFACE_MODEL('%s',(#%d))" % (solidName, shell))
    return emit("MANIFOLD_SOLID_BREP('%s',#%d)" % (solidName, shell))


def _EmitAssembly(emit, point, direction, box, wcs, context, rep1IsParent, newContext=None,
                  rootBox=None):
    """Emit _ASSEMBLY: a product, definition and shape representation per
    product, then one NEXT_ASSEMBLY_USAGE_OCCURRENCE with its
    CONTEXT_DEPENDENT_SHAPE_REPRESENTATION per row. With newContext, each
    representation gets a context of its own, and the box part's
    SHAPE_REPRESENTATION holds only its placements, joined by a
    SHAPE_REPRESENTATION_RELATIONSHIP to the ADVANCED_BREP_SHAPE_REPRESENTATION
    that holds the solid (SolidWorks' layout)."""
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
    ctx = newContext or (lambda: context)
    rigBox = box(-4 * _S, "") if rootBox else None
    if rootBox == "unjoined":
        own["rig"].append(rigBox)
    rep = {n: emit("SHAPE_REPRESENTATION('%s',(%s),#%d)" % (n, listed(own[n]), ctx()))
           for n in ("rig", "arm")}
    rigBrep = None
    if rootBox:
        rigBrep = emit("ADVANCED_BREP_SHAPE_REPRESENTATION('rig',(#%d,#%d),#%d)"
                       % (wcs, rigBox, ctx()))
        if rootBox == "joined":
            emit("SHAPE_REPRESENTATION_RELATIONSHIP('','',#%d,#%d)" % (rep["rig"], rigBrep))
    if newContext is None:
        rep["box"] = emit("ADVANCED_BREP_SHAPE_REPRESENTATION('box',(%s),#%d)"
                          % (listed(own["box"] + [box(0.0, "")]), context))
    else:
        rep["box"] = emit("SHAPE_REPRESENTATION('box',(%s),#%d)"
                          % (listed(own["box"]), ctx()))
        brep = emit("ADVANCED_BREP_SHAPE_REPRESENTATION('box',(#%d,#%d),#%d)"
                    % (wcs, box(0.0, ""), ctx()))
        emit("SHAPE_REPRESENTATION_RELATIONSHIP('','',#%d,#%d)" % (rep["box"], brep))
    pd = {}
    for n in ("rig", "arm", "box"):
        product = emit("PRODUCT('%s','%s','',(#%d))" % (
            n, n, emit("PRODUCT_CONTEXT('',#%d,'mechanical')" % app)))
        pd[n] = emit("PRODUCT_DEFINITION('design','',#%d,#%d)" % (
            emit("PRODUCT_DEFINITION_FORMATION('','',#%d)" % product),
            emit("PRODUCT_DEFINITION_CONTEXT('part definition',#%d,'design')" % app)))
        emit("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (
            emit("PRODUCT_DEFINITION_SHAPE('','',#%d)" % pd[n]), rep[n]))
    if rigBrep is not None:
        emit("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (
            emit("PRODUCT_DEFINITION_SHAPE('','',#%d)" % pd["rig"]), rigBrep))
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


def _MakeSheetStep(sheets=1, product=None, oriented=False, uncertainty=1.e-3, withBox=False):
    """Return an AP242 STEP file holding sheet bodies, as NAPA Designer exports
    a plate: each a SHELL_BASED_SURFACE_MODEL whose OPEN_SHELL holds one planar
    face, a 3 x 2 rectangle (the k-th moved 4k along x), under one
    MANIFOLD_SURFACE_SHAPE_REPRESENTATION whose length unit is the metre and
    whose accuracy is uncertainty (metres). With product, the representation
    is a part's shape, named product (as a stiffener profile's web and flange
    are one part). oriented wraps each OPEN_SHELL in a reversed
    ORIENTED_OPEN_SHELL. withBox adds the test box as a solid, under an
    ADVANCED_BREP_SHAPE_REPRESENTATION of its own."""
    rows = []

    def emit(text):
        rows.append("#%d=%s;" % (len(rows) + 1, text))
        return len(rows)

    def point(v):
        return "CARTESIAN_POINT('',(%r,%r,%r))" % tuple(float(c) for c in v)

    def direction(v):
        return "DIRECTION('',(%r,%r,%r))" % tuple(float(c) for c in v)

    def unitVector(a, b):
        d = [b[k] - a[k] for k in range(3)]
        m = sum(c * c for c in d) ** 0.5
        return tuple(c / m for c in d)

    models = []
    for k in range(sheets):
        V = [(4 * k, 0, 0), (4 * k + 3, 0, 0), (4 * k + 3, 2, 0), (4 * k, 2, 0)]
        pts = [emit(point(v)) for v in V]
        vtx = [emit("VERTEX_POINT('',#%d)" % p) for p in pts]
        edges = []
        for e in range(4):
            a, b = V[e], V[(e + 1) % 4]
            m = sum((b[i] - a[i]) ** 2 for i in range(3)) ** 0.5
            vec = emit("VECTOR('',#%d,%r)" % (emit(direction(unitVector(a, b))), float(m)))
            line = emit("LINE('',#%d,#%d)" % (pts[e], vec))
            curve = emit("EDGE_CURVE('',#%d,#%d,#%d,.T.)" % (vtx[e], vtx[(e + 1) % 4], line))
            edges.append(emit("ORIENTED_EDGE('',*,*,#%d,.T.)" % curve))
        loop = emit("EDGE_LOOP('',(%s))" % ",".join("#%d" % e for e in edges))
        bound = emit("FACE_OUTER_BOUND('',#%d,.T.)" % loop)
        plane = emit("PLANE('',#%d)" % emit("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (
            emit(point(V[0])), emit(direction((0, 0, 1))), emit(direction((1, 0, 0))))))
        face = emit("ADVANCED_FACE('',(#%d),#%d,.T.)" % (bound, plane))
        shell = emit("OPEN_SHELL('',(#%d))" % face)
        if oriented:
            shell = emit("ORIENTED_OPEN_SHELL('',*,#%d,.F.)" % shell)
        models.append(emit("SHELL_BASED_SURFACE_MODEL('',(#%d))" % shell))
    length = emit("(LENGTH_UNIT()NAMED_UNIT(*)SI_UNIT($,.METRE.))")
    angle = emit("(NAMED_UNIT(*)PLANE_ANGLE_UNIT()SI_UNIT($,.RADIAN.))")
    solid = emit("(NAMED_UNIT(*)SI_UNIT($,.STERADIAN.)SOLID_ANGLE_UNIT())")
    tol = emit("UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(%r),#%d,"
               "'distance_accuracy_value','')" % (float(uncertainty), length))

    def newContext():
        return emit("(GEOMETRIC_REPRESENTATION_CONTEXT(3)"
                    "GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#%d))"
                    "GLOBAL_UNIT_ASSIGNED_CONTEXT((#%d,#%d,#%d))"
                    "REPRESENTATION_CONTEXT('',''))" % (tol, length, angle, solid))

    shape = emit("MANIFOLD_SURFACE_SHAPE_REPRESENTATION('',(%s),#%d)"
                 % (",".join("#%d" % m for m in models), newContext()))
    if product is not None:
        app = emit("APPLICATION_CONTEXT('core data for automotive mechanical "
                   "design processes')")
        prod = emit("PRODUCT('%s','%s','',(#%d))" % (
            product, product, emit("PRODUCT_CONTEXT('',#%d,'mechanical')" % app)))
        definition = emit("PRODUCT_DEFINITION('design','',#%d,#%d)" % (
            emit("PRODUCT_DEFINITION_FORMATION('','',#%d)" % prod),
            emit("PRODUCT_DEFINITION_CONTEXT('part definition',#%d,'design')" % app)))
        emit("SHAPE_DEFINITION_REPRESENTATION(#%d,#%d)" % (
            emit("PRODUCT_DEFINITION_SHAPE('','',#%d)" % definition), shape))
    if withBox:
        wcs = emit("AXIS2_PLACEMENT_3D('',#%d,#%d,#%d)" % (
            emit(point((0, 0, 0))), emit(direction((0, 0, 1))), emit(direction((1, 0, 0)))))
        emit("ADVANCED_BREP_SHAPE_REPRESENTATION('Box',(#%d,#%d),#%d)" % (
            wcs, _EmitBox(emit, point, direction, unitVector, (), 10.0, "Box"), newContext()))
    return ("ISO-10303-21;\n"
            "HEADER;\n"
            "FILE_DESCRIPTION(('UsdSolid stepToUsdSolid test sheet'),'2;1');\n"
            "FILE_NAME('sheet.step','2026-01-01T00:00:00',(''),(''),'','','');\n"
            "FILE_SCHEMA(('AP242_MANAGED_MODEL_BASED_3D_ENGINEERING_MIM_LF'));\n"
            "ENDSEC;\nDATA;\n%s\nENDSEC;\nEND-ISO-10303-21;\n" % "\n".join(rows))


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
        three times authors three bodies, once each. The same holds for
        SolidWorks' layout, where a part's shape joins the representation
        holding its solid through a SHAPE_REPRESENTATION_RELATIONSHIP."""
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
        for rep1IsParent, solidWorks in ((True, False), (False, False), (True, True)):
            stepPath = os.path.join(self._dir, "assembly_%d%d.step" % (rep1IsParent, solidWorks))
            with open(stepPath, "w") as f:
                f.write(_MakeBoxStep(rep1IsParent=rep1IsParent, solidWorks=solidWorks))
            usdPath = os.path.join(self._dir, "assembly_%d%d.usda" % (rep1IsParent, solidWorks))
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
            # one body per placement: a duplicate would overwrite its twin in got
            self.assertEqual(len([p for p in stage.Traverse() if p.IsA(UsdSolid.BrepArray)]),
                             len(expected))
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

    def test_SheetBody(self):
        """A sheet body, as NAPA Designer exports a plate, converts: a
        SHELL_BASED_SURFACE_MODEL of one OPEN_SHELL becomes one region, the
        infinite void, whose one shell holds both faceuses of the face; each
        free edge has one edgeuse, radially its own next; metersPerUnit is the
        file's metre; and every validator passes."""
        stepPath = os.path.join(self._dir, "sheet.step")
        usdPath = os.path.join(self._dir, "sheet.usda")
        with open(stepPath, "w") as f:
            f.write(_MakeSheetStep())
        stepToUsdSolid.convert(stepPath, usdPath, verbose=False)
        stage = Usd.Stage.Open(usdPath)
        self.assertEqual(UsdGeom.GetStageMetersPerUnit(stage), 1.0)
        prim = self._Brep(stage).GetPrim()
        get = lambda name: list(prim.GetAttribute(name).Get())
        self.assertEqual(get("region:type"), ["voidRegion"])
        self.assertEqual(get("region:shellCount"), [1])
        self.assertEqual(get("shell:faceuseCount"), [2])
        self.assertEqual(sorted(get("faceuse:orientationType")), ["opposite", "same"])
        nextRadial = get("edgeuse:nextRadialEUIndex")
        self.assertEqual(nextRadial, list(range(4)))
        errors = UsdValidation.ValidationContext(_SolidValidators()).Validate(stage)
        self.assertEqual(
            [], list(errors),
            "\n".join("%s: %s" % (e.GetName(), e.GetMessage()) for e in errors))

    def _Convert(self, text, name, **kw):
        stepPath = os.path.join(self._dir, name + ".step")
        usdPath = os.path.join(self._dir, name + ".usda")
        with open(stepPath, "w") as f:
            f.write(text)
        stepToUsdSolid.convert(stepPath, usdPath, verbose=False, **kw)
        return Usd.Stage.Open(usdPath)

    def _Breps(self, stage):
        return [p for p in stage.Traverse() if p.IsA(UsdSolid.BrepArray)]

    def _AssertValid(self, stage):
        errors = UsdValidation.ValidationContext(_SolidValidators()).Validate(stage)
        self.assertEqual(
            [], list(errors),
            "\n".join("%s: %s" % (e.GetName(), e.GetMessage()) for e in errors))

    def test_LengthUnitFromTheBodysContext(self):
        """metersPerUnit is the length unit of the context the body's
        representation names. A metre the file declares for a density, in a
        context no shape uses, does not count; an inch is read through its
        conversion factor; an explicit meters_per_unit wins."""
        mpu = UsdGeom.GetStageMetersPerUnit
        self.assertEqual(mpu(self._Convert(_MakeBoxStep(densityMetre=True), "density")), 0.001)
        self.assertAlmostEqual(mpu(self._Convert(_MakeBoxStep(unit="inch"), "inch")),
                               0.0254, places=12)
        self.assertEqual(mpu(self._Convert(_MakeBoxStep(unit="inch"), "inch_given",
                                           meters_per_unit=0.01)), 0.01)

    def test_ToleranceInTheFilesUnit(self):
        """brep:intersectTol3d is the declared accuracy over 20 in the file's own
        unit, and its millimetre floor scales with the unit: a sheet in metres
        declaring 1 mm gets 0.05 mm, one declaring 1 nm the 5e-4 mm floor."""
        for unc, want in ((1e-3, 5e-5), (1e-9, 5e-7)):
            st = self._Convert(_MakeSheetStep(uncertainty=unc), "tolerance_%g" % unc)
            tol = UsdSolid.BrepArray(self._Breps(st)[0]).GetBrepIntersectTol3dAttr().Get()
            self.assertAlmostEqual(tol[0], want, delta=want * 1e-9)

    def test_SheetPartsKeepTheirProduct(self):
        """A sheet that is a part's only body is named after its PRODUCT. A part
        of two sheets, as a NAPA stiffener's web and flange are, keeps the
        bodies' own names, and each body records its part (customData
        stepToUsdSolid:product)."""
        st = self._Convert(_MakeSheetStep(product="P:P1/SHELL_P"), "sheet_named")
        self.assertEqual([p.GetParent().GetName() for p in self._Breps(st)], ["P_P1_SHELL_P"])
        st = self._Convert(_MakeSheetStep(sheets=2, product="S:ABC/SHELL_P - Profile"),
                           "sheet_pair")
        holders = [p.GetParent() for p in self._Breps(st)]
        self.assertEqual([h.GetName() for h in holders], ["body_0", "body_1"])
        for h in holders:
            self.assertEqual(h.GetCustomDataByKey("stepToUsdSolid:product"),
                             "S:ABC/SHELL_P - Profile")
        self._AssertValid(st)

    def test_OrientedOpenShell(self):
        """A surface model whose shell is a reversed ORIENTED_OPEN_SHELL converts
        as the sheet of its base shell."""
        st = self._Convert(_MakeSheetStep(oriented=True), "oriented_open")
        self.assertEqual(list(self._Breps(st)[0].GetAttribute("region:type").Get()),
                         ["voidRegion"])
        self._AssertValid(st)

    def test_SurfaceModelOfAClosedShell(self):
        """A SHELL_BASED_SURFACE_MODEL bounds no material even when its shell is
        closed: the box written as one is a single region, the infinite void,
        whose shell holds both faceuses of all six faces."""
        st = self._Convert(_MakeBoxStep(surfaceModel=True), "surface_box")
        prim = self._Breps(st)[0]
        self.assertEqual(list(prim.GetAttribute("region:type").Get()), ["voidRegion"])
        self.assertEqual(list(prim.GetAttribute("shell:faceuseCount").Get()), [12])
        self._AssertValid(st)

    def test_SolidAndSheetInOneFile(self):
        """A file holding a solid and a sheet converts both."""
        st = self._Convert(_MakeSheetStep(withBox=True), "solid_and_sheet")
        regions = sorted(tuple(p.GetAttribute("region:type").Get()) for p in self._Breps(st))
        self.assertEqual(regions, [("voidRegion",), ("voidRegion", "solidRegion")])
        self._AssertValid(st)

    def test_ExtentHoldsCurvedGeometry(self):
        """brep:extent bounds the geometry, not only the vertices: a circle
        edge's arc, a sphere face's bulge, NURBS control hulls."""
        b = {"verts": [(10.0, 0.0, 0.0)],
             "edges": [{"ctok": "BrepCurve3dCircleAPI", "rng": (0.0, 2 * math.pi),
                        "geom": {"center": (0.0, 0.0, 0.0), "axis": (0.0, 0.0, 1.0),
                                 "refDirection": (1.0, 0.0, 0.0), "radius": 10.0}},
                       {"ctok": "BrepCurve3dNurbAPI", "rng": (0.0, 1.0),
                        "geom": {"nurb": True, "controlVertices": [(0.0, -30.0, 0.0)]}}],
             "faces": [{"stok": "BrepSurfaceSphereAPI",
                        "geom": {"center": (0.0, 0.0, 50.0), "axis": (0.0, 0.0, 1.0),
                                 "refDirection": (1.0, 0.0, 0.0), "radius": 5.0}},
                       {"stok": "BrepSurfaceNurbAPI",
                        "geom": {"nurb": True, "controlVertices": [(0.0, 0.0, -20.0)]}}]}
        lo, hi = stepToUsdSolid.local_extent(stepToUsdSolid.extent_hull(b))
        for k, (want_lo, want_hi) in enumerate(((-10.0, 10.0), (-30.0, 10.0), (-20.0, 55.0))):
            self.assertLessEqual(lo[k], want_lo + 1e-9)
            self.assertGreaterEqual(hi[k], want_hi - 1e-9)

    def test_RootShapeOnce(self):
        """An assembly root's own body is written once when two
        SHAPE_DEFINITION_REPRESENTATIONs reach it, whether a
        SHAPE_REPRESENTATION_RELATIONSHIP joins their representations or the
        solid is listed in both."""
        for layout in ("joined", "unjoined"):
            st = self._Convert(_MakeBoxStep(rep1IsParent=False, rootBox=layout),
                               "root_box_" + layout)
            names = [p.GetParent().GetName() for p in self._Breps(st)]
            self.assertEqual(sum(1 for n in names if n.startswith("rig")), 1,
                             "%s: %s" % (layout, names))
            self.assertEqual(len(names), 4, "%s: %s" % (layout, names))

    def _LoopRuns(self, stage):
        """Per face, per loop, the (start, end) vertex positions of each
        edgeuse in loop order, reading an edgeuse's direction from its
        orientation against its edge."""
        prim = self._Breps(stage)[0]
        get = lambda name: list(prim.GetAttribute(name).Get())
        pos = [tuple(round(c, 9) for c in p)
               for p in get("brep:vertexPoint:point:position")]
        ends = get("edge:vertexIndices")
        loopCount, euCount = get("face:loopCount"), get("loop:edgeuseCount")
        euEdge, euOrient = get("edgeuse:edgeIndex"), get("edgeuse:orientationType")
        faces, li, eo = [], 0, 0
        for count in loopCount:
            loops = []
            for _ in range(count):
                run = []
                for j in range(eo, eo + euCount[li]):
                    a, b = ends[euEdge[j]]
                    if euOrient[j] == "opposite":
                        a, b = b, a
                    run.append((pos[a], pos[b]))
                loops.append(run)
                li, eo = li + 1, eo + euCount[li]
            faces.append(loops)
        return faces

    def test_EdgeCurveAgainstItsEdge(self):
        """An EDGE_CURVE whose curve runs against the edge (same_sense .F.):
        ORIENTED_EDGE's flag is relative to the edge and the USD edge runs
        along the curve, so the two compose. Written that way, eight of the
        box's twelve edges leave every loop closed and running as in the
        plain box."""
        stage = self._Convert(_MakeBoxStep(curvesAgainst=True), "box_curves_against")
        runs = self._LoopRuns(stage)
        for fi, loops in enumerate(runs):
            for run in loops:
                self.assertEqual(
                    [], [k for k in range(len(run)) if run[k][1] != run[(k + 1) % len(run)][0]],
                    "face %d: loop does not close" % fi)
        self.assertEqual(runs, self._LoopRuns(self._stage))
        self._AssertValid(stage)

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
