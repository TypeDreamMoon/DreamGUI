// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Engine/DataTable.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIActionTrigger.h"
#include "Interaction/DreamUIInputAction.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * A WRAPPING AREA, BACK THROUGH A STACK OF SCREENS, AND A BUTTON'S ACTION KEY -- ON A PANEL IN THE LEVEL.
 *
 * Navigation on a panel is DreamGUI's directional scan among the world-space widgets, made in the panel's own right and up
 * directions (DreamUINavigationScan::ScanDirectional); its areas, screens and bindings are the same objects as on the screen,
 * so what each promises there it promises on a panel:
 *  - the D-pad off the end of an area whose boundary rule is Wrap comes back on at its far side and never leaves it -- the
 *    walk Slate makes in FHittestGrid::FindNextFocusableWidget, confined as CommonUI's activatable confines it -- and the
 *    focus ring is drawn around where the focus is, on the panel's own canvas, as Slate draws a focused widget's focus brush on
 *    its own geometry;
 *  - Back goes to the screen in front, and closing it gives the focus back to where it was on the screen under it
 *    (UCommonActivatableWidget::NativeOnHandleBackAction deactivates it, and the activatable under it restores its desired
 *    focus);
 *  - a button that is also what an action does is clicked by the action's key, wherever the button is drawn (CommonUI's
 *    TriggeringInputAction: the key finds its binding with no position, and the binding clicks the button,
 *    UCommonButtonBase::HandleTriggeringActionCommited).
 */
namespace DreamNavigationWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	const FVector2D ButtonSize(160.0, 40.0);

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FString& InName, UDreamWidget* InParent, const FVector2D& InPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, ButtonSize, InPosition);
		const bool bMade = Button != nullptr && Button->FaceNode != nullptr && Button->ButtonBehaviour != nullptr;
		InTest.TestTrue(*FString::Printf(TEXT("A button '%s' with a face and a behaviour can be made"), *InName), bMade);
		return bMade ? Button : nullptr;
	}

	/** Which of InButtons player 0's focus is on, by name; "(none)" when it is on none of them. */
	FString FocusedName(const FDreamDriverRig& InRig, const TArray<UDreamButton*>& InButtons)
	{
		for (const UDreamButton* Button : InButtons)
		{
			if (FocusIsIn(InRig, Button))
			{
				return Button->GetDisplayName();
			}
		}
		return FString(TEXT("(none)"));
	}

	/** Each of InKeys pressed in turn on the D-pad, and where the focus was after each, as names. */
	FString Walk(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TArray<UDreamButton*>& InButtons, const TArray<FKey>& InKeys)
	{
		TArray<FString> Names;
		for (const FKey& Key : InKeys)
		{
			InTest.TestTrue(*FString::Printf(TEXT("%s completes"), *Key.ToString()), PressKey(InRig, Key));
			Names.Add(FocusedName(InRig, InButtons));
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** A widget on InParent with one button on it and a navigation scope that waits to be opened, as a page or a dialog has. */
	UDreamUINavigationScope* MakeScreen(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InParent, const TCHAR* InName,
		const FVector2D& InSize, const FVector2D& InButtonPosition, UDreamButton*& OutButton)
	{
		UDreamWidget* Screen = InRig.MakeWidget(InName, InParent, InSize);
		OutButton = Screen != nullptr ? InRig.MakeControl<UDreamButton>(FString::Printf(TEXT("%sButton"), InName), Screen, ButtonSize, InButtonPosition) : nullptr;
		UDreamUINavigationScope* Scope = Screen != nullptr ? Screen->AddComponent<UDreamUINavigationScope>() : nullptr;
		if (!InTest.TestTrue(*FString::Printf(TEXT("A screen '%s' with a button and a scope can be made"), InName), OutButton != nullptr && Scope != nullptr))
		{
			return nullptr;
		}
		// A widget in play enables what is added to it at once, and a scope enabled opens itself: taken off again, and told
		// to wait from then on, so each screen opens when the test opens it.
		Scope->SetActivateWhenEnabled(false);
		Scope->DeactivateScope();
		return Scope;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldWrapAreaTest,
	"DreamGUI.Navigation.Scope.OnAWorldPanelTheDPadOffTheEndOfAWrappingAreaComesBackOnAtItsFirstButtonWithTheRingOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldWrapAreaTest, "DreamGUI.Navigation.Scope.OnAWorldPanelTheDPadOffTheEndOfAWrappingAreaComesBackOnAtItsFirstButtonWithTheRingOnIt", "[Nav][World]")

/*
 * On a panel, a column of three buttons inside an area that keeps the D-pad to itself and wraps at its edges, and a button
 * outside the area, below and to the right of it -- what the D-pad down from the column's bottom would reach were the area
 * not there. From the middle: down to the bottom, down off the end and round to the top, up off the top and round to the
 * bottom; the button outside is never reached. The ring is drawn around the button the walk ends on, where the camera sees it.
 */
bool FDreamWorldWrapAreaTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationWorldTestLocal;
	const FFocusDrawnByKeys FocusDrawnByKeys;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamWidget* Area = Stage.IsReady() ? Rig.MakeWidget(TEXT("Area"), Stage.Panel, FVector2D(200.0, 280.0), FVector2D(-80.0, 10.0)) : nullptr;
	if (!TestNotNull(TEXT("An area for the column on the panel"), Area))
	{
		return false;
	}
	Area->SetRestrictNavigationArea(true);
	Area->SetNavigationBoundaryRule(EDreamUINavigationBoundaryRule::Wrap);
	TArray<UDreamButton*> Buttons = {
		PlaceButton(*this, Rig, TEXT("Top"), Area, FVector2D(0.0, 90.0)),
		PlaceButton(*this, Rig, TEXT("Mid"), Area, FVector2D(0.0, 0.0)),
		PlaceButton(*this, Rig, TEXT("Bottom"), Area, FVector2D(0.0, -90.0)),
		PlaceButton(*this, Rig, TEXT("Outside"), Stage.Panel, FVector2D(150.0, -135.0)),
	};
	if (Buttons.Contains(nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("Player 0's focus can be put on the middle button"), PutFocusOn(Rig, Buttons[1]->FaceNode)))
	{
		return false;
	}

	TestEqual(TEXT("Down, off the end round to the first, and up off it round to the last -- never out of the area"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Up }),
		FString(TEXT("Bottom, Top, Bottom")));

	FString RingWhere;
	TestTrue(TEXT("The focus ring is drawn on the panel around the bottom button, where the camera sees it"), WaitForRingOnFocus(Rig, RingWhere));
	AddInfo(RingWhere);
	TestTrue(TEXT("...and the ring marks the bottom button"), FocusIsIn(Rig, Buttons[2]));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldBackStackTest,
	"DreamGUI.Navigation.Back.OnAWorldPanelBackClosesTheDialogInFrontAndGivesTheFocusBackToThePagesButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldBackStackTest, "DreamGUI.Navigation.Back.OnAWorldPanelBackClosesTheDialogInFrontAndGivesTheFocusBackToThePagesButton", "[Nav][World]")

/*
 * A page and a dialog over it, both on a panel, each with a button. Opened in turn, the page takes the focus onto its button
 * and the dialog onto its own. Back closes the dialog only: the page is in front again and the focus is back on the page's
 * button on the panel. The next Back closes the page.
 */
bool FDreamWorldBackStackTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	UDreamUINavigationStack* Stack = Rig.IsUsable() ? UDreamUINavigationStack::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig came up with a navigation stack"), Stack))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamButton* PageButton = nullptr;
	UDreamButton* DialogButton = nullptr;
	UDreamUINavigationScope* Page = MakeScreen(*this, Rig, Stage.Panel, TEXT("Page"), FVector2D(520.0, 300.0), FVector2D(-160.0, 100.0), PageButton);
	UDreamUINavigationScope* Dialog = MakeScreen(*this, Rig, Stage.Panel, TEXT("Dialog"), FVector2D(300.0, 160.0), FVector2D(0.0, -20.0), DialogButton);
	if (Page == nullptr || Dialog == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(2);

	Page->ActivateScope();
	Rig.PumpFrames(1);
	TestTrue(TEXT("The page opened with the focus on its button on the panel"), FocusIsIn(Rig, PageButton));
	Dialog->ActivateScope();
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The dialog is in front of the page"), Stack->GetActiveScope(0) == Dialog))
	{
		return false;
	}
	TestTrue(TEXT("The dialog took the focus onto its own button"), FocusIsIn(Rig, DialogButton));

	TestTrue(TEXT("Back completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("Back closed the dialog"), Dialog->IsScopeActive());
	TestTrue(TEXT("...and only the dialog: the page is open and in front"), Page->IsScopeActive() && Stack->GetActiveScope(0) == Page);
	TestTrue(TEXT("The focus is back on the page's button on the panel"), FocusIsIn(Rig, PageButton));

	TestTrue(TEXT("Back again completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("The second Back closed the page"), Page->IsScopeActive());
	TestNull(TEXT("...and nothing is open now"), Stack->GetActiveScope(0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldActionKeyTest,
	"DreamGUI.Navigation.Actions.TheKeyOfAnActionOnAButtonOnAWorldPanelClicksIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldActionKeyTest, "DreamGUI.Navigation.Actions.TheKeyOfAnActionOnAButtonOnAWorldPanelClicksIt", "[Nav][World]")

/*
 * A button on a panel that is also what the Confirm action does, its Confirm keys X and the pad's left face button. Each key
 * clicks it once, though nothing about the key says where the button is drawn; a key the action does not name clicks nothing.
 */
bool FDreamWorldActionKeyTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationWorldTestLocal;
	// The clicks the key makes, counted where they land, as the router's own tests count them. Declared before the rig.
	int32 Clicks = 0;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* Button = Stage.IsReady() ? PlaceButton(*this, Rig, TEXT("Accept"), Stage.Panel, FVector2D(120.0, -60.0)) : nullptr;
	UUIEventTrigger* ClickWatcher = Button != nullptr ? Button->FaceNode->AddComponent<UUIEventTrigger>() : nullptr;
	if (!TestNotNull(TEXT("A button on the panel whose face carries a click watcher came up"), ClickWatcher))
	{
		return false;
	}
	ClickWatcher->GetOnPointerClickEvent().AddLambda([&Clicks](UDreamPointerEventData*) { ++Clicks; });

	TStrongObjectPtr<UDataTable> Table(NewObject<UDataTable>(GetTransientPackage()));
	Table->RowStruct = FDreamUIInputActionData::StaticStruct();
	FDreamUIInputActionData ConfirmRow;
	ConfirmRow.DisplayName = FText::FromString(TEXT("Confirm"));
	ConfirmRow.KeyboardKey = EKeys::X;
	ConfirmRow.GamepadKey = EKeys::Gamepad_FaceButton_Left;
	Table->AddRow(TEXT("Confirm"), ConfirmRow);
	FDataTableRowHandle Row;
	Row.DataTable = Table.Get();
	Row.RowName = TEXT("Confirm");
	// Beside the button's face, where its click is taken.
	UDreamUIActionTrigger* Trigger = Button->FaceNode->AddComponent<UDreamUIActionTrigger>();
	if (!TestNotNull(TEXT("The button's face carries an action trigger"), Trigger))
	{
		return false;
	}
	Trigger->SetAction(Row);
	// A trigger binds as its widget comes alive, and this face was alive before it had an action -- so it is woken again,
	// as DreamGUI.Navigation.ActionBar.TheTriggerAndTheBarAnswerToThePlayerWhoOwnsThemUnlessToldOtherwise binds its own.
	// Whether naming an action on a live trigger should bind it at once, as CommonUI's SetTriggeringInputAction does, is
	// not this test's question.
	Button->FaceNode->SetWidgetActive(false);
	Button->FaceNode->SetWidgetActive(true);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The trigger bound its action"), Trigger->IsActionBound()))
	{
		return false;
	}

	TestTrue(TEXT("Pressing X completes"), PressKey(Rig, EKeys::X));
	TestEqual(TEXT("X clicked the button on the panel once"), Clicks, 1);
	TestTrue(TEXT("Pressing the pad's left face button completes"), PressKey(Rig, EKeys::Gamepad_FaceButton_Left));
	TestEqual(TEXT("...and the pad's key once more"), Clicks, 2);
	TestTrue(TEXT("Pressing Y completes"), PressKey(Rig, EKeys::Y));
	TestEqual(TEXT("A key the action does not name clicked nothing"), Clicks, 2);
	return true;
}

#endif
