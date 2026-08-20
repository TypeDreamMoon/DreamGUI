// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "UObject/LazyObjectPtr.h"
#include "DreamUIPrefabSequenceObjectReference.generated.h"

class UDreamWidget;

/**
 * An external reference to a level sequence object, resolvable through an arbitrary context.
 */
USTRUCT()
struct DREAMGUI_API FDreamUIPrefabSequenceObjectReference
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	static FString GetWidgetPathRelativeToContextWidget(UDreamWidget* InContextWidget, UDreamWidget* InWidget);
	static UDreamWidget* GetWidgetFromContextWidgetByRelativePath(UDreamWidget* InContextWidget, const FString& InPath);
	bool FixObjectReferenceFromEditorHelpers(UDreamWidget* InContextWidget);
	bool CanFixObjectReferenceFromEditorHelpers()const;
	bool IsObjectReferenceGood(UDreamWidget* InContextWidget)const;
	bool IsEditorHelpersGood(UDreamWidget* InContextWidget)const;
#endif
	static bool CreateForObject(UDreamWidget* InContextWidget, UObject* InObject, FDreamUIPrefabSequenceObjectReference& OutResult);

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
	friend bool operator==(const FDreamUIPrefabSequenceObjectReference& A, const FDreamUIPrefabSequenceObjectReference& B)
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

#if WITH_EDITORONLY_DATA
	/** HelperWidget's path relative to context widget, split by '/'. If only '/' means it is the context widget itself. could use this to replace reference object in editor/ */
	UPROPERTY()
		FString HelperWidgetPath;
#endif
};

USTRUCT()
struct FDreamUIPrefabSequenceObjectReferences
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FDreamUIPrefabSequenceObjectReference> Array;
};

USTRUCT()
struct FDreamUIPrefabSequenceObjectReferenceMap
{
	GENERATED_BODY()

	/**
	 * Check whether this map has a binding for the specified object id
	 * @return true if this map contains a binding for the id, false otherwise
	 */
	bool HasBinding(const FGuid& ObjectId) const;

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
	void CreateBinding(const FGuid& ObjectId, const FDreamUIPrefabSequenceObjectReference& ObjectReference);

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
	TArray<FDreamUIPrefabSequenceObjectReferences> References;
};
