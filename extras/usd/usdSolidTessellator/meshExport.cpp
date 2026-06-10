// Standalone mesh export CLI: tessellate BrepArray and write Mesh USD.
// Delegates to UsdSolid_ExportMesh in the hdOcct library.
#include <cstdlib>
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

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <input.usd> <output.usd> [primPath]"
                  << std::endl;
        return 1;
    }

    std::string inputPath = argv[1];
    std::string outputPath = argv[2];
    std::string primPath = (argc > 3) ? argv[3] : "/World/Brep0";

    int meshCount = UsdSolid_ExportMesh(
        inputPath.c_str(), outputPath.c_str(), primPath.c_str());

    if (meshCount < 0) {
        std::cerr << "Error: tessellation failed (code " << meshCount << ")"
                  << std::endl;
        return 1;
    }

    return 0;
}
