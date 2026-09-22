// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamScrollBar.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UIScrollbar.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The scroll family after the 2026-09-01 rewrite, aimed at the claims that rewrite actually makes.
 *
 * Four layers, and the bottom one is the reason for the other three: a stretched widget's resolved
 * size is its own delta PLUS its parent's span, and the two writers that could change that span
 * both used to publish a number without telling anybody. Everything above it -- the offset model in
 * UUIScrollView, the absolute handle in UUIScrollbar, the placed rows in UDreamListViewBase -- was
 * previously written around that hole, most visibly as a per-frame watch that re-published anchors
 * and then oscillated in the designer.
 *
 * Headless: no world, no registration, no layout pass. That is deliberate here rather than merely
 * convenient, because the whole family's promise is that its geometry is AUTHORED and therefore
 * answerable before anything arranges it.
 */
namespace DreamScrollRefactorTestLocal
{
	/** A widget with a real rect and no parent -- the stand-in for a consumer that sized a control. */
	UDreamWidget* Root(UObject* InOuter, float InWidth, float InHeight)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InOuter);
		Widget->SetPivot(FVector2D(0.5, 0.5));
		Widget->SetWidth(InWidth);
		Widget->SetHeight(InHeight);
		return Widget;
	}

	/** A child stretched over the whole of its parent, with the stated delta on both axes. */
	UDreamWidget* Stretched(UDreamWidget* InParent, const FVector2D& InSizeDelta = FVector2D::ZeroVector)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InParent);
		Widget->SetParent(InParent);
		Widget->SetPivot(FVector2D(0.5, 0.5));
		Widget->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.0), FVector2D(1.0, 1.0), false, false);
		Widget->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, InSizeDelta);
		return Widget;
	}

	/** The list's shape without the list: a viewport with a taller column hanging from its top. */
	struct FScrollFixture
	{
		UDreamWidget* Viewport = nullptr;
		UDreamWidget* Content = nullptr;
		UUIScrollView* View = nullptr;
	};

	FScrollFixture MakeScroll(UObject* InOuter, float InViewportHeight, float InContentHeight,
		float InViewportWidth = 320.0f, float InContentWidth = 320.0f)
	{
		FScrollFixture Fixture;
		Fixture.Viewport = Root(InOuter, InViewportWidth, InViewportHeight);
		Fixture.Content = NewObject<UDreamWidget>(Fixture.Viewport);
		Fixture.Content->SetParent(Fixture.Viewport);
		// Top-left anchored with the pivot on the same corner, which is what makes an anchored
		// position of zero mean "start aligned" and is how every scrolled column in the library sits.
		Fixture.Content->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 1.0), FVector2D(0.0, 1.0), false, false);
		Fixture.Content->SetPivot(FVector2D(0.0, 1.0));
		Fixture.Content->SetWidth(InContentWidth);
		Fixture.Content->SetHeight(InContentHeight);
		Fixture.Content->SetAnchoredPosition(FVector2D::ZeroVector);

		Fixture.View = Fixture.Viewport->AddComponent<UUIScrollView>();
		Fixture.View->SetContent(Fixture.Content);
		Fixture.View->SetCoordinateMode(EDreamScrollCoordinateMode::AnchoredPosition);
		return Fixture;
	}

	TArray<FText> Labels(int32 InCount)
	{
		TArray<FText> Result;
		Result.Reserve(InCount);
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			Result.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Item %d"), Index)));
		}
		return Result;
	}

	template<class T>
	T* AuthorList(float InWidth, float InHeight)
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->StyleSource = EDreamUIStyleSource::Inline;
		Control->SetWidth(InWidth);
		Control->SetHeight(InHeight);
		return Control;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStretchedSizePublishTest,
	"DreamGUI.Core.Geometry.AStretchedWidgetsWidthIsItsDeltaPlusItsParentsSpan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamStretchedSizePublishTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollRefactorTestLocal;

	// Owned, not merely rooted: a widget tree collected without DestroyWidget logs an Error out of
	// BeginDestroy, and the collector fires BETWEEN tests -- so the Error lands on whichever test is
	// running when it does. That is the hazard TDreamTestControl exists for; see its header.
	TDreamTestControl<UDreamWidget> Parent(Root(GetTransientPackage(), 342.0f, 200.0f));

	// THE regression. SetAnchoredPositionAndSizeDelta used to assign the size DELTA straight into
	// the resolved-size cache and leave the dirty flag alone, so from then on GetWidth answered with
	// the delta -- zero, or minus a gutter -- however wide the parent actually was. That is where the
	// list's "-0 wide viewport inside a 342-wide face" came from: RefreshScrollFurniture states the
	// gutter exactly this way. The neighbouring SetSizeDelta always dirtied instead; the two are the
	// same operation and now agree.
	UDreamWidget* Child = Stretched(Parent.Get());
	TestEqual(TEXT("a zero delta on a stretched axis means the parent's whole span"),
		Child->GetWidth(), 342.0f);
	TestEqual(TEXT("-- on both axes"), Child->GetHeight(), 200.0f);

	// A gutter, stated the way every inset in this library is stated: a negative delta.
	UDreamWidget* Inset = Stretched(Parent.Get(), FVector2D(-12.0, 0.0));
	TestEqual(TEXT("a negative delta is the span less the inset"), Inset->GetWidth(), 330.0f);
	TestEqual(TEXT("and the delta itself is what was written"),
		static_cast<float>(Inset->GetSizeDelta().X), -12.0f);

	// The invariant the second half of the fix defends: a change in a parent's RESOLVED size reaches
	// every stretched descendant, however deep, and whether or not any intermediate node's own delta
	// moved. The middle node below keeps a delta of zero throughout -- "exactly the span" -- which is
	// precisely the case the old invalidation, keyed on the delta, could not see.
	UDreamWidget* Grandchild = Stretched(Child, FVector2D(-20.0, 0.0));
	TestEqual(TEXT("the grandchild resolved through both hops"), Grandchild->GetWidth(), 322.0f);

	Parent->SetWidth(500.0f);
	TestEqual(TEXT("a parent's new width reaches its stretched child"), Child->GetWidth(), 500.0f);
	TestEqual(TEXT("and its stretched grandchild"), Grandchild->GetWidth(), 480.0f);
	TestEqual(TEXT("with the middle node's own delta never having moved"),
		static_cast<float>(Child->GetSizeDelta().X), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollViewOffsetModelTest,
	"DreamGUI.Interaction.ScrollView.OffsetExtentAndProgressAreOneQuantitySeenThreeWays",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollViewOffsetModelTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollRefactorTestLocal;

	FScrollFixture Fixture = MakeScroll(GetTransientPackage(), 200.0f, 600.0f);
	TDreamTestControl<UDreamWidget> Own(Fixture.Viewport);
	UUIScrollView* View = Fixture.View;
	View->SetHorizontal(false);
	View->SetVertical(true);

	// The three measurements, all taken from the widgets rather than stored: this is what makes the
	// offset a VIEW of the content's position instead of a second copy of it that could drift.
	TestEqual(TEXT("the window is the content's parent"), View->GetViewportSize().Y, 200.0);
	TestEqual(TEXT("the content is what the author said"), View->GetContentSize().Y, 600.0);
	TestEqual(TEXT("and the travel is the difference"), View->GetScrollableExtent().Y, 400.0);

	TestEqual(TEXT("a fresh view rests at the start"), View->GetScrollOffset().Y, 0.0);
	TestEqual(TEXT("which is progress zero"), View->GetScrollProgress().Y, 0.0);

	// Offset in, position out. Y grows DOWNWARD -- the reading direction -- so scrolling down by 100
	// moves the content up by 100 in the engine's +Z, and the anchored position says so.
	View->SetScrollOffset(FVector2D(0.0, 100.0));
	TestEqual(TEXT("the offset is what was asked for"), View->GetScrollOffset().Y, 100.0);
	TestEqual(TEXT("the content moved by it"), Fixture.Content->GetVerticalAnchoredPosition(), 100.0f);
	TestEqual(TEXT("and the progress is the offset over the extent"), View->GetScrollProgress().Y, 0.25);

	// Progress in, the same place out. The two are the same quantity, so the round trip is exact.
	View->SetScrollProgress(FVector2D(0.0, 0.5));
	TestEqual(TEXT("progress in lands the offset"), View->GetScrollOffset().Y, 200.0);
	View->ScrollToEnd();
	TestEqual(TEXT("the end is the whole extent"), View->GetScrollOffset().Y, 400.0);
	TestEqual(TEXT("-- which is progress one"), View->GetScrollProgress().Y, 1.0);
	View->ScrollToStart();
	TestEqual(TEXT("and the start is zero again"), View->GetScrollOffset().Y, 0.0);

	// Clamped at both ends, and the ranges the recycler reads still bracket the content's position.
	View->SetScrollOffset(FVector2D(0.0, 10000.0));
	TestEqual(TEXT("an offset past the end is the end"), View->GetScrollOffset().Y, 400.0);
	TestEqual(TEXT("the vertical range starts where the content rests"), View->GetVerticalRange().X, 0.0);
	TestEqual(TEXT("and ends one extent later"), View->GetVerticalRange().Y, 400.0);

	// An axis with nothing to scroll reads zero rather than dividing by it. A list that fits is at
	// its start, and every bar over it covers the whole track.
	Fixture.Content->SetHeight(120.0f);
	View->RectRangeChanged();
	TestEqual(TEXT("content that fits has no travel"), View->GetScrollableExtent().Y, 0.0);
	TestEqual(TEXT("and reads as progress zero, not as a division by zero"),
		View->GetScrollProgress().Y, 0.0);
	TestFalse(TEXT("and says it cannot scroll"), View->CanScrollOnAxis(false));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollViewAxisFlagsTest,
	"DreamGUI.Interaction.ScrollView.OneGesturesAxisIsNotTheViewsCapability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollViewAxisFlagsTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollRefactorTestLocal;

	// Both axes overflowing, and OnlyOneDirection on -- so a gesture commits to one of them.
	FScrollFixture Fixture = MakeScroll(GetTransientPackage(), 200.0f, 600.0f, 320.0f, 900.0f);
	TDreamTestControl<UDreamWidget> Own(Fixture.Viewport);
	UUIScrollView* View = Fixture.View;
	View->SetHorizontal(true);
	View->SetVertical(true);
	View->SetOnlyOneDirection(true);

	// A vertical wheel notch. Under the old shape this wrote bAllowHorizontalScroll = false and left
	// it there for good: UpdateProgress then stopped maintaining Progress.X, and every horizontal bar
	// attached to this view quietly froze for the rest of the component's life.
	UDreamPointerEventData* Wheel = NewObject<UDreamPointerEventData>();
	Wheel->EnterWidget = Fixture.Content;
	Wheel->ScrollAxisValue = FVector2D(0.0, -1.0);
	View->OnPointerScroll_Implementation(Wheel);
	TestTrue(TEXT("the vertical notch moved the view"), View->GetScrollOffset().Y > 0.0);

	// Now move the OTHER axis. Its progress has to follow, because the view still scrolls sideways --
	// which is a fact about the view, not about the last gesture anybody made.
	View->SetScrollOffset(FVector2D(290.0, View->GetScrollOffset().Y));
	TestEqual(TEXT("the horizontal offset took"), View->GetScrollOffset().X, 290.0);
	TestEqual(TEXT("and its progress is still being maintained"),
		View->GetScrollProgress().X, 0.5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollViewWheelHandoffTest,
	"DreamGUI.Interaction.ScrollView.AWheelItCannotUseIsHandedOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollViewWheelHandoffTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollRefactorTestLocal;

	FScrollFixture Fixture = MakeScroll(GetTransientPackage(), 200.0f, 600.0f);
	TDreamTestControl<UDreamWidget> Own(Fixture.Viewport);
	UUIScrollView* View = Fixture.View;
	View->SetHorizontal(false);
	View->SetVertical(true);
	View->SetScrollSensitivity(50.0f);

	UDreamPointerEventData* Wheel = NewObject<UDreamPointerEventData>();
	Wheel->EnterWidget = Fixture.Content;

	// A notch that moves it is CONSUMED -- the component's AllowEventBubbleUp, which is off.
	Wheel->ScrollAxisValue = FVector2D(0.0, -1.0);
	TestFalse(TEXT("a wheel this view can use is consumed"), View->OnPointerScroll_Implementation(Wheel));
	TestEqual(TEXT("one notch is one sensitivity"), View->GetScrollOffset().Y, 50.0);

	// At the far end, the same notch has nowhere to go, so it is handed ON. That is what lets a list
	// inside a scrolling page continue the page instead of freezing it -- UMG's scroll box makes the
	// same call.
	View->ScrollToEnd();
	TestTrue(TEXT("a wheel at the end of the travel bubbles"), View->OnPointerScroll_Implementation(Wheel));
	TestEqual(TEXT("and did not move anything"), View->GetScrollOffset().Y, 400.0);

	// The other way at the other end, for the same reason.
	View->ScrollToStart();
	Wheel->ScrollAxisValue = FVector2D(0.0, 1.0);
	TestTrue(TEXT("a wheel at the start bubbles too"), View->OnPointerScroll_Implementation(Wheel));
	TestEqual(TEXT("still at the start"), View->GetScrollOffset().Y, 0.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollbarValueGateTest,
	"DreamGUI.Interaction.Scrollbar.AClampedValueIsNotAChange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollbarValueGateTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollRefactorTestLocal;

	TDreamTestControl<UDreamWidget> Track(Root(GetTransientPackage(), 12.0f, 200.0f));
	UDreamWidget* Handle = Stretched(Track.Get());
	UUIScrollbar* Bar = Track->AddComponent<UUIScrollbar>();
	Bar->SetDirectionType(EUIScrollbarDirectionType::BottomToTop);
	Bar->SetHandle(Handle);
	Bar->SetValueAndSize(1.0f, 0.25f, false);

	int32 Broadcasts = 0;
	Bar->GetOnValueChangedEvent().AddLambda([&Broadcasts](float) { ++Broadcasts; });

	// THE regression. The old form compared the RAW argument and clamped afterwards, so this passed
	// the gate, assigned the value it already held, and broadcast a change that never happened.
	Bar->SetValue(1.7f);
	TestEqual(TEXT("a value already at the ceiling does not move"), Bar->GetValue(), 1.0f);
	TestEqual(TEXT("and nothing was announced"), Broadcasts, 0);

	Bar->SetValue(0.5f);
	TestEqual(TEXT("a real move is a real move"), Bar->GetValue(), 0.5f);
	TestEqual(TEXT("and it is announced once"), Broadcasts, 1);

	// A size-only change is not a value change, so it re-places the handle and says nothing. The old
	// form fired on it -- and fired on only two of the three delegates while it was at it, so a
	// Blueprint listener and a C++ listener disagreed about what had happened.
	Bar->SetValueAndSize(0.5f, 0.4f, true);
	TestEqual(TEXT("a size that moved without the value announces nothing"), Broadcasts, 1);
	TestEqual(TEXT("but it did take"), Bar->GetSize(), 0.4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollbarHandleGeometryTest,
	"DreamGUI.Interaction.Scrollbar.TheHandleIsAbsoluteAndItsFloorIsTheDragScale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollbarHandleGeometryTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollRefactorTestLocal;

	TDreamTestControl<UDreamWidget> Track(Root(GetTransientPackage(), 12.0f, 200.0f));
	UDreamWidget* Handle = Stretched(Track.Get());
	UUIScrollbar* Bar = Track->AddComponent<UUIScrollbar>();
	Bar->SetDirectionType(EUIScrollbarDirectionType::BottomToTop);
	Bar->SetHandle(Handle);
	Bar->SetValueAndSize(0.0f, 0.25f, false);

	// Absolute numbers off the AREA's resolved length, with the anchor collapsed to a point on its
	// start edge. The ratio anchors this replaced resolved the parent's span at write time, which on
	// any frame that is not a full layout reads a stretched track's zero delta -- the defect this
	// library has now paid for five times over.
	TestEqual(TEXT("the handle's axis is a point anchor -- min"),
		static_cast<float>(Handle->GetAnchorMin().Y), 0.0f);
	TestEqual(TEXT("-- and max"),
		static_cast<float>(Handle->GetAnchorMax().Y), 0.0f);
	TestEqual(TEXT("the handle covers the fraction of the track"),
		static_cast<float>(Handle->GetSizeDelta().Y), 50.0f);
	TestEqual(TEXT("and spans it across"),
		static_cast<float>(Handle->GetSizeDelta().X), 12.0f);
	TestEqual(TEXT("value zero sits it on the start"),
		static_cast<float>(Handle->GetAnchoredPosition().Y), 0.0f);

	// It travels the track LESS its own length, which is what puts value 1's far edge on the far end.
	Bar->SetValue(1.0f);
	TestEqual(TEXT("value one is the end of the travel"),
		static_cast<float>(Handle->GetAnchoredPosition().Y), 150.0f);

	// The floor raises the DRAWN length and the DRAG SCALE together, because a handle drawn longer
	// than the fraction it drags with runs at the wrong rate for the whole of a long list. That is
	// why it lives in the component that owns both rather than in the control above it.
	Bar->SetValueAndSize(0.0f, 0.05f, false);
	Bar->SetMinHandleSize(40.0f);
	TestEqual(TEXT("the authored fraction is untouched"), Bar->GetSize(), 0.05f);
	TestEqual(TEXT("the effective one is the floor"), Bar->GetEffectiveSize(), 0.2f);
	TestEqual(TEXT("and the drawn handle agrees with it"),
		static_cast<float>(Handle->GetSizeDelta().Y), 40.0f);

	// A handle that fills its track has nowhere to travel. Every arithmetic that used to divide by
	// that travel -- the track click and the drag -- produced a NaN that every later clamp preserved.
	Bar->SetValueAndSize(0.5f, 1.0f, false);
	TestEqual(TEXT("a full handle fills the track"),
		static_cast<float>(Handle->GetSizeDelta().Y), 200.0f);
	TestEqual(TEXT("and sits at its start with no travel to speak of"),
		static_cast<float>(Handle->GetAnchoredPosition().Y), 0.0f);
	TestTrue(TEXT("and the value is still a number"), FMath::IsFinite(Bar->GetValue()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListVirtualizationTest,
	"DreamGUI.Controls.List.AboveTheThresholdThePoolIsAWindowOntoTheSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListVirtualizationTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollRefactorTestLocal;

	// Enough rows that building one widget each would be the wrong answer, and a viewport with a
	// real height so the window has something to be sized from.
	TDreamTestControl<UDreamListView> List(AuthorList<UDreamListView>(320.0f, 200.0f));
	List->Style.RowHeight = 20.0f;
	List->Style.RowSpacing = 0.0f;
	List->VirtualizationThreshold = 200;
	List->VirtualizationOverscan = 2;
	List->Items = Labels(300);
	List->Initialize();

	TestTrue(TEXT("a source past the threshold recycles"), List->IsVirtualizing());
	TestEqual(TEXT("the list shows every item"), List->GetRowCount(), 300);
	// The window is the viewport's worth plus one, plus an overscan at each edge. Whatever the exact
	// number, the claim that matters is that it is a window and not a wall.
	TestTrue(TEXT("but it holds a window of widgets, not one per item"),
		List->GetRealizedRowCount() > 0 && List->GetRealizedRowCount() < 40);
	TestEqual(TEXT("the column is still the whole scroll range"),
		List->ColumnNode->GetHeight(), 300.0f * 20.0f);

	// At rest the window starts at the top, and the first pool row shows the first item.
	TestEqual(TEXT("the first slot shows the first item"), List->GetRowItemIndex(0), 0);
	TestTrue(TEXT("and an item inside the window has a row"), List->GetRowWidget(0) != nullptr);
	TestNull(TEXT("while one far outside it does not"), List->GetRowWidget(250));

	// Scrolling re-binds the same widgets to different items. That is the whole of recycling, and
	// the pool must not have grown to do it.
	const int32 PoolSize = List->GetRealizedRowCount();
	List->ScrollBehaviour->SetScrollOffset(FVector2D(0.0, 400.0));
	TestEqual(TEXT("the pool did not grow"), List->GetRealizedRowCount(), PoolSize);
	// 400 units at a pitch of 20 is item 20 at the window's top, less the overscan.
	TestEqual(TEXT("the first slot came round to a later item"), List->GetRowItemIndex(0), 18);
	TestTrue(TEXT("the item under the window's top has a row now"), List->GetRowWidget(20) != nullptr);
	TestNull(TEXT("and the one it left does not"), List->GetRowWidget(0));

	// A row's rect is stated from its DISPLAY index, so a recycled widget lands where its item
	// belongs in the column rather than where its pool slot would have put it.
	if (UDreamWidget* FirstSlot = List->RowNodes[0].Get())
	{
		TestEqual(TEXT("a recycled row sits at its item's place in the column"),
			static_cast<float>(FirstSlot->GetAnchoredPosition().Y), -18.0f * 20.0f);
	}

	// Scrolling to an item that has no widget yet: the target is computed from the pitch, which is
	// the only version of this that can answer while recycling.
	TestTrue(TEXT("an unrealized row can still be scrolled to"), List->ScrollItemIntoView(250, false));
	TestTrue(TEXT("and it exists once the view got there"), List->GetRowWidget(250) != nullptr);

	// Below the threshold the contract is the old one: a widget per item, every index answerable.
	List->SetItems(Labels(12));
	TestFalse(TEXT("a short source does not recycle"), List->IsVirtualizing());
	TestEqual(TEXT("and builds one row each"), List->GetRealizedRowCount(), 12);
	TestTrue(TEXT("so every item has a row"), List->GetRowWidget(11) != nullptr);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
