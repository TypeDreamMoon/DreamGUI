// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UIScrollbar.h"

#include "DreamGUI.h"
#include "Core/Components/DreamWidget.h"

UUIScrollbar::UUIScrollbar()
{
}

void UUIScrollbar::Awake()
{
    Super::Awake();
}

void UUIScrollbar::Start()
{
    Super::Start();
    ApplyValueToVisual();
}

bool UUIScrollbar::CheckHandle()
{
    if (!Handle.IsValid())
    {
        HandleArea.Reset();
        return false;
    }
    if (!HandleArea.IsValid())
    {
        HandleArea = Handle->GetParent();
    }
    return HandleArea.IsValid();
}

#if WITH_EDITOR
void UUIScrollbar::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    HandleArea = nullptr;//force re-check
    ApplyValueToVisual();
}
#endif

void UUIScrollbar::OnEnable()
{
    Super::OnEnable();
    ApplyValueToVisual();
}
void UUIScrollbar::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
    ApplyValueToVisual();
}
void UUIScrollbar::OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
    // NEVER for the handle: the handle's rect is this component's own OUTPUT, so hearing about it is
    // never news -- and acting on it re-enters ApplyValueToVisual from inside the very setter that
    // is still broadcasting. The equality gates in the anchor setters stop it at depth two rather
    // than letting it run away, but "stopped by a gate" is not the same as "did not happen": every
    // handle placement would do its work twice, the second time underneath the first.
    if (Child == Handle)
    {
        return;
    }
    // A track authored one level in, on the other hand, IS news: the handle's length is read off the
    // AREA, and an area that grew without this component hearing about it would leave the handle at
    // the length it had when the bar was narrower. The dimension event above only covers the widget
    // this component sits on.
    if (WidthChanged || HeightChanged)
    {
        ApplyValueToVisual();
    }
}

bool UUIScrollbar::IsHorizontal() const
{
    return DirectionType == EUIScrollbarDirectionType::LeftToRight
        || DirectionType == EUIScrollbarDirectionType::RightToLeft;
}

bool UUIScrollbar::IsReversed() const
{
    // These two put the ZERO end at the far edge. The same pair the old ratio-anchor maths singled
    // out, which is what keeps a value written before this rewrite meaning what it meant.
    return DirectionType == EUIScrollbarDirectionType::RightToLeft
        || DirectionType == EUIScrollbarDirectionType::TopToBottom;
}

float UUIScrollbar::GetHandleAreaLength() const
{
    // Resolved without caching, so this stays const and stays callable before Awake: the area is the
    // handle's parent, which is knowable the moment the handle is.
    const UDreamWidget* Area = HandleArea.IsValid()
        ? HandleArea.Get()
        : (Handle.IsValid() ? Handle->GetParent() : nullptr);
    if (Area == nullptr)
    {
        return 0.0f;
    }
    return IsHorizontal() ? Area->GetWidth() : Area->GetHeight();
}

float UUIScrollbar::GetEffectiveSize() const
{
    float Fraction = FMath::Clamp(Size, 0.0f, 1.0f);
    const float AreaLength = GetHandleAreaLength();
    if (AreaLength > KINDA_SMALL_NUMBER && MinHandleSize > 0.0f)
    {
        Fraction = FMath::Clamp(FMath::Max(Fraction, MinHandleSize / AreaLength), 0.0f, 1.0f);
    }
    return Fraction;
}

float UUIScrollbar::GetHandleTravel() const
{
    const float AreaLength = GetHandleAreaLength();
    return FMath::Max(AreaLength - GetEffectiveSize() * AreaLength, 0.0f);
}

void UUIScrollbar::SetValue(float InValue, bool FireEvent)
{
    // Clamped BEFORE the comparison, which is the whole of a defect this shipped with: the old form
    // compared the raw argument, so SetValue(1.7) against a held 1.0 passed the gate, clamped to
    // 1.0, assigned no change -- and broadcast one. Every listener downstream heard a value move
    // that never happened.
    InValue = FMath::Clamp(InValue, 0.0f, 1.0f);
    if (Value == InValue)
    {
        return;
    }
    Value = InValue;
    ApplyValueToVisual();
    if (FireEvent)
    {
        OnValueChangedCPP.Broadcast(Value);
        OnValueChangedBP.Broadcast(Value);
        OnValueChanged.FireEvent((double)Value);
    }
}

void UUIScrollbar::SetValue(float InValue)
{
    SetValue(InValue, true);
}

void UUIScrollbar::SetValueWithoutNotify(float InValue)
{
    SetValue(InValue, false);
}

void UUIScrollbar::SetSize(float InSize)
{
    InSize = FMath::Clamp(InSize, 0.0f, 1.0f);
    if (Size == InSize)
    {
        return;
    }
    Size = InSize;
    ApplyValueToVisual();
}

void UUIScrollbar::SetValueAndSize(float InValue, float InSize, bool FireEvent)
{
    InValue = FMath::Clamp(InValue, 0.0f, 1.0f);
    InSize = FMath::Clamp(InSize, 0.0f, 1.0f);
    const bool bValueChanged = (Value != InValue);
    const bool bSizeChanged = (Size != InSize);
    if (!bValueChanged && !bSizeChanged)
    {
        return;
    }
    Value = InValue;
    Size = InSize;
    ApplyValueToVisual();
    // Only when the VALUE moved, and on all three delegates. The old form fired on a size-only
    // change (a scroll view whose content merely grew), and it skipped the Blueprint one -- so a
    // Blueprint listener and a C++ listener on the same bar disagreed about what had happened.
    if (FireEvent && bValueChanged)
    {
        OnValueChangedCPP.Broadcast(Value);
        OnValueChangedBP.Broadcast(Value);
        OnValueChanged.FireEvent((double)Value);
    }
}
void UUIScrollbar::SetNavigationChangeInterval(float InValue)
{
    NavigationChangeInterval = FMath::Clamp(InValue, 0.0f, 1.0f);
}

void UUIScrollbar::SetMinHandleSize(float InValue)
{
    InValue = FMath::Max(0.0f, InValue);
    if (MinHandleSize == InValue)
    {
        return;
    }
    MinHandleSize = InValue;
    ApplyValueToVisual();
}

void UUIScrollbar::SetHandle(UDreamWidget* InHandle)
{
    Handle = InHandle;
    // Re-derived rather than left to the lazy path: a re-pointed handle measured against the old
    // one's parent is a bar whose value means something different from one frame to the next.
    HandleArea = InHandle != nullptr ? InHandle->GetParent() : nullptr;
    ApplyValueToVisual();
}

void UUIScrollbar::SetDirectionType(EUIScrollbarDirectionType InDirection)
{
    if (DirectionType == InDirection)
    {
        return;
    }
    DirectionType = InDirection;
    ApplyValueToVisual();
}

float UUIScrollbar::ProjectPointerOntoAxis(const FVector& InWorldPoint) const
{
    const UDreamWidget* Area = HandleArea.IsValid()
        ? HandleArea.Get()
        : (Handle.IsValid() ? Handle->GetParent() : nullptr);
    if (Area == nullptr)
    {
        return 0.0f;
    }
    const FVector InArea = Area->GetWorldTransform().InverseTransformPosition(InWorldPoint);
    // Distance from the area's START edge -- left for a horizontal bar, bottom for a vertical one --
    // which is the same origin the handle's own offset is measured from.
    return IsHorizontal()
        ? static_cast<float>(InArea.Y) - Area->GetLocalSpaceLeft()
        : static_cast<float>(InArea.Z) - Area->GetLocalSpaceBottom();
}

/**
 * Clicking the track pages by one handle-length toward the pointer.
 *
 * Slate's and Unity's answer both, and it replaces an arithmetic that divided by
 * AreaLength * (1 - Size): a bar whose content exactly fits has Size 1, so that denominator is
 * zero, and one click on such a bar wrote a NaN into Value that every later clamp preserved.
 */
bool UUIScrollbar::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
    Super::OnPointerDown_Implementation(EventData);
    if (EventData == nullptr || EventData->InputType != EDreamUIPointerInputType::Pointer)
    {
        return AllowEventBubbleUp;
    }
    if (!CheckHandle() || EventData->EnterWidget == Handle)
    {
        return AllowEventBubbleUp;
    }
    const float Travel = GetHandleTravel();
    if (Travel <= KINDA_SMALL_NUMBER)
    {
        // The handle fills the track: there is nowhere to page to, and nothing to divide by.
        return AllowEventBubbleUp;
    }
    const float Page = FMath::Max(GetEffectiveSize(), KINDA_SMALL_NUMBER);
    const float HandleStart = (IsReversed() ? 1.0f - Value : Value) * Travel;
    const float HandleLength = GetEffectiveSize() * GetHandleAreaLength();
    const float Pointer = ProjectPointerOntoAxis(EventData->WorldPoint);

    float Step = 0.0f;
    if (Pointer > HandleStart + HandleLength)
    {
        Step = Page;
    }
    else if (Pointer < HandleStart)
    {
        Step = -Page;
    }
    if (Step != 0.0f)
    {
        // The step is stated in TRACK direction; which way that moves the value is the direction's
        // business, and reversing it here is what keeps all four directions paging toward the click.
        SetValue(Value + (IsReversed() ? -Step : Step), true);
    }
    return AllowEventBubbleUp;
}
bool UUIScrollbar::OnPointerUp_Implementation(UDreamPointerEventData *EventData)
{
    Super::OnPointerUp_Implementation(EventData);
    return AllowEventBubbleUp;
}
bool UUIScrollbar::OnPointerBeginDrag_Implementation(UDreamPointerEventData *EventData)
{
    PressValue = Value;
    CalculateInputValue(EventData);
    return AllowEventBubbleUp;
}
bool UUIScrollbar::OnPointerDrag_Implementation(UDreamPointerEventData *EventData)
{
    CalculateInputValue(EventData);
    return AllowEventBubbleUp;
}
bool UUIScrollbar::OnPointerEndDrag_Implementation(UDreamPointerEventData *EventData)
{
    CalculateInputValue(EventData);
    return AllowEventBubbleUp;
}
bool UUIScrollbar::OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)
{
    float valueIntervalMultiply = 0.0f;
    if (
        (DirectionType == EUIScrollbarDirectionType::LeftToRight && direction == EDreamUINavigationDirection::Left) || (DirectionType == EUIScrollbarDirectionType::RightToLeft && direction == EDreamUINavigationDirection::Right) || (DirectionType == EUIScrollbarDirectionType::BottomToTop && direction == EDreamUINavigationDirection::Down) || (DirectionType == EUIScrollbarDirectionType::TopToBottom && direction == EDreamUINavigationDirection::Up))
    {
        valueIntervalMultiply = -NavigationChangeInterval;
    }
    else if (
        (DirectionType == EUIScrollbarDirectionType::LeftToRight && direction == EDreamUINavigationDirection::Right) || (DirectionType == EUIScrollbarDirectionType::RightToLeft && direction == EDreamUINavigationDirection::Left) || (DirectionType == EUIScrollbarDirectionType::BottomToTop && direction == EDreamUINavigationDirection::Up) || (DirectionType == EUIScrollbarDirectionType::TopToBottom && direction == EDreamUINavigationDirection::Down))
    {
        valueIntervalMultiply = NavigationChangeInterval;
    }
    if (valueIntervalMultiply == 0.0f)
    {
        return Super::OnNavigate_Implementation(direction, result);
    }
    SetValue(Value + valueIntervalMultiply);
    return false;
}

/**
 * A drag moves the value by how far the pointer travelled OVER THE TRAVEL, not over the track: the
 * handle's own length is space it can never reach, so dividing by the track made every drag lag the
 * pointer by a factor of (1 - Size). The guard above the division is the second half of the NaN the
 * track-click arithmetic used to produce -- both denominators are the same quantity.
 */
void UUIScrollbar::CalculateInputValue(UDreamPointerEventData *EventData)
{
    if (EventData == nullptr || !CheckHandle())
    {
        return;
    }
    const float Travel = GetHandleTravel();
    if (Travel <= KINDA_SMALL_NUMBER)
    {
        return;
    }
    auto localCumulativeMoveDelta = EventData->PressWorldToLocalTransform.TransformVector(
        EventData->GetWorldPointInPlane() - EventData->PressWorldPoint);
    const float AlongAxis = IsHorizontal()
        ? static_cast<float>(localCumulativeMoveDelta.Y)
        : static_cast<float>(localCumulativeMoveDelta.Z);
    const float Moved = AlongAxis / Travel;
    SetValue(PressValue + (IsReversed() ? -Moved : Moved), true);
}

/**
 * The handle's rect: absolute numbers, point anchor on the area's start edge, pivot on that edge.
 *
 * See the class comment for why this is not the ratio anchor it used to be. The pivot sitting on the
 * start edge is what makes the length grow the way the ratio version drew it, so a bar that was
 * authored against the old shape looks unchanged.
 */
void UUIScrollbar::ApplyValueToVisual()
{
    if (!CheckHandle())
    {
        return;
    }
    UDreamWidget* HandleWidget = Handle.Get();
    UDreamWidget* Area = HandleArea.Get();
    const bool bHorizontal = IsHorizontal();
    const float AreaLength = bHorizontal ? Area->GetWidth() : Area->GetHeight();
    const float AreaThickness = bHorizontal ? Area->GetHeight() : Area->GetWidth();
    const float Length = FMath::Clamp(GetEffectiveSize() * AreaLength, 0.0f, FMath::Max(AreaLength, 0.0f));
    const float Travel = FMath::Max(AreaLength - Length, 0.0f);
    const float Offset = (IsReversed() ? 1.0f - FMath::Clamp(Value, 0.0f, 1.0f) : FMath::Clamp(Value, 0.0f, 1.0f)) * Travel;

    if (bHorizontal)
    {
        HandleWidget->SetPivot(FVector2D(0.0, 0.5));
        HandleWidget->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.5), FVector2D(0.0, 0.5), false, false);
        HandleWidget->SetAnchoredPositionAndSizeDelta(FVector2D(Offset, 0.0), FVector2D(Length, AreaThickness));
    }
    else
    {
        HandleWidget->SetPivot(FVector2D(0.5, 0.0));
        HandleWidget->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.0), FVector2D(0.5, 0.0), false, false);
        HandleWidget->SetAnchoredPositionAndSizeDelta(FVector2D(0.0, Offset), FVector2D(AreaThickness, Length));
    }
}
