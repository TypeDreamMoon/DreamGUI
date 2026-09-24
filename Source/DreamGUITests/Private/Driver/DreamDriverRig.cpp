// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverRig.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"

#include "Driver/DreamDriverInputModule.h"
#include "DreamScopedGameInstanceWorld.h"
#include "DreamScopedWorld.h"

FDreamDriverRig FDreamDriverRig::Headless(FIntPoint InViewportSize)
{
	FDreamRigOptions ViewportOnly;
	ViewportOnly.ViewportSize = InViewportSize;
	return FDreamDriverRig(ViewportOnly);
}

FDreamDriverRig FDreamDriverRig::Headless(const FDreamRigOptions& InOptions)
{
	return FDreamDriverRig(InOptions);
}

FDreamDriverRig::FDreamDriverRig(const FDreamRigOptions& InOptions)
	: Options(InOptions)
{
	const FIntPoint InViewportSize = Options.ViewportSize;
	DriverContext = MakeUnique<FDreamDriverContext>();
	// Created unconditionally, even if the build below goes wrong, so Driver() is always answerable
	// and a test that forgot to check IsUsable fails on an assertion rather than on a null driver.
	DriverInstance = MakeShared<FDreamDriver>(*DriverContext);

	// 1. The world. With a game instance by default, because the tween manager is a game instance
	// subsystem: without one every UDreamTweenManager::To answers null, every Selectable transition
	// silently snaps or never happens, and the controls are being tested in the designer's preview
	// world rather than in a game's. The bare world stays available for exactly that comparison.
	UWorld* BuildWorld = nullptr;
	if (Options.bWithGameInstance)
	{
		ScopedGameInstanceWorld = MakeUnique<DreamTests::FScopedGameInstanceWorld>();
		BuildWorld = ScopedGameInstanceWorld->World;
		DriverContext->GameInstance = ScopedGameInstanceWorld->GameInstance;
		if (BuildWorld == nullptr)
		{
			return;
		}
	}
	else
	{
		ScopedWorld = MakeUnique<DreamTests::FScopedGameWorld>(EWorldType::Game);
		BuildWorld = ScopedWorld->World;
		if (BuildWorld == nullptr)
		{
			return;
		}
	}
	DriverContext->World = BuildWorld;
	DriverContext->Manager = UDreamUIManagerWorldSubsystem::GetInstance(BuildWorld);

	Host = BuildWorld->SpawnActor<AActor>();
	if (Host == nullptr)
	{
		return;
	}

	UDreamEventSystem* BuiltEventSystem = NewObject<UDreamEventSystem>(Host);
	// AddInstanceComponent before RegisterComponent, the way the world-space raycast fixture does it:
	// it is what makes the component belong to the actor rather than merely be outered to it.
	Host->AddInstanceComponent(BuiltEventSystem);
	BuiltEventSystem->RegisterComponent();
	DriverContext->EventSystem = BuiltEventSystem;

	UDreamDriverInputModule* BuiltInputModule = NewObject<UDreamDriverInputModule>(Host);
	Host->AddInstanceComponent(BuiltInputModule);
	BuiltInputModule->RegisterComponent();
	BuiltInputModule->RegisterInputModuleToEventSystem(BuiltEventSystem);
	DriverContext->InputModule = BuiltInputModule;

	UDreamWidget* BuiltRoot = NewObject<UDreamWidget>(BuildWorld, NAME_None, RF_Public | RF_Transactional);
	BuiltRoot->SetDisplayName(TEXT("Root"));
	BuiltRoot->OnRegister();
	DriverContext->Root = BuiltRoot;

	UDreamCanvas* BuiltCanvas = BuiltRoot->AddComponent<UDreamCanvas>();
	if (BuiltCanvas == nullptr)
	{
		return;
	}
	BuiltCanvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	// AFTER the render mode. Setting the mode applies the viewport parameters, and at that moment the
	// only viewport there is is the 2x2 fallback; handing the canvas a real size is what un-does that,
	// and doing it in the other order would leave the fallback applied on top.
	BuiltCanvas->SetViewportSizeOverride(InViewportSize);
	DriverContext->RootCanvas = BuiltCanvas;

	UDreamScreenSpaceRaycaster* BuiltRaycaster = NewObject<UDreamScreenSpaceRaycaster>(Host);
	BuiltRaycaster->SetRootCanvas(BuiltCanvas);
	Host->AddInstanceComponent(BuiltRaycaster);
	BuiltRaycaster->RegisterComponent();
	// Explicit, although registering an auto-activating component normally gets here by itself: the
	// list this puts it on is the one UDreamPointerInputModule::LineTrace walks, so a rig that was
	// not on it would trace nothing and every test would fail identically and unhelpfully. Enrolling
	// twice is a no-op -- AddRaycaster refuses duplicates.
	BuiltRaycaster->ActivateRaycaster();
	DriverContext->Raycaster = BuiltRaycaster;

	// Two settling frames before anyone touches it. The first runs the layout the attach queued, the
	// second gives anything the first dirtied its own pass -- which is the one-pass convergence the
	// manager's own counter calls healthy. Without them the first action would hit-test a tree whose
	// widgets are all still at the origin.
	// Everything the world starts with exists; now it begins play, before any control is made on it.
	OpenBeginPlayGate();

	DriverContext->PumpFrames(2);
}

void FDreamDriverRig::OpenBeginPlayGate()
{
	UWorld* HostWorld = DriverContext.IsValid() ? DriverContext->World : nullptr;
	UDreamUIManagerWorldSubsystem* HostManager = DriverContext.IsValid() ? DriverContext->Manager : nullptr;
	if (HostWorld == nullptr || !IsValid(HostManager))
	{
		return;
	}

	// The event system's half: what UDreamEventSystem::BeginPlay does when a world begins play is
	// enrol with the UI manager, and that enrolment is all it does. It is done here directly rather
	// than by calling the component's BeginPlay, which would also mark it begun in a world that is not
	// -- and it is guarded, so EnsureGameInputHost, which makes the same call, stays a no-op after it.
	UDreamEventSystem* HostEventSystem = DriverContext->EventSystem;
	if (IsValid(HostEventSystem)
		&& HostManager->GetEventSystemByUserIndex(HostEventSystem->GetUserIndex()) != HostEventSystem)
	{
		HostManager->AddEventSystem(HostEventSystem);
	}

	// The UI manager's half: OnWorldBeginPlay begins every registered widget that has not begun,
	// which at this point is the root and its canvas and nothing else. Once, because the engine base
	// ensures on a second call and a widget's BeginPlay checks it has not begun -- HasBegunPlay is the
	// guard for both. From here RegisterDreamWidgetHierarchy (MakeControl's road) begins every control
	// it registers, and the pump's TickDreamUI runs their Start and Tick; nothing else drives either.
	if (!HostManager->HasBegunPlay())
	{
		HostManager->OnWorldBeginPlay(*HostWorld);
	}
}

FDreamDriverRig::~FDreamDriverRig()
{
	// Reverse order, and the widget tree before the world: DestroyWidget unregisters the tree from
	// the UI manager while the world is still whole, which is where the manager expects to be told.
	if (DriverContext.IsValid())
	{
		if (UDreamWidget* RootWidget = DriverContext->Root; IsValid(RootWidget))
		{
			RootWidget->DestroyWidget();
		}
		if (UDreamScreenSpaceRaycaster* RigRaycaster = DriverContext->Raycaster; IsValid(RigRaycaster))
		{
			RigRaycaster->DeactivateRaycaster();
		}
		if (UDreamDriverInputModule* RigInputModule = DriverContext->InputModule; IsValid(RigInputModule))
		{
			RigInputModule->UnregisterInputModuleFromEventSystem();
		}
	}
	DriverInstance.Reset();
	DriverContext.Reset();
	// Exactly one of these holds the world; the game instance one also shuts its game instance down
	// and takes its world context off the engine's list.
	ScopedWorld.Reset();
	ScopedGameInstanceWorld.Reset();
}

bool FDreamDriverRig::IsUsable() const
{
	return DriverContext.IsValid()
		&& DriverContext->IsUsable()
		&& IsValid(DriverContext->RootCanvas)
		&& IsValid(DriverContext->Raycaster);
}

UWorld* FDreamDriverRig::GetWorld() const
{
	return DriverContext.IsValid() ? DriverContext->World : nullptr;
}

UDreamWidget* FDreamDriverRig::Root() const
{
	return DriverContext.IsValid() ? DriverContext->Root : nullptr;
}

UDreamCanvas* FDreamDriverRig::RootCanvas() const
{
	return DriverContext.IsValid() ? DriverContext->RootCanvas : nullptr;
}

UDreamEventSystem* FDreamDriverRig::EventSystem() const
{
	return DriverContext.IsValid() ? DriverContext->EventSystem : nullptr;
}

UDreamDriverInputModule* FDreamDriverRig::InputModule() const
{
	return DriverContext.IsValid() ? DriverContext->InputModule : nullptr;
}

UDreamScreenSpaceRaycaster* FDreamDriverRig::Raycaster() const
{
	return DriverContext.IsValid() ? DriverContext->Raycaster : nullptr;
}

FDreamDriverContext& FDreamDriverRig::Context() const
{
	return *DriverContext;
}

FDreamDriverRef FDreamDriverRig::Driver() const
{
	return DriverInstance.ToSharedRef();
}

const FDreamRigOptions& FDreamDriverRig::GetOptions() const
{
	return Options;
}

UGameInstance* FDreamDriverRig::GetGameInstance() const
{
	return DriverContext.IsValid() ? DriverContext->GameInstance : nullptr;
}

void FDreamDriverRig::BindTest(FAutomationTestBase* InTest)
{
	if (DriverContext.IsValid())
	{
		DriverContext->CurrentTest = InTest;
	}
}

UDreamWidget* FDreamDriverRig::MakeWidget(const FString& InDisplayName, UDreamWidget* InParent,
	const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr)
	{
		return nullptr;
	}
	UDreamWidget* Parent = InParent != nullptr ? InParent : DriverContext->Root;
	if (!IsValid(Parent))
	{
		return nullptr;
	}

	UDreamWidget* NewWidget = NewObject<UDreamWidget>(DriverContext->World, NAME_None, RF_Public | RF_Transactional);
	NewWidget->SetDisplayName(InDisplayName);
	NewWidget->SetWidth(InSize.X);
	NewWidget->SetHeight(InSize.Y);
	NewWidget->OnRegister();
	NewWidget->TrySetParent(Parent, false);
	NewWidget->SetAnchoredPosition(InAnchoredPosition);
	// Last, once the widget has a parent and therefore a render canvas to be enrolled with.
	NewWidget->CreateNewVisual<UDreamVisualEmpty>();
	// The rule the runtime's own creation roads apply -- UDreamUIBPLibrary's RegisterAndPark and
	// RegisterDreamWidgetHierarchy both begin a widget made after the MANAGER has begun play. Without it
	// a widget made here would sit registered in a begun world without ever having begun, a state no
	// game can reach, and a behaviour added to it later would never Awake.
	if (IsValid(DriverContext->Manager) && DriverContext->Manager->HasBegunPlay() && !NewWidget->HasBegunPlay())
	{
		NewWidget->BeginPlay();
	}
	return NewWidget;
}

UDreamWidget* FDreamDriverRig::MakeControl(TSubclassOf<UDreamUserWidget> InClass, const FString& InDisplayName,
	UDreamWidget* InParent, const FVector2D& InSize, const FVector2D& InAnchoredPosition)
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr || !IsValid(InClass))
	{
		return nullptr;
	}
	UDreamWidget* Parent = InParent != nullptr ? InParent : DriverContext->Root;
	if (!IsValid(Parent))
	{
		return nullptr;
	}
	// Before the control exists, so nothing it does on the way in -- a part taking the selection, a
	// field beginning an edit -- meets a world without the host a game would have given it.
	EnsureGameInputHost();

	// The runtime's own factory, not a copy of it. What it does in order -- instance, Initialize,
	// parent before register, register the whole hierarchy -- is exactly the part a fixture would get
	// subtly wrong by hand, and the callback is the seam it offers for writing properties before
	// anything registered can observe them.
	UDreamUserWidget* Control = CreateDreamWidget(DriverContext->World, InClass, Parent,
		[&InDisplayName, &InSize](UDreamUserWidget* InBuilt)
		{
			InBuilt->SetDisplayName(InDisplayName);
			InBuilt->SetWidth(InSize.X);
			InBuilt->SetHeight(InSize.Y);
		});
	if (Control == nullptr)
	{
		return nullptr;
	}
	// After registration, as in MakeWidget: the anchor is resolved against the parent the control is
	// now registered under.
	Control->SetAnchoredPosition(InAnchoredPosition);
	return Control;
}

void FDreamDriverRig::EnsureGameInputHost()
{
	if (!DriverContext.IsValid() || DriverContext->World == nullptr)
	{
		return;
	}
	UWorld* HostWorld = DriverContext->World;

	UDreamUIManagerWorldSubsystem* HostManager = DriverContext->Manager;
	UDreamEventSystem* HostEventSystem = DriverContext->EventSystem;
	if (IsValid(HostManager) && IsValid(HostEventSystem)
		&& HostManager->GetEventSystemByUserIndex(HostEventSystem->GetUserIndex()) != HostEventSystem)
	{
		// The same call UDreamEventSystem::BeginPlay makes. Not BeginPlay itself: that would also mark
		// the component as having begun play in a world that never did, and nothing here needs the
		// rest of what that means.
		HostManager->AddEventSystem(HostEventSystem);
	}

	APlayerController* HostController = HostWorld->GetFirstPlayerController();
	if (HostController == nullptr)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.ObjectFlags |= RF_Transient;
		HostController = HostWorld->SpawnActor<APlayerController>(SpawnParameters);
		if (HostController != nullptr)
		{
			// Spawning alone does NOT put it on the world's controller list here. That happens in
			// AController::PostInitializeComponents, and AActor::PostActorConstruction runs
			// Pre/PostInitializeComponents only when World->AreActorsInitialized() -- which a world made
			// with UWorld::CreateWorld never is, because nothing ran InitializeActorsForPlay on it. Off the
			// list, the controller is invisible to GetFirstPlayerController and to
			// UGameplayStatics::GetPlayerController, so the text field's key agent found no player to
			// enable its input on, kept a null InputComponent, and BindKeys dereferenced it. This is the
			// one call the skipped PostInitializeComponents would have made that anything here needs.
			HostWorld->AddController(HostController);
		}
	}
	if (HostController != nullptr && HostController->PlayerInput == nullptr)
	{
		// What a game gives the controller when a local player is assigned to it (SetPlayer): its
		// PlayerInput and its own InputComponent. It also pushes input onto every AutoReceiveInput actor
		// that registered while there was no controller to enable it on (ULevel::PushPendingAutoReceiveInput),
		// which is the other half of how a field's key agent gets its InputComponent. AFTER AddController:
		// that push finds the controller's player index by walking the world's controller list.
		HostController->InitInputSystem();
	}
}

void FDreamDriverRig::PumpFrames(int32 InFrameCount)
{
	if (DriverContext.IsValid())
	{
		DriverContext->PumpFrames(InFrameCount);
	}
}
