// Copyright 2025-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/LexLayoutSelfAspectRatio.h"
#include "LGUI.h"
#include "Core/Components/LexPanelSlot.h"

DECLARE_CYCLE_STAT(TEXT("LexLayoutSelf AspectRatio"), STAT_LexLayoutSelfAspectRatio, STATGROUP_LGUI);

namespace LexAspectRatioLocal
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
	static FLexLayoutControlAnchorData GetParentLayoutControl(const ULexWidget* Widget)
	{
		if (!IsValid(Widget))
		{
			return FLexLayoutControlAnchorData();
		}
		const ULexWidget* ParentWidget = Widget->GetParent();
		const ULexLayoutContainer* ParentLayout = IsValid(ParentWidget)
			? ParentWidget->GetLayoutContainer() : nullptr;
		if (!IsValid(ParentLayout))
		{
			return FLexLayoutControlAnchorData();
		}
		return ParentLayout->GetLayoutControlAnchor(Widget);
	}
}

FLexAspectRatioSolution ULexLayoutSelfAspectRatio::Solve() const
{
	SCOPE_CYCLE_COUNTER(STAT_LexLayoutSelfAspectRatio);
	FLexAspectRatioSolution Out;
	const ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget)) return Out;
	const float SafeAspectRatio = LexAspectRatioLocal::SanitizeAspectRatio(AspectRatio);
	const FLexLayoutControlAnchorData ParentControl = LexAspectRatioLocal::GetParentLayoutControl(Widget);
	// The single-axis modes only ever write their own axis, so they consult exactly that bit. The coupled
	// modes below write both axes at once - an aspect ratio cannot be expressed one axis at a time - so
	// they yield as soon as the parent claims *either* axis: a fight the parent is going to win every
	// frame is worse than a ratio that is simply not applied.
	const bool bParentOwnsPosition = ParentControl.bCanControlHorizontalPosition
		|| ParentControl.bCanControlVerticalPosition;
	const bool bParentOwnsSize = ParentControl.bCanControlHorizontalSize
		|| ParentControl.bCanControlVerticalSize;
	Out.PreferredSize = FVector2f(
		LexAspectRatioLocal::NonNegativeFinite(Widget->GetWidth()),
		LexAspectRatioLocal::NonNegativeFinite(Widget->GetHeight()));
	switch (AspectRatioType)
	{
	case ELexLayoutAspectRatioType::None:
#if WITH_EDITOR
		if (GetWorld() && !GetWorld()->IsGameWorld())//editor mode will set AspectRatio to Width/Height
		{
			if (FMath::Abs(Widget->GetHeight()) >= LexAspectRatioLocal::MinAspectRatio
				&& FMath::IsFinite(Widget->GetWidth()) && FMath::IsFinite(Widget->GetHeight()))
			{
				Out.bAdoptRatioFromWidget = true;
				Out.AdoptedRatio = LexAspectRatioLocal::SanitizeAspectRatio(
					static_cast<double>(Widget->GetWidth()) / Widget->GetHeight());
			}
		}
#endif
		break;
	case ELexLayoutAspectRatioType::HeightControlWidth:
		{
			const float Width = LexAspectRatioLocal::NonNegativeFloat(
				static_cast<double>(LexAspectRatioLocal::NonNegativeFinite(Widget->GetHeight())) * SafeAspectRatio);
			Out.PreferredSize.X = Width;
			if (!ParentControl.bCanControlHorizontalSize)
			{
				Out.bApplyWidth = true;
				Out.Width = Width;
			}
		}
		break;
	case ELexLayoutAspectRatioType::WidthControlHeight:
		{
			const float Height = LexAspectRatioLocal::NonNegativeFloat(
				static_cast<double>(LexAspectRatioLocal::NonNegativeFinite(Widget->GetWidth())) / SafeAspectRatio);
			Out.PreferredSize.Y = Height;
			if (!ParentControl.bCanControlVerticalSize)
			{
				Out.bApplyHeight = true;
				Out.Height = Height;
			}
		}
		break;
	case ELexLayoutAspectRatioType::FitInParent:
		{
			if (const ULexWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
			{
				const float ParentWidth = LexAspectRatioLocal::NonNegativeFinite(ParentWidget->GetWidth());
				const float ParentHeight = LexAspectRatioLocal::NonNegativeFinite(ParentWidget->GetHeight());
				FVector2D ThisSize;
				if (static_cast<double>(ParentWidth) > static_cast<double>(ParentHeight) * SafeAspectRatio)
				{
					ThisSize.X = LexAspectRatioLocal::NonNegativeFloat(static_cast<double>(ParentHeight) * SafeAspectRatio);
					ThisSize.Y = ParentHeight;
				}
				else
				{
					ThisSize.Y = LexAspectRatioLocal::NonNegativeFloat(static_cast<double>(ParentWidth) / SafeAspectRatio);
					ThisSize.X = ParentWidth;
				}
				const FVector2D Pivot = Widget->GetPivot();
				FVector2D AnchoredPosition;
				AnchoredPosition.X = LexAspectRatioLocal::FiniteFloat(
					ThisSize.X * (FMath::IsFinite(Pivot.X) ? Pivot.X - 0.5 : 0.0));
				AnchoredPosition.Y = LexAspectRatioLocal::FiniteFloat(
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
	case ELexLayoutAspectRatioType::EnvelopeParent:
		{
			if (const ULexWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
			{
				const float ParentWidth = LexAspectRatioLocal::NonNegativeFinite(ParentWidget->GetWidth());
				const float ParentHeight = LexAspectRatioLocal::NonNegativeFinite(ParentWidget->GetHeight());
				FVector2D ThisSize;
				if (static_cast<double>(ParentWidth) > static_cast<double>(ParentHeight) * SafeAspectRatio)
				{
					ThisSize.Y = LexAspectRatioLocal::NonNegativeFloat(static_cast<double>(ParentWidth) / SafeAspectRatio);
					ThisSize.X = ParentWidth;
				}
				else
				{
					ThisSize.X = LexAspectRatioLocal::NonNegativeFloat(static_cast<double>(ParentHeight) * SafeAspectRatio);
					ThisSize.Y = ParentHeight;
				}
				const FVector2D Pivot = Widget->GetPivot();
				FVector2D AnchoredPosition;
				AnchoredPosition.X = LexAspectRatioLocal::FiniteFloat(
					ThisSize.X * (FMath::IsFinite(Pivot.X) ? Pivot.X - 0.5 : 0.0));
				AnchoredPosition.Y = LexAspectRatioLocal::FiniteFloat(
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

void ULexLayoutSelfAspectRatio::CalculateSize()
{
	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget) || bIsCalculatingSize) return;
	TGuardValue<bool> CalculatingGuard(bIsCalculatingSize, true);

	const FLexAspectRatioSolution Solution = Solve();
	AspectRatio = LexAspectRatioLocal::SanitizeAspectRatio(AspectRatio);
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

void ULexLayoutSelfAspectRatio::OnTransformChanged()
{
}

void ULexLayoutSelfAspectRatio::OnDimensionChanged(bool InPivotChange, bool InWidthChange,
	bool InHeightChange)
{
	if (bIsCalculatingSize)return;
	CalculateSize();
}

#if WITH_EDITOR
void ULexLayoutSelfAspectRatio::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	AspectRatio = LexAspectRatioLocal::SanitizeAspectRatio(AspectRatio);
	CalculateSize();
}

void ULexLayoutSelfAspectRatio::PostInitProperties()
{
	Super::PostInitProperties();
}
#endif

FLexLayoutControlAnchorData ULexLayoutSelfAspectRatio::GetLayoutControlAnchor(const ULexWidget* TargetWidget) const
{
	FLexLayoutControlAnchorData Result;
	ULexWidget* ThisWidget = GetWidget();
	if (ThisWidget == TargetWidget)//self
	{
		switch (AspectRatioType)
		{
		case ELexLayoutAspectRatioType::EnvelopeParent:
		case ELexLayoutAspectRatioType::FitInParent:
			Result.bCanControlHorizontalSize = true;
			Result.bCanControlVerticalSize = true;
			Result.bCanControlHorizontalPosition = true;
			Result.bCanControlVerticalPosition = true;
			break;
		case ELexLayoutAspectRatioType::HeightControlWidth:
			Result.bCanControlHorizontalSize = true;
			break;
		case ELexLayoutAspectRatioType::WidthControlHeight:
			Result.bCanControlVerticalSize = true;
			break;
		}
	}
	return Result;
}

FVector2f ULexLayoutSelfAspectRatio::GetLayoutPreferredSize() const
{
	// Used to be CalculateSize() + read the cache, so a parent panel asking a child how big it wanted to
	// be moved and resized that child as a side effect - the measurement half of ApplyChildRect ran a
	// full write pass before the arrangement half had decided anything.
	return Solve().PreferredSize;
}

void ULexLayoutSelfAspectRatio::SetAspectRatioType(const ELexLayoutAspectRatioType& Value)
{
	if (AspectRatioType != Value)
	{
		AspectRatioType = Value;
		CalculateSize();
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutSelfAspectRatio::SetAspectRatio(float Value)
{
	Value = LexAspectRatioLocal::SanitizeAspectRatio(Value);
	if (AspectRatio != Value)
	{
		AspectRatio = Value;
		CalculateSize();
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}
