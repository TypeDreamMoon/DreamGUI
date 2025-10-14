// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLexWidgetHierarchyPickerView.h"
#include "LGUIPrefabEditor.h"
#include "SLexWidgetHierarchyPickerViewItem.h"
#include "Core/Actor/LexWidgetActor.h"
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
	RootWidgets.Add(MakeShared<SLexWidgetHierarchyPickerView_DataItem>(Manager.Pin()->GetLoadedRootActor()->GetActorLabel(), Manager.Pin()->GetLoadedRootActor()));

	struct LOCAL
	{
		static void CollectChildren(DataType InParent, UClass* InObjectClass)
		{
			if (InParent->Object->IsA(InObjectClass))
			{
				InParent->bContainsValidObject = true;
			}
			TArray< DataType > OutChildren;
			if (auto WidgetActor = Cast<ALexWidgetActor>(InParent->Object))
			{
				auto CompArray = WidgetActor->GetComponents();
				for (auto& Comp : CompArray)
				{
					if(Comp->HasAnyFlags(EObjectFlags::RF_Transient))continue;
					if (Comp->IsVisualizationComponent())continue;
					OutChildren.Add(MakeShared<SLexWidgetHierarchyPickerView_DataItem>(Comp->GetName(), Comp));
				}
				auto& Children = WidgetActor->GetLexWidget()->GetUIChildren();
				for (auto& Child : Children)
				{
					OutChildren.Add(MakeShared<SLexWidgetHierarchyPickerView_DataItem>(Child->GetOwner()->GetActorLabel(), Child->GetOwner()));
				}
			}
			else if (auto Comp = Cast<UActorComponent>(InParent->Object))
			{
				ForEachObjectWithOuter(Comp, [&](UObject* Object)
				{
					if (Object->GetClass()->IsChildOf(InObjectClass))
					{
						OutChildren.Add(MakeShared<SLexWidgetHierarchyPickerView_DataItem>(Object->GetName(), Object));
					}
				});
			}
			for (int i = 0; i < OutChildren.Num(); i++)
			{
				auto Child = OutChildren[i];
				CollectChildren(Child, InObjectClass);
				if (Child->bContainsValidObject)
				{
					InParent->bContainsValidObject = true;
				}
				else
				{
					OutChildren.RemoveAt(i);
					i--;
				}
			}
			InParent->Children = OutChildren;
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
		.IsEnabled(InItem->Object->IsA(ObjectClass))
		;
}
void SLexWidgetHierarchyPickerView::OnSelectionChanged(DataType SelectedItem, ESelectInfo::Type SelectInfo)
{
	if (SelectedItem == nullptr)
	{
		OnSelectItem.ExecuteIfBound(nullptr);
	}
	else
	{
		OnSelectItem.ExecuteIfBound(SelectedItem->Object.Get());
	}
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