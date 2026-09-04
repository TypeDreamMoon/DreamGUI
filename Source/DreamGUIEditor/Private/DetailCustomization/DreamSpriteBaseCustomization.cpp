// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamSpriteBaseCustomization.h"
#include "DreamUIEditorUtils.h"
#include "DreamDetailsTemplateMirror.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IPropertyUtilities.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/Components/DreamSpriteBase.h"
#include "Core/Components/DreamWidget.h"

#define LOCTEXT_NAMESPACE "DreamUISpriteBaseCustomization"
FDreamSpriteBaseCustomization::FDreamSpriteBaseCustomization()
{
}

FDreamSpriteBaseCustomization::~FDreamSpriteBaseCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamSpriteBaseCustomization::MakeInstance()
{
	return MakeShareable(new FDreamSpriteBaseCustomization);
}
void FDreamSpriteBaseCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptArray.Empty();
	for (auto item : targetObjects)
	{
		if (auto validItem = Cast<UDreamSpriteBase>(item.Get()))
		{
			TargetScriptArray.Add(TWeakObjectPtr<UDreamSpriteBase>(validItem));
			if (validItem->GetWorld() && validItem->GetWorld()->WorldType == EWorldType::Editor)
			{
				validItem->CheckSpriteData();
				validItem->GetWidget()->MarkCanvasUpdate(true);
			}
		}
	}
	if (TargetScriptArray.Num() == 0)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	// The panel edits a visual, but "Snap Size" changes its WIDGET's geometry. Both of these are
	// captured by the button lambdas below; see DreamDetailsTemplateMirror for why the hook is
	// called by hand at all.
	FNotifyHook* NotifyHook = DetailBuilder.GetPropertyUtilities()->GetNotifyHook();
	FProperty* AnchorDataProperty = FindFProperty<FProperty>(UDreamWidget::StaticClass(), UDreamWidget::GetPropertyName_AnchorData());

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI");

	category.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamSpriteBase, Sprite));
	auto spriteHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamSpriteBase, Sprite));
	// The refresh this asks for destroys the layout builder, so the delegate keeps the utilities instead.
	TWeakPtr<IPropertyUtilities> propertyUtilities = DetailBuilder.GetPropertyUtilities();
	spriteHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, this] {
		for (auto item : TargetScriptArray)
		{
			if (item.IsValid())
			{
				item->OnPostChangeSpriteProperty();
			}
		}
		if (auto Utilities = propertyUtilities.Pin())
		{
			Utilities->ForceRefresh();
		}
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
	UDreamUISpriteData_BaseObject* spriteObject = nullptr;
	spriteHandle->GetValue(*(UObject**)&spriteObject);
	if (IsValid(spriteObject))
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
				// The size lands on the WIDGET's AnchorData, not on anything this panel owns, so that
				// is the property the mirror has to be told about and the widget is the object.
				TArray<UObject*> ResizedWidgets;
				DreamDetailsTemplateMirror::NotifyPreChange(NotifyHook, AnchorDataProperty);
				for (auto item : TargetScriptArray)
				{
					if (item.IsValid())
					{
						item->Modify();
						item->SetSizeFromSpriteData();
						if (UDreamWidget* Widget = item->GetWidget())
						{
							Widget->Modify();
							FDreamUIUtils::NotifyPropertyChanged(Widget, UDreamWidget::GetPropertyName_AnchorData());
							Widget->MarkCanvasUpdate(true);
							ResizedWidgets.Add(Widget);
						}
					}
				}
				// A widget setter, not a property handle, so the details view's FNotifyHook hears nothing
				// on its own -- and that hook is the only thing that carries a value from the PREVIEW
				// widget onto the blueprint's template. Without it the new size showed up, lasted until
				// the next compile, and then snapped back.
				DreamDetailsTemplateMirror::NotifyPostChange(NotifyHook, AnchorDataProperty, ResizedWidgets);
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
void FDreamSpriteBaseCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (DetailBuilder)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE