// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
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

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
