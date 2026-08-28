// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "SEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class FDreamWidgetBlueprintEditor;
class FDreamUIPrefabEditorViewportClient;

//Encapsulates a simple scene setup for preview or thumbnail rendering.
class SDreamUIPrefabEditorViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{
public:
	SLATE_BEGIN_ARGS(SDreamUIPrefabEditorViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FDreamWidgetBlueprintEditor> InPrefabEditor, EViewModeIndex InViewMode);

	// SEditorViewport interface
	virtual void BindCommands() override;
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
	virtual EVisibility GetTransformToolbarVisibility() const override;
	virtual void OnFocusViewportToSelection() override;
	virtual FReply OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	virtual void OnDragLeave(const FDragDropEvent& DragDropEvent) override;
	// End of SEditorViewport interface

	// ICommonEditorViewportToolbarInfoProvider interface
	virtual TSharedRef<class SEditorViewport> GetViewportWidget() override;
	virtual TSharedPtr<FExtender> GetExtenders() const override;
	virtual void OnFloatingButtonClicked() override;
	// End of ICommonEditorViewportToolbarInfoProvider interface
	TSharedPtr<FDreamWidgetBlueprintEditor> GetPrefabEditor() const { return PrefabEditorPtr.Pin(); }
	bool SummonContextMenu();

private:
	// Pointer back to owning sprite editor instance (the keeper of state)
	TWeakPtr<FDreamWidgetBlueprintEditor> PrefabEditorPtr;
	EViewModeIndex ViewMode = EViewModeIndex::VMI_Lit;

	// Viewport client
	TSharedPtr<FDreamUIPrefabEditorViewportClient> EditorViewportClient;
};
