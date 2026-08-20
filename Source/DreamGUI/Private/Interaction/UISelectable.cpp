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

UUISelectable::UUISelectable()
{
	NormalColor = FColor(255, 255, 255, 255);
	HoveredColor = FColor(200, 200, 200, 255);
	PressedColor = FColor(150, 150, 150, 255);
	DisabledColor = FColor(150, 150, 150, 128);
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
	if (!this->GetWorld()->IsGameWorld())//is editor, just set properties immediately
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
	return AllowEventBubbleUp;
}
bool UUISelectable::OnPointerDeselect_Implementation(UDreamBaseEventData* EventData)
{
	return AllowEventBubbleUp;
}

EUISelectableSelectionState UUISelectable::GetSelectionState()const
{
	if (!IsInteractable())
		return EUISelectableSelectionState::Disabled;
	if (bIsPointerDown)
		return EUISelectableSelectionState::Pressed;
	if (bIsPointerInsideThis)
		return EUISelectableSelectionState::Hovered;
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
	if (CurrentSelectionState == EUISelectableSelectionState::Hovered)
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
	const UDreamWidget* RestrictNavNode = nullptr;
	if (auto Widget = GetWidget())
	{
		auto localDir = Widget->GetWorldTransform().InverseTransformVectorNoScale(InDirection);
		LocalPos = GetPointOnRectEdge(Widget, FVector2D(localDir.Y, localDir.Z));
		if (auto RestrictNavWidget = Widget->GetRestrictNavigationAreaWidget())
		{
			RestrictNavNode = RestrictNavWidget;
		}
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
			if (!selWidget->IsPointVisibleOnClip(selCenterInWorld))
			{
				continue;//if not visible then skip it
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
UUISelectable* UUISelectable::FindDefaultSelectable(UObject* WorldContextObject)
{
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(WorldContextObject->GetWorld()))
	{
		const auto& SelectableArray = DreamUIManager->GetAllSelectableArray();
		if (SelectableArray.Num() > 0)
		{
			UUISelectable* Selectable = nullptr;
			for (int i = 0; i < SelectableArray.Num(); i++)
			{
				auto SelectableItem = SelectableArray[i];
				if (SelectableItem->IsInteractable() && SelectableItem->GetCanNavigateHere())
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


