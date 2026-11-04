// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialParameters.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipMask.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * Clip masks and clipping to the layer below, drawn: a picture of each kind held to its golden image, the built-in shader
 * and the material path held to each other, and the one property the two halves of a clip exist for -- a pixel that
 * shows is a point that hits, and a point that hits is a pixel that shows -- asked of every masked clip at once.
 */
namespace DreamClipMaskPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** White, its alpha falling from 1 at the centre to 0 at the edge: a soft spot. */
	UTexture2D* MakeSoftSpot(FGalleryStage& InStage, int32 InExtent = 64)
	{
		return InStage.MakeTexture(InExtent, [InExtent](int32 X, int32 Y)
		{
			const float Half = InExtent * 0.5f;
			const float Distance = FVector2f(X + 0.5f - Half, Y + 0.5f - Half).Size() / Half;
			return FColor(255, 255, 255, static_cast<uint8>(FMath::Clamp(1.0f - Distance, 0.0f, 1.0f) * 255.0f));
		});
	}

	/** A clip of InSize at InPosition with InMask, and a block of InContentSize in it showing InTexture. Returns the block. */
	UDreamWidget* AddMaskedClip(FGalleryStage& InStage, const TCHAR* InName, FVector2D InSize, FVector2D InPosition, const FDreamClipMask& InMask,
		UTexture* InTexture, FVector2D InContentSize, FColor InColour = FColor::White)
	{
		UDreamWidget* Clip = InStage.AddWidget(InName, InSize, InPosition);
		Clip->SetClipping(EDreamWidgetClipping::ClipToBounds);
		Clip->SetClipMask(InMask);
		return InStage.AddBlock(*FString::Printf(TEXT("%sContent"), InName), InContentSize, FVector2D::ZeroVector, InColour, InTexture, Clip);
	}

	FDreamClipMask Shaped(EDreamClipMaskShape InShape)
	{
		FDreamClipMask Mask;
		Mask.Shape = InShape;
		return Mask;
	}

	FDreamClipMask Procedural(EDreamClipMaskPattern InPattern)
	{
		FDreamClipMask Mask;
		Mask.Source = EDreamClipMaskSource::Procedural;
		Mask.Pattern = InPattern;
		return Mask;
	}

	void BuildShapes(FGalleryStage& InStage)
	{
		UTexture2D* Gradient = InStage.MakeGradient(64, FColor(255, 120, 60, 255), FColor(60, 120, 255, 255));
		const FVector2D Size(76.0, 100.0);
		const FVector2D Content(96.0, 120.0);
		UDreamWidget* Rounded = InStage.AddWidget(TEXT("Rounded"), Size, FVector2D(-84.0, 60.0));
		Rounded->SetClipping(EDreamWidgetClipping::ClipToBounds);
		Rounded->SetClippingCornerRadius(FVector4f(18.0f, 18.0f, 18.0f, 18.0f));
		InStage.AddBlock(TEXT("RoundedContent"), Content, FVector2D::ZeroVector, FColor::White, Gradient, Rounded);
		AddMaskedClip(InStage, TEXT("Ellipse"), Size, FVector2D(0.0, 60.0), Shaped(EDreamClipMaskShape::Ellipse), Gradient, Content);
		FDreamClipMask Hexagon = Shaped(EDreamClipMaskShape::Polygon);
		Hexagon.Sides = 6;
		AddMaskedClip(InStage, TEXT("Hexagon"), Size, FVector2D(84.0, 60.0), Hexagon, Gradient, Content);
		FDreamClipMask Star = Shaped(EDreamClipMaskShape::Star);
		Star.Sides = 5;
		Star.InnerRatio = 0.45f;
		AddMaskedClip(InStage, TEXT("Star"), Size, FVector2D(-84.0, -60.0), Star, Gradient, Content);
		FDreamClipMask Ring = Shaped(EDreamClipMaskShape::Ring);
		Ring.InnerRatio = 0.55f;
		AddMaskedClip(InStage, TEXT("Ring"), Size, FVector2D(0.0, -60.0), Ring, Gradient, Content);
		FDreamClipMask Triangle = Shaped(EDreamClipMaskShape::Polygon);
		Triangle.Sides = 3;
		Triangle.Roundness = 8.0f;
		AddMaskedClip(InStage, TEXT("Triangle"), Size, FVector2D(84.0, -60.0), Triangle, Gradient, Content);
	}

	void BuildTextures(FGalleryStage& InStage)
	{
		UTexture2D* Checker = InStage.MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 90, 100, 255));
		UTexture2D* Spot = MakeSoftSpot(InStage);
		UTexture2D* SmallChecker = InStage.MakeChecker(16, 4, FColor::White, FColor::Black);
		FDreamClipMask Stretched;
		Stretched.Source = EDreamClipMaskSource::Texture;
		Stretched.Texture = Spot;
		AddMaskedClip(InStage, TEXT("Stretched"), FVector2D(110.0, 110.0), FVector2D(-64.0, 64.0), Stretched, Checker, FVector2D(130.0, 130.0));
		FDreamClipMask Tiled;
		Tiled.Source = EDreamClipMaskSource::Texture;
		Tiled.Texture = SmallChecker;
		Tiled.Channel = EDreamClipMaskChannel::Luminance;
		Tiled.Fill = EDreamClipMaskFill::Tile;
		AddMaskedClip(InStage, TEXT("Tiled"), FVector2D(110.0, 110.0), FVector2D(64.0, 64.0), Tiled, nullptr, FVector2D(130.0, 130.0), FColor(255, 200, 64, 255));
		FDreamClipMask Fitted;
		Fitted.Source = EDreamClipMaskSource::Texture;
		Fitted.Texture = Spot;
		Fitted.Fill = EDreamClipMaskFill::Fit;
		Fitted.Threshold = 0.4f;
		Fitted.Softness = 0.05f;
		AddMaskedClip(InStage, TEXT("Fitted"), FVector2D(220.0, 90.0), FVector2D(0.0, -70.0), Fitted, Checker, FVector2D(240.0, 110.0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryMaskShapesTest,
	"DreamGUI.RHI.ClipMask.ShapesMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryMaskShapesTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	BuildShapes(*Stage);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Mask_Shapes"), 20000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryMaskFeatherTest,
	"DreamGUI.RHI.ClipMask.FeathersMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryMaskFeatherTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 90, 100, 255));
	FDreamClipMask Sides;
	Sides.Feather = FMargin(40.0f, 0.0f, 10.0f, 24.0f);
	AddMaskedClip(*Stage, TEXT("Sides"), FVector2D(220.0, 90.0), FVector2D(0.0, 62.0), Sides, Checker, FVector2D(240.0, 110.0));
	FDreamClipMask Ellipse = Shaped(EDreamClipMaskShape::Ellipse);
	Ellipse.Feather = FMargin(20.0f);
	AddMaskedClip(*Stage, TEXT("SoftEllipse"), FVector2D(110.0, 90.0), FVector2D(-62.0, -62.0), Ellipse, nullptr, FVector2D(130.0, 110.0), FColor(255, 200, 64, 255));
	FDreamClipMask Star = Shaped(EDreamClipMaskShape::Star);
	Star.Feather = FMargin(6.0f);
	AddMaskedClip(*Stage, TEXT("SoftStar"), FVector2D(100.0, 100.0), FVector2D(62.0, -62.0), Star, nullptr, FVector2D(120.0, 120.0), FColor(90, 220, 255, 255));
	EnqueuePictureCheck(Stage, TEXT("Gallery_Mask_Feather"), 15000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryMaskTextureTest,
	"DreamGUI.RHI.ClipMask.TextureMasksMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryMaskTextureTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	BuildTextures(*Stage);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Mask_Texture"), 15000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryMaskProceduralTest,
	"DreamGUI.RHI.ClipMask.ProceduralMasksMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryMaskProceduralTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	TArray<FDreamClipMask> Masks;
	{
		FDreamClipMask Mask = Procedural(EDreamClipMaskPattern::Linear);
		Mask.Rotation = 45.0f;
		Masks.Add(Mask);
	}
	Masks.Add(Procedural(EDreamClipMaskPattern::Radial));
	{
		FDreamClipMask Clock = Procedural(EDreamClipMaskPattern::Angular);
		Clock.Threshold = 0.3f;
		Clock.Softness = 0.0f;
		Masks.Add(Clock);
	}
	Masks.Add(Procedural(EDreamClipMaskPattern::Diamond));
	{
		FDreamClipMask Blinds = Procedural(EDreamClipMaskPattern::Blinds);
		Blinds.Frequency = 5.0f;
		Masks.Add(Blinds);
	}
	{
		FDreamClipMask Checker = Procedural(EDreamClipMaskPattern::Checker);
		Checker.Frequency = 4.0f;
		Checker.Softness = 0.0f;
		Masks.Add(Checker);
	}
	{
		FDreamClipMask Noise = Procedural(EDreamClipMaskPattern::Noise);
		Noise.Frequency = 6.0f;
		Noise.Seed = 3;
		Masks.Add(Noise);
	}
	{
		FDreamClipMask Wipe = Procedural(EDreamClipMaskPattern::Linear);
		Wipe.Threshold = 0.6f;
		Wipe.Softness = 0.05f;
		Masks.Add(Wipe);
	}
	{
		FDreamClipMask Iris = Procedural(EDreamClipMaskPattern::Radial);
		Iris.Threshold = 0.7f;
		Iris.Softness = 0.02f;
		Iris.bInvert = true;
		Masks.Add(Iris);
	}
	for (int32 Index = 0; Index < Masks.Num(); ++Index)
	{
		const FVector2D Position((Index % 3 - 1) * 84.0, (1 - Index / 3) * 84.0);
		AddMaskedClip(*Stage, *FString::Printf(TEXT("Pattern%d"), Index), FVector2D(76.0, 76.0), Position, Masks[Index], nullptr, FVector2D(90.0, 90.0), FColor::White);
	}
	EnqueuePictureCheck(Stage, TEXT("Gallery_Mask_Procedural"), 15000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryMaskInvertTest,
	"DreamGUI.RHI.ClipMask.InvertedMasksCutHolesMatchingTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryMaskInvertTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	// A spotlight: a checker under a dark veil, the veil cut by an inverted, feathered ellipse -- and by an inverted rect
	// beside it. The veil reaches past both holes' rects, and is drawn there: an inverted mask shows outside its rect.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 90, 100, 255));
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Checker);
	FDreamClipMask Spot = Shaped(EDreamClipMaskShape::Ellipse);
	Spot.Feather = FMargin(14.0f);
	Spot.bInvert = true;
	UDreamWidget* SpotClip = Stage->AddWidget(TEXT("Spotlight"), FVector2D(110.0, 80.0), FVector2D(-40.0, 30.0));
	SpotClip->SetClipping(EDreamWidgetClipping::ClipToBounds);
	SpotClip->SetClipMask(Spot);
	FDreamClipMask Hole;
	Hole.bInvert = true;
	UDreamWidget* HoleClip = Stage->AddWidget(TEXT("Hole"), FVector2D(60.0, 40.0), FVector2D(70.0, -70.0), SpotClip);
	HoleClip->SetClipping(EDreamWidgetClipping::ClipToBounds);
	HoleClip->SetClippingCornerRadius(FVector4f(10.0f, 10.0f, 10.0f, 10.0f));
	HoleClip->SetClipMask(Hole);
	Stage->AddBlock(TEXT("Veil"), FVector2D(256.0, 256.0), FVector2D(40.0, -30.0), FColor(10, 10, 20, 200), nullptr, HoleClip);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Mask_Invert"), 20000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryClipToBelowImageTest,
	"DreamGUI.RHI.ClipToBelow.AnImageLayerBelowMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryClipToBelowImageTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	// A soft spot below at two thirds its opacity, a checker clipped to it; then a gradient below, two siblings clipped to
	// it in a row, the second reaching past it.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 90, 100, 255));
	UTexture2D* Spot = MakeSoftSpot(*Stage);
	Stage->AddBlock(TEXT("Spot"), FVector2D(120.0, 120.0), FVector2D(-60.0, 50.0), FColor(255, 255, 255, 170), Spot);
	Stage->AddBlock(TEXT("OnSpot"), FVector2D(150.0, 90.0), FVector2D(-40.0, 50.0), FColor::White, Checker)->SetClipToBelow(true);
	Stage->AddBlock(TEXT("Base"), FVector2D(140.0, 70.0), FVector2D(40.0, -70.0), FColor(40, 120, 255, 255));
	Stage->AddBlock(TEXT("Stripe"), FVector2D(40.0, 200.0), FVector2D(10.0, -40.0), FColor(255, 80, 60, 255))->SetClipToBelow(true);
	Stage->AddBlock(TEXT("Corner"), FVector2D(80.0, 80.0), FVector2D(110.0, -100.0), FColor(255, 220, 60, 255))->SetClipToBelow(true);
	EnqueuePictureCheck(Stage, TEXT("Gallery_ClipToBelow_Image"), 8000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryClipToBelowNineSliceTest,
	"DreamGUI.RHI.ClipToBelow.ANineSlicedLayerBelowMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryClipToBelowNineSliceTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	// A frame -- a nine-sliced texture without its centre, its corners rounded in its alpha -- stretched wide, a gradient
	// clipped to it: the gradient shows in the frame's band only, its corners as round as the texture's.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* FrameTexture = Stage->MakeTexture(32, [](int32 X, int32 Y)
	{
		const FVector2f FromCorner(FMath::Max(FMath::Abs(X + 0.5f - 16.0f) - 6.0f, 0.0f), FMath::Max(FMath::Abs(Y + 0.5f - 16.0f) - 6.0f, 0.0f));
		return FColor(255, 255, 255, FromCorner.Size() <= 10.0f ? 255 : 0);
	});
	UTexture2D* Gradient = Stage->MakeGradient(64, FColor(255, 120, 60, 255), FColor(60, 120, 255, 255));
	UDreamWidget* Frame = Stage->AddBlock(TEXT("Frame"), FVector2D(220.0, 120.0), FVector2D(0.0, 50.0), FColor(255, 255, 255, 60), FrameTexture);
	if (UDreamTexture* Sliced = Cast<UDreamTexture>(Frame->GetVisual()))
	{
		FDreamUISpriteInfo Info = Sliced->GetSpriteInfo();
		Info.Border = FMargin(12.0f);
		Sliced->SetSpriteInfo(Info);
		Sliced->SetDrawType(EDreamUISpriteDrawType::SlicedFrame);
	}
	Stage->AddBlock(TEXT("InFrame"), FVector2D(240.0, 140.0), FVector2D(0.0, 50.0), FColor::White, Gradient)->SetClipToBelow(true);
	UDreamWidget* Panel = Stage->AddBlock(TEXT("Panel"), FVector2D(160.0, 80.0), FVector2D(0.0, -70.0), FColor::White, FrameTexture);
	if (UDreamTexture* Sliced = Cast<UDreamTexture>(Panel->GetVisual()))
	{
		FDreamUISpriteInfo Info = Sliced->GetSpriteInfo();
		Info.Border = FMargin(12.0f);
		Sliced->SetSpriteInfo(Info);
		Sliced->SetDrawType(EDreamUISpriteDrawType::Sliced);
	}
	Stage->AddBlock(TEXT("OnPanel"), FVector2D(220.0, 60.0), FVector2D(30.0, -80.0), FColor(255, 220, 60, 255), nullptr)->SetClipToBelow(true);
	EnqueuePictureCheck(Stage, TEXT("Gallery_ClipToBelow_NineSlice"), 8000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryClipToBelowRectBlockTest,
	"DreamGUI.RHI.ClipToBelow.ARectBlockLayerBelowMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryClipToBelowRectBlockTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 90, 100, 255));
	Stage->AddRectBlock(TEXT("Card"), FVector2D(180.0, 130.0), FVector2D(0.0, 20.0), FColor(40, 60, 90, 255), 36.0f);
	Stage->AddBlock(TEXT("Photo"), FVector2D(200.0, 90.0), FVector2D(0.0, 50.0), FColor::White, Checker)->SetClipToBelow(true);
	Stage->AddBlock(TEXT("Badge"), FVector2D(90.0, 90.0), FVector2D(90.0, -40.0), FColor(255, 80, 60, 255))->SetClipToBelow(true);
	EnqueuePictureCheck(Stage, TEXT("Gallery_ClipToBelow_RectBlock"), 10000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGalleryMaskMaterialPathTest,
	"DreamGUI.RHI.ClipMask.TheBuiltInShaderAndTheMaterialPathMaskAlike",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGalleryMaskMaterialPathTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	// Masks are evaluated in DreamUIClip.ush whoever draws: the built-in shader binds the mask textures itself, the default
	// UI material gets them as DreamUI_ClipMaskTexture0 and 1 through MF_DreamUI_Shade. The texture scene both ways.
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	BuildTextures(*Stage);
	TSharedRef<TArray<FColor>> BuiltIn = MakeShared<TArray<FColor>>();
	TSharedRef<FIntPoint> BuiltInSize = MakeShared<FIntPoint>(FIntPoint::ZeroValue);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Mask_BuiltInShader"), 15000);
	EnqueueDo([Stage, BuiltIn, BuiltInSize]()
	{
		Stage->ReadBack(*BuiltIn, *BuiltInSize);
		Stage->UseBuiltInShader(false);
		Stage->GetCanvas()->MarkCanvasUpdate(true);
	});
	EnqueuePictureCheck(Stage, TEXT("Gallery_Mask_MaterialPath"), 15000);
	EnqueueDo([this, Stage, BuiltIn, BuiltInSize]()
	{
		TArray<FColor> Material;
		FIntPoint MaterialSize = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("both pictures read back"), BuiltIn->Num() > 0 && Stage->ReadBack(Material, MaterialSize)))
		{
			return;
		}
		const UMaterialInterface* DefaultMaterial = Stage->GetCanvas() != nullptr ? Stage->GetCanvas()->GetDefaultMaterial() : nullptr;
		UTexture* Unused = nullptr;
		if (DefaultMaterial != nullptr && !DefaultMaterial->GetTextureParameterValue(FHashedMaterialParameterInfo(UDreamCanvas::DreamUI_ClipMaskTexture0_MaterialParameterName), Unused))
		{
			AddWarning(FString::Printf(TEXT("Gallery_Mask_BuiltInAgainstMaterial is not compared: the default UI material %s has no %s parameter, so it predates mask textures. Regenerate MF_DreamUI_Shade from DShader/ and run this test again."),
				*DefaultMaterial->GetPathName(), *UDreamCanvas::DreamUI_ClipMaskTexture0_MaterialParameterName.ToString()));
			return;
		}
		FDreamPixelProbe::ExpectPicturesMatch(*this, *BuiltIn, *BuiltInSize, Material, MaterialSize, TEXT("Gallery_Mask_BuiltInAgainstMaterial"), 12, 0.01);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskPixelAgainstHitTest,
	"DreamGUI.RHI.ClipMask.AShownPixelIsAHitAndAHitIsAShownPixel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamClipMaskPixelAgainstHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamClipMaskPixelTestLocal;
	/**
	 * White blocks in masked clips that hit test by their masks, and one clipped to the layer below, on the dark backdrop.
	 * Every other pixel is read and the point under its centre is hit-tested: where the pixel is clearly shown (more than
	 * 70% white) the point must hit, and where it is clearly not (under 30%) it must miss. Pixels on an edge's ramp say
	 * neither and are not asked. A wrong frame, a wrong formula, a wrong slot or a wrong level read shows up as pixels the
	 * two halves disagree on. Nothing overlaps, so each pixel is one block's.
	 */
	FStageRef Stage = BeginStage(*this, FIntPoint(384, 256));
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Spot = MakeSoftSpot(*Stage);
	TArray<UDreamWidget*> Contents;
	auto Masked = [&Contents, &Stage](const TCHAR* InName, FVector2D InSize, FVector2D InPosition, FDreamClipMask InMask)
	{
		InMask.HitTest = EDreamClipMaskHitTest::Mask;
		Contents.Add(AddMaskedClip(*Stage, InName, InSize, InPosition, InMask, nullptr, InSize + FVector2D(16.0, 16.0)));
	};
	Masked(TEXT("Ellipse"), FVector2D(100.0, 70.0), FVector2D(-70.0, 85.0), Shaped(EDreamClipMaskShape::Ellipse));
	{
		FDreamClipMask Star = Shaped(EDreamClipMaskShape::Star);
		Star.Sides = 5;
		Star.InnerRatio = 0.4f;
		Masked(TEXT("Star"), FVector2D(84.0, 84.0), FVector2D(60.0, 80.0), Star);
	}
	{
		FDreamClipMask Feathered;
		Feathered.Feather = FMargin(14.0f);
		Masked(TEXT("Feathered"), FVector2D(100.0, 60.0), FVector2D(-70.0, 0.0), Feathered);
	}
	{
		FDreamClipMask Hole = Shaped(EDreamClipMaskShape::Ellipse);
		Hole.bInvert = true;
		Masked(TEXT("Hole"), FVector2D(70.0, 50.0), FVector2D(60.0, -5.0), Hole);
	}
	{
		FDreamClipMask Checker = Procedural(EDreamClipMaskPattern::Checker);
		Checker.Frequency = 3.0f;
		Checker.Softness = 0.0f;
		Checker.Rotation = 20.0f;
		Masked(TEXT("Checker"), FVector2D(100.0, 60.0), FVector2D(-70.0, -80.0), Checker);
	}
	{
		FDreamClipMask Textured;
		Textured.Source = EDreamClipMaskSource::Texture;
		Textured.Texture = Spot;
		Textured.Threshold = 0.5f;
		Textured.Softness = 0.1f;
		Masked(TEXT("Textured"), FVector2D(70.0, 70.0), FVector2D(60.0, -85.0), Textured);
	}
	// Clipped to a rounded rect block below: the block itself is dark, so only the white clipped over it reads as shown.
	Stage->AddRectBlock(TEXT("Card"), FVector2D(60.0, 60.0), FVector2D(150.0, 0.0), FColor(28, 30, 38, 255), 20.0f);
	UDreamWidget* OnCard = Stage->AddBlock(TEXT("OnCard"), FVector2D(76.0, 76.0), FVector2D(150.0, 0.0), FColor::White);
	OnCard->SetClipToBelow(true);
	Contents.Add(OnCard);

	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, 5000);
	EnqueueFramesUntilStable(Stage);
	EnqueueDo([this, Stage, Contents]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size = FIntPoint::ZeroValue;
		if (!TestTrue(TEXT("The picture reads back"), Stage->ReadBack(Pixels, Size)))
		{
			return;
		}
		FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("ClipMask_PixelAgainstHit"));
		const UDreamWidget* Root = Stage->GetRoot();
		const float Left = Root->GetLocalSpaceLeft();
		const float Top = Root->GetLocalSpaceTop();
		const float UnitsPerPixelX = Root->GetWidth() / Size.X;
		const float UnitsPerPixelY = Root->GetHeight() / Size.Y;
		const FColor Ground = Stage->GetBackdrop();
		int32 Shown = 0, Hidden = 0, Disagree = 0;
		FString FirstDisagreement;
		for (int32 Y = 0; Y < Size.Y; Y += 2)
		{
			for (int32 X = 0; X < Size.X; X += 2)
			{
				const FVector Local(0.0, Left + (X + 0.5f) * UnitsPerPixelX, Top - (Y + 0.5f) * UnitsPerPixelY);
				const FVector World = Root->GetWorldTransform().TransformPosition(Local);
				// The content block the point is on, if any: a point off every block shows nothing and is nobody's hit.
				const UDreamWidget* Owner = nullptr;
				for (const UDreamWidget* Content : Contents)
				{
					const FVector InContent = Content->GetWorldTransform().InverseTransformPosition(World);
					if (InContent.Y > Content->GetLocalSpaceLeft() + 1.0 && InContent.Y < Content->GetLocalSpaceRight() - 1.0
						&& InContent.Z > Content->GetLocalSpaceBottom() + 1.0 && InContent.Z < Content->GetLocalSpaceTop() - 1.0)
					{
						Owner = Content;
					}
				}
				if (Owner == nullptr)
				{
					continue;
				}
				const FColor& Pixel = Pixels[Y * Size.X + X];
				const float Coverage = (static_cast<float>(Pixel.G) - Ground.G) / (255.0f - Ground.G);
				if (Coverage > 0.3f && Coverage < 0.7f)
				{
					continue;
				}
				const bool bShown = Coverage >= 0.7f;
				const bool bHit = Owner->IsPointVisibleOnClip(World);
				(bShown ? Shown : Hidden)++;
				if (bShown != bHit)
				{
					++Disagree;
					if (FirstDisagreement.IsEmpty())
					{
						FirstDisagreement = FString::Printf(TEXT("pixel (%d, %d) of %s is %s but %s"), X, Y, *Owner->GetDisplayName(),
							bShown ? TEXT("shown") : TEXT("not shown"), bHit ? TEXT("hits") : TEXT("misses"));
					}
				}
			}
		}
		TestTrue(FString::Printf(TEXT("Enough clearly shown pixels were asked (%d)"), Shown), Shown > 1000);
		TestTrue(FString::Printf(TEXT("Enough clearly hidden pixels were asked (%d)"), Hidden), Hidden > 300);
		TestEqual(FString::Printf(TEXT("Every clearly shown pixel hits and every clearly hidden one misses%s%s"),
			FirstDisagreement.IsEmpty() ? TEXT("") : TEXT(": "), *FirstDisagreement), Disagree, 0);
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif
