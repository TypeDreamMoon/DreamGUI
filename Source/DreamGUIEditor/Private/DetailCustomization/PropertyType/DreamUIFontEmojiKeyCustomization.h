#pragma once
#include "DetailWidgetRow.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/DreamUITextData.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "DreamUIFontEmojiKeyCustomization"

class FDreamUIFontEmojiKeyCustomization : public IPropertyTypeCustomization
{
public:
	FDreamUIFontEmojiKeyCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FDreamUIFontEmojiKeyCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		TArray<void*> StructPtrs;
		PropertyHandle->AccessRawData(StructPtrs);
		check(StructPtrs.Num() != 0);

		TArray<FDreamUIFontEmojiKey*> Instances;
		Instances.AddZeroed(StructPtrs.Num());
		for (auto Iter = StructPtrs.CreateIterator(); Iter; ++Iter)
		{
			check(*Iter);
			Instances[Iter.GetIndex()] = (FDreamUIFontEmojiKey*)(*Iter);
		}
		
		auto EmojiChar_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDreamUIFontEmojiKey, EmojiChar));
		EmojiChar_PH->SetOnPropertyValueChanged(FSimpleDelegate::CreateSPLambda(this, [=]()
		{
			for (auto StructPtr : Instances)
			{
				StructPtr->ApplyEmoji();
			}
		}));
		HeaderRow
		.IsEnabled(TAttribute<bool>(PropertyHandle, &IPropertyHandle::IsEditable))
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			EmojiChar_PH->CreatePropertyValueWidget()
		];
	}
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override{}
};
#undef LOCTEXT_NAMESPACE