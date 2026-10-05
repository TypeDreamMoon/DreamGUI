// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamTextCustomization.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Core/Components/DreamText.h"

#include "DreamGUIEditorModule.h"
#include "DreamDetailsMultiSelect.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IDetailsView.h"
#include "IPropertyUtilities.h"
#include "MaterialDomain.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "PropertyType/DreamTextAlignmentCustomization.h"
#include "PropertyType/DreamTextFontStyleCustomization.h"
#include "PropertyType/DreamUIFontFallbackCustomization.h"

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
	// An empty list is a real state -- the panel rebuilds while a selection is being cleared -- and
	// indexing [0] there reads off the end of an empty array.
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UDreamText>(targetObjects[0].Get()) : nullptr;
	if (TargetScriptPtr == nullptr)
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}
	// The layout builder is owned by the details view and is thrown away by the very refresh these
	// delegates ask for, so a delegate must not hold it -- by reference or as a payload pointer.
	// IPropertyUtilities is the handle that outlives a refresh, and it is what ForceRefresh takes.
	const TSharedPtr<IPropertyUtilities> PropertyUtilities = DetailBuilder.GetPropertyUtilities();
	const TSharedPtr<IDetailsView> DetailsView = DetailBuilder.GetDetailsViewSharedPtr();

	// THE ORDER OF A TEXT'S ROWS: what it says and how it looks first, then three groups that start closed --
	// how it wraps, the finer points of its typography, how it is rendered. They used to come in declaration
	// order, so the colour sat after the blend mode thirty rows down and the wrapping switches were spread
	// over three screens.
	IDetailCategoryBuilder& DreamGUICategory = DetailBuilder.EditCategory("DreamGUI");
	DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, Text));
	auto Font_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, Font));
	Font_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextCustomization::ForceRefresh, PropertyUtilities));
	DreamGUICategory.AddProperty(Font_PH);
	DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, FontSize));
	//font style
	if (DetailsView.IsValid())
	{
		DetailsView->RegisterInstancedCustomPropertyTypeLayout(TEXT("EDreamUITextFontStyle"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamTextFontStyleCustomization::MakeInstance));
	}
	DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, FontStyle));
	// UDreamVisual's, declared on the base, so by name: it would otherwise land last, after every row below.
	DreamGUICategory.AddProperty(DetailBuilder.GetProperty(FName(TEXT("Color")), UDreamVisual::StaticClass()));

	//text alignment
	{
		// Null on a host that is not an SDetailsView (a details panel embedded in another tool).
		if (DetailsView.IsValid())
		{
			DetailsView->RegisterInstancedCustomPropertyTypeLayout(TEXT("EDreamUITextParagraphHorizontalAlign"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamTextAlignmentCustomization::MakeInstance, true));
			DetailsView->RegisterInstancedCustomPropertyTypeLayout(TEXT("EDreamUITextParagraphVerticalAlign"), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamTextAlignmentCustomization::MakeInstance, false));
		}
		auto HAlign_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, HAlign));
		DreamGUICategory.AddProperty(HAlign_PH);
		// How a justified line is spread, and how its last line aligns, mean nothing under any other alignment. A
		// selection that disagrees shows them, for the same reason as bRichText below: some of it is justified.
		const TAttribute<EVisibility> JustifyOptionsVisibility = TAttribute<EVisibility>::CreateLambda([HAlign_PH]()
		{
			uint8 Value = 0;
			const FPropertyAccess::Result Result = HAlign_PH->GetValue(Value);
			const bool bJustified = Result == FPropertyAccess::MultipleValues
				|| (Result == FPropertyAccess::Success && Value == (uint8)EDreamUITextParagraphHorizontalAlign::Justify);
			return bJustified ? EVisibility::Visible : EVisibility::Collapsed;
		});
		DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, TextJustify)).Visibility(JustifyOptionsVisibility);
		DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, LastLineAlign)).Visibility(JustifyOptionsVisibility);
		DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, VAlign));
	}

	auto OverflowTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, OverflowType));
	OverflowTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextCustomization::ForceRefresh, PropertyUtilities));
	DreamGUICategory.AddProperty(OverflowTypeHandle);
	DreamGUICategory.AddProperty(GET_MEMBER_NAME_CHECKED(UDreamText, bAutoWrapText));

	TArray<FName> NeedToHidePropertyNames;
	auto RichText_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, bRichText));
	RichText_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextCustomization::ForceRefresh, PropertyUtilities));
	// True as the fallback because it is the value that hides NOTHING: a selection that disagrees still
	// has objects using the rich-text properties, and hiding them would hide live properties.
	const bool bRichText = DreamDetailsMultiSelect::ValueOr<bool>(RichText_PH, true);
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

	//wrapping: where a line breaks and how much room the lines take
	{
		IDetailGroup& WrappingGroup = DreamGUICategory.AddGroup(FName(TEXT("TextWrapping")), LOCTEXT("WrappingGroup", "Wrapping"));
		for (const FName Field : {
			GET_MEMBER_NAME_CHECKED(UDreamText, WrapTextAt),
			GET_MEMBER_NAME_CHECKED(UDreamText, WrappingPolicy),
			GET_MEMBER_NAME_CHECKED(UDreamText, PhraseWrap),
			GET_MEMBER_NAME_CHECKED(UDreamText, Margin),
			GET_MEMBER_NAME_CHECKED(UDreamText, LineHeightPercentage),
			GET_MEMBER_NAME_CHECKED(UDreamText, MinDesiredWidth),
			GET_MEMBER_NAME_CHECKED(UDreamText, bBestFit),
			GET_MEMBER_NAME_CHECKED(UDreamText, BestFitMinSize) })
		{
			WrappingGroup.AddPropertyRow(DetailBuilder.GetProperty(Field));
		}
	}

	//typography: the language the text is in, and how its glyphs are set
	{
		IDetailGroup& TypographyGroup = DreamGUICategory.AddGroup(FName(TEXT("TextTypography")), LOCTEXT("TypographyGroup", "Typography"));
		//language: the culture the text is written in, picked from the engine's cultures; none is the game's language
		{
			auto Language_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, Language));
			const FText GameLanguageText = LOCTEXT("GameLanguage", "Game language");
			TypographyGroup.AddPropertyRow(Language_PH)
			.CustomWidget()
			.NameContent()
			[
				Language_PH->CreatePropertyNameWidget()
			]
			.ValueContent()
			.MinDesiredWidth(200.0f)
			[
				DreamUICulturePicker::MakeComboButton(
					TAttribute<FText>::CreateLambda([Language_PH, GameLanguageText]() -> FText
					{
						FString Value;
						switch (Language_PH->GetValue(Value))
						{
						case FPropertyAccess::Success: return DreamUICulturePicker::GetCultureDisplayText(Value, GameLanguageText);
						case FPropertyAccess::MultipleValues: return LOCTEXT("MultipleLanguages", "Multiple Values");
						default: return FText::GetEmpty();
						}
					}),
					LOCTEXT("Language_Tooltip", "The language the text is written in: the font's fallbacks meant for it are preferred, and the shaper draws its script the way that language does. Game language follows the game's current language."),
					GameLanguageText,
					[Language_PH](const FString& InCultureName) { Language_PH->SetValue(InCultureName); },
					[Language_PH]()
					{
						FString Value;
						Language_PH->GetValue(Value);
						return Value;
					})
			];
		}
		for (const FName Field : {
			GET_MEMBER_NAME_CHECKED(UDreamText, FontSpace),
			GET_MEMBER_NAME_CHECKED(UDreamText, TabSize),
			GET_MEMBER_NAME_CHECKED(UDreamText, bUseKerning),
			GET_MEMBER_NAME_CHECKED(UDreamText, bLigatures),
			GET_MEMBER_NAME_CHECKED(UDreamText, bUnderline),
			GET_MEMBER_NAME_CHECKED(UDreamText, bStrikethrough),
			GET_MEMBER_NAME_CHECKED(UDreamText, TextTransform),
			GET_MEMBER_NAME_CHECKED(UDreamText, FlowDirection) })
		{
			TypographyGroup.AddPropertyRow(DetailBuilder.GetProperty(Field));
		}
	}

	//paint: the gradients a text is filled with, and what animates them, as one group under the style. The fields are split
	//between the style (where rich-text styles and the lyrics view get them too) and the text (the phases Sequencer keys),
	//so either half on its own would show half of the feature.
	{
		auto TextStyle_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, TextStyle));
		DreamGUICategory.AddProperty(TextStyle_PH);
		IDetailGroup& PaintGroup = DreamGUICategory.AddGroup(FName(TEXT("TextPaint")), LOCTEXT("PaintGroup", "Paint"));
		for (const FName StyleField : {
			GET_MEMBER_NAME_CHECKED(FDreamTextStyle, FacePaint),
			GET_MEMBER_NAME_CHECKED(FDreamTextStyle, OutlinePaint),
			GET_MEMBER_NAME_CHECKED(FDreamTextStyle, OverlayPaint),
			GET_MEMBER_NAME_CHECKED(FDreamTextStyle, OverlayBlend),
			GET_MEMBER_NAME_CHECKED(FDreamTextStyle, PaintBoxHorizontal),
			GET_MEMBER_NAME_CHECKED(FDreamTextStyle, PaintBoxVertical) })
		{
			// Taken out of the style's own rows by being placed here.
			if (const TSharedPtr<IPropertyHandle> Field = TextStyle_PH->GetChildHandle(StyleField, /*bRecurse*/ false))
			{
				PaintGroup.AddPropertyRow(Field.ToSharedRef());
			}
		}
		for (const FName TextField : {
			GET_MEMBER_NAME_CHECKED(UDreamText, FacePaintPhase),
			GET_MEMBER_NAME_CHECKED(UDreamText, OutlinePaintPhase),
			GET_MEMBER_NAME_CHECKED(UDreamText, OverlayPaintPhase),
			GET_MEMBER_NAME_CHECKED(UDreamText, PaintAngleOffset) })
		{
			PaintGroup.AddPropertyRow(DetailBuilder.GetProperty(TextField));
		}
	}

	//rendering: the material the text is drawn with and how its mesh is built
	IDetailGroup& RenderingGroup = DreamGUICategory.AddGroup(FName(TEXT("TextRendering")), LOCTEXT("RenderingGroup", "Rendering"));
	auto OverrideMaterial_PH = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, OverrideMaterial));
	OverrideMaterial_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextCustomization::ForceRefresh, PropertyUtilities));
	RenderingGroup.AddPropertyRow(OverrideMaterial_PH);
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
	RenderingGroup.AddWidgetRow()
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
	RenderingGroup.AddWidgetRow()
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
	// UDreamVisualBatchMesh's, declared on the base, so by name.
	RenderingGroup.AddPropertyRow(DetailBuilder.GetProperty(FName(TEXT("PropertiesForMaterial")), UDreamVisualBatchMesh::StaticClass()));
	RenderingGroup.AddPropertyRow(DetailBuilder.GetProperty(FName(TEXT("BlendMode")), UDreamVisualBatchMesh::StaticClass()));
	RenderingGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, ExpandMeshSize)));
	RenderingGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, SmallTextRaster)));
	RenderingGroup.AddPropertyRow(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamText, DynamicPixelsPerUnit)));
}
void FDreamTextCustomization::ForceRefresh(TSharedPtr<IPropertyUtilities> PropertyUtilities)
{
	if (TargetScriptPtr.IsValid() && PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
}
#undef LOCTEXT_NAMESPACE