// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
// Portions derived from DreamGUI, Copyright 2026-Present LexLiu. All Rights Reserved.

#include "Core/Components/DreamPanelLayouts.h"
#include "DreamGUI.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamScrollBoxInputHandler.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UIScrollbar.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamVisual.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CoreDelegates.h"

/**
 * Frame-by-frame scroll box trace, for the class of bug a console command cannot catch: the state
 * mid-gesture, while the pointer is held and the console is unreachable. Off by default; costs one
 * integer read per call site when off.
 */
static TAutoConsoleVariable<int32> CVarDreamScrollBoxTrace(
	TEXT("dreamgui.ScrollBoxTrace"), 0,
	TEXT("1 = log every scroll box drag delta and physics tick that changes state."));

/**
 * Traces the layout pass itself: which panels were asked, which got past their dirty gate, what size
 * they were working from, and what rect each child came out with. Reading a screenshot only ever says
 * "this is not where I expected it"; the difference between "never arranged", "arranged against a stale
 * size" and "arranged correctly against a size you did not expect" is not visible from outside.
 */
static TAutoConsoleVariable<int32> CVarDreamLayoutTrace(
	TEXT("dreamgui.LayoutTrace"), 0,
	TEXT("1 = log every panel arrange and every rect it commits."));
#include "Widgets/Layout/SSafeZone.h"

namespace DreamPanelLayoutLocal
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

	static UDreamWidget* GetFirstValidChild(const UDreamWidget* Panel)
	{
		if (!IsValid(Panel))
		{
			return nullptr;
		}
		for (UDreamWidget* Child : Panel->GetChildren())
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

	static EDreamPanelOrientation CleanOrientation(EDreamPanelOrientation Value)
	{
		return Value == EDreamPanelOrientation::Horizontal || Value == EDreamPanelOrientation::Vertical
			? Value
			: EDreamPanelOrientation::Vertical;
	}

	static EDreamScaleBoxStretch CleanStretch(EDreamScaleBoxStretch Value)
	{
		switch (Value)
		{
		case EDreamScaleBoxStretch::None:
		case EDreamScaleBoxStretch::Fill:
		case EDreamScaleBoxStretch::ScaleToFit:
		case EDreamScaleBoxStretch::ScaleToFill:
		case EDreamScaleBoxStretch::ScaleToFitX:
		case EDreamScaleBoxStretch::ScaleToFitY:
		case EDreamScaleBoxStretch::UserSpecified:
			return Value;
		default:
			return EDreamScaleBoxStretch::ScaleToFit;
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

	static void ApplyStableZOrderWithinParticipatingSlots(UDreamWidget* Panel, TArray<UDreamWidget*>& ParticipatingChildren)
	{
		if (!IsValid(Panel) || ParticipatingChildren.Num() < 2)
		{
			return;
		}

		// Read each slot's ZOrder ONCE into a key, then sort on the key. The comparator used to be
		// `IsValid(ASlot) && IsValid(BSlot) && ASlot->ZOrder < BSlot->ZOrder`, which is not a strict weak
		// ordering: a slotless element compares "not less" against everything AND everything compares
		// "not less" against it, so it is equivalent to every element at once and equivalence stops being
		// transitive. StableSort is a merge sort and will not run off the end the way an introsort can,
		// but the order it produces is simply undefined. A missing slot is now an explicit key of 0,
		// which is also the default ZOrder, so a slotless child sorts where an unconfigured one would.
		struct FZOrderKey
		{
			UDreamWidget* Child = nullptr;
			int32 ZOrder = 0;
		};
		TArray<FZOrderKey> Keyed;
		Keyed.Reserve(ParticipatingChildren.Num());
		for (UDreamWidget* Child : ParticipatingChildren)
		{
			const UDreamPanelSlot* Slot = IsValid(Child) ? Child->GetPanelSlot() : nullptr;
			FZOrderKey Key;
			Key.Child = Child;
			Key.ZOrder = IsValid(Slot) ? Slot->ZOrder : 0;
			Keyed.Add(Key);
		}
		Keyed.StableSort([](const FZOrderKey& A, const FZOrderKey& B) { return A.ZOrder < B.ZOrder; });
		for (int32 Index = 0; Index < Keyed.Num(); ++Index)
		{
			ParticipatingChildren[Index] = Keyed[Index].Child;
		}

		TSet<const UDreamWidget*> ParticipatingSet;
		for (const UDreamWidget* Child : ParticipatingChildren)
		{
			ParticipatingSet.Add(Child);
		}
		TArray<UDreamWidget*> DesiredOrder = Panel->GetChildren();
		int32 SortedIndex = 0;
		for (UDreamWidget*& Child : DesiredOrder)
		{
			if (ParticipatingSet.Contains(Child))
			{
				Child = ParticipatingChildren[SortedIndex++];
			}
		}
		// One linear rewrite instead of a SetSiblingIndex per moved child. See the contract on
		// UDreamWidget::ReorderChildrenToPaintOrder: the per-child form re-entered ApplySiblingIndex
		// (O(n) each) and raised MarkLayoutForRebuild on the panel that is arranging RIGHT NOW, relighting
		// the bIsLayoutDirty it had just consumed -- so any real ZOrder change bought a second full pass.
		Panel->ReorderChildrenToPaintOrder(DesiredOrder);
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

LEX_PANEL_SETTER(UDreamLayoutContainerCanvasPanel, SetSortChildrenByZOrder, bSortChildrenByZOrder, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerOverlay, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerStackBox, SetOrientation, Orientation, EDreamPanelOrientation, DreamPanelLayoutLocal::CleanOrientation(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerStackBox, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerStackBox, SetSpacing, Spacing, float, DreamPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerWrapBox, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerWrapBox, SetSpacing, Spacing, FVector2D, DreamPanelLayoutLocal::CleanSpacing(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerWrapBox, SetWrapSize, WrapSize, float, DreamPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerWrapBox, SetExplicitWrapSize, bExplicitWrapSize, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerGridPanel, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerGridPanel, SetSpacing, Spacing, FVector2D, DreamPanelLayoutLocal::CleanSpacing(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerUniformGridPanel, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerUniformGridPanel, SetSpacing, Spacing, FVector2D, DreamPanelLayoutLocal::CleanSpacing(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerUniformGridPanel, SetMinDesiredSlotWidth, MinDesiredSlotWidth, float, DreamPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerUniformGridPanel, SetMinDesiredSlotHeight, MinDesiredSlotHeight, float, DreamPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerSizeBox, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerSizeBox, SetOverrideWidth, bOverrideWidth, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerSizeBox, SetWidthOverride, WidthOverride, float, DreamPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerSizeBox, SetOverrideHeight, bOverrideHeight, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerSizeBox, SetHeightOverride, HeightOverride, float, DreamPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerScaleBox, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerScaleBox, SetUserSpecifiedScale, UserSpecifiedScale, float, DreamPanelLayoutLocal::NonNegative(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerScaleBox, SetIgnoreInheritedScale, bIgnoreInheritedScale, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerSafeZone, SetUsePlatformSafeZone, bUsePlatformSafeZone, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerSafeZone, SetPadLeft, bPadLeft, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerSafeZone, SetPadTop, bPadTop, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerSafeZone, SetPadRight, bPadRight, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerSafeZone, SetPadBottom, bPadBottom, bool, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerSafeZone, SetSafePadding, SafePadding, FMargin, DreamPanelLayoutLocal::CleanNonNegativeMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerSafeZone, SetNormalizedSafePadding, NormalizedSafePadding, FMargin, DreamPanelLayoutLocal::CleanNormalizedSafePadding(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerWidgetSwitcher, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerBorder, SetPadding, Padding, FMargin, DreamPanelLayoutLocal::CleanMargin(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerBorder, SetHorizontalAlignment, HorizontalAlignment, EDreamPanelHorizontalAlignment, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerBorder, SetVerticalAlignment, VerticalAlignment, EDreamPanelVerticalAlignment, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerBorder, SetDesiredSizeScale, DesiredSizeScale, FVector2D, DreamPanelLayoutLocal::CleanSize(Value))
LEX_PANEL_SETTER(UDreamLayoutContainerMenuAnchor, SetPlacement, Placement, EDreamMenuPlacement, Value)
LEX_PANEL_SETTER(UDreamLayoutContainerMenuAnchor, SetFitInWindow, bFitInWindow, bool, Value)

#undef LEX_PANEL_SETTER

void UDreamLayoutContainerScaleBox::SetStretch(EDreamScaleBoxStretch Value)
{
	Value = DreamPanelLayoutLocal::CleanStretch(Value);
	const bool bStretchChanged = Stretch != Value;
	Stretch = Value;
	UpdateClippingOverride();
	if (bStretchChanged)
	{
		RequestLayoutRefresh();
	}
}

void UDreamLayoutContainerGridPanel::SetColumnFill(const TArray<float>& Value)
{
	TArray<float> Cleaned = DreamPanelLayoutLocal::CleanFill(Value);
	if (ColumnFill != Cleaned)
	{
		ColumnFill = MoveTemp(Cleaned);
		RequestLayoutRefresh();
	}
}

void UDreamLayoutContainerGridPanel::SetRowFill(const TArray<float>& Value)
{
	TArray<float> Cleaned = DreamPanelLayoutLocal::CleanFill(Value);
	if (RowFill != Cleaned)
	{
		RowFill = MoveTemp(Cleaned);
		RequestLayoutRefresh();
	}
}

void UDreamLayoutContainerSizeBox::SetMinDesiredSize(FVector2D Value)
{
	Value = DreamPanelLayoutLocal::CleanSize(Value);
	bool bChanged = !MinDesiredSize.Equals(Value, 0.0);
	MinDesiredSize = Value;
	FVector2D ReconciledMax = DreamPanelLayoutLocal::CleanSize(MaxDesiredSize);
	if (ReconciledMax.X > 0.0) ReconciledMax.X = FMath::Max(ReconciledMax.X, MinDesiredSize.X);
	if (ReconciledMax.Y > 0.0) ReconciledMax.Y = FMath::Max(ReconciledMax.Y, MinDesiredSize.Y);
	bChanged |= !MaxDesiredSize.Equals(ReconciledMax, 0.0);
	MaxDesiredSize = ReconciledMax;
	if (bChanged) RequestLayoutRefresh();
}

void UDreamLayoutContainerSizeBox::SetMaxDesiredSize(FVector2D Value)
{
	Value = DreamPanelLayoutLocal::CleanSize(Value);
	if (Value.X > 0.0) Value.X = FMath::Max(Value.X, DreamPanelLayoutLocal::NonNegative(MinDesiredSize.X));
	if (Value.Y > 0.0) Value.Y = FMath::Max(Value.Y, DreamPanelLayoutLocal::NonNegative(MinDesiredSize.Y));
	if (!MaxDesiredSize.Equals(Value, 0.0))
	{
		MaxDesiredSize = Value;
		RequestLayoutRefresh();
	}
}

UDreamPanelSlot* UDreamPanelLayoutBase::EnsureSlot(UDreamWidget* Child) const
{
	if (!IsValid(Child))
	{
		return nullptr;
	}
	if (UDreamPanelSlot* ExistingSlot = Child->GetPanelSlot(); IsValid(ExistingSlot))
	{
		ExistingSlot->CaptureAuthoredGeometry();
		return ExistingSlot;
	}
	UDreamPanelSlot* NewSlot = Child->CreateNewPanelSlot<UDreamPanelSlot>();
	if (IsValid(NewSlot))
	{
		if (IsA<UDreamLayoutContainerScaleBox>())
		{
			NewSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Center);
			NewSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
		}
		NewSlot->CaptureAuthoredGeometry();
	}
	return NewSlot;
}

const UDreamPanelSlot* UDreamPanelLayoutBase::GetSlot(const UDreamWidget* Child) const
{
	if (IsValid(Child))
	{
		if (const UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
		{
			return Slot;
		}
	}
	return GetDefault<UDreamPanelSlot>();
}

/**
 * Hand a skipped child back to its authored anchors — but only when it explicitly opted out of layout.
 *
 * bIgnoreLayout means "I position myself", so restoring the authored geometry is what the author asked for.
 * A child that is merely collapsed right now is still owned by this panel and will be laid out again the moment
 * it becomes visible, so its geometry must be left untouched. The authored snapshot is captured at design time
 * (in the designer, at whatever root size that scene had) and can encode a position that is far outside the
 * runtime canvas — restoring it there teleports the whole subtree off-screen, and because the panel skips the
 * child again on the next pass it never comes back.
 */
static void ReleaseSkippedChildGeometry(UDreamWidget* Child)
{
	if (!IsValid(Child) || !Child->GetIgnoreLayout())
	{
		return;
	}
	if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
	{
		Slot->RestoreAuthoredGeometry();
	}
}

TArray<UDreamWidget*> UDreamPanelLayoutBase::CollectLayoutChildren(bool bEnsureSlots) const
{
	TArray<UDreamWidget*> Result;
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return Result;
	}
	for (UDreamWidget* Child : Widget->GetChildren())
	{
		if (!IsValid(Child))
		{
			continue;
		}
		UDreamPanelSlot* ExistingSlot = Child->GetPanelSlot();
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

TMap<UDreamPanelLayoutBase::FDesiredSizeKey, FVector2D> UDreamPanelLayoutBase::DesiredSizeMemo;
int32 UDreamPanelLayoutBase::DesiredSizeMemoDepth = 0;
int64 UDreamPanelLayoutBase::DesiredSizeComputeCount = 0;

UDreamPanelLayoutBase::FDesiredSizeMemoScope::FDesiredSizeMemoScope()
{
	++DesiredSizeMemoDepth;
}

UDreamPanelLayoutBase::FDesiredSizeMemoScope::~FDesiredSizeMemoScope()
{
	if (--DesiredSizeMemoDepth <= 0)
	{
		DesiredSizeMemoDepth = 0;
		DesiredSizeMemo.Reset();
	}
}

void UDreamPanelLayoutBase::ForgetDesiredSize(const UDreamWidget* Widget)
{
	// One widget can hold several entries now, one per constraint it was measured under, so this drops
	// by widget rather than by key.
	for (auto It = DesiredSizeMemo.CreateIterator(); It; ++It)
	{
		if (It.Key().Widget == Widget)
		{
			It.RemoveCurrent();
		}
	}
}

void UDreamPanelLayoutBase::ForgetAllDesiredSizes()
{
	DesiredSizeMemo.Reset();
}

FVector2D UDreamPanelLayoutBase::GetDesiredSize(UDreamWidget* Child) const
{
	return GetDesiredSize(Child, FDreamMeasureSpec::Undefined(), FDreamMeasureSpec::Undefined());
}

FVector2D UDreamPanelLayoutBase::GetDesiredSize(UDreamWidget* Child,
	const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	const FDesiredSizeKey MemoKey{Child, InWidthSpec, InHeightSpec};
	if (DesiredSizeMemoDepth > 0)
	{
		if (const FVector2D* Cached = DesiredSizeMemo.Find(MemoKey))
		{
			return *Cached;
		}
	}
	TFunction<FVector2D(UDreamWidget*, const FDreamMeasureSpec&, const FDreamMeasureSpec&, TSet<const UDreamWidget*>&)> GetIntrinsicSize;
	GetIntrinsicSize = [&GetIntrinsicSize](UDreamWidget* Widget,
		const FDreamMeasureSpec& WidthSpec, const FDreamMeasureSpec& HeightSpec,
		TSet<const UDreamWidget*>& Visited) -> FVector2D
	{
		if (!IsValid(Widget) || Visited.Contains(Widget))
		{
			// No opinion, not zero: a negative axis is ignored by Accumulate, a zero is a CLAIM.
			return FVector2D(-1.0, -1.0);
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

		if (UDreamLayoutSelf* LayoutSelf = Widget->GetLayoutSelf(); IsValid(LayoutSelf)
			&& LayoutSelf->GetClass() != UDreamLayoutSelf::StaticClass())
		{
			const FDreamLayoutControlAnchorData LayoutControl = LayoutSelf->GetLayoutControlAnchor(Widget);
			const FVector2f LayoutDesired = LayoutSelf->GetLayoutPreferredSize(WidthSpec, HeightSpec);
			if (LayoutControl.bCanControlHorizontalSize) SetOverride(Desired.X, bWidthOverridden, LayoutDesired.X);
			if (LayoutControl.bCanControlVerticalSize) SetOverride(Desired.Y, bHeightOverridden, LayoutDesired.Y);
		}
		const bool bHasLayoutContainer = IsValid(Widget->GetLayoutContainer());
		if (UDreamLayoutContainer* LayoutContainer = Widget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			// The constraint reaches the child's own panel here, and this is the hop that matters: a wrap
			// box asked how tall it wants to be now learns how wide it is allowed to be first.
			const FVector2f LayoutDesired = LayoutContainer->GetLayoutPreferredSize(WidthSpec, HeightSpec);
			if (const UDreamLayoutContainerSizeBox* SizeBox = Cast<UDreamLayoutContainerSizeBox>(LayoutContainer))
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
		if (UDreamVisual* Visual = Widget->GetVisual(); IsValid(Visual))
		{
			Accumulate(Desired.X, bWidthOverridden, Visual->GetPreferredWidth());
			Accumulate(Desired.Y, bHeightOverridden, Visual->GetPreferredHeight());
		}
		if (!bHasLayoutContainer)
		{
			for (UDreamWidget* ContentChild : Widget->GetChildren())
			{
				if (IsValid(ContentChild) && ContentChild->GetLayoutVisibleInHierarchy()
					&& !ContentChild->GetIgnoreLayout())
				{
					// A plain widget is not a panel and spends none of the space itself, so its content
					// children inherit the constraint unchanged.
					const FVector2D ContentDesired = GetIntrinsicSize(ContentChild, WidthSpec, HeightSpec, Visited);
					Accumulate(Desired.X, bWidthOverridden, ContentDesired.X);
					Accumulate(Desired.Y, bHeightOverridden, ContentDesired.Y);
				}
			}
		}

		FVector2f AuthoredFallback = FVector2f::ZeroVector;
		bool bHasAuthoredFallback = false;
		if (const UDreamWidget* Parent = Widget->GetParent(); IsValid(Parent)
			&& IsValid(Cast<UDreamPanelLayoutBase>(Parent->GetLayoutContainer())))
		{
			if (const UDreamPanelSlot* Slot = Widget->GetPanelSlot(); IsValid(Slot) && Slot->HasAuthoredGeometry())
			{
				AuthoredFallback = Slot->GetAuthoredDesiredSizeFallback();
				bHasAuthoredFallback = true;
			}
		}
		// Once a panel pass has written this widget's rect, its current width/height are layout OUTPUT.
		// Feeding them back into measurement closes a loop where a squeezed widget measures as squeezed
		// forever — the "column collapsed to zero and never comes back" failure. Measurement therefore
		// prefers the authored snapshot whenever one exists (every slot arranged in-session has one:
		// MarkLayoutGeometryApplied captures it first, and UDreamPanelSlot::OnRegister heals legacy slots
		// by capturing the pre-arrangement rect on load).
		//
		// An axis with neither a claim nor a snapshot stays NEGATIVE here -- raw, not cleaned. An
		// anchor-driven child (no panel above it, so no slot and no snapshot: a progress bar's track
		// and fill) has no opinion, and Accumulate ignores a negative, which is how a subtree with
		// nothing to say stays silent instead of shouting "zero". The bare current-size fallback
		// lives at the measure ROOT only -- inside the recursion it was this comment's loop one
		// level down: the child fed its CURRENT height back into the parent's desired size, one
		// mid-layout zero (a widget's first tick can run before its first arrange) and the parent
		// measured zero forever after.
		if (Desired.X < 0.0 && bHasAuthoredFallback) Desired.X = AuthoredFallback.X;
		if (Desired.Y < 0.0 && bHasAuthoredFallback) Desired.Y = AuthoredFallback.Y;
		return Desired;
	};

	TSet<const UDreamWidget*> Visited;
	++DesiredSizeComputeCount;
	FVector2D Result = GetIntrinsicSize(Child, InWidthSpec, InHeightSpec, Visited);
	// The legacy "applied but never snapshotted" fallback, at the measure root only -- see the
	// comment inside the lambda for why it must not run one level down.
	if (Result.X < 0.0) Result.X = Child->GetWidth();
	if (Result.Y < 0.0) Result.Y = Child->GetHeight();
	Result = DreamPanelLayoutLocal::CleanSize(Result);
	// Per-slot Min/MaxDesiredSize, applied here so that every panel gets them and none has to implement
	// them: this is the one function any panel's measurement goes through. Before the slot carried them,
	// bounding one child of a StackBox or one cell of a grid meant wrapping it in a SizeBox, because
	// SizeBox was the only place the two values existed and it takes a single child.
	if (const UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
	{
		Result = DreamPanelLayoutLocal::CleanSize(Slot->ConstrainDesiredSize(Result));
	}
	// A bounded spec is a CEILING on what the answer may claim, not an instruction to fill it: a child
	// that wants less keeps wanting less. Exactly is the one that overrides, matching FDreamMeasureSpec's
	// own Resolve.
	Result.X = InWidthSpec.Resolve(static_cast<float>(Result.X));
	Result.Y = InHeightSpec.Resolve(static_cast<float>(Result.Y));
	if (DesiredSizeMemoDepth > 0)
	{
		DesiredSizeMemo.Add(MemoKey, Result);
	}
	return Result;
}

void UDreamPanelLayoutBase::ApplyChildRect(UDreamWidget* Child, const FVector2D& Position, const FVector2D& Size, bool bForceFill,
	TOptional<EDreamPanelHorizontalAlignment> InHorizontalOverride,
	TOptional<EDreamPanelVerticalAlignment> InVerticalOverride) const
{
	UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel) || !IsValid(Child))
	{
		return;
	}
	UDreamPanelSlot* Slot = EnsureSlot(Child);
	if (!IsValid(Slot))
	{
		return;
	}
	Slot->MarkLayoutGeometryApplied();

	const FVector2D CleanPosition(
		DreamPanelLayoutLocal::FiniteOrZero(Position.X), DreamPanelLayoutLocal::FiniteOrZero(Position.Y));
	const FVector2D CleanAreaSize = DreamPanelLayoutLocal::CleanSize(Size);
	const FVector2D InnerPosition = CleanPosition + FVector2D(
		DreamPanelLayoutLocal::FiniteOrZero(Slot->Padding.Left), DreamPanelLayoutLocal::FiniteOrZero(Slot->Padding.Top));
	const FVector2D InnerSize(
		FMath::Max(0.0, CleanAreaSize.X - DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)),
		FMath::Max(0.0, CleanAreaSize.Y - DreamPanelLayoutLocal::VerticalPadding(Slot->Padding)));
	// The inner box is what the child is actually being offered, so that is the constraint it is measured
	// under. This reaches every panel at once -- Overlay, SizeBox, SafeZone, WidgetSwitcher and the grids
	// all place through here -- and is what makes a non-Fill wrap box wrap against the room it is getting
	// instead of against nothing at all.
	const FVector2D Desired = DreamPanelLayoutLocal::CleanSize(GetDesiredSize(Child,
		FDreamMeasureSpec::AtMost(static_cast<float>(InnerSize.X)),
		FDreamMeasureSpec::AtMost(static_cast<float>(InnerSize.Y))));

	double Width = InnerSize.X;
	double Height = InnerSize.Y;
	double Left = InnerPosition.X;
	double Top = InnerPosition.Y;
	const EDreamPanelHorizontalAlignment HorizontalAlignment = InHorizontalOverride.Get(Slot->HorizontalAlignment);
	const EDreamPanelVerticalAlignment VerticalAlignment = InVerticalOverride.Get(Slot->VerticalAlignment);
	if (!bForceFill && HorizontalAlignment != EDreamPanelHorizontalAlignment::Fill)
	{
		Width = FMath::Min(Desired.X, InnerSize.X);
		switch (HorizontalAlignment)
		{
		case EDreamPanelHorizontalAlignment::Center: Left += (InnerSize.X - Width) * 0.5; break;
		case EDreamPanelHorizontalAlignment::Right: Left += InnerSize.X - Width; break;
		default: break;
		}
	}
	if (!bForceFill && VerticalAlignment != EDreamPanelVerticalAlignment::Fill)
	{
		Height = FMath::Min(Desired.Y, InnerSize.Y);
		switch (VerticalAlignment)
		{
		case EDreamPanelVerticalAlignment::Center: Top += (InnerSize.Y - Height) * 0.5; break;
		case EDreamPanelVerticalAlignment::Bottom: Top += InnerSize.Y - Height; break;
		default: break;
		}
	}

	const FVector2f FinalSize(static_cast<float>(FMath::Max(0.0, Width)), static_cast<float>(FMath::Max(0.0, Height)));
	const FVector2D Pivot = Child->GetPivot();
	const float PanelWidth = DreamPanelLayoutLocal::NonNegative(Panel->GetWidth());
	const float PanelHeight = DreamPanelLayoutLocal::NonNegative(Panel->GetHeight());

	FDreamPanelChildRect Rect;
	Rect.Child = Child;
	Rect.Size = FinalSize;
	Rect.AnchoredPosition = FVector2D(
		-PanelWidth * 0.5 + Left + FinalSize.X * Pivot.X,
		PanelHeight * 0.5 - Top - FinalSize.Y * (1.0 - Pivot.Y));
	RecordChildRect(Rect);
}

void UDreamPanelLayoutBase::RecordChildRect(const FDreamPanelChildRect& Rect) const
{
	if (!IsValid(Rect.Child))
	{
		return;
	}
	// Inside an arrange pass nothing is written yet; the base commits the whole fragment afterwards.
	if (RecordingFragment)
	{
		RecordingFragment->Children.Add(Rect);
		return;
	}
	// Outside one - a panel driven directly, or a subclass reaching in - keep the old immediate write so
	// the two paths cannot disagree about what a recorded rect means.
	FDreamFragment Immediate;
	Immediate.Children.Add(Rect);
	CommitFragment(Immediate);
}

void UDreamPanelLayoutBase::CommitFragment(const FDreamFragment& Fragment) const
{
	UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel) || Fragment.Children.IsEmpty())
	{
		return;
	}
	// The panel's whole result reaches the tree here, once, under a single scope.
	//
	// Deferring the write does NOT make the scope unnecessary, which was worth measuring rather than
	// assuming: removing it takes every pass-count test in the suite from 1 to 2. The setters early-out
	// when the value is unchanged, so the invalidation they raise is always a real change - the commit
	// genuinely is one. What the scope encodes is that this particular real change is layout output
	// rather than authored intent. Blink needs no equivalent only because its LayoutObject does not hold
	// the geometry, so there is no setter to fire; here the geometry lives on the widget and the
	// distinction has to be made explicitly.
	UDreamWidget::FLayoutWriteScope WriteScope(Panel);
	for (const FDreamPanelChildRect& Rect : Fragment.Children)
	{
		UDreamWidget* Child = Rect.Child;
		if (!IsValid(Child))
		{
			continue;
		}
		if (Rect.bCollapseAnchors)
		{
			Child->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5), FVector2D(0.5), true, true);
		}
		Child->SetWidth(Rect.Size.X);
		Child->SetHeight(Rect.Size.Y);
		if (Rect.bApplyScale)
		{
			Child->SetLayoutScale(Rect.LayoutScale);
		}
		if (!Rect.bSizeOnly)
		{
			Child->SetAnchoredPosition(Rect.AnchoredPosition);
		}
	}
}

void UDreamPanelLayoutBase::CalculateLayout()
{
	const bool bTrace = CVarDreamLayoutTrace.GetValueOnAnyThread() != 0;
	if (!BeginLayoutPass())
	{
		if (bTrace)
		{
			UE_LOG(DreamGUI, Log, TEXT("[LayoutTrace] %s (%s): gate closed, not dirty"),
				*GetNameSafe(GetWidget()), *GetClass()->GetName());
		}
		return;
	}
	if (bTrace)
	{
		UDreamWidget* Panel = GetWidget();
		UE_LOG(DreamGUI, Log, TEXT("[LayoutTrace] %s (%s): arranging, panel size %s, %d children"),
			*GetNameSafe(Panel), *GetClass()->GetName(),
			*(IsValid(Panel) ? Panel->GetSize().ToString() : FString(TEXT("<none>"))),
			IsValid(Panel) ? Panel->GetChildrenCount() : 0);
	}
	const FDreamFragment Fragment = Arrange();
	if (bTrace)
	{
		UE_LOG(DreamGUI, Log, TEXT("[LayoutTrace]   fragment has %d child rects"), Fragment.Children.Num());
		for (const FDreamPanelChildRect& Rect : Fragment.Children)
		{
			UE_LOG(DreamGUI, Log, TEXT("[LayoutTrace]     %s -> size %s pos %s"),
				*GetNameSafe(Rect.Child), *Rect.Size.ToString(), *Rect.AnchoredPosition.ToString());
		}
	}
	CommitFragment(Fragment);
}

FDreamFragment UDreamPanelLayoutBase::Arrange()
{
	FDreamFragment Fragment;
	{
		// One arrange asks for the same child's desired size four times over, and each ask re-measures the
		// whole subtree beneath it. Safe to memoise precisely because the pass writes nothing.
		FDesiredSizeMemoScope Memo;
		TGuardValue<FDreamFragment*> Recording(RecordingFragment, &Fragment);
		ArrangeChildren();
	}
	Fragment.Size = PreferredSize;
	return Fragment;
}

bool UDreamPanelLayoutBase::BeginLayoutPass()
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

FVector2f UDreamPanelLayoutBase::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	return FVector2f::ZeroVector;
}

FVector2f UDreamPanelLayoutBase::GetLayoutPreferredSize() const
{
	return GetLayoutPreferredSize(FDreamMeasureSpec::Undefined(), FDreamMeasureSpec::Undefined());
}

FVector2f UDreamPanelLayoutBase::GetLayoutPreferredSize(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	FDesiredSizeMemoScope Memo;
	const FVector2f Result = MeasureLayout(InWidthSpec, InHeightSpec);
	return FVector2f(DreamPanelLayoutLocal::NonNegative(Result.X), DreamPanelLayoutLocal::NonNegative(Result.Y));
}

bool UDreamPanelLayoutBase::GetLayoutDebugInfo(const UDreamWidget* TargetWidget, FDreamLayoutDebugInfo& OutInfo) const
{
	UDreamWidget* Panel = GetWidget();
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
		OutInfo.DesiredSize = FVector2D(MeasureUnconstrained());
		OutInfo.Algorithm = FString::Printf(TEXT("UMG Compatible / %s"), *DreamLayoutDebugClassLabel(GetClass()));
		OutInfo.SlotRule = TEXT("Panel root");
		return true;
	}
	if (!Panel->GetChildren().Contains(const_cast<UDreamWidget*>(TargetWidget)))
	{
		return false;
	}

	const UDreamPanelSlot* Slot = GetSlot(TargetWidget);
	OutInfo = FDreamLayoutDebugInfo();
	OutInfo.Widget = const_cast<UDreamWidget*>(TargetWidget);
	OutInfo.DesiredSize = GetDesiredSize(const_cast<UDreamWidget*>(TargetWidget));
	OutInfo.ArrangedPosition = TargetWidget->GetAnchoredPosition();
	OutInfo.ArrangedSize = TargetWidget->GetSize();
	OutInfo.AuthoredSize = Slot->HasAuthoredGeometry()
		? FVector2D(Slot->GetAuthoredDesiredSizeFallback()) : OutInfo.ArrangedSize;
	OutInfo.ContentBounds = Panel->GetSize();
	OutInfo.Algorithm = FString::Printf(TEXT("UMG Compatible / %s"), *DreamLayoutDebugClassLabel(GetClass()));

	auto EnumDisplayName = [](const UEnum* Enum, int64 Value)
	{
		return Enum ? Enum->GetDisplayNameTextByValue(Value).ToString() : FString(TEXT("Unknown"));
	};
	const FString SizeRule = EnumDisplayName(StaticEnum<EDreamPanelSizeRule>(), static_cast<int64>(Slot->SizeRule));
	const FString Horizontal = EnumDisplayName(StaticEnum<EDreamPanelHorizontalAlignment>(), static_cast<int64>(Slot->HorizontalAlignment));
	const FString Vertical = EnumDisplayName(StaticEnum<EDreamPanelVerticalAlignment>(), static_cast<int64>(Slot->VerticalAlignment));
	OutInfo.SlotRule = FString::Printf(TEXT("%s %.2f | H:%s V:%s | Pad %.0f,%.0f,%.0f,%.0f"),
		*SizeRule, Slot->FillWeight, *Horizontal, *Vertical,
		Slot->Padding.Left, Slot->Padding.Top, Slot->Padding.Right, Slot->Padding.Bottom);

	const FDreamLayoutControlAnchorData Control = GetLayoutControlAnchor(TargetWidget);
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
	if (const UEnum* ClippingEnum = StaticEnum<EDreamWidgetClipping>())
	{
		OutInfo.Clipping = ClippingEnum->GetDisplayNameTextByValue(static_cast<int64>(TargetWidget->GetClipping())).ToString();
	}
	return true;
}

void UDreamPanelLayoutBase::OnUnregister()
{
	if (UDreamWidget* Panel = GetWidget(); IsValid(Panel))
	{
		for (UDreamWidget* Child : Panel->GetChildren())
		{
			if (IsValid(Child))
			{
				if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
				{
					Slot->RestoreAuthoredGeometry();
				}
				// Layout-visibility suppression is this panel's opinion about its children, so it has to
				// go with the panel. WidgetSwitcher carried its own override to do exactly this; the
				// single-content panels (SizeBox, ScaleBox, SafeZone) now suppress their surplus children
				// too, and would otherwise leave them invisible with nothing left to un-suppress them.
				Child->SetLayoutVisibilitySuppressed(false);
			}
		}
	}
	Super::OnUnregister();
}

FDreamLayoutControlAnchorData UDreamPanelLayoutBase::GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const
{
	FDreamLayoutControlAnchorData Result;
	UDreamWidget* Panel = GetWidget();
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

void UDreamPanelLayoutBase::RequestLayoutRefresh()
{
	UDreamWidget::MarkLayoutForRebuild(GetWidget());
}

#if WITH_EDITOR
namespace
{
	void SanitizePanelLayoutProperties(UDreamPanelLayoutBase* Layout)
	{
		if (UDreamLayoutContainerCanvasPanel* Panel = Cast<UDreamLayoutContainerCanvasPanel>(Layout))
		{
			Panel->SetSortChildrenByZOrder(Panel->bSortChildrenByZOrder);
		}
		if (UDreamLayoutContainerOverlay* Panel = Cast<UDreamLayoutContainerOverlay>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
		}
		if (UDreamLayoutContainerStackBox* Panel = Cast<UDreamLayoutContainerStackBox>(Layout))
		{
			Panel->SetOrientation(Panel->Orientation);
			Panel->SetPadding(Panel->Padding);
			Panel->SetSpacing(Panel->Spacing);
		}
		if (UDreamLayoutContainerWrapBox* Panel = Cast<UDreamLayoutContainerWrapBox>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetSpacing(Panel->Spacing);
			Panel->SetWrapSize(Panel->WrapSize);
			Panel->SetExplicitWrapSize(Panel->bExplicitWrapSize);
		}
		if (UDreamLayoutContainerGridPanel* Panel = Cast<UDreamLayoutContainerGridPanel>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetSpacing(Panel->Spacing);
			Panel->SetColumnFill(Panel->ColumnFill);
			Panel->SetRowFill(Panel->RowFill);
		}
		if (UDreamLayoutContainerUniformGridPanel* Panel = Cast<UDreamLayoutContainerUniformGridPanel>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetSpacing(Panel->Spacing);
			Panel->SetMinDesiredSlotWidth(Panel->MinDesiredSlotWidth);
			Panel->SetMinDesiredSlotHeight(Panel->MinDesiredSlotHeight);
		}
		if (UDreamLayoutContainerSizeBox* Panel = Cast<UDreamLayoutContainerSizeBox>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetOverrideWidth(Panel->bOverrideWidth);
			Panel->SetWidthOverride(Panel->WidthOverride);
			Panel->SetOverrideHeight(Panel->bOverrideHeight);
			Panel->SetHeightOverride(Panel->HeightOverride);
			Panel->SetMinDesiredSize(Panel->MinDesiredSize);
			Panel->SetMaxDesiredSize(Panel->MaxDesiredSize);
		}
		if (UDreamLayoutContainerScaleBox* Panel = Cast<UDreamLayoutContainerScaleBox>(Layout))
		{
			Panel->SetPadding(Panel->Padding);
			Panel->SetStretch(Panel->Stretch);
			Panel->SetUserSpecifiedScale(Panel->UserSpecifiedScale);
			Panel->SetIgnoreInheritedScale(Panel->bIgnoreInheritedScale);
		}
		if (UDreamLayoutContainerSafeZone* Panel = Cast<UDreamLayoutContainerSafeZone>(Layout))
		{
			Panel->SetUsePlatformSafeZone(Panel->bUsePlatformSafeZone);
			Panel->SetPadLeft(Panel->bPadLeft);
			Panel->SetPadTop(Panel->bPadTop);
			Panel->SetPadRight(Panel->bPadRight);
			Panel->SetPadBottom(Panel->bPadBottom);
			Panel->SetSafePadding(Panel->SafePadding);
			Panel->SetNormalizedSafePadding(Panel->NormalizedSafePadding);
		}
		if (UDreamLayoutContainerWidgetSwitcher* Panel = Cast<UDreamLayoutContainerWidgetSwitcher>(Layout))
		{
			Panel->SetActiveWidgetIndex(Panel->ActiveWidgetIndex);
			Panel->SetPadding(Panel->Padding);
		}
	}
}

void UDreamPanelLayoutBase::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SanitizePanelLayoutProperties(this);
}

void UDreamPanelLayoutBase::PostEditUndo()
{
	Super::PostEditUndo();
	SanitizePanelLayoutProperties(this);
	RequestLayoutRefresh();
}
#endif

/**
 * Specs are ignored here, and that is the answer rather than an omission: a canvas child states its own
 * rect through its anchors, so how much room the canvas itself was offered changes nothing about where
 * the children sit or how big they are.
 */
FVector2f UDreamLayoutContainerCanvasPanel::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	FVector2f Result = FVector2f::ZeroVector;
	for (UDreamWidget* Child : CollectLayoutChildren(false))
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const FVector2D Size = Slot->bAutoSize
			? GetDesiredSize(Child)
			: (Slot->HasLayoutGeometryApplied() && Slot->HasAuthoredGeometry()
				? FVector2D(Slot->GetAuthoredDesiredSizeFallback())
				: DreamPanelLayoutLocal::CleanSize(Child->GetSize()));
		const FVector2D AnchorMin = Child->GetAnchorMin();
		const FVector2D AnchorMax = Child->GetAnchorMax();
		const bool bDockedHorizontally = AnchorMin.X == AnchorMax.X && (AnchorMin.X == 0.0 || AnchorMin.X == 1.0);
		const bool bDockedVertically = AnchorMin.Y == AnchorMax.Y && (AnchorMin.Y == 0.0 || AnchorMin.Y == 1.0);
		Result.X = FMath::Max(Result.X, static_cast<float>(Size.X + (bDockedHorizontally ? FMath::Abs(Child->GetAnchorOffsetLeft()) : 0.0)));
		Result.Y = FMath::Max(Result.Y, static_cast<float>(Size.Y + (bDockedVertically ? FMath::Abs(Child->GetAnchorOffsetTop()) : 0.0)));
	}
	return Result;
}

FDreamLayoutControlAnchorData UDreamLayoutContainerCanvasPanel::GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const
{
	FDreamLayoutControlAnchorData Result;
	UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel) || !IsValid(TargetWidget) || !Panel->GetChildren().Contains(TargetWidget))
	{
		return Result;
	}
	if (TargetWidget->GetIgnoreLayout())
	{
		return Result;
	}
	const UDreamPanelSlot* Slot = GetSlot(TargetWidget);
	Result.bCanControlHorizontalSize = Slot->bAutoSize;
	Result.bCanControlVerticalSize = Slot->bAutoSize;
	return Result;
}

void UDreamLayoutContainerCanvasPanel::ArrangeChildren()
{
	TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren();
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		if (Slot->bAutoSize)
		{
			UDreamPanelSlot* MutableSlot = Child->GetPanelSlot();
			if (IsValid(MutableSlot))
			{
				MutableSlot->MarkLayoutGeometryApplied(false, false, true, true);
			}
			const FVector2D Desired = GetDesiredSize(Child);
			// A canvas child keeps the anchored position its own anchor data produced; only the size is
			// the panel's to decide, so this records a size-only rect rather than going through
			// ApplyChildRect, which would also collapse the anchors and place it.
			FDreamPanelChildRect Rect;
			Rect.Child = Child;
			Rect.Size = FVector2f(Desired);
			Rect.bCollapseAnchors = false;
			Rect.bSizeOnly = true;
			RecordChildRect(Rect);
		}
		else if (UDreamPanelSlot* MutableSlot = Child->GetPanelSlot(); IsValid(MutableSlot))
		{
			// Both branches write the child's rect from inside the arrange, so whatever the memo holds for
			// it is stale from here on. This is the one panel that writes outside the fragment.
			if (MutableSlot->HasLayoutGeometryApplied())
			{
				MutableSlot->RestoreAuthoredGeometry();
			}
			else
			{
				MutableSlot->CaptureAuthoredGeometry(true);
			}
			ForgetDesiredSize(Child);
		}
	}
	if (bSortChildrenByZOrder)
	{
		DreamPanelLayoutLocal::ApplyStableZOrderWithinParticipatingSlots(GetWidget(), LayoutChildren);
	}
	PreferredSize = MeasureUnconstrained();
}

FVector2f UDreamLayoutContainerOverlay::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	// Every child gets the whole content box, so every child is offered the same space this panel was,
	// less this panel's own padding and that child's slot padding.
	const FDreamMeasureSpec ContentWidth = InWidthSpec.ForChild(DreamPanelLayoutLocal::HorizontalPadding(Padding));
	const FDreamMeasureSpec ContentHeight = InHeightSpec.ForChild(DreamPanelLayoutLocal::VerticalPadding(Padding));
	FVector2f Result = FVector2f::ZeroVector;
	for (UDreamWidget* Child : CollectLayoutChildren(false))
	{
		const UDreamPanelSlot* ChildSlot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child,
			ContentWidth.ForChild(DreamPanelLayoutLocal::HorizontalPadding(ChildSlot->Padding)),
			ContentHeight.ForChild(DreamPanelLayoutLocal::VerticalPadding(ChildSlot->Padding)));
		const UDreamPanelSlot* Slot = GetSlot(Child);
		Result.X = FMath::Max(Result.X, static_cast<float>(Desired.X + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)));
		Result.Y = FMath::Max(Result.Y, static_cast<float>(Desired.Y + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding)));
	}
	Result.X += DreamPanelLayoutLocal::HorizontalPadding(Padding);
	Result.Y += DreamPanelLayoutLocal::VerticalPadding(Padding);
	return Result;
}

void UDreamLayoutContainerOverlay::ArrangeChildren()
{
	UDreamWidget* Panel = GetWidget();
	const FVector2D AreaPosition(DreamPanelLayoutLocal::FiniteOrZero(Padding.Left), DreamPanelLayoutLocal::FiniteOrZero(Padding.Top));
	const FVector2D AreaSize(
		FMath::Max(0.0f, Panel->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding)),
		FMath::Max(0.0f, Panel->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding)));
	TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren();
	// Paint order is the whole point of an overlay: honor the slot ZOrder the same stable way
	// CanvasPanel (opt-in) and GridPanel do. Equal ZOrder — the default — keeps sibling order
	// untouched, so this only reorders children whose ZOrder was actually authored. Before this,
	// ZOrder on overlay children was silently ignored and stacking followed sibling order alone.
	DreamPanelLayoutLocal::ApplyStableZOrderWithinParticipatingSlots(Panel, LayoutChildren);
	for (UDreamWidget* Child : LayoutChildren)
	{
		ApplyChildRect(Child, AreaPosition, AreaSize);
	}
	PreferredSize = MeasureUnconstrained();
}

FVector2f UDreamLayoutContainerStackBox::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	const TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren(false);
	const bool bHorizontal = Orientation == EDreamPanelOrientation::Horizontal;
	const float Gap = DreamPanelLayoutLocal::NonNegative(Spacing);
	// The CROSS axis is the one a stack can constrain: every child gets the whole of it, less padding.
	// The MAIN axis is not constrained per child -- the children share it by accumulating, and telling
	// each one it may have the whole thing would be a lie every child after the first pays for.
	const FDreamMeasureSpec CrossSpec = (bHorizontal ? InHeightSpec : InWidthSpec).ForChild(
		bHorizontal ? DreamPanelLayoutLocal::VerticalPadding(Padding) : DreamPanelLayoutLocal::HorizontalPadding(Padding));
	float Primary = Gap * FMath::Max(0, LayoutChildren.Num() - 1);
	float Secondary = 0.0f;
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* ChildSlot = GetSlot(Child);
		const FDreamMeasureSpec ChildCross = CrossSpec.ForChild(bHorizontal
			? DreamPanelLayoutLocal::VerticalPadding(ChildSlot->Padding)
			: DreamPanelLayoutLocal::HorizontalPadding(ChildSlot->Padding));
		const FVector2D Desired = bHorizontal
			? GetDesiredSize(Child, FDreamMeasureSpec::Undefined(), ChildCross)
			: GetDesiredSize(Child, ChildCross, FDreamMeasureSpec::Undefined());
		const UDreamPanelSlot* Slot = GetSlot(Child);
		Primary += static_cast<float>(bHorizontal ? Desired.X : Desired.Y)
			+ (bHorizontal ? DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding) : DreamPanelLayoutLocal::VerticalPadding(Slot->Padding));
		Secondary = FMath::Max(Secondary, static_cast<float>(bHorizontal ? Desired.Y : Desired.X)
			+ (bHorizontal ? DreamPanelLayoutLocal::VerticalPadding(Slot->Padding) : DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)));
	}
	return bHorizontal
		? FVector2f(Primary + DreamPanelLayoutLocal::HorizontalPadding(Padding), Secondary + DreamPanelLayoutLocal::VerticalPadding(Padding))
		: FVector2f(Secondary + DreamPanelLayoutLocal::HorizontalPadding(Padding), Primary + DreamPanelLayoutLocal::VerticalPadding(Padding));
}

void UDreamLayoutContainerStackBox::ArrangeChildren()
{
	UDreamWidget* Panel = GetWidget();
	const TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren();
	const bool bHorizontal = Orientation == EDreamPanelOrientation::Horizontal;
	const float Gap = DreamPanelLayoutLocal::NonNegative(Spacing);
	const float AvailablePrimary = bHorizontal
		? FMath::Max(0.0f, Panel->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding))
		: FMath::Max(0.0f, Panel->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding));
	const float AvailableSecondary = bHorizontal
		? FMath::Max(0.0f, Panel->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding))
		: FMath::Max(0.0f, Panel->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding));
	// Where the spec chain STARTS. An arrange is the one moment a panel legitimately knows a real
	// number -- its parent has already given it a rect -- so this is where a constraint can be minted
	// rather than inherited. Everything a child is asked from here down is asked inside it, which is what
	// stops a wrap box answering "how tall am I" against the width it had on the previous pass.
	const FDreamMeasureSpec CrossSpec = FDreamMeasureSpec::AtMost(AvailableSecondary);
	auto ChildCrossSpec = [&](const UDreamPanelSlot* InSlot)
	{
		return CrossSpec.ForChild(bHorizontal
			? DreamPanelLayoutLocal::VerticalPadding(InSlot->Padding)
			: DreamPanelLayoutLocal::HorizontalPadding(InSlot->Padding));
	};
	auto MeasureChild = [&](UDreamWidget* InChild, const UDreamPanelSlot* InSlot)
	{
		const FDreamMeasureSpec Cross = ChildCrossSpec(InSlot);
		return bHorizontal
			? GetDesiredSize(InChild, FDreamMeasureSpec::Undefined(), Cross)
			: GetDesiredSize(InChild, Cross, FDreamMeasureSpec::Undefined());
	};

	float FixedPrimary = Gap * FMath::Max(0, LayoutChildren.Num() - 1);
	float FillWeight = 0.0f;
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = MeasureChild(Child, Slot);
		const float SlotPadding = bHorizontal
			? DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)
			: DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
		FixedPrimary += SlotPadding;
		if (Slot->SizeRule == EDreamPanelSizeRule::Fill)
		{
			FillWeight += DreamPanelLayoutLocal::NonNegative(Slot->FillWeight);
		}
		else
		{
			FixedPrimary += static_cast<float>(bHorizontal ? Desired.X : Desired.Y);
		}
	}

	const float FillContentSpace = FMath::Max(0.0f, AvailablePrimary - FixedPrimary);
	float Cursor = bHorizontal ? DreamPanelLayoutLocal::FiniteOrZero(Padding.Left) : DreamPanelLayoutLocal::FiniteOrZero(Padding.Top);
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = MeasureChild(Child, Slot);
		const float PrimaryPadding = bHorizontal
			? DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)
			: DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
		const float ContentPrimary = Slot->SizeRule == EDreamPanelSizeRule::Fill
			? (FillWeight > UE_SMALL_NUMBER ? FillContentSpace * DreamPanelLayoutLocal::NonNegative(Slot->FillWeight) / FillWeight : 0.0f)
			: static_cast<float>(bHorizontal ? Desired.X : Desired.Y);
		const float SlotPrimary = FMath::Max(0.0f, ContentPrimary + PrimaryPadding);
		if (bHorizontal)
		{
			ApplyChildRect(Child, FVector2D(Cursor, DreamPanelLayoutLocal::FiniteOrZero(Padding.Top)), FVector2D(SlotPrimary, AvailableSecondary));
		}
		else
		{
			ApplyChildRect(Child, FVector2D(DreamPanelLayoutLocal::FiniteOrZero(Padding.Left), Cursor), FVector2D(AvailableSecondary, SlotPrimary));
		}
		Cursor += SlotPrimary + Gap;
	}
	PreferredSize = MeasureUnconstrained();
}

UDreamLayoutContainerHorizontalBox::UDreamLayoutContainerHorizontalBox()
{
	Orientation = EDreamPanelOrientation::Horizontal;
}

UDreamLayoutContainerVerticalBox::UDreamLayoutContainerVerticalBox()
{
	Orientation = EDreamPanelOrientation::Vertical;
}

/**
 * The panel the whole spec mechanism exists for.
 *
 * How tall a wrap box is is a function of how wide it is allowed to be, so "how big do you want to be"
 * has no answer on its own. This used to answer by reading GetWidget()->GetWidth() -- the width its
 * parent gave it on the PREVIOUS pass. A wrap box newly placed in a vertical box therefore counted its
 * rows against its authored width, the box allocated height for that row count, and only the second
 * pass agreed with itself; MaxLayoutPassesPerFrame existed partly to absorb exactly this.
 *
 * Now the width arrives as InWidthSpec. The own-width read survives only as the ROOT fallback, for an
 * ask that genuinely carries no constraint (the editor's layout diagnostics, a measure with nobody
 * above it) -- and an explicit WrapSize still outranks both, because that is an author saying the line
 * width outright.
 */
FVector2f UDreamLayoutContainerWrapBox::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	const float GapX = DreamPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = DreamPanelLayoutLocal::NonNegative(Spacing.Y);
	const float OwnWidthFallback = IsValid(GetWidget())
		? FMath::Max(0.0f, GetWidget()->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding))
		: 0.0f;
	const float AvailableWidth = bExplicitWrapSize
		? DreamPanelLayoutLocal::NonNegative(WrapSize)
		: InWidthSpec.ForChild(DreamPanelLayoutLocal::HorizontalPadding(Padding)).ResolveAvailable(OwnWidthFallback);
	// Children are measured inside the line, so a child that is itself a wrap box wraps against the room
	// this one has rather than against its own last width.
	const FDreamMeasureSpec LineSpec = FDreamMeasureSpec::AtMost(AvailableWidth);
	float X = 0.0f;
	float Y = 0.0f;
	float LineHeight = 0.0f;
	float MaxWidth = 0.0f;
	bool bBreakBeforeNext = false;
	for (UDreamWidget* Child : CollectLayoutChildren(false))
	{
		const UDreamPanelSlot* ChildSlot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child,
			LineSpec.ForChild(DreamPanelLayoutLocal::HorizontalPadding(ChildSlot->Padding)),
			FDreamMeasureSpec::Undefined());
		const UDreamPanelSlot* Slot = GetSlot(Child);
		float ItemWidth = static_cast<float>(Desired.X) + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		const float ItemHeight = static_cast<float>(Desired.Y) + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
		// The twin of the rule in ArrangeChildren: a slot under its FillSpanWhenLessThan threshold gets
		// a line to itself, which is a line-COUNT change and therefore has to be visible to measurement.
		const bool bWantsWholeLine = Slot->FillSpanWhenLessThan > 0.0f && AvailableWidth < Slot->FillSpanWhenLessThan;
		if (X > 0.0f && (bBreakBeforeNext || bWantsWholeLine || X + ItemWidth > AvailableWidth))
		{
			X = 0.0f;
			Y += LineHeight + GapY;
			LineHeight = 0.0f;
		}
		bBreakBeforeNext = bWantsWholeLine;
		if (bWantsWholeLine)
		{
			ItemWidth = FMath::Max(ItemWidth, AvailableWidth);
		}
		MaxWidth = FMath::Max(MaxWidth, X + ItemWidth);
		X += ItemWidth + GapX;
		LineHeight = FMath::Max(LineHeight, ItemHeight);
	}
	return FVector2f(MaxWidth + DreamPanelLayoutLocal::HorizontalPadding(Padding),
		Y + LineHeight + DreamPanelLayoutLocal::VerticalPadding(Padding));
}

void UDreamLayoutContainerWrapBox::ArrangeChildren()
{
	struct FWrapItem
	{
		UDreamWidget* Widget = nullptr;
		float Width = 0.0f;
		float Height = 0.0f;
		bool bFillEmptySpace = false;
	};
	struct FWrapLine
	{
		TArray<FWrapItem> Items;
		float Height = 0.0f;
	};

	const float GapX = DreamPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = DreamPanelLayoutLocal::NonNegative(Spacing.Y);
	const float AvailableWidth = bExplicitWrapSize
		? DreamPanelLayoutLocal::NonNegative(WrapSize)
		: FMath::Max(0.0f, GetWidget()->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding));
	// An arrange knows the real line width, so this is a minted constraint, not an inherited one.
	const FDreamMeasureSpec LineSpec = FDreamMeasureSpec::AtMost(AvailableWidth);
	TArray<FWrapLine> Lines;
	FWrapLine CurrentLine;
	float CurrentWidth = 0.0f;
	bool bBreakBeforeNext = false;
	for (UDreamWidget* Child : CollectLayoutChildren())
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child,
			LineSpec.ForChild(DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)),
			FDreamMeasureSpec::Undefined());
		FWrapItem Item;
		Item.Widget = Child;
		Item.Width = static_cast<float>(Desired.X) + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		Item.Height = static_cast<float>(Desired.Y) + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
		Item.bFillEmptySpace = Slot->bFillEmptySpace;
		// UWrapBoxSlot::FillSpanWhenLessThan: below this wrap width the child stops sharing a line.
		// Carried as "break before, and break after" rather than by placing it directly, so that the
		// twin rule in MeasureLayout can be written the same way and the two cannot drift.
		const bool bWantsWholeLine = Slot->FillSpanWhenLessThan > 0.0f && AvailableWidth < Slot->FillSpanWhenLessThan;
		const float RequiredWidth = CurrentLine.Items.IsEmpty() ? Item.Width : CurrentWidth + GapX + Item.Width;
		if (!CurrentLine.Items.IsEmpty() && (bBreakBeforeNext || bWantsWholeLine || RequiredWidth > AvailableWidth))
		{
			Lines.Add(MoveTemp(CurrentLine));
			CurrentLine = FWrapLine();
			CurrentWidth = 0.0f;
		}
		bBreakBeforeNext = bWantsWholeLine;
		if (bWantsWholeLine)
		{
			// A child on a line of its own takes the whole span, which is what "fill" means here.
			Item.Width = FMath::Max(Item.Width, AvailableWidth);
		}
		if (!CurrentLine.Items.IsEmpty()) CurrentWidth += GapX;
		CurrentWidth += Item.Width;
		CurrentLine.Height = FMath::Max(CurrentLine.Height, Item.Height);
		CurrentLine.Items.Add(Item);
	}
	if (!CurrentLine.Items.IsEmpty()) Lines.Add(MoveTemp(CurrentLine));

	float Y = DreamPanelLayoutLocal::FiniteOrZero(Padding.Top);
	for (const FWrapLine& Line : Lines)
	{
		// UWrapBoxSlot::bFillEmptySpace: the room the line did not use is split evenly between the
		// children on it that asked for it. Nothing asks by default, so a wrap box with no configured
		// slots lays out exactly as it did before.
		float LineUsed = GapX * FMath::Max(0, Line.Items.Num() - 1);
		int32 FillCount = 0;
		for (const FWrapItem& Item : Line.Items)
		{
			LineUsed += Item.Width;
			if (Item.bFillEmptySpace) ++FillCount;
		}
		const float FillShare = FillCount > 0
			? FMath::Max(0.0f, AvailableWidth - LineUsed) / static_cast<float>(FillCount)
			: 0.0f;
		float X = DreamPanelLayoutLocal::FiniteOrZero(Padding.Left);
		for (const FWrapItem& Item : Line.Items)
		{
			const float ItemWidth = Item.bFillEmptySpace ? Item.Width + FillShare : Item.Width;
			ApplyChildRect(Item.Widget, FVector2D(X, Y), FVector2D(ItemWidth, Line.Height));
			X += ItemWidth + GapX;
		}
		Y += Line.Height + GapY;
	}
	PreferredSize = MeasureUnconstrained();
}

/**
 * Specs are not forwarded to children here, and that is deliberate rather than unfinished: a grid's
 * track sizes are DERIVED from what the children want, so handing a child a share of the grid's own
 * space as its constraint would close the loop the whole mechanism exists to open. The grid's answer is
 * its natural size; whoever asked is free to give it less and let ArrangeTracks compress the tracks.
 */
FVector2f UDreamLayoutContainerGridPanel::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	const TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren(false);
	int32 ColumnCount = FMath::Min(ColumnFill.Num(), DreamPanelLayoutLocal::MaxGridTrackCount);
	int32 RowCount = FMath::Min(RowFill.Num(), DreamPanelLayoutLocal::MaxGridTrackCount);
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		ColumnCount = FMath::Max(ColumnCount, DreamPanelLayoutLocal::GridTrackEnd(Slot->Column, Slot->ColumnSpan));
		RowCount = FMath::Max(RowCount, DreamPanelLayoutLocal::GridTrackEnd(Slot->Row, Slot->RowSpan));
	}
	TArray<float> Columns;
	TArray<float> Rows;
	Columns.Init(0.0f, ColumnCount);
	Rows.Init(0.0f, RowCount);
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child);
		DreamPanelLayoutLocal::AddSpanRequirement(Columns, Slot->Column, Slot->ColumnSpan,
			static_cast<float>(Desired.X) + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding), Spacing.X);
		DreamPanelLayoutLocal::AddSpanRequirement(Rows, Slot->Row, Slot->RowSpan,
			static_cast<float>(Desired.Y) + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding), Spacing.Y);
	}
	return FVector2f(
		DreamPanelLayoutLocal::Sum(Columns) + DreamPanelLayoutLocal::NonNegative(Spacing.X) * FMath::Max(0, ColumnCount - 1) + DreamPanelLayoutLocal::HorizontalPadding(Padding),
		DreamPanelLayoutLocal::Sum(Rows) + DreamPanelLayoutLocal::NonNegative(Spacing.Y) * FMath::Max(0, RowCount - 1) + DreamPanelLayoutLocal::VerticalPadding(Padding));
}

void UDreamLayoutContainerGridPanel::ArrangeChildren()
{
	TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren();
	int32 ColumnCount = FMath::Min(ColumnFill.Num(), DreamPanelLayoutLocal::MaxGridTrackCount);
	int32 RowCount = FMath::Min(RowFill.Num(), DreamPanelLayoutLocal::MaxGridTrackCount);
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		ColumnCount = FMath::Max(ColumnCount, DreamPanelLayoutLocal::GridTrackEnd(Slot->Column, Slot->ColumnSpan));
		RowCount = FMath::Max(RowCount, DreamPanelLayoutLocal::GridTrackEnd(Slot->Row, Slot->RowSpan));
	}
	TArray<float> DesiredColumns;
	TArray<float> DesiredRows;
	DesiredColumns.Init(0.0f, ColumnCount);
	DesiredRows.Init(0.0f, RowCount);
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child);
		DreamPanelLayoutLocal::AddSpanRequirement(DesiredColumns, Slot->Column, Slot->ColumnSpan,
			static_cast<float>(Desired.X) + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding), Spacing.X);
		DreamPanelLayoutLocal::AddSpanRequirement(DesiredRows, Slot->Row, Slot->RowSpan,
			static_cast<float>(Desired.Y) + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding), Spacing.Y);
	}

	const float GapX = DreamPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = DreamPanelLayoutLocal::NonNegative(Spacing.Y);
	const float AvailableWidth = FMath::Max(0.0f, GetWidget()->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding) - GapX * FMath::Max(0, ColumnCount - 1));
	const float AvailableHeight = FMath::Max(0.0f, GetWidget()->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding) - GapY * FMath::Max(0, RowCount - 1));
	const TArray<float> Columns = DreamPanelLayoutLocal::ArrangeTracks(DesiredColumns, ColumnFill, AvailableWidth);
	const TArray<float> Rows = DreamPanelLayoutLocal::ArrangeTracks(DesiredRows, RowFill, AvailableHeight);

	DreamPanelLayoutLocal::ApplyStableZOrderWithinParticipatingSlots(GetWidget(), LayoutChildren);

	for (UDreamWidget* Child : LayoutChildren)
	{
		if (ColumnCount <= 0 || RowCount <= 0) break;
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const int32 Column = FMath::Clamp(DreamPanelLayoutLocal::GridIndex(Slot->Column), 0, ColumnCount - 1);
		const int32 Row = FMath::Clamp(DreamPanelLayoutLocal::GridIndex(Slot->Row), 0, RowCount - 1);
		const int32 ColumnSpan = FMath::Clamp(Slot->ColumnSpan, 1, ColumnCount - Column);
		const int32 RowSpan = FMath::Clamp(Slot->RowSpan, 1, RowCount - Row);
		const float X = DreamPanelLayoutLocal::FiniteOrZero(Padding.Left) + DreamPanelLayoutLocal::Sum(Columns, 0, Column) + GapX * Column;
		const float Y = DreamPanelLayoutLocal::FiniteOrZero(Padding.Top) + DreamPanelLayoutLocal::Sum(Rows, 0, Row) + GapY * Row;
		const float Width = DreamPanelLayoutLocal::Sum(Columns, Column, ColumnSpan) + GapX * (ColumnSpan - 1);
		const float Height = DreamPanelLayoutLocal::Sum(Rows, Row, RowSpan) + GapY * (RowSpan - 1);
		ApplyChildRect(Child, FVector2D(X, Y), FVector2D(Width, Height));
	}
	PreferredSize = MeasureUnconstrained();
}

/** Same as GridPanel: the cell size comes from the children, so the children get no cell-shaped spec. */
FVector2f UDreamLayoutContainerUniformGridPanel::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	const TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren(false);
	int32 ColumnCount = 0;
	int32 RowCount = 0;
	float CellWidth = DreamPanelLayoutLocal::NonNegative(MinDesiredSlotWidth);
	float CellHeight = DreamPanelLayoutLocal::NonNegative(MinDesiredSlotHeight);
	const float GapX = DreamPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = DreamPanelLayoutLocal::NonNegative(Spacing.Y);
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const int32 ColumnSpan = DreamPanelLayoutLocal::GridSpan(Slot->ColumnSpan);
		const int32 RowSpan = DreamPanelLayoutLocal::GridSpan(Slot->RowSpan);
		ColumnCount = FMath::Max(ColumnCount, DreamPanelLayoutLocal::GridTrackEnd(Slot->Column, ColumnSpan));
		RowCount = FMath::Max(RowCount, DreamPanelLayoutLocal::GridTrackEnd(Slot->Row, RowSpan));
		const FVector2D Desired = GetDesiredSize(Child);
		CellWidth = FMath::Max(CellWidth,
			FMath::Max(0.0f, static_cast<float>(Desired.X) + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding) - GapX * (ColumnSpan - 1)) / ColumnSpan);
		CellHeight = FMath::Max(CellHeight,
			FMath::Max(0.0f, static_cast<float>(Desired.Y) + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding) - GapY * (RowSpan - 1)) / RowSpan);
	}
	return FVector2f(
		CellWidth * ColumnCount + GapX * FMath::Max(0, ColumnCount - 1) + DreamPanelLayoutLocal::HorizontalPadding(Padding),
		CellHeight * RowCount + GapY * FMath::Max(0, RowCount - 1) + DreamPanelLayoutLocal::VerticalPadding(Padding));
}

void UDreamLayoutContainerUniformGridPanel::ArrangeChildren()
{
	const TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren();
	int32 ColumnCount = 0;
	int32 RowCount = 0;
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		ColumnCount = FMath::Max(ColumnCount, DreamPanelLayoutLocal::GridTrackEnd(Slot->Column, Slot->ColumnSpan));
		RowCount = FMath::Max(RowCount, DreamPanelLayoutLocal::GridTrackEnd(Slot->Row, Slot->RowSpan));
	}
	const float GapX = DreamPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = DreamPanelLayoutLocal::NonNegative(Spacing.Y);
	const float CellWidth = ColumnCount > 0
		? FMath::Max(0.0f, GetWidget()->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding) - GapX * (ColumnCount - 1)) / ColumnCount
		: 0.0f;
	const float CellHeight = RowCount > 0
		? FMath::Max(0.0f, GetWidget()->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding) - GapY * (RowCount - 1)) / RowCount
		: 0.0f;
	for (UDreamWidget* Child : LayoutChildren)
	{
		if (ColumnCount <= 0 || RowCount <= 0) break;
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const int32 Column = FMath::Clamp(DreamPanelLayoutLocal::GridIndex(Slot->Column), 0, ColumnCount - 1);
		const int32 Row = FMath::Clamp(DreamPanelLayoutLocal::GridIndex(Slot->Row), 0, RowCount - 1);
		const int32 ColumnSpan = FMath::Clamp(Slot->ColumnSpan, 1, ColumnCount - Column);
		const int32 RowSpan = FMath::Clamp(Slot->RowSpan, 1, RowCount - Row);
		ApplyChildRect(Child,
			FVector2D(DreamPanelLayoutLocal::FiniteOrZero(Padding.Left) + Column * (CellWidth + GapX),
				DreamPanelLayoutLocal::FiniteOrZero(Padding.Top) + Row * (CellHeight + GapY)),
			FVector2D(CellWidth * ColumnSpan + GapX * (ColumnSpan - 1), CellHeight * RowSpan + GapY * (RowSpan - 1)));
	}
	PreferredSize = MeasureUnconstrained();
}

void UDreamLayoutContainerSizeBox::GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const
{
	OutClasses.AddUnique(UDreamContentWidget::StaticClass());
}

FVector2f UDreamLayoutContainerSizeBox::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	const bool bContentParticipates = IsValid(Content) && Content->GetLayoutVisibleInHierarchy()
		&& !Content->GetIgnoreLayout();
	// An override is an EXACTLY for the content: the box will be that size whatever the content wants,
	// so that is the space the content actually gets. Without one the content inherits what this box was
	// offered, less the paddings.
	FDreamMeasureSpec ContentWidth = bOverrideWidth
		? FDreamMeasureSpec::AtMost(DreamPanelLayoutLocal::NonNegative(WidthOverride))
		: InWidthSpec;
	FDreamMeasureSpec ContentHeight = bOverrideHeight
		? FDreamMeasureSpec::AtMost(DreamPanelLayoutLocal::NonNegative(HeightOverride))
		: InHeightSpec;
	ContentWidth = ContentWidth.ForChild(DreamPanelLayoutLocal::HorizontalPadding(Padding));
	ContentHeight = ContentHeight.ForChild(DreamPanelLayoutLocal::VerticalPadding(Padding));
	if (bContentParticipates)
	{
		const UDreamPanelSlot* ContentSlot = GetSlot(Content);
		ContentWidth = ContentWidth.ForChild(DreamPanelLayoutLocal::HorizontalPadding(ContentSlot->Padding));
		ContentHeight = ContentHeight.ForChild(DreamPanelLayoutLocal::VerticalPadding(ContentSlot->Padding));
	}
	FVector2D Desired = bContentParticipates
		? GetDesiredSize(Content, ContentWidth, ContentHeight)
		: FVector2D::ZeroVector;
	if (bContentParticipates)
	{
		const UDreamPanelSlot* Slot = GetSlot(Content);
		Desired.X += DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		Desired.Y += DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
	}
	Desired.X += DreamPanelLayoutLocal::HorizontalPadding(Padding);
	Desired.Y += DreamPanelLayoutLocal::VerticalPadding(Padding);
	if (bOverrideWidth)
	{
		Desired.X = DreamPanelLayoutLocal::NonNegative(WidthOverride);
	}
	else
	{
		Desired.X = FMath::Max(Desired.X, DreamPanelLayoutLocal::NonNegative(MinDesiredSize.X));
		if (MaxDesiredSize.X > 0.0 && FMath::IsFinite(MaxDesiredSize.X)) Desired.X = FMath::Min(Desired.X, MaxDesiredSize.X);
	}
	if (bOverrideHeight)
	{
		Desired.Y = DreamPanelLayoutLocal::NonNegative(HeightOverride);
	}
	else
	{
		Desired.Y = FMath::Max(Desired.Y, DreamPanelLayoutLocal::NonNegative(MinDesiredSize.Y));
		if (MaxDesiredSize.Y > 0.0 && FMath::IsFinite(MaxDesiredSize.Y)) Desired.Y = FMath::Min(Desired.Y, MaxDesiredSize.Y);
	}
	return FVector2f(DreamPanelLayoutLocal::CleanSize(Desired));
}

FDreamLayoutControlAnchorData UDreamLayoutContainerSizeBox::GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const
{
	const UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	return Content == TargetWidget
		? Super::GetLayoutControlAnchor(TargetWidget)
		: FDreamLayoutControlAnchorData();
}

void UDreamLayoutContainerSizeBox::ArrangeChildren()
{
	UDreamWidget* Content = nullptr;
	for (UDreamWidget* Child : GetWidget()->GetChildren())
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
		if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
		{
			Slot->RestoreAuthoredGeometry();
		}
		// GetMaxChildren() is 1, so a second child can only come from legacy data or from a path that
		// bypassed the capacity check. It used to be neither arranged NOR hidden: restored to its
		// authored rect and left visible, so it floated over the content at whatever position the
		// designer's root size happened to give it. UMG cannot express this at all (UContentWidget holds
		// one child), and the panel that already had to decide -- WidgetSwitcher -- suppresses the ones
		// it is not showing. Same answer here: exactly one child is the content, the rest are collapsed
		// for layout. UDreamPanelLayoutBase::OnUnregister un-suppresses them when the panel goes.
		Child->SetLayoutVisibilitySuppressed(true);
	}
	if (IsValid(Content))
	{
		if (Content->GetLayoutVisibleInHierarchy() && !Content->GetIgnoreLayout())
		{
			ApplyChildRect(Content, FVector2D(DreamPanelLayoutLocal::FiniteOrZero(Padding.Left), DreamPanelLayoutLocal::FiniteOrZero(Padding.Top)), FVector2D(
				FMath::Max(0.0f, GetWidget()->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding)),
				FMath::Max(0.0f, GetWidget()->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding))));
		}
		else
		{
			ReleaseSkippedChildGeometry(Content);
		}
	}
	PreferredSize = MeasureUnconstrained();
}

void UDreamLayoutContainerScaleBox::GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const
{
	OutClasses.AddUnique(UDreamContentWidget::StaticClass());
}

/**
 * The other panel whose answer depends on the space it is given: ScaleToFitX/Y divides the room
 * available by the content's natural size, and the room available used to be GetWidget()->GetWidth() --
 * last pass's output read back in as this pass's input. It now comes from the spec, with the own-size
 * read kept only as the root fallback (see FDreamMeasureSpec::ResolveAvailable).
 *
 * The CONTENT is measured unconstrained on purpose: a scale box does not shrink its content to fit, it
 * scales it, so handing the content a ceiling would make the scale come out 1 and defeat the panel.
 */
FVector2f UDreamLayoutContainerScaleBox::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	if (!IsValid(Content) || !Content->GetLayoutVisibleInHierarchy()
		|| Content->GetIgnoreLayout())
	{
		return FVector2f::ZeroVector;
	}
	FVector2D Desired = GetDesiredSize(Content);
	const UDreamPanelSlot* Slot = GetSlot(Content);
	const float SpentWidth = DreamPanelLayoutLocal::HorizontalPadding(Padding) + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
	const float SpentHeight = DreamPanelLayoutLocal::VerticalPadding(Padding) + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
	const float OwnWidthFallback = IsValid(GetWidget()) ? FMath::Max(0.0f, GetWidget()->GetWidth() - SpentWidth) : 0.0f;
	const float OwnHeightFallback = IsValid(GetWidget()) ? FMath::Max(0.0f, GetWidget()->GetHeight() - SpentHeight) : 0.0f;
	FVector2f DesiredScale = FVector2f::UnitVector;
	if (Stretch == EDreamScaleBoxStretch::UserSpecified)
	{
		DesiredScale = FVector2f(DreamPanelLayoutLocal::NonNegative(UserSpecifiedScale));
	}
	else if (Stretch == EDreamScaleBoxStretch::ScaleToFitX && Desired.X > UE_SMALL_NUMBER)
	{
		const float AvailableWidth = InWidthSpec.ForChild(SpentWidth).ResolveAvailable(OwnWidthFallback);
		DesiredScale = FVector2f(DreamPanelLayoutLocal::NonNegative(AvailableWidth / Desired.X));
	}
	else if (Stretch == EDreamScaleBoxStretch::ScaleToFitY && Desired.Y > UE_SMALL_NUMBER)
	{
		const float AvailableHeight = InHeightSpec.ForChild(SpentHeight).ResolveAvailable(OwnHeightFallback);
		DesiredScale = FVector2f(DreamPanelLayoutLocal::NonNegative(AvailableHeight / Desired.Y));
	}
	if (bIgnoreInheritedScale && (Stretch == EDreamScaleBoxStretch::UserSpecified
		|| Stretch == EDreamScaleBoxStretch::ScaleToFitX || Stretch == EDreamScaleBoxStretch::ScaleToFitY))
	{
		const FVector ParentScale = GetWidget()->GetWorldScale();
		if (!FMath::IsNearlyZero(ParentScale.Y)) DesiredScale.X /= FMath::Abs(ParentScale.Y);
		if (!FMath::IsNearlyZero(ParentScale.Z)) DesiredScale.Y /= FMath::Abs(ParentScale.Z);
		DesiredScale.X = DreamPanelLayoutLocal::NonNegative(DesiredScale.X);
		DesiredScale.Y = DreamPanelLayoutLocal::NonNegative(DesiredScale.Y);
	}
	if (Stretch == EDreamScaleBoxStretch::ScaleToFitX && DesiredScale.Y > 0.0f)
	{
		Desired.Y *= DesiredScale.Y;
	}
	else if (Stretch == EDreamScaleBoxStretch::ScaleToFitY && DesiredScale.X > 0.0f)
	{
		Desired.X *= DesiredScale.X;
	}
	else if (Stretch == EDreamScaleBoxStretch::UserSpecified)
	{
		Desired.X *= DesiredScale.X;
		Desired.Y *= DesiredScale.Y;
	}
	return FVector2f(
		Desired.X + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding) + DreamPanelLayoutLocal::HorizontalPadding(Padding),
		Desired.Y + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding) + DreamPanelLayoutLocal::VerticalPadding(Padding));
}

FDreamLayoutControlAnchorData UDreamLayoutContainerScaleBox::GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const
{
	const UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	return Content == TargetWidget
		? Super::GetLayoutControlAnchor(TargetWidget)
		: FDreamLayoutControlAnchorData();
}

void UDreamLayoutContainerScaleBox::ArrangeChildren()
{
	UpdateClippingOverride();
	UDreamWidget* Child = nullptr;
	for (UDreamWidget* Candidate : GetWidget()->GetChildren())
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
		if (UDreamPanelSlot* Slot = Candidate->GetPanelSlot(); IsValid(Slot))
		{
			Slot->RestoreAuthoredGeometry();
		}
		Candidate->SetLayoutScale(FVector2f::UnitVector);
		/** A ScaleBox holds one child; see the note in UDreamLayoutContainerSizeBox::ArrangeChildren. */
		Candidate->SetLayoutVisibilitySuppressed(true);
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
		PreferredSize = MeasureUnconstrained();
		return;
	}
	if (UDreamPanelSlot* MutableSlot = Child->GetPanelSlot(); IsValid(MutableSlot))
	{
		MutableSlot->MarkLayoutGeometryApplied();
	}
	if (ScaledChild.IsValid() && ScaledChild.Get() != Child && ScaledChild->GetParent() == GetWidget())
	{
		ScaledChild->SetLayoutScale(FVector2f::UnitVector);
	}
	ScaledChild = Child;
	const UDreamPanelSlot* Slot = GetSlot(Child);
	const FVector2D Desired = GetDesiredSize(Child);
	const FVector2D InnerPosition(
		DreamPanelLayoutLocal::FiniteOrZero(Padding.Left) + DreamPanelLayoutLocal::FiniteOrZero(Slot->Padding.Left),
		DreamPanelLayoutLocal::FiniteOrZero(Padding.Top) + DreamPanelLayoutLocal::FiniteOrZero(Slot->Padding.Top));
	const float AvailableWidth = FMath::Max(0.0f, GetWidget()->GetWidth()
		- DreamPanelLayoutLocal::HorizontalPadding(Padding) - DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding));
	const float AvailableHeight = FMath::Max(0.0f, GetWidget()->GetHeight()
		- DreamPanelLayoutLocal::VerticalPadding(Padding) - DreamPanelLayoutLocal::VerticalPadding(Slot->Padding));
	const float ScaleX = Desired.X > UE_SMALL_NUMBER ? AvailableWidth / Desired.X : 1.0f;
	const float ScaleY = Desired.Y > UE_SMALL_NUMBER ? AvailableHeight / Desired.Y : 1.0f;
	FVector2f Scale(1.0f, 1.0f);
	if (Stretch == EDreamScaleBoxStretch::UserSpecified)
	{
		Scale = FVector2f(DreamPanelLayoutLocal::NonNegative(UserSpecifiedScale));
	}
	else if (Desired.X > UE_SMALL_NUMBER && Desired.Y > UE_SMALL_NUMBER)
	{
		switch (Stretch)
		{
		case EDreamScaleBoxStretch::ScaleToFit: Scale = FVector2f(FMath::Min(ScaleX, ScaleY)); break;
		case EDreamScaleBoxStretch::ScaleToFill: Scale = FVector2f(FMath::Max(ScaleX, ScaleY)); break;
		case EDreamScaleBoxStretch::ScaleToFitX: Scale = FVector2f(ScaleX); break;
		case EDreamScaleBoxStretch::ScaleToFitY: Scale = FVector2f(ScaleY); break;
		default: break;
		}
	}
	if (bIgnoreInheritedScale && Stretch != EDreamScaleBoxStretch::Fill)
	{
		const FVector ParentScale = GetWidget()->GetWorldScale();
		if (!FMath::IsNearlyZero(ParentScale.Y)) Scale.X /= FMath::Abs(ParentScale.Y);
		if (!FMath::IsNearlyZero(ParentScale.Z)) Scale.Y /= FMath::Abs(ParentScale.Z);
	}
	Scale.X = DreamPanelLayoutLocal::NonNegative(Scale.X);
	Scale.Y = DreamPanelLayoutLocal::NonNegative(Scale.Y);

	FVector2D UnscaledSize = Stretch == EDreamScaleBoxStretch::Fill
		? FVector2D(AvailableWidth, AvailableHeight)
		: Desired;
	FVector2D ScaledSize(Desired.X * Scale.X, Desired.Y * Scale.Y);
	if (Stretch == EDreamScaleBoxStretch::Fill)
	{
		ScaledSize = UnscaledSize;
	}
	if (Slot->HorizontalAlignment == EDreamPanelHorizontalAlignment::Fill && Scale.X > UE_SMALL_NUMBER)
	{
		UnscaledSize.X = AvailableWidth / Scale.X;
		ScaledSize.X = AvailableWidth;
	}
	if (Slot->VerticalAlignment == EDreamPanelVerticalAlignment::Fill && Scale.Y > UE_SMALL_NUMBER)
	{
		UnscaledSize.Y = AvailableHeight / Scale.Y;
		ScaledSize.Y = AvailableHeight;
	}
	double Left = InnerPosition.X;
	double Top = InnerPosition.Y;
	switch (Slot->HorizontalAlignment)
	{
	case EDreamPanelHorizontalAlignment::Center: Left += (AvailableWidth - ScaledSize.X) * 0.5; break;
	case EDreamPanelHorizontalAlignment::Right: Left += AvailableWidth - ScaledSize.X; break;
	default: break;
	}
	switch (Slot->VerticalAlignment)
	{
	case EDreamPanelVerticalAlignment::Center: Top += (AvailableHeight - ScaledSize.Y) * 0.5; break;
	case EDreamPanelVerticalAlignment::Bottom: Top += AvailableHeight - ScaledSize.Y; break;
	default: break;
	}

	// ScaleBox is the only panel whose result includes a scale, so it builds its rect by hand instead of
	// going through ApplyChildRect - the size it writes is unscaled and the position is in scaled space.
	const FVector2D Pivot = Child->GetPivot();
	FDreamPanelChildRect Rect;
	Rect.Child = Child;
	Rect.Size = FVector2f(UnscaledSize);
	Rect.bApplyScale = true;
	Rect.LayoutScale = Scale;
	Rect.AnchoredPosition = FVector2D(
		-GetWidget()->GetWidth() * 0.5 + Left + ScaledSize.X * Pivot.X,
		GetWidget()->GetHeight() * 0.5 - Top - ScaledSize.Y * (1.0 - Pivot.Y));
	RecordChildRect(Rect);
	PreferredSize = MeasureUnconstrained();
}

void UDreamLayoutContainerScaleBox::OnRegister()
{
	Super::OnRegister();
	UpdateClippingOverride();
}

void UDreamLayoutContainerScaleBox::UpdateClippingOverride()
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		bAppliedDefaultClipping = false;
		return;
	}
	const bool bNeedsClipping = Stretch == EDreamScaleBoxStretch::ScaleToFill
		|| Stretch == EDreamScaleBoxStretch::ScaleToFitX
		|| Stretch == EDreamScaleBoxStretch::ScaleToFitY;
	if (bNeedsClipping)
	{
		Widget->SetLayoutClippingOverride(EDreamWidgetClipping::ClipToBounds);
		bAppliedDefaultClipping = true;
	}
	else if (bAppliedDefaultClipping)
	{
		Widget->ClearLayoutClippingOverride();
		bAppliedDefaultClipping = false;
	}
}

void UDreamLayoutContainerScaleBox::OnUnregister()
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

void UDreamLayoutContainerSafeZone::GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const
{
	OutClasses.AddUnique(UDreamContentWidget::StaticClass());
}

FMargin UDreamLayoutContainerSafeZone::GetCombinedSafePadding() const
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return FMargin();
	}
	const FVector2D WidgetSize = DreamPanelLayoutLocal::CleanSize(Widget->GetSize());
	const FMargin CleanSafe = DreamPanelLayoutLocal::CleanNonNegativeMargin(SafePadding);
	const FMargin CleanNormalized = DreamPanelLayoutLocal::CleanNormalizedSafePadding(NormalizedSafePadding);
#if WITH_EDITOR
	// A unit editor override gives the platform padding coefficient directly,
	// without making preferred size depend on this widget's current size.
	const FMargin PlatformNormalized = DreamPanelLayoutLocal::GetPlatformSafePadding(
		bUsePlatformSafeZone, bPadLeft, bPadTop, bPadRight, bPadBottom, FVector2D(1.0));
	const FMargin CombinedNormalized = DreamPanelLayoutLocal::CleanNormalizedSafePadding(FMargin(
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
	const FMargin CleanPlatform = DreamPanelLayoutLocal::GetPlatformSafePadding(
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

FVector2f UDreamLayoutContainerSafeZone::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	FVector2f Result = FVector2f::ZeroVector;
	UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	if (IsValid(Content) && Content->GetLayoutVisibleInHierarchy()
		&& !Content->GetIgnoreLayout())
	{
		// The safe padding is space the content will not get, so it comes out of the constraint first.
		const FMargin Combined = GetCombinedSafePadding();
		const UDreamPanelSlot* ContentSlot = GetSlot(Content);
		const FVector2D Desired = GetDesiredSize(Content,
			InWidthSpec.ForChild(DreamPanelLayoutLocal::HorizontalPadding(Combined)
				+ DreamPanelLayoutLocal::HorizontalPadding(ContentSlot->Padding)),
			InHeightSpec.ForChild(DreamPanelLayoutLocal::VerticalPadding(Combined)
				+ DreamPanelLayoutLocal::VerticalPadding(ContentSlot->Padding)));
		const UDreamPanelSlot* Slot = GetSlot(Content);
		Result.X = static_cast<float>(Desired.X + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding));
		Result.Y = static_cast<float>(Desired.Y + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding));
	}
	FMargin AbsolutePadding = DreamPanelLayoutLocal::CleanNonNegativeMargin(SafePadding);
	FMargin CombinedNormalized = DreamPanelLayoutLocal::CleanNormalizedSafePadding(NormalizedSafePadding);
#if WITH_EDITOR
	const FMargin PlatformNormalized = DreamPanelLayoutLocal::GetPlatformSafePadding(
		bUsePlatformSafeZone, bPadLeft, bPadTop, bPadRight, bPadBottom, FVector2D(1.0));
	CombinedNormalized = DreamPanelLayoutLocal::CleanNormalizedSafePadding(FMargin(
		CombinedNormalized.Left + PlatformNormalized.Left,
		CombinedNormalized.Top + PlatformNormalized.Top,
		CombinedNormalized.Right + PlatformNormalized.Right,
		CombinedNormalized.Bottom + PlatformNormalized.Bottom));
#else
	const FMargin PlatformPadding = DreamPanelLayoutLocal::GetPlatformSafePadding(
		bUsePlatformSafeZone, bPadLeft, bPadTop, bPadRight, bPadBottom, FVector2D::ZeroVector);
	AbsolutePadding = FMargin(
		AbsolutePadding.Left + PlatformPadding.Left,
		AbsolutePadding.Top + PlatformPadding.Top,
		AbsolutePadding.Right + PlatformPadding.Right,
		AbsolutePadding.Bottom + PlatformPadding.Bottom);
#endif
	const float NormalizedHorizontal = CombinedNormalized.Left + CombinedNormalized.Right;
	const float NormalizedVertical = CombinedNormalized.Top + CombinedNormalized.Bottom;
	const float AbsoluteHorizontal = DreamPanelLayoutLocal::HorizontalPadding(AbsolutePadding);
	const float AbsoluteVertical = DreamPanelLayoutLocal::VerticalPadding(AbsolutePadding);
	Result.X = (Result.X + FMath::Max(0.0f, AbsoluteHorizontal)) / FMath::Max(1.0e-3f, 1.0f - NormalizedHorizontal);
	Result.Y = (Result.Y + FMath::Max(0.0f, AbsoluteVertical)) / FMath::Max(1.0e-3f, 1.0f - NormalizedVertical);
	return Result;
}

void UDreamLayoutContainerSafeZone::OnRegister()
{
	Super::OnRegister();
	if (!SafeFrameChangedHandle.IsValid())
	{
		SafeFrameChangedHandle = FCoreDelegates::OnSafeFrameChangedEvent.AddUObject(
			this, &UDreamLayoutContainerSafeZone::HandleSafeFrameChanged);
	}
}

void UDreamLayoutContainerSafeZone::OnUnregister()
{
	if (SafeFrameChangedHandle.IsValid())
	{
		FCoreDelegates::OnSafeFrameChangedEvent.Remove(SafeFrameChangedHandle);
		SafeFrameChangedHandle.Reset();
	}
	Super::OnUnregister();
}

void UDreamLayoutContainerSafeZone::HandleSafeFrameChanged()
{
	if (bUsePlatformSafeZone)
	{
		RequestLayoutRefresh();
	}
}

FDreamLayoutControlAnchorData UDreamLayoutContainerSafeZone::GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const
{
	return DreamPanelLayoutLocal::GetFirstValidChild(GetWidget()) == TargetWidget
		? Super::GetLayoutControlAnchor(TargetWidget)
		: FDreamLayoutControlAnchorData();
}

void UDreamLayoutContainerSafeZone::ArrangeChildren()
{
	const FMargin Combined = GetCombinedSafePadding();
	UDreamWidget* Content = nullptr;
	for (UDreamWidget* Child : GetWidget()->GetChildren())
	{
		if (!IsValid(Child)) continue;
		if (!IsValid(Content))
		{
			Content = Child;
			Child->SetLayoutVisibilitySuppressed(false);
			continue;
		}
		if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot)) Slot->RestoreAuthoredGeometry();
		/** A SafeZone holds one child; see the note in UDreamLayoutContainerSizeBox::ArrangeChildren. */
		Child->SetLayoutVisibilitySuppressed(true);
	}
	if (IsValid(Content))
	{
		if (Content->GetLayoutVisibleInHierarchy() && !Content->GetIgnoreLayout())
		{
			ApplyChildRect(Content, FVector2D(Combined.Left, Combined.Top), FVector2D(
				FMath::Max(0.0f, GetWidget()->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Combined)),
				FMath::Max(0.0f, GetWidget()->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Combined))));
		}
		else
		{
			ReleaseSkippedChildGeometry(Content);
		}
	}
	PreferredSize = MeasureUnconstrained();
}

UDreamLayoutContainerScrollBox::UDreamLayoutContainerScrollBox()
{
	Orientation = EDreamPanelOrientation::Vertical;
}

void UDreamLayoutContainerScrollBox::OnRegister()
{
	Super::OnRegister();
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return;
	}
	bAppliedDefaultClipping = true;
	Widget->SetLayoutClippingOverride(EDreamWidgetClipping::ClipToBounds);
	// No companion creation here. OnRegister can run in the middle of prefab deserialization and
	// registration, and growing the widget's Components array there would mutate state the loader is
	// still walking. Runtime input companions are created in BeginPlay, which every load path calls
	// only after the whole hierarchy is deserialized and registered.
}

void UDreamLayoutContainerScrollBox::BeginPlay()
{
	Super::BeginPlay();
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return;
	}
	// Runtime input companions exist in GAME worlds only. Editor scenes (designer, helper loads for
	// save/refresh) never begin play, and the editor never needs wheel/drag on the panel anyway; its
	// viewport has its own scrolling.
	if (!Widget->GetWorld() || !Widget->GetWorld()->IsGameWorld())
	{
		return;
	}
	// The event system raycasts against visuals only (see FDreamBaseRaycaster, which walks the canvas
	// visual list), so a container with no visual can never be hit and wheel/drag would silently do
	// nothing. Give it a fully transparent rect-raycast target. Rect tracing does not need render
	// geometry, so this costs no visible pixels.
	if (!IsValid(Widget->GetVisual()))
	{
		if (UDreamImage* HitArea = Widget->CreateNewVisual<UDreamImage>())
		{
			HitArea->SetColor(FColor(0, 0, 0, 0));
			HitArea->SetRaycastType(EDreamVisualRaycastType::Rect);
			HitArea->SetRaycastTarget(true);
		}
	}
	// A layout container cannot receive pointer events, so wheel/drag lives on a transient companion
	// behaviour that writes back into this layout. Transient so it is never serialized into a prefab.
	if (!InputHandler.IsValid())
	{
		UDreamScrollBoxInputHandler* Handler = Widget->GetComponent<UDreamScrollBoxInputHandler>();
		if (!IsValid(Handler))
		{
			Handler = Widget->AddComponent<UDreamScrollBoxInputHandler>();
		}
		if (IsValid(Handler))
		{
			Handler->TargetLayout = this;
			InputHandler = Handler;
		}
	}
}

void UDreamLayoutContainerScrollBox::EndPlay()
{
	// The handler is created per play session; leaving it alive would double its Awake if the widget
	// begins play again.
	if (InputHandler.IsValid())
	{
		InputHandler->DestroyComponent();
		InputHandler.Reset();
	}
	Super::EndPlay();
}

void UDreamLayoutContainerScrollBox::OnUnregister()
{
	if (IsValid(GetWidget()))
	{
		GetWidget()->ClearLayoutClippingOverride();
	}
	if (InputHandler.IsValid())
	{
		InputHandler->DestroyComponent();
		InputHandler.Reset();
	}
	bAppliedDefaultClipping = false;
	Super::OnUnregister();
}

FVector2f UDreamLayoutContainerScrollBox::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	// A scroll box must never report its content extent along the scroll axis. If it did, any
	// Auto-measuring ancestor (a SizeBox with a cleared override, an Auto stack slot) would grow the
	// viewport to fit ALL content — unscrollable by construction — and the designer and PIE would
	// disagree wherever the surrounding space differs. The scroll-axis preferred size is padding
	// only; measurement then falls back to the widget's authored rect (the designer's viewport size),
	// and the actual viewport comes from that, the slot, a SizeBox override, or anchors. The cross
	// axis measures like a normal stack.
	//
	// The specs are forwarded to the stack half, which constrains the CROSS axis only -- exactly right
	// here: the cross axis is the viewport's and the children must fit it, while the scroll axis is
	// unbounded by definition and is then thrown away three lines down.
	FVector2f Result = Super::MeasureLayout(InWidthSpec, InHeightSpec);
	if (Orientation == EDreamPanelOrientation::Horizontal)
	{
		Result.X = DreamPanelLayoutLocal::HorizontalPadding(Padding);
	}
	else
	{
		Result.Y = DreamPanelLayoutLocal::VerticalPadding(Padding);
	}
	return Result;
}

#if WITH_EDITOR
void UDreamLayoutContainerScrollBox::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Details-panel edits bypass SetScrollOffset; sanitize and re-arrange so the designer sees the
	// scrolled content immediately (layout clamps against MaxScrollOffset).
	ScrollOffset = FMath::Max(0.0f, DreamPanelLayoutLocal::FiniteOrZero(ScrollOffset));
	RequestedScrollOffset = ScrollOffset;
	RequestLayoutRefresh();
}
#endif

void UDreamLayoutContainerScrollBox::SetScrollOffset(float Value)
{
	// Before the first layout pass MaxScrollOffset is still zero, so clamping against it would turn
	// every offset requested during construction or BeginPlay into 0 with no way to tell. Until the
	// metrics exist the request is kept as asked and CalculateLayout clamps it on the way through.
	const float Upper = bLayoutMetricsValid ? MaxScrollOffset : TNumericLimits<float>::Max();
	const float Sanitized = FMath::Max(0.0f, DreamPanelLayoutLocal::FiniteOrZero(Value));
	const float Clamped = FMath::Min(Sanitized, Upper);
	// Record the intent even when the clamped value is unchanged: the range may grow later, and this is
	// what the next layout pass re-derives the offset from.
	RequestedScrollOffset = Sanitized;
	if (FMath::IsNearlyEqual(ScrollOffset, Clamped))
	{
		return;
	}
	ScrollOffset = Clamped;
	MarkLayoutDirty();
	if (UDreamWidget* Widget = GetWidget(); IsValid(Widget))
	{
		UDreamWidget::MarkLayoutForRebuild(Widget);
	}
	SyncScrollbar();
}

bool UDreamLayoutContainerScrollBox::ScrollBy(float Delta)
{
	const float Before = ScrollOffset;
	SetScrollOffset(ScrollOffset + Delta);
	return !FMath::IsNearlyEqual(Before, ScrollOffset);
}

bool UDreamLayoutContainerScrollBox::ScrollByFromUser(float Delta)
{
	const bool bMoved = ScrollBy(Delta);
	if (bMoved)
	{
		OnUserScrolled.Broadcast(ScrollOffset);
	}
	return bMoved;
}

void UDreamLayoutContainerScrollBox::ScrollToStart()
{
	SetScrollOffset(0.0f);
}

void UDreamLayoutContainerScrollBox::ScrollToEnd()
{
	SetScrollOffset(MaxScrollOffset);
}

float UDreamLayoutContainerScrollBox::GetViewFraction() const
{
	if (MeasuredContentPrimary <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}
	return FMath::Clamp(MeasuredViewportPrimary / MeasuredContentPrimary, 0.0f, 1.0f);
}

float UDreamLayoutContainerScrollBox::GetViewOffsetFraction() const
{
	if (MaxScrollOffset <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp(ScrollOffset / MaxScrollOffset, 0.0f, 1.0f);
}

float UDreamLayoutContainerScrollBox::GetOverscroll() const
{
	return Overscroll;
}

void UDreamLayoutContainerScrollBox::EnsureScrollbarBound()
{
	UUIScrollbar* Bar = Scrollbar.Get();
	if (!IsValid(Bar) || ScrollbarChangedHandle.IsValid())
	{
		return;
	}
	// Bound lazily rather than in OnRegister: the reference is a serialized pointer to another
	// component, which need not have been loaded yet when this one registers.
	ScrollbarChangedHandle = Bar->GetOnValueChangedEvent().AddUObject(
		this, &UDreamLayoutContainerScrollBox::HandleScrollbarValueChanged);
}

void UDreamLayoutContainerScrollBox::SyncScrollbar()
{
	if (bSyncingFromScrollbar)
	{
		return;//the bar told us; telling it back is the loop
	}
	EnsureScrollbarBound();
	UUIScrollbar* Bar = Scrollbar.Get();
	if (!IsValid(Bar))
	{
		return;
	}
	const float Fraction = GetViewFraction();
	const bool bEverythingFits = MaxScrollOffset <= KINDA_SMALL_NUMBER;
	if (ScrollbarVisibility == EDreamScrollBoxScrollbarVisibility::AutoHide)
	{
		if (UDreamWidget* BarWidget = Bar->GetWidget(); IsValid(BarWidget))
		{
			BarWidget->SetWidgetActive(!bEverythingFits);
		}
	}
	// A Size of exactly 1 leaves the bar's own slide area at zero width, and its drag maths then
	// divides by it. Keep the handle a hair short of the track even when the bar stays visible.
	const float SafeSize = FMath::Clamp(Fraction, 0.0f, 1.0f - KINDA_SMALL_NUMBER);
	// Non-notifying on purpose: this is the push direction, and letting it fire would arrive back
	// as a pull. The parity box needs no axis inversion -- its offset grows the same way on both
	// axes, so the raw fraction is fed and the bar's DirectionType decides which end is zero.
	Bar->SetValueAndSize(GetViewOffsetFraction(), SafeSize, false);
}

void UDreamLayoutContainerScrollBox::HandleScrollbarValueChanged(float InValue)
{
	if (bSyncingFromScrollbar || MaxScrollOffset <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	TGuardValue<bool> SyncGuard(bSyncingFromScrollbar, true);
	// Grabbing the bar owns the position the same way grabbing the content does: momentum and any
	// spring-back in flight are dropped rather than fighting the handle.
	StopScrolling();
	SetScrollOffset(FMath::Clamp(InValue, 0.0f, 1.0f) * MaxScrollOffset);
	OnUserScrolled.Broadcast(ScrollOffset);
}

void UDreamLayoutContainerScrollBox::SetScrollVelocity(float Value)
{
	ScrollVelocity = DreamPanelLayoutLocal::FiniteOrZero(Value);
}

bool UDreamLayoutContainerScrollBox::IsScrolling() const
{
	return bAnimatingScroll || !FMath::IsNearlyZero(ScrollVelocity) || !FMath::IsNearlyZero(Overscroll);
}

void UDreamLayoutContainerScrollBox::SetScrollOffsetAnimated(float Value)
{
	const float Upper = bLayoutMetricsValid ? MaxScrollOffset : TNumericLimits<float>::Max();
	const float Target = FMath::Clamp(DreamPanelLayoutLocal::FiniteOrZero(Value), 0.0f, Upper);
	if (FMath::IsNearlyEqual(Target, ScrollOffset, ScrollAnimationSnapThreshold))
	{
		// Already there: land exactly rather than starting an animation that has nothing to do.
		bAnimatingScroll = false;
		SetScrollOffset(Target);
		return;
	}
	// Momentum and an eased scroll would fight over the same offset, so the animation wins and the
	// velocity is dropped rather than being quietly added on top.
	ScrollVelocity = 0.0f;
	AnimatedTargetOffset = Target;
	AnimatedStartOffset = ScrollOffset;
	AnimatedElapsed = 0.0f;
	bAnimatingScroll = true;
}

void UDreamLayoutContainerScrollBox::StopScrolling()
{
	const bool bWasDisplaced = !FMath::IsNearlyZero(Overscroll);
	ScrollVelocity = 0.0f;
	Overscroll = 0.0f;
	bAnimatingScroll = false;
	if (bWasDisplaced)
	{
		MarkLayoutDirty();
		if (UDreamWidget* Widget = GetWidget(); IsValid(Widget))
		{
			UDreamWidget::MarkLayoutForRebuild(Widget);
		}
	}
}

void UDreamLayoutContainerScrollBox::ApplyDragDelta(float Delta)
{
	Delta = DreamPanelLayoutLocal::FiniteOrZero(Delta);
	if (FMath::IsNearlyZero(Delta))
	{
		return;
	}
	if (CVarDreamScrollBoxTrace.GetValueOnGameThread() != 0)
	{
		UE_LOG(DreamGUI, Log, TEXT("[ScrollTrace] drag  delta=%+8.2f | offset=%8.2f max=%8.2f band=%+8.2f dragging=%d"),
			Delta, ScrollOffset, MaxScrollOffset, Overscroll, bDragging ? 1 : 0);
	}
	// A hand on the content beats an eased scroll heading somewhere else.
	bAnimatingScroll = false;
	if (!bAllowOverscroll || OverscrollLimit <= KINDA_SMALL_NUMBER)
	{
		ScrollByFromUser(Delta);
		return;
	}
	// A band is only a band while the offset is pinned at the end it points past. Anything else is
	// residue -- spring-back decay the grab interrupted, or float dust from the remainder
	// arithmetic below -- and a same-signed drag against residue used to read as "pushing out",
	// swallowing the whole gesture while the offset sat frozen mid-range.
	const bool bBandOpen = FMath::Abs(Overscroll) > OverscrollResidueThreshold
		&& ((Overscroll > 0.0f && ScrollOffset >= MaxScrollOffset - KINDA_SMALL_NUMBER)
			|| (Overscroll < 0.0f && ScrollOffset <= KINDA_SMALL_NUMBER));
	if (!bBandOpen && !FMath::IsNearlyZero(Overscroll))
	{
		Overscroll = 0.0f;
	}
	if (bBandOpen)
	{
		// Pulling further out meets rising resistance; pulling back answers one-for-one. Damping the
		// return as well is what made a long pull feel dead -- the finger moved and nothing did,
		// because the band still had a backlog to spend.
		const float Limit = FMath::Max(OverscrollLimit, KINDA_SMALL_NUMBER);
		const bool bPushingOut = FMath::Sign(Delta) == FMath::Sign(Overscroll);
		const float Headroom = FMath::Clamp(1.0f - FMath::Abs(Overscroll) / Limit, 0.0f, 1.0f);
		const float Applied = bPushingOut ? Delta * Headroom : Delta;
		const float NewBand = Overscroll + Applied;
		if (!FMath::IsNearlyZero(NewBand) && FMath::Sign(NewBand) != FMath::Sign(Overscroll))
		{
			// Crossed back inside: the remainder belongs to the offset again.
			Overscroll = 0.0f;
			ScrollByFromUser(NewBand);
		}
		else
		{
			Overscroll = FMath::Clamp(NewBand, -Limit, Limit);
		}
		MarkLayoutDirty();
		if (UDreamWidget* Widget = GetWidget(); IsValid(Widget))
		{
			UDreamWidget::MarkLayoutForRebuild(Widget);
		}
		return;
	}
	const float Before = ScrollOffset;
	ScrollByFromUser(Delta);
	// Whatever the clamp refused to spend becomes rubber band rather than being thrown away.
	const float Remainder = Delta - (ScrollOffset - Before);
	// Dust-sized remainders are float rounding, not an end being hit; opening a band for them is
	// what seeded the frozen-gesture bug in the first place.
	if (FMath::Abs(Remainder) > OverscrollResidueThreshold)
	{
		Overscroll = FMath::Clamp(Remainder, -FMath::Max(OverscrollLimit, KINDA_SMALL_NUMBER), FMath::Max(OverscrollLimit, KINDA_SMALL_NUMBER));
		MarkLayoutDirty();
		if (UDreamWidget* Widget = GetWidget(); IsValid(Widget))
		{
			UDreamWidget::MarkLayoutForRebuild(Widget);
		}
	}
}

void UDreamLayoutContainerScrollBox::SetDragging(bool bInDragging)
{
	if (CVarDreamScrollBoxTrace.GetValueOnGameThread() != 0 && bDragging != bInDragging)
	{
		UE_LOG(DreamGUI, Log, TEXT("[ScrollTrace] %s | offset=%8.2f band=%+8.2f vel=%+8.1f"),
			bInDragging ? TEXT("GRAB ") : TEXT("LETGO"), ScrollOffset, Overscroll, ScrollVelocity);
	}
	bDragging = bInDragging;
}

void UDreamLayoutContainerScrollBox::TickScrollPhysics(float DeltaTime)
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}
	if (bDragging)
	{
		// The pointer owns the offset until it lets go. Running the spring here would shut the
		// rubber band in the very frames the drag is opening it, and because any non-zero band
		// routes the whole drag delta into itself, the content stops advancing at the same time --
		// the gesture reads as "moves a little, then snaps back" while the finger is still down.
		return;
	}
	if (CVarDreamScrollBoxTrace.GetValueOnGameThread() != 0 && IsScrolling())
	{
		UE_LOG(DreamGUI, Log, TEXT("[ScrollTrace] tick  dt=%.4f     | offset=%8.2f max=%8.2f band=%+8.2f vel=%+8.1f animating=%d"),
			DeltaTime, ScrollOffset, MaxScrollOffset, Overscroll, ScrollVelocity, bAnimatingScroll ? 1 : 0);
	}
	if (bAnimatingScroll)
	{
		// Eased scrolling takes the whole frame: it already cancelled the velocity when it started,
		// and letting momentum run underneath would make the two disagree about where to land.
		if (ScrollAnimationMode == EDreamScrollAnimationMode::EaseCurve)
		{
			AnimatedElapsed += DeltaTime;
			const float Duration = FMath::Max(ScrollAnimationDuration, KINDA_SMALL_NUMBER);
			const float Elapsed = FMath::Min(AnimatedElapsed, Duration);
			// The curve mapping comes from DreamTween rather than a private copy, so a curve named
			// OutBack here is the same shape as one named OutBack on any tween in the project.
			const FDreamTweenFunction Ease = UDreamTweener::GetEaseFunction(ScrollAnimationEase);
			const float Next = Ease.IsBound()
				? Ease.Execute(AnimatedTargetOffset - AnimatedStartOffset, AnimatedStartOffset, Elapsed, Duration)
				: FMath::Lerp(AnimatedStartOffset, AnimatedTargetOffset, Elapsed / Duration);
			SetScrollOffset(Next);
			if (AnimatedElapsed >= Duration)
			{
				SetScrollOffset(AnimatedTargetOffset);
				bAnimatingScroll = false;
			}
			return;
		}
		SetScrollOffset(FMath::FInterpTo(ScrollOffset, AnimatedTargetOffset, DeltaTime, ScrollAnimationInterpolationSpeed));
		if (FMath::IsNearlyEqual(ScrollOffset, AnimatedTargetOffset, ScrollAnimationSnapThreshold))
		{
			SetScrollOffset(AnimatedTargetOffset);
			bAnimatingScroll = false;
		}
		return;
	}
	const bool bDisplaced = !FMath::IsNearlyZero(Overscroll);
	if (!bDisplaced && (!bEnableInertia || FMath::IsNearlyZero(ScrollVelocity)))
	{
		ScrollVelocity = bEnableInertia ? ScrollVelocity : 0.0f;
		return;
	}

	if (bDisplaced)
	{
		// The legacy spring, kept because its shape is right: while the velocity still points
		// outward it takes an opposing impulse proportional to the excess, and the instant it flips
		// the velocity is dropped and the return becomes a plain positional lerp. Critically damped
		// by construction -- it cannot overshoot back through the end and oscillate.
		const bool bMovingOutward = !FMath::IsNearlyZero(ScrollVelocity)
			&& FMath::Sign(ScrollVelocity) == FMath::Sign(Overscroll);
		if (bMovingOutward)
		{
			const float SpringImpulse = FMath::Abs(Overscroll) * OverscrollSpringStiffness;
			ScrollVelocity -= FMath::Sign(ScrollVelocity) * SpringImpulse * DeltaTime;
			Overscroll += ScrollVelocity * DeltaTime;
		}
		else
		{
			ScrollVelocity = 0.0f;
			Overscroll = FMath::Lerp(Overscroll, 0.0f, FMath::Clamp(OverscrollReturnRate * DeltaTime, 0.0f, 1.0f));
			if (FMath::Abs(Overscroll) < OverscrollSnapThreshold)
			{
				Overscroll = 0.0f;
			}
		}
	}
	else
	{
		// Exponential decay with the legacy's rate scaling, so DecelerationRate means the same thing
		// in both classes.
		ScrollVelocity = FMath::Lerp(ScrollVelocity, 0.0f,
			FMath::Clamp(DecelerationRate * 50.0f * DeltaTime, 0.0f, 1.0f));
		if (FMath::IsNearlyZero(ScrollVelocity))
		{
			ScrollVelocity = 0.0f;
			return;
		}
		const float Before = ScrollOffset;
		const float Step = ScrollVelocity * DeltaTime;
		ScrollBy(Step);
		const float Remainder = Step - (ScrollOffset - Before);
		if (FMath::Abs(Remainder) > OverscrollResidueThreshold)
		{
			// Ran into an end: carry the leftover into the rubber band, or stop dead when overscroll
			// is switched off. Without this the momentum would keep being spent against the clamp
			// and the box would look frozen while still "scrolling".
			if (bAllowOverscroll && OverscrollLimit > KINDA_SMALL_NUMBER)
			{
				Overscroll = Remainder;
			}
			else
			{
				ScrollVelocity = 0.0f;
			}
		}
	}
	MarkLayoutDirty();
	if (UDreamWidget* Widget = GetWidget(); IsValid(Widget))
	{
		UDreamWidget::MarkLayoutForRebuild(Widget);
	}
}

bool UDreamLayoutContainerScrollBox::GetChildContentExtent(UDreamWidget* InWidget, float& OutStart, float& OutExtent)
{
	OutStart = 0.0f;
	OutExtent = 0.0f;
	UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel) || !IsValid(InWidget))
	{
		return false;
	}
	// Accept any descendant: walk up until the parent is this panel, which is the child that actually
	// occupies a slot and therefore the one with a position in content space.
	UDreamWidget* DirectChild = InWidget;
	while (IsValid(DirectChild) && DirectChild->GetParent() != Panel)
	{
		DirectChild = DirectChild->GetParent();
	}
	if (!IsValid(DirectChild))
	{
		return false;
	}

	const bool bHorizontal = Orientation == EDreamPanelOrientation::Horizontal;
	const float Gap = DreamPanelLayoutLocal::NonNegative(Spacing);
	// Mirrors CalculateLayout's cursor walk, minus the scroll offset: the result is the child's place
	// in CONTENT space, which is what a scroll target has to be expressed in.
	float Cursor = bHorizontal
		? DreamPanelLayoutLocal::FiniteOrZero(Padding.Left)
		: DreamPanelLayoutLocal::FiniteOrZero(Padding.Top);
	// bEnsureSlots FALSE: this is a query, and the ensure path writes. It calls RestoreAuthoredGeometry
	// on every bIgnoreLayout child (a real SetAnchorData on the widget) or else snapshots one, and
	// creates a UDreamPanelSlot UObject for any child that has none. CanScrollWidgetIntoView runs this
	// every time directional navigation weighs a candidate -- i.e. on every press of a stick or d-pad --
	// so asking "could I scroll to that" was silently rewriting geometry and allocating. GetSlot returns
	// the class default when a child has no slot, so reading Padding below stays safe.
	for (UDreamWidget* Child : CollectLayoutChildren(/*bEnsureSlots*/false))
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child);
		const float SlotPadding = bHorizontal
			? DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)
			: DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
		const float Extent = FMath::Max(0.0f, static_cast<float>(bHorizontal ? Desired.X : Desired.Y) + SlotPadding);
		if (Child == DirectChild)
		{
			OutStart = Cursor;
			OutExtent = Extent;
			return true;
		}
		Cursor += Extent + Gap;
	}
	return false;
}

bool UDreamLayoutContainerScrollBox::CalculateOffsetToReveal(UDreamWidget* InWidget, float& OutTarget)
{
	// The walk below asks every child for its desired size, and each of those measures that child's whole
	// subtree. Outside a pass the memo is empty and unshared, so without this scope the cost of one
	// navigation query is O(children x subtree) with nothing reused between them.
	FDesiredSizeMemoScope Memo;
	OutTarget = ScrollOffset;
	float Start = 0.0f;
	float Extent = 0.0f;
	if (!GetChildContentExtent(InWidget, Start, Extent))
	{
		return false;
	}
	const float ViewStart = ScrollOffset;
	const float ViewEnd = ScrollOffset + MeasuredViewportPrimary;
	if (Start < ViewStart)
	{
		OutTarget = Start;//above the view: bring its leading edge to the top
	}
	else if (Start + Extent > ViewEnd)
	{
		// Below the view: bring its trailing edge to the bottom, unless it is taller than the view,
		// in which case showing its start is the only useful answer.
		OutTarget = Extent > MeasuredViewportPrimary ? Start : Start + Extent - MeasuredViewportPrimary;
	}
	else
	{
		return false;//already fully visible
	}
	if (bLayoutMetricsValid)
	{
		// Only once a pass has measured the box: before that MaxScrollOffset is zero, and clamping
		// against it would report every target as unreachable.
		OutTarget = FMath::Clamp(OutTarget, 0.0f, MaxScrollOffset);
	}
	return !FMath::IsNearlyEqual(OutTarget, ScrollOffset);
}

bool UDreamLayoutContainerScrollBox::CanScrollWidgetIntoView(UDreamWidget* InWidget)
{
	float Unused = 0.0f;
	return CalculateOffsetToReveal(InWidget, Unused);
}

bool UDreamLayoutContainerScrollBox::ScrollWidgetIntoView(UDreamWidget* InWidget, bool bAnimateScroll)
{
	float Target = ScrollOffset;
	if (!CalculateOffsetToReveal(InWidget, Target))
	{
		return false;
	}
	if (bAnimateScroll)
	{
		const float Before = GetAnimatedScrollTarget();
		SetScrollOffsetAnimated(Target);
		return !FMath::IsNearlyEqual(Before, GetAnimatedScrollTarget());
	}
	const float Before = ScrollOffset;
	bAnimatingScroll = false;//an explicit instant scroll overrides an eased one in flight
	SetScrollOffset(Target);
	return !FMath::IsNearlyEqual(Before, ScrollOffset);
}

void UDreamLayoutContainerScrollBox::ArrangeChildren()
{
	UDreamWidget* Panel = GetWidget();
	const TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren();
	const bool bHorizontal = Orientation == EDreamPanelOrientation::Horizontal;
	const float Gap = DreamPanelLayoutLocal::NonNegative(Spacing);
	const float AvailablePrimary = bHorizontal
		? FMath::Max(0.0f, Panel->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding))
		: FMath::Max(0.0f, Panel->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding));
	const float AvailableSecondary = bHorizontal
		? FMath::Max(0.0f, Panel->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding))
		: FMath::Max(0.0f, Panel->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding));

	// Children always take their desired size along the scroll axis. Fill would shrink content to the viewport,
	// which would make the box unscrollable by construction.
	auto PrimaryExtentOf = [&](UDreamWidget* Child)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		// Constrained on the CROSS axis only, and never on the scroll axis: the cross axis is the
		// viewport's and a wrapping child has to wrap against it, while a scroll-axis ceiling would be
		// the "unscrollable by construction" failure this whole function exists to avoid.
		const float CrossPadding = bHorizontal
			? DreamPanelLayoutLocal::VerticalPadding(Slot->Padding)
			: DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		const FDreamMeasureSpec CrossSpec = FDreamMeasureSpec::AtMost(FMath::Max(0.0f, AvailableSecondary - CrossPadding));
		const FVector2D Desired = bHorizontal
			? GetDesiredSize(Child, FDreamMeasureSpec::Undefined(), CrossSpec)
			: GetDesiredSize(Child, CrossSpec, FDreamMeasureSpec::Undefined());
		const float SlotPadding = bHorizontal
			? DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)
			: DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
		return FMath::Max(0.0f, static_cast<float>(bHorizontal ? Desired.X : Desired.Y) + SlotPadding);
	};

	float ContentPrimary = Gap * FMath::Max(0, LayoutChildren.Num() - 1);
	for (UDreamWidget* Child : LayoutChildren)
	{
		ContentPrimary += PrimaryExtentOf(Child);
	}
	MaxScrollOffset = FMath::Max(0.0f, ContentPrimary - AvailablePrimary);
	// Published for GetViewFraction / ScrollWidgetIntoView, and the flag that tells SetScrollOffset
	// its clamp bound is real now.
	MeasuredContentPrimary = ContentPrimary;
	MeasuredViewportPrimary = AvailablePrimary;
	if (!bLayoutMetricsValid)
	{
		// First pass: whatever ScrollOffset was serialized or set before any range existed is the request.
		RequestedScrollOffset = FMath::Max(0.0f, DreamPanelLayoutLocal::FiniteOrZero(ScrollOffset));
	}
	bLayoutMetricsValid = true;
	// The range only becomes real here, so this is the first moment the bar can be told anything
	// truthful about handle size.
	SyncScrollbar();
	// Re-derived from the request rather than clamped in place, so a pass that measures the content too
	// small moves the view without destroying the position it moved away from.
	ScrollOffset = FMath::Clamp(RequestedScrollOffset, 0.0f, MaxScrollOffset);

	// The rubber band displaces the content without moving the scroll position: GetScrollOffset stays
	// inside the range at all times, and only what the user sees is pulled past the end.
	float Cursor = (bHorizontal
		? DreamPanelLayoutLocal::FiniteOrZero(Padding.Left)
		: DreamPanelLayoutLocal::FiniteOrZero(Padding.Top)) - ScrollOffset - GetOverscroll();
	for (UDreamWidget* Child : LayoutChildren)
	{
		const float SlotPrimary = PrimaryExtentOf(Child);
		if (bHorizontal)
		{
			ApplyChildRect(Child, FVector2D(Cursor, DreamPanelLayoutLocal::FiniteOrZero(Padding.Top)),
				FVector2D(SlotPrimary, AvailableSecondary));
		}
		else
		{
			ApplyChildRect(Child, FVector2D(DreamPanelLayoutLocal::FiniteOrZero(Padding.Left), Cursor),
				FVector2D(AvailableSecondary, SlotPrimary));
		}
		Cursor += SlotPrimary + Gap;
	}
	PreferredSize = MeasureUnconstrained();
}

FVector2f UDreamLayoutContainerWidgetSwitcher::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel))
	{
		return FVector2f::ZeroVector;
	}
	UDreamWidget* Child = nullptr;
	if (Panel->GetChildrenCount() > 0)
	{
		//same clamp-resolution as CalculateLayout, so measure and arrangement agree on the child
		const int32 ResolvedIndex = FMath::Clamp(ActiveWidgetIndex, 0, Panel->GetChildrenCount() - 1);
		Child = Panel->GetChildren()[ResolvedIndex];
	}
	if (!IsValid(Child) || !Child->GetWidgetActiveInHierarchy() || Child->GetVisibility() == EDreamWidgetVisibility::Collapsed)
	{
		return FVector2f::ZeroVector;
	}
	if (Child->GetIgnoreLayout())
	{
		return FVector2f::ZeroVector;
	}
	// The shown page gets the whole content box, like an overlay with one visible child.
	const UDreamPanelSlot* Slot = GetSlot(Child);
	const FVector2D Desired = GetDesiredSize(Child,
		InWidthSpec.ForChild(DreamPanelLayoutLocal::HorizontalPadding(Padding)
			+ DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding)),
		InHeightSpec.ForChild(DreamPanelLayoutLocal::VerticalPadding(Padding)
			+ DreamPanelLayoutLocal::VerticalPadding(Slot->Padding)));
	return FVector2f(
		Desired.X + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding) + DreamPanelLayoutLocal::HorizontalPadding(Padding),
		Desired.Y + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding) + DreamPanelLayoutLocal::VerticalPadding(Padding));
}

void UDreamLayoutContainerWidgetSwitcher::ArrangeChildren()
{
	UDreamWidget* Panel = GetWidget();
	// Index-authoritative (UMG-aligned): resolve the displayed child by clamping for THIS pass only,
	// keeping the stored request intact so pages attached later can still satisfy it. ActiveWidget is a
	// cache of the resolution, never a competing source of truth.
	UDreamWidget* ActiveChild = nullptr;
	if (Panel->GetChildrenCount() > 0)
	{
		const int32 ResolvedIndex = FMath::Clamp(ActiveWidgetIndex, 0, Panel->GetChildrenCount() - 1);
		ActiveChild = Panel->GetChildren()[ResolvedIndex];
	}
	ActiveWidget = ActiveChild;
	for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
	{
		if (UDreamWidget* Child = Panel->GetChildren()[Index]; IsValid(Child))
		{
			const bool bIsActive = Child == ActiveChild;
			const bool bIgnored = Child->GetIgnoreLayout();
			if (!bIsActive || bIgnored)
			{
				if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
				{
					Slot->RestoreAuthoredGeometry();
				}
			}
			Child->SetLayoutVisibilitySuppressed(!bIsActive);
		}
	}
	if (UDreamWidget* Child = ActiveChild; IsValid(Child) && Child->GetLayoutVisibleInHierarchy())
	{
		if (!Child->GetIgnoreLayout())
		{
			EnsureSlot(Child);
			ApplyChildRect(Child, FVector2D(DreamPanelLayoutLocal::FiniteOrZero(Padding.Left), DreamPanelLayoutLocal::FiniteOrZero(Padding.Top)), FVector2D(
				FMath::Max(0.0f, Panel->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding)),
				FMath::Max(0.0f, Panel->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding))));
		}
	}
	PreferredSize = MeasureUnconstrained();
}

void UDreamLayoutContainerWidgetSwitcher::OnUnregister()
{
	if (UDreamWidget* Panel = GetWidget(); IsValid(Panel))
	{
		for (UDreamWidget* Child : Panel->GetChildren())
		{
			if (IsValid(Child)) Child->SetLayoutVisibilitySuppressed(false);
		}
	}
	ActiveWidget.Reset();
	Super::OnUnregister();
}

void UDreamLayoutContainerWidgetSwitcher::SetActiveWidgetIndex(int32 Value)
{
	// Store the REQUEST (sanitized to >=0), never a clamp against the current child count: the index is
	// routinely set before the pages attach, and clamping to 0 silently discarded the caller's intent.
	// Display resolution clamps at layout time, so a page attached later snaps to the requested index.
	Value = FMath::Max(0, Value);
	UDreamWidget* Panel = GetWidget();
	UDreamWidget* NewActiveWidget = IsValid(Panel) && Panel->GetChildren().IsValidIndex(Value)
		? Panel->GetChildren()[Value]
		: nullptr;
	if (ActiveWidgetIndex != Value || ActiveWidget.Get() != NewActiveWidget)
	{
		ActiveWidgetIndex = Value;
		ActiveWidget = NewActiveWidget;
		UDreamWidget::MarkLayoutForRebuild(Panel);
	}
}

bool UDreamLayoutContainerWidgetSwitcher::SetActiveWidget(UDreamWidget* Value)
{
	UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel) || !IsValid(Value))
	{
		return false;
	}
	const int32 Index = Panel->GetChildren().IndexOfByKey(Value);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	SetActiveWidgetIndex(Index);
	return true;
}

UDreamWidget* UDreamLayoutContainerWidgetSwitcher::GetActiveWidget() const
{
	UDreamWidget* Panel = GetWidget();
	if (ActiveWidget.IsValid() && ActiveWidget->GetParent() == Panel)
	{
		return ActiveWidget.Get();
	}
	return IsValid(Panel) && Panel->GetChildren().IsValidIndex(ActiveWidgetIndex)
		? Panel->GetChildren()[ActiveWidgetIndex]
		: nullptr;
}

#pragma region Border

void UDreamLayoutContainerBorder::GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const
{
	OutClasses.AddUnique(UDreamContentWidget::StaticClass());
}

void UDreamLayoutContainerBorder::SetBrushColor(FLinearColor Value)
{
	if (BrushColor != Value)
	{
		BrushColor = Value;
		ApplyBrushColorToVisual();
	}
}

void UDreamLayoutContainerBorder::ApplyBrushColorToVisual() const
{
	// The background IS the owning widget's visual; the border only names its colour, so that UMG's
	// UBorder::SetBrushColor has somewhere to land. A widget with no visual is a border with no
	// background, which is a legal thing to author and not worth a warning.
	if (const UDreamWidget* Widget = GetWidget(); IsValid(Widget))
	{
		if (UDreamVisual* Visual = Widget->GetVisual(); IsValid(Visual))
		{
			Visual->SetColor(BrushColor.ToFColor(/*bSRGB*/true));
		}
	}
}

void UDreamLayoutContainerBorder::OnRegister()
{
	Super::OnRegister();
	// On register rather than only from the setter, so a border loaded from an asset paints the colour
	// it was saved with instead of whatever colour the visual happens to carry.
	ApplyBrushColorToVisual();
}

FDreamLayoutControlAnchorData UDreamLayoutContainerBorder::GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const
{
	const UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	return Content == TargetWidget
		? Super::GetLayoutControlAnchor(TargetWidget)
		: FDreamLayoutControlAnchorData();
}

FVector2f UDreamLayoutContainerBorder::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	FVector2D Result = FVector2D::ZeroVector;
	if (IsValid(Content) && Content->GetLayoutVisibleInHierarchy() && !Content->GetIgnoreLayout())
	{
		const UDreamPanelSlot* Slot = GetSlot(Content);
		const float SpentWidth = DreamPanelLayoutLocal::HorizontalPadding(Padding)
			+ DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		const float SpentHeight = DreamPanelLayoutLocal::VerticalPadding(Padding)
			+ DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
		const FVector2D Desired = GetDesiredSize(Content,
			InWidthSpec.ForChild(SpentWidth), InHeightSpec.ForChild(SpentHeight));
		Result.X = Desired.X + SpentWidth;
		Result.Y = Desired.Y + SpentHeight;
	}
	else
	{
		Result.X = DreamPanelLayoutLocal::HorizontalPadding(Padding);
		Result.Y = DreamPanelLayoutLocal::VerticalPadding(Padding);
	}
	// UBorder::DesiredSizeScale scales what the border REPORTS, nothing else. The content is still
	// arranged in the rect the border actually got, which is what makes the property a hint to the
	// parent rather than a second scale box.
	Result.X *= DreamPanelLayoutLocal::NonNegative(DesiredSizeScale.X);
	Result.Y *= DreamPanelLayoutLocal::NonNegative(DesiredSizeScale.Y);
	return FVector2f(DreamPanelLayoutLocal::CleanSize(Result));
}

void UDreamLayoutContainerBorder::ArrangeChildren()
{
	UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel))
	{
		return;
	}
	UDreamWidget* Content = nullptr;
	for (UDreamWidget* Child : Panel->GetChildren())
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
		if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot))
		{
			Slot->RestoreAuthoredGeometry();
		}
		/** A border holds one child; see the note in UDreamLayoutContainerSizeBox::ArrangeChildren. */
		Child->SetLayoutVisibilitySuppressed(true);
	}
	if (IsValid(Content))
	{
		if (Content->GetLayoutVisibleInHierarchy() && !Content->GetIgnoreLayout())
		{
			// The alignment handed over here is the BORDER's, which is the one thing this panel does
			// that an overlay with a rect-block visual cannot.
			ApplyChildRect(Content,
				FVector2D(DreamPanelLayoutLocal::FiniteOrZero(Padding.Left), DreamPanelLayoutLocal::FiniteOrZero(Padding.Top)),
				FVector2D(
					FMath::Max(0.0f, Panel->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding)),
					FMath::Max(0.0f, Panel->GetHeight() - DreamPanelLayoutLocal::VerticalPadding(Padding))),
				/*bForceFill*/false, HorizontalAlignment, VerticalAlignment);
		}
		else
		{
			ReleaseSkippedChildGeometry(Content);
		}
	}
	PreferredSize = MeasureUnconstrained();
}

#pragma endregion

#pragma region MenuAnchor

bool UDreamLayoutContainerMenuAnchor::PlacementMatchesAnchorWidth(EDreamMenuPlacement InPlacement)
{
	return InPlacement == EDreamMenuPlacement::ComboBox || InPlacement == EDreamMenuPlacement::ComboBoxRight;
}

FVector2D UDreamLayoutContainerMenuAnchor::CalculateMenuPosition(EDreamMenuPlacement InPlacement,
	const FVector2D& AnchorPosition, const FVector2D& AnchorSize, const FVector2D& MenuSize)
{
	// Top-left space with y downwards, which is the space ApplyChildRect takes a position in.
	const double Left = AnchorPosition.X;
	const double Right = AnchorPosition.X + AnchorSize.X;
	const double Top = AnchorPosition.Y;
	const double Bottom = AnchorPosition.Y + AnchorSize.Y;
	switch (InPlacement)
	{
	case EDreamMenuPlacement::BelowAnchor:
	case EDreamMenuPlacement::ComboBox:
		return FVector2D(Left, Bottom);
	case EDreamMenuPlacement::CenteredBelowAnchor:
		return FVector2D(Left + (AnchorSize.X - MenuSize.X) * 0.5, Bottom);
	case EDreamMenuPlacement::BelowRightAnchor:
	case EDreamMenuPlacement::ComboBoxRight:
		return FVector2D(Right - MenuSize.X, Bottom);
	case EDreamMenuPlacement::AboveAnchor:
		return FVector2D(Left, Top - MenuSize.Y);
	case EDreamMenuPlacement::CenteredAboveAnchor:
		return FVector2D(Left + (AnchorSize.X - MenuSize.X) * 0.5, Top - MenuSize.Y);
	case EDreamMenuPlacement::AboveRightAnchor:
		return FVector2D(Right - MenuSize.X, Top - MenuSize.Y);
	case EDreamMenuPlacement::MenuRight:
		return FVector2D(Right, Top);
	case EDreamMenuPlacement::MenuLeft:
		return FVector2D(Left - MenuSize.X, Top);
	case EDreamMenuPlacement::Center:
		return FVector2D(Left + (AnchorSize.X - MenuSize.X) * 0.5, Top + (AnchorSize.Y - MenuSize.Y) * 0.5);
	default:
		return FVector2D(Left, Bottom);
	}
}

FVector2D UDreamLayoutContainerMenuAnchor::FitMenuInWindow(const FVector2D& MenuPosition, const FVector2D& MenuSize,
	const FVector2D& WindowSize)
{
	// Shift, never resize, and the left/top edge wins when the menu is bigger than the window: a menu
	// pushed off the top has no way back, whereas one overhanging the bottom can still be scrolled to.
	FVector2D Result = MenuPosition;
	Result.X = FMath::Min(Result.X, WindowSize.X - MenuSize.X);
	Result.Y = FMath::Min(Result.Y, WindowSize.Y - MenuSize.Y);
	Result.X = FMath::Max(Result.X, 0.0);
	Result.Y = FMath::Max(Result.Y, 0.0);
	return Result;
}

UDreamWidget* UDreamLayoutContainerMenuAnchor::GetAnchorContent() const
{
	return DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
}

UDreamWidget* UDreamLayoutContainerMenuAnchor::GetMenuContent() const
{
	const UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel))
	{
		return nullptr;
	}
	bool bSeenAnchor = false;
	for (UDreamWidget* Child : Panel->GetChildren())
	{
		if (!IsValid(Child))
		{
			continue;
		}
		if (!bSeenAnchor)
		{
			bSeenAnchor = true;
			continue;
		}
		return Child;
	}
	return nullptr;
}

void UDreamLayoutContainerMenuAnchor::SetIsOpen(bool Value)
{
	if (bIsOpen != Value)
	{
		bIsOpen = Value;
		if (UDreamWidget* Menu = GetMenuContent(); IsValid(Menu))
		{
			Menu->SetLayoutVisibilitySuppressed(!bIsOpen);
		}
		RequestLayoutRefresh();
	}
}

void UDreamLayoutContainerMenuAnchor::OnUnregister()
{
	if (UDreamWidget* Menu = GetMenuContent(); IsValid(Menu))
	{
		Menu->SetLayoutVisibilitySuppressed(false);
	}
	Super::OnUnregister();
}

FDreamLayoutControlAnchorData UDreamLayoutContainerMenuAnchor::GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const
{
	// Both children are placed by this panel, so both hand it their geometry.
	const UDreamWidget* Anchor = GetAnchorContent();
	const UDreamWidget* Menu = GetMenuContent();
	return (TargetWidget != nullptr && (TargetWidget == Anchor || TargetWidget == Menu))
		? Super::GetLayoutControlAnchor(TargetWidget)
		: FDreamLayoutControlAnchorData();
}

FVector2f UDreamLayoutContainerMenuAnchor::MeasureLayout(const FDreamMeasureSpec& InWidthSpec, const FDreamMeasureSpec& InHeightSpec) const
{
	// The ANCHOR alone. A menu that grew the button it hangs off would move that button out from under
	// the pointer every time it opened, which is the whole reason UMG puts menu content on a popup
	// layer instead of in the anchor's own layout.
	UDreamWidget* Anchor = GetAnchorContent();
	if (!IsValid(Anchor) || !Anchor->GetLayoutVisibleInHierarchy() || Anchor->GetIgnoreLayout())
	{
		return FVector2f::ZeroVector;
	}
	const UDreamPanelSlot* Slot = GetSlot(Anchor);
	const float SpentWidth = DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
	const float SpentHeight = DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
	const FVector2D Desired = GetDesiredSize(Anchor,
		InWidthSpec.ForChild(SpentWidth), InHeightSpec.ForChild(SpentHeight));
	return FVector2f(DreamPanelLayoutLocal::CleanSize(
		FVector2D(Desired.X + SpentWidth, Desired.Y + SpentHeight)));
}

void UDreamLayoutContainerMenuAnchor::ArrangeChildren()
{
	UDreamWidget* Panel = GetWidget();
	if (!IsValid(Panel))
	{
		return;
	}
	UDreamWidget* Anchor = GetAnchorContent();
	UDreamWidget* Menu = GetMenuContent();
	const FVector2D AnchorSize(
		DreamPanelLayoutLocal::NonNegative(Panel->GetWidth()),
		DreamPanelLayoutLocal::NonNegative(Panel->GetHeight()));

	if (IsValid(Anchor))
	{
		Anchor->SetLayoutVisibilitySuppressed(false);
		if (Anchor->GetLayoutVisibleInHierarchy() && !Anchor->GetIgnoreLayout())
		{
			ApplyChildRect(Anchor, FVector2D::ZeroVector, AnchorSize);
		}
		else
		{
			ReleaseSkippedChildGeometry(Anchor);
		}
	}

	if (IsValid(Menu))
	{
		Menu->SetLayoutVisibilitySuppressed(!bIsOpen);
		if (bIsOpen && Menu->GetLayoutVisibleInHierarchy() && !Menu->GetIgnoreLayout())
		{
			const UDreamPanelSlot* MenuSlot = GetSlot(Menu);
			const FVector2D MenuDesired = DreamPanelLayoutLocal::CleanSize(GetDesiredSize(Menu));
			FVector2D MenuSize(
				MenuDesired.X + DreamPanelLayoutLocal::HorizontalPadding(MenuSlot->Padding),
				MenuDesired.Y + DreamPanelLayoutLocal::VerticalPadding(MenuSlot->Padding));
			if (PlacementMatchesAnchorWidth(Placement))
			{
				MenuSize.X = AnchorSize.X;
			}
			// In this panel's own content space the anchor rect IS the panel, at the origin.
			FVector2D MenuPosition = CalculateMenuPosition(Placement, FVector2D::ZeroVector, AnchorSize, MenuSize);
			if (bFitInWindow)
			{
				// Against the root widget, the nearest thing here to UMG's window: it is the rect the
				// whole hierarchy is laid out inside. The result has to come back into this panel's
				// space, so the panel's own offset within that root goes in and comes back out.
				if (const UDreamWidget* Root = Panel->GetRootWidgetInHierarchy(); IsValid(Root) && Root != Panel)
				{
					const FVector2D RootSize(
						DreamPanelLayoutLocal::NonNegative(Root->GetWidth()),
						DreamPanelLayoutLocal::NonNegative(Root->GetHeight()));
					const FVector PanelInRoot = Root->GetWorldTransform().InverseTransformPosition(
						Panel->GetWorldTransform().GetLocation());
					// Widget space is y-up about the pivot; this is the panel's top-left corner measured
					// from the root's top-left corner, which is the space FitMenuInWindow works in.
					const FVector2D PanelOffset(
						PanelInRoot.Y + RootSize.X * 0.5 - AnchorSize.X * Panel->GetPivot().X,
						RootSize.Y * 0.5 - PanelInRoot.Z - AnchorSize.Y * (1.0 - Panel->GetPivot().Y));
					const FVector2D Fitted = FitMenuInWindow(MenuPosition + PanelOffset, MenuSize, RootSize);
					MenuPosition = Fitted - PanelOffset;
				}
			}
			ApplyChildRect(Menu, MenuPosition, MenuSize, /*bForceFill*/true);
		}
		else if (!bIsOpen)
		{
			ReleaseSkippedChildGeometry(Menu);
		}
	}
	PreferredSize = MeasureUnconstrained();
}

#pragma endregion
