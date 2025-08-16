// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexWidget.h"
#include "LGUI.h"
#include "Core/Components/LexCanvas.h"
#include "Core/LGUISettings.h"
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
	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorBottomDirty = true;
	bCacheAnchorTopDirty = true;
	bCacheAnchorLeftDirty = true;
	bCacheAnchorRightDirty = true;
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
	CalculateVisibility_Recursive();
	CalculateHitTest_Recursive();
	CalculateIsEnabled_Recursive();
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

		static const FName AnchorDataName = GET_MEMBER_NAME_CHECKED(ULexWidget, AnchorData);
		static const FName VisibilityName = GET_MEMBER_NAME_CHECKED(ULexWidget, WidgetVisibility);
		static const FName HitTestTypeName = GET_MEMBER_NAME_CHECKED(ULexWidget, HitTestType);
		static const FName ClippingName = GET_MEMBER_NAME_CHECKED(ULexWidget, Clipping);
		static const FName VisualName = GET_MEMBER_NAME_CHECKED(ULexWidget, Visual);
		static const FName LayoutName = GET_MEMBER_NAME_CHECKED(ULexWidget, Layout);
		static const FName IsEnabledName = GET_MEMBER_NAME_CHECKED(ULexWidget, bIsEnabled);

		if (MemberName == AnchorDataName
		|| MemberName == VisibilityName
		)
		{
			this->MarkAnchorDataChanged(true, true, true);
			this->MarkClipDirty(false);
		}
		else if (MemberName == ClippingName)
		{
			MarkClipDirty(true);
		}
		else if (MemberName == GET_MEMBER_NAME_CHECKED(ULexWidget, SiblingIndex))
		{
			this->Call_SiblingIndexChanged();
			ApplySiblingIndex();
		}
		else if (MemberName == FName(TEXT("RelativeLocation")))
		{
			CalculateAnchorFromTransform();
			UpdateComponentToWorld();
			MarkLayoutDirty();
		}
		else if (MemberName == VisualName)
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
				Visual->Call_OnRegister();
			}
		}
		else if (MemberName == LayoutName)
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
				Layout->Call_OnRegister();
			}
			MarkAnchorDataChanged(true, true, true);
		}
		if (MemberName == AnchorDataName)
		{
			CalculateTransformFromAnchor();
			UpdateComponentToWorld();
		}
		if (MemberName == VisibilityName)
		{
			CalculateVisibility_Recursive();
		}
		if (MemberName == HitTestTypeName)
		{
			CalculateHitTest_Recursive();
		}
		if (MemberName == IsEnabledName)
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
			Visual->Call_OnUnregister();
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
			Layout->Call_OnUnregister();
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
	return FBoxSphereBounds(Origin, FVector(1, this->GetWidth() * 0.5f, this->GetHeight() * 0.5f), (this->GetWidth() > this->GetHeight() ? this->GetWidth() : this->GetHeight()) * 0.5f).TransformBy(LocalToWorld);
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

	if (bCanSetAnchorFromTransform)
	{
		CalculateAnchorFromTransform();
	}
	if (this->IsCanvasWidget() && this->RenderCanvas.IsValid())
	{
		//This is mainly to mark LGUICanvas's bIsViewProjectionMatrixDirty to true.
		//For the condition LGUI_Tutorials/Tutorials/UIRenderTarget, when move LGUIRenderTarget at runtime, the LGUICanvas's RenderTarget's matrix not update, result in wrong interaction.
		this->RenderCanvas->MarkSizeChanged();
	}
	MarkLayoutDirty();
	auto CompScale3D = this->GetComponentScale();
	auto CompScale2D = FVector2f(CompScale3D.Y, CompScale3D.Z);
	bool ScaleChanged = PrevScale2D != CompScale2D;
	PrevScale2D = CompScale2D;
	MarkTransformChanged(true, ScaleChanged);
	if (IsValid(Layout))
	{
		MarkLayoutDirty();
		Layout->OnTransformChanged();
	}
	if (GetLayoutSlot())
	{
		LayoutSlot->OnTransformChanged();
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
			this->CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
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
			this->CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
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
		Layout->Call_OnRegister();
	}
	if (IsValid(Visual))
	{
		Visual->Call_OnRegister();
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
		Layout->Call_OnUnregister();
	}
	if (IsValid(Visual))
	{
		Visual->Call_OnUnregister();
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


void ULexWidget::CalculateAnchorFromTransform()
{
	auto TempRelativeLocation = this->GetRelativeLocation();
	FVector2D CalculatedAnchoredPosition;
	if (UIParent.IsValid())
	{
		//just a reverse operation from CalculateTransformFromAnchor
		float LocalLeftPoint =
			UIParent->GetLocalSpaceLeft()
			+ (UIParent->GetWidth() * this->AnchorData.AnchorMin.X);

		float LocalBottomPoint =
			UIParent->GetLocalSpaceBottom()
			+ (UIParent->GetHeight() * this->AnchorData.AnchorMin.Y);

		CalculatedAnchoredPosition.X = TempRelativeLocation.Y
			- LocalLeftPoint
			- +(UIParent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X)) * this->AnchorData.Pivot.X;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z
			- LocalBottomPoint
			- (UIParent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y)) * this->AnchorData.Pivot.Y;
	}
	else
	{
		CalculatedAnchoredPosition.X = TempRelativeLocation.Y;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z;
	}

	bCacheAnchorLeftDirty = true;
	bCacheAnchorRightDirty = true;
	bCacheAnchorBottomDirty = true;
	bCacheAnchorTopDirty = true;

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
	if (UIParent.IsValid())
	{
		float LocalLeftPoint = //this left point anchor position in parent's space
			UIParent->GetLocalSpaceLeft()//parent's left position
			+ (UIParent->GetWidth() * this->AnchorData.AnchorMin.X);//add anchor offset
		float LocalLeftPivotPoint = //to pivot point, with anchor offset
			LocalLeftPoint
			+ (UIParent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X))//parent anchor width (width without SizeDelta)
				* this->AnchorData.Pivot.X
			+ this->AnchorData.AnchoredPosition.X;

		float LocalBottomPoint = //this bottom point anchor position in parent's space
			UIParent->GetLocalSpaceBottom()//parent's bottom position
			+ (UIParent->GetHeight() * this->AnchorData.AnchorMin.Y);//add anchor offset
		float LocalBottomPivotPoint = //to pivot point, with anchor offset
			LocalBottomPoint
			+ (UIParent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y))//parent anchor width (width without SizeDelta)
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
		GetRelativeLocation_DirectMutable() = ResultLocation;
		UpdateComponentToWorld();
	}
	bCanSetAnchorFromTransform = true;
}

#pragma region AnchorData

float ULexWidget::GetWidth() const
{
	if (bCacheWidthDirty)
	{
		bCacheWidthDirty = false;
		if (UIParent.IsValid())
		{
			if (AnchorData.IsHorizontalStretched())
			{
				CacheWidth = AnchorData.SizeDelta.X + UIParent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X);
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
		if (UIParent.IsValid())
		{
			if (AnchorData.IsVerticalStretched())
			{
				CacheHeight = AnchorData.SizeDelta.Y + UIParent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y);
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

	MarkAnchorDataChanged(true, true, true);
}

void ULexWidget::SetPivot(FVector2D Value) 
{
	if (!AnchorData.Pivot.Equals(Value, 0.0f))
	{
		AnchorData.Pivot = Value;
		MarkAnchorDataChanged(true, false, false);
	}
}

void ULexWidget::SetAnchorMin(FVector2D Value)
{
	if (this->UIParent.IsValid())
	{
		if (!AnchorData.AnchorMin.Equals(Value, 0.0f))
		{
			auto CurrentLeft = this->GetAnchorLeft();
			auto CurrentBottom = this->GetAnchorBottom();

			AnchorData.AnchorMin = Value;
			
			//SetAnchorLeft
			{
				auto CurrentRight = this->GetAnchorRight();
				CacheWidth = this->UIParent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - CurrentLeft;
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, this->AnchorData.Pivot.X);
			}

			//SetAnchorBottom
			{
				auto CurrentTop = this->GetAnchorTop();
				CacheHeight = this->UIParent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - CurrentBottom;
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, this->AnchorData.Pivot.Y);
			}

			MarkAnchorDataChanged(false, true, true);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}
void ULexWidget::SetAnchorMax(FVector2D Value)
{
	if (this->UIParent.IsValid())
	{
		if (!AnchorData.AnchorMax.Equals(Value, 0.0f))
		{
			auto CurrentRight = this->GetAnchorRight();
			auto CurrentTop = this->GetAnchorTop();

			AnchorData.AnchorMax = Value;

			//SetAnchorRight
			{
				auto CurrentLeft = this->GetAnchorLeft();
				CacheWidth = this->UIParent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - CurrentLeft;
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, this->AnchorData.Pivot.X);
			}
			//SetAnchorTop
			{
				auto CurrentBottom = this->GetAnchorBottom();
				CacheHeight = this->UIParent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - CurrentBottom;
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, this->AnchorData.Pivot.Y);
			}

			MarkAnchorDataChanged(false, true, true);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}

void ULexWidget::SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->UIParent.IsValid())
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
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}

void ULexWidget::SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->UIParent.IsValid())
	{
		if (AnchorData.AnchorMin.X != Value.X || AnchorData.AnchorMax.X != Value.Y)
		{
			auto CurrentLeft = this->GetAnchorLeft();
			auto CurrentRight = this->GetAnchorRight();

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
					CacheWidth = this->UIParent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - CurrentLeft;
				}
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, this->AnchorData.Pivot.X);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}

			MarkAnchorDataChanged(false, !bKeepSize, !bKeepSize);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}
void ULexWidget::SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->UIParent.IsValid())
	{
		if (AnchorData.AnchorMin.Y != Value.X || AnchorData.AnchorMax.Y != Value.Y)
		{
			auto CurrentBottom = this->GetAnchorBottom();
			auto CurrentTop = this->GetAnchorTop();

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
					CacheHeight = this->UIParent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - CurrentBottom;
				}
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, this->AnchorData.Pivot.Y);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}

			MarkAnchorDataChanged(false, !bKeepSize, !bKeepSize);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
#if !UE_BUILD_SHIPPING
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
	}
}

void ULexWidget::SetAnchoredPosition(FVector2D Value)
{
	if (!AnchorData.AnchoredPosition.Equals(Value, 0.0f))
	{
		AnchorData.AnchoredPosition = Value;
		MarkAnchorDataChanged(false, false, false);
	}
}

void ULexWidget::SetHorizontalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.X != Value)
	{
		AnchorData.AnchoredPosition.X = Value;
		MarkAnchorDataChanged(false, false, false);
	}
}
void ULexWidget::SetVerticalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.Y != Value)
	{
		AnchorData.AnchoredPosition.Y = Value;
		MarkAnchorDataChanged(false, false, false);
	}
}

void ULexWidget::SetSizeDelta(FVector2D Value)
{
	if (!AnchorData.SizeDelta.Equals(Value, 0.0f))
	{
		AnchorData.SizeDelta = Value;
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
		MarkAnchorDataChanged(false, true, true);
	}
}

float ULexWidget::GetAnchorLeft()const
{
	if (bCacheAnchorLeftDirty)
	{
		bCacheAnchorLeftDirty = false;
		if (this->UIParent.IsValid())
		{
			CacheAnchorLeft =
				this->GetLocalSpaceLeft()//local space left
				+ this->GetRelativeLocation().Y//convert to parent space
				-
				(this->UIParent->GetLocalSpaceLeft()//parent space left
					+ this->UIParent->GetWidth() * this->AnchorData.AnchorMin.X)//to parent anchor min point
				;
		}
		else
		{
			CacheAnchorLeft = this->GetLocalSpaceLeft();//local space left
		}
	}
	return CacheAnchorLeft;
}
float ULexWidget::GetAnchorTop()const
{
	if (bCacheAnchorTopDirty)
	{
		bCacheAnchorTopDirty = false;
		if (this->UIParent.IsValid())
		{
			CacheAnchorTop =
				-(
					this->GetLocalSpaceTop()
					+ this->GetRelativeLocation().Z
					-
					(this->UIParent->GetLocalSpaceTop()
						- this->UIParent->GetHeight() * (1.0f - this->AnchorData.AnchorMax.Y))
					)
				;
		}
		else
		{
			CacheAnchorTop = this->GetLocalSpaceTop();
		}
	}
	return CacheAnchorTop;
}
float ULexWidget::GetAnchorRight()const
{
	if (bCacheAnchorRightDirty)
	{
		bCacheAnchorRightDirty = false;
		if (this->UIParent.IsValid())
		{
			CacheAnchorRight =
				-(
					this->GetLocalSpaceRight()
					+ this->GetRelativeLocation().Y
					-
					(this->UIParent->GetLocalSpaceRight()
						- this->UIParent->GetWidth() * (1.0f - this->AnchorData.AnchorMax.X))
					)
				;
		}
		else
		{
			CacheAnchorRight = this->GetLocalSpaceRight();
		}
	}
	return CacheAnchorRight;
}
float ULexWidget::GetAnchorBottom()const
{
	if (bCacheAnchorBottomDirty)
	{
		bCacheAnchorBottomDirty = false;
		if (this->UIParent.IsValid())
		{
			CacheAnchorBottom =
				this->GetLocalSpaceBottom()
				+ this->GetRelativeLocation().Z
				-
				(this->UIParent->GetLocalSpaceBottom()
					+ this->UIParent->GetHeight() * this->AnchorData.AnchorMin.Y)
				;
		}
		else
		{
			CacheAnchorBottom = this->GetLocalSpaceBottom();
		}
	}
	return CacheAnchorBottom;
}

void ULexWidget::SetAnchorLeft(float Value)
{
	if (this->UIParent.IsValid())
	{
		if (CacheAnchorLeft != Value || bCacheAnchorLeftDirty)
		{
			bCacheAnchorLeftDirty = false;
			CacheAnchorLeft = Value;
			auto CurrentRight = this->GetAnchorRight();
			CacheWidth = this->UIParent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - Value;
			//SetWdith
			{
				if (AnchorData.IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					AnchorData.SizeDelta.X = CacheWidth;
				}
			}
			this->AnchorData.AnchoredPosition.X = FMath::Lerp(Value, -CurrentRight, this->AnchorData.Pivot.X);
			MarkAnchorDataChanged(false, true, false);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexWidget::SetAnchorTop(float Value)
{
	if (this->UIParent.IsValid())
	{
		if (CacheAnchorTop != Value || bCacheAnchorTopDirty)
		{
			bCacheAnchorTopDirty = false;
			CacheAnchorTop = Value;
			auto CurrentBottom = this->GetAnchorBottom();
			CacheHeight = this->UIParent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - Value - CurrentBottom;
			//SetHeight
			{
				if (AnchorData.IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					AnchorData.SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -Value, this->AnchorData.Pivot.Y);
			MarkAnchorDataChanged(false, false, true);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexWidget::SetAnchorRight(float Value)
{
	if (this->UIParent.IsValid())
	{
		if (CacheAnchorRight != Value || bCacheAnchorRightDirty)
		{
			bCacheAnchorRightDirty = false;
			CacheAnchorRight = Value;
			auto CurrentLeft = this->GetAnchorLeft();
			CacheWidth = this->UIParent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - Value - CurrentLeft;
			//SetWdith
			{
				if (AnchorData.IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (UIParent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					AnchorData.SizeDelta.X = CacheWidth;
				}
			}
			this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -Value, this->AnchorData.Pivot.X);
			MarkAnchorDataChanged(false, true, false);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void ULexWidget::SetAnchorBottom(float Value)
{
	if (this->UIParent.IsValid())
	{
		if (CacheAnchorBottom != Value || bCacheAnchorBottomDirty)
		{
			bCacheAnchorBottomDirty = false;
			CacheAnchorBottom = Value;
			auto CurrentTop = this->GetAnchorTop();
			CacheHeight = this->UIParent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - Value;
			//SetHeight
			{
				if (AnchorData.IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (UIParent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					AnchorData.SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchorData.AnchoredPosition.Y = FMath::Lerp(Value, -CurrentTop, this->AnchorData.Pivot.Y);
			MarkAnchorDataChanged(false, false, true);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

void ULexWidget::SetWidth(float Value)
{
	if (CacheWidth != Value || bCacheWidthDirty)
	{
		bCacheWidthDirty = false;
		CacheWidth = Value;
		if (UIParent.IsValid())
		{
			if (AnchorData.IsHorizontalStretched())
			{
				auto CalculatedSizeDeltaX = Value - (UIParent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
				if (AnchorData.SizeDelta.X != CalculatedSizeDeltaX)
				{
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
					MarkAnchorDataChanged(false, true, false);
				}
			}
			else
			{
				if (AnchorData.SizeDelta.X != Value)
				{
					AnchorData.SizeDelta.X = Value;
					MarkAnchorDataChanged(false, true, false);
				}
			}
		}
		else
		{
			if (AnchorData.SizeDelta.X != Value)
			{
				AnchorData.SizeDelta.X = Value;
				MarkAnchorDataChanged(false, true, false);
			}
		}
		bCacheWidthDirty = false;//this maybe set dirty by MarkAnchorDataChanged, but it is already calculated, so make it not dirty again
	}
}
void ULexWidget::SetHeight(float Value)
{
	if (CacheHeight != Value || bCacheHeightDirty)
	{
		bCacheHeightDirty = false;
		CacheHeight = Value;
		if (UIParent.IsValid())
		{
			if (AnchorData.IsVerticalStretched())
			{
				auto CalculatedSizeDeltaY = Value - (UIParent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
				if (AnchorData.SizeDelta.Y != CalculatedSizeDeltaY)
				{
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
					MarkAnchorDataChanged(false, false, true);
				}
			}
			else
			{
				if (AnchorData.SizeDelta.Y != Value)
				{
					AnchorData.SizeDelta.Y = Value;
					MarkAnchorDataChanged(false, false, true);
				}
			}
		}
		else
		{
			if (AnchorData.SizeDelta.Y != Value)
			{
				AnchorData.SizeDelta.Y = Value;
				MarkAnchorDataChanged(false, false, true);
			}
		}
		bCacheHeightDirty = false;//this maybe set dirty by MarkAnchorDataChanged, but it is already calculated, so make it not dirty again
	}
}

#pragma endregion

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
		MarkAnchorDataChanged(false, true, true);
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
	if (ClipData.IsValid() && ClipData.Pin()->GetWidget() == this)
	{
		ClipData.Pin()->MarkNeedUpdateData();
	}

	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);
	if (IsValid(Layout))
	{
		MarkLayoutDirty();
		Layout->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}
	if (GetLayoutSlot())
	{
		MarkLayoutDirty();
		LayoutSlot->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
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

void ULexWidget::MarkAnchorDataChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	CalculateTransformFromAnchor();
	
	if (InWidthChanged)
	{
		bCacheWidthDirty = true;
	}
	if (InHeightChanged)
	{
		bCacheHeightDirty = true;
	}
	bCacheAnchorLeftDirty = true;
	bCacheAnchorRightDirty = true;
	bCacheAnchorBottomDirty = true;
	bCacheAnchorTopDirty = true;
			
	MarkDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	if (IsValid(Layout))
	{
		for (auto& Child : GetUIChildren())
		{
			Child->MarkAnchorDataChanged(InPivotChanged, InWidthChanged, InHeightChanged);
		}
	}
	else
	{
		if (InWidthChanged || InHeightChanged)
		{
			for (auto& Child : GetUIChildren())
			{
				if (Child->AnchorData.IsHorizontalStretched() || Child->AnchorData.IsVerticalStretched())
				{
					Child->MarkAnchorDataChanged(false, true, true);
				}
			}
		}
	}
}

void ULexWidget::MarkCanvasUpdate(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall)const
{
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->MarkCanvasUpdate(bMaterialOrTextureChanged, bTransformOrVertexPositionChanged, bHierarchyOrderChanged, bForceRebuildDrawCall);
	}
}

float ULexWidget::GetMinWidth() const
{
	return GetLayoutProperty(&ULexLayoutSlot::GetMinWidth, &ULexLayout::GetMinWidth, &ULexVisual::GetMinWidth, 0);
}

float ULexWidget::GetPreferredWidth() const
{
	return GetLayoutProperty(&ULexLayoutSlot::GetPreferredWidth, &ULexLayout::GetPreferredWidth, &ULexVisual::GetPreferredWidth, 0);
}

float ULexWidget::GetFlexibleWidth() const
{
	return GetLayoutProperty(&ULexLayoutSlot::GetFlexibleWidth, &ULexLayout::GetFlexibleWidth, &ULexVisual::GetFlexibleWidth, 0);
}

float ULexWidget::GetMinHeight() const
{
	return GetLayoutProperty(&ULexLayoutSlot::GetMinHeight, &ULexLayout::GetMinHeight, &ULexVisual::GetMinHeight, 0);
}

float ULexWidget::GetPreferredHeight() const
{
	return GetLayoutProperty(&ULexLayoutSlot::GetPreferredHeight, &ULexLayout::GetPreferredHeight, &ULexVisual::GetPreferredHeight, 0);
}

float ULexWidget::GetFlexibleHeight() const
{
	return GetLayoutProperty(&ULexLayoutSlot::GetFlexibleHeight, &ULexLayout::GetFlexibleHeight, &ULexVisual::GetFlexibleHeight, 0);
}

UObject* ULexWidget::GetMinWidthSource() const
{
	return GetLayoutSource(&ULexLayoutSlot::GetMinWidth, &ULexLayout::GetMinWidth, &ULexVisual::GetMinWidth);
}

UObject* ULexWidget::GetPreferredWidthSource() const
{
	return GetLayoutSource(&ULexLayoutSlot::GetPreferredWidth, &ULexLayout::GetPreferredWidth, &ULexVisual::GetPreferredWidth);
}

UObject* ULexWidget::GetFlexibleWidthSource() const
{
	return GetLayoutSource(&ULexLayoutSlot::GetFlexibleWidth, &ULexLayout::GetFlexibleWidth, &ULexVisual::GetFlexibleWidth);
}

UObject* ULexWidget::GetMinHeightSource() const
{
	return GetLayoutSource(&ULexLayoutSlot::GetMinHeight, &ULexLayout::GetMinHeight, &ULexVisual::GetMinHeight);
}

UObject* ULexWidget::GetPreferredHeightSource() const
{
	return GetLayoutSource(&ULexLayoutSlot::GetPreferredHeight, &ULexLayout::GetPreferredHeight, &ULexVisual::GetPreferredHeight);
}

UObject* ULexWidget::GetFlexibleHeightSource() const
{
	return GetLayoutSource(&ULexLayoutSlot::GetFlexibleHeight, &ULexLayout::GetFlexibleHeight, &ULexVisual::GetFlexibleHeight);
}

float ULexWidget::GetLayoutProperty(const TFunction<float(ULexLayoutSlot*)>& GetLayoutSlotProperty,
                                    const TFunction<float(ULexLayout*)>& GetLayoutProperty,
                                    const TFunction<float(ULexVisual*)>& GetVisualProperty,
                                    float DefaultValue)const
{
	if (IsValid(LayoutSlot))
	{
		auto Value = GetLayoutSlotProperty(LayoutSlot);
		if (Value >= 0)//enable override
		{
			return Value;
		}
	}
	if (IsValid(Layout))
	{
		auto Value = GetLayoutProperty(Layout);
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
UObject* ULexWidget::GetLayoutSource(const TFunction<float(ULexLayoutSlot*)>& GetLayoutSlotProperty,
	const TFunction<float(ULexLayout*)>& GetLayoutProperty,
	const TFunction<float(ULexVisual*)>& GetVisualProperty) const
{
	if (IsValid(LayoutSlot))
	{
		auto Value = GetLayoutSlotProperty(LayoutSlot);
		if (Value >= 0)//enable override
		{
			return LayoutSlot;
		}
	}
	if (IsValid(Layout))
	{
		auto Value = GetLayoutProperty(Layout);
		if (Value >= 0)//enable override
		{
			return Layout;
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
	auto TargetWidget = InWidget;
	//move up, find if parent widget affect by layout then mark dirty
	while (TargetWidget)
	{
		TargetWidget->MarkLayoutDirty();
		if (auto ParentWidget = TargetWidget->GetUIParent())
		{
			if (auto ParentLayout = ParentWidget->GetLayout())
			{
				FLexLayoutControlAnchorData ControlChildAnchor;
				ParentLayout->GetLayoutControlAnchor(TargetWidget, ControlChildAnchor);
				FLexLayoutControlAnchorData ControlSelfAnchor;
				ParentLayout->GetLayoutControlAnchor(ParentWidget, ControlSelfAnchor);
				// if (ControlSelfAnchor.AnyControl() && !ControlChildAnchor.AnyControl())//if can only control self anchor then no need to move up 
				// {
				// 	break;
				// }
				if (ControlSelfAnchor.AnyControl() && ControlChildAnchor.AnyControl())//parent layout can control itself and children, then move up 
				{
					TargetWidget = ParentWidget;
					continue;
				}
				if (ControlChildAnchor.AnyControl())//parent layout only control children, then stop here
				{
					ParentWidget->MarkLayoutDirty();
					break;
				}
			}
		}
		break;
	}
	//
}

void ULexWidget::MarkLayoutDirty()const
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
	
	NewVisual->Call_OnRegister();
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
		OldLayout->Call_OnUnregister();
	}
	
	NewLayout->Call_OnRegister();
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
	if (!IsVisibleForLayout())
	{
		ClearLayoutSlot();
		return nullptr;
	}
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
	return LayoutSlot;
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
	BoxElem->Y = Parent->GetWidth();
	BoxElem->Z = Parent->GetHeight();

	BoxElem->Center = Origin;
}
FBoxSphereBounds ULexWidgetEditorHelperComp::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!IsValid(Parent))return FBoxSphereBounds(EForceInit::ForceInit);
	auto Center = Parent->GetLocalSpaceCenter();
	auto Origin = FVector(0, Center.X, Center.Y);
	return FBoxSphereBounds(Origin, FVector(1, Parent->GetWidth() * 0.5f, Parent->GetHeight() * 0.5f), (Parent->GetWidth() > Parent->GetHeight() ? Parent->GetWidth() : Parent->GetHeight()) * 0.5f).TransformBy(LocalToWorld);
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif
