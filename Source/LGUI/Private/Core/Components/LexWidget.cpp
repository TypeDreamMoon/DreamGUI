// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexWidget.h"
#include "LGUI.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LexUISettings.h"
#include "Core/LexUIManager.h"
#include "LTweenManager.h"
#include "Core/LexUIClipData.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexVisual.h"
#include "Event/LexEventSystem.h"
#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#include "Components/SceneComponent.h"
#include "Core/LexUIBehaviour.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

namespace
{
	void RemovePanelSlotFromChild(ULexWidget* ChildWidget)
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

	bool EnsurePanelSlotForChild(ULexWidget* ParentWidget, ULexWidget* ChildWidget, bool bRecaptureDesiredSize = false)
	{
		if (!IsValid(ParentWidget) || !IsValid(ChildWidget)
			|| !IsValid(Cast<ULexPanelLayoutBase>(ParentWidget->GetLayoutContainer())))
		{
			return false;
		}
		if (ULexPanelSlot* ExistingSlot = ChildWidget->GetPanelSlot(); IsValid(ExistingSlot))
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
		ULexPanelSlot* NewSlot = ChildWidget->CreateNewPanelSlot<ULexPanelSlot>();
		if (IsValid(NewSlot))
		{
			if (ParentWidget->GetLayoutContainer()->IsA<ULexLayoutContainerScaleBox>())
			{
				NewSlot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Center);
				NewSlot->SetVerticalAlignment(ELexPanelVerticalAlignment::Center);
			}
			NewSlot->CaptureAuthoredGeometry(bRecaptureDesiredSize);
		}
		return IsValid(NewSlot);
	}

	void SynchronizePanelSlotForParent(ULexWidget* ParentWidget, ULexWidget* ChildWidget,
		bool bRecaptureDesiredSize = false)
	{
		if (IsValid(ParentWidget)
			&& IsValid(Cast<ULexPanelLayoutBase>(ParentWidget->GetLayoutContainer())))
		{
			EnsurePanelSlotForChild(ParentWidget, ChildWidget, bRecaptureDesiredSize);
		}
		else
		{
			RemovePanelSlotFromChild(ChildWidget);
		}
	}
}

ULexWidget::ULexWidget()
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

void ULexWidget::BeginPlay()
{
	check(!bHasBegunPlay);
	bHasBegunPlay = true;

	for (auto Component : Components)
	{
		if (IsValid(Component))
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

void ULexWidget::EndPlay()
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
void ULexWidget::Call_InteractableChanged()
{
	OnInteractableChangedEvent.Broadcast(this->GetInteractableInHierarchy());
}
void ULexWidget::Call_TransformChanged()
{
	OnTransformChangedEvent.Broadcast();
}

void ULexWidget::Call_DimensionsChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);

	if (Parent.IsValid())
	{
		Parent->Call_ChildDimensionsChanged(this, InPivotChanged, InWidthChanged, InHeightChanged);
	}
}

void ULexWidget::Call_ChildDimensionsChanged(ULexWidget* Child, bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	OnChildDimensionChangedEvent.Broadcast(Child, InPivotChanged, InWidthChanged, InHeightChanged);
}

void ULexWidget::Call_AttachmentChanged()
{
	OnAttachmentChangedEvent.Broadcast();
}

void ULexWidget::Call_SiblingIndexChanged()
{
	OnSiblingIndexChangedEvent.Broadcast();
}

void ULexWidget::CollectChildrenWidgets(ULexWidget* Target, TArray<ULexWidget*>& OutAllChildrenWidgets, bool IncludeTarget)
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

void ULexWidget::Call_WidgetActiveChanged()
{
	OnWidgetActiveChangedEvent.Broadcast(this->GetWidgetActiveInHierarchy());
}
void ULexWidget::Call_RaycastableChanged()
{
	OnRaycastableChangedEvent.Broadcast(this->GetRaycastableInHierarchy());
}
#pragma endregion


void ULexWidget::CalculateFlattenHierarchyIndex_Recursive(int& index)const
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

DECLARE_CYCLE_STAT(TEXT("LexWidget CalculateFlattenHierarchyIndex"), STAT_LexWidgetCalculateFlattenHierarchyIndex, STATGROUP_LGUI);
void ULexWidget::RecalculateFlattenHierarchyIndex()const
{
	SCOPE_CYCLE_COUNTER(STAT_LexWidgetCalculateFlattenHierarchyIndex);

	this->bFlattenHierarchyIndexDirty = false;
	int tempIndex = this->FlattenHierarchyIndex;
	this->CalculateFlattenHierarchyIndex_Recursive(tempIndex);
}

int32 ULexWidget::GetFlattenHierarchyIndex()const
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

void ULexWidget::MarkFlattenHierarchyIndexDirty()
{
	if (RootWidget.IsValid())
	{
		RootWidget->bFlattenHierarchyIndexDirty = true;
	}
	//tell canvas to update
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->MarkCanvasHierarchyChanged();
		//if this LexWidget have a LGUICanvas, then we need to tell the upper canvas that hierarchy order change, in order to sort render order between canvas
		if (this->bIsCanvasWidget)
		{
			if (RenderCanvas->GetParentCanvas().IsValid())
			{
				RenderCanvas->GetParentCanvas()->MarkCanvasHierarchyChanged();
			}
		}
	}
}



void ULexWidget::ApplySiblingIndex()
{
	if (Parent.IsValid())
	{
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

void ULexWidget::SetAsFirstSibling()
{
	SetSiblingIndex(0);
}
void ULexWidget::SetAsLastSibling()
{
	if (Parent.IsValid())
	{
		SetSiblingIndex(Parent->Children.Num() - 1);
	}
}

FString ULexWidget::GetPathDisplayName(const UObject* StopOuter) const
{
	auto OuterPathName = GetOuter()->GetPathName(StopOuter);
	TStringBuilder<256> Result;
	Result.Append(OuterPathName);
	Result.AppendChar('/');
	TArray<const ULexWidget*> WidgetChain;
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

ULexWidget* ULexWidget::FindChildByDisplayName(const FString& InName, bool IncludeChildren)const
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
ULexWidget* ULexWidget::FindChildByDisplayNameWithChildren_Internal(const FString& InName)const
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
TArray<ULexWidget*> ULexWidget::FindChildArrayByDisplayName(const FString& InName, bool IncludeChildren)const
{
	TArray<ULexWidget*> resultArray;
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
void ULexWidget::FindChildArrayByDisplayNameWithChildren_Internal(const FString& InName, TArray<ULexWidget*>& OutResultArray)const
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

void ULexWidget::MarkAllDirtyRecursive()
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

void ULexWidget::MarkAllDirty()
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

void ULexWidget::MarkRenderModeChangeRecursive(ULexCanvas* Canvas, ELexRenderMode OldRenderMode, ELexRenderMode NewRenderMode)
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


void ULexWidget::PostLoad()
{
	Super::PostLoad();
	// RelativeRotationEuler is transient, so seed it from the serialized rotation. Loading writes
	// RelativeRotation through reflection rather than the setter, which would leave the mirror at
	// zero and make Sequencer restore an animated widget to no rotation at all.
	this->RelativeRotationEuler = this->RelativeRotation.Rotator();
}

void ULexWidget::BeginDestroy()
{
	if (bHasBegunPlay || bIsRegistered)
	{
		ULexWidget* TeardownRoot = RootWidget.GetEvenIfUnreachable();
		if (TeardownRoot == nullptr || TeardownRoot->HasAnyFlags(RF_FinishDestroyed))
		{
			TeardownRoot = this;
		}

		auto World = TeardownRoot->GetWorld();
		auto WorldName = World ? World->GetName() : TEXT("null");
		auto Manager = ULexUIManagerWorldSubsystem::GetInstance(World);
		auto ManagerName = Manager ? Manager->GetName() : TEXT("null");

		UE_LOG(LGUI, Error, TEXT("ULexWidget tree %s was not destroyed by its owner. World:%s, WorldType:%d, Manager:%s. Auto cleanup in BeginDestroy."),
			*TeardownRoot->GetPathDisplayName(), *WorldName, World ? World->WorldType : -1, *ManagerName);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red
				, FString::Printf(TEXT("ULexWidget tree %s was not destroyed by its owner; auto cleaned up. World:%s, Manager:%s")
					, *TeardownRoot->GetPathDisplayName(), *WorldName, *ManagerName));
		}

		// Elevating fallback cleanup to the hierarchy root prevents one diagnostic per child.
		TeardownRoot->DestroyWidget();
	}
	Super::BeginDestroy();
}

void ULexWidget::DestroyWidget()
{
	struct LOCAL
	{
		static void AppendSubtree(
			ULexWidget* Widget,
			TArray<TObjectPtr<ULexWidget>>& TeardownWidgets,
			TSet<const ULexWidget*>& ScheduledWidgets)
		{
			// BeginDestroy can run after GC has marked the object unreachable, at which point
			// IsValid() is already false even though teardown on the live memory is still required.
			if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
			{
				return;
			}

			TArray<TObjectPtr<ULexWidget>> PendingWidgets;
			PendingWidgets.Add(Widget);
			while (!PendingWidgets.IsEmpty())
			{
				ULexWidget* CurrentWidget = PendingWidgets.Pop(EAllowShrinking::No).Get();
				if (CurrentWidget == nullptr
					|| CurrentWidget->HasAnyFlags(RF_FinishDestroyed)
					|| ScheduledWidgets.Contains(CurrentWidget))
				{
					continue;
				}

				ScheduledWidgets.Add(CurrentWidget);
				TeardownWidgets.Add(CurrentWidget);
				const TArray<ULexWidget*> ChildrenSnapshot = CurrentWidget->GetChildren();
				for (int32 ChildIndex = ChildrenSnapshot.Num() - 1; ChildIndex >= 0; --ChildIndex)
				{
					PendingWidgets.Add(ChildrenSnapshot[ChildIndex]);
				}
			}
		}

		static void AppendCurrentChildren(
			ULexWidget* Widget,
			TArray<TObjectPtr<ULexWidget>>& TeardownWidgets,
			TSet<const ULexWidget*>& ScheduledWidgets)
		{
			if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
			{
				return;
			}

			const TArray<ULexWidget*> ChildrenSnapshot = Widget->GetChildren();
			for (ULexWidget* Child : ChildrenSnapshot)
			{
				AppendSubtree(Child, TeardownWidgets, ScheduledWidgets);
			}
		}
	};

	TArray<TObjectPtr<ULexWidget>> TeardownWidgets;
	TSet<const ULexWidget*> ScheduledWidgets;
	LOCAL::AppendSubtree(this, TeardownWidgets, ScheduledWidgets);

	int32 UnregisterIndex = 0;
	auto UnregisterPendingWidgets = [&]()
	{
		while (UnregisterIndex < TeardownWidgets.Num())
		{
			ULexWidget* Widget = TeardownWidgets[UnregisterIndex++].Get();
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
		ULexWidget* Widget = TeardownWidgets[WidgetIndex].Get();
		LOCAL::AppendCurrentChildren(Widget, TeardownWidgets, ScheduledWidgets);
	}
	UnregisterPendingWidgets();

	int32 EndPlayIndex = 0;
	while (EndPlayIndex < TeardownWidgets.Num())
	{
		UnregisterPendingWidgets();
		ULexWidget* Widget = TeardownWidgets[EndPlayIndex++].Get();
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

UWorld* ULexWidget::GetWorld() const
{
	auto OuterWorld = GetTypedOuter<UWorld>();
	return OuterWorld;
}

#if WITH_EDITOR
void ULexWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	static const FName AnchorDataName = GET_MEMBER_NAME_CHECKED(ULexWidget, AnchorData);
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

		static const FName WidgetActiveName = GET_MEMBER_NAME_CHECKED(ULexWidget, bWidgetActive);
		static const FName RaycastableName = GET_MEMBER_NAME_CHECKED(ULexWidget, Raycastable);
		static const FName ClippingName = GET_MEMBER_NAME_CHECKED(ULexWidget, Clipping);
		static const FName ClippingCornerRadiusName = GET_MEMBER_NAME_CHECKED(ULexWidget, ClippingCornerRadius);
		static const FName ClippingMarginName = GET_MEMBER_NAME_CHECKED(ULexWidget, ClippingMargin);
		static const FName VisualName = GET_MEMBER_NAME_CHECKED(ULexWidget, Visual);
		static const FName LayoutContainerName = GET_MEMBER_NAME_CHECKED(ULexWidget, LayoutContainer);
		static const FName LayoutSelfName = GET_MEMBER_NAME_CHECKED(ULexWidget, LayoutSelf);
		static const FName PanelSlotName = GET_MEMBER_NAME_CHECKED(ULexWidget, PanelSlot);
		static const FName VisibilityName = GET_MEMBER_NAME_CHECKED(ULexWidget, Visibility);
		static const FName IgnoreLayoutName = GET_MEMBER_NAME_CHECKED(ULexWidget, bIgnoreLayout);
		static const FName InteractableName = GET_MEMBER_NAME_CHECKED(ULexWidget, Interactable);
		static const FName RenderOpacityName = GET_MEMBER_NAME_CHECKED(ULexWidget, RenderOpacity);

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
		else if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, SiblingIndex))
		{
			// Same order as SetSiblingIndex: settle the value first, then broadcast it.
			ApplySiblingIndex();
			this->Call_SiblingIndexChanged();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeLocation) || MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeRotation) || MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeScale))
		{
			CalculateAnchorFromTransform();
			CalculateObjectToWorldTransform();
			OnUpdateTransform();
			MarkTransformChanged();
			MarkLayoutForRebuild(this);
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
			if (IsValid(LayoutContainer))
			{
				if (bHasBegunPlay)
				{
					LayoutContainer->BeginPlay();
				}
				LayoutContainer->Call_OnRegister();
				if (IsValid(Cast<ULexPanelLayoutBase>(LayoutContainer)))
				{
					for (ULexWidget* Child : Children)
					{
						EnsurePanelSlotForChild(this, Child, true);
					}
				}
				LayoutContainer->CalculateLayout();
			}
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
			MarkLayoutForRebuild(this);
		}
		if (MemberName == AnchorDataName)
		{
			CalculateTransformFromAnchor();
			this->CalculateObjectToWorldTransform();
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeLocation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeRotation)
			|| MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeScale))
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
				static void MarkDirty(const ULexWidget* Widget)
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
		ULexUIManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->MarkCanvasUpdate(true);
			}
		}, 1);
	}
}

void ULexWidget::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName MemberName = PropertyAboutToChange->GetFName();
	if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, Visual))
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
	else if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, LayoutContainer))
	{
		if (IsValid(LayoutContainer))
		{
			if (bHasBegunPlay)
			{
				LayoutContainer->EndPlay();
			}
			LayoutContainer->Call_OnUnregister();
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, LayoutSelf))
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
	else if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, PanelSlot))
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

bool ULexWidget::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	return bIsEditable;
}

bool ULexWidget::CanEditChange(const FEditPropertyChain& PropertyChain) const
{
	bool bIsEditable = UObject::CanEditChange( PropertyChain );
	return bIsEditable;
}

void ULexWidget::PostEditUndo()
{
	Super::PostEditUndo();
	// Undo restores RelativeRotation straight into the property, bypassing the setter that keeps
	// the transient euler mirror in step.
	this->RelativeRotationEuler = this->RelativeRotation.Rotator();
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
			static void RegisterRecursive(ULexWidget* Widget, TSet<const ULexWidget*>& VisitedWidgets)
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
				for (ULexWidget* Child : Widget->Children)
				{
					if (IsValid(Child))
					{
						RegisterRecursive(Child, VisitedWidgets);
					}
				}
			}
		};
		TSet<const ULexWidget*> VisitedWidgets;
		LOCAL::RegisterRecursive(this, VisitedWidgets);
	}

	// Transactional pointer swaps do not run the old layout's unregister path. Reset every
	// parent-owned transient before registering and rebuilding the currently restored layout.
	SetLayoutScale(FVector2f::UnitVector);
	SetLayoutVisibilitySuppressed(false);
	ClearLayoutClippingOverride();
	for (ULexWidget* Child : Children)
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
	if (IsValid(Cast<ULexLayoutContainerScrollBox>(LayoutContainer)))
	{
		SetLayoutClippingOverride(ELexWidgetClipping::ClipToBounds);
	}
	if (IsValid(Cast<ULexPanelLayoutBase>(LayoutContainer)))
	{
		for (ULexWidget* Child : Children)
		{
			EnsurePanelSlotForChild(this, Child);
		}
	}
	else
	{
		for (ULexWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				if (ULexPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
				{
					Slot->RestoreAuthoredGeometry();
				}
			}
		}
	}
	CalculateVisibility_Recursive();
	MarkLayoutForRebuild(this);
}

void ULexWidget::PostRename(UObject* OldOuter, const FName OldName)
{
	Super::PostRename(OldOuter, OldName);
}

void ULexWidget::EnsureChildrenAfterTransaction()
{
	struct LOCAL
	{
		static void CheckIt(ULexWidget* Widget)
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

void ULexWidget::EnsureDataForRebuild()
{
	check(this == RootWidget);
	struct LOCAL
	{
		static void RenewRenderCanvas(ULexWidget* Widget)
		{
			auto ThisRenderCanvas = Widget->GetComponent<ULexCanvas>();
			Widget->RenewRenderCanvasRecursive(ThisRenderCanvas);
		}
		static void EnsureDataForRebuildRecursive(ULexWidget* Widget)
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
		static void ForceRefreshRenderCanvasRecursive(ULexWidget* Widget)
		{
			auto NewRenderCanvas = Widget->GetComponentInParent<ULexCanvas>(true);
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
FVector ULexWidget::GetWorldLocation()const
{
	return GetWorldTransform().GetLocation();
}
FQuat ULexWidget::GetWorldRotation()const
{
	return GetWorldTransform().GetRotation();
}
FVector ULexWidget::GetWorldScale()const
{
	return GetWorldTransform().GetScale3D();
}

FVector ULexWidget::GetForwardVector() const
{
	return GetWorldTransform().GetRotation().GetForwardVector();
}

FVector ULexWidget::GetRightVector() const
{
	return GetWorldTransform().GetRotation().GetRightVector();
}

FVector ULexWidget::GetUpVector() const
{
	return GetWorldTransform().GetRotation().GetUpVector();
}

void ULexWidget::SetRelativeLocation(const FVector& Value)
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
				MarkLayoutForRebuild(this);
			}
		}
	}
}
void ULexWidget::SetRelativeRotation(const FQuat& Value)
{
	if (this->RelativeRotation != Value)
	{
		this->RelativeRotation = Value;
		this->RelativeRotationEuler = Value.Rotator();
		this->CalculateObjectToWorldTransform();
	}
}
void ULexWidget::SetRelativeRotationEuler(const FRotator& Value)
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
void ULexWidget::SetRelativeScale(const FVector& Value)
{
	if (this->RelativeScale != Value)
	{
		this->RelativeScale = Value;
		this->CalculateObjectToWorldTransform();
	}
}

void ULexWidget::SetLayoutScale(const FVector2f& Value)
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
void ULexWidget::SetRelativeLocationAndRotation(const FVector& InLocation, const FQuat& InRotation)
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
				MarkLayoutForRebuild(this);
			}
		}
	}
}

void ULexWidget::SetWorldLocation(const FVector& Value)
{
	auto WorldRotation = GetWorldRotation();
	SetWorldLocationAndRotation(Value, WorldRotation);
}
void ULexWidget::SetWorldRotation(const FQuat& Value)
{
	auto WorldPosition = GetWorldLocation();
	SetWorldLocationAndRotation(WorldPosition, Value);
}
void ULexWidget::SetWorldLocationAndRotation(const FVector& InLocation, const FQuat& InRotation)
{
	FTransform DesiredWorldTransform = GetWorldTransform();
	DesiredWorldTransform.SetLocation(InLocation);
	DesiredWorldTransform.SetRotation(InRotation);
	SetWorldTransform(DesiredWorldTransform);
}

FTransform ULexWidget::GetLocalTransform()const
{
	return FTransform(RelativeRotation, RelativeLocation,
		RelativeScale * FVector(1.0, LayoutScale.X, LayoutScale.Y));
}
const FTransform& ULexWidget::GetWorldTransform()const
{
	return ObjectToWorldTransform;
}

void ULexWidget::SetWorldTransform(const FTransform& InWorldTransform)
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
		LocalTransform = InWorldTransform.GetRelativeTransform(Parent->GetWorldTransform());
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

void ULexWidget::SetParentBeforeRegister(ULexWidget* InParent)
{
	check(!bIsRegistered);
	if (InParent == this || (IsValid(InParent) && InParent->IsChildOf(this)))
	{
		ensureMsgf(false, TEXT("Cannot restore cyclic LexWidget parent relationship for %s."), *GetPathName());
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

void ULexWidget::ApplySiblingIndexFromPrefab_Recursive()
{
	// Order by the restored indices first — StableSort keeps relative order across holes and duplicates
	// (legacy data, cross-parent moves) — then renumber contiguously so a later tail-append can never
	// collide with a restored index. Silent on purpose: this runs during prefab assembly/refresh, before
	// anything consumes the hierarchy.
	Children.StableSort([](const ULexWidget& A, const ULexWidget& B)
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

void ULexWidget::RestoreSiblingIndexFromPrefab(int32 InSiblingIndex)
{
	SiblingIndex = InSiblingIndex;
	if (Parent.IsValid())
	{
		Parent->bNeedSortUIChildren = true;
	}
}

ULexUIBehaviour* ULexWidget::AddComponent(TSubclassOf<ULexUIBehaviour> ComponentClass, ULexUIBehaviour* ComponentTemplate)
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
	auto NewComponent = NewObject<ULexUIBehaviour>(this, ComponentClass, NewComponentName, NewComponentFlags, ComponentTemplate);
	Components.Add(NewComponent);
	if (bIsRegistered)
	{
		NewComponent->OnRegister();
	}
	if (bHasBegunPlay)
	{
		NewComponent->BeginPlay();
	}
	OnComponentsChangedEvent.Broadcast(ELexWidgetComponentsChangedType::Added);
	return NewComponent;
}

ULexUIBehaviour* ULexWidget::AddComponent(TSubclassOf<ULexUIBehaviour> ComponentClass)
{
	return AddComponent(ComponentClass, nullptr);
}

ULexUIBehaviour* ULexWidget::AddComponentByTemplate(ULexUIBehaviour* ComponentTemplate)
{
	return AddComponent(ComponentTemplate->GetClass(), ComponentTemplate);
}

void ULexWidget::RemoveComponent(ULexUIBehaviour* Component)
{
	auto Index = Components.Find(Component);
	if (Index < 0)return;
	Components.RemoveAt(Index);
	if (bHasBegunPlay)
	{
		Component->EndPlay();
	}
	Component->OnUnregister();
	OnComponentsChangedEvent.Broadcast(ELexWidgetComponentsChangedType::Removed);
}

void ULexWidget::MoveComponentToIndex(ULexUIBehaviour* Component, int32 NewIndex)
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

	ULexUIBehaviour* MovingComponent = Components[SourceIndex];
	Components.RemoveAt(SourceIndex);
	Components.Insert(MovingComponent, FMath::Clamp(TargetIndex, 0, Components.Num()));
	OnComponentsChangedEvent.Broadcast(ELexWidgetComponentsChangedType::Reorder);
}

void ULexWidget::UpdateObjectToWorldTransform()
{
	auto LocalTransform = GetLocalTransform();
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
void ULexWidget::CalculateObjectToWorldTransform(bool bPropagateToChildren)
{
	this->UpdateObjectToWorldTransform();
	this->OnUpdateTransform();
	if (bPropagateToChildren)
	{
		for (ULexWidget* Child : this->Children)
		{
			if (IsValid(Child))
			{
				Child->CalculateObjectToWorldTransform(true);
			}
		}
	}
}

int32 ULexWidget::GetMaxChildrenCapacity() const
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
	for (const ULexUIBehaviour* Component : Components)
	{
		if (IsValid(Component))
		{
			ApplyLimit(Component->GetMaxWidgetChildren());
		}
	}
	return Capacity;
}

bool ULexWidget::CanAcceptAdditionalChildren(int32 AdditionalChildCount) const
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
	for (const ULexWidget* Child : Children)
	{
		CurrentChildCount += IsValid(Child) ? 1 : 0;
	}
	return CurrentChildCount <= Capacity && AdditionalChildCount <= Capacity - CurrentChildCount;
}

bool ULexWidget::CanAcceptChildren(TConstArrayView<ULexWidget*> InChildren) const
{
	TSet<const ULexWidget*> UniqueCandidates;
	for (const ULexWidget* Candidate : InChildren)
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
	TSet<const ULexWidget*> ProjectedChildren;
	for (const ULexWidget* ExistingChild : Children)
	{
		if (IsValid(ExistingChild))
		{
			ProjectedChildren.Add(ExistingChild);
		}
	}
	for (const ULexWidget* Candidate : UniqueCandidates)
	{
		ProjectedChildren.Add(Candidate);
	}
	return ProjectedChildren.Num() <= Capacity;
}

bool ULexWidget::CanAcceptChild(const ULexWidget* InChild) const
{
	ULexWidget* Candidate = const_cast<ULexWidget*>(InChild);
	return CanAcceptChildren(MakeArrayView(&Candidate, 1));
}

void ULexWidget::SetParent(ULexWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex)
{
	TrySetParent(InParent, InKeepWorldPosition, InSiblingIndex);
}

bool ULexWidget::TrySetParent(ULexWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex)
{
	return TrySetParentInternal(InParent, InKeepWorldPosition, InSiblingIndex, true);
}

bool ULexWidget::SetParentFromPrefab(ULexWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex)
{
	return TrySetParentInternal(InParent, InKeepWorldPosition, InSiblingIndex, false);
}

bool ULexWidget::TrySetParentInternal(ULexWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex, bool bEnforceCapacity)
{
	if (IsValid(InParent))//attach to parent
	{
		if (this == InParent)
		{
			ensureMsgf(false, TEXT("A LexWidget cannot be parented to itself: %s"), *GetPathName());
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
		const FTransform OldObjectToWorldTransform = this->GetWorldTransform();
		const FVector PreviousAuthoredScale = RelativeScale;
		const FVector2f PreviousLayoutScale = LayoutScale;
		bIsAttaching = true;
		if (Parent.IsValid())
		{
			TrySetParent(nullptr, false);
		}
		bIsAttaching = false;
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
			const FTransform LocalTransform = OldObjectToWorldTransform.GetRelativeTransform(InParent->GetWorldTransform());
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
		const FTransform OldObjectToWorldTransform = this->GetWorldTransform();
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

void ULexWidget::SetSiblingIndex(int32 InInt)
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

bool ULexWidget::IsChildOf(const ULexWidget* InTarget)const
{
	auto TempParent = this->Parent;
	TSet<const ULexWidget*> VisitedWidgets;
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

TArray<ULexUIBehaviour*> ULexWidget::GetComponents(TSubclassOf<ULexUIBehaviour> ComponentClass)const
{
	TArray<ULexUIBehaviour*> ResultArray;
	UClass* RequestedClass = *ComponentClass;
	if (!IsValid(RequestedClass) || !RequestedClass->IsChildOf(ULexUIBehaviour::StaticClass()))
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

ULexUIBehaviour* ULexWidget::GetComponent(TSubclassOf<ULexUIBehaviour> ComponentClass)const
{
	UClass* RequestedClass = *ComponentClass;
	if (!IsValid(RequestedClass) || !RequestedClass->IsChildOf(ULexUIBehaviour::StaticClass()))
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

ULexUIBehaviour* ULexWidget::GetComponentByInterface(UClass* InterfaceClass)const
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

DECLARE_CYCLE_STAT(TEXT("LexWidget OnUpdateTransform"), STAT_OnUpdateTransform, STATGROUP_LGUI);
void ULexWidget::OnUpdateTransform()
{
	SCOPE_CYCLE_COUNTER(STAT_OnUpdateTransform)
	// UE_LOG(LGUI, Error, TEXT("OnUpdateTransform Flag:%d %s"), (int)UpdateTransformFlags, *this->GetDisplayName());
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

void ULexWidget::OnChildAttached(ULexWidget* ChildWidget)
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
	for (ULexUIBehaviour* Component : Components)
	{
		if (IsValid(Component)) Component->OnWidgetChildAttached(ChildWidget);
	}

	MarkCanvasUpdate(false);
}

void ULexWidget::OnAttachedToParent()
{
	if (this->bIsRegistered)//registered means not during prefab process
	{
		Call_TransformChanged();
		CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
	}

	ULexCanvas* ParentCanvas = this->GetComponentInParent<ULexCanvas>();
	OnHierarchyAttachmentChanged(ParentCanvas, Parent->RootWidget.Get());

	CalculateWidgetActive_Recursive();
	CalculateVisibility_Recursive();
	CalculateRaycastable_Recursive();
	CalculateInteractable_Recursive();
	
	// MarkLayoutForRebuild(this);//why comment this? because it already called in OnHierarchyAttachmentChanged
	MarkClipDirty(true);
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
#if WITH_EDITOR
		LexUIManager->MarkLexUIWidgetOutlinerChanged();
#endif
		LexUIManager->MarkRebuildAllLayoutTree();
	}
}

void ULexWidget::OnChildDetached(ULexWidget* ChildWidget)
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
	for (ULexUIBehaviour* Component : Components)
	{
		if (IsValid(Component)) Component->OnWidgetChildDetached(ChildWidget);
	}
	MarkLayoutForRebuild(this);//child removed, so need to rebuild layout
}

void ULexWidget::OnDetachedFromParent()
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
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
#if WITH_EDITOR
		LexUIManager->MarkLexUIWidgetOutlinerChanged();
#endif
		LexUIManager->MarkRebuildAllLayoutTree();
	}
}

void ULexWidget::OnRegister()
{
	bIsRegistered = true;
	const bool bPanelSlotRegisteredByEnsure = Parent.IsValid()
		&& EnsurePanelSlotForChild(Parent.Get(), this);
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		LexUIManager->AddWidget(this);
#if WITH_EDITOR
		LexUIManager->MarkLexUIWidgetOutlinerChanged();
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
	const TArray<TObjectPtr<ULexUIBehaviour>> ComponentsToRegister = Components;
	for (ULexUIBehaviour* Component : ComponentsToRegister)
	{
		if (IsValid(Component) && Components.Contains(Component))
		{
			Component->OnRegister();
		}
	}
}
void ULexWidget::OnUnregister()
{
	bIsRegistered = false;

	// Component teardown may remove helper behaviours from this same widget.
	// Iterate a snapshot so those callbacks cannot invalidate the active iterator.
	const TArray<TObjectPtr<ULexUIBehaviour>> ComponentsToUnregister = Components;
	for (ULexUIBehaviour* Component : ComponentsToUnregister)
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
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
	{
		LexUIManager->RemoveWidget(this);
#if WITH_EDITOR
		LexUIManager->MarkLexUIWidgetOutlinerChanged();
#endif
	}
}

void ULexWidget::EnsureUIChildrenValid()
{
	for (int i = Children.Num() - 1; i >= 0; i--)
	{
		if (!IsValid(Children[i]))
		{
			Children.RemoveAt(i);
		}
	}
}

void ULexWidget::EnsureUIChildrenSorted()const
{
	if (bNeedSortUIChildren)
	{
		bNeedSortUIChildren = false;
		// StableSort: duplicate SiblingIndex values exist in legacy data and transiently during prefab
		// refresh. An unstable sort made equal-key children swap places on every RefreshAllUI (IntroSort's
		// small-array selection sort deterministically flips the tail pair each pass) — visible as widgets
		// trading positions after every editor refresh. Equal keys must keep their current order.
		Children.StableSort([](const ULexWidget& A, const ULexWidget& B)
			{
				return A.GetSiblingIndex() < B.GetSiblingIndex();
			});
	}
}


void ULexWidget::CalculateAnchorFromTransform()
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
void ULexWidget::CalculateTransformFromAnchor()
{
	bool HorizontalPositionChanged = false, VerticalPositionChanged = false;
	CalculateTransformFromAnchor(HorizontalPositionChanged, VerticalPositionChanged);
}
void ULexWidget::CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged)
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

float ULexWidget::GetWidth() const
{
	if (bCacheWidthDirty)
	{
		bCacheWidthDirty = false;
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
float ULexWidget::GetHeight() const
{
	if (bCacheHeightDirty)
	{
		bCacheHeightDirty = false;
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

void ULexWidget::SetAnchorData(const FLexUIAnchorData& Value)
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

void ULexWidget::SetPivot(FVector2D Value) 
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
			MarkLayoutForRebuild(this);
		}
	}
}

void ULexWidget::SetAnchorMin(FVector2D Value)
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
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}
void ULexWidget::SetAnchorMax(FVector2D Value)
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
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void ULexWidget::SetAnchorOffset(FMargin Value)
{
	if (this->Parent.IsValid())
	{
		bool bWidthChange = CacheAnchorOffsetLeft != Value.Left || CacheAnchorOffsetRight != Value.Right;
		bool bHeightChange = CacheAnchorOffsetBottom != Value.Bottom || CacheAnchorOffsetTop != Value.Top;
		if (bCacheAnchorOffsetLeftDirty || bCacheAnchorOffsetRightDirty || bWidthChange || bHeightChange)
		{
			bCacheAnchorOffsetLeftDirty = false;
			bCacheAnchorOffsetRightDirty = false;
			bCacheAnchorOffsetBottomDirty = false;
			bCacheAnchorOffsetTopDirty = false;
			CacheAnchorOffsetLeft = Value.Left;
			CacheAnchorOffsetRight = Value.Right;
			CacheAnchorOffsetBottom = Value.Bottom;
			CacheAnchorOffsetTop = Value.Top;
			
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
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

void ULexWidget::SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation)
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
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void ULexWidget::SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
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
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}
void ULexWidget::SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
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
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void ULexWidget::SetAnchoredPosition(FVector2D Value)
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
			MarkLayoutForRebuild(this);
		}
	}
}

void ULexWidget::SetHorizontalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.X != Value)
	{
		AnchorData.AnchoredPosition.X = Value;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this);
		}
	}
}
void ULexWidget::SetVerticalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.Y != Value)
	{
		AnchorData.AnchoredPosition.Y = Value;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this);
		}
	}
}

void ULexWidget::SetSizeDelta(FVector2D Value)
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

void ULexWidget::SetAnchoredPositionAndSizeDelta(FVector2D Position, FVector2D Size)
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

float ULexWidget::GetAnchorOffsetLeft()const
{
	if (bCacheAnchorOffsetLeftDirty)
	{
		bCacheAnchorOffsetLeftDirty = false;
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
float ULexWidget::GetAnchorOffsetTop()const
{
	if (bCacheAnchorOffsetTopDirty)
	{
		bCacheAnchorOffsetTopDirty = false;
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
float ULexWidget::GetAnchorOffsetRight()const
{
	if (bCacheAnchorOffsetRightDirty)
	{
		bCacheAnchorOffsetRightDirty = false;
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
float ULexWidget::GetAnchorOffsetBottom()const
{
	if (bCacheAnchorOffsetBottomDirty)
	{
		bCacheAnchorOffsetBottomDirty = false;
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

FMargin ULexWidget::GetAnchorOffset() const
{
	return FMargin(
		this->GetAnchorOffsetLeft(),
		this->GetAnchorOffsetTop(),
		this->GetAnchorOffsetRight(),
		this->GetAnchorOffsetBottom()
	);
}

void ULexWidget::SetAnchorOffsetLeft(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetLeft != Value || bCacheAnchorOffsetLeftDirty)
		{
			bCacheAnchorOffsetLeftDirty = false;
			CacheAnchorOffsetLeft = Value;
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
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexWidget::SetAnchorOffsetTop(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetTop != Value || bCacheAnchorOffsetTopDirty)
		{
			bCacheAnchorOffsetTopDirty = false;
			CacheAnchorOffsetTop = Value;
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
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexWidget::SetAnchorOffsetRight(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetRight != Value || bCacheAnchorOffsetRightDirty)
		{
			bCacheAnchorOffsetRightDirty = false;
			CacheAnchorOffsetRight = Value;
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
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexWidget::SetAnchorOffsetBottom(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetBottom != Value || bCacheAnchorOffsetBottomDirty)
		{
			bCacheAnchorOffsetBottomDirty = false;
			CacheAnchorOffsetBottom = Value;
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
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if LexWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

void ULexWidget::SetWidth(float Value)
{
	if (CacheWidth != Value || bCacheWidthDirty)
	{
		bCacheWidthDirty = false;
		CacheWidth = Value;
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
void ULexWidget::SetHeight(float Value)
{
	if (CacheHeight != Value || bCacheHeightDirty)
	{
		bCacheHeightDirty = false;
		CacheHeight = Value;
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

void ULexWidget::RegisterRenderCanvas(ULexCanvas* InRenderCanvas)
{
	bIsCanvasWidget = true;
	ULexCanvas* ParentCanvas = nullptr;
	if (auto ParentWidget = GetParent())
	{
		ParentCanvas = ParentWidget->GetComponentInParent<ULexCanvas>();//@todo: replace with Canvas's ParentCanvas?
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
void ULexWidget::RenewRenderCanvasRecursive(ULexCanvas* InParentRenderCanvas)
{
	auto ThisRenderCanvas = this->GetComponent<ULexCanvas>();
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

void ULexWidget::UnregisterRenderCanvas()
{
	bIsCanvasWidget = false;
	ULexCanvas* ParentCanvas = nullptr;
	if (auto ParentWidget = GetParent())
	{
		ParentCanvas = ParentWidget->GetComponentInParent<ULexCanvas>();//@todo: replace with Canvas's ParentCanvas?
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

void ULexWidget::UpdateLayout()
{
	if (IsValid(LayoutSelf))
	{
		LayoutSelf->CalculateSize();
	}
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->CalculateLayout();
	}
}

void ULexWidget::UpdateClip(ULexUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FLexUIClipData>>& ClipDataList)
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
	
	TSharedPtr<FLexUIClipData> ParentClip = nullptr;
	if (Parent.IsValid())
	{
		ParentClip = Parent->ClipData.Pin();
	}
	switch (GetClipping())
	{
	case ELexWidgetClipping::Inherit:
		this->ClipData = ParentClip;
		break;
	case ELexWidgetClipping::ClipToBounds:
		{
			if (!this->ClipData.IsValid())
			{
				auto NewClip = MakeShared<FLexUIClipData>(ParentClip, ClipDataTexture, this);
				ClipDataList.Add(NewClip);
				this->ClipData = NewClip;
			}
		}
		break;
	case ELexWidgetClipping::ClipToBoundsWithoutIntersecting:
		{
			if (!this->ClipData.IsValid())
			{
				auto NewClip = MakeShared<FLexUIClipData>(nullptr, ClipDataTexture, this);
				ClipDataList.Add(NewClip);
				this->ClipData = NewClip;
			}
		}
		break;
	case ELexWidgetClipping::Disabled:
		this->ClipData = nullptr;
		break;
	}
	if (Visual)
	{
		Visual->CheckClipDataStartPosition();
	}
}

void ULexWidget::UpdateVisual() const
{
	if (IsValid(Visual))
	{
		Visual->UpdateGeometry();
	}
}

void ULexWidget::ForceUpdateLayout()
{
	MarkWidgetLayoutDirty();
	UpdateLayout();
}

void ULexWidget::SetRenderCanvas(ULexCanvas* InNewCanvas)
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
		OldRenderCanvas->RemoveLexWidget(this);
		if (IsValid(Visual))
		{
			OldRenderCanvas->MarkVisualWillChange(Visual);
			OldRenderCanvas->UnregisterVisual(Visual);
		}
	}
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->AddLexWidget(this);
		bClipDirty = true;//mark it dirty so it will be added to new canvas
		if (IsValid(Visual))
		{
			RenderCanvas->RegisterVisual(Visual);
		}
	}
}

void ULexWidget::OnHierarchyAttachmentChanged(ULexCanvas* ParentRenderCanvas, ULexWidget* ParentRoot)
{
	auto ThisRenderCanvas = this->GetComponent<ULexCanvas>();
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

void ULexWidget::OnRenderCanvasChanged(ULexCanvas* OldCanvas, ULexCanvas* NewCanvas)
{
	if (IsValid(OldCanvas))
	{
		OldCanvas->RemoveLexWidget(this);
	}
	if (IsValid(NewCanvas))
	{
		NewCanvas->AddLexWidget(this);
	}
	if (IsValid(Visual))
	{
		Visual->OnRenderCanvasChanged(OldCanvas, NewCanvas);
	}
}

void ULexWidget::CheckRootWidget(ULexWidget* RootWidgetInParent)
{
	if (RootWidgetInParent == nullptr)
	{
		ULexWidget* TopWidget = this;
		ULexWidget* TempRootWidget = nullptr;
		while (TopWidget != nullptr)
		{
			TempRootWidget = TopWidget;
			TopWidget = TopWidget->GetParent();
		}
		RootWidgetInParent = TempRootWidget;
	}
	RootWidget = RootWidgetInParent;
}

void ULexWidget::CalculateWidgetActive_Recursive()
{
	struct LOCAL
	{
		static void CalculateWidgetActive(ULexWidget* Widget)
		{
			bool bResultActive = true;
			if (!Widget->bWidgetActive)
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
				if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(Widget->GetWorld()))
				{
					LexUIManager->MarkRebuildAllLayoutTree();
				}
				//tell layout
				MarkLayoutForRebuild(Widget);
			}
			for (auto& Child : Widget->GetChildren())
			{
				CalculateWidgetActive(Child);
			}
		}
	};
	LOCAL::CalculateWidgetActive(this);
}

void ULexWidget::CalculateVisibility_Recursive()
{
	struct FVisibilityCalculator
	{
		static void Calculate(ULexWidget* Widget)
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

			const bool bCollapsed = Widget->Visibility == ELexWidgetVisibility::Collapsed || Widget->bLayoutVisibilitySuppressed;
			const bool bPaints = Widget->Visibility != ELexWidgetVisibility::Hidden && !bCollapsed;
			const bool bBlocksChildren = Widget->Visibility == ELexWidgetVisibility::HitTestInvisible;
			const bool bSelfAcceptsHit = Widget->Visibility == ELexWidgetVisibility::Visible;

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
				if (ULexUIManagerWorldSubsystem* LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(Widget->GetWorld()))
				{
					LexUIManager->MarkRebuildAllLayoutTree();
				}
				ULexWidget::MarkLayoutForRebuild(Widget->Parent.IsValid() ? Widget->Parent.Get() : Widget);
			}
			if (bRenderChanged || bHitTestChanged)
			{
				Widget->MarkCanvasUpdate(true);
			}

			for (ULexWidget* Child : Widget->GetChildren())
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
void ULexWidget::CalculateInteractable_Recursive()
{
	struct LOCAL
	{
		static void CalculateInteractable(ULexWidget* Widget)
		{
			bool bResultInteractable = true;
			switch (Widget->Interactable)
			{
			case ELexWidgetInteractableType::Enabled:
				bResultInteractable = true;
				break;
			case ELexWidgetInteractableType::Disabled:
				bResultInteractable = false;
				break;
			case ELexWidgetInteractableType::Inherit:
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
				CalculateInteractable(Child);
			}
		}
	};
	LOCAL::CalculateInteractable(this);
}
void ULexWidget::CalculateRaycastable_Recursive()
{
	struct LOCAL
	{
		static void CalculateRaycastable(ULexWidget* Widget)
		{
			bool bResult = true;
			switch (Widget->Raycastable)
			{
			case ELexWidgetRaycastableType::Disabled:
				bResult = false;
				break;
			case ELexWidgetRaycastableType::Enabled:
				bResult = true;
				break;
			case ELexWidgetRaycastableType::Inherit:
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
				CalculateRaycastable(Child);
			}
		}
	};
	LOCAL::CalculateRaycastable(this);
}

ULexWidget* ULexWidget::GetChildByIndex(int index)const
{
	if (index < 0 || index >= Children.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Index:%d out of range[%d, %d]"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, index, 0, Children.Num() - 1);
		return nullptr;
	}
	EnsureUIChildrenSorted();
	return Children[index];
}

ULexCanvas* ULexWidget::GetRootCanvas()const
{
	if (RenderCanvas.IsValid())
	{
		return RenderCanvas->GetRootCanvas();
	}
	return nullptr;
}

USceneComponent* ULexWidget::GetAttachedRootSceneComponent() const
{
	if (auto RootCanvas = GetRootCanvas())
	{
		return RootCanvas->GetAttachedRootSceneComponent();
	}
	return nullptr;
}

FVector2D ULexWidget::GetLocalSpaceLeftBottomPoint()const
{
	FVector2D leftBottomPoint;
	leftBottomPoint.X = GetWidth() * -AnchorData.Pivot.X;
	leftBottomPoint.Y = GetHeight() * -AnchorData.Pivot.Y;
	return leftBottomPoint;
}
FVector2D ULexWidget::GetLocalSpaceRightTopPoint()const
{
	FVector2D rightTopPoint;
	rightTopPoint.X = GetWidth() * (1.0f - AnchorData.Pivot.X);
	rightTopPoint.Y = GetHeight() * (1.0f - AnchorData.Pivot.Y);
	return rightTopPoint;
}
FVector2D ULexWidget::GetLocalSpaceCenter()const
{
	return FVector2D(this->GetWidth() * (0.5f - AnchorData.Pivot.X), this->GetHeight() * (0.5f - AnchorData.Pivot.Y));
}

float ULexWidget::GetLocalSpaceLeft()const
{
	return this->GetWidth() * -AnchorData.Pivot.X;
}
float ULexWidget::GetLocalSpaceRight()const
{
	return this->GetWidth() * (1.0f - AnchorData.Pivot.X);
}
float ULexWidget::GetLocalSpaceBottom()const
{
	return this->GetHeight() * -AnchorData.Pivot.Y;
}
float ULexWidget::GetLocalSpaceTop()const
{
	return this->GetHeight() * (1.0f - AnchorData.Pivot.Y);
}

void ULexWidget::MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	// No clip invalidation here: clip rectangles are recomputed and diffed every tick from the owner's world
	// transform (see FLexUIClipData::UpdateData), so there is nothing to mark.
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

void ULexWidget::MarkTransformChanged()
{
	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(true);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			//This is mainly to mark LGUICanvas's bIsViewProjectionMatrixDirty to true.
			//For the condition LGUI_Tutorials/Tutorials/UIRenderTarget, when move LGUIRenderTarget at runtime, the LGUICanvas's RenderTarget's matrix not update, result in wrong interaction.
			this->RenderCanvas->MarkTransformOrDimensionChanged();
		}
	}

	Call_TransformChanged();
}

void ULexWidget::MarkAnchorDataChanged_Recursive(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache, bool InPropagateToChildren)
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

void ULexWidget::MarkCanvasUpdate(bool bRebuildDrawCall)const
{
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->MarkCanvasUpdate(bRebuildDrawCall);
	}
}

void ULexWidget::SetPositionAndSizeForLayoutAnimation(FVector2D Position, FVector2D Size)
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

void ULexWidget::SetPositionForLayoutAnimation(FVector2D Position)
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

void ULexWidget::SetSizeForLayoutAnimation(FVector2D Size)
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

void ULexWidget::MarkAnchorDataChangedByLayoutContainer_Recursive(bool InPivotChanged, bool InWidthChanged,
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

float ULexWidget::GetLayoutProperty(TFunctionRef<float(ULexLayoutSelf*)> GetLayoutSelfProperty,
                                    TFunctionRef<float(ULexLayoutContainer*)> GetLayoutContainerProperty,
                                    TFunctionRef<float(ULexVisual*)> GetVisualProperty,
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
UObject* ULexWidget::GetLayoutSource(TFunctionRef<float(ULexLayoutSelf*)> GetLayoutSelfProperty,
	TFunctionRef<float(ULexLayoutContainer*)> GetLayoutContainerProperty,
	TFunctionRef<float(ULexVisual*)> GetVisualProperty) const
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

ULexCanvas* ULexWidget::GetRenderCanvas()const
{
	return RenderCanvas.Get();
}

bool ULexWidget::IsScreenSpaceOverlayUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToScreenSpace();
}
bool ULexWidget::IsRenderTargetUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToRenderTarget();
}
bool ULexWidget::IsWorldSpaceUI()const
{
	if (!RenderCanvas.IsValid())return false;
	return RenderCanvas->IsRenderToWorldSpace();
}

void ULexWidget::MarkLayoutForRebuild(ULexWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return;
	}

	ULexWidget* TargetWidget = InWidget;
	ULexWidget* RebuildRoot = nullptr;
	TSet<const ULexWidget*> VisitedWidgets;
	// Desired-size dependencies may cross plain wrapper widgets, so dirty every layout on the ancestor chain.
	while (IsValid(TargetWidget) && !VisitedWidgets.Contains(TargetWidget))
	{
		VisitedWidgets.Add(TargetWidget);
		if (ULexLayoutContainer* LayoutContainer = TargetWidget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			LayoutContainer->MarkLayoutDirty();
			RebuildRoot = TargetWidget;
		}
		if (ULexLayoutSelf* LayoutSelf = TargetWidget->GetLayoutSelf(); IsValid(LayoutSelf))
		{
			LayoutSelf->MarkLayoutDirty();
			RebuildRoot = TargetWidget;
		}
		if (TargetWidget->GetIgnoreLayout())
		{
			break;
		}
		
		TargetWidget = TargetWidget->GetParent();
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

void ULexWidget::RebuildLayoutImmediately(ULexWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(InWidget->GetWorld()))
	{
		LexUIManager->RebuildLayoutImmediately(InWidget);
	}
}

void ULexWidget::MarkWidgetLayoutDirty()
{
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		LexUIManager->AddLayoutDirtyWidget(this);
	}
}

void ULexWidget::MarkClipDirty(bool InClipTypeChanged) const
{
	bClipDirty = true;
	if (InClipTypeChanged)bNeedRecreateClip = true;
	// Waking the canvas is required, not optional. ULexCanvas::UpdateCanvasDrawCall gates everything that
	// reconciles clipping behind bCanTickUpdate: UpdateClip (creates/destroys the FLexUIClipData),
	// ULexVisual::CheckClipDataStartPosition (refreshes the slot index baked into vertex data) and
	// MarkFinishUpdateCanvasDrawCall (uploads the clip blocks). Marking bClipDirty without waking the canvas
	// leaves a widget sitting on Clipping == ClipToBounds with no ClipData, while its visual still points at a
	// recycled clip slot — the shader then clips against a stale rectangle and the subtree renders nothing.
	MarkCanvasUpdate(true);
	struct LOCAL
	{
		static void MarkDirty(const ULexWidget* Widget, bool InClipTypeChanged, TSet<const ULexWidget*>& VisitedWidgets)
		{
			if (!IsValid(Widget) || VisitedWidgets.Contains(Widget))
			{
				return;
			}
			VisitedWidgets.Add(Widget);
			switch (Widget->GetClipping())
			{
			case ELexWidgetClipping::Inherit:
			case ELexWidgetClipping::ClipToBounds:
				Widget->bClipDirty = true;
				if (InClipTypeChanged)Widget->bNeedRecreateClip = true;
				// Descendants may render through a different (child) canvas, so wake each one individually.
				Widget->MarkCanvasUpdate(true);
				break;
			case ELexWidgetClipping::ClipToBoundsWithoutIntersecting:
			case ELexWidgetClipping::Disabled:
				return;
			}

			for (const ULexWidget* Child : Widget->GetChildren())
			{
				MarkDirty(Child, InClipTypeChanged, VisitedWidgets);
			}
		}
	};
	TSet<const ULexWidget*> VisitedWidgets;
	VisitedWidgets.Add(this);
	for (const ULexWidget* Child : this->GetChildren())
	{
		LOCAL::MarkDirty(Child, InClipTypeChanged, VisitedWidgets);
	}
}
bool ULexWidget::IsPointVisibleOnClip(const FVector& Value) const
{
	if (ClipData.IsValid())
	{
		return ClipData.Pin()->IsPointVisible(Value);
	}
	return true;
}
void ULexWidget::SetClipping(ELexWidgetClipping Value)
{
	if (Clipping != Value)
	{
		const ELexWidgetClipping PreviousEffectiveClipping = GetClipping();
		Clipping = Value;
		if (PreviousEffectiveClipping != GetClipping())
		{
			MarkClipDirty(true);
		}
	}
}

void ULexWidget::SetLayoutClippingOverride(ELexWidgetClipping Value)
{
	const ELexWidgetClipping PreviousEffectiveClipping = GetClipping();
	bHasLayoutClippingOverride = true;
	LayoutClippingOverride = Value;
	if (PreviousEffectiveClipping != GetClipping())
	{
		MarkClipDirty(true);
	}
}

void ULexWidget::ClearLayoutClippingOverride()
{
	if (!bHasLayoutClippingOverride)
	{
		return;
	}
	const ELexWidgetClipping PreviousEffectiveClipping = GetClipping();
	bHasLayoutClippingOverride = false;
	LayoutClippingOverride = ELexWidgetClipping::Inherit;
	if (PreviousEffectiveClipping != GetClipping())
	{
		MarkClipDirty(true);
	}
}
void ULexWidget::SetClippingCornerRadius(FVector4f Value)
{
	if (ClippingCornerRadius != Value)
	{
		ClippingCornerRadius = Value;
		MarkClipDirty(false);
	}
}

void ULexWidget::SetClippingMargin(FMargin Value)
{
	if (ClippingMargin != Value)
	{
		ClippingMargin = Value;
		MarkClipDirty(false);
	}
}

float ULexWidget::GetFinalRenderOpacity()const
{
	if (Parent.IsValid())
	{
		return this->RenderOpacity * Parent->GetFinalRenderOpacity();
	}
	return this->RenderOpacity;
}
void ULexWidget::SetRenderOpacity(float Value)
{
	Value = FMath::Clamp(Value, 0.0f, 1.0f);
	if (RenderOpacity != Value)
	{
		RenderOpacity = Value;
		struct LOCAL
		{
			static void MarkDirty(const ULexWidget* Widget)
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

bool ULexWidget::GetPixelSnappingInHierarchy() const
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

void ULexWidget::SetPixelSnapping(EWidgetPixelSnapping Value)
{
	if (PixelSnapping != Value)
	{
		PixelSnapping = Value;
		struct LOCAL
		{
			static void MarkChanged(const ULexWidget* Widget)
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

bool ULexWidget::GetWidgetActiveInHierarchy() const
{
	return bCacheWidgetActiveInHierarchy;
}

#if WITH_EDITOR
void ULexWidget::SetHiddenInDesigner(bool bHidden)
{
	if (bHiddenInDesigner != bHidden)
	{
		bHiddenInDesigner = bHidden;
		CalculateVisibility_Recursive();
	}
}
#endif

void ULexWidget::SetWidgetActive(bool Value)
{
	if (bWidgetActive != Value)
	{
		bWidgetActive = Value;
		CalculateWidgetActive_Recursive();
		CalculateVisibility_Recursive();
	}
}

void ULexWidget::SetVisibility(ELexWidgetVisibility Value)
{
	if (Visibility != Value)
	{
		Visibility = Value;
		CalculateVisibility_Recursive();
		OnVisibilityChanged.Broadcast(Visibility);
	}
}

void ULexWidget::SetLayoutVisibilitySuppressed(bool bSuppressed)
{
	if (bLayoutVisibilitySuppressed != bSuppressed)
	{
		bLayoutVisibilitySuppressed = bSuppressed;
		CalculateVisibility_Recursive();
	}
}

bool ULexWidget::SetFocus(int32 UserIndex, int32 PointerId)
{
	if (!bIsFocusable || !GetRenderVisibleInHierarchy() || !GetInteractableInHierarchy())
	{
		return false;
	}
	if (ULexEventSystem* EventSystem = ULexEventSystem::GetLexEventSystemInstance(this, UserIndex))
	{
		ULexBaseEventData* EventData = EventSystem->GetPointerEventData(PointerId, true);
		EventSystem->SetSelectWidget(this, EventData);
		return true;
	}
	return false;
}

bool ULexWidget::HasFocus(int32 UserIndex, int32 PointerId) const
{
	if (ULexEventSystem* EventSystem = ULexEventSystem::GetLexEventSystemInstance(const_cast<ULexWidget*>(this), UserIndex))
	{
		return EventSystem->GetCurrentSelectedComponent(PointerId) == this;
	}
	return false;
}

void ULexWidget::ClearFocus(int32 UserIndex, int32 PointerId)
{
	if (ULexEventSystem* EventSystem = ULexEventSystem::GetLexEventSystemInstance(this, UserIndex))
	{
		ULexBaseEventData* EventData = EventSystem->GetPointerEventData(PointerId, false);
		if (EventData && EventData->SelectedComponent == this)
		{
			EventSystem->SetSelectWidget(nullptr, EventData);
		}
	}
}

void ULexWidget::NotifyFocusReceived(int32 UserIndex, int32 PointerId)
{
	OnFocusReceived.Broadcast(UserIndex, PointerId);
	if (AccessibleBehavior != ELexAccessibleBehavior::NotAccessible)
	{
		AnnounceAccessibleText();
	}
}

void ULexWidget::NotifyFocusLost(int32 UserIndex, int32 PointerId)
{
	OnFocusLost.Broadcast(UserIndex, PointerId);
}

void ULexWidget::AnnounceAccessibleText(const FText& Announcement)
{
#if WITH_ACCESSIBILITY
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FText TextToAnnounce = Announcement;
	if (TextToAnnounce.IsEmpty())
	{
		TextToAnnounce = AccessibleBehavior == ELexAccessibleBehavior::Summary ? AccessibleSummaryText : AccessibleText;
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

void ULexWidget::SetRaycastable(ELexWidgetRaycastableType Value)
{
	if (Raycastable != Value)
	{
		Raycastable = Value;
		CalculateRaycastable_Recursive();
	}
}

void ULexWidget::SetInteractable(ELexWidgetInteractableType Value)
{
	if (Interactable != Value)
	{
		Interactable = Value;
		CalculateInteractable_Recursive();
	}
}

void ULexWidget::SetIgnoreLayout(bool Value)
{
	if (bIgnoreLayout != Value)
	{
		bIgnoreLayout = Value;
		MarkLayoutForRebuild(this);
		if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			LexUIManager->MarkRebuildAllLayoutTree();
		}
	}
}

const ULexWidget* ULexWidget::GetRestrictNavigationAreaWidget() const
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

void ULexWidget::SetRestrictNavigationArea(bool Value)
{
	bRestrictNavigationArea = Value;
}

ULexVisual* ULexWidget::CreateNewVisual(TSubclassOf<ULexVisual> VisualClass)
{
	auto OldVisual = Visual;
	auto NewVisual = NewObject<ULexVisual>(this, VisualClass, NAME_None, RF_Public | RF_Transactional);
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

void ULexWidget::RemoveVisual()
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

ULexLayoutContainer* ULexWidget::CreateNewLayoutContainer(TSubclassOf<ULexLayoutContainer> LayoutClass)
{
	UClass* RequestedClass = *LayoutClass;
	if (!IsValid(RequestedClass)
		|| !RequestedClass->IsChildOf(ULexLayoutContainer::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	const ULexLayoutContainer* RequestedLayoutDefault = Cast<ULexLayoutContainer>(RequestedClass->GetDefaultObject());
	if (IsValid(RequestedLayoutDefault))
	{
		const int32 MaxChildren = RequestedLayoutDefault->GetMaxChildren();
		if (MaxChildren >= 0)
		{
			int32 ValidChildCount = 0;
			for (const ULexWidget* Child : Children)
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
	auto NewLayout = NewObject<ULexLayoutContainer>(this, RequestedClass, NAME_None, RF_Public | RF_Transactional);
	if (!IsValid(NewLayout))
	{
		return nullptr;
	}
	const bool bInitializeScaleBoxSlots = NewLayout->IsA<ULexLayoutContainerScaleBox>()
		&& (!IsValid(OldLayout) || !OldLayout->IsA<ULexLayoutContainerScaleBox>());
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
	if (IsValid(Cast<ULexPanelLayoutBase>(NewLayout)))
	{
		for (ULexWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				// UMG creates a fresh ScaleBoxSlot with Center/Center defaults when the panel type changes.
				// Lex reuses its generic slot, so initialize those defaults explicitly on the same transition.
				if (bInitializeScaleBoxSlots)
				{
					if (ULexPanelSlot* ExistingSlot = Child->GetPanelSlot(); IsValid(ExistingSlot))
					{
#if WITH_EDITOR
						if (const UWorld* World = Child->GetWorld(); !World || !World->IsGameWorld())
						{
							ExistingSlot->Modify();
						}
#endif
						ExistingSlot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Center);
						ExistingSlot->SetVerticalAlignment(ELexPanelVerticalAlignment::Center);
					}
				}
				EnsurePanelSlotForChild(this, Child, true);
			}
		}
	}
	else
	{
		for (ULexWidget* Child : Children)
		{
			RemovePanelSlotFromChild(Child);
		}
	}
	MarkLayoutForRebuild(this);
	MarkDimensionChanged(false, true, true);//change LayoutContainer could cause LayoutSelf size change
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		LexUIManager->MarkRebuildAllLayoutTree();
	}
	return NewLayout;
}

void ULexWidget::RemoveLayoutContainer()
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
	for (ULexWidget* Child : Children)
	{
		RemovePanelSlotFromChild(Child);
	}
	MarkLayoutForRebuild(this);
	MarkDimensionChanged(false, true, true);
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		LexUIManager->MarkRebuildAllLayoutTree();
	}
}

ULexLayoutSelf* ULexWidget::CreateNewLayoutSelf(TSubclassOf<ULexLayoutSelf> LayoutClass)
{
	UClass* RequestedClass = *LayoutClass;
	if (!IsValid(RequestedClass)
		|| !RequestedClass->IsChildOf(ULexLayoutSelf::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	auto OldLayout = LayoutSelf;
	auto NewLayout = NewObject<ULexLayoutSelf>(this, RequestedClass, NAME_None, RF_Public | RF_Transactional);
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
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		LexUIManager->MarkRebuildAllLayoutTree();
	}
	return NewLayout;
}

void ULexWidget::RemoveLayoutSelf()
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
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		LexUIManager->MarkRebuildAllLayoutTree();
	}
}

ULexPanelSlot* ULexWidget::CreateNewPanelSlot(TSubclassOf<ULexPanelSlot> SlotClass)
{
	UClass* RequestedClass = *SlotClass;
	if (!IsValid(RequestedClass))
	{
		RequestedClass = ULexPanelSlot::StaticClass();
	}
	if (!RequestedClass->IsChildOf(ULexPanelSlot::StaticClass())
		|| RequestedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	ULexPanelSlot* OldSlot = PanelSlot;
	ULexPanelSlot* NewSlot = NewObject<ULexPanelSlot>(this, RequestedClass, NAME_None, RF_Public | RF_Transactional);
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

void ULexWidget::RemovePanelSlot()
{
	ULexPanelSlot* OldSlot = PanelSlot;
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
ULTweener* ULexWidget::LocalPositionXTo(double endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation().X;
	}), FLTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.X = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
ULTweener* ULexWidget::LocalPositionYTo(double endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation().Y;
	}), FLTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.Y = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
ULTweener* ULexWidget::LocalPositionZTo(double endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenDoubleGetterFunction::CreateWeakLambda(this, [this] 
	{
		return this->GetRelativeLocation().Z;
	}), FLTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.Z = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}



ULTweener* ULexWidget::WorldPositionXTo(double endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenDoubleGetterFunction::CreateWeakLambda(this, [this] 
	{
		return this->GetWorldLocation().X;
	}), FLTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.X = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
ULTweener* ULexWidget::WorldPositionYTo(double endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldLocation().Y;
	}), FLTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.Y = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
ULTweener* ULexWidget::WorldPositionZTo(double endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldLocation().Z;
	}), FLTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.Z = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
#pragma endregion PositionXYZ




#pragma region Position
ULTweener* ULexWidget::LocalPositionTo(FVector endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
	, FLTweenVectorGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation();
	})
	, FLTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		this->SetRelativeLocation(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
ULTweener* ULexWidget::WorldPositionTo(FVector endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
	, FLTweenVectorGetterFunction::CreateUObject(this, &ULexWidget::GetWorldLocation)
	, FLTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		return this->SetWorldLocation(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
#pragma endregion Position



ULTweener* ULexWidget::LocalScaleTo(FVector endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
	, FLTweenVectorGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeScale();
	})
	, FLTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		this->SetRelativeScale(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

ULTweener* ULexWidget::LocalUniformScaleTo(float endValue, float duration, float delay,	ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
	, FLTweenFloatGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeScale().X;
	})
	, FLTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value)
	{
		this->SetRelativeScale(FVector(value));
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}


#pragma region Rotation
ULTweener* ULexWidget::LocalRotationQuaternionTo(const FQuat& endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
	, FLTweenQuaternionGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeRotation();
	}), FLTweenQuaternionSetterFunction::CreateUObject(this, &ULexWidget::SetRelativeRotation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
ULTweener* ULexWidget::LocalRotatorTo(FRotator endValue, bool shortestPath, float duration, float delay, ELTweenEase ease)
{
	if (shortestPath)
	{
		return LocalRotationQuaternionTo(endValue.Quaternion(), duration, delay, ease);
	}
	else
	{
		auto Tweener = ULTweenManager::To(this
		, FLTweenRotatorGetterFunction::CreateWeakLambda(this, [this]
		{
			return this->GetRelativeRotation().Rotator();
		})
		, FLTweenRotatorSetterFunction::CreateWeakLambda(this, [this] (FRotator value)
		{
			this->SetRelativeRotation(value.Quaternion());
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
			ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
		}
		return Tweener;
	}
}



ULTweener* ULexWidget::WorldRotationQuaternionTo(const FQuat& endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenQuaternionGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldRotation();
	}), FLTweenQuaternionSetterFunction::CreateUObject(this, &ULexWidget::SetWorldRotation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
ULTweener* ULexWidget::WorldRotatorTo(FRotator endValue, bool shortestPath, float duration, float delay, ELTweenEase ease)
{
	if (shortestPath)
	{
		return WorldRotationQuaternionTo(endValue.Quaternion(), duration, delay, ease);
	}
	else
	{
		auto Tweener = ULTweenManager::To(this
		, FLTweenRotatorGetterFunction::CreateWeakLambda(this, [this]
		{
			return this->GetWorldRotation().Rotator();
		})
		, FLTweenRotatorSetterFunction::CreateWeakLambda(this, [this](FRotator value)
		{
			this->SetWorldRotation(value.Quaternion());
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
			ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
		}
		return Tweener;
	}
}

#pragma endregion Rotation


ULTweener* ULexWidget::RenderOpacityTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
		, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetRenderOpacity)
		, FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetRenderOpacity)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

ULTweener* ULexWidget::SizeDeltaTo(const FVector2D& endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
		, FLTweenVector2DGetterFunction::CreateUObject(this, &ULexWidget::GetSizeDelta)
		, FLTweenVector2DSetterFunction::CreateUObject(this, &ULexWidget::SetSizeDelta)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

ULTweener* ULexWidget::WidthTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
		, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetWidth)
		, FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetWidth)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

ULTweener* ULexWidget::HeightTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
		, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetHeight)
		, FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetHeight)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

ULTweener* ULexWidget::AnchoredPositionTo(const FVector2D& endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
		, FLTweenVector2DGetterFunction::CreateUObject(this, &ULexWidget::GetAnchoredPosition)
		, FLTweenVector2DSetterFunction::CreateUObject(this, &ULexWidget::SetAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

ULTweener* ULexWidget::HorizontalAnchoredPositionTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
		, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetHorizontalAnchoredPosition)
		, FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetHorizontalAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

ULTweener* ULexWidget::VerticalAnchoredPositionTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this
		, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetVerticalAnchoredPosition)
		, FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetVerticalAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

void ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(ULexWidget* Widget, ULTweener* Tweener)
{
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (Widget->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
}
#pragma endregion

