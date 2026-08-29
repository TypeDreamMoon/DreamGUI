// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "SDreamWidgetDesignerViewport.h"
#include "DreamWidgetDesignerViewportClient.h"
#include "DreamWidgetBlueprintEditor.h"
#include "SDreamWidgetDesignerViewportToolbar.h"
#include "SDreamWidgetPalette.h"
#include "Core/DreamUIManager.h"
#include "Framework/Application/SlateApplication.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DreamWidgetEditorHierarchyViewItem.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabEditorViewport"

void SDreamWidgetDesignerViewport::Construct(const FArguments& InArgs, TSharedPtr<FDreamWidgetBlueprintEditor> InDesigner, EViewModeIndex InViewMode)
{
	this->DesignerPtr = InDesigner;
	this->ViewMode = InViewMode;
	SEditorViewport::Construct(SEditorViewport::FArguments());
}
void SDreamWidgetDesignerViewport::BindCommands()
{
	SEditorViewport::BindCommands();
}
TSharedRef<FEditorViewportClient> SDreamWidgetDesignerViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FDreamWidgetDesignerViewportClient(this->DesignerPtr, SharedThis(this)));
	EditorViewportClient->bSetListenerPosition = false;
	EditorViewportClient->SetRealtime(true);
	EditorViewportClient->SetShowStats(true);
	EditorViewportClient->VisibilityDelegate.BindLambda([]() {return true; });
	EditorViewportClient->SetViewMode(ViewMode);
	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDreamWidgetDesignerViewport::BuildViewportToolbar()
{
	return SNew(SDreamWidgetDesignerViewportToolbar, SharedThis(this));
}
EVisibility SDreamWidgetDesignerViewport::GetTransformToolbarVisibility() const
{
	return EVisibility::Hidden;
}
void SDreamWidgetDesignerViewport::OnFocusViewportToSelection()
{
	EditorViewportClient->FocusViewportToTargets();
}

bool SDreamWidgetDesignerViewport::SummonContextMenu()
{
	FSlateApplication& SlateApplication = FSlateApplication::Get();
	SlateApplication.DismissAllMenus();

	TSharedPtr<FDreamWidgetBlueprintEditor> Editor = DesignerPtr.Pin();
	if (!Editor.IsValid())
	{
		return false;
	}
	TSharedPtr<SWidget> MenuContent = Editor->BuildWidgetContextMenu();
	if (!MenuContent.IsValid())
	{
		return false;
	}

	FWidgetPath WidgetPath;
	if (!SlateApplication.GeneratePathToWidgetUnchecked(AsShared(), WidgetPath))
	{
		return false;
	}
	return SlateApplication.PushMenu(
		AsShared(),
		WidgetPath,
		MenuContent.ToSharedRef(),
		SlateApplication.GetCursorPos(),
		FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu),
		true,
		FVector2D::ZeroVector,
		EPopupMethod::UseCurrentWindow).IsValid();
}

namespace DreamWidgetDesignerViewportLocal
{
	static FIntPoint ToViewportPixel(const FGeometry& Geometry, const FVector2D& ScreenPosition, FViewport* Viewport)
	{
		if (!Viewport)return FIntPoint::ZeroValue;
		const FVector2D LocalPosition = Geometry.AbsoluteToLocal(ScreenPosition);
		const FVector2D LocalSize = Geometry.GetLocalSize();
		const FIntPoint ViewportSize = Viewport->GetSizeXY();
		return FIntPoint(
			FMath::RoundToInt(LocalSize.X > 0 ? LocalPosition.X * ViewportSize.X / LocalSize.X : 0),
			FMath::RoundToInt(LocalSize.Y > 0 ? LocalPosition.Y * ViewportSize.Y / LocalSize.Y : 0));
	}
}

FReply SDreamWidgetDesignerViewport::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (DragDropEvent.GetOperationAs<FDreamUIPaletteDragDropOp>().IsValid() && EditorViewportClient.IsValid())
	{
		const FIntPoint Pixel = DreamWidgetDesignerViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		UDreamWidget* Target = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		EditorViewportClient->SetPaletteDropPreview(Target);
		return Target ? FReply::Handled() : FReply::Unhandled();
	}
	// A prefab dragged from the Content Browser used to fall straight through to SEditorViewport,
	// which knows nothing about widgets -- no preview, no cursor feedback, no refusal. The
	// hierarchy tree accepted the same drag, so the design surface was the odd one out. A widget
	// dragged out of the hierarchy tree was the same story: the tree could reparent it, the canvas
	// it is drawn on could not.
	if (EditorViewportClient.IsValid()
		&& (DragDropEvent.GetOperationAs<FAssetDragDropOp>().IsValid()
			|| DragDropEvent.GetOperationAs<FHierarchyDreamWidgetDragDropOp>().IsValid()))
	{
		const FIntPoint Pixel = DreamWidgetDesignerViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		UDreamWidget* Target = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		// ProcessHierarchyDragDrop is the tree's own validator: it refuses a cycle, a full parent
		// and a drop onto the dragged widget itself, and writes the reason onto the cursor. Asking
		// it here is what keeps the two surfaces on one set of rules.
		const TOptional<EItemDropZone> Zone = ProcessHierarchyDragDrop(DragDropEvent, EItemDropZone::OntoItem,
			/*bIsDrop*/false, DesignerPtr.Pin(), Target);
		EditorViewportClient->SetPaletteDropPreview(Zone.IsSet() ? Target : nullptr);
		return Zone.IsSet() ? FReply::Handled() : FReply::Unhandled();
	}
	return SEditorViewport::OnDragOver(MyGeometry, DragDropEvent);
}

FReply SDreamWidgetDesignerViewport::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FDreamUIPaletteDragDropOp> PaletteOp = DragDropEvent.GetOperationAs<FDreamUIPaletteDragDropOp>())
	{
		TSharedPtr<FDreamWidgetBlueprintEditor> Editor = DesignerPtr.Pin();
		if (!Editor.IsValid() || !EditorViewportClient.IsValid())return FReply::Unhandled();
		const FIntPoint Pixel = DreamWidgetDesignerViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		UDreamWidget* Parent = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		if (!Parent)return FReply::Unhandled();
		FVector DropWorldPosition = FVector::ZeroVector;
		const bool bHasPosition = EditorViewportClient->GetDropWorldPosition(Pixel.X, Pixel.Y, Parent, DropWorldPosition);
		// A viewport drop names a parent and a position, never a sibling order -- append.
		UDreamWidget* Created = PaletteOp->CreateUnder(Parent, TOptional<int32>(),
			[bHasPosition, DropWorldPosition](UDreamWidget* Widget)
		{
			if (Widget && bHasPosition)Widget->SetWorldLocation(DropWorldPosition);
		});
		EditorViewportClient->ClearPaletteDropPreview();
		if (Created)
		{
			UDreamUIManagerWorldSubsystem::GetInstance(Editor->GetWorld())->MarkDreamUIWidgetOutlinerChanged();
			EditorViewportClient->Invalidate();
			return FReply::Handled();
		}
	}
	if (DragDropEvent.GetOperationAs<FAssetDragDropOp>().IsValid()
		|| DragDropEvent.GetOperationAs<FHierarchyDreamWidgetDragDropOp>().IsValid())
	{
		TSharedPtr<FDreamWidgetBlueprintEditor> Editor = DesignerPtr.Pin();
		if (!Editor.IsValid() || !EditorViewportClient.IsValid())return FReply::Unhandled();
		const FIntPoint Pixel = DreamWidgetDesignerViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		UDreamWidget* Parent = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		EditorViewportClient->ClearPaletteDropPreview();
		if (!Parent)return FReply::Unhandled();
		// One validator, both surfaces. For an asset it reaches TryHandleAssetDragDropOperation --
		// the same entry point the hierarchy row uses, so a Content-Browser prefab becomes a linked
		// sub-prefab here too, guards intact. For a hierarchy drag it reparents, having already
		// refused the cycles and the full parents.
		const TOptional<EItemDropZone> Zone = ProcessHierarchyDragDrop(DragDropEvent, EItemDropZone::OntoItem,
			/*bIsDrop*/true, Editor, Parent);
		if (!Zone.IsSet())return FReply::Unhandled();
		UDreamUIManagerWorldSubsystem::GetInstance(Editor->GetWorld())->MarkDreamUIWidgetOutlinerChanged();
		EditorViewportClient->Invalidate();
		return FReply::Handled();
	}
	return SEditorViewport::OnDrop(MyGeometry, DragDropEvent);
}

void SDreamWidgetDesignerViewport::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (EditorViewportClient.IsValid())EditorViewportClient->ClearPaletteDropPreview();
	SEditorViewport::OnDragLeave(DragDropEvent);
}

TSharedRef<SEditorViewport> SDreamWidgetDesignerViewport::GetViewportWidget()
{
	return SharedThis(this);
}
TSharedPtr<FExtender> SDreamWidgetDesignerViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}
void SDreamWidgetDesignerViewport::OnFloatingButtonClicked()
{

}

#undef LOCTEXT_NAMESPACE
