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
	// With OnAssetSave frequency this only runs from the package-save path (a standalone canvas on
	// the game thread), never from the content browser's on-demand tile ticks -- those read the
	// thumbnail this render leaves cached in the package.
	if (auto Prefab = Cast<UDreamUIPrefab>(Object))
	{
		if (!ThumbnailScene.IsValid())
		{
			ThumbnailScene = MakeUnique<FDreamUIPrefabThumbnailScene>();
		}
		ThumbnailScene->SetPrefab(Prefab);
		if (!ThumbnailScene->IsValidForVisualization())
		{
			ThumbnailScene->ClearPrefab();
			return;
		}

		FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(RenderTarget, ThumbnailScene->GetScene(), FEngineShowFlags(ESFIM_Game))
			.SetTime(UThumbnailRenderer::GetTime()));

		ViewFamily.EngineShowFlags.DisableAdvancedFeatures();
		ViewFamily.EngineShowFlags.MotionBlur = 0;

		auto View = ThumbnailScene->CreateView(&ViewFamily, X, Y, Width, Height);
		RenderViewFamily(Canvas, &ViewFamily, View);

		//draw prefab icon (baked into the saved thumbnail)
		static FString DreamGUIBasePath = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"))->GetBaseDir();
		FDreamUIEditorUtils::DrawThumbnailIcon(DreamGUIBasePath + (Prefab->GetIsPrefabVariant() ? TEXT("/Resources/Icons/PrefabVariant_40x.png") : TEXT("/Resources/Icons/Prefab_40x.png"))
			, X, Y, Width, Height, Canvas);

		// The view family render is already enqueued, and the scene teardown's render-thread deletes
		// queue behind it, so tearing down here is ordered correctly.
		ThumbnailScene->ClearPrefab();
	}
}
EThumbnailRenderFrequency UDreamUIPrefabThumbnailRenderer::GetThumbnailRenderFrequency(UObject* Object) const
{
	return EThumbnailRenderFrequency::OnAssetSave;
}
void UDreamUIPrefabThumbnailRenderer::BeginDestroy()
{
	ThumbnailScene.Reset();
	Super::BeginDestroy();
}
