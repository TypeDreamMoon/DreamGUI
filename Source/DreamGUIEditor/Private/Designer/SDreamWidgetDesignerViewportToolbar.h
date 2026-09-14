// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "SCommonEditorViewportToolbarBase.h"

// In-viewport toolbar used in the DreamUI designer
class SDreamWidgetDesignerViewportToolbar : public SCommonEditorViewportToolbarBase
{
public:
	SLATE_BEGIN_ARGS(SDreamWidgetDesignerViewportToolbar) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<class ICommonEditorViewportToolbarInfoProvider> InInfoProvider);

	/**
	 * The name this toolbar registers into the process-wide UToolMenus registry.
	 *
	 * Shared with the editor module so ShutdownModule can take the menu back out again: the registry
	 * outlives this module, and the entries hold delegates bound into it.
	 */
	static FName GetViewportToolbarMenuName();

	// GenerateShowMenu is deliberately NOT overridden. The override used to return an empty
	// FMenuBuilder, so anything that did reach it -- the base class builds a Show button for
	// toolbars it populates itself -- got a popup with nothing in it. The base's own implementation
	// is a real menu; this toolbar simply does not offer the button.

protected:
	// We override Construct (the base version is non-virtual and registers a globally-shared
	// tool menu), so we keep our own info-provider handle instead of relying on the base's.
	ICommonEditorViewportToolbarInfoProvider& GetInfoProvider() const;

private:
	TWeakPtr<class ICommonEditorViewportToolbarInfoProvider> InfoProviderWeakPtr;
};
