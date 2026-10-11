// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"
#include "Interaction/DreamUIPopupLayer.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A CARD ON THE POPUP LAYER WITH A DISABLED BUTTON IN IT, AND ON A SCALED CANVAS.
 *
 * The layer is the Slate menu stack's counterpart (Slate/Private/Framework/Application/MenuStack.cpp). A menu is dismissed
 * when the focus moves outside every menu (FMenuStack::OnMenuContentLostFocus, :752-786), and a press moves the focus to the
 * leaf-most focusable widget on its path (FSlateApplication's mouse-down routing, SlateApplication.cpp:5485-5503). A press on
 * a disabled button inside a menu has the button cut off its path (FHittestGrid::GetBubblePath), but the menu's own content
 * wrapper is still on it and takes keyboard focus (SMenuContentWrapper::SupportsKeyboardFocus, MenuStack.cpp:177-180): the
 * press is inside the menu, the menu stays open, and the disabled button is not clicked. Tab inside a menu that keeps it
 * walks the menu's own controls and, like every Tab, passes over a disabled one (FHittestGrid::FindNextFocusableWidget).
 *
 * On a canvas scaled to half and to double, a press inside the card where it is drawn reaches its button and keeps it open,
 * a press outside it closes it, and the D-pad moves between its buttons by where they are drawn.
 */
namespace DreamPopupLayerCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };
	const FVector2D ButtonSize(120.0, 40.0);

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

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent,
		const FVector2D& InPosition, UDreamPressInteractionListener* InListener)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, ButtonSize, InPosition);
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

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	/** InCard pushed onto player 0's popup layer, the focus going to InInitialFocus; false when it did not go up. */
	bool PushCard(FAutomationTestBase& InTest, UDreamUIPopupLayer* InPopups, UDreamWidget* InCard, UDreamWidget* InInitialFocus, const FString& InWhat)
	{
		FDreamPopupParams Params;
		Params.Popup = InCard;
		Params.UserIndex = 0;
		Params.InitialFocus = InInitialFocus;
		return InTest.TestTrue(InWhat, InPopups->Push(Params));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoveragePopupDisabledPressTest,
	"DreamGUI.Popup.APressOnADisabledButtonInsideAnOpenPopupIsAPressInsideItThatClosesNothingAndClicksNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoveragePopupDisabledPressTest, "DreamGUI.Popup.APressOnADisabledButtonInsideAnOpenPopupIsAPressInsideItThatClosesNothingAndClicksNothing", "[Pointer][Touch][Disabled]")

/*
 * A card on the layer with a disabled button and another button in it. A click on the disabled button and a finger's tap on
 * it leave the card open and click nothing. Enabled, the same click clicks it once, and the card is still open.
 */
bool FDreamCoveragePopupDisabledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerCoverageTestLocal;
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(400.0, 160.0), FVector2D(0.0, 60.0));
	UDreamButton* Locked = Card != nullptr ? PlaceButton(*this, Rig, TEXT("Locked"), Card, FVector2D(-90.0, 0.0), Listener.Get()) : nullptr;
	UDreamButton* Other = Locked != nullptr ? PlaceButton(*this, Rig, TEXT("Other"), Card, FVector2D(90.0, 0.0), nullptr) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (Other == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups))
	{
		return false;
	}
	Rig.EventSystem()->SetDoubleClickTime(0.0f);
	Locked->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!PushCard(*this, Popups, Card, Other->FaceNode.Get(), TEXT("The card goes up on the popup layer")))
	{
		return false;
	}
	Rig.PumpFrames(1);
	FDreamElementRef LockedElement = Rig.Driver()->Find(FDreamBy::Widget(Locked));

	TestTrue(TEXT("Clicking the disabled button inside the card completes"), LockedElement->Click());
	TestTrue(TEXT("The press on a disabled button inside the card was a press inside it: the card is still open"), Popups->IsOpen(Card));
	TestTrue(TEXT("A finger tapped on the disabled button completes"), LockedElement->Tap(0));
	TestTrue(TEXT("...and the tap too left the card open"), Popups->IsOpen(Card));
	TestEqual(TEXT("Neither clicked the disabled button"), Listener->ClickedCount, 0);

	Locked->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the button once it is enabled completes"), LockedElement->Click());
	TestEqual(TEXT("Enabled, the click clicked it once"), Listener->ClickedCount, 1);
	TestTrue(TEXT("...and the card is still open"), Popups->IsOpen(Card));
	Popups->Dismiss(Card);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoveragePopupDisabledTabTest,
	"DreamGUI.Popup.TabInACyclingPopupPassesOverItsDisabledButtonAndStopsOnItOnceItIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoveragePopupDisabledTabTest, "DreamGUI.Popup.TabInACyclingPopupPassesOverItsDisabledButtonAndStopsOnItOnceItIsEnabled", "[Nav][Disabled]")

/*
 * A card that keeps Tab, with three buttons, the middle one disabled, the focus on the first. Tab goes to the last, and on
 * round to the first; Shift+Tab from the first goes back round to the last -- the middle one is never a stop. Enabled again,
 * Tab from the first stops on it. The card stays open throughout.
 */
bool FDreamCoveragePopupDisabledTabTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(500.0, 160.0), FVector2D(0.0, 60.0));
	UDreamButton* First = Card != nullptr ? PlaceButton(*this, Rig, TEXT("First"), Card, FVector2D(-160.0, 0.0), nullptr) : nullptr;
	UDreamButton* Middle = First != nullptr ? PlaceButton(*this, Rig, TEXT("Middle"), Card, FVector2D(0.0, 0.0), nullptr) : nullptr;
	UDreamButton* Last = Middle != nullptr ? PlaceButton(*this, Rig, TEXT("Last"), Card, FVector2D(160.0, 0.0), nullptr) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (Last == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups))
	{
		return false;
	}
	Middle->SetIsEnabled(false);
	Rig.PumpFrames(1);
	if (!PushCard(*this, Popups, Card, First->FaceNode.Get(), TEXT("The card goes up on the popup layer")))
	{
		return false;
	}
	Rig.PumpFrames(1);
	if (!TestEqual(TEXT("The card took the focus onto its first button"), FocusOf(Rig), First->FaceNode.Get()))
	{
		Popups->Dismiss(Card);
		return false;
	}

	TestTrue(TEXT("Pressing Tab completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestEqual(TEXT("Tab passed over the disabled button to the last"), FocusOf(Rig), Last->FaceNode.Get());
	TestTrue(TEXT("Pressing Tab again completes"), Rig.Driver()->Sequence().Tab().Perform());
	TestEqual(TEXT("...and from the last, round to the first"), FocusOf(Rig), First->FaceNode.Get());
	TestTrue(TEXT("Pressing Shift+Tab completes"), Rig.Driver()->Sequence().ShiftTab().Perform());
	TestEqual(TEXT("Shift+Tab from the first goes back round to the last, past the disabled one"), FocusOf(Rig), Last->FaceNode.Get());

	Middle->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Pressing Tab twice completes"), Rig.Driver()->Sequence().Tab().Tab().Perform());
	TestEqual(TEXT("Enabled again, the middle button is the stop after the first"), FocusOf(Rig), Middle->FaceNode.Get());
	TestTrue(TEXT("The card is still open"), Popups->IsOpen(Card));
	Popups->Dismiss(Card);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoveragePopupScaledPressTest,
	"DreamGUI.Popup.OnACanvasScaledToHalfAndToDoubleAPressInsideTheCardReachesItsButtonAndAPressOutsideClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoveragePopupScaledPressTest, "DreamGUI.Popup.OnACanvasScaledToHalfAndToDoubleAPressInsideTheCardReachesItsButtonAndAPressOutsideClosesIt", "[Pointer][Touch][Scaled]")

/*
 * At half and at double: a click and then a finger's tap on the card's button click it and leave the card open; a click
 * below the card, as drawn, closes it. Pushed again, a finger's tap below it closes it too.
 */
bool FDreamCoveragePopupScaledPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerCoverageTestLocal;
	for (const double Scale : Scales)
	{
		TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(300.0, 120.0), FVector2D(0.0, 40.0));
		UDreamButton* Inside = Card != nullptr ? PlaceButton(*this, Rig, TEXT("Inside"), Card, FVector2D::ZeroVector, Listener.Get()) : nullptr;
		UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
		if (Inside == nullptr || !TestNotNull(*At(Scale, TEXT("The world has a popup layer")), Popups))
		{
			return false;
		}
		Rig.EventSystem()->SetDoubleClickTime(0.0f);
		Rig.PumpFrames(1);
		if (!PushCard(*this, Popups, Card, Inside->FaceNode.Get(), At(Scale, TEXT("The card goes up on the popup layer"))))
		{
			return false;
		}
		Rig.PumpFrames(1);
		const TOptional<FBox2D> CardRect = Rig.Driver()->Find(FDreamBy::Widget(Card))->GetPixelRect();
		if (!TestTrue(At(Scale, TEXT("The card is on the viewport")), CardRect.IsSet()))
		{
			Popups->Dismiss(Card);
			return false;
		}
		const FVector2D BelowTheCard(CardRect->GetCenter().X, CardRect->Max.Y + 60.0);
		FDreamElementRef InsideElement = Rig.Driver()->Find(FDreamBy::Widget(Inside));

		TestTrue(At(Scale, TEXT("Clicking the card's button completes")), InsideElement->Click());
		TestTrue(At(Scale, TEXT("A finger tapped on it completes")), InsideElement->Tap(0));
		TestEqual(*At(Scale, TEXT("The click and the tap each clicked the card's button")), Listener->ClickedCount, 2);
		TestTrue(At(Scale, TEXT("...and the card is still open")), Popups->IsOpen(Card));

		TestTrue(At(Scale, TEXT("Clicking below the card completes")), Rig.Driver()->Sequence().MoveToPixel(BelowTheCard).Press().Release().Perform());
		TestFalse(At(Scale, TEXT("The click below the card closed it")), Popups->IsOpen(Card));

		Rig.PumpFrames(1);
		if (!PushCard(*this, Popups, Card, Inside->FaceNode.Get(), At(Scale, TEXT("The card goes up again"))))
		{
			return false;
		}
		Rig.PumpFrames(1);
		TestTrue(At(Scale, TEXT("A finger tapped below the card completes")), Rig.Driver()->Sequence().TouchDown(1, BelowTheCard).TouchUp(1).Perform());
		TestFalse(At(Scale, TEXT("The tap below the card closed it")), Popups->IsOpen(Card));
		TestEqual(*At(Scale, TEXT("...and neither press outside clicked its button")), Listener->ClickedCount, 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoveragePopupScaledPadTest,
	"DreamGUI.Popup.OnACanvasScaledToHalfAndToDoubleTheDPadMovesBetweenTheCardsButtonsAndBackClosesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoveragePopupScaledPadTest, "DreamGUI.Popup.OnACanvasScaledToHalfAndToDoubleTheDPadMovesBetweenTheCardsButtonsAndBackClosesIt", "[Nav][Scaled]")

/*
 * At half and at double: a card with a button on the left, which takes the focus, and one on the right. The D-pad right
 * moves the focus to the right-hand button, the confirm clicks it and leaves the card open, and Back closes the card.
 */
bool FDreamCoveragePopupScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerCoverageTestLocal;
	for (const double Scale : Scales)
	{
		TStrongObjectPtr<UDreamPressInteractionListener> RightListener(NewObject<UDreamPressInteractionListener>());
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(360.0, 120.0), FVector2D(0.0, 40.0));
		UDreamButton* Left = Card != nullptr ? PlaceButton(*this, Rig, TEXT("Left"), Card, FVector2D(-90.0, 0.0), nullptr) : nullptr;
		UDreamButton* Right = Left != nullptr ? PlaceButton(*this, Rig, TEXT("Right"), Card, FVector2D(90.0, 0.0), RightListener.Get()) : nullptr;
		UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
		if (Right == nullptr || !TestNotNull(*At(Scale, TEXT("The world has a popup layer")), Popups))
		{
			return false;
		}
		Rig.PumpFrames(1);
		if (!PushCard(*this, Popups, Card, Left->FaceNode.Get(), At(Scale, TEXT("The card goes up on the popup layer"))))
		{
			return false;
		}
		Rig.PumpFrames(1);
		if (!TestEqual(*At(Scale, TEXT("The card took the focus onto its left button")), FocusOf(Rig), Left->FaceNode.Get()))
		{
			Popups->Dismiss(Card);
			return false;
		}

		TestTrue(At(Scale, TEXT("The D-pad right completes")), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Right).WaitFrames(1).Perform());
		TestEqual(*At(Scale, TEXT("The D-pad moved the focus to the right-hand button")), FocusOf(Rig), Right->FaceNode.Get());
		TestTrue(At(Scale, TEXT("Pressing and releasing the pad's confirm completes")),
			Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
		TestEqual(*At(Scale, TEXT("The confirm clicked the right-hand button")), RightListener->ClickedCount, 1);
		TestTrue(At(Scale, TEXT("...and the card is still open")), Popups->IsOpen(Card));
		TestTrue(At(Scale, TEXT("Pressing Back completes")), Rig.Driver()->Sequence().Back().Perform());
		TestFalse(At(Scale, TEXT("Back closed the card")), Popups->IsOpen(Card));
	}
	return true;
}

#endif
