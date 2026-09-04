// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Animation/DreamWidgetAnimationObjectReference.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/Blueprint.h"
#include "UObject/Package.h"
#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"


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
// Everything from here to the #endif below is editor-only, and the guard has to sit OUTSIDE the
// first signature rather than inside its body: these functions are declared under #if WITH_EDITOR
// in the header, so in a non-editor build a definition left outside the guard has no declaration to
// match -- and with the body elided its opening brace swallowed the next function whole. This
// translation unit did not compile for Game or Shipping until the guard moved up one line.
#if WITH_EDITOR
bool FDreamWidgetAnimationObjectReference::FixObjectReferenceFromEditorHelpers(UDreamWidget* InContextWidget)
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
bool FDreamWidgetAnimationObjectReference::RenameWidgetPathSegment(const FString& InOldSegment, const FString& InNewSegment)
{
	// An empty path resolves in no context at all and a "/" path names the context widget itself.
	// Neither spells a display name, so neither can carry a rename. Checked before the split rather
	// than relying on it, because ParseIntoArray would turn "/" into two empty segments and rejoining
	// them would silently turn the self-path into "" -- a repair that unbinds the track it was fixing.
	if (HelperWidgetPath.IsEmpty() || HelperWidgetPath == TEXT("/"))
	{
		return false;
	}
	if (InOldSegment.IsEmpty() || InNewSegment.IsEmpty() || InOldSegment.Equals(InNewSegment, ESearchCase::IgnoreCase))
	{
		return false;
	}

	TArray<FString> Segments;
	// Empties kept, so a path that already had an odd shape comes back out the shape it went in.
	// This function's job is one word, not normalisation: a rejoin that dropped an empty segment
	// would rewrite paths this rename has nothing to do with, and those are the edits nobody reviews.
	HelperWidgetPath.ParseIntoArray(Segments, TEXT("/"), /*InCullEmpty*/false);

	bool bChanged = false;
	for (FString& Segment : Segments)
	{
		if (Segment.Equals(InOldSegment, ESearchCase::IgnoreCase))
		{
			Segment = InNewSegment;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		HelperWidgetPath = FString::Join(Segments, TEXT("/"));
	}
	return bChanged;
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

bool FDreamWidgetAnimationObjectReference::DetachHelperOutsideTree(const UDreamWidgetTree* InOwnTree) const
{
	// A null tree is not evidence of anything. A widget built outside any tree -- a test fixture, a
	// native control before its hierarchy exists -- reports null, and treating that as "a different
	// tree" would clear pointers that were never cross-tree at all.
	if (InOwnTree == nullptr)
	{
		return false;
	}
	// The resolve cache first and on its own terms. It is only ever built FROM HelperWidget, but it
	// outlives a HelperWidget that has since been cleared, and an object held here from another tree
	// is the same strong reference into the same tree. Transient and rebuilt on demand by
	// CheckTargetObject, so dropping it costs nothing.
	if (Object != nullptr && (!IsValid(Object) || Object->GetTypedOuter<UDreamWidgetTree>() != InOwnTree))
	{
		Object = nullptr;
	}
	// Outer, not the Parent chain: every widget in a tree is outered flat to its UDreamWidgetTree
	// (UDreamWidgetTree::ConstructWidget, and the instancing graph maps the source root to the
	// destination one), and the outer is set the moment an object is constructed -- while Parent is
	// DuplicateTransient and is only rebuilt once the whole tree has been instanced. This has to
	// answer before that, so it may not depend on it.
	if (!IsValid(HelperWidget) || HelperWidget->GetTypedOuter<UDreamWidgetTree>() == InOwnTree)
	{
		return false;
	}
	HelperWidget = nullptr;
	Object = nullptr;
	return true;
}

bool FDreamWidgetAnimationObjectReference::RebindHelperToContext(UDreamWidget* InContextWidget, bool& OutResolvedByPath) const
{
	OutResolvedByPath = false;
	if (!IsValid(InContextWidget) || HelperWidgetPath.IsEmpty())
	{
		return false;
	}
	UDreamWidget* Resolved = GetWidgetFromContextWidgetByRelativePath(InContextWidget, HelperWidgetPath);
	if (Resolved == nullptr)
	{
		// The path is the binding and playback resolves through it, so a path this context cannot walk
		// is a broken binding whatever the pointer says. All that can be done here is refuse to keep a
		// pointer into somebody else's tree; a stale path over a pointer of our OWN is a rename, and
		// FixEditorHelpers repairs the path from exactly that pointer.
		return DetachHelperOutsideTree(InContextWidget->GetTypedOuter<UDreamWidgetTree>());
	}
	OutResolvedByPath = true;
	if (Resolved == HelperWidget)
	{
		return false;
	}
	HelperWidget = Resolved;
	// Resolved from the old pointer, so it names the old tree's object.
	Object = nullptr;
	return true;
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

int32 FDreamWidgetAnimationObjectReferenceMap::DetachHelpersOutsideTree(const UDreamWidgetTree* InOwnTree) const
{
	int32 DetachedCount = 0;
	for (const FDreamWidgetAnimationObjectReferences& Reference : References)
	{
		for (const FDreamWidgetAnimationObjectReference& RefItem : Reference.Array)
		{
			if (RefItem.DetachHelperOutsideTree(InOwnTree))
			{
				++DetachedCount;
			}
		}
	}
	return DetachedCount;
}

int32 FDreamWidgetAnimationObjectReferenceMap::RebindHelpersToContext(UDreamWidget* InContextWidget, TArray<FString>& OutUnresolvedPaths) const
{
	int32 ReboundCount = 0;
	for (const FDreamWidgetAnimationObjectReferences& Reference : References)
	{
		for (const FDreamWidgetAnimationObjectReference& RefItem : Reference.Array)
		{
			bool bResolvedByPath = false;
			if (RefItem.RebindHelperToContext(InContextWidget, bResolvedByPath))
			{
				++ReboundCount;
			}
			// A reference that never recorded a path is not a path this context failed to walk -- it
			// resolves in no context at all and always did, and reporting it here would say "broken
			// binding" about references the older editor paths simply never filled in.
			if (!bResolvedByPath && !RefItem.GetHelperWidgetPath().IsEmpty())
			{
				OutUnresolvedPaths.AddUnique(RefItem.GetHelperWidgetPath());
			}
		}
	}
	return ReboundCount;
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
void FDreamWidgetAnimationObjectReferenceMap::GetUnresolvableBindingPaths(UDreamWidget* InContextWidget, TArray<TPair<FGuid, FString>>& OutBindings) const
{
	const int32 Count = FMath::Min(BindingIds.Num(), References.Num());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		for (const FDreamWidgetAnimationObjectReference& Reference : References[Index].Array)
		{
			// ResolveInContext and nothing weaker. It is the exact call LocateBoundObjects makes for
			// an instance, so whatever it refuses here is refused there too -- and there the refusal
			// is invisible, because the fallback path then resolves the stored pointer instead and
			// the animation drives the class template's widget rather than the instance's.
			if (Reference.ResolveInContext(InContextWidget) == nullptr)
			{
				OutBindings.Emplace(BindingIds[Index], Reference.GetHelperWidgetPath());
				break;
			}
		}
	}
	// Ids past the end of References hold no reference to ask. GetInvalidBindingIds reports the same
	// tail for the same reason: a count mismatch means the two arrays stopped describing each other.
	for (int32 Index = Count; Index < BindingIds.Num(); ++Index)
	{
		OutBindings.Emplace(BindingIds[Index], FString());
	}
}
int32 FDreamWidgetAnimationObjectReferenceMap::RenameWidgetPathSegment(const FString& InOldSegment, const FString& InNewSegment)
{
	// Every reference, not only the ones a context cannot walk. A hierarchy mid-rename has bindings
	// on BOTH sides of the change -- one track already pointing at the new name, another still at the
	// old -- and asking "is this one broken" first would make the repair depend on which order the
	// author renamed things in. Matching the segment is the whole condition.
	int32 ChangedCount = 0;
	for (FDreamWidgetAnimationObjectReferences& Reference : References)
	{
		for (FDreamWidgetAnimationObjectReference& RefItem : Reference.Array)
		{
			if (RefItem.RenameWidgetPathSegment(InOldSegment, InNewSegment))
			{
				++ChangedCount;
			}
		}
	}
	return ChangedCount;
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


