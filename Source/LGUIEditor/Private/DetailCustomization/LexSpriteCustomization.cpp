// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexSpriteCustomization.h"
#include "LexUIEditorUtils.h"
#include "Core/Components/LexSprite.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UISpriteCustomization"
FLexSpriteCustomization::FLexSpriteCustomization()
{
}

FLexSpriteCustomization::~FLexSpriteCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexSpriteCustomization::MakeInstance()
{
	return MakeShareable(new FLexSpriteCustomization);
}
void FLexSpriteCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<ULexSprite>(targetObjects[0].Get()) : nullptr;
	if (!TargetScriptPtr.IsValid())
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("LGUI");

	auto spriteTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, DrawType));
	category.AddProperty(spriteTypeHandle);
	spriteTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexSpriteCustomization::ForceRefresh, &DetailBuilder));
	auto DrawType = TargetScriptPtr->DrawType;
	if (DrawType == ELexUISpriteDrawType::Normal)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, PixelsPerUnitMultiplier));
	}
	else if (DrawType == ELexUISpriteDrawType::Sliced || DrawType == ELexUISpriteDrawType::SlicedFrame)
	{
		if (TargetScriptPtr->Sprite != nullptr)
		{
			if (TargetScriptPtr->Sprite->GetSpriteInfo().HasBorder() == false)
			{
				category.AddCustomRow(LOCTEXT("NoBorderWarning", "NoBorderWarning"))
					.WholeRowContent()
					.MinDesiredWidth(300)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text(LOCTEXT("Warning", "Target Sprite does not have any border information!"))
						.ColorAndOpacity(FSlateColor(FLinearColor::Red))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					];
			}
		}
	}
	else if (DrawType == ELexUISpriteDrawType::Filled)
	{
		auto fillMethodProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillMethod));
		fillMethodProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexSpriteCustomization::ForceRefresh, &DetailBuilder));
		FLexUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, fillMethodProperty);
		ELexUISpriteFillMethod fillMethod = TargetScriptPtr->FillMethod;
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, PixelsPerUnitMultiplier));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial360));
		// The three radial rows are transient editor-only mirrors of the one stored FillOrigin, which
		// PostEditChangeProperty folds back whenever the user picks one; they come out of load at zero,
		// so the row about to be added has to be refreshed from FillOrigin first - on every object the
		// row will edit, not only the first, or the row reads as multiple values. A property handle is
		// the wrong tool here: it opens a transaction and dirties the package for state never saved.
		for (auto& targetObject : targetObjects)
		{
			if (auto sprite = Cast<ULexSprite>(targetObject.Get()))
			{
				sprite->FillOriginType_Radial90 = (ELexUISpriteFillOriginType_Radial90)sprite->FillOrigin;
				sprite->FillOriginType_Radial180 = (ELexUISpriteFillOriginType_Radial180)sprite->FillOrigin;
				sprite->FillOriginType_Radial360 = (ELexUISpriteFillOriginType_Radial360)sprite->FillOrigin;
			}
		}
		switch (fillMethod)
		{
		case ELexUISpriteFillMethod::Horizontal:
		case ELexUISpriteFillMethod::Vertical:
			break;
		case ELexUISpriteFillMethod::Radial90:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial90));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case ELexUISpriteFillMethod::Radial180:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial180));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case ELexUISpriteFillMethod::Radial360:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial360));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		}
		FLexUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillDirectionFlip)));
		FLexUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillAmount)));
	}
	else if (DrawType == ELexUISpriteDrawType::Filled)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, PixelsPerUnitMultiplier));
	}

	if (DrawType != ELexUISpriteDrawType::Filled)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillMethod));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillDirectionFlip));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillAmount));

		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexSprite, FillOriginType_Radial360));
	}
}
void FLexSpriteCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (TargetScriptPtr.IsValid() && DetailBuilder != nullptr)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE