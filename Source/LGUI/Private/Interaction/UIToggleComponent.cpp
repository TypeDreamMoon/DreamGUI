// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIToggleComponent.h"
#include "Interaction/UIToggleGroupComponent.h"
#include "LTweenManager.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexVisual.h"
#include "Core/LGUISettings.h"
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
	if (ToggleTransition != ELexUISelectableTransitionType::Custom)
	{
		if (!ToggleTransitionTarget.IsValid())return;
	}

	TOptional<FColor> Color;
	TOptional<FLexUIImageBrush> Brush;
	if (ToggleTransition == ELexUISelectableTransitionType::Color)
	{
		Color = bIsOn ? OnColor : OffColor;
	}
	else if (ToggleTransition == ELexUISelectableTransitionType::ImageBrush)
	{
		Brush = bIsOn ? OnImageBrush : OffImageBrush;
	}
	else if (ToggleTransition == ELexUISelectableTransitionType::Custom)
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
			if (ULTweenManager::IsTweening(this, TransitionTweener))TransitionTweener->Kill();
			TransitionTweener = ULTweenManager::To(ToggleTransitionTarget.Get()
				, FLTweenColorGetterFunction::CreateWeakLambda(ToggleTransitionTarget.Get(), [=, this]()
			{
				return ToggleTransitionTarget->GetColor();
			}), FLTweenColorSetterFunction::CreateUObject(ToggleTransitionTarget.Get(), &ULexVisual::SetColor), Color.GetValue(), ToggleDuration);
			if (TransitionTweener)
			{
				bool bAffectByGamePause = false;
				bool bAffectByTimeDilation = false;
				if (this->GetLexWidget()->IsScreenSpaceOverlayUI())
				{
					bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
					bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
				}
				else
				{
					bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
					bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
				}
				TransitionTweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
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
					if (ULTweenManager::IsTweening(this, TransitionTweener))TransitionTweener->Kill();
					TransitionTweener = ULTweenManager::To(ToggleTransitionTargetAsLexImage
						, FLTweenColorGetterFunction::CreateWeakLambda(ToggleTransitionTargetAsLexImage, [=, this]()
					{
						return ToggleTransitionTargetAsLexImage->GetBrush().TintColor;
					}), FLTweenColorSetterFunction::CreateUObject(ToggleTransitionTargetAsLexImage, &ULexImage::SetBrushTintColor), Brush.GetValue().TintColor, ToggleDuration);
					if (TransitionTweener)
					{
						bool bAffectByGamePause = false;
						bool bAffectByTimeDilation = false;
						if (this->GetLexWidget()->IsScreenSpaceOverlayUI())
						{
							bAffectByGamePause = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByGamePause;
							bAffectByTimeDilation = GetDefault<ULGUISettings>()->bScreenSpaceUIAffectByTimeDilation;
						}
						else
						{
							bAffectByGamePause = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByGamePause;
							bAffectByTimeDilation = GetDefault<ULGUISettings>()->bWorldSpaceUIAffectByTimeDilation;
						}
						TransitionTweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
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

bool UUIToggleComponent::OnPointerClick_Implementation(ULGUIPointerEventData* eventData)
{
	SetValue(!bIsOn);
	return AllowEventBubbleUp;
}

FDelegateHandle UUIToggleComponent::RegisterToggleEvent(const FLGUIBoolDelegate& InDelegate)
{
	return OnValueChangedCPP.Add(InDelegate);
}
FDelegateHandle UUIToggleComponent::RegisterToggleEvent(const TFunction<void(bool)>& InFunction)
{
	return OnValueChangedCPP.AddLambda(InFunction);
}
void UUIToggleComponent::UnregisterToggleEvent(const FDelegateHandle& InHandle)
{
	OnValueChangedCPP.Remove(InHandle);
}

FLGUIDelegateHandleWrapper UUIToggleComponent::RegisterToggleEvent(const FUIToggleValueChangedDelegate& InDelegate)
{
	auto delegateHandle = OnValueChangedCPP.AddLambda([InDelegate](bool InIsOn) {
		InDelegate.ExecuteIfBound(InIsOn);
	});
	return FLGUIDelegateHandleWrapper(delegateHandle);
}
void UUIToggleComponent::UnregisterToggleEvent(const FLGUIDelegateHandleWrapper& InDelegateHandle)
{
	OnValueChangedCPP.Remove(InDelegateHandle.DelegateHandle);
}

int32 UUIToggleComponent::GetIndexInGroup()const
{
	if (ToggleGroup.IsValid())
	{
		return ToggleGroup->GetToggleIndex(this);
	}
	return -1;
}