// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLexWidgetEditorHierarchyViewItem.h"
#include "SLexWidgetEditorHierarchyView.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Styling/CoreStyle.h"
#include "LGUIPrefabEditor.h"
#include "LGUIHeaders.h"
#include "ScopedTransaction.h"
#include "EditorFontGlyphs.h"
#include "Editor.h"
#include "LGUIEditorModule.h"
#include "DragAndDrop/AssetDragDropOp.h"

#define LOCTEXT_NAMESPACE "LexWidgetEditorHierarchyViewItem"

class FHierarchyLexWidgetBlueprintDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FHierarchyLexWidgetDragDropOp, FDecoratedDragDropOp)

		virtual ~FHierarchyLexWidgetBlueprintDragDropOp();

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;

	bool HasOriginatedFrom(const TSharedPtr<FLGUIPrefabEditor>& Manager)
	{
		for (const FItem& Item : DraggedWidgets)
		{
			if (!Item.Widget->IsAttachedTo(Manager->GetLoadedRootActor()->GetRootComponent()))
			{
				return false;
			}
		}
		return true;
	}

	struct FItem
	{
		/** The widget being dragged and dropped */
		ULexWidget* Widget = nullptr;

		/** The original parent of the widget. */
		ULexWidget* WidgetParent = nullptr;
	};

	TArray<FItem> DraggedWidgets;

	/** The widget being dragged and dropped */
	FScopedTransaction* Transaction;

	/** Constructs a new drag/drop operation */
	static TSharedRef<FHierarchyLexWidgetBlueprintDragDropOp> New(TSharedPtr<FLGUIPrefabEditor> Blueprint, const TArray<ULexWidget*>& InWidgets);
};

TSharedRef<FHierarchyLexWidgetBlueprintDragDropOp> FHierarchyLexWidgetBlueprintDragDropOp::New(TSharedPtr<FLGUIPrefabEditor> Blueprint, const TArray<ULexWidget*>& InWidgets)
{
	check(InWidgets.Num() > 0);

	TSharedRef<FHierarchyLexWidgetBlueprintDragDropOp> Operation = MakeShareable(new FHierarchyLexWidgetBlueprintDragDropOp());

	// Set the display text and the transaction name based on whether we're dragging a single or multiple widgets
	if (InWidgets.Num() == 1)
	{
		Operation->CurrentHoverText = Operation->DefaultHoverText = FText::FromString(InWidgets[0]->GetOwner()->GetActorLabel());
		Operation->Transaction = new FScopedTransaction(LOCTEXT("MoveWidget", "Change Hierarchy"));
	}
	else
	{
		Operation->CurrentHoverText = Operation->DefaultHoverText = LOCTEXT("DragMultipleWidgets", "Multiple Widgets");
		Operation->Transaction = new FScopedTransaction(LOCTEXT("MoveWidgets", "Change Hierarchy"));
	}

	// Add an FItem for each widget in the drag operation
	for (const auto& Widget : InWidgets)
	{
		FItem DraggedWidget;

		DraggedWidget.Widget = Widget;

		Widget->Modify();

		DraggedWidget.WidgetParent = Widget->GetUIParent();
		if (DraggedWidget.WidgetParent)
		{
			DraggedWidget.WidgetParent->Modify();
		}

		Operation->DraggedWidgets.Add(DraggedWidget);
	}

	Operation->Construct();

	return Operation;
}

FHierarchyLexWidgetBlueprintDragDropOp::~FHierarchyLexWidgetBlueprintDragDropOp()
{
	delete Transaction;
}

void FHierarchyLexWidgetBlueprintDragDropOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	if (!bDropWasHandled)
	{
		Transaction->Cancel();
	}
}



TOptional<EItemDropZone> ProcessHierarchyDragDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, bool bIsDrop, TSharedPtr<FLGUIPrefabEditor> Manager, ULexWidget* TargetItem, TOptional<int32> Index = TOptional<int32>())
{
	auto TargetTemplate = TargetItem;

	//if (TSharedPtr<FHierarchyLexWidgetDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FHierarchyLexWidgetDragDropOp>())
	//{
	//	if (!HierarchyDragDropOp->HasOriginatedFrom(Manager))
	//	{
	//		return TOptional<EItemDropZone>();
	//	}
	//}

	// We do not support to dragging a Widget from the Viewport to the Hierarchy panel
	//if (TSharedPtr<FSelectedWidgetDragDropOp> SelectedWidgetDragDropOp = DragDropEvent.GetOperationAs<FSelectedWidgetDragDropOp>())
	//{
	//	return TOptional<EItemDropZone>();
	//}

	if (TargetTemplate && (DropZone == EItemDropZone::AboveItem || DropZone == EItemDropZone::BelowItem))
	{
		if (auto TargetParentTemplate = TargetTemplate->GetUIParent())
		{
			int32 InsertIndex = TargetParentTemplate->GetIndexOfUIChild(TargetTemplate);
			InsertIndex += (DropZone == EItemDropZone::AboveItem) ? 0 : 1;
			InsertIndex = FMath::Clamp(InsertIndex, 0, TargetParentTemplate->GetUIChildren().Num());

			TOptional<EItemDropZone> ParentZone = ProcessHierarchyDragDrop(DragDropEvent, EItemDropZone::OntoItem, bIsDrop, Manager, TargetParentTemplate, InsertIndex);
			if (ParentZone.IsSet())
			{
				return DropZone;
			}
			else
			{
				DropZone = EItemDropZone::OntoItem;
			}
		}
	}
	else
	{
		DropZone = EItemDropZone::OntoItem;
	}

	//drag/drop from content to create new widget
	TSharedPtr<FDragDropOperation> DragDropOp = DragDropEvent.GetOperation();
	if (DragDropOp.IsValid() && !DragDropOp->IsOfType<FHierarchyLexWidgetBlueprintDragDropOp>())
	{
		if (bIsDrop)
		{
			if (DragDropOp->IsOfType<FAssetDragDropOp>())
			{
				Manager->TryHandleAssetDragDropOperation(DragDropEvent);
			}
		}
		return EItemDropZone::OntoItem;
	}

	TSharedPtr<FHierarchyLexWidgetBlueprintDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FHierarchyLexWidgetBlueprintDragDropOp>();
	if (HierarchyDragDropOp.IsValid())
	{
		HierarchyDragDropOp->ResetToDefaultToolTip();

		// If the target item is valid we're dealing with a normal widget in the hierarchy, otherwise we should assume it's
		// the null case and we should be adding it as the root widget.
		if (TargetItem)
		{
			const bool bIsDraggedObject = HierarchyDragDropOp->DraggedWidgets.ContainsByPredicate([TargetItem](const FHierarchyLexWidgetBlueprintDragDropOp::FItem& DraggedItem)
				{
					return DraggedItem.Widget == TargetItem;
				});
			const bool bIsChildOfDraggedObject = HierarchyDragDropOp->DraggedWidgets.ContainsByPredicate([TargetItem](const FHierarchyLexWidgetBlueprintDragDropOp::FItem& DraggedItem)
				{
					return TargetItem->IsAttachedTo(DraggedItem.Widget);
				});

			if (bIsDraggedObject || bIsChildOfDraggedObject)
			{
				HierarchyDragDropOp->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				return TOptional<EItemDropZone>();
			}

			auto* NewParent = TargetItem;

			if (bIsDrop)
			{
				NewParent->SetFlags(RF_Transactional);
				NewParent->Modify();

				for (const auto& DraggedWidget : HierarchyDragDropOp->DraggedWidgets)
				{
					auto TemplateWidget = DraggedWidget.Widget;
					TemplateWidget->SetFlags(RF_Transactional);
					TemplateWidget->Modify();

					if (Index.IsSet())
					{
						// If we're inserting at an index, and the widget we're moving is already
						// in the hierarchy before the point we're moving it to, we need to reduce the index
						// count by one, because the whole set is about to be shifted when it's removed.
						const bool bInsertInSameParent = TemplateWidget->GetUIParent() == NewParent;
						const bool bNeedToDropIndex = NewParent->GetIndexOfUIChild(TemplateWidget) < Index.GetValue();

						if (bInsertInSameParent && bNeedToDropIndex)
						{
							Index = Index.GetValue() - 1;
						}
					}

					TemplateWidget->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

					if (Index.IsSet())
					{
						TemplateWidget->AttachToComponent(NewParent, FAttachmentTransformRules::KeepWorldTransform);
						TemplateWidget->SetSiblingIndex(Index.GetValue());
					}
					else
					{
						TemplateWidget->AttachToComponent(NewParent, FAttachmentTransformRules::KeepWorldTransform);
					}
				}
			}

			HierarchyDragDropOp->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK"));
			return EItemDropZone::OntoItem;
		}
		else
		{
			HierarchyDragDropOp->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
			HierarchyDragDropOp->CurrentHoverText = LOCTEXT("CantHaveChildren", "Widget can't have children.");
		}

		return TOptional<EItemDropZone>();
	}

	return TOptional<EItemDropZone>();
}



void SLexWidgetEditorHierarchyViewItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakObjectPtr<ULexWidget> InModel, TSharedPtr<SLexWidgetEditorHierarchyView> InHierarchyView, TSharedPtr<FLGUIPrefabEditor> InManager)
{
	Widget = InModel;
	MouseEnter = InArgs._MouseEnter;
	MouseExit = InArgs._MouseExit;
	HierarchyView = InHierarchyView;
	Manager = InManager;

	STableRow<TWeakObjectPtr<ULexWidget>>::Construct(
		STableRow<TWeakObjectPtr<ULexWidget>>::FArguments()
		.OnCanAcceptDrop(this, &SLexWidgetEditorHierarchyViewItem::HandleCanAcceptDrop)
		.OnAcceptDrop(this, &SLexWidgetEditorHierarchyViewItem::HandleAcceptDrop)
		.OnDragDetected(this, &SLexWidgetEditorHierarchyViewItem::HandleDragDetected)
		.OnDragEnter(this, &SLexWidgetEditorHierarchyViewItem::HandleDragEnter)
		.OnDragLeave(this, &SLexWidgetEditorHierarchyViewItem::HandleDragLeave)
		.Padding(0.0f)
		.Content()
		[
			SNew(SHorizontalBox)

				// Widget icon
				//+ SHorizontalBox::Slot()
				//.AutoWidth()
				//.VAlign(VAlign_Center)
				//[
				//	SNew(SImage)
				//	.ColorAndOpacity(FSlateColor::UseForeground())
				//	.Image(Model->GetImage())
				//	.ToolTipText(Model->GetImageToolTipText())
				//]

				// Name of the widget
				+SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(2, 0, 0, 0)
				.VAlign(VAlign_Center)
				[
					SAssignNew(EditBox, SInlineEditableTextBlock)
						//.Font(this, &SHierarchyViewItem::GetItemFont)
						.Text(this, &SLexWidgetEditorHierarchyViewItem::GetItemText)
						.ToolTipText(this, &SLexWidgetEditorHierarchyViewItem::GetItemTooltipText)
						.ColorAndOpacity(this, &SLexWidgetEditorHierarchyViewItem::GetItemColorAndOpacity)
						.IsReadOnly(this, &SLexWidgetEditorHierarchyViewItem::IsReadOnly)
						.OnEnterEditingMode(this, &SLexWidgetEditorHierarchyViewItem::OnBeginNameTextEdit)
						.OnExitEditingMode(this, &SLexWidgetEditorHierarchyViewItem::OnEndNameTextEdit)
						.OnVerifyTextChanged(this, &SLexWidgetEditorHierarchyViewItem::OnVerifyNameTextChanged)
						.OnTextCommitted(this, &SLexWidgetEditorHierarchyViewItem::OnNameTextCommited)
						.IsSelected(this, &SLexWidgetEditorHierarchyViewItem::IsSelectedExclusively)
				]

				// Locked Icon
				//+ SHorizontalBox::Slot()
				//.AutoWidth()
				//.VAlign(VAlign_Center)
				//[
				//	SNew(SButton)
				//	.ContentPadding(FMargin(3, 1))
				//	.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				//	.ForegroundColor(FCoreStyle::Get().GetSlateColor("Foreground"))
				//	.OnClicked(this, &SHierarchyViewItem::OnToggleLockedInDesigner)
				//	.Visibility(Model->CanControlLockedInDesigner() ? EVisibility::Visible : EVisibility::Hidden)
				//	.ToolTipText(LOCTEXT("WidgetLockedButtonToolTip", "Locks or Unlocks this widget and all children.  Locking a widget prevents it from being selected in the designer view by clicking on them.\n\nHolding [Shift] will only affect this widget and no children."))
				//	.HAlign(HAlign_Center)
				//	.VAlign(VAlign_Center)
				//	[
				//		SNew(SBox)
				//		.MinDesiredWidth(12.0f)
				//		.HAlign(HAlign_Left)
				//		[
				//			SNew(STextBlock)
				//			.Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
				//			.Text(this, &SHierarchyViewItem::GetLockBrushForWidget)
				//		]
				//	]
				//]

				// Visibility icon
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
						.ContentPadding(FMargin(3, 1))
						.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
						.ForegroundColor(FCoreStyle::Get().GetSlateColor("Foreground"))
						.OnClicked(this, &SLexWidgetEditorHierarchyViewItem::OnToggleVisibility)
						.ToolTipText(LOCTEXT("WidgetVisibilityButtonToolTip", "Toggle Widget's Editor Visibility"))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
								.Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
								.Text(this, &SLexWidgetEditorHierarchyViewItem::GetVisibilityBrushForWidget)
						]
				]
		],
		InOwnerTableView);
}

void SLexWidgetEditorHierarchyViewItem::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	MouseEnter.ExecuteIfBound();
	STableRow< TWeakObjectPtr<ULexWidget> >::OnMouseEnter(MyGeometry, MouseEvent);
}
void SLexWidgetEditorHierarchyViewItem::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	MouseExit.ExecuteIfBound();
	STableRow< TWeakObjectPtr<ULexWidget> >::OnMouseLeave(MouseEvent);
}
void SLexWidgetEditorHierarchyViewItem::RequestEditName()
{
	EditBox->EnterEditingMode();
}
bool SLexWidgetEditorHierarchyViewItem::CanRename()
{
	return true;
}

struct LOCAL_SLexWidgetDesignerHierarchyViewViewItem
{
	static FString PrintZone(TOptional<EItemDropZone> Zone)
	{
		if (Zone.IsSet())
		{
			switch (Zone.GetValue())
			{
			default:
			case EItemDropZone::BelowItem:
				return TEXT("BelowItem");
			case EItemDropZone::AboveItem:
				return TEXT("AboveItem");
			case EItemDropZone::OntoItem:
				return TEXT("OntoItem");
			}
		}
		else
		{
			return TEXT("(NotSet)");
		}
	}
};
TOptional<EItemDropZone> SLexWidgetEditorHierarchyViewItem::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<ULexWidget> TargetItem)
{
	TSharedPtr<FDragDropOperation> DragDropOp = DragDropEvent.GetOperation();
	if (DragDropOp.IsValid() && !DragDropOp->IsOfType<FHierarchyLexWidgetBlueprintDragDropOp>())
	{
		if (DragDropOp->IsOfType<FAssetDragDropOp>())
		{
			auto AssetDragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(DragDropOp);
			if (AssetDragDropOp->GetAssets().Num() > 0)
			{
#if 0
				const auto& AssetData = AssetDragDropOp->GetAssets()[0];
				if (AssetData.AssetClassPath == ULGUIPrefab::StaticClass()->GetClassPathName())
				{
					
				}
				FString ClassName = AssetData.GetObjectPathString() + "_C";
				UClass* AssetClass = LoadClass<ULexWidgetScript>(nullptr, *ClassName);
				if (AssetClass != nullptr && AssetClass->IsChildOf<ULexWidgetScript>())
				{
					auto WidgetScript = Widget->GetOwner();
					if (AssetClass == WidgetScript->GetClass())//drag self to self is not allowed
					{
						return TOptional<EItemDropZone>();
					}
				}
#endif
			}
		}
	}
	
	const bool bIsDrop = false;
	if (SupportDrop(HierarchyView.Pin()->DragingItem.Get(), Widget.Get(), DropZone))
	{
		auto Zone = ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, Manager.Pin(), Widget.Get());
		//UE_LOG(LogTemp, Error, TEXT("HandleCanAcceptDrop, %s, zone:%s"), *Model->GetName(), *LOCAL_SLexWidgetDesignerHierarchyViewViewItem::PrintZone(Zone));
		return Zone;
	}
	return TOptional<EItemDropZone>();
}
FReply SLexWidgetEditorHierarchyViewItem::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<ULexWidget> TargetItem)
{
	const bool bIsDrop = true;
	if (SupportDrop(HierarchyView.Pin()->DragingItem.Get(), Widget.Get(), DropZone))
	{
		TOptional<EItemDropZone> Zone = ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, Manager.Pin(), Widget.Get());
		//UE_LOG(LogTemp, Error, TEXT("HandleAcceptDrop, %s, zone:%s"), *Model->GetName(), *LOCAL_SLexWidgetDesignerHierarchyViewViewItem::PrintZone(Zone));
		if (Zone.IsSet())
		{
			HierarchyView.Pin()->RequestRefresh();
			return FReply::Handled();
		}
		else
			return FReply::Unhandled();
	}
	return FReply::Unhandled();
}
FReply SLexWidgetEditorHierarchyViewItem::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	//UE_LOG(LogTemp, Error, TEXT("HandleDragDetected, %s"), *Model->GetName());
	HierarchyView.Pin()->DragingItem = Widget;
	TArray<ULexWidget*> DraggedItems;

	// Dragging multiple items?
	//if (bIsSelected)
	{
		// const auto& SelectedWidgets = Manager.Pin()->GetSelectedWidgets();
		// if (SelectedWidgets.Num() > 1)
		// {
		// 	for (const auto& SelectedWidget : SelectedWidgets)
		// 	{
		// 		DraggedItems.Add(SelectedWidget.Get());
		// 	}
		// }
	}

	if (DraggedItems.Num() == 0)
	{
		DraggedItems.Add(Widget.Get());
	}

	if (DraggedItems.Num() > 0)
	{
		return FReply::Handled().BeginDragDrop(FHierarchyLexWidgetBlueprintDragDropOp::New(Manager.Pin(), DraggedItems));
	}

	return FReply::Handled();
}
void SLexWidgetEditorHierarchyViewItem::HandleDragEnter(FDragDropEvent const& DragDropEvent)
{
	//UE_LOG(LogTemp, Log, TEXT("HandleDragEnter, %s"), *Model->GetName());
}
void SLexWidgetEditorHierarchyViewItem::HandleDragLeave(const FDragDropEvent& DragDropEvent)
{
	//UE_LOG(LogTemp, Log, TEXT("HandleDragLeave, %s"), *Model->GetName());
}

FText SLexWidgetEditorHierarchyViewItem::GetItemText() const
{
	return FText::FromString(Widget->GetOwner()->GetActorLabel());
}

FText SLexWidgetEditorHierarchyViewItem::GetItemTooltipText() const
{
	return FText::Format(LOCTEXT("ItemTooltipFormat", "ID name: {0}\nPath name: {1}"), FText::FromName(Widget->GetFName()), FText::FromString(Widget->GetPathName()));
}

FSlateColor SLexWidgetEditorHierarchyViewItem::GetItemColorAndOpacity() const
{
	if (auto PrefabHelperObject = Manager.Pin()->GetPrefabManagerObject())
	{
		if (PrefabHelperObject->IsActorBelongsToSubPrefab(Widget->GetOwner()))//is sub prefab
		{
			return FLinearColor(FColor(124,171,240, 255));
		}
		else
		{
			if (PrefabHelperObject->IsActorBelongsToMissingSubPrefab(Widget->GetOwner()))
			{
				return FSlateColor(FColor::Red);
			}
		}
	}
	return FSlateColor(FColor(192,192,192,255));
}

bool SLexWidgetEditorHierarchyViewItem::IsReadOnly() const
{
	return false;
}
void SLexWidgetEditorHierarchyViewItem::OnBeginNameTextEdit()
{
	InitialText = FText::FromString(Widget->GetOwner()->GetActorLabel());
}
void SLexWidgetEditorHierarchyViewItem::OnEndNameTextEdit()
{

}
bool SLexWidgetEditorHierarchyViewItem::OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
{
	return true;
}
void SLexWidgetEditorHierarchyViewItem::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
{
	// The model can return nice names "Border_53" becomes [Border] in some cases
	// This check makes sure we don't rename the object internally to that nice name.
	// Most common case would be the user enters edit mode by accident then just moves focus away.
	if (InitialText.EqualToCaseIgnored(InText))
	{
		return;
	}

	GEditor->BeginTransaction(LOCTEXT("ChangeWidgetName_Transaction", "Change Name"));
	Widget->Modify();
	Widget->GetOwner()->SetActorLabel(InText.ToString(), true);
	// FLexUIUtils::NotifyPropertyChanged(Widget->GetOwner(), "ActorLabel");
	// Widget->SetDisplayName(InText.ToString());
	GEditor->EndTransaction();

	HierarchyView.Pin()->RequestRefresh();
}
FReply SLexWidgetEditorHierarchyViewItem::OnToggleVisibility()
{
	GEditor->BeginTransaction(LOCTEXT("ToggleWidgetVisibility_Transaction", "Toggle Visibility"));
	Widget->Modify();
	Widget->SetWidgetActive(!Widget->GetWidgetActive());
	GEditor->EndTransaction();

	return FReply::Handled();
}
FText SLexWidgetEditorHierarchyViewItem::GetVisibilityBrushForWidget() const
{
	return Widget->GetWidgetActiveInHierarchy() ? FEditorFontGlyphs::Eye : FEditorFontGlyphs::Eye_Slash;
}

bool SLexWidgetEditorHierarchyViewItem::SupportDrop(ULexWidget* Dragging, ULexWidget* Current, EItemDropZone DropZone)
{
	if (Current->GetOwner() == Manager.Pin()->GetLoadedRootActor())
	{
		if (DropZone == EItemDropZone::OntoItem)
		{
			return true;
		}
		return false;
	}
	if (Current->IsAttachedTo(Dragging))
	{
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
