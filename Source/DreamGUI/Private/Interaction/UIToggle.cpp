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
		// A text-authored toggle cannot express "the tick is that child" yet, so it has no checked
		// visual and this returns; closing that needs the language's intra-tree reference, not a
		// default that fights.
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
	SetValue(!bIsOn);
	return AllowEventBubbleUp;
}

int32 UUIToggle::GetIndexInGroup()const
{
	if (ToggleGroup.IsValid())
	{
		return ToggleGroup->GetToggleIndex(this);
	}
	return -1;
}