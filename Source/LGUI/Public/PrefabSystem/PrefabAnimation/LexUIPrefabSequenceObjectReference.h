// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "UObject/LazyObjectPtr.h"
#include "LexUIPrefabSequenceObjectReference.generated.h"

class ULexWidget;

/**
 * An external reference to a level sequence object, resolvable through an arbitrary context.
 */
USTRUCT()
struct LGUI_API FLexUIPrefabSequenceObjectReference
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	static FString GetWidgetPathRelativeToContextWidget(ULexWidget* InContextWidget, ULexWidget* InWidget);
	static ULexWidget* GetWidgetFromContextWidgetByRelativePath(ULexWidget* InContextWidget, const FString& InPath);
	bool FixObjectReferenceFromEditorHelpers(ULexWidget* InContextWidget);
	bool CanFixObjectReferenceFromEditorHelpers()const;
	bool IsObjectReferenceGood(ULexWidget* InContextWidget)const;
	bool IsEditorHelpersGood(ULexWidget* InContextWidget)const;
#endif
	static bool CreateForObject(ULexWidget* InContextWidget, UObject* InObject, FLexUIPrefabSequenceObjectReference& OutResult);

	bool InitHelpers(ULexWidget* InContextWidget);
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
	friend bool operator==(const FLexUIPrefabSequenceObjectReference& A, const FLexUIPrefabSequenceObjectReference& B)
	{
		return A.Resolve() == B.Resolve();
	}

private:

	UPROPERTY(Transient)
	mutable TObjectPtr<UObject> Object = nullptr;

	/** for direct reference widget. */
	UPROPERTY()
		TObjectPtr<ULexWidget> HelperWidget = nullptr;
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
struct FLexUIPrefabSequenceObjectReferences
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FLexUIPrefabSequenceObjectReference> Array;
};

USTRUCT()
struct FLexUIPrefabSequenceObjectReferenceMap
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
	void CreateBinding(const FGuid& ObjectId, const FLexUIPrefabSequenceObjectReference& ObjectReference);

	/**
	 * Resolve a binding for the specified ID using a given context
	 *
	 * @param ObjectId		The ID to associate the object with
	 * @param OutObjects	Container to populate with bound components
	 */
	void ResolveBinding(const FGuid& ObjectId, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const;

#if WITH_EDITOR
	bool IsObjectReferencesGood(ULexWidget* InContextWidget)const;
	bool IsEditorHelpersGood(ULexWidget* InContextWidget)const;
	//return true if anything changed
	bool FixObjectReferences(ULexWidget* InContextWidget);
	//return true if anything changed
	bool FixEditorHelpers(ULexWidget* InContextWidget);
#endif
private:
	
	UPROPERTY()
	TArray<FGuid> BindingIds;

	UPROPERTY()
	TArray<FLexUIPrefabSequenceObjectReferences> References;
};
