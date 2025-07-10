// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/Components/LexWidget.h"
#include "LGUI.h"
#include "LGUI/Public/Core/Components/LexCanvas.h"
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
#if WITH_EDITOR
	bUIActiveStateDirty = true;
#endif

	bIsCanvasUIItem = false;
}

void ULexWidget::BeginPlay()
{
	Super::BeginPlay();
	if (IsValid(Visual))
	{
		Visual->BeginPlay();
	}
}

void ULexWidget::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	if (IsValid(Visual))
	{
		Visual->EndPlay();
	}
}

#pragma region LGUILifeCycleUIBehaviour
void ULexWidget::CallUILifeCycleBehavioursActiveInHierarchyStateChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	bool TempIsUIActive = GetIsUIActiveInHierarchy();
	if (this->GetOwner()->GetRootComponent() != this)return;
	if (UIActiveInHierarchyStateChangedDelegate.IsBound())UIActiveInHierarchyStateChangedDelegate.Broadcast(TempIsUIActive);
}
void ULexWidget::CallUILifeCycleBehavioursChildDimensionsChanged(ULexWidget* child, bool horizontalPositionChanged, bool verticalPositionChanged, bool widthChanged, bool heightChanged)
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	if (this->GetOwner()->GetRootComponent() != this)return;
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())
	{
		GetOwner()->GetComponents(LGUILifeCycleUIBehaviourArray, false);
	}
#endif
	for (int i = 0; i < LGUILifeCycleUIBehaviourArray.Num(); i++)
	{
		auto& CompItem = LGUILifeCycleUIBehaviourArray[i];
		CompItem->Call_OnUIChildDimensionsChanged(child, horizontalPositionChanged, verticalPositionChanged, widthChanged, heightChanged);
	}
}
void ULexWidget::CallUILifeCycleBehavioursChildActiveInHierarchyStateChanged(ULexWidget* child, bool activeOrInactive)
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	if (this->GetOwner()->GetRootComponent() != this)return;
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())
	{
		GetOwner()->GetComponents(LGUILifeCycleUIBehaviourArray, false);
	}
#endif
	for (int i = 0; i < LGUILifeCycleUIBehaviourArray.Num(); i++)
	{
		auto& CompItem = LGUILifeCycleUIBehaviourArray[i];
		CompItem->Call_OnUIChildAcitveInHierarchy(child, activeOrInactive);
	}
}
void ULexWidget::CallUILifeCycleBehavioursDimensionsChanged(bool horizontalPositionChanged, bool verticalPositionChanged, bool widthChanged, bool heightChanged)
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	if (this->GetOwner()->GetRootComponent() != this)return;
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())
	{
		GetOwner()->GetComponents(LGUILifeCycleUIBehaviourArray, false);
	}
#endif
	for (int i = 0; i < LGUILifeCycleUIBehaviourArray.Num(); i++)
	{
		auto& CompItem = LGUILifeCycleUIBehaviourArray[i];
		CompItem->Call_OnUIDimensionsChanged(horizontalPositionChanged, verticalPositionChanged, widthChanged, heightChanged);
	}

	//call parent
	if (UIParent.IsValid())
	{
		UIParent->CallUILifeCycleBehavioursChildDimensionsChanged(this, horizontalPositionChanged, verticalPositionChanged, widthChanged, heightChanged);
	}
}
void ULexWidget::CallUILifeCycleBehavioursAttachmentChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	if (this->GetOwner()->GetRootComponent() != this)return;
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())
	{
		GetOwner()->GetComponents(LGUILifeCycleUIBehaviourArray, false);
	}
#endif
	for (int i = 0; i < LGUILifeCycleUIBehaviourArray.Num(); i++)
	{
		auto& CompItem = LGUILifeCycleUIBehaviourArray[i];
		CompItem->Call_OnUIAttachmentChanged();
	}
}
void ULexWidget::CallUILifeCycleBehavioursChildAttachmentChanged(ULexWidget* child, bool attachOrDettach)
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	if (this->GetOwner()->GetRootComponent() != this)return;
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())
	{
		GetOwner()->GetComponents(LGUILifeCycleUIBehaviourArray, false);
	}
#endif
	for (int i = 0; i < LGUILifeCycleUIBehaviourArray.Num(); i++)
	{
		auto& CompItem = LGUILifeCycleUIBehaviourArray[i];
		CompItem->Call_OnUIChildAttachmentChanged(child, attachOrDettach);
	}
}
void ULexWidget::CallUILifeCycleBehavioursInteractionStateChanged()
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	if (this->GetOwner()->GetRootComponent() != this)return;
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())
	{
		GetOwner()->GetComponents(LGUILifeCycleUIBehaviourArray, false);
	}
#endif
	for (int i = 0; i < LGUILifeCycleUIBehaviourArray.Num(); i++)
	{
		auto& CompItem = LGUILifeCycleUIBehaviourArray[i];
		CompItem->Call_OnUIInteractionStateChanged(GetFinalIsEnabled());
	}
}
void ULexWidget::CallUILifeCycleBehavioursChildHierarchyIndexChanged(ULexWidget* child)
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	if (this->GetOwner()->GetRootComponent() != this)return;
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())
	{
		GetOwner()->GetComponents(LGUILifeCycleUIBehaviourArray, false);
	}
#endif
	if (IsValid(Layout))
	{
		Layout->MarkLayoutDirty();
	}
	for (int i = 0; i < LGUILifeCycleUIBehaviourArray.Num(); i++)
	{
		auto& CompItem = LGUILifeCycleUIBehaviourArray[i];
		CompItem->Call_OnUIChildHierarchyIndexChanged(child);
	}
}
#pragma endregion LGUILifeCycleUIBehaviour


void ULexWidget::CalculateFlattenHierarchyIndex_Recursive(int& index)const
{
	if (this->flattenHierarchyIndex != index)
	{
		this->flattenHierarchyIndex = index;
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

DECLARE_CYCLE_STAT(TEXT("UIItem CalculateFlattenHierarchyIndex"), STAT_UIItemCalculateFlattenHierarchyIndex, STATGROUP_LGUI);
void ULexWidget::RecalculateFlattenHierarchyIndex()const
{
	SCOPE_CYCLE_COUNTER(STAT_UIItemCalculateFlattenHierarchyIndex);

	this->bFlattenHierarchyIndexDirty = false;
	int tempIndex = this->flattenHierarchyIndex;
	this->CalculateFlattenHierarchyIndex_Recursive(tempIndex);
}

int32 ULexWidget::GetFlattenHierarchyIndex()const
{
	if (RootUIItem.IsValid())
	{
		if (RootUIItem->bFlattenHierarchyIndexDirty)
		{
			RootUIItem->RecalculateFlattenHierarchyIndex();
		}
	}
	return this->flattenHierarchyIndex;
}

void ULexWidget::MarkFlattenHierarchyIndexDirty()
{
	if (RootUIItem.IsValid())
	{
		RootUIItem->bFlattenHierarchyIndexDirty = true;
	}
	//tell canvas to update
	if (RenderCanvas.IsValid()
		&& RenderCanvas->IsRegistered()//@todo: why need to check IsRegistered? the only way to set RenderCanvas is SetRenderCanvas function, but when I debug on SetRenderCanvas it's not called at all, so RenderCanvas should not valid here, no clue yet
		)
	{
		RenderCanvas->MarkCanvasUpdate(false, false, true);
		//if this UIItem have a LGUICanvas, then we need to tell the upper canvas that hierarchy order change, in order to sort render order between canvas
		if (this->bIsCanvasUIItem)
		{
			if (RenderCanvas->GetParentCanvas().IsValid())
			{
				RenderCanvas->GetParentCanvas()->MarkCanvasUpdate(false, false, true);
			}
		}
	}
}

void ULexWidget::SetHierarchyIndex(int32 InInt) 
{ 
	if (InInt != HierarchyIndex)
	{
		HierarchyIndex = InInt;
		ApplyHierarchyIndex();
	}
}

void ULexWidget::ApplyHierarchyIndex()
{
	if (UIParent.IsValid())
	{
		if (UIParent->UIChildren.Num() == 0)
		{
			UIParent->UIChildren.Add(this);
			this->HierarchyIndex = 0;
			UIParent->CallUILifeCycleBehavioursChildHierarchyIndexChanged(this);
		}
		else
		{
			UIParent->EnsureUIChildrenValid();
			UIParent->EnsureUIChildrenSorted();
			HierarchyIndex = FMath::Clamp(HierarchyIndex, 0, UIParent->UIChildren.Num() - 1);
			UIParent->UIChildren.Remove(this);
			UIParent->UIChildren.Insert(this, HierarchyIndex);
			bool anythingChange = false;
			for (int i = 0; i < UIParent->UIChildren.Num(); i++)
			{
				if (UIParent->UIChildren[i]->HierarchyIndex != i)
				{
					UIParent->UIChildren[i]->HierarchyIndex = i;
					anythingChange = true;
				}
			}
			//flatten hierarchy index
			if (anythingChange)
			{
				MarkFlattenHierarchyIndexDirty();
				UIParent->CallUILifeCycleBehavioursChildHierarchyIndexChanged(this);
			}
		}
	}
	else
	{
		HierarchyIndex = 0;
	}
}

void ULexWidget::SetAsFirstHierarchy()
{
	SetHierarchyIndex(0);
}
void ULexWidget::SetAsLastHierarchy()
{
	if (UIParent.IsValid())
	{
		SetHierarchyIndex(UIParent->UIChildren.Num() - 1);
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
#if WITH_EDITOR
	bUIActiveStateDirty = true;
#endif
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
		static const FName WidthName = GET_MEMBER_NAME_CHECKED(ULexWidget, Width);
		static const FName HeightName = GET_MEMBER_NAME_CHECKED(ULexWidget, Height);
		static const FName VisibilityName = GET_MEMBER_NAME_CHECKED(ULexWidget, WidgetVisibility);
		static const FName ClippingName = GET_MEMBER_NAME_CHECKED(ULexWidget, Clipping);
		static const FName VisualName = GET_MEMBER_NAME_CHECKED(ULexWidget, Visual);

		if (MemberName == AspectRatioName
		|| MemberName == WidthName
		|| MemberName == HeightName
		|| MemberName == VisibilityName
		)
		{
			this->MarkSizeDirty_Recursive();
			this->MarkClipDirty_Recursive(false);
		}
		else if (PropertyName == ClippingName)
		{
			MarkClipDirty_Recursive(true);
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexWidget, bIsUIActive))
		{
			bIsUIActive = !bIsUIActive;//make it work
			SetIsUIActive(!bIsUIActive);
		}

		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexWidget, HierarchyIndex))
		{
			ApplyHierarchyIndex();
		}
		else if (PropertyName == FName(TEXT("RelativeLocation")))
		{
			OnTransformChanged.Broadcast();
			UpdateComponentToWorld();
		}
		else if (PropertyName == VisualName)
		{
			
		}
		EditorForceUpdate();
		UpdateBounds();
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
		}
	}
}

bool ULexWidget::CanEditChange(const FProperty* InProperty) const
{
	bool bIsEditable = Super::CanEditChange(InProperty);
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

//void UUIItem::PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation)
//{
//	Super::PostEditUndo(TransactionAnnotation);
//	EditorForceUpdate();
//}

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
	check(this == RootUIItem);
	struct LOCAL
	{
		static void RenewRenderCanvas(ULexWidget* UIItem)
		{
			auto ThisRenderCanvas = UIItem->GetOwner()->FindComponentByClass<ULexCanvas>();
			UIItem->RenewRenderCanvasRecursive(ThisRenderCanvas);
		}
		static void EnsureDataForRebuildRecursive(ULexWidget* UIItem)
		{
			UIItem->EnsureUIChildrenValid();
			UIItem->bNeedSortUIChildren = true;
			UIItem->EnsureUIChildrenSorted();
			if (UIItem->bIsCanvasUIItem && UIItem->RenderCanvas.IsValid())
			{
				UIItem->RenderCanvas->EnsureDataForRebuild();
			}

			for (auto& uiChild : UIItem->UIChildren)
			{
				if (IsValid(uiChild))
				{
					EnsureDataForRebuildRecursive(uiChild);
				}
			}
		}
		/** force refresh render canvas, remove from old and add to new */
		static void ForceRefreshRenderCanvasRecursive(ULexWidget* UIItem)
		{
			auto NewRenderCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(UIItem->GetOwner(), false);
			UIItem->SetRenderCanvas(NewRenderCanvas);

			for (auto& uiChild : UIItem->UIChildren)
			{
				if (IsValid(uiChild))
				{
					ForceRefreshRenderCanvasRecursive(uiChild);
				}
			}
		}
		static void ForceRefreshUIActiveStateRecursive(ULexWidget* UIItem)
		{
			if (UIItem->bUIActiveStateDirty)
			{
				UIItem->bUIActiveStateDirty = false;

				UIItem->ApplyUIActiveState(true);
				//affect children
				UIItem->CheckChildrenUIActiveRecursive(UIItem->GetIsUIActiveInHierarchy());
				//callback for parent
				if (UIItem->UIParent.IsValid())
				{
					UIItem->UIParent->OnChildActiveStateChanged(UIItem);
				}
			}

			for (auto& uiChild : UIItem->UIChildren)
			{
				if (IsValid(uiChild))
				{
					ForceRefreshUIActiveStateRecursive(uiChild);
				}
			}
		}
		static void UpdateComponentToWorldRecursive(ULexWidget* UIItem)
		{
			if (!IsValid(UIItem))return;
			UIItem->UpdateComponentToWorld();
			auto& Children = UIItem->GetUIChildren();
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
	LOCAL::ForceRefreshUIActiveStateRecursive(this);
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

	if (this->IsCanvasUIItem() && this->RenderCanvas.IsValid())
	{
		//This is mainly to mark LGUICanvas's bIsViewProjectionMatrixDirty to true.
		//For the condition LGUI_Tutorials/Tutorials/UIRenderTarget, when move LGUIRenderTarget1 at runtime, the LGUICanvas's RenderTarget's matrix not update, result in wrong interaction.
		this->RenderCanvas->MarkSizeChanged();
	}
	MarkTransformChanged(true, true);
	OnTransformChanged.Broadcast();
	if (IsValid(Layout))
	{
		Layout->OnTransformChanged();
	}
	if (CheckAndGetLayoutSlot())
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
	if (ULexWidget* childUIItem = Cast<ULexWidget>(ChildComponent))
	{
		childUIItem->OnUIAttachedToParent();

		EnsureUIChildrenValid();//check
		UIChildren.Add(childUIItem);
		
		auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
		if (PrefabManager && PrefabManager->IsPrefabSystemProcessingActor(this->GetOwner()))//load from prefab or duplicated by LGUI PrefabSystem, then not set hierarchy index
		{
			//if is load from prefab system, then we don't need to sort children, because children is already sorted when save prefab
		}
		else
		{
			//need sort children here, make it true so we can sort children if we need to
			bNeedSortUIChildren = true;

			if (childUIItem->IsRegistered())
			{
				childUIItem->HierarchyIndex = UIChildren.Num() - 1;
				this->CallUILifeCycleBehavioursChildHierarchyIndexChanged(childUIItem);
			}
			else//not registered means is loading from level. then no need to set hierarchy index
			{

			}
		}

		//make sure hierarchyindex all good
		if (childUIItem->HierarchyIndex == INDEX_NONE)
		{
			for (int i = 0; i < UIChildren.Num(); i++)
			{
				auto& UIChild = UIChildren[i];
				if (UIChild->HierarchyIndex != i)
				{
					UIChild->HierarchyIndex = i;
					this->CallUILifeCycleBehavioursChildHierarchyIndexChanged(UIChild);
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
		UIParent = Cast<ULexWidget>(this->GetAttachParent());
		check(UIParent.IsValid());
		{
			if (auto LGUIManager = ULGUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
			{
				LGUIManager->AddFunctionForPrefabSystemExecutionBeforeAwake(this->GetOwner(), [Child = MakeWeakObjectPtr(this), Parent = UIParent]() {
					if (Child.IsValid() && Parent.IsValid())
					{
						Parent->CallUILifeCycleBehavioursChildAttachmentChanged(Child.Get(), true);
					}});
			}
		}
	}
	else
	{
		UIParent = Cast<ULexWidget>(this->GetAttachParent());
		check(UIParent.IsValid());
		{
			UIParent->CallUILifeCycleBehavioursChildAttachmentChanged(this, true);
		}

		if (this->IsRegistered())//not registered means is loading from level.
		{
			OnTransformChanged.Broadcast();
			//this->CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
		}
	}

	ULexCanvas* ParentCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(GetOwner()->GetAttachParentActor(), false);
	UIHierarchyChanged(ParentCanvas, UIParent->RootUIItem.Get());
	if (IsValid(Layout))
	{
		Layout->MarkLayoutDirty();
	}
	MarkClipDirty_Recursive(true);
	//callback
	CallUILifeCycleBehavioursAttachmentChanged();
}

void ULexWidget::OnChildDetached(USceneComponent* ChildComponent)
{
	Super::OnChildDetached(ChildComponent);
	if (!IsValid(this) || this->IsUnreachable())return;
	if (GetWorld() == nullptr)return;

	if (auto childUIItem = Cast<ULexWidget>(ChildComponent))
	{
		childUIItem->bIsDetaching = true;
		//hierarchy index
		EnsureUIChildrenValid();
		UIChildren.Remove(childUIItem);
		for (int i = 0; i < UIChildren.Num(); i++)
		{
			auto& UIChild = UIChildren[i];
			if (UIChild->HierarchyIndex != i)
			{
				UIChild->HierarchyIndex = i;
				this->CallUILifeCycleBehavioursChildHierarchyIndexChanged(UIChild);
			}
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
		if (UIParent.IsValid())//tell old parent
		{
			if (auto LGUIManager = ULGUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
			{
				LGUIManager->AddFunctionForPrefabSystemExecutionBeforeAwake(this->GetOwner(), [Child = MakeWeakObjectPtr(this), Parent = UIParent]() {
					if (Child.IsValid() && Parent.IsValid())
					{
						Parent->CallUILifeCycleBehavioursChildAttachmentChanged(Child.Get(), false);
					}});
			}
		}
	}
	else
	{
		if (UIParent.IsValid())//tell old parent
		{
			UIParent->CallUILifeCycleBehavioursChildAttachmentChanged(this, false);
		}

		if (this->IsRegistered())//not registered means is loading from level.
		{
			OnTransformChanged.Broadcast();
			//this->CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
		}
	}

	UIHierarchyChanged(nullptr, nullptr);
	if (IsValid(Layout))
	{
		Layout->MarkLayoutDirty();
	}
	MarkClipDirty_Recursive(true);
	//callback
	CallUILifeCycleBehavioursAttachmentChanged();
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
					HelperComp = NewObject<UUIItemEditorHelperComp>(GetOwner(), NAME_None, RF_Transient | RF_TextExportTransient);
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
	if (auto ownerActor = GetOwner())
	{
		if (!GetIsUIActiveInHierarchy())
		{
			ownerActor->SetIsTemporarilyHiddenInEditor(true);
		}
	}
#endif

	CheckRootUIItem();

	if (IsValid(Layout))
	{
		Layout->OnRegister();
	}
	if (CheckAndGetLayoutSlot())
	{
		LayoutSlot->OnRegister();
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
	CheckRootUIItem();

	if (IsValid(Layout))
	{
		Layout->OnUnregister();
	}
	if (CheckAndGetLayoutSlot())
	{
		LayoutSlot->OnRegister();
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
				if (A.GetHierarchyIndex() < B.GetHierarchyIndex())
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
	bIsCanvasUIItem = true;
	auto ParentCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(GetOwner()->GetAttachParentActor(), false);//@todo: replace with Canvas's ParentCanvas?
	if (RenderCanvas != InRenderCanvas)
	{
		SetRenderCanvas(InRenderCanvas);
	}
	InRenderCanvas->SetParentCanvas(ParentCanvas);
	for (auto& uiItem : UIChildren)
	{
		if (IsValid(uiItem))
		{
			uiItem->RenewRenderCanvasRecursive(InRenderCanvas);
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

	for (auto& uiItem : UIChildren)
	{
		if (IsValid(uiItem))
		{
			uiItem->RenewRenderCanvasRecursive(InParentRenderCanvas);
		}
	}
}

void ULexWidget::UnregisterRenderCanvas()
{
	bIsCanvasUIItem = false;
	auto ParentCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(GetOwner()->GetAttachParentActor(), false);
	if (RenderCanvas.IsValid())
	{
		RenderCanvas->SetParentCanvas(nullptr);
	}
	if (RenderCanvas != ParentCanvas)
	{
		SetRenderCanvas(ParentCanvas);
	}
	for (auto& uiItem : UIChildren)
	{
		if (IsValid(uiItem))
		{
			uiItem->RenewRenderCanvasRecursive(ParentCanvas);
		}
	}
}

void ULexWidget::UpdateLayout()
{
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
	if (IsValid(Visual))
	{
		Visual->OnClipDataChanged();
	}
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

void ULexWidget::UIHierarchyChanged(ULexCanvas* ParentRenderCanvas, ULexWidget* ParentRoot)
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

	if (UIHierarchyChangedDelegate.IsBound())
	{
		UIHierarchyChangedDelegate.Broadcast();
	}

	CheckRootUIItem(ParentRoot);
	for (auto& uiItem : UIChildren)
	{
		if (IsValid(uiItem))
		{
			uiItem->UIHierarchyChanged(ParentRenderCanvas, ParentRoot);
		}
	}

	//flatten hierarchy index
	MarkFlattenHierarchyIndexDirty();

	if (UIParent.IsValid())
	{
		//active state
		this->bAllUpParentUIActive = UIParent->GetIsUIActiveInHierarchy();
		this->CheckUIActiveState();
	}
	else
	{
		this->bAllUpParentUIActive = true;
		this->CheckUIActiveState();
	}

	//if (this->IsRegistered())//not registerd means could be load from level
	{
		MarkDimensionChanged(false, true, true);
	}
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
	bool LayoutSlotValid = CheckAndGetLayoutSlot() != nullptr;
	if (!LayoutSlotValid || !LayoutSlot->GetLayoutControlWidth())
	{
		switch (Width.Type)
		{
		case ELexWidgetSizeType::Fixed:
			RenderSize.X = Width.Value;
			break;
		case ELexWidgetSizeType::ExpandToParent:
			if (UIParent.IsValid())
			{
				if (UIParent->Width.Type == ELexWidgetSizeType::ShrinkToChildren)
				{
					RenderSize.X = Width.Value;
				}
				else
				{
					RenderSize.X = UIParent->GetRenderSize().X
					- (UIParent->GetPadding().Left + UIParent->GetPadding().Right);
					RenderSize.X *= Width.Percent * 0.01f;
				}
			}
			else
			{
				RenderSize.X = Width.Value;
			}
			break;
		case ELexWidgetSizeType::ShrinkToChildren:
			if (IsValid(Layout) && Layout->SupportShrinkToChildrenWidth() && this->IsVisibleForLayout())
			{
				RenderSize.X = Layout->GetShrinkToChildrenWidth();
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
				RenderSize.X = MaxSize + (this->GetPadding().Left + this->GetPadding().Right);
			}
			else if (IsValid(Visual))
			{
				RenderSize.X = Visual->GetShrinkToContentWidth();
			}
			else
			{
				RenderSize.X = Width.Value;
			}
			break;
		}
	}
	if (!LayoutSlotValid || !LayoutSlot->GetLayoutControlHeight())
	{
		switch (Height.Type)
		{
		case ELexWidgetSizeType::Fixed:
			RenderSize.Y = Height.Value;
			break;
		case ELexWidgetSizeType::ExpandToParent:
			if (UIParent.IsValid())
			{
				if (UIParent->Height.Type == ELexWidgetSizeType::ShrinkToChildren)
				{
					RenderSize.Y = Height.Value;
				}
				else
				{
					RenderSize.Y = UIParent->GetRenderSize().Y
					- (UIParent->GetPadding().Bottom + UIParent->GetPadding().Top);
					RenderSize.Y *= Height.Percent * 0.01f;
				}
			}
			else
			{
				RenderSize.Y = Height.Value;
			}
			break;
		case ELexWidgetSizeType::ShrinkToChildren:
			if (IsValid(Layout) && Layout->SupportShrinkToChildrenHeight() && this->IsVisibleForLayout())
			{
				RenderSize.Y = Layout->GetShrinkToChildrenHeight();
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
				RenderSize.Y = MaxSize + (this->GetPadding().Bottom + this->GetPadding().Top);
			}
			else if (IsValid(Visual))
			{
				RenderSize.Y = Visual->GetShrinkToContentHeight();
			}
			else
			{
				RenderSize.Y = Height.Value;
			}
			break;
		}
	}
	switch (AspectRatio.Type)
	{
	case ELexWidgetAspectRatioType::None:
		break;
	case ELexWidgetAspectRatioType::HeightControlWidth:
		RenderSize.X = RenderSize.Y * AspectRatio.Value;
		break;
	case ELexWidgetAspectRatioType::WidthControlHeight:
		RenderSize.Y = RenderSize.X / AspectRatio.Value;
		break;
	}
} 

void ULexWidget::CheckRootUIItem(ULexWidget* RootUIItemInParent)
{
	auto oldRootUIItem = RootUIItem;
	if (oldRootUIItem == this && oldRootUIItem != nullptr)
	{
		ULGUIManagerWorldSubsystem::RemoveRootUIItem(this);
	}

	if (RootUIItemInParent == nullptr)
	{
		ULexWidget* topUIItem = this;
		ULexWidget* tempRootUIItem = nullptr;
		while (topUIItem != nullptr && topUIItem->IsRegistered())
		{
			tempRootUIItem = topUIItem;
			topUIItem = Cast<ULexWidget>(topUIItem->GetAttachParent());
		}
		RootUIItemInParent = tempRootUIItem;
	}
	RootUIItem = RootUIItemInParent;

	if (RootUIItem == this && RootUIItem != nullptr)
	{
		ULGUIManagerWorldSubsystem::AddRootUIItem(this);
	}
}

FDelegateHandle ULexWidget::RegisterUIHierarchyChanged(const FSimpleDelegate& InCallback)
{
	return UIHierarchyChangedDelegate.Add(InCallback);
}
void ULexWidget::UnregisterUIHierarchyChanged(const FDelegateHandle& InHandle)
{
	UIHierarchyChangedDelegate.Remove(InHandle);
}

float ULexWidget::GetRenderWidth() const
{
	return GetRenderSize().X;
}

float ULexWidget::GetRenderHeight() const
{
	return GetRenderSize().Y;
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
		MarkSizeDirty_Recursive();
	}
}

void ULexWidget::SetWidth(const FLexWidgetSize& Value)
{
	if (Width != Value)
	{
		Width = Value;
		MarkSizeDirty_Recursive();
	}
}

void ULexWidget::SetHeight(const FLexWidgetSize& Value)
{
	if (Height != Value)
	{
		Height = Value;
		MarkSizeDirty_Recursive();
	}
}

void ULexWidget::SetSize(const FLexWidgetSize2& Value)
{
	if (Width != Value.X || Height != Value.Y)
	{
		Width = Value.X;
		Height = Value.Y;
		MarkSizeDirty_Recursive();
	}
}

void ULexWidget::SetPadding(const FMargin& Value)
{
	if (Padding != Value)
	{
		Padding = Value;
		MarkSizeDirty_Recursive();
	}
}

void ULexWidget::SetMargin(const FMargin& Value)
{
	if (Margin != Value)
	{
		Margin = Value;
		MarkSizeDirty_Recursive();
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

void ULexWidget::MarkDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	if (ClipData.IsValid() && ClipData.Pin()->GetWidget() == this)
	{
		ClipData.Pin()->MarkNeedUpdateData();
	}

	if (IsValid(Layout))
	{
		Layout->OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	}
	if (CheckAndGetLayoutSlot())
	{
		LayoutSlot->OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	}
	if (IsValid(Visual))
	{
		Visual->OnDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
	}

	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(false, InPivotChange || InWidthChange || InHeightChange, false);//mark canvas to update
		if (this->IsCanvasUIItem())
		{
			this->RenderCanvas->MarkSizeChanged();
		}
	}

	if (InWidthChange || InHeightChange)
	{
		CallUILifeCycleBehavioursDimensionsChanged(false, false, InWidthChange, InHeightChange);
	}

	if (UIParent.IsValid())
	{
		UIParent->OnChildDimensionChanged(this);
	}

	for (auto& UIChild : UIChildren)
	{
		if (IsValid(UIChild))
		{
			UIChild->MarkParentDimensionChanged(InPivotChange, InWidthChange, InHeightChange);
		}
	}
}

void ULexWidget::MarkParentDimensionChanged(bool InParentPivotChange, bool InParentWidthChange, bool InParentHeightChange)
{
	if (IsValid(Layout))
	{
		Layout->OnParentDimensionChanged(false, false, false);
	}
	if (CheckAndGetLayoutSlot())
	{
		LayoutSlot->OnParentDimensionChanged(InParentPivotChange, InParentWidthChange, InParentHeightChange);
	}
}

void ULexWidget::MarkNeedUpdateLayout()
{
	GetRenderCanvas()->MarkCanvasUpdate(false, true, false, false);
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
		if (this->IsCanvasUIItem())
		{
			this->RenderCanvas->MarkSizeChanged();
		}
	}

	if (InPositionChanged || InScaleChanged)
	{
		CallUILifeCycleBehavioursDimensionsChanged(InPositionChanged, InPositionChanged, InScaleChanged, InScaleChanged);
	}

	for (auto& UIChild : UIChildren)
	{
		if (IsValid(UIChild))
		{
			UIChild->MarkTransformChanged(InPositionChanged, InScaleChanged);
		}
	}
}

void ULexWidget::MarkSizeDirty_Recursive()
{
	struct LOCAL
	{
		static void MarkChildrenDirty(ULexWidget* Target)
		{
			Target->bRenderSizeDirty = true;
			Target->MarkDimensionChanged(false, true, true);
			if (IsValid(Target->Layout))
			{
				for (auto& Child : Target->GetUIChildren())
				{
					MarkChildrenDirty(Child);
				}
			}
			else
			{
				for (auto& Child : Target->GetUIChildren())
				{
					if (Child->Width.Type == ELexWidgetSizeType::ExpandToParent
						|| Child->Height.Type == ELexWidgetSizeType::ExpandToParent)
					{
						MarkChildrenDirty(Child);
					}
				}
			}
		}
	};
	auto ParentWithLayout = this;
	while (ParentWithLayout)
	{
		auto TempParent = ParentWithLayout->GetUIParent();
		if (TempParent && TempParent->GetLayout())
		{
			ParentWithLayout = TempParent;
		}
		else
		{
			break;
		}
	}
	LOCAL::MarkChildrenDirty(ParentWithLayout);
}

void ULexWidget::OnChildDimensionChanged(ULexWidget* InChild)
{
	if (IsValid(Layout))
	{
		Layout->MarkLayoutDirty();
	}

	if (this->Width.Type == ELexWidgetSizeType::ShrinkToChildren
		|| this->Height.Type == ELexWidgetSizeType::ShrinkToChildren)
	{
		this->bRenderSizeDirty = true;
		this->MarkDimensionChanged(false, true, true);
	}
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

#pragma region UIActive

void ULexWidget::OnChildActiveStateChanged(ULexWidget* child)
{
	CallUILifeCycleBehavioursChildActiveInHierarchyStateChanged(child, child->GetIsUIActiveInHierarchy());
}

void ULexWidget::MarkClipDirty_Recursive(bool InClipTypeChanged) const
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
		MarkClipDirty_Recursive(true);
	}
}

bool ULexWidget::CalculateCacheFinalIsEnabled()
{
	if (bIsEnabled)
	{
		if (UIParent.IsValid())
			return UIParent->CalculateCacheFinalIsEnabled();
		return true;
	}
	return false;
}
void ULexWidget::CheckIsEnabled_Recursive()
{
	bool NowFinalIsEnabled = CalculateCacheFinalIsEnabled();
	if (bCacheFinalIsEnabled != NowFinalIsEnabled)
	{
		bCacheFinalIsEnabled = NowFinalIsEnabled;
		CallUILifeCycleBehavioursActiveInHierarchyStateChanged();
		for (auto Child : GetUIChildren())
		{
			Child->CheckIsEnabled_Recursive();
		}
	}
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
	bool SelfVisibleForRender =	WidgetVisibility == ESlateVisibility::Visible
	|| WidgetVisibility == ESlateVisibility::HitTestInvisible
	|| WidgetVisibility == ESlateVisibility::SelfHitTestInvisible
	;
	if (SelfVisibleForRender == false)
		return false;
	if (UIParent.IsValid())
		return UIParent->IsVisibleForRender();
	return true;
}

bool ULexWidget::IsVisibleForHitTest() const
{
	bool SelfVisibleForHitTest = WidgetVisibility == ESlateVisibility::Visible;
	if (SelfVisibleForHitTest == false)
		return false;
	if (UIParent.IsValid())
	{
		if (UIParent->WidgetVisibility == ESlateVisibility::SelfHitTestInvisible)
			return true;
		return UIParent->IsVisibleForHitTest();
	}
	return true;
}

bool ULexWidget::IsVisibleForLayout() const
{
	bool SelfVisibleForLayout =	WidgetVisibility == ESlateVisibility::Visible
	|| WidgetVisibility == ESlateVisibility::Hidden
	|| WidgetVisibility == ESlateVisibility::HitTestInvisible
	|| WidgetVisibility == ESlateVisibility::SelfHitTestInvisible
	;
	if (SelfVisibleForLayout == false)
		return false;
	if (UIParent.IsValid())
		return UIParent->IsVisibleForLayout();
	return true;
}

void ULexWidget::SetWidgetVisibility(ESlateVisibility Value)
{
	if (WidgetVisibility != Value)
	{
		WidgetVisibility = Value;
		check(0);
	}
}

void ULexWidget::SetIsEnabled(bool Value)
{
	if (bIsEnabled != Value)
	{
		bIsEnabled = Value;
		CheckIsEnabled_Recursive();
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

void ULexWidget::SetVisual(ULexVisual* Value)
{
	if (Visual != Value)
	{
		if (RenderCanvas.IsValid())
		{
			if (IsValid(Visual))
			{
				RenderCanvas->MarkVisualWillChange(Visual);
				RenderCanvas->UnregisterVisual(this);
			}
			if (Value)
			{
				RenderCanvas->RegisterVisual(this);
			}
		}
		Visual = Value;
	}
}

ULexLayoutSlot* ULexWidget::CheckAndGetLayoutSlot()const
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
		LayoutSlot = NewObject<ULexLayoutSlot>(const_cast<ULexWidget*>(this), LayoutSlotClass, NAME_None, RF_Public | RF_Transactional);
	}
	return LayoutSlot;
}

void ULexWidget::CheckUIActiveState()
{
	auto thisUIActiveState = this->GetIsUIActiveInHierarchy();
	CheckChildrenUIActiveRecursive(thisUIActiveState);
}

void ULexWidget::CheckChildrenUIActiveRecursive(bool InUpParentUIActive)
{
	for (auto& uiChild : UIChildren)
	{
		if (IsValid(uiChild))
		{		//state is changed
			if ((uiChild->bIsUIActive &&//when child is active, then parent's active state can affect child
				(uiChild->bAllUpParentUIActive != InUpParentUIActive))//state change
#if WITH_EDITOR
				|| bUIActiveStateDirty
#endif
				)
			{
#if WITH_EDITOR
				bUIActiveStateDirty = false;
#endif
				uiChild->bAllUpParentUIActive = InUpParentUIActive;
				//apply for state change
				uiChild->ApplyUIActiveState(true);
				//affect children
				uiChild->CheckChildrenUIActiveRecursive(uiChild->GetIsUIActiveInHierarchy());
				//callback for parent
				this->OnChildActiveStateChanged(uiChild);
			}
			//state not changed
			else
			{
				uiChild->bAllUpParentUIActive = InUpParentUIActive;
				//apply for state change
				uiChild->ApplyUIActiveState(false);
				//affect children
				uiChild->CheckChildrenUIActiveRecursive(uiChild->GetIsUIActiveInHierarchy());
			}
		}
	}
}
void ULexWidget::SetIsUIActive(bool active)
{
	if (bIsUIActive != active)
	{
		bIsUIActive = active;
		if (bAllUpParentUIActive)//state change only happens when up parent is active
		{
			ApplyUIActiveState(true);
			//affect children
			CheckChildrenUIActiveRecursive(bIsUIActive);
			//callback for parent
			if (UIParent.IsValid())
			{
				UIParent->OnChildActiveStateChanged(this);
			}
		}
		else
		{
			//nothing
		}
	}
}

void ULexWidget::ApplyUIActiveState(bool InStateChange)
{
#if WITH_EDITOR
	//modify inactive actor's name
	auto Actor = GetOwner();
	if (Actor != nullptr && this == Actor->GetRootComponent())
	{
		auto bHiddenEdTemporary_Property = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bHiddenEdTemporary"));
		bHiddenEdTemporary_Property->SetPropertyValue_InContainer(Actor, !GetIsUIActiveInHierarchy());
		//Actor->SetIsTemporarilyHiddenInEditor(!GetIsUIActiveInHierarchy());
	}
#endif
	if (InStateChange)
	{
		//callback
		CallUILifeCycleBehavioursActiveInHierarchyStateChanged();
		//canvas update
		MarkCanvasUpdate(false, false, false, true);
	}
}

#if WITH_EDITOR
void ULexWidget::SetIsTemporarilyHiddenInEditor_Recursive_By_IsUIActiveState()
{
	ApplyUIActiveState(true);
	//affect children
	for (auto& uiChild : UIChildren)
	{
		if (IsValid(uiChild))
		{
			uiChild->SetIsTemporarilyHiddenInEditor_Recursive_By_IsUIActiveState();
		}
	}
}
#endif

FDelegateHandle ULexWidget::RegisterUIActiveStateChanged(const FUIItemActiveInHierarchyStateChangedDelegate& InCallback)\
{
	return UIActiveInHierarchyStateChangedDelegate.Add(InCallback);
}
FDelegateHandle ULexWidget::RegisterUIActiveStateChanged(const TFunction<void(bool)>& InCallback)
{
	return UIActiveInHierarchyStateChangedDelegate.AddLambda(InCallback);
}
void ULexWidget::UnregisterUIActiveStateChanged(const FDelegateHandle& InHandle)
{
	UIActiveInHierarchyStateChangedDelegate.Remove(InHandle);
}

#pragma endregion UIActive

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




UUIItemEditorHelperComp::UUIItemEditorHelperComp()
{
	bSelectable = false;
	bIsEditorOnly = true;
	MarkAsEditorOnlySubobject();
}

#if WITH_EDITOR
FPrimitiveSceneProxy* UUIItemEditorHelperComp::CreateSceneProxy()
{
	class FUIItemSceneProxy : public FPrimitiveSceneProxy
	{
	public:
		SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		FUIItemSceneProxy(ULexWidget* InComponent, UPrimitiveComponent* InPrimitive)
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

	return new FUIItemSceneProxy(this->Parent, this);
}
#endif

UBodySetup* UUIItemEditorHelperComp::GetBodySetup()
{
	UpdateBodySetup();
	return BodySetup;
}
void UUIItemEditorHelperComp::UpdateBodySetup()
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
FBoxSphereBounds UUIItemEditorHelperComp::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!IsValid(Parent))return FBoxSphereBounds(EForceInit::ForceInit);
	auto Center = Parent->GetLocalSpaceCenter();
	auto Origin = FVector(0, Center.X, Center.Y);
	return FBoxSphereBounds(Origin, FVector(1, Parent->GetRenderWidth() * 0.5f, Parent->GetRenderHeight() * 0.5f), (Parent->GetRenderWidth() > Parent->GetRenderHeight() ? Parent->GetRenderWidth() : Parent->GetRenderHeight()) * 0.5f).TransformBy(LocalToWorld);
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif
