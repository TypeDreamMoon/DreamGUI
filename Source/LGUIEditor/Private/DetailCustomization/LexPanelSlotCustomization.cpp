// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "DetailCustomization/LexPanelSlotCustomization.h"

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "LexPanelSlotCustomization"

namespace LexPanelSlotCustomizationLocal
{
	struct FPanelContext
	{
		bool bPanel = false;
		bool bCanvas = false;
		bool bLinear = false;
		bool bGrid = false;
		bool bUniformGrid = false;
	};

	FPanelContext GetPanelContext(const ULexLayoutContainer* ParentLayout)
	{
		FPanelContext Result;
		Result.bPanel = ParentLayout && ParentLayout->IsA<ULexPanelLayoutBase>();
		Result.bCanvas = ParentLayout && ParentLayout->IsA<ULexLayoutContainerCanvasPanel>();
		Result.bLinear = ParentLayout && ParentLayout->IsA<ULexLayoutContainerStackBox>();
		Result.bGrid = ParentLayout && ParentLayout->IsA<ULexLayoutContainerGridPanel>();
		Result.bUniformGrid = ParentLayout && ParentLayout->IsA<ULexLayoutContainerUniformGridPanel>();
		return Result;
	}

	void CustomizeHorizontalAlignment(IDetailPropertyRow* Row)
	{
		if (!Row)
		{
			return;
		}
		const TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
		if (!Handle.IsValid())
		{
			return;
		}
		Row->CustomWidget()
		.NameContent()[Handle->CreatePropertyNameWidget()]
		.ValueContent().MinDesiredWidth(150).MaxDesiredWidth(220)
		[
			SNew(SSegmentedControl<ELexPanelHorizontalAlignment>)
			.Value_Lambda([Handle]()
			{
				uint8 Value = static_cast<uint8>(ELexPanelHorizontalAlignment::Fill);
				Handle->GetValue(Value);
				return static_cast<ELexPanelHorizontalAlignment>(Value);
			})
			.OnValueChanged_Lambda([Handle](ELexPanelHorizontalAlignment Value)
			{
				Handle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<ELexPanelHorizontalAlignment>::Slot(ELexPanelHorizontalAlignment::Left)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Left"))).ToolTip(LOCTEXT("AlignLeft", "Align Left"))
			+ SSegmentedControl<ELexPanelHorizontalAlignment>::Slot(ELexPanelHorizontalAlignment::Center)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Center"))).ToolTip(LOCTEXT("AlignHCenter", "Align Horizontal Center"))
			+ SSegmentedControl<ELexPanelHorizontalAlignment>::Slot(ELexPanelHorizontalAlignment::Right)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Right"))).ToolTip(LOCTEXT("AlignRight", "Align Right"))
			+ SSegmentedControl<ELexPanelHorizontalAlignment>::Slot(ELexPanelHorizontalAlignment::Fill)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Fill"))).ToolTip(LOCTEXT("AlignHFill", "Fill Horizontally"))
		];
	}

	void CustomizeVerticalAlignment(IDetailPropertyRow* Row)
	{
		if (!Row)
		{
			return;
		}
		const TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
		if (!Handle.IsValid())
		{
			return;
		}
		Row->CustomWidget()
		.NameContent()[Handle->CreatePropertyNameWidget()]
		.ValueContent().MinDesiredWidth(150).MaxDesiredWidth(220)
		[
			SNew(SSegmentedControl<ELexPanelVerticalAlignment>)
			.Value_Lambda([Handle]()
			{
				uint8 Value = static_cast<uint8>(ELexPanelVerticalAlignment::Fill);
				Handle->GetValue(Value);
				return static_cast<ELexPanelVerticalAlignment>(Value);
			})
			.OnValueChanged_Lambda([Handle](ELexPanelVerticalAlignment Value)
			{
				Handle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<ELexPanelVerticalAlignment>::Slot(ELexPanelVerticalAlignment::Top)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Top"))).ToolTip(LOCTEXT("AlignTop", "Align Top"))
			+ SSegmentedControl<ELexPanelVerticalAlignment>::Slot(ELexPanelVerticalAlignment::Center)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Center"))).ToolTip(LOCTEXT("AlignVCenter", "Align Vertical Center"))
			+ SSegmentedControl<ELexPanelVerticalAlignment>::Slot(ELexPanelVerticalAlignment::Bottom)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Bottom"))).ToolTip(LOCTEXT("AlignBottom", "Align Bottom"))
			+ SSegmentedControl<ELexPanelVerticalAlignment>::Slot(ELexPanelVerticalAlignment::Fill)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Fill"))).ToolTip(LOCTEXT("AlignVFill", "Fill Vertically"))
		];
	}

	TSharedPtr<IPropertyHandle> CustomizeSizeRule(IDetailPropertyRow* Row)
	{
		if (!Row)
		{
			return nullptr;
		}
		const TSharedPtr<IPropertyHandle> Handle = Row->GetPropertyHandle();
		if (!Handle.IsValid())
		{
			return nullptr;
		}
		Row->CustomWidget()
		.NameContent()[Handle->CreatePropertyNameWidget()]
		.ValueContent().MinDesiredWidth(150).MaxDesiredWidth(220)
		[
			SNew(SSegmentedControl<ELexPanelSizeRule>)
			.Value_Lambda([Handle]()
			{
				uint8 Value = static_cast<uint8>(ELexPanelSizeRule::Auto);
				Handle->GetValue(Value);
				return static_cast<ELexPanelSizeRule>(Value);
			})
			.OnValueChanged_Lambda([Handle](ELexPanelSizeRule Value)
			{
				Handle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<ELexPanelSizeRule>::Slot(ELexPanelSizeRule::Auto).Text(LOCTEXT("AutoSizeRule", "Auto"))
			+ SSegmentedControl<ELexPanelSizeRule>::Slot(ELexPanelSizeRule::Fill).Text(LOCTEXT("FillSizeRule", "Fill"))
		];
		return Handle;
	}
}

TSharedRef<IDetailCustomization> FLexPanelSlotCustomization::MakeInstance()
{
	return MakeShared<FLexPanelSlotCustomization>();
}

void FLexPanelSlotCustomization::AddSlotProperties(
	IDetailCategoryBuilder& Category,
	const TArray<UObject*>& SlotObjects,
	const ULexLayoutContainer* ParentLayout)
{
	using namespace LexPanelSlotCustomizationLocal;
	const FPanelContext Context = GetPanelContext(ParentLayout);
	if (!Context.bPanel || SlotObjects.IsEmpty())
	{
		return;
	}

	if (!Context.bCanvas)
	{
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, Padding));
		CustomizeHorizontalAlignment(Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, HorizontalAlignment)));
		CustomizeVerticalAlignment(Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, VerticalAlignment)));
	}
	if (Context.bLinear)
	{
		const TSharedPtr<IPropertyHandle> SizeRuleHandle = CustomizeSizeRule(Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, SizeRule)));
		if (IDetailPropertyRow* FillWeightRow = Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, FillWeight)))
		{
			FillWeightRow->IsEnabled(TAttribute<bool>::CreateLambda([SizeRuleHandle]()
			{
				uint8 Value = static_cast<uint8>(ELexPanelSizeRule::Auto);
				return SizeRuleHandle.IsValid()
					&& SizeRuleHandle->GetValue(Value) == FPropertyAccess::Result::Success
					&& static_cast<ELexPanelSizeRule>(Value) == ELexPanelSizeRule::Fill;
			}));
		}
	}
	if (Context.bGrid || Context.bUniformGrid)
	{
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, Row));
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, Column));
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, RowSpan));
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, ColumnSpan));
	}
	if (Context.bGrid)
	{
		if (IDetailPropertyRow* LayerRow = Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, ZOrder)))
		{
			LayerRow->DisplayName(LOCTEXT("Layer", "Layer"));
		}
	}
	if (Context.bCanvas)
	{
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, ZOrder));
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, bAutoSize));
	}
}

void FLexPanelSlotCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	TargetSlots.Reset();
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (ULexPanelSlot* Slot = Cast<ULexPanelSlot>(Object.Get()))
		{
			TargetSlots.Add(Slot);
		}
	}
	if (TargetSlots.IsEmpty() || !TargetSlots[0].IsValid())
	{
		return;
	}

	ULexWidget* Widget = TargetSlots[0]->GetWidget();
	ULexLayoutContainer* ParentLayout = Widget && Widget->GetParent() ? Widget->GetParent()->GetLayoutContainer() : nullptr;
	const bool bCanvas = ParentLayout && ParentLayout->IsA<ULexLayoutContainerCanvasPanel>();
	const bool bLinear = ParentLayout && ParentLayout->IsA<ULexLayoutContainerStackBox>();
	const bool bGrid = ParentLayout && ParentLayout->IsA<ULexLayoutContainerGridPanel>();
	const bool bUniformGrid = ParentLayout && ParentLayout->IsA<ULexLayoutContainerUniformGridPanel>();
	const bool bPanel = ParentLayout && ParentLayout->IsA<ULexPanelLayoutBase>();

	auto PaddingHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, Padding));
	auto HorizontalHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, HorizontalAlignment));
	auto VerticalHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, VerticalAlignment));
	auto SizeRuleHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, SizeRule));
	auto FillWeightHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, FillWeight));
	auto RowHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, Row));
	auto ColumnHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, Column));
	auto RowSpanHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, RowSpan));
	auto ColumnSpanHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, ColumnSpan));
	auto ZOrderHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, ZOrder));
	auto AutoSizeHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(ULexPanelSlot, bAutoSize));

	DetailBuilder.HideProperty(PaddingHandle);
	DetailBuilder.HideProperty(HorizontalHandle);
	DetailBuilder.HideProperty(VerticalHandle);
	DetailBuilder.HideProperty(SizeRuleHandle);
	DetailBuilder.HideProperty(FillWeightHandle);
	DetailBuilder.HideProperty(RowHandle);
	DetailBuilder.HideProperty(ColumnHandle);
	DetailBuilder.HideProperty(RowSpanHandle);
	DetailBuilder.HideProperty(ColumnSpanHandle);
	DetailBuilder.HideProperty(ZOrderHandle);
	DetailBuilder.HideProperty(AutoSizeHandle);

	if (!bPanel)
	{
		return;
	}

	IDetailCategoryBuilder& SlotCategory = DetailBuilder.EditCategory(TEXT("Slot"), LOCTEXT("SlotCategory", "Slot"), ECategoryPriority::Important);
	if (!bCanvas)
	{
		SlotCategory.AddProperty(PaddingHandle);
		SlotCategory.AddCustomRow(HorizontalHandle->GetPropertyDisplayName())
		.NameContent()[HorizontalHandle->CreatePropertyNameWidget()]
		.ValueContent().MinDesiredWidth(150).MaxDesiredWidth(220)
		[
			SNew(SSegmentedControl<ELexPanelHorizontalAlignment>)
			.Value_Lambda([HorizontalHandle]()
			{
				uint8 Value = static_cast<uint8>(ELexPanelHorizontalAlignment::Fill);
				HorizontalHandle->GetValue(Value);
				return static_cast<ELexPanelHorizontalAlignment>(Value);
			})
			.OnValueChanged_Lambda([HorizontalHandle](ELexPanelHorizontalAlignment Value)
			{
				HorizontalHandle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<ELexPanelHorizontalAlignment>::Slot(ELexPanelHorizontalAlignment::Left)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Left"))).ToolTip(LOCTEXT("AlignLeft", "Align Left"))
			+ SSegmentedControl<ELexPanelHorizontalAlignment>::Slot(ELexPanelHorizontalAlignment::Center)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Center"))).ToolTip(LOCTEXT("AlignHCenter", "Align Horizontal Center"))
			+ SSegmentedControl<ELexPanelHorizontalAlignment>::Slot(ELexPanelHorizontalAlignment::Right)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Right"))).ToolTip(LOCTEXT("AlignRight", "Align Right"))
			+ SSegmentedControl<ELexPanelHorizontalAlignment>::Slot(ELexPanelHorizontalAlignment::Fill)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Fill"))).ToolTip(LOCTEXT("AlignHFill", "Fill Horizontally"))
		];

		SlotCategory.AddCustomRow(VerticalHandle->GetPropertyDisplayName())
		.NameContent()[VerticalHandle->CreatePropertyNameWidget()]
		.ValueContent().MinDesiredWidth(150).MaxDesiredWidth(220)
		[
			SNew(SSegmentedControl<ELexPanelVerticalAlignment>)
			.Value_Lambda([VerticalHandle]()
			{
				uint8 Value = static_cast<uint8>(ELexPanelVerticalAlignment::Fill);
				VerticalHandle->GetValue(Value);
				return static_cast<ELexPanelVerticalAlignment>(Value);
			})
			.OnValueChanged_Lambda([VerticalHandle](ELexPanelVerticalAlignment Value)
			{
				VerticalHandle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<ELexPanelVerticalAlignment>::Slot(ELexPanelVerticalAlignment::Top)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Top"))).ToolTip(LOCTEXT("AlignTop", "Align Top"))
			+ SSegmentedControl<ELexPanelVerticalAlignment>::Slot(ELexPanelVerticalAlignment::Center)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Center"))).ToolTip(LOCTEXT("AlignVCenter", "Align Vertical Center"))
			+ SSegmentedControl<ELexPanelVerticalAlignment>::Slot(ELexPanelVerticalAlignment::Bottom)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Bottom"))).ToolTip(LOCTEXT("AlignBottom", "Align Bottom"))
			+ SSegmentedControl<ELexPanelVerticalAlignment>::Slot(ELexPanelVerticalAlignment::Fill)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Fill"))).ToolTip(LOCTEXT("AlignVFill", "Fill Vertically"))
		];
	}

	if (bLinear)
	{
		SlotCategory.AddCustomRow(SizeRuleHandle->GetPropertyDisplayName())
		.NameContent()[SizeRuleHandle->CreatePropertyNameWidget()]
		.ValueContent().MinDesiredWidth(150).MaxDesiredWidth(220)
		[
			SNew(SSegmentedControl<ELexPanelSizeRule>)
			.Value_Lambda([SizeRuleHandle]()
			{
				uint8 Value = static_cast<uint8>(ELexPanelSizeRule::Auto);
				SizeRuleHandle->GetValue(Value);
				return static_cast<ELexPanelSizeRule>(Value);
			})
			.OnValueChanged_Lambda([SizeRuleHandle](ELexPanelSizeRule Value)
			{
				SizeRuleHandle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<ELexPanelSizeRule>::Slot(ELexPanelSizeRule::Auto).Text(LOCTEXT("AutoSizeRule", "Auto"))
			+ SSegmentedControl<ELexPanelSizeRule>::Slot(ELexPanelSizeRule::Fill).Text(LOCTEXT("FillSizeRule", "Fill"))
		];
		SlotCategory.AddProperty(FillWeightHandle)
			.IsEnabled(TAttribute<bool>::CreateLambda([SizeRuleHandle]()
			{
				uint8 Value = static_cast<uint8>(ELexPanelSizeRule::Auto);
				return SizeRuleHandle->GetValue(Value) == FPropertyAccess::Result::Success
					&& static_cast<ELexPanelSizeRule>(Value) == ELexPanelSizeRule::Fill;
			}));
	}
	if (bGrid || bUniformGrid)
	{
		SlotCategory.AddProperty(RowHandle);
		SlotCategory.AddProperty(ColumnHandle);
		SlotCategory.AddProperty(RowSpanHandle);
		SlotCategory.AddProperty(ColumnSpanHandle);
	}
	if (bGrid)
	{
		SlotCategory.AddProperty(ZOrderHandle).DisplayName(LOCTEXT("Layer", "Layer"));
	}
	if (bCanvas)
	{
		SlotCategory.AddProperty(ZOrderHandle);
		SlotCategory.AddProperty(AutoSizeHandle);
	}
}

#undef LOCTEXT_NAMESPACE
