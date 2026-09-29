// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamWidgetLifecycle.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetLifecycleTestTypes.h"
#include "Engine/World.h"
#include "Event/DreamUIInputSubsystem.h"
#include "Interaction/DreamUIActionRouter.h"
#include "Interaction/DreamUIDragDrop.h"
#include "Interaction/DreamUIModal.h"
#include "Interaction/DreamUINavigationStack.h"
#include "Interaction/DreamUIPopupLayer.h"
#include "Interaction/DreamUITooltip.h"
#include "Interaction/DreamUIVirtualCursor.h"
#include "UObject/StrongObjectPtr.h"

/*
 * HOW A WORLD ENDS ITS DREAMGUI.
 *
 * Every DreamGUI service of a world -- the screen and popup layers, the input services -- comes down on one
 * path, the manager's, highest priority first and before the trees, rather than in whatever order the
 * engine happened to create the services in.
 */

namespace DreamWorldServiceTeardownTestLocal
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

	FString Join(const TArray<FString>& InLog)
	{
		return FString::Join(InLog, TEXT(", "));
	}

	UDreamWorldServiceProbe* MakeProbe(UDreamUIManagerWorldSubsystem* InManager, int32 InPriority, const TCHAR* InLabel,
		const TSharedPtr<TArray<FString>>& InLog)
	{
		UDreamWorldServiceProbe* Probe = NewObject<UDreamWorldServiceProbe>(GetTransientPackage());
		Probe->Priority = InPriority;
		Probe->Label = InLabel;
		Probe->Log = InLog;
		InManager->RegisterWorldService(Probe, Probe);
		return Probe;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldTeardownOrderTest,
	"DreamGUI.Core.AWorldTearsItsServicesDownHighestPriorityFirstAndOnlyThenItsTrees",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldTeardownOrderTest::RunTest(const FString& Parameters)
{
	using namespace DreamWorldServiceTeardownTestLocal;
	namespace Priority = DreamUI::WorldServiceTeardownPriority;

	DreamTests::FScopedGameWorld Scope;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(Scope.World);
	if (!TestNotNull(TEXT("The world has a UI manager"), Manager))
	{
		return false;
	}
	const TSharedPtr<TArray<FString>> Log = MakeShared<TArray<FString>>();
	// Enrolled in no particular order, as services initialize.
	const TStrongObjectPtr<UDreamWorldServiceProbe> Hosts(MakeProbe(Manager, Priority::Hosts, TEXT("hosts"), Log));
	const TStrongObjectPtr<UDreamWorldServiceProbe> Input(MakeProbe(Manager, Priority::Input, TEXT("input"), Log));
	const TStrongObjectPtr<UDreamWorldServiceProbe> Layers(MakeProbe(Manager, Priority::Layers, TEXT("layers"), Log));
	TestTrue(TEXT("An enrolled service is known to the manager"), Manager->HasWorldService(Input.Get()));
	UDreamWidget* Tree = MakeRecordedWidget(Scope.World, TEXT("Tree"), Log);
	Tree->OnRegister();

	// The world comes down the way a level editor's or a test's does: cleaned up without having played.
	Log->Reset();
	UWorld* World = Scope.World;
	Scope.World = nullptr;
	World->DestroyWorld(false);
	TestTrue(TEXT("The manager took its world down"), Manager->HasTornDownWorld());
	TestEqual(TEXT("...the services highest priority first, and the tree still registered after them"), Join(*Log),
		FString(TEXT("input, layers, hosts, Unregister Tree")));
	TestEqual(TEXT("...the tree coming down with the world"), FString(LexToString(Tree->GetLifecycle())), FString(TEXT("Destroyed")));
	TestFalse(TEXT("...and forgot the services it took down"), Manager->HasWorldService(Input.Get()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldServicesEnrolTest,
	"DreamGUI.Core.EveryDreamGUIServiceOfAGameWorldEnrolsForItsTeardown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWorldServicesEnrolTest::RunTest(const FString& Parameters)
{
	DreamTests::FScopedGameWorld Scope;
	UWorld* World = Scope.World;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	if (!TestNotNull(TEXT("The world has a UI manager"), Manager))
	{
		return false;
	}
	struct FNamedService
	{
		const TCHAR* Name;
		UObject* Service;
	};
	const FNamedService Services[] = {
		{ TEXT("screen layer"), World->GetSubsystem<UDreamScreenUISubsystem>() },
		{ TEXT("popup layer"), World->GetSubsystem<UDreamUIPopupLayer>() },
		{ TEXT("input"), World->GetSubsystem<UDreamUIInputSubsystem>() },
		{ TEXT("action router"), World->GetSubsystem<UDreamUIActionRouter>() },
		{ TEXT("navigation stack"), World->GetSubsystem<UDreamUINavigationStack>() },
		{ TEXT("drag and drop"), World->GetSubsystem<UDreamUIDragDropSubsystem>() },
		{ TEXT("modals"), World->GetSubsystem<UDreamUIModalSubsystem>() },
		{ TEXT("tooltips"), World->GetSubsystem<UDreamUITooltipSubsystem>() },
		{ TEXT("virtual cursor"), World->GetSubsystem<UDreamUIVirtualCursorSubsystem>() },
	};
	for (const FNamedService& Entry : Services)
	{
		if (TestNotNull(FString::Printf(TEXT("A game world has the %s"), Entry.Name), Entry.Service))
		{
			TestTrue(FString::Printf(TEXT("...and it is enrolled for the world's teardown: %s"), Entry.Name), Manager->HasWorldService(Entry.Service));
		}
	}

	Scope.World = nullptr;
	World->DestroyWorld(false);
	TestTrue(TEXT("Destroying the world took them all down on the manager's path"), Manager->HasTornDownWorld());
	return true;
}

#endif
