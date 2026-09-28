// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetLifecycle.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetLifecycleTestTypes.h"
#include "Engine/World.h"

/*
 * WHERE A WIDGET IS IN ITS LIFE.
 *
 * One state per widget -- constructed, registered, in play, destroyed -- and five steps between them, each
 * a no-op outside the state it leaves (EDreamWidgetLifecycle). The two flags it replaced let a widget end
 * play twice, run its parts' EndPlay without ever having begun, and a behaviour woken early be woken again.
 * These pin the table, the order a whole tree leaves play and registration in, and the one early wake-up a
 * behaviour is allowed.
 */

namespace DreamLifecycleStateTestLocal
{
	/** A widget in InWorld with a behaviour recording every lifecycle call it hears into InLog. */
	UDreamWidget* MakeRecordedWidget(UWorld* InWorld, const TCHAR* InName, const TSharedPtr<TArray<FString>>& InLog)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
		Widget->SetDisplayName(InName);
		if (UDreamWidgetLifecycleRecordingBehaviour* Recorder = Widget->AddComponent<UDreamWidgetLifecycleRecordingBehaviour>())
		{
			Recorder->Log = InLog;
		}
		return Widget;
	}

	FString StateOf(const UDreamWidget* InWidget)
	{
		return LexToString(InWidget->GetLifecycle());
	}

	FString Join(const TArray<FString>& InLog)
	{
		return FString::Join(InLog, TEXT(", "));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleStepTableTest,
	"DreamGUI.Lifecycle.EachStepMovesAWidgetOnlyFromTheStateItLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleStepTableTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleStateTestLocal;

	// A game world that has not begun play: behaviours wake only in one, and nothing begins on its own.
	DreamTests::FScopedGameWorld Scope;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Scope.World);
	if (!TestNotNull(TEXT("The world has a UI manager"), Manager))
	{
		return false;
	}
	const TSharedPtr<TArray<FString>> Log = MakeShared<TArray<FString>>();
	UDreamWidget* Widget = MakeRecordedWidget(Scope.World, TEXT("W"), Log);
	TestEqual(TEXT("A new widget is constructed"), StateOf(Widget), FString(TEXT("Constructed")));

	Widget->EndPlay();
	Widget->OnUnregister();
	TestEqual(TEXT("Ending play or unregistering a constructed widget does nothing"), StateOf(Widget), FString(TEXT("Constructed")));
	TestEqual(TEXT("...and reaches none of its parts"), Log->Num(), 0);

	Widget->OnRegister();
	Widget->OnRegister();
	TestEqual(TEXT("Registering moves it to registered"), StateOf(Widget), FString(TEXT("Registered")));
	TestEqual(TEXT("...once, however often it is asked"), Join(*Log), FString(TEXT("Register W")));
	TestTrue(TEXT("...and the manager knows it"), Manager->GetAllWidgetArray().Contains(Widget));

	Log->Reset();
	Widget->BeginPlay();
	Widget->BeginPlay();
	TestEqual(TEXT("Beginning play moves it into play"), StateOf(Widget), FString(TEXT("BegunPlay")));
	TestEqual(TEXT("...waking and enabling its behaviour once"), Join(*Log), FString(TEXT("Awake W, Enable W")));

	Log->Reset();
	Widget->OnUnregister();
	TestEqual(TEXT("Unregistering a widget still in play does nothing"), StateOf(Widget), FString(TEXT("BegunPlay")));
	TestTrue(TEXT("...it stays with the manager"), Manager->GetAllWidgetArray().Contains(Widget));
	TestEqual(TEXT("...and its behaviour hears nothing"), Log->Num(), 0);

	Widget->EndPlay();
	Widget->EndPlay();
	TestEqual(TEXT("Ending play moves it back to registered"), StateOf(Widget), FString(TEXT("Registered")));
	TestEqual(TEXT("...disabling and destroying its behaviour once"), Join(*Log), FString(TEXT("Disable W, Destroy W")));

	Log->Reset();
	Widget->OnUnregister();
	Widget->OnUnregister();
	TestEqual(TEXT("Unregistering moves it back to constructed"), StateOf(Widget), FString(TEXT("Constructed")));
	TestEqual(TEXT("...once"), Join(*Log), FString(TEXT("Unregister W")));
	TestFalse(TEXT("...and the manager lets it go"), Manager->GetAllWidgetArray().Contains(Widget));

	// A second life, and the end of it: destroying a widget in play takes the steps it still owes, in order.
	Widget->OnRegister();
	Widget->BeginPlay();
	Log->Reset();
	Widget->DestroyWidget();
	TestEqual(TEXT("Destroying a widget in play leaves it destroyed"), StateOf(Widget), FString(TEXT("Destroyed")));
	TestEqual(TEXT("...ending its play before unregistering it"), Join(*Log), FString(TEXT("Disable W, Destroy W, Unregister W")));
	TestFalse(TEXT("...and the manager lets it go"), Manager->GetAllWidgetArray().Contains(Widget));

	Log->Reset();
	Widget->BeginPlay();
	Widget->EndPlay();
	Widget->OnUnregister();
	Widget->DestroyWidget();
	TestEqual(TEXT("No step moves a destroyed widget"), StateOf(Widget), FString(TEXT("Destroyed")));
	TestEqual(TEXT("...or reaches its parts"), Log->Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleTreeExitOrderTest,
	"DreamGUI.Lifecycle.DestroyingATreeEndsPlayForAllOfItBeforeAnyOfItUnregisters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleTreeExitOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleStateTestLocal;

	DreamTests::FScopedGameWorld Scope;
	const TSharedPtr<TArray<FString>> Log = MakeShared<TArray<FString>>();
	UDreamWidget* Root = MakeRecordedWidget(Scope.World, TEXT("Root"), Log);
	UDreamWidget* Child = MakeRecordedWidget(Scope.World, TEXT("Child"), Log);
	UDreamWidget* Grandchild = MakeRecordedWidget(Scope.World, TEXT("Grandchild"), Log);
	Child->SetParentBeforeRegister(Root);
	Grandchild->SetParentBeforeRegister(Child);
	RegisterDreamWidgetHierarchy(Root);
	TestEqual(TEXT("A tree registers parents first"), Join(*Log), FString(TEXT("Register Root, Register Child, Register Grandchild")));
	Root->BeginPlay();
	Child->BeginPlay();
	Grandchild->BeginPlay();
	if (!TestTrue(TEXT("The whole tree is in play"), Root->HasBegunPlay() && Child->HasBegunPlay() && Grandchild->HasBegunPlay()))
	{
		Root->DestroyWidget();
		return false;
	}

	Log->Reset();
	Root->DestroyWidget();
	TestEqual(TEXT("Every widget ends play, parents first, before any of them unregisters, parents first"), Join(*Log),
		FString(TEXT("Disable Root, Destroy Root, Disable Child, Destroy Child, Disable Grandchild, Destroy Grandchild, ")
			TEXT("Unregister Root, Unregister Child, Unregister Grandchild")));
	TestTrue(TEXT("...and every one of them is destroyed"),
		Root->GetLifecycle() == EDreamWidgetLifecycle::Destroyed
		&& Child->GetLifecycle() == EDreamWidgetLifecycle::Destroyed
		&& Grandchild->GetLifecycle() == EDreamWidgetLifecycle::Destroyed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLifecycleEarlyWakeTest,
	"DreamGUI.Lifecycle.ABehaviourWokenBeforeItsWidgetBeganPlayIsNotWokenAgainWhenItDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLifecycleEarlyWakeTest::RunTest(const FString& Parameters)
{
	using namespace DreamLifecycleStateTestLocal;

	DreamTests::FScopedGameWorld Scope;
	const TSharedPtr<TArray<FString>> Log = MakeShared<TArray<FString>>();
	UDreamWidget* Widget = MakeRecordedWidget(Scope.World, TEXT("W"), Log);
	Widget->OnRegister();

	// Made active in a game world before it began play -- a screen built during a level's load, say -- a
	// widget wakes its behaviours there and then. Its BeginPlay still follows, and used to wake them again.
	Log->Reset();
	Widget->SetWidgetActive(false);
	Widget->SetWidgetActive(true);
	TestEqual(TEXT("Activating a registered widget before play wakes and enables its behaviour"), Join(*Log), FString(TEXT("Awake W, Enable W")));

	Log->Reset();
	Widget->BeginPlay();
	TestTrue(TEXT("The widget still begins play"), Widget->HasBegunPlay());
	TestEqual(TEXT("...without its behaviour being woken or enabled a second time"), Log->Num(), 0);

	Widget->DestroyWidget();
	TestEqual(TEXT("And it ends once"), Join(*Log), FString(TEXT("Disable W, Destroy W, Unregister W")));

	// Woken early and destroyed without ever beginning play: nothing ends a play that never began, so the
	// behaviour is put to rest as its widget unregisters.
	UDreamWidget* NeverBegun = MakeRecordedWidget(Scope.World, TEXT("N"), Log);
	NeverBegun->OnRegister();
	NeverBegun->SetWidgetActive(false);
	NeverBegun->SetWidgetActive(true);
	Log->Reset();
	NeverBegun->DestroyWidget();
	TestEqual(TEXT("A behaviour woken before a play that never came is disabled and destroyed as its widget goes"), Join(*Log),
		FString(TEXT("Disable N, Destroy N, Unregister N")));
	return true;
}

#endif
