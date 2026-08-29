// Copyright 2019-Present LexLiu. All Rights Reserved.


#include "Core/DreamWidgetPresenterComponent.h"
#include "Core/DreamUserWidget.h"

#include "DreamGUI.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"

#define LOCTEXT_NAMESPACE "DreamWidgetPresenterComponent"

UDreamWidgetPresenterComponent::UDreamWidgetPresenterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UDreamWidgetPresenterComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UDreamWidgetPresenterComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UDreamWidgetPresenterComponent::LoadWidget()
{
	if (LoadedWidget.IsValid())
	{
		LoadedWidget->DestroyWidget();
		LoadedWidget = nullptr;
	}
#if WITH_EDITOR
	if (this->GetName().Contains(TEXT("SKEL_")) || this->GetName().Contains(TEXT("TRASH_")))
	{
		UE_LOG(DreamGUI, Warning, TEXT("Skip loading the widget for %s because it's a temp object for blueprint compiling!"), *this->GetName());
		return;
	}
#endif
	if (IsValid(WidgetClass))
	{
		if (auto World = GetWorld())
		{
			// The canvas swap runs before the hierarchy comes alive, as it did under the prefab
			// loader's CallbackBeforeAwake: a behaviour that woke up first could have cached the
			// canvas this replaces.
			LoadedWidget = CreateDreamWidget(World, WidgetClass, nullptr, [this](UDreamUserWidget* RootWidget)
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
			NotifyWidgetLoaded();
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
void UDreamWidgetPresenterComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.MemberProperty != nullptr)
	{
		auto PropertyName = PropertyChangedEvent.GetMemberPropertyName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UDreamWidgetPresenterComponent, WidgetClass))
		{
			LoadWidget();
		}
	}
}


#endif

void UDreamWidgetPresenterComponent::SetWidgetClass(TSubclassOf<UDreamUserWidget> Value)
{
	if (WidgetClass != Value)
	{
		WidgetClass = Value;
		LoadWidget();
	}
}

#undef LOCTEXT_NAMESPACE
