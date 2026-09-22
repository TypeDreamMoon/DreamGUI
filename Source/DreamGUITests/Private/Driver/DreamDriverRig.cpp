// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverRig.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisualEmpty.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"

#include "Driver/DreamDriverInputModule.h"
#include "DreamScopedWorld.h"

FDreamDriverRig FDreamDriverRig::Headless(FIntPoint InViewportSize)
{
	return FDreamDriverRig(InViewportSize);
}

FDreamDriverRig::FDreamDriverRig(const FIntPoint& InViewportSize)
{
	ScopedWorld = MakeUnique<DreamTests::FScopedGameWorld>(EWorldType::Game);
	DriverContext = MakeUnique<FDreamDriverContext>();
	// Created unconditionally, even if the build below goes wrong, so Driver() is always answerable
	// and a test that forgot to check IsUsable fails on an assertion rather than on a null driver.
	DriverInstance = MakeShared<FDreamDriver>(*DriverContext);

	UWorld* BuildWorld = ScopedWorld->World;
	if (BuildWorld == nullptr)
	{
		return;
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
	DriverContext->PumpFrames(2);
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
	ScopedWorld.Reset();
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
	return NewWidget;
}

void FDreamDriverRig::PumpFrames(int32 InFrameCount)
{
	if (DriverContext.IsValid())
	{
		DriverContext->PumpFrames(InFrameCount);
	}
}
