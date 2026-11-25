// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#include "DreamUIRender/DreamUIHDR.h"
#include "UnrealClient.h"
#include "HAL/IConsoleManager.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "PixelShaderUtils.h"
#include "CommonRenderResources.h"
#include "DreamGUIEngineCompat.h"
#include "DreamUIRender/DreamUIPipelines.h"

class FDreamUIHDRTransferPS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FDreamUIHDRTransferPS);
    SHADER_USE_PARAMETER_STRUCT(FDreamUIHDRTransferPS,FGlobalShader);
    class FToWorking : SHADER_PERMUTATION_BOOL("DREAMUI_HDR_TO_WORKING");
    class FScRGB : SHADER_PERMUTATION_BOOL("DREAMUI_HDR_SCRGB");
    using FPermutationDomain=TShaderPermutationDomain<FToWorking,FScRGB>;
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D,SourceTexture)
        SHADER_PARAMETER(float,PaperWhiteNits)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
};
IMPLEMENT_GLOBAL_SHADER(FDreamUIHDRTransferPS,"/Plugin/DreamGUI/Private/DreamUIHDR.usf","MainPS",SF_Pixel);

DreamUIHDR::FOutput DreamUIHDR::FromTarget(const FRenderTarget& Target)
{
    FOutput Result;
    if (!Target.GetSceneHDREnabled()) return Result;
    switch (Target.GetDisplayOutputFormat())
    {
    case EDisplayOutputFormat::HDR_ACES_1000nit_ST2084:
    case EDisplayOutputFormat::HDR_ACES_2000nit_ST2084: Result.Encoding=EEncoding::PQ; break;
    case EDisplayOutputFormat::HDR_ACES_1000nit_ScRGB:
    case EDisplayOutputFormat::HDR_ACES_2000nit_ScRGB: Result.Encoding=EEncoding::ScRGB; break;
    default: return Result;
    }
#if DREAMGUI_ENGINE_AT_LEAST(5, 8)
    float White=Target.GetHDRPaperWhiteInNits();
    const auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Luminance.Mode"));
    if (Mode && Mode->GetInt()==1)
        if (const auto* Luminance=IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Luminance"))) White=Luminance->GetFloat();
#else
    // A render target reports no paper white before 5.8: Slate composites its UI at r.HDR.UI.Luminance there.
    float White=203.f;
    if (const auto* Luminance=IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Luminance"))) White=Luminance->GetFloat();
#endif
    if (const auto* Level=IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Level"))) White*=Level->GetFloat();
    Result.PaperWhiteNits=FMath::IsFinite(White) && White>0 ? FMath::Clamp(White,1.f,10000.f) : 203.f;
    return Result;
}

void DreamUIHDR::AddTransferPass(FRDGBuilder& GraphBuilder,const FGlobalShaderMap* ShaderMap,
    FRDGTextureRef Source,FRDGTextureRef Destination,const FIntRect& Rect,const FOutput& Output,bool ToWorking)
{
    check(Source && Destination && Source!=Destination && Source->Desc.Extent==Destination->Desc.Extent);
    check(Output.IsActive());
    auto* Parameters=GraphBuilder.AllocParameters<FDreamUIHDRTransferPS::FParameters>();
    Parameters->SourceTexture=Source;
    Parameters->PaperWhiteNits=Output.PaperWhiteNits;
    // The encode can cover one player's rect; preserve the rest of the shared family target.
    Parameters->RenderTargets[0]=FRenderTargetBinding(Destination,!ToWorking || Destination->HasBeenProduced() ? ERenderTargetLoadAction::ELoad : ERenderTargetLoadAction::ENoAction);
    FDreamUIHDRTransferPS::FPermutationDomain Permutation;
    Permutation.Set<FDreamUIHDRTransferPS::FToWorking>(ToWorking);
    Permutation.Set<FDreamUIHDRTransferPS::FScRGB>(Output.Encoding==EEncoding::ScRGB);
    TShaderMapRef<FDreamUIHDRTransferPS> Shader(ShaderMap,Permutation);
    DreamUIPipelines::AddFullscreenPass(GraphBuilder,ShaderMap,
        ToWorking ? RDG_EVENT_NAME("DreamUI HDR Decode") : RDG_EVENT_NAME("DreamUI HDR Encode"),Shader,Parameters,Rect);
}

void DreamUIPipelines::CollectHDR(FCollector& Collector)
{
    // Only where the RHI can output HDR: the decode into the half-float working space, the encode into the output's
    // target, 10-bit for PQ and half-float for scRGB.
    if (!GRHISupportsHDROutput || !Collector.Find<FScreenVertexShaderVS>().IsValid())
    {
        return;
    }
    for (int32 Bits=0; Bits<4; ++Bits)
    {
        const bool bToWorking=(Bits&1)!=0;
        const bool bScRGB=(Bits&2)!=0;
        FDreamUIHDRTransferPS::FPermutationDomain Permutation;
        Permutation.Set<FDreamUIHDRTransferPS::FToWorking>(bToWorking);
        Permutation.Set<FDreamUIHDRTransferPS::FScRGB>(bScRGB);
        const TShaderRef<FDreamUIHDRTransferPS> Shader=Collector.Find<FDreamUIHDRTransferPS>(Permutation);
        if (!Shader.IsValid())
        {
            continue;
        }
        FTarget Target;
        Target.Format=bToWorking || bScRGB ? PF_FloatRGBA : PF_A2B10G10R10;
        FGraphicsPipelineStateInitializer Init;
        InitFullscreen(Init,Collector.ShaderMap,Shader);
        Collector.Add(Init,Target,EPSOPrecachePriority::Low);
    }
}
