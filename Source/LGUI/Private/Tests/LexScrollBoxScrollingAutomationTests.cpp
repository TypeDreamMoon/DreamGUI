// Copyright 2026-Present LexLiu. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Engine/World.h"

/*
 * Scroll box sizing semantics: the scroll-axis desired size must exclude content. Reporting the
 * content extent let any Auto-measuring ancestor inflate the viewport to fit everything — which is
 * unscrollable by construction, and made the designer and PIE disagree wherever the surrounding
 * space differed (the sidebar band ballooning to 994px was exactly this).
 */

namespace LexScrollBoxScrollingTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	ULexWidget* MakeWidget(UWorld* World, ULexWidget* Parent, const TCHAR* Name, float W, float H)
	{
		ULexWidget* Widget = NewObject<ULexWidget>(World, NAME_None, RF_Public | RF_Transactional);
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
	FLexScrollBoxDesiredSizeExcludesContentTest,
	"LGUI.Layout.ScrollBox.DesiredSizeExcludesContentOnScrollAxis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxDesiredSizeExcludesContentTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
	FScopedGameWorld TestWorld;
	ULexWidget* Outer = MakeWidget(TestWorld.World, nullptr, TEXT("Outer"), 300.0f, 400.0f);
	ULexWidget* ScrollWidget = MakeWidget(TestWorld.World, Outer, TEXT("Scroll"), 200.0f, 120.0f);
	ULexPanelLayoutBase* OuterPanel = Outer->CreateNewLayoutContainer<ULexLayoutContainerVerticalBox>();
	ULexLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<ULexLayoutContainerScrollBox>();
	TestNotNull(TEXT("Outer panel created"), OuterPanel);
	TestNotNull(TEXT("ScrollBox created"), ScrollBox);
	for (int32 i = 0; i < 3; i++)
	{
		MakeWidget(TestWorld.World, ScrollWidget, *FString::Printf(TEXT("Block%d"), i), 180.0f, 100.0f);
	}
	Outer->OnRegister();
	ScrollWidget->OnRegister();
	ULexWidget::MarkLayoutForRebuild(Outer);
	ULexWidget::RebuildLayoutImmediately(Outer);

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
	FLexScrollBoxScrollRangeTest,
	"LGUI.Layout.ScrollBox.ScrollRangeAndClamping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxScrollRangeTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
	FScopedGameWorld TestWorld;
	// Viewport authored at 120 tall, content 3 x 100: scrollable range must be exactly 180.
	ULexWidget* ScrollWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Scroll"), 200.0f, 120.0f);
	ULexLayoutContainerScrollBox* ScrollBox = ScrollWidget->CreateNewLayoutContainer<ULexLayoutContainerScrollBox>();
	TestNotNull(TEXT("ScrollBox created"), ScrollBox);
	for (int32 i = 0; i < 3; i++)
	{
		MakeWidget(TestWorld.World, ScrollWidget, *FString::Printf(TEXT("Block%d"), i), 180.0f, 100.0f);
	}
	ScrollWidget->OnRegister();
	ULexWidget::MarkLayoutForRebuild(ScrollWidget);
	ULexWidget::RebuildLayoutImmediately(ScrollWidget);

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

namespace LexScrollBoxScrollingTestLocal
{
	/** Viewport 120 tall over 3 x 100 of content: scrollable range 180, view fraction 120/300. */
	struct FScrollFixture
	{
		FScopedGameWorld TestWorld;
		ULexWidget* ScrollWidget = nullptr;
		ULexLayoutContainerScrollBox* ScrollBox = nullptr;
		TArray<ULexWidget*> Blocks;

		FScrollFixture()
		{
			ScrollWidget = MakeWidget(TestWorld.World, nullptr, TEXT("Scroll"), 200.0f, 120.0f);
			ScrollBox = ScrollWidget->CreateNewLayoutContainer<ULexLayoutContainerScrollBox>();
			for (int32 i = 0; i < 3; i++)
			{
				Blocks.Add(MakeWidget(TestWorld.World, ScrollWidget, *FString::Printf(TEXT("Block%d"), i), 180.0f, 100.0f));
			}
		}
		void Arrange()
		{
			ScrollWidget->OnRegister();
			ULexWidget::MarkLayoutForRebuild(ScrollWidget);
			ULexWidget::RebuildLayoutImmediately(ScrollWidget);
		}
		~FScrollFixture() { if (ScrollWidget) { ScrollWidget->DestroyWidget(); } }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexScrollBoxOffsetBeforeLayoutTest,
	"LGUI.Layout.ScrollBox.OffsetRequestedBeforeLayoutSurvives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxOffsetBeforeLayoutTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
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
	FLexScrollBoxViewFractionsTest,
	"LGUI.Layout.ScrollBox.ViewFractionsDescribeTheVisibleWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxViewFractionsTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
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
	FLexScrollBoxScrollIntoViewTest,
	"LGUI.Layout.ScrollBox.ScrollWidgetIntoViewMovesTheMinimumDistance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxScrollIntoViewTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();

	// Block0 occupies content 0..100 and the view is 0..120, so it is already whole on screen.
	TestFalse(TEXT("A fully visible widget does not scroll"), Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.Blocks[0]));

	// Block2 occupies 200..300. Bringing its trailing edge to the bottom of a 120 view means 180.
	TestTrue(TEXT("A widget below the view scrolls into it"), Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.Blocks[2]));
	TestEqual(TEXT("...by the minimum distance, not by centring it"), Fixture.ScrollBox->GetScrollOffset(), 180.0f);

	// Block0 is now above the view; its leading edge comes back to the top.
	TestTrue(TEXT("A widget above the view scrolls back to it"), Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.Blocks[0]));
	TestEqual(TEXT("...landing on its leading edge"), Fixture.ScrollBox->GetScrollOffset(), 0.0f);

	// A descendant resolves to the child that owns the slot, so callers need not know the nesting.
	// Giving Block2 a child also changes what Block2 measures as -- a widget with no layout container
	// falls back to measuring its content -- so the content extent, and with it the target offset, is
	// no longer 180. Pinning that number would be asserting the fixture rather than the behaviour, so
	// what is checked is the invariant: descendant and owning child scroll to the same place.
	ULexWidget* Nested = MakeWidget(Fixture.TestWorld.World, Fixture.Blocks[2], TEXT("Nested"), 40.0f, 20.0f);
	Fixture.Arrange();
	Fixture.ScrollBox->ScrollToStart();
	Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.Blocks[2]);
	const float OffsetViaChild = Fixture.ScrollBox->GetScrollOffset();
	Fixture.ScrollBox->ScrollToStart();
	TestTrue(TEXT("A descendant scrolls its owning child into view"), Fixture.ScrollBox->ScrollWidgetIntoView(Nested));
	TestEqual(TEXT("...to the same place as its owning child"), Fixture.ScrollBox->GetScrollOffset(), OffsetViaChild);
	TestTrue(TEXT("...and that was a real scroll, not a coincidental no-op"), OffsetViaChild > 0.0f);

	TestFalse(TEXT("A widget outside this box reports no scroll"),
		Fixture.ScrollBox->ScrollWidgetIntoView(Fixture.ScrollWidget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLexScrollBoxInertiaTest,
	"LGUI.Layout.ScrollBox.MomentumDecaysAndStops",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxInertiaTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
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
	FLexScrollBoxOverscrollTest,
	"LGUI.Layout.ScrollBox.OverscrollIsBoundedAndSpringsBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLexScrollBoxOverscrollTest::RunTest(const FString& Parameters)
{
	using namespace LexScrollBoxScrollingTestLocal;
	FScrollFixture Fixture;
	Fixture.Arrange();
	Fixture.ScrollBox->OverscrollLimit = 100.0f;

	// Dragging past the start rubber-bands instead of stopping dead, and the scroll POSITION stays
	// in range -- only the displayed content is pulled out.
	Fixture.ScrollBox->ApplyDragDelta(-50.0f);
	TestEqual(TEXT("The scroll position stays at the start"), Fixture.ScrollBox->GetScrollOffset(), 0.0f);
	// Damped = Limit * Excess / (Limit + Excess) = 100 * 50 / 150 = 33.333, signed negative.
	TestEqual(TEXT("The pull is damped, not one-to-one"), Fixture.ScrollBox->GetOverscroll(), -33.333f, 0.01f);

	// The band saturates: ten times the raw pull is nowhere near ten times the displacement, and it
	// can never exceed the limit. The legacy view's flat damping had no bound here at all.
	Fixture.ScrollBox->StopScrolling();
	Fixture.ScrollBox->ApplyDragDelta(-10000.0f);
	const float FarPull = Fixture.ScrollBox->GetOverscroll();
	TestTrue(TEXT("A huge pull stays inside the limit"), FMath::Abs(FarPull) < 100.0f);
	TestTrue(TEXT("...and is close to it, so resistance rises rather than stopping"), FMath::Abs(FarPull) > 98.0f);

	// Releasing springs back to exactly the end and settles.
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

	// With overscroll switched off the drag simply stops at the end.
	Fixture.ScrollBox->StopScrolling();
	Fixture.ScrollBox->bAllowOverscroll = false;
	Fixture.ScrollBox->ApplyDragDelta(-50.0f);
	TestEqual(TEXT("With overscroll off nothing is pulled past the end"), Fixture.ScrollBox->GetOverscroll(), 0.0f);
	return true;
}

#endif
