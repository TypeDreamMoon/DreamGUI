// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Interaction/DreamResponsiveBinding.h"
#include "UObject/UnrealType.h"

void UDreamDataBinding::Awake()
{
	Super::Awake();
	if (UpdateMode != EDreamBindingUpdateMode::Manual)
	{
		ApplyBinding();
	}
	if (UpdateMode != EDreamBindingUpdateMode::EveryFrame)
	{
		SetCanExecuteTick(false);
	}
}

void UDreamDataBinding::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	ApplyBinding();
}

bool UDreamDataBinding::ApplyBinding()
{
	UObject* Source = SourceObject;
	UObject* Target = TargetObject ? TargetObject.Get() : GetWidget();
	if (!IsValid(Source) || !IsValid(Target) || SourceProperty.IsNone() || TargetProperty.IsNone())
	{
		return false;
	}
	FProperty* SourceValueProperty = Source->GetClass()->FindPropertyByName(SourceProperty);
	FProperty* TargetValueProperty = Target->GetClass()->FindPropertyByName(TargetProperty);
	if (!SourceValueProperty || !TargetValueProperty || !SourceValueProperty->SameType(TargetValueProperty))
	{
		return false;
	}
	const void* SourceValue = SourceValueProperty->ContainerPtrToValuePtr<void>(Source);
	void* TargetValue = TargetValueProperty->ContainerPtrToValuePtr<void>(Target);
	if (UDreamWidget* TargetWidget = Cast<UDreamWidget>(Target))
	{
		if (TargetProperty == UDreamWidget::GetPropertyName_Visibility())
		{
			TargetWidget->SetVisibility(*static_cast<const EDreamWidgetVisibility*>(SourceValue));
			return true;
		}
		if (TargetProperty == UDreamWidget::GetPropertyName_WidgetActive())
		{
			const FBoolProperty* BoolProperty = CastFieldChecked<FBoolProperty>(SourceValueProperty);
			TargetWidget->SetWidgetActive(BoolProperty->GetPropertyValue(SourceValue));
			return true;
		}
		// All five names now come from a GetPropertyName_ accessor, and the uniformity is the point.
		// Falling out of this chain is not an error: it is the generic CopyCompleteValue at the
		// bottom of this function, which lands the value and skips the setter. For these three the
		// setter is where the work is -- SetInteractable and SetRaycastable recompute an
		// "...InHierarchy" cache on every descendant -- so a rename that quietly retired one of
		// these branches left a parent reading correctly in the Details panel and children behaving
		// as though nothing had changed, with nothing at compile time noticing. Spelled through the
		// accessor, a rename cannot compile without coming here too.
		if (TargetProperty == UDreamWidget::GetPropertyName_RenderOpacity())
		{
			TargetWidget->SetRenderOpacity(*static_cast<const float*>(SourceValue));
			return true;
		}
		if (TargetProperty == UDreamWidget::GetPropertyName_Interactable())
		{
			TargetWidget->SetInteractable(*static_cast<const EDreamWidgetInteractableType*>(SourceValue));
			return true;
		}
		if (TargetProperty == UDreamWidget::GetPropertyName_Raycastable())
		{
			TargetWidget->SetRaycastable(*static_cast<const EDreamWidgetRaycastableType*>(SourceValue));
			return true;
		}
	}
#if WITH_EDITOR
	// Asked BEFORE the copy, because after it the two are identical by construction.
	//
	// PostEditChangeProperty is not a notification, it is the target's whole property-changed path:
	// rebuilds, invalidations, whatever that class does on an edit. In EveryFrame mode ApplyBinding
	// runs once per frame per binding and fired it unconditionally -- every frame, for a value that
	// is the same one already sitting there on all but the frames it actually moves.
	// (ArrayDim > 1 short-circuits to "changed": Identical compares one element, CopyCompleteValue
	// copies them all, so a static array cannot be settled by asking about its first entry.)
	const bool bValueChanged = TargetValueProperty->ArrayDim > 1
		|| !TargetValueProperty->Identical(TargetValue, SourceValue);
#endif
	TargetValueProperty->CopyCompleteValue(TargetValue, SourceValue);
#if WITH_EDITOR
	if (bValueChanged)
	{
		FPropertyChangedEvent ChangeEvent(TargetValueProperty);
		Target->PostEditChangeProperty(ChangeEvent);
	}
#endif
	return true;
}

bool FDreamResponsiveRule::Matches(const FVector2D& Size) const
{
	return Size.X >= MinWidth && Size.Y >= MinHeight
		&& (MaxWidth <= 0.0f || Size.X <= MaxWidth)
		&& (MaxHeight <= 0.0f || Size.Y <= MaxHeight);
}

void UDreamResponsiveBehaviour::OnRegister()
{
	Super::OnRegister();
	BindParentDimensionEvent();
}

void UDreamResponsiveBehaviour::OnUnregister()
{
	if (BoundParent.IsValid() && ParentDimensionHandle.IsValid())
	{
		BoundParent->GetDimensionChangedEvent().Remove(ParentDimensionHandle);
	}
	ParentDimensionHandle.Reset();
	BoundParent = nullptr;
	Super::OnUnregister();
}

void UDreamResponsiveBehaviour::BindParentDimensionEvent()
{
	if (BoundParent.IsValid() && ParentDimensionHandle.IsValid())
	{
		BoundParent->GetDimensionChangedEvent().Remove(ParentDimensionHandle);
	}
	ParentDimensionHandle.Reset();
	BoundParent = GetWidget() ? GetWidget()->GetParent() : nullptr;
	if (BoundParent.IsValid())
	{
		ParentDimensionHandle = BoundParent->GetDimensionChangedEvent().AddUObject(this, &UDreamResponsiveBehaviour::HandleParentDimensionChanged);
	}
}

void UDreamResponsiveBehaviour::HandleParentDimensionChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	if (WidthChanged || HeightChanged)
	{
		EvaluateResponsiveRules();
	}
}

void UDreamResponsiveBehaviour::Awake()
{
	Super::Awake();
	EvaluateResponsiveRules();
}

void UDreamResponsiveBehaviour::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	if (WidthChanged || HeightChanged)
	{
		EvaluateResponsiveRules();
	}
}

void UDreamResponsiveBehaviour::OnAttachmentChanged()
{
	Super::OnAttachmentChanged();
	BindParentDimensionEvent();
	EvaluateResponsiveRules();
}

void UDreamResponsiveBehaviour::CaptureBaselineAppearance(UDreamWidget* Widget)
{
	// Only the FIRST rule to take over captures, which is what makes the baseline mean "before any
	// rule spoke" rather than "before the rule that happens to be winning now". Moving between two
	// matching rules is not a moment at which the widget is wearing anything of its own.
	if (!bHasBaselineAppearance)
	{
		bHasBaselineAppearance = true;
		BaselineVisibility = Widget->GetVisibility();
		BaselineRenderOpacity = Widget->GetRenderOpacity();
	}
}

void UDreamResponsiveBehaviour::RestoreBaselineAppearance(UDreamWidget* Widget)
{
	// Nothing borrowed, nothing to give back. This is the guard that keeps a component whose rules
	// have never matched from writing anything at all -- see EvaluateResponsiveRules.
	if (bHasBaselineAppearance)
	{
		bHasBaselineAppearance = false;
		Widget->SetVisibility(BaselineVisibility);
		Widget->SetRenderOpacity(BaselineRenderOpacity);
	}
}

void UDreamResponsiveBehaviour::EvaluateResponsiveRules()
{
	UDreamWidget* Widget = GetWidget();
	if (!Widget)
	{
		return;
	}
	const FVector2D ReferenceSize = Widget->GetParent() ? Widget->GetParent()->GetSize() : Widget->GetSize();
	for (const FDreamResponsiveRule& Rule : Rules)
	{
		if (Rule.Matches(ReferenceSize))
		{
			CaptureBaselineAppearance(Widget);
			ActiveRule = Rule.Name;
			Widget->SetVisibility(Rule.Visibility);
			Widget->SetRenderOpacity(Rule.RenderOpacity);
			return;
		}
	}
	// What to do when NOTHING matches is a decision rather than an oversight, and this is the one
	// that was taken: the rules are overrides on top of the appearance the widget was authored with,
	// in the same sense that a media query is an override on top of a stylesheet's base
	// declaration. Releasing the last rule therefore means giving back what was there before any
	// rule spoke. Leaving the widget in the last match's costume was the previous behaviour and it
	// reads, from the outside, as "this control is mysteriously stuck hidden" -- because the usual
	// way to arrive here is a gap in the middle of a rule set, and a gap announces itself nowhere.
	//
	// The counter-intuitive half is that the baseline is captured lazily, when a rule first takes
	// over, rather than up front in Awake. That is deliberate: a component whose rules never match
	// must be indistinguishable from no component at all, and capturing early would give it
	// something to "restore" onto a widget it has never had any business touching. Capture on
	// takeover also brackets the ownership honestly -- while a rule is active this component owns
	// Visibility and RenderOpacity and will hand them back; while no rule is active the widget's own
	// values are authoritative, and whatever anything else sets becomes the next baseline.
	//
	// Two alternatives were weighed. An authored "otherwise" rule needs no code and still works: a
	// default-constructed FDreamResponsiveRule matches every size, so an author who wants an
	// explicit fallback appearance writes one as the last entry, and a matching catch-all is reached
	// long before this line. Merely logging a warning would have been worse than silence, because
	// with the restore in place "no rule matched" is an ordinary authoring intent -- "only override
	// on wide screens" -- so the warning would fire on correct usage, every time a parent resizes,
	// and teach everyone to ignore the log.
	ActiveRule = NAME_None;
	RestoreBaselineAppearance(Widget);
}
