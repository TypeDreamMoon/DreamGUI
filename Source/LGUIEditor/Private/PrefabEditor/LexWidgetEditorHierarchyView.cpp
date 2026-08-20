// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "LexWidgetEditorHierarchyView.h"

#include "LGUIEditorModule.h"
#include "LexUIPrefabEditor.h"
#include "LexUIBehaviourEditorBackend.h"
#include "LexWidgetEditorHierarchyViewItem.h"
#include "Core/LexUIManager.h"
#include "Core/LexUISettings.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexLayout.h"
#include "Core/LexUIBehaviour.h"
#include "LexUIPrefabBehaviourUtils.h"
#include "LexUIControlRegistry.h"
#include "SLexUIPrefabPalette.h"//FLexUIPaletteDragDropOp
#include "Styling/SlateIconFinder.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/Commands/GenericCommands.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"

#define LOCTEXT_NAMESPACE "LexWidgetEditorHierarchyView"

UE_DISABLE_OPTIMIZATION

void SLexWidgetEditorHierarchyView::Construct(const FArguments& InArgs, UWorld* InWorld)
{
	World = InWorld;
	Manager = FLexUIPrefabEditor::GetEditorByWorld(World.Get());
	bRebuildTreeRequested = false;
	bIsUpdatingSelection = false;

	// register for any objects replaced
	FCoreUObjectDelegates::OnObjectsReplaced.AddRaw(this, &SLexWidgetEditorHierarchyView::OnObjectsReplaced);
	
	SearchBoxWidgetFilter = MakeShareable(new WidgetTextFilter(WidgetTextFilter::FItemToStringArray::CreateSP(this, &SLexWidgetEditorHierarchyView::GetWidgetFilterStrings)));

	FilterHandler = MakeShareable(new TreeFilterHandler<TWeakObjectPtr<ULexWidget>>());
	FilterHandler->SetFilter(SearchBoxWidgetFilter.Get());
	FilterHandler->SetRootItems(&RootWidgets, &TreeRootWidgets);
	FilterHandler->SetGetChildrenDelegate(TreeFilterHandler< TWeakObjectPtr<ULexWidget> >::FOnGetChildren::CreateRaw(this, &SLexWidgetEditorHierarchyView::OnGetChildren));

	CommandList = MakeShareable(new FUICommandList);
	CommandList->MapAction(
		FGenericCommands::Get().Rename,
		FExecuteAction::CreateSP(this, &SLexWidgetEditorHierarchyView::BeginRename),
		FCanExecuteAction::CreateSP(this, &SLexWidgetEditorHierarchyView::CanRename)
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
			.OnTextChanged(this, &SLexWidgetEditorHierarchyView::OnSearchChanged)
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
		Manager.Pin()->OnSelectionChanged.AddRaw(this, &SLexWidgetEditorHierarchyView::OnEditorSelectionChanged);

		auto PrefabHelperObject = Manager.Pin()->GetPrefabHelperObject();
		auto UnexpandWidgetGuidSet = Manager.Pin()->GetPrefabBeingEdited()->PrefabDataForPrefabEditor.UnexpandedWidgetSet;
		TSet<TWeakObjectPtr<ULexWidget>> UnexpendWidgetSet;
		for (auto& ItemActorGuid : UnexpandWidgetGuidSet)
		{
			if (auto ObjectPtr = PrefabHelperObject->MapGuidToObject.Find(ItemActorGuid))
			{
				if (auto Widget = Cast<ULexWidget>(*ObjectPtr))
				{
					UnexpendWidgetSet.Add(Widget);
				}
			}
		}

		// The tick runs a frame later and the manager holds the lambda meanwhile, so nothing in it may
		// be captured raw: closing the prefab editor within that frame leaves this panel destroyed and
		// the widgets it named collected. Weak on both sides, and the whole body is skipped if either
		// is already gone.
		ULexUIManagerObject::AddOneShotTickFunction([WeakSelf = TWeakPtr<SLexWidgetEditorHierarchyView>(SharedThis(this)), UnexpendWidgetSet]()
		{
			auto Self = WeakSelf.Pin();
			if (!Self.IsValid())return;
			TSet<TWeakObjectPtr<ULexWidget>> VisitingItems;
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
SLexWidgetEditorHierarchyView::~SLexWidgetEditorHierarchyView()
{
	if (Manager.IsValid())
	{
		Manager.Pin()->OnSelectionChanged.RemoveAll(this);
	}

	FCoreUObjectDelegates::OnObjectsReplaced.RemoveAll(this);
}
void SLexWidgetEditorHierarchyView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
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
void SLexWidgetEditorHierarchyView::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{

}
void SLexWidgetEditorHierarchyView::OnMouseLeave(const FPointerEvent& MouseEvent)
{

}
FReply SLexWidgetEditorHierarchyView::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
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
FReply SLexWidgetEditorHierarchyView::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// only reached when no tree row handled the drop (i.e. the empty area below the items)
	if (DragDropEvent.GetOperationAs<FLexUIPaletteDragDropOp>().IsValid())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
FReply SLexWidgetEditorHierarchyView::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	// drop in the empty area -> create under the prefab root widget (UMG-style "add to root")
	if (auto PaletteOp = DragDropEvent.GetOperationAs<FLexUIPaletteDragDropOp>())
	{
		if (auto Editor = Manager.Pin())
		{
			PaletteOp->CreateUnder(Editor->GetLoadedRootWidget());
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}
bool LexWidgetHierarchyRename::CanRename(const ULexWidget* Widget, bool bLockedInDesigner)
{
	return IsValid(Widget) && !bLockedInDesigner;
}
bool SLexWidgetEditorHierarchyView::CanRename() const
{
	auto SelectedItems = WidgetTreeView->GetSelectedItems();
	if (SelectedItems.Num() == 1)
	{
		auto Widget = SelectedItems[0].Get();
		return LexWidgetHierarchyRename::CanRename(Widget, Manager.IsValid() && Manager.Pin()->IsWidgetLockedForInteraction(Widget));
	}
	return false;
}
void SLexWidgetEditorHierarchyView::BeginRename()
{
	auto SelectedItems = WidgetTreeView->GetSelectedItems();
	if (SelectedItems.Num() != 1)return;
	if (!CanRename())return;
	auto Item = SelectedItems[0];
	if (auto ItemWidget = StaticCastSharedPtr<SLexWidgetEditorHierarchyViewItem>(WidgetTreeView->WidgetFromItem(Item)))
	{
		ItemWidget->RequestEditName();
		return;
	}
	// The row is virtualized away, so there is no edit box to enter yet. Scrolling to it builds one,
	// but not before this tick is over -- which is the case F2 on a selection applied from the
	// viewport lands in. Expand first: a row under a collapsed parent is not in the tree's item
	// list at all, so the scroll request is dropped and the retry finds nothing either.
	for (ULexWidget* Ancestor = Item.IsValid() ? Item->GetParent() : nullptr; Ancestor != nullptr; Ancestor = Ancestor->GetParent())
	{
		WidgetTreeView->SetItemExpansion(Ancestor, true);
	}
	WidgetTreeView->RequestScrollIntoView(Item);
	ULexUIManagerObject::AddOneShotTickFunction([WeakSelf = TWeakPtr<SLexWidgetEditorHierarchyView>(SharedThis(this)), Item]()
	{
		auto Self = WeakSelf.Pin();
		if (!Self.IsValid() || !Item.IsValid())return;
		if (auto ItemWidget = StaticCastSharedPtr<SLexWidgetEditorHierarchyViewItem>(Self->WidgetTreeView->WidgetFromItem(Item)))
		{
			ItemWidget->RequestEditName();
		}
	}, 1);
}
TWeakObjectPtr<ULexWidget> SLexWidgetEditorHierarchyView::SetSelectionByNodeObject(ULexWidget* Element)
{
	WidgetTreeView->ClearSelection();
	if (!Element)return nullptr;
	struct LOCAL
	{
		static ULexWidget* RecursiveSearch(ULexWidget* Element, ULexWidget* Root)
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

void SLexWidgetEditorHierarchyView::SetSelectionsByNodeObjects(const TArray<TWeakObjectPtr<ULexWidget>>& ElementArray)
{
	WidgetTreeView->ClearSelection();
	WidgetTreeView->SetItemSelection(ElementArray, true);
}

void SLexWidgetEditorHierarchyView::ClearSelection()
{
	WidgetTreeView->ClearSelection();
	RequestRefresh();
}

void SLexWidgetEditorHierarchyView::RequestRefresh()
{
	bRefreshRequested = true;
}
void SLexWidgetEditorHierarchyView::RefreshImmediately()
{
	RebuildTreeView();
	RefreshTree();
	UpdateItemsExpansionFromModel();
}
void SLexWidgetEditorHierarchyView::RefreshTree()
{
	RootWidgets.Empty();
	if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(World.Get()))
	{
		for (auto& Widget : LexUIManager->GetAllWidgetArray())
		{
			if (Widget->IsRootWidgetInHierarchy())
			{
				RootWidgets.Add(Widget);
			}
		}
	}

	FilterHandler->RefreshAndFilterTree();
}
void SLexWidgetEditorHierarchyView::RebuildTreeView()
{
	float OldScrollOffset = 0;
	if (WidgetTreeView.IsValid())
	{
		OldScrollOffset = WidgetTreeView->GetScrollOffset();
	}

	SAssignNew(WidgetTreeView, STreeView<TWeakObjectPtr<ULexWidget>>)
		.SelectionMode(ESelectionMode::Multi)
		.OnGetChildren(FilterHandler.ToSharedRef(), &TreeFilterHandler< TWeakObjectPtr<ULexWidget>>::OnGetFilteredChildren)
		.OnGenerateRow(this, &SLexWidgetEditorHierarchyView::OnGenerateRow)
		.OnSelectionChanged(this, &SLexWidgetEditorHierarchyView::OnSelectionChanged)
		.OnExpansionChanged(this, &SLexWidgetEditorHierarchyView::OnExpansionChanged)
		.OnContextMenuOpening(this, &SLexWidgetEditorHierarchyView::OnContextMenuOpening)
		.OnSetExpansionRecursive(this, &SLexWidgetEditorHierarchyView::SetItemExpansionRecursive)
		.TreeItemsSource(&TreeRootWidgets)
		//.OnMouseButtonClick(this, &SLexWidgetHierarchyView::WidgetHierarchy_OnMouseClick)
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

void SLexWidgetEditorHierarchyView::OnEditorSelectionChanged()
{
	if (!bIsUpdatingSelection)
	{
		WidgetTreeView->ClearSelection();

		auto Selection = ULexUISelection::GetInstance(World.Get());
		auto SelectedWidgets = Selection ? Selection->GetSelectedWidgets() : TArray<TWeakObjectPtr<ULexWidget>>();
		if (SelectedWidgets.Num() == 0)
		{
			ClearSelection();
		}
		else
		{
			TArray<TWeakObjectPtr<ULexWidget>> SelectedItems;
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

void SLexWidgetEditorHierarchyView::OnWidgetHierarchyChanged()
{
	bRefreshRequested = true;
}

void SLexWidgetEditorHierarchyView::OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementMap)
{
	if ( !bRebuildTreeRequested )
	{
		bRefreshRequested = true;
		bRebuildTreeRequested = true;
	}
}

TSharedRef< ITableRow > SLexWidgetEditorHierarchyView::OnGenerateRow(TWeakObjectPtr<ULexWidget> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SLexWidgetEditorHierarchyViewItem, OwnerTable, InItem, SharedThis(this), Manager.Pin())
	.HighlightText(this, &SLexWidgetEditorHierarchyView::GetSearchText)
	.MouseEnter_Lambda([=, this] {
		})
	.MouseExit_Lambda([=, this] {
		})
	;
}

void SLexWidgetEditorHierarchyView::OnSelectionChanged(TWeakObjectPtr<ULexWidget> SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectInfo != ESelectInfo::Direct)
	{
		bIsUpdatingSelection = true;

		auto SelectedItems = WidgetTreeView->GetSelectedItems();

		TSet<ULexWidget*> NewSelectedItems;
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
			if (auto Selection = ULexUISelection::GetInstance(World.Get()))
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
void SLexWidgetEditorHierarchyView::OnGetChildren(TWeakObjectPtr<ULexWidget> InParent, TArray< TWeakObjectPtr<ULexWidget> >& OutChildren)
{
	if (!InParent.IsValid())return;
	auto& Children = InParent->GetChildren();
	OutChildren.Append(Children);
}
namespace LexWidgetHierarchyType
{
	static void AddClassTerms(const UObject* Object, TArray<FString>& OutTerms)
	{
		if (Object == nullptr)return;
		const UClass* Class = Object->GetClass();
		OutTerms.Add(Class->GetName());
		// The class name and its display name differ wherever it counts -- "LexImage" against the
		// "Lex Image" the Palette labels it with -- and the Palette label is the one the user learned.
		const FString DisplayName = Class->GetDisplayNameText().ToString();
		if (!DisplayName.IsEmpty())
		{
			OutTerms.Add(DisplayName);
		}
	}

	void CollectSearchTerms(const ULexWidget* Widget, TArray<FString>& OutTerms)
	{
		if (Widget == nullptr)return;
		OutTerms.Add(Widget->GetDisplayName());
		AddClassTerms(Widget->GetVisual(), OutTerms);
		AddClassTerms(Widget->GetLayoutContainer(), OutTerms);
		for (const ULexUIBehaviour* Component : Widget->GetAllComponents())
		{
			AddClassTerms(Component, OutTerms);
		}
	}

	FString GetTypeLabel(const ULexWidget* Widget)
	{
		if (Widget == nullptr)return FString();
		// One label only, in the order a reader identifies an element by: what it draws, then what
		// arranges its children, then what it does. A plain widget gets none rather than a redundant
		// "Widget" printed down half the tree.
		if (const ULexVisual* Visual = Widget->GetVisual())return Visual->GetClass()->GetDisplayNameText().ToString();
		if (const ULexLayoutContainer* Container = Widget->GetLayoutContainer())return Container->GetClass()->GetDisplayNameText().ToString();
		for (const ULexUIBehaviour* Component : Widget->GetAllComponents())
		{
			if (Component != nullptr)return Component->GetClass()->GetDisplayNameText().ToString();
		}
		return FString();
	}
}
void SLexWidgetEditorHierarchyView::GetWidgetFilterStrings(TWeakObjectPtr<ULexWidget> Item, TArray<FString>& OutStrings)
{
	LexWidgetHierarchyType::CollectSearchTerms(Item.Get(), OutStrings);
}
void SLexWidgetEditorHierarchyView::OnSearchChanged(const FText& InFilterText)
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

FText SLexWidgetEditorHierarchyView::GetSearchText()const
{
	return SearchBoxWidgetFilter.IsValid() ? SearchBoxWidgetFilter->GetRawFilterText() : FText::GetEmpty();
}

void SLexWidgetEditorHierarchyView::OnExpansionChanged(TWeakObjectPtr<ULexWidget> Item, bool bExpanded)
{
	ExpansionMap.FindOrAdd(Item.Get()) = bExpanded;
}
TSharedPtr<SWidget> SLexWidgetEditorHierarchyView::OnContextMenuOpening()
{
	if (!Manager.IsValid())
	{
		return nullptr;
	}
	TFunction<ULexWidget*()> GetSelectedWidgetFunction = [this]()
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
		return (ULexWidget*)nullptr;
	};
	TFunction<TArray<ULexWidget*>()> GetSelectedWidgetsFunction = [this]()
	{
		TArray<ULexWidget*> Widgets;
		if (Manager.IsValid())
		{
			for (auto Widget : Manager.Pin()->GetSelectedWidgets())
			{
				Widgets.Add(Widget.Get());
			}
		}
		return Widgets;
	};
	return FLGUIEditorModule::Get().MakeEditorToolsMenu( GetSelectedWidgetFunction, [=, this](FMenuBuilder& MenuBuilder)
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
			// UMG sits Find References between Delete and Rename.
			MenuBuilder.AddMenuEntry(LOCTEXT("FindReferences", "Find References"),
				LOCTEXT("FindReferencesTooltip", "Search the prefab's companion behaviour blueprint for the variable this widget binds to."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), "Kismet.Tabs.FindResults"),
				FUIAction(
					FExecuteAction::CreateLambda([WeakEditor = Manager]() { if (auto E = WeakEditor.Pin())E->FindReferencesForSelectedWidget(); }),
					FCanExecuteAction::CreateLambda([WeakEditor = Manager]() { auto E = WeakEditor.Pin(); return E.IsValid() && E->CanFindReferencesForSelectedWidget(); })));
			MenuBuilder.PushCommandList(CommandList.ToSharedRef());
			{
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Rename);
			}
			MenuBuilder.PopCommandList();
		}
		MenuBuilder.EndSection();

			// UMG "Is Variable" counterpart: bind the selected element to a typed variable on
			// the prefab's companion behaviour blueprint (created on demand)
			if (auto Editor = Manager.Pin())
			{
				ULexWidget* SelectedWidget = GetSelectedWidgetFunction();
				if (SelectedWidget != nullptr && SelectedWidget != Editor->GetLoadedRootWidget())
				{
					MenuBuilder.BeginSection("Behaviour", LOCTEXT("Behaviour", "Behaviour"));
					{
						MenuBuilder.AddSubMenu(
							LOCTEXT("PromoteSubMenu", "Promote to Behaviour Variable"),
							LOCTEXT("PromoteSubMenuTooltip", "Ask the primary Behaviour's editor backend to declare and bind a reflected variable. Blueprint is supported built-in; external script systems can register a backend."),
							FNewMenuDelegate::CreateLambda([WeakEditor = Manager, WeakWidget = TWeakObjectPtr<ULexWidget>(SelectedWidget)](FMenuBuilder& SubMenu)
							{
									auto AddEntry = [&SubMenu, WeakEditor](UObject* Target, const FText& Label)
									{
									SubMenu.AddMenuEntry(Label, FText::FromString(Target->GetClass()->GetPathName()),
										FSlateIconFinder::FindIconForClass(Target->GetClass()),
										FUIAction(FExecuteAction::CreateLambda([WeakEditor, WeakTarget = TWeakObjectPtr<UObject>(Target)]()
										{
											auto E = WeakEditor.Pin();
											if (E.IsValid() && WeakTarget.IsValid())E->PromoteToBehaviourVariable(WeakTarget.Get());
										}), FCanExecuteAction::CreateLambda([WeakEditor]()
										{
											auto E = WeakEditor.Pin();
											return E.IsValid() && E->CanAuthorBehaviour();
										})));
								};
								ULexWidget* W = WeakWidget.Get();
								if (W == nullptr)return;
								// behaviours carry the useful APIs (Button.OnClick / ...), then the visual, then the widget
								for (ULexUIBehaviour* Comp : W->GetAllComponents())
								{
									if (Comp == nullptr)continue;
									AddEntry(Comp, FText::Format(LOCTEXT("PromoteAsBehaviour", "As {0} ({1})"),
										Comp->GetClass()->GetDisplayNameText(), FText::FromString(Comp->GetName())));
								}
								if (auto Visual = W->GetVisual())
								{
									AddEntry(Visual, FText::Format(LOCTEXT("PromoteAsVisual", "As {0} (Visual)"), Visual->GetClass()->GetDisplayNameText()));
								}
								SubMenu.AddSeparator();
								AddEntry(W, FText::Format(LOCTEXT("PromoteAsWidget", "As Widget ({0})"), W->GetClass()->GetDisplayNameText()));
							}));

						MenuBuilder.AddSubMenu(
							LOCTEXT("AddEventSubMenu", "Add Event Handler"),
							LOCTEXT("AddEventSubMenuTooltip", "Ask the primary Behaviour's editor backend to generate and bind a compatible handler."),
							FNewMenuDelegate::CreateLambda([WeakEditor = Manager, WeakWidget = TWeakObjectPtr<ULexWidget>(SelectedWidget)](FMenuBuilder& SubMenu)
							{
								ULexWidget* W = WeakWidget.Get();
								if (W == nullptr)return;
								TArray<LexUIPrefabBehaviourUtils::FDiscoveredEvent> Events;
								LexUIPrefabBehaviourUtils::DiscoverEvents(W, Events);
								int32 UnboundEventCount = 0;
								for (const auto& Event : Events)
								{
									if (Event.Component == nullptr || Event.bIsBound)continue;
									++UnboundEventCount;
									SubMenu.AddSubMenu(
										FText::Format(LOCTEXT("AddEventEntry", "{0} ({1})"), FText::FromString(Event.DisplayName), FText::FromString(Event.Component->GetClass()->GetName())),
										FText::Format(LOCTEXT("AddEventEntryTooltip", "Create a handler for {0} and bind it."), FText::FromString(Event.DisplayName)),
										FNewMenuDelegate::CreateLambda([WeakEditor, Event](FMenuBuilder& HandlerMenu)
										{
											auto AddHandlerType = [&HandlerMenu, WeakEditor, Event](const FText& Label,
												const FText& Tooltip, const FSlateIcon& Icon, ELexUIBehaviourHandlerType HandlerType)
											{
												HandlerMenu.AddMenuEntry(Label, Tooltip, Icon,
													FUIAction(FExecuteAction::CreateLambda([WeakEditor, Event, HandlerType]()
													{
														if (TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin())
														{
															Editor->AddEventHandler(Event, HandlerType);
														}
													}), FCanExecuteAction::CreateLambda([WeakEditor, HandlerType]()
													{
														TSharedPtr<FLexUIPrefabEditor> Editor = WeakEditor.Pin();
														return Editor.IsValid() && Editor->CanAddEventHandler(HandlerType);
													})));
											};
											AddHandlerType(LOCTEXT("AddEventFromFunction", "From Function"),
												LOCTEXT("AddEventFromFunctionTooltip", "Create and bind a Blueprint function."),
												FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Function_16x"),
												ELexUIBehaviourHandlerType::Function);
											AddHandlerType(LOCTEXT("AddEventFromEvent", "From Event"),
												LOCTEXT("AddEventFromEventTooltip", "Create and bind a Blueprint custom event."),
												FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.Event_16x"),
												ELexUIBehaviourHandlerType::Event);
										}), false, FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Plus"));
								}
								if (Events.IsEmpty())
								{
									SubMenu.AddMenuEntry(
										LOCTEXT("NoEventsAvailable", "No Events Available"),
										FText::GetEmpty(), FSlateIcon(),
										FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
								}
								else if (UnboundEventCount == 0)
								{
									SubMenu.AddMenuEntry(
										LOCTEXT("AllEventsBound", "All Events Are Bound"),
										LOCTEXT("AllEventsBoundTooltip", "Every event on this widget already has a binding."),
										FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.SuccessWithColor"),
										FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([] { return false; })));
								}
							}));
					}
					MenuBuilder.EndSection();
				}
			}

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
								TArray<const FLexUIControlDescriptor*> Panels;
								FLexUIPrefabEditor::CollectLayoutPanelDescriptors(nullptr, Panels);
								for (const FLexUIControlDescriptor* Descriptor : Panels)
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
					}
					MenuBuilder.EndSection();
				}
			}

			// UMG "Replace With": swap the panel that arranges this widget's children.
			if (auto Editor = Manager.Pin())
			{
				const TArray<TWeakObjectPtr<ULexWidget>>& Selection = Editor->GetSelectedWidgets();
				ULexWidget* Target = Selection.Num() == 1 ? Selection[0].Get() : nullptr;
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
									const TArray<TWeakObjectPtr<ULexWidget>>& Sel = E->GetSelectedWidgets();
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
								TArray<const FLexUIControlDescriptor*> Panels;
								FLexUIPrefabEditor::CollectLayoutPanelDescriptors(Current, Panels);
								for (const FLexUIControlDescriptor* Descriptor : Panels)
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

			// UMG-toolbar-style Align / Distribute for a multi-widget selection
			if (auto Editor = Manager.Pin())
			{
				if (Editor->GetSelectedWidgets().Num() >= 2)
				{
					MenuBuilder.BeginSection("AlignDistribute", LOCTEXT("AlignDistribute", "Align"));
					{
						MenuBuilder.AddSubMenu(
							LOCTEXT("AlignSubMenu", "Align"),
							LOCTEXT("AlignSubMenuTooltip", "Line the selected sibling widgets up along an edge or center (they must share a parent)."),
							FNewMenuDelegate::CreateLambda([WeakEditor = Manager](FMenuBuilder& SubMenu)
							{
								auto AddAlign = [&SubMenu, WeakEditor](const FText& Label, ELexUIWidgetAlignType Type)
								{
									SubMenu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
										FUIAction(FExecuteAction::CreateLambda([WeakEditor, Type]()
										{
											if (auto E = WeakEditor.Pin())E->AlignSelectedWidgets(Type);
										})));
								};
								AddAlign(LOCTEXT("AlignLeft", "Left Edges"), ELexUIWidgetAlignType::LeftEdge);
								AddAlign(LOCTEXT("AlignCenterH", "Horizontal Centers"), ELexUIWidgetAlignType::HorizontalCenter);
								AddAlign(LOCTEXT("AlignRight", "Right Edges"), ELexUIWidgetAlignType::RightEdge);
								SubMenu.AddSeparator();
								AddAlign(LOCTEXT("AlignTop", "Top Edges"), ELexUIWidgetAlignType::TopEdge);
								AddAlign(LOCTEXT("AlignCenterV", "Vertical Centers"), ELexUIWidgetAlignType::VerticalCenter);
								AddAlign(LOCTEXT("AlignBottom", "Bottom Edges"), ELexUIWidgetAlignType::BottomEdge);
							}));

						if (Editor->GetSelectedWidgets().Num() >= 3)
						{
							MenuBuilder.AddSubMenu(
								LOCTEXT("DistributeSubMenu", "Distribute"),
								LOCTEXT("DistributeSubMenuTooltip", "Even out the gaps between the selected sibling widgets (keeps the two outermost fixed)."),
								FNewMenuDelegate::CreateLambda([WeakEditor = Manager](FMenuBuilder& SubMenu)
								{
									SubMenu.AddMenuEntry(LOCTEXT("DistributeH", "Horizontally"), FText::GetEmpty(), FSlateIcon(),
										FUIAction(FExecuteAction::CreateLambda([WeakEditor]() { if (auto E = WeakEditor.Pin())E->DistributeSelectedWidgets(true); })));
									SubMenu.AddMenuEntry(LOCTEXT("DistributeV", "Vertically"), FText::GetEmpty(), FSlateIcon(),
										FUIAction(FExecuteAction::CreateLambda([WeakEditor]() { if (auto E = WeakEditor.Pin())E->DistributeSelectedWidgets(false); })));
								}));
						}
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
					FUIAction(FExecuteAction::CreateSP(this, &SLexWidgetEditorHierarchyView::SetAllExpansion, false)));
				MenuBuilder.AddMenuEntry(LOCTEXT("ExpandAll", "Expand All"),
					LOCTEXT("ExpandAllTooltip", "Expand every widget in the tree."), FSlateIcon(),
					FUIAction(FExecuteAction::CreateSP(this, &SLexWidgetEditorHierarchyView::SetAllExpansion, true)));
			}
			MenuBuilder.EndSection();
			// Last, so a project's entries read as additions rather than interleaving with ours.
			if (auto Editor = Manager.Pin())FLGUIEditorModule::Get().ExtendWidgetContextMenu(MenuBuilder, Editor.ToSharedRef());
	});
}

TSharedPtr<SWidget> SLexWidgetEditorHierarchyView::BuildContextMenu()
{
	return OnContextMenuOpening();
}

void SLexWidgetEditorHierarchyView::GetExpandWidgets(TSet<TWeakObjectPtr<ULexWidget>>& OutExpandWidgets)
{
	WidgetTreeView->GetExpandedItems(OutExpandWidgets);
}

void SLexWidgetEditorHierarchyView::UpdateItemsExpansionFromModel()
{
	for (auto Widget: RootWidgets)
	{
		RecursiveExpand(Widget.Get(), EExpandBehavior::FromModel);
	}
}

void SLexWidgetEditorHierarchyView::RestoreItemsExpansion()
{
	for (auto Widget : RootWidgets)
	{
		RecursiveExpand(Widget.Get(), EExpandBehavior::RestoreFromPrevious);
	}
}

void SLexWidgetEditorHierarchyView::SaveItemsExpansion()
{
	ExpandedItemNames.Empty();

	if (WidgetTreeView.IsValid())
	{
		TSet< TWeakObjectPtr<ULexWidget> > ExpandedItems;
		WidgetTreeView->GetExpandedItems(ExpandedItems);

		for (TWeakObjectPtr<ULexWidget> Item : ExpandedItems)
		{
			if (Item.IsValid())
			{
				ExpandedItemNames.Add(Item->GetName());
			}
		}
	}
}
void SLexWidgetEditorHierarchyView::RecursiveExpand(ULexWidget* Widget, EExpandBehavior ExpandBehavior)
{
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
			if (auto ValuePtr = ExpansionMap.Find(Widget))
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

void SLexWidgetEditorHierarchyView::SetItemExpansionRecursive(TWeakObjectPtr<ULexWidget> Model, bool bInExpansionState)
{
	if (Model.IsValid())
	{
		RecursiveExpand(Model.Get(), bInExpansionState ? EExpandBehavior::AlwaysExpand : EExpandBehavior::NeverExpand);
	}
}

void SLexWidgetEditorHierarchyView::SetAllExpansion(bool bExpand)
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

UE_ENABLE_OPTIMIZATION

#undef LOCTEXT_NAMESPACE
