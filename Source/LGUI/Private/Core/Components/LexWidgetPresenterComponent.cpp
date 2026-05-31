// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexWidgetPresenterComponent.h"

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
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	
	bWantsOnUpdateTransform = true;

	CanvasTemplate = CreateDefaultSubobject<ULexCanvas>(TEXT("CanvasTemplate"));
	
	NavigationSelectionPrefab = LoadObject<ULexUIPrefab>(NULL, TEXT("/LGUI/Prefabs/NavigationSelectionInputHandler"));
}

void ULexWidgetPresenterComponent::BeginPlay()
{
	Super::BeginPlay();
}

void ULexWidgetPresenterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ULexWidgetPresenterComponent::OnRegister()
{
	Super::OnRegister();
	LoadPrefab();//load prefab when OnRegister in edit mode
}

void ULexWidgetPresenterComponent::OnUnregister()
{
	bool bIsEditMode = false;
	if (auto World = GetWorld())
	{
		if (!World->IsGameWorld())
		{
			bIsEditMode = true;
		}
	}
	if (bIsEditMode)
	{
		if (LoadedWidget.IsValid())
		{
			LoadedWidget->DestroyWidget();
			LoadedWidget = nullptr;
		}
	}
	Super::OnUnregister();
}

void ULexWidgetPresenterComponent::PostLoad()
{
	Super::PostLoad();
}

void ULexWidgetPresenterComponent::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	if (Ar.HasAllPortFlags(PPF_DuplicateForPIE))
	{
		// PIE duplication should just work normally
		Ar << CanvasTemplate;
	}
	else if (Ar.HasAllPortFlags(PPF_Duplicate))
	{
		if (GIsEditor && Ar.IsLoading() && !IsTemplate())
		{
			// If we're not a template then we do not want the duplicate so serialize manually and destroy the template that was created for us
			Ar.Serialize(&CanvasTemplate, sizeof(UObject*));
		}
		else if (!GIsEditor && !Ar.IsLoading() && !GIsDuplicatingClassForReinstancing)
		{
			// Avoid the archiver in the duplicate writer case because we want to avoid the duplicate being created
			Ar.Serialize(&CanvasTemplate, sizeof(UObject*));
		}
		else
		{
			// When we're loading outside of the editor we won't have created the duplicate, so its fine to just use the normal path
			// When we're loading a template then we want the duplicate, so it is fine to use normal archiver
			// When we're saving in the editor we'll create the duplicate, but on loading decide whether to take it or not
			Ar << CanvasTemplate;
		}
	}
#if WITH_EDITOR
	// Since we sometimes serialize properties in instead of using duplication and we can end up pointing at the wrong template
	if (!Ar.IsPersistent() && CanvasTemplate)
	{
		if (IsTemplate())
		{
			// If we are a template and are not pointing at a component we own we'll need to fix that
			if (CanvasTemplate->GetOuter() != this)
			{
				const FString TemplateName = FString::Printf(TEXT("%s_%s_CAT"), *GetName(), *ULexCanvas::StaticClass()->GetName());
				if (UObject* ExistingTemplate = StaticFindObject(nullptr, this, *TemplateName))
				{
					CanvasTemplate = CastChecked<ULexCanvas>(ExistingTemplate);
				}
				else
				{
					CanvasTemplate = CastChecked<ULexCanvas>(StaticDuplicateObject(CanvasTemplate, this, *TemplateName));
				}
			}
		}
		else
		{
			// Because the template may have fixed itself up, the tagged property delta serialized for 
			// the instance may point at a trashed template, so always repoint us to the archetypes template
			CanvasTemplate = CastChecked<ULexWidgetPresenterComponent>(GetArchetype())->CanvasTemplate;
		}
	}
#endif
}

void ULexWidgetPresenterComponent::PostInitProperties()
{
	Super::PostInitProperties();
}

void ULexWidgetPresenterComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
	if (LoadedWidget.IsValid())
	{
		LoadedWidget->CalculateObjectToWorldTransform(true);
	}
}

void ULexWidgetPresenterComponent::LoadPrefab()
{
	if (LoadedWidget.IsValid())
	{
		LoadedWidget->DestroyWidget();
		LoadedWidget = nullptr;
	}
#if WITH_EDITOR
	if (this->GetName().Contains(TEXT("SKEL_")) || this->GetName().Contains(TEXT("TRASH_")))
	{
		UE_LOG(LGUI, Warning, TEXT("Skip LoadPrefab for %s because it's a temp object for blueprint compiling!"), *this->GetName());
		return;
	}
#endif
	if (IsValid(WidgetPrefab))
	{
		if (auto World = GetWorld())
		{
			LoadedWidget = WidgetPrefab->LoadPrefab(World, nullptr, [this](ULexWidget* RootWidget)
			{
				if (auto Canvas = RootWidget->GetComponent<ULexCanvas>())
				{
					RootWidget->RemoveComponent(Canvas);
				}
				RootCanvas = RootWidget->AddComponentByTemplate<ULexCanvas>(CanvasTemplate);
				RootCanvas->AttachToSceneComponent(this);
			});
			LoadedWidget->CalculateObjectToWorldTransform(true);
#if WITH_EDITOR
			TArray<ULexWidget*> AllLoadedWidgets;
			ULexWidget::CollectChildrenWidgets(LoadedWidget.Get(), AllLoadedWidgets, true);
			if (World->WorldType == EWorldType::Editor)
			{
				for (auto Widget : AllLoadedWidgets)
				{
					//set transient in edit mode because we don't want to save these widgets in level, not set in game mode because no need to
					//skip EditorPreview mode because we need full transactional
					Widget->SetFlags(RF_Transient);
				}
			}
			OverallVersionMD5 = WidgetPrefab->GenerateOverallVersionMD5();//store version for auto update
#endif
		}
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
	if (PropertyChangedEvent.MemberProperty != nullptr)
	{
		auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(ULexWidgetPresenterComponent, WidgetPrefab))
		{
			LoadPrefab();
		}
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
		if (!RootCanvas.IsValid())
		{
			UE_LOG(LGUI, Warning, TEXT("[%s].%d RootCanvas is null, skip check WorldSpaceRaycasterSource!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
		//check if there is WorldSpaceRaycaster when this is WorldSpace UI
		if (this->RootCanvas->GetRenderMode() == ELexRenderMode::WorldSpace || this->RootCanvas->GetRenderMode() == ELexRenderMode::WorldSpace_LexUI)
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
		if (auto Widget = NavigationSelectionPrefab->LoadPrefab(this->GetWorld(), this->LoadedWidget.Get()))
		{
			NavigationSelection = Widget->GetComponent<UUINavigationInputSelectionHandler>();
		}
	}
	return NavigationSelection.Get();
}

#undef LOCTEXT_NAMESPACE
