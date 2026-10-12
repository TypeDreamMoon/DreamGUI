// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamMenuAnchor.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamControlsWorldSpaceInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamMenuAnchor on a panel in the level, in UMG's arrangement: the menu opens from a trigger button's click handler that
 * asks ShouldOpenDueToClick first (the listener's HandleTriggerClicked).
 *
 * In a UWidgetComponent a menu stays in the component's own window: the virtual window asks for popups to go up in the
 * current window (SVirtualWindow::OnQueryPopupMethod answers UseCurrentWindow, Slate/Private/Widgets/SVirtualWindow.cpp:
 * 35-38), and the menu stack hosts them in that window's popup layer (FMenuStack::SetHostPath, Slate/Private/Framework/
 * Application/MenuStack.cpp:680-712). So the menu is drawn on the panel where its placement puts it against the anchor
 * ("Place the menu immediately below the anchor", MenuPlacement_BelowAnchor, SlateCore/Public/Types/SlateEnums.h:215) --
 * this control hanging it its style's Offset lower, two units by default -- above the panel's content, and the interaction
 * component's ray reaches its entries where the camera sees them. A press anywhere outside every menu -- elsewhere on the
 * panel, on the screen -- dismisses it (FMenuStack::OnMenuContentLostFocus) and, with DreamGUI's default, goes on to what it
 * landed on. The pad presses the trigger with Accept, walks the menu and presses an entry with Accept, and Back dismisses
 * the top menu with the focus back where it was.
 */
namespace DreamMenuAnchorWorldTestLocal
{
	const FVector2D TriggerSize(160.0, 40.0);
	const FVector2D MenuSize(200.0, 150.0);
	const FVector2D EntrySize(180.0, 40.0);

	struct FPlacedMenu
	{
		UDreamButton* Trigger = nullptr;
		UDreamMenuAnchor* Anchor = nullptr;
		/** The three entries, top to bottom, each a button. */
		TArray<UDreamButton*> Entries;
		TStrongObjectPtr<UDreamPressInteractionListener> Listener;
		TArray<TStrongObjectPtr<UDreamPressInteractionListener>> EntryClicks;

		bool IsReady() const
		{
			return Trigger != nullptr && Trigger->FaceNode != nullptr && Anchor != nullptr && Anchor->PopupNode != nullptr
				&& Anchor->MenuNode != nullptr && Entries.Num() == 3 && !Entries.Contains(nullptr);
		}

		int32 ClicksOf(int32 InEntry) const { return EntryClicks.IsValidIndex(InEntry) ? EntryClicks[InEntry]->ClickedCount : -1; }
	};

	/**
	 * A trigger button and a menu anchor over it under InParent at InPosition (its units, Y up), the menu opening below it
	 * at MenuSize, with three button entries in a column inside it, each one's clicks counted.
	 */
	FPlacedMenu PlaceMenu(FDreamDriverRig& InRig, UDreamWidget* InParent, const FVector2D& InPosition)
	{
		FPlacedMenu Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		Placed.Trigger = InRig.MakeControl<UDreamButton>(TEXT("Trigger"), InParent, TriggerSize, InPosition);
		Placed.Anchor = InRig.MakeControl<UDreamMenuAnchor>(TEXT("Anchor"), InParent, TriggerSize, InPosition);
		if (Placed.Trigger == nullptr || Placed.Anchor == nullptr || Placed.Anchor->MenuNode == nullptr)
		{
			return Placed;
		}
		Placed.Anchor->SetPlacement(EDreamMenuPlacement::BelowAnchor);
		Placed.Anchor->SetMenuSize(MenuSize);
		UDreamWidget* Column = InRig.MakeWidget(TEXT("Column"), Placed.Anchor->MenuNode.Get(), MenuSize);
		for (int32 Index = 0; Index < 3; ++Index)
		{
			UDreamButton* Entry = Column != nullptr
				? InRig.MakeControl<UDreamButton>(FString::Printf(TEXT("Entry%d"), Index), Column, EntrySize, FVector2D(0.0, 50.0 - 50.0 * Index))
				: nullptr;
			TStrongObjectPtr<UDreamPressInteractionListener> Clicks(NewObject<UDreamPressInteractionListener>());
			if (Entry != nullptr && Entry->FaceNode != nullptr)
			{
				Entry->OnClicked.AddDynamic(Clicks.Get(), &UDreamPressInteractionListener::HandleClicked);
			}
			Placed.Entries.Add(Entry != nullptr && Entry->FaceNode != nullptr ? Entry : nullptr);
			Placed.EntryClicks.Add(MoveTemp(Clicks));
		}
		Placed.Listener->MenuAnchorToOpen = Placed.Anchor;
		Placed.Trigger->OnClicked.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleTriggerClicked);
		Placed.Anchor->OnMenuOpenChanged.AddDynamic(Placed.Listener.Get(), &UDreamPressInteractionListener::HandleMenuOpenChanged);
		if (InRig.EventSystem() != nullptr)
		{
			InRig.EventSystem()->SetDoubleClickTime(0.0f);
		}
		InRig.PumpFrames(2);
		return Placed;
	}

	/** Pumped until the open menu has faded all the way in, for at most a second: past any menu's fade. */
	bool WaitForMenuShown(FDreamDriverRig& InRig, const FPlacedMenu& InPlaced)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(1.0);
		const UDreamMenuAnchor* Anchor = InPlaced.Anchor;
		return InRig.Driver()->Wait(FDreamUntil::Condition([Anchor]()
		{
			return Anchor->IsOpen() && Anchor->PopupNode->GetRenderOpacity() >= 1.0f;
		}, Timeout), Timeout, TEXT("the menu open and faded in"));
	}

	/**
	 * Whether the open menu is up on InPanel -- its child, drawn by its root canvas, in a canvas sorted above the trigger's --
	 * and not on the player's screen; and whether the camera sees it below the anchor: its top the style's Offset below the
	 * anchor's bottom edge, its left on the anchor's left, at MenuSize in panel units, each to within a pixel and a half.
	 * OutWhere says where all of it was.
	 */
	bool MenuIsUpBelowTheAnchorAsSeen(const FDreamDriverRig& InRig, const FPlacedMenu& InPlaced, const UDreamWidget* InPanel, FString& OutWhere)
	{
		const UDreamWidget* Menu = InPlaced.Anchor->PopupNode.Get();
		const UDreamCanvas* MenuCanvas = Menu->GetRenderCanvas();
		const UDreamCanvas* TriggerCanvas = InPlaced.Trigger->FaceNode->GetRenderCanvas();
		const TOptional<FBox2D> AnchorRect = DreamControlsWorldTest::SeenRect(InRig, InPlaced.Anchor);
		const TOptional<FBox2D> MenuRect = DreamControlsWorldTest::SeenRect(InRig, Menu);
		if (MenuCanvas == nullptr || TriggerCanvas == nullptr || !AnchorRect.IsSet() || !MenuRect.IsSet())
		{
			OutWhere = TEXT("the menu or the trigger has no canvas, or the camera does not see the anchor or the menu");
			return false;
		}
		const double Gap = static_cast<double>(InPlaced.Anchor->GetStyle().Offset) * DreamControlsWorldTest::PixelsPerUnit();
		OutWhere = FString::Printf(TEXT("the menu under '%s' at sort order %d, the trigger at %d; the anchor seen at %s, the menu at %s, %.1f pixels of gap expected"),
			Menu->GetParent() != nullptr ? *Menu->GetParent()->GetDisplayName() : TEXT("nothing"),
			MenuCanvas->GetActualSortOrder(), TriggerCanvas->GetActualSortOrder(),
			*DreamControlsConfigTest::Describe(AnchorRect.GetValue()), *DreamControlsConfigTest::Describe(MenuRect.GetValue()), Gap);
		return Menu->GetParent() == InPanel
			&& Menu->GetRootCanvas() == InPanel->GetRootCanvas()
			&& !Menu->IsScreenSpaceOverlayUI()
			&& MenuCanvas->GetActualSortOrder() > TriggerCanvas->GetActualSortOrder()
			&& FMath::Abs(MenuRect->Min.Y - (AnchorRect->Max.Y + Gap)) <= 1.5
			&& FMath::Abs(MenuRect->Min.X - AnchorRect->Min.X) <= 1.5
			&& DreamControlsWorldTest::IsSeenAtItsSize(MenuRect.GetValue(), MenuSize);
	}

	/** A pixel on InRect's horizontal middle line, InInset pixels in from its right edge: clear of a rounded face's corners. */
	FVector2D InsideRightMiddle(const FBox2D& InRect, double InInset)
	{
		return FVector2D(InRect.Max.X - InInset, InRect.GetCenter().Y);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorWorldPressTest,
	"DreamGUI.MenuAnchor.OnAWorldPanelTheMenuOpensOnThePanelBelowTheAnchorAsSeenAndIsPressedAndDismissedWhereItIsSeen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorWorldPressTest, "DreamGUI.MenuAnchor.OnAWorldPanelTheMenuOpensOnThePanelBelowTheAnchorAsSeenAndIsPressedAndDismissedWhereItIsSeen", "[Pointer][Touch][World]")

/*
 * A trigger and its menu anchor on a panel three metres ahead, and a button on the player's screen, low and to the right. A
 * click just inside the trigger's right edge opens the menu: it is up on the panel, above the trigger, and the camera sees it
 * hang below the anchor. A click just inside the second entry's right edge presses that entry, and a click four pixels below
 * the menu, on the bare panel, closes it. A tap on the trigger opens it again, a tap just inside the third entry's left edge
 * presses that entry, and a tap on the screen's button closes the menu and goes on to click that button.
 */
bool FDreamMenuAnchorWorldPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorWorldTestLocal;
	using namespace DreamControlsConfigTest;
	TStrongObjectPtr<UDreamPressInteractionListener> ScreenClicks(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const DreamControlsWorldTest::FWorldStage Stage = DreamControlsWorldTest::SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	UDreamButton* OnScreen = Rig.MakeControl<UDreamButton>(TEXT("OnScreen"), nullptr, FVector2D(160.0, 50.0), FVector2D(480.0, -300.0));
	if (!TestTrue(TEXT("A button on the player's screen"), OnScreen != nullptr && OnScreen->FaceNode != nullptr))
	{
		return false;
	}
	OnScreen->OnClicked.AddDynamic(ScreenClicks.Get(), &UDreamPressInteractionListener::HandleClicked);
	const FPlacedMenu Placed = PlaceMenu(Rig, Stage.Panel, FVector2D(-120.0, 110.0));
	if (!TestTrue(TEXT("A trigger and a menu anchor with three entries can be made on the panel"), Placed.IsReady()))
	{
		return false;
	}
	const TOptional<FBox2D> Trigger = DreamControlsWorldTest::SeenRect(Rig, Placed.Trigger->FaceNode.Get());
	const TOptional<FBox2D> ScreenButton = DrawnRect(OnScreen->FaceNode.Get());
	if (!TestTrue(TEXT("The camera sees the trigger, and the screen's button is on the viewport"), Trigger.IsSet() && ScreenButton.IsSet()))
	{
		return false;
	}

	TestTrue(TEXT("A click just inside the trigger's right edge completes"), ClickAt(Rig, InsideRightMiddle(Trigger.GetValue(), 3.0)));
	if (!TestTrue(TEXT("It opened the menu"), WaitForMenuShown(Rig, Placed)))
	{
		return false;
	}
	FString Where;
	TestTrue(TEXT("The menu is up on the panel above the trigger, not on the screen, and the camera sees it hang below the anchor"),
		MenuIsUpBelowTheAnchorAsSeen(Rig, Placed, Stage.Panel, Where));
	AddInfo(Where);
	const TOptional<FBox2D> Second = DreamControlsWorldTest::SeenRect(Rig, Placed.Entries[1]->FaceNode.Get());
	const TOptional<FBox2D> Menu = DreamControlsWorldTest::SeenRect(Rig, Placed.Anchor->PopupNode.Get());
	if (!TestTrue(TEXT("The camera sees the second entry and the menu"), Second.IsSet() && Menu.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A click just inside the second entry's right edge completes"), ClickAt(Rig, InsideRightMiddle(Second.GetValue(), 3.0)));
	TestEqual(TEXT("It pressed the second entry"), Placed.ClicksOf(1), 1);
	TestTrue(TEXT("...and the menu is still open"), Placed.Anchor->IsOpen());
	TestTrue(TEXT("A click on the bare panel just below the menu completes"), ClickAt(Rig, PastBottomEdge(Menu.GetValue(), 4.0)));
	TestFalse(TEXT("It closed the menu"), Placed.Anchor->IsOpen());
	Rig.PumpFrames(2);

	TestTrue(TEXT("A tap on the trigger completes"), TapAt(Rig, Trigger->GetCenter()));
	if (!TestTrue(TEXT("It opened the menu again"), WaitForMenuShown(Rig, Placed)))
	{
		return false;
	}
	TestTrue(TEXT("Opened by a tap, the menu is up on the panel below the anchor as seen"), MenuIsUpBelowTheAnchorAsSeen(Rig, Placed, Stage.Panel, Where));
	AddInfo(Where);
	const TOptional<FBox2D> Third = DreamControlsWorldTest::SeenRect(Rig, Placed.Entries[2]->FaceNode.Get());
	if (!TestTrue(TEXT("The camera sees the third entry"), Third.IsSet()))
	{
		return false;
	}
	TestTrue(TEXT("A tap just inside the third entry's left edge completes"), TapAt(Rig, FVector2D(Third->Min.X + 3.0, Third->GetCenter().Y)));
	TestEqual(TEXT("It pressed the third entry"), Placed.ClicksOf(2), 1);
	TestTrue(TEXT("A tap on the button on the screen completes"), TapAt(Rig, ScreenButton->GetCenter()));
	TestFalse(TEXT("The tap on the screen closed the menu on the panel"), Placed.Anchor->IsOpen());
	TestEqual(TEXT("...and went on to click the button it landed on"), ScreenClicks->ClickedCount, 1);
	TestEqual(TEXT("The first entry was never pressed"), Placed.ClicksOf(0), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorWorldPadTest,
	"DreamGUI.MenuAnchor.OnAWorldPanelThePadOpensTheMenuOnThePanelWalksItWithTheRingOnEachEntryAsSeenAndBackReturnsToTheTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorWorldPadTest, "DreamGUI.MenuAnchor.OnAWorldPanelThePadOpensTheMenuOnThePanelWalksItWithTheRingOnEachEntryAsSeenAndBackReturnsToTheTrigger", "[Nav][World]")

/*
 * The focus on the trigger on a panel. The pad's Accept presses it, and its click opens the menu up on the panel, hanging
 * below the anchor as the camera sees it; Back closes it, the focus back on the trigger. Then the menu opened with its focus,
 * as the screen version opens it: the focus is on the first entry; D-pad down takes it to the second, the ring sits on the
 * second where the camera sees it on the menu, and Accept presses the second. Back closes the menu and the focus is back on
 * the trigger.
 */
bool FDreamMenuAnchorWorldPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorWorldTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(DreamControlsWorldTest::ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const DreamControlsWorldTest::FWorldStage Stage = DreamControlsWorldTest::SetUpPanel(*this, Rig);
	if (!Stage.IsReady())
	{
		return false;
	}
	const FPlacedMenu Placed = PlaceMenu(Rig, Stage.Panel, FVector2D(-120.0, 110.0));
	if (!TestTrue(TEXT("A trigger and a menu anchor with three entries can be made on the panel"), Placed.IsReady())
		|| !TestTrue(TEXT("The focus goes onto the trigger"), PutFocusOn(Rig, Placed.Trigger->FaceNode.Get())))
	{
		return false;
	}

	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	if (!TestTrue(TEXT("Accept pressed the trigger, which opened the menu"), WaitForMenuShown(Rig, Placed)))
	{
		return false;
	}
	TestEqual(TEXT("...one press of the trigger"), Placed.Listener->TriggerClickedCount, 1);
	FString Where;
	TestTrue(TEXT("The menu is up on the panel above the trigger, not on the screen, and the camera sees it hang below the anchor"),
		MenuIsUpBelowTheAnchorAsSeen(Rig, Placed, Stage.Panel, Where));
	AddInfo(Where);
	TestTrue(TEXT("The pad's Back completes"), PressKey(Rig, EKeys::Gamepad_FaceButton_Right));
	TestFalse(TEXT("Back closed the menu"), Placed.Anchor->IsOpen());
	TestTrue(TEXT("...with the focus on the trigger"), FocusIsIn(Rig, Placed.Trigger));
	Rig.PumpFrames(2);

	Placed.Anchor->Open(/*bFocusMenu*/ true);
	if (!TestTrue(TEXT("The menu opened again"), WaitForMenuShown(Rig, Placed))
		|| !TestTrue(TEXT("...with the focus on its first entry"), FocusIsIn(Rig, Placed.Entries[0])))
	{
		return false;
	}
	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	TestTrue(TEXT("The D-pad took the focus to the second entry"), FocusIsIn(Rig, Placed.Entries[1]));
	const bool bRingSits = DreamControlsWorldTest::WaitForRingOnFocusAsSeen(Rig, Where);
	TestTrue(FString::Printf(TEXT("The ring sits on the second entry where the camera sees it on the menu (%s)"), *Where), bRingSits);
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept pressed the second entry"), Placed.ClicksOf(1), 1);

	if (Placed.Anchor->IsOpen())
	{
		TestTrue(TEXT("The pad's Back completes"), PressKey(Rig, EKeys::Gamepad_FaceButton_Right));
	}
	TestFalse(TEXT("The menu is closed"), Placed.Anchor->IsOpen());
	TestTrue(TEXT("...and the focus is back on the trigger"), FocusIsIn(Rig, Placed.Trigger));
	TestEqual(TEXT("No other entry was pressed"), Placed.ClicksOf(0) + Placed.ClicksOf(2), 0);
	return true;
}

#endif
