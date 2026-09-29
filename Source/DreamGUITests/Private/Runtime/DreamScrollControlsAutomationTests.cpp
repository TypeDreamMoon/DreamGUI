// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamScrollBar.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Interaction/UIScrollView.h"
#include "Interaction/UIScrollbar.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The two scrolling controls, aimed at the wiring that fails SILENTLY.
 *
 * Three of the four claims here exist because the shipped behaviours have a default or a habit that
 * is wrong for a code-built tree, and none of them says so at the time:
 *
 *  - UUIScrollView ships with BOTH axes on. A box that only ever scrolls one way drifts sideways the
 *    first time a drag lands, and looks like a physics bug rather than a missing line.
 *  - UUIScrollbar places its handle with RATIO anchors, and an anchor setter resolves the parent's
 *    span at write time -- against a stretched parent's zero SizeDelta on every frame that is not a
 *    full layout. Four defects in this library have come from that shape. So the handle's rect must
 *    be absolute numbers with a point anchor, and these tests assert exactly that.
 *  - A UUISelectable with no colours of its own re-tints its target white on the first hover.
 *
 * Everything runs headless: no world, no registration, no layout pass. Sizes are therefore authored
 * on the control before Initialize (which is what a designer or a .dui line does anyway) so every
 * derived rect below is an exact number rather than whatever a default happened to be, and colours
 * are read from behaviour state, since the tween manager returns null without a world.
 */
namespace DreamScrollControlsTestLocal
{
	/** The box every test here starts from: a known rect, an inline style, a bar that always shows. */
	UDreamScrollBox* MakeBox(EDreamPanelOrientation InOrientation, float InThickness)
	{
		UDreamScrollBox* Box = NewObject<UDreamScrollBox>(GetTransientPackage());
		// Inline rather than the sheet: a project sheet in the running editor would otherwise decide
		// what these assertions are comparing against.
		Box->StyleSource = EDreamUIStyleSource::Inline;
		Box->Orientation = InOrientation;
		Box->ScrollBarVisibility = EDreamScrollBoxScrollbarVisibility::Permanent;
		Box->Style.Bar.Thickness = InThickness;
		Box->SetWidth(300.0f);
		Box->SetHeight(200.0f);
		Box->Initialize();
		return Box;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlScrollBoxTest,
	"DreamGUI.Controls.ScrollBox.TheViewportHoldsTheScrollAndOnlyTheAuthoredAxisIsOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlScrollBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollControlsTestLocal;

	TDreamTestControl<UDreamScrollBox> Box(MakeBox(EDreamPanelOrientation::Vertical, 14.0f));
	Box->Style.Padding = FMargin(3.0f, 4.0f, 5.0f, 6.0f);
	Box->Style.Background = FColor(11, 22, 33, 255);
	Box->ApplyStyle();

	if (!TestNotNull(TEXT("the face exists"), Box->FaceNode.Get()) ||
		!TestNotNull(TEXT("the viewport exists"), Box->ViewportNode.Get()) ||
		!TestNotNull(TEXT("the content exists"), Box->ContentNode.Get()) ||
		!TestNotNull(TEXT("the scroll behaviour exists"), Box->ScrollView.Get()) ||
		!TestNotNull(TEXT("the content stack exists"), Box->ContentStack.Get()) ||
		!TestNotNull(TEXT("the bar exists"), Box->ScrollBarNode.Get()))
	{
		return false;
	}

	// The shape the rest of the control depends on: content INSIDE the viewport (so the viewport is
	// what the behaviour slides it in), and the bar a SIBLING of the viewport rather than a child.
	// The sibling part is load-bearing -- a scroll view only accepts drags from inside its own
	// widget, so a bar hung under the viewport would scroll the content on every grab of the handle.
	TestTrue(TEXT("the viewport lives inside the face"),
		(UObject*)Box->ViewportNode->GetParent() == (UObject*)Box->FaceNode.Get());
	TestTrue(TEXT("the content lives inside the viewport"),
		(UObject*)Box->ContentNode->GetParent() == (UObject*)Box->ViewportNode.Get());
	TestTrue(TEXT("the bar is the viewport's sibling, not its child"),
		(UObject*)Box->ScrollBarNode->GetParent() == (UObject*)Box->FaceNode.Get());
	TestTrue(TEXT("the behaviour scrolls the content node"),
		(UObject*)Box->ScrollView->GetContent() == (UObject*)Box->ContentNode.Get());

	// The drift trap. Both axes are stated, because the behaviour's default is both-on and a vertical
	// box that never turns the other one off slides sideways under the first drag.
	TestFalse(TEXT("the horizontal axis is explicitly off"), Box->ScrollView->GetHorizontal());
	TestTrue(TEXT("the vertical axis is explicitly on"), Box->ScrollView->GetVertical());
	// The control writes the content's rect, so the offset has to live where both parties read it.
	TestEqual(TEXT("the scroll offset is an anchored position, not a relative location"),
		Box->ScrollView->GetCoordinateMode(), EDreamScrollCoordinateMode::AnchoredPosition);

	// A viewport that does not clip is a pile, not a scroll box.
	TestEqual(TEXT("the face clips to its own silhouette"),
		Box->FaceNode->GetAuthoredClipping(), EDreamWidgetClipping::ClipToBounds);
	TestEqual(TEXT("the viewport clips its content"),
		Box->ViewportNode->GetAuthoredClipping(), EDreamWidgetClipping::ClipToBounds);

	// The style's numbers arrived: the face wears the background, the stack wears the padding (it
	// belongs to what SCROLLS, not to the window), and the bar's thickness became the gutter.
	if (UDreamVisual* FaceVisual = Box->FaceNode->GetVisual())
	{
		TestEqual(TEXT("the face wears the style's background"), FaceVisual->GetColor(), FColor(11, 22, 33, 255));
	}
	TestEqual(TEXT("the padding reached the content stack"), Box->ContentStack->Padding, FMargin(3.0f, 4.0f, 5.0f, 6.0f));
	TestEqual(TEXT("the stack runs the way the box scrolls"),
		Box->ContentStack->Orientation, EDreamPanelOrientation::Vertical);
	// The gutter is a DELTA on a stretched axis, which is the only spelling that survives the box
	// being resized: the arranger subtracts it from whatever span the box has that frame. Asserting
	// an arranged width instead would be asserting that a layout pass ran, and none does here --
	// headless, a stretched axis reads back its delta.
	TestEqual(TEXT("the bar is as thick as the style says"), Box->ScrollBarNode->GetWidth(), 14.0f);
	TestEqual(TEXT("and it stretches down the box's height"),
		static_cast<float>(Box->ScrollBarNode->GetAnchorMin().Y), 0.0f);
	TestEqual(TEXT("with nothing taken off that axis"),
		static_cast<float>(Box->ScrollBarNode->GetSizeDelta().Y), 0.0f);
	TestEqual(TEXT("the viewport gave up exactly the bar's thickness"),
		static_cast<float>(Box->ViewportNode->GetSizeDelta().X), -14.0f);
	TestEqual(TEXT("and nothing on the other axis"),
		static_cast<float>(Box->ViewportNode->GetSizeDelta().Y), 0.0f);
	// Absolute, off the viewport's RESOLVED width -- no span left for an anchor setter to resolve
	// wrongly. This assertion used to read -14, which is the viewport's size DELTA and not any width
	// at all: a stretched node published its delta as its resolved size, so the content rect was
	// literally negative-width and every child of it drew nowhere. Both halves are fixed at the
	// source (UDreamWidget::SetAnchoredPositionAndSizeDelta and ::SetWidth); 300 less the bar is 286.
	TestEqual(TEXT("the content is sized against the viewport that came out"),
		static_cast<float>(Box->ContentNode->GetSizeDelta().X), 286.0f);
	TestEqual(TEXT("the content's cross axis is a point anchor -- min"),
		static_cast<float>(Box->ContentNode->GetAnchorMin().X), 0.5f);
	TestEqual(TEXT("the content's cross axis is a point anchor -- max"),
		static_cast<float>(Box->ContentNode->GetAnchorMax().X), 0.5f);

	// The bar is a real control, not a node: proof that the nested user widget was initialized, which
	// nothing does for a code-built tree -- the walk that initializes nested widgets belongs to
	// instancing a class template, and a class with a code hierarchy has none.
	TestNotNull(TEXT("the nested bar built its own track"), Box->ScrollBarNode->TrackNode.Get());
	TestNotNull(TEXT("the nested bar built its own handle"), Box->ScrollBarNode->HandleNode.Get());
	TestTrue(TEXT("the bar follows this box's view"),
		(UObject*)Box->ScrollBarNode->GetScrollView() == (UObject*)Box->ScrollView.Get());
	TestEqual(TEXT("the bar wears the box's own bar style"),
		Box->ScrollBarNode->Style.Thickness, 14.0f);
	TestEqual(TEXT("a vertical box hands its bar the vertical direction"),
		Box->ScrollBarNode->Direction, EUIScrollbarDirectionType::TopToBottom);

	// AutoHide against a box with nothing in it: the content is exactly the viewport, so there is
	// nothing to scroll and no reason to spend a gutter on saying so.
	TDreamTestControl<UDreamScrollBox> Empty(NewObject<UDreamScrollBox>(GetTransientPackage()));
	Empty->StyleSource = EDreamUIStyleSource::Inline;
	Empty->SetWidth(300.0f);
	Empty->SetHeight(200.0f);
	Empty->Initialize();
	if (!TestNotNull(TEXT("the empty box built a bar to hide"), Empty->ScrollBarNode.Get()) ||
		!TestNotNull(TEXT("the empty box built a viewport"), Empty->ViewportNode.Get()))
	{
		return false;
	}
	TestFalse(TEXT("an empty box auto-hides its bar"), Empty->ScrollBarNode->GetWidgetActive());
	TestEqual(TEXT("and the viewport keeps the whole width -- no gutter taken"),
		static_cast<float>(Empty->ViewportNode->GetSizeDelta().X), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlScrollBoxOrientationTest,
	"DreamGUI.Controls.ScrollBox.OrientationMovesTheAxisTheStackAndTheBarTogether",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlScrollBoxOrientationTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollControlsTestLocal;

	// The single property that replaced BP_HorizontalScrollView and BP_VerticalScrollView. Everything
	// the two presets disagreed about has to move with it, in one go: the behaviour's axes, the way
	// the content stacks, and which edge the bar takes its gutter from.
	TDreamTestControl<UDreamScrollBox> Box(MakeBox(EDreamPanelOrientation::Horizontal, 14.0f));
	if (!TestNotNull(TEXT("the scroll behaviour exists"), Box->ScrollView.Get()) ||
		!TestNotNull(TEXT("the content stack exists"), Box->ContentStack.Get()) ||
		!TestNotNull(TEXT("the viewport exists"), Box->ViewportNode.Get()) ||
		!TestNotNull(TEXT("the content exists"), Box->ContentNode.Get()) ||
		!TestNotNull(TEXT("the bar exists"), Box->ScrollBarNode.Get()))
	{
		return false;
	}

	TestTrue(TEXT("the horizontal axis is explicitly on"), Box->ScrollView->GetHorizontal());
	TestFalse(TEXT("the vertical axis is explicitly off"), Box->ScrollView->GetVertical());
	TestEqual(TEXT("the stack runs the way the box scrolls"),
		Box->ContentStack->Orientation, EDreamPanelOrientation::Horizontal);
	TestEqual(TEXT("a horizontal box hands its bar the horizontal direction"),
		Box->ScrollBarNode->Direction, EUIScrollbarDirectionType::LeftToRight);

	// The gutter moved to the bottom edge: the bar spans the width and is thickness-tall, and the
	// viewport lost the thickness off its height instead of its width.
	TestEqual(TEXT("the bar stretches across the box's width"),
		static_cast<float>(Box->ScrollBarNode->GetSizeDelta().X), 0.0f);
	TestEqual(TEXT("the bar is as thick as the style says"), Box->ScrollBarNode->GetHeight(), 14.0f);
	TestEqual(TEXT("the viewport keeps its full width"),
		static_cast<float>(Box->ViewportNode->GetSizeDelta().X), 0.0f);
	TestEqual(TEXT("and gave up the thickness off its height"),
		static_cast<float>(Box->ViewportNode->GetSizeDelta().Y), -14.0f);
	// Shifted up by half the gutter, so the top edge stayed where it was.
	TestEqual(TEXT("the viewport shifted by half the gutter to keep its top edge"),
		static_cast<float>(Box->ViewportNode->GetAnchoredPosition().Y), 7.0f);
	// The cross axis, resolved rather than published as a delta -- see the vertical twin above for
	// what -14 used to mean here. 200 less the bar is 186.
	TestEqual(TEXT("the content is sized against the viewport that came out"),
		static_cast<float>(Box->ContentNode->GetSizeDelta().Y), 186.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlScrollBarTest,
	"DreamGUI.Controls.ScrollBar.TheHandleIsAbsoluteGeometryThatTravelsWithTheValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlScrollBarTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamScrollBar> Bar(NewObject<UDreamScrollBar>(GetTransientPackage()));
	Bar->StyleSource = EDreamUIStyleSource::Inline;
	Bar->Style.Thickness = 12.0f;
	Bar->Style.HandleNormal = FColor(1, 2, 3, 255);
	Bar->Direction = EUIScrollbarDirectionType::BottomToTop;
	Bar->HandleSize = 0.25f;
	// Off, so the fraction under test is the authored one and not a floor. The floor gets its own
	// assertion below, where it is the point.
	Bar->MinHandleLength = 0.0f;
	// The length belongs to whoever places a bar; authored here so every rect below is exact.
	Bar->SetHeight(200.0f);
	Bar->Initialize();

	if (!TestNotNull(TEXT("the track exists"), Bar->TrackNode.Get()) ||
		!TestNotNull(TEXT("the handle exists"), Bar->HandleNode.Get()) ||
		!TestNotNull(TEXT("the behaviour is always there"), Bar->BarBehaviour.Get()))
	{
		return false;
	}
	// The handle's PARENT is what the behaviour measures the value against, so this nesting is the
	// whole coordinate system: a scroll bar handle rides the entire track, because its own length
	// already takes the travel out (a slider's handle area is inset instead, for the opposite reason).
	TestTrue(TEXT("the handle lives inside the track"),
		(UObject*)Bar->HandleNode->GetParent() == (UObject*)Bar->TrackNode.Get());
	TestTrue(TEXT("the behaviour was handed that handle"),
		(UObject*)Bar->BarBehaviour->GetHandle() == (UObject*)Bar->HandleNode.Get());
	TestEqual(TEXT("the behaviour was handed the direction too"),
		Bar->BarBehaviour->GetDirectionType(), EUIScrollbarDirectionType::BottomToTop);

	// The style's numbers arrived: thickness is the bar's cross axis, and the track fills it.
	TestEqual(TEXT("the bar is as thick as the style says"), Bar->GetWidth(), 12.0f);
	// The track is stretched over the bar: its DELTA is zero, meaning "exactly the bar", and its
	// resolved length is the bar's -- which it now answers with even headless, because a parent's
	// resolved size invalidates its anchor-driven children at the source (UDreamWidget::SetWidth).
	// That is what let the handle maths move down into UUIScrollbar and read the TRACK, where it
	// belongs, instead of being re-done up here off the control's own rect.
	TestEqual(TEXT("the track fills the bar across"),
		static_cast<float>(Bar->TrackNode->GetSizeDelta().X), 0.0f);
	TestEqual(TEXT("and along it"),
		static_cast<float>(Bar->TrackNode->GetSizeDelta().Y), 0.0f);
	const float TrackLength = static_cast<float>(Bar->GetHeight());
	if (!TestEqual(TEXT("the bar was placed at the length the handle maths uses"), TrackLength, 200.0f))
	{
		return false;
	}
	TestEqual(TEXT("and the stretched track resolves to it with no layout pass"),
		Bar->TrackNode->GetHeight(), 200.0f);

	// The white trap: the handle's colour comes from the SELECTABLE, which re-tints its target on
	// every state change. A behaviour that was never given colours ships a white handle.
	TestEqual(TEXT("the handle's normal colour is the style's"),
		Bar->BarBehaviour->GetNormalColor(), FColor(1, 2, 3, 255));
	TestTrue(TEXT("the pointer transition tints the handle, not the track"),
		(UObject*)Bar->BarBehaviour->GetTransitionTarget()
			== (UObject*)(Bar->HandleNode != nullptr ? Bar->HandleNode->GetVisual() : nullptr));

	// The geometry claim. The handle's rect is ABSOLUTE numbers off the track's live size with the
	// anchor collapsed to a POINT on the track's start edge -- never the ratio anchor the base
	// component writes, whose setter resolves the parent's span at write time and reads a stretched
	// track's zero SizeDelta on every frame that is not a full layout.
	TestEqual(TEXT("the handle's axis is a point anchor -- min"),
		static_cast<float>(Bar->HandleNode->GetAnchorMin().Y), 0.0f);
	TestEqual(TEXT("the handle's axis is a point anchor -- max"),
		static_cast<float>(Bar->HandleNode->GetAnchorMax().Y), 0.0f);
	TestEqual(TEXT("the handle covers the authored fraction of the track"),
		static_cast<float>(Bar->HandleNode->GetSizeDelta().Y), TrackLength * 0.25f);
	TestEqual(TEXT("and spans the track across it"),
		static_cast<float>(Bar->HandleNode->GetSizeDelta().X), 12.0f);
	TestEqual(TEXT("value zero puts the handle on the track's start"),
		static_cast<float>(Bar->HandleNode->GetAnchoredPosition().Y), 0.0f);

	// It travels the track LESS its own length, which is what makes value 1 land the far edge of the
	// handle on the far end of the track rather than a quarter past it.
	const float Travel = TrackLength * 0.75f;
	Bar->SetValue(1.0f);
	TestEqual(TEXT("value one puts the handle at the end of its travel"),
		static_cast<float>(Bar->HandleNode->GetAnchoredPosition().Y), Travel);
	Bar->SetValue(0.5f);
	TestEqual(TEXT("half way is half the travel"),
		static_cast<float>(Bar->HandleNode->GetAnchoredPosition().Y), Travel * 0.5f);
	TestEqual(TEXT("and the length never moved"),
		static_cast<float>(Bar->HandleNode->GetSizeDelta().Y), TrackLength * 0.25f);

	// A value that arrives from the behaviour -- which is the road every drag and every page-jump
	// takes -- reaches the control's own mirror and re-lays the handle out. Without the round trip
	// the handle would be left where the base component's ratio anchors put it.
	Bar->BarBehaviour->SetValue(0.25f);
	TestEqual(TEXT("a value pushed through the behaviour reaches the control"), Bar->Value, 0.25f);
	TestEqual(TEXT("and the control re-placed the handle after it"),
		static_cast<float>(Bar->HandleNode->GetAnchoredPosition().Y), Travel * 0.25f);

	// The direction flip: the same value, the other end. Nothing else about the bar changes.
	Bar->Direction = EUIScrollbarDirectionType::TopToBottom;
	Bar->Value = 0.0f;
	Bar->ApplyStyle();
	TestEqual(TEXT("top-to-bottom puts value zero at the far end"),
		static_cast<float>(Bar->HandleNode->GetAnchoredPosition().Y), Travel);

	// The minimum length is applied by raising the FRACTION, not by drawing a longer rect: the
	// behaviour derives its drag scale from Size, so a handle drawn longer than Size claims would
	// drag at the wrong rate for the whole length of a long list.
	Bar->MinHandleLength = 100.0f;
	Bar->HandleSize = 0.1f;
	Bar->ApplyStyle();
	TestEqual(TEXT("the authored fraction is left alone"), Bar->BarBehaviour->GetSize(), 0.1f);
	TestEqual(TEXT("and the floor is what the behaviour actually draws and drags with"),
		Bar->BarBehaviour->GetEffectiveSize(), 0.5f);
	TestEqual(TEXT("so the drawn handle and the drag scale agree"),
		static_cast<float>(Bar->HandleNode->GetSizeDelta().Y), 100.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlScrollBarDrivesBoxTest,
	"DreamGUI.Controls.ScrollBar.ItDrivesAScrollViewAndFollowsItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlScrollBarDrivesBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamScrollControlsTestLocal;

	// UUIScrollViewWithScrollbar is the shipped coupling and would have been the thing to compose,
	// but its viewport and scrollbar fields are private with no setters, so a control that builds its
	// own tree cannot hand it its parts. What it does is two subscriptions; this asserts that the
	// two-way link the bar carries instead behaves the same -- in both directions, and without the
	// ring a naive pair of pushes would make.
	TDreamTestControl<UDreamScrollBox> Box(MakeBox(EDreamPanelOrientation::Vertical, 14.0f));
	if (!TestNotNull(TEXT("the box built its bar"), Box->ScrollBarNode.Get()) ||
		!TestNotNull(TEXT("the box has a view to drive"), Box->GetScrollView()))
	{
		return false;
	}

	// Something to actually scroll. Without it this box has a 200-tall window over 200-tall content,
	// and a view with nowhere to go now reports progress ZERO however it is driven -- which is the
	// right answer (a bar over content that fits sits at its start covering the whole track) and was
	// not the old one: the old UpdateProgress skipped an axis whose range was degenerate, so the
	// authored 1.0 simply stayed there and the test read it back as if the view had moved.
	UDreamWidget* Filler = NewObject<UDreamWidget>(Box->GetContentNode());
	Filler->SetWidth(200.0f);
	Filler->SetHeight(600.0f);
	Box->AddContent(Filler);
	if (!TestTrue(TEXT("the box now has somewhere to scroll"),
		Box->GetScrollView()->GetScrollableExtent().Y > 0.0))
	{
		return false;
	}

	// A bar the box did not build, pointed at the same view: the standalone case, which is what makes
	// Native.ScrollBar useful on its own rather than only as a part of a box.
	TDreamTestControl<UDreamScrollBar> Loose(NewObject<UDreamScrollBar>(GetTransientPackage()));
	Loose->StyleSource = EDreamUIStyleSource::Inline;
	Loose->Direction = EUIScrollbarDirectionType::TopToBottom;
	Loose->SetHeight(200.0f);
	Loose->Initialize();
	Loose->SetScrollView(Box->GetScrollView());
	TestTrue(TEXT("the loose bar took the view"),
		(UObject*)Loose->GetScrollView() == (UObject*)Box->GetScrollView());

	// View to bars: one push, both bars follow, and neither of them pushes back (which would be the
	// same number going round again, and is why the follow path is the silent one).
	Box->SetScrollProgress(1.0f);
	TestEqual(TEXT("the box's own bar followed the view"), Box->ScrollBarNode->GetValue(), 1.0f);
	TestEqual(TEXT("so did the loose one"), Loose->GetValue(), 1.0f);

	// Bar to view: dragging a bar is this call, and the view has to end up where the bar says.
	Loose->SetValue(0.25f);
	TestEqual(TEXT("the view took the loose bar's value"), Box->GetScrollProgress(), 0.25f);
	TestEqual(TEXT("and the box's mirror property followed"), Box->ScrollProgress, 0.25f);
	TestEqual(TEXT("and the other bar came along with the view"), Box->ScrollBarNode->GetValue(), 0.25f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
