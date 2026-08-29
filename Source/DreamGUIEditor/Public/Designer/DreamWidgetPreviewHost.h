// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Designer/DreamWidgetReference.h"

class FDreamUIPrefabInstanceScene;
class UDreamUserWidget;
class UDreamWidget;
class UDreamWidgetBlueprint;
class UDreamWidgetTree;
class UWorld;
class FEditPropertyChain;
class UBlueprint;
struct FPropertyChangedEvent;

/**
 * The live half of authoring one UDreamWidgetBlueprint: a world, a design canvas, an instance of the
 * compiled class hanging under it, and the correspondence between that instance and the template.
 *
 * This exists apart from any editor toolkit on purpose. Everything here is about data -- what the
 * preview is, when it is stale, which preview widget answers for which template -- and it is all
 * testable without a window. The designer surface, the panels and the modes are built on top of it.
 *
 * ## Why a preview at all
 *
 * The prefab editor had no such split: it opened a prefab by DESERIALIZING it into the editor world
 * and then edited that live hierarchy, applying it back to the asset on demand. That is what made
 * "Apply" a concept, and what made an editor holding an out-of-date copy a recurring failure. Under
 * the class model the asset holds an ordinary object graph, and the graph is inert -- template
 * widgets are outered to the Blueprint, have no world, are never registered, and cannot draw or lay
 * out. So the drawn thing has to be a separate instance, and the two have to be kept in
 * correspondence rather than being the same objects.
 *
 * ## Which half to edit
 *
 * Structure (create, delete, reparent, reorder) is edited on the TEMPLATE, and the preview is
 * rebuilt from it -- see DreamWidgetTreeEditing. Values (properties, anchors, geometry) are edited
 * on the PREVIEW, where layout actually runs, and mirrored back with MigratePropertyToTemplate.
 * Both directions are UMG's, and for the same reasons.
 *
 * ## What ties them together
 *
 * Object FName. The preview's contents are instanced from the authoring tree itself, and
 * FObjectInstancingGraph preserves the archetype's names, so a template widget and its preview
 * counterpart share one. Display names cannot serve here: they are what a designer types, they are
 * not unique, and they change while the correspondence has to hold.
 *
 * Must be held in a shared pointer -- it hands out references that point back at it.
 */
class DREAMGUIEDITOR_API FDreamWidgetPreviewHost
	: public FGCObject
	, public TSharedFromThis<FDreamWidgetPreviewHost>
{
public:
	FDreamWidgetPreviewHost();
	virtual ~FDreamWidgetPreviewHost() override;

	/** Bind to an asset and build the first preview. Call once. */
	void Initialize(UDreamWidgetBlueprint* InBlueprint);
	/** Drop the preview and the world. Safe to call twice, and called for you on destruction. */
	void Shutdown();

	UDreamWidgetBlueprint* GetBlueprint() const { return Blueprint; }
	UWorld* GetWorld() const;
	FDreamUIPrefabInstanceScene* GetScene() const { return Scene.Get(); }

	/** The design canvas everything hangs under. Not part of the hierarchy and never serialized. */
	UDreamWidget* GetRootAgent() const;
	/** The instance of the compiled class. Null before the first successful build. */
	UDreamUserWidget* GetPreviewWidget() const { return PreviewWidget; }
	/** Root of the previewed CONTENTS -- the counterpart of the template tree's root, not the user widget. */
	UDreamWidget* GetPreviewRoot() const;

	/**
	 * Mark the preview stale. The rebuild happens on the next RebuildPreviewIfInvalidated, not here,
	 * so a burst of edits inside one operation costs one rebuild rather than one each.
	 */
	void InvalidatePreview() { bPreviewInvalidated = true; }
	bool IsPreviewInvalidated() const { return bPreviewInvalidated; }
	/** Rebuild if stale. Cheap when it is not; call it from a tick. */
	void RebuildPreviewIfInvalidated();
	/** Destroy the current preview and build a fresh one from the class. */
	void RebuildPreview();

	/** Compile the Blueprint, which is what makes an authoring edit reach the class the preview is built from. */
	void CompileBlueprint();

	FDreamWidgetReference GetReferenceFromTemplate(UDreamWidget* InTemplateWidget);
	/** The reference whose template answers for this preview widget; invalid when it has none. */
	FDreamWidgetReference GetReferenceFromPreview(UDreamWidget* InPreviewWidget);

	/** The preview counterpart of a template widget, or null when the preview does not have one. */
	UDreamWidget* FindPreviewForTemplate(const UDreamWidget* InTemplateWidget) const;
	/** The template counterpart of a preview widget, or null when it is not part of the authored tree. */
	UDreamWidget* FindTemplateForPreview(const UDreamWidget* InPreviewWidget) const;

	/**
	 * Copy one edited value from a preview object onto its template counterpart.
	 *
	 * InChain is the details panel's property chain, which is what carries the path through instanced
	 * sub-objects -- a PanelSlot padding, a Visual's colour -- rather than just the leaf. bIsModify is
	 * the pre-change pass: it only snapshots the destination for undo and writes nothing.
	 *
	 * Returns false when the widget has no template counterpart, which is the normal answer for
	 * anything in the preview world that is not part of the authored hierarchy (the design canvas).
	 *
	 * A successful write also marks the Blueprint modified, because the class archetype is a duplicate
	 * of the template and every instance keeps the old value until the next full compile. Callers must
	 * therefore NOT call this for EPropertyChangeType::Interactive -- a drag would pay for that on
	 * every mouse move. UMG's details view skips the whole migration for interactive changes and
	 * mirrors once at the end; do the same.
	 */
	/**
	 * Copy one details-panel edit from a PREVIEW object onto its counterpart in the authoring tree.
	 *
	 * InPreviewObject is whatever the panel is showing -- the widget, its visual, or one of its
	 * behaviours. Taking it as a widget was wrong twice over: the property does not belong to
	 * UDreamWidget, so applying it asserts, and the edit never reached the asset either.
	 */
	bool MigratePropertyToTemplate(UObject* InPreviewObject, FEditPropertyChain& InChain, bool bIsModify);

	/**
	 * Copy named properties straight from a preview widget onto its template counterpart.
	 *
	 * The chain-walking migration above is what a details panel needs, because an edit there can be
	 * buried inside an instanced sub-object. A designer gesture is not like that: it moves a widget,
	 * and what it moved is a handful of known properties on the widget itself. This is that case,
	 * and having it saves every gesture from having to fabricate an FEditPropertyChain.
	 *
	 * Returns how many were copied. Zero means the widget has no template -- the design canvas, or
	 * anything else in the preview world that is not part of the authored hierarchy.
	 */
	int32 CopyPreviewValuesToTemplate(UDreamWidget* InPreviewWidget, TConstArrayView<FName> InPropertyNames);

	/** Fires after every rebuild, once the new preview and the name map are both in place. */
	FSimpleMulticastDelegate OnPreviewRebuilt;

	// FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FDreamWidgetPreviewHost"); }
	// End FGCObject

private:
	void DestroyPreview();
	/**
	 * The tree the preview is instanced from: the Blueprint's own authoring tree, or -- when this asset
	 * authors none -- the nearest ancestor class that does.
	 */
	UDreamWidgetTree* FindArchetypeForPreview() const;
	/** Rebuild the id -> preview widget map from the current preview. */
	void RebuildPreviewGuidMap();
	/** Mint an id for any authored widget that has none, before the preview is instanced from it. */
	void EnsureAuthoredGuids();
	/** Repoint the handle pool after the editor replaced objects (a widget's own class recompiling). */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& InReplacementMap);
	/** The Blueprint changed shape (a structural edit, or an undo). The preview is a compile behind. */
	void OnBlueprintChanged(UBlueprint* InBlueprint);
	void OnBlueprintCompiled(UBlueprint* InBlueprint);

	TObjectPtr<UDreamWidgetBlueprint> Blueprint = nullptr;
	TObjectPtr<UDreamUserWidget> PreviewWidget = nullptr;
	TUniquePtr<FDreamUIPrefabInstanceScene> Scene;

	/**
	 * Preview widgets by UDreamWidget::GetWidgetGuid. Rebuilt wholesale; never patched.
	 *
	 * Keyed on the id rather than the object FName, which is what this used to be. Object names are
	 * generated per process and every loaded asset brings its own numbering back starting at zero, so
	 * a nested widget blueprint's DreamWidget_0 collided with the host's own DreamWidget_0 -- and a
	 * TMap resolves that by overwriting. Editing a value inside a nested Button wrote it onto an
	 * unrelated host widget. See UDreamWidget::GetWidgetGuid.
	 */
	TMap<FGuid, TWeakObjectPtr<UDreamWidget>> PreviewWidgetsByGuid;

	TArray<TWeakPtr<FDreamWidgetHandle>> HandlePool;

	FDelegateHandle ObjectsReplacedHandle;
	FDelegateHandle BlueprintChangedHandle;
	FDelegateHandle BlueprintCompiledHandle;
	bool bPreviewInvalidated = false;
};
