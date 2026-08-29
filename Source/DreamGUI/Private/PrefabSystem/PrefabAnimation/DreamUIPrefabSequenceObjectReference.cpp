// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceObjectReference.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"


FString FDreamUIPrefabSequenceObjectReference::GetWidgetPathRelativeToContextWidget(UDreamWidget* InContextWidget, UDreamWidget* InWidget)
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
UDreamWidget* FDreamUIPrefabSequenceObjectReference::GetWidgetFromContextWidgetByRelativePath(UDreamWidget* InContextWidget, const FString& InPath)
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
			UDreamWidget* FoundChild = nullptr;
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
bool FDreamUIPrefabSequenceObjectReference::FixObjectReferenceFromEditorHelpers(UDreamWidget* InContextWidget)
{
#if WITH_EDITOR
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
bool FDreamUIPrefabSequenceObjectReference::CanFixObjectReferenceFromEditorHelpers()const
{
	return !HelperWidgetPath.IsEmpty();
}
bool FDreamUIPrefabSequenceObjectReference::IsObjectReferenceGood(UDreamWidget* InContextWidget)const
{
	if (!IsValid(InContextWidget) || !CheckTargetObject() || !IsValid(Object))
	{
		return false;
	}
	auto Widget = Cast<UDreamWidget>(Object);
	if (Widget == nullptr)
	{
		Widget = Object->GetTypedOuter<UDreamWidget>();
	}

	if (Widget != nullptr)
	{
		return (Widget == InContextWidget || Widget->IsChildOf(InContextWidget))//only allow widget self or child widget
			;
	}
	return false;
}
bool FDreamUIPrefabSequenceObjectReference::IsEditorHelpersGood(UDreamWidget* InContextWidget)const
{
	return IsValid(HelperWidget)
		&& HelperWidgetPath == GetWidgetPathRelativeToContextWidget(InContextWidget, HelperWidget)
		;
}
#endif

bool FDreamUIPrefabSequenceObjectReference::InitHelpers(UDreamWidget* InContextWidget)
{
	if (!IsValid(InContextWidget) || !IsValid(Object))
	{
		HelperWidget = nullptr;
		ObjectPathRelativeToWidget.Reset();
		HelperWidgetPath.Reset();
		return false;
	}

	if (auto Widget = Cast<UDreamWidget>(Object))
	{
		this->HelperWidget = Widget;
		this->ObjectPathRelativeToWidget = "";
		// Always, not only in the editor: this path is the binding now, not a convenience for fixup.
		this->HelperWidgetPath = GetWidgetPathRelativeToContextWidget(InContextWidget, Widget);
		return true;
	}
	else
	{
		Widget = Object->GetTypedOuter<UDreamWidget>();
		this->HelperWidget = Widget;
		this->ObjectPathRelativeToWidget = Object->GetPathName(Widget);
		this->HelperWidgetPath = GetWidgetPathRelativeToContextWidget(InContextWidget, Widget);
		return true;
	}
}
bool FDreamUIPrefabSequenceObjectReference::CreateForObject(UDreamWidget* InContextWidget, UObject* InObject, FDreamUIPrefabSequenceObjectReference& OutResult)
{
	OutResult.Object = InObject;
	return OutResult.InitHelpers(InContextWidget);
}

bool FDreamUIPrefabSequenceObjectReference::CheckTargetObject()const
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

UObject* FDreamUIPrefabSequenceObjectReference::Resolve() const
{
	CheckTargetObject();
	return Object;
}

UObject* FDreamUIPrefabSequenceObjectReference::ResolveInContext(UDreamWidget* InContextWidget) const
{
	if (!IsValid(InContextWidget) || HelperWidgetPath.IsEmpty())
	{
		return nullptr;
	}
	UDreamWidget* Widget = GetWidgetFromContextWidgetByRelativePath(InContextWidget, HelperWidgetPath);
	if (!IsValid(Widget))
	{
		return nullptr;
	}
	if (ObjectPathRelativeToWidget.IsEmpty())
	{
		return Widget;
	}
	// A sub-object of that widget -- its visual, or one of its behaviours. Relative to the widget
	// this context resolved to, so it lands in the same tree rather than the authored one.
	const FSoftObjectPath SubObjectPath(FString::Printf(TEXT("%s.%s"), *Widget->GetPathName(), *ObjectPathRelativeToWidget));
	return SubObjectPath.ResolveObject();
}

bool FDreamUIPrefabSequenceObjectReferenceMap::HasBinding(const FGuid& ObjectId) const
{
	const int32 Index = BindingIds.IndexOfByKey(ObjectId);
	return References.IsValidIndex(Index);
}

void FDreamUIPrefabSequenceObjectReferenceMap::RemoveBinding(const FGuid& ObjectId)
{
	References.SetNum(BindingIds.Num());
	int32 Index = BindingIds.IndexOfByKey(ObjectId);
	if (Index != INDEX_NONE)
	{
		BindingIds.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		References.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}
}

void FDreamUIPrefabSequenceObjectReferenceMap::CreateBinding(const FGuid& ObjectId, const FDreamUIPrefabSequenceObjectReference& ObjectReference)
{
	References.SetNum(BindingIds.Num());
	int32 ExistingIndex = BindingIds.IndexOfByKey(ObjectId);
	if (ExistingIndex == INDEX_NONE)
	{
		ExistingIndex = BindingIds.Num();

		BindingIds.Add(ObjectId);
		References.AddDefaulted();
	}
	References[ExistingIndex].Array.AddUnique(ObjectReference);
}

void FDreamUIPrefabSequenceObjectReferenceMap::ResolveBinding(const FGuid& ObjectId, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	int32 Index = BindingIds.IndexOfByKey(ObjectId);
	if (!References.IsValidIndex(Index))
	{
		return;
	}

	for (const FDreamUIPrefabSequenceObjectReference& Reference : References[Index].Array)
	{
		if (UObject* Object = Reference.Resolve())
		{
			OutObjects.Add(Object);
		}
	}
}

void FDreamUIPrefabSequenceObjectReferenceMap::ResolveBindingInContext(const FGuid& ObjectId,
	UDreamWidget* InContextWidget, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	const int32 Index = BindingIds.IndexOfByKey(ObjectId);
	if (!References.IsValidIndex(Index))
	{
		return;
	}
	for (const FDreamUIPrefabSequenceObjectReference& Reference : References[Index].Array)
	{
		if (UObject* Object = Reference.ResolveInContext(InContextWidget))
		{
			OutObjects.Add(Object);
		}
	}
}

#if WITH_EDITOR
bool FDreamUIPrefabSequenceObjectReferenceMap::IsObjectReferencesGood(UDreamWidget* InContextWidget)const
{
	if (BindingIds.Num() != References.Num()) return false;
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
void FDreamUIPrefabSequenceObjectReferenceMap::GetInvalidBindingIds(UDreamWidget* InContextWidget, TArray<FGuid>& OutBindingIds) const
{
	const int32 Count = FMath::Min(BindingIds.Num(), References.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		for (const FDreamUIPrefabSequenceObjectReference& Reference : References[Index].Array)
		{
			if (!Reference.IsObjectReferenceGood(InContextWidget))
			{
				OutBindingIds.AddUnique(BindingIds[Index]);
				break;
			}
		}
	}
	for (int32 Index = Count; Index < BindingIds.Num(); ++Index)
	{
		OutBindingIds.AddUnique(BindingIds[Index]);
	}
}
bool FDreamUIPrefabSequenceObjectReferenceMap::IsEditorHelpersGood(UDreamWidget* InContextWidget)const
{
	if (BindingIds.Num() != References.Num()) return false;
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
bool FDreamUIPrefabSequenceObjectReferenceMap::FixObjectReferences(UDreamWidget* InContextWidget)
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
bool FDreamUIPrefabSequenceObjectReferenceMap::FixEditorHelpers(UDreamWidget* InContextWidget)
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


