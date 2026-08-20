// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/Components/DreamWidgetPlacement.h"

#include "Core/Components/DreamWidget.h"

void FDreamWidgetPlacement::Capture(const UDreamWidget* InWidget)
{
	Reset();
	if (!::IsValid(InWidget))
	{
		return;
	}
	Parent = InWidget->GetParent();
	SiblingIndex = InWidget->GetSiblingIndex();
	Pivot = InWidget->GetPivot();
	AnchorMin = InWidget->GetAnchorMin();
	AnchorMax = InWidget->GetAnchorMax();
	AnchoredPosition = InWidget->GetAnchoredPosition();
	SizeDelta = InWidget->GetSizeDelta();
	bIgnoreLayout = InWidget->GetIgnoreLayout();

	if (const UDreamPanelSlot* Slot = InWidget->GetPanelSlot())
	{
		bHadSlot = true;
		SlotPadding = Slot->Padding;
		SlotHorizontalAlignment = Slot->HorizontalAlignment;
		SlotVerticalAlignment = Slot->VerticalAlignment;
		SlotSizeRule = Slot->SizeRule;
		SlotFillWeight = Slot->FillWeight;
		SlotRow = Slot->Row;
		SlotColumn = Slot->Column;
		SlotRowSpan = Slot->RowSpan;
		SlotColumnSpan = Slot->ColumnSpan;
		bSlotAutoSize = Slot->bAutoSize;
		SlotZOrder = Slot->ZOrder;
	}
}

bool FDreamWidgetPlacement::Restore(UDreamWidget* InWidget) const
{
	if (!::IsValid(InWidget) || !IsValid())
	{
		return false;
	}
	UDreamWidget* OriginalParent = Parent.Get();
	if (InWidget->GetParent() != OriginalParent)
	{
		if (!InWidget->TrySetParent(OriginalParent, true, SiblingIndex))
		{
			return false;
		}
	}
	else if (SiblingIndex != INDEX_NONE)
	{
		InWidget->SetSiblingIndex(SiblingIndex);
	}

	InWidget->SetPivot(Pivot);
	InWidget->SetAnchorMin(AnchorMin);
	InWidget->SetAnchorMax(AnchorMax);
	InWidget->SetSizeDelta(SizeDelta);
	InWidget->SetAnchoredPosition(AnchoredPosition);
	InWidget->SetIgnoreLayout(bIgnoreLayout);

	if (bHadSlot)
	{
		// The slot here is a FRESH default one -- detaching destroyed the original. Restoring only
		// the parent and the ZOrder, which is the obvious subset, silently resets padding,
		// alignment, fill weight, grid placement and auto-size to defaults.
		if (UDreamPanelSlot* Slot = InWidget->GetPanelSlot())
		{
			Slot->SetPadding(SlotPadding);
			Slot->SetHorizontalAlignment(SlotHorizontalAlignment);
			Slot->SetVerticalAlignment(SlotVerticalAlignment);
			Slot->SetSizeRule(SlotSizeRule);
			Slot->SetFillWeight(SlotFillWeight);
			Slot->SetRow(SlotRow);
			Slot->SetColumn(SlotColumn);
			Slot->SetRowSpan(SlotRowSpan);
			Slot->SetColumnSpan(SlotColumnSpan);
			Slot->SetAutoSize(bSlotAutoSize);
			Slot->SetZOrder(SlotZOrder);
			Slot->CaptureAuthoredGeometry(true);
		}
	}
	return true;
}

bool FDreamWidgetPlacement::IsValid() const
{
	return Parent.IsValid();
}

void FDreamWidgetPlacement::Reset()
{
	*this = FDreamWidgetPlacement();
}
