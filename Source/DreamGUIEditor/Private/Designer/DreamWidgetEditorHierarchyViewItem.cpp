// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamWidgetEditorHierarchyViewItem.h"
#include "DreamWidgetBlueprint.h"

#include "ClassIconFinder.h"
#include "DetailLayoutBuilder.h"
#include "DreamWidgetEditorHierarchyView.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Styling/CoreStyle.h"
#include "DreamWidgetBlueprintEditor.h"
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
#include "SDreamWidgetPalette.h"//FDreamUIPaletteDragDropOp
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

	// Add an FItem for each widget in the drag operation.
	//
	// Nothing is Modify()'d here. These are PREVIEW widgets, which are not in the transaction
	// buffer at all: the drop performs the move on them for the geometry, then mirrors it onto the
	// authoring tree through ReparentTemplatesFrom, and that is what this transaction records.
	for (const auto& Widget : InWidgets)
	{
		FItem DraggedWidget;

		DraggedWidget.Widget = Widget;
		DraggedWidget.WidgetParent = Widget->GetParent();

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

TOptional<EItemDropZone> ProcessHierarchyDragDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, bool bIsDrop, TSharedPtr<FDreamWidgetBlueprintEditor> Manager, UDreamWidget* TargetItem, TOptional<int32> Index)
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
		// The palette and asset ops are decorated the same way the hierarchy-internal drag is,
		// so an illegal target says no while the author still holds the button. Without this the
		// cursor carried whatever the previous row wrote -- the drag looked legal over a full
		// container and dead over a legal one.
		const TSharedPtr<FDecoratedDragDropOp> Decorated = DragDropOp->IsOfType<FDecoratedDragDropOp>()
			? StaticCastSharedPtr<FDecoratedDragDropOp>(DragDropOp) : TSharedPtr<FDecoratedDragDropOp>();
		if (Decorated.IsValid())
		{
			Decorated->ResetToDefaultToolTip();
		}
		if (!IsValid(TargetItem) || !TargetItem->CanAcceptAdditionalChildren())
		{
			if (Decorated.IsValid())
			{
				Decorated->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				Decorated->CurrentHoverText = LOCTEXT("TargetCannotTakeChild", "This widget cannot accept another child.");
			}
			return TOptional<EItemDropZone>();
		}
		// The same gate the drop will hit in CreateWidget, said during the drag: a text-authored
		// class refuses structural edits from the designer, and finding that out on release reads
		// as a broken palette rather than a rule.
		if (Manager.IsValid() && DreamUITextAuthoring::IsTextAuthored(Manager->GetWidgetBlueprint()))
		{
			if (Decorated.IsValid())
			{
				Decorated->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				Decorated->CurrentHoverText = DreamUITextAuthoring::DescribeStructuralRefusal(
					Manager->GetWidgetBlueprint(), TEXT("create a widget"));
			}
			return TOptional<EItemDropZone>();
		}
		if (Decorated.IsValid())
		{
			Decorated->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.OK"));
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
			// A structural entry that does not pass through DreamWidgetTreeEditing directly: this
			// drop calls TrySetParent on the PREVIEW widgets below, then mirrors the whole move onto
			// the templates through ReparentTemplatesFrom -- the same two halves, in the same order,
			// as the viewport's drag. Refused during the drag rather than at the drop, so the cursor
			// says no before the author lets go -- which is what "visibly disabled" means for a
			// gesture.
			if (Manager.IsValid() && DreamUITextAuthoring::IsTextAuthored(Manager->GetWidgetBlueprint()))
			{
				HierarchyDragDropOp->CurrentIconBrush = FAppStyle::GetBrush(TEXT("Graph.ConnectorFeedback.Error"));
				HierarchyDragDropOp->CurrentHoverText = DreamUITextAuthoring::DescribeStructuralRefusal(
					Manager->GetWidgetBlueprint(), TEXT("move a widget"));
				return TOptional<EItemDropZone>();
			}

			if (bIsDrop)
			{
				if (Manager.IsValid())
				{
					Manager->MarkDesignChanged();
				}
				// No Modify on NewParent, and none on the dragged widgets below: every one of them
				// is a PREVIEW object, and the preview is not undoable -- it is rebuilt from the
				// template after the transaction. ReparentTemplatesFrom at the bottom of this block
				// records the authored half, which is the half undo has to be able to put back.

				// The preview widgets that actually moved, for the template mirror below. Collected
				// rather than mirrored per widget: ReparentWidget broadcasts a structural change that
				// invalidates the preview, and doing that while this loop still iterates preview
				// widgets is how a drop eats its own state.
				TArray<UDreamWidget*> MovedPreviewWidgets;

				for (const auto& DraggedWidget : HierarchyDragDropOp->DraggedWidgets)
				{
					// Named TemplateWidget, but it is the preview's -- see the note above. Nothing
					// records it.
					auto TemplateWidget = DraggedWidget.Widget;

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
					MovedPreviewWidgets.Add(TemplateWidget);
				}

				// The other half of the move: without it the reparent lives only in the preview and
				// the next rebuild puts everything back. Once, after the loop, exactly as the
				// viewport's drag does after ApplyPendingReparent.
				if (Manager.IsValid() && MovedPreviewWidgets.Num() > 0)
				{
					Manager->ReparentTemplatesFrom(MovedPreviewWidgets, NewParent);
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


void SDreamWidgetEditorHierarchyViewItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakObjectPtr<UDreamWidget> InModel, TSharedPtr<SDreamWidgetEditorHierarchyView> InHierarchyView, TSharedPtr<FDreamWidgetBlueprintEditor> InManager)
{
	Widget = InModel;
	MouseEnter = InArgs._MouseEnter;
	MouseExit = InArgs._MouseExit;
	HierarchyView = InHierarchyView;
	Manager = InManager;
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
								// Every other reader of Widget in this row checks first: the row
								// outlives the preview widget it is showing, because a rebuild
								// destroys the tree and the list refreshes a tick later.
								if (Widget.IsValid() && Widget->IsCanvasWidget())
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
	// Dropping an ASSET onto a hierarchy row used to nest a prefab under it, and the only check worth
	// making was "not into itself". Nothing is placed from the content browser this way any more.
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
		// Pinned and checked, like every other weak handle this row holds: a drop can be the
		// gesture that closes the designer, and the tree view goes before the row does.
		if (const TSharedPtr<SDreamWidgetEditorHierarchyView> View = HierarchyView.Pin())
		{
			View->RequestRefresh();
		}
		return FReply::Handled();
	}
	else
		return FReply::Unhandled();
}
FReply SDreamWidgetEditorHierarchyViewItem::HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!Widget.IsValid())return FReply::Handled();
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
		// The refusal here was "a widget inside a sub-prefab instance cannot be dragged out of it".
		// A class model has no sub-prefab instances, so every selection is draggable.
		return FReply::Handled().BeginDragDrop(FHierarchyDreamWidgetDragDropOp::New(DraggedItems));
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
		// Sub-prefab widgets used to get their own name colour here. There are none.
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
	InitialText = Widget.IsValid() ? FText::FromString(Widget->GetDisplayName()) : FText::GetEmpty();
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
	// The commit can arrive after the row's widget is gone -- a rebuild while the text box has focus
	// is enough -- and everything below dereferences it.
	if (!Widget.IsValid())
	{
		return;
	}

	GEditor->BeginTransaction(LOCTEXT("ChangeWidgetName_Transaction", "Change Name"));
	// The display name is the compiler's variable name, so renaming is an edit to the asset, not a
	// label on a preview object that is about to be rebuilt away.
	if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(Widget.Get()))
	{
		Designer->DesignerRenameWidget(Widget.Get(), InText.ToString().TrimStartAndEnd());
		GEditor->EndTransaction();
		if (const TSharedPtr<SDreamWidgetEditorHierarchyView> View = HierarchyView.Pin())
		{
			View->RequestRefresh();
		}
		return;
	}
	Widget->Modify();
	const FString UniqueName = FDreamUIEditorTools::MakeUniqueWidgetDisplayName(
		Widget.Get(), InText.ToString().TrimStartAndEnd(), Widget.Get());
	FDreamUIUtils::ChangePropertyWithNotify(Widget.Get(), UDreamWidget::GetPropertyName_DisplayName(), [=, this]()
	{
		Widget->SetDisplayName(UniqueName);
	});
	GEditor->EndTransaction();

	if (const TSharedPtr<SDreamWidgetEditorHierarchyView> View = HierarchyView.Pin())
	{
		View->RequestRefresh();
	}
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
