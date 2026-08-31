// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUserWidget.h"
#include "Core/DreamUIEachAdapter.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamUIManager.h"
#include "Interaction/DreamContentWidget.h"
#include "DreamGUI.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"

/**
 * Bring a freshly built hierarchy to life, exactly as the prefab loader does at the end of a load.
 *
 * BeginPlay is gated on the MANAGER having begun play, not the world. The prefab loader learned
 * that the hard way and left a note: World->HasBegunPlay() returns false even when called from
 * BeginPlay. When it has not, the manager's own OnWorldBeginPlay picks these up later.
 */
bool DreamWidget_ShouldEditorExpandContents(const UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	if (!InWidget->IsA<UDreamUserWidget>())
	{
		// A plain widget's children are its own hierarchy. Nothing to hide.
		return true;
	}
	// Walk up rather than asking the editor which asset is open: the outermost instance on this path
	// is the one being edited, and that is decidable from the widget alone. In the designer the walk
	// stops at the design canvas's root agent; at runtime, at whatever the screen was added to.
	for (const UDreamWidget* Ancestor = InWidget->GetParent(); Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		if (Ancestor->IsA<UDreamUserWidget>())
		{
			return false;
		}
	}
	return true;
}

void CollectDreamEditorChildren(UDreamWidget* InWidget, TArray<UDreamWidget*>& OutChildren)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	if (DreamWidget_ShouldEditorExpandContents(InWidget))
	{
		OutChildren.Append(InWidget->GetChildren());
		return;
	}
	// A nested instance. Its own contents are another asset's; its slots are holes this host is
	// invited to fill, so those are the only thing it shows -- named, in the order the class declares
	// them, and present whether or not anything is in them yet. An empty slot the author cannot see
	// is a slot nobody uses.
	const UDreamUserWidget* Nested = Cast<UDreamUserWidget>(InWidget);
	if (Nested == nullptr)
	{
		return;
	}
	// The CLASS, not this instance's tree: a native control has no tree object to read slots off,
	// and asking the instance would show the author no rows at all for one.
	TArray<FName> Declared;
	UDreamUserWidget::CollectDeclaredSlotNames(Nested->GetClass(), Declared);
	for (const FName& SlotName : Declared)
	{
		if (UDreamWidget* SlotWidget = Nested->FindSlotWidget(SlotName))
		{
			OutChildren.Add(SlotWidget);
		}
	}
}

void CollectDreamWidgetsToNestedBoundary(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets, bool bIncludeRoot)
{
	if (!IsValid(InRoot))
	{
		return;
	}
	if (bIncludeRoot)
	{
		OutWidgets.Add(InRoot);
	}
	TArray<UDreamWidget*> Children;
	CollectDreamEditorChildren(InRoot, Children);
	for (UDreamWidget* Child : Children)
	{
		CollectDreamWidgetsToNestedBoundary(Child, OutWidgets, true);
	}
}

void RegisterDreamWidgetHierarchy(UDreamWidget* InRoot)
{
	if (!IsValid(InRoot))
	{
		return;
	}
	TArray<UDreamWidget*> AllWidgets;
	UDreamWidget::CollectChildrenWidgets(InRoot, AllWidgets, true);

	// Parents before children, which CollectChildrenWidgets already gives us: OnRegister reads the
	// parent link to reconcile panel slots.
	for (UDreamWidget* Widget : AllWidgets)
	{
		if (IsValid(Widget))
		{
			Widget->OnRegister();
		}
	}

	// Everything this subtree inherits from the parent it was just attached to. OnRegister does
	// this itself only for a hierarchy ROOT; a subtree parented through SetParentBeforeRegister
	// raises no attach event, so without this it registers holding its birth defaults -- visible
	// under a hidden parent, raycastable under a disabled one, and off the parent's render canvas.
	// One call here rather than at each of the four call sites, because this function IS the seam
	// every one of them goes through.
	if (InRoot->GetParent() != nullptr)
	{
		InRoot->RefreshInheritedStateFromParentChain();
	}

	if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(InRoot->GetWorld()))
	{
		// A layout tree is collected once and cached against the widget it is rooted at, and nothing
		// invalidates that cache when a widget appears -- so a subtree registered after its ancestor's
		// tree was cached is laid out by nobody, and keeps its authored defaults until something
		// re-dirties everything top-down (a viewport resize). That is how a list cell created on a
		// second pass ends up drawn as a 100x100 block of overlapping text.
		//
		// Only ancestors: a cached tree not rooted above this subtree cannot contain it. Bounded,
		// because a parent chain is only acyclic while nothing has corrupted it.
		UDreamWidget* Parent = InRoot->GetParent();
		int32 DepthGuard = 0;
		for (UDreamWidget* Ancestor = Parent;
			Ancestor != nullptr && DepthGuard < 256;
			Ancestor = Ancestor->GetParent(), ++DepthGuard)
		{
			Manager->MarkRebuildLayoutTree(Ancestor);
		}
		// Dropping the cache only decides what the next pass would see; something still has to ask for
		// a pass. Ask on the parent, since that is the widget whose contents just changed.
		if (Parent != nullptr)
		{
			Manager->AddLayoutDirtyWidget(Parent);
		}

		if (Manager->HasBegunPlay())
		{
			for (UDreamWidget* Widget : AllWidgets)
			{
				// Skipped the way OnRegister above skips an already-registered widget, and for the same
				// reason: this walk does not own every widget it covers. It collects the whole subtree,
				// nested user widgets and all, and anything built by CreateDreamWidget has already
				// registered and begun on its own -- a nested widget when its own class initialized it,
				// an `each` list's cells the moment the list was given a data source, which is before
				// the containing widget is registered at all. BeginPlay asserts rather than tolerating a
				// second call, so the caller is the one that has to know.
				if (IsValid(Widget) && !Widget->HasBegunPlay())
				{
					Widget->BeginPlay();
				}
			}
		}
	}
}

UDreamWidget* DuplicateDreamWidgetHierarchy(UObject* InOuter, UDreamWidget* InTemplate, UDreamWidget* InParent)
{
	if (!IsValid(InTemplate))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Nothing to duplicate."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	UObject* Outer = InOuter != nullptr ? InOuter : InTemplate->GetOuter();
	if (Outer == nullptr)
	{
		return nullptr;
	}
	// The deep copy, and the parent back-pointers with it.
	UDreamWidget* Copy = UDreamWidget::DuplicateSubtree(Outer, InTemplate);
	if (!IsValid(Copy))
	{
		return nullptr;
	}
	if (InParent != nullptr)
	{
		Copy->SetParentBeforeRegister(InParent);
	}
	// Registration now re-derives everything the copy inherits from its new parent, the render
	// canvas among it -- which is what kept duplicated list cells built, laid out, active and
	// invisible until it was found.
	RegisterDreamWidgetHierarchy(Copy);
	return Copy;
}

void UDreamUserWidget::Initialize()
{
	// Walk up for the tree: a subclass that only adds logic declares none of its own, and has to
	// instance its parent's. Resolving this on the class rather than here keeps a native subclass
	// (which never gets a generated class at all) working the same way.
	InitializeFromArchetype(UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(GetClass()));
}

void UDreamUserWidget::InitializeFromArchetype(UDreamWidgetTree* InArchetype)
{
	if (bInitialized || IsTemplate())
	{
		return;
	}
	bInitialized = true;

	// What the HOST hung on this widget, taken before this widget makes anything of its own. Both
	// roads that produce contents run below -- InitializeWidgetStatic instances an archetype,
	// NativeOnInitialized realizes a native control's tree -- so this snapshot is exactly "not
	// mine", and AdoptUnslottedChildren needs no other rule to tell guests from furniture.
	TArray<UDreamWidget*> HostSuppliedChildren(GetChildren());

	UDreamWidgetGeneratedClass::InitializeWidgetStatic(this, GetClass(), InArchetype);

	// The Blueprint surface's wiring, before NativeOnInitialized so OnInitialized code can already
	// SetWantsTick or SetAllowEventBubbleUp and have it hold. The bridge carries lifecycle, pointer,
	// drag and navigation delivery (see UDreamUserWidgetEventBridge); focus rides this widget's own
	// existing broadcasts, bound here because Initialize is the one moment every instance passes
	// through exactly once.
	EnsureEventBridge();
	OnFocusReceived.AddUniqueDynamic(this, &UDreamUserWidget::HandleFocusReceivedBroadcast);
	OnFocusLost.AddUniqueDynamic(this, &UDreamUserWidget::HandleFocusLostBroadcast);
	{
		// Whether the Blueprint actually implemented OnTick: the UFunction on a Blueprint-compiled
		// class is an override, the one on the native declaring class is the empty stub. Same probe
		// UDreamUIBehaviour runs for ReceiveTick, cached for the same per-frame reason.
		static const FName OnTickName(TEXT("OnTick"));
		const UFunction* TickFunction = GetClass()->FindFunctionByName(OnTickName);
		bHasBlueprintOnTick = TickFunction != nullptr
			&& TickFunction->GetOuterUClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
	}

	// UMG's TickFrequency=Auto, translated: implementing On Tick IS opting in. bWantsTick stays
	// off by default because the widgets that never tick should pay nothing, but a Blueprint that
	// put the event in its graph means it -- without this the graph compiles, PIE runs, and nothing
	// fires, with no line anywhere saying why. Before NativeOnInitialized, so an OnInitialized that
	// explicitly calls SetWantsTick(false) still wins.
	if (bHasBlueprintOnTick && !bWantsTick)
	{
		SetWantsTick(true);
	}

	// Before anything reads this widget's data. Everything below pulls from it -- a binding reads a
	// property, an `each` list asks its source how many rows there are -- so a subclass filling a list
	// source gets the floor here. Later is after the first frame has already been composed from
	// whatever the source held, which for a list built at Begin Play is nothing.
	NativeOnInitialized();

	// Both kinds of contents now exist, and nothing is registered yet: the one moment that works for
	// an archetype-instanced tree and a code-built one alike.
	AttachNamedSlotContent();
	AdoptUnslottedChildren(HostSuppliedChildren);
	NativeOnSlotContentAttached();

	// After the tree exists: the bindings name widgets in it.
	ResolvePropertyBindings();
	BindEventBindings();
	ResolveEachBindings();
	if (ResolvedBindings.Num() > 0)
	{
		// Once now, so the first frame shows bound values rather than the authored ones. All of
		// them: a subscribed binding's broadcast only fires on the NEXT change, and the current
		// value has to reach the widget too.
		EvaluatePropertyBindings();
		if (HasPolledPropertyBindings())
		{
			// Only the polled remainder needs the per-frame visit; the subscribed bindings
			// re-evaluate from their field's broadcast.
			if (UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
			{
				Manager->AddPropertyBindingUser(this);
			}
		}
	}
}

void UDreamUserWidget::NativeOnInitialized()
{
	OnInitialized();
}

#pragma region BlueprintSurface
void UDreamUserWidget::EnsureEventBridge()
{
	UWorld* World = GetWorld();
	if (World == nullptr || !World->IsGameWorld())
	{
		// Edit worlds -- the designer's preview above all -- never get a bridge: behaviour lifecycle
		// refuses edit mode anyway, and a component the author did not add has no business in a
		// preview whose component list the editor reads back.
		return;
	}
	if (GetComponent<UDreamUserWidgetEventBridge>() != nullptr)
	{
		// A duplicated instance (a list cell) arrives with its copy through the Instanced Components
		// array; a second bridge would double every event.
		return;
	}
	if (UDreamUIBehaviour* Bridge = AddComponent<UDreamUserWidgetEventBridge>())
	{
		// The class is already Transient; the instance flag keeps even the REFERENCE out of any
		// serializer that ever walks a live game-world tree.
		Bridge->SetFlags(RF_Transient);
	}
}

void UDreamUserWidget::NativeOnConstruct()
{
	bConstructed = true;
	OnConstruct();
}

void UDreamUserWidget::NativeOnDestruct()
{
	bConstructed = false;
	OnDestruct();
}

void UDreamUserWidget::NativeOnEnable()
{
	OnEnable();
}

void UDreamUserWidget::NativeOnDisable()
{
	OnDisable();
}

void UDreamUserWidget::NativeOnTick(float DeltaTime)
{
	if (bHasBlueprintOnTick)
	{
		OnTick(DeltaTime);
	}
}

void UDreamUserWidget::SetWantsTick(bool Value)
{
	if (bWantsTick == Value)
	{
		return;
	}
	bWantsTick = Value;
	if (UDreamUserWidgetEventBridge* Bridge = GetComponent<UDreamUserWidgetEventBridge>())
	{
		Bridge->SyncTickEnabled(Value);
	}
	// No bridge -- a template, an edit world, or before Initialize -- means nothing is registered
	// anywhere; the bridge reads bWantsTick when it awakes.
}

bool UDreamUserWidget::NativeOnPointerEnter(UDreamPointerEventData* EventData)
{
	OnPointerEnter(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerExit(UDreamPointerEventData* EventData)
{
	OnPointerExit(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerDown(UDreamPointerEventData* EventData)
{
	OnPointerDown(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerUp(UDreamPointerEventData* EventData)
{
	OnPointerUp(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnPointerClick(UDreamPointerEventData* EventData)
{
	OnPointerClick(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnBeginDrag(UDreamPointerEventData* EventData)
{
	OnBeginDrag(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnDrag(UDreamPointerEventData* EventData)
{
	OnDrag(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnEndDrag(UDreamPointerEventData* EventData)
{
	OnEndDrag(EventData);
	return bAllowEventBubbleUp;
}

bool UDreamUserWidget::NativeOnDrop(UDreamPointerEventData* EventData)
{
	OnDrop(EventData);
	return bAllowEventBubbleUp;
}

void UDreamUserWidget::NativeOnFocusReceived(int32 UserIndex, int32 PointerId)
{
	ReceiveFocusReceived(UserIndex, PointerId);
}

void UDreamUserWidget::NativeOnFocusLost(int32 UserIndex, int32 PointerId)
{
	ReceiveFocusLost(UserIndex, PointerId);
}

void UDreamUserWidget::HandleFocusReceivedBroadcast(int32 UserIndex, int32 PointerId)
{
	NativeOnFocusReceived(UserIndex, PointerId);
}

void UDreamUserWidget::HandleFocusLostBroadcast(int32 UserIndex, int32 PointerId)
{
	NativeOnFocusLost(UserIndex, PointerId);
}

void UDreamUserWidget::NativeOnNavigate(EDreamUINavigationDirection Direction, UDreamWidget*& OutNextWidget)
{
	OutNextWidget = OnNavigate(Direction);
}
#pragma endregion

#pragma region EventBridge
UDreamUserWidgetEventBridge::UDreamUserWidgetEventBridge()
{
	// Opt-in cost: never in the tick list until the widget asks. Awake re-reads the widget's
	// bWantsTick, so these are only the values for the window before Awake -- both lowered so
	// IsTickForwardingEnabled cannot claim ticking that is not armed.
	bStartWithTickEnabled = false;
	bCanExecuteTick = false;
}

UDreamUserWidget* UDreamUserWidgetEventBridge::GetUserWidget() const
{
	return Cast<UDreamUserWidget>(GetWidget());
}

void UDreamUserWidgetEventBridge::SyncTickEnabled(bool bValue)
{
	bStartWithTickEnabled = bValue;
	if (bIsEnableCalled)
	{
		// Enabled: the standard door, which adds to or removes from the manager's tick list once
		// Start has run and leaves the flag for the start pass to read when it has not.
		SetCanExecuteTick(bValue);
	}
	else
	{
		// Disabled (or not yet begun): no tick registration exists, so going through
		// SetCanExecuteTick on a started behaviour would try to remove what OnDisable already
		// removed and log a spurious warning. OnEnable reads the flag and registers.
		bCanExecuteTick = bValue;
	}
}

void UDreamUserWidgetEventBridge::Awake()
{
	Super::Awake();
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		// Before UDreamUIBehaviour::BeginPlay copies bStartWithTickEnabled into bCanExecuteTick,
		// which happens right after Awake returns.
		bStartWithTickEnabled = UserWidget->GetWantsTick();
		UserWidget->NativeOnConstruct();
		++ConstructForwardCount;
	}
}

void UDreamUserWidgetEventBridge::OnEnable()
{
	Super::OnEnable();
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		UserWidget->NativeOnEnable();
		++EnableForwardCount;
	}
}

void UDreamUserWidgetEventBridge::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		UserWidget->NativeOnTick(DeltaTime);
		++TickForwardCount;
	}
}

void UDreamUserWidgetEventBridge::OnDisable()
{
	Super::OnDisable();
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		UserWidget->NativeOnDisable();
		++DisableForwardCount;
	}
}

void UDreamUserWidgetEventBridge::OnDestroy()
{
	Super::OnDestroy();
	if (UDreamUserWidget* UserWidget = GetUserWidget())
	{
		UserWidget->NativeOnDestruct();
		++DestructForwardCount;
	}
}

bool UDreamUserWidgetEventBridge::OnPointerEnter_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerEnter(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerExit_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerExit(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerDown_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerDown(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerUp_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerUp(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerClick_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnPointerClick(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnBeginDrag(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerDrag_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnDrag(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnEndDrag(EventData) : true;
}

bool UDreamUserWidgetEventBridge::OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	return UserWidget != nullptr ? UserWidget->NativeOnDrop(EventData) : true;
}

bool UDreamUserWidgetEventBridge::CanNavigateHere_Implementation() const
{
	const UDreamUserWidget* UserWidget = Cast<UDreamUserWidget>(GetWidget());
	return UserWidget != nullptr
		&& UserWidget->GetCanNavigateHere()
		&& UserWidget->GetWidgetActiveInHierarchy()
		&& UserWidget->GetInteractableInHierarchy();
}

bool UDreamUserWidgetEventBridge::OnNavigate_Implementation(EDreamUINavigationDirection direction, TScriptInterface<IDreamNavigationInterface>& result)
{
	UDreamUserWidget* UserWidget = GetUserWidget();
	if (UserWidget == nullptr)
	{
		return false;
	}
	UDreamWidget* NextWidget = nullptr;
	UserWidget->NativeOnNavigate(direction, NextWidget);
	if (IsValid(NextWidget))
	{
		// The navigation module speaks to behaviours (its result is cast to UDreamUIBehaviour), so a
		// widget answer resolves to that widget's navigation-capable component -- a UISelectable, a
		// nested user widget's own bridge, or any custom handler.
		if (UDreamUIBehaviour* NextHandler = NextWidget->GetComponentByInterface(UDreamNavigationInterface::StaticClass()))
		{
			result.SetObject(NextHandler);
			result.SetInterface(Cast<IDreamNavigationInterface>(NextHandler));
		}
		else
		{
			UE_LOG(DreamGUI, Warning,
				TEXT("[%s].%d OnNavigate on '%s' returned '%s', which has no navigation-capable behaviour; staying put."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *UserWidget->GetPathDisplayName(), *NextWidget->GetPathDisplayName());
		}
	}
	// True with a null result keeps the highlight here: an opted-in widget that names no successor
	// is a navigation sink, which is what it opted in to be.
	return true;
}
#pragma endregion

void UDreamUserWidget::FFieldNotificationClassDescriptor::ForEachField(const UClass* Class, TFunctionRef<bool(::UE::FieldNotification::FFieldId FieldId)> Callback) const
{
	if (const UBlueprintGeneratedClass* BPClass = Cast<const UBlueprintGeneratedClass>(Class))
	{
		BPClass->ForEachFieldNotify(Callback, true);
	}
}

FDelegateHandle UDreamUserWidget::AddFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FFieldValueChangedDelegate InNewDelegate)
{
	return NotificationDelegates.AddFieldValueChangedDelegate(this, InFieldId, MoveTemp(InNewDelegate));
}

bool UDreamUserWidget::RemoveFieldValueChangedDelegate(UE::FieldNotification::FFieldId InFieldId, FDelegateHandle InHandle)
{
	return NotificationDelegates.RemoveFieldValueChangedDelegate(this, InFieldId, InHandle);
}

int32 UDreamUserWidget::RemoveAllFieldValueChangedDelegates(FDelegateUserObjectConst InUserObject)
{
	return NotificationDelegates.RemoveAllFieldValueChangedDelegates(this, InUserObject);
}

int32 UDreamUserWidget::RemoveAllFieldValueChangedDelegates(UE::FieldNotification::FFieldId InFieldId, FDelegateUserObjectConst InUserObject)
{
	return NotificationDelegates.RemoveAllFieldValueChangedDelegates(this, InFieldId, InUserObject);
}

const UE::FieldNotification::IClassDescriptor& UDreamUserWidget::GetFieldNotificationDescriptor() const
{
	static FFieldNotificationClassDescriptor Local;
	return Local;
}

void UDreamUserWidget::BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId)
{
	NotificationDelegates.BroadcastFieldValueChanged(this, InFieldId);
}

void UDreamUserWidget::K2_AddFieldValueChangedDelegate(FFieldNotificationId InFieldId, FFieldValueChangedDynamicDelegate InDelegate)
{
	if (InFieldId.IsValid())
	{
		const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), InFieldId.FieldName);
		if (FieldId.IsValid())
		{
			NotificationDelegates.AddFieldValueChangedDelegate(this, FieldId, InDelegate);
		}
	}
}

void UDreamUserWidget::K2_RemoveFieldValueChangedDelegate(FFieldNotificationId InFieldId, FFieldValueChangedDynamicDelegate InDelegate)
{
	if (InFieldId.IsValid())
	{
		const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), InFieldId.FieldName);
		if (FieldId.IsValid())
		{
			NotificationDelegates.RemoveFieldValueChangedDelegate(this, FieldId, InDelegate);
		}
	}
}

void UDreamUserWidget::BindEventBindings()
{
	TArray<FDreamWidgetEventBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectEventBindings(GetClass(), Bindings);
	for (const FDreamWidgetEventBinding& Binding : Bindings)
	{
		FObjectPropertyBase* WidgetProperty = FindFProperty<FObjectPropertyBase>(GetClass(), Binding.WidgetName);
		if (WidgetProperty == nullptr)
		{
			continue;
		}
		UDreamWidget* TargetWidget = Cast<UDreamWidget>(WidgetProperty->GetObjectPropertyValue_InContainer(this));
		UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Binding.Target, Binding.BehaviourIndex);
		if (!IsValid(Target) || FindFunction(Binding.FunctionName) == nullptr)
		{
			// The compiler checked all of this; reaching here means the class moved underneath us,
			// which is the property bindings' rule too: skip, never guess.
			continue;
		}
		FMulticastDelegateProperty* Event = CastField<FMulticastDelegateProperty>(
			Target->GetClass()->FindPropertyByName(Binding.EventName));
		if (Event == nullptr)
		{
			continue;
		}
		// AddDelegate, not Set: an event can have other listeners, and a route from the file is one
		// more of them, not a replacement. The delegate holds this widget weakly, so an instance
		// dying does not need an unbind pass -- broadcast skips dead entries.
		FScriptDelegate Route;
		Route.BindUFunction(this, Binding.FunctionName);
		Event->AddDelegate(MoveTemp(Route), Target);
	}
}

void UDreamUserWidget::ResolvePropertyBindings()
{
	ResolvedBindings.Reset();
	PolledBindingCount = 0;

	TArray<FDreamWidgetPropertyBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectPropertyBindings(GetClass(), Bindings);
	if (Bindings.Num() == 0)
	{
		return;
	}

	// One subscription per distinct source field, no matter how many bindings read it; the handler
	// fans out to every binding carrying that id.
	TSet<int32> SubscribedFieldIndices;

	for (const FDreamWidgetPropertyBinding& Binding : Bindings)
	{
		// The target widget is reached the same way everything else reaches one: the class property
		// the compiler named after it, which InitializeWidgetStatic has already filled in.
		FObjectPropertyBase* WidgetProperty = FindFProperty<FObjectPropertyBase>(GetClass(), Binding.WidgetName);
		if (WidgetProperty == nullptr)
		{
			continue;
		}
		UDreamWidget* TargetWidget = Cast<UDreamWidget>(WidgetProperty->GetObjectPropertyValue_InContainer(this));
		UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Binding.Target, Binding.BehaviourIndex);
		if (!IsValid(Target))
		{
			continue;
		}
		UFunction* SourceFunction = FindFunction(Binding.FunctionName);
		UFunction* Setter = Target->FindFunction(Binding.SetterName);
		if (SourceFunction == nullptr || Setter == nullptr)
		{
			continue;
		}

		FResolvedBinding& Resolved = ResolvedBindings.AddDefaulted_GetRef();
		Resolved.Target = Target;
		Resolved.SourceFunction = SourceFunction;
		Resolved.Setter = Setter;

		// A source function the class marked FieldNotify tells us when it changes; everything else
		// can change silently and stays on the per-frame poll. The classification is per instance
		// only because the resolution is -- the answer is a pure function of the class. A two-way
		// binding names its VARIABLE in NotifyField -- the function is only its generated getter,
		// which no broadcast will ever carry -- so the subscription keys on the variable instead.
		Resolved.SourceFieldId = GetFieldNotificationDescriptor().GetField(GetClass(),
			Binding.NotifyField.IsNone() ? Binding.FunctionName : Binding.NotifyField);
		if (Resolved.SourceFieldId.IsValid())
		{
			bool bAlreadySubscribed = false;
			SubscribedFieldIndices.Add(Resolved.SourceFieldId.GetIndex(), &bAlreadySubscribed);
			if (!bAlreadySubscribed)
			{
				// Bound to self: the delegate store lives on this same object, so lifetime is
				// co-terminal and no unbind pass is owed.
				AddFieldValueChangedDelegate(Resolved.SourceFieldId,
					FFieldValueChangedDelegate::CreateUObject(this, &UDreamUserWidget::HandleSourceFieldValueChanged));
			}
		}
		else
		{
			++PolledBindingCount;
		}
	}
}

void UDreamUserWidget::EvaluateBinding(const FResolvedBinding& Binding)
{
	UObject* Target = Binding.Target.Get();
	if (!IsValid(Target) || Binding.SourceFunction == nullptr || Binding.Setter == nullptr)
	{
		return;
	}

	// FStructOnScope rather than a raw buffer: a returned FText or FString has to be constructed
	// before ProcessEvent writes it and destroyed afterwards, and this does both.
	FStructOnScope SourceFrame(Binding.SourceFunction);
	ProcessEvent(Binding.SourceFunction, SourceFrame.GetStructMemory());

	FProperty* ReturnProperty = Binding.SourceFunction->GetReturnProperty();
	FProperty* SetterParameter = nullptr;
	for (TFieldIterator<FProperty> It(Binding.Setter); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		SetterParameter = *It;
		break;
	}
	if (ReturnProperty == nullptr || SetterParameter == nullptr
		|| !ReturnProperty->SameType(SetterParameter))
	{
		// The compiler checked this pairing; reaching here means the class moved underneath us.
		return;
	}

	FStructOnScope SetterFrame(Binding.Setter);
	SetterParameter->CopyCompleteValue(
		SetterParameter->ContainerPtrToValuePtr<void>(SetterFrame.GetStructMemory()),
		ReturnProperty->ContainerPtrToValuePtr<void>(SourceFrame.GetStructMemory()));
	Target->ProcessEvent(Binding.Setter, SetterFrame.GetStructMemory());
}

void UDreamUserWidget::EvaluatePropertyBindings()
{
	for (const FResolvedBinding& Binding : ResolvedBindings)
	{
		EvaluateBinding(Binding);
	}
}

void UDreamUserWidget::EvaluatePolledPropertyBindings()
{
	for (const FResolvedBinding& Binding : ResolvedBindings)
	{
		if (!Binding.SourceFieldId.IsValid())
		{
			EvaluateBinding(Binding);
		}
	}
}

void UDreamUserWidget::HandleSourceFieldValueChanged(UObject* InObject, UE::FieldNotification::FFieldId InFieldId)
{
	for (const FResolvedBinding& Binding : ResolvedBindings)
	{
		if (Binding.SourceFieldId.IsValid() && Binding.SourceFieldId.GetName() == InFieldId.GetName())
		{
			EvaluateBinding(Binding);
		}
	}
}

void UDreamUserWidget::ResolveEachBindings()
{
	EachAdapters.Reset();

	TArray<FDreamWidgetEachBinding> Bindings;
	UDreamWidgetGeneratedClass::CollectEachBindings(GetClass(), Bindings);
	if (Bindings.Num() == 0)
	{
		return;
	}

	TSet<int32> SubscribedFieldIndices;
	for (const FDreamWidgetEachBinding& Binding : Bindings)
	{
		auto FindWidgetByVariable = [this](FName InName) -> UDreamWidget*
		{
			FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(GetClass(), InName);
			return Property != nullptr ? Cast<UDreamWidget>(Property->GetObjectPropertyValue_InContainer(this)) : nullptr;
		};
		UDreamWidget* Host = FindWidgetByVariable(Binding.HostWidgetName);
		UDreamWidget* Template = FindWidgetByVariable(Binding.TemplateWidgetName);
		UUIRecyclableScrollView* ListView = IsValid(Host) ? Host->GetComponent<UUIRecyclableScrollView>() : nullptr;
		if (!IsValid(ListView) || !IsValid(Template))
		{
			// The compiler and builder vetted all of this; the class moved underneath us. Skip.
			continue;
		}

		// The view's Content pointer was authored against the archetype; re-aim it at THIS
		// instance's content, the same per-instance re-wiring the template gets below. Without it
		// every cell the view clones lands in the invisible archetype tree. The synthesized
		// content may not have earned a class variable, so the template's own parent -- which IS
		// that content whenever the builder synthesized one -- is the fallback.
		if (!Binding.ContentWidgetName.IsNone())
		{
			UDreamWidget* Content = FindWidgetByVariable(Binding.ContentWidgetName);
			if (!IsValid(Content) && Template->GetParent() != Host)
			{
				Content = Template->GetParent();
			}
			if (IsValid(Content))
			{
				ListView->SetContent(Content);
			}
		}

		UDreamUIEachAdapter* Adapter = NewObject<UDreamUIEachAdapter>(this);
		Adapter->Initialize(this, Binding, ListView);
		EachAdapters.Add(Adapter);

		ListView->SetCellTemplate(Template);
		TScriptInterface<IUIRecyclableScrollViewDataSource> DataSource;
		DataSource.SetObject(Adapter);
		DataSource.SetInterface(Cast<IUIRecyclableScrollViewDataSource>(Adapter));
		ListView->SetDataSource(DataSource);

		// A variable source that broadcasts refreshes its list the way a FieldNotify binding
		// re-evaluates: from the change, not from a poll.
		if (!Binding.bSourceIsFunction)
		{
			const UE::FieldNotification::FFieldId FieldId = GetFieldNotificationDescriptor().GetField(GetClass(), Binding.SourceName);
			if (FieldId.IsValid())
			{
				bool bAlreadySubscribed = false;
				SubscribedFieldIndices.Add(FieldId.GetIndex(), &bAlreadySubscribed);
				if (!bAlreadySubscribed)
				{
					AddFieldValueChangedDelegate(FieldId,
						FFieldValueChangedDelegate::CreateUObject(this, &UDreamUserWidget::HandleEachSourceChanged));
				}
			}
		}
	}
}

void UDreamUserWidget::HandleEachSourceChanged(UObject* InObject, UE::FieldNotification::FFieldId InFieldId)
{
	for (UDreamUIEachAdapter* Adapter : EachAdapters)
	{
		if (IsValid(Adapter) && !Adapter->GetBinding().bSourceIsFunction
			&& Adapter->GetBinding().SourceName == InFieldId.GetName())
		{
			Adapter->Refresh();
		}
	}
}

void UDreamUserWidget::RefreshEachBindings()
{
	for (UDreamUIEachAdapter* Adapter : EachAdapters)
	{
		if (IsValid(Adapter))
		{
			Adapter->Refresh();
		}
	}
}

UDreamWidget* UDreamUserWidget::GetContentRoot() const
{
	return IsValid(WidgetTree) ? WidgetTree->RootWidget : nullptr;
}

void UDreamUserWidget::CollectDeclaredSlotNames(const UDreamWidgetTree* InTree, TArray<FName>& OutNames)
{
	if (!IsValid(InTree))
	{
		return;
	}
	InTree->ForEachWidget([&OutNames](UDreamWidget* Widget)
	{
		if (const UDreamNamedSlot* Slot = Widget->GetComponent<UDreamNamedSlot>())
		{
			const FName SlotName = Slot->GetSlotName();
			// A duplicate slot name is a mistake the class author has to see; the compiler reports it
			// (DreamWidgetBlueprintCompiler). Listing it once here keeps every consumer agreeing on
			// what the class offers rather than each de-duplicating differently.
			if (!SlotName.IsNone())
			{
				OutNames.AddUnique(SlotName);
			}
		}
	});
}

void UDreamUserWidget::CollectDeclaredSlotNames(const UClass* InClass, TArray<FName>& OutNames)
{
	if (InClass == nullptr || !InClass->IsChildOf(UDreamUserWidget::StaticClass()))
	{
		return;
	}
	// The archetype's slots first, so an archetype-built class is answered exactly as before.
	CollectDeclaredSlotNames(UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(InClass), OutNames);

	// Then what the class declares for itself. The CDO, because this is a question about the class
	// and is asked before any instance exists -- the compiler asks it, and so does a designer
	// hierarchy row for a control nobody has placed yet.
	if (const UDreamUserWidget* Defaults = InClass->GetDefaultObject<UDreamUserWidget>())
	{
		for (const FName& SlotName : Defaults->GetNativeSlotNames())
		{
			if (!SlotName.IsNone())
			{
				OutNames.AddUnique(SlotName);
			}
		}
	}
}

TArray<FName> UDreamUserWidget::GetNativeSlotNames() const
{
	// Nothing by default: a class built from an archetype declares its slots in that archetype, and
	// answering for it here would be a second source for the same question.
	return TArray<FName>();
}

FName UDreamUserWidget::GetDefaultSlotName() const
{
	return NAME_None;
}

UDreamWidget* UDreamUserWidget::FindSlotWidget(FName InSlotName) const
{
	if (InSlotName.IsNone())
	{
		return nullptr;
	}
	UDreamWidget* Found = nullptr;
	const auto Consider = [&Found, InSlotName](UDreamWidget* Widget)
	{
		if (Found != nullptr || !IsValid(Widget))
		{
			return;
		}
		if (const UDreamNamedSlot* Slot = Widget->GetComponent<UDreamNamedSlot>())
		{
			if (Slot->GetSlotName() == InSlotName)
			{
				Found = Widget;
			}
		}
	};

	if (IsValid(WidgetTree))
	{
		WidgetTree->ForEachWidget(Consider);
		return Found;
	}

	// A native control: no tree object, because nothing instanced a template to make its contents --
	// it built them under itself. A plain structural walk, and NOT CollectDreamWidgetsToNestedBoundary,
	// whose children are editor-semantic: for a nested instance that function returns the very slot
	// rows this function is being asked for, so using it here would ask the question to answer it.
	//
	// Stopping at a nested instance is the same boundary either way. A slot inside a Button placed
	// inside this control is that Button's hole, and answering with it would let a host fill it from
	// outside the asset that opened it.
	TArray<UDreamWidget*> Pending(GetChildren());
	while (Pending.Num() > 0 && Found == nullptr)
	{
		UDreamWidget* Widget = Pending.Pop(EAllowShrinking::No);
		if (!IsValid(Widget))
		{
			continue;
		}
		Consider(Widget);
		if (!Widget->IsA<UDreamUserWidget>())
		{
			Pending.Append(Widget->GetChildren());
		}
	}
	return Found;
}

void UDreamUserWidget::AttachNamedSlotContent()
{
	// The content objects belong to the host's tree and arrived with it; all that is left is to hang
	// each under the UDreamNamedSlot of that name inside this instance. Done by this widget rather
	// than by the host, because only it knows where its own slots are.
	for (const TPair<FName, TObjectPtr<UDreamWidget>>& Binding : NamedSlotContent)
	{
		UDreamWidget* Content = Binding.Value;
		if (!IsValid(Content))
		{
			continue;
		}
		UDreamWidget* SlotWidget = FindSlotWidget(Binding.Key);
		if (!IsValid(SlotWidget))
		{
			// The class dropped or renamed a slot the host still binds. Silently discarding it is how
			// content disappears from a screen with nothing in the log to say why; the compiler
			// reports this as an error on the host too, but a class can change after that compile.
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' has no slot named '%s'; the content bound to it is not shown."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetClass()->GetName(), *Binding.Key.ToString());
			continue;
		}
		if (Content->HasRegistered())
		{
			Content->TrySetParent(SlotWidget, false);
		}
		else
		{
			Content->SetParentBeforeRegister(SlotWidget);
		}
	}
}

void UDreamUserWidget::AdoptUnslottedChildren(const TArray<UDreamWidget*>& InHostSupplied)
{
	const FName DefaultSlot = GetDefaultSlotName();
	if (DefaultSlot.IsNone() || InHostSupplied.Num() == 0)
	{
		return;
	}
	UDreamWidget* SlotWidget = FindSlotWidget(DefaultSlot);
	if (!IsValid(SlotWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' names '%s' as its default slot but opens no slot of that name."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetClass()->GetName(), *DefaultSlot.ToString());
		return;
	}
	for (UDreamWidget* Content : InHostSupplied)
	{
		// Still a child of this widget means no named binding claimed it: AttachNamedSlotContent
		// re-parents what it places, so anything it took is already somewhere else.
		if (!IsValid(Content) || Content->GetParent() != this || SlotWidget->IsChildOf(Content))
		{
			continue;
		}
		if (Content->HasRegistered())
		{
			// Try, not Set: a refusal is real (a cycle, or a slot already holding its one child) and
			// silent otherwise -- the content would vanish from a hierarchy that still looks right.
			// World position dropped on purpose, the same call AddChild makes: content handed to a
			// slot is handed to that slot's arrangement.
			if (!Content->TrySetParent(SlotWidget, false))
			{
				UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' refused '%s' as default-slot content."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *SlotWidget->GetDisplayName(), *Content->GetDisplayName());
			}
		}
		else
		{
			Content->SetParentBeforeRegister(SlotWidget);
		}
	}
}

UDreamWidget* UDreamUserWidget::GetContentForNamedSlot(FName InSlotName) const
{
	const TObjectPtr<UDreamWidget>* Found = NamedSlotContent.Find(InSlotName);
	return Found != nullptr && IsValid(*Found) ? Found->Get() : nullptr;
}

bool UDreamUserWidget::SetContentForNamedSlot(FName InSlotName, UDreamWidget* InContent)
{
	if (InSlotName.IsNone())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Cannot bind content to an unnamed slot on '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathDisplayName());
		return false;
	}
	if (InContent == nullptr)
	{
		NamedSlotContent.Remove(InSlotName);
		return true;
	}
	if (InContent == this || InContent->IsChildOf(this) || this->IsChildOf(InContent))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' cannot go into a slot of '%s': one contains the other."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InContent->GetPathDisplayName(), *GetPathDisplayName());
		return false;
	}
	// The content must be the host's own. Taking a widget out of a third asset would make this a
	// cross-asset difference record, which is the thing P4 deleted and is not coming back.
	if (InContent->GetTypedOuter<UDreamWidgetTree>() != this->GetTypedOuter<UDreamWidgetTree>())
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' belongs to another hierarchy; a slot is filled by the host that placed '%s'."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InContent->GetPathDisplayName(), *GetPathDisplayName());
		return false;
	}
	NamedSlotContent.Add(InSlotName, InContent);
	return true;
}

UDreamUserWidget* CreateDreamWidget(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InClass, UDreamWidget* InParent,
	const TFunction<void(UDreamUserWidget*)>& InCallbackBeforeAlive)
{
	if (!IsValid(InWorld))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d CreateDreamWidget needs a valid world."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	if (!IsValid(InClass))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d CreateDreamWidget needs a valid class."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}

	// Same ownership rule a prefab load follows: join the parent's tree, or mint one outered to the
	// world so GetTypedOuter<UWorld> resolves for everything inside.
	UObject* Owner = nullptr;
	UDreamWidgetTree* OwnedTree = nullptr;
	if (IsValid(InParent) && InParent->GetOuter() != nullptr)
	{
		Owner = InParent->GetOuter();
	}
	else
	{
		OwnedTree = NewObject<UDreamWidgetTree>(InWorld);
		Owner = OwnedTree;
	}

	UDreamUserWidget* UserWidget = NewObject<UDreamUserWidget>(Owner, InClass, NAME_None, RF_Transactional);
	if (OwnedTree != nullptr)
	{
		OwnedTree->RootWidget = UserWidget;
	}
	UserWidget->Initialize();
	// Parent first, then registration: OnRegister reconciles the panel slot against the parent, so
	// registering an orphan and attaching it afterwards produces a widget the parent never laid out.
	if (IsValid(InParent))
	{
		UserWidget->SetParentBeforeRegister(InParent);
	}
	// Last chance to reshape what was built before anything observes it. See the header.
	if (InCallbackBeforeAlive)
	{
		InCallbackBeforeAlive(UserWidget);
	}
	RegisterDreamWidgetHierarchy(UserWidget);
	return UserWidget;
}
