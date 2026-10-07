// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISelectable.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * THE D-PAD ACROSS A SCREEN OF BUTTONS, AS SLATE WALKS ONE.
 *
 * FSlateApplication::AttemptNavigation asks the focused widget for its navigation (FNavigationReply) and, for an Escape
 * reply, the hit-test grid for the nearest focusable widget that way (FHittestGrid::FindNextFocusableWidget,
 * SlateCore/Private/Input/HittestGrid.cpp): a disabled one is passed over (FindFocusableWidget skips a widget that is not
 * enabled), an Explicit rule names its target outright, a Stop rule keeps the focus where it is, and at the edge of a
 * widget whose boundary rule is Wrap the walk comes back on at the far side, under Escape it goes on out, under Stop it
 * stays. CommonUI's buttons walk the same grid.
 *
 * Every press here is the D-pad through the rig's key road (DreamUIKeyRouting::RouteKey), from a focus put on a button
 * first, and what is read is which button player 0's focus is on after it.
 */
namespace DreamPadNavigationInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(160.0, 60.0);

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FString& InName, UDreamWidget* InParent, const FVector2D& InPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, ButtonSize, InPosition);
		InTest.TestTrue(*FString::Printf(TEXT("A button '%s' with a face and a behaviour can be made"), *InName),
			Button != nullptr && Button->FaceNode != nullptr && Button->ButtonBehaviour != nullptr);
		return Button != nullptr && Button->FaceNode != nullptr && Button->ButtonBehaviour != nullptr ? Button : nullptr;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	/** Which of InButtons player 0's focus is on, by name; "(none)" when it is on none of them. */
	FString FocusedName(const FDreamDriverRig& InRig, const TArray<UDreamButton*>& InButtons)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		const UDreamWidget* Focus = Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
		for (const UDreamButton* Button : InButtons)
		{
			if (IsPartOf(Focus, Button))
			{
				return Button->GetDisplayName();
			}
		}
		return FString(TEXT("(none)"));
	}

	bool FocusOn(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamButton* InButton)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return InTest.TestTrue(*FString::Printf(TEXT("Player 0's focus can be put on '%s'"), *GetNameSafe(InButton)),
			Services != nullptr && InButton != nullptr && Services->FocusForNavigation(InButton->FaceNode, 0));
	}

	/** Each of InKeys pressed in turn on the D-pad, and where the focus was after each, as names. */
	FString Walk(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TArray<UDreamButton*>& InButtons, const TArray<FKey>& InKeys)
	{
		TArray<FString> Names;
		for (const FKey& Key : InKeys)
		{
			InTest.TestTrue(*FString::Printf(TEXT("%s completes"), *Key.ToString()), InRig.Driver()->Sequence().Key(Key).WaitFrames(1).Perform());
			Names.Add(FocusedName(InRig, InButtons));
		}
		return FString::Join(Names, TEXT(", "));
	}

	/**
	 * A column of three buttons inside an area that restricts navigation to itself with InRule at its edge, and one button
	 * below the area, outside it. OutButtons is Top, Mid, Bottom, Outside.
	 */
	bool MakeRestrictedColumn(FAutomationTestBase& InTest, FDreamDriverRig& InRig, EDreamUINavigationBoundaryRule InRule, TArray<UDreamButton*>& OutButtons)
	{
		UDreamWidget* Area = InRig.MakeWidget(TEXT("Area"), nullptr, FVector2D(240.0, 360.0), FVector2D(0.0, 80.0));
		if (!InTest.TestNotNull(TEXT("An area for the column"), Area))
		{
			return false;
		}
		Area->SetRestrictNavigationArea(true);
		Area->SetNavigationBoundaryRule(InRule);
		OutButtons = {
			PlaceButton(InTest, InRig, TEXT("Top"), Area, FVector2D(0.0, 120.0)),
			PlaceButton(InTest, InRig, TEXT("Mid"), Area, FVector2D(0.0, 0.0)),
			PlaceButton(InTest, InRig, TEXT("Bottom"), Area, FVector2D(0.0, -120.0)),
			PlaceButton(InTest, InRig, TEXT("Outside"), nullptr, FVector2D(0.0, -260.0)),
		};
		InRig.PumpFrames(1);
		return !OutButtons.Contains(nullptr);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPadNavigationGridTest,
	"DreamGUI.Button.TheDPadWalksAThreeByThreeGridOfButtonsAlongItsRowsAndColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPadNavigationGridTest, "DreamGUI.Button.TheDPadWalksAThreeByThreeGridOfButtonsAlongItsRowsAndColumns", "[Nav][Animated]")

/*
 * Nine buttons in three rows of three. From the top-left one the D-pad goes right along the top row, down the right-hand
 * column, left along the bottom row, and so round to the start: every press reaches the next button that way, never one
 * diagonal to it and never one two cells off.
 */
bool FDreamPadNavigationGridTest::RunTest(const FString& Parameters)
{
	using namespace DreamPadNavigationInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TArray<UDreamButton*> Buttons;
	for (int32 Row = 0; Row < 3; ++Row)
	{
		for (int32 Column = 0; Column < 3; ++Column)
		{
			Buttons.Add(PlaceButton(*this, Rig, FString::Printf(TEXT("R%dC%d"), Row, Column), nullptr,
				FVector2D(-240.0 + Column * 240.0, 140.0 - Row * 140.0)));
		}
	}
	if (Buttons.Contains(nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!FocusOn(*this, Rig, Buttons[0]))
	{
		return false;
	}
	TestEqual(TEXT("The D-pad walks round the grid's edge and through its middle, one cell a press"),
		Walk(*this, Rig, Buttons, {
			EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down,
			EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Left, EKeys::Gamepad_DPad_Up }),
		FString(TEXT("R0C1, R0C2, R1C2, R2C2, R2C1, R1C1, R1C0, R0C0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPadNavigationSkipsDisabledTest,
	"DreamGUI.Button.TheDPadPassesOverADisabledButtonToTheNextOneTheSameWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPadNavigationSkipsDisabledTest, "DreamGUI.Button.TheDPadPassesOverADisabledButtonToTheNextOneTheSameWay", "[Nav][Disabled]")

/*
 * Three buttons in a row, the middle one disabled. FindFocusableWidget passes over a widget that is not enabled, so the
 * D-pad goes from the first straight to the third and back, and the focus is never on the disabled one. Enabled again, it
 * is the next stop once more.
 */
bool FDreamPadNavigationSkipsDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamPadNavigationInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), nullptr, FVector2D(-300.0, 0.0));
	UDreamButton* Middle = PlaceButton(*this, Rig, TEXT("Middle"), nullptr, FVector2D(0.0, 0.0));
	UDreamButton* Last = PlaceButton(*this, Rig, TEXT("Last"), nullptr, FVector2D(300.0, 0.0));
	if (First == nullptr || Middle == nullptr || Last == nullptr)
	{
		return false;
	}
	Middle->SetIsEnabled(false);
	Rig.PumpFrames(1);
	const TArray<UDreamButton*> Buttons = { First, Middle, Last };
	if (!FocusOn(*this, Rig, First))
	{
		return false;
	}
	TestEqual(TEXT("Right goes past the disabled button, and Left back past it"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Right, EKeys::Gamepad_DPad_Left }), FString(TEXT("Last, First")));

	Middle->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestEqual(TEXT("Enabled again, the middle button is the next stop"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Right }), FString(TEXT("Middle")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPadNavigationExplicitRuleTest,
	"DreamGUI.Button.AnExplicitNavigationRuleSendsTheDPadToItsTargetAndANoneRuleKeepsItWhereItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPadNavigationExplicitRuleTest, "DreamGUI.Button.AnExplicitNavigationRuleSendsTheDPadToItsTargetAndANoneRuleKeepsItWhereItIs", "[Nav][Animated]")

/*
 * UWidget::SetNavigationRuleExplicit: the first button's Right names the third, so the D-pad goes there over the nearer
 * second -- FHittestGrid answers an Explicit rule with its target and no search. The third button's Left is set to go
 * nowhere (Slate's Stop), so Left from it keeps the focus on it.
 */
bool FDreamPadNavigationExplicitRuleTest::RunTest(const FString& Parameters)
{
	using namespace DreamPadNavigationInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), nullptr, FVector2D(-300.0, 0.0));
	UDreamButton* Second = PlaceButton(*this, Rig, TEXT("Second"), nullptr, FVector2D(0.0, 0.0));
	UDreamButton* Third = PlaceButton(*this, Rig, TEXT("Third"), nullptr, FVector2D(300.0, 0.0));
	if (First == nullptr || Second == nullptr || Third == nullptr)
	{
		return false;
	}
	First->ButtonBehaviour->SetNavigationRight(EUISelectableNavigationMode::Explicit);
	First->ButtonBehaviour->SetNavigationRightExplicit(Third->ButtonBehaviour);
	Third->ButtonBehaviour->SetNavigationLeft(EUISelectableNavigationMode::None);
	Rig.PumpFrames(1);
	const TArray<UDreamButton*> Buttons = { First, Second, Third };
	if (!FocusOn(*this, Rig, First))
	{
		return false;
	}
	TestEqual(TEXT("Right from the first button goes where its rule says, past the nearer button"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Right }), FString(TEXT("Third")));
	TestEqual(TEXT("Left from the third, whose rule goes nowhere, stays on it"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Left }), FString(TEXT("Third")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPadNavigationWrapAreaTest,
	"DreamGUI.Navigation.Scope.TheDPadOffTheEndOfAWrappingAreaComesBackOnAtItsFirstButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPadNavigationWrapAreaTest, "DreamGUI.Navigation.Scope.TheDPadOffTheEndOfAWrappingAreaComesBackOnAtItsFirstButton", "[Nav][Animated]")

/*
 * A boundary rule of Wrap (FHittestGrid::FindNextFocusableWidgetDefault): at the area's last button the D-pad comes back on
 * at the far side of the area, its first, and never reaches the button outside it.
 */
bool FDreamPadNavigationWrapAreaTest::RunTest(const FString& Parameters)
{
	using namespace DreamPadNavigationInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TArray<UDreamButton*> Buttons;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable())
		|| !MakeRestrictedColumn(*this, Rig, EDreamUINavigationBoundaryRule::Wrap, Buttons)
		|| !FocusOn(*this, Rig, Buttons[1]))
	{
		return false;
	}
	TestEqual(TEXT("Down to the last button, then off the end and round to the first"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down }), FString(TEXT("Bottom, Top")));
	TestEqual(TEXT("...and Up off the first comes round to the last"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Up }), FString(TEXT("Bottom")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPadNavigationEscapeAreaTest,
	"DreamGUI.Navigation.Scope.TheDPadOffTheEndOfAnEscapingAreaGoesOnToTheButtonOutsideIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPadNavigationEscapeAreaTest, "DreamGUI.Navigation.Scope.TheDPadOffTheEndOfAnEscapingAreaGoesOnToTheButtonOutsideIt", "[Nav][Animated]")

/*
 * A boundary rule of Escape, Slate's default: with no button left that way inside the area, the D-pad goes on out of it
 * to the nearest one beyond -- and while the area still has a button that way, that one wins.
 */
bool FDreamPadNavigationEscapeAreaTest::RunTest(const FString& Parameters)
{
	using namespace DreamPadNavigationInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TArray<UDreamButton*> Buttons;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable())
		|| !MakeRestrictedColumn(*this, Rig, EDreamUINavigationBoundaryRule::Escape, Buttons)
		|| !FocusOn(*this, Rig, Buttons[1]))
	{
		return false;
	}
	TestEqual(TEXT("Down to the last button inside, then on out of the area"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down }), FString(TEXT("Bottom, Outside")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPadNavigationStopAreaTest,
	"DreamGUI.Navigation.Scope.TheDPadOffTheEndOfAStoppingAreaKeepsTheFocusOnItsLastButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPadNavigationStopAreaTest, "DreamGUI.Navigation.Scope.TheDPadOffTheEndOfAStoppingAreaKeepsTheFocusOnItsLastButton", "[Nav][Animated]")

/*
 * A boundary rule of Stop: at the area's last button the D-pad goes nowhere -- not round, not out to the button below --
 * and the focus stays on that last button however often it is pressed.
 */
bool FDreamPadNavigationStopAreaTest::RunTest(const FString& Parameters)
{
	using namespace DreamPadNavigationInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TArray<UDreamButton*> Buttons;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable())
		|| !MakeRestrictedColumn(*this, Rig, EDreamUINavigationBoundaryRule::Stop, Buttons)
		|| !FocusOn(*this, Rig, Buttons[1]))
	{
		return false;
	}
	TestEqual(TEXT("Down to the last button, then twice more against the edge"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down }),
		FString(TEXT("Bottom, Bottom, Bottom")));
	return true;
}

#endif
