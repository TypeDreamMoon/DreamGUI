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


UDreamUIManagerObject* UDreamUIManagerObject::Instance = nullptr;
#if WITH_EDITOR
bool UDreamUIManagerObject::bIsBlueprintCompiling = false;
#endif
UDreamUIManagerObject::UDreamUIManagerObject()
{

}
void UDreamUIManagerObject::BeginDestroy()
{
#if WITH_EDITORONLY_DATA
	if (OnAssetReimportDelegateHandle.IsValid())
	{
		if (GEditor)
		{
			if (auto ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
			{
				ImportSubsystem->OnAssetReimport.Remove(OnAssetReimportDelegateHandle);
			}
		}
	}
	if (OnMapOpenedDelegateHandle.IsValid())
	{
		FEditorDelegates::OnMapOpened.Remove(OnMapOpenedDelegateHandle);
	}
	if (OnPackageReloadedDelegateHandle.IsValid())
	{
		FCoreUObjectDelegates::OnPackageReloaded.Remove(OnPackageReloadedDelegateHandle);
	}
	if (OnObjectsReplacedDelegateHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectsReplaced.Remove(OnObjectsReplacedDelegateHandle);
	}
	if (OnBlueprintPreCompileDelegateHandle.IsValid())
	{
		if (GEditor)
		{
			GEditor->OnBlueprintPreCompile().Remove(OnBlueprintPreCompileDelegateHandle);
		}
	}
	if (OnBlueprintCompiledDelegateHandle.IsValid())
	{
		if (GEditor)
		{
			GEditor->OnBlueprintCompiled().Remove(OnBlueprintCompiledDelegateHandle);
		}
	}
#endif
	Instance = nullptr;
	Super::BeginDestroy();
}

void UDreamUIManagerObject::Tick(float DeltaTime)
{
#if WITH_EDITOR
	if (EditorTick.IsBound())
	{
		EditorTick.Broadcast(DeltaTime);
	}
	if (OneShotFunctionsToExecuteInTick.Num() > 0)
	{
		for (int i = 0; i < OneShotFunctionsToExecuteInTick.Num(); i++)
		{
			if (OneShotFunctionsToExecuteInTick[i].Key <= 0)
			{
				// Move the function out and drop its entry BEFORE calling it. A one-shot is free to
				// queue another one -- OnBlueprintCompiled -> RefreshAllUI -> EnsureDataForRebuild does
				// exactly that -- and the resulting reallocation used to happen underneath both the
				// reference held here and the TFunction object being executed.
				TFunction<void()> Function = MoveTemp(OneShotFunctionsToExecuteInTick[i].Value);
				OneShotFunctionsToExecuteInTick.RemoveAt(i);
				i--;
				Function();
			}
			else
			{
				OneShotFunctionsToExecuteInTick[i].Key--;
			}
		}
	}
#endif
}
TStatId UDreamUIManagerObject::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamGUIEditorManagerObject, STATGROUP_Tickables);
}

#if WITH_EDITOR

void UDreamUIManagerObject::AddOneShotTickFunction(const TFunction<void()>& InFunction, int InDelayFrameCount)
{
	InitCheck();
	InDelayFrameCount = FMath::Max(0, InDelayFrameCount);
	TTuple<int, TFunction<void()>> Item;
	Item.Key = InDelayFrameCount;
	Item.Value = InFunction;
	Instance->OneShotFunctionsToExecuteInTick.Add(Item);
}

FDreamUIEditorTickMulticastDelegate& UDreamUIManagerObject::GetEditorTickDelegate()
{
	return EditorTick;
}

UDreamUIManagerObject* UDreamUIManagerObject::GetInstance(bool CreateIfNotValid)
{
	if (CreateIfNotValid)
	{
		InitCheck();
	}
	return Instance;
}
bool UDreamUIManagerObject::InitCheck()
{
	if (Instance == nullptr)
	{
		UE_LOG(DreamGUI, Log, TEXT("[%s].%d No Instance of class %s, create it"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *UDreamUIManagerObject::StaticClass()->GetName());
		Instance = NewObject<UDreamUIManagerObject>();
		Instance->AddToRoot();
		//open map
		Instance->OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddUObject(Instance, &UDreamUIManagerObject::OnMapOpened);
		Instance->OnPackageReloadedDelegateHandle = FCoreUObjectDelegates::OnPackageReloaded.AddUObject(Instance, &UDreamUIManagerObject::OnPackageReloaded);
		//a recompiled class's instances replaced by copies
		Instance->OnObjectsReplacedDelegateHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(Instance, &UDreamUIManagerObject::OnObjectsReplaced);
		if (GEditor)
		{
			//reimport asset
			Instance->OnAssetReimportDelegateHandle = GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddUObject(Instance, &UDreamUIManagerObject::OnAssetReimport);
			//blueprint recompile
			Instance->OnBlueprintPreCompileDelegateHandle = GEditor->OnBlueprintPreCompile().AddUObject(Instance, &UDreamUIManagerObject::OnBlueprintPreCompile);
			Instance->OnBlueprintCompiledDelegateHandle = GEditor->OnBlueprintCompiled().AddUObject(Instance, &UDreamUIManagerObject::OnBlueprintCompiled);
		}
	}
	return true;
}

void UDreamUIManagerObject::OnBlueprintPreCompile(UBlueprint* InBlueprint)
{
	bIsBlueprintCompiling = true;
}

void UDreamUIManagerObject::OnObjectsReplaced(const TMap<UObject*, UObject*>& InReplacementMap)
{
	/**
	 * Recompiling a widget Blueprint replaces every live instance of it with a copy, and the reinstancer
	 * marks each original -- and every object outered to it -- as garbage while the original is still
	 * REGISTERED. Nobody unregistered it: the copy takes its place in whatever held it, the next
	 * collection frees it, and everything its registration had set up was simply abandoned.
	 *
	 * Most of that dies with it. A world-space canvas's mesh does not: it belongs to the host actor, so
	 * it outlived the collection, still registered with the renderer, drawing sections whose materials
	 * the canvas had made and the collection had just freed. The level viewport's next frame called
	 * into one of them. Until then it drew the old tree beside the copy's new one.
	 *
	 * So the original is unregistered here, while its memory is live and before any collection: its
	 * canvases leave the manager and destroy the meshes they made, its visuals leave their canvas. It is
	 * not detached or destroyed -- the reinstancer is about to swap the copy into the parent that holds
	 * it, and the compile's own repair (OnBlueprintCompiled) brings the copy up in its place.
	 *
	 * Only the replaced instance's OWN widgets, the ones outered inside it. A slot's content belongs to
	 * the host that placed the instance, is not being replaced, and is re-registered under the copy.
	 */
	const auto IsLiveForTeardown = [](const UObject* InObject)
	{
		return InObject != nullptr
			&& !InObject->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& !InObject->IsUnreachable();
	};
	TSet<const UDreamWidget*> Visited;
	TArray<UDreamWidget*> Pending;
	for (const TPair<UObject*, UObject*>& Replacement : InReplacementMap)
	{
		UDreamWidget* Replaced = Cast<UDreamWidget>(Replacement.Key);
		if (Replaced == nullptr || Replacement.Value == Replacement.Key
			|| !IsLiveForTeardown(Replaced) || !Replaced->HasRegistered())
		{
			continue;
		}
		// Parents before children, the order DestroyWidget unregisters in.
		Pending.Reset();
		Pending.Add(Replaced);
		while (Pending.Num() > 0)
		{
			UDreamWidget* Widget = Pending.Pop(EAllowShrinking::No);
			if (!IsLiveForTeardown(Widget) || Visited.Contains(Widget))
			{
				continue;
			}
			Visited.Add(Widget);
			const TArray<UDreamWidget*> Children = Widget->GetChildren();
			if (Widget->HasRegistered())
			{
				Widget->OnUnregister();
			}
			for (int32 Index = Children.Num() - 1; Index >= 0; --Index)
			{
				UDreamWidget* Child = Children[Index];
				if (Child != nullptr && Child->IsIn(Replaced))
				{
					Pending.Add(Child);
				}
			}
		}
	}
}
void UDreamUIManagerObject::OnBlueprintCompiled()
{
	// Cleared on the announcement itself, not a tick later: the compile is over when the editor says
	// so. The editor broadcasts OnBlueprintPreCompile only from a compilation queue's flush, and every
	// flush ends in OnBlueprintCompiled -- its own, or its caller's once reinstancing is done -- whether
	// or not the compile had errors (BlueprintCompilationManager.cpp, and the RigVM copy of it), so the
	// set and this clear are a pair. It used to be cleared in the one-shot below, and this object ticks only in an engine frame
	// in which some world ticked (UEditorEngine::Tick), which can be many frames later: for all of them
	// the editor looked as though a Blueprint were still compiling.
	bIsBlueprintCompiling = false;
	UDreamUIManagerObject::AddOneShotTickFunction([] {
		// Before the refresh, because a half-dead instance has nothing for a refresh to walk.
		//
		// Recompiling replaces every live instance with a fresh copy of the new class, and the copy
		// arrives with its contents still attached, its WidgetTree null (DuplicateTransient) and its
		// bInitialized false -- so the widget on screen had a null content root, no resolved bindings
		// and silently inert animation verbs, and nothing anywhere would have put it right. Deferred a
		// tick, like the refresh, so the reinstancer has finished swapping references first.
		int32 RepairedCount = 0;
		for (TObjectIterator<UDreamUserWidget> It; It; ++It)
		{
			UDreamUserWidget* UserWidget = *It;
			if (IsValid(UserWidget) && UserWidget->NeedsReinitializeFromClass())
			{
				UserWidget->ReinitializeFromClass();
				++RepairedCount;
			}
		}
		if (RepairedCount > 0)
		{
			UE_LOG(DreamGUI, Log, TEXT("Rebuilt %d live DreamUI widget(s) from their recompiled class."), RepairedCount);
		}
		UDreamUIManagerWorldSubsystem::RefreshAllUI();
		});
}

void UDreamUIManagerObject::OnAssetReimport(UObject* Asset)
{
	if (IsValid(Asset))
	{
		if (auto TextureAsset = Cast<UTexture2D>(Asset))
		{
			bool bNeedToRebuildUI = false;
			//find sprite data that reference this texture
			for (TObjectIterator<UDreamUISpriteData> Itr; Itr; ++Itr)
			{
				UDreamUISpriteData* SpriteData = *Itr;
				if (IsValid(SpriteData))
				{
					if (SpriteData->GetSpriteTexture() == TextureAsset)
					{
						SpriteData->ReloadTexture();
						SpriteData->MarkPackageDirty();
						bNeedToRebuildUI = true;
					}
				}
			}
			//Refresh ui
			if (bNeedToRebuildUI)
			{
				UDreamUIManagerWorldSubsystem::RefreshAllUI();
			}
		}
		// A presenter used to be refreshed by hand when its prefab was saved, by comparing a stored
		// MD5. It holds a class now, and recompiling a Blueprint reinstances the objects of that class
		// on its own, so there is nothing left for this branch to do.
	}
}

void UDreamUIManagerObject::OnMapOpened(const FString& FileName, bool AsTemplate)
{

}

void UDreamUIManagerObject::OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (Phase == EPackageReloadPhase::PostBatchPostGC && Event != nullptr && Event->GetNewPackage() != nullptr)
	{
		auto Asset = Event->GetNewPackage()->FindAssetInPackage();
	}
}


UDreamUISelection* UDreamUISelection::GetInstance(UWorld* InWorld)
{
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
	{
		return DreamUIManager->GetSelection();
	}
	return nullptr;
}

void UDreamUISelection::SelectWidget(UDreamWidget* Widget)
{
	// A widget listed twice takes every per-selection delta twice: Align and Distribute walk the
	// array, so a duplicate entry moves that widget by double the offset the others get.
	SelectedWidgetArray.AddUnique(Widget);
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::DeselectWidget(UDreamWidget* Widget)
{
	if (SelectedWidgetArray.Remove(Widget) > 0)
	{
		OnSelectionChanged.Broadcast();
	}
}

void UDreamUISelection::SelectComponent(UDreamUIBehaviour* Component)
{
	SelectedComponentArray.Add(Component);
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::ClearComponentSelection()
{
	SelectedComponentArray.Empty();
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::SelectNone()
{
	SelectedWidgetArray.Empty();
	SelectedComponentArray.Empty();
	OnSelectionChanged.Broadcast();
}

bool UDreamUISelection::IsSelected(UDreamWidget* Widget)const
{
	return SelectedWidgetArray.Contains(Widget);
}
#endif

#undef LOCTEXT_NAMESPACE
