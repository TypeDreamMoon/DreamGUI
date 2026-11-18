// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#include "DreamUIRender/DreamUIHDR.h"
#include "UnrealClient.h"
#include "HAL/IConsoleManager.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "PixelShaderUtils.h"

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
    float White=Target.GetHDRPaperWhiteInNits();
    const auto* Mode=IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Luminance.Mode"));
    if (Mode && Mode->GetInt()==1)
        if (const auto* Luminance=IConsoleManager::Get().FindConsoleVariable(TEXT("r.HDR.UI.Luminance"))) White=Luminance->GetFloat();
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
    FPixelShaderUtils::AddFullscreenPass(GraphBuilder,ShaderMap,
        ToWorking ? RDG_EVENT_NAME("DreamUI HDR Decode") : RDG_EVENT_NAME("DreamUI HDR Encode"),Shader,Parameters,Rect);
}
