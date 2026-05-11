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
void ULexUIPrefabWorldSubsystem::BeginPrefabSystemProcessingWidget(const FGuid& InSessionId)
{
	OnBeginDeserializeSession.Broadcast(InSessionId);
}
void ULexUIPrefabWorldSubsystem::EndPrefabSystemProcessingWidget(const FGuid& InSessionId)
{
	OnEndDeserializeSession.Broadcast(InSessionId);
}
void ULexUIPrefabWorldSubsystem::AddWidgetForPrefabSystem(ULexWidget* InWidget, const FGuid& InSessionId)
{
	AllActors_PrefabSystemProcessing.Add(InWidget, InSessionId);
}
void ULexUIPrefabWorldSubsystem::RemoveWidgetForPrefabSystem(ULexWidget* InWidget)
{
	AllActors_PrefabSystemProcessing.Remove(InWidget);
}
FGuid ULexUIPrefabWorldSubsystem::GetPrefabSystemSessionIdForWidget(ULexWidget* InWidget)
{
	if (auto FoundPtr = AllActors_PrefabSystemProcessing.Find(InWidget))
	{
		return *FoundPtr;
	}
	return FGuid();
}

bool ULexUIPrefabWorldSubsystem::IsLexUIPrefabSystemProcessingWidget(UObject* WorldContextObject, ULexWidget* InWidget)
{
	if (auto World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		if (auto PrefabManager = ULexUIPrefabWorldSubsystem::GetInstance(World))
		{
			if (PrefabManager->IsPrefabSystemProcessingWidget(InWidget))
			{
				return true;
			}
		}
	}
	return false;
}
bool ULexUIPrefabWorldSubsystem::IsPrefabSystemProcessingWidget(ULexWidget* InWidget)
{
	return AllActors_PrefabSystemProcessing.Contains(InWidget);
}


#undef LOCTEXT_NAMESPACE