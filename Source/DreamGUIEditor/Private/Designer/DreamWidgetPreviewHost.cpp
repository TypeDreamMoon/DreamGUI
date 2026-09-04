// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamWidgetPreviewHost.h"

#include "DreamWidgetBlueprint.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamWidgetSubObjectBehaviour.h"
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
#include "UObject/UObjectHash.h"

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

	/**
	 * A widget's sub-objects that no binding can name.
	 *
	 * ResolveBindingSite answers for the widget, its visual and its behaviours, because those are the
	 * three a `<-` can target. It is the right answer to the question BINDINGS ask. The mirror asks a
	 * different question that happens to have the same answer three times out of five -- "given this
	 * object in the preview, which object is it on the template?" -- and by reusing that call it
	 * inherited the two cases bindings do not have.
	 *
	 * Those two are the panel slot and the layouts, and between them they own most of what a designer
	 * actually drags: Padding, both alignments, FillWeight, the grid coordinates, every spacing and
	 * every layout rule. All of them moved the preview and none of them reached the asset -- and since
	 * the .dui is written by comparing the file against the ASSET, none of them reached the file
	 * either. It read as a write-back that did not fire.
	 */
	enum class ESubObjectSite : uint8
	{
		None,
		PanelSlot,
		LayoutContainer,
		LayoutSelf,
	};

	/** Which sub-object of which widget InObject is, by identity rather than by type. */
	ESubObjectSite ResolveSubObjectSite(const UObject* InObject, UDreamWidget*& OutOwner)
	{
		OutOwner = nullptr;
		const UDreamWidgetSubObjectBehaviour* SubObject = Cast<UDreamWidgetSubObjectBehaviour>(InObject);
		if (SubObject == nullptr)
		{
			return ESubObjectSite::None;
		}
		UDreamWidget* Owner = SubObject->GetWidget();
		if (!IsValid(Owner))
		{
			return ESubObjectSite::None;
		}
		// Compared by pointer, the same way ResolveBindingSite insists on the widget's OWN visual: an
		// object that merely has this outer but is no longer the one the widget uses would otherwise
		// have its edits written onto whichever object replaced it.
		OutOwner = Owner;
		if (Owner->GetPanelSlot() == InObject)
		{
			return ESubObjectSite::PanelSlot;
		}
		if (Owner->GetLayoutContainer() == InObject)
		{
			return ESubObjectSite::LayoutContainer;
		}
		if (Owner->GetLayoutSelf() == InObject)
		{
			return ESubObjectSite::LayoutSelf;
		}
		OutOwner = nullptr;
		return ESubObjectSite::None;
	}

	/**
	 * The same sub-object on another widget, minting a panel slot if that is what is missing.
	 *
	 * Minting is right for the slot and wrong for the layouts. A panel slot is not authored: it is
	 * per-child data the PARENT's layout hands out, created at registration by EnsurePanelSlotForChild
	 * -- which an authoring tree never reaches, so a template routinely has none until something writes
	 * to it. Refusing here would mean the first padding a designer sets is dropped and every one after
	 * it works, which is worse than either alternative.
	 *
	 * A layout is authored: `+ VerticalBox {}` in the text, or the Layout picker in the panel. If the
	 * template has none and the preview does, the two have diverged structurally, and quietly creating
	 * one here would write the divergence into the asset instead of reporting it.
	 */
	UObject* ResolveSubObjectOn(UDreamWidget* InWidget, ESubObjectSite InSite, UClass* InExpectedClass)
	{
		if (!IsValid(InWidget))
		{
			return nullptr;
		}
		switch (InSite)
		{
		case ESubObjectSite::PanelSlot:
		{
			UDreamPanelSlot* Slot = InWidget->GetPanelSlot();
			if (!IsValid(Slot) && InExpectedClass != nullptr
				&& InExpectedClass->IsChildOf(UDreamPanelSlot::StaticClass()))
			{
				InWidget->SetFlags(RF_Transactional);
				InWidget->Modify();
				// The preview's class, not the default: the parent's layout chose it, and both widgets
				// have the same parent.
				Slot = InWidget->CreateNewPanelSlot(TSubclassOf<UDreamPanelSlot>(InExpectedClass));
			}
			return Slot;
		}
		case ESubObjectSite::LayoutContainer:
			return InWidget->GetLayoutContainer();
		case ESubObjectSite::LayoutSelf:
			return InWidget->GetLayoutSelf();
		default:
			return nullptr;
		}
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

	// Last moment at which the outgoing preview is still addressable. See OnPreviewAboutToRebuild.
	OnPreviewAboutToRebuild.Broadcast();

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
		// RF_Transient and, deliberately, NOT RF_Transactional -- and this is not a statement about
		// this object alone. UDreamWidgetGeneratedClass::InitializeWidgetStatic instances the tree
		// with the HOST's RF_Transactional, and that flag propagates to sub-objects, so these flags
		// are what keeps the entire preview hierarchy out of the transaction buffer.
		//
		// That is the whole undo model: the authoring tree is the only undoable half, and the preview
		// is a projection of it that is thrown away and rebuilt. A preview that recorded its own
		// edits would let an undo restore objects a rebuild had already destroyed -- which is exactly
		// what produced a preview tree with a cycle in it -- and would pin the outgoing hierarchy in
		// the buffer for as long as the entry lived. UMG clears the same flag for the same reason,
		// and rebuilds from the template on PostUndo/PostRedo as this editor now does; see
		// FDreamWidgetBlueprintEditor::HandlePostTransaction.
		PreviewWidget = NewObject<UDreamUserWidget>(RootAgent->GetOuter(), GeneratedClass, NAME_None, RF_Transient);
	}

	// The AUTHORING tree, not the class's archetype. This is what lets an added widget appear without
	// a recompile -- the class is a compile behind for as long as the author has not pressed the
	// button, and a preview built from it would show the hierarchy as it was, not as it is. Straight
	// out of UMG (CreateUserWidgetFromBlueprint: "so the preview can update without a full recompile").
	EnsureAuthoredGuids();
	PreviewWidget->InitializeFromArchetype(FindArchetypeForPreview());
	PreviewWidget->SetParentBeforeRegister(RootAgent);

	// Fill the design canvas. A freshly constructed widget is 100x100 centred, and this one is not a
	// widget the author placed -- it is the instance the class's contents hang inside, so any size of
	// its own is a second, invisible frame that the authored root then fills instead of the canvas.
	// The symptom is a screen laid out correctly and clipped to a 100x100 square in the middle.
	//
	// After parenting on purpose: anchors are resolved against a parent, and setting them on an
	// orphan is refused (UDreamWidget::SetHorizontalAndVerticalAnchorMinMax says so out loud).
	{
		FDreamUIAnchorData FillParent;
		FillParent.Pivot = FVector2D(0.5, 0.5);
		FillParent.AnchorMin = FVector2D(0.0, 0.0);
		FillParent.AnchorMax = FVector2D(1.0, 1.0);
		FillParent.AnchoredPosition = FVector2D::ZeroVector;
		FillParent.SizeDelta = FVector2D::ZeroVector;
		PreviewWidget->SetAnchorData(FillParent);
	}

	// And ARRANGE what is inside it, which is the other half of that same paragraph and was missing.
	//
	// This is UMG's PreviewSizeConstraint (SDesignerView.cpp: an SBox with WidthOverride /
	// HeightOverride around UserWidget->TakeWidget()). In UMG the asset's root cannot be the wrong
	// size because it has no size of its own to author -- a Slate tree root has no slot, so what it
	// gets is whatever that SBox hands it. Here every widget carries an anchor rect, the ROOT
	// included, and the wrapper above had no layout container -- so it handed its child nothing and
	// the authored root fell back on anchors nothing validates. A root authored as point anchors with
	// a zero SizeDelta then measured 0x0 and arranged every descendant into a 0x0 rect: a hierarchy
	// that is structurally perfect and entirely invisible, reported as "every control in here has
	// size zero". (The 100x100 case above is the same defect one level up, found first.)
	//
	// An overlay rather than a size box: the box takes one child and this wrapper legitimately holds
	// exactly one, but a refused second child during a rebuild would be a lost preview rather than a
	// tidier one -- and Fill on both axes, which is the default slot, is all the constraint that is
	// wanted here. The AUTHORED anchors are untouched and still what runs at runtime; the designer is
	// simulating the host that would arrange this widget, and which host it simulates is the
	// designer's size rule (see FDreamWidgetBlueprintEditor::SetDesignerSizeRule).
	{
		UDreamLayoutContainer* Previous = PreviewWidget->GetLayoutContainer();
		UDreamLayoutContainer* Stage = PreviewWidget->CreateNewLayoutContainer<UDreamLayoutContainerOverlay>();
		PreviewWidget->SyncRequiredBehavioursForLayoutContainer(Previous, Stage);
	}

	RegisterDreamWidgetHierarchy(PreviewWidget);

	// After registration, because registration is where the second kind of preview object is born.
	//
	// Instancing carries the flags of the tree it built (see the NewObject above), and UDreamWidget's
	// own creators -- AddComponent, CreateNewVisual, CreateNewLayoutContainer, CreateNewLayoutSelf,
	// CreateNewPanelSlot -- now take RF_Transactional from their owner, so the objects registration
	// mints are non-transactional too. That covers the case this was written for: a panel slot is
	// per-child data the PARENT's layout hands out, minted by EnsurePanelSlotForChild for any child
	// whose authored counterpart has none, which the content root always is.
	//
	// A BACKSTOP, then, rather than the mechanism -- and it stays because the rule is not universal.
	// Sites that still hard-code the flag and can put an object inside this preview: DreamUIBuilder's
	// tree for a native control that declares its hierarchy in code, UDreamLayout's animation handler,
	// UDreamWidgetAnimationComponent's sequences, and anything CreateDreamWidget makes (a list view's
	// cells). One sweep per rebuild is cheap insurance against a rule that holds in five places and
	// not in a sixth; what it cannot reach is an object created AFTER the rebuild.
	ClearTransactionalFlagsOnPreview();

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

void FDreamWidgetPreviewHost::ClearTransactionalFlagsOnPreview()
{
	if (!IsValid(PreviewWidget))
	{
		return;
	}
	PreviewWidget->ClearFlags(RF_Transactional);
	// Nested, because the objects this exists for are two and three levels down: the tree, the
	// widgets outered flat to it, and each widget's slot, layouts, visual and behaviours.
	//
	// Rooted at the preview widget and not at the world. The design canvas is a sibling, not a
	// descendant, and it is transactional ON PURPOSE -- it survives every rebuild, so undoing a
	// screen-size change has something real to restore. See
	// FDreamWidgetBlueprintEditor::ApplyDesignerViewportSize.
	ForEachObjectWithOuter(PreviewWidget.Get(), [](UObject* Object)
	{
		Object->ClearFlags(RF_Transactional);
	});
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
	// Which object on the widget this is. Two questions, asked in this order because the second is
	// the narrower one: bindings name the widget, its visual and its behaviours, and everything else
	// a details panel edits is a sub-object identified by position on its owner.
	UObject* TemplateObject = nullptr;
	UDreamWidget* SubObjectOwner = nullptr;
	const DreamWidgetPreviewHostLocal::ESubObjectSite SubSite =
		DreamWidgetPreviewHostLocal::ResolveSubObjectSite(InPreviewObject, SubObjectOwner);
	if (SubSite != DreamWidgetPreviewHostLocal::ESubObjectSite::None)
	{
		TemplateObject = DreamWidgetPreviewHostLocal::ResolveSubObjectOn(
			FindTemplateForPreview(SubObjectOwner), SubSite, InPreviewObject->GetClass());
	}
	else
	{
		// The same question bindings ask, answered in the same place so the two cannot disagree.
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
		TemplateObject = ResolveDreamWidgetBindingTarget(Template, Site.Target, Site.BehaviourIndex);
	}
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
		bTemplateDirty = true;
	}
	return bMigrated;
}

void FDreamWidgetPreviewHost::FlushTemplateChanges()
{
	// Guarded rather than broadcasting unconditionally, so a flush point is free to fire on every
	// gesture end, every committed edit and every focus change without any of them costing a write.
	if (!bTemplateDirty)
	{
		return;
	}
	bTemplateDirty = false;
	OnTemplateChanged.Broadcast();
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
	// A drag calls this on every mouse move, so this marks far more often than anything should act
	// on it. That is the point: the flag absorbs the rate, and FlushTemplateChanges decides when a
	// write is worth doing. See IsTemplateDirty.
	if (Copied > 0)
	{
		bTemplateDirty = true;
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
