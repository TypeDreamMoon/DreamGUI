// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "DreamTweenManager.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#include "Event/DreamEventSystem.h"
#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#include "Components/SceneComponent.h"
#include "Core/DreamUIBehaviour.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

namespace
{
	void RemovePanelSlotFromChild(UDreamWidget* ChildWidget)
	{
		if (!IsValid(ChildWidget) || !IsValid(ChildWidget->GetPanelSlot()))
		{
			return;
		}
#if WITH_EDITOR
		if (const UWorld* World = ChildWidget->GetWorld(); !World || !World->IsGameWorld())
		{
			ChildWidget->Modify();
		}
#endif
		ChildWidget->RemovePanelSlot();
	}

	bool EnsurePanelSlotForChild(UDreamWidget* ParentWidget, UDreamWidget* ChildWidget, bool bRecaptureDesiredSize = false)
	{
		if (!IsValid(ParentWidget) || !IsValid(ChildWidget)
			|| !IsValid(Cast<UDreamPanelLayoutBase>(ParentWidget->GetLayoutContainer())))
		{
			return false;
		}
		if (UDreamPanelSlot* ExistingSlot = ChildWidget->GetPanelSlot(); IsValid(ExistingSlot))
		{
			if (bRecaptureDesiredSize)
			{
				ExistingSlot->CaptureAuthoredGeometry(true);
			}
			else
			{
				ExistingSlot->CaptureAuthoredGeometry();
			}
			return false;
		}
#if WITH_EDITOR
		if (const UWorld* World = ChildWidget->GetWorld(); !World || !World->IsGameWorld())
		{
			ChildWidget->Modify();
		}
#endif
		UDreamPanelSlot* NewSlot = ChildWidget->CreateNewPanelSlot<UDreamPanelSlot>();
		if (IsValid(NewSlot))
		{
			if (ParentWidget->GetLayoutContainer()->IsA<UDreamLayoutContainerScaleBox>())
			{
				NewSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
				NewSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
			}
			NewSlot->CaptureAuthoredGeometry(bRecaptureDesiredSize);
		}
		return IsValid(NewSlot);
	}

	void SynchronizePanelSlotForParent(UDreamWidget* ParentWidget, UDreamWidget* ChildWidget,
		bool bRecaptureDesiredSize = false)
	{
		if (IsValid(ParentWidget)
			&& IsValid(Cast<UDreamPanelLayoutBase>(ParentWidget->GetLayoutContainer())))
		{
			EnsurePanelSlotForChild(ParentWidget, ChildWidget, bRecaptureDesiredSize);
		}
		else
		{
			RemovePanelSlotFromChild(ChildWidget);
		}
	}
}

UDreamWidget::UDreamWidget()
{
	bFlattenHierarchyIndexDirty = true;
	bNeedSortUIChildren = true;
	bIsCanvasWidget = false;
	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;
	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;

	bClipDirty = true;
	bNeedRecreateClip = true;
}

void UDreamWidget::BeginPlay()
{
	check(!bHasBegunPlay);
	bHasBegunPlay = true;

	// Iterate a snapshot: a component's BeginPlay can add or remove components on this same widget (a layout
	// container attaching its companion behaviour, for one), which would invalidate a ranged-for over Components.
	// OnRegister and OnUnregister already guard this way.
	const TArray<TObjectPtr<UDreamUIBehaviour>> ComponentsToBeginPlay = Components;
	for (auto Component : ComponentsToBeginPlay)
	{
		if (IsValid(Component) && Components.Contains(Component))
		{
			Component->BeginPlay();
		}
	}

	if (IsValid(LayoutContainer))
	{
		LayoutContainer->BeginPlay();
	}
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->BeginPlay();
	}
	if (IsValid(PanelSlot))
	{
		PanelSlot->BeginPlay();
	}
	if (IsValid(Visual))
	{
		Visual->BeginPlay();
	}
}

void UDreamWidget::EndPlay()
{
	bHasBegunPlay = false;

	for (auto Component : Components)
	{
		if (IsValid(Component))
		{
			Component->EndPlay();
		}
	}
	
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->EndPlay();
	}
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->EndPlay();
	}
	if (IsValid(PanelSlot))
	{
		PanelSlot->EndPlay();
	}
	if (IsValid(Visual))
	{
		Visual->EndPlay();
	}
}

#pragma region CallbackEvents
void UDreamWidget::Call_InteractableChanged()
{
	OnInteractableChangedEvent.Broadcast(this->GetInteractableInHierarchy());
}
void UDreamWidget::Call_TransformChanged()
{
	OnTransformChangedEvent.Broadcast();
}

void UDreamWidget::Call_DimensionsChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);

	if (Parent.IsValid())
	{
		Parent->Call_ChildDimensionsChanged(this, InPivotChanged, InWidthChanged, InHeightChanged);
	}
}

void UDreamWidget::Call_ChildDimensionsChanged(UDreamWidget* Child, bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	OnChildDimensionChangedEvent.Broadcast(Child, InPivotChanged, InWidthChanged, InHeightChanged);
}

void UDreamWidget::Call_AttachmentChanged()
{
	OnAttachmentChangedEvent.Broadcast();
}

void UDreamWidget::Call_SiblingIndexChanged()
{
	OnSiblingIndexChangedEvent.Broadcast();
}

void UDreamWidget::CollectChildrenWidgets(UDreamWidget* Target, TArray<UDreamWidget*>& OutAllChildrenWidgets, bool IncludeTarget)
{
	if (IncludeTarget)
	{
		OutAllChildrenWidgets.Add(Target);
	}
	for (auto& Child : Target->GetChildren())
	{
		CollectChildrenWidgets(Child, OutAllChildrenWidgets, true);
	}
}

void UDreamWidget::Call_WidgetActiveChanged()
{
	OnWidgetActiveChangedEvent.Broadcast(this->GetWidgetActiveInHierarchy());
}
void UDreamWidget::Call_RaycastableChanged()
{
	OnRaycastableChangedEvent.Broadcast(this->GetRaycastableInHierarchy());
}
#pragma endregion


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
			SiblingIndex = FMath::Clamp(SiblingIndex, 0, Parent->Children.Num() - 1);
			Parent->Children.Remove(this);
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

void UDreamWidget::MarkAllDirtyRecursive()
{
	MarkAllDirty();
	
	for (auto& uiChild : Children)
	{
		if (IsValid(uiChild))
		{
			uiChild->MarkAllDirtyRecursive();
		}
	}
}

void UDreamWidget::MarkAllDirty()
{
	bFlattenHierarchyIndexDirty = true;
	bClipDirty = true;

	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;
	
	if (IsValid(Visual))
	{
		Visual->MarkAllDirty();
	}
}

void UDreamWidget::MarkRenderModeChangeRecursive(UDreamCanvas* Canvas, EDreamRenderMode OldRenderMode, EDreamRenderMode NewRenderMode)
{
	if (this->RenderCanvas == Canvas)
	{
		MarkAllDirty();
		for (auto& uiChild : Children)
		{
			if (IsValid(uiChild))
			{
				uiChild->MarkRenderModeChangeRecursive(Canvas, OldRenderMode, NewRenderMode);
			}
		}
	}
}


void UDreamWidget::PostLoad()
{
	Super::PostLoad();
	// RelativeRotationEuler is transient, so seed it from the serialized rotation. Loading writes
	// RelativeRotation through reflection rather than the setter, which would leave the mirror at
	// zero and make Sequencer restore an animated widget to no rotation at all.
	this->RelativeRotationEuler = this->RelativeRotation.Rotator();
	// Every asset authored before ids existed has none. Backfilling here rather than in a migration
	// commandlet keeps a widget that never gets resaved working for the session it is open in: the
	// preview is instanced from this object, so it copies whatever id this object is holding.
	EnsureWidgetGuid();
}

void UDreamWidget::BeginDestroy()
{
	if (bHasBegunPlay || bIsRegistered)
	{
		UDreamWidget* TeardownRoot = RootWidget.GetEvenIfUnreachable();
		if (TeardownRoot == nullptr || TeardownRoot->HasAnyFlags(RF_FinishDestroyed))
		{
			TeardownRoot = this;
		}

		auto World = TeardownRoot->GetWorld();
		auto WorldName = World ? World->GetName() : TEXT("null");
		auto Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
		auto ManagerName = Manager ? Manager->GetName() : TEXT("null");

		UE_LOG(DreamGUI, Error, TEXT("UDreamWidget tree %s was not destroyed by its owner. World:%s, WorldType:%d, Manager:%s. Auto cleanup in BeginDestroy."),
			*TeardownRoot->GetPathDisplayName(), *WorldName, World ? World->WorldType : -1, *ManagerName);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red
				, FString::Printf(TEXT("UDreamWidget tree %s was not destroyed by its owner; auto cleaned up. World:%s, Manager:%s")
					, *TeardownRoot->GetPathDisplayName(), *WorldName, *ManagerName));
		}

		// Elevating fallback cleanup to the hierarchy root prevents one diagnostic per child.
		TeardownRoot->DestroyWidget();
	}
	Super::BeginDestroy();
}

void UDreamWidget::DestroyWidget()
{
	struct LOCAL
	{
		static void AppendSubtree(
			UDreamWidget* Widget,
			TArray<TObjectPtr<UDreamWidget>>& TeardownWidgets,
			TSet<const UDreamWidget*>& ScheduledWidgets)
		{
			// BeginDestroy can run after GC has marked the object unreachable, at which point
			// IsValid() is already false even though teardown on the live memory is still required.
			if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
			{
				return;
			}

			TArray<TObjectPtr<UDreamWidget>> PendingWidgets;
			PendingWidgets.Add(Widget);
			while (!PendingWidgets.IsEmpty())
			{
				UDreamWidget* CurrentWidget = PendingWidgets.Pop(EAllowShrinking::No).Get();
				if (CurrentWidget == nullptr
					|| CurrentWidget->HasAnyFlags(RF_FinishDestroyed)
					|| ScheduledWidgets.Contains(CurrentWidget))
				{
					continue;
				}

				ScheduledWidgets.Add(CurrentWidget);
				TeardownWidgets.Add(CurrentWidget);
				const TArray<UDreamWidget*> ChildrenSnapshot = CurrentWidget->GetChildren();
				for (int32 ChildIndex = ChildrenSnapshot.Num() - 1; ChildIndex >= 0; --ChildIndex)
				{
					PendingWidgets.Add(ChildrenSnapshot[ChildIndex]);
				}
			}
		}

		static void AppendCurrentChildren(
			UDreamWidget* Widget,
			TArray<TObjectPtr<UDreamWidget>>& TeardownWidgets,
			TSet<const UDreamWidget*>& ScheduledWidgets)
		{
			if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
			{
				return;
			}

			const TArray<UDreamWidget*> ChildrenSnapshot = Widget->GetChildren();
			for (UDreamWidget* Child : ChildrenSnapshot)
			{
				AppendSubtree(Child, TeardownWidgets, ScheduledWidgets);
			}
		}
	};

	TArray<TObjectPtr<UDreamWidget>> TeardownWidgets;
	TSet<const UDreamWidget*> ScheduledWidgets;
	LOCAL::AppendSubtree(this, TeardownWidgets, ScheduledWidgets);

	int32 UnregisterIndex = 0;
	auto UnregisterPendingWidgets = [&]()
	{
		while (UnregisterIndex < TeardownWidgets.Num())
		{
			UDreamWidget* Widget = TeardownWidgets[UnregisterIndex++].Get();
			if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
			{
				continue;
			}
			if (Widget->bIsRegistered)
			{
				Widget->OnUnregister();
			}
			LOCAL::AppendCurrentChildren(Widget, TeardownWidgets, ScheduledWidgets);
		}
	};

	UnregisterPendingWidgets();
	this->SetParent(nullptr);
	const int32 WidgetCountAfterDetach = TeardownWidgets.Num();
	for (int32 WidgetIndex = 0; WidgetIndex < WidgetCountAfterDetach; ++WidgetIndex)
	{
		UDreamWidget* Widget = TeardownWidgets[WidgetIndex].Get();
		LOCAL::AppendCurrentChildren(Widget, TeardownWidgets, ScheduledWidgets);
	}
	UnregisterPendingWidgets();

	int32 EndPlayIndex = 0;
	while (EndPlayIndex < TeardownWidgets.Num())
	{
		UnregisterPendingWidgets();
		UDreamWidget* Widget = TeardownWidgets[EndPlayIndex++].Get();
		if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
		{
			continue;
		}
		if (Widget->bHasBegunPlay)
		{
			Widget->EndPlay();
		}
		LOCAL::AppendCurrentChildren(Widget, TeardownWidgets, ScheduledWidgets);
	}
}

UWorld* UDreamWidget::GetWorld() const
{
	auto OuterWorld = GetTypedOuter<UWorld>();
	return OuterWorld;
}

#if WITH_EDITOR
void UDreamWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName AnchorDataName = GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData);
	const FName ChangedMemberName = PropertyChangedEvent.GetMemberPropertyName();
	// Component/prefab notifications dispatched by Super can run a layout pass immediately.
	// Preserve a direct anchor edit before that pass has a chance to restore stale geometry.
	if (ChangedMemberName == AnchorDataName && IsValid(PanelSlot))
	{
		PanelSlot->SyncAuthoredGeometryAfterUserEdit();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		//MarkAllDirtyRecursive();
		auto MemberName = PropertyChangedEvent.GetMemberPropertyName();
		auto PropertyName = PropertyChangedEvent.GetPropertyName();

		static const FName WidgetActiveName = GET_MEMBER_NAME_CHECKED(UDreamWidget, bWidgetActive);
		static const FName RaycastableName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Raycastable);
		static const FName ClippingName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Clipping);
		static const FName ClippingCornerRadiusName = GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingCornerRadius);
		static const FName ClippingMarginName = GET_MEMBER_NAME_CHECKED(UDreamWidget, ClippingMargin);
		static const FName VisualName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Visual);
		static const FName LayoutContainerName = GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutContainer);
		static const FName LayoutSelfName = GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutSelf);
		static const FName PanelSlotName = GET_MEMBER_NAME_CHECKED(UDreamWidget, PanelSlot);
		static const FName VisibilityName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Visibility);
		static const FName IgnoreLayoutName = GET_MEMBER_NAME_CHECKED(UDreamWidget, bIgnoreLayout);
		static const FName InteractableName = GET_MEMBER_NAME_CHECKED(UDreamWidget, Interactable);
		static const FName RenderOpacityName = GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderOpacity);

		if (MemberName == AnchorDataName
		|| MemberName == WidgetActiveName
		|| MemberName == ClippingCornerRadiusName
		|| MemberName == ClippingMarginName
		)
		{
			this->MarkAnchorDataChanged_Recursive(true, true, true);
			this->MarkLayoutForRebuild(this);
			this->MarkClipDirty(false);
		}
		else if (MemberName == ClippingName)
		{
			MarkClipDirty(true);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, SiblingIndex))
		{
			// Same order as SetSiblingIndex: settle the value first, then broadcast it.
			ApplySiblingIndex();
			this->Call_SiblingIndexChanged();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeLocation) || MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeRotation) || MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeScale))
		{
			CalculateAnchorFromTransform();
			CalculateObjectToWorldTransform();
			OnUpdateTransform();
			MarkTransformChanged();
			MarkLayoutForRebuild(this);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderTranslation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderRotation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderScale)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RenderTransformPivot))
		{
			// The details panel writes the property memory and then tells us; it does not call the
			// setter. Without this the value lands in the field and the widget never moves, which
			// looks exactly like the feature not working.
			ApplyRenderTransformChange();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, bPerspective)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, PerspectiveFieldOfView)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, PerspectiveOrigin))
		{
			ApplyPerspectiveChange();
		}
		else if (MemberName == VisualName)
		{
			if (IsValid(Visual))
			{
				if (RenderCanvas.IsValid())
				{
					RenderCanvas->RegisterVisual(Visual);
				}
				if (bHasBegunPlay)
				{
					Visual->BeginPlay();
				}
				Visual->Call_OnRegister();
			}
			MarkDimensionChanged(false, true, true);//change Visual could cause LayoutSelf size change
			MarkLayoutForRebuild(this);
		}
		else if (MemberName == LayoutContainerName)
		{
			UDreamLayoutContainer* PreviousLayout = LayoutContainerBeforeEdit.Get();
			LayoutContainerBeforeEdit.Reset();
			// The dropdown assigns the instance directly, so the checks CreateNewLayoutContainer makes
			// before accepting a class are repeated here: a container that caps its child count is
			// refused when the widget already has more, and the previous container is put back.
			if (!IsValid(Cast<UDreamPanelLayoutBase>(LayoutContainer)))
			{
				for (UDreamWidget* Child : Children)
				{
					RemovePanelSlotFromChild(Child);
				}
			}
			if (IsValid(LayoutContainer) && LayoutContainer != PreviousLayout)
			{
				const int32 MaxChildren = LayoutContainer->GetMaxChildren();
				int32 ValidChildCount = 0;
				for (const UDreamWidget* Child : Children)
				{
					ValidChildCount += IsValid(Child) ? 1 : 0;
				}
				if (MaxChildren >= 0 && ValidChildCount > MaxChildren)
				{
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d %s accepts at most %d children but '%s' has %d; keeping the previous panel."),
						ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *LayoutContainer->GetClass()->GetName(), MaxChildren, *GetDisplayName(), ValidChildCount);
					LayoutContainer = PreviousLayout;
				}
			}
			const bool bInitializeScaleBoxSlots = IsValid(LayoutContainer) && LayoutContainer->IsA<UDreamLayoutContainerScaleBox>()
				&& (!IsValid(PreviousLayout) || !PreviousLayout->IsA<UDreamLayoutContainerScaleBox>());
			if (IsValid(LayoutContainer))
			{
				if (bHasBegunPlay)
				{
					LayoutContainer->BeginPlay();
				}
				LayoutContainer->Call_OnRegister();
				if (IsValid(Cast<UDreamPanelLayoutBase>(LayoutContainer)))
				{
					for (UDreamWidget* Child : Children)
					{
						if (bInitializeScaleBoxSlots && IsValid(Child))
						{
							// Same transition CreateNewLayoutContainer handles: UMG gives a ScaleBox child a fresh
							// Center/Center slot, Dream reuses the generic slot so the defaults are set here.
							if (UDreamPanelSlot* ExistingSlot = Child->GetPanelSlot(); IsValid(ExistingSlot))
							{
								ExistingSlot->Modify();
								ExistingSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
								ExistingSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
							}
						}
						EnsurePanelSlotForChild(this, Child, true);
					}
				}
				LayoutContainer->CalculateLayout();
			}
			// The details dropdown assigns the container without going through CreateNewLayoutContainer,
			// so the behaviour dependencies are reconciled here. This used to be skipped for a widget
			// inside a sub-prefab instance; a class model has none.
			SyncRequiredBehavioursForLayoutContainer(PreviousLayout, LayoutContainer);
			MarkDimensionChanged(false, true, true);//change LayoutContainer could cause LayoutSelf size change
			MarkLayoutForRebuild(this);
		}
		else if (MemberName == LayoutSelfName)
		{
			if (IsValid(LayoutSelf))
			{
				if (bHasBegunPlay)
				{
					LayoutSelf->BeginPlay();
				}
				LayoutSelf->Call_OnRegister();
			}
			MarkDimensionChanged(false, true, true);//change LayoutSelf could cause size change
			MarkLayoutForRebuild(this);
		}
		else if (MemberName == PanelSlotName)
		{
			if (IsValid(PanelSlot))
			{
				if (bHasBegunPlay)
				{
					PanelSlot->BeginPlay();
				}
				PanelSlot->Call_OnRegister();
			}
			MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
		}
		else if (MemberName == IgnoreLayoutName)
		{
			MarkDimensionChanged(false, true, true);//change LayoutSelf could cause size change
			//from the parent: the ancestor walk stops at an IgnoreLayout widget, see SetIgnoreLayout
			MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
		}
		if (MemberName == AnchorDataName)
		{
			CalculateTransformFromAnchor();
			this->CalculateObjectToWorldTransform();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeLocation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeRotation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeScale))
		{
			if (IsValid(PanelSlot))
			{
				PanelSlot->SyncAuthoredGeometryAfterUserEdit();
			}
		}
		if (MemberName == WidgetActiveName)
		{
			CalculateWidgetActive_Recursive();
			CalculateVisibility_Recursive();
		}
		if (MemberName == VisibilityName)
		{
			CalculateVisibility_Recursive();
		}
		if (MemberName == RaycastableName)
		{
			CalculateRaycastable_Recursive();
		}
		if (MemberName == InteractableName)
		{
			CalculateInteractable_Recursive();
		}
		if (MemberName == RenderOpacityName)
		{
			struct LOCAL
			{
				static void MarkDirty(const UDreamWidget* Widget)
				{
					if (Widget->Visual)
					{
						Widget->Visual->MarkColorDirty();
					}
					for (auto& Child : Widget->Children)
					{
						MarkDirty(Child);
					}
				}
			};
			LOCAL::MarkDirty(this);
		}
		UDreamUIManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->MarkCanvasUpdate(true);
			}
		}, 1);
	}
}

void UDreamWidget::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName MemberName = PropertyAboutToChange->GetFName();
	if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, Visual))
	{
		if (IsValid(Visual))
		{
			if (RenderCanvas.IsValid())
			{
				RenderCanvas->MarkVisualWillChange(Visual);
				RenderCanvas->UnregisterVisual(Visual);
			}
			if (bHasBegunPlay)
			{
				Visual->EndPlay();
			}
			Visual->Call_OnUnregister();
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutContainer))
	{
		LayoutContainerBeforeEdit = LayoutContainer;
		if (IsValid(LayoutContainer))
		{
			if (bHasBegunPlay)
			{
				LayoutContainer->EndPlay();
			}
			LayoutContainer->Call_OnUnregister();
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, LayoutSelf))
	{
		if (IsValid(LayoutSelf))
		{
			if (bHasBegunPlay)
			{
				LayoutSelf->EndPlay();
			}
			LayoutSelf->Call_OnUnregister();
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamWidget, PanelSlot))
	{
		if (IsValid(PanelSlot))
		{
			if (bHasBegunPlay)
			{
				PanelSlot->EndPlay();
			}
			PanelSlot->Call_OnUnregister();
		}
	}
}

bool UDreamWidget::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	return bIsEditable;
}

bool UDreamWidget::CanEditChange(const FEditPropertyChain& PropertyChain) const
{
	bool bIsEditable = UObject::CanEditChange( PropertyChain );
	return bIsEditable;
}

void UDreamWidget::PostEditUndo()
{
	Super::PostEditUndo();
	// Undo restores RelativeRotation straight into the property, bypassing the setter that keeps
	// the transient euler mirror in step.
	this->RelativeRotationEuler = this->RelativeRotation.Rotator();
	// Same silence for the bits derived from the render transform and perspective properties.
	RefreshRenderTransformFlag();
	RefreshPerspectiveInHierarchy();
	if (Parent.IsValid())
	{
		//restore SiblingIndex
		Parent->Children.Remove(this);
		const int32 RestoredSiblingIndex = FMath::Clamp(SiblingIndex, 0, Parent->Children.Num());
		Parent->Children.Insert(this, RestoredSiblingIndex);
		for (int i = 0; i < Parent->Children.Num(); i++)
		{
			auto& UIChild = Parent->Children[i];
			if (UIChild->SiblingIndex != i)
			{
				UIChild->SiblingIndex = i;
			}
		}
	}
	// Re-register if unregistered (e.g., undo of a delete operation via DeleteForUndo).
	// bIsRegistered is not a UPROPERTY so it is not saved/restored by the undo system;
	// after soft-delete it remains false, so we need to call OnRegister() explicitly.
	const bool bWasRegistered = bIsRegistered;
	if (!bIsRegistered)
	{
		struct LOCAL
		{
			static void RegisterRecursive(UDreamWidget* Widget, TSet<const UDreamWidget*>& VisitedWidgets)
			{
				if (!IsValid(Widget) || VisitedWidgets.Contains(Widget))
				{
					return;
				}
				VisitedWidgets.Add(Widget);
				if (!Widget->bIsRegistered)
				{
					Widget->OnRegister();
				}
				for (UDreamWidget* Child : Widget->Children)
				{
					if (IsValid(Child))
					{
						RegisterRecursive(Child, VisitedWidgets);
					}
				}
			}
		};
		TSet<const UDreamWidget*> VisitedWidgets;
		LOCAL::RegisterRecursive(this, VisitedWidgets);
	}

	// Transactional pointer swaps do not run the old layout's unregister path. Reset every
	// parent-owned transient before registering and rebuilding the currently restored layout.
	SetLayoutScale(FVector2f::UnitVector);
	SetLayoutVisibilitySuppressed(false);
	ClearLayoutClippingOverride();
	for (UDreamWidget* Child : Children)
	{
		if (!IsValid(Child))
		{
			continue;
		}
		Child->SetLayoutScale(FVector2f::UnitVector);
		Child->SetLayoutVisibilitySuppressed(false);
	}
	if (IsValid(LayoutContainer) && bWasRegistered)
	{
		LayoutContainer->Call_OnRegister();
	}
	if (IsValid(Cast<UDreamLayoutContainerScrollBox>(LayoutContainer)))
	{
		SetLayoutClippingOverride(EDreamWidgetClipping::ClipToBounds);
	}
	if (IsValid(Cast<UDreamPanelLayoutBase>(LayoutContainer)))
	{
		for (UDreamWidget* Child : Children)
		{
			EnsurePanelSlotForChild(this, Child);
		}
	}
	else
	{
		for (UDreamWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
				{
					Slot->RestoreAuthoredGeometry();
				}
			}
		}
	}
	CalculateVisibility_Recursive();
	MarkLayoutForRebuild(this);
}

void UDreamWidget::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);
}

void UDreamWidget::EnsureChildrenAfterTransaction()
{
	struct LOCAL
	{
		static void CheckIt(UDreamWidget* Widget)
		{
			for (int i = 0;i < Widget->Children.Num(); i++)
			{
				auto Child = Widget->Children[i];
				if (!IsValid(Child))
				{
					Widget->Children.RemoveAt(i);
					i--;
					continue;
				}
				Child->SiblingIndex = i;
				CheckIt(Child);
			}
		}
	};
	LOCAL::CheckIt(this);
}

void UDreamWidget::EnsureDataForRebuild()
{
	check(this == RootWidget);
	struct LOCAL
	{
		static void RenewRenderCanvas(UDreamWidget* Widget)
		{
			auto ThisRenderCanvas = Widget->GetComponent<UDreamCanvas>();
			Widget->RenewRenderCanvasRecursive(ThisRenderCanvas);
		}
		static void EnsureDataForRebuildRecursive(UDreamWidget* Widget)
		{
			Widget->EnsureUIChildrenValid();
			Widget->bNeedSortUIChildren = true;
			Widget->EnsureUIChildrenSorted();
			if (Widget->bIsCanvasWidget && Widget->RenderCanvas.IsValid())
			{
				Widget->RenderCanvas->EnsureDataForRebuild();
			}

			for (auto& uiChild : Widget->Children)
			{
				if (IsValid(uiChild))
				{
					EnsureDataForRebuildRecursive(uiChild);
				}
			}
		}
		/** force refresh render canvas, remove from old and add to new */
		static void ForceRefreshRenderCanvasRecursive(UDreamWidget* Widget)
		{
			auto NewRenderCanvas = Widget->GetComponentInParent<UDreamCanvas>(true);
			Widget->SetRenderCanvas(NewRenderCanvas);

			for (auto& uiChild : Widget->Children)
			{
				if (IsValid(uiChild))
				{
					ForceRefreshRenderCanvasRecursive(uiChild);
				}
			}
		}
	};
	MarkAllDirtyRecursive();
	LOCAL::RenewRenderCanvas(this);
	LOCAL::EnsureDataForRebuildRecursive(this);
	LOCAL::ForceRefreshRenderCanvasRecursive(this);
	CalculateWidgetActive_Recursive();
	CalculateVisibility_Recursive();
	CalculateRaycastable_Recursive();
	CalculateInteractable_Recursive();
	CalculateObjectToWorldTransform();
}

#endif


#pragma region Transform
FVector UDreamWidget::GetWorldLocation()const
{
	return GetWorldTransform().GetLocation();
}
FQuat UDreamWidget::GetWorldRotation()const
{
	return GetWorldTransform().GetRotation();
}
FVector UDreamWidget::GetWorldScale()const
{
	return GetWorldTransform().GetScale3D();
}

FVector UDreamWidget::GetForwardVector() const
{
	return GetWorldTransform().GetRotation().GetForwardVector();
}

FVector UDreamWidget::GetRightVector() const
{
	return GetWorldTransform().GetRotation().GetRightVector();
}

FVector UDreamWidget::GetUpVector() const
{
	return GetWorldTransform().GetRotation().GetUpVector();
}

void UDreamWidget::SetRelativeLocation(const FVector& Value)
{
	if (this->RelativeLocation != Value)
	{
		this->RelativeLocation = Value;
		this->CalculateObjectToWorldTransform();
		
		if (bCanSetAnchorFromTransform)
		{
			CalculateAnchorFromTransform();
			if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
			{
				MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
			}
		}
	}
}
void UDreamWidget::SetRelativeRotation(const FQuat& Value)
{
	if (this->RelativeRotation != Value)
	{
		this->RelativeRotation = Value;
		this->RelativeRotationEuler = Value.Rotator();
		this->CalculateObjectToWorldTransform();
	}
}
void UDreamWidget::SetRelativeRotationEuler(const FRotator& Value)
{
	// Store what the caller gave us rather than round-tripping through the quaternion: that would
	// normalize the angles (370 degrees becomes 10), making an animation jump as it crosses a turn.
	this->RelativeRotationEuler = Value;
	const FQuat NewRotation = Value.Quaternion();
	if (this->RelativeRotation != NewRotation)
	{
		this->RelativeRotation = NewRotation;
		this->CalculateObjectToWorldTransform();
	}
}
void UDreamWidget::SetRelativeScale(const FVector& Value)
{
	if (this->RelativeScale != Value)
	{
		this->RelativeScale = Value;
		this->CalculateObjectToWorldTransform();
	}
}

void UDreamWidget::SetLayoutScale(const FVector2f& Value)
{
	const FVector2f SanitizedScale(
		FMath::IsFinite(Value.X) ? FMath::Max(0.0f, Value.X) : 1.0f,
		FMath::IsFinite(Value.Y) ? FMath::Max(0.0f, Value.Y) : 1.0f);
	if (!LayoutScale.Equals(SanitizedScale, 0.0f))
	{
		LayoutScale = SanitizedScale;
		CalculateObjectToWorldTransform();
	}
}
void UDreamWidget::SetRelativeLocationAndRotation(const FVector& InLocation, const FQuat& InRotation)
{
	if (this->RelativeLocation != InLocation || this->RelativeRotation != InRotation)
	{
		this->RelativeLocation = InLocation;
		this->RelativeRotation = InRotation;
		this->RelativeRotationEuler = InRotation.Rotator();
		this->CalculateObjectToWorldTransform();

		if (bCanSetAnchorFromTransform)
		{
			CalculateAnchorFromTransform();
			if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
			{
				MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
			}
		}
	}
}

void UDreamWidget::SetWorldLocation(const FVector& Value)
{
	auto WorldRotation = GetWorldRotation();
	SetWorldLocationAndRotation(Value, WorldRotation);
}
void UDreamWidget::SetWorldRotation(const FQuat& Value)
{
	auto WorldPosition = GetWorldLocation();
	SetWorldLocationAndRotation(WorldPosition, Value);
}
void UDreamWidget::SetWorldLocationAndRotation(const FVector& InLocation, const FQuat& InRotation)
{
	FTransform DesiredWorldTransform = GetWorldTransform();
	DesiredWorldTransform.SetLocation(InLocation);
	DesiredWorldTransform.SetRotation(InRotation);
	SetWorldTransform(DesiredWorldTransform);
}

FTransform UDreamWidget::GetLocalTransform()const
{
	return FTransform(RelativeRotation, RelativeLocation,
		RelativeScale * FVector(1.0, LayoutScale.X, LayoutScale.Y));
}

FTransform UDreamWidget::GetRenderTransform()const
{
	if (!bHasRenderTransform)
	{
		return FTransform::Identity;
	}
	// The pivot is normalized inside the widget's own rect, so it has to be resolved against the
	// current size -- a widget stretched by its layout must still turn about its own middle. It sits
	// on the widget's plane, which is local X = 0.
	const FVector PivotPoint(0.0,
		GetLocalSpaceLeft() + GetWidth() * RenderTransformPivot.X,
		GetLocalSpaceBottom() + GetHeight() * RenderTransformPivot.Y);
	const FTransform ScaleAndRotate(RenderRotation.Quaternion(), FVector::ZeroVector, RenderScale);
	// Bracket by the pivot, then translate. FTransform composes left-to-right as "apply A, then B".
	return FTransform(-PivotPoint) * ScaleAndRotate * FTransform(PivotPoint) * FTransform(RenderTranslation);
}

FTransform UDreamWidget::GetRenderLocalTransform()const
{
	// One bit test on the overwhelmingly common path: nothing is animating, so nothing is composed.
	return bHasRenderTransform ? GetRenderTransform() * GetLocalTransform() : GetLocalTransform();
}

FTransform UDreamWidget::GetLayoutWorldTransform()const
{
	// Deliberately recomputed by walking up rather than cached: this is only asked for when a world
	// transform is being converted back into authored data, which is rare, and a second cached chain
	// would be a second thing to keep in step on every move.
	const FTransform LocalTransform = GetLocalTransform();
	if (Parent.IsValid())
	{
		return LocalTransform * Parent->GetLayoutWorldTransform();
	}
	if (auto WidgetPresenterComponent = GetAttachedRootSceneComponent())
	{
		return LocalTransform * WidgetPresenterComponent->GetComponentTransform();
	}
	return LocalTransform;
}

void UDreamWidget::SetRenderTranslation(const FVector& Value)
{
	if (this->RenderTranslation != Value)
	{
		this->RenderTranslation = Value;
		this->ApplyRenderTransformChange();
	}
}

void UDreamWidget::SetRenderRotation(const FRotator& Value)
{
	if (this->RenderRotation != Value)
	{
		this->RenderRotation = Value;
		this->ApplyRenderTransformChange();
	}
}

void UDreamWidget::SetRenderScale(const FVector& Value)
{
	if (this->RenderScale != Value)
	{
		this->RenderScale = Value;
		this->ApplyRenderTransformChange();
	}
}

void UDreamWidget::SetRenderTransformPivot(const FVector2D& Value)
{
	if (this->RenderTransformPivot != Value)
	{
		this->RenderTransformPivot = Value;
		this->ApplyRenderTransformChange();
	}
}

void UDreamWidget::ClearRenderTransform()
{
	if (bHasRenderTransform)
	{
		RenderTranslation = FVector::ZeroVector;
		RenderRotation = FRotator::ZeroRotator;
		RenderScale = FVector::OneVector;
		ApplyRenderTransformChange();
	}
}

bool UDreamWidget::GetPerspectiveScope(DreamPerspective::FScope& OutScope)const
{
	if (!bPerspective)
	{
		return false;
	}
	// Layout's transform, not the drawn one. The scope's plane has to be where LAYOUT put this
	// widget: measured against its own drawn transform the declarer is flat in its own plane by
	// construction, and no perspective of its own could ever reach it. Taken from layout, the
	// widget's own render rotation is a departure from that plane and foreshortens like anything
	// else -- while a widget with no render transform still lies in the plane and is left alone.
	const FTransform World = GetLayoutWorldTransform();
	// The origin sits on this widget's own rect, like RenderTransformPivot, so it tracks a widget
	// that gets stretched by its layout rather than drifting off it.
	const FVector LocalOrigin(0.0,
		GetLocalSpaceLeft() + GetWidth() * PerspectiveOrigin.X,
		GetLocalSpaceBottom() + GetHeight() * PerspectiveOrigin.Y);
	OutScope.PlanePoint = World.TransformPosition(LocalOrigin);
	OutScope.PlaneNormal = World.TransformVector(FVector::XAxisVector).GetSafeNormal();
	// MINUS the normal. A widget's local +X points INTO the screen, not out of it:
	// UDreamCanvas::GetViewLocation puts the viewer at "world location - forward * distance", so the
	// eye is on the -X side and +X is depth away from it. Placing the scope's eye on +X would put
	// it behind the plane, opposite the canvas's own eye, and the remap would then re-aim from a
	// viewer in front to a viewer behind -- foreshortening inverted and overstated. The eye has to
	// sit on the same side of the plane as the one that is actually looking.
	OutScope.EyePosition = OutScope.PlanePoint - OutScope.PlaneNormal * GetPerspectiveDistance();
	return true;
}

FMatrix UDreamWidget::GetInheritedPerspectiveRemap()const
{
	if (!HasPerspectiveApplied())
	{
		return FMatrix::Identity;
	}
	// Walking up gathers the scopes innermost first, which is the order the composition wants.
	// Deliberately not cached per widget: caching would be two matrices on every widget in the
	// project to speed up a subtree that usually does not exist, and this is asked for once per
	// geometry rebuild rather than per vertex.
	TArray<DreamPerspective::FScope, TInlineAllocator<4>> Scopes;
	for (const UDreamWidget* Ancestor = this; Ancestor != nullptr; Ancestor = Ancestor->Parent.Get())
	{
		DreamPerspective::FScope Scope;
		if (Ancestor->GetPerspectiveScope(Scope))
		{
			Scopes.Add(Scope);
		}
	}
	// The whole feature works by re-aiming geometry at the eye the canvas projects from, so it only
	// means anything where that eye is what is actually looking. In a world-space mode the scene
	// camera does the projecting and the canvas's eye is nobody; re-aiming at it would displace the
	// geometry for a viewer that does not exist. And an orthographic canvas has its eye at infinity,
	// which no affine map can reach -- CalculateDistanceToCamera even returns a hard 1000 there,
	// a number that means "not applicable" rather than a distance.
	UDreamCanvas* Canvas = GetRenderCanvas();
	UDreamCanvas* Root = Canvas ? Canvas->GetRootCanvas() : nullptr;
	if (Root == nullptr || Root->IsRenderToWorldSpace()
		|| Root->GetProjectionType() != ECameraProjectionMode::Perspective)
	{
		return FMatrix::Identity;
	}
	return DreamPerspective::ComposeRemap(Scopes, Root->GetViewLocation());
}

FMatrix UDreamWidget::GetWorldMatrix()const
{
	const FMatrix Base = ObjectToWorldTransform.ToMatrixWithScale();
	return HasPerspectiveApplied() ? Base * GetInheritedPerspectiveRemap() : Base;
}

FMatrix UDreamWidget::GetInverseWorldMatrix()const
{
	return GetWorldMatrix().Inverse();
}

void UDreamWidget::SetPerspective(bool Value)
{
	if (bPerspective != Value)
	{
		bPerspective = Value;
		ApplyPerspectiveChange();
	}
}

float UDreamWidget::GetPerspectiveDistance()const
{
	// The same formula UDreamCanvas::CalculateDistanceToCamera uses, so a widget's field of view means
	// what the canvas's field of view means. Derived rather than stored: the widget's width is what
	// makes the angle scale-invariant, and that width changes.
	const float Clamped = FMath::Clamp(PerspectiveFieldOfView, 1.0f, 179.0f);
	return GetWidth() * 0.5f / FMath::Tan(FMath::DegreesToRadians(Clamped * 0.5f));
}

void UDreamWidget::SetPerspectiveFieldOfView(float Value)
{
	const float Sanitized = FMath::Clamp(Value, 1.0f, 179.0f);
	if (PerspectiveFieldOfView != Sanitized)
	{
		PerspectiveFieldOfView = Sanitized;
		ApplyPerspectiveChange();
	}
}

void UDreamWidget::SetPerspectiveOrigin(const FVector2D& Value)
{
	if (PerspectiveOrigin != Value)
	{
		PerspectiveOrigin = Value;
		ApplyPerspectiveChange();
	}
}

#if WITH_EDITOR
void UDreamWidget::WarnIfPerspectiveCannotApply()const
{
	if (!bPerspective)
	{
		return;
	}
	UDreamCanvas* Canvas = GetRenderCanvas();
	UDreamCanvas* Root = Canvas ? Canvas->GetRootCanvas() : nullptr;
	const TCHAR* Reason = nullptr;
	if (Root == nullptr)
	{
		Reason = TEXT("this widget is not under a canvas");
	}
	else if (Root->IsRenderToWorldSpace())
	{
		// The prefab editor previews in world space by default, so this is the message an author is
		// most likely to need: the field they are adjusting is genuinely inert in front of them.
		Reason = TEXT("its canvas renders in world space, where the scene camera does the projecting and already supplies perspective of its own. Perspective is defined against a canvas's own virtual camera, so switch the preview canvas to ScreenSpaceOverlay (the Screen Space button on the toolbar). The remap is baked into the geometry, so it then shows in the editor viewport too -- use Canvas Eye to view it from the projection it was built for");
	}
	else if (Root->GetProjectionType() != ECameraProjectionMode::Perspective)
	{
		Reason = TEXT("its canvas is orthographic, which has its eye at infinity and no perspective to share");
	}
	if (Reason != nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("Perspective on '%s' has no effect here, because %s."),
			*GetPathDisplayName(), Reason);
	}
}
#endif

void UDreamWidget::ApplyPerspectiveChange()
{
	// Same shape as a render transform change and pointedly not a layout one: a perspective moves
	// where things are drawn, never where the layout believes they are.
	RefreshPerspectiveInHierarchy();
	CalculateObjectToWorldTransform(true);
	MarkCanvasUpdate(true);
#if WITH_EDITOR
	// Said at the moment the author touches it, which is the moment they are wondering.
	WarnIfPerspectiveCannotApply();
#endif
}

void UDreamWidget::RefreshPerspectiveInHierarchy()
{
	const bool bNew = bPerspective || (Parent.IsValid() && Parent->bHasPerspectiveInHierarchy);
	if (bHasPerspectiveInHierarchy != bNew)
	{
		bHasPerspectiveInHierarchy = bNew;
		for (UDreamWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				Child->RefreshPerspectiveInHierarchy();
			}
		}
	}
}

void UDreamWidget::RefreshRenderTransformFlag()
{
	bHasRenderTransform = !RenderTranslation.IsNearlyZero()
		|| !RenderRotation.IsNearlyZero()
		|| !RenderScale.Equals(FVector::OneVector);
}

void UDreamWidget::ApplyRenderTransformChange()
{
	RefreshRenderTransformFlag();
	// Exactly what SetLayoutScale does, and pointedly NOT what SetRelativeLocation does: no
	// CalculateAnchorFromTransform, no MarkLayoutForRebuild. Those two lines are the reason
	// animating a laid-out widget's position fights the layout instead of moving it.
	CalculateObjectToWorldTransform(true);
}
const FTransform& UDreamWidget::GetWorldTransform()const
{
	return ObjectToWorldTransform;
}

void UDreamWidget::SetWorldTransform(const FTransform& InWorldTransform)
{
	const FVector PreviousAuthoredScale = RelativeScale;
	auto ResolveAuthoredScale = [this, &PreviousAuthoredScale](const FVector& EffectiveLocalScale)
	{
		FVector Result = EffectiveLocalScale;
		Result.Y = FMath::IsNearlyZero(LayoutScale.X) ? PreviousAuthoredScale.Y : EffectiveLocalScale.Y / LayoutScale.X;
		Result.Z = FMath::IsNearlyZero(LayoutScale.Y) ? PreviousAuthoredScale.Z : EffectiveLocalScale.Z / LayoutScale.Y;
		return Result;
	};
	FTransform LocalTransform = InWorldTransform;
	if (Parent.IsValid())
	{
		// Layout's basis, not the drawn one: if an ancestor is mid-animation, dividing by the drawn
		// transform would fold that offset into this widget's authored RelativeLocation.
		LocalTransform = InWorldTransform.GetRelativeTransform(Parent->GetLayoutWorldTransform());
	}
	else if (const USceneComponent* WidgetPresenterComponent = GetAttachedRootSceneComponent())
	{
		LocalTransform = InWorldTransform.GetRelativeTransform(WidgetPresenterComponent->GetComponentTransform());
	}

	this->RelativeLocation = LocalTransform.GetLocation();
	this->RelativeRotation = LocalTransform.GetRotation();
	this->RelativeRotationEuler = this->RelativeRotation.Rotator();
	this->RelativeScale = ResolveAuthoredScale(LocalTransform.GetScale3D());
	CalculateObjectToWorldTransform(true);
	if (bCanSetAnchorFromTransform)
	{
		CalculateAnchorFromTransform();
		MarkLayoutForRebuild(this);
	}
}

void UDreamWidget::SetParentBeforeRegister(UDreamWidget* InParent)
{
	check(!bIsRegistered);
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
		}
	}
}

void UDreamWidget::ApplySiblingIndexFromPrefab_Recursive()
{
	// Order by the restored indices first — StableSort keeps relative order across holes and duplicates
	// (legacy data, cross-parent moves) — then renumber contiguously so a later tail-append can never
	// collide with a restored index. Silent on purpose: this runs during prefab assembly/refresh, before
	// anything consumes the hierarchy.
	Children.StableSort([](const UDreamWidget& A, const UDreamWidget& B)
		{
			return A.SiblingIndex < B.SiblingIndex;
		});
	for (int i = 0; i < Children.Num(); i++)
	{
		auto& Child = Children[i];
		Child->SiblingIndex = i;
		Child->ApplySiblingIndexFromPrefab_Recursive();
	}
}

void UDreamWidget::RestoreSiblingIndexFromPrefab(int32 InSiblingIndex)
{
	SiblingIndex = InSiblingIndex;
	if (Parent.IsValid())
	{
		Parent->bNeedSortUIChildren = true;
	}
}

UDreamUIBehaviour* UDreamWidget::AddComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass, UDreamUIBehaviour* ComponentTemplate)
{
	if (!*ComponentClass)
	{
		return nullptr;
	}
	if (ComponentTemplate && ComponentTemplate->GetClass() != *ComponentClass)
	{
		return nullptr;
	}

	EObjectFlags NewComponentFlags = RF_Public | RF_Transactional;
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		// Components created while building class defaults must be archetype/default-subobjects.
		// They also need to be public, otherwise Blueprint-generated templates can end up
		// referencing parent CDO private archetype objects that SavePackage rejects.
		NewComponentFlags |= (RF_Public | RF_DefaultSubObject | RF_ArchetypeObject);
	}

	const FName NewComponentName = MakeUniqueObjectName(this, ComponentClass, ComponentClass->GetFName());
	auto NewComponent = NewObject<UDreamUIBehaviour>(this, ComponentClass, NewComponentName, NewComponentFlags, ComponentTemplate);
	Components.Add(NewComponent);
	if (bIsRegistered)
	{
		NewComponent->OnRegister();
	}
	if (bHasBegunPlay)
	{
		NewComponent->BeginPlay();
	}
	OnComponentsChangedEvent.Broadcast(EDreamWidgetComponentsChangedType::Added);
	return NewComponent;
}

UDreamUIBehaviour* UDreamWidget::AddComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass)
{
	return AddComponent(ComponentClass, nullptr);
}

UDreamUIBehaviour* UDreamWidget::AddComponentByTemplate(UDreamUIBehaviour* ComponentTemplate)
{
	return AddComponent(ComponentTemplate->GetClass(), ComponentTemplate);
}

void UDreamWidget::RemoveComponent(UDreamUIBehaviour* Component)
{
	auto Index = Components.Find(Component);
	if (Index < 0)return;
	Components.RemoveAt(Index);
	if (bHasBegunPlay)
	{
		Component->EndPlay();
	}
	Component->OnUnregister();
	OnComponentsChangedEvent.Broadcast(EDreamWidgetComponentsChangedType::Removed);
}

void UDreamWidget::MoveComponentToIndex(UDreamUIBehaviour* Component, int32 NewIndex)
{
	const int32 SourceIndex = Components.Find(Component);
	if (SourceIndex < 0)
	{
		return;
	}

	const int32 TargetIndex = FMath::Clamp(NewIndex, 0, Components.Num() - 1);
	if (SourceIndex == TargetIndex)
	{
		return;
	}

	UDreamUIBehaviour* MovingComponent = Components[SourceIndex];
	Components.RemoveAt(SourceIndex);
	Components.Insert(MovingComponent, FMath::Clamp(TargetIndex, 0, Components.Num()));
	OnComponentsChangedEvent.Broadcast(EDreamWidgetComponentsChangedType::Reorder);
}

void UDreamWidget::UpdateObjectToWorldTransform()
{
	auto LocalTransform = GetRenderLocalTransform();
	if (Parent.IsValid())
	{
		ObjectToWorldTransform = LocalTransform * Parent->GetWorldTransform();
	}
	else
	{
		if (auto WidgetPresenterComponent = GetAttachedRootSceneComponent())
		{
			ObjectToWorldTransform = LocalTransform * WidgetPresenterComponent->GetComponentTransform();
		}
		else
		{
			ObjectToWorldTransform = LocalTransform;
		}
	}
	this->MarkTransformChanged();
}
void UDreamWidget::CalculateObjectToWorldTransform(bool bPropagateToChildren)
{
	this->UpdateObjectToWorldTransform();
	this->OnUpdateTransform();
	if (bPropagateToChildren)
	{
		for (UDreamWidget* Child : this->Children)
		{
			if (IsValid(Child))
			{
				Child->CalculateObjectToWorldTransform(true);
			}
		}
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

bool UDreamWidget::SetParentFromPrefab(UDreamWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex)
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
			SynchronizePanelSlotForParent(InParent, this, true);
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
		if (bIsRegistered)
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

bool UDreamWidget::IsChildOf(const UDreamWidget* InTarget)const
{
	auto TempParent = this->Parent;
	TSet<const UDreamWidget*> VisitedWidgets;
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
#pragma endregion Transform

TArray<UDreamUIBehaviour*> UDreamWidget::GetComponents(TSubclassOf<UDreamUIBehaviour> ComponentClass)const
{
	TArray<UDreamUIBehaviour*> ResultArray;
	UClass* RequestedClass = *ComponentClass;
	if (!IsValid(RequestedClass) || !RequestedClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		return ResultArray;
	}
	for (auto& Comp : Components)
	{
		if (IsValid(Comp) && Comp->IsA(RequestedClass))
		{
			ResultArray.Add(Comp);
		}
	}
	return ResultArray;
}

UDreamUIBehaviour* UDreamWidget::GetComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass)const
{
	UClass* RequestedClass = *ComponentClass;
	if (!IsValid(RequestedClass) || !RequestedClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
	{
		return nullptr;
	}
	for (auto& Comp : Components)
	{
		if (IsValid(Comp) && Comp->IsA(RequestedClass))
		{
			return Comp;
		}
	}
	return nullptr;
}

bool UDreamWidget::SyncRequiredBehavioursForLayoutContainer(const UDreamLayoutContainer* OldLayout, const UDreamLayoutContainer* NewLayout)
{
	if (HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return false;
	}
	TArray<TSubclassOf<UDreamUIBehaviour>> RequiredClasses;
	TArray<TSubclassOf<UDreamUIBehaviour>> PreviouslyRequiredClasses;
	if (IsValid(NewLayout))
	{
		NewLayout->GetRequiredBehaviourClasses(RequiredClasses);
	}
	if (IsValid(OldLayout))
	{
		OldLayout->GetRequiredBehaviourClasses(PreviouslyRequiredClasses);
	}

	TArray<UDreamUIBehaviour*> ComponentsToRemove;
	for (const TSubclassOf<UDreamUIBehaviour>& PreviousClass : PreviouslyRequiredClasses)
	{
		UClass* Class = *PreviousClass;
		const bool bStillRequired = IsValid(Class) && RequiredClasses.ContainsByPredicate([Class](const TSubclassOf<UDreamUIBehaviour>& Required)
		{
			return IsValid(*Required) && (Class->IsChildOf(*Required) || (*Required)->IsChildOf(Class));
		});
		if (!IsValid(Class) || bStillRequired)
		{
			continue;
		}
		// Every instance goes, not only the one the old container added: a leftover ContentWidget would
		// keep capping the new container at one child.
		ComponentsToRemove.Append(GetComponents(PreviousClass));
	}
	TArray<UClass*> ClassesToAdd;
	for (const TSubclassOf<UDreamUIBehaviour>& RequiredClass : RequiredClasses)
	{
		UClass* Class = *RequiredClass;
		if (IsValid(Class) && !Class->HasAnyClassFlags(CLASS_Abstract) && !IsValid(GetComponent(RequiredClass)))
		{
			ClassesToAdd.AddUnique(Class);
		}
	}
	if (ComponentsToRemove.Num() == 0 && ClassesToAdd.Num() == 0)
	{
		return false;
	}

#if WITH_EDITOR
	if (UObject* WidgetOuter = GetOuter())
	{
		WidgetOuter->SetFlags(RF_Transactional);
		WidgetOuter->Modify();
	}
	SetFlags(RF_Transactional);
	Modify();
#endif
	for (UDreamUIBehaviour* Component : ComponentsToRemove)
	{
#if WITH_EDITOR
		Component->Modify();
#endif
		RemoveComponent(Component);
	}
	for (UClass* Class : ClassesToAdd)
	{
		if (UDreamUIBehaviour* NewComponent = AddComponent(Class))
		{
#if WITH_EDITOR
			NewComponent->SetFlags(RF_Transactional);
			NewComponent->Modify();
#endif
		}
	}
	return true;
}

UDreamUIBehaviour* UDreamWidget::GetComponentByInterface(UClass* InterfaceClass)const
{
	if (!IsValid(InterfaceClass) || !InterfaceClass->HasAnyClassFlags(CLASS_Interface))
	{
		return nullptr;
	}
	for (auto& Component : GetAllComponents())
	{
		if (IsValid(Component) && Component->GetClass()->ImplementsInterface(InterfaceClass))
		{
			return Component;
		}
	}
	return nullptr;
}

DECLARE_CYCLE_STAT(TEXT("DreamWidget OnUpdateTransform"), STAT_OnUpdateTransform, STATGROUP_DreamGUI);
void UDreamWidget::OnUpdateTransform()
{
	SCOPE_CYCLE_COUNTER(STAT_OnUpdateTransform)
	// UE_LOG(DreamGUI, Error, TEXT("OnUpdateTransform Flag:%d %s"), (int)UpdateTransformFlags, *this->GetDisplayName());
		bool bPositionChanged = false, bRotationChanged = false, bScaleChanged = false;
	{
		auto Pos = this->GetRelativeLocation();
		auto Pos2D = FVector2D(Pos.Y, Pos.Z);
		if (Pos2D != PrevLocation2D)
		{
			PrevLocation2D = Pos2D;
			bPositionChanged = true;
		}
		auto CompScale3D = this->GetWorldScale();
		auto CompScale2D = FVector2D(CompScale3D.Y, CompScale3D.Z);
		if (PrevScale2D != CompScale2D)
		{
			PrevScale2D = CompScale2D;
			bScaleChanged = true;
		}
		if (LayoutContainer)
		{
			LayoutContainer->OnTransformChanged();
		}
		if (LayoutSelf)
		{
			LayoutSelf->OnTransformChanged();
		}
		if (Visual)
		{
			Visual->OnTransformChanged(bPositionChanged, bScaleChanged);
		}
	}
}

void UDreamWidget::OnChildAttached(UDreamWidget* ChildWidget)
{
	//make sure SiblingIndex all good
	if (ChildWidget->SiblingIndex == INDEX_NONE)
	{
		for (int i = 0; i < Children.Num(); i++)
		{
			auto& UIChild = Children[i];
			if (UIChild->SiblingIndex != i)
			{
				UIChild->SiblingIndex = i;
				UIChild->Call_SiblingIndexChanged();
			}
		}
	}
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
	}
	RefreshPerspectiveInHierarchy();//a new parent can put this subtree inside a perspective scope
	if (this->bIsRegistered)//registered means not during prefab process
	{
		Call_TransformChanged();
		CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
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
	// Drop the dead slots before renumbering. This ran unguarded and was the only Children loop in
	// this file that did: deleting a widget blueprint instance from the designer tears its contents
	// down first, and the detach that follows walked a Children array holding an entry the teardown
	// had already emptied -- an access violation reading SiblingIndex off a null, taking the editor
	// with it. Removing rather than skipping is also what makes the renumbering below mean anything:
	// a hole would leave every sibling after it numbered one past its own position.
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
	}
}

void UDreamWidget::OnRegister()
{
	// Idempotent: UDreamUIManagerWorldSubsystem::AddWidget treats a duplicate as a bug worth an error
	// and a stack dump, and every step below is either a set-to-true or an AddUnique, so a second
	// call can only do redundant work. Registering a widget you did not create is now safe to ask for.
	if (bIsRegistered)
	{
		return;
	}
	bIsRegistered = true;
	// The prefab loader writes properties straight into memory -- no setter, no
	// PostEditChangeProperty -- and restores the hierarchy through SetParentBeforeRegister, which
	// fires no attach events. So registration is the first moment the transient bits derived from
	// serialized properties can be recomputed. Without this, a saved render transform (or a saved
	// perspective declared anywhere but the root) works in the session that authored it and
	// silently does nothing after a load -- which reads as "only works in the prefab editor".
	RefreshRenderTransformFlag();
	RefreshPerspectiveInHierarchy();
	const bool bPanelSlotRegisteredByEnsure = Parent.IsValid()
		&& EnsurePanelSlotForChild(Parent.Get(), this);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		DreamUIManager->AddWidget(this);
#if WITH_EDITOR
		DreamUIManager->MarkDreamUIWidgetOutlinerChanged();
#endif
	}
	CheckRootWidget();

	if (this->IsRootWidgetInHierarchy())
	{
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
		CalculateRaycastable_Recursive();
		CalculateInteractable_Recursive();
	}

	if (IsValid(LayoutContainer))
	{
		LayoutContainer->Call_OnRegister();
	}
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->Call_OnRegister();
	}
	if (IsValid(PanelSlot) && !bPanelSlotRegisteredByEnsure)
	{
		PanelSlot->Call_OnRegister();
	}
	if (IsValid(Visual))
	{
		Visual->Call_OnRegister();
		if (RenderCanvas.IsValid())
		{
			RenderCanvas->RegisterVisual(Visual);
		}
	}

	Components.Remove(nullptr);//clear null component
	const TArray<TObjectPtr<UDreamUIBehaviour>> ComponentsToRegister = Components;
	for (UDreamUIBehaviour* Component : ComponentsToRegister)
	{
		if (IsValid(Component) && Components.Contains(Component))
		{
			Component->OnRegister();
		}
	}
}
void UDreamWidget::OnUnregister()
{
	bIsRegistered = false;

	// Component teardown may remove helper behaviours from this same widget.
	// Iterate a snapshot so those callbacks cannot invalidate the active iterator.
	const TArray<TObjectPtr<UDreamUIBehaviour>> ComponentsToUnregister = Components;
	for (UDreamUIBehaviour* Component : ComponentsToUnregister)
	{
		if (IsValid(Component) && Components.Contains(Component))
		{
			Component->OnUnregister();
		}
	}

	if (IsValid(LayoutContainer))
	{
		LayoutContainer->Call_OnUnregister();
	}
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->Call_OnUnregister();
	}
	if (IsValid(PanelSlot))
	{
		PanelSlot->Call_OnUnregister();
	}
	if (IsValid(Visual))
	{
		Visual->Call_OnUnregister();
		if (RenderCanvas.IsValid())
		{
			RenderCanvas->MarkVisualWillChange(Visual);
			RenderCanvas->UnregisterVisual(Visual);
		}
	}
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		DreamUIManager->RemoveWidget(this);
#if WITH_EDITOR
		DreamUIManager->MarkDreamUIWidgetOutlinerChanged();
#endif
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
			// become a saveable one, and a copy of an authored widget has to stay transactional or
			// undo cannot reach it.
			FObjectInstancingGraph InstancingGraph;
			UDreamWidget* Copy = NewObject<UDreamWidget>(InOuter, InSource->GetClass(), NAME_None,
				InSource->GetMaskedFlags(RF_Transactional | RF_Transient | RF_Public),
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


void UDreamWidget::CalculateAnchorFromTransform()
{
	auto TempRelativeLocation = this->GetRelativeLocation();
	FVector2D CalculatedAnchoredPosition;
	if (Parent.IsValid())
	{
		//just a reverse operation from CalculateTransformFromAnchor
		float LocalLeftPoint =
			Parent->GetLocalSpaceLeft()
			+ (Parent->GetWidth() * this->AnchorData.AnchorMin.X);

		float LocalBottomPoint =
			Parent->GetLocalSpaceBottom()
			+ (Parent->GetHeight() * this->AnchorData.AnchorMin.Y);

		CalculatedAnchoredPosition.X = TempRelativeLocation.Y
			- LocalLeftPoint
			- +(Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X)) * this->AnchorData.Pivot.X;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z
			- LocalBottomPoint
			- (Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y)) * this->AnchorData.Pivot.Y;
	}
	else
	{
		CalculatedAnchoredPosition.X = TempRelativeLocation.Y;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z;
	}

	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;

	if (AnchorData.AnchoredPosition != CalculatedAnchoredPosition)
	{
		AnchorData.AnchoredPosition = CalculatedAnchoredPosition;
	}
}
void UDreamWidget::CalculateTransformFromAnchor()
{
	bool HorizontalPositionChanged = false, VerticalPositionChanged = false;
	CalculateTransformFromAnchor(HorizontalPositionChanged, VerticalPositionChanged);
}
void UDreamWidget::CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged)
{
	bCanSetAnchorFromTransform = false;
	FVector ResultLocation = this->GetRelativeLocation();
	if (Parent.IsValid())
	{
		float LocalLeftPoint = //this left point anchor position in parent's space
			Parent->GetLocalSpaceLeft()//parent's left position
			+ (Parent->GetWidth() * this->AnchorData.AnchorMin.X);//add anchor offset
		float LocalLeftPivotPoint = //to pivot point, with anchor offset
			LocalLeftPoint
			+ (Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X))//parent anchor width (width without SizeDelta)
				* this->AnchorData.Pivot.X
			+ this->AnchorData.AnchoredPosition.X;

		float LocalBottomPoint = //this bottom point anchor position in parent's space
			Parent->GetLocalSpaceBottom()//parent's bottom position
			+ (Parent->GetHeight() * this->AnchorData.AnchorMin.Y);//add anchor offset
		float LocalBottomPivotPoint = //to pivot point, with anchor offset
			LocalBottomPoint
			+ (Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y))//parent anchor width (width without SizeDelta)
				* this->AnchorData.Pivot.Y
			+ this->AnchorData.AnchoredPosition.Y;

		ResultLocation.Y = LocalLeftPivotPoint;
		ResultLocation.Z = LocalBottomPivotPoint;
	}
	else
	{
		ResultLocation.Y = this->AnchorData.AnchoredPosition.X;
		ResultLocation.Z = this->AnchorData.AnchoredPosition.Y;
	}

	auto OriginRelativeLocation = this->GetRelativeLocation();
	double Tolerance = 0.0f;
	if (FMath::Abs(OriginRelativeLocation.Y - ResultLocation.Y) > Tolerance)
	{
		OutHorizontalPositionChanged = true;
	}
	if (FMath::Abs(OriginRelativeLocation.Z - ResultLocation.Z) > Tolerance)
	{
		OutVerticalPositionChanged = true;
	}
	if (OutHorizontalPositionChanged || OutVerticalPositionChanged)
	{
		this->SetRelativeLocation(ResultLocation);
	}
	bCanSetAnchorFromTransform = true;
}

#pragma region AnchorData

void UDreamWidget::SyncAnimatableGeometryMirrors() const
{
	auto* MutableThis = const_cast<UDreamWidget*>(this);
	MutableThis->AnimatableWidth = CacheWidth;
	MutableThis->AnimatableHeight = CacheHeight;
	MutableThis->AnimatableAnchorLeft = CacheAnchorOffsetLeft;
	MutableThis->AnimatableAnchorRight = CacheAnchorOffsetRight;
	MutableThis->AnimatableAnchorTop = CacheAnchorOffsetTop;
	MutableThis->AnimatableAnchorBottom = CacheAnchorOffsetBottom;
}

float UDreamWidget::GetWidth() const
{
	if (bCacheWidthDirty)
	{
		bCacheWidthDirty = false;
		SyncAnimatableGeometryMirrors();
		if (Parent.IsValid())
		{
			if (AnchorData.IsHorizontalStretched())
			{
				CacheWidth = AnchorData.SizeDelta.X + Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X);
			}
			else
			{
				CacheWidth = AnchorData.SizeDelta.X;
			}
		}
		else
		{
			CacheWidth = AnchorData.SizeDelta.X;
		}
	}
	return CacheWidth;
}
float UDreamWidget::GetHeight() const
{
	if (bCacheHeightDirty)
	{
		bCacheHeightDirty = false;
		SyncAnimatableGeometryMirrors();
		if (Parent.IsValid())
		{
			if (AnchorData.IsVerticalStretched())
			{
				CacheHeight = AnchorData.SizeDelta.Y + Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y);
			}
			else
			{
				CacheHeight = AnchorData.SizeDelta.Y;
			}
		}
		else
		{
			CacheHeight = AnchorData.SizeDelta.Y;
		}
	}
	return CacheHeight;
}

void UDreamWidget::SetAnchorData(const FDreamUIAnchorData& Value)
{
	AnchorData.Pivot = Value.Pivot;
	AnchorData.AnchorMin = Value.AnchorMin;
	AnchorData.AnchorMax = Value.AnchorMax;
	AnchorData.AnchoredPosition = Value.AnchoredPosition;
	AnchorData.SizeDelta = Value.SizeDelta;

	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;

	MarkAnchorDataChanged_Recursive(true, true, true, false);
	MarkLayoutForRebuild(this);
}

void UDreamWidget::SetPivot(FVector2D Value) 
{
	if (!AnchorData.Pivot.Equals(Value, 0.0f))
	{
		AnchorData.Pivot = Value;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		MarkAnchorDataChanged_Recursive(true, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetAnchorMin(FVector2D Value)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMin.Equals(Value, 0.0f))
		{
			auto CurrentLeft = this->GetAnchorOffsetLeft();
			auto CurrentBottom = this->GetAnchorOffsetBottom();

			AnchorData.AnchorMin = Value;
			
			//SetAnchorLeft
			{
				auto CurrentRight = this->GetAnchorOffsetRight();
				CacheWidth = -CurrentRight - CurrentLeft;
				//SetWidth
				AnchorData.SizeDelta.X = CacheWidth;
				this->AnchorData.AnchoredPosition.X = CurrentLeft + CacheWidth * this->AnchorData.Pivot.X;
			}

			//SetAnchorBottom
			{
				auto CurrentTop = this->GetAnchorOffsetTop();
				CacheHeight = -CurrentTop - CurrentBottom;
				//SetHeight
				AnchorData.SizeDelta.Y = CacheHeight;
				this->AnchorData.AnchoredPosition.Y = CurrentBottom + CacheHeight * this->AnchorData.Pivot.Y;
			}

			MarkAnchorDataChanged_Recursive(false, true, true, false);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}
void UDreamWidget::SetAnchorMax(FVector2D Value)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMax.Equals(Value, 0.0f))
		{
			auto CurrentRight = this->GetAnchorOffsetRight();
			auto CurrentTop = this->GetAnchorOffsetTop();

			AnchorData.AnchorMax = Value;

			//SetAnchorRight
			{
				auto CurrentLeft = this->GetAnchorOffsetLeft();
				CacheWidth = -CurrentRight - CurrentLeft;
				//SetWidth
				AnchorData.SizeDelta.X = CacheWidth;
				this->AnchorData.AnchoredPosition.X = CurrentLeft + CacheWidth * this->AnchorData.Pivot.X;
			}
			//SetAnchorTop
			{
				auto CurrentBottom = this->GetAnchorOffsetBottom();
				CacheHeight = -CurrentTop - CurrentBottom;
				//SetHeight
				AnchorData.SizeDelta.Y = CacheHeight;
				this->AnchorData.AnchoredPosition.Y = CurrentBottom + CacheHeight * this->AnchorData.Pivot.Y;
			}

			MarkAnchorDataChanged_Recursive(false, true, true, false);
			MarkLayoutForRebuild(this);;
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void UDreamWidget::SetAnchorOffset(FMargin Value)
{
	if (this->Parent.IsValid())
	{
		bool bWidthChange = CacheAnchorOffsetLeft != Value.Left || CacheAnchorOffsetRight != Value.Right;
		bool bHeightChange = CacheAnchorOffsetBottom != Value.Bottom || CacheAnchorOffsetTop != Value.Top;
		if (bCacheAnchorOffsetLeftDirty || bCacheAnchorOffsetRightDirty || bWidthChange || bHeightChange)
		{
			bCacheAnchorOffsetLeftDirty = false;
			SyncAnimatableGeometryMirrors();
			bCacheAnchorOffsetRightDirty = false;
			SyncAnimatableGeometryMirrors();
			bCacheAnchorOffsetBottomDirty = false;
			SyncAnimatableGeometryMirrors();
			bCacheAnchorOffsetTopDirty = false;
			CacheAnchorOffsetLeft = Value.Left;
			CacheAnchorOffsetRight = Value.Right;
			CacheAnchorOffsetBottom = Value.Bottom;
			CacheAnchorOffsetTop = Value.Top;
			SyncAnimatableGeometryMirrors();
			
			CacheWidth = -Value.Right - Value.Left;
			//SetWidth
			AnchorData.SizeDelta.X = CacheWidth;
			AnchorData.AnchoredPosition.X = Value.Left + CacheWidth * AnchorData.Pivot.X;

			CacheHeight = -Value.Top - Value.Bottom;
			//SetHeight
			AnchorData.SizeDelta.Y = CacheHeight;
			AnchorData.AnchoredPosition.Y = Value.Bottom + CacheHeight * AnchorData.Pivot.Y;
			
			MarkAnchorDataChanged_Recursive(false, bWidthChange, bHeightChange, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetLeftDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

void UDreamWidget::SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMin.Equals(MinValue, 0.0f) || !AnchorData.AnchorMax.Equals(MaxValue, 0.0f))
		{
			auto PrevRelativeLocation = this->GetRelativeLocation();
			auto PrevWidth = this->GetWidth();
			auto PrevHeight = this->GetHeight();
			this->SetAnchorMin(MinValue);
			this->SetAnchorMax(MaxValue);
			if (bKeepSize)
			{
				this->SetWidth(PrevWidth);
				this->SetHeight(PrevHeight);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void UDreamWidget::SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (AnchorData.AnchorMin.X != Value.X || AnchorData.AnchorMax.X != Value.Y)
		{
			auto CurrentLeft = this->GetAnchorOffsetLeft();
			auto CurrentRight = this->GetAnchorOffsetRight();

			if (bKeepSize)
			{
				CacheWidth = this->GetWidth();
			}
			auto PrevRelativeLocation = this->GetRelativeLocation();

			AnchorData.AnchorMin.X = Value.X;
			AnchorData.AnchorMax.X = Value.Y;

			//SetAnchorLeft & SetAnchorRight
			{
				if (!bKeepSize)//recalculate size on new anchor if not keep size
				{
					CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - CurrentLeft;
				}
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, this->AnchorData.Pivot.X);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}

			MarkAnchorDataChanged_Recursive(false, !bKeepSize, !bKeepSize, false);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}
void UDreamWidget::SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (AnchorData.AnchorMin.Y != Value.X || AnchorData.AnchorMax.Y != Value.Y)
		{
			auto CurrentBottom = this->GetAnchorOffsetBottom();
			auto CurrentTop = this->GetAnchorOffsetTop();

			if (bKeepSize)
			{
				CacheHeight = this->GetHeight();
			}
			auto PrevRelativeLocation = this->GetRelativeLocation();

			AnchorData.AnchorMin.Y = Value.X;
			AnchorData.AnchorMax.Y = Value.Y;

			//SetAnchorBottom && SetAnchorTop
			{
				if (!bKeepSize)//recalculate size on new anchor if not keep size
				{
					CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - CurrentBottom;
				}
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, this->AnchorData.Pivot.Y);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}

			MarkAnchorDataChanged_Recursive(false, !bKeepSize, !bKeepSize, false);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void UDreamWidget::SetAnchoredPosition(FVector2D Value)
{
	if (!AnchorData.AnchoredPosition.Equals(Value, 0.0f))
	{
		AnchorData.AnchoredPosition = Value;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetHorizontalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.X != Value)
	{
		AnchorData.AnchoredPosition.X = Value;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}
void UDreamWidget::SetVerticalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.Y != Value)
	{
		AnchorData.AnchoredPosition.Y = Value;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetSizeDelta(FVector2D Value)
{
	if (!AnchorData.SizeDelta.Equals(Value, 0.0f))
	{
		AnchorData.SizeDelta = Value;
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, true, true, false);
		MarkLayoutForRebuild(this);
	}
}

void UDreamWidget::SetAnchoredPositionAndSizeDelta(FVector2D Position, FVector2D Size)
{
	bool bPosChange = false, bSizeChange = false;
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		bPosChange = true;
		AnchorData.AnchoredPosition = Position;
	}
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		bSizeChange = true;
		AnchorData.SizeDelta = Size;
		CacheWidth = Size.X;
		CacheHeight = Size.Y;
	}
	if (bPosChange || bSizeChange)
	{
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, bSizeChange, bSizeChange, false);
		MarkLayoutForRebuild(this);
	}
}

float UDreamWidget::GetAnchorOffsetLeft()const
{
	if (bCacheAnchorOffsetLeftDirty)
	{
		bCacheAnchorOffsetLeftDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetLeft = this->AnchorData.AnchoredPosition.X - this->AnchorData.SizeDelta.X * this->AnchorData.Pivot.X;
		}
		else
		{
			CacheAnchorOffsetLeft = this->GetLocalSpaceLeft();//local space left
		}
	}
	return CacheAnchorOffsetLeft;
}
float UDreamWidget::GetAnchorOffsetTop()const
{
	if (bCacheAnchorOffsetTopDirty)
	{
		bCacheAnchorOffsetTopDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetTop = -(this->AnchorData.AnchoredPosition.Y + this->AnchorData.SizeDelta.Y * (1.0f - this->AnchorData.Pivot.Y));
		}
		else
		{
			CacheAnchorOffsetTop = this->GetLocalSpaceTop();
		}
	}
	return CacheAnchorOffsetTop;
}
float UDreamWidget::GetAnchorOffsetRight()const
{
	if (bCacheAnchorOffsetRightDirty)
	{
		bCacheAnchorOffsetRightDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetRight = -(this->AnchorData.AnchoredPosition.X + this->AnchorData.SizeDelta.X * (1.0f - this->AnchorData.Pivot.X));
		}
		else
		{
			CacheAnchorOffsetRight = this->GetLocalSpaceRight();
		}
	}
	return CacheAnchorOffsetRight;
}
float UDreamWidget::GetAnchorOffsetBottom()const
{
	if (bCacheAnchorOffsetBottomDirty)
	{
		bCacheAnchorOffsetBottomDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetBottom = this->AnchorData.AnchoredPosition.Y - this->AnchorData.SizeDelta.Y * this->AnchorData.Pivot.Y;
		}
		else
		{
			CacheAnchorOffsetBottom = this->GetLocalSpaceBottom();
		}
	}
	return CacheAnchorOffsetBottom;
}

FMargin UDreamWidget::GetAnchorOffset() const
{
	return FMargin(
		this->GetAnchorOffsetLeft(),
		this->GetAnchorOffsetTop(),
		this->GetAnchorOffsetRight(),
		this->GetAnchorOffsetBottom()
	);
}

void UDreamWidget::SetAnchorOffsetLeft(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetLeft != Value || bCacheAnchorOffsetLeftDirty)
		{
			bCacheAnchorOffsetLeftDirty = false;
			CacheAnchorOffsetLeft = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentRight = this->GetAnchorOffsetRight();
			CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - Value;
			//SetWidth
			{
				if (AnchorData.IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					AnchorData.SizeDelta.X = CacheWidth;
				}
			}
			this->AnchorData.AnchoredPosition.X = FMath::Lerp(Value, -CurrentRight, this->AnchorData.Pivot.X);
			MarkAnchorDataChanged_Recursive(false, true, false, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetLeftDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetTop(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetTop != Value || bCacheAnchorOffsetTopDirty)
		{
			bCacheAnchorOffsetTopDirty = false;
			CacheAnchorOffsetTop = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentBottom = this->GetAnchorOffsetBottom();
			CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - Value - CurrentBottom;
			//SetHeight
			{
				if (AnchorData.IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					AnchorData.SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -Value, this->AnchorData.Pivot.Y);
			MarkAnchorDataChanged_Recursive(false, false, true, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetTopDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetRight(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetRight != Value || bCacheAnchorOffsetRightDirty)
		{
			bCacheAnchorOffsetRightDirty = false;
			CacheAnchorOffsetRight = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentLeft = this->GetAnchorOffsetLeft();
			CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - Value - CurrentLeft;
			//SetWidth
			{
				if (AnchorData.IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					AnchorData.SizeDelta.X = CacheWidth;
				}
			}
			this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -Value, this->AnchorData.Pivot.X);
			MarkAnchorDataChanged_Recursive(false, true, false, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetRightDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetBottom(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetBottom != Value || bCacheAnchorOffsetBottomDirty)
		{
			bCacheAnchorOffsetBottomDirty = false;
			CacheAnchorOffsetBottom = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentTop = this->GetAnchorOffsetTop();
			CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - Value;
			//SetHeight
			{
				if (AnchorData.IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					AnchorData.SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchorData.AnchoredPosition.Y = FMath::Lerp(Value, -CurrentTop, this->AnchorData.Pivot.Y);
			MarkAnchorDataChanged_Recursive(false, false, true, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetBottomDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

void UDreamWidget::SetWidth(float Value)
{
	if (CacheWidth != Value || bCacheWidthDirty)
	{
		bCacheWidthDirty = false;
		CacheWidth = Value;
		SyncAnimatableGeometryMirrors();
		if (Parent.IsValid())
		{
			if (AnchorData.IsHorizontalStretched())
			{
				auto CalculatedSizeDeltaX = Value - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
				if (AnchorData.SizeDelta.X != CalculatedSizeDeltaX)
				{
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
					MarkAnchorDataChanged_Recursive(false, true, false, false);
					MarkLayoutForRebuild(this);
				}
			}
			else
			{
				if (AnchorData.SizeDelta.X != Value)
				{
					AnchorData.SizeDelta.X = Value;
					MarkAnchorDataChanged_Recursive(false, true, false, false);
					MarkLayoutForRebuild(this);
				}
			}
		}
		else
		{
			if (AnchorData.SizeDelta.X != Value)
			{
				AnchorData.SizeDelta.X = Value;
				MarkAnchorDataChanged_Recursive(false, true, false, false);
				MarkLayoutForRebuild(this);
			}
		}
	}
}
void UDreamWidget::SetHeight(float Value)
{
	if (CacheHeight != Value || bCacheHeightDirty)
	{
		bCacheHeightDirty = false;
		CacheHeight = Value;
		SyncAnimatableGeometryMirrors();
		if (Parent.IsValid())
		{
			if (AnchorData.IsVerticalStretched())
			{
				auto CalculatedSizeDeltaY = Value - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
				if (AnchorData.SizeDelta.Y != CalculatedSizeDeltaY)
				{
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
					MarkAnchorDataChanged_Recursive(false, false, true, false);
					MarkLayoutForRebuild(this);
				}
			}
			else
			{
				if (AnchorData.SizeDelta.Y != Value)
				{
					AnchorData.SizeDelta.Y = Value;
					MarkAnchorDataChanged_Recursive(false, false, true, false);
					MarkLayoutForRebuild(this);
				}
			}
		}
		else
		{
			if (AnchorData.SizeDelta.Y != Value)
			{
				AnchorData.SizeDelta.Y = Value;
				MarkAnchorDataChanged_Recursive(false, false, true, false);
				MarkLayoutForRebuild(this);
			}
		}
	}
}

#pragma endregion

void UDreamWidget::RegisterRenderCanvas(UDreamCanvas* InRenderCanvas)
{
	bIsCanvasWidget = true;
	UDreamCanvas* ParentCanvas = nullptr;
	if (auto ParentWidget = GetParent())
	{
		ParentCanvas = ParentWidget->GetComponentInParent<UDreamCanvas>();//@todo: replace with Canvas's ParentCanvas?
	}
	if (RenderCanvas != InRenderCanvas)
	{
		SetRenderCanvas(InRenderCanvas);
	}
	InRenderCanvas->SetParentCanvas(ParentCanvas);
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(InRenderCanvas);
		}
	}
}
void UDreamWidget::RenewRenderCanvasRecursive(UDreamCanvas* InParentRenderCanvas)
{
	auto ThisRenderCanvas = this->GetComponent<UDreamCanvas>();
	if (ThisRenderCanvas != nullptr)
	{
		ThisRenderCanvas = nullptr;
	}
	if (ThisRenderCanvas != nullptr)
	{
		if (InParentRenderCanvas != ThisRenderCanvas)
		{
			ThisRenderCanvas->SetParentCanvas(InParentRenderCanvas);//set parent Canvas for this actor's Canvas
		}
		return;
	}

	if (RenderCanvas != InParentRenderCanvas)//if attach to new Canvas, need to remove from old and add to new
	{
		SetRenderCanvas(InParentRenderCanvas);
	}

	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(InParentRenderCanvas);
		}
	}
}

void UDreamWidget::UnregisterRenderCanvas()
{
	bIsCanvasWidget = false;
	UDreamCanvas* ParentCanvas = nullptr;
	if (auto ParentWidget = GetParent())
	{
		ParentCanvas = ParentWidget->GetComponentInParent<UDreamCanvas>();//@todo: replace with Canvas's ParentCanvas?
	}
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->SetParentCanvas(nullptr);
	}
	if (RenderCanvas != ParentCanvas)
	{
		SetRenderCanvas(ParentCanvas);
	}
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(ParentCanvas);
		}
	}
}

void UDreamWidget::UpdateLayout()
{
	// Everything written from here down is layout output, not something anybody asked for; see
	// IsLayoutWriting.
	++LayoutPassDepth;
	ON_SCOPE_EXIT{ --LayoutPassDepth; };
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->CalculateSize();
	}
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->CalculateLayout();
	}
}

void UDreamWidget::UpdateClip(UDreamUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FDreamUIClipData>>& ClipDataList)
{
	if (!bClipDirty)return;
	bClipDirty = false;
	
	if (bNeedRecreateClip && ClipData.IsValid())
	{
		if (ClipData.Pin()->GetWidget() == this)//remove old clip-data
		{
			ClipDataList.Remove(ClipData.Pin());
		}
		else
		{
			ClipData = nullptr;//will create new
		}
	}
	bNeedRecreateClip = false;
	
	TSharedPtr<FDreamUIClipData> ParentClip = nullptr;
	if (Parent.IsValid())
	{
		ParentClip = Parent->ClipData.Pin();
	}
	switch (GetClipping())
	{
	case EDreamWidgetClipping::Inherit:
		this->ClipData = ParentClip;
		break;
	case EDreamWidgetClipping::ClipToBounds:
		{
			if (!this->ClipData.IsValid())
			{
				auto NewClip = MakeShared<FDreamUIClipData>(ParentClip, ClipDataTexture, this);
				ClipDataList.Add(NewClip);
				this->ClipData = NewClip;
			}
		}
		break;
	case EDreamWidgetClipping::ClipToBoundsWithoutIntersecting:
		{
			if (!this->ClipData.IsValid())
			{
				auto NewClip = MakeShared<FDreamUIClipData>(nullptr, ClipDataTexture, this);
				ClipDataList.Add(NewClip);
				this->ClipData = NewClip;
			}
		}
		break;
	case EDreamWidgetClipping::Disabled:
		this->ClipData = nullptr;
		break;
	}
	if (Visual)
	{
		Visual->CheckClipDataStartPosition();
	}
}

void UDreamWidget::UpdateVisual() const
{
	if (IsValid(Visual))
	{
		Visual->UpdateGeometry();
	}
}

void UDreamWidget::ForceUpdateLayout()
{
	MarkWidgetLayoutDirty();
	UpdateLayout();
}

void UDreamWidget::SetRenderCanvas(UDreamCanvas* InNewCanvas)
{
	auto OldRenderCanvas = RenderCanvas;
	RenderCanvas = InNewCanvas;
	if (ClipData.IsValid() && ClipData.Pin()->GetWidget() == this)//delete old clip-data
	{
		if (OldRenderCanvas.IsValid())
		{
			OldRenderCanvas->RemoveClipData(ClipData.Pin());//remove it from old canvas
		}
	}
	if (OldRenderCanvas.IsValid())
	{
		OldRenderCanvas->RemoveDreamWidget(this);
		if (IsValid(Visual))
		{
			OldRenderCanvas->MarkVisualWillChange(Visual);
			OldRenderCanvas->UnregisterVisual(Visual);
		}
	}
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->AddDreamWidget(this);
		bClipDirty = true;//mark it dirty so it will be added to new canvas
		if (IsValid(Visual))
		{
			RenderCanvas->RegisterVisual(Visual);
		}
	}
}

void UDreamWidget::OnHierarchyAttachmentChanged(UDreamCanvas* ParentRenderCanvas, UDreamWidget* ParentRoot)
{
	auto ThisRenderCanvas = this->GetComponent<UDreamCanvas>();
	if (ThisRenderCanvas != nullptr)
	{
		ParentRenderCanvas = ThisRenderCanvas;
	}

	if (RenderCanvas != ParentRenderCanvas)//if attach to new Canvas, need to remove from old and add to new
	{
		SetRenderCanvas(ParentRenderCanvas);
	}

	CheckRootWidget(ParentRoot);
	for (auto& Child : Children)
	{
		if (IsValid(Child))
		{
			Child->OnHierarchyAttachmentChanged(ParentRenderCanvas, ParentRoot);
		}
	}

	//flatten hierarchy index
	MarkFlattenHierarchyIndexDirty();

	{
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		
		MarkAnchorDataChanged_Recursive(false, true, true, false, false);
		MarkLayoutForRebuild(this);
	}

	Call_AttachmentChanged();
}

void UDreamWidget::OnRenderCanvasChanged(UDreamCanvas* OldCanvas, UDreamCanvas* NewCanvas)
{
	if (IsValid(OldCanvas))
	{
		OldCanvas->RemoveDreamWidget(this);
	}
	if (IsValid(NewCanvas))
	{
		NewCanvas->AddDreamWidget(this);
	}
	if (IsValid(Visual))
	{
		Visual->OnRenderCanvasChanged(OldCanvas, NewCanvas);
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

void UDreamWidget::CalculateWidgetActive_Recursive()
{
	struct LOCAL
	{
		static void CalculateWidgetActive(UDreamWidget* Widget)
		{
			bool bResultActive = true;
			if (!Widget->bWidgetActive || Widget->bParked)
				bResultActive = false;
			else if (Widget->Parent.IsValid())
				bResultActive = Widget->Parent->GetWidgetActiveInHierarchy();
			else
				bResultActive = true;

			if (Widget->bCacheWidgetActiveInHierarchy != bResultActive)
			{
				Widget->bCacheWidgetActiveInHierarchy = bResultActive;
				//callback
				Widget->Call_WidgetActiveChanged();
				//canvas update
				Widget->MarkCanvasUpdate(true);
				//refresh layout tree
				if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(Widget->GetWorld()))
				{
					DreamUIManager->MarkRebuildAllLayoutTree();
				}
				//tell layout
				MarkLayoutForRebuild(Widget);
			}
			for (auto& Child : Widget->GetChildren())
			{
				// Children can hold nulls -- garbage collection clears a reference to a widget it
				// took while this one lived, and this walk runs from OnDetachedFromParent, which is
				// exactly when a hierarchy is coming apart. Every other walk in this file guards;
				// this one did not, and dereferenced the null on its first line.
				if (IsValid(Child))
				{
					CalculateWidgetActive(Child);
				}
			}
		}
	};
	LOCAL::CalculateWidgetActive(this);
}

void UDreamWidget::CalculateVisibility_Recursive()
{
	struct FVisibilityCalculator
	{
		static void Calculate(UDreamWidget* Widget)
		{
			const bool bParentLayoutVisible = !Widget->Parent.IsValid() || Widget->Parent->bCacheLayoutVisibleInHierarchy;
			const bool bParentRenderVisible = !Widget->Parent.IsValid() || Widget->Parent->bCacheRenderVisibleInHierarchy;
			const bool bParentAllowsHitTest = !Widget->Parent.IsValid() || Widget->Parent->bCacheChildrenHitTestVisibleInHierarchy;
			const bool bActive = Widget->bCacheWidgetActiveInHierarchy;

			bool bDesignerVisible = true;
#if WITH_EDITOR
			if (Widget->bHiddenInDesigner && (!Widget->GetWorld() || !Widget->GetWorld()->IsGameWorld()))
			{
				bDesignerVisible = false;
			}
#endif

			const bool bCollapsed = Widget->Visibility == EDreamWidgetVisibility::Collapsed || Widget->bLayoutVisibilitySuppressed;
			const bool bPaints = Widget->Visibility != EDreamWidgetVisibility::Hidden && !bCollapsed;
			const bool bBlocksChildren = Widget->Visibility == EDreamWidgetVisibility::HitTestInvisible;
			const bool bSelfAcceptsHit = Widget->Visibility == EDreamWidgetVisibility::Visible;

			const bool bNewLayoutVisible = bActive && bParentLayoutVisible && !bCollapsed;
			const bool bNewRenderVisible = bActive && bDesignerVisible && bParentRenderVisible && bPaints;
			const bool bNewChildrenHitTestVisible = bNewRenderVisible && bParentAllowsHitTest && !bBlocksChildren;
			const bool bNewSelfHitTestVisible = bNewChildrenHitTestVisible && bSelfAcceptsHit;

			const bool bLayoutChanged = Widget->bCacheLayoutVisibleInHierarchy != bNewLayoutVisible;
			const bool bRenderChanged = Widget->bCacheRenderVisibleInHierarchy != bNewRenderVisible;
			const bool bHitTestChanged = Widget->bCacheSelfHitTestVisibleInHierarchy != bNewSelfHitTestVisible
				|| Widget->bCacheChildrenHitTestVisibleInHierarchy != bNewChildrenHitTestVisible;

			Widget->bCacheLayoutVisibleInHierarchy = bNewLayoutVisible;
			Widget->bCacheRenderVisibleInHierarchy = bNewRenderVisible;
			Widget->bCacheSelfHitTestVisibleInHierarchy = bNewSelfHitTestVisible;
			Widget->bCacheChildrenHitTestVisibleInHierarchy = bNewChildrenHitTestVisible;

			if (bLayoutChanged)
			{
				// Cached layout trees keep collapsed subtrees and filter at update time, so this wipe is
				// belt-and-braces (it is also swallowed while a layout pass is executing); the layout pass
				// itself no longer depends on it to see a revealed subtree.
				if (UDreamUIManagerWorldSubsystem* DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(Widget->GetWorld()))
				{
					DreamUIManager->MarkRebuildAllLayoutTree();
				}
				UDreamWidget::MarkLayoutForRebuild(Widget->Parent.IsValid() ? Widget->Parent.Get() : Widget);
			}
			if (bRenderChanged || bHitTestChanged)
			{
				Widget->MarkCanvasUpdate(true);
			}

			for (UDreamWidget* Child : Widget->GetChildren())
			{
				if (IsValid(Child))
				{
					Calculate(Child);
				}
			}
		}
	};
	FVisibilityCalculator::Calculate(this);
}
void UDreamWidget::CalculateInteractable_Recursive()
{
	struct LOCAL
	{
		static void CalculateInteractable(UDreamWidget* Widget)
		{
			bool bResultInteractable = true;
			switch (Widget->Interactable)
			{
			case EDreamWidgetInteractableType::Enabled:
				bResultInteractable = true;
				break;
			case EDreamWidgetInteractableType::Disabled:
				bResultInteractable = false;
				break;
			case EDreamWidgetInteractableType::Inherit:
				if (Widget->Parent.IsValid())
					bResultInteractable = Widget->Parent->GetInteractableInHierarchy();
				else
					bResultInteractable = true;
				break;
			}

			if (Widget->bCacheInteractableInHierarchy != bResultInteractable)
			{
				Widget->bCacheInteractableInHierarchy = bResultInteractable;
				Widget->Call_InteractableChanged();
			}
			for (auto& Child : Widget->GetChildren())
			{
				// Same guard the visibility walk already had, and for the same reason: Children can
				// hold nulls, and this runs from OnDetachedFromParent -- when a hierarchy is coming
				// apart is exactly when it will.
				if (IsValid(Child))
				{
					CalculateInteractable(Child);
				}
			}
		}
	};
	LOCAL::CalculateInteractable(this);
}
void UDreamWidget::CalculateRaycastable_Recursive()
{
	struct LOCAL
	{
		static void CalculateRaycastable(UDreamWidget* Widget)
		{
			bool bResult = true;
			switch (Widget->Raycastable)
			{
			case EDreamWidgetRaycastableType::Disabled:
				bResult = false;
				break;
			case EDreamWidgetRaycastableType::Enabled:
				bResult = true;
				break;
			case EDreamWidgetRaycastableType::Inherit:
				if (Widget->Parent.IsValid())
					bResult = Widget->Parent->GetRaycastableInHierarchy();
				else
					bResult = true;
				break;
			}

			if (Widget->bCacheRaycastableInHierarchy != bResult)
			{
				Widget->bCacheRaycastableInHierarchy = bResult;
				Widget->Call_RaycastableChanged();
			}
			for (auto& Child : Widget->GetChildren())
			{
				// Same guard the visibility walk already had, and for the same reason: Children can
				// hold nulls, and this runs from OnDetachedFromParent -- when a hierarchy is coming
				// apart is exactly when it will.
				if (IsValid(Child))
				{
					CalculateRaycastable(Child);
				}
			}
		}
	};
	LOCAL::CalculateRaycastable(this);
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

UDreamCanvas* UDreamWidget::GetRootCanvas()const
{
	if (RenderCanvas.IsValid())
	{
		return RenderCanvas->GetRootCanvas();
	}
	return nullptr;
}

USceneComponent* UDreamWidget::GetAttachedRootSceneComponent() const
{
	if (auto RootCanvas = GetRootCanvas())
	{
		return RootCanvas->GetAttachedRootSceneComponent();
	}
	return nullptr;
}

FVector2D UDreamWidget::GetLocalSpaceLeftBottomPoint()const
{
	FVector2D leftBottomPoint;
	leftBottomPoint.X = GetWidth() * -AnchorData.Pivot.X;
	leftBottomPoint.Y = GetHeight() * -AnchorData.Pivot.Y;
	return leftBottomPoint;
}
FVector2D UDreamWidget::GetLocalSpaceRightTopPoint()const
{
	FVector2D rightTopPoint;
	rightTopPoint.X = GetWidth() * (1.0f - AnchorData.Pivot.X);
	rightTopPoint.Y = GetHeight() * (1.0f - AnchorData.Pivot.Y);
	return rightTopPoint;
}
FVector2D UDreamWidget::GetLocalSpaceCenter()const
{
	return FVector2D(this->GetWidth() * (0.5f - AnchorData.Pivot.X), this->GetHeight() * (0.5f - AnchorData.Pivot.Y));
}

float UDreamWidget::GetLocalSpaceLeft()const
{
	return this->GetWidth() * -AnchorData.Pivot.X;
}
float UDreamWidget::GetLocalSpaceRight()const
{
	return this->GetWidth() * (1.0f - AnchorData.Pivot.X);
}
float UDreamWidget::GetLocalSpaceBottom()const
{
	return this->GetHeight() * -AnchorData.Pivot.Y;
}
float UDreamWidget::GetLocalSpaceTop()const
{
	return this->GetHeight() * (1.0f - AnchorData.Pivot.Y);
}

void UDreamWidget::MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	// No clip invalidation here: clip rectangles are recomputed and diffed every tick from the owner's world
	// transform (see FDreamUIClipData::UpdateData), so there is nothing to mark.
	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}
	if (GetLayoutSelf())
	{
		LayoutSelf->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}
	if (IsValid(Visual))
	{
		Visual->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}

	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(InPivotChanged || InWidthChanged || InHeightChanged);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			this->RenderCanvas->MarkTransformOrDimensionChanged();
		}
	}

	Call_DimensionsChanged(InPivotChanged, InWidthChanged, InHeightChanged);
}

void UDreamWidget::MarkTransformChanged()
{
	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(true);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			//This is mainly to mark DreamGUICanvas's bIsViewProjectionMatrixDirty to true.
			//For the condition DreamGUI_Tutorials/Tutorials/UIRenderTarget, when move DreamGUIRenderTarget at runtime, the DreamGUICanvas's RenderTarget's matrix not update, result in wrong interaction.
			this->RenderCanvas->MarkTransformOrDimensionChanged();
		}
	}

	Call_TransformChanged();
}

void UDreamWidget::MarkAnchorDataChanged_Recursive(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache, bool InPropagateToChildren)
{
	CalculateTransformFromAnchor();

	if (InDiscardCache)
	{
		if (InWidthChanged)
		{
			bCacheWidthDirty = true;
		}
		if (InHeightChanged)
		{
			bCacheHeightDirty = true;
		}
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
	}
	MarkDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);

	// A size change that did not come out of a layout pass is a new authored intent, so the panel slot's
	// measurement snapshot has to follow it. Without this the snapshot froze at whatever the widget
	// measured when its slot was first registered - nothing outside the editor and prefab paths ever
	// re-captured it - so the next pass measured the child from that stale value and wrote the old size
	// straight back. A runtime SetWidth, or UDreamSpriteBase::SetSprite swapping in art of a different size,
	// visibly flashed and snapped back. This funnel only ever runs on the widget the setter was called on:
	// the recursion below hands children to the by-layout-container twin instead.
	if ((InWidthChanged || InHeightChanged) && !IsLayoutWriting() && IsValid(PanelSlot))
	{
		PanelSlot->SyncAuthoredDesiredSizeFromWidget();
	}

	if (!InPropagateToChildren)return;
	for (auto& Child : GetChildren())
	{
		if (!IsValid(Child))continue;
		bool ChildWidthChange = false, ChildHeightChange = false;
		if (InWidthChanged && Child->AnchorData.IsHorizontalStretched())
		{
			ChildWidthChange = true;
		}
		if (InHeightChanged && Child->AnchorData.IsVerticalStretched())
		{
			ChildHeightChange = true;
		}
		Child->MarkAnchorDataChanged_Recursive(false, ChildWidthChange, ChildHeightChange);

		//check if child need layout rebuild, the widget self is already marked outside of this function
		if (ChildWidthChange || ChildHeightChange//parent size change may cause child layout change
			|| ((InWidthChanged || InHeightChanged) && Child->GetLayoutSelf())//parent size changed and parent can affect child layout, need calculate child layout
			)
		{
			MarkLayoutForRebuild(Child);
		}
	}
}

void UDreamWidget::MarkCanvasUpdate(bool bRebuildDrawCall)const
{
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->MarkCanvasUpdate(bRebuildDrawCall);
	}
}

void UDreamWidget::SetPositionAndSizeForLayoutAnimation(FVector2D Position, FVector2D Size)
{
	bool AnyChanged = false;
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		AnyChanged = true;
		AnchorData.AnchoredPosition = Position;
	}
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		AnyChanged = true;
		AnchorData.SizeDelta = Size;
		CacheWidth = Size.X;
		CacheHeight = Size.Y;
	}
	if (AnyChanged)
	{
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::SetPositionForLayoutAnimation(FVector2D Position)
{
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		AnchorData.AnchoredPosition = Position;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::SetSizeForLayoutAnimation(FVector2D Size)
{
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		AnchorData.SizeDelta = Size;
		CacheWidth = Size.X;
		CacheHeight = Size.Y;

		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::MarkAnchorDataChangedByLayoutContainer_Recursive(bool InPivotChanged, bool InWidthChanged,
                                                                  bool InHeightChanged, bool InDiscardCache, bool InPropagateToChildren)
{
	CalculateTransformFromAnchor();

	if (InDiscardCache)
	{
		if (InWidthChanged)
		{
			bCacheWidthDirty = true;
		}
		if (InHeightChanged)
		{
			bCacheHeightDirty = true;
		}
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
	}
	MarkDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);

	if (!InPropagateToChildren)return;
	for (auto& Child : GetChildren())
	{
		if (!IsValid(Child))continue;
		bool ChildWidthChange = false, ChildHeightChange = false;
		if (InWidthChanged && Child->AnchorData.IsHorizontalStretched())
		{
			ChildWidthChange = true;
		}
		if (InHeightChanged && Child->AnchorData.IsVerticalStretched())
		{
			ChildHeightChange = true;
		}
		Child->MarkAnchorDataChangedByLayoutContainer_Recursive(false, ChildWidthChange, ChildHeightChange);
	}
}

float UDreamWidget::GetLayoutProperty(TFunctionRef<float(UDreamLayoutSelf*)> GetLayoutSelfProperty,
                                    TFunctionRef<float(UDreamLayoutContainer*)> GetLayoutContainerProperty,
                                    TFunctionRef<float(UDreamVisual*)> GetVisualProperty,
                                    float DefaultValue)const
{
	if (IsValid(LayoutSelf))
	{
		auto Value = GetLayoutSelfProperty(LayoutSelf);
		if (Value >= 0)//enable override
		{
			return Value;
		}
	}
	if (IsValid(LayoutContainer))
	{
		auto Value = GetLayoutContainerProperty(LayoutContainer);
		if (Value >= 0)//enable override
		{
			return Value;
		}
	}
	if (IsValid(Visual))
	{
		auto Value = GetVisualProperty(Visual);
		if (Value >= 0)
		{
			return Value;
		}
	}
	return DefaultValue;
}
UObject* UDreamWidget::GetLayoutSource(TFunctionRef<float(UDreamLayoutSelf*)> GetLayoutSelfProperty,
	TFunctionRef<float(UDreamLayoutContainer*)> GetLayoutContainerProperty,
	TFunctionRef<float(UDreamVisual*)> GetVisualProperty) const
{
	if (IsValid(LayoutSelf))
	{
		auto Value = GetLayoutSelfProperty(LayoutSelf);
		if (Value >= 0)//enable override
		{
			return LayoutSelf;
		}
	}
	if (IsValid(LayoutContainer))
	{
		auto Value = GetLayoutContainerProperty(LayoutContainer);
		if (Value >= 0)//enable override
		{
			return LayoutContainer;
		}
	}
	if (IsValid(Visual))
	{
		auto Value = GetVisualProperty(Visual);
		if (Value >= 0)
		{
			return Visual;
		}
	}
	return nullptr;
}

UDreamCanvas* UDreamWidget::GetRenderCanvas()const
{
	return RenderCanvas.Get();
}

bool UDreamWidget::IsScreenSpaceOverlayUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToScreenSpace();
}
bool UDreamWidget::IsRenderTargetUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToRenderTarget();
}
bool UDreamWidget::IsWorldSpaceUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToWorldSpace();
}

TArray<UDreamWidget*> UDreamWidget::LayoutWriterStack;
int32 UDreamWidget::LayoutPassDepth = 0;

UDreamWidget::FLayoutWriteScope::FLayoutWriteScope(UDreamWidget* InLayoutWidget)
{
	if (IsValid(InLayoutWidget))
	{
		LayoutWriterStack.Push(InLayoutWidget);
		bPushed = true;
	}
}

UDreamWidget::FLayoutWriteScope::~FLayoutWriteScope()
{
	if (bPushed)
	{
		LayoutWriterStack.Pop(EAllowShrinking::No);
	}
}

void UDreamWidget::MarkLayoutForRebuild(UDreamWidget* InWidget)
{
	MarkLayoutForRebuild(InWidget, EDreamLayoutInvalidation::Measure);
}

void UDreamWidget::MarkLayoutForRebuild(UDreamWidget* InWidget, EDreamLayoutInvalidation Reason)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	static IConsoleVariable* LayoutTraceCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("dreamgui.LayoutTrace"));
	if (LayoutTraceCVar && LayoutTraceCVar->GetInt() != 0)
	{
		UE_LOG(DreamGUI, Log, TEXT("[LayoutTrace] invalidate %s reason=%s registered=%d"),
			*GetNameSafe(InWidget),
			Reason == EDreamLayoutInvalidation::Arrange ? TEXT("Arrange") : TEXT("Measure"),
			InWidget->HasRegistered() ? 1 : 0);
	}

	if (Reason == EDreamLayoutInvalidation::Arrange)
	{
		// Nothing above the parent can come out differently: a panel measures its children by desired
		// size and never by where they sit, so a widget that only moved leaves every preferred size on
		// the chain exactly as it was. The parent still re-arranges, which is what keeps a panel-managed
		// child from staying where it was put - the visible behaviour is unchanged, only the reach is.
		UDreamWidget* ParentWidget = InWidget->GetParent();
		if (!IsValid(ParentWidget))
		{
			return;
		}
		UDreamLayoutContainer* ParentLayout = ParentWidget->GetLayoutContainer();
		if (!IsValid(ParentLayout))
		{
			return;
		}
		// A writer applying its own arrangement must not be re-dirtied by it, same as the walk below.
		if (LayoutWriterStack.Contains(ParentWidget))
		{
			return;
		}
		ParentLayout->MarkLayoutDirty();
		if (UDreamLayoutSelf* LayoutSelf = InWidget->GetLayoutSelf(); IsValid(LayoutSelf))
		{
			LayoutSelf->MarkLayoutDirty();
		}
		ParentWidget->MarkWidgetLayoutDirty();
		return;
	}

	UDreamWidget* TargetWidget = InWidget;
	UDreamWidget* RebuildRoot = nullptr;
	bool bStoppedAtLayoutWriter = false;
	TSet<const UDreamWidget*> VisitedWidgets;
	// Desired-size dependencies may cross plain wrapper widgets, so dirty every layout on the ancestor chain.
	while (IsValid(TargetWidget) && !VisitedWidgets.Contains(TargetWidget))
	{
		// A layout that is applying its own results must not be re-dirtied by them. Stop the walk at the
		// writer: everything below it still gets dirtied, because a nested container does have to react to
		// the size it was just handed, but the writer and its ancestors keep the dirty state they already
		// consumed. Widgets outside the writer's subtree never reach this branch and behave as before.
		if (LayoutWriterStack.Contains(TargetWidget))
		{
			bStoppedAtLayoutWriter = true;
			break;
		}
		VisitedWidgets.Add(TargetWidget);
		if (UDreamLayoutContainer* LayoutContainer = TargetWidget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			LayoutContainer->MarkLayoutDirty();
			RebuildRoot = TargetWidget;
		}
		if (UDreamLayoutSelf* LayoutSelf = TargetWidget->GetLayoutSelf(); IsValid(LayoutSelf))
		{
			LayoutSelf->MarkLayoutDirty();
			RebuildRoot = TargetWidget;
		}
		if (TargetWidget->GetIgnoreLayout())
		{
			break;
		}

		// No relayout boundary here, and it is not an oversight. Blink stops this walk at a box whose
		// size cannot be affected from inside it, and the obvious translation - stop where the parent
		// panel sizes the child by Fill - was written, measured and reverted.
		//
		// It does not hold. A panel's MeasureLayout sums its children's desired sizes even for children
		// it sizes by Fill, so the panel's preferred size still moves when their content does. The
		// tempting rescue is that GetLayoutPreferredSize recomputes on every call and caches nothing, so
		// there is no stale value anywhere - true, and irrelevant: the ancestor still has to RUN to
		// consume the new one, and stopping the walk is precisely what stops it running. Three text
		// invalidation tests went to zero reflows and the convergence test went from one pass to two.
		//
		// A sound version has to establish that no ancestor's size derives from this subtree at all,
		// which is a walk to the root - the walk being avoided. Cacheable per widget, invalidated on
		// slot and hierarchy changes; that is a design, not a condition.
		TargetWidget = TargetWidget->GetParent();
	}
	if (bStoppedAtLayoutWriter)
	{
		// The pass that is running collected the writer's whole subtree and visits it in pre-order, so any
		// descendant dirtied above is still ahead of the cursor. Enqueuing it would only buy a second pass.
		return;
	}
	if (!IsValid(RebuildRoot))
	{
		RebuildRoot = InWidget;
	}
	if (IsValid(RebuildRoot))
	{
		RebuildRoot->MarkWidgetLayoutDirty();
	}
}

void UDreamWidget::RebuildLayoutImmediately(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(InWidget->GetWorld()))
	{
		DreamUIManager->RebuildLayoutImmediately(InWidget);
	}
}

void UDreamWidget::MarkWidgetLayoutDirty()
{
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->AddLayoutDirtyWidget(this);
	}
}

void UDreamWidget::MarkClipDirty(bool InClipTypeChanged) const
{
	bClipDirty = true;
	if (InClipTypeChanged)bNeedRecreateClip = true;
	// Waking the canvas is required, not optional. UDreamCanvas::UpdateCanvasDrawCall gates everything that
	// reconciles clipping behind bCanTickUpdate: UpdateClip (creates/destroys the FDreamUIClipData),
	// UDreamVisual::CheckClipDataStartPosition (refreshes the slot index baked into vertex data) and
	// MarkFinishUpdateCanvasDrawCall (uploads the clip blocks). Marking bClipDirty without waking the canvas
	// leaves a widget sitting on Clipping == ClipToBounds with no ClipData, while its visual still points at a
	// recycled clip slot — the shader then clips against a stale rectangle and the subtree renders nothing.
	MarkCanvasUpdate(true);
	struct LOCAL
	{
		static void MarkDirty(const UDreamWidget* Widget, bool InClipTypeChanged, TSet<const UDreamWidget*>& VisitedWidgets)
		{
			if (!IsValid(Widget) || VisitedWidgets.Contains(Widget))
			{
				return;
			}
			VisitedWidgets.Add(Widget);
			switch (Widget->GetClipping())
			{
			case EDreamWidgetClipping::Inherit:
			case EDreamWidgetClipping::ClipToBounds:
				Widget->bClipDirty = true;
				if (InClipTypeChanged)Widget->bNeedRecreateClip = true;
				// Descendants may render through a different (child) canvas, so wake each one individually.
				Widget->MarkCanvasUpdate(true);
				break;
			case EDreamWidgetClipping::ClipToBoundsWithoutIntersecting:
			case EDreamWidgetClipping::Disabled:
				return;
			}

			for (const UDreamWidget* Child : Widget->GetChildren())
			{
				MarkDirty(Child, InClipTypeChanged, VisitedWidgets);
			}
		}
	};
	TSet<const UDreamWidget*> VisitedWidgets;
	VisitedWidgets.Add(this);
	for (const UDreamWidget* Child : this->GetChildren())
	{
		LOCAL::MarkDirty(Child, InClipTypeChanged, VisitedWidgets);
	}
}
bool UDreamWidget::IsPointVisibleOnClip(const FVector& Value) const
{
	if (ClipData.IsValid())
	{
		return ClipData.Pin()->IsPointVisible(Value);
	}
	return true;
}
void UDreamWidget::SetClipping(EDreamWidgetClipping Value)
{
	if (Clipping != Value)
	{
		const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
		Clipping = Value;
		if (PreviousEffectiveClipping != GetClipping())
		{
			MarkClipDirty(true);
		}
	}
}

void UDreamWidget::SetLayoutClippingOverride(EDreamWidgetClipping Value)
{
	const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
	bHasLayoutClippingOverride = true;
	LayoutClippingOverride = Value;
	if (PreviousEffectiveClipping != GetClipping())
	{
		MarkClipDirty(true);
	}
}

void UDreamWidget::ClearLayoutClippingOverride()
{
	if (!bHasLayoutClippingOverride)
	{
		return;
	}
	const EDreamWidgetClipping PreviousEffectiveClipping = GetClipping();
	bHasLayoutClippingOverride = false;
	LayoutClippingOverride = EDreamWidgetClipping::Inherit;
	if (PreviousEffectiveClipping != GetClipping())
	{
		MarkClipDirty(true);
	}
}
void UDreamWidget::SetClippingCornerRadius(FVector4f Value)
{
	if (ClippingCornerRadius != Value)
	{
		ClippingCornerRadius = Value;
		MarkClipDirty(false);
	}
}

void UDreamWidget::SetClippingMargin(FMargin Value)
{
	if (ClippingMargin != Value)
	{
		ClippingMargin = Value;
		MarkClipDirty(false);
	}
}

float UDreamWidget::GetFinalRenderOpacity()const
{
	if (Parent.IsValid())
	{
		return this->RenderOpacity * Parent->GetFinalRenderOpacity();
	}
	return this->RenderOpacity;
}
void UDreamWidget::SetRenderOpacity(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (RenderOpacity != Value)
	{
		RenderOpacity = Value;
		struct LOCAL
		{
			static void MarkDirty(const UDreamWidget* Widget)
			{
				if (Widget->Visual)
				{
					Widget->Visual->MarkColorDirty();
				}
				for (auto& Child : Widget->Children)
				{
					MarkDirty(Child);
				}
			}
		};
		LOCAL::MarkDirty(this);
	}
}

bool UDreamWidget::GetPixelSnappingInHierarchy() const
{
	switch (this->PixelSnapping)
	{
	case EWidgetPixelSnapping::SnapToPixel:
		return true;
	case EWidgetPixelSnapping::Disabled:
		return false;
	case EWidgetPixelSnapping::Inherit:
		if (Parent.IsValid())
		{
			return Parent->GetPixelSnappingInHierarchy();
		}
		return false;
	}
	return false;
}

void UDreamWidget::SetPixelSnapping(EWidgetPixelSnapping Value)
{
	if (PixelSnapping != Value)
	{
		PixelSnapping = Value;
		struct LOCAL
		{
			static void MarkChanged(const UDreamWidget* Widget)
			{
				if (Widget->Visual)
				{
					Widget->Visual->OnPixelSnappingChanged();
				}
				for (auto& Child : Widget->GetChildren())
				{
					MarkChanged(Child);
				}
			}
		};
		LOCAL::MarkChanged(this);
	}
}

bool UDreamWidget::GetWidgetActiveInHierarchy() const
{
	return bCacheWidgetActiveInHierarchy;
}

#if WITH_EDITOR
void UDreamWidget::SetHiddenInDesigner(bool bHidden)
{
	if (bHiddenInDesigner != bHidden)
	{
		bHiddenInDesigner = bHidden;
		CalculateVisibility_Recursive();
	}
}
#endif

void UDreamWidget::SetParked(bool Value)
{
	if (bParked != Value)
	{
		bParked = Value;
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
	}
}

void UDreamWidget::SetWidgetActive(bool Value)
{
	if (bWidgetActive != Value)
	{
		bWidgetActive = Value;
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
	}
}

void UDreamWidget::SetVisibility(EDreamWidgetVisibility Value)
{
	if (Visibility != Value)
	{
		Visibility = Value;
		CalculateVisibility_Recursive();
		OnVisibilityChanged.Broadcast(Visibility);
	}
}

void UDreamWidget::SetLayoutVisibilitySuppressed(bool bSuppressed)
{
	if (bLayoutVisibilitySuppressed != bSuppressed)
	{
		bLayoutVisibilitySuppressed = bSuppressed;
		// SizeBox, ScaleBox and WidgetSwitcher all flip this from inside their own arrange. It changes
		// which widgets a measurement is allowed to include, so every memoised desired size in the pass
		// may now be wrong - not just this widget's. Outside a pass the memo is empty and this is free.
		UDreamPanelLayoutBase::ForgetAllDesiredSizes();
		CalculateVisibility_Recursive();
	}
}

bool UDreamWidget::SetFocus(int32 UserIndex, int32 PointerId)
{
	if (!bIsFocusable || !GetRenderVisibleInHierarchy() || !GetInteractableInHierarchy())
	{
		return false;
	}
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, UserIndex))
	{
		UDreamBaseEventData* EventData = EventSystem->GetPointerEventData(PointerId, true);
		EventSystem->SetSelectWidget(this, EventData);
		return true;
	}
	return false;
}

bool UDreamWidget::HasFocus(int32 UserIndex, int32 PointerId) const
{
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(const_cast<UDreamWidget*>(this), UserIndex))
	{
		return EventSystem->GetCurrentSelectedComponent(PointerId) == this;
	}
	return false;
}

void UDreamWidget::ClearFocus(int32 UserIndex, int32 PointerId)
{
	if (UDreamEventSystem* EventSystem = UDreamEventSystem::GetDreamEventSystemInstance(this, UserIndex))
	{
		UDreamBaseEventData* EventData = EventSystem->GetPointerEventData(PointerId, false);
		if (EventData && EventData->SelectedComponent == this)
		{
			EventSystem->SetSelectWidget(nullptr, EventData);
		}
	}
}

void UDreamWidget::NotifyFocusReceived(int32 UserIndex, int32 PointerId)
{
	OnFocusReceived.Broadcast(UserIndex, PointerId);
	if (AccessibleBehavior != EDreamAccessibleBehavior::NotAccessible)
	{
		AnnounceAccessibleText();
	}
}

void UDreamWidget::NotifyFocusLost(int32 UserIndex, int32 PointerId)
{
	OnFocusLost.Broadcast(UserIndex, PointerId);
}

void UDreamWidget::AnnounceAccessibleText(const FText& Announcement)
{
#if WITH_ACCESSIBILITY
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FText TextToAnnounce = Announcement;
	if (TextToAnnounce.IsEmpty())
	{
		TextToAnnounce = AccessibleBehavior == EDreamAccessibleBehavior::Summary ? AccessibleSummaryText : AccessibleText;
	}
	if (TextToAnnounce.IsEmpty())
	{
		TextToAnnounce = FText::FromString(DisplayName);
	}
	if (!TextToAnnounce.IsEmpty())
	{
		FSlateApplication::Get().GetAccessibleMessageHandler()->MakeAccessibleAnnouncement(TextToAnnounce.ToString());
	}
#endif
}

void UDreamWidget::SetRaycastable(EDreamWidgetRaycastableType Value)
{
	if (Raycastable != Value)
	{
		Raycastable = Value;
		CalculateRaycastable_Recursive();
	}
}

void UDreamWidget::SetInteractable(EDreamWidgetInteractableType Value)
{
	if (Interactable != Value)
	{
		Interactable = Value;
		CalculateInteractable_Recursive();
	}
}

void UDreamWidget::SetIgnoreLayout(bool Value)
{
	if (bIgnoreLayout != Value)
	{
		bIgnoreLayout = Value;
		// Mark from the parent, not from here. MarkLayoutForRebuild breaks its ancestor walk on the first
		// widget with IgnoreLayout set - and we just set it - so starting at `this` stopped immediately and
		// the container that has to close the gap never heard about it. Turning the flag off happened to
		// work, because by then the flag reads false, which made this look like a one-way switch.
		MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			DreamUIManager->MarkRebuildAllLayoutTree();
		}
	}
}

const UDreamWidget* UDreamWidget::GetRestrictNavigationAreaWidget() const
{
	if (bRestrictNavigationArea)
	{
		return this;
	}
	if (Parent.IsValid())
	{
		return Parent->GetRestrictNavigationAreaWidget();
	}
	return nullptr;
}

void UDreamWidget::SetRestrictNavigationArea(bool Value)
{
	bRestrictNavigationArea = Value;
}

void UDreamWidget::SetNavigationBoundaryRule(EDreamUINavigationBoundaryRule Value)
{
	NavigationBoundaryRule = Value;
}

UDreamVisual* UDreamWidget::CreateNewVisual(TSubclassOf<UDreamVisual> VisualClass)
{
	auto OldVisual = Visual;
	auto NewVisual = NewObject<UDreamVisual>(this, VisualClass, NAME_None, RF_Public | RF_Transactional);
	if (RenderCanvas.IsValid())
	{
		if (IsValid(OldVisual))
		{
			RenderCanvas->MarkVisualWillChange(OldVisual);
			RenderCanvas->UnregisterVisual(OldVisual);
		}
		if (NewVisual)
		{
			RenderCanvas->RegisterVisual(NewVisual);
		}
	}
	if (IsValid(OldVisual))
	{
		if (bHasBegunPlay)
		{
			OldVisual->EndPlay();
		}
		OldVisual->Call_OnUnregister();
	}
	
	NewVisual->Call_OnRegister();
	if (bHasBegunPlay)
	{
		NewVisual->BeginPlay();
	}
	Visual = NewVisual;
	return NewVisual;
}

void UDreamWidget::RemoveVisual()
{
	auto OldVisual = Visual;
	Visual = nullptr;

	if (IsValid(OldVisual))
	{
		if (bHasBegunPlay)
		{
			OldVisual->EndPlay();
		}
		OldVisual->Call_OnUnregister();
		if (RenderCanvas.IsValid())
		{
			RenderCanvas->MarkVisualWillChange(OldVisual);
			RenderCanvas->UnregisterVisual(OldVisual);
		}
	}
}

UDreamLayoutContainer* UDreamWidget::CreateNewLayoutContainer(TSubclassOf<UDreamLayoutContainer> LayoutClass)
{
	UClass* RequestedClass = *LayoutClass;
	if (!IsValid(RequestedClass)
		|| !RequestedClass->IsChildOf(UDreamLayoutContainer::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	const UDreamLayoutContainer* RequestedLayoutDefault = Cast<UDreamLayoutContainer>(RequestedClass->GetDefaultObject());
	if (IsValid(RequestedLayoutDefault))
	{
		const int32 MaxChildren = RequestedLayoutDefault->GetMaxChildren();
		if (MaxChildren >= 0)
		{
			int32 ValidChildCount = 0;
			for (const UDreamWidget* Child : Children)
			{
				ValidChildCount += IsValid(Child) ? 1 : 0;
			}
			if (ValidChildCount > MaxChildren)
			{
				return nullptr;
			}
		}
	}
	auto OldLayout = LayoutContainer;
	auto NewLayout = NewObject<UDreamLayoutContainer>(this, RequestedClass, NAME_None, RF_Public | RF_Transactional);
	if (!IsValid(NewLayout))
	{
		return nullptr;
	}
	const bool bInitializeScaleBoxSlots = NewLayout->IsA<UDreamLayoutContainerScaleBox>()
		&& (!IsValid(OldLayout) || !OldLayout->IsA<UDreamLayoutContainerScaleBox>());
	if (IsValid(OldLayout))
	{
		if (bHasBegunPlay)
		{
			OldLayout->EndPlay();
		}
		OldLayout->Call_OnUnregister();
	}
	
	NewLayout->Call_OnRegister();
	if (bHasBegunPlay)
	{
		NewLayout->BeginPlay();
	}
	LayoutContainer = NewLayout;
	if (IsValid(Cast<UDreamPanelLayoutBase>(NewLayout)))
	{
		for (UDreamWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				// UMG creates a fresh ScaleBoxSlot with Center/Center defaults when the panel type changes.
				// Dream reuses its generic slot, so initialize those defaults explicitly on the same transition.
				if (bInitializeScaleBoxSlots)
				{
					if (UDreamPanelSlot* ExistingSlot = Child->GetPanelSlot(); IsValid(ExistingSlot))
					{
#if WITH_EDITOR
						if (const UWorld* World = Child->GetWorld(); !World || !World->IsGameWorld())
						{
							ExistingSlot->Modify();
						}
#endif
						ExistingSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
						ExistingSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
					}
				}
				EnsurePanelSlotForChild(this, Child, true);
			}
		}
	}
	else
	{
		for (UDreamWidget* Child : Children)
		{
			RemovePanelSlotFromChild(Child);
		}
	}
	SyncRequiredBehavioursForLayoutContainer(OldLayout, NewLayout);
	MarkLayoutForRebuild(this);
	MarkDimensionChanged(false, true, true);//change LayoutContainer could cause LayoutSelf size change
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
	return NewLayout;
}

void UDreamWidget::RemoveLayoutContainer()
{
	auto OldLayout = LayoutContainer;
	LayoutContainer = nullptr;

	if (IsValid(OldLayout))
	{
		if (bHasBegunPlay)
		{
			OldLayout->EndPlay();
		}
		OldLayout->Call_OnUnregister();
	}
	for (UDreamWidget* Child : Children)
	{
		RemovePanelSlotFromChild(Child);
	}
	SyncRequiredBehavioursForLayoutContainer(OldLayout, nullptr);
	MarkLayoutForRebuild(this);
	MarkDimensionChanged(false, true, true);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
}

UDreamLayoutSelf* UDreamWidget::CreateNewLayoutSelf(TSubclassOf<UDreamLayoutSelf> LayoutClass)
{
	UClass* RequestedClass = *LayoutClass;
	if (!IsValid(RequestedClass)
		|| !RequestedClass->IsChildOf(UDreamLayoutSelf::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	auto OldLayout = LayoutSelf;
	auto NewLayout = NewObject<UDreamLayoutSelf>(this, RequestedClass, NAME_None, RF_Public | RF_Transactional);
	if (!IsValid(NewLayout))
	{
		return nullptr;
	}
	if (IsValid(OldLayout))
	{
		if (bHasBegunPlay)
		{
			OldLayout->EndPlay();
		}
		OldLayout->Call_OnUnregister();
	}
	
	NewLayout->Call_OnRegister();
	if (bHasBegunPlay)
	{
		NewLayout->BeginPlay();
	}
	LayoutSelf = NewLayout;
	MarkLayoutForRebuild(this);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
	return NewLayout;
}

void UDreamWidget::RemoveLayoutSelf()
{
	auto OldLayout = LayoutSelf;
	LayoutSelf = nullptr;

	if (IsValid(OldLayout))
	{
		if (bHasBegunPlay)
		{
			OldLayout->EndPlay();
		}
		OldLayout->Call_OnUnregister();
	}
	MarkLayoutForRebuild(this);
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->MarkRebuildAllLayoutTree();
	}
}

UDreamPanelSlot* UDreamWidget::CreateNewPanelSlot(TSubclassOf<UDreamPanelSlot> SlotClass)
{
	UClass* RequestedClass = *SlotClass;
	if (!IsValid(RequestedClass))
	{
		RequestedClass = UDreamPanelSlot::StaticClass();
	}
	if (!RequestedClass->IsChildOf(UDreamPanelSlot::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	UDreamPanelSlot* OldSlot = PanelSlot;
	UDreamPanelSlot* NewSlot = NewObject<UDreamPanelSlot>(this, RequestedClass, NAME_None, RF_Public | RF_Transactional);
	if (!IsValid(NewSlot))
	{
		return nullptr;
	}
	if (IsValid(OldSlot))
	{
		OldSlot->RestoreAuthoredGeometry();
		if (bHasBegunPlay)
		{
			OldSlot->EndPlay();
		}
		OldSlot->Call_OnUnregister();
	}
	PanelSlot = NewSlot;
	if (IsValid(NewSlot))
	{
		NewSlot->Call_OnRegister();
		if (bHasBegunPlay)
		{
			NewSlot->BeginPlay();
		}
	}
	MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
	return NewSlot;
}

void UDreamWidget::RemovePanelSlot()
{
	UDreamPanelSlot* OldSlot = PanelSlot;
	if (IsValid(OldSlot))
	{
		OldSlot->RestoreAuthoredGeometry();
		if (bHasBegunPlay)
		{
			OldSlot->EndPlay();
		}
		OldSlot->Call_OnUnregister();
	}
	PanelSlot = nullptr;
	MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
}

#pragma region TweenAnimation


#pragma region PositionXYZ
UDreamTweener* UDreamWidget::LocalPositionXTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation().X;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.X = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::LocalPositionYTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation().Y;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.Y = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::LocalPositionZTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this] 
	{
		return this->GetRelativeLocation().Z;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.Z = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}



UDreamTweener* UDreamWidget::WorldPositionXTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this] 
	{
		return this->GetWorldLocation().X;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.X = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::WorldPositionYTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldLocation().Y;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.Y = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::WorldPositionZTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldLocation().Z;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.Z = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
#pragma endregion PositionXYZ




#pragma region Position
UDreamTweener* UDreamWidget::LocalPositionTo(FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenVectorGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation();
	})
	, FDreamTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		this->SetRelativeLocation(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::WorldPositionTo(FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenVectorGetterFunction::CreateUObject(this, &UDreamWidget::GetWorldLocation)
	, FDreamTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		return this->SetWorldLocation(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
#pragma endregion Position



UDreamTweener* UDreamWidget::LocalScaleTo(FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenVectorGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeScale();
	})
	, FDreamTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		this->SetRelativeScale(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::LocalUniformScaleTo(float endValue, float duration, float delay,	EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenFloatGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeScale().X;
	})
	, FDreamTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value)
	{
		this->SetRelativeScale(FVector(value));
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}


#pragma region Rotation
UDreamTweener* UDreamWidget::LocalRotationQuaternionTo(const FQuat& endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenQuaternionGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeRotation();
	}), FDreamTweenQuaternionSetterFunction::CreateUObject(this, &UDreamWidget::SetRelativeRotation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::LocalRotatorTo(FRotator endValue, bool shortestPath, float duration, float delay, EDreamTweenEase ease)
{
	if (shortestPath)
	{
		return LocalRotationQuaternionTo(endValue.Quaternion(), duration, delay, ease);
	}
	else
	{
		auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenRotatorGetterFunction::CreateWeakLambda(this, [this]
		{
			return this->GetRelativeRotation().Rotator();
		})
		, FDreamTweenRotatorSetterFunction::CreateWeakLambda(this, [this] (FRotator value)
		{
			this->SetRelativeRotation(value.Quaternion());
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
			UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
		}
		return Tweener;
	}
}



UDreamTweener* UDreamWidget::WorldRotationQuaternionTo(const FQuat& endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenQuaternionGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldRotation();
	}), FDreamTweenQuaternionSetterFunction::CreateUObject(this, &UDreamWidget::SetWorldRotation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::WorldRotatorTo(FRotator endValue, bool shortestPath, float duration, float delay, EDreamTweenEase ease)
{
	if (shortestPath)
	{
		return WorldRotationQuaternionTo(endValue.Quaternion(), duration, delay, ease);
	}
	else
	{
		auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenRotatorGetterFunction::CreateWeakLambda(this, [this]
		{
			return this->GetWorldRotation().Rotator();
		})
		, FDreamTweenRotatorSetterFunction::CreateWeakLambda(this, [this](FRotator value)
		{
			this->SetWorldRotation(value.Quaternion());
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
			UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
		}
		return Tweener;
	}
}

#pragma endregion Rotation


UDreamTweener* UDreamWidget::RenderOpacityTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetRenderOpacity)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetRenderOpacity)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::SizeDeltaTo(const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenVector2DGetterFunction::CreateUObject(this, &UDreamWidget::GetSizeDelta)
		, FDreamTweenVector2DSetterFunction::CreateUObject(this, &UDreamWidget::SetSizeDelta)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::WidthTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetWidth)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetWidth)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::HeightTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetHeight)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetHeight)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::AnchoredPositionTo(const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenVector2DGetterFunction::CreateUObject(this, &UDreamWidget::GetAnchoredPosition)
		, FDreamTweenVector2DSetterFunction::CreateUObject(this, &UDreamWidget::SetAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::HorizontalAnchoredPositionTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetHorizontalAnchoredPosition)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetHorizontalAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::VerticalAnchoredPositionTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetVerticalAnchoredPosition)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetVerticalAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

void UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(UDreamWidget* Widget, UDreamTweener* Tweener)
{
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (Widget->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<UDreamUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<UDreamUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<UDreamUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<UDreamUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
}
#pragma endregion

