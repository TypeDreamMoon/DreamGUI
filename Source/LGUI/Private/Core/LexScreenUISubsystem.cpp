// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Core/LexScreenUISubsystem.h"

#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Event/LexEventSystem.h"
#include "Event/LexScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "LGUI.h"
#include "PrefabSystem/LexUIPrefab.h"

ULexScreenUISubsystem* ULexScreenUISubsystem::Get(UWorld* InWorld)
{
	return InWorld ? InWorld->GetSubsystem<ULexScreenUISubsystem>() : nullptr;
}

ULexScreenUISubsystem* ULexScreenUISubsystem::GetLexScreenUISubsystem(UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}
	return Get(GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull));
}

bool ULexScreenUISubsystem::DoesSupportWorldType(EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::GamePreview;
}

void ULexScreenUISubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<ULexUIManagerWorldSubsystem>();
}

void ULexScreenUISubsystem::Deinitialize()
{
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

bool ULexScreenUISubsystem::IsUsablePage(const ULexWidget* InRoot) const
{
	return IsValid(InRoot) && InRoot->HasRegistered() && InRoot->GetWorld() == GetWorld();
}

ULexCanvas* ULexScreenUISubsystem::GetScreenCanvas() const
{
	return IsUsablePage(ScreenRoot) ? ScreenRoot->GetComponent<ULexCanvas>() : nullptr;
}

ULexWidget* ULexScreenUISubsystem::GetOrCreateScreenRoot()
{
	if (ULexCanvas* ExistingCanvas = GetScreenCanvas())
	{
		if (ExistingCanvas->IsRootCanvas() && ExistingCanvas->GetActualRenderMode() == ELexRenderMode::ScreenSpaceOverlay)
		{
			EnsureInteractionObjects(ExistingCanvas);
			return ScreenRoot;
		}
	}

	ScreenRoot = nullptr;
	bOwnsScreenRoot = false;
	if (ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		for (const TWeakObjectPtr<ULexCanvas>& CanvasPtr : Manager->GetAllCanvasArray())
		{
			ULexCanvas* Canvas = CanvasPtr.Get();
			if (IsValid(Canvas) && Canvas->IsRootCanvas() && Canvas->GetActualRenderMode() == ELexRenderMode::ScreenSpaceOverlay)
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

	const FName RootName = MakeUniqueObjectName(World, ULexWidget::StaticClass(), TEXT("LexScreenRoot"));
	ScreenRoot = NewObject<ULexWidget>(World, RootName, RF_Transient);
	ScreenRoot->SetDisplayName(TEXT("[LexScreenRoot]"));
	ScreenRoot->SetSizeDelta(FVector2D(1920.0, 1080.0));
	ScreenRoot->OnRegister();

	ULexCanvas* Canvas = ScreenRoot->AddComponent<ULexCanvas>();
	if (!Canvas)
	{
		ScreenRoot->DestroyWidget();
		ScreenRoot = nullptr;
		return nullptr;
	}
	Canvas->SetRenderMode(ELexRenderMode::ScreenSpaceOverlay);
	bOwnsScreenRoot = true;

	if (ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(World); Manager && Manager->HasBegunPlay())
	{
		ScreenRoot->BeginPlay();
	}
	ScreenRoot->CalculateObjectToWorldTransform(true);
	EnsureInteractionObjects(Canvas);
	return ScreenRoot;
}

void ULexScreenUISubsystem::EnsureInteractionObjects(ULexCanvas* InRootCanvas)
{
	if (!IsValid(InRootCanvas))
	{
		return;
	}

	ULexUIManagerWorldSubsystem* Manager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld());
	bool bHasScreenRaycaster = false;
	if (Manager)
	{
		for (const TWeakObjectPtr<ULexBaseRaycaster>& Raycaster : Manager->GetAllRaycasterArray())
		{
			if (ULexScreenSpaceRaycaster* ScreenRaycaster = Cast<ULexScreenSpaceRaycaster>(Raycaster.Get()))
			{
				ScreenRaycaster->SetRootCanvas(InRootCanvas);
				bHasScreenRaycaster = true;
			}
		}
	}

	if (!bHasScreenRaycaster && !IsValid(InteractionHost))
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = MakeUniqueObjectName(GetWorld(), AActor::StaticClass(), TEXT("LexScreenInteractionHost"));
		SpawnParameters.ObjectFlags |= RF_Transient;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		InteractionHost = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
		if (InteractionHost)
		{
			InteractionHost->SetActorEnableCollision(false);
			ULexScreenSpaceRaycaster* Raycaster = NewObject<ULexScreenSpaceRaycaster>(InteractionHost, NAME_None, RF_Transient);
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
			if (ActorIt->FindComponentByClass<ULexEventSystem>())
			{
				bHasEventSystem = true;
				break;
			}
		}
	}
	if (!bHasEventSystem && !IsValid(CreatedEventSystemActor))
	{
		static const TCHAR* EventSystemPath = TEXT("/LGUI/Blueprints/LexEventSystemActor_EnhancedInput.LexEventSystemActor_EnhancedInput_C");
		if (UClass* EventSystemClass = LoadClass<AActor>(nullptr, EventSystemPath))
		{
			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Name = MakeUniqueObjectName(GetWorld(), EventSystemClass, TEXT("LexEventSystem"));
			SpawnParameters.ObjectFlags |= RF_Transient;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			CreatedEventSystemActor = GetWorld()->SpawnActor<AActor>(EventSystemClass, FTransform::Identity, SpawnParameters);
		}
		else
		{
			UE_LOG(LGUI, Error, TEXT("Cannot create screen UI input: missing %s"), EventSystemPath);
		}
	}
}

void ULexScreenUISubsystem::ConfigurePage(ULexWidget* InRoot, int32 InSortOrder)
{
	ULexWidget* Root = GetOrCreateScreenRoot();
	if (!IsUsablePage(InRoot) || !IsUsablePage(Root) || InRoot == Root)
	{
		return;
	}
	if (InRoot->GetParent() != Root)
	{
		InRoot->SetParent(Root, false);
	}

	InRoot->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	InRoot->SetAnchoredPosition(FVector2D::ZeroVector);
	InRoot->SetSizeDelta(FVector2D::ZeroVector);
	InRoot->SetWidgetActive(true);
	InRoot->SetVisibility(ELexWidgetVisibility::Visible);

	ULexCanvas* PageCanvas = InRoot->GetComponent<ULexCanvas>();
	if (!PageCanvas)
	{
		PageCanvas = InRoot->AddComponent<ULexCanvas>();
	}
	if (PageCanvas)
	{
		PageCanvas->SetOverrideSorting(true);
		PageCanvas->SetSortOrder(FMath::Clamp(InSortOrder, static_cast<int32>(MIN_int16), static_cast<int32>(MAX_int16)), true);
	}
}

FName ULexScreenUISubsystem::FindNameForWidget(const ULexWidget* InRoot) const
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

void ULexScreenUISubsystem::AddToViewport(ULexWidget* InRoot, int32 InSortOrder)
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

ULexWidget* ULexScreenUISubsystem::LoadPrefabToScreen(ULexUIPrefab* InPrefab, int32 InSortOrder)
{
	if (!IsValid(InPrefab))
	{
		return nullptr;
	}
	ULexWidget* Root = GetOrCreateScreenRoot();
	if (!Root)
	{
		return nullptr;
	}
	ULexWidget* Page = InPrefab->LoadPrefab(GetWorld(), Root, nullptr, true);
	if (Page)
	{
		AddToViewport(Page, InSortOrder);
	}
	return Page;
}

void ULexScreenUISubsystem::RegisterUI(FName InName, ULexWidget* InRoot, int32 InSortOrder)
{
	if (InName.IsNone() || !IsUsablePage(InRoot) || InRoot == ScreenRoot)
	{
		return;
	}

	const FName PreviousName = FindNameForWidget(InRoot);
	if (!PreviousName.IsNone() && PreviousName != InName)
	{
		Entries.Remove(PreviousName);
		Stack.Remove(PreviousName);
	}
	if (const FEntry* Existing = Entries.Find(InName))
	{
		if (ULexWidget* ExistingRoot = Existing->Root.Get(); ExistingRoot && ExistingRoot != InRoot)
		{
			DestroyPage(ExistingRoot);
		}
	}

	ConfigurePage(InRoot, InSortOrder);
	Entries.Add(InName, FEntry{ InRoot, InSortOrder });
}

ULexWidget* ULexScreenUISubsystem::ShowPrefab(FName InName, ULexUIPrefab* InPrefab, int32 InSortOrder)
{
	if (InName.IsNone() || !IsValid(InPrefab))
	{
		return nullptr;
	}
	ULexWidget* Root = GetOrCreateScreenRoot();
	if (!Root)
	{
		return nullptr;
	}
	ULexWidget* Page = InPrefab->LoadPrefab(GetWorld(), Root, nullptr, true);
	if (Page)
	{
		RegisterUI(InName, Page, InSortOrder);
	}
	return Page;
}

ULexWidget* ULexScreenUISubsystem::GetUI(FName InName) const
{
	if (const FEntry* Entry = Entries.Find(InName))
	{
		ULexWidget* Root = Entry->Root.Get();
		return IsUsablePage(Root) ? Root : nullptr;
	}
	return nullptr;
}

bool ULexScreenUISubsystem::IsInViewport(ULexWidget* InRoot) const
{
	return IsUsablePage(InRoot) && !FindNameForWidget(InRoot).IsNone();
}

bool ULexScreenUISubsystem::IsUIShowing(FName InName) const
{
	const ULexWidget* Root = GetUI(InName);
	if (!Root || !Root->GetWidgetActive())
	{
		return false;
	}
	const ELexWidgetVisibility Visibility = Root->GetVisibility();
	return Visibility != ELexWidgetVisibility::Hidden && Visibility != ELexWidgetVisibility::Collapsed;
}

void ULexScreenUISubsystem::SetUIVisible(FName InName, bool bVisible)
{
	if (ULexWidget* Root = GetUI(InName))
	{
		Root->SetVisibility(bVisible ? ELexWidgetVisibility::Visible : ELexWidgetVisibility::Collapsed);
	}
}

void ULexScreenUISubsystem::DestroyPage(ULexWidget* InRoot)
{
	if (IsValid(InRoot) && InRoot != ScreenRoot && (InRoot->HasRegistered() || InRoot->HasBegunPlay()))
	{
		InRoot->DestroyWidget();
	}
}

void ULexScreenUISubsystem::RemoveFromViewport(ULexWidget* InRoot)
{
	const FName Name = FindNameForWidget(InRoot);
	if (!Name.IsNone())
	{
		RemoveUI(Name);
	}
}

void ULexScreenUISubsystem::RemoveUI(FName InName)
{
	FEntry Entry;
	if (Entries.RemoveAndCopyValue(InName, Entry))
	{
		DestroyPage(Entry.Root.Get());
	}
	Stack.Remove(InName);
}

void ULexScreenUISubsystem::RemoveAllUI()
{
	TArray<TWeakObjectPtr<ULexWidget>> Roots;
	Roots.Reserve(Entries.Num());
	for (const TPair<FName, FEntry>& Pair : Entries)
	{
		Roots.AddUnique(Pair.Value.Root);
	}
	Entries.Reset();
	Stack.Reset();
	for (const TWeakObjectPtr<ULexWidget>& Root : Roots)
	{
		DestroyPage(Root.Get());
	}
}

TArray<FName> ULexScreenUISubsystem::GetAllUINames() const
{
	TArray<FName> Names;
	for (const TPair<FName, FEntry>& Pair : Entries)
	{
		if (IsUsablePage(Pair.Value.Root.Get()))
		{
			Names.Add(Pair.Key);
		}
	}
	return Names;
}

ULexWidget* ULexScreenUISubsystem::PushPrefab(FName InName, ULexUIPrefab* InPrefab)
{
	const int32 SortOrder = StackBaseSortOrder + Stack.Num() * StackSortOrderStep;
	ULexWidget* Page = ShowPrefab(InName, InPrefab, SortOrder);
	if (Page)
	{
		Stack.Remove(InName);
		Stack.Add(InName);
	}
	return Page;
}

void ULexScreenUISubsystem::PushUI(FName InName, ULexWidget* InRoot)
{
	if (InName.IsNone() || !IsUsablePage(InRoot))
	{
		return;
	}
	const int32 SortOrder = StackBaseSortOrder + Stack.Num() * StackSortOrderStep;
	RegisterUI(InName, InRoot, SortOrder);
	Stack.Remove(InName);
	Stack.Add(InName);
}

void ULexScreenUISubsystem::PopUI()
{
	while (!Stack.IsEmpty())
	{
		const FName Top = Stack.Pop();
		if (Entries.Contains(Top))
		{
			RemoveUI(Top);
			return;
		}
	}
}

FName ULexScreenUISubsystem::GetTopUI() const
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
