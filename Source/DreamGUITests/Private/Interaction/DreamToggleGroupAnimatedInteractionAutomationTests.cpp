// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamToggle.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UUIToggleGroup as shipped -- every member's tick fading between its colours as it is checked and let go -- chosen from with
 * a finger and with the pad.
 *
 * The UMG counterpart is CommonUI's button group with bSelectionRequired off (CommonUI/Public/Groups/CommonButtonGroupBase.h:141)
 * over toggleable buttons: a touch is a button's press and lift (SButton.cpp:354, :410), the pad's Accept presses the focused
 * button (SButton::OnKeyDown, :296), selecting one deselects the one that was selected (UCommonButtonGroupBase::
 * OnSelectionStateChangedBase), and a group that does not require a selection lets the chosen one go when it is pressed again
 * (UCommonButtonBase::HandleButtonClicked, CommonUI/Private/CommonButtonBase.cpp:1525-1550). CommonUI's buttons switch their
 * look at once; DreamGUI's toggles fade theirs (UUIToggle's ToggleDuration), and a fade is how the state arrives, not what
 * the state is: the group's choice and every announcement are there the moment the gesture ends, and each tick then settles
 * on the colour of the state its member is in.
 *
 * The rig is the default one -- its world has a game instance, so the tweens run -- and nothing about the toggles is changed
 * but their group.
 */
namespace DreamToggleGroupAnimatedTestLocal
{
	struct FToggleRow
	{
		UDreamWidget* Holder = nullptr;
		UUIToggleGroup* Group = nullptr;
		TArray<UDreamToggle*> Toggles;
		TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;

		bool IsReady() const { return Holder != nullptr && Group != nullptr && Toggles.Num() == 3 && !Toggles.Contains(nullptr); }

		int32 ChoiceIndex() const
		{
			const UUIToggle* Selected = Group->GetSelectedItem();
			for (int32 Index = 0; Index < Toggles.Num(); ++Index)
			{
				if (Selected != nullptr && Toggles[Index]->ToggleBehaviour.Get() == Selected)
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		int32 FocusedIndex(const FDreamDriverRig& InRig) const
		{
			for (int32 Index = 0; Index < Toggles.Num(); ++Index)
			{
				if (DreamControlsConfigTest::FocusIsIn(InRig, Toggles[Index]))
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		const UDreamVisual* TickOf(int32 InIndex) const
		{
			return Toggles[InIndex]->ToggleBehaviour != nullptr ? Toggles[InIndex]->ToggleBehaviour->GetToggleTransitionTarget() : nullptr;
		}

		FColor OnColour(int32 InIndex) const { return Toggles[InIndex]->ToggleBehaviour->GetOnColor(); }
		FColor OffColour(int32 InIndex) const { return Toggles[InIndex]->ToggleBehaviour->GetOffColor(); }
	};

	/** Three toggles in a row under one group that allows none, the first chosen before anyone listens. */
	FToggleRow MakeToggleRow(FDreamDriverRig& InRig)
	{
		FToggleRow Made;
		Made.Holder = InRig.MakeWidget(TEXT("Filters"), nullptr, FVector2D(420.0, 100.0), FVector2D(0.0, 60.0));
		Made.Group = Made.Holder != nullptr ? Made.Holder->AddComponent<UUIToggleGroup>() : nullptr;
		if (Made.Group == nullptr)
		{
			return Made;
		}
		Made.Group->SetAllowNoneSelected(true);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamToggle* Toggle = InRig.MakeControl<UDreamToggle>(FString::Printf(TEXT("Filters_Toggle%d"), Index), Made.Holder,
				FVector2D(48.0, 48.0), FVector2D(-140.0 + 140.0 * Index, 0.0));
			const bool bReady = Toggle != nullptr && Toggle->ToggleBehaviour != nullptr && Toggle->BoxNode != nullptr;
			if (bReady)
			{
				Toggle->ToggleBehaviour->SetToggleGroup(Made.Group);
			}
			Made.Toggles.Add(bReady ? Toggle : nullptr);
		}
		if (!Made.IsReady())
		{
			return Made;
		}
		Made.Toggles[0]->SetIsChecked(true);
		for (UDreamToggle* Toggle : Made.Toggles)
		{
			TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
			Toggle->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
			Made.Listeners.Add(MoveTemp(Listener));
		}
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(1);
		return Made;
	}

	/** Frames until InTick wears InColour, pumping one at a time for at most InMaxFrames; INDEX_NONE when it never did. */
	int32 FramesUntilColour(FDreamDriverRig& InRig, const UDreamVisual* InTick, const FColor& InColour, int32 InMaxFrames)
	{
		for (int32 Frame = 0; Frame <= InMaxFrames; ++Frame)
		{
			if (InTick->GetColor() == InColour)
			{
				return Frame;
			}
			InRig.PumpFrames(1);
		}
		return INDEX_NONE;
	}

	/** Two seconds of frames: a stop far past any tick's fade, not a guess at how long one takes. */
	int32 Ceiling(const FDreamDriverRig& InRig)
	{
		return FMath::CeilToInt(2.0f / InRig.Context().FrameSeconds);
	}

	/** Whether every member's tick wears the colour of its member's state, waiting out the fades. */
	bool TicksSettleOnTheirStates(FDreamDriverRig& InRig, const FToggleRow& InRow)
	{
		bool bAllSettled = true;
		for (int32 Index = 0; Index < InRow.Toggles.Num(); ++Index)
		{
			const FColor Wanted = InRow.Toggles[Index]->IsChecked() ? InRow.OnColour(Index) : InRow.OffColour(Index);
			bAllSettled &= InRow.TickOf(Index) != nullptr && FramesUntilColour(InRig, InRow.TickOf(Index), Wanted, Ceiling(InRig)) != INDEX_NONE;
		}
		return bAllSettled;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupAnimatedTapTest,
	"DreamGUI.ToggleGroup.ATapOnAnotherMemberMovesTheChoiceAtOnceAndTheTicksFadeOverToIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupAnimatedTapTest, "DreamGUI.ToggleGroup.ATapOnAnotherMemberMovesTheChoiceAtOnceAndTheTicksFadeOverToIt", "[Touch][Animated]")

/*
 * The first member chosen and its tick settled. A tap on the second makes it the group's choice the moment the finger lifts:
 * it says so once, the first lets go and says so once, the third says nothing. The second's tick then fades in over several
 * frames to the checked colour while the first's fades out to the unchecked one. A second tap on the second -- the chosen one,
 * in a group that allows none -- lets it go: nothing is chosen, and its tick fades back out.
 */
bool FDreamToggleGroupAnimatedTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupAnimatedTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsConfigTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(Rig);
	if (!TestTrue(TEXT("Three toggles in a group can be made"), Row.IsReady())
		|| !TestEqual(TEXT("The first is the group's choice to begin with"), Row.ChoiceIndex(), 0))
	{
		return false;
	}
	if (!TestTrue(TEXT("Every member has a tick, and its checked and unchecked colours differ"),
			Row.TickOf(1) != nullptr && Row.TickOf(0) != nullptr && Row.OnColour(1) != Row.OffColour(1) && Row.OnColour(0) != Row.OffColour(0))
		|| !TestTrue(TEXT("Before anything is touched, every tick wears its member's state"), TicksSettleOnTheirStates(Rig, Row)))
	{
		return false;
	}

	TestTrue(TEXT("Tapping the second member completes"), Rig.Driver()->Find(FDreamBy::Widget(Row.Toggles[1]))->Tap());
	TestEqual(TEXT("The tap made the second the group's choice at once"), Row.ChoiceIndex(), 1);
	TestTrue(TEXT("...checked"), Row.Toggles[1]->IsChecked());
	TestFalse(TEXT("...and the first let go"), Row.Toggles[0]->IsChecked());
	if (TestEqual(TEXT("The second announced one change"), Row.Listeners[1]->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("...to checked"), Row.Listeners[1]->CheckStates[0], EDreamCheckState::Checked);
	}
	if (TestEqual(TEXT("The first announced one change"), Row.Listeners[0]->CheckStates.Num(), 1))
	{
		TestEqual(TEXT("...to unchecked"), Row.Listeners[0]->CheckStates[0], EDreamCheckState::Unchecked);
	}
	TestEqual(TEXT("The third said nothing"), Row.Listeners[2]->CheckStates.Num(), 0);

	const int32 FadeIn = FramesUntilColour(Rig, Row.TickOf(1), Row.OnColour(1), Ceiling(Rig));
	TestTrue(FString::Printf(TEXT("The second's tick faded in to the checked colour over several frames (%d)"), FadeIn), FadeIn >= 4);
	TestTrue(TEXT("...and the first's faded out to the unchecked colour"),
		FramesUntilColour(Rig, Row.TickOf(0), Row.OffColour(0), Ceiling(Rig)) != INDEX_NONE);

	TestTrue(TEXT("Tapping the chosen second member again completes"), Rig.Driver()->Find(FDreamBy::Widget(Row.Toggles[1]))->Tap());
	TestEqual(TEXT("With none allowed, the chosen member lets go and nothing is chosen"), Row.ChoiceIndex(), static_cast<int32>(INDEX_NONE));
	TestFalse(TEXT("...the second unchecked"), Row.Toggles[1]->IsChecked());
	TestEqual(TEXT("...saying so once more"), Row.Listeners[1]->CheckStates.Num(), 2);
	TestEqual(TEXT("...and nobody else said anything"), Row.Listeners[0]->CheckStates.Num() + Row.Listeners[2]->CheckStates.Num(), 1);
	TestTrue(TEXT("The second's tick fades back out to the unchecked colour"),
		FramesUntilColour(Rig, Row.TickOf(1), Row.OffColour(1), Ceiling(Rig)) != INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamToggleGroupAnimatedPadTest,
	"DreamGUI.ToggleGroup.TheDPadReachesTheNextMemberWithTheRingOnItAndAcceptMovesTheChoiceWhileTheTicksFadeOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamToggleGroupAnimatedPadTest, "DreamGUI.ToggleGroup.TheDPadReachesTheNextMemberWithTheRingOnItAndAcceptMovesTheChoiceWhileTheTicksFadeOver", "[Nav][Animated]")

/*
 * The first member chosen and focused. D-pad right takes the focus to the second; the ring flies over and fades in on it;
 * moving the focus left the choice alone and nobody said anything. The pad's Accept makes the second the choice at once, the
 * first letting go, each saying so once, and the ticks then fade over to the new choice. D-pad left and Accept give the choice
 * back to the first the same way.
 */
bool FDreamToggleGroupAnimatedPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamToggleGroupAnimatedTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsConfigTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FToggleRow Row = MakeToggleRow(Rig);
	if (!TestTrue(TEXT("Three toggles in a group can be made"), Row.IsReady())
		|| !TestTrue(TEXT("Before anything is pressed, every tick wears its member's state"), TicksSettleOnTheirStates(Rig, Row))
		|| !TestTrue(TEXT("The focus goes onto the first member"), PutFocusOn(Rig, Row.Toggles[0]->BoxNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("The D-pad took the focus to the second member"), Row.FocusedIndex(Rig), 1);
	TestEqual(TEXT("Moving the focus left the choice alone"), Row.ChoiceIndex(), 0);
	TestEqual(TEXT("...and nobody said anything"),
		Row.Listeners[0]->CheckStates.Num() + Row.Listeners[1]->CheckStates.Num() + Row.Listeners[2]->CheckStates.Num(), 0);
	FString Where;
	const bool bRingSits = WaitForRingOnFocus(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring came to sit on the second member where it is drawn (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept made the focused member the choice at once"), Row.ChoiceIndex(), 1);
	TestFalse(TEXT("...and the first let go"), Row.Toggles[0]->IsChecked());
	TestEqual(TEXT("The second said so once"), Row.Listeners[1]->CheckStates.Num(), 1);
	TestEqual(TEXT("...and so did the first"), Row.Listeners[0]->CheckStates.Num(), 1);
	const int32 FadeIn = FramesUntilColour(Rig, Row.TickOf(1), Row.OnColour(1), Ceiling(Rig));
	TestTrue(FString::Printf(TEXT("The second's tick faded in to the checked colour over several frames (%d)"), FadeIn), FadeIn >= 4);
	TestTrue(TEXT("...and the first's faded out to the unchecked colour"),
		FramesUntilColour(Rig, Row.TickOf(0), Row.OffColour(0), Ceiling(Rig)) != INDEX_NONE);

	TestTrue(TEXT("D-pad left completes"), PressKey(Rig, EKeys::Gamepad_DPad_Left));
	TestEqual(TEXT("D-pad left took the focus back to the first member"), Row.FocusedIndex(Rig), 0);
	TestTrue(TEXT("The pad's Accept completes again"), PadConfirm(Rig));
	TestEqual(TEXT("Accept gave the choice back to the first"), Row.ChoiceIndex(), 0);
	TestFalse(TEXT("...and the second let go"), Row.Toggles[1]->IsChecked());
	TestTrue(TEXT("Every tick settles on its member's state again"), TicksSettleOnTheirStates(Rig, Row));
	return true;
}

#endif
