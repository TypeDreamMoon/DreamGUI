// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Actor/LexWidgetPresenterComponent.h"

#include "EngineUtils.h"
#include "LGUI.h"
#include "Core/LexUIManager.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexWidget.h"
#include "Event/LexEventSystem.h"
#include "Event/LexWorldSpaceRaycasterBase.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "PrefabSystem/LexUIPrefab.h"

#define LOCTEXT_NAMESPACE "LexWidgetRootActor"

ULexWidgetPresenterComponent::ULexWidgetPresenterComponent()
{
	RootWidget = CreateDefaultSubobject<ULexWidget>(FName("RootWidget"));
	RootWidget->SetSizeDelta(FVector2D(1920, 1080));
	RootWidget->SetDisplayName(TEXT("[RootAgent]"));
	Canvas = RootWidget->AddComponent<ULexCanvas>();
	Canvas->SetWidgetPresenterComponent(this);

	NavigationSelectionPrefab = LoadObject<ULexUIPrefab>(NULL, TEXT("/LGUI/Prefabs/NavigationSelectionInputHandler"));
}

void ULexWidgetPresenterComponent::BeginPlay()
{
	Super::BeginPlay();
	RootWidget->BeginPlay();
}

void ULexWidgetPresenterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	RootWidget->EndPlay();
}

void ULexWidgetPresenterComponent::OnRegister()
{
	Super::OnRegister();
	ULexUIManagerWorldSubsystem::AddWidgetPresenter(this);
	RootWidget->OnRegister();
	LoadPrefab();
}

void ULexWidgetPresenterComponent::OnUnregister()
{
	Super::OnUnregister();
	RootWidget->OnUnregister();
	ULexUIManagerWorldSubsystem::RemoveWidgetPresenter(this);
}

void ULexWidgetPresenterComponent::LoadPrefab()
{
	if (LoadedWidget.IsValid())
	{
		LoadedWidget->DestroyWidget();
		LoadedWidget = nullptr;
	}
	if (IsValid(WidgetPrefab))
	{
		LoadedWidget = WidgetPrefab->LoadPrefab(this->GetWorld(), this, RootWidget);
#if WITH_EDITOR
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
void ULexWidgetPresenterComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property != nullptr)
	{
	}
}

#include "Dialog/SCustomDialog.h"
bool ULexWidgetPresenterComponent::bNeedCheckEventSystem = true;
bool ULexWidgetPresenterComponent::bNeverCheckEventSystem = false;
bool ULexWidgetPresenterComponent::bNeedCheckRaycasterSource = true;
bool ULexWidgetPresenterComponent::bNeverCheckRaycasterSource = false;
void ULexWidgetPresenterComponent::CheckNecessaryObjects()
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
							auto ClassName = TEXT("LexEventSystemActor_EnhancedInput");
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
				if (auto WorldSpaceRaycaster = this->GetOwner()->FindComponentByClass<ULexWorldSpaceRaycasterBase>())
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

void ULexWidgetPresenterComponent::MarkNeedCheckNecessaryObjects()
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

void ULexWidgetPresenterComponent::CheckPrefabVersion()
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
		if (LoadedWidget.IsValid())
		{
			LoadedWidget->DestroyWidget();
			LoadedWidget = nullptr;
		}
	}
}
#endif

void ULexWidgetPresenterComponent::SetPrefab(ULexUIPrefab* Value)
{
	if (WidgetPrefab != Value)
	{
		WidgetPrefab = Value;
		LoadPrefab();
	}
}

UUINavigationInputSelectionHandler* ULexWidgetPresenterComponent::GetNavigationSelection()
{
	if (!NavigationSelection.IsValid())
	{
		if (auto Widget = NavigationSelectionPrefab->LoadPrefab(this->GetWorld(), this, this->RootWidget.Get()))
		{
			NavigationSelection = Widget->GetComponent<UUINavigationInputSelectionHandler>();
		}
	}
	return NavigationSelection.Get();
}

#undef LOCTEXT_NAMESPACE
