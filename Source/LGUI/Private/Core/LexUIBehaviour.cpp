// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIBehaviour.h"
#include "LGUI.h"
#include "Core/LGUIManager.h"
#include "Components/SceneComponent.h"
#include "Core/Actor/LexWidgetActor.h"
#include "Core/Components/LexWidget.h"

ULexUIBehaviour::ULexUIBehaviour()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	bCanExecuteBlueprintEvent = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native);
	CallbacksBeforeAwake.SetNumZeroed((int)ECallbackFunctionType::COUNT);
}

void ULexUIBehaviour::BeginPlay()
{
	Super::BeginPlay();
	ULGUIManagerWorldSubsystem::AddLGUILifeCycleBehaviourForLifecycleEvent(this);
}
void ULexUIBehaviour::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}
void ULexUIBehaviour::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}
void ULexUIBehaviour::OnRegister()
{
	Super::OnRegister();
	if (auto Widget = GetLexWidget())
	{
		Widget->GetWidgetActiveChangedEvent().AddUObject(this, &ULexUIBehaviour::Call_OnWidgetActiveChanged);
		Widget->GetTransformChangedEvent().AddUObject(this, &ULexUIBehaviour::Call_OnTransformChanged);
		Widget->GetDimensionChangedEvent().AddUObject(this, &ULexUIBehaviour::Call_OnDimensionsChanged);
		Widget->GetAttachmentChangedEvent().AddUObject(this, &ULexUIBehaviour::Call_OnAttachmentChanged);
		Widget->GetSiblingIndexChangedEvent().AddUObject(this, &ULexUIBehaviour::Call_OnSiblingIndexChanged);
		Widget->GetInteractableChangedEvent().AddUObject(this, &ULexUIBehaviour::Call_OnInteractableChanged);
		Widget->GetRaycastableChangedEvent().AddUObject(this, &ULexUIBehaviour::Call_OnRaycastableChanged);
	}
}
void ULexUIBehaviour::OnUnregister()
{
	Super::OnUnregister();
	if (CacheWidget.IsValid())
	{
		CacheWidget->GetWidgetActiveChangedEvent().RemoveAll(this);
		CacheWidget->GetTransformChangedEvent().RemoveAll(this);
		CacheWidget->GetDimensionChangedEvent().RemoveAll(this);
		CacheWidget->GetAttachmentChangedEvent().RemoveAll(this);
		CacheWidget->GetSiblingIndexChangedEvent().RemoveAll(this);
		CacheWidget->GetInteractableChangedEvent().RemoveAll(this);
		CacheWidget->GetRaycastableChangedEvent().RemoveAll(this);
	}
}

#if WITH_EDITOR
void ULexUIBehaviour::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (auto Property = PropertyChangedEvent.Property)
	{
		auto PropertyName = Property->GetFName();
	}
}
#endif

void ULexUIBehaviour::SetCanExecuteUpdate(bool Value)
{
	if (bCanExecuteUpdate != Value)
	{
		bCanExecuteUpdate = Value;
		if (bIsStartCalled)
		{
			if (bCanExecuteUpdate)
			{
				ULGUIManagerWorldSubsystem::AddLGUILifeCycleBehavioursForUpdate(this);
			}
			else
			{
				ULGUIManagerWorldSubsystem::RemoveLGUILifeCycleBehavioursFromUpdate(this);
			}
		}
	}
}

bool ULexUIBehaviour::IsAllowToCallAwake() const
{
	if (auto Widget = GetLexWidget())
	{
		return Widget->GetWidgetActiveInHierarchy();
	}
	return false;
}
void ULexUIBehaviour::Awake()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveAwake();
	}
}
void ULexUIBehaviour::Start()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveStart();
	}
}
void ULexUIBehaviour::Update(float DeltaTime)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveUpdate(DeltaTime);
	}
}

void ULexUIBehaviour::Call_Awake()
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
		UE_LOG(LGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (bIsAwakeCalled)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Awake already executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsAwakeCalled = true;
	Awake();
}

void ULexUIBehaviour::Call_Start()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Should never reach this point!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
#if !UE_BUILD_SHIPPING
	if (bIsStartCalled)
	{
		UE_LOG(LGUI, Error, TEXT("[%s].%d Start already executed!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		return;
	}
#endif
	bIsStartCalled = true;
	Start();
}

USceneComponent* ULexUIBehaviour::GetRootSceneComponent()
{
	if (!CacheSceneComp.IsValid())
	{
		if (this->GetWorld())
		{
			if (auto Owner = GetOwner())
			{
				CacheSceneComp = Owner->GetRootComponent();
			}
			else
			{
				UE_LOG(LGUI, Warning, TEXT("[%s].%d LexUIBehaviour must attach to Actor"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			}
		}
	}
	return CacheSceneComp.Get();
}
ULexWidget* ULexUIBehaviour::GetLexWidget() const
{
	if (!CacheWidget.IsValid())
	{
		if (this->GetWorld())
		{
			if (auto Owner = Cast<ALexWidgetActor>(GetOwner()))
			{
				CacheWidget = Owner->GetLexWidget();
			}
			else
			{
				UE_LOG(LGUI, Warning, TEXT("[%s].%d LexUIBehaviour must attach to a LexWidgetActor"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			}
		}
	}
	return CacheWidget.Get();
}

void ULexUIBehaviour::OnInteractableChanged(bool Interactable) 
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnInteractableChanged(Interactable);
	}
}

void ULexUIBehaviour::OnTransformChanged()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnTransformChanged();
	}
}

void ULexUIBehaviour::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
	}
}

void ULexUIBehaviour::OnChildDimensionsChanged(ULexWidget* Child, bool PivotChanged, bool WidthChanged,
	bool HeightChanged)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnChildDimensionsChanged(Child, PivotChanged, WidthChanged, HeightChanged);
	}
}

void ULexUIBehaviour::OnAttachmentChanged()
{ 
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnAttachmentChanged();
	}
}

void ULexUIBehaviour::OnSiblingIndexChanged()
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnSiblingIndexChanged();
	}
}

void ULexUIBehaviour::OnWidgetActiveChanged(bool WidgetActive)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnWidgetActiveChanged(WidgetActive);
	}
}

void ULexUIBehaviour::OnRaycastableChanged(bool Raycastable)
{
	if (bCanExecuteBlueprintEvent)
	{
		ReceiveOnRaycastableChanged(Raycastable);
	}
}

void ULexUIBehaviour::Call_OnInteractableChanged(bool Interactable)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
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

void ULexUIBehaviour::Call_OnTransformChanged()
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
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

void ULexUIBehaviour::Call_OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
#if WITH_EDITOR
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

void ULexUIBehaviour::Call_OnChildDimensionsChanged(ULexWidget* Child, bool PivotChanged, bool WidthChanged,
	bool HeightChanged)
{
#if WITH_EDITOR
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

void ULexUIBehaviour::Call_OnAttachmentChanged()
{
#if WITH_EDITOR
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

void ULexUIBehaviour::Call_OnSiblingIndexChanged()
{
#if WITH_EDITOR
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

void ULexUIBehaviour::Call_OnWidgetActiveChanged(bool WidgetActive)
{
#if WITH_EDITOR
	if (!this->GetWorld()->IsGameWorld())//edit mode
	{
		OnWidgetActiveChanged(WidgetActive);
	}
	else
#endif
	{
		if (bIsAwakeCalled)
		{
			OnWidgetActiveChanged(WidgetActive);
		}
		else
		{
			auto ThisPtr = MakeWeakObjectPtr(this);
			CallbacksBeforeAwake[(int)ECallbackFunctionType::OnWidgetActiveChanged] = [=]() {
				if (ThisPtr.IsValid())
				{
					ThisPtr->OnWidgetActiveChanged(WidgetActive);
				}};
		}
	}
}

void ULexUIBehaviour::Call_OnRaycastableChanged(bool Raycastable)
{
#if WITH_EDITOR
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
