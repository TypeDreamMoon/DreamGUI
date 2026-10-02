// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"
#include "DreamListControlsTestTypes.h"

#include "Controls/DreamScrollBar.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UIScrollbar.h"
#include "Interaction/UISelectable.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The knobs a UMG user reaches for on a scroll box, aimed at the one failure they all share.
 *
 * Every claim here is of the form "the control's property reached the thing that actually decides".
 * That is the failure this family keeps having and the reason these are worth writing: nothing in
 * this library re-derives a control from a property that moved, so a knob whose setter forgets to
 * push is a knob that reads back correctly, shows the right value in the details panel, and does
 * nothing at all. Reading the behaviour's own getter is the only assertion that can tell the
 * difference; reading the control's field back would pass either way.
 *
 * Headless: no world, no registration, no layout pass. Sizes are authored before Initialize, which
 * is what a designer or a .dui line does anyway, so every derived rect is an exact number.
 */
namespace DreamScrollBoxParityTestLocal
{
	/** A box with a known rect, an inline style and a bar that always shows. */
	UDreamScrollBox* MakeBox(float InThickness = 14.0f)
	{
		UDreamScrollBox* Box = NewObject<UDreamScrollBox>(GetTransientPackage());
		// Inline rather than the sheet: a project sheet in the running editor would otherwise decide
		// what these assertions are comparing against.
		Box->StyleSource = EDreamUIStyleSource::Inline;
		Box->ScrollBarVisibility = EDreamScrollBoxScrollbarVisibility::Permanent;
		Box->Style.Bar.Thickness = InThickness;
		Box->SetWidth(300.0f);
		Box->SetHeight(200.0f);
		Box->Initialize();
		return Box;
	}

	/** Something taller than the window, so the box has somewhere to scroll. */
	UDreamWidget* FillBox(UDreamScrollBox& InBox, float InHeight)
	{
		UDreamWidget* Filler = NewObject<UDreamWidget>(InBox.GetContentNode());
		Filler->SetWidth(200.0f);
		Filler->SetHeight(InHeight);
		InBox.AddContent(Filler);
		return Filler;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxKnobsReachTheBehaviourTest,
	"DreamGUI.ScrollBox.EveryScrollingKnobIsPushedIntoTheBehaviourThatDecides",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxKnobsReachTheBehaviourTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox());
	UUIScrollView* View = Box->GetScrollView();
	if (!TestNotNull(TEXT("the box built its scroll behaviour"), View))
	{
		return false;
	}

	// The defaults first, because "the setter pushed" is only interesting if the starting value was
	// something else -- and because these are the numbers every existing asset silently carries.
	TestEqual(TEXT("a fresh box consumes the wheel while there is somewhere to go"),
		View->GetConsumeMouseWheel(), EDreamScrollBoxConsumeMouseWheel::WhenScrollingPossible);
	TestEqual(TEXT("and multiplies one notch by exactly one"), View->GetWheelScrollMultiplier(), 1.0f);
	TestTrue(TEXT("and lets the content rubber-band"), View->GetAllowOverscroll());
	TestFalse(TEXT("with no pad at either end"), View->GetBackPadScrolling());
	TestFalse(TEXT("neither of them"), View->GetFrontPadScrolling());
	TestTrue(TEXT("touch scrolls it"), View->GetEnableTouchScrolling());
	TestTrue(TEXT("so does a right-click drag"), View->GetAllowRightClickDragScrolling());
	TestTrue(TEXT("and it swallows the pointer events it acted on"), View->GetConsumePointerInput());
	TestEqual(TEXT("focus landing inside glides it, which is what navigation already did"),
		View->GetScrollWhenFocusChanges(), EDreamUIScrollWhenFocusChanges::AnimatedScroll);

	// Each setter, and then the behaviour's own getter. Reading Box->... back instead would pass for
	// a setter that only wrote its own field, which is the whole failure being guarded against.
	Box->SetConsumeMouseWheel(EDreamScrollBoxConsumeMouseWheel::Always);
	TestEqual(TEXT("the wheel rule reached the behaviour"),
		View->GetConsumeMouseWheel(), EDreamScrollBoxConsumeMouseWheel::Always);

	Box->SetWheelScrollMultiplier(3.0f);
	TestEqual(TEXT("so did the multiplier"), View->GetWheelScrollMultiplier(), 3.0f);

	Box->SetAllowOverscroll(false);
	TestFalse(TEXT("and the overscroll switch"), View->GetAllowOverscroll());

	Box->SetEnableTouchScrolling(false);
	TestFalse(TEXT("and the touch switch"), View->GetEnableTouchScrolling());

	Box->SetAllowRightClickDragScrolling(false);
	TestFalse(TEXT("and the right-button switch"), View->GetAllowRightClickDragScrolling());

	Box->SetConsumePointerInput(false);
	TestFalse(TEXT("and the consume switch, which is the bubble flag asked the other way round"),
		View->GetConsumePointerInput());

	Box->SetScrollWhenFocusChanges(EDreamUIScrollWhenFocusChanges::NoScroll);
	TestEqual(TEXT("and the focus rule"),
		View->GetScrollWhenFocusChanges(), EDreamUIScrollWhenFocusChanges::NoScroll);

	Box->SetAnalogMouseWheelKey(EKeys::Gamepad_LeftY);
	TestTrue(TEXT("and the analog key"), View->GetAnalogMouseWheelKey() == EKeys::Gamepad_LeftY);

	Box->SetWheelScrollAnimationDuration(0.5f);
	TestEqual(TEXT("and the wheel animation's duration"), View->GetWheelScrollAnimationDuration(), 0.5f);

	Box->SetNavigationDestination(EDreamUIScrollDestination::BottomOrRight);
	TestEqual(TEXT("and the fourth destination, which only exists because UMG has it"),
		View->GetNavigationDestination(), EDreamUIScrollDestination::BottomOrRight);

	// The style push is the other road into the behaviour, and it is the one a TEMPLATE-built box
	// takes: the view is added fresh, carrying library defaults rather than authored ones. A knob
	// only the setter pushed would be a knob that road silently did without.
	Box->ApplyStyle();
	TestEqual(TEXT("a style push re-states the wheel rule rather than leaving the library default"),
		View->GetConsumeMouseWheel(), EDreamScrollBoxConsumeMouseWheel::Always);
	TestFalse(TEXT("and the overscroll switch"), View->GetAllowOverscroll());
	TestFalse(TEXT("and the consume switch"), View->GetConsumePointerInput());
	TestEqual(TEXT("and the focus rule"),
		View->GetScrollWhenFocusChanges(), EDreamUIScrollWhenFocusChanges::NoScroll);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxBarPaddingTest,
	"DreamGUI.ScrollBox.PaddingTheBarWidensTheGutterItTakesFromTheViewport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxBarPaddingTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox(14.0f));
	if (!TestNotNull(TEXT("the box built a viewport"), Box->ViewportNode.Get()) ||
		!TestNotNull(TEXT("and a bar"), Box->ScrollBarNode.Get()))
	{
		return false;
	}
	// The unpadded answer first: this is the number the existing scroll box tests assert, and it has
	// to be exactly what it was before the margin existed.
	TestEqual(TEXT("with no margin the gutter is the bar's thickness"),
		static_cast<float>(Box->ViewportNode->GetSizeDelta().X), -14.0f);

	// A margin around the bar is spent on the GUTTER, not out of the bar's thickness: padding a bar
	// must make it better spaced, not thinner.
	Box->SetScrollbarPadding(FMargin(3.0f, 5.0f, 4.0f, 6.0f));
	TestEqual(TEXT("the margin came back out of the style, which is where it lives"),
		Box->GetScrollbarPadding(), FMargin(3.0f, 5.0f, 4.0f, 6.0f));
	TestEqual(TEXT("the gutter grew by the margin across the bar's axis"),
		static_cast<float>(Box->ViewportNode->GetSizeDelta().X), -(14.0f + 3.0f + 4.0f));
	TestEqual(TEXT("the bar is still exactly as thick as the style says"),
		Box->ScrollBarNode->GetWidth(), 14.0f);
	TestEqual(TEXT("and sits in from the box's right edge by the right margin"),
		static_cast<float>(Box->ScrollBarNode->GetAnchoredPosition().X), -4.0f);
	TestEqual(TEXT("with the top and bottom margins taken off its length"),
		static_cast<float>(Box->ScrollBarNode->GetSizeDelta().Y), -(5.0f + 6.0f));

	// The thickness is a READER of the style rather than a field beside it, so writing it through the
	// control has to land in the same place the bar is drawn from.
	Box->SetScrollbarThickness(20.0f);
	TestEqual(TEXT("the thickness went into the style"), Box->Style.Bar.Thickness, 20.0f);
	TestEqual(TEXT("and reads back through the control"), Box->GetScrollbarThickness(), 20.0f);
	TestEqual(TEXT("and the bar is drawn at it"), Box->ScrollBarNode->GetWidth(), 20.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxIdleBarFollowsUMGTest,
	"DreamGUI.ScrollBox.WithNothingToScrollTheBarCollapsesAndTheTrackFlagOnlyBrightensTheIdleTrack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * This replaces DreamGUI.ScrollBox.TheTrackCanOutliveTheBarAndKeepsItsGutterWhenItDoes, which pinned the opposite
 * rule: with AlwaysShowScrollbarTrack on, a box with nothing to scroll kept its bar as a bare groove and kept the
 * gutter for it. That rule was this library's own, and decision C-3 reverses it to follow UMG. SScrollBar collapses,
 * gutter and all, while nothing needs scrolling unless AlwaysShowScrollbar is on; AlwaysShowScrollbarTrack only raises
 * the opacity of an idle track, to half (whole while the bar is hovered or dragged, nothing otherwise). Also checked:
 * the Hidden setting draws no bar and keeps its room while the content overflows, as a Hidden Slate widget keeps its
 * slot, and a Permanent bar is shown with its track at half however little there is to scroll.
 */
bool FDreamScrollBoxIdleBarFollowsUMGTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	// An auto-hiding box with nothing in it: the content is exactly the viewport, so there is nothing to scroll.
	TDreamTestControl<UDreamScrollBox> Box(NewObject<UDreamScrollBox>(GetTransientPackage()));
	Box->StyleSource = EDreamUIStyleSource::Inline;
	Box->Style.Bar.Thickness = 12.0f;
	Box->SetWidth(300.0f);
	Box->SetHeight(200.0f);
	Box->Initialize();
	UDreamVisual* TrackVisual = Box->ScrollBarNode != nullptr && Box->ScrollBarNode->TrackNode != nullptr
		? Box->ScrollBarNode->TrackNode->GetVisual() : nullptr;
	if (!TestNotNull(TEXT("the box built a bar"), Box->ScrollBarNode.Get()) ||
		!TestNotNull(TEXT("and a viewport"), Box->ViewportNode.Get()) ||
		!TestNotNull(TEXT("and the bar draws a track"), TrackVisual))
	{
		return false;
	}
	// The gutter is what the viewport gives up across its width; the track's opacity is the multiplier the box lays
	// over the colour the bar's style gives it.
	auto Gutter = [&Box]() { return -static_cast<float>(Box->ViewportNode->GetSizeDelta().X); };
	auto TrackOpacity = [TrackVisual]() { return TrackVisual->GetColorMultiplier().A; };

	TestFalse(TEXT("an empty box collapses its bar"), Box->ScrollBarNode->GetWidgetActive());
	TestEqual(TEXT("...and takes no gutter for it"), Gutter(), 0.0f);
	TestEqual(TEXT("an idle track is not drawn by default"), TrackOpacity(), 0.0f);

	Box->SetAlwaysShowScrollbarTrack(true);
	TestFalse(TEXT("the track flag does not bring a collapsed bar back"), Box->ScrollBarNode->GetWidgetActive());
	TestEqual(TEXT("...nor take a gutter for one"), Gutter(), 0.0f);
	TestEqual(TEXT("it draws an idle track at half"), TrackOpacity(), 0.5f);

	// Something to scroll: the bar comes out by itself, into a gutter of its thickness, handle and all.
	FillBox(*Box, 600.0f);
	TestTrue(TEXT("a box with somewhere to scroll shows its bar"), Box->ScrollBarNode->GetWidgetActive());
	TestEqual(TEXT("...in a gutter of its thickness"), Gutter(), 12.0f);
	if (Box->ScrollBarNode->HandleNode != nullptr)
	{
		TestTrue(TEXT("...with its handle"), Box->ScrollBarNode->HandleNode->GetWidgetActive());
	}
	TestEqual(TEXT("its idle track is drawn at half"), TrackOpacity(), 0.5f);
	Box->HandleBarStateChangedForTest(EUISelectableSelectionState::Hovered);
	TestEqual(TEXT("hovered, the track is drawn whole"), TrackOpacity(), 1.0f);
	Box->HandleBarStateChangedForTest(EUISelectableSelectionState::Normal);
	TestEqual(TEXT("left alone again, at half"), TrackOpacity(), 0.5f);
	Box->SetAlwaysShowScrollbarTrack(false);
	TestEqual(TEXT("without the flag an idle track is not drawn"), TrackOpacity(), 0.0f);
	Box->HandleBarStateChangedForTest(EUISelectableSelectionState::Pressed);
	TestEqual(TEXT("...and a dragged one is, whole"), TrackOpacity(), 1.0f);
	Box->HandleBarStateChangedForTest(EUISelectableSelectionState::Normal);

	Box->SetScrollBarVisibility(EDreamScrollBoxScrollbarVisibility::Hidden);
	TestFalse(TEXT("a Hidden bar is not drawn"), Box->ScrollBarNode->GetWidgetActive());
	TestEqual(TEXT("...but keeps its room while there is something to scroll"), Gutter(), 12.0f);

	// A Permanent bar over a box with nothing to scroll.
	TDreamTestControl<UDreamScrollBox> Permanent(MakeBox(14.0f));
	UDreamVisual* PermanentTrack = Permanent->ScrollBarNode != nullptr && Permanent->ScrollBarNode->TrackNode != nullptr
		? Permanent->ScrollBarNode->TrackNode->GetVisual() : nullptr;
	if (!TestNotNull(TEXT("the permanent box's bar draws a track"), PermanentTrack))
	{
		return false;
	}
	TestTrue(TEXT("a Permanent bar is shown with nothing to scroll"), Permanent->ScrollBarNode->GetWidgetActive());
	TestEqual(TEXT("...in its gutter"), -static_cast<float>(Permanent->ViewportNode->GetSizeDelta().X), 14.0f);
	TestEqual(TEXT("...with its idle track at half, as AlwaysShowScrollbar draws it"), PermanentTrack->GetColorMultiplier().A, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxStylePushKeepsTheContentTest,
	"DreamGUI.ScrollBox.AStylePushLeavesTheContentWhereScrollingLeftIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The style push put the control's cached ScrollProgress back into the view. That cache follows the view's change
 * event, and a range that moves under the content raises none, so after rows were added the next style push -- any
 * property edit makes one -- moved the content to where the old fraction falls in the new range: a box scrolled 100
 * into a range of 400 sat at a quarter, and given 400 more rows the push took it to 200. The push now reads the
 * progress back from the view. The authored value is still pushed until it first lands on content that can scroll,
 * and again whenever ScrollProgress itself is set, so a box built empty still opens where it was authored to.
 */
bool FDreamScrollBoxStylePushKeepsTheContentTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox());
	FillBox(*Box, 600.0f);
	UUIScrollView* View = Box->GetScrollView();
	if (!TestNotNull(TEXT("the box has a view"), View))
	{
		return false;
	}
	const double RangeBefore = View->GetScrollableExtent().Y;
	if (!TestTrue(FString::Printf(TEXT("the box has more than 100 to scroll (%.1f)"), RangeBefore), RangeBefore > 100.0))
	{
		return false;
	}
	Box->SetScrollOffset(100.0f);
	TestEqual(TEXT("scrolled 100 in"), Box->GetScrollOffset(), 100.0f, 0.01f);

	FillBox(*Box, 400.0f);
	const double RangeAfter = View->GetScrollableExtent().Y;
	TestTrue(TEXT("more rows, more range"), RangeAfter > RangeBefore + 1.0);
	TestEqual(TEXT("adding rows leaves the content where it was"), Box->GetScrollOffset(), 100.0f, 0.01f);
	Box->ApplyStyle();
	TestEqual(TEXT("...and so does the style push after it"), Box->GetScrollOffset(), 100.0f, 0.01f);
	TestEqual(TEXT("the progress it keeps is the offset's share of the new range"),
		Box->ScrollProgress, static_cast<float>(100.0 / RangeAfter), 0.001f);

	// Set, the authored value is pushed at once.
	Box->SetScrollProgress(0.5f);
	TestEqual(TEXT("setting the progress moves the content"), Box->GetScrollOffset(), static_cast<float>(0.5 * RangeAfter), 0.5f);

	// Authored on a box built empty: nothing to scroll yet, so it waits for the content and lands on it.
	TDreamTestControl<UDreamScrollBox> Late(NewObject<UDreamScrollBox>(GetTransientPackage()));
	Late->StyleSource = EDreamUIStyleSource::Inline;
	Late->ScrollProgress = 0.5f;
	Late->SetWidth(300.0f);
	Late->SetHeight(200.0f);
	Late->Initialize();
	UUIScrollView* LateView = Late->GetScrollView();
	if (!TestNotNull(TEXT("the late box has a view"), LateView))
	{
		return false;
	}
	TestEqual(TEXT("built empty, the box has nowhere to put the authored progress"), Late->GetScrollOffset(), 0.0f, 0.01f);
	TestEqual(TEXT("...and keeps it rather than reporting the zero it is at"), Late->ScrollProgress, 0.5f);
	FillBox(*Late, 600.0f);
	TestEqual(TEXT("content arriving takes the box where it was authored to open"),
		Late->GetScrollOffset(), static_cast<float>(0.5 * LateView->GetScrollableExtent().Y), 0.5f);
	// Landed, it is not pushed again over a later scroll.
	Late->SetScrollOffset(10.0f);
	Late->ApplyStyle();
	TestEqual(TEXT("once landed, the authored value is not pushed over a later scroll"), Late->GetScrollOffset(), 10.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxUserScrolledTest,
	"DreamGUI.ScrollBox.OnlyAScrollTheControlDidNotMakeIsReportedAsTheUsers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxUserScrolledTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox());
	FillBox(*Box, 600.0f);
	UUIScrollView* View = Box->GetScrollView();
	if (!TestNotNull(TEXT("the box has a view to drive"), View))
	{
		return false;
	}

	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>());
	Box->OnUserScrolled.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordScrollValue);
	Box->OnScrolled.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordScrollValue);

	// Everything the CONTROL pushes is its own doing, however it was asked for. By the time the value
	// comes back out of the behaviour there is nothing left in it to say which road it took, so this
	// is the only place the distinction can be drawn.
	Box->SetScrollProgress(0.5f);
	Box->ScrollToEnd();
	Box->ScrollToStart();
	Box->SetScrollOffset(120.0f);
	Box->ApplyStyle();
	const int32 OwnPushes = Probe->ScrollValues.Num();
	TestTrue(TEXT("the plain scrolled event fired for the control's own pushes"), OwnPushes > 0);

	// Now the same event arriving from the behaviour with nobody's hand on the control -- which is
	// what a drag, a wheel notch and a bar grab all are.
	Probe->ScrollValues.Reset();
	View->SetScrollOffset(FVector2D(0.0, 80.0));
	TestEqual(TEXT("both events fired once each for one move the control did not make"),
		Probe->ScrollValues.Num(), 2);
	TestEqual(TEXT("and the user-scrolled half reported an OFFSET, which is UMG's unit"),
		Probe->ScrollValues.Last(), Box->GetScrollOffset());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollViewPadScrollingTest,
	"DreamGUI.ScrollView.PaddingAnEndAddsAWholeWindowOfTravelBeyondTheContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollViewPadScrollingTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox(0.0f));
	FillBox(*Box, 600.0f);
	UUIScrollView* View = Box->GetScrollView();
	if (!TestNotNull(TEXT("the box has a view"), View))
	{
		return false;
	}
	const double Window = View->GetViewportSize().Y;
	const double PlainExtent = View->GetScrollableExtent().Y;
	if (!TestTrue(TEXT("the box has a window with something behind it"), Window > 0.0 && PlainExtent > 0.0))
	{
		return false;
	}

	// One whole window per pad, which is Slate's arithmetic -- not the half a window the sketch for
	// this feature described. The two are independent, so each is worth its own claim.
	Box->SetBackPadScrolling(true);
	TestEqual(TEXT("a leading pad adds a whole window of travel"),
		View->GetScrollableExtent().Y, PlainExtent + Window);
	Box->SetFrontPadScrolling(true);
	TestEqual(TEXT("and a trailing pad adds another"),
		View->GetScrollableExtent().Y, PlainExtent + Window * 2.0);

	// The leading pad also moves where offset zero IS, which is the half that cannot be bolted on per
	// caller: at rest the content now starts a window further in, so scrolling forward by that much
	// is what puts its first edge back at the window's own.
	Box->SetFrontPadScrolling(false);
	Box->ScrollToStart();
	const double RestingPosition = Box->ContentNode->GetAnchoredPosition().Y;
	Box->SetBackPadScrolling(false);
	Box->ScrollToStart();
	TestEqual(TEXT("dropping the leading pad moves the resting position back by a window"),
		Box->ContentNode->GetAnchoredPosition().Y, RestingPosition + Window);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollViewOverscrollQueryTest,
	"DreamGUI.ScrollView.OverscrollIsReportedInUnitsAndAsAShareOfTheWindow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollViewOverscrollQueryTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox(0.0f));
	FillBox(*Box, 600.0f);
	UUIScrollView* View = Box->GetScrollView();
	if (!TestNotNull(TEXT("the box has a view"), View) || Box->ContentNode == nullptr)
	{
		return false;
	}
	const double Window = View->GetViewportSize().Y;
	const double Extent = View->GetScrollableExtent().Y;

	// Offset zero is where the content rests, so reading the rect there gives the fixed point every
	// other position in this test is measured from -- without reaching into the behaviour's protected
	// arithmetic to ask it.
	Box->ScrollToStart();
	const double RestingY = Box->ContentNode->GetAnchoredPosition().Y;
	TestEqual(TEXT("a view sitting in range is not overscrolled"), Box->GetOverscrollOffset(), 0.0f);

	// Past the END: the offset runs downward, and the content's own Y runs up, so an offset beyond
	// the extent is a LARGER content Y. Written straight onto the rect because that is what a drag
	// does -- the setters all clamp, which is the state this query exists to describe.
	Box->ContentNode->SetAnchoredPosition(FVector2D(
		Box->ContentNode->GetAnchoredPosition().X, RestingY + Extent + 30.0));
	TestEqual(TEXT("past the end reads as a positive distance in local units"),
		Box->GetOverscrollOffset(), 30.0f);
	TestEqual(TEXT("and as that share of the window, in per cent"),
		Box->GetOverscrollPercentage(), static_cast<float>((30.0 / Window) * 100.0));

	// Before the START is the mirror, and it is signed the other way.
	Box->ContentNode->SetAnchoredPosition(FVector2D(
		Box->ContentNode->GetAnchoredPosition().X, RestingY - 20.0));
	TestEqual(TEXT("before the start reads as a negative distance"), Box->GetOverscrollOffset(), -20.0f);

	// Switched off, there is nothing out there to describe: a band that cannot be opened cannot be
	// reported as open, whatever the content's rect happens to say.
	Box->SetAllowOverscroll(false);
	TestEqual(TEXT("with overscroll off the question has no answer but zero"),
		Box->GetOverscrollOffset(), 0.0f);
	TestEqual(TEXT("in either unit"), Box->GetOverscrollPercentage(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxViewFractionTest,
	"DreamGUI.ScrollBox.TheViewFractionAndTheEndOffsetComeFromTheOneMeasurement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxViewFractionTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Empty(MakeBox(0.0f));
	// Content that fits shows all of itself. One, not a fraction over a smaller number -- which is
	// what a naive viewport-over-content would give for a content rect the box floors at the window.
	TestEqual(TEXT("a box with nothing to scroll shows all of it"), Empty->GetViewFraction(), 1.0f);
	TestEqual(TEXT("and its end is where it already is"), Empty->GetScrollOffsetOfEnd(), 0.0f);
	TestEqual(TEXT("and it is at the start of a range of nothing"), Empty->GetViewOffsetFraction(), 0.0f);

	TDreamTestControl<UDreamScrollBox> Box(MakeBox(0.0f));
	FillBox(*Box, 600.0f);
	UUIScrollView* View = Box->GetScrollView();
	if (!TestNotNull(TEXT("the box has a view"), View))
	{
		return false;
	}
	const float Window = static_cast<float>(View->GetViewportSize().Y);
	const float Content = static_cast<float>(View->GetContentSize().Y);
	TestEqual(TEXT("a 200-tall window onto 600 of content shows a third of it"),
		Box->GetViewFraction(), Window / Content);
	TestEqual(TEXT("and the end is the whole scrollable extent"),
		Box->GetScrollOffsetOfEnd(), Content - Window);

	Box->ScrollToEnd();
	TestEqual(TEXT("at the end the offset fraction is one"), Box->GetViewOffsetFraction(), 1.0f);
	TestEqual(TEXT("and the offset is the end offset"), Box->GetScrollOffset(), Box->GetScrollOffsetOfEnd());
	Box->ScrollToStart();
	TestEqual(TEXT("and back at the start it is zero"), Box->GetViewOffsetFraction(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarStateAndAutoHideTest,
	"DreamGUI.ScrollBar.SettingStateMovesThePositionAndTheHandleTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBarStateAndAutoHideTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamScrollBar> Bar(NewObject<UDreamScrollBar>(GetTransientPackage()));
	Bar->StyleSource = EDreamUIStyleSource::Inline;
	Bar->Style.Thickness = 12.0f;
	Bar->MinHandleLength = 0.0f;
	Bar->SetHeight(200.0f);
	Bar->Initialize();
	if (!TestNotNull(TEXT("the bar built its handle"), Bar->HandleNode.Get()) ||
		!TestNotNull(TEXT("and its behaviour"), Bar->BarBehaviour.Get()))
	{
		return false;
	}

	// One call, both numbers. A scroll view always has them together, and writing them one at a time
	// lays the handle out twice for one change -- with an intermediate shape that is a lie.
	Bar->SetState(0.5f, 0.4f);
	TestEqual(TEXT("the position landed"), Bar->GetValue(), 0.5f);
	TestEqual(TEXT("and the visible fraction with it"), Bar->GetHandleSize(), 0.4f);
	TestEqual(TEXT("and the handle was drawn at that fraction of the track"),
		static_cast<float>(Bar->HandleNode->GetSizeDelta().Y), 200.0f * 0.4f);

	// The direction is a whole re-layout, not a field: it decides the track's axis, the handle's rect
	// and which end zero is.
	Bar->SetDirection(EUIScrollbarDirectionType::LeftToRight);
	TestTrue(TEXT("the bar now runs across"), Bar->IsHorizontal());
	TestEqual(TEXT("and the behaviour was told"),
		Bar->BarBehaviour->GetDirectionType(), EUIScrollbarDirectionType::LeftToRight);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarAutoHideTest,
	"DreamGUI.ScrollBar.ABarThatIsNotAlwaysShownStandsDownWhenEverythingFits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBarAutoHideTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamScrollBar> Bar(NewObject<UDreamScrollBar>(GetTransientPackage()));
	Bar->StyleSource = EDreamUIStyleSource::Inline;
	Bar->SetHeight(200.0f);
	Bar->Initialize();
	if (!TestNotNull(TEXT("the bar built its handle"), Bar->HandleNode.Get()))
	{
		return false;
	}

	// The shipped answer, and the reason it is not UMG's: this bar has always drawn whatever it was
	// placed into, so a screen whose bar vanished the moment its list fitted would be a layout that
	// changed under everyone.
	TestTrue(TEXT("a bar is always shown unless the author says otherwise"), Bar->GetAlwaysShowScrollbar());
	Bar->SetState(0.0f, 1.0f, /*bInCollapseIfNecessary*/true);
	TestTrue(TEXT("so a full-length handle does not make it disappear"), Bar->GetWidgetActive());

	// Turned off, a fraction of one means "the window already shows everything", which is the only
	// state there is nothing to scroll in.
	Bar->SetAlwaysShowScrollbar(false);
	TestFalse(TEXT("with nothing to scroll the whole bar stands down"), Bar->GetWidgetActive());

	// Unless the author wanted the groove kept, in which case the thumb is what goes.
	Bar->SetAlwaysShowScrollbarTrack(true);
	TestTrue(TEXT("the track is kept when the author asked for it"), Bar->GetWidgetActive());
	TestFalse(TEXT("but the handle is not"), Bar->HandleNode->GetWidgetActive());

	// Something to scroll again and both come back, whatever the flags say.
	Bar->SetState(0.0f, 0.25f, /*bInCollapseIfNecessary*/true);
	TestTrue(TEXT("a bar with something to say is shown"), Bar->GetWidgetActive());
	TestTrue(TEXT("handle and all"), Bar->HandleNode->GetWidgetActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxLayoutSettersTest,
	"DreamGUI.Layout.ScrollBox.ItsSettersDoTheWorkAPlainFieldWriteWouldSkip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxLayoutSettersTest::RunTest(const FString& Parameters)
{
	// The layout container's knobs were reachable from Blueprint only as variables, which skips
	// whatever the write has to settle. These are the ones where that difference is observable.
	UDreamWidget* Widget = NewObject<UDreamWidget>(GetTransientPackage());
	TStrongObjectPtr<UDreamWidget> Owned(Widget);
	Widget->SetWidth(200.0f);
	Widget->SetHeight(200.0f);
	UDreamLayoutContainerScrollBox* Box = Widget->CreateNewLayoutContainer<UDreamLayoutContainerScrollBox>();
	if (!TestNotNull(TEXT("the widget took a scroll box layout"), Box))
	{
		return false;
	}

	// A negative or non-finite number is not something anybody authored on purpose, and the field
	// write is exactly the road that would have kept it.
	Box->SetScrollSensitivity(-10.0f);
	TestEqual(TEXT("a negative sensitivity is floored, not stored"), Box->GetScrollSensitivity(), 0.0f);
	Box->SetWheelScrollMultiplier(-1.0f);
	TestEqual(TEXT("and so is a negative multiplier"), Box->GetWheelScrollMultiplier(), 0.0f);
	Box->SetNavigationScrollPadding(-4.0f);
	TestEqual(TEXT("and a negative navigation padding"), Box->GetNavigationScrollPadding(), 0.0f);

	// Configured is an ARGUMENT value -- "whatever this box is set up with" -- so storing it would
	// make the property its own answer.
	Box->SetNavigationDestination(EDreamUIScrollDestination::Center);
	Box->SetNavigationDestination(EDreamUIScrollDestination::Configured);
	TestEqual(TEXT("the placeholder destination is refused, leaving the real one"),
		Box->GetNavigationDestination(), EDreamUIScrollDestination::Center);

	// Switching inertia off has to stop what is already flying, or the box keeps travelling under a
	// rule that no longer applies.
	Box->SetScrollVelocity(500.0f);
	Box->SetEnableInertia(false);
	TestEqual(TEXT("switching inertia off stops what was already flying"), Box->GetScrollVelocity(), 0.0f);

	// And switching overscroll off has to close the band: with nothing stepping it, an open one would
	// simply stay open for the rest of this box's life.
	Box->SetAllowOverscroll(false);
	TestEqual(TEXT("switching overscroll off closes whatever band was open"), Box->GetOverscroll(), 0.0f);
	TestEqual(TEXT("and the percentage that goes with it"), Box->GetOverscrollPercentage(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxFocusTest,
	"DreamGUI.ScrollBox.OnlyABoxThatWasAskedToBeFocusableBecomesAFocusTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox());
	UDreamWidget* Face = Box->FaceNode;
	if (!TestNotNull(TEXT("the box has a face"), Face))
	{
		return false;
	}
	// The gate, and the whole reason these events could not simply be added: a box that became a
	// navigation candidate would change where every directional press in an existing screen lands.
	TestFalse(TEXT("a box is not focusable unless an author said so"), Box->GetIsFocusable());
	TestNull(TEXT("so nothing was added to its face that could take focus"),
		Face->GetComponent<UUISelectable>());

	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>());
	Box->OnFocusReceived.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordFocusReceived);
	Box->OnFocusLost.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordFocusLost);

	Box->SetIsFocusable(true);
	Box->ApplyStyle();
	UUISelectable* Selectable = Face->GetComponent<UUISelectable>();
	if (!TestNotNull(TEXT("asking for it puts a focus target on the face"), Selectable))
	{
		return false;
	}
	TestEqual(TEXT("and nothing has been announced yet"), Probe->FocusReceivedCount, 0);

	// The EDGE, not the state: a selectable re-applies its state for a repaint as well as for a
	// change, and a box that announced on every repaint would fire dozens of times for one press.
	Box->HandleFaceSelectionStateChangedForTest(EUISelectableSelectionState::Focused);
	TestEqual(TEXT("taking focus is announced once"), Probe->FocusReceivedCount, 1);
	Box->HandleFaceSelectionStateChangedForTest(EUISelectableSelectionState::Focused);
	TestEqual(TEXT("and re-applying the same state announces nothing"), Probe->FocusReceivedCount, 1);

	Box->HandleFaceSelectionStateChangedForTest(EUISelectableSelectionState::Normal);
	TestEqual(TEXT("losing it is announced once"), Probe->FocusLostCount, 1);
	Box->HandleFaceSelectionStateChangedForTest(EUISelectableSelectionState::Hovered);
	TestEqual(TEXT("and a hover is not a second loss"), Probe->FocusLostCount, 1);

	// Turned back off, the behaviour is KEPT -- destroying it costs the designer a details rebuild --
	// but the box stops answering, which is what "not a focus target" has to mean from outside.
	Box->SetIsFocusable(false);
	Box->HandleFaceSelectionStateChangedForTest(EUISelectableSelectionState::Focused);
	TestEqual(TEXT("a box that stopped being focusable stops announcing"), Probe->FocusReceivedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBoxFocusUpdatedTest,
	"DreamGUI.ScrollBox.FocusLandingOnItsContentIsReportedWithTheWidgetThatTookIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBoxFocusUpdatedTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollBoxParityTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox());
	UDreamWidget* Filler = FillBox(*Box, 600.0f);
	UUIScrollView* View = Box->GetScrollView();
	if (!TestNotNull(TEXT("the box has a view"), View) || !TestNotNull(TEXT("and content"), Filler))
	{
		return false;
	}
	TStrongObjectPtr<UDreamListControlsProbe> Probe(NewObject<UDreamListControlsProbe>());
	Box->OnFocusUpdated.AddDynamic(Probe.Get(), &UDreamListControlsProbe::RecordFocusUpdated);

	// Raised by the navigation reveal, which is the one place that knows both which widget took
	// focus and which scrolling ancestors contain it. Driven directly here, because the reveal is
	// FDreamUINavigationScroll's claim rather than this control's.
	View->NotifyContentFocusMoved(Filler);
	TestEqual(TEXT("focus landing on the content is announced once"), Probe->FocusUpdatedCount, 1);
	TestTrue(TEXT("with the widget that took it"), Probe->LastFocusedWidget == Filler);

	// Not gated on bIsFocusable, and that is the point: the CONTENT is focusable whether or not the
	// box is, so a box nobody made a focus target still hears about focus inside it.
	TestFalse(TEXT("even though the box itself is not a focus target"), Box->GetIsFocusable());

	// The box's own face reaching here is the box TAKING focus, which OnFocusReceived already said.
	View->NotifyContentFocusMoved(Box->FaceNode);
	TestEqual(TEXT("the box's own face is not reported as focus moving inside it"),
		Probe->FocusUpdatedCount, 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
