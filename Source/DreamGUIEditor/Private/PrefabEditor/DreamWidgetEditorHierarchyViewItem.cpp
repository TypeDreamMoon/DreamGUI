// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamWidgetEditorHierarchyViewItem.h"

#include "ClassIconFinder.h"
#include "DetailLayoutBuilder.h"
#include "DreamWidgetEditorHierarchyView.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Styling/CoreStyle.h"
#include "DreamUIPrefabEditor.h"
#include "ScopedTransaction.h"
#include "EditorFontGlyphs.h"
#include "Editor.h"
#include "DreamGUIEditorModule.h"
#include "DreamGUIEditorStyle.h"
#include "DreamUIEditorTools.h"
#include "Core/DreamUIManager.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamVisual.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "SDreamUIPrefabPalette.h"//FDreamUIPaletteDragDropOp
#include "PrefabSystem/DreamUIPrefabHelperObject.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "DreamWidgetEditorHierarchyViewItem"


TSharedRef<FHierarchyDreamWidgetDragDropOp> FHierarchyDreamWidgetDragDropOp::New(const TArray<UDreamWidget*>& InWidgets)
{
	check(InWidgets.Num() > 0);

	TSharedRef<FHierarchyDreamWidgetDragDropOp> Operation = MakeShareable(new FHierarchyDreamWidgetDragDropOp());

	// Set the display text and the transaction name based on whether we're dragging a single or multiple widgets
	if (InWidgets.Num() == 1)
	{
		Operation->CurrentHoverText = Operation->DefaultHoverText = FText::FromString(InWidgets[0]->GetDisplayName());
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

		DraggedWidget.WidgetParent = Widget->GetParent();
		if (DraggedWidget.WidgetParent)
		{
			DraggedWidget.WidgetParent->Modify();
		}

		Operation->DraggedWidgets.Add(DraggedWidget);
	}

	Operation->Construct();

	return Operation;
}

FHierarchyDreamWidgetDragDropOp::~FHierarchyDreamWidgetDragDropOp()
{
	delete Transaction;
}

void FHierarchyDreamWidgetDragDropOp::OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent)
{
	if (!bDropWasHandled)
	{
		Transaction->Cancel();
	}
}



const UDreamWidget* DreamWidgetHierarchyDrop::GetLockOwnerForDropZone(const UDreamWidget* TargetItem, EItemDropZone DropZone)
{
	if (TargetItem == nullptr)return nullptr;
	if (DropZone == EItemDropZone::OntoItem)return TargetItem;
	// A row with no parent has no sibling list to insert into, so the drop path turns Above/Below on
	// it into a drop INSIDE it -- which is the row's own lock to refuse, not a parent's it lacks.
	const UDreamWidget* Parent = TargetItem->GetParent();
	return Parent != nullptr ? Parent : TargetItem;
}

TOptional<EItemDropZone> ProcessHierarchyDragDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, bool bIsDrop, TSharedPtr<FDreamUIPrefabEditor> Manager, UDreamWidget* TargetItem, TOptional<int32> Index)
{
	auto TargetTemplate = TargetItem;
	if (TargetTemplate && (DropZone == EItemDropZone::AboveItem || DropZone == EItemDropZone::BelowItem))
	{
		if (auto TargetParentTemplate = TargetTemplate->GetParent())
		{
			int32 InsertIndex = TargetTemplate->GetSiblingIndex();
			InsertIndex += (DropZone == EItemDropZone::AboveItem) ? 0 : 1;
			InsertIndex = FMath::Clamp(InsertIndex, 0, TargetParentTemplate->GetChildren().Num());

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
		else
		{
			DropZone = EItemDropZone::OntoItem;
		}
	}
	else
	{
		DropZone = EItemDropZone::OntoItem;
	}
	// Every branch above can turn a sibling insert into a drop inside the hovered row, and the row
	// the caller asked the lock about was the parent. Ask again against the row it now lands in,
	// which is also what makes the design surface honour the same lock the tree does. Through the
	// interaction gate, so the toolbar's respect-locks switch still reaches the canvas.
	if (const UDreamWidget* LockOwner = DreamWidgetHierarchyDrop::GetLockOwnerForDropZone(TargetItem, DropZone))
	{
		if (Manager.IsValid() && Manager->IsWidgetLockedForInteraction(LockOwner))return TOptional<EItemDropZone>();
	}

	//drag/drop from content to create new widget
	TSharedPtr<FDragDropOperation> DragDropOp = DragDropEvent.GetOperation();
	if (DragDropOp.IsValid() && !DragDropOp->IsOfType<FHierarchyDreamWidgetDragDropOp>())
	{
		if (!IsValid(TargetItem) || !TargetItem->CanAcceptAdditionalChildren())
		{
			return TOptional<EItemDropZone>();
		}
		if (bIsDrop)
		{
			if (DragDropOp->IsOfType<FAssetDragDropOp>() && Manager.IsValid())
			{
				Manager->TryHandleAssetDragDropOperation(DragDropEvent, TargetItem);
			}
			// Palette element dropped onto an Outliner row -> create it under the target widget.
			// Index is set when an above/below drop was rewritten into "under this row's parent",
			// so honouring it is what lets the palette insert between siblings and not only append.
			else if (auto PaletteOp = DragDropEvent.GetOperationAs<FDreamUIPaletteDragDropOp>())
			{
				PaletteOp->CreateUnder(TargetItem, Index);
			}
		}
		return EItemDropZone::OntoItem;
	}

	TSharedPtr<FHierarchyDreamWidgetDragDropOp> HierarchyDragDropOp = DragDropEvent.GetOperationAs<FHierarchyDreamWidgetDragDropOp>();
	if (HierarchyDragDropOp.IsValid())
	{
		HierarchyDragDropOp->ResetToDefaultToolTip();

		// If the target item is valid we're dealing with a normal widget in the hierarchy, otherwise we should assume it's
		// the null case and we should be adding it as the root widget.
		if (TargetItem)
		{
			const bool bIsDraggedObject = HierarchyDragDropOp->DraggedWidgets.ContainsByPredicate([TargetItem](const FHierarchyDreamWidgetDragDropOp::FItem& DraggedItem)
				{
					return DraggedItem.Widget == TargetItem;
				});
			const bool bIsChildOfDraggedObject = HierarchyDragDropOp->DraggedWidgets.ContainsByPredicate([TargetItem](const FHierarchyDreamWidgetDragDropOp::FItem& DraggedItem)
				{
					return TargetItem->IsChildOf(DraggedItem.Widget);
				});

			if (bIsDraggedObject || bIsChildOfDraggedObject)
			{
				HierarchyDragDropOp->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				return TOptional<EItemDropZone>();
			}

			auto* NewParent = TargetItem;
			TArray<UDreamWidget*> ProposedChildren;
			for (const FHierarchyDreamWidgetDragDropOp::FItem& DraggedItem : HierarchyDragDropOp->DraggedWidgets)
			{
				if (IsValid(DraggedItem.Widget)) ProposedChildren.Add(DraggedItem.Widget);
			}
			if (!NewParent->CanAcceptChildren(ProposedChildren))
			{
				HierarchyDragDropOp->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				HierarchyDragDropOp->CurrentHoverText = LOCTEXT("ParentAtCapacity", "This widget cannot accept the selected children.");
				return TOptional<EItemDropZone>();
			}

			if (bIsDrop)
			{
				if (Manager.IsValid())
				{
					Manager->GetPrefabHelperObject()->SetAnythingDirty();
				}
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
						const bool bInsertInSameParent = TemplateWidget->GetParent() == NewParent;
						const bool bNeedToDropIndex = TemplateWidget->GetSiblingIndex() < Index.GetValue();

						if (bInsertInSameParent && bNeedToDropIndex)
						{
							Index = Index.GetValue() - 1;
						}
					}

					const int32 DesiredIndex = Index.IsSet()
						? Index.GetValue()
						: (TemplateWidget->GetParent() == NewParent ? NewParent->GetChildrenCount() - 1 : -1);
					if (!TemplateWidget->TrySetParent(NewParent, true, DesiredIndex))
					{
						HierarchyDragDropOp->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
						HierarchyDragDropOp->CurrentHoverText = LOCTEXT("ReparentFailed", "Unable to move this widget to the target parent.");
						return TOptional<EItemDropZone>();
					}
					// Advance past the widget just placed so the next dragged widget lands AFTER it
					// (UMG parity: SHierarchyViewItem increments after every InsertChildAt). Without
					// this, every later widget inserted at the same index and a multi-selection came
					// out reversed.
					if (Index.IsSet())
					{
						Index = Index.GetValue() + 1;
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


void SDreamWidgetEditorHierarchyViewItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakObjectPtr<UDreamWidget> InModel, TSharedPtr<SDreamWidgetEditorHierarchyView> InHierarchyView, TSharedPtr<FDreamUIPrefabEditor> InManager)
{
	Widget = InModel;
	MouseEnter = InArgs._MouseEnter;
	MouseExit = InArgs._MouseExit;
	HierarchyView = InHierarchyView;
	Manager = InManager;
	auto PrefabHelperObject = InManager.IsValid() ? InManager->GetPrefabHelperObject() : nullptr;

	STableRow::Construct(
		STableRow::FArguments()
		.OnCanAcceptDrop(this, &SDreamWidgetEditorHierarchyViewItem::HandleCanAcceptDrop)
		.OnAcceptDrop(this, &SDreamWidgetEditorHierarchyViewItem::HandleAcceptDrop)
		.OnDragDetected(this, &SDreamWidgetEditorHierarchyViewItem::HandleDragDetected)
		.OnDragEnter(this, &SDreamWidgetEditorHierarchyViewItem::HandleDragEnter)
		.OnDragLeave(this, &SDreamWidgetEditorHierarchyViewItem::HandleDragLeave)
		.Padding(FMargin(0, 2))
		.Content()
		[
			SNew(SHorizontalBox)

			// Widget icon
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(SImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.DesiredSizeOverride(FVector2D(16, 16))
				.Image_Lambda([=, this]()
				{
					return FDreamGUIEditorModule::Get().GetWidgetIconBrush(Widget.Get());
				})
			]

			// Canvas
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0)
			[
				SNew(SBox)
				.Visibility_Lambda([=, this]()
				{
					if (Widget.IsValid() && Widget->IsCanvasWidget())
					{
						return EVisibility::Visible;
					}
					return EVisibility::Collapsed;
				})
				[
					SNew(SOverlay)
					+SOverlay::Slot()//canvas icon
					[
						SNew(SBox)
						.WidthOverride(16)
						.HeightOverride(16)
						.Padding(FMargin(0))
						.HAlign(EHorizontalAlignment::HAlign_Center)
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(SImage)
							.Image(FDreamGUIEditorStyle::Get().GetBrush("CanvasMark"))
							.Visibility_Lambda([=, this]()
							{
								if (Widget->IsCanvasWidget())
								{
									return EVisibility::Visible;
								}
								return EVisibility::Hidden;
							})
							.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.4f))
							.ToolTipText(LOCTEXT("CanvasMarkTip", "This is canvas widget. The number is the draw-call count of this canvas."))
						]
					]
					+SOverlay::Slot()//draw-call count
					[
						SNew(SBox)
						.WidthOverride(16)
						.HeightOverride(16)
						.Padding(FMargin(0))
						.HAlign(EHorizontalAlignment::HAlign_Left)
						.VAlign(EVerticalAlignment::VAlign_Center)
						[
							SNew(STextBlock)
							.ShadowColorAndOpacity(FLinearColor::Black)
							.ShadowOffset(FVector2D(1, 1))
							.Text_Lambda([=, this]()
							{
								int DrawCallCount = 0;
								if (Widget.IsValid() && Widget->IsCanvasWidget() && Widget->GetRenderCanvas())
								{
									 DrawCallCount = Widget->GetRenderCanvas()->GetDrawCallCount();
								}
								return FText::FromString(FString::Printf(TEXT("%d"), DrawCallCount));
							})
							.ColorAndOpacity(FSlateColor(FLinearColor(FColor::Green)))
							.Visibility_Lambda([=, this]()
							{
								if (Widget.IsValid() && Widget->IsCanvasWidget())
								{
									return EVisibility::Visible;
								}
								return EVisibility::Hidden;
							})
							.ToolTipText(LOCTEXT("DrawCallCountTip", "The number is the draw-call count generated by this DreamCanvas."))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
					]
				]
			]			

			// Name of the widget
			+SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SAssignNew(EditBox, SInlineEditableTextBlock)
				//.Font(this, &SHierarchyViewItem::GetItemFont)
				.Text(this, &SDreamWidgetEditorHierarchyViewItem::GetItemText)
				.HighlightText(InArgs._HighlightText)
				.ToolTipText(this, &SDreamWidgetEditorHierarchyViewItem::GetItemTooltipText)
				.ColorAndOpacity(this, &SDreamWidgetEditorHierarchyViewItem::GetNameTextColorAndOpacity)
				.IsReadOnly(this, &SDreamWidgetEditorHierarchyViewItem::IsReadOnly)
				.OnEnterEditingMode(this, &SDreamWidgetEditorHierarchyViewItem::OnBeginNameTextEdit)
				.OnExitEditingMode(this, &SDreamWidgetEditorHierarchyViewItem::OnEndNameTextEdit)
				.OnVerifyTextChanged(this, &SDreamWidgetEditorHierarchyViewItem::OnVerifyNameTextChanged)
				.OnTextCommitted(this, &SDreamWidgetEditorHierarchyViewItem::OnNameTextCommited)
				.IsSelected(this, &SDreamWidgetEditorHierarchyViewItem::IsSelectedExclusively)
			]

			// What the widget is made of. Every row here is a UDreamWidget, so the name is the only
			// thing telling a text block from a horizontal box unless the type is printed as well.
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6, 0, 2, 0)
			[
				SNew(STextBlock)
				.Text(this, &SDreamWidgetEditorHierarchyViewItem::GetItemTypeText)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]

			// SubPrefab
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SOverlay)
				+SOverlay::Slot()
				[
					SNew(SImage)
					.Image_Lambda([=, this]()
					{
						if (PrefabHelperObject)
						{
							if (!PrefabHelperObject->IsWidgetBelongsToSubPrefab(Widget.Get()))//is sub prefab
							{
								if (PrefabHelperObject->IsWidgetBelongsToMissingSubPrefab(Widget.Get()))
								{
									return FDreamGUIEditorStyle::Get().GetBrush("PrefabMarkBroken");
								}
							}
							else
							{
								if (PrefabHelperObject->GetSubPrefabAsset(Widget.Get())->GetIsPrefabVariant())
								{
									return FDreamGUIEditorStyle::Get().GetBrush("PrefabVariantMarkWhite");
								}
							}
						}
						return FDreamGUIEditorStyle::Get().GetBrush("PrefabMarkWhite");
					})
					.ColorAndOpacity_Lambda([=, this]()
					{
						if (PrefabHelperObject)
						{
							if (PrefabHelperObject->IsWidgetBelongsToSubPrefab(Widget.Get()))//is sub prefab
							{
								return FSlateColor(PrefabHelperObject->GetSubPrefabData(Widget.Get()).EditorIdentifyColor);
							}
							else
							{
								if (PrefabHelperObject->IsWidgetBelongsToMissingSubPrefab(Widget.Get()))
								{
									return FSlateColor(FColor::White);
								}
							}
						}
						return FSlateColor(FColor::Green);
					})
					.Visibility_Lambda([=, this]()
					{
						if (PrefabHelperObject)
						{
							if (PrefabHelperObject->IsWidgetBelongsToSubPrefab(Widget.Get()))//is sub prefab
							{
								return EVisibility::Visible;
							}
							else
							{
								if (PrefabHelperObject->IsWidgetBelongsToMissingSubPrefab(Widget.Get()))
								{
									return EVisibility::Visible;
								}
								else
								{
									return EVisibility::Hidden;
								}
							}
						}
						return EVisibility::Hidden;
					})
					.ToolTipText_Lambda([=, this]()
					{
						if (PrefabHelperObject)
						{
							if (!PrefabHelperObject->IsWidgetBelongsToSubPrefab(Widget.Get()))//is sub prefab
							{
								if (PrefabHelperObject->IsWidgetBelongsToMissingSubPrefab(Widget.Get()))
								{
									return LOCTEXT("PrefabMarkBrokenTip", "This widget was part of another prefab, but the prefab asset is missing!");
								}
							}
						}
						return LOCTEXT("PrefabMarkWhiteTip", "This widget belongs to another prefab.");
					})
				]
			]

			// Designer lock
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(FMargin(3, 1))
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.ForegroundColor(FCoreStyle::Get().GetSlateColor("Foreground"))
				.OnClicked(this, &SDreamWidgetEditorHierarchyViewItem::OnToggleLockedInDesigner)
				.ToolTipText(LOCTEXT("WidgetLockedButtonToolTip", "Lock or unlock this widget and its children in the designer. Hold Shift to affect only this widget."))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(this, &SDreamWidgetEditorHierarchyViewItem::GetLockBrushForWidget)
					.ColorAndOpacity(this, &SDreamWidgetEditorHierarchyViewItem::GetLockIconColorAndOpacity)
				]
			]

			// Designer visibility
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ContentPadding(FMargin(3, 1))
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.ForegroundColor(FCoreStyle::Get().GetSlateColor("Foreground"))
				.OnClicked(this, &SDreamWidgetEditorHierarchyViewItem::OnToggleVisibility)
				.ToolTipText(LOCTEXT("WidgetVisibilityButtonToolTip", "Toggle Widget's Editor Visibility"))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(this, &SDreamWidgetEditorHierarchyViewItem::GetVisibilityBrushForWidget)
					.ColorAndOpacity(this, &SDreamWidgetEditorHierarchyViewItem::GetVisibilityIconColorAndOpacity)
				]
			]
		],
		InOwnerTableView);
}

void SDreamWidgetEditorHierarchyViewItem::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	MouseEnter.ExecuteIfBound();
	STableRow::OnMouseEnter(MyGeometry, MouseEvent);
}
void SDreamWidgetEditorHierarchyViewItem::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	MouseExit.ExecuteIfBound();
	STableRow::OnMouseLeave(MouseEvent);
}
void SDreamWidgetEditorHierarchyViewItem::RequestEditName()
{
	EditBox->EnterEditingMode();
}
bool SDreamWidgetEditorHierarchyViewItem::CanRename() const
{
	// The same policy the Rename command asks, so a lock the drag and the drop already honour cannot
	// be walked around by typing over the name instead.
	return DreamWidgetHierarchyRename::CanRename(Widget.Get(), Manager.IsValid() && Manager.Pin()->IsWidgetLockedForInteraction(Widget.Get()));
}

TOptional<EItemDropZone> SDreamWidgetEditorHierarchyViewItem::HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<UDreamWidget> TargetItem)
{
	const UDreamWidget* LockOwner = DreamWidgetHierarchyDrop::GetLockOwnerForDropZone(Widget.Get(), DropZone);
	if (LockOwner != nullptr && Manager.IsValid() && Manager.Pin()->IsWidgetLockedForInteraction(LockOwner))return TOptional<EItemDropZone>();
	TSharedPtr<FDragDropOperation> DragDropOp = DragDropEvent.GetOperation();
	if (DragDropOp.IsValid() && DragDropOp->IsOfType<FAssetDragDropOp>())
	{
		auto AssetDragDropOp = StaticCastSharedPtr<FAssetDragDropOp>(DragDropOp);
		if (AssetDragDropOp->GetAssets().Num() > 0)
		{
			auto EditingPrefab = Manager.Pin()->GetPrefabBeingEdited();
			TOptional<EItemDropZone> ValidDropZone;
			for (auto AssetData : AssetDragDropOp->GetAssets())
			{
				if (AssetData.AssetClassPath == UDreamUIPrefab::StaticClass()->GetClassPathName())
				{
					if (AssetData.GetAsset()->GetPathName() == EditingPrefab->GetPathName())
					{
						AssetDragDropOp->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
						AssetDragDropOp->CurrentHoverText = LOCTEXT("CantDropPrefabToItself", "Can't drop prefab to itself.");
						return TOptional<EItemDropZone>();
					}
					ValidDropZone = EItemDropZone::OntoItem;
				}
			}
			return ValidDropZone;
		}
	}
	if (DragDropEvent.GetOperationAs<FDreamUIPaletteDragDropOp>().IsValid())
	{
		// Hard-coding OntoItem here discarded the zone the row had already computed, so a palette
		// element could only ever be appended under whatever row it landed on -- no insertion
		// indicator, and OnAcceptDrop could never see Above/Below because the engine feeds it back
		// whatever this function returns. Routing it through the same processing as an internal
		// drag is what rewrites an above/below drop into "under this row's parent, at this index",
		// and it keeps the two drag sources from drifting apart again.
		const bool bIsDrop = false;
		return ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, Manager.Pin(), Widget.Get());
	}

	if (DragDropOp.IsValid() && DragDropOp->IsOfType<FHierarchyDreamWidgetDragDropOp>())
	{
		const bool bIsDrop = false;
		auto HierarchyDragDropOp = StaticCastSharedPtr<FHierarchyDreamWidgetDragDropOp>(DragDropOp);
		if (HierarchyDragDropOp->DraggedWidgets.Num() > 0)
		{
			TOptional<EItemDropZone> ValidDropZone;
			for (auto DraggedWidget : HierarchyDragDropOp->DraggedWidgets)
			{
				if (SupportDrop(DraggedWidget.Widget, Widget.Get(), DropZone))
				{
					auto Zone = ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, Manager.Pin(), Widget.Get());
					if (ValidDropZone.IsSet())
					{
						if (Zone.GetValue() != ValidDropZone.GetValue())
						{
							return TOptional<EItemDropZone>();
						}
					}
					else
						ValidDropZone = Zone;
				}
			}
			return ValidDropZone;
		}
	}
	return TOptional<EItemDropZone>();
}
FReply SDreamWidgetEditorHierarchyViewItem::HandleAcceptDrop(FDragDropEvent const& DragDropEvent, EItemDropZone DropZone, TWeakObjectPtr<UDreamWidget> TargetItem)
{
	const bool bIsDrop = true;
	TOptional<EItemDropZone> Zone = ProcessHierarchyDragDrop(DragDropEvent, DropZone, bIsDrop, Manager.Pin(), Widget.Get());
	if (Zone.IsSet())
	{
		HierarchyView.Pin()->RequestRefresh();
		return FReply::Handled();
	}
	else
		return FReply::Unhandled();
}
FReply SDreamWidgetEditorHierarchyViewItem::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (Manager.IsValid() && Manager.Pin()->IsWidgetLockedForInteraction(Widget.Get()))return FReply::Handled();
	TArray<UDreamWidget*> DraggedItems;

	// Dragging multiple items?
	if (auto Selection = UDreamUISelection::GetInstance(Widget->GetWorld()))
	{
		if (Selection->GetSelectedWidgets().Num() > 1 && Selection->GetSelectedWidgets().Contains(Widget.Get()))
		{
			for (auto Selected : Selection->GetSelectedWidgets())
			{
				if (Selected.IsValid() && (!Manager.IsValid() || !Manager.Pin()->IsWidgetLockedForInteraction(Selected.Get())))
				{
					DraggedItems.Add(Selected.Get());
				}
			}
		}
	}

	if (DraggedItems.Num() == 0)
	{
		DraggedItems.Add(Widget.Get());
	}

	if (DraggedItems.Num() > 0)
	{
		bool bAllCanDrag = true;
		if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget.Get()))
		{
			for (auto Item : DraggedItems)
			{
				if (PrefabHelperObject->IsWidgetBelongsToSubPrefab(Item) && !PrefabHelperObject->IsSubPrefabRootWidget(Item))
				{
					bAllCanDrag = false;
					break;
				}
			}
		}
		if (bAllCanDrag)
		{
			return FReply::Handled().BeginDragDrop(FHierarchyDreamWidgetDragDropOp::New(DraggedItems));
		}
	}

	return FReply::Handled();
}
void SDreamWidgetEditorHierarchyViewItem::HandleDragEnter(FDragDropEvent const& DragDropEvent)
{
	//UE_LOG(LogTemp, Log, TEXT("HandleDragEnter, %s"), *Model->GetName());
}
void SDreamWidgetEditorHierarchyViewItem::HandleDragLeave(const FDragDropEvent& DragDropEvent)
{
	//UE_LOG(LogTemp, Log, TEXT("HandleDragLeave, %s"), *Model->GetName());
}

FText SDreamWidgetEditorHierarchyViewItem::GetItemText() const
{
	return Widget.IsValid() ? FText::FromString(Widget->GetDisplayName()) : FText::GetEmpty();
}

FText SDreamWidgetEditorHierarchyViewItem::GetItemTypeText() const
{
	const FString TypeLabel = DreamWidgetHierarchyType::GetTypeLabel(Widget.Get());
	return TypeLabel.IsEmpty() ? FText::GetEmpty() : FText::FromString(TypeLabel);
}

FText SDreamWidgetEditorHierarchyViewItem::GetItemTooltipText() const
{
	return Widget.IsValid() ? FText::Format(LOCTEXT("ItemTooltipFormat", "Path name: {0}"), FText::FromString(Widget->GetPathName()))
		: FText::GetEmpty();
}

FSlateColor SDreamWidgetEditorHierarchyViewItem::GetNameTextColorAndOpacity() const
{
	if (Widget.IsValid())
	{
		if (auto PrefabHelperObject = UDreamUIPrefabHelperObject::GetPrefabHelperObject_WhichManageThisWidget(Widget.Get()))
		{
			if (PrefabHelperObject->IsWidgetBelongsToSubPrefab(Widget.Get()))//is sub prefab
			{
				if (Widget->GetWidgetActiveInHierarchy())
				{
					return FLinearColor(FColor(124,171,240, 255));
				}
				return FLinearColor(FColor(124,171,240, 128));
			}
			else
			{
				if (PrefabHelperObject->IsWidgetBelongsToMissingSubPrefab(Widget.Get()))
				{
					if (Widget->GetWidgetActiveInHierarchy())
					{
						return FSlateColor(FColor::Red);
					}
					return FSlateColor(FColor(255, 0, 0, 128));
				}
			}
		}
		if (Widget->GetWidgetActiveInHierarchy())
		{
			return FSlateColor(FColor(192,192,192,255));
		}
		return FSlateColor(FColor(192,192,192,128));
	}
	return FSlateColor(FColor(192,192,192,128));
}

FSlateColor SDreamWidgetEditorHierarchyViewItem::GetVisibilityIconColorAndOpacity() const
{
	auto NameTextColorAndOpacity = GetNameTextColorAndOpacity();
	auto Alpha = NameTextColorAndOpacity.GetSpecifiedColor().A * 255;
	NameTextColorAndOpacity = FSlateColor(FColor(255,255,255,(uint8)Alpha));
	return NameTextColorAndOpacity;
}

bool SDreamWidgetEditorHierarchyViewItem::IsReadOnly() const
{
	return !CanRename();
}
void SDreamWidgetEditorHierarchyViewItem::OnBeginNameTextEdit()
{
	InitialText = FText::FromString(Widget->GetDisplayName());
}
void SDreamWidgetEditorHierarchyViewItem::OnEndNameTextEdit()
{

}
bool SDreamWidgetEditorHierarchyViewItem::OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
{
	const FString ProposedName = InText.ToString().TrimStartAndEnd();
	if (ProposedName.IsEmpty())
	{
		OutErrorMessage = LOCTEXT("EmptyWidgetName", "Widget name cannot be empty.");
		return false;
	}
	if (!FName::IsValidXName(ProposedName, FString(INVALID_OBJECTNAME_CHARACTERS) + TEXT("/"), &OutErrorMessage))
	{
		return false;
	}
	return true;
}
void SDreamWidgetEditorHierarchyViewItem::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
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
	const FString UniqueName = FDreamUIEditorTools::MakeUniqueWidgetDisplayName(
		Widget.Get(), InText.ToString().TrimStartAndEnd(), Widget.Get());
	FDreamUIUtils::ChangePropertyWithNotify(Widget.Get(), UDreamWidget::GetPropertyName_DisplayName(), [=, this]()
	{
		Widget->SetDisplayName(UniqueName);
	});
	GEditor->EndTransaction();

	HierarchyView.Pin()->RequestRefresh();
}
FReply SDreamWidgetEditorHierarchyViewItem::OnToggleVisibility()
{
	if (Manager.IsValid() && Widget.IsValid())
	{
		Manager.Pin()->SetWidgetHiddenInDesigner(Widget.Get(), !Manager.Pin()->IsWidgetHiddenInDesigner(Widget.Get()));
	}
	return FReply::Handled();
}
FText SDreamWidgetEditorHierarchyViewItem::GetVisibilityBrushForWidget() const
{
	return Manager.IsValid() && Widget.IsValid() && !Manager.Pin()->IsWidgetHiddenInDesigner(Widget.Get())
		? FEditorFontGlyphs::Eye : FEditorFontGlyphs::Eye_Slash;
}

FReply SDreamWidgetEditorHierarchyViewItem::OnToggleLockedInDesigner()
{
	if (Manager.IsValid() && Widget.IsValid())
	{
		const bool bRecursive = !FSlateApplication::Get().GetModifierKeys().IsShiftDown();
		Manager.Pin()->SetWidgetLockedInDesigner(Widget.Get(), !Manager.Pin()->IsWidgetLockedInDesigner(Widget.Get()), bRecursive);
	}
	return FReply::Handled();
}

FText SDreamWidgetEditorHierarchyViewItem::GetLockBrushForWidget() const
{
	return Manager.IsValid() && Widget.IsValid() && Manager.Pin()->IsWidgetLockedInDesigner(Widget.Get())
		? FEditorFontGlyphs::Lock : FEditorFontGlyphs::Unlock;
}

FSlateColor SDreamWidgetEditorHierarchyViewItem::GetLockIconColorAndOpacity() const
{
	return GetVisibilityIconColorAndOpacity();
}

bool SDreamWidgetEditorHierarchyViewItem::SupportDrop(UDreamWidget* Dragging, UDreamWidget* Current, EItemDropZone DropZone)
{
	if (Current == Current->GetRootWidgetInHierarchy())
	{
		if (DropZone == EItemDropZone::OntoItem)
		{
			return true;
		}
		return false;
	}
	if (Current->IsChildOf(Dragging))
	{
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
