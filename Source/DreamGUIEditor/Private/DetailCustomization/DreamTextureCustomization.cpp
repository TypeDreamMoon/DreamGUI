// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/DreamTextureCustomization.h"
#include "DreamUIEditorUtils.h"
#include "Core/Components/DreamTexture.h"

#include "DreamGUIEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"

#define LOCTEXT_NAMESPACE "UITextureCustomization"
FDreamTextureCustomization::FDreamTextureCustomization()
{
}

FDreamTextureCustomization::~FDreamTextureCustomization()
{
	
}

TSharedRef<IDetailCustomization> FDreamTextureCustomization::MakeInstance()
{
	return MakeShareable(new FDreamTextureCustomization);
}
void FDreamTextureCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> targetObjects;
	DetailBuilder.GetObjectsBeingCustomized(targetObjects);
	TargetScriptPtr = targetObjects.Num() > 0 ? Cast<UDreamTexture>(targetObjects[0].Get()) : nullptr;
	if (!TargetScriptPtr.IsValid())
	{
		UE_LOG(DreamGUIEditor, Log, TEXT("[%s].%d Get TargetScript is null"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return;
	}

	IDetailCategoryBuilder& category = DetailBuilder.EditCategory("DreamGUI");

	auto spriteTypeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, DrawType));
	category.AddProperty(spriteTypeHandle);
	spriteTypeHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextureCustomization::ForceRefresh, &DetailBuilder));
	auto DrawType = TargetScriptPtr->DrawType;
	if (DrawType == EDreamUISpriteDrawType::Filled)
	{
		auto fillMethodProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, FillMethod));
		fillMethodProperty->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &FDreamTextureCustomization::ForceRefresh, &DetailBuilder));
		FDreamUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, fillMethodProperty);
		EDreamUISpriteFillMethod fillMethod = TargetScriptPtr->FillMethod;
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial360));
		// The three radial rows are transient editor-only mirrors of the one stored FillOrigin, which
		// PostEditChangeProperty folds back whenever the user picks one; they come out of load at zero,
		// so the row about to be added has to be refreshed from FillOrigin first - on every object the
		// row will edit, not only the first, or the row reads as multiple values. A property handle is
		// the wrong tool here: it opens a transaction and dirties the package for state never saved.
		for (auto& targetObject : targetObjects)
		{
			if (auto texture = Cast<UDreamTexture>(targetObject.Get()))
			{
				texture->fillOriginType_Radial90 = (EDreamUISpriteFillOriginType_Radial90)texture->FillOrigin;
				texture->fillOriginType_Radial180 = (EDreamUISpriteFillOriginType_Radial180)texture->FillOrigin;
				texture->fillOriginType_Radial360 = (EDreamUISpriteFillOriginType_Radial360)texture->FillOrigin;
			}
		}
		switch (fillMethod)
		{
		case EDreamUISpriteFillMethod::Horizontal:
		case EDreamUISpriteFillMethod::Vertical:
			break;
		case EDreamUISpriteFillMethod::Radial90:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial90));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case EDreamUISpriteFillMethod::Radial180:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial180));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		case EDreamUISpriteFillMethod::Radial360:
		{
			auto originTypeRadialProperty = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial360));
			originTypeRadialProperty->SetPropertyDisplayName(LOCTEXT("FillOrigin", "    Fill Origin"));
			category.AddProperty(originTypeRadialProperty);
		}
			break;
		}
		FDreamUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, FillDirectionFlip)));
		FDreamUIEditorUtils::CreateSubDetail(&category, &DetailBuilder, DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, FillAmount)));
	}

	if (DrawType != EDreamUISpriteDrawType::Sliced && DrawType != EDreamUISpriteDrawType::SlicedFrame)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, PixelsPerUnitMultiplier));
	}

	if (DrawType != EDreamUISpriteDrawType::Filled)
	{
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, FillMethod));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, FillOrigin));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, FillDirectionFlip));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, FillAmount));

		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial90));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial180));
		DetailBuilder.HideProperty(GET_MEMBER_NAME_CHECKED(UDreamTexture, fillOriginType_Radial360));
	}
}
void FDreamTextureCustomization::ForceRefresh(IDetailLayoutBuilder* DetailBuilder)
{
	if (TargetScriptPtr.IsValid() && DetailBuilder != nullptr)
	{
		DetailBuilder->ForceRefreshDetails();
	}
}
#undef LOCTEXT_NAMESPACE