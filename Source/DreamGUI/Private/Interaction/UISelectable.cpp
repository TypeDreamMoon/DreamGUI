// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Interaction/UISelectable.h"
#include "DreamGUI.h"
#include "DreamTweenBPLibrary.h"
#include "Core/Components/DreamVisual.h"
#include "Core/DreamUIManager.h"
#include "DreamTweenManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamEventSystem.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Interaction/UINavigationInputSelectionHandler.h"
#include "Interaction/DreamSelectableStyle.h"
#include "Interaction/DreamUINavigationScroll.h"
#include "Interaction/DreamUINavigationScope.h"
#include "Interaction/DreamUINavigationStack.h"
#include "GameFramework/ForceFeedbackEffect.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	/** 2D UI sound, gated to worlds where a player is actually listening. */
	void PlayDreamUISound(const UDreamWidget* InWidget, USoundBase* InSound)
	{
		UWorld* World = IsValid(InWidget) ? InWidget->GetWorld() : nullptr;
		if (IsValid(InSound) && IsValid(World) && World->IsGameWorld())
		{
			UGameplayStatics::PlaySound2D(World, InSound);
		}
	}
}


UUITransition::UUITransition()
{
	bStartWithTickEnabled = false;
}

void UUITransition::StopTransition() 
{ 
	for (auto tweener : TweenerCollection)
	{
		UDreamTweenBPLibrary::KillIfIsTweening(this, tweener);
	}
	TweenerCollection.Reset();
}
void UUITransition::CollectTweener(UDreamTweener* InItem)
{
	TweenerCollection.Add(InItem);
}
void UUITransition::CollectTweeners(const TSet<UDreamTweener*>& InItems)
{
	TweenerCollection.Reserve(TweenerCollection.Num() + InItems.Num());
	for (auto item : InItems)
	{
		TweenerCollection.Add(item);
	}
}

UUISelectable* UUISelectableTransition::GetSelectableComponent() const
{
	if (!IsValid(UISelectableComp))
	{
		UISelectableComp = GetWidget()->GetComponent<UUISelectable>();
	}
	return UISelectableComp;
}

void UUISelectableTransition::OnNormal(bool InImmediateSet)
{ 
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnNormal(InImmediateSet);
	}
}
void UUISelectableTransition::OnHovered(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnHovered(InImmediateSet);
	}
}
void UUISelectableTransition::OnPressed(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnPressed(InImmediateSet);
	}
}
void UUISelectableTransition::OnDisabled(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnDisabled(InImmediateSet);
	}
}
void UUISelectableTransition::OnFocused(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnFocused(InImmediateSet);
	}
}

UUISelectable::UUISelectable()
{
	NormalColor = FColor(255, 255, 255, 255);
	HoveredColor = FColor(200, 200, 200, 255);
	PressedColor = FColor(150, 150, 150, 255);
	DisabledColor = FColor(150, 150, 150, 128);
	FocusedColor = FColor(220, 220, 255, 255);
}

void UUISelectable::Awake()
{
	Super::Awake();
}

void UUISelectable::OnRegister()
{
	Super::OnRegister();
	if (GetWidget())
	{
		GetWidget()->SetIsFocusable(true);

		// The transition needs something to tint, and ApplyPointerSelectionState returns without a
		// word when it has none -- so an unwired selectable is not "a button with no feedback", it
		// is a button whose feedback silently never happens. The preset control Blueprints set this
		// by hand in the asset; nothing else could, because a text-authored .dui can only give an
		// object property an ASSET PATH and this target is a sibling in the same live tree.
		//
		// The widget's own visual is the answer every one of those Blueprints picked anyway, so
		// default to it and leave an explicit choice untouched.
		if (!TransitionTarget.IsValid() && TransitionType != EUISelectableTransitionType::Custom)
		{
			if (UDreamVisual* OwnVisual = GetWidget()->GetVisual())
			{
				TransitionTarget = OwnVisual;
				// The state was computed before a target existed; apply it now so the control opens
				// in its normal colours rather than whatever the brush was authored with.
				CurrentSelectionState = GetSelectionState();
				ApplyPointerSelectionState(true);
			}
		}
	}
	UDreamUIManagerWorldSubsystem::AddSelectable(this);
}
void UUISelectable::OnUnregister()
{
	Super::OnUnregister();
	UDreamUIManagerWorldSubsystem::RemoveSelectable(this);
}

#if WITH_EDITOR
void UUISelectable::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property)
	{
		ApplyPointerSelectionState(true);
	}
}
#endif

void UUISelectable::OnInteractableChanged(bool IsEnabled)
{
	Super::OnInteractableChanged(IsEnabled);
	CurrentSelectionState = GetSelectionState();
#if WITH_EDITOR
	// Null world included, and taking the immediate branch is the only thing that CAN work there:
	// the tween manager is a world subsystem, so the animated path has nothing to animate with.
	// A selectable reaches this with no world whenever something disables a widget outside a game --
	// a headless test, or a Blueprint's authoring tree, whose outer is the Blueprint. (Its caller,
	// UDreamUIBehaviour::Call_OnInteractableChanged, carries the same guard for the same reason.)
	const UWorld* SelectableWorld = this->GetWorld();
	if (SelectableWorld == nullptr || !SelectableWorld->IsGameWorld())//is editor, just set properties immediately
	{
		ApplyPointerSelectionState(true);
	}
	else
#endif
	{
		ApplyPointerSelectionState(false);
	}
}

void UUISelectable::ApplyPointerSelectionState(bool ImmediateSet)
{
	// Feedback on the TRANSITION, and only for interactive applies: ImmediateSet is the initial
	// state and the editor's property refresh, neither of which is something the user did.
	if (!ImmediateSet && CurrentSelectionState != LastFeedbackState)
	{
		PlaySelectionStateFeedback();
	}
	LastFeedbackState = CurrentSelectionState;

	// Before the early return below, deliberately: a listener that swaps a PICTURE per state has
	// nothing to do with whether this selectable has a transition target, and a control whose
	// transition type is None would otherwise hear about the states it is in exactly never.
	OnSelectionStateChangedCPP.Broadcast(CurrentSelectionState, ImmediateSet);

	const float EffectiveAnimDuration = Style ? Style->AnimationDuration : AnimDuration;
	if (TransitionType != EUISelectableTransitionType::Custom)
	{
		if (!TransitionTarget.IsValid())return;
	}

	TOptional<FColor> Color;
	TOptional<FDreamUIImageBrush> Brush;
	switch (CurrentSelectionState)
	{
	case EUISelectableSelectionState::Normal:
		{
			switch (TransitionType)
			{
			case EUISelectableTransitionType::None:break;
			case EUISelectableTransitionType::Color:
				{
					Color = GetNormalColor();
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush = GetNormalImageBrush();
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
					if (CustomTransition.IsValid())
					{
						CustomTransition->OnNormal(ImmediateSet);
					}
				}
				break;
			}
		}
		break;
	case EUISelectableSelectionState::Hovered:
		{
			switch (TransitionType)
			{
			case EUISelectableTransitionType::None:break;
			case EUISelectableTransitionType::Color:
				{
					Color = GetHoveredColor();
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush = GetHoveredImageBrush();
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
					if (CustomTransition.IsValid())
					{
						CustomTransition->OnHovered(ImmediateSet);
					}
				}
				break;
			}
		}
		break;
	case EUISelectableSelectionState::Pressed:
		{
			switch (TransitionType)
			{
			case EUISelectableTransitionType::None:break;
			case EUISelectableTransitionType::Color:
				{
					Color = GetPressedColor();
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush = GetPressedImageBrush();
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
					if (CustomTransition.IsValid())
					{
						CustomTransition->OnPressed(ImmediateSet);
					}
				}
				break;
			}
		}
		break;
	case EUISelectableSelectionState::Disabled:
		{
			switch (TransitionType)
			{
			case EUISelectableTransitionType::None:break;
			case EUISelectableTransitionType::Color:
				{
					Color = GetDisabledColor();
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush = GetDisabledImageBrush();
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
					if (CustomTransition.IsValid())
					{
						CustomTransition->OnDisabled(ImmediateSet);
					}
				}
				break;
			}
		}
		break;
	case EUISelectableSelectionState::Focused:
		{
			switch (TransitionType)
			{
			case EUISelectableTransitionType::None:break;
			case EUISelectableTransitionType::Color:
				{
					Color = GetFocusedColor();
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush = GetFocusedImageBrush();
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
					if (CustomTransition.IsValid())
					{
						// A transition that was written before focus existed only implements OnHovered,
						// and that is where a control with no focus look of its own belongs anyway.
						if (GetUseFocusedVisuals())
						{
							CustomTransition->OnFocused(ImmediateSet);
						}
						else
						{
							CustomTransition->OnHovered(ImmediateSet);
						}
					}
				}
				break;
			}
		}
		break;
	}

	if (Color.IsSet())
	{
		if (EffectiveAnimDuration <= 0.0f || ImmediateSet)
		{
			TransitionTarget->SetColor(Color.GetValue());
		}
		else
		{
			if (UDreamTweenManager::IsTweening(this, TransitionTweener))TransitionTweener->Kill();
			TransitionTweener = UDreamTweenManager::To(TransitionTarget.Get()
				, FDreamTweenColorGetterFunction::CreateWeakLambda(TransitionTarget.Get(), [=, this]()
			{
				return TransitionTarget->GetColor();
			}), FDreamTweenColorSetterFunction::CreateUObject(TransitionTarget.Get(), &UDreamVisual::SetColor), Color.GetValue(), EffectiveAnimDuration);
			if (TransitionTweener)
			{
				UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), TransitionTweener);
			}
		}
	}
	if (Brush.IsSet())
	{
		if (auto TransitionTargetAsDreamImage = Cast<UDreamImage>(TransitionTarget.Get()))
		{
			if (IsValid(Brush.GetValue().GetResourceObject()))
			{
				TransitionTargetAsDreamImage->SetBrush(Brush.GetValue());
			}
			else
			{
				if (EffectiveAnimDuration <= 0.0f || ImmediateSet)
				{
					TransitionTargetAsDreamImage->SetBrushTintColor(Brush.GetValue().TintColor);
				}
				else
				{
					if (UDreamTweenManager::IsTweening(this, TransitionTweener))TransitionTweener->Kill();
					TransitionTweener = UDreamTweenManager::To(TransitionTargetAsDreamImage
						, FDreamTweenColorGetterFunction::CreateWeakLambda(TransitionTargetAsDreamImage, [=, this]()
					{
						return TransitionTargetAsDreamImage->GetBrush().TintColor;
					}), FDreamTweenColorSetterFunction::CreateUObject(TransitionTargetAsDreamImage, &UDreamImage::SetBrushTintColor), Brush.GetValue().TintColor, EffectiveAnimDuration);
					if (TransitionTweener)
					{
						UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), TransitionTweener);
					}
				}
			}
		}
	}
}

bool UUISelectable::CheckNavigationSelectionState()
{
	if (!NavigationSelection.IsValid())
	{
		if (auto Widget = GetWidget())
		{
			// The presenter that hosts this tree owns the selection cursor -- it is the thing that
			// knows which cursor class the project configured and where the cursor may live. The
			// resolve was commented out, which left NavigationSelection with no writer anywhere in
			// the plugin: the branch below it could never be taken, UUINavigationInputSelectionHandler
			// was unreachable from the only code that would have driven it, and the gamepad
			// selection cursor was a feature that shipped switched off.
			if (auto Presenter = Cast<UDreamWidgetPresenterComponentBase>(Widget->GetAttachedRootSceneComponent()))
			{
				NavigationSelection = Presenter->GetNavigationSelection();
			}
		}
	}
	return NavigationSelection.IsValid();
}

bool UUISelectable::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	bIsPointerInsideThis = true;
	// One question, asked once. The line below used to dereference EventData bare, one statement
	// after this one had already allowed for it being null -- and a null event data is ordinary here:
	// the enter/exit path is driven by code that can clear a pointer without one.
	const bool bIsNavigationEnter = IsValid(EventData) && EventData->InputType == EDreamUIPointerInputType::Navigation;
	bIsEnteredByNavigation = bIsNavigationEnter;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	if (bIsNavigationEnter)
	{
		if (CheckNavigationSelectionState())
		{
			NavigationSelection->SelectWidget(GetWidget());
		}
	}
	else
	{
		if (NavigationSelection.IsValid())
		{
			NavigationSelection->SelectNone();
		}
	}
	return AllowEventBubbleUp;
}
bool UUISelectable::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	bIsPointerInsideThis = false;
	bIsEnteredByNavigation = false;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectable::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	// Disabled means disabled. bInteractable used to reach nothing but the LOOK: a control drawn in
	// its Disabled colours still pressed, still took selection, still clicked, still played its
	// click sound and still rumbled the pad, because the raycast gate one level up asks the widget's
	// hierarchy flag and has never known about this one. Enter and Exit stay delivered on purpose --
	// hovering a disabled button to read the tooltip that says why it is disabled is the point.
	if (!IsInteractable())
	{
		return AllowEventBubbleUp;
	}
	bIsPointerDown = true;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	if (auto eventSystemInstance = UDreamEventSystem::GetDreamEventSystemInstance(this, IsValid(EventData) ? EventData->UserIndex : 0))
	{
		eventSystemInstance->SetSelectWidget(GetWidget(), EventData);
	}
	return AllowEventBubbleUp;
}
bool UUISelectable::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	// Not gated on IsInteractable: a control disabled BETWEEN the press and the release still has a
	// press to let go of, and refusing the up would leave it stuck looking pressed for good.
	bIsPointerDown = false;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectable::OnPointerSelect_Implementation(UDreamBaseEventData* EventData)
{
	// Deselect below is deliberately NOT gated, for the same reason the up is not: whatever put
	// selection here, a disabled control must always be able to give it up again.
	if (!IsInteractable())
	{
		return AllowEventBubbleUp;
	}
	bIsSelected = true;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectable::OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)
{
	bIsSelected = false;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	return AllowEventBubbleUp;
}

EUISelectableSelectionState UUISelectable::GetSelectionState()const
{
	if (!IsInteractable())
		return EUISelectableSelectionState::Disabled;
	if (bIsPointerDown)
		return EUISelectableSelectionState::Pressed;
	// Navigation and a real pointer both arrive as an enter, because the confirm button has to press
	// whatever navigation landed on. Which of them it was is the whole difference between hover and
	// focus, and it is only knowable here, at the moment the enter came in.
	if (bIsPointerInsideThis)
		return bIsEnteredByNavigation ? EUISelectableSelectionState::Focused : EUISelectableSelectionState::Hovered;
	// Still the selected control with the pointer somewhere else: focused, and drawn as such.
	if (bIsSelected)
		return EUISelectableSelectionState::Focused;
	return EUISelectableSelectionState::Normal;
}

FColor UUISelectable::GetNormalColor() const { return Style ? Style->NormalColor : NormalColor; }
FColor UUISelectable::GetHoveredColor() const { return Style ? Style->HoveredColor : HoveredColor; }
FColor UUISelectable::GetPressedColor() const { return Style ? Style->PressedColor : PressedColor; }
FColor UUISelectable::GetDisabledColor() const { return Style ? Style->DisabledColor : DisabledColor; }
const FDreamUIImageBrush& UUISelectable::GetNormalImageBrush() const { return Style ? Style->NormalImageBrush : NormalImageBrush; }
const FDreamUIImageBrush& UUISelectable::GetHoveredImageBrush() const { return Style ? Style->HoveredImageBrush : HoveredImageBrush; }
const FDreamUIImageBrush& UUISelectable::GetPressedImageBrush() const { return Style ? Style->PressedImageBrush : PressedImageBrush; }
const FDreamUIImageBrush& UUISelectable::GetDisabledImageBrush() const { return Style ? Style->DisabledImageBrush : DisabledImageBrush; }

bool UUISelectable::GetUseFocusedVisuals() const { return Style ? Style->bUseFocusedVisuals : bUseFocusedVisuals; }
// Falling back to the hovered look is what keeps every control authored before focus was a state of
// its own looking exactly as it did: focus used to BE hover, so that is the honest default.
FColor UUISelectable::GetFocusedColor() const
{
	if (!GetUseFocusedVisuals())return GetHoveredColor();
	return Style ? Style->FocusedColor : FocusedColor;
}
const FDreamUIImageBrush& UUISelectable::GetFocusedImageBrush() const
{
	if (!GetUseFocusedVisuals())return GetHoveredImageBrush();
	return Style ? Style->FocusedImageBrush : FocusedImageBrush;
}

void UUISelectable::SetStyle(UDreamSelectableStyle* Value)
{
	if (Style != Value)
	{
		Style = Value;
		ApplyPointerSelectionState(false);
	}
}

void UUISelectable::SetTransitionTarget(UDreamVisual* Value)
{
	if (TransitionTarget != Value)
	{
		TransitionTarget = Value;
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetNormalColor(FColor Value)
{
	NormalColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Normal)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetHoveredColor(FColor Value)
{
	HoveredColor = Value;
	// Focused borrows the hovered colour while it has none of its own, so it has to repaint too.
	if (CurrentSelectionState == EUISelectableSelectionState::Hovered
		|| (CurrentSelectionState == EUISelectableSelectionState::Focused && !GetUseFocusedVisuals()))
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetFocusedColor(FColor Value)
{
	FocusedColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Focused)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetFocusedImageBrush(const FDreamUIImageBrush& Value)
{
	FocusedImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Focused)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetUseFocusedVisuals(bool Value)
{
	bUseFocusedVisuals = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Focused)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetPressedColor(FColor Value)
{
	PressedColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Pressed)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetDisabledColor(FColor Value)
{
	DisabledColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Disabled)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetAnimDuration(float Value)
{
	// Nothing re-applied: a duration decides how the NEXT change plays, and re-running the current
	// state to honour a new speed would replay a transition the pointer already finished.
	AnimDuration = FMath::Max(Value, 0.0f);
}
void UUISelectable::SetNormalImageBrush(const FDreamUIImageBrush& Value)
{
	NormalImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Normal)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetHoveredImageBrush(const FDreamUIImageBrush& Value)
{
	HoveredImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Hovered)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetPressedImageBrush(const FDreamUIImageBrush& Value)
{
	PressedImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Pressed)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetDisabledImageBrush(const FDreamUIImageBrush& Value)
{
	DisabledImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Disabled)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectable::SetSelectionState(EUISelectableSelectionState NewState)
{
	if (CurrentSelectionState != NewState)
	{
		CurrentSelectionState = NewState;
		ApplyPointerSelectionState(false);
	}
}
bool UUISelectable::IsInteractable()const
{
	if (auto Widget = GetWidget())
	{
		return Widget->GetRenderVisibleInHierarchy() && Widget->GetInteractableInHierarchy() && bInteractable;
	}
	return bInteractable;
}
void UUISelectable::SetInteractable(bool Value)
{
	if (bInteractable == Value)
	{
		return;
	}
	bInteractable = Value;
	// A control that goes disabled under a finger is no longer pressed and no longer selected, and
	// saying so here is what keeps the repaint below from drawing it as Disabled-but-still-pressed.
	if (!bInteractable)
	{
		bIsPointerDown = false;
		bIsSelected = false;
	}
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
}

#pragma region Navigation
bool UUISelectable::CanNavigateHere_Implementation() const
{
	return IsInteractable() && GetCanNavigateHere();
}
bool UUISelectable::OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)
{
	if (direction == EDreamUINavigationDirection::None)
	{
		return false;
	}
	// The behaviour, not the selectable: a widget that carries only a UDreamWidgetNavigation is a
	// navigation participant too, and returning nothing for it would put the pool back to
	// selectables-only, which is the thing the per-widget rules exist to undo.
	UDreamUIBehaviour* Found = FindNavigableOn(direction);
	result = Found != this ? Found : nullptr;
	return true;
}
UDreamUIBehaviour* UUISelectable::FindNavigableOn(EDreamUINavigationDirection InDirection)
{
	// A rule authored on the WIDGET outranks this component's own per-direction mode. Without that
	// order, which of the two answers would depend on which component the pipeline's walk reached
	// first, and that is a property of the order things were added in -- invisible to the author.
	if (UDreamWidget* Widget = GetWidget())
	{
		if (UDreamWidgetNavigation* Navigation = Widget->GetNavigation())
		{
			if (Navigation->HasRuleFor(InDirection))
			{
				TScriptInterface<IDreamNavigationInterface> RuleResult = nullptr;
				IDreamNavigationInterface::Execute_OnNavigate(Navigation, InDirection, RuleResult);
				UDreamUIBehaviour* Target = Cast<UDreamUIBehaviour>(RuleResult.GetObject());
				return Target != nullptr ? Target : this;
			}
		}
	}

	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return nullptr;//a behaviour outliving its widget has nowhere to navigate from
	}

	switch (InDirection)
	{
	case EDreamUINavigationDirection::Left:
		return NavigationLeft == EUISelectableNavigationMode::Explicit
			? ResolveExplicitTarget(NavigationLeftSpecific.Get(), InDirection)
			: (NavigationLeft == EUISelectableNavigationMode::Auto ? FindNavigableIn(-Widget->GetRightVector(), nullptr, /*bResolveCanvasParent*/true) : nullptr);
	case EDreamUINavigationDirection::Right:
		return NavigationRight == EUISelectableNavigationMode::Explicit
			? ResolveExplicitTarget(NavigationRightSpecific.Get(), InDirection)
			: (NavigationRight == EUISelectableNavigationMode::Auto ? FindNavigableIn(Widget->GetRightVector(), nullptr, /*bResolveCanvasParent*/true) : nullptr);
	case EDreamUINavigationDirection::Up:
		return NavigationUp == EUISelectableNavigationMode::Explicit
			? ResolveExplicitTarget(NavigationUpSpecific.Get(), InDirection)
			: (NavigationUp == EUISelectableNavigationMode::Auto ? FindNavigableIn(Widget->GetUpVector(), nullptr, /*bResolveCanvasParent*/true) : nullptr);
	case EDreamUINavigationDirection::Down:
		return NavigationDown == EUISelectableNavigationMode::Explicit
			? ResolveExplicitTarget(NavigationDownSpecific.Get(), InDirection)
			: (NavigationDown == EUISelectableNavigationMode::Auto ? FindNavigableIn(-Widget->GetUpVector(), nullptr, /*bResolveCanvasParent*/true) : nullptr);
	case EDreamUINavigationDirection::Next:
	{
		if (NavigationNext == EUISelectableNavigationMode::Explicit)
		{
			return ResolveExplicitTarget(NavigationNextSpecific.Get(), InDirection);
		}
		if (NavigationNext != EUISelectableNavigationMode::Auto)
		{
			return nullptr;
		}
		UDreamUIBehaviour* RightHop = FindNavigableOn(EDreamUINavigationDirection::Right);
		return RightHop != this && RightHop != nullptr ? RightHop : FindNavigableOn(EDreamUINavigationDirection::Down);
	}
	case EDreamUINavigationDirection::Prev:
	{
		if (NavigationPrev == EUISelectableNavigationMode::Explicit)
		{
			return ResolveExplicitTarget(NavigationPrevSpecific.Get(), InDirection);
		}
		if (NavigationPrev != EUISelectableNavigationMode::Auto)
		{
			return nullptr;
		}
		UDreamUIBehaviour* LeftHop = FindNavigableOn(EDreamUINavigationDirection::Left);
		return LeftHop != this && LeftHop != nullptr ? LeftHop : FindNavigableOn(EDreamUINavigationDirection::Up);
	}
	default:
		return nullptr;
	}
}
UUISelectable* UUISelectable::FindSelectable(FVector InDirection)
{
	return Cast<UUISelectable>(FindNavigableIn(InDirection, nullptr, /*bResolveCanvasParent*/true));
}

UUISelectable* UUISelectable::FindSelectable(FVector InDirection, UDreamWidget* InParent)
{
	// Answers only about SELECTABLES, which is what every existing caller of this asks for. The scan
	// underneath now also considers widgets whose only navigation is a UDreamWidgetNavigation, and one
	// of those winning comes back as null here -- use FindNavigableIn when the answer has to include
	// them, which is what the navigation pipeline itself does.
	return Cast<UUISelectable>(FindNavigableIn(InDirection, InParent, /*bResolveCanvasParent*/false));
}

UDreamUIBehaviour* UUISelectable::FindNavigableIn(FVector InDirection, UDreamWidget* InParent, bool bResolveCanvasParent)
{
	InDirection.Normalize();
	if (bResolveCanvasParent)
	{
		UDreamWidget* Widget = GetWidget();
		if (!IsValid(Widget))
		{
			return FindNavigableIn(InDirection, nullptr, false);
		}
		if (Widget->GetRenderCanvas() == nullptr || Widget->GetRenderCanvas()->GetRootCanvas() == nullptr)
		{
			return nullptr;//not active render
		}
		if (Widget->IsScreenSpaceOverlayUI() || Widget->IsRenderTargetUI())
		{
			return FindNavigableIn(InDirection, Widget->GetRootCanvas()->GetWidget(), false);
		}
		return FindNavigableIn(InDirection, nullptr, false);
	}

	// A screen that confines navigation is a harder boundary than whatever the caller asked for: while
	// a dialog is on top, no directional move may land on the page behind it, wherever that page sits
	// in the hierarchy. Applied here rather than one level up so it also holds for the Escape boundary
	// rule, which would otherwise be a way out of a screen that said nothing may leave.
	if (auto Stack = UDreamUINavigationStack::Get(this))
	{
		if (auto Scope = Stack->FindConfiningScopeFor(GetWidget()))
		{
			InParent = Scope->GetWidget();
		}
	}
	const UDreamWidget* RestrictNavNode = nullptr;
	if (auto Widget = GetWidget())
	{
		RestrictNavNode = Widget->GetRestrictNavigationAreaWidget();
	}
	return FindNavigableWithin(InDirection, InParent, RestrictNavNode, 0);
}

UDreamUIBehaviour* UUISelectable::FindNavigableWithin(const FVector& InDirection, UDreamWidget* InParent, const UDreamWidget* InRestrictNode, int32 InEscapeDepth)
{
	UDreamUIBehaviour* Found = DreamUINavigationScan::ScanDirectional(this, InDirection, InParent, InRestrictNode);
	if (Found != this)
	{
		return Found;//the scan moved, so the edge was never reached
	}
	// Nothing that way. Whether that is the end of the story is the area's decision, and with no area
	// around us there is nobody to ask -- stopping is the only thing "the edge of everything" can mean.
	if (!IsValid(InRestrictNode))
	{
		return this;
	}
	switch (InRestrictNode->GetNavigationBoundaryRule())
	{
	case EDreamUINavigationBoundaryRule::Wrap:
		return DreamUINavigationScan::ScanWrap(this, InDirection, InParent, InRestrictNode);
	case EDreamUINavigationBoundaryRule::Escape:
		{
			// One area out, and only if there is one: past the outermost area the move has genuinely
			// left everything that could restrict it, and the plain scan already covered that ground.
			if (InEscapeDepth >= MaxNavigationEscapeDepth)
			{
				return this;
			}
			const UDreamWidget* AreaParent = InRestrictNode->GetParent();
			const UDreamWidget* Enclosing = IsValid(AreaParent) ? AreaParent->GetRestrictNavigationAreaWidget() : nullptr;
			if (Enclosing == nullptr)
			{
				return DreamUINavigationScan::ScanDirectional(this, InDirection, InParent, nullptr);
			}
			return FindNavigableWithin(InDirection, InParent, Enclosing, InEscapeDepth + 1);
		}
	case EDreamUINavigationBoundaryRule::Stop:
	default:
		return this;
	}
}

UUISelectable* UUISelectable::FindDefaultSelectable(UObject* WorldContextObject, int32 InUserIndex)
{
	// A scope knows where its screen wants focus. This scan only knows registration order, which is
	// invisible to whoever authored the screen and need not even match between editor and packaged.
	if (auto Stack = UDreamUINavigationStack::Get(WorldContextObject))
	{
		if (auto Scope = Stack->GetActiveScope(InUserIndex))
		{
			if (auto Target = Scope->ResolveFocusTarget())
			{
				return Target;
			}
		}
	}
	return FindDefaultSelectableIn(WorldContextObject, nullptr);
}

UUISelectable* UUISelectable::FindDefaultSelectableIn(UObject* WorldContextObject, const UDreamWidget* InParent)
{
	auto IsInsideParent = [InParent](const UUISelectable* Item)
	{
		if (InParent == nullptr)return true;
		const UDreamWidget* Widget = Item->GetWidget();
		return IsValid(Widget) && (Widget == InParent || Widget->IsChildOf(InParent));
	};

	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(WorldContextObject->GetWorld()))
	{
		const auto& SelectableArray = DreamUIManager->GetAllSelectableArray();
		if (SelectableArray.Num() > 0)
		{
			UUISelectable* Selectable = nullptr;
			for (int i = 0; i < SelectableArray.Num(); i++)
			{
				auto SelectableItem = SelectableArray[i];
				if (SelectableItem.IsValid() && SelectableItem->IsInteractable() && SelectableItem->GetCanNavigateHere()
					&& IsInsideParent(SelectableItem.Get()))
				{
					Selectable = SelectableItem.Get();//find a interactable one
					break;
				}
			}
			if (Selectable)
			{
				//default selectable is the most "prev" one, so we need to find it
				TSet<UUISelectable*> FoundSelectables;
				while (true)
				{
					FoundSelectables.Add(Selectable);
					// Position only, explicit links excluded -- asked for directly instead of by
					// writing Auto into three of this OTHER selectable's authored UPROPERTYs, running
					// the ordinary finder, and writing them back. That round trip left the data
					// permanently rewritten on any path that did not reach the restore, and dirtied
					// the asset in an editor world for a query that changes nothing.
					auto PrevSelectable = Selectable->FindAutoPrev();

					if (!IsValid(PrevSelectable)
						|| PrevSelectable == Selectable
						|| FoundSelectables.Contains(PrevSelectable)//incase cycle loop, eg: A is left and B is top, A's top return B, and B's left return A
						|| !IsInsideParent(PrevSelectable)//the walk must not stroll out of the area we were asked about
						)
					{
						break;
					}
					else
					{
						Selectable = PrevSelectable;
					}
				}
				return Selectable;
			}
		}
	}
	return nullptr;
}
UUISelectable* UUISelectable::FindAutoPrev()
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return nullptr;
	}
	// The same two hops FindSelectableOnPrev makes under Auto: left first, then up.
	UUISelectable* LeftComp = FindSelectable(-Widget->GetRightVector());
	if (LeftComp != this)
	{
		return LeftComp;
	}
	return FindSelectable(Widget->GetUpVector());
}
UUISelectable* UUISelectable::ResolveExplicitTarget(UUISelectable* InTarget, EDreamUINavigationDirection InDirection)
{
	TSet<UUISelectable*> Visited;
	UUISelectable* Candidate = InTarget;
	while (IsValid(Candidate))
	{
		// The same two questions the Auto scan asks of every candidate it considers, so that an
		// explicit link and a scanned one agree on what "can be navigated to" means.
		if (Candidate->IsInteractable() && Candidate->GetCanNavigateHere())
		{
			return Candidate;
		}
		bool bAlreadySeen = false;
		Visited.Add(Candidate, &bAlreadySeen);
		if (bAlreadySeen)
		{
			return nullptr;//a ring of disabled controls wired to each other
		}
		UUISelectable* Next = nullptr;
		switch (InDirection)
		{
		case EDreamUINavigationDirection::Left:
			Next = Candidate->NavigationLeft == EUISelectableNavigationMode::Explicit ? Candidate->NavigationLeftSpecific.Get() : nullptr;
			break;
		case EDreamUINavigationDirection::Right:
			Next = Candidate->NavigationRight == EUISelectableNavigationMode::Explicit ? Candidate->NavigationRightSpecific.Get() : nullptr;
			break;
		case EDreamUINavigationDirection::Up:
			Next = Candidate->NavigationUp == EUISelectableNavigationMode::Explicit ? Candidate->NavigationUpSpecific.Get() : nullptr;
			break;
		case EDreamUINavigationDirection::Down:
			Next = Candidate->NavigationDown == EUISelectableNavigationMode::Explicit ? Candidate->NavigationDownSpecific.Get() : nullptr;
			break;
		case EDreamUINavigationDirection::Next:
			Next = Candidate->NavigationNext == EUISelectableNavigationMode::Explicit ? Candidate->NavigationNextSpecific.Get() : nullptr;
			break;
		case EDreamUINavigationDirection::Prev:
			Next = Candidate->NavigationPrev == EUISelectableNavigationMode::Explicit ? Candidate->NavigationPrevSpecific.Get() : nullptr;
			break;
		default:
			break;
		}
		Candidate = Next;
	}
	// Nothing reachable that way. Null keeps focus where it is, which beats parking it on a control
	// the player has been told they cannot use.
	return nullptr;
}
/*
 * The six public finders are now one-line views onto FindNavigableOn, which is where the rules --
 * the widget's own navigation panel first, then this component's per-direction mode -- actually
 * live. They keep answering UUISelectable because that is what they have always answered and what
 * UDreamUIManagerWorldSubsystem's editor gizmo draws; a move that lands on a widget whose only
 * navigation is a UDreamWidgetNavigation comes back null here and is delivered properly by
 * OnNavigate, which reads the behaviour rather than the selectable.
 */
UUISelectable* UUISelectable::FindSelectableOnLeft()
{
	return Cast<UUISelectable>(FindNavigableOn(EDreamUINavigationDirection::Left));
}
UUISelectable* UUISelectable::FindSelectableOnRight()
{
	return Cast<UUISelectable>(FindNavigableOn(EDreamUINavigationDirection::Right));
}
UUISelectable* UUISelectable::FindSelectableOnUp()
{
	return Cast<UUISelectable>(FindNavigableOn(EDreamUINavigationDirection::Up));
}
UUISelectable* UUISelectable::FindSelectableOnDown()
{
	return Cast<UUISelectable>(FindNavigableOn(EDreamUINavigationDirection::Down));
}
UUISelectable* UUISelectable::FindSelectableOnNext()
{
	return Cast<UUISelectable>(FindNavigableOn(EDreamUINavigationDirection::Next));
}
UUISelectable* UUISelectable::FindSelectableOnPrev()
{
	return Cast<UUISelectable>(FindNavigableOn(EDreamUINavigationDirection::Prev));
}

void UUISelectable::SetCanNavigateHere(bool Value)
{
	bCanNavigateHere = Value;
}
void UUISelectable::SetNavigationLeft(EUISelectableNavigationMode Value)
{
	NavigationLeft = Value;
}
void UUISelectable::SetNavigationRight(EUISelectableNavigationMode Value)
{
	NavigationRight = Value;
}
void UUISelectable::SetNavigationUp(EUISelectableNavigationMode Value)
{
	NavigationUp = Value;
}
void UUISelectable::SetNavigationDown(EUISelectableNavigationMode Value)
{
	NavigationDown = Value;
}
void UUISelectable::SetNavigationPrev(EUISelectableNavigationMode Value)
{
	NavigationPrev = Value;
}
void UUISelectable::SetNavigationNext(EUISelectableNavigationMode Value)
{
	NavigationNext = Value;
}

void UUISelectable::SetNavigationLeftExplicit(UUISelectable* Value)
{
	if (IsValid(Value))
	{
		NavigationLeftSpecific = Value;
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectable::SetNavigationRightExplicit(UUISelectable* Value)
{
	if (IsValid(Value))
	{
		NavigationRightSpecific = Value;
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectable::SetNavigationUpExplicit(UUISelectable* Value)
{
	if (IsValid(Value))
	{
		NavigationUpSpecific = Value;
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectable::SetNavigationDownExplicit(UUISelectable* Value)
{
	if (IsValid(Value))
	{
		NavigationDownSpecific = Value;
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectable::SetNavigationPrevExplicit(UUISelectable* Value)
{
	if (IsValid(Value))
	{
		NavigationPrevSpecific = Value;
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectable::SetNavigationNextExplicit(UUISelectable* Value)
{
	if (IsValid(Value))
	{
		NavigationNextSpecific = Value;
	}
	else
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
#pragma endregion

// The style asset wins, the instance's own slot is the fallback -- the same rule the colours above
// follow, and the reason there is an instance slot at all: a control built from code pushes its
// project sheet's feedback here, because it has no asset to hang a UDreamSelectableStyle on.
USoundBase* UUISelectable::GetHoveredSound() const { return Style ? Style->HoveredSound : HoveredSound; }
USoundBase* UUISelectable::GetPressedSound() const { return Style ? Style->PressedSound : PressedSound; }
USoundBase* UUISelectable::GetClickedSound() const { return Style ? Style->ClickedSound : ClickedSound; }

void UUISelectable::SetHoveredSound(USoundBase* Value) { HoveredSound = Value; }
void UUISelectable::SetPressedSound(USoundBase* Value) { PressedSound = Value; }
void UUISelectable::SetClickedSound(USoundBase* Value) { ClickedSound = Value; }

void UUISelectable::PlaySelectionStateFeedback()
{
	switch (CurrentSelectionState)
	{
	case EUISelectableSelectionState::Hovered:
		PlayDreamUISound(GetWidget(), GetHoveredSound());
		break;
	case EUISelectableSelectionState::Pressed:
		PlayDreamUISound(GetWidget(), GetPressedSound());
		break;
	default:
		break;
	}
}

void UUISelectable::PlayClickFeedback()
{
	PlayDreamUISound(GetWidget(), GetClickedSound());
	if (Style == nullptr)
	{
		// Rumble stays style-only: it is a property of the PAD rather than of the control, and a
		// control style struct that named one would be describing hardware it cannot see.
		return;
	}
	UWorld* World = IsValid(GetWidget()) ? GetWidget()->GetWorld() : nullptr;
	if (IsValid(Style->ClickedForceFeedback) && IsValid(World) && World->IsGameWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->ClientPlayForceFeedback(Style->ClickedForceFeedback);
		}
	}
}



namespace DreamSelectableClickMethodLocal
{
	/**
	 * Which of the three method enums an event falls under.
	 *
	 * The event itself says whether it came from navigation; the DEVICE question is the event
	 * system's, because a pointer event carries a pointer id and nothing about what moved the
	 * pointer -- a touch and a mouse are the same shape by the time they reach here, which is the
	 * whole point of the abstraction and exactly why the device has to be asked separately.
	 */
	enum class EKind : uint8 { Mouse, Touch, Press };

	EKind ResolveKind(const UDreamUIBehaviour* InOwner, const UDreamPointerEventData* InEventData)
	{
		if (InEventData != nullptr && InEventData->InputType == EDreamUIPointerInputType::Navigation)
		{
			return EKind::Press;
		}
		UDreamUIBehaviour* Owner = const_cast<UDreamUIBehaviour*>(InOwner);
		const UDreamEventSystem* Events = (Owner != nullptr && Owner->GetWorld() != nullptr)
			? UDreamEventSystem::GetDreamEventSystemInstance(Owner, 0)
			: nullptr;
		if (Events != nullptr && Events->GetCurrentInputDevice() == EDreamUIInputDevice::Touch)
		{
			return EKind::Touch;
		}
		// No event system to ask (a headless test, a tree not yet registered) is the mouse: it is the
		// device every method enum's DEFAULT was written for, so an unknown device behaves as before.
		return EKind::Mouse;
	}
}

bool UUISelectable::ShouldClickOnDown(const UDreamPointerEventData* InEventData)const
{
	using namespace DreamSelectableClickMethodLocal;
	switch (ResolveKind(this, InEventData))
	{
	case EKind::Press: return PressMethod == EDreamUIPressMethod::ButtonPress;
	case EKind::Touch: return TouchMethod == EDreamUITouchMethod::Down;
	default:           return ClickMethod == EDreamUIClickMethod::MouseDown;
	}
}

bool UUISelectable::ShouldClickOnUp(const UDreamPointerEventData* InEventData)const
{
	using namespace DreamSelectableClickMethodLocal;
	switch (ResolveKind(this, InEventData))
	{
	case EKind::Press: return PressMethod == EDreamUIPressMethod::ButtonRelease;
	// MouseUp fires on the release WHEREVER the press landed, which is the whole of what separates it
	// from DownAndUp: the event system delivers an up to the press target, and a press that started
	// somewhere else reaches this control only as a plain up with no click behind it.
	case EKind::Touch: return false;
	default:           return ClickMethod == EDreamUIClickMethod::MouseUp;
	}
}

bool UUISelectable::ShouldClickOnClick(const UDreamPointerEventData* InEventData)const
{
	using namespace DreamSelectableClickMethodLocal;
	// A click is by construction a down and an up both on this widget, so the two DownAndUp cases are
	// simply "yes" -- and the PRECISE ones are that answer narrowed by whether the pointer ever
	// started dragging, which is the cancel gesture a list of buttons inside a scroll view needs.
	const bool bDragged = InEventData != nullptr && InEventData->bIsDragging;
	switch (ResolveKind(this, InEventData))
	{
	case EKind::Press:
		return PressMethod == EDreamUIPressMethod::DownAndUp;
	case EKind::Touch:
		return TouchMethod == EDreamUITouchMethod::DownAndUp
			|| (TouchMethod == EDreamUITouchMethod::PreciseTap && !bDragged);
	default:
		return ClickMethod == EDreamUIClickMethod::DownAndUp
			|| (ClickMethod == EDreamUIClickMethod::PreciseClick && !bDragged);
	}
}
