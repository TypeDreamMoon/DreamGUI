// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/UISpriteBaseCustomization.h"
#include "LGUIEditorUtils.h"
#include "LGUIHeaders.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UISpriteBaseCustomization"
FLexUISpriteBaseCustomization::FLexUISpriteBaseCustomization()
{
}

FLexUISpriteBaseCustomization::~FLexUISpriteBaseCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexUISpriteBaseCustomization::MakeInstance()
{
	return MakeShareable(new FLexUISpriteBaseCustomization);
}
void FLexUISpriteBaseCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<UUISpriteBase>(item.Get()))
		{
			TargetScriptArray.Add(TWeakObjectPtr<UUISpriteBase>(validItem));
			if (validItem->GetWorld() && validItem->GetWorld()->WorldType == EWorldType::Editor)
			{
				validItem->CheckSpriteData();
				validItem->GetWidget()->EditorForceUpdate();
			}
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(LGUIEditor, Log, TEXT("[UISpriteBaseCustomization]Get TargetScript is null"));
		return;
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("LGUI");

	category.AddProperty(GET_MEMBER_NAME_CHECKED(UUISpriteBase, Sprite));
	auto spriteHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UUISpriteBase, Sprite));
	spriteHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this, &DetailBuilder] {
		for (auto item : TargetScriptArray)
		{
			if (item.IsValid())
			{
				item->OnPostChangeSpriteProperty();
			}
		}
		DetailBuilder.ForceRefreshDetails();
	}));
	spriteHandle->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda([=, this] {
		for (auto item : TargetScriptArray)
		{
			if (item.IsValid())
			{
				item->OnPreChangeSpriteProperty();
			}
		}
	}));
	ULexUISpriteData_BaseObject* spriteObject = nullptr;
	spriteHandle->GetValue(*(UObject**)&spriteObject);
	if (IsValid(spriteObject))
	{
		ELexVisualHitTestType raycastType = ELexVisualHitTestType::Rect;
		bool bGetRaycastTypeValue = true;
		for (int i = 0; i < TargetScriptArray.Num(); i++)
		{
			if (i == 0)
			{
				raycastType = TargetScriptArray[i]->GetRaycastType();
			}
			else
			{
				if (raycastType != TargetScriptArray[i]->GetRaycastType())
				{
					bGetRaycastTypeValue = false;
					break;
				}
			}
		}
		if (bGetRaycastTypeValue)
		{
			if (raycastType == ELexVisualHitTestType::VisiblePixel)
			{
				if (!spriteObject->SupportReadPixel())
				{
					category.AddCustomRow(LOCTEXT("NotSupportVisiblePixelRaycast_Row", "NotSupportVisiblePixelRaycast"))
						.WholeRowContent()
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(STextBlock)
							.ColorAndOpacity(FLinearColor::Yellow)
							.Text(LOCTEXT("NotSupportVisiblePixelRaycast_Text", "Use RaycastType of VisiblePixel, but this Sprite does not support this type."))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					;
				}
			}
		}

		category.AddCustomRow(LOCTEXT("AdditionalButton", "AdditionalButton"))
		.ValueContent()
		[
			SNew(SButton)
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.VAlign(EVerticalAlignment::VAlign_Center)
			.OnClicked_Lambda([=, this]()
			{
				GEditor->BeginTransaction(LOCTEXT("SpriteSnapSize_Transaction", "UISprite snap size"));
				for (auto item : TargetScriptArray)
				{
					if (item.IsValid())
					{
						item->Modify();
						item->SetSizeFromSpriteData();
						FLexUIUtils::NotifyPropertyChanged(item.Get(), ULexWidget::GetPropertyName_Width());
						FLexUIUtils::NotifyPropertyChanged(item.Get(), ULexWidget::GetPropertyName_Height());
						item->GetWidget()->EditorForceUpdate();
					}
				}
				GEditor->EndTransaction();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(LOCTEXT("MakePixelPerfectButton", "Snap Size"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];
	}
}
void FLexUISpriteBaseCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE