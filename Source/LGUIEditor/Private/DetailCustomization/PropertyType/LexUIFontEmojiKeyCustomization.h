#pragma once
#include "DetailWidgetRow.h"
#include "Core/LexUIFontEmojiData.h"
#include "Core/LexUITextData.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexUIFontEmojiKeyCustomization"

class FLexUIFontEmojiKeyCustomization : public IPropertyTypeCustomization
{
public:
	FLexUIFontEmojiKeyCustomization(){}
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FLexUIFontEmojiKeyCustomization());
	}
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override
	{
		TArray<void*> StructPtrs;
		PropertyHandle->AccessRawData(StructPtrs);
		check(StructPtrs.Num() != 0);

		TArray<FLexUIFontEmojiKey*> Instances;
		Instances.AddZeroed(StructPtrs.Num());
		for (auto Iter = StructPtrs.CreateIterator(); Iter; ++Iter)
		{
			check(*Iter);
			Instances[Iter.GetIndex()] = (FLexUIFontEmojiKey*)(*Iter);
		}
		
		auto EmojiChar_PH = PropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FLexUIFontEmojiKey, EmojiChar));
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