// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexTextureCustomization.h"
#include "LexUIEditorUtils.h"
#include "Core/Components/LexTexture.h"

#include "LGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UITextureCustomization"
FLexTextureCustomization::FLexTextureCustomization()
{
}

FLexTextureCustomization::~FLexTextureCustomization()
{
	
}

TSharedRef<IDetailCustomization> FLexTextureCustomization::MakeInstance()
{
	return MakeShareable(new FLexTextureCustomization);
}
void FLexTextureCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<ULexTexture>(targetObjects[0].Get()) : nullptr;
	if (!TargetScriptPtr.IsValid())
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("LGUI");

	auto spriteTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, DrawType));
	category.AddProperty(spriteTypeHandle);
	spriteTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexTextureCustomization::ForceRefresh, &DetailBuilder));
	auto DrawType = TargetScriptPtr->DrawType;
	if (DrawType == ELexUISpriteDrawType::Filled)
	{
		auto fillMethodProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillMethod));
		fillMethodProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FLexTextureCustomization::ForceRefresh, &DetailBuilder));
		FLexUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, fillMethodProperty);
		ELexUISpriteFillMethod fillMethod = TargetScriptPtr->FillMethod;
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial360));
		// The three radial rows are transient editor-only mirrors of the one stored FillOrigin, which
		// PostEditChangeProperty folds back whenever the user picks one; they come out of load at zero,
		// so the row about to be added has to be refreshed from FillOrigin first - on every object the
		// row will edit, not only the first, or the row reads as multiple values. A property handle is
		// the wrong tool here: it opens a transaction and dirties the package for state never saved.
		for (auto& targetObject : targetObjects)
		{
			if (auto texture = Cast<ULexTexture>(targetObject.Get()))
			{
				texture->fillOriginType_Radial90 = (ELexUISpriteFillOriginType_Radial90)texture->FillOrigin;
				texture->fillOriginType_Radial180 = (ELexUISpriteFillOriginType_Radial180)texture->FillOrigin;
				texture->fillOriginType_Radial360 = (ELexUISpriteFillOriginType_Radial360)texture->FillOrigin;
			}
		}
		switch (fillMethod)
		{
		case ELexUISpriteFillMethod::Horizontal:
		case ELexUISpriteFillMethod::Vertical:
			break;
		case ELexUISpriteFillMethod::Radial90:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial90));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case ELexUISpriteFillMethod::Radial180:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial180));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case ELexUISpriteFillMethod::Radial360:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial360));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		}
		FLexUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillDirectionFlip)));
		FLexUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillAmount)));
	}

	if (DrawType != ELexUISpriteDrawType::Sliced && DrawType != ELexUISpriteDrawType::SlicedFrame)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, PixelsPerUnitMultiplier));
	}

	if (DrawType != ELexUISpriteDrawType::Filled)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillMethod));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillDirectionFlip));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillAmount));

		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial360));
	}
}
void FLexTextureCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (TargetScriptPtr.IsValid() && DetailBuilder != nullptr)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE