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
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/DreamUIMesh/DreamUIGizmoMesh.h"
#include "CoreGlobals.h"
#include "EngineUtils.h"
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

#undef LOCTEXT_NAMESPACE
