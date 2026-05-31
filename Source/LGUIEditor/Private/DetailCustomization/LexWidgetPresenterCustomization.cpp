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

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("LexWidgetPresenter");

	Category.AddCustomRow(LOCTEXT("AdditionalButton", "AdditionalButton"))
	.WholeRowContent()
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
			SLexUIWidgetInspector::CurrentSelectedWorld = TargetScriptArray[0]->GetWorld();
			FGlobalTabmanager::Get()->TryInvokeTab(FLGUIEditorModule::LexUIWidgetInspectorTabName);
			return FReply::Handled();
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OpenWidgetInspector", "Open Widget Inspector"))
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