// SPDX-License-Identifier: Apache-2.0
// brepExport: build an OCCT shape and emit it as a UsdSolid BrepArray .usda.
//
// This is the seed of the C4 STEP->UsdSolid producer: it converts an OCCT
// TopoDS_Shape (here, parametric primitives) into the flat-packed BrepArray
// schema (Proposal #109) by extracting NURBS surfaces, 3D edge curves, and —
// critically — the authored 2D trim pcurves (BRep_Tool::CurveOnSurface), so the
// pcurves live in the SAME parameter space as their face's surface.
//
// Topology PRESERVES OCCT's shared Radial-Edge structure: unique vertices and
// unique edges are collected with orientation-independent TopExp maps, so each
// edge is authored ONCE and every edgeuse references it via a SHARED
// edgeuse:edgeIndex. Edgeuses around an edge are stitched into a radial ring.
// This is what the BrepArraySolidClosure validator requires (no unshared-vertex
// / identity-radial-ring "soup"). Tessellation is unaffected: the hdOcct
// builder's NURBS trim path consumes one curveUv pcurve per edgeuse in global
// order (unchanged here) and addresses per-edge 3D data through
// edgeuse:edgeIndex, so sharing changes the indexing, not the geometry.

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
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
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
#include <map>
#include <string>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

// -------- accumulating arrays for the BrepArray --------
struct Out {
    // per face
    std::vector<int> faceLoopCount;
    std::vector<std::array<double,4>> faceRange; // umin,umax,vmin,vmax per face
    std::vector<std::string> faceuseOrient;      // 2 per face (natural, complement)
    // per loop
    std::vector<int> loopEdgeuseCount;
    // per EDGEUSE (global order, loop/face order, CCW-forced)
    std::vector<int> edgeuseEdgeIndex;           // SHARED -> unique edge index
    std::vector<std::string> edgeuseOrient;      // "same"/"opposite"
    std::vector<int> edgeuseNextRadial;          // filled after all faces
    std::vector<std::string> edgeuseRadialEntry; // filled after all faces
    // geometry
    struct Surf { int uo,vo,un,vn; std::vector<double> uk,vk,w; std::vector<std::array<double,3>> cp; };
    struct Crv3 { int order,n; std::vector<double> k,w; std::vector<std::array<double,3>> cp; };
    struct Crv2 { int order,n; std::vector<double> k,w; std::vector<std::array<double,2>> cp; };
    std::vector<Surf> surfaces;            // per face
    std::vector<Crv3> edge3d;              // per UNIQUE edge
    std::vector<Crv2> curveUv;             // per EDGEUSE
    std::vector<std::array<double,3>> verts; // per UNIQUE vertex
    std::vector<std::array<int,2>> edgeVtx;  // per UNIQUE edge (vertex indices)
    std::vector<std::array<double,2>> edgeRange; // per UNIQUE edge
    std::vector<bool> edgeDegenerate;      // per UNIQUE edge (placeholder line)
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

// Build a 2-pole placeholder line BSpline between two 3D points (for a
// degenerate / null-curve edge), so the per-edge arrays stay aligned.
static Out::Crv3 placeholderLine3d(const std::array<double,3>& a,
                                   const std::array<double,3>& b) {
    Out::Crv3 c{};
    c.order = 2; c.n = 2;
    c.k = {0.0, 0.0, 1.0, 1.0};
    c.cp = {a, b};
    c.w = {1.0, 1.0};
    return c;
}

// ---- per-shape sharing context (TopExp maps over the whole shape) ----
struct ShareCtx {
    TopTools_IndexedMapOfShape vmap; // unique vertices (orientation-independent)
    TopTools_IndexedMapOfShape emap; // unique edges
    // Per emap edge (0-based): degeneracy (null 3D curve) + raw geometry, kept
    // until the post-pass compacts out unused/degenerate edges.
    std::vector<bool> edgeDegenerate;
    std::vector<Out::Crv3> edgeCrv3;       // 3D curve per emap edge
    std::vector<std::array<double,2>> edgeRng; // range per emap edge
    std::vector<std::array<int,2>> edgeVtx;    // endpoint vertex indices per emap edge
    // For each emap edge index: the global edgeuse indices that reference it,
    // in discovery order. Used to stitch the radial ring after all faces.
    std::map<int, std::vector<int>> edgeToEUs;
};

// Append one face (with all its wires) to the Out arrays.
static void addFace(Out& o, ShareCtx& ctx, const TopoDS_Face& face) {
    // surface
    o.surfaces.push_back(extractSurface(face));
    // face range (corner-point form is emitted later in emit()).
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
        // Collect this loop's edgeuses, in traversal order. Each parallel array
        // entry describes ONE edgeuse: its 2D pcurve (curveUv), the SHARED
        // unique-edge index, and the orientation token.
        std::vector<Out::Crv2> p2;                   // curveUv per edgeuse
        std::vector<int> euEdgeIdx;                  // shared edge index per eu
        std::vector<std::string> euOrient;           // token per eu
        for (BRepTools_WireExplorer wexp(w, face); wexp.More(); wexp.Next()) {
            const TopoDS_Edge& edge = wexp.Current();
            try {
                int eidx = ctx.emap.FindIndex(edge) - 1; // 0-based SHARED index
                // Skip edgeuses on degenerate (null-3D-curve) seam/pole edges, as
                // the legacy fan path did. They carry no boundary geometry the
                // NURBS trim path needs, and authoring them as extra trim
                // segments perturbs the tessellation. The edge itself is dropped
                // by the post-pass compaction (so no orphan-edge error).
                if (eidx >= 0 && eidx < (int)ctx.edgeDegenerate.size() &&
                    ctx.edgeDegenerate[eidx]) {
                    continue;
                }
                Standard_Real f2,l2;
                Handle(Geom2d_Curve) pc = BRep_Tool::CurveOnSurface(edge, face, f2, l2);
                if (pc.IsNull()) continue;
                // Trim to the edge's range FIRST (handles full/periodic basis
                // curves), then convert to BSpline.
                Handle(Geom2d_BSplineCurve) bpc = Geom2dConvert::CurveToBSplineCurve(
                    new Geom2d_TrimmedCurve(pc, f2, l2));
                bool reversed = (edge.Orientation() == TopAbs_REVERSED);
                if (reversed) bpc->Reverse();
                p2.push_back(extractCurve2d(bpc));
                euEdgeIdx.push_back(eidx);
                // "opposite" when this edgeuse traverses the edge against its
                // natural (FORWARD) sense.
                euOrient.push_back(reversed ? "opposite" : "same");
            } catch (const Standard_Failure& e) {
                std::fprintf(stderr, "  skip edge: %s\n", e.GetMessageString());
            }
        }
        // Force the loop CCW in UV (positive signed area). The builder bounds
        // the face by its outer wire (needs CCW) and reverses inner wires to
        // cut holes — so EVERY authored loop must be CCW regardless of the
        // face's 3D orientation. Shoelace over all 2D control points in order.
        double signedA = 0.0; std::vector<std::array<double,2>> poly;
        for (auto& c : p2) for (auto& cp : c.cp) poly.push_back(cp);
        for (size_t i=0;i+1<poly.size();++i)
            signedA += poly[i][0]*poly[i+1][1] - poly[i+1][0]*poly[i][1];
        if (!poly.empty())
            signedA += poly.back()[0]*poly.front()[1] - poly.front()[0]*poly.back()[1];
        if (signedA < 0) {
            // Reverse the per-edgeuse lists in LOCKSTEP so curveUv[k],
            // edgeuseEdgeIndex[k] and orientation stay aligned, and reverse the
            // pcurve direction (and flip the orientation token) of each.
            std::reverse(p2.begin(), p2.end());
            std::reverse(euEdgeIdx.begin(), euEdgeIdx.end());
            std::reverse(euOrient.begin(), euOrient.end());
            for (auto& c : p2) {
                std::reverse(c.cp.begin(), c.cp.end());
                std::reverse(c.w.begin(), c.w.end());
                // knot vector of a reversed BSpline: reflect about its span
                double a = c.k.front(), b = c.k.back();
                std::vector<double> nk(c.k.size());
                for (size_t i=0;i<c.k.size();++i) nk[i] = a + b - c.k[c.k.size()-1-i];
                c.k.swap(nk);
            }
            for (auto& t : euOrient) t = (t == "same") ? "opposite" : "same";
        }
        int euCount = 0;
        for (size_t i=0;i<p2.size();++i) {
            int euIdx = (int)o.curveUv.size();
            o.curveUv.push_back(p2[i]);
            o.edgeuseEdgeIndex.push_back(euEdgeIdx[i]);
            o.edgeuseOrient.push_back(euOrient[i]);
            ctx.edgeToEUs[euEdgeIdx[i]].push_back(euIdx);
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

static void emit(const Out& o, bool isSolid, const std::string& prim,
                 const std::string& path, const std::string& doc) {
    std::ofstream f(path);
    auto P3 = [&](const std::array<double,3>& p){ return "("+fnum(p[0])+", "+fnum(p[1])+", "+fnum(p[2])+")"; };
    auto P2 = [&](const std::array<double,2>& p){ return "("+fnum(p[0])+", "+fnum(p[1])+")"; };
    size_t nFaces = o.faceLoopCount.size();
    size_t nEU    = o.edgeuseEdgeIndex.size();
    size_t nEdges = o.edge3d.size();
    size_t nVerts = o.verts.size();
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

    // ---- brep-level extent ----
    // Encompass vertex positions AND all NURBS control hulls (surface + edge3d)
    // so that the Containment validator's BA.310 (vertex) Error and the
    // BA.365/BA.465 (control-hull) Warnings stay silent — control hulls of
    // curved/rational geometry can bulge past the surface bounds.
    std::array<double,3> mn{ std::numeric_limits<double>::max(),
                             std::numeric_limits<double>::max(),
                             std::numeric_limits<double>::max() };
    std::array<double,3> mx{ -std::numeric_limits<double>::max(),
                             -std::numeric_limits<double>::max(),
                             -std::numeric_limits<double>::max() };
    auto grow = [&](const std::array<double,3>& p){
        for (int a=0;a<3;++a){ mn[a]=std::min(mn[a],p[a]); mx[a]=std::max(mx[a],p[a]); } };
    for (auto& p : o.verts) grow(p);
    for (auto& s : o.surfaces) for (auto& p : s.cp) grow(p);
    for (auto& c : o.edge3d)  for (auto& p : c.cp) grow(p);
    if (o.verts.empty() && o.surfaces.empty() && o.edge3d.empty()) { mn = {0,0,0}; mx = {0,0,0}; }

    // ---- topology: solid (void+solid shells) vs sheet (single void shell) ----
    if (isSolid) {
        arrU("brep:regionCount", {2});
        arrU("region:shellCount", {1, 1});
        arrTok("region:type", {"voidRegion", "solidRegion"});
        arrU("shell:faceuseCount", {(int)nFaces, (int)nFaces});
        arrU("shell:wireEdgeCount", {0, 0});
        arrTok("shell:pointType", {"none", "none"}); // one per shell
    } else {
        arrU("brep:regionCount", {1});
        arrU("region:shellCount", {1});
        arrTok("region:type", {"voidRegion"});
        arrU("shell:faceuseCount", {(int)(2*nFaces)});
        arrU("shell:wireEdgeCount", {0});
        arrTok("shell:pointType", {"none"});          // one per shell
    }

    { std::vector<int> lc(o.faceLoopCount); arrU("face:loopCount", lc); }
    { std::vector<std::string> st(nFaces, "BrepSurfaceNurbAPI"); arrTok("face:surfaceType", st); }
    { std::vector<std::string> tt(nFaces, "general"); arrTok("face:trimType", tt); }
    // face:range CORNER POINTS: for (u0,u1,v0,v1) -> "(u0, v0), (u1, v1)".
    { std::ostringstream s; s<<"uniform double2[] face:range = [";
      for (size_t i=0;i<o.faceRange.size();++i){ auto&r=o.faceRange[i];
        s<<"("<<fnum(r[0])<<", "<<fnum(r[2])<<"), ("<<fnum(r[1])<<", "<<fnum(r[3])<<")";
        if(i+1<o.faceRange.size())s<<", "; } s<<"]"; line(s.str()); }
    arrU("loop:edgeuseCount", o.loopEdgeuseCount);
    { std::vector<int> lv(o.loopEdgeuseCount.size(),0); arrU("loop:vertexIndex", lv); }
    arrU("edgeuse:edgeIndex", o.edgeuseEdgeIndex);
    arrTok("edgeuse:orientationType", o.edgeuseOrient);

    // faceuse:orientationType / faceuse:faceIndex
    if (isSolid) {
        // De-interleave the per-face [natural, complement] pairs into the void
        // shell (naturals) then the solid shell (complements); each shell lists
        // faces 0..nFaces-1 in order.
        std::vector<std::string> fo; std::vector<int> fidx;
        for (size_t fi = 0; fi < nFaces; ++fi) fo.push_back(o.faceuseOrient[2*fi]);
        for (size_t fi = 0; fi < nFaces; ++fi) fo.push_back(o.faceuseOrient[2*fi+1]);
        for (int pass = 0; pass < 2; ++pass)
            for (size_t fi = 0; fi < nFaces; ++fi) fidx.push_back((int)fi);
        arrTok("faceuse:orientationType", fo);
        arrU("faceuse:faceIndex", fidx);
    } else {
        // Single void shell: all-outward (natural) then all-complement, faces
        // 0..nFaces-1 twice.
        std::vector<std::string> fo; std::vector<int> fidx;
        for (size_t fi = 0; fi < nFaces; ++fi) fo.push_back(o.faceuseOrient[2*fi]);
        for (size_t fi = 0; fi < nFaces; ++fi) fo.push_back(o.faceuseOrient[2*fi+1]);
        for (int pass = 0; pass < 2; ++pass)
            for (size_t fi = 0; fi < nFaces; ++fi) fidx.push_back((int)fi);
        arrTok("faceuse:orientationType", fo);
        arrU("faceuse:faceIndex", fidx);
    }

    arrU("edgeuse:nextRadialEUIndex", o.edgeuseNextRadial);
    arrTok("edgeuse:thisRadialEntryType", o.edgeuseRadialEntry);

    // ---- edges (per UNIQUE edge) ----
    { std::vector<std::string> ct(nEdges,"BrepCurve3dNurbAPI"); arrTok("edge:curveType", ct); }
    { std::vector<double> er; for(auto&r:o.edgeRange){er.push_back(r[0]);er.push_back(r[1]);} arrD("edge:range", er); }
    { std::ostringstream s; s<<"uniform int2[] edge:vertexIndices = [";
      for(size_t i=0;i<o.edgeVtx.size();++i){ s<<"("<<o.edgeVtx[i][0]<<", "<<o.edgeVtx[i][1]<<")";
        if(i+1<o.edgeVtx.size())s<<", ";} s<<"]"; line(s.str()); }

    // ---- vertices (per UNIQUE vertex) ----
    // vertex:pointType only allows "BrepPointAPI"; the matching
    // brep:vertexPoint:point:position data is authored below.
    { std::vector<std::string> pt(nVerts,"BrepPointAPI"); arrTok("vertex:pointType", pt); }
    { std::ostringstream s; s<<"uniform point3d[] brep:vertexPoint:point:position = [";
      for(size_t i=0;i<o.verts.size();++i){ s<<P3(o.verts[i]); if(i+1<o.verts.size())s<<", ";} s<<"]"; line(s.str()); }

    // ---- surfaces (per face) ----
    { std::vector<int> a; for(auto&s:o.surfaces)a.push_back(s.uo); arrU("brep:surface:nurb:uOrder",a); }
    { std::vector<int> a; for(auto&s:o.surfaces)a.push_back(s.vo); arrU("brep:surface:nurb:vOrder",a); }
    { std::vector<int> a; for(auto&s:o.surfaces)a.push_back(s.un); arrU("brep:surface:nurb:uVertexCount",a); }
    { std::vector<int> a; for(auto&s:o.surfaces)a.push_back(s.vn); arrU("brep:surface:nurb:vVertexCount",a); }
    { std::vector<double> a; for(auto&s:o.surfaces)for(double k:s.uk)a.push_back(k); arrD("brep:surface:nurb:uKnots",a); }
    { std::vector<double> a; for(auto&s:o.surfaces)for(double k:s.vk)a.push_back(k); arrD("brep:surface:nurb:vKnots",a); }
    { std::ostringstream s; s<<"uniform point3d[] brep:surface:nurb:controlVertices = [";
      bool first=true; for(auto&su:o.surfaces)for(auto&p:su.cp){ if(!first)s<<", "; s<<P3(p); first=false;} s<<"]"; line(s.str()); }
    { std::vector<double> a; for(auto&s:o.surfaces)for(double w:s.w)a.push_back(w); arrD("brep:surface:nurb:weights",a); }

    // ---- edge 3d curves (per UNIQUE edge) ----
    { std::vector<int> a; for(auto&c:o.edge3d)a.push_back(c.order); arrU("brep:edge3dNurb:curve3d:nurb:order",a); }
    { std::vector<int> a; for(auto&c:o.edge3d)a.push_back(c.n); arrU("brep:edge3dNurb:curve3d:nurb:vertexCount",a); }
    { std::vector<double> a; for(auto&c:o.edge3d)for(double k:c.k)a.push_back(k); arrD("brep:edge3dNurb:curve3d:nurb:knots",a); }
    { std::ostringstream s; s<<"uniform point3d[] brep:edge3dNurb:curve3d:nurb:controlVertices = [";
      bool first=true; for(auto&c:o.edge3d)for(auto&p:c.cp){ if(!first)s<<", "; s<<P3(p); first=false;} s<<"]"; line(s.str()); }
    { std::vector<double> a; for(auto&c:o.edge3d)for(double w:c.w)a.push_back(w); arrD("brep:edge3dNurb:curve3d:nurb:weights",a); }

    // ---- uv trim curves (per EDGEUSE) ----
    { std::vector<int> a; for(auto&c:o.curveUv)a.push_back(c.order); arrU("brep:curveUv:nurb:order",a); }
    { std::vector<int> a; for(auto&c:o.curveUv)a.push_back(c.n); arrU("brep:curveUv:nurb:vertexCount",a); }
    { std::vector<double> a; for(auto&c:o.curveUv)for(double k:c.k)a.push_back(k); arrD("brep:curveUv:nurb:knots",a); }
    { std::ostringstream s; s<<"uniform double2[] brep:curveUv:nurb:controlVertices = [";
      bool first=true; for(auto&c:o.curveUv)for(auto&p:c.cp){ if(!first)s<<", "; s<<P2(p); first=false;} s<<"]"; line(s.str()); }
    { std::vector<double> a; for(auto&c:o.curveUv)for(double w:c.w)a.push_back(w); arrD("brep:curveUv:nurb:weights",a); }

    arrD("brep:intersectTol3d", {1e-6});
    { std::ostringstream s; s<<"uniform double3[] brep:extent = ["<<P3(mn)<<", "<<P3(mx)<<"]"; line(s.str()); }
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
        prim = "CubeWithHole"; doc = "Box 10^3 with r=3 through-hole along Z (OCCT-generated, shared topology)."; }
    else if (what == "filleted") { shape = makeFilletedCubeWithHole();
        prim = "FilletedCubeWithHole"; doc = "Box 10^3, fillet r=1, r=3 through-hole along Z (OCCT-generated, shared topology)."; }
    else if (what == "plane") { shape = makePlaneWithHole();
        prim = "PlaneWithHole"; doc = "20x20 plane sheet with r=3 circular hole (OCCT-generated, shared topology)."; }
    else { std::fprintf(stderr, "unknown shape %s\n", what.c_str()); return 2; }

    // Convert all geometry to NURBS, then split closed/periodic faces so every
    // face has a clean open boundary (no seam pcurve ambiguity).
    BRepBuilderAPI_NurbsConvert nc(shape); shape = nc.Shape();
    ShapeUpgrade_ShapeDivideClosed div(shape); div.Perform(); shape = div.Result();
    BRepLib::BuildCurves3d(shape);

    const bool isSolid = TopExp_Explorer(shape, TopAbs_SOLID).More();

    Out o;
    ShareCtx ctx;
    // Orientation-independent unique-vertex / unique-edge maps.
    TopExp::MapShapes(shape, TopAbs_VERTEX, ctx.vmap);
    TopExp::MapShapes(shape, TopAbs_EDGE, ctx.emap);

    // Per UNIQUE VERTEX: position.
    for (int i = 1; i <= ctx.vmap.Extent(); ++i) {
        gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(ctx.vmap(i)));
        o.verts.push_back({p.X(), p.Y(), p.Z()});
    }

    // Per UNIQUE EDGE (emap order, 0-based): 3D curve, range, endpoint vertex
    // indices, degeneracy. Stored in ctx; compacted into Out after the faces are
    // explored (degenerate / unreferenced edges are dropped).
    int degenerateCount = 0;
    const int nEmap = ctx.emap.Extent();
    ctx.edgeDegenerate.assign(nEmap, false);
    ctx.edgeCrv3.resize(nEmap);
    ctx.edgeRng.resize(nEmap);
    ctx.edgeVtx.resize(nEmap);
    for (int i = 1; i <= nEmap; ++i) {
        const TopoDS_Edge& edge = TopoDS::Edge(ctx.emap(i));
        // endpoint vertices (orientation-independent indices into vmap)
        TopoDS_Vertex v1, v2;
        TopExp::Vertices(edge, v1, v2);
        int i1 = v1.IsNull() ? 0 : ctx.vmap.FindIndex(v1) - 1;
        int i2 = v2.IsNull() ? 0 : ctx.vmap.FindIndex(v2) - 1;
        ctx.edgeVtx[i-1] = {i1, i2};

        Standard_Real f3,l3;
        Handle(Geom_Curve) c3 = BRep_Tool::Curve(edge, f3, l3);
        bool degen = c3.IsNull();
        if (!degen) {
            try {
                Handle(Geom_BSplineCurve) bc3 = GeomConvert::CurveToBSplineCurve(
                    new Geom_TrimmedCurve(c3, f3, l3));
                ctx.edgeCrv3[i-1] = extractCurve3d(bc3);
                ctx.edgeRng[i-1] = {f3, l3};
            } catch (const Standard_Failure& e) {
                std::fprintf(stderr, "  edge %d curve convert failed: %s\n",
                             i-1, e.GetMessageString());
                degen = true;
            }
        }
        ctx.edgeDegenerate[i-1] = degen;
        if (degen) ++degenerateCount;
    }

    // Faces -> edgeuses (curveUv per edgeuse). edgeuse:edgeIndex temporarily
    // holds the EMAP index; compaction below remaps it to the authored index.
    for (TopExp_Explorer fx(shape, TopAbs_FACE); fx.More(); fx.Next())
        addFace(o, ctx, TopoDS::Face(fx.Current()));

    // ---- compact edges: author only emap edges actually referenced by a
    // surviving edgeuse, assign 0-based authored indices, remap. ----
    std::vector<int> emap2authored(nEmap, -1);
    for (int eu = 0; eu < (int)o.edgeuseEdgeIndex.size(); ++eu) {
        int em = o.edgeuseEdgeIndex[eu];
        if (em < 0 || em >= nEmap) continue;
        if (emap2authored[em] < 0) {
            emap2authored[em] = (int)o.edge3d.size();
            o.edge3d.push_back(ctx.edgeCrv3[em]);
            o.edgeRange.push_back(ctx.edgeRng[em]);
            o.edgeVtx.push_back(ctx.edgeVtx[em]);
        }
    }
    for (int eu = 0; eu < (int)o.edgeuseEdgeIndex.size(); ++eu)
        o.edgeuseEdgeIndex[eu] = emap2authored[o.edgeuseEdgeIndex[eu]];

    // ---- radial ring: per authored edge, its edgeuses form ONE cycle ----
    size_t nEU = o.edgeuseEdgeIndex.size();
    o.edgeuseNextRadial.assign(nEU, 0);
    o.edgeuseRadialEntry.assign(nEU, "topEntry");
    // Group edgeuses by AUTHORED edge index (ctx.edgeToEUs is keyed by emap).
    std::map<int, std::vector<int>> authoredEdgeToEUs;
    for (auto& kv : ctx.edgeToEUs) {
        int au = (kv.first >= 0 && kv.first < nEmap) ? emap2authored[kv.first] : -1;
        if (au < 0) continue;
        auto& dst = authoredEdgeToEUs[au];
        dst.insert(dst.end(), kv.second.begin(), kv.second.end());
    }
    for (auto& kv : authoredEdgeToEUs) {
        const std::vector<int>& eus = kv.second;
        size_t m = eus.size();
        for (size_t k = 0; k < m; ++k) {
            // cycle: eu[k] -> eu[(k+1)%m]  (1 edgeuse -> self-loop)
            o.edgeuseNextRadial[eus[k]] = eus[(k+1)%m];
            // alternate top/bottom by position in the ring
            o.edgeuseRadialEntry[eus[k]] = (k % 2 == 0) ? "topEntry" : "bottomEntry";
        }
    }

    emit(o, isSolid, prim, path, doc);
    std::fprintf(stderr,
        "wrote %s | solid=%d faces=%zu authoredEdges=%zu edgeuses=%zu verts=%zu "
        "emapEdges=%d degenerateEdges=%d\n",
        path.c_str(), (int)isSolid, o.faceLoopCount.size(), o.edge3d.size(),
        o.edgeuseEdgeIndex.size(), o.verts.size(), nEmap, degenerateCount);
    return 0;
}
