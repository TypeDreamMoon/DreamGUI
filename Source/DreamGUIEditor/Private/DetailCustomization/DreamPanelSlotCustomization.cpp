// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/DreamPanelSlotCustomization.h"

#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailPropertyRow.h"
#include "DetailWidgetRow.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SSegmentedControl.h"

#define LOCTEXT_NAMESPACE "DreamPanelSlotCustomization"

namespace DreamPanelSlotCustomizationLocal
{
	struct FPanelContext
	{
		bool bPanel = false;
		bool bCanvas = false;
		bool bOverlay = false;
		bool bLinear = false;
		bool bGrid = false;
		bool bUniformGrid = false;
	};

	FPanelContext GetPanelContext(const UDreamLayoutContainer* ParentLayout)
	{
		FPanelContext Result;
		Result.bPanel = ParentLayout && ParentLayout->IsA<UDreamPanelLayoutBase>();
		Result.bCanvas = ParentLayout && ParentLayout->IsA<UDreamLayoutContainerCanvasPanel>();
		Result.bOverlay = ParentLayout && ParentLayout->IsA<UDreamLayoutContainerOverlay>();
		Result.bLinear = ParentLayout && ParentLayout->IsA<UDreamLayoutContainerStackBox>();
		Result.bGrid = ParentLayout && ParentLayout->IsA<UDreamLayoutContainerGridPanel>();
		Result.bUniformGrid = ParentLayout && ParentLayout->IsA<UDreamLayoutContainerUniformGridPanel>();
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
			SNew(SSegmentedControl<EDreamPanelHorizontalAlignment>)
			.Value_Lambda([Handle]()
			{
				uint8 Value = static_cast<uint8>(EDreamPanelHorizontalAlignment::Fill);
				Handle->GetValue(Value);
				return static_cast<EDreamPanelHorizontalAlignment>(Value);
			})
			.OnValueChanged_Lambda([Handle](EDreamPanelHorizontalAlignment Value)
			{
				Handle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<EDreamPanelHorizontalAlignment>::Slot(EDreamPanelHorizontalAlignment::Left)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Left"))).ToolTip(LOCTEXT("AlignLeft", "Align Left"))
			+ SSegmentedControl<EDreamPanelHorizontalAlignment>::Slot(EDreamPanelHorizontalAlignment::Center)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Center"))).ToolTip(LOCTEXT("AlignHCenter", "Align Horizontal Center"))
			+ SSegmentedControl<EDreamPanelHorizontalAlignment>::Slot(EDreamPanelHorizontalAlignment::Right)
				.Icon(FAppStyle::GetBrush(TEXT("HorizontalAlignment_Right"))).ToolTip(LOCTEXT("AlignRight", "Align Right"))
			+ SSegmentedControl<EDreamPanelHorizontalAlignment>::Slot(EDreamPanelHorizontalAlignment::Fill)
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
			SNew(SSegmentedControl<EDreamPanelVerticalAlignment>)
			.Value_Lambda([Handle]()
			{
				uint8 Value = static_cast<uint8>(EDreamPanelVerticalAlignment::Fill);
				Handle->GetValue(Value);
				return static_cast<EDreamPanelVerticalAlignment>(Value);
			})
			.OnValueChanged_Lambda([Handle](EDreamPanelVerticalAlignment Value)
			{
				Handle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<EDreamPanelVerticalAlignment>::Slot(EDreamPanelVerticalAlignment::Top)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Top"))).ToolTip(LOCTEXT("AlignTop", "Align Top"))
			+ SSegmentedControl<EDreamPanelVerticalAlignment>::Slot(EDreamPanelVerticalAlignment::Center)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Center"))).ToolTip(LOCTEXT("AlignVCenter", "Align Vertical Center"))
			+ SSegmentedControl<EDreamPanelVerticalAlignment>::Slot(EDreamPanelVerticalAlignment::Bottom)
				.Icon(FAppStyle::GetBrush(TEXT("VerticalAlignment_Bottom"))).ToolTip(LOCTEXT("AlignBottom", "Align Bottom"))
			+ SSegmentedControl<EDreamPanelVerticalAlignment>::Slot(EDreamPanelVerticalAlignment::Fill)
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
			SNew(SSegmentedControl<EDreamPanelSizeRule>)
			.Value_Lambda([Handle]()
			{
				uint8 Value = static_cast<uint8>(EDreamPanelSizeRule::Auto);
				Handle->GetValue(Value);
				return static_cast<EDreamPanelSizeRule>(Value);
			})
			.OnValueChanged_Lambda([Handle](EDreamPanelSizeRule Value)
			{
				Handle->SetValue(static_cast<uint8>(Value));
			})
			+ SSegmentedControl<EDreamPanelSizeRule>::Slot(EDreamPanelSizeRule::Auto).Text(LOCTEXT("AutoSizeRule", "Auto"))
			+ SSegmentedControl<EDreamPanelSizeRule>::Slot(EDreamPanelSizeRule::Fill).Text(LOCTEXT("FillSizeRule", "Fill"))
		];
		return Handle;
	}
}

bool FDreamPanelSlotCustomization::ShouldShowZOrder(const UDreamLayoutContainer* ParentLayout)
{
	using namespace DreamPanelSlotCustomizationLocal;
	const FPanelContext Context = GetPanelContext(ParentLayout);
	// Overlay and GridPanel reorder by it on every pass, and CanvasPanel does when told to. Anywhere
	// else a stray value - copied in with a prefab, or written from Blueprint - would sit unseen and
	// silently undo hierarchy reordering, because the reorder is applied as a sibling-index write.
	return Context.bOverlay || Context.bGrid || Context.bCanvas;
}

void FDreamPanelSlotCustomization::AddSlotProperties(
	IDetailCategoryBuilder& Category,
	const TArray<UObject*>& SlotObjects,
	const UDreamLayoutContainer* ParentLayout)
{
	using namespace DreamPanelSlotCustomizationLocal;
	const FPanelContext Context = GetPanelContext(ParentLayout);
	if (!Context.bPanel || SlotObjects.IsEmpty())
	{
		return;
	}

	if (!Context.bCanvas)
	{
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, Padding));
		CustomizeHorizontalAlignment(Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, HorizontalAlignment)));
		CustomizeVerticalAlignment(Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, VerticalAlignment)));
	}
	if (Context.bLinear)
	{
		const TSharedPtr<IPropertyHandle> SizeRuleHandle = CustomizeSizeRule(Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, SizeRule)));
		if (IDetailPropertyRow* FillWeightRow = Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, FillWeight)))
		{
			FillWeightRow->IsEnabled(TAttribute<bool>::CreateLambda([SizeRuleHandle]()
			{
				uint8 Value = static_cast<uint8>(EDreamPanelSizeRule::Auto);
				return SizeRuleHandle.IsValid()
					&& SizeRuleHandle->GetValue(Value) == FPropertyAccess::Result::Success
					&& static_cast<EDreamPanelSizeRule>(Value) == EDreamPanelSizeRule::Fill;
			}));
		}
	}
	if (Context.bGrid || Context.bUniformGrid)
	{
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, Row));
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, Column));
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, RowSpan));
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, ColumnSpan));
	}
	if (ShouldShowZOrder(ParentLayout))
	{
		if (IDetailPropertyRow* ZOrderRow = Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, ZOrder)))
		{
			if (Context.bGrid)
			{
				ZOrderRow->DisplayName(LOCTEXT("Layer", "Layer"));
			}
		}
	}
	if (Context.bCanvas)
	{
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(UDreamPanelSlot, bAutoSize));
	}
}

#undef LOCTEXT_NAMESPACE
