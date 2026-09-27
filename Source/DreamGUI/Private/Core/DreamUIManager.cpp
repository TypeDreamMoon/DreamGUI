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
#include "Interaction/UISelectable.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Components/DreamVisual.h"
#include "Engine/Engine.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/DreamUIMesh/DreamUIGizmoMesh.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "Event/DreamWorldSpaceRaycaster.h"
#include "GameFramework/Actor.h"
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

void UDreamUIManagerWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_EDITOR
	InstanceArray.Add(this);
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
	if (this->GetWorld()->IsGameWorld() || this->GetWorld()->WorldType == EWorldType::Editor)//game world or editor world, skip editor preview world
	{
		bShouldTickInEditor = true;
	}
	else
	{
		bShouldTickInEditor = false;
	}
	FCoreDelegates::OnEndFrame.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnEndOfFrame);
	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnEnginePreExit);
	UDreamUIManagerObject::GetInstance(true);//make sure it is created
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
#if WITH_EDITOR
	InstanceArray.Remove(this);
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
	// The interaction objects this subsystem spawned are its to take away again. They are transient,
	// so a level change would not carry them anyway; destroying them here is what keeps a PIE session
	// that starts and stops repeatedly from leaving a host actor behind on every run.
	for (TPair<int32, TObjectPtr<AActor>>& HostPair : InteractionHosts)
	{
		if (IsValid(HostPair.Value))
		{
			HostPair.Value->Destroy();
		}
	}
	InteractionHosts.Reset();
	if (IsValid(CreatedEventSystemActor))
	{
		CreatedEventSystemActor->Destroy();
		CreatedEventSystemActor = nullptr;
	}
	DestroyRegisteredWidgetTrees();
	if (MainViewportViewExtension.IsValid())
	{
		MainViewportViewExtension.Reset();
	}
	if (OnCultureChangedDelegateHandle.IsValid())
	{
		FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedDelegateHandle);
	}
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.RemoveAll(this);
	Super::Deinitialize();
}

void UDreamUIManagerWorldSubsystem::BeginDestroy()
{
	check(!IsInitialized());
	DestroyRegisteredWidgetTrees();
	Super::BeginDestroy();
}

void UDreamUIManagerWorldSubsystem::OnCultureChanged()
{
	bShouldUpdateOnCultureChanged = true;
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
	const TArray<TObjectPtr<UDreamWidget>> WidgetsToBeginPlay = AllWidgetArray;
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
		DestroyRegisteredWidgetTrees();
	}
	Super::OnWorldEndPlay(InWorld);
}

#if WITH_EDITOR
TArray<UDreamUIManagerWorldSubsystem*> UDreamUIManagerWorldSubsystem::InstanceArray;
#endif

void UDreamUIManagerWorldSubsystem::RegisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface> InItem)
{
	// Blueprint-callable, and an interface pin left empty arrives here as a null object. This is the
	// only way to subscribe to a culture change, so it is a pin every localised widget touches.
	UObject* Item = InItem.GetObject();
	if (!IsValid(Item))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Register culture changed event was given no object; nothing to register."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	if (auto Instance = GetInstance(Item->GetWorld()))
	{
		Instance->AllCultureChangedArray.AddUnique(Item);
	}
}
void UDreamUIManagerWorldSubsystem::UnregisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface> InItem)
{
	UObject* Item = InItem.GetObject();
	if (!IsValid(Item))
	{
		// Nothing to take off the list, and the list drops dead weak entries on its own.
		return;
	}
	if (auto Instance = GetInstance(Item->GetWorld()))
	{
		Instance->AllCultureChangedArray.RemoveSingle(Item);
	}
}

TArray<UDreamCanvas*> UDreamUIManagerWorldSubsystem::GetCanvasArrayByRenderMode(EDreamRenderMode RenderMode) const
{
	TArray<UDreamCanvas*> CanvasArray;
	for (auto& Canvas : AllCanvasArray)
	{
		if (!Canvas.IsValid())continue;
		if (Canvas->GetActualRenderMode() == RenderMode)
		{
			CanvasArray.Add(Canvas.Get());
		}
	}
	return CanvasArray;
}

#if WITH_EDITOR
void UDreamUIManagerWorldSubsystem::RefreshAllUI(UWorld* InWorld)
{
	for (auto InstanceItem : InstanceArray)
	{
		if (InstanceItem != nullptr)
		{
			if (InWorld != nullptr && InstanceItem->GetWorld() != InWorld)
			{
				continue;
			}
		}
		auto Instance = InstanceItem;
		for (const TWeakObjectPtr<UDreamCanvas>& Canvas : Instance->SnapshotCanvases())
		{
			if (!Instance->IsCanvasStillRegistered(Canvas))continue;
			if (!Canvas->IsRootCanvas())continue;
			if (auto Widget = Canvas->GetWidget())
			{
				Widget->EnsureDataForRebuild();
				Widget->MarkCanvasUpdate(true);
			}
		}
	}
}

#endif

void UDreamUIManagerWorldSubsystem::AddCanvas(UDreamCanvas* InCanvas)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (this->AllCanvasArray.Contains(InCanvas))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	this->AllCanvasArray.AddUnique(InCanvas);
}

void UDreamUIManagerWorldSubsystem::RemoveCanvas(UDreamCanvas* InCanvas)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (!this->AllCanvasArray.Contains(InCanvas))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	this->AllCanvasArray.RemoveSingle(InCanvas);
}

int32 UDreamUIManagerWorldSubsystem::CountCompetingScreenSpaceOverlayCanvases()const
{
	int32 Count = 0;
	for (auto& Canvas : AllCanvasArray)
	{
		if (!Canvas.IsValid())continue;
		if (!Canvas->IsRootCanvas())continue;
		if (Canvas->GetRenderMode() != EDreamRenderMode::ScreenSpaceOverlay)continue;
		// A canvas on an inactive widget is not on screen and is not fighting anyone for it. This
		// is the ordinary state of a widget that has been created but not yet added, so counting it
		// would fire the "only one ScreenSpace UI" error on a page prefab merely being prepared.
		const UDreamWidget* CanvasWidget = Canvas->GetWidget();
		if (CanvasWidget != nullptr && !CanvasWidget->GetWidgetActiveInHierarchy())continue;
		Count++;
	}
	return Count;
}

void UDreamUIManagerWorldSubsystem::ParkWidget(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget) || IsWidgetParked(InWidget))
	{
		return;
	}
	FDreamParkedWidgetEntry& Entry = ParkedWidgets.AddDefaulted_GetRef();
	Entry.Widget = InWidget;
	Entry.ParkedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	InWidget->SetParked(true);
}

bool UDreamUIManagerWorldSubsystem::UnparkWidget(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	const int32 Index = ParkedWidgets.IndexOfByPredicate(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == InWidget; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	ParkedWidgets.RemoveAt(Index);
	InWidget->SetParked(false);
	return true;
}

namespace DreamParkedWidgetConsole
{
	/**
	 * The other half of "held, not lost". Holding created widgets in a named array is what stops
	 * them being collected; being able to list them is what stops that becoming a place things
	 * quietly accumulate.
	 */
	static FAutoConsoleCommandWithWorldAndArgs ListParkedWidgetsCommand(
		TEXT("dreamgui.ListPendingWidgets"),
		TEXT("List widgets created but not yet added to anything, with how long they have been waiting."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World);
				if (DreamUIManager == nullptr)
				{
					UE_LOG(DreamGUI, Log, TEXT("No DreamUI manager for this world."));
					return;
				}
				const TArray<FDreamParkedWidgetEntry>& Parked = DreamUIManager->GetParkedWidgets();
				if (Parked.IsEmpty())
				{
					UE_LOG(DreamGUI, Log, TEXT("No pending widgets."));
					return;
				}
				const double Now = World ? World->GetTimeSeconds() : 0.0;
				UE_LOG(DreamGUI, Log, TEXT("%d pending widget(s):"), Parked.Num());
				for (const FDreamParkedWidgetEntry& Entry : Parked)
				{
					UE_LOG(DreamGUI, Log, TEXT("  %s   waiting %.1fs")
						, Entry.Widget != nullptr ? *Entry.Widget->GetPathDisplayName() : TEXT("<stale>")
						, Now - Entry.ParkedAtSeconds);
				}
			}));
}

int32 UDreamUIManagerWorldSubsystem::SweepExpiredParkedWidgets()
{
	const float LifetimeSeconds = UDreamUISettings::GetParkedWidgetLifetimeSeconds();
	if (LifetimeSeconds <= 0.0f || ParkedWidgets.IsEmpty())
	{
		return 0;//off by default: a slow-but-legitimate caller must not have its widget taken away
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0;
	}
	const double Now = World->GetTimeSeconds();

	TArray<TObjectPtr<UDreamWidget>> Expired;
	for (const FDreamParkedWidgetEntry& Entry : ParkedWidgets)
	{
		if (Entry.Widget != nullptr && (Now - Entry.ParkedAtSeconds) >= (double)LifetimeSeconds)
		{
			Expired.Add(Entry.Widget);
		}
	}
	for (const TObjectPtr<UDreamWidget>& Widget : Expired)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		UE_LOG(DreamGUI, Warning, TEXT("Widget %s was created %.1fs ago and never added to anything; destroying it. Add it with AddChild or AddToViewport, or destroy it yourself. (DreamUI setting: Parked Widget Lifetime Seconds)")
			, *Widget->GetPathDisplayName(), LifetimeSeconds);
		// DestroyWidget rather than letting go: it unregisters and ends play in the right order, so
		// the widget never reaches BeginDestroy still registered, which is the state that logs an
		// error and an on-screen banner from a stack that says nothing about where it came from.
		Widget->DestroyWidget();
	}
	return Expired.Num();
}

bool UDreamUIManagerWorldSubsystem::IsWidgetParked(const UDreamWidget* InWidget)const
{
	return ParkedWidgets.ContainsByPredicate(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == InWidget; });
}

void UDreamUIManagerWorldSubsystem::AddWidget(UDreamWidget* InWidget)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (AllWidgetArray.Contains(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	AllWidgetArray.AddUnique(InWidget);
}

void UDreamUIManagerWorldSubsystem::RemoveWidget(UDreamWidget* InWidget)
{
	ParkedWidgets.RemoveAll(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == nullptr || Entry.Widget == InWidget; });
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (!AllWidgetArray.Contains(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	AllWidgetArray.RemoveSingle(InWidget);
	if (UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(InWidget))
	{
		// A widget only reaches here from OnUnregister, which is teardown -- and a torn-down user
		// widget must stop being polled for its property bindings. IsValid() is no answer to that
		// question: DestroyWidget unregisters, ends play and detaches without ever marking the
		// object garbage, so the poll loop's own sweep went on calling binding source functions on
		// a widget that had already run EndPlay, until the next full GC.
		RemovePropertyBindingUser(UserWidget);
	}
}

void UDreamUIManagerWorldSubsystem::DestroyRegisteredWidgetTrees()
{
	if (AllWidgetArray.IsEmpty())
	{
		return;
	}

	const TArray<TObjectPtr<UDreamWidget>> RegisteredWidgets = AllWidgetArray;
	TSet<UDreamWidget*> Roots;
	for (UDreamWidget* Widget : RegisteredWidgets)
	{
		if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
		{
			continue;
		}
		UDreamWidget* Root = Widget->GetRootWidgetInHierarchyEvenIfUnreachable();
		Roots.Add(Root ? Root : Widget);
	}

	for (UDreamWidget* Root : Roots)
	{
		if (Root != nullptr && !Root->HasAnyFlags(RF_FinishDestroyed))
		{
			Root->DestroyWidget();
		}
	}

	// Corrupt or partially collected hierarchies may not have a usable cached root.
	for (UDreamWidget* Widget : RegisteredWidgets)
	{
		if (Widget != nullptr && !Widget->HasAnyFlags(RF_FinishDestroyed)
			&& (Widget->HasRegistered() || Widget->HasBegunPlay()))
		{
			Widget->DestroyWidget();
		}
	}
}

TSharedPtr<class FDreamUIRenderer, ESPMode::ThreadSafe> UDreamUIManagerWorldSubsystem::GetViewExtension(UWorld* InWorld, bool InCreateIfNotExist)
{
	if (auto Instance = GetInstance(InWorld))
	{
		if (!Instance->MainViewportViewExtension.IsValid())
		{
			if (InCreateIfNotExist)
			{
				Instance->MainViewportViewExtension = FSceneViewExtensions::NewExtension<FDreamUIRenderer>(InWorld, EDreamUIRendererType::ScreenSpace_and_WorldSpace);
			}
		}
		return Instance->MainViewportViewExtension;
	}
	return nullptr;
}

void UDreamUIManagerWorldSubsystem::AddRaycaster(UDreamBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		auto& AllRaycasterArray = Instance->AllRaycasterArray;
		if (AllRaycasterArray.Contains(InRaycaster))return;
		AllRaycasterArray.Add(InRaycaster);
	}
}
void UDreamUIManagerWorldSubsystem::RemoveRaycaster(UDreamBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		int32 index;
		if (Instance->AllRaycasterArray.Find(InRaycaster, index))
		{
			Instance->AllRaycasterArray.RemoveAt(index);
		}
	}
}

void UDreamUIManagerWorldSubsystem::AddSelectable(UUISelectable* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		auto& AllSelectableArray = Instance->AllSelectableArray;
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
		if (AllSelectableArray.Contains(InSelectable))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
#endif
		AllSelectableArray.AddUnique(InSelectable);
	}
}
void UDreamUIManagerWorldSubsystem::RemoveSelectable(UUISelectable* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		int32 index;
		if (Instance->AllSelectableArray.Find(InSelectable, index))
		{
			Instance->AllSelectableArray.RemoveAt(index);
		}
	}
}

UDreamEventSystem* UDreamUIManagerWorldSubsystem::GetEventSystemByUserIndex(int UserIndex)
{
	if (auto ResultPtr = MapUserIndexToEventSystem.Find(UserIndex))
	{
		return ResultPtr->Get();
	}
	return nullptr;
}

void UDreamUIManagerWorldSubsystem::AddEventSystem(UDreamEventSystem* InEventSystem)
{
	if (!IsValid(InEventSystem))return;

	// The entry is a weak pointer, so "a key exists" and "an event system is registered" are different
	// questions. A level reload destroys the old component and leaves its stale entry behind: asking
	// that entry for an owner to name was a null dereference, and reporting it was a duplicate error
	// about a component that no longer exists -- after which the new level's UI was never registered
	// and stopped responding entirely.
	auto InstancePtr = MapUserIndexToEventSystem.Find(InEventSystem->GetUserIndex());
	UDreamEventSystem* Instance = InstancePtr != nullptr ? InstancePtr->Get() : nullptr;
	if (IsValid(Instance) && Instance != InEventSystem)
	{
		const AActor* InstanceOwner = Instance->GetOwner();
		FString ActorName = InstanceOwner == nullptr ? TEXT("(no owner)") :
#if WITH_EDITOR
			InstanceOwner->GetActorLabel();
#else
			InstanceOwner->GetName();
#endif
		FString ErrorMsg = FString::Printf(TEXT("[%s].%d DreamEventSystem component is already exist in actor:%s, pathName:%s, world:%s, multiple DreamEventSystem with same UserIndex in same world is not allowed!")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ActorName, *Instance->GetPathName(), *GetWorld()->GetPathName());
		UE_LOG(DreamGUI, Error, TEXT("%s"), *ErrorMsg);
		GEngine->AddOnScreenDebugMessage(-1, -1, FColor::Red, ErrorMsg);
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(FText::FromString(ErrorMsg), false, 10);
#endif
	}
	else
	{
		MapUserIndexToEventSystem.Add(InEventSystem->GetUserIndex(), InEventSystem);
	}
}

void UDreamUIManagerWorldSubsystem::RemoveEventSystem(UDreamEventSystem* InEventSystem)
{
	if (InEventSystem == nullptr)return;

	const int UserIndex = InEventSystem->GetUserIndex();
	auto InstancePtr = MapUserIndexToEventSystem.Find(UserIndex);
	if (InstancePtr == nullptr)return;
	// Removed by identity, not by user index. An unregister arriving late -- the previous level's event
	// system being destroyed after the new one has already claimed the same index -- used to evict the
	// live registration and leave that player's UI deaf with nothing in the log.
	UDreamEventSystem* Instance = InstancePtr->Get();
	if (Instance == InEventSystem || Instance == nullptr)
	{
		MapUserIndexToEventSystem.Remove(UserIndex);
	}
}

namespace DreamInteractionLocal
{
	/**
	 * The index of the first local player -- the one a null owning player resolves to everywhere else
	 * in the plugin. Usually 0, but it is read rather than assumed so that "the first player" keeps
	 * meaning the same thing here as it does to a widget asking who owns it.
	 */
	int32 FirstLocalPlayerIndex(const UWorld* InWorld)
	{
		return InWorld != nullptr ? UDreamWidget::GetLocalPlayerIndexOf(InWorld->GetFirstPlayerController()) : 0;
	}

	/** Does this player already have a raycaster of this kind, wherever it was placed? */
	bool HasRaycasterOfKind(const UDreamBaseRaycaster* InRaycaster, EDreamInteractionKind InKind)
	{
		return InKind == EDreamInteractionKind::Screen
			? InRaycaster->IsA(UDreamScreenSpaceRaycaster::StaticClass())
			: InRaycaster->IsA(UDreamWorldSpaceRaycaster::StaticClass());
	}
}

void UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer(int32 InUserIndex, EDreamInteractionKind InKind)
{
	UWorld* World = GetWorld();
	if (World == nullptr)return;

	// The event system for THIS player, not "the one at index 0". A second local player with no event
	// system of their own gets nothing rather than borrowing the first player's cursor.
	//
	// The registry is only half the answer: a placed event system enrols itself when it begins play,
	// so during level startup the component can exist while the map does not know about it yet.
	bool bHasEventSystem = GetEventSystemByUserIndex(InUserIndex) != nullptr;
	if (!bHasEventSystem)
	{
		for (TActorIterator<AActor> ActorIt(World); ActorIt; ++ActorIt)
		{
			if (const UDreamEventSystem* PlacedEventSystem = ActorIt->FindComponentByClass<UDreamEventSystem>();
				PlacedEventSystem != nullptr && PlacedEventSystem->GetUserIndex() == InUserIndex)
			{
				bHasEventSystem = true;
				break;
			}
		}
	}
	// Only the first player gets one spawned for them. A second local player's event system has to be
	// placed deliberately -- with its UserIndex set -- because spawning a copy of the default actor
	// would give both players the same index and make each read the other's input.
	if (!bHasEventSystem && InUserIndex != DreamInteractionLocal::FirstLocalPlayerIndex(World))
	{
		UE_LOG(DreamGUI, Warning,
			TEXT("Local player %d has DreamUI to point at but no event system with that UserIndex, so it takes no input. ")
			TEXT("Place a DreamEventSystem with UserIndex %d for that player."), InUserIndex, InUserIndex);
	}
	else if (!bHasEventSystem && !IsValid(CreatedEventSystemActor))
	{
		if (UClass* EventSystemClass = UDreamGUISettings::LoadSettingClass(
			UDreamGUISettings::Get()->EventSystemActorClass, TEXT("EventSystemActorClass")))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = MakeUniqueObjectName(World, EventSystemClass, TEXT("DreamEventSystem"));
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CreatedEventSystemActor = World->SpawnActor<AActor>(EventSystemClass, FTransform::Identity, SpawnParameters);
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("Cannot create DreamUI input: Project Settings > Plugins > Dream GUI > ")
				TEXT("EventSystemActorClass is not set or failed to load."));
		}
	}

	// An authored raycaster wins. Somebody who placed a world-space raycaster on their pawn, or a
	// screen raycaster with a hand-tuned drag threshold, said what they wanted; adding a default one
	// beside it would give that player two rays into the same UI.
	for (const TWeakObjectPtr<UDreamBaseRaycaster>& RaycasterPtr : AllRaycasterArray)
	{
		const UDreamBaseRaycaster* Raycaster = RaycasterPtr.Get();
		if (IsValid(Raycaster) && Raycaster->GetUserIndex() == InUserIndex
			&& DreamInteractionLocal::HasRaycasterOfKind(Raycaster, InKind))
		{
			return;
		}
	}

	TObjectPtr<AActor>& HostSlot = InteractionHosts.FindOrAdd(InUserIndex);
	if (!IsValid(HostSlot))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(World, AActor::StaticClass(),
			*FString::Printf(TEXT("DreamInteractionHost_P%d"), InUserIndex));
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		HostSlot = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (IsValid(HostSlot))
		{
			HostSlot->SetActorEnableCollision(false);
		}
	}
	AActor* Host = HostSlot.Get();
	if (!IsValid(Host))return;
	// Asked again on the host itself, because a raycaster only enrols in AllRaycasterArray when it
	// activates, and a world that has not begun play never activates one. Without this the second
	// call would add a second raycaster to the same host and the function would not be idempotent
	// in exactly the case -- an inactive or headless world -- where nothing else would notice.
	for (UActorComponent* Component : Host->GetComponents())
	{
		const UDreamBaseRaycaster* Existing = Cast<UDreamBaseRaycaster>(Component);
		if (Existing != nullptr && DreamInteractionLocal::HasRaycasterOfKind(Existing, InKind))
		{
			return;
		}
	}

	UDreamBaseRaycaster* NewRaycaster = InKind == EDreamInteractionKind::Screen
		? static_cast<UDreamBaseRaycaster*>(NewObject<UDreamScreenSpaceRaycaster>(Host, NAME_None, RF_Transient))
		: static_cast<UDreamBaseRaycaster*>(NewObject<UDreamWorldSpaceRaycaster>(Host, NAME_None, RF_Transient));
	NewRaycaster->SetUserIndex(InUserIndex);
	Host->AddInstanceComponent(NewRaycaster);
	NewRaycaster->RegisterComponent();
}

AActor* UDreamUIManagerWorldSubsystem::GetInteractionHost(int32 InUserIndex)const
{
	const TObjectPtr<AActor>* Found = InteractionHosts.Find(InUserIndex);
	return Found != nullptr ? Found->Get() : nullptr;
}

#undef LOCTEXT_NAMESPACE
