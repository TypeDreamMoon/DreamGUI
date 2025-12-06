// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLexWidgetHierarchyPickerView.h"
#include "LGUIPrefabEditor.h"
#include "SLexWidgetHierarchyPickerViewItem.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/Input/SSearchBox.h"
#include "Core/Components/LexRectBlock.h"

#define LOCTEXT_NAMESPACE "LexWidgetHierarchyPickerView"

void SLexWidgetHierarchyPickerView::Construct(const FArguments& InArgs, TSharedPtr<FLGUIPrefabEditor> InManager, UClass* InObjectClass)
{
	Manager = InManager;
	OnSelectItem = InArgs._OnSelectItem;
	ObjectClass = InObjectClass;

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
void SLexWidgetHierarchyPickerView::RefreshTree()
{
	RootWidgets.Empty();
	if (auto RootItem = Cast<ULexWidget>(Manager.Pin()->GetLoadedRootActor()->GetRootComponent()))
	{
		RootWidgets.Add(MakeShared<FLexWidgetHierarchyPickerView_DataItem>(Manager.Pin()->GetLoadedRootActor()->GetActorLabel(), RootItem));
	}

	struct LOCAL
	{
		static void CollectChildren(DataType InParent, UClass* InObjectClass)
		{
			if (InParent->Widget->GetOwner()->IsA(InObjectClass))
			{
				InParent->ValidObjectArray.Add(InParent->Widget->GetOwner());
				InParent->bContainsValidObject = true;
			}
			
			ForEachObjectWithOuter(InParent->Widget->GetOwner(), [=](UObject* SubObject)
			{
				if (SubObject->IsA(InObjectClass))
				{
					InParent->ValidObjectArray.Add(SubObject);
					InParent->bContainsValidObject = true;
				}
			});

			auto WidgetChildren = InParent->Widget->GetUIChildren();
			for (int i = 0; i < WidgetChildren.Num(); i++)
			{
				auto ChildWidget = WidgetChildren[i];
				auto ChildData = MakeShared<FLexWidgetHierarchyPickerView_DataItem>(ChildWidget->GetDisplayName(), ChildWidget);
				CollectChildren(ChildData, InObjectClass);
				if (ChildData->bContainsValidObject)
				{
					InParent->bContainsValidObject = true;
					InParent->Children.Add(ChildData);
				}
			}
		}
	};
	LOCAL::CollectChildren(RootWidgets[0], ObjectClass);

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
	return SNew(SLexWidgetHierarchyPickerViewItem, OwnerTable, InItem, Manager.Pin(), ObjectClass)
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
		RecursiveExpand(Widget);
	}
}
void SLexWidgetHierarchyPickerView::RecursiveExpand(DataType Model)
{
	WidgetTreeView->SetItemExpansion(Model, true);

	for (auto Child: Model->Children)
	{
		RecursiveExpand(Child);
	}
}
void SLexWidgetHierarchyPickerView::SetItemExpansionRecursive(DataType Model, bool bInExpansionState)
{
	if (Model.IsValid())
	{
		RecursiveExpand(Model);
	}
}
#undef LOCTEXT_NAMESPACE