// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Binding/DreamUIEachAdapter.h"

#include "Core/DreamUIBindingObserver.h"
#include "Core/DreamUIEachBindingHandler.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/Components/DreamWidget.h"
#include "DreamGUI.h"
#include "Interaction/UIListView.h"
#include "UObject/UnrealType.h"

void UDreamUIEachAdapter::Initialize(UDreamUserWidget* InOwner, const FDreamWidgetEachBinding& InBinding, UUIRecyclableScrollView* InView)
{
	Owner = InOwner;
	Binding = InBinding;
	View = InView;
	// A second Initialize is a new `each`: the old cells' watchers stop. Their routes stay -- the cells are about to be
	// set again, and a cell seen for the first time is scrubbed of what it carries before its own routes go on.
	Rows.Initialize(this, FDreamUIEntryRows::FOnEntryChanged::CreateUObject(this, &UDreamUIEachAdapter::HandleEntryChanged));
	FetchItems();
}

void UDreamUIEachAdapter::FetchItems()
{
	Items.Reset();
	if (!IsValid(Owner))
	{
		return;
	}

	// `in Inventory.Items`: read on the object the path's leading members reach right now, none when the chain is
	// broken. A one-segment source reads the user widget itself, as it always did. The compiler vetted the shape; a
	// miss anywhere in there means the class moved underneath us, which is the property bindings' rule too: skip,
	// never guess.
	UObject* SourceOwner = Binding.SourcePath.Num() > 0
		? DreamUIBindingPath::ResolveOwner(Owner.Get(), Binding.SourcePath)
		: Owner.Get();
	TArray<UObject*> Fetched;
	DreamUIBindingPath::ReadObjectArray(SourceOwner, Binding.SourceName, Binding.bSourceIsFunction, Fetched);
	Items.Reserve(Fetched.Num());
	for (UObject* Item : Fetched)
	{
		Items.Add(Item);
	}
}

void UDreamUIEachAdapter::Refresh()
{
	// The list AS IT WAS, not merely how long it was. A refresh is raised by any broadcast on the
	// source field, and most of those change nothing about the rows -- an array property written with
	// the same contents, a FieldNotify fired defensively, a second `each` sharing one source. Each of
	// those used to re-bind every visible cell, which means one reflected setter call per bound member
	// per visible row, every time.
	TArray<TWeakObjectPtr<UObject>> PreviousItems;
	PreviousItems.Reserve(Items.Num());
	for (const TObjectPtr<UObject>& Item : Items)
	{
		PreviousItems.Emplace(Item.Get());
	}

	FetchItems();
	// Cells the view destroyed since the last pass (a shorter list, a cleared view) leave their rows here.
	Rows.ReleaseDeadRows();
	if (!IsValid(View))
	{
		return;
	}

	if (Items.Num() == PreviousItems.Num())
	{
		bool bSameItems = true;
		for (int32 Index = 0; Index < Items.Num(); ++Index)
		{
			if (PreviousItems[Index].Get() != Items[Index].Get())
			{
				bSameItems = false;
				break;
			}
		}
		if (bSameItems)
		{
			// Same objects in the same order. A cell's CONTENTS can still have changed underneath the
			// object -- and a member the item announces has already been written onto its cell by the
			// cell's own watcher, so re-binding here on a broadcast that moved nothing is still the part
			// that was free to stop doing. A member nobody announces is the one case left for whoever
			// changed it, who calls UpdateCellData.
			return;
		}
		// Same length, different objects: a reorder or a wholesale replacement. The pool is right and
		// only the data behind each cell changed.
		View->UpdateCellData();
		return;
	}

	// The count changed, so the content area has to be re-sized and the cell pool re-checked against
	// the new count. Note this is NOT proportional to the number of rows: UUIRecyclableScrollView
	// sizes its pool from VisibleCellCount -- how many cells fit in the viewport, capped at the item
	// count -- and grows it only when that number rises, so a list of ten thousand rows rebuilds the
	// same handful of cells a list of twenty does.
	View->RecreateList();
}

void UDreamUIEachAdapter::BeforeSetCell_Implementation()
{
	// Once per pass of the view over its cells, before any of them is set: the rows of cells it destroyed go.
	Rows.ReleaseDeadRows();
}

void UDreamUIEachAdapter::SetCell_Implementation(UDreamUIBehaviour* Component, int32 Index)
{
	UDreamWidget* CellRoot = IsValid(Component) ? Component->GetWidget() : nullptr;
	if (!IsValid(CellRoot))
	{
		return;
	}
	UObject* Item = Items.IsValidIndex(Index) ? Items[Index].Get() : nullptr;
	const bool bHasRowWork = FDreamUIEntryRows::HasRowWork(Binding);
	if (!IsValid(Item) && !bHasRowWork)
	{
		return;
	}

	// One pass over the cell instead of one per entry binding. This runs for every visible cell on
	// every scroll update, and a cell with N widgets and M bindings was paying N*M display-name
	// comparisons for an answer that does not change within the call. First match wins, exactly as
	// the linear search that used to break on it did -- display names are not unique in a subtree.
	TMap<FName, UDreamWidget*> WidgetsByDisplayName;
	MapCellWidgets(CellRoot, WidgetsByDisplayName);

	// The cell follows its item before any value is written: a recycled cell's routes come off the item it showed and
	// go onto this one, and its watcher is re-aimed. A cell given no item keeps its values, as it always did, but
	// calls nobody and hears nobody.
	Rows.AimRow(CellRoot, Item, Binding, WidgetsByDisplayName);
	if (!IsValid(Item))
	{
		return;
	}

	TSet<UDreamUserWidget*> WrittenUserWidgets;
	for (int32 EntryIndex = 0; EntryIndex < Binding.EntryBindings.Num(); ++EntryIndex)
	{
		ApplyEntryBinding(EntryIndex, Item, WidgetsByDisplayName, WrittenUserWidgets);
	}
	FDreamUIEntryRows::RerunWrittenUserWidgets(WrittenUserWidgets);
}

void UDreamUIEachAdapter::BeginDestroy()
{
	// Inside a collection: the items' watchers only, whose delegates sit on items that may well live on. The routes are
	// on the cells' widgets, which go down with the view.
	Rows.StopWatching();
	Super::BeginDestroy();
}

void UDreamUIEachAdapter::MapCellWidgets(UDreamWidget* InCellRoot, TMap<FName, UDreamWidget*>& OutWidgetsByDisplayName)
{
	TArray<UDreamWidget*> CellWidgets;
	UDreamWidget::CollectChildrenWidgets(InCellRoot, CellWidgets, /*IncludeTarget*/true);
	FDreamUIEntryRows::MapByDisplayName(CellWidgets, OutWidgetsByDisplayName);
}

void UDreamUIEachAdapter::ApplyEntryBinding(int32 InEntryIndex, UObject* InItem, const TMap<FName, UDreamWidget*>& InCellWidgets, TSet<UDreamUserWidget*>& OutWrittenUserWidgets)
{
	const FDreamWidgetEntryBinding& Entry = Binding.EntryBindings[InEntryIndex];
	UDreamWidget* const* FoundWidget = InCellWidgets.Find(Entry.TargetWidgetDisplayName);
	UObject* Target = ResolveDreamWidgetBindingTarget(FoundWidget != nullptr ? *FoundWidget : nullptr, Entry.Target, Entry.BehaviourIndex);
	if (!IsValid(Target))
	{
		return;
	}
	++EntryWriteCount;
	// The rule a `for` copy writes by, so the two kinds of loop cannot disagree about a value: an exact type copied
	// whole, two numbers of different widths converted (this used to refuse anything but the exact type), anything
	// else refused. An `each` always names a setter -- the builder refuses one without -- so the direct write that
	// rule also knows is never reached from here.
	FDreamUIEntryRows::WriteEntry(Entry, Target, InItem, OutWrittenUserWidgets);
}

void UDreamUIEachAdapter::HandleEntryChanged(UDreamWidget* InCell, int32 InEntryIndex)
{
	if (!IsFeedingView())
	{
		// The owner re-resolved and another adapter feeds the view now; the cells are its, item and all. This one only
		// stops listening -- taking routes off would take the new adapter's too, when a cell kept its item.
		Rows.ReleaseAll(/*bInUnbindRoutes*/false);
		return;
	}
	UObject* Item = Rows.GetItem(InCell);
	if (!IsValid(InCell) || !IsValid(Item) || !Binding.EntryBindings.IsValidIndex(InEntryIndex))
	{
		return;
	}
	TMap<FName, UDreamWidget*> CellWidgets;
	MapCellWidgets(InCell, CellWidgets);
	TSet<UDreamUserWidget*> WrittenUserWidgets;
	ApplyEntryBinding(InEntryIndex, Item, CellWidgets, WrittenUserWidgets);
	FDreamUIEntryRows::RerunWrittenUserWidgets(WrittenUserWidgets);
}

bool UDreamUIEachAdapter::IsFeedingView() const
{
	// No view at all is a test driving SetCell itself, or a view already gone with its cells: nothing to defer to.
	return !IsValid(View) || View->GetDataSource().GetObject() == this;
}

namespace DreamUIEachAdapterLocal
{
	/** The list views' side of an `each`, which the core reaches through IDreamUIEachBindingHandler. */
	class FEachBindingHandler final : public IDreamUIEachBindingHandler
	{
	public:
		virtual bool HasListView(const UDreamWidget* InHost) const override
		{
			return IsValid(InHost) && InHost->GetComponent<UUIRecyclableScrollView>() != nullptr;
		}

		virtual bool PrepareHost(UDreamWidget* InHost, UDreamWidget* InTemplate) const override
		{
			// The recyclable view clones whatever carries the cell-marker interface; a template the
			// author did not mark gets the plain list entry, which is the marker plus click plumbing.
			if (InTemplate->GetComponentByInterface(UUIRecyclableScrollViewCell::StaticClass()) == nullptr)
			{
				InTemplate->AddComponent<UUIListEntry>();
			}
			// InitializeOnDataSource needs exactly one scroll axis, and the scroll-view default is both.
			// An author who set one axis on the behaviour keeps it.
			UUIRecyclableScrollView* View = InHost->GetComponent<UUIRecyclableScrollView>();
			if (View->GetHorizontal() == View->GetVertical())
			{
				View->SetHorizontal(false);
				View->SetVertical(true);
			}
			return View->GetVertical();
		}

		virtual void SetContent(UDreamWidget* InHost, UDreamWidget* InContent) const override
		{
			if (UUIRecyclableScrollView* View = InHost->GetComponent<UUIRecyclableScrollView>())
			{
				View->SetContent(InContent);
			}
		}

		virtual UObject* Bind(UDreamUserWidget* InOwner, const FDreamWidgetEachBinding& InBinding,
			UDreamWidget* InHost, UDreamWidget* InTemplate, UDreamWidget* InContent) const override
		{
			UUIRecyclableScrollView* ListView = IsValid(InHost) ? InHost->GetComponent<UUIRecyclableScrollView>() : nullptr;
			if (!IsValid(ListView) || !IsValid(InTemplate))
			{
				return nullptr;
			}
			// The view's Content pointer was authored against the archetype; this instance's content is
			// what the cells have to land under.
			if (IsValid(InContent))
			{
				ListView->SetContent(InContent);
			}
			UDreamUIEachAdapter* Adapter = NewObject<UDreamUIEachAdapter>(InOwner);
			Adapter->Initialize(InOwner, InBinding, ListView);

			ListView->SetCellTemplate(InTemplate);
			TScriptInterface<IUIRecyclableScrollViewDataSource> DataSource;
			DataSource.SetObject(Adapter);
			DataSource.SetInterface(Cast<IUIRecyclableScrollViewDataSource>(Adapter));
			ListView->SetDataSource(DataSource);
			return Adapter;
		}

		virtual void Refresh(UObject* InAdapter) const override
		{
			if (UDreamUIEachAdapter* Adapter = Cast<UDreamUIEachAdapter>(InAdapter); IsValid(Adapter))
			{
				Adapter->Refresh();
			}
		}

		virtual const FDreamWidgetEachBinding* GetBinding(const UObject* InAdapter) const override
		{
			const UDreamUIEachAdapter* Adapter = Cast<UDreamUIEachAdapter>(InAdapter);
			return IsValid(Adapter) ? &Adapter->GetBinding() : nullptr;
		}
	};

	FEachBindingHandler Handler;
}

void DreamUIEachAdapter::RegisterEachBindingHandler()
{
	DreamUI::SetEachBindingHandler(&DreamUIEachAdapterLocal::Handler);
}

void DreamUIEachAdapter::UnregisterEachBindingHandler()
{
	if (DreamUI::GetEachBindingHandler() == &DreamUIEachAdapterLocal::Handler)
	{
		DreamUI::SetEachBindingHandler(nullptr);
	}
}
