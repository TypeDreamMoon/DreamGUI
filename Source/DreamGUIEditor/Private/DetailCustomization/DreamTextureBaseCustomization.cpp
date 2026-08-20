// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamTextureBaseCustomization.h"
#include "DreamUIEditorUtils.h"
#include "Core/Components/DreamTextureBase.h"
#include "Utils/DreamUIUtils.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IPropertyUtilities.h"
#include "Core/Components/DreamWidget.h"

#define LOCTEXT_NAMESPACE "UITextureBaseCustomization"
FDreamTextureBaseCustomization::FDreamTextureBaseCustomization()
{
}

FDreamTextureBaseCustomization::~FDreamTextureBaseCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamTextureBaseCustomization::MakeInstance()
{
	return MakeShareable(new FDreamTextureBaseCustomization);
}
void FDreamTextureBaseCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<UDreamTextureBase>(item.Get()))
		{
			TargetScriptArray.Add(TWeakObjectPtr<UDreamTextureBase>(validItem));
			if (validItem->GetWorld() && validItem->GetWorld()->WorldType == EWorldType::Editor)
			{
				validItem->CheckTexture();
				validItem->GetWidget()->MarkCanvasUpdate(true);
			}
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI");
	auto textureHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamTextureBase, Texture));
	textureHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextureBaseCustomization::ForceRefresh, &DetailBuilder));
	category.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamTextureBase, Texture));
	UTexture* texture = nullptr;
	textureHandle->GetValue((*(UObject**)&texture));
	if(IsValid(texture))
	{
		EDreamVisualRaycastType raycastType = EDreamVisualRaycastType::Rect;
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
			if (raycastType == EDreamVisualRaycastType::VisiblePixel)
			{
				if (texture->CompressionSettings != TextureCompressionSettings::TC_EditorIcon)
				{
					// The button outlives the layout that built it, so it may hold neither the builder
					// nor the texture: the refresh it asks for is what destroys the builder.
					TWeakObjectPtr<UTexture> weakTexture = texture;
					TWeakPtr<IPropertyUtilities> propertyUtilities = DetailBuilder.GetPropertyUtilities();
					category.AddCustomRow(LOCTEXT("FixTextureSettingForHitTest_Row", "FixTextureSettingForHitTest"))
						.ValueContent()
						[
							SNew(SButton)
							.HAlign(EHorizontalAlignment::HAlign_Center)
							.VAlign(EVerticalAlignment::VAlign_Center)
							.OnClicked_Lambda([weakTexture, propertyUtilities] {
								UTexture* targetTexture = weakTexture.Get();
								if (targetTexture == nullptr)return FReply::Handled();
								GEditor->BeginTransaction(LOCTEXT("FixTextureForHitTest_Transaction", "Fix texture for hit test"));
								targetTexture->Modify();
								targetTexture->CompressionSettings = TextureCompressionSettings::TC_EditorIcon;
								// Recompresses, refreshes the resource and notifies the materials using it;
								// a bare UpdateResource() leaves those materials sampling the old format.
								FPropertyChangedEvent PropertyChangedEvent(UTexture::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UTexture, CompressionSettings)));
								targetTexture->PostEditChangeProperty(PropertyChangedEvent);
								GEditor->EndTransaction();
								if (auto Utilities = propertyUtilities.Pin())
								{
									Utilities->ForceRefresh();
								}
								return FReply::Handled();
								})
							.ToolTipText(LOCTEXT("FixTextureSettingForHitTest_Tooltip", "\
	By default we can't access texture's pixel data, which is required for line trace.\
	Click this button to fix it by change texture settings.\
		"))
							[
								SNew(STextBlock)
								.Text(LOCTEXT("FixTextureForHitTest", "Fix texture for hit test"))
								.Font(IDetailLayoutBuilder::GetDetailFont())
							]
						]
						;
				}
			}
		}
	
		category.AddCustomRow(LOCTEXT("AdditionalButton_Row", "AdditionalButton"))
		.ValueContent()
		.MinDesiredWidth(160)
		[
			SNew(SButton)
			.HAlign(EHorizontalAlignment::HAlign_Center)
			.VAlign(EVerticalAlignment::VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SnapSize_Button", "Snap Size"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.OnClicked_Lambda([=, this]()
			{
				GEditor->BeginTransaction(LOCTEXT("TextureSnapSize_Transaction", "UITexture snap size"));
				for (auto item : TargetScriptArray)
				{
					if (item.IsValid())
					{
						item->Modify();
						item->SetSizeFromTexture();
						FDreamUIUtils::NotifyPropertyChanged(item.Get(), UDreamWidget::GetPropertyName_AnchorData());
						item->GetWidget()->MarkCanvasUpdate(true);
					}
				}
				GEditor->EndTransaction();
				return FReply::Handled();
			})
		];
	}
}
void FDreamTextureBaseCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE