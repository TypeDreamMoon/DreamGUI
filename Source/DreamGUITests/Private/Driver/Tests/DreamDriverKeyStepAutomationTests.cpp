// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputTypes.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUISlateInputSource.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIInputAction.h"
#include "Interaction/UITextInput.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "DreamNavigationTestTypes.h"
#include "DreamPlayerScreenTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * KEYS AS A KEYBOARD SENDS THEM, UNDER EVERY INPUT HOST.
 *
 * Type(FKey) hands a key to whatever the driver decides should have it -- an armed key selector, Back, the field being
 * edited -- which is a fine way to type and no way at all to test what the pipeline does with a key. Key, KeyDown,
 * KeyUp, Tab and ShiftTab deliver it where the rig's host delivers keys instead: ModuleOnly down DreamUIKeyRouting::
 * RouteKey for the rig's player, the two actor hosts through the player controller with the modifier keys pressed
 * around it, SlateSource as Slate's key events to the world's Slate input source. The pipeline then decides who hears
 * it, in its own order -- the field, the focused widget and the bindings, navigation -- and these tests watch the
 * decision with probes that do not depend on what any one key means: a widget that records the keys and characters it
 * is offered, and a binding that counts.
 */
namespace DreamDriverKeyStepTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(200.0, 60.0);

	struct FHostCase
	{
		EDreamRigInputHost Host;
		const TCHAR* Name;
	};

	/** Every arrangement a headless rig has. Each test below runs its body once through each. */
	const FHostCase EveryHost[] = {
		{ EDreamRigInputHost::ModuleOnly, TEXT("module only") },
		{ EDreamRigInputHost::StandaloneActor, TEXT("standalone input actor") },
		{ EDreamRigInputHost::EnhancedActor, TEXT("Enhanced Input actor") },
		{ EDreamRigInputHost::SlateSource, TEXT("Slate source") },
	};

	FDreamRigOptions OptionsFor(EDreamRigInputHost InHost)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.InputHost = InHost;
		return Options;
	}

	/** "[Slate source] what", so a failure says which host it failed under. */
	FString Under(const FHostCase& InCase, const TCHAR* InWhat)
	{
		return FString::Printf(TEXT("[%s] %s"), InCase.Name, InWhat);
	}

	/** The rig-came-up claim, carrying the rig's own reason when it did not. */
	FString RigCameUp(const FHostCase& InCase, const FDreamDriverRig& InRig)
	{
		const FString& WhyNot = InRig.GetBuildFailure();
		return WhyNot.IsEmpty()
			? Under(InCase, TEXT("The rig came up"))
			: FString::Printf(TEXT("[%s] The rig came up -- it did not: %s"), InCase.Name, *WhyNot);
	}

	/**
	 * A plain widget that records every key, character and release it is offered, focused: where the bindings' walk
	 * begins (UDreamUIActionRouter offers the focused widget a key before any named binding) and where a character no
	 * field takes goes as a KeyChar. Not a navigation stop, so no key the pipeline gives a meaning moves the focus off it.
	 */
	UDreamKeyRecordingBehaviour* MakeFocusedRecorder(FDreamDriverRig& InRig)
	{
		// The controller a game would have: a field's key agent and the virtual cursor read it, and the actor hosts
		// already have one.
		InRig.EnsureGameInputHost();
		UDreamWidget* Target = InRig.MakeWidget(TEXT("Recorder"), nullptr, FVector2D(200.0, 100.0));
		UDreamKeyRecordingBehaviour* Recorder = IsValid(Target) ? Target->AddComponent<UDreamKeyRecordingBehaviour>() : nullptr;
		if (Recorder == nullptr || InRig.EventSystem() == nullptr)
		{
			return nullptr;
		}
		InRig.PumpFrames(1);
		InRig.EventSystem()->SetSelectComponentWithDefault(Target);
		return Recorder;
	}

	/** A global binding of InKey -- with Ctrl when bInRequiresCtrl -- on player 0, counting the times it fires. */
	struct FCountedBinding
	{
		TStrongObjectPtr<UDataTable> Table;
		TStrongObjectPtr<UDreamActionCallCounter> Counter;

		int32 Count() const { return Counter.IsValid() ? Counter->CallCount : -1; }
	};

	bool BindGlobalAction(UWorld* InWorld, const FKey& InKey, bool bInRequiresCtrl, FCountedBinding& OutBinding)
	{
		UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(InWorld);
		if (Router == nullptr)
		{
			return false;
		}
		const FName RowName(TEXT("DriverProbe"));
		OutBinding.Table.Reset(NewObject<UDataTable>(GetTransientPackage()));
		OutBinding.Table->RowStruct = FDreamUIInputActionData::StaticStruct();
		FDreamUIInputActionData Row;
		Row.DisplayName = FText::FromName(RowName);
		Row.KeyboardKey = InKey;
		Row.bRequiresCtrl = bInRequiresCtrl;
		OutBinding.Table->AddRow(RowName, Row);
		OutBinding.Counter.Reset(NewObject<UDreamActionCallCounter>());

		FDataTableRowHandle Handle;
		Handle.DataTable = OutBinding.Table.Get();
		Handle.RowName = RowName;
		FDreamUIActionExecutedDelegate Callback;
		Callback.BindUFunction(OutBinding.Counter.Get(), TEXT("Fire"));
		// No scope: a global binding answers with no screen open, which is all this rig has.
		Router->RegisterAction(nullptr, Handle, Callback);
		return true;
	}

	UDreamButton* MakeButton(FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InPosition, UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, nullptr, ButtonSize, InPosition);
		if (Button != nullptr && InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
			Button->OnPressed.AddDynamic(InListener, &UDreamPressInteractionListener::HandlePressed);
		}
		return Button;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	/**
	 * Which of two buttons the navigation landed on: the navigation highlight, else the player's focus -- a landing
	 * moves both, and which of the two a reader asks is the pipeline's business, not this test's. Null for neither.
	 */
	UDreamButton* LandedOn(const FDreamDriverRig& InRig, UDreamButton* InWest, UDreamButton* InEast)
	{
		const UDreamEventSystem* EventSystem = InRig.EventSystem();
		if (EventSystem == nullptr)
		{
			return nullptr;
		}
		const UDreamWidget* Candidates[] = { EventSystem->GetHighlightedComponentForNavigation(0), EventSystem->GetCurrentSelectedComponent(0) };
		for (const UDreamWidget* Candidate : Candidates)
		{
			if (IsPartOf(Candidate, InWest))
			{
				return InWest;
			}
			if (IsPartOf(Candidate, InEast))
			{
				return InEast;
			}
		}
		return nullptr;
	}
}

/**
 * Key is two steps, a frame each, as Navigate is: the press, which the pipeline acts on in the frame it is given, then
 * the release. KeyDown and KeyUp are the halves on their own, for a key held across frames -- the focused widget hears
 * the press, nothing more while the key is held, and the release when it comes, under every host.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverKeyHalvesTest,
	"DreamGUI.Driver.Keys.APressAndItsReleaseReachTheFocusedWidgetAsSeparateStepsUnderEveryHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverKeyHalvesTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverKeyStepTestLocal;
	for (const FHostCase& Case : EveryHost)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamKeyRecordingBehaviour* Recorder = MakeFocusedRecorder(Rig);
		if (!TestNotNull(*Under(Case, TEXT("A focused widget that records keys")), Recorder))
		{
			continue;
		}

		FDreamDriverSequence Whole = Rig.Driver()->Sequence();
		Whole.Key(EKeys::F9);
		TestEqual(Under(Case, TEXT("Key is two steps, the press and the release")), Whole.Num(), 2);

		// The sequence runs here and now (Perform), so the recorder the assertions read is the one the rig still holds.
		TestTrue(Under(Case, TEXT("A press held across frames, then its release, complete")), Rig.Driver()->Sequence()
			.KeyDown(EKeys::F9)
			.Then([this, &Case, Recorder](FDreamDriverContext&)
			{
				TestEqual(Under(Case, TEXT("The press reached the focused widget")), Recorder->KeyDownCount, 1);
				TestTrue(Under(Case, TEXT("...as the key it is")), Recorder->LastKey == EKeys::F9);
				TestEqual(Under(Case, TEXT("...and nothing was released yet")), Recorder->KeyUpCount, 0);
			})
			.WaitFrames(3)
			.Then([this, &Case, Recorder](FDreamDriverContext&)
			{
				TestEqual(Under(Case, TEXT("Held, the key is not pressed again")), Recorder->KeyDownCount, 1);
				TestEqual(Under(Case, TEXT("...nor released")), Recorder->KeyUpCount, 0);
			})
			.KeyUp(EKeys::F9)
			.Then([this, &Case, Recorder](FDreamDriverContext&)
			{
				TestEqual(Under(Case, TEXT("The release reached the focused widget, once")), Recorder->KeyUpCount, 1);
			})
			.Perform());

		TestTrue(Under(Case, TEXT("A whole Key completes")), Rig.Driver()->Sequence().Key(EKeys::F9).Perform());
		TestEqual(Under(Case, TEXT("...and is a second press")), Recorder->KeyDownCount, 2);
		TestEqual(Under(Case, TEXT("...and a second release")), Recorder->KeyUpCount, 2);
	}
	return true;
}

/**
 * Tab and Shift+Tab as a keyboard sends them: the key, and right after it the character '\t' its WM_CHAR carries, by
 * the viewport's road for characters. With no field being edited the character reaches the focused widget as a
 * KeyChar -- the proof it was sent, whatever Tab itself went on to mean.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTabCharacterTest,
	"DreamGUI.Driver.Keys.TabAndShiftTabSendTheTabCharacterRightAfterTheKeyUnderEveryHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDriverTabCharacterTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverKeyStepTestLocal;
	for (const FHostCase& Case : EveryHost)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamKeyRecordingBehaviour* Recorder = MakeFocusedRecorder(Rig);
		if (!TestNotNull(*Under(Case, TEXT("A focused widget that records characters")), Recorder))
		{
			continue;
		}

		FDreamDriverSequence Steps = Rig.Driver()->Sequence();
		Steps.Tab();
		TestEqual(Under(Case, TEXT("Tab is two steps, as Key is")), Steps.Num(), 2);

		TestTrue(Under(Case, TEXT("Tab completes")), Rig.Driver()->Sequence().Tab().Perform());
		TestEqual(Under(Case, TEXT("Its character reached the focused widget, once")), Recorder->KeyCharCount, 1);
		TestTrue(Under(Case, TEXT("Shift+Tab completes")), Rig.Driver()->Sequence().ShiftTab().Perform());
		TestEqual(Under(Case, TEXT("...and sends the same character")), Recorder->KeyCharCount, 2);
	}
	return true;
}

/**
 * A chord travels as a chord: the binding that asks for Ctrl+F9 fires on Ctrl+F9 and on nothing else, whether the
 * modifier is a state on the key's event (ModuleOnly, SlateSource) or a key held through the controller's input frame
 * (the actor hosts).
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverKeyChordTest,
	"DreamGUI.Driver.Keys.AChordReachesOnlyTheBindingThatAsksForItUnderEveryHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDriverKeyChordTest, "DreamGUI.Driver.Keys.AChordReachesOnlyTheBindingThatAsksForItUnderEveryHost", "[Nav][Animated]")

bool FDreamDriverKeyChordTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverKeyStepTestLocal;
	for (const FHostCase& Case : EveryHost)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		Rig.EnsureGameInputHost();
		FCountedBinding CtrlF9;
		if (!TestTrue(Under(Case, TEXT("Ctrl+F9 is bound")), BindGlobalAction(Rig.GetWorld(), EKeys::F9, /*bInRequiresCtrl*/ true, CtrlF9)))
		{
			continue;
		}

		TestTrue(Under(Case, TEXT("The F9 key alone completes")), Rig.Driver()->Sequence().Key(EKeys::F9).Perform());
		TestEqual(Under(Case, TEXT("...and does not fire the Ctrl+F9 binding")), CtrlF9.Count(), 0);
		TestTrue(Under(Case, TEXT("Shift+F9 completes")), Rig.Driver()->Sequence().Key(EKeys::F9, EDreamDriverModifierKeys::Shift).Perform());
		TestEqual(Under(Case, TEXT("...and does not fire it either")), CtrlF9.Count(), 0);
		TestTrue(Under(Case, TEXT("Ctrl+F9 completes")), Rig.Driver()->Sequence().Key(EKeys::F9, EDreamDriverModifierKeys::Ctrl).Perform());
		TestEqual(Under(Case, TEXT("...and fires the binding, once")), CtrlF9.Count(), 1);
		TestTrue(Under(Case, TEXT("Ctrl+Shift+F9 completes")),
			Rig.Driver()->Sequence().Key(EKeys::F9, EDreamDriverModifierKeys::Ctrl | EDreamDriverModifierKeys::Shift).Perform());
		TestEqual(Under(Case, TEXT("...and fires it again: a modifier the binding does not ask for does not silence it")), CtrlF9.Count(), 2);
	}
	return true;
}

/**
 * The pipeline's order, not the driver's: a key reaches the bindings before navigation. An arrow key moves between two
 * buttons; bound to an action, the same key fires the action and the focus stays where it was. Under ModuleOnly this
 * is what RouteKey does with it -- the module's own Navigate would have skipped the bindings altogether.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverKeyBindingBeforeNavigationTest,
	"DreamGUI.Driver.Keys.ABoundDirectionKeyFiresItsBindingInsteadOfNavigatingUnderEveryHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDriverKeyBindingBeforeNavigationTest, "DreamGUI.Driver.Keys.ABoundDirectionKeyFiresItsBindingInsteadOfNavigatingUnderEveryHost", "[Nav][Animated]")

bool FDreamDriverKeyBindingBeforeNavigationTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverKeyStepTestLocal;
	for (const FHostCase& Case : EveryHost)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		// 400 canvas units apart on one row: each is the other's only neighbour to the side.
		UDreamButton* West = MakeButton(Rig, TEXT("West"), FVector2D(-200.0, 0.0), nullptr);
		UDreamButton* East = MakeButton(Rig, TEXT("East"), FVector2D(200.0, 0.0), nullptr);
		if (!TestTrue(Under(Case, TEXT("Two buttons can be made on the rig")), West != nullptr && East != nullptr))
		{
			continue;
		}
		Rig.PumpFrames(1);

		// The first press of all lands on whichever button the pipeline starts from; the test goes on from there.
		TestTrue(Under(Case, TEXT("The first arrow key completes")), Rig.Driver()->Sequence().Key(EKeys::Right).Perform());
		UDreamButton* First = LandedOn(Rig, West, East);
		if (!TestNotNull(*Under(Case, TEXT("The first arrow key landed on one of the two buttons")), First))
		{
			continue;
		}
		UDreamButton* Other = First == West ? East : West;
		const FKey TowardOther = First == West ? EKeys::Right : EKeys::Left;
		const FKey TowardFirst = First == West ? EKeys::Left : EKeys::Right;
		TestTrue(Under(Case, TEXT("The arrow toward the other button completes")), Rig.Driver()->Sequence().Key(TowardOther).Perform());
		TestTrue(Under(Case, TEXT("...and navigation moved there")), LandedOn(Rig, West, East) == Other);

		FCountedBinding Bound;
		if (!TestTrue(Under(Case, TEXT("The arrow back is bound")), BindGlobalAction(Rig.GetWorld(), TowardFirst, /*bInRequiresCtrl*/ false, Bound)))
		{
			continue;
		}
		TestTrue(Under(Case, TEXT("The bound arrow completes")), Rig.Driver()->Sequence().Key(TowardFirst).WaitFrames(1).Perform());
		TestEqual(Under(Case, TEXT("The binding was offered the key first, and fired once")), Bound.Count(), 1);
		TestTrue(Under(Case, TEXT("...and the key it took did not also navigate")), LandedOn(Rig, West, East) == Other);
	}
	return true;
}

/**
 * Whatever Tab does to a field being edited, it never types: the field is given the key in the pipeline's order and the
 * character '\t' after it, and a single-line field takes neither as text.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverTabInFieldTest,
	"DreamGUI.Driver.Keys.TabInAFieldBeingEditedNeverTypesTheTabCharacterUnderEveryHost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDriverTabInFieldTest, "DreamGUI.Driver.Keys.TabInAFieldBeingEditedNeverTypesTheTabCharacterUnderEveryHost", "[Pointer][Text][Animated]")

bool FDreamDriverTabInFieldTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverKeyStepTestLocal;
	for (const FHostCase& Case : EveryHost)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
		Rig.BindTest(this);
		if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
		{
			continue;
		}
		UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Username"), nullptr, FVector2D(320.0, 40.0));
		if (!TestTrue(Under(Case, TEXT("A field can be made on the rig")), Field != nullptr && Field->InputBehaviour != nullptr))
		{
			continue;
		}
		Rig.PumpFrames(1);
		TestTrue(Under(Case, TEXT("Clicking the field completes")), Rig.Driver()->Find(FDreamBy::Name(TEXT("Username")))->Click());
		if (!TestTrue(Under(Case, TEXT("The click began an edit")), Field->InputBehaviour->IsInputActive()))
		{
			continue;
		}
		TestTrue(Under(Case, TEXT("Typing completes")), Rig.Driver()->Sequence().Type(TEXT("ab")).Perform());
		TestTrue(Under(Case, TEXT("Tab completes")), Rig.Driver()->Sequence().Tab().WaitFrames(1).Perform());
		TestEqual(Under(Case, TEXT("The field holds what was typed and no tab character")), Field->GetText(), FString(TEXT("ab")));
	}
	return true;
}

/**
 * The SlateSource arrangement is the world's own Slate source, fed by the driver alone: on, so the world's input is
 * heard from it; off Slate's list of pre-processors, so the machine's own mouse and keyboard never reach the rig; and
 * with the module's stand-in cursor put away, so the pointer is Slate's mouse. A click is then a mouse move and its
 * button's down and up, and a tap a finger's own pointer events -- each pressing what it was aimed at, once.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDriverSlateSourceRigTest,
	"DreamGUI.Driver.SlateSource.TheRigsInputReachesTheWorldsSlateSourceAsSlateEventsAndNeverFromTheDesk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamDriverSlateSourceRigTest, "DreamGUI.Driver.SlateSource.TheRigsInputReachesTheWorldsSlateSourceAsSlateEventsAndNeverFromTheDesk", "[Pointer][Touch][Animated]")

bool FDreamDriverSlateSourceRigTest::RunTest(const FString& Parameters)
{
	using namespace DreamDriverKeyStepTestLocal;
	const FHostCase Case = { EDreamRigInputHost::SlateSource, TEXT("Slate source") };
	FDreamDriverRig Rig = FDreamDriverRig::Headless(OptionsFor(Case.Host));
	Rig.BindTest(this);
	if (!TestTrue(RigCameUp(Case, Rig), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	const TSharedPtr<FDreamUISlateInputSource> Source = Input != nullptr ? Input->GetSlateInputSource() : nullptr;
	if (!TestTrue(TEXT("The world's input is heard from its Slate source"), Input != nullptr && Input->IsSlateInputSourceActive() && Source.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("...which is not on Slate's own list, so nothing from the desk reaches it"),
		FSlateApplication::Get().FindInputPreProcessor(Source, EInputPreProcessorType::Game), static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("...nor does the desk's window focus: the source is not told when the application goes to the background"),
		FSlateApplication::Get().OnApplicationActivationStateChanged().IsBoundToObject(Source.Get()));
	TestFalse(TEXT("The module stands no cursor of its own in for the mouse"), Rig.InputModule()->GetOverrideMousePosition());

	const TStrongObjectPtr<UDreamPressInteractionListener> ClickListener(NewObject<UDreamPressInteractionListener>());
	const TStrongObjectPtr<UDreamPressInteractionListener> TapListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Play = MakeButton(Rig, TEXT("Play"), FVector2D(-200.0, 0.0), ClickListener.Get());
	UDreamButton* Tap = MakeButton(Rig, TEXT("Tap"), FVector2D(200.0, 0.0), TapListener.Get());
	if (!TestTrue(TEXT("Two buttons"), Play != nullptr && Tap != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("A click through Slate's mouse completes"), Rig.Driver()->Find(FDreamBy::Widget(Play))->Click());
	TestEqual(TEXT("...pressed the button once"), ClickListener->PressedCount, 1);
	TestEqual(TEXT("...and clicked it once"), ClickListener->ClickedCount, 1);
	const TOptional<FVector2D> PlayCentre = FDreamDriverProjection::WidgetCentrePixel(Play);
	const UDreamUIInputUser* User = Input->GetUser(0);
	const UDreamPointerEventData* Mouse = User != nullptr ? User->FindPointerEventData(DreamUIPointerIds::Mouse) : nullptr;
	TestTrue(TEXT("The mouse's own pointer is where Slate's mouse was moved"),
		PlayCentre.IsSet() && Mouse != nullptr && Mouse->PointerPosition.Equals(FVector(PlayCentre.GetValue(), 0.0)));

	const TOptional<FVector2D> TapCentre = FDreamDriverProjection::WidgetCentrePixel(Tap);
	if (!TestTrue(TEXT("The second button has a centre on the viewport"), TapCentre.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A finger down and up on the second button completes"),
		Rig.Driver()->Sequence().TouchDown(0, TapCentre.GetValue()).TouchUp(0).WaitFrames(1).Perform());
	TestEqual(TEXT("...and the tap pressed it once"), TapListener->PressedCount, 1);
	TestEqual(TEXT("...and clicked it once"), TapListener->ClickedCount, 1);
	TestEqual(TEXT("...leaving the first button alone"), ClickListener->ClickedCount, 1);
	return true;
}

#endif
