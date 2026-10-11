// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamWidgetNavigation.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/DreamUIActionTrigger.h"
#include "Interaction/DreamUIInputAction.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/UIEventTrigger.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A BUTTON'S ACTION KEY, A WRAPPING AREA AND BACK, WITH SOMETHING IN THEM DISABLED -- AND THE AREA ON A SCALED CANVAS.
 *
 * The action key: CommonUI's TriggeringInputAction on a button -- here UDreamUIActionTrigger beside a button -- is a binding
 * the router offers a key to only while the widget it is bound to can take input: FActionRouterBindingCollection::
 * IsWidgetReachableForInput walks the widget's parents and CanWidgetReceiveInput answers no for one that is not enabled
 * (CommonUI/Private/Input/UIActionRouterTypes.cpp:111-116), and UCommonButtonBase::HandleTriggeringActionCommited clicks only
 * while IsInteractionEnabled. So a disabled button's key clicks nothing, and once it is enabled the key clicks it.
 *
 * The wrapping area: the D-pad off the end of an area whose boundary rule is Wrap comes back on at its far side
 * (FHittestGrid::FindNextFocusableWidget, SlateCore/Private/Input/HittestGrid.cpp), and the search that finds where it comes
 * back on passes over a widget that is not enabled, as every other one does. On a canvas scaled to half and to double the
 * walk is the same, because the search is made in the positions the widgets are drawn at.
 *
 * Back: CommonUI's activatable widget handles Back through a binding of its own (UCommonActivatableWidget::NativeOnActivated
 * registers the back action on itself, CommonActivatableWidget.cpp:34-50), and whether a binding of the activatable itself
 * can be reached is decided at the activatable, not at its children (FActivatableTreeNode::IsWidgetReachableForInput stops
 * at the represented widget, UIActionRouterTypes.cpp:1056-1066). A dialog whose every button is disabled still closes on
 * Back, and the next Back closes the page under it.
 */
namespace DreamNavigationCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };
	const FVector2D ButtonSize(160.0, 40.0);

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FString& InName, UDreamWidget* InParent, const FVector2D& InPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, ButtonSize, InPosition);
		const bool bMade = Button != nullptr && Button->FaceNode != nullptr && Button->ButtonBehaviour != nullptr;
		InTest.TestTrue(*FString::Printf(TEXT("A button '%s' with a face and a behaviour can be made"), *InName), bMade);
		return bMade ? Button : nullptr;
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
	 * A column of three buttons inside an area that keeps the D-pad to itself and wraps at its edges, and one button below
	 * the area, outside it -- sized to fit the root at double scale. OutButtons is Top, Mid, Bottom, Outside.
	 */
	bool MakeWrappingColumn(FAutomationTestBase& InTest, FDreamDriverRig& InRig, TArray<UDreamButton*>& OutButtons)
	{
		UDreamWidget* Area = InRig.MakeWidget(TEXT("Area"), nullptr, FVector2D(240.0, 300.0), FVector2D(0.0, 20.0));
		if (!InTest.TestNotNull(TEXT("An area for the column"), Area))
		{
			return false;
		}
		Area->SetRestrictNavigationArea(true);
		Area->SetNavigationBoundaryRule(EDreamUINavigationBoundaryRule::Wrap);
		OutButtons = {
			PlaceButton(InTest, InRig, TEXT("Top"), Area, FVector2D(0.0, 100.0)),
			PlaceButton(InTest, InRig, TEXT("Mid"), Area, FVector2D(0.0, 0.0)),
			PlaceButton(InTest, InRig, TEXT("Bottom"), Area, FVector2D(0.0, -100.0)),
			PlaceButton(InTest, InRig, TEXT("Outside"), nullptr, FVector2D(0.0, -150.0)),
		};
		InRig.PumpFrames(1);
		return !OutButtons.Contains(nullptr);
	}

	/** A panel with one button on it and a navigation scope that waits to be opened, as a page or a dialog has. */
	UDreamUINavigationScope* MakeScreen(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, const FVector2D& InSize, UDreamButton*& OutButton)
	{
		UDreamWidget* Panel = InRig.MakeWidget(InName, nullptr, InSize);
		OutButton = Panel != nullptr ? InRig.MakeControl<UDreamButton>(FString::Printf(TEXT("%sButton"), InName), Panel, ButtonSize) : nullptr;
		UDreamUINavigationScope* Scope = Panel != nullptr ? Panel->AddComponent<UDreamUINavigationScope>() : nullptr;
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
	FDreamCoverageActionKeyDisabledButtonTest,
	"DreamGUI.Navigation.Actions.TheKeyOfAnActionOnADisabledButtonClicksNothingAndClicksItOnceTheButtonIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageActionKeyDisabledButtonTest, "DreamGUI.Navigation.Actions.TheKeyOfAnActionOnADisabledButtonClicksNothingAndClicksItOnceTheButtonIsEnabled", "[Nav][Disabled]")

/*
 * A button that is also what the Confirm action does, its Confirm keys X and the pad's left face button, made while the
 * button is disabled. Both keys click nothing. Enabled, each clicks it once; disabled again, neither does.
 */
bool FDreamCoverageActionKeyDisabledButtonTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationCoverageTestLocal;
	// The clicks the key makes, counted where they land, as the router's own tests count them. Declared before the rig.
	int32 Clicks = 0;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamButton* Button = Rig.IsUsable() ? PlaceButton(*this, Rig, TEXT("Accept"), nullptr, FVector2D::ZeroVector) : nullptr;
	UUIEventTrigger* ClickWatcher = Button != nullptr ? Button->FaceNode->AddComponent<UUIEventTrigger>() : nullptr;
	if (!TestNotNull(TEXT("A button whose face carries a click watcher came up"), ClickWatcher))
	{
		return false;
	}
	ClickWatcher->GetOnPointerClickEvent().AddLambda([&Clicks](UDreamPointerEventData*) { ++Clicks; });
	Button->SetIsEnabled(false);

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
	Rig.PumpFrames(1);

	TestTrue(TEXT("Pressing X and the pad's left face button completes"),
		Rig.Driver()->Sequence().Key(EKeys::X).Key(EKeys::Gamepad_FaceButton_Left).Perform());
	TestEqual(TEXT("Neither key clicked the disabled button"), Clicks, 0);

	Button->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Pressing X completes"), Rig.Driver()->Sequence().Key(EKeys::X).Perform());
	TestEqual(TEXT("Enabled, X clicked it once"), Clicks, 1);
	TestTrue(TEXT("Pressing the pad's left face button completes"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_FaceButton_Left).Perform());
	TestEqual(TEXT("...and the pad's key once more"), Clicks, 2);

	Button->SetIsEnabled(false);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Pressing X again completes"), Rig.Driver()->Sequence().Key(EKeys::X).Perform());
	TestEqual(TEXT("Disabled again, X clicks nothing"), Clicks, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageWrapAreaDisabledTest,
	"DreamGUI.Navigation.Scope.TheDPadOffTheEndOfAWrappingAreaComesBackOnAtItsFirstEnabledButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageWrapAreaDisabledTest, "DreamGUI.Navigation.Scope.TheDPadOffTheEndOfAWrappingAreaComesBackOnAtItsFirstEnabledButton", "[Nav][Disabled]")

/*
 * The column's top button disabled. From the middle one: down to the bottom, then off the end and back on -- past the
 * disabled top -- at the middle; up from the middle passes the top over too and wraps to the bottom. The button outside is
 * never reached. Enabled again, the top is where the walk comes back on.
 */
bool FDreamCoverageWrapAreaDisabledTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	TArray<UDreamButton*> Buttons;
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()) || !MakeWrappingColumn(*this, Rig, Buttons))
	{
		return false;
	}
	Buttons[0]->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!FocusOn(*this, Rig, Buttons[1]))
	{
		return false;
	}
	TestEqual(TEXT("Down to the bottom, off the end and back on past the disabled top, then up past it and round"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Up }),
		FString(TEXT("Bottom, Mid, Bottom")));

	Buttons[0]->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestEqual(TEXT("Enabled again, off the bottom comes back on at the top"),
		Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Down }), FString(TEXT("Top")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageWrapAreaScaledTest,
	"DreamGUI.Navigation.Scope.OnACanvasScaledToHalfAndToDoubleTheDPadOffTheEndOfAWrappingAreaComesBackOnAtItsFirstButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageWrapAreaScaledTest, "DreamGUI.Navigation.Scope.OnACanvasScaledToHalfAndToDoubleTheDPadOffTheEndOfAWrappingAreaComesBackOnAtItsFirstButton", "[Nav][Scaled]")

/* At half and at double: down to the bottom, off the end round to the top, up off the top round to the bottom -- never out. */
bool FDreamCoverageWrapAreaScaledTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationCoverageTestLocal;
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		TArray<UDreamButton*> Buttons;
		if (!ComeUp(*this, Rig, Scale) || !MakeWrappingColumn(*this, Rig, Buttons) || !FocusOn(*this, Rig, Buttons[1]))
		{
			return false;
		}
		TestEqual(*At(Scale, TEXT("Down, off the end round to the first, and up off it round to the last -- never out of the area")),
			Walk(*this, Rig, Buttons, { EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Down, EKeys::Gamepad_DPad_Up }),
			FString(TEXT("Bottom, Top, Bottom")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageBackDisabledDialogTest,
	"DreamGUI.Navigation.Back.BackClosesADialogWhoseButtonsAreAllDisabledAndTheNextBackThePageUnderIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageBackDisabledDialogTest, "DreamGUI.Navigation.Back.BackClosesADialogWhoseButtonsAreAllDisabledAndTheNextBackThePageUnderIt", "[Nav][Disabled]")

/*
 * A page and a dialog over it, the dialog's only button disabled -- nothing in it can take the focus. The pad's Back through
 * the player controller still closes the dialog and leaves the page open and in front; the next Back closes the page.
 */
bool FDreamCoverageBackDisabledDialogTest::RunTest(const FString& Parameters)
{
	using namespace DreamNavigationCoverageTestLocal;
	FDreamRigOptions Options;
	Options.ViewportSize = ViewportSize;
	Options.InputHost = EDreamRigInputHost::StandaloneActor;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(Options);
	Rig.BindTest(this);
	UDreamUINavigationStack* Stack = Rig.IsUsable() ? UDreamUINavigationStack::Get(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig came up with a navigation stack"), Stack))
	{
		return false;
	}
	UDreamButton* PageButton = nullptr;
	UDreamButton* DialogButton = nullptr;
	UDreamUINavigationScope* Page = MakeScreen(*this, Rig, TEXT("Page"), FVector2D(800.0, 500.0), PageButton);
	UDreamUINavigationScope* Dialog = MakeScreen(*this, Rig, TEXT("Dialog"), FVector2D(400.0, 240.0), DialogButton);
	if (Page == nullptr || Dialog == nullptr)
	{
		return false;
	}
	DialogButton->SetIsEnabled(false);
	Rig.PumpFrames(1);
	Page->ActivateScope();
	Dialog->ActivateScope();
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The dialog is in front of the page"), Stack->GetActiveScope(0) == Dialog))
	{
		return false;
	}

	TestTrue(TEXT("The pad's Back completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("Back closed the dialog, though nothing in it could be focused"), Dialog->IsScopeActive());
	TestTrue(TEXT("...and only the dialog: the page is open and in front"), Page->IsScopeActive() && Stack->GetActiveScope(0) == Page);

	TestTrue(TEXT("The pad's Back again completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("The second Back closed the page"), Page->IsScopeActive());
	return true;
}

#endif
