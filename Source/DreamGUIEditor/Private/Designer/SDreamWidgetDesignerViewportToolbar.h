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

	// SCommonEditorViewportToolbarBase interface
	virtual TSharedRef<SWidget> GenerateShowMenu() const override;
	// End of SCommonEditorViewportToolbarBase

protected:
	// We override Construct (the base version is non-virtual and registers a globally-shared
	// tool menu), so we keep our own info-provider handle instead of relying on the base's.
	ICommonEditorViewportToolbarInfoProvider& GetInfoProvider() const;

private:
	TWeakPtr<class ICommonEditorViewportToolbarInfoProvider> InfoProviderWeakPtr;
};
