// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"

#define LEX_SLOT_SETTER(MethodName, FieldName, Type, Transform) \
	void ULexPanelSlot::MethodName(Type Value) \
	{ \
		Value = Transform; \
		if (FieldName != Value) \
		{ \
			FieldName = Value; \
			NotifySlotChanged(); \
		} \
	}

LEX_SLOT_SETTER(SetPadding, Padding, FMargin, Value)
LEX_SLOT_SETTER(SetHorizontalAlignment, HorizontalAlignment, ELexPanelHorizontalAlignment, Value)
LEX_SLOT_SETTER(SetVerticalAlignment, VerticalAlignment, ELexPanelVerticalAlignment, Value)
LEX_SLOT_SETTER(SetSizeRule, SizeRule, ELexPanelSizeRule, Value)
LEX_SLOT_SETTER(SetFillWeight, FillWeight, float, FMath::Max(0.0f, Value))
LEX_SLOT_SETTER(SetRow, Row, int32, FMath::Max(0, Value))
LEX_SLOT_SETTER(SetColumn, Column, int32, FMath::Max(0, Value))
LEX_SLOT_SETTER(SetRowSpan, RowSpan, int32, FMath::Max(1, Value))
LEX_SLOT_SETTER(SetColumnSpan, ColumnSpan, int32, FMath::Max(1, Value))
LEX_SLOT_SETTER(SetZOrder, ZOrder, int32, Value)
LEX_SLOT_SETTER(SetAutoSize, bAutoSize, bool, Value)

#undef LEX_SLOT_SETTER

void ULexPanelSlot::NotifySlotChanged()
{
	if (ULexWidget* Widget = GetWidget())
	{
		ULexWidget::MarkLayoutForRebuild(Widget->GetParent() ? Widget->GetParent() : Widget);
	}
}

#if WITH_EDITOR
void ULexPanelSlot::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	Row = FMath::Max(0, Row);
	Column = FMath::Max(0, Column);
	RowSpan = FMath::Max(1, RowSpan);
	ColumnSpan = FMath::Max(1, ColumnSpan);
	FillWeight = FMath::Max(0.0f, FillWeight);
	NotifySlotChanged();
}
#endif
