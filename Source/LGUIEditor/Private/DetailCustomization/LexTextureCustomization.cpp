// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexTextureCustomization.h"
#include "LGUIEditorUtils.h"
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
	TargetScriptPtr = Cast<ULexTexture>(targetObjects[0].Get());
	if (TargetScriptPtr != nullptr)
	{
		
	}
	else
	{
		UE_LOG(LGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
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
		LGUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, fillMethodProperty);
		ELexUISpriteFillMethod fillMethod = TargetScriptPtr->FillMethod;
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial360));
		switch (fillMethod)
		{
		case ELexUISpriteFillMethod::Horizontal:
		case ELexUISpriteFillMethod::Vertical:
			break;
		case ELexUISpriteFillMethod::Radial90:
		{
			TargetScriptPtr->fillOriginType_Radial90 = (ELexUISpriteFillOriginType_Radial90)TargetScriptPtr->FillOrigin;
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial90));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case ELexUISpriteFillMethod::Radial180:
		{
			TargetScriptPtr->fillOriginType_Radial180 = (ELexUISpriteFillOriginType_Radial180)TargetScriptPtr->FillOrigin;
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial180));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case ELexUISpriteFillMethod::Radial360:
		{
			TargetScriptPtr->fillOriginType_Radial360 = (ELexUISpriteFillOriginType_Radial360)TargetScriptPtr->FillOrigin;
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, fillOriginType_Radial360));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		}
		LGUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillDirectionFlip)));
		LGUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexTexture, FillAmount)));
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