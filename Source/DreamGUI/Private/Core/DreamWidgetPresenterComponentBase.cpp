// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamWidgetTree.h"

#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UINavigationInputSelectionHandler.h"

#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieScene.h"
#include "MovieSceneObjectBindingID.h"
#include "EngineUtils.h"
#define LOCTEXT_NAMESPACE "DreamWidgetPresenterComponentBase"

UDreamWidgetPresenterComponentBase::UDreamWidgetPresenterComponentBase()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// The configured class is deliberately NOT loaded here. This constructor runs for the CDO while
	// the engine is still bringing modules up, and the setting now names a Blueprint whose class
	// (UDreamWidgetBlueprint) lives in the editor module -- too early to exist, so the load failed and
	// left every instance null. It used to name a prefab asset from this module, which could load that
	// early. GetNavigationSelection() resolves it on first use instead; the property stays an override.
}

void UDreamWidgetPresenterComponentBase::BeginPlay()
{
	Super::BeginPlay();
	if (DreamUI::IsGameWorld(this))
	{
		LoadWidget();//load when BeginPlay in game mode
	}
}

void UDreamWidgetPresenterComponentBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// The tree is this component's to let go: a world-space health bar otherwise outlived every enemy
	// that died -- still registered, still ticking, still drawing. EndPlay and not OnUnregister, because
	// a component unregisters for reasons that are not a teardown (a reregister context, for one) and
	// throwing the tree away there would take it out from under a live actor.
	//
	// Skipped while garbage collection is already destroying this component -- UActorComponent's
	// BeginDestroy routes here too, and the tree goes with the component then. A whole-world shutdown
	// is safe as it stands: UWorld::EndPlay routes every actor before it reaches the subsystems, so the
	// world's teardown finds this tree already gone, and DestroyWidget tolerates a second call regardless.
	if (!HasAnyFlags(RF_BeginDestroyed))
	{
		DestroyLoadedWidget();
	}
	LoadedWidget = nullptr;
	RootCanvas = nullptr;
}

bool UDreamWidgetPresenterComponentBase::IsInAWorldThatRunsTrees() const
{
	const UWorld* World = GetWorld();
	return World != nullptr && World->WorldType != EWorldType::Inactive && World->WorldType != EWorldType::None;
}

void UDreamWidgetPresenterComponentBase::OnRegister()
{
	Super::OnRegister();
	UWorld* World = GetWorld();
	// An Inactive (or typeless) world is a package being preloaded -- double-clicking a map asset
	// loads it before the map command runs. A tree built there answers to no manager and is reaped
	// by the next GC through the BeginDestroy fallback, mid-purge; building it is pure liability.
	if (!IsInAWorldThatRunsTrees())
	{
		return;
	}
	// Known to the world's manager as a host, so the world's teardown and a level's removal can ask for
	// the tree back (IDreamWidgetTreeHost).
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World))
	{
		Manager->RegisterTreeHost(this);
	}
	if (!World->IsGameWorld())
	{
		// Edit mode loads here because there is no BeginPlay to load from; the preview has to exist as
		// soon as the component does. But a register is not always a first one: editing any property of
		// the owning actor unregisters and registers every component on it, and rebuilding the whole
		// hierarchy each time is what made moving a host feel like reopening the asset. So a host that
		// can see its tree is still the right one keeps it and merely re-seats it on this component --
		// the canvas holds the host pointer, and a reregister is exactly when that pointer needs
		// renewing.
		if (LoadedWidget.IsValid() && IsLoadedWidgetCurrent())
		{
			if (UDreamCanvas* Canvas = RootCanvas.Get())
			{
				Canvas->AttachToSceneComponent(this);
			}
		}
		else
		{
			LoadWidget();
		}
	}
}

void UDreamWidgetPresenterComponentBase::OnUnregister()
{
	// Does not simply destroy the tree. A component unregisters for reasons that are not a teardown --
	// a reregister context is one, and the editor opens one for any property edit on the owning actor
	// -- so destroying here meant every edit rebuilt the hierarchy and dropped whatever it was holding.
	// Most teardowns that ARE teardowns have their own hooks: EndPlay in a game world, and
	// OnComponentDestroyed / BeginDestroy below.
	Super::OnUnregister();

	// An edit-mode tree has no EndPlay, and an unregister on its own does not say which of three things
	// is happening:
	//  - a reregister (a property edit, an undo that keeps the component): it registers again before
	//    the frame is out and the tree must survive it, which is the reason for not destroying above;
	//  - a destruction -- the component deleted or its actor destroyed, or an undo taking either away,
	//    which leaves it garbage rather than destroyed: the tree goes now;
	//  - a level leaving the world -- a sublevel hidden, World Partition unloading a cell: the world's
	//    manager hears of it (LevelRemovedFromWorld) and asks every host in that level for its tree.
	// Anything that comes back later (a sublevel shown again, a redo) goes through OnRegister, which
	// builds a fresh tree.
	const UWorld* World = GetWorld();
	if (LoadedWidget.IsValid() && (World == nullptr || !World->IsGameWorld()))
	{
		const AActor* Owner = GetOwner();
		if (IsBeingDestroyed() || !IsValid(this) || (Owner != nullptr && (Owner->IsActorBeingDestroyed() || !IsValid(Owner))))
		{
			ReleaseTree(EDreamTreeReleaseReason::HostDestroyed);
		}
	}
}

#if WITH_EDITOR
void UDreamWidgetPresenterComponentBase::PostEditUndo()
{
	Super::PostEditUndo();
	// A transaction that takes this component away -- redoing its actor's deletion, undoing the actor's
	// placement -- unregisters it BEFORE it marks it garbage: PreEditUndo opens a reregister context, and
	// OnUnregister, seeing a live component then, kept the tree for the register that would follow. None
	// follows for garbage, so the tree goes now. Undo bringing the component back registers it, and that
	// builds a fresh one.
	if (!IsValid(this))
	{
		ReleaseTree(EDreamTreeReleaseReason::HostDestroyed);
		if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			Manager->UnregisterTreeHost(this);
		}
	}
}
#endif

void UDreamWidgetPresenterComponentBase::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// The component is going away for good -- deleted from an actor, or its actor destroyed -- which
	// OnUnregister no longer stands in for.
	ReleaseTree(EDreamTreeReleaseReason::HostDestroyed);
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		Manager->UnregisterTreeHost(this);
	}
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UDreamWidgetPresenterComponentBase::BeginDestroy()
{
	// Nothing is taken down here: this runs inside a collection, and the tree, outered to this
	// component, is collected with it. Every way a host goes -- EndPlay, destruction, an undo, a level
	// leaving, the world's teardown -- let the tree go before now; a tree still held here is said so.
	UE_CLOG(OwnedTree != nullptr, DreamGUI, Warning,
		TEXT("%s was collected with its widget tree still loaded; nothing let it go."), *GetPathName());
	LoadedWidget = nullptr;
	RootCanvas = nullptr;
	OwnedTree = nullptr;
	Super::BeginDestroy();
}

void UDreamWidgetPresenterComponentBase::ReleaseTree(EDreamTreeReleaseReason InReason)
{
	DestroyLoadedWidget();
}

void UDreamWidgetPresenterComponentBase::RebuildTree()
{
	if (IsRegistered() && !IsBeingDestroyed() && IsValid(this))
	{
		LoadWidget();
	}
}

void UDreamWidgetPresenterComponentBase::DestroyLoadedWidget()
{
	if (UDreamWidget* Widget = LoadedWidget.Get(); IsValid(Widget))
	{
		Widget->DestroyWidget();
	}
	// Cleared whether or not there was anything to tear down, so the second caller of a pair -- and
	// they do come in pairs, a destroyed component is also collected -- finds nothing left to do.
	LoadedWidget = nullptr;
	RootCanvas = nullptr;
	OwnedTree = nullptr;
}

UUINavigationInputSelectionHandler* UDreamWidgetPresenterComponentBase::GetNavigationSelection()
{
	if (!NavigationSelection.IsValid())
	{
		// Null means "not overridden on this component", so fall back to the project setting.
		TSubclassOf<UDreamUserWidget> SelectionClass = NavigationSelectionClass;
		if (SelectionClass == nullptr)
		{
			SelectionClass = UDreamGUISettings::LoadSettingClass(UDreamGUISettings::Get()->NavigationSelectionClass, TEXT("NavigationSelectionClass"));
		}
		if (auto Widget = CreateDreamWidget(this->GetWorld(), SelectionClass, this->LoadedWidget.Get()))
		{
			NavigationSelection = Widget->GetComponent<UUINavigationInputSelectionHandler>();
		}
	}
	return NavigationSelection.Get();
}

void UDreamWidgetPresenterComponentBase::SetWidgetOpacity(float Value)
{
	WidgetOpacity = Value;
	if (UDreamWidget* Widget = LoadedWidget.Get())
	{
		Widget->SetRenderOpacity(Value);
	}
}

void UDreamWidgetPresenterComponentBase::SetWidgetOffset(const FVector& Value)
{
	WidgetOffset = Value;
	if (UDreamWidget* Widget = LoadedWidget.Get())
	{
		Widget->SetRenderTranslation(Value);
	}
}

void UDreamWidgetPresenterComponentBase::SetWidgetVisible(bool Value)
{
	bWidgetVisible = Value;
	if (UDreamWidget* Widget = LoadedWidget.Get())
	{
		Widget->SetWidgetActive(Value);
	}
}

void UDreamWidgetPresenterComponentBase::ApplyWidgetOverridesToLoadedWidget()
{
	UDreamWidget* Widget = LoadedWidget.Get();
	if (Widget == nullptr)
	{
		return;
	}
	// Only values someone actually set: pushing the defaults would stomp what the prefab authored
	// (a root the author deliberately hid, most importantly).
	if (WidgetOpacity != 1.0f)
	{
		Widget->SetRenderOpacity(WidgetOpacity);
	}
	if (!WidgetOffset.IsZero())
	{
		Widget->SetRenderTranslation(WidgetOffset);
	}
	if (!bWidgetVisible)
	{
		Widget->SetWidgetActive(false);
	}
}

void UDreamWidgetPresenterComponentBase::NotifyWidgetLoaded()
{
	UWorld* World = GetWorld();
	// Game worlds only: the editor's sequencer is not a UMovieSceneSequencePlayer, and it re-resolves
	// through its own refresh paths anyway.
	if (World == nullptr || !World->IsGameWorld())
	{
		return;
	}
	for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
	{
		ULevelSequencePlayer* Player = It->GetSequencePlayer();
		UMovieSceneSequence* Sequence = Player != nullptr ? Player->GetSequence() : nullptr;
		const UMovieScene* MovieScene = Sequence != nullptr ? Sequence->GetMovieScene() : nullptr;
		if (MovieScene == nullptr)
		{
			continue;
		}
		// Root-level bindings only; a widget binding inside a subsequence still needs its own poke.
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			Player->RequestInvalidateBinding(UE::MovieScene::FFixedObjectBindingID(Binding.GetObjectGuid(), MovieSceneSequenceID::Root));
		}
	}
}

#if WITH_EDITOR

void UDreamWidgetPresenterComponentBase::ReloadWidget()
{
	LoadWidget();
}
#endif

#undef LOCTEXT_NAMESPACE
