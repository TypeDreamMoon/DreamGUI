// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UISliderComponent.h"
#include "LGUI.h"
#include "Core/Actor/LexWidgetActor.h"
#include "Core/Components/LexWidget.h"

void UUISliderComponent::Awake()
{
    Super::Awake();
}

void UUISliderComponent::Start()
{
    Super::Start();
    ApplyValueToUI();
}

bool UUISliderComponent::CheckFill()
{
    if (Fill.IsValid() && FillArea.IsValid())
        return true;
    if (!Fill.IsValid())
        return false;
    FillArea = Fill->GetUIParent();
    if (Fill.IsValid() && FillArea.IsValid())
        return true;
    return false;
}
bool UUISliderComponent::CheckHandle()
{
    if (Handle.IsValid() && HandleArea.IsValid())
        return true;
    if (!Handle.IsValid())
        return false;
    HandleArea = Handle->GetUIParent();
    if (HandleArea.IsValid())
        return true;
    return false;
}

#if WITH_EDITOR
void UUISliderComponent::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (WholeNumbers)
    {
        Value = FMath::FloorToFloat(Value);
    }
    Value = FMath::Clamp(Value, MinValue, MaxValue);
    HandleArea = nullptr;//force re-check
    FillArea = nullptr;//force re-check
    ApplyValueToUI();
}
#endif

void UUISliderComponent::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
    ApplyValueToUI();
}

void UUISliderComponent::SetValue(float InValue, bool FireEvent)
{
    InValue = FMath::Clamp(InValue, MinValue, MaxValue);
    if (Value != InValue)
    {
        Value = InValue;
        ApplyValueToUI();
        if (FireEvent)
        {
            OnValueChangedCPP.Broadcast(Value);
            OnValueChangedBP.Broadcast(Value);
            OnValueChanged.FireEvent((double)Value);
        }
    }
}

void UUISliderComponent::SetValue(float InValue)
{
    SetValue(InValue, true);
}

void UUISliderComponent::SetValueWithoutNotify(float InValue)
{
    SetValue(InValue, false);
}

void UUISliderComponent::SetMinValue(float InMinValue, bool KeepRelativeValue, bool FireEvent)
{
    if (MinValue != InMinValue)
    {
		float value01 = (Value - MinValue) / (MaxValue - MinValue);
		MinValue = InMinValue;
        if (KeepRelativeValue)
        {
            Value = value01 * (MaxValue - MinValue) + MinValue;
        }
        else
        {
            Value = FMath::Clamp(Value, MinValue, MaxValue);
        }
        ApplyValueToUI();
		if (FireEvent)
		{
			OnValueChangedCPP.Broadcast(Value);
			OnValueChanged.FireEvent((double)Value);
		}
    }
}
void UUISliderComponent::SetMaxValue(float InMaxValue, bool KeepRelativeValue, bool FireEvent)
{
	if (MaxValue != InMaxValue)
	{
		float value01 = (Value - MinValue) / (MaxValue - MinValue);
        MaxValue = InMaxValue;
		if (KeepRelativeValue)
		{
			Value = value01 * (MaxValue - MinValue) + MinValue;
		}
		else
		{
			Value = FMath::Clamp(Value, MinValue, MaxValue);
		}
		ApplyValueToUI();
		if (FireEvent)
		{
			OnValueChangedCPP.Broadcast(Value);
			OnValueChanged.FireEvent((double)Value);
		}
	}
}

void UUISliderComponent::SetNavigationChangeInterval(float InValue)
{
    NavigationChangeInterval = InValue;
}

bool UUISliderComponent::OnPointerDown_Implementation(ULexPointerEventData *eventData)
{
    Super::OnPointerDown_Implementation(eventData);
    if (eventData->inputType == ELexUIPointerInputType::Pointer)
    {
        CalculateInputValue(eventData);
    }
    return AllowEventBubbleUp;
}
bool UUISliderComponent::OnPointerUp_Implementation(ULexPointerEventData *eventData)
{
    Super::OnPointerUp_Implementation(eventData);
    return AllowEventBubbleUp;
}
bool UUISliderComponent::OnPointerBeginDrag_Implementation(ULexPointerEventData *eventData)
{
    CalculateInputValue(eventData);
    return AllowEventBubbleUp;
}
bool UUISliderComponent::OnPointerDrag_Implementation(ULexPointerEventData *eventData)
{
    CalculateInputValue(eventData);
    return AllowEventBubbleUp;
}
bool UUISliderComponent::OnPointerEndDrag_Implementation(ULexPointerEventData *eventData)
{
    CalculateInputValue(eventData);
    return AllowEventBubbleUp;
}
bool UUISliderComponent::OnNavigate_Implementation(ELexUINavigationDirection direction, TScriptInterface<ILexNavigationInterface>& result)
{
    float valueIntervalMultiply = 0.0f;
    if (
        (DirectionType == EUISliderDirectionType::LeftToRight && direction == ELexUINavigationDirection::Left)
        || (DirectionType == EUISliderDirectionType::RightToLeft && direction == ELexUINavigationDirection::Right)
        || (DirectionType == EUISliderDirectionType::BottomToTop && direction == ELexUINavigationDirection::Down)
        || (DirectionType == EUISliderDirectionType::TopToBottom && direction == ELexUINavigationDirection::Up))
    {
        valueIntervalMultiply = -NavigationChangeInterval;
    }
    else if (
        (DirectionType == EUISliderDirectionType::LeftToRight && direction == ELexUINavigationDirection::Right)
        || (DirectionType == EUISliderDirectionType::RightToLeft && direction == ELexUINavigationDirection::Left)
        || (DirectionType == EUISliderDirectionType::BottomToTop && direction == ELexUINavigationDirection::Up)
        || (DirectionType == EUISliderDirectionType::TopToBottom && direction == ELexUINavigationDirection::Down))
    {
        valueIntervalMultiply = NavigationChangeInterval;
    }
    if (valueIntervalMultiply == 0.0f)
    {
        return Super::OnNavigate_Implementation(direction, result);
    }
    else
    {
        auto tempValue = Value;
        tempValue += (MaxValue - MinValue) * valueIntervalMultiply;
        tempValue = FMath::Clamp(tempValue, MinValue, MaxValue);
        SetValue(tempValue);
        return false;
    }
}

void UUISliderComponent::CalculateInputValue(ULexPointerEventData *eventData)
{
    ULexWidget *mainUIItem = nullptr;
    ULexWidget *areaUIItem = nullptr;
    if (CheckHandle())
    {
        mainUIItem = Handle.Get();
        areaUIItem = HandleArea.Get();
    }
    else
    {
        if (CheckFill())
        {
            mainUIItem = Fill.Get();
            areaUIItem = FillArea.Get();
        }
    }
    if (mainUIItem != nullptr && areaUIItem != nullptr)
    {
        //calculate value to 0-1 range
        auto localPointerPosition = areaUIItem->GetComponentTransform().InverseTransformPosition(eventData->GetWorldPointInPlane());
        float MinPosition = 0;
        float value01 = 0;
        switch (DirectionType)
        {
        case EUISliderDirectionType::LeftToRight:
        {
            MinPosition = -areaUIItem->GetPivot().X * areaUIItem->GetWidth();
            value01 = (localPointerPosition.Y - MinPosition) / areaUIItem->GetWidth();
        }
        break;
        case EUISliderDirectionType::RightToLeft:
        {
            MinPosition = -areaUIItem->GetPivot().X * areaUIItem->GetWidth();
            value01 = 1.0f - (localPointerPosition.Y - MinPosition) / areaUIItem->GetWidth();
        }
        break;
        case EUISliderDirectionType::BottomToTop:
        {
            MinPosition = -areaUIItem->GetPivot().Y * areaUIItem->GetHeight();
            value01 = (localPointerPosition.Z - MinPosition) / areaUIItem->GetHeight();
        }
        break;
        case EUISliderDirectionType::TopToBottom:
        {
            MinPosition = -areaUIItem->GetPivot().Y * areaUIItem->GetHeight();
            value01 = 1.0f - (localPointerPosition.Z - MinPosition) / areaUIItem->GetHeight();
        }
        break;
        }
        value01 = FMath::Clamp(value01, 0.0f, 1.0f);
        float value = (MaxValue - MinValue) * value01 + MinValue;
        if (WholeNumbers)
        {
            value = FMath::FloorToFloat(value);
        }
        SetValue(value, true);
    }
}
void UUISliderComponent::ApplyValueToUI()
{
    float value01 = (Value - MinValue) / (MaxValue - MinValue);
    value01 = FMath::Clamp(value01, 0.0f, 1.0f);

    if (CheckHandle() || CheckFill())
    {
        switch (DirectionType)
        {
        case EUISliderDirectionType::LeftToRight:
        {
            if (CheckHandle())
            {
                Handle->SetHorizontalAnchorMinMax(FVector2D(value01, value01));
            }
            if (CheckFill())
            {
                Fill->SetHorizontalAnchorMinMax(FVector2D(0, value01));
            }
        }
        break;
        case EUISliderDirectionType::RightToLeft:
        {
            if (CheckHandle())
            {
                float invValue01 = 1.0f - value01;
                Handle->SetHorizontalAnchorMinMax(FVector2D(invValue01, invValue01));
            }
            if (CheckFill())
            {
                Fill->SetHorizontalAnchorMinMax(FVector2D(1.0f - value01, 1));
            }
        }
        break;
        case EUISliderDirectionType::BottomToTop:
        {
            if (CheckHandle())
            {
                Handle->SetVerticalAnchorMinMax(FVector2D(value01, value01));
            }
            if (CheckFill())
            {
                Fill->SetVerticalAnchorMinMax(FVector2D(0, value01));
            }
        }
        break;
        case EUISliderDirectionType::TopToBottom:
        {
            if (CheckHandle())
            {
                float invValue01 = 1.0f - value01;
                Handle->SetVerticalAnchorMinMax(FVector2D(invValue01, invValue01));
            }
            if (CheckFill())
            {
                Fill->SetVerticalAnchorMinMax(FVector2D(1.0f - value01, 1));
            }
        }
        break;
        }
    }
}