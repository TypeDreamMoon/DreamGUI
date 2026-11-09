// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamLayerStyleTypes.h"
#include "Core/DreamUIManager.h"
#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A rect block's multi-stop gradients: the two-colour gradient it had before, read as two stops that draw what it drew; the
 * gradient it draws, written or two-colour; the old setters on a gradient that has stops; the rows of the world's paint
 * rows it holds; and its gradients as a layer style's and back.
 */
namespace DreamRectBlockGradientTestLocal
{
	UDreamWidget* MakeWidget(UWorld* InWorld, UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
		Widget->SetDisplayName(InName);
		Widget->SetWidth(static_cast<float>(InSize.X));
		Widget->SetHeight(static_cast<float>(InSize.Y));
		Widget->OnRegister();
		if (InParent != nullptr)
		{
			Widget->TrySetParent(InParent, false);
		}
		return Widget;
	}

	/** A game world with a UI manager and a screen-space canvas root. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamCanvas* Canvas = nullptr;

		FStage()
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(World.World, nullptr, TEXT("Root"), FVector2D(800.0, 600.0));
			Canvas = Root->AddComponent<UDreamCanvas>();
			if (Canvas != nullptr)
			{
				Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
			}
		}
		bool IsUsable() const { return Manager != nullptr && Root != nullptr && Canvas != nullptr; }
		void Frames(int32 InCount = 3) const
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}
		int32 CountGradientRows() const
		{
			int32 TextureRows = 0;
			int32 TextRows = 0;
			int32 GradientRows = 0;
			int64 Bytes = 0;
			Manager->GetPaintRowsMemoryInfo(TextureRows, TextRows, GradientRows, Bytes);
			return GradientRows;
		}
	};

	/** sRGB's decode, exactly. */
	float Decode(uint8 InCode)
	{
		const float Encoded = InCode / 255.0f;
		return Encoded <= 0.04045f ? Encoded / 12.92f : FMath::Pow((Encoded + 0.055f) / 1.055f, 2.4f);
	}

	/**
	 * What a rect block drew before its gradients had stops, at a point (UV from the top left), on a rect InSize big: the
	 * two-colour radial gradient's own arithmetic, in linear light, the gradient colour laid over the outer one by
	 * (1 - distance) -- straight colour out.
	 */
	FLinearColor TwoColourAt(const FColor& InInner, const FColor& InOuter, FVector2f InCenter, FVector2f InRadius, float InRotation, FVector2f InSize, FVector2f InUV)
	{
		const FVector2f Centre(InCenter.X, InSize.Y - InCenter.Y);
		FVector2f Frag = InUV * InSize;
		const float Angle = FMath::DegreesToRadians(InRotation);
		const FVector2f Offset = Frag - Centre;
		Frag = FVector2f(FMath::Cos(Angle) * Offset.X + FMath::Sin(Angle) * Offset.Y, -FMath::Sin(Angle) * Offset.X + FMath::Cos(Angle) * Offset.Y) + Centre;
		Frag.X = (Frag.X - Centre.X) * InRadius.Y / InRadius.X + Centre.X;
		const float Distance = FMath::Clamp(FVector2f::Distance(Frag, Centre) / InRadius.Y, 0.0f, 1.0f);
		const float OuterA = InOuter.A / 255.0f;
		const float InnerA = InInner.A / 255.0f * (1.0f - Distance);
		const FVector3f Outer(Decode(InOuter.R) * OuterA, Decode(InOuter.G) * OuterA, Decode(InOuter.B) * OuterA);
		const FVector3f Inner(Decode(InInner.R) * InnerA, Decode(InInner.G) * InnerA, Decode(InInner.B) * InnerA);
		const FVector3f Mixed = Outer * (1.0f - InnerA) + Inner;
		const float Alpha = OuterA * (1.0f - InnerA) + InnerA;
		return Alpha > 0.0f ? FLinearColor(Mixed.X / Alpha, Mixed.Y / Alpha, Mixed.Z / Alpha, Alpha) : FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);
	}

	/** The two as they reach an 8-bit sRGB target, premultiplied over black. */
	int32 LargestCodeDifference(const FLinearColor& InA, const FLinearColor& InB)
	{
		const FColor A = FLinearColor(InA.R * InA.A, InA.G * InA.A, InA.B * InA.A, 1.0f).ToFColor(true);
		const FColor B = FLinearColor(InB.R * InB.A, InB.G * InB.A, InB.B * InB.A, 1.0f).ToFColor(true);
		return FMath::Max(FMath::Max(FMath::Abs(A.R - B.R), FMath::Abs(A.G - B.G)),
			FMath::Max(FMath::Abs(A.B - B.B), FMath::Abs(FMath::RoundToInt(InA.A * 255.0f) - FMath::RoundToInt(InB.A * 255.0f))));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockTwoColourStopsTest,
	"DreamGUI.RectBlock.Gradient.ATwoColourGradientAsTwoStopsDrawsWhatItDrewToALevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockTwoColourStopsTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockGradientTestLocal;
	// The plain case first: the gradient colour at the centre, the outer one at the radius and beyond.
	const FDreamGradient Plain = UDreamRectBlock::MakeTwoColourGradient(FColor::Red, FColor::Blue, FVector2f(50.0f, 30.0f), FVector2f(50.0f, 30.0f), 0.0f, 100.0f, 60.0f);
	TestTrue(TEXT("a radial ellipse of an explicit radius"), Plain.Type == EDreamPaintType::Radial && Plain.Shape == EDreamPaintRadialShape::Ellipse
		&& Plain.Size == EDreamPaintRadialSize::Explicit);
	TestTrue(TEXT("mixed in linear light"), Plain.Interpolation == EDreamPaintInterpolation::Linear);
	TestTrue(TEXT("the gradient colour, then the outer one"), Plain.Stops.Num() == 2 && Plain.Stops[0] == FDreamGradientStop(0.0f, FColor::Red)
		&& Plain.Stops[1] == FDreamGradientStop(1.0f, FColor::Blue));
	TestTrue(TEXT("centred, half the rect across"), Plain.Center.Equals(FVector2f(0.5f, 0.5f)) && Plain.Radius.Equals(FVector2f(0.5f, 0.5f)));

	// A see-through gradient colour is laid over the outer one at the centre: the first stop is the two together.
	const FDreamGradient HalfSeen = UDreamRectBlock::MakeTwoColourGradient(FColor(255, 255, 255, 128), FColor(0, 0, 0, 255), FVector2f(50.0f, 30.0f), FVector2f(50.0f, 30.0f), 0.0f, 100.0f, 60.0f);
	TestTrue(*FString::Printf(TEXT("half white over black is opaque grey: %s"), *HalfSeen.Stops[0].Color.ToString()),
		HalfSeen.Stops[0].Color.A == 255 && FMath::Abs(HalfSeen.Stops[0].Color.R - 188) <= 1);

	// Then everywhere, for many: the stops draw what the two-colour arithmetic drew.
	FRandomStream Random(0x2C010);
	int32 Worst = 0;
	FString WorstCase;
	for (int32 Round = 0; Round < 300; ++Round)
	{
		const FColor Inner(Random.RandRange(0, 255), Random.RandRange(0, 255), Random.RandRange(0, 255), Random.FRand() < 0.5f ? 255 : Random.RandRange(0, 255));
		const FColor Outer(Random.RandRange(0, 255), Random.RandRange(0, 255), Random.RandRange(0, 255), Random.FRand() < 0.5f ? 255 : Random.RandRange(1, 255));
		const FVector2f Size(Random.FRandRange(20.0f, 300.0f), Random.FRandRange(20.0f, 300.0f));
		const FVector2f Centre(Random.FRandRange(-0.2f, 1.2f) * Size.X, Random.FRandRange(-0.2f, 1.2f) * Size.Y);
		const FVector2f Radius(Random.FRandRange(0.1f, 1.0f) * Size.X, Random.FRandRange(0.1f, 1.0f) * Size.Y);
		const float Rotation = Random.FRand() < 0.3f ? 0.0f : Random.FRandRange(0.0f, 360.0f);
		const FDreamGradient Stops = UDreamRectBlock::MakeTwoColourGradient(Inner, Outer, Centre, Radius, Rotation, Size.X, Size.Y);
		for (int32 Sample = 0; Sample < 12; ++Sample)
		{
			const FVector2f UV(Random.FRandRange(0.0f, 1.0f), Random.FRandRange(0.0f, 1.0f));
			const int32 Difference = LargestCodeDifference(Stops.Evaluate(UV, Size.X / Size.Y), TwoColourAt(Inner, Outer, Centre, Radius, Rotation, Size, UV));
			if (Difference > Worst)
			{
				Worst = Difference;
				WorstCase = FString::Printf(TEXT("%s over %s at (%s) radius (%s) turned %.1f on %s, at (%s)"), *Inner.ToString(), *Outer.ToString(),
					*Centre.ToString(), *Radius.ToString(), Rotation, *Size.ToString(), *UV.ToString());
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("no channel is more than a level away (worst %d: %s)"), Worst, *WorstCase), Worst <= 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockEffectiveGradientTest,
	"DreamGUI.RectBlock.Gradient.ARectBlockDrawsItsGradientOnceItHasStopsAndItsTwoColourOneUntilThen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockEffectiveGradientTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockGradientTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Widget = MakeWidget(Stage.World.World, Stage.Root, TEXT("Block"), FVector2D(200.0, 100.0));
	UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
	if (!TestNotNull(TEXT("a rect block"), Block))
	{
		return false;
	}
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Block->SetBodyColor(FColor(10, 20, 30));
	Block->SetEnableBodyGradient(true);
	Block->SetBodyGradientColor(FColor(200, 100, 50));
	Block->SetBodyGradientCenterUnitMode(EDreamRectBlockUnitMode::Value);
	Block->SetBodyGradientCenter(FVector2D(50.0, 25.0));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	// No stops: the two-colour gradient its old properties say, measured at its widget's size.
	TestTrue(TEXT("written: no stops"), Block->GetBodyGradient().Stops.IsEmpty());
	FDreamGradient Drawn = Block->GetEffectiveBodyGradient();
	TestTrue(TEXT("drawn: two stops, the gradient colour then the body's"), Drawn.Stops.Num() == 2
		&& Drawn.Stops[0].Color == FColor(200, 100, 50) && Drawn.Stops[1].Color == FColor(10, 20, 30));
	TestTrue(*FString::Printf(TEXT("a centre in units, from the bottom left, as fractions from the top left: %s"), *Drawn.Center.ToString()),
		Drawn.Center.Equals(FVector2f(0.25f, 0.75f)));
	// The body's colour is the outer stop: changing it changes what is drawn.
	Block->SetBodyColor(FColor(0, 255, 0));
	TestEqual(TEXT("the outer stop follows the body's colour"), Block->GetEffectiveBodyGradient().Stops[1].Color, FColor(0, 255, 0));
	// And a size in units follows the widget.
	Widget->SetWidth(400.0f);
	TestTrue(TEXT("a centre in units stays where it is in units"), Block->GetEffectiveBodyGradient().Center.Equals(FVector2f(0.125f, 0.75f)));

	// Written with stops, the gradient is drawn as written.
	FDreamGradient Written;
	FDreamGradient::ParseCss(TEXT("linear-gradient(90deg, #fff, #000 40%, #f00)"), Written);
	Block->SetBodyGradient(Written);
	TestTrue(TEXT("drawn as written"), Block->GetEffectiveBodyGradient() == Written);

	// The rect block holds a row of the world's paint rows while its gradient paints, and gives it back.
	const int32 RowsBefore = Stage.CountGradientRows();
	Stage.Frames();
	TestTrue(TEXT("it holds a gradient row"), Block->GetBodyGradientRow() != INDEX_NONE);
	TestEqual(TEXT("one more than before"), Stage.CountGradientRows(), RowsBefore + 1);
	UDreamWidget* Twin = MakeWidget(Stage.World.World, Stage.Root, TEXT("Twin"), FVector2D(100.0, 100.0));
	UDreamRectBlock* TwinBlock = Twin->CreateNewVisual<UDreamRectBlock>();
	TwinBlock->SetEnableBodyGradient(true);
	TwinBlock->SetBodyGradient(Written);
	Stage.Frames();
	TestEqual(TEXT("another drawing the same gradient shares its row"), TwinBlock->GetBodyGradientRow(), Block->GetBodyGradientRow());
	TestEqual(TEXT("so no row more is taken"), Stage.CountGradientRows(), RowsBefore + 1);
	Block->SetEnableBodyGradient(false);
	TwinBlock->SetEnableBodyGradient(false);
	Stage.Frames();
	TestTrue(TEXT("off, it holds none"), Block->GetBodyGradientRow() == INDEX_NONE && TwinBlock->GetBodyGradientRow() == INDEX_NONE);
	TestEqual(TEXT("and the row is free again"), Stage.CountGradientRows(), RowsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockOldSettersTest,
	"DreamGUI.RectBlock.Gradient.TheTwoColourSettersSetTheFirstStopTheCentreTheRadiusAndTheAngleOfAGradientWithStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockOldSettersTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockGradientTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Widget = MakeWidget(Stage.World.World, Stage.Root, TEXT("Block"), FVector2D(200.0, 100.0));
	UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
	FDreamGradient Written;
	FDreamGradient::ParseCss(TEXT("radial-gradient(#fff, #000)"), Written);
	Block->SetBodyGradient(Written);
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Block->SetBodyGradientColor(FColor(1, 2, 3));
	TestEqual(TEXT("the colour is the first stop's"), Block->GetBodyGradient().Stops[0].Color, FColor(1, 2, 3));
	TestEqual(TEXT("and reads back"), Block->GetBodyGradientColor(), FColor(1, 2, 3));
	Block->SetBodyGradientCenter(FVector2D(0.25, 0.75));
	TestTrue(TEXT("a centre from the bottom left, as a fraction, lands from the top left"), Block->GetBodyGradient().Center.Equals(FVector2f(0.25f, 0.25f)));
	TestTrue(TEXT("and reads back as it was set"), Block->GetBodyGradientCenter().Equals(FVector2f(0.25f, 0.75f)));
	Block->SetBodyGradientRadiusUnitMode(EDreamRectBlockUnitMode::Value);
	Block->SetBodyGradientRadius(FVector2D(50.0, 25.0));
	TestTrue(TEXT("a radius in units is an explicit one, of the rect"), Block->GetBodyGradient().Size == EDreamPaintRadialSize::Explicit
		&& Block->GetBodyGradient().Radius.Equals(FVector2f(0.25f, 0.25f)));
	TestTrue(TEXT("and reads back in units"), Block->GetBodyGradientRadius().Equals(FVector2f(50.0f, 25.0f), 1e-3f));
	Block->SetBodyGradientRotation(30.0f);
	TestEqual(TEXT("the rotation is the angle"), Block->GetBodyGradient().Angle, 30.0f);
	TestEqual(TEXT("which a tween's getter reads"), Block->GetBodyGradientRotation(), 30.0f);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	TestEqual(TEXT("the stops are still the gradient's two"), Block->GetBodyGradient().Stops.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockGradientAsLayerStyleTest,
	"DreamGUI.RectBlock.Gradient.ABodyGradientIsALayerStylesGradientOverlayOfItsStopsAndBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockGradientAsLayerStyleTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockGradientTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is set up"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Widget = MakeWidget(Stage.World.World, Stage.Root, TEXT("Block"), FVector2D(200.0, 100.0));
	UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
	FDreamGradient Written;
	FDreamGradient::ParseCss(TEXT("diamond-gradient(#ff0000, #00ff0080 50%, #0000ff)"), Written);
	Block->SetEnableBodyGradient(true);
	Block->SetBodyGradient(Written);
	const FDreamLayerEffects Style = Block->GetEffectsAsLayerStyle();
	if (!TestEqual(TEXT("one gradient overlay"), Style.GradientOverlays.Num(), 1))
	{
		return false;
	}
	const FDreamStyleGradientFill& Fill = Style.GradientOverlays[0].Fill;
	TestTrue(TEXT("of the same kind"), Fill.Type == EDreamStyleGradientType::Diamond);
	TestTrue(TEXT("a colour stop and an opacity stop for each stop"), Fill.Gradient.ColorStops.Num() == 3 && Fill.Gradient.OpacityStops.Num() == 3);
	TestTrue(TEXT("its colours opaque, its alpha apart"), Fill.Gradient.ColorStops[1].Color == FColor(0, 255, 0, 255)
		&& FMath::IsNearlyEqual(Fill.Gradient.OpacityStops[1].Opacity, 128.0f / 255.0f));

	// Back: the style's stops, as the rect block's own.
	UDreamWidget* Other = MakeWidget(Stage.World.World, Stage.Root, TEXT("Other"), FVector2D(200.0, 100.0));
	UDreamRectBlock* OtherBlock = Other->CreateNewVisual<UDreamRectBlock>();
	OtherBlock->SetEffectsFromLayerStyle(Style);
	TestTrue(TEXT("its body gradient on"), OtherBlock->GetEnableBodyGradient());
	const FDreamGradient& Back = OtherBlock->GetBodyGradient();
	TestTrue(TEXT("a diamond again"), Back.Type == EDreamPaintType::Diamond);
	if (TestEqual(TEXT("with the three stops"), Back.Stops.Num(), 3))
	{
		TestTrue(*FString::Printf(TEXT("the middle one half see-through green: %s"), *Back.Stops[1].Color.ToString()),
			Back.Stops[1].Color.G == 255 && FMath::Abs(Back.Stops[1].Color.A - 128) <= 1 && FMath::IsNearlyEqual(Back.Stops[1].Position, 0.5f));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
