// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamMenuAnchor.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputSubsystem.h"
#include "GameFramework/Actor.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UIDropdown.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * Tab around popups and dialogs: what Tab does in an open dropdown list, in a menu, in a popup that keeps it, and in a
 * dialog -- and whose a dialog's confinement is.
 *
 * A dropdown's list and a menu close on Tab (EDreamPopupTabBehavior::CloseAndContinue), as an HTML select's list does: the
 * list chooses the row the player is on (bTabCommitsHighlightedRow), the whole chain closes, the focus comes back to what
 * opened it, and the Tab goes on from there to the next control -- or, with Shift, the one before. A popup pushed to cycle
 * keeps Tab inside it. A dialog keeps Tab and the pad inside it whenever it is dimmed, whether or not Back closes it, and
 * only for the player it belongs to.
 *
 * The keys go through the rig's input host as a keyboard sends them (FDreamDriverSequence::Tab), so the routing, the Tab
 * walk and the popup layer each take their part in the order a game gives it to them. Focus is read from the input system.
 */
namespace DreamPopupTabTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig, int32 InUserIndex)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(InUserIndex) : nullptr;
	}

	bool IsInside(const UDreamWidget* InWidget, const UDreamWidget* InRoot)
	{
		return IsValid(InWidget) && IsValid(InRoot) && (InWidget == InRoot || InWidget->IsChildOf(InRoot));
	}

	/** Put player InUserIndex's focus on InWidget, the way a directional move would. */
	bool FocusForNavigation(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InWidget, int32 InUserIndex)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return InTest.TestTrue(*FString::Printf(TEXT("Player %d's focus can be put on '%s'"), InUserIndex, *GetNameSafe(InWidget)),
			Services != nullptr && Services->FocusForNavigation(InWidget, InUserIndex));
	}

	/** A button whose face is somewhere focus can go, under InParent (the root when null). */
	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent,
		const FVector2D& InAnchoredPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, FVector2D(160.0, 40.0), InAnchoredPosition);
		if (!InTest.TestNotNull(*FString::Printf(TEXT("A button '%s' can be made"), InName), Button)
			|| !InTest.TestNotNull(*FString::Printf(TEXT("Button '%s' has a face"), InName), Button->FaceNode.Get()))
		{
			return nullptr;
		}
		return Button;
	}

	/**
	 * A dropdown under InParent (the root when null) holding three options with the first selected, its choices and rows
	 * reported to InListener.
	 */
	UDreamDropdown* PlaceDropdown(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener,
		UDreamWidget* InParent, const FVector2D& InAnchoredPosition)
	{
		UDreamDropdown* Dropdown = InRig.MakeControl<UDreamDropdown>(TEXT("Quality"), InParent, FVector2D(200.0, 40.0), InAnchoredPosition);
		if (!InTest.TestNotNull(TEXT("A dropdown can be made on the rig"), Dropdown)
			|| !InTest.TestNotNull(TEXT("It has its behaviour"), Dropdown->DropdownBehaviour.Get())
			|| !InTest.TestNotNull(TEXT("And a face to take focus"), Dropdown->FaceNode.Get()))
		{
			return nullptr;
		}
		TArray<FText> Options;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			Options.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Option %d"), Index)));
		}
		Dropdown->SetOptions(Options);
		Dropdown->SetSelectedIndex(0);
		Dropdown->OnSelectionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleSelectionChanged);
		Dropdown->OnItemGenerated.AddDynamic(InListener, &UDreamPressInteractionListener::HandleItemGenerated);
		return Dropdown;
	}

	/** The row the listener was handed for option InIndex, or null. */
	UDreamWidget* RowOf(const UDreamPressInteractionListener* InListener, int32 InIndex)
	{
		return InListener->GeneratedItems.IsValidIndex(InIndex) ? InListener->GeneratedItems[InIndex].Get() : nullptr;
	}

	/** A button, a dropdown, a button: three controls one after another in the tree, which is the order Tab walks. */
	struct FDropdownBetweenButtons
	{
		UDreamButton* Before = nullptr;
		UDreamDropdown* Dropdown = nullptr;
		UDreamButton* After = nullptr;

		bool IsReady() const { return Before != nullptr && Dropdown != nullptr && After != nullptr; }
	};

	/**
	 * The three controls, and the dropdown's list opened by a click -- the focus on its selected row -- then moved down a
	 * row with the pad, so the row the player is on is not the one already chosen.
	 */
	FDropdownBetweenButtons PlaceAndOpenOnSecondRow(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener)
	{
		FDropdownBetweenButtons Placed;
		Placed.Before = PlaceButton(InTest, InRig, TEXT("Before"), nullptr, FVector2D(-380.0, 200.0));
		Placed.Dropdown = PlaceDropdown(InTest, InRig, InListener, nullptr, FVector2D(0.0, 200.0));
		Placed.After = PlaceButton(InTest, InRig, TEXT("After"), nullptr, FVector2D(380.0, 200.0));
		if (!Placed.IsReady())
		{
			return FDropdownBetweenButtons();
		}
		InRig.PumpFrames(1);
		InTest.TestTrue(TEXT("Clicking the dropdown completes"), InRig.Driver()->Find(FDreamBy::Widget(Placed.Dropdown))->Click());
		InRig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("The click opened the list"), Placed.Dropdown->IsOpen())
			|| !InTest.TestNotNull(TEXT("With a row for the second option"), RowOf(InListener, 1)))
		{
			return FDropdownBetweenButtons();
		}
		InTest.TestTrue(TEXT("Moving down a row completes"), InRig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Down).Perform());
		if (!InTest.TestEqual(TEXT("The player is on the second row"), FocusOf(InRig, 0), RowOf(InListener, 1)))
		{
			return FDropdownBetweenButtons();
		}
		return Placed;
	}

	/** The button in InDialog's row that answers with InResult, or null. */
	UDreamButton* ButtonAnswering(const UDreamDialog* InDialog, FName InResult)
	{
		const TArray<FDreamDialogButton> Specs = InDialog->GetButtons();
		for (int32 Index = 0; Index < Specs.Num(); ++Index)
		{
			if (Specs[Index].Result == InResult && InDialog->ButtonWidgets.IsValidIndex(Index))
			{
				return InDialog->ButtonWidgets[Index].Get();
			}
		}
		return nullptr;
	}

	/**
	 * A second player on the rig's world: an event system for it, a module feeding it, a screen raycaster for it on the
	 * rig's canvas -- the arrangement the popup focus tests use, made the same way.
	 */
	struct FSecondPlayer
	{
		UDreamEventSystem* EventSystem = nullptr;
		UDreamDriverInputModule* Module = nullptr;
		UDreamScreenSpaceRaycaster* Raycaster = nullptr;

		FSecondPlayer(FDreamDriverRig& InRig, int32 InUserIndex)
		{
			AActor* Host = InRig.GetHostActor();
			UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(InRig.GetWorld());
			if (Host == nullptr || Input == nullptr)
			{
				return;
			}
			EventSystem = NewObject<UDreamEventSystem>(Host);
			EventSystem->SetUserIndex(InUserIndex);
			Host->AddInstanceComponent(EventSystem);
			EventSystem->RegisterComponent();
			Input->AddEventSystem(EventSystem);
			Module = NewObject<UDreamDriverInputModule>(Host);
			Host->AddInstanceComponent(Module);
			Module->RegisterComponent();
			Module->RegisterInputModuleToEventSystem(EventSystem);
			Raycaster = NewObject<UDreamScreenSpaceRaycaster>(Host);
			Raycaster->SetUserIndex(InUserIndex);
			Raycaster->SetRootCanvas(InRig.RootCanvas());
			Host->AddInstanceComponent(Raycaster);
			Raycaster->RegisterComponent();
			Raycaster->ActivateRaycaster();
		}

		bool IsUsable() const { return EventSystem != nullptr && Module != nullptr && Raycaster != nullptr; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabDropdownForwardTest,
	"DreamGUI.Dropdown.TabInTheOpenListChoosesTheHighlightedRowClosesItAndMovesToTheNextControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabDropdownForwardTest, "DreamGUI.Dropdown.TabInTheOpenListChoosesTheHighlightedRowClosesItAndMovesToTheNextControl", "[Nav][Animated]")

/*
 * Tab in an open list walked its rows, and stopped at the last: it neither chose nor closed, and the form behind it could not
 * be tabbed through while a list was open. Tab now leaves the list the way it leaves an HTML select's: the row the player is
 * on is chosen, the list closes, the focus comes back to the dropdown and the Tab goes on to the control after it.
 */
bool FDreamPopupTabDropdownForwardTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FDropdownBetweenButtons Placed = PlaceAndOpenOnSecondRow(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());

	if (TestEqual(TEXT("Tab chose an option, once"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("The row the player was on"), Listener->SelectionIndices[0], 1);
	}
	TestEqual(TEXT("Which the dropdown now shows"), Placed.Dropdown->GetSelectedIndex(), 1);
	TestFalse(TEXT("The list closed"), Placed.Dropdown->IsOpen());
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	TestTrue(TEXT("Nothing is left open on the popup layer"), Popups != nullptr && Popups->GetTopPopup(0) == nullptr);
	TestEqual(TEXT("And the Tab went on past the dropdown, to the control after it"), FocusOf(Rig, 0), Placed.After->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabDropdownBackwardTest,
	"DreamGUI.Dropdown.ShiftTabInTheOpenListChoosesTheHighlightedRowClosesItAndMovesToThePreviousControl",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabDropdownBackwardTest, "DreamGUI.Dropdown.ShiftTabInTheOpenListChoosesTheHighlightedRowClosesItAndMovesToThePreviousControl", "[Nav][Animated]")

/* Shift+Tab does the same, backwards: the row is chosen, the list closes, and the focus goes to the control before the dropdown. */
bool FDreamPopupTabDropdownBackwardTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FDropdownBetweenButtons Placed = PlaceAndOpenOnSecondRow(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}

	TestTrue(TEXT("Pressing Shift+Tab completes"), Rig.Driver()->Sequence().ShiftTab().Perform());

	if (TestEqual(TEXT("Shift+Tab chose an option, once"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("The row the player was on"), Listener->SelectionIndices[0], 1);
	}
	TestFalse(TEXT("The list closed"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("And the focus went back past the dropdown, to the control before it"), FocusOf(Rig, 0), Placed.Before->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabDropdownNoCommitTest,
	"DreamGUI.Dropdown.TabWithoutCommittingTheHighlightedRowClosesTheListChoosingNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabDropdownNoCommitTest, "DreamGUI.Dropdown.TabWithoutCommittingTheHighlightedRowClosesTheListChoosingNothing", "[Nav][Animated]")

/* bTabCommitsHighlightedRow off: Tab still never stays in the list -- it closes it and moves on -- but chooses nothing. */
bool FDreamPopupTabDropdownNoCommitTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FDropdownBetweenButtons Placed = PlaceAndOpenOnSecondRow(*this, Rig, Listener.Get());
	if (!Placed.IsReady())
	{
		return false;
	}
	// Turned off with the list already open: the switch is read when the Tab comes.
	Placed.Dropdown->SetTabCommitsHighlightedRow(false);
	TestFalse(TEXT("The switch reaches the behaviour"), Placed.Dropdown->DropdownBehaviour->GetTabCommitsHighlightedRow());

	TestTrue(TEXT("Pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());

	TestEqual(TEXT("Nothing was chosen"), Listener->SelectionIndices.Num(), 0);
	TestEqual(TEXT("The dropdown still shows the first option"), Placed.Dropdown->GetSelectedIndex(), 0);
	TestFalse(TEXT("The list closed"), Placed.Dropdown->IsOpen());
	TestEqual(TEXT("And the Tab went on to the control after the dropdown"), FocusOf(Rig, 0), Placed.After->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabMenuChainTest,
	"DreamGUI.MenuAnchor.TabInAMenuWithItsSubmenuOpenClosesTheWholeChainAndMovesOn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabMenuChainTest, "DreamGUI.MenuAnchor.TabInAMenuWithItsSubmenuOpenClosesTheWholeChainAndMovesOn", "[Nav][Animated]")

/*
 * A menu with a submenu open, the focus in the submenu: Tab closes all of it -- the submenu first, its focus given back to
 * the item it opened from, then the menu, its focus given back to the trigger -- and goes on from the menu's anchor to the
 * control after it, as a Tab out of an application menu leaves the whole menu.
 */
bool FDreamPopupTabMenuChainTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> MenuListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> SubListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The trigger, then the anchor at the same place, then a control after them both.
	UDreamButton* Trigger = PlaceButton(*this, Rig, TEXT("Trigger"), nullptr, FVector2D(-300.0, 150.0));
	UDreamMenuAnchor* Anchor = Trigger != nullptr
		? Rig.MakeControl<UDreamMenuAnchor>(TEXT("Anchor"), nullptr, FVector2D(160.0, 40.0), FVector2D(-300.0, 150.0)) : nullptr;
	if (!TestNotNull(TEXT("A menu anchor can be made on the rig"), Anchor)
		|| !TestNotNull(TEXT("With a menu node to put content in"), Anchor->MenuNode.Get()))
	{
		return false;
	}
	UDreamButton* MenuButton = PlaceButton(*this, Rig, TEXT("MenuButton"), Anchor->MenuNode.Get(), FVector2D::ZeroVector);
	UDreamMenuAnchor* Sub = MenuButton != nullptr
		? Rig.MakeControl<UDreamMenuAnchor>(TEXT("SubAnchor"), Anchor->MenuNode.Get(), FVector2D(160.0, 40.0), FVector2D(0.0, -50.0)) : nullptr;
	if (!TestNotNull(TEXT("A submenu anchor can be put in the menu"), Sub)
		|| !TestNotNull(TEXT("With a menu node of its own"), Sub->MenuNode.Get()))
	{
		return false;
	}
	UDreamButton* SubButton = PlaceButton(*this, Rig, TEXT("SubButton"), Sub->MenuNode.Get(), FVector2D::ZeroVector);
	UDreamButton* After = SubButton != nullptr ? PlaceButton(*this, Rig, TEXT("After"), nullptr, FVector2D(300.0, 150.0)) : nullptr;
	if (After == nullptr)
	{
		return false;
	}
	Anchor->OnMenuOpenChanged.AddDynamic(MenuListener.Get(), &UDreamPressInteractionListener::HandleMenuOpenChanged);
	Sub->OnMenuOpenChanged.AddDynamic(SubListener.Get(), &UDreamPressInteractionListener::HandleMenuOpenChanged);
	Rig.PumpFrames(1);
	if (!FocusForNavigation(*this, Rig, Trigger->FaceNode.Get(), 0))
	{
		return false;
	}
	Anchor->Open(/*bFocusMenu*/true);
	Rig.PumpFrames(1);
	Sub->Open(/*bFocusMenu*/true);
	Rig.PumpFrames(1);
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (!TestEqual(TEXT("The submenu is open with focus on its button"), FocusOf(Rig, 0), SubButton->FaceNode.Get())
		|| !TestTrue(TEXT("Over the menu it came from"), Popups != nullptr && Popups->GetTopPopup(0) == Sub->PopupNode.Get())
		|| !TestTrue(TEXT("Both closing on Tab"), Popups->GetTopPopupTabBehavior(0) == EDreamPopupTabBehavior::CloseAndContinue))
	{
		return false;
	}

	TestTrue(TEXT("Pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());

	TestFalse(TEXT("Tab closed the submenu"), Sub->IsOpen());
	TestFalse(TEXT("And the menu"), Anchor->IsOpen());
	TestNull(TEXT("Nothing is left open on the popup layer"), Popups->GetTopPopup(0));
	TestTrue(TEXT("The submenu's popup is home in the submenu"), Sub->PopupNode->IsChildOf(Sub));
	TestTrue(TEXT("And the menu's in the anchor"), Anchor->PopupNode->IsChildOf(Anchor));
	if (TestEqual(TEXT("The submenu said it opened and closed"), SubListener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("Closed last"), SubListener->MenuOpenStates[1]);
	}
	if (TestEqual(TEXT("And so did the menu"), MenuListener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("Closed last"), MenuListener->MenuOpenStates[1]);
	}
	TestEqual(TEXT("And the Tab went on from the menu's anchor to the control after it"), FocusOf(Rig, 0), After->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabPanelMenuTest,
	"DreamGUI.MenuAnchor.TabInAnOpenPanelMenuClosesItAndMovesPastItsTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabPanelMenuTest, "DreamGUI.MenuAnchor.TabInAnOpenPanelMenuClosesItAndMovesPastItsTrigger", "[Nav][Animated]")

/*
 * The panel spelling of a menu anchor holds its trigger: the anchor content is the panel's first child, the menu its
 * second. Tab in its open menu closes the menu, gives the focus back to the trigger, and goes on past the trigger -- not
 * back onto it, which a walk going on from the panel, the menu's opener, would have done -- to the control after the panel.
 */
bool FDreamPopupTabPanelMenuTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* AnchorWidget = Rig.MakeWidget(TEXT("PanelAnchor"), nullptr, FVector2D(200.0, 40.0), FVector2D(-200.0, 100.0));
	UDreamButton* Face = AnchorWidget != nullptr ? PlaceButton(*this, Rig, TEXT("PanelFace"), AnchorWidget, FVector2D::ZeroVector) : nullptr;
	UDreamWidget* Menu = Face != nullptr ? Rig.MakeWidget(TEXT("PanelMenu"), AnchorWidget, FVector2D(160.0, 100.0)) : nullptr;
	UDreamButton* Item = Menu != nullptr ? PlaceButton(*this, Rig, TEXT("PanelItem"), Menu, FVector2D::ZeroVector) : nullptr;
	UDreamButton* After = Item != nullptr ? PlaceButton(*this, Rig, TEXT("After"), nullptr, FVector2D(300.0, 100.0)) : nullptr;
	UDreamLayoutContainerMenuAnchor* MenuAnchor = After != nullptr ? AnchorWidget->CreateNewLayoutContainer<UDreamLayoutContainerMenuAnchor>() : nullptr;
	if (!TestNotNull(TEXT("A panel menu anchor can be built on the rig"), MenuAnchor))
	{
		return false;
	}
	MenuAnchor->SetUseApplicationMenuStack(true);
	Rig.PumpFrames(1);
	if (!FocusForNavigation(*this, Rig, Face->FaceNode.Get(), 0))
	{
		return false;
	}
	MenuAnchor->SetIsOpen(true);
	Rig.PumpFrames(1);
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("The menu is up on the popup layer"), Popups != nullptr && Popups->IsOpen(Menu))
		|| !TestEqual(TEXT("With the focus on its item"), FocusOf(Rig, 0), Item->FaceNode.Get()))
	{
		MenuAnchor->SetIsOpen(false);
		return false;
	}

	TestTrue(TEXT("Pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());

	TestFalse(TEXT("Tab closed the menu, and the anchor with it"), MenuAnchor->IsOpen());
	TestTrue(TEXT("The menu is home under the anchor"), Menu->GetParent() == AnchorWidget);
	TestEqual(TEXT("And the Tab went on past the trigger, to the control after the panel"), FocusOf(Rig, 0), After->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabCycleTest,
	"DreamGUI.Popup.TabInAPopupThatCyclesGoesRoundItAndLeavesItOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabCycleTest, "DreamGUI.Popup.TabInAPopupThatCyclesGoesRoundItAndLeavesItOpen", "[Nav][Animated]")

/*
 * A popup pushed to cycle -- the default, for a sheet of controls rather than a list -- keeps Tab in front: it goes round
 * the popup's own controls, both ways, and never closes it nor reaches the screen behind. CloseForTab leaves it alone.
 * The pointer rests on the popup's outside-click sheet meanwhile: Tab started from the hover, and the hover on the sheet
 * sent it behind the popup. It starts from the focus.
 */
bool FDreamPopupTabCycleTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Behind = PlaceButton(*this, Rig, TEXT("Behind"), nullptr, FVector2D(0.0, -250.0));
	UDreamWidget* Card = Behind != nullptr ? Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(420.0, 160.0), FVector2D(0.0, 50.0)) : nullptr;
	UDreamButton* First = Card != nullptr ? PlaceButton(*this, Rig, TEXT("First"), Card, FVector2D(-100.0, 0.0)) : nullptr;
	UDreamButton* Second = First != nullptr ? PlaceButton(*this, Rig, TEXT("Second"), Card, FVector2D(100.0, 0.0)) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (Second == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups))
	{
		return false;
	}
	Rig.PumpFrames(1);
	FDreamPopupParams Params;
	Params.Popup = Card;
	Params.UserIndex = 0;
	Params.InitialFocus = First->FaceNode.Get();
	if (!TestTrue(TEXT("The card goes up on the popup layer"), Popups->Push(Params)))
	{
		return false;
	}
	Rig.PumpFrames(1);
	TestTrue(TEXT("Pushed with nothing said, it cycles"), Popups->GetTopPopupTabBehavior(0) == EDreamPopupTabBehavior::Cycle);
	if (!TestEqual(TEXT("The card took the focus onto its first button"), FocusOf(Rig, 0), First->FaceNode.Get()))
	{
		Popups->Dismiss(Card);
		return false;
	}
	// The bottom-right corner, far from the card and from the button behind it: over nothing but the card's sheet.
	TestTrue(TEXT("The card eats the presses outside it, so its sheet is up behind it"), Popups->HasSheets());
	TestTrue(TEXT("Resting the pointer on the sheet completes"), Rig.Driver()->Sequence().MoveToPixel(FVector2D(1200.0, 680.0)).Perform());

	TestTrue(TEXT("Pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestEqual(TEXT("Tab moved to the card's second button"), FocusOf(Rig, 0), Second->FaceNode.Get());
	TestTrue(TEXT("Pressing Tab again completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestEqual(TEXT("And from the last, round to the first -- not out to the screen"), FocusOf(Rig, 0), First->FaceNode.Get());
	TestTrue(TEXT("Pressing Shift+Tab completes"), Rig.Driver()->Sequence().ShiftTab().Perform());
	TestEqual(TEXT("Shift+Tab from the first goes round to the last"), FocusOf(Rig, 0), Second->FaceNode.Get());
	TestTrue(TEXT("The card is still open, in front"), Popups->IsOpen(Card) && Popups->GetTopPopup(0) == Card);
	TestNull(TEXT("A Tab close leaves a card that cycles alone"), Popups->CloseForTab(0));
	TestTrue(TEXT("Which is still open"), Popups->IsOpen(Card));
	Popups->Dismiss(Card);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabCloseForTabTest,
	"DreamGUI.Popup.CloseForTabClosesWhatClosesOnTabCommitsTheRowAndAnswersTheOpener",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabCloseForTabTest, "DreamGUI.Popup.CloseForTabClosesWhatClosesOnTabCommitsTheRowAndAnswersTheOpener", "[Nav][Animated]")

/*
 * The popup layer's half of a Tab, asked directly: a dropdown open inside a card that cycles. CloseForTab closes the
 * player's chain from the top for as long as it closes on Tab -- the dropdown's list, which chooses the row the player is on
 * as it goes, gives the focus back to the dropdown's face, and is answered as the place the Tab goes on from -- and stops at
 * the card, which cycles and stays open around it.
 */
bool FDreamPopupTabCloseForTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(500.0, 300.0), FVector2D(0.0, 0.0));
	UDreamDropdown* Dropdown = Card != nullptr ? PlaceDropdown(*this, Rig, Listener.Get(), Card, FVector2D(0.0, 80.0)) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (Dropdown == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups))
	{
		return false;
	}
	Rig.PumpFrames(1);
	FDreamPopupParams Params;
	Params.Popup = Card;
	Params.UserIndex = 0;
	Params.InitialFocus = Dropdown->FaceNode.Get();
	if (!TestTrue(TEXT("The card goes up on the popup layer"), Popups->Push(Params)))
	{
		return false;
	}
	Rig.PumpFrames(1);
	Dropdown->DropdownBehaviour->Show();
	Rig.PumpFrames(1);
	UDreamWidget* SecondRow = RowOf(Listener.Get(), 1);
	if (!TestTrue(TEXT("The dropdown's list is open over the card"), Dropdown->IsOpen() && Popups->GetTopPopup(0) == Dropdown->ListNode.Get())
		|| !TestNotNull(TEXT("With a second row"), SecondRow)
		|| !FocusForNavigation(*this, Rig, SecondRow, 0))
	{
		Popups->Dismiss(Card);
		return false;
	}
	TestTrue(TEXT("The list closes on Tab"), Popups->GetTopPopupTabBehavior(0) == EDreamPopupTabBehavior::CloseAndContinue);

	UDreamWidget* Opener = Popups->CloseForTab(0);

	TestEqual(TEXT("The Tab goes on from the list's opener, the dropdown's face"), Opener, Dropdown->FaceNode.Get());
	TestFalse(TEXT("The list closed"), Dropdown->IsOpen());
	if (TestEqual(TEXT("Choosing the row the player was on"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("The second"), Listener->SelectionIndices[0], 1);
	}
	TestEqual(TEXT("The focus is back on the face"), FocusOf(Rig, 0), Dropdown->FaceNode.Get());
	TestTrue(TEXT("The card, which cycles, is still open"), Popups->IsOpen(Card) && Popups->GetTopPopup(0) == Card);
	TestTrue(TEXT("And now the player's top popup: Tab cycles there"), Popups->GetTopPopupTabBehavior(0) == EDreamPopupTabBehavior::Cycle);
	TestNull(TEXT("A second Tab close closes nothing"), Popups->CloseForTab(0));
	Popups->Dismiss(Card);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabDialogCycleTest,
	"DreamGUI.Dialog.TabCyclesInsideADimmedDialogThatDoesNotCloseOnBackAndNeverReachesThePageBehind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabDialogCycleTest, "DreamGUI.Dialog.TabCyclesInsideADimmedDialogThatDoesNotCloseOnBackAndNeverReachesThePageBehind", "[Nav][Animated]")

/*
 * A dialog confined navigation only while Back closed it, so one that did not close on Back let Tab and the pad walk out of
 * it onto the page under its dimmer. A dimmed dialog now keeps them in whatever Back does: Tab goes round its two buttons,
 * both ways, and the pad stops at its edge, with the page's controls on both sides of it never reached.
 */
bool FDreamPopupTabDialogCycleTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The page: a control on each side of where the dialog's panel will be, before the dialog in the tree.
	UDreamButton* BehindLeft = PlaceButton(*this, Rig, TEXT("BehindLeft"), nullptr, FVector2D(-520.0, 0.0));
	UDreamButton* BehindRight = BehindLeft != nullptr ? PlaceButton(*this, Rig, TEXT("BehindRight"), nullptr, FVector2D(520.0, 0.0)) : nullptr;
	// The dialog, asleep until it is asked to appear, Back not closing it.
	UDreamWidget* Host = BehindRight != nullptr ? Rig.MakeWidget(TEXT("Host"), nullptr, FVector2D(ViewportSize.X, ViewportSize.Y)) : nullptr;
	if (!TestNotNull(TEXT("A host for the dialog can be made"), Host))
	{
		return false;
	}
	Host->SetWidgetActive(false);
	UDreamDialog* Dialog = Rig.MakeControl<UDreamDialog>(TEXT("Ask"), Host, FVector2D(ViewportSize.X, ViewportSize.Y));
	if (!TestNotNull(TEXT("A dialog can be made under it"), Dialog))
	{
		return false;
	}
	Dialog->SetCloseOnBack(false);
	Rig.PumpFrames(1);
	UDreamButton* Cancel = ButtonAnswering(Dialog, TEXT("Cancel"));
	UDreamButton* Confirm = ButtonAnswering(Dialog, TEXT("Confirm"));
	if (!TestNotNull(TEXT("The dialog has its cancel button"), Cancel) || !TestNotNull(TEXT("And its confirm button"), Confirm)
		|| !TestNotNull(TEXT("And the scope it wears standalone"), Dialog->BackScope.Get()))
	{
		return false;
	}
	TestTrue(TEXT("Dimmed, the dialog confines navigation though Back does not close it"), Dialog->BackScope->GetConfineNavigation());

	Host->SetWidgetActive(true);
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("Woken, the focus is on its default button"), FocusOf(Rig, 0), Confirm->FaceNode.Get()))
	{
		return false;
	}

	TestTrue(TEXT("Pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestEqual(TEXT("Tab from the last button goes round to the first, not out to the page"), FocusOf(Rig, 0), Cancel->FaceNode.Get());
	TestTrue(TEXT("Pressing Tab again completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestEqual(TEXT("And on to the last"), FocusOf(Rig, 0), Confirm->FaceNode.Get());
	TestTrue(TEXT("Pressing Tab a third time completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestEqual(TEXT("And round again"), FocusOf(Rig, 0), Cancel->FaceNode.Get());
	TestTrue(TEXT("Pressing Shift+Tab completes"), Rig.Driver()->Sequence().ShiftTab().Perform());
	TestEqual(TEXT("Shift+Tab from the first goes round to the last"), FocusOf(Rig, 0), Confirm->FaceNode.Get());

	// The pad too: left to the first button, then left again -- where the page's control lies -- stays in the dialog.
	TestTrue(TEXT("Moving left completes"), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Left).Perform());
	TestEqual(TEXT("The pad moves within the dialog"), FocusOf(Rig, 0), Cancel->FaceNode.Get());
	TestTrue(TEXT("Moving left again completes"), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Left).Perform());
	TestTrue(TEXT("And stops at its edge, inside it"), IsInside(FocusOf(Rig, 0), Dialog));
	TestFalse(TEXT("Never reaching the page's control on the left"), FocusOf(Rig, 0) == BehindLeft->FaceNode.Get());
	TestFalse(TEXT("Nor the one on the right"), FocusOf(Rig, 0) == BehindRight->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupTabSecondPlayersDialogTest,
	"DreamGUI.Dialog.ASecondPlayersDialogDoesNotConfineTheFirstPlayer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupTabSecondPlayersDialogTest, "DreamGUI.Dialog.ASecondPlayersDialogDoesNotConfineTheFirstPlayer", "[Nav][Animated]")

/*
 * A standalone dialog's scope never had a player: it was player 0's whoever's dialog it was, so a dialog on the second
 * player's screen held the first player's Tab and answered the first player's Back. The scope now names no player of its
 * own, and so belongs to its widget's owner -- the dialog's owning player.
 *
 * A headless world cannot give a widget an owner whose player index is 1 -- a local player knows its index only through a
 * game viewport -- so the dialog is made the second player's the way that owner would make it: its scope answers 1. The
 * first player then walks their page past it with Tab, and their Back does not cancel it.
 */
bool FDreamPopupTabSecondPlayersDialogTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupTabTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	FSecondPlayer Second(Rig, 1);
	if (!TestTrue(TEXT("A second player can be added"), Second.IsUsable()))
	{
		return false;
	}
	UDreamButton* PageFirst = PlaceButton(*this, Rig, TEXT("PageFirst"), nullptr, FVector2D(-450.0, -250.0));
	UDreamButton* PageSecond = PageFirst != nullptr ? PlaceButton(*this, Rig, TEXT("PageSecond"), nullptr, FVector2D(-200.0, -250.0)) : nullptr;
	UDreamWidget* Host = PageSecond != nullptr ? Rig.MakeWidget(TEXT("Host"), nullptr, FVector2D(ViewportSize.X, ViewportSize.Y)) : nullptr;
	if (!TestNotNull(TEXT("A host for the dialog can be made"), Host))
	{
		return false;
	}
	Host->SetWidgetActive(false);
	UDreamDialog* Dialog = Rig.MakeControl<UDreamDialog>(TEXT("SecondPlayersDialog"), Host, FVector2D(ViewportSize.X, ViewportSize.Y));
	if (!TestNotNull(TEXT("A dialog can be made under it"), Dialog))
	{
		return false;
	}
	Dialog->OnDialogClosed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleDialogClosed);
	Rig.PumpFrames(1);
	UDreamUINavigationScope* Scope = Dialog->BackScope.Get();
	if (!TestNotNull(TEXT("The standalone dialog wears its scope"), Scope))
	{
		return false;
	}
	// The dialog says nothing about whose it is: its scope's own player index is left at -1, the widget's owner.
	const FIntProperty* UserIndexProperty = FindFProperty<FIntProperty>(UDreamUINavigationScope::StaticClass(), TEXT("UserIndex"));
	if (TestNotNull(TEXT("A scope has a player index"), UserIndexProperty))
	{
		TestEqual(TEXT("Which the dialog leaves to the owner"), UserIndexProperty->GetPropertyValue_InContainer(Scope), -1);
	}
	TestEqual(TEXT("So the scope answers the dialog's owning player"), Scope->GetUserIndex(), Dialog->GetOwningPlayerIndex());
	// The second player's dialog, as an owner of index 1 would make it.
	Scope->SetUserIndex(1);
	if (!FocusForNavigation(*this, Rig, PageFirst->FaceNode.Get(), 0))
	{
		return false;
	}

	Host->SetWidgetActive(true);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Appearing, the dialog took the second player's focus"), IsInside(FocusOf(Rig, 1), Dialog));
	TestEqual(TEXT("And left the first player's where it was"), FocusOf(Rig, 0), PageFirst->FaceNode.Get());

	TestTrue(TEXT("The first player pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestEqual(TEXT("Their Tab walks their page, not into the second player's dialog"), FocusOf(Rig, 0), PageSecond->FaceNode.Get());

	TestTrue(TEXT("The first player pressing Back completes"), Rig.Driver()->Sequence().Back().Perform());
	TestEqual(TEXT("Their Back does not cancel the second player's dialog"), Listener->DialogClosedResults.Num(), 0);
	TestTrue(TEXT("Which is still up"), Dialog->GetWidgetActive());
	return true;
}

#endif
