// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamVisualBatchMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Extensions/2DLineRenderer/Dream2DLineRaw.h"
#include "Extensions/DreamPolygon.h"
#include "Extensions/DreamRing.h"
#include "Extensions/DreamShape.h"
#include "DreamGalleryStage.h"

/*
 * The edge strip as the picture shows it. A white triangle whose long side runs at 45 degrees, on the gallery's
 * backdrop, without multisampling: with the strip, the pixels that side crosses are drawn partly covered, between the
 * backdrop and white; with it switched off on the element, every pixel is one or the other, which is what an
 * unsmoothed edge is. Read on the gallery's render-target canvas, where a pixel has one size.
 */

namespace DreamEdgeAntiAliasingPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** Pixels strictly between the backdrop and white on every channel: only coverage short of full draws those. */
	int32 CountPartlyCovered(const TArray<FColor>& InPixels)
	{
		constexpr int32 Margin = 12;
		const auto Between = [](uint8 InValue, uint8 InLow)
		{
			return InValue > InLow + Margin && InValue < 255 - Margin;
		};
		int32 Count = 0;
		for (const FColor& Pixel : InPixels)
		{
			Count += (Between(Pixel.R, Backdrop.R) && Between(Pixel.G, Backdrop.G) && Between(Pixel.B, Backdrop.B)) ? 1 : 0;
		}
		return Count;
	}

	void EnqueueCount(const FStageRef& InStage, TSharedRef<int32> OutCount)
	{
		EnqueueDo([InStage, OutCount]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (InStage->ReadBack(Pixels, Size))
			{
				*OutCount = CountPartlyCovered(Pixels);
			}
			else
			{
				InStage->GetTest().AddError(TEXT("The picture was not read back"));
			}
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingPixelTest,
	"DreamGUI.RHI.EdgeAntiAliasing.AShapesSlantedSideIsDrawnPartlyCoveredAndNotOnceSwitchedOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamEdgeAntiAliasingPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	Stage->UseMultisampling(1);
	UDreamWidget* Widget = Stage->AddWidget(TEXT("Slash"), FVector2D(160.0, 160.0), FVector2D::ZeroVector);
	UDreamShape* Shape = Widget != nullptr ? Widget->CreateNewVisual<UDreamShape>() : nullptr;
	if (!TestNotNull(TEXT("The shape was made"), Shape))
	{
		Stage->TearDown();
		return false;
	}
	// Normalized, (0, 0) at the bottom left: two sides along the rect's edges and the third from corner to corner.
	Shape->SetPoints({ FVector2D(0.0, 0.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0) });
	Shape->SetColor(FColor::White);

	const TSharedRef<int32> WithStrip = MakeShared<int32>(-1);
	const TSharedRef<int32> WithoutStrip = MakeShared<int32>(-1);
	EnqueueFramesUntilDrawn(Stage, 5000);
	EnqueueFramesUntilSettled(Stage);
	EnqueueCount(Stage, WithStrip);
	const TWeakObjectPtr<UDreamShape> WeakShape = Shape;
	EnqueueDo([WeakShape, Stage]()
	{
		if (UDreamShape* Live = WeakShape.Get())
		{
			Live->SetEdgeAntiAliasing(EDreamEdgeAntiAliasing::Off);
		}
		Stage->RequestRedraw();
	});
	EnqueueFrames(Stage, 4);
	EnqueueFramesUntilSettled(Stage);
	EnqueueCount(Stage, WithoutStrip);
	EnqueueDo([this, WithStrip, WithoutStrip]()
	{
		// The long side is some 226 pixels of diagonal; every row it crosses has at least one pixel it cuts.
		TestTrue(*FString::Printf(TEXT("With the strip, the slanted side is drawn partly covered (%d pixels)"), *WithStrip), *WithStrip >= 120);
		TestEqual(TEXT("Switched off on the element, no pixel is drawn partly covered"), *WithoutStrip, 0);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingGalleryTest,
	"DreamGUI.RHI.Gallery.MeshOutlinesWithAndWithoutEdgeAntiAliasingMatchTheirGoldenImages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * The mesh visuals the strip is for, side by side, without multisampling: a hexagon turned off the grid, a Shape's
 * arrow, a ring and a zigzag line. Drawn with the strip switched off on every element and then on, each held to its
 * golden image -- the pair is what shows the difference, and what a change to either drawing has to answer to.
 */
bool FDreamEdgeAntiAliasingGalleryTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	Stage->UseMultisampling(1);

	TArray<TWeakObjectPtr<UDreamVisualBatchMesh>> Elements;
	const auto Keep = [&Elements](UDreamVisualBatchMesh* InVisual, FColor InColour)
	{
		if (InVisual != nullptr)
		{
			InVisual->SetColor(InColour);
			InVisual->SetEdgeAntiAliasing(EDreamEdgeAntiAliasing::Off);
			Elements.Add(InVisual);
		}
	};
	if (UDreamPolygon* Hexagon = Stage->AddWidget(TEXT("Hexagon"), FVector2D(96.0, 96.0), FVector2D(-62.0, 60.0))->CreateNewVisual<UDreamPolygon>())
	{
		Hexagon->SetSides(6);
		Hexagon->SetStartAngle(17.0f);
		Keep(Hexagon, FColor(232, 184, 72, 255));
	}
	if (UDreamShape* Arrow = Stage->AddWidget(TEXT("Arrow"), FVector2D(100.0, 90.0), FVector2D(62.0, 60.0))->CreateNewVisual<UDreamShape>())
	{
		Arrow->SetPoints({ FVector2D(0.0, 0.35), FVector2D(0.55, 0.35), FVector2D(0.55, 0.05), FVector2D(1.0, 0.5),
			FVector2D(0.55, 0.95), FVector2D(0.55, 0.65), FVector2D(0.0, 0.65) });
		Keep(Arrow, FColor(96, 200, 255, 255));
	}
	if (UDreamRing* Ring = Stage->AddWidget(TEXT("Ring"), FVector2D(96.0, 96.0), FVector2D(-62.0, -60.0))->CreateNewVisual<UDreamRing>())
	{
		Ring->SetLineWidth(9.0f);
		Ring->SetSegment(40);
		Keep(Ring, FColor(240, 96, 128, 255));
	}
	if (UDream2DLineRaw* Zigzag = Stage->AddWidget(TEXT("Zigzag"), FVector2D(110.0, 90.0), FVector2D(62.0, -60.0))->CreateNewVisual<UDream2DLineRaw>())
	{
		Zigzag->SetLineWidth(6.0f);
		Zigzag->SetPoints({ FVector2D(-48.0, -34.0), FVector2D(-18.0, 34.0), FVector2D(14.0, -30.0), FVector2D(48.0, 34.0) });
		Keep(Zigzag, FColor(120, 230, 150, 255));
	}
	TestEqual(TEXT("All four elements were made"), Elements.Num(), 4);

	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_EdgeAntiAliasing_Off"), 6000, EMissingGolden::Warn);
	EnqueueDo([Elements, Stage]()
	{
		for (const TWeakObjectPtr<UDreamVisualBatchMesh>& Element : Elements)
		{
			if (UDreamVisualBatchMesh* Live = Element.Get())
			{
				Live->SetEdgeAntiAliasing(EDreamEdgeAntiAliasing::On);
			}
		}
		Stage->RequestRedraw();
	});
	EnqueueFrames(Stage, 4);
	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_EdgeAntiAliasing_On"), 6000, EMissingGolden::Warn);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingTurnedRectPixelTest,
	"DreamGUI.RHI.EdgeAntiAliasing.ATurnedTexturesSidesAreDrawnPartlyCoveredAndAnUnturnedOnesAreNot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * A white Texture on the backdrop, without multisampling. Unturned, its sides lie along the pixel rows and columns: no
 * strip, and every pixel is the backdrop or white. Turned 25 degrees, the pixels its sides cross are drawn partly covered;
 * turned and switched off on the element, every pixel is one or the other again, which is the stepped edge.
 */
bool FDreamEdgeAntiAliasingTurnedRectPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	Stage->UseMultisampling(1);
	UDreamWidget* Card = Stage->AddBlock(TEXT("Card"), FVector2D(140.0, 90.0), FVector2D::ZeroVector, FColor::White);
	UDreamVisualBatchMesh* Visual = Card != nullptr ? Cast<UDreamVisualBatchMesh>(Card->GetVisual()) : nullptr;
	if (!TestNotNull(TEXT("The texture was made"), Visual))
	{
		Stage->TearDown();
		return false;
	}
	const TSharedRef<int32> Unturned = MakeShared<int32>(-1);
	const TSharedRef<int32> Turned = MakeShared<int32>(-1);
	const TSharedRef<int32> TurnedOff = MakeShared<int32>(-1);
	EnqueueFramesUntilDrawn(Stage, 5000);
	EnqueueFramesUntilSettled(Stage);
	EnqueueCount(Stage, Unturned);
	const TWeakObjectPtr<UDreamWidget> WeakCard = Card;
	const TWeakObjectPtr<UDreamVisualBatchMesh> WeakVisual = Visual;
	EnqueueDo([WeakCard, Stage]()
	{
		if (UDreamWidget* Live = WeakCard.Get())
		{
			Live->SetRelativeRotationEuler(FRotator(0.0, 0.0, 25.0));
		}
		Stage->RequestRedraw();
	});
	EnqueueFrames(Stage, 4);
	EnqueueFramesUntilSettled(Stage);
	EnqueueCount(Stage, Turned);
	EnqueueDo([WeakVisual, Stage]()
	{
		if (UDreamVisualBatchMesh* Live = WeakVisual.Get())
		{
			Live->SetEdgeAntiAliasing(EDreamEdgeAntiAliasing::Off);
		}
		Stage->RequestRedraw();
	});
	EnqueueFrames(Stage, 4);
	EnqueueFramesUntilSettled(Stage);
	EnqueueCount(Stage, TurnedOff);
	EnqueueDo([this, Unturned, Turned, TurnedOff]()
	{
		TestEqual(TEXT("Unturned, no pixel is drawn partly covered"), *Unturned, 0);
		// Four sides of 140 and 90 pixels, every row and column they cross cut once at least.
		TestTrue(*FString::Printf(TEXT("Turned, its sides are drawn partly covered (%d pixels)"), *Turned), *Turned >= 300);
		TestEqual(TEXT("Turned and switched off, no pixel is drawn partly covered"), *TurnedOff, 0);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEdgeAntiAliasingTurnedImagesGalleryTest,
	"DreamGUI.RHI.Gallery.TurnedImagesWithAndWithoutEdgeAntiAliasingMatchTheirGoldenImages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

/*
 * The rects the strip is for once they are turned, side by side, without multisampling: a checkered Texture turned in the
 * canvas plane, a framed nine-slice Image turned the other way, an Image tiled with stripes, and a Texture tilted in a
 * perspective.
 * Drawn with the strip switched off on every element and then on, each held to its golden image. With it, the sides are
 * smooth and the texture stays where it was: the checker's squares and the frame's border keep their size.
 */
bool FDreamEdgeAntiAliasingTurnedImagesGalleryTest::RunTest(const FString& Parameters)
{
	using namespace DreamEdgeAntiAliasingPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	Stage->UseMultisampling(1);
	UTexture2D* Checker = Stage->MakeTexture(16, [](int32 X, int32 Y)
	{
		return ((X / 4 + Y / 4) % 2 == 0) ? FColor(236, 236, 236, 255) : FColor(64, 132, 220, 255);
	});
	UTexture2D* Framed = Stage->MakeTexture(16, [](int32 X, int32 Y)
	{
		const bool bBorder = X < 4 || Y < 4 || X >= 12 || Y >= 12;
		return bBorder ? FColor(232, 184, 72, 255) : FColor(60, 66, 90, 255);
	});
	// Upright stripes for the tiles: the picture is held to what each tile shows across, which is what tiling repeats.
	UTexture2D* Stripes = Stage->MakeTexture(16, [](int32 X, int32 Y)
	{
		return (X / 4) % 2 == 0 ? FColor(236, 236, 236, 255) : FColor(120, 200, 140, 255);
	});

	TArray<TWeakObjectPtr<UDreamVisualBatchMesh>> Elements;
	const auto Keep = [&Elements](UDreamVisualBatchMesh* InVisual)
	{
		if (InVisual != nullptr)
		{
			InVisual->SetEdgeAntiAliasing(EDreamEdgeAntiAliasing::Off);
			Elements.Add(InVisual);
		}
	};
	const auto MakeImage = [&Stage](const TCHAR* InName, FVector2D InSize, FVector2D InPosition, UTexture* InTexture,
		EDreamUIImageBrushDrawType InDrawAs, FVector2f InImageSize) -> UDreamImage*
	{
		UDreamWidget* Widget = Stage->AddWidget(InName, InSize, InPosition);
		UDreamImage* Image = Widget != nullptr ? Widget->CreateNewVisual<UDreamImage>() : nullptr;
		if (Image != nullptr)
		{
			FDreamUIImageBrush Brush = Image->GetBrush();
			Brush.SetResourceObject(InTexture);
			Brush.DrawAs = InDrawAs;
			Brush.ImageSize = InImageSize;
			Brush.Margin = FMargin(0.25f);
			Image->SetBrush(Brush);
		}
		return Image;
	};

	if (UDreamWidget* Turned = Stage->AddBlock(TEXT("Turned"), FVector2D(96.0, 64.0), FVector2D(-62.0, 60.0), FColor::White, Checker))
	{
		Turned->SetRelativeRotationEuler(FRotator(0.0, 0.0, 25.0));
		Keep(Cast<UDreamVisualBatchMesh>(Turned->GetVisual()));
	}
	if (UDreamImage* Sliced = MakeImage(TEXT("Sliced"), FVector2D(100.0, 70.0), FVector2D(62.0, 60.0), Framed, EDreamUIImageBrushDrawType::Box, FVector2f(32.0f, 32.0f)))
	{
		Sliced->GetWidget()->SetRelativeRotationEuler(FRotator(0.0, 0.0, -15.0));
		Keep(Sliced);
	}
	if (UDreamImage* Tiled = MakeImage(TEXT("Tiled"), FVector2D(100.0, 64.0), FVector2D(-62.0, -60.0), Stripes, EDreamUIImageBrushDrawType::Tiled, FVector2f(24.0f, 24.0f)))
	{
		Tiled->GetWidget()->SetRelativeRotationEuler(FRotator(0.0, 0.0, 12.0));
		Keep(Tiled);
	}
	if (UDreamWidget* Scope = Stage->AddWidget(TEXT("Scope"), FVector2D(110.0, 90.0), FVector2D(62.0, -60.0)))
	{
		Scope->SetPerspective(true);
		Scope->SetPerspectiveFieldOfView(50.0f);
		if (UDreamWidget* Tilted = Stage->AddBlock(TEXT("Tilted"), FVector2D(84.0, 60.0), FVector2D::ZeroVector, FColor::White, Checker, Scope))
		{
			Tilted->SetRenderRotation(FRotator(20.0, 35.0, 0.0));
			Keep(Cast<UDreamVisualBatchMesh>(Tilted->GetVisual()));
		}
	}
	TestEqual(TEXT("All four elements were made"), Elements.Num(), 4);

	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_TurnedImages_Off"), 6000, EMissingGolden::Warn);
	EnqueueDo([Elements, Stage]()
	{
		for (const TWeakObjectPtr<UDreamVisualBatchMesh>& Element : Elements)
		{
			if (UDreamVisualBatchMesh* Live = Element.Get())
			{
				Live->SetEdgeAntiAliasing(EDreamEdgeAntiAliasing::On);
			}
		}
		Stage->RequestRedraw();
	});
	EnqueueFrames(Stage, 4);
	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_TurnedImages_On"), 6000, EMissingGolden::Warn);
	EnqueueTearDown(Stage);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
