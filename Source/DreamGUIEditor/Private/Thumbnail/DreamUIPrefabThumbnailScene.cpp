// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Thumbnail/DreamUIPrefabThumbnailScene.h"
#include "Components/PrimitiveComponent.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "Core/Components/DreamCanvas.h"
#include "DreamGUIEditorModule.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIMesh/DreamUIMeshComponent.h"
#include "PrefabSystem/DreamUIPrefab.h"


FDreamUIPrefabThumbnailScene::FDreamUIPrefabThumbnailScene()
	:FThumbnailPreviewScene()
	, NumStartingActors(0)
	, CurrentPrefab(nullptr)
{
	NumStartingActors = GetWorld()->GetCurrentLevel()->Actors.Num();
}
FDreamUIPrefabThumbnailScene::~FDreamUIPrefabThumbnailScene()
{
	ClearOldWidgets();
}
void FDreamUIPrefabThumbnailScene::SpawnPreviewActor()
{
	if (!CurrentPrefab.IsValid())return;
	if (RootAgentWidget != nullptr)return;
	auto CanvasSize = CurrentPrefab->CanvasSize;

	//create Canvas for UI
	auto RootWidget = NewObject<UDreamWidget>(this->GetWorld(), FName("[RootAgent]"));
	RootWidget->SetSizeDelta(CanvasSize);
	RootWidget->SetDisplayName(TEXT("[RootAgent]"));
	RootWidget->OnRegister();
	RootAgentWidget = TStrongObjectPtr(RootWidget);

	// The canvas must exist before the tree loads under the root: widgets adopt their render canvas
	// as they register, and in this never-ticked world nothing revisits that later, so a
	// load-then-add-canvas order leaves every child canvas-less and the draw-call batch empty.
	auto Canvas = RootWidget->AddComponent<UDreamCanvas>();

	// Not the asset's own render mode: ScreenSpaceOverlay (edit mode remaps it) and WorldSpace_DreamUI
	// both draw through the DreamUI view extension, and the thumbnail's view family carries no view
	// extensions, so those modes can never reach this render. The UE-renderer path draws plain
	// primitive components, which a thumbnail scene render does see.
	Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
	Canvas->bFixedSizeInEditMode = true;
	Canvas->SizeInEditMode = CanvasSize;

	CurrentPrefab->LoadPrefab(this->GetWorld(), RootWidget);

	Canvas->UpdateRootCanvas();//builds the geometry inline and pushes the draw-call batch to the async batcher
	// A preview world has no UDreamUIManagerWorldSubsystem -- the world-subsystem world-type filter
	// excludes EditorPreview -- so the manager-driven pipeline that normally uploads clip data and
	// consumes the async batch never runs here. Drive the canvas directly. The consume side races
	// the batcher thread (a just-pushed batch is not "batching" yet), and this world never gets
	// another frame to catch up on, hence the bounded retry; the cap only bites for a prefab with
	// nothing visible, which has no materials to wait for.
	Canvas->RefreshAllClipData();
	for (int32 Attempt = 0; Attempt < 50; ++Attempt)
	{
		Canvas->UpdateDrawCallBatchData();
		UDreamUIMeshComponent* UIMesh = Canvas->GetUIMesh();
		if (IsValid(UIMesh) && UIMesh->GetNumMaterials() > 0)
		{
			break;
		}
		FPlatformProcess::Sleep(0.001f);
	}
	// The proxies for the freshly filled mesh ride the deferred render-state flush, which this
	// world also never reaches on its own.
	GetWorld()->SendAllEndOfFrameUpdates();
	GetBoundsRecursive(RootWidget, PreviewBounds);
	if (PreviewBounds.SphereRadius < KINDA_SMALL_NUMBER)//if bounds is too small, set to 1x1 box
	{
		PreviewBounds = FBoxSphereBounds(FBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f)));
	}
}
void FDreamUIPrefabThumbnailScene::GetBoundsRecursive(UDreamWidget* RootWidget, FBoxSphereBounds& OutBounds)const
{
	if (!IsValid(RootWidget))
	{
		OutBounds = FBoxSphereBounds(EForceInit::ForceInitToZero);
		return;
	}
	struct LOCAL
	{
		static void GetBounds(UDreamWidget* InWidget, bool& bIsFirstBounds, FBox& OutBox)
		{
			if (auto Visual = InWidget->GetVisual())
			{
				FVector Min, Max;
				Visual->GetGeometryBounds3DInLocalSpace(Min, Max);
				FBox Box = FBox(Min, Max);
				Box = Box.TransformBy(InWidget->GetWorldTransform());
				if (bIsFirstBounds)
				{
					bIsFirstBounds = false;
					OutBox = Box;
				}
				else
				{
					OutBox += Box;
				}
			}
			for (auto Child : InWidget->GetChildren())
			{
				GetBounds(Child, bIsFirstBounds, OutBox);
			}
		}
	};

	bool bIsFirstBounds = true;
	FBox BoxBounds;
	LOCAL::GetBounds(RootWidget, bIsFirstBounds, BoxBounds);
	OutBounds = BoxBounds;
}
void FDreamUIPrefabThumbnailScene::ClearOldWidgets()
{
	if (RootAgentWidget != nullptr)
	{
		RootAgentWidget->DestroyWidget();
		RootAgentWidget.Reset();
	}
}
bool FDreamUIPrefabThumbnailScene::IsValidForVisualization()
{
	if (!CurrentPrefab.IsValid())
	{
		return false;
	}
	if (CurrentPrefab->BinaryData.Num() == 0)
	{
		return false;
	}
	if (PreviewBounds.ContainsNaN())
	{
		UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d Prefab:'%s' bounds is invalid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(CurrentPrefab->GetPathName()));
		return false;
	}
	return true;
}
void FDreamUIPrefabThumbnailScene::GetViewMatrixParameters(const float InFOVDegrees, FVector& OutOrigin, float& OutOrbitPitch, float& OutOrbitYaw, float& OutOrbitZoom)const
{
	const float HalfFOVRadians = FMath::DegreesToRadians<float>(InFOVDegrees) * 0.5f;

	const float PreviewSize = PreviewBounds.SphereRadius * 1.2f;
	const float BoundsZOffset = GetBoundsZOffset(PreviewBounds);
	const float TargetDistance = PreviewSize / FMath::Tan(HalfFOVRadians);

	USceneThumbnailInfo* ThumbnailInfo = GetSceneThumbnailInfo(TargetDistance);
	check(ThumbnailInfo);

	OutOrigin = -1 * PreviewBounds.Origin;
	OutOrbitPitch = ThumbnailInfo->OrbitPitch;
	OutOrbitYaw = ThumbnailInfo->OrbitYaw;
	OutOrbitZoom = TargetDistance + ThumbnailInfo->OrbitZoom;
}
void FDreamUIPrefabThumbnailScene::SetPrefab(class UDreamUIPrefab* Prefab)
{
	ClearOldWidgets();
	CurrentPrefab = Prefab;
	if (IsValid(Prefab))
	{
		SpawnPreviewActor();
	}
}
void FDreamUIPrefabThumbnailScene::ClearPrefab()
{
	ClearOldWidgets();
	CurrentPrefab = nullptr;
}
USceneThumbnailInfo* FDreamUIPrefabThumbnailScene::GetSceneThumbnailInfo(const float TargetDistance)const
{
	UDreamUIPrefab* Prefab = CurrentPrefab.Get();
	check(Prefab);
	USceneThumbnailInfo* ThumbnailInfo = Cast<USceneThumbnailInfo>(Prefab->ThumbnailInfo);
	if (!IsValid(ThumbnailInfo))
	{
		ThumbnailInfo = NewObject<USceneThumbnailInfo>(Prefab);
		Prefab->ThumbnailInfo = ThumbnailInfo;
	}
	ThumbnailInfo->OrbitPitch = 0;
	ThumbnailInfo->OrbitYaw = 90.0f;
	ThumbnailInfo->OrbitZoom = 0;
	return ThumbnailInfo;
}
