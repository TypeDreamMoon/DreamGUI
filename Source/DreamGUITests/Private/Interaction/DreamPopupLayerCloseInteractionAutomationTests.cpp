// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamDialog.h"
#include "Core/DreamUIInputServices.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamUIModal.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/UIButton.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * What an owner is told as its popup closes, and why: UDreamUIPopupLayer's OnClosing and OnDismissed, with the reason, for a
 * popup closed by the player's own input -- a press outside, Back, a modal brought up by a click.
 *
 * The layer is the Slate menu stack's counterpart (Slate/Private/Framework/Application/MenuStack.cpp): a press outside every
 * menu dismisses them, Escape dismisses the top one, and dismissing several goes newest first, children before parents
 * (FMenuStack::DismissInternal, :667-676). A menu's owner hears of the dismissal (IMenu::GetOnMenuDismissed); the layer says
 * it twice -- OnClosing as the popup starts to close, while the focus is still in it, then OnDismissed once it has gone -- and
 * each time with the reason.
 */
namespace DreamPopupLayerCloseTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const FVector2D FarFromEverything(1200.0, 680.0);

	/** One thing a popup's owner was told. */
	struct FCloseEntry
	{
		bool bClosing = false;
		const UDreamWidget* Popup = nullptr;
		EDreamPopupDismissReason Reason = EDreamPopupDismissReason::Explicit;
		/** Whether the layer still had the popup open when it said so. */
		bool bOpenOnLayer = false;
	};

	using FCloseLog = TSharedRef<TArray<FCloseEntry>>;

	/** Params for InPopup whose two callbacks write to InLog. */
	FDreamPopupParams ParamsFor(UDreamWidget* InPopup, UDreamWidget* InOpener, const UDreamUIPopupLayer* InLayer, const FCloseLog& InLog)
	{
		FDreamPopupParams Params;
		Params.Popup = InPopup;
		Params.Opener = InOpener;
		Params.UserIndex = 0;
		Params.OnClosing = FDreamPopupDismissedDelegate::CreateLambda([InLayer, InLog](UDreamWidget* InClosing, EDreamPopupDismissReason InReason)
		{
			InLog->Add({ true, InClosing, InReason, InLayer->IsOpen(InClosing) });
		});
		Params.OnDismissed = FDreamPopupDismissedDelegate::CreateLambda([InLayer, InLog](UDreamWidget* InDismissed, EDreamPopupDismissReason InReason)
		{
			InLog->Add({ false, InDismissed, InReason, InLayer->IsOpen(InDismissed) });
		});
		return Params;
	}

	/** Whether InLog says exactly: InPopup closing for InReason, then dismissed for it -- starting at InAt. */
	bool SaysClosedFor(const TArray<FCloseEntry>& InLog, int32 InAt, const UDreamWidget* InPopup, EDreamPopupDismissReason InReason)
	{
		return InLog.IsValidIndex(InAt + 1)
			&& InLog[InAt].bClosing && InLog[InAt].Popup == InPopup && InLog[InAt].Reason == InReason
			&& !InLog[InAt + 1].bClosing && InLog[InAt + 1].Popup == InPopup && InLog[InAt + 1].Reason == InReason;
	}

	UDreamButton* PlaceButton(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const TCHAR* InName, UDreamWidget* InParent, const FVector2D& InPosition)
	{
		UDreamButton* Button = InRig.MakeControl<UDreamButton>(InName, InParent, FVector2D(160.0, 40.0), InPosition);
		InTest.TestTrue(*FString::Printf(TEXT("A button '%s' with a face can be made"), InName), Button != nullptr && Button->FaceNode != nullptr);
		return Button != nullptr && Button->FaceNode != nullptr ? Button : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupOutsidePressReasonTest,
	"DreamGUI.Popup.APressOutsideTellsThePopupItIsClosingThenThatItClosedBothForAnOutsideClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupOutsidePressReasonTest, "DreamGUI.Popup.APressOutsideTellsThePopupItIsClosingThenThatItClosedBothForAnOutsideClick", "[Pointer][Animated]")

/*
 * A card on the popup layer with a button in it that has the focus; a click far from it. The owner is told twice, in order:
 * closing -- the card already off the stack, the focus still in it -- then dismissed, both for an outside click. Nothing
 * else is said.
 */
bool FDreamPopupOutsidePressReasonTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerCloseTestLocal;
	// What the focus was on while the card was closing: read in OnClosing, before the focus is given back. Declared before
	// the rig, so a card the rig's teardown closes writes into something still alive.
	UDreamWidget* FocusWhileClosing = nullptr;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(400.0, 200.0), FVector2D(0.0, 100.0));
	UDreamButton* Inside = Card != nullptr ? PlaceButton(*this, Rig, TEXT("Inside"), Card, FVector2D::ZeroVector) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	UDreamUIInputServices* Services = UDreamUIInputServices::Get(Rig.GetWorld());
	if (Inside == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups) || !TestNotNull(TEXT("...and input"), Services))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const FCloseLog Log = MakeShared<TArray<FCloseEntry>>();
	FDreamPopupParams Params = ParamsFor(Card, nullptr, Popups, Log);
	Params.InitialFocus = Inside->FaceNode.Get();
	const FDreamPopupDismissedDelegate Recorder = Params.OnClosing;
	Params.OnClosing = FDreamPopupDismissedDelegate::CreateLambda([Recorder, Services, &FocusWhileClosing](UDreamWidget* InPopup, EDreamPopupDismissReason InReason)
	{
		FocusWhileClosing = Services->GetFocusedWidget(0);
		Recorder.ExecuteIfBound(InPopup, InReason);
	});
	if (!TestTrue(TEXT("The card goes up on the popup layer"), Popups->Push(Params)))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking far from the card completes"), Rig.Driver()->Sequence().MoveToPixel(FarFromEverything).Press().Release().Perform());

	TestFalse(TEXT("The press outside closed the card"), Popups->IsOpen(Card));
	if (TestEqual(TEXT("The owner was told twice"), Log->Num(), 2))
	{
		TestTrue(TEXT("Closing, then dismissed, both for an outside click"), SaysClosedFor(*Log, 0, Card, EDreamPopupDismissReason::OutsideClick));
		TestFalse(TEXT("As it was closing the card was already off the stack"), (*Log)[0].bOpenOnLayer);
	}
	TestTrue(TEXT("...and the focus was still in it"),
		FocusWhileClosing != nullptr && (FocusWhileClosing == Card || FocusWhileClosing->IsChildOf(Card)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupBackReasonTest,
	"DreamGUI.Popup.BackClosesTheTopPopupForBackAndLeavesThePopupItWasOpenedFromOpen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupBackReasonTest, "DreamGUI.Popup.BackClosesTheTopPopupForBackAndLeavesThePopupItWasOpenedFromOpen", "[Nav][Animated]")

/*
 * A card, and a second card opened from a button inside the first -- the first card's child. Back closes the top one only, as
 * Escape takes the top menu off the Slate menu stack: the child is told closing and dismissed for Back, the parent hears
 * nothing and stays open. A second Back closes the parent, for Back.
 */
bool FDreamPopupBackReasonTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerCloseTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Parent = Rig.MakeWidget(TEXT("Parent"), nullptr, FVector2D(400.0, 200.0), FVector2D(0.0, 150.0));
	UDreamButton* More = Parent != nullptr ? PlaceButton(*this, Rig, TEXT("More"), Parent, FVector2D::ZeroVector) : nullptr;
	UDreamWidget* Child = More != nullptr ? Rig.MakeWidget(TEXT("Child"), nullptr, FVector2D(300.0, 150.0), FVector2D(0.0, -150.0)) : nullptr;
	UDreamButton* Deeper = Child != nullptr ? PlaceButton(*this, Rig, TEXT("Deeper"), Child, FVector2D::ZeroVector) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	if (Deeper == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const FCloseLog Log = MakeShared<TArray<FCloseEntry>>();
	FDreamPopupParams ParentParams = ParamsFor(Parent, nullptr, Popups, Log);
	ParentParams.InitialFocus = More->FaceNode.Get();
	FDreamPopupParams ChildParams = ParamsFor(Child, More->FaceNode.Get(), Popups, Log);
	ChildParams.InitialFocus = Deeper->FaceNode.Get();
	if (!TestTrue(TEXT("The parent card goes up"), Popups->Push(ParentParams))
		|| !TestTrue(TEXT("...and the child card, opened from the parent's button"), Popups->Push(ChildParams)))
	{
		return false;
	}
	Rig.PumpFrames(1);
	TArray<UDreamWidget*> Open;
	Popups->GetOpenPopups(0, Open);
	if (!TestEqual(TEXT("Both cards are open, the child on top"), Open.Num(), 2) || !TestEqual(TEXT("...the child on top"), Popups->GetTopPopup(0), Child))
	{
		return false;
	}

	TestTrue(TEXT("Pressing Back completes"), Rig.Driver()->Sequence().Back().Perform());
	TestFalse(TEXT("Back closed the child card"), Popups->IsOpen(Child));
	TestTrue(TEXT("...and left the parent open"), Popups->IsOpen(Parent));
	if (TestEqual(TEXT("Only the child's owner was told, twice"), Log->Num(), 2))
	{
		TestTrue(TEXT("Closing, then dismissed, for Back"), SaysClosedFor(*Log, 0, Child, EDreamPopupDismissReason::Back));
	}

	TestTrue(TEXT("Pressing Back again completes"), Rig.Driver()->Sequence().Back().Perform());
	TestFalse(TEXT("The second Back closed the parent card"), Popups->IsOpen(Parent));
	if (TestEqual(TEXT("...whose owner was told twice more"), Log->Num(), 4))
	{
		TestTrue(TEXT("Closing, then dismissed, for Back"), SaysClosedFor(*Log, 2, Parent, EDreamPopupDismissReason::Back));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPopupModalReplacesTest,
	"DreamGUI.Popup.AModalBroughtUpByAClickInAPopupClosesEveryPopupOfThePlayerNewestFirstAsReplaced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamPopupModalReplacesTest, "DreamGUI.Popup.AModalBroughtUpByAClickInAPopupClosesEveryPopupOfThePlayerNewestFirstAsReplaced", "[Pointer][Animated]")

/*
 * A parent card and its child card, and in the child a button whose click asks the player something -- a modal dialog. A
 * modal comes up in front of all of the player's popups and closes them (DismissAll for the player): newest first, the child
 * before the parent, as the Slate menu stack dismisses (MenuStack.cpp:667-676), each owner told closing and then dismissed,
 * for Replaced. The click itself reached the button: the press was inside the top popup.
 */
bool FDreamPopupModalReplacesTest::RunTest(const FString& Parameters)
{
	using namespace DreamPopupLayerCloseTestLocal;
	// Before the rig, so the modal's answer -- which a teardown would give if the modal were still up -- lands in something
	// still alive.
	int32 AskClicks = 0;
	TArray<FName> Answers;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The headless rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamWidget* Parent = Rig.MakeWidget(TEXT("Parent"), nullptr, FVector2D(400.0, 200.0), FVector2D(0.0, 150.0));
	UDreamButton* More = Parent != nullptr ? PlaceButton(*this, Rig, TEXT("More"), Parent, FVector2D::ZeroVector) : nullptr;
	UDreamWidget* Child = More != nullptr ? Rig.MakeWidget(TEXT("Child"), nullptr, FVector2D(300.0, 150.0), FVector2D(0.0, -150.0)) : nullptr;
	UDreamButton* Ask =Child != nullptr ? PlaceButton(*this, Rig, TEXT("Ask"), Child, FVector2D::ZeroVector) : nullptr;
	UDreamUIPopupLayer* Popups = UDreamUIPopupLayer::Get(Rig.GetWorld());
	UDreamUIModalSubsystem* Modals = UDreamUIModalSubsystem::Get(Rig.GetWorld());
	if (Ask == nullptr || !TestNotNull(TEXT("The world has a popup layer"), Popups) || !TestNotNull(TEXT("...and modals"), Modals)
		|| !TestNotNull(TEXT("The button has its behaviour"), Ask->ButtonBehaviour.Get()))
	{
		return false;
	}
	Ask->ButtonBehaviour->GetOnClickEvent().AddLambda([Modals, &AskClicks, &Answers]()
	{
		++AskClicks;
		Modals->ShowModalNative(UDreamDialog::StaticClass(), [&Answers](FName InResult) { Answers.Add(InResult); }, 0);
	});
	Rig.PumpFrames(1);
	const FCloseLog Log = MakeShared<TArray<FCloseEntry>>();
	FDreamPopupParams ParentParams = ParamsFor(Parent, nullptr, Popups, Log);
	ParentParams.InitialFocus = More->FaceNode.Get();
	FDreamPopupParams ChildParams = ParamsFor(Child, More->FaceNode.Get(), Popups, Log);
	ChildParams.InitialFocus = Ask->FaceNode.Get();
	if (!TestTrue(TEXT("The parent card goes up"), Popups->Push(ParentParams))
		|| !TestTrue(TEXT("...and the child card, opened from the parent's button"), Popups->Push(ChildParams)))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the button in the child card completes"), Rig.Driver()->Find(FDreamBy::Widget(Ask))->Click());
	Rig.PumpFrames(2);
	TestEqual(TEXT("The click reached the button in the top popup"), AskClicks, 1);
	TestTrue(TEXT("...which brought a modal up"), Modals->IsModalActive(0));
	TestFalse(TEXT("The child card is closed"), Popups->IsOpen(Child));
	TestFalse(TEXT("...and the parent"), Popups->IsOpen(Parent));
	if (TestEqual(TEXT("Each owner was told twice"), Log->Num(), 4))
	{
		TestTrue(TEXT("The child first: closing, then dismissed, as replaced"), SaysClosedFor(*Log, 0, Child, EDreamPopupDismissReason::Replaced));
		TestTrue(TEXT("Then the parent: closing, then dismissed, as replaced"), SaysClosedFor(*Log, 2, Parent, EDreamPopupDismissReason::Replaced));
	}

	// Put the question away, so the rig comes down with nothing up.
	TestTrue(TEXT("Pressing Back completes"), Rig.Driver()->Sequence().Back().Perform());
	TestFalse(TEXT("Back answered the modal"), Modals->IsModalActive(0));
	TestEqual(TEXT("...once"), Answers.Num(), 1);
	return true;
}

#endif
