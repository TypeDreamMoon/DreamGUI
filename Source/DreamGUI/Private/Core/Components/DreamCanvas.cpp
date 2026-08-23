// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamCanvas.h"
#include "Core/DreamGUISettings.h"
#include "Engine/UserInterfaceSettings.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Engine/GameViewportClient.h"
#include "SceneView.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamVisualDirectMesh.h"
#include "Core/Components/DreamWidget.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "SceneViewExtension.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Math/TransformCalculus2D.h"
#include "TextureResource.h"
#include "Camera/CameraComponent.h"
#include "Core/DreamCanvasDrawCallProcessingRunnable.h"
#include "Core/DreamUIClipData.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/Components/DreamLayout.h"


#define LOCTEXT_NAMESPACE "DreamCanvas"

UDreamCanvas::UDreamCanvas()
{
	DefaultMeshType = UDreamUIMeshComponent::StaticClass();
	DefaultMaterial = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultUIMaterial, TEXT("DefaultUIMaterial"));
	bStartWithTickEnabled = false;
}

void UDreamCanvas::Awake()
{
	Super::Awake();
	this->SetCanExecuteTick(false);

	CheckRootCanvas();
	CurrentRenderMode = this->GetActualRenderMode();
	if (auto DreamWidget = GetWidget())
	{
		bPrevIsVisible = DreamWidget->GetRenderVisibleInHierarchy();
	}
	else
	{
		bPrevIsVisible = false;
	}
	MarkCanvasUpdate(true);

	bNeedToSortRenderPriority = true;

	if (this->IsRootCanvas())
	{
		if (this->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay
				|| this->GetRenderMode() == EDreamRenderMode::RenderTarget
				)
		{
			CheckAndApplyViewportParameter();
		}
	}
	
	if (IsValid(CustomScale))
	{
		CustomScale->Init(this);
	}
}

TSharedPtr<class FDreamUIRenderer, ESPMode::ThreadSafe> UDreamCanvas::GetRenderTargetViewExtension()
{
	if (!RenderTargetViewExtension.IsValid())
	{
		RenderTargetViewExtension = FSceneViewExtensions::NewExtension<FDreamUIRenderer>(GetWorld(), EDreamUIRendererType::RenderTarget);
	}
	return RenderTargetViewExtension;
}

void UDreamCanvas::UpdateRootCanvas()
{
	if (!GetWorld())
		return;
	CheckRootCanvas();
	if (this == RootCanvas)
	{
		if (RenderModeIsDreamRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (!GetWorld()->IsGameWorld())//edit mode
			{
				if (ActualRenderMode == EDreamRenderMode::ScreenSpaceOverlay)
					ActualRenderMode = EDreamRenderMode::WorldSpace_DreamUI;
			}
#endif
			switch (ActualRenderMode)
			{
			case EDreamRenderMode::ScreenSpaceOverlay:
			{
				if (!bHasAddToDreamScreenSpaceRenderer)
				{
					auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true);

					if (ViewExtension.IsValid())//only root canvas can add screen space UI to DreamGUIRenderer
					{
						ViewExtension->SetScreenSpaceRootCanvas(this);
						bHasAddToDreamScreenSpaceRenderer = true;
					}
				}
			}
			break;
			case EDreamRenderMode::RenderTarget:
			{
				if (!bHasAddToDreamScreenSpaceRenderer)
				{
					GetRenderTargetViewExtension()->SetScreenSpaceRootCanvas(this);
					bHasAddToDreamScreenSpaceRenderer = true;
				}
			}
			break;
			case EDreamRenderMode::WorldSpace_DreamUI:
			{
				if (!bHasSetInitialStateForDreamWorldSpaceRenderer)
				{
					auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true);

					if (ViewExtension.IsValid())//only root canvas can add screen space UI to DreamGUIRenderer
					{
						//put initial code here
						bHasSetInitialStateForDreamWorldSpaceRenderer = true;
					}
				}
			}
			break;
			}
		}
		
		UpdateCanvasDrawCall();
	}
}

void UDreamCanvas::UpdateRenderTarget(bool CallEvent)
{
	auto DreamWidget = GetWidget();
	FIntPoint DesiredRenderTargetSize(DreamWidget->GetWidth() * RenderTargetResolutionScale, DreamWidget->GetHeight() * RenderTargetResolutionScale);
	static const int32 MaxAllowedDrawSize = GetMax2DTextureDimension();
	if (DesiredRenderTargetSize.X <= 0 || DesiredRenderTargetSize.Y <= 0)
	{
		return;
	}
	DesiredRenderTargetSize.X = FMath::Min(DesiredRenderTargetSize.X, MaxAllowedDrawSize);
	DesiredRenderTargetSize.Y = FMath::Min(DesiredRenderTargetSize.Y, MaxAllowedDrawSize);

	if (RenderTarget == nullptr)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, EObjectFlags::RF_Transient);
		RenderTarget->AddressX = TextureAddress::TA_Clamp;
		RenderTarget->AddressY = TextureAddress::TA_Clamp;
		RenderTarget->ClearColor = FLinearColor::Transparent;
		RenderTarget->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
		if (CallEvent)
		{
			OnRenderTargetChanged.Broadcast(RenderTarget);
		}
	}
	else
	{
		switch (RenderTargetSizeMode)
		{
		case EDreamCanvasRenderTargetSizeMode::None:
		case EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget:
			if (RenderTarget != nullptr)
			{
				DesiredRenderTargetSize.X = RenderTarget->SizeX;
				DesiredRenderTargetSize.Y = RenderTarget->SizeY;
			}
			break;
		case EDreamCanvasRenderTargetSizeMode::RenderTargetFitToCanvas:
			break;
		}
		if (RenderTarget->SizeX != DesiredRenderTargetSize.X || RenderTarget->SizeY != DesiredRenderTargetSize.Y)
		{
			RenderTarget->ClearColor = FLinearColor::Transparent;
			RenderTarget->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
			RenderTarget->UpdateResourceImmediate();
#if WITH_EDITOR
			RenderTarget->Modify();
#endif
			if (CallEvent)
			{
				OnRenderTargetChanged.Broadcast(RenderTarget);
			}
		}
	}
}

void UDreamCanvas::CheckRenderTargetUpdate()
{
	bool bIsRenderTargetRenderer = false;
	if (RenderModeIsDreamRendererOrUERenderer(CurrentRenderMode))
	{
		auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
		if (!GetWorld()->IsGameWorld())//edit mode
		{
			if (ActualRenderMode == EDreamRenderMode::ScreenSpaceOverlay)
				ActualRenderMode = EDreamRenderMode::WorldSpace_DreamUI;
		}
#endif
		if (ActualRenderMode == EDreamRenderMode::RenderTarget)
		{
			bIsRenderTargetRenderer = true;
		}
	}
	if (bIsRenderTargetRenderer)
	{
		bool bCanUpdateRenderTarget = false;
		switch (RenderTargetUpdateMode)
		{
		default:
		case EDreamCanvasRenderTargetUpdateMode::Automatic:
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
		case EDreamCanvasRenderTargetUpdateMode::Always:
			bCanUpdateRenderTarget = true;
			break;
		case EDreamCanvasRenderTargetUpdateMode::WhenRequest:
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
			if (IsValid(RenderTarget))
			{
#if WITH_EDITOR
				if (!this->GetWorld()->IsGameWorld())
				{
					if (!RenderTarget->GameThread_GetRenderTargetResource())
					{
						RenderTarget->InitCustomFormat(RenderTarget->SizeX, RenderTarget->SizeY, EPixelFormat::PF_B8G8R8A8, false);
					}
				}
#endif
				if (RenderTargetViewExtension.IsValid())
				{
					RenderTargetViewExtension->UpdateRenderTargetRenderer(RenderTarget, RenderTargetClearColor);
				}
			}
		}
	}
}

void UDreamCanvas::OnRegister()
{
	Super::OnRegister();
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->AddCanvas(this);
	}
	if (DrawCallProcessingRunnable == nullptr)
	{
		DrawCallProcessingRunnable = MakeUnique<FDreamCanvasDrawCallProcessingRunnable>();
		DrawCallProcessingRunnable->Start();
	}
	if (TransformVerticesAsyncFunctionRunnable == nullptr)
	{
		TransformVerticesAsyncFunctionRunnable = MakeUnique<FDreamCanvasAsyncFunctionRunnable>();
		TransformVerticesAsyncFunctionRunnable->Start();
	}
	if (auto DreamWidget = GetWidget())
	{
		DreamWidget->RegisterRenderCanvas(this);
		DreamWidget->GetAttachmentChangedEvent().AddUObject(this, &UDreamCanvas::OnUIHierarchyAttachmentChanged);
		DreamWidget->GetWidgetActiveChangedEvent().AddUObject(this, &UDreamCanvas::OnWidgetActiveChanged);

		OnUIHierarchyAttachmentChanged();
	}

	if (!IsValid(ClipDataAsTexture))
	{
		ClipDataAsTexture = NewObject<UDreamUIDataAsTexture>(this, UDreamUIDataAsTexture::StaticClass(), NAME_None, RF_Transient);
		ClipDataAsTexture->Init(FDreamUIClipData::BlockSizeInBytes, EDreamUIDataAsTexturePixelFormat::R32G32B32A32, 128);
		ClipDataAsTexture->OnDataTextureChange.AddUObject(this, &UDreamCanvas::OnClipDataTextureChanged);
		ClipDataAsTexture->RegisterBuffer();//register a zero position as a placeholder for not clipping type.
	}

	RegisterCanvasScaler();
}
void UDreamCanvas::OnUnregister()
{
	Super::OnUnregister();
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->RemoveCanvas(this);
	}
	ClearDrawCall();
	if (IsValid(UIMesh))
	{
		UIMesh->DestroyComponent();
		UIMesh = nullptr;
	}
	if (DrawCallProcessingRunnable.IsValid())
	{
		DrawCallProcessingRunnable->Stop();
		DrawCallProcessingRunnable.Reset();
	}
	if (TransformVerticesAsyncFunctionRunnable.IsValid())
	{
		TransformVerticesAsyncFunctionRunnable->Stop();
		TransformVerticesAsyncFunctionRunnable.Reset();
	}

	ClipDataList.Empty();
	
	{
		//these three functions is from OnUIHierarchyChanged()
		RemoveFromViewExtension(true);
		CheckRenderMode(true);
	}

	//tell Widget
	if (auto DreamWidget = GetWidget())
	{
		DreamWidget->UnregisterRenderCanvas();
		DreamWidget->GetAttachmentChangedEvent().RemoveAll(this);
		DreamWidget->GetWidgetActiveChangedEvent().RemoveAll(this);
	}

	UnregisterCanvasScaler();
}

void UDreamCanvas::PostInitProperties()
{
	Super::PostInitProperties();
}

void UDreamCanvas::ClearDrawCall()
{
	if (IsValid(UIMesh))
	{
		UIMesh->ClearRenderData();
		bUIMeshNeedToSetInitialParameters = true;
	}
	PooledDefaultMaterialList.Empty();
	MapSrcMatToDynamicMat.Empty();
	CurrentDrawCallData.DrawCallArray.Empty();
	bNeedToSetClipDataTextureMaterialParameter = true;
}

void UDreamCanvas::RemoveFromViewExtension(bool PropogateToChildrenCanvas)
{
	if (bHasAddToDreamScreenSpaceRenderer)
	{
		bHasAddToDreamScreenSpaceRenderer = false;
		if (RenderTargetViewExtension.IsValid())//could be RenderTarget mode
		{
			RenderTargetViewExtension->ClearScreenSpaceRootCanvas();
		}
		else//if not RenderTarget mode, then should be ScreenSpaceOverlay
		{
			auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
			if (ViewExtension.IsValid())
			{
				ViewExtension->ClearScreenSpaceRootCanvas();
			}
		}
	}
	if (bHasSetInitialStateForDreamWorldSpaceRenderer)
	{
		bHasSetInitialStateForDreamWorldSpaceRenderer = false;
	}

	if (PropogateToChildrenCanvas)
	{
		for (const auto& ChildCanvas : ChildrenCanvasArray)
		{
			if (!ChildCanvas.IsValid())continue;
			if (ChildCanvas->bForceRenderToTarget)continue;
			ChildCanvas->RemoveFromViewExtension(PropogateToChildrenCanvas);
		}
	}
}

bool UDreamCanvas::CheckRootCanvas(bool forceRecheck)const
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
	auto FindRootCanvas = [](UDreamWidget* Widget)
	{
		UDreamCanvas* ResultCanvas = nullptr;
		auto ParentWidget = Widget;
		while (ParentWidget != nullptr)
		{
			if (auto FoundCanvas = ParentWidget->GetComponent<UDreamCanvas>())
			{
				ResultCanvas = FoundCanvas;
				if (FoundCanvas->bForceRenderToTarget)
				{
					return ResultCanvas;
				}
			}
			ParentWidget = ParentWidget->GetParent();
		}
		return ResultCanvas;
	};
	auto NewRootCanvas = FindRootCanvas(this->GetWidget());
	if (NewRootCanvas != RootCanvas)
	{
		RootCanvas = NewRootCanvas;
		bNeedToSetClipDataTextureMaterialParameter = true;
	}
	if (RootCanvas.IsValid())
	{
		return true;
	}
	return false;
}

void UDreamCanvas::SetParentCanvas(UDreamCanvas* InParentCanvas)
{
	if (ParentCanvas != InParentCanvas)
	{
		this->ClearDrawCall();
		this->MarkCanvasUpdate(true);
		if (ParentCanvas.IsValid())
		{
			this->DrawCallAsChildCanvas = nullptr;

			ParentCanvas->ChildrenCanvasArray.Remove(this);
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->MarkCanvasUpdate(true);
		}
		ParentCanvas = InParentCanvas;
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->ChildrenCanvasArray.AddUnique(this);
			ParentCanvas->MarkCanvasUpdate(true);
		}
	}
}

void UDreamCanvas::CollectChildrenCanvas(UDreamCanvas* Target, TArray<UDreamCanvas*>& OutAllChildrenCanvas, bool IncludeTarget)
{
	if (IncludeTarget)
	{
		OutAllChildrenCanvas.Add(Target);
	}
	for (auto& Child : Target->GetChildrenCanvasArray())
	{
		CollectChildrenCanvas(Child.Get(), OutAllChildrenCanvas, true);
	}
}

void UDreamCanvas::CheckRenderMode(bool PropagateToChildrenCanvas)
{
	const auto OldRenderMode = CurrentRenderMode;
	if (CheckRootCanvas(true))
	{
		CurrentRenderMode = RootCanvas->GetRenderMode();
	}
	else
	{
		CurrentRenderMode = EDreamRenderMode::None;
	}
	//if render space changed, we need to change recreate all render data
	if (CurrentRenderMode != OldRenderMode)
	{
		if (auto DreamWidget = GetWidget())
		{
			DreamWidget->MarkRenderModeChangeRecursive(this, OldRenderMode, CurrentRenderMode);
		}
		//clear drawcall, delete mesh, because UE/DreamGUI render's mesh data not compatible
		this->ClearDrawCall();
		OnRenderModeChanged.Broadcast(this, OldRenderMode, CurrentRenderMode);
	}

	if (PropagateToChildrenCanvas)
	{
		for (const auto& ChildCanvas : ChildrenCanvasArray)
		{
			if (!ChildCanvas.IsValid())continue;
			if (ChildCanvas->bForceRenderToTarget)continue;
			ChildCanvas->CheckRenderMode(PropagateToChildrenCanvas);
		}
	}
}
void UDreamCanvas::OnUIHierarchyAttachmentChanged()
{
 	this->bCanTickUpdate = true;
	RemoveFromViewExtension(true);
	CheckRenderMode(true);

	auto NewParentCanvas = GetWidget()->GetComponentInParent<UDreamCanvas>(false);
	SetParentCanvas(NewParentCanvas);
}

void UDreamCanvas::OnWidgetActiveChanged(bool WidgetActive)
{
	if (GetWidget()->GetRenderVisibleInHierarchy())
	{
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->MarkCanvasUpdate(true);

		}
	}
	else
	{
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->MarkCanvasUpdate(true);
		}
	}
}

bool UDreamCanvas::IsRenderToScreenSpace()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == EDreamRenderMode::ScreenSpaceOverlay;
	}
	return false;
}
bool UDreamCanvas::IsRenderToRenderTarget()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == EDreamRenderMode::RenderTarget;
	}
	return false;
}
bool UDreamCanvas::IsRenderToWorldSpace()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == EDreamRenderMode::WorldSpace
			|| RootCanvas->RenderMode == EDreamRenderMode::WorldSpace_DreamUI
			;
	}
	return false;
}

bool UDreamCanvas::IsRenderByDreamUIRendererOrUERenderer()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == EDreamRenderMode::ScreenSpaceOverlay
			|| RootCanvas->RenderMode == EDreamRenderMode::RenderTarget
			|| RootCanvas->RenderMode == EDreamRenderMode::WorldSpace_DreamUI
			;
	}
	return false;
}

void UDreamCanvas::RefreshAllClipData()
{
	// All children canvas clip data is stored in the root canvas, so only the root has a list to walk.
	if (this != RootCanvas)
	{
		return;
	}
	for (const auto& ClipData : ClipDataList)
	{
		ClipData->UpdateData();
	}
}

void UDreamCanvas::MarkCanvasUpdate(bool bRebuildDrawCall)
{
	this->bCanTickUpdate = true;
	if (bRebuildDrawCall)
	{
		this->bShouldRebuildDrawCall = true;
	}
}

void UDreamCanvas::MarkCanvasHierarchyChanged()
{
	bNeedToGenerateWidgetList = true;
	MarkCanvasUpdate(true);
}

#if WITH_EDITOR
bool UDreamCanvas::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		auto MemberName = InProperty->GetFName();
		bool bIsRootCanvas = this->IsRootCanvas()
		|| this->GetWorld() == nullptr;//world is null maybe it is blueprint editor
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, ProjectionType))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, FieldOfView))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, NearClipPlane))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, FarClipPlane))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScaleMode))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, ReferenceResolution))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, MatchFromWidthToHeight))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, ScreenMatchMode))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, bFixedSizeInEditMode))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, SizeInEditMode))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, RenderMode))
		{
			if (bIsRootCanvas)
			{
				if (bForceRenderToTarget)
				{
					return false;
				}
				return true;
			}
		}
	}

	return Super::CanEditChange(InProperty);
}
void UDreamCanvas::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (auto DreamWidget = GetWidget())
	{
		DreamWidget->MarkAllDirtyRecursive();
	}
	if (CheckRootCanvas())
	{
		RootCanvas->MarkCanvasUpdate(true);
		RootCanvas->bRequestUpdateForRenderTarget = true;
	}

	auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, bForceRenderToTarget))
	{
		if (bForceRenderToTarget)
		{
			RenderMode = EDreamRenderMode::RenderTarget;
			OnRenderTargetChanged.Broadcast(RenderTarget);
		}
		else
		{
			OnRenderTargetChanged.Broadcast(nullptr);
		}
	}

	OnViewportParameterChanged();
}
void UDreamCanvas::PostLoad()
{
	Super::PostLoad();
}
void UDreamCanvas::PostEditUndo()
{
	Super::PostEditUndo();

	UDreamUIManagerWorldSubsystem::RefreshAllUI(this->GetWorld());
}
void UDreamCanvas::EnsureDataForRebuild()
{
	struct LOCAL
	{
		static void RecheckRootCanvasRecursive(UDreamCanvas* Target)
		{
			Target->MarkCanvasUpdate(true);
			Target->CheckRenderMode(false);
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
	UDreamUIManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]() {
		if (WeakThis.IsValid())
		{
			LOCAL::RecheckRootCanvasRecursive(WeakThis.Get());
		}
		}, 0);
}
#endif

UDreamCanvas* UDreamCanvas::GetRootCanvas() const
{ 
	CheckRootCanvas(); 
	return RootCanvas.Get(); 
}
bool UDreamCanvas::IsRootCanvas()const
{
	return GetRootCanvas() == this;
}

USceneComponent* UDreamCanvas::GetAttachedRootSceneComponent() const
{
	return AttachedRootSceneComponent.Get();
}

void UDreamCanvas::AttachToSceneComponent(USceneComponent* InSceneComp)
{
	if (AttachedRootSceneComponent.Get() == InSceneComp)
	{
		return;
	}
	if (USceneComponent* Previous = AttachedRootSceneComponent.Get())
	{
		if (AttachedRootSceneComponentTransformHandle.IsValid())
		{
			Previous->TransformUpdated.Remove(AttachedRootSceneComponentTransformHandle);
		}
	}
	AttachedRootSceneComponentTransformHandle.Reset();
	AttachedRootSceneComponent = InSceneComp;
	if (InSceneComp)
	{
		AttachedRootSceneComponentTransformHandle = InSceneComp->TransformUpdated.AddUObject(this, &UDreamCanvas::OnAttachedRootSceneComponentTransformUpdated);
		// Place the tree at the new host now; the binding keeps it there afterwards.
		if (auto Widget = GetWidget())
		{
			Widget->CalculateObjectToWorldTransform(true);
		}
	}
}

void UDreamCanvas::OnAttachedRootSceneComponentTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	if (auto Widget = GetWidget())
	{
		Widget->CalculateObjectToWorldTransform(true);
	}
}

void UDreamCanvas::BeginDestroy()
{
	AttachToSceneComponent(nullptr);
	Super::BeginDestroy();
}

void UDreamCanvas::MarkVisualWillChange(UDreamVisual* InOldVisual)
{
	MarkCanvasUpdate(false);
}

void UDreamCanvas::RegisterVisual(UDreamVisual* InVisual)
{
	VisualList.AddUnique(InVisual);
	CheckWidgetPropertyData();
	InVisual->SetWidgetPropertyDataStartPosition(WidgetPropertyDataAsTexture->RegisterBuffer());
}

void UDreamCanvas::UnregisterVisual(UDreamVisual* InVisual)
{
	VisualList.Remove(InVisual);
	auto WidgetPropertyDataStartPosition = InVisual->GetWidgetPropertyDataStartPosition();
	if (WidgetPropertyDataStartPosition > INDEX_NONE)
	{
		if (IsValid(WidgetPropertyDataAsTexture))
		{
			WidgetPropertyDataAsTexture->UnregisterBuffer(WidgetPropertyDataStartPosition);
		}
		InVisual->SetWidgetPropertyDataStartPosition(INDEX_NONE);
	}
}

void UDreamCanvas::AddDreamWidget(UDreamWidget* InWidget)
{
	bNeedToGenerateWidgetList = true;
	MarkCanvasUpdate(true);
}
void UDreamCanvas::RemoveDreamWidget(UDreamWidget* InWidget)
{
	bNeedToGenerateWidgetList = true;
	MarkCanvasUpdate(true);
}

bool UDreamCanvas::Is2DUITransform(const FTransform& Transform)
{
#if WITH_EDITOR
	float threshold = UDreamUISettings::GetAutoBatchThreshold();
#else
	static float threshold = UDreamUISettings::GetAutoBatchThreshold();
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

void UDreamCanvas::SetOverrideViewLocation(bool Override, FVector Value)
{
	bOverrideViewLocation = Override;
	OverrideViewLocation = Value;
}
void UDreamCanvas::SetOverrideViewRotation(bool Override, FRotator Value)
{
	bOverrideViewRotation = Override;
	OverrideViewRotation = Value;
}
void UDreamCanvas::SetOverrideFovAngle(bool Override, float Value)
{
	bOverrideFovAngle = Override;
	OverrideFovAngle = Value;
}
void UDreamCanvas::SetOverrideProjectionMatrix(bool Override, FMatrix Value)
{
	bOverrideProjectionMatrix = Override;
	OverrideProjectionMatrix = Value;
}

void UDreamCanvas::MarkTransformOrDimensionChanged()
{
	bIsViewProjectionMatrixDirty = true;
}

void UDreamCanvas::SetDefaultMeshType(TSubclassOf<UDreamUIMeshComponent> InValue)
{
	if (DefaultMeshType != InValue)
	{
		DefaultMeshType = InValue;
		//clear mesh
		if (IsValid(UIMesh))
		{
			UIMesh->DestroyComponent();
			UIMesh = nullptr;
		}
		MarkCanvasUpdate(true);
	}
}

UDreamCanvas* UDreamCanvas::GetSortOwnerCanvas()
{
	UDreamCanvas* Canvas = this;
	while (IsValid(Canvas) && !Canvas->IsRootCanvas() && !Canvas->GetOverrideSorting())
	{
		UDreamCanvas* Parent = Canvas->GetParentCanvas().Get();
		if (!IsValid(Parent))
		{
			break;
		}
		Canvas = Parent;
	}
	return Canvas;
}

void UDreamCanvas::ConsumePendingRenderPrioritySort()
{
	if (!bNeedToSortRenderPriority)
	{
		return;
	}
	if (!IsRootCanvas() && !GetOverrideSorting())
	{
		return;//not a sort owner; the request was escalated to the owner at set time
	}
	bNeedToSortRenderPriority = false;
	SortDrawCall();
}

void UDreamCanvas::MarkFinishUpdateCanvasDrawCall()
{
	//sort render priority
	if (bNeedToSortRenderPriority)
	{
		bNeedToSortRenderPriority = false;
		if (this->IsRootCanvas() || this->GetOverrideSorting())
		{
			this->SortDrawCall();
		}
	}

	//update children canvas
	for (auto& item : ChildrenCanvasArray)
	{
		if (!item.IsValid())continue;
		if (item->bForceRenderToTarget)continue;
		item->MarkFinishUpdateCanvasDrawCall();
	}
}

DECLARE_CYCLE_STAT(TEXT("Canvas PrepareDrawCallBatchingData"), STAT_PrepareDrawCallBatching, STATGROUP_DreamGUI);
void UDreamCanvas::PrepareDrawCallBatchingData(TArray<FDreamUIRenderData>& OutRenderDataArray)
{
	SCOPE_CYCLE_COUNTER(STAT_PrepareDrawCallBatching);
	OutRenderDataArray.Reset();
	for (int i = 0; i < WidgetList.Num(); i++)
	{
		auto& Widget = WidgetList[i];
		if (Widget->IsCanvasWidget() && Widget->GetRenderCanvas() != this)//is child canvas
		{
			auto ChildCanvas = Widget->GetRenderCanvas();
			if (ChildCanvas == nullptr)continue;//normally this won't be nullptr, but when redo in editor this breaks
			if (ChildCanvas->bForceRenderToTarget)continue;//skip this type
			if (ChildCanvas->GetOverrideSorting())continue;//override sorting means render by itself, then no need to use it as child-canvas
			auto RenderData = FDreamUIRenderData(EDreamUIDrawCallType::ChildCanvas);
			RenderData.ChildCanvas = ChildCanvas;
			OutRenderDataArray.Add(MoveTemp(RenderData));
		}
		else
		{
			auto Visual = Widget->GetVisual();
			if (!Visual)continue;
			if (!Widget->GetRenderVisibleInHierarchy())//if not visible, need to remove the draw-call from draw-call list
			{
				continue;
			}
			switch (Visual->GetVisualType())
			{
			default:
			case EDreamVisualType::BatchMesh:
				{
					auto DreamVisualBatchMesh = static_cast<UDreamVisualBatchMesh*>(Visual);
					auto ItemGeo = DreamVisualBatchMesh->GetGeometry();
					if (ItemGeo == nullptr)continue;
					while (ItemGeo->bIsCalculating)
					{
						FPlatformProcess::Sleep(0.001f);//we must wait until geometry calculation finish, or CopyDataForPrepare will get wrong data
					}
					if (ItemGeo->Vertices.Num() == 0)continue;
					if (ItemGeo->Vertices.Num() > LEXUI_MAX_VERTEX_COUNT)continue;
					auto RenderData = FDreamUIRenderData(EDreamUIDrawCallType::BatchMesh);
					RenderData.BatchMeshGeometry.CopyDataForPrepare(*ItemGeo);
					RenderData.BatchMeshVisualObject = DreamVisualBatchMesh;
					OutRenderDataArray.Add(MoveTemp(RenderData));
				}
				break;
			case EDreamVisualType::PostProcess:
				{
					auto DreamVisualPostProcess = static_cast<UDreamVisualPostProcess*>(Visual);
					if (!DreamVisualPostProcess->HaveValidData())continue;
					auto RenderData = FDreamUIRenderData(EDreamUIDrawCallType::PostProcess);
					RenderData.PostProcessVisualObject = DreamVisualPostProcess;
					OutRenderDataArray.Add(MoveTemp(RenderData));
				}
				break;
			case EDreamVisualType::DirectMesh:
				{
					auto DreamVisualDirectMesh = static_cast<UDreamVisualDirectMesh*>(Visual);
					if (!DreamVisualDirectMesh->HaveValidData())continue;
					auto RenderData = FDreamUIRenderData(EDreamUIDrawCallType::DirectMesh);
					RenderData.DirectMeshVisualObject = DreamVisualDirectMesh;
					OutRenderDataArray.Add(MoveTemp(RenderData));
				}
				break;
			}
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("Canvas BatchDrawCallAsync"), STAT_BatchDrawCall, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas BatchDrawCall/OverlapTest"), STAT_OverlapTest, STATGROUP_DreamGUI);

void UDreamCanvas::BatchDrawCallAsync(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop,
	const TArray<FDreamUIRenderData>& InRenderDataArray, TArray<FDreamUIDrawCall>& InOutUIDrawCallList)
{
	SCOPE_CYCLE_COUNTER(STAT_BatchDrawCall);

	InOutUIDrawCallList.Reset();
	
	auto CanvasRect = DreamUIQuadTree::Rectangle(InCanvasLeftBottom, InCanvasRightTop);

	auto IntersectBounds = [](FVector2D aMin, FVector2D aMax, FVector2D bMin, FVector2D bMax) {
		return !(bMin.X >= aMax.X
			|| bMax.X <= aMin.X
			|| bMax.Y <= aMin.Y
			|| bMin.Y >= aMax.Y
			);
	};
	auto OverlapWithOtherDrawCall = [&](const FDreamUIGeometry& InGeo, const FDreamUIDrawCall& OtherDrawCallItem) {
		// SCOPE_CYCLE_COUNTER(STAT_OverlapTest);
		switch (OtherDrawCallItem.Type)
		{
		case EDreamUIDrawCallType::BatchMesh:
			{
				//compare draw-call item's bounds
				if (OtherDrawCallItem.BatchMeshTreeNode->Overlap(DreamUIQuadTree::Rectangle(InGeo.BoundsMin2DInCanvasSpace, InGeo.BoundsMax2DInCanvasSpace)))
				{
					return true;
				}
			}
			break;
		case EDreamUIDrawCallType::PostProcess:
			{
				auto OtherUIGeo = OtherDrawCallItem.PostProcessVisualObject->GetGeometry();
				//check bounds overlap
				if (IntersectBounds(InGeo.BoundsMin2DInCanvasSpace, InGeo.BoundsMax2DInCanvasSpace, OtherUIGeo->BoundsMin2DInCanvasSpace, OtherUIGeo->BoundsMax2DInCanvasSpace))
				{
					return true;
				}
			}
			break;
		case EDreamUIDrawCallType::DirectMesh://mostly direct mesh are difficult to calculate 2d bounds (particles or static-mesh), so just return true-overlap
				return true;
		}

		return false;
	};

	int FitInDrawCallMinIndex = 0;
	auto CanFitInDrawCall = [&](const FDreamUIGeometry& InGeo, bool InIs2DUI, int32& OutDrawCallIndexToFitin){
		const auto LastDrawCallIndex = InOutUIDrawCallList.Num() - 1;
		if (LastDrawCallIndex < 0)
		{
			return false;
		}

		if (!InIs2DUI)
		{
			//3d UI can only batch into last draw-call
			const auto& LastDrawCall = InOutUIDrawCallList[LastDrawCallIndex];
			if (LastDrawCall.CanConsumeUIGeometryForBatchMesh(InGeo))
			{
				OutDrawCallIndexToFitin = LastDrawCallIndex;
				return true;
			}
			return false;
		}
		TArray<int> CanFitinDrawCallIndexArray;
		//get all draw-call that can fit-in this UI item, then use the first one (because we iterate from tail to head)
		for (int i = LastDrawCallIndex; i >= FitInDrawCallMinIndex; i--)//from tail to head
		{
			const auto& OtherDrawCall = InOutUIDrawCallList[i];
			if (!OtherDrawCall.bIs2DSpace)//draw-call is 3d, can't batch
			{
				return false;
			}

			if (!OtherDrawCall.CanConsumeUIGeometryForBatchMesh(InGeo))//can't fit in this draw-call, should check overlap
			{
				if (OverlapWithOtherDrawCall(InGeo, OtherDrawCall))//overlap with other draw-call, can't batch
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
			//can fit-in this drawcall but also overlap with it, then no need to go deeper because it must not batch in other deeper drawcall
			if (OtherDrawCall.BatchMeshTreeNode->Overlap(DreamUIQuadTree::Rectangle(InGeo.BoundsMin2DInCanvasSpace, InGeo.BoundsMax2DInCanvasSpace)))
			{
				OutDrawCallIndexToFitin = i;
				return true;
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

	auto PushSingleDrawCall = [&](const FDreamUIRenderData& InRenderData, EDreamUIDrawCallType InDrawCallType, bool InIs2DSpace = true) {
		switch (InDrawCallType)
		{
		default:
		case EDreamUIDrawCallType::BatchMesh:
			{
				auto& InItemGeo = InRenderData.BatchMeshGeometry;
				auto DrawCallItem = FDreamUIDrawCall(CanvasRect);
				if (InItemGeo.bIsFont)
				{
					DrawCallItem.FontTexture = InItemGeo.Texture;
					DrawCallItem.Font = InItemGeo.Font;
				}
				else
				{
					DrawCallItem.Texture = InItemGeo.Texture;
				}
				DrawCallItem.Material = InItemGeo.Material.Get();
				DrawCallItem.BatchMeshGeometryArray.Add(InItemGeo);
				DrawCallItem.BatchMeshVisualArray.Add(InRenderData.BatchMeshVisualObject);
				DrawCallItem.VerticesCount = InItemGeo.Vertices.Num();
				DrawCallItem.IndicesCount = InItemGeo.Triangles.Num();
				DrawCallItem.BatchMeshTreeNode->Insert(DreamUIQuadTree::Rectangle(InItemGeo.BoundsMin2DInCanvasSpace, InItemGeo.BoundsMax2DInCanvasSpace));
				DrawCallItem.bIs2DSpace = InIs2DSpace;
				InOutUIDrawCallList.Add(MoveTemp(DrawCallItem));
			}
			break;
		case EDreamUIDrawCallType::PostProcess:
			{
				auto DrawCallItem = FDreamUIDrawCall(InDrawCallType);
				DrawCallItem.PostProcessVisualObject = InRenderData.PostProcessVisualObject;
				DrawCallItem.bIs2DSpace = InIs2DSpace;
				InOutUIDrawCallList.Add(MoveTemp(DrawCallItem));
			}
			break;
		case EDreamUIDrawCallType::DirectMesh:
			{
				auto DrawCallItem = FDreamUIDrawCall(InDrawCallType);
				DrawCallItem.DirectMeshVisualObject = InRenderData.DirectMeshVisualObject;
				DrawCallItem.bIs2DSpace = InIs2DSpace;
				InOutUIDrawCallList.Add(MoveTemp(DrawCallItem));
			}
			break;
		}
	};

	//for sorted ui items, iterate from head to tail, compare draw-call from tail to head
	for (int i = 0; i < InRenderDataArray.Num(); i++)
	{
		auto& RenderData = InRenderDataArray[i];
		switch (RenderData.Type)
		{
		case EDreamUIDrawCallType::ChildCanvas:
			{
				auto ChildCanvasDrawCall = FDreamUIDrawCall(EDreamUIDrawCallType::ChildCanvas);
				ChildCanvasDrawCall.ChildCanvas = RenderData.ChildCanvas;
				InOutUIDrawCallList.Add(MoveTemp(ChildCanvasDrawCall));

				FitInDrawCallMinIndex = InOutUIDrawCallList.Num();
			}
			break;
		case EDreamUIDrawCallType::BatchMesh:
			{
				auto& ItemGeo = RenderData.BatchMeshGeometry;

				bool is2DUIItem = Is2DUITransform(ItemGeo.TransformRelativeToCanvas);
				int DrawCallIndexToFitin;
				if (ItemGeo.bSupportDrawcallBatching && CanFitInDrawCall(ItemGeo, is2DUIItem, DrawCallIndexToFitin))
				{
					auto& DrawCallItem = InOutUIDrawCallList[DrawCallIndexToFitin];
					DrawCallItem.bIs2DSpace = DrawCallItem.bIs2DSpace && is2DUIItem;
					if (ItemGeo.bIsFont)
					{
						if (DrawCallItem.FontTexture != ItemGeo.Texture)
						{
							DrawCallItem.FontTexture = ItemGeo.Texture;
						}
						DrawCallItem.Font = ItemGeo.Font;
					}
					else
					{
						if (DrawCallItem.Texture != ItemGeo.Texture)
						{
							DrawCallItem.Texture = ItemGeo.Texture;
						}
					}
					//add to this draw-call
					DrawCallItem.BatchMeshGeometryArray.Add(ItemGeo);
					DrawCallItem.BatchMeshVisualArray.Add(RenderData.BatchMeshVisualObject);
					DrawCallItem.BatchMeshTreeNode->Insert(DreamUIQuadTree::Rectangle(ItemGeo.BoundsMin2DInCanvasSpace, ItemGeo.BoundsMax2DInCanvasSpace));
					DrawCallItem.VerticesCount += ItemGeo.Vertices.Num();
					DrawCallItem.IndicesCount += ItemGeo.Triangles.Num();
					check(DrawCallItem.VerticesCount < LEXUI_MAX_VERTEX_COUNT);
				}
				else//cannot fit in any other draw-call
				{
					//make a new draw-call
					PushSingleDrawCall(RenderData, EDreamUIDrawCallType::BatchMesh, is2DUIItem);
				}
			}
			break;
		case EDreamUIDrawCallType::DirectMesh:
			{
				//every direct mesh is a draw-call
				bool is2DUIItem = true;//post process just use true because it not matter
				PushSingleDrawCall(RenderData, EDreamUIDrawCallType::DirectMesh, is2DUIItem);
				FitInDrawCallMinIndex = InOutUIDrawCallList.Num();
			}
			break;
		case EDreamUIDrawCallType::PostProcess:
			{
				//every postprocess is a draw-call
				bool is2DUIItem = true;//post process just use true because it not matter
				PushSingleDrawCall(RenderData, EDreamUIDrawCallType::PostProcess, is2DUIItem);
				FitInDrawCallMinIndex = InOutUIDrawCallList.Num();
			}
			break;
		}
	}

	for (auto& DrawCallItem : InOutUIDrawCallList)
	{
		if (DrawCallItem.Type == EDreamUIDrawCallType::BatchMesh)
		{
			DrawCallItem.ApplyBatchMeshGeometryToCombined();
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("Canvas UpdateDrawCall"), STAT_UpdateDrawCall, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas CopyBatchMeshGeometry&UpdateMeshSection"), STAT_CopyBatchMeshGeometry, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas UpdateClipAndGeometry"), STAT_UpdateClipAndGeometry, STATGROUP_DreamGUI);
void UDreamCanvas::UpdateCanvasDrawCall()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateDrawCall)

	//update children canvas
	for (auto& item : ChildrenCanvasArray)
	{
		if (!item.IsValid())continue;
		if (item->bForceRenderToTarget)continue;
		item->UpdateCanvasDrawCall();
	}

	auto DreamWidget = GetWidget();
	if (!DreamWidget)return;
	/**
	 * Why use bPrevIsVisible?:
	 * If Canvas is rendering in frame 1, and in frame 2 the Canvas is disabled(set WidgetActive to false), then the Canvas will not do draw-call calculation, and the prev existing draw-call mesh is still there and render,
	 * so we check bPrevIsVisible, then we can still do draw-call calculation at this frame, and the prev existing draw-call will be removed.
	 */
	const bool bNowIsVisible = DreamWidget->GetRenderVisibleInHierarchy();
	if (bNowIsVisible || bPrevIsVisible)
	{
		if (bNowIsVisible != bPrevIsVisible)
		{
			bCanTickUpdate = true;
		}
		bPrevIsVisible = bNowIsVisible;
	}

	//update draw-call
	bHasPendingUpdateData = false;
	if (bCanTickUpdate)
	{
		bCanTickUpdate = false;
		RootCanvas->bAnythingChangedForRenderTarget = true;
		CheckUIMesh();
		struct LOCAL
		{
			static void CollectRenderWidget(UDreamWidget* Widget
				, UDreamCanvas* ThisCanvas
				, TArray<TObjectPtr<UDreamWidget>>& WidgetCollection)
			{
				WidgetCollection.Add(Widget);//maybe sub-canvas, so collect it before tell canvas
				if (Widget->GetRenderCanvas() == ThisCanvas)
				{
					for (auto Child : Widget->GetChildren())
					{
						CollectRenderWidget(Child, ThisCanvas, WidgetCollection);
					}
				}
			}
		};
		if (bNeedToGenerateWidgetList)
		{
			bNeedToGenerateWidgetList = false;
			WidgetList.Reset();
			LOCAL::CollectRenderWidget(GetWidget(), this, WidgetList);
		}

		CheckWidgetPropertyData();
		WidgetPropertyDataAsTexture->PrepareForBatchUpdate();
		//update clip and geometry from head to tail
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateClipAndGeometry)
			for (const auto& Widget : WidgetList)
			{
				Widget->UpdateClip(RootCanvas->ClipDataAsTexture, RootCanvas->ClipDataList);
				if (Widget->GetRenderVisibleInHierarchy() && Widget->GetRenderCanvas() == this)
				{
					Widget->UpdateVisual();
				}
			}
			// Clips created above are uploaded by RefreshAllClipData, driven every tick from the UI manager.
		}
		WidgetPropertyDataAsTexture->Flush();
		
		if (bShouldRebuildDrawCall)
		{
			bShouldRebuildDrawCall = false;
			NewestDrawCallFrameNumber = GFrameCounter;

			//rect size minimal at 100, so UIQuadTree can work properly (prevent too small rect)
			//@todo: use a better size, maybe screen size (only for screen space UI)
			const auto Width = FMath::Max(DreamWidget->GetWidth(), 100.0f);
			const auto Height = FMath::Max(DreamWidget->GetHeight(), 100.0f);
			FVector2D LeftBottomPoint;
			LeftBottomPoint.X = Width * -DreamWidget->GetPivot().X;
			LeftBottomPoint.Y = Height * -DreamWidget->GetPivot().Y;
			FVector2D RightTopPoint;
			RightTopPoint.X = Width * (1.0f - DreamWidget->GetPivot().X);
			RightTopPoint.Y = Height * (1.0f - DreamWidget->GetPivot().Y);
			//prepare
			{
				FDreamCanvasPreparedDrawCallData PreparedDrawCallData;
				PreparedDrawCallData.LeftBottomPoint = LeftBottomPoint;
				PreparedDrawCallData.RightTopPoint = RightTopPoint;
				PreparedDrawCallData.FrameNumber = GFrameCounter;
				PrepareDrawCallBatchingData(PreparedDrawCallData.DataArray);
				//push to async thread
				DrawCallProcessingRunnable->PushPreparedDrawCallData(MoveTemp(PreparedDrawCallData));
			}
		}
		else
		{
			bHasPendingUpdateData = true;
		}
	}

	if (this == RootCanvas)
	{
		CheckRenderTargetUpdate();
	}
}

void UDreamCanvas::UpdateDrawCallBatchData()
{
	if(!GetWidget()->HasRegistered())return;
	//update children canvas
	for (auto& item : ChildrenCanvasArray)
	{
		if (!item.IsValid())continue;
		if (item->bForceRenderToTarget)continue;
		item->UpdateDrawCallBatchData();
	}

	if (!IsValid(UIMesh))return;

	if (!bAllowDropFrame)
	{
		while (DrawCallProcessingRunnable->IsBatching())
		{
			FPlatformProcess::Sleep(0.001f);
		}
	}

	if (DrawCallProcessingRunnable->TryGetDrawCallData(CurrentDrawCallData))
	{
		//update draw-call mesh
		UpdateDrawCallMesh();
		//update draw-call material
		UpdateDrawCallMaterial();

		if (bNeedToVerifyMaterials)
		{
			bNeedToVerifyMaterials = false;
			UIMesh->VerifyMaterials();
		}

		MarkFinishUpdateCanvasDrawCall();
	}
	else
	{
		if (bHasPendingUpdateData)//make sure there is no pending data in async thread, if there is pending data we may update draw-call with wrong data
		{
			//current draw-call data only need to update, then we compare the frame-number,
			//if frame-number is greater than current rendering draw-call's frame-number, that means we can safely update it
			if (GFrameCounter > CurrentDrawCallData.FrameNumber && CurrentDrawCallData.FrameNumber == NewestDrawCallFrameNumber)
			{
				for (int i = 0; i < CurrentDrawCallData.DrawCallArray.Num(); i++)
				{
					auto& DrawCallItem = CurrentDrawCallData.DrawCallArray[i];
					if (DrawCallItem.Type == EDreamUIDrawCallType::BatchMesh)
					{
						DrawCallItem.CopyBatchMeshGeometry();
						UIMesh->UpdateMeshSection(DrawCallItem.RenderSection, &DrawCallItem);
					}
				}
			}
		}
	}
	if (IsValid(UIMesh))
	{
		UIMesh->FlushRenderCommand();
	}
}

DECLARE_CYCLE_STAT(TEXT("Canvas UpdateDrawCallMesh"), STAT_UpdateDrawCallMesh, STATGROUP_DreamGUI);
void UDreamCanvas::UpdateDrawCallMesh()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateDrawCallMesh);
	if (!IsValid(UIMesh))return;
	UIMesh->PoolAllRenderSection();
	bool bNeedToUpdateBounds = false;
	bool bAnySectionCreated = false;
	for (int i = 0; i < CurrentDrawCallData.DrawCallArray.Num(); i++)
	{
		auto& DrawCallItem = CurrentDrawCallData.DrawCallArray[i];
		switch (DrawCallItem.Type)
		{
		case EDreamUIDrawCallType::DirectMesh:
			{
				if (!DrawCallItem.DirectMeshVisualObject.IsValid())
				{
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Invalid DirectMesh draw-call, will ignore it"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
					continue;
				}
				DrawCallItem.RenderSection = UIMesh->SetupRenderSection(EDreamUIRenderSectionType::DirectMesh, &DrawCallItem);
				bAnySectionCreated = true;
				bNeedToUpdateBounds = true;
			}
			break;
		case EDreamUIDrawCallType::BatchMesh:
			{
				DrawCallItem.RenderSection = UIMesh->SetupRenderSection(EDreamUIRenderSectionType::Mesh, &DrawCallItem);
				bAnySectionCreated = true;
				bNeedToUpdateBounds = true;
			}
			break;
		case EDreamUIDrawCallType::PostProcess:
			{
				//only DreamUI renderer can render post process
				if (this->GetActualRenderMode() == EDreamRenderMode::WorldSpace)
				{
					continue;
				}
				if (!DrawCallItem.PostProcessVisualObject.IsValid())
				{
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Invalid PostProcess draw-call, will ignore it"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
					continue;
				}

				DrawCallItem.RenderSection = UIMesh->SetupRenderSection(EDreamUIRenderSectionType::PostProcess, &DrawCallItem);
				//create new section, need to sort it
				bAnySectionCreated = true;
				bNeedToUpdateBounds = true;
			}
			break;
		case EDreamUIDrawCallType::ChildCanvas:
			{
				if (!DrawCallItem.ChildCanvas.IsValid())
				{
					UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Invalid ChildCanvas draw-call, will ignore it"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
					continue;
				}
				
				DrawCallItem.RenderSection = UIMesh->SetupRenderSection(EDreamUIRenderSectionType::ChildCanvas, &DrawCallItem);
				//create new section, need to sort it
				bAnySectionCreated = true;
				bNeedToUpdateBounds = true;
			}
			break;
		}
	}

	if (bAnySectionCreated)
	{
		// Sections carry fresh (or pooled, stale) priorities and must be re-sorted. Sorting is owned by
		// the nearest override-sorting ancestor or the root (SortDrawCall cascades from there), so the
		// request has to land on the owner: setting it on a plain child canvas fed a flag that its own
		// consume site cleared without ever sorting, and the owner never heard about it.
		if (UDreamCanvas* SortOwner = GetSortOwnerCanvas())
		{
			SortOwner->bNeedToSortRenderPriority = true;
		}
	}

	if (this->IsRootCanvas())
	{
		UIMesh->UpdateChildCanvasSectionBox();
	}
	if (bNeedToUpdateBounds)
	{
		UIMesh->UpdateLocalBounds();//update bounds for UE-Renderer
	}
}

void UDreamCanvas::CheckUIMesh()const
{
	if (!IsValid(UIMesh))
	{
		auto MeshType = DefaultMeshType.Get();
		if (MeshType == nullptr)MeshType = UDreamUIMeshComponent::StaticClass();
		auto DreamWidget = GetWidget();
		auto ObjectName = MakeUniqueObjectName(DreamWidget, MeshType, FName(*this->GetWidget()->GetDisplayName()));
		UIMesh = NewObject<UDreamUIMeshComponent>(DreamWidget, MeshType, ObjectName, RF_Transient);
		UIMesh->RegisterComponentWithWorld(this->GetWorld());
		UIMesh->AttachToComponent(this->GetAttachedRootSceneComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		UIMesh->SetRelativeTransform(FTransform::Identity);
		UIMesh->Init(const_cast<UDreamCanvas*>(this));
		bUIMeshNeedToSetInitialParameters = true;
	}
	if (IsValid(UIMesh))
	{
		UIMesh->SetComponentToWorld(GetWidget()->GetWorldTransform());
	}

	if (bUIMeshNeedToSetInitialParameters)
	{
		bUIMeshNeedToSetInitialParameters = false;
		if (RenderModeIsDreamRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (!GetWorld()->IsGameWorld())//edit mode
			{
				if (ActualRenderMode == EDreamRenderMode::ScreenSpaceOverlay)
					ActualRenderMode = EDreamRenderMode::WorldSpace_DreamUI;
			}
#endif
			switch (ActualRenderMode)
			{
			case EDreamRenderMode::RenderTarget:
			{
				UIMesh->SetSupportDreamUIRenderer(true, this->GetRootCanvas()->GetRenderTargetViewExtension(), false);
				UIMesh->SetSupportUERenderer(false);
			}
			break;
			case EDreamRenderMode::ScreenSpaceOverlay:
			{
#if WITH_EDITOR
				if (!GetWorld()->IsGameWorld())
				{
					UIMesh->SetSupportDreamUIRenderer(true, UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), false);
					UIMesh->SetSupportUERenderer(true);
				}
				else
#endif
				{
					UIMesh->SetSupportDreamUIRenderer(true, UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), false);
					UIMesh->SetSupportUERenderer(false);
				}
			}
			break;
			case EDreamRenderMode::WorldSpace_DreamUI:
			{
				UIMesh->SetSupportDreamUIRenderer(true, UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), true);
				UIMesh->SetSupportUERenderer(false);
			}
			break;
			}
		}
		else
		{
			UIMesh->SetSupportDreamUIRenderer(false, nullptr, false);
			UIMesh->SetSupportUERenderer(true);
		}
	}
}

void UDreamCanvas::SortDrawCall()
{
	if (!IsValid(UIMesh))
	{
		return;
	}
	UIMesh->SetUITranslucentSortPriority(this->GetActualSortOrder());
	int MeshSectionIndex = 0;
	for (int i = 0; i < CurrentDrawCallData.DrawCallArray.Num(); i++)
	{
		auto& DrawCallItem = CurrentDrawCallData.DrawCallArray[i];
		// Only draw-calls that actually produced a section own a priority slot. Skipped draw-calls
		// (invalid objects, WorldSpace post process) have no section, so addressing sections by the
		// draw-call's position both shifted every later priority and, at the tail, indexed past the
		// section array.
		if (DrawCallItem.RenderSection.IsValid())
		{
			UIMesh->SetRenderSectionRenderPriority(DrawCallItem.RenderSection, MeshSectionIndex++);
		}
		if (DrawCallItem.Type == EDreamUIDrawCallType::ChildCanvas && DrawCallItem.ChildCanvas.IsValid())
		{
			DrawCallItem.ChildCanvas->SortDrawCall();
		}
	}

	if (this->IsRootCanvas())
	{
		switch (this->GetActualRenderMode())
		{
		case EDreamRenderMode::ScreenSpaceOverlay:
			UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true)->MarkNeedToSortScreenSpacePrimitiveRenderPriority();
			break;
		case EDreamRenderMode::RenderTarget:
			GetRenderTargetViewExtension()->MarkNeedToSortScreenSpacePrimitiveRenderPriority();
			break;
		case EDreamRenderMode::WorldSpace_DreamUI:
			UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true)->MarkNeedToSortWorldSpacePrimitiveRenderPriority();
			break;
		}
	}
}

FName UDreamCanvas::DreamUI_MainTextureMaterialParameterName = FName(TEXT("DreamUI_MainTexture"));
FName UDreamCanvas::DreamUI_FontTextureMaterialParameterName = FName(TEXT("DreamUI_FontTexture"));
FName UDreamCanvas::DreamUI_FontAtlasInfoMaterialParameterName = FName(TEXT("DreamUI_FontAtlasInfo"));
FName UDreamCanvas::DreamUI_ClipDataTexture_MaterialParameterName = FName(TEXT("DreamUI_ClipDataTexture"));
FName UDreamCanvas::DreamUI_WidgetPropertyDataTexture_MaterialParameterName = FName(TEXT("DreamUI_WidgetPropertyDataTexture"));
FName UDreamCanvas::DreamUI_IsRenderByDreamUIRenderer_MaterialParameterName = FName(TEXT("DreamUI_IsRenderByDreamUIRenderer"));

FVector4f UDreamCanvas::MakeFontAtlasInfo(const FDreamUIDrawCall& DrawCallItem)
{
	// What the MTSDF decode needs from the atlas, for the built-in shader and MF_DreamUI_Shade alike:
	// xy the slice size in texels, z the field range in texels (twice the spread), w texels per em.
	FVector4f Info(1.0f, 1.0f, 0.0f, 0.0f);
	if (DrawCallItem.FontTexture.IsValid())
	{
		Info.X = DrawCallItem.FontTexture->GetSurfaceWidth();
		Info.Y = DrawCallItem.FontTexture->GetSurfaceHeight();
	}
	if (DrawCallItem.Font.IsValid())
	{
		Info.Z = DrawCallItem.Font->GetAtlasFieldRangeTexels();
		Info.W = DrawCallItem.Font->GetAtlasEmTexels();
	}
	return Info;
}

bool UDreamCanvas::IsMaterialContainsDreamUIParameter(const UMaterialInterface* InMaterial)
{
	static TArray<FMaterialParameterInfo> ParameterInfos;
	static TArray<FGuid> ParameterIds;
	InMaterial->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
	auto FoundIndex = ParameterInfos.IndexOfByPredicate([](const FMaterialParameterInfo& Item)
		{
			return
				Item.Name == DreamUI_MainTextureMaterialParameterName
				|| Item.Name == DreamUI_FontTextureMaterialParameterName
				|| Item.Name == DreamUI_ClipDataTexture_MaterialParameterName
				|| Item.Name == DreamUI_WidgetPropertyDataTexture_MaterialParameterName
				|| Item.Name == DreamUI_IsRenderByDreamUIRenderer_MaterialParameterName
				;
		});
	return FoundIndex != INDEX_NONE;
}

DECLARE_CYCLE_STAT(TEXT("Canvas UpdateDrawCallMaterial"), STAT_UpdateDrawCallMaterial, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas SetMaterialParameter"), STAT_SetMaterialParameter, STATGROUP_DreamGUI);
void UDreamCanvas::UpdateDrawCallMaterial()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateDrawCallMaterial);

	//pool and reuse material
	{
		UsingMaterialStartIndex = PooledDefaultMaterialList.Num() - 1;
	}
	//reset index for dynamic material
	{
		for (auto& KeyValue : MapSrcMatToDynamicMat)
		{
			KeyValue.Value.CurrentIndex = 0;
		}
	}

	const bool bUseBuiltInShader = UDreamUISettings::GetUseBuiltInUIShader() && IsRenderByDreamUIRendererOrUERenderer();
	auto SetParameterForNewlyCreatedMaterial = [&](UMaterialInstanceDynamic* InMaterialInstanceDynamic)
	{
		InMaterialInstanceDynamic->SetScalarParameterValue(DreamUI_IsRenderByDreamUIRenderer_MaterialParameterName, this->IsRenderByDreamUIRendererOrUERenderer());
		InMaterialInstanceDynamic->SetTextureParameterValue(DreamUI_WidgetPropertyDataTexture_MaterialParameterName, this->WidgetPropertyDataAsTexture->GetDataTexture());
		InMaterialInstanceDynamic->SetTextureParameterValue(DreamUI_ClipDataTexture_MaterialParameterName, RootCanvas->ClipDataAsTexture->GetDataTexture());
	};

	for (int i = 0; i < CurrentDrawCallData.DrawCallArray.Num(); i++)
	{
		auto& DrawCallItem = CurrentDrawCallData.DrawCallArray[i];
		switch (DrawCallItem.Type)
		{
		case EDreamUIDrawCallType::BatchMesh:
			{
				UMaterialInterface* RenderMat = nullptr;
				bool bShouldSetMaterialParameter = false;
				if (DrawCallItem.Material.IsValid())
				{
					if (DrawCallItem.Material->IsA<UMaterialInstanceDynamic>())
					{
						auto RenderMatDynamic = static_cast<UMaterialInstanceDynamic*>(DrawCallItem.Material.Get());
						RenderMat = RenderMatDynamic;
						bShouldSetMaterialParameter = true;
						SetParameterForNewlyCreatedMaterial(RenderMatDynamic);
					}
					else
					{
						auto DynamicMaterialContainerPtr = MapSrcMatToDynamicMat.Find(DrawCallItem.Material.Get());
						if (!DynamicMaterialContainerPtr)
						{
							if (IsMaterialContainsDreamUIParameter(DrawCallItem.Material.Get()))
							{
								bShouldSetMaterialParameter = true;
								auto RenderMatDynamic = UMaterialInstanceDynamic::Create(DrawCallItem.Material.Get(), this);
								SetParameterForNewlyCreatedMaterial(RenderMatDynamic);
								auto MaterialContainer = FDreamCanvasDynamicMaterialArrayContainer();
								MaterialContainer.MaterialArray.Add(RenderMatDynamic);
								MaterialContainer.CurrentIndex = 1;
								MapSrcMatToDynamicMat.Add(DrawCallItem.Material.Get(), MaterialContainer);
								RenderMat = RenderMatDynamic;
								for (auto& BatchMeshVisual : DrawCallItem.BatchMeshVisualArray)
								{
									if (!BatchMeshVisual.IsValid())continue;
									BatchMeshVisual->OnMaterialInstanceDynamicCreated(RenderMatDynamic);
								}
								bNeedToVerifyMaterials = true;//verify material when new material will be used
							}
							else
							{
								RenderMat = DrawCallItem.Material.Get();
								bNeedToVerifyMaterials = true;//verify material when new material will be used
							}
						}
						else
						{
							bShouldSetMaterialParameter = true;
							auto& MaterialArray = DynamicMaterialContainerPtr->MaterialArray;
							if (!MaterialArray.IsValidIndex(DynamicMaterialContainerPtr->CurrentIndex))//material use up, need more
							{
								auto RenderMatDynamic = UMaterialInstanceDynamic::Create(DrawCallItem.Material.Get(), this);
								MaterialArray.Add(RenderMatDynamic);
								SetParameterForNewlyCreatedMaterial(RenderMatDynamic);
								RenderMat = RenderMatDynamic;
								DynamicMaterialContainerPtr->CurrentIndex++;
								bNeedToVerifyMaterials = true;//verify material when new material will be used
								for (auto& BatchMeshVisual : DrawCallItem.BatchMeshVisualArray)
								{
									if (!BatchMeshVisual.IsValid())continue;
									BatchMeshVisual->OnMaterialInstanceDynamicCreated(RenderMatDynamic);
								}
							}
							else//enough material, use index one
							{
								auto RenderMatDynamic = MaterialArray[DynamicMaterialContainerPtr->CurrentIndex];
								RenderMat = RenderMatDynamic;
								if (bWidgetPropertyDataAsTextureChanged || RootCanvas->bClipDataAsTextureChanged)
								{
									SetParameterForNewlyCreatedMaterial(RenderMatDynamic);//update texture to material
								}
								DynamicMaterialContainerPtr->CurrentIndex++;
								for (auto& BatchMeshVisual : DrawCallItem.BatchMeshVisualArray)
								{
									if (!BatchMeshVisual.IsValid())continue;
									BatchMeshVisual->OnMaterialInstanceDynamicCreated(RenderMatDynamic);
								}
							}
						}
					}
				}
				else if (bUseBuiltInShader)
				{
					// No material at all: the renderer draws this section with the built-in UI shader.
					FDreamUIBuiltInDrawParams BuiltIn;
					BuiltIn.bEnabled = true;
					BuiltIn.MainTexture = DrawCallItem.Texture.IsValid() ? DrawCallItem.Texture->GetResource() : nullptr;
					BuiltIn.FontTexture = DrawCallItem.FontTexture.IsValid() ? DrawCallItem.FontTexture->GetResource() : nullptr;
					BuiltIn.WidgetDataTexture = WidgetPropertyDataAsTexture->GetDataTexture() ? WidgetPropertyDataAsTexture->GetDataTexture()->GetResource() : nullptr;
					BuiltIn.ClipDataTexture = RootCanvas->ClipDataAsTexture->GetDataTexture() ? RootCanvas->ClipDataAsTexture->GetDataTexture()->GetResource() : nullptr;
					const FVector4f AtlasInfo = MakeFontAtlasInfo(DrawCallItem);
					BuiltIn.FontAtlasSize = FVector2f(AtlasInfo.X, AtlasInfo.Y);
					BuiltIn.FontFieldRangeTexels = AtlasInfo.Z;
					BuiltIn.FontEmTexels = AtlasInfo.W;
					UIMesh->SetMeshSectionBuiltIn(i, BuiltIn);
					UIMesh->SetMeshSectionMaterial(i, nullptr);
					break;
				}
				else
				{
					auto GetUIMaterialFromPool = [&]()
					{
						if (UsingMaterialStartIndex < 0)
						{
							auto SrcMaterial = GetDefaultMaterial();
							auto RenderMatDynamic = UMaterialInstanceDynamic::Create(SrcMaterial, this);
							RenderMatDynamic->SetFlags(RF_Transient);
							PooledDefaultMaterialList.Add(RenderMatDynamic);
							SetParameterForNewlyCreatedMaterial(RenderMatDynamic);
							bNeedToVerifyMaterials = true;//verify material when new material will be used
							return RenderMatDynamic;
						}
						auto RenderMatDynamic = PooledDefaultMaterialList[UsingMaterialStartIndex];
						if (bWidgetPropertyDataAsTextureChanged || RootCanvas->bClipDataAsTextureChanged)
						{
							SetParameterForNewlyCreatedMaterial(RenderMatDynamic);//update texture to material
						}
						UsingMaterialStartIndex--;
						return RenderMatDynamic.Get();
					};
					RenderMat = GetUIMaterialFromPool();
					bShouldSetMaterialParameter = true;//pooled material definitely contains DreamUIParam
				}
				if (bShouldSetMaterialParameter)
				{
					SCOPE_CYCLE_COUNTER(STAT_SetMaterialParameter)
					auto RenderMat_MID = static_cast<UMaterialInstanceDynamic*>(RenderMat);
					auto& ParamCache = MapMatToParamCache.FindOrAdd(RenderMat_MID);
					if (ParamCache.Texture != DrawCallItem.Texture || ParamCache.FontTexture != DrawCallItem.FontTexture)
					{
						RenderMat_MID->SetTextureParameterValue(DreamUI_MainTextureMaterialParameterName, DrawCallItem.Texture.Get());
						RenderMat_MID->SetTextureParameterValue(DreamUI_FontTextureMaterialParameterName, DrawCallItem.FontTexture.Get());
						// The atlas geometry travels with the atlas: a new font texture means new values.
						const FVector4f AtlasInfo = MakeFontAtlasInfo(DrawCallItem);
						RenderMat_MID->SetVectorParameterValue(DreamUI_FontAtlasInfoMaterialParameterName, FLinearColor(AtlasInfo.X, AtlasInfo.Y, AtlasInfo.Z, AtlasInfo.W));
						ParamCache.Texture = DrawCallItem.Texture;
						ParamCache.FontTexture = DrawCallItem.FontTexture;
					}
					if (bNeedToSetClipDataTextureMaterialParameter)
					{
						RenderMat_MID->SetTextureParameterValue(DreamUI_ClipDataTexture_MaterialParameterName, RootCanvas->ClipDataAsTexture->GetDataTexture());
					}
				}
				if (UIMesh->IsMeshSectionBuiltIn(i))
				{
					UIMesh->SetMeshSectionBuiltIn(i, FDreamUIBuiltInDrawParams());
				}
				UIMesh->SetMeshSectionMaterial(i, RenderMat);
			}
			break;
		case EDreamUIDrawCallType::PostProcess:
		case EDreamUIDrawCallType::ChildCanvas:
		case EDreamUIDrawCallType::DirectMesh:
			{

			}
			break;
		}
	}

	if (bNeedToVerifyMaterials
		|| CurrentDrawCallData.DrawCallArray.Num() == 0
		)
	{
		MarkNeedVerifyMaterials();//tell parent canvas to verify material
	}

	bNeedToSetClipDataTextureMaterialParameter = false;
	bWidgetPropertyDataAsTextureChanged = false;
	if (RootCanvas == this)
	{
		RootCanvas->bClipDataAsTextureChanged = false;
	}
}

void UDreamCanvas::MarkNeedVerifyMaterials()
{
	bNeedToVerifyMaterials = true;
	if (ParentCanvas.IsValid()
		&& !this->GetOverrideSorting()//if override sorting, then render by self(not parent)
		)
	{
		ParentCanvas->MarkNeedVerifyMaterials();
	}
}

void UDreamCanvas::SetRenderTargetResolutionScale(float Value)
{
	if (RenderTargetResolutionScale != Value)
	{
		RenderTargetResolutionScale = Value;
		bAnythingChangedForRenderTarget = true;
	}
}

void UDreamCanvas::SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode Value)
{
	if (RenderTargetSizeMode != Value)
	{
		RenderTargetSizeMode = Value;
		bAnythingChangedForRenderTarget = true;
	}
}

void UDreamCanvas::SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode Value)
{
	if (RenderTargetUpdateMode != Value)
	{
		RenderTargetUpdateMode = Value;
		bAnythingChangedForRenderTarget = true;
	}
}

void UDreamCanvas::RequestUpdateForRenderTarget()
{
	if (RootCanvas == this)
	{
		bRequestUpdateForRenderTarget = true;
	}
}

void UDreamCanvas::SetSortOrderAdditionalValueRecursive(int32 InAdditionalValue)
{
	if (FMath::Abs(this->SortOrder + InAdditionalValue) > MAX_int16)
	{
		auto errorMsg = FText::Format(LOCTEXT("SortOrderOutOfRange", "{0} sortOrder out of range!\nNOTE! sortOrder value is stored with int16 type, so valid range is -32768 to 32767")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(DreamGUI, Error, TEXT("%s"), *errorMsg.ToString());
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(errorMsg, false);
#endif
		return;
	}

	this->SortOrder += InAdditionalValue;
	for (auto ChildCanvas : ChildrenCanvasArray)
	{
		if (!ChildCanvas.IsValid())continue;
		if (ChildCanvas->bForceRenderToTarget)continue;
		ChildCanvas->SetSortOrderAdditionalValueRecursive(InAdditionalValue);
	}
}

void UDreamCanvas::SetSortOrder(int32 InSortOrder, bool InPropagateToChildrenCanvas)
{
	if (SortOrder != InSortOrder)
	{
		if (CheckRootCanvas())
		{
			RootCanvas->bNeedToSortRenderPriority = true;
		}
		MarkCanvasUpdate(false);
		if (InPropagateToChildrenCanvas)
		{
			int32 Diff = InSortOrder - SortOrder;
			SetSortOrderAdditionalValueRecursive(Diff);
		}
		else
		{
			if (FMath::Abs(InSortOrder) > MAX_int16)
			{
				auto errorMsg = FText::Format(LOCTEXT("SortOrderOutOfRange", "{0} sortOrder out of range!\nNOTE! sortOrder value is stored with int16 type, so valid range is -32768 to 32767")
					, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
				UE_LOG(DreamGUI, Error, TEXT("%s"), *errorMsg.ToString());
#if WITH_EDITOR
				FDreamUIUtils::EditorNotification(errorMsg, false);
#endif
				InSortOrder = FMath::Clamp(InSortOrder, (int32)MIN_int16, (int32)MAX_int16);
			}
			this->SortOrder = InSortOrder;
		}

		if (CheckRootCanvas())
		{
			RootCanvas->bNeedToSortRenderPriority = true;
		}
	}
}
void UDreamCanvas::SetSortOrderToHighestOfHierarchy(bool InPropagateToChildrenCanvas)
{
	int32 Min = INT_MAX, Max = INT_MIN;
	GetMinMaxSortOrderOfHierarchy(Min, Max);
	SetSortOrder(Max + 1, InPropagateToChildrenCanvas);
}
void UDreamCanvas::SetSortOrderToLowestOfHierarchy(bool InPropagateToChildrenCanvas)
{
	int32 Min = INT_MAX, Max = INT_MIN;
	GetMinMaxSortOrderOfHierarchy(Min, Max);
	SetSortOrder(Min - 1, InPropagateToChildrenCanvas);
}

void UDreamCanvas::GetMinMaxSortOrderOfHierarchy(int32& OutMin, int32& OutMax)
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
		if (!ChildCanvas.IsValid())continue;
		if (ChildCanvas->bForceRenderToTarget)continue;
		ChildCanvas->GetMinMaxSortOrderOfHierarchy(OutMin, OutMax);
	}
}


UMaterialInterface* UDreamCanvas::GetDefaultMaterial()const
{
	if (!DefaultMaterial)
	{
		DefaultMaterial = UDreamGUISettings::LoadSetting(UDreamGUISettings::Get()->DefaultUIMaterial, TEXT("DefaultUIMaterial"));
		if (!DefaultMaterial)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d Load DefaultMaterial error! Missing some content of DreamUI plugin, reinstall this plugin may fix the issue."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		}
	}
	return DefaultMaterial;
}

void UDreamCanvas::SetDefaultMaterial(UMaterialInterface* InMaterial)
{
	if (DefaultMaterial != InMaterial)
	{
		ClearDrawCall();
		MarkCanvasUpdate(true);
	}
}

void UDreamCanvas::SetTraceChannel(TEnumAsByte<ETraceTypeQuery> InTraceChannel)
{
	if (TraceChannel != InTraceChannel)
	{
		TraceChannel = InTraceChannel;
	}
}

float UDreamCanvas::GetActualBlendDepth()const
{
	if (IsRootCanvas())
	{
		return BlendDepth;
	}
	else
	{
		if (GetOverrideBlendDepth())
		{
			return BlendDepth;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualBlendDepth();
			}
		}
	}
	return BlendDepth;
}

int UDreamCanvas::GetActualDepthFade()const
{
	if (IsRootCanvas())
	{
		return DepthFade;
	}
	else
	{
		if (GetOverrideDepthFade())
		{
			return DepthFade;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualDepthFade();
			}
		}
	}
	return DepthFade;
}

int32 UDreamCanvas::GetActualSortOrder()const
{
	if (IsRootCanvas())
	{
		if (bOverrideSorting)
		{
			return SortOrder;
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
			return SortOrder;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualSortOrder();
			}
		}
	}
	return SortOrder;
}

void UDreamCanvas::SetOverrideSorting(bool Value)
{
	if (bOverrideSorting != Value)
	{
		bOverrideSorting = Value;
		if (CheckRootCanvas())
		{
			RootCanvas->bNeedToSortRenderPriority = true;
		}
		MarkCanvasUpdate(false);
	}
}

bool UDreamCanvas::GetActualRequireNormalAndTangent()const
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
void UDreamCanvas::SetRequireNormalAndTangent(bool Value)
{
	if (bRequireNormalAndTangent != Value)
	{
		bRequireNormalAndTangent = Value;
		MarkCanvasUpdate(false);
		if (auto DreamWidget = GetWidget())
		{
			DreamWidget->MarkAllDirtyRecursive();
		}
	}
}

void UDreamCanvas::BuildProjectionMatrix(FIntPoint InViewportSize, ECameraProjectionMode::Type InProjectionType, float InFOV, float FarClipPlane, float NearClipPlane, FMatrix& OutProjectionMatrix)
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
		float XAxisMultiplier = 1.0f;
		float YAxisMultiplier = InViewportSize.X / (float)InViewportSize.Y;

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
float UDreamCanvas::CalculateDistanceToCamera()const
{
	if (ProjectionType == ECameraProjectionMode::Orthographic)
	{
		return 1000;
	}
	else
	{
		if (auto DreamWidget = GetWidget())
		{
			return DreamWidget->GetWidth() * 0.5f / FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f)) * DreamWidget->GetWorldScale().X;
		}
		return 1;
	}
}
FMatrix UDreamCanvas::GetViewProjectionMatrix()const
{
	if (bIsViewProjectionMatrixDirty)
	{
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
		CacheViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;
	}
	return CacheViewProjectionMatrix;
}
FMatrix UDreamCanvas::GetProjectionMatrix()const
{
	if (bOverrideProjectionMatrix)
		return OverrideProjectionMatrix;

	FMatrix ProjectionMatrix = FMatrix::Identity;
	const float FOV = (bOverrideFovAngle ? OverrideFovAngle : FieldOfView) * (float)PI / 360.0f;
	auto DreamWidget = GetWidget();
	BuildProjectionMatrix(FIntPoint(DreamWidget->GetWidth(), DreamWidget->GetHeight()), ProjectionType, FOV, FarClipPlane, NearClipPlane, ProjectionMatrix);
	return ProjectionMatrix;
}
FVector UDreamCanvas::GetViewLocation()const
{
	if (bOverrideViewLocation)
		return OverrideViewLocation;

	auto DreamWidget = GetWidget();
	return DreamWidget->GetWorldLocation() - DreamWidget->GetForwardVector() * CalculateDistanceToCamera();
}
FRotator UDreamCanvas::GetViewRotator()const
{
	if (bOverrideViewRotation)
		return OverrideViewRotation;

	return GetWidget()->GetWorldRotation().Rotator();
}
FIntPoint UDreamCanvas::GetViewportSize()const
{
	auto TempViewportSize = FIntPoint(2, 2);
	if (auto world = this->GetWorld())
	{
#if WITH_EDITOR
		if (!world->IsGameWorld())
		{
			if (auto DreamWidget = GetWidget())
			{
				TempViewportSize.X = DreamWidget->GetWidth();
				TempViewportSize.Y = DreamWidget->GetHeight();
			}
		}
		else
#endif
		{
			if (RenderMode == EDreamRenderMode::ScreenSpaceOverlay)
			{
				if (auto pc = world->GetFirstPlayerController())
				{
					pc->GetViewportSize(TempViewportSize.X, TempViewportSize.Y);
				}
			}
			else if (RenderMode == EDreamRenderMode::RenderTarget && IsValid(RenderTarget))
			{
				TempViewportSize.X = RenderTarget->SizeX / RenderTargetResolutionScale;
				TempViewportSize.Y = RenderTarget->SizeY / RenderTargetResolutionScale;
			}
		}
	}
	return TempViewportSize;
}

void UDreamCanvas::SetRenderMode(EDreamRenderMode Value)
{
	if (RenderMode != Value)
	{
		RenderMode = Value;
		MarkCanvasUpdate(true);
		CheckRenderMode(true);

		UnregisterCanvasScaler();
		RegisterCanvasScaler();
	}
}

void UDreamCanvas::SetForceRenderToTarget(bool Value)
{
	if (bForceRenderToTarget != Value)
	{
		bForceRenderToTarget = Value;
		if (bForceRenderToTarget)
		{
			MarkCanvasUpdate(true);
			GetWidget()->MarkAllDirtyRecursive();
		}
	}
}

void UDreamCanvas::SetProjectionParameters(TEnumAsByte<ECameraProjectionMode::Type> InProjectionType, float InFovAngle, float InNearClipPlane, float InFarClipPlane)
{
	ProjectionType = InProjectionType;
	FieldOfView = InFovAngle;
	NearClipPlane = InNearClipPlane;
	FarClipPlane = InFarClipPlane;

	bIsViewProjectionMatrixDirty = true;
}

void UDreamCanvas::SetRenderTarget(UTextureRenderTarget2D* Value)
{
	if (RenderTarget != Value)
	{
		RenderTarget = Value;
		if (CheckRootCanvas() && RootCanvas == this)
		{
			UpdateRenderTarget(false);
		}
		OnRenderTargetChanged.Broadcast(RenderTarget);
	}
}

void UDreamCanvas::SetRenderTargetClearColor(FColor Value)
{
	if (RenderTargetClearColor != Value)
	{
		RenderTargetClearColor = Value;
		if (CheckRootCanvas() && RootCanvas == this)
		{
			this->bRequestUpdateForRenderTarget = true;
			this->MarkCanvasUpdate(false);
		}
	}
}

EDreamRenderMode UDreamCanvas::GetActualRenderMode()const
{
	if (IsRootCanvas())
	{
		return this->RenderMode;
	}
	else
	{
		if (bForceRenderToTarget)
		{
			checkf(this->RenderMode == EDreamRenderMode::RenderTarget, TEXT("[%s].%d This error should not happen!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return this->RenderMode;
		}
		if (CheckRootCanvas())
		{
			return RootCanvas->RenderMode;
		}
	}
	return EDreamRenderMode::WorldSpace;
}

void UDreamCanvas::SetBlendDepth(float Value)
{
	if (BlendDepth != Value)
	{
		BlendDepth = Value;

		if (CheckRootCanvas())
		{
			if (RootCanvas->RenderModeIsDreamRendererOrUERenderer(CurrentRenderMode))
			{
				if (RootCanvas->IsRenderToWorldSpace())
				{
					auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
					if (ViewExtension.IsValid())
					{
						ViewExtension->SetRenderCanvasDepthParameter(this, this->GetActualBlendDepth(), this->GetActualDepthFade());
					}
				}
			}
		}
	}
}

void UDreamCanvas::SetDepthFade(int Value)
{
	if (DepthFade != Value)
	{
		DepthFade = Value;

		if (CheckRootCanvas())
		{
			if (RootCanvas->RenderModeIsDreamRendererOrUERenderer(CurrentRenderMode))
			{
				if (RootCanvas->IsRenderToWorldSpace())
				{
					auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
					if (ViewExtension.IsValid())
					{
						ViewExtension->SetRenderCanvasDepthParameter(this, this->GetActualBlendDepth(), this->GetActualDepthFade());
					}
				}
			}
		}
	}
}

void UDreamCanvas::SetEnableDepthTest(bool Value)
{
	if (bEnableDepthTest != Value)
	{
		bEnableDepthTest = Value;
	}
}

UTextureRenderTarget2D* UDreamCanvas::GetActualRenderTarget()const
{
	if (IsRootCanvas())
	{
		return this->RenderTarget;
	}
	else
	{
		if (CheckRootCanvas())
		{
			return RootCanvas->RenderTarget;
		}
	}
	return nullptr;
}

int32 UDreamCanvas::GetDrawCallCount()const
{
	int32 Result = 0;
	for (auto& Item : CurrentDrawCallData.DrawCallArray)
	{
		if (Item.Type != EDreamUIDrawCallType::ChildCanvas)
		{
			Result++;
		}
	}
	return Result;
}

void UDreamCanvas::OnClipDataTextureChanged(UTexture* NewTexture)
{
	check(this == RootCanvas);//only root canvas use ClipDataTexture
	MarkCanvasUpdate(true);
	bClipDataAsTextureChanged = true;
}

void UDreamCanvas::OnWidgetPropertyDataTextureChanged(UTexture* NewTexture)
{
	MarkCanvasUpdate(true);
	bWidgetPropertyDataAsTextureChanged = true;
}

void UDreamCanvas::CheckWidgetPropertyData()
{
	if (!IsValid(WidgetPropertyDataAsTexture))
	{
		WidgetPropertyDataAsTexture = NewObject<UDreamUIDataAsTexture>(this, UDreamUIDataAsTexture::StaticClass(), NAME_None, RF_Transient);
		WidgetPropertyDataAsTexture->Init(UDreamVisual::WidgetPropertyDataLength, EDreamUIDataAsTexturePixelFormat::R32, 128);
		WidgetPropertyDataAsTexture->OnDataTextureChange.AddUObject(this, &UDreamCanvas::OnWidgetPropertyDataTextureChanged);
	}
}

void UDreamCanvas::PushAsyncFunction_TransformVertices(TFunction<void()> InFunction)
{
	TransformVerticesAsyncFunctionRunnable->PushFunction(MoveTemp(InFunction));
}

void UDreamCanvas::RemoveClipData(const TSharedPtr<FDreamUIClipData>& InClipData)
{
	RootCanvas->ClipDataList.Remove(InClipData);
}
UTexture* UDreamCanvas::GetClipDataTexture()const
{
	return IsValid(RootCanvas->ClipDataAsTexture) ? RootCanvas->ClipDataAsTexture->GetDataTexture() : nullptr;
}

FTransform2D UDreamCanvas::ConvertTo2DTransform(const FTransform& Transform)
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
void UDreamCanvas::CalculateVisual2DBounds(UDreamVisual* Visual, const FTransform2D& OutTransform2D, FVector2D& OutMin, FVector2D& OutMax)
{
	FVector2D LocalPoint1, LocalPoint2;
	Visual->GetGeometryBoundsInLocalSpace(LocalPoint1, LocalPoint2);
	const auto Point1 = OutTransform2D.TransformPoint(LocalPoint1);
	const auto Point2 = OutTransform2D.TransformPoint(LocalPoint2);
	const auto Point3 = OutTransform2D.TransformPoint(FVector2D(LocalPoint2.X, LocalPoint1.Y));
	const auto Point4 = OutTransform2D.TransformPoint(FVector2D(LocalPoint1.X, LocalPoint2.Y));

	GetMinMax(Point1.X, Point2.X, Point3.X, Point4.X, OutMin.X, OutMax.X);
	GetMinMax(Point1.Y, Point2.Y, Point3.Y, Point4.Y, OutMin.Y, OutMax.Y);
}

#undef LOCTEXT_NAMESPACE


#pragma region CanvasScaler
void UDreamCanvasCustomScale::Init(UDreamCanvas* InCanvas)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveInit(InCanvas);
	}
}
void UDreamCanvasCustomScale::CalculateSizeAndScale(UDreamCanvas* InCanvas, const FIntPoint& InViewportSize, FIntPoint& OutDreamGUICanvasSize, float& OutScale)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveCalculateSizeAndScale(InCanvas, InViewportSize, OutDreamGUICanvasSize, OutScale);
	}
}

bool UDreamCanvasCustomScale::ConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result) const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveConvertPositionFromViewportToCanvas(InPosition, Result);
	}
	return false;
}

bool UDreamCanvasCustomScale::ConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result) const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveConvertPositionFromCanvasToViewport(InPosition, Result);
	}
	return false;
}

void UDreamCanvas::CheckAndApplyViewportParameter()
{
	switch (this->GetRenderMode())
	{
	case EDreamRenderMode::ScreenSpaceOverlay:
	{
		ViewportSize = this->GetViewportSize();
		OnViewportParameterChanged();
	}
	break;
	case EDreamRenderMode::RenderTarget:
	{
		if (IsValid(RenderTarget))
		{
			ViewportSize.X = RenderTarget->SizeX / RenderTargetResolutionScale;
			ViewportSize.Y = RenderTarget->SizeY / RenderTargetResolutionScale;
			OnViewportParameterChanged();
		}
	}
	break;
	}
}

void UDreamCanvas::RegisterCanvasScaler()
{
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld() && this->IsRootCanvas())
	{
		if (auto DreamUIManagerObject = UDreamUIManagerObject::GetInstance(true))
		{
			EditorTickDelegateHandle = DreamUIManagerObject->GetEditorTickDelegate().AddWeakLambda(this, [this](float deltaTime) {
				this->OnEditorTick(deltaTime);
				});
		}
	}
#endif

	bIsViewProjectionMatrixDirty = true;

	if (this->IsRootCanvas())
	{
		if (this->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay
			|| this->GetRenderMode() == EDreamRenderMode::RenderTarget
			)
		{
			CheckAndApplyViewportParameter();

			if (this->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay)
			{
				if (auto world = GetWorld())
				{
					if (auto gameViewport = world->GetGameViewport())
					{
						if (auto viewport = gameViewport->Viewport)
						{
							ViewportResizeDelegateHandle = viewport->ViewportResizedEvent.AddWeakLambda(this, [this](FViewport*, uint32)
							{
								CheckAndApplyViewportParameter();
							});
						}
					}
				}
			}
		}
	}
}

void UDreamCanvas::UnregisterCanvasScaler()
{
#if WITH_EDITOR
	if (EditorTickDelegateHandle.IsValid())
	{
		if (auto DreamUIManagerObject = UDreamUIManagerObject::GetInstance(false))
		{
			DreamUIManagerObject->GetEditorTickDelegate().Remove(EditorTickDelegateHandle);
		}
	}
#endif
	//reset the canvasScale to default
	CanvasScale = 1.0f;

	if (ViewportResizeDelegateHandle.IsValid())
	{
		if (auto world = GetWorld())
		{
			if (auto gameViewport = world->GetGameViewport())
			{
				if (auto viewport = gameViewport->Viewport)
				{
					viewport->ViewportResizedEvent.Remove(ViewportResizeDelegateHandle);
				}
			}
		}
	}
}

void UDreamCanvas::CalculateCanvasSizeAndScale(FIntPoint InViewportSize, FVector2D& OutCanvasSize, float& OutScale)
{
	OutCanvasSize = FVector2D(InViewportSize.X, InViewportSize.Y);
	OutScale = 1.0f;
	if (InViewportSize.X <= 0 || InViewportSize.Y <= 0)return;

	switch (ScaleMode)
	{
	case EDreamCanvasScaleMode::ConstantPixelSize:
		{
			OutCanvasSize = FVector2D(InViewportSize.X, InViewportSize.Y);
			OutScale = 1.0f;
		}
		break;
	case EDreamCanvasScaleMode::ScaleWithScreenSize:
		{
			switch (ScreenMatchMode)
			{
			case EDreamCanvasScreenMatchMode::MatchWidthOrHeight:
				{
					float matchWidth_PreferredWidth = ReferenceResolution.X;
					float matchWidth_PreferredHeight = ReferenceResolution.X * InViewportSize.Y / InViewportSize.X;
					float matchWidth_ScaleRatio = InViewportSize.X / ReferenceResolution.X;

					float matchHeight_PreferredHeight = ReferenceResolution.Y;
					float matchHeight_PreferredWidth = ReferenceResolution.Y * InViewportSize.X / InViewportSize.Y;
					float matchHeight_ScaleRatio = InViewportSize.Y / ReferenceResolution.Y;

					OutCanvasSize.X = FMath::Lerp(matchWidth_PreferredWidth, matchHeight_PreferredWidth, MatchFromWidthToHeight);
					OutCanvasSize.Y = FMath::Lerp(matchWidth_PreferredHeight, matchHeight_PreferredHeight, MatchFromWidthToHeight);

					OutScale = FMath::Lerp(matchWidth_ScaleRatio, matchHeight_ScaleRatio, MatchFromWidthToHeight);
				}
				break;
			case EDreamCanvasScreenMatchMode::Expand:
			case EDreamCanvasScreenMatchMode::Shrink:
				{
					float resultWidth = InViewportSize.X, resultHeight = InViewportSize.Y;

					float screenAspect = (float)InViewportSize.X / InViewportSize.Y;
					float referenceAspect = ReferenceResolution.X / ReferenceResolution.Y;
					if (screenAspect > referenceAspect)//screen width > reference width
					{
						if (ScreenMatchMode == EDreamCanvasScreenMatchMode::Shrink)
						{
							resultHeight = ReferenceResolution.Y;
							resultWidth = resultHeight * screenAspect;
							OutScale = (float)InViewportSize.Y / resultHeight;
						}
						else if (ScreenMatchMode == EDreamCanvasScreenMatchMode::Expand)
						{
							resultWidth = ReferenceResolution.X;
							resultHeight = resultWidth / screenAspect;
							OutScale = (float)InViewportSize.X / resultWidth;
						}
					}
					else//screen height > reference height
					{
						if (ScreenMatchMode == EDreamCanvasScreenMatchMode::Shrink)
						{
							resultWidth = ReferenceResolution.X;
							resultHeight = resultWidth / screenAspect;
							OutScale = (float)InViewportSize.X / resultWidth;
						}
						else if (ScreenMatchMode == EDreamCanvasScreenMatchMode::Expand)
						{
							resultHeight = ReferenceResolution.Y;
							resultWidth = resultHeight * screenAspect;
							OutScale = (float)InViewportSize.Y / resultHeight;
						}
					}
					OutCanvasSize = FVector2D(resultWidth, resultHeight);
				}
				break;
			}
		}
		break;
	case EDreamCanvasScaleMode::ScaleWithEngineDPI:
		{
			// Ask the engine for the same number UMG uses rather than reimplementing the curve, so
			// the two cannot disagree and a project that retunes its DPI curve moves both at once.
			const float DPIScale = GetDefault<UUserInterfaceSettings>()->GetDPIScaleBasedOnSize(InViewportSize);
			OutScale = DPIScale > UE_KINDA_SMALL_NUMBER ? DPIScale : 1.0f;
			// SDPIScaler's arrangement, verbatim: lay out in viewport/scale units and render at
			// scale. That is what keeps the layout rect at the curve's design size instead of
			// shrinking it with the window, which is the whole difference from ScaleWithScreenSize.
			OutCanvasSize = FVector2D(InViewportSize.X / OutScale, InViewportSize.Y / OutScale);
		}
		break;
	case EDreamCanvasScaleMode::Custom:
		{
			if (IsValid(CustomScale))
			{
				OutScale = 1.0f;
				auto ScaledViewportSize = InViewportSize;
				CustomScale->CalculateSizeAndScale(this, InViewportSize, ScaledViewportSize, OutScale);
				OutCanvasSize = FVector2D(ScaledViewportSize.X, ScaledViewportSize.Y);
			}
			else
			{
				//default is constant pixel
				OutCanvasSize = FVector2D(InViewportSize.X, InViewportSize.Y);
				OutScale = 1.0f;
			}
		}
		break;
	}
}

void UDreamCanvas::OnViewportParameterChanged()
{
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)return;
	if (this->IsRootCanvas())
	{
		if (this->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay
			|| this->GetRenderMode() == EDreamRenderMode::RenderTarget
			)
		{
			if (auto DreamWidget = GetWidget())
			{
				FVector2D NewCanvasSize;
				float TempCanvasScale = 1.0f;
				CalculateCanvasSizeAndScale(ViewportSize, NewCanvasSize, TempCanvasScale);
				DreamWidget->SetWidth(NewCanvasSize.X);
				DreamWidget->SetHeight(NewCanvasSize.Y);
				this->CanvasScale = TempCanvasScale;

				DreamWidget->MarkAllDirtyRecursive();
				this->MarkCanvasUpdate(true);
			}
		}
	}
}
#if WITH_EDITOR
void UDreamCanvas::OnEditorTick(float DeltaTime)
{
	if (!GetWorld())
		return;
	if (GetWorld()->IsGameWorld())//When hit play there is still an editor world and DrawViewportArea is called, which could cause frame dropdown, so skip it when playing
		return;
	if (this->IsUnreachable())
		return;
	if (auto WidgetPresenter = this->GetAttachedRootSceneComponent())
	{
		if (WidgetPresenter->GetName().Contains(TEXT("SKEL_")) || WidgetPresenter->GetName().Contains(TEXT("TRASH_")))
			return;
	}

	if (this->IsRootCanvas() && !this->bForceRenderToTarget)
	{
		if (this->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay
			|| this->GetRenderMode() == EDreamRenderMode::RenderTarget
			)
		{
			// The editor tick can still reach a canvas whose world has gone -- closing a prefab
			// editor, ending PIE, changing level -- and every accessor below resolves through the
			// world subsystem, which returns null for an invalid world. Dereferencing it unguarded
			// is what crashed here in UDreamUISelection::IsSelected. Resolving it once also means a
			// valid subsystem implies a valid world, so GetWorld() below needs no second check.
			UDreamUIManagerWorldSubsystem* ManagerSubsystem = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld());
			if (ManagerSubsystem == nullptr)
			{
				return;
			}
			DrawViewportArea();
			if (ManagerSubsystem->GetSelection()->IsSelected(this->GetWidget()))
			{
				if (auto ViewportClient = ManagerSubsystem->GetEditorViewportClient())
				{
					if (!ViewportClient->IsOrtho())
					{
						DrawVirtualCamera();
					}
				}
			}

			if (!GetWorld()->IsGameWorld())
			{
				if (this->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay)
				{
					TOptional<FIntPoint> NewViewportSize;
#if WITH_EDITOR
					if (bFixedSizeInEditMode)//Edit mode
					{
						NewViewportSize = SizeInEditMode;
					}
					else
#endif
					{
						if (auto ViewportClient = ManagerSubsystem->GetEditorViewportClient())
						{
							auto Viewport = ViewportClient->Viewport;
							if (Viewport == nullptr)
							{
								Viewport = GEditor->GetActiveViewport();
							}
							if (Viewport != nullptr)
							{
								NewViewportSize = Viewport->GetSizeXY();
							}
						}
					}

					if (NewViewportSize.IsSet())
					{
						if (NewViewportSize.GetValue() != ViewportSize)
						{
							ViewportSize = NewViewportSize.GetValue();
							OnViewportParameterChanged();
						}
					}
				}
				if (this->GetRenderMode() == EDreamRenderMode::RenderTarget && IsValid(this->RenderTarget))
				{
					auto prevSize = ViewportSize;
					ViewportSize.X = this->RenderTarget->SizeX;
					ViewportSize.Y = this->RenderTarget->SizeY;
					if (prevSize != ViewportSize)
					{
						OnViewportParameterChanged();
					}
				}
			}
			else
			{
				auto newViewportSize = this->GetViewportSize();
				if (newViewportSize != ViewportSize)
				{
					ViewportSize = newViewportSize;
					OnViewportParameterChanged();
				}
			}
		}
	}
}
void DeprojectViewPointToWorld(const FMatrix& InViewProjectionMatrix, const FVector2D& InViewPoint01, FVector& OutWorldStart, FVector& OutWorldEnd)
{
	FMatrix InvViewProjMatrix = InViewProjectionMatrix.InverseFast();

	const float ScreenSpaceX = (InViewPoint01.X - 0.5f) * 2.0f;
	const float ScreenSpaceY = (InViewPoint01.Y - 0.5f) * 2.0f;

	// The start of the raytrace is defined to be at mousex,mousey,1 in projection space (z=1 is near, z=0 is far - this gives us better precision)
	// To get the direction of the raytrace we need to use any z between the near and the far plane, so let's use (mousex, mousey, 0.5)
	const FVector4 RayStartProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 1.0f, 1.0f);
	const FVector4 RayEndProjectionSpace = FVector4(ScreenSpaceX, ScreenSpaceY, 0, 1.0f);

	// Projection (changing the W coordinate) is not handled by the FMatrix transforms that work with vectors, so multiplications
	// by the projection matrix should use homogeneous coordinates (i.e. FPlane).
	const FVector4 HGRayStartWorldSpace = InvViewProjMatrix.TransformFVector4(RayStartProjectionSpace);
	const FVector4 HGRayEndWorldSpace = InvViewProjMatrix.TransformFVector4(RayEndProjectionSpace);
	FVector RayStartWorldSpace(HGRayStartWorldSpace.X, HGRayStartWorldSpace.Y, HGRayStartWorldSpace.Z);
	FVector RayEndWorldSpace(HGRayEndWorldSpace.X, HGRayEndWorldSpace.Y, HGRayEndWorldSpace.Z);
	// divide vectors by W to undo any projection and get the 3-space coordinate 
	if (HGRayStartWorldSpace.W != 0.0f)
	{
		RayStartWorldSpace /= HGRayStartWorldSpace.W;
	}
	if (HGRayEndWorldSpace.W != 0.0f)
	{
		RayEndWorldSpace /= HGRayEndWorldSpace.W;
	}
	// Finally, store the results in the outputs
	OutWorldStart = RayStartWorldSpace;
	OutWorldEnd = RayEndWorldSpace;
}

void UDreamCanvas::DrawViewportArea()
{
	auto DreamWidget = GetWidget();
	auto RectExtends = FVector(0.1f, DreamWidget->GetWidth(), DreamWidget->GetHeight()) * 0.5f;
	auto RectDrawColor = FColor(128, 128, 128, 128);//gray means normal object
	auto WorldTransform = DreamWidget->GetWorldTransform();

	UDreamUIManagerWorldSubsystem::DrawDebugBox(GetWorld()
		, FVector::Zero(), WorldTransform.ToMatrixWithScale()
		, RectExtends, RectDrawColor, this, FString::Printf(TEXT("%s.DreamCanvas.ViewportArea"), *this->GetWidget()->GetDisplayName())
		, false);
}

void UDreamCanvas::DrawVirtualCamera()
{
	auto ViewLocation = this->GetViewLocation();
	auto ViewRotationMatrix = FInverseRotationMatrix(this->GetViewRotator()) * FMatrix(
		FPlane(0, 0, 1, 0),
		FPlane(1, 0, 0, 0),
		FPlane(0, 1, 0, 0),
		FPlane(0, 0, 0, 1));
	auto ProjectionMatrix = this->GetProjectionMatrix();
	auto ViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;

	FVector leftBottom, rightBottom, leftTop, rightTop;
	FVector leftBottomEnd, rightBottomEnd, leftTopEnd, rightTopEnd;
	auto lineColor = FColor::Green;
	TArray<FVector3f> LinePoints;
	//draw view frustum
	DeprojectViewPointToWorld(ViewProjectionMatrix, FVector2D(0, 0), leftBottom, leftBottomEnd);
	new(LinePoints)FVector3f(leftBottom);
	new(LinePoints)FVector3f(leftBottomEnd);
	DeprojectViewPointToWorld(ViewProjectionMatrix, FVector2D(1, 0), rightBottom, rightBottomEnd);
	new(LinePoints)FVector3f(rightBottom);
	new(LinePoints)FVector3f(rightBottomEnd);
	DeprojectViewPointToWorld(ViewProjectionMatrix, FVector2D(0, 1), leftTop, leftTopEnd);
	new(LinePoints)FVector3f(leftTop);
	new(LinePoints)FVector3f(leftTopEnd);
	DeprojectViewPointToWorld(ViewProjectionMatrix, FVector2D(1, 1), rightTop, rightTopEnd);
	new(LinePoints)FVector3f(rightTop);
	new(LinePoints)FVector3f(rightTopEnd);
	//draw near clip plane
	new(LinePoints)FVector3f(leftBottom);
	new(LinePoints)FVector3f(rightBottom);
	
	new(LinePoints)FVector3f(leftBottom);
	new(LinePoints)FVector3f(leftTop);
	
	new(LinePoints)FVector3f(rightTop);
	new(LinePoints)FVector3f(rightBottom);
	
	new(LinePoints)FVector3f(rightTop);
	new(LinePoints)FVector3f(leftTop);
	//draw far clip plane
	new(LinePoints)FVector3f(leftBottomEnd);
	new(LinePoints)FVector3f(rightBottomEnd);

	new(LinePoints)FVector3f(leftBottomEnd);
	new(LinePoints)FVector3f(leftTopEnd);

	new(LinePoints)FVector3f(rightTopEnd);
	new(LinePoints)FVector3f(rightBottomEnd);

	new(LinePoints)FVector3f(rightTopEnd);
	new(LinePoints)FVector3f(leftTopEnd);

	UDreamUIManagerWorldSubsystem::DrawDebugLine(GetWorld(), FMatrix::Identity
		, LinePoints, lineColor, this, FString::Printf(TEXT("%s.DreamCanvas.VirtualCamera"), *this->GetWidget()->GetDisplayName())
		, false);

	// if (DreamWidget.IsValid())
	// {
	// 	DrawDebugCamera(this->GetWorld(), this->GetViewLocation(), this->GetViewRotator(), FieldOfView, this->GetDreamWidget()->GetComponentScale().X * 3.0f, FColor::Green);
	// }
}
#endif

void UDreamCanvas::SetProjectionType(TEnumAsByte<ECameraProjectionMode::Type> Value)
{
	if (ProjectionType != Value)
	{
		ProjectionType = ProjectionType = Value;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetFieldOfView(float Value)
{
	if (FieldOfView != Value)
	{
		FieldOfView = FieldOfView = Value;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetNearClipPlane(float Value)
{
	if (NearClipPlane != Value)
	{
		NearClipPlane = Value;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetFarClipPlane(float Value)
{
	if (FarClipPlane != Value)
	{
		FarClipPlane = Value;
		OnViewportParameterChanged();
	}
}

void UDreamCanvas::SetScaleMode(EDreamCanvasScaleMode Value)
{
	if (ScaleMode != Value)
	{
		ScaleMode = Value;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetReferenceResolution(FVector2D Value)
{
	if (ReferenceResolution != Value)
	{
		ReferenceResolution = Value;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetMatchFromWidthToHeight(float Value)
{
	if (MatchFromWidthToHeight != Value)
	{
		MatchFromWidthToHeight = Value;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetScreenMatchMode(EDreamCanvasScreenMatchMode Value)
{
	if (ScreenMatchMode != Value)
	{
		ScreenMatchMode = Value;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetCustomScale(UDreamCanvasCustomScale* Value)
{
	if (CustomScale != Value)
	{
		CustomScale = Value;
		CustomScale->Init(this);//need to initialize when first set
		if (ScaleMode == EDreamCanvasScaleMode::Custom)
		{
			OnViewportParameterChanged();
		}
	}
}

bool UDreamCanvas::ConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result)const
{
	if (RootCanvas != this)return false;
	switch (ScaleMode)
	{
	case EDreamCanvasScaleMode::ConstantPixelSize:
		Result = FVector2D(InPosition.X, ViewportSize.Y - InPosition.Y);
		return true;
	case EDreamCanvasScaleMode::ScaleWithScreenSize:
		Result = FVector2D(InPosition.X, ViewportSize.Y - InPosition.Y) / this->CanvasScale;
		return true;
	case EDreamCanvasScaleMode::Custom:
		if (IsValid(CustomScale))
		{
			return CustomScale->ConvertPositionFromViewportToCanvas(InPosition, Result);
		}
	}
	return false;
}
bool UDreamCanvas::ConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result)const
{
	if (RootCanvas != this)return false;
	switch (ScaleMode)
	{
	case EDreamCanvasScaleMode::ConstantPixelSize:
		Result = FVector2D(InPosition.X, ViewportSize.Y - InPosition.Y);
		return true;
	case EDreamCanvasScaleMode::ScaleWithScreenSize:
		Result = FVector2D(InPosition.X * this->CanvasScale, ViewportSize.Y - InPosition.Y * this->CanvasScale);
		return true;
	case EDreamCanvasScaleMode::Custom:
		if (IsValid(CustomScale))
		{
			return CustomScale->ConvertPositionFromCanvasToViewport(InPosition, Result);
		}
	}
	return false;
}
bool UDreamCanvas::ProjectWorldPointOntoCanvasPlane(const FVector& InWorldPoint, FVector& OutWorldOnPlane)const
{
	auto DreamWidget = GetWidget();
	if (!IsValid(DreamWidget) || DreamWidget->GetWidth() <= 0.0f || DreamWidget->GetHeight() <= 0.0f)return false;
	const FVector4 Clip = GetViewProjectionMatrix().TransformFVector4(FVector4(InWorldPoint, 1.0));
	if (Clip.W <= UE_KINDA_SMALL_NUMBER)return false;//at or behind the eye
	// NDC is a direct fraction of the canvas rect: at CalculateDistanceToCamera the rect maps to
	// the NDC square on both axes, which is what makes this a plain remap rather than a fit.
	const FVector2D NDC(Clip.X / Clip.W, Clip.Y / Clip.W);
	const FVector Local(0.0,
		(NDC.X * 0.5 + 0.5 - DreamWidget->GetPivot().X) * DreamWidget->GetWidth(),
		(NDC.Y * 0.5 + 0.5 - DreamWidget->GetPivot().Y) * DreamWidget->GetHeight());
	OutWorldOnPlane = DreamWidget->GetWorldTransform().TransformPosition(Local);
	return true;
}
bool UDreamCanvas::Project3DToScreen(const FVector& Position3D, FVector2D& OutPosition2D)const
{
	if (RootCanvas != this)return false;
	auto viewProjectionMatrix = this->GetViewProjectionMatrix();
	auto result = viewProjectionMatrix.TransformFVector4(FVector4(Position3D, 1.0f));
	if (result.W > 0.0f)
	{
		// the result of this will be x and y coords in -1..1 projection space
		const float RHW = 1.0f / result.W;
		FPlane PosInScreenSpace = FPlane(result.X * RHW, result.Y * RHW, result.Z * RHW, result.W);

		// Move from projection space to normalized 0..1 UI space
		OutPosition2D.X = (PosInScreenSpace.X / 2.f) + 0.5f;
		OutPosition2D.Y = (PosInScreenSpace.Y / 2.f) + 0.5f;
		//Convert to DreamGUI's viewport size
		OutPosition2D *= this->GetViewportSize();
		OutPosition2D /= this->CanvasScale;

		return true;
	}
	return false;
}

bool UDreamCanvas::ProjectWorldToScreenWithPlayerCamera(APlayerController* Player, UCameraComponent* PlayerCamera, const FVector& InPosition, FVector2D& OutPosition2D)
{
	if (Player != nullptr && PlayerCamera != nullptr)
	{
		ULocalPlayer* const LP = Player ? Player->GetLocalPlayer() : nullptr;
		if (LP && LP->ViewportClient)
		{
			FSceneViewProjectionData ProjectionData;
			LP->GetProjectionData(LP->ViewportClient->Viewport, /*out*/ ProjectionData);

			auto ViewLocation = PlayerCamera->GetComponentLocation();
			auto ViewRotationMatrix = FInverseRotationMatrix(PlayerCamera->GetComponentRotation()) * FMatrix(
				FPlane(0, 0, 1, 0),
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, 0, 1));

			auto ViewRect = ProjectionData.GetConstrainedViewRect();
			auto ViewportSize = ViewRect.Size();
#if 0//not sure what is wrong but this calculation can't get correct result
			auto FovInRadians = PlayerCamera->FieldOfView * UE_PI / 360.0f;//we need half fov so 360 instead of 180
			FMatrix ProjectionMatrix;
			UDreamCanvas::BuildProjectionMatrix(ViewportSize, PlayerCamera->ProjectionMode
				, FovInRadians, 1000000, 0.01f, ProjectionMatrix);
			auto ViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;
#else
			ProjectionData.ViewOrigin = ViewLocation;
			ProjectionData.ViewRotationMatrix = ViewRotationMatrix;
			auto ViewProjectionMatrix = ProjectionData.ComputeViewProjectionMatrix();
#endif

			auto ScreenPos = ViewProjectionMatrix.TransformFVector4(FVector4(InPosition, 1.0f));
			if (ScreenPos.W > 0.0f)
			{
				// the result of this will be x and y coords in -1..1 projection space
				const float RHW = 1.0f / ScreenPos.W;
				FPlane PosInScreenSpace = FPlane(ScreenPos.X * RHW, ScreenPos.Y * RHW, ScreenPos.Z * RHW, ScreenPos.W);

				// Move from projection space to normalized 0..1 UI space
				const float NormalizedX = (PosInScreenSpace.X * 0.5f) + 0.5f;
				const float NormalizedY = 1 - (PosInScreenSpace.Y * 0.5f) - 0.5f;

				FVector2D RayStartViewRectSpace(
					NormalizedX * (float)ViewportSize.X,
					NormalizedY * (float)ViewportSize.Y
				);
				
				OutPosition2D = FVector2D(RayStartViewRectSpace.X, RayStartViewRectSpace.Y) + FVector2D(static_cast<float>(ViewRect.Min.X), static_cast<float>(ViewRect.Min.Y));
				return true;
			}
		}
	}
	return false;
}

bool UDreamCanvas::BuildViewProjectionMatrixForPlayerCamera(APlayerController* Player, UCameraComponent* PlayerCamera, FMatrix& OutViewProjectionMatrix)
{
	if (Player != nullptr && PlayerCamera != nullptr)
	{
		ULocalPlayer* const LP = Player ? Player->GetLocalPlayer() : nullptr;
		if (LP && LP->ViewportClient)
		{
			FSceneViewProjectionData ProjectionData;
			LP->GetProjectionData(LP->ViewportClient->Viewport, /*out*/ ProjectionData);

			auto ViewLocation = PlayerCamera->GetComponentLocation();
			auto ViewRotationMatrix = FInverseRotationMatrix(PlayerCamera->GetComponentRotation()) * FMatrix(
				FPlane(0, 0, 1, 0),
				FPlane(1, 0, 0, 0),
				FPlane(0, 1, 0, 0),
				FPlane(0, 0, 0, 1));

			auto ViewRect = ProjectionData.GetConstrainedViewRect();
			auto ViewportSize = ViewRect.Size();
#if 0//not sure what is wrong but this calculation can't get correct result
			auto FovInRadians = PlayerCamera->FieldOfView * UE_PI / 360.0f;//we need half fov so 360 instead of 180
			FMatrix ProjectionMatrix;
			UDreamCanvas::BuildProjectionMatrix(ViewportSize, PlayerCamera->ProjectionMode
				, FovInRadians, 1000000, 0.01f, ProjectionMatrix);
			OutViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;
#else
			ProjectionData.ViewOrigin = ViewLocation;
			ProjectionData.ViewRotationMatrix = ViewRotationMatrix;
			OutViewProjectionMatrix = ProjectionData.ComputeViewProjectionMatrix();
#endif
		}
	}
	return false;
}

bool UDreamCanvas::ProjectWorldToScreenWithViewProjectionMatrix(const FMatrix& InViewProjectionMatrix, const FVector2D& InViewportSize, const FVector& InPosition, FVector2D& OutPosition2D)
{
	auto ScreenPos = InViewProjectionMatrix.TransformFVector4(FVector4(InPosition, 1.0f));
	if (ScreenPos.W > 0.0f)
	{
		// the result of this will be x and y coords in -1..1 projection space
		const float RHW = 1.0f / ScreenPos.W;
		FPlane PosInScreenSpace = FPlane(ScreenPos.X * RHW, ScreenPos.Y * RHW, ScreenPos.Z * RHW, ScreenPos.W);

		// Move from projection space to normalized 0..1 UI space
		const float NormalizedX = (PosInScreenSpace.X / 2.f) + 0.5f;
		const float NormalizedY = 1.f - (PosInScreenSpace.Y / 2.f) - 0.5f;

		OutPosition2D.X = (NormalizedX * (float)InViewportSize.X);
		OutPosition2D.Y = (NormalizedY * (float)InViewportSize.Y);

		OutPosition2D = FVector2D(OutPosition2D.X, InViewportSize.Y - OutPosition2D.Y);
		return true;
	}
	return false;
}

#pragma endregion


