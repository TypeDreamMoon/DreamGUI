// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamExpandableArea.h"
#include "Controls/DreamProgressBar.h"
#include "Controls/DreamRadioButton.h"
#include "Controls/DreamScrollBar.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamSpinBox.h"
#include "Controls/DreamTabView.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UISlider.h"
#include "Interaction/UIToggle.h"
#include "DreamDialogTestTypes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The UMG-parity round: the knobs and events each control was missing against its UMG counterpart,
 * asserted the way the rest of the control suite asserts things -- at the wiring that fails SILENTLY.
 *
 * A knob the control never pushes is the recurring shape of that failure, and it is invisible in
 * every screenshot: the property holds the number the author typed, the behaviour holds the
 * library's default, and the control does what the default says forever. Several tests below are
 * therefore "the authored value reached the behaviour", which is a claim about a PUSH rather than
 * about a pixel.
 *
 * Everything runs headless: no world, no registration, no layout pass, no event system. That shapes
 * three things. Colours are read from behaviour state rather than off a visual (the tween manager
 * returns null without a world). Anything that needs the event system -- focus, the popup layer --
 * is asserted up to the point where the event system would be asked, because a null one is the
 * documented headless answer and not a failure. And a per-frame animation is driven by calling the
 * tick directly, which is exactly what the manager would do.
 */
namespace DreamControlParityTestLocal
{
	template<class T>
	T* Make()
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->Initialize();
		return Control;
	}

	FString TextOf(const UDreamWidget* InNode)
	{
		const UDreamText* TextVisual = InNode != nullptr ? Cast<UDreamText>(InNode->GetVisual()) : nullptr;
		return TextVisual != nullptr ? TextVisual->GetText().ToString() : FString();
	}

	/** A pointer event of a given kind. Enough for the click-method questions, which read two fields. */
	UDreamPointerEventData* MakeEvent(EDreamUIPointerInputType InType, bool bInDragging)
	{
		UDreamPointerEventData* Data = NewObject<UDreamPointerEventData>(GetTransientPackage());
		Data->InputType = InType;
		Data->bIsDragging = bInDragging;
		return Data;
	}
}

/**
 * WHEN a click fires is three enums, and which one applies is the input's kind.
 *
 * The three resolvers are asserted rather than a synthesized click, and deliberately: a click's
 * DELIVERY is the event system's (a press and a release on the same widget, with a drag state
 * between them), and a headless test that faked one would be asserting its own fake. What the
 * control layer decides is narrower and is exactly this -- given an event, does the down fire, does
 * the up, does the click.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSelectableClickMethodTest,
	"DreamGUI.Interaction.Selectable.TheClickMethodDecidesWhetherTheDownTheUpOrTheClickFires",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSelectableClickMethodTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlParityTestLocal;

	TStrongObjectPtr<UDreamButton> Button(Make<UDreamButton>());
	UUIButton* Behaviour = Button->ButtonBehaviour.Get();
	if (!TestNotNull(TEXT("the behaviour is always there"), Behaviour))
	{
		return false;
	}
	UDreamPointerEventData* Pointer = MakeEvent(EDreamUIPointerInputType::Pointer, /*bDragging*/false);
	UDreamPointerEventData* Navigation = MakeEvent(EDreamUIPointerInputType::Navigation, /*bDragging*/false);

	// DownAndUp, the desktop's rule and the default: the CLICK fires it and neither half alone does.
	TestFalse(TEXT("DownAndUp does not fire on the press"), Behaviour->ShouldClickOnDown(Pointer));
	TestFalse(TEXT("DownAndUp does not fire on the release"), Behaviour->ShouldClickOnUp(Pointer));
	TestTrue(TEXT("DownAndUp fires on the click"), Behaviour->ShouldClickOnClick(Pointer));

	// MouseDown: the press IS the click, and then the click itself must NOT fire again -- firing
	// twice is the whole risk of adding a second entry point.
	Behaviour->SetClickMethod(EDreamUIClickMethod::MouseDown);
	TestTrue(TEXT("MouseDown fires on the press"), Behaviour->ShouldClickOnDown(Pointer));
	TestFalse(TEXT("...and not a second time on the click"), Behaviour->ShouldClickOnClick(Pointer));

	Behaviour->SetClickMethod(EDreamUIClickMethod::MouseUp);
	TestTrue(TEXT("MouseUp fires on the release"), Behaviour->ShouldClickOnUp(Pointer));
	TestFalse(TEXT("...and not again on the click"), Behaviour->ShouldClickOnClick(Pointer));

	// PreciseClick is DownAndUp narrowed by the cancel gesture: a press that turned into a drag is a
	// scroll, not a choice, which is what a list of buttons inside a scroll box needs.
	Behaviour->SetClickMethod(EDreamUIClickMethod::PreciseClick);
	TestTrue(TEXT("PreciseClick fires on a click that never dragged"), Behaviour->ShouldClickOnClick(Pointer));
	UDreamPointerEventData* Dragged = MakeEvent(EDreamUIPointerInputType::Pointer, /*bDragging*/true);
	TestFalse(TEXT("...and refuses one that did"), Behaviour->ShouldClickOnClick(Dragged));

	// A NAVIGATION event is the gamepad's, so PressMethod answers it -- whatever ClickMethod says.
	Behaviour->SetClickMethod(EDreamUIClickMethod::MouseDown);
	Behaviour->SetPressMethod(EDreamUIPressMethod::DownAndUp);
	TestFalse(TEXT("a navigation press is not the mouse's business"), Behaviour->ShouldClickOnDown(Navigation));
	TestTrue(TEXT("the gamepad's own method answers it"), Behaviour->ShouldClickOnClick(Navigation));
	Behaviour->SetPressMethod(EDreamUIPressMethod::ButtonPress);
	TestTrue(TEXT("ButtonPress fires on the navigation press"), Behaviour->ShouldClickOnDown(Navigation));
	TestFalse(TEXT("...and not again on the click"), Behaviour->ShouldClickOnClick(Navigation));
	return true;
}

/**
 * The button's three input rules reach the selectable that enforces them.
 *
 * The recurring silent failure: the control holds what the author typed and the behaviour holds the
 * library's default, so the control does what the default says forever. On the TEMPLATE road it is
 * worse still -- the selectable is one the control just added a moment ago.
 *
 * FOCUS is absent on purpose. UMG's IsFocusable is UDreamWidget's own bIsFocusable here, carried by
 * every widget in the framework, so this control has no flag of its own to push -- and a member of
 * that name would shadow the base one, which UHT refuses outright.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonInputRulesReachTheBehaviourTest,
	"DreamGUI.Controls.Button.TheClickMethodsReachTheBehaviour",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonInputRulesReachTheBehaviourTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDreamButton> Button(NewObject<UDreamButton>(GetTransientPackage()));
	Button->ClickMethod = EDreamUIClickMethod::MouseDown;
	Button->TouchMethod = EDreamUITouchMethod::PreciseTap;
	Button->PressMethod = EDreamUIPressMethod::ButtonRelease;
	Button->Initialize();

	UUIButton* Behaviour = Button->ButtonBehaviour.Get();
	if (!TestNotNull(TEXT("the behaviour is always there"), Behaviour))
	{
		return false;
	}
	TestEqual(TEXT("the authored click method arrived"),
		Behaviour->GetClickMethod(), EDreamUIClickMethod::MouseDown);
	TestEqual(TEXT("and the touch method"), Behaviour->GetTouchMethod(), EDreamUITouchMethod::PreciseTap);
	TestEqual(TEXT("and the press method"), Behaviour->GetPressMethod(), EDreamUIPressMethod::ButtonRelease);

	// And the setters push, which is the other half: a runtime write must not merely move the number.
	Button->SetClickMethod(EDreamUIClickMethod::PreciseClick);
	TestEqual(TEXT("the setter pushes too"), Behaviour->GetClickMethod(), EDreamUIClickMethod::PreciseClick);
	Button->SetTouchMethod(EDreamUITouchMethod::Down);
	TestEqual(TEXT("and so does the touch one"), Behaviour->GetTouchMethod(), EDreamUITouchMethod::Down);
	Button->SetPressMethod(EDreamUIPressMethod::ButtonPress);
	TestEqual(TEXT("and the press one"), Behaviour->GetPressMethod(), EDreamUIPressMethod::ButtonPress);
	return true;
}

/**
 * The radio button's third state, which its sibling check box has always had.
 *
 * One of the pair could say "I do not know" and the other could not, and a mixed multi-select is as
 * ordinary for a radio group as for a check box. The rule is the toggle's, word for word: authorable
 * but never clickable-into, the behaviour underneath parked at unchecked so the GROUP reads it as
 * not-selected, and the dot wearing the checked colour so the state is not mistaken for "no".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioButtonTriStateTest,
	"DreamGUI.Controls.RadioButton.TheThirdStateIsAuthorableAndAClickLeavesItByBecomingChecked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRadioButtonTriStateTest::RunTest(const FString& Parameters)
{
	const FDreamRadioButtonStyle Defaults;

	TStrongObjectPtr<UDreamRadioButton> Radio(NewObject<UDreamRadioButton>(GetTransientPackage()));
	Radio->CheckedState = EDreamCheckState::Undetermined;
	Radio->Initialize();

	UUIToggle* Behaviour = Radio->ToggleBehaviour.Get();
	if (!TestNotNull(TEXT("the behaviour is always there"), Behaviour))
	{
		return false;
	}
	TestEqual(TEXT("the authored third state survived initialize"),
		Radio->GetCheckedState(), EDreamCheckState::Undetermined);
	TestFalse(TEXT("its bool projection is false"), Radio->GetIsOn());
	// The GROUP's reading, and the reason the behaviour stays two-state: an undetermined radio must
	// neither hold the group's selection nor stop a sibling taking it.
	TestFalse(TEXT("the behaviour beneath reads unchecked"), Behaviour->GetValue());
	// Not mistaken for "no": the dot wears the CHOSEN colour while the state stands.
	TestEqual(TEXT("the dot wears the checked colour"), Behaviour->GetOffColor(), Defaults.DotChecked);

	// A click arrives at the behaviour as unchecked -> checked, which is how the state is left and
	// why it can never be entered by clicking.
	Behaviour->SetValue(true);
	TestEqual(TEXT("a click leaves it as Checked"), Radio->GetCheckedState(), EDreamCheckState::Checked);
	TestTrue(TEXT("and the bool spelling followed"), Radio->GetIsOn());
	TestEqual(TEXT("and the dot's off colour is the ordinary one again"),
		Behaviour->GetOffColor(), Defaults.DotUnchecked);

	// The compatibility spelling still drives the real one, both ways.
	Radio->SetIsOn(false);
	TestEqual(TEXT("the bool setter drives the tri-state"),
		Radio->GetCheckedState(), EDreamCheckState::Unchecked);
	return true;
}

/**
 * The slider's UMG knobs reach the behaviour, and the controller lock is a real gate.
 *
 * RequiresControllerLock is the one with teeth: without it a row of sliders is a trap, because the
 * first one swallows every left and right and the stick can never leave it. The capture is asserted
 * through its own events, which is the seam a consumer uses to stop reacting mid-drag.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderCaptureAndStepTest,
	"DreamGUI.Controls.Slider.TheStepAndTheControllerLockReachTheBehaviourAndTheLockSpeaks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderCaptureAndStepTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UDreamSlider> Slider(NewObject<UDreamSlider>(GetTransientPackage()));
	Slider->MaxValue = 10.0f;
	Slider->StepSize = 0.5f;
	Slider->bMouseUsesStep = true;
	Slider->bRequiresControllerLock = true;
	Slider->Initialize();

	UUISlider* Behaviour = Slider->SliderBehaviour.Get();
	if (!TestNotNull(TEXT("the behaviour is always there"), Behaviour))
	{
		return false;
	}
	TestEqual(TEXT("the authored step arrived"), Behaviour->GetStepSize(), 0.5f);
	TestTrue(TEXT("and the rule that spends it"), Behaviour->GetMouseUsesStep());
	TestTrue(TEXT("and the lock"), Behaviour->GetRequiresControllerLock());
	TestFalse(TEXT("a slider starts uncaptured"), Slider->IsControllerCaptured());

	// The capture speaks, on both edges and exactly once each.
	int32 Began = 0;
	int32 Ended = 0;
	Slider->SliderBehaviour->GetOnControllerCaptureBeginEvent().AddLambda([&Began]() { ++Began; });
	Slider->SliderBehaviour->GetOnControllerCaptureEndEvent().AddLambda([&Ended]() { ++Ended; });
	Behaviour->SetControllerCaptured(true);
	TestTrue(TEXT("taking the lock is visible"), Slider->IsControllerCaptured());
	TestEqual(TEXT("and said once"), Began, 1);
	Behaviour->SetControllerCaptured(true);
	TestEqual(TEXT("taking it twice says nothing new"), Began, 1);
	Behaviour->SetControllerCaptured(false);
	TestEqual(TEXT("giving it back is said once"), Ended, 1);

	// The lock cannot outlive the rule that created it.
	Behaviour->SetControllerCaptured(true);
	Behaviour->SetRequiresControllerLock(false);
	TestFalse(TEXT("turning the rule off releases a standing capture"), Slider->IsControllerCaptured());
	return true;
}

/**
 * The bar's fill grows from the edge FillType names, and a marquee is a fixed fill that sweeps.
 *
 * A progress bar that could only run left to right was the family's one asymmetry -- the slider next
 * door has had Direction since it was written -- and an indeterminate bar had no expression at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProgressBarFillTypeAndMarqueeTest,
	"DreamGUI.Controls.ProgressBar.TheFillTypeMovesTheGrowingEdgeAndAMarqueeSweepsAFixedFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamProgressBarFillTypeAndMarqueeTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlParityTestLocal;

	TDreamTestControl<UDreamProgressBar> Bar(Make<UDreamProgressBar>());
	Bar->SetPercent(0.5f);
	if (!TestNotNull(TEXT("the fill exists"), Bar->FillNode.Get()))
	{
		return false;
	}
	const double TrackWidth = Bar->TrackNode->GetWidth();
	const double TrackHeight = Bar->TrackNode->GetHeight();

	// Left to right: a point anchor on the LEFT edge, and the length is the width.
	TestEqual(TEXT("it grows from the left by default"),
		static_cast<float>(Bar->FillNode->GetAnchorMin().X), 0.0f);

	Bar->SetFillType(EDreamProgressFillType::RightToLeft);
	TestEqual(TEXT("right-to-left hangs the fill from the right edge"),
		static_cast<float>(Bar->FillNode->GetAnchorMin().X), 1.0f);
	TestEqual(TEXT("...and still spends the percent on the width"),
		static_cast<float>(Bar->FillNode->GetSizeDelta().X), static_cast<float>(TrackWidth * 0.5));

	// Vertical: the length moves to the other axis and the cross axis becomes the track's.
	Bar->SetFillType(EDreamProgressFillType::BottomToTop);
	TestEqual(TEXT("bottom-to-top hangs the fill from the bottom edge"),
		static_cast<float>(Bar->FillNode->GetAnchorMin().Y), 0.0f);
	TestEqual(TEXT("...and spends the percent on the height"),
		static_cast<float>(Bar->FillNode->GetSizeDelta().Y), static_cast<float>(TrackHeight * 0.5));
	TestEqual(TEXT("...with the track's own width across it"),
		static_cast<float>(Bar->FillNode->GetSizeDelta().X), static_cast<float>(TrackWidth));

	Bar->SetFillType(EDreamProgressFillType::TopToBottom);
	TestEqual(TEXT("top-to-bottom hangs it from the top, which is Y of one"),
		static_cast<float>(Bar->FillNode->GetAnchorMin().Y), 1.0f);

	// Indeterminate: the PERCENT decides nothing and the fill is MarqueeFraction long.
	Bar->SetFillType(EDreamProgressFillType::LeftToRight);
	Bar->MarqueeFraction = 0.25f;
	Bar->MarqueeDuration = 1.0f;
	Bar->SetIsMarquee(true);
	TestEqual(TEXT("a marquee's fill is its own fraction of the track"),
		static_cast<float>(Bar->FillNode->GetSizeDelta().X), static_cast<float>(TrackWidth * 0.25));
	TestTrue(TEXT("and it asked for the tick it needs"), Bar->GetWantsTick());

	// One sweep: it starts entirely off the near edge and travels. Driven directly, which is exactly
	// what the manager would do.
	const double Before = Bar->FillNode->GetAnchoredPosition().X;
	static_cast<UDreamUserWidget*>(Bar.Get())->NativeOnTick(0.5f);
	const double After = Bar->FillNode->GetAnchoredPosition().X;
	TestTrue(TEXT("a tick moves the sweep along"), After > Before);
	TestEqual(TEXT("and the length never changes while it sweeps"),
		static_cast<float>(Bar->FillNode->GetSizeDelta().X), static_cast<float>(TrackWidth * 0.25));

	Bar->SetIsMarquee(false);
	TestFalse(TEXT("turning it off gives the tick back"), Bar->GetWantsTick());
	TestEqual(TEXT("and the percent decides the length again"),
		static_cast<float>(Bar->FillNode->GetSizeDelta().X), static_cast<float>(TrackWidth * 0.5));
	return true;
}

/**
 * How the spin box SPELLS its number, which is two knobs because it answers two opposite wants.
 *
 * A currency field must show "2.50" -- a MINIMUM number of digits -- and a count field must not show
 * "3.0000001" -- a MAXIMUM. Without them the field printed the shortest spelling that reads back
 * exactly, which is right for neither.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpinBoxFractionalDigitsTest,
	"DreamGUI.Controls.SpinBox.TheFractionalDigitsRuleSpellsTheNumberAndAStepIsACommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpinBoxFractionalDigitsTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlParityTestLocal;

	TDreamTestControl<UDreamSpinBox> Spin(Make<UDreamSpinBox>());
	Spin->MaxValue = 100.0f;

	// The MINIMUM: trailing zeros are kept down to it.
	Spin->MinFractionalDigits = 2;
	Spin->MaxFractionalDigits = 4;
	Spin->SetValue(2.5f);
	TestEqual(TEXT("a minimum of two keeps the trailing zero"), TextOf(Spin->ValueTextNode), FString(TEXT("2.50")));

	// The MAXIMUM: nothing past it is printed.
	Spin->MinFractionalDigits = 0;
	Spin->MaxFractionalDigits = 2;
	Spin->SetValue(1.0f / 3.0f);
	TestEqual(TEXT("a maximum of two cuts the rest"), TextOf(Spin->ValueTextNode), FString(TEXT("0.33")));

	// A whole number with a minimum of zero loses the point entirely, rather than printing "3.".
	Spin->SetValue(3.0f);
	TestEqual(TEXT("a whole number keeps no decimal point"), TextOf(Spin->ValueTextNode), FString(TEXT("3")));

	// A step moves by StepSize, and the drag range defaults to the hard range -- the two numbers the
	// scrub is measured against, which an unconfigured spin box must still be able to answer.
	Spin->MinFractionalDigits = 0;
	Spin->MaxFractionalDigits = 6;
	Spin->StepSize = 2.0f;
	Spin->Increment();
	TestEqual(TEXT("a step moves by StepSize"), Spin->GetValue(), 5.0f);
	TestEqual(TEXT("the scrub sweeps the hard range until told otherwise"),
		Spin->GetSliderMaxValue(), Spin->MaxValue);
	Spin->bOverride_MaxSliderValue = true;
	Spin->MaxSliderValue = 10.0f;
	TestEqual(TEXT("and the override is what it sweeps once ticked"), Spin->GetSliderMaxValue(), 10.0f);

	// Delta snap makes EVERY road land on a multiple, measured from MinValue.
	Spin->bAlwaysUsesDeltaSnap = true;
	Spin->SetValue(5.4f);
	TestEqual(TEXT("delta snap rounds a typed value to the step"), Spin->GetValue(), 6.0f);
	return true;
}

/**
 * Closing a tab, moving a tab, and disabling one -- the three things a strip of tabs is expected to
 * do and this one could not.
 *
 * The index arithmetic is the part worth pinning: closing a tab BEFORE the open one must leave the
 * same page open, and dragging a tab must not switch tabs.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabViewCloseMoveDisableTest,
	"DreamGUI.Controls.TabView.ClosingAndMovingATabKeepTheSamePageOpenAndADisabledTabRefusesClicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabViewCloseMoveDisableTest::RunTest(const FString& Parameters)
{
	auto Label = [](const TCHAR* InText) { return FText::AsCultureInvariant(InText); };

	TDreamTestControl<UDreamTabView> View(NewObject<UDreamTabView>(GetTransientPackage()));
	View->TabLabels = { Label(TEXT("Video")), Label(TEXT("Audio")), Label(TEXT("Controls")) };
	View->bTabsClosable = true;
	View->Initialize();
	if (!TestEqual(TEXT("three captions made three tabs"), View->Tabs.Num(), 3))
	{
		return false;
	}
	// The close buttons exist and are awake, because the view offers closing.
	TestNotNull(TEXT("a closable tab has a close button"), View->Tabs[0].CloseNode.Get());
	TestTrue(TEXT("...and it is awake"),
		View->Tabs[0].CloseNode != nullptr && View->Tabs[0].CloseNode->GetWidgetActive());

	// Open the last, then close the FIRST: the same page must stay open, which means the index moves
	// down with it.
	View->SetActiveTabIndex(2);
	View->CloseTab(0);
	TestEqual(TEXT("closing a tab drops its caption"), View->TabLabels.Num(), 2);
	TestEqual(TEXT("the strip shrank with it"), View->Tabs.Num(), 2);
	TestEqual(TEXT("and the same page is still open"), View->GetActiveTabIndex(), 1);

	// Moving the OPEN tab carries the selection with it -- dragging a tab must not switch tabs.
	View->MoveTab(1, 0);
	TestEqual(TEXT("moving the open tab moves the selection"), View->GetActiveTabIndex(), 0);
	TestEqual(TEXT("and the caption travelled"),
		View->TabLabels[0].ToString(), FString(TEXT("Controls")));

	// A disabled tab cannot be clicked into -- the selectable is what refuses, so that is what is
	// asserted. Code may still open it, which is this library's rule everywhere.
	View->SetTabEnabled(1, false);
	TestFalse(TEXT("a disabled tab answers that it is"), View->IsTabEnabled(1));
	if (TestNotNull(TEXT("the tab has its toggle"), View->Tabs[1].Toggle.Get()))
	{
		TestFalse(TEXT("and its toggle refuses the player"), View->Tabs[1].Toggle->GetInteractable());
	}
	TestTrue(TEXT("a tab nobody disabled is still enabled"), View->IsTabEnabled(0));
	return true;
}

/**
 * The scroll bar's arrows: they inset the TRACK, which is what keeps the handle out from under them.
 *
 * Not a cosmetic claim. The behaviour reads the handle's parent as the space its value is measured
 * in, so an arrow drawn over a track that still ran the whole length would be a button the handle
 * slides underneath.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamScrollBarArrowsTest,
	"DreamGUI.Controls.ScrollBar.TheArrowsInsetTheTrackAndStepTheValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamScrollBarArrowsTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlParityTestLocal;

	TDreamTestControl<UDreamScrollBar> Bar(Make<UDreamScrollBar>());
	if (!TestNotNull(TEXT("the track exists"), Bar->TrackNode.Get()) ||
		!TestNotNull(TEXT("the start arrow exists"), Bar->ArrowStartNode.Get()))
	{
		return false;
	}
	// Off by default, which is what this bar has always drawn.
	TestFalse(TEXT("a bar has no arrows unless asked"), Bar->ArrowStartNode->GetWidgetActive());
	TestEqual(TEXT("and its track is the whole length"),
		static_cast<float>(Bar->TrackNode->GetSizeDelta().Y), 0.0f);

	const FDreamScrollBarStyle Defaults;
	Bar->SetShowArrows(true);
	TestTrue(TEXT("asking wakes them"), Bar->ArrowStartNode->GetWidgetActive());
	TestTrue(TEXT("both of them"),
		Bar->ArrowEndNode != nullptr && Bar->ArrowEndNode->GetWidgetActive());
	// A vertical bar (the default direction) insets its track on the long axis, by one arrow at each
	// end -- and an arrow is as long as the bar is thick.
	TestEqual(TEXT("the track is inset by an arrow at each end"),
		static_cast<float>(Bar->TrackNode->GetSizeDelta().Y), -2.0f * Defaults.Thickness);

	// And the buttons step the value, towards the end each is pinned to.
	Bar->SetValue(0.5f);
	Bar->ArrowStepSize = 0.25f;
	if (TestNotNull(TEXT("the end arrow is a button"), Bar->ArrowEndBehaviour.Get()))
	{
		Bar->ArrowEndBehaviour->GetOnClickEvent().Broadcast();
		TestEqual(TEXT("the end arrow steps towards one"), Bar->GetValue(), 0.75f);
	}
	if (TestNotNull(TEXT("the start arrow is a button"), Bar->ArrowStartBehaviour.Get()))
	{
		Bar->ArrowStartBehaviour->GetOnClickEvent().Broadcast();
		TestEqual(TEXT("and the start arrow back towards zero"), Bar->GetValue(), 0.5f);
	}
	return true;
}

/**
 * MaxHeight caps what an expanded section CLAIMS, which is the only honest way to put a section of
 * unknown length inside a page that has one.
 *
 * The control's own height is what a consumer's Auto slot measures, so capping the measurement is
 * the whole feature -- and the column has to clip, or the content would simply draw past it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamExpandableAreaMaxHeightTest,
	"DreamGUI.Controls.ExpandableArea.MaxHeightCapsWhatAnExpandedSectionClaimsAndClipsTheRest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamExpandableAreaMaxHeightTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlParityTestLocal;

	const FDreamExpandableAreaStyle Defaults;

	TDreamTestControl<UDreamExpandableArea> Area(Make<UDreamExpandableArea>());
	if (!TestNotNull(TEXT("the content column exists"), Area->ContentNode.Get()))
	{
		return false;
	}
	UDreamWidget* Content = NewObject<UDreamWidget>(Area.Get());
	Content->SetHeight(400.0f);
	Area->SetContent(Content);
	Area->SetIsExpanded(true);

	const float Uncapped = Area->GetHeight();
	// The CEILING, asserted as a bound rather than as an exact number: what the content measures is
	// the layout's answer and this test is not about the measure, it is about the cap. Both halves of
	// "capped" are here -- never taller than header plus cap, and never taller than it was.
	Area->SetMaxHeight(100.0f);
	const float Capped = Area->GetHeight();
	TestTrue(TEXT("a capped section claims at most the header plus the cap"),
		Capped <= Defaults.HeaderHeight + 100.0f + KINDA_SMALL_NUMBER);
	TestTrue(TEXT("and never more than it did uncapped"), Capped <= Uncapped + KINDA_SMALL_NUMBER);
	TestEqual(TEXT("and the column clips what it cannot show"),
		Area->ContentNode->GetClipping(), EDreamWidgetClipping::ClipToBounds);

	// Zero is NO cap, not a cap of zero -- the reading every optional number in this family gets.
	Area->SetMaxHeight(0.0f);
	TestEqual(TEXT("zero lifts the cap again"), Area->GetHeight(), Uncapped);
	TestEqual(TEXT("and the clip goes with it"),
		Area->ContentNode->GetClipping(), EDreamWidgetClipping::Inherit);
	return true;
}

/**
 * Back closes a STANDALONE dialog with its own cancel result.
 *
 * Standalone only, and that is the load-bearing half: hosted by the modal subsystem the LAYER's
 * scope already answers Back with the "Back" result its header promises whoever called ShowModal,
 * and a second scope on top would quietly change that answer. So the scope exists exactly where the
 * subsystem is not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDialogBackClosesItTest,
	"DreamGUI.Controls.Dialog.BackClosesAStandaloneDialogWithItsOwnCancelResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDialogBackClosesItTest::RunTest(const FString& Parameters)
{
	TDreamTestControl<UDreamDialog> Dialog(NewObject<UDreamDialog>(GetTransientPackage()));
	Dialog->StyleSource = EDreamUIStyleSource::Inline;
	Dialog->Initialize();

	// Nothing above it is scrimming (it has no parent at all), so this is the standalone arrangement
	// and the Back handler is the one the dialog put there.
	if (!TestNotNull(TEXT("a standalone dialog carries a Back handler"), Dialog->BackScope.Get()))
	{
		return false;
	}
	UDreamDialogResultProbe* Probe = NewObject<UDreamDialogResultProbe>(GetTransientPackage());
	Dialog->OnDialogClosed.AddDynamic(Probe, &UDreamDialogResultProbe::Record);

	// TRUE: Back is taken here and must go no further down the stack -- one press, one dialog closed,
	// never the dialog AND the page behind it.
	TestTrue(TEXT("Back is taken"), Dialog->BackScope->HandleBackAction());
	TestEqual(TEXT("and it closed the dialog once"), Probe->CallCount, 1);
	// The seeded pair is Cancel then OK (OK primary), so the row's own cancel is "Cancel".
	TestEqual(TEXT("with the row's own cancel result"), Probe->LastResult, FName(TEXT("Cancel")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
