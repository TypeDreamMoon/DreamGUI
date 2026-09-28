// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIInputServices.h"
#include "Core/DreamUIManager.h"
#include "Driver/DreamDriverInputModule.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "DreamInputPipelineTestTypes.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Event/DreamUIInputUser.h"
#include "GameFramework/Actor.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUITooltip.h"

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

#endif
