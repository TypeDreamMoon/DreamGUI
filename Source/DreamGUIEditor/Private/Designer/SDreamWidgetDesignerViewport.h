// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once
#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SWidget.h"
#include "SEditorViewport.h"
#include "SCommonEditorViewportToolbarBase.h"

class FDreamWidgetBlueprintEditor;
class FDreamWidgetDesignerViewportClient;

//Encapsulates a simple scene setup for preview or thumbnail rendering.
class SDreamWidgetDesignerViewport : public SEditorViewport, public ICommonEditorViewportToolbarInfoProvider
{
public:
	SLATE_BEGIN_ARGS(SDreamWidgetDesignerViewport) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FDreamWidgetBlueprintEditor> InDesigner, EViewModeIndex InViewMode);

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
	TSharedPtr<FDreamWidgetBlueprintEditor> GetDesigner() const { return DesignerPtr.Pin(); }
	bool SummonContextMenu();

private:
	// Pointer back to owning sprite editor instance (the keeper of state)
	TWeakPtr<FDreamWidgetBlueprintEditor> DesignerPtr;
	EViewModeIndex ViewMode = EViewModeIndex::VMI_Lit;

	// Viewport client
	TSharedPtr<FDreamWidgetDesignerViewportClient> EditorViewportClient;

public:
	/** The client, for panels that talk to the design surface directly -- the hierarchy's hover. */
	TSharedPtr<FDreamWidgetDesignerViewportClient> GetViewportClient() const { return EditorViewportClient; }
};
