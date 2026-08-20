// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DetailCustomization/DreamWidgetPresenterBaseCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Window/DreamUIWidgetInspector.h"

#define LOCTEXT_NAMESPACE "DreamWidgetPresenterBaseCustomization"
FDreamWidgetPresenterBaseCustomization::FDreamWidgetPresenterBaseCustomization()
{
}

FDreamWidgetPresenterBaseCustomization::~FDreamWidgetPresenterBaseCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamWidgetPresenterBaseCustomization::MakeInstance()
{
	return MakeShareable(new FDreamWidgetPresenterBaseCustomization);
}
void FDreamWidgetPresenterBaseCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	for (auto Item : TargetObjects)
	{
		if (auto ValidItem = Cast<UDreamWidgetPresenterComponentBase>(Item.Get()))
		{
			TargetScriptArray.Add(ValidItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	const TWeakObjectPtr<UDreamWidgetPresenterComponentBase> PrimaryTarget = TargetScriptArray[0];
	const TWeakObjectPtr<UWorld> TargetWorld = PrimaryTarget.IsValid() ? PrimaryTarget->GetWorld() : nullptr;

	auto& Category = DetailBuilder.EditCategory("DreamWidgetPresenter");

	// ReloadWidget button
	Category.AddCustomRow(LOCTEXT("ReloadWidget", "ReloadWidget"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ReloadWidget", "ReloadWidget"))
			.Font(DetailBuilder.GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([Targets = TargetScriptArray]()
			{
				for (const TWeakObjectPtr<UDreamWidgetPresenterComponentBase>& Target : Targets)
				{
					if (Target.IsValid())
					{
						Target->ReloadWidget();
					}
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ReloadWidgetBtn", "Reload"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

	TSharedPtr<FTabManager> HostTabManager = nullptr;
	if (auto DetailsView = DetailBuilder.GetDetailsViewSharedPtr())
	{
		HostTabManager = DetailsView->GetHostTabManager();
	}
	if (!HostTabManager.IsValid())
	{
		HostTabManager = FGlobalTabmanager::Get();
	}

	bool bIsExternalTabAlreadyOpened = false;

	TSharedPtr<SDockTab> ExistingTab = HostTabManager->FindExistingLiveTab(FDreamGUIEditorModule:: DreamUIWidgetInspectorTabName);
	if (ExistingTab.IsValid())
	{
		auto WidgetInspector = StaticCastSharedRef<SDreamUIWidgetInspector>(ExistingTab->GetContent());
		bIsExternalTabAlreadyOpened = TargetWorld.IsValid() && WidgetInspector->GetWorld() == TargetWorld.Get();
	}
	Category.AddCustomRow(FText())
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("WidgetInspector", "WidgetInspector"))
			.Font(DetailBuilder.GetDetailFont())
		]
		.ValueContent()
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Visibility_Lambda([PrimaryTarget]()
			{
				if (const UDreamWidgetPresenterComponentBase* Target = PrimaryTarget.Get(); Target && IsValid(Target->GetWorld()))
				{
					return EVisibility::Visible;
				}
				return EVisibility::Collapsed;
			})
			.OnClicked_Lambda([PrimaryTarget]()
			{
				UDreamWidgetPresenterComponentBase* Target = PrimaryTarget.Get();
				if (Target && IsValid(Target->GetWorld()))
				{
					if (auto Tab = FGlobalTabmanager::Get()->TryInvokeTab(FDreamGUIEditorModule::DreamUIWidgetInspectorTabName))
					{
						StaticCastSharedRef<SDreamUIWidgetInspector>(Tab->GetContent())->AssignWorld(Target->GetWorld());
					}
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(bIsExternalTabAlreadyOpened ? LOCTEXT("OpenWidgetInspector", "Focus Tab") : LOCTEXT("OpenWidgetInspector", "Open in Tab"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

	//canvas template
	{
		auto CanvasTemplate_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamWidgetPresenterComponentBase, CanvasTemplate));
		UObject* CanvasTemplate = nullptr;
		CanvasTemplate_PH->GetValue(CanvasTemplate);
		auto& CanvasTemplateCategory = DetailBuilder.EditCategory("CanvasTemplate");
		if (IsValid(CanvasTemplate))
		{
			if (IDetailPropertyRow* CanvasTemplateRow = CanvasTemplateCategory.AddExternalObjects(
				{ CanvasTemplate }, EPropertyLocation::Default,
				FAddPropertyParams().HideRootObjectNode(true).CreateCategoryNodes(true)))
			{
				CanvasTemplateRow->ShouldAutoExpand(true);
				CanvasTemplateRow->Visibility(TAttribute<EVisibility>::CreateLambda([TargetWorld]()
				{
					const UWorld* World = TargetWorld.Get();
					return World && World->IsGameWorld() ? EVisibility::Collapsed : EVisibility::Visible;
				}));
				DetailBuilder.HideProperty(CanvasTemplate_PH);
			}
		}
	}
}
void FDreamWidgetPresenterBaseCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE
