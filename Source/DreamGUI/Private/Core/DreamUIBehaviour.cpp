// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIBehaviour.h"

#include "DreamGUI.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIGoneCount.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/Components/DreamWidget.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/DreamUIWorldContext.h"

namespace DreamUIBehaviourLocal
{
	/**
	 * Whether InClass's Blueprint implemented the event InName names: a UFunction that lives on a Blueprint-compiled class
	 * is an override, the one on the native declaring class is the empty stub. Asked per call, not cached, because a
	 * Blueprint recompile replaces the class; the lookup costs less than the ProcessEvent it saves.
	 */
	bool IsImplementedInBlueprint(const UClass* InClass, FName InName)
	{
		const UFunction* Function = InClass->FindFunctionByName(InName);
		return Function != nullptr && Function->GetOuterUClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
	}

	/** The C++ of InClass: InClass itself when it is native, otherwise the native class its Blueprint was made from. */
	const UClass* GetNativeClass(const UClass* InClass)
	{
		while (InClass != nullptr && !InClass->HasAnyClassFlags(CLASS_Native))
		{
			InClass = InClass->GetSuperClass();
		}
		return InClass;
	}
}

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

void UDreamUIBehaviour::DeclareTickUnused(const UClass* InClass)
{
	// The class being constructed is already this object's class, so a subclass's constructor, which runs after this
	// one, still finds its own class here and not InClass.
	if (DreamUIBehaviourLocal::GetNativeClass(GetClass()) == InClass)
	{
		bNativeTickUnused = true;
	}
}

void UDreamUIBehaviour::DeclareTransformChangedUnused(const UClass* InClass)
{
	if (DreamUIBehaviourLocal::GetNativeClass(GetClass()) == InClass)
	{
		bNativeTransformChangedUnused = true;
	}
}

bool UDreamUIBehaviour::HearsTransformChanges() const
{
	static const FName ReceiveOnTransformChangedName(TEXT("ReceiveOnTransformChanged"));
	return !bNativeTransformChangedUnused
		|| (bCanExecuteBlueprintEvent && DreamUIBehaviourLocal::IsImplementedInBlueprint(GetClass(), ReceiveOnTransformChangedName));
}

void UDreamUIBehaviour::PostDuplicate(EDuplicateMode::Type DuplicateMode)
{
	Super::PostDuplicate(DuplicateMode);
	if (DuplicateMode == EDuplicateMode::PIE)
	{
		DreamUI::ReportCopiedIntoPlaySession(*this);
	}
}

void UDreamUIBehaviour::BeginPlay()
{
	auto Widget = this->GetWidget();
	if (!ensureMsgf(Widget != nullptr, TEXT("%s: BeginPlay on a behaviour that belongs to no widget."), *GetPathName()))
	{
		return;
	}
	GetAnimationPlayer();
	// Already awake is not a mistake: a widget made active in a game world before it began play wakes
	// its behaviours then (Call_OnWidgetActiveChanged), and its BeginPlay still follows. What that
	// leaves owed is only what did not happen yet.
	if (!this->bIsAwakeCalled)
	{
		this->Call_Awake();
	}
	if (Widget->GetWidgetActiveInHierarchy() && !this->bIsEnableCalled)
	{
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
void UDreamUIBehaviour::Call_OnRegister()
{
	if (bIsRegisteredWithWidget)
	{
		return;
	}
	bIsRegisteredWithWidget = true;
	OnRegister();
}

void UDreamUIBehaviour::Call_OnUnregister()
{
	if (!bIsRegisteredWithWidget)
	{
		return;
	}
	bIsRegisteredWithWidget = false;
	OnUnregister();
}

void UDreamUIBehaviour::OnRegister()
{
	if (auto Widget = GetWidget())
	{
		Widget->GetWidgetActiveChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnWidgetActiveChanged);
		// Asked, not assumed: a widget with a listener is announced every move and composed for it, and most behaviours
		// have nothing to do when their widget moves.
		if (HearsTransformChanges())
		{
			Widget->GetTransformChangedEvent().AddUObject(this, &UDreamUIBehaviour::Call_OnTransformChanged);
		}
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
	// No longer one to write without a look-up (DreamUIGone).
	DreamUIGone::Note();
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
		// One with nothing to do on Tick is never in the list, allowed to tick or not.
		if (bIsStartCalled && HasTickWork())
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
	// Clear the SLOTS, not the array. It is sized once in the constructor to
	// ECallbackFunctionType::COUNT and every pre-Awake path writes it BY INDEX, so emptying it left
	// a zero-length array that the next such write indexes out of bounds -- an assertion in
	// Development and a plain out-of-bounds store in Shipping, where RangeCheck is compiled out.
	// Call_OnDestroy puts bIsAwakeCalled back to false, so that next write is reachable.
	for (TFunction<void()>& CallbackFunc : CallbacksBeforeAwake)
	{
		CallbackFunc = nullptr;
	}

#if WITH_EDITOR
	if (!DreamUI::IsGameWorld(this))//edit mode
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
	if (!DreamUI::IsGameWorld(this))//edit mode
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
		if (bCanExecuteTick && HasTickWork())
		{
			UDreamUIManagerWorldSubsystem::AddDreamUIBehavioursForTick(this);
		}
	}
}

void UDreamUIBehaviour::Call_OnDisable()
{
#if WITH_EDITOR
	if (!DreamUI::IsGameWorld(this))//edit mode
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
		if (bCanExecuteTick && HasTickWork())
		{
			UDreamUIManagerWorldSubsystem::RemoveDreamUIBehavioursFromTick(this);
		}
	}
}

void UDreamUIBehaviour::Call_OnDestroy()
{
#if WITH_EDITOR
	if (!DreamUI::IsGameWorld(this))//edit mode
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
	if (!DreamUI::IsGameWorld(this))//edit mode
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
	// The widget is the behaviour's outer, and nearly always its direct one: the cache matching that is the cache being
	// right, which the object array need not be asked about -- whether the widget is garbage -- on every call, several a
	// frame for every canvas of a world of panels. A garbage widget found so is the one GetTypedOuter would find again.
	UDreamWidget* const Cached = CacheWidget.Get();
	if (Cached != nullptr && Cached == GetOuter())
	{
		return Cached;
	}
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
	// Every behaviour on every moved widget hears this, and a Blueprint behaviour that never wrote the event used to pay a
	// ProcessEvent for it each time.
	static const FName ReceiveOnTransformChangedName(TEXT("ReceiveOnTransformChanged"));
	if (bCanExecuteBlueprintEvent && DreamUIBehaviourLocal::IsImplementedInBlueprint(GetClass(), ReceiveOnTransformChangedName))
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
	// The worldless case is why this branch is the right one to give it, and not merely the safe
	// one: it is the branch that actually DELIVERS the state change. Call_OnTransformChanged's
	// older guard returns instead, and a selectable left un-notified is one that never goes grey.
	// See DreamUIWorldContext.h for the rule and for how a widget comes to have no world.
	if (!DreamUI::IsGameWorld(this))//edit mode
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
	// Awake, the answer is the same in any world: told now, as a game build always tells it. The walk to the world below
	// is only for a behaviour that has not woken, which in a game world waits for Awake; this runs for every behaviour of
	// every widget a moving parent reaches.
	if (bIsAwakeCalled)
	{
		OnTransformChanged();
		return;
	}
#if WITH_EDITOR
	const UWorld* World = GetWorld();
	if (!World)return;
	if (!World->IsGameWorld())//edit mode
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
	const UWorld* World = GetWorld();
	if (!World)return;
	if (!World->IsGameWorld())//edit mode
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
	const UWorld* World = GetWorld();
	if (!World)return;
	if (!World->IsGameWorld())//edit mode
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
	const UWorld* World = GetWorld();
	if (!World)return;
	if (!World->IsGameWorld())//edit mode
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
	const UWorld* World = GetWorld();
	if (!World)return;
	if (!World->IsGameWorld())//edit mode
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
	if (!DreamUI::IsGameWorld(this))return;//edit mode
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
				if (GetWorld() && !GetWorld()->IsGameWorld())//edit mode
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
	const UWorld* World = GetWorld();
	if (!World)return;
	if (!World->IsGameWorld())//edit mode
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
