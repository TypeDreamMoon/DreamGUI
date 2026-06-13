// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/Components/LexLayout.h"

#include "LTweenBPLibrary.h"
#include "Core/Components/LexWidget.h"

void ULexLayout::ApplyWidgetWidth(ULexWidget* InWidget, const float& InWidth)
{
	InWidget->SetWidth(InWidth);
}
void ULexLayout::ApplyWidgetHeight(ULexWidget* InWidget, const float& InHeight)
{
	InWidget->SetHeight(InHeight);
}
void ULexLayout::ApplyWidgetAnchoredPosition(ULexWidget* InWidget, const FVector2D& InAnchoredPosition)
{
	InWidget->SetAnchoredPosition(InAnchoredPosition);
}
void ULexLayout::ApplyWidgetSizeDelta(ULexWidget* InWidget, const FVector2D& InSizedDelta)
{
	InWidget->SetSizeDelta(InSizedDelta);
}


ULexLayoutAnimationCustom::ULexLayoutAnimationCustom()
{
	bCanExecuteBlueprintEvent = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native);
}
void ULexLayoutAnimationCustom::BeginSetupAnimations()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveBeginSetupAnimations();
	}
}
void ULexLayoutAnimationCustom::ApplyAnchoredPositionAnimation(const FVector2D& Value, ULexWidget* Target)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveApplyAnchoredPositionAnimation(Value, Target);
	}
}
void ULexLayoutAnimationCustom::ApplyRotatorAnimation(const FQuat& Value, ULexWidget* Target)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveApplyRotatorAnimation(Value, Target);
	}
}
void ULexLayoutAnimationCustom::ApplyWidthAnimation(float Value, ULexWidget* Target)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveApplyWidthAnimation(Value, Target);
	}
}
void ULexLayoutAnimationCustom::ApplyHeightAnimation(float Value, ULexWidget* Target)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveApplyHeightAnimation(Value, Target);
	}
}
void ULexLayoutAnimationCustom::ApplySizeDeltaAnimation(const FVector2D& Value, ULexWidget* Target)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveApplySizeDeltaAnimation(Value, Target);
	}
}
void ULexLayoutAnimationCustom::EndSetupAnimations()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveEndSetupAnimations();
	}
}


void ULexLayoutAnimation::BeginSetupAnimations()
{
	if (AnimationType == ELexLayoutAnimationType::Custom && IsValid(CustomAnimation))
	{
		CustomAnimation->BeginSetupAnimations();
	}
	CancelAllAnimations(false);
}
void ULexLayoutAnimation::CancelAllAnimations(bool callComplete)
{
	if (TweenerArray.Num() > 0)
	{
		for (auto item : TweenerArray)
		{
			if (IsValid(item))
			{
				ULTweenBPLibrary::KillIfIsTweening(this, item, callComplete);
			}
		}
		TweenerArray.Reset();
	}
	bIsAnimationPlaying = false;
}

void ULexLayoutAnimation::EndSetupAnimations()
{
	if (AnimationType == ELexLayoutAnimationType::Custom && IsValid(CustomAnimation))
	{
		CustomAnimation->EndSetupAnimations();
	}
	bIsAnimationPlaying = true;
	auto Tweener = ULTweenManager::VirtualTo(this, AnimationDuration)->OnComplete(FSimpleDelegate::CreateWeakLambda(this, [this] {
		bIsAnimationPlaying = false;
		if (bShouldRebuildLayoutAfterAnimation)
		{
			bShouldRebuildLayoutAfterAnimation = false;
			ULexWidget::MarkLayoutForRebuild(GetWidget());
		}
		}));
	if (Tweener)
	{
		ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this->GetWidget(), Tweener);
		TweenerArray.Add(Tweener);
	}
}

void ULexLayoutAnimation::ApplyAnchoredPositionWithAnimation(ELexLayoutAnimationType tempAnimationType, FVector2D Value, ULexWidget* Target)
{
	switch (tempAnimationType)
	{
	default:
	case ELexLayoutAnimationType::Immediately:
	{
		ApplyWidgetAnchoredPosition(Target, Value);
	}
	break;
	case ELexLayoutAnimationType::EaseAnimation:
	{
		if (Target->GetAnchoredPosition() != Value)
		{
			if (auto Tweener = Target->AnchoredPositionTo(Value, AnimationDuration, 0, ELTweenEase::InOutSine))
			{
				Tweener->SetEase(ELTweenEase::InOutSine);
				ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this->GetWidget(), Tweener);
				TweenerArray.Add(Tweener);
			}
		}
	}
	break;
	case ELexLayoutAnimationType::Custom:
	{
		if (IsValid(CustomAnimation))
		{
			CustomAnimation->ApplyAnchoredPositionAnimation(Value, Target);
		}
		else
		{
			ApplyWidgetAnchoredPosition(Target, Value);
		}
	}
	break;
	}
}

void ULexLayoutAnimation::ApplyRotationWithAnimation(ELexLayoutAnimationType tempAnimationType, const FQuat& Value, ULexWidget* Target)
{
	switch (tempAnimationType)
	{
	default:
	case ELexLayoutAnimationType::Immediately:
	{
		Target->SetRelativeRotation(Value);
	}
	break;
	case ELexLayoutAnimationType::EaseAnimation:
	{
		if (Target->GetRelativeRotation() != Value)
		{
			if (auto Tweener = Target->LocalRotationQuaternionTo(Value, AnimationDuration, 0, ELTweenEase::InOutSine))
			{
				ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this->GetWidget(), Tweener);
				TweenerArray.Add(Tweener);
			}
		}
	}
	break;
	case ELexLayoutAnimationType::Custom:
	{
		if (IsValid(CustomAnimation))
		{
			CustomAnimation->ApplyRotatorAnimation(Value, Target);
		}
		else
		{
			Target->SetRelativeRotation(Value);
		}
	}
	break;
	}
}

void ULexLayoutAnimation::ApplyWidthWithAnimation(ELexLayoutAnimationType tempAnimationType, float Value, ULexWidget* Target)
{
	switch (tempAnimationType)
	{
	default:
	case ELexLayoutAnimationType::Immediately:
	{
		ApplyWidgetWidth(Target, Value);
	}
	break;
	case ELexLayoutAnimationType::EaseAnimation:
	{
		if (Target->GetWidth() != Value)
		{
			auto Tweener = ULTweenManager::To(Target
				, FLTweenFloatGetterFunction::CreateUObject(Target, &ULexWidget::GetWidth)
				, FLTweenFloatSetterFunction::CreateUObject(Target, &ULexWidget::SetWidth)
				, Value, AnimationDuration);
			if (Tweener)
			{
				Tweener->SetEase(ELTweenEase::InOutSine);
				ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this->GetWidget(), Tweener);
				TweenerArray.Add(Tweener);
			}
		}
	}
	break;
	case ELexLayoutAnimationType::Custom:
	{
		if (IsValid(CustomAnimation))
		{
			CustomAnimation->ApplyWidthAnimation(Value, Target);
		}
		else
		{
			ApplyWidgetWidth(Target, Value);
		}
	}
	break;
	}
}

void ULexLayoutAnimation::ApplyHeightWithAnimation(ELexLayoutAnimationType tempAnimationType, float Value, ULexWidget* Target)
{
	switch (tempAnimationType)
	{
	default:
	case ELexLayoutAnimationType::Immediately:
	{
		ApplyWidgetHeight(Target, Value);
	}
	break;
	case ELexLayoutAnimationType::EaseAnimation:
	{
		if (Target->GetHeight() != Value)
		{
			auto Tweener = ULTweenManager::To(Target
				, FLTweenFloatGetterFunction::CreateUObject(Target, &ULexWidget::GetHeight)
				, FLTweenFloatSetterFunction::CreateUObject(Target, &ULexWidget::SetHeight)
				, Value, AnimationDuration);
			if (Tweener)
			{
				Tweener->SetEase(ELTweenEase::InOutSine);
				ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this->GetWidget(), Tweener);
				TweenerArray.Add(Tweener);
			}
		}
	}
	break;
	case ELexLayoutAnimationType::Custom:
	{
		if (IsValid(CustomAnimation))
		{
			CustomAnimation->ApplyHeightAnimation(Value, Target);
		}
		else
		{
			ApplyWidgetHeight(Target, Value);
		}
	}
	break;
	}
}

void ULexLayoutAnimation::ApplySizeDeltaWithAnimation(ELexLayoutAnimationType TempAnimationType, FVector2D Value, ULexWidget* Target)
{
	switch (TempAnimationType)
	{
	default:
	case ELexLayoutAnimationType::Immediately:
	{
		ApplyWidgetSizeDelta(Target, Value);
	}
	break;
	case ELexLayoutAnimationType::EaseAnimation:
	{
		if (Target->GetSizeDelta() != Value)
		{
			auto Tweener = ULTweenManager::To(Target
				, FLTweenVector2DGetterFunction::CreateUObject(Target, &ULexWidget::GetSizeDelta)
				, FLTweenVector2DSetterFunction::CreateUObject(Target, &ULexWidget::SetSizeDelta)
				, Value, AnimationDuration);
			if (Tweener)
			{
				Tweener->SetEase(ELTweenEase::InOutSine);
				ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this->GetWidget(), Tweener);
				TweenerArray.Add(Tweener);
			}
		}
	}
	break;
	case ELexLayoutAnimationType::Custom:
	{
		if (IsValid(CustomAnimation))
		{
			CustomAnimation->ApplySizeDeltaAnimation(Value, Target);
		}
		else
		{
			ApplyWidgetSizeDelta(Target, Value);
		}
	}
	break;
	}
}

void ULexLayoutAnimation::SetAnimationType(ELexLayoutAnimationType Value)
{
	if (AnimationType != Value)
	{
		AnimationType = Value;
	}
}
void ULexLayoutAnimation::SetAnimationDuration(float Value)
{
	if (AnimationDuration != Value)
	{
		AnimationDuration = Value;
	}
}
void ULexLayoutAnimation::SetCustomAnimation(ULexLayoutAnimationCustom* Value)
{
	if (CustomAnimation != Value)
	{
		CustomAnimation = Value;
	}
}

void ULexLayoutContainer::PostReinitProperties()
{
	Super::PostReinitProperties();
#if WITH_EDITOR
	if (!this->GetName().StartsWith("Default__"))
	{
		if (auto Widget = GetWidget())
		{
			if (auto World = Widget->GetWorld())
			{
				if (!World->IsGameWorld())
				{
					ULexWidget::MarkLayoutForRebuild(Widget);
				}
			}
		}
	}
#endif
}

#if WITH_EDITOR
void ULexLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void ULexLayoutContainer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (!this->GetName().StartsWith("Default__"))
	{
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutSelf::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (!this->GetName().StartsWith("Default__"))
	{
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}
#endif

void ULexLayoutSelf::PostReinitProperties()
{
	Super::PostReinitProperties();
#if WITH_EDITOR
	if (!this->GetName().StartsWith("Default__"))
	{
		if (auto Widget = GetWidget())
		{
			if (auto World = Widget->GetWorld())
			{
				if (!World->IsGameWorld())
				{
					ULexWidget::MarkLayoutForRebuild(Widget);
				}
			}
		}
	}
#endif
}

void ULexLayoutSelf::SetIgnoreLayoutContainer(bool Value)
{
	if (bIgnoreLayoutContainer != Value)
	{
		bIgnoreLayoutContainer = Value;
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}
