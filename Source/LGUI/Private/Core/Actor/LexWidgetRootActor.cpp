// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Actor/LexWidgetRootActor.h"

#include "EngineUtils.h"
#include "LGUI.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Event/LexEventSystem.h"
#include "Event/LexWorldSpaceRaycasterBase.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "Utils/LexUIUtils.h"

#define LOCTEXT_NAMESPACE "LexWidgetRootActor"

ALexWidgetRootActor::ALexWidgetRootActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Canvas = CreateDefaultSubobject<ULexCanvas>(FName("Canvas"));
	LexWidget->SetSizeDelta(FVector2D(1920, 1080));
}

void ALexWidgetRootActor::BeginPlay()
{
	Super::BeginPlay();
	
}

void ALexWidgetRootActor::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	LoadPrefab();
}

void ALexWidgetRootActor::LoadPrefab()
{
	if (LoadedActor.IsValid())
	{
		FLexUIUtils::DestroyActorWithHierarchy(LoadedActor.Get());
		LoadedActor = nullptr;
	}
	if (IsValid(WidgetPrefab))
	{
		LoadedActor = WidgetPrefab->LoadPrefab(this->GetWorld(), this->GetRootComponent());
#if WITH_EDITOR
		TArray<AActor*> AllLoadedActors;
		FLexUIUtils::CollectChildrenActors(LoadedActor.Get(), AllLoadedActors);
		bool bIsGameWorld = this->GetWorld()->IsGameWorld();
		for (AActor* Actor : AllLoadedActors)
		{
			if (!bIsGameWorld)
				Actor->SetFlags(RF_Transient);//set transient in edt mode because we don't want to save these actors in level, not set in game mode because no need to, and LexUIDuplicateActor need none-transient
			auto bListedInSceneOutliner_Property = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bListedInSceneOutliner"));
			bListedInSceneOutliner_Property->SetPropertyValue_InContainer(Actor, bListInSceneOutliner);
		}
		OverallVersionMD5 = WidgetPrefab->GenerateOverallVersionMD5();//store version for auto update
#endif
	}
#if WITH_EDITOR
	auto World = GetWorld();
	if (World && World->WorldType != EWorldType::EditorPreview && !World->IsGameWorld())//Edit mode and not BlueprintEditorPreview
	{
		ULexUIManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]()
		{
			if (WeakThis.IsValid())
			{
				WeakThis->CheckNecessaryObjects();
			}
		}, 1);
	}
#endif
}

#if WITH_EDITOR
void ALexWidgetRootActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property != nullptr)
	{
		auto MemberName = PropertyChangedEvent.GetMemberPropertyName();
		if (MemberName == GET_MEMBER_NAME_CHECKED(ALexWidgetRootActor, bListInSceneOutliner))
		{
			ApplyListInSceneOutliner();
		}
	}
}

void ALexWidgetRootActor::CheckNecessaryObjects()
{
	//check if there is EventSystem in editor
	bool bEventSystemExits = false;
	for (TActorIterator<AActor> ActorItr(this->GetWorld()); ActorItr; ++ActorItr)
	{
		auto Actor = *ActorItr;
		if (Actor->FindComponentByClass<ULexEventSystem>())
		{
			bEventSystemExits = true;
			break;
		}
	}
	if (!bEventSystemExits)
	{
		auto MsgReturn = FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::YesNo, LOCTEXT("MissingEventSystem", "There is no LexEventSystem in the world! LexUI will not interactable without LexEventSystem, would you like to create a default one?"));
		if (MsgReturn == EAppReturnType::Yes)
		{
			auto ClassName = TEXT("LexEventSystemActor");
			if (auto ActorClass = LoadObject<UClass>(NULL, *FString::Printf(TEXT("/LGUI/Blueprints/%s.%s_C"), ClassName, ClassName)))
			{
				auto Actor = this->GetWorld()->SpawnActor<AActor>(ActorClass);
				Actor->SetActorLabel(ClassName);
			}
			else
			{
				UE_LOG(LGUI, Error, TEXT("[%s].%d Load %s error! Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), 
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ClassName);
			}
		}
	}
	//check if there is WorldSpaceRaycaster if this is WorldSpace UI
	if (this->Canvas->GetRenderMode() == ELexRenderMode::WorldSpace || this->Canvas->GetRenderMode() == ELexRenderMode::WorldSpace_LexUI)
	{
		ULexWorldSpaceRaycasterSource* ExistWorldSpaceRaycasterSource = nullptr;
		for (TActorIterator<AActor> ActorItr(this->GetWorld()); ActorItr; ++ActorItr)
		{
			auto Actor = *ActorItr;
			if (auto Comp = Actor->FindComponentByClass<ULexWorldSpaceRaycasterSource>())
			{
				ExistWorldSpaceRaycasterSource = Comp;
				break;
			}
		}
		if (!ExistWorldSpaceRaycasterSource)
		{
			auto MsgReturn = FMessageDialog::Open(EAppMsgCategory::Warning, EAppMsgType::YesNo, LOCTEXT("MissingWorldSpaceRaycasterSource", "There is no WorldSpaceRaycasterSource in the world! WorldSpaceUI will not interactable without WorldSpaceRaycasterSource, would you like to create a default one which use MouseInput?"));
			if (MsgReturn == EAppReturnType::Yes)
			{
				auto ClassName = TEXT("LexWorldSpaceRaycasterSource_Mouse");
				if (auto ActorClass = LoadObject<UClass>(NULL, *FString::Printf(TEXT("/LGUI/Blueprints/%s.%s_C"), ClassName, ClassName)))
				{
					auto Actor = this->GetWorld()->SpawnActor<AActor>(ActorClass);
					Actor->SetActorLabel(ClassName);
					ExistWorldSpaceRaycasterSource = Actor->FindComponentByClass<ULexWorldSpaceRaycasterSource>();
				}
				else
				{
					UE_LOG(LGUI, Error, TEXT("[%s].%d Load %s error! Missing some content of LexUI plugin, reinstall this plugin may fix the issue."), 
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, ClassName);
				}
			}
		}
		if (ExistWorldSpaceRaycasterSource)
		{
			if (auto WorldSpaceRaycaster = this->FindComponentByClass<ULexWorldSpaceRaycasterBase>())
			{
				if (auto RaycasterSourceActor = Cast<ALexWorldSpaceRaycasterSourceActor>(ExistWorldSpaceRaycasterSource->GetOwner()))
				{
					WorldSpaceRaycaster->SetRaycasterSourceActor(RaycasterSourceActor);
				}
			}
		}
	}
}

void ALexWidgetRootActor::ApplyListInSceneOutliner()
{
	if (!LoadedActor.IsValid())return;
	TArray<AActor*> AllLoadedActors;
	FLexUIUtils::CollectChildrenActors(LoadedActor.Get(), AllLoadedActors, false);
	for (AActor* Actor : AllLoadedActors)
	{
		auto bListedInSceneOutliner_Property = FindFProperty<FBoolProperty>(AActor::StaticClass(), TEXT("bListedInSceneOutliner"));
		bListedInSceneOutliner_Property->SetPropertyValue_InContainer(Actor, bListInSceneOutliner);
	}
	ULexUIManagerObject::MarkBroadcastLevelActorListChanged();
}

void ALexWidgetRootActor::CheckPrefabVersion()
{
	if (OverallVersionMD5 != WidgetPrefab->GenerateOverallVersionMD5())
	{
		LoadPrefab();
	}
}
#endif

void ALexWidgetRootActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ALexWidgetRootActor::SetPrefab(ULexUIPrefab* Value)
{
	if (WidgetPrefab != Value)
	{
		WidgetPrefab = Value;
		LoadPrefab();
	}
}

#undef LOCTEXT_NAMESPACE
