// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamWidgetAnimationObjectReference.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"


FString FDreamWidgetAnimationObjectReference::GetWidgetPathRelativeToContextWidget(UDreamWidget* InContextWidget, UDreamWidget* InWidget)
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
UDreamWidget* FDreamWidgetAnimationObjectReference::GetWidgetFromContextWidgetByRelativePath(UDreamWidget* InContextWidget, const FString& InPath)
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
bool FDreamWidgetAnimationObjectReference::FixObjectReferenceFromEditorHelpers(UDreamWidget* InContextWidget)
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
bool FDreamWidgetAnimationObjectReference::CanFixObjectReferenceFromEditorHelpers()const
{
	return !HelperWidgetPath.IsEmpty();
}
bool FDreamWidgetAnimationObjectReference::IsObjectReferenceGood(UDreamWidget* InContextWidget)const
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
bool FDreamWidgetAnimationObjectReference::IsEditorHelpersGood(UDreamWidget* InContextWidget)const
{
	return IsValid(HelperWidget)
		&& HelperWidgetPath == GetWidgetPathRelativeToContextWidget(InContextWidget, HelperWidget)
		;
}
#endif

bool FDreamWidgetAnimationObjectReference::InitHelpers(UDreamWidget* InContextWidget)
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
bool FDreamWidgetAnimationObjectReference::CreateForObject(UDreamWidget* InContextWidget, UObject* InObject, FDreamWidgetAnimationObjectReference& OutResult)
{
	OutResult.Object = InObject;
	return OutResult.InitHelpers(InContextWidget);
}

bool FDreamWidgetAnimationObjectReference::CheckTargetObject()const
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

UObject* FDreamWidgetAnimationObjectReference::Resolve() const
{
	CheckTargetObject();
	return Object;
}

UObject* FDreamWidgetAnimationObjectReference::ResolveInContext(UDreamWidget* InContextWidget) const
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

bool FDreamWidgetAnimationObjectReferenceMap::HasBinding(const FGuid& ObjectId) const
{
	const int32 Index = BindingIds.IndexOfByKey(ObjectId);
	return References.IsValidIndex(Index);
}

void FDreamWidgetAnimationObjectReferenceMap::RemoveBinding(const FGuid& ObjectId)
{
	References.SetNum(BindingIds.Num());
	int32 Index = BindingIds.IndexOfByKey(ObjectId);
	if (Index != INDEX_NONE)
	{
		BindingIds.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		References.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	}
}

void FDreamWidgetAnimationObjectReferenceMap::CreateBinding(const FGuid& ObjectId, const FDreamWidgetAnimationObjectReference& ObjectReference)
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

void FDreamWidgetAnimationObjectReferenceMap::ResolveBinding(const FGuid& ObjectId, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	int32 Index = BindingIds.IndexOfByKey(ObjectId);
	if (!References.IsValidIndex(Index))
	{
		return;
	}

	for (const FDreamWidgetAnimationObjectReference& Reference : References[Index].Array)
	{
		if (UObject* Object = Reference.Resolve())
		{
			OutObjects.Add(Object);
		}
	}
}

void FDreamWidgetAnimationObjectReferenceMap::ResolveBindingInContext(const FGuid& ObjectId,
	UDreamWidget* InContextWidget, TArray<UObject*, TInlineAllocator<1>>& OutObjects) const
{
	const int32 Index = BindingIds.IndexOfByKey(ObjectId);
	if (!References.IsValidIndex(Index))
	{
		return;
	}
	for (const FDreamWidgetAnimationObjectReference& Reference : References[Index].Array)
	{
		if (UObject* Object = Reference.ResolveInContext(InContextWidget))
		{
			OutObjects.Add(Object);
		}
	}
}

#if WITH_EDITOR
bool FDreamWidgetAnimationObjectReferenceMap::IsObjectReferencesGood(UDreamWidget* InContextWidget)const
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
void FDreamWidgetAnimationObjectReferenceMap::GetInvalidBindingIds(UDreamWidget* InContextWidget, TArray<FGuid>& OutBindingIds) const
{
	const int32 Count = FMath::Min(BindingIds.Num(), References.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		for (const FDreamWidgetAnimationObjectReference& Reference : References[Index].Array)
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
bool FDreamWidgetAnimationObjectReferenceMap::IsEditorHelpersGood(UDreamWidget* InContextWidget)const
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
bool FDreamWidgetAnimationObjectReferenceMap::FixObjectReferences(UDreamWidget* InContextWidget)
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
bool FDreamWidgetAnimationObjectReferenceMap::FixEditorHelpers(UDreamWidget* InContextWidget)
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


