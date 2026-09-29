// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamGUIEditorSubsystem.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUISpriteData.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWidgetTreeHost.h"
#include "DreamGUIEditorModule.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Subsystems/ImportSubsystem.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

UDreamGUIEditorSubsystem* UDreamGUIEditorSubsystem::Get()
{
	return GEditor != nullptr ? GEditor->GetEditorSubsystem<UDreamGUIEditorSubsystem>() : nullptr;
}

void UDreamGUIEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (GEditor != nullptr)
	{
		PreCompileHandle = GEditor->OnBlueprintPreCompile().AddUObject(this, &UDreamGUIEditorSubsystem::HandleBlueprintPreCompile);
		CompiledHandle = GEditor->OnBlueprintCompiled().AddUObject(this, &UDreamGUIEditorSubsystem::HandleBlueprintCompiled);
		// Up before this one, or the reimport would go unheard for the whole session.
		if (UImportSubsystem* Import = Collection.InitializeDependency<UImportSubsystem>())
		{
			ReimportHandle = Import->OnAssetReimport.AddUObject(this, &UDreamGUIEditorSubsystem::HandleAssetReimport);
		}
	}
	ObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddUObject(this, &UDreamGUIEditorSubsystem::HandleObjectsReplaced);
	PropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UDreamGUIEditorSubsystem::HandleObjectPropertyChanged);
	// Save a .dui and the classes built from it recompile, for the whole session: a .dui saved with no
	// designer open still has to reach whatever classes are loaded.
	FDreamUISourceWatcher::Register(SourceWatcher);
}

void UDreamGUIEditorSubsystem::Deinitialize()
{
	FDreamUISourceWatcher::Unregister(SourceWatcher);
	if (GEditor != nullptr)
	{
		GEditor->OnBlueprintPreCompile().Remove(PreCompileHandle);
		GEditor->OnBlueprintCompiled().Remove(CompiledHandle);
		if (UImportSubsystem* Import = GEditor->GetEditorSubsystem<UImportSubsystem>())
		{
			Import->OnAssetReimport.Remove(ReimportHandle);
		}
	}
	FCoreUObjectDelegates::OnObjectsReplaced.Remove(ObjectsReplacedHandle);
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(PropertyChangedHandle);
	if (RebuildTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RebuildTickerHandle);
		RebuildTickerHandle.Reset();
	}
	Previews.Reset();
	ReleasedPreviews.Reset();
	ReleasedHosts.Reset();
	ReleasedScreens.Reset();
	SpriteIconTextures.Reset();
	Super::Deinitialize();
}

void UDreamGUIEditorSubsystem::RegisterPreview(IDreamRecompilePreview* InPreview)
{
	if (InPreview != nullptr)
	{
		Previews.AddUnique(InPreview);
	}
}

void UDreamGUIEditorSubsystem::UnregisterPreview(IDreamRecompilePreview* InPreview)
{
	Previews.Remove(InPreview);
	ReleasedPreviews.Remove(InPreview);
}

void UDreamGUIEditorSubsystem::HandleBlueprintPreCompile(UBlueprint* InBlueprint)
{
	const UClass* Compiled = InBlueprint != nullptr ? InBlueprint->GeneratedClass.Get() : nullptr;
	if (Compiled == nullptr || !Compiled->IsChildOf(UDreamUserWidget::StaticClass()))
	{
		return;
	}
	bRecompiling = true;

	// The previews first: each takes its whole tree down, which leaves nothing of the class registered in
	// its world for the scan below to find a second time.
	for (IDreamRecompilePreview* Preview : TArray<IDreamRecompilePreview*>(Previews))
	{
		if (Preview != nullptr && Preview->UsesClass(Compiled))
		{
			Preview->ReleaseForRecompile();
			ReleasedPreviews.AddUnique(Preview);
		}
	}
	// Then every world that is still up: the level editor's, each preview's, a play session's.
	for (TObjectIterator<UDreamUIManagerWorldSubsystem> It(RF_ClassDefaultObject, true, EInternalObjectFlags::Garbage); It; ++It)
	{
		if (It->IsInitialized() && !It->HasTornDownWorld())
		{
			ReleaseTreesUsing(**It, Compiled);
		}
	}
}

void UDreamGUIEditorSubsystem::ReleaseTreesUsing(UDreamUIManagerWorldSubsystem& InManager, const UClass* InClass)
{
	// The screen's pages first, asked of the screen itself: a page hangs under whatever root the screen
	// uses, and that is not always one it made -- an overlay canvas somebody else placed is adopted -- so
	// the root's owner cannot say whether a tree is a page. The screen knows its pages by name.
	if (UWorld* World = InManager.GetWorld())
	{
		if (UDreamScreenUISubsystem* Screen = World->GetSubsystem<UDreamScreenUISubsystem>())
		{
			if (Screen->ReleasePagesUsing(InClass) > 0)
			{
				ReleasedScreens.AddUnique(Screen);
			}
		}
	}
	TArray<UDreamWidget*> Instances;
	for (UDreamWidget* Widget : InManager.GetRegisteredWidgets())
	{
		if (Widget->IsA(InClass))
		{
			Instances.Add(Widget);
		}
	}
	for (UDreamWidget* Instance : Instances)
	{
		if (!IsValid(Instance) || !Instance->HasRegistered())
		{
			continue;//an earlier release in this loop took it down with its tree
		}
		// Whoever holds the hierarchy it hangs in: the outer of the tree its root is the root of, or the
		// root's own outer.
		UDreamWidget* Root = Instance;
		while (UDreamWidget* Up = Root->GetParent())
		{
			Root = Up;
		}
		UObject* Holder = Root->GetOuter();
		if (const UDreamWidgetTree* Tree = Cast<UDreamWidgetTree>(Holder); Tree != nullptr && Tree->RootWidget == Root)
		{
			Holder = Tree->GetOuter();
		}
		if (IDreamWidgetTreeHost* Host = Cast<IDreamWidgetTreeHost>(Holder))
		{
			Host->ReleaseTree(EDreamTreeReleaseReason::Recompile);
			ReleasedHosts.AddUnique(Holder);
			continue;
		}
		// Nothing knows how to build it again -- a Blueprint's free widget, a tree made with no host -- and
		// a copy the reinstancer made of it would be a husk. The outermost instance of the class goes, and
		// nothing around it: the hierarchy it hangs in -- a preview's stage, a screen's root -- is not the
		// class's, and whoever hung the instance there makes it again.
		UDreamWidget* Outermost = Instance;
		for (UDreamWidget* Up = Instance->GetParent(); Up != nullptr; Up = Up->GetParent())
		{
			if (Up->IsA(InClass))
			{
				Outermost = Up;
			}
		}
		UE_LOG(DreamGUIEditor, Warning, TEXT("Recompiling %s destroyed %s, which nothing can build again: create it again."),
			*InClass->GetName(), *Outermost->GetPathDisplayName());
		Outermost->DestroyWidget();
	}
}

void UDreamGUIEditorSubsystem::HandleBlueprintCompiled()
{
	if (!bRecompiling)
	{
		return;
	}
	bRecompiling = false;
	if (ReleasedHosts.Num() == 0 && ReleasedScreens.Num() == 0 && ReleasedPreviews.Num() == 0)
	{
		ReportStaleInstances();
		return;
	}
	// A tick later, not now: the reinstancer is still swapping references when this is announced, and the
	// class a host builds from has to be the finished one.
	if (!RebuildTickerHandle.IsValid())
	{
		RebuildTickerHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("DreamGUIRebuildAfterRecompile"), 0.0f,
			[WeakThis = TWeakObjectPtr<UDreamGUIEditorSubsystem>(this)](float)
			{
				if (UDreamGUIEditorSubsystem* Self = WeakThis.Get())
				{
					Self->RebuildTickerHandle.Reset();
					Self->RebuildReleasedTrees();
				}
				return false;
			});
	}
}

void UDreamGUIEditorSubsystem::HandleObjectsReplaced(const TMap<UObject*, UObject*>& InReplacementMap)
{
	/**
	 * The backstop behind the release. A widget the reinstancer replaces while it is still registered is
	 * one no release reached -- registered in no world, or in a world with no manager -- and the reinstancer
	 * marks it and everything outered to it as garbage without unregistering any of it. Its canvas's mesh
	 * belongs to the host actor and outlives the collection that ends the compile, still registered with
	 * the renderer and drawing with materials that collection frees: the crash the level viewport met on
	 * the first frame after a compile. So such a widget still leaves the world here, while its memory is
	 * live -- EndPlay for all of it, then Unregister, the order a widget's own teardown takes -- and one in a
	 * world with a manager, which the release did walk, is reported: its tree's owner never let it go.
	 *
	 * Only the replaced widget's OWN contents, the ones outered inside it. A slot's content belongs to the
	 * host that placed the instance and is not being replaced.
	 */
	const auto IsLiveForTeardown = [](const UObject* InObject)
	{
		return InObject != nullptr
			&& !InObject->HasAnyFlags(RF_BeginDestroyed | RF_FinishDestroyed)
			&& !InObject->IsUnreachable();
	};
	TSet<const UDreamWidget*> Visited;
	TArray<UDreamWidget*> Pending;
	TArray<UDreamWidget*> ToTearDown;
	for (const TPair<UObject*, UObject*>& Replacement : InReplacementMap)
	{
		UDreamWidget* Replaced = Cast<UDreamWidget>(Replacement.Key);
		if (Replaced == nullptr || Replacement.Value == Replacement.Key
			|| !IsLiveForTeardown(Replaced) || !Replaced->HasRegistered())
		{
			continue;
		}
		ensureMsgf(UDreamUIManagerWorldSubsystem::GetInstance(Replaced->GetWorld()) == nullptr,
			TEXT("%s was still registered when a compile replaced it: whatever holds its tree did not let it go for the compile."),
			*Replaced->GetPathName());
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
			ToTearDown.Add(Widget);
			const TArray<UDreamWidget*> Children = Widget->GetChildren();
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
	for (UDreamWidget* Widget : ToTearDown)
	{
		if (IsLiveForTeardown(Widget))
		{
			Widget->EndPlay();
		}
	}
	for (UDreamWidget* Widget : ToTearDown)
	{
		if (IsLiveForTeardown(Widget))
		{
			Widget->OnUnregister();
		}
	}
}

void UDreamGUIEditorSubsystem::RebuildReleasedTrees()
{
	if (RebuildTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(RebuildTickerHandle);
		RebuildTickerHandle.Reset();
	}
	const TArray<TWeakObjectPtr<UObject>> Hosts = MoveTemp(ReleasedHosts);
	const TArray<TWeakObjectPtr<UDreamScreenUISubsystem>> Screens = MoveTemp(ReleasedScreens);
	const TArray<IDreamRecompilePreview*> ReleasedNow = MoveTemp(ReleasedPreviews);
	ReleasedHosts.Reset();
	ReleasedScreens.Reset();
	ReleasedPreviews.Reset();
	for (const TWeakObjectPtr<UObject>& WeakHost : Hosts)
	{
		if (IDreamWidgetTreeHost* Host = Cast<IDreamWidgetTreeHost>(WeakHost.Get()))
		{
			Host->RebuildTree();
		}
	}
	for (const TWeakObjectPtr<UDreamScreenUISubsystem>& Screen : Screens)
	{
		if (Screen.IsValid())
		{
			Screen->RebuildReleasedPages();
		}
	}
	for (IDreamRecompilePreview* Preview : ReleasedNow)
	{
		if (Previews.Contains(Preview))
		{
			Preview->RebuildAfterRecompile();
		}
	}
	UDreamUIManagerWorldSubsystem::RefreshAllUI();
	ReportStaleInstances();
}

void UDreamGUIEditorSubsystem::ReportStaleInstances() const
{
	for (TObjectIterator<UDreamUIManagerWorldSubsystem> It(RF_ClassDefaultObject, true, EInternalObjectFlags::Garbage); It; ++It)
	{
		if (!It->IsInitialized() || It->HasTornDownWorld())
		{
			continue;
		}
		for (const UDreamWidget* Widget : It->GetRegisteredWidgets())
		{
			const UClass* Class = Widget->GetClass();
			ensureMsgf(!Class->HasAnyClassFlags(CLASS_NewerVersionExists) && !Class->GetName().StartsWith(TEXT("REINST_")),
				TEXT("%s is still registered as an instance of %s, a class a compile has replaced."), *Widget->GetPathName(), *Class->GetName());
		}
	}
}

void UDreamGUIEditorSubsystem::HandleAssetReimport(UObject* InAsset)
{
	// A texture a sprite is cut from came back from disk: the sprites reload it, and every tree redraws.
	UTexture2D* Texture = Cast<UTexture2D>(InAsset);
	if (!IsValid(Texture))
	{
		return;
	}
	bool bNeedToRebuildUI = false;
	for (TObjectIterator<UDreamUISpriteData> It; It; ++It)
	{
		UDreamUISpriteData* SpriteData = *It;
		if (IsValid(SpriteData) && SpriteData->GetSpriteTexture() == Texture)
		{
			SpriteData->ReloadTexture();
			SpriteData->MarkPackageDirty();
			bNeedToRebuildUI = true;
		}
	}
	if (bNeedToRebuildUI)
	{
		UDreamUIManagerWorldSubsystem::RefreshAllUI();
	}
}

void UDreamGUIEditorSubsystem::HandleObjectPropertyChanged(UObject* InObject, FPropertyChangedEvent& InEvent)
{
	const UDreamText* Text = Cast<UDreamText>(InObject);
	if (Text == nullptr || Text->HasAnyFlags(RF_ClassDefaultObject)
		|| InEvent.GetMemberPropertyName() != UDreamText::GetPropertyName_Font())
	{
		return;
	}
	LastPickedFont = Text->GetFont();
}

UTexture2D* UDreamGUIEditorSubsystem::GetSpriteIconTexture(const FString& InPath, TFunctionRef<UTexture2D*()> InLoad)
{
	if (const TObjectPtr<UTexture2D>* Found = SpriteIconTextures.Find(InPath); Found != nullptr && IsValid(*Found))
	{
		return *Found;
	}
	UTexture2D* Loaded = InLoad();
	SpriteIconTextures.Add(InPath, Loaded);
	return Loaded;
}
