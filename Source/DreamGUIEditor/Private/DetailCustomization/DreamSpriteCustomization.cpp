// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamSpriteCustomization.h"
#include "DreamUIEditorUtils.h"
#include "Core/Components/DreamSprite.h"
#include "Core/DreamUISpriteData_BaseObject.h"
#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UISpriteCustomization"
FDreamSpriteCustomization::FDreamSpriteCustomization()
{
}

FDreamSpriteCustomization::~FDreamSpriteCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamSpriteCustomization::MakeInstance()
{
	return MakeShareable(new FDreamSpriteCustomization);
}
void FDreamSpriteCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UDreamSprite>(targetObjects[0].Get()) : nullptr;
	if (!TargetScriptPtr.IsValid())
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI");

	auto spriteTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, DrawType));
	category.AddProperty(spriteTypeHandle);
	spriteTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamSpriteCustomization::ForceRefresh, &DetailBuilder));
	auto DrawType = TargetScriptPtr->DrawType;
	if (DrawType == EDreamUISpriteDrawType::Normal)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, PixelsPerUnitMultiplier));
	}
	else if (DrawType == EDreamUISpriteDrawType::Sliced || DrawType == EDreamUISpriteDrawType::SlicedFrame)
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
	else if (DrawType == EDreamUISpriteDrawType::Filled)
	{
		auto fillMethodProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillMethod));
		fillMethodProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamSpriteCustomization::ForceRefresh, &DetailBuilder));
		FDreamUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, fillMethodProperty);
		EDreamUISpriteFillMethod fillMethod = TargetScriptPtr->FillMethod;
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, PixelsPerUnitMultiplier));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial360));
		// The three radial rows are transient editor-only mirrors of the one stored FillOrigin, which
		// PostEditChangeProperty folds back whenever the user picks one; they come out of load at zero,
		// so the row about to be added has to be refreshed from FillOrigin first - on every object the
		// row will edit, not only the first, or the row reads as multiple values. A property handle is
		// the wrong tool here: it opens a transaction and dirties the package for state never saved.
		for (auto& targetObject : targetObjects)
		{
			if (auto sprite = Cast<UDreamSprite>(targetObject.Get()))
			{
				sprite->FillOriginType_Radial90 = (EDreamUISpriteFillOriginType_Radial90)sprite->FillOrigin;
				sprite->FillOriginType_Radial180 = (EDreamUISpriteFillOriginType_Radial180)sprite->FillOrigin;
				sprite->FillOriginType_Radial360 = (EDreamUISpriteFillOriginType_Radial360)sprite->FillOrigin;
			}
		}
		switch (fillMethod)
		{
		case EDreamUISpriteFillMethod::Horizontal:
		case EDreamUISpriteFillMethod::Vertical:
			break;
		case EDreamUISpriteFillMethod::Radial90:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial90));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case EDreamUISpriteFillMethod::Radial180:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial180));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case EDreamUISpriteFillMethod::Radial360:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial360));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		}
		FDreamUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillDirectionFlip)));
		FDreamUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillAmount)));
	}
	else if (DrawType == EDreamUISpriteDrawType::Filled)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, PixelsPerUnitMultiplier));
	}

	if (DrawType != EDreamUISpriteDrawType::Filled)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillMethod));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillDirectionFlip));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillAmount));

		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamSprite, FillOriginType_Radial360));
	}
}
void FDreamSpriteCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (TargetScriptPtr.IsValid() && DetailBuilder != nullptr)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE