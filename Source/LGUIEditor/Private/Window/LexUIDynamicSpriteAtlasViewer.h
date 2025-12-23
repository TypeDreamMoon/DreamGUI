// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Widgets/SCompoundWidget.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Views/STableViewBase.h"
#pragma once

/**
 * 
 */
class SLexUIDynamicSpriteAtlasViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIDynamicSpriteAtlasViewer) {}
	
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<SDockTab> InOwnerTab);

private:
	void CloseTabCallback(TSharedRef<SDockTab> TabClosed);
};
