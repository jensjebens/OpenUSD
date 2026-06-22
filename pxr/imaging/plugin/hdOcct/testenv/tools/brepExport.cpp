// SPDX-License-Identifier: Apache-2.0
// brepExport: build an OCCT shape and emit it as a UsdSolid BrepArray .usda.
//
// This is the seed of the C4 STEP->UsdSolid producer: it converts an OCCT
// TopoDS_Shape (here, parametric primitives) into the flat-packed BrepArray
// schema (Proposal #109) by extracting NURBS surfaces, 3D edge curves, and —
// critically — the authored 2D trim pcurves (BRep_Tool::CurveOnSurface), so the
// pcurves live in the SAME parameter space as their face's surface. That
// consistency is what the hand-authored fixtures lacked.
//
// Topology is emitted as a "fan": one edge per edgeuse, pcurve per edgeuse in
// loop/face order. The hdOcct builder's trim path builds each face
// independently from its surface + per-edgeuse pcurves, so this is sufficient
// and robust (no global radial-edge edge-sharing needed for tessellation).

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepBuilderAPI_NurbsConvert.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <ShapeUpgrade_ShapeDivideClosed.hxx>
#include <BRepLib.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Wire.hxx>
#include <TopAbs_Orientation.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom2d_BSplineCurve.hxx>
#include <Geom_BSplineCurve.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Geom2d_TrimmedCurve.hxx>
#include <Standard_Failure.hxx>
#include <Geom2dConvert.hxx>
#include <GeomConvert.hxx>
#include <gp_Pnt.hxx>
#include <gp_Pnt2d.hxx>
#include <gp_Pln.hxx>
#include <gp_Circ.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>

#include <TColStd_Array1OfReal.hxx>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>

// -------- accumulating arrays for the BrepArray --------
struct Out {
    std::vector<int> faceLoopCount;        // per face
    std::vector<std::array<double,4>> faceRange; // umin,umax,vmin,vmax per face
    std::vector<std::string> faceuseOrient; // 2 per face
    std::vector<int> loopEdgeuseCount;     // per loop
    std::vector<int> edgeuseEdgeIndex;     // per edgeuse (== identity here)
    // geometry, per face (surfaces) and per edgeuse (edges + pcurves)
    struct Surf { int uo,vo,un,vn; std::vector<double> uk,vk,w; std::vector<std::array<double,3>> cp; };
    struct Crv3 { int order,n; std::vector<double> k,w; std::vector<std::array<double,3>> cp; };
    struct Crv2 { int order,n; std::vector<double> k,w; std::vector<std::array<double,2>> cp; };
    std::vector<Surf> surfaces;            // per face
    std::vector<Crv3> edge3d;              // per edgeuse
    std::vector<Crv2> curveUv;             // per edgeuse
    std::vector<std::array<double,3>> verts; // 2 per edgeuse
    std::vector<std::array<int,2>> edgeVtx;  // per edgeuse
    std::vector<std::array<double,2>> edgeRange; // per edgeuse
};

static Out::Surf extractSurface(const TopoDS_Face& face) {
    Out::Surf s{};
    Handle(Geom_Surface) gs = BRep_Tool::Surface(face);
    Handle(Geom_BSplineSurface) bs = Handle(Geom_BSplineSurface)::DownCast(gs);
    if (bs.IsNull()) bs = GeomConvert::SurfaceToBSplineSurface(gs);
    s.un = bs->NbUPoles(); s.vn = bs->NbVPoles();
    s.uo = bs->UDegree()+1; s.vo = bs->VDegree()+1;
    // flat knot sequences (length = poles + order)
    TColStd_Array1OfReal uks(1, s.un + s.uo); bs->UKnotSequence(uks);
    TColStd_Array1OfReal vks(1, s.vn + s.vo); bs->VKnotSequence(vks);
    for (int i = uks.Lower(); i <= uks.Upper(); ++i) s.uk.push_back(uks.Value(i));
    for (int i = vks.Lower(); i <= vks.Upper(); ++i) s.vk.push_back(vks.Value(i));
    // poles + weights, row-major u-major: idx = u*vn + v
    bool rational = bs->IsURational() || bs->IsVRational();
    for (int u = 1; u <= s.un; ++u)
        for (int v = 1; v <= s.vn; ++v) {
            gp_Pnt p = bs->Pole(u, v);
            s.cp.push_back({p.X(), p.Y(), p.Z()});
            s.w.push_back(rational ? bs->Weight(u, v) : 1.0);
        }
    return s;
}

static Out::Crv3 extractCurve3d(Handle(Geom_BSplineCurve) bc) {
    Out::Crv3 c{};
    c.order = bc->Degree()+1; c.n = bc->NbPoles();
    TColStd_Array1OfReal ks(1, c.n + c.order); bc->KnotSequence(ks);
    for (int i = ks.Lower(); i <= ks.Upper(); ++i) c.k.push_back(ks.Value(i));
    bool rational = bc->IsRational();
    for (int i = 1; i <= c.n; ++i) {
        gp_Pnt p = bc->Pole(i);
        c.cp.push_back({p.X(), p.Y(), p.Z()});
        c.w.push_back(rational ? bc->Weight(i) : 1.0);
    }
    return c;
}

static Out::Crv2 extractCurve2d(Handle(Geom2d_BSplineCurve) bc) {
    Out::Crv2 c{};
    c.order = bc->Degree()+1; c.n = bc->NbPoles();
    TColStd_Array1OfReal ks(1, c.n + c.order); bc->KnotSequence(ks);
    for (int i = ks.Lower(); i <= ks.Upper(); ++i) c.k.push_back(ks.Value(i));
    bool rational = bc->IsRational();
    for (int i = 1; i <= c.n; ++i) {
        gp_Pnt2d p = bc->Pole(i);
        c.cp.push_back({p.X(), p.Y()});
        c.w.push_back(rational ? bc->Weight(i) : 1.0);
    }
    return c;
}

// Append one face (with all its wires) to the Out arrays.
static void addFace(Out& o, const TopoDS_Face& face) {
    // surface
    o.surfaces.push_back(extractSurface(face));
    // face range
    Standard_Real u0,u1,v0,v1; BRepTools::UVBounds(face, u0,u1,v0,v1);
    o.faceRange.push_back({u0,u1,v0,v1});
    // faceuse orientation: REVERSED -> outward against natural normal
    bool rev = (face.Orientation() == TopAbs_REVERSED);
    o.faceuseOrient.push_back(rev ? "opposite" : "same");
    o.faceuseOrient.push_back(rev ? "same" : "opposite");

    // wires: outer first, then holes
    TopoDS_Wire outer = BRepTools::OuterWire(face);
    std::vector<TopoDS_Wire> wires;
    wires.push_back(outer);
    for (TopExp_Explorer we(face, TopAbs_WIRE); we.More(); we.Next()) {
        TopoDS_Wire w = TopoDS::Wire(we.Current());
        if (!w.IsSame(outer)) wires.push_back(w);
    }
    o.faceLoopCount.push_back((int)wires.size());

    for (const TopoDS_Wire& w : wires) {
        // Collect this loop's oriented curves (2D pcurve + 3D), in traversal order.
        std::vector<Handle(Geom2d_BSplineCurve)> p2;
        std::vector<Handle(Geom_BSplineCurve)> p3;
        for (BRepTools_WireExplorer wexp(w, face); wexp.More(); wexp.Next()) {
            const TopoDS_Edge& edge = wexp.Current();
            try {
                Standard_Real f2,l2;
                Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(edge, face, f2, l2);
                if (pc.IsNull()) continue;
                Standard_Real f3,l3;
                Handle(Geom_Curve) c3 = BRep_Tool::Curve(edge, f3, l3);
                if (c3.IsNull()) continue;
                // Trim to the edge's range FIRST (handles full/periodic basis
                // curves), then convert to BSpline.
                Handle(Geom2d_BSplineCurve) bpc = Geom2dConvert::CurveToBSplineCurve(
                    new Geom2d_TrimmedCurve(pc, f2, l2));
                Handle(Geom_BSplineCurve) bc3 = GeomConvert::CurveToBSplineCurve(
                    new Geom_TrimmedCurve(c3, f3, l3));
                if (edge.Orientation() == TopAbs_REVERSED) { bpc->Reverse(); bc3->Reverse(); }
                p2.push_back(bpc); p3.push_back(bc3);
            } catch (const Standard_Failure& e) {
                std::fprintf(stderr, "  skip edge: %s\n", e.GetMessageString());
            }
        }
        // Force the loop CCW in UV (positive signed area). The builder bounds
        // the face by its outer wire (needs CCW) and reverses inner wires to
        // cut holes — so EVERY authored loop must be CCW regardless of the
        // face's 3D orientation. Shoelace over all 2D control points in order.
        double signedA = 0.0; std::vector<gp_Pnt2d> poly;
        for (auto& c : p2) for (int i=1;i<=c->NbPoles();++i) poly.push_back(c->Pole(i));
        for (size_t i=0;i+1<poly.size();++i)
            signedA += poly[i].X()*poly[i+1].Y() - poly[i+1].X()*poly[i].Y();
        if (!poly.empty())
            signedA += poly.back().X()*poly.front().Y() - poly.front().X()*poly.back().Y();
        if (signedA < 0) {
            std::reverse(p2.begin(), p2.end()); std::reverse(p3.begin(), p3.end());
            for (auto& c : p2) c->Reverse();
            for (auto& c : p3) c->Reverse();
        }
        int euCount = 0;
        for (size_t i=0;i<p2.size();++i) {
            o.curveUv.push_back(extractCurve2d(p2[i]));
            o.edge3d.push_back(extractCurve3d(p3[i]));
            gp_Pnt ps = p3[i]->Value(p3[i]->FirstParameter());
            gp_Pnt pe = p3[i]->Value(p3[i]->LastParameter());
            int vi = (int)o.verts.size();
            o.verts.push_back({ps.X(),ps.Y(),ps.Z()});
            o.verts.push_back({pe.X(),pe.Y(),pe.Z()});
            o.edgeVtx.push_back({vi, vi+1});
            o.edgeRange.push_back({p3[i]->FirstParameter(), p3[i]->LastParameter()});
            o.edgeuseEdgeIndex.push_back((int)o.edge3d.size()-1);
            ++euCount;
        }
        o.loopEdgeuseCount.push_back(euCount);
    }
}

static std::string fnum(double x) {
    if (std::fabs(x - std::round(x)) < 1e-12 && std::fabs(x) < 1e15) {
        char b[32]; std::snprintf(b, sizeof b, "%lld", (long long)std::llround(x)); return b;
    }
    char b[64]; std::snprintf(b, sizeof b, "%.12g", x); return b;
}

static void emit(const Out& o, const std::string& prim, const std::string& path,
                 const std::string& doc) {
    std::ofstream f(path);
    auto P3 = [&](const std::array<double,3>& p){ return "("+fnum(p[0])+", "+fnum(p[1])+", "+fnum(p[2])+")"; };
    auto P2 = [&](const std::array<double,2>& p){ return "("+fnum(p[0])+", "+fnum(p[1])+")"; };
    size_t nFaces = o.faceLoopCount.size();
    size_t nEU = o.edgeuseEdgeIndex.size();
    f << "#usda 1.0\n(\n    defaultPrim = \"World\"\n    metersPerUnit = 0.01\n    upAxis = \"Y\"\n";
    f << "    doc = \"\"\"" << doc << "\"\"\"\n)\n\n";
    f << "def Xform \"World\"\n{\n    def BrepArray \"" << prim << "\"\n    {\n";
    auto line = [&](const std::string& s){ f << "        " << s << "\n"; };
    auto arrTok = [&](const std::string& nm, const std::vector<std::string>& v){
        std::ostringstream s; s << "uniform token[] " << nm << " = [";
        for (size_t i=0;i<v.size();++i){ s<<"\""<<v[i]<<"\""; if(i+1<v.size())s<<", "; } s<<"]"; line(s.str()); };
    auto arrU = [&](const std::string& nm, const std::vector<int>& v){
        std::ostringstream s; s << "uniform uint[] " << nm << " = [";
        for (size_t i=0;i<v.size();++i){ s<<v[i]; if(i+1<v.size())s<<", "; } s<<"]"; line(s.str()); };
    auto arrD = [&](const std::string& nm, const std::vector<double>& v){
        std::ostringstream s; s << "uniform double[] " << nm << " = [";
        for (size_t i=0;i<v.size();++i){ s<<fnum(v[i]); if(i+1<v.size())s<<", "; } s<<"]"; line(s.str()); };

    // topology (void-included form, issue #68): the infinite exterior void
    // region is first, then the solid; faceuses are grouped by shell with an
    // explicit faceuse:faceIndex map.
    arrU("brep:regionCount", {2});
    arrU("region:shellCount", {1, 1});
    arrTok("region:type", {"voidRegion", "solidRegion"});
    arrU("shell:faceuseCount", {(int)nFaces, (int)nFaces});
    arrU("shell:wireEdgeCount", {0, 0});
    { std::vector<int> lc(o.faceLoopCount); arrU("face:loopCount", lc); }
    { std::vector<std::string> st(nFaces, "BrepSurfaceNurbAPI"); arrTok("face:surfaceType", st); }
    { std::vector<std::string> tt(nFaces, "general"); arrTok("face:trimType", tt); }
    { std::ostringstream s; s<<"uniform double2[] face:range = [";
      for (size_t i=0;i<o.faceRange.size();++i){ auto&r=o.faceRange[i];
        s<<"("<<fnum(r[0])<<", "<<fnum(r[1])<<"), ("<<fnum(r[2])<<", "<<fnum(r[3])<<")";
        if(i+1<o.faceRange.size())s<<", "; } s<<"]"; line(s.str()); }
    arrU("loop:edgeuseCount", o.loopEdgeuseCount);
    { std::vector<int> lv(o.loopEdgeuseCount.size(),0); arrU("loop:vertexIndex", lv); }
    arrU("edgeuse:edgeIndex", o.edgeuseEdgeIndex);
    { std::vector<std::string> eo(nEU,"same"); arrTok("edgeuse:orientationType", eo); }
    // De-interleave the per-face [outward, complement] pairs into the void
    // shell (outward refs) then the solid shell (complements), and emit the
    // faceuse:faceIndex map (each shell lists faces 0..nFaces-1 in order).
    { std::vector<std::string> fo; std::vector<int> fidx;
      for (size_t fi = 0; fi < nFaces; ++fi) fo.push_back(o.faceuseOrient[2*fi]);
      for (size_t fi = 0; fi < nFaces; ++fi) fo.push_back(o.faceuseOrient[2*fi+1]);
      for (int pass = 0; pass < 2; ++pass)
          for (size_t fi = 0; fi < nFaces; ++fi) fidx.push_back((int)fi);
      arrTok("faceuse:orientationType", fo);
      arrU("faceuse:faceIndex", fidx); }
    { std::vector<int> nr(nEU); for(size_t i=0;i<nEU;++i)nr[i]=(int)i; arrU("edgeuse:nextRadialEUIndex", nr); }
    { std::vector<std::string> te(nEU,"topEntry"); arrTok("edgeuse:thisRadialEntryType", te); }

    // edges
    { std::vector<std::string> ct(o.edge3d.size(),"BrepCurve3dNurbAPI"); arrTok("edge:curveType", ct); }
    { std::vector<double> er; for(auto&r:o.edgeRange){er.push_back(r[0]);er.push_back(r[1]);} arrD("edge:range", er); }
    { std::ostringstream s; s<<"uniform int2[] edge:vertexIndices = [";
      for(size_t i=0;i<o.edgeVtx.size();++i){ s<<"("<<o.edgeVtx[i][0]<<", "<<o.edgeVtx[i][1]<<")";
        if(i+1<o.edgeVtx.size())s<<", ";} s<<"]"; line(s.str()); }

    // vertices
    { std::vector<std::string> pt(o.verts.size(),"BrepPointAPI"); arrTok("vertex:pointType", pt); }
    { std::ostringstream s; s<<"uniform point3d[] brep:vertexPoint:point:position = [";
      for(size_t i=0;i<o.verts.size();++i){ s<<P3(o.verts[i]); if(i+1<o.verts.size())s<<", ";} s<<"]"; line(s.str()); }

    // surfaces (per face)
    { std::vector<int> a; for(auto&s:o.surfaces)a.push_back(s.uo); arrU("brep:surface:nurb:uOrder",a); }
    { std::vector<int> a; for(auto&s:o.surfaces)a.push_back(s.vo); arrU("brep:surface:nurb:vOrder",a); }
    { std::vector<int> a; for(auto&s:o.surfaces)a.push_back(s.un); arrU("brep:surface:nurb:uVertexCount",a); }
    { std::vector<int> a; for(auto&s:o.surfaces)a.push_back(s.vn); arrU("brep:surface:nurb:vVertexCount",a); }
    { std::vector<double> a; for(auto&s:o.surfaces)for(double k:s.uk)a.push_back(k); arrD("brep:surface:nurb:uKnots",a); }
    { std::vector<double> a; for(auto&s:o.surfaces)for(double k:s.vk)a.push_back(k); arrD("brep:surface:nurb:vKnots",a); }
    { std::ostringstream s; s<<"uniform point3d[] brep:surface:nurb:controlVertices = [";
      bool first=true; for(auto&su:o.surfaces)for(auto&p:su.cp){ if(!first)s<<", "; s<<P3(p); first=false;} s<<"]"; line(s.str()); }
    { std::vector<double> a; for(auto&s:o.surfaces)for(double w:s.w)a.push_back(w); arrD("brep:surface:nurb:weights",a); }

    // edge 3d curves (per edgeuse)
    { std::vector<int> a; for(auto&c:o.edge3d)a.push_back(c.order); arrU("brep:edge3dNurb:curve3d:nurb:order",a); }
    { std::vector<int> a; for(auto&c:o.edge3d)a.push_back(c.n); arrU("brep:edge3dNurb:curve3d:nurb:vertexCount",a); }
    { std::vector<double> a; for(auto&c:o.edge3d)for(double k:c.k)a.push_back(k); arrD("brep:edge3dNurb:curve3d:nurb:knots",a); }
    { std::ostringstream s; s<<"uniform point3d[] brep:edge3dNurb:curve3d:nurb:controlVertices = [";
      bool first=true; for(auto&c:o.edge3d)for(auto&p:c.cp){ if(!first)s<<", "; s<<P3(p); first=false;} s<<"]"; line(s.str()); }
    { std::vector<double> a; for(auto&c:o.edge3d)for(double w:c.w)a.push_back(w); arrD("brep:edge3dNurb:curve3d:nurb:weights",a); }

    // uv trim curves (per edgeuse)
    { std::vector<int> a; for(auto&c:o.curveUv)a.push_back(c.order); arrU("brep:curveUv:nurb:order",a); }
    { std::vector<int> a; for(auto&c:o.curveUv)a.push_back(c.n); arrU("brep:curveUv:nurb:vertexCount",a); }
    { std::vector<double> a; for(auto&c:o.curveUv)for(double k:c.k)a.push_back(k); arrD("brep:curveUv:nurb:knots",a); }
    { std::ostringstream s; s<<"uniform double2[] brep:curveUv:nurb:controlVertices = [";
      bool first=true; for(auto&c:o.curveUv)for(auto&p:c.cp){ if(!first)s<<", "; s<<P2(p); first=false;} s<<"]"; line(s.str()); }
    { std::vector<double> a; for(auto&c:o.curveUv)for(double w:c.w)a.push_back(w); arrD("brep:curveUv:nurb:weights",a); }

    arrD("brep:intersectTol3d", {1e-6});
    f << "    }\n}\n";
}

// ---- shape builders ----
static TopoDS_Shape makeCubeWithHole() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10,10,10).Shape();
    gp_Ax2 ax(gp_Pnt(5,5,-1), gp_Dir(0,0,1));
    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(ax, 3.0, 12.0).Shape();
    return BRepAlgoAPI_Cut(box, cyl).Shape();
}
static TopoDS_Shape makeFilletedCubeWithHole() {
    TopoDS_Shape box = BRepPrimAPI_MakeBox(10,10,10).Shape();
    BRepFilletAPI_MakeFillet fil(box);
    for (TopExp_Explorer ex(box, TopAbs_EDGE); ex.More(); ex.Next())
        fil.Add(1.0, TopoDS::Edge(ex.Current()));
    TopoDS_Shape filleted = fil.Shape();
    gp_Ax2 ax(gp_Pnt(5,5,-2), gp_Dir(0,0,1));
    TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(ax, 3.0, 14.0).Shape();
    return BRepAlgoAPI_Cut(filleted, cyl).Shape();
}
static TopoDS_Shape makePlaneWithHole() {
    gp_Pln pln(gp_Pnt(0,0,0), gp_Dir(0,0,1));
    BRepBuilderAPI_MakePolygon poly(gp_Pnt(-10,-10,0), gp_Pnt(10,-10,0),
                                    gp_Pnt(10,10,0), gp_Pnt(-10,10,0), Standard_True);
    TopoDS_Wire outer = poly.Wire();
    gp_Circ circ(gp_Ax2(gp_Pnt(0,0,0), gp_Dir(0,0,1)), 3.0);
    TopoDS_Edge ce = BRepBuilderAPI_MakeEdge(circ).Edge();
    TopoDS_Wire hole = BRepBuilderAPI_MakeWire(ce).Wire();
    BRepBuilderAPI_MakeFace mf(pln, outer);
    hole.Reverse();
    mf.Add(hole);
    return mf.Face();
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: brepExport <plane|cube|filleted> <out.usda>\n"); return 2; }
    std::string what = argv[1], path = argv[2];
    TopoDS_Shape shape; std::string prim, doc;
    if (what == "cube") { shape = makeCubeWithHole();
        prim = "CubeWithHole"; doc = "Box 10^3 with r=3 through-hole along Z (OCCT-generated)."; }
    else if (what == "filleted") { shape = makeFilletedCubeWithHole();
        prim = "FilletedCubeWithHole"; doc = "Box 10^3, fillet r=1, r=3 through-hole along Z (OCCT-generated)."; }
    else if (what == "plane") { shape = makePlaneWithHole();
        prim = "PlaneWithHole"; doc = "20x20 plane sheet with r=3 circular hole (OCCT-generated)."; }
    else { std::fprintf(stderr, "unknown shape %s\n", what.c_str()); return 2; }

    // Convert all geometry to NURBS, then split closed/periodic faces so every
    // face has a clean open boundary (no seam pcurve ambiguity).
    BRepBuilderAPI_NurbsConvert nc(shape); shape = nc.Shape();
    ShapeUpgrade_ShapeDivideClosed div(shape); div.Perform(); shape = div.Result();
    BRepLib::BuildCurves3d(shape);

    Out o;
    for (TopExp_Explorer fx(shape, TopAbs_FACE); fx.More(); fx.Next())
        addFace(o, TopoDS::Face(fx.Current()));

    emit(o, prim, path, doc);
    std::fprintf(stderr, "wrote %s | faces=%zu edgeuses=%zu verts=%zu\n",
                 path.c_str(), o.faceLoopCount.size(), o.edgeuseEdgeIndex.size(), o.verts.size());
    return 0;
}
