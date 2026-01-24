// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Actor/LexWidgetRootActor.h"

#include "EngineUtils.h"
#include "LGUI.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Event/LexEventSystem.h"
#include "Event/LexWorldSpaceRaycasterBase.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "Utils/LexUIUtils.h"

#define LOCTEXT_NAMESPACE "LexWidgetRootActor"

ALexWidgetRootActor::ALexWidgetRootActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Canvas = CreateDefaultSubobject<ULexCanvas>(FName("Canvas"));
	LexWidget->SetSizeDelta(FVector2D(1920, 1080));

	NavigationSelectionPrefab = LoadObject<ULexUIPrefab>(NULL, TEXT("/LGUI/Prefabs/NavigationSelectionInputHandler"));
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
	if (!bIsSpawnFromPrefabFactory)//if spawn from prefab-factory then the "CheckNecessaryObjects" is handled from there
	{
		auto World = GetWorld();
		if (World && World->WorldType != EWorldType::EditorPreview && !World->IsGameWorld())//Edit mode and not BlueprintEditorPreview
		{
			ULexUIManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]()
			{
				if (WeakThis.IsValid())
				{
					WeakThis->CheckNecessaryObjects();
					MarkNeedCheckNecessaryObjects();
				}
			}, 1);
		}
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

#include "Dialog/SCustomDialog.h"
bool ALexWidgetRootActor::bNeedCheckEventSystem = true;
bool ALexWidgetRootActor::bNeverCheckEventSystem = false;
bool ALexWidgetRootActor::bNeedCheckRaycasterSource = true;
bool ALexWidgetRootActor::bNeverCheckRaycasterSource = false;
void ALexWidgetRootActor::CheckNecessaryObjects()
{
	if (bNeedCheckEventSystem)
	{
		bNeedCheckEventSystem = false;
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
			auto Dialog =
				SNew(SCustomDialog)
				.Title(LOCTEXT("MessageDialogTitle", "Message"))
				.Content()
				[
					SNew(SBox)
					.Padding(20, 10)
					.MaxDesiredWidth(500)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("MissingEventSystem", "There is no LexEventSystem in the world! LexUI will not interactable without LexEventSystem, would you like to create a default one?"))
					]
				]
				.Buttons({
					SCustomDialog::FButton(
						LOCTEXT("DialogBtnYes", "Yes"),
						FSimpleDelegate::CreateLambda([=, this]()
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
						})),
					SCustomDialog::FButton(
						LOCTEXT("DialogBtnNo", "No")),
					SCustomDialog::FButton(
						LOCTEXT("DialogBtnNoToAll", "NoAndNeverShowAgain"),
						FSimpleDelegate::CreateLambda([=]()
						{
							bNeverCheckEventSystem = true;
						}))
				});
			Dialog->ShowModal();
		}
	}
	if (bNeedCheckRaycasterSource)
	{
		bNeedCheckRaycasterSource = false;
		//check if there is WorldSpaceRaycaster when this is WorldSpace UI
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
				auto Dialog =
				SNew(SCustomDialog)
				.Title(LOCTEXT("MessageDialogTitle", "Message"))
				.Content()
				[
					SNew(SBox)
					.Padding(20, 10)
					.MaxDesiredWidth(500)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("MissingWorldSpaceRaycasterSource", "There is no WorldSpaceRaycasterSource in the world! WorldSpaceUI will not interactable without WorldSpaceRaycasterSource, would you like to create a default one which use mouse input?"))
					]
				]
				.Buttons({
					SCustomDialog::FButton(
						LOCTEXT("DialogBtnYes", "Yes"),
						FSimpleDelegate::CreateLambda([=, &ExistWorldSpaceRaycasterSource, this]()
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
						})),
					SCustomDialog::FButton(
						LOCTEXT("DialogBtnNo", "No")),
					SCustomDialog::FButton(
						LOCTEXT("DialogBtnNoToAll", "NoAndNeverShowAgain"),
						FSimpleDelegate::CreateLambda([=]()
						{
							bNeverCheckRaycasterSource = true;
						}))
				});
				Dialog->ShowModal();
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

void ALexWidgetRootActor::MarkNeedCheckNecessaryObjects()
{
	if (!bNeverCheckEventSystem)
	{
		bNeedCheckEventSystem = true;
	}
	if (!bNeverCheckRaycasterSource)
	{
		bNeedCheckRaycasterSource = true;
	}
}

void ALexWidgetRootActor::CheckPrefabVersion()
{
	if (IsValid(WidgetPrefab))
	{
		if (OverallVersionMD5 != WidgetPrefab->GenerateOverallVersionMD5())
		{
			LoadPrefab();
		}
	}
	else
	{
		if (LoadedActor.IsValid())
		{
			FLexUIUtils::DestroyActorWithHierarchy(LoadedActor.Get());
			LoadedActor = nullptr;
		}
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

UUINavigationInputSelectionHandler* ALexWidgetRootActor::GetNavigationSelection()
{
	if (!NavigationSelection.IsValid())
	{
		if (auto WidgetActor = Cast<ALexWidgetActor>(NavigationSelectionPrefab->LoadPrefab(this->GetWorld(), this->GetLexWidget())))
		{
			NavigationSelection = WidgetActor->FindComponentByClass<UUINavigationInputSelectionHandler>();
		}
	}
	return NavigationSelection.Get();
}

#undef LOCTEXT_NAMESPACE
