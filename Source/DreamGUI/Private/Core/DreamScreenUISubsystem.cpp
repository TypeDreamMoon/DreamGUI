// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamGUISettings.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "DreamGUI.h"
#include "PrefabSystem/DreamUIPrefab.h"

UDreamScreenUISubsystem* UDreamScreenUISubsystem::Get(UWorld* InWorld)
{
	return InWorld ? InWorld->GetSubsystem<UDreamScreenUISubsystem>() : nullptr;
}

UDreamScreenUISubsystem* UDreamScreenUISubsystem::GetDreamScreenUISubsystem(UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}
	return Get(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull));
}

bool UDreamScreenUISubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void UDreamScreenUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UDreamUIManagerWorldSubsystem>();
}

void UDreamScreenUISubsystem::Deinitialize()
{
	for (TPair<FName, FPendingPageLoad>& Pair : PendingPageLoads)
	{
		if (Pair.Value.Handle.IsValid())
		{
			Pair.Value.Handle->CancelHandle();
		}
	}
	PendingPageLoads.Reset();
	PageDefinitions.Reset();
	RemoveAllUI();

	if (IsValid(InteractionHost))
	{
		InteractionHost->Destroy();
		InteractionHost = nullptr;
	}
	if (bOwnsScreenRoot && IsUsablePage(ScreenRoot))
	{
		ScreenRoot->DestroyWidget();
	}
	ScreenRoot = nullptr;
	bOwnsScreenRoot = false;

	if (IsValid(CreatedEventSystemActor))
	{
		CreatedEventSystemActor->Destroy();
		CreatedEventSystemActor = nullptr;
	}

	Super::Deinitialize();
}

bool UDreamScreenUISubsystem::IsUsablePage(const UDreamWidget* InRoot) const
{
	return IsValid(InRoot) && InRoot->HasRegistered() && InRoot->GetWorld() == GetWorld();
}

UDreamCanvas* UDreamScreenUISubsystem::GetScreenCanvas() const
{
	return IsUsablePage(ScreenRoot) ? ScreenRoot->GetComponent<UDreamCanvas>() : nullptr;
}

UDreamWidget* UDreamScreenUISubsystem::GetOrCreateScreenRoot()
{
	if (UDreamCanvas* ExistingCanvas = GetScreenCanvas())
	{
		if (ExistingCanvas->IsRootCanvas() && ExistingCanvas->GetActualRenderMode() == EDreamRenderMode::ScreenSpaceOverlay)
		{
			EnsureInteractionObjects(ExistingCanvas);
			return ScreenRoot;
		}
	}

	ScreenRoot = nullptr;
	bOwnsScreenRoot = false;
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		for (const TWeakObjectPtr<UDreamCanvas>& CanvasPtr : Manager->GetAllCanvasArray())
		{
			UDreamCanvas* Canvas = CanvasPtr.Get();
			if (IsValid(Canvas) && Canvas->IsRootCanvas() && Canvas->GetActualRenderMode() == EDreamRenderMode::ScreenSpaceOverlay)
			{
				ScreenRoot = Canvas->GetWidget();
				EnsureInteractionObjects(Canvas);
				return ScreenRoot;
			}
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const FName RootName = MakeUniqueObjectName(World, UDreamWidget::StaticClass(), TEXT("DreamScreenRoot"));
	ScreenRoot = NewObject<UDreamWidget>(World, RootName, RF_Transient);
	ScreenRoot->SetDisplayName(TEXT("[DreamScreenRoot]"));
	ScreenRoot->SetSizeDelta(FVector2D(1920.0, 1080.0));
	ScreenRoot->OnRegister();

	UDreamCanvas* Canvas = ScreenRoot->AddComponent<UDreamCanvas>();
	if (!Canvas)
	{
		ScreenRoot->DestroyWidget();
		ScreenRoot = nullptr;
		return nullptr;
	}
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	bOwnsScreenRoot = true;

	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World); Manager && Manager->HasBegunPlay())
	{
		ScreenRoot->BeginPlay();
	}
	ScreenRoot->CalculateObjectToWorldTransform(true);
	EnsureInteractionObjects(Canvas);
	return ScreenRoot;
}

void UDreamScreenUISubsystem::EnsureInteractionObjects(UDreamCanvas* InRootCanvas)
{
	if (!IsValid(InRootCanvas))
	{
		return;
	}

	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld());
	bool bHasScreenRaycaster = false;
	if (Manager)
	{
		for (const TWeakObjectPtr<UDreamBaseRaycaster>& Raycaster : Manager->GetAllRaycasterArray())
		{
			if (UDreamScreenSpaceRaycaster* ScreenRaycaster = Cast<UDreamScreenSpaceRaycaster>(Raycaster.Get()))
			{
				ScreenRaycaster->SetRootCanvas(InRootCanvas);
				bHasScreenRaycaster = true;
			}
		}
	}

	if (!bHasScreenRaycaster && !IsValid(InteractionHost))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(GetWorld(), AActor::StaticClass(), TEXT("DreamScreenInteractionHost"));
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		InteractionHost = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (InteractionHost)
		{
			InteractionHost->SetActorEnableCollision(false);
			UDreamScreenSpaceRaycaster* Raycaster = NewObject<UDreamScreenSpaceRaycaster>(InteractionHost, NAME_None, RF_Transient);
			Raycaster->SetRootCanvas(InRootCanvas);
			InteractionHost->AddInstanceComponent(Raycaster);
			Raycaster->RegisterComponent();
		}
	}

	bool bHasEventSystem = Manager && Manager->GetEventSystemByUserIndex(0) != nullptr;
	if (!bHasEventSystem)
	{
		for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
		{
			if (ActorIt->FindComponentByClass<UDreamEventSystem>())
			{
				bHasEventSystem = true;
				break;
			}
		}
	}
	if (!bHasEventSystem && !IsValid(CreatedEventSystemActor))
	{
		if (UClass* EventSystemClass = UDreamGUISettings::LoadSettingClass(
			UDreamGUISettings::Get()->EventSystemActorClass, TEXT("EventSystemActorClass")))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = MakeUniqueObjectName(GetWorld(), EventSystemClass, TEXT("DreamEventSystem"));
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CreatedEventSystemActor = GetWorld()->SpawnActor<AActor>(EventSystemClass, FTransform::Identity, SpawnParameters);
		}
		else
		{
			UE_LOG(DreamGUI, Error, TEXT("Cannot create screen UI input: Project Settings > Plugins > Dream GUI > ")
				TEXT("EventSystemActorClass is not set or failed to load."));
		}
	}
}

void UDreamScreenUISubsystem::ConfigurePage(UDreamWidget* InRoot, int32 InSortOrder)
{
	UDreamWidget* Root = GetOrCreateScreenRoot();
	if (!IsUsablePage(InRoot) || !IsUsablePage(Root) || InRoot == Root)
	{
		return;
	}
	// A widget arriving here parked was created to be shown, so switching it on is right -- but ask
	// before attaching, because attaching is what un-parks it.
	auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld());
	const bool bWasParked = DreamUIManager != nullptr && DreamUIManager->IsWidgetParked(InRoot);
	if (InRoot->GetParent() != Root)
	{
		InRoot->SetParent(Root, false);
	}

	InRoot->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	InRoot->SetAnchoredPosition(FVector2D::ZeroVector);
	InRoot->SetSizeDelta(FVector2D::ZeroVector);
	if (!bWasParked || InRoot->GetWidgetActive())
	{
		// AddToViewport means "show this", so a page that was merely parked gets switched on. What
		// it must not do is override a caller who explicitly switched the page off while preparing
		// it -- parking no longer touches that flag, so the caller's intent is readable here.
		InRoot->SetWidgetActive(true);
	}

	UDreamCanvas* PageCanvas = InRoot->GetComponent<UDreamCanvas>();
	if (!PageCanvas)
	{
		PageCanvas = InRoot->AddComponent<UDreamCanvas>();
	}
	if (PageCanvas)
	{
		PageCanvas->SetOverrideSorting(true);
		PageCanvas->SetSortOrder(FMath::Clamp(InSortOrder, static_cast<int32>(MIN_int16), static_cast<int32>(MAX_int16)), true);
	}
}

FName UDreamScreenUISubsystem::FindNameForWidget(const UDreamWidget* InRoot) const
{
	if (!InRoot)
	{
		return NAME_None;
	}
	for (const TPair<FName, FEntry>& Pair : Entries)
	{
		if (Pair.Value.Root.Get() == InRoot)
		{
			return Pair.Key;
		}
	}
	return NAME_None;
}

void UDreamScreenUISubsystem::AddToViewport(UDreamWidget* InRoot, int32 InSortOrder)
{
	if (!IsUsablePage(InRoot) || InRoot == ScreenRoot)
	{
		return;
	}
	const FName ExistingName = FindNameForWidget(InRoot);
	if (!ExistingName.IsNone())
	{
		ConfigurePage(InRoot, InSortOrder);
		Entries[ExistingName].SortOrder = InSortOrder;
		return;
	}

	FName AutoName;
	do
	{
		AutoName = FName(*FString::Printf(TEXT("__Viewport_%d"), AutoNameCounter++));
	}
	while (Entries.Contains(AutoName));
	RegisterUI(AutoName, InRoot, InSortOrder);
}

UDreamWidget* UDreamScreenUISubsystem::LoadPrefabToScreen(UDreamUIPrefab* InPrefab, int32 InSortOrder)
{
	if (!IsValid(InPrefab))
	{
		return nullptr;
	}
	UDreamWidget* Root = GetOrCreateScreenRoot();
	if (!Root)
	{
		return nullptr;
	}
	UDreamWidget* Page = InPrefab->LoadPrefab(GetWorld(), Root, nullptr, true);
	if (Page)
	{
		AddToViewport(Page, InSortOrder);
	}
	return Page;
}

void UDreamScreenUISubsystem::RegisterUI(FName InName, UDreamWidget* InRoot, int32 InSortOrder)
{
	const FName PreviousTop = GetTopUI();
	RegisterUIInternal(InName, InRoot, InSortOrder, EDreamUIScreenPageCachePolicy::DestroyOnPop, nullptr, true);
	RefreshStack(PreviousTop);
}

void UDreamScreenUISubsystem::RegisterUIInternal(
	FName InName,
	UDreamWidget* InRoot,
	int32 InSortOrder,
	EDreamUIScreenPageCachePolicy InCachePolicy,
	TSoftObjectPtr<UDreamUIPrefab> InSourcePrefab,
	bool bInitiallyVisible)
{
	if (InName.IsNone() || !IsUsablePage(InRoot) || InRoot == ScreenRoot)
	{
		return;
	}

	const FName PreviousName = FindNameForWidget(InRoot);
	if (!PreviousName.IsNone() && PreviousName != InName)
	{
		Stack.Remove(PreviousName);
		Entries.Remove(PreviousName);
		OnPageRemoved.Broadcast(PreviousName, InRoot);
	}

	if (FEntry* Existing = Entries.Find(InName))
	{
		if (Existing->Root.Get() == InRoot)
		{
			Existing->SortOrder = InSortOrder;
			Existing->CachePolicy = InCachePolicy;
			if (!InSourcePrefab.IsNull())
			{
				Existing->SourcePrefab = InSourcePrefab;
			}
			ConfigurePage(InRoot, InSortOrder);
			SetPageActive(InName, bInitiallyVisible);
			return;
		}
		Stack.Remove(InName);
		RemoveEntry(InName, true);
	}

	ConfigurePage(InRoot, InSortOrder);
	FEntry Entry;
	Entry.Root = InRoot;
	Entry.SourcePrefab = InSourcePrefab;
	Entry.SortOrder = InSortOrder;
	Entry.CachePolicy = InCachePolicy;
	Entry.State = EDreamUIScreenPageState::Inactive;
	Entries.Add(InName, MoveTemp(Entry));
	InRoot->SetVisibility(EDreamWidgetVisibility::Collapsed);
	OnPageCreated.Broadcast(InName, InRoot);
	SetPageActive(InName, bInitiallyVisible);
}

UDreamWidget* UDreamScreenUISubsystem::ShowPrefab(FName InName, UDreamUIPrefab* InPrefab, int32 InSortOrder)
{
	if (InName.IsNone() || !IsValid(InPrefab))
	{
		return nullptr;
	}
	UDreamWidget* Root = GetOrCreateScreenRoot();
	if (!Root)
	{
		return nullptr;
	}
	UDreamWidget* Page = InPrefab->LoadPrefab(GetWorld(), Root, nullptr, true);
	if (Page)
	{
		const FName PreviousTop = GetTopUI();
		RegisterUIInternal(InName, Page, InSortOrder, EDreamUIScreenPageCachePolicy::DestroyOnPop, InPrefab, true);
		RefreshStack(PreviousTop);
	}
	return Page;
}

UDreamWidget* UDreamScreenUISubsystem::GetUI(FName InName) const
{
	if (const FEntry* Entry = Entries.Find(InName))
	{
		UDreamWidget* Root = Entry->Root.Get();
		return IsUsablePage(Root) ? Root : nullptr;
	}
	return nullptr;
}

bool UDreamScreenUISubsystem::IsInViewport(UDreamWidget* InRoot) const
{
	return IsUsablePage(InRoot) && !FindNameForWidget(InRoot).IsNone();
}

bool UDreamScreenUISubsystem::IsUIShowing(FName InName) const
{
	const UDreamWidget* Root = GetUI(InName);
	if (!Root || !Root->GetWidgetActive())
	{
		return false;
	}
	const EDreamWidgetVisibility Visibility = Root->GetVisibility();
	return Visibility != EDreamWidgetVisibility::Hidden && Visibility != EDreamWidgetVisibility::Collapsed;
}

void UDreamScreenUISubsystem::SetUIVisible(FName InName, bool bVisible)
{
	SetPageActive(InName, bVisible);
}

void UDreamScreenUISubsystem::SetPageActive(FName InName, bool bActive)
{
	FEntry* Entry = Entries.Find(InName);
	UDreamWidget* Root = Entry ? Entry->Root.Get() : nullptr;
	if (!Entry || !IsUsablePage(Root))
	{
		return;
	}

	const bool bWasShowing = IsUIShowing(InName);
	const EDreamUIScreenPageState NewState = bActive ? EDreamUIScreenPageState::Active : EDreamUIScreenPageState::Inactive;
	Root->SetWidgetActive(true);
	Root->SetVisibility(bActive ? EDreamWidgetVisibility::Visible : EDreamWidgetVisibility::Collapsed);
	const bool bStateChanged = Entry->State != NewState || bWasShowing != bActive;
	Entry->State = NewState;
	if (bStateChanged)
	{
		if (bActive)
		{
			OnPageShown.Broadcast(InName, Root);
		}
		else
		{
			OnPageHidden.Broadcast(InName, Root);
		}
	}
}

void UDreamScreenUISubsystem::DestroyPage(UDreamWidget* InRoot)
{
	if (IsValid(InRoot) && InRoot != ScreenRoot && (InRoot->HasRegistered() || InRoot->HasBegunPlay()))
	{
		InRoot->DestroyWidget();
	}
}

void UDreamScreenUISubsystem::RemoveFromViewport(UDreamWidget* InRoot)
{
	const FName Name = FindNameForWidget(InRoot);
	if (!Name.IsNone())
	{
		RemoveUI(Name);
	}
}

void UDreamScreenUISubsystem::RemoveUI(FName InName)
{
	const FName PreviousTop = GetTopUI();
	Stack.Remove(InName);
	RemoveEntry(InName, true);
	RefreshStack(PreviousTop);
}

void UDreamScreenUISubsystem::RemoveEntry(FName InName, bool bDestroyPage)
{
	FEntry Entry;
	if (!Entries.RemoveAndCopyValue(InName, Entry))
	{
		return;
	}

	UDreamWidget* Root = Entry.Root.Get();
	const bool bWasShowing = IsUsablePage(Root)
		&& Root->GetWidgetActive()
		&& Root->GetVisibility() != EDreamWidgetVisibility::Hidden
		&& Root->GetVisibility() != EDreamWidgetVisibility::Collapsed;
	if (IsUsablePage(Root))
	{
		Root->SetWidgetActive(true);
		Root->SetVisibility(EDreamWidgetVisibility::Collapsed);
	}
	if (bWasShowing || Entry.State == EDreamUIScreenPageState::Active)
	{
		OnPageHidden.Broadcast(InName, Root);
	}
	OnPageRemoved.Broadcast(InName, Root);
	if (bDestroyPage)
	{
		DestroyPage(Root);
	}
}

void UDreamScreenUISubsystem::RemoveAllUI()
{
	TArray<FName> LoadingNames;
	PendingPageLoads.GenerateKeyArray(LoadingNames);
	for (FName Name : LoadingNames)
	{
		CancelPageLoad(Name);
	}

	TArray<FName> Names;
	Entries.GenerateKeyArray(Names);
	const FName PreviousTop = GetTopUI();
	Stack.Reset();
	for (FName Name : Names)
	{
		RemoveEntry(Name, true);
	}
	RefreshStack(PreviousTop);
}

TArray<FName> UDreamScreenUISubsystem::GetAllUINames() const
{
	TArray<FName> Names;
	for (const TPair<FName, FEntry>& Pair : Entries)
	{
		if (IsUsablePage(Pair.Value.Root.Get()))
		{
			Names.Add(Pair.Key);
		}
	}
	Names.Sort([](FName A, FName B) { return A.ToString() < B.ToString(); });
	return Names;
}

bool UDreamScreenUISubsystem::RegisterPageAsset(
	FName InName,
	TSoftObjectPtr<UDreamUIPrefab> InPrefab,
	EDreamUIScreenPageCachePolicy InCachePolicy)
{
	if (InName.IsNone() || InPrefab.IsNull())
	{
		return false;
	}

	const FSoftObjectPath NewPrefabPath = InPrefab.ToSoftObjectPath();
	bool bPrefabChanged = false;
	if (const FPageDefinition* ExistingDefinition = PageDefinitions.Find(InName))
	{
		bPrefabChanged = ExistingDefinition->Prefab.ToSoftObjectPath() != NewPrefabPath;
	}

	PageDefinitions.Add(InName, FPageDefinition{ InPrefab, InCachePolicy });
	if (bPrefabChanged)
	{
		CancelPageLoad(InName);
		const FPageDefinition* CurrentDefinition = PageDefinitions.Find(InName);
		if (!CurrentDefinition || CurrentDefinition->Prefab.ToSoftObjectPath() != NewPrefabPath)
		{
			return true;
		}
		if (const FEntry* ExistingEntry = Entries.Find(InName);
			ExistingEntry && !ExistingEntry->SourcePrefab.IsNull()
			&& ExistingEntry->SourcePrefab.ToSoftObjectPath() != NewPrefabPath)
		{
			RemoveUI(InName);
		}
	}
	if (FEntry* Entry = Entries.Find(InName);
		Entry && Entry->SourcePrefab.ToSoftObjectPath() == NewPrefabPath)
	{
		Entry->CachePolicy = InCachePolicy;
	}
	return true;
}

void UDreamScreenUISubsystem::UnregisterPageAsset(FName InName, bool bRemoveLoadedPage)
{
	PageDefinitions.Remove(InName);
	CancelPageLoad(InName);
	if (bRemoveLoadedPage)
	{
		RemoveUI(InName);
	}
}

TSoftObjectPtr<UDreamUIPrefab> UDreamScreenUISubsystem::GetPageAsset(FName InName) const
{
	if (const FPageDefinition* Definition = PageDefinitions.Find(InName))
	{
		return Definition->Prefab;
	}
	return {};
}

TArray<FName> UDreamScreenUISubsystem::GetRegisteredPageNames() const
{
	TArray<FName> Names;
	PageDefinitions.GenerateKeyArray(Names);
	Names.Sort([](FName A, FName B) { return A.ToString() < B.ToString(); });
	return Names;
}

void UDreamScreenUISubsystem::ExecuteLoadCallbacks(
	FName InName,
	FPendingPageLoad& InPendingLoad,
	UDreamWidget* InPage,
	bool bSuccess)

{
	for (const FDreamUIScreenPageAsyncCallback& Callback : InPendingLoad.Callbacks)
	{
		Callback.ExecuteIfBound(InName, InPage, bSuccess);
	}
}

void UDreamScreenUISubsystem::CompletePageLoad(FName InName)
{
	FPendingPageLoad PendingLoad;
	if (!PendingPageLoads.RemoveAndCopyValue(InName, PendingLoad))
	{
		return;
	}

	const FPageDefinition* Definition = PageDefinitions.Find(InName);
	UDreamUIPrefab* Prefab = Definition ? Definition->Prefab.Get() : nullptr;
	UDreamWidget* Page = Definition && IsValid(Prefab)
		? PushPrefab(InName, Prefab, Definition->CachePolicy, PendingLoad.bHidePrevious)
		: nullptr;
	ExecuteLoadCallbacks(InName, PendingLoad, Page, Page != nullptr);
}

void UDreamScreenUISubsystem::PushPageAsync(
	FName InName,
	const FDreamUIScreenPageAsyncCallback& OnComplete,
	bool bHidePrevious)
{
	const FPageDefinition* Definition = PageDefinitions.Find(InName);
	if (InName.IsNone() || !Definition || Definition->Prefab.IsNull())
	{
		OnComplete.ExecuteIfBound(InName, nullptr, false);
		return;
	}

	if (UDreamWidget* ExistingPage = GetUI(InName))
	{
		const FEntry* ExistingEntry = Entries.Find(InName);
		if (ExistingEntry
			&& !ExistingEntry->SourcePrefab.IsNull()
			&& ExistingEntry->SourcePrefab.ToSoftObjectPath() == Definition->Prefab.ToSoftObjectPath())
		{
			PushUI(InName, ExistingPage, Definition->CachePolicy, bHidePrevious);
			OnComplete.ExecuteIfBound(InName, ExistingPage, true);
			return;
		}
		RemoveUI(InName);
		Definition = PageDefinitions.Find(InName);
		if (!Definition || Definition->Prefab.IsNull())
		{
			OnComplete.ExecuteIfBound(InName, nullptr, false);
			return;
		}
	}

	if (FPendingPageLoad* ExistingLoad = PendingPageLoads.Find(InName))
	{
		ExistingLoad->bHidePrevious = bHidePrevious;
		if (OnComplete.IsBound())
		{
			ExistingLoad->Callbacks.Add(OnComplete);
		}
		return;
	}

	if (UDreamUIPrefab* LoadedPrefab = Definition->Prefab.Get())
	{
		UDreamWidget* Page = PushPrefab(InName, LoadedPrefab, Definition->CachePolicy, bHidePrevious);
		OnComplete.ExecuteIfBound(InName, Page, Page != nullptr);
		return;
	}

	FPendingPageLoad& PendingLoad = PendingPageLoads.Add(InName);
	PendingLoad.bHidePrevious = bHidePrevious;
	if (OnComplete.IsBound())
	{
		PendingLoad.Callbacks.Add(OnComplete);
	}

	const TWeakObjectPtr<UDreamScreenUISubsystem> WeakThis(this);
	PendingLoad.Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Definition->Prefab.ToSoftObjectPath(),
		[WeakThis, InName]()
		{
			if (UDreamScreenUISubsystem* This = WeakThis.Get())
			{
				This->CompletePageLoad(InName);
			}
		},
		FStreamableManager::DefaultAsyncLoadPriority,
		false,
		false,
		FString::Printf(TEXT("DreamUI Page %s"), *InName.ToString()));

	if (!PendingLoad.Handle.IsValid())
	{
		FPendingPageLoad FailedLoad;
		PendingPageLoads.RemoveAndCopyValue(InName, FailedLoad);
		ExecuteLoadCallbacks(InName, FailedLoad, nullptr, false);
	}
}

bool UDreamScreenUISubsystem::CancelPageLoad(FName InName)
{
	FPendingPageLoad PendingLoad;
	if (!PendingPageLoads.RemoveAndCopyValue(InName, PendingLoad))
	{
		return false;
	}
	if (PendingLoad.Handle.IsValid())
	{
		PendingLoad.Handle->CancelHandle();
	}
	ExecuteLoadCallbacks(InName, PendingLoad, nullptr, false);
	return true;
}

EDreamUIScreenPageState UDreamScreenUISubsystem::GetPageState(FName InName) const
{
	if (PendingPageLoads.Contains(InName))
	{
		return EDreamUIScreenPageState::Loading;
	}
	if (const FEntry* Entry = Entries.Find(InName); Entry && IsUsablePage(Entry->Root.Get()))
	{
		return IsUIShowing(InName) ? EDreamUIScreenPageState::Active : EDreamUIScreenPageState::Inactive;
	}
	return EDreamUIScreenPageState::Unloaded;
}

UDreamWidget* UDreamScreenUISubsystem::PushPrefab(
	FName InName,
	UDreamUIPrefab* InPrefab,
	EDreamUIScreenPageCachePolicy InCachePolicy,
	bool bHidePrevious)
{
	if (InName.IsNone() || !IsValid(InPrefab))
	{
		return nullptr;
	}

	if (FEntry* ExistingEntry = Entries.Find(InName))
	{
		const TSoftObjectPtr<UDreamUIPrefab> RequestedPrefab(InPrefab);
		if (!ExistingEntry->SourcePrefab.IsNull()
			&& ExistingEntry->SourcePrefab.ToSoftObjectPath() == RequestedPrefab.ToSoftObjectPath())
		{
			if (UDreamWidget* ExistingPage = GetUI(InName))
			{
				PushUI(InName, ExistingPage, InCachePolicy, bHidePrevious);
				return ExistingPage;
			}
		}
		RemoveUI(InName);
	}

	UDreamWidget* Root = GetOrCreateScreenRoot();
	if (!Root)
	{
		return nullptr;
	}
	UDreamWidget* Page = InPrefab->LoadPrefab(GetWorld(), Root, nullptr, true);
	if (!Page)
	{
		return nullptr;
	}

	const FName PreviousTop = GetTopUI();
	const int32 SortOrder = StackBaseSortOrder + Stack.Num() * StackSortOrderStep;
	RegisterUIInternal(InName, Page, SortOrder, InCachePolicy, InPrefab, false);
	Stack.Remove(InName);
	Stack.Add(InName);
	if (FEntry* Entry = Entries.Find(InName))
	{
		Entry->bHidePrevious = bHidePrevious;
	}
	RefreshStack(PreviousTop);
	return Page;
}

void UDreamScreenUISubsystem::PushUI(
	FName InName,
	UDreamWidget* InRoot,
	EDreamUIScreenPageCachePolicy InCachePolicy,
	bool bHidePrevious)
{
	if (InName.IsNone() || !IsUsablePage(InRoot))
	{
		return;
	}

	const FName PreviousTop = GetTopUI();
	if (GetUI(InName) != InRoot)
	{
		const int32 SortOrder = StackBaseSortOrder + Stack.Num() * StackSortOrderStep;
		RegisterUIInternal(InName, InRoot, SortOrder, InCachePolicy, nullptr, false);
	}
	FEntry* Entry = Entries.Find(InName);
	if (!Entry)
	{
		return;
	}
	Entry->CachePolicy = InCachePolicy;
	Entry->bHidePrevious = bHidePrevious;
	Stack.Remove(InName);
	Stack.Add(InName);
	RefreshStack(PreviousTop);
}

void UDreamScreenUISubsystem::RefreshStack(FName InPreviousTop)
{
	if (bRefreshingStack)
	{
		bStackRefreshRequested = true;
		return;
	}

	bRefreshingStack = true;
	do
	{
		bStackRefreshRequested = false;
		for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
		{
			if (!GetUI(Stack[Index]))
			{
				Stack.RemoveAt(Index);
			}
		}

		for (int32 Index = 0; Index < Stack.Num(); ++Index)
		{
			if (FEntry* Entry = Entries.Find(Stack[Index]))
			{
				Entry->SortOrder = StackBaseSortOrder + Index * StackSortOrderStep;
				ConfigurePage(Entry->Root.Get(), Entry->SortOrder);
			}
		}

		TArray<TPair<FName, bool>> VisibilityUpdates;
		VisibilityUpdates.Reserve(Stack.Num());
		bool bPreviousPagesCovered = false;
		for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
		{
			const FName PageName = Stack[Index];
			VisibilityUpdates.Emplace(PageName, !bPreviousPagesCovered);
			if (const FEntry* Entry = Entries.Find(PageName); Entry && Entry->bHidePrevious)
			{
				bPreviousPagesCovered = true;
			}
		}

		for (const TPair<FName, bool>& Update : VisibilityUpdates)
		{
			SetPageActive(Update.Key, Update.Value);
			if (bStackRefreshRequested)
			{
				break;
			}
		}
	}
	while (bStackRefreshRequested);
	bRefreshingStack = false;

	const FName NewTop = GetTopUI();
	if (InPreviousTop != NewTop)
	{
		OnStackChanged.Broadcast(InPreviousTop, NewTop);
	}
}

void UDreamScreenUISubsystem::PopUI()
{
	const FName PreviousTop = GetTopUI();
	while (!Stack.IsEmpty())
	{
		const FName Top = Stack.Pop();
		FEntry* Entry = Entries.Find(Top);
		if (!Entry || !IsUsablePage(Entry->Root.Get()))
		{
			continue;
		}

		const EDreamUIScreenPageCachePolicy CachePolicy = Entry->CachePolicy;
		if (CachePolicy == EDreamUIScreenPageCachePolicy::KeepAlive)
		{
			SetPageActive(Top, false);
		}
		else
		{
			RemoveEntry(Top, true);
		}
		break;
	}
	RefreshStack(PreviousTop);
}

bool UDreamScreenUISubsystem::PopToUI(FName InName)
{
	if (!Stack.Contains(InName) || !GetUI(InName))
	{
		return false;
	}
	while (!Stack.IsEmpty() && Stack.Last() != InName)
	{
		PopUI();
	}
	return !Stack.IsEmpty() && Stack.Last() == InName;
}

void UDreamScreenUISubsystem::ClearStack(bool bRemovePages)
{
	const FName PreviousTop = GetTopUI();
	const TArray<FName> PreviousStack = Stack;
	Stack.Reset();
	for (FName PageName : PreviousStack)
	{
		if (bRemovePages)
		{
			RemoveEntry(PageName, true);
		}
		else
		{
			SetPageActive(PageName, false);
		}
	}
	RefreshStack(PreviousTop);
}

FName UDreamScreenUISubsystem::GetTopUI() const
{
	for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
	{
		if (GetUI(Stack[Index]))
		{
			return Stack[Index];
		}
	}
	return NAME_None;
}
