// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexPanelSlot.h"
#include "Core/Components/LexWidget.h"

namespace LexPanelSlotLocal
{
	constexpr float MaxLayoutValue = 1.0e9f;
	constexpr int32 MaxGridIndex = 4095;
	constexpr int32 MaxGridSpan = 4096;
	constexpr uint8 HorizontalPositionMask = 1 << 0;
	constexpr uint8 VerticalPositionMask = 1 << 1;
	constexpr uint8 HorizontalSizeMask = 1 << 2;
	constexpr uint8 VerticalSizeMask = 1 << 3;
	constexpr uint8 AllGeometryMask = HorizontalPositionMask | VerticalPositionMask | HorizontalSizeMask | VerticalSizeMask;

	float FiniteClamped(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -MaxLayoutValue, MaxLayoutValue) : 0.0f;
	}

	float NonNegative(float Value)
	{
		return FMath::Max(0.0f, FiniteClamped(Value));
	}

	FMargin SanitizeMargin(const FMargin& Value)
	{
		return FMargin(
			FiniteClamped(Value.Left), FiniteClamped(Value.Top),
			FiniteClamped(Value.Right), FiniteClamped(Value.Bottom));
	}
}

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

LEX_SLOT_SETTER(SetPadding, Padding, FMargin, LexPanelSlotLocal::SanitizeMargin(Value))
LEX_SLOT_SETTER(SetHorizontalAlignment, HorizontalAlignment, ELexPanelHorizontalAlignment, Value)
LEX_SLOT_SETTER(SetVerticalAlignment, VerticalAlignment, ELexPanelVerticalAlignment, Value)
LEX_SLOT_SETTER(SetSizeRule, SizeRule, ELexPanelSizeRule, Value)
LEX_SLOT_SETTER(SetFillWeight, FillWeight, float, LexPanelSlotLocal::NonNegative(Value))
LEX_SLOT_SETTER(SetRow, Row, int32, FMath::Clamp(Value, 0, LexPanelSlotLocal::MaxGridIndex))
LEX_SLOT_SETTER(SetColumn, Column, int32, FMath::Clamp(Value, 0, LexPanelSlotLocal::MaxGridIndex))
LEX_SLOT_SETTER(SetRowSpan, RowSpan, int32, FMath::Clamp(Value, 1, LexPanelSlotLocal::MaxGridSpan))
LEX_SLOT_SETTER(SetColumnSpan, ColumnSpan, int32, FMath::Clamp(Value, 1, LexPanelSlotLocal::MaxGridSpan))
LEX_SLOT_SETTER(SetZOrder, ZOrder, int32, Value)
LEX_SLOT_SETTER(SetAutoSize, bAutoSize, bool, Value)

#undef LEX_SLOT_SETTER

void ULexPanelSlot::OnRegister()
{
	Super::OnRegister();
	CaptureAuthoredGeometry();
}

void ULexPanelSlot::CaptureAuthoredGeometry(bool bForce)
{
	if (bHasAuthoredGeometry && !bForce)
	{
		return;
	}
	if (const ULexWidget* Widget = GetWidget(); IsValid(Widget))
	{
		AuthoredAnchorData = Widget->GetAnchorData();
		const FVector2D Size = Widget->GetSize();
		AuthoredDesiredSizeFallback = FVector2f(
			FMath::IsFinite(Size.X) ? FMath::Max(0.0, Size.X) : 0.0,
			FMath::IsFinite(Size.Y) ? FMath::Max(0.0, Size.Y) : 0.0);
		bHasAuthoredGeometry = true;
		bLayoutGeometryApplied = false;
		LayoutGeometryControlMask = 0;
	}
}

bool ULexPanelSlot::RestoreAuthoredGeometry(bool bForce)
{
	if (!bHasAuthoredGeometry || (!bForce && !bLayoutGeometryApplied))
	{
		return false;
	}

	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		bLayoutGeometryApplied = false;
		LayoutGeometryControlMask = 0;
		return false;
	}

	const uint8 ControlMask = LayoutGeometryControlMask != 0
		? LayoutGeometryControlMask : LexPanelSlotLocal::AllGeometryMask;
	const FLexUIAnchorData& Current = Widget->GetAnchorData();
	FLexUIAnchorData Restored = Current;
	if (ControlMask & (LexPanelSlotLocal::HorizontalPositionMask | LexPanelSlotLocal::HorizontalSizeMask))
	{
		Restored.AnchorMin.X = AuthoredAnchorData.AnchorMin.X;
		Restored.AnchorMax.X = AuthoredAnchorData.AnchorMax.X;
	}
	if (ControlMask & (LexPanelSlotLocal::VerticalPositionMask | LexPanelSlotLocal::VerticalSizeMask))
	{
		Restored.AnchorMin.Y = AuthoredAnchorData.AnchorMin.Y;
		Restored.AnchorMax.Y = AuthoredAnchorData.AnchorMax.Y;
	}
	if (ControlMask & LexPanelSlotLocal::HorizontalPositionMask)
	{
		Restored.AnchoredPosition.X = AuthoredAnchorData.AnchoredPosition.X;
	}
	if (ControlMask & LexPanelSlotLocal::VerticalPositionMask)
	{
		Restored.AnchoredPosition.Y = AuthoredAnchorData.AnchoredPosition.Y;
	}
	if (ControlMask & LexPanelSlotLocal::HorizontalSizeMask)
	{
		Restored.SizeDelta.X = AuthoredAnchorData.SizeDelta.X;
	}
	if (ControlMask & LexPanelSlotLocal::VerticalSizeMask)
	{
		Restored.SizeDelta.Y = AuthoredAnchorData.SizeDelta.Y;
	}
	const bool bChanged = !Current.AnchorMin.Equals(Restored.AnchorMin, 0.0)
		|| !Current.AnchorMax.Equals(Restored.AnchorMax, 0.0)
		|| !Current.AnchoredPosition.Equals(Restored.AnchoredPosition, 0.0)
		|| !Current.SizeDelta.Equals(Restored.SizeDelta, 0.0);
	bLayoutGeometryApplied = false;
	LayoutGeometryControlMask = 0;
	if (bChanged)
	{
		Widget->SetAnchorData(Restored);
	}
	return bChanged;
}

void ULexPanelSlot::InvalidateAuthoredGeometry()
{
	bHasAuthoredGeometry = false;
	bLayoutGeometryApplied = false;
	LayoutGeometryControlMask = 0;
	AuthoredAnchorData = FLexUIAnchorData();
	AuthoredDesiredSizeFallback = FVector2f::ZeroVector;
}

void ULexPanelSlot::MarkLayoutGeometryApplied(bool bHorizontalPosition, bool bVerticalPosition,
	bool bHorizontalSize, bool bVerticalSize)
{
	CaptureAuthoredGeometry();
	bLayoutGeometryApplied = bHasAuthoredGeometry;
	LayoutGeometryControlMask = (bHorizontalPosition ? LexPanelSlotLocal::HorizontalPositionMask : 0)
		| (bVerticalPosition ? LexPanelSlotLocal::VerticalPositionMask : 0)
		| (bHorizontalSize ? LexPanelSlotLocal::HorizontalSizeMask : 0)
		| (bVerticalSize ? LexPanelSlotLocal::VerticalSizeMask : 0);
}

void ULexPanelSlot::NotifySlotChanged()
{
	if (ULexWidget* Widget = GetWidget())
	{
		ULexWidget* LayoutWidget = Widget->GetParent() ? Widget->GetParent() : Widget;
		ULexWidget::MarkLayoutForRebuild(LayoutWidget);
#if WITH_EDITOR
		if (const UWorld* World = LayoutWidget->GetWorld(); World && !World->IsGameWorld())
		{
			ULexWidget::ForceRebuildLayoutImmediately(LayoutWidget);
		}
#endif
	}
}

#if WITH_EDITOR
namespace
{
	void SanitizePanelSlot(ULexPanelSlot& Slot)
	{
		Slot.Padding = LexPanelSlotLocal::SanitizeMargin(Slot.Padding);
		Slot.Row = FMath::Clamp(Slot.Row, 0, LexPanelSlotLocal::MaxGridIndex);
		Slot.Column = FMath::Clamp(Slot.Column, 0, LexPanelSlotLocal::MaxGridIndex);
		Slot.RowSpan = FMath::Clamp(Slot.RowSpan, 1, LexPanelSlotLocal::MaxGridSpan);
		Slot.ColumnSpan = FMath::Clamp(Slot.ColumnSpan, 1, LexPanelSlotLocal::MaxGridSpan);
		Slot.FillWeight = LexPanelSlotLocal::NonNegative(Slot.FillWeight);
	}
}

void ULexPanelSlot::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SanitizePanelSlot(*this);
	NotifySlotChanged();
}

void ULexPanelSlot::PostEditUndo()
{
	Super::PostEditUndo();
	SanitizePanelSlot(*this);
	NotifySlotChanged();
}
#endif
