// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ClearQuad.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "FXRenderingUtils.h"
#include "GameFramework/PlayerController.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "SceneRenderTargetParameters.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUISettings.h"
#include "DreamUICaptureLibrary.h"
#include "Extensions/Effects/DreamAdjustmentLayer.h"
#include "Utils/DreamUIUtils.h"

#include "Driver/DreamDriverPieRig.h"
#include "Driver/DreamDriverSequence.h"
#include "RHI/DreamGalleryStage.h"
#include "RHI/DreamPixelProbe.h"

/*
 * Groups and the blend modes that read what is behind, on a world-space canvas DreamGUI's renderer draws -- which is how the
 * designer and the level editor draw a screen-space one. A panel in a play session is placed so that one of its units is
 * one pixel of the viewport, whatever size the viewport is, and the part of the picture it covers is held to the formulas
 * and to golden images. The scene's depth occludes an element of such a mode before it blends.
 */
namespace DreamWorldSpaceCompositePixelTestLocal
{
	/** The panel, in units that are pixels: the picture of it is this size. */
	const FIntPoint PanelSize(256, 160);

	const FColor BehindColours[4] = { FColor(230, 60, 40, 255), FColor(40, 180, 90, 255), FColor(60, 90, 220, 255), FColor(200, 200, 200, 255) };
	const FColor ElementColours[4] = { FColor(250, 210, 60, 255), FColor(90, 40, 160, 255), FColor(240, 120, 200, 128), FColor(20, 200, 220, 128) };
	const EDreamUIBlendMode ChartModes[8] = {
		EDreamUIBlendMode::Overlay, EDreamUIBlendMode::Darken, EDreamUIBlendMode::ColorBurn, EDreamUIBlendMode::Hue,
		EDreamUIBlendMode::Difference, EDreamUIBlendMode::Subtract, EDreamUIBlendMode::SoftLight, EDreamUIBlendMode::Luminosity };
	constexpr int32 Swatch = 28;
	constexpr int32 Cell = 32;
	/** The backdrop's checker: two colours in squares of eight texels of a 64-texel texture, stretched over the panel. */
	const FColor CheckerA(70, 110, 150, 255);
	const FColor CheckerB(200, 170, 90, 255);

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
		TArray<TStrongObjectPtr<UTexture2D>> Textures;
		/** What writes the scene's depth for a test that needs it: an extension lives as long as a reference to it. */
		TSharedPtr<ISceneViewExtension, ESPMode::ThreadSafe> DepthWriter;
		TWeakObjectPtr<ACameraActor> Camera;
		TWeakObjectPtr<UDreamWidget> Panel;
		/** The viewport's size, and the pixel of it the panel's centre falls on. */
		FIntPoint ViewportSize = FIntPoint::ZeroValue;
		FIntPoint PanelOrigin = FIntPoint::ZeroValue;
	};

	UTexture2D* MakeTexture(FState& InState, int32 InExtent, TFunctionRef<FColor(int32 X, int32 Y)> InColourAt)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(InExtent, InExtent, PF_B8G8R8A8);
		if (Texture == nullptr || Texture->GetPlatformData() == nullptr || Texture->GetPlatformData()->Mips.Num() == 0)
		{
			return nullptr;
		}
		Texture->Filter = TF_Nearest;
		Texture->SRGB = true;
		Texture->LODGroup = TEXTUREGROUP_UI;
		FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
		FColor* Pixels = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
		for (int32 Y = 0; Y < InExtent; ++Y)
		{
			for (int32 X = 0; X < InExtent; ++X)
			{
				Pixels[Y * InExtent + X] = InColourAt(X, Y);
			}
		}
		Mip.BulkData.Unlock();
		Texture->UpdateResource();
		InState.Textures.Emplace(Texture);
		return Texture;
	}

	UTexture2D* MakeStripes(FState& InState, const FColor (&InColours)[4], bool bInAcross)
	{
		return MakeTexture(InState, 64, [&InColours, bInAcross](int32 X, int32 Y) { return InColours[(bInAcross ? X : Y) / 16]; });
	}

	UDreamWidget* AddBlock(FDreamDriverPieRig& InRig, UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FColor InColour
		, UTexture* InTexture = nullptr, EDreamUIBlendMode InMode = EDreamUIBlendMode::Alpha)
	{
		UDreamWidget* Widget = InRig.MakeWidgetWithVisual(UDreamTexture::StaticClass(), InName, InParent, InSize, InPosition);
		if (UDreamTexture* Visual = Widget != nullptr ? Cast<UDreamTexture>(Widget->GetVisual()) : nullptr)
		{
			Visual->SetTexture(InTexture != nullptr ? InTexture : FDreamUIUtils::GetDefaultWhiteTexture());
			Visual->SetColor(InColour);
			Visual->SetBlendMode(InMode);
		}
		return Widget;
	}

	/** The fixed camera every picture is taken through: at the origin, looking down +X. */
	bool SetUpCamera(FAutomationTestBase& InTest, FDreamDriverPieRig& InRig, FState& InState)
	{
		UWorld* World = InRig.GetWorld();
		ACameraActor* Camera = World != nullptr ? World->SpawnActor<ACameraActor>() : nullptr;
		if (!InTest.TestNotNull(TEXT("A fixed camera belongs to the play session"), Camera)
			|| !InTest.TestNotNull(TEXT("The player has a camera controller"), InRig.GetPlayerController()))
		{
			return false;
		}
		Camera->SetActorLocationAndRotation(FVector::ZeroVector, FRotator::ZeroRotator);
		Camera->GetCameraComponent()->SetConstraintAspectRatio(false);
		InRig.GetPlayerController()->SetViewTarget(Camera);
		InState.Camera = Camera;
		return true;
	}

	/**
	 * A panel face on to the camera, as far away as makes one of its units one pixel of the view: half the view's width times
	 * the projection's horizontal scale, read from the player's own projection once the camera has taken. Its centre on a
	 * pixel's corner, so that its edges fall on pixel edges whichever way the view's size is odd.
	 */
	bool PlacePanel(FAutomationTestBase& InTest, FDreamDriverPieRig& InRig, FState& InState, const TCHAR* InName, int32 InSortOrder, float InBlendDepth
		, UDreamWidget*& OutPanel)
	{
		ULocalPlayer* Player = InRig.GetLocalPlayer();
		UGameViewportClient* Client = InRig.GetViewportClient();
		FSceneViewProjectionData Projection;
		if (!InTest.TestTrue(TEXT("The player projects through its view"), Player != nullptr && Client != nullptr && Client->Viewport != nullptr
			&& Player->GetProjectionData(Client->Viewport, Projection)))
		{
			return false;
		}
		const FIntRect ViewRect = Projection.GetConstrainedViewRect();
		const FIntPoint View = ViewRect.Size();
		InState.ViewportSize = Client->Viewport->GetSizeXY();
		if (!InTest.TestTrue(FString::Printf(TEXT("The view %s fills the viewport %s and holds the panel"), *ViewRect.ToString(), *InState.ViewportSize.ToString())
			, ViewRect.Min == FIntPoint::ZeroValue && View == InState.ViewportSize && View.X >= PanelSize.X + 8 && View.Y >= PanelSize.Y + 8))
		{
			return false;
		}
		const double Distance = View.X * 0.5 * Projection.ProjectionMatrix.M[0][0];
		InTest.AddInfo(FString::Printf(TEXT("View %s, projection scale %.4f x %.4f, panel %.2f units away from %s"), *View.ToString()
			, Projection.ProjectionMatrix.M[0][0], Projection.ProjectionMatrix.M[1][1], Distance, *Projection.ViewOrigin.ToString()));
		// The view's centre is at half the view; the panel's centre at the pixel corner just right of and below it.
		InState.PanelOrigin = FIntPoint((View.X + 1) / 2, (View.Y + 1) / 2);
		const double Right = InState.PanelOrigin.X - View.X * 0.5;
		const double Down = InState.PanelOrigin.Y - View.Y * 0.5;
		UDreamWidget* Panel = InRig.MakeWorldPanel(InName, FTransform(Projection.ViewOrigin + FVector(Distance, Right, -Down)), FVector2D(PanelSize));
		UDreamCanvas* Canvas = Panel != nullptr ? Panel->GetComponent<UDreamCanvas>() : nullptr;
		if (!InTest.TestNotNull(TEXT("A world-space panel"), Canvas))
		{
			return false;
		}
		Canvas->SetRenderMode(EDreamRenderMode::WorldSpace_DreamUI);
		Canvas->SetSortOrder(InSortOrder);
		Canvas->SetDepthFade(0);
		Canvas->SetBlendDepth(InBlendDepth);
		InState.Panel = Panel;
		OutPanel = Panel;
		return true;
	}

	/** The panel's part of the viewport's picture: one pixel to each of its units, its top row first. */
	bool ReadPanel(FAutomationTestBase& InTest, UWorld* InWorld, const FState& InState, TArray<FColor>& OutPixels)
	{
		TArray<FColor> Whole;
		FIntPoint Size;
		if (!InTest.TestTrue(TEXT("The play viewport reads back"), UDreamUICaptureLibrary::ReadViewportPixels(UDreamUICaptureLibrary::FindViewportOf(InWorld), Whole, Size))
			|| !InTest.TestEqual(TEXT("...at the size the panel was placed for"), Size, InState.ViewportSize))
		{
			return false;
		}
		const FIntPoint Min = InState.PanelOrigin - PanelSize / 2;
		OutPixels.SetNumUninitialized(PanelSize.X * PanelSize.Y);
		for (int32 Y = 0; Y < PanelSize.Y; ++Y)
		{
			for (int32 X = 0; X < PanelSize.X; ++X)
			{
				OutPixels[Y * PanelSize.X + X] = Whole[(Min.Y + Y) * Size.X + Min.X + X];
			}
		}
		return true;
	}

	/** A panel unit's pixel of the panel's picture, the unit's (X, Y) from the panel's centre, +Y up. */
	FIntPoint PixelOf(FVector2D InUnit)
	{
		return FIntPoint(FMath::FloorToInt(InUnit.X + PanelSize.X * 0.5), FMath::FloorToInt(PanelSize.Y * 0.5 - InUnit.Y));
	}

	FColor At(const TArray<FColor>& InPixels, FVector2D InUnit)
	{
		const FIntPoint Pixel = PixelOf(InUnit);
		return InPixels[FMath::Clamp(Pixel.Y, 0, PanelSize.Y - 1) * PanelSize.X + FMath::Clamp(Pixel.X, 0, PanelSize.X - 1)];
	}

	/** The part of the panel's picture from InMin, InSize big, for a golden image of it alone. */
	TArray<FColor> Crop(const TArray<FColor>& InPixels, FIntPoint InMin, FIntPoint InSize)
	{
		TArray<FColor> Out;
		Out.SetNumUninitialized(InSize.X * InSize.Y);
		for (int32 Y = 0; Y < InSize.Y; ++Y)
		{
			for (int32 X = 0; X < InSize.X; ++X)
			{
				Out[Y * InSize.X + X] = InPixels[(InMin.Y + Y) * PanelSize.X + InMin.X + X];
			}
		}
		return Out;
	}

	FVector3f Unit(const FColor& InColour)
	{
		return FVector3f(InColour.R / 255.0f, InColour.G / 255.0f, InColour.B / 255.0f);
	}

	/** What a viewport holds where InElement of InMode is drawn over the opaque InBehind: the modes blend encoded values there. */
	FColor Expected(EDreamUIBlendMode InMode, const FColor& InBehind, const FColor& InElement)
	{
		const FVector3f Cb = Unit(InBehind);
		const float Sa = InElement.A / 255.0f;
		const FVector4f Over = FDreamUIBlendMath::OverBackdrop(InMode, Unit(InElement), Sa, FVector4f(Cb.X, Cb.Y, Cb.Z, 1.0f), false, true);
		return FColor(FMath::RoundToInt((Over.X + Cb.X * (1.0f - Sa)) * 255.0f), FMath::RoundToInt((Over.Y + Cb.Y * (1.0f - Sa)) * 255.0f)
			, FMath::RoundToInt((Over.Z + Cb.Z * (1.0f - Sa)) * 255.0f), 255);
	}

	/** The centre of a mode's cell, in panel units: four to a row on the panel's right half. */
	FVector2D CellCentre(int32 InIndex)
	{
		return FVector2D(Cell * 0.5 + Cell * (InIndex % 4), 36.0 - Cell * 1.25 * (InIndex / 4));
	}

	/** The backdrop's colour at a panel unit, as the stretched checker has it. */
	FColor CheckerAt(FVector2D InUnit)
	{
		const int32 X = FMath::FloorToInt((InUnit.X + PanelSize.X * 0.5) / PanelSize.X * 64.0);
		const int32 Y = FMath::FloorToInt((PanelSize.Y * 0.5 - InUnit.Y) / PanelSize.Y * 64.0);
		return ((X / 8) + (Y / 8)) % 2 == 0 ? CheckerA : CheckerB;
	}

	FColor Lerp(const FColor& A, const FColor& B, float T)
	{
		auto Channel = [T](uint8 InA, uint8 InB) { return static_cast<uint8>(FMath::RoundToInt(FMath::Lerp(static_cast<float>(InA), static_cast<float>(InB), T))); };
		return FColor(Channel(A.R, B.R), Channel(A.G, B.G), Channel(A.B, B.B), 255);
	}

	FVector2D ComboUnit(int32 InIndex, int32 InBehind, int32 InElement)
	{
		const FVector2D Centre = CellCentre(InIndex);
		return FVector2D(Centre.X - Swatch * 0.5 + Swatch / 8.0 + Swatch / 4.0 * InBehind, Centre.Y + Swatch * 0.5 - Swatch / 8.0 - Swatch / 4.0 * InElement);
	}

	/**
	 * The scene: a backdrop of stripes; on the left half two pairs of a blue square and a white one in Difference, the first
	 * pair passed through and the second in an isolated group, and below them an opaque red and green square overlapping in
	 * a group at half opacity; on the right half eight of the modes that read what is behind, over stripes of their own.
	 */
	void BuildScene(FDreamDriverPieRig& InRig, FState& InState, UDreamWidget* InPanel)
	{
		UTexture2D* Backdrop = MakeTexture(InState, 64, [](int32 X, int32 Y) { return ((X / 8) + (Y / 8)) % 2 == 0 ? CheckerA : CheckerB; });
		AddBlock(InRig, InPanel, TEXT("Backdrop"), FVector2D(PanelSize), FVector2D::ZeroVector, FColor::White, Backdrop);
		for (int32 Pair = 0; Pair < 2; ++Pair)
		{
			UDreamWidget* Container = InRig.MakeWidget(Pair == 0 ? TEXT("PassThrough") : TEXT("Isolated"), InPanel, FVector2D(56.0, 56.0), FVector2D(-96.0 + 64.0 * Pair, 36.0));
			AddBlock(InRig, Container, TEXT("Blue"), FVector2D(32.0, 32.0), FVector2D(-8.0, 8.0), FColor(40, 80, 200, 255));
			AddBlock(InRig, Container, TEXT("White"), FVector2D(32.0, 32.0), FVector2D(8.0, -8.0), FColor::White, nullptr, EDreamUIBlendMode::Difference);
			if (Pair == 1)
			{
				Container->AddComponent<UDreamLayerGroup>()->SetIsolation(EDreamLayerGroupIsolation::Isolated);
			}
		}
		UDreamWidget* Faded = InRig.MakeWidget(TEXT("Faded"), InPanel, FVector2D(90.0, 50.0), FVector2D(-64.0, -40.0));
		AddBlock(InRig, Faded, TEXT("Red"), FVector2D(40.0, 40.0), FVector2D(-12.0, 0.0), FColor(220, 40, 40, 255));
		AddBlock(InRig, Faded, TEXT("Green"), FVector2D(40.0, 40.0), FVector2D(12.0, 0.0), FColor(40, 200, 60, 255));
		Faded->AddComponent<UDreamLayerGroup>()->SetGroupOpacity(0.5f);
		// An adjustment layer reads what is behind it on the world-space canvas too: a narrow inversion over the checker.
		if (UDreamWidget* Inverting = InRig.MakeWidgetWithVisual(UDreamAdjustmentLayer::StaticClass(), TEXT("Inverting"), InPanel, FVector2D(16.0, 40.0), FVector2D(-116.0, -40.0)))
		{
			if (UDreamAdjustmentLayer* Layer = Cast<UDreamAdjustmentLayer>(Inverting->GetVisual()))
			{
				FDreamAdjustment Invert;
				Invert.Type = EDreamAdjustmentType::Invert;
				Layer->SetAdjustments({ Invert });
			}
		}

		UTexture2D* Behind = MakeStripes(InState, BehindColours, true);
		UTexture2D* Elements = MakeStripes(InState, ElementColours, false);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(ChartModes); ++Index)
		{
			AddBlock(InRig, InPanel, *FString::Printf(TEXT("Behind%d"), Index), FVector2D(Swatch, Swatch), CellCentre(Index), FColor::White, Behind);
		}
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(ChartModes); ++Index)
		{
			AddBlock(InRig, InPanel, *FString::Printf(TEXT("Mode%d"), Index), FVector2D(Swatch, Swatch), CellCentre(Index), FColor::White, Elements, ChartModes[Index]);
		}
	}

	/** Writes the depth the scene renderer leaves for the UI: near (occluding) on the left half of each view, far on the rest. */
	class FPartitionedSceneDepth : public FSceneViewExtensionBase
	{
	public:
		FPartitionedSceneDepth(const FAutoRegister& AutoRegister, UWorld* InWorld) : FSceneViewExtensionBase(AutoRegister), World(InWorld) {}
		virtual void SetupViewFamily(FSceneViewFamily&) override {}
		virtual void SetupView(FSceneViewFamily&, FSceneView&) override {}
		virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}
		virtual int32 GetPriority() const override { return MAX_int32; }
		virtual void PostRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InFamily) override
		{
			for (const FSceneView* View : InFamily.Views)
			{
				if (View == nullptr || !View->bIsViewInfo || View->StereoPass != EStereoscopicPass::eSSP_FULL)
				{
					continue;
				}
				const FSceneTextureShaderParameters Textures = GetSceneTextureShaderParameters(*View);
				const FRDGTextureRef Depth = Textures.SceneTextures ? Textures.SceneTextures->GetContents()->SceneDepthTexture : nullptr;
				if (Depth == nullptr)
				{
					continue;
				}
				AddClearDepthStencilPass(GraphBuilder, Depth, true, 0.0f, false, 0);
				const FIntRect RawRect = UE::FXRenderingUtils::GetRawViewRectUnsafe(*View);
				FIntRect Occluded = RawRect;
				Occluded.Max.X = RawRect.Min.X + FMath::RoundToInt32(RawRect.Width() * 0.5f);
				auto* PassParameters = GraphBuilder.AllocParameters<FRenderTargetParameters>();
				PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(Depth, ERenderTargetLoadAction::ELoad, ERenderTargetLoadAction::ENoAction
					, FExclusiveDepthStencil::DepthWrite_StencilNop);
				GraphBuilder.AddPass(RDG_EVENT_NAME("DreamGUI_Test_PartitionSceneDepth"), PassParameters, ERDGPassFlags::Raster, [Occluded](FRHICommandList& RHICmdList)
				{
					RHICmdList.SetViewport(Occluded.Min.X, Occluded.Min.Y, 0.0f, Occluded.Max.X, Occluded.Max.Y, 1.0f);
					DrawClearQuad(RHICmdList, false, FLinearColor::Black, true, 1.0f, false, 0);
				});
				break;
			}
		}
	protected:
		virtual bool IsActiveThisFrame_Internal(const FSceneViewExtensionContext& Context) const override
		{
			return World.IsValid() && World.Get() == Context.GetWorld();
		}
	private:
		TWeakObjectPtr<UWorld> World;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSpaceGroupsAndModesPixelTest,
	"DreamGUI.World.RHI.AWorldSpaceCanvasDrawsItsGroupsApartAndTheModesThatReadWhatIsBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamWorldSpaceGroupsAndModesPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSpaceCompositePixelTestLocal;
	const TSharedRef<FState> State = MakeShared<FState>();
	const TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, State](FDreamDriverPieRig& InRig)
	{
		SetUpCamera(*this, InRig, *State);
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.WaitFrames(4);
	Steps.Then([this, State, Rig](FDreamDriverContext& Context)
	{
		UDreamWidget* Panel = nullptr;
		if (PlacePanel(*this, *Rig, *State, TEXT("CompositePanel"), 0, 1.0f, Panel))
		{
			BuildScene(*Rig, *State, Panel);
		}
	});
	Steps.WaitFrames(12);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		TArray<FColor> Pixels;
		if (!State->Panel.IsValid() || !ReadPanel(*this, Context.World, *State, Pixels))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Pixels, PanelSize, TEXT("World_Composite"));
		// Passed through, the white square in Difference turns the backdrop over too; isolated, it has nothing but the blue
		// square to turn over, and is white where it covers nothing.
		const FVector2D PassCorner(-96.0 + 20.0, 36.0 - 20.0);
		const FVector2D IsolatedCorner(-32.0 + 20.0, 36.0 - 20.0);
		const FColor Checker = CheckerAt(PassCorner);
		FDreamPixelProbe::ExpectColorAt(*this, Pixels, PanelSize, PixelOf(PassCorner), FColor(255 - Checker.R, 255 - Checker.G, 255 - Checker.B, 255), 4
			, TEXT("Passed through, the white square turns the backdrop over"));
		FDreamPixelProbe::ExpectColorAt(*this, Pixels, PanelSize, PixelOf(IsolatedCorner), FColor::White, 4, TEXT("Isolated, the white square is white where it covers nothing of the group's"));
		FDreamPixelProbe::ExpectColorAt(*this, Pixels, PanelSize, PixelOf(FVector2D(-32.0 + 2.0, 36.0 + 2.0)), FColor(255 - 40, 255 - 80, 255 - 200, 255), 4
			, TEXT("...and turns over the blue square it covers"));
		// A group at half opacity is faded as one: where the green square covers the red one, no red shows through.
		const FColor Green(40, 200, 60, 255);
		for (const FVector2D& Unit : { FVector2D(-64.0, -40.0), FVector2D(-38.0, -40.0) })
		{
			FDreamPixelProbe::ExpectColorAt(*this, Pixels, PanelSize, PixelOf(Unit), Lerp(CheckerAt(Unit), Green, 0.5f), 6
				, *FString::Printf(TEXT("At %s the faded group is half its green over the backdrop, nothing of the red under it"), *Unit.ToString()));
		}
		// The inversion over the checker.
		for (const FVector2D& Unit : { FVector2D(-116.0, -30.0), FVector2D(-116.0, -50.0) })
		{
			const FColor Under = CheckerAt(Unit);
			FDreamPixelProbe::ExpectColorAt(*this, Pixels, PanelSize, PixelOf(Unit), FColor(255 - Under.R, 255 - Under.G, 255 - Under.B, 255), 4
				, *FString::Printf(TEXT("At %s the adjustment layer inverts the checker behind it"), *Unit.ToString()));
		}
		// Every combination of every mode, to the formula.
		int32 Wrong = 0;
		int32 Checked = 0;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(ChartModes); ++Index)
		{
			for (int32 Back = 0; Back < 4; ++Back)
			{
				for (int32 Element = 0; Element < 4; ++Element)
				{
					const FColor Got = At(Pixels, ComboUnit(Index, Back, Element));
					const FColor Want = Expected(ChartModes[Index], BehindColours[Back], ElementColours[Element]);
					++Checked;
					if (!FDreamPixelProbe::IsNear(Got, Want, 4) && ++Wrong <= 16)
					{
						AddError(FString::Printf(TEXT("%s, %s over %s: %s, the formula says %s"), *StaticEnum<EDreamUIBlendMode>()->GetNameStringByValue(static_cast<int64>(ChartModes[Index]))
							, *FDreamPixelProbe::Describe(ElementColours[Element]), *FDreamPixelProbe::Describe(BehindColours[Back]), *FDreamPixelProbe::Describe(Got), *FDreamPixelProbe::Describe(Want)));
					}
				}
			}
		}
		TestEqual(TEXT("Every combination of every mode on the world-space canvas is what the formula says"), Wrong, 0);
		TestEqual(TEXT("...and they were all read"), Checked, 8 * 16);
		const FIntPoint Half(PanelSize.X / 2, PanelSize.Y);
		DreamGalleryStage::CheckGolden(*this, Crop(Pixels, FIntPoint::ZeroValue, Half), Half, TEXT("World_Groups"), DreamGalleryStage::EMissingGolden::Warn);
		DreamGalleryStage::CheckGolden(*this, Crop(Pixels, FIntPoint(PanelSize.X / 2, 0), Half), Half, TEXT("World_BackdropModes"), DreamGalleryStage::EMissingGolden::Warn);
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSpaceBackdropDepthPixelTest,
	"DreamGUI.World.RHI.AnElementOfAModeThatReadsWhatIsBehindIsOccludedByTheSceneBeforeItBlends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamWorldSpaceBackdropDepthPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSpaceCompositePixelTestLocal;
	const TSharedRef<FState> State = MakeShared<FState>();
	const TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, State](FDreamDriverPieRig& InRig)
	{
		// Held by the state, which every step holds, for as long as the test runs.
		State->DepthWriter = FSceneViewExtensions::NewExtension<FPartitionedSceneDepth>(InRig.GetWorld());
		SetUpCamera(*this, InRig, *State);
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.WaitFrames(4);
	Steps.Then([this, State, Rig](FDreamDriverContext& Context)
	{
		// Behind, a panel the scene never hides: stripes. In front, at the same place but drawn after it, a panel the scene's
		// depth hides on the left half of the view, holding a white bar in Difference across both halves.
		UDreamWidget* Back = nullptr;
		UDreamWidget* Front = nullptr;
		if (!PlacePanel(*this, *Rig, *State, TEXT("BackPanel"), 0, 1.0f, Back)
			|| !PlacePanel(*this, *Rig, *State, TEXT("FrontPanel"), 1, 0.0f, Front))
		{
			return;
		}
		UTexture2D* Stripes = MakeStripes(*State, BehindColours, true);
		AddBlock(*Rig, Back, TEXT("Stripes"), FVector2D(PanelSize), FVector2D::ZeroVector, FColor::White, Stripes);
		AddBlock(*Rig, Front, TEXT("Turn"), FVector2D(PanelSize.X - 16.0, 64.0), FVector2D::ZeroVector, FColor::White, nullptr, EDreamUIBlendMode::Difference);
	});
	Steps.WaitFrames(12);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		TArray<FColor> Pixels;
		if (!ReadPanel(*this, Context.World, *State, Pixels))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Pixels, PanelSize, TEXT("World_BackdropDepth"));
		// The panel's centre is the view's, where the occluded half ends: the two stripes left of it are hidden, the two right
		// of it are not.
		for (int32 Stripe = 0; Stripe < 4; ++Stripe)
		{
			const double X = -PanelSize.X * 0.5 + PanelSize.X / 8.0 + PanelSize.X / 4.0 * Stripe;
			const FColor Behind = BehindColours[Stripe];
			const FColor Got = At(Pixels, FVector2D(X, 0.0));
			if (Stripe < 2)
			{
				TestTrue(*FString::Printf(TEXT("Stripe %d, behind the scene: the stripes as they are, %s"), Stripe, *FDreamPixelProbe::Describe(Got)), FDreamPixelProbe::IsNear(Got, Behind, 4));
			}
			else
			{
				const FColor Turned(255 - Behind.R, 255 - Behind.G, 255 - Behind.B, 255);
				TestTrue(*FString::Printf(TEXT("Stripe %d, in front of the scene: the stripes turned over, %s, want %s"), Stripe, *FDreamPixelProbe::Describe(Got), *FDreamPixelProbe::Describe(Turned))
					, FDreamPixelProbe::IsNear(Got, Turned, 4));
			}
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

namespace DreamWorldSpaceCompositePixelTestLocal
{
	/** A layer style's cell: its centre on the panel, the style in the element's own pixels on the left, drawn apart on the right. */
	FVector2D StyleCellCentre(int32 InIndex, bool bInApart)
	{
		return FVector2D(bInApart ? 64.0 : -64.0, InIndex == 0 ? 40.0 : -40.0);
	}
	const FVector2D StyledBlockSize(56.0, 36.0);
	const FIntPoint StyleCellSize(120, 76);
	const FColor StyleBackdrop(120, 130, 150, 255);
	const FColor StrokeColour(30, 20, 60, 255);

	TArray<FDreamLayerEffects> WorldStyles()
	{
		TArray<FDreamLayerEffects> Styles;
		{
			// An outside stroke over a drop shadow.
			FDreamLayerEffects S;
			FDreamStyleDropShadow& Shadow = S.DropShadows.AddDefaulted_GetRef();
			Shadow.bUseGlobalLight = false;
			Shadow.Angle = 120.0f;
			Shadow.Distance = 6.0f;
			Shadow.Size = 8.0f;
			Shadow.Opacity = 0.75f;
			FDreamStyleStroke& Stroke = S.Strokes.AddDefaulted_GetRef();
			Stroke.Size = 4.0f;
			Stroke.Position = EDreamStyleStrokePosition::Outside;
			Stroke.Color = StrokeColour;
			Styles.Add(S);
		}
		{
			// An outer glow round a bevel.
			FDreamLayerEffects S;
			S.OuterGlow.bEnabled = true;
			S.OuterGlow.Color = FColor(255, 210, 90);
			S.OuterGlow.Size = 12.0f;
			S.OuterGlow.Opacity = 1.0f;
			S.OuterGlow.BlendMode = EDreamUIBlendMode::Alpha;
			S.BevelEmboss.bEnabled = true;
			S.BevelEmboss.Size = 6.0f;
			Styles.Add(S);
		}
		return Styles;
	}

	void SetUpStyledBlock(UDreamWidget* InWidget)
	{
		if (UDreamRectBlock* Block = InWidget != nullptr ? Cast<UDreamRectBlock>(InWidget->GetVisual()) : nullptr)
		{
			Block->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetCornerRadius(FVector4(8.0, 8.0, 8.0, 8.0));
			Block->SetBodyColor(FColor(220, 140, 80, 255));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamWorldSpaceLayerStylePixelTest,
	"DreamGUI.World.RHI.AStyleDrawnApartOnAWorldSpaceCanvasLooksAsItDoesInARectBlocksOwnPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamWorldSpaceLayerStylePixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldSpaceCompositePixelTestLocal;
	const TSharedRef<FState> State = MakeShared<FState>();
	const TSharedRef<FDreamDriverPieRig> Rig = FDreamDriverPieRig::Create(*this);
	Rig->Start();
	Rig->WhenReady([this, State](FDreamDriverPieRig& InRig)
	{
		SetUpCamera(*this, InRig, *State);
	});
	FDreamDriverSequence Steps = Rig->Sequence();
	Steps.WaitFrames(4);
	Steps.Then([this, State, Rig](FDreamDriverContext& Context)
	{
		// How the designer draws a screen-space canvas: on a world-space one DreamGUI's renderer draws. Each style twice: in a rect
		// block's own pixels, and on a holder round the same block, which is drawn apart and measured from its pixels.
		UDreamWidget* Panel = nullptr;
		if (!PlacePanel(*this, *Rig, *State, TEXT("StylePanel"), 0, 1.0f, Panel))
		{
			return;
		}
		AddBlock(*Rig, Panel, TEXT("Backdrop"), FVector2D(PanelSize), FVector2D::ZeroVector, StyleBackdrop);
		const TArray<FDreamLayerEffects> Styles = WorldStyles();
		for (int32 Index = 0; Index < Styles.Num(); ++Index)
		{
			if (UDreamWidget* Own = Rig->MakeWidgetWithVisual(UDreamRectBlock::StaticClass(), FString::Printf(TEXT("OwnPixels%d"), Index), Panel, StyledBlockSize, StyleCellCentre(Index, false)))
			{
				SetUpStyledBlock(Own);
				Own->AddComponent<UDreamLayerStyle>()->SetStyle(Styles[Index]);
			}
			if (UDreamWidget* Holder = Rig->MakeWidget(FString::Printf(TEXT("Holder%d"), Index), Panel, StyledBlockSize, StyleCellCentre(Index, true)))
			{
				SetUpStyledBlock(Rig->MakeWidgetWithVisual(UDreamRectBlock::StaticClass(), FString::Printf(TEXT("Held%d"), Index), Holder, StyledBlockSize, FVector2D::ZeroVector));
				UDreamLayerStyle* Style = Holder->AddComponent<UDreamLayerStyle>();
				Style->SetStyle(Styles[Index]);
				TestEqual(*FString::Printf(TEXT("Style %d on the holder is drawn apart"), Index), Style->GetPath(), EDreamLayerStylePath::Group);
			}
		}
	});
	Steps.WaitFrames(12);
	Steps.Then([this, State](FDreamDriverContext& Context)
	{
		TArray<FColor> Pixels;
		if (!State->Panel.IsValid() || !ReadPanel(*this, Context.World, *State, Pixels))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Pixels, PanelSize, TEXT("World_LayerStyle"));
		for (int32 Index = 0; Index < 2; ++Index)
		{
			auto CellOf = [&Pixels](FVector2D InCentre)
			{
				const FIntPoint Centre = PixelOf(InCentre);
				return Crop(Pixels, Centre - StyleCellSize / 2, StyleCellSize);
			};
			const TArray<FColor> Own = CellOf(StyleCellCentre(Index, false));
			const TArray<FColor> Apart = CellOf(StyleCellCentre(Index, true));
			// The same tolerance as the parity on a render target: the flood is half a texel out here and there.
			FDreamPixelProbe::ExpectPicturesMatch(*this, Own, StyleCellSize, Apart, StyleCellSize, FString::Printf(TEXT("World_LayerStyle_ApartAgainstElement%d"), Index), 24, 0.04);
			int32 Styled = 0;
			for (const FColor& Colour : Apart)
			{
				Styled += FDreamPixelProbe::IsNear(Colour, StyleBackdrop, 6) || FDreamPixelProbe::IsNear(Colour, FColor(220, 140, 80, 255), 6) ? 0 : 1;
			}
			TestTrue(*FString::Printf(TEXT("Style %d drawn apart puts down more than the block (%d pixels neither backdrop nor block)"), Index, Styled), Styled > 400);
		}
		// The outside stroke, two units past the block's right edge, on both.
		for (const bool bApart : { false, true })
		{
			const FVector2D Point = StyleCellCentre(0, bApart) + FVector2D(StyledBlockSize.X * 0.5 + 2.0, 0.0);
			FDreamPixelProbe::ExpectColorAt(*this, Pixels, PanelSize, PixelOf(Point), StrokeColour, 6
				, bApart ? TEXT("The stroke drawn apart, outside the block") : TEXT("The stroke in the block's own pixels, outside it"));
		}
	});
	Steps.PerformLatent();
	Rig->Finish();
	return true;
}

#endif
