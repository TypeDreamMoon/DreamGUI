// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamTextCustomization.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Core/Components/DreamText.h"

#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "MaterialDomain.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "PropertyType/DreamTextAlignmentCustomization.h"
#include "PropertyType/DreamTextFontStyleCustomization.h"

#define LOCTEXT_NAMESPACE "UITextCustomization"
FDreamTextCustomization::FDreamTextCustomization()
{
}

FDreamTextCustomization::~FDreamTextCustomization()
{
}

TSharedRef<IDetailCustomization> FDreamTextCustomization::MakeInstance()
{
	return MakeShareable(new FDreamTextCustomization);
}
void FDreamTextCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = Cast<UDreamText>(targetObjects[0].Get());
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	
	IDetailCategoryBuilder& DreamGUICategory = DetailBuilder.EditCategory("DreamGUI");
	auto Font_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, Font));
	Font_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextCustomization::ForceRefresh, &DetailBuilder));
	DreamGUICategory.AddProperty(Font_PH);
	DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, Text));

	DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, FontSize));
	DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, FontSpace));

	//text alignment
	{
		DetailBuilder.GetDetailsViewSharedPtr()->RegisterInstancedCustomPropertyTypeLayout(TEXT("EDreamUITextParagraphHorizontalAlign"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamTextAlignmentCustomization::MakeInstance, true));
		DetailBuilder.GetDetailsViewSharedPtr()->RegisterInstancedCustomPropertyTypeLayout(TEXT("EDreamUITextParagraphVerticalAlign"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamTextAlignmentCustomization::MakeInstance, false));
		DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, HAlign));
		DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, VAlign));
	}
	//font style
	DetailBuilder.GetDetailsViewSharedPtr()->RegisterInstancedCustomPropertyTypeLayout(TEXT("EDreamUITextFontStyle"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamTextFontStyleCustomization::MakeInstance));

	auto OverflowTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, OverflowType));
	OverflowTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextCustomization::ForceRefresh, &DetailBuilder));
	DreamGUICategory.AddProperty(OverflowTypeHandle);
	
	TArray<FName> NeedToHidePropertyNames;
	auto RichText_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, bRichText));
	RichText_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextCustomization::ForceRefresh, &DetailBuilder));
	bool bRichText = false;
	RichText_PH->GetValue(bRichText);
	if (bRichText)
	{
		IDetailGroup& RichTextGroup = DreamGUICategory.AddGroup(FName("RichText"), RichText_PH->GetPropertyDisplayName());
		RichTextGroup.HeaderProperty(RichText_PH);
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, RichTextTagFilterFlags)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, RichTextCustomStyleData)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, RichTextImageData)));
		RichTextGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, CreatedRichTextImageObjectArray)));
	}
	else
	{
		DreamGUICategory.AddProperty(RichText_PH);
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamText, RichTextCustomStyleData));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamText, RichTextImageData));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamText, CreatedRichTextImageObjectArray));
		NeedToHidePropertyNames.Add(GET_MEMBER_NAME_CHECKED(UDreamText, RichTextTagFilterFlags));
	}

	for (auto item : NeedToHidePropertyNames)
	{
		DetailBuilder.HideProperty(item);
	}

	auto OverrideMaterial_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, OverrideMaterial));
	OverrideMaterial_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda([=, &DetailBuilder] {
		DetailBuilder.ForceRefreshDetails();
		}));
	DreamGUICategory.AddProperty(OverrideMaterial_PH);
	{
		UDreamUIFontData_BaseObject* Font = nullptr;
		Font_PH->GetValue(*(UObject**)&Font);
		if (Font)
		{
			for (auto Item : Font->GetPresetMaterials())
			{
				if (IsValid(Item))
				{
					PresetMaterials.Add(Item);
				}
			}
		}
	}
	DreamGUICategory.AddCustomRow(LOCTEXT("PresetOverrideMaterialsRow", "PresetOverrideMaterials"))
	.Visibility(PresetMaterials.Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed)
	.ValueContent()
	[
		SNew(SBox)
		.VAlign(VAlign_Center)
		[
			SNew(SComboButton)
			.HasDownArrow(true)
			.ButtonContent()
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(LOCTEXT("PresetMaterials_PropertyName", "PresetMaterials"))
				.ToolTipText(LOCTEXT("PresetMaterials_ToolTip", "Here list PresetMaterials from DreamUIFont, you can easily set these materials to OverrideMaterial."))
			]
			.MenuContent()
			[
				SNew(SVerticalBox)
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SListView<TWeakObjectPtr<UMaterialInterface>>)
					.ListItemsSource(&PresetMaterials)
					.OnGenerateRow_Lambda([=](TWeakObjectPtr<UMaterialInterface> Item, const TSharedRef<STableViewBase>& OwnerTable)
					{
						return SNew(STableRow<TWeakObjectPtr<UMaterialInterface>>, OwnerTable)
							[
								SNew(SBox)
								.VAlign(VAlign_Center)
								.Padding(6, 4)
								[
									SNew(STextBlock)
									.Font(IDetailLayoutBuilder::GetDetailFont())
									.Text(FText::FromString(Item->GetName()))
									.ToolTipText(FText::FromString(Item->GetPathName()))
								]
							];
					})
					.OnSelectionChanged_Lambda([=](TWeakObjectPtr<UMaterialInterface> Item, ESelectInfo::Type SelectInfo)
					{
						if (auto MatItem = Item.Get())
						{
							OverrideMaterial_PH->SetValue(*(UObject**)&MatItem);
						}
					})
				]
			]
		]
	]
	;
	DreamGUICategory.AddCustomRow(LOCTEXT("MaterialDomainErrorTipRow", "MaterialDomainErrorTip"))
	.Visibility(TAttribute<EVisibility>::CreateSPLambda(this, [=]()
	{
		UMaterialInterface* OverrideMaterial = nullptr;
		OverrideMaterial_PH->GetValue((UObject*&)OverrideMaterial);
		if (!OverrideMaterial)
		{
			return EVisibility::Collapsed;
		}
		auto Mat = OverrideMaterial->GetMaterial();
		if (!Mat)
		{
			return EVisibility::Collapsed;
		}
		if (Mat->MaterialDomain == EMaterialDomain::MD_Surface)
		{
			return EVisibility::Collapsed;
		}
		return EVisibility::Visible;
	}))
	.WholeRowContent()
	.MinDesiredWidth(500)
	.VAlign(VAlign_Center)
	[
		SNew(STextBlock)
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.Text(LOCTEXT("MaterialDomainErrorTip", "OverrideMaterial should use Surface domain!"))
		.ColorAndOpacity(FLinearColor(FColor::Yellow))
		.AutoWrapText(true)
	]
	;
	DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, ExpandMeshSize));
}
void FDreamTextCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (auto Script = TargetScriptPtr.Get())
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE