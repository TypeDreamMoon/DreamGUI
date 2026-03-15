// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/LexUIPrefabInstanceScene.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "Components/DirectionalLightComponent.h"
#include "Core/Actor/LexWidgetActor.h"
#include "Core/Components/LexWidget.h"
#include "Engine/TextureCube.h"
#include "Event/LexScreenSpaceRaycaster.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceConstant.h"

#if WITH_EDITOR

#define LOCTEXT_NAMESPACE "LexUIPrefabInstanceScene"

const FString FLexUIPrefabInstanceScene::RootAgentActorName = TEXT("[temporary_RootAgent]");

FLexUIPrefabInstanceScene::FLexUIPrefabInstanceScene(ConstructionValues CVS) :FLexUIPrefabScene(CVS)
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

USceneComponent* FLexUIPrefabInstanceScene::GetParentComponentForPrefab(ULexUIPrefab* InPrefab)
{
	if (RootAgentActor != nullptr)
	{
		return RootAgentActor->GetRootComponent();
	}
	if (!IsValid(InPrefab))return nullptr;
	auto Prefab = InPrefab;
	if (InPrefab->GetIsPrefabVariant())
	{
		auto RootSubPrefab = InPrefab;
		while (RootSubPrefab->GetIsPrefabVariant())
		{
			if (RootSubPrefab->ReferenceAssetList.Num() <= 0)
			{
				return nullptr;
			}
			RootSubPrefab = Cast<ULexUIPrefab>(RootSubPrefab->ReferenceAssetList[0]);
			if (!RootSubPrefab)
			{
				return nullptr;
			}
		}
		Prefab = RootSubPrefab;
	}
	
	auto CanvasSize = Prefab->PrefabDataForPrefabEditor.CanvasSize;
	//create Canvas for UI
	auto RootUICanvasActor = this->GetWorld()->SpawnActor<ALexWidgetActor>(ALexWidgetActor::StaticClass(), FTransform::Identity);
	RootUICanvasActor->GetRootComponent()->SetWorldLocationAndRotationNoPhysics(FVector::ZeroVector, FRotator(0, 0, 0));
	{
		auto RenderMode = (ELexRenderMode)Prefab->PrefabDataForPrefabEditor.CanvasRenderMode;
		auto CanvasComp = NewObject<ULexCanvas>(RootUICanvasActor, TEXT("LexCanvas"));
		CanvasComp->RegisterComponent();
		RootUICanvasActor->AddInstanceComponent(CanvasComp);
		CanvasComp->SetRenderMode(RenderMode);
		CanvasComp->bFixedSizeInEditMode = true;
	}

	RootUICanvasActor->GetLexWidget()->SetWidth(CanvasSize.X);
	RootUICanvasActor->GetLexWidget()->SetHeight(CanvasSize.Y);
	RootUICanvasActor->GetLexWidget()->SetSiblingIndex(0);

	RootAgentActor = RootUICanvasActor;

	//set properties
	RootAgentActor->SetLockLocation(true);
	RootAgentActor->SetActorLabel(*RootAgentActorName);
	RootAgentActor->GetRootComponent()->SetWorldLocationAndRotationNoPhysics(FVector::ZeroVector, FRotator(0, 0, 0));

	return RootAgentActor->GetRootComponent();
}

void FLexUIPrefabInstanceScene::SetSkyCubeVisibility(bool bVisible)
{
	SkySphereComponent->SetVisibility(bVisible);
}

#undef LOCTEXT_NAMESPACE

#endif
