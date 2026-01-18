// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UISelectableComponent.h"
#include "LGUI.h"
#include "LTweenBPLibrary.h"
#include "Core/Components/LexVisual.h"
#include "Core/LexUIManager.h"
#include "LTweenManager.h"
#include "Core/Components/LexCanvas.h"
#include "Event/LexEventSystem.h"
#include "Core/LexUISettings.h"
#include "Core/Actor/LexWidgetRootActor.h"
#include "Core/Components/LexImage.h"
#include "Interaction/UINavigationInputSelectionHandler.h"


void UUITransitionComponent::StopTransition() 
{ 
	for (auto tweener : TweenerCollection)
	{
		ULTweenBPLibrary::KillIfIsTweening(this, tweener);
	}
	TweenerCollection.Reset();
}
void UUITransitionComponent::CollectTweener(ULTweener* InItem)
{
	TweenerCollection.Add(InItem);
}
void UUITransitionComponent::CollectTweeners(const TSet<ULTweener*>& InItems)
{
	TweenerCollection.Reserve(TweenerCollection.Num() + InItems.Num());
	for (auto item : InItems)
	{
		TweenerCollection.Add(item);
	}
}

UUISelectableComponent* UUISelectableTransition::GetSelectableComponent() const
{
	if (!IsValid(UISelectableComp))
	{
		UISelectableComp = GetOwner()->FindComponentByClass<UUISelectableComponent>();
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

UUISelectableComponent::UUISelectableComponent()
{
	NormalColor = FColor(255, 255, 255, 255);
	HoveredColor = FColor(200, 200, 200, 255);
	PressedColor = FColor(150, 150, 150, 255);
	DisabledColor = FColor(150, 150, 150, 128);
}

void UUISelectableComponent::Awake()
{
	Super::Awake();
	this->SetCanExecuteUpdate(false);
}

void UUISelectableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void UUISelectableComponent::OnRegister()
{
	Super::OnRegister();
	ULexUIManagerWorldSubsystem::AddSelectable(this);
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(true);
}
void UUISelectableComponent::OnUnregister()
{
	Super::OnUnregister();
	ULexUIManagerWorldSubsystem::RemoveSelectable(this);
}

#if WITH_EDITOR
void UUISelectableComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (PropertyChangedEvent.Property)
	{
		ApplyPointerSelectionState(true);
	}
}
#endif

void UUISelectableComponent::OnInteractableChanged(bool IsEnabled)
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

void UUISelectableComponent::ApplyPointerSelectionState(bool ImmediateSet)
{
	if (TransitionType != EUISelectableTransitionType::Custom)
	{
		if (!TransitionTarget.IsValid())return;
	}

	TOptional<FColor> Color;
	TOptional<FLexUIImageBrush> Brush;
	switch (CurrentSelectionState)
	{
	case EUISelectableSelectionState::Normal:
		{
			switch (TransitionType)
			{
			case EUISelectableTransitionType::None:break;
			case EUISelectableTransitionType::Color:
				{
					Color = NormalColor;
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush = NormalImageBrush;
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
#if WITH_EDITOR
					if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
					{
						if (CustomTransition.IsValid())
						{
							CustomTransition->OnNormal(ImmediateSet);
						}
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
					Color = HoveredColor;
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush = HoveredImageBrush;
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
#if WITH_EDITOR
					if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
					{
						if (CustomTransition.IsValid())
						{
							CustomTransition->OnHovered(ImmediateSet);
						}
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
					Color = PressedColor;
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush = PressedImageBrush;
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
#if WITH_EDITOR
					if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
					{
						if (CustomTransition.IsValid())
						{
							CustomTransition->OnPressed(ImmediateSet);
						}
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
					Color = DisabledColor;
				}
				break;
			case EUISelectableTransitionType::ImageBrush:
				{
					Brush =  DisabledImageBrush;
				}
				break;
			case EUISelectableTransitionType::Custom:
				{
#if WITH_EDITOR
					if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
					{
						if (CustomTransition.IsValid())
						{
							CustomTransition->OnDisabled(ImmediateSet);
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
		if (AnimDuration <= 0.0f || ImmediateSet)
		{
			TransitionTarget->SetColor(Color.GetValue());
		}
		else
		{
			if (ULTweenManager::IsTweening(this, TransitionTweener))TransitionTweener->Kill();
			TransitionTweener = ULTweenManager::To(TransitionTarget.Get()
				, FLTweenColorGetterFunction::CreateWeakLambda(TransitionTarget.Get(), [=, this]()
			{
				return TransitionTarget->GetColor();
			}), FLTweenColorSetterFunction::CreateUObject(TransitionTarget.Get(), &ULexVisual::SetColor), Color.GetValue(), AnimDuration);
			if (TransitionTweener)
			{
				ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), TransitionTweener);
			}
		}
	}
	if (Brush.IsSet())
	{
		if (auto TransitionTargetAsLexImage = Cast<ULexImage>(TransitionTarget.Get()))
		{
			if (IsValid(Brush.GetValue().GetResourceObject()))
			{
				TransitionTargetAsLexImage->SetBrush(Brush.GetValue());
			}
			else
			{
				if (AnimDuration <= 0.0f || ImmediateSet)
				{
					TransitionTargetAsLexImage->SetBrushTintColor(Brush.GetValue().TintColor);
				}
				else
				{
					if (ULTweenManager::IsTweening(this, TransitionTweener))TransitionTweener->Kill();
					TransitionTweener = ULTweenManager::To(TransitionTargetAsLexImage
						, FLTweenColorGetterFunction::CreateWeakLambda(TransitionTargetAsLexImage, [=, this]()
					{
						return TransitionTargetAsLexImage->GetBrush().TintColor;
					}), FLTweenColorSetterFunction::CreateUObject(TransitionTargetAsLexImage, &ULexImage::SetBrushTintColor), Brush.GetValue().TintColor, AnimDuration);
					if (TransitionTweener)
					{
						ULexWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(GetWidget(), TransitionTweener);
					}
				}
			}
		}
	}
}

bool UUISelectableComponent::CheckNavigationSelectionState()
{
	if (!NavigationSelection.IsValid())
	{
		if (auto Widget = GetWidget())
		{
			if (auto WidgetRootActor = Widget->GetWidgetRootActor())
			{
				NavigationSelection = WidgetRootActor->GetNavigationSelection();
			}
		}
	}
	return NavigationSelection.IsValid();
}

bool UUISelectableComponent::OnPointerEnter_Implementation(ULexPointerEventData* EventData)
{
	bIsPointerInsideThis = true;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	if (EventData->InputType == ELexUIPointerInputType::Navigation)
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
bool UUISelectableComponent::OnPointerExit_Implementation(ULexPointerEventData* EventData)
{
	bIsPointerInsideThis = false;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerDown_Implementation(ULexPointerEventData* EventData)
{
	bIsPointerDown = true;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	if (auto eventSystemInstance = ULexEventSystem::GetLexEventSystemInstance(this, IsValid(EventData) ? EventData->UserIndex : 0))
	{
		eventSystemInstance->SetSelectComponent(GetWidget(), EventData, EventData->EnterComponentEventFireType);
	}
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerUp_Implementation(ULexPointerEventData* EventData)
{
	bIsPointerDown = false;
	CurrentSelectionState = GetSelectionState();
	ApplyPointerSelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerSelect_Implementation(ULexBaseEventData* EventData)
{
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerDeselect_Implementation(ULexBaseEventData* EventData)
{
	return AllowEventBubbleUp;
}

EUISelectableSelectionState UUISelectableComponent::GetSelectionState()const
{
	if (!IsInteractable())
		return EUISelectableSelectionState::Disabled;
	if (bIsPointerDown)
		return EUISelectableSelectionState::Pressed;
	if (bIsPointerInsideThis)
		return EUISelectableSelectionState::Hovered;
	return EUISelectableSelectionState::Normal;
}

void UUISelectableComponent::SetTransitionTarget(ULexVisual* Value)
{
	if (TransitionTarget != Value)
	{
		TransitionTarget = Value;
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetNormalColor(FColor Value)
{
	NormalColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Normal)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetHoveredColor(FColor Value)
{
	HoveredColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Hovered)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetPressedColor(FColor Value)
{
	PressedColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Pressed)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetDisabledColor(FColor Value)
{
	DisabledColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Disabled)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetNormalImageBrush(const FLexUIImageBrush& Value)
{
	NormalImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Normal)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetHoveredImageBrush(const FLexUIImageBrush& Value)
{
	HoveredImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Hovered)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetPressedImageBrush(const FLexUIImageBrush& Value)
{
	PressedImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Pressed)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetDisabledImageBrush(const FLexUIImageBrush& Value)
{
	DisabledImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Disabled)
	{
		ApplyPointerSelectionState(false);
	}
}
void UUISelectableComponent::SetSelectionState(EUISelectableSelectionState NewState)
{
	if (CurrentSelectionState != NewState)
	{
		CurrentSelectionState = NewState;
		ApplyPointerSelectionState(false);
	}
}
bool UUISelectableComponent::IsInteractable()const
{
	if (auto Widget = GetWidget())
	{
		return Widget->GetWidgetActiveInHierarchy() && Widget->GetInteractableInHierarchy() && bInteractable;
	}
	return bInteractable;
}

#pragma region Navigation
bool UUISelectableComponent::CanNavigateHere_Implementation() const
{
	return IsInteractable() && GetCanNavigateHere();
}
bool UUISelectableComponent::OnNavigate_Implementation(ELexUINavigationDirection direction, TScriptInterface<ILexNavigationInterface>& result)
{
	UUISelectableComponent* Selectable = nullptr;
	switch (direction)
	{
	default:
	case ELexUINavigationDirection::None:
		return false;
		break;
	case ELexUINavigationDirection::Left:
		Selectable = FindSelectableOnLeft();
		break;
	case ELexUINavigationDirection::Right:
		Selectable = FindSelectableOnRight();
		break;
	case ELexUINavigationDirection::Up:
		Selectable = FindSelectableOnUp();
		break;
	case ELexUINavigationDirection::Down:
		Selectable = FindSelectableOnDown();
		break;
	case ELexUINavigationDirection::Prev:
		Selectable = FindSelectableOnPrev();
		break;
	case ELexUINavigationDirection::Next:
		Selectable = FindSelectableOnNext();
		break;
	}
	result = Selectable;
	return true;
}
UUISelectableComponent* UUISelectableComponent::FindSelectable(FVector InDirection)
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
			auto rootCanvasUIItem = Widget->GetRootCanvas()->GetLexWidget();
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

UUISelectableComponent* UUISelectableComponent::FindSelectable(FVector InDirection, USceneComponent* InParent)
{
	auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld());
	if (LexUIManager == nullptr)return nullptr;
	const auto& SelectableArray = LexUIManager->GetAllSelectableArray();

	auto GetPointOnRectEdge = [](ULexWidget* rect, FVector2D dir)
	{
		if (dir != FVector2D::ZeroVector)
			dir /= FMath::Max(FMath::Abs(dir.X), FMath::Abs(dir.Y));
		auto center = rect->GetLocalSpaceCenter();
		dir = center + FVector2D(rect->GetWidth() * dir.X * 0.5f, rect->GetHeight() * dir.Y * 0.5f);
		return FVector(0, dir.X, dir.Y);
	};

	auto LocalPos = FVector::ZeroVector;
	const USceneComponent* RestrictNavNode = nullptr;
	if (auto Widget = GetWidget())
	{
		auto localDir = Widget->GetComponentTransform().InverseTransformVectorNoScale(InDirection);
		LocalPos = GetPointOnRectEdge(Widget, FVector2D(localDir.Y, localDir.Z));
		if (auto RestrictNavWidget = Widget->GetRestrictNavigationAreaWidget())
		{
			RestrictNavNode = RestrictNavWidget;
		}
	}
	auto pos = this->GetSceneComponent()->GetComponentTransform().TransformPosition(LocalPos);
	auto thisWidget = this->GetWidget();
	float maxScore = -MAX_flt;
	UUISelectableComponent* bestPick = this;
	for (int i = 0; i < SelectableArray.Num(); ++i)
	{
		auto sel = SelectableArray[i];

		if (sel == this || !sel.IsValid())
			continue;

		if (IsValid(InParent) && !sel->GetSceneComponent()->IsAttachedTo(InParent))
			continue;

		if (!sel->IsInteractable())
			continue;

		if (!sel->GetCanNavigateHere())
			continue;

		//if is UI node, not allow inactive one
		auto selWidget = sel->GetWidget();
		if (selWidget && !sel->GetWidget()->GetRaycastableInHierarchy())
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
		if (RestrictNavNode && !sel->GetSceneComponent()->IsAttachedTo(RestrictNavNode))
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
			selCenter = sel->GetSceneComponent()->GetRelativeLocation();
		}
		auto selCenterInWorld = sel->GetSceneComponent()->GetComponentTransform().TransformPosition(selCenter);
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
UUISelectableComponent* UUISelectableComponent::FindDefaultSelectable(UObject* WorldContextObject)
{
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(WorldContextObject->GetWorld()))
	{
		const auto& SelectableArray = LexUIManager->GetAllSelectableArray();
		if (SelectableArray.Num() > 0)
		{
			UUISelectableComponent* Selectable = nullptr;
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
				TSet<UUISelectableComponent*> FoundSelectables;
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
UUISelectableComponent* UUISelectableComponent::FindSelectableOnLeft()
{
	if (NavigationLeft == EUISelectableNavigationMode::Explicit)
	{
		return NavigationLeftSpecific.Get();
	}
	if (NavigationLeft == EUISelectableNavigationMode::Auto)
	{
		return FindSelectable(-GetSceneComponent()->GetRightVector());
	}
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnRight()
{
	if (NavigationRight == EUISelectableNavigationMode::Explicit)
	{
		return NavigationRightSpecific.Get();
	}
	if (NavigationRight == EUISelectableNavigationMode::Auto)
	{
		return FindSelectable(GetSceneComponent()->GetRightVector());
	}
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnUp()
{
	if (NavigationUp == EUISelectableNavigationMode::Explicit)
	{
		return NavigationUpSpecific.Get();
	}
	if (NavigationUp == EUISelectableNavigationMode::Auto)
	{
		return FindSelectable(GetSceneComponent()->GetUpVector());
	}
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnDown()
{
	if (NavigationDown == EUISelectableNavigationMode::Explicit)
	{
		return NavigationDownSpecific.Get();
	}
	if (NavigationDown == EUISelectableNavigationMode::Auto)
	{
		return FindSelectable(-GetSceneComponent()->GetUpVector());
	}
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnNext()
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
UUISelectableComponent* UUISelectableComponent::FindSelectableOnPrev()
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

void UUISelectableComponent::SetCanNavigateHere(bool Value)
{
	bCanNavigateHere = Value;
}
void UUISelectableComponent::SetNavigationLeft(EUISelectableNavigationMode Value)
{
	NavigationLeft = Value;
}
void UUISelectableComponent::SetNavigationRight(EUISelectableNavigationMode Value)
{
	NavigationRight = Value;
}
void UUISelectableComponent::SetNavigationUp(EUISelectableNavigationMode Value)
{
	NavigationUp = Value;
}
void UUISelectableComponent::SetNavigationDown(EUISelectableNavigationMode Value)
{
	NavigationDown = Value;
}
void UUISelectableComponent::SetNavigationPrev(EUISelectableNavigationMode Value)
{
	NavigationPrev = Value;
}
void UUISelectableComponent::SetNavigationNext(EUISelectableNavigationMode Value)
{
	NavigationNext = Value;
}

void UUISelectableComponent::SetNavigationLeftExplicit(UUISelectableComponent* Value)
{
	if (IsValid(Value))
	{
		NavigationLeftSpecific = Value;
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectableComponent::SetNavigationRightExplicit(UUISelectableComponent* Value)
{
	if (IsValid(Value))
	{
		NavigationRightSpecific = Value;
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectableComponent::SetNavigationUpExplicit(UUISelectableComponent* Value)
{
	if (IsValid(Value))
	{
		NavigationUpSpecific = Value;
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectableComponent::SetNavigationDownExplicit(UUISelectableComponent* Value)
{
	if (IsValid(Value))
	{
		NavigationDownSpecific = Value;
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectableComponent::SetNavigationPrevExplicit(UUISelectableComponent* Value)
{
	if (IsValid(Value))
	{
		NavigationPrevSpecific = Value;
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
void UUISelectableComponent::SetNavigationNextExplicit(UUISelectableComponent* Value)
{
	if (IsValid(Value))
	{
		NavigationNextSpecific = Value;
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Value is not valid!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
	}
}
#pragma endregion


