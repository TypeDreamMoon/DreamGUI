// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamUIPrefabCustomization : public IDetailCustomization
{
public:

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UDreamUIPrefab> TargetScriptPtr;
	FText GetEngineVersionText()const;
	FText GetPrefabVersionText()const;
	FText GetPrefabSchemaVersionText()const;
	EVisibility ShouldShowFixEngineVersionButton()const;
	FSlateColor GetEngineVersionTextColorAndOpacity()const;
	FSlateColor GetPrefabVersionTextColorAndOpacity()const;
	FSlateColor GetPrefabSchemaVersionTextColorAndOpacity()const;
	EVisibility ShouldShowFixPrefabVersionButton()const;
	FText GetSchemaDiagnosticsText()const { return SchemaDiagnosticsText; }
	EVisibility GetSchemaDiagnosticsVisibility()const;

	FReply OnClickRecreteButton();
	FReply OnClickEditPrefabButton();
	FReply OnClickPreviewSchemaButton();
	FReply OnClickUpgradeSchemaButton();

	FText SchemaDiagnosticsText;
};
