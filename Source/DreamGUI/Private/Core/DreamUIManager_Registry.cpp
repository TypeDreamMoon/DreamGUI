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

#undef LOCTEXT_NAMESPACE
