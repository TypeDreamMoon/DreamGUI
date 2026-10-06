// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputSubsystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamDragInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextInteractionTestTypes.h"

/*
 * THE GAME HOST'S GESTURES, HEARD FROM SLATE.
 *
 * With the Slate input source on (UDreamGUISettings::bUseSlateInputSource) a world's input comes from one of Slate's input
 * pre-processors, FDreamUISlateInputSource, rather than from the preset actor behind the player controller: Slate offers
 * it every mouse, touch and key event before the game viewport sees one, and a typed character reaches DreamGUI through
 * the viewport's road (UDreamUIInputSubsystem::HandleViewportCharacter). The rig plays Slate's part (DreamDriverSlateHost):
 * every step below is FPointerEvents and FKeyEvents made as FSlateApplication makes them, and nothing from the desk.
 *
 * So what Driver.GameHost.* shows of the controller's road is shown here of Slate's, with the same controls and the same
 * claims, which are UMG's: SButton's press, release and click in that order; SScrollBox's wheel notch, finger pan and
 * right-button drag; SEditableText's characters, one change each, and Escape ending the edit with the text kept.
 */
namespace DreamSlateSourceHostTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(200.0, 60.0);

	FDreamRigOptions SlateSourceOptions()
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = EDreamRigInputHost::SlateSource;
		return Options;
	}

	/** The rig came up, and its world hears its input from the Slate source, as the claims below assume. */
	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		InRig.BindTest(&InTest);
		const FString& WhyNot = InRig.GetBuildFailure();
		if (!InTest.TestTrue(WhyNot.IsEmpty() ? FString(TEXT("The Slate-source rig came up"))
			: FString::Printf(TEXT("The Slate-source rig came up -- it did not: %s"), *WhyNot), InRig.IsUsable()))
		{
			return false;
		}
		const UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(InRig.GetWorld());
		return InTest.TestTrue(TEXT("The world's input is heard from its Slate source"), Input != nullptr && Input->IsSlateInputSourceActive());
	}

	UDreamButton* MakeListenedButton(FDreamDriverRig& InRig, const TCHAR* InName, UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, nullptr, ButtonSize);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
			Button->OnReleased.AddDynamic(InListener, &UDreamPressInteractionListener::HandleReleased);
		}
		return Button;
	}

	/** Twenty rows of 100 in a 300 by 400 window, measured and laid out. */
	UDreamScrollBox* MakeFilledBox(FDreamDriverRig& InRig)
	{
		UDreamScrollBox* Box = InRig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 400.0));
		if (Box == nullptr || Box->GetContentNode() == nullptr)
		{
			return Box;
		}
		for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
		{
			InRig.MakeWidget(FString::Printf(TEXT("Box_Row%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
		}
		Box->RefreshContentExtent();
		InRig.PumpFrames(2);
		return Box;
	}

	bool HasParts(const UDreamScrollBox* InBox)
	{
		return InBox != nullptr && InBox->ViewportNode != nullptr && InBox->GetContentNode() != nullptr;
	}

	/** Local units of the box per viewport pixel, from the viewport's own two heights; unset when it is not on screen. */
	TOptional<double> UnitsPerPixel(FDreamDriverRig& InRig, const UDreamScrollBox* InBox)
	{
		const TOptional<FBox2D> Rect = InRig.Driver()->Find(FDreamBy::Widget(InBox->ViewportNode.Get()))->GetPixelRect();
		if (!Rect.IsSet() || Rect->Max.Y - Rect->Min.Y <= 1.0)
		{
			return TOptional<double>();
		}
		return InBox->ViewportNode->GetHeight() / (Rect->Max.Y - Rect->Min.Y);
	}

	UDreamTextInput* MakeObservedField(FDreamDriverRig& InRig, UDreamTextInteractionListener* InListener)
	{
		UDreamTextInput* Field = InRig.MakeControl<UDreamTextInput>(TEXT("Username"), nullptr, FVector2D(320.0, 40.0));
		if (Field != nullptr && InListener != nullptr)
		{
			Field->OnTextChanged.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextChanged);
			Field->OnTextCommitted.AddDynamic(InListener, &UDreamTextInteractionListener::HandleTextCommitted);
		}
		return Field;
	}

	bool IsEditing(const UDreamTextInput* InField)
	{
		return InField != nullptr && InField->InputBehaviour != nullptr && InField->InputBehaviour->IsInputActive();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceClickOrderTest,
	"DreamGUI.Button.ThroughTheSlateInputSourceAClickPressesReleasesAndClicksTheButtonOnceEachInOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSlateSourceClickOrderTest, "DreamGUI.Button.ThroughTheSlateInputSourceAClickPressesReleasesAndClicksTheButtonOnceEachInOrder", "[Pointer][Animated]")

/*
 * SButton's order -- OnMouseButtonDown presses, OnMouseButtonUp releases and then clicks -- with the mouse's move, down and
 * up arriving as Slate's pointer events at the source.
 */
bool FDreamSlateSourceClickOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateSourceHostTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(SlateSourceOptions());
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), Listener.Get());
	if (!TestNotNull(TEXT("A button came up"), Button))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the button through Slate's mouse completes"), Rig.Driver()->Find(FDreamBy::Widget(Button))->Click());
	TestEqual(TEXT("One press"), Listener->PressedCount, 1);
	TestEqual(TEXT("One release"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("One click"), Listener->ClickedCount, 1);
	const TArray<FName> Expected = { FName(TEXT("Pressed")), FName(TEXT("Released")), FName(TEXT("Clicked")) };
	TestTrue(TEXT("The press comes first, then the release, then the click -- SButton's order"), Listener->LogOnly(Expected) == Expected);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceTapsTest,
	"DreamGUI.Button.ThroughTheSlateInputSourceEachFingersTapIsAClickOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSlateSourceTapsTest, "DreamGUI.Button.ThroughTheSlateInputSourceEachFingersTapIsAClickOfItsOwn", "[Touch][Animated]")

/*
 * Slate makes each finger a pointer of its own (its pointer index), and SButton takes a touch as the pointer it is: a
 * finger's tap is a press, a release and a click, and a second finger's tap is a click of its own rather than half of a
 * double click.
 */
bool FDreamSlateSourceTapsTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateSourceHostTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(SlateSourceOptions());
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	UDreamButton* Button = MakeListenedButton(Rig, TEXT("Play"), Listener.Get());
	if (!TestNotNull(TEXT("A button came up"), Button))
	{
		return false;
	}
	Rig.PumpFrames(1);
	FDreamElementRef ButtonElement = Rig.Driver()->Find(FDreamBy::Widget(Button));

	TestTrue(TEXT("A tap with the first finger completes"), ButtonElement->Tap(0));
	TestEqual(TEXT("The finger landing pressed the button once"), Listener->PressedCount, 1);
	TestEqual(TEXT("...lifting it released it once"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("...and clicked it once"), Listener->ClickedCount, 1);

	TestTrue(TEXT("A tap with the second finger completes"), ButtonElement->Tap(1));
	TestEqual(TEXT("The second finger's tap is a click of its own"), Listener->ClickedCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceWheelTest,
	"DreamGUI.ScrollBox.ThroughTheSlateInputSourceEachWheelNotchScrollsTheBoxUnderThePointerOneNotch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSlateSourceWheelTest, "DreamGUI.ScrollBox.ThroughTheSlateInputSourceEachWheelNotchScrollsTheBoxUnderThePointerOneNotch", "[Pointer][Animated]")

/*
 * SScrollBox::OnMouseWheel scrolls one notch per wheel delta. The wheel arrives as one of Slate's wheel events at the
 * mouse, its value read as the actor hosts read theirs, so three notches go three notches' distance.
 */
bool FDreamSlateSourceWheelTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateSourceHostTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(SlateSourceOptions());
	if (!ComeUp(*this, Rig)
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamScrollBox* Box = MakeFilledBox(Rig);
	if (!TestTrue(TEXT("The box came up with a viewport and a content node"), HasParts(Box)))
	{
		return false;
	}
	const float Notch = Box->GetScrollSensitivity() * Box->GetWheelScrollMultiplier();
	if (!TestTrue(TEXT("There is more than three notches to scroll"), Notch > 0.0f && Box->GetScrollOffsetOfEnd() > 3.0f * Notch))
	{
		return false;
	}
	FDreamElementRef Viewport = Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()));
	for (int32 NotchIndex = 0; NotchIndex < 3; ++NotchIndex)
	{
		TestTrue(TEXT("A notch toward the user through Slate's wheel completes"), Viewport->ScrollBy(FVector2D(-1.0, -1.0)));
	}
	TestNearlyEqual(TEXT("Three notches through Slate scrolled three notches' distance"), Box->GetScrollOffset(), 3.0f * Notch, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceFingerPanTest,
	"DreamGUI.ScrollBox.ThroughTheSlateInputSourceAFingerDraggedUpTheBoxPullsTheContentUpWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSlateSourceFingerPanTest, "DreamGUI.ScrollBox.ThroughTheSlateInputSourceAFingerDraggedUpTheBoxPullsTheContentUpWithIt", "[Touch][Animated]")

/*
 * SScrollBox pans on touch: a finger that lands on the box and travels up past the drag distance pulls the content up with
 * it. The finger's down, moves and up are its own pointer events at the source.
 */
bool FDreamSlateSourceFingerPanTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateSourceHostTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(SlateSourceOptions());
	if (!ComeUp(*this, Rig)
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamScrollBox* Box = MakeFilledBox(Rig);
	const TOptional<double> Scale = HasParts(Box) ? UnitsPerPixel(Rig, Box) : TOptional<double>();
	if (!TestTrue(TEXT("The box came up with a viewport on screen"), Scale.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("Dragging a finger 150 pixels up the box completes"),
		Rig.Driver()->Find(FDreamBy::Widget(Box->ViewportNode.Get()))->TouchDragBy(FVector2D(0.0, -150.0), 0));
	const float Offset = Box->GetScrollOffset();
	TestTrue(FString::Printf(TEXT("The drag pulled the content up with the finger (offset %.1f)"), Offset),
		Offset > static_cast<float>(0.5 * 150.0 * Scale.GetValue()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceRightDragTest,
	"DreamGUI.ScrollBox.ThroughTheSlateInputSourceARightButtonDragKeepsTheContentUnderThePointer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSlateSourceRightDragTest, "DreamGUI.ScrollBox.ThroughTheSlateInputSourceARightButtonDragKeepsTheContentUnderThePointer", "[Pointer][Animated]")

/*
 * The mouse's drag, which the controller's road has never been driven with: SScrollBox's right-button drag scrolls by every
 * move's delta once past the drag distance, the crossing move included, so 150 pixels of pull is 150 pixels of content.
 * The button is held across the moves as Slate's pressed-buttons set on every move event, and read before it is let go.
 */
bool FDreamSlateSourceRightDragTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateSourceHostTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(SlateSourceOptions());
	if (!ComeUp(*this, Rig)
		|| !TestTrue(TEXT("Its UI has begun play, as a game's has"), DreamDragInteraction::BeginPlayForUI(Rig.GetWorld())))
	{
		return false;
	}
	UDreamScrollBox* Box = MakeFilledBox(Rig);
	const TOptional<double> Scale = HasParts(Box) ? UnitsPerPixel(Rig, Box) : TOptional<double>();
	if (!TestTrue(TEXT("The box came up with a viewport on screen"), Scale.IsSet()))
	{
		return false;
	}
	const double FirstMove = FMath::Sqrt(static_cast<double>(Rig.Raycaster()->GetScaledDragThresholdSquare())) + 2.0;
	const double LaterMove = (150.0 - FirstMove) * 0.5;
	TestTrue(TEXT("The right-button drag up the box completes"),
		Rig.Driver()->Sequence()
			.MoveTo(FDreamBy::Widget(Box->ViewportNode.Get()))
			.Press(EDreamUIMouseButtonType::Right)
			.MoveBy(FVector2D(0.0, -FirstMove))
			.MoveBy(FVector2D(0.0, -LaterMove))
			.MoveBy(FVector2D(0.0, -LaterMove))
			.WaitFrames(1)
			.Perform());
	const float Offset = Box->GetScrollOffset();
	TestTrue(TEXT("Letting go completes"), Rig.Driver()->Sequence().Release(EDreamUIMouseButtonType::Right).Perform());
	TestNearlyEqual(TEXT("The content moved the full 150 pixels, staying under the pointer that grabbed it"),
		Offset, static_cast<float>(150.0 * Scale.GetValue()), static_cast<float>(1.5 * Scale.GetValue()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceCharactersTest,
	"DreamGUI.TextInput.ThroughTheSlateInputSourceCharactersByTheViewportsRoadLandInTheFieldBeingEdited",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSlateSourceCharactersTest, "DreamGUI.TextInput.ThroughTheSlateInputSourceCharactersByTheViewportsRoadLandInTheFieldBeingEdited", "[Pointer][Text][Animated]")

/*
 * Characters do not pass through the source: Slate routes a typed character to the focused viewport, whose client hands it
 * to UDreamUIInputSubsystem::HandleViewportCharacter. The click through Slate's mouse makes this field the one being
 * edited, and every character then lands in it, one change each (SEditableText raises OnTextChanged once per edit), with
 * nothing committed.
 */
bool FDreamSlateSourceCharactersTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateSourceHostTestLocal;
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(SlateSourceOptions());
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestNotNull(TEXT("A field came up"), Field))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the field through Slate's mouse completes"), Rig.Driver()->Find(FDreamBy::Widget(Field))->Click());
	if (!TestTrue(TEXT("The click began the field's edit"), IsEditing(Field)))
	{
		return false;
	}
	TestTrue(TEXT("Typing by the viewport's road completes"), Rig.Driver()->Sequence().Type(TEXT("hello")).Perform());
	TestEqual(TEXT("The field holds what was typed"), Field->GetText(), FString(TEXT("hello")));
	TestEqual(TEXT("Each character was announced on its own"), Listener->TextChangedCount, 5);
	TestEqual(TEXT("Typing is not committing"), Listener->TextCommittedCount, 0);
	TestTrue(TEXT("Enter ends the edit"), Rig.Driver()->Sequence().Key(EKeys::Enter).Perform());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSlateSourceEscapeTest,
	"DreamGUI.TextInput.ThroughTheSlateInputSourceEscapeEndsTheEditKeepingTheTextAndCommitsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamSlateSourceEscapeTest, "DreamGUI.TextInput.ThroughTheSlateInputSourceEscapeEndsTheEditKeepingTheTextAndCommitsOnce", "[Nav][Text][Animated]")

/*
 * Escape is Back, and a field being edited takes it as the end of its edit (Docs/Reference/DreamTextInput.md: Escape is
 * Back, and an edit that ends without Enter commits once; no revert by default, as in UMG). Heard from Slate as Escape's
 * key events, the same three claims the controller's road makes.
 */
bool FDreamSlateSourceEscapeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSlateSourceHostTestLocal;
	TStrongObjectPtr<UDreamTextInteractionListener> Listener(NewObject<UDreamTextInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(SlateSourceOptions());
	if (!ComeUp(*this, Rig))
	{
		return false;
	}
	UDreamTextInput* Field = MakeObservedField(Rig, Listener.Get());
	if (!TestNotNull(TEXT("A field came up"), Field))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking in and typing completes"), Rig.Driver()->Find(FDreamBy::Widget(Field))->Type(TEXT("abc")));
	if (!TestTrue(TEXT("The field is being edited"), IsEditing(Field)))
	{
		return false;
	}
	TestTrue(TEXT("Escape through Slate completes"), Rig.Driver()->Sequence().Key(EKeys::Escape).Perform());
	TestEqual(TEXT("Escape keeps what was typed"), Field->GetText(), FString(TEXT("abc")));
	TestFalse(TEXT("...ends the edit"), IsEditing(Field));
	TestEqual(TEXT("...and the edit that ended committed once"), Listener->TextCommittedCount, 1);
	return true;
}

#endif
