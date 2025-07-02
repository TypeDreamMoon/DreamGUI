// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/Components/LexWidget.h"
#include "LGUI.h"
#include "LGUI/Public/Core/Components/LexCanvas.h"
#include "Core/LGUISettings.h"
#include "Core/LGUILifeCycleUIBehaviour.h"
#include "Core/LGUIManager.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "PhysicsEngine/BodySetup.h"
#include "Layout/LGUICanvasScaler.h"
#include "LTweenManager.h"
#include "Core/LexUIClipData.h"
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
	bCanSetAnchorFromTransform = false;//skip construction

	bWantsOnUpdateTransform = true;
	bFlattenHierarchyIndexDirty = true;
	bWidthCached = false;
	bHeightCached = false;
	bAnchorLeftCached = false;
	bAnchorRightCached = false;
	bAnchorBottomCached = false;
	bAnchorTopCached = false;
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
	OnUIActiveInHierachy(TempIsUIActive);
	if (this->GetOwner()->GetRootComponent() != this)return;
	if (UIActiveInHierarchyStateChangedDelegate.IsBound())UIActiveInHierarchyStateChangedDelegate.Broadcast(TempIsUIActive);
}
void ULexWidget::CallUILifeCycleBehavioursChildDimensionsChanged(ULexWidget* child, bool horizontalPositionChanged, bool verticalPositionChanged, bool widthChanged, bool heightChanged)
{
	if (this->GetOwner() == nullptr)return;
	if (this->GetWorld() == nullptr)return;
	OnUIChildDimensionsChanged(child, horizontalPositionChanged, verticalPositionChanged, widthChanged, heightChanged);
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
	OnUIChildAcitveInHierarchy(child, activeOrInactive);
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
	OnUIDimensionsChanged(horizontalPositionChanged, verticalPositionChanged, widthChanged, heightChanged);
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
	OnUIAttachmentChanged();
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
	OnUIChildAttachmentChanged(child, attachOrDettach);
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
	OnUIInteractionStateChanged(GetFinalIsEnabled());
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
	OnUIChildHierarchyIndexChanged(child);
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
	if (InInt != hierarchyIndex)
	{
		hierarchyIndex = InInt;
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
			this->hierarchyIndex = 0;
			UIParent->CallUILifeCycleBehavioursChildHierarchyIndexChanged(this);
		}
		else
		{
			UIParent->EnsureUIChildrenValid();
			UIParent->EnsureUIChildrenSorted();
			hierarchyIndex = FMath::Clamp(hierarchyIndex, 0, UIParent->UIChildren.Num() - 1);
			UIParent->UIChildren.Remove(this);
			UIParent->UIChildren.Insert(this, hierarchyIndex);
			bool anythingChange = false;
			for (int i = 0; i < UIParent->UIChildren.Num(); i++)
			{
				if (UIParent->UIChildren[i]->hierarchyIndex != i)
				{
					UIParent->UIChildren[i]->hierarchyIndex = i;
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
		hierarchyIndex = 0;
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
	bWidthCached = false;
	bHeightCached = false;
	bAnchorLeftCached = false;
	bAnchorRightCached = false;
	bAnchorTopCached = false;
	bAnchorBottomCached = false;
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
		auto PropertyName = PropertyChangedEvent.Property->GetFName();

		static const FName VisualName = GET_MEMBER_NAME_CHECKED(ULexWidget, Visual);
		
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexWidget, bIsUIActive))
		{
			bIsUIActive = !bIsUIActive;//make it work
			SetIsUIActive(!bIsUIActive);
		}

		else if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexWidget, hierarchyIndex))
		{
			ApplyHierarchyIndex();
		}
		else if (PropertyName == FName(TEXT("RelativeLocation")))
		{
			CalculateAnchorFromTransform();
			UpdateComponentToWorld();
		}
		else if (PropertyName == FName(TEXT("AnchorData")))
		{
			CalculateTransformFromAnchor();
			UpdateComponentToWorld();
		}
		else if (PropertyName == VisualName)
		{
			
		}
		EditorForceUpdate();
		UpdateBounds();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexWidget, Clipping))
		{
			MarkClipDirty_Recursive(true);
		}
		else
		{
			MarkClipDirty_Recursive(false);
		}
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
	return FBoxSphereBounds(Origin, FVector(1, this->GetWidth() * 0.5f, this->GetHeight() * 0.5f), (this->GetWidth() > this->GetHeight() ? this->GetWidth() : this->GetHeight()) * 0.5f).TransformBy(LocalToWorld);
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
			UIItem->CalculateTransformFromAnchor();
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
	if (bCanSetAnchorFromTransform
		&& this->IsRegistered()//check if registerred, because it may called from reconstruction.
		)
	{
		CalculateAnchorFromTransform();
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
		this->RenderCanvas->MarkCanvasLayoutDirty();
	}
	if (IsValid(Visual))
	{
		Visual->OnTransformChanged();
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
	auto CompScale3D = this->GetComponentScale();
	auto CompScale2D = FVector2f(CompScale3D.Y, CompScale3D.Z);

	bAnchorLeftCached = false;
	bAnchorRightCached = false;
	bAnchorBottomCached = false;
	bAnchorTopCached = false;

	bool AnchorChanged = !AnchorData.AnchoredPosition.Equals(CalculatedAnchoredPosition);
	bool ScaleChanged = !PrevScale2D.Equals(CompScale2D);
	if (AnchorChanged || ScaleChanged)
	{
		AnchorData.AnchoredPosition = CalculatedAnchoredPosition;
		PrevScale2D = CompScale2D;
	}
	SetOnTransformChange(AnchorChanged, ScaleChanged);
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
				childUIItem->hierarchyIndex = UIChildren.Num() - 1;
				this->CallUILifeCycleBehavioursChildHierarchyIndexChanged(childUIItem);
			}
			else//not registered means is loading from level. then no need to set hierarchy index
			{

			}
		}

		//make sure hierarchyindex all good
		if (childUIItem->hierarchyIndex == INDEX_NONE)
		{
			for (int i = 0; i < UIChildren.Num(); i++)
			{
				auto& UIChild = UIChildren[i];
				if (UIChild->hierarchyIndex != i)
				{
					UIChild->hierarchyIndex = i;
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
			this->CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
		}
	}

	ULexCanvas* ParentCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(GetOwner()->GetAttachParentActor(), false);
	UIHierarchyChanged(ParentCanvas, UIParent->RootUIItem.Get());
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
			if (UIChild->hierarchyIndex != i)
			{
				UIChild->hierarchyIndex = i;
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
			this->CalculateAnchorFromTransform();//if not from PrefabSystem, then calculate anchors on transform, so when use AttachComponent, the KeepRelative or KeepWorld will work. If from PrefabSystem, then anchor will automatically do the job
		}
	}

	UIHierarchyChanged(nullptr, nullptr);
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

	bCanSetAnchorFromTransform = true;
	CheckRootUIItem();

	if (IsValid(Visual))
	{
		Visual->OnRegister();
	}
}
void ULexWidget::OnUnregister()
{
	Super::OnUnregister();
	if (auto world = this->GetWorld())
	{
#if WITH_EDITOR
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
#endif
	}
	CheckRootUIItem();

	if (IsValid(Visual))
	{
		Visual->OnUnregister();
	}
}

void ULexWidget::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);
#if WITH_EDITORONLY_DATA
	if (HelperComp)
	{
		HelperComp->DestroyComponent();
		HelperComp = nullptr;
	}
#endif
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
		bWidthCached = false;
		bHeightCached = false;
		bAnchorLeftCached = false;
		bAnchorRightCached = false;
		bAnchorBottomCached = false;
		bAnchorTopCached = false;

		SetOnAnchorChange(false, true, true);
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
	if (!bWidthCached)
	{
		bWidthCached = true;
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
	if (!bHeightCached)
	{
		bHeightCached = true;
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

void ULexWidget::SetAnchorData(const FUIAnchorData& InAnchorData)
{
	AnchorData.Pivot = InAnchorData.Pivot;
	AnchorData.AnchorMin = InAnchorData.AnchorMin;
	AnchorData.AnchorMax = InAnchorData.AnchorMax;
	AnchorData.AnchoredPosition = InAnchorData.AnchoredPosition;
	AnchorData.SizeDelta = InAnchorData.SizeDelta;

	bWidthCached = false;
	bHeightCached = false;
	bAnchorLeftCached = false;
	bAnchorRightCached = false;
	bAnchorBottomCached = false;
	bAnchorTopCached = false;

	SetOnAnchorChange(true, true, true);
}

void ULexWidget::SetPivot(FVector2D Value) 
{
	if (!AnchorData.Pivot.Equals(Value, 0.0f))
	{
		AnchorData.Pivot = Value;
		bAnchorLeftCached = false;
		bAnchorRightCached = false;
		bAnchorBottomCached = false;
		bAnchorTopCached = false;
		SetOnAnchorChange(true, false, false);
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

			SetOnAnchorChange(false, true, true);
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

			SetOnAnchorChange(false, true, true);
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

			SetOnAnchorChange(false, !bKeepSize, !bKeepSize);
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

			SetOnAnchorChange(false, !bKeepSize, !bKeepSize);
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
		SetOnAnchorChange(false, false, false);
	}
}

void ULexWidget::SetHorizontalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.X != Value)
	{
		AnchorData.AnchoredPosition.X = Value;
		SetOnAnchorChange(false, false, false);
	}
}
void ULexWidget::SetVerticalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.Y != Value)
	{
		AnchorData.AnchoredPosition.Y = Value;
		SetOnAnchorChange(false, false, false);
	}
}

void ULexWidget::SetSizeDelta(FVector2D Value)
{
	if (!AnchorData.SizeDelta.Equals(Value, 0.0f))
	{
		AnchorData.SizeDelta = Value;
		bWidthCached = false;
		bHeightCached = false;
		SetOnAnchorChange(false, true, true);
	}
}

float ULexWidget::GetAnchorLeft()const
{
	if (!bAnchorLeftCached)
	{
		bAnchorLeftCached = true;
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
	if (!bAnchorTopCached)
	{
		bAnchorTopCached = true;
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
	if (!bAnchorRightCached)
	{
		bAnchorRightCached = true;
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
	if (!bAnchorBottomCached)
	{
		bAnchorBottomCached = true;
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
		if (CacheAnchorLeft != Value || !bAnchorLeftCached)
		{
			bAnchorLeftCached = true;
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
			SetOnAnchorChange(false, true, false);
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
		if (CacheAnchorTop != Value || !bAnchorTopCached)
		{
			bAnchorTopCached = true;
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
			SetOnAnchorChange(false, false, true);
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
		if (CacheAnchorRight != Value || !bAnchorRightCached)
		{
			bAnchorRightCached = true;
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
			SetOnAnchorChange(false, true, false);
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
		if (CacheAnchorBottom != Value || !bAnchorBottomCached)
		{
			bAnchorBottomCached = true;
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
			SetOnAnchorChange(false, false, true);
		}
	}
	else
	{
		UE_LOG(LGUI, Warning, TEXT("[%s].%d This function only valid if UIItem have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

void ULexWidget::SetWidth(float Value)
{
	if (CacheWidth != Value || !bWidthCached)
	{
		bWidthCached = true;
		CacheWidth = Value;
		if (UIParent.IsValid())
		{
			if (AnchorData.IsHorizontalStretched())
			{
				auto CalculatedSizeDeltaX = Value - (UIParent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
				if (AnchorData.SizeDelta.X != CalculatedSizeDeltaX)
				{
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
					SetOnAnchorChange(false, true, false);
				}
			}
			else
			{
				if (AnchorData.SizeDelta.X != Value)
				{
					AnchorData.SizeDelta.X = Value;
					SetOnAnchorChange(false, true, false);
				}
			}
		}
		else
		{
			if (AnchorData.SizeDelta.X != Value)
			{
				AnchorData.SizeDelta.X = Value;
				SetOnAnchorChange(false, true, false);
			}
		}
	}
}
void ULexWidget::SetHeight(float Value)
{
	if (CacheHeight != Value || !bHeightCached)
	{
		bHeightCached = true;
		CacheHeight = Value;
		if (UIParent.IsValid())
		{
			if (AnchorData.IsVerticalStretched())
			{
				auto CalculatedSizeDeltaY = Value - (UIParent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
				if (AnchorData.SizeDelta.Y != CalculatedSizeDeltaY)
				{
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
					SetOnAnchorChange(false, false, true);
				}
			}
			else
			{
				if (AnchorData.SizeDelta.Y != Value)
				{
					AnchorData.SizeDelta.Y = Value;
					SetOnAnchorChange(false, false, true);
				}
			}
		}
		else
		{
			if (AnchorData.SizeDelta.Y != Value)
			{
				AnchorData.SizeDelta.Y = Value;
				SetOnAnchorChange(false, false, true);
			}
		}
	}
}

#pragma endregion

ULexWidget* ULexWidget::GetAttachUIChild(int index)const
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
ULGUICanvasScaler* ULexWidget::GetCanvasScaler()const
{
	if (auto canvas = GetRootCanvas())
	{
		return canvas->GetOwner()->FindComponentByClass<ULGUICanvasScaler>();
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

void ULexWidget::SetOnAnchorChange(bool InPivotChange, bool InWidthChange, bool InHeightChange)
{
	OnAnchorChange(InPivotChange, InWidthChange, InHeightChange, false);
}

void ULexWidget::SetOnTransformChange(bool InPositionChanged, bool InScaleChanged)
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
			this->RenderCanvas->MarkCanvasLayoutDirty();
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
			UIChild->SetOnTransformChange(InPositionChanged, InScaleChanged);
		}
	}
}

void ULexWidget::OnAnchorChange(bool InPivotChange, bool InWidthChange, bool InHeightChange, bool InDiscardCache)
{
	if (ClipData.IsValid() && ClipData.Pin()->GetWidget() == this)
	{
		ClipData.Pin()->MarkNeedUpdateData();
	}
	bool HorizontalPositionChanged = false, VerticalPositionChanged = false;
	CalculateTransformFromAnchor(HorizontalPositionChanged, VerticalPositionChanged);

	if (InDiscardCache)
	{
		if (InWidthChange)
		{
			bWidthCached = false;
		}
		if (InHeightChange)
		{
			bHeightCached = false;
		}
		bAnchorLeftCached = false;
		bAnchorRightCached = false;
		bAnchorBottomCached = false;
		bAnchorTopCached = false;
	}

	if (IsValid(Visual))
	{
		Visual->OnAnchorChange(InPivotChange, InWidthChange, InHeightChange);
	}

	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(false, HorizontalPositionChanged || VerticalPositionChanged, false);//mark canvas to update
		if (this->IsCanvasUIItem())
		{
			this->RenderCanvas->MarkCanvasLayoutDirty();
		}
	}

	if (HorizontalPositionChanged || VerticalPositionChanged || InWidthChange || InHeightChange)
	{
		CallUILifeCycleBehavioursDimensionsChanged(HorizontalPositionChanged, VerticalPositionChanged, InWidthChange, InHeightChange);
	}

	for (auto& UIChild : UIChildren)
	{
		if (IsValid(UIChild))
		{
			bool ChildWidthChange = false, ChildHeightChange = false;
			if (InWidthChange && UIChild->AnchorData.IsHorizontalStretched())
			{
				ChildWidthChange = true;
			}
			if (InHeightChange && UIChild->AnchorData.IsVerticalStretched())
			{
				ChildHeightChange = true;
			}
			UIChild->OnAnchorChange(false, ChildWidthChange, ChildHeightChange);
		}
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
ULTweener* ULexWidget::WidthTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetWidth), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetWidth), endValue, duration);
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
ULTweener* ULexWidget::HeightTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetHeight), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetHeight), endValue, duration);
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

ULTweener* ULexWidget::HorizontalAnchoredPositionTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetHorizontalAnchoredPosition), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetHorizontalAnchoredPosition), endValue, duration);
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
ULTweener* ULexWidget::VerticalAnchoredPositionTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetVerticalAnchoredPosition), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetVerticalAnchoredPosition), endValue, duration);
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
ULTweener* ULexWidget::AnchoredPositionTo(FVector2D endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenVector2DGetterFunction::CreateUObject(this, &ULexWidget::GetAnchoredPosition), FLTweenVector2DSetterFunction::CreateUObject(this, &ULexWidget::SetAnchoredPosition), endValue, duration);
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
ULTweener* ULexWidget::PivotTo(FVector2D endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenVector2DGetterFunction::CreateUObject(this, &ULexWidget::GetPivot), FLTweenVector2DSetterFunction::CreateUObject(this, &ULexWidget::SetPivot), endValue, duration);
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

ULTweener* ULexWidget::AnchorLeftTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetAnchorLeft), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetAnchorLeft), endValue, duration);
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
ULTweener* ULexWidget::AnchorRightTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetAnchorRight), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetAnchorRight), endValue, duration);
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
ULTweener* ULexWidget::AnchorTopTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetAnchorTop), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetAnchorTop), endValue, duration);
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
ULTweener* ULexWidget::AnchorBottomTo(float endValue, float duration, float delay, ELTweenEase ease)
{
	auto Tweener = ULTweenManager::To(this, FLTweenFloatGetterFunction::CreateUObject(this, &ULexWidget::GetAnchorBottom), FLTweenFloatSetterFunction::CreateUObject(this, &ULexWidget::SetAnchorBottom), endValue, duration);
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
#include "Layout/LGUICanvasScaler.h"
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
	BoxElem->Y = Parent->GetWidth();
	BoxElem->Z = Parent->GetHeight();

	BoxElem->Center = Origin;
}
FBoxSphereBounds UUIItemEditorHelperComp::CalcBounds(const FTransform& LocalToWorld) const
{
	if (!IsValid(Parent))return FBoxSphereBounds(EForceInit::ForceInit);
	auto Center = Parent->GetLocalSpaceCenter();
	auto Origin = FVector(0, Center.X, Center.Y);
	return FBoxSphereBounds(Origin, FVector(1, Parent->GetWidth() * 0.5f, Parent->GetHeight() * 0.5f), (Parent->GetWidth() > Parent->GetHeight() ? Parent->GetWidth() : Parent->GetHeight()) * 0.5f).TransformBy(LocalToWorld);
}

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif
