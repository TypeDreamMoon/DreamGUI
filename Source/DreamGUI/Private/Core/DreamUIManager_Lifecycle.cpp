// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIManager.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamGUISettings.h"

#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/GameInstance.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamBaseRaycaster.h"
#include "Engine/World.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Components/DreamVisual.h"
#include "Engine/Engine.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "DreamUIRender/DreamUIGizmoMesh.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Core/DreamUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "DreamUIManager"
#define ENABLED_DreamGUI_DEBUG_DUMP				0
#define ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME		0
#if WITH_EDITOR

void UDreamUIManagerWorldSubsystem::OnEnginePreExit()
{
	// for (TObjectIterator<UDreamUIPrefab> Itr; Itr; ++Itr)
	// {
	// 	auto Prefab = *Itr;
	// 	Prefab->ClearDesignerScene();
	// }
}
#endif

bool UDreamUIManagerWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

bool UDreamUIManagerWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::Editor || WorldType == EWorldType::PIE
		|| WorldType == EWorldType::EditorPreview;
}

void UDreamUIManagerWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Every world comes down through its cleanup, whether or not it ever played: the one moment an
	// editor or preview world announces that it is going, and the fallback for a game world that ended
	// without EndPlay reaching this manager. See TeardownWorld.
	FWorldDelegates::OnWorldCleanup.AddUObject(this, &UDreamUIManagerWorldSubsystem::HandleWorldCleanup);
	FWorldDelegates::LevelRemovedFromWorld.AddUObject(this, &UDreamUIManagerWorldSubsystem::HandleLevelRemovedFromWorld);
#if WITH_EDITOR
	if (this->GetWorld()->WorldType == EWorldType::EditorPreview//EditorPreview world don't tick, so manually tick it
		|| this->GetWorld()->WorldType == EWorldType::Editor)
	{
		EditorTickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("DreamUIManagerWorldSubsystemEditorTick"), 0, [WeakThis = MakeWeakObjectPtr(this)](float DeltaTime) {
			if (WeakThis.IsValid())
			{
				WeakThis->Tick(DeltaTime);
				return true;
			}
			return false;
			});
	}
	if (this->GetWorld()->IsGameWorld() || this->GetWorld()->WorldType == EWorldType::Editor)//game world or editor world; a preview ticks when its host says so (the designer does)
	{
		bShouldTickInEditor = true;
	}
	else
	{
		bShouldTickInEditor = false;
	}
	FCoreDelegates::OnEndFrame.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnEndOfFrame);
	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnEnginePreExit);
#endif
	//localization
	OnCultureChangedDelegateHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UDreamUIManagerWorldSubsystem::OnCultureChanged);
}
void UDreamUIManagerWorldSubsystem::PostInitialize()
{
	Super::PostInitialize();
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnWorldPreSendAllEndOfFrameUpdates);
}
void UDreamUIManagerWorldSubsystem::Deinitialize()
{
	// Passive. The engine broadcasts a world's cleanup before it deinitializes the world's subsystems,
	// and the cleanup has taken everything down (HandleWorldCleanup). A manager deinitialized without
	// that is one whose world came down in an order nobody chose; it is still taken down here, and said
	// so unless the whole engine is on its way out.
	if (!bWorldTornDown)
	{
		if (IsEngineExitRequested())
		{
			UE_LOG(DreamGUI, Log, TEXT("%s: deinitialized at exit before its world was torn down; tearing it down now."), *GetPathName());
		}
		else
		{
			ensureMsgf(false, TEXT("%s: deinitialized before its world was torn down; tearing it down now."), *GetPathName());
		}
		TeardownWorld();
	}
#if WITH_EDITOR
	if (EditorTickDelegateHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorTickDelegateHandle);
		EditorTickDelegateHandle.Reset();
	}
	// Both of these are subscribed in Initialize, so they have to come off here: a subsystem is
	// created and destroyed on every PIE start/stop, and AddUObject being a weak binding only means
	// the stale entries do not crash -- they still accumulate in two global multicast arrays for the
	// length of an editor session, and this object still receives one more OnEndOfFrame between
	// Deinitialize and its own collection.
	FCoreDelegates::OnEndFrame.RemoveAll(this);
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);
	OnDeinitialize.Broadcast();
#endif
	if (MainViewportViewExtension.IsValid())
	{
		MainViewportViewExtension.Reset();
	}
	if (OnCultureChangedDelegateHandle.IsValid())
	{
		FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedDelegateHandle);
	}
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.RemoveAll(this);
	FWorldDelegates::OnWorldCleanup.RemoveAll(this);
	FWorldDelegates::LevelRemovedFromWorld.RemoveAll(this);
	Super::Deinitialize();
}

void UDreamUIManagerWorldSubsystem::BeginDestroy()
{
	// Nothing is taken down here: this runs inside a collection, and a world's DreamGUI comes down on
	// TeardownWorld, outside one. Reaching it still initialized means the engine collected the world's
	// subsystems without deinitializing them -- it says so itself -- and any tree still registered then
	// is reported by the widgets' own BeginDestroy.
	UE_CLOG(IsInitialized(), DreamGUI, Warning, TEXT("%s: collected without having been deinitialized."), *GetPathName());
	Super::BeginDestroy();
}

void UDreamUIManagerWorldSubsystem::RegisterWorldService(UObject* InServiceObject, IDreamUIWorldService* InService)
{
	if (!ensureMsgf(InServiceObject != nullptr && InService != nullptr, TEXT("%s: a world service enrolled as nothing."), *GetPathName()))
	{
		return;
	}
	// Late is a mistake worth saying: the teardown this enrols for has already run, so nothing will
	// ever take this service down but its own Deinitialize.
	ensureMsgf(!bWorldTornDown, TEXT("%s: %s enrolled for a teardown that has already happened."), *GetPathName(), *InServiceObject->GetPathName());
	if (WorldServices.ContainsByPredicate([InServiceObject](const FWorldServiceEntry& Entry) { return Entry.Object.Get() == InServiceObject; }))
	{
		return;
	}
	WorldServices.Add({ InServiceObject, InService });
}

void UDreamUIManagerWorldSubsystem::UnregisterWorldService(const UObject* InServiceObject)
{
	WorldServices.RemoveAll([InServiceObject](const FWorldServiceEntry& Entry)
	{
		return !Entry.Object.IsValid() || Entry.Object.Get() == InServiceObject;
	});
}

bool UDreamUIManagerWorldSubsystem::HasWorldService(const UObject* InServiceObject) const
{
	return InServiceObject != nullptr && WorldServices.ContainsByPredicate([InServiceObject](const FWorldServiceEntry& Entry)
	{
		return Entry.Object.Get() == InServiceObject;
	});
}

void UDreamUIManagerWorldSubsystem::TeardownWorld()
{
	if (bWorldTornDown)
	{
		return;
	}
	bWorldTornDown = true;
	UWorld* World = GetWorld();

	// The services, highest priority first: the input services stop captures, drags and the cursor
	// before the layers let go of their roots, and all of it before any tree comes down under them.
	// The list is taken whole first, because a service's teardown is free to reach this manager.
	TArray<IDreamUIWorldService*> Services;
	for (const FWorldServiceEntry& Entry : WorldServices)
	{
		if (Entry.Object.IsValid() && Entry.Service != nullptr)
		{
			Services.Add(Entry.Service);
		}
	}
	WorldServices.Reset();
	DreamUI::SortForTeardown(Services);
	if (World != nullptr)
	{
		for (IDreamUIWorldService* Service : Services)
		{
			Service->TeardownForWorld(*World);
		}
	}

	// Then the hosts' trees: every host lets its own go, as it would if it were destroyed. What is still
	// registered after that is the pool's -- trees nobody hosts, which are the manager's to take down --
	// and any tree a host failed to let go of, which is reported.
	ReleaseHostTrees(EDreamTreeReleaseReason::HostDestroyed);
	TreeHosts.Reset();
	DestroyRegisteredWidgetTrees(/*bInReportTreesOutlivingHosts*/ true);

	// Last, what the manager kept for those trees. Unregistering took nearly all of it with it; a layout
	// pass still open is the one thing that could have outlived them, and it is a bug to say so about.
	ensureMsgf(LayoutPassContext.IsBalanced(), TEXT("%s: a layout pass was still open when its world was torn down (depth %d, memo depth %d, %d writer(s))."),
		*GetPathName(), LayoutPassContext.GetPassDepth(), LayoutPassContext.GetMemoDepth(), LayoutPassContext.GetWriterCount());
	ParkedWidgets.Reset();
}

void UDreamUIManagerWorldSubsystem::HandleWorldCleanup(UWorld* InWorld, bool bInSessionEnded, bool bInCleanupResources)
{
	// Only a cleanup that releases the world's resources: that one is followed by Deinitialize. One
	// that keeps them leaves the world standing, and its trees with it.
	if (InWorld != nullptr && InWorld == GetWorld() && bInCleanupResources)
	{
		TeardownWorld();
	}
}

bool DreamUI::EnrolWorldService(FSubsystemCollectionBase& InCollection, UObject& InServiceObject, IDreamUIWorldService& InService)
{
	UDreamUIManagerWorldSubsystem* Manager = InCollection.InitializeDependency<UDreamUIManagerWorldSubsystem>();
	if (Manager == nullptr)
	{
		return false;
	}
	Manager->RegisterWorldService(&InServiceObject, &InService);
	return true;
}

UDreamUIManagerWorldSubsystem* UDreamUIManagerWorldSubsystem::GetInstance(UWorld* InWorld)
{
	return IsValid(InWorld) ? InWorld->GetSubsystem<UDreamUIManagerWorldSubsystem>() : nullptr;
}

#if WITH_EDITOR
UDreamUISelection* UDreamUIManagerWorldSubsystem::GetSelection() const
{
	if (!IsValid(Selection))
	{
		Selection = NewObject<UDreamUISelection>();
		Selection->SetFlags(RF_Transactional);
	}
	return Selection;
}

void UDreamUIManagerWorldSubsystem::MarkDreamUIWidgetOutlinerChanged()
{
	bDreamUIWidgetOutlinerChanged = true;
}
#endif

void UDreamUIManagerWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	//normally BeginPlay is already called when LoadPrefab, but if World has not BeginPlay then this will work
	//
	// Snapshot first, and re-check each entry, the way UDreamWidget::BeginPlay/EndPlay/OnRegister do:
	// an Awake is free to create or destroy widgets, and RemoveWidget uses RemoveSingle, which shifts
	// everything after it down -- walking the live array by index skipped the widget that moved into
	// the slot just visited. A pending-kill widget can also still be in the array, so IsValid is not
	// optional here either. HasRegistered answers the other half: a widget in the snapshot that has
	// since been unregistered was torn down while this loop was running, and BeginPlay on it would
	// restart a widget that is on its way out.
	const TArray<UDreamWidget*> WidgetsToBeginPlay = GetRegisteredWidgets();
	for (UDreamWidget* Widget : WidgetsToBeginPlay)
	{
		if (IsValid(Widget) && Widget->HasRegistered() && !Widget->HasBegunPlay())
		{
			Widget->BeginPlay();
		}
	}
}

void UDreamUIManagerWorldSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
#if WITH_EDITOR
	OnEndPlay.Broadcast();
	if (this->GetWorld()->IsGameWorld())//game mode should deinit when EndPlay
#endif
	{
		// Every actor has ended play by now, so every host has let its tree go; what the teardown finds
		// still registered is what the services hold, and what nobody did.
		TeardownWorld();
	}
	Super::OnWorldEndPlay(InWorld);
}

#if WITH_EDITOR
void UDreamUIManagerWorldSubsystem::RefreshAllUI(UWorld* InWorld)
{
	// InWorld's manager, or every live one. Found by asking for the objects rather than kept in a list
	// of our own: a static list of subsystems was process-wide state, and it held on to a manager past
	// its world whenever a Deinitialize was skipped.
	TArray<UDreamUIManagerWorldSubsystem*> Managers;
	if (InWorld != nullptr)
	{
		if (UDreamUIManagerWorldSubsystem* Manager = GetInstance(InWorld))
		{
			Managers.Add(Manager);
		}
	}
	else
	{
		for (TObjectIterator<UDreamUIManagerWorldSubsystem> It(RF_ClassDefaultObject, true, EInternalObjectFlags::Garbage); It; ++It)
		{
			if (It->IsInitialized() && !It->HasTornDownWorld())
			{
				Managers.Add(*It);
			}
		}
	}
	for (UDreamUIManagerWorldSubsystem* Instance : Managers)
	{
		for (const TWeakObjectPtr<UDreamCanvas>& Canvas : Instance->SnapshotCanvases())
		{
			if (!Instance->IsCanvasStillRegistered(Canvas))continue;
			if (!Canvas->IsRootCanvas())continue;
			if (auto Widget = Canvas->GetWidget())
			{
				Widget->EnsureDataForRebuild();
				// The canvas, not its widget: a widget asking wakes the canvas for that widget alone.
				Canvas->MarkCanvasUpdate(true);
			}
		}
	}
}

#endif

#undef LOCTEXT_NAMESPACE
