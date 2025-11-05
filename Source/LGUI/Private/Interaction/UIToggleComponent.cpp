// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIToggleComponent.h"
#include "Interaction/UIToggleGroupComponent.h"
#include "LTweenManager.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexVisual.h"
#include "Core/LexUISettings.h"
#include "Core/Components/LexImage.h"


UUIToggleComponent::UUIToggleComponent()
{
	OnColor = FColor(255, 255, 255, 255);
	OffColor = FColor(255, 255, 255, 0);
}
void UUIToggleComponent::Awake()
{
	Super::Awake();
	CheckTarget();
	//check toggle group
	if (ToggleGroup.IsValid())
	{
		ToggleGroup->AddToggleComponent(this);
	}
}

void UUIToggleComponent::Start()
{
	Super::Start();
	if (ToggleGroup.IsValid() && bIsOn)
	{
		ToggleGroup->SetSelection(this);//if default is selected, set to group
	}
	ApplyValueToUI(true);
}

void UUIToggleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	if (ToggleGroup.IsValid())
	{
		ToggleGroup->RemoveToggleComponent(this);
	}
}

#if WITH_EDITOR
void UUIToggleComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.MemberProperty)
	{
		auto PropertyName = Property->GetFName();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIToggleComponent, bIsOn))
		{
			ApplyValueToUI(true);
		}
	}
}
#endif

bool UUIToggleComponent::CheckTarget()
{
	if (ToggleTransitionTarget.IsValid())return true;
	return false;
}

void UUIToggleComponent::SetValue(bool Value, bool SendCallback)
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

		ApplyValueToUI(false);
	}
}
void UUIToggleComponent::ApplyValueToUI(bool immediateSet)
{
	if (!CheckTarget())return;
	if (ToggleTransition != EUISelectableTransitionType::Custom)
	{
		if (!ToggleTransitionTarget.IsValid())return;
	}

	TOptional<FColor> Color;
	TOptional<FLexUIImageBrush> Brush;
	if (ToggleTransition == EUISelectableTransitionType::Color)
	{
		Color = bIsOn ? OnColor : OffColor;
	}
	else if (ToggleTransition == EUISelectableTransitionType::ImageBrush)
	{
		Brush = bIsOn ? OnImageBrush : OffImageBrush;
	}
	else if (ToggleTransition == EUISelectableTransitionType::Custom)
	{
#if WITH_EDITOR
		if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
		{
			if (IsValid(CustomToggleTransition))
			{
				CustomToggleTransition->OnStartCustomTransition(bIsOn ? OnTransitionName : OffTransitionName, immediateSet);
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
			if (ULTweenManager::IsTweening(this, ToggleTransitionTweener))ToggleTransitionTweener->Kill();
			ToggleTransitionTweener = ULTweenManager::To(ToggleTransitionTarget.Get()
				, FLTweenColorGetterFunction::CreateWeakLambda(ToggleTransitionTarget.Get(), [=, this]()
			{
				return ToggleTransitionTarget->GetColor();
			}), FLTweenColorSetterFunction::CreateUObject(ToggleTransitionTarget.Get(), &ULexVisual::SetColor), Color.GetValue(), ToggleDuration);
			if (ToggleTransitionTweener)
			{
				bool bAffectByGamePause = false;
				bool bAffectByTimeDilation = false;
				if (this->GetWidget()->IsScreenSpaceOverlayUI())
				{
					bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
					bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
				}
				else
				{
					bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
					bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
				}
				ToggleTransitionTweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
			}
		}
	}
	if (Brush.IsSet())
	{
		if (auto ToggleTransitionTargetAsLexImage = Cast<ULexImage>(ToggleTransitionTarget.Get()))
		{
			if (IsValid(Brush.GetValue().GetResourceObject()))
			{
				ToggleTransitionTargetAsLexImage->SetBrush(Brush.GetValue());
			}
			else
			{
				if (ToggleDuration <= 0.0f || immediateSet)
				{
					ToggleTransitionTargetAsLexImage->SetBrushTintColor(Brush.GetValue().TintColor);
				}
				else
				{
					if (ULTweenManager::IsTweening(this, ToggleTransitionTweener))ToggleTransitionTweener->Kill();
					ToggleTransitionTweener = ULTweenManager::To(ToggleTransitionTargetAsLexImage
						, FLTweenColorGetterFunction::CreateWeakLambda(ToggleTransitionTargetAsLexImage, [=, this]()
					{
						return ToggleTransitionTargetAsLexImage->GetBrush().TintColor;
					}), FLTweenColorSetterFunction::CreateUObject(ToggleTransitionTargetAsLexImage, &ULexImage::SetBrushTintColor), Brush.GetValue().TintColor, ToggleDuration);
					if (ToggleTransitionTweener)
					{
						bool bAffectByGamePause = false;
						bool bAffectByTimeDilation = false;
						if (this->GetWidget()->IsScreenSpaceOverlayUI())
						{
							bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
							bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
						}
						else
						{
							bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
							bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
						}
						ToggleTransitionTweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
					}
				}
			}
		}
	}
}

void UUIToggleComponent::SetToggleGroup(UUIToggleGroupComponent* InGroupComp)
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

void UUIToggleComponent::SetValue(bool Value)
{
	SetValue(Value, true);
}

void UUIToggleComponent::SetValueWithoutNotify(bool Value)
{
	SetValue(Value, false);
}

bool UUIToggleComponent::OnPointerClick_Implementation(ULexPointerEventData* eventData)
{
	SetValue(!bIsOn);
	return AllowEventBubbleUp;
}

int32 UUIToggleComponent::GetIndexInGroup()const
{
	if (ToggleGroup.IsValid())
	{
		return ToggleGroup->GetToggleIndex(this);
	}
	return -1;
}