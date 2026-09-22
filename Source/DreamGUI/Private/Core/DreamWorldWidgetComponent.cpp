// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamWorldWidgetComponent.h"

#include "DreamGUI.h"
#include "DreamUIBPLibrary.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UDreamWorldWidgetComponent::BeginPlay()
{
	Super::BeginPlay();

	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		return;
	}
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	if (Manager == nullptr)
	{
		return;
	}

	// World-space UI takes input through one raycaster per local player, and a level is not required
	// to carry that -- or an event system -- for it to work: the first host to begin play asks for
	// whatever is missing, and the call is idempotent so every other host asks for nothing. Only the
	// players who exist NOW; one who joins later is the same gap split screen has always had, and is
	// filled by placing an event system with that UserIndex.
	//
	// The index is the player's position in the game instance's list, which is what UserIndex means
	// throughout DreamGUI -- UDreamScreenUISubsystem::ResolvePlayerIndex resolves to the same number.
	const UGameInstance* GameInstance = World->GetGameInstance();
	const int32 LocalPlayerCount = GameInstance != nullptr ? GameInstance->GetLocalPlayers().Num() : 0;
	if (LocalPlayerCount == 0)
	{
		// No local player yet: a world built in code, or one whose players arrive after this. Player 0
		// is the answer the screen subsystem gives when no controller is named, so the objects created
		// under that index are the ones the first player to arrive will find.
		Manager->EnsureInteractionForPlayer(0, EDreamInteractionKind::World);
		return;
	}
	for (int32 PlayerIndex = 0; PlayerIndex < LocalPlayerCount; ++PlayerIndex)
	{
		Manager->EnsureInteractionForPlayer(PlayerIndex, EDreamInteractionKind::World);
	}
}

void UDreamWorldWidgetComponent::LoadWidget()
{
	// A rebuild: nothing below reuses any part of what is standing, so it goes first.
	DestroyLoadedWidget();

#if WITH_EDITOR
	if (this->GetName().Contains(TEXT("SKEL_")) || this->GetName().Contains(TEXT("TRASH_")))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Skip loading the widget for %s because it's a temp object for blueprint compiling!"), *this->GetName());
		return;
	}
#endif

	if (!IsValid(WidgetClass))
	{
		return;
	}
	// IsValid, not just non-null: OnRegister runs during level transitions too, and CreateDreamWidget
	// answers null for a world that is on its way out.
	UWorld* World = GetWorld();
	if (!IsValid(World))
	{
		return;
	}

	LoadedWidget = CreateDreamWidget(World, WidgetClass, nullptr, [this](UDreamUserWidget* RootWidget)
	{
		// Before the hierarchy comes alive, which is what this hook is for: a behaviour that wakes up
		// first may read the render mode off the canvas and cache what it found.
		//
		// The hierarchy's OWN canvas is kept. Replacing it with one carried by this component was how
		// a world-space root used to be built, and it meant everything the designer set on the canvas
		// -- render mode, sort order, the scaler -- stopped counting the moment the class was placed
		// in a level.
		UDreamCanvas* Canvas = RootWidget->GetComponent<UDreamCanvas>();
		if (Canvas == nullptr)
		{
			// Ordinary rather than wrong: a hierarchy authored for the screen borrows the screen
			// root's canvas and carries none of its own. Hosting it in the world means it needs one.
			UE_LOG(DreamGUI, Verbose, TEXT("[%s].%d %s carries no DreamCanvas, so one is added for it to be drawn in the world."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetNameSafe(WidgetClass));
			Canvas = RootWidget->AddComponent<UDreamCanvas>();
		}
		RootCanvas = Canvas;
		ApplyCanvasSettings();
	});

	// CreateDreamWidget answers null for an invalid world or an unusable class, and everything below
	// dereferences the result.
	if (!LoadedWidget.IsValid())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s failed to create a widget of class %s."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), *GetNameSafe(WidgetClass));
		return;
	}
	if (!RootCanvas.IsValid())
	{
		// Without a canvas the tree renders nothing and the attach below refuses it, so say so once
		// rather than leave an invisible hierarchy with no explanation.
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d %s could not give %s a root canvas; the hierarchy will not render."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathName(), *GetNameSafe(WidgetClass));
	}

	ApplyGeometryToLoadedWidget();
	// The one way a hierarchy becomes a world-space root: it places the tree on this component, binds
	// it to follow, and takes it out of the parked state. Doing any part of that by hand here is how
	// the creation paths drifted into meaning different things.
	UDreamUIBPLibrary::AttachWidgetToSceneComponent(LoadedWidget.Get(), this);
	ApplyWidgetOverridesToLoadedWidget();
	NotifyWidgetLoaded();

#if WITH_EDITOR
	if (World->WorldType == EWorldType::Editor)
	{
		// Transient in the editor world so the level does not save a hierarchy that is rebuilt from
		// the class on load anyway. Not in EditorPreview, where the designer needs the tree fully
		// transactional, and not in a game world, where nothing is saved.
		TArray<UDreamWidget*> AllLoadedWidgets;
		UDreamWidget::CollectChildrenWidgets(LoadedWidget.Get(), AllLoadedWidgets, true);
		for (UDreamWidget* Widget : AllLoadedWidgets)
		{
			Widget->SetFlags(RF_Transient);
		}
	}
#endif
}

bool UDreamWorldWidgetComponent::IsLoadedWidgetCurrent() const
{
	const UDreamWidget* Widget = LoadedWidget.Get();
	return IsValid(Widget) && Widget->GetClass() == WidgetClass.Get();
}

#if WITH_EDITOR
void UDreamWorldWidgetComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// The details panel has already written the field, so each branch runs what the matching setter
	// would have done AFTER assigning -- the value is in place and only the consequence is missing.
	const FName PropertyName = PropertyChangedEvent.MemberProperty != nullptr
		? PropertyChangedEvent.GetMemberPropertyName()
		: NAME_None;
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamWorldWidgetComponent, WidgetClass))
	{
		RefreshDrawSizeFromClass();
		// Super reregisters the component for most edits, and the register path rebuilds a tree whose
		// class no longer matches -- with the size this function had not refreshed yet. Rebuilding a
		// second time would only throw that tree away, so the size is applied to it instead.
		if (IsLoadedWidgetCurrent())
		{
			ApplyGeometryToLoadedWidget();
		}
		else
		{
			LoadWidget();
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamWorldWidgetComponent, Backend)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamWorldWidgetComponent, SortOrder)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamWorldWidgetComponent, TraceChannel))
	{
		ApplyCanvasSettings();
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamWorldWidgetComponent, bUseDesignSize)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamWorldWidgetComponent, DrawSize)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(UDreamWorldWidgetComponent, Pivot))
	{
		RefreshDrawSizeFromClass();
		ApplyGeometryToLoadedWidget();
	}
}
#endif

void UDreamWorldWidgetComponent::SetWidgetClass(TSubclassOf<UDreamUserWidget> InClass)
{
	if (WidgetClass == InClass)
	{
		return;
	}
	WidgetClass = InClass;
	// Before the load, so the tree is built at the new class's size rather than resized afterwards.
	RefreshDrawSizeFromClass();
	LoadWidget();
}

void UDreamWorldWidgetComponent::SetBackend(EDreamWorldWidgetBackend InBackend)
{
	if (Backend == InBackend)
	{
		return;
	}
	Backend = InBackend;
	ApplyCanvasSettings();
}

void UDreamWorldWidgetComponent::SetDrawSize(FVector2D InSize)
{
	// An explicit size and "take the class's" are two readings of the same field, so naming a size
	// turns the other off. Otherwise the value would survive until the next load and then vanish.
	bUseDesignSize = false;
	DrawSize = InSize;
	ApplyGeometryToLoadedWidget();
}

FVector2D UDreamWorldWidgetComponent::GetDrawSize() const
{
	if (bUseDesignSize)
	{
		// Asked of the class rather than read from the cached field: the class may have been
		// recompiled at a different canvas size since the field was last written.
		const FIntPoint DesignSize = UDreamWidgetGeneratedClass::FindDesignSize(WidgetClass);
		return FVector2D(DesignSize.X, DesignSize.Y);
	}
	return DrawSize;
}

void UDreamWorldWidgetComponent::SetUseDesignSize(bool bInUse)
{
	if (bUseDesignSize == bInUse)
	{
		return;
	}
	bUseDesignSize = bInUse;
	// Turning it back on republishes the class's size into the field, so the details panel shows the
	// size that is actually in force. Turning it off keeps whatever is showing as the starting point.
	RefreshDrawSizeFromClass();
	ApplyGeometryToLoadedWidget();
}

void UDreamWorldWidgetComponent::SetPivot(FVector2D InPivot)
{
	if (Pivot.Equals(InPivot))
	{
		return;
	}
	Pivot = InPivot;
	ApplyGeometryToLoadedWidget();
}

void UDreamWorldWidgetComponent::SetSortOrder(int32 InSortOrder)
{
	if (SortOrder == InSortOrder)
	{
		return;
	}
	SortOrder = InSortOrder;
	ApplyCanvasSettings();
}

void UDreamWorldWidgetComponent::SetTraceChannel(TEnumAsByte<ETraceTypeQuery> InChannel)
{
	if (TraceChannel == InChannel)
	{
		return;
	}
	TraceChannel = InChannel;
	ApplyCanvasSettings();
}

void UDreamWorldWidgetComponent::ApplyCanvasSettings()
{
	UDreamCanvas* Canvas = RootCanvas.Get();
	if (!IsValid(Canvas))
	{
		return;
	}
	Canvas->SetRenderMode(Backend == EDreamWorldWidgetBackend::UERenderer
		? EDreamRenderMode::WorldSpace
		: EDreamRenderMode::WorldSpace_DreamUI);
	Canvas->SetSortOrder(SortOrder);
	Canvas->SetTraceChannel(TraceChannel);
}

void UDreamWorldWidgetComponent::ApplyGeometryToLoadedWidget()
{
	UDreamWidget* Root = LoadedWidget.Get();
	if (!IsValid(Root))
	{
		return;
	}
	// Written as one anchor struct rather than through the individual setters: those refuse to touch
	// a widget that has no parent, and a world-space root has none -- the scene component it hangs
	// from is not a widget.
	//
	// The anchors are points, not spans, because there is nothing to stretch against. A stretched
	// axis resolves its size from the parent's span across the anchors; with no parent it falls back
	// to the size delta while still reporting itself stretched. Point anchors make the size delta the
	// size outright, which is what DrawSize means.
	FDreamUIAnchorData Anchors = Root->GetAnchorData();
	Anchors.AnchorMin = FVector2D(0.5, 0.5);
	Anchors.AnchorMax = FVector2D(0.5, 0.5);
	Anchors.AnchoredPosition = FVector2D::ZeroVector;
	Anchors.Pivot = Pivot;
	Anchors.SizeDelta = GetDrawSize();
	Root->SetAnchorData(Anchors);
}

void UDreamWorldWidgetComponent::RefreshDrawSizeFromClass()
{
	if (!bUseDesignSize)
	{
		return;
	}
	const FIntPoint DesignSize = UDreamWidgetGeneratedClass::FindDesignSize(WidgetClass);
	DrawSize = FVector2D(DesignSize.X, DesignSize.Y);
}
