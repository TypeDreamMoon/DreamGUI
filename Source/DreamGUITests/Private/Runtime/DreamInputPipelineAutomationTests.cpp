// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamTextInput.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUIManager.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "DreamDragDropTestTypes.h"
#include "DreamInputPipelineTestTypes.h"
#include "DreamNavigationTestTypes.h"
#include "DreamPlayerScreenTestTypes.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "Event/DreamUIKeyRouting.h"
#include "GameFramework/Actor.h"
#include "GenericPlatform/GenericApplication.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUIInputAction.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUITooltip.h"
#include "Interaction/UITextInput.h"
#include "UObject/StrongObjectPtr.h"

/*
 * ONE PIPELINE PER PLAYER, AND WHAT IT PROMISES.
 *
 * Every player's input is its own (UDreamUIInputUser): its pointers, what they hover and press, its drags and its
 * tooltip. Whatever a handler does in the middle of an event -- lets every pointer go, turns tracing off, retires a
 * pointer, moves focus, points the event system at another player, destroys the widget it was handed -- it does it
 * after the event, not inside it, and every Enter still gets one Exit, every Down one Up. An input source that goes
 * away lets go of what its player hovered and pressed. A pointer at rest over UI that has not changed is not traced
 * again. Two pointers pressed through one raycaster drag along their own rays.
 */

namespace DreamInputPipelineTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	FVector2D CentreOf(const UDreamWidget* InWidget)
	{
		const TOptional<FVector2D> Pixel = FDreamDriverProjection::WidgetCentrePixel(InWidget);
		return Pixel.IsSet() ? Pixel.GetValue() : FVector2D::ZeroVector;
	}

	/**
	 * Hover InAt, click it, press it again and drag well past any threshold, release, then leave for InAway -- a frame
	 * each. Every moment a handler can act from comes up once: Enter, Down, Up, Click, BeginDrag, Drag, Exit.
	 */
	void HoverPressDragReleaseLeave(FDreamDriverRig& InRig, UDreamDriverInputModule* InModule, const FVector2D& InAt, const FVector2D& InAway)
	{
		InModule->MoveTo(InAt);
		InRig.PumpFrames(1);
		InModule->Press();
		InRig.PumpFrames(1);
		InModule->Release();
		InRig.PumpFrames(1);
		InModule->Press();
		InRig.PumpFrames(1);
		InModule->MoveTo(InAt + FVector2D(40.0, 0.0));
		InRig.PumpFrames(1);
		InModule->MoveTo(InAt + FVector2D(60.0, 0.0));
		InRig.PumpFrames(1);
		InModule->Release();
		InRig.PumpFrames(1);
		InModule->MoveTo(InAway);
		InRig.PumpFrames(1);
	}

	/** A second player on the rig's world: an event system for it, a module feeding it, a screen raycaster for it on the rig's canvas. */
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

	struct FMatrixAction
	{
		const TCHAR* Name;
		TFunction<void(FDreamDriverRig&, UDreamWidget* /*Target*/, UDreamWidget* /*Other*/)> Run;
		bool bDestroysTarget = false;
	};

	const TCHAR* NameOf(EDreamUIPointerEventType InEvent)
	{
		switch (InEvent)
		{
		case EDreamUIPointerEventType::Enter: return TEXT("Enter");
		case EDreamUIPointerEventType::Exit: return TEXT("Exit");
		case EDreamUIPointerEventType::Down: return TEXT("Down");
		case EDreamUIPointerEventType::Up: return TEXT("Up");
		case EDreamUIPointerEventType::Click: return TEXT("Click");
		case EDreamUIPointerEventType::BeginDrag: return TEXT("BeginDrag");
		case EDreamUIPointerEventType::Drag: return TEXT("Drag");
		default: return TEXT("?");
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputReentrancyMatrixTest,
	"DreamGUI.Input.Pipeline.WhateverAHandlerDoesToThePointersEveryEnterGetsOneExitAndEveryDownOneUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputReentrancyMatrixTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamEventSystem* Events = Rig.EventSystem();
	UDreamDriverInputModule* Module = Rig.InputModule();
	// The second press of the script is a press, not the second half of a double click.
	Events->SetDoubleClickTime(0.0f);

	const TArray<FMatrixAction> Actions = {
		{ TEXT("letting every pointer go"), [](FDreamDriverRig& R, UDreamWidget*, UDreamWidget*) { R.EventSystem()->ClearEvent(); } },
		{ TEXT("turning tracing off with a clear"), [](FDreamDriverRig& R, UDreamWidget*, UDreamWidget*) { R.EventSystem()->SetRaycastEnable(false, true); } },
		{ TEXT("retiring another pointer"), [](FDreamDriverRig& R, UDreamWidget*, UDreamWidget*) { R.EventSystem()->RemovePointerEventData(DreamUIPointerIds::ScriptBase); } },
		{ TEXT("moving focus elsewhere"), [](FDreamDriverRig& R, UDreamWidget*, UDreamWidget* Other)
			{
				if (UDreamUIInputServices* Services = UDreamUIInputServices::Get(R.GetWorld()))
				{
					Services->SetFocus(Other, 0, 0);
				}
			} },
		{ TEXT("pointing the event system at another player"), [](FDreamDriverRig& R, UDreamWidget*, UDreamWidget*) { R.EventSystem()->SetUserIndex(1); } },
		{ TEXT("destroying the widget it was handed"), [](FDreamDriverRig&, UDreamWidget* Target, UDreamWidget*)
			{
				if (IsValid(Target))
				{
					Target->DestroyWidget();
				}
			}, true },
	};
	const EDreamUIPointerEventType Moments[] = {
		EDreamUIPointerEventType::Enter, EDreamUIPointerEventType::Exit, EDreamUIPointerEventType::Down,
		EDreamUIPointerEventType::Up, EDreamUIPointerEventType::Click, EDreamUIPointerEventType::BeginDrag,
		EDreamUIPointerEventType::Drag,
	};

	for (const FMatrixAction& Act : Actions)
	{
		for (const EDreamUIPointerEventType Moment : Moments)
		{
			const FString Case = FString::Printf(TEXT("%s from inside %s"), Act.Name, NameOf(Moment));
			UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(200.0, 100.0), FVector2D(-250.0, 0.0));
			UDreamWidget* Other = Rig.MakeWidget(TEXT("Other"), nullptr, FVector2D(200.0, 100.0), FVector2D(250.0, 0.0));
			UDreamPointerLedger* Ledger = IsValid(Target) ? Target->AddComponent<UDreamPointerLedger>() : nullptr;
			if (!TestTrue(*FString::Printf(TEXT("%s: the widgets were made"), *Case), Ledger != nullptr && IsValid(Other)))
			{
				return false;
			}
			// The pointer the retiring action takes away.
			if (UDreamUIInputUser* User = Events->GetInputUser())
			{
				User->GetPointerEventData(DreamUIPointerIds::ScriptBase, true);
			}
			// Armed before the first frame: whatever the first frame brings is a moment the action can run from.
			const TWeakObjectPtr<UDreamWidget> WeakTarget = Target;
			const TWeakObjectPtr<UDreamWidget> WeakOther = Other;
			FDreamDriverRig* RigPtr = &Rig;
			Ledger->ActFrom(Moment, [RigPtr, Act, WeakTarget, WeakOther](UDreamPointerEventData*)
			{
				Act.Run(*RigPtr, WeakTarget.Get(), WeakOther.Get());
			});
			Rig.PumpFrames(1);
			const FVector2D At = CentreOf(Target);
			const FVector2D Away = CentreOf(Other) + FVector2D(0.0, 200.0);

			HoverPressDragReleaseLeave(Rig, Module, At, Away);

			TestTrue(*FString::Printf(TEXT("%s: the action ran"), *Case), Ledger->bActed);
			if (!Act.bDestroysTarget)
			{
				TestEqual(*FString::Printf(TEXT("%s: every Enter got one Exit"), *Case), Ledger->Exit, Ledger->Enter);
				TestEqual(*FString::Printf(TEXT("%s: every Down got one Up"), *Case), Ledger->Up, Ledger->Down);
				TestEqual(*FString::Printf(TEXT("%s: every drag begun was ended"), *Case), Ledger->EndDrag, Ledger->BeginDrag);
			}
			const UDreamPointerEventData* Mouse = Events->GetInputUser() != nullptr ? Events->GetInputUser()->FindPointerEventData(DreamUIPointerIds::Mouse) : nullptr;
			TestFalse(*FString::Printf(TEXT("%s: the pointer is not left pressed"), *Case), Mouse != nullptr && Mouse->bNowIsTriggerPressed);

			// Back to where every case starts: the widgets gone, tracing on, the rig's player 0, nothing held.
			if (IsValid(Target))
			{
				Target->DestroyWidget();
			}
			if (IsValid(Other))
			{
				Other->DestroyWidget();
			}
			Events->SetUserIndex(0);
			Events->SetRaycastEnable(true);
			Events->ClearEvent();
			Rig.PumpFrames(1);
			// And the pointer somewhere nothing is, so the next case's widgets are not under it from their first frame.
			Module->MoveTo(FVector2D(8.0, 8.0));
			Rig.PumpFrames(1);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputSourceGoesTest,
	"DreamGUI.Input.Pipeline.WhenItsInputGoesAwayAPlayerLetsGoOfWhatItHoveredAndPressed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputSourceGoesTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	UDreamEventSystem* Events = Rig.EventSystem();
	UDreamDriverInputModule* Module = Rig.InputModule();
	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(200.0, 100.0));
	UDreamPointerLedger* Ledger = IsValid(Target) ? Target->AddComponent<UDreamPointerLedger>() : nullptr;
	if (!TestTrue(TEXT("An input subsystem and a widget to hover"), Input != nullptr && Ledger != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);
	const FVector2D At = CentreOf(Target);
	auto HoverAndHold = [&]()
	{
		Module->MoveTo(At);
		Rig.PumpFrames(1);
		Module->Press();
		Rig.PumpFrames(1);
	};

	// The event system is taken away while its player hovers and holds a press: the level it was placed in is
	// unloading, say. It used to unregister and nothing else -- the widget stayed hovered and pressed for good.
	HoverAndHold();
	TestEqual(TEXT("Hovered"), Ledger->Enter, 1);
	TestEqual(TEXT("...and pressed"), Ledger->Down, 1);
	Input->RemoveEventSystem(Events);
	TestEqual(TEXT("Its input gone, the player exits what it hovered"), Ledger->Exit, 1);
	TestEqual(TEXT("...and lets go of what it pressed"), Ledger->Up, 1);
	TestEqual(TEXT("...which is not a click"), Ledger->Click, 0);
	Input->AddEventSystem(Events);
	Module->Release();
	Rig.PumpFrames(1);

	// Moved to another player in the middle of the same.
	HoverAndHold();
	TestEqual(TEXT("Hovered again"), Ledger->Enter, 2);
	Events->SetUserIndex(1);
	TestEqual(TEXT("Pointed at another player, the old one exits what it hovered"), Ledger->Exit, 2);
	TestEqual(TEXT("...and lets go of what it pressed"), Ledger->Up, 2);
	Events->SetUserIndex(0);
	Module->Release();
	Rig.PumpFrames(1);

	// And the world's input torn down in the middle of it, as a level ends: the same, while the world is whole.
	HoverAndHold();
	TestEqual(TEXT("Hovered a third time"), Ledger->Enter, 3);
	Input->TeardownForWorld(*Rig.GetWorld());
	TestEqual(TEXT("The world's input ending exits what was hovered"), Ledger->Exit, 3);
	TestEqual(TEXT("...and lets go of what was pressed"), Ledger->Up, 3);
	TestEqual(TEXT("...none of it a click"), Ledger->Click, 0);
	// Nothing is dispatched afterwards.
	Module->Release();
	Rig.PumpFrames(2);
	TestEqual(TEXT("A torn-down world's input dispatches nothing more"), Ledger->Up, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputTwoPlayersTest,
	"DreamGUI.Input.Players.TwoPlayersOnOneScreenEachHoverPressDragAndGetTooltipsOfTheirOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputTwoPlayersTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	FSecondPlayer Second(Rig, 1);
	if (!TestTrue(TEXT("A second player with its own event system, module and raycaster"), Input != nullptr && Second.IsUsable()))
	{
		return false;
	}
	UDreamWidget* First = Rig.MakeWidget(TEXT("First"), nullptr, FVector2D(200.0, 100.0), FVector2D(-250.0, 0.0));
	UDreamWidget* Other = Rig.MakeWidget(TEXT("Other"), nullptr, FVector2D(200.0, 100.0), FVector2D(250.0, 0.0));
	UDreamPointerLedger* FirstLedger = IsValid(First) ? First->AddComponent<UDreamPointerLedger>() : nullptr;
	UDreamPointerLedger* OtherLedger = IsValid(Other) ? Other->AddComponent<UDreamPointerLedger>() : nullptr;
	UDreamUIDragSource* FirstSource = IsValid(First) ? First->AddComponent<UDreamUIDragSource>() : nullptr;
	UDreamUIDragSource* OtherSource = IsValid(Other) ? Other->AddComponent<UDreamUIDragSource>() : nullptr;
	if (!TestTrue(TEXT("Two widgets, each keeping books and each a drag source"),
		FirstLedger != nullptr && OtherLedger != nullptr && FirstSource != nullptr && OtherSource != nullptr))
	{
		return false;
	}
	First->SetToolTipText(FText::FromString(TEXT("First")));
	Other->SetToolTipText(FText::FromString(TEXT("Other")));
	Rig.PumpFrames(1);

	// Each player's mouse over its own widget.
	Rig.InputModule()->MoveTo(CentreOf(First));
	Second.Module->MoveTo(CentreOf(Other));
	Rig.PumpFrames(1);
	TestEqual(TEXT("Player 0 is over the first widget"), FirstLedger->Enter, 1);
	TestEqual(TEXT("...as player 0"), FirstLedger->LastUserIndex, 0);
	TestEqual(TEXT("Player 1 is over the other"), OtherLedger->Enter, 1);
	TestEqual(TEXT("...as player 1"), OtherLedger->LastUserIndex, 1);

	// Each player's tooltip, after the dwell, is its own.
	Rig.PumpFrames(45);
	if (UDreamUITooltipSubsystem* Tooltip = UDreamUITooltipSubsystem::Get(Rig.GetWorld()))
	{
		TestEqual(TEXT("Player 0's tooltip is the first widget's"), Tooltip->GetShownForUser(0), First);
		TestEqual(TEXT("Player 1's tooltip is the other's"), Tooltip->GetShownForUser(1), Other);
	}

	// Both press and drag.
	Rig.InputModule()->Press();
	Second.Module->Press();
	Rig.PumpFrames(1);
	TestEqual(TEXT("Player 0 pressed the first widget"), FirstLedger->Down, 1);
	TestEqual(TEXT("Player 1 pressed the other"), OtherLedger->Down, 1);
	Rig.InputModule()->MoveTo(CentreOf(First) + FVector2D(0.0, 60.0));
	Second.Module->MoveTo(CentreOf(Other) + FVector2D(0.0, 60.0));
	Rig.PumpFrames(2);
	UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(Rig.GetWorld());
	if (TestNotNull(TEXT("A drag-drop service"), DragDrop))
	{
		TestTrue(TEXT("Player 0 is dragging"), DragDrop->IsDragInProgressForUser(0));
		TestTrue(TEXT("Player 1 is dragging"), DragDrop->IsDragInProgressForUser(1));
		// Player 1's Escape: its own drag ends, player 0's goes on. Any player's Escape used to cancel player 0's.
		TestTrue(TEXT("Player 1's drag is cancelled"), DragDrop->CancelActiveDragForUser(1));
		TestFalse(TEXT("...and is over"), DragDrop->IsDragInProgressForUser(1));
		TestTrue(TEXT("...while player 0's goes on"), DragDrop->IsDragInProgressForUser(0));
	}
	Rig.InputModule()->Release();
	Second.Module->Release();
	Rig.PumpFrames(1);
	TestEqual(TEXT("Player 1's press was let go of once"), OtherLedger->Up, 1);
	TestEqual(TEXT("Player 0's press was let go of once"), FirstLedger->Up, 1);

	// A player joins later, and one leaves: the one leaving lets go of what it was over.
	Second.Module->MoveTo(CentreOf(Other));
	Rig.PumpFrames(1);
	const int32 OtherEnters = OtherLedger->Enter;
	Input->RemoveEventSystem(Second.EventSystem);
	Input->RemoveUser(1);
	TestNull(TEXT("Player 1 is gone"), Input->GetUser(1));
	TestEqual(TEXT("...having exited what it was over"), OtherLedger->Exit, OtherEnters);
	UDreamUIInputUser* Late = Input->GetOrCreateUser(2);
	TestTrue(TEXT("A player that joins later gets input of its own"), Late != nullptr && Late->GetUserIndex() == 2);
	TestTrue(TEXT("...one with no local player behind it here, a script player"), Late != nullptr && Late->IsScriptUser());
	TestEqual(TEXT("Player 0 was never touched by any of it"), FirstLedger->LastUserIndex, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputTraceCacheTest,
	"DreamGUI.Input.Pipeline.APointerAtRestOverUIThatHasNotChangedIsNotTracedAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputTraceCacheTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputUser* User = Rig.EventSystem()->GetInputUser();
	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(200.0, 100.0));
	UDreamPointerLedger* Ledger = IsValid(Target) ? Target->AddComponent<UDreamPointerLedger>() : nullptr;
	if (!TestTrue(TEXT("A player and a widget to rest over"), User != nullptr && Ledger != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const FVector2D At = CentreOf(Target);
	Rig.InputModule()->MoveTo(At);
	Rig.PumpFrames(3);
	TestEqual(TEXT("The pointer is over the widget"), Ledger->Enter, 1);

	// At rest over a screen that is not changing: every frame used to trace the whole canvas tree regardless.
	const int32 AtRest = User->GetLineTraceCount();
	Rig.PumpFrames(10);
	TestEqual(TEXT("A pointer at rest over UI that has not changed is not traced"), User->GetLineTraceCount(), AtRest);
	TestEqual(TEXT("...and is still over the widget"), Ledger->Exit, 0);

	// The UI moves under it: that is traced, and the pointer finds it has left.
	Target->SetAnchoredPosition(FVector2D(400.0, 0.0));
	Rig.PumpFrames(2);
	TestTrue(TEXT("UI that moved under a resting pointer is traced again"), User->GetLineTraceCount() > AtRest);
	TestEqual(TEXT("...and the pointer has left the widget that moved away"), Ledger->Exit, 1);

	// The pointer moves: traced.
	const int32 BeforeMove = User->GetLineTraceCount();
	Rig.InputModule()->MoveTo(CentreOf(Target));
	Rig.PumpFrames(1);
	TestTrue(TEXT("A pointer that moved is traced"), User->GetLineTraceCount() > BeforeMove);
	TestEqual(TEXT("...and finds the widget where it went"), Ledger->Enter, 2);

	// Pressed: a press, a drag and a long press read the trace every frame.
	Rig.InputModule()->Press();
	Rig.PumpFrames(1);
	const int32 Pressed = User->GetLineTraceCount();
	Rig.PumpFrames(3);
	TestEqual(TEXT("A held pointer is traced every frame"), User->GetLineTraceCount(), Pressed + 3);
	Rig.InputModule()->Release();
	Rig.PumpFrames(1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputRayPerPointerTest,
	"DreamGUI.Input.Pipeline.TwoPointersPressedThroughOneRaycasterEachDragAlongTheirOwnRay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputRayPerPointerTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputUser* User = Rig.EventSystem()->GetInputUser();
	UDreamWidget* Left = Rig.MakeWidget(TEXT("Left"), nullptr, FVector2D(200.0, 100.0), FVector2D(-300.0, 0.0));
	UDreamWidget* Right = Rig.MakeWidget(TEXT("Right"), nullptr, FVector2D(200.0, 100.0), FVector2D(300.0, 0.0));
	if (!TestTrue(TEXT("A player and two widgets"), User != nullptr && IsValid(Left) && IsValid(Right)))
	{
		return false;
	}
	Left->AddComponent<UDreamPointerLedger>();
	Right->AddComponent<UDreamPointerLedger>();
	Rig.PumpFrames(2);

	// Two fingers pressed through the rig's one screen raycaster, then moved apart.
	UDreamDriverInputModule* Module = Rig.InputModule();
	Module->TouchPress(1, CentreOf(Left));
	Module->TouchPress(2, CentreOf(Right));
	Rig.PumpFrames(1);
	Module->TouchMoveTo(1, CentreOf(Left) + FVector2D(30.0, 0.0));
	Module->TouchMoveTo(2, CentreOf(Right) + FVector2D(0.0, 30.0));
	Rig.PumpFrames(1);

	const UDreamPointerEventData* First = User->FindPointerEventData(UDreamStandaloneInputModule::GetTouchPointerID(1));
	const UDreamPointerEventData* Second = User->FindPointerEventData(UDreamStandaloneInputModule::GetTouchPointerID(2));
	if (!TestTrue(TEXT("Both fingers are pressed pointers"), First != nullptr && Second != nullptr
		&& First->bNowIsTriggerPressed && Second->bNowIsTriggerPressed))
	{
		return false;
	}
	TestTrue(TEXT("Both were pressed through the one raycaster"),
		First->PressRaycaster == Rig.Raycaster() && Second->PressRaycaster == Rig.Raycaster());
	const bool bSameRay = First->GetDragRayOrigin().Equals(Second->GetDragRayOrigin())
		&& First->GetDragRayDirection().Equals(Second->GetDragRayDirection());
	TestFalse(TEXT("Each drags along its own ray"), bSameRay);
	// The raycaster itself keeps only the ray it made last -- the second finger's, traced after the first -- which is
	// what the first finger's drag used to read.
	TestTrue(TEXT("The raycaster's own ray is the last finger's"),
		Second->GetDragRayOrigin().Equals(Rig.Raycaster()->GetRayOrigin()) && Second->GetDragRayDirection().Equals(Rig.Raycaster()->GetRayDirection()));
	TestFalse(TEXT("...which the first finger no longer reads"),
		First->GetDragRayOrigin().Equals(Rig.Raycaster()->GetRayOrigin()) && First->GetDragRayDirection().Equals(Rig.Raycaster()->GetRayDirection()));

	Module->TouchRelease(1, CentreOf(Left));
	Module->TouchRelease(2, CentreOf(Right));
	Rig.PumpFrames(1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputFocusOutlivesTheFingerTest,
	"DreamGUI.Input.Focus.AFieldTappedAndLetGoOfKeepsTheFocusSoBackEndsItsEditAndATapElsewhereTakesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputFocusOutlivesTheFingerTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputUser* User = Rig.EventSystem()->GetInputUser();
	UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Name"), nullptr, FVector2D(320.0, 40.0), FVector2D(-250.0, 0.0));
	UDreamWidget* Elsewhere = Rig.MakeWidget(TEXT("Elsewhere"), nullptr, FVector2D(200.0, 100.0), FVector2D(250.0, 0.0));
	if (!TestTrue(TEXT("A player, a field and somewhere else to tap"),
		User != nullptr && Field != nullptr && Field->InputBehaviour != nullptr && IsValid(Elsewhere)))
	{
		return false;
	}
	Rig.PumpFrames(2);
	UDreamDriverInputModule* Module = Rig.InputModule();
	const FVector2D At = CentreOf(Field);

	// A tap: a finger down on the field and up again -- which takes the finger's pointer away with it.
	Module->TouchPress(0, At);
	Rig.PumpFrames(1);
	Module->TouchRelease(0, At);
	Rig.PumpFrames(1);
	const UDreamWidget* FieldWidget = Field->InputBehaviour->GetWidget();
	TestNull(TEXT("The lifted finger's pointer is gone"), User->FindPointerEventData(UDreamStandaloneInputModule::GetTouchPointerID(0)));
	TestTrue(TEXT("The tap began an edit"), Field->InputBehaviour->IsInputActive());
	// The finger took its own record of the focus with it; the player's stays.
	TestTrue(TEXT("...and the field has the player's focus, though the finger that gave it is gone"),
		FieldWidget != nullptr && User->GetFocusedWidget() == FieldWidget);
	TestTrue(TEXT("...which the widget's own query answers too"), FieldWidget != nullptr && FieldWidget->HasFocus());
	TestTrue(TEXT("...and the field owns the player's keyboard"),
		UUITextInput::GetActiveTextInputForPlayer(Rig.GetWorld(), 0) == Field->InputBehaviour.Get());

	// Back ends the edit, because it asks the player's focus -- which is still the field.
	UDreamUINavigationStack* Stack = UDreamUINavigationStack::Get(Rig.GetWorld());
	TestTrue(TEXT("Back is taken"), Stack != nullptr && Stack->HandleBack(0));
	TestFalse(TEXT("...by ending the edit"), Field->InputBehaviour->IsInputActive());
	TestNull(TEXT("...which lets go of the player's keyboard"), UUITextInput::GetActiveTextInputForPlayer(Rig.GetWorld(), 0));

	// A tap on something else, with another finger that has no record of any focus: the player's goes.
	const FVector2D There = CentreOf(Elsewhere);
	Module->TouchPress(1, There);
	Rig.PumpFrames(1);
	Module->TouchRelease(1, There);
	Rig.PumpFrames(1);
	TestTrue(TEXT("A tap on something else takes the field's focus away"), User->GetFocusedWidget() != FieldWidget);
	TestFalse(TEXT("...and the widget no longer has it"), FieldWidget != nullptr && FieldWidget->HasFocus());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputTwoPlayersTypeTest,
	"DreamGUI.Input.Players.TwoPlayersEachTypeIntoTheFieldTheyClicked",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputTwoPlayersTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	FSecondPlayer Second(Rig, 1);
	UDreamTextInput* First = Rig.MakeControl<UDreamTextInput>(TEXT("FirstName"), nullptr, FVector2D(320.0, 40.0), FVector2D(-250.0, 0.0));
	UDreamTextInput* Other = Rig.MakeControl<UDreamTextInput>(TEXT("OtherName"), nullptr, FVector2D(320.0, 40.0), FVector2D(250.0, 0.0));
	if (!TestTrue(TEXT("Two players and a field for each"), Input != nullptr && Second.IsUsable()
		&& First != nullptr && First->InputBehaviour != nullptr && Other != nullptr && Other->InputBehaviour != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(2);

	// Each player clicks their own field.
	Rig.InputModule()->MoveTo(CentreOf(First));
	Second.Module->MoveTo(CentreOf(Other));
	Rig.PumpFrames(1);
	Rig.InputModule()->Press();
	Second.Module->Press();
	Rig.PumpFrames(1);
	Rig.InputModule()->Release();
	Second.Module->Release();
	Rig.PumpFrames(1);
	TestTrue(TEXT("Both fields are being edited at once"), First->InputBehaviour->IsInputActive() && Other->InputBehaviour->IsInputActive());
	TestTrue(TEXT("Player 0's keyboard is the first field's"), UUITextInput::GetActiveTextInputForPlayer(Rig.GetWorld(), 0) == First->InputBehaviour.Get());
	TestTrue(TEXT("...and player 1's the other's -- the second click did not take it from player 0"),
		UUITextInput::GetActiveTextInputForPlayer(Rig.GetWorld(), 1) == Other->InputBehaviour.Get());

	// Each player's characters go to their own field.
	TestTrue(TEXT("Player 0 types"), Input->HandleViewportCharacter(0, TEXT('a')));
	TestTrue(TEXT("Player 1 types"), Input->HandleViewportCharacter(1, TEXT('b')));
	Input->HandleViewportCharacter(1, TEXT('c'));
	TestEqual(TEXT("The first field holds what player 0 typed"), First->GetText(), FString(TEXT("a")));
	TestEqual(TEXT("...and the other what player 1 typed"), Other->GetText(), FString(TEXT("bc")));
	TestFalse(TEXT("A player typing into nothing types into nobody's field"), Input->HandleViewportCharacter(2, TEXT('x')));

	// Player 1 leaves: their edit ends with them, and player 0's goes on.
	Input->RemoveEventSystem(Second.EventSystem);
	Input->RemoveUser(1);
	TestFalse(TEXT("The player who left is no longer typing"), Other->InputBehaviour->IsInputActive());
	TestTrue(TEXT("...while the one who stayed still is"), First->InputBehaviour->IsInputActive()
		&& UUITextInput::GetActiveTextInputForPlayer(Rig.GetWorld(), 0) == First->InputBehaviour.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeyCharAndAnalogTest,
	"DreamGUI.Input.Keys.ACharacterNoFieldTakesAndAMovingStickReachWhatThePlayerHasFocused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamInputKeyCharAndAnalogTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(Rig.GetWorld());
	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(200.0, 100.0));
	UDreamKeyRecordingBehaviour* Recorder = IsValid(Target) ? Target->AddComponent<UDreamKeyRecordingBehaviour>() : nullptr;
	if (!TestTrue(TEXT("Input, a router and a widget that hears keys"), Input != nullptr && Router != nullptr && Recorder != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	TestFalse(TEXT("With nothing focused, a character goes nowhere"), Input->HandleViewportCharacter(0, TEXT('q')));
	Rig.EventSystem()->SetSelectComponentWithDefault(Target);

	// A character: the KeyChar channel, which nothing ever dispatched -- NativeOnKeyChar was dead code.
	TestFalse(TEXT("A character the focused widget does not keep is not taken"), Input->HandleViewportCharacter(0, TEXT('x')));
	TestEqual(TEXT("...but it heard it, as a character"), Recorder->KeyCharCount, 1);
	TestEqual(TEXT("...and not as a key"), Recorder->KeyDownCount, 0);
	Recorder->bKeepTheKey = true;
	TestTrue(TEXT("One it keeps is taken"), Input->HandleViewportCharacter(0, TEXT('y')));
	TestEqual(TEXT("...and heard"), Recorder->KeyCharCount, 2);

	// A stick: AnalogValueChanged, once per change -- a stick reports every frame whether or not it moved.
	TestTrue(TEXT("A stick pushed is heard, and kept"), Router->HandleAnalog(0, EKeys::Gamepad_RightX, 0.5f));
	TestEqual(TEXT("...once"), Recorder->AnalogCount, 1);
	TestTrue(TEXT("Held where it is, it is still kept -- so it does not scroll as well"), Router->HandleAnalog(0, EKeys::Gamepad_RightX, 0.5f));
	TestEqual(TEXT("...and the widget is not told again"), Recorder->AnalogCount, 1);
	Router->HandleAnalog(0, EKeys::Gamepad_RightX, 0.0f);
	TestEqual(TEXT("Let go, it has moved, and is heard"), Recorder->AnalogCount, 2);
	TestFalse(TEXT("A trigger at rest from its first sample has not moved"), Router->HandleAnalog(0, EKeys::Gamepad_LeftTriggerAxis, 0.0f));
	TestEqual(TEXT("...and is not heard"), Recorder->AnalogCount, 2);
	TestFalse(TEXT("Another player's stick is not this player's focus's"), Router->HandleAnalog(1, EKeys::Gamepad_RightX, 0.7f));
	TestEqual(TEXT("...and is not heard"), Recorder->AnalogCount, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputDragSourceGoneWhileHeldTest,
	"DreamGUI.Input.Pipeline.ADragWhoseSourceIsDestroyedWhileTheButtonIsHeldIsCancelledOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A drag source destroyed mid-drag -- a recycled row, a screen closed under the pointer -- with the button still held. The
 * pipeline noticed the source was gone on the next frame and stopped dragging, but told nobody: the release then took the
 * not-dragging road, and the operation's OnDragCancelled, the handler that puts an item back where it came from, never
 * ran. The operation is told on the frame the pipeline notices, once, and the release adds nothing.
 */
bool FDreamInputDragSourceGoneWhileHeldTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	Rig.EnsureGameInputHost();
	UDreamWidget* Card = Rig.MakeWidget(TEXT("Card"), nullptr, FVector2D(160.0, 120.0), FVector2D(-200.0, 0.0));
	UDreamUIDragDropSubsystem* DragDrop = UDreamUIDragDropSubsystem::Get(Rig.GetWorld());
	if (!TestNotNull(TEXT("The card was built"), Card) || !TestNotNull(TEXT("A game world has a drag-drop subsystem"), DragDrop))
	{
		return false;
	}
	Card->AddComponent<UDreamUIDragSource>();
	Rig.PumpFrames(3);

	Rig.InputModule()->MoveTo(CentreOf(Card));
	Rig.PumpFrames(1);
	Rig.InputModule()->Press();
	Rig.PumpFrames(1);
	Rig.InputModule()->MoveTo(CentreOf(Card) + FVector2D(60.0, 0.0));
	Rig.PumpFrames(2);
	UDreamDragDropOperation* Operation = DragDrop->GetDragOperationForPointer(0);
	if (!TestNotNull(TEXT("The source put an operation on the pointer when the drag began"), Operation))
	{
		Rig.InputModule()->Release();
		Rig.PumpFrames(1);
		return false;
	}
	TStrongObjectPtr<UDreamDragDropCallProbe> Probe(NewObject<UDreamDragDropCallProbe>());
	Operation->OnDragCancelled.AddDynamic(Probe.Get(), &UDreamDragDropCallProbe::OnOperation);

	// The source goes, the button stays down, and the pointer keeps moving.
	const FVector2D FurtherOn = CentreOf(Card) + FVector2D(90.0, 0.0);
	Card->DestroyWidget();
	Rig.InputModule()->MoveTo(FurtherOn);
	Rig.PumpFrames(2);
	TestEqual(TEXT("The operation is told its drag was cancelled while the button is still held"), Probe->CallCount, 1);

	Rig.InputModule()->Release();
	Rig.PumpFrames(1);
	TestEqual(TEXT("...and the release does not tell it twice"), Probe->CallCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputMouseOffTheViewportTest,
	"DreamGUI.Input.Pipeline.AMouseOnNoPartOfTheViewportIsOverNothingNotOverItsTopLeftCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The standalone module answered (0,0) for a mouse it could not place -- over another window, no mouse attached, no
 * viewport at all -- and the presets push its answer into pointer 0 every frame: whatever was drawn in the top-left corner
 * was hovered, and a drag let go of over another window was dropped onto it. The driver's own module stands a position in
 * for the mouse and so never asked. The answer is now the viewport's own reckoning, (-1,-1) wherever the cursor is on no
 * part of it, which is over nothing. Checked with the module's own answer in a world with no viewport, pushed into
 * pointer 0 as the presets push it, over a widget covering the top-left corner.
 */
bool FDreamInputMouseOffTheViewportTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable() && Rig.InputModule() != nullptr))
	{
		return false;
	}
	// 100 by 100 in the top-left corner: the root's middle is the viewport's, and up is up.
	UDreamWidget* Corner = Rig.MakeWidget(TEXT("Corner"), nullptr, FVector2D(100.0, 100.0),
		FVector2D(-0.5 * ViewportSize.X + 50.0, 0.5 * ViewportSize.Y - 50.0));
	UDreamPointerLedger* Ledger = IsValid(Corner) ? Corner->AddComponent<UDreamPointerLedger>() : nullptr;
	if (!TestNotNull(TEXT("A widget in the top-left corner, keeping books"), Ledger))
	{
		return false;
	}
	Rig.PumpFrames(2);
	const FVector2D CornerCentre = CentreOf(Corner);
	if (!TestTrue(TEXT("The widget covers the viewport's top-left pixel"), CornerCentre.X > 0.0 && CornerCentre.X < 60.0 && CornerCentre.Y > 0.0 && CornerCentre.Y < 60.0))
	{
		return false;
	}
	UDreamDriverInputModule* Module = Rig.InputModule();
	// Pointer 0 somewhere in the middle, over nothing, whatever it was over before.
	Module->MoveTo(FVector2D(0.5 * ViewportSize.X, 0.5 * ViewportSize.Y));
	Rig.PumpFrames(1);
	const int32 EntersBefore = Ledger->Enter;
	const int32 ExitsBefore = Ledger->Exit;

	// The module's own answer, its stand-in position switched off: this world has no viewport for a mouse to be on.
	Module->SetOverrideMousePosition(false);
	FVector2D Answer = FVector2D::ZeroVector;
	Module->GetMousePosition(Answer);
	TestTrue(TEXT("A mouse on no viewport is at (-1,-1), where a viewport puts a cursor that is off it"), Answer.Equals(FVector2D(-1.0, -1.0)));
	Module->InputMouseMove(FVector(Answer, 0.0));
	Rig.PumpFrames(2);
	TestEqual(TEXT("...which, pushed into pointer 0 as the presets push it, hovers nothing"), Ledger->Enter, EntersBefore);

	// The corner widget is what the old answer put the mouse over.
	Module->InputMouseMove(FVector(1.0, 1.0, 0.0));
	Rig.PumpFrames(1);
	TestEqual(TEXT("The viewport's top-left pixel is the corner widget's"), Ledger->Enter, EntersBefore + 1);
	Module->InputMouseMove(FVector(Answer, 0.0));
	Rig.PumpFrames(1);
	TestEqual(TEXT("...and the mouse going off the viewport leaves it"), Ledger->Exit, ExitsBefore + 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputKeyReleaseFollowsPressTest,
	"DreamGUI.Input.Keys.AKeysReleaseGoesWhereItsPressWentWhateverTakesThatKeyByThen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A release was routed by what the key meant when it came up, not by what had taken its press. A dialog that opened over
 * a held confirm and bound the confirm key took its release, and the button the confirm had pressed was never let go of;
 * a field that begins its edit on being navigated into, reached with the arrow key held, took the arrow's release as
 * typing, and the direction went on stepping. Each player now remembers what took each held key, and its release goes
 * there. Checked through the key routing every input source takes.
 */
bool FDreamInputKeyReleaseFollowsPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputUser* User = Rig.EventSystem()->GetInputUser();
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(Rig.GetWorld());
	TStrongObjectPtr<UDreamPressInteractionListener> Listener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Button = Rig.MakeControl<UDreamButton>(TEXT("Delete"), nullptr, FVector2D(200.0, 60.0));
	if (!TestTrue(TEXT("A player, a router and a button"), User != nullptr && Router != nullptr && Button != nullptr))
	{
		return false;
	}
	Button->OnPressed.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandlePressed);
	Button->OnReleased.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleReleased);
	Button->OnClicked.AddDynamic(Listener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Rig.PumpFrames(2);
	const FModifierKeysState NoModifiers;
	bool bTyped = false;
	auto Route = [User, &NoModifiers, &bTyped](const FKey& InKey, bool bInPressed)
	{
		return DreamUIKeyRouting::RouteKey(User, InKey, bInPressed, NoModifiers, bTyped);
	};

	// The highlight onto the button -- the first step lands on the only selectable there is -- and the confirm held on it.
	Route(EKeys::Down, true);
	Rig.PumpFrames(1);
	Route(EKeys::Down, false);
	Rig.PumpFrames(1);
	Route(EKeys::Enter, true);
	Rig.PumpFrames(1);
	TestEqual(TEXT("The confirm pressed the highlighted button"), Listener->PressedCount, 1);

	// A dialog opens over the held key, and binds it.
	UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
	Table->RowStruct = FDreamUIInputActionData::StaticStruct();
	FDreamUIInputActionData Row;
	Row.DisplayName = FText::FromString(TEXT("Confirm"));
	Row.KeyboardKey = EKeys::Enter;
	Row.GamepadKey = EKeys::Gamepad_FaceButton_Bottom;
	Table->AddRow(TEXT("Confirm"), Row);
	FDataTableRowHandle ConfirmRow;
	ConfirmRow.DataTable = Table;
	ConfirmRow.RowName = TEXT("Confirm");
	TStrongObjectPtr<UDreamActionCallCounter> DialogConfirm(NewObject<UDreamActionCallCounter>());
	FDreamUIActionExecutedDelegate OnDialogConfirm;
	OnDialogConfirm.BindUFunction(DialogConfirm.Get(), TEXT("Fire"));
	const FDreamUIActionHandle DialogBinding = Router->RegisterAction(nullptr, ConfirmRow, OnDialogConfirm);

	Route(EKeys::Enter, false);
	Rig.PumpFrames(1);
	TestEqual(TEXT("The confirm's release let go of the button it pressed"), Listener->ReleasedCount, 1);
	TestEqual(TEXT("...and clicked it, the press having been made on it"), Listener->ClickedCount, 1);
	TestEqual(TEXT("...and the dialog's binding, which never saw the press, took nothing"), DialogConfirm->CallCount, 0);
	Router->UnregisterAction(DialogBinding);

	// A field below the button that begins its edit on being navigated into, reached with the arrow held.
	UDreamTextInput* Field = Rig.MakeControl<UDreamTextInput>(TEXT("Name"), nullptr, FVector2D(320.0, 40.0), FVector2D(0.0, -200.0));
	if (!TestTrue(TEXT("A field"), Field != nullptr && Field->InputBehaviour != nullptr))
	{
		return false;
	}
	Field->InputBehaviour->SetAutoActivateInputWhenNavigateIn(true);
	Rig.PumpFrames(2);
	Route(EKeys::Down, true);
	Rig.PumpFrames(2);
	if (!TestTrue(TEXT("The arrow moved the highlight into the field, which began its edit"), Field->InputBehaviour->IsInputActive()))
	{
		Route(EKeys::Down, false);
		return false;
	}
	Route(EKeys::Down, false);
	TestFalse(TEXT("The arrow's release is no typing for the field its press never reached"), bTyped);
	const UDreamPointerEventData* Navigation = User->FindPointerEventData(DreamUIPointerIds::Mouse);
	TestTrue(TEXT("...it lets go of the direction its press held"),
		Navigation != nullptr && Navigation->NavigateDirection == EDreamUINavigationDirection::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputTwoDirectionsTest,
	"DreamGUI.Input.Keys.LettingGoOfOneOfTwoHeldDirectionsLeavesTheOtherStepping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Navigation kept one direction per pointer, and the release of any direction emptied it: a D-pad rolled from Down to
 * Right -- Down pressed, Right pressed, Down let go of -- stopped stepping with Right still held, and so did an arrow key
 * held while another was tapped beside it. Every direction held is kept now, and letting go of one leaves the latest of
 * the others stepping. Checked through the key routing every input source takes, on the navigation pointer's own state.
 */
bool FDreamInputTwoDirectionsTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputUser* User = Rig.EventSystem()->GetInputUser();
	if (!TestNotNull(TEXT("A player"), User))
	{
		return false;
	}
	const FModifierKeysState NoModifiers;
	bool bTyped = false;
	auto Route = [User, &NoModifiers, &bTyped](const FKey& InKey, bool bInPressed)
	{
		return DreamUIKeyRouting::RouteKey(User, InKey, bInPressed, NoModifiers, bTyped);
	};

	Route(EKeys::Gamepad_DPad_Down, true);
	Route(EKeys::Gamepad_DPad_Right, true);
	const UDreamPointerEventData* Navigation = User->FindPointerEventData(DreamUIPointerIds::Mouse);
	if (!TestNotNull(TEXT("Navigation has its pointer"), Navigation))
	{
		return false;
	}
	TestEqual(TEXT("Of two directions held, the later steps"), Navigation->NavigateDirection, EDreamUINavigationDirection::Right);
	Route(EKeys::Gamepad_DPad_Down, false);
	TestEqual(TEXT("Letting go of the earlier leaves the later stepping"), Navigation->NavigateDirection, EDreamUINavigationDirection::Right);
	Route(EKeys::Gamepad_DPad_Right, false);
	TestEqual(TEXT("...until it is let go of too"), Navigation->NavigateDirection, EDreamUINavigationDirection::None);

	Route(EKeys::Down, true);
	Route(EKeys::Right, true);
	Route(EKeys::Right, false);
	TestEqual(TEXT("Letting go of the later goes back to the one still held"), Navigation->NavigateDirection, EDreamUINavigationDirection::Down);
	Route(EKeys::Down, false);
	TestEqual(TEXT("...and letting go of that stops it"), Navigation->NavigateDirection, EDreamUINavigationDirection::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputScreenKeepsOtherCanvasRaycasterTest,
	"DreamGUI.Input.Players.APlayersScreenGetsARaycasterOfItsOwnRatherThanTakingOneFromAnotherCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Building a player's screen pointed every screen raycaster of that player at the new screen's canvas, whatever canvas
 * it had been put there to serve: a raycaster placed for an overlay canvas of the project's own was taken from it, and
 * nothing on that canvas answered the player again. A raycaster projecting through an overlay root of its own now keeps
 * it, and a screen that finds every one of the player's so taken is given one of its own. Checked with a second player
 * whose raycaster serves the rig's canvas, and that player's screen then built.
 */
bool FDreamInputScreenKeepsOtherCanvasRaycasterTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	// Two overlay canvases for one player compete for one screen, which the manager reports every tick; the
	// configuration is the point of this test, so the report is expected.
	AddExpectedMessage(TEXT("rendered with ScreenSpaceOverlay mode"), ELogVerbosity::Error, EAutomationExpectedErrorFlags::Contains, -1);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputSubsystem* Input = UDreamUIInputSubsystem::Get(Rig.GetWorld());
	UDreamScreenUISubsystem* ScreenUI = UDreamScreenUISubsystem::Get(Rig.GetWorld());
	FSecondPlayer Second(Rig, 1);
	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(200.0, 100.0));
	UDreamPointerLedger* Ledger = IsValid(Target) ? Target->AddComponent<UDreamPointerLedger>() : nullptr;
	if (!TestTrue(TEXT("Input, screens, a second player on the rig's canvas and a widget there"),
		Input != nullptr && ScreenUI != nullptr && Second.IsUsable() && Ledger != nullptr))
	{
		return false;
	}
	Rig.PumpFrames(1);

	// Player 1's own screen, as a page pushed for player 1 builds it.
	UDreamWidget* SecondScreen = ScreenUI->GetOrCreateScreenRootForUserIndex(1);
	UDreamCanvas* SecondCanvas = SecondScreen != nullptr ? SecondScreen->GetComponent<UDreamCanvas>() : nullptr;
	if (!TestTrue(TEXT("Player 1 has a screen of its own"), SecondCanvas != nullptr && SecondCanvas != Rig.RootCanvas()))
	{
		return false;
	}
	TestTrue(TEXT("The raycaster serving the rig's canvas for player 1 keeps it"), Second.Raycaster->GetRootCanvas() == Rig.RootCanvas());
	const AActor* Host = Input->GetInteractionHost(1);
	const UDreamScreenSpaceRaycaster* Made = Host != nullptr ? Host->FindComponentByClass<UDreamScreenSpaceRaycaster>() : nullptr;
	TestTrue(TEXT("...and the new screen was given a raycaster of its own"), Made != nullptr && Made->GetRootCanvas() == SecondCanvas);

	// And player 1 still points at what is on the rig's canvas.
	Rig.PumpFrames(1);
	Second.Module->MoveTo(CentreOf(Target));
	Rig.PumpFrames(1);
	Second.Module->Press();
	Rig.PumpFrames(1);
	Second.Module->Release();
	Rig.PumpFrames(1);
	TestEqual(TEXT("Player 1's click on the rig's canvas lands"), Ledger->Click, 1);
	TestEqual(TEXT("...as player 1's"), Ledger->LastUserIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputOverlaySortOrderTest,
	"DreamGUI.Input.Pipeline.OfTwoOverlayCanvasesUnderThePointerTheOneDrawnOnTopIsWhatItIsOver",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Two overlay canvases under the pointer, each answered by a screen raycaster of its own, were ordered by distance along
 * the ray -- which between two overlays drawn one over the other says nothing about which is on top -- and at a tie the
 * raycaster listed first won, so a canvas drawn over another passed the pointer through to the one below. Within one
 * raycaster the canvas sort order has always decided; across raycasters it decides now too. Checked with a second overlay
 * canvas sorted above the rig's, its raycaster listed after the rig's, and a widget over the same pixel as one of the rig's.
 */
bool FDreamInputOverlaySortOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	// Two overlay canvases for one player compete for one screen, which the manager reports every tick; the
	// configuration is the point of this test, so the report is expected.
	AddExpectedMessage(TEXT("rendered with ScreenSpaceOverlay mode"), ELogVerbosity::Error, EAutomationExpectedErrorFlags::Contains, -1);
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	AActor* Host = Rig.GetHostActor();
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable() && Host != nullptr && Rig.RootCanvas() != nullptr))
	{
		return false;
	}
	UDreamWidget* Below = Rig.MakeWidget(TEXT("Below"), nullptr, FVector2D(200.0, 100.0));
	UDreamPointerLedger* BelowLedger = IsValid(Below) ? Below->AddComponent<UDreamPointerLedger>() : nullptr;

	// A second overlay root, built as the rig builds its own, drawn above it, with a raycaster of its own.
	UDreamWidget* OverlayRoot = NewObject<UDreamWidget>(Rig.GetWorld(), NAME_None, RF_Public | RF_Transactional);
	OverlayRoot->SetDisplayName(TEXT("Overlay"));
	OverlayRoot->OnRegister();
	UDreamCanvas* OverlayCanvas = OverlayRoot->AddComponent<UDreamCanvas>();
	if (!TestTrue(TEXT("A widget on the rig's canvas, and a second overlay canvas"), BelowLedger != nullptr && OverlayCanvas != nullptr))
	{
		OverlayRoot->DestroyWidget();
		return false;
	}
	OverlayCanvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	OverlayCanvas->SetViewportSizeOverride(ViewportSize);
	// A root canvas's sort order counts only with sorting overridden; without it every root sorts at 0.
	OverlayCanvas->SetOverrideSorting(true);
	OverlayCanvas->SetSortOrder(Rig.RootCanvas()->GetActualSortOrder() + 10);
	if (!OverlayRoot->HasBegunPlay())
	{
		OverlayRoot->BeginPlay();
	}
	OverlayRoot->CalculateObjectToWorldTransform(true);
	UDreamScreenSpaceRaycaster* OverlayRaycaster = NewObject<UDreamScreenSpaceRaycaster>(Host);
	OverlayRaycaster->SetRootCanvas(OverlayCanvas);
	Host->AddInstanceComponent(OverlayRaycaster);
	OverlayRaycaster->RegisterComponent();
	OverlayRaycaster->ActivateRaycaster();
	UDreamWidget* Above = Rig.MakeWidget(TEXT("Above"), OverlayRoot, FVector2D(200.0, 100.0));
	UDreamPointerLedger* AboveLedger = IsValid(Above) ? Above->AddComponent<UDreamPointerLedger>() : nullptr;
	if (!TestNotNull(TEXT("A widget on the second canvas, over the first one"), AboveLedger))
	{
		OverlayRoot->DestroyWidget();
		return false;
	}
	Rig.PumpFrames(2);

	UDreamDriverInputModule* Module = Rig.InputModule();
	Module->MoveTo(CentreOf(Below));
	Rig.PumpFrames(1);
	Module->Press();
	Rig.PumpFrames(1);
	Module->Release();
	Rig.PumpFrames(1);
	TestEqual(TEXT("The pointer is over the widget on the canvas drawn on top"), AboveLedger->Enter, 1);
	TestEqual(TEXT("...and clicks it"), AboveLedger->Click, 1);
	TestEqual(TEXT("...not the one on the canvas below"), BelowLedger->Click, 0);
	OverlayRoot->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputHiddenFocusTest,
	"DreamGUI.Input.Focus.AButtonHiddenOrDisabledWhileItHoldsTheFocusGivesItUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget is refused the focus while it is hidden or disabled, but one that became so while it held the focus kept it,
 * and went on hearing its player's keys and characters: a page hidden under the one in front still answered Escape with
 * its own handler. Hiding a widget -- or a widget it is inside -- and disabling one now takes the focus from it, as
 * making it unfocusable always did, and Slate does; the action router passes over a focused widget that is hidden or
 * disabled all the same. Checked with three buttons, each focused in turn: one hidden, one disabled, one inside a panel
 * that is hidden -- and with a key that no longer reaches the hidden one.
 */
bool FDreamInputHiddenFocusTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIInputUser* User = Rig.EventSystem()->GetInputUser();
	UDreamUIActionRouter* Router = UDreamUIActionRouter::Get(Rig.GetWorld());
	UDreamButton* Hidden = Rig.MakeControl<UDreamButton>(TEXT("Hidden"), nullptr, FVector2D(200.0, 60.0), FVector2D(-300.0, 0.0));
	UDreamButton* Disabled = Rig.MakeControl<UDreamButton>(TEXT("Disabled"), nullptr, FVector2D(200.0, 60.0), FVector2D(0.0, 0.0));
	UDreamWidget* Panel = Rig.MakeWidget(TEXT("Panel"), nullptr, FVector2D(300.0, 200.0), FVector2D(300.0, 0.0));
	UDreamButton* Inside = IsValid(Panel) ? Rig.MakeControl<UDreamButton>(TEXT("Inside"), Panel, FVector2D(200.0, 60.0)) : nullptr;
	if (!TestTrue(TEXT("A player, a router, and three buttons, one of them inside a panel"),
		User != nullptr && Router != nullptr && Hidden != nullptr && Disabled != nullptr && Inside != nullptr
		&& Hidden->FaceNode != nullptr && Disabled->FaceNode != nullptr && Inside->FaceNode != nullptr))
	{
		return false;
	}
	UDreamKeyRecordingBehaviour* Recorder = Hidden->FaceNode->AddComponent<UDreamKeyRecordingBehaviour>();
	if (!TestNotNull(TEXT("The first button's face hears keys"), Recorder))
	{
		return false;
	}
	Recorder->bKeepTheKey = true;
	Rig.PumpFrames(1);
	// The face is what a click or a navigation step focuses: the button's selectable lives there.
	auto FocusOn = [&Rig, User](UDreamButton* InButton)
	{
		Rig.EventSystem()->SetSelectComponentWithDefault(InButton->FaceNode);
		return User->GetFocusedWidget() == InButton->FaceNode;
	};

	if (!TestTrue(TEXT("The first button has the focus"), FocusOn(Hidden)))
	{
		return false;
	}
	TestTrue(TEXT("...and takes a key"), Router->HandleKey(0, EKeys::F, true));
	Router->HandleKey(0, EKeys::F, false);
	Hidden->SetVisibility(EDreamWidgetVisibility::Hidden);
	TestTrue(TEXT("Hidden, it no longer holds the focus"), User->GetFocusedWidget() != Hidden->FaceNode);
	TestFalse(TEXT("...and a key does not reach it"), Router->HandleKey(0, EKeys::F, true));
	Router->HandleKey(0, EKeys::F, false);
	TestEqual(TEXT("...which heard only the key from before"), Recorder->KeyDownCount, 1);
	Hidden->SetVisibility(EDreamWidgetVisibility::Visible);
	TestTrue(TEXT("Shown again, it does not take the focus back by itself"), User->GetFocusedWidget() != Hidden->FaceNode);

	TestTrue(TEXT("The second button has the focus"), FocusOn(Disabled));
	Disabled->SetIsEnabled(false);
	TestTrue(TEXT("Disabled, it no longer holds the focus"), User->GetFocusedWidget() != Disabled->FaceNode);

	TestTrue(TEXT("The button in the panel has the focus"), FocusOn(Inside));
	Panel->SetVisibility(EDreamWidgetVisibility::Hidden);
	TestTrue(TEXT("Its panel hidden, it no longer holds the focus"), User->GetFocusedWidget() != Inside->FaceNode);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamInputReleaseWhileTracingOffTest,
	"DreamGUI.Input.Pipeline.AReleaseWhileTracingIsOffEndsItsPressThereWithNoClick",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A player whose tracing was turned off -- a cutscene taking the pointer away, say -- kept a release that was waiting for
 * the next frame, and dropped any release that came while tracing stayed off: the press held on, and its release landed,
 * if at all, the frame tracing came back, on whatever was under the pointer by then -- a click nobody made. A release
 * waiting when tracing goes off, or arriving while it is off, now ends its press at once, with its up and no click.
 */
bool FDreamInputReleaseWhileTracingOffTest::RunTest(const FString& Parameters)
{
	using namespace DreamInputPipelineTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamEventSystem* Events = Rig.EventSystem();
	UDreamDriverInputModule* Module = Rig.InputModule();
	UDreamWidget* Target = Rig.MakeWidget(TEXT("Target"), nullptr, FVector2D(200.0, 100.0));
	UDreamPointerLedger* Ledger = IsValid(Target) ? Target->AddComponent<UDreamPointerLedger>() : nullptr;
	if (!TestNotNull(TEXT("A widget keeping books"), Ledger))
	{
		return false;
	}
	Rig.PumpFrames(1);
	Module->MoveTo(CentreOf(Target));
	Rig.PumpFrames(1);

	// Let go of in the frame tracing is turned off.
	Module->Press();
	Rig.PumpFrames(1);
	TestEqual(TEXT("Pressed"), Ledger->Down, 1);
	Module->Release();
	Events->SetRaycastEnable(false);
	Rig.PumpFrames(1);
	TestEqual(TEXT("A release waiting when tracing goes off ends its press"), Ledger->Up, 1);
	TestEqual(TEXT("...with no click"), Ledger->Click, 0);
	Events->SetRaycastEnable(true);
	Rig.PumpFrames(2);
	TestEqual(TEXT("...and none when tracing comes back"), Ledger->Click, 0);

	// Let go of while tracing is off.
	Module->Press();
	Rig.PumpFrames(1);
	TestEqual(TEXT("Pressed again"), Ledger->Down, 2);
	Events->SetRaycastEnable(false);
	Rig.PumpFrames(1);
	Module->Release();
	Rig.PumpFrames(1);
	TestEqual(TEXT("A release while tracing is off ends its press"), Ledger->Up, 2);
	Events->SetRaycastEnable(true);
	Rig.PumpFrames(2);
	TestEqual(TEXT("...and nothing clicks when tracing comes back"), Ledger->Click, 0);
	const UDreamPointerEventData* Mouse = Events->GetInputUser() != nullptr ? Events->GetInputUser()->FindPointerEventData(DreamUIPointerIds::Mouse) : nullptr;
	TestTrue(TEXT("...nor is anything left pressed"), Mouse != nullptr && !Mouse->bNowIsTriggerPressed);
	return true;
}

#endif
