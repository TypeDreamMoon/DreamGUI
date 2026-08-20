// Copyright 2025-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamLayoutSelfAspectRatio.h"
#include "DreamGUI.h"
#include "Core/Components/DreamPanelSlot.h"

DECLARE_CYCLE_STAT(TEXT("DreamLayoutSelf AspectRatio"), STAT_DreamLayoutSelfAspectRatio, STATGROUP_DreamGUI);

namespace DreamAspectRatioLocal
{
	constexpr float MinAspectRatio = 0.0001f;

	static float NonNegativeFinite(float Value)
	{
		return FMath::IsFinite(Value) ? FMath::Max(0.0f, Value) : 0.0f;
	}

	static float FiniteFloat(double Value, float Fallback = 0.0f)
	{
		if (!FMath::IsFinite(Value)) return Fallback;
		return static_cast<float>(FMath::Clamp(
			Value, -static_cast<double>(UE_MAX_FLT), static_cast<double>(UE_MAX_FLT)));
	}

	static float NonNegativeFloat(double Value)
	{
		return FMath::Max(0.0f, FiniteFloat(Value));
	}

	static float SanitizeAspectRatio(double Value)
	{
		if (!FMath::IsFinite(Value)) return 1.0f;
		return static_cast<float>(FMath::Clamp(
			Value, static_cast<double>(MinAspectRatio), static_cast<double>(UE_MAX_FLT)));
	}

	/**
	 * What the parent layout has claimed for this widget, per axis; all-false when there is no parent layout.
	 *
	 * This used to be a single "is the parent panel in charge" bool that required all four bits at once,
	 * plus a panel slot. A container that declares only the two *position* bits cannot satisfy that, so the
	 * suppression never engaged under one and AspectRatio overwrote the position the container had just
	 * written. It does not oscillate - AspectRatio recomputes from the parent's size, which the pass does
	 * not change, so it settles quietly on the wrong answer, with the child parked on top of its siblings.
	 *
	 * Asking per axis lets the two cooperate the way they always should have: the container places the
	 * child, AspectRatio sizes it.
	 */
	static FDreamLayoutControlAnchorData GetParentLayoutControl(const UDreamWidget* Widget)
	{
		if (!IsValid(Widget))
		{
			return FDreamLayoutControlAnchorData();
		}
		const UDreamWidget* ParentWidget = Widget->GetParent();
		const UDreamLayoutContainer* ParentLayout = IsValid(ParentWidget)
			? ParentWidget->GetLayoutContainer() : nullptr;
		if (!IsValid(ParentLayout))
		{
			return FDreamLayoutControlAnchorData();
		}
		return ParentLayout->GetLayoutControlAnchor(Widget);
	}
}

FDreamAspectRatioSolution UDreamLayoutSelfAspectRatio::Solve() const
{
	SCOPE_CYCLE_COUNTER(STAT_DreamLayoutSelfAspectRatio);
	FDreamAspectRatioSolution Out;
	const UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget)) return Out;
	const float SafeAspectRatio = DreamAspectRatioLocal::SanitizeAspectRatio(AspectRatio);
	const FDreamLayoutControlAnchorData ParentControl = DreamAspectRatioLocal::GetParentLayoutControl(Widget);
	// The single-axis modes only ever write their own axis, so they consult exactly that bit. The coupled
	// modes below write both axes at once - an aspect ratio cannot be expressed one axis at a time - so
	// they yield as soon as the parent claims *either* axis: a fight the parent is going to win every
	// frame is worse than a ratio that is simply not applied.
	const bool bParentOwnsPosition = ParentControl.bCanControlHorizontalPosition
		|| ParentControl.bCanControlVerticalPosition;
	const bool bParentOwnsSize = ParentControl.bCanControlHorizontalSize
		|| ParentControl.bCanControlVerticalSize;
	Out.PreferredSize = FVector2f(
		DreamAspectRatioLocal::NonNegativeFinite(Widget->GetWidth()),
		DreamAspectRatioLocal::NonNegativeFinite(Widget->GetHeight()));
	switch (AspectRatioType)
	{
	case EDreamLayoutAspectRatioType::None:
#if WITH_EDITOR
		if (GetWorld() && !GetWorld()->IsGameWorld())//editor mode will set AspectRatio to Width/Height
		{
			if (FMath::Abs(Widget->GetHeight()) >= DreamAspectRatioLocal::MinAspectRatio
				&& FMath::IsFinite(Widget->GetWidth()) && FMath::IsFinite(Widget->GetHeight()))
			{
				Out.bAdoptRatioFromWidget = true;
				Out.AdoptedRatio = DreamAspectRatioLocal::SanitizeAspectRatio(
					static_cast<double>(Widget->GetWidth()) / Widget->GetHeight());
			}
		}
#endif
		break;
	case EDreamLayoutAspectRatioType::HeightControlWidth:
		{
			const float Width = DreamAspectRatioLocal::NonNegativeFloat(
				static_cast<double>(DreamAspectRatioLocal::NonNegativeFinite(Widget->GetHeight())) * SafeAspectRatio);
			Out.PreferredSize.X = Width;
			if (!ParentControl.bCanControlHorizontalSize)
			{
				Out.bApplyWidth = true;
				Out.Width = Width;
			}
		}
		break;
	case EDreamLayoutAspectRatioType::WidthControlHeight:
		{
			const float Height = DreamAspectRatioLocal::NonNegativeFloat(
				static_cast<double>(DreamAspectRatioLocal::NonNegativeFinite(Widget->GetWidth())) / SafeAspectRatio);
			Out.PreferredSize.Y = Height;
			if (!ParentControl.bCanControlVerticalSize)
			{
				Out.bApplyHeight = true;
				Out.Height = Height;
			}
		}
		break;
	case EDreamLayoutAspectRatioType::FitInParent:
		{
			if (const UDreamWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
			{
				const float ParentWidth = DreamAspectRatioLocal::NonNegativeFinite(ParentWidget->GetWidth());
				const float ParentHeight = DreamAspectRatioLocal::NonNegativeFinite(ParentWidget->GetHeight());
				FVector2D ThisSize;
				if (static_cast<double>(ParentWidth) > static_cast<double>(ParentHeight) * SafeAspectRatio)
				{
					ThisSize.X = DreamAspectRatioLocal::NonNegativeFloat(static_cast<double>(ParentHeight) * SafeAspectRatio);
					ThisSize.Y = ParentHeight;
				}
				else
				{
					ThisSize.Y = DreamAspectRatioLocal::NonNegativeFloat(static_cast<double>(ParentWidth) / SafeAspectRatio);
					ThisSize.X = ParentWidth;
				}
				const FVector2D Pivot = Widget->GetPivot();
				FVector2D AnchoredPosition;
				AnchoredPosition.X = DreamAspectRatioLocal::FiniteFloat(
					ThisSize.X * (FMath::IsFinite(Pivot.X) ? Pivot.X - 0.5 : 0.0));
				AnchoredPosition.Y = DreamAspectRatioLocal::FiniteFloat(
					ThisSize.Y * (FMath::IsFinite(Pivot.Y) ? Pivot.Y - 0.5 : 0.0));
				Out.PreferredSize = FVector2f(ThisSize);
				if (!bParentOwnsPosition)
				{
					Out.bApplyPosition = true;
					Out.AnchoredPosition = AnchoredPosition;
				}
				if (!bParentOwnsSize)
				{
					Out.bApplySizeDelta = true;
					Out.SizeDelta = ThisSize;
				}
			}
		}
		break;
	case EDreamLayoutAspectRatioType::EnvelopeParent:
		{
			if (const UDreamWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
			{
				const float ParentWidth = DreamAspectRatioLocal::NonNegativeFinite(ParentWidget->GetWidth());
				const float ParentHeight = DreamAspectRatioLocal::NonNegativeFinite(ParentWidget->GetHeight());
				FVector2D ThisSize;
				if (static_cast<double>(ParentWidth) > static_cast<double>(ParentHeight) * SafeAspectRatio)
				{
					ThisSize.Y = DreamAspectRatioLocal::NonNegativeFloat(static_cast<double>(ParentWidth) / SafeAspectRatio);
					ThisSize.X = ParentWidth;
				}
				else
				{
					ThisSize.X = DreamAspectRatioLocal::NonNegativeFloat(static_cast<double>(ParentHeight) * SafeAspectRatio);
					ThisSize.Y = ParentHeight;
				}
				const FVector2D Pivot = Widget->GetPivot();
				FVector2D AnchoredPosition;
				AnchoredPosition.X = DreamAspectRatioLocal::FiniteFloat(
					ThisSize.X * (FMath::IsFinite(Pivot.X) ? Pivot.X - 0.5 : 0.0));
				AnchoredPosition.Y = DreamAspectRatioLocal::FiniteFloat(
					ThisSize.Y * (FMath::IsFinite(Pivot.Y) ? Pivot.Y - 0.5 : 0.0));
				Out.PreferredSize = FVector2f(ThisSize);
				if (!bParentOwnsPosition)
				{
					Out.bApplyPosition = true;
					Out.AnchoredPosition = AnchoredPosition;
				}
				if (!bParentOwnsSize)
				{
					Out.bApplySizeDelta = true;
					Out.SizeDelta = ThisSize;
				}
			}
		}
		break;
	}
	return Out;
}

void UDreamLayoutSelfAspectRatio::CalculateSize()
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget) || bIsCalculatingSize) return;
	TGuardValue<bool> CalculatingGuard(bIsCalculatingSize, true);

	const FDreamAspectRatioSolution Solution = Solve();
	AspectRatio = DreamAspectRatioLocal::SanitizeAspectRatio(AspectRatio);
#if WITH_EDITOR
	if (Solution.bAdoptRatioFromWidget)
	{
		AspectRatio = Solution.AdoptedRatio;
	}
#endif
	if (Solution.bApplyWidth)
	{
		Widget->SetWidth(Solution.Width);
	}
	if (Solution.bApplyHeight)
	{
		Widget->SetHeight(Solution.Height);
	}
	if (Solution.bApplyPosition)
	{
		Widget->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5));
		Widget->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5));
		Widget->SetAnchoredPosition(Solution.AnchoredPosition);
	}
	if (Solution.bApplySizeDelta)
	{
		Widget->SetSizeDelta(Solution.SizeDelta);
	}
	CalculatedPreferred = Solution.PreferredSize;
}

void UDreamLayoutSelfAspectRatio::OnTransformChanged()
{
}

void UDreamLayoutSelfAspectRatio::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
	bool InHeightChange)
{
	if (bIsCalculatingSize)return;
	CalculateSize();
}

#if WITH_EDITOR
void UDreamLayoutSelfAspectRatio::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	AspectRatio = DreamAspectRatioLocal::SanitizeAspectRatio(AspectRatio);
	CalculateSize();
}

void UDreamLayoutSelfAspectRatio::PostInitProperties()
{
	Super::PostInitProperties();
}
#endif

FDreamLayoutControlAnchorData UDreamLayoutSelfAspectRatio::GetLayoutControlAnchor(const UDreamWidget* TargetWidget) const
{
	FDreamLayoutControlAnchorData Result;
	UDreamWidget* ThisWidget = GetWidget();
	if (ThisWidget == TargetWidget)//self
	{
		switch (AspectRatioType)
		{
		case EDreamLayoutAspectRatioType::EnvelopeParent:
		case EDreamLayoutAspectRatioType::FitInParent:
			Result.bCanControlHorizontalSize = true;
			Result.bCanControlVerticalSize = true;
			Result.bCanControlHorizontalPosition = true;
			Result.bCanControlVerticalPosition = true;
			break;
		case EDreamLayoutAspectRatioType::HeightControlWidth:
			Result.bCanControlHorizontalSize = true;
			break;
		case EDreamLayoutAspectRatioType::WidthControlHeight:
			Result.bCanControlVerticalSize = true;
			break;
		}
	}
	return Result;
}

FVector2f UDreamLayoutSelfAspectRatio::GetLayoutPreferredSize() const
{
	// Used to be CalculateSize() + read the cache, so a parent panel asking a child how big it wanted to
	// be moved and resized that child as a side effect - the measurement half of ApplyChildRect ran a
	// full write pass before the arrangement half had decided anything.
	return Solve().PreferredSize;
}

void UDreamLayoutSelfAspectRatio::SetAspectRatioType(const EDreamLayoutAspectRatioType& Value)
{
	if (AspectRatioType != Value)
	{
		AspectRatioType = Value;
		CalculateSize();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamLayoutSelfAspectRatio::SetAspectRatio(float Value)
{
	Value = DreamAspectRatioLocal::SanitizeAspectRatio(Value);
	if (AspectRatio != Value)
	{
		AspectRatio = Value;
		CalculateSize();
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
