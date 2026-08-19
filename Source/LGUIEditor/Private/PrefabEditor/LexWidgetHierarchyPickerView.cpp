// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexWidgetHierarchyPickerView.h"
#include "LexWidgetHierarchyPickerViewItem.h"
#include "Core/LexUIManager.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Core/Components/LexWidget.h"

#define LOCTEXT_NAMESPACE "LexWidgetHierarchyPickerView"

void SLexWidgetHierarchyPickerView::Construct(const FArguments& InArgs, UWorld* InPrefabWorld, UClass* InObjectClass, ULexWidget* InRootWidget)
{
	PrefabWorld = InPrefabWorld;
	OnSelectItem = InArgs._OnSelectItem;
	ObjectClass = InObjectClass;
	SpecificRootWidget = InRootWidget;

	SearchBoxWidgetFilter = MakeShareable(new WidgetTextFilter(WidgetTextFilter::FItemToStringArray::CreateSP(this, &SLexWidgetHierarchyPickerView::GetWidgetFilterStrings)));

	FilterHandler = MakeShareable(new TreeFilterHandler<DataType>());
	FilterHandler->SetFilter(SearchBoxWidgetFilter.Get());
	FilterHandler->SetRootItems(&RootWidgets, &TreeRootWidgets);
	FilterHandler->SetGetChildrenDelegate(TreeFilterHandler< DataType >::FOnGetChildren::CreateRaw(this, &SLexWidgetHierarchyPickerView::OnGetChildren));

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(4)
		.AutoHeight()
		[
			SAssignNew(SearchBoxPtr, SSearchBox)
			.HintText(LOCTEXT("SearchWidgets", "Search Widgets"))
			.OnTextChanged(this, &SLexWidgetHierarchyPickerView::OnSearchChanged)
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
SLexWidgetHierarchyPickerView::~SLexWidgetHierarchyPickerView()
{
}
void SLexWidgetHierarchyPickerView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshRequested)
	{
		RebuildTreeView();
		RefreshTree();
		UpdateItemsExpansionFromModel();
		bRefreshRequested = false;
	}
}

void SLexWidgetHierarchyPickerView::RefreshImmediately()
{
	RebuildTreeView();
	RefreshTree();
	UpdateItemsExpansionFromModel();
}
namespace
{
	void CollectValidObjectsRecursive(TSharedPtr<FLexWidgetHierarchyPickerView_DataItem> InParent, UClass* InObjectClass)
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
			auto ChildData = MakeShared<FLexWidgetHierarchyPickerView_DataItem>(ChildWidget->GetDisplayName(), ChildWidget);
			CollectValidObjectsRecursive(ChildData, InObjectClass);
			if (ChildData->bContainsValidObject)
			{
				InParent->bContainsValidObject = true;
				InParent->Children.Add(ChildData);
			}
		}
	}
}

void LexWidgetHierarchyPicker_BuildRoots(const TArray<ULexWidget*>& InRootWidgets, UClass* InObjectClass
	, TArray<TSharedPtr<FLexWidgetHierarchyPickerView_DataItem>>& OutRoots)
{
	OutRoots.Reset();
	if (InObjectClass == nullptr)return;
	for (ULexWidget* RootWidget : InRootWidgets)
	{
		if (!IsValid(RootWidget))continue;
		auto RootData = MakeShared<FLexWidgetHierarchyPickerView_DataItem>(RootWidget->GetDisplayName(), RootWidget);
		CollectValidObjectsRecursive(RootData, InObjectClass);
		OutRoots.Add(RootData);
	}
}

void SLexWidgetHierarchyPickerView::RefreshTree()
{
	TArray<ULexWidget*> Roots;
	if (SpecificRootWidget)
	{
		Roots.Add(SpecificRootWidget);
	}
	else
	{
		if (auto LexUIManager = ULexUIManagerWorldSubsystem::GetInstance(PrefabWorld.Get()))
		{
			for (auto& Widget : LexUIManager->GetAllWidgetArray())
			{
				if (Widget->IsRootWidgetInHierarchy())
				{
					Roots.Add(Widget);
				}
			}
		}
	}

	// A picker opened on a world holding no LexUI root is a legitimate state -- the first Tick used
	// to reach for RootWidgets[0] and assert instead of showing an empty tree. BuildRoots resets
	// the output and is a no-op on an empty input, so that case needs no branch of its own; giving
	// it one only put the empty path somewhere the helper's tests could not reach.
	LexWidgetHierarchyPicker_BuildRoots(Roots, ObjectClass, RootWidgets);

	FilterHandler->RefreshAndFilterTree();
}
void SLexWidgetHierarchyPickerView::RebuildTreeView()
{
	float OldScrollOffset = 0;
	if (WidgetTreeView.IsValid())
	{
		OldScrollOffset = WidgetTreeView->GetScrollOffset();
	}

	SAssignNew(WidgetTreeView, STreeView<DataType>)
		.SelectionMode(ESelectionMode::Single)
		.OnGetChildren(FilterHandler.ToSharedRef(), &TreeFilterHandler< DataType>::OnGetFilteredChildren)
		.OnGenerateRow(this, &SLexWidgetHierarchyPickerView::OnGenerateRow)
		.OnSelectionChanged(this, &SLexWidgetHierarchyPickerView::OnSelectionChanged)
		.OnSetExpansionRecursive(this, &SLexWidgetHierarchyPickerView::SetItemExpansionRecursive)
		.SelectionMode(ESelectionMode::Type::Single)
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

TSharedRef< ITableRow > SLexWidgetHierarchyPickerView::OnGenerateRow(DataType InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SLexWidgetHierarchyPickerViewItem, OwnerTable, InItem, ObjectClass)
		.OnSelectObject(OnSelectItem)
		.IsEnabled(InItem->ValidObjectArray.Num() > 0)
		;
}
void SLexWidgetHierarchyPickerView::OnSelectionChanged(DataType SelectedItem, ESelectInfo::Type SelectInfo)
{
	
}
void SLexWidgetHierarchyPickerView::OnGetChildren(DataType InParent, TArray< DataType >& OutChildren)
{
	OutChildren = InParent->Children;
}
void SLexWidgetHierarchyPickerView::GetWidgetFilterStrings(DataType Item, TArray<FString>& OutStrings)
{
	OutStrings.Add(Item->DisplayText);
}
void SLexWidgetHierarchyPickerView::OnSearchChanged(const FText& InFilterText)
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
void SLexWidgetHierarchyPickerView::UpdateItemsExpansionFromModel()
{
	for (auto Widget: RootWidgets)
	{
		RecursiveExpand(Widget, true);
	}
}
void SLexWidgetHierarchyPickerView::RecursiveExpand(DataType Model, bool bInExpansionState)
{
	WidgetTreeView->SetItemExpansion(Model, bInExpansionState);

	for (auto Child: Model->Children)
	{
		RecursiveExpand(Child, bInExpansionState);
	}
}
void SLexWidgetHierarchyPickerView::SetItemExpansionRecursive(DataType Model, bool bInExpansionState)
{
	if (Model.IsValid())
	{
		// Shift-click asks for a state, not for "expand": ignoring it left a deep subtree here with
		// no way to collapse again, which is the behaviour the main outliner already gets right.
		RecursiveExpand(Model, bInExpansionState);
	}
}
#undef LOCTEXT_NAMESPACE