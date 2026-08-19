// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

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
		bool bOverlay = false;
		bool bLinear = false;
		bool bGrid = false;
		bool bUniformGrid = false;
	};

	FPanelContext GetPanelContext(const ULexLayoutContainer* ParentLayout)
	{
		FPanelContext Result;
		Result.bPanel = ParentLayout && ParentLayout->IsA<ULexPanelLayoutBase>();
		Result.bCanvas = ParentLayout && ParentLayout->IsA<ULexLayoutContainerCanvasPanel>();
		Result.bOverlay = ParentLayout && ParentLayout->IsA<ULexLayoutContainerOverlay>();
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

bool FLexPanelSlotCustomization::ShouldShowZOrder(const ULexLayoutContainer* ParentLayout)
{
	using namespace LexPanelSlotCustomizationLocal;
	const FPanelContext Context = GetPanelContext(ParentLayout);
	// Overlay and GridPanel reorder by it on every pass, and CanvasPanel does when told to. Anywhere
	// else a stray value - copied in with a prefab, or written from Blueprint - would sit unseen and
	// silently undo hierarchy reordering, because the reorder is applied as a sibling-index write.
	return Context.bOverlay || Context.bGrid || Context.bCanvas;
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
	if (ShouldShowZOrder(ParentLayout))
	{
		if (IDetailPropertyRow* ZOrderRow = Category.AddExternalObjectProperty(
			SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, ZOrder)))
		{
			if (Context.bGrid)
			{
				ZOrderRow->DisplayName(LOCTEXT("Layer", "Layer"));
			}
		}
	}
	if (Context.bCanvas)
	{
		Category.AddExternalObjectProperty(SlotObjects, GET_MEMBER_NAME_CHECKED(ULexPanelSlot, bAutoSize));
	}
}

#undef LOCTEXT_NAMESPACE
