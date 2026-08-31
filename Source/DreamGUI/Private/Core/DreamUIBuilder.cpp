// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIBuilder.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelSlot.h"
#include "DreamGUI.h"

namespace
{
	/** A .Then and the widget it was written on, held until the tree is finished. */
	using FDreamUIDeferredEntry = TPair<TWeakObjectPtr<UDreamWidget>, TFunction<void(UDreamWidget&)>>;

	/**
	 * One `+ Something { }`.
	 *
	 * The three-way split is the framework's, not a convenience: a layout container is set through
	 * CreateNewLayoutContainer and owes its companion behaviours a Sync call, a layout-self through
	 * CreateNewLayoutSelf, and only what is left is a component. .dui dispatches the same three ways
	 * on the same classes, and the two must agree or the same `+ Overlay` would mean different
	 * things depending on which one wrote it.
	 */
	UObject* CreateComponent(UDreamWidget& InWidget, UClass* InClass)
	{
		if (InClass->IsChildOf(UDreamLayoutContainer::StaticClass()))
		{
			UDreamLayoutContainer* Previous = InWidget.GetLayoutContainer();
			UDreamLayoutContainer* Container = InWidget.CreateNewLayoutContainer(InClass);
			InWidget.SyncRequiredBehavioursForLayoutContainer(Previous, Container);
			return Container;
		}
		if (InClass->IsChildOf(UDreamLayoutSelf::StaticClass()))
		{
			return InWidget.CreateNewLayoutSelf(InClass);
		}
		if (InClass->IsChildOf(UDreamUIBehaviour::StaticClass()))
		{
			return InWidget.AddComponent(InClass);
		}
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' is neither a behaviour nor a layout, so '%s' cannot carry one."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InClass->GetName(), *InWidget.GetDisplayName());
		return nullptr;
	}

	/** The child's slot, minted if the parent hands them out. Same condition the text builder uses. */
	void ApplySlotSettings(FDreamUINodeSpec& InSpec, UDreamWidget& InWidget, UDreamWidget* InParent)
	{
		if (InSpec.SlotInit.Num() == 0)
		{
			return;
		}
		UDreamPanelSlot* Slot = InWidget.GetPanelSlot();
		if (!IsValid(Slot) && IsValid(InParent) && InParent->HasPanelSlots())
		{
			// EnsurePanelSlotForChild does this at registration, and this tree is not registered yet.
			// Without minting it here the settings would be written to nothing, and the slot would then
			// be created empty the first time anything looked.
			Slot = InWidget.CreateNewPanelSlot(UDreamPanelSlot::StaticClass());
		}
		if (!IsValid(Slot))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' has slot settings, but its parent lays out no panel slots."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *InWidget.GetDisplayName());
			return;
		}
		for (TFunction<void(UDreamPanelSlot&)>& Init : InSpec.SlotInit)
		{
			if (Init)
			{
				Init(*Slot);
			}
		}
	}

	UDreamWidget* RealizeNode(UDreamWidgetTree& InTree, FDreamUINodeSpec& InSpec, UDreamWidget* InParent,
		TArray<FDreamUIDeferredEntry>& OutDeferred)
	{
		UClass* WidgetClass = InSpec.WidgetClass != nullptr ? InSpec.WidgetClass : UDreamWidget::StaticClass();
		// NAME_None deliberately: object names have to be unique inside the tree, and two nodes called
		// "BG" in different branches is ordinary. The authored name goes to DisplayName, which is what
		// every downstream lookup matches on anyway.
		UDreamWidget* Widget = InTree.ConstructWidget(WidgetClass, NAME_None);
		if (!IsValid(Widget))
		{
			return nullptr;
		}
		if (!InSpec.Name.IsNone())
		{
			Widget->SetDisplayName(InSpec.Name.ToString());
		}

		if (InSpec.VisualClass != nullptr)
		{
			Widget->CreateNewVisual(InSpec.VisualClass);
		}

		for (FDreamUINodeSpec::FComponent& Component : InSpec.Components)
		{
			if (Component.Class == nullptr)
			{
				continue;
			}
			UObject* Created = CreateComponent(*Widget, Component.Class);
			if (Created != nullptr && Component.Init)
			{
				Component.Init(*Created);
			}
		}

		for (TFunction<void(UDreamWidget&)>& Init : InSpec.WidgetInit)
		{
			if (Init)
			{
				Init(*Widget);
			}
		}

		if (UDreamVisual* Visual = Widget->GetVisual())
		{
			for (TFunction<void(UDreamVisual&)>& Init : InSpec.VisualInit)
			{
				if (Init)
				{
					Init(*Visual);
				}
			}
		}
		else if (InSpec.VisualInit.Num() > 0)
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' has visual settings but no visual was created."),
				ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Widget->GetDisplayName());
		}

		// Before the children, so a child's lambda may already read what an ancestor captured.
		for (TFunction<void(UDreamWidget*)>& Capture : InSpec.Captures)
		{
			if (Capture)
			{
				Capture(Widget);
			}
		}

		// The statement after the widget exists. Nothing here is registered, so this is the attach that
		// costs nothing; TrySetParent would run layout against a hierarchy that is still being built.
		if (InParent != nullptr)
		{
			Widget->SetParentBeforeRegister(InParent);
		}

		ApplySlotSettings(InSpec, *Widget, InParent);

		for (FDreamUINodeSpec& Child : InSpec.ChildSpecs)
		{
			RealizeNode(InTree, Child, Widget, OutDeferred);
		}

		for (TFunction<void(UDreamWidget&)>& Deferred : InSpec.Deferred)
		{
			if (Deferred)
			{
				OutDeferred.Emplace(Widget, MoveTemp(Deferred));
			}
		}
		return Widget;
	}
}

UDreamWidget* DreamUI::Realize(UDreamWidgetTree* InTree, FDreamUINodeSpec&& InRoot)
{
	if (!IsValid(InTree))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Nothing to build into."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	FDreamUINodeSpec RootSpec = MoveTemp(InRoot);
	TArray<FDreamUIDeferredEntry> Deferred;
	UDreamWidget* Root = RealizeNode(*InTree, RootSpec, nullptr, Deferred);

	// Declaration order, parents before children. Which of two .Then bodies runs first is not
	// something a tree should depend on, but it should at least be the same answer every time.
	for (FDreamUIDeferredEntry& Entry : Deferred)
	{
		if (UDreamWidget* Widget = Entry.Key.Get())
		{
			Entry.Value(*Widget);
		}
	}
	return Root;
}

UDreamWidget* DreamUI::Realize(UDreamUserWidget* InOwner, FDreamUINodeSpec&& InRoot)
{
	if (!IsValid(InOwner))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Nothing to build for."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		return nullptr;
	}
	if (InOwner->WidgetTree == nullptr)
	{
		// A class that declares its hierarchy in code gets no archetype, so InitializeWidgetStatic
		// returned before making one. Everything below the user widget still has to live in a tree:
		// that indirection is what separates "the class of a widget" from "the class of a hierarchy".
		InOwner->WidgetTree = NewObject<UDreamWidgetTree>(InOwner, NAME_None, RF_Transactional);
	}
	UDreamWidget* Root = DreamUI::Realize(InOwner->WidgetTree, MoveTemp(InRoot));
	if (IsValid(Root))
	{
		InOwner->WidgetTree->RootWidget = Root;
		Root->SetParentBeforeRegister(InOwner);
	}
	return Root;
}
