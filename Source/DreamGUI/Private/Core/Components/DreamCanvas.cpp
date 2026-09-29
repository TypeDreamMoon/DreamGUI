// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUIWorldContext.h"
#include "Core/DreamGUISettings.h"
#include "Engine/UserInterfaceSettings.h"
#include "DreamGUI.h"
#include "Core/DreamUIGeometry.h"
#include "Utils/DreamUIUtils.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "DreamUIRender/DreamUIRenderer.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "DreamUIRender/DreamUIMaterialProxy.h"
#include "HAL/IConsoleManager.h"
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
#include "Core/DreamUIRuntimeObject.h"
#include "Core/Components/DreamLayout.h"


#define LOCTEXT_NAMESPACE "DreamCanvas"

static TAutoConsoleVariable<int32> CVarDreamUIVerifyPartialPrepare(
	TEXT("r.DreamUI.VerifyPartialPrepare"),
	0,
	TEXT("1: every prepare a canvas makes from its last one -- making again only what the widgets that asked, came or moved ")
	TEXT("draw -- is checked against a prepare of every widget, and a difference is an ensure. For tests: it costs a full ")
	TEXT("prepare each time."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarDreamUIMaterialWrappers(
	TEXT("r.DreamUI.MaterialWrappers"),
	1,
	TEXT("1: a canvas draws its own materials through render-thread proxies that answer DreamGUI's parameters in the ")
	TEXT("material's place, a material instance given to it included. 0: through a material instance per draw call, ")
	TEXT("pooled per material, whose parameters it sets."),
	ECVF_Default);

namespace DreamCanvasLocal
{
	/**
	 * What a canvas makes while it runs -- its mesh, its material instances, its data textures, its render
	 * target -- carries DreamUI::RuntimeObjectFlags: never saved, never cloned by a duplication (play in
	 * editor, Duplicate), never written out by the level editor's Copy.
	 */
	UMaterialInstanceDynamic* CreateRuntimeMaterial(UMaterialInterface* InParent, UObject* InOuter)
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(InParent, InOuter);
		if (Material != nullptr)
		{
			Material->SetFlags(DreamUI::RuntimeObjectFlags);
		}
		return Material;
	}
}

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
	DREAMUI_STAGE_SCOPE(CanvasUpdate);
	CheckRootCanvas();
	if (this == RootCanvas)
	{
		if (RenderModeIsDreamRendererOrUERenderer(CurrentRenderMode))
		{
			auto ActualRenderMode = GetActualRenderMode();
#if WITH_EDITOR
			if (!DreamUI::IsGameWorld(this))//edit mode
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
						ViewExtension->SetScreenSpaceRootCanvas(this, this);
						bHasAddToDreamScreenSpaceRenderer = true;
					}
				}
			}
			break;
			case EDreamRenderMode::RenderTarget:
			{
				if (!bHasAddToDreamScreenSpaceRenderer)
				{
					GetRenderTargetViewExtension()->SetScreenSpaceRootCanvas(this, this);
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

	if (RenderTarget == nullptr && AutoRenderTarget == nullptr)
	{
		// Made here and held apart from the assigned one, so never saved, duplicated or copied: a copy of
		// this canvas makes its own. A render target assigned from outside keeps whatever flags its owner
		// gave it.
		AutoRenderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, DreamUI::RuntimeObjectFlags);
		AutoRenderTarget->AddressX = TextureAddress::TA_Clamp;
		AutoRenderTarget->AddressY = TextureAddress::TA_Clamp;
		AutoRenderTarget->ClearColor = FLinearColor::Transparent;
		AutoRenderTarget->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
		if (CallEvent)
		{
			OnRenderTargetChanged.Broadcast(AutoRenderTarget);
		}
	}
	else
	{
		UTextureRenderTarget2D* Target = GetRenderTarget();
		switch (RenderTargetSizeMode)
		{
		case EDreamCanvasRenderTargetSizeMode::None:
		case EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget:
			DesiredRenderTargetSize.X = Target->SizeX;
			DesiredRenderTargetSize.Y = Target->SizeY;
			break;
		case EDreamCanvasRenderTargetSizeMode::RenderTargetFitToCanvas:
			break;
		}
		if (Target->SizeX != DesiredRenderTargetSize.X || Target->SizeY != DesiredRenderTargetSize.Y)
		{
			Target->ClearColor = FLinearColor::Transparent;
			Target->InitCustomFormat(DesiredRenderTargetSize.X, DesiredRenderTargetSize.Y, EPixelFormat::PF_B8G8R8A8, false);
			Target->UpdateResourceImmediate();
#if WITH_EDITOR
			DreamUI::ModifyIfKeptByUndo(*Target);
#endif
			if (CallEvent)
			{
				OnRenderTargetChanged.Broadcast(Target);
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
		if (!DreamUI::IsGameWorld(this))//edit mode
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
			if (UTextureRenderTarget2D* Target = GetRenderTarget(); IsValid(Target))
			{
#if WITH_EDITOR
				if (!DreamUI::IsGameWorld(this))
				{
					if (!Target->GameThread_GetRenderTargetResource())
					{
						Target->InitCustomFormat(Target->SizeX, Target->SizeY, EPixelFormat::PF_B8G8R8A8, false);
					}
				}
#endif
				if (RenderTargetViewExtension.IsValid())
				{
					if (FDreamUIRenderer::IsRenderTargetDrawerEnabled())
					{
						// Drawn once this frame's sections have gone to the render thread: DrawRenderTargetIfRequested.
						bRenderTargetDrawRequested = true;
					}
					else
					{
						RenderTargetViewExtension->UpdateRenderTargetRenderer(Target, RenderTargetClearColor);
					}
				}
			}
		}
	}
}

void UDreamCanvas::DrawRenderTargetIfRequested()
{
	if (!bRenderTargetDrawRequested)
	{
		return;
	}
	bRenderTargetDrawRequested = false;
	UTextureRenderTarget2D* Target = GetRenderTarget();
	if (RenderTargetViewExtension.IsValid() && IsValid(Target))
	{
		RenderTargetViewExtension->DrawRenderTarget_GameThread(Target, RenderTargetClearColor);
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
		ClipDataAsTexture = NewObject<UDreamUIDataAsTexture>(this, UDreamUIDataAsTexture::StaticClass(), NAME_None, DreamUI::RuntimeObjectFlags);
		ClipDataAsTexture->Init(FDreamUIClipData::BlockSizeInBytes, EDreamUIDataAsTexturePixelFormat::R32G32B32A32, 128);
		ClipDataAsTexture->RegisterBuffer();//register a zero position as a placeholder for not clipping type.
	}

	RegisterCanvasScaler();
}
void UDreamCanvas::OnUnregister()
{
	// Whatever shows this canvas's target lets go of it first. A render-target surface's material holds it as
	// a texture parameter; when the collector takes the target with this canvas -- a destroyed widget's parts
	// go with it -- it nulls that parameter on the game thread only, and the material's render-thread copy
	// goes on pointing at a freed texture until the next uniform-expression update reads it.
	if (GetRenderTarget() != nullptr)
	{
		OnRenderTargetChanged.Broadcast(nullptr);
	}
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
	// Out of the parent canvas's mesh first. A canvas clears its draw calls when it is about to draw
	// some other way -- sorting itself, rendering to its own target -- and a mesh still hooked into the
	// parent's as a child section goes on being drawn by the parent as well.
	//
	// The parent's mesh as it stands, not GetUIMesh(), which makes one when there is none: a child
	// unregistering after its parent -- a tree coming down with its world -- made the parent a new mesh
	// and registered it in a world already cleaned up, which the engine refuses and logs as an error.
	if (IsValid(UIMesh) && ParentCanvas.IsValid())
	{
		UIMesh->ClearParentCanvasMeshComp(ParentCanvas->UIMesh.Get());
	}
	if (IsValid(UIMesh))
	{
		UIMesh->ClearRenderData();
		bUIMeshNeedToSetInitialParameters = true;
	}
	PooledDefaultMaterialList.Empty();
	MapSrcMatToDynamicMat.Empty();
	// The parameter cache keys the same MIDs the two pools above just dropped; without this line it
	// both leaked entries and kept those MIDs rooted forever -- the pool died, the cache did not.
	MapMatToParamCache.Empty();
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
			RenderTargetViewExtension->ClearScreenSpaceRootCanvas(this);
		}
		else//if not RenderTarget mode, then should be ScreenSpaceOverlay
		{
			auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(GetWorld(), false);
			if (ViewExtension.IsValid())
			{
				//only this canvas: the world's view extension is shared with every other root canvas
				ViewExtension->ClearScreenSpaceRootCanvas(this);
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
	//a destroyed child canvas leaves a stale entry behind until the array is rebuilt, and recursing into
	//it would dereference null on the very next line
	if (!IsValid(Target))return;
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
			DreamWidget->MarkRenderModeChangeRecursive(this);
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
	bUpdateEveryWidget = true;
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
	// The clips that changed go up together, in one command and as few texture updates as their rows allow, rather
	// than a command each.
	const bool bBatch = IsValid(ClipDataAsTexture) && !ClipDataAsTexture->GetIsBatchUpdateMode();
	if (bBatch)
	{
		ClipDataAsTexture->PrepareForBatchUpdate();
	}
	for (const auto& ClipData : ClipDataList)
	{
		//removing a widget's clip leaves the slot behind until the list is compacted
		if (!ClipData.IsValid())continue;
		ClipData->UpdateData();
	}
	if (bBatch)
	{
		ClipDataAsTexture->Flush();
	}
}

void UDreamCanvas::MarkCanvasUpdate(bool bRebuildDrawCall)
{
	this->bCanTickUpdate = true;
	// No widget named: which widgets need looking at is unknown, so every one of them is.
	this->bUpdateEveryWidget = true;
	if (bRebuildDrawCall)
	{
		this->bShouldRebuildDrawCall = true;
	}
}

void UDreamCanvas::MarkWidgetUpdate(UDreamWidget* InWidget, bool bRebuildDrawCall)
{
	this->bCanTickUpdate = true;
	if (bRebuildDrawCall)
	{
		this->bShouldRebuildDrawCall = true;
	}
	if (InWidget == nullptr)
	{
		this->bUpdateEveryWidget = true;
	}
	if (this->bUpdateEveryWidget)
	{
		return;
	}
	// Past half the list, looking at every widget costs no more than sorting the ones that asked.
	if (WidgetsToUpdate.Num() >= FMath::Max(32, WidgetList.Num() / 2))
	{
		this->bUpdateEveryWidget = true;
		WidgetsToUpdate.Reset();
		return;
	}
	WidgetsToUpdate.Add(InWidget);
}

void UDreamCanvas::EnsureWidgetListIndex()
{
	if (bWidgetListIndexValid)
	{
		return;
	}
	bWidgetListIndexValid = true;
	WidgetListIndex.Reset();
	WidgetListIndex.Reserve(WidgetList.Num());
	for (int32 Index = 0; Index < WidgetList.Num(); ++Index)
	{
		WidgetListIndex.Add(TObjectKey<UDreamWidget>(WidgetList[Index]), Index);
	}
}

bool UDreamCanvas::GatherWidgetsToUpdateInListOrder(const TArray<TWeakObjectPtr<UDreamWidget>>& InAsking, TArray<UDreamWidget*>& OutWidgets)
{
	EnsureWidgetListIndex();
	TArray<TPair<int32, UDreamWidget*>, TInlineAllocator<64>> Ordered;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : InAsking)
	{
		UDreamWidget* Widget = WeakWidget.Get();
		const int32* Index = IsValid(Widget) ? WidgetListIndex.Find(TObjectKey<UDreamWidget>(Widget)) : nullptr;
		if (Index == nullptr)
		{
			// Gone since it asked, or no longer this canvas's: what it drew goes with the prepare that merges the list
			// made again without it. Anything else not in the list means the list is behind, which only a full walk and
			// prepare see.
			if (bWidgetListChangedSincePrepare && (!IsValid(Widget) || Widget->GetRenderCanvas() != this))
			{
				continue;
			}
			return false;
		}
		Ordered.Emplace(*Index, Widget);
	}
	Ordered.Sort([](const TPair<int32, UDreamWidget*>& A, const TPair<int32, UDreamWidget*>& B) { return A.Key < B.Key; });
	OutWidgets.Reserve(Ordered.Num());
	for (int32 Index = 0; Index < Ordered.Num(); ++Index)
	{
		if (Index == 0 || Ordered[Index].Key != Ordered[Index - 1].Key)
		{
			OutWidgets.Add(Ordered[Index].Value);
		}
	}
	return true;
}

bool UDreamCanvas::MergePreparedDataCache()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_MergePreparedData);
	EnsureWidgetListIndex();
	if (!bWidgetListChangedSincePrepare)
	{
		/**
		 * The list is as it was, so each widget looked at keeps its place: an entry it had and still has is made again
		 * where it is, and nothing else moves. It is found by the widget's place in the list, the order the entries are
		 * in. A widget gaining an entry or losing one moves the others, which the merge below does -- and it makes again
		 * whatever was made here, so giving up halfway is no harm. Walking the whole cache for a few widgets that asked
		 * is what a canvas with a few widgets moving paid for, every frame, before this.
		 */
		bool bInPlace = true;
		TArray<FDreamUIRenderData> Made;
		for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : WidgetsToPrepare)
		{
			UDreamWidget* Widget = WeakWidget.Get();
			const int32* Place = IsValid(Widget) ? WidgetListIndex.Find(TObjectKey<UDreamWidget>(Widget)) : nullptr;
			if (Place == nullptr)
			{
				bInPlace = false;
				break;
			}
			int32 Low = 0;
			int32 High = PreparedDataCache.Num();
			while (Low < High)
			{
				const int32 Middle = (Low + High) / 2;
				const int32* MiddlePlace = WidgetListIndex.Find(PreparedDataCache[Middle].Widget);
				if (MiddlePlace == nullptr)
				{
					bInPlace = false;
					break;
				}
				if (*MiddlePlace < *Place)
				{
					Low = Middle + 1;
				}
				else
				{
					High = Middle;
				}
			}
			if (!bInPlace)
			{
				break;
			}
			const bool bHadEntry = Low < PreparedDataCache.Num() && PreparedDataCache[Low].Widget == TObjectKey<UDreamWidget>(Widget);
			Made.Reset();
			AppendRenderDataOf(Widget, Made);
			if (bHadEntry != (Made.Num() > 0))
			{
				bInPlace = false;
				break;
			}
			if (bHadEntry)
			{
				PreparedDataCache[Low] = MoveTemp(Made[0]);
			}
		}
		if (bInPlace)
		{
			return true;
		}
	}
	// The widgets looked at since the last prepare, in list order, each once: what a prepare makes of them now is made
	// again. One gone, or no longer this canvas's, has no place in the list, and nothing is made for it.
	TArray<TPair<int32, UDreamWidget*>, TInlineAllocator<64>> Looked;
	TSet<TObjectKey<UDreamWidget>> LookedKeys;
	for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : WidgetsToPrepare)
	{
		UDreamWidget* Widget = WeakWidget.Get();
		if (!IsValid(Widget))
		{
			continue;
		}
		const TObjectKey<UDreamWidget> Key(Widget);
		const int32* Index = WidgetListIndex.Find(Key);
		if (Index == nullptr)
		{
			if (Widget->GetRenderCanvas() == this)
			{
				return false;//this canvas's and not in its list: the list is behind
			}
			continue;
		}
		bool bAlreadyLooked = false;
		LookedKeys.Add(Key, &bAlreadyLooked);
		if (!bAlreadyLooked)
		{
			Looked.Emplace(*Index, Widget);
		}
	}
	Looked.Sort([](const TPair<int32, UDreamWidget*>& A, const TPair<int32, UDreamWidget*>& B) { return A.Key < B.Key; });

	// Every other widget's entry stands as the last prepare made it, in its place: the widgets that stayed keep their order
	// in a list made again, and one that moved was looked at. An entry whose widget is no longer in the list goes. Entries
	// are moved out of the cache as they are taken; a merge that gives up leaves the cache to a full prepare.
	TArray<FDreamUIRenderData> Merged;
	Merged.Reserve(PreparedDataCache.Num() + Looked.Num());
	int32 NextLooked = 0;
	int32 LastPlace = INDEX_NONE;
	for (FDreamUIRenderData& Entry : PreparedDataCache)
	{
		if (LookedKeys.Contains(Entry.Widget))
		{
			continue;
		}
		const int32* Place = WidgetListIndex.Find(Entry.Widget);
		if (Place == nullptr)
		{
			continue;
		}
		if (*Place <= LastPlace)
		{
			return false;//out of the list's order: only a full prepare says what the order is
		}
		LastPlace = *Place;
		for (; NextLooked < Looked.Num() && Looked[NextLooked].Key < *Place; ++NextLooked)
		{
			AppendRenderDataOf(Looked[NextLooked].Value, Merged);
		}
		Merged.Add(MoveTemp(Entry));
	}
	for (; NextLooked < Looked.Num(); ++NextLooked)
	{
		AppendRenderDataOf(Looked[NextLooked].Value, Merged);
	}
	PreparedDataCache = MoveTemp(Merged);
	return true;
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
		// What the setter does, for the same reason: the canvas stops, or starts, being drawn as part
		// of its root.
		ClearDrawCall();
		CheckRootCanvas(true);
		if (bForceRenderToTarget)
		{
			RenderMode = EDreamRenderMode::RenderTarget;
			OnRenderTargetChanged.Broadcast(GetRenderTarget());
		}
		else
		{
			OnRenderTargetChanged.Broadcast(nullptr);
		}
	}
	else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamCanvas, bOverrideSorting))
	{
		ClearDrawCall();
	}

	//The Details panel writes ProjectionType/FieldOfView/the clip planes/the overrides straight into the
	//property, bypassing the setters that would invalidate the cached view-projection matrix, and
	//MarkAllDirtyRecursive above does not reach it either. Without this an author who changes the
	//projection in the panel gets a picture on the new matrix and hit testing on the old one.
	bIsViewProjectionMatrixDirty = true;
	OnViewportParameterChanged();
}
void UDreamCanvas::PostLoad()
{
	Super::PostLoad();
	// An older build kept the render target it made for itself in RenderTarget, the author's property, and
	// saved it there. One outered to this canvas is that, never an author's asset: it goes, and the canvas
	// makes its own again, in AutoRenderTarget.
	if (RenderTarget != nullptr && RenderTarget->GetOuter() == this)
	{
		RenderTarget = nullptr;
	}
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
	DreamUI::DeferToLaterTick([WeakThis = MakeWeakObjectPtr(this)]() {
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
	/**
	 * A visual leaving this canvas invalidates the draw-call list itself, not just the vertex data:
	 * the geometry it contributed has to come out of whatever draw-call it was batched into, and the
	 * draw-call may no longer exist at all. Marking a plain tick update only re-runs the cheap refresh
	 * path, which copies vertices into the layout built for the visual that is going away. In the
	 * editor this was invisible because PostReinitProperties marks everything dirty; at runtime
	 * (Blueprint swapping a visual type, or an object pool reusing a widget) the old visual kept
	 * drawing.
	 *
	 * The visual's widget asks, when it stays in this canvas: what it draws now, if anything, is made again. One that
	 * left this canvas goes from the list made again without it, and its entry with it. A visual with no widget leaves
	 * nothing to say whose entry goes, and every widget is looked at.
	 */
	UDreamWidget* VisualWidget = InOldVisual != nullptr ? InOldVisual->GetWidget() : nullptr;
	if (VisualWidget == nullptr)
	{
		MarkCanvasUpdate(true);
	}
	else if (VisualWidget->GetRenderCanvas() == this)
	{
		MarkWidgetUpdate(VisualWidget, true);
	}
	else
	{
		MarkWidgetCameOrWent(VisualWidget);
	}
}

void UDreamCanvas::RegisterVisual(UDreamVisual* InVisual)
{
	if (!IsValid(InVisual))return;
	/**
	 * Registration has to be idempotent: a widget's visual is handed to this canvas once when the parent's
	 * OnRegister walks into an unregistered child, and again when that child runs its own OnRegister. A
	 * second RegisterBuffer() would hand out a second row of the property-data texture and overwrite the
	 * recorded row number, so the first row is never returned to the free list -- one leaked row per
	 * redundant registration. Moving a visual to another canvas always goes through UnregisterVisual first,
	 * which clears the position, so this only short-circuits the genuinely redundant case.
	 */
	const bool bAlreadyRegisteredHere = InVisual->IsRegisteredToCanvas() && VisualList.Contains(InVisual);
	VisualList.AddUnique(InVisual);
	CheckWidgetPropertyData();
	if (bAlreadyRegisteredHere)return;
	//a visual arriving needs a draw-call of its own (or a place in someone else's), which only the
	//rebuild pass can work out -- the refresh path just re-copies vertices into the existing layout.
	//Its widget asks for that; the canvas's other widgets stay as they are.
	UDreamWidget* VisualWidget = InVisual->GetWidget();
	MarkWidgetUpdate(VisualWidget != nullptr && VisualWidget->GetRenderCanvas() == this ? VisualWidget : nullptr, true);
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
	MarkWidgetCameOrWent(InWidget);
}
void UDreamCanvas::RemoveDreamWidget(UDreamWidget* InWidget)
{
	MarkWidgetCameOrWent(InWidget);
}

void UDreamCanvas::MarkWidgetCameOrWent(UDreamWidget* InWidget)
{
	bNeedToGenerateWidgetList = true;
	if (IsValid(InWidget) && InWidget->GetRenderCanvas() == this)
	{
		MarkWidgetUpdate(InWidget, true);
	}
	else
	{
		bCanTickUpdate = true;
		bShouldRebuildDrawCall = true;
	}
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

/**
 * These four are the other half of what GetViewProjectionMatrix reads: GetViewLocation and GetViewRotator
 * return their override verbatim, and GetProjectionMatrix returns OverrideProjectionMatrix outright or
 * feeds OverrideFovAngle into the matrix it builds. So each has to invalidate the cached matrix for the
 * same reason the projection setters do -- the renderer recomputes every frame, but the raycaster reads
 * the cache, so a stale cache means hit testing answers for a view that is no longer on screen. Marked
 * unconditionally rather than on a value change, matching SetProjectionParameters: a redundant mark costs
 * one recalculation, and the flag and the value have to be considered together anyway.
 */
void UDreamCanvas::SetOverrideViewLocation(bool Override, FVector Value)
{
	bOverrideViewLocation = Override;
	OverrideViewLocation = Value;
	bIsViewProjectionMatrixDirty = true;
}
void UDreamCanvas::SetOverrideViewRotation(bool Override, FRotator Value)
{
	bOverrideViewRotation = Override;
	OverrideViewRotation = Value;
	bIsViewProjectionMatrixDirty = true;
}
void UDreamCanvas::SetOverrideFovAngle(bool Override, float Value)
{
	bOverrideFovAngle = Override;
	OverrideFovAngle = Value;
	bIsViewProjectionMatrixDirty = true;
}
void UDreamCanvas::SetOverrideProjectionMatrix(bool Override, FMatrix Value)
{
	bOverrideProjectionMatrix = Override;
	OverrideProjectionMatrix = Value;
	bIsViewProjectionMatrixDirty = true;
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
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_PrepareDrawCallBatchingData);
	OutRenderDataArray.Reset();
	/**
	 * Drain the vertex-transform work once, here, before reading any geometry: CopyDataForPrepare
	 * below must not read a geometry that is still being written. This used to be a per-geometry
	 * `while (bIsCalculating) Sleep(1ms)` inside the loop -- one game-thread sleep per unfinished
	 * element, each rounded up to the scheduler's granularity. One wait covers the whole list, and it
	 * can run the outstanding transforms on this thread rather than waiting to be scheduled.
	 */
	if (TransformVerticesAsyncFunctionRunnable.IsValid())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_WaitForVertexTransforms);
		TransformVerticesAsyncFunctionRunnable->WaitForAllFunctions();
	}
	// When only some widgets were looked at since the last prepare -- they asked, came or moved -- the last prepare stands
	// but for them. Past half the list, walking it all costs no more.
	if (!bPrepareEveryWidget && bPreparedDataCacheValid && WidgetsToPrepare.Num() < FMath::Max(32, WidgetList.Num() / 2)
		&& MergePreparedDataCache())
	{
		OutRenderDataArray = PreparedDataCache;
		WidgetsToPrepare.Reset();
		bWidgetListChangedSincePrepare = false;
		if (CVarDreamUIVerifyPartialPrepare.GetValueOnGameThread() != 0)
		{
			VerifyPartialPrepare(OutRenderDataArray);
		}
		return;
	}
	for (const TObjectPtr<UDreamWidget>& Widget : WidgetList)
	{
		AppendRenderDataOf(Widget.Get(), OutRenderDataArray);
	}
	// Kept for the prepares to come: see MergePreparedDataCache.
	PreparedDataCache = OutRenderDataArray;
	bPreparedDataCacheValid = true;
	bPrepareEveryWidget = false;
	bWidgetListChangedSincePrepare = false;
	WidgetsToPrepare.Reset();
}

void UDreamCanvas::AppendRenderDataOf(UDreamWidget* Widget, TArray<FDreamUIRenderData>& OutRenderDataArray)
{
	if (!IsValid(Widget))return;//a widget collected earlier can be destroyed before the list is regenerated
	if (Widget->IsCanvasWidget() && Widget->GetRenderCanvas() != this)//is child canvas
	{
		auto ChildCanvas = Widget->GetRenderCanvas();
		if (ChildCanvas == nullptr)return;//normally this won't be nullptr, but when redo in editor this breaks
		if (ChildCanvas->bForceRenderToTarget)return;//skip this type
		if (ChildCanvas->GetOverrideSorting())return;//override sorting means render by itself, then no need to use it as child-canvas
		auto RenderData = FDreamUIRenderData(EDreamUIDrawCallType::ChildCanvas);
		RenderData.ChildCanvas = ChildCanvas;
		RenderData.Widget = Widget;
		OutRenderDataArray.Add(MoveTemp(RenderData));
		return;
	}
	auto Visual = Widget->GetVisual();
	if (!Visual)return;
	if (!Widget->GetRenderVisibleInHierarchy())//if not visible, need to remove the draw-call from draw-call list
	{
		return;
	}
	switch (Visual->GetVisualType())
	{
	default:
	case EDreamVisualType::BatchMesh:
		{
			auto DreamVisualBatchMesh = static_cast<UDreamVisualBatchMesh*>(Visual);
			auto ItemGeo = DreamVisualBatchMesh->GetGeometry();
			if (ItemGeo == nullptr)return;
			while (ItemGeo->bIsCalculating)
			{
				//should not be reached: the transform queue was drained before the prepare began.
				//Kept as the correctness backstop -- CopyDataForPrepare must never read a
				//half-written geometry -- but yielding rather than sleeping a millisecond.
				FPlatformProcess::Sleep(0.0f);
			}
			if (ItemGeo->Vertices.Num() == 0)return;
			/**
			 * One element that does not fit in an index buffer cannot be drawn, but it used to
			 * disappear without a word -- a long text block or a big tiled image simply stopped
			 * rendering, with nothing in the log to connect it to a vertex budget. Say so.
			 *
			 * The comparison is >=, not >: PushSingleDrawCall asserts VerticesCount is strictly
			 * below the budget, so a geometry sitting exactly on it passed this gate and then
			 * tripped the check one step later.
			 */
			if (ItemGeo->Vertices.Num() >= LEXUI_MAX_VERTEX_COUNT)
			{
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d Widget '%s' has %d vertices, at or past the %d a single draw-call can index, so it cannot be drawn. Split it into several widgets, or build with a 32-bit index buffer (LEXUI_USE_32BIT_INDEXBUFFER in DreamGUI.Build.cs).")
					, ANSI_TO_TCHAR(__FUNCTION__), __LINE__
					, *Widget->GetDisplayName(), ItemGeo->Vertices.Num(), LEXUI_MAX_VERTEX_COUNT);
				return;
			}
			auto RenderData = FDreamUIRenderData(EDreamUIDrawCallType::BatchMesh);
			//the visual's copy, made again only when the geometry changed since the last one
			RenderData.BatchMeshGeometry = DreamVisualBatchMesh->GetGeometryForBatching();
			RenderData.BatchMeshVisualObject = DreamVisualBatchMesh;
			RenderData.Widget = Widget;
			OutRenderDataArray.Add(MoveTemp(RenderData));
		}
		break;
	case EDreamVisualType::PostProcess:
		{
			auto DreamVisualPostProcess = static_cast<UDreamVisualPostProcess*>(Visual);
			if (!DreamVisualPostProcess->HaveValidData())return;
			auto RenderData = FDreamUIRenderData(EDreamUIDrawCallType::PostProcess);
			RenderData.PostProcessVisualObject = DreamVisualPostProcess;
			//read the bounds here, on the game thread: the batching pass that needs them runs on a
			//worker thread, where dereferencing the visual races with garbage collection
			if (auto PostProcessGeo = DreamVisualPostProcess->GetGeometry())
			{
				RenderData.PostProcessBoundsMin2DInCanvasSpace = PostProcessGeo->BoundsMin2DInCanvasSpace;
				RenderData.PostProcessBoundsMax2DInCanvasSpace = PostProcessGeo->BoundsMax2DInCanvasSpace;
			}
			RenderData.Widget = Widget;
			OutRenderDataArray.Add(MoveTemp(RenderData));
		}
		break;
	case EDreamVisualType::DirectMesh:
		{
			auto DreamVisualDirectMesh = static_cast<UDreamVisualDirectMesh*>(Visual);
			if (!DreamVisualDirectMesh->HaveValidData())return;
			auto RenderData = FDreamUIRenderData(EDreamUIDrawCallType::DirectMesh);
			RenderData.DirectMeshVisualObject = DreamVisualDirectMesh;
			RenderData.Widget = Widget;
			OutRenderDataArray.Add(MoveTemp(RenderData));
		}
		break;
	}
}

void UDreamCanvas::VerifyPartialPrepare(const TArray<FDreamUIRenderData>& InPrepared)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_VerifyPartialPrepare);
	TArray<FDreamUIRenderData> Full;
	for (const TObjectPtr<UDreamWidget>& Widget : WidgetList)
	{
		AppendRenderDataOf(Widget.Get(), Full);
	}
	auto Same = [](const FDreamUIRenderData& A, const FDreamUIRenderData& B)
	{
		return A.Type == B.Type
			&& A.Widget == B.Widget
			&& A.BatchMeshGeometry == B.BatchMeshGeometry
			&& A.BatchMeshVisualObject == B.BatchMeshVisualObject
			&& A.PostProcessVisualObject == B.PostProcessVisualObject
			&& A.PostProcessBoundsMin2DInCanvasSpace == B.PostProcessBoundsMin2DInCanvasSpace
			&& A.PostProcessBoundsMax2DInCanvasSpace == B.PostProcessBoundsMax2DInCanvasSpace
			&& A.DirectMeshVisualObject == B.DirectMeshVisualObject
			&& A.ChildCanvas == B.ChildCanvas;
	};
	int32 First = INDEX_NONE;
	const int32 Common = FMath::Min(Full.Num(), InPrepared.Num());
	for (int32 Index = 0; Index < Common; ++Index)
	{
		if (!Same(Full[Index], InPrepared[Index]))
		{
			First = Index;
			break;
		}
	}
	if (First == INDEX_NONE && Full.Num() != InPrepared.Num())
	{
		First = Common;
	}
	ensureMsgf(First == INDEX_NONE, TEXT("%s: the prepare made from the last one differs from a prepare of every widget, first at entry %d (%d entries against %d)."),
		*GetPathName(), First, InPrepared.Num(), Full.Num());
}

DECLARE_CYCLE_STAT(TEXT("Canvas BatchDrawCallAsync"), STAT_BatchDrawCall, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas BatchDrawCall/OverlapTest"), STAT_OverlapTest, STATGROUP_DreamGUI);

void UDreamCanvas::BatchDrawCallAsync(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop,
	const TArray<FDreamUIRenderData>& InRenderDataArray, TArray<FDreamUIDrawCall>& InOutUIDrawCallList,
	bool bCullElementsOutsideCanvasRect)
{
	// A copy the batching may use up: the data a caller hands over as const is the caller's to keep.
	TArray<FDreamUIRenderData> RenderDataArray = InRenderDataArray;
	BatchDrawCallAsync(InCanvasLeftBottom, InCanvasRightTop, MoveTemp(RenderDataArray), InOutUIDrawCallList, bCullElementsOutsideCanvasRect);
}

void UDreamCanvas::BatchDrawCallAsync(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop,
	TArray<FDreamUIRenderData>&& InRenderDataArray, TArray<FDreamUIDrawCall>& InOutUIDrawCallList,
	bool bCullElementsOutsideCanvasRect, const TArray<TArray<TSharedPtr<const FDreamUIGeometry>>>* InGeometryListsOnSections)
{
	SCOPE_CYCLE_COUNTER(STAT_BatchDrawCall);
	DREAMUI_STAGE_SCOPE(Batching);

	InOutUIDrawCallList.Reset();

	auto CanvasRect = DreamUIQuadTree::Rectangle(InCanvasLeftBottom, InCanvasRightTop);

	/**
	 * Element-level culling. An element whose canvas-space bounds do not touch the canvas rect cannot
	 * put a pixel on screen, so it is left out of the draw-call list entirely -- it costs no vertices,
	 * no index range, and (more importantly) it no longer sits between two elements that would
	 * otherwise batch together. Long scrolling lists are the case this is for: without it, every row
	 * ever spawned is still assembled, transformed and uploaded.
	 *
	 * Bounds are inclusive-of-touching on purpose: an element exactly on the boundary is kept, because
	 * a half-pixel of antialiasing or a rounding difference on the GPU side would show it. Only
	 * elements that are wholly past an edge are dropped.
	 *
	 * Off unless the caller says otherwise, and the caller only says so when THIS canvas's rect is the
	 * surface being drawn (a root canvas, or one rendering to its own target). A plain child canvas
	 * draws into its parent's surface and its own rect says nothing about what is visible -- a canvas
	 * is not a clipper, and children are free to sit outside it.
	 *
	 * Post-process and direct-mesh elements are never culled: a post process reads what is behind it
	 * and a direct mesh's 2D bounds are not meaningful (particles, static meshes), which is the same
	 * reason OverlapWithOtherDrawCall answers "always overlaps" for them.
	 */
	auto IsOutsideCanvas = [&](const FVector2D& InBoundsMin, const FVector2D& InBoundsMax) {
		if (!bCullElementsOutsideCanvasRect)return false;
		return InBoundsMax.X < InCanvasLeftBottom.X
			|| InBoundsMin.X > InCanvasRightTop.X
			|| InBoundsMax.Y < InCanvasLeftBottom.Y
			|| InBoundsMin.Y > InCanvasRightTop.Y;
	};

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
				//check bounds overlap. This whole function runs on the draw-call batching thread, so the
				//bounds are the copies taken on the game thread in PrepareDrawCallBatchingData -- asking the
				//visual itself here would dereference a weak pointer while GC may be collecting it.
				if (IntersectBounds(InGeo.BoundsMin2DInCanvasSpace, InGeo.BoundsMax2DInCanvasSpace, OtherDrawCallItem.PostProcessBoundsMin2DInCanvasSpace, OtherDrawCallItem.PostProcessBoundsMax2DInCanvasSpace))
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
			/**
			 * 3d UI can only batch into last draw-call: its depth ordering against anything further back is
			 * not something the 2D bounds can answer, so only the neighbour it is already adjacent to in sort
			 * order is safe. That is also why this path can skip FitInDrawCallMinIndex without crossing a
			 * barrier: a post-process/direct-mesh/child-canvas draw-call raises that floor to Num() as it is
			 * pushed, so whenever the floor is above the last index the last draw-call IS the barrier, and
			 * CanConsumeUIGeometryForBatchMesh rejects every draw-call that is not a BatchMesh.
			 */
			const auto& LastDrawCall = InOutUIDrawCallList[LastDrawCallIndex];
			if (LastDrawCall.CanConsumeUIGeometryForBatchMesh(InGeo))
			{
				OutDrawCallIndexToFitin = LastDrawCallIndex;
				return true;
			}
			return false;
		}
		//the deepest draw-call walked so far that can take this item: the walk goes from tail to head, so the last one
		//found is the one to use. One index, where a list of every candidate used to be allocated per item.
		int32 DeepestFit = INDEX_NONE;
		for (int i = LastDrawCallIndex; i >= FitInDrawCallMinIndex; i--)//from tail to head
		{
			const auto& OtherDrawCall = InOutUIDrawCallList[i];
			if (!OtherDrawCall.bIs2DSpace)//draw-call is 3d, can't batch into it and can't look past it
			{
				/**
				 * A 3D draw-call is a floor, not a dead end. Every draw-call already walked between here and
				 * the tail was proven not to overlap this geometry (that is the only way the loop gets this
				 * deep), so a candidate collected above the floor is still a legal place to put the item and
				 * the search never crosses the 3D draw-call -- exactly the reasoning the overlap bail-out
				 * below uses. Returning false outright would open a new draw-call for nothing. Before
				 * CopyDataForPrepare carried TransformRelativeToCanvas, bIs2DSpace was always true and this
				 * whole branch was unreachable.
				 */
				if (DeepestFit != INDEX_NONE)
				{
					OutDrawCallIndexToFitin = DeepestFit;
					return true;
				}
				return false;
			}

			if (!OtherDrawCall.CanConsumeUIGeometryForBatchMesh(InGeo))//can't fit in this draw-call, should check overlap
			{
				if (OverlapWithOtherDrawCall(InGeo, OtherDrawCall))//overlap with other draw-call, can't batch
				{
					if (DeepestFit != INDEX_NONE)
					{
						OutDrawCallIndexToFitin = DeepestFit;
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
			DeepestFit = i;
		}
		if (DeepestFit != INDEX_NONE)
		{
			OutDrawCallIndexToFitin = DeepestFit;
			return true;
		}
		return false;
	};

	auto PushSingleDrawCall = [&](FDreamUIRenderData& InRenderData, EDreamUIDrawCallType InDrawCallType, bool InIs2DSpace = true) {
		switch (InDrawCallType)
		{
		default:
		case EDreamUIDrawCallType::BatchMesh:
			{
				const FDreamUIGeometry& InItemGeo = *InRenderData.BatchMeshGeometry;
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
				DrawCallItem.BlendMode = InItemGeo.BlendMode;
				DrawCallItem.BatchMeshVisualArray.Add(InRenderData.BatchMeshVisualObject);
				DrawCallItem.VerticesCount = InItemGeo.Vertices.Num();
				DrawCallItem.IndicesCount = InItemGeo.Triangles.Num();
				DrawCallItem.BatchMeshTreeNode->Insert(DreamUIQuadTree::Rectangle(InItemGeo.BoundsMin2DInCanvasSpace, InItemGeo.BoundsMax2DInCanvasSpace));
				DrawCallItem.bIs2DSpace = InIs2DSpace;
				//last: the draw call keeps the prepared copy itself, shared with its visual, rather than a copy of it
				DrawCallItem.BatchMeshGeometryArray.Add(MoveTemp(InRenderData.BatchMeshGeometry));
				InOutUIDrawCallList.Add(MoveTemp(DrawCallItem));
			}
			break;
		case EDreamUIDrawCallType::PostProcess:
			{
				auto DrawCallItem = FDreamUIDrawCall(InDrawCallType);
				DrawCallItem.PostProcessVisualObject = InRenderData.PostProcessVisualObject;
				DrawCallItem.PostProcessBoundsMin2DInCanvasSpace = InRenderData.PostProcessBoundsMin2DInCanvasSpace;
				DrawCallItem.PostProcessBoundsMax2DInCanvasSpace = InRenderData.PostProcessBoundsMax2DInCanvasSpace;
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

	// Same gate as the renderer's dump: which geometry reaches assembly, with what material. Looked up once a batch --
	// a console variable is found by name, which cost a lookup per element when it was done in the loop. The
	// variable is a static in the renderer's translation unit, so the lookup can answer null (not yet constructed,
	// or that unit compiled out), and this runs on the batching thread.
	const IConsoleVariable* DumpMaterialDrawsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("dreamgui.DumpMaterialDraws"));
	const bool bDumpMaterialDraws = DumpMaterialDrawsCVar != nullptr && DumpMaterialDrawsCVar->GetInt() != 0;

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
				if (!RenderData.BatchMeshGeometry.IsValid())
				{
					continue;
				}
				const FDreamUIGeometry& ItemGeo = *RenderData.BatchMeshGeometry;

				if (bDumpMaterialDraws)
				{
					UE_LOG(DreamGUI, Display, TEXT("[DumpMaterialDraws][assemble] visual=%s verts=%d material=%s batching=%d"),
						RenderData.BatchMeshVisualObject.IsValid() ? *RenderData.BatchMeshVisualObject->GetClass()->GetName() : TEXT("null"),
						ItemGeo.Vertices.Num(),
						ItemGeo.Material.IsValid() ? *ItemGeo.Material->GetName() : TEXT("none"),
						ItemGeo.bSupportDrawcallBatching ? 1 : 0);
				}

				bool is2DUIItem = Is2DUITransform(ItemGeo.TransformRelativeToCanvas);
				//a 3D element's 2D bounds do not describe where it ends up on screen, so only flat
				//elements are culled by them
				if (is2DUIItem && IsOutsideCanvas(ItemGeo.BoundsMin2DInCanvasSpace, ItemGeo.BoundsMax2DInCanvasSpace))
				{
					continue;
				}
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
					DrawCallItem.BatchMeshVisualArray.Add(RenderData.BatchMeshVisualObject);
					DrawCallItem.BatchMeshTreeNode->Insert(DreamUIQuadTree::Rectangle(ItemGeo.BoundsMin2DInCanvasSpace, ItemGeo.BoundsMax2DInCanvasSpace));
					DrawCallItem.VerticesCount += ItemGeo.Vertices.Num();
					DrawCallItem.IndicesCount += ItemGeo.Triangles.Num();
					//last, shared rather than copied: see PushSingleDrawCall
					DrawCallItem.BatchMeshGeometryArray.Add(MoveTemp(RenderData.BatchMeshGeometry));
					// CanFitInDrawCall keeps this true; past the limit the indices would wrap, which is a wrong
					// picture rather than a reason to stop the process, on a thread that is only batching.
					ensureMsgf(DrawCallItem.VerticesCount < LEXUI_MAX_VERTEX_COUNT, TEXT("A draw call reached %d vertices; the limit is %d."), DrawCallItem.VerticesCount, LEXUI_MAX_VERTEX_COUNT);
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

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_CombineDrawCalls);
		/**
		 * The draw calls that will take a section back: those built from the very copies a section was built from, then,
		 * among the rest, those built from copies laid out as a section's are, each section going to one draw call -- the
		 * way UDreamUIMeshComponent::ClaimPooledMeshSections hands them out. Their vertices are left uncombined: nothing
		 * reads them unless the section is not there after all (FDreamCanvasPreparedDrawCallData::GeometryListsOnSections).
		 */
		TArray<bool, TInlineAllocator<64>> TakesASectionBack;
		TakesASectionBack.SetNumZeroed(InOutUIDrawCallList.Num());
		if (InGeometryListsOnSections != nullptr && InGeometryListsOnSections->Num() > 0)
		{
			TArray<bool, TInlineAllocator<64>> SectionTaken;
			SectionTaken.SetNumZeroed(InGeometryListsOnSections->Num());
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				for (int32 DrawCallIndex = 0; DrawCallIndex < InOutUIDrawCallList.Num(); ++DrawCallIndex)
				{
					const FDreamUIDrawCall& DrawCallItem = InOutUIDrawCallList[DrawCallIndex];
					if (DrawCallItem.Type != EDreamUIDrawCallType::BatchMesh || TakesASectionBack[DrawCallIndex])
					{
						continue;
					}
					for (int32 SectionIndex = 0; SectionIndex < SectionTaken.Num(); ++SectionIndex)
					{
						const TArray<TSharedPtr<const FDreamUIGeometry>>& OnSection = (*InGeometryListsOnSections)[SectionIndex];
						if (!SectionTaken[SectionIndex] && (Pass == 0 ? OnSection == DrawCallItem.BatchMeshGeometryArray
							: FDreamUIDrawCall::GeometryListsShareLayout(OnSection, DrawCallItem.BatchMeshGeometryArray)))
						{
							SectionTaken[SectionIndex] = true;
							TakesASectionBack[DrawCallIndex] = true;
							break;
						}
					}
				}
			}
		}
		for (int32 DrawCallIndex = 0; DrawCallIndex < InOutUIDrawCallList.Num(); ++DrawCallIndex)
		{
			FDreamUIDrawCall& DrawCallItem = InOutUIDrawCallList[DrawCallIndex];
			if (DrawCallItem.Type != EDreamUIDrawCallType::BatchMesh)
			{
				continue;
			}
			if (TakesASectionBack[DrawCallIndex])
			{
				DrawCallItem.ApplyBatchMeshBoundsToCombined();
				DrawCallItem.bCombinePending = true;
			}
			else
			{
				DrawCallItem.ApplyBatchMeshGeometryToCombined();
			}
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
			bUpdateEveryWidget = true;
		}
		bPrevIsVisible = bNowIsVisible;
	}

	//update draw-call
	if (bCanTickUpdate)
	{
		bCanTickUpdate = false;
		RootCanvas->bAnythingChangedForRenderTarget = true;
		// Whatever made this canvas update -- layout, a transform, geometry, a sort -- may have moved what a
		// ray would hit on it.
		UDreamUIManagerWorldSubsystem::BumpHitTestGenerationFor(this);
		CheckUIMesh();
		struct LOCAL
		{
			static void CollectRenderWidget(UDreamWidget* Widget
				, UDreamCanvas* ThisCanvas
				, TArray<TObjectPtr<UDreamWidget>>& WidgetCollection)
			{
				//deleting a child in the designer leaves a null entry in Children until the tree is rebuilt,
				//and the widget list must not carry it into the update passes below
				if (!IsValid(Widget))return;
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
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_GenerateWidgetList);
			bNeedToGenerateWidgetList = false;
			WidgetList.Reset();
			LOCAL::CollectRenderWidget(GetWidget(), this, WidgetList);
			// A new list, in which the widgets that stayed keep their order: a widget that came or moved asked for itself,
			// and every widget is looked at only when something else woke the canvas too. The next prepare merges.
			bWidgetListIndexValid = false;
			bWidgetListChangedSincePrepare = true;
		}

		CheckWidgetPropertyData();
		WidgetPropertyDataAsTexture->PrepareForBatchUpdate();
		//update clip and geometry from head to tail
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateClipAndGeometry)
			TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_UpdateClipAndGeometry);
			// Resolved once for the loop rather than twice for every widget in it: the root is a weak pointer.
			UDreamCanvas* const Root = RootCanvas.Get();
			auto UpdateWidget = [this, Root](UDreamWidget* Widget)
			{
				if (Root != nullptr)
				{
					Widget->UpdateClip(Root->ClipDataAsTexture, Root->ClipDataList);
				}
				if (Widget->GetRenderVisibleInHierarchy() && Widget->GetRenderCanvas() == this)
				{
					Widget->UpdateVisual();
				}
			};
			// Taken now: a widget that asks again while this update runs -- its geometry asking for its block data, say --
			// is looked at in the next one, as it was when the whole canvas woke up again.
			const TArray<TWeakObjectPtr<UDreamWidget>> Asking = MoveTemp(WidgetsToUpdate);
			WidgetsToUpdate.Reset();
			const bool bEveryWidget = bUpdateEveryWidget;
			bUpdateEveryWidget = false;
			TArray<UDreamWidget*> AskedInListOrder;
			if (!bEveryWidget && GatherWidgetsToUpdateInListOrder(Asking, AskedInListOrder))
			{
				// Only the widgets that asked. Walking the list, a widget's parents have their clips brought up to date
				// before it, and it may inherit one: here its parents' clips are, as far as this canvas's widgets go.
				for (UDreamWidget* Widget : AskedInListOrder)
				{
					if (Root != nullptr)
					{
						TArray<UDreamWidget*, TInlineAllocator<16>> Parents;
						for (UDreamWidget* Parent = Widget->GetParent(); IsValid(Parent) && Parent->GetRenderCanvas() == this; Parent = Parent->GetParent())
						{
							Parents.Add(Parent);
						}
						for (int32 Index = Parents.Num() - 1; Index >= 0; --Index)
						{
							Parents[Index]->UpdateClip(Root->ClipDataAsTexture, Root->ClipDataList);
						}
					}
					UpdateWidget(Widget);
					if (!bPrepareEveryWidget)
					{
						WidgetsToPrepare.Add(Widget);
					}
				}
				// Colour after colour with no rebuild between would grow the list without end; past half the widgets a
				// full prepare costs no more.
				if (WidgetsToPrepare.Num() > FMath::Max(32, WidgetList.Num() / 2))
				{
					bPrepareEveryWidget = true;
					WidgetsToPrepare.Reset();
				}
				DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::WidgetsUpdated, AskedInListOrder.Num());
			}
			else
			{
				for (const auto& Widget : WidgetList)
				{
					//a widget collected earlier can be destroyed before the list is regenerated
					if (!IsValid(Widget))continue;
					UpdateWidget(Widget);
				}
				DreamUIRenderStats::AddCount(DreamUIRenderStats::ECounter::WidgetsUpdated, WidgetList.Num());
				bPrepareEveryWidget = true;
				WidgetsToPrepare.Reset();
			}
			// Clips created above are uploaded by RefreshAllClipData, driven every tick from the UI manager.
		}
		WidgetPropertyDataAsTexture->Flush();
		
		if (bShouldRebuildDrawCall && !bDrawCallRebuildSuspended)
		{
			bShouldRebuildDrawCall = false;
			// The prepare below takes every vertex change asked for until now.
			bHasPendingUpdateData = false;
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
				//only when this canvas's rect is the surface being drawn: a root canvas, or one that
				//renders to its own target. See BatchDrawCallAsync.
				PreparedDrawCallData.bCullElementsOutsideCanvasRect = bCullElementsOutsideCanvas
					&& (this->IsRootCanvas() || this->bForceRenderToTarget);
				PrepareDrawCallBatchingData(PreparedDrawCallData.DataArray);
				PreparedDrawCallData.GeometryListsOnSections = UIMesh->GetMeshSectionGeometryLists();
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
		//this frame must show this frame's batching, so wait for it. The wait is not a sleep loop any
		//more: it can retract a batch the worker pool has not started and run it here, which is both
		//sooner than the old 1ms granularity and work the game thread was going to wait for anyway.
		DrawCallProcessingRunnable->WaitForBatchingToFinish();
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
	/**
	 * A vertex refresh asked for since the draw calls in hand were prepared, once they are the newest asked for: a
	 * rebuild still on its way prepared after the request, and takes it. The request used to go with the next update of
	 * the canvas, which cleared it whether or not a refresh had happened in between, and with any frame a batch result
	 * arrived in -- a colour changed then never showed.
	 */
	if (bHasPendingUpdateData && GFrameCounter > CurrentDrawCallData.FrameNumber && CurrentDrawCallData.FrameNumber == NewestDrawCallFrameNumber)
	{
		bHasPendingUpdateData = false;
		for (int i = 0; i < CurrentDrawCallData.DrawCallArray.Num(); i++)
		{
			auto& DrawCallItem = CurrentDrawCallData.DrawCallArray[i];
			// Only a draw call one of whose elements changed is copied and goes up again.
			if (DrawCallItem.Type == EDreamUIDrawCallType::BatchMesh && DrawCallItem.CopyBatchMeshGeometry())
			{
				UIMesh->UpdateMeshSection(DrawCallItem.RenderSection, &DrawCallItem);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(DreamUI_UpdateDrawCallMesh);
	if (!IsValid(UIMesh))return;
	UIMesh->PoolAllRenderSection();
	// Before any section is set up: every draw call that can take its old section back as it was claims it first.
	UIMesh->ClaimPooledMeshSections(CurrentDrawCallData.DrawCallArray);
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
		// The mesh belongs to the host ACTOR when this hierarchy has one.
		//
		// A component's owner is read off its outer chain when it is constructed, and a widget tree's
		// chain ends at the World -- so a mesh outered to the widget answered null to GetOwner(), and
		// put itself in nobody's component list. That list is what the viewport picks through and what
		// the details panel shows, so an entire world-space panel was unclickable and invisible there,
		// and every GetOwner() on the mesh was a null waiting to be dereferenced.
		//
		// Asked of the ROOT canvas, not of this one: only the root records which scene component the
		// tree hangs from, so a nested canvas would otherwise keep its own meshes outered to a widget
		// while the root's moved to the actor. A canvas with no host at all -- screen space, render
		// target -- keeps the widget as its outer exactly as before.
		UObject* MeshOuter = DreamWidget;
		USceneComponent* HostComponent = nullptr;
		if (const UDreamCanvas* HostingCanvas = this->GetRootCanvas())
		{
			HostComponent = HostingCanvas->GetAttachedRootSceneComponent();
		}
		if (HostComponent != nullptr)
		{
			if (AActor* HostActor = HostComponent->GetOwner())
			{
				MeshOuter = HostActor;
			}
		}
		auto ObjectName = MakeUniqueObjectName(MeshOuter, MeshType, FName(*this->GetWidget()->GetDisplayName()));
		// Never saved, duplicated or copied: a copy of the host actor builds its own tree and its own mesh.
		// TextExportTransient matters most here. The level editor's Copy writes out every object inside a
		// copied actor that lacks it -- Transient does not keep an object out of the text -- and Paste made
		// that text an ordinary, non-transient component of the new actor, still naming this canvas's
		// materials. A play-in-editor duplication carries every non-transient component of an actor, so it
		// followed those materials into this tree and cloned its data textures without their size.
		UIMesh = NewObject<UDreamUIMeshComponent>(MeshOuter, MeshType, ObjectName, DreamUI::RuntimeObjectFlags);
		UIMesh->RegisterComponentWithWorld(this->GetWorld());
		// The same host the outer came from, so the mesh is in the actor's attachment tree as well as
		// in its component list. A scene component that an actor owns but that hangs off nothing is a
		// loose component: the editor's picking and the outliner both walk the attachment tree, and
		// neither would find it. Attaching costs nothing at runtime -- SetComponentToWorld below
		// overwrites the transform outright, every frame, so the relative transform never matters --
		// and a canvas with no host passes null here exactly as it did before.
		UIMesh->AttachToComponent(HostComponent, FAttachmentTransformRules::KeepRelativeTransform);
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
			if (!DreamUI::IsGameWorld(this))//edit mode
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
				if (!DreamUI::IsGameWorld(this))
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
		//WorldSpace_DreamUI needs no request: the renderer rebuilds and resorts its world-space
		//sequence every frame, because that order depends on distance to the current camera.
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
	/**
	 * Locals, not function-level statics. This is called from the draw-call update of every canvas
	 * (and from the editor), so two callers sharing one scratch buffer is a data race, and a static
	 * one also holds on to the high-water-mark allocation of the worst material ever inspected for
	 * the rest of the process. (GetAllTextureParameterInfo takes default-allocator arrays, so these
	 * cannot be inline-allocated.)
	 */
	TArray<FMaterialParameterInfo> ParameterInfos;
	TArray<FGuid> ParameterIds;
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
	//reset index for dynamic material -- and retire what stopped being used. The pools were
	//high-water-mark allocators: a screenful of one-off materials was rent paid forever (the old
	//@todo on MapSrcMatToDynamicMat). At this point the counters still hold LAST frame's usage, so
	//the tail beyond it is provably idle; a tail idle for a whole decay window is dropped, its
	//parameter-cache entries with it. The window keeps a transiently hidden panel from thrashing
	//allocate/free on every blink.
	{
		constexpr int MaterialPoolDecayFrames = 120;
		TArray<UMaterialInterface*, TInlineAllocator<8>> EmptiedSources;
		for (auto& KeyValue : MapSrcMatToDynamicMat)
		{
			FDreamCanvasDynamicMaterialArrayContainer& Container = KeyValue.Value;
			const int UsedLastFrame = Container.CurrentIndex;
			if (UsedLastFrame < Container.MaterialArray.Num())
			{
				++Container.UnusedStreak;
				if (Container.UnusedStreak > MaterialPoolDecayFrames)
				{
					for (int Index = UsedLastFrame; Index < Container.MaterialArray.Num(); ++Index)
					{
						MapMatToParamCache.Remove(Container.MaterialArray[Index]);
					}
					Container.MaterialArray.SetNum(UsedLastFrame);
					Container.UnusedStreak = 0;
					if (Container.MaterialArray.Num() == 0)
					{
						EmptiedSources.Add(KeyValue.Key);
					}
				}
			}
			else
			{
				Container.UnusedStreak = 0;
			}
			Container.CurrentIndex = 0;
		}
		for (UMaterialInterface* Source : EmptiedSources)
		{
			MapSrcMatToDynamicMat.Remove(Source);
		}
		for (auto It = MaterialProxyPools.CreateIterator(); It; ++It)
		{
			FMaterialProxyPool& Pool = It.Value();
			if (Pool.CurrentIndex < Pool.Proxies.Num())
			{
				if (++Pool.UnusedStreak > MaterialPoolDecayFrames)
				{
					Pool.Proxies.SetNum(Pool.CurrentIndex);
					Pool.UnusedStreak = 0;
				}
			}
			else
			{
				Pool.UnusedStreak = 0;
			}
			Pool.CurrentIndex = 0;
			if (Pool.Proxies.Num() == 0)
			{
				It.RemoveCurrent();
			}
		}
	}
	const bool bUseMaterialProxies = CVarDreamUIMaterialWrappers.GetValueOnGameThread() != 0;
	// A proxy of InSource's from its pool, a new one when the pool is used up.
	auto TakeMaterialProxy = [this](UMaterialInterface* InSource)
	{
		FMaterialProxyPool& Pool = MaterialProxyPools.FindOrAdd(TObjectKey<UMaterialInterface>(InSource));
		if (!Pool.Proxies.IsValidIndex(Pool.CurrentIndex))
		{
			Pool.Proxies.Add(FDreamUIMaterialProxy::Create(InSource));
			bNeedToVerifyMaterials = true;//verify material when new material will be used
		}
		return Pool.Proxies[Pool.CurrentIndex++];
	};

	const bool bUseBuiltInShader = UDreamUISettings::GetUseBuiltInUIShader() && IsRenderByDreamUIRendererOrUERenderer();
	auto SetCommonParameterForMaterial = [&](UMaterialInstanceDynamic* InMaterialInstanceDynamic)
	{
		InMaterialInstanceDynamic->SetScalarParameterValue(DreamUI_IsRenderByDreamUIRenderer_MaterialParameterName, this->IsRenderByDreamUIRendererOrUERenderer());
		InMaterialInstanceDynamic->SetTextureParameterValue(DreamUI_WidgetPropertyDataTexture_MaterialParameterName, this->WidgetPropertyDataAsTexture->GetDataTexture());
		InMaterialInstanceDynamic->SetTextureParameterValue(DreamUI_ClipDataTexture_MaterialParameterName, RootCanvas->ClipDataAsTexture->GetDataTexture());
	};

	// Every pooled instance, not only those this frame's draw calls take. An instance waiting in a pool
	// keeps the data textures it was last given, and a frame that took it back after they were replaced
	// -- the widget property data grown past its texture, the clip data likewise, a new root canvas --
	// drew through textures the canvas no longer uses, which may be serving something else by then.
	if (RootCanvas.IsValid() && (bWidgetPropertyDataAsTextureChanged || RootCanvas->bClipDataAsTextureChanged || bNeedToSetClipDataTextureMaterialParameter))
	{
		for (UMaterialInstanceDynamic* Material : PooledDefaultMaterialList)
		{
			if (IsValid(Material))
			{
				SetCommonParameterForMaterial(Material);
			}
		}
		for (auto& SourceAndInstances : MapSrcMatToDynamicMat)
		{
			for (UMaterialInstanceDynamic* Material : SourceAndInstances.Value.MaterialArray)
			{
				if (IsValid(Material))
				{
					SetCommonParameterForMaterial(Material);
				}
			}
		}
	}

	// UpdateDrawCallMesh does not create a section for every draw-call (the WorldSpace path skips
	// PostProcess ones), so the section index must be counted from the draw-calls that own one --
	// indexing sections by the loop index walks past the end of the section array as soon as
	// anything was skipped.
	int32 SectionIndex = -1;
	for (int i = 0; i < CurrentDrawCallData.DrawCallArray.Num(); i++)
	{
		auto& DrawCallItem = CurrentDrawCallData.DrawCallArray[i];
		if (DrawCallItem.RenderSection.IsValid())
		{
			++SectionIndex;
		}
		switch (DrawCallItem.Type)
		{
		case EDreamUIDrawCallType::BatchMesh:
			if (bUseMaterialProxies && (DrawCallItem.Material.IsValid() || (!bUseBuiltInShader && GetDefaultMaterial() != nullptr)))
			{
				/**
				 * The draw call's own material, or the default one: DreamGUI answers the parameters it gives a material in
				 * the material's place, through a proxy of the material's render proxy -- a material instance given to the
				 * draw call is answered for as it is, never written to. A material with none of those parameters draws as
				 * it is.
				 */
				UMaterialInterface* Source = DrawCallItem.Material.IsValid() ? DrawCallItem.Material.Get() : GetDefaultMaterial();
				if (DrawCallItem.Material.IsValid() && !Source->IsA<UMaterialInstanceDynamic>() && !IsMaterialContainsDreamUIParameter(Source))
				{
					bNeedToVerifyMaterials = true;//verify material when new material will be used
					if (UIMesh->IsMeshSectionBuiltIn(SectionIndex))
					{
						UIMesh->SetMeshSectionBuiltIn(SectionIndex, FDreamUIBuiltInDrawParams());
					}
					UIMesh->SetMeshSectionMaterial(SectionIndex, Source);
					break;
				}
				const TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe> Proxy = TakeMaterialProxy(Source);
				FDreamUIMaterialParameters Parameters;
				Parameters.SetScalar(DreamUI_IsRenderByDreamUIRenderer_MaterialParameterName, this->IsRenderByDreamUIRendererOrUERenderer() ? 1.0f : 0.0f);
				Parameters.SetTexture(DreamUI_WidgetPropertyDataTexture_MaterialParameterName, this->WidgetPropertyDataAsTexture->GetDataTexture());
				Parameters.SetTexture(DreamUI_ClipDataTexture_MaterialParameterName, RootCanvas->ClipDataAsTexture->GetDataTexture());
				Parameters.SetTexture(DreamUI_MainTextureMaterialParameterName, DrawCallItem.Texture.Get());
				Parameters.SetTexture(DreamUI_FontTextureMaterialParameterName, DrawCallItem.FontTexture.Get());
				const FVector4f AtlasInfo = MakeFontAtlasInfo(DrawCallItem);
				Parameters.SetVector(DreamUI_FontAtlasInfoMaterialParameterName, FLinearColor(AtlasInfo.X, AtlasInfo.Y, AtlasInfo.Z, AtlasInfo.W));
				for (const TWeakObjectPtr<UDreamVisualBatchMesh>& BatchMeshVisual : DrawCallItem.BatchMeshVisualArray)
				{
					if (const UDreamVisualBatchMesh* Visual = BatchMeshVisual.Get())
					{
						Visual->AddMaterialParameters(Parameters);
					}
				}
				// Sent to the render thread only when something in them changed.
				if (Proxy->GetParameters_GameThread() != Parameters)
				{
					Proxy->SetParameters_GameThread(Parameters);
				}
				if (UIMesh->IsMeshSectionBuiltIn(SectionIndex))
				{
					UIMesh->SetMeshSectionBuiltIn(SectionIndex, FDreamUIBuiltInDrawParams());
				}
				UIMesh->SetMeshSectionMaterial(SectionIndex, Source, Proxy);
				break;
			}
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
						SetCommonParameterForMaterial(RenderMatDynamic);
					}
					else
					{
						auto DynamicMaterialContainerPtr = MapSrcMatToDynamicMat.Find(DrawCallItem.Material.Get());
						if (!DynamicMaterialContainerPtr)
						{
							if (IsMaterialContainsDreamUIParameter(DrawCallItem.Material.Get()))
							{
								bShouldSetMaterialParameter = true;
								auto RenderMatDynamic = DreamCanvasLocal::CreateRuntimeMaterial(DrawCallItem.Material.Get(), this);
								SetCommonParameterForMaterial(RenderMatDynamic);
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
								auto RenderMatDynamic = DreamCanvasLocal::CreateRuntimeMaterial(DrawCallItem.Material.Get(), this);
								MaterialArray.Add(RenderMatDynamic);
								SetCommonParameterForMaterial(RenderMatDynamic);
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
					// No material at all: the renderer draws this section with the built-in UI shader. The textures go
					// over as textures, not as their resources: the render thread binds each one's reference, which
					// follows it through a rebuild and outlives it (see FDreamUIBuiltInDrawParams).
					FDreamUIBuiltInDrawParams BuiltIn;
					BuiltIn.bEnabled = true;
					BuiltIn.MainTexture = DrawCallItem.Texture.Get();
					BuiltIn.FontTexture = DrawCallItem.FontTexture.Get();
					BuiltIn.WidgetDataTexture = WidgetPropertyDataAsTexture->GetDataTexture();
					BuiltIn.ClipDataTexture = RootCanvas->ClipDataAsTexture->GetDataTexture();
					const FVector4f AtlasInfo = MakeFontAtlasInfo(DrawCallItem);
					BuiltIn.FontAtlasSize = FVector2f(AtlasInfo.X, AtlasInfo.Y);
					BuiltIn.FontFieldRangeTexels = AtlasInfo.Z;
					BuiltIn.FontEmTexels = AtlasInfo.W;
					//every element in this draw-call agreed on it; CanConsumeUIGeometryForBatchMesh is
					//what makes that true
					BuiltIn.BlendMode = DrawCallItem.BlendMode;
					UIMesh->SetMeshSectionBuiltIn(SectionIndex, BuiltIn);
					UIMesh->SetMeshSectionMaterial(SectionIndex, nullptr);
					break;
				}
				else
				{
					auto GetUIMaterialFromPool = [&]()
					{
						if (UsingMaterialStartIndex < 0)
						{
							auto SrcMaterial = GetDefaultMaterial();
							auto RenderMatDynamic = DreamCanvasLocal::CreateRuntimeMaterial(SrcMaterial, this);
							PooledDefaultMaterialList.Add(RenderMatDynamic);
							SetCommonParameterForMaterial(RenderMatDynamic);
							bNeedToVerifyMaterials = true;//verify material when new material will be used
							return RenderMatDynamic;
						}
						auto RenderMatDynamic = PooledDefaultMaterialList[UsingMaterialStartIndex];
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
				if (UIMesh->IsMeshSectionBuiltIn(SectionIndex))
				{
					UIMesh->SetMeshSectionBuiltIn(SectionIndex, FDreamUIBuiltInDrawParams());
				}
				UIMesh->SetMeshSectionMaterial(SectionIndex, RenderMat);
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

	// The proxies point at their sources and at the textures they answer with; a texture let go of here was replaced in its
	// proxy by a command already sent, which the render thread carries out before the texture can be collected.
	MaterialProxyReferences.Reset();
	for (const TPair<TObjectKey<UMaterialInterface>, FMaterialProxyPool>& SourceAndPool : MaterialProxyPools)
	{
		for (const TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe>& Proxy : SourceAndPool.Value.Proxies)
		{
			MaterialProxyReferences.Add(Proxy->GetSource());
			for (const TPair<FName, const UTexture*>& Texture : Proxy->GetParameters_GameThread().Textures)
			{
				if (Texture.Value != nullptr)
				{
					MaterialProxyReferences.Add(const_cast<UTexture*>(Texture.Value));
				}
			}
		}
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
		//this is a divisor of the render target size in CheckAndApplyViewportParameter, so it changes
		//the canvas's notion of its viewport; nothing else would re-derive that outside the editor
		CheckAndApplyViewportParameter();
	}
}

void UDreamCanvas::SetDrawCallRebuildSuspended(bool Value)
{
	if (bDrawCallRebuildSuspended != Value)
	{
		bDrawCallRebuildSuspended = Value;
		if (!bDrawCallRebuildSuspended)
		{
			/**
			 * Releasing is what makes the deferral safe: bShouldRebuildDrawCall was never cleared while
			 * suspended, so anything that went dirty in the meantime is still asking for its rebuild.
			 * bCanTickUpdate is what actually gets the update pass to look, and it may well have been
			 * consumed by a vertex-only refresh since.
			 */
			bCanTickUpdate = true;
			bUpdateEveryWidget = true;
		}
	}
}

void UDreamCanvas::SetCullElementsOutsideCanvas(bool Value)
{
	if (bCullElementsOutsideCanvas != Value)
	{
		bCullElementsOutsideCanvas = Value;
		//which elements reach the draw-call list changes, so the list itself has to be rebuilt
		MarkCanvasUpdate(true);
	}
}

void UDreamCanvas::SetScreenSpaceRenderScale(float Value)
{
	Value = FMath::Clamp(Value, 0.1f, 1.0f);
	if (ScreenSpaceRenderScale != Value)
	{
		ScreenSpaceRenderScale = Value;
		bAnythingChangedForRenderTarget = true;
	}
}

void UDreamCanvas::SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode Value)
{
	if (RenderTargetSizeMode != Value)
	{
		RenderTargetSizeMode = Value;
		bAnythingChangedForRenderTarget = true;
		CheckAndApplyViewportParameter();
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
		DefaultMaterial = InMaterial;
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
		// Sorted on its own now, or with its parent again: either way its mesh must not stay hooked
		// into the parent's as a child section, or the parent draws it too, in the parent's order. The
		// parent's mesh as it stands (see ClearDrawCall).
		if (IsValid(UIMesh) && ParentCanvas.IsValid())
		{
			UIMesh->ClearParentCanvasMeshComp(ParentCanvas->UIMesh.Get());
		}
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
	// Reversed Z either way: every RHI the engine runs on inverts the depth buffer, which is why it is retiring the
	// switch that said so.
	if (InProjectionType == ECameraProjectionMode::Orthographic)
	{
		const float tempOrthoWidth = InViewportSize.X * 0.5f;
		const float tempOrthoHeight = InViewportSize.Y * 0.5f;

		const float ZScale = 1.0f / (FarClipPlane - NearClipPlane);
		const float ZOffset = -NearClipPlane;

		OutProjectionMatrix = FReversedZOrthoMatrix(
			tempOrthoWidth,
			tempOrthoHeight,
			ZScale,
			ZOffset
		);
	}
	else
	{
		float XAxisMultiplier = 1.0f;
		float YAxisMultiplier = InViewportSize.X / (float)InViewportSize.Y;

		OutProjectionMatrix = FReversedZPerspectiveMatrix(
			InFOV,
			InFOV,
			XAxisMultiplier,
			YAxisMultiplier,
			NearClipPlane,
			FarClipPlane
		);
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
	// Answered before the world, the render mode or the player controller are consulted, because
	// standing in for sources that are not there is the whole of what a substituted viewport is for.
	if (ViewportSizeOverride.IsSet())
	{
		return ViewportSizeOverride.GetValue();
	}
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
			else if (RenderMode == EDreamRenderMode::RenderTarget && IsValid(GetRenderTarget()))
			{
				TempViewportSize.X = GetRenderTarget()->SizeX / RenderTargetResolutionScale;
				TempViewportSize.Y = GetRenderTarget()->SizeY / RenderTargetResolutionScale;
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
		// A canvas that renders to its own target is a root of its own, and one that stops is part of
		// its parent's root again: the draw calls built for the other arrangement are dropped, and the
		// root is looked up afresh rather than taken from the cache.
		ClearDrawCall();
		CheckRootCanvas(true);
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
			/**
			 * In RenderTarget mode ViewportSize is derived from the render target's size, not from the
			 * game viewport -- which is why RegisterCanvasScaler binds the viewport resize event only
			 * for ScreenSpaceOverlay. Nothing else told this canvas that its "viewport" had changed
			 * size, so outside the editor (where the editor tick happened to re-derive it) the canvas
			 * scale and projection kept the size of the previous render target for good.
			 */
			CheckAndApplyViewportParameter();
		}
		OnRenderTargetChanged.Broadcast(GetRenderTarget());
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
			// Reported rather than asserted: the two are separate properties, and nothing about loading or
			// editing them keeps them in step.
			ensureMsgf(this->RenderMode == EDreamRenderMode::RenderTarget, TEXT("%s: forced to render to a target while its render mode says otherwise."), *GetPathName());
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
		return this->GetRenderTarget();
	}
	else
	{
		if (CheckRootCanvas())
		{
			return RootCanvas->GetRenderTarget();
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

void UDreamCanvas::CheckWidgetPropertyData()
{
	// Neither data texture is watched for growing: each grows in place, and everything that samples it -- the
	// material instances, the built-in draws -- binds its reference, which follows it. A growth used to swap in a new
	// texture, which every instance had to be given again and every draw call rebuilt for.
	if (!IsValid(WidgetPropertyDataAsTexture))
	{
		WidgetPropertyDataAsTexture = NewObject<UDreamUIDataAsTexture>(this, UDreamUIDataAsTexture::StaticClass(), NAME_None, DreamUI::RuntimeObjectFlags);
		WidgetPropertyDataAsTexture->Init(UDreamVisual::WidgetPropertyDataLength, EDreamUIDataAsTexturePixelFormat::R32, 128);
	}
}

void UDreamCanvas::PushAsyncFunction_TransformVertices(TFunction<void()> InFunction)
{
	//if there is no worker to take it, run it here. The caller has already flagged its geometry as
	//being calculated, and a function that is silently dropped leaves that flag set for ever, so
	//every later wait on that geometry would never return.
	if (TransformVerticesAsyncFunctionRunnable.IsValid() && TransformVerticesAsyncFunctionRunnable->IsRunning())
	{
		TransformVerticesAsyncFunctionRunnable->PushFunction(MoveTemp(InFunction));
		return;
	}
	InFunction();
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
	CalculateVisual2DBounds(LocalPoint1, LocalPoint2, OutTransform2D, OutMin, OutMax);
}

void UDreamCanvas::CalculateVisual2DBounds(const FVector2D& InLocalMin, const FVector2D& InLocalMax, const FTransform2D& OutTransform2D, FVector2D& OutMin, FVector2D& OutMax)
{
	//takes the local bounds as data rather than reading them off the visual, so the vertex-transform
	//worker task can call it without dereferencing a UObject
	const FVector2D LocalPoint1 = InLocalMin;
	const FVector2D LocalPoint2 = InLocalMax;
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

void UDreamCanvas::SetViewportSizeOverride(const FIntPoint& InSize)
{
	if (ViewportSizeOverride.IsSet() && ViewportSizeOverride.GetValue() == InSize)
	{
		return;
	}
	ViewportSizeOverride = InSize;
	// The size the canvas last applied is cached in ViewportSize and the root widget was sized from
	// it, so a substitution that only changed what GetViewportSize answers would leave the widget and
	// the projection matrix disagreeing with it until something else happened to re-apply.
	CheckAndApplyViewportParameter();
}

void UDreamCanvas::ClearViewportSizeOverride()
{
	if (!ViewportSizeOverride.IsSet())
	{
		return;
	}
	ViewportSizeOverride.Reset();
	//re-applied for the same reason setting it is: the cached size is still the substituted one
	CheckAndApplyViewportParameter();
}

void UDreamCanvas::CheckAndApplyViewportParameter()
{
	// A substituted viewport outranks both real sources. The overlay branch would honour it anyway --
	// it reads GetViewportSize -- but the render-target branch reads the texture's dimensions
	// directly, and would walk straight past the substitution.
	if (ViewportSizeOverride.IsSet())
	{
		ViewportSize = ViewportSizeOverride.GetValue();
		OnViewportParameterChanged();
		return;
	}
	// The viewport is the render target's only when the canvas follows the target. With
	// RenderTargetFitToCanvas the target follows the canvas, and sizing the canvas from it would chase
	// its own tail.
	const auto ApplyRenderTargetSize = [this]()
	{
		switch (RenderTargetSizeMode)
		{
		case EDreamCanvasRenderTargetSizeMode::None:
		case EDreamCanvasRenderTargetSizeMode::CanvasFitToRenderTarget:
			if (UTextureRenderTarget2D* Target = GetRenderTarget(); IsValid(Target))
			{
				ViewportSize.X = Target->SizeX / RenderTargetResolutionScale;
				ViewportSize.Y = Target->SizeY / RenderTargetResolutionScale;
				OnViewportParameterChanged();
			}
			break;
		case EDreamCanvasRenderTargetSizeMode::RenderTargetFitToCanvas:
			break;
		}
	};
	if (bForceRenderToTarget)
	{
		// A child canvas forced into its own target has a viewport of its own, whatever its root's is.
		ApplyRenderTargetSize();
		return;
	}
	if (!this->IsRootCanvas())
	{
		return;
	}
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
		ApplyRenderTargetSize();
	}
	break;
	}
}

void UDreamCanvas::RegisterCanvasScaler()
{
	/**
	 * Safe to call any number of times: every bind below first drops whatever its handle still names.
	 * A canvas can be registered twice without an unregister in between -- a world widget component
	 * switches the canvas's render mode while the widget is being created (SetRenderMode unregisters
	 * and registers), and the canvas's own OnRegister registers again right after. Overwriting a live
	 * handle leaked the binding it named: OnEditorTick ran twice a frame, and the first binding could
	 * never be removed by UnregisterCanvasScaler.
	 */
#if WITH_EDITOR
	if (GetWorld() && !GetWorld()->IsGameWorld() && this->IsRootCanvas())
	{
		if (auto WorldManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			if (EditorTickDelegateHandle.IsValid())
			{
				WorldManager->GetEditorTickDelegate().Remove(EditorTickDelegateHandle);
				EditorTickDelegateHandle.Reset();
			}
			EditorTickDelegateHandle = WorldManager->GetEditorTickDelegate().AddWeakLambda(this, [this](float deltaTime) {
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
							if (ViewportResizeDelegateHandle.IsValid())
							{
								viewport->ViewportResizedEvent.Remove(ViewportResizeDelegateHandle);
								ViewportResizeDelegateHandle.Reset();
							}
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
		if (auto WorldManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			WorldManager->GetEditorTickDelegate().Remove(EditorTickDelegateHandle);
		}
		//reset whether or not the manager was still there to remove it from: a handle kept after
		//unregistering reads as "still bound", so the next RegisterCanvasScaler overwrites it and any
		//binding it did name is never removable again
		EditorTickDelegateHandle.Reset();
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
		ViewportResizeDelegateHandle.Reset();
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
	if (DreamUI::IsGameWorld(this))//When hit play there is still an editor world and DrawViewportArea is called, which could cause frame dropdown, so skip it when playing
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

			if (!DreamUI::IsGameWorld(this))
			{
				// A substituted viewport outranks everything this branch would otherwise read -- the
				// fixed edit-mode size, the editor viewport, the render target's own dimensions -- and
				// has to say so here, because neither of the two branches below goes through
				// GetViewportSize. Unset, which is every canvas nobody handed one to, leaves them
				// exactly as they were. See SetViewportSizeOverride.
				if (ViewportSizeOverride.IsSet())
				{
					if (ViewportSize != ViewportSizeOverride.GetValue())
					{
						ViewportSize = ViewportSizeOverride.GetValue();
						OnViewportParameterChanged();
					}
				}
				else if (this->GetRenderMode() == EDreamRenderMode::ScreenSpaceOverlay)
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
				if (!ViewportSizeOverride.IsSet()
					&& this->GetRenderMode() == EDreamRenderMode::RenderTarget && IsValid(this->GetRenderTarget()))
				{
					auto prevSize = ViewportSize;
					ViewportSize.X = this->GetRenderTarget()->SizeX;
					ViewportSize.Y = this->GetRenderTarget()->SizeY;
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

/**
 * Every one of these four feeds GetProjectionMatrix, so the cached view-projection matrix has to be
 * invalidated here. OnViewportParameterChanged does not do it: it only re-derives the canvas size, and
 * only for a root canvas in a screen-space/render-target mode with a known viewport. The renderer
 * recomputes its matrix each frame, but the raycaster reads GetViewProjectionMatrix -- so without this
 * hit testing keeps using the projection from before the change while the picture uses the new one.
 * SetProjectionParameters, the bulk setter, already marks it dirty for the same reason.
 */
void UDreamCanvas::SetProjectionType(TEnumAsByte<ECameraProjectionMode::Type> Value)
{
	if (ProjectionType != Value)
	{
		ProjectionType = Value;
		bIsViewProjectionMatrixDirty = true;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetFieldOfView(float Value)
{
	if (FieldOfView != Value)
	{
		FieldOfView = Value;
		bIsViewProjectionMatrixDirty = true;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetNearClipPlane(float Value)
{
	if (NearClipPlane != Value)
	{
		NearClipPlane = Value;
		bIsViewProjectionMatrixDirty = true;
		OnViewportParameterChanged();
	}
}
void UDreamCanvas::SetFarClipPlane(float Value)
{
	if (FarClipPlane != Value)
	{
		FarClipPlane = Value;
		bIsViewProjectionMatrixDirty = true;
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
		//null is the documented way to clear a custom scale, so only initialize when one was actually given
		if (IsValid(CustomScale))
		{
			CustomScale->Init(this);//need to initialize when first set
		}
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


