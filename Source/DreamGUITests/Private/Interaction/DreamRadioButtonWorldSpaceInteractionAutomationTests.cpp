// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamRadioButton.h"
#include "Controls/DreamToggle.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamRadioButton in a group on a panel in the level.
 *
 * UMG's game UI makes a radio from CommonUI's toggleable buttons in a group that requires a selection
 * (UCommonButtonGroupBase, bSelectionRequired): choosing one lets the chosen one go. A button in a UWidgetComponent is hit
 * where the interaction's ray lands on it, a finger's tap is its press and release (SButton.cpp:354, :410), and the pad
 * walks to it and presses it with Accept (SButton::OnKeyDown, :296) where it stands in the level.
 */
namespace DreamRadioWorldTestLocal
{
	const FVector2D RadioSize(48.0, 48.0);

	/** A group that forbids "none" on the panel, and three radios in a row inside it, the first chosen. */
	struct FRadioGroup
	{
		UDreamWidget* Holder = nullptr;
		UUIToggleGroup* Group = nullptr;
		TArray<UDreamRadioButton*> Radios;
		TArray<TStrongObjectPtr<UDreamPressInteractionListener>> Listeners;

		bool IsReady() const { return Holder != nullptr && Group != nullptr && Radios.Num() == 3 && !Radios.Contains(nullptr); }

		int32 FocusedIndex(const FDreamDriverRig& InRig) const
		{
			for (int32 Index = 0; Index < Radios.Num(); ++Index)
			{
				if (DreamControlsConfigTest::FocusIsIn(InRig, Radios[Index]))
				{
					return Index;
				}
			}
			return INDEX_NONE;
		}

		/** Which radio is chosen, by index, as the radios say; INDEX_NONE for none, and -2 for more than one. */
		int32 CheckedIndex() const
		{
			int32 Found = INDEX_NONE;
			for (int32 Index = 0; Index < Radios.Num(); ++Index)
			{
				if (Radios[Index]->IsChecked())
				{
					Found = Found == INDEX_NONE ? Index : -2;
				}
			}
			return Found;
		}
	};

	/** The group at InPosition on InPanel (panel units, Y up), listened to after the first radio was chosen. */
	FRadioGroup MakeRadioGroup(FDreamDriverRig& InRig, UDreamWidget* InPanel, const FVector2D& InPosition)
	{
		FRadioGroup Made;
		Made.Holder = InRig.MakeWidget(TEXT("Quality"), InPanel, FVector2D(420.0, 90.0), InPosition);
		Made.Group = Made.Holder != nullptr ? Made.Holder->AddComponent<UUIToggleGroup>() : nullptr;
		if (Made.Group == nullptr)
		{
			return Made;
		}
		Made.Group->SetAllowNoneSelected(false);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamRadioButton* Radio = InRig.MakeControl<UDreamRadioButton>(FString::Printf(TEXT("Quality_Radio%d"), Index), Made.Holder,
				RadioSize, FVector2D(-140.0 + 140.0 * Index, 0.0));
			if (Radio != nullptr && Radio->BoxNode != nullptr)
			{
				Radio->SetToggleGroup(Made.Group);
			}
			Made.Radios.Add(Radio != nullptr && Radio->BoxNode != nullptr ? Radio : nullptr);
		}
		if (!Made.IsReady())
		{
			return Made;
		}
		Made.Radios[0]->SetIsChecked(true);
		for (UDreamRadioButton* Radio : Made.Radios)
		{
			TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
			Radio->OnCheckStateChanged.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleCheckStateChanged);
			Made.Listeners.Add(MoveTemp(Listener));
		}
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(2);
		return Made;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioWorldHitTest,
	"DreamGUI.RadioButton.OnAWorldPanelAClickAndATapChooseTheRadioOnlyInsideWhereItIsSeen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioWorldHitTest, "DreamGUI.RadioButton.OnAWorldPanelAClickAndATapChooseTheRadioOnlyInsideWhereItIsSeen", "[Pointer][Touch][World]")

/*
 * The group high on a panel three metres ahead. Clicks four pixels past the third radio's right and bottom edges, as the
 * camera sees it, choose nothing; a click three pixels inside its left edge, level with its centre, chooses it and the first
 * lets go. Taps past the second radio's edges choose nothing; a tap three pixels inside its bottom edge, level with its
 * centre, chooses it.
 *
 * Inside an edge's middle rather than a corner, as on the screen: the radio's box is round, and a corner three pixels in is
 * off the circle (UDreamRectBlock::bRaycastSupportCornerRadius).
 */
bool FDreamRadioWorldHitTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(Rig, Stage.Panel, FVector2D(-20.0, 80.0));
	if (!TestTrue(TEXT("Three radios in a group can be made on the panel"), Radios.IsReady()))
	{
		return false;
	}
	const TOptional<FBox2D> Third = SeenRect(Rig, Radios.Radios[2]->BoxNode.Get());
	const TOptional<FBox2D> Second = SeenRect(Rig, Radios.Radios[1]->BoxNode.Get());
	if (!TestTrue(TEXT("The camera sees the radios' boxes"), Third.IsSet() && Second.IsSet()))
	{
		return false;
	}
	TestTrue(FString::Printf(TEXT("A box is seen at its size in panel units (%s)"), *Describe(Third.GetValue())),
		IsSeenAtItsSize(Third.GetValue(), Radios.Radios[2]->BoxNode->GetSize()));
	const FBox2D ThirdOuter = Covering(Third.GetValue(), SeenRect(Rig, Radios.Radios[2]));
	const FBox2D SecondOuter = Covering(Second.GetValue(), SeenRect(Rig, Radios.Radios[1]));

	TestTrue(TEXT("Clicks just past the third radio's right and bottom edges complete"),
		ClickAt(Rig, PastRightEdge(ThirdOuter, 4.0)) && ClickAt(Rig, PastBottomEdge(ThirdOuter, 4.0)));
	TestEqual(TEXT("A click past the seen edge chooses nothing"), Radios.CheckedIndex(), 0);
	TestTrue(TEXT("A click just inside the third radio's left edge completes"),
		ClickAt(Rig, FVector2D(Third->Min.X + 3.0, Third->GetCenter().Y)));
	TestEqual(TEXT("It chooses the third radio"), Radios.CheckedIndex(), 2);
	TestEqual(TEXT("...the chosen one and the one let go each saying so once"),
		Radios.Listeners[2]->CheckStates.Num() + Radios.Listeners[0]->CheckStates.Num(), 2);

	TestTrue(TEXT("Taps just past the second radio's right and bottom edges complete"),
		TapAt(Rig, PastRightEdge(SecondOuter, 4.0)) && TapAt(Rig, PastBottomEdge(SecondOuter, 4.0)));
	TestEqual(TEXT("A tap past the seen edge chooses nothing"), Radios.CheckedIndex(), 2);
	TestTrue(TEXT("A tap just inside the second radio's bottom edge completes"),
		TapAt(Rig, FVector2D(Second->GetCenter().X, Second->Max.Y - 3.0)));
	TestEqual(TEXT("It chooses the second radio"), Radios.CheckedIndex(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRadioWorldPadTest,
	"DreamGUI.RadioButton.OnAWorldPanelTheDPadReachesTheNextRadioWithTheRingOnItAsSeenAndAcceptChoosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRadioWorldPadTest, "DreamGUI.RadioButton.OnAWorldPanelTheDPadReachesTheNextRadioWithTheRingOnItAsSeenAndAcceptChoosesIt", "[Nav][World]")

/*
 * The focus on the chosen first radio on the panel; D-pad right takes it to the second, the ring sits on the second where the
 * camera sees it, moving the focus chose nothing -- CommonUI selects on focus only when asked (bShouldSelectUponReceivingFocus)
 * -- and the pad's Accept chooses the second.
 */
bool FDreamRadioWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamRadioWorldTestLocal;
	using namespace DreamControlsConfigTest;
	using namespace DreamControlsWorldTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FRadioGroup Radios = MakeRadioGroup(Rig, Stage.Panel, FVector2D(-20.0, 80.0));
	if (!TestTrue(TEXT("Three radios in a group can be made on the panel"), Radios.IsReady())
		|| !TestTrue(TEXT("The focus goes onto the first radio"), PutFocusOn(Rig, Radios.Radios[0]->BoxNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestEqual(TEXT("The D-pad took the focus to the second radio"), Radios.FocusedIndex(Rig), 1);
	TestEqual(TEXT("Moving the focus chose nothing"), Radios.CheckedIndex(), 0);
	FString Where;
	const bool bRingSits = WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the second radio where the camera sees it (%s)"), *Where), bRingSits);

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept chose the focused radio"), Radios.CheckedIndex(), 1);
	TestEqual(TEXT("...the chosen one and the one let go each saying so once"),
		Radios.Listeners[1]->CheckStates.Num() + Radios.Listeners[0]->CheckStates.Num(), 2);
	return true;
}

#endif
