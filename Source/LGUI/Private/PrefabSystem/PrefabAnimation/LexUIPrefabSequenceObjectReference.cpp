// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceObjectReference.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "LGUI.h"
#include "Core/Components/LexWidget.h"


#if WITH_EDITOR
FString FLexUIPrefabSequenceObjectReference::GetWidgetPathRelativeToContextWidget(ULexWidget* InContextWidget, ULexWidget* InWidget)
{
	if (InWidget == InContextWidget)
	{
		return TEXT("/");
	}
	else if (InWidget->IsChildOf(InContextWidget))
	{
		FString Result = InWidget->GetDisplayName();
		auto Parent = InWidget->GetParent();
		while (Parent != nullptr && Parent != InContextWidget)
		{
			Result = Parent->GetDisplayName() + "/" + Result;
			Parent = Parent->GetParent();
		}
		return Result;
	}
	return TEXT("");
}
ULexWidget* FLexUIPrefabSequenceObjectReference::GetWidgetFromContextWidgetByRelativePath(ULexWidget* InContextWidget, const FString& InPath)
{
	if (InPath == TEXT("/"))
	{
		return InContextWidget;
	}
	else
	{
		TArray<FString> SplitedArray;
		{
			if (InPath.Contains(TEXT("/")))
			{
				FString SourceString = InPath;
				FString Left, Right;
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

		auto Parent = InContextWidget;
		for (int i = 0; i < SplitedArray.Num(); i++)
		{
			auto& PathItem = SplitedArray[i];
			auto Children = Parent->GetChildren();
			ULexWidget* FoundChild = nullptr;
			for (auto& Child : Children)
			{
				if (PathItem == Child->GetDisplayName())
				{
					FoundChild = Child;
					break;
				}
			}
			if (FoundChild != nullptr)
			{
				if (i + 1 == SplitedArray.Num())
				{
					return FoundChild;
				}
				Parent = FoundChild;
			}
			else
			{
				return nullptr;
			}
		}
	}
	return nullptr;
}
bool FLexUIPrefabSequenceObjectReference::FixObjectReferenceFromEditorHelpers(ULexWidget* InContextWidget)
{
	if (auto FoundHelper = GetWidgetFromContextWidgetByRelativePath(InContextWidget, this->HelperWidgetPath))
	{
		HelperWidget = FoundHelper;
		if (ObjectPathRelativeToWidget.IsEmpty())
		{
			Object = HelperWidget;
			return true;
		}
		else 
		{
			FSoftObjectPath ObjectPath(FString::Printf(TEXT("%s.%s"), *HelperWidget->GetPathName(), *this->ObjectPathRelativeToWidget));
			Object = ObjectPath.ResolveObject();
			return IsValid(Object);
		}
	}
	return false;
}
bool FLexUIPrefabSequenceObjectReference::CanFixObjectReferenceFromEditorHelpers()const
{
	return !HelperWidgetPath.IsEmpty();
}
bool FLexUIPrefabSequenceObjectReference::IsObjectReferenceGood(ULexWidget* InContextWidget)const
{
	CheckTargetObject();
	auto Widget = Cast<ULexWidget>(Object);
	if (Widget == nullptr)
	{
		Widget = Object->GetTypedOuter<ULexWidget>();
	}

	if (Widget != nullptr)
	{
		return (Widget == InContextWidget || Widget->IsChildOf(InContextWidget))//only allow widget self or child widget
			;
	}
	return false;
}
bool FLexUIPrefabSequenceObjectReference::IsEditorHelpersGood(ULexWidget* InContextWidget)const
{
	return IsValid(HelperWidget)
		&& HelperWidgetPath == GetWidgetPathRelativeToContextWidget(InContextWidget, HelperWidget)
		;
}
#endif

bool FLexUIPrefabSequenceObjectReference::InitHelpers(ULexWidget* InContextWidget)
{
	if (auto Widget = Cast<ULexWidget>(Object))
	{
		this->HelperWidget = Widget;
		this->ObjectPathRelativeToWidget = "";
#if WITH_EDITOR
		this->HelperWidgetPath = GetWidgetPathRelativeToContextWidget(InContextWidget, Widget);
#endif
		return true;
	}
	else
	{
		Widget = Object->GetTypedOuter<ULexWidget>();
		this->HelperWidget = Widget;
		this->ObjectPathRelativeToWidget = Object->GetPathName(Widget);
#if WITH_EDITOR
		this->HelperWidgetPath = GetWidgetPathRelativeToContextWidget(InContextWidget, Widget);
#endif
		return true;
	}
}
bool FLexUIPrefabSequenceObjectReference::CreateForObject(ULexWidget* InContextWidget, UObject* InObject, FLexUIPrefabSequenceObjectReference& OutResult)
{
	OutResult.Object = InObject;
	return OutResult.InitHelpers(InContextWidget);
}

bool FLexUIPrefabSequenceObjectReference::CheckTargetObject()const
{
	if (IsValid(Object))
	{
		return true;
	}
	else
	{
		if (IsValid(HelperWidget))
		{
			if (this->ObjectPathRelativeToWidget.IsEmpty())
			{
				Object = HelperWidget;
				return true;
			}
			else
			{
				FSoftObjectPath ObjectPath(FString::Printf(TEXT("%s.%s"), *HelperWidget->GetPathName(), *this->ObjectPathRelativeToWidget));
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
bool FLexUIPrefabSequenceObjectReferenceMap::IsObjectReferencesGood(ULexWidget* InContextWidget)const
{
	for (auto& Reference : References)
	{
		for (auto& RefItem : Reference.Array)
		{
			if (!RefItem.IsObjectReferenceGood(InContextWidget))
			{
				return false;
			}
		}
	}
	return true;
}
bool FLexUIPrefabSequenceObjectReferenceMap::IsEditorHelpersGood(ULexWidget* InContextWidget)const
{
	for (auto& Reference : References)
	{
		for (auto& RefItem : Reference.Array)
		{
			if (!RefItem.IsEditorHelpersGood(InContextWidget))
			{
				return false;
			}
		}
	}
	return true;
}
bool FLexUIPrefabSequenceObjectReferenceMap::FixObjectReferences(ULexWidget* InContextWidget)
{
	bool anythingChanged = false;
	for (auto& Reference : References)
	{
		for (auto& RefItem : Reference.Array)
		{
			if (!RefItem.IsObjectReferenceGood(InContextWidget) && RefItem.CanFixObjectReferenceFromEditorHelpers())
			{
				if (RefItem.FixObjectReferenceFromEditorHelpers(InContextWidget))
				{
					anythingChanged = true;
				}
			}
		}
	}
	return anythingChanged;
}
bool FLexUIPrefabSequenceObjectReferenceMap::FixEditorHelpers(ULexWidget* InContextWidget)
{
	bool anythingChanged = false;
	for (auto& Reference : References)
	{
		for (auto& RefItem : Reference.Array)
		{
			if (RefItem.IsObjectReferenceGood(InContextWidget) && !RefItem.IsEditorHelpersGood(InContextWidget))
			{
				if (RefItem.InitHelpers(InContextWidget))
				{
					anythingChanged = true;
				}
			}
		}
	}
	return anythingChanged;
}
#endif


