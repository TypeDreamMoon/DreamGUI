// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UISelectableComponent.h"
#include "LGUI.h"
#include "LTweenBPLibrary.h"
#include "Core/Components/LexVisual.h"
#include "Core/LexUIManager.h"
#include "LTweenManager.h"
#include "Core/Components/LexCanvas.h"
#include "Event/LexEventSystem.h"
#include "Core/Components/LexSprite.h"
#include "Core/LexUISpriteData_BaseObject.h"
#include "Core/LexUISettings.h"
#include "Core/Components/LexImage.h"
#if WITH_EDITOR
#include "Utils/LexUIUtils.h"
#endif

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_DISABLE_OPTIMIZATION
#endif


void UUISelectableTransitionComponent::StopTransition() 
{ 
	for (auto tweener : TweenerCollection)
	{
		ULTweenBPLibrary::KillIfIsTweening(this, tweener);
	}
	TweenerCollection.Reset();
}
void UUISelectableTransitionComponent::CollectTweener(ULTweener* InItem)
{
	TweenerCollection.Add(InItem);
}
void UUISelectableTransitionComponent::CollectTweeners(const TSet<ULTweener*>& InItems)
{
	TweenerCollection.Reserve(TweenerCollection.Num() + InItems.Num());
	for (auto item : InItems)
	{
		TweenerCollection.Add(item);
	}
}

void UUISelectableTransitionComponent::BeginPlay()
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveBeginPlay();
	}
}

void UUISelectableTransitionComponent::EndPlay()
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveEndPlay();
	}
}

void UUISelectableTransitionComponent::OnNormal(bool InImmediateSet)
{ 
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnNormal(InImmediateSet);
	}
}
void UUISelectableTransitionComponent::OnHovered(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnHovered(InImmediateSet);
	}
}
void UUISelectableTransitionComponent::OnPressed(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnPressed(InImmediateSet);
	}
}
void UUISelectableTransitionComponent::OnDisabled(bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnDisabled(InImmediateSet);
	}
}
void UUISelectableTransitionComponent::OnStartCustomTransition(FName InTransitionName, bool InImmediateSet)
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveOnStartCustomTransition(InTransitionName, InImmediateSet);
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
	if (IsValid(CustomTransition))
	{
		CustomTransition->BeginPlay();
	}
}

void UUISelectableComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	if (IsValid(CustomTransition))
	{
		CustomTransition->EndPlay();
	}
}

void UUISelectableComponent::OnRegister()
{
	Super::OnRegister();
	ULexUIManagerWorldSubsystem::AddSelectable(this);
	CurrentSelectionState = GetSelectionState();
	ApplySelectionState(true);
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
		ApplySelectionState(true);
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
		ApplySelectionState(true);
	}
	else
#endif
	{
		ApplySelectionState(false);
	}
}

void UUISelectableComponent::ApplySelectionState(bool ImmediateSet)
{
	if (Transition != ELexUISelectableTransitionType::Custom)
	{
		if (!TransitionTarget.IsValid())return;
	}

	TOptional<FColor> Color;
	TOptional<FLexUIImageBrush> Brush;
	switch (CurrentSelectionState)
	{
	case ELexUISelectableSelectionState::Normal:
		{
			switch (Transition)
			{
			case ELexUISelectableTransitionType::None:break;
			case ELexUISelectableTransitionType::Color:
				{
					Color = NormalColor;
				}
				break;
			case ELexUISelectableTransitionType::ImageBrush:
				{
					Brush = NormalImageBrush;
				}
				break;
			case ELexUISelectableTransitionType::Custom:
				{
#if WITH_EDITOR
					if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
					{
						if (IsValid(CustomTransition))
						{
							CustomTransition->OnNormal(ImmediateSet);
						}
					}
				}
				break;
			}
		}
		break;
	case ELexUISelectableSelectionState::Hovered:
		{
			switch (Transition)
			{
			case ELexUISelectableTransitionType::None:break;
			case ELexUISelectableTransitionType::Color:
				{
					Color = HoveredColor;
				}
				break;
			case ELexUISelectableTransitionType::ImageBrush:
				{
					Brush = HoveredImageBrush;
				}
				break;
			case ELexUISelectableTransitionType::Custom:
				{
#if WITH_EDITOR
					if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
					{
						if (IsValid(CustomTransition))
						{
							CustomTransition->OnHovered(ImmediateSet);
						}
					}
				}
				break;
			}
		}
		break;
	case ELexUISelectableSelectionState::Pressed:
		{
			switch (Transition)
			{
			case ELexUISelectableTransitionType::None:break;
			case ELexUISelectableTransitionType::Color:
				{
					Color = PressedColor;
				}
				break;
			case ELexUISelectableTransitionType::ImageBrush:
				{
					Brush = PressedImageBrush;
				}
				break;
			case ELexUISelectableTransitionType::Custom:
				{
#if WITH_EDITOR
					if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
					{
						if (IsValid(CustomTransition))
						{
							CustomTransition->OnPressed(ImmediateSet);
						}
					}
				}
				break;
			}
		}
		break;
	case ELexUISelectableSelectionState::Disabled:
		{
			switch (Transition)
			{
			case ELexUISelectableTransitionType::None:break;
			case ELexUISelectableTransitionType::Color:
				{
					Color = DisabledColor;
				}
				break;
			case ELexUISelectableTransitionType::ImageBrush:
				{
					Brush =  DisabledImageBrush;
				}
				break;
			case ELexUISelectableTransitionType::Custom:
				{
#if WITH_EDITOR
					if (this->GetWorld() && this->GetWorld()->IsGameWorld())
#endif
					{
						if (IsValid(CustomTransition))
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
				bool bAffectByGamePause = false;
				bool bAffectByTimeDilation = false;
				if (this->GetLexWidget()->IsScreenSpaceOverlayUI())
				{
					bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
					bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
				}
				else
				{
					bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
					bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
				}
				TransitionTweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
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
						bool bAffectByGamePause = false;
						bool bAffectByTimeDilation = false;
						if (this->GetLexWidget()->IsScreenSpaceOverlayUI())
						{
							bAffectByGamePause = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByGamePause;
							bAffectByTimeDilation = GetDefault<ULexUISettings>()->bScreenSpaceUIAffectByTimeDilation;
						}
						else
						{
							bAffectByGamePause = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByGamePause;
							bAffectByTimeDilation = GetDefault<ULexUISettings>()->bWorldSpaceUIAffectByTimeDilation;
						}
						TransitionTweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
					}
				}
			}
		}
	}
}

bool UUISelectableComponent::OnPointerEnter_Implementation(ULexPointerEventData* eventData)
{
	IsPointerInsideThis = true;
	CurrentSelectionState = GetSelectionState();
	ApplySelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerExit_Implementation(ULexPointerEventData* eventData)
{
	IsPointerInsideThis = false;
	CurrentSelectionState = GetSelectionState();
	ApplySelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerDown_Implementation(ULexPointerEventData* eventData)
{
	IsPointerDown = true;
	CurrentSelectionState = GetSelectionState();
	ApplySelectionState(false);
	if (auto eventSystemInstance = ULexEventSystem::GetLexEventSystemInstance(this))
	{
		eventSystemInstance->SetSelectComponent(GetLexWidget(), eventData, eventData->enterComponentEventFireType);
	}
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerUp_Implementation(ULexPointerEventData* eventData)
{
	IsPointerDown = false;
	CurrentSelectionState = GetSelectionState();
	ApplySelectionState(false);
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerSelect_Implementation(ULexBaseEventData* eventData)
{
	return AllowEventBubbleUp;
}
bool UUISelectableComponent::OnPointerDeselect_Implementation(ULexBaseEventData* eventData)
{
	return AllowEventBubbleUp;
}

ELexUISelectableSelectionState UUISelectableComponent::GetSelectionState()const
{
	if (!IsInteractable())
		return ELexUISelectableSelectionState::Disabled;
	if (IsPointerDown)
		return ELexUISelectableSelectionState::Pressed;
	if (IsPointerInsideThis)
		return ELexUISelectableSelectionState::Hovered;
	return ELexUISelectableSelectionState::Normal;
}

void UUISelectableComponent::SetTransitionTarget(ULexVisual* value)
{
	if (TransitionTarget != value)
	{
		TransitionTarget = value;
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetNormalColor(FColor Value)
{
	NormalColor = Value;
	if (CurrentSelectionState == ELexUISelectableSelectionState::Normal)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetHoveredColor(FColor Value)
{
	HoveredColor = Value;
	if (CurrentSelectionState == ELexUISelectableSelectionState::Hovered)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetPressedColor(FColor Value)
{
	PressedColor = Value;
	if (CurrentSelectionState == ELexUISelectableSelectionState::Pressed)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetDisabledColor(FColor Value)
{
	DisabledColor = Value;
	if (CurrentSelectionState == ELexUISelectableSelectionState::Disabled)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetNormalImageBrush(const FLexUIImageBrush& Value)
{
	NormalImageBrush = Value;
	if (CurrentSelectionState == ELexUISelectableSelectionState::Normal)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetHoveredImageBrush(const FLexUIImageBrush& Value)
{
	HoveredImageBrush = Value;
	if (CurrentSelectionState == ELexUISelectableSelectionState::Hovered)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetPressedImageBrush(const FLexUIImageBrush& Value)
{
	PressedImageBrush = Value;
	if (CurrentSelectionState == ELexUISelectableSelectionState::Pressed)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetDisabledImageBrush(const FLexUIImageBrush& Value)
{
	DisabledImageBrush = Value;
	if (CurrentSelectionState == ELexUISelectableSelectionState::Disabled)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetSelectionState(ELexUISelectableSelectionState NewState)
{
	if (CurrentSelectionState != NewState)
	{
		CurrentSelectionState = NewState;
		ApplySelectionState(false);
	}
}
bool UUISelectableComponent::IsInteractable()const
{
	if (auto Widget = GetLexWidget())
	{
		return Widget->GetInteractableInHierarchy() && bInteractable;
	}
	return bInteractable;
}

#pragma region Navigation
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
	if (auto Widget = GetLexWidget())
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
	auto LGUIManagerActor = ULexUIManagerWorldSubsystem::GetInstance(this->GetWorld());
	if (LGUIManagerActor == nullptr)return nullptr;
	const auto& SelectableArray = LGUIManagerActor->GetAllSelectableArray();

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
	if (auto Widget = GetLexWidget())
	{
		auto localDir = Widget->GetComponentTransform().InverseTransformVectorNoScale(InDirection);
		LocalPos = GetPointOnRectEdge(Widget, FVector2D(localDir.Y, localDir.Z));
		if (auto RestrictNavWidget = Widget->GetRestrictNavigationAreaWidget())
		{
			RestrictNavNode = RestrictNavWidget;
		}
	}
	auto pos = GetRootSceneComponent()->GetComponentTransform().TransformPosition(LocalPos);
	float maxScore = -MAX_flt;
	UUISelectableComponent* bestPick = this;
	for (int i = 0; i < SelectableArray.Num(); ++i)
	{
		auto sel = SelectableArray[i];

		if (sel == this || !sel.IsValid())
			continue;

		if (IsValid(InParent) && !sel->GetRootSceneComponent()->IsAttachedTo(InParent))
			continue;

		if (!sel->IsInteractable())
			continue;

		if (!sel->GetCanNavigateHere())
			continue;

		//if is UI node, not allow inactive one
		auto selRootUIComp = sel->GetLexWidget();
		if (selRootUIComp && !sel->GetLexWidget()->GetRaycastableInHierarchy())
		{
			continue;
		}

		//if navigation is restricted, only allow child of restric node
		if (RestrictNavNode && !sel->GetRootSceneComponent()->IsAttachedTo(RestrictNavNode))
		{
			continue;
		}

#if WITH_EDITOR
		if (this->GetWorld() != sel->GetWorld())
			continue;
#endif

		FVector selCenter;
		if (selRootUIComp)
		{
			auto LocalCenter2D = selRootUIComp->GetLocalSpaceCenter();
			selCenter = FVector(0, LocalCenter2D.X, LocalCenter2D.Y);
		}
		else
		{
			selCenter = sel->GetRootSceneComponent()->GetRelativeLocation();
		}
		auto selCenterInWorld = sel->GetRootSceneComponent()->GetComponentTransform().TransformPosition(selCenter);
		if (selRootUIComp)
		{
			if (selRootUIComp->IsPointVisibleOnClip(selCenterInWorld))
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
	if (auto LGUIManagerActor = ULexUIManagerWorldSubsystem::GetInstance(WorldContextObject->GetWorld()))
	{
		const auto& SelectableArray = LGUIManagerActor->GetAllSelectableArray();
		if (SelectableArray.Num() > 0)
		{
			auto Selectable = SelectableArray[0].Get();
			//default selectable is the most "prev" one, so we need to find it
			TSet<UUISelectableComponent*> FoundSelectables;
			while (true)
			{
				FoundSelectables.Add(Selectable);
				//change navigation mode to auto, so we can find selectable only by position (exclude explicit)
				auto OriginNavigationLeftMode = Selectable->NavigationLeft;
				auto OriginNavigationUpMode = Selectable->NavigationUp;
				auto OriginNavigationPrevMode = Selectable->NavigationPrev;
				Selectable->NavigationLeft = ELexUISelectableNavigationMode::Auto;
				Selectable->NavigationUp = ELexUISelectableNavigationMode::Auto;
				Selectable->NavigationPrev = ELexUISelectableNavigationMode::Auto;

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
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnLeft()
{
	if (NavigationLeft == ELexUISelectableNavigationMode::Explicit)
	{
		return NavigationLeftSpecific.GetComponent<UUISelectableComponent>();
	}
	if (NavigationLeft == ELexUISelectableNavigationMode::Auto)
	{
		return FindSelectable(-GetRootSceneComponent()->GetRightVector());
	}
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnRight()
{
	if (NavigationRight == ELexUISelectableNavigationMode::Explicit)
	{
		return NavigationRightSpecific.GetComponent<UUISelectableComponent>();
	}
	if (NavigationRight == ELexUISelectableNavigationMode::Auto)
	{
		return FindSelectable(GetRootSceneComponent()->GetRightVector());
	}
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnUp()
{
	if (NavigationUp == ELexUISelectableNavigationMode::Explicit)
	{
		return NavigationUpSpecific.GetComponent<UUISelectableComponent>();
	}
	if (NavigationUp == ELexUISelectableNavigationMode::Auto)
	{
		return FindSelectable(GetRootSceneComponent()->GetUpVector());
	}
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnDown()
{
	if (NavigationDown == ELexUISelectableNavigationMode::Explicit)
	{
		return NavigationDownSpecific.GetComponent<UUISelectableComponent>();
	}
	if (NavigationDown == ELexUISelectableNavigationMode::Auto)
	{
		return FindSelectable(-GetRootSceneComponent()->GetUpVector());
	}
	return nullptr;
}
UUISelectableComponent* UUISelectableComponent::FindSelectableOnNext()
{
	if (NavigationNext == ELexUISelectableNavigationMode::Explicit && NavigationNextSpecific.IsValidComponentReference())
	{
		return NavigationNextSpecific.GetComponent<UUISelectableComponent>();
	}
	if (NavigationNext == ELexUISelectableNavigationMode::Auto)
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
	if (NavigationPrev == ELexUISelectableNavigationMode::Explicit)
	{
		return NavigationPrevSpecific.GetComponent<UUISelectableComponent>();
	}
	if (NavigationPrev == ELexUISelectableNavigationMode::Auto)
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

void UUISelectableComponent::SetCanNavigateHere(bool value)
{
	bCanNavigateHere = value;
}
void UUISelectableComponent::SetNavigationLeft(ELexUISelectableNavigationMode value)
{
	NavigationLeft = value;
}
void UUISelectableComponent::SetNavigationRight(ELexUISelectableNavigationMode value)
{
	NavigationRight = value;
}
void UUISelectableComponent::SetNavigationUp(ELexUISelectableNavigationMode value)
{
	NavigationUp = value;
}
void UUISelectableComponent::SetNavigationDown(ELexUISelectableNavigationMode value)
{
	NavigationDown = value;
}
void UUISelectableComponent::SetNavigationPrev(ELexUISelectableNavigationMode value)
{
	NavigationPrev = value;
}
void UUISelectableComponent::SetNavigationNext(ELexUISelectableNavigationMode value)
{
	NavigationNext = value;
}

void UUISelectableComponent::SetNavigationLeftExplicit(UUISelectableComponent* value)
{
	if (IsValid(value))
	{
		NavigationLeftSpecific = FLGUIComponentReference(value);
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[UUISelectableComponent::SetNavigationLeftExplicit] value is not valid!"));
	}
}
void UUISelectableComponent::SetNavigationRightExplicit(UUISelectableComponent* value)
{
	if (IsValid(value))
	{
		NavigationRightSpecific = FLGUIComponentReference(value);
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[UUISelectableComponent::SetNavigationRightExplicit] value is not valid!"));
	}
}
void UUISelectableComponent::SetNavigationUpExplicit(UUISelectableComponent* value)
{
	if (IsValid(value))
	{
		NavigationUpSpecific = FLGUIComponentReference(value);
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[UUISelectableComponent::SetNavigationUpExplicit] value is not valid!"));
	}
}
void UUISelectableComponent::SetNavigationDownExplicit(UUISelectableComponent* value)
{
	if (IsValid(value))
	{
		NavigationDownSpecific = FLGUIComponentReference(value);
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[UUISelectableComponent::SetNavigationDownExplicit] value is not valid!"));
	}
}
void UUISelectableComponent::SetNavigationPrevExplicit(UUISelectableComponent* value)
{
	if (IsValid(value))
	{
		NavigationPrevSpecific = FLGUIComponentReference(value);
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[UUISelectableComponent::SetNavigationPrevExplicit] value is not valid!"));
	}
}
void UUISelectableComponent::SetNavigationNextExplicit(UUISelectableComponent* value)
{
	if (IsValid(value))
	{
		NavigationNextSpecific = FLGUIComponentReference(value);
	}
	else
	{
		UE_LOG(LGUI, Error, TEXT("[UUISelectableComponent::SetNavigationNextExplicit] value is not valid!"));
	}
}
#pragma endregion

#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif
