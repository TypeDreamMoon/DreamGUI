// Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexPanelLayouts.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexVisual.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/CoreDelegates.h"
#include "Widgets/Layout/SSafeZone.h"

namespace LexPanelLayoutLocal
{
	constexpr float MaxLayoutValue = 1.0e9f;
	constexpr int32 MaxGridTrackCount = 8192;

	static float FiniteOrZero(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, -MaxLayoutValue, MaxLayoutValue) : 0.0f;
	}

	static float NonNegative(float Value)
	{
		return FMath::Max(0.0f, FiniteOrZero(Value));
	}

	static FVector2D CleanSize(const FVector2D& Value)
	{
		return FVector2D(NonNegative(Value.X), NonNegative(Value.Y));
	}

	static FMargin CleanMargin(const FMargin& Value)
	{
		return FMargin(
			FiniteOrZero(Value.Left), FiniteOrZero(Value.Top),
			FiniteOrZero(Value.Right), FiniteOrZero(Value.Bottom));
	}

	static FMargin CleanNonNegativeMargin(const FMargin& Value)
	{
		return FMargin(
			NonNegative(Value.Left), NonNegative(Value.Top),
			NonNegative(Value.Right), NonNegative(Value.Bottom));
	}

	static FVector2D CleanSpacing(const FVector2D& Value)
	{
		return FVector2D(NonNegative(Value.X), NonNegative(Value.Y));
	}

	static ULexWidget* GetFirstValidChild(const ULexWidget* Panel)
	{
		if (!IsValid(Panel))
		{
			return nullptr;
		}
		for (ULexWidget* Child : Panel->GetChildren())
		{
			if (IsValid(Child))
			{
				return Child;
			}
		}
		return nullptr;
	}

	static TArray<float> CleanFill(const TArray<float>& Value)
	{
		TArray<float> Result;
		const int32 Count = FMath::Min(Value.Num(), MaxGridTrackCount);
		Result.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Result.Add(NonNegative(Value[Index]));
		}
		return Result;
	}

	static int32 GridIndex(int32 Value)
	{
		return FMath::Clamp(Value, 0, MaxGridTrackCount - 1);
	}

	static int32 GridSpan(int32 Value)
	{
		return FMath::Clamp(Value, 1, MaxGridTrackCount);
	}

	static int32 GridTrackEnd(int32 Index, int32 Span)
	{
		return FMath::Min(MaxGridTrackCount, GridIndex(Index) + GridSpan(Span));
	}

	static FMargin CleanNormalizedSafePadding(const FMargin& Value)
	{
		FMargin Result(
			FMath::Clamp(FiniteOrZero(Value.Left), 0.0f, 0.499f),
			FMath::Clamp(FiniteOrZero(Value.Top), 0.0f, 0.499f),
			FMath::Clamp(FiniteOrZero(Value.Right), 0.0f, 0.499f),
			FMath::Clamp(FiniteOrZero(Value.Bottom), 0.0f, 0.499f));
		auto LimitPair = [](float& First, float& Second)
		{
			constexpr float MaxCombined = 0.998f;
			const float Combined = First + Second;
			if (Combined > MaxCombined)
			{
				const float Scale = MaxCombined / Combined;
				First *= Scale;
				Second *= Scale;
			}
		};
		LimitPair(Result.Left, Result.Right);
		LimitPair(Result.Top, Result.Bottom);
		return Result;
	}

	static FMargin GetPlatformSafePadding(bool bUsePlatformSafeZone, bool bPadLeft, bool bPadTop,
		bool bPadRight, bool bPadBottom, const FVector2D& OverrideSize)
	{
		if (!bUsePlatformSafeZone || !FSlateApplication::IsInitialized())
		{
			return FMargin();
		}

		FMargin Result;
		FSlateApplication::Get().GetSafeZoneSize(Result, OverrideSize);
		if (const TOptional<float> GlobalSafeZoneScale = SSafeZone::GetGlobalSafeZoneScale(); GlobalSafeZoneScale.IsSet())
		{
			Result = Result * NonNegative(GlobalSafeZoneScale.GetValue());
		}
		if (!bPadLeft) Result.Left = 0.0f;
		if (!bPadTop) Result.Top = 0.0f;
		if (!bPadRight) Result.Right = 0.0f;
		if (!bPadBottom) Result.Bottom = 0.0f;
		return CleanNonNegativeMargin(Result);
	}

	static ELexPanelOrientation CleanOrientation(ELexPanelOrientation Value)
	{
		return Value == ELexPanelOrientation::Horizontal || Value == ELexPanelOrientation::Vertical
			? Value
			: ELexPanelOrientation::Vertical;
	}

	static ELexScaleBoxStretch CleanStretch(ELexScaleBoxStretch Value)
	{
		switch (Value)
		{
		case ELexScaleBoxStretch::None:
		case ELexScaleBoxStretch::Fill:
		case ELexScaleBoxStretch::ScaleToFit:
		case ELexScaleBoxStretch::ScaleToFill:
		case ELexScaleBoxStretch::ScaleToFitX:
		case ELexScaleBoxStretch::ScaleToFitY:
		case ELexScaleBoxStretch::UserSpecified:
			return Value;
		default:
			return ELexScaleBoxStretch::ScaleToFit;
		}
	}

	static float HorizontalPadding(const FMargin& Padding)
	{
		return FiniteOrZero(Padding.Left) + FiniteOrZero(Padding.Right);
	}

	static float VerticalPadding(const FMargin& Padding)
	{
		return FiniteOrZero(Padding.Top) + FiniteOrZero(Padding.Bottom);
	}

	static float Sum(const TArray<float>& Values, int32 Start = 0, int32 Count = MAX_int32)
	{
		float Result = 0.0f;
		const int32 End = FMath::Min(Values.Num(), Start + FMath::Max(0, Count));
		for (int32 Index = FMath::Max(0, Start); Index < End; ++Index)
		{
			Result += Values[Index];
		}
		return Result;
	}

	static void AddSpanRequirement(TArray<float>& Tracks, int32 Start, int32 Span, float RequiredSize, float Spacing)
	{
		if (Tracks.IsEmpty())
		{
			return;
		}
		Start = FMath::Clamp(Start, 0, Tracks.Num() - 1);
		Span = FMath::Clamp(Span, 1, Tracks.Num() - Start);
		const float CurrentSize = Sum(Tracks, Start, Span) + NonNegative(Spacing) * (Span - 1);
		const float MissingSize = NonNegative(RequiredSize) - CurrentSize;
		if (MissingSize > 0.0f)
		{
			const float PerTrack = MissingSize / Span;
			for (int32 Index = Start; Index < Start + Span; ++Index)
			{
				Tracks[Index] += PerTrack;
			}
		}
	}

	static TArray<float> ArrangeTracks(const TArray<float>& Desired, const TArray<float>& ConfiguredFill, float AvailableSize)
	{
		TArray<float> Result = Desired;
		float FixedSize = 0.0f;
		float FillTotal = 0.0f;
		for (int32 Index = 0; Index < Desired.Num(); ++Index)
		{
			const float Fill = ConfiguredFill.IsValidIndex(Index) ? NonNegative(ConfiguredFill[Index]) : 0.0f;
			if (Fill > UE_SMALL_NUMBER)
			{
				FillTotal += Fill;
			}
			else
			{
				FixedSize += Desired[Index];
			}
		}

		const float FillSpace = FMath::Max(0.0f, NonNegative(AvailableSize) - FixedSize);
		if (FillTotal > UE_SMALL_NUMBER)
		{
			for (int32 Index = 0; Index < Result.Num(); ++Index)
			{
				const float Fill = ConfiguredFill.IsValidIndex(Index) ? NonNegative(ConfiguredFill[Index]) : 0.0f;
				if (Fill > UE_SMALL_NUMBER)
				{
					Result[Index] = FillSpace * Fill / FillTotal;
				}
			}
		}
		return Result;
	}

	static void ApplyStableZOrderWithinParticipatingSlots(ULexWidget* Panel, TArray<ULexWidget*>& ParticipatingChildren)
	{
		if (!IsValid(Panel) || ParticipatingChildren.Num() < 2)
		{
			return;
		}

		ParticipatingChildren.StableSort([](const ULexWidget& A, const ULexWidget& B)
		{
			const ULexPanelSlot* ASlot = A.GetPanelSlot();
			const ULexPanelSlot* BSlot = B.GetPanelSlot();
			return IsValid(ASlot) && IsValid(BSlot) && ASlot->ZOrder < BSlot->ZOrder;
		});

		TSet<const ULexWidget*> ParticipatingSet;
		for (const ULexWidget* Child : ParticipatingChildren)
		{
			ParticipatingSet.Add(Child);
		}
		TArray<ULexWidget*> DesiredOrder = Panel->GetChildren();
		int32 SortedIndex = 0;
		for (ULexWidget*& Child : DesiredOrder)
		{
			if (ParticipatingSet.Contains(Child))
			{
				Child = ParticipatingChildren[SortedIndex++];
			}
		}
		for (int32 Index = 0; Index < DesiredOrder.Num(); ++Index)
		{
			if (IsValid(DesiredOrder[Index]) && DesiredOrder[Index]->GetSiblingIndex() != Index)
			{
				DesiredOrder[Index]->SetSiblingIndex(Index);
			}
		}
	}
}

#define LEX_PANEL_SETTER(ClassName, MethodName, FieldName, Type, CleanExpression) \
	void ClassName::MethodName(Type Value) \
	{ \
		Value = CleanExpression; \
		if (FieldName != Value) \
		{ \
			FieldName = Value; \
			RequestLayoutRefresh(); \
		} \
	}

LEX_PANEL_SETTER(ULexLayoutContainerCanvasPanel, SetSortChildrenByZOrder, bSortChildrenByZOrder, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerOverlay, SetPadding, Padding, FMargin, LexPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(ULexLayoutContainerStackBox, SetOrientation, Orientation, ELexPanelOrientation, LexPanelLayoutLocal::CleanOrientation(Value))
LEX_PANEL_SETTER(ULexLayoutContainerStackBox, SetPadding, Padding, FMargin, LexPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(ULexLayoutContainerStackBox, SetSpacing, Spacing, float, LexPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(ULexLayoutContainerWrapBox, SetPadding, Padding, FMargin, LexPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(ULexLayoutContainerWrapBox, SetSpacing, Spacing, FVector2D, LexPanelLayoutLocal::CleanSpacing(Value))
LEX_PANEL_SETTER(ULexLayoutContainerWrapBox, SetWrapSize, WrapSize, float, LexPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(ULexLayoutContainerWrapBox, SetExplicitWrapSize, bExplicitWrapSize, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerGridPanel, SetPadding, Padding, FMargin, LexPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(ULexLayoutContainerGridPanel, SetSpacing, Spacing, FVector2D, LexPanelLayoutLocal::CleanSpacing(Value))
LEX_PANEL_SETTER(ULexLayoutContainerUniformGridPanel, SetPadding, Padding, FMargin, LexPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(ULexLayoutContainerUniformGridPanel, SetSpacing, Spacing, FVector2D, LexPanelLayoutLocal::CleanSpacing(Value))
LEX_PANEL_SETTER(ULexLayoutContainerUniformGridPanel, SetMinDesiredSlotWidth, MinDesiredSlotWidth, float, LexPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(ULexLayoutContainerUniformGridPanel, SetMinDesiredSlotHeight, MinDesiredSlotHeight, float, LexPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(ULexLayoutContainerSizeBox, SetPadding, Padding, FMargin, LexPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(ULexLayoutContainerSizeBox, SetOverrideWidth, bOverrideWidth, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerSizeBox, SetWidthOverride, WidthOverride, float, LexPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(ULexLayoutContainerSizeBox, SetOverrideHeight, bOverrideHeight, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerSizeBox, SetHeightOverride, HeightOverride, float, LexPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(ULexLayoutContainerScaleBox, SetPadding, Padding, FMargin, LexPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(ULexLayoutContainerScaleBox, SetUserSpecifiedScale, UserSpecifiedScale, float, LexPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(ULexLayoutContainerScaleBox, SetIgnoreInheritedScale, bIgnoreInheritedScale, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerSafeZone, SetUsePlatformSafeZone, bUsePlatformSafeZone, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerSafeZone, SetPadLeft, bPadLeft, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerSafeZone, SetPadTop, bPadTop, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerSafeZone, SetPadRight, bPadRight, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerSafeZone, SetPadBottom, bPadBottom, bool, Value)
LEX_PANEL_SETTER(ULexLayoutContainerSafeZone, SetSafePadding, SafePadding, FMargin, LexPanelLayoutLocal::CleanNonNegativeMargin(Value))
LEX_PANEL_SETTER(ULexLayoutContainerSafeZone, SetNormalizedSafePadding, NormalizedSafePadding, FMargin, LexPanelLayoutLocal::CleanNormalizedSafePadding(Value))
LEX_PANEL_SETTER(ULexLayoutContainerWidgetSwitcher, SetPadding, Padding, FMargin, LexPanelLayoutLocal::CleanMargin(Value))

#undef LEX_PANEL_SETTER

void ULexLayoutContainerScaleBox::SetStretch(ELexScaleBoxStretch Value)
{
	Value = LexPanelLayoutLocal::CleanStretch(Value);
	const bool bStretchChanged = Stretch != Value;
	Stretch = Value;
	UpdateClippingOverride();
	if (bStretchChanged)
	{
		RequestLayoutRefresh();
	}
}

void ULexLayoutContainerGridPanel::SetColumnFill(const TArray<float>& Value)
{
	TArray<float> Cleaned = LexPanelLayoutLocal::CleanFill(Value);
	if (ColumnFill != Cleaned)
	{
		ColumnFill = MoveTemp(Cleaned);
		RequestLayoutRefresh();
	}
}

void ULexLayoutContainerGridPanel::SetRowFill(const TArray<float>& Value)
{
	TArray<float> Cleaned = LexPanelLayoutLocal::CleanFill(Value);
	if (RowFill != Cleaned)
	{
		RowFill = MoveTemp(Cleaned);
		RequestLayoutRefresh();
	}
}

void ULexLayoutContainerSizeBox::SetMinDesiredSize(FVector2D Value)
{
	Value = LexPanelLayoutLocal::CleanSize(Value);
	bool bChanged = !MinDesiredSize.Equals(Value, 0.0);
	MinDesiredSize = Value;
	FVector2D ReconciledMax = LexPanelLayoutLocal::CleanSize(MaxDesiredSize);
	if (ReconciledMax.X > 0.0) ReconciledMax.X = FMath::Max(ReconciledMax.X, MinDesiredSize.X);
	if (ReconciledMax.Y > 0.0) ReconciledMax.Y = FMath::Max(ReconciledMax.Y, MinDesiredSize.Y);
	bChanged |= !MaxDesiredSize.Equals(ReconciledMax, 0.0);
	MaxDesiredSize = ReconciledMax;
	if (bChanged) RequestLayoutRefresh();
}

void ULexLayoutContainerSizeBox::SetMaxDesiredSize(FVector2D Value)
{
	Value = LexPanelLayoutLocal::CleanSize(Value);
	if (Value.X > 0.0) Value.X = FMath::Max(Value.X, LexPanelLayoutLocal::NonNegative(MinDesiredSize.X));
	if (Value.Y > 0.0) Value.Y = FMath::Max(Value.Y, LexPanelLayoutLocal::NonNegative(MinDesiredSize.Y));
	if (!MaxDesiredSize.Equals(Value, 0.0))
	{
		MaxDesiredSize = Value;
		RequestLayoutRefresh();
	}
}

ULexPanelSlot* ULexPanelLayoutBase::EnsureSlot(ULexWidget* Child) const
{
	if (!IsValid(Child))
	{
		return nullptr;
	}
	if (ULexPanelSlot* ExistingSlot = Child->GetPanelSlot(); IsValid(ExistingSlot))
	{
		ExistingSlot->CaptureAuthoredGeometry();
		return ExistingSlot;
	}
	ULexPanelSlot* NewSlot = Child->CreateNewPanelSlot<ULexPanelSlot>();
	if (IsValid(NewSlot))
	{
		if (IsA<ULexLayoutContainerScaleBox>())
		{
			NewSlot->SetHorizontalAlignment(ELexPanelHorizontalAlignment::Center);
			NewSlot->SetVerticalAlignment(ELexPanelVerticalAlignment::Center);
		}
		NewSlot->CaptureAuthoredGeometry();
	}
	return NewSlot;
}

const ULexPanelSlot* ULexPanelLayoutBase::GetSlot(const ULexWidget* Child) const
{
	if (IsValid(Child))
	{
		if (const ULexPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
		{
			return Slot;
		}
	}
	return GetDefault<ULexPanelSlot>();
}

/**
 * Hand a skipped child back to its authored anchors — but only when it explicitly opted out of layout.
 *
 * bIgnoreLayout means "I position myself", so restoring the authored geometry is what the author asked for.
 * A child that is merely collapsed right now is still owned by this panel and will be laid out again the moment
 * it becomes visible, so its geometry must be left untouched. The authored snapshot is captured at design time
 * (in the prefab editor, at whatever root size that scene had) and can encode a position that is far outside the
 * runtime canvas — restoring it there teleports the whole subtree off-screen, and because the panel skips the
 * child again on the next pass it never comes back.
 */
static void ReleaseSkippedChildGeometry(ULexWidget* Child)
{
	if (!IsValid(Child) || !Child->GetIgnoreLayout())
	{
		return;
	}
	if (ULexPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
	{
		Slot->RestoreAuthoredGeometry();
	}
}

TArray<ULexWidget*> ULexPanelLayoutBase::CollectLayoutChildren(bool bEnsureSlots) const
{
	TArray<ULexWidget*> Result;
	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return Result;
	}
	for (ULexWidget* Child : Widget->GetChildren())
	{
		if (!IsValid(Child))
		{
			continue;
		}
		ULexPanelSlot* ExistingSlot = Child->GetPanelSlot();
		const bool bIgnored = Child->GetIgnoreLayout();
		if (!Child->GetLayoutVisibleInHierarchy() || bIgnored)
		{
			// Only re-snapshot or restore for a child that opted out of layout; see ReleaseSkippedChildGeometry.
			// A collapsed child keeps whatever this panel last gave it, and is re-laid-out when it reappears.
			if (bIgnored && bEnsureSlots && IsValid(ExistingSlot))
			{
				if (ExistingSlot->HasLayoutGeometryApplied())
				{
					ExistingSlot->RestoreAuthoredGeometry();
				}
				else
				{
					ExistingSlot->CaptureAuthoredGeometry(true);
				}
			}
			continue;
		}
		if (bEnsureSlots && !IsValid(EnsureSlot(Child)))
		{
			continue;
		}
		Result.Add(Child);
	}
	return Result;
}

FVector2D ULexPanelLayoutBase::GetDesiredSize(ULexWidget* Child) const
{
	TFunction<FVector2D(ULexWidget*, TSet<const ULexWidget*>&)> GetIntrinsicSize;
	GetIntrinsicSize = [&GetIntrinsicSize](ULexWidget* Widget, TSet<const ULexWidget*>& Visited) -> FVector2D
	{
		if (!IsValid(Widget) || Visited.Contains(Widget))
		{
			return FVector2D::ZeroVector;
		}
		Visited.Add(Widget);

		FVector2D Desired(-1.0, -1.0);
		bool bWidthOverridden = false;
		bool bHeightOverridden = false;
		auto SetOverride = [](double& Target, bool& bOverridden, float Value)
		{
			if (FMath::IsFinite(Value) && Value >= 0.0f)
			{
				Target = Value;
				bOverridden = true;
			}
		};
		auto Accumulate = [](double& Target, bool bOverridden, float Value)
		{
			if (!bOverridden && FMath::IsFinite(Value) && Value >= 0.0f)
			{
				Target = FMath::Max(Target, static_cast<double>(Value));
			}
		};

		if (ULexLayoutSelf* LayoutSelf = Widget->GetLayoutSelf(); IsValid(LayoutSelf)
			&& LayoutSelf->GetClass() != ULexLayoutSelf::StaticClass())
		{
			const FLexLayoutControlAnchorData LayoutControl = LayoutSelf->GetLayoutControlAnchor(Widget);
			const FVector2f LayoutDesired = LayoutSelf->GetLayoutPreferredSize();
			if (LayoutControl.bCanControlHorizontalSize) SetOverride(Desired.X, bWidthOverridden, LayoutDesired.X);
			if (LayoutControl.bCanControlVerticalSize) SetOverride(Desired.Y, bHeightOverridden, LayoutDesired.Y);
		}
		const bool bHasLayoutContainer = IsValid(Widget->GetLayoutContainer());
		if (ULexLayoutContainer* LayoutContainer = Widget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			const FVector2f LayoutDesired = LayoutContainer->GetLayoutPreferredSize();
			if (const ULexLayoutContainerSizeBox* SizeBox = Cast<ULexLayoutContainerSizeBox>(LayoutContainer))
			{
				if (SizeBox->bOverrideWidth) SetOverride(Desired.X, bWidthOverridden, LayoutDesired.X);
				else if (LayoutDesired.X > 0.0f) Accumulate(Desired.X, bWidthOverridden, LayoutDesired.X);
				if (SizeBox->bOverrideHeight) SetOverride(Desired.Y, bHeightOverridden, LayoutDesired.Y);
				else if (LayoutDesired.Y > 0.0f) Accumulate(Desired.Y, bHeightOverridden, LayoutDesired.Y);
			}
			else
			{
				if (LayoutDesired.X > 0.0f) Accumulate(Desired.X, bWidthOverridden, LayoutDesired.X);
				if (LayoutDesired.Y > 0.0f) Accumulate(Desired.Y, bHeightOverridden, LayoutDesired.Y);
			}
		}
		if (ULexVisual* Visual = Widget->GetVisual(); IsValid(Visual))
		{
			Accumulate(Desired.X, bWidthOverridden, Visual->GetPreferredWidth());
			Accumulate(Desired.Y, bHeightOverridden, Visual->GetPreferredHeight());
		}
		if (!bHasLayoutContainer)
		{
			for (ULexWidget* ContentChild : Widget->GetChildren())
			{
				if (IsValid(ContentChild) && ContentChild->GetLayoutVisibleInHierarchy()
					&& !ContentChild->GetIgnoreLayout())
				{
					const FVector2D ContentDesired = GetIntrinsicSize(ContentChild, Visited);
					Accumulate(Desired.X, bWidthOverridden, ContentDesired.X);
					Accumulate(Desired.Y, bHeightOverridden, ContentDesired.Y);
				}
			}
		}

		FVector2f AuthoredFallback = FVector2f::ZeroVector;
		bool bHasAuthoredFallback = false;
		if (const ULexWidget* Parent = Widget->GetParent(); IsValid(Parent)
			&& IsValid(Cast<ULexPanelLayoutBase>(Parent->GetLayoutContainer())))
		{
			if (const ULexPanelSlot* Slot = Widget->GetPanelSlot(); IsValid(Slot) && Slot->HasAuthoredGeometry())
			{
				AuthoredFallback = Slot->GetAuthoredDesiredSizeFallback();
				bHasAuthoredFallback = true;
			}
		}
		if (Desired.X < 0.0) Desired.X = bHasAuthoredFallback ? AuthoredFallback.X : Widget->GetWidth();
		if (Desired.Y < 0.0) Desired.Y = bHasAuthoredFallback ? AuthoredFallback.Y : Widget->GetHeight();
		return LexPanelLayoutLocal::CleanSize(Desired);
	};

	TSet<const ULexWidget*> Visited;
	return GetIntrinsicSize(Child, Visited);
}

void ULexPanelLayoutBase::ApplyChildRect(ULexWidget* Child, const FVector2D& Position, const FVector2D& Size, bool bForceFill) const
{
	ULexWidget* Panel = GetWidget();
	if (!IsValid(Panel) || !IsValid(Child))
	{
		return;
	}
	ULexPanelSlot* Slot = EnsureSlot(Child);
	if (!IsValid(Slot))
	{
		return;
	}
	Slot->MarkLayoutGeometryApplied();

	const FVector2D CleanPosition(
		LexPanelLayoutLocal::FiniteOrZero(Position.X), LexPanelLayoutLocal::FiniteOrZero(Position.Y));
	const FVector2D CleanAreaSize = LexPanelLayoutLocal::CleanSize(Size);
	const FVector2D InnerPosition = CleanPosition + FVector2D(
		LexPanelLayoutLocal::FiniteOrZero(Slot->Padding.Left), LexPanelLayoutLocal::FiniteOrZero(Slot->Padding.Top));
	const FVector2D InnerSize(
		FMath::Max(0.0, CleanAreaSize.X - LexPanelLayoutLocal::HorizontalPadding(Slot->Padding)),
		FMath::Max(0.0, CleanAreaSize.Y - LexPanelLayoutLocal::VerticalPadding(Slot->Padding)));
	const FVector2D Desired = LexPanelLayoutLocal::CleanSize(GetDesiredSize(Child));

	double Width = InnerSize.X;
	double Height = InnerSize.Y;
	double Left = InnerPosition.X;
	double Top = InnerPosition.Y;
	if (!bForceFill && Slot->HorizontalAlignment != ELexPanelHorizontalAlignment::Fill)
	{
		Width = FMath::Min(Desired.X, InnerSize.X);
		switch (Slot->HorizontalAlignment)
		{
		case ELexPanelHorizontalAlignment::Center: Left += (InnerSize.X - Width) * 0.5; break;
		case ELexPanelHorizontalAlignment::Right: Left += InnerSize.X - Width; break;
		default: break;
		}
	}
	if (!bForceFill && Slot->VerticalAlignment != ELexPanelVerticalAlignment::Fill)
	{
		Height = FMath::Min(Desired.Y, InnerSize.Y);
		switch (Slot->VerticalAlignment)
		{
		case ELexPanelVerticalAlignment::Center: Top += (InnerSize.Y - Height) * 0.5; break;
		case ELexPanelVerticalAlignment::Bottom: Top += InnerSize.Y - Height; break;
		default: break;
		}
	}

	const FVector2f FinalSize(static_cast<float>(FMath::Max(0.0, Width)), static_cast<float>(FMath::Max(0.0, Height)));
	Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5), FVector2D(0.5), true, true);
	if (ULexLayoutSelfFlexBox* FlexSelf = Cast<ULexLayoutSelfFlexBox>(Child->GetLayoutSelf()))
	{
		FlexSelf->SetFinalSizeByLayoutContainer(FinalSize);
		Child->SetSizeDelta(FVector2D(FinalSize));
	}
	else
	{
		Child->SetWidth(FinalSize.X);
		Child->SetHeight(FinalSize.Y);
	}
	const FVector2D Pivot = Child->GetPivot();
	const float PanelWidth = LexPanelLayoutLocal::NonNegative(Panel->GetWidth());
	const float PanelHeight = LexPanelLayoutLocal::NonNegative(Panel->GetHeight());
	Child->SetAnchoredPosition(FVector2D(
		-PanelWidth * 0.5 + Left + FinalSize.X * Pivot.X,
		PanelHeight * 0.5 - Top - FinalSize.Y * (1.0 - Pivot.Y)));
}

bool ULexPanelLayoutBase::BeginLayoutPass()
{
	if (!bIsLayoutDirty)
	{
		return false;
	}
	bIsLayoutDirty = false;
	if (!IsValid(GetWidget()))
	{
		PreferredSize = FVector2f::ZeroVector;
		return false;
	}
	return true;
}

FVector2f ULexPanelLayoutBase::MeasureLayout() const
{
	return FVector2f::ZeroVector;
}

FVector2f ULexPanelLayoutBase::GetLayoutPreferredSize()
{
	const FVector2f Result = MeasureLayout();
	return FVector2f(LexPanelLayoutLocal::NonNegative(Result.X), LexPanelLayoutLocal::NonNegative(Result.Y));
}

bool ULexPanelLayoutBase::GetLayoutDebugInfo(const ULexWidget* TargetWidget, FLexLayoutDebugInfo& OutInfo) const
{
	ULexWidget* Panel = GetWidget();
	if (!IsValid(TargetWidget) || !IsValid(Panel))
	{
		return false;
	}
	if (TargetWidget == Panel)
	{
		if (!Super::GetLayoutDebugInfo(TargetWidget, OutInfo))
		{
			return false;
		}
		OutInfo.DesiredSize = FVector2D(MeasureLayout());
		OutInfo.Algorithm = FString::Printf(TEXT("UMG Compatible / %s"), *GetClass()->GetDisplayNameText().ToString());
		OutInfo.SlotRule = TEXT("Panel root");
		return true;
	}
	if (!Panel->GetChildren().Contains(const_cast<ULexWidget*>(TargetWidget)))
	{
		return false;
	}

	const ULexPanelSlot* Slot = GetSlot(TargetWidget);
	OutInfo = FLexLayoutDebugInfo();
	OutInfo.Widget = const_cast<ULexWidget*>(TargetWidget);
	OutInfo.DesiredSize = GetDesiredSize(const_cast<ULexWidget*>(TargetWidget));
	OutInfo.ArrangedPosition = TargetWidget->GetAnchoredPosition();
	OutInfo.ArrangedSize = TargetWidget->GetSize();
	OutInfo.AuthoredSize = Slot->HasAuthoredGeometry()
		? FVector2D(Slot->GetAuthoredDesiredSizeFallback()) : OutInfo.ArrangedSize;
	OutInfo.ContentBounds = Panel->GetSize();
	OutInfo.Algorithm = FString::Printf(TEXT("UMG Compatible / %s"), *GetClass()->GetDisplayNameText().ToString());

	auto EnumDisplayName = [](const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetDisplayNameTextByValue(Value).ToString() : FString(TEXT("Unknown"));
	};
	const FString SizeRule = EnumDisplayName(StaticEnum<ELexPanelSizeRule>(), static_cast<int64>(Slot->SizeRule));
	const FString Horizontal = EnumDisplayName(StaticEnum<ELexPanelHorizontalAlignment>(), static_cast<int64>(Slot->HorizontalAlignment));
	const FString Vertical = EnumDisplayName(StaticEnum<ELexPanelVerticalAlignment>(), static_cast<int64>(Slot->VerticalAlignment));
	OutInfo.SlotRule = FString::Printf(TEXT("%s %.2f | H:%s V:%s | Pad %.0f,%.0f,%.0f,%.0f"),
		*SizeRule, Slot->FillWeight, *Horizontal, *Vertical,
		Slot->Padding.Left, Slot->Padding.Top, Slot->Padding.Right, Slot->Padding.Bottom);

	const FLexLayoutControlAnchorData Control = GetLayoutControlAnchor(TargetWidget);
	auto DescribeAxes = [](bool bX, bool bY, const TCHAR* Controlled, const TCHAR* Authored)
	{
		if (bX && bY) return FString::Printf(TEXT("%s X+Y"), Controlled);
		if (bX) return FString::Printf(TEXT("%s X / %s Y"), Controlled, Authored);
		if (bY) return FString::Printf(TEXT("%s Y / %s X"), Controlled, Authored);
		return FString::Printf(TEXT("%s X+Y"), Authored);
	};
	OutInfo.PositionOwner = DescribeAxes(Control.bCanControlHorizontalPosition, Control.bCanControlVerticalPosition,
		TEXT("Panel"), TEXT("Anchors"));
	OutInfo.SizeOwner = DescribeAxes(Control.bCanControlHorizontalSize, Control.bCanControlVerticalSize,
		TEXT("Panel"), TEXT("Authored"));
	if (const UEnum* ClippingEnum = StaticEnum<ELexWidgetClipping>())
	{
		OutInfo.Clipping = ClippingEnum->GetDisplayNameTextByValue(static_cast<int64>(TargetWidget->GetClipping())).ToString();
	}
	return true;
}

void ULexPanelLayoutBase::OnUnregister()
{
	if (ULexWidget* Panel = GetWidget(); IsValid(Panel))
	{
		for (ULexWidget* Child : Panel->GetChildren())
		{
			if (IsValid(Child))
			{
				if (ULexPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
				{
					Slot->RestoreAuthoredGeometry();
				}
			}
		}
	}
	Super::OnUnregister();
}

FLexLayoutControlAnchorData ULexPanelLayoutBase::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
	FLexLayoutControlAnchorData Result;
	ULexWidget* Panel = GetWidget();
	if (!IsValid(Panel) || !IsValid(TargetWidget) || !Panel->GetChildren().Contains(TargetWidget))
	{
		return Result;
	}
	if (TargetWidget->GetIgnoreLayout())
	{
		return Result;
	}
	Result.bCanControlHorizontalPosition = true;
	Result.bCanControlVerticalPosition = true;
	Result.bCanControlHorizontalSize = true;
	Result.bCanControlVerticalSize = true;
	return Result;
}

void ULexPanelLayoutBase::RequestLayoutRefresh()
{
	ULexWidget::MarkLayoutForRebuild(GetWidget());
}

#if WITH_EDITOR
namespace
{
	void SanitizePanelLayoutProperties(ULexPanelLayoutBase* Layout)
	{
		if (ULexLayoutContainerCanvasPanel* Panel = Cast<ULexLayoutContainerCanvasPanel>(Layout))
		{
			Panel->SetSortChildrenByZOrder(Panel->bSortChildrenByZOrder);
		}
		if (ULexLayoutContainerOverlay* Panel = Cast<ULexLayoutContainerOverlay>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
		}
		if (ULexLayoutContainerStackBox* Panel = Cast<ULexLayoutContainerStackBox>(Layout))
		{
			Panel->SetOrientation(Panel->Orientation);
			Panel->SetPadding(Panel->Padding);
			Panel->SetSpacing(Panel->Spacing);
		}
		if (ULexLayoutContainerWrapBox* Panel = Cast<ULexLayoutContainerWrapBox>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetSpacing(Panel->Spacing);
			Panel->SetWrapSize(Panel->WrapSize);
			Panel->SetExplicitWrapSize(Panel->bExplicitWrapSize);
		}
		if (ULexLayoutContainerGridPanel* Panel = Cast<ULexLayoutContainerGridPanel>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetSpacing(Panel->Spacing);
			Panel->SetColumnFill(Panel->ColumnFill);
			Panel->SetRowFill(Panel->RowFill);
		}
		if (ULexLayoutContainerUniformGridPanel* Panel = Cast<ULexLayoutContainerUniformGridPanel>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetSpacing(Panel->Spacing);
			Panel->SetMinDesiredSlotWidth(Panel->MinDesiredSlotWidth);
			Panel->SetMinDesiredSlotHeight(Panel->MinDesiredSlotHeight);
		}
		if (ULexLayoutContainerSizeBox* Panel = Cast<ULexLayoutContainerSizeBox>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetOverrideWidth(Panel->bOverrideWidth);
			Panel->SetWidthOverride(Panel->WidthOverride);
			Panel->SetOverrideHeight(Panel->bOverrideHeight);
			Panel->SetHeightOverride(Panel->HeightOverride);
			Panel->SetMinDesiredSize(Panel->MinDesiredSize);
			Panel->SetMaxDesiredSize(Panel->MaxDesiredSize);
		}
		if (ULexLayoutContainerScaleBox* Panel = Cast<ULexLayoutContainerScaleBox>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetStretch(Panel->Stretch);
			Panel->SetUserSpecifiedScale(Panel->UserSpecifiedScale);
			Panel->SetIgnoreInheritedScale(Panel->bIgnoreInheritedScale);
		}
		if (ULexLayoutContainerSafeZone* Panel = Cast<ULexLayoutContainerSafeZone>(Layout))
		{
			Panel->SetUsePlatformSafeZone(Panel->bUsePlatformSafeZone);
			Panel->SetPadLeft(Panel->bPadLeft);
			Panel->SetPadTop(Panel->bPadTop);
			Panel->SetPadRight(Panel->bPadRight);
			Panel->SetPadBottom(Panel->bPadBottom);
			Panel->SetSafePadding(Panel->SafePadding);
			Panel->SetNormalizedSafePadding(Panel->NormalizedSafePadding);
		}
		if (ULexLayoutContainerWidgetSwitcher* Panel = Cast<ULexLayoutContainerWidgetSwitcher>(Layout))
		{
			Panel->SetActiveWidgetIndex(Panel->ActiveWidgetIndex);
			Panel->SetPadding(Panel->Padding);
		}
	}
}

void ULexPanelLayoutBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SanitizePanelLayoutProperties(this);
}

void ULexPanelLayoutBase::PostEditUndo()
{
	Super::PostEditUndo();
	SanitizePanelLayoutProperties(this);
	RequestLayoutRefresh();
}
#endif

FVector2f ULexLayoutContainerCanvasPanel::MeasureLayout() const
{
	FVector2f Result = FVector2f::ZeroVector;
	for (ULexWidget* Child : CollectLayoutChildren(false))
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		const FVector2D Size = Slot->bAutoSize
			? GetDesiredSize(Child)
			: (Slot->HasLayoutGeometryApplied() && Slot->HasAuthoredGeometry()
				? FVector2D(Slot->GetAuthoredDesiredSizeFallback())
				: LexPanelLayoutLocal::CleanSize(Child->GetSize()));
		const FVector2D AnchorMin = Child->GetAnchorMin();
		const FVector2D AnchorMax = Child->GetAnchorMax();
		const bool bDockedHorizontally = AnchorMin.X == AnchorMax.X && (AnchorMin.X == 0.0 || AnchorMin.X == 1.0);
		const bool bDockedVertically = AnchorMin.Y == AnchorMax.Y && (AnchorMin.Y == 0.0 || AnchorMin.Y == 1.0);
		Result.X = FMath::Max(Result.X, static_cast<float>(Size.X + (bDockedHorizontally ? FMath::Abs(Child->GetAnchorOffsetLeft()) : 0.0)));
		Result.Y = FMath::Max(Result.Y, static_cast<float>(Size.Y + (bDockedVertically ? FMath::Abs(Child->GetAnchorOffsetTop()) : 0.0)));
	}
	return Result;
}

FLexLayoutControlAnchorData ULexLayoutContainerCanvasPanel::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
	FLexLayoutControlAnchorData Result;
	ULexWidget* Panel = GetWidget();
	if (!IsValid(Panel) || !IsValid(TargetWidget) || !Panel->GetChildren().Contains(TargetWidget))
	{
		return Result;
	}
	if (TargetWidget->GetIgnoreLayout())
	{
		return Result;
	}
	const ULexPanelSlot* Slot = GetSlot(TargetWidget);
	Result.bCanControlHorizontalSize = Slot->bAutoSize;
	Result.bCanControlVerticalSize = Slot->bAutoSize;
	return Result;
}

void ULexLayoutContainerCanvasPanel::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	TArray<ULexWidget*> LayoutChildren = CollectLayoutChildren();
	for (ULexWidget* Child : LayoutChildren)
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		if (Slot->bAutoSize)
		{
			ULexPanelSlot* MutableSlot = Child->GetPanelSlot();
			if (IsValid(MutableSlot))
			{
				MutableSlot->MarkLayoutGeometryApplied(false, false, true, true);
			}
			const FVector2D Desired = GetDesiredSize(Child);
			if (ULexLayoutSelfFlexBox* FlexSelf = Cast<ULexLayoutSelfFlexBox>(Child->GetLayoutSelf()))
			{
				FlexSelf->SetFinalSizeByLayoutContainer(FVector2f(Desired));
				Child->SetSizeDelta(Desired);
			}
			else
			{
				Child->SetWidth(static_cast<float>(Desired.X));
				Child->SetHeight(static_cast<float>(Desired.Y));
			}
		}
		else if (ULexPanelSlot* MutableSlot = Child->GetPanelSlot(); IsValid(MutableSlot))
		{
			if (MutableSlot->HasLayoutGeometryApplied())
			{
				MutableSlot->RestoreAuthoredGeometry();
			}
			else
			{
				MutableSlot->CaptureAuthoredGeometry(true);
			}
		}
	}
	if (bSortChildrenByZOrder)
	{
		LexPanelLayoutLocal::ApplyStableZOrderWithinParticipatingSlots(GetWidget(), LayoutChildren);
	}
	PreferredSize = MeasureLayout();
}

FVector2f ULexLayoutContainerOverlay::MeasureLayout() const
{
	FVector2f Result = FVector2f::ZeroVector;
	for (ULexWidget* Child : CollectLayoutChildren(false))
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = GetSlot(Child);
		Result.X = FMath::Max(Result.X, static_cast<float>(Desired.X + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding)));
		Result.Y = FMath::Max(Result.Y, static_cast<float>(Desired.Y + LexPanelLayoutLocal::VerticalPadding(Slot->Padding)));
	}
	Result.X += LexPanelLayoutLocal::HorizontalPadding(Padding);
	Result.Y += LexPanelLayoutLocal::VerticalPadding(Padding);
	return Result;
}

void ULexLayoutContainerOverlay::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	ULexWidget* Panel = GetWidget();
	const FVector2D AreaPosition(LexPanelLayoutLocal::FiniteOrZero(Padding.Left), LexPanelLayoutLocal::FiniteOrZero(Padding.Top));
	const FVector2D AreaSize(
		FMath::Max(0.0f, Panel->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding)),
		FMath::Max(0.0f, Panel->GetHeight() - LexPanelLayoutLocal::VerticalPadding(Padding)));
	for (ULexWidget* Child : CollectLayoutChildren())
	{
		ApplyChildRect(Child, AreaPosition, AreaSize);
	}
	PreferredSize = MeasureLayout();
}

FVector2f ULexLayoutContainerStackBox::MeasureLayout() const
{
	const TArray<ULexWidget*> LayoutChildren = CollectLayoutChildren(false);
	const bool bHorizontal = Orientation == ELexPanelOrientation::Horizontal;
	const float Gap = LexPanelLayoutLocal::NonNegative(Spacing);
	float Primary = Gap * FMath::Max(0, LayoutChildren.Num() - 1);
	float Secondary = 0.0f;
	for (ULexWidget* Child : LayoutChildren)
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = GetSlot(Child);
		Primary += static_cast<float>(bHorizontal ? Desired.X : Desired.Y)
			+ (bHorizontal ? LexPanelLayoutLocal::HorizontalPadding(Slot->Padding) : LexPanelLayoutLocal::VerticalPadding(Slot->Padding));
		Secondary = FMath::Max(Secondary, static_cast<float>(bHorizontal ? Desired.Y : Desired.X)
			+ (bHorizontal ? LexPanelLayoutLocal::VerticalPadding(Slot->Padding) : LexPanelLayoutLocal::HorizontalPadding(Slot->Padding)));
	}
	return bHorizontal
		? FVector2f(Primary + LexPanelLayoutLocal::HorizontalPadding(Padding), Secondary + LexPanelLayoutLocal::VerticalPadding(Padding))
		: FVector2f(Secondary + LexPanelLayoutLocal::HorizontalPadding(Padding), Primary + LexPanelLayoutLocal::VerticalPadding(Padding));
}

void ULexLayoutContainerStackBox::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	ULexWidget* Panel = GetWidget();
	const TArray<ULexWidget*> LayoutChildren = CollectLayoutChildren();
	const bool bHorizontal = Orientation == ELexPanelOrientation::Horizontal;
	const float Gap = LexPanelLayoutLocal::NonNegative(Spacing);
	const float AvailablePrimary = bHorizontal
		? FMath::Max(0.0f, Panel->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding))
		: FMath::Max(0.0f, Panel->GetHeight() - LexPanelLayoutLocal::VerticalPadding(Padding));
	const float AvailableSecondary = bHorizontal
		? FMath::Max(0.0f, Panel->GetHeight() - LexPanelLayoutLocal::VerticalPadding(Padding))
		: FMath::Max(0.0f, Panel->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding));

	float FixedPrimary = Gap * FMath::Max(0, LayoutChildren.Num() - 1);
	float FillWeight = 0.0f;
	for (ULexWidget* Child : LayoutChildren)
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child);
		const float SlotPadding = bHorizontal
			? LexPanelLayoutLocal::HorizontalPadding(Slot->Padding)
			: LexPanelLayoutLocal::VerticalPadding(Slot->Padding);
		FixedPrimary += SlotPadding;
		if (Slot->SizeRule == ELexPanelSizeRule::Fill)
		{
			FillWeight += LexPanelLayoutLocal::NonNegative(Slot->FillWeight);
		}
		else
		{
			FixedPrimary += static_cast<float>(bHorizontal ? Desired.X : Desired.Y);
		}
	}

	const float FillContentSpace = FMath::Max(0.0f, AvailablePrimary - FixedPrimary);
	float Cursor = bHorizontal ? LexPanelLayoutLocal::FiniteOrZero(Padding.Left) : LexPanelLayoutLocal::FiniteOrZero(Padding.Top);
	for (ULexWidget* Child : LayoutChildren)
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = GetSlot(Child);
		const float PrimaryPadding = bHorizontal
			? LexPanelLayoutLocal::HorizontalPadding(Slot->Padding)
			: LexPanelLayoutLocal::VerticalPadding(Slot->Padding);
		const float ContentPrimary = Slot->SizeRule == ELexPanelSizeRule::Fill
			? (FillWeight > UE_SMALL_NUMBER ? FillContentSpace * LexPanelLayoutLocal::NonNegative(Slot->FillWeight) / FillWeight : 0.0f)
			: static_cast<float>(bHorizontal ? Desired.X : Desired.Y);
		const float SlotPrimary = FMath::Max(0.0f, ContentPrimary + PrimaryPadding);
		if (bHorizontal)
		{
			ApplyChildRect(Child, FVector2D(Cursor, LexPanelLayoutLocal::FiniteOrZero(Padding.Top)), FVector2D(SlotPrimary, AvailableSecondary));
		}
		else
		{
			ApplyChildRect(Child, FVector2D(LexPanelLayoutLocal::FiniteOrZero(Padding.Left), Cursor), FVector2D(AvailableSecondary, SlotPrimary));
		}
		Cursor += SlotPrimary + Gap;
	}
	PreferredSize = MeasureLayout();
}

ULexLayoutContainerHorizontalBox::ULexLayoutContainerHorizontalBox()
{
	Orientation = ELexPanelOrientation::Horizontal;
}

ULexLayoutContainerVerticalBox::ULexLayoutContainerVerticalBox()
{
	Orientation = ELexPanelOrientation::Vertical;
}

FVector2f ULexLayoutContainerWrapBox::MeasureLayout() const
{
	const float GapX = LexPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = LexPanelLayoutLocal::NonNegative(Spacing.Y);
	const float AvailableWidth = bExplicitWrapSize
		? LexPanelLayoutLocal::NonNegative(WrapSize)
		: (IsValid(GetWidget()) ? FMath::Max(0.0f, GetWidget()->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding)) : 0.0f);
	float X = 0.0f;
	float Y = 0.0f;
	float LineHeight = 0.0f;
	float MaxWidth = 0.0f;
	for (ULexWidget* Child : CollectLayoutChildren(false))
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = GetSlot(Child);
		const float ItemWidth = static_cast<float>(Desired.X) + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		const float ItemHeight = static_cast<float>(Desired.Y) + LexPanelLayoutLocal::VerticalPadding(Slot->Padding);
		if (X > 0.0f && X + ItemWidth > AvailableWidth)
		{
			X = 0.0f;
			Y += LineHeight + GapY;
			LineHeight = 0.0f;
		}
		MaxWidth = FMath::Max(MaxWidth, X + ItemWidth);
		X += ItemWidth + GapX;
		LineHeight = FMath::Max(LineHeight, ItemHeight);
	}
	return FVector2f(MaxWidth + LexPanelLayoutLocal::HorizontalPadding(Padding),
		Y + LineHeight + LexPanelLayoutLocal::VerticalPadding(Padding));
}

void ULexLayoutContainerWrapBox::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	struct FWrapItem
	{
		ULexWidget* Widget = nullptr;
		float Width = 0.0f;
		float Height = 0.0f;
	};
	struct FWrapLine
	{
		TArray<FWrapItem> Items;
		float Height = 0.0f;
	};

	const float GapX = LexPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = LexPanelLayoutLocal::NonNegative(Spacing.Y);
	const float AvailableWidth = bExplicitWrapSize
		? LexPanelLayoutLocal::NonNegative(WrapSize)
		: FMath::Max(0.0f, GetWidget()->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding));
	TArray<FWrapLine> Lines;
	FWrapLine CurrentLine;
	float CurrentWidth = 0.0f;
	for (ULexWidget* Child : CollectLayoutChildren())
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const ULexPanelSlot* Slot = GetSlot(Child);
		FWrapItem Item;
		Item.Widget = Child;
		Item.Width = static_cast<float>(Desired.X) + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		Item.Height = static_cast<float>(Desired.Y) + LexPanelLayoutLocal::VerticalPadding(Slot->Padding);
		const float RequiredWidth = CurrentLine.Items.IsEmpty() ? Item.Width : CurrentWidth + GapX + Item.Width;
		if (!CurrentLine.Items.IsEmpty() && RequiredWidth > AvailableWidth)
		{
			Lines.Add(MoveTemp(CurrentLine));
			CurrentLine = FWrapLine();
			CurrentWidth = 0.0f;
		}
		if (!CurrentLine.Items.IsEmpty()) CurrentWidth += GapX;
		CurrentWidth += Item.Width;
		CurrentLine.Height = FMath::Max(CurrentLine.Height, Item.Height);
		CurrentLine.Items.Add(Item);
	}
	if (!CurrentLine.Items.IsEmpty()) Lines.Add(MoveTemp(CurrentLine));

	float Y = LexPanelLayoutLocal::FiniteOrZero(Padding.Top);
	for (const FWrapLine& Line : Lines)
	{
		float X = LexPanelLayoutLocal::FiniteOrZero(Padding.Left);
		for (const FWrapItem& Item : Line.Items)
		{
			ApplyChildRect(Item.Widget, FVector2D(X, Y), FVector2D(Item.Width, Line.Height));
			X += Item.Width + GapX;
		}
		Y += Line.Height + GapY;
	}
	PreferredSize = MeasureLayout();
}

FVector2f ULexLayoutContainerGridPanel::MeasureLayout() const
{
	const TArray<ULexWidget*> LayoutChildren = CollectLayoutChildren(false);
	int32 ColumnCount = FMath::Min(ColumnFill.Num(), LexPanelLayoutLocal::MaxGridTrackCount);
	int32 RowCount = FMath::Min(RowFill.Num(), LexPanelLayoutLocal::MaxGridTrackCount);
	for (ULexWidget* Child : LayoutChildren)
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		ColumnCount = FMath::Max(ColumnCount, LexPanelLayoutLocal::GridTrackEnd(Slot->Column, Slot->ColumnSpan));
		RowCount = FMath::Max(RowCount, LexPanelLayoutLocal::GridTrackEnd(Slot->Row, Slot->RowSpan));
	}
	TArray<float> Columns;
	TArray<float> Rows;
	Columns.Init(0.0f, ColumnCount);
	Rows.Init(0.0f, RowCount);
	for (ULexWidget* Child : LayoutChildren)
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child);
		LexPanelLayoutLocal::AddSpanRequirement(Columns, Slot->Column, Slot->ColumnSpan,
			static_cast<float>(Desired.X) + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding), Spacing.X);
		LexPanelLayoutLocal::AddSpanRequirement(Rows, Slot->Row, Slot->RowSpan,
			static_cast<float>(Desired.Y) + LexPanelLayoutLocal::VerticalPadding(Slot->Padding), Spacing.Y);
	}
	return FVector2f(
		LexPanelLayoutLocal::Sum(Columns) + LexPanelLayoutLocal::NonNegative(Spacing.X) * FMath::Max(0, ColumnCount - 1) + LexPanelLayoutLocal::HorizontalPadding(Padding),
		LexPanelLayoutLocal::Sum(Rows) + LexPanelLayoutLocal::NonNegative(Spacing.Y) * FMath::Max(0, RowCount - 1) + LexPanelLayoutLocal::VerticalPadding(Padding));
}

void ULexLayoutContainerGridPanel::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	TArray<ULexWidget*> LayoutChildren = CollectLayoutChildren();
	int32 ColumnCount = FMath::Min(ColumnFill.Num(), LexPanelLayoutLocal::MaxGridTrackCount);
	int32 RowCount = FMath::Min(RowFill.Num(), LexPanelLayoutLocal::MaxGridTrackCount);
	for (ULexWidget* Child : LayoutChildren)
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		ColumnCount = FMath::Max(ColumnCount, LexPanelLayoutLocal::GridTrackEnd(Slot->Column, Slot->ColumnSpan));
		RowCount = FMath::Max(RowCount, LexPanelLayoutLocal::GridTrackEnd(Slot->Row, Slot->RowSpan));
	}
	TArray<float> DesiredColumns;
	TArray<float> DesiredRows;
	DesiredColumns.Init(0.0f, ColumnCount);
	DesiredRows.Init(0.0f, RowCount);
	for (ULexWidget* Child : LayoutChildren)
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child);
		LexPanelLayoutLocal::AddSpanRequirement(DesiredColumns, Slot->Column, Slot->ColumnSpan,
			static_cast<float>(Desired.X) + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding), Spacing.X);
		LexPanelLayoutLocal::AddSpanRequirement(DesiredRows, Slot->Row, Slot->RowSpan,
			static_cast<float>(Desired.Y) + LexPanelLayoutLocal::VerticalPadding(Slot->Padding), Spacing.Y);
	}

	const float GapX = LexPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = LexPanelLayoutLocal::NonNegative(Spacing.Y);
	const float AvailableWidth = FMath::Max(0.0f, GetWidget()->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding) - GapX * FMath::Max(0, ColumnCount - 1));
	const float AvailableHeight = FMath::Max(0.0f, GetWidget()->GetHeight() - LexPanelLayoutLocal::VerticalPadding(Padding) - GapY * FMath::Max(0, RowCount - 1));
	const TArray<float> Columns = LexPanelLayoutLocal::ArrangeTracks(DesiredColumns, ColumnFill, AvailableWidth);
	const TArray<float> Rows = LexPanelLayoutLocal::ArrangeTracks(DesiredRows, RowFill, AvailableHeight);

	LexPanelLayoutLocal::ApplyStableZOrderWithinParticipatingSlots(GetWidget(), LayoutChildren);

	for (ULexWidget* Child : LayoutChildren)
	{
		if (ColumnCount <= 0 || RowCount <= 0) break;
		const ULexPanelSlot* Slot = GetSlot(Child);
		const int32 Column = FMath::Clamp(LexPanelLayoutLocal::GridIndex(Slot->Column), 0, ColumnCount - 1);
		const int32 Row = FMath::Clamp(LexPanelLayoutLocal::GridIndex(Slot->Row), 0, RowCount - 1);
		const int32 ColumnSpan = FMath::Clamp(Slot->ColumnSpan, 1, ColumnCount - Column);
		const int32 RowSpan = FMath::Clamp(Slot->RowSpan, 1, RowCount - Row);
		const float X = LexPanelLayoutLocal::FiniteOrZero(Padding.Left) + LexPanelLayoutLocal::Sum(Columns, 0, Column) + GapX * Column;
		const float Y = LexPanelLayoutLocal::FiniteOrZero(Padding.Top) + LexPanelLayoutLocal::Sum(Rows, 0, Row) + GapY * Row;
		const float Width = LexPanelLayoutLocal::Sum(Columns, Column, ColumnSpan) + GapX * (ColumnSpan - 1);
		const float Height = LexPanelLayoutLocal::Sum(Rows, Row, RowSpan) + GapY * (RowSpan - 1);
		ApplyChildRect(Child, FVector2D(X, Y), FVector2D(Width, Height));
	}
	PreferredSize = MeasureLayout();
}

FVector2f ULexLayoutContainerUniformGridPanel::MeasureLayout() const
{
	const TArray<ULexWidget*> LayoutChildren = CollectLayoutChildren(false);
	int32 ColumnCount = 0;
	int32 RowCount = 0;
	float CellWidth = LexPanelLayoutLocal::NonNegative(MinDesiredSlotWidth);
	float CellHeight = LexPanelLayoutLocal::NonNegative(MinDesiredSlotHeight);
	const float GapX = LexPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = LexPanelLayoutLocal::NonNegative(Spacing.Y);
	for (ULexWidget* Child : LayoutChildren)
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		const int32 ColumnSpan = LexPanelLayoutLocal::GridSpan(Slot->ColumnSpan);
		const int32 RowSpan = LexPanelLayoutLocal::GridSpan(Slot->RowSpan);
		ColumnCount = FMath::Max(ColumnCount, LexPanelLayoutLocal::GridTrackEnd(Slot->Column, ColumnSpan));
		RowCount = FMath::Max(RowCount, LexPanelLayoutLocal::GridTrackEnd(Slot->Row, RowSpan));
		const FVector2D Desired = GetDesiredSize(Child);
		CellWidth = FMath::Max(CellWidth,
			FMath::Max(0.0f, static_cast<float>(Desired.X) + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding) - GapX * (ColumnSpan - 1)) / ColumnSpan);
		CellHeight = FMath::Max(CellHeight,
			FMath::Max(0.0f, static_cast<float>(Desired.Y) + LexPanelLayoutLocal::VerticalPadding(Slot->Padding) - GapY * (RowSpan - 1)) / RowSpan);
	}
	return FVector2f(
		CellWidth * ColumnCount + GapX * FMath::Max(0, ColumnCount - 1) + LexPanelLayoutLocal::HorizontalPadding(Padding),
		CellHeight * RowCount + GapY * FMath::Max(0, RowCount - 1) + LexPanelLayoutLocal::VerticalPadding(Padding));
}

void ULexLayoutContainerUniformGridPanel::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	const TArray<ULexWidget*> LayoutChildren = CollectLayoutChildren();
	int32 ColumnCount = 0;
	int32 RowCount = 0;
	for (ULexWidget* Child : LayoutChildren)
	{
		const ULexPanelSlot* Slot = GetSlot(Child);
		ColumnCount = FMath::Max(ColumnCount, LexPanelLayoutLocal::GridTrackEnd(Slot->Column, Slot->ColumnSpan));
		RowCount = FMath::Max(RowCount, LexPanelLayoutLocal::GridTrackEnd(Slot->Row, Slot->RowSpan));
	}
	const float GapX = LexPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = LexPanelLayoutLocal::NonNegative(Spacing.Y);
	const float CellWidth = ColumnCount > 0
		? FMath::Max(0.0f, GetWidget()->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding) - GapX * (ColumnCount - 1)) / ColumnCount
		: 0.0f;
	const float CellHeight = RowCount > 0
		? FMath::Max(0.0f, GetWidget()->GetHeight() - LexPanelLayoutLocal::VerticalPadding(Padding) - GapY * (RowCount - 1)) / RowCount
		: 0.0f;
	for (ULexWidget* Child : LayoutChildren)
	{
		if (ColumnCount <= 0 || RowCount <= 0) break;
		const ULexPanelSlot* Slot = GetSlot(Child);
		const int32 Column = FMath::Clamp(LexPanelLayoutLocal::GridIndex(Slot->Column), 0, ColumnCount - 1);
		const int32 Row = FMath::Clamp(LexPanelLayoutLocal::GridIndex(Slot->Row), 0, RowCount - 1);
		const int32 ColumnSpan = FMath::Clamp(Slot->ColumnSpan, 1, ColumnCount - Column);
		const int32 RowSpan = FMath::Clamp(Slot->RowSpan, 1, RowCount - Row);
		ApplyChildRect(Child,
			FVector2D(LexPanelLayoutLocal::FiniteOrZero(Padding.Left) + Column * (CellWidth + GapX),
				LexPanelLayoutLocal::FiniteOrZero(Padding.Top) + Row * (CellHeight + GapY)),
			FVector2D(CellWidth * ColumnSpan + GapX * (ColumnSpan - 1), CellHeight * RowSpan + GapY * (RowSpan - 1)));
	}
	PreferredSize = MeasureLayout();
}

FVector2f ULexLayoutContainerSizeBox::MeasureLayout() const
{
	ULexWidget* Content = LexPanelLayoutLocal::GetFirstValidChild(GetWidget());
	const bool bContentParticipates = IsValid(Content) && Content->GetLayoutVisibleInHierarchy()
		&& !Content->GetIgnoreLayout();
	FVector2D Desired = bContentParticipates ? GetDesiredSize(Content) : FVector2D::ZeroVector;
	if (bContentParticipates)
	{
		const ULexPanelSlot* Slot = GetSlot(Content);
		Desired.X += LexPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		Desired.Y += LexPanelLayoutLocal::VerticalPadding(Slot->Padding);
	}
	Desired.X += LexPanelLayoutLocal::HorizontalPadding(Padding);
	Desired.Y += LexPanelLayoutLocal::VerticalPadding(Padding);
	if (bOverrideWidth)
	{
		Desired.X = LexPanelLayoutLocal::NonNegative(WidthOverride);
	}
	else
	{
		Desired.X = FMath::Max(Desired.X, LexPanelLayoutLocal::NonNegative(MinDesiredSize.X));
		if (MaxDesiredSize.X > 0.0 && FMath::IsFinite(MaxDesiredSize.X)) Desired.X = FMath::Min(Desired.X, MaxDesiredSize.X);
	}
	if (bOverrideHeight)
	{
		Desired.Y = LexPanelLayoutLocal::NonNegative(HeightOverride);
	}
	else
	{
		Desired.Y = FMath::Max(Desired.Y, LexPanelLayoutLocal::NonNegative(MinDesiredSize.Y));
		if (MaxDesiredSize.Y > 0.0 && FMath::IsFinite(MaxDesiredSize.Y)) Desired.Y = FMath::Min(Desired.Y, MaxDesiredSize.Y);
	}
	return FVector2f(LexPanelLayoutLocal::CleanSize(Desired));
}

FLexLayoutControlAnchorData ULexLayoutContainerSizeBox::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
	const ULexWidget* Content = LexPanelLayoutLocal::GetFirstValidChild(GetWidget());
	return Content == TargetWidget
		? Super::GetLayoutControlAnchor(TargetWidget)
		: FLexLayoutControlAnchorData();
}

void ULexLayoutContainerSizeBox::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	ULexWidget* Content = nullptr;
	for (ULexWidget* Child : GetWidget()->GetChildren())
	{
		if (!IsValid(Child))
		{
			continue;
		}
		if (!IsValid(Content))
		{
			Content = Child;
			Child->SetLayoutVisibilitySuppressed(false);
			continue;
		}
		if (ULexPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
		{
			Slot->RestoreAuthoredGeometry();
		}
		Child->SetLayoutVisibilitySuppressed(false);
	}
	if (IsValid(Content))
	{
		if (Content->GetLayoutVisibleInHierarchy() && !Content->GetIgnoreLayout())
		{
			ApplyChildRect(Content, FVector2D(LexPanelLayoutLocal::FiniteOrZero(Padding.Left), LexPanelLayoutLocal::FiniteOrZero(Padding.Top)), FVector2D(
				FMath::Max(0.0f, GetWidget()->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding)),
				FMath::Max(0.0f, GetWidget()->GetHeight() - LexPanelLayoutLocal::VerticalPadding(Padding))));
		}
		else
		{
			ReleaseSkippedChildGeometry(Content);
		}
	}
	PreferredSize = MeasureLayout();
}

FVector2f ULexLayoutContainerScaleBox::MeasureLayout() const
{
	ULexWidget* Content = LexPanelLayoutLocal::GetFirstValidChild(GetWidget());
	if (!IsValid(Content) || !Content->GetLayoutVisibleInHierarchy()
		|| Content->GetIgnoreLayout())
	{
		return FVector2f::ZeroVector;
	}
	FVector2D Desired = GetDesiredSize(Content);
	const ULexPanelSlot* Slot = GetSlot(Content);
	FVector2f DesiredScale = FVector2f::UnitVector;
	if (Stretch == ELexScaleBoxStretch::UserSpecified)
	{
		DesiredScale = FVector2f(LexPanelLayoutLocal::NonNegative(UserSpecifiedScale));
	}
	else if (Stretch == ELexScaleBoxStretch::ScaleToFitX && Desired.X > UE_SMALL_NUMBER)
	{
		const float AvailableWidth = FMath::Max(0.0f, GetWidget()->GetWidth()
			- LexPanelLayoutLocal::HorizontalPadding(Padding) - LexPanelLayoutLocal::HorizontalPadding(Slot->Padding));
		DesiredScale = FVector2f(LexPanelLayoutLocal::NonNegative(AvailableWidth / Desired.X));
	}
	else if (Stretch == ELexScaleBoxStretch::ScaleToFitY && Desired.Y > UE_SMALL_NUMBER)
	{
		const float AvailableHeight = FMath::Max(0.0f, GetWidget()->GetHeight()
			- LexPanelLayoutLocal::VerticalPadding(Padding) - LexPanelLayoutLocal::VerticalPadding(Slot->Padding));
		DesiredScale = FVector2f(LexPanelLayoutLocal::NonNegative(AvailableHeight / Desired.Y));
	}
	if (bIgnoreInheritedScale && (Stretch == ELexScaleBoxStretch::UserSpecified
		|| Stretch == ELexScaleBoxStretch::ScaleToFitX || Stretch == ELexScaleBoxStretch::ScaleToFitY))
	{
		const FVector ParentScale = GetWidget()->GetWorldScale();
		if (!FMath::IsNearlyZero(ParentScale.Y)) DesiredScale.X /= FMath::Abs(ParentScale.Y);
		if (!FMath::IsNearlyZero(ParentScale.Z)) DesiredScale.Y /= FMath::Abs(ParentScale.Z);
		DesiredScale.X = LexPanelLayoutLocal::NonNegative(DesiredScale.X);
		DesiredScale.Y = LexPanelLayoutLocal::NonNegative(DesiredScale.Y);
	}
	if (Stretch == ELexScaleBoxStretch::ScaleToFitX && DesiredScale.Y > 0.0f)
	{
		Desired.Y *= DesiredScale.Y;
	}
	else if (Stretch == ELexScaleBoxStretch::ScaleToFitY && DesiredScale.X > 0.0f)
	{
		Desired.X *= DesiredScale.X;
	}
	else if (Stretch == ELexScaleBoxStretch::UserSpecified)
	{
		Desired.X *= DesiredScale.X;
		Desired.Y *= DesiredScale.Y;
	}
	return FVector2f(
		Desired.X + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding) + LexPanelLayoutLocal::HorizontalPadding(Padding),
		Desired.Y + LexPanelLayoutLocal::VerticalPadding(Slot->Padding) + LexPanelLayoutLocal::VerticalPadding(Padding));
}

FLexLayoutControlAnchorData ULexLayoutContainerScaleBox::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
	const ULexWidget* Content = LexPanelLayoutLocal::GetFirstValidChild(GetWidget());
	return Content == TargetWidget
		? Super::GetLayoutControlAnchor(TargetWidget)
		: FLexLayoutControlAnchorData();
}

void ULexLayoutContainerScaleBox::CalculateLayout()
{
	UpdateClippingOverride();
	if (!BeginLayoutPass()) return;
	ULexWidget* Child = nullptr;
	for (ULexWidget* Candidate : GetWidget()->GetChildren())
	{
		if (!IsValid(Candidate))
		{
			continue;
		}
		if (!IsValid(Child))
		{
			Child = Candidate;
			Candidate->SetLayoutVisibilitySuppressed(false);
			continue;
		}
		if (ULexPanelSlot* Slot = Candidate->GetPanelSlot(); IsValid(Slot))
		{
			Slot->RestoreAuthoredGeometry();
		}
		Candidate->SetLayoutScale(FVector2f::UnitVector);
		Candidate->SetLayoutVisibilitySuppressed(false);
	}
	if (!IsValid(Child) || !Child->GetLayoutVisibleInHierarchy()
		|| Child->GetIgnoreLayout())
	{
		ReleaseSkippedChildGeometry(Child);
		if (ScaledChild.IsValid() && ScaledChild->GetParent() == GetWidget())
		{
			ScaledChild->SetLayoutScale(FVector2f::UnitVector);
		}
		ScaledChild.Reset();
		PreferredSize = MeasureLayout();
		return;
	}
	if (ULexPanelSlot* MutableSlot = Child->GetPanelSlot(); IsValid(MutableSlot))
	{
		MutableSlot->MarkLayoutGeometryApplied();
	}
	if (ScaledChild.IsValid() && ScaledChild.Get() != Child && ScaledChild->GetParent() == GetWidget())
	{
		ScaledChild->SetLayoutScale(FVector2f::UnitVector);
	}
	ScaledChild = Child;
	const ULexPanelSlot* Slot = GetSlot(Child);
	const FVector2D Desired = GetDesiredSize(Child);
	const FVector2D InnerPosition(
		LexPanelLayoutLocal::FiniteOrZero(Padding.Left) + LexPanelLayoutLocal::FiniteOrZero(Slot->Padding.Left),
		LexPanelLayoutLocal::FiniteOrZero(Padding.Top) + LexPanelLayoutLocal::FiniteOrZero(Slot->Padding.Top));
	const float AvailableWidth = FMath::Max(0.0f, GetWidget()->GetWidth()
		- LexPanelLayoutLocal::HorizontalPadding(Padding) - LexPanelLayoutLocal::HorizontalPadding(Slot->Padding));
	const float AvailableHeight = FMath::Max(0.0f, GetWidget()->GetHeight()
		- LexPanelLayoutLocal::VerticalPadding(Padding) - LexPanelLayoutLocal::VerticalPadding(Slot->Padding));
	const float ScaleX = Desired.X > UE_SMALL_NUMBER ? AvailableWidth / Desired.X : 1.0f;
	const float ScaleY = Desired.Y > UE_SMALL_NUMBER ? AvailableHeight / Desired.Y : 1.0f;
	FVector2f Scale(1.0f, 1.0f);
	if (Stretch == ELexScaleBoxStretch::UserSpecified)
	{
		Scale = FVector2f(LexPanelLayoutLocal::NonNegative(UserSpecifiedScale));
	}
	else if (Desired.X > UE_SMALL_NUMBER && Desired.Y > UE_SMALL_NUMBER)
	{
		switch (Stretch)
		{
		case ELexScaleBoxStretch::ScaleToFit: Scale = FVector2f(FMath::Min(ScaleX, ScaleY)); break;
		case ELexScaleBoxStretch::ScaleToFill: Scale = FVector2f(FMath::Max(ScaleX, ScaleY)); break;
		case ELexScaleBoxStretch::ScaleToFitX: Scale = FVector2f(ScaleX); break;
		case ELexScaleBoxStretch::ScaleToFitY: Scale = FVector2f(ScaleY); break;
		default: break;
		}
	}
	if (bIgnoreInheritedScale && Stretch != ELexScaleBoxStretch::Fill)
	{
		const FVector ParentScale = GetWidget()->GetWorldScale();
		if (!FMath::IsNearlyZero(ParentScale.Y)) Scale.X /= FMath::Abs(ParentScale.Y);
		if (!FMath::IsNearlyZero(ParentScale.Z)) Scale.Y /= FMath::Abs(ParentScale.Z);
	}
	Scale.X = LexPanelLayoutLocal::NonNegative(Scale.X);
	Scale.Y = LexPanelLayoutLocal::NonNegative(Scale.Y);

	FVector2D UnscaledSize = Stretch == ELexScaleBoxStretch::Fill
		? FVector2D(AvailableWidth, AvailableHeight)
		: Desired;
	FVector2D ScaledSize(Desired.X * Scale.X, Desired.Y * Scale.Y);
	if (Stretch == ELexScaleBoxStretch::Fill)
	{
		ScaledSize = UnscaledSize;
	}
	if (Slot->HorizontalAlignment == ELexPanelHorizontalAlignment::Fill && Scale.X > UE_SMALL_NUMBER)
	{
		UnscaledSize.X = AvailableWidth / Scale.X;
		ScaledSize.X = AvailableWidth;
	}
	if (Slot->VerticalAlignment == ELexPanelVerticalAlignment::Fill && Scale.Y > UE_SMALL_NUMBER)
	{
		UnscaledSize.Y = AvailableHeight / Scale.Y;
		ScaledSize.Y = AvailableHeight;
	}
	double Left = InnerPosition.X;
	double Top = InnerPosition.Y;
	switch (Slot->HorizontalAlignment)
	{
	case ELexPanelHorizontalAlignment::Center: Left += (AvailableWidth - ScaledSize.X) * 0.5; break;
	case ELexPanelHorizontalAlignment::Right: Left += AvailableWidth - ScaledSize.X; break;
	default: break;
	}
	switch (Slot->VerticalAlignment)
	{
	case ELexPanelVerticalAlignment::Center: Top += (AvailableHeight - ScaledSize.Y) * 0.5; break;
	case ELexPanelVerticalAlignment::Bottom: Top += AvailableHeight - ScaledSize.Y; break;
	default: break;
	}

	Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5), FVector2D(0.5), true, true);
	if (ULexLayoutSelfFlexBox* FlexSelf = Cast<ULexLayoutSelfFlexBox>(Child->GetLayoutSelf()))
	{
		FlexSelf->SetFinalSizeByLayoutContainer(FVector2f(UnscaledSize));
		Child->SetSizeDelta(UnscaledSize);
	}
	else
	{
		Child->SetWidth(static_cast<float>(UnscaledSize.X));
		Child->SetHeight(static_cast<float>(UnscaledSize.Y));
	}
	Child->SetLayoutScale(Scale);
	const FVector2D Pivot = Child->GetPivot();
	Child->SetAnchoredPosition(FVector2D(
		-GetWidget()->GetWidth() * 0.5 + Left + ScaledSize.X * Pivot.X,
		GetWidget()->GetHeight() * 0.5 - Top - ScaledSize.Y * (1.0 - Pivot.Y)));
	PreferredSize = MeasureLayout();
}

void ULexLayoutContainerScaleBox::OnRegister()
{
	Super::OnRegister();
	UpdateClippingOverride();
}

void ULexLayoutContainerScaleBox::UpdateClippingOverride()
{
	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		bAppliedDefaultClipping = false;
		return;
	}
	const bool bNeedsClipping = Stretch == ELexScaleBoxStretch::ScaleToFill
		|| Stretch == ELexScaleBoxStretch::ScaleToFitX
		|| Stretch == ELexScaleBoxStretch::ScaleToFitY;
	if (bNeedsClipping)
	{
		Widget->SetLayoutClippingOverride(ELexWidgetClipping::ClipToBounds);
		bAppliedDefaultClipping = true;
	}
	else if (bAppliedDefaultClipping)
	{
		Widget->ClearLayoutClippingOverride();
		bAppliedDefaultClipping = false;
	}
}

void ULexLayoutContainerScaleBox::OnUnregister()
{
	if (ScaledChild.IsValid() && ScaledChild->GetParent() == GetWidget())
	{
		ScaledChild->SetLayoutScale(FVector2f::UnitVector);
	}
	ScaledChild.Reset();
	if (bAppliedDefaultClipping && IsValid(GetWidget()))
	{
		GetWidget()->ClearLayoutClippingOverride();
	}
	bAppliedDefaultClipping = false;
	Super::OnUnregister();
}

FMargin ULexLayoutContainerSafeZone::GetCombinedSafePadding() const
{
	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return FMargin();
	}
	const FVector2D WidgetSize = LexPanelLayoutLocal::CleanSize(Widget->GetSize());
	const FMargin CleanSafe = LexPanelLayoutLocal::CleanNonNegativeMargin(SafePadding);
	const FMargin CleanNormalized = LexPanelLayoutLocal::CleanNormalizedSafePadding(NormalizedSafePadding);
#if WITH_EDITOR
	// A unit editor override gives the platform padding coefficient directly,
	// without making preferred size depend on this widget's current size.
	const FMargin PlatformNormalized = LexPanelLayoutLocal::GetPlatformSafePadding(
		bUsePlatformSafeZone, bPadLeft, bPadTop, bPadRight, bPadBottom, FVector2D(1.0));
	const FMargin CombinedNormalized = LexPanelLayoutLocal::CleanNormalizedSafePadding(FMargin(
		CleanNormalized.Left + PlatformNormalized.Left,
		CleanNormalized.Top + PlatformNormalized.Top,
		CleanNormalized.Right + PlatformNormalized.Right,
		CleanNormalized.Bottom + PlatformNormalized.Bottom));
	return FMargin(
		CleanSafe.Left + WidgetSize.X * CombinedNormalized.Left,
		CleanSafe.Top + WidgetSize.Y * CombinedNormalized.Top,
		CleanSafe.Right + WidgetSize.X * CombinedNormalized.Right,
		CleanSafe.Bottom + WidgetSize.Y * CombinedNormalized.Bottom);
#else
	const FMargin CleanPlatform = LexPanelLayoutLocal::GetPlatformSafePadding(
		bUsePlatformSafeZone, bPadLeft, bPadTop, bPadRight, bPadBottom, WidgetSize);
	return FMargin(
		CleanSafe.Left + CleanPlatform.Left
			+ WidgetSize.X * CleanNormalized.Left,
		CleanSafe.Top + CleanPlatform.Top
			+ WidgetSize.Y * CleanNormalized.Top,
		CleanSafe.Right + CleanPlatform.Right
			+ WidgetSize.X * CleanNormalized.Right,
		CleanSafe.Bottom + CleanPlatform.Bottom
			+ WidgetSize.Y * CleanNormalized.Bottom);
#endif
}

FVector2f ULexLayoutContainerSafeZone::MeasureLayout() const
{
	FVector2f Result = FVector2f::ZeroVector;
	ULexWidget* Content = LexPanelLayoutLocal::GetFirstValidChild(GetWidget());
	if (IsValid(Content) && Content->GetLayoutVisibleInHierarchy()
		&& !Content->GetIgnoreLayout())
	{
		const FVector2D Desired = GetDesiredSize(Content);
		const ULexPanelSlot* Slot = GetSlot(Content);
		Result.X = static_cast<float>(Desired.X + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding));
		Result.Y = static_cast<float>(Desired.Y + LexPanelLayoutLocal::VerticalPadding(Slot->Padding));
	}
	FMargin AbsolutePadding = LexPanelLayoutLocal::CleanNonNegativeMargin(SafePadding);
	FMargin CombinedNormalized = LexPanelLayoutLocal::CleanNormalizedSafePadding(NormalizedSafePadding);
#if WITH_EDITOR
	const FMargin PlatformNormalized = LexPanelLayoutLocal::GetPlatformSafePadding(
		bUsePlatformSafeZone, bPadLeft, bPadTop, bPadRight, bPadBottom, FVector2D(1.0));
	CombinedNormalized = LexPanelLayoutLocal::CleanNormalizedSafePadding(FMargin(
		CombinedNormalized.Left + PlatformNormalized.Left,
		CombinedNormalized.Top + PlatformNormalized.Top,
		CombinedNormalized.Right + PlatformNormalized.Right,
		CombinedNormalized.Bottom + PlatformNormalized.Bottom));
#else
	const FMargin PlatformPadding = LexPanelLayoutLocal::GetPlatformSafePadding(
		bUsePlatformSafeZone, bPadLeft, bPadTop, bPadRight, bPadBottom, FVector2D::ZeroVector);
	AbsolutePadding = FMargin(
		AbsolutePadding.Left + PlatformPadding.Left,
		AbsolutePadding.Top + PlatformPadding.Top,
		AbsolutePadding.Right + PlatformPadding.Right,
		AbsolutePadding.Bottom + PlatformPadding.Bottom);
#endif
	const float NormalizedHorizontal = CombinedNormalized.Left + CombinedNormalized.Right;
	const float NormalizedVertical = CombinedNormalized.Top + CombinedNormalized.Bottom;
	const float AbsoluteHorizontal = LexPanelLayoutLocal::HorizontalPadding(AbsolutePadding);
	const float AbsoluteVertical = LexPanelLayoutLocal::VerticalPadding(AbsolutePadding);
	Result.X = (Result.X + FMath::Max(0.0f, AbsoluteHorizontal)) / FMath::Max(1.0e-3f, 1.0f - NormalizedHorizontal);
	Result.Y = (Result.Y + FMath::Max(0.0f, AbsoluteVertical)) / FMath::Max(1.0e-3f, 1.0f - NormalizedVertical);
	return Result;
}

void ULexLayoutContainerSafeZone::OnRegister()
{
	Super::OnRegister();
	if (!SafeFrameChangedHandle.IsValid())
	{
		SafeFrameChangedHandle = FCoreDelegates::OnSafeFrameChangedEvent.AddUObject(
			this, &ULexLayoutContainerSafeZone::HandleSafeFrameChanged);
	}
}

void ULexLayoutContainerSafeZone::OnUnregister()
{
	if (SafeFrameChangedHandle.IsValid())
	{
		FCoreDelegates::OnSafeFrameChangedEvent.Remove(SafeFrameChangedHandle);
		SafeFrameChangedHandle.Reset();
	}
	Super::OnUnregister();
}

void ULexLayoutContainerSafeZone::HandleSafeFrameChanged()
{
	if (bUsePlatformSafeZone)
	{
		RequestLayoutRefresh();
	}
}

FLexLayoutControlAnchorData ULexLayoutContainerSafeZone::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
	return LexPanelLayoutLocal::GetFirstValidChild(GetWidget()) == TargetWidget
		? Super::GetLayoutControlAnchor(TargetWidget)
		: FLexLayoutControlAnchorData();
}

void ULexLayoutContainerSafeZone::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	const FMargin Combined = GetCombinedSafePadding();
	ULexWidget* Content = nullptr;
	for (ULexWidget* Child : GetWidget()->GetChildren())
	{
		if (!IsValid(Child)) continue;
		if (!IsValid(Content))
		{
			Content = Child;
			continue;
		}
		if (ULexPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot)) Slot->RestoreAuthoredGeometry();
	}
	if (IsValid(Content))
	{
		if (Content->GetLayoutVisibleInHierarchy() && !Content->GetIgnoreLayout())
		{
			ApplyChildRect(Content, FVector2D(Combined.Left, Combined.Top), FVector2D(
				FMath::Max(0.0f, GetWidget()->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Combined)),
				FMath::Max(0.0f, GetWidget()->GetHeight() - LexPanelLayoutLocal::VerticalPadding(Combined))));
		}
		else
		{
			ReleaseSkippedChildGeometry(Content);
		}
	}
	PreferredSize = MeasureLayout();
}

ULexLayoutContainerScrollBox::ULexLayoutContainerScrollBox()
{
	Orientation = ELexPanelOrientation::Vertical;
}

void ULexLayoutContainerScrollBox::OnRegister()
{
	Super::OnRegister();
	if (IsValid(GetWidget()))
	{
		bAppliedDefaultClipping = true;
		GetWidget()->SetLayoutClippingOverride(ELexWidgetClipping::ClipToBounds);
	}
}

void ULexLayoutContainerScrollBox::OnUnregister()
{
	if (IsValid(GetWidget()))
	{
		GetWidget()->ClearLayoutClippingOverride();
	}
	bAppliedDefaultClipping = false;
	Super::OnUnregister();
}

FVector2f ULexLayoutContainerWidgetSwitcher::MeasureLayout() const
{
	ULexWidget* Panel = GetWidget();
	if (!IsValid(Panel))
	{
		return FVector2f::ZeroVector;
	}
	ULexWidget* Child = ActiveWidget.IsValid() && ActiveWidget->GetParent() == Panel
		? ActiveWidget.Get()
		: (Panel->GetChildren().IsValidIndex(ActiveWidgetIndex)
			? Panel->GetChildren()[ActiveWidgetIndex]
			: nullptr);
	if (!IsValid(Child) || !Child->GetWidgetActiveInHierarchy() || Child->GetVisibility() == ELexWidgetVisibility::Collapsed)
	{
		return FVector2f::ZeroVector;
	}
	if (Child->GetIgnoreLayout())
	{
		return FVector2f::ZeroVector;
	}
	const FVector2D Desired = GetDesiredSize(Child);
	const ULexPanelSlot* Slot = GetSlot(Child);
	return FVector2f(
		Desired.X + LexPanelLayoutLocal::HorizontalPadding(Slot->Padding) + LexPanelLayoutLocal::HorizontalPadding(Padding),
		Desired.Y + LexPanelLayoutLocal::VerticalPadding(Slot->Padding) + LexPanelLayoutLocal::VerticalPadding(Padding));
}

void ULexLayoutContainerWidgetSwitcher::CalculateLayout()
{
	if (!BeginLayoutPass()) return;
	ULexWidget* Panel = GetWidget();
	ULexWidget* ActiveChild = ActiveWidget.IsValid() && ActiveWidget->GetParent() == Panel
		? ActiveWidget.Get()
		: nullptr;
	if (IsValid(ActiveChild))
	{
		ActiveWidgetIndex = Panel->GetChildren().IndexOfByKey(ActiveChild);
	}
	else if (Panel->GetChildrenCount() > 0)
	{
		ActiveWidgetIndex = FMath::Clamp(ActiveWidgetIndex, 0, Panel->GetChildrenCount() - 1);
		ActiveChild = Panel->GetChildren()[ActiveWidgetIndex];
	}
	else
	{
		ActiveWidgetIndex = 0;
	}
	ActiveWidget = ActiveChild;
	for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
	{
		if (ULexWidget* Child = Panel->GetChildren()[Index]; IsValid(Child))
		{
			const bool bIsActive = Child == ActiveChild;
			const bool bIgnored = Child->GetIgnoreLayout();
			if (!bIsActive || bIgnored)
			{
				if (ULexPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
				{
					Slot->RestoreAuthoredGeometry();
				}
			}
			Child->SetLayoutVisibilitySuppressed(!bIsActive);
		}
	}
	if (ULexWidget* Child = ActiveChild; IsValid(Child) && Child->GetLayoutVisibleInHierarchy())
	{
		if (!Child->GetIgnoreLayout())
		{
			EnsureSlot(Child);
			ApplyChildRect(Child, FVector2D(LexPanelLayoutLocal::FiniteOrZero(Padding.Left), LexPanelLayoutLocal::FiniteOrZero(Padding.Top)), FVector2D(
				FMath::Max(0.0f, Panel->GetWidth() - LexPanelLayoutLocal::HorizontalPadding(Padding)),
				FMath::Max(0.0f, Panel->GetHeight() - LexPanelLayoutLocal::VerticalPadding(Padding))));
		}
	}
	PreferredSize = MeasureLayout();
}

void ULexLayoutContainerWidgetSwitcher::OnUnregister()
{
	if (ULexWidget* Panel = GetWidget(); IsValid(Panel))
	{
		for (ULexWidget* Child : Panel->GetChildren())
		{
			if (IsValid(Child)) Child->SetLayoutVisibilitySuppressed(false);
		}
	}
	ActiveWidget.Reset();
	Super::OnUnregister();
}

void ULexLayoutContainerWidgetSwitcher::SetActiveWidgetIndex(int32 Value)
{
	ULexWidget* Panel = GetWidget();
	const int32 Clamped = IsValid(Panel) && Panel->GetChildrenCount() > 0
		? FMath::Clamp(Value, 0, Panel->GetChildrenCount() - 1)
		: 0;
	ULexWidget* NewActiveWidget = IsValid(Panel) && Panel->GetChildren().IsValidIndex(Clamped)
		? Panel->GetChildren()[Clamped]
		: nullptr;
	if (ActiveWidgetIndex != Clamped || ActiveWidget.Get() != NewActiveWidget)
	{
		ActiveWidgetIndex = Clamped;
		ActiveWidget = NewActiveWidget;
		ULexWidget::MarkLayoutForRebuild(Panel);
	}
}

ULexWidget* ULexLayoutContainerWidgetSwitcher::GetActiveWidget() const
{
	ULexWidget* Panel = GetWidget();
	if (ActiveWidget.IsValid() && ActiveWidget->GetParent() == Panel)
	{
		return ActiveWidget.Get();
	}
	return IsValid(Panel) && Panel->GetChildren().IsValidIndex(ActiveWidgetIndex)
		? Panel->GetChildren()[ActiveWidgetIndex]
		: nullptr;
}
