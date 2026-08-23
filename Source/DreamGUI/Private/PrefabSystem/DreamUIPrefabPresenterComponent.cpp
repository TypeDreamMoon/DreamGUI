// Copyright 2019-Present LexLiu. All Rights Reserved.


#include "PrefabSystem/DreamUIPrefabPresenterComponent.h"

#include "DreamGUI.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "PrefabSystem/DreamUIPrefab.h"

#define LOCTEXT_NAMESPACE "DreamWidgetPresenterComponent"

UDreamUIPrefabPresenterComponent::UDreamUIPrefabPresenterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDreamUIPrefabPresenterComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UDreamUIPrefabPresenterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDreamUIPrefabPresenterComponent::LoadWidget()
{
	if (LoadedWidget.IsValid())
	{
		LoadedWidget->DestroyWidget();
		LoadedWidget = nullptr;
	}
#if WITH_EDITOR
	if (this->GetName().Contains(TEXT("SKEL_")) || this->GetName().Contains(TEXT("TRASH_")))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Skip LoadPrefab for %s because it's a temp object for blueprint compiling!"), *this->GetName());
		return;
	}
#endif
	if (IsValid(WidgetPrefab))
	{
		if (auto World = GetWorld())
		{
			LoadedWidget = WidgetPrefab->LoadPrefab(World, nullptr, [this](UDreamWidget* RootWidget)
			{
				if (auto Canvas = RootWidget->GetComponent<UDreamCanvas>())
				{
					RootWidget->RemoveComponent(Canvas);
				}
				RootCanvas = RootWidget->AddComponentByTemplate<UDreamCanvas>(CanvasTemplate);
				RootCanvas->AttachToSceneComponent(this);
			});
			LoadedWidget->CalculateObjectToWorldTransform(true);
			ApplyWidgetOverridesToLoadedWidget();
#if WITH_EDITOR
			TArray<UDreamWidget*> AllLoadedWidgets;
			UDreamWidget::CollectChildrenWidgets(LoadedWidget.Get(), AllLoadedWidgets, true);
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
	if (!bIsSpawnFromFactory)//if spawn from prefab-factory then the "CheckNecessaryObjects" is handled from there
	{
		auto World = GetWorld();
		if (World && World->WorldType != EWorldType::EditorPreview && !World->IsGameWorld())//Edit mode and not BlueprintEditorPreview
		{
			UDreamUIManagerObject::AddOneShotTickFunction([WeakThis = MakeWeakObjectPtr(this)]()
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
void UDreamUIPrefabPresenterComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.MemberProperty != nullptr)
	{
		auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamUIPrefabPresenterComponent, WidgetPrefab))
		{
			LoadWidget();
		}
	}
}

void UDreamUIPrefabPresenterComponent::CheckPrefabVersion()
{
	if (IsValid(WidgetPrefab))
	{
		if (OverallVersionMD5 != WidgetPrefab->GenerateOverallVersionMD5())
		{
			LoadWidget();
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

void UDreamUIPrefabPresenterComponent::SetPrefab(UDreamUIPrefab* Value)
{
	if (WidgetPrefab != Value)
	{
		WidgetPrefab = Value;
		LoadWidget();
	}
}

#undef LOCTEXT_NAMESPACE
