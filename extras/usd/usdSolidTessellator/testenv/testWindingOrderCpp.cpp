// testWindingOrderCpp.cpp — ground truth winding test using OCCT primitives.
// Creates box/sphere/cylinder, tessellates with BRepMesh, applies the SAME
// winding/normal logic as our hdOcct plugin, and verifies:
//   1. Signed volume > 0 (outward winding = CCW for USD/rightHanded)
//   2. |signed volume| ≈ expected analytical volume (mesh quality check)
//
// Ground truth: SMLib solid modeling repo mass_properties_mesh.py uses
// V = sum_over_tris( v0 · (v1 × v2) ) / 6 — positive for outward winding.
//
// Compile:
//   g++ -std=c++17 -I/usr/include/opencascade \
//       testWindingOrderCpp.cpp -o testWinding \
//       -lTKBRep -lTKPrim -lTKTopAlgo -lTKMath -lTKMesh -lTKG3d
//
// Exit code: 0 = pass, 1 = fail

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <BRep_Tool.hxx>
#include <Poly_Triangulation.hxx>
#include <TopLoc_Location.hxx>
#include <gp_Vec.hxx>
#include <gp_Pnt.hxx>

#include <cstdio>
#include <cmath>
#include <vector>

struct Vec3 { double x, y, z; };

static Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x};
}
static double dot(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }

struct TestResult {
    double signedVolume;
    int nTris;
    int nVerts;
};

// Tessellate shape and compute signed volume using the SAME logic as hdOcct:
// - BRepMesh_IncrementalMesh for tessellation
// - Face orientation for winding swap: if (isReversed) swap(n2, n3)
// - Transform by Location
static TestResult tessellateAndMeasure(const TopoDS_Shape& shape) {
    // Tessellate
    BRepMesh_IncrementalMesh mesher(shape, 0.1, Standard_False, 0.5, Standard_True);
    if (!mesher.IsDone()) {
        fprintf(stderr, "BRepMesh failed\n");
        return {0.0, 0, 0};
    }

    TestResult out = {0.0, 0, 0};

    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        TopoDS_Face face = TopoDS::Face(exp.Current());
        TopLoc_Location loc;
        auto tri = BRep_Tool::Triangulation(face, loc);
        if (!tri) continue;

        const gp_Trsf& trsf = loc.Transformation();
        bool isReversed = (face.Orientation() == TopAbs_REVERSED);

        int nbNodes = tri->NbNodes();
        int nbTris = tri->NbTriangles();

        // Transform points
        std::vector<Vec3> pts(nbNodes);
        for (int i = 1; i <= nbNodes; ++i) {
            gp_Pnt p = tri->Node(i).Transformed(trsf);
            pts[i-1] = {p.X(), p.Y(), p.Z()};
        }

        // Compute signed volume per triangle
        for (int i = 1; i <= nbTris; ++i) {
            int n1, n2, n3;
            tri->Triangle(i).Get(n1, n2, n3);

            // Winding swap for reversed faces (same as hdOcct tessellator.cpp)
            if (isReversed) {
                std::swap(n2, n3);
            }

            Vec3 v0 = pts[n1-1];
            Vec3 v1 = pts[n2-1];
            Vec3 v2 = pts[n3-1];

            // Signed tet volume: v0 . (v1 × v2) / 6
            out.signedVolume += dot(v0, cross(v1, v2)) / 6.0;
        }
        out.nTris += nbTris;
        out.nVerts += nbNodes;
    }
    return out;
}

static bool testShape(const char* name, const TopoDS_Shape& shape,
                      double expectedVol, double volTol) {
    TestResult r = tessellateAndMeasure(shape);
    double relErr = std::abs(std::abs(r.signedVolume) - expectedVol) / expectedVol;
    bool sign_ok = (r.signedVolume > 0.0);
    bool vol_ok = (relErr < volTol);
    bool pass = sign_ok && vol_ok;

    printf("[%s] signed_vol=%.4f expected=%.4f relErr=%.4f%% tris=%d %s\n",
           name, r.signedVolume, expectedVol, relErr*100.0, r.nTris,
           pass ? "PASS" : "FAIL");

    if (!sign_ok) {
        fprintf(stderr, "  WINDING ERROR: signed_vol=%.4f (negative → inward winding)\n",
                r.signedVolume);
    }
    if (!vol_ok) {
        fprintf(stderr, "  VOLUME ERROR: relErr=%.2f%% > tolerance=%.2f%%\n",
                relErr*100.0, volTol*100.0);
    }
    return pass;
}

int main() {
    printf("=== Winding Order Ground Truth Test ===\n");
    printf("Formula: V = sum(v0 · (v1 × v2)) / 6\n");
    printf("Positive V → outward winding (CCW, USD rightHanded convention)\n");
    printf("Reference: SMLib solid modeling repo mass_properties_mesh.py\n\n");

    bool all = true;

    // Box 10x10x10 → volume = 1000
    all &= testShape("Box_10x10x10",
        BRepPrimAPI_MakeBox(10.0, 10.0, 10.0).Shape(), 1000.0, 0.001);

    // Sphere r=5 → volume = 4/3 π r³ ≈ 523.6 (5% tolerance for mesh approx)
    all &= testShape("Sphere_r5",
        BRepPrimAPI_MakeSphere(5.0).Shape(), 4.0/3.0*M_PI*125.0, 0.05);

    // Cylinder r=3 h=10 → volume = π r² h ≈ 282.7 (5% tolerance)
    all &= testShape("Cylinder_r3_h10",
        BRepPrimAPI_MakeCylinder(3.0, 10.0).Shape(), M_PI*9.0*10.0, 0.05);

    printf("\n%s\n", all ? "ALL PASSED" : "SOME FAILED");
    return all ? 0 : 1;
}
