// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamWidget.h"

namespace DreamPanelSlotLocal
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

// Reason is per property, decided by whether the field is read by any panel's MeasureLayout.
// Padding is added by every one of them; the grid coordinates size GridPanel's and
// UniformGridPanel's tracks; bAutoSize is what CanvasPanel's measures with. Alignment, size rule,
// fill weight and z-order appear in no MeasureLayout at all - only in ApplyChildRect and the
// arrange loops - so they cannot move a preferred size and the invalidation stops at the parent.
#define LEX_SLOT_SETTER(MethodName, FieldName, Type, Transform, Reason) \
	void UDreamPanelSlot::MethodName(Type Value) \
	{ \
		Value = Transform; \
		if (FieldName != Value) \
		{ \
			FieldName = Value; \
			NotifySlotChanged(EDreamLayoutInvalidation::Reason); \
		} \
	}

LEX_SLOT_SETTER(SetPadding, Padding, FMargin, DreamPanelSlotLocal::SanitizeMargin(Value), Measure)
LEX_SLOT_SETTER(SetHorizontalAlignment, HorizontalAlignment, EDreamPanelHorizontalAlignment, Value, Arrange)
LEX_SLOT_SETTER(SetVerticalAlignment, VerticalAlignment, EDreamPanelVerticalAlignment, Value, Arrange)
LEX_SLOT_SETTER(SetSizeRule, SizeRule, EDreamPanelSizeRule, Value, Arrange)
LEX_SLOT_SETTER(SetFillWeight, FillWeight, float, DreamPanelSlotLocal::NonNegative(Value), Arrange)
LEX_SLOT_SETTER(SetRow, Row, int32, FMath::Clamp(Value, 0, DreamPanelSlotLocal::MaxGridIndex), Measure)
LEX_SLOT_SETTER(SetColumn, Column, int32, FMath::Clamp(Value, 0, DreamPanelSlotLocal::MaxGridIndex), Measure)
LEX_SLOT_SETTER(SetRowSpan, RowSpan, int32, FMath::Clamp(Value, 1, DreamPanelSlotLocal::MaxGridSpan), Measure)
LEX_SLOT_SETTER(SetColumnSpan, ColumnSpan, int32, FMath::Clamp(Value, 1, DreamPanelSlotLocal::MaxGridSpan), Measure)
LEX_SLOT_SETTER(SetZOrder, ZOrder, int32, Value, Arrange)
LEX_SLOT_SETTER(SetAutoSize, bAutoSize, bool, Value, Measure)

#undef LEX_SLOT_SETTER

void UDreamPanelSlot::OnRegister()
{
	Super::OnRegister();
	CaptureAuthoredGeometry();
}

void UDreamPanelSlot::CaptureAuthoredGeometry(bool bForce)
{
	if (bHasAuthoredGeometry && !bForce)
	{
		return;
	}
	if (const UDreamWidget* Widget = GetWidget(); IsValid(Widget))
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

bool UDreamPanelSlot::RestoreAuthoredGeometry(bool bForce)
{
	if (!bHasAuthoredGeometry || (!bForce && !bLayoutGeometryApplied))
	{
		return false;
	}

	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		bLayoutGeometryApplied = false;
		LayoutGeometryControlMask = 0;
		return false;
	}

	const FDreamUIAnchorData& Current = Widget->GetAnchorData();
	const FDreamUIAnchorData Restored = ComposeAuthoredAnchorData(Current);
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

FDreamUIAnchorData UDreamPanelSlot::ComposeAuthoredAnchorData(const FDreamUIAnchorData& Current) const
{
	const uint8 ControlMask = LayoutGeometryControlMask != 0
		? LayoutGeometryControlMask : DreamPanelSlotLocal::AllGeometryMask;
	FDreamUIAnchorData Composed = Current;
	// Anchors are part of positioning, not sizing. Canvas AutoSize only owns SizeDelta,
	// so composing its size must not discard anchors authored while AutoSize was active.
	if (ControlMask & DreamPanelSlotLocal::HorizontalPositionMask)
	{
		Composed.AnchorMin.X = AuthoredAnchorData.AnchorMin.X;
		Composed.AnchorMax.X = AuthoredAnchorData.AnchorMax.X;
		Composed.AnchoredPosition.X = AuthoredAnchorData.AnchoredPosition.X;
	}
	if (ControlMask & DreamPanelSlotLocal::VerticalPositionMask)
	{
		Composed.AnchorMin.Y = AuthoredAnchorData.AnchorMin.Y;
		Composed.AnchorMax.Y = AuthoredAnchorData.AnchorMax.Y;
		Composed.AnchoredPosition.Y = AuthoredAnchorData.AnchoredPosition.Y;
	}
	if (ControlMask & DreamPanelSlotLocal::HorizontalSizeMask)
	{
		Composed.SizeDelta.X = AuthoredAnchorData.SizeDelta.X;
	}
	if (ControlMask & DreamPanelSlotLocal::VerticalSizeMask)
	{
		Composed.SizeDelta.Y = AuthoredAnchorData.SizeDelta.Y;
	}
	return Composed;
}

void UDreamPanelSlot::SyncAuthoredDesiredSizeFromWidget()
{
	const UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return;
	}
	const FVector2D Size = Widget->GetSize();
	AuthoredDesiredSizeFallback = FVector2f(
		FMath::IsFinite(Size.X) ? FMath::Max(0.0, Size.X) : 0.0,
		FMath::IsFinite(Size.Y) ? FMath::Max(0.0, Size.Y) : 0.0);
	bHasAuthoredGeometry = true;
}

void UDreamPanelSlot::InvalidateAuthoredGeometry()
{
	bHasAuthoredGeometry = false;
	bLayoutGeometryApplied = false;
	LayoutGeometryControlMask = 0;
	AuthoredAnchorData = FDreamUIAnchorData();
	AuthoredDesiredSizeFallback = FVector2f::ZeroVector;
}

void UDreamPanelSlot::MarkLayoutGeometryApplied(bool bHorizontalPosition, bool bVerticalPosition,
	bool bHorizontalSize, bool bVerticalSize)
{
	CaptureAuthoredGeometry();
	bLayoutGeometryApplied = bHasAuthoredGeometry;
	LayoutGeometryControlMask = (bHorizontalPosition ? DreamPanelSlotLocal::HorizontalPositionMask : 0)
		| (bVerticalPosition ? DreamPanelSlotLocal::VerticalPositionMask : 0)
		| (bHorizontalSize ? DreamPanelSlotLocal::HorizontalSizeMask : 0)
		| (bVerticalSize ? DreamPanelSlotLocal::VerticalSizeMask : 0);
}

void UDreamPanelSlot::NotifySlotChanged(EDreamLayoutInvalidation Reason)
{
	if (UDreamWidget* Widget = GetWidget())
	{
		UDreamWidget* LayoutWidget = Widget->GetParent() ? Widget->GetParent() : Widget;
		// Arrange is stated about the widget whose placement changed, and the walk then dirties its
		// parent - the same widget this used to reach for directly, just arrived at by saying why.
		UDreamWidget::MarkLayoutForRebuild(
			Reason == EDreamLayoutInvalidation::Arrange ? Widget : LayoutWidget, Reason);
#if WITH_EDITOR
		if (const UWorld* World = LayoutWidget->GetWorld(); World && !World->IsGameWorld())
		{
			UDreamWidget::RebuildLayoutImmediately(LayoutWidget);
		}
#endif
	}
}

#if WITH_EDITOR
void UDreamPanelSlot::SyncAuthoredGeometryAfterUserEdit()
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return;
	}
	Modify();

	FDreamLayoutControlAnchorData LayoutControl;
	if (UDreamLayoutSelf* LayoutSelf = Widget->GetLayoutSelf(); IsValid(LayoutSelf))
	{
		LayoutControl.Or(LayoutSelf->GetLayoutControlAnchor(Widget));
	}
	if (UDreamWidget* Parent = Widget->GetParent(); IsValid(Parent))
	{
		if (UDreamLayoutContainer* ParentLayout = Parent->GetLayoutContainer(); IsValid(ParentLayout))
		{
			LayoutControl.Or(ParentLayout->GetLayoutControlAnchor(Widget));
		}
	}

	const FDreamUIAnchorData& Current = Widget->GetAnchorData();
	const FVector2D CurrentSize = Widget->GetSize();
	if (!bHasAuthoredGeometry)
	{
		AuthoredAnchorData = Current;
		AuthoredDesiredSizeFallback = FVector2f(
			FMath::IsFinite(CurrentSize.X) ? FMath::Max(0.0, CurrentSize.X) : 0.0,
			FMath::IsFinite(CurrentSize.Y) ? FMath::Max(0.0, CurrentSize.Y) : 0.0);
		bHasAuthoredGeometry = true;
	}

	AuthoredAnchorData.Pivot = Current.Pivot;
	if (!LayoutControl.bCanControlHorizontalPosition)
	{
		AuthoredAnchorData.AnchorMin.X = Current.AnchorMin.X;
		AuthoredAnchorData.AnchorMax.X = Current.AnchorMax.X;
		AuthoredAnchorData.AnchoredPosition.X = Current.AnchoredPosition.X;
	}
	if (!LayoutControl.bCanControlVerticalPosition)
	{
		AuthoredAnchorData.AnchorMin.Y = Current.AnchorMin.Y;
		AuthoredAnchorData.AnchorMax.Y = Current.AnchorMax.Y;
		AuthoredAnchorData.AnchoredPosition.Y = Current.AnchoredPosition.Y;
	}
	if (!LayoutControl.bCanControlHorizontalSize)
	{
		AuthoredAnchorData.SizeDelta.X = Current.SizeDelta.X;
		AuthoredDesiredSizeFallback.X = FMath::IsFinite(CurrentSize.X) ? FMath::Max(0.0, CurrentSize.X) : 0.0;
	}
	if (!LayoutControl.bCanControlVerticalSize)
	{
		AuthoredAnchorData.SizeDelta.Y = Current.SizeDelta.Y;
		AuthoredDesiredSizeFallback.Y = FMath::IsFinite(CurrentSize.Y) ? FMath::Max(0.0, CurrentSize.Y) : 0.0;
	}

	LayoutGeometryControlMask = (LayoutControl.bCanControlHorizontalPosition ? DreamPanelSlotLocal::HorizontalPositionMask : 0)
		| (LayoutControl.bCanControlVerticalPosition ? DreamPanelSlotLocal::VerticalPositionMask : 0)
		| (LayoutControl.bCanControlHorizontalSize ? DreamPanelSlotLocal::HorizontalSizeMask : 0)
		| (LayoutControl.bCanControlVerticalSize ? DreamPanelSlotLocal::VerticalSizeMask : 0);
	bLayoutGeometryApplied = LayoutGeometryControlMask != 0;
}

namespace
{
	void SanitizePanelSlot(UDreamPanelSlot& Slot)
	{
		Slot.Padding = DreamPanelSlotLocal::SanitizeMargin(Slot.Padding);
		Slot.Row = FMath::Clamp(Slot.Row, 0, DreamPanelSlotLocal::MaxGridIndex);
		Slot.Column = FMath::Clamp(Slot.Column, 0, DreamPanelSlotLocal::MaxGridIndex);
		Slot.RowSpan = FMath::Clamp(Slot.RowSpan, 1, DreamPanelSlotLocal::MaxGridSpan);
		Slot.ColumnSpan = FMath::Clamp(Slot.ColumnSpan, 1, DreamPanelSlotLocal::MaxGridSpan);
		Slot.FillWeight = DreamPanelSlotLocal::NonNegative(Slot.FillWeight);
	}
}

void UDreamPanelSlot::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SanitizePanelSlot(*this);
	// A property edit can be any of them, including padding, so take the safe reason.
	NotifySlotChanged(EDreamLayoutInvalidation::Measure);
}

void UDreamPanelSlot::PostEditUndo()
{
	Super::PostEditUndo();
	SanitizePanelSlot(*this);
	NotifySlotChanged(EDreamLayoutInvalidation::Measure);
}
#endif
