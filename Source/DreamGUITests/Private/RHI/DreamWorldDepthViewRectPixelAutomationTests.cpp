// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ClearQuad.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUISettings.h"
#include "DreamUICaptureLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "FXRenderingUtils.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeLock.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"
#include "RHI/DreamPixelProbe.h"

namespace DreamWorldDepthViewRectPixelTestLocal
{
	// Write the depth texture the production shader actually samples. A second camera or a scene mesh
	// could make these regions too, but a depth of exactly zero or one makes the expected occlusion
	// independent of the host map, camera distance, lighting and the scene renderer's AA settings.
	class FPartitionedSceneDepth : public FSceneViewExtensionBase
	{
	public:
		FPartitionedSceneDepth(const FAutoRegister& AutoRegister, UWorld* InWorld)
			: FSceneViewExtensionBase(AutoRegister), World(InWorld)
		{
		}

		virtual void SetupViewFamily(FSceneViewFamily&) override {}
		virtual void SetupView(FSceneViewFamily&, FSceneView&) override {}
		virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}
		virtual int32 GetPriority() const override { return MAX_int32; }

		virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InFamily) override
		{
			TArray<FIntRect> WrittenViewRects;
			FRDGTextureRef Depth = nullptr;
			for (const FSceneView* View : InFamily.Views)
			{
				if (View == nullptr || !View->bIsViewInfo || View->StereoPass != EStereoscopicPass::eSSP_FULL)
				{
					continue;
				}
				const FSceneTextureShaderParameters Textures = GetSceneTextureShaderParameters(*View);
				const FRDGTextureRef ViewDepth = Textures.SceneTextures
					? Textures.SceneTextures->GetContents()->SceneDepthTexture : nullptr;
				if (ViewDepth == nullptr)
				{
					continue;
				}
				if (Depth == nullptr)
				{
					Depth = ViewDepth;
					// Clear outside constrained views too: sampling the padding must not happen to look
					// occluded because of the scene allocator's previous contents.
					AddClearDepthStencilPass(GraphBuilder, Depth, true, 0.0f, false, 0);
				}
				if (ViewDepth != Depth)
				{
					continue;
				}
				const FIntRect RawRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(*View);
				FIntRect OccludedRect = RawRect;
				OccludedRect.Max.X = RawRect.Min.X + FMath::RoundToInt32(RawRect.Width() * 0.7f);
				auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Depth,
					ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction,
					FExclusiveDepthStencil::DepthWrite_StencilNop);
				GraphBuilder.AddPass(RDG_EVENT_NAME("DreamGUI_Test_PartitionSceneDepth"), PassParameters,
					ERDGPassFlags::Raster, [OccludedRect](FRHICommandList& RHICmdList)
					{
						RHICmdList.SetViewport(OccludedRect.Min.X, OccludedRect.Min.Y, 0.0f,
							OccludedRect.Max.X, OccludedRect.Max.Y, 1.0f);
						DrawClearQuad(RHICmdList, false, FLinearColor::Black, true, 1.0f, false, 0);
					});
				WrittenViewRects.Add(View->UnscaledViewRect);
			}
			FScopeLock Lock(&SnapshotMutex);
			ViewRects = MoveTemp(WrittenViewRects);
		}

		TArray<FIntRect> GetWrittenViewRects() const
		{
			FScopeLock Lock(&SnapshotMutex);
			return ViewRects;
		}

	protected:
		virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override
		{
			return World.IsValid() && World.Get() == Context.GetWorld();
		}

	private:
		TWeakObjectPtr<UWorld> World;
		mutable FCriticalSection SnapshotMutex;
		TArray<FIntRect> ViewRects;
	};

	struct FState
	{
		FState()
		{
			GetMutableDefault<UDreamUISettings>()->AntiAliasingMethod = EDreamUIRendererAntiAliasingMethod::None;
			GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader = true;
		}
		~FState()
		{
			GetMutableDefault<UDreamUISettings>()->AntiAliasingMethod = SavedAA;
			GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader = bSavedBuiltIn;
		}
		const EDreamUIRendererAntiAliasingMethod SavedAA = GetDefault<UDreamUISettings>()->AntiAliasingMethod;
		const bool bSavedBuiltIn = GetDefault<UDreamUISettings>()->bUseBuiltInUIShader;
		TSharedPtr<FPartitionedSceneDepth, ESPMode::ThreadSafe> DepthWriter;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<ULocalPlayer> SecondPlayer;
		TWeakObjectPtr<UDreamWidget> Panel;
		TWeakObjectPtr<UDreamCanvas> Canvas;
		TArray<FColor> Background;
		FIntPoint Size = FIntPoint::ZeroValue;
	};

	FIntPoint Probe(const FIntRect& InRect, float InLocalX)
	{
		return FIntPoint(InRect.Min.X + FMath::RoundToInt32(InRect.Width() * InLocalX),
			InRect.Min.Y + InRect.Height() / 2);
	}

	bool ReadPicture(FAutomationTestBase& InTest, UWorld* InWorld, TArray<FColor>& OutPixels, FIntPoint& OutSize)
	{
		return InTest.TestTrue(TEXT("The actual play viewport reads back"),
			UDreamUICaptureLibrary::ReadViewportPixels(UDreamUICaptureLibrary::FindViewportOf(InWorld), OutPixels, OutSize));
	}

	void ExpectPanel(FAutomationTestBase& InTest, const FState& InState, UWorld* InWorld,
		int32 InViewCount, bool bConstrained, bool bOccluded, const TCHAR* InCapture)
	{
		TArray<FColor> Pixels;
		FIntPoint Size;
		if (!ReadPicture(InTest, InWorld, Pixels, Size)
			|| !InTest.TestEqual(TEXT("The background and UI pictures have the same size"), Size, InState.Size)
			|| !InTest.TestEqual(TEXT("The saved background has every pixel"), InState.Background.Num(), Size.X * Size.Y)
			|| !InState.DepthWriter.IsValid())
		{
			return;
		}
		// ReadPicture has flushed the scene render, so this is the very set of real FViewInfos whose
		// depth was partitioned and whose world-space UI is in Pixels, rather than a guessed player rect.
		const TArray<FIntRect> Rects = InState.DepthWriter->GetWrittenViewRects();
		if (!InTest.TestEqual(TEXT("Every actual player view used the real partitioned scene depth"), Rects.Num(), InViewCount))
		{
			return;
		}
		if (InViewCount == 2)
		{
			InTest.TestTrue(TEXT("The actual two views occupy distinct left and right viewport parts"),
				Rects[0].Min.X == 0 && Rects[0].Max.X == Rects[1].Min.X
				&& Rects[1].Max.X == Size.X && Rects[0].Height() == Size.Y && Rects[1].Height() == Size.Y);
		}
		else
		{
			InTest.TestTrue(TEXT("The control has the requested full or constrained view rect"), bConstrained
				? Rects[0].Min.X > 0 && Rects[0].Max.X < Size.X && Rects[0].Width() < Size.X / 2
				: Rects[0] == FIntRect(FIntPoint::ZeroValue, Size));
		}
		for (int32 Index = 0; Index < Rects.Num(); ++Index)
		{
			const FIntPoint Hidden = Probe(Rects[Index], bConstrained ? 0.6f : 0.4f);
			const FIntPoint Visible = Probe(Rects[Index], bConstrained ? 0.85f : 0.8f);
			const FColor ExpectedHidden = bOccluded ? InState.Background[Hidden.Y * Size.X + Hidden.X] : FColor::Red;
			InTest.TestFalse(TEXT("The scene background can distinguish an occluded panel from the red UI"),
				FDreamPixelProbe::IsNear(InState.Background[Hidden.Y * Size.X + Hidden.X], FColor::Red, 8));
			FDreamPixelProbe::ExpectColorAt(InTest, Pixels, Size, Hidden, ExpectedHidden, 8,
				*FString::Printf(TEXT("view %d: its own left 70%% depth region %s the UI"), Index,
					bOccluded ? TEXT("occludes") : TEXT("does not occlude with BlendDepth=1")));
			FDreamPixelProbe::ExpectColorAt(InTest, Pixels, Size, Visible, FColor::Red, 8,
				*FString::Printf(TEXT("view %d: its own right 30%% depth region keeps the UI visible"), Index));
		}
		FDreamPixelProbe::SaveCapture(Pixels, Size, InCapture);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldDepthUsesEachViewRectPictureTest,
	"DreamGUI.World.RHI.SceneDepthOcclusionUsesEachPlayersAndConstrainedViewRect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamWorldDepthUsesEachViewRectPictureTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldDepthViewRectPixelTestLocal;
	const TSharedRef<FState> State = MakeShared<FState>();
	const TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, State](FDreamDriverPieRig& InRig)
	{
		UWorld* World = InRig.GetWorld();
		ACameraActor* Camera = World != nullptr ? World->SpawnActor<ACameraActor>() : nullptr;
		if (!TestNotNull(TEXT("A fixed perspective camera belongs to the play session"), Camera)
			|| !TestNotNull(TEXT("The first player has a camera controller"), InRig.GetPlayerController()))
		{
			return;
		}
		Camera->SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		InRig.GetPlayerController()->SetViewTarget(Camera);
		State->Camera = Camera;
		State->DepthWriter = FSceneViewExtensions::NewExtension<FPartitionedSceneDepth>(World);
		UDreamWidget* Panel = InRig.MakeWorldPanel(TEXT("DepthProbePanel"), FTransform(FVector(400.0, 0.0, 0.0)), FVector2D(10000.0, 10000.0));
		if (!TestNotNull(TEXT("A real world-space panel covers every camera rect"), Panel))
		{
			return;
		}
		State->Panel = Panel;
		State->Canvas = Panel->GetComponent<UDreamCanvas>();
		if (!TestNotNull(TEXT("The panel has a world-space canvas"), State->Canvas.Get()))return;
		// MakeWorldPanel's default is the engine's primitive path. BlendDepth and the view/depth UV
		// mapping under test belong to DreamGUI's own world-space renderer.
		State->Canvas->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);
		UDreamTexture* Visual = Panel->CreateNewVisual<UDreamTexture>();
		if (!TestNotNull(TEXT("The panel uses the production built-in texture shader"), Visual)
			|| !TestNotNull(TEXT("The panel has a world-space canvas"), State->Canvas.Get()))
		{
			return;
		}
		Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
		Visual->SetColor(FColor::Red);
		State->Canvas->SetDepthFade(0);
		State->Canvas->SetBlendDepth(1.0f);
		Panel->SetVisibility(EDreamWidgetVisibility::Hidden);
	});

	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		ReadPicture(*this, Context.World, State->Background, State->Size);
		if (State->Panel.IsValid()) State->Panel->SetVisibility(EDreamWidgetVisibility::Visible);
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		ExpectPanel(*this, *State, Context.World, 1, false, false, TEXT("WorldDepth_FullView_BlendDepth1"));
		if (State->Canvas.IsValid()) State->Canvas->SetBlendDepth(0.0f);
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State, Rig](FDreamDriverContext& Context)
	{
		// This baseline proves the real depth write, shader sampling, and hidden/visible pixel oracles
		// work before split-screen is involved. A failure here is a fixture failure, not the UV bug.
		ExpectPanel(*this, *State, Context.World, 1, false, true, TEXT("WorldDepth_FullView_Occlusion"));
		UGameInstance* Instance = Context.World != nullptr ? Context.World->GetGameInstance() : nullptr;
		UGameViewportClient* Viewport = Rig->GetViewportClient();
		if (!TestNotNull(TEXT("The play session has a game instance"), Instance)
			|| !TestNotNull(TEXT("The play session has a viewport"), Viewport)) return;
		Viewport->SplitscreenInfo[ESplitScreenType::TwoPlayer_Horizontal] = Viewport->SplitscreenInfo[ESplitScreenType::TwoPlayer_Vertical];
		FString WhyNot;
		ULocalPlayer* Second = Instance->CreateLocalPlayer(1, WhyNot, true);
		if (!TestNotNull(*FString::Printf(TEXT("A second real player joins (%s)"), *WhyNot), Second)) return;
		State->SecondPlayer = Second;
		APlayerController* SecondController = Second->GetPlayerController(Context.World);
		if (TestNotNull(TEXT("The second player has a camera controller"), SecondController))
		{
			SecondController->SetViewTarget(State->Camera.Get());
		}
		if (State->Panel.IsValid()) State->Panel->SetVisibility(EDreamWidgetVisibility::Hidden);
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		ReadPicture(*this, Context.World, State->Background, State->Size);
		if (State->Canvas.IsValid()) State->Canvas->SetBlendDepth(1.0f);
		if (State->Panel.IsValid()) State->Panel->SetVisibility(EDreamWidgetVisibility::Visible);
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		ExpectPanel(*this, *State, Context.World, 2, false, false, TEXT("WorldDepth_SplitView_BlendDepth1"));
		if (State->Canvas.IsValid()) State->Canvas->SetBlendDepth(0.0f);
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		ExpectPanel(*this, *State, Context.World, 2, false, true, TEXT("WorldDepth_SplitView_Occlusion"));
		if (UGameInstance* Instance = Context.World != nullptr ? Context.World->GetGameInstance() : nullptr)
		{
			if (State->SecondPlayer.IsValid()) Instance->RemoveLocalPlayer(State->SecondPlayer.Get());
		}
		if (State->Camera.IsValid())
		{
			State->Camera->GetCameraComponent()->SetAspectRatio(0.5f);
			State->Camera->GetCameraComponent()->SetConstraintAspectRatio(true);
		}
		if (State->Panel.IsValid()) State->Panel->SetVisibility(EDreamWidgetVisibility::Hidden);
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		ReadPicture(*this, Context.World, State->Background, State->Size);
		if (State->Canvas.IsValid()) State->Canvas->SetBlendDepth(1.0f);
		if (State->Panel.IsValid()) State->Panel->SetVisibility(EDreamWidgetVisibility::Visible);
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		ExpectPanel(*this, *State, Context.World, 1, true, false, TEXT("WorldDepth_ConstrainedView_BlendDepth1"));
		if (State->Canvas.IsValid()) State->Canvas->SetBlendDepth(0.0f);
	});
	Steps.WaitFrames(8);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		ExpectPanel(*this, *State, Context.World, 1, true, true, TEXT("WorldDepth_ConstrainedView_Occlusion"));
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif