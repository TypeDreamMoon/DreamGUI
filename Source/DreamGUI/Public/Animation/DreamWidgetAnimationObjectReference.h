// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "UObject/LazyObjectPtr.h"
#include "Templates/Tuple.h"
#include "DreamWidgetAnimationObjectReference.generated.h"

class UDreamWidget;
class UDreamWidgetTree;

/**
 * An external reference to a level sequence object, resolvable through an arbitrary context.
 */
USTRUCT()
struct DREAMGUI_API FDreamWidgetAnimationObjectReference
{
	GENERATED_BODY()

public:

	/**
	 * The chain of display names from the widget that owns the animation down to the bound one.
	 *
	 * Not editor-only any more, and not a helper: under the class model this IS the binding. A stored
	 * pointer names one particular widget, which is right for a prefab (one tree) and wrong for a
	 * class (one tree per instance) -- every instance's animation would drive the authoring tree's
	 * widgets, resolving perfectly well the entire time.
	 */
	static FString GetWidgetPathRelativeToContextWidget(UDreamWidget* InContextWidget, UDreamWidget* InWidget);
	static UDreamWidget* GetWidgetFromContextWidgetByRelativePath(UDreamWidget* InContextWidget, const FString& InPath);

	/** Resolve against THIS context's tree rather than through the stored pointer. Null if it cannot. */
	UObject* ResolveInContext(UDreamWidget* InContextWidget) const;

	/**
	 * Drop a cached pointer that names a widget in a tree other than InOwnTree. The path is untouched.
	 *
	 * A pointer from another tree is never right for this reference and cannot become right: the tree
	 * it names is somebody else's, and holding it keeps that whole tree alive. A pointer from
	 * InOwnTree is never dropped, however stale the path beside it may be -- in the authoring tree a
	 * good pointer under a renamed path is the ordinary shape of a rename, and FixEditorHelpers
	 * repairs the path FROM the pointer.
	 *
	 * @return true when a pointer was dropped.
	 */
	bool DetachHelperOutsideTree(const UDreamWidgetTree* InOwnTree) const;
	/**
	 * Re-point the cached pointer at the widget InContextWidget's own tree holds for this path.
	 *
	 * @param OutResolvedByPath set to true when the path named a widget in that tree; false means the
	 *        path resolved to nothing there, which is a broken binding worth reporting.
	 * @return true when the stored pointer changed.
	 */
	bool RebindHelperToContext(UDreamWidget* InContextWidget, bool& OutResolvedByPath) const;

	/**
	 * The stored path, verbatim, so a caller that found this reference unresolvable can say what it
	 * was looking for. Empty means no path was ever recorded, which resolves in no context at all.
	 */
	const FString& GetHelperWidgetPath() const { return HelperWidgetPath; }

#if WITH_EDITOR
	bool FixObjectReferenceFromEditorHelpers(UDreamWidget* InContextWidget);
	bool CanFixObjectReferenceFromEditorHelpers()const;
	bool IsObjectReferenceGood(UDreamWidget* InContextWidget)const;
	bool IsEditorHelpersGood(UDreamWidget* InContextWidget)const;
	/**
	 * Rename one '/'-separated step of the stored path, for a widget that moved name rather than away.
	 *
	 * This is the repair half of GetUnresolvableBindingPaths: that one reports a path this context
	 * cannot walk, and when the reason is a rename -- not a deletion -- the path is still correct
	 * about everything except one word. Nothing else on the reference needs touching. The stored
	 * pointer already points at the renamed object (renaming a widget does not replace it), and
	 * ObjectPathRelativeToWidget is built from OBJECT names, which a display-name rename never
	 * touches, so the path is the only stale field there is.
	 *
	 * Segment equality is case-insensitive, matching the id namespace it comes from: a .dui refuses
	 * two ids that differ only in case (they would collide as FName member variables anyway), so two
	 * segments that compare equal here cannot both be real widgets of one hierarchy.
	 *
	 * The "/" path -- the context widget itself -- is never rewritten, and correctly: it names no
	 * widget by name, so renaming the context widget leaves every path it owns already right.
	 *
	 * @return true when this reference's path changed, so a caller knows whether to mark anything.
	 */
	bool RenameWidgetPathSegment(const FString& InOldSegment, const FString& InNewSegment);
#endif
	static bool CreateForObject(UDreamWidget* InContextWidget, UObject* InObject, FDreamWidgetAnimationObjectReference& OutResult);

	bool InitHelpers(UDreamWidget* InContextWidget);
	bool CheckTargetObject()const;
	/**
	 * Check whether this object reference is valid or not
	 */
	bool IsValidReference() const
	{
		return CheckTargetObject();
	}

	/**
	 * Resolve this reference from the specified source object
	 *
	 * @return The object
	 */
	UObject* Resolve() const;

	/**
	 * Equality comparator
	 */
	friend bool operator==(const FDreamWidgetAnimationObjectReference& A, const FDreamWidgetAnimationObjectReference& B)
	{
		return A.Resolve() == B.Resolve();
	}

private:

	UPROPERTY(Transient)
	mutable TObjectPtr<UObject> Object = nullptr;

	/**
	 * for direct reference widget.
	 *
	 * MUTABLE, like Object above and for the same reason: under the class model this pointer is a
	 * CACHE over HelperWidgetPath, not the binding. An instanced animation is handed the class
	 * template's copy of it verbatim -- FObjectInstancingGraph re-points only Instanced properties and
	 * this is a plain one -- so it arrives naming a widget in the authoring tree, which resolves
	 * perfectly and animates the wrong tree while holding it alive. Correcting that is a re-point, so
	 * the const resolvers have to be able to do it.
	 */
	UPROPERTY()
		mutable TObjectPtr<UDreamWidget> HelperWidget = nullptr;
	/** object path relative to owner widget
	 * if path is empty then means widget self
	 */
	UPROPERTY()
	FString ObjectPathRelativeToWidget;

	/** HelperWidget's path relative to context widget, split by '/'. If only '/' means it is the context widget itself. */
	UPROPERTY()
	FString HelperWidgetPath;
};

USTRUCT()
struct FDreamWidgetAnimationObjectReferences
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FDreamWidgetAnimationObjectReference> Array;
};

USTRUCT()
struct FDreamWidgetAnimationObjectReferenceMap
{
	GENERATED_BODY()

	/**
	 * Check whether this map has a binding for the specified object id
	 * @return true if this map contains a binding for the id, false otherwise
	 */
	bool HasBinding(const FGuid& ObjectId) const;

	/** Resolve every reference for this id against InContextWidget's tree. Empty if none resolve there. */
	void ResolveBindingInContext(const FGuid& ObjectId, UDreamWidget* InContextWidget, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const;

	/**
	 * Drop every cached pointer naming a widget outside InOwnTree, keeping every path.
	 * @return how many were dropped.
	 */
	int32 DetachHelpersOutsideTree(const UDreamWidgetTree* InOwnTree) const;
	/**
	 * Re-point every cached pointer at InContextWidget's own tree, by path.
	 *
	 * @param OutUnresolvedPaths receives the recorded path of every reference this context could not
	 *        walk -- a binding that will find nothing at playback, so worth saying out loud.
	 * @return how many pointers changed.
	 */
	int32 RebindHelpersToContext(UDreamWidget* InContextWidget, TArray<FString>& OutUnresolvedPaths) const;

	/**
	 * Remove a binding for the specified ID
	 *
	 * @param ObjectId	The ID to remove
	 */
	void RemoveBinding(const FGuid& ObjectId);

	/**
	 * Create a binding for the specified ID
	 *
	 * @param ObjectId				The ID to associate the component with
	 * @param ObjectReference	The component reference to bind
	 */
	void CreateBinding(const FGuid& ObjectId, const FDreamWidgetAnimationObjectReference& ObjectReference);

	/**
	 * Resolve a binding for the specified ID using a given context
	 *
	 * @param ObjectId		The ID to associate the object with
	 * @param OutObjects	Container to populate with bound components
	 */
	void ResolveBinding(const FGuid& ObjectId, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const;

#if WITH_EDITOR
	bool IsObjectReferencesGood(UDreamWidget* InContextWidget)const;
	void GetInvalidBindingIds(UDreamWidget* InContextWidget, TArray<FGuid>& OutBindingIds) const;
	/**
	 * Every binding this context cannot walk to, paired with the path it holds.
	 *
	 * A different question from GetInvalidBindingIds, and the difference is the whole reason this
	 * exists: that one asks whether the stored POINTER still lands on a widget under the context, and
	 * a rename leaves the pointer perfectly good while only the path goes stale. So the pointer-based
	 * check answers "fine" for the one case playback cannot survive -- ResolveInContext is what an
	 * instance actually calls, so it is what gets asked here.
	 */
	void GetUnresolvableBindingPaths(UDreamWidget* InContextWidget, TArray<TPair<FGuid, FString>>& OutBindings) const;
	/**
	 * Rename one path step across every reference in this map. Returns how many references changed.
	 *
	 * Deliberately takes no context widget, unlike everything else in this block. A rename fixup runs
	 * while the hierarchy is being rebuilt from its source of truth, at which point the tree these
	 * paths describe may not exist yet in either its old or its new shape -- so a repair that first
	 * had to resolve the path against a live tree could only run when the damage was already done.
	 * The path is text, the edit is textual, and that is what makes it orderable.
	 */
	int32 RenameWidgetPathSegment(const FString& InOldSegment, const FString& InNewSegment);
	bool HasBindingCountMismatch() const { return BindingIds.Num() != References.Num(); }
	bool IsEditorHelpersGood(UDreamWidget* InContextWidget)const;
	//return true if anything changed
	bool FixObjectReferences(UDreamWidget* InContextWidget);
	//return true if anything changed
	bool FixEditorHelpers(UDreamWidget* InContextWidget);
#endif
private:
	
	UPROPERTY()
	TArray<FGuid> BindingIds;

	UPROPERTY()
	TArray<FDreamWidgetAnimationObjectReferences> References;
};
