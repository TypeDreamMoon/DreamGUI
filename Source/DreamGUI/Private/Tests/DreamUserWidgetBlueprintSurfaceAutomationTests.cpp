// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/InputModule/DreamPointerInputModule.h"
#include "Event/InputModule/DreamStandaloneInputModule.h"
#include "Event/Interface/DreamPointerScrollInterface.h"
#include "Event/Interface/DreamPointerSelectDeselectInterface.h"
#include "GameFramework/Actor.h"
#include "Interaction/UIEventTrigger.h"

/*
 * THE USER WIDGET'S BLUEPRINT SURFACE, and the one promise it must keep.
 *
 * Everything here rides UDreamUserWidgetEventBridge, a behaviour added at Initialize, because every
 * dispatch path in this framework speaks to a widget's components and never to the widget object:
 * ExecuteDreamUIInterface iterates GetAllComponents(), the navigation search does the same, and the
 * manager's tick lists hold behaviours. Adding a component to EVERY user widget is therefore also the
 * one thing that could silently change what existing screens receive -- so half of this file is the
 * routing-neutrality proof, run through the same hand-driven ProcessPointerEvent rig the drag
 * threshold tests use: same raycast answer, same frames, once with a plain widget in the chain and
 * once with a user widget, asserting the observable delivery is identical.
 *
 * What is asserted behaviourally versus structurally: the bridge's ForwardCount fields increment
 * AFTER the widget-side Native* call returns, so a count is proof the seam fired end to end;
 * IsConstructed() flips inside NativeOnConstruct/NativeOnDestruct, proving those independently. The
 * Blueprint events themselves are BlueprintImplementableEvents with no bodies on a native class, so
 * their reachability is asserted structurally -- the UFUNCTION exists on the class -- which is the
 * headless limit.
 */

namespace DreamUserWidgetBlueprintSurfaceTestLocal
{
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedGameWorld() { if (World) { World->DestroyWorld(false); } }
	};

	struct FScopedEditorWorld
	{
		UWorld* World = nullptr;
		FScopedEditorWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedEditorWorld() { if (World) { World->DestroyWorld(false); } }
	};

	UDreamWidget* MakeWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W = 100.0f, float H = 100.0f)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	/** Production order: Initialize (which attaches the bridge in a game world), then register. */
	UDreamUserWidget* MakeUserWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W = 200.0f, float H = 200.0f)
	{
		UDreamUserWidget* Widget = NewObject<UDreamUserWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->Initialize();
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	/** Counted by the production UUIEventTrigger, which is what "this widget was sent the event" means. */
	struct FWidgetEventLog
	{
		int32 Enter = 0;
		int32 Exit = 0;
		int32 Down = 0;
		int32 Up = 0;
		int32 Click = 0;

		void Observe(UDreamWidget* Widget)
		{
			UUIEventTrigger* Trigger = Widget->AddComponent<UUIEventTrigger>();
			if (Trigger == nullptr)
			{
				return;
			}
			Trigger->GetOnPointerEnterEvent().AddLambda([this](UDreamPointerEventData*) { ++Enter; });
			Trigger->GetOnPointerExitEvent().AddLambda([this](UDreamPointerEventData*) { ++Exit; });
			Trigger->GetOnPointerDownEvent().AddLambda([this](UDreamPointerEventData*) { ++Down; });
			Trigger->GetOnPointerUpEvent().AddLambda([this](UDreamPointerEventData*) { ++Up; });
			Trigger->GetOnPointerClickEvent().AddLambda([this](UDreamPointerEventData*) { ++Click; });
		}
	};

	/** The drag-threshold tests' rig: the pipeline driven frame by frame with the raycast answered by hand. */
	struct FPointerRig
	{
		AActor* Host = nullptr;
		UDreamEventSystem* EventSystem = nullptr;
		UDreamStandaloneInputModule* Module = nullptr;
		UDreamScreenSpaceRaycaster* Raycaster = nullptr;
		UDreamPointerEventData* EventData = nullptr;

		explicit FPointerRig(UWorld* InWorld)
		{
			Host = InWorld->SpawnActor<AActor>();
			EventSystem = NewObject<UDreamEventSystem>(Host);
			EventSystem->RegisterComponent();
			Module = NewObject<UDreamStandaloneInputModule>(Host);
			Module->RegisterComponent();
			Module->RegisterInputModuleToEventSystem(EventSystem);
			Raycaster = NewObject<UDreamScreenSpaceRaycaster>(Host);
			EventData = EventSystem->GetPointerEventData(0, true);
		}

		bool IsUsable() const
		{
			return Host != nullptr && EventSystem != nullptr && Module != nullptr
				&& Raycaster != nullptr && EventData != nullptr;
		}

		void Press(const FVector2D& Position)
		{
			Module->InputMouseMove(FVector(Position.X, Position.Y, 0.0));
			EventData->bNowIsTriggerPressed = true;
			EventData->PressTime = Host->GetWorld()->TimeSeconds;
			EventData->PressPointerPosition = FVector(Position.X, Position.Y, 0.0);
		}

		void Release(const FVector2D& Position)
		{
			Module->InputMouseMove(FVector(Position.X, Position.Y, 0.0));
			EventData->bNowIsTriggerPressed = false;
			EventData->ReleaseTime = Host->GetWorld()->TimeSeconds;
		}

		void Frame(UDreamWidget* WidgetUnderPointer)
		{
			FDreamUIHitResultContainer HitContainer;
			HitContainer.Raycaster = Raycaster;
			if (WidgetUnderPointer != nullptr)
			{
				HitContainer.HitResult.Widget = WidgetUnderPointer;
				HitContainer.HoverArray.Add(WidgetUnderPointer);
			}
			bool bOutIsHitSomething = false;
			FDreamUIHitResult OutHitResult;
			UDreamPointerInputModule::ProcessPointerEvent(
				EventSystem, EventData, WidgetUnderPointer != nullptr,
				HitContainer, bOutIsHitSomething, OutHitResult);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetSurfaceExistsTest,
	"DreamGUI.UserWidget.BlueprintSurface.TheEventsExistAndTheNativeSeamsAreCallable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetSurfaceExistsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetBlueprintSurfaceTestLocal;

	// Structural half: every Blueprint event is a real UFUNCTION on the class, so a Blueprint
	// subclass can implement it. These names are the Blueprint-facing contract.
	const TCHAR* EventNames[] = {
		TEXT("OnConstruct"), TEXT("OnDestruct"), TEXT("OnEnable"), TEXT("OnDisable"), TEXT("OnTick"),
		TEXT("OnPointerEnter"), TEXT("OnPointerExit"), TEXT("OnPointerDown"), TEXT("OnPointerUp"), TEXT("OnPointerClick"),
		TEXT("OnBeginDrag"), TEXT("OnDrag"), TEXT("OnEndDrag"), TEXT("OnDrop"),
		TEXT("ReceiveFocusReceived"), TEXT("ReceiveFocusLost"), TEXT("OnNavigate"),
	};
	for (const TCHAR* EventName : EventNames)
	{
		TestNotNull(*FString::Printf(TEXT("the class declares '%s'"), EventName),
			UDreamUserWidget::StaticClass()->FindFunctionByName(FName(EventName)));
	}

	// Behavioural half: the Native* seams are directly callable, and the two that carry state --
	// constructed and the bubble policy -- observably do what they say with no Blueprint involved.
	UDreamUserWidget* Widget = NewObject<UDreamUserWidget>(GetTransientPackage());
	UDreamPointerEventData* EventData = NewObject<UDreamPointerEventData>(GetTransientPackage());

	TestFalse(TEXT("a fresh widget is not constructed"), Widget->IsConstructed());
	Widget->NativeOnConstruct();
	TestTrue(TEXT("NativeOnConstruct marks it constructed"), Widget->IsConstructed());
	Widget->NativeOnEnable();
	Widget->NativeOnTick(0.016f);
	Widget->NativeOnDisable();
	Widget->NativeOnDestruct();
	TestFalse(TEXT("NativeOnDestruct clears it"), Widget->IsConstructed());

	TestTrue(TEXT("bubble-up defaults to true, so containers keep working"), Widget->GetAllowEventBubbleUp());
	TestTrue(TEXT("...and every pointer seam returns that policy"), Widget->NativeOnPointerDown(EventData));
	TestTrue(TEXT("...the drag seams too"), Widget->NativeOnBeginDrag(EventData));
	Widget->SetAllowEventBubbleUp(false);
	TestFalse(TEXT("...and flipping the policy flips the returns"), Widget->NativeOnPointerClick(EventData));
	TestFalse(TEXT("...for drops as well"), Widget->NativeOnDrop(EventData));

	Widget->NativeOnFocusReceived(0, 0);
	Widget->NativeOnFocusLost(0, 0);

	// Pre-filled so the assertion below can only pass if NativeOnNavigate actually wrote the answer.
	UDreamWidget* NextWidget = NewObject<UDreamWidget>(GetTransientPackage());
	Widget->NativeOnNavigate(EDreamUINavigationDirection::Down, NextWidget);
	TestNull(TEXT("with no Blueprint body, OnNavigate answers 'stay here'"), NextWidget);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetBridgeAttachmentTest,
	"DreamGUI.UserWidget.BlueprintSurface.TheBridgeAttachesOnlyInGameWorldsAndOnlyOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetBridgeAttachmentTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetBlueprintSurfaceTestLocal;

	// Game world: exactly one bridge, however often Initialize is asked.
	{
		FScopedGameWorld Scope;
		if (!TestNotNull(TEXT("a game world"), Scope.World))
		{
			return false;
		}
		UDreamUserWidget* Widget = NewObject<UDreamUserWidget>(Scope.World);
		Widget->Initialize();
		Widget->Initialize();
		TestEqual(TEXT("one bridge, not zero, not two"),
			Widget->GetComponents(UDreamUserWidgetEventBridge::StaticClass()).Num(), 1);

		UDreamUserWidgetEventBridge* Bridge = Cast<UDreamUserWidgetEventBridge>(
			Widget->GetComponent(UDreamUserWidgetEventBridge::StaticClass()));
		if (!TestNotNull(TEXT("and it is reachable"), Bridge))
		{
			return false;
		}
		TestTrue(TEXT("the bridge instance is transient"), Bridge->HasAnyFlags(RF_Transient));
		TestEqual(TEXT("and outered to its widget, as behaviours are"), Bridge->GetWidget(), (UDreamWidget*)Widget);

		// The interface roster IS the routing contract: what it speaks it may receive, and the two it
		// must NOT speak are the ones whose mere presence changes routing decisions --
		// select/deselect drives GetEventHandle (focus and deselect targeting), and scroll belongs
		// to scroll views.
		UClass* BridgeClass = Bridge->GetClass();
		TestTrue(TEXT("speaks enter/exit"), BridgeClass->ImplementsInterface(UDreamPointerEnterExitInterface::StaticClass()));
		TestTrue(TEXT("speaks down/up"), BridgeClass->ImplementsInterface(UDreamPointerDownUpInterface::StaticClass()));
		TestTrue(TEXT("speaks click"), BridgeClass->ImplementsInterface(UDreamPointerClickInterface::StaticClass()));
		TestTrue(TEXT("speaks drag"), BridgeClass->ImplementsInterface(UDreamPointerDragInterface::StaticClass()));
		TestTrue(TEXT("speaks drop"), BridgeClass->ImplementsInterface(UDreamPointerDragDropInterface::StaticClass()));
		TestTrue(TEXT("speaks navigation"), BridgeClass->ImplementsInterface(UDreamNavigationInterface::StaticClass()));
		TestFalse(TEXT("does NOT speak select/deselect -- GetEventHandle must not find it"),
			BridgeClass->ImplementsInterface(UDreamPointerSelectDeselectInterface::StaticClass()));
		TestFalse(TEXT("does NOT speak scroll"),
			BridgeClass->ImplementsInterface(UDreamPointerScrollInterface::StaticClass()));

		// Focus is bound at Initialize; a broadcast reaching the handlers must be safe with no
		// Blueprint bodies. (The observable end of this seam needs a Blueprint, so this is a smoke
		// assertion by construction: it completes or the test dies.)
		Widget->NotifyFocusReceived(0, 0);
		Widget->NotifyFocusLost(0, 0);
	}

	// No world at all: no bridge, and Initialize survives.
	{
		UDreamUserWidget* Orphan = NewObject<UDreamUserWidget>(GetTransientPackage());
		Orphan->Initialize();
		TestNull(TEXT("a widget with no world gets no bridge"),
			Orphan->GetComponent(UDreamUserWidgetEventBridge::StaticClass()));
	}

	// Edit world: no bridge. This is the designer-cleanliness guarantee -- the preview's component
	// list must contain exactly what the author put there.
	{
		FScopedEditorWorld Scope;
		if (TestNotNull(TEXT("an editor world"), Scope.World))
		{
			UDreamUserWidget* Widget = NewObject<UDreamUserWidget>(Scope.World);
			Widget->Initialize();
			TestNull(TEXT("an edit-world widget gets no bridge"),
				Widget->GetComponent(UDreamUserWidgetEventBridge::StaticClass()));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetLifecycleSeamsTest,
	"DreamGUI.UserWidget.BlueprintSurface.LifecycleRidesBeginPlayActiveChangesAndEndPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetLifecycleSeamsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetBlueprintSurfaceTestLocal;

	FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("a game world"), Scope.World))
	{
		return false;
	}

	UDreamUserWidget* Widget = MakeUserWidget(Scope.World, nullptr, TEXT("Screen"));
	UDreamUserWidgetEventBridge* Bridge = Cast<UDreamUserWidgetEventBridge>(
		Widget->GetComponent(UDreamUserWidgetEventBridge::StaticClass()));
	if (!TestNotNull(TEXT("the bridge exists"), Bridge))
	{
		return false;
	}

	// Registered but not begun: nothing has fired. Registration is structure; begin play is life.
	TestEqual(TEXT("no construct before begin play"), Bridge->ConstructForwardCount, 0);
	TestEqual(TEXT("no enable before begin play"), Bridge->EnableForwardCount, 0);
	TestFalse(TEXT("not constructed before begin play"), Widget->IsConstructed());

	// Begin play: construct once, then enable, in that order and exactly once.
	Widget->BeginPlay();
	TestEqual(TEXT("begin play constructs"), Bridge->ConstructForwardCount, 1);
	TestEqual(TEXT("...and enables an active widget"), Bridge->EnableForwardCount, 1);
	TestEqual(TEXT("...without disabling anything"), Bridge->DisableForwardCount, 0);
	TestTrue(TEXT("...and the widget-side flag proves the widget's Native ran"), Widget->IsConstructed());

	// Active switching is the enable/disable seam.
	Widget->SetWidgetActive(false);
	TestEqual(TEXT("deactivating disables"), Bridge->DisableForwardCount, 1);
	TestEqual(TEXT("...it does not destruct"), Bridge->DestructForwardCount, 0);
	TestTrue(TEXT("...and the widget stays constructed"), Widget->IsConstructed());

	Widget->SetWidgetActive(true);
	TestEqual(TEXT("reactivating enables again"), Bridge->EnableForwardCount, 2);
	TestEqual(TEXT("...without a second construct"), Bridge->ConstructForwardCount, 1);

	// End play: disable first, then destruct -- the behaviour contract, surfaced.
	Widget->EndPlay();
	TestEqual(TEXT("end play disables an enabled widget first"), Bridge->DisableForwardCount, 2);
	TestEqual(TEXT("...then destructs"), Bridge->DestructForwardCount, 1);
	TestFalse(TEXT("...and the widget-side flag clears"), Widget->IsConstructed());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetTickOptInTest,
	"DreamGUI.UserWidget.BlueprintSurface.TickIsOptInAndFlowsThroughTheManager",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetTickOptInTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetBlueprintSurfaceTestLocal;

	FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("a game world"), Scope.World))
	{
		return false;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Scope.World);
	if (!TestNotNull(TEXT("the manager subsystem exists"), Manager))
	{
		return false;
	}

	// One widget that wants to tick, one that never asked. The second is the cost claim: opting out
	// means the manager never visits it.
	UDreamUserWidget* Ticking = MakeUserWidget(Scope.World, nullptr, TEXT("Ticking"));
	UDreamUserWidget* Silent = MakeUserWidget(Scope.World, nullptr, TEXT("Silent"));
	Ticking->SetWantsTick(true);

	UDreamUserWidgetEventBridge* TickingBridge = Cast<UDreamUserWidgetEventBridge>(
		Ticking->GetComponent(UDreamUserWidgetEventBridge::StaticClass()));
	UDreamUserWidgetEventBridge* SilentBridge = Cast<UDreamUserWidgetEventBridge>(
		Silent->GetComponent(UDreamUserWidgetEventBridge::StaticClass()));
	if (!TestTrue(TEXT("both bridges exist"), TickingBridge != nullptr && SilentBridge != nullptr))
	{
		return false;
	}

	Ticking->BeginPlay();
	Silent->BeginPlay();

	// The manager's own frame: Start runs first, then the behaviours whose tick flag is up.
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("an opted-in widget ticks on the first manager frame"), TickingBridge->TickForwardCount, 1);
	TestEqual(TEXT("an opted-out widget does not tick"), SilentBridge->TickForwardCount, 0);
	TestFalse(TEXT("...and its bridge is not even armed"), SilentBridge->IsTickForwardingEnabled());

	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("every manager frame is a tick"), TickingBridge->TickForwardCount, 2);

	// Runtime opt-out stops the flow on the next frame.
	Ticking->SetWantsTick(false);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("opting out stops ticking"), TickingBridge->TickForwardCount, 2);
	TestEqual(TEXT("...while the silent widget is still untouched"), SilentBridge->TickForwardCount, 0);

	// And back in, through the started-behaviour registration path.
	Ticking->SetWantsTick(true);
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("opting back in resumes ticking"), TickingBridge->TickForwardCount, 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetRoutingNeutralityTest,
	"DreamGUI.UserWidget.BlueprintSurface.PointerRoutingIsNeutralUntilTheWidgetOptsOutOfBubbling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetRoutingNeutralityTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetBlueprintSurfaceTestLocal;

	const FVector2D ClickPos(150.0, 150.0);

	// ---- Arrangement A: Root <- Holder(plain widget) <- Inner. The baseline every screen already has.
	int32 BaselineEnter = 0, BaselineDown = 0, BaselineUp = 0, BaselineClick = 0;
	{
		FScopedGameWorld Scope;
		FPointerRig Rig(Scope.World);
		if (!TestTrue(TEXT("the baseline rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamWidget* Root = MakeWidget(Scope.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
		UDreamWidget* Holder = MakeWidget(Scope.World, Root, TEXT("Holder"), 200.0f, 200.0f);
		UDreamWidget* Inner = MakeWidget(Scope.World, Holder, TEXT("Inner"));

		FWidgetEventLog RootLog;
		RootLog.Observe(Root);

		Rig.Press(ClickPos);
		Rig.Frame(Inner);
		TestTrue(TEXT("baseline: the press lands on the inner widget"), Rig.EventData->PressWidget == Inner);
		Rig.Release(ClickPos);
		Rig.Frame(Inner);

		BaselineEnter = RootLog.Enter;
		BaselineDown = RootLog.Down;
		BaselineUp = RootLog.Up;
		BaselineClick = RootLog.Click;
		TestEqual(TEXT("baseline: entering the inner widget entered the root"), BaselineEnter, 1);
		TestEqual(TEXT("baseline: the click bubbled to the root"), BaselineClick, 1);
	}

	// ---- Arrangement B: the Holder is now a plain UDreamUserWidget carrying its bridge. Every number
	// the root observes must match the baseline exactly -- that is what "adding the surface breaks no
	// existing screen" means, measured.
	{
		FScopedGameWorld Scope;
		FPointerRig Rig(Scope.World);
		if (!TestTrue(TEXT("the user-widget rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamWidget* Root = MakeWidget(Scope.World, nullptr, TEXT("Root"), 800.0f, 600.0f);
		UDreamUserWidget* Holder = MakeUserWidget(Scope.World, Root, TEXT("UserHolder"));
		UDreamWidget* Inner = MakeWidget(Scope.World, Holder, TEXT("Inner"));
		if (!TestNotNull(TEXT("the user widget carries its bridge"),
			Holder->GetComponent(UDreamUserWidgetEventBridge::StaticClass())))
		{
			return false;
		}

		FWidgetEventLog RootLog;
		RootLog.Observe(Root);

		Rig.Press(ClickPos);
		Rig.Frame(Inner);
		TestTrue(TEXT("the press still lands on the inner widget, not the user widget"), Rig.EventData->PressWidget == Inner);
		Rig.Release(ClickPos);
		Rig.Frame(Inner);

		TestEqual(TEXT("enter reaches the root exactly as before"), RootLog.Enter, BaselineEnter);
		TestEqual(TEXT("down reaches the root exactly as before"), RootLog.Down, BaselineDown);
		TestEqual(TEXT("up reaches the root exactly as before"), RootLog.Up, BaselineUp);
		TestEqual(TEXT("click reaches the root exactly as before"), RootLog.Click, BaselineClick);

		// The opt-out: bubble-up false consumes events at the user widget's boundary. The root stops
		// hearing anything NEW -- and since the only thing between Inner and Root is the bridge, a
		// stopped bubble is also the proof that delivery runs through NativeOnPointer* and reads the
		// widget's policy.
		Holder->SetAllowEventBubbleUp(false);
		Rig.Press(ClickPos);
		Rig.Frame(Inner);
		TestTrue(TEXT("the press target is policy-independent"), Rig.EventData->PressWidget == Inner);
		Rig.Release(ClickPos);
		Rig.Frame(Inner);

		TestEqual(TEXT("with bubbling off, no second down reaches the root"), RootLog.Down, BaselineDown);
		TestEqual(TEXT("...no second up"), RootLog.Up, BaselineUp);
		TestEqual(TEXT("...and no second click"), RootLog.Click, BaselineClick);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUserWidgetNavigationOptInTest,
	"DreamGUI.UserWidget.BlueprintSurface.NavigationIsInvisibleUntilOptedIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUserWidgetNavigationOptInTest::RunTest(const FString& Parameters)
{
	using namespace DreamUserWidgetBlueprintSurfaceTestLocal;

	FScopedGameWorld Scope;
	if (!TestNotNull(TEXT("a game world"), Scope.World))
	{
		return false;
	}

	UDreamUserWidget* Widget = MakeUserWidget(Scope.World, nullptr, TEXT("Panel"));
	UDreamUserWidgetEventBridge* Bridge = Cast<UDreamUserWidgetEventBridge>(
		Widget->GetComponent(UDreamUserWidgetEventBridge::StaticClass()));
	if (!TestNotNull(TEXT("the bridge exists"), Bridge))
	{
		return false;
	}

	// The default answer is the neutrality guarantee: the navigation search skips components whose
	// CanNavigateHere is false, so an opted-out user widget cannot steal a move from the
	// UISelectables inside it.
	TestFalse(TEXT("navigation cannot land here by default"),
		IDreamNavigationInterface::Execute_CanNavigateHere(Bridge));

	Widget->SetCanNavigateHere(true);
	TestTrue(TEXT("opting in makes this widget a navigation stop"),
		IDreamNavigationInterface::Execute_CanNavigateHere(Bridge));

	// A move on an opted-in widget with no Blueprint body: handled, going nowhere. That is what a
	// navigation sink is.
	TScriptInterface<IDreamNavigationInterface> Result;
	TestTrue(TEXT("a navigation move is accepted"),
		IDreamNavigationInterface::Execute_OnNavigate(Bridge, EDreamUINavigationDirection::Down, Result));
	TestNull(TEXT("...and with no Blueprint answer the highlight stays put"), Result.GetObject());

	// Deactivating the widget takes it back out of the search, opt-in or not.
	Widget->SetWidgetActive(false);
	TestFalse(TEXT("an inactive widget is not a navigation stop"),
		IDreamNavigationInterface::Execute_CanNavigateHere(Bridge));

	return true;
}

#endif
