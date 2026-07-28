// Copyright 2025-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/LexLayoutSelfAspectRatio.h"
#include "LGUI.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
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

	static bool IsGeometryManagedByParentPanel(const ULexWidget* Widget)
	{
		if (!IsValid(Widget) || !IsValid(Widget->GetPanelSlot()))
		{
			return false;
		}
		const ULexWidget* ParentWidget = Widget->GetParent();
		const ULexLayoutContainer* ParentLayout = IsValid(ParentWidget)
			? ParentWidget->GetLayoutContainer() : nullptr;
		if (!IsValid(ParentLayout))
		{
			return false;
		}

		// Panel slots own the allotted rect. LayoutSelf remains the desired-size
		// provider, but must not overwrite that rect after the parent layout pass.
		const FLexLayoutControlAnchorData ParentControl = ParentLayout->GetLayoutControlAnchor(Widget);
		return ParentControl.bCanControlHorizontalPosition
			&& ParentControl.bCanControlVerticalPosition
			&& ParentControl.bCanControlHorizontalSize
			&& ParentControl.bCanControlVerticalSize;
	}
}

void ULexLayoutSelfAspectRatio::CalculateSize()
{
	SCOPE_CYCLE_COUNTER(STAT_LexLayoutSelfAspectRatio);
	ULexWidget* Widget = GetWidget();
	if (!IsValid(Widget) || bIsCalculatingSize) return;
	TGuardValue<bool> CalculatingGuard(bIsCalculatingSize, true);
	const float SafeAspectRatio = LexAspectRatioLocal::SanitizeAspectRatio(AspectRatio);
	AspectRatio = SafeAspectRatio;
	const bool bParentPanelOwnsGeometry = LexAspectRatioLocal::IsGeometryManagedByParentPanel(Widget);
	FVector2f PreferredSize(
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
				AspectRatio = LexAspectRatioLocal::SanitizeAspectRatio(
					static_cast<double>(Widget->GetWidth()) / Widget->GetHeight());
			}
		}
#endif
		break;
	case ELexLayoutAspectRatioType::HeightControlWidth:
		{
			const float Width = LexAspectRatioLocal::NonNegativeFloat(
				static_cast<double>(LexAspectRatioLocal::NonNegativeFinite(Widget->GetHeight())) * SafeAspectRatio);
			PreferredSize.X = Width;
			if (!bParentPanelOwnsGeometry)
			{
				Widget->SetWidth(Width);
			}
		}
		break;
	case ELexLayoutAspectRatioType::WidthControlHeight:
		{
			const float Height = LexAspectRatioLocal::NonNegativeFloat(
				static_cast<double>(LexAspectRatioLocal::NonNegativeFinite(Widget->GetWidth())) / SafeAspectRatio);
			PreferredSize.Y = Height;
			if (!bParentPanelOwnsGeometry)
			{
				Widget->SetHeight(Height);
			}
		}
		break;
	case ELexLayoutAspectRatioType::FitInParent:
		{
			if (ULexWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
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
				PreferredSize = FVector2f(ThisSize);
				if (!bParentPanelOwnsGeometry)
				{
					Widget->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5));
					Widget->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5));
					Widget->SetAnchoredPosition(AnchoredPosition);
					Widget->SetSizeDelta(ThisSize);
				}
			}
		}
		break;
	case ELexLayoutAspectRatioType::EnvelopeParent:
		{
			if (ULexWidget* ParentWidget = Widget->GetParent(); IsValid(ParentWidget))
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
				PreferredSize = FVector2f(ThisSize);
				if (!bParentPanelOwnsGeometry)
				{
					Widget->SetHorizontalAnchorMinMax(FVector2D(0.5, 0.5));
					Widget->SetVerticalAnchorMinMax(FVector2D(0.5, 0.5));
					Widget->SetAnchoredPosition(AnchoredPosition);
					Widget->SetSizeDelta(ThisSize);
				}
			}
		}
		break;
	}
	CalculatedPreferred = PreferredSize;
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

FVector2f ULexLayoutSelfAspectRatio::GetLayoutPreferredSize()
{
	CalculateSize();
	return CalculatedPreferred;
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
