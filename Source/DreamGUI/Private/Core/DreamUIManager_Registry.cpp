// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIManager.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/DreamGUISettings.h"

#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "UObject/Package.h"
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
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Core/DreamUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "DreamUIManager"
#define ENABLED_DreamGUI_DEBUG_DUMP				0
#define ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME		0

void UDreamUIManagerWorldSubsystem::OnCultureChanged()
{
	bShouldUpdateOnCultureChanged = true;
}

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


bool UDreamUIManagerWorldSubsystem::IsCanvasStillRegistered(const TWeakObjectPtr<UDreamCanvas>& InCanvas)const
{
	const UDreamCanvas* Canvas = InCanvas.Get();
	return Canvas != nullptr && RegisteredCanvasKeys.Contains(FObjectKey(Canvas));
}

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
	this->RegisteredCanvasKeys.Add(FObjectKey(InCanvas));
	BumpHitTestGeneration();
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
	this->RegisteredCanvasKeys.Remove(FObjectKey(InCanvas));
	BumpHitTestGeneration();
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

TArray<UDreamWidget*> UDreamUIManagerWorldSubsystem::GetRegisteredWidgets()const
{
	TArray<UDreamWidget*> Widgets;
	Widgets.Reserve(RegisteredWidgets.Num());
	for (const TWeakObjectPtr<UDreamWidget>& Registered : RegisteredWidgets)
	{
		if (UDreamWidget* Widget = Registered.Get())
		{
			Widgets.Add(Widget);
		}
	}
	return Widgets;
}

bool UDreamUIManagerWorldSubsystem::IsWidgetRegistered(const UDreamWidget* InWidget)const
{
	return InWidget != nullptr && RegisteredWidgets.ContainsByPredicate(
		[InWidget](const TWeakObjectPtr<UDreamWidget>& Registered) { return Registered.Get() == InWidget; });
}

bool UDreamUIManagerWorldSubsystem::IsHeldByHost(const UDreamWidget* InRoot)const
{
	if (InRoot == nullptr)
	{
		return false;
	}
	const UObject* Owner = InRoot->GetOuter();
	if (const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Owner))
	{
		// Only the root a tree names is held through it; any other widget of the tree hangs off a parent,
		// or off nothing.
		if (Tree->RootWidget != InRoot)
		{
			return false;
		}
		Owner = Tree->GetOuter();
	}
	return Owner != nullptr && Owner != this && !Owner->IsA<UWorld>() && !Owner->IsA<UDreamWidget>() && !Owner->IsA<UPackage>();
}

void UDreamUIManagerWorldSubsystem::AdoptIfFreeRoot(UDreamWidget* InRoot)
{
	if (IsValid(InRoot) && InRoot->GetParent() == nullptr && InRoot->HasRegistered() && !IsHeldByHost(InRoot))
	{
		FreeRoots.AddUnique(InRoot);
	}
}

bool UDreamUIManagerWorldSubsystem::IsFreeRoot(const UDreamWidget* InWidget)const
{
	return InWidget != nullptr && FreeRoots.ContainsByPredicate([InWidget](const TObjectPtr<UDreamWidget>& Root) { return Root.Get() == InWidget; });
}

void UDreamUIManagerWorldSubsystem::ForgetFreeRoot(const UDreamWidget* InWidget)
{
	FreeRoots.RemoveSingle(const_cast<UDreamWidget*>(InWidget));
}

void UDreamUIManagerWorldSubsystem::RegisterTreeHost(UObject* InHost)
{
	if (!ensureMsgf(IsValid(InHost) && InHost->Implements<UDreamWidgetTreeHost>(), TEXT("%s: %s enrolled as a tree host without being one."),
		*GetPathName(), *GetPathNameSafe(InHost)))
	{
		return;
	}
	TreeHosts.RemoveAll([](const TWeakObjectPtr<UObject>& Host) { return !Host.IsValid(); });
	TreeHosts.AddUnique(InHost);
}

void UDreamUIManagerWorldSubsystem::UnregisterTreeHost(const UObject* InHost)
{
	TreeHosts.RemoveAll([InHost](const TWeakObjectPtr<UObject>& Host) { return !Host.IsValid() || Host.Get() == InHost; });
}

bool UDreamUIManagerWorldSubsystem::IsTreeHostRegistered(const UObject* InHost)const
{
	return InHost != nullptr && TreeHosts.ContainsByPredicate([InHost](const TWeakObjectPtr<UObject>& Host) { return Host.Get() == InHost; });
}

void UDreamUIManagerWorldSubsystem::ReleaseHostTrees(EDreamTreeReleaseReason InReason, const ULevel* InOnlyLevel)
{
	// A snapshot: letting a tree go is free to unregister the host, or to register another.
	const TArray<TWeakObjectPtr<UObject>> Hosts = TreeHosts;
	for (const TWeakObjectPtr<UObject>& WeakHost : Hosts)
	{
		UObject* HostObject = WeakHost.Get();
		IDreamWidgetTreeHost* Host = Cast<IDreamWidgetTreeHost>(HostObject);
		if (Host == nullptr)
		{
			continue;
		}
		if (InOnlyLevel != nullptr)
		{
			const AActor* HostActor = Cast<AActor>(HostObject);
			if (HostActor == nullptr)
			{
				HostActor = HostObject->GetTypedOuter<AActor>();
			}
			if (HostActor == nullptr || HostActor->GetLevel() != InOnlyLevel)
			{
				continue;
			}
		}
		Host->ReleaseTree(InReason);
	}
}

void UDreamUIManagerWorldSubsystem::HandleLevelRemovedFromWorld(ULevel* InLevel, UWorld* InWorld)
{
	// Hiding or unloading a sublevel unregisters its actors' components and stops there: no EndPlay in
	// an editor world, no destruction. The trees of the hosts in it go now, rather than drawing on for a
	// level that is no longer in the world; showing it again registers the hosts, which build afresh.
	// A null level is the whole world going, which TeardownWorld sees to.
	if (InLevel != nullptr && InWorld != nullptr && InWorld == GetWorld())
	{
		ReleaseHostTrees(EDreamTreeReleaseReason::LevelRemoved, InLevel);
	}
}

void UDreamUIManagerWorldSubsystem::AddWidget(UDreamWidget* InWidget)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (IsWidgetRegistered(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	RegisteredWidgets.RemoveAll([](const TWeakObjectPtr<UDreamWidget>& Registered) { return !Registered.IsValid(); });
	RegisteredWidgets.AddUnique(InWidget);
	// A root no host holds has nobody else to keep it alive now that registering is not owning.
	AdoptIfFreeRoot(InWidget);
}

void UDreamUIManagerWorldSubsystem::RemoveWidget(UDreamWidget* InWidget)
{
	ParkedWidgets.RemoveAll(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == nullptr || Entry.Widget == InWidget; });
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (!IsWidgetRegistered(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	RegisteredWidgets.RemoveAll(
		[InWidget](const TWeakObjectPtr<UDreamWidget>& Registered) { return !Registered.IsValid() || Registered.Get() == InWidget; });
	// Unregistered is no longer the manager's to keep, whatever becomes of it: a widget moved out of a
	// tree that came down is free to be registered where it went, and holds its own place there.
	ForgetFreeRoot(InWidget);
	if (UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(InWidget))
	{
		// A widget only reaches here from OnUnregister, which is teardown -- and a torn-down user
		// widget must stop being polled for its property bindings. The poll loop's own IsValid sweep
		// answers that only once DestroyWidget has marked the widget garbage, which is the last thing
		// it does; a widget unregistered without being destroyed is never marked at all.
		RemovePropertyBindingUser(UserWidget);
	}
}

void UDreamUIManagerWorldSubsystem::DestroyRegisteredWidgetTrees(bool bInReportTreesOutlivingHosts)
{
	const TArray<UDreamWidget*> Registered = GetRegisteredWidgets();
	TArray<UDreamWidget*> Roots;
	for (UDreamWidget* Widget : Registered)
	{
		UDreamWidget* Root = Widget->GetRootWidgetInHierarchy();
		Roots.AddUnique(Root != nullptr ? Root : Widget);
	}

	for (UDreamWidget* Root : Roots)
	{
		if (!IsValid(Root))
		{
			continue;
		}
		// The pool's own are the manager's to take down; a tree its host still holds is a host that was
		// asked to let go and did not.
		if (bInReportTreesOutlivingHosts && !IsFreeRoot(Root) && IsHeldByHost(Root))
		{
			const UObject* Host = Root->GetOuter();
			if (const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Host))
			{
				Host = Tree->GetOuter();
			}
			ensureMsgf(false, TEXT("%s: the tree %s outlived its host %s, which never let it go."),
				*GetPathName(), *Root->GetPathDisplayName(), *GetPathNameSafe(Host));
		}
		Root->DestroyWidget();
	}

	// A hierarchy whose root could not be reached from its widgets -- corrupt, or half taken apart.
	for (UDreamWidget* Widget : GetRegisteredWidgets())
	{
		if (IsValid(Widget) && Widget->HasRegistered())
		{
			Widget->DestroyWidget();
		}
	}
	FreeRoots.Reset();
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

#undef LOCTEXT_NAMESPACE

UDreamUIDataAsTexture* UDreamUIManagerWorldSubsystem::GetRectBlockDataRows(UDreamRectBlockData* InData, int32 InBlockSizeInBytes, EDreamUIDataAsTexturePixelFormat InPixelFormat)
{
	if (InData == nullptr)
	{
		return nullptr;
	}
	if (const TObjectPtr<UDreamUIDataAsTexture>* Found = RectBlockDataRows.Find(InData); Found != nullptr && IsValid(Found->Get()))
	{
		return Found->Get();
	}
	// The manager's, and never saved, duplicated or copied: a play session's copy of this world makes its own.
	UDreamUIDataAsTexture* Rows = NewObject<UDreamUIDataAsTexture>(this, NAME_None, DreamUI::RuntimeObjectFlags);
	Rows->Init(InBlockSizeInBytes, InPixelFormat, 32);
	RectBlockDataRows.Add(InData, Rows);
	return Rows;
}
