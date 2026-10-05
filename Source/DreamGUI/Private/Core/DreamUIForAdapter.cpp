// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIForAdapter.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBindingObserver.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "DreamGUI.h"
#include "DreamUIBPLibrary.h"
#include "Event/DreamUIEventDelegate.h"
#include "INotifyFieldValueChanged.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Class.h"
#include "UObject/ScriptDelegates.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace DreamUIForAdapterLocal
{
	/**
	 * How many frames a refresh may wait for a duplicated template to be adopted (see IsTemplateReadyToCopy) before
	 * it goes ahead regardless. The adoption pass finishes inside the call that made the duplicate, so one frame is
	 * always enough when anything is going to happen at all; the bound is for the case where it never will -- a
	 * duplicate DuplicateDreamWidgetHierarchy could not adopt -- which must not keep a ticker alive for ever.
	 */
	constexpr int32 MaxDeferredRefreshes = 4;

	/**
	 * A user widget that was duplicated and has not been handed its own tree yet: it still answers every question
	 * about its contents from the SOURCE's tree. The same test DuplicateDreamWidgetHierarchy's adoption pass uses to
	 * decide that a duplicate needs adopting -- a content root -- plus "not done yet".
	 */
	bool IsUnadoptedDuplicate(const UDreamWidget* InWidget)
	{
		const UDreamUserWidget* Nested = Cast<UDreamUserWidget>(InWidget);
		return Nested != nullptr && !Nested->IsInitialized() && Nested->GetContentRoot() != nullptr;
	}

	/**
	 * The widgets of one copy that the item bindings can name: the copy, and everything its file wrote inside it.
	 *
	 * Not UDreamWidget::CollectChildrenWidgets, which is what the `each` adapter walks. That one goes on into a nested
	 * user widget's own contents, which belong to the nested widget's class and are named by ITS file -- and the
	 * lookup below keeps the first widget of each display name, so a row whose class happens to call a widget what
	 * the host calls one of its own wrote the item onto the row's innards instead. The walk stops at each nested
	 * user widget and goes on only into its slots, which is where the host's own widgets inside it live: the same
	 * line CollectDreamEditorChildren draws for the designer, without the designer's "outermost instance" rule,
	 * which a copy deep inside a screen would always fail.
	 */
	void CollectAuthoredWidgets(UDreamWidget* InWidget, TArray<UDreamWidget*>& OutWidgets)
	{
		if (!IsValid(InWidget))
		{
			return;
		}
		OutWidgets.Add(InWidget);
		if (const UDreamUserWidget* Nested = Cast<UDreamUserWidget>(InWidget))
		{
			if (IsUnadoptedDuplicate(Nested))
			{
				// Its slots would be looked up in the source's tree, and a write there lands on somebody else's widget.
				return;
			}
			TArray<FName> SlotNames;
			UDreamUserWidget::CollectDeclaredSlotNames(Nested->GetClass(), SlotNames);
			for (const FName& SlotName : SlotNames)
			{
				CollectAuthoredWidgets(Nested->FindSlotWidget(SlotName), OutWidgets);
			}
			return;
		}
		for (UDreamWidget* Child : InWidget->GetChildren())
		{
			CollectAuthoredWidgets(Child, OutWidgets);
		}
	}

	/** InPanel's live children, in order -- the array sibling indices count, once the invalid entries are gone. */
	TArray<UDreamWidget*> LiveChildrenOf(const UDreamWidget* InPanel)
	{
		TArray<UDreamWidget*> Live;
		for (UDreamWidget* Child : InPanel->GetChildren())
		{
			if (IsValid(Child))
			{
				Live.Add(Child);
			}
		}
		return Live;
	}
}

// ============================================================================================ FDreamUIEntryRows

namespace DreamUIEntryRowsLocal
{
	/** The FDreamUIEventDelegate an event property is, or null when it is some other kind of property. */
	FDreamUIEventDelegate* AsEventDelegate(FProperty* InEventProperty, UObject* InTarget)
	{
		FStructProperty* StructEvent = CastField<FStructProperty>(InEventProperty);
		if (StructEvent == nullptr || StructEvent->Struct != FDreamUIEventDelegate::StaticStruct())
		{
			return nullptr;
		}
		return StructEvent->ContainerPtrToValuePtr<FDreamUIEventDelegate>(InTarget);
	}

	/**
	 * Whether InFunction may listen to a dynamic delegate of signature InSignature: it takes exactly what the delegate
	 * sends, or -- the author's `()`, or a handler that wants nothing -- it takes nothing at all.
	 *
	 * Nothing at all is safe to call from a delegate that sends parameters: the delegate hands ProcessEvent its own
	 * parameter frame, and ProcessEvent copies only the CALLEE's ParmsSize bytes out of it (UObject::ProcessEvent,
	 * ScriptCore.cpp: `FMemory::Memcpy(Frame, Parms, Function->ParmsSize)`), which for a function with no parameters and
	 * no return value is zero; a native thunk for such a function reads nothing from the frame either. A return value
	 * would be written back into the caller's frame, which a delegate without one does not have room for, so "nothing"
	 * means no return value too (NumParms counts it); and a delegate that itself returns something gets no such
	 * shortcut -- its caller reads the result.
	 */
	bool FitsDelegate(const UFunction* InSignature, const UFunction* InFunction, bool bInCallWithoutArguments)
	{
		const bool bSignatureReturns = InSignature != nullptr && InSignature->GetReturnProperty() != nullptr;
		if (InFunction->NumParms == 0 && !bSignatureReturns)
		{
			return true;
		}
		if (bInCallWithoutArguments || InSignature == nullptr)
		{
			return false;
		}
		return InSignature->IsSignatureCompatibleWith(InFunction);
	}

	/**
	 * Route InTarget's event InEventProperty to InItem's InFunction, if the function fits the event -- false, and nothing
	 * placed, when it does not: the compiler only checks the shape when the source's element class is known, and a
	 * mismatch against an array of UObject is skipped here, silently, as every other loop mismatch is.
	 */
	bool PlaceRoute(UObject* InTarget, FProperty* InEventProperty, UObject* InItem, UFunction* InFunction, bool bInCallWithoutArguments)
	{
		const FName FunctionName = InFunction->GetFName();
		if (FDreamUIEventDelegate* EventDelegate = AsEventDelegate(InEventProperty, InTarget))
		{
			// What UDreamUIEventDelegateParameterHelper::IsStillSupported accepts, which is what the event checks again
			// every time it fires: exactly the event's own parameter type, or -- through the no-argument route -- none.
			if (InFunction->NumParms == 0)
			{
				EventDelegate->AddRuntimeRoute(InItem, FunctionName, /*bInCallWithoutArguments*/true);
				return true;
			}
			if (bInCallWithoutArguments
				|| !UDreamUIEventDelegateParameterHelper::IsStillSupported(InFunction, EventDelegate->GetNativeParameterType()))
			{
				return false;
			}
			EventDelegate->AddRuntimeRoute(InItem, FunctionName);
			return true;
		}
		if (FMulticastDelegateProperty* Multicast = CastField<FMulticastDelegateProperty>(InEventProperty))
		{
			if (!FitsDelegate(Multicast->SignatureFunction, InFunction, bInCallWithoutArguments))
			{
				return false;
			}
			// AddDelegate, which adds uniquely: one more listener beside the control's own and the graph's, as
			// UDreamUserWidget::BindEventBindings routes a class-level `->`.
			FScriptDelegate Route;
			Route.BindUFunction(InItem, FunctionName);
			Multicast->AddDelegate(MoveTemp(Route), InTarget);
			return true;
		}
		if (FDelegateProperty* SingleCast = CastField<FDelegateProperty>(InEventProperty))
		{
			if (!FitsDelegate(SingleCast->SignatureFunction, InFunction, bInCallWithoutArguments))
			{
				return false;
			}
			// The one slot, replaced: `=` means exactly that, and a single-cast delegate has nowhere for a second.
			if (FScriptDelegate* Slot = SingleCast->GetPropertyValuePtr_InContainer(InTarget))
			{
				Slot->BindUFunction(InItem, FunctionName);
				return true;
			}
		}
		return false;
	}

	/** Take back what PlaceRoute placed for InItem's InFunctionName. Only that: whatever else listens stays. */
	void TakeRouteOff(UObject* InTarget, FProperty* InEventProperty, UObject* InItem, FName InFunctionName)
	{
		if (FDreamUIEventDelegate* EventDelegate = AsEventDelegate(InEventProperty, InTarget))
		{
			// The struct holds its target strongly, so the item is alive whenever there is an entry to take off.
			if (IsValid(InItem))
			{
				EventDelegate->RemoveRuntimeRoute(InItem, InFunctionName);
			}
			return;
		}
		if (FMulticastDelegateProperty* Multicast = CastField<FMulticastDelegateProperty>(InEventProperty))
		{
			// A listener whose object died is never called and is compacted by the delegate itself.
			if (IsValid(InItem))
			{
				FScriptDelegate Route;
				Route.BindUFunction(InItem, InFunctionName);
				Multicast->RemoveDelegate(Route, InTarget);
			}
			return;
		}
		if (FDelegateProperty* SingleCast = CastField<FDelegateProperty>(InEventProperty))
		{
			// Cleared only while it still names this item's function: something set afterwards -- a graph, another
			// `=` -- is somebody else's, and clearing it would be taking a listener this row never placed.
			FScriptDelegate* Slot = SingleCast->GetPropertyValuePtr_InContainer(InTarget);
			if (Slot != nullptr && Slot->GetFunctionName() == InFunctionName
				&& (IsValid(InItem) ? Slot->GetUObject() == InItem : !Slot->IsBound()))
			{
				Slot->Unbind();
			}
		}
	}

	/**
	 * Take off whatever listens to InTarget's event through InFunctionName and was not placed by this row: what a widget
	 * duplicated while it carried a route brings along (see FDreamUIEntryRows). A single-cast delegate needs nothing --
	 * the row's own route replaces it.
	 */
	void ScrubCarriedRoutes(UObject* InTarget, FProperty* InEventProperty, FName InFunctionName)
	{
		if (FDreamUIEventDelegate* EventDelegate = AsEventDelegate(InEventProperty, InTarget))
		{
			// Every runtime route to the function, whoever it calls: a duplicate's (its Transient target did not come
			// across) or another adapter's that fed these cells before (an owner that re-resolved).
			EventDelegate->RemoveRuntimeRoute(nullptr, InFunctionName);
			return;
		}
		FMulticastDelegateProperty* Multicast = CastField<FMulticastDelegateProperty>(InEventProperty);
		if (Multicast == nullptr)
		{
			return;
		}
		const FMulticastScriptDelegate* Listeners = Multicast->GetMulticastDelegate(Multicast->ContainerPtrToValuePtr<void>(InTarget));
		if (Listeners == nullptr)
		{
			return;
		}
		// A copy of the list: removing changes the delegate's own.
		for (UObject* Listener : Listeners->GetAllObjects())
		{
			FScriptDelegate Carried;
			Carried.BindUFunction(Listener, InFunctionName);
			Multicast->RemoveDelegate(Carried, InTarget);
		}
	}

	UObject* ResolveRouteTarget(const FDreamWidgetEntryRoute& InRoute, const TMap<FName, UDreamWidget*>& InWidgetsByDisplayName)
	{
		UDreamWidget* const* FoundWidget = InWidgetsByDisplayName.Find(InRoute.TargetWidgetDisplayName);
		return ResolveDreamWidgetBindingTarget(FoundWidget != nullptr ? *FoundWidget : nullptr, InRoute.Target, InRoute.BehaviourIndex);
	}
}

FDreamUIEntryRows::~FDreamUIEntryRows()
{
	// Rows' watchers stop in their own destructors. Routes are left: the rows' widgets are the adapter owner's, and they
	// go down with it.
}

void FDreamUIEntryRows::Initialize(UObject* InLifetimeOwner, FOnEntryChanged InOnEntryChanged)
{
	StopWatching();
	for (TPair<TObjectKey<UDreamWidget>, FRow>& Pair : Rows)
	{
		Retire(MoveTemp(Pair.Value.Observer));
	}
	Rows.Reset();
	LifetimeOwner = InLifetimeOwner;
	OnEntryChanged = MoveTemp(InOnEntryChanged);
	LastRuledClass.Reset();
	bLastRuledClassAnnounces = false;
}

void FDreamUIEntryRows::AimRow(UDreamWidget* InRow, UObject* InItem, const FDreamWidgetEachBinding& InBinding,
	const TMap<FName, UDreamWidget*>& InWidgetsByDisplayName)
{
	if (!IsValid(InRow) || !HasRowWork(InBinding))
	{
		return;
	}
	UObject* NewItem = IsValid(InItem) ? InItem : nullptr;

	const TObjectKey<UDreamWidget> Key(InRow);
	FRow* Found = Rows.Find(Key);
	const bool bFirstSight = Found == nullptr;
	FRow& Row = bFirstSight ? Rows.Add(Key) : *Found;
	if (!bFirstSight && Row.Item.Get() == NewItem && (NewItem != nullptr || !Row.Item.IsStale()))
	{
		// Showing it already: its routes are on and its watcher is on it.
		return;
	}
	Row.Widget = InRow;

	// Off the old item before anything goes on the new one: between the two the row calls nobody, never both.
	TakeRoutesOff(Row);
	Row.Item = NewItem;

	if (NewItem == nullptr)
	{
		if (Row.Observer.IsValid())
		{
			Row.Observer->Stop();
		}
	}
	else if (Row.Observer.IsValid())
	{
		// Re-aimed: SetRoot re-subscribes along every path only when it was started, so a watcher a row showing nothing
		// had stopped is started again.
		Row.Observer->SetRoot(NewItem);
		if (!Row.Observer->IsStarted())
		{
			Row.Observer->Start();
		}
	}
	else if (ShouldWatch(NewItem->GetClass(), InBinding))
	{
		Row.Observer = MakeUnique<FDreamUIBindingObserver>();
		// Weak on the lifetime owner, as every delegate the watcher places on the item is: an adapter collected before
		// the item announces anything is simply not called.
		const TWeakObjectPtr<UDreamWidget> WeakRow(InRow);
		Row.Observer->Initialize(NewItem, LifetimeOwner.Get(),
			FDreamUIBindingObserver::FOnClientChanged::CreateWeakLambda(LifetimeOwner.Get(), [this, WeakRow](int32 InClientId)
			{
				HandleClientChanged(WeakRow, InClientId);
			}));
		for (int32 EntryIndex = 0; EntryIndex < InBinding.EntryBindings.Num(); ++EntryIndex)
		{
			// Every entry, announced or not: the watcher subscribes only what the item's class announces, and the
			// entry's index is what a report names.
			const FName Member = InBinding.EntryBindings[EntryIndex].ItemMember;
			Row.Observer->AddPath(MakeArrayView(&Member, 1), EntryIndex);
		}
		Row.Observer->Start();
	}

	PlaceRoutes(Row, NewItem, InBinding, InWidgetsByDisplayName, /*bInScrubFirst*/bFirstSight);
}

void FDreamUIEntryRows::ReleaseRow(const UDreamWidget* InRow, bool bInUnbindRoutes)
{
	FRow Released;
	if (!Rows.RemoveAndCopyValue(TObjectKey<UDreamWidget>(InRow), Released))
	{
		return;
	}
	if (bInUnbindRoutes)
	{
		TakeRoutesOff(Released);
	}
	Retire(MoveTemp(Released.Observer));
}

void FDreamUIEntryRows::ReleaseAll(bool bInUnbindRoutes)
{
	// Taken out first: unbinding can run nothing, but a watcher stopping is not the place to find the map half-emptied.
	TMap<TObjectKey<UDreamWidget>, FRow> Releasing = MoveTemp(Rows);
	Rows.Reset();
	for (TPair<TObjectKey<UDreamWidget>, FRow>& Pair : Releasing)
	{
		if (bInUnbindRoutes)
		{
			TakeRoutesOff(Pair.Value);
		}
		Retire(MoveTemp(Pair.Value.Observer));
	}
}

void FDreamUIEntryRows::ReleaseDeadRows()
{
	for (auto It = Rows.CreateIterator(); It; ++It)
	{
		if (!It.Value().Widget.IsValid())
		{
			Retire(MoveTemp(It.Value().Observer));
			It.RemoveCurrent();
		}
	}
}

void FDreamUIEntryRows::StopWatching()
{
	for (TPair<TObjectKey<UDreamWidget>, FRow>& Pair : Rows)
	{
		if (Pair.Value.Observer.IsValid())
		{
			Pair.Value.Observer->Stop();
		}
	}
	for (const TUniquePtr<FDreamUIBindingObserver>& Retired : RetiredObservers)
	{
		Retired->Stop();
	}
}

UObject* FDreamUIEntryRows::GetItem(const UDreamWidget* InRow) const
{
	const FRow* Found = Rows.Find(TObjectKey<UDreamWidget>(InRow));
	return Found != nullptr ? Found->Item.Get() : nullptr;
}

int32 FDreamUIEntryRows::GetWatchedRowCount() const
{
	int32 Count = 0;
	for (const TPair<TObjectKey<UDreamWidget>, FRow>& Pair : Rows)
	{
		Count += Pair.Value.Observer.IsValid() && Pair.Value.Observer->IsStarted() ? 1 : 0;
	}
	return Count;
}

int32 FDreamUIEntryRows::GetSubscriptionCount() const
{
	int32 Count = 0;
	for (const TPair<TObjectKey<UDreamWidget>, FRow>& Pair : Rows)
	{
		Count += Pair.Value.Observer.IsValid() ? Pair.Value.Observer->GetSubscriptionCount() : 0;
	}
	return Count;
}

int32 FDreamUIEntryRows::GetRouteCount() const
{
	int32 Count = 0;
	for (const TPair<TObjectKey<UDreamWidget>, FRow>& Pair : Rows)
	{
		Count += Pair.Value.Routes.Num();
	}
	return Count;
}

void FDreamUIEntryRows::MapByDisplayName(TConstArrayView<UDreamWidget*> InWidgets, TMap<FName, UDreamWidget*>& OutWidgetsByDisplayName)
{
	OutWidgetsByDisplayName.Reset();
	OutWidgetsByDisplayName.Reserve(InWidgets.Num());
	for (UDreamWidget* Candidate : InWidgets)
	{
		if (IsValid(Candidate))
		{
			OutWidgetsByDisplayName.FindOrAdd(FName(*Candidate->GetDisplayName()), Candidate);
		}
	}
}

bool FDreamUIEntryRows::WriteEntry(const FDreamWidgetEntryBinding& InEntry, UObject* InTarget, UObject* InItem,
	TSet<UDreamUserWidget*>& OutWrittenUserWidgets)
{
	if (!IsValid(InTarget) || !IsValid(InItem))
	{
		return false;
	}
	FProperty* ItemProperty = InItem->GetClass()->FindPropertyByName(InEntry.ItemMember);
	if (ItemProperty == nullptr)
	{
		// An item of another class than the one the author had in mind: skip. The compiler cannot check an item's class
		// when the source is an array of UObject.
		return false;
	}
	const void* ItemValue = ItemProperty->ContainerPtrToValuePtr<void>(InItem);

	if (!InEntry.SetterName.IsNone())
	{
		// Through the setter, which is what makes the change take: SetText marks what SetText knows to mark. See
		// FDreamWidgetPropertyBinding.
		UFunction* Setter = InTarget->FindFunction(InEntry.SetterName);
		FProperty* SetterParameter = nullptr;
		if (Setter != nullptr)
		{
			for (TFieldIterator<FProperty> It(Setter); It && (It->PropertyFlags & CPF_Parm); ++It)
			{
				SetterParameter = *It;
				break;
			}
		}
		if (SetterParameter == nullptr)
		{
			return false;
		}
		FStructOnScope SetterFrame(Setter);
		// The shared conversion, as every bound value takes: an exact type copied whole, two numbers of different
		// widths converted, anything else refused rather than copied as bytes.
		if (!CopyDreamWidgetBoundValue(ItemProperty, ItemValue, SetterParameter,
			SetterParameter->ContainerPtrToValuePtr<void>(SetterFrame.GetStructMemory())))
		{
			return false;
		}
		InTarget->ProcessEvent(Setter, SetterFrame.GetStructMemory());
		return true;
	}

	// No setter: a property the builder allowed to be written directly, which it does only for a user widget's (a
	// component's `props` variable is a Blueprint variable, and those have none). What a setter would have done after
	// the write -- show the change -- is the written widget re-running its own bindings, done once per widget by the
	// caller (RerunWrittenUserWidgets) rather than once per property here.
	FProperty* TargetProperty = InTarget->GetClass()->FindPropertyByName(InEntry.PropertyName);
	if (TargetProperty == nullptr)
	{
		return false;
	}
	void* TargetValue = TargetProperty->ContainerPtrToValuePtr<void>(InTarget);
	if (TargetProperty->SameType(ItemProperty) && TargetProperty->Identical(TargetValue, ItemValue))
	{
		// Unchanged, which every refresh of an unchanged list is: no write, no broadcast, and no reason to run the
		// written widget's bindings again.
		return false;
	}
	if (!CopyDreamWidgetBoundValue(ItemProperty, ItemValue, TargetProperty, TargetValue))
	{
		return false;
	}
	if (UDreamUserWidget* WrittenWidget = Cast<UDreamUserWidget>(InTarget))
	{
		// What a Blueprint's own set node does for a FieldNotify variable, and what anything subscribed to the field --
		// a graph, a two-way binding, the written widget's own `for` over it -- is waiting for.
		const UE::FieldNotification::FFieldId FieldId =
			WrittenWidget->GetFieldNotificationDescriptor().GetField(WrittenWidget->GetClass(), InEntry.PropertyName);
		if (FieldId.IsValid())
		{
			WrittenWidget->BroadcastFieldValueChanged(FieldId);
		}
		OutWrittenUserWidgets.Add(WrittenWidget);
	}
	return true;
}

void FDreamUIEntryRows::RerunWrittenUserWidgets(const TSet<UDreamUserWidget*>& InWrittenUserWidgets)
{
	// A user widget written to directly heard nothing (unless the property is a FieldNotify field, which WriteEntry
	// broadcast), so its own `<-` bindings would show the old value until their next poll -- and its `for` and `each`
	// blocks, which may read the very property just written, would not move at all. Only an initialized one: anything
	// else is a duplicate still waiting for its tree, and what it holds in EachAdapters is its source's.
	for (UDreamUserWidget* WrittenWidget : InWrittenUserWidgets)
	{
		if (IsValid(WrittenWidget) && WrittenWidget->IsInitialized())
		{
			WrittenWidget->EvaluatePropertyBindings();
			WrittenWidget->RefreshEachBindings();
		}
	}
}

void FDreamUIEntryRows::PlaceRoutes(FRow& InOutRow, UObject* InItem, const FDreamWidgetEachBinding& InBinding,
	const TMap<FName, UDreamWidget*>& InWidgetsByDisplayName, bool bInScrubFirst)
{
	for (const FDreamWidgetEntryRoute& Route : InBinding.EntryRoutes)
	{
		UObject* Target = DreamUIEntryRowsLocal::ResolveRouteTarget(Route, InWidgetsByDisplayName);
		FProperty* EventProperty = IsValid(Target) ? Target->GetClass()->FindPropertyByName(Route.EventName) : nullptr;
		if (EventProperty == nullptr)
		{
			// A row missing the widget, or a widget of another class than the template's: skip, as an entry binding does.
			continue;
		}
		if (bInScrubFirst)
		{
			DreamUIEntryRowsLocal::ScrubCarriedRoutes(Target, EventProperty, Route.ItemFunction);
		}
		UFunction* Function = IsValid(InItem) ? InItem->FindFunction(Route.ItemFunction) : nullptr;
		if (Function == nullptr || !DreamUIEntryRowsLocal::PlaceRoute(Target, EventProperty, InItem, Function, Route.bCallWithoutArguments))
		{
			continue;
		}
		FBoundRoute& Bound = InOutRow.Routes.AddDefaulted_GetRef();
		Bound.Target = Target;
		Bound.EventName = Route.EventName;
		Bound.ItemFunction = Route.ItemFunction;
	}
}

void FDreamUIEntryRows::TakeRoutesOff(FRow& InOutRow)
{
	UObject* Item = InOutRow.Item.Get();
	for (const FBoundRoute& Bound : InOutRow.Routes)
	{
		// A target that is gone took its listeners with it.
		UObject* Target = Bound.Target.Get();
		FProperty* EventProperty = Target != nullptr ? Target->GetClass()->FindPropertyByName(Bound.EventName) : nullptr;
		if (EventProperty != nullptr)
		{
			DreamUIEntryRowsLocal::TakeRouteOff(Target, EventProperty, Item, Bound.ItemFunction);
		}
	}
	InOutRow.Routes.Reset();
}

void FDreamUIEntryRows::Retire(TUniquePtr<FDreamUIBindingObserver>&& InObserver)
{
	if (!InObserver.IsValid())
	{
		return;
	}
	InObserver->Stop();
	if (ReportDepth > 0)
	{
		// Possibly the watcher whose report is on the stack right now. Destroyed once the outermost report returns.
		RetiredObservers.Add(MoveTemp(InObserver));
		return;
	}
	InObserver.Reset();
}

bool FDreamUIEntryRows::ShouldWatch(const UClass* InItemClass, const FDreamWidgetEachBinding& InBinding)
{
	if (InItemClass == nullptr)
	{
		return false;
	}
	if (LastRuledClass.Get() == InItemClass)
	{
		return bLastRuledClassAnnounces;
	}
	// Asked of the class, through FindFieldId, as every subscribe-or-poll ruling is: an item of a class that announces
	// none of the members the entries read gets no watcher, and costs what a row cost before watching existed.
	bool bAnnounces = false;
	for (const FDreamWidgetEntryBinding& Entry : InBinding.EntryBindings)
	{
		if (DreamUIBindingPath::FindFieldId(InItemClass, Entry.ItemMember).IsValid())
		{
			bAnnounces = true;
			break;
		}
	}
	LastRuledClass = InItemClass;
	bLastRuledClassAnnounces = bAnnounces;
	return bAnnounces;
}

void FDreamUIEntryRows::HandleClientChanged(TWeakObjectPtr<UDreamWidget> InRow, int32 InClientId)
{
	UDreamWidget* RowWidget = InRow.Get();
	const FRow* Found = RowWidget != nullptr ? Rows.Find(TObjectKey<UDreamWidget>(RowWidget)) : nullptr;
	if (Found == nullptr || !Found->Item.IsValid())
	{
		// A row destroyed, or released, since the watcher was placed: ReleaseDeadRows drops it -- not from here, under
		// the watcher's own callback.
		return;
	}
	++ReportDepth;
	OnEntryChanged.ExecuteIfBound(RowWidget, InClientId);
	--ReportDepth;
	if (ReportDepth == 0)
	{
		RetiredObservers.Reset();
	}
}

// ============================================================================================ UDreamUIForAdapter

void UDreamUIForAdapter::Initialize(UDreamUserWidget* InOwner, const FDreamWidgetEachBinding& InBinding, UDreamWidget* InHost, UDreamWidget* InTemplate)
{
	// A second Initialize is a new `for`, not an addition to the old one.
	ReleaseCopies();
	Owner = InOwner;
	Binding = InBinding;
	Host = InHost;
	Template = InTemplate;
	bCopiesKnownReady = false;
	DeferredRefreshCount = 0;
	Rows.Initialize(this, FDreamUIEntryRows::FOnEntryChanged::CreateUObject(this, &UDreamUIForAdapter::HandleEntryChanged));

	UDreamWidget* Panel = IsValid(InTemplate) ? InTemplate->GetParent() : nullptr;
	if (!IsValid(InOwner) || !IsValid(Panel))
	{
		return;
	}

	// The class's archetype rather than the live template, which this adapter is about to collapse and which a
	// duplicated owner hands over collapsed already. A template no class archetype has -- a test assembling one by
	// hand -- answers for itself.
	EDreamWidgetVisibility Authored = InTemplate->GetVisibility();
	if (const UDreamWidgetTree* Archetype = UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(InOwner->GetClass()))
	{
		if (const UDreamWidget* AuthoredTemplate = Archetype->FindWidgetByVariableName(InBinding.TemplateWidgetName))
		{
			Authored = AuthoredTemplate->GetVisibility();
		}
	}
	ShownVisibility = Authored == EDreamWidgetVisibility::Collapsed ? EDreamWidgetVisibility::Visible : Authored;

	AdoptCopiesLeftByDuplication(Panel, InTemplate);
	Refresh();
}

void UDreamUIForAdapter::Refresh()
{
	// A refresh calls out -- the source function, every setter, every copy's own bindings -- and any of those may ask
	// for another one: a source that broadcasts its own field, a graph that calls RefreshEachBindings. Running it
	// there, halfway through this one's bookkeeping, would reconcile against a list of copies that is half old and
	// half new. Asked for during a refresh, it runs again once this one is done.
	if (bRefreshing)
	{
		bRefreshRequestedWhileRefreshing = true;
		return;
	}
	TGuardValue<bool> RefreshingGuard(bRefreshing, true);
	// Bounded: a source that broadcasts every time it is read would otherwise hold the game thread here for ever.
	// Four passes is three more than anything well-behaved needs.
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		bRefreshRequestedWhileRefreshing = false;
		RefreshOnce();
		if (!bRefreshRequestedWhileRefreshing)
		{
			break;
		}
	}
}

void UDreamUIForAdapter::RefreshOnce()
{
	using namespace DreamUIForAdapterLocal;

	UDreamUserWidget* OwningWidget = Owner.Get();
	UDreamWidget* TemplateWidget = Template.Get();
	UDreamWidget* Panel = IsValid(TemplateWidget) ? TemplateWidget->GetParent() : nullptr;
	if (!IsValid(OwningWidget) || !IsValid(Panel))
	{
		// The template is gone or out of the tree, and copies of a template that is not there are copies of nothing:
		// left standing they would be rows no refresh can reach any more. The owner going down takes them with it
		// anyway (they are in its tree); this is for a graph that removed the template itself.
		ReleaseCopies();
		return;
	}

	if (!bCopiesKnownReady)
	{
		bool bReady = IsTemplateReadyToCopy(TemplateWidget);
		for (const TObjectPtr<UDreamWidget>& Existing : Copies)
		{
			bReady = bReady && (!IsValid(Existing) || IsTemplateReadyToCopy(Existing));
		}
		if (!bReady && DeferredRefreshCount < MaxDeferredRefreshes)
		{
			// Nothing is touched meanwhile. The copies a duplicated owner brought along show what the source's
			// showed, which for the same items is already right, so the frame in between draws the right rows.
			++DeferredRefreshCount;
			ScheduleDeferredRefresh();
			return;
		}
		// Once ready, ready for good: the template's user widgets are not un-initialized again, and every copy made
		// from here on comes back from DuplicateWidget already adopted.
		bCopiesKnownReady = true;
	}

	// Every time, not once: the template is a widget in the tree like any other, and a graph or a binding that
	// shows it again would put the authored copy on screen beside its own copies.
	if (TemplateWidget->GetVisibility() != EDreamWidgetVisibility::Collapsed)
	{
		TemplateWidget->SetVisibility(EDreamWidgetVisibility::Collapsed);
	}

	TArray<UObject*> NewItems;
	FetchItems(NewItems);

	bool bSameList = NewItems.Num() == Items.Num() && Copies.Num() == Items.Num();
	for (int32 Index = 0; bSameList && Index < NewItems.Num(); ++Index)
	{
		const UDreamWidget* Existing = Copies[Index].Get();
		bSameList = Items[Index] == NewItems[Index] && IsValid(Existing) && Existing->GetParent() == Panel;
	}

	if (!bSameList)
	{
		// Every copy still standing, filed under the item it shows. An array per item rather than one copy, because
		// a source may list the same object twice and each listing is a row of its own.
		TMap<UObject*, TArray<UDreamWidget*>> Pool;
		for (int32 Index = 0; Index < Copies.Num(); ++Index)
		{
			UDreamWidget* Existing = Copies[Index].Get();
			if (!IsValid(Existing))
			{
				continue;
			}
			if (Existing->GetParent() != Panel)
			{
				// Moved away by something else. Still this adapter's to remove: it is a copy of the template, and the
				// next refresh would otherwise make a second one for the same item.
				Rows.ReleaseRow(Existing, /*bInUnbindRoutes*/true);
				Existing->DestroyWidget();
				continue;
			}
			Pool.FindOrAdd(Items.IsValidIndex(Index) ? Items[Index].Get() : nullptr).Add(Existing);
		}

		TArray<UDreamWidget*> Ordered;
		Ordered.SetNumZeroed(NewItems.Num());
		for (int32 Index = 0; Index < NewItems.Num(); ++Index)
		{
			TArray<UDreamWidget*>* Candidates = Pool.Find(NewItems[Index]);
			if (Candidates != nullptr && Candidates->Num() > 0)
			{
				Ordered[Index] = (*Candidates)[0];
				Candidates->RemoveAt(0);
			}
		}
		// What no item claimed goes before anything is made, so a panel with a child limit counts the survivors only.
		for (TPair<UObject*, TArray<UDreamWidget*>>& Unclaimed : Pool)
		{
			for (UDreamWidget* Leaving : Unclaimed.Value)
			{
				// Its item's watcher stopped and its routes off first: the item may well outlive the row.
				Rows.ReleaseRow(Leaving, /*bInUnbindRoutes*/true);
				Leaving->DestroyWidget();
			}
		}
		for (int32 Index = 0; Index < NewItems.Num(); ++Index)
		{
			if (Ordered[Index] == nullptr)
			{
				Ordered[Index] = MakeCopy(Panel, TemplateWidget);
			}
		}
		PlaceCopies(Panel, TemplateWidget, Ordered);

		Items.Reset(NewItems.Num());
		Copies.Reset(NewItems.Num());
		for (int32 Index = 0; Index < NewItems.Num(); ++Index)
		{
			Items.Add(NewItems[Index]);
			Copies.Add(Ordered[Index]);
		}
	}

	// A copy something else destroyed since the last refresh (a graph, the template going) is no longer in Copies; its
	// row goes now.
	Rows.ReleaseDeadRows();
	if (!FDreamUIEntryRows::HasRowWork(Binding))
	{
		// Nothing written per copy and nothing routed: no copy needs walking.
		return;
	}

	// Every copy, kept ones included: the same object can hold different values than it did at the last refresh, and
	// the broadcast that brought this refresh about is usually exactly that. A member the item announces is written
	// again between refreshes too, by the copy's own watcher (HandleEntryChanged); one it does not announce only here.
	TSet<UDreamUserWidget*> WrittenUserWidgets;
	TMap<FName, UDreamWidget*> CopyWidgets;
	for (int32 Index = 0; Index < Copies.Num(); ++Index)
	{
		UDreamWidget* Copy = Copies[Index].Get();
		if (!IsValid(Copy))
		{
			continue;
		}
		UObject* Item = Items[Index].Get();
		MapCopyWidgets(Copy, CopyWidgets);
		// The watcher and the routes follow the item before any value is written: nothing once the copy already shows it.
		Rows.AimRow(Copy, Item, Binding, CopyWidgets);
		ApplyEntryBindings(Item, CopyWidgets, WrittenUserWidgets);
	}
	// Run here, once per widget however many of its properties were written.
	FDreamUIEntryRows::RerunWrittenUserWidgets(WrittenUserWidgets);
}

void UDreamUIForAdapter::ReleaseCopies()
{
	if (DeferredRefreshHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredRefreshHandle);
		DeferredRefreshHandle.Reset();
	}
	// Taken out first: destroying a widget can run code (a behaviour's OnDestroy, a graph's Destruct) that asks this
	// adapter for a refresh, which must find an adapter with nothing in it rather than a list being torn down.
	TArray<TObjectPtr<UDreamWidget>> Releasing = MoveTemp(Copies);
	Copies.Reset();
	Items.Reset();
	// Every item's watcher stopped and every route taken off before the copies go: the items usually outlive them, and
	// a delegate left on one would be one more for every list that ever showed it.
	Rows.ReleaseAll(/*bInUnbindRoutes*/true);
	for (const TObjectPtr<UDreamWidget>& Released : Releasing)
	{
		if (IsValid(Released))
		{
			Released->DestroyWidget();
		}
	}
}

UDreamUserWidget* UDreamUIForAdapter::GetOwningWidget() const
{
	return Owner.Get();
}

TArray<UDreamWidget*> UDreamUIForAdapter::GetCopies() const
{
	TArray<UDreamWidget*> Result;
	Result.Reserve(Copies.Num());
	for (const TObjectPtr<UDreamWidget>& Made : Copies)
	{
		if (IsValid(Made))
		{
			Result.Add(Made.Get());
		}
	}
	return Result;
}

void UDreamUIForAdapter::BeginDestroy()
{
	// The copies are not destroyed here: this runs inside a garbage collection, and they are the owner's widgets,
	// which go down with the owner. Only the ticker, which would otherwise call into an object being collected, and the
	// items' watchers, whose delegates sit on items that may well live on. Routes stay where they are: they are on the
	// copies' widgets, which go down with the owner too.
	if (DeferredRefreshHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredRefreshHandle);
		DeferredRefreshHandle.Reset();
	}
	Rows.StopWatching();
	Super::BeginDestroy();
}

void UDreamUIForAdapter::FetchItems(TArray<UObject*>& OutItems) const
{
	OutItems.Reset();
	UDreamUserWidget* OwningWidget = Owner.Get();
	if (!IsValid(OwningWidget))
	{
		return;
	}
	// `in Player.Items`: the object the path's leading members reach right now, and none when the chain is broken
	// (Player not set yet) -- no items, as a source that is empty has none. The owning widget refreshes this adapter
	// when anything along the path changes. A one-segment source reads the widget itself, as it always did.
	UObject* SourceOwner = Binding.SourcePath.Num() > 0
		? DreamUIBindingPath::ResolveOwner(OwningWidget, Binding.SourcePath)
		: OwningWidget;
	// The compiler vetted the shape (DUI6006, DUI6007); a miss anywhere in there means the class moved underneath us,
	// which is the property bindings' rule too: no items, never a guess.
	DreamUIBindingPath::ReadObjectArray(SourceOwner, Binding.SourceName, Binding.bSourceIsFunction, OutItems);
}

void UDreamUIForAdapter::AdoptCopiesLeftByDuplication(UDreamWidget* InPanel, UDreamWidget* InTemplate)
{
	// Told apart by name: every copy carries the template's display name (the item bindings find their widgets by
	// display name, the copy's root among them), and node ids are unique in a file, so a sibling of the template that
	// shares its name can only be a copy of it. Nothing marks a copy otherwise -- a flag on the widget would be one
	// more thing duplication has to carry right.
	const FString TemplateName = InTemplate->GetDisplayName();
	TArray<UDreamWidget*> Left;
	for (UDreamWidget* Child : InPanel->GetChildren())
	{
		if (IsValid(Child) && Child != InTemplate && Child->GetDisplayName().Equals(TemplateName, ESearchCase::CaseSensitive))
		{
			Left.Add(Child);
		}
	}
	if (Left.Num() == 0)
	{
		return;
	}
	// Filed under the items the source lists NOW, by position, which is what the source's copies showed when they
	// were made: same object, same row. Where the two disagree the first refresh sorts it out -- a copy filed under
	// the wrong item only means a different copy is kept, and every copy is written again anyway.
	TArray<UObject*> Fetched;
	FetchItems(Fetched);
	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		Copies.Add(Left[Index]);
		Items.Add(Fetched.IsValidIndex(Index) ? Fetched[Index] : nullptr);
	}
}

bool UDreamUIForAdapter::IsTemplateReadyToCopy(const UDreamWidget* InTemplate) const
{
	TArray<UDreamWidget*> Widgets;
	UDreamWidget::CollectChildrenWidgets(const_cast<UDreamWidget*>(InTemplate), Widgets, /*IncludeTarget*/true);
	return !Widgets.ContainsByPredicate([](const UDreamWidget* Candidate)
	{
		return DreamUIForAdapterLocal::IsUnadoptedDuplicate(Candidate);
	});
}

UDreamWidget* UDreamUIForAdapter::MakeCopy(UDreamWidget* InPanel, UDreamWidget* InTemplate) const
{
	// The builder refuses a `for` in a widget with a child limit; a template a component routed into one of its slots
	// at run time can still land in one. Every copy past the limit would be attached anyway -- duplication attaches
	// without asking -- and a panel holding more than it can lay out draws them on top of each other.
	if (!InPanel->CanAcceptAdditionalChildren(1))
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d '%s' has no room for another copy of '%s' (a 'for' in %s); the rest of the items are not shown."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InPanel->GetDisplayName(), *InTemplate->GetDisplayName(),
			IsValid(Owner.Get()) ? *Owner->GetClass()->GetName() : TEXT("nothing"));
		return nullptr;
	}
	// DuplicateWidget is the whole of making a widget at run time here: the deep copy, a nested user widget handed
	// its own tree and initialized (so ITS bindings, `for` and `each` blocks resolve), the parent link, and
	// registration -- which is where every selectable inside joins navigation, as an authored one does.
	UDreamWidget* Made = UDreamUIBPLibrary::DuplicateWidget(Owner.Get(), InTemplate, InPanel);
	if (!IsValid(Made))
	{
		return nullptr;
	}
	// Copied from a collapsed template, so collapsed until told otherwise.
	Made->SetVisibility(ShownVisibility);
	return Made;
}

void UDreamUIForAdapter::PlaceCopies(UDreamWidget* InPanel, UDreamWidget* InTemplate, TConstArrayView<UDreamWidget*> InCopies)
{
	using namespace DreamUIForAdapterLocal;

	// SetSiblingIndex does nothing when the index it is given is the one the widget already HOLDS, and after a child
	// is detached nothing renumbers the rest -- so a panel that has just lost copies holds indices with gaps in them,
	// and an index can match while the position does not. Renumbered to the positions first, quietly (no reorder,
	// no notification: the order is unchanged), so that from here an index and a position are the same thing.
	{
		const TArray<UDreamWidget*> Live = LiveChildrenOf(InPanel);
		for (int32 Index = 0; Index < Live.Num(); ++Index)
		{
			if (Live[Index]->GetSiblingIndex() != Index)
			{
				Live[Index]->RestoreSiblingIndex(Index);
			}
		}
	}

	int32 Placed = 0;
	for (UDreamWidget* Placing : InCopies)
	{
		if (!IsValid(Placing) || Placing->GetParent() != InPanel)
		{
			continue;
		}
		const TArray<UDreamWidget*> Live = LiveChildrenOf(InPanel);
		const int32 TemplateIndex = Live.IndexOfByKey(InTemplate);
		const int32 CopyIndex = Live.IndexOfByKey(Placing);
		if (TemplateIndex == INDEX_NONE || CopyIndex == INDEX_NONE)
		{
			return;
		}
		// The index after the move, which SetSiblingIndex counts with the copy already taken out: a copy that somehow
		// stood BEFORE the template moves the template down one on its way past.
		const int32 Wanted = TemplateIndex + 1 + Placed - (CopyIndex < TemplateIndex ? 1 : 0);
		// A no-op for a copy already standing there, which a list that only grew at the end is made of.
		Placing->SetSiblingIndex(Wanted);
		++Placed;
	}
}

void UDreamUIForAdapter::MapCopyWidgets(UDreamWidget* InCopy, TMap<FName, UDreamWidget*>& OutWidgetsByDisplayName)
{
	TArray<UDreamWidget*> CopyWidgets;
	DreamUIForAdapterLocal::CollectAuthoredWidgets(InCopy, CopyWidgets);
	// One pass over the copy rather than one per binding, first match wins -- the `each` adapter's rule, for the same
	// reason.
	FDreamUIEntryRows::MapByDisplayName(CopyWidgets, OutWidgetsByDisplayName);
}

void UDreamUIForAdapter::ApplyEntryBindings(UObject* InItem, const TMap<FName, UDreamWidget*>& InCopyWidgets, TSet<UDreamUserWidget*>& OutWrittenUserWidgets)
{
	if (!IsValid(InItem))
	{
		return;
	}
	for (int32 EntryIndex = 0; EntryIndex < Binding.EntryBindings.Num(); ++EntryIndex)
	{
		ApplyEntryBinding(EntryIndex, InItem, InCopyWidgets, OutWrittenUserWidgets);
	}
}

void UDreamUIForAdapter::ApplyEntryBinding(int32 InEntryIndex, UObject* InItem, const TMap<FName, UDreamWidget*>& InCopyWidgets, TSet<UDreamUserWidget*>& OutWrittenUserWidgets)
{
	const FDreamWidgetEntryBinding& Entry = Binding.EntryBindings[InEntryIndex];
	UDreamWidget* const* FoundWidget = InCopyWidgets.Find(Entry.TargetWidgetDisplayName);
	UObject* Target = ResolveDreamWidgetBindingTarget(FoundWidget != nullptr ? *FoundWidget : nullptr, Entry.Target, Entry.BehaviourIndex);
	if (!IsValid(Target))
	{
		// A copy missing the widget: skip, as an `each` cell does.
		return;
	}
	++EntryWriteCount;
	FDreamUIEntryRows::WriteEntry(Entry, Target, InItem, OutWrittenUserWidgets);
}

void UDreamUIForAdapter::HandleEntryChanged(UDreamWidget* InCopy, int32 InEntryIndex)
{
	UObject* Item = Rows.GetItem(InCopy);
	if (!IsValid(InCopy) || !IsValid(Item) || !Binding.EntryBindings.IsValidIndex(InEntryIndex))
	{
		return;
	}
	// The one entry, on the one copy: no refresh, no other copy, no other member of this one. A refresh in progress
	// needs no special care -- this writes a value onto a copy that exists, which is all the refresh's last step does.
	TMap<FName, UDreamWidget*> CopyWidgets;
	MapCopyWidgets(InCopy, CopyWidgets);
	TSet<UDreamUserWidget*> WrittenUserWidgets;
	ApplyEntryBinding(InEntryIndex, Item, CopyWidgets, WrittenUserWidgets);
	FDreamUIEntryRows::RerunWrittenUserWidgets(WrittenUserWidgets);
}

void UDreamUIForAdapter::ScheduleDeferredRefresh()
{
	if (!DeferredRefreshHandle.IsValid())
	{
		// A UObject delegate, so an adapter collected before the frame comes is simply not called.
		DeferredRefreshHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UDreamUIForAdapter::HandleDeferredRefresh));
	}
}

bool UDreamUIForAdapter::HandleDeferredRefresh(float /*InDeltaTime*/)
{
	DeferredRefreshHandle.Reset();
	Refresh();
	// One shot.
	return false;
}
