// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/Components/LGUICanvas.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Utils/LexUIUtils.h"
#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#endif
#include "Core/LGUISettings.h"
#include "Core/LGUIManager.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "LGUI/Public/Core/LexUIRender/LexUIRenderer.h"
#include "LGUI/Public/Core/LexUIMesh/LexUIMeshComponent.h"
#include "Core/LexUIDrawCall.h"
#include "LGUI/Public/Core/Components/UIBaseRenderable.h"
#include "LGUI/Public/Core/Components/UIBatchMeshRenderable.h"
#include "LGUI/Public/Core/Components/UIPostProcessRenderable.h"
#include "LGUI/Public/Core/Components/UIDirectMeshRenderable.h"
#include "LGUI/Public/Core/Components/UIItem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "SceneViewExtension.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Math/TransformCalculus2D.h"
#include "TextureResource.h"
#include "Core/LexUIClipData.h"
#include "Core/LexUIDataAsTexture.h"

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif

#define LOCTEXT_NAMESPACE "LGUICanvas"

ULGUICanvas::ULGUICanvas()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	bHasAddToLGUIScreenSpaceRenderer = false;
	bHasSetIntialStateforLGUIWorldSpaceRenderer = false;
	bOverrideViewLocation = false;
	bOverrideViewRotation = false;
	bOverrideProjectionMatrix = false;
	bOverrideFovAngle = false;
	bPrevUIItemIsActive = true;
	bNeedToVerifyMaterials = true;
	bRootCanvasNeedToUpdateChildrenCanvasBounds = false;
	bUIMeshNeedToSetInitialParameters = true;

	bCanTickUpdate = true;
	bShouldRebuildDrawCall = true;
	bShouldSortRenderableOrder = true;
	bAnythingChangedForRenderTarget = true;
	bPrevAnythingChangedForRenderTarget = true;

	bIsViewProjectionMatrixDirty = true;

	DefaultMeshType = ULexUIMeshComponent::StaticClass();
	DefaultMaterial = LoadObject<UMaterialInterface>(NULL, TEXT("/LGUI/Materials/LexUI_Image"));
}

void ULGUICanvas::BeginPlay()
{
	Super::BeginPlay();
	CheckRootCanvas();
	CurrentRenderMode = this->GetActualRenderMode();
	if (CheckUIItem())
	{
		bPrevUIItemIsActive = UIItem->GetIsUIActiveInHierarchy();
	}
	else
	{
		bPrevUIItemIsActive = false;
	}
	MarkCanvasUpdate(true, true, true, true);

	bNeedToSortRenderPriority = true;
}
void ULGUICanvas::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ULGUICanvas::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> ULGUICanvas::GetRenderTargetViewExtension()
{
	if (!RenderTargetViewExtension.IsValid())
	{
		RenderTargetViewExtension = FSceneViewExtensions::NewExtension<FLexUIRenderer>(GetWorld(), ELexUIRendererType::RenderTarget);
	}
	return RenderTargetViewExtension;
}

void ULGUICanvas::UpdateRootCanvas()
{
	CheckRootCanvas();
	if (this == RootCanvas)
	{
		bool bIsRenderTargetRenderer = false;
		if (RenderModeIsLGUIRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (previewWithLGUIRenderer)
			{
				if (!GetWorld()->IsGameWorld())//edit mode
				{
					if (ActualRenderMode == ELGUIRenderMode::ScreenSpaceOverlay)
						ActualRenderMode = ELGUIRenderMode::WorldSpace_LGUI;
				}
			}
#endif
			switch (ActualRenderMode)
			{
			case ELGUIRenderMode::ScreenSpaceOverlay:
			{
				if (!bHasAddToLGUIScreenSpaceRenderer)
				{
					auto ViewExtension = ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true);

					if (ViewExtension.IsValid())//only root canvas can add screen space UI to LGUIRenderer
					{
						ViewExtension->SetScreenSpaceRootCanvas(this);
						bHasAddToLGUIScreenSpaceRenderer = true;
					}
				}
			}
			break;
			case ELGUIRenderMode::RenderTarget:
			{
				if (!bHasAddToLGUIScreenSpaceRenderer)
				{
					GetRenderTargetViewExtension()->SetScreenSpaceRootCanvas(this);
					bHasAddToLGUIScreenSpaceRenderer = true;
				}
				bIsRenderTargetRenderer = true;
			}
			break;
			case ELGUIRenderMode::WorldSpace_LGUI:
			{
				if (!bHasSetIntialStateforLGUIWorldSpaceRenderer)
				{
					auto ViewExtension = ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true);

					if (ViewExtension.IsValid())//only root canvas can add screen space UI to LGUIRenderer
					{
						//put initial code here
						bHasSetIntialStateforLGUIWorldSpaceRenderer = true;
					}
				}
			}
			break;
			}
		}
		
		if (CheckUIItem())
		{
			if (UpdateCanvasDrawCallRecursive())
			{
				MarkFinishRenderFrameRecursive();
			}
		}

		if (bIsRenderTargetRenderer)
		{
			bool bCanUpdateRenderTarget = false;
			switch (RenderTargetUpdateMode)
			{
			default:
			case ELGUICanvasRenderTargetUpdateMode::Automatic:
			{
				if (bAnythingChangedForRenderTarget || bPrevAnythingChangedForRenderTarget || bRequestUpdateForRenderTarget)
				{
					bPrevAnythingChangedForRenderTarget = bAnythingChangedForRenderTarget;
					bAnythingChangedForRenderTarget = false;
					bRequestUpdateForRenderTarget = false;
					bCanUpdateRenderTarget = true;
				}
			}
				break;
			case ELGUICanvasRenderTargetUpdateMode::Always:
				bCanUpdateRenderTarget = true;
				break;
			case ELGUICanvasRenderTargetUpdateMode::WhenRequest:
			{
				if (bRequestUpdateForRenderTarget)
				{
					bRequestUpdateForRenderTarget = false;
					bCanUpdateRenderTarget = true;
				}
			}
				break;
			}
			if (bCanUpdateRenderTarget)
			{
				UpdateRenderTarget(true);
#if WITH_EDITOR
				if (!this->GetWorld()->IsGameWorld())
				{
					if (!renderTarget->GameThread_GetRenderTargetResource())
					{
						renderTarget->InitCustomFormat(renderTarget->SizeX, renderTarget->SizeY, EPixelFormat::PF_B8G8R8A8, false);
					}
				}
#endif
				if (RenderTargetViewExtension.IsValid())
				{
					RenderTargetViewExtension->UpdateRenderTargetRenderer(renderTarget);
				}
			}
		}
	}
}

void ULGUICanvas::UpdateRenderTarget(bool CallEvent)
{
	FIntPoint DesiredRenderTargetSize(UIItem->GetWidth() * RenderTargetResolutionScale, UIItem->GetHeight() * RenderTargetResolutionScale);
	static const int32 MaxAllowedDrawSize = GetMax2DTextureDimension();
	if (DesiredRenderTargetSize.X <= 0 || DesiredRenderTargetSize.Y <= 0)
	{
		return;
	}
	DesiredRenderTargetSize.X = FMath::Min(DesiredRenderTargetSize.X, MaxAllowedDrawSize);
	DesiredRenderTargetSize.Y = FMath::Min(DesiredRenderTargetSize.Y, MaxAllowedDrawSize);

	if (renderTarget == nullptr)
	{
		renderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, EObjectFlags::RF_Transient);
		renderTarget->AddressX = TextureAddress::TA_Clamp;
		renderTarget->AddressY = TextureAddress::TA_Clamp;
		renderTarget->ClearColor = FLinearColor::Transparent;
		renderTarget->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
		if (CallEvent)
		{
			OnRenderTargetCreatedOrChanged.Broadcast(renderTarget, true);
		}
	}
	else
	{
		switch (RenderTargetSizeMode)
		{
		case ELGUICanvasRenderTargetSizeMode::None:
		case ELGUICanvasRenderTargetSizeMode::CanvasFitToRenderTarget:
			if (renderTarget != nullptr)
			{
				DesiredRenderTargetSize.X = renderTarget->SizeX;
				DesiredRenderTargetSize.Y = renderTarget->SizeY;
			}
			break;
		case ELGUICanvasRenderTargetSizeMode::RenderTargetFitToCanvas:
			break;
		}
		if (renderTarget->SizeX != DesiredRenderTargetSize.X || renderTarget->SizeY != DesiredRenderTargetSize.Y)
		{
			renderTarget->ClearColor = FLinearColor::Transparent;
			renderTarget->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
			renderTarget->UpdateResourceImmediate();
#if WITH_EDITOR
			renderTarget->Modify();
#endif
			if (CallEvent)
			{
				OnRenderTargetCreatedOrChanged.Broadcast(renderTarget, false);
			}
		}
	}
}

void ULGUICanvas::EnsureDrawCallObjectReference()
{
	for (int i = 0; i < UIRenderableList.Num(); i++)
	{
		if (!IsValid(UIRenderableList[i]))
		{
			UIRenderableList.RemoveAt(i);
			i--;
		}
	}

	for (const auto& DrawCallItem : UIDrawCallList)
	{
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchGeometry:
		{
			for (int i = 0; i < DrawCallItem->BatchMeshRenderObjectList.Num(); i++)
			{
				if (!DrawCallItem->BatchMeshRenderObjectList[i].IsValid())
				{
					DrawCallItem->BatchMeshRenderObjectList.RemoveAt(i);
					i--;
				}
			}
		}
		break;
		}
	}
}

void ULGUICanvas::OnRegister()
{
	Super::OnRegister();
	if (CheckUIItem())
	{
		ULGUIManagerWorldSubsystem::AddCanvas(this, CurrentRenderMode);
		//tell UIItem
		UIItem->RegisterRenderCanvas(this);
		UIHierarchyChangedDelegateHandle = UIItem->RegisterUIHierarchyChanged(FSimpleDelegate::CreateUObject(this, &ULGUICanvas::OnUIHierarchyChanged));
		UIActiveStateChangedDelegateHandle = UIItem->RegisterUIActiveStateChanged(FUIItemActiveInHierarchyStateChangedDelegate::CreateUObject(this, &ULGUICanvas::OnUIActiveStateChanged));

		OnUIHierarchyChanged();
	}

	if (!IsValid(ClipDataAsTexture))
	{
		ClipDataAsTexture = NewObject<ULexUIDataAsTexture>(this, ULexUIDataAsTexture::StaticClass(), NAME_None, RF_Transient);
		ClipDataAsTexture->Init(FLexUIClipData::BlockSizeInBytes, 512);
		ClipDataAsTexture->OnDataTextureChange.AddUObject(this, &ULGUICanvas::OnClipDataTextureChanged);
		ClipDataAsTexture->RegisterBuffer();//register a zero position as a placeholder for not clipping type.
	}
}
void ULGUICanvas::OnUnregister()
{
	Super::OnUnregister();
	ULGUIManagerWorldSubsystem::RemoveCanvas(this, CurrentRenderMode);

	ClipDataList.Empty();
	
	{
		//these three functions is from OnUIHierarchyChanged()
		RemoveFromViewExtension(true);
		CheckRootCanvas(true);
		CheckRenderMode(true);
	}

	//tell UIItem
	if (UIItem.IsValid())
	{
		UIItem->UnregisterRenderCanvas();
		UIItem->UnregisterUIHierarchyChanged(UIHierarchyChangedDelegateHandle);
		UIItem->UnregisterUIActiveStateChanged(UIActiveStateChangedDelegateHandle);
	}
}
void ULGUICanvas::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);
	if (UIMesh.IsValid())
	{
		UIMesh->DestroyComponent();
		UIMesh = nullptr;
	}
}

void ULGUICanvas::ClearDrawCall()
{
	if (UIMesh.IsValid())
	{
		UIMesh->ClearRenderData();
		bUIMeshNeedToSetInitialParameters = true;
	}

	PooledUIMaterialList.Empty();
	UIDrawCallList.Empty();
	CacheUIDrawCallList.Empty();
}

void ULGUICanvas::RemoveFromViewExtension(bool PropogateToChildrenCanvas)
{
	if (bHasAddToLGUIScreenSpaceRenderer)
	{
		bHasAddToLGUIScreenSpaceRenderer = false;
		if (RenderTargetViewExtension.IsValid())//could be RenderTarget mode
		{
			RenderTargetViewExtension->ClearScreenSpaceRootCanvas();
		}
		else//if not RenderTarget mode, then should be ScreenSpaceOverlay
		{
			auto ViewExtension = ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
			if (ViewExtension.IsValid())
			{
				ViewExtension->ClearScreenSpaceRootCanvas();
			}
		}
	}
	if (bHasSetIntialStateforLGUIWorldSpaceRenderer)
	{
		bHasSetIntialStateforLGUIWorldSpaceRenderer = false;
	}

	if (PropogateToChildrenCanvas)
	{
		for (const auto& ChildCanvas : ChildrenCanvasArray)
		{
			if (ChildCanvas.IsValid())
			{
				ChildCanvas->RemoveFromViewExtension(PropogateToChildrenCanvas);
			}
		}
	}
}

bool ULGUICanvas::CheckRootCanvas(bool forceRecheck)const
{
	if (forceRecheck)
	{
		if (RootCanvas.IsValid())
		{
			RootCanvas = nullptr;
		}
	}
	if (RootCanvas.IsValid())return true;
	if (this->GetWorld() == nullptr)return false;
	auto FindRootCanvas = [](AActor* Actor)
	{
		ULGUICanvas* ResultCanvas = nullptr;
		auto ParentActor = Actor;
		while (ParentActor != nullptr
			&& Cast<UUIItem>(ParentActor->GetRootComponent()) != nullptr//root must be UI component
			)
		{
			auto FoundCanvas = ParentActor->FindComponentByClass<ULGUICanvas>();
			if (FoundCanvas)
			{
				ResultCanvas = FoundCanvas;
			}
			ParentActor = ParentActor->GetAttachParentActor();
		}
		return ResultCanvas;
	};
	RootCanvas = FindRootCanvas(this->GetOwner());
	if (RootCanvas.IsValid())
	{
		return true;
	}
	return false;
}

void ULGUICanvas::SetParentCanvas(ULGUICanvas* InParentCanvas)
{
	if (ParentCanvas != InParentCanvas)
	{
		this->ClearDrawCall();
		this->MarkCanvasUpdate(false, false, true, true);
		if (ParentCanvas.IsValid())
		{
			//if render as child, then delete render section
			if (DrawCallAsChildCanvas.IsValid() && DrawCallAsChildCanvas->DrawCallRenderSection.IsValid())
			{
				DrawCallAsChildCanvas->DrawCallMesh->DeleteRenderSection(DrawCallAsChildCanvas->DrawCallRenderSection.Pin());
				DrawCallAsChildCanvas->DrawCallRenderSection = nullptr;
			}
			this->DrawCallAsChildCanvas = nullptr;

			ParentCanvas->ChildrenCanvasArray.Remove(this);
			ParentCanvas->UIRenderableList.Remove(this->UIItem.Get());
			ParentCanvas->MarkCanvasUpdate(false, false, true, true);
		}
		ParentCanvas = InParentCanvas;
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->UIRenderableList.AddUnique(this->UIItem.Get());
			ParentCanvas->ChildrenCanvasArray.AddUnique(this);
			ParentCanvas->MarkCanvasUpdate(false, false, true, true);
		}
	}
}

bool ULGUICanvas::CheckUIItem()const
{
	if (UIItem.IsValid())return true;
	if (this->GetWorld() == nullptr)return false;
	UIItem = Cast<UUIItem>(GetOwner()->GetRootComponent());
	if (!UIItem.IsValid())
	{
		if (this->IsRegistered())
		{
			UE_LOG(LGUI, Warning, TEXT("LGUICanvas component should only attach to a actor which have UIItem as RootComponent! %s"), *this->GetPathName());
#if !UE_BUILD_SHIPPING
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
		}
		return false;
	}
	else
	{
		return true;
	}
}
void ULGUICanvas::CheckRenderMode(bool PropogateToChildrenCanvas)
{
	const auto OldRenderMode = CurrentRenderMode;
	if (this->IsRegistered())
	{
		if (CheckRootCanvas(true))
		{
			CurrentRenderMode = RootCanvas->GetRenderMode();
		}
		else
		{
			CurrentRenderMode = ELGUIRenderMode::None;
		}
	}
	else
	{
		CurrentRenderMode = ELGUIRenderMode::None;
	}
	//if render space changed, we need to change recreate all render data
	if (CurrentRenderMode != OldRenderMode)
	{
		if (CheckUIItem())
		{
			UIItem->MarkRenderModeChangeRecursive(this, OldRenderMode, CurrentRenderMode);
		}
		//clear drawcall, delete mesh, because UE/LGUI render's mesh data not compatible
		this->ClearDrawCall();

		ULGUIManagerWorldSubsystem::CanvasRenderModeChange(this, OldRenderMode, CurrentRenderMode);
		OnRenderModeChanged.Broadcast(this, OldRenderMode, CurrentRenderMode);
	}

	if (PropogateToChildrenCanvas)
	{
		for (const auto& ChildCanvas : ChildrenCanvasArray)
		{
			if (ChildCanvas.IsValid())
			{
				ChildCanvas->CheckRenderMode(PropogateToChildrenCanvas);
			}
		}
	}
}
void ULGUICanvas::OnUIHierarchyChanged()
{
	this->bCanTickUpdate = true;
	RemoveFromViewExtension(true);
	CheckRootCanvas(true);
	CheckRenderMode(true);

	ULGUICanvas* NewParentCanvas = nullptr;
	if (this->IsRegistered())
	{
		NewParentCanvas = UUIItem::GetComponentInParentUI<ULGUICanvas>(this->GetOwner()->GetAttachParentActor(), true);
	}
	SetParentCanvas(NewParentCanvas);
}

void ULGUICanvas::OnUIActiveStateChanged(bool value)
{
	if (value)
	{
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->UIRenderableList.AddUnique(this->UIItem.Get());
			ParentCanvas->MarkCanvasUpdate(false, false, true//why make this to true? becase we need to sort UIRenderableList, and set bShouldSortRenderableOrder to true can do it
				, true);

		}
	}
	else
	{
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->UIRenderableList.Remove(this->UIItem.Get());
			ParentCanvas->MarkCanvasUpdate(false, false, false, true);
		}
	}
}

bool ULGUICanvas::IsRenderToScreenSpace()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->renderMode == ELGUIRenderMode::ScreenSpaceOverlay;
	}
	return false;
}
bool ULGUICanvas::IsRenderToRenderTarget()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->renderMode == ELGUIRenderMode::RenderTarget;
	}
	return false;
}
bool ULGUICanvas::IsRenderToWorldSpace()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->renderMode == ELGUIRenderMode::WorldSpace
			|| RootCanvas->renderMode == ELGUIRenderMode::WorldSpace_LGUI
			;
	}
	return false;
}

bool ULGUICanvas::IsRenderByLGUIRendererOrUERenderer()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->renderMode == ELGUIRenderMode::ScreenSpaceOverlay
			|| RootCanvas->renderMode == ELGUIRenderMode::RenderTarget
			|| RootCanvas->renderMode == ELGUIRenderMode::WorldSpace_LGUI
			;
	}
	return false;
}

void ULGUICanvas::MarkCanvasUpdate(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall)
{
	this->bCanTickUpdate = true;
	if (bMaterialOrTextureChanged || bTransformOrVertexPositionChanged || bHierarchyOrderChanged || bForceRebuildDrawCall)
	{
		this->bShouldRebuildDrawCall = true;
	}
	if (bHierarchyOrderChanged)
	{
		this->bShouldSortRenderableOrder = true;
	}
}
void ULGUICanvas::MarkCanvasUpdateRecursive(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall)
{
	this->MarkCanvasUpdate(bMaterialOrTextureChanged, bTransformOrVertexPositionChanged, bHierarchyOrderChanged, bForceRebuildDrawCall);
	for (auto& ChildCanvas : this->ChildrenCanvasArray)
	{
		ChildCanvas->MarkCanvasUpdateRecursive(bMaterialOrTextureChanged, bTransformOrVertexPositionChanged, bHierarchyOrderChanged, bForceRebuildDrawCall);
	}
}

#if WITH_EDITOR
bool ULGUICanvas::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{

	}

	return Super::CanEditChange(InProperty);
}
void ULGUICanvas::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (CheckUIItem())
	{
		UIItem->MarkAllDirtyRecursive();
	}
	if (CheckRootCanvas())
	{
		RootCanvas->MarkCanvasUpdate(true, true, true);
	}
}
void ULGUICanvas::PostLoad()
{
	Super::PostLoad();
}
void ULGUICanvas::PostEditUndo()
{
	Super::PostEditUndo();

	ULGUIManagerWorldSubsystem::RefreshAllUI(this->GetWorld());
}
void ULGUICanvas::EnsureDataForRebuild()
{
	struct LOCAL
	{
		static void RecheckRootCanvasRecursive(ULGUICanvas* Target)
		{
			Target->CheckRootCanvas(true);
			Target->MarkCanvasUpdate(true, true, true, true);
			Target->CheckRenderMode(false);
			Target->bShouldClearCachedDrawCall = true;
			for (int i = Target->ChildrenCanvasArray.Num() - 1; i >= 0; i--)
			{
				auto ChildCanvas = Target->ChildrenCanvasArray[i];
				if (ChildCanvas.IsValid())
				{
					RecheckRootCanvasRecursive(ChildCanvas.Get());
				}
				else
				{
					Target->ChildrenCanvasArray.RemoveAt(i);
				}
			}
		}
	};
	EnsureDrawCallObjectReference();
	ULGUIPrefabManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]() {
		if (WeakThis.IsValid())
		{
			LOCAL::RecheckRootCanvasRecursive(WeakThis.Get());
		}
		}, 0);
}
#endif

ULGUICanvas* ULGUICanvas::GetRootCanvas() const
{ 
	CheckRootCanvas(); 
	return RootCanvas.Get(); 
}
bool ULGUICanvas::IsRootCanvas()const
{
	return GetRootCanvas() == this;
}

bool ULGUICanvas::GetIsUIActive()const
{
	if (UIItem.IsValid())
	{
		return UIItem->GetIsUIActiveInHierarchy();
	}
	return false;
}

void ULGUICanvas::AddUIRenderable(UUIBaseRenderable* InUIRenderable)
{
	UIRenderableList.AddUnique(InUIRenderable);
	MarkCanvasUpdate(false, false, true);
}

void ULGUICanvas::RemoveUIRenderable(UUIBaseRenderable* UIRenderableItem)
{
	if (UIRenderableList.Remove(UIRenderableItem) > 0)
	{
		auto DrawCall = UIRenderableItem->drawcall;
		if (DrawCall.IsValid())
		{
			switch (UIRenderableItem->GetUIRenderableType())
			{
			case EUIRenderableType::UIBatchMeshRenderable:
			{
				auto UIBatchMeshRenderable = (UUIBatchMeshRenderable*)UIRenderableItem;
				auto index = DrawCall->BatchMeshRenderObjectList.IndexOfByKey(UIBatchMeshRenderable);
				if (index != INDEX_NONE)
				{
					DrawCall->BatchMeshRenderObjectList.RemoveAt(index);
					DrawCall->bNeedToUpdateVertex = true;
				}
			}
			break;
			case EUIRenderableType::UIDirectMeshRenderable:
			{
				if (DrawCall->DirectMeshRenderableObject.IsValid())
				{
					DrawCall->DirectMeshRenderableObject->ClearMeshData();
				}
			}
			break;
			}
			UIRenderableItem->drawcall = nullptr;
		}
		MarkCanvasUpdate(false, false, true);
	}
}

void ULGUICanvas::AddUIItem(UUIItem* InUIItem)
{
	UIItemList.AddUnique(InUIItem);
	MarkCanvasUpdate(false, false, false);
}
void ULGUICanvas::RemoveUIItem(UUIItem* InUIItem)
{
	UIItemList.Remove(InUIItem);
	MarkCanvasUpdate(false, false, false);
}

void ULGUICanvas::SetRequireNormalAndTangent(bool Value)
{
	if (bRequireNormalAndTangent != Value)
	{
		bRequireNormalAndTangent = Value;
		MarkCanvasUpdate(false, false, false);
	}
}

bool ULGUICanvas::Is2DUITransform(const FTransform& Transform)
{
#if WITH_EDITOR
	float threshold = ULGUISettings::GetAutoBatchThreshold();
#else
	static float threshold = ULGUISettings::GetAutoBatchThreshold();
#endif
	if (FMath::Abs(Transform.GetLocation().X) > threshold)//location X moved
	{
		return false;
	}
	const auto rotation = Transform.GetRotation().Rotator();
	if (FMath::Abs(rotation.Yaw) > threshold || FMath::Abs(rotation.Pitch) > threshold)//rotate
	{
		return false;
	}
	return true;
}

void ULGUICanvas::UpdateGeometry_Implement()
{
	//hierarchy change, need to sort it
	if (bShouldSortRenderableOrder)
	{
		bShouldSortRenderableOrder = false;
		UIRenderableList.Sort([](const UUIItem& A, const UUIItem& B) {
			return A.GetFlattenHierarchyIndex() < B.GetFlattenHierarchyIndex();
			});
	}
	for (int i = 0; i < UIRenderableList.Num(); i++)
	{
		auto& Item = UIRenderableList[i];
		//check(Item->GetIsUIActiveInHierarchy());
		if (!Item->GetIsUIActiveInHierarchy())continue;
		if (!Item->GetRenderCanvas())continue;

		if (Item->IsCanvasUIItem() && Item->GetRenderCanvas() != this)//is child canvas
		{
			//auto ChildRenderCanvas = Item->GetRenderCanvas();
		}
		else
		{
			const auto UIRenderableItem = (UUIBaseRenderable*)(Item);
			UIRenderableItem->UpdateGeometry();
		}
	}
}

#define LGUI_Test_ResetRenderObjectList 0

DECLARE_CYCLE_STAT(TEXT("Canvas BatchDrawCall"), STAT_BatchDrawCall, STATGROUP_LGUI);
void ULGUICanvas::BatchDrawCall_Implement(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop, TArray<TSharedPtr<FLexUIDrawCall>>& InUIDrawCallList, TArray<TSharedPtr<FLexUIDrawCall>>& InCacheUIDrawCallList, bool& OutNeedToSortRenderPriority)
{
	SCOPE_CYCLE_COUNTER(STAT_BatchDrawCall);
	
	auto CanvasRect = LexUIQuadTree::Rectangle(InCanvasLeftBottom, InCanvasRightTop);

	auto IntersectBounds = [](FVector2D aMin, FVector2D aMax, FVector2D bMin, FVector2D bMax) {
		return !(bMin.X >= aMax.X
			|| bMax.X <= aMin.X
			|| bMax.Y <= aMin.Y
			|| bMin.Y >= aMax.Y
			);
	};
	auto OverlapWithOtherDrawCall = [&](FLexUIGeometry* ThisUIGeo, const TSharedPtr<FLexUIDrawCall>& OtherDrawCallItem) {
		switch (OtherDrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchGeometry:
			{
				//compare draw-call item's bounds
				if (OtherDrawCallItem->BatchMeshTreeNode->Overlap(LexUIQuadTree::Rectangle(ThisUIGeo->BoundsMin2DInCanvasSpace, ThisUIGeo->BoundsMax2DInCanvasSpace)))
				{
					return true;
				}
			}
			break;
		case ELexUIDrawCallType::PostProcess:
			{
				auto OtherUIGeo = OtherDrawCallItem->PostProcessRenderableObject->GetGeometry();
				//check bounds overlap
				if (IntersectBounds(ThisUIGeo->BoundsMin2DInCanvasSpace, ThisUIGeo->BoundsMax2DInCanvasSpace, OtherUIGeo->BoundsMin2DInCanvasSpace, OtherUIGeo->BoundsMax2DInCanvasSpace))
				{
					return true;
				}
			}
			break;
		case ELexUIDrawCallType::DirectMesh://mostly direct mesh are difficult to calculate 2d bounds (particles or static-mesh), so just return true-overlap
				return true;
		}

		return false;
	};

	int FitInDrawCallMinIndex = InUIDrawCallList.Num();//0 means the first canvas that processing draw-call. if not 0 means this is child canvas, then we should skip the previous canvas when batch draw-call, because child canvas's UI element can't batch into other canvas's drawcall
	auto CanFitInDrawCall = [&](const UUIBatchMeshRenderable* InUIItem, bool InIs2DUI, int32 InUIItemVerticesCount, int32& OutDrawCallIndexToFitin)
	{
		const auto LastDrawCallIndex = InUIDrawCallList.Num() - 1;
		if (LastDrawCallIndex < 0)
		{
			return false;
		}

		if (!InIs2DUI)
		{
			//3d UI can only batch into last draw-call
			const auto LastDrawCall = InUIDrawCallList[LastDrawCallIndex];
			if (LastDrawCall->CanConsumeUIBatchMeshRenderable(InUIItem->GetGeometry(), InUIItemVerticesCount))
			{
				OutDrawCallIndexToFitin = LastDrawCallIndex;
				return true;
			}
			return false;
		}
		static TArray<int> CanFitinDrawCallIndexArray;
		CanFitinDrawCallIndexArray.Reset();
		//get all draw-call that can fit-in this UI item, then use the first one (because we iterate from tail to head)
		for (int i = LastDrawCallIndex; i >= FitInDrawCallMinIndex; i--)//from tail to head
		{
			const auto DrawCallItem = InUIDrawCallList[i];
			if (!DrawCallItem->bIs2DSpace)//draw-call is 3d, can't batch
			{
				return false;
			}

			if (!DrawCallItem->CanConsumeUIBatchMeshRenderable(InUIItem->GetGeometry(), InUIItemVerticesCount))//can't fit in this draw-call, should check overlap
			{
				if (OverlapWithOtherDrawCall(InUIItem->GetGeometry(), DrawCallItem))//overlap with other draw-call, can't batch
				{
					if (CanFitinDrawCallIndexArray.Num() > 0)
					{
						OutDrawCallIndexToFitin = CanFitinDrawCallIndexArray[CanFitinDrawCallIndexArray.Num() - 1];
						return true;
					}
					return false;
				}
				continue;//not overlap with other draw-call, keep searching
			}
			CanFitinDrawCallIndexArray.Add(i);
		}
		if (CanFitinDrawCallIndexArray.Num() > 0)
		{
			OutDrawCallIndexToFitin = CanFitinDrawCallIndexArray[CanFitinDrawCallIndexArray.Num() - 1];
			return true;
		}
		return false;
	};

	auto PushSingleDrawCall = [&](UUIItem* InUIItem, bool InSearchInCacheList, const FLexUIGeometry* InItemGeo, ELexUIDrawCallType InDrawCallType, bool InIs2DSpace = true) {
		//if this UIItem exist in InCacheUIDrawCallList, then grab the entire draw-call item (may include other UIItem in RenderObjectList). No need to worry other UIItem, because they could be cleared in further operation, or exist in the same draw-call
		int32 FoundDrawCallIndex = INDEX_NONE;
		if (InSearchInCacheList)
		{
			FoundDrawCallIndex = InCacheUIDrawCallList.IndexOfByPredicate([=](const TSharedPtr<FLexUIDrawCall>& DrawCallItem) {
				if (DrawCallItem->Type == InDrawCallType)
				{
					switch (InDrawCallType)
					{
					case ELexUIDrawCallType::BatchGeometry:
					{
						if (DrawCallItem->BatchMeshRenderObjectList.Contains(InUIItem))
						{
							return true;
						}
					}
					break;
					case ELexUIDrawCallType::PostProcess:
					{
						if (DrawCallItem->PostProcessRenderableObject == InUIItem)
						{
							return true;
						}
					}
					break;
					case ELexUIDrawCallType::DirectMesh:
					{
						if (DrawCallItem->DirectMeshRenderableObject == InUIItem)
						{
							return true;
						}
					}
					break;
					}
				}
				return false;
				});
		}
		TSharedPtr<FLexUIDrawCall> DrawCallItem = nullptr;
		if (FoundDrawCallIndex != INDEX_NONE)//find exist draw-call from old DrawCallList
		{
			DrawCallItem = InCacheUIDrawCallList[FoundDrawCallIndex];
			InCacheUIDrawCallList.RemoveAt(FoundDrawCallIndex);//cannot use "RemoveAtSwap" here, because we need correct order to tell if we should sort render order, see "bNeedToSortRenderPriority"

			switch (InDrawCallType)
			{
			case ELexUIDrawCallType::BatchGeometry:
			{
				DrawCallItem->Texture = InItemGeo->Texture;
				DrawCallItem->Material = InItemGeo->Material.Get();
#if LGUI_Test_ResetRenderObjectList
				DrawCallItem->RenderObjectList.Reset();
				DrawCallItem->RenderObjectList.Add((UUIBatchMeshRenderable*)InUIItem);
#endif
				DrawCallItem->BatchMeshTreeNode = MakeUnique<LexUIQuadTree::Node>(CanvasRect);
				DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(InItemGeo->BoundsMin2DInCanvasSpace, InItemGeo->BoundsMax2DInCanvasSpace));
				DrawCallItem->VerticesCount = InItemGeo->Vertices.Num();
				DrawCallItem->IndicesCount = InItemGeo->Triangles.Num();
			}
			break;
			case ELexUIDrawCallType::PostProcess:
			{
				DrawCallItem->PostProcessRenderableObject = (UUIPostProcessRenderable*)InUIItem;
			}
			break;
			case ELexUIDrawCallType::DirectMesh:
			{
				DrawCallItem->DirectMeshRenderableObject = (UUIDirectMeshRenderable*)InUIItem;
			}
			break;
			}
		}
		else
		{
			switch (InDrawCallType)
			{
			default:
			case ELexUIDrawCallType::BatchGeometry:
			{
				DrawCallItem = MakeShared<FLexUIDrawCall>(CanvasRect);
				DrawCallItem->bNeedToUpdateVertex = true;
				DrawCallItem->Texture = InItemGeo->Texture;
				DrawCallItem->Material = InItemGeo->Material.Get();
				DrawCallItem->BatchMeshRenderObjectList.Add((UUIBatchMeshRenderable*)InUIItem);
				DrawCallItem->VerticesCount = InItemGeo->Vertices.Num();
				DrawCallItem->IndicesCount = InItemGeo->Triangles.Num();
				DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(InItemGeo->BoundsMin2DInCanvasSpace, InItemGeo->BoundsMax2DInCanvasSpace));
				DrawCallItem->DrawCallMesh = UIMesh;
			}
			break;
			case ELexUIDrawCallType::PostProcess:
			{
				DrawCallItem = MakeShared<FLexUIDrawCall>(InDrawCallType);
				DrawCallItem->PostProcessRenderableObject = (UUIPostProcessRenderable*)InUIItem;
				DrawCallItem->DrawCallMesh = UIMesh;
			}
			break;
			case ELexUIDrawCallType::DirectMesh:
			{
				DrawCallItem = MakeShared<FLexUIDrawCall>(InDrawCallType);
				DrawCallItem->DirectMeshRenderableObject = (UUIDirectMeshRenderable*)InUIItem;
				DrawCallItem->DrawCallMesh = UIMesh;
			}
			break;
			}
		}
		DrawCallItem->bIs2DSpace = InIs2DSpace;

		if (InDrawCallType == ELexUIDrawCallType::BatchGeometry
			|| InDrawCallType == ELexUIDrawCallType::PostProcess
			|| InDrawCallType == ELexUIDrawCallType::DirectMesh)
		{
			((UUIBaseRenderable*)InUIItem)->drawcall = DrawCallItem;
		}
		InUIDrawCallList.Add(DrawCallItem);

		if (FoundDrawCallIndex != 0)//if not find draw-call or found draw-call not at head of array, means draw-call list's order is changed compare to cache list, then we need to sort render order
		{
			OutNeedToSortRenderPriority = true;
		}
		//OutNeedToSortRenderPriority = true;//@todo: this line could make it sort every time, which is not good performance
	};
	auto ClearObjectFromDrawCall = [&](const TSharedPtr<FLexUIDrawCall>& InDrawCallItem, UUIBatchMeshRenderable* InUIBatchMeshRenderable) {
		if (InDrawCallItem->DrawCallRenderSection.IsValid())
		{
			InDrawCallItem->DrawCallMesh->DeleteRenderSection(InDrawCallItem->DrawCallRenderSection.Pin());
			InDrawCallItem->DrawCallRenderSection = nullptr;
		}

		InDrawCallItem->bNeedToUpdateVertex = true;
		InDrawCallItem->bMaterialNeedToReassign = true;
		int index = InDrawCallItem->BatchMeshRenderObjectList.IndexOfByKey(InUIBatchMeshRenderable);
		InDrawCallItem->BatchMeshRenderObjectList.RemoveAt(index);
		InUIBatchMeshRenderable->drawcall = nullptr;
	};
	auto ClearChildCanvasFromDrawCall = [&](const TSharedPtr<FLexUIDrawCall>& InDrawCallItem, ULGUICanvas* InChildCanvas) {
		if (InDrawCallItem->DrawCallRenderSection.IsValid())
		{
			InDrawCallItem->DrawCallMesh->DeleteRenderSection(InDrawCallItem->DrawCallRenderSection.Pin());
			InDrawCallItem->DrawCallRenderSection = nullptr;
		}

		InChildCanvas->DrawCallAsChildCanvas = nullptr;
	};

	//for sorted ui items, iterate from head to tail, compare draw-call from tail to head
	for (int i = 0; i < UIRenderableList.Num(); i++)
	{
		auto& Item = UIRenderableList[i];
		//check(Item->GetIsUIActiveInHierarchy());
		if (!Item->GetIsUIActiveInHierarchy())continue;
		
		if (Item->IsCanvasUIItem() && Item->GetRenderCanvas() != this)//is child canvas
		{
			auto ChildCanvas = Item->GetRenderCanvas();
			if (ChildCanvas == nullptr)continue;//normally this won't be nullptr, but when redo in editor this breaks
			if (!ChildCanvas->GetOverrideSorting())
			{
				if (InCacheUIDrawCallList.Num() > 0)
				{
					int FoundIndex = InCacheUIDrawCallList.IndexOfByPredicate([ChildCanvas](const TSharedPtr<FLexUIDrawCall>& CacheDrawCallItem) {
						return CacheDrawCallItem->Type == ELexUIDrawCallType::ChildCanvas && CacheDrawCallItem->ChildCanvas == ChildCanvas;
						});
					if (FoundIndex != INDEX_NONE)
					{
						InUIDrawCallList.Add(InCacheUIDrawCallList[FoundIndex]);
						InCacheUIDrawCallList.RemoveAt(FoundIndex);
					}
					else
					{
						auto OldDrawCall = ChildCanvas->DrawCallAsChildCanvas;
						if (OldDrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
						{
							ClearChildCanvasFromDrawCall(OldDrawCall, ChildCanvas);
						}
						auto ChildCanvasDrawCall = MakeShared<FLexUIDrawCall>(ELexUIDrawCallType::ChildCanvas);
						ChildCanvasDrawCall->ChildCanvas = ChildCanvas;
						ChildCanvasDrawCall->DrawCallMesh = UIMesh;
						ChildCanvas->DrawCallAsChildCanvas = ChildCanvasDrawCall;
						InUIDrawCallList.Add(ChildCanvasDrawCall);
					}
					if (FoundIndex != 0)//if not find draw-call or found draw-call not at head of array, means draw-call list's order is changed compare to cache list, then we need to sort render order
					{
						OutNeedToSortRenderPriority = true;
					}
				}
				else
				{
					auto OldDrawCall = ChildCanvas->DrawCallAsChildCanvas;
					if (OldDrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
					{
						ClearChildCanvasFromDrawCall(OldDrawCall, ChildCanvas);
					}
					auto ChildCanvasDrawCall = MakeShared<FLexUIDrawCall>(ELexUIDrawCallType::ChildCanvas);
					ChildCanvasDrawCall->ChildCanvas = ChildCanvas;
					ChildCanvasDrawCall->DrawCallMesh = UIMesh;
					ChildCanvas->DrawCallAsChildCanvas = ChildCanvasDrawCall;
					InUIDrawCallList.Add(ChildCanvasDrawCall);
					OutNeedToSortRenderPriority = true;
				}

				FitInDrawCallMinIndex = InUIDrawCallList.Num();
			}
		}
		else
		{
			auto UIRenderableItem = (UUIBaseRenderable*)(Item);
			switch (UIRenderableItem->GetUIRenderableType())
			{
			default:
			case EUIRenderableType::UIBatchMeshRenderable:
			{
				auto UIBatchMeshRenderableItem = (UUIBatchMeshRenderable*)UIRenderableItem;
				auto ItemGeo = UIBatchMeshRenderableItem->GetGeometry();
				if (ItemGeo == nullptr)continue;
				if (ItemGeo->Vertices.Num() == 0)continue;
				if (ItemGeo->Vertices.Num() > LEXUI_MAX_VERTEX_COUNT)continue;

				bool is2DUIItem = Is2DUITransform(ItemGeo->TransformRelativeToCanvas);
				int DrawCallIndexToFitin;
				if (UIBatchMeshRenderableItem->SupportDrawCallBatching() && CanFitInDrawCall(UIBatchMeshRenderableItem, is2DUIItem, ItemGeo->Vertices.Num(), DrawCallIndexToFitin))
				{
					auto DrawCallItem = InUIDrawCallList[DrawCallIndexToFitin];
					DrawCallItem->bIs2DSpace = DrawCallItem->bIs2DSpace && is2DUIItem;
					if (UIBatchMeshRenderableItem->drawcall == DrawCallItem)//already exist in this draw-call (added previously)
					{
#if LGUI_Test_ResetRenderObjectList
						DrawCallItem->RenderObjectList.Add(UIBatchMeshRenderableItem);
#else
						//mark sort list
						DrawCallItem->bNeedToSortBatchMeshRenderObjectList = true;
#endif
						//update tree
						DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(ItemGeo->BoundsMin2DInCanvasSpace, ItemGeo->BoundsMax2DInCanvasSpace));
						DrawCallItem->VerticesCount += ItemGeo->Vertices.Num();
						DrawCallItem->IndicesCount += ItemGeo->Triangles.Num();
					}
					else//not exist in this draw-call
					{
						auto OldDrawCall = UIBatchMeshRenderableItem->drawcall;
						if (OldDrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
						{
							ClearObjectFromDrawCall(OldDrawCall, UIBatchMeshRenderableItem);
						}
						//add to this draw-call
						DrawCallItem->BatchMeshRenderObjectList.Add(UIBatchMeshRenderableItem);
						DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(ItemGeo->BoundsMin2DInCanvasSpace, ItemGeo->BoundsMax2DInCanvasSpace));
						DrawCallItem->VerticesCount += ItemGeo->Vertices.Num();
						DrawCallItem->IndicesCount += ItemGeo->Triangles.Num();
						DrawCallItem->bNeedToUpdateVertex = true;
						UIBatchMeshRenderableItem->drawcall = DrawCallItem;
						//copy update state from old to new
						if (OldDrawCall.IsValid())
						{
							OldDrawCall->CopyUpdateState(DrawCallItem.Get());
						}
					}
					check(DrawCallItem->VerticesCount < LEXUI_MAX_VERTEX_COUNT);
				}
				else//cannot fit in any other draw-call
				{
					auto OldDrawCall = UIBatchMeshRenderableItem->drawcall;
					if (OldDrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
					{
						if (InUIDrawCallList.Contains(OldDrawCall))//if this draw-call already exist (added previously), then remove the object from the draw-call.
						{
							ClearObjectFromDrawCall(OldDrawCall, UIBatchMeshRenderableItem);
						}
					}
					//make a new draw-call
					PushSingleDrawCall(UIBatchMeshRenderableItem, true, ItemGeo, ELexUIDrawCallType::BatchGeometry, is2DUIItem);
					check(UIBatchMeshRenderableItem->drawcall->VerticesCount < LEXUI_MAX_VERTEX_COUNT);
				}
			}
			break;
			case EUIRenderableType::UIPostProcessRenderable:
			{
				auto UIPostProcessRenderableItem = (UUIPostProcessRenderable*)UIRenderableItem;
				if (!UIPostProcessRenderableItem->HaveValidData())continue;
				//every postprocess is a draw-call
				bool is2DUIItem = true;//post process just use true because it not matter
				PushSingleDrawCall(UIRenderableItem, true, nullptr, ELexUIDrawCallType::PostProcess, is2DUIItem);
				//no need to copy draw-call's update data for UIPostProcessRenderable, because UIPostProcessRenderable's draw-call should be the same as previous one

				FitInDrawCallMinIndex = InUIDrawCallList.Num();
			}
			break;
			case EUIRenderableType::UIDirectMeshRenderable:
			{
				auto UIDirectMeshRenderableItem = (UUIDirectMeshRenderable*)UIRenderableItem;
				if (!UIDirectMeshRenderableItem->HaveValidData())continue;
				//every direct mesh is a draw-call
				bool is2DUIItem = true;//post process just use true because it not matter
				PushSingleDrawCall(UIRenderableItem, true, nullptr, ELexUIDrawCallType::DirectMesh, is2DUIItem);
				UIDirectMeshRenderableItem->drawcall->Material = UIDirectMeshRenderableItem->GetMaterial();
			}
			break;
			}
		}
	}

	//@todo: the UIRenderableList is already sorted, so actually we better not to sort the RenderObjectList. But when I try to do it (LGUI_Test_ResetRenderObjectList), a RenderObjectList become "Invalid", that is very strange, a TArray can't just become "Invalid".
	//check if we need to sort RenderObjectList
#if !LGUI_Test_ResetRenderObjectList
	for (auto& DrawCallItem : InUIDrawCallList)
	{
		if (DrawCallItem->bNeedToSortBatchMeshRenderObjectList)
		{
			DrawCallItem->bNeedToSortBatchMeshRenderObjectList = false;
			DrawCallItem->BatchMeshRenderObjectList.Sort([](const TWeakObjectPtr<UUIBatchMeshRenderable>& A, const TWeakObjectPtr<UUIBatchMeshRenderable>& B) {
				return A->GetFlattenHierarchyIndex() < B->GetFlattenHierarchyIndex();
				});
		}
	}
#endif
}

void ULGUICanvas::SetOverrideViewLocation(bool InOverride, FVector InValue)
{
	bOverrideViewLocation = InOverride;
	OverrideViewLocation = InValue;
}
void ULGUICanvas::SetOverrideViewRotation(bool InOverride, FRotator InValue)
{
	bOverrideViewRotation = InOverride;
	OverrideViewRotation = InValue;
}
void ULGUICanvas::SetOverrideFovAngle(bool InOverride, float InValue)
{
	bOverrideFovAngle = InOverride;
	OverrideFovAngle = InValue;
}
void ULGUICanvas::SetOverrideProjectionMatrix(bool InOverride, FMatrix InValue)
{
	bOverrideProjectionMatrix = InOverride;
	OverrideProjectionMatrix = InValue;
}

void ULGUICanvas::MarkCanvasLayoutDirty()
{
	bIsViewProjectionMatrixDirty = true;
}

void ULGUICanvas::SetDefaultMeshType(TSubclassOf<ULexUIMeshComponent> InValue)
{
	if (DefaultMeshType != InValue)
	{
		DefaultMeshType = InValue;

		for (int i = 0; i < UIDrawCallList.Num(); i++)
		{
			const auto& DrawCallItem = UIDrawCallList[i];
			DrawCallItem->bNeedToUpdateVertex = true;
			DrawCallItem->DrawCallRenderSection = nullptr;
			DrawCallItem->bMaterialChanged = true;//material is directly used by mesh
		}
		//clear mesh
		if (UIMesh.IsValid())
		{
			UIMesh->DestroyComponent();
			UIMesh = nullptr;
			//if render as child, then delete render section
			if (DrawCallAsChildCanvas.IsValid() && DrawCallAsChildCanvas->DrawCallRenderSection.IsValid())
			{
				DrawCallAsChildCanvas->DrawCallMesh->DeleteRenderSection(DrawCallAsChildCanvas->DrawCallRenderSection.Pin());
				DrawCallAsChildCanvas->DrawCallRenderSection = nullptr;
			}
		}

		MarkCanvasUpdate(true, false, false);
	}
}

void ULGUICanvas::MarkFinishRenderFrameRecursive()
{
	//mark children canvas
	for (const auto& ChildCanvas : ChildrenCanvasArray)
	{
		if (ChildCanvas.IsValid() && ChildCanvas->GetIsUIActive())
		{
			ChildCanvas->MarkFinishRenderFrameRecursive();
		}
	}

	bShouldRebuildDrawCall = false;
}

bool ULGUICanvas::UpdateCanvasDrawCallRecursive()
{
	/**
	 * Why use bPrevUIItemIsActive?:
	 * If Canvas is rendering in frame 1, but when in frame 2 the Canvas is disabled(by disable UIItem), then the Canvas will not do draw-call calculation, and the prev existing draw-call mesh is still there and render,
	 * so we check bPrevUIItemIsActive, then we can still do draw-call calculation at this frame, and the prev existing draw-call will be removed.
	 */
	bool bResult = false;
	const bool bNowUIItemIsActive = UIItem->GetIsUIActiveInHierarchy();
	if (bNowUIItemIsActive || bPrevUIItemIsActive)
	{
		bResult = true;
		bPrevUIItemIsActive = bNowUIItemIsActive;
		//update children canvas
		for (auto& item : ChildrenCanvasArray)
		{
			if (item.IsValid())
			{
				item->UpdateCanvasDrawCallRecursive();
			}
		}
	}

	//update draw-call
	if (bCanTickUpdate)
	{
		bCanTickUpdate = false;
		RootCanvas->bAnythingChangedForRenderTarget = true;

		struct LOCAL
		{
			static void CollectWidgetAndUpdateLayout(UUIItem* Widget
				, ULexUIDataAsTexture* ClipDataTexture
				, TArray<TSharedPtr<FLexUIClipData>>& ClipDataList)
			{
				// Widget->UpdateLayout();
				Widget->UpdateClip(ClipDataTexture, ClipDataList);
				for (auto Child : Widget->GetAttachUIChildren())
				{
					CollectWidgetAndUpdateLayout(Child, ClipDataTexture, ClipDataList);
				}
			}
		};
		LOCAL::CollectWidgetAndUpdateLayout(this->UIItem.Get(), ClipDataAsTexture, ClipDataList);
		for (const auto& ClipData : ClipDataList)
		{
			ClipData->UpdateData();
		}

		UpdateGeometry_Implement();

		if (bShouldRebuildDrawCall)
		{
			CheckUIMesh();
			auto ClearDrawCallData = [this](TArray<TSharedPtr<FLexUIDrawCall>>& DrawCallArray) {
				for (int i = 0; i < DrawCallArray.Num(); i++)
				{
					auto DrawCallInCache = DrawCallArray[i];
					//check(DrawCallInCache->RenderObjectList.Num() == 0);//why comment this?: need to wait until UUIBaseRenderable::OnRenderCanvasChanged.todo finish
					if (DrawCallInCache->DrawCallRenderSection.IsValid())
					{
						DrawCallInCache->DrawCallMesh->DeleteRenderSection(DrawCallInCache->DrawCallRenderSection.Pin());
						DrawCallInCache->DrawCallRenderSection = nullptr;
					}
					if (DrawCallInCache->RenderMaterial.IsValid())
					{
						if (DrawCallInCache->bMaterialContainsLexUIParameter)
						{
							this->AddUIMaterialToPool((UMaterialInstanceDynamic*)DrawCallInCache->RenderMaterial.Get());
						}
						DrawCallInCache->RenderMaterial = nullptr;
						DrawCallInCache->bMaterialContainsLexUIParameter = false;
					}
					if (DrawCallInCache->DirectMeshRenderableObject.IsValid())
					{
						DrawCallInCache->DirectMeshRenderableObject->ClearMeshData();
					}
					if (DrawCallInCache->ChildCanvas.IsValid())
					{
						DrawCallInCache->ChildCanvas->DrawCallAsChildCanvas = nullptr;
					}
				}
				DrawCallArray.Reset();
			};
			if (bShouldClearCachedDrawCall)
			{
				bShouldClearCachedDrawCall = false;
				ClearDrawCallData(UIDrawCallList);
			}
			else
			{
				//store prev created draw-call to cache list, so when we create draw-call, we can search in the cache list and use existing one
				CacheUIDrawCallList.Append(UIDrawCallList);
				UIDrawCallList.Reset();
			}

			//rect size minimal at 100, so UIQuadTree can work properly (prevent too small rect)
			//@todo: use a better size, maybe screen size (only for screen space UI)
			const auto Width = FMath::Max(UIItem->GetWidth(), 100.0f);
			const auto Height = FMath::Max(UIItem->GetHeight(), 100.0f);
			FVector2D LeftBottomPoint;
			LeftBottomPoint.X = Width * -UIItem->GetPivot().X;
			LeftBottomPoint.Y = Height * -UIItem->GetPivot().Y;
			FVector2D RightTopPoint;
			RightTopPoint.X = Width * (1.0f - UIItem->GetPivot().X);
			RightTopPoint.Y = Height * (1.0f - UIItem->GetPivot().Y);
			bool bOutNeedToSortRenderPriority = false;
			BatchDrawCall_Implement(LeftBottomPoint, RightTopPoint, UIDrawCallList, CacheUIDrawCallList
				, bOutNeedToSortRenderPriority//cannot pass a uint32:1 here, so use a temp bool
			);
			if (bOutNeedToSortRenderPriority)
			{
				bNeedToSortRenderPriority = true;
			}

			//for not used draw-calls, clear data
			ClearDrawCallData(CacheUIDrawCallList);
		}

		//update draw-call mesh
		UpdateDrawCallMesh_Implement();

		//update draw-call material
		UpdateDrawCallMaterial_Implement();
	}

	//sort render priority
	{
		if (bNeedToSortRenderPriority)
		{
			bNeedToSortRenderPriority = false;
			if (auto Instance = ULGUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
			{
				switch (this->GetActualRenderMode())
				{
				default:
				case ELGUIRenderMode::ScreenSpaceOverlay:
					Instance->MarkSortScreenSpaceCanvas();
					break;
				case ELGUIRenderMode::WorldSpace_LGUI:
					Instance->MarkSortWorldSpaceLGUICanvas();
					break;
				case ELGUIRenderMode::WorldSpace:
					Instance->MarkSortWorldSpaceCanvas();
					break;
				case ELGUIRenderMode::RenderTarget:
					Instance->MarkSortRenderTargetSpaceCanvas();
					break;
				}
			}
		}
	}

	return bResult;
}

DECLARE_CYCLE_STAT(TEXT("Canvas UpdateDrawCallMesh"), STAT_UpdateDrawCallMesh, STATGROUP_LGUI);
void ULGUICanvas::UpdateDrawCallMesh_Implement()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateDrawCallMesh);

	CheckUIMesh();
	auto MarkRootCanvasNeedToUpdateChildrenCanvasBounds = [this] {
		if (!this->GetOverrideSorting())//if override sorting (render by self) then no need to notify root canvas
		{
			if (RootCanvas.IsValid())
			{
				RootCanvas->bRootCanvasNeedToUpdateChildrenCanvasBounds = true;
				RootCanvas->bCanTickUpdate = true;
			}
		}
	};
	bool bNeedToUpdateBounds = false;
	if (UIDrawCallList.Num() == 0)
	{
		/** 
		 * no draw-call, need to mark it dirty so the previous created SceneProxy will be deleted.
		 * Solve the case: Set child-canvas inactive, but UIMesh of child-canvas did not clear scene-proxy, and the scene-proxy still contains reference of parent-scene-proxy.
		 */
		UIMesh->MarkRenderStateDirty();
	}
	while (ThreadProcessingGeometryCount != 0)
	{
		FPlatformProcess::Sleep(0.001f);
	}
#if 1
	//use ParallelFor to slightly optimize, mainly for the GetCombined function
	FCriticalSection Mutex;
	ParallelFor(UIDrawCallList.Num(), [&](int index)
	{
		auto DrawCallItem = UIDrawCallList[index];
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::DirectMesh:
		{
			auto MeshSection = DrawCallItem->DrawCallRenderSection;
			if (!MeshSection.IsValid())
			{
				Mutex.Lock();
				MeshSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::Mesh);

				DrawCallItem->DrawCallRenderSection = MeshSection;
				DrawCallItem->DirectMeshRenderableObject->OnMeshDataReady();
				UIMesh->CreateRenderSectionRenderData(MeshSection.Pin());
				Mutex.Unlock();
				//create new mesh section, need to sort it
				bNeedToSortRenderPriority = true;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		case ELexUIDrawCallType::BatchGeometry:
		{
			auto RenderSection = DrawCallItem->DrawCallRenderSection;
			if (!RenderSection.IsValid())
			{
				Mutex.Lock();
				RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::Mesh);
				Mutex.Unlock();
				DrawCallItem->DrawCallRenderSection = RenderSection;
				//create new mesh section, need to sort it
				bNeedToSortRenderPriority = true;
				DrawCallItem->bNeedToUpdateVertex = true;
			}
			if (DrawCallItem->bNeedToUpdateVertex)
			{
				auto RenderSectionPtr = RenderSection.Pin();
				check(RenderSectionPtr->Type == ELexUIRenderSectionType::Mesh);
				auto MeshSectionPtr = (FLexUIMeshSection*)RenderSectionPtr.Get();
				MeshSectionPtr->vertices.Reset();
				MeshSectionPtr->triangles.Reset();
				DrawCallItem->GetCombined(MeshSectionPtr->vertices, MeshSectionPtr->triangles);
				if (MeshSectionPtr->prevVertexCount != MeshSectionPtr->vertices.Num() || MeshSectionPtr->prevIndexCount != MeshSectionPtr->triangles.Num())
				{
					MeshSectionPtr->prevVertexCount = MeshSectionPtr->vertices.Num();
					MeshSectionPtr->prevIndexCount = MeshSectionPtr->triangles.Num();
					Mutex.Lock();
					UIMesh->CreateRenderSectionRenderData(RenderSectionPtr);
					Mutex.Unlock();
				}
				else
				{
					UIMesh->UpdateMeshSectionRenderData(RenderSectionPtr, true, GetActualRequireNormalAndTangent());
				}
				DrawCallItem->bNeedToUpdateVertex = false;
				DrawCallItem->bVertexPositionChanged = false;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		case ELexUIDrawCallType::PostProcess:
		{
			//only LGUI renderer can render post process
			if (this->GetActualRenderMode() == ELGUIRenderMode::WorldSpace)
			{
				return;
			}

			if (!DrawCallItem->DrawCallRenderSection.IsValid())
			{
				auto RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::PostProcess);
				auto ChildCanvasSection = (FLexUIPostProcessSection*)RenderSection.Get();
				ChildCanvasSection->PostProcessRenderableObject = DrawCallItem->PostProcessRenderableObject;
				Mutex.Lock();
				UIMesh->CreateRenderSectionRenderData(RenderSection);
				Mutex.Unlock();
				DrawCallItem->DrawCallRenderSection = RenderSection;
				//create new section, need to sort it
				bNeedToSortRenderPriority = true;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		case ELexUIDrawCallType::ChildCanvas:
		{
			if (!DrawCallItem->DrawCallRenderSection.IsValid())
			{
				auto RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::ChildCanvas);
				auto ChildCanvasSection = (FLexUIChildCanvasSection*)RenderSection.Get();
				ChildCanvasSection->ChildCanvasMeshComponent = DrawCallItem->ChildCanvas->GetUIMesh();
				ChildCanvasSection->ChildCanvasMeshComponent->SetParentCanvasMeshComp(this->UIMesh.Get());
				Mutex.Lock();
				UIMesh->CreateRenderSectionRenderData(RenderSection);
				Mutex.Unlock();
				DrawCallItem->DrawCallRenderSection = RenderSection;
				//create new section, need to sort it
				bNeedToSortRenderPriority = true;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		}
	});
#else
	for (int i = 0; i < UIDrawCallList.Num(); i++)
	{
		auto DrawCallItem = UIDrawCallList[i];
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::DirectMesh:
		{
			auto MeshSection = DrawCallItem->DrawCallRenderSection;
			if (!MeshSection.IsValid())
			{
				MeshSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::Mesh);

				DrawCallItem->DrawCallRenderSection = MeshSection;
				DrawCallItem->DirectMeshRenderableObject->OnMeshDataReady();
				UIMesh->CreateRenderSectionRenderData(MeshSection.Pin());
				//create new mesh section, need to sort it
				bNeedToSortRenderPriority = true;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		case ELexUIDrawCallType::BatchGeometry:
		{
			auto RenderSection = DrawCallItem->DrawCallRenderSection;
			if (!RenderSection.IsValid())
			{
				RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::Mesh);
				DrawCallItem->DrawCallRenderSection = RenderSection;
				//create new mesh section, need to sort it
				bNeedToSortRenderPriority = true;
				DrawCallItem->bNeedToUpdateVertex = true;
			}
			if (DrawCallItem->bNeedToUpdateVertex)
			{
				auto RenderSectionPtr = RenderSection.Pin();
				check(RenderSectionPtr->Type == ELexUIRenderSectionType::Mesh);
				auto MeshSectionPtr = (FLexUIMeshSection*)RenderSectionPtr.Get();
				MeshSectionPtr->vertices.Reset();
				MeshSectionPtr->triangles.Reset();
				DrawCallItem->GetCombined(MeshSectionPtr->vertices, MeshSectionPtr->triangles);
				if (MeshSectionPtr->prevVertexCount != MeshSectionPtr->vertices.Num() || MeshSectionPtr->prevIndexCount != MeshSectionPtr->triangles.Num())
				{
					MeshSectionPtr->prevVertexCount = MeshSectionPtr->vertices.Num();
					MeshSectionPtr->prevIndexCount = MeshSectionPtr->triangles.Num();
					UIMesh->CreateRenderSectionRenderData(RenderSectionPtr);
				}
				else
				{
					UIMesh->UpdateMeshSectionRenderData(RenderSectionPtr, true, GetActualRequireNormalAndTangent());
				}
				DrawCallItem->bNeedToUpdateVertex = false;
				DrawCallItem->bVertexPositionChanged = false;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		case ELexUIDrawCallType::PostProcess:
		{
			//only LGUI renderer can render post process
			if (this->GetActualRenderMode() == ELGUIRenderMode::WorldSpace)
			{
				continue;
			}

			if (!DrawCallItem->DrawCallRenderSection.IsValid())
			{
				auto RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::PostProcess);
				auto ChildCanvasSection = (FLexUIPostProcessSection*)RenderSection.Get();
				ChildCanvasSection->PostProcessRenderableObject = DrawCallItem->PostProcessRenderableObject;
				UIMesh->CreateRenderSectionRenderData(RenderSection);
				DrawCallItem->DrawCallRenderSection = RenderSection;
				//create new section, need to sort it
				bNeedToSortRenderPriority = true;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		case ELexUIDrawCallType::ChildCanvas:
		{
			if (!DrawCallItem->DrawCallRenderSection.IsValid())
			{
				auto RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::ChildCanvas);
				auto ChildCanvasSection = (FLexUIChildCanvasSection*)RenderSection.Get();
				ChildCanvasSection->ChildCanvasMeshComponent = DrawCallItem->ChildCanvas->GetUIMesh();
				ChildCanvasSection->ChildCanvasMeshComponent->SetParentCanvasMeshComp(this->UIMesh.Get());
				UIMesh->CreateRenderSectionRenderData(RenderSection);
				DrawCallItem->DrawCallRenderSection = RenderSection;
				//create new section, need to sort it
				bNeedToSortRenderPriority = true;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		}
	}
#endif
	if (this->IsRootCanvas() && this->bRootCanvasNeedToUpdateChildrenCanvasBounds)
	{
		this->bRootCanvasNeedToUpdateChildrenCanvasBounds = false;
		UIMesh->UpdateChildCanvasSectionBox();
	}
	if (bNeedToUpdateBounds)
	{
		UIMesh->UpdateLocalBounds();
	}
}

float ULGUICanvas::GetLastRenderTime()const
{
	auto RenderMode = GetActualRenderMode();
#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld())//edit mode
	{
		if (previewWithLGUIRenderer)
		{
			if (RenderMode == ELGUIRenderMode::ScreenSpaceOverlay)
				RenderMode = ELGUIRenderMode::WorldSpace_LGUI;
		}
		else
		{
			if (RenderMode == ELGUIRenderMode::ScreenSpaceOverlay)
				RenderMode = ELGUIRenderMode::WorldSpace;
		}
	}
#endif
	if (RenderModeIsLGUIRendererOrUERenderer(RenderMode))
	{
		return LastRenderTime;
	}
	else
	{
		return GetUIMesh()->GetLastRenderTime();
	}
}

void ULGUICanvas::CheckUIMesh()const
{
	if (!UIMesh.IsValid())
	{
		auto MeshType = DefaultMeshType.Get();
		if (MeshType == nullptr)MeshType = ULexUIMeshComponent::StaticClass();
		auto ObjectName = MakeUniqueObjectName(this->GetOwner(), MeshType, FName(*this->GetUIItem()->GetDisplayName()));
		UIMesh = NewObject<ULexUIMeshComponent>(this->GetOwner(), MeshType, ObjectName, RF_Transient);
		UIMesh->RegisterComponent();
		UIMesh->AttachToComponent(this->GetOwner()->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		UIMesh->SetRelativeTransform(FTransform::Identity);
		UIMesh->SetRenderCanvas((ULGUICanvas*)this);
		bUIMeshNeedToSetInitialParameters = true;
	}

	if (bUIMeshNeedToSetInitialParameters)
	{
		bUIMeshNeedToSetInitialParameters = false;
		if (RenderModeIsLGUIRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (previewWithLGUIRenderer)
			{
				if (!GetWorld()->IsGameWorld())//edit mode
				{
					if (ActualRenderMode == ELGUIRenderMode::ScreenSpaceOverlay)
						ActualRenderMode = ELGUIRenderMode::WorldSpace_LGUI;
				}
			}
#endif
			switch (ActualRenderMode)
			{
			case ELGUIRenderMode::RenderTarget:
			{
				UIMesh->SetSupportLexUIRenderer(true, this->GetRootCanvas()->GetRenderTargetViewExtension(), false);
#if WITH_EDITOR
				if (!GetWorld()->IsGameWorld())
				{
					UIMesh->SetSupportUERenderer(true);
				}
				else
#endif
				{
					UIMesh->SetSupportUERenderer(false);
				}
			}
			break;
			case ELGUIRenderMode::ScreenSpaceOverlay:
			{
#if WITH_EDITOR
				if (!GetWorld()->IsGameWorld())
				{
					UIMesh->SetSupportLexUIRenderer(true, ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), false);
					UIMesh->SetSupportUERenderer(true);
				}
				else
#endif
				{
					UIMesh->SetSupportLexUIRenderer(true, ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), false);
					UIMesh->SetSupportUERenderer(false);
				}
			}
			break;
			case ELGUIRenderMode::WorldSpace_LGUI:
			{
				UIMesh->SetSupportLexUIRenderer(true, ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), true);
				UIMesh->SetSupportUERenderer(false);
			}
			break;
			}
		}
		else
		{
			UIMesh->SetSupportLexUIRenderer(false, nullptr, false);
			UIMesh->SetSupportUERenderer(true);
		}
	}
}

void ULGUICanvas::SortDrawCall()
{
	UIMesh->SetUITranslucentSortPriority(this->GetActualSortOrder());
	int MeshSectionIndex = 0;
	for (int i = 0; i < UIDrawCallList.Num(); i++)
	{
		auto DrawCallItem = UIDrawCallList[i];
		if (!DrawCallItem->DrawCallRenderSection.IsValid())continue;
		UIMesh->SetRenderSectionRenderPriority(DrawCallItem->DrawCallRenderSection.Pin(), MeshSectionIndex++);
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchGeometry:
		case ELexUIDrawCallType::DirectMesh:
		case ELexUIDrawCallType::PostProcess:
		{
		}
		break;
		case ELexUIDrawCallType::ChildCanvas:
		{
			DrawCallItem->ChildCanvas->SortDrawCall();
		}
		break;
		}
	}
}

FName ULGUICanvas::LexUI_MainTextureMaterialParameterName = FName(TEXT("LexUI_MainTexture"));
FName ULGUICanvas::LexUI_ClipDataTexture_MaterialParameterName = FName(TEXT("LexUI_ClipDataTexture"));

bool ULGUICanvas::IsMaterialContainsLexUIParameter(UMaterialInterface* InMaterial)
{
	static TArray<FMaterialParameterInfo> ParameterInfos;
	static TArray<FGuid> ParameterIds;
	InMaterial->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
	auto FoundIndex = ParameterInfos.IndexOfByPredicate([](const FMaterialParameterInfo& Item)
		{
			return
				Item.Name == LexUI_MainTextureMaterialParameterName
				|| Item.Name == LexUI_ClipDataTexture_MaterialParameterName
				;
		});
	return FoundIndex != INDEX_NONE;
}
void ULGUICanvas::UpdateDrawCallMaterial_Implement()
{
	for (int i = 0; i < UIDrawCallList.Num(); i++)
	{
		auto DrawCallItem = UIDrawCallList[i];
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchGeometry:
		case ELexUIDrawCallType::DirectMesh:
		{
			auto RenderMat = DrawCallItem->RenderMaterial;
			if (!RenderMat.IsValid() || DrawCallItem->bMaterialChanged)
			{
				if (DrawCallItem->Material.IsValid())//custom material
				{
					//the prev RenderMaterial will not be used because we have custom material, so we can try to pool it
					if (RenderMat.IsValid()
						&& RenderMat->IsA(UMaterialInstanceDynamic::StaticClass()))
					{
						this->AddUIMaterialToPool((UMaterialInstanceDynamic*)RenderMat.Get());
					}
					auto SrcMaterial = DrawCallItem->Material.Get();
					auto bContainsLGUIParam = IsMaterialContainsLexUIParameter(SrcMaterial);
					if (SrcMaterial->IsA(UMaterialInstanceDynamic::StaticClass()))//if custom material is UMaterialInstanceDynamic then use it directly
					{
						RenderMat = SrcMaterial;
						UIMesh->SetMeshSectionMaterial(DrawCallItem->DrawCallRenderSection.Pin(), SrcMaterial);
					}
					else//if custom material is not UMaterialInstanceDynamic
					{
						if (bContainsLGUIParam)//if custom material contains LGUI parameters, then LGUI should control these parameters, then we need to create UMaterialInstanceDynamic with the custom material
						{
							RenderMat = UMaterialInstanceDynamic::Create(SrcMaterial, this);
							RenderMat->SetFlags(RF_Transient);
							UIMesh->SetMeshSectionMaterial(DrawCallItem->DrawCallRenderSection.Pin(), RenderMat.Get());
							if (DrawCallItem->DirectMeshRenderableObject.IsValid())
							{
								DrawCallItem->DirectMeshRenderableObject->OnMaterialInstanceDynamicCreated((UMaterialInstanceDynamic*)RenderMat.Get());
							}
							for (auto& RenderObjectItem : DrawCallItem->BatchMeshRenderObjectList)
							{
								RenderObjectItem->OnMaterialInstanceDynamicCreated((UMaterialInstanceDynamic*)RenderMat.Get());
							}
						}
						else//if custom material not contains LGUI parameters, then use it directly
						{
							RenderMat = SrcMaterial;
							UIMesh->SetMeshSectionMaterial(DrawCallItem->DrawCallRenderSection.Pin(), SrcMaterial);
						}
					}
					DrawCallItem->bMaterialContainsLexUIParameter = bContainsLGUIParam;
				}
				else
				{
					RenderMat = this->GetUIMaterialFromPool();
					UIMesh->SetMeshSectionMaterial(DrawCallItem->DrawCallRenderSection.Pin(), RenderMat.Get());
					DrawCallItem->bMaterialContainsLexUIParameter = true;
				}
				DrawCallItem->RenderMaterial = RenderMat;
				DrawCallItem->bMaterialChanged = false;
				if (RenderMat.IsValid() && DrawCallItem->bMaterialContainsLexUIParameter)
				{
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_MainTextureMaterialParameterName, DrawCallItem->Texture.Get());
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_ClipDataTexture_MaterialParameterName, ClipDataAsTexture->GetDataTexture());
				}
				DrawCallItem->bTextureChanged = false;
				DrawCallItem->bMaterialNeedToReassign = false;
				bNeedToVerifyMaterials = true;

				if (DrawCallItem->DirectMeshRenderableObject.IsValid())
				{
					//DrawCallItem->DirectMeshRenderableObject->SetClipType(TempClipType);
				}
			}
			if (DrawCallItem->bTextureChanged)
			{
				DrawCallItem->bTextureChanged = false;
				if (RenderMat.IsValid() && DrawCallItem->bMaterialContainsLexUIParameter)
				{
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_MainTextureMaterialParameterName, DrawCallItem->Texture.Get());
				}
			}
			if (DrawCallItem->bMaterialNeedToReassign)
			{
				DrawCallItem->bMaterialNeedToReassign = false;
				UIMesh->SetMeshSectionMaterial(DrawCallItem->DrawCallRenderSection.Pin(), RenderMat.Get());
				bNeedToVerifyMaterials = true;
			}
		}
		break;
		case ELexUIDrawCallType::PostProcess:
		{
			if (DrawCallItem->bMaterialChanged//maybe it is newly created, so check the materialChanged parameter
				)
			{
				if (DrawCallItem->PostProcessRenderableObject.IsValid())
				{
					// nothing to do here
				}
				DrawCallItem->bMaterialChanged = false;
			}
		}
		break;
		}
	}

	if (bNeedToVerifyMaterials)
	{
		MarkNeedVerifyMaterials();//tell parent canvas to verify material
	}
	if (bNeedToVerifyMaterials)
	{
		bNeedToVerifyMaterials = false;
		UIMesh->VerifyMaterials();
	}
}

void ULGUICanvas::MarkNeedVerifyMaterials()
{
	bNeedToVerifyMaterials = true;
	if (ParentCanvas.IsValid()
		&& !this->GetOverrideSorting()//if override sorting, then render by self(not parent)
		)
	{
		ParentCanvas->MarkNeedVerifyMaterials();
		ParentCanvas->bCanTickUpdate = true;
	}
}


UMaterialInstanceDynamic* ULGUICanvas::GetUIMaterialFromPool()
{
	bNeedToVerifyMaterials = true;
	if (PooledUIMaterialList.Num() == 0)
	{
		auto SrcMaterial = GetDefaultMaterial();
		auto UIMat = UMaterialInstanceDynamic::Create(SrcMaterial, this);
		UIMat->SetFlags(RF_Transient);
		return UIMat;
	}
	else
	{
		auto UIMat = PooledUIMaterialList[PooledUIMaterialList.Num() - 1];
		PooledUIMaterialList.RemoveAt(PooledUIMaterialList.Num() - 1);
		return UIMat;
	}
}
void ULGUICanvas::AddUIMaterialToPool(UMaterialInstanceDynamic* UIMat)
{
	bNeedToVerifyMaterials = true;
	if (UIMat->Parent == GetDefaultMaterial())
	{
		PooledUIMaterialList.Add(UIMat);
	}
}

void ULGUICanvas::SetRenderTargetResolutionScale(float value)
{
	if (RenderTargetResolutionScale != value)
	{
		RenderTargetResolutionScale = value;
		bAnythingChangedForRenderTarget = true;
	}
}

void ULGUICanvas::SetRenderTargetSizeMode(ELGUICanvasRenderTargetSizeMode value)
{
	if (RenderTargetSizeMode != value)
	{
		RenderTargetSizeMode = value;
		bAnythingChangedForRenderTarget = true;
	}
}

void ULGUICanvas::SetRenderTargetUpdateMode(ELGUICanvasRenderTargetUpdateMode value)
{
	if (RenderTargetUpdateMode != value)
	{
		RenderTargetUpdateMode = value;
		bAnythingChangedForRenderTarget = true;
	}
}

void ULGUICanvas::RequestUpdateForRenderTarget()
{
	if (RootCanvas == this)
	{
		bRequestUpdateForRenderTarget = true;
	}
}

void ULGUICanvas::SetSortOrderAdditionalValueRecursive(int32 InAdditionalValue)
{
	if (FMath::Abs(this->sortOrder + InAdditionalValue) > MAX_int16)
	{
		auto errorMsg = FText::Format(LOCTEXT("SortOrderOutOfRange", "{0} sortOrder out of range!\nNOTE! sortOrder value is stored with int16 type, so valid range is -32768 to 32767")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(LGUI, Error, TEXT("%s"), *errorMsg.ToString());
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errorMsg);
#endif
		return;
	}

	this->sortOrder += InAdditionalValue;
	for (auto ChildCanvas : ChildrenCanvasArray)
	{
		ChildCanvas->SetSortOrderAdditionalValueRecursive(InAdditionalValue);
	}
}

void ULGUICanvas::SetSortOrder(int32 InSortOrder, bool InPropagateToChildrenCanvas)
{
	if (sortOrder != InSortOrder)
	{
		if (CheckRootCanvas())
		{
			RootCanvas->bNeedToSortRenderPriority = true;
		}
		MarkCanvasUpdate(false, false, false);
		if (InPropagateToChildrenCanvas)
		{
			int32 Diff = InSortOrder - sortOrder;
			SetSortOrderAdditionalValueRecursive(Diff);
		}
		else
		{
			if (FMath::Abs(InSortOrder) > MAX_int16)
			{
				auto errorMsg = FText::Format(LOCTEXT("SortOrderOutOfRange", "{0} sortOrder out of range!\nNOTE! sortOrder value is stored with int16 type, so valid range is -32768 to 32767")
					, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
				UE_LOG(LGUI, Error, TEXT("%s"), *errorMsg.ToString());
#if WITH_EDITOR
				FLexUIUtils::EditorNotification(errorMsg);
#endif
				InSortOrder = FMath::Clamp(InSortOrder, (int32)MIN_int16, (int32)MAX_int16);
			}
			this->sortOrder = InSortOrder;
		}

		if (auto Instance = ULGUIManagerWorldSubsystem::GetInstance(this->GetWorld()))
		{
			switch (this->GetActualRenderMode())
			{
			default:
			case ELGUIRenderMode::ScreenSpaceOverlay:
				Instance->MarkSortScreenSpaceCanvas();
				break;
			case ELGUIRenderMode::WorldSpace_LGUI:
				Instance->MarkSortWorldSpaceLGUICanvas();
				break;
			case ELGUIRenderMode::WorldSpace:
				Instance->MarkSortWorldSpaceCanvas();
				break;
			case ELGUIRenderMode::RenderTarget:
				Instance->MarkSortRenderTargetSpaceCanvas();
				break;
			}
		}
	}
}
void ULGUICanvas::SetSortOrderToHighestOfHierarchy(bool InPropagateToChildrenCanvas)
{
	int32 Min = INT_MAX, Max = INT_MIN;
	GetMinMaxSortOrderOfHierarchy(Min, Max);
	SetSortOrder(Max + 1, InPropagateToChildrenCanvas);
}
void ULGUICanvas::SetSortOrderToLowestOfHierarchy(bool InPropagateToChildrenCanvas)
{
	int32 Min = INT_MAX, Max = INT_MIN;
	GetMinMaxSortOrderOfHierarchy(Min, Max);
	SetSortOrder(Min - 1, InPropagateToChildrenCanvas);
}

void ULGUICanvas::GetMinMaxSortOrderOfHierarchy(int32& OutMin, int32& OutMax)
{
	auto ThisCanvasSortOrder = this->GetActualSortOrder();
	if (ThisCanvasSortOrder < OutMin)
	{
		OutMin = ThisCanvasSortOrder;
	}
	if (ThisCanvasSortOrder > OutMax)
	{
		OutMax = ThisCanvasSortOrder;
	}
	for (auto ChildCanvas : ChildrenCanvasArray)
	{
		ChildCanvas->GetMinMaxSortOrderOfHierarchy(OutMin, OutMax);
	}
}


UMaterialInterface* ULGUICanvas::GetDefaultMaterial()const
{
	if (!DefaultMaterial)
	{
		DefaultMaterial = LoadObject<UMaterialInterface>(NULL, TEXT("/LGUI/Materials/LexUI_Image"));;
	}
	return DefaultMaterial;
}

void ULGUICanvas::SetDefaultMaterial(UMaterialInterface* InMaterial)
{
	if (DefaultMaterial != InMaterial)
	{
		for (int i = 0; i < UIDrawCallList.Num(); i++)
		{
			auto DrawCallItem = UIDrawCallList[i];
			if (DrawCallItem->Type == ELexUIDrawCallType::BatchGeometry)
			{
				if (DrawCallItem->Material == nullptr)
				{
					DrawCallItem->bMaterialChanged = true;
				}
			}
		}
		//clear old material
		PooledUIMaterialList.Reset();
		MarkCanvasUpdate(true, false, false);
	}
}

void ULGUICanvas::SetDynamicPixelsPerUnit(float newValue)
{
	if (dynamicPixelsPerUnit != newValue)
	{
		dynamicPixelsPerUnit = newValue;
		for (int i = 0; i < UIDrawCallList.Num(); i++)
		{
			UIDrawCallList[i]->bVertexPositionChanged = true;
		}
		MarkCanvasUpdate(false, true, false);
	}
}
float ULGUICanvas::GetActualDynamicPixelsPerUnit()const
{
	if (IsRootCanvas())
	{
		return dynamicPixelsPerUnit;
	}
	else
	{
		if (GetOverrideDynamicPixelsPerUnit())
		{
			return dynamicPixelsPerUnit;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualDynamicPixelsPerUnit();
			}
		}
	}
	return dynamicPixelsPerUnit;
}

float ULGUICanvas::GetActualBlendDepth()const
{
	if (IsRootCanvas())
	{
		return blendDepth;
	}
	else
	{
		if (GetOverrideBlendDepth())
		{
			return blendDepth;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualBlendDepth();
			}
		}
	}
	return blendDepth;
}

int ULGUICanvas::GetActualDepthFade()const
{
	if (IsRootCanvas())
	{
		return depthFade;
	}
	else
	{
		if (GetOverrideDepthFade())
		{
			return depthFade;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualDepthFade();
			}
		}
	}
	return depthFade;
}

int32 ULGUICanvas::GetActualSortOrder()const
{
	if (IsRootCanvas())
	{
		if (bOverrideSorting)
		{
			return sortOrder;
		}
		else
		{
			return 0;
		}
	}
	else
	{
		if (bOverrideSorting)
		{
			return sortOrder;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualSortOrder();
			}
		}
	}
	return sortOrder;
}

void ULGUICanvas::SetOverrideSorting(bool value)
{
	if (bOverrideSorting != value)
	{
		bOverrideSorting = value;
		if (CheckRootCanvas())
		{
			RootCanvas->bNeedToSortRenderPriority = true;
		}
		MarkCanvasUpdate(false, false, false);
	}
}

bool ULGUICanvas::GetActualRequireNormalAndTangent()const
{
	if (IsRootCanvas())
	{
		return bRequireNormalAndTangent;
	}
	else
	{
		if (GetOverrideRequireNormalAndTangent())
		{
			return bRequireNormalAndTangent;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualRequireNormalAndTangent();
			}
		}
	}
	return bRequireNormalAndTangent;
}


void ULGUICanvas::BuildProjectionMatrix(FIntPoint InViewportSize, ECameraProjectionMode::Type InProjectionType, float InFOV, float FarClipPlane, float NearClipPlane, FMatrix& OutProjectionMatrix)
{
	if (InViewportSize.X == 0 || InViewportSize.Y == 0)//in DebugCamera mode(toggle in editor by press ';'), viewport size is 0
	{
		InViewportSize.X = InViewportSize.Y = 1;
	}
	if (InProjectionType == ECameraProjectionMode::Orthographic)
	{
		check((int32)ERHIZBuffer::IsInverted);
		const float tempOrthoWidth = InViewportSize.X * 0.5f;
		const float tempOrthoHeight = InViewportSize.Y * 0.5f;

		const float ZScale = 1.0f / (FarClipPlane - NearClipPlane);
		const float ZOffset = -NearClipPlane;

		if ((int32)ERHIZBuffer::IsInverted)
		{
			OutProjectionMatrix = FReversedZOrthoMatrix(
				tempOrthoWidth,
				tempOrthoHeight,
				ZScale,
				ZOffset
			);
		}
		else
		{
			OutProjectionMatrix = FOrthoMatrix(
				tempOrthoWidth,
				tempOrthoHeight,
				ZScale,
				ZOffset
			);
		}
	}
	else
	{
		float XAxisMultiplier;
		float YAxisMultiplier;

		XAxisMultiplier = 1.0f;
		YAxisMultiplier = InViewportSize.X / (float)InViewportSize.Y;

		if ((int32)ERHIZBuffer::IsInverted)
		{
			OutProjectionMatrix = FReversedZPerspectiveMatrix(
				InFOV,
				InFOV,
				XAxisMultiplier,
				YAxisMultiplier,
				NearClipPlane,
				FarClipPlane
			);
		}
		else
		{
			OutProjectionMatrix = FPerspectiveMatrix(
				InFOV,
				InFOV,
				XAxisMultiplier,
				YAxisMultiplier,
				NearClipPlane,
				FarClipPlane
			);
		}
	}
}
float ULGUICanvas::CalculateDistanceToCamera()const
{
	if (ProjectionType == ECameraProjectionMode::Orthographic)
	{
		return 1000;
	}
	else
	{
		return UIItem->GetWidth() * 0.5f / FMath::Tan(FMath::DegreesToRadians(FOVAngle * 0.5f)) * UIItem->GetComponentScale().X;
	}
}
FMatrix ULGUICanvas::GetViewProjectionMatrix()const
{
	if (bIsViewProjectionMatrixDirty)
	{
		if (!CheckUIItem())
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d UIItem not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return cacheViewProjectionMatrix;
		}
		bIsViewProjectionMatrixDirty = false;

		FVector ViewLocation = GetViewLocation();
		FMatrix ViewRotationMatrix = FInverseRotationMatrix(GetViewRotator())
			* FMatrix(
				FPlane(0, 0, 1, 0),
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, 0, 1))
			;
		FMatrix ProjectionMatrix = GetProjectionMatrix();
		cacheViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;
	}
	return cacheViewProjectionMatrix;
}
FMatrix ULGUICanvas::GetProjectionMatrix()const
{
	if (bOverrideProjectionMatrix)
		return OverrideProjectionMatrix;

	FMatrix ProjectionMatrix = FMatrix::Identity;
	const float FOV = (bOverrideFovAngle ? OverrideFovAngle : FOVAngle) * (float)PI / 360.0f;
	BuildProjectionMatrix(FIntPoint(UIItem->GetWidth(), UIItem->GetHeight()), ProjectionType, FOV, FarClipPlane, NearClipPlane, ProjectionMatrix);
	return ProjectionMatrix;
}
FVector ULGUICanvas::GetViewLocation()const
{
	if (bOverrideViewLocation)
		return OverrideViewLocation;

	return UIItem->GetComponentLocation() - UIItem->GetForwardVector() * CalculateDistanceToCamera();
}
FRotator ULGUICanvas::GetViewRotator()const
{
	if (bOverrideViewRotation)
		return OverrideViewRotation;

	return UIItem->GetComponentRotation();
}
FIntPoint ULGUICanvas::GetViewportSize()const
{
	FIntPoint ViewportSize = FIntPoint(2, 2);
	if (auto world = this->GetWorld())
	{
#if WITH_EDITOR
		if (!world->IsGameWorld())
		{
			if (CheckUIItem())
			{
				ViewportSize.X = UIItem->GetWidth();
				ViewportSize.Y = UIItem->GetHeight();
			}
		}
		else
#endif
		{
			if (renderMode == ELGUIRenderMode::ScreenSpaceOverlay)
			{
				if (auto pc = world->GetFirstPlayerController())
				{
					pc->GetViewportSize(ViewportSize.X, ViewportSize.Y);
				}
			}
			else if (renderMode == ELGUIRenderMode::RenderTarget && IsValid(renderTarget))
			{
				ViewportSize.X = renderTarget->SizeX / RenderTargetResolutionScale;
				ViewportSize.Y = renderTarget->SizeY / RenderTargetResolutionScale;
			}
		}
	}
	return ViewportSize;
}

void ULGUICanvas::SetRenderMode(ELGUIRenderMode value)
{
	if (renderMode != value)
	{
		renderMode = value;
		MarkCanvasUpdate(false, false, false, true);
		CheckRenderMode(true);
	}
}

void ULGUICanvas::SetProjectionParameters(TEnumAsByte<ECameraProjectionMode::Type> InProjectionType, float InFovAngle, float InNearClipPlane, float InFarClipPlane)
{
	ProjectionType = InProjectionType;
	FOVAngle = InFovAngle;
	NearClipPlane = InNearClipPlane;
	FarClipPlane = InFarClipPlane;

	bIsViewProjectionMatrixDirty = true;
}

void ULGUICanvas::SetRenderTarget(UTextureRenderTarget2D* value)
{
	if (renderTarget != value)
	{
		renderTarget = value;
		UpdateRenderTarget(false);
		OnRenderTargetCreatedOrChanged.Broadcast(renderTarget, false);
	}
}

ELGUIRenderMode ULGUICanvas::GetActualRenderMode()const
{
	if (IsRootCanvas())
	{
		return this->renderMode;
	}
	else
	{
		if (CheckRootCanvas())
		{
			return RootCanvas->renderMode;
		}
	}
	return ELGUIRenderMode::WorldSpace;
}

void ULGUICanvas::SetBlendDepth(float value)
{
	if (blendDepth != value)
	{
		blendDepth = value;

		if (CheckRootCanvas())
		{
			if (RootCanvas->RenderModeIsLGUIRendererOrUERenderer(CurrentRenderMode))
			{
				if (RootCanvas->IsRenderToWorldSpace())
				{
					auto ViewExtension = ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
					if (ViewExtension.IsValid())
					{
						ViewExtension->SetRenderCanvasDepthParameter(this, this->GetActualBlendDepth(), this->GetActualDepthFade());
					}
				}
			}
		}
	}
}

void ULGUICanvas::SetDepthFade(int value)
{
	if (depthFade != value)
	{
		depthFade = value;

		if (CheckRootCanvas())
		{
			if (RootCanvas->RenderModeIsLGUIRendererOrUERenderer(CurrentRenderMode))
			{
				if (RootCanvas->IsRenderToWorldSpace())
				{
					auto ViewExtension = ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
					if (ViewExtension.IsValid())
					{
						ViewExtension->SetRenderCanvasDepthParameter(this, this->GetActualBlendDepth(), this->GetActualDepthFade());
					}
				}
			}
		}
	}
}

void ULGUICanvas::SetEnableDepthTest(bool value)
{
	if (bEnableDepthTest != value)
	{
		bEnableDepthTest = value;
	}
}

UTextureRenderTarget2D* ULGUICanvas::GetActualRenderTarget()const
{
	if (IsRootCanvas())
	{
		return this->renderTarget;
	}
	else
	{
		if (CheckRootCanvas())
		{
			return RootCanvas->renderTarget;
		}
	}
	return nullptr;
}

float ULGUICanvas::GetActualRenderTargetResolutionScale()const
{
	if (IsRootCanvas())
	{
		return this->RenderTargetResolutionScale;
	}
	else
	{
		if (CheckRootCanvas())
		{
			return RootCanvas->RenderTargetResolutionScale;
		}
	}
	return RenderTargetResolutionScale;
}

ELGUICanvasRenderTargetSizeMode ULGUICanvas::GetActualRenderTargetSizeMode()const
{
	if (IsRootCanvas())
	{
		return this->RenderTargetSizeMode;
	}
	else
	{
		if (CheckRootCanvas())
		{
			return RootCanvas->RenderTargetSizeMode;
		}
	}
	return RenderTargetSizeMode;
}

ELGUICanvasRenderTargetUpdateMode ULGUICanvas::GetActualRenderTargetUpdateMode()const
{
	if (IsRootCanvas())
	{
		return this->RenderTargetUpdateMode;
	}
	else
	{
		if (CheckRootCanvas())
		{
			return RootCanvas->RenderTargetUpdateMode;
		}
	}
	return RenderTargetUpdateMode;
}

int32 ULGUICanvas::GetDrawCallCount()const
{
	int32 Result = 0;
	for (auto& Item : UIDrawCallList)
	{
		if (Item->Type != ELexUIDrawCallType::ChildCanvas)
		{
			Result++;
		}
	}
	return Result;
}

void ULGUICanvas::OnClipDataTextureChanged(UTexture* NewTexture)
{
	for (const auto& DrawCallItem : UIDrawCallList)
	{
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchGeometry:
			{
				auto RenderMat = DrawCallItem->RenderMaterial;
				if (RenderMat.IsValid() && DrawCallItem->bMaterialContainsLexUIParameter)
				{
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_ClipDataTexture_MaterialParameterName, NewTexture);
				}
			}
			break;
		case ELexUIDrawCallType::PostProcess:
			break;
		case ELexUIDrawCallType::DirectMesh:
			break;
		}
	}
}

void ULGUICanvas::RemoveClipData(const TSharedPtr<FLexUIClipData>& InClipData)
{
	ClipDataList.Remove(InClipData);
}
UTexture* ULGUICanvas::GetClipDataTexture()const
{
	return ClipDataAsTexture->GetDataTexture();
}

FTransform2D ULGUICanvas::ConvertTo2DTransform(const FTransform& Transform)
{
	auto itemToCanvasMatrix = Transform.ToMatrixWithScale();
	auto itemLocation = Transform.GetLocation();
	auto itemToCanvasTf2D = FTransform2D(FMatrix2x2(itemToCanvasMatrix.M[1][1], itemToCanvasMatrix.M[1][2], itemToCanvasMatrix.M[2][1], itemToCanvasMatrix.M[2][2]), FVector2D(itemLocation.Y, itemLocation.Z));
	return itemToCanvasTf2D;
}

template<class T>
FORCEINLINE void GetMinMax(T a, T b, T c, T d, T& min, T& max)
{
	float abMin = FMath::Min(a, b);
	float abMax = FMath::Max(a, b);
	float cdMin = FMath::Min(c, d);
	float cdMax = FMath::Max(c, d);
	min = FMath::Min(abMin, cdMin);
	max = FMath::Max(abMax, cdMax);
}
void ULGUICanvas::CalculateUIItem2DBounds(UUIBaseRenderable* item, const FTransform2D& transform, FVector2D& min, FVector2D& max)
{
	FVector2D LocalPoint1, LocalPoint2;
	item->GetGeometryBoundsInLocalSpace(LocalPoint1, LocalPoint2);
	const auto Point1 = transform.TransformPoint(LocalPoint1);
	const auto Point2 = transform.TransformPoint(LocalPoint2);
	const auto Point3 = transform.TransformPoint(FVector2D(LocalPoint2.X, LocalPoint1.Y));
	const auto Point4 = transform.TransformPoint(FVector2D(LocalPoint1.X, LocalPoint2.Y));

	GetMinMax(Point1.X, Point2.X, Point3.X, Point4.X, min.X, max.X);
	GetMinMax(Point1.Y, Point2.Y, Point3.Y, Point4.Y, min.Y, max.Y);
}

#undef LOCTEXT_NAMESPACE

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif