// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/LexUIPrefabManager.h"
#include "LGUI.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabManagerObject"


ULexUIPrefabManagerObject* ULexUIPrefabManagerObject::Instance = nullptr;
ULexUIPrefabManagerObject::ULexUIPrefabManagerObject()
{
	
}
void ULexUIPrefabManagerObject::BeginDestroy()
{
	Super::BeginDestroy();
	Instance = nullptr;
}

#if WITH_EDITOR

UWorld* ULexUIPrefabManagerObject::GetPreviewWorldForPrefabPackage()
{
	if (!GEngine)return nullptr;
	if (Instance == nullptr)
	{
		Instance = NewObject<ULexUIPrefabManagerObject>();
		Instance->AddToRoot();
		UE_LOG(LGUI, Log, TEXT("[%s].%d No Instance for %s, create it!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(ULexUIPrefabManagerObject::StaticClass()->GetName()));
	}
	
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

bool ULexUIPrefabWorldSubsystem::IsLexUIPrefabSystemProcessingActor(AActor* InActor)
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