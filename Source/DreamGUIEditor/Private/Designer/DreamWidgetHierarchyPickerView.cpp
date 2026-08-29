// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "DreamWidgetHierarchyPickerView.h"
#include "DreamWidgetHierarchyPickerViewItem.h"
#include "Core/DreamUIManager.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Core/Components/DreamWidget.h"

#define LOCTEXT_NAMESPACE "DreamWidgetHierarchyPickerView"

void SDreamWidgetHierarchyPickerView::Construct(const FArguments& InArgs, UWorld* InDesignerWorld, UClass* InObjectClass, UDreamWidget* InRootWidget)
{
	DesignerWorld = InDesignerWorld;
	OnSelectItem = InArgs._OnSelectItem;
	ObjectClass = InObjectClass;
	SpecificRootWidget = InRootWidget;

	SearchBoxWidgetFilter = MakeShareable(new WidgetTextFilter(WidgetTextFilter::FItemToStringArray::CreateSP(this, &SDreamWidgetHierarchyPickerView::GetWidgetFilterStrings)));

	FilterHandler = MakeShareable(new TreeFilterHandler<DataType>());
	FilterHandler->SetFilter(SearchBoxWidgetFilter.Get());
	FilterHandler->SetRootItems(&RootWidgets, &TreeRootWidgets);
	FilterHandler->SetGetChildrenDelegate(TreeFilterHandler< DataType >::FOnGetChildren::CreateRaw(this, &SDreamWidgetHierarchyPickerView::OnGetChildren));

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(4)
		.AutoHeight()
		[
			SAssignNew(SearchBoxPtr, SSearchBox)
			.HintText(LOCTEXT("SearchWidgets", "Search Widgets"))
			.OnTextChanged(this, &SDreamWidgetHierarchyPickerView::OnSearchChanged)
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
}
SDreamWidgetHierarchyPickerView::~SDreamWidgetHierarchyPickerView()
{
}
void SDreamWidgetHierarchyPickerView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshRequested)
	{
		RebuildTreeView();
		RefreshTree();
		UpdateItemsExpansionFromModel();
		bRefreshRequested = false;
	}
}

void SDreamWidgetHierarchyPickerView::RefreshImmediately()
{
	RebuildTreeView();
	RefreshTree();
	UpdateItemsExpansionFromModel();
}
namespace
{
	void CollectValidObjectsRecursive(TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem> InParent, UClass* InObjectClass)
	{
		if (InParent->Widget->IsA(InObjectClass))
		{
			InParent->ValidObjectArray.Add(InParent->Widget);
			InParent->bContainsValidObject = true;
		}

		ForEachObjectWithOuter(InParent->Widget.Get(), [=](UObject* SubObject)
		{
			if (SubObject->IsA(InObjectClass))
			{
				InParent->ValidObjectArray.Add(SubObject);
				InParent->bContainsValidObject = true;
			}
		});

		auto WidgetChildren = InParent->Widget->GetChildren();
		for (int i = 0; i < WidgetChildren.Num(); i++)
		{
			auto ChildWidget = WidgetChildren[i];
			auto ChildData = MakeShared<FDreamWidgetHierarchyPickerView_DataItem>(ChildWidget->GetDisplayName(), ChildWidget);
			CollectValidObjectsRecursive(ChildData, InObjectClass);
			if (ChildData->bContainsValidObject)
			{
				InParent->bContainsValidObject = true;
				InParent->Children.Add(ChildData);
			}
		}
	}
}

void DreamWidgetHierarchyPicker_BuildRoots(const TArray<UDreamWidget*>& InRootWidgets, UClass* InObjectClass
	, TArray<TSharedPtr<FDreamWidgetHierarchyPickerView_DataItem>>& OutRoots)
{
	OutRoots.Reset();
	if (InObjectClass == nullptr)return;
	for (UDreamWidget* RootWidget : InRootWidgets)
	{
		if (!IsValid(RootWidget))continue;
		auto RootData = MakeShared<FDreamWidgetHierarchyPickerView_DataItem>(RootWidget->GetDisplayName(), RootWidget);
		CollectValidObjectsRecursive(RootData, InObjectClass);
		OutRoots.Add(RootData);
	}
}

void SDreamWidgetHierarchyPickerView::RefreshTree()
{
	TArray<UDreamWidget*> Roots;
	if (SpecificRootWidget)
	{
		Roots.Add(SpecificRootWidget);
	}
	else
	{
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(DesignerWorld.Get()))
		{
			for (auto& Widget : DreamUIManager->GetAllWidgetArray())
			{
				if (Widget->IsRootWidgetInHierarchy())
				{
					Roots.Add(Widget);
				}
			}
		}
	}

	// A picker opened on a world holding no DreamUI root is a legitimate state -- the first Tick used
	// to reach for RootWidgets[0] and assert instead of showing an empty tree. BuildRoots resets
	// the output and is a no-op on an empty input, so that case needs no branch of its own; giving
	// it one only put the empty path somewhere the helper's tests could not reach.
	DreamWidgetHierarchyPicker_BuildRoots(Roots, ObjectClass, RootWidgets);

	FilterHandler->RefreshAndFilterTree();
}
void SDreamWidgetHierarchyPickerView::RebuildTreeView()
{
	float OldScrollOffset = 0;
	if (WidgetTreeView.IsValid())
	{
		OldScrollOffset = WidgetTreeView->GetScrollOffset();
	}

	SAssignNew(WidgetTreeView, STreeView<DataType>)
		.SelectionMode(ESelectionMode::Single)
		.OnGetChildren(FilterHandler.ToSharedRef(), &TreeFilterHandler< DataType>::OnGetFilteredChildren)
		.OnGenerateRow(this, &SDreamWidgetHierarchyPickerView::OnGenerateRow)
		.OnSelectionChanged(this, &SDreamWidgetHierarchyPickerView::OnSelectionChanged)
		.OnSetExpansionRecursive(this, &SDreamWidgetHierarchyPickerView::SetItemExpansionRecursive)
		.SelectionMode(ESelectionMode::Type::Single)
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

TSharedRef< ITableRow > SDreamWidgetHierarchyPickerView::OnGenerateRow(DataType InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SDreamWidgetHierarchyPickerViewItem, OwnerTable, InItem, ObjectClass)
		.OnSelectObject(OnSelectItem)
		.IsEnabled(InItem->ValidObjectArray.Num() > 0)
		;
}
void SDreamWidgetHierarchyPickerView::OnSelectionChanged(DataType SelectedItem, ESelectInfo::Type SelectInfo)
{
	
}
void SDreamWidgetHierarchyPickerView::OnGetChildren(DataType InParent, TArray< DataType >& OutChildren)
{
	OutChildren = InParent->Children;
}
void SDreamWidgetHierarchyPickerView::GetWidgetFilterStrings(DataType Item, TArray<FString>& OutStrings)
{
	OutStrings.Add(Item->DisplayText);
}
void SDreamWidgetHierarchyPickerView::OnSearchChanged(const FText& InFilterText)
{
	bRefreshRequested = true;
	const bool bFilteringEnabled = !InFilterText.IsEmpty();
	if (bFilteringEnabled != FilterHandler->GetIsEnabled())
	{
		FilterHandler->SetIsEnabled(bFilteringEnabled);
	}
	SearchBoxWidgetFilter->SetRawFilterText(InFilterText);
	SearchBoxPtr->SetError(SearchBoxWidgetFilter->GetFilterErrorText());
}
void SDreamWidgetHierarchyPickerView::UpdateItemsExpansionFromModel()
{
	for (auto Widget: RootWidgets)
	{
		RecursiveExpand(Widget, true);
	}
}
void SDreamWidgetHierarchyPickerView::RecursiveExpand(DataType Model, bool bInExpansionState)
{
	WidgetTreeView->SetItemExpansion(Model, bInExpansionState);

	for (auto Child: Model->Children)
	{
		RecursiveExpand(Child, bInExpansionState);
	}
}
void SDreamWidgetHierarchyPickerView::SetItemExpansionRecursive(DataType Model, bool bInExpansionState)
{
	if (Model.IsValid())
	{
		// Shift-click asks for a state, not for "expand": ignoring it left a deep subtree here with
		// no way to collapse again, which is the behaviour the main outliner already gets right.
		RecursiveExpand(Model, bInExpansionState);
	}
}
#undef LOCTEXT_NAMESPACE