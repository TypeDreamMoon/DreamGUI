// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "RenderTargetPool.h"
#include "RenderingThread.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBlendMath.h"
#include "Core/DreamUIClipMask.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Extensions/DreamCanvasRenderTargetPreviewer.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Core/DreamUIDrawCall.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * The blend modes and the groups, drawn: every mode over a chart of colours, each pixel held to the CPU's formula and the
 * picture to its golden image; groups isolated and passed through, faded as one, nested, masked, clipped to, and drawn
 * from a texture kept from frame to frame; and the group textures handed back to the pool.
 */
namespace DreamBlendGroupPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** The colours behind, one to a vertical stripe, and the elements', one to a horizontal stripe -- two of them half transparent. */
	const FColor BehindColours[4] = { FColor(230, 60, 40, 255), FColor(40, 180, 90, 255), FColor(60, 90, 220, 255), FColor(200, 200, 200, 255) };
	const FColor ElementColours[4] = { FColor(250, 210, 60, 255), FColor(90, 40, 160, 255), FColor(240, 120, 200, 128), FColor(20, 200, 220, 128) };
	constexpr int32 Columns = 6;
	constexpr int32 Rows = 5;
	constexpr int32 Cell = 48;
	constexpr int32 Swatch = 40;
	const FIntPoint ChartSize(Columns * Cell, Rows * Cell);

	/** InColours in four stripes across (vertical stripes) or down (horizontal ones). */
	UTexture2D* MakeStripes(FGalleryStage& InStage, const FColor (&InColours)[4], bool bInAcross)
	{
		return InStage.MakeTexture(64, [&InColours, bInAcross](int32 X, int32 Y)
		{
			return InColours[(bInAcross ? X : Y) / 16];
		});
	}

	/** The centre of a mode's cell, on the canvas (+Y up, the target's centre at 0). */
	FVector2D CellCentre(int32 InMode)
	{
		const int32 Column = InMode % Columns;
		const int32 Row = InMode / Columns;
		return FVector2D(-ChartSize.X * 0.5 + Cell * 0.5 + Cell * Column, ChartSize.Y * 0.5 - Cell * 0.5 - Cell * Row);
	}

	/** The pixel of the target at the middle of what behind stripe InBehind and element stripe InElement make in a mode's cell. */
	FIntPoint ComboPixel(int32 InMode, int32 InBehind, int32 InElement)
	{
		const FVector2D Centre = CellCentre(InMode);
		const double X = Centre.X - Swatch * 0.5 + Swatch / 8.0 + Swatch / 4.0 * InBehind;
		const double Y = Centre.Y + Swatch * 0.5 - Swatch / 8.0 - Swatch / 4.0 * InElement;
		return FIntPoint(FMath::FloorToInt(X + ChartSize.X * 0.5), FMath::FloorToInt(ChartSize.Y * 0.5 - Y));
	}

	FVector3f Linear(const FColor& InColour)
	{
		return FDreamUIBlendMath::ToLinear(FVector3f(InColour.R / 255.0f, InColour.G / 255.0f, InColour.B / 255.0f));
	}

	/**
	 * What the target holds where InElement of InMode is drawn over the opaque InBehind, on a linear target: the hardware's
	 * modes blend linear light as the target's format does, the rest go through the encoded blend.
	 */
	FColor Expected(EDreamUIBlendMode InMode, const FColor& InBehind, const FColor& InElement)
	{
		const FVector3f Cb = Linear(InBehind);
		const FVector3f Cs = Linear(InElement);
		const float Sa = InElement.A / 255.0f;
		FVector4f Out;
		if (DreamUIBlend::ReadsBackdrop(InMode))
		{
			const FVector4f Over = FDreamUIBlendMath::OverBackdrop(InMode, Cs, Sa, FVector4f(Cb.X, Cb.Y, Cb.Z, 1.0f), true, false);
			Out = FVector4f(Over.X + Cb.X * (1.0f - Sa), Over.Y + Cb.Y * (1.0f - Sa), Over.Z + Cb.Z * (1.0f - Sa), 1.0f);
		}
		else
		{
			Out = FDreamUIBlendMath::ApplyFixedBlendState(InMode, FDreamUIBlendMath::FixedOutput(InMode, Cs * Sa, Sa), FVector4f(Cb.X, Cb.Y, Cb.Z, 1.0f));
		}
		const FVector3f Encoded = FDreamUIBlendMath::ToSrgb(FVector3f(Out.X, Out.Y, Out.Z));
		return FColor(FMath::RoundToInt(Encoded.X * 255.0f), FMath::RoundToInt(Encoded.Y * 255.0f), FMath::RoundToInt(Encoded.Z * 255.0f), 255);
	}

	/** Every mode in its cell: the colours behind as a block, then the element's over them in the mode. */
	void BuildChart(FGalleryStage& InStage)
	{
		UTexture2D* Behind = MakeStripes(InStage, BehindColours, true);
		UTexture2D* Elements = MakeStripes(InStage, ElementColours, false);
		for (int32 Mode = 0; Mode < DreamUIBlend::NumModes; ++Mode)
		{
			InStage.AddBlock(*FString::Printf(TEXT("Behind%d"), Mode), FVector2D(Swatch, Swatch), CellCentre(Mode), FColor::White, Behind);
		}
		for (int32 Mode = 0; Mode < DreamUIBlend::NumModes; ++Mode)
		{
			UDreamWidget* Element = InStage.AddBlock(*FString::Printf(TEXT("Mode%d"), Mode), FVector2D(Swatch, Swatch), CellCentre(Mode), FColor::White, Elements);
			if (UDreamTexture* Visual = Cast<UDreamTexture>(Element->GetVisual()))
			{
				Visual->SetBlendMode(static_cast<EDreamUIBlendMode>(Mode));
			}
		}
	}

	/** Every combination of every mode held to Expected; dissolve only where its element is opaque, which it always draws. */
	void CheckChart(FAutomationTestBase& InTest, const TArray<FColor>& InPixels, FIntPoint InSize, uint8 InTolerance)
	{
		int32 Wrong = 0;
		int32 Checked = 0;
		for (int32 Mode = 0; Mode < DreamUIBlend::NumModes; ++Mode)
		{
			const EDreamUIBlendMode BlendMode = static_cast<EDreamUIBlendMode>(Mode);
			for (int32 Back = 0; Back < 4; ++Back)
			{
				for (int32 Element = 0; Element < 4; ++Element)
				{
					if (BlendMode == EDreamUIBlendMode::Dissolve && ElementColours[Element].A < 255)
					{
						continue;
					}
					const FIntPoint Pixel = ComboPixel(Mode, Back, Element);
					if (Pixel.X < 0 || Pixel.Y < 0 || Pixel.X >= InSize.X || Pixel.Y >= InSize.Y)
					{
						continue;
					}
					const FColor Got = InPixels[Pixel.Y * InSize.X + Pixel.X];
					const FColor Want = Expected(BlendMode, BehindColours[Back], ElementColours[Element]);
					++Checked;
					if (!FDreamPixelProbe::IsNear(Got, Want, InTolerance))
					{
						if (++Wrong <= 24)
						{
							InTest.AddError(FString::Printf(TEXT("%s, %s over %s, at (%d, %d): %s, the formula says %s"),
								*StaticEnum<EDreamUIBlendMode>()->GetNameStringByValue(Mode), *FDreamPixelProbe::Describe(ElementColours[Element]),
								*FDreamPixelProbe::Describe(BehindColours[Back]), Pixel.X, Pixel.Y, *FDreamPixelProbe::Describe(Got), *FDreamPixelProbe::Describe(Want)));
						}
					}
				}
			}
		}
		InTest.TestEqual(TEXT("Every combination of every mode is what the formula says"), Wrong, 0);
		InTest.TestTrue(TEXT("...and they were all read"), Checked == 27 * 16 - 2 * 4);
	}

	FColor PixelAt(const TArray<FColor>& InPixels, FIntPoint InSize, FVector2D InCanvas)
	{
		const int32 X = FMath::Clamp(FMath::FloorToInt(InCanvas.X + InSize.X * 0.5), 0, InSize.X - 1);
		const int32 Y = FMath::Clamp(FMath::FloorToInt(InSize.Y * 0.5 - InCanvas.Y), 0, InSize.Y - 1);
		return InPixels[Y * InSize.X + X];
	}

	/** Reads the picture back once it has settled, then hands it to InCheck. */
	void EnqueueReadBack(const FStageRef& InStage, TFunction<void(const TArray<FColor>&, FIntPoint)> InCheck)
	{
		EnqueueDo([InStage, InCheck]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (InStage->ReadBack(Pixels, Size))
			{
				InCheck(Pixels, Size);
			}
			else
			{
				InStage->GetTest().AddError(TEXT("The picture was not read back"));
			}
		});
	}

	UDreamLayerGroup* AddGroup(UDreamWidget* InWidget, EDreamLayerGroupIsolation InIsolation, float InOpacity = 1.0f, EDreamUIBlendMode InMode = EDreamUIBlendMode::Alpha)
	{
		UDreamLayerGroup* Group = InWidget->AddComponent<UDreamLayerGroup>();
		Group->SetIsolation(InIsolation);
		Group->SetGroupOpacity(InOpacity);
		Group->SetGroupBlendMode(InMode);
		return Group;
	}

	void SetMode(UDreamWidget* InWidget, EDreamUIBlendMode InMode)
	{
		if (UDreamVisualBatchMesh* Visual = Cast<UDreamVisualBatchMesh>(InWidget->GetVisual()))
		{
			Visual->SetBlendMode(InMode);
		}
	}

	/** The isolation scene: a backdrop of stripes, a blue square and a white square in Difference half over it, in a container. */
	UDreamWidget* BuildDifferencePair(FGalleryStage& InStage, const TCHAR* InName, FVector2D InPosition)
	{
		UDreamWidget* Container = InStage.AddWidget(InName, FVector2D(110.0, 110.0), InPosition);
		InStage.AddBlock(*FString::Printf(TEXT("%sBlue"), InName), FVector2D(60.0, 60.0), FVector2D(-15.0, 15.0), FColor(40, 80, 200, 255), nullptr, Container);
		UDreamWidget* White = InStage.AddBlock(*FString::Printf(TEXT("%sWhite"), InName), FVector2D(60.0, 60.0), FVector2D(15.0, -15.0), FColor::White, nullptr, Container);
		SetMode(White, EDreamUIBlendMode::Difference);
		return Container;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendChartPixelTest,
	"DreamGUI.RHI.Blend.EveryModeDrawsWhatItsFormulaSaysAndMatchesItsGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamBlendChartPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupPixelTestLocal;
	FStageRef Stage = BeginStage(*this, ChartSize);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->UseBuiltInShader(true);
	BuildChart(*Stage);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Blend_Modes"), 30000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		CheckChart(*this, InPixels, InSize, 3);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBlendMaterialPathPixelTest,
	"DreamGUI.RHI.Blend.AnElementAMaterialDrawsBlendsThroughAGroupAsTheBuiltInShaderBlends",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamBlendMaterialPathPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupPixelTestLocal;
	FStageRef Stage = BeginStage(*this, ChartSize);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	// The whole chart through the default material: every element of a mode after Multiply is a group of one, the three
	// before it the material's own, which is Normal -- those cells are left out of the comparison.
	Stage->UseBuiltInShader(false);
	BuildChart(*Stage);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Blend_MaterialPath"), 30000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		int32 Wrong = 0;
		for (int32 Mode = static_cast<int32>(EDreamUIBlendMode::Dissolve) + 1; Mode < DreamUIBlend::NumModes; ++Mode)
		{
			for (int32 Back = 0; Back < 4; ++Back)
			{
				for (int32 Element = 0; Element < 4; ++Element)
				{
					const FIntPoint Pixel = ComboPixel(Mode, Back, Element);
					const FColor Got = InPixels[Pixel.Y * InSize.X + Pixel.X];
					const FColor Want = Expected(static_cast<EDreamUIBlendMode>(Mode), BehindColours[Back], ElementColours[Element]);
					if (!FDreamPixelProbe::IsNear(Got, Want, 4) && ++Wrong <= 16)
					{
						AddError(FString::Printf(TEXT("%s through a material at (%d, %d): %s, the formula says %s"), *StaticEnum<EDreamUIBlendMode>()->GetNameStringByValue(Mode),
							Pixel.X, Pixel.Y, *FDreamPixelProbe::Describe(Got), *FDreamPixelProbe::Describe(Want)));
					}
				}
			}
		}
		TestEqual(TEXT("An element a material draws blends as the formula says"), Wrong, 0);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupIsolationPixelTest,
	"DreamGUI.RHI.Group.IsolatedAndPassedThroughMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGroupIsolationPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Stripes = MakeStripes(*Stage, BehindColours, true);
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Stripes);
	// Passed through, the white square in Difference inverts the stripes as well as the blue; isolated, it inverts the blue
	// alone and is white where the group had drawn nothing yet.
	BuildDifferencePair(*Stage, TEXT("Through"), FVector2D(-64.0, 60.0));
	AddGroup(BuildDifferencePair(*Stage, TEXT("Isolated"), FVector2D(64.0, 60.0)), EDreamLayerGroupIsolation::Isolated);
	// A group in Multiply: its picture -- the pair as drawn apart -- darkens the stripes as one.
	AddGroup(BuildDifferencePair(*Stage, TEXT("Multiplied"), FVector2D(0.0, -66.0)), EDreamLayerGroupIsolation::Isolated, 1.0f, EDreamUIBlendMode::Multiply);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Group_Isolation"), 30000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		// The white square's corner away from the blue one, over the stripes.
		const FVector2D Corner(15.0 + 20.0, -15.0 - 20.0);
		const FColor Through = PixelAt(InPixels, InSize, FVector2D(-64.0, 60.0) + Corner);
		const FColor Isolated = PixelAt(InPixels, InSize, FVector2D(64.0, 60.0) + Corner);
		TestTrue(FString::Printf(TEXT("Isolated, Difference over nothing yet is white (%s)"), *FDreamPixelProbe::Describe(Isolated)), FDreamPixelProbe::IsNear(Isolated, FColor::White, 3));
		TestFalse(FString::Printf(TEXT("Passed through, it is the stripes inverted, not white (%s)"), *FDreamPixelProbe::Describe(Through)), FDreamPixelProbe::IsNear(Through, FColor::White, 40));
		// Over the blue, both invert it the same.
		const FColor ThroughOverBlue = PixelAt(InPixels, InSize, FVector2D(-64.0, 60.0));
		const FColor IsolatedOverBlue = PixelAt(InPixels, InSize, FVector2D(64.0, 60.0));
		TestTrue(TEXT("Over the blue, isolated or not, the white inverts the blue alike"), FDreamPixelProbe::IsNear(ThroughOverBlue, IsolatedOverBlue, 3));
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupOpacityPixelTest,
	"DreamGUI.RHI.Group.AGroupFadesAsOneAndItsChildrenDoNotShowThroughEachOther",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGroupOpacityPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Stripes = MakeStripes(*Stage, BehindColours, true);
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Stripes);
	auto Pair = [&Stage](const TCHAR* InName, FVector2D InPosition)
	{
		UDreamWidget* Container = Stage->AddWidget(InName, FVector2D(110.0, 110.0), InPosition);
		Stage->AddBlock(*FString::Printf(TEXT("%sRed"), InName), FVector2D(70.0, 70.0), FVector2D(-15.0, 15.0), FColor(220, 40, 40, 255), nullptr, Container);
		Stage->AddBlock(*FString::Printf(TEXT("%sBlue"), InName), FVector2D(70.0, 70.0), FVector2D(15.0, -15.0), FColor(40, 80, 220, 255), nullptr, Container);
		return Container;
	};
	// The same pair twice at half opacity: as a group, and each child at half by its widget's opacity.
	AddGroup(Pair(TEXT("Group"), FVector2D(-64.0, 0.0)), EDreamLayerGroupIsolation::PassThrough, 0.5f);
	UDreamWidget* Each = Pair(TEXT("Each"), FVector2D(64.0, 0.0));
	Each->SetRenderOpacity(0.5f);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Group_Opacity"), 30000);
	EnqueueReadBack(Stage, [this](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		// The blue over the red, and the blue over the stripes alone, in the same stripe column.
		const FColor GroupOverlap = PixelAt(InPixels, InSize, FVector2D(-64.0, 0.0));
		const FColor GroupBlueAlone = PixelAt(InPixels, InSize, FVector2D(-64.0 + 15.0, -15.0 - 25.0));
		const FColor EachOverlap = PixelAt(InPixels, InSize, FVector2D(64.0, 0.0));
		const FColor EachBlueAlone = PixelAt(InPixels, InSize, FVector2D(64.0 + 15.0, -15.0 - 25.0));
		TestTrue(FString::Printf(TEXT("In the group the red does not show through the blue: %s where they overlap, %s where the blue is alone"),
			*FDreamPixelProbe::Describe(GroupOverlap), *FDreamPixelProbe::Describe(GroupBlueAlone)), FDreamPixelProbe::IsNear(GroupOverlap, GroupBlueAlone, 3));
		TestFalse(FString::Printf(TEXT("Faded one by one, it does: %s and %s"), *FDreamPixelProbe::Describe(EachOverlap), *FDreamPixelProbe::Describe(EachBlueAlone)),
			FDreamPixelProbe::IsNear(EachOverlap, EachBlueAlone, 6));
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupNestedPixelTest,
	"DreamGUI.RHI.Group.ThreeNestedGroupsWithModesAndAMaskMatchTheirGoldenImage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGroupNestedPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Stripes = MakeStripes(*Stage, BehindColours, true);
	UTexture2D* Gradient = Stage->MakeGradient(64, FColor(255, 220, 120, 255), FColor(30, 60, 160, 255));
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Stripes);
	UDreamWidget* Outer = Stage->AddWidget(TEXT("Outer"), FVector2D(220.0, 220.0), FVector2D::ZeroVector);
	Stage->AddBlock(TEXT("OuterFill"), FVector2D(200.0, 200.0), FVector2D::ZeroVector, FColor::White, Gradient, Outer);
	UDreamWidget* Middle = Stage->AddWidget(TEXT("Middle"), FVector2D(150.0, 150.0), FVector2D(10.0, -10.0), Outer);
	if (UDreamRectBlock* Card = Stage->AddRectBlock(TEXT("MiddleCard"), FVector2D(140.0, 140.0), FVector2D::ZeroVector, FColor(120, 200, 255, 255), 24.0f))
	{
		Card->GetWidget()->TrySetParent(Middle, false);
		Card->GetWidget()->SetAnchoredPosition(FVector2D::ZeroVector);
	}
	UDreamWidget* Inner = Stage->AddWidget(TEXT("Inner"), FVector2D(80.0, 80.0), FVector2D(0.0, 0.0), Middle);
	Stage->AddBlock(TEXT("InnerA"), FVector2D(60.0, 60.0), FVector2D(-10.0, 10.0), FColor(255, 255, 255, 255), nullptr, Inner);
	UDreamWidget* InnerB = Stage->AddBlock(TEXT("InnerB"), FVector2D(60.0, 60.0), FVector2D(10.0, -10.0), FColor(255, 120, 40, 255), nullptr, Inner);
	SetMode(InnerB, EDreamUIBlendMode::Difference);
	AddGroup(Outer, EDreamLayerGroupIsolation::Isolated, 0.85f, EDreamUIBlendMode::Multiply);
	UDreamLayerGroup* MiddleGroup = AddGroup(Middle, EDreamLayerGroupIsolation::Isolated, 1.0f, EDreamUIBlendMode::Screen);
	FDreamClipMask Ellipse;
	Ellipse.Shape = EDreamClipMaskShape::Ellipse;
	Ellipse.Feather = FMargin(10.0f);
	MiddleGroup->SetGroupMask(Ellipse);
	AddGroup(Inner, EDreamLayerGroupIsolation::Isolated, 0.75f, EDreamUIBlendMode::Overlay);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Group_Nested"), 30000);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamGroupPoolPixelTest,
	"DreamGUI.RHI.Group.GroupTexturesComeFromThePoolAndGoBackFrameAfterFrame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamGroupPoolPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Stripes = MakeStripes(*Stage, BehindColours, true);
	Stage->AddBlock(TEXT("Scene"), FVector2D(256.0, 256.0), FVector2D::ZeroVector, FColor::White, Stripes);
	TArray<UDreamLayerGroup*> Groups;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		UDreamWidget* Pair = BuildDifferencePair(*Stage, *FString::Printf(TEXT("Pair%d"), Index), FVector2D(Index % 2 == 0 ? -64.0 : 64.0, Index < 2 ? 64.0 : -64.0));
		Groups.Add(AddGroup(Pair, EDreamLayerGroupIsolation::Isolated, 0.8f, Index % 2 == 0 ? EDreamUIBlendMode::Alpha : EDreamUIBlendMode::Overlay));
	}
	EnqueueFramesUntilDrawn(Stage, 20000);
	EnqueueFrames(Stage, 10);
	struct FPool
	{
		uint32 Elements = 0;
		uint32 WholeKB = 0;
		int64 Composites = 0;
	};
	TSharedRef<FPool> Before = MakeShared<FPool>();
	EnqueueDo([Before]()
	{
		FlushRenderingCommands();
		uint32 Used = 0;
		GRenderTargetPool.GetStats(Before->Elements, Before->WholeKB, Used);
		DreamUIRenderStats::TakeSnapshot(true);
	});
	// Thirty frames, each redrawing every group, with an opacity that changes every frame.
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		EnqueueDo([Stage, Groups, Frame]()
		{
			for (UDreamLayerGroup* Group : Groups)
			{
				Group->SetGroupOpacity(0.5f + 0.4f * ((Frame % 4) / 3.0f));
			}
			Stage->RequestRedraw();
		});
		EnqueueFrames(Stage, 1);
	}
	EnqueueDo([this, Before]()
	{
		FlushRenderingCommands();
		const DreamUIRenderStats::FSnapshot Snapshot = DreamUIRenderStats::TakeSnapshot(true);
		uint32 Elements = 0;
		uint32 WholeKB = 0;
		uint32 Used = 0;
		GRenderTargetPool.GetStats(Elements, WholeKB, Used);
		const int64 Composites = Snapshot.Counters[static_cast<int32>(DreamUIRenderStats::ECounter::GroupsComposited)];
		TestTrue(FString::Printf(TEXT("The groups were put down every frame drawn (%lld in %llu frames)"), Composites, Snapshot.Frames), Composites >= 4 * 20);
		TestTrue(FString::Printf(TEXT("The pool did not grow: %u elements and %u KB before, %u and %u after"), Before->Elements, Before->WholeKB, Elements, WholeKB),
			Elements <= Before->Elements + 2 && WholeKB <= Before->WholeKB + 1024);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipToBelowGroupPixelTest,
	"DreamGUI.RHI.ClipToBelow.AContainerAndATextBelowAreClippedToByAGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamClipToBelowGroupPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	UTexture2D* Checker = Stage->MakeChecker(64, 8, FColor(235, 235, 235, 255), FColor(90, 90, 100, 255));
	UTexture2D* Gradient = Stage->MakeGradient(64, FColor(255, 120, 60, 255), FColor(60, 120, 255, 255));
	// Above: a container of two rect blocks, the layer they make clipped to by a gradient.
	UDreamWidget* Base = Stage->AddWidget(TEXT("Base"), FVector2D(200.0, 90.0), FVector2D(0.0, 62.0));
	for (const bool bLeft : { true, false })
	{
		UDreamRectBlock* Pill = Stage->AddRectBlock(bLeft ? TEXT("PillLeft") : TEXT("PillRight"), FVector2D(90.0, 70.0), FVector2D::ZeroVector
			, FColor(255, 255, 255, bLeft ? 255 : 140), 35.0f);
		UDreamWidget* PillWidget = Pill != nullptr ? Pill->GetWidget() : nullptr;
		if (PillWidget != nullptr)
		{
			PillWidget->TrySetParent(Base, false);
			PillWidget->SetAnchoredPosition(FVector2D(bLeft ? -50.0 : 50.0, 0.0));
		}
	}
	Stage->AddBlock(TEXT("OnBase"), FVector2D(240.0, 100.0), FVector2D(0.0, 62.0), FColor::White, Gradient)->SetClipToBelow(true);
	// Below: a text, which no clip level follows, clipped to by a checker -- a picture in the shape of its glyphs.
	Stage->AddText(TEXT("Word"), TEXT("DREAM"), 60.0f, FVector2D(230.0, 90.0), FVector2D(0.0, -62.0), FColor::White);
	Stage->AddBlock(TEXT("OnWord"), FVector2D(240.0, 100.0), FVector2D(0.0, -62.0), FColor::White, Checker)->SetClipToBelow(true);
	EnqueueSettledPictureCheck(Stage, TEXT("Gallery_ClipToBelow_Group"), 8000, EMissingGolden::Warn);
	EnqueueReadBack(Stage, [this, Stage](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		// Past the layer below, what clips to it is not drawn: the backdrop shows.
		const FColor Outside = PixelAt(InPixels, InSize, FVector2D(115.0, 62.0));
		TestTrue(FString::Printf(TEXT("Past the layer below the backdrop shows (%s)"), *FDreamPixelProbe::Describe(Outside)), FDreamPixelProbe::IsNear(Outside, Stage->GetBackdrop(), 3));
		// Over the text's row, the checker shows in the glyphs only: some of the row is drawn, and much of it is not.
		int32 Drawn = 0;
		int32 Total = 0;
		for (int32 X = -115; X < 115; ++X)
		{
			for (int32 Y = -100; Y < -24; Y += 2)
			{
				++Total;
				Drawn += FDreamPixelProbe::IsNear(PixelAt(InPixels, InSize, FVector2D(X, Y)), Stage->GetBackdrop(), 6) ? 0 : 1;
			}
		}
		TestTrue(FString::Printf(TEXT("The checker shows in the glyphs and not between them (%d of %d drawn)"), Drawn, Total), Drawn > Total / 10 && Drawn < Total * 7 / 10);
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRetainedCompositePixelTest,
	"DreamGUI.RHI.Group.ARetainedTextureIsPutDownAsAGroupOfTheSameSubtreeIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamRetainedCompositePixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamBlendGroupPixelTestLocal;
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	// The same stripes behind each side, so that the two sides are the same picture.
	UTexture2D* Stripes = MakeStripes(*Stage, BehindColours, true);
	Stage->AddBlock(TEXT("SceneLeft"), FVector2D(128.0, 256.0), FVector2D(-64.0, 0.0), FColor::White, Stripes);
	Stage->AddBlock(TEXT("SceneRight"), FVector2D(128.0, 256.0), FVector2D(64.0, 0.0), FColor::White, Stripes);
	auto Subtree = [&Stage](const TCHAR* InName, FVector2D InPosition)
	{
		UDreamWidget* Container = Stage->AddWidget(InName, FVector2D(100.0, 100.0), InPosition);
		Stage->AddBlock(*FString::Printf(TEXT("%sRed"), InName), FVector2D(60.0, 60.0), FVector2D(-15.0, 15.0), FColor(220, 40, 40, 255), nullptr, Container);
		Stage->AddBlock(*FString::Printf(TEXT("%sBlue"), InName), FVector2D(60.0, 60.0), FVector2D(15.0, -15.0), FColor(40, 80, 220, 200), nullptr, Container);
		return Container;
	};
	// On the left, a layer group; on the right, the same subtree drawn into a texture of its own (a retainer box's setup),
	// shown by a previewer through the composite. Both at the same opacity and mode: the same picture.
	AddGroup(Subtree(TEXT("Grouped"), FVector2D(-64.0, 0.0)), EDreamLayerGroupIsolation::Isolated, 0.6f, EDreamUIBlendMode::Overlay);
	UDreamWidget* Retained = Subtree(TEXT("Retained"), FVector2D(64.0, 0.0));
	UDreamCanvas* RetainedCanvas = Retained->AddComponent<UDreamCanvas>();
	RetainedCanvas->SetForceRenderToTarget(true);
	RetainedCanvas->SetRenderMode(EDreamRenderMode::RenderTarget);
	// Redrawn every frame: what a retainer box asks of it, through RequestUpdateForRenderTarget, is not what is under test.
	RetainedCanvas->SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode::Always);
	UDreamWidget* Display = Stage->AddWidget(TEXT("Display"), FVector2D(100.0, 100.0), FVector2D(64.0, 0.0));
	UDreamCanvasRenderTargetPreviewer* Previewer = Display->CreateNewVisual<UDreamCanvasRenderTargetPreviewer>();
	Previewer->SetPreviewCanvas(RetainedCanvas);
	Previewer->SetComposite(true, EDreamUIBlendMode::Overlay, 0.6f);
	EnqueuePictureCheck(Stage, TEXT("Gallery_Group_Retained"), 30000);
	EnqueueReadBack(Stage, [this, Stage, Previewer, RetainedCanvas](const TArray<FColor>& InPixels, FIntPoint InSize)
	{
		// What the retained side is made of, said whichever way the picture comes out.
		UTextureRenderTarget2D* Target = RetainedCanvas->GetRenderTarget();
		int32 Opaque = 0;
		TArray<FColor> Retained;
		FIntPoint RetainedSize = FIntPoint::ZeroValue;
		if (Target != nullptr && FDreamPixelProbe::ReadBack(Target, Retained, RetainedSize))
		{
			for (const FColor& Texel : Retained)
			{
				Opaque += Texel.A > 0 ? 1 : 0;
			}
		}
		FString Shape;
		for (const FDreamUIDrawCall& DrawCall : Stage->GetCanvas()->GetCurrentDrawCalls())
		{
			Shape += DrawCall.Type == EDreamUIDrawCallType::GroupBegin ? (DrawCall.Composite.IsValid() && DrawCall.Composite->bExternal ? TEXT("[") : TEXT("("))
				: DrawCall.Type == EDreamUIDrawCallType::GroupEnd ? TEXT(")") : DrawCall.Type == EDreamUIDrawCallType::BatchMesh ? TEXT("B") : TEXT("?");
		}
		FDreamUICompositeDesc Desc;
		const bool bSource = Previewer->GetCompositeSource(Desc);
		AddInfo(FString::Printf(TEXT("retained target %s %dx%d, %d texels with alpha; previewer through the composite %d, source %d; stage draw calls %s; bounds %s"),
			Target != nullptr ? TEXT("made") : TEXT("missing"), RetainedSize.X, RetainedSize.Y, Opaque, Previewer->DrawsThroughComposite() ? 1 : 0, bSource ? 1 : 0, *Shape,
			*Desc.WorldBounds.ToString()));
		int32 Differing = 0;
		int32 Compared = 0;
		for (int32 Y = -40; Y <= 40; Y += 4)
		{
			for (int32 X = -40; X <= 40; X += 4)
			{
				const FColor Left = PixelAt(InPixels, InSize, FVector2D(-64.0 + X, Y));
				const FColor Right = PixelAt(InPixels, InSize, FVector2D(64.0 + X, Y));
				++Compared;
				Differing += FDreamPixelProbe::IsNear(Left, Right, 4) ? 0 : 1;
			}
		}
		TestTrue(FString::Printf(TEXT("The retained texture and the group put down alike (%d of %d samples differ)"), Differing, Compared), Differing <= Compared / 50);
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
