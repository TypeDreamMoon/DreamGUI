// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "PrefabSystem/DreamUIPrefabInstanceScene.h"
#include "DreamGUI.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "Components/DirectionalLightComponent.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/TextureCube.h"
#include "Event/DreamScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceConstant.h"

#if WITH_EDITOR

#define LOCTEXT_NAMESPACE "DreamUIPrefabInstanceScene"

const FString FDreamUIPrefabInstanceScene::RootAgentActorName = TEXT("[temporary_RootAgent]");

FDreamUIPrefabInstanceScene::FDreamUIPrefabInstanceScene(ConstructionValues CVS) :FDreamUIPrefabScene(CVS)
{
	//GetWorld()->GetWorldSettings()->NotifyBeginPlay();
	//GetWorld()->GetWorldSettings()->NotifyMatchStarted();
	//GetWorld()->GetWorldSettings()->SetActorHiddenInGame(false);
	//GetWorld()->bBegunPlay = true;

	// This is all from the AnimationEditorPreviewScene.cpp
	// set light options
	DirectionalLight->SetRelativeLocation(FVector(-1024.f, 1024.f, 2048.f));
	DirectionalLight->SetRelativeScale3D(FVector(15.f));
	DirectionalLight->Mobility = EComponentMobility::Movable;
	DirectionalLight->DynamicShadowDistanceStationaryLight = 3000.f;

	SetLightBrightness(4.f);

	DirectionalLight->InvalidateLightingCache();
	DirectionalLight->RecreateRenderState_Concurrent();

	if (IsRunningCookCommandlet())
		return;
	// A background sky sphere
	{
		// Large scale to prevent sphere from clipping
		const FTransform SphereTransform(FRotator(0, 0, 0), FVector(0, 0, 0), FVector(2000));
		auto SkyComponent = NewObject<UStaticMeshComponent>(GetTransientPackage());

		// Set up sky sphere showing the same cube map as used by the sky light
		UStaticMesh* SkySphere = LoadObject<UStaticMesh>(NULL, TEXT("/Engine/EditorMeshes/AssetViewer/Sphere_inversenormals.Sphere_inversenormals"), NULL, LOAD_None, NULL);
		check(SkySphere);
		SkyComponent->SetStaticMesh(SkySphere);
		SkyComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SkyComponent->CastShadow = false;
		SkyComponent->bCastDynamicShadow = false;

		UMaterial* SkyMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EditorMaterials/AssetViewer/M_SkyBox.M_SkyBox"), NULL, LOAD_None, NULL);
		check(SkyMaterial);

		auto InstancedSkyMaterial = NewObject<UMaterialInstanceConstant>(GetTransientPackage());
		InstancedSkyMaterial->Parent = SkyMaterial;		

		UTextureCube* DefaultTexture = LoadObject<UTextureCube>(NULL, TEXT("/Engine/EditorMaterials/AssetViewer/EpicQuadPanorama_CC+EV1.EpicQuadPanorama_CC+EV1"));
    
		InstancedSkyMaterial->SetTextureParameterValueEditorOnly(FName("SkyBox"), DefaultTexture );
		InstancedSkyMaterial->SetScalarParameterValueEditorOnly(FName("CubemapRotation"), 0);
		InstancedSkyMaterial->SetScalarParameterValueEditorOnly(FName("Intensity"), 1);
		InstancedSkyMaterial->PostLoad();
		SkyComponent->SetMaterial(0, InstancedSkyMaterial);
		AddComponent(SkyComponent, SphereTransform);
		SkySphereComponent = SkyComponent;
	}
}

FDreamUIPrefabInstanceScene::~FDreamUIPrefabInstanceScene()
{
	if (RootAgentWidget != nullptr)
	{
		RootAgentWidget->DestroyWidget();
		RootAgentWidget.Reset();
	}
}

UDreamWidget* FDreamUIPrefabInstanceScene::GetParentForLoadPrefab(UDreamUIPrefab* InPrefab)
{
	if (RootAgentWidget != nullptr)
	{
		return RootAgentWidget.Get();
	}
	if (!IsValid(InPrefab))return nullptr;
	auto Prefab = InPrefab;
	if (InPrefab->GetIsPrefabVariant())
	{
		auto RootSubPrefab = InPrefab;
		TSet<const UDreamUIPrefab*> VisitedVariants;
		while (RootSubPrefab->GetIsPrefabVariant())
		{
			if (VisitedVariants.Contains(RootSubPrefab))
			{
				UE_LOG(DreamGUI, Error, TEXT("Circular prefab variant reference detected while opening '%s'."), *InPrefab->GetPathName());
				return nullptr;
			}
			VisitedVariants.Add(RootSubPrefab);
			if (RootSubPrefab->ReferenceAssetList.Num() <= 0)
			{
				return nullptr;
			}
			RootSubPrefab = Cast<UDreamUIPrefab>(RootSubPrefab->ReferenceAssetList[0]);
			if (!RootSubPrefab)
			{
				return nullptr;
			}
		}
		Prefab = RootSubPrefab;
	}
	
	// The design canvas size belongs to the asset being edited: a variant saves its own
	// CanvasSize (SetDesignerCanvasSize / SaveEditorState), so reading the walked base's value
	// here would snap the canvas back to the base size on every reopen.
	auto CanvasSize = InPrefab->PrefabDataForPrefabEditor.CanvasSize;
	//create Canvas for UI
	auto RootWidget = NewObject<UDreamWidget>(this->GetWorld(), FName("[RootAgent]"));
	RootWidget->SetSizeDelta(CanvasSize);
	RootWidget->SetDisplayName(RootAgentActorName);
	RootWidget->OnRegister();
	RootAgentWidget = TStrongObjectPtr(RootWidget);
	auto Canvas = RootWidget->AddComponent<UDreamCanvas>();
	
	auto RenderMode = (EDreamRenderMode)Prefab->PrefabDataForPrefabEditor.CanvasRenderMode;
	Canvas->SetRenderMode(RenderMode);
	Canvas->bFixedSizeInEditMode = true;
	// The design viewport size is what the editor is simulating, so the edit-mode fixed size must
	// be it and not the 1920x1080 class default -- otherwise a screen-space preview would resize
	// the agent to the default on its first editor tick.
	{
		const FIntPoint DesignViewportSize = InPrefab->PrefabDataForPrefabEditor.DesignViewportSize;
		Canvas->SizeInEditMode = (DesignViewportSize.X > 0 && DesignViewportSize.Y > 0) ? DesignViewportSize : CanvasSize;
	}
	return RootWidget;
}

void FDreamUIPrefabInstanceScene::SetSkyCubeVisibility(bool bVisible)
{
	SkySphereComponent->SetVisibility(bVisible);
}

#undef LOCTEXT_NAMESPACE

#endif
