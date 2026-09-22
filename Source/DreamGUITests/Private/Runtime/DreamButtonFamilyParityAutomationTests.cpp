// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamBorder.h"
#include "Controls/DreamButton.h"
// FDreamListStyle and FDreamUIStateFaces, for the row-brush fallback test at the end.
#include "Controls/DreamControlStyles.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamToggle.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/Texture2D.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIEventTrigger.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UISlider.h"
#include "Interaction/UIToggle.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"

/*
 * The state-face round: a control face that draws a different PICTURE per state, rather than one
 * picture tinted five ways.
 *
 * Every test here is about a PUSH, which is where this kind of feature fails silently: the style
 * holds the brush the author dropped in, the face holds whatever it was built with, and the control
 * looks right in the designer and wrong the moment a pointer touches it. So the assertions read the
 * rect block the face actually draws with, and the selectable the pointer actually talks to, rather
 * than the properties the control was handed.
 *
 * The group ships EMPTY, and the first two tests are what makes that claim testable: with nothing
 * stated, every state has to leave the style's own single brush exactly where it was. A feature that
 * cannot prove it changed nothing is a feature nobody can turn on safely.
 *
 * Headless: no world, no registration, no event system. Transition durations are zeroed so the
 * colour path takes its immediate branch -- the tween manager is a world subsystem and answers null
 * out here, which would make an animated assertion a test of the fallback rather than of the push.
 */
namespace DreamButtonFamilyParityTestLocal
{
	template<class T>
	T* Make()
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->Initialize();
		return Control;
	}

	/** The rect a control part draws with, or null where the part is not rect-faced. */
	UDreamRectBlock* RectOf(const UDreamWidget* InNode)
	{
		return InNode != nullptr ? Cast<UDreamRectBlock>(InNode->GetVisual()) : nullptr;
	}

	UTexture2D* MakeTexture()
	{
		return NewObject<UTexture2D>(GetTransientPackage());
	}

	/**
	 * The expected product of an authored colour and a runtime tint.
	 *
	 * Spelled out here rather than read from the control's own helper, so the test states the rule
	 * independently: a helper asserting itself proves only that it is consistent.
	 */
	int32 TintedChannel(uint8 InBase, uint8 InTint)
	{
		return static_cast<int32>(InBase) * static_cast<int32>(InTint) / 255;
	}
}

/**
 * A style that states no state faces has to draw what it always drew.
 *
 * The strongest claim this feature makes, and the only one that can be checked without turning the
 * feature on: walk every one of the five states and assert the face still carries the style's own
 * brush. A fallback chain with an off-by-one in it would show up here as a state going blank.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonEmptyStateFacesTest,
	"DreamGUI.Button.AnUnstatedStateFaceGroupLeavesTheStyleBrushOnEveryState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonEmptyStateFacesTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamButton> Button(Make<UDreamButton>());
	UTexture2D* FaceTexture = MakeTexture();

	Button->StyleSource = EDreamUIStyleSource::Inline;
	Button->Style.TransitionDuration = 0.0f;
	Button->Style.FaceBrush.Image = FaceTexture;
	Button->ApplyStyle();

	UDreamRectBlock* Rect = RectOf(Button->FaceNode);
	if (!TestNotNull(TEXT("The face draws with a rect block"), Rect))
	{
		return false;
	}

	const EUISelectableSelectionState EveryState[] = {
		EUISelectableSelectionState::Normal,
		EUISelectableSelectionState::Hovered,
		EUISelectableSelectionState::Pressed,
		EUISelectableSelectionState::Disabled,
		EUISelectableSelectionState::Focused,
	};
	for (const EUISelectableSelectionState State : EveryState)
	{
		Button->ButtonBehaviour->SetSelectionState(State);
		TestEqual(TEXT("Every state keeps the style's own brush while the group says nothing"),
			Rect->GetBodyTexture(), static_cast<UTexture*>(FaceTexture));
	}
	return true;
}

/**
 * A state with a brush of its own wins, and one without falls back to the group's resting brush.
 *
 * Two claims in one test because they are one rule read twice: the chain is state, then Normal,
 * then the control's own. Hovered states a brush here and Pressed does not, so the same pointer
 * sequence proves both ends.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonStateFaceSwapTest,
	"DreamGUI.Button.AStateWithoutABrushOfItsOwnFallsBackToTheGroupsRestingOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonStateFaceSwapTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamButton> Button(Make<UDreamButton>());
	UTexture2D* RestingTexture = MakeTexture();
	UTexture2D* HoveredTexture = MakeTexture();

	Button->StyleSource = EDreamUIStyleSource::Inline;
	Button->Style.TransitionDuration = 0.0f;
	Button->Style.StateFaces.Normal.Image = RestingTexture;
	Button->Style.StateFaces.Hovered.Image = HoveredTexture;
	Button->ApplyStyle();

	UDreamRectBlock* Rect = RectOf(Button->FaceNode);
	if (!TestNotNull(TEXT("The face draws with a rect block"), Rect))
	{
		return false;
	}
	TestEqual(TEXT("The push paints the state the control is already in"),
		Rect->GetBodyTexture(), static_cast<UTexture*>(RestingTexture));

	Button->ButtonBehaviour->SetSelectionState(EUISelectableSelectionState::Hovered);
	TestEqual(TEXT("A state that states a brush wears it"),
		Rect->GetBodyTexture(), static_cast<UTexture*>(HoveredTexture));

	Button->ButtonBehaviour->SetSelectionState(EUISelectableSelectionState::Pressed);
	TestEqual(TEXT("A state that states none falls back to the group's resting brush"),
		Rect->GetBodyTexture(), static_cast<UTexture*>(RestingTexture));

	return true;
}

/**
 * The runtime background tint multiplies over the brush's authored one.
 *
 * Two channels that could each have been "the tint", so the test asserts the PRODUCT: an
 * implementation that let either one win outright passes half of this and fails the other half.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonBackgroundColorTest,
	"DreamGUI.Button.TheBackgroundColourMultipliesOverTheFaceBrushsAuthoredTint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonBackgroundColorTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamButton> Button(Make<UDreamButton>());
	Button->StyleSource = EDreamUIStyleSource::Inline;
	Button->Style.TransitionDuration = 0.0f;
	Button->Style.FaceBrush.Tint = FColor(255, 128, 255, 255);
	Button->ApplyStyle();

	UDreamRectBlock* Rect = RectOf(Button->FaceNode);
	if (!TestNotNull(TEXT("The face draws with a rect block"), Rect))
	{
		return false;
	}
	TestEqual(TEXT("White is no opinion, so the authored tint arrives untouched"),
		static_cast<int32>(Rect->GetBodyColor().G), 128);

	Button->SetBackgroundColor(FColor(128, 255, 255, 255));
	TestEqual(TEXT("The runtime tint halves the red channel"),
		static_cast<int32>(Rect->GetBodyColor().R), 128);
	TestEqual(TEXT("And leaves the authored green where it was"),
		static_cast<int32>(Rect->GetBodyColor().G), 128);

	// Through a style push as well, because that is the path an editor edit takes and the one where
	// a tint held only in a local variable would quietly be dropped.
	Button->ApplyStyle();
	TestEqual(TEXT("A later style push keeps the runtime tint"),
		static_cast<int32>(Rect->GetBodyColor().R), 128);

	return true;
}

/**
 * The style's three sounds reach the selectable, which is what plays them.
 *
 * Feedback used to be reachable only through a hand-assigned style ASSET, so a control whose look
 * comes from the project sheet -- every control in this family -- could state its colours and not
 * its click. Nothing is played here: playing is gated on a game world, and there is none.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonStateFaceSoundsTest,
	"DreamGUI.Button.TheStylesSoundsArePushedOntoTheSelectableThatPlaysThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonStateFaceSoundsTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamButton> Button(Make<UDreamButton>());
	USoundWave* Hovered = NewObject<USoundWave>(GetTransientPackage());
	USoundWave* Clicked = NewObject<USoundWave>(GetTransientPackage());

	TestNull(TEXT("A control that states no sound starts silent"),
		Button->ButtonBehaviour->GetClickedSound());

	Button->StyleSource = EDreamUIStyleSource::Inline;
	Button->Style.StateFaces.HoveredSound = Hovered;
	Button->Style.StateFaces.ClickedSound = Clicked;
	Button->ApplyStyle();

	TestEqual(TEXT("The hover sound reached the selectable"),
		Button->ButtonBehaviour->GetHoveredSound(), static_cast<USoundBase*>(Hovered));
	TestEqual(TEXT("The click sound reached the selectable"),
		Button->ButtonBehaviour->GetClickedSound(), static_cast<USoundBase*>(Clicked));

	return true;
}

/**
 * Pressed padding alternates with the resting padding, and puts it back on the way out.
 *
 * The return trip is the half worth testing: a control that only ever WROTE the pressed number
 * would look right the first time it was pressed and stay pushed in forever after.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonPressedPaddingTest,
	"DreamGUI.Button.PressedPaddingAlternatesWithTheStylesContentPadding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonPressedPaddingTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamButton> Button(Make<UDreamButton>());
	Button->StyleSource = EDreamUIStyleSource::Inline;
	Button->Style.TransitionDuration = 0.0f;
	Button->Style.ContentPadding = FMargin(10.0f, 4.0f, 10.0f, 4.0f);
	Button->Style.StateFaces.bUsePressedPadding = true;
	Button->Style.StateFaces.PressedPadding = FMargin(10.0f, 7.0f, 10.0f, 1.0f);
	Button->ApplyStyle();

	UDreamLayoutContainerSizeBox* Box = Button->FaceNode != nullptr
		? Cast<UDreamLayoutContainerSizeBox>(Button->FaceNode->GetLayoutContainer())
		: nullptr;
	if (!TestNotNull(TEXT("The face measures with a size box"), Box))
	{
		return false;
	}
	TestEqual(TEXT("At rest the style's content padding is what measures"), Box->Padding.Top, 4.0f);

	Button->ButtonBehaviour->SetSelectionState(EUISelectableSelectionState::Pressed);
	TestEqual(TEXT("Pressed, the content sits a little lower"), Box->Padding.Top, 7.0f);

	Button->ButtonBehaviour->SetSelectionState(EUISelectableSelectionState::Hovered);
	TestEqual(TEXT("Releasing puts the resting padding back"), Box->Padding.Top, 4.0f);

	return true;
}

/**
 * The drag-drop switch adds the face's drag source, and turning it off destroys it again.
 *
 * Destroyed rather than muted, so the second half is the interesting one: a dormant behaviour left
 * on every button in a project is a cost everybody pays for a switch nobody set.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonAllowDragDropTest,
	"DreamGUI.Button.TheDragDropSwitchAddsTheFacesDragSourceAndTakesItAwayAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonAllowDragDropTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamButton> Button(Make<UDreamButton>());
	TestNull(TEXT("A button nobody asked to be draggable grows nothing"), Button->GetDragSource());
	TestNull(TEXT("And the face carries no drag source either"),
		Button->FaceNode->GetComponent<UDreamUIDragSource>());

	Button->SetAllowDragDrop(true);
	TestNotNull(TEXT("The switch puts a drag source on the face"), Button->GetDragSource());
	TestEqual(TEXT("And it is the face's own, not a second one"),
		Button->GetDragSource(), Button->FaceNode->GetComponent<UDreamUIDragSource>());

	Button->SetAllowDragDrop(false);
	TestNull(TEXT("Turning it off takes the component away"), Button->GetDragSource());

	return true;
}

/**
 * IsPressed is the behaviour's answer, not a flag the control keeps.
 *
 * A control-side copy is the version that goes stale: a press ending somewhere the control never
 * hears about leaves it saying "pressed" for good.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonIsPressedTest,
	"DreamGUI.Button.IsPressedReadsTheSelectablesStateRatherThanAFlagOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonIsPressedTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamButton> Button(Make<UDreamButton>());
	Button->StyleSource = EDreamUIStyleSource::Inline;
	Button->Style.TransitionDuration = 0.0f;
	Button->ApplyStyle();

	TestFalse(TEXT("A button nobody is touching is not pressed"), Button->IsPressed());

	Button->ButtonBehaviour->SetSelectionState(EUISelectableSelectionState::Pressed);
	TestTrue(TEXT("A pressed selectable is a pressed button"), Button->IsPressed());

	Button->ButtonBehaviour->SetSelectionState(EUISelectableSelectionState::Normal);
	TestFalse(TEXT("And letting go ends it"), Button->IsPressed());

	return true;
}

/**
 * Setting the whole style re-pushes it, rather than waiting for something else to push.
 *
 * The failure this guards is the one the family is built around: a setter that writes its field and
 * stops leaves the control wearing the previous look until an unrelated edit happens to push.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamButtonSetStyleTest,
	"DreamGUI.Button.SettingTheStyleRePushesTheLookInsteadOfWaitingForOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamButtonSetStyleTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamButton> Button(Make<UDreamButton>());
	Button->SetStyleSource(EDreamUIStyleSource::Inline);

	FDreamButtonStyle Replacement;
	Replacement.TransitionDuration = 0.0f;
	Replacement.CornerRadius = 17.0f;
	Replacement.Normal = FColor(11, 22, 33, 255);
	Button->SetStyle(Replacement);

	UDreamRectBlock* Rect = RectOf(Button->FaceNode);
	if (!TestNotNull(TEXT("The face draws with a rect block"), Rect))
	{
		return false;
	}
	TestEqual(TEXT("The replacement's rounding reached the face"), Rect->GetCornerRadius().X, 17.0f);
	TestEqual(TEXT("And its resting colour reached the selectable"),
		Button->ButtonBehaviour->GetNormalColor(), FColor(11, 22, 33, 255));

	return true;
}

/**
 * The check box's two skinning questions do not fight.
 *
 * The three mark brushes answer "which of the three values is this"; the state group answers "is the
 * pointer on it". They are aimed at two nodes on purpose, and the way that goes wrong is one of them
 * quietly driving the other's node -- so the pointer is moved with a value standing, and the value
 * is asserted to still be standing afterwards.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleStateFacesTest,
	"DreamGUI.Toggle.TheBoxsStateFacesSwapWithoutDisturbingTheCheckedValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamToggleStateFacesTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamToggle> Toggle(Make<UDreamToggle>());
	UTexture2D* BoxTexture = MakeTexture();
	UTexture2D* HoveredTexture = MakeTexture();

	Toggle->StyleSource = EDreamUIStyleSource::Inline;
	Toggle->Style.TransitionDuration = 0.0f;
	Toggle->Style.BoxBrush.Image = BoxTexture;
	Toggle->Style.StateFaces.Hovered.Image = HoveredTexture;
	Toggle->SetCheckedState(EDreamCheckState::Checked);
	Toggle->ApplyStyle();

	UDreamRectBlock* Box = RectOf(Toggle->BoxNode);
	if (!TestNotNull(TEXT("The box draws with a rect block"), Box))
	{
		return false;
	}
	TestEqual(TEXT("At rest the box wears the style's own brush"),
		Box->GetBodyTexture(), static_cast<UTexture*>(BoxTexture));
	TestFalse(TEXT("Nobody is pressing it"), Toggle->IsPressed());

	Toggle->ToggleBehaviour->SetSelectionState(EUISelectableSelectionState::Hovered);
	TestEqual(TEXT("Hovering swaps the box's drawing"),
		Box->GetBodyTexture(), static_cast<UTexture*>(HoveredTexture));

	Toggle->ToggleBehaviour->SetSelectionState(EUISelectableSelectionState::Pressed);
	TestTrue(TEXT("A pressed selectable is a pressed check box"), Toggle->IsPressed());
	TestEqual(TEXT("A state with no drawing of its own falls back to the style's brush"),
		Box->GetBodyTexture(), static_cast<UTexture*>(BoxTexture));
	TestEqual(TEXT("And the value the box is showing never moved"),
		Toggle->GetCheckedState(), EDreamCheckState::Checked);

	return true;
}

/**
 * Replacing the whole style pushes it, as every other setter in this family does.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleSetStyleTest,
	"DreamGUI.Toggle.SettingTheStyleRePushesTheBoxsLook",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamToggleSetStyleTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamToggle> Toggle(Make<UDreamToggle>());
	Toggle->SetStyleSource(EDreamUIStyleSource::Inline);

	FDreamToggleStyle Replacement;
	Replacement.TransitionDuration = 0.0f;
	Replacement.CornerRadius = 9.0f;
	Replacement.BoxNormal = FColor(44, 55, 66, 255);
	Toggle->SetStyle(Replacement);

	UDreamRectBlock* Box = RectOf(Toggle->BoxNode);
	if (!TestNotNull(TEXT("The box draws with a rect block"), Box))
	{
		return false;
	}
	TestEqual(TEXT("The replacement's rounding reached the box"), Box->GetCornerRadius().X, 9.0f);
	TestEqual(TEXT("And its resting colour reached the behaviour"),
		Toggle->ToggleBehaviour->GetNormalColor(), FColor(44, 55, 66, 255));

	return true;
}

/**
 * The lock reaches the behaviour, and does not stop game code writing the value.
 *
 * Both halves matter. A lock pushed nowhere is the silent failure this whole round is about; a lock
 * that also gagged SetValue would leave a read-only slider with nothing to show.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderLockedTest,
	"DreamGUI.Slider.TheLockReachesTheBehaviourAndStillLetsCodeWriteTheValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderLockedTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamSlider> Slider(Make<UDreamSlider>());
	TestFalse(TEXT("A slider nobody locked is not locked"), Slider->SliderBehaviour->IsLocked());

	Slider->SetLocked(true);
	TestTrue(TEXT("The lock reached the behaviour that enforces it"), Slider->SliderBehaviour->IsLocked());

	Slider->SetValue(0.75f);
	TestEqual(TEXT("A locked slider is still what code says it is"), Slider->GetValue(), 0.75f);

	// Through the style push as well, which is the path an editor edit and a restyle both take.
	Slider->ApplyStyle();
	TestTrue(TEXT("A style push does not quietly unlock it"), Slider->SliderBehaviour->IsLocked());

	return true;
}

/**
 * The bar and handle tints multiply over the style's colours rather than replacing them.
 *
 * Replacing is the implementation that looks right once and is wrong forever after: the next style
 * push, or the next hover, puts the style's colour back and the tint is gone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderTintTest,
	"DreamGUI.Slider.TheBarAndHandleTintsMultiplyOverTheStylesOwnColours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderTintTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamSlider> Slider(Make<UDreamSlider>());
	Slider->SetStyleSource(EDreamUIStyleSource::Inline);
	Slider->Style.TransitionDuration = 0.0f;
	Slider->Style.TrackColor = FColor(200, 200, 200, 255);
	Slider->Style.HandleNormal = FColor(200, 100, 50, 255);
	Slider->ApplyStyle();

	UDreamVisual* Track = Slider->TrackNode != nullptr ? Slider->TrackNode->GetVisual() : nullptr;
	if (!TestNotNull(TEXT("The track has a visual to colour"), Track))
	{
		return false;
	}
	TestEqual(TEXT("White is no opinion, so the style's track colour arrives untouched"),
		static_cast<int32>(Track->GetColor().R), 200);

	Slider->SetSliderBarColor(FColor(128, 255, 255, 255));
	TestEqual(TEXT("The bar tint halves the track's red"),
		static_cast<int32>(Track->GetColor().R), 100);

	Slider->SetSliderHandleColor(FColor(255, 128, 255, 255));
	TestEqual(TEXT("And the handle tint reaches the behaviour's resting colour"),
		static_cast<int32>(Slider->SliderBehaviour->GetNormalColor().G), TintedChannel(100, 128));
	TestEqual(TEXT("...through the hovered one too, so a tinted handle stays tinted"),
		static_cast<int32>(Slider->SliderBehaviour->GetHoveredColor().G),
		TintedChannel(Slider->Style.HandleHovered.G, 128));

	return true;
}

/**
 * A range with no width has no positions inside it, so the normalized value is zero rather than a
 * division by zero.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderNormalizedValueTest,
	"DreamGUI.Slider.GetNormalizedValueAnswersZeroForARangeWithNoWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderNormalizedValueTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamSlider> Slider(Make<UDreamSlider>());
	Slider->SetMinValue(0.0f);
	Slider->SetMaxValue(200.0f);
	Slider->SetValue(50.0f);
	TestEqual(TEXT("A quarter of the way along"), Slider->GetNormalizedValue(), 0.25f);

	Slider->SetMaxValue(0.0f);
	TestEqual(TEXT("Ends that meet answer zero rather than dividing by nothing"),
		Slider->GetNormalizedValue(), 0.0f);

	return true;
}

/**
 * A border reports the pointer only when it is asked to, and consuming maps to the trigger's own
 * bubbling switch inverted.
 *
 * Both halves are the whole reason the switch exists: a listener that appeared on every border
 * would make every border start eating clicks, and a "consume" that did not reach the trigger would
 * be a property with an opinion and no effect.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamBorderMouseEventsTest,
	"DreamGUI.Border.TheMouseListenerIsMadeOnlyWhenAskedForAndConsumingReachesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamBorderMouseEventsTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamBorder> Border(Make<UDreamBorder>());
	TestNull(TEXT("A border nobody asked costs no component at all"), Border->GetEventTrigger());

	Border->SetReportMouseEvents(true);
	UUIEventTrigger* Trigger = Border->GetEventTrigger();
	if (!TestNotNull(TEXT("Asking for the events makes the listener"), Trigger))
	{
		return false;
	}
	TestFalse(TEXT("Consuming is the default, and consuming is not bubbling"),
		Trigger->GetAllowEventBubbleUp());

	Border->SetConsumeMouseEvents(false);
	TestTrue(TEXT("Letting events carry on reaches the trigger that decides it"),
		Trigger->GetAllowEventBubbleUp());

	Border->SetReportMouseEvents(false);
	TestNull(TEXT("Turning it off destroys the listener rather than muting it"), Border->GetEventTrigger());

	return true;
}

/**
 * Un-indenting the handle gives it the track's whole length.
 *
 * The indent is an inset on the handle AREA, so this is the one number that moves -- and asserting
 * it is what separates the change from "the handle looks different", which nothing headless can see.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSliderIndentHandleTest,
	"DreamGUI.Slider.UnIndentingTheHandleGivesItTheTracksWholeLength",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSliderIndentHandleTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	TDreamTestControl<UDreamSlider> Slider(Make<UDreamSlider>());
	Slider->SetStyleSource(EDreamUIStyleSource::Inline);
	Slider->Style.HandleSize = FVector2D(18.0, 18.0);
	Slider->ApplyStyle();

	if (!TestNotNull(TEXT("The handle area exists to be inset"), Slider->HandleAreaNode.Get()))
	{
		return false;
	}
	TestEqual(TEXT("Indented, the area is shorter than the track by the handle's width"),
		Slider->HandleAreaNode->GetSizeDelta().X, -18.0);

	Slider->SetIndentHandle(false);
	TestEqual(TEXT("Un-indented, the area is the track itself"),
		Slider->HandleAreaNode->GetSizeDelta().X, 0.0);

	// The value is the handle area's business only in where it PUTS the handle, so neither answer
	// may move: a slider that reported differently for a cosmetic switch would be a trap.
	Slider->SetValue(0.5f);
	TestEqual(TEXT("And the reported value is untouched either way"), Slider->GetValue(), 0.5f);

	return true;
}

/**
 * A list's rows fall back to RowBrush while the state group is empty, and take a state's own
 * drawing once one is stated.
 *
 * Asserted on the RULE rather than on a hovered row, and deliberately: a row's brush is chosen by
 * FDreamListStyle::StateFaces::BrushFor at bind time and again on every state change, so the
 * fallback chain IS the feature -- and it is the half that has to be exactly true for every list
 * already in a project to keep drawing what it drew. Building a pool of live rows and moving a
 * pointer over one needs a world and a layout pass; what that would add is the plumbing, which the
 * two call sites share with the hover bookkeeping the suite already covers.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamListRowStateFacesTest,
	"DreamGUI.ListView.RowsFallBackToTheRowBrushUntilAStateStatesItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamListRowStateFacesTest::RunTest(const FString& Parameters)
{
	using namespace DreamButtonFamilyParityTestLocal;

	FDreamListStyle Style;
	UTexture2D* RowPicture = MakeTexture();
	Style.RowBrush.Image = RowPicture;

	// Empty group: every state answers the one brush the list has always drawn with.
	for (EUISelectableSelectionState State : {
		EUISelectableSelectionState::Normal,
		EUISelectableSelectionState::Hovered,
		EUISelectableSelectionState::Pressed,
		EUISelectableSelectionState::Disabled,
		EUISelectableSelectionState::Focused })
	{
		TestEqual(TEXT("An empty state group leaves every state on the row brush"),
			static_cast<const UObject*>(Style.StateFaces.BrushFor(State, Style.RowBrush).Image),
			static_cast<const UObject*>(RowPicture));
	}

	UTexture2D* HoverPicture = MakeTexture();
	Style.StateFaces.Hovered.Image = HoverPicture;

	TestEqual(TEXT("A stated hover draws its own picture"),
		static_cast<const UObject*>(Style.StateFaces.BrushFor(EUISelectableSelectionState::Hovered, Style.RowBrush).Image),
		static_cast<const UObject*>(HoverPicture));
	// The states nobody stated still fall back, rather than following the one that was stated.
	TestEqual(TEXT("Resting is unmoved by a hover being stated"),
		static_cast<const UObject*>(Style.StateFaces.BrushFor(EUISelectableSelectionState::Normal, Style.RowBrush).Image),
		static_cast<const UObject*>(RowPicture));
	TestEqual(TEXT("And so is pressed, which states nothing of its own"),
		static_cast<const UObject*>(Style.StateFaces.BrushFor(EUISelectableSelectionState::Pressed, Style.RowBrush).Image),
		static_cast<const UObject*>(RowPicture));

	// A group with a resting drawing is the second rung of the chain: an unstated state takes the
	// group's Normal before it takes the control's single brush.
	UTexture2D* GroupResting = MakeTexture();
	Style.StateFaces.Normal.Image = GroupResting;
	TestEqual(TEXT("A group resting drawing outranks the row brush for an unstated state"),
		static_cast<const UObject*>(Style.StateFaces.BrushFor(EUISelectableSelectionState::Pressed, Style.RowBrush).Image),
		static_cast<const UObject*>(GroupResting));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
