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

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
