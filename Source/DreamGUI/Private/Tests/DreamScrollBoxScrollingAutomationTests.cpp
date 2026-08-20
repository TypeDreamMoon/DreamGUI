// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"

/*
 * Scroll box sizing semantics: the scroll-axis desired size must exclude content. Reporting the
 * content extent let any Auto-measuring ancestor inflate the viewport to fit everything — which is
 * unscrollable by construction, and made the designer and PIE disagree wherever the surrounding
 * space differed (the sidebar band ballooning to 994px was exactly this).
 */

namespace DreamScrollBoxScrollingTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxDesiredSizeExcludesContentTest,
	"DreamGUI.Layout.ScrollBox.DesiredSizeExcludesContentOnScrollAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxDesiredSizeExcludesContentTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScopedGameWorld TestWorld;
	UDreamWidget* Outer = MakeWidget(TestWorld.World, nullptr, TEXT("Outer"), 300.0f, 400.0f);
	UDreamWidget* ScrollWidget = MakeWidget(TestWorld.World, Outer, TEXT("Scroll"), 200.0f, 120.0f);
	UDreamPanelLayoutBase* OuterPanel = Outer->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>();
	UDreamLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
	TestNotNull(TEXT("Outer panel created"), OuterPanel);
	TestNotNull(TEXT("ScrollBox created"), ScrollBox);
	for (int32 i = 0; i < 3; i++)
	{
		MakeWidget(TestWorld.World, ScrollWidget, *FString::Printf(TEXT("Block%d"), i), 180.0f, 100.0f);
	}
	Outer->OnRegister();
	ScrollWidget->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(Outer);
	UDreamWidget::RebuildLayoutImmediately(Outer);

	// 3 x 100 of content must NOT leak into the scroll-axis desired size. With the panel reporting
	// only padding there, measurement falls back to the widget's AUTHORED height — the designer's
	// viewport size — so an Auto parent grants exactly what was authored instead of inflating the
	// viewport to fit all content. The cross axis measures content like a normal stack.
	const FVector2D Desired = OuterPanel->GetDesiredSize(ScrollWidget);
	TestEqual(TEXT("Scroll-axis desired size is the authored viewport, not content"), Desired.Y, 120.0);
	TestEqual(TEXT("Cross-axis desired size measures content"), Desired.X, 180.0);

	Outer->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxScrollRangeTest,
	"DreamGUI.Layout.ScrollBox.ScrollRangeAndClamping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxScrollRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScopedGameWorld TestWorld;
	// Viewport authored at 120 tall, content 3 x 100: scrollable range must be exactly 180.
	UDreamWidget* ScrollWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Scroll"), 200.0f, 120.0f);
	UDreamLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
	TestNotNull(TEXT("ScrollBox created"), ScrollBox);
	for (int32 i = 0; i < 3; i++)
	{
		MakeWidget(TestWorld.World, ScrollWidget, *FString::Printf(TEXT("Block%d"), i), 180.0f, 100.0f);
	}
	ScrollWidget->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);

	TestEqual(TEXT("Max scroll = content minus viewport"), ScrollBox->GetMaxScrollOffset(), 180.0f);
	ScrollBox->SetScrollOffset(10000.0f);
	TestEqual(TEXT("Offset clamps to max"), ScrollBox->GetScrollOffset(), 180.0f);
	TestTrue(TEXT("ScrollBy moves within range"), ScrollBox->ScrollBy(-140.0f));
	TestEqual(TEXT("ScrollBy lands exactly"), ScrollBox->GetScrollOffset(), 40.0f);
	ScrollBox->ScrollBy(-10000.0f);
	TestEqual(TEXT("Offset clamps to zero"), ScrollBox->GetScrollOffset(), 0.0f);
	TestFalse(TEXT("ScrollBy at the limit reports no movement"), ScrollBox->ScrollBy(-1.0f));

	ScrollWidget->DestroyWidget();
	return true;
}

namespace DreamScrollBoxScrollingTestLocal
{
	/** Viewport 120 tall over 3 x 100 of content: scrollable range 180, view fraction 120/300. */
	struct FScrollFixture
	{
		FScopedGameWorld TestWorld;
		UDreamWidget* ScrollWidget = nullptr;
		UDreamLayoutContainerScrollBox* ScrollBox = nullptr;
		TArray<UDreamWidget*> Blocks;

		FScrollFixture()
		{
			ScrollWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Scroll"), 200.0f, 120.0f);
			ScrollBox = ScrollWidget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
			for (int32 i = 0; i < 3; i++)
			{
				Blocks.Add(MakeWidget(TestWorld.World, ScrollWidget, *FString::Printf(TEXT("Block%d"), i), 180.0f, 100.0f));
			}
		}
		void Arrange()
		{
			ScrollWidget->OnRegister();
			UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
			UDreamWidget::RebuildLayoutImmediately(ScrollWidget);
		}
		~FScrollFixture() { if (ScrollWidget) { ScrollWidget->DestroyWidget(); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxOffsetBeforeLayoutTest,
	"DreamGUI.Layout.ScrollBox.OffsetRequestedBeforeLayoutSurvives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxOffsetBeforeLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;

	// MaxScrollOffset is only computed by a layout pass, so clamping a request against it before
	// that pass silently turns every offset into 0 -- the trap UMG sidesteps by deferring its clamp.
	// Anything setting a scroll position from BeginPlay or a constructor hits exactly this.
	Fixture.ScrollBox->SetScrollOffset(90.0f);
	TestEqual(TEXT("An offset requested before layout is not swallowed"), Fixture.ScrollBox->GetScrollOffset(), 90.0f);

	Fixture.Arrange();
	TestEqual(TEXT("...and survives the first layout pass"), Fixture.ScrollBox->GetScrollOffset(), 90.0f);
	TestEqual(TEXT("Range is content minus viewport"), Fixture.ScrollBox->GetMaxScrollOffset(), 180.0f);

	// Once the metrics exist the clamp is real again, which is what lets a nested box hand over.
	Fixture.ScrollBox->SetScrollOffset(9999.0f);
	TestEqual(TEXT("After layout an over-large offset clamps to the end"), Fixture.ScrollBox->GetScrollOffset(), 180.0f);
	TestFalse(TEXT("Scrolling past the end reports no movement"), Fixture.ScrollBox->ScrollBy(50.0f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxViewFractionsTest,
	"DreamGUI.Layout.ScrollBox.ViewFractionsDescribeTheVisibleWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxViewFractionsTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();

	// 120 of 300 visible. These two are what a scrollbar thumb needs: how big it is, and where.
	TestEqual(TEXT("View fraction is viewport over content"), Fixture.ScrollBox->GetViewFraction(), 0.4f, 0.001f);
	TestEqual(TEXT("At the start the offset fraction is 0"), Fixture.ScrollBox->GetViewOffsetFraction(), 0.0f, 0.001f);

	Fixture.ScrollBox->ScrollToEnd();
	TestEqual(TEXT("ScrollToEnd lands on the end offset"), Fixture.ScrollBox->GetScrollOffset(), 180.0f);
	TestEqual(TEXT("GetScrollOffsetOfEnd agrees with the range"), Fixture.ScrollBox->GetScrollOffsetOfEnd(), 180.0f);
	TestEqual(TEXT("At the end the offset fraction is 1"), Fixture.ScrollBox->GetViewOffsetFraction(), 1.0f, 0.001f);

	Fixture.ScrollBox->ScrollToStart();
	TestEqual(TEXT("ScrollToStart returns to zero"), Fixture.ScrollBox->GetScrollOffset(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxScrollIntoViewTest,
	"DreamGUI.Layout.ScrollBox.ScrollWidgetIntoViewMovesTheMinimumDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxScrollIntoViewTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();

	// Block0 occupies content 0..100 and the view is 0..120, so it is already whole on screen.
	TestFalse(TEXT("A fully visible widget does not scroll"), Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.Blocks[0], false));

	// Block2 occupies 200..300. Bringing its trailing edge to the bottom of a 120 view means 180.
	TestTrue(TEXT("A widget below the view scrolls into it"), Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.Blocks[2], false));
	TestEqual(TEXT("...by the minimum distance, not by centring it"), Fixture.ScrollBox->GetScrollOffset(), 180.0f);

	// Block0 is now above the view; its leading edge comes back to the top.
	TestTrue(TEXT("A widget above the view scrolls back to it"), Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.Blocks[0], false));
	TestEqual(TEXT("...landing on its leading edge"), Fixture.ScrollBox->GetScrollOffset(), 0.0f);

	// A descendant resolves to the child that owns the slot, so callers need not know the nesting.
	// Giving Block2 a child also changes what Block2 measures as -- a widget with no layout container
	// falls back to measuring its content -- so the content extent, and with it the target offset, is
	// no longer 180. Pinning that number would be asserting the fixture rather than the behaviour, so
	// what is checked is the invariant: descendant and owning child scroll to the same place.
	UDreamWidget* Nested = MakeWidget(Fixture.TestWorld.World, Fixture.Blocks[2], TEXT("Nested"), 40.0f, 20.0f);
	Fixture.Arrange();
	Fixture.ScrollBox->ScrollToStart();
	Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.Blocks[2], false);
	const float OffsetViaChild = Fixture.ScrollBox->GetScrollOffset();
	Fixture.ScrollBox->ScrollToStart();
	TestTrue(TEXT("A descendant scrolls its owning child into view"), Fixture.ScrollBox->ScrollWidgetIntoView(Nested, false));
	TestEqual(TEXT("...to the same place as its owning child"), Fixture.ScrollBox->GetScrollOffset(), OffsetViaChild);
	TestTrue(TEXT("...and that was a real scroll, not a coincidental no-op"), OffsetViaChild > 0.0f);

	TestFalse(TEXT("A widget outside this box reports no scroll"),
		Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.ScrollWidget, false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxInertiaTest,
	"DreamGUI.Layout.ScrollBox.MomentumDecaysAndStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxInertiaTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();
	Fixture.ScrollBox->bAllowOverscroll = false;//isolate momentum from the rubber band

	// One step by hand: alpha = DecelerationRate * 50 * dt = 0.135 * 50 / 60 = 0.1125, so a velocity
	// of 300 decays to 300 * (1 - 0.1125) = 266.25 and the content moves 266.25/60 = 4.4375 units.
	const float Step = 1.0f / 60.0f;
	Fixture.ScrollBox->SetScrollVelocity(300.0f);
	Fixture.ScrollBox->TickScrollPhysics(Step);
	TestEqual(TEXT("Velocity decays by the deceleration rate"), Fixture.ScrollBox->GetScrollVelocity(), 266.25f, 0.01f);
	TestEqual(TEXT("The decayed velocity is what moves the content"), Fixture.ScrollBox->GetScrollOffset(), 4.4375f, 0.01f);
	TestTrue(TEXT("The box reports itself as scrolling"), Fixture.ScrollBox->IsScrolling());

	// Left alone it must come to rest rather than creeping forever.
	for (int32 i = 0; i < 600; i++)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestFalse(TEXT("Momentum eventually stops"), Fixture.ScrollBox->IsScrolling());
	TestEqual(TEXT("...with no velocity left"), Fixture.ScrollBox->GetScrollVelocity(), 0.0f);

	// Deceleration of zero is documented as never slowing down; it must still respect the end.
	Fixture.ScrollBox->DecelerationRate = 0.0f;
	Fixture.ScrollBox->SetScrollVelocity(10000.0f);
	for (int32 i = 0; i < 120; i++)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestEqual(TEXT("Momentum never scrolls past the end"), Fixture.ScrollBox->GetScrollOffset(), 180.0f);
	TestFalse(TEXT("Hitting the end with overscroll off kills the momentum"), Fixture.ScrollBox->IsScrolling());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxOverscrollTest,
	"DreamGUI.Layout.ScrollBox.OverscrollIsBoundedAndSpringsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxOverscrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();
	Fixture.ScrollBox->OverscrollLimit = 100.0f;

	// Dragging past the start rubber-bands instead of stopping dead, and the scroll POSITION stays
	// in range -- only the displayed content is pulled out.
	Fixture.ScrollBox->ApplyDragDelta(-50.0f);
	TestEqual(TEXT("The scroll position stays at the start"), Fixture.ScrollBox->GetScrollOffset(), 0.0f);
	// The first move past the end answers one-for-one: at an empty band the headroom is full, and a
	// finger that has just crossed the edge should not feel it stick. Resistance appears from there.
	TestEqual(TEXT("Crossing the end answers one-for-one"), Fixture.ScrollBox->GetOverscroll(), -50.0f, 0.01f);
	const float AfterFirst = FMath::Abs(Fixture.ScrollBox->GetOverscroll());
	Fixture.ScrollBox->ApplyDragDelta(-50.0f);
	TestTrue(TEXT("Pulling the same distance again moves the band less"),
		FMath::Abs(Fixture.ScrollBox->GetOverscroll()) - AfterFirst < 50.0f);

	// The band saturates: ten times the raw pull is nowhere near ten times the displacement, and it
	// can never exceed the limit. The legacy view's flat damping had no bound here at all.
	Fixture.ScrollBox->StopScrolling();
	for (int32 i = 0; i < 40; i++)
	{
		Fixture.ScrollBox->ApplyDragDelta(-60.0f);//keep pulling out against rising resistance
	}
	const float FarPull = Fixture.ScrollBox->GetOverscroll();
	TestTrue(TEXT("A sustained pull never exceeds the limit"), FMath::Abs(FarPull) <= 100.0f);
	TestTrue(TEXT("...and does approach it, so resistance rises rather than stopping dead"), FMath::Abs(FarPull) > 90.0f);
	// Coming back answers one-for-one: a pull of the band's own size closes it and reaches content.
	Fixture.ScrollBox->ApplyDragDelta(FMath::Abs(FarPull) + 20.0f);
	TestTrue(TEXT("Dragging back closes the band immediately rather than spending a backlog"),
		FMath::IsNearlyZero(Fixture.ScrollBox->GetOverscroll()));

	// Releasing springs back to exactly the end and settles.
	Fixture.ScrollBox->StopScrolling();
	Fixture.ScrollBox->ScrollToStart();
	Fixture.ScrollBox->ApplyDragDelta(-60.0f);
	const float Step = 1.0f / 60.0f;
	for (int32 i = 0; i < 300; i++)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestEqual(TEXT("The band closes completely"), Fixture.ScrollBox->GetOverscroll(), 0.0f);
	TestFalse(TEXT("...and the box comes to rest"), Fixture.ScrollBox->IsScrolling());
	TestEqual(TEXT("...back at the start it was pulled from"), Fixture.ScrollBox->GetScrollOffset(), 0.0f);

	// The spring must not overshoot back through the end on the way home: sampling every step, the
	// displacement may only shrink in magnitude, never flip sign.
	Fixture.ScrollBox->ApplyDragDelta(-60.0f);
	float Previous = FMath::Abs(Fixture.ScrollBox->GetOverscroll());
	bool bMonotonic = true;
	for (int32 i = 0; i < 200; i++)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
		const float Current = Fixture.ScrollBox->GetOverscroll();
		if (Current > KINDA_SMALL_NUMBER || FMath::Abs(Current) > Previous + 0.001f)
		{
			bMonotonic = false;
			break;
		}
		Previous = FMath::Abs(Current);
	}
	TestTrue(TEXT("The spring-back never overshoots into an oscillation"), bMonotonic);

	// A held pointer owns the offset: the spring must not run underneath a drag in progress. It did,
	// and because any non-zero band swallows the whole drag delta, the content stopped advancing
	// while the band was pulled shut -- the gesture read as "moves a little, then snaps back" with
	// the button still down.
	Fixture.ScrollBox->StopScrolling();
	Fixture.ScrollBox->SetDragging(true);
	Fixture.ScrollBox->ApplyDragDelta(-40.0f);
	const float HeldBand = Fixture.ScrollBox->GetOverscroll();
	TestTrue(TEXT("Dragging past the end opens the band"), FMath::Abs(HeldBand) > 1.0f);
	for (int32 i = 0; i < 30; i++)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestEqual(TEXT("Ticking while the pointer is down does not close the band"),
		Fixture.ScrollBox->GetOverscroll(), HeldBand, 0.001f);

	// Dragging back out of the band has to respond straight away. The undamped pull used to be
	// unbounded, so a long drag banked thousands of units that the return drag had to unwind before
	// the content moved at all -- with physics suspended under the drag, that read as the gesture
	// having died. Bounding the raw keeps the two directions symmetric.
	// Letting go hands it back to the spring.
	Fixture.ScrollBox->StopScrolling();
	Fixture.ScrollBox->SetDragging(true);
	Fixture.ScrollBox->ApplyDragDelta(-40.0f);
	Fixture.ScrollBox->SetDragging(false);
	for (int32 i = 0; i < 300; i++)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestEqual(TEXT("Releasing lets the band close"), Fixture.ScrollBox->GetOverscroll(), 0.0f);

	// With overscroll switched off the drag simply stops at the end.
	Fixture.ScrollBox->StopScrolling();
	Fixture.ScrollBox->bAllowOverscroll = false;
	Fixture.ScrollBox->ApplyDragDelta(-50.0f);
	TestEqual(TEXT("With overscroll off nothing is pulled past the end"), Fixture.ScrollBox->GetOverscroll(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxBarFractionsTest,
	"DreamGUI.Layout.ScrollBox.BarFractionsStaySafeWhenEverythingFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxBarFractionsTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	// A viewport taller than its content: nothing to scroll, and the pair of numbers a scrollbar
	// consumes has to stay usable anyway. A Size of exactly 1 leaves UUIScrollbar's slide area at
	// zero and its drag maths divides by it, so the clamp the sync applies is worth pinning here
	// where it can be checked without a bar, a handle hierarchy or a pointer.
	FScopedGameWorld TestWorld;
	UDreamWidget* ScrollWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Scroll"), 200.0f, 400.0f);
	UDreamLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
	MakeWidget(TestWorld.World, ScrollWidget, TEXT("Small"), 180.0f, 100.0f);
	ScrollWidget->OnRegister();
	UDreamWidget::MarkLayoutForRebuild(ScrollWidget);
	UDreamWidget::RebuildLayoutImmediately(ScrollWidget);

	TestEqual(TEXT("Nothing can scroll"), ScrollBox->GetMaxScrollOffset(), 0.0f);
	TestEqual(TEXT("The whole content is in view"), ScrollBox->GetViewFraction(), 1.0f, 0.001f);
	TestEqual(TEXT("There is nowhere to be, so the offset fraction is zero"),
		ScrollBox->GetViewOffsetFraction(), 0.0f, 0.001f);
	// Both are finite and inside [0,1]; a bar fed these can compute a slide area without dividing
	// by zero once the sync's clamp trims the size.
	TestTrue(TEXT("Both fractions are finite"),
		FMath::IsFinite(ScrollBox->GetViewFraction()) && FMath::IsFinite(ScrollBox->GetViewOffsetFraction()));
	TestTrue(TEXT("A trimmed size leaves a usable slide area"),
		1.0f - FMath::Min(ScrollBox->GetViewFraction(), 1.0f - KINDA_SMALL_NUMBER) > 0.0f);

	ScrollWidget->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxStaleBandTest,
	"DreamGUI.Layout.ScrollBox.ABandStrandedMidRangeDoesNotEatTheDrag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxStaleBandTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();
	Fixture.ScrollBox->OverscrollLimit = 100.0f;

	// The live frame trace's exact shape: a band left over from an earlier gesture while the offset
	// sits mid-range. Same-signed drags then read as "pushing out" and the whole gesture vanished
	// into the band with the offset frozen -- at offset 74 of 4198 in the reported session.
	Fixture.ScrollBox->SetDragging(true);
	Fixture.ScrollBox->ApplyDragDelta(99999.0f);//pin to the end and open the band
	TestEqual(TEXT("Fixture: offset pinned at the end"), Fixture.ScrollBox->GetScrollOffset(), 180.0f);
	TestTrue(TEXT("Fixture: the band is open"), FMath::Abs(Fixture.ScrollBox->GetOverscroll()) > 1.0f);
	// Strand the band: a code-driven move puts the offset mid-range without touching the band,
	// standing in for the spring residue a fresh grab interrupts.
	Fixture.ScrollBox->SetScrollOffset(50.0f);

	const float Before = Fixture.ScrollBox->GetScrollOffset();
	Fixture.ScrollBox->ApplyDragDelta(10.0f);//same sign as the stranded band
	TestEqual(TEXT("A drag against a stranded band moves the offset"), Fixture.ScrollBox->GetScrollOffset(), Before + 10.0f);
	TestEqual(TEXT("...and the stranded band is folded away"), Fixture.ScrollBox->GetOverscroll(), 0.0f);

	// The genuine article still works: at the actual end, the same drag rubber-bands.
	Fixture.ScrollBox->StopScrolling();
	Fixture.ScrollBox->SetScrollOffset(180.0f);
	Fixture.ScrollBox->ApplyDragDelta(50.0f);
	TestTrue(TEXT("At the real end the same drag still opens the band"),
		Fixture.ScrollBox->GetOverscroll() > 1.0f);
	TestEqual(TEXT("...with the offset still pinned there"), Fixture.ScrollBox->GetScrollOffset(), 180.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxAnimatedScrollTest,
	"DreamGUI.Layout.ScrollBox.AnimatedScrollEasesToItsTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxAnimatedScrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();
	const float Step = 1.0f / 60.0f;

	// Asking for an eased scroll must not move anything this instant -- that is the whole difference
	// from SetScrollOffset, and the thing a caller relies on when it wants the reader to keep place.
	Fixture.ScrollBox->SetScrollOffsetAnimated(100.0f);
	TestEqual(TEXT("An eased scroll does not move the offset immediately"), Fixture.ScrollBox->GetScrollOffset(), 0.0f);
	TestTrue(TEXT("...and reports itself as animating"), Fixture.ScrollBox->IsAnimatingScroll());
	TestEqual(TEXT("...towards the requested target"), Fixture.ScrollBox->GetAnimatedScrollTarget(), 100.0f);

	// FInterpTo covers Dist * clamp(dt * speed, 0, 1) each step: 100 * (15/60) = 25 on the first.
	Fixture.ScrollBox->TickScrollPhysics(Step);
	TestEqual(TEXT("The first step covers the interpolation speed's share"), Fixture.ScrollBox->GetScrollOffset(), 25.0f, 0.01f);

	for (int32 i = 0; i < 240; i++)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestEqual(TEXT("It lands exactly on the target"), Fixture.ScrollBox->GetScrollOffset(), 100.0f, 0.001f);
	TestFalse(TEXT("...and stops animating"), Fixture.ScrollBox->IsAnimatingScroll());
	TestFalse(TEXT("...leaving the box at rest"), Fixture.ScrollBox->IsScrolling());

	// The target is clamped like any other offset, so an eased scroll cannot park past the end.
	Fixture.ScrollBox->SetScrollOffsetAnimated(9999.0f);
	TestEqual(TEXT("An eased target clamps to the range"), Fixture.ScrollBox->GetAnimatedScrollTarget(), 180.0f);

	// A drag beats an animation: grabbing the content must not leave it drifting to an old target.
	Fixture.ScrollBox->ApplyDragDelta(-10.0f);
	TestFalse(TEXT("A drag cancels an eased scroll in flight"), Fixture.ScrollBox->IsAnimatingScroll());

	// Momentum and easing must not both drive the offset; starting an ease drops the velocity.
	Fixture.ScrollBox->SetScrollVelocity(500.0f);
	Fixture.ScrollBox->SetScrollOffsetAnimated(0.0f);
	TestEqual(TEXT("Starting an eased scroll drops any momentum"), Fixture.ScrollBox->GetScrollVelocity(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxEaseCurveScrollTest,
	"DreamGUI.Layout.ScrollBox.EaseCurveScrollFollowsTheCurveOverItsDuration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxEaseCurveScrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();
	const float Step = 1.0f / 60.0f;

	// Linear is the one curve whose position at any moment can be stated without reproducing the
	// easing maths here, which is what makes it the right probe: halfway through the duration the
	// offset must be exactly halfway to the target, whatever DreamTween does internally.
	Fixture.ScrollBox->ScrollAnimationMode = EDreamScrollAnimationMode::EaseCurve;
	Fixture.ScrollBox->ScrollAnimationEase = EDreamTweenEase::Linear;
	Fixture.ScrollBox->ScrollAnimationDuration = 0.5f;
	Fixture.ScrollBox->SetScrollOffsetAnimated(120.0f);
	TestEqual(TEXT("An eased scroll still does not move on the frame it is asked for"),
		Fixture.ScrollBox->GetScrollOffset(), 0.0f);

	for (int32 i = 0; i < 15; i++)//0.25s of a 0.5s duration
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestEqual(TEXT("Halfway through the duration it is halfway to the target"),
		Fixture.ScrollBox->GetScrollOffset(), 60.0f, 0.5f);
	TestTrue(TEXT("...and is still animating"), Fixture.ScrollBox->IsAnimatingScroll());

	for (int32 i = 0; i < 16; i++)//past the end of the duration
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestEqual(TEXT("At the duration it lands exactly on the target"), Fixture.ScrollBox->GetScrollOffset(), 120.0f, 0.001f);
	TestFalse(TEXT("...and stops"), Fixture.ScrollBox->IsAnimatingScroll());

	// Duration is what decides arrival, not distance -- the difference from InterpToSpeed, where a
	// longer distance simply takes longer.
	Fixture.ScrollBox->SetScrollOffsetAnimated(0.0f);
	int32 StepsToArrive = 0;
	while (Fixture.ScrollBox->IsAnimatingScroll() && StepsToArrive < 600)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
		StepsToArrive++;
	}
	// 0.5s at 1/60 is 30 steps; allow one for the frame the duration is crossed on.
	TestTrue(TEXT("A different distance takes the same number of frames"), FMath::Abs(StepsToArrive - 30) <= 1);

	// A curve that is not bound must still arrive rather than freezing the box forever.
	Fixture.ScrollBox->ScrollAnimationEase = EDreamTweenEase::CurveFloat;//carries its curve elsewhere; unbound here
	Fixture.ScrollBox->SetScrollOffsetAnimated(90.0f);
	for (int32 i = 0; i < 40; i++)
	{
		Fixture.ScrollBox->TickScrollPhysics(Step);
	}
	TestEqual(TEXT("An unbound curve falls back to a linear blend and still arrives"),
		Fixture.ScrollBox->GetScrollOffset(), 90.0f, 0.001f);
	TestFalse(TEXT("...without leaving the box animating"), Fixture.ScrollBox->IsAnimatingScroll());
	return true;
}

#endif
