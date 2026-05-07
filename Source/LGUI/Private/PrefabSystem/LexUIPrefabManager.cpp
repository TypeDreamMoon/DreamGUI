// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "PrefabSystem/LexUIPrefabManager.h"
#include "LGUI.h"
#include "Engine/World.h"

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
void ULexUIPrefabWorldSubsystem::AddActorForPrefabSystem(ULexWidget* InActor, const FGuid& InSessionId)
{
	AllActors_PrefabSystemProcessing.Add(InActor, InSessionId);
}
void ULexUIPrefabWorldSubsystem::RemoveActorForPrefabSystem(ULexWidget* InActor, const FGuid& InSessionId)
{
	AllActors_PrefabSystemProcessing.Remove(InActor);
}
FGuid ULexUIPrefabWorldSubsystem::GetPrefabSystemSessionIdForActor(ULexWidget* InActor)
{
	if (auto FoundPtr = AllActors_PrefabSystemProcessing.Find(InActor))
	{
		return *FoundPtr;
	}
	return FGuid();
}

bool ULexUIPrefabWorldSubsystem::IsLexUIPrefabSystemProcessingActor(ULexWidget* InActor)
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
bool ULexUIPrefabWorldSubsystem::IsPrefabSystemProcessingActor(ULexWidget* InActor)
{
	return AllActors_PrefabSystemProcessing.Contains(InActor);
}


#undef LOCTEXT_NAMESPACE