#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate a CLEAN depressed-plane UsdSolid BrepArray fixture.

A 20x20 flat sheet (z=0) with a circular region (r=3 at (10,10)) recessed into a
cylindrical pocket of depth 5 and a flat circular bottom cap at z=-5.

Everything is authored as NURBS (surfaces: brep:surface:nurb; 3D curves:
brep:edge3dNurb; trim curves: brep:curveUv:nurb) so the existing hdOcct builder
readers consume it, and — critically — every face's curveUv pcurves live in the
SAME (u,v) space as that face's NURBS surface (the cube fixture's bug was a
mismatch). The pcurve path (build edge FROM c2d) then meshes it correctly.

--top-only emits just the plane-with-hole sheet (1 face) to validate the fix.
"""
import math, sys

CX, CY, R, DEPTH = 10.0, 10.0, 3.0, 5.0

# ---- rational-quadratic full circle (3x120deg arcs), center (cx,cy), radius r
# in a 2D space. Returns (cps[7] as (x,y)), weights[7], knots[10] (explicit mult).
def circle2d(cx, cy, r):
    s = r / math.cos(math.radians(60))   # shoulder distance = 2r
    pts = []
    for k in range(3):
        a_on = math.radians(120 * k)            # on-circle angle
        a_sh = math.radians(120 * k + 60)       # shoulder bisector
        pts.append((cx + r*math.cos(a_on), cy + r*math.sin(a_on)))
        pts.append((cx + s*math.cos(a_sh), cy + s*math.sin(a_sh)))
    pts.append(pts[0])                           # close
    w = [1.0, 0.5, 1.0, 0.5, 1.0, 0.5, 1.0]
    t = 2*math.pi
    knots = [0,0,0, t/3,t/3, 2*t/3,2*t/3, t,t,t]
    return pts, w, knots

# 3D circle in plane z=zc, center (cx,cy)
def circle3d(cx, cy, zc, r):
    p2, w, k = circle2d(cx, cy, r)
    return [(x, y, zc) for (x, y) in p2], w, k

def fmt(x):
    x = float(x)
    return str(int(x)) if x == int(x) else repr(round(x, 10))

class Brep:
    def __init__(self):
        self.verts=[]; self.edges=[]; self.edgeuses=[]; self.loops=[]
        self.faces=[]; self.faceuses=[]; self.shells=[]
        # geometry pools
        self.surf=[]            # list of dict(nurb surface)
        self.c3d_nurb=[]        # list of dict(3d nurb curve) per edge in edge order
        self.cuv=[]             # list of dict(2d nurb) per edgeuse in edgeuse order

    # ---- emit ----
    def usda(self, name):
        L=["#usda 1.0",""]
        L.append('def Xform "World"\n(\n    defaultPrim = "World"\n    upAxis = "Y"\n)\n{')
        L.append(f'    def BrepArray "{name}" (')
        L.append('        prepend apiSchemas = ["BrepPointAPI:vertexPoint", '
                 '"BrepCurve3dNurbAPI:edge3dNurb", "BrepCurveUvNurbAPI", '
                 '"BrepSurfaceNurbAPI"]')
        L.append('    )')
        L.append('    {')
        a=L.append
        def arr(t,n,vals): a(f"        uniform {t}[] {n} = [{', '.join(vals)}]")
        def pts(ps): return [f"({fmt(p[0])}, {fmt(p[1])}, {fmt(p[2])})" for p in ps]
        def pts2(ps): return [f"({fmt(p[0])}, {fmt(p[1])})" for p in ps]
        def nums(xs): return [fmt(x) for x in xs]
        def toks(xs): return [f'"{x}"' for x in xs]
        def i2(xs): return [f"({a_},{b_})" for (a_,b_) in xs]

        # A depressed plane is an open sheet (a plane with a cylindrical
        # pocket), not a closed solid, so it is modelled as a SINGLE voidRegion
        # whose one shell holds every faceuse. Labelling any shell a solidRegion
        # would make BrepArraySolidClosure (BA.590) flag the sheet's single-use
        # boundary edges as an open solid. self.faceuses holds interleaved
        # [outward, complement] pairs per face; both orientations of every face
        # live in the one void shell (faceuse:faceIndex repeats the face list).
        nF=len(self.faceuses)//2
        outward=[self.faceuses[2*i]["o"] for i in range(nF)]
        complement=[self.faceuses[2*i+1]["o"] for i in range(nF)]
        # brep-level scalars (one Brep). extent = geometric bbox of the vertices.
        xs=[p[0] for p in self.verts]; ys=[p[1] for p in self.verts]
        zs=[p[2] for p in self.verts]
        ext=[(min(xs),min(ys),min(zs)),(max(xs),max(ys),max(zs))]
        arr("double3","brep:extent",pts(ext))
        arr("double","brep:intersectTol3d",["0.000001"])
        arr("uint","brep:regionCount",["1"])
        arr("uint","region:shellCount",["1"])
        arr("token","region:type",toks(["voidRegion"]))
        arr("uint","shell:faceuseCount",[str(2*nF)])
        arr("uint","shell:wireEdgeCount",["0"])
        arr("token","shell:pointType",toks(["none"]))
        arr("uint","faceuse:faceIndex",nums(list(range(nF))+list(range(nF))))
        arr("token","faceuse:orientationType",toks(outward+complement))
        arr("uint","face:loopCount",nums([f["lc"] for f in self.faces]))
        arr("token","face:surfaceType",toks(["BrepSurfaceNurbAPI"]*len(self.faces)))
        arr("token","face:trimType",toks(["general"]*len(self.faces)))
        # face:range = 2 double2 per face: (uMin,vMin),(uMax,vMax) -- the pair
        # ordering BA.155/160 (BrepArrayRanges) checks for non-degeneracy.
        fr=[]
        for f in self.faces:
            (u0,u1),(v0,v1)=f["range"]
            fr+= [f"({fmt(u0)}, {fmt(v0)})", f"({fmt(u1)}, {fmt(v1)})"]
        arr("double2","face:range",fr)
        arr("uint","loop:edgeuseCount",nums(self.loops))
        arr("uint","loop:vertexIndex",["0"]*len(self.loops))
        arr("uint","edgeuse:edgeIndex",nums([e["edge"] for e in self.edgeuses]))
        arr("uint","edgeuse:nextRadialEUIndex",nums([e["next"] for e in self.edgeuses]))
        arr("token","edgeuse:orientationType",toks([e["o"] for e in self.edgeuses]))
        # thisRadialEntryType: the first edgeuse to reference an edge enters the
        # radial ring at "topEntry"; its twin enters at "bottomEntry". Single-use
        # boundary edges stay "topEntry". (Per-edgeuse e["entry"] documents intent.)
        _seen=set(); _entry=[]
        for e in self.edgeuses:
            _entry.append("bottomEntry" if e["edge"] in _seen else "topEntry")
            _seen.add(e["edge"])
        arr("token","edgeuse:thisRadialEntryType",toks(_entry))
        arr("int2","edge:vertexIndices",i2([e["v"] for e in self.edges]))
        arr("token","edge:curveType",toks(["BrepCurve3dNurbAPI"]*len(self.edges)))
        er=[]
        for e in self.edges: er+= [fmt(e["range"][0]), fmt(e["range"][1])]
        arr("double","edge:range",er)
        # No wire (dangling) edges on this sheet; author the arrays empty so the
        # wireEdge family is present and self-consistent (sum(shell:wireEdgeCount)=0).
        arr("token","wireEdge:curveType",[])
        arr("int2","wireEdge:vertexIndices",[])
        arr("double","wireEdge:range",[])
        arr("token","vertex:pointType",toks(["BrepPointAPI"]*len(self.verts)))
        arr("point3d","brep:vertexPoint:point:position",pts(self.verts))

        # ---- surfaces (nurb), packed in face order ----
        arr("uint","brep:surface:nurb:uOrder",nums([s["uo"] for s in self.surf]))
        arr("uint","brep:surface:nurb:vOrder",nums([s["vo"] for s in self.surf]))
        arr("uint","brep:surface:nurb:uVertexCount",nums([s["un"] for s in self.surf]))
        arr("uint","brep:surface:nurb:vVertexCount",nums([s["vn"] for s in self.surf]))
        arr("double","brep:surface:nurb:uKnots",nums([k for s in self.surf for k in s["uk"]]))
        arr("double","brep:surface:nurb:vKnots",nums([k for s in self.surf for k in s["vk"]]))
        arr("point3d","brep:surface:nurb:controlVertices",pts([p for s in self.surf for p in s["cp"]]))
        arr("double","brep:surface:nurb:weights",nums([w for s in self.surf for w in s["w"]]))

        # ---- 3d edge curves (nurb), packed in edge order ----
        arr("uint","brep:edge3dNurb:curve3d:nurb:order",nums([c["o"] for c in self.c3d_nurb]))
        arr("uint","brep:edge3dNurb:curve3d:nurb:vertexCount",nums([c["n"] for c in self.c3d_nurb]))
        arr("double","brep:edge3dNurb:curve3d:nurb:knots",nums([k for c in self.c3d_nurb for k in c["k"]]))
        arr("point3d","brep:edge3dNurb:curve3d:nurb:controlVertices",pts([p for c in self.c3d_nurb for p in c["cp"]]))
        arr("double","brep:edge3dNurb:curve3d:nurb:weights",nums([w for c in self.c3d_nurb for w in c["w"]]))

        # ---- 2d trim curves (curveUv), packed in edgeuse order ----
        arr("uint","brep:curveUv:nurb:order",nums([c["o"] for c in self.cuv]))
        arr("uint","brep:curveUv:nurb:vertexCount",nums([c["n"] for c in self.cuv]))
        arr("double","brep:curveUv:nurb:knots",nums([k for c in self.cuv for k in c["k"]]))
        arr("double2","brep:curveUv:nurb:controlVertices",pts2([p for c in self.cuv for p in c["cp"]]))
        arr("double","brep:curveUv:nurb:weights",nums([w for c in self.cuv for w in c["w"]]))

        a("    }")
        a("}")
        return "\n".join(L)+"\n"

def line_nurb_3d(p0, p1):
    return dict(o=2, n=2, k=[0,0,1,1], cp=[p0,p1], w=[1,1])
def line_nurb_2d(p0, p1):
    return dict(o=2, n=2, k=[0,0,1,1], cp=[p0,p1], w=[1,1])
def rev2d(cp, w, k):
    # Reverse a 2D NURBS curve so its loop winds the other way. Used to author
    # inner/hole loops CW (the outer loop stays CCW), per the material-on-left
    # convention that SMLib and the OCCT/PRC converters expect. Reverse the
    # control points and weights; reflect the knot vector about its span.
    a, bb = k[0], k[-1]
    return list(reversed(cp)), list(reversed(w)), [a + bb - x for x in reversed(k)]

def build_top_only():
    b=Brep()
    # plane surface: bilinear, UV [0,20]^2 -> 3D (u,v,0)
    b.surf.append(dict(uo=2,vo=2,un=2,vn=2,uk=[0,0,20,20],vk=[0,0,20,20],
        cp=[(0,0,0),(0,20,0),(20,0,0),(20,20,0)], w=[1,1,1,1]))
    # vertices: 4 corners + circle seam
    b.verts=[(0,0,0),(20,0,0),(20,20,0),(0,20,0),(CX+R,CY,0)]
    # edges: 4 square lines + 1 circle (edge order)
    b.edges=[dict(v=(0,1),range=(0,1)),dict(v=(1,2),range=(0,1)),
             dict(v=(2,3),range=(0,1)),dict(v=(3,0),range=(0,1)),
             dict(v=(4,4),range=(0,2*math.pi))]
    b.c3d_nurb=[line_nurb_3d((0,0,0),(20,0,0)),line_nurb_3d((20,0,0),(20,20,0)),
                line_nurb_3d((20,20,0),(0,20,0)),line_nurb_3d((0,20,0),(0,0,0))]
    cp3,w3,k3=circle3d(CX,CY,0,R); b.c3d_nurb.append(dict(o=3,n=7,k=k3,cp=cp3,w=w3))
    # 1 face, 2 loops: outer square (4 EU) + inner circle (1 EU)
    b.faces=[dict(lc=2,range=((0,20),(0,20)))]
    b.loops=[4,1]
    # edgeuses (edgeuse order = loop order): 4 square + 1 circle
    b.edgeuses=[dict(edge=0,o="same",next=0,entry="topEntry"),
                dict(edge=1,o="same",next=1,entry="topEntry"),
                dict(edge=2,o="same",next=2,entry="topEntry"),
                dict(edge=3,o="same",next=3,entry="topEntry"),
                dict(edge=4,o="opposite",next=4,entry="topEntry")]  # inner hole -> opposite
    # curveUv (one per edgeuse, in face UV [0,20]^2)
    b.cuv=[line_nurb_2d((0,0),(20,0)),line_nurb_2d((20,0),(20,20)),
           line_nurb_2d((20,20),(0,20)),line_nurb_2d((0,20),(0,0))]
    c2,w2,k2=circle2d(CX,CY,R); c2,w2,k2=rev2d(c2,w2,k2)  # inner hole loop -> CW
    b.cuv.append(dict(o=3,n=7,k=k2,cp=c2,w=w2))
    # sheet body: 1 face -> 2 faceuses, 1 shell, 1 region
    b.faceuses=[dict(face=0,o="same"),dict(face=0,o="opposite")]
    b.shells=[2]
    return b

def build_full():
    b=Brep()
    # ---- surfaces (face order: TOP, WALL, BOTTOM) ----
    # F0 TOP plane, UV [0,20]^2 -> (u,v,0)
    b.surf.append(dict(uo=2,vo=2,un=2,vn=2,uk=[0,0,20,20],vk=[0,0,20,20],
        cp=[(0,0,0),(0,20,0),(20,0,0),(20,20,0)], w=[1,1,1,1]))
    # F1 WALL cylinder: u = rational circle (7 CP, deg2), v linear z=0..-5.
    ccp,cw,ck = circle3d(CX,CY,0,R)          # top rim ring (z=0)
    bcp = [(x,y,-DEPTH) for (x,y,_) in ccp]  # bottom ring (z=-5)
    wall_cp=[]; wall_w=[]
    for u in range(7):                        # u-major: idx=u*2+v
        wall_cp += [ccp[u], bcp[u]]
        wall_w  += [cw[u],  cw[u]]
    b.surf.append(dict(uo=3,vo=2,un=7,vn=2,uk=ck,vk=[0,0,DEPTH,DEPTH],
        cp=wall_cp, w=wall_w))
    # F2 BOTTOM plane at z=-5, UV [7,13]^2 -> (u,v,-5)
    b.surf.append(dict(uo=2,vo=2,un=2,vn=2,uk=[7,7,13,13],vk=[7,7,13,13],
        cp=[(7,7,-DEPTH),(7,13,-DEPTH),(13,7,-DEPTH),(13,13,-DEPTH)], w=[1,1,1,1]))
    # ---- vertices ----
    b.verts=[(0,0,0),(20,0,0),(20,20,0),(0,20,0),
             (CX+R,CY,0),(CX+R,CY,-DEPTH)]      # V4 rim seam, V5 bottom seam
    # ---- edges (shared): E0-3 square, E4 rim circle, E5 bottom circle, E6 seam ----
    b.edges=[dict(v=(0,1),range=(0,1)),dict(v=(1,2),range=(0,1)),
             dict(v=(2,3),range=(0,1)),dict(v=(3,0),range=(0,1)),
             dict(v=(4,4),range=(0,2*math.pi)),dict(v=(5,5),range=(0,2*math.pi)),
             dict(v=(4,5),range=(0,1))]
    b.c3d_nurb=[line_nurb_3d((0,0,0),(20,0,0)),line_nurb_3d((20,0,0),(20,20,0)),
                line_nurb_3d((20,20,0),(0,20,0)),line_nurb_3d((0,20,0),(0,0,0))]
    r0=circle3d(CX,CY,0,R); b.c3d_nurb.append(dict(o=3,n=7,k=r0[2],cp=r0[0],w=r0[1]))
    r1=circle3d(CX,CY,-DEPTH,R); b.c3d_nurb.append(dict(o=3,n=7,k=r1[2],cp=r1[0],w=r1[1]))
    b.c3d_nurb.append(line_nurb_3d((CX+R,CY,0),(CX+R,CY,-DEPTH)))
    # ---- faces / loops ----
    b.faces=[dict(lc=2,range=((0,20),(0,20))),          # TOP
             dict(lc=1,range=((0,2*math.pi),(0,DEPTH))), # WALL
             dict(lc=1,range=((7,13),(7,13)))]           # BOTTOM
    b.loops=[4,1, 4, 1]
    # ---- edgeuses (loop order) ----
    same=lambda e,nx: dict(edge=e,o="same",next=nx,entry="topEntry")
    b.edgeuses=[same(0,0),same(1,1),same(2,2),same(3,3),   # TOP outer (EU0-3)
                same(4,5),                                  # TOP hole (EU4) -> rim E4
                same(4,4),same(6,6),same(5,7),same(6,8),    # WALL (EU5-8): rim,seam,bot,seam
                same(5,4)]                                  # BOTTOM (EU9) -> bottom E5
    # radial pairing: EU4(top hole) <-> EU5(wall rim) share E4; EU7(wall bot)<->EU9(bottom) share E5
    b.edgeuses[4]["next"]=5; b.edgeuses[5]["next"]=4
    b.edgeuses[7]["next"]=9; b.edgeuses[9]["next"]=7
    b.edgeuses[6]["next"]=8; b.edgeuses[8]["next"]=6
    b.edgeuses[4]["o"]="opposite"   # TOP hole -> inner loop CW (material-on-left)
    # ---- curveUv (one per edgeuse, in each face's UV) ----
    cTop,wTop,kTop = circle2d(CX,CY,R)        # in TOP UV [0,20]
    cTop,wTop,kTop = rev2d(cTop,wTop,kTop)    # TOP hole -> inner loop CW
    cBot,wBot,kBot = circle2d(CX,CY,R)        # in BOTTOM UV [7,13] (same coords)
    D=DEPTH; TP=2*math.pi
    b.cuv=[line_nurb_2d((0,0),(20,0)),line_nurb_2d((20,0),(20,20)),
           line_nurb_2d((20,20),(0,20)),line_nurb_2d((0,20),(0,0)),
           dict(o=3,n=7,k=kTop,cp=cTop,w=wTop),             # TOP hole circle
           line_nurb_2d((0,0),(TP,0)),                       # WALL rim (v=0)
           line_nurb_2d((TP,0),(TP,D)),                      # WALL seam (u=2pi)
           line_nurb_2d((TP,D),(0,D)),                       # WALL bottom (v=D)
           line_nurb_2d((0,D),(0,0)),                        # WALL seam (u=0)
           dict(o=3,n=7,k=kBot,cp=cBot,w=wBot)]              # BOTTOM disk circle
    # ---- faceuses (2 per face) + shell/region ----
    b.faceuses=[dict(face=0,o="same"),dict(face=0,o="opposite"),
                dict(face=1,o="same"),dict(face=1,o="opposite"),
                dict(face=2,o="opposite"),dict(face=2,o="same")]
    b.shells=[6]
    return b

if __name__=="__main__":
    top = "--top-only" in sys.argv
    out = sys.argv[-1]
    b = build_top_only() if top else build_full()
    open(out,"w",newline="\n").write(b.usda("DepressedPlane"))
    print("wrote", out, "| faces", len(b.faces), "edgeuses", len(b.edgeuses),
          "faceuses", len(b.faceuses))
