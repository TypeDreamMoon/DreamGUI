// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintEditorModes.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"

class FDreamWidgetBlueprintEditor;
class UDreamWidgetBlueprint;

/**
 * The two things a DreamUI hierarchy is: a layout, and a class.
 *
 * They are modes rather than tabs because they want the whole window. The prefab editor had only the
 * first, and the second lived in a separate asset -- the companion behaviour blueprint -- which is
 * precisely the separation the class model removes.
 */
struct FDreamWidgetBlueprintApplicationModes
{
	static const FName DesignerMode;
	static const FName GraphMode;

	static FText GetLocalizedMode(FName InMode);
};

/** Shared base: holds the designer editor and the mode's own tab factories. UMG's shape. */
class FDreamWidgetBlueprintApplicationMode : public FBlueprintEditorApplicationMode
{
public:
	FDreamWidgetBlueprintApplicationMode(TSharedPtr<FDreamWidgetBlueprintEditor> InEditor, FName InModeName);

	TSharedPtr<FDreamWidgetBlueprintEditor> GetDesignerEditor() const { return MyDesignerEditor.Pin(); }
	/** FBlueprintEditorApplicationMode keeps MyBlueprintEditor protected and hands out no accessor. */
	TSharedPtr<FBlueprintEditor> GetOwningBlueprintEditor() const { return MyBlueprintEditor.Pin(); }
	UDreamWidgetBlueprint* GetWidgetBlueprint() const;

protected:
	TWeakPtr<FDreamWidgetBlueprintEditor> MyDesignerEditor;
	FWorkflowAllowedTabSet TabFactories;
};

/** Viewport, hierarchy, palette, details, animations -- the panels the prefab editor already had. */
class FDreamWidgetDesignerApplicationMode : public FDreamWidgetBlueprintApplicationMode
{
public:
	explicit FDreamWidgetDesignerApplicationMode(TSharedPtr<FDreamWidgetBlueprintEditor> InEditor);

	virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;
};

/** The stock Blueprint graph, verbatim. Nothing here is DreamUI's business. */
class FDreamWidgetGraphApplicationMode : public FDreamWidgetBlueprintApplicationMode
{
public:
	explicit FDreamWidgetGraphApplicationMode(TSharedPtr<FDreamWidgetBlueprintEditor> InEditor);

	virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;
};
