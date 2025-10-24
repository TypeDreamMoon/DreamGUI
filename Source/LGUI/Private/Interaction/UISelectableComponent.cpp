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

UE_DISABLE_OPTIMIZATION


void UUISelectableTransition::StopTransition() 
{ 
	for (auto tweener : TweenerCollection)
	{
		ULTweenBPLibrary::KillIfIsTweening(this, tweener);
	}
	TweenerCollection.Reset();
}
void UUISelectableTransition::CollectTweener(ULTweener* InItem)
{
	TweenerCollection.Add(InItem);
}
void UUISelectableTransition::CollectTweeners(const TSet<ULTweener*>& InItems)
{
	TweenerCollection.Reserve(TweenerCollection.Num() + InItems.Num());
	for (auto item : InItems)
	{
		TweenerCollection.Add(item);
	}
}

void UUISelectableTransition::BeginPlay()
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveBeginPlay();
	}
}

void UUISelectableTransition::EndPlay()
{
	if (GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native))
	{
		ReceiveEndPlay();
	}
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
void UUISelectableTransition::OnStartCustomTransition(FName InTransitionName, bool InImmediateSet)
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
	if (Transition != EUISelectableTransitionType::Custom)
	{
		if (!TransitionTarget.IsValid())return;
	}

	TOptional<FColor> Color;
	TOptional<FLexUIImageBrush> Brush;
	switch (CurrentSelectionState)
	{
	case EUISelectableSelectionState::Normal:
		{
			switch (Transition)
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
	case EUISelectableSelectionState::Hovered:
		{
			switch (Transition)
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
	case EUISelectableSelectionState::Pressed:
		{
			switch (Transition)
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
	case EUISelectableSelectionState::Disabled:
		{
			switch (Transition)
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
		eventSystemInstance->SetSelectComponent(GetLexWidget(), eventData, eventData->EnterComponentEventFireType);
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

EUISelectableSelectionState UUISelectableComponent::GetSelectionState()const
{
	if (!IsInteractable())
		return EUISelectableSelectionState::Disabled;
	if (IsPointerDown)
		return EUISelectableSelectionState::Pressed;
	if (IsPointerInsideThis)
		return EUISelectableSelectionState::Hovered;
	return EUISelectableSelectionState::Normal;
}

void UUISelectableComponent::SetTransitionTarget(ULexVisual* Value)
{
	if (TransitionTarget != Value)
	{
		TransitionTarget = Value;
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetNormalColor(FColor Value)
{
	NormalColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Normal)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetHoveredColor(FColor Value)
{
	HoveredColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Hovered)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetPressedColor(FColor Value)
{
	PressedColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Pressed)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetDisabledColor(FColor Value)
{
	DisabledColor = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Disabled)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetNormalImageBrush(const FLexUIImageBrush& Value)
{
	NormalImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Normal)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetHoveredImageBrush(const FLexUIImageBrush& Value)
{
	HoveredImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Hovered)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetPressedImageBrush(const FLexUIImageBrush& Value)
{
	PressedImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Pressed)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetDisabledImageBrush(const FLexUIImageBrush& Value)
{
	DisabledImageBrush = Value;
	if (CurrentSelectionState == EUISelectableSelectionState::Disabled)
	{
		ApplySelectionState(false);
	}
}
void UUISelectableComponent::SetSelectionState(EUISelectableSelectionState NewState)
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
	auto pos = this->GetRootSceneComponent()->GetComponentTransform().TransformPosition(LocalPos);
	auto thisWidget = this->GetLexWidget();
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
		auto selWidget = sel->GetLexWidget();
		if (selWidget && !sel->GetLexWidget()->GetRaycastableInHierarchy())
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
		if (RestrictNavNode && !sel->GetRootSceneComponent()->IsAttachedTo(RestrictNavNode))
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
			selCenter = sel->GetRootSceneComponent()->GetRelativeLocation();
		}
		auto selCenterInWorld = sel->GetRootSceneComponent()->GetComponentTransform().TransformPosition(selCenter);
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
		return FindSelectable(-GetRootSceneComponent()->GetRightVector());
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
		return FindSelectable(GetRootSceneComponent()->GetRightVector());
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
		return FindSelectable(GetRootSceneComponent()->GetUpVector());
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
		return FindSelectable(-GetRootSceneComponent()->GetUpVector());
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

UE_ENABLE_OPTIMIZATION
