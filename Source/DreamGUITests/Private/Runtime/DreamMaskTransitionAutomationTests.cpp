// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "UObject/Package.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIClipData.h"
#include "Core/DreamUIClipMask.h"
#include "Core/DreamUIManager.h"
#include "DreamTweenBPLibrary.h"
#include "Extensions/DreamMaskTransition.h"
#include "Utils/DreamUIUtils.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * Mask transitions on the CPU: the mask each preset makes, measured with the clip mask's own CPU twin
 * (FDreamUIClipMaskMath, which the hit test runs and the picture tests hold to the shader), the order textures, and the
 * driving of a widget's clip. The textures a preset reads are measured from the same arithmetic that made them.
 */
namespace DreamMaskTransitionTestLocal
{
	const FVector2f Size(240.0f, 120.0f);

	/** InOrder of InWidth by InHeight read as the GPU reads it: bilinearly, clamped, or repeated and nearest for a tile. */
	FLinearColor Read(const TArray<float>& InOrder, int32 InWidth, int32 InHeight, FVector2f InUV, bool bInTile)
	{
		if (InOrder.Num() != InWidth * InHeight)
		{
			return FLinearColor::White;
		}
		if (bInTile)
		{
			const float U = InUV.X - FMath::FloorToFloat(InUV.X);
			const float V = InUV.Y - FMath::FloorToFloat(InUV.Y);
			const int32 X = FMath::Clamp(FMath::FloorToInt(U * InWidth), 0, InWidth - 1);
			const int32 Y = FMath::Clamp(FMath::FloorToInt(V * InHeight), 0, InHeight - 1);
			return FLinearColor(InOrder[Y * InWidth + X], 0.0f, 0.0f, 1.0f);
		}
		const float X = FMath::Clamp(InUV.X * InWidth - 0.5f, 0.0f, InWidth - 1.0f);
		const float Y = FMath::Clamp(InUV.Y * InHeight - 0.5f, 0.0f, InHeight - 1.0f);
		const int32 X0 = FMath::FloorToInt(X), Y0 = FMath::FloorToInt(Y);
		const int32 X1 = FMath::Min(X0 + 1, InWidth - 1), Y1 = FMath::Min(Y0 + 1, InHeight - 1);
		const float Fx = X - X0, Fy = Y - Y0;
		const float Top = FMath::Lerp(InOrder[Y0 * InWidth + X0], InOrder[Y0 * InWidth + X1], Fx);
		const float Bottom = FMath::Lerp(InOrder[Y1 * InWidth + X0], InOrder[Y1 * InWidth + X1], Fx);
		return FLinearColor(FMath::Lerp(Top, Bottom, Fy), 0.0f, 0.0f, 1.0f);
	}

	/** A transition, not on any widget, set to InPreset at InProgress. */
	UDreamMaskTransition* Make(EDreamMaskTransitionPreset InPreset, float InProgress, bool bInInvert = false, bool bInReverse = false)
	{
		UDreamMaskTransition* Transition = NewObject<UDreamMaskTransition>(GetTransientPackage());
		Transition->SetPreset(InPreset);
		Transition->SetSoftness(0.05f);
		Transition->SetInvert(bInInvert);
		Transition->SetReverse(bInReverse);
		Transition->SetProgress(InProgress);
		return Transition;
	}

	/**
	 * How much of the point InAt (0..1 over the rect, y down) a mask shows, measured as the hit test measures it. The order
	 * textures are made again here by the arithmetic that made the transition's.
	 */
	struct FMeasure
	{
		FDreamClipMask Mask;
		FDreamUIClipLevel Level;
		TArray<float> Order;
		int32 Extent = 0;
		bool bTile = false;

		bool Prepare(UDreamMaskTransition* InTransition, UTexture* InCustom = nullptr)
		{
			if (!InTransition->MakeMask(Size, Mask))
			{
				return false;
			}
			const FVector2f Half = Size * 0.5f;
			Level.HalfSize = Half;
			Level.CornerRadius = FVector4f::Zero();
			Level.bMasked = FDreamUIClipMaskMath::PackMask(Mask, Half, 0, Level.Mask);
			const float Aspect = Size.X / Size.Y;
			const FVector2f Centre(InTransition->GetCentre());
			switch (InTransition->GetPreset())
			{
			case EDreamMaskTransitionPreset::Ink: Extent = 256; FDreamMaskTransitionMath::MakeInk(Extent, Extent, Aspect, Centre, InTransition->GetSeed(), Order); break;
			case EDreamMaskTransitionPreset::Shatter: Extent = 512; FDreamMaskTransitionMath::MakeShatter(Extent, Extent, Aspect, Centre, InTransition->GetCount(), InTransition->GetSeed(), Order); break;
			case EDreamMaskTransitionPreset::Checker: Extent = 64; bTile = true; FDreamMaskTransitionMath::MakeCheckerTile(Extent, Order); break;
			default: break;
			}
			return Level.bMasked;
		}
		float At(FVector2f InAt) const
		{
			const FVector2f P((InAt.X - 0.5f) * Size.X, (0.5f - InAt.Y) * Size.Y);
			return FDreamUIClipMaskMath::EvaluateLevel(Level, P, 0.0f, [this](FVector2f UV, FLinearColor& OutTexel)
			{
				if (Extent > 0)
				{
					OutTexel = Read(Order, Extent, Extent, UV, bTile);
					return true;
				}
				// The custom texture of the tests below: dark on the left, light on the right.
				OutTexel = FLinearColor(FMath::Clamp(UV.X, 0.0f, 1.0f), 0.0f, 0.0f, 1.0f);
				return true;
			});
		}
	};

	TArray<FVector2f> Grid(int32 InSteps = 16)
	{
		TArray<FVector2f> Points;
		for (int32 Y = 0; Y < InSteps; ++Y)
		{
			for (int32 X = 0; X < InSteps; ++X)
			{
				Points.Add(FVector2f((X + 0.5f) / InSteps, (Y + 0.5f) / InSteps));
			}
		}
		return Points;
	}

	/** A game world with a UI manager and a world-space canvas root, as the clip mask tests have it. */
	struct FStage
	{
		DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Game };
		UDreamUIManagerWorldSubsystem* Manager = nullptr;
		UDreamWidget* Root = nullptr;

		FStage()
		{
			Manager = UDreamUIManagerWorldSubsystem::GetInstance(World.World);
			Root = MakeWidget(nullptr, TEXT("Root"), FVector2D(800.0, 600.0));
			if (UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>())
			{
				Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
			}
		}
		bool IsUsable() const { return Manager != nullptr && Root != nullptr && Root->GetRenderCanvas() != nullptr; }
		void Frames(int32 InCount = 2) const
		{
			for (int32 Frame = 0; Frame < InCount; ++Frame)
			{
				Manager->Tick(1.0f / 30.0f);
				Manager->SubmitCanvasDrawCall();
			}
		}
		UDreamWidget* MakeWidget(UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize, FVector2D InPosition = FVector2D::ZeroVector) const
		{
			UDreamWidget* Widget = NewObject<UDreamWidget>(World.World, NAME_None, RF_Transient);
			Widget->SetDisplayName(InName);
			Widget->SetWidth(static_cast<float>(InSize.X));
			Widget->SetHeight(static_cast<float>(InSize.Y));
			Widget->OnRegister();
			if (InParent != nullptr)
			{
				Widget->TrySetParent(InParent, false);
				Widget->SetAnchoredPosition(InPosition);
			}
			return Widget;
		}
		UDreamWidget* MakeBlock(UDreamWidget* InParent, const TCHAR* InName, FVector2D InSize) const
		{
			UDreamWidget* Widget = MakeWidget(InParent, InName, InSize);
			if (UDreamTexture* Visual = Widget->CreateNewVisual<UDreamTexture>())
			{
				Visual->SetTexture(FDreamUIUtils::GetDefaultWhiteTexture());
			}
			return Widget;
		}
	};

	/** The world point at InAt (0..1, y down) of InWidget's rect, in its plane. */
	FVector PointOf(const UDreamWidget* InWidget, FVector2f InAt)
	{
		return InWidget->GetWorldTransform().TransformPosition(FVector(0.0,
			InWidget->GetLocalSpaceLeft() + InAt.X * InWidget->GetWidth(), InWidget->GetLocalSpaceTop() - InAt.Y * InWidget->GetHeight()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionRangeTest,
	"DreamGUI.MaskTransition.EveryPresetHidesAllAtNoneShowsAllAtOneAndMoreAsItGoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMaskTransitionRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamMaskTransitionTestLocal;
	UTexture2D* Custom = UTexture2D::CreateTransient(4, 4);
	const TArray<FVector2f> Points = Grid();
	const UEnum* Presets = StaticEnum<EDreamMaskTransitionPreset>();
	for (int32 PresetIndex = 0; PresetIndex < Presets->NumEnums() - 1; ++PresetIndex)
	{
		const EDreamMaskTransitionPreset Preset = static_cast<EDreamMaskTransitionPreset>(PresetIndex);
		const FString Name = Presets->GetNameStringByValue(PresetIndex);
		TArray<float> Previous;
		Previous.Init(0.0f, Points.Num());
		for (const float Progress : { 0.0f, 0.15f, 0.35f, 0.5f, 0.65f, 0.85f, 1.0f })
		{
			UDreamMaskTransition* Transition = Make(Preset, Progress);
			if (Preset == EDreamMaskTransitionPreset::Texture)
			{
				Transition->SetTexture(Custom, EDreamClipMaskChannel::Red);
			}
			FMeasure Measure;
			if (!TestTrue(*FString::Printf(TEXT("%s makes a mask at %.2f"), *Name, Progress), Measure.Prepare(Transition)))
			{
				break;
			}
			float Lowest = 1.0f, Highest = 0.0f;
			bool bMonotonic = true;
			for (int32 Index = 0; Index < Points.Num(); ++Index)
			{
				const float Shown = Measure.At(Points[Index]);
				Lowest = FMath::Min(Lowest, Shown);
				Highest = FMath::Max(Highest, Shown);
				bMonotonic &= Shown >= Previous[Index] - 1.0e-4f;
				Previous[Index] = Shown;
			}
			if (Progress == 0.0f)
			{
				TestTrue(*FString::Printf(TEXT("%s shows nothing at 0"), *Name), Highest <= 1.0e-3f);
			}
			else if (Progress == 1.0f)
			{
				TestTrue(*FString::Printf(TEXT("%s shows everything at 1"), *Name), Lowest >= 1.0f - 1.0e-3f);
			}
			else if (Progress == 0.5f)
			{
				TestTrue(*FString::Printf(TEXT("%s shows some and hides some halfway"), *Name), Lowest < 0.5f && Highest > 0.5f);
			}
			TestTrue(*FString::Printf(TEXT("%s shows no less of any point at %.2f than before"), *Name, Progress), bMonotonic);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionDirectionTest,
	"DreamGUI.MaskTransition.EachShapeStartsWhereItsNameSaysAndReverseStartsAtTheOtherEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMaskTransitionDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamMaskTransitionTestLocal;
	auto Shown = [&](UDreamMaskTransition* InTransition, FVector2f InAt)
	{
		FMeasure Measure;
		Measure.Prepare(InTransition);
		return Measure.At(InAt);
	};
	const FVector2f Left(0.1f, 0.5f), Right(0.9f, 0.5f), Top(0.5f, 0.1f), Bottom(0.5f, 0.9f), Middle(0.5f, 0.5f), Corner(0.05f, 0.05f);
	TestTrue(TEXT("A wipe starts at the left"), Shown(Make(EDreamMaskTransitionPreset::Wipe, 0.3f), Left) > 0.99f && Shown(Make(EDreamMaskTransitionPreset::Wipe, 0.3f), Right) < 0.01f);
	TestTrue(TEXT("...reversed, at the right"), Shown(Make(EDreamMaskTransitionPreset::Wipe, 0.3f, false, true), Right) > 0.99f && Shown(Make(EDreamMaskTransitionPreset::Wipe, 0.3f, false, true), Left) < 0.01f);
	UDreamMaskTransition* Down = Make(EDreamMaskTransitionPreset::Wipe, 0.3f);
	Down->SetAngle(90.0f);
	TestTrue(TEXT("...turned a quarter clockwise, at the top"), Shown(Down, Top) > 0.99f && Shown(Down, Bottom) < 0.01f);
	const FVector2f HalfPastOne(0.7f, 0.2f), HalfPastTen(0.3f, 0.2f);
	TestTrue(TEXT("A clock sweeps clockwise from twelve"), Shown(Make(EDreamMaskTransitionPreset::Clock, 0.25f), HalfPastOne) > 0.99f && Shown(Make(EDreamMaskTransitionPreset::Clock, 0.25f), HalfPastTen) < 0.01f);
	TestTrue(TEXT("...reversed, anticlockwise"), Shown(Make(EDreamMaskTransitionPreset::Clock, 0.25f, false, true), HalfPastTen) > 0.99f && Shown(Make(EDreamMaskTransitionPreset::Clock, 0.25f, false, true), HalfPastOne) < 0.01f);
	for (const EDreamMaskTransitionPreset Opening : { EDreamMaskTransitionPreset::Iris, EDreamMaskTransitionPreset::Diamond, EDreamMaskTransitionPreset::Ink, EDreamMaskTransitionPreset::Shatter })
	{
		const FString Name = StaticEnum<EDreamMaskTransitionPreset>()->GetNameStringByValue(static_cast<int64>(Opening));
		TestTrue(*FString::Printf(TEXT("%s opens from the middle"), *Name), Shown(Make(Opening, 0.25f), Middle) > 0.99f && Shown(Make(Opening, 0.25f), Corner) < 0.01f);
		if (Opening == EDreamMaskTransitionPreset::Iris || Opening == EDreamMaskTransitionPreset::Diamond)
		{
			TestTrue(*FString::Printf(TEXT("%s reversed closes in from the edge"), *Name), Shown(Make(Opening, 0.25f, false, true), Corner) > 0.99f && Shown(Make(Opening, 0.25f, false, true), Middle) < 0.01f);
		}
	}
	UDreamMaskTransition* Off = Make(EDreamMaskTransitionPreset::Iris, 0.2f);
	Off->SetCentre(FVector2D(0.15, 0.2));
	TestTrue(TEXT("An iris moved opens where it was put"), Shown(Off, FVector2f(0.15f, 0.2f)) > 0.99f && Shown(Off, Middle) < 0.01f);
	// An iris is round on a widget twice as wide as high: as far across as up, the same.
	FMeasure Round;
	Round.Prepare(Make(EDreamMaskTransitionPreset::Iris, 0.3f));
	const float Reach = 0.3f;
	TestTrue(TEXT("An iris is round on a wide widget"),
		FMath::IsNearlyEqual(Round.At(FVector2f(0.5f + Reach * 0.25f, 0.5f)), Round.At(FVector2f(0.5f, 0.5f + Reach * 0.5f)), 0.02f));
	// Blinds: every stripe opens from its own left.
	UDreamMaskTransition* Blinds = Make(EDreamMaskTransitionPreset::Blinds, 0.3f);
	Blinds->SetCount(4);
	int32 Opened = 0;
	for (int32 Stripe = 0; Stripe < 4; ++Stripe)
	{
		Opened += Shown(Blinds, FVector2f((Stripe + 0.1f) / 4.0f, 0.5f)) > 0.99f && Shown(Blinds, FVector2f((Stripe + 0.9f) / 4.0f, 0.5f)) < 0.01f;
	}
	TestEqual(TEXT("Each of four blinds opens from its left"), Opened, 4);
	// Checker: one colour of squares first.
	UDreamMaskTransition* Checker = Make(EDreamMaskTransitionPreset::Checker, 0.45f);
	Checker->SetCount(4);
	FMeasure Board;
	Board.Prepare(Checker);
	const float Square = 1.0f / 4.0f;
	const float SquareDown = Size.X / 4.0f / Size.Y;
	TestTrue(TEXT("Halfway through its first half, one square shows its left part"), Board.At(FVector2f(0.5f + 0.1f * Square, 0.5f + 0.5f * SquareDown)) > 0.99f);
	TestTrue(TEXT("...and its neighbour nothing yet"), Board.At(FVector2f(0.5f + 1.1f * Square, 0.5f + 0.5f * SquareDown)) < 0.01f
		|| Board.At(FVector2f(0.5f + 0.1f * Square, 0.5f + 1.5f * SquareDown)) < 0.01f);
	// Dissolve: about as much shows as the progress says.
	FMeasure Specks;
	Specks.Prepare(Make(EDreamMaskTransitionPreset::Dissolve, 0.5f));
	float Sum = 0.0f;
	const TArray<FVector2f> Points = Grid(40);
	for (const FVector2f& Point : Points)
	{
		Sum += Specks.At(Point);
	}
	TestTrue(TEXT("Halfway, a dissolve shows about half"), FMath::Abs(Sum / Points.Num() - 0.5f) < 0.2f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionInvertTest,
	"DreamGUI.MaskTransition.InvertedEachPresetHidesExactlyWhatItWouldHaveShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMaskTransitionInvertTest::RunTest(const FString& Parameters)
{
	using namespace DreamMaskTransitionTestLocal;
	UTexture2D* Custom = UTexture2D::CreateTransient(4, 4);
	const TArray<FVector2f> Points = Grid(12);
	const UEnum* Presets = StaticEnum<EDreamMaskTransitionPreset>();
	for (int32 PresetIndex = 0; PresetIndex < Presets->NumEnums() - 1; ++PresetIndex)
	{
		const EDreamMaskTransitionPreset Preset = static_cast<EDreamMaskTransitionPreset>(PresetIndex);
		for (const bool bReverse : { false, true })
		{
			for (const float Progress : { 0.2f, 0.5f, 0.8f })
			{
				UDreamMaskTransition* Showing = Make(Preset, Progress, false, bReverse);
				UDreamMaskTransition* Hiding = Make(Preset, Progress, true, bReverse);
				if (Preset == EDreamMaskTransitionPreset::Texture)
				{
					Showing->SetTexture(Custom, EDreamClipMaskChannel::Red);
					Hiding->SetTexture(Custom, EDreamClipMaskChannel::Red);
				}
				FMeasure Shown, Hidden;
				if (!Shown.Prepare(Showing) || !Hidden.Prepare(Hiding))
				{
					AddError(FString::Printf(TEXT("%s made no mask"), *Presets->GetNameStringByValue(PresetIndex)));
					continue;
				}
				float Worst = 0.0f;
				for (const FVector2f& Point : Points)
				{
					Worst = FMath::Max(Worst, FMath::Abs(Shown.At(Point) + Hidden.At(Point) - 1.0f));
				}
				TestTrue(*FString::Printf(TEXT("%s%s at %.1f: shown and hidden add to one everywhere"), *Presets->GetNameStringByValue(PresetIndex),
					bReverse ? TEXT(" reversed") : TEXT(""), Progress), Worst < 1.0e-3f);
			}
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionTexturesTest,
	"DreamGUI.MaskTransition.TheOrderTexturesAreMadeTheSameEveryTimeAndRunFromZeroToOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMaskTransitionTexturesTest::RunTest(const FString& Parameters)
{
	auto Range = [](const TArray<float>& InValues, float& OutMin, float& OutMax)
	{
		OutMin = MAX_flt;
		OutMax = -MAX_flt;
		for (const float Value : InValues)
		{
			OutMin = FMath::Min(OutMin, Value);
			OutMax = FMath::Max(OutMax, Value);
		}
	};
	TArray<float> Ink, InkAgain, OtherInk;
	FDreamMaskTransitionMath::MakeInk(64, 64, 2.0f, FVector2f(0.5f, 0.5f), 3, Ink);
	FDreamMaskTransitionMath::MakeInk(64, 64, 2.0f, FVector2f(0.5f, 0.5f), 3, InkAgain);
	FDreamMaskTransitionMath::MakeInk(64, 64, 2.0f, FVector2f(0.5f, 0.5f), 4, OtherInk);
	TestTrue(TEXT("The same ink twice, to the bit"), Ink == InkAgain);
	TestFalse(TEXT("Another seed, another blot"), Ink == OtherInk);
	float Min, Max;
	Range(Ink, Min, Max);
	TestTrue(TEXT("Ink runs 0 to 1"), Min == 0.0f && Max == 1.0f);
	TestTrue(TEXT("...lowest near the middle"), Ink[32 * 64 + 32] < 0.2f && Ink[0] > 0.6f);
	TArray<float> Shards, ShardsAgain;
	FDreamMaskTransitionMath::MakeShatter(128, 128, 1.5f, FVector2f(0.5f, 0.5f), 12, 7, Shards);
	FDreamMaskTransitionMath::MakeShatter(128, 128, 1.5f, FVector2f(0.5f, 0.5f), 12, 7, ShardsAgain);
	TestTrue(TEXT("The same shards twice, to the bit"), Shards == ShardsAgain);
	Range(Shards, Min, Max);
	TestTrue(TEXT("Shards run 0 to 1"), Min == 0.0f && Max == 1.0f);
	// A shard is one piece: neighbours mostly differ by the shard's own small gradient, and only at edges by a jump.
	int32 Jumps = 0;
	for (int32 Y = 0; Y < 128; ++Y)
	{
		for (int32 X = 1; X < 128; ++X)
		{
			Jumps += FMath::Abs(Shards[Y * 128 + X] - Shards[Y * 128 + X - 1]) > 0.02f;
		}
	}
	TestTrue(TEXT("Shards are pieces, not noise"), Jumps > 10 && Jumps < 128 * 127 / 10);
	TArray<float> Tile;
	FDreamMaskTransitionMath::MakeCheckerTile(8, Tile);
	TestEqual(TEXT("An 8 tile is 64 texels"), Tile.Num(), 64);
	TestTrue(TEXT("The first square runs its first half"), FMath::IsNearlyEqual(Tile[0], 0.5f / 4.0f * 0.5f, 1.0e-6f) && Tile[3] < 0.5f);
	TestTrue(TEXT("...its neighbour the second"), Tile[4] > 0.5f && Tile[4 * 8] > 0.5f && Tile[4 * 8 + 4] < 0.5f);
	TestTrue(TEXT("Showing at no progress puts the threshold past the top"), FDreamMaskTransitionMath::Threshold(0.0f, 0.0f, 1.0f, 0.1f, false) >= 1.1f - 1.0e-6f);
	TestTrue(TEXT("...and at full progress past the bottom"), FDreamMaskTransitionMath::Threshold(1.0f, 0.0f, 1.0f, 0.1f, false) <= -0.1f + 1.0e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionDrivingTest,
	"DreamGUI.MaskTransition.ItDrivesAWidgetsClipWithoutTouchingTheClipMaskItWasGiven",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMaskTransitionDrivingTest::RunTest(const FString& Parameters)
{
	using namespace DreamMaskTransitionTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Panel = Stage.MakeWidget(Stage.Root, TEXT("Panel"), FVector2D(200.0, 100.0));
	UDreamWidget* Content = Stage.MakeBlock(Panel, TEXT("Content"), FVector2D(200.0, 100.0));
	TestTrue(TEXT("The panel does not clip"), Panel->GetClipping() == EDreamWidgetClipping::Inherit);
	UDreamMaskTransition* Transition = Panel->AddComponent<UDreamMaskTransition>();
	Transition->SetSoftness(0.0f);
	Transition->SetProgress(0.5f);
	Stage.Frames(2);
	TestTrue(TEXT("Halfway, the panel's clip is driven"), Panel->IsClipMaskDriven());
	TestTrue(TEXT("...its own Clip Mask untouched"), Panel->GetClipMask() == FDreamClipMask());
	TestTrue(TEXT("...the one it is drawn with active"), Panel->GetEffectiveClipMask().IsActive());
	TestTrue(TEXT("...and its clipping still Inherit"), Panel->GetClipping() == EDreamWidgetClipping::Inherit);
	TSharedPtr<FDreamUIClipData> Clip = Content->GetClipData().Pin();
	TestTrue(TEXT("The content is clipped by the panel"), Clip.IsValid() && Clip->GetWidget() == Panel);
	TestTrue(TEXT("The wiped-in left is hit"), Content->IsPointVisibleOnClip(PointOf(Content, FVector2f(0.2f, 0.5f))));
	TestFalse(TEXT("The right, still to come, is not"), Content->IsPointVisibleOnClip(PointOf(Content, FVector2f(0.8f, 0.5f))));
	Transition->SetProgress(0.0f);
	Stage.Frames(2);
	TestFalse(TEXT("Hidden, nothing is hit"), Content->IsPointVisibleOnClip(PointOf(Content, FVector2f(0.2f, 0.5f))));
	Transition->SetProgress(1.0f);
	Stage.Frames(2);
	TestFalse(TEXT("Shown, it lets go"), Panel->IsClipMaskDriven());
	Clip = Content->GetClipData().Pin();
	TestTrue(TEXT("...and the panel clips nothing again"), !Clip.IsValid() || Clip->GetWidget() != Panel);
	TestTrue(TEXT("...everything is hit"), Content->IsPointVisibleOnClip(PointOf(Content, FVector2f(0.8f, 0.5f))));
	Transition->SetProgress(0.3f);
	Stage.Frames(1);
	TestTrue(TEXT("Driven again"), Panel->IsClipMaskDriven());
	Transition->DestroyComponent();
	Stage.Frames(2);
	TestFalse(TEXT("A transition taken away lets go"), Panel->IsClipMaskDriven());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionCrossFadeTest,
	"DreamGUI.MaskTransition.ACrossFadeShowsEveryPointOnceAndEndsSwapped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMaskTransitionCrossFadeTest::RunTest(const FString& Parameters)
{
	using namespace DreamMaskTransitionTestLocal;
	FStage Stage;
	if (!TestTrue(TEXT("The stage is usable"), Stage.IsUsable()))
	{
		return false;
	}
	UDreamWidget* From = Stage.MakeBlock(Stage.Root, TEXT("From"), FVector2D(240.0, 120.0));
	UDreamWidget* To = Stage.MakeBlock(Stage.Root, TEXT("To"), FVector2D(240.0, 120.0));
	UDreamMaskTransition* Coming = UDreamMaskTransition::FindOrAdd(To);
	Coming->SetSeed(5);
	Coming->SetAngle(30.0f);
	// A test world runs no tween manager, so there the swap happens at once (as a tween of no time would end); in a game
	// the tween runs. Either way the two are set up alike, and the halfway points are set by hand below.
	UDreamTweener* Tween = UDreamMaskTransition::CrossFade(From, To, EDreamMaskTransitionPreset::Ink, 1.0f);
	UDreamMaskTransition* Going = From->GetComponent<UDreamMaskTransition>();
	if (!TestNotNull(TEXT("The one going has a transition"), Going))
	{
		return false;
	}
	TestTrue(TEXT("The one going hides"), Going->GetInvert() && !Coming->GetInvert());
	TestTrue(TEXT("Both are ink"), Going->GetPreset() == EDreamMaskTransitionPreset::Ink && Coming->GetPreset() == EDreamMaskTransitionPreset::Ink);
	if (Tween != nullptr)
	{
		TestTrue(TEXT("At the start the one going shows and the one coming does not"), Going->IsFullyShown() && Coming->IsFullyHidden());
	}
	TestTrue(TEXT("The one going took the other's settings"), Going->GetSeed() == 5 && FMath::IsNearlyEqual(Going->GetAngle(), 30.0f));
	for (const float Progress : { 0.3f, 0.6f })
	{
		Going->SetProgress(Progress);
		Coming->SetProgress(Progress);
		FMeasure Out, In;
		Out.Prepare(Going);
		In.Prepare(Coming);
		float Worst = 0.0f;
		for (const FVector2f& Point : Grid(12))
		{
			Worst = FMath::Max(Worst, FMath::Abs(Out.At(Point) + In.At(Point) - 1.0f));
		}
		TestTrue(*FString::Printf(TEXT("At %.1f every point is shown by one of the two"), Progress), Worst < 1.0e-3f);
	}
	if (Tween != nullptr)
	{
		UDreamTweenBPLibrary::KillIfIsTweening(To, Tween, true);
	}
	else
	{
		Going->SetProgress(1.0f);
		Coming->SetProgress(1.0f);
	}
	Stage.Frames(1);
	TestTrue(TEXT("Done, the one going is hidden"), Going->IsFullyHidden());
	TestTrue(TEXT("...and the one coming shown, its clip let go"), Coming->IsFullyShown() && !To->IsClipMaskDriven());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamClipMaskReverseTest,
	"DreamGUI.ClipMask.AReversedSourceRunsTheOtherWayAndLeavesTheShapeAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamClipMaskReverseTest::RunTest(const FString& Parameters)
{
	const FVector2f Half(100.0f, 50.0f);
	FDreamClipMask Forward;
	Forward.Source = EDreamClipMaskSource::Procedural;
	Forward.Pattern = EDreamClipMaskPattern::Linear;
	FDreamClipMask Backward = Forward;
	Backward.bReverse = true;
	TestFalse(TEXT("Reverse is part of a mask's identity"), Forward == Backward);
	auto AlphaOf = [&](const FDreamClipMask& InMask, FVector2f P)
	{
		FDreamUIClipLevel Level;
		Level.HalfSize = Half;
		Level.bMasked = FDreamUIClipMaskMath::PackMask(InMask, Half, 0, Level.Mask);
		return FDreamUIClipMaskMath::EvaluateLevel(Level, P, 0.0f, [](FVector2f, FLinearColor&) { return false; });
	};
	for (const float X : { -80.0f, -20.0f, 30.0f, 90.0f })
	{
		TestTrue(*FString::Printf(TEXT("Reversed at %.0f is forward at %.0f"), X, -X), FMath::IsNearlyEqual(AlphaOf(Backward, FVector2f(X, 0.0f)), AlphaOf(Forward, FVector2f(-X, 0.0f)), 1.0e-4f));
	}
	TestEqual(TEXT("Outside the clip rect it still hides"), AlphaOf(Backward, FVector2f(150.0f, 0.0f)), 0.0f);
	FDreamClipMask Shape;
	Shape.Shape = EDreamClipMaskShape::Ellipse;
	FDreamClipMask ShapeReversed = Shape;
	ShapeReversed.bReverse = true;
	FVector4f Packed[DreamUIClipLayout::MaskTexels], PackedReversed[DreamUIClipLayout::MaskTexels];
	FDreamUIClipMaskMath::PackMask(Shape, Half, 0, Packed);
	FDreamUIClipMaskMath::PackMask(ShapeReversed, Half, 0, PackedReversed);
	TestEqual(TEXT("With no source to reverse, the flag is not packed"), Packed[0].W, PackedReversed[0].W);
	FDreamUIClipMaskMath::PackMask(Backward, Half, 0, PackedReversed);
	const uint32 Flags = static_cast<uint32>(PackedReversed[0].W + 0.5f);
	TestTrue(TEXT("With a source it is"), ((Flags >> DreamUIClipLayout::ReverseShift) & 1u) == 1u);
	return true;
}

#endif
