// Standalone mesh export CLI: tessellate BrepArray and write Mesh USD.
// Delegates to UsdSolid_ExportMesh / UsdSolid_ExportMeshEx in the hdOcct library.
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

// Platform-specific import declaration for the hdOcct library function.
// On Windows, symbols must be explicitly imported from DLLs.
#if defined(_WIN32)
#   define HDOCCT_IMPORT __declspec(dllimport)
#else
#   define HDOCCT_IMPORT
#endif

extern "C" HDOCCT_IMPORT int UsdSolid_ExportMesh(
    const char* inputPath, const char* outputPath, const char* primPath);

extern "C" HDOCCT_IMPORT int UsdSolid_ExportMeshEx(
    const char* inputPath, const char* outputPath, const char* primPath,
    double linearDeflection, double angularDeflection, int relativeDeflection);

static void usage(const char* argv0) {
    std::cerr
        << "Usage: " << argv0
        << " <input.usd> <output.usd> [primPath]\n"
        << "          [--linear-deflection <f>] [--angular-deflection <rad>]\n"
        << "          [--relative-deflection]\n"
        << "\n"
        << "  --linear-deflection   max chord distance between the tessellation\n"
        << "                        and the true surface (default 0.1; ~0.02 is fine).\n"
        << "                        With --relative-deflection this is a fraction of\n"
        << "                        the bounding-box diagonal instead.\n"
        << "  --angular-deflection  max angle in RADIANS between adjacent facet\n"
        << "                        normals (default 0.5; ~0.1 smooths curved\n"
        << "                        silhouettes at close zoom).\n"
        << "  --relative-deflection interpret --linear-deflection as a bbox-diagonal\n"
        << "                        fraction.\n";
}

int main(int argc, char* argv[]) {
    std::string inputPath;
    std::string outputPath;
    std::string primPath = "/World/Brep0";
    // Sentinel < 0 means "flag not given"; fall through to the legacy fixed
    // defaults (the header's 0.1 / 0.5) so behaviour is unchanged without flags.
    double linearDeflection = -1.0;
    double angularDeflection = -1.0;
    int relativeDeflection = 0;

    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--linear-deflection" && i + 1 < argc) {
            linearDeflection = atof(argv[++i]);
        } else if (a == "--angular-deflection" && i + 1 < argc) {
            angularDeflection = atof(argv[++i]);
        } else if (a == "--relative-deflection") {
            relativeDeflection = 1;
        } else if (a == "-h" || a == "--help") {
            usage(argv[0]);
            return 0;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "Unknown option: " << a << std::endl;
            usage(argv[0]);
            return 1;
        } else {
            switch (positional++) {
                case 0: inputPath = a; break;
                case 1: outputPath = a; break;
                case 2: primPath = a; break;
                default:
                    std::cerr << "Too many positional arguments." << std::endl;
                    usage(argv[0]);
                    return 1;
            }
        }
    }

    if (inputPath.empty() || outputPath.empty()) {
        usage(argv[0]);
        return 1;
    }

    int meshCount;
    if (linearDeflection < 0.0 && angularDeflection < 0.0 && !relativeDeflection) {
        // No quality flags: legacy fixed-quality path (bit-for-bit unchanged).
        meshCount = UsdSolid_ExportMesh(
            inputPath.c_str(), outputPath.c_str(), primPath.c_str());
    } else {
        // Fill any unspecified flag with the legacy default so a single flag can
        // be tuned in isolation.
        double ld = (linearDeflection < 0.0) ? 0.1 : linearDeflection;
        double ad = (angularDeflection < 0.0) ? 0.5 : angularDeflection;
        meshCount = UsdSolid_ExportMeshEx(
            inputPath.c_str(), outputPath.c_str(), primPath.c_str(),
            ld, ad, relativeDeflection);
    }

    if (meshCount < 0) {
        std::cerr << "Error: tessellation failed (code " << meshCount << ")"
                  << std::endl;
        return 1;
    }

    return 0;
}
