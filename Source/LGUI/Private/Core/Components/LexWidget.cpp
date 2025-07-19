// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexWidget.h"
#include "LGUI.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LGUISettings.h"
#include "Core/LGUILifeCycleUIBehaviour.h"
#include "Core/LGUIManager.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "Core/Components/LexCanvasScaler.h"
#include "LTweenManager.h"
#include "Core/LexUIClipData.h"
#include "Core/Components/LexLayout.h"
#include "Core/Components/LexVisual.h"
#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#include "EditorViewportClient.h"
#include "UObject/UnrealType.h"
#endif

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

ULexWidget::ULexWidget(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetMobility(EComponentMobility::Movable);
	SetUsingAbsoluteLocation(false);
	SetUsingAbsoluteRotation(false);
	SetUsingAbsoluteScale(false);
	SetVisibility(false);

	bWantsOnUpdateTransform = true;
	bFlattenHierarchyIndexDirty = true;
	bNeedSortUIChildren = true;
	bIsDetaching = false;
	bIsCanvasWidget = false;
}

void ULexWidget::BeginPlay()
{
	Super::BeginPlay();
	if (!ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld())->IsPrefabSystemProcessingActor(this->GetOwner()))
	{
		Awake_Implementation();
	}
}

void ULexWidget::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	if (IsValid(Layout))
	{
		Layout->EndPlay();
	}
	if (IsValid(Visual))
	{
		Visual->EndPlay();
	}
}

void ULexWidget::Awake_Implementation()
{
	CalculateVisibility_Recursive();
	CalculateHitTest_Recursive();
	CalculateIsEnabled_Recursive();
	if (IsValid(Layout))
	{
		Layout->BeginPlay();
	}
	if (IsValid(Visual))
	{
		Visual->BeginPlay();
	}
}

void ULexWidget::EditorAwake_Implementation()
{
	
}

#pragma region CallbackEvents
void ULexWidget::Call_IsEnabledChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnIsEnabledChangedEvent.Broadcast(this->GetFinalIsEnabled());
}
void ULexWidget::Call_TransformChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnTransformChangedEvent.Broadcast();
}

void ULexWidget::Call_DimensionsChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);

	if (UIParent.IsValid())
	{
		UIParent->Call_ChildDimensionsChanged(this, InPivotChanged, InWidthChanged, InHeightChanged);
	}
}

void ULexWidget::Call_ChildDimensionsChanged(ULexWidget* Child, bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnChildDimensionChangedEvent.Broadcast(Child, InPivotChanged, InWidthChanged, InHeightChanged);
}

void ULexWidget::Call_AttachmentChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnAttachmentChangedEvent.Broadcast();
}

void ULexWidget::Call_SiblingIndexChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnSiblingIndexChangedEvent.Broadcast();
}
void ULexWidget::Call_RenderVisibilityChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnRenderVisibilityChangedEvent.Broadcast();
}

void ULexWidget::Call_LayoutVisibilityChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnLayoutVisibilityChangedEvent.Broadcast();
}

void ULexWidget::Call_HitTestVisibilityChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnHitTestVisibilityChangedEvent.Broadcast();
}
#pragma endregion


void ULexWidget::CalculateFlattenHierarchyIndex_Recursive(int& index)const
{
	if (this->FlattenHierarchyIndex != index)
	{
		this->FlattenHierarchyIndex = index;
	}
	EnsureUIChildrenSorted();
	for (auto& child : UIChildren)
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
	if (RenderCanvas.IsValid()
		&& RenderCanvas->IsRegistered()//@todo: why need to check IsRegistered? the only way to set RenderCanvas is SetRenderCanvas function, but when I debug on SetRenderCanvas it's not called at all, so RenderCanvas should not valid here, no clue yet
		)
	{
		RenderCanvas->MarkCanvasUpdate(false, false, true);
		//if this LexWidget have a LGUICanvas, then we need to tell the upper canvas that hierarchy order change, in order to sort render order between canvas
		if (this->bIsCanvasWidget)
		{
			if (RenderCanvas->GetParentCanvas().IsValid())
			{
				RenderCanvas->GetParentCanvas()->MarkCanvasUpdate(false, false, true);
			}
		}
	}
}

void ULexWidget::SetSiblingIndex(int32 InInt) 
{ 
	if (InInt != SiblingIndex)
	{
		SiblingIndex = InInt;
		this->Call_SiblingIndexChanged();
		ApplySiblingIndex();
	}
}

void ULexWidget::ApplySiblingIndex()
{
	if (UIParent.IsValid())
	{
		if (UIParent->UIChildren.Num() == 0)
		{
			UIParent->UIChildren.Add(this);
			if (SiblingIndex != 0)
			{
				this->SiblingIndex = 0;
				this->Call_SiblingIndexChanged();
			}
		}
		else
		{
			UIParent->EnsureUIChildrenValid();
			UIParent->EnsureUIChildrenSorted();
			SiblingIndex = FMath::Clamp(SiblingIndex, 0, UIParent->UIChildren.Num() - 1);
			UIParent->UIChildren.Remove(this);
			UIParent->UIChildren.Insert(this, SiblingIndex);
			bool anythingChange = false;
			for (int i = 0; i < UIParent->UIChildren.Num(); i++)
			{
				if (UIParent->UIChildren[i]->SiblingIndex != i)
				{
					UIParent->UIChildren[i]->SiblingIndex = i;
					UIParent->UIChildren[i]->Call_SiblingIndexChanged();
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
	if (UIParent.IsValid())
	{
		SetSiblingIndex(UIParent->UIChildren.Num() - 1);
	}
}

ULexWidget* ULexWidget::FindChildByDisplayName(const FString& InName, bool IncludeChildren)const
{
	int indexOfFirstSlash;
	if (InName.FindChar('/', indexOfFirstSlash))
	{
		auto firstLayerName = InName.Left(indexOfFirstSlash);
		for (auto& childItem : UIChildren)
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
			for (auto& childItem : UIChildren)
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
	for (auto& childItem : UIChildren)
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
			for (auto& childItem : UIChildren)
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
	for (auto& childItem : UIChildren)
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
	
	for (auto& uiChild : UIChildren)
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
		for (auto& uiChild : UIChildren)
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
}

#if WITH_EDITOR
void ULexWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.Property != nullptr)
	{
		MarkAllDirtyRecursive();
		auto MemberName = PropertyChangedEvent.GetMemberPropertyName();
		auto PropertyName = PropertyChangedEvent.GetPropertyName();

		static const FName AspectRatioName = GET_MEMBER_NAME_CHECKED(ULexWidget, AspectRatio);
		static const FName PivotName = GET_MEMBER_NAME_CHECKED(ULexWidget, Pivot);
		static const FName WidthName = GET_MEMBER_NAME_CHECKED(ULexWidget, Width);
		static const FName HeightName = GET_MEMBER_NAME_CHECKED(ULexWidget, Height);
		static const FName PaddingName = GET_MEMBER_NAME_CHECKED(ULexWidget, Padding);
		static const FName MarginName = GET_MEMBER_NAME_CHECKED(ULexWidget, Margin);
		static const FName VisibilityName = GET_MEMBER_NAME_CHECKED(ULexWidget, WidgetVisibility);
		static const FName HitTestTypeName = GET_MEMBER_NAME_CHECKED(ULexWidget, HitTestType);
		static const FName ClippingName = GET_MEMBER_NAME_CHECKED(ULexWidget, Clipping);
		static const FName VisualName = GET_MEMBER_NAME_CHECKED(ULexWidget, Visual);
		static const FName LayoutName = GET_MEMBER_NAME_CHECKED(ULexWidget, Layout);
		static const FName IsEnabledName = GET_MEMBER_NAME_CHECKED(ULexWidget, bIsEnabled);

		if (MemberName == AspectRatioName
		|| MemberName == PivotName
		|| MemberName == WidthName
		|| MemberName == HeightName
		|| MemberName == PaddingName
		|| MemberName == MarginName
		|| MemberName == VisibilityName
		)
		{
			this->MarkRenderSizeChanged();
			this->MarkClipDirty(false);
			ULGUIPrefabManagerObject::AddOneShotTickFunction([this]()
			{
				this->MarkRenderSizeChanged();
				this->MarkClipDirty(false);
				EditorForceUpdate();
			}, 1);
		}
		else if (PropertyName == ClippingName)
		{
			MarkClipDirty(true);
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexWidget, SiblingIndex))
		{
			this->Call_SiblingIndexChanged();
			ApplySiblingIndex();
		}
		else if (PropertyName == FName(TEXT("RelativeLocation")))
		{
			UpdateComponentToWorld();
		}
		else if (PropertyName == VisualName)
		{
			if (RenderCanvas.IsValid() && IsValid(Visual))
			{
				RenderCanvas->RegisterVisual(this);
			}
			if (GetWorld()->IsGameWorld())
			{
				if (this->HasBegunPlay())
				{
					Visual->BeginPlay();
				}
				Visual->OnRegister();
			}
		}
		else if (PropertyName == LayoutName)
		{
			if (!IsValid(Layout))
			{
				for (auto Child : GetUIChildren())
				{
					Child->GetLayoutSlot();//use this to refresh layout-slot
				}
			}
			if (GetWorld()->IsGameWorld())
			{
				if (this->HasBegunPlay())
				{
					Layout->BeginPlay();
				}
				Layout->OnRegister();
			}
			MarkRenderSizeChanged();
		}
		if (PropertyName == VisibilityName)
		{
			CalculateVisibility_Recursive();
		}
		if (PropertyName == HitTestTypeName)
		{
			CalculateHitTest_Recursive();
		}
		if (PropertyName == IsEnabledName)
		{
			CalculateIsEnabled_Recursive();
		}
		ULGUIPrefabManagerObject::AddOneShotTickFunction([this]()
		{
			EditorForceUpdate();
			UpdateBounds();
		}, 1);
	}
}

void ULexWidget::PreEditChange(FProperty* PropertyAboutToChange)
{
	Super::PreEditChange(PropertyAboutToChange);

	const FName MemberName = PropertyAboutToChange->GetFName();
	if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, Visual))
	{
		if (RenderCanvas.IsValid() && IsValid(Visual))
		{
			RenderCanvas->MarkVisualWillChange(Visual);
			RenderCanvas->UnregisterVisual(this);
		}
		if (GetWorld()->IsGameWorld())
		{
			if (this->HasBegunPlay())
			{
				Visual->EndPlay();
			}
			Visual->OnUnregister();
		}
	}
	else if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, Layout))
	{
		if (GetWorld()->IsGameWorld())
		{
			if (this->HasBegunPlay())
			{
				Layout->EndPlay();
			}
			Layout->OnUnregister();
		}
	}
}

bool ULexWidget::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
	static auto WidthName = GET_MEMBER_NAME_CHECKED(ULexWidget, Width);
	static auto HeightName = GET_MEMBER_NAME_CHECKED(ULexWidget, Height);
	if (InProperty->GetFName() == TEXT("bAbsoluteLocation"))
	{
		bIsEditable = false;
	}
	else if (InProperty->GetFName() == TEXT("bAbsoluteRotation"))
	{
		bIsEditable = false;
	}
	else if (InProperty->GetFName() == TEXT("bAbsoluteScale"))
	{
		bIsEditable = false;
	}
	else if (InProperty->GetFName() == WidthName)
	{
		if (IsValid(LayoutSlot))
		{
			if (LayoutSlot->GetLayoutControlWidth())
			{
				bIsEditable = false;
			}
		}
	}
	else if (InProperty->GetFName() == HeightName)
	{
		if (IsValid(LayoutSlot))
		{
			if (LayoutSlot->GetLayoutControlHeight())
			{
				bIsEditable = false;
			}
		}
	}
	return bIsEditable;
}

bool ULexWidget::CanEditChange(const FEditPropertyChain& PropertyChain) const
{
	bool bIsEditable = UObject::CanEditChange( PropertyChain );
	return bIsEditable;
}

void ULexWidget::PostEditComponentMove(bool bFinished)
{
	Super::PostEditComponentMove(bFinished);
	EditorForceUpdate();
}

void ULexWidget::PostEditUndo()
{
	Super::PostEditUndo();
	ULGUIManagerWorldSubsystem::RefreshAllUI(this->GetWorld());
}

void ULexWidget::PostTransacted(const FTransactionObjectEvent& TransactionEvent)
{
	Super::PostTransacted(TransactionEvent);
}

FBoxSphereBounds ULexWidget::CalcBounds(const FTransform& LocalToWorld) const
{
	auto Center = this->GetLocalSpaceCenter();
	auto Origin = FVector(0, Center.X, Center.Y);
	return FBoxSphereBounds(Origin, FVector(1, this->GetRenderWidth() * 0.5f, this->GetRenderHeight() * 0.5f), (this->GetRenderWidth() > this->GetRenderHeight() ? this->GetRenderWidth() : this->GetRenderHeight()) * 0.5f).TransformBy(LocalToWorld);
}
void ULexWidget::EditorForceUpdate()
{
	MarkCanvasUpdate(true, true, true, true);
}
void ULexWidget::EnsureDataForRebuild()
{
	check(this == RootWidget);
	struct LOCAL
	{
		static void RenewRenderCanvas(ULexWidget* Widget)
		{
			auto ThisRenderCanvas = Widget->GetOwner()->FindComponentByClass<ULexCanvas>();
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

			for (auto& uiChild : Widget->UIChildren)
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
			auto NewRenderCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(Widget->GetOwner(), false);
			Widget->SetRenderCanvas(NewRenderCanvas);

			for (auto& uiChild : Widget->UIChildren)
			{
				if (IsValid(uiChild))
				{
					ForceRefreshRenderCanvasRecursive(uiChild);
				}
			}
		}
		static void UpdateComponentToWorldRecursive(ULexWidget* Widget)
		{
			if (!IsValid(Widget))return;
			Widget->UpdateComponentToWorld();
			auto& Children = Widget->GetUIChildren();
			for (auto& Child : Children)
			{
				UpdateComponentToWorldRecursive(Child);
			}
		}
	};
	MarkAllDirtyRecursive();
	LOCAL::RenewRenderCanvas(this);
	LOCAL::EnsureDataForRebuildRecursive(this);
	LOCAL::ForceRefreshRenderCanvasRecursive(this);
	CalculateVisibility_Recursive();
	CalculateHitTest_Recursive();
	LOCAL::UpdateComponentToWorldRecursive(this);
}

#endif

bool ULexWidget::MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* Hit, EMoveComponentFlags MoveFlags, ETeleportType Teleport)
{
	auto result = Super::MoveComponentImpl(Delta, NewRotation, bSweep, Hit, MoveFlags, Teleport);
	if (this->IsRegistered()//check if registerred, because it may called from reconstruction.
		)
	{
		
	}
	return result;
}
void ULexWidget::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);

	if (this->IsCanvasWidget() && this->RenderCanvas.IsValid())
	{
		//This is mainly to mark LGUICanvas's bIsViewProjectionMatrixDirty to true.
		//For the condition LGUI_Tutorials/Tutorials/UIRenderTarget, when move LGUIRenderTarget1 at runtime, the LGUICanvas's RenderTarget's matrix not update, result in wrong interaction.
		this->RenderCanvas->MarkSizeChanged();
	}
	MarkTransformChanged(true, true);
	if (IsValid(Layout))
	{
		Layout->OnTransformChanged();
	}
	if (IsValid(Visual))
	{
		Visual->OnTransformChanged();
	}
}

void ULexWidget::OnChildAttached(USceneComponent* ChildComponent)
{
	Super::OnChildAttached(ChildComponent);
	if (!IsValid(this) || this->IsUnreachable())return;
	if (GetWorld() == nullptr)return;
	if (ULexWidget* ChildWidget = Cast<ULexWidget>(ChildComponent))
	{
		ChildWidget->UIParent = this;
		ChildWidget->OnUIAttachedToParent();

		EnsureUIChildrenValid();//check
		UIChildren.Add(ChildWidget);
		
		auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
		if (PrefabManager && PrefabManager->IsPrefabSystemProcessingActor(this->GetOwner()))//load from prefab or duplicated by LGUI PrefabSystem, then not set hierarchy index
		{
			//if is load from prefab system, then we don't need to sort children, because children is already sorted when save prefab
		}
		else
		{
			//need sort children here, make it true so we can sort children if we need to
			bNeedSortUIChildren = true;

			if (ChildWidget->IsRegistered())
			{
				ChildWidget->SiblingIndex = UIChildren.Num() - 1;
				ChildWidget->Call_SiblingIndexChanged();
			}
			else//not registered means is loading from level. then no need to set hierarchy index
			{

			}
		}

		//make sure hierarchyindex all good
		if (ChildWidget->SiblingIndex == INDEX_NONE)
		{
			for (int i = 0; i < UIChildren.Num(); i++)
			{
				auto& UIChild = UIChildren[i];
				if (UIChild->SiblingIndex != i)
				{
					UIChild->SiblingIndex = i;
					UIChild->Call_SiblingIndexChanged();
				}
			}
		}

		MarkCanvasUpdate(false, false, false);
	}
}

void ULexWidget::OnUIAttachedToParent()
{
	auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
	if (PrefabManager && PrefabManager->IsPrefabSystemProcessingActor(this->GetOwner()))//when load from prefab or duplicate by LGUI PrefabSystem, the ChildAttachmentChanged callback should execute til prefab serialization ready
	{

	}
	else
	{
		if (this->IsRegistered())//not registered means is loading from level.
		{
			Call_TransformChanged();
			//this->CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
		}
	}

	ULexCanvas* ParentCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(GetOwner()->GetAttachParentActor(), false);
	UIHierarchyAttachmentChanged(ParentCanvas, UIParent->RootWidget.Get());
	MarkLayoutDirty();
	MarkClipDirty(true);
}

void ULexWidget::OnChildDetached(USceneComponent* ChildComponent)
{
	Super::OnChildDetached(ChildComponent);
	if (!IsValid(this) || this->IsUnreachable())return;
	if (GetWorld() == nullptr)return;

	if (auto ChildWidget = Cast<ULexWidget>(ChildComponent))
	{
		ChildWidget->bIsDetaching = true;
		//hierarchy index
		EnsureUIChildrenValid();
		UIChildren.Remove(ChildWidget);
		for (int i = 0; i < UIChildren.Num(); i++)
		{
			auto& UIChild = UIChildren[i];
			if (UIChild->SiblingIndex != i)
			{
				UIChild->SiblingIndex = i;
				UIChild->Call_SiblingIndexChanged();
			}
		}
		ChildWidget->UIParent = nullptr;
		MarkLayoutDirty();
		if (IsValid(Layout))
		{
			Layout->OnChildDetached(ChildWidget);
		}
		MarkCanvasUpdate(false, false, false);
	}
}

void ULexWidget::OnAttachmentChanged()
{
	if (this->bIsDetaching)//OnAttachmentChanged happens after SetParent, which is better for search parent things
	{
		this->OnUIDetachedFromParent();
		this->bIsDetaching = false;
	}
}

void ULexWidget::OnUIDetachedFromParent()
{
	auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
	if (PrefabManager && PrefabManager->IsPrefabSystemProcessingActor(this->GetOwner()))//when load from prefab or duplicate by LGUI PrefabSystem, the ChildAttachmentChanged callback should execute til prefab serialization ready
	{
		
	}
	else
	{
		if (this->IsRegistered())//not registered means is loading from level.
		{
			Call_TransformChanged();
			//this->CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
		}
	}

	UIHierarchyAttachmentChanged(nullptr, nullptr);
	MarkLayoutDirty();
	MarkClipDirty(true);
}

void ULexWidget::OnRegister()
{
	Super::OnRegister();
	//UE_LOG(LGUI, Error, TEXT("OnRegister:%s, registered:%d"), *(this->GetOwner()->GetActorLabel()), this->IsRegistered());
#if WITH_EDITOR
	if (auto world = this->GetWorld())
	{
		if (!world->IsGameWorld() && GetOwner() && !IsRunningCommandlet())
		{
			//create helper for root component
			if (this->GetOwner()->GetRootComponent() == this 
				)
			{
				if (!HelperComp)
				{
					HelperComp = NewObject<ULexWidgetEditorHelperComp>(GetOwner(), NAME_None, RF_Transient | RF_TextExportTransient);
					HelperComp->Parent = this;
					HelperComp->Mobility = EComponentMobility::Movable;
					HelperComp->SetIsVisualizationComponent(true);
					HelperComp->SetupAttachment(this);
					HelperComp->RegisterComponent();
				}

				//display name
				auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
				if (PrefabManager && PrefabManager->IsPrefabSystemProcessingActor(this->GetOwner()))//when load from prefab or duplicate by LGUI PrefabSystem, the displayName should be set from prefab
				{

				}
				else
				{
					auto actorLabel = FString(*this->GetOwner()->GetActorLabel());
					this->DisplayName = actorLabel;
				}
			}
			else
			{
				this->DisplayName = this->GetName();
			}
		}
	}
	else
	{
		this->DisplayName = this->GetName();
	}
#endif

#if WITH_EDITOR
	//apply inactive actor's visibility state in editor scene outliner
	if (auto OwnerActor = GetOwner())
	{
		if (!IsVisibleForRender())
		{
			OwnerActor->SetIsTemporarilyHiddenInEditor(true);
		}
	}
#endif

	CheckRootWidget();

	if (IsValid(Layout))
	{
		Layout->OnRegister();
	}
	if (IsValid(Visual))
	{
		Visual->OnRegister();
	}
}
void ULexWidget::OnUnregister()
{
	Super::OnUnregister();
#if WITH_EDITOR
	if (auto world = this->GetWorld())
	{
		if (!world->IsGameWorld())
		{
			if (this->GetName().StartsWith(TEXT("REINST_")))//when recompile a blueprint object, the old one will become REINST_XXX and not valid
			{
				if (RenderCanvas.IsValid())
				{
					OnRenderCanvasChanged(RenderCanvas.Get(), nullptr);
					RenderCanvas = nullptr;
				}
			}
		}
	}
	if (HelperComp)
	{
		HelperComp->DestroyComponent();
		HelperComp = nullptr;
	}
#endif
	CheckRootWidget();

	if (IsValid(Layout))
	{
		Layout->OnUnregister();
	}
	if (IsValid(Visual))
	{
		Visual->OnUnregister();
	}
}

void ULexWidget::EnsureUIChildrenValid()
{
	for (int i = UIChildren.Num() - 1; i >= 0; i--)
	{
		if (!IsValid(UIChildren[i]))
		{
			UIChildren.RemoveAt(i);
		}
	}
}

void ULexWidget::EnsureUIChildrenSorted()const
{
	if (bNeedSortUIChildren)
	{
		bNeedSortUIChildren = false;
		UIChildren.Sort([](const ULexWidget& A, const ULexWidget& B)
			{
				if (A.GetSiblingIndex() < B.GetSiblingIndex())
					return true;
				return false;
			});
	}
}

void ULexWidget::SetPivot(FVector2D Value) 
{
	if (!Pivot.Equals(Value, 0.0f))
	{
		Pivot = Value;
		MarkDimensionChanged(true, false, false);
	}
}

void ULexWidget::RegisterRenderCanvas(ULexCanvas* InRenderCanvas)
{
	bIsCanvasWidget = true;
	auto ParentCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(GetOwner()->GetAttachParentActor(), false);//@todo: replace with Canvas's ParentCanvas?
	if (RenderCanvas != InRenderCanvas)
	{
		SetRenderCanvas(InRenderCanvas);
	}
	InRenderCanvas->SetParentCanvas(ParentCanvas);
	for (auto& Child : UIChildren)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(InRenderCanvas);
		}
	}
}
void ULexWidget::RenewRenderCanvasRecursive(ULexCanvas* InParentRenderCanvas)
{
	auto ThisRenderCanvas = GetOwner()->FindComponentByClass<ULexCanvas>();
	if (ThisRenderCanvas != nullptr && !ThisRenderCanvas->IsRegistered())//ignore unregistered
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

	for (auto& Child : UIChildren)
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
	auto ParentCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(GetOwner()->GetAttachParentActor(), false);
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->SetParentCanvas(nullptr);
	}
	if (RenderCanvas != ParentCanvas)
	{
		SetRenderCanvas(ParentCanvas);
	}
	for (auto& Child : UIChildren)
	{
		if (IsValid(Child))
		{
			Child->RenewRenderCanvasRecursive(ParentCanvas);
		}
	}
}

void ULexWidget::UpdateLayout()
{
	if (!bLayoutDirty)return;
	bLayoutDirty = false;
	if (IsValid(Layout))
	{
		Layout->UpdateLayout();
	}

	if (!IsValid(LayoutSlot))
	{
		SetRenderSizeByLayout(GetPreferredSize());
		if (UIParent.IsValid())
		{
			auto Position = this->GetRelativeLocation();
			{
				float PaddingAndMarginOffset = UIParent->GetPadding().Left - UIParent->GetPadding().Right + (this->GetMargin().Left - this->GetMargin().Right);
				PaddingAndMarginOffset *= 0.5f;
				auto PivotOffset =
					this->GetRenderSize().X * (this->GetPivot().X - 0.5f)//this pivot
				+ UIParent->GetRenderSize().X * (0.5f - UIParent->GetPivot().X);//parent pivot
				Position.Y = PaddingAndMarginOffset + PivotOffset;
			}
			{
				float PaddingAndMarginOffset = UIParent->GetPadding().Bottom - UIParent->GetPadding().Top + (this->GetMargin().Bottom - this->GetMargin().Top);
				PaddingAndMarginOffset *= 0.5f;
				auto PivotOffset =
					this->GetRenderSize().Y * (this->GetPivot().Y - 0.5f)//this pivot
				+ UIParent->GetRenderSize().Y * (0.5f - UIParent->GetPivot().Y);//parent pivot
				Position.Z = PaddingAndMarginOffset + PivotOffset;
			}
			this->SetRelativeLocation(Position);
		}
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
	if (UIParent.IsValid())
	{
		ParentClip = UIParent->ClipData.Pin();
	}
	switch (Clipping)
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
	this->bClipDataChanged = true;
}

void ULexWidget::UpdateVisual() const
{
	if (IsValid(Visual))
	{
		Visual->UpdateGeometry();
	}
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
			OldRenderCanvas->UnregisterVisual(this);
		}
	}
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->AddLexWidget(this);
		if (IsValid(Visual))
		{
			RenderCanvas->RegisterVisual(this);
		}
	}
}

void ULexWidget::UIHierarchyAttachmentChanged(ULexCanvas* ParentRenderCanvas, ULexWidget* ParentRoot)
{
	auto ThisRenderCanvas = GetOwner()->FindComponentByClass<ULexCanvas>();
	if (ThisRenderCanvas != nullptr)
	{
		ParentRenderCanvas = ThisRenderCanvas;
	}

	if (RenderCanvas != ParentRenderCanvas)//if attach to new Canvas, need to remove from old and add to new
	{
		SetRenderCanvas(ParentRenderCanvas);
	}

	CheckRootWidget(ParentRoot);
	for (auto& Child : UIChildren)
	{
		if (IsValid(Child))
		{
			Child->UIHierarchyAttachmentChanged(ParentRenderCanvas, ParentRoot);
		}
	}

	//flatten hierarchy index
	MarkFlattenHierarchyIndexDirty();

	CalculateVisibility_Recursive();
	CalculateHitTest_Recursive();

	//if (this->IsRegistered())//not register means could be load from level
	{
		MarkDimensionChanged(false, true, true);
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
}

void ULexWidget::CalculateRenderSize()
{
	bool LayoutSlotValid = IsValid(LayoutSlot);
	if (!LayoutSlotValid || !LayoutSlot->GetLayoutControlWidth())
	{
		RenderSize.X = GetPreferredWidth();
	}
	if (!LayoutSlotValid || !LayoutSlot->GetLayoutControlHeight())
	{
		RenderSize.Y = GetPreferredHeight();
	}
} 

void ULexWidget::CheckRootWidget(ULexWidget* RootWidgetInParent)
{
	auto OldRootWidget = RootWidget;
	if (OldRootWidget == this && OldRootWidget != nullptr)
	{
		ULGUIManagerWorldSubsystem::RemoveRootWidget(this);
	}

	if (RootWidgetInParent == nullptr)
	{
		ULexWidget* TopWidget = this;
		ULexWidget* TempRootWidget = nullptr;
		while (TopWidget != nullptr && TopWidget->IsRegistered())
		{
			TempRootWidget = TopWidget;
			TopWidget = Cast<ULexWidget>(TopWidget->GetAttachParent());
		}
		RootWidgetInParent = TempRootWidget;
	}
	RootWidget = RootWidgetInParent;

	if (RootWidget == this && RootWidget != nullptr)
	{
		ULGUIManagerWorldSubsystem::AddRootWidget(this);
	}
}

void ULexWidget::CalculateVisibility_Recursive()
{
	struct LOCAL
	{
		static void CalculateRenderVisibility(ULexWidget* Widget)
		{
			auto WidgetVisibility = Widget->WidgetVisibility;
			bool bResultVisibility = true;
			bool bSelfVisibleForRender = WidgetVisibility == ELexWidgetVisibility::Visible;
			if (!bSelfVisibleForRender)
				bResultVisibility = false;
			else if (Widget->UIParent.IsValid())
				bResultVisibility = Widget->UIParent->IsVisibleForRender();
			else
				bResultVisibility = true;

			if (Widget->bCacheIsVisibleForRender != bResultVisibility)
			{
				Widget->bCacheIsVisibleForRender = bResultVisibility;
				Widget->Call_RenderVisibilityChanged();
				for (auto& Child : Widget->GetUIChildren())
				{
					CalculateRenderVisibility(Child);
				}
			}
		}
		static void CalculateLayoutVisibility(ULexWidget* Widget)
		{
			auto WidgetVisibility = Widget->WidgetVisibility;
			bool bResultVisibility = true;
			bool bSelfVisibleForLayout = WidgetVisibility == ELexWidgetVisibility::Visible
			|| WidgetVisibility == ELexWidgetVisibility::Hidden;
			if (bSelfVisibleForLayout == false)
				bResultVisibility = false;
			else if (Widget->UIParent.IsValid())
				bResultVisibility = Widget->UIParent->IsVisibleForLayout();
			else
				bResultVisibility = true;

			if (Widget->bCacheIsVisibleForLayout != bResultVisibility)
			{
				Widget->bCacheIsVisibleForLayout = bResultVisibility;
				Widget->Call_LayoutVisibilityChanged();
				for (auto& Child : Widget->GetUIChildren())
				{
					CalculateLayoutVisibility(Child);
				}
			}
		}
	};
	LOCAL::CalculateRenderVisibility(this);
	LOCAL::CalculateLayoutVisibility(this);
}

void ULexWidget::CalculateHitTest_Recursive()
{
	struct LOCAL
	{
		static void CalculateHitTestVisibility(ULexWidget* Widget)
		{
			auto HitTestType = Widget->HitTestType;
			bool bResult = true;
			if (HitTestType == ELexWidgetHitTestType::NotHitTestable)
				bResult = false;
			else if (HitTestType == ELexWidgetHitTestType::HitTestable)
				bResult = true;
			else if (HitTestType == ELexWidgetHitTestType::Inherit)
			{
				if (Widget->UIParent.IsValid())
					bResult = Widget->UIParent->IsVisibleForHitTest();
				else
					bResult = true;
			}
			else
				bResult = true;

			if (Widget->bCacheIsVisibleForHitTest != bResult)
			{
				Widget->bCacheIsVisibleForHitTest = bResult;
				Widget->Call_HitTestVisibilityChanged();
				for (auto& Child : Widget->GetUIChildren())
				{
					CalculateHitTestVisibility(Child);
				}
			}
		}
	};
	LOCAL::CalculateHitTestVisibility(this);
}

FVector2D ULexWidget::GetRenderSize() const
{
	if (bRenderSizeDirty)
	{
		bRenderSizeDirty = false;
		const_cast<ULexWidget*>(this)->CalculateRenderSize();
	}
	return RenderSize;
}

float ULexWidget::GetPreferredWidth() const
{
	float PreferredWidth = 0;
	if (AspectRatio.Type == ELexWidgetAspectRatioType::HeightControlWidth)
	{
		PreferredWidth = GetPreferredHeight() * AspectRatio.Value;
	}
	else
	{
		switch (Width.Type)
		{
		case ELexWidgetSizeType::Fixed:
			PreferredWidth = Width.Value;
			break;
		case ELexWidgetSizeType::ExpandToParent:
			if (UIParent.IsValid())
			{
				if (UIParent->Width.Type == ELexWidgetSizeType::ShrinkToChildren)//size conflict, fallback to fixed value
				{
					PreferredWidth = Width.Value;
				}
				else
				{
					PreferredWidth = UIParent->GetRenderSize().X
					- (UIParent->GetPadding().Left + UIParent->GetPadding().Right)
					- (this->GetMargin().Left + this->GetMargin().Right)
					;
					PreferredWidth *= Width.Percent * 0.01f;
				}
			}
			else
			{
				PreferredWidth = Width.Value;
			}
			break;
		case ELexWidgetSizeType::ShrinkToChildren:
			if (IsValid(Layout) && Layout->SupportShrinkToChildrenWidth() && this->IsVisibleForLayout())
			{
				PreferredWidth = Layout->GetShrinkToChildrenWidth();
			}
			else if (UIChildren.Num() > 0)
			{
				float MaxSize = 0;
				for (auto& Child : UIChildren)
				{
					auto ChildSize = Child->GetRenderSize().X + (Child->GetMargin().Left + Child->GetMargin().Right);
					if (MaxSize < ChildSize)
					{
						MaxSize = ChildSize;
					}
				}
				PreferredWidth = MaxSize + (this->GetPadding().Left + this->GetPadding().Right);
			}
			else if (IsValid(Visual))
			{
				PreferredWidth = Visual->GetShrinkToContentWidth();
			}
			else
			{
				PreferredWidth = Width.Value;
			}
			break;
		}
	}
	return PreferredWidth;
}

float ULexWidget::GetPreferredHeight() const
{
	float PreferredHeight = 0;
	if (AspectRatio.Type == ELexWidgetAspectRatioType::WidthControlHeight)
	{
		PreferredHeight = GetPreferredWidth() / AspectRatio.Value;
	}
	else
	{
		switch (Height.Type)
		{
		case ELexWidgetSizeType::Fixed:
			PreferredHeight = Height.Value;
			break;
		case ELexWidgetSizeType::ExpandToParent:
			if (UIParent.IsValid())
			{
				if (UIParent->Height.Type == ELexWidgetSizeType::ShrinkToChildren)//size conflict, fallback to fixed value
				{
					PreferredHeight = Height.Value;
				}
				else
				{
					PreferredHeight = UIParent->GetRenderSize().Y
					- (UIParent->GetPadding().Bottom + UIParent->GetPadding().Top)
					- (this->GetMargin().Bottom + this->GetMargin().Top)
					;
					PreferredHeight *= Height.Percent * 0.01f;
				}
			}
			else
			{
				PreferredHeight = Height.Value;
			}
			break;
		case ELexWidgetSizeType::ShrinkToChildren:
			if (IsValid(Layout) && Layout->SupportShrinkToChildrenHeight() && this->IsVisibleForLayout())
			{
				PreferredHeight = Layout->GetShrinkToChildrenHeight();
			}
			else if (UIChildren.Num() > 0)
			{
				float MaxSize = 0;
				for (auto& Child : UIChildren)
				{
					auto ChildSize = Child->GetRenderSize().Y + (Child->GetMargin().Bottom + Child->GetMargin().Top);
					if (MaxSize < ChildSize)
					{
						MaxSize = ChildSize;
					}
				}
				PreferredHeight = MaxSize + (this->GetPadding().Bottom + this->GetPadding().Top);
			}
			else if (IsValid(Visual))
			{
				PreferredHeight = Visual->GetShrinkToContentHeight();
			}
			else
			{
				PreferredHeight = Height.Value;
			}
			break;
		}
	}
	return PreferredHeight;
}

FVector2D ULexWidget::GetPreferredSize() const
{
	return FVector2D(GetPreferredWidth(), GetPreferredHeight());
}

void ULexWidget::SetRenderSizeByLayout(FVector2D Value)
{
	if (RenderSize != Value)
	{
		RenderSize = Value;
		bRenderSizeDirty = false;
		MarkDimensionChanged(false, true, true);
	}
}

void ULexWidget::SetAspectRatio(const FLexWidgetAspectRatio& Value)
{
	if (AspectRatio != Value)
	{
		AspectRatio = Value;
		MarkRenderSizeChanged();
	}
}

void ULexWidget::SetWidth(const FLexWidgetSize& Value)
{
	if (Width != Value)
	{
		Width = Value;
		MarkRenderSizeChanged();
	}
}

void ULexWidget::SetHeight(const FLexWidgetSize& Value)
{
	if (Height != Value)
	{
		Height = Value;
		MarkRenderSizeChanged();
	}
}

void ULexWidget::SetSize(const FLexWidgetSize2& Value)
{
	if (Width != Value.X || Height != Value.Y)
	{
		Width = Value.X;
		Height = Value.Y;
		MarkRenderSizeChanged();
	}
}

void ULexWidget::SetPadding(const FMargin& Value)
{
	if (Padding != Value)
	{
		Padding = Value;
		MarkRenderSizeChanged();
	}
}

void ULexWidget::SetMargin(const FMargin& Value)
{
	if (Margin != Value)
	{
		Margin = Value;
		MarkRenderSizeChanged();
	}
}

ULexWidget* ULexWidget::GetUIChild(int index)const
{
	if (index < 0 || index >= UIChildren.Num())
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Index:%d out of range[%d, %d]"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, index, 0, UIChildren.Num() - 1);
		return nullptr;
	}
	EnsureUIChildrenSorted();
	return UIChildren[index];
}
ULexCanvas* ULexWidget::GetRootCanvas()const
{
	if (RenderCanvas.IsValid())
	{
		return RenderCanvas->GetRootCanvas();
	}
	return nullptr;
}
ULexCanvasScaler* ULexWidget::GetCanvasScaler()const
{
	if (auto canvas = GetRootCanvas())
	{
		return canvas->GetOwner()->FindComponentByClass<ULexCanvasScaler>();
	}
	return nullptr;
}

FVector2D ULexWidget::GetLocalSpaceLeftBottomPoint()const
{
	FVector2D leftBottomPoint;
	leftBottomPoint.X = GetRenderWidth() * -Pivot.X;
	leftBottomPoint.Y = GetRenderHeight() * -Pivot.Y;
	return leftBottomPoint;
}
FVector2D ULexWidget::GetLocalSpaceRightTopPoint()const
{
	FVector2D rightTopPoint;
	rightTopPoint.X = GetRenderWidth() * (1.0f - Pivot.X);
	rightTopPoint.Y = GetRenderHeight() * (1.0f - Pivot.Y);
	return rightTopPoint;
}
FVector2D ULexWidget::GetLocalSpaceCenter()const
{
	return FVector2D(this->GetRenderWidth() * (0.5f - Pivot.X), this->GetRenderHeight() * (0.5f - Pivot.Y));
}

float ULexWidget::GetLocalSpaceLeft()const
{
	return this->GetRenderWidth() * -Pivot.X;
}
float ULexWidget::GetLocalSpaceRight()const
{
	return this->GetRenderWidth() * (1.0f - Pivot.X);
}
float ULexWidget::GetLocalSpaceBottom()const
{
	return this->GetRenderHeight() * -Pivot.Y;
}
float ULexWidget::GetLocalSpaceTop()const
{
	return this->GetRenderHeight() * (1.0f - Pivot.Y);
}

void ULexWidget::MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	if (ClipData.IsValid() && ClipData.Pin()->GetWidget() == this)
	{
		ClipData.Pin()->MarkNeedUpdateData();
	}

	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);
	if (IsValid(Layout))
	{
		Layout->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}
	if (IsValid(Visual))
	{
		Visual->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}

	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(false, InPivotChanged || InWidthChanged || InHeightChanged, false);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			this->RenderCanvas->MarkSizeChanged();
		}
	}

	Call_DimensionsChanged(InPivotChanged, InWidthChanged, InHeightChanged);
}

void ULexWidget::MarkTransformChanged(bool InPositionChanged, bool InScaleChanged)
{
	if (ClipData.IsValid() && ClipData.Pin()->GetWidget() == this)
	{
		ClipData.Pin()->MarkNeedUpdateData();
	}
	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(false, true, false);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			this->RenderCanvas->MarkSizeChanged();
		}
	}

	Call_TransformChanged();

	for (auto& UIChild : UIChildren)
	{
		if (IsValid(UIChild))
		{
			UIChild->MarkTransformChanged(InPositionChanged, InScaleChanged);
		}
	}
}

void ULexWidget::MarkRenderSizeChanged()
{
	struct LOCAL
	{
		static void MarkDirty(ULexWidget* Target)
		{
			Target->bRenderSizeDirty = true;
			Target->MarkDimensionChanged(false, true, true);
			if (IsValid(Target->Layout))
			{
				Target->MarkLayoutDirty();
				for (auto& Child : Target->GetUIChildren())
				{
					MarkDirty(Child);
				}
			}
			else
			{
				for (auto& Child : Target->GetUIChildren())
				{
					if (Child->Width.Type == ELexWidgetSizeType::ExpandToParent
						|| Child->Height.Type == ELexWidgetSizeType::ExpandToParent)
					{
						MarkDirty(Child);
					}
				}
			}
		}
	};
	//search up in hierarchy to find the first widget which is affected by the size change
	auto ParentWillBeAffected = this;
	while (ParentWillBeAffected != nullptr)
	{
		auto TempParent = ParentWillBeAffected->GetUIParent();
		if (!TempParent)
		{
			break;
		}
		if (TempParent->GetLayout())
		{
			ParentWillBeAffected = TempParent;
		}
		else
		{
			if (TempParent->GetWidth().Type == ELexWidgetSizeType::ShrinkToChildren
			|| TempParent->GetHeight().Type == ELexWidgetSizeType::ShrinkToChildren)
			{
				ParentWillBeAffected = TempParent;
			}
			else
			{
				break;
			}
		}
	}
	LOCAL::MarkDirty(ParentWillBeAffected);
}

void ULexWidget::MarkCanvasUpdate(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawcall)
{
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->MarkCanvasUpdate(bMaterialOrTextureChanged, bTransformOrVertexPositionChanged, bHierarchyOrderChanged, bForceRebuildDrawcall);
	}
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

void ULexWidget::MarkLayoutDirty()
{
	bLayoutDirty = true;
	MarkCanvasUpdate(false, true, false);
}

void ULexWidget::MarkClipDirty(bool InClipTypeChanged) const
{
	bClipDirty = true;
	if (InClipTypeChanged)bNeedRecreateClip = true;
	struct LOCAL
	{
		static void MarkDirty(const ULexWidget* Widget)
		{
			switch (Widget->Clipping)
			{
			case ELexWidgetClipping::Inherit:
			case ELexWidgetClipping::ClipToBounds:
				Widget->bClipDirty = true;
				break;
			case ELexWidgetClipping::ClipToBoundsWithoutIntersecting:
			case ELexWidgetClipping::Disabled:
				return;
			}

			for (auto& Child : Widget->GetUIChildren())
			{
				MarkDirty(Child);
			}
		}
	};
	for (auto& Child : this->GetUIChildren())
	{
		LOCAL::MarkDirty(Child);
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
		Clipping = Value;
		MarkClipDirty(true);
	}
}

void ULexWidget::CalculateIsEnabled_Recursive()
{
	struct LOCAL
	{
		static void CalculateIsEnabled(ULexWidget* Widget)
		{
			bool bResult = true;
			if (!Widget->bIsEnabled)
			{
				bResult = false;
			}
			else if (Widget->UIParent.IsValid())
			{
				bResult = Widget->UIParent->GetFinalIsEnabled();
			}
			else
			{
				bResult = true;
			}

			if (Widget->bCacheFinalIsEnabled != bResult)
			{
				Widget->bCacheFinalIsEnabled = bResult;
				Widget->Call_IsEnabledChanged();
				for (auto& Child : Widget->GetUIChildren())
				{
					CalculateIsEnabled(Child);
				}
			}
		}
	};
	LOCAL::CalculateIsEnabled(this);
}

float ULexWidget::GetFinalRenderOpacity()const
{
	if (UIParent.IsValid())
	{
		return this->RenderOpacity * UIParent->GetFinalRenderOpacity();
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
				for (auto& Child : Widget->UIChildren)
				{
					MarkDirty(Child);
				}
			}
		};
		LOCAL::MarkDirty(this);
	}
}

bool ULexWidget::GetFinalPixelSnapping() const
{
	switch (this->PixelSnapping)
	{
	case EWidgetPixelSnapping::SnapToPixel:
		return true;
	case EWidgetPixelSnapping::Disabled:
		return false;
	case EWidgetPixelSnapping::Inherit:
		if (UIParent.IsValid())
		{
			return UIParent->GetFinalPixelSnapping();
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
				for (auto& Child : Widget->GetUIChildren())
				{
					MarkChanged(Child);
				}
			}
		};
		LOCAL::MarkChanged(this);
	}
}

bool ULexWidget::IsVisibleForRender() const
{
	return bCacheIsVisibleForRender;
}

bool ULexWidget::IsVisibleForHitTest() const
{
	return bCacheIsVisibleForHitTest;
}

bool ULexWidget::IsVisibleForLayout() const
{
	return bCacheIsVisibleForLayout;
}

void ULexWidget::SetWidgetVisibility(ELexWidgetVisibility Value)
{
	if (WidgetVisibility != Value)
	{
		WidgetVisibility = Value;
		CalculateVisibility_Recursive();
	}
}

void ULexWidget::SetHitTestType(ELexWidgetHitTestType Value)
{
	if (HitTestType != Value)
	{
		HitTestType = Value;
		CalculateHitTest_Recursive();
	}
}

void ULexWidget::SetIsEnabled(bool Value)
{
	if (bIsEnabled != Value)
	{
		bIsEnabled = Value;
		CalculateIsEnabled_Recursive();
	}
}

const ULexWidget* ULexWidget::GetRestrictNavigationAreaWidget() const
{
	if (bRestrictNavigationArea)
	{
		return this;
	}
	if (UIParent.IsValid())
	{
		return UIParent->GetRestrictNavigationAreaWidget();
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
	auto NewVisual = NewObject<ULexVisual>(this, VisualClass);
	if (RenderCanvas.IsValid())
	{
		if (IsValid(OldVisual))
		{
			RenderCanvas->MarkVisualWillChange(OldVisual);
			RenderCanvas->UnregisterVisual(this);
		}
		if (NewVisual)
		{
			RenderCanvas->RegisterVisual(this);
		}
	}
	if (IsValid(OldVisual))
	{
		if (GetWorld()->IsGameWorld())
		{
			if (this->HasBegunPlay())
			{
				OldVisual->EndPlay();
			}
		}
		OldVisual->OnUnregister();
	}
	
	NewVisual->OnRegister();
	if (GetWorld()->IsGameWorld())
	{
		if (this->HasBegunPlay())
		{
			NewVisual->BeginPlay();
		}
	}
	Visual = NewVisual;
	return NewVisual;
}

ULexLayout* ULexWidget::CreateNewLayout(TSubclassOf<ULexLayout> LayoutClass)
{
	auto OldLayout = Layout;
	auto NewLayout = NewObject<ULexLayout>(this, LayoutClass);
	if (IsValid(OldLayout))
	{
		if (GetWorld()->IsGameWorld())
		{
			if (this->HasBegunPlay())
			{
				OldLayout->EndPlay();
			}
		}
		OldLayout->OnUnregister();
	}
	
	NewLayout->OnRegister();
	if (GetWorld()->IsGameWorld())
	{
		if (this->HasBegunPlay())
		{
			NewLayout->BeginPlay();
		}
	}
	Layout = NewLayout;
	return NewLayout;
}

ULexLayoutSlot* ULexWidget::GetLayoutSlot() const
{
	auto ClearLayoutSlot = [this]
	{
		if (IsValid(LayoutSlot))
		{
			LayoutSlot->MarkAsGarbage();
			LayoutSlot = nullptr;
		}
	};
	if (!UIParent.IsValid())
	{
		ClearLayoutSlot();
		return nullptr;
	}
	auto ParentLayout = UIParent->GetLayout();
	if (!ParentLayout)
	{
		ClearLayoutSlot();
		return nullptr;
	}

	auto LayoutSlotClass = ParentLayout->GetSlotClass();
	if (!IsValid(LayoutSlot) || LayoutSlot->GetClass() != LayoutSlotClass)
	{
		LayoutSlot = ParentLayout->GetOrCreateSlot(this, ParentLayout->GetSlotClass());
	}
	return LayoutSlot.Get();
}

#if WITH_EDITOR
void ULexWidget::SetIsTemporarilyHiddenInEditor_Recursive_By_RenderVisibility()
{
#if WITH_EDITOR
	//modify inactive actor's name
	auto Actor = GetOwner();
	if (Actor != nullptr && this == Actor->GetRootComponent())
	{
		auto bHiddenEdTemporary_Property = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bHiddenEdTemporary"));
		bHiddenEdTemporary_Property->SetPropertyValue_InContainer(Actor, !IsVisibleForRender());
		//Actor->SetIsTemporarilyHiddenInEditor(!IsVisibleForRender());
	}
#endif
	//callback
	Call_RenderVisibilityChanged();
	Call_LayoutVisibilityChanged();
	Call_HitTestVisibilityChanged();
	//canvas update
	MarkCanvasUpdate(false, false, false, true);

	//affect children
	for (auto& uiChild : UIChildren)
	{
		if (IsValid(uiChild))
		{
			uiChild->SetIsTemporarilyHiddenInEditor_Recursive_By_RenderVisibility();
		}
	}
}
#endif

#pragma region TweenAnimation
#include "LTweenManager.h"

ULTweener* ULexWidget::RenderOpacityTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetRenderOpacity), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetRenderOpacity), endValue, duration);
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		if (this->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetEase(ease)->SetDelay(delay)->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
	return Tweener;
}
#pragma endregion




ULexWidgetEditorHelperComp::ULexWidgetEditorHelperComp()
{
	bSelectable = false;
	bIsEditorOnly = true;
	MarkAsEditorOnlySubobject();
}

#if WITH_EDITOR
FPrimitiveSceneProxy* ULexWidgetEditorHelperComp::CreateSceneProxy()
{
	class FWidgetSceneProxy : public FPrimitiveSceneProxy
	{
	public:
		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		FWidgetSceneProxy(ULexWidget* InComponent, UPrimitiveComponent* InPrimitive)
			: FPrimitiveSceneProxy(InPrimitive)
		{
			bWillEverBeLit = false;
			Component = InComponent;
		}

		virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily, uint32 VisibilityMap, FMeshElementCollector& Collector) const override
		{
			return;
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = true;
			Result.bDynamicRelevance = true;
			Result.bShadowRelevance = IsShadowCast(View);
			Result.bEditorPrimitiveRelevance = UseEditorCompositing(View);
			return Result;
		}
		virtual uint32 GetMemoryFootprint(void) const override { return(sizeof(*this) + GetAllocatedSize()); }
		uint32 GetAllocatedSize(void) const { return(FPrimitiveSceneProxy::GetAllocatedSize()); }
	private:
		TWeakObjectPtr<ULexWidget> Component;
	};

	return new FWidgetSceneProxy(this->Parent, this);
}
#endif

UBodySetup* ULexWidgetEditorHelperComp::GetBodySetup()
{
	UpdateBodySetup();
	return BodySetup;
}
void ULexWidgetEditorHelperComp::UpdateBodySetup()
{
	if (!IsValid(Parent))return;
	if (!IsValid(BodySetup))
	{
		BodySetup = NewObject<UBodySetup>(this, NAME_None, RF_Transient);
		BodySetup->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		FKBoxElem Box = FKBoxElem();
		Box.SetTransform(FTransform::Identity);
		BodySetup->AggGeom.BoxElems.Add(Box);
	}
	FKBoxElem* BoxElem = BodySetup->AggGeom.BoxElems.GetData();

	auto Center = Parent->GetLocalSpaceCenter();
	auto Origin = FVector(0, Center.X, Center.Y);

	BoxElem->X = 0.0f;
	BoxElem->Y = Parent->GetRenderWidth();
	BoxElem->Z = Parent->GetRenderHeight();

	BoxElem->Center = Origin;
}
FBoxSphereBounds ULexWidgetEditorHelperComp::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!IsValid(Parent))return FBoxSphereBounds(EForceInit::ForceInit);
	auto Center = Parent->GetLocalSpaceCenter();
	auto Origin = FVector(0, Center.X, Center.Y);
	return FBoxSphereBounds(Origin, FVector(1, Parent->GetRenderWidth() * 0.5f, Parent->GetRenderHeight() * 0.5f), (Parent->GetRenderWidth() > Parent->GetRenderHeight() ? Parent->GetRenderWidth() : Parent->GetRenderHeight()) * 0.5f).TransformBy(LocalToWorld);
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif
