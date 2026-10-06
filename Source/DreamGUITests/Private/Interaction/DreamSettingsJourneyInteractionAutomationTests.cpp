// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamListView.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamTabView.h"
#include "Controls/DreamTextInput.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIModal.h"
#include "Interaction/UIButton.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"
#include "Interaction/DreamListsInteractionTestTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A settings screen, used from end to end the way a player uses one: open the Audio tab, scroll the presets, choose a
 * quality, set the volume, type a name, press Apply, and back out of the confirmation -- once with the mouse, once with
 * a pad, once with a finger.
 *
 * Every control on it has tests of its own. What only a journey shows is that they still behave once they share a
 * screen: the tab view gives its page the input, the dropdown's list opens over the controls below it and lets go of the
 * pointer when it closes, the confirmation's scrim holds every control underneath until it is dismissed, and dismissing
 * it hands the screen back as it was. Each step asserts its own state before the next one is taken, so a broken step is
 * named by the journey rather than by whatever went wrong after it.
 *
 * The confirmation is a modal (UDreamUIModalSubsystem), brought up by the Apply button's click, as a CommonUI settings
 * screen pushes its confirm dialog: Back closes it with "Back", the dialog's own buttons with their results.
 */
namespace DreamSettingsJourneyTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** One notch toward the user, in the shape the production input actors send a wheel. */
	const FVector2D WheelTowardUser(-1.0, -1.0);
	constexpr float PresetRowHeight = 40.0f;
	constexpr int32 PresetCount = 10;

	/** What the screen answered, kept outside the rig so an answer given while it is torn down still lands somewhere alive. */
	struct FAnswers
	{
		int32 ApplyClicks = 0;
		TArray<FName> Confirmations;
	};

	struct FSettingsScreen
	{
		UDreamTabView* Tabs = nullptr;
		UDreamWidget* VideoPage = nullptr;
		UDreamWidget* AudioPage = nullptr;
		UDreamListView* Presets = nullptr;
		UDreamDropdown* Quality = nullptr;
		UDreamSlider* Volume = nullptr;
		UDreamTextInput* PlayerName = nullptr;
		UDreamButton* Apply = nullptr;
		UDreamUIModalSubsystem* Modals = nullptr;
		TArray<UObject*> PresetItems;

		bool IsReady(bool bInWithName) const
		{
			return Tabs != nullptr && Tabs->Tabs.Num() == 2 && VideoPage != nullptr && AudioPage != nullptr && Presets != nullptr
				&& Quality != nullptr && Volume != nullptr && Volume->HandleNode != nullptr && Volume->HandleAreaNode != nullptr
				&& (!bInWithName || PlayerName != nullptr) && Apply != nullptr && Apply->ButtonBehaviour != nullptr && Modals != nullptr;
		}

		UDreamWidget* TabAt(int32 InIndex) const
		{
			return Tabs != nullptr && Tabs->Tabs.IsValidIndex(InIndex) ? Tabs->Tabs[InIndex].TabNode.Get() : nullptr;
		}
	};

	/**
	 * Two tabs, Video and Audio. The Audio page holds, top to bottom: a list of presets taller than its window, a quality
	 * dropdown, a volume slider, a name field (unless bInWithName is false) and an Apply button that asks for
	 * confirmation. Laid out in one column, so the pad's walk down the page meets them in that order.
	 */
	FSettingsScreen BuildScreen(FAutomationTestBase& InTest, FDreamDriverRig& InRig, FAnswers& InAnswers,
		UDreamPressInteractionListener* InListener, bool bInWithName)
	{
		FSettingsScreen Screen;
		InRig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The headless rig came up"), InRig.IsUsable()))
		{
			return Screen;
		}
		Screen.Modals = UDreamUIModalSubsystem::Get(InRig.GetWorld());
		Screen.Tabs = InRig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, FVector2D(900.0, 620.0));
		Screen.VideoPage = InRig.MakeWidget(TEXT("Video"), nullptr, FVector2D(860.0, 500.0));
		Screen.AudioPage = InRig.MakeWidget(TEXT("Audio"), nullptr, FVector2D(860.0, 500.0));
		if (Screen.Tabs == nullptr || Screen.VideoPage == nullptr || Screen.AudioPage == nullptr)
		{
			return Screen;
		}
		InRig.MakeControl<UDreamButton>(TEXT("ResetVideo"), Screen.VideoPage, FVector2D(200.0, 50.0));

		Screen.Presets = InRig.MakeControl<UDreamListView>(TEXT("Presets"), Screen.AudioPage, FVector2D(400.0, 160.0), FVector2D(0.0, 150.0));
		if (Screen.Presets != nullptr)
		{
			Screen.Presets->SetStyleSource(EDreamUIStyleSource::Inline);
			Screen.Presets->SetStyle(DreamListsInteraction::WithRows(Screen.Presets->GetStyle(), PresetRowHeight));
			Screen.PresetItems = DreamListsInteraction::MakeItems(PresetCount);
			Screen.Presets->SetItemObjects(Screen.PresetItems);
		}
		Screen.Quality = InRig.MakeControl<UDreamDropdown>(TEXT("Quality"), Screen.AudioPage, FVector2D(400.0, 40.0), FVector2D(0.0, 30.0));
		if (Screen.Quality != nullptr)
		{
			Screen.Quality->SetOptions({ FText::AsCultureInvariant(TEXT("Low")), FText::AsCultureInvariant(TEXT("Medium")),
				FText::AsCultureInvariant(TEXT("High")), FText::AsCultureInvariant(TEXT("Epic")) });
			Screen.Quality->SetSelectedIndex(0);
			Screen.Quality->OnItemGenerated.AddDynamic(InListener, &UDreamPressInteractionListener::HandleItemGenerated);
			Screen.Quality->OnSelectionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleSelectionChanged);
		}
		Screen.Volume = InRig.MakeControl<UDreamSlider>(TEXT("Volume"), Screen.AudioPage, FVector2D(400.0, 40.0), FVector2D(0.0, -40.0));
		if (bInWithName)
		{
			Screen.PlayerName = InRig.MakeControl<UDreamTextInput>(TEXT("PlayerName"), Screen.AudioPage, FVector2D(400.0, 40.0), FVector2D(0.0, -110.0));
		}
		Screen.Apply = InRig.MakeControl<UDreamButton>(TEXT("Apply"), Screen.AudioPage, FVector2D(200.0, 50.0), FVector2D(0.0, -190.0));
		if (Screen.Apply != nullptr && Screen.Apply->ButtonBehaviour != nullptr && Screen.Modals != nullptr)
		{
			FAnswers* Answers = &InAnswers;
			UDreamUIModalSubsystem* Modals = Screen.Modals;
			Screen.Apply->ButtonBehaviour->GetOnClickEvent().AddLambda([Answers, Modals]()
			{
				++Answers->ApplyClicks;
				Modals->ShowModalNative(UDreamDialog::StaticClass(), [Answers](FName InResult) { Answers->Confirmations.Add(InResult); }, 0);
			});
		}
		Screen.Tabs->AddPage(Screen.VideoPage);
		Screen.Tabs->AddPage(Screen.AudioPage);
		Screen.Tabs->SetFocusPageOnTabChange(true);
		Screen.Tabs->SetActiveTabIndex(0);
		// Every click below is one of its own, never the second half of a double click.
		InRig.EventSystem()->SetDoubleClickTime(0.0f);
		InRig.PumpFrames(2);
		InTest.TestTrue(TEXT("The settings screen came up with its two tabs and every control on its Audio page"), Screen.IsReady(bInWithName));
		return Screen;
	}

	/** Pump until InCondition holds, for at most InSeconds; false, reported, when it never did. */
	bool PumpUntil(FDreamDriverRig& InRig, TFunction<bool()> InCondition, double InSeconds, const TCHAR* InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(InSeconds);
		return InRig.Driver()->Wait(FDreamUntil::Condition(MoveTemp(InCondition), Timeout), Timeout, InWhat);
	}

	/** Whether player 0's focus is on InControl or on one of its parts. */
	bool IsFocusOn(FDreamDriverRig& InRig, const UDreamWidget* InControl)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		const UDreamWidget* Focused = Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
		return Focused != nullptr && InControl != nullptr && (Focused == InControl || Focused->IsChildOf(InControl));
	}

	/** Where along the volume's travel a fraction of it is, in pixels, and the slider's handle as something to drag. */
	bool VolumeTravel(FDreamDriverRig& InRig, const FSettingsScreen& InScreen, TOptional<FBox2D>& OutTravel, TOptional<FVector2D>& OutGrip)
	{
		OutTravel = InRig.Driver()->Find(FDreamBy::Widget(InScreen.Volume->HandleAreaNode.Get()))->GetPixelRect();
		OutGrip = InRig.Driver()->Find(FDreamBy::Widget(InScreen.Volume->HandleNode.Get()))->GetCentrePixel();
		return OutTravel.IsSet() && OutGrip.IsSet() && OutTravel->Max.X - OutTravel->Min.X > 100.0;
	}

	/** The open confirmation's button answering InResult, or null. */
	UDreamButton* ConfirmationButton(const FSettingsScreen& InScreen, FName InResult)
	{
		const UDreamDialog* Dialog = InScreen.Modals != nullptr ? Cast<UDreamDialog>(InScreen.Modals->GetActiveModalWidget(0)) : nullptr;
		if (Dialog == nullptr)
		{
			return nullptr;
		}
		const TArray<FDreamDialogButton> Specs = Dialog->GetButtons();
		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			if (Specs[Index].Result == InResult && Dialog->ButtonWidgets.IsValidIndex(Index))
			{
				return Dialog->ButtonWidgets[Index].Get();
			}
		}
		return nullptr;
	}

	/**
	 * A pixel of the Audio page's controls that the open confirmation's panel does not cover, so a click there lands on the
	 * scrim: a point along the volume's track, or else the middle of the preset list or of the Video tab. The dialog's
	 * panel is centred over the page and may hide the track; a click on it is the dialog's, not the screen's underneath.
	 */
	TOptional<FVector2D> PixelBehindTheConfirmation(FDreamDriverRig& InRig, const FSettingsScreen& InScreen, const FBox2D& InTravel)
	{
		const UDreamDialog* Dialog = InScreen.Modals != nullptr ? Cast<UDreamDialog>(InScreen.Modals->GetActiveModalWidget(0)) : nullptr;
		const TOptional<FBox2D> Panel = Dialog != nullptr && Dialog->PanelNode != nullptr
			? InRig.Driver()->Find(FDreamBy::Widget(Dialog->PanelNode.Get()))->GetPixelRect() : TOptional<FBox2D>();
		if (!Panel.IsSet())
		{
			return TOptional<FVector2D>();
		}
		const auto Uncovered = [&Panel](const FVector2D& InPixel) { return !Panel->ExpandBy(4.0).IsInside(InPixel); };
		for (int32 Step = 1; Step < 20; ++Step)
		{
			const FVector2D OnTrack(InTravel.Min.X + Step / 20.0 * (InTravel.Max.X - InTravel.Min.X), InTravel.GetCenter().Y);
			if (Uncovered(OnTrack))
			{
				return OnTrack;
			}
		}
		for (UDreamWidget* Fallback : { static_cast<UDreamWidget*>(InScreen.Presets), InScreen.TabAt(0) })
		{
			const TOptional<FVector2D> Middle = Fallback != nullptr ? InRig.Driver()->Find(FDreamBy::Widget(Fallback))->GetCentrePixel() : TOptional<FVector2D>();
			if (Middle.IsSet() && Uncovered(Middle.GetValue()))
			{
				return Middle;
			}
		}
		return TOptional<FVector2D>();
	}

	/** What the Audio page holds now, for the check that dismissing the confirmation handed the screen back as it was. */
	struct FAudioState
	{
		int32 Tab = INDEX_NONE;
		int32 Quality = INDEX_NONE;
		float Volume = -1.0f;
		FString Name;

		static FAudioState Of(const FSettingsScreen& InScreen)
		{
			FAudioState State;
			State.Tab = InScreen.Tabs->GetActiveTabIndex();
			State.Quality = InScreen.Quality->GetSelectedIndex();
			State.Volume = InScreen.Volume->GetValue();
			State.Name = InScreen.PlayerName != nullptr ? InScreen.PlayerName->GetText() : FString();
			return State;
		}

		bool operator==(const FAudioState& InOther) const
		{
			return Tab == InOther.Tab && Quality == InOther.Quality && FMath::IsNearlyEqual(Volume, InOther.Volume, 0.001f) && Name == InOther.Name;
		}

		FString Describe() const
		{
			return FString::Printf(TEXT("tab %d, quality %d, volume %.3f, name \"%s\""), Tab, Quality, Volume, *Name);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSettingsJourneyMouseTest,
	"DreamGUI.Journey.Settings.WithTheMouseAPlayerOpensATabScrollsChoosesSlidesTypesAppliesAndEscapesTheConfirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSettingsJourneyMouseTest::RunTest(const FString& Parameters)
{
	using namespace DreamSettingsJourneyTestLocal;
	FAnswers Answers;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FSettingsScreen Screen = BuildScreen(*this, Rig, Answers, Listener.Get(), /*bInWithName*/ true);
	if (!Screen.IsReady(true))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();

	// The Audio tab, clicked: its page is the one shown.
	TestTrue(TEXT("Clicking the Audio tab completes"), Driver->Find(FDreamBy::Widget(Screen.TabAt(1)))->Click());
	if (!TestEqual(TEXT("The Audio tab is open"), Screen.Tabs->GetActiveTabIndex(), 1)
		|| !TestTrue(TEXT("...its page shown"), Driver->Find(FDreamBy::Widget(Screen.AudioPage))->IsVisible())
		|| !TestFalse(TEXT("...and the Video page not"), Driver->Find(FDreamBy::Widget(Screen.VideoPage))->IsVisible()))
	{
		return false;
	}

	// The presets, scrolled by the wheel over them.
	TestTrue(TEXT("A wheel notch over the presets completes"), Driver->Find(FDreamBy::Widget(Screen.Presets))->ScrollBy(WheelTowardUser));
	if (!TestTrue(TEXT("The presets scrolled toward their end"), PumpUntil(Rig, [&Screen]() { return Screen.Presets->GetScrollOffset() > 0.0f; }, 1.0, TEXT("the presets scrolled"))))
	{
		return false;
	}

	// The quality, chosen from its list.
	TestTrue(TEXT("Clicking the quality dropdown completes"), Driver->Find(FDreamBy::Widget(Screen.Quality))->Click());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("Its list is open"), Screen.Quality->IsOpen())
		|| !TestTrue(TEXT("...with a row for High"), Listener->GeneratedItems.IsValidIndex(2) && Listener->GeneratedItems[2] != nullptr))
	{
		return false;
	}
	TestTrue(TEXT("Clicking High completes"), Driver->Find(FDreamBy::Widget(Listener->GeneratedItems[2].Get()))->Click());
	if (!TestEqual(TEXT("High is the quality"), Screen.Quality->GetSelectedIndex(), 2) || !TestFalse(TEXT("...and the list closed"), Screen.Quality->IsOpen()))
	{
		return false;
	}

	// The volume, dragged three quarters of the way along its travel.
	TOptional<FBox2D> Travel;
	TOptional<FVector2D> Grip;
	if (!TestTrue(TEXT("The volume's handle and travel are on screen"), VolumeTravel(Rig, Screen, Travel, Grip)))
	{
		return false;
	}
	const float OnePixel = static_cast<float>(1.0 / (Travel->Max.X - Travel->Min.X));
	const double TargetX = Travel->Min.X + 0.75 * (Travel->Max.X - Travel->Min.X);
	TestTrue(TEXT("Dragging the volume's handle completes"), Driver->Find(FDreamBy::Widget(Screen.Volume->HandleNode.Get()))->DragBy(FVector2D(TargetX - Grip->X, 0.0)));
	if (!TestNearlyEqual(TEXT("The volume is three quarters"), Screen.Volume->GetValue(), 0.75f, OnePixel * 1.5f))
	{
		return false;
	}

	// The name, typed and committed.
	TestTrue(TEXT("Clicking the name field and typing completes"), Driver->Find(FDreamBy::Widget(Screen.PlayerName))->Type(TEXT("Kim")));
	TestTrue(TEXT("Enter completes"), Driver->Sequence().Key(EKeys::Enter).Perform());
	if (!TestEqual(TEXT("The name field holds what was typed"), Screen.PlayerName->GetText(), FString(TEXT("Kim"))))
	{
		return false;
	}

	// Apply, and its confirmation.
	const FAudioState Chosen = FAudioState::Of(Screen);
	TestTrue(TEXT("Clicking Apply completes"), Driver->Find(FDreamBy::Widget(Screen.Apply))->Click());
	Rig.PumpFrames(2);
	if (!TestEqual(TEXT("Apply was clicked once"), Answers.ApplyClicks, 1) || !TestEqual(TEXT("...and asks for confirmation"), Screen.Modals->GetModalDepth(0), 1))
	{
		return false;
	}
	// The scrim holds the screen underneath: a click on a control behind the confirmation -- where the dialog's panel
	// is not -- moves nothing, and leaves the confirmation up.
	const TOptional<FVector2D> Behind = PixelBehindTheConfirmation(Rig, Screen, Travel.GetValue());
	if (!TestTrue(TEXT("Some control of the page shows past the confirmation's panel"), Behind.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A click on the page behind the confirmation completes"), Driver->Sequence().MoveToPixel(Behind.GetValue()).Press().Release().Perform());
	TestTrue(FString::Printf(TEXT("...and the screen underneath took none of it: %s"), *FAudioState::Of(Screen).Describe()), FAudioState::Of(Screen) == Chosen);
	TestEqual(TEXT("...and the confirmation is still up, unanswered"), Answers.Confirmations.Num(), 0);

	// Escape: the confirmation goes, answering Back, and the screen is handed back as it was.
	TestTrue(TEXT("Escape completes"), Driver->Sequence().Key(EKeys::Escape).WaitFrames(1).Perform());
	TestEqual(TEXT("Escape closed the confirmation"), Screen.Modals->GetModalDepth(0), 0);
	if (TestEqual(TEXT("...answering once"), Answers.Confirmations.Num(), 1))
	{
		TestEqual(TEXT("...with Back"), Answers.Confirmations[0], FName(TEXT("Back")));
	}
	TestTrue(FString::Printf(TEXT("The screen is as the player left it: %s"), *FAudioState::Of(Screen).Describe()), FAudioState::Of(Screen) == Chosen);
	TestTrue(TEXT("Clicking the Video tab completes"), Driver->Find(FDreamBy::Widget(Screen.TabAt(0)))->Click());
	TestEqual(TEXT("...and the screen takes the click again: the Video tab is open"), Screen.Tabs->GetActiveTabIndex(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSettingsJourneyPadTest,
	"DreamGUI.Journey.Settings.WithThePadAPlayerWalksTheScreenByDPadConfirmBackAndTheShoulders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The same screen without the name field -- typing with a pad needs a virtual keyboard, which is a road of its own -- and
 * no pointer at all. The screen opens with the focus on its first tab. The right shoulder opens Audio and the focus goes
 * into its page; the D-pad walks down the presets, which scroll to keep the focus in view, and on past their last row to
 * the quality; confirm opens it, the D-pad goes down two options and confirm chooses; down to the volume, confirm takes
 * it, right three times steps it up and confirm lets it go; down to Apply, confirm asks for confirmation with the focus
 * on the dialog; Back dismisses it; the left shoulder goes back to Video. UMG under CommonUI: geometric navigation, the
 * combo box's list taking the D-pad, SSlider taking the pad's Accept before stepping on Left and Right (USlider's
 * RequiresControllerLock, on by default), the modal's Back.
 */
bool FDreamSettingsJourneyPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamSettingsJourneyTestLocal;
	FAnswers Answers;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FSettingsScreen Screen = BuildScreen(*this, Rig, Answers, Listener.Get(), /*bInWithName*/ false);
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (!Screen.IsReady(false) || !TestNotNull(TEXT("The rig's world has input"), Services))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();
	Rig.EventSystem()->ReportInputDevice(EDreamUIInputDevice::Gamepad);
	const auto Press = [&Driver](const FKey& InKey) { return Driver->Sequence().Key(InKey).WaitFrames(1).Perform(); };
	const auto Confirm = [&Driver]() { return Driver->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform(); };

	// The screen's initial focus, as a page gives it when it opens.
	if (!TestTrue(TEXT("The first tab takes the screen's initial focus"), Services->FocusForNavigation(Screen.TabAt(0), 0)))
	{
		return false;
	}

	TestTrue(TEXT("The right shoulder completes"), Press(EKeys::Gamepad_RightShoulder));
	if (!TestEqual(TEXT("The right shoulder opened Audio"), Screen.Tabs->GetActiveTabIndex(), 1)
		|| !TestTrue(TEXT("...and the focus went into its page"), IsFocusOn(Rig, Screen.AudioPage)))
	{
		return false;
	}

	// Down the presets and on past them to the quality.
	int32 Presses = 0;
	while (!IsFocusOn(Rig, Screen.Quality) && Presses < PresetCount + 4)
	{
		Press(EKeys::Gamepad_DPad_Down);
		++Presses;
	}
	if (!TestTrue(FString::Printf(TEXT("The D-pad walked down the presets to the quality in %d presses"), Presses), IsFocusOn(Rig, Screen.Quality)))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("...and the presets scrolled to keep the focus in view on the way (offset %.1f)"), Screen.Presets->GetScrollOffset()),
		Screen.Presets->GetScrollOffset() > 0.0f);

	// The quality: open, two down, choose.
	TestTrue(TEXT("Confirm on the quality completes"), Confirm());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("Confirm opened its list"), Screen.Quality->IsOpen()))
	{
		return false;
	}
	Press(EKeys::Gamepad_DPad_Down);
	Press(EKeys::Gamepad_DPad_Down);
	TestTrue(TEXT("Confirm on the third option completes"), Confirm());
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("The pad chose High"), Screen.Quality->GetSelectedIndex(), 2) || !TestFalse(TEXT("...and the list closed"), Screen.Quality->IsOpen())
		|| !TestTrue(TEXT("...with the focus back on the dropdown"), IsFocusOn(Rig, Screen.Quality)))
	{
		return false;
	}

	// The volume: down onto it, three steps right.
	Press(EKeys::Gamepad_DPad_Down);
	if (!TestTrue(TEXT("Down from the quality reaches the volume"), IsFocusOn(Rig, Screen.Volume)))
	{
		return false;
	}
	// Confirm takes the slider, as an SSlider that requires the controller lock (UMG's USlider default) takes the pad's
	// Accept before Left and Right are its own; confirm again gives the directions back to navigation.
	const float VolumeBefore = Screen.Volume->GetValue();
	TestTrue(TEXT("Confirm on the volume completes"), Confirm());
	Press(EKeys::Gamepad_DPad_Right);
	const float AfterOne = Screen.Volume->GetValue();
	Press(EKeys::Gamepad_DPad_Right);
	Press(EKeys::Gamepad_DPad_Right);
	const float AfterThree = Screen.Volume->GetValue();
	TestTrue(FString::Printf(TEXT("Right steps the volume up: %.3f -> %.3f"), VolumeBefore, AfterOne), AfterOne > VolumeBefore);
	TestTrue(FString::Printf(TEXT("...by the same step each time: three steps make %.3f"), AfterThree),
		FMath::IsNearlyEqual(AfterThree - VolumeBefore, 3.0f * (AfterOne - VolumeBefore), 0.001f));
	TestTrue(TEXT("...and the focus stays on the volume"), IsFocusOn(Rig, Screen.Volume));
	TestTrue(TEXT("Confirm on the volume again, letting it go, completes"), Confirm());

	// Apply, its confirmation, and Back.
	Press(EKeys::Gamepad_DPad_Down);
	if (!TestTrue(TEXT("Down from the volume reaches Apply"), IsFocusOn(Rig, Screen.Apply)))
	{
		return false;
	}
	TestTrue(TEXT("Confirm on Apply completes"), Confirm());
	Rig.PumpFrames(2);
	if (!TestEqual(TEXT("Apply asked for confirmation"), Screen.Modals->GetModalDepth(0), 1))
	{
		return false;
	}
	const UDreamUserWidget* Dialog = Screen.Modals->GetActiveModalWidget(0);
	TestTrue(TEXT("...with the focus on the confirmation"), Dialog != nullptr && IsFocusOn(Rig, Dialog));
	TestTrue(TEXT("Back completes"), Driver->Sequence().Back().WaitFrames(1).Perform());
	TestEqual(TEXT("Back closed the confirmation"), Screen.Modals->GetModalDepth(0), 0);
	if (TestEqual(TEXT("...answering once"), Answers.Confirmations.Num(), 1))
	{
		TestEqual(TEXT("...with Back"), Answers.Confirmations[0], FName(TEXT("Back")));
	}
	TestTrue(TEXT("...and the focus came back to the screen it was raised from"), IsFocusOn(Rig, Screen.AudioPage));

	TestTrue(TEXT("The left shoulder completes"), Press(EKeys::Gamepad_LeftShoulder));
	TestEqual(TEXT("The left shoulder went back to Video"), Screen.Tabs->GetActiveTabIndex(), 0);
	TestEqual(TEXT("The choices made on the way stayed: High"), Screen.Quality->GetSelectedIndex(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSettingsJourneyTouchTest,
	"DreamGUI.Journey.Settings.WithAFingerAPlayerTapsDragsAndTypesThroughTheScreenAndCancelsTheConfirmation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The same screen under a finger: a tap opens Audio, a finger drawn up the presets scrolls them (a list scrolls under a
 * finger, as STableViewBase pans on touch), taps open the quality and choose High, a finger carries the volume's handle to
 * three quarters, a tap on the name field and the characters a touch keyboard sends type the name, a tap on Apply asks
 * for confirmation, and a tap on its Cancel button answers it -- a touch screen has no Escape.
 */
bool FDreamSettingsJourneyTouchTest::RunTest(const FString& Parameters)
{
	using namespace DreamSettingsJourneyTestLocal;
	FAnswers Answers;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	const FSettingsScreen Screen = BuildScreen(*this, Rig, Answers, Listener.Get(), /*bInWithName*/ true);
	if (!Screen.IsReady(true))
	{
		return false;
	}
	FDreamDriverRef Driver = Rig.Driver();

	TestTrue(TEXT("Tapping the Audio tab completes"), Driver->Find(FDreamBy::Widget(Screen.TabAt(1)))->Tap());
	if (!TestEqual(TEXT("The tap opened Audio"), Screen.Tabs->GetActiveTabIndex(), 1))
	{
		return false;
	}

	TestTrue(TEXT("A finger drawn up the presets completes"), Driver->Find(FDreamBy::Widget(Screen.Presets))->TouchDragBy(FVector2D(0.0, -2.0 * PresetRowHeight)));
	if (!TestTrue(TEXT("The presets scrolled under the finger"), PumpUntil(Rig, [&Screen]() { return Screen.Presets->GetScrollOffset() > 0.0f; }, 1.0, TEXT("the presets scrolled"))))
	{
		return false;
	}

	TestTrue(TEXT("Tapping the quality completes"), Driver->Find(FDreamBy::Widget(Screen.Quality))->Tap());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The tap opened its list"), Screen.Quality->IsOpen())
		|| !TestTrue(TEXT("...with a row for High"), Listener->GeneratedItems.IsValidIndex(2) && Listener->GeneratedItems[2] != nullptr))
	{
		return false;
	}
	TestTrue(TEXT("Tapping High completes"), Driver->Find(FDreamBy::Widget(Listener->GeneratedItems[2].Get()))->Tap());
	if (!TestEqual(TEXT("The tap chose High"), Screen.Quality->GetSelectedIndex(), 2) || !TestFalse(TEXT("...and the list closed"), Screen.Quality->IsOpen()))
	{
		return false;
	}

	TOptional<FBox2D> Travel;
	TOptional<FVector2D> Grip;
	if (!TestTrue(TEXT("The volume's handle and travel are on screen"), VolumeTravel(Rig, Screen, Travel, Grip)))
	{
		return false;
	}
	const float OnePixel = static_cast<float>(1.0 / (Travel->Max.X - Travel->Min.X));
	const double TargetX = Travel->Min.X + 0.75 * (Travel->Max.X - Travel->Min.X);
	TestTrue(TEXT("A finger carrying the volume's handle completes"), Driver->Find(FDreamBy::Widget(Screen.Volume->HandleNode.Get()))->TouchDragBy(FVector2D(TargetX - Grip->X, 0.0)));
	if (!TestNearlyEqual(TEXT("The volume is three quarters"), Screen.Volume->GetValue(), 0.75f, OnePixel * 1.5f))
	{
		return false;
	}

	TestTrue(TEXT("Tapping the name field completes"), Driver->Find(FDreamBy::Widget(Screen.PlayerName))->Tap());
	TestTrue(TEXT("Typing and Enter complete"), Driver->Sequence().Type(TEXT("Kim")).Key(EKeys::Enter).Perform());
	if (!TestEqual(TEXT("The name field holds what was typed"), Screen.PlayerName->GetText(), FString(TEXT("Kim"))))
	{
		return false;
	}

	const FAudioState Chosen = FAudioState::Of(Screen);
	TestTrue(TEXT("Tapping Apply completes"), Driver->Find(FDreamBy::Widget(Screen.Apply))->Tap());
	Rig.PumpFrames(2);
	UDreamButton* Cancel = ConfirmationButton(Screen, TEXT("Cancel"));
	if (!TestEqual(TEXT("The tap on Apply asked for confirmation"), Screen.Modals->GetModalDepth(0), 1) || !TestNotNull(TEXT("...offering Cancel"), Cancel))
	{
		return false;
	}
	TestTrue(TEXT("Tapping Cancel completes"), Driver->Find(FDreamBy::Widget(Cancel))->Tap());
	TestEqual(TEXT("The tap on Cancel closed the confirmation"), Screen.Modals->GetModalDepth(0), 0);
	if (TestEqual(TEXT("...answering once"), Answers.Confirmations.Num(), 1))
	{
		TestEqual(TEXT("...with Cancel"), Answers.Confirmations[0], FName(TEXT("Cancel")));
	}
	TestTrue(FString::Printf(TEXT("The screen is as the player left it: %s"), *FAudioState::Of(Screen).Describe()), FAudioState::Of(Screen) == Chosen);
	TestTrue(TEXT("Tapping the Video tab completes"), Driver->Find(FDreamBy::Widget(Screen.TabAt(0)))->Tap());
	TestEqual(TEXT("...and the screen takes the tap again: the Video tab is open"), Screen.Tabs->GetActiveTabIndex(), 0);
	return true;
}

#endif
