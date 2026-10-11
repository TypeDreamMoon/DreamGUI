// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamMenuAnchor.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"

#include "Driver/DreamDriverRig.h"
#include "Interaction/DreamControlsConfigInteractionTestHelpers.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * UDreamMenuAnchor with a disabled entry in its open menu, and on a canvas scaled to half and to double -- in UMG's
 * arrangement, where the menu opens from a trigger button's click handler that asks ShouldOpenDueToClick first (the
 * listener's HandleTriggerClicked).
 *
 * A UMenuAnchor has no input of its own: what a player does to a menu they do to the entries in it and to what opened it. A
 * disabled entry is off the hit path (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp:241) and passed
 * over by navigation (FindFocusableWidget, HittestGrid.cpp:471), but a press on it is still a press inside the menu, so the
 * menu stack keeps the menu up (it dismisses only menus the press or the focus left, FMenuStack::OnMenuContentLostFocus and
 * FindMenuInWidgetPath, Slate/Private/Framework/Application/MenuStack.cpp:752-773, :809). The menu goes up where its placement
 * puts it against the anchor's DPI-scaled geometry ("Place the menu immediately below the anchor", MenuPlacement_BelowAnchor,
 * SlateCore/Public/Types/SlateEnums.h:215), so on a scaled layer it hangs from the anchor as the anchor is drawn and its
 * entries are hit and focused where they are drawn. UMG's menu touches its anchor; this control's menu hangs the style's Offset
 * below it (FDreamMenuAnchorStyle::Offset, two units by default), in the anchor's units like the rest of it, so at double
 * scale the gap is four pixels and at half scale one.
 */
namespace DreamMenuAnchorConfigTestLocal
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
	 * A trigger button and a menu anchor over it at InPosition (canvas units, Y up), the menu opening below it at MenuSize, with
	 * three button entries in a column inside it, each one's clicks counted.
	 */
	FPlacedMenu PlaceMenu(FDreamDriverRig& InRig, const FVector2D& InPosition)
	{
		FPlacedMenu Placed;
		Placed.Listener = TStrongObjectPtr<UDreamPressInteractionListener>(NewObject<UDreamPressInteractionListener>());
		Placed.Trigger = InRig.MakeControl<UDreamButton>(TEXT("Trigger"), nullptr, TriggerSize, InPosition);
		Placed.Anchor = InRig.MakeControl<UDreamMenuAnchor>(TEXT("Anchor"), nullptr, TriggerSize, InPosition);
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
		InRig.PumpFrames(1);
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
	 * Whether the open menu is drawn below the anchor as the anchor is drawn: its top the style's Offset below the anchor's
	 * bottom edge, the offset at the canvas's scale, its left on the anchor's left, and MenuSize at the canvas's scale, each to
	 * within a pixel and a half. OutWhere says where both were.
	 */
	bool MenuHangsBelowAnchor(const FPlacedMenu& InPlaced, double InScale, FString& OutWhere)
	{
		const TOptional<FBox2D> AnchorRect = DreamControlsConfigTest::DrawnRect(InPlaced.Anchor);
		const TOptional<FBox2D> Menu = DreamControlsConfigTest::DrawnRect(InPlaced.Anchor->PopupNode.Get());
		if (!AnchorRect.IsSet() || !Menu.IsSet())
		{
			OutWhere = TEXT("the anchor or the menu has no pixels");
			return false;
		}
		const double Gap = static_cast<double>(InPlaced.Anchor->GetStyle().Offset) * InScale;
		OutWhere = FString::Printf(TEXT("the anchor at %s, the menu at %s, %.1f pixels of gap expected"),
			*DreamControlsConfigTest::Describe(AnchorRect.GetValue()), *DreamControlsConfigTest::Describe(Menu.GetValue()), Gap);
		return FMath::Abs(Menu->Min.Y - (AnchorRect->Max.Y + Gap)) <= 1.5
			&& FMath::Abs(Menu->Min.X - AnchorRect->Min.X) <= 1.5
			&& DreamControlsConfigTest::IsDrawnAtScale(Menu.GetValue(), MenuSize, InScale);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorDisabledEntryPressTest,
	"DreamGUI.MenuAnchor.AClickOrATapOnADisabledEntryOfTheOpenMenuNeitherPressesItNorClosesTheMenu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorDisabledEntryPressTest, "DreamGUI.MenuAnchor.AClickOrATapOnADisabledEntryOfTheOpenMenuNeitherPressesItNorClosesTheMenu", "[Pointer][Touch][Disabled]")

/*
 * The menu opened by its trigger, its first entry disabled. A click on that entry and a tap on it press nothing, and the menu
 * stays open: the press was inside it. A click on the second entry, enabled, presses that one.
 */
bool FDreamMenuAnchorDisabledEntryPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedMenu Placed = PlaceMenu(Rig, FVector2D(-300.0, 150.0));
	if (!TestTrue(TEXT("A trigger and a menu anchor with three entries can be made"), Placed.IsReady()))
	{
		return false;
	}
	Placed.Entries[0]->SetIsEnabled(false);
	TestTrue(TEXT("Clicking the trigger completes"), Rig.Driver()->Find(FDreamBy::Widget(Placed.Trigger))->Click());
	if (!TestTrue(TEXT("The trigger's click opened the menu"), WaitForMenuShown(Rig, Placed)))
	{
		return false;
	}
	FDreamElementRef Disabled = Rig.Driver()->Find(FDreamBy::Widget(Placed.Entries[0]));

	TestTrue(TEXT("Clicking the disabled entry completes"), Disabled->Click());
	TestTrue(TEXT("Tapping it completes"), Disabled->Tap());
	TestEqual(TEXT("The disabled entry was pressed by neither"), Placed.ClicksOf(0), 0);
	TestTrue(TEXT("...and the menu is still open: the presses were inside it"), Placed.Anchor->IsOpen());
	TestEqual(TEXT("...having announced only its opening"), Placed.Listener->MenuOpenStates.Num(), 1);

	TestTrue(TEXT("Clicking the second entry completes"), Rig.Driver()->Find(FDreamBy::Widget(Placed.Entries[1]))->Click());
	TestEqual(TEXT("The enabled entry beside it is pressed"), Placed.ClicksOf(1), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorDisabledEntryPadTest,
	"DreamGUI.MenuAnchor.ThePadInAnOpenMenuPassesOverADisabledEntrySoAcceptPressesTheNextOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorDisabledEntryPadTest, "DreamGUI.MenuAnchor.ThePadInAnOpenMenuPassesOverADisabledEntrySoAcceptPressesTheNextOne", "[Nav][Disabled]")

/*
 * The focus on the trigger and the menu opened with its focus (UMG's Open(bFocusMenu)), its middle entry disabled. The focus
 * is on the first entry; D-pad down passes over the middle one to the third, and the pad's Accept presses the third and never
 * the disabled one. D-pad up goes back past it to the first.
 */
bool FDreamMenuAnchorDisabledEntryPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FPlacedMenu Placed = PlaceMenu(Rig, FVector2D(-300.0, 150.0));
	if (!TestTrue(TEXT("A trigger and a menu anchor with three entries can be made"), Placed.IsReady())
		|| !TestTrue(TEXT("The focus goes onto the trigger"), PutFocusOn(Rig, Placed.Trigger->FaceNode.Get())))
	{
		return false;
	}
	Placed.Entries[1]->SetIsEnabled(false);
	Placed.Anchor->Open(/*bFocusMenu*/ true);
	if (!TestTrue(TEXT("The menu opened"), WaitForMenuShown(Rig, Placed))
		|| !TestTrue(TEXT("...with the focus on its first entry"), FocusIsIn(Rig, Placed.Entries[0])))
	{
		return false;
	}

	TestTrue(TEXT("D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
	TestTrue(TEXT("D-pad down passes over the disabled entry to the third"), FocusIsIn(Rig, Placed.Entries[2]));
	TestTrue(TEXT("The pad's Accept completes"), PadConfirm(Rig));
	TestEqual(TEXT("Accept pressed the third entry"), Placed.ClicksOf(2), 1);
	TestEqual(TEXT("...and never the disabled one"), Placed.ClicksOf(1), 0);
	if (Placed.Anchor->IsOpen())
	{
		TestTrue(TEXT("D-pad up completes"), PressKey(Rig, EKeys::Gamepad_DPad_Up));
		TestTrue(TEXT("D-pad up passes back over the disabled entry to the first"), FocusIsIn(Rig, Placed.Entries[0]));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorScaledPressTest,
	"DreamGUI.MenuAnchor.OnACanvasScaledToHalfAndToDoubleTheMenuHangsBelowTheDrawnAnchorAndIsPressedAndDismissedWhereItIsDrawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorScaledPressTest, "DreamGUI.MenuAnchor.OnACanvasScaledToHalfAndToDoubleTheMenuHangsBelowTheDrawnAnchorAndIsPressedAndDismissedWhereItIsDrawn", "[Pointer][Touch][Scaled]")

/*
 * At each scale: a click three pixels inside the trigger's bottom-right corner opens the menu, which hangs from the anchor's
 * drawn bottom edge, left edges aligned, at its size times the scale. A click three pixels inside the second entry's
 * bottom-right corner presses that entry, and a click four pixels below the menu's drawn bottom edge closes it. Then a tap
 * inside the trigger opens it again, a tap three pixels inside the third entry's top-left corner presses that entry, and a tap
 * four pixels past the menu's right edge closes it.
 */
bool FDreamMenuAnchorScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorConfigTestLocal;
	using namespace DreamControlsConfigTest;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FPlacedMenu Placed = PlaceMenu(Rig, FVector2D(-200.0, 150.0));
		if (!TestTrue(At(Scale, TEXT("A trigger and a menu anchor with three entries can be made")), Placed.IsReady()))
		{
			return false;
		}
		const TOptional<FBox2D> Trigger = DrawnRect(Placed.Trigger->FaceNode.Get());
		if (!TestTrue(At(Scale, TEXT("The trigger is on the viewport")), Trigger.IsSet()))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("A click just inside the trigger's bottom-right corner completes")), ClickAt(Rig, InsideBottomRight(Trigger.GetValue(), 3.0)));
		if (!TestTrue(At(Scale, TEXT("It opened the menu")), WaitForMenuShown(Rig, Placed)))
		{
			return false;
		}
		FString Where;
		TestTrue(At(Scale, TEXT("The menu hangs from the anchor's drawn bottom edge, left edges aligned, at the canvas's scale")),
			MenuHangsBelowAnchor(Placed, Scale, Where));
		AddInfo(At(Scale, Where));
		const TOptional<FBox2D> Second = DrawnRect(Placed.Entries[1]->FaceNode.Get());
		const TOptional<FBox2D> Menu = DrawnRect(Placed.Anchor->PopupNode.Get());
		if (!TestTrue(At(Scale, TEXT("The second entry and the menu are on the viewport")), Second.IsSet() && Menu.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("A click just inside the second entry's bottom-right corner completes")), ClickAt(Rig, InsideBottomRight(Second.GetValue(), 3.0)));
		TestEqual(*At(Scale, TEXT("It pressed the second entry")), Placed.ClicksOf(1), 1);
		TestTrue(At(Scale, TEXT("A click just below the menu's drawn bottom edge completes")), ClickAt(Rig, PastBottomEdge(Menu.GetValue(), 4.0)));
		TestFalse(At(Scale, TEXT("It closed the menu")), Placed.Anchor->IsOpen());
		Rig.PumpFrames(2);

		TestTrue(At(Scale, TEXT("A tap just inside the trigger's top-left corner completes")), TapAt(Rig, InsideTopLeft(Trigger.GetValue(), 3.0)));
		if (!TestTrue(At(Scale, TEXT("It opened the menu again")), WaitForMenuShown(Rig, Placed)))
		{
			return false;
		}
		const TOptional<FBox2D> Third = DrawnRect(Placed.Entries[2]->FaceNode.Get());
		const TOptional<FBox2D> MenuAgain = DrawnRect(Placed.Anchor->PopupNode.Get());
		if (!TestTrue(At(Scale, TEXT("The third entry and the menu are on the viewport")), Third.IsSet() && MenuAgain.IsSet()))
		{
			return false;
		}
		TestTrue(At(Scale, TEXT("A tap just inside the third entry's top-left corner completes")), TapAt(Rig, InsideTopLeft(Third.GetValue(), 3.0)));
		TestEqual(*At(Scale, TEXT("It pressed the third entry")), Placed.ClicksOf(2), 1);
		TestTrue(At(Scale, TEXT("A tap just past the menu's drawn right edge completes")), TapAt(Rig, PastRightEdge(MenuAgain.GetValue(), 4.0)));
		TestFalse(At(Scale, TEXT("It closed the menu")), Placed.Anchor->IsOpen());
		TestEqual(*At(Scale, TEXT("The first entry was never pressed")), Placed.ClicksOf(0), 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMenuAnchorScaledPadTest,
	"DreamGUI.MenuAnchor.OnACanvasScaledToHalfAndToDoubleThePadWalksTheOpenMenuWithTheRingOnEachEntryAsDrawnAndBackReturnsToTheTrigger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamMenuAnchorScaledPadTest, "DreamGUI.MenuAnchor.OnACanvasScaledToHalfAndToDoubleThePadWalksTheOpenMenuWithTheRingOnEachEntryAsDrawnAndBackReturnsToTheTrigger", "[Nav][Scaled]")

/*
 * At each scale, the focus on the trigger and the menu opened with its focus: the focus is on the first entry. D-pad down
 * takes it to the second, the ring sits on the second where it is drawn on the lifted menu, and the pad's Accept presses the
 * second. The pad's Back closes the menu and the focus is back on the trigger.
 */
bool FDreamMenuAnchorScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamMenuAnchorConfigTestLocal;
	using namespace DreamControlsConfigTest;
	FFocusDrawnByKeys Settings;
	for (const double Scale : Scales())
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		Rig.BindTest(this);
		if (!CameUpAt(*this, Rig, Scale))
		{
			return false;
		}
		const FPlacedMenu Placed = PlaceMenu(Rig, FVector2D(-200.0, 150.0));
		if (!TestTrue(At(Scale, TEXT("A trigger and a menu anchor with three entries can be made")), Placed.IsReady())
			|| !TestTrue(At(Scale, TEXT("The focus goes onto the trigger")), PutFocusOn(Rig, Placed.Trigger->FaceNode.Get())))
		{
			return false;
		}
		Placed.Anchor->Open(/*bFocusMenu*/ true);
		if (!TestTrue(At(Scale, TEXT("The menu opened")), WaitForMenuShown(Rig, Placed))
			|| !TestTrue(At(Scale, TEXT("...with the focus on its first entry")), FocusIsIn(Rig, Placed.Entries[0])))
		{
			return false;
		}
		FString Where;
		TestTrue(At(Scale, TEXT("The menu hangs from the anchor's drawn bottom edge, left edges aligned, at the canvas's scale")),
			MenuHangsBelowAnchor(Placed, Scale, Where));
		AddInfo(At(Scale, Where));

		TestTrue(At(Scale, TEXT("D-pad down completes")), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		TestTrue(At(Scale, TEXT("The D-pad took the focus to the second entry")), FocusIsIn(Rig, Placed.Entries[1]));
		const bool bRingSits = WaitForRingOnFocus(Rig, Where);
		TestTrue(At(Scale, FString::Printf(TEXT("The ring sits on the second entry where it is drawn (%s)"), *Where)), bRingSits);
		TestTrue(At(Scale, TEXT("The pad's Accept completes")), PadConfirm(Rig));
		TestEqual(*At(Scale, TEXT("Accept pressed the second entry")), Placed.ClicksOf(1), 1);

		if (Placed.Anchor->IsOpen())
		{
			TestTrue(At(Scale, TEXT("The pad's Back completes")), PressKey(Rig, EKeys::Gamepad_FaceButton_Right));
		}
		TestFalse(At(Scale, TEXT("The menu is closed")), Placed.Anchor->IsOpen());
		TestTrue(At(Scale, TEXT("...and the focus is back on the trigger")), FocusIsIn(Rig, Placed.Trigger));
	}
	return true;
}

#endif
