// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/PropertyType/DreamUIRichTextCustomStyleItemCustomization.h"

#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/Text/DreamTextPaint.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h"
#include "PropertyHandle.h"

#define LOCTEXT_NAMESPACE "DreamUIRichTextCustomStyleItemCustomization"

TSharedRef<IPropertyTypeCustomization> FDreamUIRichTextCustomStyleItemCustomization::MakeInstance()
{
	return MakeShareable(new FDreamUIRichTextCustomStyleItemCustomization());
}

void FDreamUIRichTextCustomStyleItemCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	// An entry of the style map: the row puts the entry's key, its tag name, in the name column whatever this says.
	HeaderRow
	.NameContent()
	[
		PropertyHandle->CreatePropertyNameWidget()
	];
}

void FDreamUIRichTextCustomStyleItemCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	const TSharedPtr<IPropertyHandle> PaintTypeHandle =
		PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIRichTextCustomStyleItemData, paintType), /*bRecurse*/ false);
	// Set, or a selection where any entry is Set.
	const TAttribute<EVisibility> PaintVisibility = TAttribute<EVisibility>::CreateLambda([PaintTypeHandle]()
	{
		uint8 PaintType = 0;
		const FPropertyAccess::Result Result = PaintTypeHandle.IsValid() ? PaintTypeHandle->GetValue(PaintType) : FPropertyAccess::Fail;
		const bool bSet = Result == FPropertyAccess::MultipleValues
			|| (Result == FPropertyAccess::Success && PaintType == (uint8)EDreamUIRichTextCustomStyleData_PaintType::Set);
		return bSet ? EVisibility::Visible : EVisibility::Collapsed;
	});

	const FName PaintName = GET_MEMBER_NAME_CHECKED(FDreamUIRichTextCustomStyleItemData, paint);
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		const TSharedPtr<IPropertyHandle> Child = PropertyHandle->GetChildHandle(ChildIndex);
		if (!Child.IsValid() || Child->GetProperty() == nullptr)
		{
			continue;
		}
		if (Child->GetProperty()->GetFName() != PaintName)
		{
			ChildBuilder.AddProperty(Child.ToSharedRef());
			continue;
		}
		const TSharedPtr<IPropertyHandle> PresetHandle = Child->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamTextPaint, Preset), /*bRecurse*/ false);
		const TSharedPtr<IPropertyHandle> GradientHandle = Child->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamTextPaint, Gradient), /*bRecurse*/ false);
		if (!PresetHandle.IsValid() || !GradientHandle.IsValid())
		{
			ChildBuilder.AddProperty(Child.ToSharedRef()).Visibility(PaintVisibility);
			continue;
		}
		// The paint's own two fields, a level up. Their EditCondition is the bEnabled a custom style does not read: overridden,
		// or they would stay greyed out behind a switch that is not there.
		ChildBuilder.AddProperty(PresetHandle.ToSharedRef())
			.DisplayName(LOCTEXT("PaintPreset", "Paint Preset"))
			.Visibility(PaintVisibility)
			.EditCondition(true, FOnBooleanValueChanged());
		ChildBuilder.AddProperty(GradientHandle.ToSharedRef())
			.DisplayName(LOCTEXT("PaintGradient", "Paint Gradient"))
			.Visibility(PaintVisibility)
			.EditCondition(true, FOnBooleanValueChanged());
	}
}

#undef LOCTEXT_NAMESPACE
