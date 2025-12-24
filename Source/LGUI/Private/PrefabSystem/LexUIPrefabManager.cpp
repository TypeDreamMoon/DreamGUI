// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/LexUIPrefabManager.h"
#include "LGUI.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Core/Actor/LexWidgetRootActor.h"
#if WITH_EDITOR
#include "Editor.h"
#include "DrawDebugHelpers.h"
#include "EditorViewportClient.h"
#include "PrefabSystem/LexUIPrefab.h"
#endif

#define LOCTEXT_NAMESPACE "LGUIPrefabManagerObject"


ULexUIPrefabManagerObject* ULexUIPrefabManagerObject::Instance = nullptr;
ULexUIPrefabManagerObject::ULexUIPrefabManagerObject()
{
	
}
void ULexUIPrefabManagerObject::BeginDestroy()
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

void ULexUIPrefabManagerObject::Tick(float DeltaTime)
{

}
TStatId ULexUIPrefabManagerObject::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULGUIPrefabManagerObject, STATGROUP_Tickables);
}

#if WITH_EDITOR

ULexUIPrefabManagerObject* ULexUIPrefabManagerObject::GetInstance(bool CreateIfNotValid)
{
	if (CreateIfNotValid)
	{
		InitCheck();
	}
	return Instance;
}
bool ULexUIPrefabManagerObject::InitCheck()
{
	if (!GEngine)return false;
	if (Instance == nullptr)
	{
		Instance = NewObject<ULexUIPrefabManagerObject>();
		Instance->AddToRoot();
		UE_LOG(LGUI, Log, TEXT("[%s].%d No Instance for %s, create it!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ULexUIPrefabManagerObject::StaticClass()->GetName()));
		//open map
		Instance->OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddUObject(Instance, &ULexUIPrefabManagerObject::OnMapOpened);
		Instance->OnPackageReloadedDelegateHandle = FCoreUObjectDelegates::OnPackageReloaded.AddUObject(Instance, &ULexUIPrefabManagerObject::OnPackageReloaded);
		if (GEditor)
		{
			//reimport asset
			Instance->OnAssetReimportDelegateHandle = GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddUObject(Instance, &ULexUIPrefabManagerObject::OnAssetReimport);
		}
	}
	return true;
}

void ULexUIPrefabManagerObject::OnAssetReimport(UObject* Asset)
{
	if (IsValid(Asset))
	{
		if (Asset->IsA<ULexUIPrefab>())
		{
			for (TObjectIterator<ALexWidgetRootActor> Itr; Itr; ++Itr)
			{
				if (Itr->GetWorld())
				{
					Itr->CheckPrefabVersion();
				}
			}
		}
	}
}

void ULexUIPrefabManagerObject::OnMapOpened(const FString& FileName, bool AsTemplate)
{

}

void ULexUIPrefabManagerObject::OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (Phase == EPackageReloadPhase::PostBatchPostGC && Event != nullptr && Event->GetNewPackage() != nullptr)
	{
		auto Asset = Event->GetNewPackage()->FindAssetInPackage();
		if (auto PrefabAsset = Cast<ULexUIPrefab>(Asset))
		{
			
		}
	}
}

UWorld* ULexUIPrefabManagerObject::GetPreviewWorldForPrefabPackage()
{
	InitCheck();
	auto& PreviewScene = Instance->PreviewSceneForPrefabPackage;
	if (!PreviewScene)
	{
		PreviewScene = MakeUnique<FPreviewScene>();
	}
	return PreviewScene->GetWorld();
}
#endif


ULexUIPrefabWorldSubsystem* ULexUIPrefabWorldSubsystem::GetInstance(UWorld* World)
{
	return World->GetSubsystem<ULexUIPrefabWorldSubsystem>();
}
void ULexUIPrefabWorldSubsystem::BeginPrefabSystemProcessingActor(const FGuid& InSessionId)
{
	OnBeginDeserializeSession.Broadcast(InSessionId);
}
void ULexUIPrefabWorldSubsystem::EndPrefabSystemProcessingActor(const FGuid& InSessionId)
{
	OnEndDeserializeSession.Broadcast(InSessionId);
}
void ULexUIPrefabWorldSubsystem::AddActorForPrefabSystem(AActor* InActor, const FGuid& InSessionId)
{
	AllActors_PrefabSystemProcessing.Add(InActor, InSessionId);
}
void ULexUIPrefabWorldSubsystem::RemoveActorForPrefabSystem(AActor* InActor, const FGuid& InSessionId)
{
	AllActors_PrefabSystemProcessing.Remove(InActor);
}
FGuid ULexUIPrefabWorldSubsystem::GetPrefabSystemSessionIdForActor(AActor* InActor)
{
	if (auto FoundPtr = AllActors_PrefabSystemProcessing.Find(InActor))
	{
		return *FoundPtr;
	}
	return FGuid();
}

bool ULexUIPrefabWorldSubsystem::IsLGUIPrefabSystemProcessingActor(AActor* InActor)
{
	if (auto PrefabManager = ULexUIPrefabWorldSubsystem::GetInstance(InActor->GetWorld()))
	{
		if (PrefabManager->IsPrefabSystemProcessingActor(InActor))
		{
			return true;
		}
	}
	return false;
}
bool ULexUIPrefabWorldSubsystem::IsPrefabSystemProcessingActor(AActor* InActor)
{
	return AllActors_PrefabSystemProcessing.Contains(InActor);
}


#undef LOCTEXT_NAMESPACE