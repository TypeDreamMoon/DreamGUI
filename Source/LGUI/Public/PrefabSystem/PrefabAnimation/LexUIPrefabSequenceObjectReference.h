// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "UObject/LazyObjectPtr.h"
#include "LexUIPrefabSequenceObjectReference.generated.h"

class ULexWidget;
class UActorComponent;

/**
 * An external reference to a level sequence object, resolvable through an arbitrary context.
 */
USTRUCT()
struct LGUI_API FLexUIPrefabSequenceObjectReference
{
	GENERATED_BODY()

public:

#if WITH_EDITOR
	static FString GetActorPathRelativeToContextActor(ULexWidget* InContextActor, ULexWidget* InActor);
	static ULexWidget* GetActorFromContextActorByRelativePath(ULexWidget* InContextActor, const FString& InPath);
	bool FixObjectReferenceFromEditorHelpers(ULexWidget* InContextActor);
	bool CanFixObjectReferenceFromEditorHelpers()const;
	bool IsObjectReferenceGood(ULexWidget* InContextActor)const;
	bool IsEditorHelpersGood(ULexWidget* InContextActor)const;
#endif
	static bool CreateForObject(ULexWidget* InContextActor, UObject* InObject, FLexUIPrefabSequenceObjectReference& OutResult);

	bool InitHelpers(ULexWidget* InContextActor);
	bool CheckTargetObject()const;
	/**
	 * Check whether this object reference is valid or not
	 */
	bool IsValidReference() const
	{
		return CheckTargetObject();
	}

	/**
	 * Resolve this reference from the specified source actor
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

	/** for direct reference actor. */
	UPROPERTY()
		TObjectPtr<ULexWidget> HelperActor = nullptr;
	/** object path relative to owner actor
	 * if path is empty then means actor self
	 */
	UPROPERTY()
	FString ObjectPathRelativeToActor;

#if WITH_EDITORONLY_DATA
	/** HelperActor's actor label/ */
	UPROPERTY()
		FString HelperActorLabel;
	/** HelperActor's path relative to context actor, split by '/'. If only '/' means it is the context actor. could use this to replace reference object in editor/ */
	UPROPERTY()
		FString HelperActorPath;
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
	bool IsObjectReferencesGood(ULexWidget* InContextActor)const;
	bool IsEditorHelpersGood(ULexWidget* InContextActor)const;
	//return true if anything changed
	bool FixObjectReferences(ULexWidget* InContextActor);
	//return true if anything changed
	bool FixEditorHelpers(ULexWidget* InContextActor);
#endif
private:
	
	UPROPERTY()
	TArray<FGuid> BindingIds;

	UPROPERTY()
	TArray<FLexUIPrefabSequenceObjectReferences> References;
};
