// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamExpandableArea.h"
#include "Controls/DreamMenuAnchor.h"
#include "Controls/DreamTabView.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputSubsystem.h"
#include "GameFramework/Actor.h"
#include "Interaction/DreamUIModal.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UIDropdown.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * Where focus goes when something popup-like closes: a dropdown's list, a menu, a modal, a collapsing
 * body, a tab page.
 *
 * Slate gives focus back to the combo box for every user whose focus is inside its list, and the menu
 * stack remembers the focused widget when a menu opens. DreamGUI only ever cleared focus that could not
 * stay, and recorded nothing about who opened what, so closing a popup left the player's focus nowhere
 * -- or, through a click catcher that took focus and was destroyed, nowhere by a longer road. What is
 * asserted here is the rule the popups now share (FDreamFocusReturn): a player whose focus is inside
 * the closing thing, or went nowhere from it, gets back what opened it -- else what they had focused
 * when it opened, else what their screen asks for -- in one step and with the pad's cursor on it; a
 * player who moved focus elsewhere meanwhile keeps it.
 *
 * Focus is read from the input system, which is where it lives (UDreamUIInputServices), and the pad's
 * cursor from the event system's navigation highlight, which is where the next stick press starts.
 */
namespace DreamPopupFocusTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** The bottom-right corner of the viewport, where nothing but the rig's root is. */
	const FVector2D FarFromEverything(1200.0, 680.0);

	UDreamUIInputServices* ServicesOf(const FDreamDriverRig& InRig)
	{
		return UDreamUIInputServices::Get(InRig.GetWorld());
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig, int32 InUserIndex)
	{
		const UDreamUIInputServices* Services = ServicesOf(InRig);
		return Services != nullptr ? Services->GetFocusedWidget(InUserIndex) : nullptr;
	}

	UDreamWidget* CursorOf(const FDreamDriverRig& InRig)
	{
		return InRig.EventSystem() != nullptr ? InRig.EventSystem()->GetHighlightedComponentForNavigation(0) : nullptr;
	}

	bool IsInside(const UDreamWidget* InWidget, const UDreamWidget* InRoot)
	{
		return IsValid(InWidget) && IsValid(InRoot) && (InWidget == InRoot || InWidget->IsChildOf(InRoot));
	}

	TArray<FText> MakeOptions(int32 InCount)
	{
		TArray<FText> Options;
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			Options.Add(FText::AsCultureInvariant(FString::Printf(TEXT("Option %d"), Index)));
		}
		return Options;
	}

	/**
	 * A dropdown under InParent (the root when null) holding InOptionCount options with the first
	 * selected, its events bound to InListener, laid out. The rows reach the listener as the list is built.
	 */
	UDreamDropdown* PlaceDropdown(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamPressInteractionListener* InListener,
		UDreamWidget* InParent, const FVector2D& InAnchoredPosition, int32 InOptionCount = 3)
	{
		UDreamDropdown* Dropdown = InRig.MakeControl<UDreamDropdown>(TEXT("Quality"), InParent, FVector2D(200.0, 40.0), InAnchoredPosition);
		if (!InTest.TestNotNull(TEXT("A dropdown can be made on the rig"), Dropdown)
			|| !InTest.TestNotNull(TEXT("It has its behaviour"), Dropdown->DropdownBehaviour.Get())
			|| !InTest.TestNotNull(TEXT("And a face to take focus"), Dropdown->FaceNode.Get()))
		{
			return nullptr;
		}
		Dropdown->SetOptions(MakeOptions(InOptionCount));
		Dropdown->SetSelectedIndex(0);
		Dropdown->OnSelectionChanged.AddDynamic(InListener, &UDreamPressInteractionListener::HandleSelectionChanged);
		Dropdown->OnItemGenerated.AddDynamic(InListener, &UDreamPressInteractionListener::HandleItemGenerated);
		InRig.PumpFrames(1);
		return Dropdown;
	}

	/** Click the dropdown open and let the rows it built settle before anything aims at them. */
	bool OpenByClicking(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamDropdown* InDropdown)
	{
		InTest.TestTrue(TEXT("Clicking the dropdown completes"), InRig.Driver()->Find(FDreamBy::Widget(InDropdown))->Click());
		InRig.PumpFrames(2);
		return InTest.TestTrue(TEXT("The click opened the list"), InDropdown->IsOpen());
	}

	/** The row the listener was handed for option InIndex, or null. */
	UDreamWidget* RowOf(const UDreamPressInteractionListener* InListener, int32 InIndex)
	{
		return InListener->GeneratedItems.IsValidIndex(InIndex) ? InListener->GeneratedItems[InIndex].Get() : nullptr;
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

	/** Put player InUserIndex's focus on InWidget, the way a directional move would. */
	bool FocusForNavigation(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InWidget, int32 InUserIndex)
	{
		UDreamUIInputServices* Services = ServicesOf(InRig);
		return InTest.TestTrue(*FString::Printf(TEXT("Player %d's focus can be put on '%s'"), InUserIndex, *GetNameSafe(InWidget)),
			Services != nullptr && Services->FocusForNavigation(InWidget, InUserIndex));
	}

	/**
	 * A second player on the rig's world: an event system for it, a module feeding it, a screen raycaster for it on the
	 * rig's canvas. The input pipeline tests' arrangement, made the same way.
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

	/** A trigger button, and a menu anchor beside it with one button in its menu. */
	struct FPlacedMenu
	{
		UDreamButton* Trigger = nullptr;
		UDreamMenuAnchor* Anchor = nullptr;
		UDreamButton* MenuButton = nullptr;

		bool IsReady() const { return Trigger != nullptr && Anchor != nullptr && MenuButton != nullptr; }
	};

	FPlacedMenu PlaceMenu(FAutomationTestBase& InTest, FDreamDriverRig& InRig)
	{
		FPlacedMenu Placed;
		Placed.Trigger = PlaceButton(InTest, InRig, TEXT("Trigger"), nullptr, FVector2D(-300.0, 150.0));
		Placed.Anchor = InRig.MakeControl<UDreamMenuAnchor>(TEXT("Anchor"), nullptr, FVector2D(160.0, 40.0), FVector2D(-300.0, 150.0));
		if (!InTest.TestNotNull(TEXT("A menu anchor can be made on the rig"), Placed.Anchor)
			|| !InTest.TestNotNull(TEXT("The anchor has a menu node to put content in"), Placed.Anchor->MenuNode.Get()))
		{
			return Placed;
		}
		Placed.MenuButton = PlaceButton(InTest, InRig, TEXT("MenuButton"), Placed.Anchor->MenuNode.Get(), FVector2D::ZeroVector);
		InRig.PumpFrames(1);
		return Placed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusDropdownPadChoiceTest,
	"DreamGUI.Dropdown.ChoosingARowWithThePadPutsFocusBackOnTheFaceAtOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusDropdownPadChoiceTest, "DreamGUI.Dropdown.ChoosingARowWithThePadPutsFocusBackOnTheFaceAtOnce", "[Nav][Animated]")

/*
 * The list fades out over a third of a second after a row is chosen, and is put to sleep only when the
 * fade ends. Until then the chosen row kept the focus -- the confirm and the stick still went to it --
 * and the sleep then cleared it, leaving the player's focus nowhere and the face never given it back.
 * Chosen with the pad here: on the frame of the choice, the focus and the pad's cursor are on the face,
 * and a step taken while the list is still fading does not land in it.
 */
bool FDreamPopupFocusDropdownPadChoiceTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDropdown* Dropdown = PlaceDropdown(*this, Rig, Listener.Get(), nullptr, FVector2D(0.0, 200.0));
	if (Dropdown == nullptr)
	{
		return false;
	}
	UDreamWidget* Face = Dropdown->FaceNode.Get();

	// The first direction lands on the only control there is; the accept button opens it.
	TestTrue(TEXT("Moving onto the dropdown and accepting completes"),
		Rig.Driver()->Sequence()
			.Navigate(EDreamUINavigationDirection::Down)
			.NavigationTrigger(true)
			.NavigationTrigger(false)
			.Perform());
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The accept button opened the list"), Dropdown->IsOpen())
		|| !TestNotNull(TEXT("Opening built a row for the second option"), RowOf(Listener.Get(), 1)))
	{
		return false;
	}
	TestTrue(TEXT("Moving down within the list completes"), Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Down).Perform());
	if (!TestEqual(TEXT("Focus is on the second row"), FocusOf(Rig, 0), RowOf(Listener.Get(), 1)))
	{
		return false;
	}

	TestTrue(TEXT("Accepting completes"), Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).Perform());

	if (TestEqual(TEXT("The second option was chosen"), Listener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("Once, naming it"), Listener->SelectionIndices[0], 1);
	}
	TestFalse(TEXT("Choosing closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("And focus is back on the face on the frame of the choice, not when the fade ends"), FocusOf(Rig, 0), Face);
	TestEqual(TEXT("With the pad's cursor on it too"), CursorOf(Rig), Face);

	TestTrue(TEXT("Stepping down while the list is still fading completes"),
		Rig.Driver()->Sequence().Navigate(EDreamUINavigationDirection::Down).Perform());
	TestFalse(TEXT("The step does not land in the fading list"), IsInside(FocusOf(Rig, 0), Dropdown->ListNode.Get()));
	TestEqual(TEXT("Focus stays on the face, the only control there is"), FocusOf(Rig, 0), Face);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusDropdownBackInDialogTest,
	"DreamGUI.Dropdown.BackClosesTheListAndNotTheDialogAroundIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusDropdownBackInDialogTest, "DreamGUI.Dropdown.BackClosesTheListAndNotTheDialogAroundIt", "[Nav][Animated]")

/*
 * No popup handled Back, so Back walked straight down the screens: a dropdown open inside a dialog had
 * the dialog cancelled under it while the list stayed up. The open popups now hear Back first -- after a
 * field's edit, before any screen -- and only the player's top one closes. Here the list closes, focus
 * goes back to its face, and the dialog around it is still up and was never answered.
 */
bool FDreamPopupFocusDropdownBackInDialogTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDialog* Dialog = Rig.MakeControl<UDreamDialog>(TEXT("Ask"), nullptr, FVector2D(ViewportSize.X, ViewportSize.Y));
	if (!TestNotNull(TEXT("A dialog can be made on the rig"), Dialog)
		|| !TestNotNull(TEXT("It has a body to put a dropdown in"), Dialog->BodyNode.Get()))
	{
		return false;
	}
	Dialog->OnDialogClosed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleDialogClosed);
	UDreamDropdown* Dropdown = PlaceDropdown(*this, Rig, Listener.Get(), Dialog->BodyNode.Get(), FVector2D::ZeroVector);
	if (Dropdown == nullptr)
	{
		return false;
	}
	// The body is awake only while something fills it, which the dialog decides on a style push.
	Dialog->ApplyStyle();
	Rig.PumpFrames(2);

	Dropdown->DropdownBehaviour->Show();
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The list inside the dialog is open"), Dropdown->IsOpen()))
	{
		return false;
	}
	TestTrue(TEXT("With focus in it"), IsInside(FocusOf(Rig, 0), Dropdown->ListNode.Get()));

	TestTrue(TEXT("Pressing Back completes"), Rig.Driver()->Sequence().Back().Perform());

	TestFalse(TEXT("Back closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("And not the dialog around it, which was never answered"), Listener->DialogClosedResults.Num(), 0);
	TestTrue(TEXT("Which is still up"), Dialog->GetWidgetActive());
	TestEqual(TEXT("Focus is back on the dropdown's face"), FocusOf(Rig, 0), Dropdown->FaceNode.Get());

	TestTrue(TEXT("Pressing Back again completes"), Rig.Driver()->Sequence().Back().Perform());
	if (TestEqual(TEXT("With no list open, Back reaches the dialog"), Listener->DialogClosedResults.Num(), 1))
	{
		TestEqual(TEXT("As a cancel"), Listener->DialogClosedResults[0], Dialog->ResolveCancelResult());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusDropdownClickOutsideTest,
	"DreamGUI.Dropdown.ClickingOutsideTheListReturnsFocusToTheFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusDropdownClickOutsideTest, "DreamGUI.Dropdown.ClickingOutsideTheListReturnsFocusToTheFace", "[Pointer][Animated]")

/*
 * The click that closed an open list used to land on a full-screen catcher that was a button: the press
 * gave the catcher the focus, the click destroyed it, and the focus went with it -- nowhere, and the face
 * never got it back. The list opens with focus on its selected row; a click far from it closes it, goes
 * no further, and leaves the focus and the pad's cursor on the face.
 */
bool FDreamPopupFocusDropdownClickOutsideTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamDropdown* Dropdown = PlaceDropdown(*this, Rig, Listener.Get(), nullptr, FVector2D(0.0, 200.0));
	if (Dropdown == nullptr || !OpenByClicking(*this, Rig, Dropdown))
	{
		return false;
	}
	TestEqual(TEXT("The list opened with focus on its selected row"), FocusOf(Rig, 0), RowOf(Listener.Get(), 0));

	TestTrue(TEXT("Clicking far from the list completes"),
		Rig.Driver()->Sequence().MoveToPixel(FarFromEverything).Press().Release().Perform());

	TestFalse(TEXT("The click closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("And chose nothing"), Listener->SelectionIndices.Num(), 0);
	TestEqual(TEXT("Focus is back on the face rather than nowhere"), FocusOf(Rig, 0), Dropdown->FaceNode.Get());
	TestEqual(TEXT("With the pad's cursor on it"), CursorOf(Rig), Dropdown->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusDropdownCollapsedAncestorTest,
	"DreamGUI.Dropdown.CollapsingAnAncestorClosesTheLiftedList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusDropdownCollapsedAncestorTest, "DreamGUI.Dropdown.CollapsingAnAncestorClosesTheLiftedList", "[Pointer][Animated]")

/*
 * The dropdown closed its list only on being disabled or destroyed, which follow activation and not
 * visibility -- so under an ancestor collapsed with the list open, the lifted list, being no longer
 * under the dropdown, stayed on the screen. The popup layer checks every opener after the frame's
 * layout: one no longer drawn takes its popup down, as a hidden SMenuAnchor hides its menu. The list is
 * closed on the next frame, home under the dropdown, asleep.
 */
bool FDreamPopupFocusDropdownCollapsedAncestorTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Holder = Rig.MakeWidget(TEXT("Holder"), nullptr, FVector2D(400.0, 200.0), FVector2D(0.0, 150.0));
	if (!TestNotNull(TEXT("A holder for the dropdown can be made"), Holder))
	{
		return false;
	}
	UDreamDropdown* Dropdown = PlaceDropdown(*this, Rig, Listener.Get(), Holder, FVector2D(0.0, 50.0));
	if (Dropdown == nullptr || !OpenByClicking(*this, Rig, Dropdown))
	{
		return false;
	}
	UDreamWidget* List = Dropdown->ListNode.Get();
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (!TestTrue(TEXT("The open list is lifted out from under the dropdown"), Popups != nullptr && Popups->IsOpen(List) && !List->IsChildOf(Dropdown)))
	{
		return false;
	}

	Holder->SetVisibility(EDreamWidgetVisibility::Collapsed);
	Rig.PumpFrames(1);

	TestFalse(TEXT("Collapsing an ancestor of the dropdown closed its list"), Dropdown->IsOpen());
	TestFalse(TEXT("Which is no longer up on the popup layer"), Popups->IsOpen(List));
	TestTrue(TEXT("It is home under the dropdown"), List->IsChildOf(Dropdown));
	TestFalse(TEXT("And asleep, rather than fading out where nobody can see its dropdown"), List->GetWidgetActive());
	TestFalse(TEXT("Nobody's focus is left in it"), IsInside(FocusOf(Rig, 0), List));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusMenuMovedElsewhereTest,
	"DreamGUI.MenuAnchor.FocusMovedElsewhereWhileOpenIsNotTakenBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusMenuMovedElsewhereTest, "DreamGUI.MenuAnchor.FocusMovedElsewhereWhileOpenIsNotTakenBack", "[Nav][Animated]")

/*
 * A popup gives focus back only to a player whose focus is still in it, or went nowhere from it: a
 * player who has moved focus somewhere else while it was open keeps it there when it closes. A restore
 * that put focus back regardless took the player's choice away -- which is what popping a navigation
 * scope used to do to the screen underneath.
 */
bool FDreamPopupFocusMenuMovedElsewhereTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedMenu Placed = PlaceMenu(*this, Rig);
	UDreamButton* Elsewhere = PlaceButton(*this, Rig, TEXT("Elsewhere"), nullptr, FVector2D(300.0, -200.0));
	if (!Placed.IsReady() || Elsewhere == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!FocusForNavigation(*this, Rig, Placed.Trigger->FaceNode.Get(), 0))
	{
		return false;
	}
	Placed.Anchor->Open(/*bFocusMenu*/true);
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("Opening the menu put focus on its button"), FocusOf(Rig, 0), Placed.MenuButton->FaceNode.Get()))
	{
		return false;
	}

	// The player moves on while the menu is still up.
	if (!FocusForNavigation(*this, Rig, Elsewhere->FaceNode.Get(), 0))
	{
		return false;
	}
	Placed.Anchor->Close();
	Rig.PumpFrames(1);

	TestFalse(TEXT("The menu closed"), Placed.Anchor->IsOpen());
	TestEqual(TEXT("And the focus the player moved elsewhere was not taken back"), FocusOf(Rig, 0), Elsewhere->FaceNode.Get());
	TestEqual(TEXT("Nor the pad's cursor"), CursorOf(Rig), Elsewhere->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusSubmenuFirstTest,
	"DreamGUI.MenuAnchor.ClosingAMenuClosesItsOpenSubmenuFirst",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusSubmenuFirstTest, "DreamGUI.MenuAnchor.ClosingAMenuClosesItsOpenSubmenuFirst", "[Nav][Animated]")

/*
 * A submenu's popup lives on the screen root, not inside the menu it was opened from, so a closing menu
 * never closed it: it stayed up with nothing left to close it. A menu opened from inside an open menu is
 * now its child on the popup layer, and a menu closes its children before itself, as Slate's menu stack
 * does. The order shows in where focus ends up: the submenu gives focus back to the item it was opened
 * from while that item's menu is still up, and the menu then gives it back to the trigger. Closed the
 * other way round, the submenu's focus would have had nowhere left to go.
 */
bool FDreamPopupFocusSubmenuFirstTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> SubListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedMenu Placed = PlaceMenu(*this, Rig);
	if (!Placed.IsReady())
	{
		return false;
	}
	// The submenu's anchor is an item of the menu, below its button; its own menu holds one more button.
	UDreamMenuAnchor* Sub = Rig.MakeControl<UDreamMenuAnchor>(TEXT("SubAnchor"), Placed.Anchor->MenuNode.Get(),
		FVector2D(160.0, 40.0), FVector2D(0.0, -50.0));
	if (!TestNotNull(TEXT("A submenu anchor can be put in the menu"), Sub)
		|| !TestNotNull(TEXT("With a menu node of its own"), Sub->MenuNode.Get()))
	{
		return false;
	}
	UDreamButton* SubButton = PlaceButton(*this, Rig, TEXT("SubButton"), Sub->MenuNode.Get(), FVector2D::ZeroVector);
	if (SubButton == nullptr)
	{
		return false;
	}
	Sub->OnMenuOpenChanged.AddDynamic(SubListener.Get(), &UDreamPressInteractionListener::HandleMenuOpenChanged);
	Rig.PumpFrames(1);
	if (!FocusForNavigation(*this, Rig, Placed.Trigger->FaceNode.Get(), 0))
	{
		return false;
	}

	Placed.Anchor->Open(/*bFocusMenu*/true);
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("The menu opened with focus on its button"), FocusOf(Rig, 0), Placed.MenuButton->FaceNode.Get()))
	{
		return false;
	}
	Sub->Open(/*bFocusMenu*/true);
	Rig.PumpFrames(1);
	TestEqual(TEXT("The submenu opened with focus on its button"), FocusOf(Rig, 0), SubButton->FaceNode.Get());
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	TArray<UDreamWidget*> Open;
	if (Popups != nullptr)
	{
		Popups->GetOpenPopups(0, Open);
	}
	if (!TestEqual(TEXT("Both are up, the submenu over the menu it came from"), Open.Num(), 2)
		|| !TestEqual(TEXT("The submenu on top"), Popups->GetTopPopup(0), Sub->PopupNode.Get()))
	{
		return false;
	}

	Placed.Anchor->Close();
	Rig.PumpFrames(1);

	TestFalse(TEXT("Closing the menu closed its submenu"), Sub->IsOpen());
	if (TestEqual(TEXT("Which said so"), SubListener->MenuOpenStates.Num(), 2))
	{
		TestFalse(TEXT("As closed"), SubListener->MenuOpenStates[1]);
	}
	TestFalse(TEXT("And the menu itself"), Placed.Anchor->IsOpen());
	TestNull(TEXT("Nothing is left open on the popup layer"), Popups->GetTopPopup(0));
	TestTrue(TEXT("The submenu's popup is home in the submenu"), Sub->PopupNode->IsChildOf(Sub));
	TestEqual(TEXT("Focus went back through the menu to the trigger, which closing the submenu first is what allows"),
		FocusOf(Rig, 0), Placed.Trigger->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusModalOverBarePageTest,
	"DreamGUI.Modal.ClosingAModalOverAPageWithoutAScopeReturnsFocusToItsOpener",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusModalOverBarePageTest, "DreamGUI.Modal.ClosingAModalOverAPageWithoutAScopeReturnsFocusToItsOpener", "[Nav][Animated]")

/*
 * Popping a modal's navigation scope gave focus only to a scope underneath, and a page without one had
 * nothing to give it to: the focus on the dialog's button went with the dialog, and the player was left
 * with none. The stack now notes, as the modal's scope is pushed, what had focus -- the button on the
 * page that opened it -- and gives it back when nothing underneath asks for focus.
 */
bool FDreamPopupFocusModalOverBarePageTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(Rig.GetWorld());
	UDreamButton* Opener = PlaceButton(*this, Rig, TEXT("Opener"), nullptr, FVector2D(-300.0, 0.0));
	if (!TestNotNull(TEXT("The rig's world has the modal subsystem"), Modals) || Opener == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!FocusForNavigation(*this, Rig, Opener->FaceNode.Get(), 0))
	{
		return false;
	}

	Modals->ShowModalNative(UDreamDialog::StaticClass(), [](FName) {}, 0);
	Rig.PumpFrames(2);
	UDreamUserWidget* Dialog = Modals->GetActiveModalWidget(0);
	if (!TestNotNull(TEXT("The modal opened"), Dialog))
	{
		return false;
	}
	TestTrue(TEXT("And took the focus into its dialog"), IsInside(FocusOf(Rig, 0), Dialog));

	Modals->CloseTopModal(TEXT("Cancel"), 0);
	Rig.PumpFrames(1);

	TestFalse(TEXT("The modal is closed"), Modals->IsModalActive(0));
	TestEqual(TEXT("And focus is back on the button that opened it, not nowhere"), FocusOf(Rig, 0), Opener->FaceNode.Get());
	TestEqual(TEXT("With the pad's cursor on it"), CursorOf(Rig), Opener->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusBothPlayersTest,
	"DreamGUI.Focus.BothPlayersInsideAPopupGetTheOpenerBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusBothPlayersTest, "DreamGUI.Focus.BothPlayersInsideAPopupGetTheOpenerBack", "[Pointer][Nav][Animated]")

/*
 * Focus is every player's, and so is giving it back: SComboBox returns focus to itself for every user
 * whose focus is inside its list. Players were counted from the game instance's local players, which
 * leaves a script-driven player -- this rig's second -- out of every release and every restore. Both
 * players' focus is in the open list here when the first chooses a row: both get the face back.
 */
bool FDreamPopupFocusBothPlayersTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
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
	TArray<int32> Players;
	if (const UDreamUIInputServices* Services = ServicesOf(Rig))
	{
		Services->GetUserIndices(Players);
	}
	TestTrue(TEXT("The input system counts the second player among its players"), Players.Contains(1));

	UDreamDropdown* Dropdown = PlaceDropdown(*this, Rig, Listener.Get(), nullptr, FVector2D(0.0, 200.0));
	if (Dropdown == nullptr || !OpenByClicking(*this, Rig, Dropdown)
		|| !TestNotNull(TEXT("The list has a third row"), RowOf(Listener.Get(), 2)))
	{
		return false;
	}
	if (!FocusForNavigation(*this, Rig, RowOf(Listener.Get(), 1), 1))
	{
		return false;
	}
	TestTrue(TEXT("The first player's focus is in the list"), IsInside(FocusOf(Rig, 0), Dropdown->ListNode.Get()));
	TestTrue(TEXT("And so is the second's"), IsInside(FocusOf(Rig, 1), Dropdown->ListNode.Get()));

	TestTrue(TEXT("The first player clicking the third row completes"), Rig.Driver()->Find(FDreamBy::Widget(RowOf(Listener.Get(), 2)))->Click());

	TestFalse(TEXT("Choosing closed the list"), Dropdown->IsOpen());
	TestEqual(TEXT("The first player's focus is back on the face"), FocusOf(Rig, 0), Dropdown->FaceNode.Get());
	TestEqual(TEXT("And so is the second player's"), FocusOf(Rig, 1), Dropdown->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusTwoPlayersListsTest,
	"DreamGUI.Dropdown.AnotherPlayersOpenListLeavesTheFirstPlayerChoosingFromTheirOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusTwoPlayersListsTest, "DreamGUI.Dropdown.AnotherPlayersOpenListLeavesTheFirstPlayerChoosingFromTheirOwn", "[Pointer][Animated]")

/*
 * An open list puts an invisible sheet behind itself that keeps the presses and the hover outside the list off what
 * is behind it -- for the player whose list it is. Two players share this screen root, and each list goes up over
 * everything on it, sheet and all: the second player's sheet lay over the first player's open list, so the first
 * player's click on their own row landed on it, read as a click outside, and closed their list with nothing chosen;
 * and the first player's sheet kept the second player from opening a list at all. A sheet now stops its own player's
 * pointer only. Here both players open a list, and the first then chooses from theirs.
 */
bool FDreamPopupFocusTwoPlayersListsTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> FirstListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> SecondListener(NewObject<UDreamPressInteractionListener>());
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
	// Side by side, so neither list hangs over the other dropdown or the other list.
	UDreamDropdown* FirstDropdown = PlaceDropdown(*this, Rig, FirstListener.Get(), nullptr, FVector2D(-300.0, 200.0));
	UDreamDropdown* SecondDropdown = PlaceDropdown(*this, Rig, SecondListener.Get(), nullptr, FVector2D(300.0, 200.0));
	if (FirstDropdown == nullptr || SecondDropdown == nullptr || !OpenByClicking(*this, Rig, FirstDropdown))
	{
		return false;
	}

	// The second player clicks their dropdown open, under the first player's sheet.
	const TOptional<FVector2D> SecondFace = FDreamDriverProjection::WidgetCentrePixel(SecondDropdown->FaceNode.Get());
	if (!TestTrue(TEXT("The second dropdown's face is on screen"), SecondFace.IsSet()))
	{
		return false;
	}
	Second.Module->MoveTo(SecondFace.GetValue());
	Rig.PumpFrames(1);
	Second.Module->Press();
	Rig.PumpFrames(1);
	Second.Module->Release();
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The second player's click opened their list"), SecondDropdown->IsOpen())
		|| !TestTrue(TEXT("Without closing the first player's"), FirstDropdown->IsOpen()))
	{
		return false;
	}
	const UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	TestTrue(TEXT("The second list is up for the second player"),
		Popups != nullptr && Popups->GetTopPopup(1) == SecondDropdown->ListNode.Get());

	// The first player chooses from their own list, which the second player's sheet now lies over.
	UDreamWidget* ThirdRow = RowOf(FirstListener.Get(), 2);
	if (!TestNotNull(TEXT("The first list has a third row"), ThirdRow))
	{
		return false;
	}
	TestTrue(TEXT("The first player clicking their third row completes"), Rig.Driver()->Find(FDreamBy::Widget(ThirdRow))->Click());

	if (TestEqual(TEXT("The click chose from the first player's list"), FirstListener->SelectionIndices.Num(), 1))
	{
		TestEqual(TEXT("The row that was clicked"), FirstListener->SelectionIndices[0], 2);
	}
	TestFalse(TEXT("Which closed the first player's list"), FirstDropdown->IsOpen());
	TestTrue(TEXT("And left the second player's open"), SecondDropdown->IsOpen());
	TestEqual(TEXT("With nothing chosen from it"), SecondListener->SelectionIndices.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusExpanderCollapseTest,
	"DreamGUI.ExpandableArea.CollapsingWithFocusInsideMovesItToTheHeader",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusExpanderCollapseTest, "DreamGUI.ExpandableArea.CollapsingWithFocusInsideMovesItToTheHeader", "[Nav][Animated]")

/*
 * Collapsing put the body to sleep, which cleared any focus inside it and left it nowhere. The focus a
 * player had in the body now moves to the header that collapsed it, with the pad's cursor, so the next
 * stick press starts from something the player can see.
 */
bool FDreamPopupFocusExpanderCollapseTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamExpandableArea* Area = Rig.MakeControl<UDreamExpandableArea>(TEXT("Advanced"), nullptr, FVector2D(300.0, 200.0));
	if (!TestNotNull(TEXT("An expandable area can be made on the rig"), Area)
		|| !TestNotNull(TEXT("It has a header"), Area->HeaderNode.Get())
		|| !TestNotNull(TEXT("And a content column"), Area->ContentNode.Get()))
	{
		return false;
	}
	UDreamButton* Inside = PlaceButton(*this, Rig, TEXT("Inside"), Area->ContentNode.Get(), FVector2D::ZeroVector);
	if (Inside == nullptr)
	{
		return false;
	}
	// No animation to wait out: the collapse is the question, not its curve.
	Area->SetExpansionDuration(0.0f);
	Area->SetIsExpanded(true);
	Rig.PumpFrames(2);
	if (!FocusForNavigation(*this, Rig, Inside->FaceNode.Get(), 0))
	{
		return false;
	}

	Area->SetIsExpanded(false);
	Rig.PumpFrames(2);

	TestFalse(TEXT("The area collapsed"), Area->GetIsExpanded());
	TestEqual(TEXT("And the focus that was in its body is on the header"), FocusOf(Rig, 0), Area->HeaderNode.Get());
	TestEqual(TEXT("With the pad's cursor on it"), CursorOf(Rig), Area->HeaderNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusTabSwitchTest,
	"DreamGUI.TabView.SwitchingTabsWithFocusPageOnTabChangeFocusesTheNewPage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusTabSwitchTest, "DreamGUI.TabView.SwitchingTabsWithFocusPageOnTabChangeFocusesTheNewPage", "[Pointer][Nav][Animated]")

/*
 * bFocusPageOnTabChange moved focus into the page the player opened -- in principle. The switcher
 * shows its new page only at its next arrange, so the page focus was searched while it was still
 * collapsed, nothing in it could be found, and focus stayed on the tab; the next arrange then took any
 * focus left in the old page with it. Now the page is laid out first, and focus and the pad's cursor
 * land on its first control.
 */
bool FDreamPopupFocusTabSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamTabView* TabView = Rig.MakeControl<UDreamTabView>(TEXT("Settings"), nullptr, FVector2D(600.0, 300.0));
	UDreamWidget* PageA = Rig.MakeWidget(TEXT("PageA"), nullptr, FVector2D(600.0, 250.0));
	UDreamWidget* PageB = Rig.MakeWidget(TEXT("PageB"), nullptr, FVector2D(600.0, 250.0));
	if (!TestNotNull(TEXT("A tab view can be made on the rig"), TabView)
		|| !TestNotNull(TEXT("With a first page"), PageA) || !TestNotNull(TEXT("And a second"), PageB))
	{
		return false;
	}
	UDreamButton* OnA = PlaceButton(*this, Rig, TEXT("OnA"), PageA, FVector2D::ZeroVector);
	UDreamButton* OnB = PlaceButton(*this, Rig, TEXT("OnB"), PageB, FVector2D::ZeroVector);
	if (OnA == nullptr || OnB == nullptr)
	{
		return false;
	}
	TabView->AddPage(PageA);
	TabView->AddPage(PageB);
	TabView->SetFocusPageOnTabChange(true);
	TabView->SetActiveTabIndex(0);
	Rig.PumpFrames(2);
	if (!TestEqual(TEXT("The strip has a tab per page"), TabView->Tabs.Num(), 2))
	{
		return false;
	}

	TestTrue(TEXT("Clicking the second tab completes"), Rig.Driver()->Find(FDreamBy::Widget(TabView->Tabs[1].TabNode.Get()))->Click());
	Rig.PumpFrames(2);

	TestEqual(TEXT("The second tab is open"), TabView->GetActiveTabIndex(), 1);
	TestEqual(TEXT("And focus went into its page"), FocusOf(Rig, 0), OnB->FaceNode.Get());
	TestEqual(TEXT("With the pad's cursor on it"), CursorOf(Rig), OnB->FaceNode.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupFocusMenuReturnTest,
	"DreamGUI.MenuAnchor.ClosingTheMenuReturnsFocusToItsOpener",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupFocusMenuReturnTest, "DreamGUI.MenuAnchor.ClosingTheMenuReturnsFocusToItsOpener", "[Nav][Animated]")

/*
 * Close put the menu away and that was all: focus that Open(true) had moved into the menu was cleared
 * with it, and the player was left with none. The anchor itself is nothing focus sits on, so what
 * opened the menu, as far as focus goes, is what had it when the menu opened -- the trigger -- and that
 * is where it comes back to, with the pad's cursor.
 */
bool FDreamPopupFocusMenuReturnTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupFocusTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedMenu Placed = PlaceMenu(*this, Rig);
	if (!Placed.IsReady() || !FocusForNavigation(*this, Rig, Placed.Trigger->FaceNode.Get(), 0))
	{
		return false;
	}

	Placed.Anchor->Open(/*bFocusMenu*/true);
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("Opening the menu put focus on its button"), FocusOf(Rig, 0), Placed.MenuButton->FaceNode.Get()))
	{
		return false;
	}
	TestEqual(TEXT("With the pad's cursor on it"), CursorOf(Rig), Placed.MenuButton->FaceNode.Get());

	Placed.Anchor->Close();
	Rig.PumpFrames(1);

	TestFalse(TEXT("The menu closed"), Placed.Anchor->IsOpen());
	TestEqual(TEXT("And focus is back on the trigger that had it"), FocusOf(Rig, 0), Placed.Trigger->FaceNode.Get());
	TestEqual(TEXT("With the pad's cursor on it"), CursorOf(Rig), Placed.Trigger->FaceNode.Get());
	return true;
}

#endif
