// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamUIPrefabEditorViewport.h"
#include "DreamUIPrefabEditorViewportClient.h"
#include "DreamUIPrefabEditor.h"
#include "DreamUIPrefabEditorViewportToolbar.h"
#include "SDreamUIPrefabPalette.h"
#include "Core/DreamUIManager.h"
#include "Framework/Application/SlateApplication.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "DreamWidgetEditorHierarchyViewItem.h"

#define LOCTEXT_NAMESPACE "DreamGUIPrefabEditorViewport"

void SDreamUIPrefabEditorViewport::Construct(const FArguments& InArgs, TSharedPtr<FDreamUIPrefabEditor> InPrefabEditor, EViewModeIndex InViewMode)
{
	this->PrefabEditorPtr = InPrefabEditor;
	this->ViewMode = InViewMode;
	SEditorViewport::Construct(SEditorViewport::FArguments());
}
void SDreamUIPrefabEditorViewport::BindCommands()
{
	SEditorViewport::BindCommands();
}
TSharedRef<FEditorViewportClient> SDreamUIPrefabEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FDreamUIPrefabEditorViewportClient(this->PrefabEditorPtr, SharedThis(this)));
	EditorViewportClient->bSetListenerPosition = false;
	EditorViewportClient->SetRealtime(true);
	EditorViewportClient->SetShowStats(true);
	EditorViewportClient->VisibilityDelegate.BindLambda([]() {return true; });
	EditorViewportClient->SetViewMode(ViewMode);
	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SDreamUIPrefabEditorViewport::BuildViewportToolbar()
{
	return SNew(SDreamUIPrefabEditorViewportToolbar, SharedThis(this));
}
EVisibility SDreamUIPrefabEditorViewport::GetTransformToolbarVisibility() const
{
	return EVisibility::Hidden;
}
void SDreamUIPrefabEditorViewport::OnFocusViewportToSelection()
{
	EditorViewportClient->FocusViewportToTargets();
}

bool SDreamUIPrefabEditorViewport::SummonContextMenu()
{
	FSlateApplication& SlateApplication = FSlateApplication::Get();
	SlateApplication.DismissAllMenus();

	TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
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

namespace DreamUIPrefabViewportLocal
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

FReply SDreamUIPrefabEditorViewport::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (DragDropEvent.GetOperationAs<FDreamUIPaletteDragDropOp>().IsValid() && EditorViewportClient.IsValid())
	{
		const FIntPoint Pixel = DreamUIPrefabViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
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
		const FIntPoint Pixel = DreamUIPrefabViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		UDreamWidget* Target = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		// ProcessHierarchyDragDrop is the tree's own validator: it refuses a cycle, a full parent
		// and a drop onto the dragged widget itself, and writes the reason onto the cursor. Asking
		// it here is what keeps the two surfaces on one set of rules.
		const TOptional<EItemDropZone> Zone = ProcessHierarchyDragDrop(DragDropEvent, EItemDropZone::OntoItem,
			/*bIsDrop*/false, PrefabEditorPtr.Pin(), Target);
		EditorViewportClient->SetPaletteDropPreview(Zone.IsSet() ? Target : nullptr);
		return Zone.IsSet() ? FReply::Handled() : FReply::Unhandled();
	}
	return SEditorViewport::OnDragOver(MyGeometry, DragDropEvent);
}

FReply SDreamUIPrefabEditorViewport::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FDreamUIPaletteDragDropOp> PaletteOp = DragDropEvent.GetOperationAs<FDreamUIPaletteDragDropOp>())
	{
		TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
		if (!Editor.IsValid() || !EditorViewportClient.IsValid())return FReply::Unhandled();
		const FIntPoint Pixel = DreamUIPrefabViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
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
		TSharedPtr<FDreamUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
		if (!Editor.IsValid() || !EditorViewportClient.IsValid())return FReply::Unhandled();
		const FIntPoint Pixel = DreamUIPrefabViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
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

void SDreamUIPrefabEditorViewport::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (EditorViewportClient.IsValid())EditorViewportClient->ClearPaletteDropPreview();
	SEditorViewport::OnDragLeave(DragDropEvent);
}

TSharedRef<SEditorViewport> SDreamUIPrefabEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}
TSharedPtr<FExtender> SDreamUIPrefabEditorViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}
void SDreamUIPrefabEditorViewport::OnFloatingButtonClicked()
{

}

#undef LOCTEXT_NAMESPACE
