// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIInputServices.h"
#include "Engine/World.h"
#include "Event/DreamUIInputSubsystem.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/DreamUITabOrder.h"
#include "Interaction/UIButton.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "Interaction/UISelectable.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"

/*
 * Tab, Shift+Tab, the focus look and the scroll keys on a real screen: the rig's screen-space root canvas, controls built
 * the way the runtime builds them, and keys sent the way the rig's input host delivers them (FDreamDriverSequence::Tab,
 * Key) -- through the key routing to the player's navigation step, which asks the Tab order (FDreamUITabOrder) where to
 * go and moves the focus there.
 *
 * What the pure order tests next door cannot show is the whole press: that the step starts at the player's focus, lands
 * on the stop the order names, scrolls a clipped one into view, draws the focus only when keys put it there, rings it on a
 * page no presenter hosts, and stands down for a player whose screen says Game.
 */

namespace DreamTabNavigationTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(160.0, 40.0);
	/** The bottom-right corner of the viewport, where nothing but the rig's root is. */
	const FVector2D FarFromEverything(1200.0, 680.0);

	/** The settings every test here runs under, whatever the project's config says. */
	struct FTabSettingsGuard
	{
		TGuardValue<bool> TabNavigation;
		TGuardValue<EDreamUITabOrder> TabOrder;
		TGuardValue<bool> WrapsAtScreenEnd;
		TGuardValue<bool> FocusVisibleOnlyFromKeys;
		TGuardValue<EDreamUIScopeInputMode> InputModeWithoutScope;

		FTabSettingsGuard()
			: TabNavigation(GetMutableDefault<UDreamGUISettings>()->bTabNavigation, true)
			, TabOrder(GetMutableDefault<UDreamGUISettings>()->TabOrder, EDreamUITabOrder::Hierarchy)
			, WrapsAtScreenEnd(GetMutableDefault<UDreamGUISettings>()->bTabWrapsAtScreenEnd, true)
			, FocusVisibleOnlyFromKeys(GetMutableDefault<UDreamGUISettings>()->bFocusVisibleOnlyFromKeys, true)
			, InputModeWithoutScope(GetMutableDefault<UDreamGUISettings>()->InputModeWithoutScope, EDreamUIScopeInputMode::All)
		{
		}
	};

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent,
		const FVector2D& InAnchoredPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, ButtonSize, InAnchoredPosition);
		if (!InTest.TestNotNull(*FString::Printf(TEXT("A button '%s' can be made"), InName), Button)
			|| !InTest.TestNotNull(*FString::Printf(TEXT("Button '%s' has a face and a behaviour"), InName), Button->ButtonBehaviour.Get()))
		{
			return nullptr;
		}
		return Button;
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig, int32 InUserIndex = 0)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(InUserIndex) : nullptr;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	/** Which of InButtons player 0's focus is on, by name; "(none)" when it is on none of them. */
	FString FocusedName(const FDreamDriverRig& InRig, const TArray<UDreamButton*>& InButtons)
	{
		const UDreamWidget* Focus = FocusOf(InRig);
		for (const UDreamButton* Button : InButtons)
		{
			if (IsPartOf(Focus, Button))
			{
				return Button->GetDisplayName();
			}
		}
		return FString(TEXT("(none)"));
	}

	/** InPresses presses of Tab (Shift+Tab when bInBackward) through the rig's input host, and where the focus was after each. */
	FString PressTab(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TArray<UDreamButton*>& InButtons, int32 InPresses, bool bInBackward)
	{
		TArray<FString> Names;
		for (int32 Press = 0; Press < InPresses; ++Press)
		{
			const bool bPerformed = bInBackward
				? InRig.Driver()->Sequence().ShiftTab().Perform()
				: InRig.Driver()->Sequence().Tab().Perform();
			InTest.TestTrue(TEXT("the key press completes"), bPerformed);
			Names.Add(FocusedName(InRig, InButtons));
		}
		return FString::Join(Names, TEXT(", "));
	}

	/**
	 * A navigation scope on InWidget that waits to be pushed. A widget in play enables what is added to it at once, and a
	 * scope enabled pushes itself (bActivateWhenEnabled), taking the focus: it is taken off again -- giving back the focus
	 * there was -- and told to wait from then on.
	 */
	UDreamUINavigationScope* AddWaitingScope(UDreamWidget* InWidget)
	{
		UDreamUINavigationScope* Scope = InWidget != nullptr ? InWidget->AddComponent<UDreamUINavigationScope>() : nullptr;
		if (Scope != nullptr)
		{
			Scope->SetActivateWhenEnabled(false);
			Scope->DeactivateScope();
		}
		return Scope;
	}

	bool FocusForNavigation(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InWidget)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return InTest.TestTrue(*FString::Printf(TEXT("Player 0's focus can be put on '%s'"), *GetNameSafe(InWidget)),
			Services != nullptr && Services->FocusForNavigation(InWidget, 0));
	}

	/** The widget a native ring is marking, read from its record (a protected UPROPERTY); null for a ring a Blueprint drives. */
	UDreamWidget* MarkedBy(FAutomationTestBase& InTest, UUINavigationInputSelectionHandler* InRing)
	{
		if (InRing == nullptr || InRing->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
		{
			return nullptr;
		}
		const FProperty* Property = InRing->GetClass()->FindPropertyByName(TEXT("CurrentSelected"));
		if (Property == nullptr)
		{
			InTest.AddError(TEXT("UUINavigationInputSelectionHandler no longer has a property named 'CurrentSelected'."));
			return nullptr;
		}
		return Property->ContainerPtrToValuePtr<TWeakObjectPtr<UDreamWidget>>(InRing)->Get();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationFormTest,
	"DreamGUI.Navigation.Tab.TheTabKeyWalksTheScreenInTreeOrderAndShiftTabWalksItBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationFormTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// A column of three, and lower down a button pushed to the right: geometrically the first Tab went from the top of the
	// column straight to it.
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), nullptr, FVector2D(-300.0, 150.0));
	UDreamButton* Second = PlaceButton(*this, Rig, TEXT("Second"), nullptr, FVector2D(-300.0, 50.0));
	UDreamButton* Third = PlaceButton(*this, Rig, TEXT("Third"), nullptr, FVector2D(-300.0, -50.0));
	UDreamButton* Submit = PlaceButton(*this, Rig, TEXT("Submit"), nullptr, FVector2D(300.0, -150.0));
	if (First == nullptr || Second == nullptr || Third == nullptr || Submit == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	const TArray<UDreamButton*> Buttons = { First, Second, Third, Submit };

	TestEqual(TEXT("with nothing focused, Tab enters at the first stop and walks the tree, round at the end"),
		PressTab(*this, Rig, Buttons, 5, false), TEXT("First, Second, Third, Submit, First"));
	TestEqual(TEXT("Shift+Tab walks exactly the same way back"),
		PressTab(*this, Rig, Buttons, 5, true), TEXT("Submit, Third, Second, First, Submit"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationShiftStartTest,
	"DreamGUI.Navigation.Tab.ShiftTabWithNothingFocusedStartsAtTheLastStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationShiftStartTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), nullptr, FVector2D(-200.0, 0.0));
	UDreamButton* Last = PlaceButton(*this, Rig, TEXT("Last"), nullptr, FVector2D(200.0, 0.0));
	if (First == nullptr || Last == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	// Both keys used to land on the same "default" top-left control.
	TestEqual(TEXT("Shift+Tab with nothing focused lands on the last stop"), PressTab(*this, Rig, { First, Last }, 1, true), TEXT("Last"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationOnlyDropdownTest,
	"DreamGUI.Navigation.Tab.TabInTheOpenListOfTheOnlyControlLeavesTheFocusOnTheDropdown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Tab in an open dropdown list closes the list and steps on from the dropdown (CloseAndContinue). With the dropdown the only
 * control on its screen the walk comes round to the dropdown itself, and the step has nowhere to go: it fell back to what
 * had the focus before it -- a row of the list that had just closed -- and put the focus back there, on a hidden row. The
 * focus stays where closing the list gave it back now: the dropdown's face.
 */
bool FDreamTabNavigationOnlyDropdownTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDropdown* Dropdown = Rig.MakeControl<UDreamDropdown>(TEXT("Quality"), nullptr, FVector2D(200.0, 40.0), FVector2D(0.0, 200.0));
	if (!TestNotNull(TEXT("A dropdown"), Dropdown) || !TestNotNull(TEXT("with a face to take focus"), Dropdown->FaceNode.Get()))
	{
		return false;
	}
	TArray<FText> Options;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Options.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Option %d"), Index)));
	}
	Dropdown->SetOptions(Options);
	Dropdown->SetSelectedIndex(0);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the dropdown completes"), Rig.Driver()->Find(FDreamBy::Widget(Dropdown))->Click());
	Rig.PumpFrames(2);
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("The click opened the list"), Dropdown->IsOpen())
		|| !TestTrue(TEXT("...and put the focus in it"), Popups != nullptr && Popups->FindPopupContaining(FocusOf(Rig), INDEX_NONE) != nullptr))
	{
		return false;
	}

	TestTrue(TEXT("Pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestFalse(TEXT("Tab closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("With no other control to go to, the focus stayed on the dropdown's face"), FocusOf(Rig), Dropdown->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationOtherPlayersPopupTest,
	"DreamGUI.Navigation.Tab.AnotherPlayersPopupOnASharedScreenHoldsNoStopsForThisPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A popup is lifted onto its screen root, and players can share one: another player's open menu then hangs under this
 * player's screen with the rest of it, and its controls were walked as this player's Tab stops. A popup the walk is not in
 * holds none of its stops now. Player 1's menu, open on the screen player 0's controls are on; player 0 tabs round theirs.
 */
bool FDreamTabNavigationOtherPlayersPopupTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), nullptr, FVector2D(-300.0, 150.0));
	UDreamButton* Second = PlaceButton(*this, Rig, TEXT("Second"), nullptr, FVector2D(-300.0, 50.0));
	UDreamWidget* Menu = Rig.MakeWidget(TEXT("OtherPlayersMenu"), nullptr, FVector2D(300.0, 200.0), FVector2D(300.0, 0.0));
	UDreamButton* InMenu = Menu != nullptr ? PlaceButton(*this, Rig, TEXT("InMenu"), Menu, FVector2D::ZeroVector) : nullptr;
	if (!TestTrue(TEXT("Two buttons, a menu with a button, the world's input and its popup layer"),
		First != nullptr && Second != nullptr && InMenu != nullptr && Input != nullptr && Popups != nullptr))
	{
		return false;
	}
	if (!TestNotNull(TEXT("A second player"), Input->GetOrCreateUser(1)))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		Popups->Dismiss(Menu);
		Input->RemoveUser(1);
	};
	Rig.PumpFrames(1);
	FDreamPopupParams Params;
	Params.Popup = Menu;
	Params.UserIndex = 1;
	Params.OutsideClick = EDreamPopupOutsideClick::Ignore;
	Params.bFocusOnOpen = false;
	if (!TestTrue(TEXT("Player 1 opens the menu"), Popups->Push(Params)))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestEqual(TEXT("Player 0's Tab walks player 0's controls and goes round, never into player 1's menu"),
		PressTab(*this, Rig, { First, Second, InMenu }, 3, false), TEXT("First, Second, First"));
	TestTrue(TEXT("...and the menu is still player 1's, open"), Popups->IsOpen(Menu));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationLayerInFrontOfListTest,
	"DreamGUI.Navigation.Tab.TabOnALayerInFrontOfAnOpenListStaysOnThatLayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A layer put up in front of an open list -- a dialog, a page, sorted above the list -- with the player's focus on it gets
 * Back before the list does (UDreamUIPopupLayer::HandleBack). Tab went to the list all the same: the list was the player's
 * domain, so the first Tab closed it behind the layer and went on from its dropdown. Tab is the layer's while the focus is
 * on it now, as Back is: it moves between the layer's controls and leaves the list open.
 */
bool FDreamTabNavigationLayerInFrontOfListTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDropdown* Dropdown = Rig.MakeControl<UDreamDropdown>(TEXT("Quality"), nullptr, FVector2D(200.0, 40.0), FVector2D(0.0, 250.0));
	if (!TestNotNull(TEXT("A dropdown"), Dropdown))
	{
		return false;
	}
	Dropdown->SetOptions({ FText::AsCultureInvariant(TEXT("Low")), FText::AsCultureInvariant(TEXT("High")) });
	Dropdown->SetSelectedIndex(0);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the dropdown completes"), Rig.Driver()->Find(FDreamBy::Widget(Dropdown))->Click());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The click opened the list"), Dropdown->IsOpen()))
	{
		return false;
	}
	// The layer after the list opened, sorted in front of it on the same screen, as a dialog put up over it would be.
	UDreamWidget* Layer = Rig.MakeWidget(TEXT("Layer"), nullptr, FVector2D(600.0, 300.0), FVector2D(0.0, -150.0));
	UDreamCanvas* LayerCanvas = Layer != nullptr ? Layer->AddComponent<UDreamCanvas>() : nullptr;
	if (!TestNotNull(TEXT("A layer with a canvas of its own"), LayerCanvas))
	{
		return false;
	}
	LayerCanvas->SetOverrideSorting(true);
	LayerCanvas->SetSortOrder(28000, /*PropagateToChildrenCanvas*/true);
	if (const UDreamCanvas* RootCanvas = Rig.RootCanvas())
	{
		LayerCanvas->SetTraceChannel(RootCanvas->GetTraceChannel());
	}
	UDreamButton* Ok = PlaceButton(*this, Rig, TEXT("Ok"), Layer, FVector2D(-120.0, 0.0));
	UDreamButton* Cancel = PlaceButton(*this, Rig, TEXT("Cancel"), Layer, FVector2D(120.0, 0.0));
	if (Ok == nullptr || Cancel == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!FocusForNavigation(*this, Rig, Ok->FaceNode.Get()))
	{
		return false;
	}

	TestEqual(TEXT("Tab on the layer in front moves between its own controls"), PressTab(*this, Rig, { Ok, Cancel }, 1, false), TEXT("Cancel"));
	TestTrue(TEXT("...and leaves the list behind it open"), Dropdown->IsOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationRevealTest,
	"DreamGUI.Navigation.Tab.AStopScrolledOutOfSightIsScrolledIntoViewWhenTabReachesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationRevealTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 400.0));
	if (!TestNotNull(TEXT("a scroll box"), Box) || !TestNotNull(TEXT("with content"), Box->GetContentNode()))
	{
		return false;
	}
	// The box's stack measures a button by its content, as UMG's does, not by the rect it was made with: buttons alone
	// stack to far less than the box is tall. A plain gap after each -- measured at its own rect, as the stress lists'
	// rows are -- pushes the later buttons below the fold.
	TArray<UDreamButton*> Rows;
	for (int32 RowIndex = 0; RowIndex < 8; ++RowIndex)
	{
		UDreamButton* Row = Rig.MakeControl<UDreamButton>(FString::Printf(TEXT("Row%d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
		if (!TestNotNull(TEXT("a row"), Row))
		{
			return false;
		}
		Rows.Add(Row);
		Rig.MakeWidget(FString::Printf(TEXT("Gap%d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 150.0));
	}
	Box->RefreshContentExtent();
	Rig.PumpFrames(2);
	TestEqual(TEXT("the box starts at the top"), Box->GetScrollOffset(), 0.0f);
	if (!TestTrue(TEXT("the rows run past the bottom of the box"), Box->GetScrollOffsetOfEnd() > 0.0f))
	{
		return false;
	}
	// Clipped away, and one scroll from sight: still a stop.
	TestTrue(TEXT("a row below the fold is a Tab stop"), FDreamUITabOrder::IsTabStop(Rows[5]->FaceNode.Get()));

	if (!FocusForNavigation(*this, Rig, Rows[0]->FaceNode.Get()))
	{
		return false;
	}
	TestEqual(TEXT("Tab walks the rows in order, past the fold"), PressTab(*this, Rig, Rows, 5, false), TEXT("Row1, Row2, Row3, Row4, Row5"));
	Rig.PumpFrames(30);
	TestTrue(TEXT("and the box scrolled to show the row Tab reached"), Box->GetScrollOffset() > 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationLegacyTest,
	"DreamGUI.Navigation.Tab.LegacyGeometricKeepsTheOldOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationLegacyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// A B / C D.
	UDreamButton* CellA = PlaceButton(*this, Rig, TEXT("A"), nullptr, FVector2D(-150.0, 100.0));
	UDreamButton* CellB = PlaceButton(*this, Rig, TEXT("B"), nullptr, FVector2D(150.0, 100.0));
	UDreamButton* CellC = PlaceButton(*this, Rig, TEXT("C"), nullptr, FVector2D(-150.0, -100.0));
	UDreamButton* CellD = PlaceButton(*this, Rig, TEXT("D"), nullptr, FVector2D(150.0, -100.0));
	if (CellA == nullptr || CellB == nullptr || CellC == nullptr || CellD == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("in tree order, Next from B is C"), CellB->ButtonBehaviour->FindSelectableOnNext() == CellC->ButtonBehaviour.Get());
	GetMutableDefault<UDreamGUISettings>()->TabOrder = EDreamUITabOrder::LegacyGeometric;
	// The old pair, kept for one version: the nearest to the right, else the nearest below.
	TestTrue(TEXT("legacy: Next from A is B, to its right"), CellA->ButtonBehaviour->FindSelectableOnNext() == CellB->ButtonBehaviour.Get());
	TestTrue(TEXT("legacy: Next from B is D, below it, nothing being to its right"), CellB->ButtonBehaviour->FindSelectableOnNext() == CellD->ButtonBehaviour.Get());
	TestTrue(TEXT("legacy: Prev from D is C, to its left"), CellD->ButtonBehaviour->FindSelectableOnPrev() == CellC->ButtonBehaviour.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationInputModeTest,
	"DreamGUI.Navigation.InputMode.GameTurnsTheKeysOffForThatPlayerOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationInputModeTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("the world has a navigation stack"), Stack))
	{
		return false;
	}
	UDreamWidget* Hud = Rig.MakeWidget(TEXT("Hud"), nullptr, FVector2D(1000.0, 400.0));
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), Hud, FVector2D(-200.0, 0.0));
	UDreamButton* Second = PlaceButton(*this, Rig, TEXT("Second"), Hud, FVector2D(200.0, 0.0));
	if (Hud == nullptr || First == nullptr || Second == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	const TArray<UDreamButton*> Buttons = { First, Second };

	// Another player's gameplay HUD changes nothing for player 0.
	UDreamUINavigationScope* OtherPlayers = AddWaitingScope(Hud);
	if (!TestNotNull(TEXT("a scope for player 1's HUD"), OtherPlayers))
	{
		return false;
	}
	TestNull(TEXT("player 0 starts with nothing focused"), FocusOf(Rig));
	OtherPlayers->SetConfineNavigation(false);
	OtherPlayers->SetUserIndex(1);
	OtherPlayers->SetInputMode(EDreamUIScopeInputMode::Game);
	OtherPlayers->ActivateScope();
	TestEqual(TEXT("player 1's HUD is Game for player 1"), Stack->GetEffectiveInputMode(1), EDreamUIScopeInputMode::Game);
	TestEqual(TEXT("and player 0 is untouched"), Stack->GetEffectiveInputMode(0), EDreamUIScopeInputMode::All);
	TestEqual(TEXT("so player 0's Tab still lands"), PressTab(*this, Rig, Buttons, 1, false), TEXT("First"));
	OtherPlayers->DeactivateScope();

	// Player 0's own HUD in Game mode: the keys are the game's.
	UDreamUINavigationScope* PlayerHud = AddWaitingScope(Hud);
	if (!TestNotNull(TEXT("a scope for player 0's HUD"), PlayerHud))
	{
		return false;
	}
	PlayerHud->SetConfineNavigation(false);
	PlayerHud->SetUserIndex(0);
	PlayerHud->SetInputMode(EDreamUIScopeInputMode::Game);
	PlayerHud->ActivateScope();
	TestEqual(TEXT("player 0's HUD makes player 0's mode Game"), Stack->GetEffectiveInputMode(0), EDreamUIScopeInputMode::Game);
	if (!FocusForNavigation(*this, Rig, First->FaceNode.Get()))
	{
		return false;
	}
	TestEqual(TEXT("Tab no longer moves player 0's focus"), PressTab(*this, Rig, Buttons, 1, false), TEXT("First"));
	TestTrue(TEXT("nor does the D-pad"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Right).Perform());
	TestEqual(TEXT("...which leaves the focus where it was"), FocusedName(Rig, Buttons), TEXT("First"));

	// A menu over it gives the keys back.
	PlayerHud->SetInputMode(EDreamUIScopeInputMode::Menu);
	TestEqual(TEXT("a menu gives Tab back"), PressTab(*this, Rig, Buttons, 1, false), TEXT("Second"));
	PlayerHud->DeactivateScope();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationFocusVisibleTest,
	"DreamGUI.Navigation.Focus.FocusIsDrawnAfterAKeyStepButNotAfterAClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationFocusVisibleTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), nullptr, FVector2D(-200.0, 0.0));
	UDreamButton* Second = PlaceButton(*this, Rig, TEXT("Second"), nullptr, FVector2D(200.0, 0.0));
	if (First == nullptr || Second == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	const TArray<UDreamButton*> Buttons = { First, Second };
	UUIButton* FirstBehaviour = First->ButtonBehaviour.Get();
	UUIButton* SecondBehaviour = Second->ButtonBehaviour.Get();

	// Keys put the focus there: it is drawn (CSS's :focus-visible).
	TestEqual(TEXT("Tab focuses the first button"), PressTab(*this, Rig, Buttons, 1, false), TEXT("First"));
	TestEqual(TEXT("and it draws focused"), FirstBehaviour->GetCurrentSelectionState(), EUISelectableSelectionState::Focused);

	// A click puts the focus somewhere else: it moves, and is not drawn once the pointer leaves.
	TestTrue(TEXT("clicking the second button completes"), Rig.Driver()->Find(FDreamBy::Widget(Second))->Click());
	TestTrue(TEXT("moving the pointer away completes"), Rig.Driver()->Sequence().MoveToPixel(FarFromEverything).Perform());
	TestEqual(TEXT("the click moved the focus"), FocusedName(Rig, Buttons), TEXT("Second"));
	TestTrue(TEXT("the clicked button holds focus"), SecondBehaviour->IsFocused());
	TestEqual(TEXT("but does not draw it"), SecondBehaviour->GetCurrentSelectionState(), EUISelectableSelectionState::Normal);
	TestEqual(TEXT("and the first no longer draws it either"), FirstBehaviour->GetCurrentSelectionState(), EUISelectableSelectionState::Normal);

	// The next key draws it again, where the key took it.
	TestEqual(TEXT("Shift+Tab moves the focus back"), PressTab(*this, Rig, Buttons, 1, true), TEXT("First"));
	TestEqual(TEXT("and draws it"), FirstBehaviour->GetCurrentSelectionState(), EUISelectableSelectionState::Focused);

	// With the setting off, focus is always drawn, a clicked control included.
	GetMutableDefault<UDreamGUISettings>()->bFocusVisibleOnlyFromKeys = false;
	TestTrue(TEXT("clicking the second button again completes"), Rig.Driver()->Find(FDreamBy::Widget(Second))->Click());
	TestTrue(TEXT("moving the pointer away again completes"), Rig.Driver()->Sequence().MoveToPixel(FarFromEverything).Perform());
	TestEqual(TEXT("with the setting off a clicked control draws its focus"), SecondBehaviour->GetCurrentSelectionState(), EUISelectableSelectionState::Focused);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationRingTest,
	"DreamGUI.Navigation.Focus.APageWithNoPresenterGetsAFocusRingOnKeysAndOnInitialFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationRingTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The rig's screen is a root canvas no presenter component hosts, as every page the screen subsystem shows is: the ring
	// used to be made by presenters alone, so these never had one.
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), nullptr, FVector2D(-300.0, 100.0));
	UDreamButton* Second = PlaceButton(*this, Rig, TEXT("Second"), nullptr, FVector2D(0.0, 100.0));
	UDreamWidget* Panel = Rig.MakeWidget(TEXT("Panel"), nullptr, FVector2D(400.0, 200.0), FVector2D(0.0, -150.0));
	UDreamButton* InPanel = PlaceButton(*this, Rig, TEXT("InPanel"), Panel, FVector2D::ZeroVector);
	if (First == nullptr || Second == nullptr || Panel == nullptr || InPanel == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	const TArray<UDreamButton*> Buttons = { First, Second, InPanel };

	TestNull(TEXT("no ring before anything is focused"), UUINavigationInputSelectionHandler::FindFor(First->FaceNode.Get()));
	TestEqual(TEXT("Tab focuses the first button"), PressTab(*this, Rig, Buttons, 1, false), TEXT("First"));
	UUINavigationInputSelectionHandler* Ring = UUINavigationInputSelectionHandler::FindFor(First->FaceNode.Get());
	if (!TestNotNull(TEXT("a focus drawn by keys brings a ring on a page with no presenter"), Ring))
	{
		return false;
	}
	TestTrue(TEXT("the ring is not something a pointer can hit"),
		Ring->GetWidget() != nullptr && Ring->GetWidget()->GetRaycastable() == EDreamWidgetRaycastableType::Disabled);
	if (const UDreamWidget* Marked = MarkedBy(*this, Ring))
	{
		TestTrue(TEXT("the ring marks the focused button"), IsPartOf(Marked, First));
	}

	TestEqual(TEXT("Tab moves the focus on"), PressTab(*this, Rig, Buttons, 1, false), TEXT("Second"));
	TestTrue(TEXT("the same ring serves the whole screen"), UUINavigationInputSelectionHandler::FindFor(Second->FaceNode.Get()) == Ring);
	if (const UDreamWidget* Marked = MarkedBy(*this, Ring))
	{
		TestTrue(TEXT("and moved with it"), IsPartOf(Marked, Second));
	}

	// A screen opening gives focus by code, and the player's last input was a key: the ring comes with that focus too.
	UDreamUINavigationScope* Scope = AddWaitingScope(Panel);
	if (!TestNotNull(TEXT("a scope for the panel"), Scope))
	{
		return false;
	}
	TestEqual(TEXT("before the panel opens, the focus is where Tab left it"), FocusedName(Rig, Buttons), TEXT("Second"));
	Scope->ActivateScope();
	TestEqual(TEXT("the screen took the focus"), FocusedName(Rig, Buttons), TEXT("InPanel"));
	if (const UDreamWidget* Marked = MarkedBy(*this, Ring))
	{
		TestTrue(TEXT("and the ring came with that first focus"), IsPartOf(Marked, InPanel));
	}
	Scope->DeactivateScope();
	return true;
}

namespace DreamTabNavigationTestLocal
{
	/**
	 * The ring's root is on InMarked at its size, and every picture under the root covers it: at least its size, and no
	 * more than a frame's few pixels over. The project's ring class is the plugin's, whose frame is a child authored at a
	 * fixed size; it stayed that size over every control.
	 */
	void ExpectRingCovers(FAutomationTestBase& InTest, UUINavigationInputSelectionHandler* InRing, const UDreamWidget* InMarked, const TCHAR* InWhat)
	{
		const UDreamWidget* RingWidget = InRing != nullptr ? InRing->GetWidget() : nullptr;
		if (!InTest.TestNotNull(*FString::Printf(TEXT("the ring marks %s"), InWhat), InMarked)
			|| !InTest.TestNotNull(TEXT("the ring has a widget"), RingWidget))
		{
			return;
		}
		InTest.TestEqual(*FString::Printf(TEXT("the ring's root is %s's width"), InWhat), RingWidget->GetWidth(), InMarked->GetWidth(), 0.5f);
		InTest.TestEqual(*FString::Printf(TEXT("the ring's root is %s's height"), InWhat), RingWidget->GetHeight(), InMarked->GetHeight(), 0.5f);
		constexpr float MaxFrameMargin = 16.0f;
		int32 Pictures = 0;
		for (const UDreamWidget* Child : RingWidget->GetChildren())
		{
			if (!IsValid(Child) || Child->GetVisual() == nullptr)
			{
				continue;
			}
			++Pictures;
			const bool bCovers = Child->GetWidth() >= InMarked->GetWidth() - 0.5f && Child->GetHeight() >= InMarked->GetHeight() - 0.5f
				&& Child->GetWidth() <= InMarked->GetWidth() + MaxFrameMargin && Child->GetHeight() <= InMarked->GetHeight() + MaxFrameMargin;
			InTest.TestTrue(*FString::Printf(TEXT("the ring's picture '%s' (%.1f x %.1f) covers %s (%.1f x %.1f)"), *Child->GetDisplayName(),
				Child->GetWidth(), Child->GetHeight(), InWhat, InMarked->GetWidth(), InMarked->GetHeight()), bCovers);
		}
		InTest.TestTrue(TEXT("the project's ring draws a picture"), Pictures > 0);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationRingFitsAndStaysOffTest,
	"DreamGUI.Navigation.Focus.TheRingCoversTheControlItMarksAndKeepsOffOneThatMarksItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationRingFitsAndStaysOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// A wide button and a tall one, so a ring of any fixed size fits neither; one that marks its own focus; one after it.
	UDreamButton* Wide = Rig.MakeControl<UDreamButton>(TEXT("Wide"), nullptr, FVector2D(420.0, 36.0), FVector2D(-300.0, 150.0));
	UDreamButton* Tall = Rig.MakeControl<UDreamButton>(TEXT("Tall"), nullptr, FVector2D(60.0, 140.0), FVector2D(100.0, 150.0));
	UDreamButton* OwnLook = PlaceButton(*this, Rig, TEXT("OwnLook"), nullptr, FVector2D(-200.0, -100.0));
	UDreamButton* After = PlaceButton(*this, Rig, TEXT("After"), nullptr, FVector2D(200.0, -100.0));
	if (!TestTrue(TEXT("the wide and tall buttons have faces and behaviours"), Wide != nullptr && Tall != nullptr
			&& Wide->ButtonBehaviour != nullptr && Tall->ButtonBehaviour != nullptr)
		|| OwnLook == nullptr || After == nullptr)
	{
		return false;
	}
	OwnLook->ButtonBehaviour->SetUseFocusRing(false);
	Rig.PumpFrames(1);
	const TArray<UDreamButton*> Buttons = { Wide, Tall, OwnLook, After };
	// Long enough for the ring's flight and fade to end (UUINavigationInputSelectionHandler's AnimDuration, 0.25 s).
	constexpr int32 SettleFrames = 40;

	TestEqual(TEXT("Tab focuses the wide button"), PressTab(*this, Rig, Buttons, 1, false), TEXT("Wide"));
	UUINavigationInputSelectionHandler* Ring = UUINavigationInputSelectionHandler::FindFor(Wide->FaceNode.Get());
	if (!TestNotNull(TEXT("focus drawn by keys brings the project's ring"), Ring))
	{
		return false;
	}
	Rig.PumpFrames(SettleFrames);
	ExpectRingCovers(*this, Ring, MarkedBy(*this, Ring), TEXT("the wide button"));

	TestEqual(TEXT("Tab moves on to the tall button"), PressTab(*this, Rig, Buttons, 1, false), TEXT("Tall"));
	Rig.PumpFrames(SettleFrames);
	ExpectRingCovers(*this, Ring, MarkedBy(*this, Ring), TEXT("the tall button"));

	// A control that marks its own focus takes no ring, and the ring does not stay behind on the one the focus left.
	TestEqual(TEXT("Tab moves on to the button that marks its own focus"), PressTab(*this, Rig, Buttons, 1, false), TEXT("OwnLook"));
	Rig.PumpFrames(SettleFrames);
	TestNull(TEXT("the ring marks nothing while that button has the focus"), MarkedBy(*this, Ring));
	TestTrue(TEXT("and has faded out"), Ring->GetWidget() != nullptr && Ring->GetWidget()->GetRenderOpacity() < 0.01f);
	TestEqual(TEXT("the button drew its focus with its own look"), OwnLook->ButtonBehaviour->GetCurrentSelectionState(), EUISelectableSelectionState::Focused);

	// The ring fades back in on the next control that uses it, though the focus arrives there twice -- selected, then
	// entered by navigation -- and the second arrival moves the ring while its fade-in has only just started.
	TestEqual(TEXT("Tab moves on to the next button"), PressTab(*this, Rig, Buttons, 1, false), TEXT("After"));
	Rig.PumpFrames(SettleFrames);
	if (UUINavigationInputSelectionHandler* NextRing = UUINavigationInputSelectionHandler::FindFor(After->FaceNode.Get()))
	{
		TestTrue(TEXT("the ring comes back on a control that uses it"), NextRing->GetWidget() != nullptr && NextRing->GetWidget()->GetRenderOpacity() > 0.99f);
		ExpectRingCovers(*this, NextRing, MarkedBy(*this, NextRing), TEXT("the next button"));
	}
	else
	{
		AddError(TEXT("the screen has a ring for the next button"));
	}

	// Asked for the ring again while it has the focus, the button gets it at once.
	TestEqual(TEXT("Shift+Tab goes back to the button that marks its own focus"), PressTab(*this, Rig, Buttons, 1, true), TEXT("OwnLook"));
	OwnLook->ButtonBehaviour->SetUseFocusRing(true);
	Rig.PumpFrames(SettleFrames);
	if (UUINavigationInputSelectionHandler* OwnRing = UUINavigationInputSelectionHandler::FindFor(OwnLook->FaceNode.Get()))
	{
		TestTrue(TEXT("the ring shows on the button that now uses it"), OwnRing->GetWidget() != nullptr && OwnRing->GetWidget()->GetRenderOpacity() > 0.99f);
		ExpectRingCovers(*this, OwnRing, MarkedBy(*this, OwnRing), TEXT("the button that now uses it"));
	}
	else
	{
		AddError(TEXT("the screen has a ring for the button that now uses it"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabNavigationScrollBoxTest,
	"DreamGUI.Navigation.Scroll.AFocusedScrollBoxScrollsItselfByPageDownAndTheStick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTabNavigationScrollBoxTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabNavigationTestLocal;
	FTabSettingsGuard Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// Text-only content, nothing inside to focus: an EULA, a credits roll. The box itself takes the focus.
	UDreamScrollBox* Box = Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(300.0, 400.0));
	if (!TestNotNull(TEXT("a scroll box"), Box) || !TestNotNull(TEXT("with content"), Box->GetContentNode()))
	{
		return false;
	}
	for (int32 RowIndex = 0; RowIndex < 20; ++RowIndex)
	{
		Rig.MakeWidget(FString::Printf(TEXT("Line%02d"), RowIndex), Box->GetContentNode(), FVector2D(300.0, 100.0));
	}
	Box->SetIsFocusable(true);
	Box->ApplyStyle();
	Box->RefreshContentExtent();
	Rig.PumpFrames(2);
	UDreamWidget* Face = Box->FaceNode.Get();
	if (!TestNotNull(TEXT("the box has a face"), Face) || !TestNotNull(TEXT("which takes focus"), Face->GetComponent<UUISelectable>()))
	{
		return false;
	}
	if (!FocusForNavigation(*this, Rig, Face))
	{
		return false;
	}

	TestTrue(TEXT("PageDown completes"), Rig.Driver()->Sequence().Key(EKeys::PageDown).Perform());
	Rig.PumpFrames(30);
	const float AfterPage = Box->GetScrollOffset();
	TestTrue(TEXT("PageDown on the focused box scrolls the box itself"), AfterPage > 0.0f);

	// The right stick, as both of its roads deliver it: a per-frame delta to the focused widget.
	TestTrue(TEXT("the right stick on the focused box scrolls it"),
		FDreamUINavigationScroll::ScrollByAnalogAxis(Face, EKeys::Gamepad_RightY, FVector2D(0.0f, 50.0f)));
	Rig.PumpFrames(1);
	TestTrue(TEXT("...further down"), Box->GetScrollOffset() > AfterPage);
	TestTrue(TEXT("End takes it all the way"), FDreamUINavigationScroll::ScrollToExtent(Face, false));
	Rig.PumpFrames(30);
	TestEqual(TEXT("...to the end"), Box->GetScrollOffset(), Box->GetScrollOffsetOfEnd(), 0.5f);
	return true;
}

#endif
