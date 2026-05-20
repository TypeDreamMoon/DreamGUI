// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Thumbnail/LexUIPrefabThumbnailScene.h"
#include "Components/PrimitiveComponent.h"
#include "ThumbnailRendering/SceneThumbnailInfo.h"
#include "Core/Components/LexCanvas.h"
#include "LGUIEditorModule.h"
#include "Core/Components/LexWidgetPresenterComponent.h"
#include "Core/Components/LexWidget.h"
#include "Interaction/UIRecyclableScrollView.h"
#include "PrefabSystem/LexUIPrefab.h"


FLexUIPrefabInstanceThumbnailScene::FLexUIPrefabInstanceThumbnailScene()
{
	InstancedThumbnailScenes.Reserve(MAX_NUM_SCENES);
}
TSharedPtr<FLexUIPrefabThumbnailScene> FLexUIPrefabInstanceThumbnailScene::FindThumbnailScene(const FString& InPrefabPath)const
{
	return InstancedThumbnailScenes.FindRef(InPrefabPath);
}
TSharedRef<FLexUIPrefabThumbnailScene> FLexUIPrefabInstanceThumbnailScene::EnsureThumbnailScene(const FString& InPrefabPath)
{
	TSharedPtr<FLexUIPrefabThumbnailScene> ExistingThumbnailScene = InstancedThumbnailScenes.FindRef(InPrefabPath);
	if (!ExistingThumbnailScene.IsValid())
	{
		if (InstancedThumbnailScenes.Num() >= MAX_NUM_SCENES)
		{
			InstancedThumbnailScenes.Reset();
		}
		ExistingThumbnailScene = MakeShareable(new FLexUIPrefabThumbnailScene());
		InstancedThumbnailScenes.Add(InPrefabPath, ExistingThumbnailScene);
	}
	return ExistingThumbnailScene.ToSharedRef();
}
void FLexUIPrefabInstanceThumbnailScene::Clear()
{
	InstancedThumbnailScenes.Reset();
}



FLexUIPrefabThumbnailScene::FLexUIPrefabThumbnailScene()
	:FThumbnailPreviewScene()
	, NumStartingActors(0)
	, CurrentPrefab(nullptr)
{
	NumStartingActors = GetWorld()->GetCurrentLevel()->Actors.Num();
}
void FLexUIPrefabThumbnailScene::SpawnPreviewActor()
{
	if (CurrentPrefab.IsValid())
	{
		if (!PresenterComponent.IsValid())
		{
			auto CanvasSize = CurrentPrefab->PrefabDataForPrefabEditor.CanvasSize;
			//create Canvas for UI
			auto WidgetPresenterActor = this->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity);
			auto WidgetPresenterComponent = WidgetPresenterActor->FindComponentByClass<ULexWidgetPresenterComponent>();
			if (!WidgetPresenterComponent)
			{
				WidgetPresenterComponent = NewObject<ULexWidgetPresenterComponent>(WidgetPresenterActor, ULexWidgetPresenterComponent::StaticClass());
				WidgetPresenterActor->SetRootComponent(WidgetPresenterComponent);
				WidgetPresenterComponent->RegisterComponent();
				WidgetPresenterActor->AddInstanceComponent(WidgetPresenterComponent);
			}
			WidgetPresenterComponent->CreateWidgetAndCanvasForEditor();
			auto Canvas = WidgetPresenterComponent->GetRootCanvasForEditor();
			auto RenderMode = (ELexRenderMode)CurrentPrefab->PrefabDataForPrefabEditor.CanvasRenderMode;
			Canvas->SetRenderMode(RenderMode);
			Canvas->bFixedSizeInEditMode = true;
			auto RootWidget = WidgetPresenterComponent->GetRootWidgetForEditor();

			RootWidget->SetWidth(CanvasSize.X);
			RootWidget->SetHeight(CanvasSize.Y);
			RootWidget->SetSiblingIndex(0);

			PresenterComponent = WidgetPresenterComponent;
		}
		PresenterComponent->SetPrefab(CurrentPrefab.Get());
		if (auto Canvas = PresenterComponent->GetLoadedCanvas())//for update draw-call immediately
		{
			Canvas->UpdateRootCanvas();
			GetBoundsRecursive(PresenterComponent->GetLoadedWidget(), PreviewBounds);
			if (PreviewBounds.SphereRadius < KINDA_SMALL_NUMBER)//if bounds is too small, set to 1x1 box
			{
				PreviewBounds = FBoxSphereBounds(FBox(FVector(-0.5f, -0.5f, -0.5f), FVector(0.5f, 0.5f, 0.5f)));
			}
		}
	}
}
void FLexUIPrefabThumbnailScene::GetBoundsRecursive(ULexWidget* RootWidget, FBoxSphereBounds& OutBounds)const
{
	if (!IsValid(RootWidget))
	{
		OutBounds = FBoxSphereBounds(EForceInit::ForceInitToZero);
		return;
	}
	struct LOCAL
	{
		static void GetBounds(ULexWidget* InWidget, bool& bIsFirstBounds, FBox& OutBox)
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
void FLexUIPrefabThumbnailScene::ClearOldActors()
{
	auto Level = GetWorld()->GetCurrentLevel();
	for (int i = NumStartingActors; i < Level->Actors.Num(); i++)
	{
		if (Level->Actors[i])
		{
			Level->Actors[i]->Destroy();
		}
	}
}
bool FLexUIPrefabThumbnailScene::IsValidForVisualization()
{
	if (CurrentPrefab.Get())
	{
		if (CurrentPrefab->BinaryData.Num() == 0)
			return false;
	}
	if (PreviewBounds.ContainsNaN())
	{
		UE_LOG(LGUIEditor, Warning, TEXT("[%s].%d Prefab:'%s' bounds is invalid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(CurrentPrefab->GetPathName()));
		return false;
	}
	return true;
}
void FLexUIPrefabThumbnailScene::GetViewMatrixParameters(const float InFOVDegrees, FVector& OutOrigin, float& OutOrbitPitch, float& OutOrbitYaw, float& OutOrbitZoom)const
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
void FLexUIPrefabThumbnailScene::SetPrefab(class ULexUIPrefab* Prefab)
{
	if (!CurrentPrefab.IsValid())
	{
		CurrentPrefab = nullptr;
		ClearOldActors();
	}
	if (CurrentPrefab.IsValid() && IsValid(Prefab))
	{
		if (CurrentPrefab == Prefab && !CurrentPrefab->bThumbnailDirty)
		{
			return;
		}
		ClearOldActors();
	}
	CurrentPrefab = Prefab;
	CurrentPrefab->bThumbnailDirty = false;
	if (IsValid(Prefab))
	{
		SpawnPreviewActor();
	}
}
USceneThumbnailInfo* FLexUIPrefabThumbnailScene::GetSceneThumbnailInfo(const float TargetDistance)const
{
	ULexUIPrefab* Prefab = CurrentPrefab.Get();
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