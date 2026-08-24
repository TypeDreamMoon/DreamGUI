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

		ParticipatingChildren.StableSort([](const UDreamWidget& A, const UDreamWidget& B)
		{
			const UDreamPanelSlot* ASlot = A.GetPanelSlot();
			const UDreamPanelSlot* BSlot = B.GetPanelSlot();
			return IsValid(ASlot) && IsValid(BSlot) && ASlot->ZOrder < BSlot->ZOrder;
		});

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
 * (in the prefab editor, at whatever root size that scene had) and can encode a position that is far outside the
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

TMap<const UDreamWidget*, FVector2D> UDreamPanelLayoutBase::DesiredSizeMemo;
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
	DesiredSizeMemo.Remove(Widget);
}

void UDreamPanelLayoutBase::ForgetAllDesiredSizes()
{
	DesiredSizeMemo.Reset();
}

FVector2D UDreamPanelLayoutBase::GetDesiredSize(UDreamWidget* Child) const
{
	if (DesiredSizeMemoDepth > 0)
	{
		if (const FVector2D* Cached = DesiredSizeMemo.Find(Child))
		{
			return *Cached;
		}
	}
	TFunction<FVector2D(UDreamWidget*, TSet<const UDreamWidget*>&)> GetIntrinsicSize;
	GetIntrinsicSize = [&GetIntrinsicSize](UDreamWidget* Widget, TSet<const UDreamWidget*>& Visited) -> FVector2D
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

		if (UDreamLayoutSelf* LayoutSelf = Widget->GetLayoutSelf(); IsValid(LayoutSelf)
			&& LayoutSelf->GetClass() != UDreamLayoutSelf::StaticClass())
		{
			const FDreamLayoutControlAnchorData LayoutControl = LayoutSelf->GetLayoutControlAnchor(Widget);
			const FVector2f LayoutDesired = LayoutSelf->GetLayoutPreferredSize();
			if (LayoutControl.bCanControlHorizontalSize) SetOverride(Desired.X, bWidthOverridden, LayoutDesired.X);
			if (LayoutControl.bCanControlVerticalSize) SetOverride(Desired.Y, bHeightOverridden, LayoutDesired.Y);
		}
		const bool bHasLayoutContainer = IsValid(Widget->GetLayoutContainer());
		if (UDreamLayoutContainer* LayoutContainer = Widget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			const FVector2f LayoutDesired = LayoutContainer->GetLayoutPreferredSize();
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
					const FVector2D ContentDesired = GetIntrinsicSize(ContentChild, Visited);
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
		// by capturing the pre-arrangement rect on load). The bare GetWidth/GetHeight fallback remains
		// only for legacy "applied but never snapshotted" data in worlds that skip registration, where
		// zeroing instead would collapse content that used to render; the prefab compiler reports
		// zero-desired Auto children so those spots surface in CompilerResults rather than on screen.
		if (Desired.X < 0.0) Desired.X = bHasAuthoredFallback ? AuthoredFallback.X : Widget->GetWidth();
		if (Desired.Y < 0.0) Desired.Y = bHasAuthoredFallback ? AuthoredFallback.Y : Widget->GetHeight();
		return DreamPanelLayoutLocal::CleanSize(Desired);
	};

	TSet<const UDreamWidget*> Visited;
	++DesiredSizeComputeCount;
	const FVector2D Result = GetIntrinsicSize(Child, Visited);
	if (DesiredSizeMemoDepth > 0)
	{
		DesiredSizeMemo.Add(Child, Result);
	}
	return Result;
}

void UDreamPanelLayoutBase::ApplyChildRect(UDreamWidget* Child, const FVector2D& Position, const FVector2D& Size, bool bForceFill) const
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
	const FVector2D Desired = DreamPanelLayoutLocal::CleanSize(GetDesiredSize(Child));

	double Width = InnerSize.X;
	double Height = InnerSize.Y;
	double Left = InnerPosition.X;
	double Top = InnerPosition.Y;
	if (!bForceFill && Slot->HorizontalAlignment != EDreamPanelHorizontalAlignment::Fill)
	{
		Width = FMath::Min(Desired.X, InnerSize.X);
		switch (Slot->HorizontalAlignment)
		{
		case EDreamPanelHorizontalAlignment::Center: Left += (InnerSize.X - Width) * 0.5; break;
		case EDreamPanelHorizontalAlignment::Right: Left += InnerSize.X - Width; break;
		default: break;
		}
	}
	if (!bForceFill && Slot->VerticalAlignment != EDreamPanelVerticalAlignment::Fill)
	{
		Height = FMath::Min(Desired.Y, InnerSize.Y);
		switch (Slot->VerticalAlignment)
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

FVector2f UDreamPanelLayoutBase::MeasureLayout() const
{
	return FVector2f::ZeroVector;
}

FVector2f UDreamPanelLayoutBase::GetLayoutPreferredSize() const
{
	FDesiredSizeMemoScope Memo;
	const FVector2f Result = MeasureLayout();
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
		OutInfo.DesiredSize = FVector2D(MeasureLayout());
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

FVector2f UDreamLayoutContainerCanvasPanel::MeasureLayout() const
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
	PreferredSize = MeasureLayout();
}

FVector2f UDreamLayoutContainerOverlay::MeasureLayout() const
{
	FVector2f Result = FVector2f::ZeroVector;
	for (UDreamWidget* Child : CollectLayoutChildren(false))
	{
		const FVector2D Desired = GetDesiredSize(Child);
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
	PreferredSize = MeasureLayout();
}

FVector2f UDreamLayoutContainerStackBox::MeasureLayout() const
{
	const TArray<UDreamWidget*> LayoutChildren = CollectLayoutChildren(false);
	const bool bHorizontal = Orientation == EDreamPanelOrientation::Horizontal;
	const float Gap = DreamPanelLayoutLocal::NonNegative(Spacing);
	float Primary = Gap * FMath::Max(0, LayoutChildren.Num() - 1);
	float Secondary = 0.0f;
	for (UDreamWidget* Child : LayoutChildren)
	{
		const FVector2D Desired = GetDesiredSize(Child);
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

	float FixedPrimary = Gap * FMath::Max(0, LayoutChildren.Num() - 1);
	float FillWeight = 0.0f;
	for (UDreamWidget* Child : LayoutChildren)
	{
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const FVector2D Desired = GetDesiredSize(Child);
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
		const FVector2D Desired = GetDesiredSize(Child);
		const UDreamPanelSlot* Slot = GetSlot(Child);
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
	PreferredSize = MeasureLayout();
}

UDreamLayoutContainerHorizontalBox::UDreamLayoutContainerHorizontalBox()
{
	Orientation = EDreamPanelOrientation::Horizontal;
}

UDreamLayoutContainerVerticalBox::UDreamLayoutContainerVerticalBox()
{
	Orientation = EDreamPanelOrientation::Vertical;
}

FVector2f UDreamLayoutContainerWrapBox::MeasureLayout() const
{
	const float GapX = DreamPanelLayoutLocal::NonNegative(Spacing.X);
	const float GapY = DreamPanelLayoutLocal::NonNegative(Spacing.Y);
	const float AvailableWidth = bExplicitWrapSize
		? DreamPanelLayoutLocal::NonNegative(WrapSize)
		: (IsValid(GetWidget()) ? FMath::Max(0.0f, GetWidget()->GetWidth() - DreamPanelLayoutLocal::HorizontalPadding(Padding)) : 0.0f);
	float X = 0.0f;
	float Y = 0.0f;
	float LineHeight = 0.0f;
	float MaxWidth = 0.0f;
	for (UDreamWidget* Child : CollectLayoutChildren(false))
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const UDreamPanelSlot* Slot = GetSlot(Child);
		const float ItemWidth = static_cast<float>(Desired.X) + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		const float ItemHeight = static_cast<float>(Desired.Y) + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
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
	TArray<FWrapLine> Lines;
	FWrapLine CurrentLine;
	float CurrentWidth = 0.0f;
	for (UDreamWidget* Child : CollectLayoutChildren())
	{
		const FVector2D Desired = GetDesiredSize(Child);
		const UDreamPanelSlot* Slot = GetSlot(Child);
		FWrapItem Item;
		Item.Widget = Child;
		Item.Width = static_cast<float>(Desired.X) + DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding);
		Item.Height = static_cast<float>(Desired.Y) + DreamPanelLayoutLocal::VerticalPadding(Slot->Padding);
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

	float Y = DreamPanelLayoutLocal::FiniteOrZero(Padding.Top);
	for (const FWrapLine& Line : Lines)
	{
		float X = DreamPanelLayoutLocal::FiniteOrZero(Padding.Left);
		for (const FWrapItem& Item : Line.Items)
		{
			ApplyChildRect(Item.Widget, FVector2D(X, Y), FVector2D(Item.Width, Line.Height));
			X += Item.Width + GapX;
		}
		Y += Line.Height + GapY;
	}
	PreferredSize = MeasureLayout();
}

FVector2f UDreamLayoutContainerGridPanel::MeasureLayout() const
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
	PreferredSize = MeasureLayout();
}

FVector2f UDreamLayoutContainerUniformGridPanel::MeasureLayout() const
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
	PreferredSize = MeasureLayout();
}

void UDreamLayoutContainerSizeBox::GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const
{
	OutClasses.AddUnique(UDreamContentWidget::StaticClass());
}

FVector2f UDreamLayoutContainerSizeBox::MeasureLayout() const
{
	UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	const bool bContentParticipates = IsValid(Content) && Content->GetLayoutVisibleInHierarchy()
		&& !Content->GetIgnoreLayout();
	FVector2D Desired = bContentParticipates ? GetDesiredSize(Content) : FVector2D::ZeroVector;
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
		Child->SetLayoutVisibilitySuppressed(false);
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
	PreferredSize = MeasureLayout();
}

void UDreamLayoutContainerScaleBox::GetRequiredBehaviourClasses(TArray<TSubclassOf<UDreamUIBehaviour>>& OutClasses) const
{
	OutClasses.AddUnique(UDreamContentWidget::StaticClass());
}

FVector2f UDreamLayoutContainerScaleBox::MeasureLayout() const
{
	UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	if (!IsValid(Content) || !Content->GetLayoutVisibleInHierarchy()
		|| Content->GetIgnoreLayout())
	{
		return FVector2f::ZeroVector;
	}
	FVector2D Desired = GetDesiredSize(Content);
	const UDreamPanelSlot* Slot = GetSlot(Content);
	FVector2f DesiredScale = FVector2f::UnitVector;
	if (Stretch == EDreamScaleBoxStretch::UserSpecified)
	{
		DesiredScale = FVector2f(DreamPanelLayoutLocal::NonNegative(UserSpecifiedScale));
	}
	else if (Stretch == EDreamScaleBoxStretch::ScaleToFitX && Desired.X > UE_SMALL_NUMBER)
	{
		const float AvailableWidth = FMath::Max(0.0f, GetWidget()->GetWidth()
			- DreamPanelLayoutLocal::HorizontalPadding(Padding) - DreamPanelLayoutLocal::HorizontalPadding(Slot->Padding));
		DesiredScale = FVector2f(DreamPanelLayoutLocal::NonNegative(AvailableWidth / Desired.X));
	}
	else if (Stretch == EDreamScaleBoxStretch::ScaleToFitY && Desired.Y > UE_SMALL_NUMBER)
	{
		const float AvailableHeight = FMath::Max(0.0f, GetWidget()->GetHeight()
			- DreamPanelLayoutLocal::VerticalPadding(Padding) - DreamPanelLayoutLocal::VerticalPadding(Slot->Padding));
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
	PreferredSize = MeasureLayout();
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

FVector2f UDreamLayoutContainerSafeZone::MeasureLayout() const
{
	FVector2f Result = FVector2f::ZeroVector;
	UDreamWidget* Content = DreamPanelLayoutLocal::GetFirstValidChild(GetWidget());
	if (IsValid(Content) && Content->GetLayoutVisibleInHierarchy()
		&& !Content->GetIgnoreLayout())
	{
		const FVector2D Desired = GetDesiredSize(Content);
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
			continue;
		}
		if (UDreamPanelSlot* Slot = Child->GetPanelSlot(); IsValid(Slot)) Slot->RestoreAuthoredGeometry();
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
	PreferredSize = MeasureLayout();
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
	// Runtime input companions exist in GAME worlds only. Editor scenes (prefab editor, helper loads for
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

FVector2f UDreamLayoutContainerScrollBox::MeasureLayout() const
{
	// A scroll box must never report its content extent along the scroll axis. If it did, any
	// Auto-measuring ancestor (a SizeBox with a cleared override, an Auto stack slot) would grow the
	// viewport to fit ALL content — unscrollable by construction — and the designer and PIE would
	// disagree wherever the surrounding space differs. The scroll-axis preferred size is padding
	// only; measurement then falls back to the widget's authored rect (the designer's viewport size),
	// and the actual viewport comes from that, the slot, a SizeBox override, or anchors. The cross
	// axis measures like a normal stack.
	FVector2f Result = Super::MeasureLayout();
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
	for (UDreamWidget* Child : CollectLayoutChildren())
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

bool UDreamLayoutContainerScrollBox::ScrollWidgetIntoView(UDreamWidget* InWidget, bool bAnimateScroll)
{
	float Start = 0.0f;
	float Extent = 0.0f;
	if (!GetChildContentExtent(InWidget, Start, Extent))
	{
		return false;
	}
	const float ViewStart = ScrollOffset;
	const float ViewEnd = ScrollOffset + MeasuredViewportPrimary;
	float Target = ScrollOffset;
	if (Start < ViewStart)
	{
		Target = Start;//above the view: bring its leading edge to the top
	}
	else if (Start + Extent > ViewEnd)
	{
		// Below the view: bring its trailing edge to the bottom, unless it is taller than the view,
		// in which case showing its start is the only useful answer.
		Target = Extent > MeasuredViewportPrimary ? Start : Start + Extent - MeasuredViewportPrimary;
	}
	else
	{
		return false;//already fully visible
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
		const FVector2D Desired = GetDesiredSize(Child);
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
	PreferredSize = MeasureLayout();
}

FVector2f UDreamLayoutContainerWidgetSwitcher::MeasureLayout() const
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
	const FVector2D Desired = GetDesiredSize(Child);
	const UDreamPanelSlot* Slot = GetSlot(Child);
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
	PreferredSize = MeasureLayout();
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
