// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamScrollBox.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIInputServices.h"
#include "InputCoreTypes.h"
#include "Interaction/UIButton.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "Interaction/UISelectable.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverUntil.h"

/*
 * THE TAB KEY'S ORDER, AND THE FOCUS RING, ON A REAL SCREEN.
 *
 * The order is FDreamUITabOrder's -- the widget tree depth first, siblings by TabIndex, an explicit Next link first, a
 * container's TabNavigation holding Tab inside it, the screen's end going round only when the screen says so -- and its own
 * tests ask it as a sequence. These press the Tab key through the rig's key road and read where player 0's focus went, as
 * a player would see it: the same rules UMG's widgets keep (a lower TabIndex first among siblings, as a browser walks
 * tabindex; UWidget::SetNavigationRuleExplicit for Next; a cycling container as Slate's Wrap boundary rule).
 *
 * The focus ring marks the control the focus is on while the focus is drawn, the way Slate draws a focused widget's own
 * focus brush: it goes where the D-pad takes the focus, and it stays on the control when the control moves under it -- a
 * scroll box scrolling the focused button along.
 */
namespace DreamTabOrderKeyInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D ButtonSize(160.0, 40.0);

	/** The settings every test here runs under, whatever the project's config says. */
	struct FTabSettings
	{
		TGuardValue<bool> TabNavigation;
		TGuardValue<EDreamUITabOrder> TabOrder;
		TGuardValue<bool> WrapsAtScreenEnd;
		TGuardValue<bool> FocusVisibleOnlyFromKeys;

		explicit FTabSettings(bool bInWrapsAtScreenEnd = true)
			: TabNavigation(GetMutableDefault<UDreamGUISettings>()->bTabNavigation, true)
			, TabOrder(GetMutableDefault<UDreamGUISettings>()->TabOrder, EDreamUITabOrder::Hierarchy)
			, WrapsAtScreenEnd(GetMutableDefault<UDreamGUISettings>()->bTabWrapsAtScreenEnd, bInWrapsAtScreenEnd)
			, FocusVisibleOnlyFromKeys(GetMutableDefault<UDreamGUISettings>()->bFocusVisibleOnlyFromKeys, true)
		{
		}
	};

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent,
		const FVector2D& InPosition, const FVector2D& InSize = ButtonSize)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, InSize, InPosition);
		const bool bReady = Button != nullptr && Button->FaceNode != nullptr && Button->ButtonBehaviour != nullptr;
		InTest.TestTrue(*FString::Printf(TEXT("A button '%s' with a face and a behaviour can be made"), InName), bReady);
		return bReady ? Button : nullptr;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

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

	/** InPresses presses of Tab through the rig's key road, and where the focus was after each. */
	FString PressTab(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TArray<UDreamButton*>& InButtons, int32 InPresses)
	{
		TArray<FString> Names;
		for (int32 Press = 0; Press < InPresses; ++Press)
		{
			InTest.TestTrue(TEXT("The Tab press completes"), InRig.Driver()->Sequence().Tab().Perform());
			Names.Add(FocusedName(InRig, InButtons));
		}
		return FString::Join(Names, TEXT(", "));
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

	/** Whether the ring's own rect sits on InFace's: the same centre within a pixel and a half. */
	bool RingSitsOn(FDreamDriverRig& InRig, const UUINavigationInputSelectionHandler* InRing, const UDreamWidget* InFace, FString& OutWhere)
	{
		const UDreamWidget* RingWidget = InRing != nullptr ? InRing->GetWidget() : nullptr;
		const TOptional<FBox2D> RingRect = RingWidget != nullptr ? FDreamDriverProjection::WidgetToPixelRect(RingWidget) : TOptional<FBox2D>();
		const TOptional<FBox2D> FaceRect = InFace != nullptr ? FDreamDriverProjection::WidgetToPixelRect(InFace) : TOptional<FBox2D>();
		if (!RingRect.IsSet() || !FaceRect.IsSet())
		{
			// Which of the two, and where the ring hangs, so a failure says what to look at.
			const UDreamCanvas* RingRoot = RingWidget != nullptr ? RingWidget->GetRootCanvas() : nullptr;
			const UDreamCanvas* RingCanvas = RingWidget != nullptr ? RingWidget->GetRenderCanvas() : nullptr;
			OutWhere = FString::Printf(TEXT("(no pixels for %s%s%s; the ring's render canvas %s, its root canvas %s in mode %d, the ring under %s)"),
				RingWidget == nullptr ? TEXT("the ring, which has no widget") : (!RingRect.IsSet() ? TEXT("the ring") : TEXT("")),
				!RingRect.IsSet() && !FaceRect.IsSet() ? TEXT(" and ") : TEXT(""),
				!FaceRect.IsSet() ? TEXT("the control") : TEXT(""),
				RingCanvas != nullptr ? *RingCanvas->GetPathName() : TEXT("none"),
				RingRoot != nullptr ? *RingRoot->GetPathName() : TEXT("none"),
				RingRoot != nullptr ? static_cast<int32>(RingRoot->GetActualRenderMode()) : -1,
				RingWidget != nullptr && RingWidget->GetParent() != nullptr ? *RingWidget->GetParent()->GetDisplayName() : TEXT("nothing"));
			if (RingWidget != nullptr && InFace != nullptr)
			{
				OutWhere += FString::Printf(TEXT(" (the ring at %s, the control at %s)"),
					*RingWidget->GetWorldTransform().GetLocation().ToString(), *InFace->GetWorldTransform().GetLocation().ToString());
			}
			return false;
		}
		OutWhere = FString::Printf(TEXT("the ring's centre %s, the control's %s"), *RingRect->GetCenter().ToString(), *FaceRect->GetCenter().ToString());
		return RingRect->GetCenter().Equals(FaceRect->GetCenter(), 1.5);
	}

	/**
	 * Pumped until the screen's ring for InFace is shown and sits on it: its flight and its fade are its own (a quarter of a
	 * second, UUINavigationInputSelectionHandler::AnimDuration), so the wait is for the ring, with room to spare.
	 */
	bool WaitForRingOn(FDreamDriverRig& InRig, const UDreamWidget* InFace, const TCHAR* InWhat)
	{
		const FWaitTimeout Timeout = FWaitTimeout::InSeconds(2.0);
		FDreamDriverRig* Rig = &InRig;
		return InRig.Driver()->Wait(FDreamUntil::Condition([Rig, InFace]()
		{
			const UUINavigationInputSelectionHandler* Ring = UUINavigationInputSelectionHandler::FindFor(InFace);
			FString Where;
			return Ring != nullptr && Ring->GetWidget() != nullptr && Ring->GetWidget()->GetRenderOpacity() > 0.99f
				&& RingSitsOn(*Rig, Ring, InFace, Where);
		}, Timeout), Timeout, InWhat);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabKeyTabIndexTest,
	"DreamGUI.Navigation.Tab.TabIndexPutsAButtonAheadOfItsSiblingsWhenTheTabKeyWalksTheScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabKeyTabIndexTest, "DreamGUI.Navigation.Tab.TabIndexPutsAButtonAheadOfItsSiblingsWhenTheTabKeyWalksTheScreen", "[Nav][Animated]")

/*
 * Three buttons made A, B, C, and C given a lower TabIndex than the other two: the Tab key enters at C, then walks A and B
 * in the order they were made, and goes round to C at the screen's end.
 */
bool FDreamTabKeyTabIndexTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderKeyInteractionTestLocal;
	FTabSettings Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* A = PlaceButton(*this, Rig, TEXT("A"), nullptr, FVector2D(-300.0, 0.0));
	UDreamButton* B = PlaceButton(*this, Rig, TEXT("B"), nullptr, FVector2D(0.0, 0.0));
	UDreamButton* C = PlaceButton(*this, Rig, TEXT("C"), nullptr, FVector2D(300.0, 0.0));
	if (A == nullptr || B == nullptr || C == nullptr)
	{
		return false;
	}
	C->SetTabIndex(-1);
	Rig.PumpFrames(1);
	TestEqual(TEXT("Tab walks the lower TabIndex first, then the rest in the order they were made, and goes round"),
		PressTab(*this, Rig, { A, B, C }, 4), FString(TEXT("C, A, B, C")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabKeyExplicitNextTest,
	"DreamGUI.Navigation.Tab.AnExplicitNextLinkSendsTheTabKeyStraightToItsTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabKeyExplicitNextTest, "DreamGUI.Navigation.Tab.AnExplicitNextLinkSendsTheTabKeyStraightToItsTarget", "[Nav][Animated]")

/*
 * Four buttons in a row, the first one's Next linked to the last (UWidget::SetNavigationRuleExplicit on Next): Tab from
 * the first goes straight to the last, over the two between, and from there round to the first as the screen's end does.
 */
bool FDreamTabKeyExplicitNextTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderKeyInteractionTestLocal;
	FTabSettings Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* A = PlaceButton(*this, Rig, TEXT("A"), nullptr, FVector2D(-450.0, 0.0));
	UDreamButton* B = PlaceButton(*this, Rig, TEXT("B"), nullptr, FVector2D(-150.0, 0.0));
	UDreamButton* C = PlaceButton(*this, Rig, TEXT("C"), nullptr, FVector2D(150.0, 0.0));
	UDreamButton* D = PlaceButton(*this, Rig, TEXT("D"), nullptr, FVector2D(450.0, 0.0));
	if (A == nullptr || B == nullptr || C == nullptr || D == nullptr)
	{
		return false;
	}
	A->ButtonBehaviour->SetNavigationNext(EUISelectableNavigationMode::Explicit);
	A->ButtonBehaviour->SetNavigationNextExplicit(D->ButtonBehaviour);
	Rig.PumpFrames(1);
	TestEqual(TEXT("Tab enters at the first, follows its link to the last, and goes round"),
		PressTab(*this, Rig, { A, B, C, D }, 3), FString(TEXT("A, D, A")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabKeyCycleGroupTest,
	"DreamGUI.Navigation.Tab.ACyclingGroupKeepsTheTabKeyGoingRoundInsideItOnceItIsIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabKeyCycleGroupTest, "DreamGUI.Navigation.Tab.ACyclingGroupKeepsTheTabKeyGoingRoundInsideItOnceItIsIn", "[Nav][Animated]")

/*
 * A button, a group of two set to cycle, and a button after it. Tab enters the group from the button before it as usual,
 * and once inside goes round the group's two buttons and never on to the one after.
 */
bool FDreamTabKeyCycleGroupTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderKeyInteractionTestLocal;
	FTabSettings Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* Before = PlaceButton(*this, Rig, TEXT("Before"), nullptr, FVector2D(0.0, 200.0));
	UDreamWidget* Group = Rig.MakeWidget(TEXT("Group"), nullptr, FVector2D(500.0, 120.0), FVector2D(0.0, 0.0));
	UDreamButton* G1 = Group != nullptr ? PlaceButton(*this, Rig, TEXT("G1"), Group, FVector2D(-120.0, 0.0)) : nullptr;
	UDreamButton* G2 = Group != nullptr ? PlaceButton(*this, Rig, TEXT("G2"), Group, FVector2D(120.0, 0.0)) : nullptr;
	UDreamButton* After = PlaceButton(*this, Rig, TEXT("After"), nullptr, FVector2D(0.0, -200.0));
	if (Before == nullptr || G1 == nullptr || G2 == nullptr || After == nullptr)
	{
		return false;
	}
	Group->SetTabNavigation(EDreamWidgetTabNavigation::Cycle);
	Rig.PumpFrames(1);
	TestEqual(TEXT("Tab enters the group from the button before it, then goes round inside it"),
		PressTab(*this, Rig, { Before, G1, G2, After }, 5), FString(TEXT("Before, G1, G2, G1, G2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTabKeyNoWrapTest,
	"DreamGUI.Navigation.Tab.WithTheScreenSetNotToWrapTheTabKeyHoldsAtTheLastStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamTabKeyNoWrapTest, "DreamGUI.Navigation.Tab.WithTheScreenSetNotToWrapTheTabKeyHoldsAtTheLastStop", "[Nav][Animated]")

/*
 * bTabWrapsAtScreenEnd off: the screen's last stop is the end of the walk, as a Slate screen whose root's Next is Stop. Tab
 * reaches the last button and then keeps the focus there.
 */
bool FDreamTabKeyNoWrapTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderKeyInteractionTestLocal;
	FTabSettings Settings(/*bInWrapsAtScreenEnd*/false);
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
	TestEqual(TEXT("Tab walks to the last stop and holds there"), PressTab(*this, Rig, { First, Last }, 4), FString(TEXT("First, Last, Last, Last")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFocusRingFollowsPadTest,
	"DreamGUI.Navigation.Focus.TheRingGoesWhereTheDPadTakesTheFocus",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamFocusRingFollowsPadTest, "DreamGUI.Navigation.Focus.TheRingGoesWhereTheDPadTakesTheFocus", "[Nav][Animated]")

/*
 * Two buttons side by side, the first focused with Tab, which draws the focus and brings the ring. The D-pad takes the
 * focus to the second, and the ring goes with it: it marks the second button and, once its flight is over, sits on it.
 */
bool FDreamFocusRingFollowsPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderKeyInteractionTestLocal;
	FTabSettings Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamButton* First = PlaceButton(*this, Rig, TEXT("First"), nullptr, FVector2D(-250.0, 0.0));
	UDreamButton* Second = PlaceButton(*this, Rig, TEXT("Second"), nullptr, FVector2D(250.0, 0.0));
	if (First == nullptr || Second == nullptr)
	{
		return false;
	}
	Rig.PumpFrames(1);
	const TArray<UDreamButton*> Buttons = { First, Second };
	TestEqual(TEXT("Tab focuses the first button"), PressTab(*this, Rig, Buttons, 1), FString(TEXT("First")));
	UUINavigationInputSelectionHandler* Ring = UUINavigationInputSelectionHandler::FindFor(First->FaceNode.Get());
	if (!TestNotNull(TEXT("The focus drawn by a key brings a ring"), Ring))
	{
		return false;
	}
	TestTrue(TEXT("The ring comes to sit on the first button"), WaitForRingOn(Rig, First->FaceNode.Get(), TEXT("the ring sits on the first button")));
	FString Where;

	TestTrue(TEXT("D-pad right completes"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Right).WaitFrames(1).Perform());
	TestEqual(TEXT("The D-pad took the focus to the second button"), FocusedName(Rig, Buttons), FString(TEXT("Second")));
	TestTrue(TEXT("The ring goes to sit on the second button"), WaitForRingOn(Rig, Second->FaceNode.Get(), TEXT("the ring sits on the second button")));
	UUINavigationInputSelectionHandler* RingNow = UUINavigationInputSelectionHandler::FindFor(Second->FaceNode.Get());
	if (!TestNotNull(TEXT("The screen's ring serves the second button"), RingNow))
	{
		return false;
	}
	if (const UDreamWidget* Marked = MarkedBy(*this, RingNow))
	{
		TestTrue(TEXT("The ring marks the second button"), IsPartOf(Marked, Second));
	}
	const bool bSitsOnSecond = RingSitsOn(Rig, RingNow, Second->FaceNode.Get(), Where);
	TestTrue(FString::Printf(TEXT("...and sits on it (%s)"), *Where), bSitsOnSecond);
	TestTrue(TEXT("...shown"), RingNow->GetWidget() != nullptr && RingNow->GetWidget()->GetRenderOpacity() > 0.99f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFocusRingFollowsScrollTest,
	"DreamGUI.Navigation.Focus.TheRingStaysOnAFocusedButtonWhileItsScrollBoxScrollsItAlong",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamFocusRingFollowsScrollTest, "DreamGUI.Navigation.Focus.TheRingStaysOnAFocusedButtonWhileItsScrollBoxScrollsItAlong", "[Pointer][Nav][Animated]")

/*
 * A scroll box of buttons, the second focused by Tab, which brings the ring onto it. The wheel over the box scrolls the
 * content -- the focused button with it -- and the ring is still on the button where the button is drawn now, not where
 * it was.
 */
bool FDreamFocusRingFollowsScrollTest::RunTest(const FString& Parameters)
{
	using namespace DreamTabOrderKeyInteractionTestLocal;
	FTabSettings Settings;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamScrollBox* Box = Rig.IsUsable() ? Rig.MakeControl<UDreamScrollBox>(TEXT("Box"), nullptr, FVector2D(320.0, 300.0)) : nullptr;
	if (!TestTrue(TEXT("The rig and a scroll box with content came up"), Box != nullptr && Box->GetContentNode() != nullptr))
	{
		return false;
	}
	TArray<UDreamButton*> Buttons;
	for (int32 Index = 0; Index < 10; ++Index)
	{
		Buttons.Add(PlaceButton(*this, Rig, *FString::Printf(TEXT("Row%d"), Index), Box->GetContentNode(), FVector2D::ZeroVector, FVector2D(300.0, 80.0)));
	}
	if (Buttons.Contains(nullptr))
	{
		return false;
	}
	Box->RefreshContentExtent();
	Rig.PumpFrames(2);

	TestEqual(TEXT("Tab twice focuses the second button in the box"), PressTab(*this, Rig, Buttons, 2), FString(TEXT("Row0, Row1")));
	UDreamButton* Focused = Buttons[1];
	UUINavigationInputSelectionHandler* Ring = UUINavigationInputSelectionHandler::FindFor(Focused->FaceNode.Get());
	if (!TestNotNull(TEXT("The focus drawn by a key brings a ring"), Ring))
	{
		return false;
	}
	TestTrue(TEXT("The ring comes to sit on the focused button"), WaitForRingOn(Rig, Focused->FaceNode.Get(), TEXT("the ring sits on the focused button")));
	FString Where;
	const TOptional<FVector2D> ButtonBefore = Rig.Driver()->Find(FDreamBy::Widget(Focused->FaceNode.Get()))->GetCentrePixel();

	TestTrue(TEXT("A wheel notch over the box completes"), Rig.Driver()->Find(FDreamBy::Widget(Box))->ScrollBy(FVector2D(-1.0, -1.0)));
	const FWaitTimeout ScrollTimeout = FWaitTimeout::InSeconds(2.0);
	TestTrue(TEXT("...and the box comes to rest"), Rig.Driver()->Wait(FDreamUntil::Condition([Box]() { return !Box->GetIsScrolling(); }, ScrollTimeout),
		ScrollTimeout, TEXT("the scroll box comes to rest")));
	const TOptional<FVector2D> ButtonAfter = Rig.Driver()->Find(FDreamBy::Widget(Focused->FaceNode.Get()))->GetCentrePixel();
	if (!TestTrue(TEXT("The wheel scrolled the focused button along"),
		ButtonBefore.IsSet() && ButtonAfter.IsSet() && FMath::Abs(ButtonAfter->Y - ButtonBefore->Y) > 10.0))
	{
		return false;
	}
	TestEqual(TEXT("The focus is still on the button"), FocusedName(Rig, Buttons), FString(TEXT("Row1")));
	const bool bSitsWhereDrawnNow = RingSitsOn(Rig, Ring, Focused->FaceNode.Get(), Where);
	TestTrue(FString::Printf(TEXT("...and the ring is on it where it is drawn now (%s)"), *Where), bSitsWhereDrawnNow);
	return true;
}

#endif
