// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIForAdapter.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "DreamGUI.h"
#include "DreamUIBPLibrary.h"
#include "INotifyFieldValueChanged.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/Class.h"
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

	// Every copy, kept ones included: the same object can hold different values than it did at the last refresh, and
	// the broadcast that brought this refresh about is usually exactly that.
	TSet<UDreamUserWidget*> WrittenUserWidgets;
	for (int32 Index = 0; Index < Copies.Num(); ++Index)
	{
		ApplyEntryBindings(Copies[Index].Get(), Items[Index].Get(), WrittenUserWidgets);
	}
	// A user widget written to directly heard nothing (unless the property is a FieldNotify field, which
	// ApplyEntryBindings broadcast), so its own `<-` bindings would show the old value until their next poll -- and
	// its `for` and `each` blocks, which may read the very property just written, would not move at all. Both are
	// run here, once per widget however many of its properties were written. Only an initialized one: anything
	// else is a duplicate still waiting for its tree, and what it holds in EachAdapters is its source's.
	for (UDreamUserWidget* WrittenWidget : WrittenUserWidgets)
	{
		if (IsValid(WrittenWidget) && WrittenWidget->IsInitialized())
		{
			WrittenWidget->EvaluatePropertyBindings();
			WrittenWidget->RefreshEachBindings();
		}
	}
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
	// which go down with the owner. Only the ticker, which would otherwise call into an object being collected.
	if (DeferredRefreshHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DeferredRefreshHandle);
		DeferredRefreshHandle.Reset();
	}
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

	// The compiler vetted the shape (DUI6006, DUI6007); a miss anywhere below means the class moved underneath us,
	// which is the property bindings' rule too: no items, never a guess.
	auto CopyOut = [&OutItems](const FArrayProperty* InItemsProperty, const void* InItemsMemory)
	{
		const FObjectPropertyBase* Inner = InItemsProperty != nullptr ? CastField<FObjectPropertyBase>(InItemsProperty->Inner) : nullptr;
		if (Inner == nullptr || InItemsMemory == nullptr)
		{
			return;
		}
		FScriptArrayHelper Helper(InItemsProperty, InItemsMemory);
		OutItems.Reserve(Helper.Num());
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			OutItems.Add(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
		}
	};

	if (Binding.bSourceIsFunction)
	{
		UFunction* Source = OwningWidget->FindFunction(Binding.SourceName);
		if (Source == nullptr)
		{
			return;
		}
		// FStructOnScope rather than a raw buffer: the returned array has to be constructed before the call writes it
		// and destroyed after it has been read.
		FStructOnScope SourceFrame(Source);
		OwningWidget->ProcessEvent(Source, SourceFrame.GetStructMemory());
		const FArrayProperty* ItemsProperty = CastField<FArrayProperty>(Source->GetReturnProperty());
		CopyOut(ItemsProperty, ItemsProperty != nullptr
			? ItemsProperty->ContainerPtrToValuePtr<void>(SourceFrame.GetStructMemory()) : nullptr);
	}
	else
	{
		const FArrayProperty* ItemsProperty = FindFProperty<FArrayProperty>(OwningWidget->GetClass(), Binding.SourceName);
		CopyOut(ItemsProperty, ItemsProperty != nullptr
			? ItemsProperty->ContainerPtrToValuePtr<void>(OwningWidget) : nullptr);
	}
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

void UDreamUIForAdapter::ApplyEntryBindings(UDreamWidget* InCopy, UObject* InItem, TSet<UDreamUserWidget*>& OutWrittenUserWidgets) const
{
	if (!IsValid(InCopy) || !IsValid(InItem) || Binding.EntryBindings.Num() == 0)
	{
		return;
	}

	TArray<UDreamWidget*> CopyWidgets;
	DreamUIForAdapterLocal::CollectAuthoredWidgets(InCopy, CopyWidgets);
	// One pass over the copy rather than one per binding, first match wins -- the `each` adapter's rule, for the
	// same reason.
	TMap<FName, UDreamWidget*> WidgetsByDisplayName;
	WidgetsByDisplayName.Reserve(CopyWidgets.Num());
	for (UDreamWidget* Candidate : CopyWidgets)
	{
		WidgetsByDisplayName.FindOrAdd(FName(*Candidate->GetDisplayName()), Candidate);
	}

	UClass* ItemClass = InItem->GetClass();
	for (const FDreamWidgetEntryBinding& Entry : Binding.EntryBindings)
	{
		UDreamWidget* const* FoundWidget = WidgetsByDisplayName.Find(Entry.TargetWidgetDisplayName);
		UObject* Target = ResolveDreamWidgetBindingTarget(FoundWidget != nullptr ? *FoundWidget : nullptr, Entry.Target, Entry.BehaviourIndex);
		FProperty* ItemProperty = ItemClass->FindPropertyByName(Entry.ItemMember);
		if (!IsValid(Target) || ItemProperty == nullptr)
		{
			// An item of another class than the one the author had in mind, or a copy missing the widget: skip, as an
			// `each` cell does. The compiler cannot check an item's class -- the source is an array of UObject.
			continue;
		}
		const void* ItemValue = ItemProperty->ContainerPtrToValuePtr<void>(InItem);

		if (!Entry.SetterName.IsNone())
		{
			// Through the setter, which is what makes the change take: SetText marks what SetText knows to mark. See
			// FDreamWidgetPropertyBinding.
			UFunction* Setter = Target->FindFunction(Entry.SetterName);
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
				continue;
			}
			FStructOnScope SetterFrame(Setter);
			// The shared conversion, as every bound value takes: an exact type copied whole, two numbers of different
			// widths converted, anything else refused rather than copied as bytes.
			if (!CopyDreamWidgetBoundValue(ItemProperty, ItemValue, SetterParameter,
				SetterParameter->ContainerPtrToValuePtr<void>(SetterFrame.GetStructMemory())))
			{
				continue;
			}
			Target->ProcessEvent(Setter, SetterFrame.GetStructMemory());
			continue;
		}

		// No setter: a property the builder allowed to be written directly, which it does only for a user widget's
		// (a component's `props` variable is a Blueprint variable, and those have none). What a setter would have
		// done after the write -- show the change -- is the copy re-running its own bindings, done once per widget
		// by RefreshOnce rather than once per property here.
		FProperty* TargetProperty = Target->GetClass()->FindPropertyByName(Entry.PropertyName);
		if (TargetProperty == nullptr)
		{
			continue;
		}
		void* TargetValue = TargetProperty->ContainerPtrToValuePtr<void>(Target);
		if (TargetProperty->SameType(ItemProperty) && TargetProperty->Identical(TargetValue, ItemValue))
		{
			// Unchanged, which every refresh of an unchanged list is: no write, no broadcast, and no reason to run the
			// copy's bindings again.
			continue;
		}
		if (!CopyDreamWidgetBoundValue(ItemProperty, ItemValue, TargetProperty, TargetValue))
		{
			continue;
		}
		if (UDreamUserWidget* WrittenWidget = Cast<UDreamUserWidget>(Target))
		{
			// What a Blueprint's own set node does for a FieldNotify variable, and what anything subscribed to the
			// field -- a graph, a two-way binding, the copy's own `for` over it -- is waiting for.
			const UE::FieldNotification::FFieldId FieldId =
				WrittenWidget->GetFieldNotificationDescriptor().GetField(WrittenWidget->GetClass(), Entry.PropertyName);
			if (FieldId.IsValid())
			{
				WrittenWidget->BroadcastFieldValueChanged(FieldId);
			}
			OutWrittenUserWidgets.Add(WrittenWidget);
		}
	}
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
