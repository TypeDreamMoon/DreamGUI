// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceConstant.h"
#include "RenderingThread.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUISettings.h"
#include "Utils/DreamUIUtils.h"

#include "DreamPixelProbe.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

namespace DreamRenderScaleBlendTestLocal
{
	static constexpr int32 Extent = 256;
	static constexpr int32 FramesToDraw = 6;
	static constexpr uint8 ColourTolerance = 8;
	static const FColor Backdrop(96, 160, 224, 180);
	static const FIntPoint TintPixel(64, 128);
	static const FIntPoint AlphaPixel(192, 128);
	static const FIntPoint BackdropPixel(8, 8);

	void EnqueueStep(TFunction<bool()> InStep)
	{
		ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(InStep));
	}

	/** A non-white destination with alpha, a tint drawn with the blend under test, and an ordinary Alpha block. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{EWorldType::Game};
		TStrongObjectPtr<UTextureRenderTarget2D> Target;
		TStrongObjectPtr<UDreamWidget> Root;
		TStrongObjectPtr<UMaterialInstanceConstant> ModulateMaterial;
		TWeakObjectPtr<UDreamCanvas> Canvas;
		TWeakObjectPtr<UDreamWidget> TintWidget;
		TArray<FColor> Baseline;
		FIntPoint BaselineSize = FIntPoint::ZeroValue;
		FColor AlphaOnlyBaseline;
		FColor AlphaOnlyBackdrop;
		bool bHasBaseline = false;
		bool bHasAlphaOnlyBaseline = false;
		bool bSavedBuiltIn = false;
		EDreamUIRendererAntiAliasingMethod SavedAA = EDreamUIRendererAntiAliasingMethod::None;
		EDreamUIRendererMSAASampleCount SavedSamples = EDreamUIRendererMSAASampleCount::One;

		FStage()
		{
			UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
			bSavedBuiltIn = Settings->bUseBuiltInUIShader;
			SavedAA = Settings->AntiAliasingMethod;
			SavedSamples = Settings->MSAASampleCount;
			Settings->bUseBuiltInUIShader = true;
			Settings->AntiAliasingMethod = EDreamUIRendererAntiAliasingMethod::None;
			Settings->MSAASampleCount = EDreamUIRendererMSAASampleCount::One;
		}

		~FStage()
		{
			if (IsValid(Root.Get()))
			{
				Root->DestroyWidget();
			}
			FlushRenderingCommands();
			Root.Reset();
			Target.Reset();
			ModulateMaterial.Reset();
			UDreamUISettings* Settings = GetMutableDefault<UDreamUISettings>();
			Settings->bUseBuiltInUIShader = bSavedBuiltIn;
			Settings->AntiAliasingMethod = SavedAA;
			Settings->MSAASampleCount = SavedSamples;
		}

		UDreamTexture* AddBlock(FVector2D InPosition, FColor InColour, UDreamWidget*& OutWidget)
		{
			OutWidget = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			OutWidget->SetWidth(80.0f);
			OutWidget->SetHeight(80.0f);
			OutWidget->OnRegister();
			OutWidget->TrySetParent(Root.Get(), false);
			OutWidget->SetAnchoredPosition(InPosition);
			UDreamTexture* Visual = OutWidget->CreateNewVisual<UDreamTexture>();
			if (Visual != nullptr)
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
				Visual->SetColor(InColour);
			}
			return Visual;
		}

		bool Build(FAutomationTestBase& InTest, bool bInMaterialModulate, EDreamUIBlendMode InBuiltInBlend)
		{
			if (!InTest.TestNotNull(TEXT("a world whose render-target canvas draws through a real RHI"), World.World))
			{
				return false;
			}
			UTextureRenderTarget2D* NewTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient);
			NewTarget->InitCustomFormat(Extent, Extent, PF_B8G8R8A8, false);
			NewTarget->UpdateResourceImmediate(true);
			Target.Reset(NewTarget);
			UDreamWidget* NewRoot = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			NewRoot->SetWidth(static_cast<float>(Extent));
			NewRoot->SetHeight(static_cast<float>(Extent));
			NewRoot->OnRegister();
			Root.Reset(NewRoot);
			UDreamCanvas* NewCanvas = NewRoot->AddComponent<UDreamCanvas>();
			if (!InTest.TestNotNull(TEXT("the render-target canvas"), NewCanvas))
			{
				return false;
			}
			Canvas = NewCanvas;
			NewCanvas->SetRenderMode(EDreamRenderMode::RenderTarget);
			NewCanvas->SetRenderTargetClearColor(Backdrop);
			NewCanvas->SetRenderTargetResolutionScale(1.0f);
			NewCanvas->SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget);
			NewCanvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
			NewCanvas->SetRenderTarget(NewTarget);
			NewCanvas->SetEnableDepthTest(false);
			NewCanvas->SetScreenSpaceRenderScale(1.0f);

			UDreamWidget* NewTint = nullptr;
			UDreamTexture* Tint = AddBlock(FVector2D(-64.0, 0.0), FColor(128, 200, 64, 192), NewTint);
			TintWidget = NewTint;
			UDreamWidget* AlphaWidget = nullptr;
			UDreamTexture* Alpha = AddBlock(FVector2D(64.0, 0.0), FColor(220, 80, 48, 128), AlphaWidget);
			if (!InTest.TestNotNull(TEXT("the tint visual"), Tint) || !InTest.TestNotNull(TEXT("the Alpha visual"), Alpha))
			{
				return false;
			}
			Alpha->SetBlendMode(EDreamUIBlendMode::Alpha);
			if (bInMaterialModulate)
			{
				UMaterialInterface* Parent = NewCanvas->GetDefaultMaterial();
				if (!InTest.TestNotNull(TEXT("the existing DreamGUI UI material"), Parent))
				{
					return false;
				}
				UMaterialInstanceConstant* Material = NewObject<UMaterialInstanceConstant>(GetTransientPackage(), NAME_None, RF_Transient);
				ModulateMaterial.Reset(Material);
				Material->SetParentEditorOnly(Parent, false);
				Material->BasePropertyOverrides.bOverride_BlendMode = true;
				Material->BasePropertyOverrides.BlendMode = BLEND_Modulate;
				Material->PostEditChange();
				InTest.TestEqual(TEXT("the custom material really overrides its parent's blend mode"), Material->GetBlendMode(), BLEND_Modulate);
				Tint->SetOverrideMaterial(Material);
			}
			else
			{
				Tint->SetBlendMode(InBuiltInBlend);
			}
			return true;
		}

		void Frame()
		{
			if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World))
			{
				Manager->Tick(1.0f / 30.0f);
			}
			World.World->SendAllEndOfFrameUpdates();
		}

		FColor Pixel(const TArray<FColor>& InPixels, FIntPoint InPosition) const
		{
			return InPixels[InPosition.Y * Extent + InPosition.X];
		}

		bool Read(FAutomationTestBase& InTest, TArray<FColor>& OutPixels, FIntPoint& OutSize)
		{
			return InTest.TestTrue(TEXT("the GPU target reads back"), FDreamPixelProbe::ReadBack(Target.Get(), OutPixels, OutSize))
				&& InTest.TestEqual(TEXT("the target size"), OutSize, FIntPoint(Extent, Extent))
				&& InTest.TestEqual(TEXT("all GPU pixels are present"), OutPixels.Num(), Extent * Extent);
		}
	};

	void EnqueueFrames(const TSharedRef<FStage>& InStage)
	{
		const TSharedRef<int32> Frames = MakeShared<int32>(0);
		EnqueueStep([InStage, Frames]()
		{
			InStage->Frame();
			return ++(*Frames) >= FramesToDraw;
		});
	}

	bool RunComparison(FAutomationTestBase& InTest, bool bInMaterialModulate, EDreamUIBlendMode InBuiltInBlend = EDreamUIBlendMode::Multiply)
	{
		const TSharedRef<FStage> Stage = MakeShared<FStage>();
		if (!Stage->Build(InTest, bInMaterialModulate, InBuiltInBlend))
		{
			return false;
		}
		const bool bAdditive = !bInMaterialModulate && InBuiltInBlend == EDreamUIBlendMode::Additive;
		const FString Name = bInMaterialModulate ? TEXT("RenderScale_MaterialModulate")
			: bAdditive ? TEXT("RenderScale_BuiltInAdditive") : TEXT("RenderScale_BuiltInMultiply");
		const TSharedRef<int32> Frames = MakeShared<int32>(0);
		const TSharedRef<double> Deadline = MakeShared<double>(0.0);
		EnqueueStep([&InTest, Stage, Name, Frames, Deadline, bAdditive]()
		{
			if (*Deadline == 0.0)
			{
				*Deadline = FPlatformTime::Seconds() + 90.0;
			}
			Stage->Frame();
			if (++(*Frames) < FramesToDraw)
			{
				return false;
			}
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			const bool bRead = FDreamPixelProbe::ReadBack(Stage->Target.Get(), Pixels, Size)
				&& Size == FIntPoint(Extent, Extent) && Pixels.Num() == Extent * Extent;
			// A pending shader must not let two identical clear targets masquerade as a successful comparison.
			const bool bDrawn = bRead && !FDreamPixelProbe::IsNear(Stage->Pixel(Pixels, TintPixel), Stage->Pixel(Pixels, BackdropPixel), ColourTolerance)
				&& !FDreamPixelProbe::IsNear(Stage->Pixel(Pixels, AlphaPixel), Stage->Pixel(Pixels, BackdropPixel), ColourTolerance);
			if (!bDrawn)
			{
				if (FPlatformTime::Seconds() > *Deadline)
				{
					InTest.AddError(Name + TEXT(": the tint and Alpha block did not both draw within 90 seconds."));
					if (bRead) { FDreamPixelProbe::SaveCapture(Pixels, Size, Name + TEXT("_NotDrawn")); }
					return true;
				}
				return false;
			}
			const FColor Tint = Stage->Pixel(Pixels, TintPixel);
			const FColor Clear = Stage->Pixel(Pixels, BackdropPixel);
			InTest.TestTrue(bAdditive ? TEXT("Additive increases every non-white backdrop channel") : TEXT("Multiply/Modulate darkens every non-white backdrop channel"),
				bAdditive ? Tint.R > Clear.R && Tint.G > Clear.G && Tint.B > Clear.B : Tint.R < Clear.R && Tint.G < Clear.G && Tint.B < Clear.B);
			InTest.TestEqual(TEXT("the tint preserves destination alpha at scale 1"), Tint.A, Clear.A);
			InTest.TestTrue(TEXT("the destination has partial alpha"), Clear.A > 0 && Clear.A < 255);
			InTest.TestTrue(TEXT("ordinary Alpha increases destination alpha"), Stage->Pixel(Pixels, AlphaPixel).A > Clear.A);
			Stage->Baseline = MoveTemp(Pixels);
			Stage->BaselineSize = Size;
			Stage->bHasBaseline = true;
			FDreamPixelProbe::SaveCapture(Stage->Baseline, Size, Name + TEXT("_Scale1"));
			Stage->Canvas->SetScreenSpaceRenderScale(0.5f);
			return true;
		});
		EnqueueFrames(Stage);
		EnqueueStep([&InTest, Stage, Name, bAdditive]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (Stage->bHasBaseline && Stage->Read(InTest, Pixels, Size))
			{
				// Additive can scale, so bilinear filtering changes boundary pixels; its interiors must still match.
				FDreamPixelProbe::ExpectPicturesMatch(InTest, Pixels, Size, Stage->Baseline, Stage->BaselineSize,
					Name + TEXT("_ScaleHalf"), 2, bAdditive ? 0.025 : 0.0);
				FDreamPixelProbe::ExpectColorAt(InTest, Pixels, Size, TintPixel, Stage->Pixel(Stage->Baseline, TintPixel), 2,
					TEXT("scale 0.5 keeps the tint over the original destination, including alpha"));
				FDreamPixelProbe::ExpectColorAt(InTest, Pixels, Size, BackdropPixel, Stage->Pixel(Stage->Baseline, BackdropPixel), 2,
					TEXT("scale 0.5 preserves uncovered destination colour and alpha"));
				FDreamPixelProbe::ExpectColorAt(InTest, Pixels, Size, AlphaPixel, Stage->Pixel(Stage->Baseline, AlphaPixel), 2,
					TEXT("Alpha in the same layer keeps its baseline composite"));
				InTest.TestEqual(TEXT("rendering retains the caller's configured scale"), Stage->Canvas->GetScreenSpaceRenderScale(), 0.5f);
			}
			// With the tint gone, this layer can use the smaller transparent target again.
			if (UDreamWidget* Tint = Stage->TintWidget.Get()) { Tint->DestroyWidget(); }
			Stage->Canvas->SetScreenSpaceRenderScale(1.0f);
			return true;
		});
		EnqueueFrames(Stage);
		EnqueueStep([&InTest, Stage]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (Stage->bHasBaseline && Stage->Read(InTest, Pixels, Size))
			{
				Stage->AlphaOnlyBaseline = Stage->Pixel(Pixels, AlphaPixel);
				Stage->AlphaOnlyBackdrop = Stage->Pixel(Pixels, BackdropPixel);
				Stage->bHasAlphaOnlyBaseline = true;
				FDreamPixelProbe::ExpectColorAt(InTest, Pixels, Size, TintPixel, Stage->AlphaOnlyBackdrop, 2, TEXT("the removed tint leaves the backdrop"));
			}
			Stage->Canvas->SetScreenSpaceRenderScale(0.5f);
			return true;
		});
		EnqueueFrames(Stage);
		EnqueueStep([&InTest, Stage, Name]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (Stage->bHasAlphaOnlyBaseline && Stage->Read(InTest, Pixels, Size))
			{
				FDreamPixelProbe::SaveCapture(Pixels, Size, Name + TEXT("_AlphaOnlyScaleHalf"));
				FDreamPixelProbe::ExpectColorAt(InTest, Pixels, Size, AlphaPixel, Stage->AlphaOnlyBaseline, ColourTolerance,
					TEXT("ordinary Alpha alone still composites correctly through the scaled target"));
				FDreamPixelProbe::ExpectColorAt(InTest, Pixels, Size, BackdropPixel, Stage->AlphaOnlyBackdrop, ColourTolerance,
					TEXT("the scaled transparent target preserves uncovered destination colour and alpha"));
				InTest.TestEqual(TEXT("an Alpha-only layer keeps its configured scale"), Stage->Canvas->GetScreenSpaceRenderScale(), 0.5f);
			}
			return true;
		});
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRenderScaleMultiplyPixelTest,
	"DreamGUI.RHI.AScaledBuiltInMultiplyKeepsItsDestinationColourAndAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRenderScaleMultiplyPixelTest::RunTest(const FString& Parameters)
{
	return DreamRenderScaleBlendTestLocal::RunComparison(*this, false);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRenderScaleMaterialModulatePixelTest,
	"DreamGUI.RHI.AScaledCustomModulateMaterialKeepsItsDestinationColourAndAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRenderScaleMaterialModulatePixelTest::RunTest(const FString& Parameters)
{
	return DreamRenderScaleBlendTestLocal::RunComparison(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRenderScaleAdditivePixelTest,
	"DreamGUI.RHI.AScaledBuiltInAdditiveKeepsItsColourAndDestinationAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRenderScaleAdditivePixelTest::RunTest(const FString& Parameters)
{
	return DreamRenderScaleBlendTestLocal::RunComparison(*this, false, EDreamUIBlendMode::Additive);
}

#endif
