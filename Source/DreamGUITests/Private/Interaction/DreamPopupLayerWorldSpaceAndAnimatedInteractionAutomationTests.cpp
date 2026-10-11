// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIPopupLayer.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * A CARD ON THE POPUP LAYER, WITH A PANEL IN THE LEVEL BEHIND IT -- AND A FINGER ON A CARD AS SHIPPED.
 *
 * The layer is the Slate menu stack's counterpart (Slate/Private/Framework/Application/MenuStack.cpp). A menu is dismissed
 * when a press lands outside every menu -- the focus moving off it (FMenuStack::OnMenuContentLostFocus) -- and a press on the
 * game viewport is such a press: the world behind the screen, a widget component in the level included, is outside every
 * menu on the screen. DreamGUI's layer says so itself (UDreamUIPopupLayer::IsInFrontOfPopup: "Another root canvas -- the
 * world, a render target, another player's screen -- is no layer over this popup"), and does with that press what the popup
 * asked for: Consume, its default, closes the popup and lets the press go no further; PassThrough closes it and lets the
 * press on to what is under it, as Slate's menus let it.
 *
 * The focus: a popup takes the player's focus as it opens and gives it back to its opener when it closes (SMenuAnchor and
 * SComboBox return focus to themselves), whether the opener is on the screen or on a panel in the level -- where DreamGUI's
 * D-pad and confirm reach it as on the screen. A finger is the mouse's press (FSlateApplication hands an unanswered touch on
 * as one), inside the card and outside it alike.
 *
 * Every card here is on the screen. Where a popup opened by a control on a panel should be drawn -- on the panel, as UMG
 * draws a widget component's menus inside its own window, or on the screen -- is left open, and nothing here depends on it.
 */
namespace DreamPopupLayerWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	const FVector2D ButtonSize(120.0, 40.0);

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent,
		const FVector2D& InSize, const FVector2D& InPosition, UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, InSize, InPosition);
		if (!InTest.TestTrue(*FString::Printf(TEXT("A button '%s' with a face can be made"), InName), Button != nullptr && Button->FaceNode != nullptr))
		{
			return nullptr;
		}
		if (InListener != nullptr)
		{
			Button->OnClicked.AddDynamic(InListener, &UDreamPressInteractionListener::HandleClicked);
		}
		return Button;
	}

	/** InCard pushed onto player 0's popup layer as InOutsideClick asks, the focus going to InInitialFocus; false when it did not go up. */
	bool PushCard(FAutomationTestBase& InTest, UDreamUIPopupLayer* InPopups, UDreamWidget* InCard, UDreamWidget* InInitialFocus,
		UDreamWidget* InOpener, EDreamPopupOutsideClick InOutsideClick, const FString& InWhat)
	{
		FDreamPopupParams Params;
		Params.Popup = InCard;
		Params.Opener = InOpener;
		Params.UserIndex = 0;
		Params.InitialFocus = InInitialFocus;
		Params.OutsideClick = InOutsideClick;
		return InTest.TestTrue(InWhat, InPopups->Push(Params));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedPopupTapTest,
	"DreamGUI.Popup.ATapInsideTheCardReachesItsButtonAndATapOutsideClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedPopupTapTest, "DreamGUI.Popup.ATapInsideTheCardReachesItsButtonAndATapOutsideClosesIt", "[Touch][Animated]")

/*
 * As shipped on the screen: a card on the layer with a button in it. A finger tapped on the button clicks it and leaves the
 * card open; a finger tapped below the card closes it, and clicks nothing.
 */
bool FDreamAnimatedPopupTapTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerWorldTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(300.0, 120.0), FVector2D(0.0, 40.0));
	UDreamButton* Inside = Card != nullptr ? PlaceButton(*this, Rig, TEXT("Inside"), Card, ButtonSize, FVector2D::ZeroVector, Listener.Get()) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (Inside == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups))
	{
		return false;
	}
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(1);
	if (!PushCard(*this, Popups, Card, Inside->FaceNode.Get(), nullptr, EDreamPopupOutsideClick::Consume, TEXT("The card goes up on the popup layer")))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const TOptional<FBox2D> CardRect = RectOf(Rig, Card);
	if (!TestTrue(TEXT("The card is on the viewport"), CardRect.IsSet()))
	{
		Popups->Dismiss(Card);
		return false;
	}
	const FVector2D BelowTheCard(CardRect->GetCenter().X, CardRect->Max.Y + 60.0);

	TestTrue(TEXT("A finger tapped on the card's button completes"), Rig.Driver()->Find(FDreamBy::Widget(Inside))->Tap(0));
	TestEqual(TEXT("The tap clicked the card's button"), Listener->ClickedCount, 1);
	TestTrue(TEXT("...and the card is still open"), Popups->IsOpen(Card));

	TestTrue(TEXT("A finger tapped below the card completes"), Rig.Driver()->Sequence().TouchDown(1, BelowTheCard).TouchUp(1).WaitFrames(1).Perform());
	TestFalse(TEXT("The tap below the card closed it"), Popups->IsOpen(Card));
	TestEqual(TEXT("...and clicked nothing"), Listener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldPopupOutsidePressTest,
	"DreamGUI.Popup.APressOnAWorldPanelIsAPressOutsideAnOpenCardThatClosesItAndGoesOnOnlyWhenItPassesThrough",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldPopupOutsidePressTest, "DreamGUI.Popup.APressOnAWorldPanelIsAPressOutsideAnOpenCardThatClosesItAndGoesOnOnlyWhenItPassesThrough", "[Pointer][Touch][World]")

/*
 * A card on the screen's popup layer, clear of a button on a panel in the level. Opened as it opens by default (Consume): a
 * click through the world pointer on the panel's button closes the card and clicks nothing; opened again, a finger's tap there
 * does the same. Opened to let outside presses through (PassThrough): the click closes the card and reaches the panel's button,
 * and so does the tap.
 */
bool FDreamWorldPopupOutsidePressTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerWorldTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> CardListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> PanelListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* OnPanel = Stage.IsReady() ? PlaceButton(*this, Rig, TEXT("OnPanel"), Stage.Panel, FVector2D(160.0, 60.0), FVector2D(150.0, -80.0), PanelListener.Get()) : nullptr;
	// High on the screen, well clear of where the panel's button is seen.
	UDreamWidget* Card = OnPanel != nullptr ? Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(300.0, 120.0), FVector2D(-200.0, 200.0)) : nullptr;
	UDreamButton* Inside = Card != nullptr ? PlaceButton(*this, Rig, TEXT("Inside"), Card, ButtonSize, FVector2D::ZeroVector, CardListener.Get()) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (Inside == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups))
	{
		return false;
	}
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Rig.PumpFrames(2);
	FDreamElementRef PanelElement = Rig.Driver()->Find(FDreamBy::Widget(OnPanel));
	const TOptional<FBox2D> PanelButtonRect = PanelElement->GetPixelRect();
	const TOptional<FBox2D> CardRect = RectOf(Rig, Card);
	if (!TestTrue(TEXT("The panel's button and the card are both on the viewport"), PanelButtonRect.IsSet() && CardRect.IsSet())
		|| !TestFalse(TEXT("...apart (the layout this test needs)"), PanelButtonRect->Intersect(CardRect.GetValue())))
	{
		return false;
	}

	struct FCase
	{
		EDreamPopupOutsideClick OutsideClick;
		bool bFinger;
		int32 PanelClicksAfter;
		const TCHAR* Name;
	};
	const FCase Cases[] = {
		{ EDreamPopupOutsideClick::Consume, false, 0, TEXT("Consume, a click") },
		{ EDreamPopupOutsideClick::Consume, true, 0, TEXT("Consume, a tap") },
		{ EDreamPopupOutsideClick::PassThrough, false, 1, TEXT("PassThrough, a click") },
		{ EDreamPopupOutsideClick::PassThrough, true, 2, TEXT("PassThrough, a tap") },
	};
	int32 Finger = 0;
	for (const FCase& Case : Cases)
	{
		if (!PushCard(*this, Popups, Card, Inside->FaceNode.Get(), nullptr, Case.OutsideClick,
			FString::Printf(TEXT("[%s] The card goes up on the popup layer"), Case.Name)))
		{
			return false;
		}
		Rig.PumpFrames(1);
		const bool bPressed = Case.bFinger ? PanelElement->Tap(Finger++) : PanelElement->Click();
		TestTrue(FString::Printf(TEXT("[%s] The press on the panel's button through the world pointer completes"), Case.Name), bPressed);
		Rig.PumpFrames(1);
		TestFalse(FString::Printf(TEXT("[%s] The press on the panel was a press outside the card and closed it"), Case.Name), Popups->IsOpen(Card));
		TestEqual(*FString::Printf(TEXT("[%s] The panel's button has been clicked as often as the presses let through"), Case.Name),
			PanelListener->ClickedCount, Case.PanelClicksAfter);
	}
	TestEqual(TEXT("No press on the panel clicked the card's button"), CardListener->ClickedCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldPopupFocusTest,
	"DreamGUI.Popup.ACardOpenedFromAButtonOnAWorldPanelTakesThePadsFocusAndBackGivesItBackToTheButton",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldPopupFocusTest, "DreamGUI.Popup.ACardOpenedFromAButtonOnAWorldPanelTakesThePadsFocusAndBackGivesItBackToTheButton", "[Nav][World]")

/*
 * The pad's focus on a button on a panel, and a card opened from it on the popup layer, its left button taking the focus. The
 * D-pad right moves the focus to the card's right-hand button and the confirm clicks it, leaving the card open; Back closes
 * the card and the focus is back on the panel's button, whose click the next confirm makes.
 */
bool FDreamWorldPopupFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerWorldTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> OpenerListener(NewObject<UDreamPressInteractionListener>());
	TStrongObjectPtr<UDreamPressInteractionListener> RightListener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	UDreamButton* Opener = Stage.IsReady() ? PlaceButton(*this, Rig, TEXT("Opener"), Stage.Panel, FVector2D(160.0, 60.0), FVector2D(-150.0, -80.0), OpenerListener.Get()) : nullptr;
	UDreamWidget* Card = Opener != nullptr ? Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(360.0, 120.0), FVector2D(0.0, 200.0)) : nullptr;
	UDreamButton* Left = Card != nullptr ? PlaceButton(*this, Rig, TEXT("Left"), Card, ButtonSize, FVector2D(-90.0, 0.0), nullptr) : nullptr;
	UDreamButton* Right = Left != nullptr ? PlaceButton(*this, Rig, TEXT("Right"), Card, ButtonSize, FVector2D(90.0, 0.0), RightListener.Get()) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (Right == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups))
	{
		return false;
	}
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("Player 0's focus can be put on the panel's button"), PutFocusOn(Rig, Opener->FaceNode)))
	{
		return false;
	}
	if (!PushCard(*this, Popups, Card, Left->FaceNode.Get(), Opener->FaceNode.Get(), EDreamPopupOutsideClick::Consume,
		TEXT("The card goes up on the popup layer, opened from the panel's button")))
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!TestTrue(TEXT("The card took the focus onto its left button"), FocusIsIn(Rig, Left)))
	{
		Popups->Dismiss(Card);
		return false;
	}

	TestTrue(TEXT("The D-pad right completes"), PressKey(Rig, EKeys::Gamepad_DPad_Right));
	TestTrue(TEXT("The D-pad moved the focus to the card's right-hand button"), FocusIsIn(Rig, Right));
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"), PadConfirm(Rig));
	TestEqual(TEXT("The confirm clicked the right-hand button"), RightListener->ClickedCount, 1);
	TestTrue(TEXT("...and the card is still open"), Popups->IsOpen(Card));

	TestTrue(TEXT("Back completes"), Rig.Driver()->Sequence().Back().WaitFrames(1).Perform());
	TestFalse(TEXT("Back closed the card"), Popups->IsOpen(Card));
	TestTrue(TEXT("The focus is back on the panel's button that opened it"), FocusIsIn(Rig, Opener));
	TestTrue(TEXT("Pressing and releasing the pad's confirm again completes"), PadConfirm(Rig));
	TestEqual(TEXT("...and the confirm clicked the panel's button"), OpenerListener->ClickedCount, 1);
	return true;
}

#endif
