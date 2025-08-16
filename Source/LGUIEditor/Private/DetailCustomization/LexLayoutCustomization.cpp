// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexLayoutCustomization.h"
#include "LGUIEditorUtils.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "Core/Components/LexLayout.h"

#define LOCTEXT_NAMESPACE "LexLayoutCustomization"
FLexLayoutCustomization::FLexLayoutCustomization()
{
}

FLexLayoutCustomization::~FLexLayoutCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexLayoutCustomization::MakeInstance()
{
	return MakeShareable(new FLexLayoutCustomization);
}
void FLexLayoutCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> TargetObjects;
	DetailBuilder.GetObjectsBeingCustomized(TargetObjects);
	TargetScriptArray.Empty();
	for (auto item : TargetObjects)
	{
		if (auto validItem = Cast<ULexLayout>(item.Get()))
		{
			TargetScriptArray.Add(validItem);
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	auto Conflict = [](ULexLayout* Target)
	{
		auto Widget = Target->GetWidget();
		if (auto ParentWidget = Widget->GetUIParent())
		{
			if (auto ParentLayout = ParentWidget->GetLayout())
			{
				FLexLayoutControlAnchorData ParentControl;
				ParentLayout->GetLayoutControlAnchor(Widget, ParentControl);
				FLexLayoutControlAnchorData ThisControl;
				Target->GetLayoutControlAnchor(Widget, ThisControl);
				if (ParentControl.Conflict(ThisControl))
				{
					return true;
				}
			}
		}
		return false;
	};
	auto& LayoutCategory = DetailBuilder.EditCategory("Layout");
	auto InfoText = LOCTEXT("LayoutGoodInfo", "No info");
	auto ErrorInfoText = LOCTEXT("LayoutConflictInfo", "Parent Layout is controlling this widget, which is conflict with this layout!");
	LayoutCategory.AddCustomRow(LOCTEXT("LayoutConflictRow", "LayoutConflict"))
	.NameContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.Text(LOCTEXT("LayoutInfo", "Info"))
		]
	]
	.ValueContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		.WidthOverride(500)
		[
			SNew(STextBlock)
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity_Lambda([&]
			{
				if (TargetScriptArray.Num() == 1 && TargetScriptArray[0].IsValid() && Conflict(TargetScriptArray[0].Get()))
					return FSlateColor(FLinearColor::Red);
				return FSlateColor::UseForeground();
			})
			.AutoWrapText(true)
			.Text_Lambda([&]
			{
				if (TargetScriptArray.Num() == 1 && TargetScriptArray[0].IsValid() && Conflict(TargetScriptArray[0].Get()))
					return LOCTEXT("LayoutInfo_Conflict", "Parent Layout is controlling this widget, which is conflict with this layout!");
				return LOCTEXT("LayoutInfo_Good", "");
			})
		]
	]
	;
}

#undef LOCTEXT_NAMESPACE