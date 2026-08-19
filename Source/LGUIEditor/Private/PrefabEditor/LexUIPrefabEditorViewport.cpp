// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "LexUIPrefabEditorViewport.h"
#include "LexUIPrefabEditorViewportClient.h"
#include "LexUIPrefabEditor.h"
#include "LexUIPrefabEditorViewportToolbar.h"
#include "SLexUIPrefabPalette.h"
#include "Core/LexUIManager.h"
#include "Framework/Application/SlateApplication.h"
#include "DragAndDrop/AssetDragDropOp.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabEditorViewport"

void SLexUIPrefabEditorViewport::Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditor, EViewModeIndex InViewMode)
{
	this->PrefabEditorPtr = InPrefabEditor;
	this->ViewMode = InViewMode;
	SEditorViewport::Construct(SEditorViewport::FArguments());
}
void SLexUIPrefabEditorViewport::BindCommands()
{
	SEditorViewport::BindCommands();
}
TSharedRef<FEditorViewportClient> SLexUIPrefabEditorViewport::MakeEditorViewportClient()
{
	EditorViewportClient = MakeShareable(new FLexUIPrefabEditorViewportClient(this->PrefabEditorPtr, SharedThis(this)));
	EditorViewportClient->bSetListenerPosition = false;
	EditorViewportClient->SetRealtime(true);
	EditorViewportClient->SetShowStats(true);
	EditorViewportClient->VisibilityDelegate.BindLambda([]() {return true; });
	EditorViewportClient->SetViewMode(ViewMode);
	return EditorViewportClient.ToSharedRef();
}

TSharedPtr<SWidget> SLexUIPrefabEditorViewport::BuildViewportToolbar()
{
	return SNew(SLexUIPrefabEditorViewportToolbar, SharedThis(this));
}
EVisibility SLexUIPrefabEditorViewport::GetTransformToolbarVisibility() const
{
	return EVisibility::Hidden;
}
void SLexUIPrefabEditorViewport::OnFocusViewportToSelection()
{
	EditorViewportClient->FocusViewportToTargets();
}

bool SLexUIPrefabEditorViewport::SummonContextMenu()
{
	FSlateApplication& SlateApplication = FSlateApplication::Get();
	SlateApplication.DismissAllMenus();

	TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
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

namespace LexUIPrefabViewportLocal
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

FReply SLexUIPrefabEditorViewport::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (DragDropEvent.GetOperationAs<FLexUIPaletteDragDropOp>().IsValid() && EditorViewportClient.IsValid())
	{
		const FIntPoint Pixel = LexUIPrefabViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		ULexWidget* Target = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		EditorViewportClient->SetPaletteDropPreview(Target);
		return Target ? FReply::Handled() : FReply::Unhandled();
	}
	// A prefab dragged from the Content Browser used to fall straight through to SEditorViewport,
	// which knows nothing about widgets -- no preview, no cursor feedback, no refusal. The
	// hierarchy tree accepted the same drag, so the design surface was the odd one out.
	if (DragDropEvent.GetOperationAs<FAssetDragDropOp>().IsValid() && EditorViewportClient.IsValid())
	{
		const FIntPoint Pixel = LexUIPrefabViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		ULexWidget* Target = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		EditorViewportClient->SetPaletteDropPreview(Target);
		return Target ? FReply::Handled() : FReply::Unhandled();
	}
	return SEditorViewport::OnDragOver(MyGeometry, DragDropEvent);
}

FReply SLexUIPrefabEditorViewport::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FLexUIPaletteDragDropOp> PaletteOp = DragDropEvent.GetOperationAs<FLexUIPaletteDragDropOp>())
	{
		TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
		if (!Editor.IsValid() || !EditorViewportClient.IsValid())return FReply::Unhandled();
		const FIntPoint Pixel = LexUIPrefabViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		ULexWidget* Parent = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		if (!Parent)return FReply::Unhandled();
		FVector DropWorldPosition = FVector::ZeroVector;
		const bool bHasPosition = EditorViewportClient->GetDropWorldPosition(Pixel.X, Pixel.Y, Parent, DropWorldPosition);
		// A viewport drop names a parent and a position, never a sibling order -- append.
		ULexWidget* Created = PaletteOp->CreateUnder(Parent, TOptional<int32>(),
			[bHasPosition, DropWorldPosition](ULexWidget* Widget)
		{
			if (Widget && bHasPosition)Widget->SetWorldLocation(DropWorldPosition);
		});
		EditorViewportClient->ClearPaletteDropPreview();
		if (Created)
		{
			ULexUIManagerWorldSubsystem::GetInstance(Editor->GetWorld())->MarkLexUIWidgetOutlinerChanged();
			EditorViewportClient->Invalidate();
			return FReply::Handled();
		}
	}
	if (DragDropEvent.GetOperationAs<FAssetDragDropOp>().IsValid())
	{
		TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
		if (!Editor.IsValid() || !EditorViewportClient.IsValid())return FReply::Unhandled();
		const FIntPoint Pixel = LexUIPrefabViewportLocal::ToViewportPixel(MyGeometry, DragDropEvent.GetScreenSpacePosition(), EditorViewportClient->Viewport);
		ULexWidget* Parent = EditorViewportClient->GetDropContainerUnderCursor(Pixel.X, Pixel.Y);
		EditorViewportClient->ClearPaletteDropPreview();
		if (!Parent)return FReply::Unhandled();
		// The same entry point the hierarchy row uses, so a Content-Browser prefab becomes a linked
		// sub-prefab here too -- with its cyclic, self-nesting and version guards intact.
		const FReply Reply = Editor->TryHandleAssetDragDropOperation(DragDropEvent, Parent);
		if (Reply.IsEventHandled())EditorViewportClient->Invalidate();
		return Reply;
	}
	return SEditorViewport::OnDrop(MyGeometry, DragDropEvent);
}

void SLexUIPrefabEditorViewport::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	if (EditorViewportClient.IsValid())EditorViewportClient->ClearPaletteDropPreview();
	SEditorViewport::OnDragLeave(DragDropEvent);
}

TSharedRef<SEditorViewport> SLexUIPrefabEditorViewport::GetViewportWidget()
{
	return SharedThis(this);
}
TSharedPtr<FExtender> SLexUIPrefabEditorViewport::GetExtenders() const
{
	TSharedPtr<FExtender> Result(MakeShareable(new FExtender));
	return Result;
}
void SLexUIPrefabEditorViewport::OnFloatingButtonClicked()
{

}

#undef LOCTEXT_NAMESPACE
