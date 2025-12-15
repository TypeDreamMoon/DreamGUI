// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/LGUIPrefabManager.h"
#include "LGUI.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#if WITH_EDITOR
#include "Editor.h"
#include "DrawDebugHelpers.h"
#include "Engine/Selection.h"
#include "EditorViewportClient.h"
#include "PrefabSystem/LGUIPrefab.h"
#endif

#define LOCTEXT_NAMESPACE "LGUIPrefabManagerObject"


ULGUIPrefabManagerObject* ULGUIPrefabManagerObject::Instance = nullptr;
ULGUIPrefabManagerObject::ULGUIPrefabManagerObject()
{
	
}
void ULGUIPrefabManagerObject::BeginDestroy()
{
	Super::BeginDestroy();
#if WITH_EDITORONLY_DATA
	if (OnAssetReimportDelegateHandle.IsValid())
	{
		if (GEditor)
		{
			if (auto ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
			{
				ImportSubsystem->OnAssetReimport.Remove(OnAssetReimportDelegateHandle);
			}
		}
	}
	if (OnMapOpenedDelegateHandle.IsValid())
	{
		FEditorDelegates::OnMapOpened.Remove(OnMapOpenedDelegateHandle);
	}
	if (OnPackageReloadedDelegateHandle.IsValid())
	{
		FCoreUObjectDelegates::OnPackageReloaded.Remove(OnPackageReloadedDelegateHandle);
	}
#endif
	Instance = nullptr;
}

void ULGUIPrefabManagerObject::Tick(float DeltaTime)
{

}
TStatId ULGUIPrefabManagerObject::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULGUIPrefabManagerObject, STATGROUP_Tickables);
}

#if WITH_EDITOR

ULGUIPrefabManagerObject* ULGUIPrefabManagerObject::GetInstance(bool CreateIfNotValid)
{
	if (CreateIfNotValid)
	{
		InitCheck();
	}
	return Instance;
}
bool ULGUIPrefabManagerObject::InitCheck()
{
	if (!GEngine)return false;
	if (Instance == nullptr)
	{
		Instance = NewObject<ULGUIPrefabManagerObject>();
		Instance->AddToRoot();
		UE_LOG(LGUI, Log, TEXT("[%s].%d No Instance for ULGUIPrefabManagerObject, create it!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		//open map
		Instance->OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddUObject(Instance, &ULGUIPrefabManagerObject::OnMapOpened);
		Instance->OnPackageReloadedDelegateHandle = FCoreUObjectDelegates::OnPackageReloaded.AddUObject(Instance, &ULGUIPrefabManagerObject::OnPackageReloaded);
		if (GEditor)
		{
			//reimport asset
			Instance->OnAssetReimportDelegateHandle = GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddUObject(Instance, &ULGUIPrefabManagerObject::OnAssetReimport);
		}
	}
	return true;
}

void ULGUIPrefabManagerObject::OnAssetReimport(UObject* asset)
{
	if (IsValid(asset))
	{
		auto textureAsset = Cast<UTexture2D>(asset);
		if (IsValid(textureAsset))
		{
			
		}
	}
}

void ULGUIPrefabManagerObject::OnMapOpened(const FString& FileName, bool AsTemplate)
{

}

void ULGUIPrefabManagerObject::OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (Phase == EPackageReloadPhase::PostBatchPostGC && Event != nullptr && Event->GetNewPackage() != nullptr)
	{
		auto Asset = Event->GetNewPackage()->FindAssetInPackage();
		if (auto PrefabAsset = Cast<ULGUIPrefab>(Asset))
		{
			
		}
	}
}

UWorld* ULGUIPrefabManagerObject::GetPreviewWorldForPrefabPackage()
{
	InitCheck();
	auto& PreviewScene = Instance->PreviewSceneForPrefabPackage;
	if (!PreviewScene)
	{
		PreviewScene = MakeUnique<FPreviewScene>();
	}
	return PreviewScene->GetWorld();
}

ULGUIPrefabManagerObject::FSerialize_SortChildrenActors ULGUIPrefabManagerObject::OnSerialize_SortChildrenActors;
ULGUIPrefabManagerObject::FDeserialize_Components ULGUIPrefabManagerObject::OnDeserialize_ProcessComponentsBeforeRerunConstructionScript;
ULGUIPrefabManagerObject::FPrefabEditorViewport_MouseClick ULGUIPrefabManagerObject::OnPrefabEditorViewport_MouseClick;
ULGUIPrefabManagerObject::FPrefabEditorViewport_MouseMove ULGUIPrefabManagerObject::OnPrefabEditorViewport_MouseMove;
ULGUIPrefabManagerObject::FPrefabEditor_CreateRootAgent ULGUIPrefabManagerObject::OnPrefabEditor_CreateRootAgent;
ULGUIPrefabManagerObject::FPrefabEditor_GetBounds ULGUIPrefabManagerObject::OnPrefabEditor_GetBounds;
ULGUIPrefabManagerObject::FPrefabEditor_SavePrefab ULGUIPrefabManagerObject::OnPrefabEditor_SavePrefab;
ULGUIPrefabManagerObject::FPrefabEditor_Refresh ULGUIPrefabManagerObject::OnPrefabEditor_Refresh;
ULGUIPrefabManagerObject::FPrefabEditor_ReplaceObjectPropertyForApplyOrRevert ULGUIPrefabManagerObject::OnPrefabEditor_ReplaceObjectPropertyForApplyOrRevert;
ULGUIPrefabManagerObject::FPrefabEditor_AfterObjectPropertyApplyOrRevert ULGUIPrefabManagerObject::OnPrefabEditor_AfterObjectPropertyApplyOrRevert;
ULGUIPrefabManagerObject::FPrefabEditor_AfterMakePrefabAsSubPrefab ULGUIPrefabManagerObject::OnPrefabEditor_AfterMakePrefabAsSubPrefab;
ULGUIPrefabManagerObject::FPrefabEditor_AfterCollectPropertyToOverride ULGUIPrefabManagerObject::OnPrefabEditor_AfterCollectPropertyToOverride;
ULGUIPrefabManagerObject::FPrefabEditor_CopyRootObjectParentAnchorData ULGUIPrefabManagerObject::OnPrefabEditor_CopyRootObjectParentAnchorData;
#endif


ULGUIPrefabWorldSubsystem* ULGUIPrefabWorldSubsystem::GetInstance(UWorld* World)
{
	return World->GetSubsystem<ULGUIPrefabWorldSubsystem>();
}
void ULGUIPrefabWorldSubsystem::BeginPrefabSystemProcessingActor(const FGuid& InSessionId)
{
	OnBeginDeserializeSession.Broadcast(InSessionId);
}
void ULGUIPrefabWorldSubsystem::EndPrefabSystemProcessingActor(const FGuid& InSessionId)
{
	OnEndDeserializeSession.Broadcast(InSessionId);
}
void ULGUIPrefabWorldSubsystem::AddActorForPrefabSystem(AActor* InActor, const FGuid& InSessionId)
{
	AllActors_PrefabSystemProcessing.Add(InActor, InSessionId);
}
void ULGUIPrefabWorldSubsystem::RemoveActorForPrefabSystem(AActor* InActor, const FGuid& InSessionId)
{
	AllActors_PrefabSystemProcessing.Remove(InActor);
}
FGuid ULGUIPrefabWorldSubsystem::GetPrefabSystemSessionIdForActor(AActor* InActor)
{
	if (auto FoundPtr = AllActors_PrefabSystemProcessing.Find(InActor))
	{
		return *FoundPtr;
	}
	return FGuid();
}

bool ULGUIPrefabWorldSubsystem::IsLGUIPrefabSystemProcessingActor(AActor* InActor)
{
	if (auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(InActor->GetWorld()))
	{
		if (PrefabManager->IsPrefabSystemProcessingActor(InActor))
		{
			return true;
		}
	}
	return false;
}
bool ULGUIPrefabWorldSubsystem::IsPrefabSystemProcessingActor(AActor* InActor)
{
	return AllActors_PrefabSystemProcessing.Contains(InActor);
}


#undef LOCTEXT_NAMESPACE