// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Extensions/DreamMaskTransition.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * A mask transition drawn on the frames where it starts or stops driving its widget's clip.
 *
 * Starting or stopping makes the widget's clip anew, and a new clip is given a row of the clip data texture: one never
 * written yet, which clips nothing, or one another clip let go of, which still holds that clip's rect. Whatever row it is
 * given, the frame the widget is first drawn through it must show the clip it was made for. So each test reads every
 * frame from the change on and holds it to the picture from before the change or the one after it: never the subtree
 * unclipped, nor cut away to somebody else's rect.
 *
 * A row let go of is the next one handed out, so a test that wants its new clip to be given a used row first draws a
 * clipper of its own in a corner and then makes it clip no more.
 */
namespace DreamMaskTransitionPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** Frames read after a change: the canvas's worker and the scene render each trail the tick by a frame. */
	static constexpr int32 FramesWatched = 8;
	static constexpr uint8 WhiteTolerance = 8;
	/** Pixels an edge's ramp may put just outside a rect. */
	static constexpr int32 EdgeAllowance = 16;
	/** Grey, drawn the whole time, so that there is something to wait for while the scene shows nothing else. */
	static const FColor MarkerColour = FColor(120, 120, 120, 255);
	static const FVector2D MarkerSize(24.0, 24.0);
	static const FVector2D MarkerPosition(-108.0, -108.0);
	static const FVector2D DecoySize(24.0, 24.0);
	static const FVector2D DecoyPosition(108.0, 108.0);

	/** InSize at InPosition -- its centre from the stage's, +Y up -- in the stage's pixels, one to a unit, grown by InGrow. */
	FIntRect PixelRect(FIntPoint InStageSize, FVector2D InSize, FVector2D InPosition, int32 InGrow)
	{
		const int32 MinX = FMath::RoundToInt(InStageSize.X * 0.5 + InPosition.X - InSize.X * 0.5) - InGrow;
		const int32 MinY = FMath::RoundToInt(InStageSize.Y * 0.5 - InPosition.Y - InSize.Y * 0.5) - InGrow;
		const int32 MaxX = FMath::RoundToInt(InStageSize.X * 0.5 + InPosition.X + InSize.X * 0.5) + InGrow;
		const int32 MaxY = FMath::RoundToInt(InStageSize.Y * 0.5 - InPosition.Y + InSize.Y * 0.5) + InGrow;
		return FIntRect(FMath::Clamp(MinX, 0, InStageSize.X), FMath::Clamp(MinY, 0, InStageSize.Y),
			FMath::Clamp(MaxX, 0, InStageSize.X), FMath::Clamp(MaxY, 0, InStageSize.Y));
	}

	/** White pixels inside a rect, shrunk by two, and outside it, grown by two: an edge's ramp is in neither. */
	struct FWhite
	{
		int32 Inside = 0;
		int32 Outside = 0;
		int32 Area = 0;
		float InsideShare() const { return Area > 0 ? static_cast<float>(Inside) / Area : 0.0f; }
	};

	FWhite CountWhite(const TArray<FColor>& InPixels, FIntPoint InSize, FVector2D InRectSize, FVector2D InRectPosition)
	{
		const FIntRect Inner = PixelRect(InSize, InRectSize, InRectPosition, -2);
		const FIntRect Outer = PixelRect(InSize, InRectSize, InRectPosition, 2);
		FWhite White;
		White.Area = Inner.Area();
		White.Inside = FDreamPixelProbe::CountColor(InPixels, InSize, Inner, FColor::White, WhiteTolerance);
		White.Outside = FDreamPixelProbe::CountColor(InPixels, InSize, FIntRect(0, 0, InSize.X, InSize.Y), FColor::White, WhiteTolerance)
			- FDreamPixelProbe::CountColor(InPixels, InSize, Outer, FColor::White, WhiteTolerance);
		return White;
	}

	FString Describe(const FWhite& InWhite)
	{
		return FString::Printf(TEXT("%d of %d pixels white inside (%.0f%%), %d outside"), InWhite.Inside, InWhite.Area, InWhite.InsideShare() * 100.0f, InWhite.Outside);
	}

	/** The marker, the one thing every scene here draws from its first frame. */
	void AddMarker(FGalleryStage& InStage)
	{
		InStage.AddBlock(TEXT("Marker"), MarkerSize, MarkerPosition, MarkerColour);
	}

	/** A clipper in the corner, nothing in it: drawn until its row is written, then let go of (EnqueueLetGoOfDecoy). */
	UDreamWidget* AddDecoy(FGalleryStage& InStage)
	{
		UDreamWidget* Decoy = InStage.AddWidget(TEXT("Decoy"), DecoySize, DecoyPosition);
		Decoy->SetClipping(EDreamWidgetClipping::ClipToBounds);
		return Decoy;
	}

	/** A mask transition on InWidget, hard-edged so that a share of the rect reads as a share of the pixels. */
	UDreamMaskTransition* AddTransition(UDreamWidget* InWidget, float InProgress)
	{
		UDreamMaskTransition* Transition = InWidget->AddComponent<UDreamMaskTransition>();
		Transition->SetPreset(EDreamMaskTransitionPreset::Wipe);
		Transition->SetSoftness(0.0f);
		Transition->SetProgress(InProgress);
		return Transition;
	}

	/** Drawn -- the marker at least -- then still. */
	void EnqueueSettle(const FStageRef& InStage)
	{
		EnqueueFrames(InStage, 3);
		EnqueueFramesUntilDrawn(InStage, static_cast<int32>(MarkerSize.X * MarkerSize.Y * 0.8));
		EnqueueFramesUntilStable(InStage);
	}

	/** The decoy stops clipping: its row, written with its rect, goes back to be handed out next. */
	void EnqueueLetGoOfDecoy(const FStageRef& InStage, UDreamWidget* InDecoy)
	{
		TWeakObjectPtr<UDreamWidget> Decoy = InDecoy;
		EnqueueDo([Decoy]()
		{
			if (UDreamWidget* Widget = Decoy.Get())
			{
				Widget->SetClipping(EDreamWidgetClipping::Inherit);
			}
		});
		EnqueueSettle(InStage);
	}

	/** InChange, and a redraw asked for in the same frame, so that the frame it is drawn in is drawn. */
	void EnqueueChange(const FStageRef& InStage, TFunction<void()> InChange)
	{
		EnqueueDo([InStage, InChange]()
		{
			InChange();
			InStage->RequestRedraw();
		});
	}

	/**
	 * FramesWatched frames from the change on, a redraw asked for on each: every picture read must pass InCheck, which says
	 * what is wrong with one or nothing. The first that does not fails the test, saved as <InName>_Frame<n>.
	 */
	void EnqueueEveryFrame(const FStageRef& InStage, const FString& InName, TFunction<FString(const TArray<FColor>&, FIntPoint)> InCheck)
	{
		TSharedRef<int32> Frame = MakeShared<int32>(0);
		EnqueueStep([InStage, InName, InCheck, Frame]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (!InStage->ReadBack(Pixels, Size))
			{
				InStage->GetTest().AddError(FString::Printf(TEXT("%s: frame %d after the change did not read back."), *InName, *Frame));
				return true;
			}
			const FString Wrong = InCheck(Pixels, Size);
			if (!Wrong.IsEmpty())
			{
				const FString CaptureName = FString::Printf(TEXT("%s_Frame%d"), *InName, *Frame);
				FDreamPixelProbe::SaveCapture(Pixels, Size, CaptureName);
				InStage->GetTest().AddError(FString::Printf(TEXT("%s: frame %d after the change is wrong: %s (saved as %s)."), *InName, *Frame, *Wrong, *CaptureName));
				return true;
			}
			if (++(*Frame) >= FramesWatched)
			{
				return true;
			}
			InStage->RequestRedraw();
			return false;
		});
	}

	/** Once the scene has settled after the change: the picture it ends on must pass InCheck. */
	void EnqueueEndsOn(const FStageRef& InStage, const FString& InName, TFunction<FString(const TArray<FColor>&, FIntPoint)> InCheck)
	{
		EnqueueSettle(InStage);
		EnqueueDo([InStage, InName, InCheck]()
		{
			TArray<FColor> Pixels;
			FIntPoint Size = FIntPoint::ZeroValue;
			if (!InStage->GetTest().TestTrue(FString::Printf(TEXT("%s: the settled picture reads back"), *InName), InStage->ReadBack(Pixels, Size)))
			{
				return;
			}
			FDreamPixelProbe::SaveCapture(Pixels, Size, InName);
			const FString Wrong = InCheck(Pixels, Size);
			InStage->GetTest().TestTrue(FString::Printf(TEXT("%s: the picture it settles on is right%s%s"), *InName, Wrong.IsEmpty() ? TEXT("") : TEXT(": "), *Wrong), Wrong.IsEmpty());
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionPixelShownHiddenTest,
	"DreamGUI.RHI.MaskTransition.AWidgetShownAtNoProgressDrawsNothingOfItOnTheFrameItIsShown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamMaskTransitionPixelShownHiddenTest::RunTest(const FString& Parameters)
{
	using namespace DreamMaskTransitionPixelTestLocal;
	/**
	 * A collapsed page, its transition fully shown and so driving nothing, is made visible and put at no progress in one
	 * frame: the transition starts driving the page's clip on the frame the page is first drawn. Its clip is given a row
	 * never written, which clips nothing -- the whole page must still not show, then or after.
	 */
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	AddMarker(*Stage);
	const FVector2D PageSize(120.0, 120.0);
	UDreamWidget* Page = Stage->AddWidget(TEXT("Page"), PageSize, FVector2D::ZeroVector);
	Stage->AddBlock(TEXT("PageContent"), PageSize, FVector2D::ZeroVector, FColor::White, nullptr, Page);
	UDreamMaskTransition* Transition = AddTransition(Page, 1.0f);
	Page->SetVisibility(EDreamWidgetVisibility::Collapsed);
	EnqueueSettle(Stage);
	TWeakObjectPtr<UDreamWidget> WeakPage = Page;
	TWeakObjectPtr<UDreamMaskTransition> WeakTransition = Transition;
	EnqueueChange(Stage, [WeakPage, WeakTransition]()
	{
		if (WeakPage.IsValid() && WeakTransition.IsValid())
		{
			WeakPage->SetVisibility(EDreamWidgetVisibility::Visible);
			WeakTransition->SetProgress(0.0f);
		}
	});
	auto NothingOfThePage = [PageSize](const TArray<FColor>& InPixels, FIntPoint InSize) -> FString
	{
		const FWhite White = CountWhite(InPixels, InSize, PageSize, FVector2D::ZeroVector);
		return White.Inside + White.Outside > EdgeAllowance ? FString::Printf(TEXT("the page shows: %s"), *Describe(White)) : FString();
	};
	EnqueueEveryFrame(Stage, TEXT("MaskTransition_ShownAtNoProgress"), NothingOfThePage);
	EnqueueEndsOn(Stage, TEXT("MaskTransition_ShownAtNoProgress"), NothingOfThePage);
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionPixelStartTest,
	"DreamGUI.RHI.MaskTransition.TheFrameATransitionStartsDrivingAClipDrawsTheWidgetThroughItsMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamMaskTransitionPixelStartTest::RunTest(const FString& Parameters)
{
	using namespace DreamMaskTransitionPixelTestLocal;
	/**
	 * A page fully shown, inside a clipper of its size, its content larger than both: what shows is the clipper's rect.
	 * Wiped back to half, the transition starts driving the page's clip, which is given the row the decoy let go of. Every
	 * frame from then on shows the clipper's rect whole or half of it: never nothing (the decoy's rect, in the corner),
	 * nor the content spilling past the clipper (a row that clips nothing).
	 */
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	AddMarker(*Stage);
	UDreamWidget* Decoy = AddDecoy(*Stage);
	const FVector2D PageSize(120.0, 120.0);
	UDreamWidget* Frame = Stage->AddWidget(TEXT("Frame"), PageSize, FVector2D::ZeroVector);
	Frame->SetClipping(EDreamWidgetClipping::ClipToBounds);
	UDreamWidget* Page = Stage->AddWidget(TEXT("Page"), PageSize, FVector2D::ZeroVector, Frame);
	Stage->AddBlock(TEXT("PageContent"), FVector2D(180.0, 180.0), FVector2D::ZeroVector, FColor::White, nullptr, Page);
	UDreamMaskTransition* Transition = AddTransition(Page, 1.0f);
	EnqueueSettle(Stage);
	EnqueueLetGoOfDecoy(Stage, Decoy);
	TWeakObjectPtr<UDreamMaskTransition> WeakTransition = Transition;
	EnqueueChange(Stage, [WeakTransition]()
	{
		if (WeakTransition.IsValid())
		{
			WeakTransition->SetProgress(0.5f);
		}
	});
	EnqueueEveryFrame(Stage, TEXT("MaskTransition_StartsDriving"), [PageSize](const TArray<FColor>& InPixels, FIntPoint InSize) -> FString
	{
		const FWhite White = CountWhite(InPixels, InSize, PageSize, FVector2D::ZeroVector);
		if (White.InsideShare() < 0.35f)
		{
			return FString::Printf(TEXT("the page is cut away: %s"), *Describe(White));
		}
		if (White.Outside > EdgeAllowance)
		{
			return FString::Printf(TEXT("the page is not clipped: %s"), *Describe(White));
		}
		return FString();
	});
	EnqueueEndsOn(Stage, TEXT("MaskTransition_StartsDriving"), [PageSize](const TArray<FColor>& InPixels, FIntPoint InSize) -> FString
	{
		const FWhite White = CountWhite(InPixels, InSize, PageSize, FVector2D::ZeroVector);
		return White.InsideShare() < 0.4f || White.InsideShare() > 0.6f || White.Outside > EdgeAllowance
			? FString::Printf(TEXT("half the page should show: %s"), *Describe(White)) : FString();
	});
	EnqueueTearDown(Stage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMaskTransitionPixelStopTest,
	"DreamGUI.RHI.MaskTransition.TheFrameATransitionStopsDrivingAClipDrawsTheWidgetThroughItsOwnBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamMaskTransitionPixelStopTest::RunTest(const FString& Parameters)
{
	using namespace DreamMaskTransitionPixelTestLocal;
	/**
	 * A page that clips to its bounds, half wiped in, its content larger than it. Shown fully, the transition lets go of
	 * the page's clip, which is made again -- given the row the decoy let go of. Every frame from then on shows half the
	 * page's rect or all of it: never nothing, nor the content spilling past the page.
	 */
	FStageRef Stage = BeginStage(*this);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	AddMarker(*Stage);
	UDreamWidget* Decoy = AddDecoy(*Stage);
	const FVector2D PageSize(120.0, 120.0);
	UDreamWidget* Page = Stage->AddWidget(TEXT("Page"), PageSize, FVector2D::ZeroVector);
	Page->SetClipping(EDreamWidgetClipping::ClipToBounds);
	Stage->AddBlock(TEXT("PageContent"), FVector2D(180.0, 180.0), FVector2D::ZeroVector, FColor::White, nullptr, Page);
	UDreamMaskTransition* Transition = AddTransition(Page, 0.5f);
	EnqueueSettle(Stage);
	EnqueueLetGoOfDecoy(Stage, Decoy);
	TWeakObjectPtr<UDreamMaskTransition> WeakTransition = Transition;
	EnqueueChange(Stage, [WeakTransition]()
	{
		if (WeakTransition.IsValid())
		{
			WeakTransition->SetProgress(1.0f);
		}
	});
	EnqueueEveryFrame(Stage, TEXT("MaskTransition_StopsDriving"), [PageSize](const TArray<FColor>& InPixels, FIntPoint InSize) -> FString
	{
		const FWhite White = CountWhite(InPixels, InSize, PageSize, FVector2D::ZeroVector);
		if (White.InsideShare() < 0.35f)
		{
			return FString::Printf(TEXT("the page is cut away: %s"), *Describe(White));
		}
		if (White.Outside > EdgeAllowance)
		{
			return FString::Printf(TEXT("the page is not clipped: %s"), *Describe(White));
		}
		return FString();
	});
	EnqueueEndsOn(Stage, TEXT("MaskTransition_StopsDriving"), [PageSize](const TArray<FColor>& InPixels, FIntPoint InSize) -> FString
	{
		const FWhite White = CountWhite(InPixels, InSize, PageSize, FVector2D::ZeroVector);
		return White.InsideShare() < 0.95f || White.Outside > EdgeAllowance
			? FString::Printf(TEXT("all of the page and nothing past it should show: %s"), *Describe(White)) : FString();
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif
