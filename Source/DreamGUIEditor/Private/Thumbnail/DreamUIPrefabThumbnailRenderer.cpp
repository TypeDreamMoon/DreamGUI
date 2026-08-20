// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Thumbnail/DreamUIPrefabThumbnailRenderer.h"
#include "RendererInterface.h"
#include "SceneView.h"
#include "Engine/EngineTypes.h"
#include "DreamUIEditorUtils.h"
#include "Interfaces/IPluginManager.h"
#include "PrefabSystem/DreamUIPrefab.h"

UDreamUIPrefabThumbnailRenderer::UDreamUIPrefabThumbnailRenderer()
{

}

bool UDreamUIPrefabThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
	if (Object->IsA(UDreamUIPrefab::StaticClass()))
		return true;
	return false;
}
void UDreamUIPrefabThumbnailRenderer::Draw(UObject* Object, int32 X, int32 Y, uint32 Width, uint32 Height, FRenderTarget* RenderTarget, FCanvas* Canvas, bool bAdditionalViewFamily)
{
	if (auto Prefab = Cast<UDreamUIPrefab>(Object))
	{
		TSharedRef<FDreamUIPrefabThumbnailScene> ThumbnailScene = ThumbnailScenes.EnsureThumbnailScene(Prefab->GetPathName());
		ThumbnailScene->SetPrefab(Prefab);
		if (!ThumbnailScene->IsValidForVisualization())
			return;

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(RenderTarget, ThumbnailScene->GetScene(), FEngineShowFlags(ESFIM_Game))
			.SetTime(UThumbnailRenderer::GetTime()));

		ViewFamily.EngineShowFlags.DisableAdvancedFeatures();
		ViewFamily.EngineShowFlags.MotionBlur = 0;

		auto View = ThumbnailScene->CreateView(&ViewFamily, X, Y, Width, Height);
		RenderViewFamily(Canvas, &ViewFamily, View);

		//draw prefab icon
		static FString DreamGUIBasePath = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"))->GetBaseDir();
		FDreamUIEditorUtils::DrawThumbnailIcon(DreamGUIBasePath + (Prefab->GetIsPrefabVariant() ? TEXT("/Resources/Icons/PrefabVariant_40x.png") : TEXT("/Resources/Icons/Prefab_40x.png"))
			, X, Y, Width, Height, Canvas);

		Prefab->bThumbnailDirty = false;
	}
}
void UDreamUIPrefabThumbnailRenderer::BeginDestroy()
{
	ThumbnailScenes.Clear();
	Super::BeginDestroy();
}