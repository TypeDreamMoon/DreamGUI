// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexCanvas.h"
#include "LGUI.h"
#include "Core/LexUIGeometry.h"
#include "Utils/LexUIUtils.h"
#include "Core/LexUISettings.h"
#include "Core/LexUIManager.h"
#include "PrefabSystem/LexUIPrefabManager.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "Core/LexUIMesh/LexUIMeshComponent.h"
#include "Core/LexUIDrawCall.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexVisualPostProcess.h"
#include "Core/Components/LexVisualDirectMesh.h"
#include "Core/Components/LexWidget.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
#include "SceneViewExtension.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Math/TransformCalculus2D.h"
#include "TextureResource.h"
#include "Core/LexUIClipData.h"
#include "Core/LexUIDataAsTexture.h"



#define LOCTEXT_NAMESPACE "LexCanvas"

ULexCanvas::ULexCanvas()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	bHasAddToLexScreenSpaceRenderer = false;
	bHasSetInitialStateForLexWorldSpaceRenderer = false;
	bOverrideViewLocation = false;
	bOverrideViewRotation = false;
	bOverrideProjectionMatrix = false;
	bOverrideFovAngle = false;
	bPrevIsVisible = true;
	bNeedToVerifyMaterials = true;
	bRootCanvasNeedToUpdateChildrenCanvasBounds = false;
	bUIMeshNeedToSetInitialParameters = true;

	bCanTickUpdate = true;
	bShouldRebuildDrawCall = true;
	bShouldSortVisualOrder = true;
	bAnythingChangedForRenderTarget = true;
	bPrevAnythingChangedForRenderTarget = true;

	bIsViewProjectionMatrixDirty = true;

	DefaultMeshType = ULexUIMeshComponent::StaticClass();
	DefaultMaterial = LoadObject<UMaterialInterface>(NULL, TEXT("/LGUI/Materials/LexUI_ImageAndFont"));
}

void ULexCanvas::BeginPlay()
{
	Super::BeginPlay();
	if (!ULexUIPrefabWorldSubsystem::GetInstance(this->GetWorld())->IsPrefabSystemProcessingActor(this->GetOwner()))
	{
		Awake_Implementation();
	}
}
void ULexCanvas::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ULexCanvas::Awake_Implementation()
{
	CheckRootCanvas();
	CurrentRenderMode = this->GetActualRenderMode();
	if (CheckLexWidget())
	{
		bPrevIsVisible = LexWidget->GetWidgetActiveInHierarchy();
	}
	else
	{
		bPrevIsVisible = false;
	}
	MarkCanvasUpdate(true, true, true, true);

	bNeedToSortRenderPriority = true;

	if (this->IsRootCanvas())
	{
		if (this->GetRenderMode() == ELexRenderMode::ScreenSpaceOverlay
				|| this->GetRenderMode() == ELexRenderMode::RenderTarget
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

void ULexCanvas::EditorAwake_Implementation()
{
	
}

void ULexCanvas::TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> ULexCanvas::GetRenderTargetViewExtension()
{
	if (!RenderTargetViewExtension.IsValid())
	{
		RenderTargetViewExtension = FSceneViewExtensions::NewExtension<FLexUIRenderer>(GetWorld(), ELexUIRendererType::RenderTarget);
	}
	return RenderTargetViewExtension;
}

void ULexCanvas::UpdateRootCanvas()
{
	CheckRootCanvas();
	if (this == RootCanvas)
	{
		bool bIsRenderTargetRenderer = false;
		if (RenderModeIsLexRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (!GetWorld()->IsGameWorld())//edit mode
			{
				if (ActualRenderMode == ELexRenderMode::ScreenSpaceOverlay)
					ActualRenderMode = ELexRenderMode::WorldSpace_LexUI;
			}
#endif
			switch (ActualRenderMode)
			{
			case ELexRenderMode::ScreenSpaceOverlay:
			{
				if (!bHasAddToLexScreenSpaceRenderer)
				{
					auto ViewExtension = ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true);

					if (ViewExtension.IsValid())//only root canvas can add screen space UI to LGUIRenderer
					{
						ViewExtension->SetScreenSpaceRootCanvas(this);
						bHasAddToLexScreenSpaceRenderer = true;
					}
				}
			}
			break;
			case ELexRenderMode::RenderTarget:
			{
				if (!bHasAddToLexScreenSpaceRenderer)
				{
					GetRenderTargetViewExtension()->SetScreenSpaceRootCanvas(this);
					bHasAddToLexScreenSpaceRenderer = true;
				}
				bIsRenderTargetRenderer = true;
			}
			break;
			case ELexRenderMode::WorldSpace_LexUI:
			{
				if (!bHasSetInitialStateForLexWorldSpaceRenderer)
				{
					auto ViewExtension = ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true);

					if (ViewExtension.IsValid())//only root canvas can add screen space UI to LGUIRenderer
					{
						//put initial code here
						bHasSetInitialStateForLexWorldSpaceRenderer = true;
					}
				}
			}
			break;
			}
		}
		
		if (CheckLexWidget())
		{
			UpdateRootCanvasDrawCall();
			MarkFinishUpdateRootCanvasDrawCall();
		}

		if (bIsRenderTargetRenderer)
		{
			bool bCanUpdateRenderTarget = false;
			switch (RenderTargetUpdateMode)
			{
			default:
			case ELexCanvasRenderTargetUpdateMode::Automatic:
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
			case ELexCanvasRenderTargetUpdateMode::Always:
				bCanUpdateRenderTarget = true;
				break;
			case ELexCanvasRenderTargetUpdateMode::WhenRequest:
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
}

void ULexCanvas::UpdateRenderTarget(bool CallEvent)
{
	FIntPoint DesiredRenderTargetSize(LexWidget->GetWidth() * RenderTargetResolutionScale, LexWidget->GetHeight() * RenderTargetResolutionScale);
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
		case ELexCanvasRenderTargetSizeMode::None:
		case ELexCanvasRenderTargetSizeMode::CanvasFitToRenderTarget:
			if (RenderTarget != nullptr)
			{
				DesiredRenderTargetSize.X = RenderTarget->SizeX;
				DesiredRenderTargetSize.Y = RenderTarget->SizeY;
			}
			break;
		case ELexCanvasRenderTargetSizeMode::RenderTargetFitToCanvas:
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

void ULexCanvas::EnsureDrawCallObjectReference()
{
	for (const auto& DrawCallItem : UIDrawCallList)
	{
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchMesh:
		{
			for (int i = 0; i < DrawCallItem->BatchMeshVisualObjectList.Num(); i++)
			{
				if (!DrawCallItem->BatchMeshVisualObjectList[i].IsValid())
				{
					DrawCallItem->BatchMeshVisualObjectList.RemoveAt(i);
					i--;
				}
			}
		}
		break;
		}
	}
}

void ULexCanvas::OnRegister()
{
	Super::OnRegister();
	if (CheckLexWidget())
	{
		ULexUIManagerWorldSubsystem::AddCanvas(this, CurrentRenderMode);
		//tell UIItem
		LexWidget->RegisterRenderCanvas(this);
		LexWidget->GetAttachmentChangedEvent().AddUObject(this, &ULexCanvas::OnUIHierarchyAttachmentChanged);
		LexWidget->GetWidgetActiveChangedEvent().AddUObject(this, &ULexCanvas::OnWidgetActiveChanged);

		OnUIHierarchyAttachmentChanged();
	}

	if (!IsValid(ClipDataAsTexture))
	{
		ClipDataAsTexture = NewObject<ULexUIDataAsTexture>(this, ULexUIDataAsTexture::StaticClass(), NAME_None, RF_Transient);
		ClipDataAsTexture->Init(FLexUIClipData::BlockSizeInBytes, ELexUIDataAsTexturePixelFormat::R32G32B32A32, 512);
		ClipDataAsTexture->OnDataTextureChange.AddUObject(this, &ULexCanvas::OnClipDataTextureChanged);
		ClipDataAsTexture->RegisterBuffer();//register a zero position as a placeholder for not clipping type.
	}

	RegisterCanvasScaler();
}
void ULexCanvas::OnUnregister()
{
	Super::OnUnregister();
	ULexUIManagerWorldSubsystem::RemoveCanvas(this, CurrentRenderMode);

	ClipDataList.Empty();
	
	{
		//these three functions is from OnUIHierarchyChanged()
		RemoveFromViewExtension(true);
		CheckRootCanvas(true);
		CheckRenderMode(true);
	}

	//tell UIItem
	if (LexWidget.IsValid())
	{
		LexWidget->UnregisterRenderCanvas();
		LexWidget->GetAttachmentChangedEvent().RemoveAll(this);
		LexWidget->GetWidgetActiveChangedEvent().RemoveAll(this);
	}

	UnregisterCanvasScaler();
}
void ULexCanvas::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	Super::OnComponentDestroyed(bDestroyingHierarchy);
	if (UIMesh.IsValid())
	{
		UIMesh->DestroyComponent();
		UIMesh = nullptr;
	}
}

void ULexCanvas::ClearDrawCall()
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

void ULexCanvas::RemoveFromViewExtension(bool PropogateToChildrenCanvas)
{
	if (bHasAddToLexScreenSpaceRenderer)
	{
		bHasAddToLexScreenSpaceRenderer = false;
		if (RenderTargetViewExtension.IsValid())//could be RenderTarget mode
		{
			RenderTargetViewExtension->ClearScreenSpaceRootCanvas();
		}
		else//if not RenderTarget mode, then should be ScreenSpaceOverlay
		{
			auto ViewExtension = ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
			if (ViewExtension.IsValid())
			{
				ViewExtension->ClearScreenSpaceRootCanvas();
			}
		}
	}
	if (bHasSetInitialStateForLexWorldSpaceRenderer)
	{
		bHasSetInitialStateForLexWorldSpaceRenderer = false;
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

bool ULexCanvas::CheckRootCanvas(bool forceRecheck)const
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
		ULexCanvas* ResultCanvas = nullptr;
		auto ParentActor = Actor;
		while (ParentActor != nullptr
			&& Cast<ULexWidget>(ParentActor->GetRootComponent()) != nullptr//root must be UI component
			)
		{
			auto FoundCanvas = ParentActor->FindComponentByClass<ULexCanvas>();
			if (FoundCanvas)
			{
				ResultCanvas = FoundCanvas;
				if (FoundCanvas->bForceRenderToTarget)
				{
					return ResultCanvas;
				}
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

void ULexCanvas::SetParentCanvas(ULexCanvas* InParentCanvas)
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
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->MarkCanvasUpdate(false, false, true, true);
		}
		ParentCanvas = InParentCanvas;
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->ChildrenCanvasArray.AddUnique(this);
			ParentCanvas->MarkCanvasUpdate(false, false, true, true);
		}
	}
}

bool ULexCanvas::CheckLexWidget()const
{
	if (LexWidget.IsValid())return true;
	if (this->GetWorld() == nullptr)return false;
	LexWidget = Cast<ULexWidget>(GetOwner()->GetRootComponent());
	if (!LexWidget.IsValid())
	{
		if (this->IsRegistered())
		{
			UE_LOG(LGUI, Warning, TEXT("[%s].%d LexCanvas component should only attach to a actor which have UIItem as RootComponent! %s")
				, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
		}
		return false;
	}
	else
	{
		return true;
	}
}
void ULexCanvas::CheckRenderMode(bool PropogateToChildrenCanvas)
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
			CurrentRenderMode = ELexRenderMode::None;
		}
	}
	else
	{
		CurrentRenderMode = ELexRenderMode::None;
	}
	//if render space changed, we need to change recreate all render data
	if (CurrentRenderMode != OldRenderMode)
	{
		if (CheckLexWidget())
		{
			LexWidget->MarkRenderModeChangeRecursive(this, OldRenderMode, CurrentRenderMode);
		}
		//clear drawcall, delete mesh, because UE/LGUI render's mesh data not compatible
		this->ClearDrawCall();

		ULexUIManagerWorldSubsystem::CanvasRenderModeChange(this, OldRenderMode, CurrentRenderMode);
		OnRenderModeChanged.Broadcast(this, OldRenderMode, CurrentRenderMode);
	}

	if (PropogateToChildrenCanvas)
	{
		for (const auto& ChildCanvas : ChildrenCanvasArray)
		{
			if (!ChildCanvas.IsValid())continue;
			if (ChildCanvas->bForceRenderToTarget)continue;
			ChildCanvas->CheckRenderMode(PropogateToChildrenCanvas);
		}
	}
}
void ULexCanvas::OnUIHierarchyAttachmentChanged()
{
	this->bCanTickUpdate = true;
	RemoveFromViewExtension(true);
	CheckRootCanvas(true);
	CheckRenderMode(true);

	ULexCanvas* NewParentCanvas = nullptr;
	if (this->IsRegistered())
	{
		NewParentCanvas = ULexWidget::GetComponentInParentUI<ULexCanvas>(this->GetOwner()->GetAttachParentActor(), true);
	}
	SetParentCanvas(NewParentCanvas);
}

void ULexCanvas::OnWidgetActiveChanged(bool WidgetActive)
{
	if (LexWidget->GetWidgetActiveInHierarchy())
	{
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->MarkCanvasUpdate(false, false, true//why make this to true? because we need to sort UIRenderableList, and set bShouldSortRenderableOrder to true can do it
				, true);

		}
	}
	else
	{
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->MarkCanvasUpdate(false, false, false, true);
		}
	}
}

bool ULexCanvas::IsRenderToScreenSpace()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == ELexRenderMode::ScreenSpaceOverlay;
	}
	return false;
}
bool ULexCanvas::IsRenderToRenderTarget()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == ELexRenderMode::RenderTarget;
	}
	return false;
}
bool ULexCanvas::IsRenderToWorldSpace()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == ELexRenderMode::WorldSpace
			|| RootCanvas->RenderMode == ELexRenderMode::WorldSpace_LexUI
			;
	}
	return false;
}

bool ULexCanvas::IsRenderByLexUIRendererOrUERenderer()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == ELexRenderMode::ScreenSpaceOverlay
			|| RootCanvas->RenderMode == ELexRenderMode::RenderTarget
			|| RootCanvas->RenderMode == ELexRenderMode::WorldSpace_LexUI
			;
	}
	return false;
}

void ULexCanvas::MarkCanvasUpdate(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall)
{
	this->bCanTickUpdate = true;
	if (bMaterialOrTextureChanged || bTransformOrVertexPositionChanged || bHierarchyOrderChanged || bForceRebuildDrawCall)
	{
		this->bShouldRebuildDrawCall = true;
	}
	if (bHierarchyOrderChanged)
	{
		this->bShouldSortVisualOrder = true;
	}
}
void ULexCanvas::MarkCanvasUpdateRecursive(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall)
{
	this->MarkCanvasUpdate(bMaterialOrTextureChanged, bTransformOrVertexPositionChanged, bHierarchyOrderChanged, bForceRebuildDrawCall);
	for (auto& ChildCanvas : this->ChildrenCanvasArray)
	{
		if (!ChildCanvas.IsValid())continue;
		if (ChildCanvas->bForceRenderToTarget)continue;
		ChildCanvas->MarkCanvasUpdateRecursive(bMaterialOrTextureChanged, bTransformOrVertexPositionChanged, bHierarchyOrderChanged, bForceRebuildDrawCall);
	}
}

#if WITH_EDITOR
bool ULexCanvas::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		auto MemberName = InProperty->GetFName();
		bool bIsRootCanvas = this->IsRootCanvas()
		|| this->GetWorld() == nullptr;//world is null maybe it is blueprint editor
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, ProjectionType))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, FieldOfView))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, NearClipPlane))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, FarClipPlane))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, ScaleMode))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, ReferenceResolution))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, MatchFromWidthToHeight))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, ScreenMatchMode))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, bFixedSizeInEditMode))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, SizeInEditMode))
		{
			return bIsRootCanvas;
		}
		if (MemberName == GET_MEMBER_NAME_CHECKED(ULexCanvas, RenderMode))
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
void ULexCanvas::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (CheckLexWidget())
	{
		LexWidget->MarkAllDirtyRecursive();
	}
	if (CheckRootCanvas())
	{
		RootCanvas->MarkCanvasUpdate(true, true, true);
		RootCanvas->bRequestUpdateForRenderTarget = true;
	}

	auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexCanvas, bForceRenderToTarget))
	{
		if (bForceRenderToTarget)
		{
			RenderMode = ELexRenderMode::RenderTarget;
			OnRenderTargetChanged.Broadcast(RenderTarget);
		}
		else
		{
			OnRenderTargetChanged.Broadcast(nullptr);
		}
	}

	OnViewportParameterChanged();
}
void ULexCanvas::PostLoad()
{
	Super::PostLoad();
}
void ULexCanvas::PostEditUndo()
{
	Super::PostEditUndo();

	ULexUIManagerWorldSubsystem::RefreshAllUI(this->GetWorld());
}
void ULexCanvas::EnsureDataForRebuild()
{
	struct LOCAL
	{
		static void RecheckRootCanvasRecursive(ULexCanvas* Target)
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
	ULexUIManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]() {
		if (WeakThis.IsValid())
		{
			LOCAL::RecheckRootCanvasRecursive(WeakThis.Get());
		}
		}, 0);
}
#endif

ULexCanvas* ULexCanvas::GetRootCanvas() const
{ 
	CheckRootCanvas(); 
	return RootCanvas.Get(); 
}
bool ULexCanvas::IsRootCanvas()const
{
	return GetRootCanvas() == this;
}

void ULexCanvas::MarkVisualWillChange(ULexVisual* InOldVisual)
{
	auto DrawCall = InOldVisual->DrawCall;
	if (DrawCall.IsValid())
	{
		switch (InOldVisual->GetVisualType())
		{
		case ELexVisualType::BatchMesh:
			{
				auto VisualMesh = (ULexVisualBatchMesh*)InOldVisual;
				auto index = DrawCall->BatchMeshVisualObjectList.IndexOfByKey(VisualMesh);
				if (index != INDEX_NONE)
				{
					DrawCall->BatchMeshVisualObjectList.RemoveAt(index);
					DrawCall->bNeedToUpdateVertex = true;
				}
			}
			break;
		case ELexVisualType::DirectMesh:
			{
				if (DrawCall->DirectMeshVisualObject.IsValid())
				{
					DrawCall->DirectMeshVisualObject->ClearMeshData();
				}
			}
			break;
		case ELexVisualType::None:
		case ELexVisualType::PostProcess:break;
		}
		InOldVisual->DrawCall = nullptr;
	}
	MarkCanvasUpdate(false, false, false);
}

void ULexCanvas::RegisterVisual(ULexWidget* InWidget, int& OutWidgetPropertyDataStartPosition)
{
	VisualWidgetList.AddUnique(InWidget);
	
	if (!IsValid(WidgetPropertyDataAsTexture))
	{
		WidgetPropertyDataAsTexture = NewObject<ULexUIDataAsTexture>(this, ULexUIDataAsTexture::StaticClass(), NAME_None, RF_Transient);
		WidgetPropertyDataAsTexture->Init(ULexVisual::WidgetPropertyDataLength, ELexUIDataAsTexturePixelFormat::R32G32B32A32, 4);
		WidgetPropertyDataAsTexture->OnDataTextureChange.AddUObject(this, &ULexCanvas::OnWidgetPropertyDataTextureChanged);
	}
	OutWidgetPropertyDataStartPosition = WidgetPropertyDataAsTexture->RegisterBuffer();
}

void ULexCanvas::UnregisterVisual(ULexWidget* InWidget, int& InOutWidgetPropertyDataStartPosition)
{
	VisualWidgetList.Remove(InWidget);
	if (InOutWidgetPropertyDataStartPosition > INDEX_NONE)
	{
		if (IsValid(WidgetPropertyDataAsTexture))
		{
			WidgetPropertyDataAsTexture->UnregisterBuffer(InOutWidgetPropertyDataStartPosition);
		}
		InOutWidgetPropertyDataStartPosition = INDEX_NONE;
	}
}

void ULexCanvas::AddLexWidget(ULexWidget* InWidget)
{
	bNeedToGenerateWidgetList = true;
	MarkCanvasUpdate(false, false, true);
}
void ULexCanvas::RemoveLexWidget(ULexWidget* InWidget)
{
	bNeedToGenerateWidgetList = true;
	MarkCanvasUpdate(false, false, true);
}

bool ULexCanvas::Is2DUITransform(const FTransform& Transform)
{
#if WITH_EDITOR
	float threshold = ULexUISettings::GetAutoBatchThreshold();
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

DECLARE_CYCLE_STAT(TEXT("Canvas BatchDrawCall"), STAT_BatchDrawCall, STATGROUP_LGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas BatchDrawCall/OverlapTest"), STAT_OverlapTest, STATGROUP_LGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas BatchDrawCall/SortBatchMeshInDrawCall"), STAT_SortBatchMeshInDrawCall, STATGROUP_LGUI);
void ULexCanvas::BatchDrawCall_Implement(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop, TArray<TSharedPtr<FLexUIDrawCall>>& InUIDrawCallList, TArray<TSharedPtr<FLexUIDrawCall>>& InCacheUIDrawCallList, bool& OutNeedToSortRenderPriority)
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
		SCOPE_CYCLE_COUNTER(STAT_OverlapTest);
		switch (OtherDrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchMesh:
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
				auto OtherUIGeo = OtherDrawCallItem->PostProcessVisualObject->GetGeometry();
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
	auto CanFitInDrawCall = [&](const ULexVisualBatchMesh* InUIItem, bool InIs2DUI, int32 InUIItemVerticesCount, int32& OutDrawCallIndexToFitin)
	{
		const auto LastDrawCallIndex = InUIDrawCallList.Num() - 1;
		if (LastDrawCallIndex < 0)
		{
			return false;
		}

		if (!InIs2DUI)
		{
			//3d UI can only batch into last draw-call
			const auto& LastDrawCall = InUIDrawCallList[LastDrawCallIndex];
			if (LastDrawCall->CanConsumeUIGeometryForBatchMesh(InUIItem->GetGeometry(), InUIItemVerticesCount))
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
			const auto& OtherDrawCall = InUIDrawCallList[i];
			if (!OtherDrawCall->bIs2DSpace)//draw-call is 3d, can't batch
			{
				return false;
			}

			auto UIGeo = InUIItem->GetGeometry();
			if (!OtherDrawCall->CanConsumeUIGeometryForBatchMesh(UIGeo, InUIItemVerticesCount))//can't fit in this draw-call, should check overlap
			{
				if (OverlapWithOtherDrawCall(UIGeo, OtherDrawCall))//overlap with other draw-call, can't batch
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
			if (OtherDrawCall->BatchMeshTreeNode->Overlap(LexUIQuadTree::Rectangle(UIGeo->BoundsMin2DInCanvasSpace, UIGeo->BoundsMax2DInCanvasSpace)))
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

	auto PushSingleDrawCall = [&](ULexVisual* InUIItem, bool InSearchInCacheList, const FLexUIGeometry* InItemGeo, ELexUIDrawCallType InDrawCallType, bool InIs2DSpace = true) {
		//if this UIItem exist in InCacheUIDrawCallList, then grab the entire draw-call item (may include other UIItem in RenderObjectList). No need to worry other UIItem, because they could be cleared in further operation, or exist in the same draw-call
		int32 FoundDrawCallIndex = INDEX_NONE;
		if (InSearchInCacheList)
		{
			FoundDrawCallIndex = InCacheUIDrawCallList.IndexOfByPredicate([=](const TSharedPtr<FLexUIDrawCall>& DrawCallItem) {
				if (DrawCallItem->Type == InDrawCallType)
				{
					switch (InDrawCallType)
					{
					case ELexUIDrawCallType::BatchMesh:
					{
						if (DrawCallItem->BatchMeshVisualObjectList.Contains(InUIItem))
						{
							return true;
						}
					}
					break;
					case ELexUIDrawCallType::PostProcess:
					{
						if (DrawCallItem->PostProcessVisualObject == InUIItem)
						{
							return true;
						}
					}
					break;
					case ELexUIDrawCallType::DirectMesh:
					{
						if (DrawCallItem->DirectMeshVisualObject == InUIItem)
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
			case ELexUIDrawCallType::BatchMesh:
				{
					if (InItemGeo->bIsFont)
					{
						DrawCallItem->FontTexture = InItemGeo->Texture;
					}
					else
					{
						DrawCallItem->Texture = InItemGeo->Texture;
					}
					DrawCallItem->Material = InItemGeo->Material.Get();
					DrawCallItem->BatchMeshVisualObjectList.Reset();
					DrawCallItem->BatchMeshVisualObjectList.Add((ULexVisualBatchMesh*)InUIItem);
					DrawCallItem->BatchMeshTreeNode = MakeUnique<LexUIQuadTree::Node>(CanvasRect);
					DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(InItemGeo->BoundsMin2DInCanvasSpace, InItemGeo->BoundsMax2DInCanvasSpace));
					DrawCallItem->VerticesCount = InItemGeo->Vertices.Num();
					DrawCallItem->IndicesCount = InItemGeo->Triangles.Num();
				}
				break;
			case ELexUIDrawCallType::PostProcess:
				{
					DrawCallItem->PostProcessVisualObject = (ULexVisualPostProcess*)InUIItem;
				}
				break;
			case ELexUIDrawCallType::DirectMesh:
				{
					DrawCallItem->DirectMeshVisualObject = (ULexVisualDirectMesh*)InUIItem;
				}
				break;
			}
		}
		else
		{
			switch (InDrawCallType)
			{
			default:
			case ELexUIDrawCallType::BatchMesh:
				{
					DrawCallItem = MakeShared<FLexUIDrawCall>(CanvasRect);
					DrawCallItem->bNeedToUpdateVertex = true;
					if (InItemGeo->bIsFont)
					{
						DrawCallItem->FontTexture = InItemGeo->Texture;
					}
					else
					{
						DrawCallItem->Texture = InItemGeo->Texture;
					}
					DrawCallItem->Material = InItemGeo->Material.Get();
					DrawCallItem->BatchMeshVisualObjectList.Add((ULexVisualBatchMesh*)InUIItem);
					DrawCallItem->VerticesCount = InItemGeo->Vertices.Num();
					DrawCallItem->IndicesCount = InItemGeo->Triangles.Num();
					DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(InItemGeo->BoundsMin2DInCanvasSpace, InItemGeo->BoundsMax2DInCanvasSpace));
					DrawCallItem->DrawCallMesh = UIMesh;
				}
				break;
			case ELexUIDrawCallType::PostProcess:
				{
					DrawCallItem = MakeShared<FLexUIDrawCall>(InDrawCallType);
					DrawCallItem->PostProcessVisualObject = (ULexVisualPostProcess*)InUIItem;
					DrawCallItem->DrawCallMesh = UIMesh;
				}
				break;
			case ELexUIDrawCallType::DirectMesh:
				{
					DrawCallItem = MakeShared<FLexUIDrawCall>(InDrawCallType);
					DrawCallItem->DirectMeshVisualObject = (ULexVisualDirectMesh*)InUIItem;
					DrawCallItem->DrawCallMesh = UIMesh;
				}
				break;
			}
		}
		DrawCallItem->bIs2DSpace = InIs2DSpace;

		if (InDrawCallType == ELexUIDrawCallType::BatchMesh
			|| InDrawCallType == ELexUIDrawCallType::PostProcess
			|| InDrawCallType == ELexUIDrawCallType::DirectMesh)
		{
			((ULexVisual*)InUIItem)->DrawCall = DrawCallItem;
		}
		InUIDrawCallList.Add(DrawCallItem);

		if (FoundDrawCallIndex != 0)//if not find draw-call or found draw-call not at head of array, means draw-call list's order is changed compare to cache list, then we need to sort render order
		{
			OutNeedToSortRenderPriority = true;
		}
		//OutNeedToSortRenderPriority = true;//@todo: this line could make it sort every time, which is not good performance
	};
	auto ClearObjectFromDrawCall = [&](const TSharedPtr<FLexUIDrawCall>& InDrawCallItem, ULexVisual* InVisual) {
		if (InDrawCallItem->DrawCallRenderSection.IsValid())
		{
			InDrawCallItem->DrawCallMesh->DeleteRenderSection(InDrawCallItem->DrawCallRenderSection.Pin());
			InDrawCallItem->DrawCallRenderSection = nullptr;
		}

		switch (InVisual->GetVisualType())
		{
		default:
		case ELexVisualType::BatchMesh:
			{
				InDrawCallItem->bNeedToUpdateVertex = true;
				InDrawCallItem->bMaterialNeedToReassign = true;
				int index = InDrawCallItem->BatchMeshVisualObjectList.IndexOfByKey(InVisual);
				if (index != INDEX_NONE)
				{
					InDrawCallItem->BatchMeshVisualObjectList.RemoveAt(index);
				}
			}
			break;
		case ELexVisualType::PostProcess:
			check(InDrawCallItem->PostProcessVisualObject == InVisual);
			InDrawCallItem->PostProcessVisualObject = nullptr;
			break;
		case ELexVisualType::DirectMesh:
			check(InDrawCallItem->DirectMeshVisualObject == InVisual);
			InDrawCallItem->DirectMeshVisualObject = nullptr;
			break;
		}
		InVisual->DrawCall = nullptr;
	};
	auto ClearChildCanvasFromDrawCall = [&](const TSharedPtr<FLexUIDrawCall>& InDrawCallItem, ULexCanvas* InChildCanvas) {
		if (InDrawCallItem->DrawCallRenderSection.IsValid())
		{
			InDrawCallItem->DrawCallMesh->DeleteRenderSection(InDrawCallItem->DrawCallRenderSection.Pin());
			InDrawCallItem->DrawCallRenderSection = nullptr;
		}

		InChildCanvas->DrawCallAsChildCanvas = nullptr;
	};

	//for sorted ui items, iterate from head to tail, compare draw-call from tail to head
	for (int i = 0; i < WidgetList.Num(); i++)
	{
		auto& Item = WidgetList[i];
		
		if (Item->IsCanvasWidget() && Item->GetRenderCanvas() != this)//is child canvas
		{
			auto ChildCanvas = Item->GetRenderCanvas();
			if (ChildCanvas == nullptr)continue;//normally this won't be nullptr, but when redo in editor this breaks
			if (ChildCanvas->bForceRenderToTarget)continue;//skip this type
			if (ChildCanvas->GetOverrideSorting())continue;//override sorting means render by itself, then no need to use it as child-canvas

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
		else
		{
			auto Visual = Item->GetVisual();
			if (!Visual)continue;
			if (!Item->GetWidgetActiveInHierarchy())//if not visible, need to remove the draw-call from draw-call list
			{
				if (Visual->DrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
				{
					ClearObjectFromDrawCall(Visual->DrawCall, Visual);
				}
				continue;
			}
			switch (Visual->GetVisualType())
			{
			default:
			case ELexVisualType::BatchMesh:
			{
				auto LexVisualBatchMeshItem = (ULexVisualBatchMesh*)Visual;
				auto ItemGeo = LexVisualBatchMeshItem->GetGeometry();
				if (ItemGeo == nullptr)continue;
				if (ItemGeo->Vertices.Num() == 0)continue;
				if (ItemGeo->Vertices.Num() > LEXUI_MAX_VERTEX_COUNT)continue;

				bool is2DUIItem = Is2DUITransform(ItemGeo->TransformRelativeToCanvas);
				int DrawCallIndexToFitin;
				if (LexVisualBatchMeshItem->SupportDrawCallBatching() && CanFitInDrawCall(LexVisualBatchMeshItem, is2DUIItem, ItemGeo->Vertices.Num(), DrawCallIndexToFitin))
				{
					auto DrawCallItem = InUIDrawCallList[DrawCallIndexToFitin];
					DrawCallItem->bIs2DSpace = DrawCallItem->bIs2DSpace && is2DUIItem;
					if (ItemGeo->bIsFont)
					{
						if (DrawCallItem->FontTexture != ItemGeo->Texture)
						{
							DrawCallItem->FontTexture = ItemGeo->Texture;
							DrawCallItem->bTextureChanged = true;
						}
					}
					else
					{
						if (DrawCallItem->Texture != ItemGeo->Texture)
						{
							DrawCallItem->Texture = ItemGeo->Texture;
							DrawCallItem->bTextureChanged = true;
						}
					}
					if (LexVisualBatchMeshItem->DrawCall == DrawCallItem)//already exist in this draw-call (added previously)
					{
						DrawCallItem->BatchMeshVisualObjectList.Add(LexVisualBatchMeshItem);
						//mark sort list
						DrawCallItem->bNeedToSortBatchMeshVisualObjectList = true;
						//update tree
						DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(ItemGeo->BoundsMin2DInCanvasSpace, ItemGeo->BoundsMax2DInCanvasSpace));
						DrawCallItem->VerticesCount += ItemGeo->Vertices.Num();
						DrawCallItem->IndicesCount += ItemGeo->Triangles.Num();
					}
					else//not exist in this draw-call
					{
						auto OldDrawCall = LexVisualBatchMeshItem->DrawCall;
						if (OldDrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
						{
							ClearObjectFromDrawCall(OldDrawCall, LexVisualBatchMeshItem);
						}
						//add to this draw-call
						DrawCallItem->BatchMeshVisualObjectList.Add(LexVisualBatchMeshItem);
						DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(ItemGeo->BoundsMin2DInCanvasSpace, ItemGeo->BoundsMax2DInCanvasSpace));
						DrawCallItem->VerticesCount += ItemGeo->Vertices.Num();
						DrawCallItem->IndicesCount += ItemGeo->Triangles.Num();
						DrawCallItem->bNeedToUpdateVertex = true;
						LexVisualBatchMeshItem->DrawCall = DrawCallItem;
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
					auto OldDrawCall = LexVisualBatchMeshItem->DrawCall;
					if (OldDrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
					{
						if (InUIDrawCallList.Contains(OldDrawCall))//if this draw-call already exist (added previously), then remove the object from the draw-call.
						{
							ClearObjectFromDrawCall(OldDrawCall, LexVisualBatchMeshItem);
						}
					}
					//make a new draw-call
					PushSingleDrawCall(LexVisualBatchMeshItem, true, ItemGeo, ELexUIDrawCallType::BatchMesh, is2DUIItem);
					check(LexVisualBatchMeshItem->DrawCall->VerticesCount < LEXUI_MAX_VERTEX_COUNT);
				}
			}
			break;
			case ELexVisualType::PostProcess:
			{
				auto UIPostProcessRenderableItem = (ULexVisualPostProcess*)Visual;
				if (!UIPostProcessRenderableItem->HaveValidData())continue;
				//every postprocess is a draw-call
				bool is2DUIItem = true;//post process just use true because it not matter
				PushSingleDrawCall(Visual, true, nullptr, ELexUIDrawCallType::PostProcess, is2DUIItem);
				//no need to copy draw-call's update data for UIPostProcessRenderable, because UIPostProcessRenderable's draw-call should be the same as previous one

				FitInDrawCallMinIndex = InUIDrawCallList.Num();
			}
			break;
			case ELexVisualType::DirectMesh:
			{
				auto UIDirectMeshRenderableItem = (ULexVisualDirectMesh*)Visual;
				if (!UIDirectMeshRenderableItem->HaveValidData())continue;
				//every direct mesh is a draw-call
				bool is2DUIItem = true;//post process just use true because it not matter
				PushSingleDrawCall(Visual, true, nullptr, ELexUIDrawCallType::DirectMesh, is2DUIItem);
				UIDirectMeshRenderableItem->DrawCall->Material = UIDirectMeshRenderableItem->GetMaterial();
			}
			break;
			}
		}
	}
}

void ULexCanvas::SetOverrideViewLocation(bool Override, FVector Value)
{
	bOverrideViewLocation = Override;
	OverrideViewLocation = Value;
}
void ULexCanvas::SetOverrideViewRotation(bool Override, FRotator Value)
{
	bOverrideViewRotation = Override;
	OverrideViewRotation = Value;
}
void ULexCanvas::SetOverrideFovAngle(bool Override, float Value)
{
	bOverrideFovAngle = Override;
	OverrideFovAngle = Value;
}
void ULexCanvas::SetOverrideProjectionMatrix(bool Override, FMatrix Value)
{
	bOverrideProjectionMatrix = Override;
	OverrideProjectionMatrix = Value;
}

void ULexCanvas::MarkSizeChanged()
{
	bIsViewProjectionMatrixDirty = true;
}

void ULexCanvas::SetDefaultMeshType(TSubclassOf<ULexUIMeshComponent> InValue)
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

void ULexCanvas::MarkFinishUpdateRootCanvasDrawCall()
{
	//All children canvas clip data is stored in root canvas, so update from root canvas
	if (this == RootCanvas)
	{
		for (const auto& ClipData : ClipDataList)
		{
			ClipData->UpdateData();
		}
	}
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
		item->MarkFinishUpdateRootCanvasDrawCall();
	}
}

void ULexCanvas::UpdateRootCanvasDrawCall()
{		
	/**
	 * Why use bPrevIsVisible?:
	 * If Canvas is rendering in frame 1, but when in frame 2 the Canvas is disabled(set WidgetActive to false), then the Canvas will not do draw-call calculation, and the prev existing draw-call mesh is still there and render,
	 * so we check bPrevIsVisible, then we can still do draw-call calculation at this frame, and the prev existing draw-call will be removed.
	 */
	const bool bNowIsVisible = LexWidget->GetWidgetActiveInHierarchy();
	if (bNowIsVisible || bPrevIsVisible)
	{
		if (bNowIsVisible != bPrevIsVisible)
		{
			bCanTickUpdate = true;
		}
		bPrevIsVisible = bNowIsVisible;
	}

	//update draw-call
	if (bCanTickUpdate)
	{
		bCanTickUpdate = false;
		RootCanvas->bAnythingChangedForRenderTarget = true;

		struct LOCAL
		{
			static void CollectRenderWidget(ULexWidget* Widget
				, ULexCanvas* ThisCanvas
				, TArray<TObjectPtr<ULexWidget>>& WidgetCollection)
			{
				WidgetCollection.Add(Widget);//maybe sub-canvas, so collect it before tell canvas
				if (Widget->GetRenderCanvas() == ThisCanvas)
				{
					for (auto Child : Widget->GetUIChildren())
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
			LOCAL::CollectRenderWidget(this->LexWidget.Get(), this, WidgetList);
		}
		//update layout from tail to head
		for (int i = WidgetList.Num() - 1; i >= 0; i--)
		{
			auto& Widget = WidgetList[i];
			if (Widget->GetWidgetActiveInHierarchy() && Widget->GetRenderCanvas() == this)
			{
				Widget->UpdateLayout();
			}
		}
		//update clip and geometry from head to tail
		for (const auto& Widget : WidgetList)
		{
			Widget->UpdateClip(RootCanvas->ClipDataAsTexture, RootCanvas->ClipDataList);
			if (Widget->GetWidgetActiveInHierarchy() && Widget->GetRenderCanvas() == this)
			{
				Widget->UpdateVisual();
			}
		}
		
		if (bShouldRebuildDrawCall)
		{
			bShouldRebuildDrawCall = false;
			CheckUIMesh();
			auto ClearDrawCallData = [this](TArray<TSharedPtr<FLexUIDrawCall>>& DrawCallArray) {
				for (int i = 0; i < DrawCallArray.Num(); i++)
				{
					auto DrawCallInCache = DrawCallArray[i];
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
					if (DrawCallInCache->DirectMeshVisualObject.IsValid())
					{
						DrawCallInCache->DirectMeshVisualObject->ClearMeshData();
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
			const auto Width = FMath::Max(LexWidget->GetWidth(), 100.0f);
			const auto Height = FMath::Max(LexWidget->GetHeight(), 100.0f);
			FVector2D LeftBottomPoint;
			LeftBottomPoint.X = Width * -LexWidget->GetPivot().X;
			LeftBottomPoint.Y = Height * -LexWidget->GetPivot().Y;
			FVector2D RightTopPoint;
			RightTopPoint.X = Width * (1.0f - LexWidget->GetPivot().X);
			RightTopPoint.Y = Height * (1.0f - LexWidget->GetPivot().Y);
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
	//update children canvas
	for (auto& item : ChildrenCanvasArray)
	{
		if (!item.IsValid())continue;
		if (item->bForceRenderToTarget)continue;
		item->UpdateRootCanvasDrawCall();
	}
}

DECLARE_CYCLE_STAT(TEXT("Canvas UpdateDrawCallMesh"), STAT_UpdateDrawCallMesh, STATGROUP_LGUI);
void ULexCanvas::UpdateDrawCallMesh_Implement()
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
				DrawCallItem->DirectMeshVisualObject->OnMeshDataReady();
				UIMesh->CreateRenderSectionRenderData(MeshSection.Pin());
				//create new mesh section, need to sort it
				bNeedToSortRenderPriority = true;
				bNeedToUpdateBounds = true;
				MarkRootCanvasNeedToUpdateChildrenCanvasBounds();
			}
		}
		break;
		case ELexUIDrawCallType::BatchMesh:
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
			if (this->GetActualRenderMode() == ELexRenderMode::WorldSpace)
			{
				continue;
			}

			if (!DrawCallItem->DrawCallRenderSection.IsValid())
			{
				auto RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::PostProcess);
				auto ChildCanvasSection = (FLexUIPostProcessSection*)RenderSection.Get();
				ChildCanvasSection->PostProcessVisualObject = DrawCallItem->PostProcessVisualObject;
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

float ULexCanvas::GetLastRenderTime()const
{
	auto TempRenderMode = GetActualRenderMode();
#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld())//edit mode
	{
		if (TempRenderMode == ELexRenderMode::ScreenSpaceOverlay)
			TempRenderMode = ELexRenderMode::WorldSpace_LexUI;
	}
#endif
	if (RenderModeIsLexRendererOrUERenderer(TempRenderMode))
	{
		return LastRenderTime;
	}
	else
	{
		return GetUIMesh()->GetLastRenderTime();
	}
}

void ULexCanvas::CheckUIMesh()const
{
	if (!UIMesh.IsValid())
	{
		auto MeshType = DefaultMeshType.Get();
		if (MeshType == nullptr)MeshType = ULexUIMeshComponent::StaticClass();
		auto ObjectName = MakeUniqueObjectName(this->GetOwner(), MeshType, FName(*this->GetLexWidget()->GetDisplayName()));
		UIMesh = NewObject<ULexUIMeshComponent>(this->GetOwner(), MeshType, ObjectName, RF_Transient);
		UIMesh->RegisterComponent();
		UIMesh->AttachToComponent(this->GetOwner()->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		UIMesh->SetRelativeTransform(FTransform::Identity);
		UIMesh->SetRenderCanvas((ULexCanvas*)this);
		bUIMeshNeedToSetInitialParameters = true;
	}

	if (bUIMeshNeedToSetInitialParameters)
	{
		bUIMeshNeedToSetInitialParameters = false;
		if (RenderModeIsLexRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (!GetWorld()->IsGameWorld())//edit mode
			{
				if (ActualRenderMode == ELexRenderMode::ScreenSpaceOverlay)
					ActualRenderMode = ELexRenderMode::WorldSpace_LexUI;
			}
#endif
			switch (ActualRenderMode)
			{
			case ELexRenderMode::RenderTarget:
			{
				UIMesh->SetSupportLexUIRenderer(true, this->GetRootCanvas()->GetRenderTargetViewExtension(), false);
				UIMesh->SetSupportUERenderer(false);
			}
			break;
			case ELexRenderMode::ScreenSpaceOverlay:
			{
#if WITH_EDITOR
				if (!GetWorld()->IsGameWorld())
				{
					UIMesh->SetSupportLexUIRenderer(true, ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), false);
					UIMesh->SetSupportUERenderer(true);
				}
				else
#endif
				{
					UIMesh->SetSupportLexUIRenderer(true, ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), false);
					UIMesh->SetSupportUERenderer(false);
				}
			}
			break;
			case ELexRenderMode::WorldSpace_LexUI:
			{
				UIMesh->SetSupportLexUIRenderer(true, ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true), true);
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

void ULexCanvas::SortDrawCall()
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
		case ELexUIDrawCallType::BatchMesh:
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

	if (this->IsRootCanvas())
	{
		switch (this->GetActualRenderMode())
		{
		case ELexRenderMode::ScreenSpaceOverlay:
			ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true)->MarkNeedToSortScreenSpacePrimitiveRenderPriority();
			break;
		case ELexRenderMode::RenderTarget:
			GetRenderTargetViewExtension()->MarkNeedToSortScreenSpacePrimitiveRenderPriority();
			break;
		case ELexRenderMode::WorldSpace_LexUI:
			ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true)->MarkNeedToSortWorldSpacePrimitiveRenderPriority();
			break;
		}
	}
}

FName ULexCanvas::LexUI_MainTextureMaterialParameterName = FName(TEXT("LexUI_MainTexture"));
FName ULexCanvas::LexUI_FontTextureMaterialParameterName = FName(TEXT("LexUI_FontTexture"));
FName ULexCanvas::LexUI_ClipDataTexture_MaterialParameterName = FName(TEXT("LexUI_ClipDataTexture"));
FName ULexCanvas::LexUI_WidgetPropertyDataTexture_MaterialParameterName = FName(TEXT("LexUI_WidgetPropertyDataTexture"));

bool ULexCanvas::IsMaterialContainsLexUIParameter(UMaterialInterface* InMaterial)
{
	static TArray<FMaterialParameterInfo> ParameterInfos;
	static TArray<FGuid> ParameterIds;
	InMaterial->GetAllTextureParameterInfo(ParameterInfos, ParameterIds);
	auto FoundIndex = ParameterInfos.IndexOfByPredicate([](const FMaterialParameterInfo& Item)
		{
			return
				Item.Name == LexUI_MainTextureMaterialParameterName
				|| Item.Name == LexUI_FontTextureMaterialParameterName
				|| Item.Name == LexUI_ClipDataTexture_MaterialParameterName
				|| Item.Name == LexUI_WidgetPropertyDataTexture_MaterialParameterName
				;
		});
	return FoundIndex != INDEX_NONE;
}
DECLARE_CYCLE_STAT(TEXT("Canvas UpdateDrawCallMaterial"), STAT_UpdateDrawCallMaterial, STATGROUP_LGUI);
void ULexCanvas::UpdateDrawCallMaterial_Implement()
{
	SCOPE_CYCLE_COUNTER(STAT_UpdateDrawCallMaterial);
	for (int i = 0; i < UIDrawCallList.Num(); i++)
	{
		auto DrawCallItem = UIDrawCallList[i];
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchMesh:
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
					auto bContainsLexUIParam = IsMaterialContainsLexUIParameter(SrcMaterial);
					if (SrcMaterial->IsA(UMaterialInstanceDynamic::StaticClass()))//if custom material is UMaterialInstanceDynamic then use it directly
					{
						RenderMat = SrcMaterial;
						UIMesh->SetMeshSectionMaterial(DrawCallItem->DrawCallRenderSection.Pin(), SrcMaterial);
					}
					else//if custom material is not UMaterialInstanceDynamic
					{
						if (bContainsLexUIParam)//if custom material contains LexUI parameters, then LexUI should control these parameters, then we need to create UMaterialInstanceDynamic with the custom material
						{
							RenderMat = UMaterialInstanceDynamic::Create(SrcMaterial, this);
							RenderMat->SetFlags(RF_Transient);
							UIMesh->SetMeshSectionMaterial(DrawCallItem->DrawCallRenderSection.Pin(), RenderMat.Get());
							if (DrawCallItem->DirectMeshVisualObject.IsValid())
							{
								DrawCallItem->DirectMeshVisualObject->OnMaterialInstanceDynamicCreated((UMaterialInstanceDynamic*)RenderMat.Get());
							}
							for (auto& RenderObjectItem : DrawCallItem->BatchMeshVisualObjectList)
							{
								RenderObjectItem->OnMaterialInstanceDynamicCreated((UMaterialInstanceDynamic*)RenderMat.Get());
							}
						}
						else//if custom material not contains LexUI parameters, then use it directly
						{
							RenderMat = SrcMaterial;
							UIMesh->SetMeshSectionMaterial(DrawCallItem->DrawCallRenderSection.Pin(), SrcMaterial);
						}
					}
					DrawCallItem->bMaterialContainsLexUIParameter = bContainsLexUIParam;
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
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_FontTextureMaterialParameterName, DrawCallItem->FontTexture.Get());
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_ClipDataTexture_MaterialParameterName, RootCanvas->ClipDataAsTexture->GetDataTexture());
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_WidgetPropertyDataTexture_MaterialParameterName, WidgetPropertyDataAsTexture->GetDataTexture());
				}
				DrawCallItem->bTextureChanged = false;
				DrawCallItem->bMaterialNeedToReassign = false;
				bNeedToVerifyMaterials = true;

				if (DrawCallItem->DirectMeshVisualObject.IsValid())
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
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_FontTextureMaterialParameterName, DrawCallItem->FontTexture.Get());
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
				if (DrawCallItem->PostProcessVisualObject.IsValid())
				{
					// nothing to do here
				}
				DrawCallItem->bMaterialChanged = false;
			}
		}
		break;
		}
	}

	MarkNeedVerifyMaterials();//tell parent canvas to verify material
	if (bNeedToVerifyMaterials)
	{
		bNeedToVerifyMaterials = false;
		UIMesh->VerifyMaterials();
	}
}

void ULexCanvas::MarkNeedVerifyMaterials()
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


UMaterialInstanceDynamic* ULexCanvas::GetUIMaterialFromPool()
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
void ULexCanvas::AddUIMaterialToPool(UMaterialInstanceDynamic* UIMat)
{
	bNeedToVerifyMaterials = true;
	if (UIMat->Parent == GetDefaultMaterial())
	{
		PooledUIMaterialList.Add(UIMat);
	}
}

void ULexCanvas::SetRenderTargetResolutionScale(float Value)
{
	if (RenderTargetResolutionScale != Value)
	{
		RenderTargetResolutionScale = Value;
		bAnythingChangedForRenderTarget = true;
	}
}

void ULexCanvas::SetRenderTargetSizeMode(ELexCanvasRenderTargetSizeMode Value)
{
	if (RenderTargetSizeMode != Value)
	{
		RenderTargetSizeMode = Value;
		bAnythingChangedForRenderTarget = true;
	}
}

void ULexCanvas::SetRenderTargetUpdateMode(ELexCanvasRenderTargetUpdateMode Value)
{
	if (RenderTargetUpdateMode != Value)
	{
		RenderTargetUpdateMode = Value;
		bAnythingChangedForRenderTarget = true;
	}
}

void ULexCanvas::RequestUpdateForRenderTarget()
{
	if (RootCanvas == this)
	{
		bRequestUpdateForRenderTarget = true;
	}
}

void ULexCanvas::SetSortOrderAdditionalValueRecursive(int32 InAdditionalValue)
{
	if (FMath::Abs(this->SortOrder + InAdditionalValue) > MAX_int16)
	{
		auto errorMsg = FText::Format(LOCTEXT("SortOrderOutOfRange", "{0} sortOrder out of range!\nNOTE! sortOrder value is stored with int16 type, so valid range is -32768 to 32767")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(LGUI, Error, TEXT("%s"), *errorMsg.ToString());
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errorMsg, false);
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

void ULexCanvas::SetSortOrder(int32 InSortOrder, bool InPropagateToChildrenCanvas)
{
	if (SortOrder != InSortOrder)
	{
		if (CheckRootCanvas())
		{
			RootCanvas->bNeedToSortRenderPriority = true;
		}
		MarkCanvasUpdate(false, false, false);
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
				UE_LOG(LGUI, Error, TEXT("%s"), *errorMsg.ToString());
#if WITH_EDITOR
				FLexUIUtils::EditorNotification(errorMsg, false);
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
void ULexCanvas::SetSortOrderToHighestOfHierarchy(bool InPropagateToChildrenCanvas)
{
	int32 Min = INT_MAX, Max = INT_MIN;
	GetMinMaxSortOrderOfHierarchy(Min, Max);
	SetSortOrder(Max + 1, InPropagateToChildrenCanvas);
}
void ULexCanvas::SetSortOrderToLowestOfHierarchy(bool InPropagateToChildrenCanvas)
{
	int32 Min = INT_MAX, Max = INT_MIN;
	GetMinMaxSortOrderOfHierarchy(Min, Max);
	SetSortOrder(Min - 1, InPropagateToChildrenCanvas);
}

void ULexCanvas::GetMinMaxSortOrderOfHierarchy(int32& OutMin, int32& OutMax)
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


UMaterialInterface* ULexCanvas::GetDefaultMaterial()const
{
	if (!DefaultMaterial)
	{
		DefaultMaterial = LoadObject<UMaterialInterface>(NULL, TEXT("/LGUI/Materials/LexUI_ImageAndFont"));
	}
	return DefaultMaterial;
}

void ULexCanvas::SetDefaultMaterial(UMaterialInterface* InMaterial)
{
	if (DefaultMaterial != InMaterial)
	{
		for (int i = 0; i < UIDrawCallList.Num(); i++)
		{
			auto DrawCallItem = UIDrawCallList[i];
			if (DrawCallItem->Type == ELexUIDrawCallType::BatchMesh)
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

void ULexCanvas::SetTraceChannel(TEnumAsByte<ETraceTypeQuery> InTraceChannel)
{
	if (TraceChannel != InTraceChannel)
	{
		TraceChannel = InTraceChannel;
	}
}

float ULexCanvas::GetActualBlendDepth()const
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

int ULexCanvas::GetActualDepthFade()const
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

int32 ULexCanvas::GetActualSortOrder()const
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

void ULexCanvas::SetOverrideSorting(bool Value)
{
	if (bOverrideSorting != Value)
	{
		bOverrideSorting = Value;
		if (CheckRootCanvas())
		{
			RootCanvas->bNeedToSortRenderPriority = true;
		}
		MarkCanvasUpdate(false, false, false);
	}
}

bool ULexCanvas::GetActualRequireNormalAndTangent()const
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
void ULexCanvas::SetRequireNormalAndTangent(bool Value)
{
	if (bRequireNormalAndTangent != Value)
	{
		bRequireNormalAndTangent = Value;
		MarkCanvasUpdate(false, false, false);
		if (CheckLexWidget())
		{
			LexWidget->MarkAllDirtyRecursive();
		}
	}
}

void ULexCanvas::BuildProjectionMatrix(FIntPoint InViewportSize, ECameraProjectionMode::Type InProjectionType, float InFOV, float FarClipPlane, float NearClipPlane, FMatrix& OutProjectionMatrix)
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
float ULexCanvas::CalculateDistanceToCamera()const
{
	if (ProjectionType == ECameraProjectionMode::Orthographic)
	{
		return 1000;
	}
	else
	{
		return LexWidget->GetWidth() * 0.5f / FMath::Tan(FMath::DegreesToRadians(FieldOfView * 0.5f)) * LexWidget->GetComponentScale().X;
	}
}
FMatrix ULexCanvas::GetViewProjectionMatrix()const
{
	if (bIsViewProjectionMatrixDirty)
	{
		if (!CheckLexWidget())
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d UIItem not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return CacheViewProjectionMatrix;
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
		CacheViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;
	}
	return CacheViewProjectionMatrix;
}
FMatrix ULexCanvas::GetProjectionMatrix()const
{
	if (bOverrideProjectionMatrix)
		return OverrideProjectionMatrix;

	FMatrix ProjectionMatrix = FMatrix::Identity;
	const float FOV = (bOverrideFovAngle ? OverrideFovAngle : FieldOfView) * (float)PI / 360.0f;
	BuildProjectionMatrix(FIntPoint(LexWidget->GetWidth(), LexWidget->GetHeight()), ProjectionType, FOV, FarClipPlane, NearClipPlane, ProjectionMatrix);
	return ProjectionMatrix;
}
FVector ULexCanvas::GetViewLocation()const
{
	if (bOverrideViewLocation)
		return OverrideViewLocation;

	return LexWidget->GetComponentLocation() - LexWidget->GetForwardVector() * CalculateDistanceToCamera();
}
FRotator ULexCanvas::GetViewRotator()const
{
	if (bOverrideViewRotation)
		return OverrideViewRotation;

	return LexWidget->GetComponentRotation();
}
FIntPoint ULexCanvas::GetViewportSize()const
{
	auto TempViewportSize = FIntPoint(2, 2);
	if (auto world = this->GetWorld())
	{
#if WITH_EDITOR
		if (!world->IsGameWorld())
		{
			if (CheckLexWidget())
			{
				TempViewportSize.X = LexWidget->GetWidth();
				TempViewportSize.Y = LexWidget->GetHeight();
			}
		}
		else
#endif
		{
			if (RenderMode == ELexRenderMode::ScreenSpaceOverlay)
			{
				if (auto pc = world->GetFirstPlayerController())
				{
					pc->GetViewportSize(TempViewportSize.X, TempViewportSize.Y);
				}
			}
			else if (RenderMode == ELexRenderMode::RenderTarget && IsValid(RenderTarget))
			{
				TempViewportSize.X = RenderTarget->SizeX / RenderTargetResolutionScale;
				TempViewportSize.Y = RenderTarget->SizeY / RenderTargetResolutionScale;
			}
		}
	}
	return TempViewportSize;
}

void ULexCanvas::SetRenderMode(ELexRenderMode Value)
{
	if (RenderMode != Value)
	{
		RenderMode = Value;
		MarkCanvasUpdate(false, false, false, true);
		CheckRenderMode(true);

		UnregisterCanvasScaler();
		RegisterCanvasScaler();
	}
}

void ULexCanvas::SetForceRenderToTarget(bool Value)
{
	if (bForceRenderToTarget != Value)
	{
		bForceRenderToTarget = Value;
		if (bForceRenderToTarget)
		{
			MarkCanvasUpdate(false, false, false, true);
			LexWidget->MarkAllDirtyRecursive();
		}
	}
}

void ULexCanvas::SetProjectionParameters(TEnumAsByte<ECameraProjectionMode::Type> InProjectionType, float InFovAngle, float InNearClipPlane, float InFarClipPlane)
{
	ProjectionType = InProjectionType;
	FieldOfView = InFovAngle;
	NearClipPlane = InNearClipPlane;
	FarClipPlane = InFarClipPlane;

	bIsViewProjectionMatrixDirty = true;
}

void ULexCanvas::SetRenderTarget(UTextureRenderTarget2D* Value)
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

void ULexCanvas::SetRenderTargetClearColor(FColor Value)
{
	if (RenderTargetClearColor != Value)
	{
		RenderTargetClearColor = Value;
		if (CheckRootCanvas() && RootCanvas == this)
		{
			this->bRequestUpdateForRenderTarget = true;
			this->MarkCanvasUpdate(false, false, false, false);
		}
	}
}

ELexRenderMode ULexCanvas::GetActualRenderMode()const
{
	if (IsRootCanvas())
	{
		return this->RenderMode;
	}
	else
	{
		if (bForceRenderToTarget)
		{
			checkf(this->RenderMode == ELexRenderMode::RenderTarget, TEXT("[%s].%d This error should not happen!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return this->RenderMode;
		}
		if (CheckRootCanvas())
		{
			return RootCanvas->RenderMode;
		}
	}
	return ELexRenderMode::WorldSpace;
}

void ULexCanvas::SetBlendDepth(float Value)
{
	if (BlendDepth != Value)
	{
		BlendDepth = Value;

		if (CheckRootCanvas())
		{
			if (RootCanvas->RenderModeIsLexRendererOrUERenderer(CurrentRenderMode))
			{
				if (RootCanvas->IsRenderToWorldSpace())
				{
					auto ViewExtension = ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
					if (ViewExtension.IsValid())
					{
						ViewExtension->SetRenderCanvasDepthParameter(this, this->GetActualBlendDepth(), this->GetActualDepthFade());
					}
				}
			}
		}
	}
}

void ULexCanvas::SetDepthFade(int Value)
{
	if (DepthFade != Value)
	{
		DepthFade = Value;

		if (CheckRootCanvas())
		{
			if (RootCanvas->RenderModeIsLexRendererOrUERenderer(CurrentRenderMode))
			{
				if (RootCanvas->IsRenderToWorldSpace())
				{
					auto ViewExtension = ULexUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
					if (ViewExtension.IsValid())
					{
						ViewExtension->SetRenderCanvasDepthParameter(this, this->GetActualBlendDepth(), this->GetActualDepthFade());
					}
				}
			}
		}
	}
}

void ULexCanvas::SetEnableDepthTest(bool Value)
{
	if (bEnableDepthTest != Value)
	{
		bEnableDepthTest = Value;
	}
}

UTextureRenderTarget2D* ULexCanvas::GetActualRenderTarget()const
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

int32 ULexCanvas::GetDrawCallCount()const
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

void ULexCanvas::OnClipDataTextureChanged(UTexture* NewTexture)
{
	for (const auto& DrawCallItem : UIDrawCallList)
	{
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchMesh:
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

void ULexCanvas::OnWidgetPropertyDataTextureChanged(UTexture* NewTexture)
{
	for (const auto& DrawCallItem : UIDrawCallList)
	{
		switch (DrawCallItem->Type)
		{
		case ELexUIDrawCallType::BatchMesh:
			{
				auto RenderMat = DrawCallItem->RenderMaterial;
				if (RenderMat.IsValid() && DrawCallItem->bMaterialContainsLexUIParameter)
				{
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_WidgetPropertyDataTexture_MaterialParameterName, NewTexture);
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

void ULexCanvas::RemoveClipData(const TSharedPtr<FLexUIClipData>& InClipData)
{
	RootCanvas->ClipDataList.Remove(InClipData);
}
UTexture* ULexCanvas::GetClipDataTexture()const
{
	return IsValid(RootCanvas->ClipDataAsTexture) ? RootCanvas->ClipDataAsTexture->GetDataTexture() : nullptr;
}

FTransform2D ULexCanvas::ConvertTo2DTransform(const FTransform& Transform)
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
void ULexCanvas::CalculateVisual2DBounds(ULexVisual* item, const FTransform2D& transform, FVector2D& min, FVector2D& max)
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


#pragma region CanvasScaler
void ULexCanvasCustomScale::Init(ULexCanvas* InCanvas)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveInit(InCanvas);
	}
}
void ULexCanvasCustomScale::CalculateSizeAndScale(ULexCanvas* InCanvas, const FIntPoint& InViewportSize, FIntPoint& OutLGUICanvasSize, float& OutScale)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveCalculateSizeAndScale(InCanvas, InViewportSize, OutLGUICanvasSize, OutScale);
	}
}

bool ULexCanvasCustomScale::ConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result) const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveConvertPositionFromViewportToCanvas(InPosition, Result);
	}
	return false;
}

bool ULexCanvasCustomScale::ConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result) const
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		return ReceiveConvertPositionFromCanvasToViewport(InPosition, Result);
	}
	return false;
}

void ULexCanvas::CheckAndApplyViewportParameter()
{
	switch (this->GetRenderMode())
	{
	case ELexRenderMode::ScreenSpaceOverlay:
	{
		ViewportSize = this->GetViewportSize();
		OnViewportParameterChanged();
	}
	break;
	case ELexRenderMode::RenderTarget:
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

void ULexCanvas::RegisterCanvasScaler()
{
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld())
	{
		EditorTickDelegateHandle = ULexUIManagerObject::RegisterEditorTickFunction([this](float deltaTime) {
			this->OnEditorTick(deltaTime);
			});
	}
#endif

	bIsViewProjectionMatrixDirty = true;

	if (this->IsRootCanvas())
	{
		if (this->GetRenderMode() == ELexRenderMode::ScreenSpaceOverlay
			|| this->GetRenderMode() == ELexRenderMode::RenderTarget
			)
		{
			CheckAndApplyViewportParameter();

			if (this->GetRenderMode() == ELexRenderMode::ScreenSpaceOverlay)
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

void ULexCanvas::UnregisterCanvasScaler()
{
#if WITH_EDITOR
	if (EditorTickDelegateHandle.IsValid())
	{
		ULexUIManagerObject::UnregisterEditorTickFunction(EditorTickDelegateHandle);
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

void ULexCanvas::OnViewportParameterChanged()
{
	if (ViewportSize.X <= 0 || ViewportSize.Y <= 0)return;
	if (this->IsRootCanvas())
	{
		if (this->GetRenderMode() == ELexRenderMode::ScreenSpaceOverlay
			|| this->GetRenderMode() == ELexRenderMode::RenderTarget
			)
		{
			if (LexWidget.IsValid())
			{
				float TempCanvasScale = 1.0f;
				//adjust size
				switch (ScaleMode)
				{
				case ELexCanvasScaleMode::ConstantPixelSize:
					{
						LexWidget->SetWidth(ViewportSize.X);
						LexWidget->SetHeight(ViewportSize.Y);
						TempCanvasScale = 1.0f;
					}
					break;
				case ELexCanvasScaleMode::ScaleWithScreenSize:
					{
						switch (ScreenMatchMode)
						{
						case ELexCanvasScreenMatchMode::MatchWidthOrHeight:
							{
								float matchWidth_PreferredWidth = ReferenceResolution.X;
								float matchWidth_PreferredHeight = ReferenceResolution.X * ViewportSize.Y / ViewportSize.X;
								float matchWidth_ScaleRatio = ViewportSize.X / ReferenceResolution.X;

								float matchHeight_PreferredHeight = ReferenceResolution.Y;
								float matchHeight_PreferredWidth = ReferenceResolution.Y * ViewportSize.X / ViewportSize.Y;
								float matchHeight_ScaleRatio = ViewportSize.Y / ReferenceResolution.Y;

								LexWidget->SetWidth(FMath::Lerp(matchWidth_PreferredWidth, matchHeight_PreferredWidth, MatchFromWidthToHeight));
								LexWidget->SetHeight(FMath::Lerp(matchWidth_PreferredHeight, matchHeight_PreferredHeight, MatchFromWidthToHeight));

								TempCanvasScale = FMath::Lerp(matchWidth_ScaleRatio, matchHeight_ScaleRatio, MatchFromWidthToHeight);
							}
							break;
						case ELexCanvasScreenMatchMode::Expand:
						case ELexCanvasScreenMatchMode::Shrink:
							{
								float resultWidth = ViewportSize.X, resultHeight = ViewportSize.Y;

								float screenAspect = (float)ViewportSize.X / ViewportSize.Y;
								float referenceAspect = ReferenceResolution.X / ReferenceResolution.Y;
								if (screenAspect > referenceAspect)//screen width > reference width
								{
									if (ScreenMatchMode == ELexCanvasScreenMatchMode::Shrink)
									{
										resultHeight = ReferenceResolution.Y;
										resultWidth = resultHeight * screenAspect;
										TempCanvasScale = (float)ViewportSize.Y / resultHeight;
									}
									else if (ScreenMatchMode == ELexCanvasScreenMatchMode::Expand)
									{
										resultWidth = ReferenceResolution.X;
										resultHeight = resultWidth / screenAspect;
										TempCanvasScale = (float)ViewportSize.X / resultWidth;
									}
								}
								else//screen height > reference height
								{
									if (ScreenMatchMode == ELexCanvasScreenMatchMode::Shrink)
									{
										resultWidth = ReferenceResolution.X;
										resultHeight = resultWidth / screenAspect;
										TempCanvasScale = (float)ViewportSize.X / resultWidth;
									}
									else if (ScreenMatchMode == ELexCanvasScreenMatchMode::Expand)
									{
										resultHeight = ReferenceResolution.Y;
										resultWidth = resultHeight * screenAspect;
										TempCanvasScale = (float)ViewportSize.Y / resultHeight;
									}
								}
								LexWidget->SetWidth(resultWidth);
								LexWidget->SetHeight(resultHeight);
							}
							break;
						}
					}
					break;
				case ELexCanvasScaleMode::Custom:
					{
						if (IsValid(CustomScale))
						{
							TempCanvasScale = 1.0f;
							auto ScaledViewportSize = ViewportSize;
							CustomScale->CalculateSizeAndScale(this, ViewportSize, ScaledViewportSize, TempCanvasScale);
							LexWidget->SetWidth(ScaledViewportSize.X);
							LexWidget->SetHeight(ScaledViewportSize.Y);
						}
						else
						{
							//default is constant pixel
							LexWidget->SetWidth(ViewportSize.X);
							LexWidget->SetHeight(ViewportSize.Y);
							TempCanvasScale = 1.0f;
						}
					}
					break;
				}
				this->CanvasScale = TempCanvasScale;

				LexWidget->MarkAllDirtyRecursive();
				this->MarkCanvasUpdate(false, true, false, true);
			}
		}
	}
}
#if WITH_EDITOR
void ULexCanvas::OnEditorTick(float DeltaTime)
{
	if (ULexUIManagerWorldSubsystem::GetIsPlaying())//When hit play there is still a editor world and DrawViewportArea is called, which could cause frame dropdown, so skip it when playing
		return;
	if (this->IsRootCanvas() && !this->bForceRenderToTarget)
	{
		if (this->GetRenderMode() == ELexRenderMode::ScreenSpaceOverlay
			|| this->GetRenderMode() == ELexRenderMode::RenderTarget
			)
		{
			DrawViewportArea();
			if (ULexUIManagerObject::IsSelected(this->GetOwner()))
			{
				DrawVirtualCamera();
			}
				
			if (!GetWorld()->IsGameWorld())
			{
				if (this->GetRenderMode() == ELexRenderMode::ScreenSpaceOverlay)
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
						if (auto ViewportClient = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld())->GetEditorViewportClient())
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
				if (this->GetRenderMode() == ELexRenderMode::RenderTarget && IsValid(this->RenderTarget))
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

void ULexCanvas::DrawViewportArea()
{
	auto RectExtends = FVector(0.1f, LexWidget->GetWidth(), LexWidget->GetHeight()) * 0.5f;
	auto RectDrawColor = FColor(128, 128, 128, 128);//gray means normal object
	auto WorldTransform = LexWidget->GetComponentTransform();

	ULexUIManagerWorldSubsystem::DrawDebugBox(GetWorld()
		, FVector::Zero(), FMatrix44f(WorldTransform.ToMatrixWithScale())
		, RectExtends, RectDrawColor, this, FString::Printf(TEXT("%s.LexCanvas.ViewportArea"), *this->GetOwner()->GetActorLabel())
		, false);
}

void ULexCanvas::DrawVirtualCamera()
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

	ULexUIManagerWorldSubsystem::DrawDebugLine(GetWorld(), FMatrix44f::Identity
		, LinePoints, lineColor, this, FString::Printf(TEXT("%s.LexCanvas.VirtualCamera"), *this->GetOwner()->GetActorLabel())
		, false);

	// if (LexWidget.IsValid())
	// {
	// 	DrawDebugCamera(this->GetWorld(), this->GetViewLocation(), this->GetViewRotator(), FieldOfView, this->GetLexWidget()->GetComponentScale().X * 3.0f, FColor::Green);
	// }
}
#endif

void ULexCanvas::SetProjectionType(TEnumAsByte<ECameraProjectionMode::Type> Value)
{
	if (ProjectionType != Value)
	{
		ProjectionType = ProjectionType = Value;
		OnViewportParameterChanged();
	}
}
void ULexCanvas::SetFieldOfView(float Value)
{
	if (FieldOfView != Value)
	{
		FieldOfView = FieldOfView = Value;
		OnViewportParameterChanged();
	}
}
void ULexCanvas::SetNearClipPlane(float Value)
{
	if (NearClipPlane != Value)
	{
		NearClipPlane = Value;
		OnViewportParameterChanged();
	}
}
void ULexCanvas::SetFarClipPlane(float Value)
{
	if (FarClipPlane != Value)
	{
		FarClipPlane = Value;
		OnViewportParameterChanged();
	}
}

void ULexCanvas::SetScaleMode(ELexCanvasScaleMode Value)
{
	if (ScaleMode != Value)
	{
		ScaleMode = Value;
		OnViewportParameterChanged();
	}
}
void ULexCanvas::SetReferenceResolution(FVector2D Value)
{
	if (ReferenceResolution != Value)
	{
		ReferenceResolution = Value;
		OnViewportParameterChanged();
	}
}
void ULexCanvas::SetMatchFromWidthToHeight(float Value)
{
	if (MatchFromWidthToHeight != Value)
	{
		MatchFromWidthToHeight = Value;
		OnViewportParameterChanged();
	}
}
void ULexCanvas::SetScreenMatchMode(ELexCanvasScreenMatchMode Value)
{
	if (ScreenMatchMode != Value)
	{
		ScreenMatchMode = Value;
		OnViewportParameterChanged();
	}
}
void ULexCanvas::SetCustomScale(ULexCanvasCustomScale* Value)
{
	if (CustomScale != Value)
	{
		CustomScale = Value;
		CustomScale->Init(this);//need to initialize when first set
		if (ScaleMode == ELexCanvasScaleMode::Custom)
		{
			OnViewportParameterChanged();
		}
	}
}

bool ULexCanvas::ConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result)const
{
	if (RootCanvas != this)return false;
	switch (ScaleMode)
	{
	case ELexCanvasScaleMode::ConstantPixelSize:
		Result = FVector2D(InPosition.X, ViewportSize.Y - InPosition.Y);
		return true;
	case ELexCanvasScaleMode::ScaleWithScreenSize:
		Result = FVector2D(InPosition.X, ViewportSize.Y - InPosition.Y) / this->CanvasScale;
		return true;
	case ELexCanvasScaleMode::Custom:
		if (IsValid(CustomScale))
		{
			return CustomScale->ConvertPositionFromViewportToCanvas(InPosition, Result);
		}
	}
	return false;
}
bool ULexCanvas::ConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result)const
{
	if (RootCanvas != this)return false;
	switch (ScaleMode)
	{
	case ELexCanvasScaleMode::ConstantPixelSize:
		Result = FVector2D(InPosition.X, ViewportSize.Y - InPosition.Y);
		return true;
	case ELexCanvasScaleMode::ScaleWithScreenSize:
		Result = FVector2D(InPosition.X * this->CanvasScale, ViewportSize.Y - InPosition.Y * this->CanvasScale);
		return true;
	case ELexCanvasScaleMode::Custom:
		if (IsValid(CustomScale))
		{
			return CustomScale->ConvertPositionFromCanvasToViewport(InPosition, Result);
		}
	}
	return false;
}
bool ULexCanvas::Project3DToScreen(const FVector& Position3D, FVector2D& OutPosition2D)const
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
		//Convert to LGUI's viewport size
		OutPosition2D *= this->GetViewportSize();
		OutPosition2D /= this->CanvasScale;

		return true;
	}
	return false;
}

#if 1
#include "GameFramework/PlayerController.h"
#include "Engine/LocalPlayer.h"
bool ULexCanvas::ProjectWorldToScreen(APlayerController* Player, const FVector& Position3D, FVector2D& OutPosition2D)const
{
	ULocalPlayer* const LP = Player ? Player->GetLocalPlayer() : nullptr;
	if (LP && LP->ViewportClient)
	{
		auto TempFovAngle = Player->PlayerCameraManager->GetFOVAngle() * (float)PI / 360.0f;
		auto TempViewportSize = LP->ViewportClient->Viewport->GetSizeXY();
		FMatrix ProjectionMatrix;
		ULexCanvas::BuildProjectionMatrix(TempViewportSize, ECameraProjectionMode::Perspective
			, TempFovAngle, 1000, 0.01f, ProjectionMatrix);

		auto ViewLocation = Player->PlayerCameraManager->GetCameraLocation();
		auto ViewRotationMatrix = FInverseRotationMatrix(Player->GetRootComponent()->GetComponentRotation()) * FMatrix(
			FPlane(0, 0, 1, 0),
			FPlane(1, 0, 0, 0),
			FPlane(0, 1, 0, 0),
			FPlane(0, 0, 0, 1));
		auto ViewProjectionMatrix = FTranslationMatrix(-ViewLocation) * ViewRotationMatrix * ProjectionMatrix;

		auto ScreenPos = ViewProjectionMatrix.TransformFVector4(FVector4(Position3D, 1.0f));
		if (ScreenPos.W > 0.0f)
		{
			// the result of this will be x and y coords in -1..1 projection space
			const float RHW = 1.0f / ScreenPos.W;
			FPlane PosInScreenSpace = FPlane(ScreenPos.X * RHW, ScreenPos.Y * RHW, ScreenPos.Z * RHW, ScreenPos.W);

			// Move from projection space to normalized 0..1 UI space
			const float NormalizedX = (PosInScreenSpace.X / 2.f) + 0.5f;
			const float NormalizedY = 1.f - (PosInScreenSpace.Y / 2.f) - 0.5f;

			OutPosition2D.X = (NormalizedX * (float)ViewportSize.X);
			OutPosition2D.Y = (NormalizedY * (float)ViewportSize.Y);

			return ConvertPositionFromViewportToCanvas(FVector2D(OutPosition2D), OutPosition2D);
		}
	}
	return false;
}
#endif

#pragma endregion


