// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIManager.h"

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

	// The loaded tree is outered to the World and held by the manager's AllWidgetArray, so nothing
	// releases it when the owning actor goes away unless someone says so here. A world-space health
	// bar otherwise outlived every enemy that died -- still registered, still ticking, still drawing
	// -- one live tree per corpse until the world ended. EndPlay and not OnUnregister, because a
	// component unregisters for reasons that are not a teardown (a reregister context, for one) and
	// throwing the tree away there would take it out from under a live actor.
	//
	// Skipped while garbage collection is already destroying this component -- UActorComponent's
	// BeginDestroy routes here too, and the widget is unreachable by then, so the weak pointer
	// answers null anyway. A whole-world shutdown is safe as it stands: UWorld::EndPlay routes every
	// actor before it reaches the subsystems, so DestroyRegisteredWidgetTrees finds this tree already
	// gone, and DestroyWidget tolerates a second call regardless.
	if (!HasAnyFlags(RF_BeginDestroyed))
	{
		DestroyLoadedWidget();
	}
	LoadedWidget = nullptr;
	RootCanvas = nullptr;
}

void UDreamWidgetPresenterComponentBase::OnRegister()
{
	Super::OnRegister();
	UWorld* World = GetWorld();
	// An Inactive (or typeless) world is a package being preloaded -- double-clicking a map asset
	// loads it before the map command runs. A tree built there answers to no manager and is reaped
	// by the next GC through the BeginDestroy fallback, mid-purge; building it is pure liability.
	if (World == nullptr || World->WorldType == EWorldType::Inactive || World->WorldType == EWorldType::None)
	{
		return;
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

#if WITH_EDITOR
	// An edit-mode tree has no EndPlay, and an unregister on its own does not say which of three things
	// is happening:
	//  - a reregister (a property edit, an undo that keeps the component): it registers again before
	//    the frame is out and the tree must survive it, which is the reason for not destroying above;
	//  - a destruction (component deleted, actor destroyed): OnComponentDestroyed follows, but the
	//    component already knows, so the tree goes now rather than drawing on for the rest of the frame;
	//  - a removal that never routes a destruction at all. World Partition unloading actors in the
	//    editor, hiding a sublevel, and an undo that takes the component away all unregister and stop
	//    there. Nothing after that releases the tree until garbage collection reaches BeginDestroy --
	//    until then it keeps drawing for an actor that is no longer in the level -- and a teardown run
	//    from inside GC is one this plugin has already been bitten by: DestroyWidget reaches other
	//    objects, and any of them may be unreachable in the same purge.
	// So a known destruction tears down at once, and anything else is looked at again a frame later and
	// torn down only if nobody has registered the component in the meantime. Anything that comes back
	// later (a sublevel shown again, a redo) goes through OnRegister, which builds a fresh tree.
	const UWorld* World = GetWorld();
	if (LoadedWidget.IsValid() && (World == nullptr || !World->IsGameWorld()))
	{
		const AActor* Owner = GetOwner();
		if (IsBeingDestroyed() || (Owner != nullptr && Owner->IsActorBeingDestroyed()))
		{
			DestroyLoadedWidget();
		}
		else
		{
			TWeakObjectPtr<UDreamWidgetPresenterComponentBase> WeakThis(this);
			UDreamUIManagerObject::AddOneShotTickFunction([WeakThis]()
			{
				// Garbage still resolves: an undo that removes the component leaves it marked garbage but
				// uncollected, and releasing the tree before collection is the whole point. Unreachable
				// objects still answer null, so this never touches one a purge is already destroying.
				UDreamWidgetPresenterComponentBase* Presenter = WeakThis.Get(/*bEvenIfPendingKill*/ true);
				if (Presenter != nullptr && !Presenter->IsRegistered())
				{
					Presenter->DestroyLoadedWidget();
				}
			}, 1);
		}
	}
#endif
}

void UDreamWidgetPresenterComponentBase::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// The component is going away for good -- deleted from an actor, or its actor destroyed -- which
	// OnUnregister no longer stands in for.
	DestroyLoadedWidget();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UDreamWidgetPresenterComponentBase::BeginDestroy()
{
	// The backstop for a component collected without anyone destroying it first, which is how an
	// editor-world tree usually ends. By this point garbage collection may already have taken the
	// widget, in which case the weak pointer answers null and there is nothing to do.
	DestroyLoadedWidget();
	Super::BeginDestroy();
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
		UMovieScene* MovieScene = Sequence != nullptr ? Sequence->GetMovieScene() : nullptr;
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
