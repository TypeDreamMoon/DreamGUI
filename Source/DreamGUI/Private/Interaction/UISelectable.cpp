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
			if (auto WidgetRootActor = Widget->GetAttachedRootSceneComponent())
			{
				// NavigationSelection = WidgetRootActor->GetNavigationSelection();
			}
		}
	}
	return NavigationSelection.IsValid();
}

bool UUISelectable::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	bIsPointerInsideThis = true;
	bIsEnteredByNavigation = IsValid(EventData) && EventData->InputType == EDreamUIPointerInputType::Navigation;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	if (EventData->InputType == EDreamUIPointerInputType::Navigation)
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
	bIsPointerDown = false;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectable::OnPointerSelect_Implementation(UDreamBaseEventData* EventData)
{
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

#pragma region Navigation
bool UUISelectable::CanNavigateHere_Implementation() const
{
	return IsInteractable() && GetCanNavigateHere();
}
bool UUISelectable::OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)
{
	UUISelectable* Selectable = nullptr;
	switch (direction)
	{
	default:
	case EDreamUINavigationDirection::None:
		return false;
		break;
	case EDreamUINavigationDirection::Left:
		Selectable = FindSelectableOnLeft();
		break;
	case EDreamUINavigationDirection::Right:
		Selectable = FindSelectableOnRight();
		break;
	case EDreamUINavigationDirection::Up:
		Selectable = FindSelectableOnUp();
		break;
	case EDreamUINavigationDirection::Down:
		Selectable = FindSelectableOnDown();
		break;
	case EDreamUINavigationDirection::Prev:
		Selectable = FindSelectableOnPrev();
		break;
	case EDreamUINavigationDirection::Next:
		Selectable = FindSelectableOnNext();
		break;
	}
	result = Selectable;
	return true;
}
UUISelectable* UUISelectable::FindSelectable(FVector InDirection)
{
	InDirection.Normalize();
	if (auto Widget = GetWidget())
	{
		if (Widget->GetRenderCanvas() == nullptr || Widget->GetRenderCanvas()->GetRootCanvas() == nullptr)
		{
			return nullptr;//not active render
		}
		if (Widget->IsScreenSpaceOverlayUI() || Widget->IsRenderTargetUI())
		{
			auto rootCanvasUIItem = Widget->GetRootCanvas()->GetWidget();
			return FindSelectable(InDirection, rootCanvasUIItem);
		}
		else
		{
			return FindSelectable(InDirection, nullptr);
		}
	}
	else
	{
		return FindSelectable(InDirection, nullptr);
	}
}

UUISelectable* UUISelectable::FindSelectable(FVector InDirection, UDreamWidget* InParent)
{
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
	return FindSelectableWithin(InDirection, InParent, RestrictNavNode, 0);
}

UUISelectable* UUISelectable::FindSelectableWithin(const FVector& InDirection, UDreamWidget* InParent, const UDreamWidget* InRestrictNode, int32 InEscapeDepth)
{
	UUISelectable* Found = ScanForSelectable(InDirection, InParent, InRestrictNode);
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
		return FindWrapTarget(InDirection, InParent, InRestrictNode);
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
				return ScanForSelectable(InDirection, InParent, nullptr);
			}
			return FindSelectableWithin(InDirection, InParent, Enclosing, InEscapeDepth + 1);
		}
	case EDreamUINavigationBoundaryRule::Stop:
	default:
		return this;
	}
}

UUISelectable* UUISelectable::FindWrapTarget(const FVector& InDirection, UDreamWidget* InParent, const UDreamWidget* InRestrictNode)
{
	// Walk backwards with the very same scan until it stops. Wrapping then lands exactly where holding
	// the opposite direction would have left the player, which no standalone "pick the far one" scoring
	// can promise -- and it needs no weighting between how far back a candidate is and how well it
	// lines up, the two quantities such a scoring would have had to trade off against each other.
	UUISelectable* Walker = this;
	TSet<UUISelectable*> Visited;
	Visited.Add(this);
	const FVector Backwards = -InDirection;
	for (;;)
	{
		UUISelectable* Back = Walker->ScanForSelectable(Backwards, InParent, InRestrictNode);
		if (Back == nullptr || Back == Walker || Visited.Contains(Back))
		{
			break;//at the far edge, or round a cycle a strange layout built
		}
		Visited.Add(Back);
		Walker = Back;
	}
	return Walker;
}

UUISelectable* UUISelectable::ScanForSelectable(const FVector& InDirection, UDreamWidget* InParent, const UDreamWidget* RestrictNavNode)
{
	auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(this->GetWorld());
	if (DreamUIManager == nullptr)return nullptr;
	const auto& SelectableArray = DreamUIManager->GetAllSelectableArray();

	auto GetPointOnRectEdge = [](UDreamWidget* rect, FVector2D dir)
	{
		if (dir != FVector2D::ZeroVector)
			dir /= FMath::Max(FMath::Abs(dir.X), FMath::Abs(dir.Y));
		auto center = rect->GetLocalSpaceCenter();
		dir = center + FVector2D(rect->GetWidth() * dir.X * 0.5f, rect->GetHeight() * dir.Y * 0.5f);
		return FVector(0, dir.X, dir.Y);
	};

	auto LocalPos = FVector::ZeroVector;
	if (auto Widget = GetWidget())
	{
		auto localDir = Widget->GetWorldTransform().InverseTransformVectorNoScale(InDirection);
		LocalPos = GetPointOnRectEdge(Widget, FVector2D(localDir.Y, localDir.Z));
	}
	auto pos = this->GetWidget()->GetWorldTransform().TransformPosition(LocalPos);
	auto thisWidget = this->GetWidget();
	float maxScore = -MAX_flt;
	UUISelectable* bestPick = this;
	for (int i = 0; i < SelectableArray.Num(); ++i)
	{
		auto sel = SelectableArray[i];

		if (sel == this || !sel.IsValid())
			continue;

		if (IsValid(InParent) && !sel->GetWidget()->IsChildOf(InParent))
			continue;

		if (!sel->IsInteractable())
			continue;

		if (!sel->GetCanNavigateHere())
			continue;

		//if is UI node, not allow inactive one
		auto selWidget = sel->GetWidget();
		if (selWidget && !sel->GetWidget()->GetInteractableInHierarchy())
		{
			continue;
		}

		if (selWidget && thisWidget)
		{
			if (selWidget->IsWorldSpaceUI() != thisWidget->IsWorldSpaceUI())
			{
				continue;
			}
		}

		//if navigation is restricted, only allow child of restrict node
		if (RestrictNavNode && !sel->GetWidget()->IsChildOf(RestrictNavNode))
		{
			continue;
		}

#if WITH_EDITOR
		if (this->GetWorld() != sel->GetWorld())
			continue;
#endif

		FVector selCenter;
		if (selWidget)
		{
			auto LocalCenter2D = selWidget->GetLocalSpaceCenter();
			selCenter = FVector(0, LocalCenter2D.X, LocalCenter2D.Y);
		}
		else
		{
			selCenter = sel->GetWidget()->GetRelativeLocation();
		}
		auto selCenterInWorld = sel->GetWidget()->GetWorldTransform().TransformPosition(selCenter);
		if (selWidget)
		{
			// Clipped away is not the same as out of reach. A row scrolled off the end of a list is
			// one scroll from being on screen, and refusing it here is what used to pin a gamepad to
			// the rows that happened to be visible; anything else hidden -- behind a mask, under a
			// closed panel -- has no way back and stays skipped.
			if (!selWidget->IsPointVisibleOnClip(selCenterInWorld)
				&& !FDreamUINavigationScroll::IsReachableByScrolling(selWidget))
			{
				continue;
			}
		}
		FVector myVector = selCenterInWorld - pos;

		float dot = FVector::DotProduct(InDirection, myVector);
		if (dot <= 0.0f)
			continue;

		float score = dot / myVector.SizeSquared();
		if (score > maxScore)
		{
			maxScore = score;
			bestPick = sel.Get();
		}
	}
	return bestPick;
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
					//change navigation mode to auto, so we can find selectable only by position (exclude explicit)
					auto OriginNavigationLeftMode = Selectable->NavigationLeft;
					auto OriginNavigationUpMode = Selectable->NavigationUp;
					auto OriginNavigationPrevMode = Selectable->NavigationPrev;
					Selectable->NavigationLeft = EUISelectableNavigationMode::Auto;
					Selectable->NavigationUp = EUISelectableNavigationMode::Auto;
					Selectable->NavigationPrev = EUISelectableNavigationMode::Auto;

					auto PrevSelectable = Selectable->FindSelectableOnPrev();

					//restore navigation mode
					Selectable->NavigationLeft = OriginNavigationLeftMode;
					Selectable->NavigationUp = OriginNavigationUpMode;
					Selectable->NavigationPrev = OriginNavigationPrevMode;

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
UUISelectable* UUISelectable::FindSelectableOnLeft()
{
	if (NavigationLeft == EUISelectableNavigationMode::Explicit)
	{
		return NavigationLeftSpecific.Get();
	}
	if (NavigationLeft == EUISelectableNavigationMode::Auto)
	{
		return FindSelectable(-GetWidget()->GetRightVector());
	}
	return nullptr;
}
UUISelectable* UUISelectable::FindSelectableOnRight()
{
	if (NavigationRight == EUISelectableNavigationMode::Explicit)
	{
		return NavigationRightSpecific.Get();
	}
	if (NavigationRight == EUISelectableNavigationMode::Auto)
	{
		return FindSelectable(GetWidget()->GetRightVector());
	}
	return nullptr;
}
UUISelectable* UUISelectable::FindSelectableOnUp()
{
	if (NavigationUp == EUISelectableNavigationMode::Explicit)
	{
		return NavigationUpSpecific.Get();
	}
	if (NavigationUp == EUISelectableNavigationMode::Auto)
	{
		return FindSelectable(GetWidget()->GetUpVector());
	}
	return nullptr;
}
UUISelectable* UUISelectable::FindSelectableOnDown()
{
	if (NavigationDown == EUISelectableNavigationMode::Explicit)
	{
		return NavigationDownSpecific.Get();
	}
	if (NavigationDown == EUISelectableNavigationMode::Auto)
	{
		return FindSelectable(-GetWidget()->GetUpVector());
	}
	return nullptr;
}
UUISelectable* UUISelectable::FindSelectableOnNext()
{
	if (NavigationNext == EUISelectableNavigationMode::Explicit)
	{
		return NavigationNextSpecific.Get();
	}
	if (NavigationNext == EUISelectableNavigationMode::Auto)
	{
		auto rightComp = FindSelectableOnRight();
		if (rightComp != this)
		{
			return rightComp;
		}
		return FindSelectableOnDown();
	}
	return nullptr;
}
UUISelectable* UUISelectable::FindSelectableOnPrev()
{
	if (NavigationPrev == EUISelectableNavigationMode::Explicit)
	{
		return NavigationPrevSpecific.Get();
	}
	if (NavigationPrev == EUISelectableNavigationMode::Auto)
	{
		auto leftComp = FindSelectableOnLeft();
		if (leftComp != this)
		{
			return leftComp;
		}
		return FindSelectableOnUp();
	}
	return nullptr;
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

void UUISelectable::PlaySelectionStateFeedback()
{
	if (Style == nullptr)
	{
		// Deliberately style-only, no per-instance twins: a click should sound like the OTHER
		// clicks in this UI, and forty inline sound slots is how it stops doing that.
		return;
	}
	switch (CurrentSelectionState)
	{
	case EUISelectableSelectionState::Hovered:
		PlayDreamUISound(GetWidget(), Style->HoveredSound);
		break;
	case EUISelectableSelectionState::Pressed:
		PlayDreamUISound(GetWidget(), Style->PressedSound);
		break;
	default:
		break;
	}
}

void UUISelectable::PlayClickFeedback()
{
	if (Style == nullptr)
	{
		return;
	}
	PlayDreamUISound(GetWidget(), Style->ClickedSound);
	UWorld* World = IsValid(GetWidget()) ? GetWidget()->GetWorld() : nullptr;
	if (IsValid(Style->ClickedForceFeedback) && IsValid(World) && World->IsGameWorld())
	{
		if (APlayerController* PlayerController = World->GetFirstPlayerController())
		{
			PlayerController->ClientPlayForceFeedback(Style->ClickedForceFeedback);
		}
	}
}


