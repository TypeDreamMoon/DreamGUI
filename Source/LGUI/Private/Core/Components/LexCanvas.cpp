// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUI/Public/Core/Components/LexCanvas.h"
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
#include "LGUI/Public/Core/Components/LexVisual.h"
#include "LGUI/Public/Core/Components/LexVisualPostProcess.h"
#include "LGUI/Public/Core/Components/UIDirectMeshRenderable.h"
#include "LGUI/Public/Core/Components/LexWidget.h"
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

ULexCanvas::ULexCanvas()
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
	DefaultMaterial = LoadObject<UMaterialInterface>(NULL, TEXT("/LGUI/Materials/LexUI_ImageAndFont"));
}

void ULexCanvas::BeginPlay()
{
	Super::BeginPlay();
	CheckRootCanvas();
	CurrentRenderMode = this->GetActualRenderMode();
	if (CheckUIItem())
	{
		bPrevUIItemIsActive = LexWidget->GetIsUIActiveInHierarchy();
	}
	else
	{
		bPrevUIItemIsActive = false;
	}
	MarkCanvasUpdate(true, true, true, true);

	bNeedToSortRenderPriority = true;
}
void ULexCanvas::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
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
		if (RenderModeIsLGUIRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (bPreviewWithLGUIRenderer)
			{
				if (!GetWorld()->IsGameWorld())//edit mode
				{
					if (ActualRenderMode == ELexRenderMode::ScreenSpaceOverlay)
						ActualRenderMode = ELexRenderMode::WorldSpace_LGUI;
				}
			}
#endif
			switch (ActualRenderMode)
			{
			case ELexRenderMode::ScreenSpaceOverlay:
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
			case ELexRenderMode::RenderTarget:
			{
				if (!bHasAddToLGUIScreenSpaceRenderer)
				{
					GetRenderTargetViewExtension()->SetScreenSpaceRootCanvas(this);
					bHasAddToLGUIScreenSpaceRenderer = true;
				}
				bIsRenderTargetRenderer = true;
			}
			break;
			case ELexRenderMode::WorldSpace_LGUI:
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
					RenderTargetViewExtension->UpdateRenderTargetRenderer(RenderTarget);
				}
			}
		}
	}
}

void ULexCanvas::UpdateRenderTarget(bool CallEvent)
{
	FIntPoint DesiredRenderTargetSize(LexWidget->GetRenderWidth() * RenderTargetResolutionScale, LexWidget->GetRenderHeight() * RenderTargetResolutionScale);
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
			OnRenderTargetCreatedOrChanged.Broadcast(RenderTarget, true);
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
				OnRenderTargetCreatedOrChanged.Broadcast(RenderTarget, false);
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
	if (CheckUIItem())
	{
		ULGUIManagerWorldSubsystem::AddCanvas(this, CurrentRenderMode);
		//tell UIItem
		LexWidget->RegisterRenderCanvas(this);
		UIHierarchyChangedDelegateHandle = LexWidget->RegisterUIHierarchyChanged(FSimpleDelegate::CreateUObject(this, &ULexCanvas::OnUIHierarchyChanged));
		UIActiveStateChangedDelegateHandle = LexWidget->RegisterUIActiveStateChanged(FUIItemActiveInHierarchyStateChangedDelegate::CreateUObject(this, &ULexCanvas::OnUIActiveStateChanged));

		OnUIHierarchyChanged();
	}

	if (!IsValid(ClipDataAsTexture))
	{
		ClipDataAsTexture = NewObject<ULexUIDataAsTexture>(this, ULexUIDataAsTexture::StaticClass(), NAME_None, RF_Transient);
		ClipDataAsTexture->Init(FLexUIClipData::BlockSizeInBytes, 512);
		ClipDataAsTexture->OnDataTextureChange.AddUObject(this, &ULexCanvas::OnClipDataTextureChanged);
		ClipDataAsTexture->RegisterBuffer();//register a zero position as a placeholder for not clipping type.
	}
}
void ULexCanvas::OnUnregister()
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
	if (LexWidget.IsValid())
	{
		LexWidget->UnregisterRenderCanvas();
		LexWidget->UnregisterUIHierarchyChanged(UIHierarchyChangedDelegateHandle);
		LexWidget->UnregisterUIActiveStateChanged(UIActiveStateChangedDelegateHandle);
	}
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

bool ULexCanvas::CheckUIItem()const
{
	if (LexWidget.IsValid())return true;
	if (this->GetWorld() == nullptr)return false;
	LexWidget = Cast<ULexWidget>(GetOwner()->GetRootComponent());
	if (!LexWidget.IsValid())
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
		if (CheckUIItem())
		{
			LexWidget->MarkRenderModeChangeRecursive(this, OldRenderMode, CurrentRenderMode);
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
void ULexCanvas::OnUIHierarchyChanged()
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

void ULexCanvas::OnUIActiveStateChanged(bool value)
{
	if (value)
	{
		if (ParentCanvas.IsValid())
		{
			ParentCanvas->bNeedToGenerateWidgetList = true;
			ParentCanvas->MarkCanvasUpdate(false, false, true//why make this to true? becase we need to sort UIRenderableList, and set bShouldSortRenderableOrder to true can do it
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
			|| RootCanvas->RenderMode == ELexRenderMode::WorldSpace_LGUI
			;
	}
	return false;
}

bool ULexCanvas::IsRenderByLGUIRendererOrUERenderer()const
{
	if (CheckRootCanvas())
	{
		return RootCanvas->RenderMode == ELexRenderMode::ScreenSpaceOverlay
			|| RootCanvas->RenderMode == ELexRenderMode::RenderTarget
			|| RootCanvas->RenderMode == ELexRenderMode::WorldSpace_LGUI
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
		this->bShouldSortRenderableOrder = true;
	}
}
void ULexCanvas::MarkCanvasUpdateRecursive(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall)
{
	this->MarkCanvasUpdate(bMaterialOrTextureChanged, bTransformOrVertexPositionChanged, bHierarchyOrderChanged, bForceRebuildDrawCall);
	for (auto& ChildCanvas : this->ChildrenCanvasArray)
	{
		ChildCanvas->MarkCanvasUpdateRecursive(bMaterialOrTextureChanged, bTransformOrVertexPositionChanged, bHierarchyOrderChanged, bForceRebuildDrawCall);
	}
}

#if WITH_EDITOR
bool ULexCanvas::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{

	}

	return Super::CanEditChange(InProperty);
}
void ULexCanvas::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (CheckUIItem())
	{
		LexWidget->MarkAllDirtyRecursive();
	}
	if (CheckRootCanvas())
	{
		RootCanvas->MarkCanvasUpdate(true, true, true);
	}
}
void ULexCanvas::PostLoad()
{
	Super::PostLoad();
}
void ULexCanvas::PostEditUndo()
{
	Super::PostEditUndo();

	ULGUIManagerWorldSubsystem::RefreshAllUI(this->GetWorld());
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
	ULGUIPrefabManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]() {
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

bool ULexCanvas::GetIsUIActive()const
{
	if (LexWidget.IsValid())
	{
		return LexWidget->GetIsUIActiveInHierarchy();
	}
	return false;
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

void ULexCanvas::RegisterVisual(ULexWidget* InWidget)
{
	VisualWidgetList.AddUnique(InWidget);
}

void ULexCanvas::UnregisterVisual(ULexWidget* InWidget)
{
	VisualWidgetList.Remove(InWidget);
}

void ULexCanvas::AddLexWidget(ULexWidget* InUIItem)
{
	bNeedToGenerateWidgetList = true;
	MarkCanvasUpdate(false, false, false);
}
void ULexCanvas::RemoveLexWidget(ULexWidget* InUIItem)
{
	bNeedToGenerateWidgetList = true;
	MarkCanvasUpdate(false, false, false);
}

void ULexCanvas::SetRequireNormalAndTangent(bool Value)
{
	if (bRequireNormalAndTangent != Value)
	{
		bRequireNormalAndTangent = Value;
		MarkCanvasUpdate(false, false, false);
	}
}

bool ULexCanvas::Is2DUITransform(const FTransform& Transform)
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
					DrawCallItem->DirectMeshVisualObject = (UUIDirectMeshRenderable*)InUIItem;
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
					DrawCallItem->DirectMeshVisualObject = (UUIDirectMeshRenderable*)InUIItem;
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
			auto Visual = Item->GetVisual();
			if (!Visual)continue;
			if (!Item->IsVisibleForRender())//if not visible, need to remove the draw-call from draw-call list
			{
				if (Visual->DrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
				{
					if (InUIDrawCallList.Contains(Visual->DrawCall))//if this draw-call already exist (added previously), then remove the object from the draw-call.
					{
						ClearObjectFromDrawCall(Visual->DrawCall, Visual);
					}
				}
				continue;
			}
			switch (Visual->GetVisualType())
			{
			default:
			case ELexVisualType::BatchMesh:
			{
				auto UIBatchMeshRenderableItem = (ULexVisualBatchMesh*)Visual;
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
					if (ItemGeo->bIsFont)
					{
						DrawCallItem->FontTexture = ItemGeo->Texture;
					}
					else
					{
						DrawCallItem->Texture = ItemGeo->Texture;
					}
					if (UIBatchMeshRenderableItem->DrawCall == DrawCallItem)//already exist in this draw-call (added previously)
					{
						DrawCallItem->BatchMeshVisualObjectList.Add(UIBatchMeshRenderableItem);
						//mark sort list
						DrawCallItem->bNeedToSortBatchMeshVisualObjectList = true;
						//update tree
						DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(ItemGeo->BoundsMin2DInCanvasSpace, ItemGeo->BoundsMax2DInCanvasSpace));
						DrawCallItem->VerticesCount += ItemGeo->Vertices.Num();
						DrawCallItem->IndicesCount += ItemGeo->Triangles.Num();
					}
					else//not exist in this draw-call
					{
						auto OldDrawCall = UIBatchMeshRenderableItem->DrawCall;
						if (OldDrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
						{
							ClearObjectFromDrawCall(OldDrawCall, UIBatchMeshRenderableItem);
						}
						//add to this draw-call
						DrawCallItem->BatchMeshVisualObjectList.Add(UIBatchMeshRenderableItem);
						DrawCallItem->BatchMeshTreeNode->Insert(LexUIQuadTree::Rectangle(ItemGeo->BoundsMin2DInCanvasSpace, ItemGeo->BoundsMax2DInCanvasSpace));
						DrawCallItem->VerticesCount += ItemGeo->Vertices.Num();
						DrawCallItem->IndicesCount += ItemGeo->Triangles.Num();
						DrawCallItem->bNeedToUpdateVertex = true;
						UIBatchMeshRenderableItem->DrawCall = DrawCallItem;
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
					auto OldDrawCall = UIBatchMeshRenderableItem->DrawCall;
					if (OldDrawCall.IsValid())//maybe exist in other draw-call, should remove from that draw-call
					{
						if (InUIDrawCallList.Contains(OldDrawCall))//if this draw-call already exist (added previously), then remove the object from the draw-call.
						{
							ClearObjectFromDrawCall(OldDrawCall, UIBatchMeshRenderableItem);
						}
					}
					//make a new draw-call
					PushSingleDrawCall(UIBatchMeshRenderableItem, true, ItemGeo, ELexUIDrawCallType::BatchMesh, is2DUIItem);
					check(UIBatchMeshRenderableItem->DrawCall->VerticesCount < LEXUI_MAX_VERTEX_COUNT);
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
				auto UIDirectMeshRenderableItem = (UUIDirectMeshRenderable*)Visual;
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

void ULexCanvas::SetOverrideViewLocation(bool InOverride, FVector InValue)
{
	bOverrideViewLocation = InOverride;
	OverrideViewLocation = InValue;
}
void ULexCanvas::SetOverrideViewRotation(bool InOverride, FRotator InValue)
{
	bOverrideViewRotation = InOverride;
	OverrideViewRotation = InValue;
}
void ULexCanvas::SetOverrideFovAngle(bool InOverride, float InValue)
{
	bOverrideFovAngle = InOverride;
	OverrideFovAngle = InValue;
}
void ULexCanvas::SetOverrideProjectionMatrix(bool InOverride, FMatrix InValue)
{
	bOverrideProjectionMatrix = InOverride;
	OverrideProjectionMatrix = InValue;
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

void ULexCanvas::MarkFinishRenderFrameRecursive()
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

bool ULexCanvas::UpdateCanvasDrawCallRecursive()
{
	/**
	 * Why use bPrevUIItemIsActive?:
	 * If Canvas is rendering in frame 1, but when in frame 2 the Canvas is disabled(by disable UIItem), then the Canvas will not do draw-call calculation, and the prev existing draw-call mesh is still there and render,
	 * so we check bPrevUIItemIsActive, then we can still do draw-call calculation at this frame, and the prev existing draw-call will be removed.
	 */
	bool bResult = false;
	const bool bNowUIItemIsActive = LexWidget->GetIsUIActiveInHierarchy();
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
			static void CollectRenderWidget(ULexWidget* Widget
				, ULexCanvas* ThisCanvas
				, TArray<TObjectPtr<ULexWidget>>& WidgetCollection)
			{
				if (Widget->GetIsUIActiveInHierarchy())
				{
					// if ((Widget->IsCanvasUIItem() && Widget->GetRenderCanvas() != ThisCanvas)//is child canvas
					// || Widget->GetVisual()//is visual
					// )
					{
						WidgetCollection.Add(Widget);
					}
				}
				for (auto Child : Widget->GetUIChildren())
				{
					CollectRenderWidget(Child, ThisCanvas, WidgetCollection);
				}
			}
		};
		if (bNeedToGenerateWidgetList)
		{
			WidgetList.Reset();
			LOCAL::CollectRenderWidget(this->LexWidget.Get(), this, WidgetList);
		}
		for (const auto& Widget : WidgetList)
		{
			Widget->UpdateLayout();
			Widget->UpdateClip(ClipDataAsTexture, ClipDataList);
			Widget->UpdateVisual();
		}
		for (const auto& ClipData : ClipDataList)
		{
			ClipData->UpdateData();
		}

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
			const auto Width = FMath::Max(LexWidget->GetRenderWidth(), 100.0f);
			const auto Height = FMath::Max(LexWidget->GetRenderHeight(), 100.0f);
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

	//sort render priority
	{
		if (bNeedToSortRenderPriority)
		{
			bNeedToSortRenderPriority = false;
			if (this->IsRootCanvas() || this->GetOverrideSorting())
			{
				this->SortDrawCall();
			}
		}
	}

	return bResult;
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
				DrawCallItem->DirectMeshVisualObject->OnMeshDataReady();
				UIMesh->CreateRenderSectionRenderData(MeshSection.Pin());
				Mutex.Unlock();
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
			if (this->GetActualRenderMode() == ELexRenderMode::WorldSpace)
			{
				return;
			}

			if (!DrawCallItem->DrawCallRenderSection.IsValid())
			{
				Mutex.Lock();
				auto RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::PostProcess);
				auto ChildCanvasSection = (FLexUIPostProcessSection*)RenderSection.Get();
				ChildCanvasSection->PostProcessRenderableObject = DrawCallItem->PostProcessVisualObject;
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
				Mutex.Lock();
				auto RenderSection = UIMesh->CreateRenderSection(ELexUIRenderSectionType::ChildCanvas);
				auto ChildCanvasSection = (FLexUIChildCanvasSection*)RenderSection.Get();
				ChildCanvasSection->ChildCanvasMeshComponent = DrawCallItem->ChildCanvas->GetUIMesh();
				ChildCanvasSection->ChildCanvasMeshComponent->SetParentCanvasMeshComp(this->UIMesh.Get());
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

float ULexCanvas::GetLastRenderTime()const
{
	auto TempRenderMode = GetActualRenderMode();
#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld())//edit mode
	{
		if (bPreviewWithLGUIRenderer)
		{
			if (TempRenderMode == ELexRenderMode::ScreenSpaceOverlay)
				TempRenderMode = ELexRenderMode::WorldSpace_LGUI;
		}
		else
		{
			if (TempRenderMode == ELexRenderMode::ScreenSpaceOverlay)
				TempRenderMode = ELexRenderMode::WorldSpace;
		}
	}
#endif
	if (RenderModeIsLGUIRendererOrUERenderer(TempRenderMode))
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
		if (RenderModeIsLGUIRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (bPreviewWithLGUIRenderer)
			{
				if (!GetWorld()->IsGameWorld())//edit mode
				{
					if (ActualRenderMode == ELexRenderMode::ScreenSpaceOverlay)
						ActualRenderMode = ELexRenderMode::WorldSpace_LGUI;
				}
			}
#endif
			switch (ActualRenderMode)
			{
			case ELexRenderMode::RenderTarget:
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
			case ELexRenderMode::ScreenSpaceOverlay:
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
			case ELexRenderMode::WorldSpace_LGUI:
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
			ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true)->MarkNeedToSortScreenSpacePrimitiveRenderPriority();
			break;
		case ELexRenderMode::RenderTarget:
			GetRenderTargetViewExtension()->MarkNeedToSortScreenSpacePrimitiveRenderPriority();
			break;
		case ELexRenderMode::WorldSpace_LGUI:
			ULGUIManagerWorldSubsystem::GetViewExtension(GetWorld(), true)->MarkNeedToSortWorldSpacePrimitiveRenderPriority();
			break;
		}
	}
}

FName ULexCanvas::LexUI_MainTextureMaterialParameterName = FName(TEXT("LexUI_MainTexture"));
FName ULexCanvas::LexUI_FontTextureMaterialParameterName = FName(TEXT("LexUI_FontTexture"));
FName ULexCanvas::LexUI_ClipDataTexture_MaterialParameterName = FName(TEXT("LexUI_ClipDataTexture"));

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
							if (DrawCallItem->DirectMeshVisualObject.IsValid())
							{
								DrawCallItem->DirectMeshVisualObject->OnMaterialInstanceDynamicCreated((UMaterialInstanceDynamic*)RenderMat.Get());
							}
							for (auto& RenderObjectItem : DrawCallItem->BatchMeshVisualObjectList)
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
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_FontTextureMaterialParameterName, DrawCallItem->FontTexture.Get());
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_ClipDataTexture_MaterialParameterName, ClipDataAsTexture->GetDataTexture());
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
					((UMaterialInstanceDynamic*)RenderMat.Get())->SetTextureParameterValue(LexUI_FontTextureMaterialParameterName, DrawCallItem->Texture.Get());
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

void ULexCanvas::SetRenderTargetResolutionScale(float value)
{
	if (RenderTargetResolutionScale != value)
	{
		RenderTargetResolutionScale = value;
		bAnythingChangedForRenderTarget = true;
	}
}

void ULexCanvas::SetRenderTargetSizeMode(ELexCanvasRenderTargetSizeMode value)
{
	if (RenderTargetSizeMode != value)
	{
		RenderTargetSizeMode = value;
		bAnythingChangedForRenderTarget = true;
	}
}

void ULexCanvas::SetRenderTargetUpdateMode(ELexCanvasRenderTargetUpdateMode value)
{
	if (RenderTargetUpdateMode != value)
	{
		RenderTargetUpdateMode = value;
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
		FLexUIUtils::EditorNotification(errorMsg);
#endif
		return;
	}

	this->SortOrder += InAdditionalValue;
	for (auto ChildCanvas : ChildrenCanvasArray)
	{
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
				FLexUIUtils::EditorNotification(errorMsg);
#endif
				InSortOrder = FMath::Clamp(InSortOrder, (int32)MIN_int16, (int32)MAX_int16);
			}
			this->SortOrder = InSortOrder;
		}

		SortDrawCall();
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
		ChildCanvas->GetMinMaxSortOrderOfHierarchy(OutMin, OutMax);
	}
}


UMaterialInterface* ULexCanvas::GetDefaultMaterial()const
{
	if (!DefaultMaterial)
	{
		DefaultMaterial = LoadObject<UMaterialInterface>(NULL, TEXT("/LGUI/Materials/LexUI_ImageAndFont"));;
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

void ULexCanvas::SetDynamicPixelsPerUnit(float newValue)
{
	if (DynamicPixelsPerUnit != newValue)
	{
		DynamicPixelsPerUnit = newValue;
		for (int i = 0; i < UIDrawCallList.Num(); i++)
		{
			UIDrawCallList[i]->bVertexPositionChanged = true;
		}
		MarkCanvasUpdate(false, true, false);
	}
}
float ULexCanvas::GetActualDynamicPixelsPerUnit()const
{
	if (IsRootCanvas())
	{
		return DynamicPixelsPerUnit;
	}
	else
	{
		if (GetOverrideDynamicPixelsPerUnit())
		{
			return DynamicPixelsPerUnit;
		}
		else
		{
			if (ParentCanvas.IsValid())
			{
				return ParentCanvas->GetActualDynamicPixelsPerUnit();
			}
		}
	}
	return DynamicPixelsPerUnit;
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

void ULexCanvas::SetOverrideSorting(bool value)
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
		return LexWidget->GetRenderWidth() * 0.5f / FMath::Tan(FMath::DegreesToRadians(FOVAngle * 0.5f)) * LexWidget->GetComponentScale().X;
	}
}
FMatrix ULexCanvas::GetViewProjectionMatrix()const
{
	if (bIsViewProjectionMatrixDirty)
	{
		if (!CheckUIItem())
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
	const float FOV = (bOverrideFovAngle ? OverrideFovAngle : FOVAngle) * (float)PI / 360.0f;
	BuildProjectionMatrix(FIntPoint(LexWidget->GetRenderWidth(), LexWidget->GetRenderHeight()), ProjectionType, FOV, FarClipPlane, NearClipPlane, ProjectionMatrix);
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
	FIntPoint ViewportSize = FIntPoint(2, 2);
	if (auto world = this->GetWorld())
	{
#if WITH_EDITOR
		if (!world->IsGameWorld())
		{
			if (CheckUIItem())
			{
				ViewportSize.X = LexWidget->GetRenderWidth();
				ViewportSize.Y = LexWidget->GetRenderHeight();
			}
		}
		else
#endif
		{
			if (RenderMode == ELexRenderMode::ScreenSpaceOverlay)
			{
				if (auto pc = world->GetFirstPlayerController())
				{
					pc->GetViewportSize(ViewportSize.X, ViewportSize.Y);
				}
			}
			else if (RenderMode == ELexRenderMode::RenderTarget && IsValid(RenderTarget))
			{
				ViewportSize.X = RenderTarget->SizeX / RenderTargetResolutionScale;
				ViewportSize.Y = RenderTarget->SizeY / RenderTargetResolutionScale;
			}
		}
	}
	return ViewportSize;
}

void ULexCanvas::SetRenderMode(ELexRenderMode value)
{
	if (RenderMode != value)
	{
		RenderMode = value;
		MarkCanvasUpdate(false, false, false, true);
		CheckRenderMode(true);
	}
}

void ULexCanvas::SetProjectionParameters(TEnumAsByte<ECameraProjectionMode::Type> InProjectionType, float InFovAngle, float InNearClipPlane, float InFarClipPlane)
{
	ProjectionType = InProjectionType;
	FOVAngle = InFovAngle;
	NearClipPlane = InNearClipPlane;
	FarClipPlane = InFarClipPlane;

	bIsViewProjectionMatrixDirty = true;
}

void ULexCanvas::SetRenderTarget(UTextureRenderTarget2D* value)
{
	if (RenderTarget != value)
	{
		RenderTarget = value;
		UpdateRenderTarget(false);
		OnRenderTargetCreatedOrChanged.Broadcast(RenderTarget, false);
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
		if (CheckRootCanvas())
		{
			return RootCanvas->RenderMode;
		}
	}
	return ELexRenderMode::WorldSpace;
}

void ULexCanvas::SetBlendDepth(float value)
{
	if (BlendDepth != value)
	{
		BlendDepth = value;

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

void ULexCanvas::SetDepthFade(int value)
{
	if (DepthFade != value)
	{
		DepthFade = value;

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

void ULexCanvas::SetEnableDepthTest(bool value)
{
	if (bEnableDepthTest != value)
	{
		bEnableDepthTest = value;
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

float ULexCanvas::GetActualRenderTargetResolutionScale()const
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

ELexCanvasRenderTargetSizeMode ULexCanvas::GetActualRenderTargetSizeMode()const
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

ELexCanvasRenderTargetUpdateMode ULexCanvas::GetActualRenderTargetUpdateMode()const
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

void ULexCanvas::RemoveClipData(const TSharedPtr<FLexUIClipData>& InClipData)
{
	ClipDataList.Remove(InClipData);
}
UTexture* ULexCanvas::GetClipDataTexture()const
{
	return ClipDataAsTexture->GetDataTexture();
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
void ULexCanvas::CalculateUIItem2DBounds(ULexVisual* item, const FTransform2D& transform, FVector2D& min, FVector2D& max)
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