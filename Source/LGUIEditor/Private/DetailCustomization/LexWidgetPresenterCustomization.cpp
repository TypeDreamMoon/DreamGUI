// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexWidgetPresenterCustomization.h"

#include "DetailCategoryBuilder.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Core/Components/LexWidgetPresenterComponent.h"
#include "Window/LexUIWidgetInspector.h"

#define LOCTEXT_NAMESPACE "LexWidgetPresenterCustomization"
FLexWidgetPresenterCustomization::FLexWidgetPresenterCustomization()
{
}

FLexWidgetPresenterCustomization::~FLexWidgetPresenterCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexWidgetPresenterCustomization::MakeInstance()
{
	return MakeShareable(new FLexWidgetPresenterCustomization);
}
void FLexWidgetPresenterCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	for (auto Item : TargetObjects)
	{
		if (auto ValidItem = Cast<ULexWidgetPresenterComponent>(Item.Get()))
		{
			TargetScriptArray.Add(ValidItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	auto TargetWorld = TargetScriptArray[0]->GetWorld();

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("LexWidgetPresenter");

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
		bIsExternalTabAlreadyOpened = TargetWorld != nullptr && WidgetInspector->GetWorld() == TargetWorld;
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
			.Visibility_Lambda([=, this]()
			{
				if (TargetScriptArray.Num() > 0 && TargetScriptArray[0] != nullptr && TargetScriptArray[0]->GetWorld() != nullptr)
				{
					return EVisibility::Visible;
				}
				return EVisibility::Collapsed;
			})
			.OnClicked_Lambda([=, this]()
			{
				if (auto Tab = FGlobalTabmanager::Get()->TryInvokeTab(FLGUIEditorModule::LexUIWidgetInspectorTabName))
				{
					StaticCastSharedRef<SLexUIWidgetInspector>(Tab->GetContent())->AssignWorld(TargetScriptArray[0]->GetWorld());
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(bIsExternalTabAlreadyOpened ? LOCTEXT("OpenWidgetInspector", "Focus Tab") : LOCTEXT("OpenWidgetInspector", "Open in Tab"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
}
void FLexWidgetPresenterCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE