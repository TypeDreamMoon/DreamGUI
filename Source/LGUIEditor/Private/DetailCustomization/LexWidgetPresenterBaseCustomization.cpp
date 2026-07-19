// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexWidgetPresenterBaseCustomization.h"

#include "DetailCategoryBuilder.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SButton.h"
#include "Core/LexWidgetPresenterComponentBase.h"
#include "Window/LexUIWidgetInspector.h"

#define LOCTEXT_NAMESPACE "LexWidgetPresenterBaseCustomization"
FLexWidgetPresenterBaseCustomization::FLexWidgetPresenterBaseCustomization()
{
}

FLexWidgetPresenterBaseCustomization::~FLexWidgetPresenterBaseCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexWidgetPresenterBaseCustomization::MakeInstance()
{
	return MakeShareable(new FLexWidgetPresenterBaseCustomization);
}
void FLexWidgetPresenterBaseCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	for (auto Item : TargetObjects)
	{
		if (auto ValidItem = Cast<ULexWidgetPresenterComponentBase>(Item.Get()))
		{
			TargetScriptArray.Add(ValidItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	const TWeakObjectPtr<ULexWidgetPresenterComponentBase> PrimaryTarget = TargetScriptArray[0];
	const TWeakObjectPtr<UWorld> TargetWorld = PrimaryTarget.IsValid() ? PrimaryTarget->GetWorld() : nullptr;

	auto& Category = DetailBuilder.EditCategory("LexWidgetPresenter");

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
				for (const TWeakObjectPtr<ULexWidgetPresenterComponentBase>& Target : Targets)
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

	TSharedPtr<SDockTab> ExistingTab = HostTabManager->FindExistingLiveTab(FLGUIEditorModule:: LexUIWidgetInspectorTabName);
	if (ExistingTab.IsValid())
	{
		auto WidgetInspector = StaticCastSharedRef<SLexUIWidgetInspector>(ExistingTab->GetContent());
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
				if (const ULexWidgetPresenterComponentBase* Target = PrimaryTarget.Get(); Target && IsValid(Target->GetWorld()))
				{
					return EVisibility::Visible;
				}
				return EVisibility::Collapsed;
			})
			.OnClicked_Lambda([PrimaryTarget]()
			{
				ULexWidgetPresenterComponentBase* Target = PrimaryTarget.Get();
				if (Target && IsValid(Target->GetWorld()))
				{
					if (auto Tab = FGlobalTabmanager::Get()->TryInvokeTab(FLGUIEditorModule::LexUIWidgetInspectorTabName))
					{
						StaticCastSharedRef<SLexUIWidgetInspector>(Tab->GetContent())->AssignWorld(Target->GetWorld());
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
		auto CanvasTemplate_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexWidgetPresenterComponentBase, CanvasTemplate));
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
void FLexWidgetPresenterBaseCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE
