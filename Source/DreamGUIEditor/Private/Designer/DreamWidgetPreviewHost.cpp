// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamWidgetPreviewHost.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamWidgetPropertyBindingExtension.h"
#include "DreamGUI.h"
#include "DreamGUIEditorModule.h"
#include "Preview/DreamWidgetDesignerScene.h"

#include "Engine/World.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ObjectEditorUtils.h"
#include "PropertyCustomizationHelpers.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "DreamWidgetPreviewHost"

namespace DreamWidgetPreviewHostLocal
{
	/**
	 * Copy one value along a property chain from the preview onto the template.
	 *
	 * Ported from UMG's WidgetBlueprintEditor.cpp. The recursion is the whole point: a designer edits
	 * a padding on a PanelSlot or a colour on a Visual far more often than anything directly on the
	 * widget, and those are instanced sub-objects -- the chain has to be walked down into them or the
	 * only edits that ever reach the template are the ones on the widget itself.
	 *
	 * bIsModify is the PreEditChange pass: it snapshots the destination for undo and writes nothing.
	 */
	bool MigrateAlongChain(UObject* InSource, UObject* InDestination,
		FEditPropertyChain::TDoubleLinkedListNode* InChainNode, FProperty* InMemberProperty, bool bIsModify)
	{
		if (InSource == nullptr || InDestination == nullptr || InChainNode == nullptr)
		{
			return false;
		}
		if (InSource->GetClass() != InDestination->GetClass())
		{
			// The pair is matched by name, and a name can outlive a class change (a widget replaced by
			// one of another type keeping the object name). Copying between them would import text
			// written for a layout that is not there.
			return false;
		}

		FProperty* CurrentProperty = InChainNode->GetValue();
		FEditPropertyChain::TDoubleLinkedListNode* NextNode = InChainNode->GetNextNode();

		// Containers and structs copy whole: descending into one element would leave the rest of the
		// container on the template as it was, which is not what the edit meant.
		if (CastField<FArrayProperty>(CurrentProperty) || CastField<FMapProperty>(CurrentProperty)
			|| CastField<FSetProperty>(CurrentProperty) || CastField<FStructProperty>(CurrentProperty))
		{
			NextNode = nullptr;
		}

		FObjectProperty* CurrentObjectProperty = CastField<FObjectProperty>(CurrentProperty);
		if (CurrentObjectProperty != nullptr)
		{
			// One side missing the sub-object means there is nothing below to descend into; the copy
			// has to happen at this level, which replaces the reference itself.
			if (CurrentObjectProperty->GetObjectPropertyValue_InContainer(InSource) == nullptr
				|| CurrentObjectProperty->GetObjectPropertyValue_InContainer(InDestination) == nullptr)
			{
				NextNode = nullptr;
			}
		}

		if (NextNode == nullptr)
		{
			if (bIsModify)
			{
				InDestination->SetFlags(RF_Transactional);
				InDestination->Modify();
				return true;
			}
			// A property gated by an edit condition is only half-copied without the condition itself.
			bool bDummyNegate = false;
			if (FBoolProperty* EditConditionProperty = PropertyCustomizationHelpers::GetEditConditionProperty(InMemberProperty, bDummyNegate))
			{
				FObjectEditorUtils::MigratePropertyValue(InSource, EditConditionProperty, InDestination, EditConditionProperty);
			}
			return FObjectEditorUtils::MigratePropertyValue(InSource, InMemberProperty, InDestination, InMemberProperty);
		}

		if (CurrentObjectProperty != nullptr)
		{
			return MigrateAlongChain(
				CurrentObjectProperty->GetObjectPropertyValue_InContainer(InSource),
				CurrentObjectProperty->GetObjectPropertyValue_InContainer(InDestination),
				NextNode, NextNode->GetValue(), bIsModify);
		}
		return MigrateAlongChain(InSource, InDestination, NextNode, InMemberProperty, bIsModify);
	}
}

FDreamWidgetPreviewHost::FDreamWidgetPreviewHost() = default;

FDreamWidgetPreviewHost::~FDreamWidgetPreviewHost()
{
	Shutdown();
}

void FDreamWidgetPreviewHost::Initialize(UDreamWidgetBlueprint* InBlueprint)
{
	if (!IsValid(InBlueprint))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d The preview host needs a valid Blueprint."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	Blueprint = InBlueprint;

	Scene = MakeUnique<FDreamWidgetDesignerScene>(
		FDreamWidgetDesignerScene::ConstructionValues()
		.AllowAudioPlayback(true)
		.ShouldSimulatePhysics(false)
		.SetEditor(true));

	// Without this the manager never ticks in the editor world, so nothing in the preview lays out
	// or draws -- it would be structurally correct and completely blank.
	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		Manager->bShouldTickInEditor = true;
	}

	ObjectsReplacedHandle = FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &FDreamWidgetPreviewHost::OnObjectsReplaced);
	// Invalidating from here rather than from each editing function: a caller that forgets produces a
	// designer that silently stops updating, which reads as a rendering bug rather than a missing call.
	// OnChanged fires on structural edits and on undo; MarkBlueprintAsModified (a value edit) does not,
	// which is what keeps a drag from rebuilding the preview on every mouse move.
	BlueprintChangedHandle = Blueprint->OnChanged().AddRaw(this, &FDreamWidgetPreviewHost::OnBlueprintChanged);
	BlueprintCompiledHandle = Blueprint->OnCompiled().AddRaw(this, &FDreamWidgetPreviewHost::OnBlueprintCompiled);

	RebuildPreview();
}

void FDreamWidgetPreviewHost::Shutdown()
{
	if (ObjectsReplacedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectsReplaced.Remove(ObjectsReplacedHandle);
		ObjectsReplacedHandle.Reset();
	}
	if (IsValid(Blueprint))
	{
		Blueprint->OnChanged().Remove(BlueprintChangedHandle);
		Blueprint->OnCompiled().Remove(BlueprintCompiledHandle);
	}
	BlueprintChangedHandle.Reset();
	BlueprintCompiledHandle.Reset();
	DestroyPreview();
	// The design canvas, taken down HERE rather than left to the scene's destructor.
	//
	// Both orders destroy it, but only this one is guaranteed to run while the world is still alive.
	// A widget whose world is collected first is cleaned up by UDreamWidget's last-resort path in
	// BeginDestroy instead, which says so at Error verbosity -- correct, and a real signal, but the
	// owner is right here and can simply do it.
	if (Scene.IsValid())
	{
		if (UDreamWidget* RootAgent = Scene->GetRootAgent())
		{
			RootAgent->DestroyWidget();
		}
	}
	// The scene owns the world and the design canvas; dropping it takes both.
	Scene.Reset();
	Blueprint = nullptr;
	HandlePool.Reset();
}

UWorld* FDreamWidgetPreviewHost::GetWorld() const
{
	return Scene.IsValid() ? Scene->GetWorld() : nullptr;
}

UDreamWidget* FDreamWidgetPreviewHost::GetRootAgent() const
{
	return Scene.IsValid() ? Scene->GetRootAgent() : nullptr;
}

UDreamWidget* FDreamWidgetPreviewHost::GetPreviewRoot() const
{
	return IsValid(PreviewWidget) ? PreviewWidget->GetContentRoot() : nullptr;
}

void FDreamWidgetPreviewHost::DestroyPreview()
{
	PreviewWidgetsByGuid.Reset();
	// Deliberately not IsValid(): an object marked for collection is still live memory whose widgets
	// are still registered, and skipping teardown on it is precisely how one gets orphaned.
	// DestroyWidget is written for that case -- it walks on RF_FinishDestroyed, not on IsValid.
	if (UDreamUserWidget* Preview = PreviewWidget)
	{
		if (!Preview->HasAnyFlags(RF_FinishDestroyed))
		{
			Preview->DestroyWidget();
		}
	}
	PreviewWidget = nullptr;
}

void FDreamWidgetPreviewHost::RebuildPreviewIfInvalidated()
{
	if (bPreviewInvalidated)
	{
		RebuildPreview();
	}
}

void FDreamWidgetPreviewHost::RebuildPreview()
{
	bPreviewInvalidated = false;

	// Checked BEFORE the teardown, not after. Once the asset is gone -- a closed editor, a collected
	// package -- the preview's world can already have been destroyed, and walking the hierarchy to
	// take it down then reads freed memory. There is also nothing left to rebuild from.
	if (!IsValid(Blueprint) || !Scene.IsValid() || Scene->GetWorld() == nullptr)
	{
		return;
	}

	DestroyPreview();

	UDreamWidget* RootAgent = Scene->EnsureRootAgent(
		Blueprint->DesignerData.CanvasSize,
		(EDreamRenderMode)Blueprint->DesignerData.CanvasRenderMode,
		Blueprint->DesignerData.DesignViewportSize);

	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (GeneratedClass == nullptr || !GeneratedClass->IsChildOf(UDreamUserWidget::StaticClass()) || RootAgent == nullptr)
	{
		RebuildPreviewGuidMap();
		OnPreviewRebuilt.Broadcast();
		return;
	}

	{
		// A class whose last compile failed is marked abstract, and NewObject refuses those. Showing
		// whatever the class last managed to build beats showing nothing while the author fixes it.
		FMakeClassSpawnableOnScope TemporarilySpawnable(GeneratedClass);
		// Not RF_Transactional: values written onto a preview must never enter the transaction buffer.
		// Only the template is undoable; a preview that recorded its own edits would let undo restore
		// an object the next rebuild is about to destroy. UMG clears the same flag for the same reason.
		PreviewWidget = NewObject<UDreamUserWidget>(RootAgent->GetOuter(), GeneratedClass, NAME_None, RF_Transient);
	}

	// The AUTHORING tree, not the class's archetype. This is what lets an added widget appear without
	// a recompile -- the class is a compile behind for as long as the author has not pressed the
	// button, and a preview built from it would show the hierarchy as it was, not as it is. Straight
	// out of UMG (CreateUserWidgetFromBlueprint: "so the preview can update without a full recompile").
	EnsureAuthoredGuids();
	PreviewWidget->InitializeFromArchetype(FindArchetypeForPreview());
	PreviewWidget->SetParentBeforeRegister(RootAgent);
	RegisterDreamWidgetHierarchy(PreviewWidget);

	RebuildPreviewGuidMap();
	// Tell the canvas it has something new to draw. Nothing else here does, and a canvas that is
	// never marked builds no draw calls at all -- the preview would be registered, laid out, and
	// invisible. It looked like it worked because compiling a Blueprint refreshes every canvas in
	// every world (UDreamUIManagerObject::OnBlueprintCompiled), which is true of the first open of a
	// freshly loaded asset and of pressing Compile, and false of every open after that.
	UDreamUIManagerWorldSubsystem::RefreshAllUI(Scene->GetWorld());

	OnPreviewRebuilt.Broadcast();
}

UDreamWidgetTree* FDreamWidgetPreviewHost::FindArchetypeForPreview() const
{
	if (!IsValid(Blueprint))
	{
		return nullptr;
	}
	if (IsValid(Blueprint->WidgetTree) && IsValid(Blueprint->WidgetTree->RootWidget))
	{
		return Blueprint->WidgetTree;
	}
	// Nothing authored here. A subclass that only adds logic inherits its parent's hierarchy, so the
	// preview has to as well -- otherwise subclassing a screen to change one function previews blank.
	// The walk deliberately starts at the SUPER class: this class's own archetype is a compile behind,
	// and using it would show whatever was there before the author emptied the tree.
	if (Blueprint->GeneratedClass != nullptr)
	{
		return UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(Blueprint->GeneratedClass->GetSuperClass());
	}
	return nullptr;
}

void FDreamWidgetPreviewHost::RebuildPreviewGuidMap()
{
	PreviewWidgetsByGuid.Reset();
	if (!IsValid(PreviewWidget) || !IsValid(PreviewWidget->GetWidgetTree()))
	{
		return;
	}
	// From the content root down to the nested boundary, NOT the tree's ForEachWidget. The tree walks
	// Children, and a nested instance hangs its own contents off itself as children -- so the tree
	// walk crosses into other assets. Those widgets have no counterpart in this Blueprint's authoring
	// tree and belong in nobody's map.
	TArray<UDreamWidget*> PreviewWidgets;
	CollectDreamWidgetsToNestedBoundary(PreviewWidget->GetContentRoot(), PreviewWidgets);
	for (UDreamWidget* Widget : PreviewWidgets)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		const FGuid& Guid = Widget->GetWidgetGuid();
		if (!Guid.IsValid())
		{
			// An unidentified widget is not a key. Adding it would file every unidentified widget in
			// the preview under the same empty guid and hand the details panel whichever one landed
			// last -- the exact failure the ids were introduced to end, rebuilt out of a default value.
			UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d Preview widget '%s' has no id; it will not pair with anything authored."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Widget->GetPathDisplayName());
			continue;
		}
		PreviewWidgetsByGuid.Add(Guid, Widget);
	}
}

void FDreamWidgetPreviewHost::EnsureAuthoredGuids()
{
	if (!IsValid(Blueprint) || !IsValid(Blueprint->WidgetTree))
	{
		return;
	}
	// Before the preview is instanced, never after: the preview copies whatever the authored widget
	// is holding, so minting here means both sides come out of the same value. Minting on the preview
	// instead would give it an id the authoring tree has never heard of.
	Blueprint->WidgetTree->ForEachWidget([](UDreamWidget* Widget)
	{
		if (IsValid(Widget))
		{
			Widget->EnsureWidgetGuid();
		}
	});
}

void FDreamWidgetPreviewHost::CompileBlueprint()
{
	if (!IsValid(Blueprint))
	{
		return;
	}
	// SkipGarbageCollection: a collection here would run while the old preview's objects are still
	// referenced by this host, and the compile is about to invalidate them anyway.
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection);
	InvalidatePreview();
}

FDreamWidgetReference FDreamWidgetPreviewHost::GetReferenceFromTemplate(UDreamWidget* InTemplateWidget)
{
	TSharedPtr<FDreamWidgetHandle> Handle = MakeShareable(new FDreamWidgetHandle(InTemplateWidget));
	HandlePool.Add(Handle);
	return FDreamWidgetReference(AsShared(), Handle);
}

FDreamWidgetReference FDreamWidgetPreviewHost::GetReferenceFromPreview(UDreamWidget* InPreviewWidget)
{
	if (UDreamWidget* Template = FindTemplateForPreview(InPreviewWidget))
	{
		return GetReferenceFromTemplate(Template);
	}
	return FDreamWidgetReference();
}

UDreamWidget* FDreamWidgetPreviewHost::FindPreviewForTemplate(const UDreamWidget* InTemplateWidget) const
{
	if (InTemplateWidget == nullptr)
	{
		return nullptr;
	}
	if (!InTemplateWidget->GetWidgetGuid().IsValid())
	{
		return nullptr;
	}
	const TWeakObjectPtr<UDreamWidget>* Found = PreviewWidgetsByGuid.Find(InTemplateWidget->GetWidgetGuid());
	return Found != nullptr ? Found->Get() : nullptr;
}

UDreamWidget* FDreamWidgetPreviewHost::FindTemplateForPreview(const UDreamWidget* InPreviewWidget) const
{
	if (InPreviewWidget == nullptr || !IsValid(Blueprint) || !IsValid(Blueprint->WidgetTree))
	{
		return nullptr;
	}
	const FGuid Guid = InPreviewWidget->GetWidgetGuid();
	if (!Guid.IsValid())
	{
		return nullptr;
	}
	// The authoring tree of THIS asset only. A nested widget blueprint's contents live in the same
	// preview hierarchy but carry their own asset's ids, so they find nothing here -- which is the
	// answer the designer wants: a nested instance is edited by opening the class it came from.
	UDreamWidget* Found = nullptr;
	Blueprint->WidgetTree->ForEachWidget([&Found, Guid](UDreamWidget* Widget)
	{
		if (Found == nullptr && Widget->GetWidgetGuid() == Guid)
		{
			Found = Widget;
		}
	});
	return Found;
}

bool FDreamWidgetPreviewHost::MigratePropertyToTemplate(UObject* InPreviewObject, FEditPropertyChain& InChain, bool bIsModify)
{
	if (!IsValid(InPreviewObject))
	{
		return false;
	}
	// Which object on the widget this is -- the widget, its visual, or a behaviour by position. The
	// same question bindings ask, answered in the same place so the two cannot disagree about it.
	const DreamWidgetPropertyBindingExtension::FBindingSite Site =
		DreamWidgetPropertyBindingExtension::ResolveBindingSite(InPreviewObject);
	if (Site.Widget == nullptr)
	{
		return false;
	}
	UDreamWidget* Template = FindTemplateForPreview(Site.Widget);
	if (Template == nullptr)
	{
		return false;
	}
	UObject* TemplateObject = ResolveDreamWidgetBindingTarget(Template, Site.Target, Site.BehaviourIndex);
	if (!IsValid(TemplateObject) || TemplateObject->GetClass() != InPreviewObject->GetClass())
	{
		return false;
	}

	FEditPropertyChain::TDoubleLinkedListNode* Head = InChain.GetHead();
	if (Head == nullptr)
	{
		return false;
	}
	// The chain has to belong to what it is being applied to. Without this an edit made while a
	// different object was selected reaches here and asserts inside ContainerPtrToValuePtr.
	const FProperty* HeadProperty = Head->GetValue();
	if (HeadProperty == nullptr || !InPreviewObject->GetClass()->IsChildOf(HeadProperty->GetOwnerClass()))
	{
		return false;
	}
	const bool bMigrated = DreamWidgetPreviewHostLocal::MigrateAlongChain(InPreviewObject, TemplateObject, Head, Head->GetValue(), bIsModify);
	if (bMigrated && !bIsModify)
	{
		// Modified, not structurally modified: no member changed, so there is nothing for the skeleton
		// to regenerate -- but the class archetype is a duplicate of this tree, and until the next full
		// compile every instance still carries the old value. This is what marks that gap.
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	return bMigrated;
}

int32 FDreamWidgetPreviewHost::CopyPreviewValuesToTemplate(UDreamWidget* InPreviewWidget, TConstArrayView<FName> InPropertyNames)
{
	UDreamWidget* Template = FindTemplateForPreview(InPreviewWidget);
	if (Template == nullptr || Template->GetClass() != InPreviewWidget->GetClass())
	{
		return 0;
	}
	Template->SetFlags(RF_Transactional);
	Template->Modify();

	int32 Copied = 0;
	for (const FName PropertyName : InPropertyNames)
	{
		FProperty* Property = InPreviewWidget->GetClass()->FindPropertyByName(PropertyName);
		if (Property == nullptr)
		{
			// A name that does not resolve is a rename nobody followed through, and it would
			// otherwise show up as a value that silently stops being saved.
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d '%s' is not a property of %s."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *PropertyName.ToString(), *InPreviewWidget->GetClass()->GetName());
			continue;
		}
		if (FObjectEditorUtils::MigratePropertyValue(InPreviewWidget, Property, Template, Property))
		{
			Copied++;
		}
	}
	return Copied;
}

void FDreamWidgetPreviewHost::OnBlueprintChanged(UBlueprint* InBlueprint)
{
	if (InBlueprint == Blueprint)
	{
		InvalidatePreview();
	}
}

void FDreamWidgetPreviewHost::OnBlueprintCompiled(UBlueprint* InBlueprint)
{
	if (InBlueprint == Blueprint)
	{
		InvalidatePreview();
	}
}

void FDreamWidgetPreviewHost::OnObjectsReplaced(const TMap<UObject*, UObject*>& InReplacementMap)
{
	// A recompile reinstances the preview like any other instance of the class: the original is
	// renamed aside and every reference to it is swapped for a property copy. Ours is swapped too --
	// a beat after this delegate -- and that is the whole problem, because the original is REGISTERED
	// in the preview world and the copy is not (bIsRegistered is not a UPROPERTY, so it does not come
	// across). Adopt the copy and the original is left live, registered, and unowned; it turns up much
	// later as UDreamWidget's last-resort cleanup, at Error verbosity, inside whatever happened to be
	// running when GC reached it.
	//
	// This delegate is the one moment the pointer still names the object that needs tearing down.
	// Take it down and leave the preview empty: the compile has already invalidated it, so the next
	// tick builds a real one rather than adopting a husk that never registered.
	for (const TPair<UObject*, UObject*>& Replacement : InReplacementMap)
	{
		if (Replacement.Key == PreviewWidget && Replacement.Key != Replacement.Value)
		{
			DestroyPreview();
			InvalidatePreview();
			break;
		}
	}

	// A template widget whose own class is a Blueprint gets replaced when that class recompiles. The
	// references handed out before then point at the dead object; repointing them here is what keeps
	// a selection alive across an unrelated asset's compile.
	for (int32 Index = HandlePool.Num() - 1; Index >= 0; Index--)
	{
		TSharedPtr<FDreamWidgetHandle> Handle = HandlePool[Index].Pin();
		if (!Handle.IsValid())
		{
			HandlePool.RemoveAtSwap(Index);
			continue;
		}
		if (UObject* const* Replacement = InReplacementMap.Find(Handle->Widget.Get()))
		{
			Handle->Widget = Cast<UDreamWidget>(*Replacement);
		}
	}
}

void FDreamWidgetPreviewHost::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Blueprint);
	Collector.AddReferencedObject(PreviewWidget);
	if (Scene.IsValid())
	{
		Scene->AddReferencedObjects(Collector);
	}
}

#undef LOCTEXT_NAMESPACE
