// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIBehaviour.h"

#include "DreamGUI.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamWidget.h"
#include "Animation/DreamWidgetAnimationComponent.h"

UDreamUIBehaviour::UDreamUIBehaviour()
{
	bCanExecuteBlueprintEvent = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native);
	// Whether the Blueprint actually IMPLEMENTED the tick event, not merely whether it could: a
	// UFunction that lives on a Blueprint-compiled class is an override; the one on the native
	// declaring class is the empty stub. Every BP behaviour used to pay a ProcessEvent per frame
	// for a tick event it never wrote.
	if (bCanExecuteBlueprintEvent)
	{
		static const FName ReceiveTickName(TEXT("ReceiveTick"));
		const UFunction* TickFunction = GetClass()->FindFunctionByName(ReceiveTickName);
		bHasBlueprintTick = TickFunction != nullptr
			&& TickFunction->GetOuterUClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
	}
	CallbacksBeforeAwake.SetNumZeroed((int)ECallbackFunctionType::COUNT);
}

void UDreamUIBehaviour::BeginPlay()
{
	auto Widget = this->GetWidget();
	check(Widget);
	check (!this->bIsAwakeCalled);
	GetAnimationPlayer();
	this->Call_Awake();
	if (Widget->GetWidgetActiveInHierarchy())
	{
		check (!this->bIsEnableCalled);
		bCanExecuteTick = bStartWithTickEnabled;
		this->Call_OnEnable();
	}
}
void UDreamUIBehaviour::EndPlay()
{
	if (bIsEnableCalled)
	{
		Call_OnDisable();
	}
	if (bIsAwakeCalled)
	{
		Call_OnDestroy();
	}
}
void UDreamUIBehaviour::OnRegister()
{
	if (auto Widget = GetWidget())
	{
		Widget->GetWidgetActiveChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnWidgetActiveChanged);
		Widget->GetTransformChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnTransformChanged);
		Widget->GetDimensionChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnDimensionsChanged);
		Widget->GetChildDimensionChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnChildDimensionsChanged);
		Widget->GetAttachmentChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnAttachmentChanged);
		Widget->GetSiblingIndexChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnSiblingIndexChanged);
		Widget->GetInteractableChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnInteractableChanged);
		Widget->GetRaycastableChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnRaycastableChanged);
	}
}
void UDreamUIBehaviour::OnUnregister()
{
	if (IsValid(CacheWidget))
	{
		CacheWidget->GetWidgetActiveChangedEvent().RemoveAll(this);
		CacheWidget->GetTransformChangedEvent().RemoveAll(this);
		CacheWidget->GetDimensionChangedEvent().RemoveAll(this);
		CacheWidget->GetChildDimensionChangedEvent().RemoveAll(this);
		CacheWidget->GetAttachmentChangedEvent().RemoveAll(this);
		CacheWidget->GetSiblingIndexChangedEvent().RemoveAll(this);
		CacheWidget->GetInteractableChangedEvent().RemoveAll(this);
		CacheWidget->GetRaycastableChangedEvent().RemoveAll(this);
	}
	AnimationPlayer = nullptr;
}

#if WITH_EDITOR
void UDreamUIBehaviour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
	}
}
#endif

UWorld* UDreamUIBehaviour::GetWorld() const
{
	auto Widget = GetWidget();
	if (!Widget)return nullptr;
	return Widget->GetWorld();
}

int32 UDreamUIBehaviour::GetComponentIndexInWidget() const
{
	if (auto Widget = GetWidget())
	{
		return Widget->GetAllComponents().IndexOfByKey(this);
	}
	return INDEX_NONE;
}

void UDreamUIBehaviour::SetCanExecuteTick(bool Value)
{
	if (bCanExecuteTick != Value)
	{
		bCanExecuteTick = Value;
		if (bIsStartCalled)
		{
			if (bCanExecuteTick)
			{
				UDreamUIManagerWorldSubsystem::AddDreamUIBehavioursForTick(this);
			}
			else
			{
				UDreamUIManagerWorldSubsystem::RemoveDreamUIBehavioursFromTick(this);
			}
		}
	}
}

void UDreamUIBehaviour::OnEnable()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnEnable();
	}
}
void UDreamUIBehaviour::OnDisable()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnDisable();
	}
}

void UDreamUIBehaviour::OnDestroy()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnDestroy();
	}
}

void UDreamUIBehaviour::Awake()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveAwake();
	}
}
void UDreamUIBehaviour::Start()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveStart();
	}
}
void UDreamUIBehaviour::Tick(float DeltaTime)
{
	if (bCanExecuteBlueprintEvent && bHasBlueprintTick)
	{
		ReceiveTick(DeltaTime);
	}
}

void UDreamUIBehaviour::Call_Awake()
{
	for (auto& CallbackFunc : CallbacksBeforeAwake)
	{
		if (CallbackFunc != nullptr)
		{
			CallbackFunc();
		}
	}
	CallbacksBeforeAwake.Empty();
	
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (bIsAwakeCalled)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Awake already executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsAwakeCalled = true;
	Awake();
}

void UDreamUIBehaviour::Call_OnEnable()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (bIsEnableCalled)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d OnEnable already executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsEnableCalled = true;

	OnEnable();
	if (!bIsStartCalled)
	{
		UDreamUIManagerWorldSubsystem::AddDreamUIBehavioursForStart(this);
	}
	else
	{
		if (bCanExecuteTick)
		{
			UDreamUIManagerWorldSubsystem::AddDreamUIBehavioursForTick(this);
		}
	}
}

void UDreamUIBehaviour::Call_OnDisable()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (!bIsEnableCalled)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d OnEnable not executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsEnableCalled = false;

	OnDisable();
	if (!bIsStartCalled)
	{
		UDreamUIManagerWorldSubsystem::RemoveDreamUIBehavioursFromStart(this);
	}
	else
	{
		if (bCanExecuteTick)
		{
			UDreamUIManagerWorldSubsystem::RemoveDreamUIBehavioursFromTick(this);
		}
	}
}

void UDreamUIBehaviour::Call_OnDestroy()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (!bIsAwakeCalled)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Awake already executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsAwakeCalled = false;
	OnDestroy();
}

void UDreamUIBehaviour::Call_Start()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (bIsStartCalled)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Start already executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsStartCalled = true;
	Start();
}

UDreamWidget* UDreamUIBehaviour::GetWidget() const
{
	if (!IsValid(CacheWidget))
	{
		CacheWidget = this->GetTypedOuter<UDreamWidget>();
	}
	return CacheWidget.Get();
}

UDreamWidgetAnimationComponent* UDreamUIBehaviour::GetAnimationPlayer() const
{
	UDreamWidget* Widget = GetWidget();
	if (!IsValid(Widget))
	{
		AnimationPlayer = nullptr;
		return nullptr;
	}

	if (IsValid(AnimationPlayer))
	{
		UDreamWidget* HostWidget = AnimationPlayer->GetWidget();
		const bool bHostIsInHierarchy = IsValid(HostWidget)
			&& (HostWidget == Widget || Widget->IsChildOf(HostWidget));
		const bool bHostIsStillRegistered = bHostIsInHierarchy
			&& HostWidget->GetComponent<UDreamWidgetAnimationComponent>() == AnimationPlayer;
		if (bHostIsStillRegistered)
		{
			return AnimationPlayer.Get();
		}
	}

	AnimationPlayer = Widget->GetComponent<UDreamWidgetAnimationComponent>();
	if (!IsValid(AnimationPlayer))
	{
		AnimationPlayer = Widget->GetComponentInParent<UDreamWidgetAnimationComponent>();
	}
	return AnimationPlayer.Get();
}

FString UDreamUIBehaviour::GetPathDisplayName() const
{
	return GetWidget()->GetPathDisplayName() / this->GetName();
}

void UDreamUIBehaviour::DestroyComponent()
{
	GetWidget()->RemoveComponent(this);
}

void UDreamUIBehaviour::OnInteractableChanged(bool Interactable) 
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnInteractableChanged(Interactable);
	}
}

void UDreamUIBehaviour::OnTransformChanged()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnTransformChanged();
	}
}

void UDreamUIBehaviour::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	}
}

void UDreamUIBehaviour::OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged,
	bool HeightChanged)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
	}
}

void UDreamUIBehaviour::OnAttachmentChanged()
{ 
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnAttachmentChanged();
	}
}

void UDreamUIBehaviour::OnSiblingIndexChanged()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnSiblingIndexChanged();
	}
}

void UDreamUIBehaviour::OnRaycastableChanged(bool Raycastable)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnRaycastableChanged(Raycastable);
	}
}

void UDreamUIBehaviour::Call_OnInteractableChanged(bool Interactable)
{
#if WITH_EDITOR
	// A widget can genuinely have NO world -- a headless test, and a Blueprint's authoring tree,
	// whose outer is the Blueprint rather than a world. UDreamWidget::SetInteractable broadcasts
	// here through CalculateInteractable_Recursive, so any caller in either of those places used to
	// dereference null: UDreamRingMenu was simply the first control to disable one of its own parts.
	// A missing world is not a game world, so it takes the same branch edit mode does -- which is
	// the branch that actually delivers the state change (Call_OnTransformChanged's older guard
	// returns instead, and a selectable left un-notified is one that never goes grey).
	const UWorld* BehaviourWorld = this->GetWorld();
	if (BehaviourWorld == nullptr || !BehaviourWorld->IsGameWorld())//edit mode
	{
		OnInteractableChanged(Interactable);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnInteractableChanged(Interactable);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnInteractableChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnInteractableChanged(Interactable);
				}};
		}
	}
}

void UDreamUIBehaviour::Call_OnTransformChanged()
{
#if WITH_EDITOR
	if (!GetWorld())return;
	if (!GetWorld()->IsGameWorld())//edit mode
	{
		OnTransformChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnTransformChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnTransformChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnTransformChanged();
				}};
		}
	}
}

void UDreamUIBehaviour::Call_OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
#if WITH_EDITOR
	if (!GetWorld())return;
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnDimensionsChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
				}};
		}
	}
}

void UDreamUIBehaviour::Call_OnChildDimensionsChanged(UDreamWidget* Child, bool PivotChanged, bool WidthChanged,
	bool HeightChanged)
{
#if WITH_EDITOR
	if (!GetWorld())return;
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnChildDimensionsChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
				}};
		}
	}
}

void UDreamUIBehaviour::Call_OnAttachmentChanged()
{
#if WITH_EDITOR
	if (!GetWorld())return;
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnAttachmentChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnAttachmentChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnAttachmentChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnAttachmentChanged();
				}};
		}
	}
}

void UDreamUIBehaviour::Call_OnSiblingIndexChanged()
{
#if WITH_EDITOR
	if (!GetWorld())return;
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnSiblingIndexChanged();
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnSiblingIndexChanged();
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnSiblingIndexChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnSiblingIndexChanged();
				}};
		}
	}
}

void UDreamUIBehaviour::Call_OnWidgetActiveChanged(bool WidgetActive)
{
#if WITH_EDITOR
	if (!GetWorld())return;
	if (!this->GetWorld()->IsGameWorld())return;//edit mode
#endif
	if (bIsAwakeCalled)
	{
		if (WidgetActive)
		{
			if (!bIsEnableCalled)
			{
#if WITH_EDITOR
				if (GetWorld() && !GetWorld()->IsGameWorld())//edit mode
				{

				}
				else
#endif
				{
					Call_OnEnable();
				}
			}
		}
		else
		{
			if (bIsEnableCalled)
			{
#if WITH_EDITOR
				if (GetWorld() && !this->GetWorld()->IsGameWorld())//edit mode
				{

				}
				else
#endif
				{
					Call_OnDisable();
				}
			}
		}
	}
	else//awake not called, should be the first time that get WidgetActive
	{
		if (!this->GetWidget()->HasBegunPlay())
		{
			if (WidgetActive)
			{
				Call_Awake();
				if (!bIsEnableCalled)
				{
					Call_OnEnable();
				}
			}
		}
	}
}

void UDreamUIBehaviour::Call_OnRaycastableChanged(bool Raycastable)
{
#if WITH_EDITOR
	if (!GetWorld())return;
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnRaycastableChanged(Raycastable);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnRaycastableChanged(Raycastable);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnRaycastableChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnRaycastableChanged(Raycastable);
				}};
		}
	}
}
