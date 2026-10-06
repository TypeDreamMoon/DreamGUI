// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UISlider.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamUIInputTypes.h"

namespace UISliderLocal
{
    /** A finger's press, as the list asks it: a pointer event on one of the touch pointer ids (DreamUIPointerIds). */
    bool IsFinger(const UDreamPointerEventData* InEventData)
    {
        return InEventData != nullptr && InEventData->InputType == EDreamUIPointerInputType::Pointer
            && DreamUIPointerIds::IsTouch(InEventData->PointerID);
    }
}

UUISlider::UUISlider()
{
	// Moved by the pointer and by its value, never by the frame; its parts are placed when their areas are laid out.
	DeclareTickUnused(StaticClass());
	DeclareTransformChangedUnused(StaticClass());
}

void UUISlider::Awake()
{
    Super::Awake();
}

void UUISlider::Start()
{
    Super::Start();
    ApplyValueToVisual();
}

bool UUISlider::CheckFill()
{
    if (Fill.IsValid() && FillArea.IsValid())
        return true;
    if (!Fill.IsValid())
        return false;
    FillArea = Fill->GetParent();
    if (Fill.IsValid() && FillArea.IsValid())
        return true;
    return false;
}
bool UUISlider::CheckHandle()
{
    if (Handle.IsValid() && HandleArea.IsValid())
        return true;
    if (!Handle.IsValid())
        return false;
    HandleArea = Handle->GetParent();
    if (HandleArea.IsValid())
        return true;
    return false;
}

#if WITH_EDITOR
void UUISlider::PostEditChangeProperty(FPropertyChangedEvent &PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    Value = ConstrainValue(Value);
    HandleArea = nullptr;//force re-check
    FillArea = nullptr;//force re-check
    ApplyValueToVisual();
}
#endif

void UUISlider::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
    ApplyValueToVisual();
}

void UUISlider::OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
    // NEVER for the two parts this component places: their rects are its own OUTPUT, so hearing about
    // them is not news, and acting on it re-enters ApplyValueToVisual from inside the setter that is
    // still broadcasting. UUIScrollbar draws the same line around its handle, in the same words.
    if (Child == Fill || Child == Handle)
    {
        return;
    }
    // The AREAS are news. Both rects are absolute numbers read off the area they move in -- the fill's
    // length and both parts' cross-axis extent -- so an area that was re-arranged without this
    // component hearing about it would leave them at the size the control had before it was resized.
    // The dimension event above only covers the widget this component sits on.
    if (WidthChanged || HeightChanged)
    {
        ApplyValueToVisual();
    }
}

void UUISlider::OnDisable()
{
    Super::OnDisable();
    // Asleep with the stick held: the hold ends with its capture-end, rather than greeting the next visit already
    // taken.
    SetControllerCaptured(false);
}

void UUISlider::OnInteractableChanged(bool IsEnabled)
{
    Super::OnInteractableChanged(IsEnabled);
    if (!IsInteractable())
    {
        // A slider the player can no longer reach cannot go on holding the stick: the capture ends here
        // rather than surviving the disable and greeting the next visit already taken.
        SetControllerCaptured(false);
    }
}

bool UUISlider::IsHorizontal() const
{
    return DirectionType == EUISliderDirectionType::LeftToRight
        || DirectionType == EUISliderDirectionType::RightToLeft;
}

bool UUISlider::IsReversed() const
{
    // These two put the ZERO end at the far edge -- exactly the pair the old ratio-anchor maths
    // singled out, which is what keeps a value authored before this rewrite meaning what it meant.
    return DirectionType == EUISliderDirectionType::RightToLeft
        || DirectionType == EUISliderDirectionType::TopToBottom;
}

float UUISlider::GetValue01() const
{
    // Guarded, because MinValue and MaxValue are both authorable and both default to numbers a
    // "locked" slider is routinely left at: an empty span made this 0/0, FMath::Clamp returns NaN for
    // NaN (both comparisons are false), and the NaN went straight into an anchor.
    const float Span = MaxValue - MinValue;
    if (Span <= KINDA_SMALL_NUMBER && Span >= -KINDA_SMALL_NUMBER)
    {
        return 0.0f;
    }
    return FMath::Clamp((Value - MinValue) / Span, 0.0f, 1.0f);
}

void UUISlider::SetFill(UDreamWidget* InFill)
{
	if (Fill != InFill)
	{
		Fill = InFill;
		FillArea = nullptr;//force re-check
		ApplyValueToVisual();
	}
}

void UUISlider::SetHandle(UDreamWidget* InHandle)
{
	if (Handle != InHandle)
	{
		Handle = InHandle;
		HandleArea = nullptr;//force re-check
		ApplyValueToVisual();
	}
}

void UUISlider::SetDirectionType(EUISliderDirectionType InDirection)
{
	if (DirectionType != InDirection)
	{
		DirectionType = InDirection;
		ApplyValueToVisual();
	}
}

float UUISlider::ConstrainValue(float InValue) const
{
    if (WholeNumbers)
    {
        // The rule lives here, on the one road every value takes, rather than where a drag becomes a
        // value: there, a gamepad step, a SetValue and an authored number all went round it, and a
        // whole-number slider stepped by a quarter of 0..10 said 2.5. To the NEAREST whole number, so
        // each one owns an equal stretch of the track -- rounding down left the top value nothing but
        // the very end of it. A tie goes to the even neighbour, as uGUI's Mathf.Round sends it.
        InValue = FMath::RoundHalfToEven(InValue);
    }
    // After the rounding: the range is the harder rule, so a whole number just past a fractional end
    // is pulled back to that end.
    return FMath::Clamp(InValue, MinValue, MaxValue);
}

void UUISlider::SetValue(float InValue, bool FireEvent)
{
    InValue = ConstrainValue(InValue);
    if (Value != InValue)
    {
        Value = InValue;
        ApplyValueToVisual();
        if (FireEvent)
        {
            OnValueChangedCPP.Broadcast(Value);
            OnValueChangedBP.Broadcast(Value);
            OnValueChanged.FireEvent((double)Value);
        }
    }
}

void UUISlider::SetValue(float InValue)
{
    SetValue(InValue, true);
}

void UUISlider::SetValueWithoutNotify(float InValue)
{
    SetValue(InValue, false);
}

void UUISlider::SetMinValue(float InMinValue, bool KeepRelativeValue, bool FireEvent)
{
    if (MinValue != InMinValue)
    {
		// Through the guarded reader: an empty span here produced NaN, and KeepRelativeValue then
		// multiplied it straight back into Value.
		float value01 = GetValue01();
		MinValue = InMinValue;
        // Through the one rule every value obeys, so a whole-number slider keeps holding a whole number
        // across a range change as well.
        if (KeepRelativeValue)
        {
            Value = ConstrainValue(value01 * (MaxValue - MinValue) + MinValue);
        }
        else
        {
            Value = ConstrainValue(Value);
        }
        ApplyValueToVisual();
		if (FireEvent)
		{
			OnValueChangedCPP.Broadcast(Value);
			OnValueChanged.FireEvent((double)Value);
		}
    }
}
void UUISlider::SetMaxValue(float InMaxValue, bool KeepRelativeValue, bool FireEvent)
{
	if (MaxValue != InMaxValue)
	{
		// The guarded reader, for the reason SetMinValue states.
		float value01 = GetValue01();
        MaxValue = InMaxValue;
		// The one rule every value obeys, for SetMinValue's reason.
		if (KeepRelativeValue)
		{
			Value = ConstrainValue(value01 * (MaxValue - MinValue) + MinValue);
		}
		else
		{
			Value = ConstrainValue(Value);
		}
		ApplyValueToVisual();
		if (FireEvent)
		{
			OnValueChangedCPP.Broadcast(Value);
			OnValueChanged.FireEvent((double)Value);
		}
	}
}

void UUISlider::SetNavigationChangeInterval(float InValue)
{
    NavigationChangeInterval = InValue;
}

void UUISlider::SetWholeNumbers(bool InValue)
{
	if (WholeNumbers == InValue)
	{
		return;
	}
	WholeNumbers = InValue;
	if (WholeNumbers)
	{
		// Without notify: stating the RULE is not the user moving the slider. SetValue is where the rule
		// is applied, to this and to every other value.
		SetValue(Value, false);
	}
}

void UUISlider::SetRequiresControllerLock(bool InValue)
{
	if (RequiresControllerLock == InValue)
	{
		return;
	}
	RequiresControllerLock = InValue;
	if (!RequiresControllerLock)
	{
		// A standing capture cannot outlive the rule that created it: left set, the slider would go on
		// reporting itself captured with nothing able to release it.
		SetControllerCaptured(false);
	}
}

void UUISlider::SetControllerCaptured(bool InValue)
{
	if (bControllerCaptured == InValue)
	{
		return;
	}
	bControllerCaptured = InValue;
	if (bControllerCaptured)
	{
		OnControllerCaptureBeginCPP.Broadcast();
	}
	else
	{
		OnControllerCaptureEndCPP.Broadcast();
	}
}

bool UUISlider::AcceptsPointerInput(const UDreamPointerEventData* InEventData) const
{
    // A slider switched off through this behaviour stays hit-testable (see bInteractable), and every
    // mouse button reaches it, so both questions are this component's to ask: disabled means nothing
    // moves, and SSlider answers the left button alone. A touch and a pad's accept are not mouse
    // buttons and always count.
    return IsInteractable() && AcceptsPointerButton(InEventData);
}

bool UUISlider::OnPointerDown_Implementation(UDreamPointerEventData *EventData)
{
    if (!AcceptsPointerButton(EventData))
    {
        // Not a press at all -- no pressed look, no selection, no capture, no value -- and passed on, as
        // SSlider::OnMouseButtonDown leaves every button but the left one unhandled and UUIButton passes
        // on a button it does not answer.
        return true;
    }
    Super::OnPointerDown_Implementation(EventData);
    if (!IsInteractable())
    {
        // The base has just refused the press of a disabled control, and the capture and the value are
        // refused with it: a capture announced by a slider that cannot move would tell a consumer to
        // wait for a value that is never coming.
        return AllowEventBubbleUp;
    }
    if (EventData->InputType == EDreamUIPointerInputType::Pointer)
    {
        // A pointer press ends a gamepad capture, exactly as SSlider::OnMouseButtonDown resets its
        // controller state: the player has switched to the mouse, and the stick's hold has to end with
        // the capture-end its consumers commit on.
        SetControllerCaptured(false);
        // A locked slider refuses the press outright: SSlider::OnMouseButtonDown asks IsLocked() before
        // anything else and returns Unhandled, so no capture begins -- and, having begun none, none
        // ends at the release. The value was never the problem (CalculateInputValue has its own lock
        // check); the capture events were, because a consumer holds off acting on the value between
        // them and a locked slider announced a drag that could not move anything.
        //
        // A finger's press only lands: SSlider::OnTouchStarted notes where and takes nothing, and the slider
        // takes the finger -- the capture, then the value under it -- once it has travelled the drag distance
        // (OnTouchMoved; here OnPointerBeginDrag). A finger that lifts without travelling changes nothing
        // (OnTouchEnded acts only on a finger it captured): a touch screen's slider must not jump under a
        // finger that was on its way to scroll the page.
        if (!bLocked && !UISliderLocal::IsFinger(EventData))
        {
            // The press IS the mouse capture -- UMG fires OnMouseCaptureBegin from SSlider's mouse-down
            // for the same reason, so a consumer can stop reacting to a value the player is still moving.
            bMouseCaptured = true;
            OnMouseCaptureBeginCPP.Broadcast();
            CalculateInputValue(EventData);
        }
    }
    else if (RequiresControllerLock)
    {
        // The navigation TRIGGER on a focused slider, which is the only non-pointer down that reaches
        // here. It takes the lock, and takes it back -- one key in, one key out, which is the whole of
        // UMG's controller capture and the reason a stick can travel through a row of sliders.
        SetControllerCaptured(!bControllerCaptured);
    }
    return AllowEventBubbleUp;
}
bool UUISlider::OnPointerUp_Implementation(UDreamPointerEventData *EventData)
{
    Super::OnPointerUp_Implementation(EventData);
    // Ends the capture the press began, and only that: keyed on the capture rather than on the lock,
    // so a slider locked in the middle of a drag still ends the capture it already announced --
    // SSlider's end comes from losing the capture, which the release does whatever the lock says.
    if (EventData->InputType == EDreamUIPointerInputType::Pointer && bMouseCaptured)
    {
        bMouseCaptured = false;
        OnMouseCaptureEndCPP.Broadcast();
    }
    return AllowEventBubbleUp;
}
bool UUISlider::OnPointerBeginDrag_Implementation(UDreamPointerEventData *EventData)
{
    // A drag is a press that moved, and is held to what the press was: a button this slider does not
    // answer, or a slider that is switched off, moves nothing however far the pointer travels.
    if (AcceptsPointerInput(EventData))
    {
        // A finger that has travelled the drag distance is the slider's now (SSlider::OnTouchMoved): its capture begins
        // here, where the mouse's began at the press, and is ended at the lift by OnPointerUp, as the mouse's is.
        if (UISliderLocal::IsFinger(EventData) && !bMouseCaptured && !bLocked)
        {
            bMouseCaptured = true;
            OnMouseCaptureBeginCPP.Broadcast();
        }
        CalculateInputValue(EventData);
    }
    return AllowEventBubbleUp;
}
bool UUISlider::OnPointerDrag_Implementation(UDreamPointerEventData *EventData)
{
    if (AcceptsPointerInput(EventData))
    {
        CalculateInputValue(EventData);
    }
    return AllowEventBubbleUp;
}
bool UUISlider::OnPointerEndDrag_Implementation(UDreamPointerEventData *EventData)
{
    if (AcceptsPointerInput(EventData))
    {
        CalculateInputValue(EventData);
    }
    return AllowEventBubbleUp;
}
bool UUISlider::OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)
{
    Super::OnPointerDeselect_Implementation(EventData);
    // Focus has gone somewhere else, and the gamepad's hold on this slider goes with it -- SSlider's
    // OnFocusLost resets its controller state for the same reason. A direction across a captured
    // slider moves focus on, and the capture used to stay behind: OnControllerCaptureEnd never came, and
    // the next visit found the slider already taken, so the accept meant to take it let it go instead.
    SetControllerCaptured(false);
    return AllowEventBubbleUp;
}
bool UUISlider::OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)
{
    float valueIntervalMultiply = 0.0f;
    if (
        (DirectionType == EUISliderDirectionType::LeftToRight && direction == EDreamUINavigationDirection::Left)
        || (DirectionType == EUISliderDirectionType::RightToLeft && direction == EDreamUINavigationDirection::Right)
        || (DirectionType == EUISliderDirectionType::BottomToTop && direction == EDreamUINavigationDirection::Down)
        || (DirectionType == EUISliderDirectionType::TopToBottom && direction == EDreamUINavigationDirection::Up))
    {
        valueIntervalMultiply = -NavigationChangeInterval;
    }
    else if (
        (DirectionType == EUISliderDirectionType::LeftToRight && direction == EDreamUINavigationDirection::Right)
        || (DirectionType == EUISliderDirectionType::RightToLeft && direction == EDreamUINavigationDirection::Left)
        || (DirectionType == EUISliderDirectionType::BottomToTop && direction == EDreamUINavigationDirection::Up)
        || (DirectionType == EUISliderDirectionType::TopToBottom && direction == EDreamUINavigationDirection::Down))
    {
        valueIntervalMultiply = NavigationChangeInterval;
    }
    if (!IsInteractable())
    {
        // Switched off while it held the stick -- through the behaviour's own switch, which tells
        // OnInteractableChanged nothing -- so the hold ends here, with its capture-end, and the directions go on
        // to the navigation search as an uncaptured slider's do.
        SetControllerCaptured(false);
        return Super::OnNavigate_Implementation(direction, result);
    }
    if (bLocked)
    {
        // A locked slider shows its value and refuses to move: the directions are not its to spend,
        // so they fall through to the navigation search exactly as an uncaptured one's do. Falling
        // through rather than swallowing them is what keeps a locked slider from being a dead end a
        // gamepad cannot leave.
        return Super::OnNavigate_Implementation(direction, result);
    }
    if (RequiresControllerLock && !bControllerCaptured)
    {
        // Not captured, so the directions are not this slider's to spend: they fall through to the
        // navigation search and move focus on. Without this rule a row of sliders is a trap -- the
        // first one eats every left and right and the stick can never leave it.
        return Super::OnNavigate_Implementation(direction, result);
    }
    if (valueIntervalMultiply == 0.0f)
    {
        return Super::OnNavigate_Implementation(direction, result);
    }
    else
    {
        float valueDelta = (MaxValue - MinValue) * valueIntervalMultiply;
        if (WholeNumbers && valueDelta != 0.0f && FMath::Abs(valueDelta) < 1.0f)
        {
            // A whole-number slider moves by whole numbers or not at all, and a press worth less than
            // one would be rounded straight back to where it started -- the stick would push and the
            // slider would never move. One whole number is the least a press can mean.
            valueDelta = valueDelta > 0.0f ? 1.0f : -1.0f;
        }
        // SetValue applies the range and the whole-number rule.
        SetValue(Value + valueDelta);
        return false;
    }
}

void UUISlider::CalculateInputValue(UDreamPointerEventData *EventData)
{
    if (bLocked)
    {
        // The one funnel every pointer road into a value takes -- down, begin drag, drag, end drag --
        // so the lock is stated once here rather than four times at the doors. Deliberately NOT in
        // SetValue: game code setting a locked slider's value is the ordinary way a read-only slider
        // shows anything at all.
        return;
    }
    UDreamWidget *MainWidget = nullptr;
    UDreamWidget *AreaWidget = nullptr;
    if (CheckHandle())
    {
        MainWidget = Handle.Get();
        AreaWidget = HandleArea.Get();
    }
    else
    {
        if (CheckFill())
        {
            MainWidget = Fill.Get();
            AreaWidget = FillArea.Get();
        }
    }
    if (MainWidget != nullptr && AreaWidget != nullptr)
    {
        // An area with no length has no positions in it to mean anything, and dividing by it below
        // would hand SetValue a NaN -- the same denominator defect the range arithmetic carried, from
        // the other side. Happens for real before the first layout pass.
        const float InputAreaLength = IsHorizontal() ? AreaWidget->GetWidth() : AreaWidget->GetHeight();
        if (FMath::Abs(InputAreaLength) <= KINDA_SMALL_NUMBER)
        {
            return;
        }
        //calculate value to 0-1 range
        auto localPointerPosition = AreaWidget->GetWorldTransform().InverseTransformPosition(EventData->GetWorldPointInPlane());
        float MinPosition = 0;
        float value01 = 0;
        switch (DirectionType)
        {
        case EUISliderDirectionType::LeftToRight:
        {
            MinPosition = -AreaWidget->GetPivot().X * AreaWidget->GetWidth();
            value01 = (localPointerPosition.Y - MinPosition) / AreaWidget->GetWidth();
        }
        break;
        case EUISliderDirectionType::RightToLeft:
        {
            MinPosition = -AreaWidget->GetPivot().X * AreaWidget->GetWidth();
            value01 = 1.0f - (localPointerPosition.Y - MinPosition) / AreaWidget->GetWidth();
        }
        break;
        case EUISliderDirectionType::BottomToTop:
        {
            MinPosition = -AreaWidget->GetPivot().Y * AreaWidget->GetHeight();
            value01 = (localPointerPosition.Z - MinPosition) / AreaWidget->GetHeight();
        }
        break;
        case EUISliderDirectionType::TopToBottom:
        {
            MinPosition = -AreaWidget->GetPivot().Y * AreaWidget->GetHeight();
            value01 = 1.0f - (localPointerPosition.Z - MinPosition) / AreaWidget->GetHeight();
        }
        break;
        }
        value01 = FMath::Clamp(value01, 0.0f, 1.0f);
        float value = (MaxValue - MinValue) * value01 + MinValue;
        if (MouseUsesStep && StepSize > KINDA_SMALL_NUMBER)
        {
            // Quantised to the step, measured FROM MinValue rather than from zero: a range of
            // 3..10 stepped by 2 must offer 3, 5, 7, 9 -- the ends the author stated -- and not the
            // 4, 6, 8, 10 that rounding an absolute value would give. Re-clamped after, because the
            // nearest multiple can sit outside the range.
            value = MinValue + FMath::RoundToFloat((value - MinValue) / StepSize) * StepSize;
            value = FMath::Clamp(value, MinValue, MaxValue);
        }
        // The whole-number rule is SetValue's, applied there to this value as to every other.
        SetValue(value, true);
    }
}
/**
 * The handle's and the fill's rects: ABSOLUTE numbers read off the live areas, against POINT anchors
 * on the area's start edge.
 *
 * This was the last ratio-anchor consumer in the library, and the reason every one of its siblings
 * moved off that road applies here unchanged: an anchor SETTER resolves the parent's span at write
 * time, and both areas are STRETCHED along the slider's long axis -- their SizeDelta is zero on every
 * frame but a full-layout one. A ratio anchor therefore drew the fill correctly on layout frames and
 * at zero length on all the others. That is the progress fill's "walking dot", the dropdown list's
 * zero width and the tab indicator's vanishing underline, and each of them was fixed exactly this
 * way (UUIScrollbar::ApplyValueToVisual is the closest twin -- same geometry, same shape of answer).
 *
 * The flip side of reading live numbers is that they are read WHEN THIS RUNS, which is what the two
 * dimension subscriptions above are for: the component's own widget, and its children, which is where
 * the areas are.
 *
 * The handle keeps ITS OWN size -- read before the anchors move and written straight back -- because
 * the size is the control's to state (UDreamSlider::ApplyStyle writes the style's HandleSize onto it)
 * and this component only ever decides WHERE it sits.
 */
void UUISlider::ApplyValueToVisual()
{
    const float value01 = GetValue01();
    const bool bHorizontal = IsHorizontal();
    // The fraction measured from the area's START edge, which is where both point anchors sit. The
    // two reversed directions put the zero end at the far edge, and this is the whole of that.
    const float Fraction = IsReversed() ? 1.0f - value01 : value01;

    if (CheckHandle())
    {
        UDreamWidget* HandleWidget = Handle.Get();
        UDreamWidget* Area = HandleArea.Get();
        // Before the anchors move: changing an anchor recomputes the size from the offsets, so the
        // control's authored handle size has to be captured while it is still the answer.
        const FVector2D HandleSize(HandleWidget->GetWidth(), HandleWidget->GetHeight());
        const float AreaLength = bHorizontal ? Area->GetWidth() : Area->GetHeight();
        const float Offset = Fraction * AreaLength;

        HandleWidget->SetPivot(FVector2D(0.5, 0.5));
        if (bHorizontal)
        {
            HandleWidget->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0, 0.5), FVector2D(0.0, 0.5), false, false);
            HandleWidget->SetAnchoredPositionAndSizeDelta(FVector2D(Offset, 0.0), HandleSize);
        }
        else
        {
            HandleWidget->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.0), FVector2D(0.5, 0.0), false, false);
            HandleWidget->SetAnchoredPositionAndSizeDelta(FVector2D(0.0, Offset), HandleSize);
        }
    }

    if (CheckFill())
    {
        UDreamWidget* FillWidget = Fill.Get();
        UDreamWidget* Area = FillArea.Get();
        const float AreaLength = bHorizontal ? Area->GetWidth() : Area->GetHeight();
        // The cross axis is the area's, absolutely: the fill used to be STRETCHED across it, and an
        // absolute number off the live area is the same rect with no span left for a setter to
        // resolve. The subscription above is what keeps it the same rect after a resize.
        const float AreaThickness = bHorizontal ? Area->GetHeight() : Area->GetWidth();
        const float Length = value01 * AreaLength;
        // The pivot sits on the edge the fill GROWS FROM, which is what makes the length read the way
        // the ratio version drew it: forward directions grow from the start edge, reversed ones from
        // the far edge, so a bar authored against the old shape looks unchanged.
        const FVector2D Anchor = bHorizontal
            ? FVector2D(IsReversed() ? 1.0 : 0.0, 0.5)
            : FVector2D(0.5, IsReversed() ? 1.0 : 0.0);

        FillWidget->SetPivot(Anchor);
        FillWidget->SetHorizontalAndVerticalAnchorMinMax(Anchor, Anchor, false, false);
        FillWidget->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, bHorizontal
            ? FVector2D(Length, AreaThickness)
            : FVector2D(AreaThickness, Length));
    }
}