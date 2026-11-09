// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * A rect block's gradients written the two-colour way -- a gradient colour at a centre, the body's (or the border's) colour
 * at a radius, turned by a rotation, in either unit mode -- drawn by the built-in shader and through the rect block's
 * material, and held to golden images to the last level: these are the pictures every rect block made before its gradients
 * had stops, and they must not change by more than a rounding.
 */
namespace DreamRectBlockGradientPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** Per channel, and no pixel may be past it. */
	constexpr uint8 TwoColourTolerance = 1;

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	void BuildTwoColourGallery(FGalleryStage& InStage)
	{
		const FVector2D Size(104.0, 96.0);
		auto Place = [](int32 InColumn, int32 InRow)
		{
			return FVector2D(-195.0 + 130.0 * InColumn, 62.0 - 124.0 * InRow);
		};

		// The default: black at the middle to the body's colour at half the size.
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(TEXT("Default"), Size, Place(0, 0), FColor(240, 236, 228), 10.0f);
			Block->SetEnableBodyGradient(true);
		}
		// An ellipse moved off the middle and turned.
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(TEXT("Turned"), Size, Place(1, 0), FColor(40, 90, 200), 14.0f);
			Block->SetEnableBodyGradient(true);
			Block->SetBodyGradientColor(FColor(255, 40, 160, 255));
			Block->SetBodyGradientCenter(FVector2D(0.3, 0.6));
			Block->SetBodyGradientRadius(FVector2D(0.6, 0.3));
			Block->SetBodyGradientRotation(30.0f);
		}
		// A gradient colour that is half see-through, over an opaque body.
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(TEXT("HalfAlpha"), Size, Place(2, 0), FColor(30, 160, 90), 6.0f);
			Block->SetEnableBodyGradient(true);
			Block->SetBodyGradientColor(FColor(250, 230, 60, 128));
			Block->SetBodyGradientRadius(FVector2D(0.7, 0.7));
		}
		// The centre and the radius in units, from the bottom left.
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(TEXT("Units"), Size, Place(3, 0), FColor(200, 70, 40), 0.0f);
			Block->SetBodyGradientCenterUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetBodyGradientRadiusUnitMode(EDreamRectBlockUnitMode::Value);
			Block->SetEnableBodyGradient(true);
			Block->SetBodyGradientColor(FColor(255, 250, 240, 255));
			Block->SetBodyGradientCenter(FVector2D(24.0, 70.0));
			Block->SetBodyGradientRadius(FVector2D(80.0, 40.0));
			Block->SetBodyGradientRotation(300.0f);
		}
		// A see-through body under an opaque gradient colour.
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(TEXT("SeeThrough"), Size, Place(0, 1), FColor(90, 120, 255, 150), 18.0f);
			Block->SetEnableBodyGradient(true);
			Block->SetBodyGradientColor(FColor(255, 255, 255, 255));
			Block->SetBodyGradientCenter(FVector2D(0.5, 0.2));
			Block->SetBodyGradientRadius(FVector2D(0.8, 0.5));
		}
		// The border's gradient alone, on a plain body.
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(TEXT("Border"), Size, Place(1, 1), FColor(36, 38, 48), 16.0f);
			Block->SetEnableBorder(true);
			Block->SetBorderWidth(7.0f);
			Block->SetBorderColor(FColor(80, 200, 255));
			Block->SetEnableBorderGradient(true);
			Block->SetBorderGradientColor(FColor(255, 120, 40, 255));
			Block->SetBorderGradientCenter(FVector2D(0.8, 0.8));
			Block->SetBorderGradientRadius(FVector2D(0.9, 0.5));
			Block->SetBorderGradientRotation(60.0f);
		}
		// Both, with a half see-through border gradient and an inner shadow between them.
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(TEXT("Both"), Size, Place(2, 1), FColor(250, 200, 120), 22.0f);
			Block->SetEnableBodyGradient(true);
			Block->SetBodyGradientColor(FColor(120, 40, 20, 255));
			Block->SetBodyGradientCenter(FVector2D(0.7, 0.3));
			Block->SetBodyGradientRadius(FVector2D(0.5, 0.9));
			Block->SetBodyGradientRotation(135.0f);
			Block->SetEnableInnerShadow(true);
			Block->SetInnerShadowColor(FColor(0, 0, 0, 120));
			Block->SetInnerShadowBlur(8.0f);
			Block->SetEnableBorder(true);
			Block->SetBorderWidth(5.0f);
			Block->SetBorderColor(FColor(40, 30, 20));
			Block->SetEnableBorderGradient(true);
			Block->SetBorderGradientColor(FColor(255, 255, 255, 140));
			Block->SetBorderGradientRadius(FVector2D(0.4, 0.4));
		}
		// An opaque texture under the gradient: both colours are the texture's tint.
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(TEXT("Textured"), Size, Place(3, 1), FColor(255, 255, 255), 8.0f);
			Block->SetBodyTextureMode(EDreamRectBlockTextureMode::Texture);
			Block->SetBodyTexture(InStage.MakeChecker(32, 8, FColor(250, 250, 250), FColor(150, 170, 190)));
			Block->SetEnableBodyGradient(true);
			Block->SetBodyGradientColor(FColor(30, 60, 140, 255));
			Block->SetBodyGradientCenter(FVector2D(0.5, 0.5));
			Block->SetBodyGradientRadius(FVector2D(0.6, 0.6));
		}
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/** A rect block of a gradient's own, by its CSS, where it stands on the stage. */
	struct FPlacedGradient
	{
		const TCHAR* Name;
		const TCHAR* Css;
		FVector2D Position;
	};

	/** Every type, each with three stops or more and something turned or moved; the stops opaque. */
	const FPlacedGradient MultiStopGradients[] =
	{
		{ TEXT("Linear"), TEXT("linear-gradient(60deg, #FF2D55, #34C759 40%, #0A84FF)"), FVector2D(-185.0, 62.0) },
		{ TEXT("Radial"), TEXT("radial-gradient(70deg ellipse 45% 30% at 40% 60% in srgb-linear, #FFF3B0, #E8B64A 55%, #9C6A12)"), FVector2D(-62.0, 62.0) },
		{ TEXT("Conic"), TEXT("conic-gradient(from 30deg at 50% 50%, #FF9500, #AF52DE, #5AC8FA, #FF9500)"), FVector2D(62.0, 62.0) },
		{ TEXT("Diamond"), TEXT("diamond-gradient(20deg 40% 30% in oklab, #FFFFFF, #30B0C7 60%, #1C1C1E)"), FVector2D(185.0, 62.0) },
		{ TEXT("Reflected"), TEXT("reflected-gradient(120deg at 50% 50%, #F2F2F7, #FF375F 50%, #2C2C2E)"), FVector2D(-62.0, -62.0) },
		{ TEXT("Repeat"), TEXT("repeating-linear-gradient(90deg, #FFD60A, #FF453A 25%)"), FVector2D(62.0, -62.0) },
	};
	const FVector2D MultiStopSize(110.0, 84.0);
	/** Codes a channel may be from the evaluator's colour: float arithmetic on both sides, and the target's 8-bit encode. */
	constexpr int32 MultiStopTolerance = 3;

	/** Each gradient's rect block, square-cornered, its gradient in place of its body colour. */
	void BuildMultiStopGallery(FGalleryStage& InStage)
	{
		for (const FPlacedGradient& Placed : MultiStopGradients)
		{
			UDreamRectBlock* Block = InStage.AddRectBlock(Placed.Name, MultiStopSize, Placed.Position, FColor::White, 0.0f);
			FDreamGradient Gradient;
			if (!FDreamGradient::ParseCss(Placed.Css, Gradient))
			{
				InStage.GetTest().AddError(FString::Printf(TEXT("%s does not read"), Placed.Css));
				continue;
			}
			Block->SetEnableBodyGradient(true);
			Block->SetBodyGradient(Gradient);
		}
	}

	/** How far a pixel's colour is from Evaluate's at InUV, in codes, the largest channel. */
	int32 GetDifferenceFromEvaluate(const FDreamGradient& InGradient, const FVector2f& InUV, float InAspect, const FColor& InActual)
	{
		const FColor Expected = InGradient.Evaluate(InUV, InAspect).ToFColor(/*bSRGB*/ true);
		return FMath::Max3(FMath::Abs(InActual.R - Expected.R), FMath::Abs(InActual.G - Expected.G), FMath::Abs(InActual.B - Expected.B));
	}

	/**
	 * Every pixel inside each block (two in from its edges) held to Evaluate at the pixel's centre. A centre that lies on a
	 * hard edge (a repeating gradient's seam) may take either side of it: such a pixel is also held to Evaluate a twentieth of a
	 * pixel to either side, and passes on the nearer.
	 */
	void CheckMultiStopPixels(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize, const TCHAR* InPath)
	{
		const float Aspect = static_cast<float>(MultiStopSize.X / MultiStopSize.Y);
		const FVector2f Nudge(static_cast<float>(0.05 / MultiStopSize.X), static_cast<float>(0.05 / MultiStopSize.Y));
		for (const FPlacedGradient& Placed : MultiStopGradients)
		{
			FDreamGradient Gradient;
			FDreamGradient::ParseCss(Placed.Css, Gradient);
			const double Left = InSize.X * 0.5 + Placed.Position.X - MultiStopSize.X * 0.5;
			const double Top = InSize.Y * 0.5 - Placed.Position.Y - MultiStopSize.Y * 0.5;
			int32 Worst = 0;
			FString WorstAt;
			for (int32 Y = FMath::CeilToInt32(Top) + 2; Y < FMath::FloorToInt32(Top + MultiStopSize.Y) - 2; ++Y)
			{
				for (int32 X = FMath::CeilToInt32(Left) + 2; X < FMath::FloorToInt32(Left + MultiStopSize.X) - 2; ++X)
				{
					const FVector2f UV(static_cast<float>((X + 0.5 - Left) / MultiStopSize.X), static_cast<float>((Y + 0.5 - Top) / MultiStopSize.Y));
					const FColor& Actual = InPixels[Y * InSize.X + X];
					int32 Difference = GetDifferenceFromEvaluate(Gradient, UV, Aspect, Actual);
					if (Difference > MultiStopTolerance)
					{
						for (const FVector2f Offset : { FVector2f(Nudge.X, 0.0f), FVector2f(-Nudge.X, 0.0f), FVector2f(0.0f, Nudge.Y), FVector2f(0.0f, -Nudge.Y) })
						{
							Difference = FMath::Min(Difference, GetDifferenceFromEvaluate(Gradient, UV + Offset, Aspect, Actual));
						}
					}
					const FColor Expected = Gradient.Evaluate(UV, Aspect).ToFColor(/*bSRGB*/ true);
					if (Difference > Worst)
					{
						Worst = Difference;
						WorstAt = FString::Printf(TEXT("(%d, %d): drawn %s, Evaluate %s"), X, Y, *Actual.ToString(), *Expected.ToString());
					}
				}
			}
			InTest.TestTrue(*FString::Printf(TEXT("%s, %s: every pixel is what Evaluate says (worst %d at %s)"), InPath, Placed.Name, Worst, *WorstAt),
				Worst <= MultiStopTolerance);
		}
	}

	/** Let the scene settle and hold its picture to Golden/<InName>.png to TwoColourTolerance on every pixel. */
	void EnqueueStrictPictureCheck(const FStageRef& InStage, const FString& InName, int32 InMinDrawn)
	{
		EnqueueFramesUntilDrawn(InStage, InMinDrawn);
		EnqueueFramesUntilStable(InStage);
		EnqueueDo([InStage, InName]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (!InStage->ReadBack(Pixels, Size))
			{
				InStage->GetTest().AddError(TEXT("The picture was not read back"));
				return;
			}
			FDreamPixelProbe::ExpectMatchesGolden(InStage->GetTest(), Pixels, Size, InName, TwoColourTolerance, 0.0);
		});
	}

	/** The rect block's material compiled now: one still compiling is not drawn at all, and a picture of nothing is still. */
	void FinishRectBlockMaterial(FAutomationTestBase& InTest)
	{
		UDreamRectBlockData* Data = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultRectBlockData, TEXT("DefaultRectBlockData"));
		UMaterialInterface* Material = Data != nullptr ? Data->GetMaterial() : nullptr;
		FMaterialResource* Resource = Material != nullptr ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
		if (Resource == nullptr)
		{
			InTest.AddError(TEXT("The rect block's material was not found"));
			return;
		}
		Resource->FinishCompilation();
		if (Resource->GetCompileErrors().Num() > 0)
		{
			InTest.AddError(FString::Printf(TEXT("The rect block's material %s does not compile: %s"), *Material->GetPathName(),
				*FString::Join(Resource->GetCompileErrors(), TEXT(" | "))));
		}
	}

	bool RunTwoColourGallery(FAutomationTestBase& InTest, bool bInBuiltInShader, const TCHAR* InGolden)
	{
		FStageRef Stage = BeginStage(InTest, FIntPoint(512, 256));
		if (!Stage->IsUsable())
		{
			Stage->TearDown();
			return false;
		}
		if (!bInBuiltInShader)
		{
			FinishRectBlockMaterial(InTest);
		}
		Stage->UseBuiltInShader(bInBuiltInShader);
		BuildTwoColourGallery(*Stage);
		// A frame or two first: the stage's target can come out of the pool still holding the last test's picture.
		EnqueueFrames(Stage, 2);
		EnqueueStrictPictureCheck(Stage, InGolden, 40000);
		EnqueueTearDown(Stage);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockTwoColourBuiltInPixelTest,
	"DreamGUI.RHI.RectBlock.TwoColourGradientsDrawnByTheBuiltInShaderMatchTheirGoldenImageToALevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRectBlockTwoColourBuiltInPixelTest::RunTest(const FString& Parameters)
{
	return DreamRectBlockGradientPixelTestLocal::RunTwoColourGallery(*this, true, TEXT("RectBlock_TwoColourGradients_BuiltIn"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockTwoColourMaterialPixelTest,
	"DreamGUI.RHI.RectBlock.TwoColourGradientsDrawnThroughTheMaterialMatchTheirGoldenImageToALevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRectBlockTwoColourMaterialPixelTest::RunTest(const FString& Parameters)
{
	return DreamRectBlockGradientPixelTestLocal::RunTwoColourGallery(*this, false, TEXT("RectBlock_TwoColourGradients_Material"));
}

namespace DreamRectBlockGradientPixelTestLocal
{
	bool RunMultiStopGallery(FAutomationTestBase& InTest, bool bInBuiltInShader)
	{
		FStageRef Stage = BeginStage(InTest, FIntPoint(512, 256));
		if (!Stage->IsUsable())
		{
			Stage->TearDown();
			return false;
		}
		if (!bInBuiltInShader)
		{
			FinishRectBlockMaterial(InTest);
		}
		Stage->UseBuiltInShader(bInBuiltInShader);
		BuildMultiStopGallery(*Stage);
		EnqueueFrames(Stage, 2);
		EnqueueFramesUntilDrawn(Stage, 40000);
		EnqueueFramesUntilStable(Stage);
		const TCHAR* Path = bInBuiltInShader ? TEXT("built-in shader") : TEXT("material");
		const FString Capture = bInBuiltInShader ? TEXT("RectBlock_MultiStop_BuiltIn") : TEXT("RectBlock_MultiStop_Material");
		EnqueueDo([Stage, Path, Capture]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (!Stage->ReadBack(Pixels, Size))
			{
				Stage->GetTest().AddError(TEXT("The picture was not read back"));
				return;
			}
			FDreamPixelProbe::SaveCapture(Pixels, Size, Capture);
			CheckMultiStopPixels(Stage->GetTest(), Pixels, Size, Path);
		});
		EnqueueTearDown(Stage);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockMultiStopBuiltInPixelTest,
	"DreamGUI.RHI.RectBlock.MultiStopGradientsOfEveryTypeDrawnByTheBuiltInShaderAreWhatEvaluateSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRectBlockMultiStopBuiltInPixelTest::RunTest(const FString& Parameters)
{
	return DreamRectBlockGradientPixelTestLocal::RunMultiStopGallery(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockMultiStopMaterialPixelTest,
	"DreamGUI.RHI.RectBlock.MultiStopGradientsOfEveryTypeDrawnThroughTheMaterialAreWhatEvaluateSays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRectBlockMultiStopMaterialPixelTest::RunTest(const FString& Parameters)
{
	return DreamRectBlockGradientPixelTestLocal::RunMultiStopGallery(*this, false);
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
