// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"

class FDreamWidgetBlueprintEditor;

/** Tab ids for the designer's own panels. Blueprint's own tabs come from FBlueprintEditorTabs. */
struct FDreamWidgetDesignerTabs
{
	static const FName ViewportID;
	static const FName HierarchyID;
	static const FName PaletteID;
	static const FName DetailsID;
	static const FName AnimationsID;
	/** The sequencer invokes this by its engine-wide id and fills it with the curve editor. */
	static const FName SequencerCurvesID;
};

/**
 * One dockable designer panel.
 *
 * Deliberately one class taking a content-provider rather than six near-identical FWorkflowTabFactory
 * subclasses (which is what UMG has, at ~35 lines each). What varies between these panels is an id, a
 * label, an icon and one line that builds the widget; a class per panel spends a file to say that.
 * The table in FDreamWidgetDesignerApplicationMode is then the whole list, in one place, which is the
 * property the FTabDescriptor array this replaces was worth keeping.
 */
struct FDreamWidgetDesignerTabSummoner : public FWorkflowTabFactory
{
public:
	using FMakeContent = TFunction<TSharedRef<SWidget>(FDreamWidgetBlueprintEditor&)>;

	FDreamWidgetDesignerTabSummoner(TSharedPtr<FDreamWidgetBlueprintEditor> InEditor, FName InTabID,
		const FText& InLabel, const FSlateIcon& InIcon, FMakeContent InMakeContent);

	/** Runs when the user closes this tab, for a panel that drives editor-wide state while it is open. */
	TFunction<void(FDreamWidgetBlueprintEditor&)> OnClosedCallback;

	virtual TSharedRef<SWidget> CreateTabBody(const FWorkflowTabSpawnInfo& Info) const override;
	/**
	 * FWorkflowTabFactory's own OnTabClosed hook is commented out in the engine, so the close
	 * callback is attached to the dock tab here instead.
	 */
	virtual TSharedRef<SDockTab> SpawnTab(const FWorkflowTabSpawnInfo& Info) const override;

protected:
	TWeakPtr<FDreamWidgetBlueprintEditor> DesignerEditor;
	FMakeContent MakeContent;
};
