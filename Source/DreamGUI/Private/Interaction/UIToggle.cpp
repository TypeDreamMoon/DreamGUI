// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"
#include "DreamTweenManager.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamImage.h"


UUIToggle* UUIToggleTransition::GetToggleComponent() const
{
	if (!IsValid(UIToggleComp))
	{
		UIToggleComp = GetWidget()->GetComponent<UUIToggle>();
	}
	return UIToggleComp;
}

void UUIToggleTransition::ToggleOn(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveToggleOn(InImmediateSet);
	}
}

void UUIToggleTransition::ToggleOff(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveToggleOff(InImmediateSet);
	}
}

UUIToggle::UUIToggle()
{
	OnColor = FColor(255, 255, 255, 255);
	OffColor = FColor(255, 255, 255, 0);
}
void UUIToggle::Awake()
{
	Super::Awake();
	//check toggle group
	if (!ToggleGroup.IsValid())
	{
		if (bAutoFindToggleGroupInParent)
		{
			ToggleGroup = GetWidget()->GetComponentInParent<UUIToggleGroup>();
		}
	}
	if (ToggleGroup.IsValid())
	{
		ToggleGroup->AddToggleComponent(this);
	}
}

void UUIToggle::Start()
{
	Super::Start();
	if (ToggleGroup.IsValid() && bIsOn)
	{
		ToggleGroup->SetSelection(this);//if default is selected, set to group
	}
	ApplyValueToVisual(true);
}

void UUIToggle::OnDestroy()
{
	Super::OnDestroy();
	if (ToggleGroup.IsValid())
	{
		ToggleGroup->RemoveToggleComponent(this);
	}
}

#if WITH_EDITOR
void UUIToggle::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIToggle, bIsOn))
		{
			ApplyValueToVisual(true);
		}
	}
}
#endif

void UUIToggle::SetValue(bool Value, bool SendCallback)
{
	if (bIsOn != Value)
	{
		if (ToggleGroup.IsValid())
		{
			if (ToggleGroup->GetAllowNoneSelected() == false && ToggleGroup->GetSelectedItem() == this && Value == false)//not allow none select
			{
				return;
			}
		}

		bIsOn = Value;
		if (ToggleGroup.IsValid())
		{
			if (bIsOn)
			{
				ToggleGroup->SetSelection(this);
			}
			else
			{
				if (ToggleGroup->GetSelectedItem() == this)
				{
					ToggleGroup->ClearSelection();
				}
			}
		}
		if (SendCallback)
		{
			OnValueChangedCPP.Broadcast(bIsOn);
			OnValueChangedBP.Broadcast(bIsOn);
			OnValueChanged.FireEvent(bIsOn);
		}

		ApplyValueToVisual(false);
	}
}
void UUIToggle::ApplyValueToVisual(bool immediateSet)
{
	if (ToggleTransitionType != EUISelectableTransitionType::Custom)
	{
		// Deliberately NOT defaulted to the widget's own visual the way UUISelectable's hover target
		// is: a toggle owns two transitions -- the selectable one (normal/hovered/pressed) and this
		// one (checked/unchecked) -- and pointing both at one visual makes them overwrite each
		// other, so the checked colour would survive only until the next hover event. The preset
		// Blueprints give them separate visuals (a backing plate and a tick), which is the design.
		// A text-authored toggle names its own tick now -- `ToggleTransitionTarget = Check` -- and a
		// code-authored one calls SetToggleTransitionTarget. Left unset it still returns rather than
		// falling back to the widget's own visual, because that fallback is the one that fights.
		if (!ToggleTransitionTarget.IsValid())return;
	}

	TOptional<FColor> Color;
	TOptional<FDreamUIImageBrush> Brush;
	if (ToggleTransitionType == EUISelectableTransitionType::Color)
	{
		Color = bIsOn ? OnColor : OffColor;
	}
	else if (ToggleTransitionType == EUISelectableTransitionType::ImageBrush)
	{
		Brush = bIsOn ? OnImageBrush : OffImageBrush;
	}
	else if (ToggleTransitionType == EUISelectableTransitionType::Custom)
	{
		if (CustomToggleTransition.IsValid())
		{
			if (bIsOn)
			{
				CustomToggleTransition->ToggleOn(immediateSet);
			}
			else
			{
				CustomToggleTransition->ToggleOff(immediateSet);
			}
		}
	}

	if (Color.IsSet())
	{
		if (ToggleDuration <= 0.0f || immediateSet)
		{
			ToggleTransitionTarget->SetColor(Color.GetValue());
		}
		else
		{
			if (UDreamTweenManager::IsTweening(this, ToggleTransitionTweener))ToggleTransitionTweener->Kill();
			ToggleTransitionTweener = UDreamTweenManager::To(ToggleTransitionTarget.Get()
				, FDreamTweenColorGetterFunction::CreateWeakLambda(ToggleTransitionTarget.Get(), [=, this]()
			{
				return ToggleTransitionTarget->GetColor();
			}), FDreamTweenColorSetterFunction::CreateUObject(ToggleTransitionTarget.Get(), &UDreamVisual::SetColor), Color.GetValue(), ToggleDuration);
			if (ToggleTransitionTweener)
			{
				UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), ToggleTransitionTweener);
			}
		}
	}
	if (Brush.IsSet())
	{
		if (auto ToggleTransitionTargetAsDreamImage = Cast<UDreamImage>(ToggleTransitionTarget.Get()))
		{
			if (IsValid(Brush.GetValue().GetResourceObject()))
			{
				ToggleTransitionTargetAsDreamImage->SetBrush(Brush.GetValue());
			}
			else
			{
				if (ToggleDuration <= 0.0f || immediateSet)
				{
					ToggleTransitionTargetAsDreamImage->SetBrushTintColor(Brush.GetValue().TintColor);
				}
				else
				{
					if (UDreamTweenManager::IsTweening(this, ToggleTransitionTweener))ToggleTransitionTweener->Kill();
					ToggleTransitionTweener = UDreamTweenManager::To(ToggleTransitionTargetAsDreamImage
						, FDreamTweenColorGetterFunction::CreateWeakLambda(ToggleTransitionTargetAsDreamImage, [=, this]()
					{
						return ToggleTransitionTargetAsDreamImage->GetBrush().TintColor;
					}), FDreamTweenColorSetterFunction::CreateUObject(ToggleTransitionTargetAsDreamImage, &UDreamImage::SetBrushTintColor), Brush.GetValue().TintColor, ToggleDuration);
					if (ToggleTransitionTweener)
					{
						UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), ToggleTransitionTweener);
					}
				}
			}
		}
	}
}

void UUIToggle::SetToggleTransitionTarget(UDreamVisual* Value)
{
	if (ToggleTransitionTarget != Value)
	{
		ToggleTransitionTarget = Value;
		// Immediately, and without the tween: a target handed over after the toggle already has a
		// value would otherwise show the unchecked colour until the next click.
		ApplyValueToVisual(false);
	}
}

void UUIToggle::SetOnColor(FColor Value)
{
	OnColor = Value;
	if (bIsOn)
	{
		ApplyValueToVisual(true);
	}
}

void UUIToggle::SetOffColor(FColor Value)
{
	OffColor = Value;
	if (!bIsOn)
	{
		ApplyValueToVisual(true);
	}
}

void UUIToggle::SetToggleGroup(UUIToggleGroup* InGroupComp)
{
	if (ToggleGroup != InGroupComp)
	{
		if (ToggleGroup.IsValid())
		{
			ToggleGroup->RemoveToggleComponent(this);
		}
		if (IsValid(InGroupComp))
		{
			InGroupComp->AddToggleComponent(this);
		}
		ToggleGroup = InGroupComp;
	}
}

void UUIToggle::SetValue(bool Value)
{
	SetValue(Value, true);
}

void UUIToggle::SetValueWithoutNotify(bool Value)
{
	SetValue(Value, false);
}

bool UUIToggle::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	if (!AcceptsPointerButton(EventData))
	{
		// SCheckBox toggles on the left button alone; any other one is passed on, as it leaves those
		// unhandled. See AcceptedMouseButtons.
		return true;
	}
	if (ShouldClickOnClick(EventData))
	{
		SetValue(!bIsOn);
	}
	return AllowEventBubbleUp;
}

bool UUIToggle::OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData)
{
	// SCheckBox's whole answer to a double click is its down, so this is the down: through the
	// interface, so it is exactly what a down dispatched to this component would have run -- the
	// button filter, the disabled test, OnPressed and a MouseDown click method's flip included.
	return IDreamPointerDownUpInterface::Execute_OnPointerDown(this, EventData);
}

bool UUIToggle::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	// Super FIRST, always: the base runs the pointer transition, so a handler on this signal sees a
	// selectable that has already moved into the state it is being told about.
	const bool bBubble = Super::OnPointerEnter_Implementation(EventData);
	OnHoveredCPP.Broadcast();
	return bBubble;
}

bool UUIToggle::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::OnPointerExit_Implementation(EventData);
	OnUnhoveredCPP.Broadcast();
	return bBubble;
}

bool UUIToggle::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	if (!AcceptsPointerButton(EventData))
	{
		// Not a press at all -- no pressed look, no selection, no OnPressed -- and passed on, exactly
		// as UUIButton treats a mouse button it does not answer.
		return true;
	}
	// The interactable test is asked here rather than read off the Super's return value, because that
	// value is the BUBBLING policy and says nothing about whether the press was honoured -- the same
	// reading UUIButton's press pair makes. A toggle drawn disabled must not speak.
	const bool bBubble = Super::OnPointerDown_Implementation(EventData);
	bPressAccepted = IsInteractable();
	if (bPressAccepted)
	{
		OnPressedCPP.Broadcast();
		if (ShouldClickOnDown(EventData))
		{
			// MouseDown / Touch Down / ButtonPress: the press IS the toggle. Same body the click
			// takes, so the value cannot move differently depending on which method was chosen.
			SetValue(!bIsOn);
		}
	}
	return bBubble;
}

bool UUIToggle::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	// Released only what was pressed, whichever button came up -- UUIButton::OnPointerUp states why.
	const bool bReleasesAPress = bPressAccepted;
	bPressAccepted = false;
	const bool bAnswered = AcceptsPointerButton(EventData);
	const bool bBubble = Super::OnPointerUp_Implementation(EventData);
	if (bReleasesAPress)
	{
		OnReleasedCPP.Broadcast();
	}
	if (bAnswered && IsInteractable() && ShouldClickOnUp(EventData))
	{
		SetValue(!bIsOn);
	}
	return bAnswered ? bBubble : true;
}

int32 UUIToggle::GetIndexInGroup()const
{
	if (ToggleGroup.IsValid())
	{
		return ToggleGroup->GetToggleIndex(this);
	}
	return -1;
}