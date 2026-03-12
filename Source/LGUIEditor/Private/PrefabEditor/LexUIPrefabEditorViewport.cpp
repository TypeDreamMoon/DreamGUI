// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabEditorViewport.h"
#include "LexUIPrefabEditorViewportClient.h"
#include "LexUIPrefabEditor.h"
#include "LexUIPrefabEditorViewportToolbar.h"

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
	return EVisibility::Visible;
}
void SLexUIPrefabEditorViewport::OnFocusViewportToSelection()
{
	EditorViewportClient->FocusViewportToTargets();
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

FReply SLexUIPrefabEditorViewport::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return PrefabEditorPtr.Pin()->TryHandleAssetDragDropOperation(DragDropEvent);
}

#undef LOCTEXT_NAMESPACE