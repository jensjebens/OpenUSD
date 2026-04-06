//
// testExecUnitsXformable.cpp — Test the unit-aware transform computation.
//
#include "pxr/pxr.h"

#include "pxr/exec/execUsd/cacheView.h"
#include "pxr/exec/execUsd/request.h"
#include "pxr/exec/execUsd/system.h"
#include "pxr/exec/execUsd/valueKey.h"

#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnosticLite.h"
#include "pxr/base/tf/pathUtils.h"
#include "pxr/base/tf/staticTokens.h"
#include "pxr/base/tf/token.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usd/stage.h"

#include <iostream>
#include <utility>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (computeUnitAwareLocalToWorldTransform)
    (computeLocalToWorldTransform)
);


static const std::string layerContents =
    R"usda(#usda 1.0
(
    defaultPrim = "Root"
    metersPerUnit = 1.0
    upAxis = "Y"
)
def Xform "Root"
{
    def Xform "CmBox" (
        apiSchemas = ["UnitsResolutionAPI"]
    )
    {
        uniform token[] xformOpOrder = [ "xformOp:transform" ]
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (100, 200, 300, 1) )
        double unitsResolution:metersPerUnitScale = 0.01
    }

    def Xform "MBox" (
        apiSchemas = ["UnitsResolutionAPI"]
    )
    {
        uniform token[] xformOpOrder = [ "xformOp:transform" ]
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (5, 10, 15, 1) )
        double unitsResolution:metersPerUnitScale = 1.0
    }

    def Xform "NoApi"
    {
        uniform token[] xformOpOrder = [ "xformOp:transform" ]
        matrix4d xformOp:transform = ( (1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (7, 8, 9, 1) )
    }
}
)usda";


static void
TestStandardTransformUnchanged()
{
    std::cout << "=== TestStandardTransformUnchanged ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer->ImportFromString(layerContents));
    UsdStageConstRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    ExecUsdSystem execSystem(stage);

    // The standard computeLocalToWorldTransform should work as before
    std::vector<ExecUsdValueKey> valueKeys {
        {stage->GetPrimAtPath(SdfPath("/Root/CmBox")),
         _tokens->computeLocalToWorldTransform},
    };

    ExecUsdRequest request = execSystem.BuildRequest(std::move(valueKeys));
    TF_AXIOM(request.IsValid());
    execSystem.PrepareRequest(request);
    TF_AXIOM(request.IsValid());

    ExecUsdCacheView cache = execSystem.Compute(request);

    VtValue value = cache.Get(0);
    TF_AXIOM(!value.IsEmpty());
    const GfMatrix4d matrix = value.Get<GfMatrix4d>();
    const GfVec3d translate = matrix.ExtractTranslation();
    std::cout << "  CmBox standard L2W translate: " << translate << std::endl;
    // Standard computation returns raw values — no unit correction
    TF_AXIOM(GfIsClose(translate, GfVec3d(100, 200, 300), 1e-6));

    std::cout << "=== PASSED ===" << std::endl;
}


static void
TestUnitAwareTransform()
{
    std::cout << "=== TestUnitAwareTransform ===" << std::endl;

    SdfLayerRefPtr layer = SdfLayer::CreateAnonymous(".usda");
    TF_AXIOM(layer->ImportFromString(layerContents));
    UsdStageConstRefPtr stage = UsdStage::Open(layer);
    TF_AXIOM(stage);

    ExecUsdSystem execSystem(stage);

    std::vector<ExecUsdValueKey> valueKeys {
        {stage->GetPrimAtPath(SdfPath("/Root/CmBox")),
         _tokens->computeUnitAwareLocalToWorldTransform},
        {stage->GetPrimAtPath(SdfPath("/Root/MBox")),
         _tokens->computeUnitAwareLocalToWorldTransform},
    };

    ExecUsdRequest request = execSystem.BuildRequest(std::move(valueKeys));
    TF_AXIOM(request.IsValid());
    execSystem.PrepareRequest(request);
    TF_AXIOM(request.IsValid());

    ExecUsdCacheView cache = execSystem.Compute(request);

    // CmBox: (100,200,300) * 0.01 = (1, 2, 3)
    {
        VtValue value = cache.Get(0);
        TF_AXIOM(!value.IsEmpty());
        const GfMatrix4d matrix = value.Get<GfMatrix4d>();
        const GfVec3d translate = matrix.ExtractTranslation();
        std::cout << "  CmBox unit-aware translate: " << translate << std::endl;
        TF_AXIOM(GfIsClose(translate, GfVec3d(1, 2, 3), 1e-6));
    }

    // MBox: (5,10,15) * 1.0 = (5, 10, 15)
    {
        VtValue value = cache.Get(1);
        TF_AXIOM(!value.IsEmpty());
        const GfMatrix4d matrix = value.Get<GfMatrix4d>();
        const GfVec3d translate = matrix.ExtractTranslation();
        std::cout << "  MBox unit-aware translate: " << translate << std::endl;
        TF_AXIOM(GfIsClose(translate, GfVec3d(5, 10, 15), 1e-6));
    }

    std::cout << "=== PASSED ===" << std::endl;
}


int main()
{
    // Register our plugin
    const PlugPluginPtrVector plugins = PlugRegistry::GetInstance()
        .RegisterPlugins(TfAbsPath("resources"));
    
    if (!plugins.empty()) {
        std::cout << "Loaded plugin: " << plugins[0]->GetName() << std::endl;
    }

    TestStandardTransformUnchanged();
    TestUnitAwareTransform();

    std::cout << "\nAll tests passed!" << std::endl;
    return 0;
}
