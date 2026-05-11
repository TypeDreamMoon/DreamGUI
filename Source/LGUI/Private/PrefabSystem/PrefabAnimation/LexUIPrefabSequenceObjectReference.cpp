// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceObjectReference.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"


#if WITH_EDITOR
FString FLexUIPrefabSequenceObjectReference::GetActorPathRelativeToContextActor(ULexWidget* InContextActor, ULexWidget* InActor)
{
	if (InActor == InContextActor)
	{
		return TEXT("/");
	}
	else if (InActor->IsChildOf(InContextActor))
	{
		FString Result = InActor->GetDisplayName();
		auto Parent = InActor->GetParent();
		while (Parent != nullptr && Parent != InContextActor)
		{
			Result = Parent->GetDisplayName() + "/" + Result;
			Parent = Parent->GetParent();
		}
		return Result;
	}
	return TEXT("");
}
ULexWidget* FLexUIPrefabSequenceObjectReference::GetActorFromContextActorByRelativePath(ULexWidget* InContextActor, const FString& InPath)
{
	if (InPath == TEXT("/"))
	{
		return InContextActor;
	}
	else
	{
		TArray<FString> SplitedArray;
		{
			if (InPath.Contains(TEXT("/")))
			{
				FString SourceString = InPath;
				FString Left, Right;
				TArray<AActor*> ChildrenActors;
				while (SourceString.Split(TEXT("/"), &Left, &Right, ESearchCase::CaseSensitive))
				{
					SplitedArray.Add(Left);
					SourceString = Right;
				}
				SplitedArray.Add(Right);
			}
			else
			{
				SplitedArray.Add(InPath);
			}
		}

		auto ParentActor = InContextActor;
		for (int i = 0; i < SplitedArray.Num(); i++)
		{
			auto& PathItem = SplitedArray[i];
			auto Children = ParentActor->GetChildren();
			ULexWidget* FoundChildActor = nullptr;
			for (auto& ChildActor : Children)
			{
				if (PathItem == ChildActor->GetDisplayName())
				{
					FoundChildActor = ChildActor;
					break;
				}
			}
			if (FoundChildActor != nullptr)
			{
				if (i + 1 == SplitedArray.Num())
				{
					return FoundChildActor;
				}
				ParentActor = FoundChildActor;
			}
			else
			{
				return nullptr;
			}
		}
	}
	return nullptr;
}
bool FLexUIPrefabSequenceObjectReference::FixObjectReferenceFromEditorHelpers(ULexWidget* InContextActor)
{
	if (auto FoundHelperActor = GetActorFromContextActorByRelativePath(InContextActor, this->HelperActorPath))
	{
		HelperActor = FoundHelperActor;
		HelperActorLabel = HelperActor->GetDisplayName();
		if (ObjectPathRelativeToActor.IsEmpty())
		{
			Object = HelperActor;
			return true;
		}
		else 
		{
			FSoftObjectPath ObjectPath(FString::Printf(TEXT("%s.%s"), *HelperActor->GetPathName(), *this->ObjectPathRelativeToActor));
			Object = ObjectPath.ResolveObject();
			return IsValid(Object);
		}
	}
	return false;
}
bool FLexUIPrefabSequenceObjectReference::CanFixObjectReferenceFromEditorHelpers()const
{
	return !HelperActorPath.IsEmpty();
}
bool FLexUIPrefabSequenceObjectReference::IsObjectReferenceGood(ULexWidget* InContextActor)const
{
	CheckTargetObject();
	auto Actor = Cast<ULexWidget>(Object);
	if (Actor == nullptr)
	{
		Actor = Object->GetTypedOuter<ULexWidget>();
	}

	if (Actor != nullptr)
	{
		return (Actor == InContextActor || Actor->IsChildOf(InContextActor))//only allow actor self or child actor
			;
	}
	return false;
}
bool FLexUIPrefabSequenceObjectReference::IsEditorHelpersGood(ULexWidget* InContextActor)const
{
	return IsValid(HelperActor)
		&& HelperActorPath == GetActorPathRelativeToContextActor(InContextActor, HelperActor)
		;
}
#endif

bool FLexUIPrefabSequenceObjectReference::InitHelpers(ULexWidget* InContextActor)
{
	if (auto Actor = Cast<ULexWidget>(Object))
	{
		this->HelperActor = Actor;
		this->ObjectPathRelativeToActor = "";
#if WITH_EDITOR
		this->HelperActorLabel = Actor->GetDisplayName();
		this->HelperActorPath = GetActorPathRelativeToContextActor(InContextActor, Actor);
#endif
		return true;
	}
	else
	{
		Actor = Object->GetTypedOuter<ULexWidget>();
		this->HelperActor = Actor;
		this->ObjectPathRelativeToActor = Object->GetPathName(Actor);
#if WITH_EDITOR
		this->HelperActorLabel = Actor->GetDisplayName();
		this->HelperActorPath = GetActorPathRelativeToContextActor(InContextActor, Actor);
#endif
		return true;
	}
}
bool FLexUIPrefabSequenceObjectReference::CreateForObject(ULexWidget* InContextActor, UObject* InObject, FLexUIPrefabSequenceObjectReference& OutResult)
{
	OutResult.Object = InObject;
	return OutResult.InitHelpers(InContextActor);
}

bool FLexUIPrefabSequenceObjectReference::CheckTargetObject()const
{
	if (IsValid(Object))
	{
		return true;
	}
	else
	{
		if (IsValid(HelperActor))
		{
			if (this->ObjectPathRelativeToActor.IsEmpty())
			{
				Object = HelperActor;
				return true;
			}
			else
			{
				FSoftObjectPath ObjectPath(FString::Printf(TEXT("%s.%s"), *HelperActor->GetPathName(), *this->ObjectPathRelativeToActor));
				Object = ObjectPath.ResolveObject();
				return IsValid(Object);
			}
		}
	}
	return false;
}

UObject* FLexUIPrefabSequenceObjectReference::Resolve() const
{
	CheckTargetObject();
	return Object;
}

bool FLexUIPrefabSequenceObjectReferenceMap::HasBinding(const FGuid& ObjectId) const
{
	return BindingIds.Contains(ObjectId);
}

void FLexUIPrefabSequenceObjectReferenceMap::RemoveBinding(const FGuid& ObjectId)
{
	int32 Index = BindingIds.IndexOfByKey(ObjectId);
	if (Index != INDEX_NONE)
	{
		BindingIds.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		References.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}
}

void FLexUIPrefabSequenceObjectReferenceMap::CreateBinding(const FGuid& ObjectId, const FLexUIPrefabSequenceObjectReference& ObjectReference)
{
	int32 ExistingIndex = BindingIds.IndexOfByKey(ObjectId);
	if (ExistingIndex == INDEX_NONE)
	{
		ExistingIndex = BindingIds.Num();

		BindingIds.Add(ObjectId);
		References.AddDefaulted();
	}

	References[ExistingIndex].Array.AddUnique(ObjectReference);
}

void FLexUIPrefabSequenceObjectReferenceMap::ResolveBinding(const FGuid& ObjectId, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	int32 Index = BindingIds.IndexOfByKey(ObjectId);
	if (Index == INDEX_NONE)
	{
		return;
	}

	for (const FLexUIPrefabSequenceObjectReference& Reference : References[Index].Array)
	{
		if (UObject* Object = Reference.Resolve())
		{
			OutObjects.Add(Object);
		}
	}
}

#if WITH_EDITOR
bool FLexUIPrefabSequenceObjectReferenceMap::IsObjectReferencesGood(ULexWidget* InContextActor)const
{
	for (auto& Reference : References)
	{
		for (auto& RefItem : Reference.Array)
		{
			if (!RefItem.IsObjectReferenceGood(InContextActor))
			{
				return false;
			}
		}
	}
	return true;
}
bool FLexUIPrefabSequenceObjectReferenceMap::IsEditorHelpersGood(ULexWidget* InContextActor)const
{
	for (auto& Reference : References)
	{
		for (auto& RefItem : Reference.Array)
		{
			if (!RefItem.IsEditorHelpersGood(InContextActor))
			{
				return false;
			}
		}
	}
	return true;
}
bool FLexUIPrefabSequenceObjectReferenceMap::FixObjectReferences(ULexWidget* InContextActor)
{
	bool anythingChanged = false;
	for (auto& Reference : References)
	{
		for (auto& RefItem : Reference.Array)
		{
			if (!RefItem.IsObjectReferenceGood(InContextActor) && RefItem.CanFixObjectReferenceFromEditorHelpers())
			{
				if (RefItem.FixObjectReferenceFromEditorHelpers(InContextActor))
				{
					anythingChanged = true;
				}
			}
		}
	}
	return anythingChanged;
}
bool FLexUIPrefabSequenceObjectReferenceMap::FixEditorHelpers(ULexWidget* InContextActor)
{
	bool anythingChanged = false;
	for (auto& Reference : References)
	{
		for (auto& RefItem : Reference.Array)
		{
			if (RefItem.IsObjectReferenceGood(InContextActor) && !RefItem.IsEditorHelpersGood(InContextActor))
			{
				if (RefItem.InitHelpers(InContextActor))
				{
					anythingChanged = true;
				}
			}
		}
	}
	return anythingChanged;
}
#endif


