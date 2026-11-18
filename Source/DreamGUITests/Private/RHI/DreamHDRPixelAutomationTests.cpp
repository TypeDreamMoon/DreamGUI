// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "RHI/DreamGalleryStage.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIHDR.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UnrealClient.h"
#include "HDRHelper.h"
#include "RenderGraphBuilder.h"
#include "RenderingThread.h"
#include "HAL/IConsoleManager.h"
#include "Math/Float16Color.h"

namespace DreamHDRTest
{
    class FTarget final : public FRenderTarget
    {
    public:
        FTextureRHIRef Texture;
        FIntPoint ViewSize=FIntPoint(64,64);
        EDisplayOutputFormat Format=EDisplayOutputFormat::HDR_ACES_1000nit_ST2084;
        float White=200;
        bool HDR=true;
        virtual const FTextureRHIRef& GetRenderTargetTexture() const override { return Texture; }
        virtual FIntPoint GetSizeXY() const override { return FIntPoint(64,64); }
        virtual EDisplayOutputFormat GetDisplayOutputFormat() const override { return Format; }
        virtual bool GetSceneHDREnabled() const override { return HDR; }
        virtual float GetHDRPaperWhiteInNits() const override { return White; }
    };
    struct FCVarScope
    {
        IConsoleVariable* Variable=nullptr;
        float Original=0;
        EConsoleVariableFlags Priority=ECVF_SetByConstructor;
        FCVarScope(const TCHAR* Name,float Value)
        {
            Variable=IConsoleManager::Get().FindConsoleVariable(Name);
            if (Variable)
            {
                Original=Variable->GetFloat(); Priority=EConsoleVariableFlags(Variable->GetFlags()&ECVF_SetByMask);
                Variable->Set(Value,Priority);
            }
        }
        ~FCVarScope() { if (Variable) Variable->Set(Original,Priority); }
    };
}

/** Use real registered canvas primitives and the ordinary screen renderer, with an offscreen HDR target.
 * The operating system/swap chain remains SDR. Only the synthetic view's metadata changes. */
struct FDreamUIHDRTestAccess
{
    static void Draw(FDreamUIRenderer& Renderer,FRHICommandListImmediate& RHICmdList,DreamHDRTest::FTarget& Target)
    {
        const auto SavedType=Renderer.RendererType;
        const auto SavedView=Renderer.RenderThreadViewParameter;
        Renderer.RendererType=EDreamUIRendererType::ScreenSpace_and_WorldSpace;
        Renderer.RenderThreadViewParameter.bCanRenderScreenSpace=true;
        Renderer.RenderThreadViewParameter.bIsPlaying=true;
        FRDGBuilder Graph(RHICmdList,RDG_EVENT_NAME("DreamUI HDR pixel acceptance"));
        auto* Family=Graph.AllocObject<FSceneViewFamily>(FSceneViewFamily::ConstructionValues(&Target,nullptr,FEngineShowFlags(ESFIM_Game)).SetTime(FGameTime::GetTimeSinceAppStart()));
        FSceneViewInitOptions Options;
        Options.ViewFamily=Family; Options.SetViewRectangle(FIntRect(FIntPoint::ZeroValue,Target.ViewSize));
        Options.ViewOrigin=SavedView.ViewOrigin; Options.ViewRotationMatrix=SavedView.ViewRotationMatrix;
        Options.ProjectionMatrix=SavedView.ProjectionMatrix;
        auto* View=Graph.AllocObject<FSceneView>(Options); View->bIsGameView=true;
        Renderer.PostRenderView_RenderThread(Graph,*View);
        Graph.Execute();
        Renderer.RendererType=SavedType; Renderer.RenderThreadViewParameter=SavedView;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamHDRMetadataTest,"DreamGUI.Rendering.HDR.DisplayMetadata",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDreamHDRMetadataTest::RunTest(const FString&)
{
    DreamHDRTest::FCVarScope Mode(TEXT("r.HDR.UI.Luminance.Mode"),0),Level(TEXT("r.HDR.UI.Level"),1);
    DreamHDRTest::FTarget Target;
    TestTrue(TEXT("HDR10 selects PQ transfer"),DreamUIHDR::FromTarget(Target).Encoding==DreamUIHDR::EEncoding::PQ);
    TestEqual(TEXT("The target's paper white is used"),DreamUIHDR::FromTarget(Target).PaperWhiteNits,200.f);
    Target.HDR=false;
    TestFalse(TEXT("A requested format alone never activates HDR"),DreamUIHDR::FromTarget(Target).IsActive());
    Target.HDR=true; Target.Format=EDisplayOutputFormat::HDR_LinearEXR;
    TestFalse(TEXT("Linear captures are not mistaken for HDR display encoding"),DreamUIHDR::FromTarget(Target).IsActive());
    Target.Format=EDisplayOutputFormat::HDR_ACES_2000nit_ScRGB;
    TestTrue(TEXT("scRGB selects its 80-nit reference transport"),DreamUIHDR::FromTarget(Target).Encoding==DreamUIHDR::EEncoding::ScRGB);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamHDRPixelTest,"DreamGUI.Rendering.HDR.NonNullRHI.PaperWhiteAndLinearBlend",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter|EAutomationTestFlags::NonNullRHI)
bool FDreamHDRPixelTest::RunTest(const FString&)
{
    using namespace DreamGalleryStage;
    struct FState
    {
        DreamHDRTest::FCVarScope Mode{TEXT("r.HDR.UI.Luminance.Mode"),0},Level{TEXT("r.HDR.UI.Level"),1};
    };
    auto State=MakeShared<FState>();
    auto Stage=BeginStage(*this,FIntPoint(64,64),FColor::Black);
    if (!Stage->IsUsable()) { AddError(Stage->GetFailure()); return false; }
    Stage->AddBlock(TEXT("OpaqueWhite"),FVector2D(16,16),FVector2D(-24,0),FColor::White);
    Stage->AddBlock(TEXT("OpaqueRed"),FVector2D(16,16),FVector2D(-8,0),FColor::Red);
    Stage->AddBlock(TEXT("HalfWhite"),FVector2D(16,16),FVector2D(8,0),FColor(255,255,255,128));
    for (bool BuiltIn:{true,false}) for (uint8 Samples:{uint8(1),uint8(2)})
    {
        EnqueueDo([Stage,BuiltIn,Samples]{ Stage->UseBuiltInShader(BuiltIn); Stage->UseMultisampling(Samples); Stage->RequestRedraw(); });
        EnqueueFrames(Stage,4);
        EnqueueFramesUntilDrawn(Stage,200);
        EnqueueDo([this,Stage,State,BuiltIn,Samples]
        {
            const auto Renderer=Stage->GetCanvas()->GetRenderTargetViewExtension();
            if (!TestTrue(TEXT("Real canvas renderer exists"),Renderer.IsValid())) return;
            for (auto Format:{EDisplayOutputFormat::HDR_ACES_1000nit_ST2084,EDisplayOutputFormat::HDR_ACES_1000nit_ScRGB})
            for (float White:{200.f,300.f})
            for (bool ResizedView:{false,true})
            {
                const float EncodedScene=Format==EDisplayOutputFormat::HDR_ACES_1000nit_ST2084 ? .7518271f : 12.5f; // 1000 nits
                TStrongObjectPtr<UTextureRenderTarget2D> Texture(NewObject<UTextureRenderTarget2D>());
                Texture->ClearColor=FLinearColor(EncodedScene,EncodedScene,EncodedScene,1);
                Texture->InitCustomFormat(64,64,PF_FloatRGBA,true); Texture->UpdateResourceImmediate(true);
                auto* Resource=Texture->GameThread_GetRenderTargetResource();
                TArray<FFloat16Color> Pixels,Before;
                ENQUEUE_RENDER_COMMAND(DreamHDRDrawAndRead)([Renderer,Resource,Format,White,ResizedView,&Pixels,&Before](FRHICommandListImmediate& RHICmdList)
                {
                    DreamHDRTest::FTarget Target; Target.Texture=Resource->GetRenderTargetTexture(); Target.Format=Format; Target.White=White;
                    // During a resize the view may still describe the previous, larger window.
                    if (ResizedView) Target.ViewSize=FIntPoint(128,128);
                    RHICmdList.ReadSurfaceFloatData(Target.Texture,FIntRect(0,0,64,64),Before,FReadSurfaceDataFlags(RCM_MinMax));
                    FDreamUIHDRTestAccess::Draw(*Renderer,RHICmdList,Target);
                    RHICmdList.ReadSurfaceFloatData(Target.Texture,FIntRect(0,0,64,64),Pixels,FReadSurfaceDataFlags(RCM_MinMax));
                });
                FlushRenderingCommands();
                if (!TestEqual(TEXT("HDR float readback is complete"),Pixels.Num(),64*64)) continue;
                if (!TestEqual(TEXT("Input float readback is complete"),Before.Num(),64*64)) continue;
                TArray<FLinearColor> Linear;
                for (const auto& Pixel:Pixels) Linear.Add(FLinearColor(Pixel));
                // Engine reference decoder, independent of the DreamGUI shader under test.
                ConvertPixelDataToSCRGB(Linear,Format);
                // Compare with the actual FP16 input, not the unquantized clear literal.
                TArray<FLinearColor> Scene{FLinearColor(Before[0])};
                ConvertPixelDataToSCRGB(Scene,Format);
                const FLinearColor SceneNits=Scene[0]*80.f;
                const FString Case=FString::Printf(TEXT("%s %dx format%d white%.0f %s"),BuiltIn ? TEXT("builtin") : TEXT("material"),Samples,int32(Format),White,ResizedView ? TEXT("resized view") : TEXT("matching view"));
                TestTrue(Case+TEXT(" input retains the 1000-nit reference within FP16 precision"),FMath::IsNearlyEqual(SceneNits.R,1000.f,5.f));
                AddInfo(FString::Printf(TEXT("%s scene input %.6f output %.6f; decoded input %.2f output %.2f nits"),*Case,float(Before[0].R),float(Pixels[4*64+4].R),SceneNits.R,Linear[4*64+4].R*80.f));
                auto Check=[&](int32 X,int32 Y,FLinearColor Expected,const TCHAR* Label)
                {
                    const auto Actual=Linear[Y*64+X]*80.f;
                    TestTrue(Case+TEXT(" ")+Label,FMath::IsNearlyEqual(Actual.R,Expected.R,5.f) && FMath::IsNearlyEqual(Actual.G,Expected.G,5.f) && FMath::IsNearlyEqual(Actual.B,Expected.B,5.f));
                    if (!FMath::IsNearlyEqual(Actual.R,Expected.R,5.f)) AddInfo(FString::Printf(TEXT("Pixel %d,%d actual %.2f %.2f %.2f, expected %.2f %.2f %.2f nits"),X,Y,Actual.R,Actual.G,Actual.B,Expected.R,Expected.G,Expected.B));
                };
                Check(8,32,FLinearColor(White,White,White),TEXT("opaque white follows paper white"));
                Check(24,32,FLinearColor(White,0,0),TEXT("red preserves sRGB primaries through HDR transport"));
                const auto Blended=FMath::Lerp(SceneNits,FLinearColor(White,White,White),128.f/255.f);
                Check(40,32,Blended,TEXT("half opacity blends in linear light"));
                Check(56,32,SceneNits,TEXT("untouched HDR highlights survive"));
                Check(4,4,SceneNits,TEXT("scene outside the UI is preserved"));
            }
        });
    }
    EnqueueTearDown(Stage);
    EnqueueDo([State]{});
    return true;
}
#endif
