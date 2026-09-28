// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "DreamWidgetPrivate.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Engine/World.h"
#include "DreamTweenManager.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#include "Components/SceneComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamPointerEventData.h"
#include "GameFramework/PlayerController.h"
// FLayoutLocalization, for the Culture flow-direction preference. SlateCore is already a public
// dependency; this is the one header of it that answers "which way does the running culture read".
#include "Layout/FlowDirection.h"
#if WITH_EDITOR
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#endif

void UDreamWidget::CollectChildrenWidgets(UDreamWidget* Target, TArray<UDreamWidget*>& OutAllChildrenWidgets, bool IncludeTarget)
{
	// A hole in Children is an ordinary state, not a corrupt one. Children is an Instanced UPROPERTY,
	// so the collector nulls an entry whose widget was destroyed (DestroyWidget marks the subtree as
	// garbage) while something else still held the array -- which is exactly what a Blueprint recompile
	// produces: the reinstancer's copy of a live widget shares the original's children, the original is
	// torn down, and the copy is left holding nulls. This walk used to follow them, and because it is
	// the walk RegisterDreamWidgetHierarchy, the compiler, the write-back and the editor tools all
	// share, one hole anywhere took the editor down from whichever of them ran first.
	//
	// IsValid rather than a null test: a widget already marked as garbage is on its way out, and no
	// caller of this wants to register, compile or write back something that is being destroyed.
	if (!IsValid(Target))
	{
		return;
	}
	if (IncludeTarget)
	{
		OutAllChildrenWidgets.Add(Target);
	}
	for (UDreamWidget* Child : Target->GetChildren())
	{
		CollectChildrenWidgets(Child, OutAllChildrenWidgets, true);
	}
}


void UDreamWidget::CalculateFlattenHierarchyIndex_Recursive(int& index)const
{
	if (this->FlattenHierarchyIndex != index)
	{
		this->FlattenHierarchyIndex = index;
	}
	EnsureUIChildrenSorted();
	for (auto& child : Children)
	{
		if (IsValid(child))
		{
			index++;
			child->CalculateFlattenHierarchyIndex_Recursive(index);
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("DreamWidget CalculateFlattenHierarchyIndex"), STAT_DreamWidgetCalculateFlattenHierarchyIndex, STATGROUP_DreamGUI);
void UDreamWidget::RecalculateFlattenHierarchyIndex()const
{
	SCOPE_CYCLE_COUNTER(STAT_DreamWidgetCalculateFlattenHierarchyIndex);

	this->bFlattenHierarchyIndexDirty = false;
	int tempIndex = this->FlattenHierarchyIndex;
	this->CalculateFlattenHierarchyIndex_Recursive(tempIndex);
}

int32 UDreamWidget::GetFlattenHierarchyIndex()const
{
	if (RootWidget.IsValid())
	{
		if (RootWidget->bFlattenHierarchyIndexDirty)
		{
			RootWidget->RecalculateFlattenHierarchyIndex();
		}
	}
	return this->FlattenHierarchyIndex;
}

void UDreamWidget::MarkFlattenHierarchyIndexDirty()
{
	if (RootWidget.IsValid())
	{
		RootWidget->bFlattenHierarchyIndexDirty = true;
	}
	//tell canvas to update
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->MarkCanvasHierarchyChanged();
		//if this DreamWidget have a DreamGUICanvas, then we need to tell the upper canvas that hierarchy order change, in order to sort render order between canvas
		if (this->bIsCanvasWidget)
		{
			if (RenderCanvas->GetParentCanvas().IsValid())
			{
				RenderCanvas->GetParentCanvas()->MarkCanvasHierarchyChanged();
			}
		}
	}
}



void UDreamWidget::ApplySiblingIndex()
{
	if (Parent.IsValid())
	{
		// Reordering rewrites the parent's persistent Children, so the parent is what has to be
		// snapshotted -- the moved child alone would leave undo with half the change.
		Parent->Modify();
		if (Parent->Children.Num() == 0)
		{
			Parent->Children.Add(this);
			if (SiblingIndex != 0)
			{
				this->SiblingIndex = 0;
				this->Call_SiblingIndexChanged();
			}
		}
		else
		{
			Parent->EnsureUIChildrenValid();
			Parent->EnsureUIChildrenSorted();
			// Clamp AFTER the remove and against Num(), not before it and against Num()-1, which is how
			// PostEditUndo has always spelled the same operation. EnsureUIChildrenValid can empty the
			// array outright -- an abnormally deleted actor or an undo leaves a parent whose every child
			// slot is invalid, this widget included -- and FMath::Clamp(x, 0, -1) answers -1 for any
			// x >= 0. TArray::Insert only checkSlow's its index, so Development and Shipping went on to
			// memmove from Data - 1 and store the pointer there: an out-of-bounds write into the heap.
			// With this widget present in the array the two spellings agree exactly (the remove drops
			// Num() by one), so nothing else about the ordering changes.
			Parent->Children.Remove(this);
			SiblingIndex = FMath::Clamp(SiblingIndex, 0, Parent->Children.Num());
			Parent->Children.Insert(this, SiblingIndex);
			bool anythingChange = false;
			for (int i = 0; i < Parent->Children.Num(); i++)
			{
				if (Parent->Children[i]->SiblingIndex != i)
				{
					Parent->Children[i]->SiblingIndex = i;
					Parent->Children[i]->Call_SiblingIndexChanged();
					anythingChange = true;
				}
			}
			//flatten hierarchy index
			if (anythingChange)
			{
				MarkFlattenHierarchyIndexDirty();
			}
		}
	}
	else
	{
		if (SiblingIndex != 0)
		{
			SiblingIndex = 0;
			this->Call_SiblingIndexChanged();
		}
	}
}

void UDreamWidget::SetAsFirstSibling()
{
	SetSiblingIndex(0);
}
void UDreamWidget::SetAsLastSibling()
{
	if (Parent.IsValid())
	{
		SetSiblingIndex(Parent->Children.Num() - 1);
	}
}

FString UDreamWidget::GetPathDisplayName(const UObject* StopOuter) const
{
	auto OuterPathName = GetOuter()->GetPathName(StopOuter);
	TStringBuilder<256> Result;
	Result.Append(OuterPathName);
	Result.AppendChar('/');
	TArray<const UDreamWidget*> WidgetChain;
	auto TempParent = this;
	while (TempParent != nullptr)
	{
		WidgetChain.Add(TempParent);
		TempParent = TempParent->GetParent();
	}
	for (int i = WidgetChain.Num() - 1; i >= 0; i--)
	{
		auto Widget = WidgetChain[i];
		Result.Append(Widget->GetDisplayName());
		if (i != 0)
		{
			Result.AppendChar('/');
		}
	}
	return Result.ToString();
}

UDreamWidget* UDreamWidget::FindChildByDisplayName(const FString& InName, bool IncludeChildren)const
{
	int indexOfFirstSlash;
	if (InName.FindChar('/', indexOfFirstSlash))
	{
		auto firstLayerName = InName.Left(indexOfFirstSlash);
		for (auto& childItem : Children)
		{
			if (!IsValid(childItem)) continue;//Children can hold nulls between a teardown and the next tidy-up
			if (childItem->DisplayName.Equals(firstLayerName, ESearchCase::CaseSensitive))
			{
				auto restName = InName.Right(InName.Len() - indexOfFirstSlash - 1);
				return childItem->FindChildByDisplayName(restName);
			}
		}
	}
	else
	{
		if (IncludeChildren)
		{
			return FindChildByDisplayNameWithChildren_Internal(InName);
		}
		else
		{
			for (auto& childItem : Children)
			{
				if (!IsValid(childItem)) continue;//Children can hold nulls between a teardown and the next tidy-up
				if (childItem->DisplayName.Equals(InName, ESearchCase::CaseSensitive))
				{
					return childItem;
				}
			}
		}
	}
	return nullptr;
}
UDreamWidget* UDreamWidget::FindChildByDisplayNameWithChildren_Internal(const FString& InName)const
{
	for (auto& childItem : Children)
	{
		if (!IsValid(childItem)) continue;//Children can hold nulls between a teardown and the next tidy-up
		if (childItem->DisplayName.Equals(InName, ESearchCase::CaseSensitive))
		{
			return childItem;
		}
		else
		{
			auto result = childItem->FindChildByDisplayNameWithChildren_Internal(InName);
			if (result)
			{
				return result;
			}
		}
	}
	return nullptr;
}
TArray<UDreamWidget*> UDreamWidget::FindChildArrayByDisplayName(const FString& InName, bool IncludeChildren)const
{
	TArray<UDreamWidget*> resultArray;
	int indexOfLastSlash;
	if (InName.FindLastChar('/', indexOfLastSlash))
	{
		auto parentLayerName = InName.Left(indexOfLastSlash);
		auto parentItem = this->FindChildByDisplayName(parentLayerName, false);
		if (IsValid(parentItem))
		{
			auto matchName = InName.Right(InName.Len() - indexOfLastSlash - 1);
			return parentItem->FindChildArrayByDisplayName(matchName, IncludeChildren);
		}
	}
	else
	{
		if (IncludeChildren)
		{
			FindChildArrayByDisplayNameWithChildren_Internal(InName, resultArray);
		}
		else
		{
			EnsureUIChildrenSorted();//make sure sorted, so result is predictable
			for (auto& childItem : Children)
			{
				if (!IsValid(childItem)) continue;//Children can hold nulls between a teardown and the next tidy-up
				if (childItem->DisplayName.Equals(InName, ESearchCase::CaseSensitive))
				{
					resultArray.Add(childItem);
				}
			}
		}
	}
	return resultArray;
}
void UDreamWidget::FindChildArrayByDisplayNameWithChildren_Internal(const FString& InName, TArray<UDreamWidget*>& OutResultArray)const
{
	EnsureUIChildrenSorted();//make sure sorted, so result is predictable
	for (auto& childItem : Children)
	{
		if (!IsValid(childItem)) continue;//Children can hold nulls between a teardown and the next tidy-up
		if (childItem->DisplayName.Equals(InName, ESearchCase::CaseSensitive))
		{
			OutResultArray.Add(childItem);
		}
		else
		{
			childItem->FindChildArrayByDisplayNameWithChildren_Internal(InName, OutResultArray);
		}
	}
}

void UDreamWidget::SetParentBeforeRegister(UDreamWidget* InParent)
{
	if (!ensureMsgf(!HasRegistered(), TEXT("%s: SetParentBeforeRegister on a registered widget; SetParent is the call for that."), *GetPathName()))
	{
		return;
	}
	if (InParent == this || (IsValid(InParent) && InParent->IsChildOf(this)))
	{
		ensureMsgf(false, TEXT("Cannot restore cyclic DreamWidget parent relationship for %s."), *GetPathName());
		return;
	}
	if (Parent != InParent)
	{
		if (Parent.IsValid() && IsValid(PanelSlot))
		{
			PanelSlot->RestoreAuthoredGeometry();
			PanelSlot->InvalidateAuthoredGeometry();
		}
		if (Parent.IsValid())
		{
			Parent->Children.Remove(this);
		}
		Parent = InParent;
		if (Parent.IsValid())
		{
			Parent->Children.Add(this);
			// Say the index the append just produced, exactly as TrySetParentInternal's append branch
			// does. Without it a widget attached this way kept SiblingIndex == INDEX_NONE while sitting
			// LAST in Children -- the two disagreeing about the same thing, with the array (last = drawn
			// on top) saying what the callers wanted and the index (-1) saying the opposite. Nothing read
			// the index until something re-sorted the parent (RestoreParentLinksRecursive,
			// EnsureDataForRebuild -- a preview rebuild or an undo in the designer), and then -1 sorted
			// FIRST: the modal dim, the tooltip, the drag visual and the virtual cursor all went to the
			// bottom of the stack, and the renumber that followed made it permanent.
			this->SiblingIndex = Parent->Children.Num() - 1;
		}
	}
}

void UDreamWidget::RestoreSiblingIndex(int32 InSiblingIndex)
{
	SiblingIndex = InSiblingIndex;
	if (Parent.IsValid())
	{
		Parent->bNeedSortUIChildren = true;
	}
}

int32 UDreamWidget::GetMaxChildrenCapacity() const
{
	int32 Capacity = INDEX_NONE;
	auto ApplyLimit = [&Capacity](int32 Limit)
	{
		if (Limit >= 0)
		{
			Capacity = Capacity == INDEX_NONE ? Limit : FMath::Min(Capacity, Limit);
		}
	};
	if (IsValid(LayoutContainer))
	{
		ApplyLimit(LayoutContainer->GetMaxChildren());
	}
	for (const UDreamUIBehaviour* Component : Components)
	{
		if (IsValid(Component))
		{
			ApplyLimit(Component->GetMaxWidgetChildren());
		}
	}
	return Capacity;
}

bool UDreamWidget::CanAcceptAdditionalChildren(int32 AdditionalChildCount) const
{
	if (AdditionalChildCount <= 0)
	{
		return true;
	}
	const int32 Capacity = GetMaxChildrenCapacity();
	if (Capacity == INDEX_NONE)
	{
		return true;
	}
	int32 CurrentChildCount = 0;
	for (const UDreamWidget* Child : Children)
	{
		CurrentChildCount += IsValid(Child) ? 1 : 0;
	}
	return CurrentChildCount <= Capacity && AdditionalChildCount <= Capacity - CurrentChildCount;
}

bool UDreamWidget::CanAcceptChildren(TConstArrayView<UDreamWidget*> InChildren) const
{
	TSet<const UDreamWidget*> UniqueCandidates;
	for (const UDreamWidget* Candidate : InChildren)
	{
		if (!IsValid(Candidate) || Candidate == this || IsChildOf(Candidate))
		{
			return false;
		}
		UniqueCandidates.Add(Candidate);
	}

	const int32 Capacity = GetMaxChildrenCapacity();
	if (Capacity == INDEX_NONE)
	{
		return true;
	}
	TSet<const UDreamWidget*> ProjectedChildren;
	for (const UDreamWidget* ExistingChild : Children)
	{
		if (IsValid(ExistingChild))
		{
			ProjectedChildren.Add(ExistingChild);
		}
	}
	for (const UDreamWidget* Candidate : UniqueCandidates)
	{
		ProjectedChildren.Add(Candidate);
	}
	return ProjectedChildren.Num() <= Capacity;
}

bool UDreamWidget::CanAcceptChild(const UDreamWidget* InChild) const
{
	UDreamWidget* Candidate = const_cast<UDreamWidget*>(InChild);
	return CanAcceptChildren(MakeArrayView(&Candidate, 1));
}

void UDreamWidget::SetParent(UDreamWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex)
{
	TrySetParent(InParent, InKeepWorldPosition, InSiblingIndex);
}

bool UDreamWidget::TrySetParent(UDreamWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex)
{
	return TrySetParentInternal(InParent, InKeepWorldPosition, InSiblingIndex, true);
}

bool UDreamWidget::SetParentIgnoringCapacity(UDreamWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex)
{
	return TrySetParentInternal(InParent, InKeepWorldPosition, InSiblingIndex, false);
}

bool UDreamWidget::TrySetParentInternal(UDreamWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex, bool bEnforceCapacity)
{
	if (IsValid(InParent))//attach to parent
	{
		if (this == InParent)
		{
			ensureMsgf(false, TEXT("A DreamWidget cannot be parented to itself: %s"), *GetPathName());
			return false;
		}
		if (this->Parent == InParent)
		{
			if (InSiblingIndex >= 0)
			{
				SetSiblingIndex(InSiblingIndex);
			}
			// A reorder, not a move, so the authored geometry is NOT recaptured. Forcing it promoted
			// whatever the last layout pass had squeezed the child into to "what the author asked for"
			// -- silently, and from there into the saved asset. AddChild's same-parent branch refuses to
			// route through here for exactly this reason; the unforced call still creates a slot if the
			// parent has since become a panel, and is a no-op on a slot that already has its geometry.
			SynchronizePanelSlotForParent(InParent, this, /*bRecaptureDesiredSize*/false);
			return true;
		}
		if (InParent->IsChildOf(this))return false;
		if (InParent->Children.Contains(this))return false;
		if (bEnforceCapacity && !InParent->CanAcceptChild(this))return false;
		const FTransform OldObjectToWorldTransform = this->GetLayoutWorldTransform();
		const FVector PreviousAuthoredScale = RelativeScale;
		const FVector2f PreviousLayoutScale = LayoutScale;
		bIsAttaching = true;
		if (Parent.IsValid())
		{
			TrySetParent(nullptr, false);
		}
		bIsAttaching = false;
		// Children is the persistent hierarchy now, so the parent has to be snapshotted before it is
		// written or undo restores a tree the parent never agreed to. It was already wrong to omit this
		// (the 2026-08-19 editor review, B3); while Children was Transient the transaction buffer simply
		// never saw the damage.
		InParent->Modify();
		this->Modify();
		if (InSiblingIndex == -1 || !InParent->Children.IsValidIndex(InSiblingIndex))
		{
			InParent->Children.Add(this);
			this->SiblingIndex = InParent->Children.Num() - 1;
			this->Call_SiblingIndexChanged();
		}
		else
		{
			InParent->Children.Insert(this, InSiblingIndex);
			for (int i = InSiblingIndex; i < InParent->Children.Num(); i++)
			{
				auto Child = InParent->Children[i];
				if (!IsValid(Child)) continue;
				const bool bIndexChanged = Child->SiblingIndex != i;
				Child->SiblingIndex = i;
				// Displaced siblings observe their move, matching every other renumber path
				// (ApplySiblingIndex, OnChildDetached).
				if (bIndexChanged && Child != this)
				{
					Child->Call_SiblingIndexChanged();
				}
			}
			this->Call_SiblingIndexChanged();
		}
		this->Parent = InParent;
		if (InKeepWorldPosition)
		{
			const FTransform LocalTransform = OldObjectToWorldTransform.GetRelativeTransform(InParent->GetLayoutWorldTransform());
			this->RelativeLocation = LocalTransform.GetLocation();
			this->RelativeRotation = LocalTransform.GetRotation();
			this->RelativeRotationEuler = this->RelativeRotation.Rotator();
			this->RelativeScale = LocalTransform.GetScale3D();
			if (FMath::IsNearlyZero(PreviousLayoutScale.X)) this->RelativeScale.Y = PreviousAuthoredScale.Y;
			if (FMath::IsNearlyZero(PreviousLayoutScale.Y)) this->RelativeScale.Z = PreviousAuthoredScale.Z;
		}
		this->CalculateObjectToWorldTransform();
		this->OnAttachedToParent();
		SynchronizePanelSlotForParent(InParent, this, true);
		InParent->OnChildAttached(this);
		return true;
	}
	else//detach from parent
	{
		if (this->Parent == nullptr)return true;
		auto OldParent = this->Parent;
		// Same reason as the attach branch: the removal below edits the parent's persistent Children.
		OldParent->Modify();
		this->Modify();
		// Layout's basis again -- detaching with keep-world-position writes straight into
		// RelativeLocation, so the drawn transform must not be what gets written.
		const FTransform OldObjectToWorldTransform = this->GetLayoutWorldTransform();
		const FVector PreviousAuthoredScale = RelativeScale;
		const FVector2f PreviousLayoutScale = LayoutScale;
		RemovePanelSlotFromChild(this);
		SetLayoutVisibilitySuppressed(false);
		SetLayoutScale(FVector2f::UnitVector);
		const FVector PreviousRelativeLocation = RelativeLocation;
		const FQuat PreviousRelativeRotation = RelativeRotation;
		const FVector PreviousRelativeScale = RelativeScale;
		this->Parent->Children.Remove(this);
		this->Parent = nullptr;
		this->OnDetachedFromParent();
		if (InKeepWorldPosition)
		{
			FTransform LocalTransform = OldObjectToWorldTransform;
			if (const USceneComponent* WidgetPresenterComponent = GetAttachedRootSceneComponent())
			{
				LocalTransform = OldObjectToWorldTransform.GetRelativeTransform(WidgetPresenterComponent->GetComponentTransform());
			}
			this->RelativeLocation = LocalTransform.GetLocation();
			this->RelativeRotation = LocalTransform.GetRotation();
			this->RelativeScale = LocalTransform.GetScale3D();
			if (FMath::IsNearlyZero(PreviousLayoutScale.X)) this->RelativeScale.Y = PreviousAuthoredScale.Y;
			if (FMath::IsNearlyZero(PreviousLayoutScale.Y)) this->RelativeScale.Z = PreviousAuthoredScale.Z;
		}
		else
		{
			this->RelativeLocation = PreviousRelativeLocation;
			this->RelativeRotation = PreviousRelativeRotation;
			this->RelativeScale = PreviousRelativeScale;
		}
		this->RelativeRotationEuler = this->RelativeRotation.Rotator();
		this->CalculateObjectToWorldTransform();
		if (HasRegistered())
		{
			CalculateAnchorFromTransform();
		}
		if (OldParent.IsValid())
		{
			OldParent->OnChildDetached(this);
		}
		return true;
	}
}

void UDreamWidget::SetSiblingIndex(int32 InInt)
{
	if (InInt != SiblingIndex)
	{
		SiblingIndex = InInt;
		// Apply (clamp + rearrange) BEFORE broadcasting, so observers see the settled index and the
		// rearranged children array instead of a raw, possibly out-of-range request.
		ApplySiblingIndex();
		this->Call_SiblingIndexChanged();
		MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
	}
}

bool UDreamWidget::ReorderChildrenToPaintOrder(const TArray<UDreamWidget*>& InDesiredOrder)
{
	// A permutation of exactly the current array, or nothing: this bypasses the invalidation the normal
	// reorder raises, so it must not be usable to add, drop or substitute a child by accident.
	if (InDesiredOrder.Num() != Children.Num())
	{
		return false;
	}
	bool bOrderChanged = false;
	for (int32 Index = 0; Index < InDesiredOrder.Num(); ++Index)
	{
		if (Children[Index] != InDesiredOrder[Index])
		{
			bOrderChanged = true;
			break;
		}
	}
	if (!bOrderChanged)
	{
		return false;
	}
	{
		TSet<const UDreamWidget*> Requested;
		Requested.Reserve(InDesiredOrder.Num());
		for (const UDreamWidget* Child : InDesiredOrder)
		{
			Requested.Add(Child);
		}
		if (Requested.Num() != InDesiredOrder.Num())
		{
			return false;//a duplicate, so not a permutation
		}
		for (const TObjectPtr<UDreamWidget>& Child : Children)
		{
			if (!Requested.Contains(Child.Get()))
			{
				return false;
			}
		}
	}

	// Same reason ApplySiblingIndex snapshots the parent: Children is persistent, and this rewrites it.
	Modify();
	Children.Reset(InDesiredOrder.Num());
	for (UDreamWidget* Child : InDesiredOrder)
	{
		Children.Add(Child);
	}
	// The array is now exactly the order the indices are about to state, so nothing is owed a sort.
	bNeedSortUIChildren = false;
	for (int32 Index = 0; Index < Children.Num(); ++Index)
	{
		UDreamWidget* Child = Children[Index];
		if (!IsValid(Child) || Child->SiblingIndex == Index)
		{
			continue;
		}
		Child->SiblingIndex = Index;
		Child->Call_SiblingIndexChanged();
	}
	// Draw order is derived from the flattened hierarchy index, and that is what actually moved.
	MarkFlattenHierarchyIndexDirty();
	return true;
}

bool UDreamWidget::IsChildOf(const UDreamWidget* InTarget)const
{
	auto TempParent = this->Parent;
	FDreamVisitedWidgetSet VisitedWidgets;
	while (TempParent.IsValid())
	{
		if (TempParent == InTarget)
		{
			return true;
		}
		if (VisitedWidgets.Contains(TempParent.Get()))
		{
			return false;
		}
		VisitedWidgets.Add(TempParent.Get());
		TempParent = TempParent->Parent;
	}
	return false;
}

void UDreamWidget::OnChildAttached(UDreamWidget* ChildWidget)
{
	// No "if SiblingIndex == INDEX_NONE then renumber everything" fallback here any more: the sole
	// caller is TrySetParentInternal, and both of its attach branches assign the child's SiblingIndex
	// before calling this, so the condition could not be true. It read as a safety net that was in fact
	// never armed, which is worse than not having one.
	for (UDreamUIBehaviour* Component : Components)
	{
		if (IsValid(Component)) Component->OnWidgetChildAttached(ChildWidget);
	}

	MarkCanvasUpdate(false);
}

UDreamPanelSlot* UDreamWidget::AddChild(UDreamWidget* InChild, int32 InSiblingIndex)
{
	if (!IsValid(InChild) || InChild == this)
	{
		return nullptr;
	}
	if (InChild->GetParent() == this)
	{
		// A reorder, not a move. Deliberately NOT routed through TrySetParent: its same-parent branch
		// forces CaptureAuthoredGeometry(true), which would promote whatever the last layout pass
		// arranged into the authored geometry -- silently, and then into the saved prefab. The
		// unforced call below still creates a slot if the parent has since become a panel, and is a
		// no-op on a slot that already has its geometry.
		if (InSiblingIndex >= 0)
		{
			InChild->SetSiblingIndex(InSiblingIndex);
		}
		SynchronizePanelSlotForParent(this, InChild, /*bRecaptureDesiredSize*/false);
		return InChild->GetPanelSlot();
	}
	// KeepWorldPosition false: a widget being added to a panel is being handed over to that panel's
	// arrangement, so preserving its old screen position would only fight the first layout pass.
	if (!InChild->TrySetParent(this, /*InKeepWorldPosition*/false, InSiblingIndex))
	{
		return nullptr;
	}
	return InChild->GetPanelSlot();
}

bool UDreamWidget::RemoveChild(UDreamWidget* InChild)
{
	if (!IsValid(InChild) || InChild->GetParent() != this)
	{
		return false;
	}
	if (!InChild->TrySetParent(nullptr, /*InKeepWorldPosition*/false))
	{
		return false;
	}
	// Back to the state a freshly created widget is in. Deliberately done here and not in the
	// general detach path: a move between parents detaches on its way through, and parking there
	// would disable the widget's behaviours for the width of an operation that is not a removal.
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		DreamUIManager->ParkWidget(InChild);
	}
	return true;
}

bool UDreamWidget::RemoveFromParent()
{
	// A tracked screen page first: it has a NAME in the subsystem as well as a parent, and detaching
	// it without giving the name back leaves an entry pointing at a page that is registered, alive
	// and nowhere. ForgetPage keeps the widget, unlike RemoveFromViewport.
	if (UWorld* World = GetWorld())
	{
		if (UDreamScreenUISubsystem* Screen = World->GetSubsystem<UDreamScreenUISubsystem>())
		{
			if (Screen->IsInViewport(this))
			{
				Screen->ForgetPage(this);
			}
		}
	}
	UDreamWidget* CurrentParent = GetParent();
	return CurrentParent != nullptr ? CurrentParent->RemoveChild(this) : false;
}

bool UDreamWidget::RemoveChildAt(int32 InIndex)
{
	const TArray<UDreamWidget*>& CurrentChildren = GetChildren();
	return CurrentChildren.IsValidIndex(InIndex) ? RemoveChild(CurrentChildren[InIndex]) : false;
}

bool UDreamWidget::DestroyChild(UDreamWidget* InChild)
{
	if (!IsValid(InChild) || InChild->GetParent() != this)
	{
		return false;
	}
	InChild->DestroyWidget();//detaches itself on the way down
	return true;
}

void UDreamWidget::DestroyAllChildren()
{
	// A copy, not the live array: each teardown removes its widget from Children, so iterating the
	// original would skip every other child and read off the end.
	const TArray<UDreamWidget*> ChildrenSnapshot = GetChildren();
	for (UDreamWidget* Child : ChildrenSnapshot)
	{
		if (IsValid(Child))
		{
			Child->DestroyWidget();
		}
	}
}

int32 UDreamWidget::GetChildIndex(const UDreamWidget* InChild)const
{
	return InChild != nullptr ? GetChildren().IndexOfByKey(InChild) : INDEX_NONE;
}

bool UDreamWidget::HasChild(const UDreamWidget* InChild)const
{
	return InChild != nullptr && InChild->GetParent() == this;
}

bool UDreamWidget::HasAnyChildren()const
{
	for (const UDreamWidget* Child : Children)
	{
		if (IsValid(Child))
		{
			return true;//Children can hold nulls between a teardown and the next tidy-up
		}
	}
	return false;
}

bool UDreamWidget::HasPanelSlots()const
{
	return IsValid(Cast<UDreamPanelLayoutBase>(LayoutContainer));
}

void UDreamWidget::OnAttachedToParent()
{
	// Getting a parent is what ends the not-yet-added state, whichever verb did it -- AddChild,
	// TrySetParent, or the screen subsystem. Widgets the prefab loader attaches were never parked,
	// so this is a lookup miss for all of them.
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		DreamUIManager->UnparkWidget(this);
		// ...and what ends its time in the pool of roots nobody hosts: its parent holds it now.
		DreamUIManager->ForgetFreeRoot(this);
	}
	RefreshPerspectiveInHierarchy();//a new parent can put this subtree inside a perspective scope
	RefreshShearInHierarchy();//...and, the same way, inside a sheared one
	if (this->HasRegistered())//registered means the hierarchy is live, not still being assembled
	{
		Call_TransformChanged();
		CalculateAnchorFromTransform();//a live attach has to derive anchors from the transform so KeepRelative/KeepWorld hold; while a tree is being assembled the serialized anchors are already right
	}

	UDreamCanvas* ParentCanvas = this->GetComponentInParent<UDreamCanvas>();
	OnHierarchyAttachmentChanged(ParentCanvas, Parent->RootWidget.Get());

	CalculateWidgetActive_Recursive();
	CalculateVisibility_Recursive();
	CalculateRaycastable_Recursive();
	CalculateInteractable_Recursive();
	
	// MarkLayoutForRebuild(this);//why comment this? because it already called in OnHierarchyAttachmentChanged
	MarkClipDirty(true);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
#if WITH_EDITOR
		DreamUIManager->MarkDreamUIWidgetOutlinerChanged();
#endif
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
}

void UDreamWidget::OnChildDetached(UDreamWidget* ChildWidget)
{
	// Drop the dead slots before renumbering. This loop once ran unguarded: deleting a widget
	// blueprint instance from the designer tears its contents down first, and the detach that
	// follows walked a Children array holding an entry the teardown had already emptied -- an
	// access violation reading SiblingIndex off a null, taking the editor with it. Removing rather
	// than skipping is also what makes the renumbering below mean anything: a hole would leave
	// every sibling after it numbered one past its own position. OnChildAttached does the same.
	EnsureUIChildrenValid();
	for (int i = 0; i < Children.Num(); i++)
	{
		auto& UIChild = Children[i];
		if (UIChild->SiblingIndex != i)
		{
			UIChild->SiblingIndex = i;
			UIChild->Call_SiblingIndexChanged();
		}
	}
	for (UDreamUIBehaviour* Component : Components)
	{
		if (IsValid(Component)) Component->OnWidgetChildDetached(ChildWidget);
	}
	MarkLayoutForRebuild(this);//child removed, so need to rebuild layout
}

void UDreamWidget::OnDetachedFromParent()
{
	if (bIsAttaching)
	{
		return;
	}
	OnHierarchyAttachmentChanged(nullptr, nullptr);

	CalculateWidgetActive_Recursive();
	CalculateVisibility_Recursive();
	CalculateRaycastable_Recursive();
	CalculateInteractable_Recursive();

	// MarkLayoutForRebuild(this);//why comment this? because it already called in OnHierarchyAttachmentChanged
	MarkClipDirty(true);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
#if WITH_EDITOR
		DreamUIManager->MarkDreamUIWidgetOutlinerChanged();
#endif
		DreamUIManager->MarkRebuildAllLayoutTree();
		// A registered widget with no parent has nobody holding it but the manager: its old parent let it
		// go, and whatever hosts the tree it is outered to holds only that tree's root.
		if (HasRegistered())
		{
			DreamUIManager->AdoptIfFreeRoot(this);
		}
	}
}

void UDreamWidget::EnsureUIChildrenValid()
{
	for (int i = Children.Num() - 1; i >= 0; i--)
	{
		if (!IsValid(Children[i]))
		{
			Children.RemoveAt(i);
		}
	}
}

namespace DreamWidgetDuplicateLocal
{
	/** Widget pair by pair, every sub-object a widget owns: what "the same part, other tree" means. */
	void MapCounterparts(UDreamWidget* InSource, UDreamWidget* InCopy, TMap<UObject*, UObject*>& OutMap)
	{
		OutMap.Add(InSource, InCopy);
		if (InSource->GetVisual() != nullptr && InCopy->GetVisual() != nullptr)
		{
			OutMap.Add(InSource->GetVisual(), InCopy->GetVisual());
		}
		if (InSource->GetLayoutContainer() != nullptr && InCopy->GetLayoutContainer() != nullptr)
		{
			OutMap.Add(InSource->GetLayoutContainer(), InCopy->GetLayoutContainer());
		}
		if (InSource->GetPanelSlot() != nullptr && InCopy->GetPanelSlot() != nullptr)
		{
			OutMap.Add(InSource->GetPanelSlot(), InCopy->GetPanelSlot());
		}
		const TArray<UDreamUIBehaviour*>& SourceComponents = InSource->GetAllComponents();
		const TArray<UDreamUIBehaviour*>& CopyComponents = InCopy->GetAllComponents();
		// By index: Components is Instanced, so the copy's array is the source's in order, and only
		// the index tells two behaviours of one class apart.
		const int32 Count = FMath::Min(SourceComponents.Num(), CopyComponents.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (SourceComponents[Index] != nullptr && CopyComponents[Index] != nullptr
				&& SourceComponents[Index]->GetClass() == CopyComponents[Index]->GetClass())
			{
				OutMap.Add(SourceComponents[Index], CopyComponents[Index]);
			}
		}
	}

	/**
	 * Aim every reference InContainer holds into the source subtree at its counterpart in the copy -- in its
	 * own properties and inside every struct and container of them. An event binding's target widget lives
	 * in a struct in an array, and walking only the top level left a copied button calling the original's
	 * target. Map keys and set elements are left as they are: rewriting one in place would leave its
	 * container hashed by the old value.
	 */
	void RemapReferencesOn(UObject* InContainer, const TMap<UObject*, UObject*>& InMap)
	{
		if (!IsValid(InContainer))
		{
			return;
		}
		for (TPropertyValueIterator<FObjectPropertyBase> It(InContainer->GetClass(), InContainer); It; ++It)
		{
			const FObjectPropertyBase* Property = It.Key();
			if (Property->IsA<FSoftObjectProperty>() || Property->IsA<FClassProperty>())
			{
				continue;
			}
			const FMapProperty* OwningMap = Property->GetOwner<FMapProperty>();
			if (Property->GetOwner<FSetProperty>() != nullptr || (OwningMap != nullptr && OwningMap->KeyProp == Property))
			{
				continue;
			}
			void* ValueAddress = const_cast<void*>(It.Value());
			UObject* Value = Property->GetObjectPropertyValue(ValueAddress);
			if (Value == nullptr)
			{
				continue;
			}
			if (UObject* const* Counterpart = InMap.Find(Value))
			{
				Property->SetObjectPropertyValue(ValueAddress, *Counterpart);
			}
		}
	}
}

UDreamWidget* UDreamWidget::DuplicateSubtree(UObject* InOuter, UDreamWidget* InSource)
{
	if (InOuter == nullptr || !IsValid(InSource))
	{
		return nullptr;
	}
	struct LOCAL
	{
		static UDreamWidget* Walk(UObject* InOuter, UDreamWidget* InSource, TSet<const UDreamWidget*>& InVisited)
		{
			bool bAlreadyVisited = false;
			InVisited.Add(InSource, &bAlreadyVisited);
			if (bAlreadyVisited)
			{
				// The same guard RestoreParentLinksRecursive keeps: a malformed Children array would
				// recurse forever here rather than fail somewhere legible.
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cycle in Children reached widget '%s'; not duplicating it twice."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InSource->GetPathDisplayName());
				return nullptr;
			}

			// Flags from the source, not fixed here: a copy of a transient preview widget must not
			// become a saveable one, a copy of a widget the level editor keeps out of every copy of
			// the level must stay out too, and a copy of an authored widget has to stay
			// transactional or undo cannot reach it -- but only where undo keeps what it is copied
			// into, or an authored template copied into a tree made at run time would bring undo
			// into that tree with it.
			const EObjectFlags Transactional = InSource->HasAnyFlags(RF_Transactional) && DreamUI::IsKeptByUndo(*InOuter)
				? RF_Transactional : RF_NoFlags;
			FObjectInstancingGraph InstancingGraph;
			UDreamWidget* Copy = NewObject<UDreamWidget>(InOuter, InSource->GetClass(), NAME_None,
				InSource->GetMaskedFlags(RF_Public | DreamUI::RuntimeObjectFlags) | Transactional,
				InSource, /*bCopyTransientsFromClassDefaults*/false, &InstancingGraph);
			if (!IsValid(Copy))
			{
				return nullptr;
			}
			// Whatever instancing put in Children is not the hierarchy -- see the header. Rebuilt below
			// from the source's array, which is the structural truth.
			Copy->Children.Reset();
			Copy->Parent = nullptr;
			// A copy is a different widget. Instancing brought the source's id across with everything
			// else, and leaving it would make the designer pair the copy's preview with the ORIGINAL's
			// authored widget -- so an edit to either would land on the other. Same shape as the
			// Children array above: what instancing gives you is not what identity means here.
			Copy->AssignNewWidgetGuid();

			for (const TObjectPtr<UDreamWidget>& Child : InSource->Children)
			{
				if (!IsValid(Child))
				{
					continue;
				}
				if (UDreamWidget* ChildCopy = Walk(InOuter, Child, InVisited))
				{
					Copy->Children.Add(ChildCopy);
				}
			}
			return Copy;
		}
	};
	TSet<const UDreamWidget*> Visited;
	UDreamWidget* Copy = LOCAL::Walk(InOuter, InSource, Visited);
	if (IsValid(Copy))
	{
		// Parent is transient, so the copies arrive with the structure intact and every back-pointer
		// empty. OnRegister reads Parent, so nothing may register before this.
		Copy->RestoreParentLinksRecursive();

		// Re-aim every pointer that runs from one part of the subtree to another. Instancing follows
		// only Instanced properties, so a plain or weak reference at a sibling -- a toggle's tick, a
		// dropdown item's label, a slider's fill -- was copied VERBATIM and still points into the
		// SOURCE subtree. Every copy then drives the original's parts: the measured symptom was a
		// dropdown whose duplicated rows all wrote their text onto the template's one label, each row
		// on screen wearing whatever the template said at the moment it was copied, off by one.
		//
		// The same defect InitializeWidgetStatic re-aims per instance, one layer down: this is the
		// duplicate path's half, and it lives in the one choke point every duplication goes through --
		// list cells, dropdown rows, tooltips, drag visuals.
		//
		// Structure-parallel rather than by name: the copy's tree IS the source's tree in order, by
		// construction three lines up, and names need not be unique across a subtree.
		TMap<UObject*, UObject*> SourceToCopy;
		struct FPairWalk
		{
			static void Walk(UDreamWidget* InFrom, UDreamWidget* InTo, TMap<UObject*, UObject*>& OutMap)
			{
				DreamWidgetDuplicateLocal::MapCounterparts(InFrom, InTo, OutMap);
				// Both sides are read as RAW arrays, because that is how the copy was built: Walk above
				// appends to Copy->Children while iterating InSource->Children, unsorted, skipping the
				// source's invalid entries. Pairing used to ask GetChildren() instead, which sorts BOTH
				// sides by SiblingIndex -- and SiblingIndex is not unique: EnsureUIChildrenSorted's own
				// comment records duplicates in legacy data and transiently during a prefab refresh. A
				// stable sort preserves the incoming order for equal keys, the two incoming orders are
				// different (one sorted, one not), so the walks diverged and paired a copy with the wrong
				// original -- after which RemapReferencesOn aimed every intra-subtree reference at it.
				int32 ToIndex = 0;
				for (const TObjectPtr<UDreamWidget>& FromChild : InFrom->Children)
				{
					if (!IsValid(FromChild))
					{
						continue;
					}
					if (!InTo->Children.IsValidIndex(ToIndex))
					{
						break;
					}
					Walk(FromChild, InTo->Children[ToIndex++], OutMap);
				}
			}
		};
		FPairWalk::Walk(InSource, Copy, SourceToCopy);
		for (const TPair<UObject*, UObject*>& Pair : SourceToCopy)
		{
			DreamWidgetDuplicateLocal::RemapReferencesOn(Pair.Value, SourceToCopy);
		}
	}
	return Copy;
}

void UDreamWidget::RestoreParentLinksRecursive()
{
	// A cycle here would hang the walk rather than assert somewhere useful later, and a persisted
	// Children array is exactly the kind of data that can arrive malformed (hand-edited asset, a
	// partial migration). Bail on revisit and report, rather than spin.
	TSet<UDreamWidget*> Visited;
	struct LOCAL
	{
		static void Walk(UDreamWidget* Widget, TSet<UDreamWidget*>& InVisited)
		{
			bool bAlreadyVisited = false;
			InVisited.Add(Widget, &bAlreadyVisited);
			if (bAlreadyVisited)
			{
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cycle in Children reached widget '%s'; hierarchy is malformed."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Widget->GetPathDisplayName());
				return;
			}
			for (int32 i = Widget->Children.Num() - 1; i >= 0; i--)
			{
				UDreamWidget* Child = Widget->Children[i];
				if (!IsValid(Child))
				{
					Widget->Children.RemoveAt(i);
					continue;
				}
				Child->Parent = Widget;
				Walk(Child, InVisited);
			}
			Widget->bNeedSortUIChildren = true;
		}
	};
	LOCAL::Walk(this, Visited);
}

void UDreamWidget::EnsureUIChildrenSorted()const
{
	if (bNeedSortUIChildren)
	{
		bNeedSortUIChildren = false;
		// StableSort: duplicate SiblingIndex values exist in legacy data and transiently during prefab
		// refresh. An unstable sort made equal-key children swap places on every RefreshAllUI (IntroSort's
		// small-array selection sort deterministically flips the tail pair each pass) — visible as widgets
		// trading positions after every editor refresh. Equal keys must keep their current order.
		Children.StableSort([](const UDreamWidget& A, const UDreamWidget& B)
			{
				return A.GetSiblingIndex() < B.GetSiblingIndex();
			});
	}
}

void UDreamWidget::CheckRootWidget(UDreamWidget* RootWidgetInParent)
{
	if (RootWidgetInParent == nullptr)
	{
		UDreamWidget* TopWidget = this;
		UDreamWidget* TempRootWidget = nullptr;
		while (TopWidget != nullptr)
		{
			TempRootWidget = TopWidget;
			TopWidget = TopWidget->GetParent();
		}
		RootWidgetInParent = TempRootWidget;
	}
	RootWidget = RootWidgetInParent;
}

UDreamWidget* UDreamWidget::GetChildByIndex(int index)const
{
	if (index < 0 || index >= Children.Num())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Index:%d out of range[%d, %d]"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, index, 0, Children.Num() - 1);
		return nullptr;
	}
	EnsureUIChildrenSorted();
	return Children[index];
}
