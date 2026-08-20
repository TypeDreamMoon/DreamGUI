// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/DreamWidgetPresenterComponentBase.h"

#include "EngineUtils.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamWorldSpaceRaycasterBase.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "PrefabSystem/DreamUIPrefab.h"

#define LOCTEXT_NAMESPACE "DreamWidgetPresenterComponentBase"

UDreamWidgetPresenterComponentBase::UDreamWidgetPresenterComponentBase()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	
	bWantsOnUpdateTransform = true;

	CanvasTemplate = CreateDefaultSubobject<UDreamCanvas>(TEXT("CanvasTemplate"));
	
	NavigationSelectionPrefab = LoadObject<UDreamUIPrefab>(NULL, TEXT("/DreamGUI/Prefabs/NavigationSelectionInputHandler"));
}

void UDreamWidgetPresenterComponentBase::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld()->IsGameWorld())
	{
		LoadWidget();//load when BeginPlay in game mode
	}
}

void UDreamWidgetPresenterComponentBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDreamWidgetPresenterComponentBase::OnRegister()
{
	Super::OnRegister();
	if (!GetWorld()->IsGameWorld())
	{
		LoadWidget();//load when OnRegister in edit mode
	}
}

void UDreamWidgetPresenterComponentBase::OnUnregister()
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

void UDreamWidgetPresenterComponentBase::PostLoad()
{
	Super::PostLoad();
}

void UDreamWidgetPresenterComponentBase::Serialize(FArchive& Ar)
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
				const FString TemplateName = FString::Printf(TEXT("%s_%s_CAT"), *GetName(), *UDreamCanvas::StaticClass()->GetName());
				if (UObject* ExistingTemplate = StaticFindObject(nullptr, this, *TemplateName))
				{
					CanvasTemplate = CastChecked<UDreamCanvas>(ExistingTemplate);
				}
				else
				{
					CanvasTemplate = CastChecked<UDreamCanvas>(StaticDuplicateObject(CanvasTemplate, this, *TemplateName));
				}
			}
		}
		else
		{
			// Because the template may have fixed itself up, the tagged property delta serialized for 
			// the instance may point at a trashed template, so always repoint us to the archetypes template
			CanvasTemplate = CastChecked<UDreamWidgetPresenterComponentBase>(GetArchetype())->CanvasTemplate;
		}
	}
#endif
}

void UDreamWidgetPresenterComponentBase::PostInitProperties()
{
	Super::PostInitProperties();
}

void UDreamWidgetPresenterComponentBase::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
	Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
	if (LoadedWidget.IsValid())
	{
		LoadedWidget->CalculateObjectToWorldTransform(true);
	}
}

#if WITH_EDITOR
void UDreamWidgetPresenterComponentBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.MemberProperty != nullptr)
	{
		auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
	}
}

#include "Dialog/SCustomDialog.h"
bool UDreamWidgetPresenterComponentBase::bNeedCheckEventSystem = true;
bool UDreamWidgetPresenterComponentBase::bNeverCheckEventSystem = false;
bool UDreamWidgetPresenterComponentBase::bNeedCheckRaycasterSource = true;
bool UDreamWidgetPresenterComponentBase::bNeverCheckRaycasterSource = false;
void UDreamWidgetPresenterComponentBase::CheckNecessaryObjects()
{
	if (bNeedCheckEventSystem)
	{
		bNeedCheckEventSystem = false;
		//check if there is EventSystem in editor
		bool bEventSystemExits = false;
		for (TActorIterator<AActor> ActorItr(this->GetWorld()); ActorItr; ++ActorItr)
		{
			auto Actor = *ActorItr;
			if (Actor->FindComponentByClass<UDreamEventSystem>())
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
						.Text(LOCTEXT("MissingEventSystem", "There is no DreamEventSystem in the world! DreamUI will not interactable without DreamEventSystem, would you like to create a default one?"))
					]
				]
				.Buttons({
					SCustomDialog::FButton(
						LOCTEXT("DialogBtnYes", "Yes"),
						FSimpleDelegate::CreateLambda([=, WeakThis = MakeWeakObjectPtr(this)]()
						{
							if (!WeakThis.IsValid())return;
							auto ClassName = TEXT("DreamEventSystemActor_EnhancedInput");
							if (auto ActorClass = LoadObject<UClass>(NULL, *FString::Printf(TEXT("/DreamGUI/Blueprints/%s.%s_C"), ClassName, ClassName)))
							{
								auto Actor = WeakThis->GetWorld()->SpawnActor<AActor>(ActorClass);
								Actor->SetActorLabel(ClassName);
							}
							else
							{
								UE_LOG(DreamGUI, Error, TEXT("[%s].%d Load %s error! Missing some content of DreamUI plugin, reinstall this plugin may fix the issue."), 
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
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d RootCanvas is null, skip check WorldSpaceRaycasterSource!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			return;
		}
		//check if there is WorldSpaceRaycaster when this is WorldSpace UI
		if (this->RootCanvas->GetRenderMode() == EDreamRenderMode::WorldSpace || this->RootCanvas->GetRenderMode() == EDreamRenderMode::WorldSpace_DreamUI)
		{
			UDreamWorldSpaceRaycasterSource* ExistWorldSpaceRaycasterSource = nullptr;
			for (TActorIterator<AActor> ActorItr(this->GetWorld()); ActorItr; ++ActorItr)
			{
				auto Actor = *ActorItr;
				if (auto Comp = Actor->FindComponentByClass<UDreamWorldSpaceRaycasterSource>())
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
						FSimpleDelegate::CreateLambda([=, &ExistWorldSpaceRaycasterSource, WeakThis = MakeWeakObjectPtr(this)]()
						{
							if (!WeakThis.IsValid())return;
							auto ClassName = TEXT("DreamWorldSpaceRaycasterSource_Mouse");
							if (auto ActorClass = LoadObject<UClass>(NULL, *FString::Printf(TEXT("/DreamGUI/Blueprints/%s.%s_C"), ClassName, ClassName)))
							{
								auto Actor = WeakThis->GetWorld()->SpawnActor<AActor>(ActorClass);
								Actor->SetActorLabel(ClassName);
								ExistWorldSpaceRaycasterSource = Actor->FindComponentByClass<UDreamWorldSpaceRaycasterSource>();
							}
							else
							{
								UE_LOG(DreamGUI, Error, TEXT("[%s].%d Load %s error! Missing some content of DreamUI plugin, reinstall this plugin may fix the issue."), 
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
				if (auto WorldSpaceRaycaster = this->GetOwner()->FindComponentByClass<UDreamWorldSpaceRaycasterBase>())
				{
					if (auto RaycasterSourceActor = Cast<ADreamWorldSpaceRaycasterSourceActor>(ExistWorldSpaceRaycasterSource->GetOwner()))
					{
						WorldSpaceRaycaster->SetRaycasterSourceActor(RaycasterSourceActor);
					}
				}
			}
		}
	}
}

void UDreamWidgetPresenterComponentBase::MarkNeedCheckNecessaryObjects()
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
#endif

UUINavigationInputSelectionHandler* UDreamWidgetPresenterComponentBase::GetNavigationSelection()
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

#if WITH_EDITOR

void UDreamWidgetPresenterComponentBase::ReloadWidget()
{
	LoadWidget();
}
#endif

#undef LOCTEXT_NAMESPACE
