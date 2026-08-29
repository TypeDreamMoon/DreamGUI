// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "UObject/LazyObjectPtr.h"
#include "DreamWidgetAnimationObjectReference.generated.h"

class UDreamWidget;

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

#if WITH_EDITOR
	bool FixObjectReferenceFromEditorHelpers(UDreamWidget* InContextWidget);
	bool CanFixObjectReferenceFromEditorHelpers()const;
	bool IsObjectReferenceGood(UDreamWidget* InContextWidget)const;
	bool IsEditorHelpersGood(UDreamWidget* InContextWidget)const;
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

	/** for direct reference widget. */
	UPROPERTY()
		TObjectPtr<UDreamWidget> HelperWidget = nullptr;
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
