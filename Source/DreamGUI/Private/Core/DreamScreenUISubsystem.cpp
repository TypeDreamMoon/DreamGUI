// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUserWidget.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIManager.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "DreamGUI.h"

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

	for (TPair<int32, TObjectPtr<UDreamWidget>>& RootPair : ScreenRoots)
	{
		if (OwnedScreenRoots.Contains(RootPair.Key) && IsUsablePage(RootPair.Value))
		{
			RootPair.Value->DestroyWidget();
		}
	}
	ScreenRoots.Reset();
	OwnedScreenRoots.Reset();

	Super::Deinitialize();
}

bool UDreamScreenUISubsystem::IsUsablePage(const UDreamWidget* InRoot) const
{
	return IsValid(InRoot) && InRoot->HasRegistered() && InRoot->GetWorld() == GetWorld();
}

int32 UDreamScreenUISubsystem::ResolvePlayerIndex(const APlayerController* InOwningPlayer) const
{
	if (IsValid(InOwningPlayer))
	{
		return UDreamUserWidget::GetLocalPlayerIndexOf(InOwningPlayer);
	}
	// No owner named: the first local player, which is the whole answer in a single-player game and
	// the reason none of the existing call sites had to grow a parameter.
	const UWorld* World = GetWorld();
	return World != nullptr ? UDreamUserWidget::GetLocalPlayerIndexOf(World->GetFirstPlayerController()) : 0;
}

int32 UDreamScreenUISubsystem::PlayerIndexForWidget(const UDreamWidget* InRoot) const
{
	// Two ways for a widget to know its player, tried in the order that makes an overlay land where
	// the thing it is about already is.
	for (const UDreamWidget* Walker = InRoot; Walker != nullptr; Walker = Walker->GetParent())
	{
		// 1. Which screen it is already sitting on. Structural, so it is right even for a plain widget
		//    with no owner of its own.
		if (const int32* RootIndex = ScreenRoots.FindKey(TObjectPtr<UDreamWidget>(const_cast<UDreamWidget*>(Walker))))
		{
			return *RootIndex;
		}
		// 2. The nearest user widget that has an owner.
		if (const UDreamUserWidget* UserWidget = Cast<const UDreamUserWidget>(Walker))
		{
			if (IsValid(UserWidget->GetOwningPlayer()))
			{
				return UserWidget->GetOwningPlayerIndex();
			}
		}
	}
	// Neither: the first local player, which is the whole answer in a single-player game.
	return ResolvePlayerIndex(nullptr);
}

int32 UDreamScreenUISubsystem::PlayerIndexForPage(FName InName) const
{
	const FEntry* Entry = Entries.Find(InName);
	return Entry != nullptr ? Entry->PlayerIndex : ResolvePlayerIndex(nullptr);
}

TArray<FName> UDreamScreenUISubsystem::StackFor(int32 InPlayerIndex) const
{
	TArray<FName> PlayerStack;
	PlayerStack.Reserve(Stack.Num());
	for (FName PageName : Stack)
	{
		if (const FEntry* Entry = Entries.Find(PageName); Entry != nullptr && Entry->PlayerIndex == InPlayerIndex)
		{
			PlayerStack.Add(PageName);
		}
	}
	return PlayerStack;
}

TArray<int32> UDreamScreenUISubsystem::GetScreenPlayerIndices() const
{
	TArray<int32> Indices;
	ScreenRoots.GetKeys(Indices);
	Indices.Sort();
	return Indices;
}

UDreamCanvas* UDreamScreenUISubsystem::GetScreenCanvas(APlayerController* InOwningPlayer) const
{
	UDreamWidget* Root = GetScreenRoot(InOwningPlayer);
	return IsUsablePage(Root) ? Root->GetComponent<UDreamCanvas>() : nullptr;
}

UDreamWidget* UDreamScreenUISubsystem::GetScreenRoot(APlayerController* InOwningPlayer) const
{
	const TObjectPtr<UDreamWidget>* Found = ScreenRoots.Find(ResolvePlayerIndex(InOwningPlayer));
	return Found != nullptr ? Found->Get() : nullptr;
}

UDreamWidget* UDreamScreenUISubsystem::GetOrCreateScreenRoot(APlayerController* InOwningPlayer)
{
	return GetOrCreateScreenRootForIndex(ResolvePlayerIndex(InOwningPlayer));
}

UDreamWidget* UDreamScreenUISubsystem::GetOrCreateScreenRootForWidget(UDreamWidget* InContextWidget)
{
	return GetOrCreateScreenRootForIndex(PlayerIndexForWidget(InContextWidget));
}

UDreamWidget* UDreamScreenUISubsystem::GetOrCreateScreenRootForUserIndex(int32 InUserIndex)
{
	return GetOrCreateScreenRootForIndex(FMath::Max(0, InUserIndex));
}

UDreamWidget* UDreamScreenUISubsystem::GetOrCreateScreenRootForIndex(int32 InPlayerIndex)
{
	TObjectPtr<UDreamWidget>& RootSlot = ScreenRoots.FindOrAdd(InPlayerIndex);
	if (IsUsablePage(RootSlot))
	{
		if (UDreamCanvas* ExistingCanvas = RootSlot->GetComponent<UDreamCanvas>())
		{
			if (ExistingCanvas->IsRootCanvas() && ExistingCanvas->GetActualRenderMode() == EDreamRenderMode::ScreenSpaceOverlay)
			{
				EnsureInteractionObjects(ExistingCanvas, InPlayerIndex);
				return RootSlot;
			}
		}
	}

	RootSlot = nullptr;
	OwnedScreenRoots.Remove(InPlayerIndex);
	// An overlay canvas somebody else placed is adopted, but only by the FIRST player -- an authored
	// canvas says nothing about which local player it belongs to, and handing the same one to two
	// players would put both their pages in one place.
	if (InPlayerIndex == ResolvePlayerIndex(nullptr))
	{
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			for (const TWeakObjectPtr<UDreamCanvas>& CanvasPtr : Manager->GetAllCanvasArray())
			{
				UDreamCanvas* Canvas = CanvasPtr.Get();
				if (IsValid(Canvas) && Canvas->IsRootCanvas() && Canvas->GetActualRenderMode() == EDreamRenderMode::ScreenSpaceOverlay
					&& !ScreenRoots.FindKey(TObjectPtr<UDreamWidget>(Canvas->GetWidget())))
				{
					RootSlot = Canvas->GetWidget();
					EnsureInteractionObjects(Canvas, InPlayerIndex);
					return RootSlot;
				}
			}
		}
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const FName RootName = MakeUniqueObjectName(World, UDreamWidget::StaticClass(),
		*FString::Printf(TEXT("DreamScreenRoot_P%d"), InPlayerIndex));
	UDreamWidget* NewRoot = NewObject<UDreamWidget>(World, RootName, RF_Transient);
	NewRoot->SetDisplayName(FString::Printf(TEXT("[DreamScreenRoot P%d]"), InPlayerIndex));
	NewRoot->SetSizeDelta(FVector2D(1920.0, 1080.0));
	NewRoot->OnRegister();

	UDreamCanvas* Canvas = NewRoot->AddComponent<UDreamCanvas>();
	if (!Canvas)
	{
		NewRoot->DestroyWidget();
		ScreenRoots.Remove(InPlayerIndex);
		return nullptr;
	}
	Canvas->SetRenderMode(EDreamRenderMode::ScreenSpaceOverlay);
	ScreenRoots.Add(InPlayerIndex, NewRoot);
	OwnedScreenRoots.Add(InPlayerIndex);

	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World); Manager && Manager->HasBegunPlay())
	{
		NewRoot->BeginPlay();
	}
	NewRoot->CalculateObjectToWorldTransform(true);
	EnsureInteractionObjects(Canvas, InPlayerIndex);
	return NewRoot;
}

void UDreamScreenUISubsystem::EnsureInteractionObjects(UDreamCanvas* InRootCanvas, int32 InPlayerIndex)
{
	if (!IsValid(InRootCanvas))
	{
		return;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld());
	if (Manager == nullptr)
	{
		return;
	}
	// Creating the event system and the raycaster is the manager's job, because a world-space host
	// needs exactly the same pair and neither of them is anything to do with a screen. What is left
	// here is the part that IS: telling this player's screen raycaster which canvas it projects
	// through, which the manager has no way to know.
	Manager->EnsureInteractionForPlayer(InPlayerIndex, EDreamInteractionKind::Screen);

	for (const TWeakObjectPtr<UDreamBaseRaycaster>& Raycaster : Manager->GetAllRaycasterArray())
	{
		UDreamScreenSpaceRaycaster* ScreenRaycaster = Cast<UDreamScreenSpaceRaycaster>(Raycaster.Get());
		// Only a raycaster that speaks for THIS player. A second player's raycaster carries its own
		// UserIndex and must keep pointing at its own canvas; retargeting every screen raycaster at
		// whichever root was built last is what made split screen impossible.
		if (ScreenRaycaster != nullptr && ScreenRaycaster->GetUserIndex() == InPlayerIndex)
		{
			ScreenRaycaster->SetRootCanvas(InRootCanvas);
		}
	}
	// The one the manager has just created is on its host actor and has not necessarily enrolled --
	// enrolment happens on activation, which a world that has not begun play never performs -- so it
	// would otherwise be left without a canvas until the first frame of play.
	if (const AActor* Host = Manager->GetInteractionHost(InPlayerIndex))
	{
		for (UActorComponent* Component : Host->GetComponents())
		{
			if (UDreamScreenSpaceRaycaster* ScreenRaycaster = Cast<UDreamScreenSpaceRaycaster>(Component);
				ScreenRaycaster != nullptr && ScreenRaycaster->GetUserIndex() == InPlayerIndex)
			{
				ScreenRaycaster->SetRootCanvas(InRootCanvas);
			}
		}
	}
}

void UDreamScreenUISubsystem::ConfigurePage(UDreamWidget* InRoot, int32 InSortOrder, int32 InPlayerIndex, bool bInCustomPlacement)
{
	UDreamWidget* Root = GetOrCreateScreenRootForIndex(InPlayerIndex);
	if (!IsUsablePage(InRoot) || !IsUsablePage(Root) || InRoot == Root)
	{
		return;
	}
	if (InRoot->GetParent() != Root)
	{
		// Attaching is what un-parks a widget that was created but never added.
		InRoot->SetParent(Root, false);
	}

	// Full-bleed, unless the caller placed this page by hand. Forcing it unconditionally is what made
	// SetPositionInViewport impossible to keep: the next refresh moved the window back to edge-to-edge.
	if (!bInCustomPlacement)
	{
		InRoot->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
		InRoot->SetAnchoredPosition(FVector2D::ZeroVector);
		InRoot->SetSizeDelta(FVector2D::ZeroVector);
	}
	// Placement only. The page's ACTIVE state belongs to SetPageActive, which owns both axes for
	// every page -- switching it on here as well made the stack's own "this page is covered" pass
	// toggle a covered page off and straight back on again on every refresh.

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
	if (!IsUsablePage(InRoot) || ScreenRoots.FindKey(TObjectPtr<UDreamWidget>(InRoot)) != nullptr)
	{
		return;
	}
	const FName ExistingName = FindNameForWidget(InRoot);
	if (!ExistingName.IsNone())
	{
		WarnIfSortOrderReserved(ExistingName, InSortOrder);
		FEntry& Existing = Entries[ExistingName];
		// A widget whose owner changed since it was added moves to that player's screen.
		Existing.PlayerIndex = PlayerIndexForWidget(InRoot);
		ConfigurePage(InRoot, InSortOrder, Existing.PlayerIndex, Existing.bCustomPlacement);
		Existing.SortOrder = InSortOrder;
		// AddToViewport means "show this", on both axes. Re-adding a page that had been hidden used
		// to switch only bWidgetActive back on, which left it running and still invisible.
		SetPageActive(ExistingName, true);
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

void UDreamScreenUISubsystem::AddToPlayerScreen(UDreamWidget* InRoot, APlayerController* InOwningPlayer, int32 InSortOrder)
{
	// Name the owner on the WIDGET rather than carrying it as a parameter from here on: everything
	// downstream -- the screen it is parented to, the event system that can focus it, the input it
	// hears -- asks the widget, so this one line is what makes the rest of the split-screen story work
	// without a player argument on every call.
	if (UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(InRoot))
	{
		UserWidget->SetOwningPlayer(InOwningPlayer);
	}
	else if (IsValid(InRoot) && IsValid(InOwningPlayer))
	{
		// A plain widget has nowhere to remember an owner, so it can only be placed, not owned. Placing
		// it is still the useful half; say the other half is missing rather than silently using player 0.
		UE_LOG(DreamGUI, Warning,
			TEXT("AddToPlayerScreen on '%s', which is not a DreamUI User Widget: it will be placed on that player's screen ")
			TEXT("but cannot remember the owner, so anything it creates later belongs to the first local player."),
			*InRoot->GetPathDisplayName());
	}
	AddToViewport(InRoot, InSortOrder);
	if (!IsValid(InRoot) || Cast<UDreamUserWidget>(InRoot) != nullptr)
	{
		return;
	}
	// The plain-widget case: place it on the named player's screen even though the widget cannot say so.
	if (const FName Name = FindNameForWidget(InRoot); !Name.IsNone())
	{
		FEntry& Entry = Entries[Name];
		Entry.PlayerIndex = ResolvePlayerIndex(InOwningPlayer);
		ConfigurePage(InRoot, Entry.SortOrder, Entry.PlayerIndex, Entry.bCustomPlacement);
	}
}

UDreamWidget* UDreamScreenUISubsystem::CreateWidgetOnScreen(TSubclassOf<UDreamUserWidget> InWidgetClass, int32 InSortOrder,
	APlayerController* InOwningPlayer)
{
	if (!IsValid(InWidgetClass))
	{
		return nullptr;
	}
	UDreamWidget* Root = GetOrCreateScreenRoot(InOwningPlayer);
	if (!Root)
	{
		return nullptr;
	}
	UDreamWidget* Page = CreateDreamWidget(GetWorld(), InWidgetClass, Root);
	if (Page)
	{
		// Owner first, so the widget answers for itself from here on.
		if (UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(Page); UserWidget != nullptr && IsValid(InOwningPlayer))
		{
			UserWidget->SetOwningPlayer(InOwningPlayer);
		}
		AddToViewport(Page, InSortOrder);
	}
	return Page;
}

void UDreamScreenUISubsystem::RegisterUI(FName InName, UDreamWidget* InRoot, int32 InSortOrder)
{
	WarnIfSortOrderReserved(InName, InSortOrder);
	const int32 PlayerIndex = PlayerIndexForWidget(InRoot);
	const FName PreviousTop = GetTopUIForIndex(PlayerIndex);
	RegisterUIInternal(InName, InRoot, InSortOrder, EDreamUIScreenPageCachePolicy::DestroyOnPop, nullptr, true, PlayerIndex);
	RefreshStack(PlayerIndex, PreviousTop);
}

void UDreamScreenUISubsystem::WarnIfSortOrderReserved(FName InName, int32 InSortOrder) const
{
	// Sort orders from StackBaseSortOrder up belong to the page stack, which hands its own pages
	// StackBaseSortOrder + depth * StackSortOrderStep and rewrites them on every refresh. A
	// free-standing page asking for a number in that band draws in an undefined order against
	// whatever the stack has at the same value, and loses it outright the next time the stack
	// refreshes. Say so, rather than leaving "my HUD is sometimes behind the pause menu" to be
	// guessed at from the outside.
	if (InSortOrder >= StackBaseSortOrder)
	{
		UE_LOG(DreamGUI, Warning,
			TEXT("Screen page '%s' asked for sort order %d, which is inside the band the page stack reserves (%d and up). ")
			TEXT("Use a sort order below %d for a page that is not pushed on the stack."),
			*InName.ToString(), InSortOrder, StackBaseSortOrder, StackBaseSortOrder);
	}
}

void UDreamScreenUISubsystem::RegisterUIInternal(
	FName InName,
	UDreamWidget* InRoot,
	int32 InSortOrder,
	EDreamUIScreenPageCachePolicy InCachePolicy,
	TSoftClassPtr<UDreamUserWidget> InSourceClass,
	bool bInitiallyVisible,
	int32 InPlayerIndex)
{
	if (InName.IsNone() || !IsUsablePage(InRoot) || ScreenRoots.FindKey(TObjectPtr<UDreamWidget>(InRoot)) != nullptr)
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
			Existing->PlayerIndex = InPlayerIndex;
			if (!InSourceClass.IsNull())
			{
				Existing->SourceClass = InSourceClass;
			}
			ConfigurePage(InRoot, InSortOrder, InPlayerIndex, Existing->bCustomPlacement);
			SetPageActive(InName, bInitiallyVisible);
			return;
		}
		Stack.Remove(InName);
		RemoveEntry(InName);
	}

	// A page being registered for the first time is full-bleed until somebody places it by hand.
	ConfigurePage(InRoot, InSortOrder, InPlayerIndex, false);
	FEntry Entry;
	Entry.Root = InRoot;
	Entry.SourceClass = InSourceClass;
	Entry.SortOrder = InSortOrder;
	Entry.CachePolicy = InCachePolicy;
	Entry.State = EDreamUIScreenPageState::Inactive;
	Entry.PlayerIndex = InPlayerIndex;
	Entries.Add(InName, MoveTemp(Entry));
	InRoot->SetVisibility(EDreamWidgetVisibility::Collapsed);
	OnPageCreated.Broadcast(InName, InRoot);
	if (bInitiallyVisible)
	{
		SetPageActive(InName, true);
	}
	// A page registered hidden is on its way into the stack, which decides its real state a moment
	// later. The entry already starts Inactive and the root is already collapsed above, so there is
	// nothing left to apply -- and going through SetPageActive(false) here would switch a page that
	// has never been shown off and straight back on, with the OnDisable/OnEnable pair to match.
}

UDreamWidget* UDreamScreenUISubsystem::ShowWidgetOfClass(FName InName, TSubclassOf<UDreamUserWidget> InWidgetClass, int32 InSortOrder,
	APlayerController* InOwningPlayer)
{
	if (InName.IsNone() || !IsValid(InWidgetClass))
	{
		return nullptr;
	}
	const int32 PlayerIndex = ResolvePlayerIndex(InOwningPlayer);
	UDreamWidget* Root = GetOrCreateScreenRootForIndex(PlayerIndex);
	if (!Root)
	{
		return nullptr;
	}
	WarnIfSortOrderReserved(InName, InSortOrder);
	UDreamWidget* Page = CreateDreamWidget(GetWorld(), InWidgetClass, Root);
	if (Page)
	{
		if (UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(Page); UserWidget != nullptr && IsValid(InOwningPlayer))
		{
			UserWidget->SetOwningPlayer(InOwningPlayer);
		}
		const FName PreviousTop = GetTopUIForIndex(PlayerIndex);
		RegisterUIInternal(InName, Page, InSortOrder, EDreamUIScreenPageCachePolicy::DestroyOnPop, TSoftClassPtr<UDreamUserWidget>(InWidgetClass), true, PlayerIndex);
		RefreshStack(PlayerIndex, PreviousTop);
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
	// Both axes, because they carry different halves of "this page is not on screen".
	// bWidgetActive drives behaviour lifecycle -- OnEnable/OnDisable, the manager's tick list, and
	// with it OnTick and the polled property bindings. Visibility drives rendering, layout and hit
	// testing. Collapsing alone left a page the stack had covered ticking and evaluating bindings
	// for as long as it stayed covered, which is not what anyone pushing a full-screen page over
	// another means, and not what UMG does with the page it removed from the viewport.
	Root->SetWidgetActive(bActive);
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
	const bool bIsAScreenRoot = IsValid(InRoot) && ScreenRoots.FindKey(TObjectPtr<UDreamWidget>(InRoot)) != nullptr;
	if (IsValid(InRoot) && !bIsAScreenRoot && (InRoot->HasRegistered() || InRoot->HasBegunPlay()))
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

bool UDreamScreenUISubsystem::SetPageHasCustomPlacement(UDreamWidget* InRoot, bool bInCustomPlacement)
{
	const FName Name = FindNameForWidget(InRoot);
	FEntry* Entry = Name.IsNone() ? nullptr : Entries.Find(Name);
	if (Entry == nullptr)
	{
		return false;
	}
	Entry->bCustomPlacement = bInCustomPlacement;
	if (!bInCustomPlacement)
	{
		// Back under the stack's geometry immediately rather than at the next push, so "clear my
		// placement" reads as an instruction and not a preference.
		ConfigurePage(InRoot, Entry->SortOrder, Entry->PlayerIndex, false);
	}
	return true;
}

bool UDreamScreenUISubsystem::GetPageHasCustomPlacement(UDreamWidget* InRoot) const
{
	const FName Name = FindNameForWidget(InRoot);
	const FEntry* Entry = Name.IsNone() ? nullptr : Entries.Find(Name);
	return Entry != nullptr && Entry->bCustomPlacement;
}

bool UDreamScreenUISubsystem::ForgetPage(UDreamWidget* InRoot)
{
	const FName Name = FindNameForWidget(InRoot);
	if (Name.IsNone())
	{
		return false;
	}
	const int32 PlayerIndex = PlayerIndexForPage(Name);
	const FName PreviousTop = GetTopUIForIndex(PlayerIndex);
	FEntry Entry;
	if (!Entries.RemoveAndCopyValue(Name, Entry))
	{
		return false;
	}
	Stack.Remove(Name);
	if (Entry.State == EDreamUIScreenPageState::Active)
	{
		OnPageHidden.Broadcast(Name, InRoot);
	}
	OnPageRemoved.Broadcast(Name, InRoot);
	RefreshStack(PlayerIndex, PreviousTop);
	return true;
}

void UDreamScreenUISubsystem::RemoveUI(FName InName)
{
	const int32 PlayerIndex = PlayerIndexForPage(InName);
	const FName PreviousTop = GetTopUIForIndex(PlayerIndex);
	Stack.Remove(InName);
	RemoveEntry(InName);
	RefreshStack(PlayerIndex, PreviousTop);
}

int32 UDreamScreenUISubsystem::PruneDeadEntries()
{
	// A page destroyed by someone other than this subsystem -- DestroyWidget on the page itself, or
	// the whole tree going down with its owner -- used to leave its name in Entries forever: GetUI
	// answers null for it, RefreshStack only ever prunes Stack, and nothing touched the map. The
	// name set then grew without bound and re-registering that name took the "already exists" road
	// to remove a page that was already gone.
	TArray<FName> DeadNames;
	for (const TPair<FName, FEntry>& Pair : Entries)
	{
		if (!IsUsablePage(Pair.Value.Root.Get()))
		{
			DeadNames.Add(Pair.Key);
		}
	}
	for (FName DeadName : DeadNames)
	{
		FEntry Dead;
		if (!Entries.RemoveAndCopyValue(DeadName, Dead))
		{
			continue;
		}
		Stack.Remove(DeadName);
		// Nothing to hide and nothing to destroy: the page is already gone. Listeners still have to
		// hear that the name no longer names anything.
		OnPageRemoved.Broadcast(DeadName, Dead.Root.Get());
	}
	return DeadNames.Num();
}

void UDreamScreenUISubsystem::RemoveEntry(FName InName)
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
		// Off on both axes. This page is leaving the screen; switching it ON here was a line copied
		// from SetPageActive back when that one did the same, and it restarted a page's behaviours
		// for the length of the broadcasts below.
		Root->SetWidgetActive(false);
		Root->SetVisibility(EDreamWidgetVisibility::Collapsed);
	}
	if (bWasShowing || Entry.State == EDreamUIScreenPageState::Active)
	{
		OnPageHidden.Broadcast(InName, Root);
	}
	OnPageRemoved.Broadcast(InName, Root);
	DestroyPage(Root);
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
	// Every player's stack empties, so every player's stack gets told. Collected before the removals,
	// because the entries that name the players are about to go.
	TSet<int32> AffectedPlayers;
	TMap<int32, FName> PreviousTops;
	for (FName Name : Names)
	{
		const int32 PlayerIndex = PlayerIndexForPage(Name);
		if (!AffectedPlayers.Contains(PlayerIndex))
		{
			AffectedPlayers.Add(PlayerIndex);
			PreviousTops.Add(PlayerIndex, GetTopUIForIndex(PlayerIndex));
		}
	}
	Stack.Reset();
	for (FName Name : Names)
	{
		RemoveEntry(Name);
	}
	if (AffectedPlayers.Num() == 0)
	{
		AffectedPlayers.Add(ResolvePlayerIndex(nullptr));
		PreviousTops.Add(ResolvePlayerIndex(nullptr), NAME_None);
	}
	for (int32 PlayerIndex : AffectedPlayers)
	{
		RefreshStack(PlayerIndex, PreviousTops[PlayerIndex]);
	}
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

bool UDreamScreenUISubsystem::RegisterPageClass(
	FName InName,
	TSoftClassPtr<UDreamUserWidget> InWidgetClass,
	EDreamUIScreenPageCachePolicy InCachePolicy)
{
	if (InName.IsNone() || InWidgetClass.IsNull())
	{
		return false;
	}

	const FSoftObjectPath NewClassPath = InWidgetClass.ToSoftObjectPath();
	bool bPageClassChanged = false;
	if (const FPageDefinition* ExistingDefinition = PageDefinitions.Find(InName))
	{
		bPageClassChanged = ExistingDefinition->PageClass.ToSoftObjectPath() != NewClassPath;
	}

	PageDefinitions.Add(InName, FPageDefinition{ InWidgetClass, InCachePolicy });
	if (bPageClassChanged)
	{
		CancelPageLoad(InName);
		const FPageDefinition* CurrentDefinition = PageDefinitions.Find(InName);
		if (!CurrentDefinition || CurrentDefinition->PageClass.ToSoftObjectPath() != NewClassPath)
		{
			return true;
		}
		if (const FEntry* ExistingEntry = Entries.Find(InName);
			ExistingEntry && !ExistingEntry->SourceClass.IsNull()
			&& ExistingEntry->SourceClass.ToSoftObjectPath() != NewClassPath)
		{
			RemoveUI(InName);
		}
	}
	if (FEntry* Entry = Entries.Find(InName);
		Entry && Entry->SourceClass.ToSoftObjectPath() == NewClassPath)
	{
		Entry->CachePolicy = InCachePolicy;
	}
	return true;
}

void UDreamScreenUISubsystem::UnregisterPageClass(FName InName, bool bRemoveLoadedPage)
{
	PageDefinitions.Remove(InName);
	CancelPageLoad(InName);
	if (bRemoveLoadedPage)
	{
		RemoveUI(InName);
	}
}

TSoftClassPtr<UDreamUserWidget> UDreamScreenUISubsystem::GetPageClass(FName InName) const
{
	if (const FPageDefinition* Definition = PageDefinitions.Find(InName))
	{
		return Definition->PageClass;
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
	UClass* PageClass = Definition ? Definition->PageClass.Get() : nullptr;
	UDreamWidget* Page = Definition && IsValid(PageClass)
		? PushWidgetOfClass(InName, PageClass, Definition->CachePolicy, PendingLoad.bHidePrevious)
		: nullptr;
	ExecuteLoadCallbacks(InName, PendingLoad, Page, Page != nullptr);
}

void UDreamScreenUISubsystem::PushPageAsync(
	FName InName,
	const FDreamUIScreenPageAsyncCallback& OnComplete,
	bool bHidePrevious)
{
	const FPageDefinition* Definition = PageDefinitions.Find(InName);
	if (InName.IsNone() || !Definition || Definition->PageClass.IsNull())
	{
		OnComplete.ExecuteIfBound(InName, nullptr, false);
		return;
	}

	if (UDreamWidget* ExistingPage = GetUI(InName))
	{
		const FEntry* ExistingEntry = Entries.Find(InName);
		if (ExistingEntry
			&& !ExistingEntry->SourceClass.IsNull()
			&& ExistingEntry->SourceClass.ToSoftObjectPath() == Definition->PageClass.ToSoftObjectPath())
		{
			PushUI(InName, ExistingPage, Definition->CachePolicy, bHidePrevious);
			OnComplete.ExecuteIfBound(InName, ExistingPage, true);
			return;
		}
		RemoveUI(InName);
		Definition = PageDefinitions.Find(InName);
		if (!Definition || Definition->PageClass.IsNull())
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

	if (UClass* AlreadyLoadedClass = Definition->PageClass.Get())
	{
		UDreamWidget* Page = PushWidgetOfClass(InName, AlreadyLoadedClass, Definition->CachePolicy, bHidePrevious);
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
		Definition->PageClass.ToSoftObjectPath(),
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

UDreamWidget* UDreamScreenUISubsystem::PushWidgetOfClass(
	FName InName,
	TSubclassOf<UDreamUserWidget> InWidgetClass,
	EDreamUIScreenPageCachePolicy InCachePolicy,
	bool bHidePrevious,
	APlayerController* InOwningPlayer)
{
	if (InName.IsNone() || !IsValid(InWidgetClass))
	{
		return nullptr;
	}

	if (FEntry* ExistingEntry = Entries.Find(InName))
	{
		const TSoftClassPtr<UDreamUserWidget> RequestedClass(InWidgetClass);
		if (!ExistingEntry->SourceClass.IsNull()
			&& ExistingEntry->SourceClass.ToSoftObjectPath() == RequestedClass.ToSoftObjectPath())
		{
			if (UDreamWidget* ExistingPage = GetUI(InName))
			{
				PushUI(InName, ExistingPage, InCachePolicy, bHidePrevious);
				return ExistingPage;
			}
		}
		RemoveUI(InName);
	}

	const int32 PlayerIndex = ResolvePlayerIndex(InOwningPlayer);
	UDreamWidget* Root = GetOrCreateScreenRootForIndex(PlayerIndex);
	if (!Root)
	{
		return nullptr;
	}
	UDreamWidget* Page = CreateDreamWidget(GetWorld(), InWidgetClass, Root);
	if (!Page)
	{
		return nullptr;
	}
	if (UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(Page); UserWidget != nullptr && IsValid(InOwningPlayer))
	{
		UserWidget->SetOwningPlayer(InOwningPlayer);
	}

	const FName PreviousTop = GetTopUIForIndex(PlayerIndex);
	// Depth counted within THIS player's stack, so two players' pages do not climb over each other's
	// sort orders as they push.
	const int32 SortOrder = StackBaseSortOrder + StackFor(PlayerIndex).Num() * StackSortOrderStep;
	RegisterUIInternal(InName, Page, SortOrder, InCachePolicy, TSoftClassPtr<UDreamUserWidget>(InWidgetClass), false, PlayerIndex);
	Stack.Remove(InName);
	Stack.Add(InName);
	if (FEntry* Entry = Entries.Find(InName))
	{
		Entry->bHidePrevious = bHidePrevious;
	}
	RefreshStack(PlayerIndex, PreviousTop);
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

	// The player the widget already belongs to. Pushing never moves a page to another player's screen;
	// AddToPlayerScreen is the verb for that.
	const int32 PlayerIndex = PlayerIndexForWidget(InRoot);
	const FName PreviousTop = GetTopUIForIndex(PlayerIndex);
	if (GetUI(InName) != InRoot)
	{
		const int32 SortOrder = StackBaseSortOrder + StackFor(PlayerIndex).Num() * StackSortOrderStep;
		RegisterUIInternal(InName, InRoot, SortOrder, InCachePolicy, nullptr, false, PlayerIndex);
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
	RefreshStack(PlayerIndex, PreviousTop);
}

void UDreamScreenUISubsystem::RefreshStack(int32 InPlayerIndex, FName InPreviousTop)
{
	if (bRefreshingStack)
	{
		StackRefreshRequests.Add(InPlayerIndex);
		return;
	}

	bRefreshingStack = true;
	do
	{
		StackRefreshRequests.Remove(InPlayerIndex);
		// Names whose page died elsewhere leave the map here, not just the stack. Inside the guard,
		// because OnPageRemoved listeners are free to push or pop.
		PruneDeadEntries();
		for (int32 Index = Stack.Num() - 1; Index >= 0; --Index)
		{
			if (!GetUI(Stack[Index]))
			{
				Stack.RemoveAt(Index);
			}
		}

		// This player's slice of the stack. Sort orders and covering are decided WITHIN a player:
		// another local player's full-screen page is on another screen and covers nothing here.
		const TArray<FName> PlayerStack = StackFor(InPlayerIndex);
		for (int32 Index = 0; Index < PlayerStack.Num(); ++Index)
		{
			if (FEntry* Entry = Entries.Find(PlayerStack[Index]))
			{
				Entry->SortOrder = StackBaseSortOrder + Index * StackSortOrderStep;
				ConfigurePage(Entry->Root.Get(), Entry->SortOrder, Entry->PlayerIndex, Entry->bCustomPlacement);
			}
		}

		TArray<TPair<FName, bool>> VisibilityUpdates;
		VisibilityUpdates.Reserve(PlayerStack.Num());
		bool bPreviousPagesCovered = false;
		for (int32 Index = PlayerStack.Num() - 1; Index >= 0; --Index)
		{
			const FName PageName = PlayerStack[Index];
			VisibilityUpdates.Emplace(PageName, !bPreviousPagesCovered);
			if (const FEntry* Entry = Entries.Find(PageName); Entry && Entry->bHidePrevious)
			{
				bPreviousPagesCovered = true;
			}
		}

		for (const TPair<FName, bool>& Update : VisibilityUpdates)
		{
			SetPageActive(Update.Key, Update.Value);
			if (StackRefreshRequests.Contains(InPlayerIndex))
			{
				break;
			}
		}
	}
	while (StackRefreshRequests.Contains(InPlayerIndex));
	bRefreshingStack = false;

	const FName NewTop = GetTopUIForIndex(InPlayerIndex);
	if (InPreviousTop != NewTop)
	{
		OnStackChanged.Broadcast(InPreviousTop, NewTop);
	}

	// Another player's stack asked to refresh while this one was running. Drain them now, outside the
	// guard, rather than leaving that player's pages at whatever state they were mid-change.
	while (StackRefreshRequests.Num() > 0)
	{
		const int32 OtherIndex = *StackRefreshRequests.CreateConstIterator();
		StackRefreshRequests.Remove(OtherIndex);
		RefreshStack(OtherIndex, GetTopUIForIndex(OtherIndex));
	}
}

void UDreamScreenUISubsystem::PopUI(APlayerController* InOwningPlayer)
{
	const int32 PlayerIndex = ResolvePlayerIndex(InOwningPlayer);
	const FName PreviousTop = GetTopUIForIndex(PlayerIndex);
	TArray<FName> PlayerStack = StackFor(PlayerIndex);
	while (!PlayerStack.IsEmpty())
	{
		const FName Top = PlayerStack.Pop();
		Stack.Remove(Top);
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
			RemoveEntry(Top);
		}
		break;
	}
	RefreshStack(PlayerIndex, PreviousTop);
}

bool UDreamScreenUISubsystem::PopToUI(FName InName)
{
	if (!Stack.Contains(InName) || !GetUI(InName))
	{
		return false;
	}
	// Pops only the pages above it ON ITS OWN PLAYER'S STACK.
	const int32 PlayerIndex = PlayerIndexForPage(InName);
	APlayerController* OwningPlayer = nullptr;
	if (const FEntry* Entry = Entries.Find(InName))
	{
		if (const UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(Entry->Root.Get()))
		{
			OwningPlayer = UserWidget->GetOwningPlayer();
		}
	}
	TArray<FName> PlayerStack = StackFor(PlayerIndex);
	int32 SafetyCount = PlayerStack.Num();
	while (!PlayerStack.IsEmpty() && PlayerStack.Last() != InName && SafetyCount-- > 0)
	{
		PopUI(OwningPlayer);
		PlayerStack = StackFor(PlayerIndex);
	}
	return !PlayerStack.IsEmpty() && PlayerStack.Last() == InName;
}

void UDreamScreenUISubsystem::ClearStack(bool bRemovePages, APlayerController* InOwningPlayer)
{
	const int32 PlayerIndex = ResolvePlayerIndex(InOwningPlayer);
	const FName PreviousTop = GetTopUIForIndex(PlayerIndex);
	const TArray<FName> PreviousStack = StackFor(PlayerIndex);
	for (FName PageName : PreviousStack)
	{
		Stack.Remove(PageName);
	}
	for (FName PageName : PreviousStack)
	{
		if (bRemovePages)
		{
			RemoveEntry(PageName);
		}
		else
		{
			SetPageActive(PageName, false);
		}
	}
	RefreshStack(PlayerIndex, PreviousTop);
}

FName UDreamScreenUISubsystem::GetTopUIForIndex(int32 InPlayerIndex) const
{
	const TArray<FName> PlayerStack = StackFor(InPlayerIndex);
	for (int32 Index = PlayerStack.Num() - 1; Index >= 0; --Index)
	{
		if (GetUI(PlayerStack[Index]))
		{
			return PlayerStack[Index];
		}
	}
	return NAME_None;
}

FName UDreamScreenUISubsystem::GetTopUI(APlayerController* InOwningPlayer) const
{
	return GetTopUIForIndex(ResolvePlayerIndex(InOwningPlayer));
}

int32 UDreamScreenUISubsystem::GetStackDepth(APlayerController* InOwningPlayer) const
{
	return StackFor(ResolvePlayerIndex(InOwningPlayer)).Num();
}

TArray<FName> UDreamScreenUISubsystem::GetUIStack(APlayerController* InOwningPlayer) const
{
	return StackFor(ResolvePlayerIndex(InOwningPlayer));
}
