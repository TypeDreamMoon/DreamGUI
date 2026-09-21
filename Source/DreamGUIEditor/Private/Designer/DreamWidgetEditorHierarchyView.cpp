// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamWidgetEditorHierarchyView.h"
#include "DreamWidgetBlueprint.h"

#include "DreamGUIEditorModule.h"
#include "DreamWidgetBlueprintEditor.h"
#include "DreamWidgetEditorHierarchyViewItem.h"
#include "SDreamWidgetDesignerViewport.h"
#include "DreamWidgetDesignerViewportClient.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamUISettings.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamLayout.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamUIControlRegistry.h"
#include "SDreamWidgetPalette.h"//FDreamUIPaletteDragDropOp
#include "Styling/SlateIconFinder.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/Commands/GenericCommands.h"

#define LOCTEXT_NAMESPACE "DreamWidgetEditorHierarchyView"

void SDreamWidgetEditorHierarchyView::Construct(const FArguments& InArgs, UWorld* InWorld)
{
	World = InWorld;
	Manager = FDreamWidgetBlueprintEditor::GetEditorByWorld(World.Get());
	bRebuildTreeRequested = false;
	bIsUpdatingSelection = false;

	// register for any objects replaced
	FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &SDreamWidgetEditorHierarchyView::OnObjectsReplaced);
	
	SearchBoxWidgetFilter = MakeShareable(new WidgetTextFilter(WidgetTextFilter::FItemToStringArray::CreateSP(this, &SDreamWidgetEditorHierarchyView::GetWidgetFilterStrings)));

	FilterHandler = MakeShareable(new TreeFilterHandler<TWeakObjectPtr<UDreamWidget>>());
	FilterHandler->SetFilter(SearchBoxWidgetFilter.Get());
	FilterHandler->SetRootItems(&RootWidgets, &TreeRootWidgets);
	FilterHandler->SetGetChildrenDelegate(TreeFilterHandler< TWeakObjectPtr<UDreamWidget> >::FOnGetChildren::CreateRaw(this, &SDreamWidgetEditorHierarchyView::OnGetChildren));

	CommandList = MakeShareable(new FUICommandList);
	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SDreamWidgetEditorHierarchyView::BeginRename),
		FCanExecuteAction::CreateSP(this, &SDreamWidgetEditorHierarchyView::CanRename)
	);

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(4)
		.AutoHeight()
		[
			SAssignNew(SearchBoxPtr, SSearchBox)
			.HintText(LOCTEXT("SearchWidgets", "Search Widgets"))
			.OnTextChanged(this, &SDreamWidgetEditorHierarchyView::OnSearchChanged)
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TreeViewArea, SBorder)
			.Padding(0)
			.BorderImage( FAppStyle::GetBrush( "NoBrush" ) )
		]
	];

	RebuildTreeView();

	bRefreshRequested = true;

	if (Manager.IsValid())
	{
		Manager.Pin()->OnSelectionChanged.AddRaw(this, &SDreamWidgetEditorHierarchyView::OnEditorSelectionChanged);

		// Collapsed rows are recorded by name: the preview widget a row shows is a different object
		// after every rebuild, but the name it shares with its template is not.
		//
		// The Blueprint is checked rather than dereferenced through: this panel is also constructed
		// for a manager whose asset is being replaced, and GetWidgetBlueprint answers null there.
		TSet<TWeakObjectPtr<UDreamWidget>> UnexpendWidgetSet;
		const UDreamWidgetBlueprint* Blueprint = Manager.Pin()->GetWidgetBlueprint();
		UDreamWidget* PreviewRoot = Manager.Pin()->GetPreviewRootWidget();
		if (IsValid(Blueprint) && IsValid(PreviewRoot))
		{
			const TSet<FName>& UnexpandedNames = Blueprint->DesignerData.UnexpandedWidgets;
			TArray<UDreamWidget*> AllWidgets;
			CollectDreamWidgetsToNestedBoundary(PreviewRoot, AllWidgets);
			for (UDreamWidget* Widget : AllWidgets)
			{
				if (IsValid(Widget) && UnexpandedNames.Contains(Widget->GetFName()))
				{
					UnexpendWidgetSet.Add(Widget);
				}
			}
		}

		// The tick runs a frame later and the manager holds the lambda meanwhile, so nothing in it may
		// be captured raw: closing the designer within that frame leaves this panel destroyed and
		// the widgets it named collected. Weak on both sides, and the whole body is skipped if either
		// is already gone.
		UDreamUIManagerObject::AddOneShotTickFunction([WeakSelf = TWeakPtr<SDreamWidgetEditorHierarchyView>(SharedThis(this)), UnexpendWidgetSet]()
		{
			auto Self = WeakSelf.Pin();
			if (!Self.IsValid() || !Self->WidgetTreeView.IsValid())return;
			TSet<TWeakObjectPtr<UDreamWidget>> VisitingItems;
			Self->WidgetTreeView->GetExpandedItems(VisitingItems);
			for (auto& Item : VisitingItems)
			{
				if (UnexpendWidgetSet.Contains(Item))
				{
					Self->WidgetTreeView->SetItemExpansion(Item, false);
				}
			}
		},1);
	}
}
SDreamWidgetEditorHierarchyView::~SDreamWidgetEditorHierarchyView()
{
	if (Manager.IsValid())
	{
		Manager.Pin()->OnSelectionChanged.RemoveAll(this);
	}

	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
}
void SDreamWidgetEditorHierarchyView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRebuildTreeRequested || bRefreshRequested)
	{
		if (bRebuildTreeRequested)
		{
			RebuildTreeView();
		}

		RefreshTree();

		UpdateItemsExpansionFromModel();

		OnEditorSelectionChanged();

		bRefreshRequested = false;
		bRebuildTreeRequested = false;
	}
}
void SDreamWidgetEditorHierarchyView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{

}
void SDreamWidgetEditorHierarchyView::OnMouseLeave(const FPointerEvent& MouseEvent)
{

}
FReply SDreamWidgetEditorHierarchyView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Manager.IsValid() && Manager.Pin()->GetToolkitCommands()->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	if (CommandList->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
FReply SDreamWidgetEditorHierarchyView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// only reached when no tree row handled the drop (i.e. the empty area below the items)
	if (DragDropEvent.GetOperationAs<FDreamUIPaletteDragDropOp>().IsValid())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
FReply SDreamWidgetEditorHierarchyView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// drop in the empty area -> create under the prefab root widget (UMG-style "add to root")
	if (auto PaletteOp = DragDropEvent.GetOperationAs<FDreamUIPaletteDragDropOp>())
	{
		if (auto Editor = Manager.Pin())
		{
			PaletteOp->CreateUnder(Editor->GetPreviewRootWidget());
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}
bool DreamWidgetHierarchyRename::CanRename(const UDreamWidget* Widget, bool bLockedInDesigner)
{
	return IsValid(Widget) && !bLockedInDesigner;
}
bool SDreamWidgetEditorHierarchyView::CanRename() const
{
	auto SelectedItems = WidgetTreeView->GetSelectedItems();
	if (SelectedItems.Num() == 1)
	{
		auto Widget = SelectedItems[0].Get();
		return DreamWidgetHierarchyRename::CanRename(Widget, Manager.IsValid() && Manager.Pin()->IsWidgetLockedForInteraction(Widget));
	}
	return false;
}
void SDreamWidgetEditorHierarchyView::BeginRename()
{
	auto SelectedItems = WidgetTreeView->GetSelectedItems();
	if (SelectedItems.Num() != 1)return;
	if (!CanRename())return;
	auto Item = SelectedItems[0];
	if (auto ItemWidget = StaticCastSharedPtr<SDreamWidgetEditorHierarchyViewItem>(WidgetTreeView->WidgetFromItem(Item)))
	{
		ItemWidget->RequestEditName();
		return;
	}
	// The row is virtualized away, so there is no edit box to enter yet. Scrolling to it builds one,
	// but not before this tick is over -- which is the case F2 on a selection applied from the
	// viewport lands in. Expand first: a row under a collapsed parent is not in the tree's item
	// list at all, so the scroll request is dropped and the retry finds nothing either.
	for (UDreamWidget* Ancestor = Item.IsValid() ? Item->GetParent() : nullptr; Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		WidgetTreeView->SetItemExpansion(Ancestor, true);
	}
	WidgetTreeView->RequestScrollIntoView(Item);
	UDreamUIManagerObject::AddOneShotTickFunction([WeakSelf = TWeakPtr<SDreamWidgetEditorHierarchyView>(SharedThis(this)), Item]()
	{
		auto Self = WeakSelf.Pin();
		if (!Self.IsValid() || !Item.IsValid())return;
		if (auto ItemWidget = StaticCastSharedPtr<SDreamWidgetEditorHierarchyViewItem>(Self->WidgetTreeView->WidgetFromItem(Item)))
		{
			ItemWidget->RequestEditName();
		}
	}, 1);
}
TWeakObjectPtr<UDreamWidget> SDreamWidgetEditorHierarchyView::SetSelectionByNodeObject(UDreamWidget* Element)
{
	WidgetTreeView->ClearSelection();
	if (!Element)return nullptr;
	struct LOCAL
	{
		static UDreamWidget* RecursiveSearch(UDreamWidget* Element, UDreamWidget* Root)
		{
			auto& Children = Root->GetChildren();
			for (auto& Item: Children)
			{
				if (Item == Element)
				{
					return Item;
				}
				if (auto Result = RecursiveSearch(Element, Item))
				{
					return Result;
				}
			}
			return nullptr;
		}
	};
	
	// Every root hierarchy, not just the first -- and there may be none at all, which used to index
	// past the end of an empty array.
	for (auto& RootWidget : RootWidgets)
	{
		if (!RootWidget.IsValid())continue;
		// RecursiveSearch only ever descends into children, so a root could never match itself --
		// selecting the prefab root from anywhere outside the tree silently did nothing.
		if (RootWidget.Get() == Element)
		{
			WidgetTreeView->SetSelection(Element);
			return Element;
		}
		if (auto FoundItem = LOCAL::RecursiveSearch(Element, RootWidget.Get()))
		{
			WidgetTreeView->SetSelection(FoundItem);
			return FoundItem;
		}
	}
	return nullptr;
}

void SDreamWidgetEditorHierarchyView::SetSelectionsByNodeObjects(const TArray<TWeakObjectPtr<UDreamWidget>>& ElementArray)
{
	WidgetTreeView->ClearSelection();
	WidgetTreeView->SetItemSelection(ElementArray, true);
}

void SDreamWidgetEditorHierarchyView::ClearSelection()
{
	WidgetTreeView->ClearSelection();
	RequestRefresh();
}

void SDreamWidgetEditorHierarchyView::RequestRefresh()
{
	bRefreshRequested = true;
}
void SDreamWidgetEditorHierarchyView::RefreshImmediately()
{
	RebuildTreeView();
	RefreshTree();
	UpdateItemsExpansionFromModel();
}
namespace DreamWidgetHierarchyRows
{
	void CollectRoots(const TArray<TObjectPtr<UDreamWidget>>& InAllWidgets, TArray<TWeakObjectPtr<UDreamWidget>>& OutRoots)
	{
		TSet<const UDreamWidget*> Seen;
		Seen.Reserve(InAllWidgets.Num());
		for (const TObjectPtr<UDreamWidget>& Widget : InAllWidgets)
		{
			if (!IsValid(Widget) || !Widget->IsRootWidgetInHierarchy())
			{
				continue;
			}
			bool bAlreadySeen = false;
			Seen.Add(Widget, &bAlreadySeen);
			if (bAlreadySeen)
			{
				// The manager's list holding one widget twice is a registration bug, not something
				// the panel can fix -- but it must not be the reason the editor dies.
				UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d '%s' is registered more than once; showing it once."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Widget->GetPathDisplayName());
				continue;
			}
			OutRoots.Add(Widget);
		}
	}

	void CollectChildren(UDreamWidget* InParent, TArray<TWeakObjectPtr<UDreamWidget>>& OutChildren)
	{
		if (!IsValid(InParent))
		{
			return;
		}
		// A nested widget blueprint instance shows its SLOTS here, not its contents: the contents
		// belong to another asset and are edited by opening it, while a slot is a hole this host was
		// invited to fill. Everything else shows its own children.
		TArray<UDreamWidget*> Children;
		CollectDreamEditorChildren(InParent, Children);
		// The back-pointer check only means anything for a widget InParent actually lists. A slot row
		// is reached through the class that declares it, not through Children, and it can be reached
		// that way from exactly one instance -- so it cannot land on two rows either.
		const bool bChildrenAreItsOwn = DreamWidget_ShouldEditorExpandContents(InParent);
		TSet<const UDreamWidget*> Seen;
		Seen.Reserve(Children.Num());
		for (UDreamWidget* Child : Children)
		{
			if (!IsValid(Child))
			{
				continue;
			}
			if (bChildrenAreItsOwn && Child->GetParent() != InParent)
			{
				// Children is the persistent record and Parent is derived from it, so the two
				// disagreeing means some other widget's Children array holds this one too. Showing
				// it under both is what SListView asserts on; showing it under the parent it names
				// keeps every widget on exactly one row.
				UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d '%s' is listed under '%s' but names '%s' as its parent; not showing it here."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Child->GetPathDisplayName(),
					*InParent->GetPathDisplayName(), *GetNameSafe(Child->GetParent()));
				continue;
			}
			bool bAlreadySeen = false;
			Seen.Add(Child, &bAlreadySeen);
			if (bAlreadySeen)
			{
				UE_LOG(DreamGUIEditor, Warning, TEXT("[%s].%d '%s' appears twice under '%s'; showing it once."),
					ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *Child->GetPathDisplayName(), *InParent->GetPathDisplayName());
				continue;
			}
			OutChildren.Add(Child);
		}
	}
}

void SDreamWidgetEditorHierarchyView::RefreshTree()
{
	RootWidgets.Empty();

	// The authored root is the top row, the way UMG's hierarchy shows the user widget's own tree.
	//
	// Walking the manager's parentless widgets instead surfaces the preview scaffolding: the design
	// canvas agent, and under it the UDreamUserWidget instance the contents hang inside. Both are
	// real widgets and neither is in the .dui or in the authoring tree, so every row above the root
	// is one the author cannot edit, cannot find in their file, and cannot delete.
	if (Manager.IsValid())
	{
		if (UDreamWidget* PreviewRoot = Manager.Pin()->GetPreviewRootWidget())
		{
			RootWidgets.Add(PreviewRoot);
		}
	}

	// Before the first successful build there is no preview root, and the scaffolding walk is still
	// the only thing that can show anything at all.
	if (RootWidgets.Num() == 0)
	{
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World.Get()))
		{
			DreamWidgetHierarchyRows::CollectRoots(DreamUIManager->GetAllWidgetArray(), RootWidgets);
		}
	}

	FilterHandler->RefreshAndFilterTree();
}
void SDreamWidgetEditorHierarchyView::RebuildTreeView()
{
	float OldScrollOffset = 0;
	if (WidgetTreeView.IsValid())
	{
		OldScrollOffset = WidgetTreeView->GetScrollOffset();
	}

	SAssignNew(WidgetTreeView, STreeView<TWeakObjectPtr<UDreamWidget>>)
		.SelectionMode(ESelectionMode::Multi)
		.OnGetChildren(FilterHandler.ToSharedRef(), &TreeFilterHandler< TWeakObjectPtr<UDreamWidget>>::OnGetFilteredChildren)
		.OnGenerateRow(this, &SDreamWidgetEditorHierarchyView::OnGenerateRow)
		.OnSelectionChanged(this, &SDreamWidgetEditorHierarchyView::OnSelectionChanged)
		.OnExpansionChanged(this, &SDreamWidgetEditorHierarchyView::OnExpansionChanged)
		.OnContextMenuOpening(this, &SDreamWidgetEditorHierarchyView::OnContextMenuOpening)
		.OnSetExpansionRecursive(this, &SDreamWidgetEditorHierarchyView::SetItemExpansionRecursive)
		.TreeItemsSource(&TreeRootWidgets)
		//.OnMouseButtonClick(this, &SDreamWidgetHierarchyView::WidgetHierarchy_OnMouseClick)
		;

	FilterHandler->SetTreeView(WidgetTreeView.Get());

	TreeViewArea->SetContent(
		SNew(SScrollBorder, WidgetTreeView.ToSharedRef())
		[
			WidgetTreeView.ToSharedRef()
		]
	);

	// Restore the previous scroll offset
	WidgetTreeView->SetScrollOffset(OldScrollOffset);
}

void SDreamWidgetEditorHierarchyView::OnEditorSelectionChanged()
{
	if (!bIsUpdatingSelection)
	{
		WidgetTreeView->ClearSelection();

		auto Selection = UDreamUISelection::GetInstance(World.Get());
		auto SelectedWidgets = Selection ? Selection->GetSelectedWidgets() : TArray<TWeakObjectPtr<UDreamWidget>>();
		if (SelectedWidgets.Num() == 0)
		{
			ClearSelection();
		}
		else
		{
			TArray<TWeakObjectPtr<UDreamWidget>> SelectedItems;
			for (auto& Item: SelectedWidgets)
			{
				if (Item.IsValid())
				{
					SelectedItems.Add(Item.Get());
				}
			}
			if (SelectedItems.Num() == 0)
			{
				ClearSelection();
			}
			else
			{
				SetSelectionsByNodeObjects(SelectedItems);

				//expand
				if (SelectedItems.Num() == 1)
				{
					auto Widget = SelectedItems[0]->GetParent();
					while (Widget != nullptr)
					{
						WidgetTreeView->SetItemExpansion(Widget, true);
						Widget = Widget->GetParent();
					}
				}

				// This selection came from somewhere else -- a viewport click, Find References, undo
				// -- so the tree is still scrolled wherever the user left it. Expanding the ancestors
				// only makes the row exist; it does not bring it on screen, and a row off screen is
				// not generated at all.
				WidgetTreeView->RequestScrollIntoView(SelectedItems[0]);
			}
		}
	}
}

void SDreamWidgetEditorHierarchyView::OnWidgetHierarchyChanged()
{
	bRefreshRequested = true;
}

void SDreamWidgetEditorHierarchyView::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	// OnObjectsReplaced fires for EVERY Blueprint recompile in the editor, and the answer here used
	// to be to throw the STreeView away and build a new one. That is two defects in one line: it ran
	// for a recompile of an Actor Blueprint that has nothing to do with this panel, and even when the
	// recompile WAS relevant it destroyed the widget the user might be typing a new name into.
	//
	// Rows hold UDreamWidget pointers, so only a replacement touching one of those can have
	// invalidated anything shown here.
	bool bTouchesHierarchy = false;
	for (const TPair<UObject*, UObject*>& Replacement : ReplacementMap)
	{
		const UObject* Replaced = Replacement.Key;
		if (Replaced != nullptr
			&& (Replaced->IsA<UDreamWidget>() || Replaced->IsA<UDreamUIBehaviour>()))
		{
			bTouchesHierarchy = true;
			break;
		}
	}
	if (!bTouchesHierarchy)
	{
		return;
	}
	// A REFRESH, never a rebuild. RefreshTree re-derives RootWidgets from the manager from scratch and
	// the filter handler re-flattens the tree into the source array the STreeView reads, so nothing
	// stale survives it -- the tree VIEW is the one thing that does, and it is the thing holding the
	// inline rename editor, the scroll position and the row widgets. Rebuilding it was only ever
	// standing in for the expansion state that a pointer-keyed map lost on every rebuild; that map is
	// keyed by name now (see ExpansionMap) and comes back through UpdateItemsExpansionFromModel.
	bRefreshRequested = true;
}

TSharedRef< ITableRow > SDreamWidgetEditorHierarchyView::OnGenerateRow(TWeakObjectPtr<UDreamWidget> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	// Hovering a row outlines the widget on the design surface -- the reverse of the viewport's own
	// hover, through the same overlay. Exit clears only its own announcement: rows fire enter-B
	// before exit-A when the cursor slides down the list, and an unconditional clear would wipe B's.
	auto ViewportClientOf = [](const TWeakPtr<FDreamWidgetBlueprintEditor>& InManager)
		-> TSharedPtr<FDreamWidgetDesignerViewportClient>
	{
		const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = InManager.Pin();
		const TSharedPtr<SDreamWidgetDesignerViewport> Viewport = Editor.IsValid() ? Editor->GetViewportWidget() : nullptr;
		return Viewport.IsValid() ? Viewport->GetViewportClient() : nullptr;
	};
	return SNew(SDreamWidgetEditorHierarchyViewItem, OwnerTable, InItem, SharedThis(this), Manager.Pin())
	.HighlightText(this, &SDreamWidgetEditorHierarchyView::GetSearchText)
	.MouseEnter_Lambda([WeakManager = Manager, InItem, ViewportClientOf] {
			if (const TSharedPtr<FDreamWidgetDesignerViewportClient> Client = ViewportClientOf(WeakManager))
			{
				Client->SetExternalHoverWidget(InItem.Get());
			}
		})
	.MouseExit_Lambda([WeakManager = Manager, InItem, ViewportClientOf] {
			if (const TSharedPtr<FDreamWidgetDesignerViewportClient> Client = ViewportClientOf(WeakManager))
			{
				Client->ClearExternalHoverWidget(InItem.Get());
			}
		})
	;
}

void SDreamWidgetEditorHierarchyView::OnSelectionChanged(TWeakObjectPtr<UDreamWidget> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		bIsUpdatingSelection = true;

		auto SelectedItems = WidgetTreeView->GetSelectedItems();

		TSet<UDreamWidget*> NewSelectedItems;
		for (auto& Item : SelectedItems)
		{
			NewSelectedItems.Add(Item.Get());
		}
		if (Manager.IsValid())
		{
			Manager.Pin()->SelectWidgets(NewSelectedItems, false);
		}
		else
		{
			if (auto Selection = UDreamUISelection::GetInstance(World.Get()))
			{
				Selection->SelectNone();
				for ( const auto& Widget : NewSelectedItems )
				{
					Selection->SelectWidget(Widget);
				}
			}
		}

		bIsUpdatingSelection = false;
	}
}
void SDreamWidgetEditorHierarchyView::OnGetChildren(TWeakObjectPtr<UDreamWidget> InParent, TArray< TWeakObjectPtr<UDreamWidget> >& OutChildren)
{
	DreamWidgetHierarchyRows::CollectChildren(InParent.Get(), OutChildren);
}
namespace DreamWidgetHierarchyType
{
	static void AddClassTerms(const UObject* Object, TArray<FString>& OutTerms)
	{
		if (Object == nullptr)return;
		const UClass* Class = Object->GetClass();
		OutTerms.Add(Class->GetName());
		// The class name and its display name differ wherever it counts -- "DreamImage" against the
		// "Dream Image" the Palette labels it with -- and the Palette label is the one the user learned.
		const FString DisplayName = Class->GetDisplayNameText().ToString();
		if (!DisplayName.IsEmpty())
		{
			OutTerms.Add(DisplayName);
		}
	}

	void CollectSearchTerms(const UDreamWidget* Widget, TArray<FString>& OutTerms)
	{
		if (Widget == nullptr)return;
		OutTerms.Add(Widget->GetDisplayName());
		AddClassTerms(Widget->GetVisual(), OutTerms);
		AddClassTerms(Widget->GetLayoutContainer(), OutTerms);
		for (const UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			AddClassTerms(Component, OutTerms);
		}
	}

	FString GetTypeLabel(const UDreamWidget* Widget)
	{
		if (Widget == nullptr)return FString();
		// One label only, in the order a reader identifies an element by: what it draws, then what
		// arranges its children, then what it does. A plain widget gets none rather than a redundant
		// "Widget" printed down half the tree.
		if (const UDreamVisual* Visual = Widget->GetVisual())return Visual->GetClass()->GetDisplayNameText().ToString();
		if (const UDreamLayoutContainer* Container = Widget->GetLayoutContainer())return Container->GetClass()->GetDisplayNameText().ToString();
		for (const UDreamUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (Component != nullptr)return Component->GetClass()->GetDisplayNameText().ToString();
		}
		return FString();
	}
}
void SDreamWidgetEditorHierarchyView::GetWidgetFilterStrings(TWeakObjectPtr<UDreamWidget> Item, TArray<FString>& OutStrings)
{
	DreamWidgetHierarchyType::CollectSearchTerms(Item.Get(), OutStrings);
}
void SDreamWidgetEditorHierarchyView::OnSearchChanged(const FText& InFilterText)
{
	bRefreshRequested = true;
	const bool bFilteringEnabled = !InFilterText.IsEmpty();
	if (bFilteringEnabled != FilterHandler->GetIsEnabled())
	{
		FilterHandler->SetIsEnabled(bFilteringEnabled);
		if (bFilteringEnabled)
		{
			SaveItemsExpansion();
		}
		else
		{
			RestoreItemsExpansion();
		}
	}
	SearchBoxWidgetFilter->SetRawFilterText(InFilterText);
	SearchBoxPtr->SetError(SearchBoxWidgetFilter->GetFilterErrorText());
}

FText SDreamWidgetEditorHierarchyView::GetSearchText()const
{
	return SearchBoxWidgetFilter.IsValid() ? SearchBoxWidgetFilter->GetRawFilterText() : FText::GetEmpty();
}

void SDreamWidgetEditorHierarchyView::OnExpansionChanged(TWeakObjectPtr<UDreamWidget> Item, bool bExpanded)
{
	if (!Item.IsValid())
	{
		return;
	}
	ExpansionMap.FindOrAdd(Item->GetFName()) = bExpanded;
}
TSharedPtr<SWidget> SDreamWidgetEditorHierarchyView::OnContextMenuOpening()
{
	if (!Manager.IsValid())
	{
		return nullptr;
	}
	TFunction<UDreamWidget*()> GetSelectedWidgetFunction = [this]()
	{
		if (Manager.IsValid())
		{
			if (Manager.Pin()->GetSelectedWidgets().Num() == 1)
			{
				auto Widget = Manager.Pin()->GetSelectedWidgets()[0];
				if (Widget.IsValid())
				{
					return Widget.Get();
				}
			}
		}
		return (UDreamWidget*)nullptr;
	};
	TFunction<TArray<UDreamWidget*>()> GetSelectedWidgetsFunction = [this]()
	{
		TArray<UDreamWidget*> Widgets;
		if (Manager.IsValid())
		{
			for (auto Widget : Manager.Pin()->GetSelectedWidgets())
			{
				Widgets.Add(Widget.Get());
			}
		}
		return Widgets;
	};
	return FDreamGUIEditorModule::Get().MakeEditorToolsMenu( GetSelectedWidgetFunction, [=, this](FMenuBuilder& MenuBuilder)
	{
		MenuBuilder.BeginSection("Edit", LOCTEXT("Edit", "Edit"));
		{
			MenuBuilder.PushCommandList(Manager.Pin()->GetToolkitCommands());
			{
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Duplicate);
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete);
			}
			MenuBuilder.PopCommandList();
			MenuBuilder.PushCommandList(CommandList.ToSharedRef());
			{
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
			}
			MenuBuilder.PopCommandList();
		}
		MenuBuilder.EndSection();


			// UMG "Wrap With": group the selection under a new container widget
			if (auto Editor = Manager.Pin())
			{
				if (Editor->GetSelectedWidgets().Num() >= 1)
				{
					MenuBuilder.BeginSection("Wrap", LOCTEXT("Wrap", "Wrap"));
					{
						MenuBuilder.AddSubMenu(
							LOCTEXT("WrapWithSubMenu", "Wrap With..."),
							LOCTEXT("WrapWithSubMenuTooltip", "Group the selected widgets under a new container widget inserted at their position; they keep their layout. The chosen panel then arranges them."),
							FNewMenuDelegate::CreateLambda([WeakEditor = Manager](FMenuBuilder& SubMenu)
							{
								// The plain widget is the only choice with no descriptor behind it: it is
								// the absence of a panel, so a null class is what it spells.
								SubMenu.AddMenuEntry(LOCTEXT("WrapWidget", "Widget"),
									LOCTEXT("WrapWidgetTip", "A wrapper with no panel at all: the children keep the places they are in."),
									FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([WeakEditor]()
									{
										if (auto E = WeakEditor.Pin())E->WrapSelectedWidgets(nullptr);
									})));
								SubMenu.AddSeparator();
								// Everything the palette registers as a panel, which is the same set
								// Replace With offers: a menu naming its containers by hand is a
								// second list, and the day one is extended the two disagree.
								TArray<const FDreamUIControlDescriptor*> Panels;
								FDreamWidgetBlueprintEditor::CollectLayoutPanelDescriptors(nullptr, Panels);
								for (const FDreamUIControlDescriptor* Descriptor : Panels)
								{
									UClass* PanelClass = Descriptor->LayoutContainerClass.Get();
									SubMenu.AddMenuEntry(Descriptor->DisplayName,
										FText::FromString(PanelClass->GetPathName()), Descriptor->Icon,
										FUIAction(FExecuteAction::CreateLambda([WeakEditor, PanelClass]()
										{
											if (auto E = WeakEditor.Pin())E->WrapSelectedWidgets(PanelClass);
										})));
								}
							}));
						// The inverse, in the same section: a wrapper chosen by mistake, or inherited
						// from a hierarchy somebody else authored, had no way out before this.
						MenuBuilder.AddMenuEntry(
							LOCTEXT("UnwrapWidget", "Unwrap"),
							LOCTEXT("UnwrapWidgetTooltip", "Take this widget out from between its parent and its children. The children keep their order and land where it stood; it is then deleted."),
							FSlateIcon(),
							FUIAction(
								FExecuteAction::CreateLambda([WeakEditor = Manager]()
								{
									if (auto E = WeakEditor.Pin())E->UnwrapSelectedWidget();
								}),
								FCanExecuteAction::CreateLambda([WeakEditor = Manager]()
								{
									const TSharedPtr<FDreamWidgetBlueprintEditor> E = WeakEditor.Pin();
									return E.IsValid() && E->CanUnwrapSelectedWidget();
								})));
					}
					MenuBuilder.EndSection();
				}
			}

			// UMG "Replace With": swap the panel that arranges this widget's children.
			if (auto Editor = Manager.Pin())
			{
				const TArray<TWeakObjectPtr<UDreamWidget>>& Selection = Editor->GetSelectedWidgets();
				UDreamWidget* Target = Selection.Num() == 1 ? Selection[0].Get() : nullptr;
				if (IsValid(Target) && IsValid(Target->GetLayoutContainer()))
				{
					MenuBuilder.BeginSection("Replace", LOCTEXT("Replace", "Replace"));
					{
						MenuBuilder.AddSubMenu(
							LOCTEXT("ReplaceWithSubMenu", "Replace With..."),
							LOCTEXT("ReplaceWithSubMenuTooltip", "Swap the panel arranging this widget's children. Unlike UMG, the widget itself is untouched -- its name, place, slot, components and children all stay put; only the layout container changes."),
							FNewMenuDelegate::CreateLambda([WeakEditor = Manager](FMenuBuilder& SubMenu)
							{
								UClass* Current = nullptr;
								if (auto E = WeakEditor.Pin())
								{
									const TArray<TWeakObjectPtr<UDreamWidget>>& Sel = E->GetSelectedWidgets();
									if (Sel.Num() == 1 && Sel[0].IsValid() && IsValid(Sel[0]->GetLayoutContainer()))
									{
										Current = Sel[0]->GetLayoutContainer()->GetClass();
									}
								}
								// Offer whatever the palette registers as a panel, which is the same
								// set the designer can create in the first place -- no second list to
								// drift out of step. UMG filters to multi-child panels; here every
								// registered layout container takes children, so the equivalent
								// filter is simply "has a layout container class".
								TArray<const FDreamUIControlDescriptor*> Panels;
								FDreamWidgetBlueprintEditor::CollectLayoutPanelDescriptors(Current, Panels);
								for (const FDreamUIControlDescriptor* Descriptor : Panels)
								{
									UClass* PanelClass = Descriptor->LayoutContainerClass.Get();
									SubMenu.AddMenuEntry(Descriptor->DisplayName,
										FText::FromString(PanelClass->GetPathName()), Descriptor->Icon,
										FUIAction(FExecuteAction::CreateLambda([WeakEditor, PanelClass]()
										{
											if (auto E = WeakEditor.Pin())E->ReplaceSelectedWidgetLayout(PanelClass);
										})));
								}
							}));
					}
					MenuBuilder.EndSection();
				}
			}

			// Hide / Lock / Focus. The row already carries the first two as little buttons, but the
			// VIEWPORT reuses this menu and has no rows -- so from the design surface there was no
			// way to put a widget away, and no Focus at all outside the F key. Every one of these is
			// an existing designer operation; none of them is new behaviour.
			if (auto Editor = Manager.Pin())
			{
				TArray<TWeakObjectPtr<UDreamWidget>> Targets = Editor->GetSelectedWidgets();
				Targets.RemoveAll([](const TWeakObjectPtr<UDreamWidget>& Widget) { return !Widget.IsValid(); });
				if (Targets.Num() > 0)
				{
					// What the toggles DO is decided from the first selected widget and applied to
					// all of them: a mixed selection must end up in one state, not flip each widget
					// to its own opposite and leave the set exactly as mixed as it was.
					const bool bFirstHidden = Editor->IsWidgetHiddenInDesigner(Targets[0].Get());
					const bool bFirstLocked = Editor->IsWidgetLockedInDesigner(Targets[0].Get());
					MenuBuilder.BeginSection("Designer", LOCTEXT("DesignerSection", "Designer"));
					{
						MenuBuilder.AddMenuEntry(
							bFirstHidden ? LOCTEXT("ShowInDesigner", "Show in Designer") : LOCTEXT("HideInDesigner", "Hide in Designer"),
							LOCTEXT("HideInDesignerTooltip", "Take the selected widgets out of the design surface. They still exist and still run; they are simply not drawn and cannot be clicked while hidden."),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), bFirstHidden ? TEXT("Level.VisibleIcon16x") : TEXT("Level.NotVisibleIcon16x")),
							FUIAction(FExecuteAction::CreateLambda([WeakEditor = Manager, Targets, bFirstHidden]()
							{
								if (auto E = WeakEditor.Pin())
								{
									for (const TWeakObjectPtr<UDreamWidget>& Widget : Targets)
									{
										if (Widget.IsValid())E->SetWidgetHiddenInDesigner(Widget.Get(), !bFirstHidden);
									}
								}
							})));
						MenuBuilder.AddMenuEntry(
							bFirstLocked ? LOCTEXT("UnlockInDesigner", "Unlock") : LOCTEXT("LockInDesigner", "Lock"),
							LOCTEXT("LockInDesignerTooltip", "Stop the selected widgets being picked or dragged on the design surface. Their children go with them."),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), bFirstLocked ? TEXT("Icons.Unlock") : TEXT("Icons.Lock")),
							FUIAction(FExecuteAction::CreateLambda([WeakEditor = Manager, Targets, bFirstLocked]()
							{
								if (auto E = WeakEditor.Pin())
								{
									for (const TWeakObjectPtr<UDreamWidget>& Widget : Targets)
									{
										if (Widget.IsValid())E->SetWidgetLockedInDesigner(Widget.Get(), !bFirstLocked, /*bRecursive*/true);
									}
								}
							})));
						// Find References, where UMG's hierarchy puts it. Two searches behind one
						// entry: Kismet's Find-in-Blueprint over this asset's graphs, and the asset
						// registry over the project when the widget is an instance of another class.
						MenuBuilder.AddMenuEntry(
							LOCTEXT("FindWidgetReferences", "Find References"),
							LOCTEXT("FindWidgetReferencesTooltip", "Search this Blueprint's graphs for nodes that name this widget, and report which other assets use its class."),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Kismet.Tabs.FindResults")),
							FUIAction(
								FExecuteAction::CreateLambda([WeakEditor = Manager]()
								{
									if (auto E = WeakEditor.Pin())E->FindReferencesToSelectedWidget();
								}),
								FCanExecuteAction::CreateLambda([WeakEditor = Manager]()
								{
									const TSharedPtr<FDreamWidgetBlueprintEditor> E = WeakEditor.Pin();
									return E.IsValid() && E->CanFindReferencesToSelectedWidget();
								})));
						MenuBuilder.AddMenuEntry(
							LOCTEXT("FocusSelection", "Focus Selection"),
							LOCTEXT("FocusSelectionTooltip", "Frame the selected widgets in the design viewport. The same thing the F key does."),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Search")),
							FUIAction(FExecuteAction::CreateLambda([WeakEditor = Manager]()
							{
								const TSharedPtr<FDreamWidgetBlueprintEditor> E = WeakEditor.Pin();
								const TSharedPtr<SDreamWidgetDesignerViewport> Viewport = E.IsValid() ? E->GetViewportWidget() : nullptr;
								if (Viewport.IsValid() && Viewport->GetViewportClient().IsValid())
								{
									Viewport->GetViewportClient()->FocusViewportToTargets();
								}
							})));
					}
					MenuBuilder.EndSection();
				}
			}

			// UMG-toolbar-style Align / Distribute for a multi-widget selection. The entries themselves
			// live on the toolkit so the viewport menu and the mode toolbar offer the same ones.
			FDreamWidgetBlueprintEditor::FillAlignDistributeMenu(MenuBuilder, Manager);

			// The text half of the designer, and only for a hierarchy the text owns: on a
			// hand-authored one there is no file behind the row that was right-clicked, so the
			// entry is absent rather than greyed -- nothing done here would make it work.
			//
			// One selected widget only. The gesture is "show me this line", and a multi-selection
			// has no single line; offering it for two would have to pick one, which is a cursor
			// jump nobody asked for.
			if (const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = Manager.Pin())
			{
				if (Editor->GetSelectedWidgets().Num() == 1 && Editor->CanRevealInVSCode())
				{
					MenuBuilder.BeginSection("DreamUISource", LOCTEXT("DreamUISource", "DreamUI Source"));
					{
						MenuBuilder.AddMenuEntry(LOCTEXT("RevealInVSCode", "Reveal in VS Code"),
							LOCTEXT("RevealInVSCodeTooltip",
								"Put the VS Code cursor on the line of the .dui that declares this widget, opening the DreamUI workspace first if VS Code is not running."),
							FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.OpenInExternalEditor")),
							FUIAction(FExecuteAction::CreateLambda([WeakEditor = Manager]()
							{
								if (const TSharedPtr<FDreamWidgetBlueprintEditor> E = WeakEditor.Pin())
								{
									const TArray<TWeakObjectPtr<UDreamWidget>>& Sel = E->GetSelectedWidgets();
									E->RevealInVSCode(Sel.Num() == 1 ? Sel[0].Get() : nullptr);
								}
							})));
					}
					MenuBuilder.EndSection();
				}
			}

			// UMG's Hierarchy closes with an Expansion section; it is tree-view state, so it
			// applies whatever is selected.
			MenuBuilder.BeginSection("Expansion", LOCTEXT("Expansion", "Expansion"));
			{
				MenuBuilder.AddMenuEntry(LOCTEXT("CollapseAll", "Collapse All"),
					LOCTEXT("CollapseAllTooltip", "Collapse every widget in the tree."), FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SDreamWidgetEditorHierarchyView::SetAllExpansion, false)));
				MenuBuilder.AddMenuEntry(LOCTEXT("ExpandAll", "Expand All"),
					LOCTEXT("ExpandAllTooltip", "Expand every widget in the tree."), FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SDreamWidgetEditorHierarchyView::SetAllExpansion, true)));
			}
			MenuBuilder.EndSection();
			// Last, so a project's entries read as additions rather than interleaving with ours.
			if (auto Editor = Manager.Pin())FDreamGUIEditorModule::Get().ExtendWidgetContextMenu(MenuBuilder, Editor.ToSharedRef());
	});
}

TSharedPtr<SWidget> SDreamWidgetEditorHierarchyView::BuildContextMenu()
{
	return OnContextMenuOpening();
}

void SDreamWidgetEditorHierarchyView::GetExpandWidgets(TSet<TWeakObjectPtr<UDreamWidget>>& OutExpandWidgets)
{
	WidgetTreeView->GetExpandedItems(OutExpandWidgets);
}

void SDreamWidgetEditorHierarchyView::UpdateItemsExpansionFromModel()
{
	for (auto Widget: RootWidgets)
	{
		RecursiveExpand(Widget.Get(), EExpandBehavior::FromModel);
	}
}

void SDreamWidgetEditorHierarchyView::RestoreItemsExpansion()
{
	for (auto Widget : RootWidgets)
	{
		RecursiveExpand(Widget.Get(), EExpandBehavior::RestoreFromPrevious);
	}
}

void SDreamWidgetEditorHierarchyView::SaveItemsExpansion()
{
	ExpandedItemNames.Empty();

	if (WidgetTreeView.IsValid())
	{
		TSet< TWeakObjectPtr<UDreamWidget> > ExpandedItems;
		WidgetTreeView->GetExpandedItems(ExpandedItems);

		for (TWeakObjectPtr<UDreamWidget> Item : ExpandedItems)
		{
			if (Item.IsValid())
			{
				ExpandedItemNames.Add(Item->GetName());
			}
		}
	}
}
void SDreamWidgetEditorHierarchyView::RecursiveExpand(UDreamWidget* Widget, EExpandBehavior ExpandBehavior)
{
	// Reached with a dead widget for real: RestoreItemsExpansion and UpdateItemsExpansionFromModel
	// walk RootWidgets, which is a list of weak pointers that a structural edit can empty out
	// between the edit and the next Tick -- clearing the search box right after one used to come
	// straight here with null.
	if (!IsValid(Widget) || !WidgetTreeView.IsValid())
	{
		return;
	}
	bool bShouldExpandItem = true;

	switch (ExpandBehavior)
	{
	case EExpandBehavior::NeverExpand:
		{
			bShouldExpandItem = false;
		}
		break;

	case EExpandBehavior::RestoreFromPrevious:
		{
			bShouldExpandItem = ExpandedItemNames.Contains(Widget->GetName());
		}
		break;

	case EExpandBehavior::AlwaysExpand:
		{
			bShouldExpandItem = true;
		}
		break;

	case EExpandBehavior::FromModel:
	default:
		{
			if (const bool* ValuePtr = ExpansionMap.Find(Widget->GetFName()))
			{
				bShouldExpandItem = *ValuePtr;
			}
			else
			{
				bShouldExpandItem = true;
			}
		}
		break;
	}

	WidgetTreeView->SetItemExpansion(Widget, bShouldExpandItem);

	auto& Children = Widget->GetChildren();
	for (auto Child: Children)
	{
		RecursiveExpand(Child, ExpandBehavior);
	}
}

void SDreamWidgetEditorHierarchyView::SetItemExpansionRecursive(TWeakObjectPtr<UDreamWidget> Model, bool bInExpansionState)
{
	if (Model.IsValid())
	{
		RecursiveExpand(Model.Get(), bInExpansionState ? EExpandBehavior::AlwaysExpand : EExpandBehavior::NeverExpand);
	}
}

void SDreamWidgetEditorHierarchyView::SetAllExpansion(bool bExpand)
{
	// The recursion writes through SetItemExpansion, so ExpansionMap follows via OnExpansionChanged
	// and the state survives into PrefabDataForPrefabEditor.UnexpandedWidgetSet on save.
	const EExpandBehavior Behavior = bExpand ? EExpandBehavior::AlwaysExpand : EExpandBehavior::NeverExpand;
	for (auto& Widget : RootWidgets)
	{
		if (Widget.IsValid())
		{
			RecursiveExpand(Widget.Get(), Behavior);
		}
	}
}

#undef LOCTEXT_NAMESPACE
