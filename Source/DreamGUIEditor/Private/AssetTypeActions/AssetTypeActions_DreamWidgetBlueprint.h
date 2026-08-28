// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"
#include "Toolkits/IToolkitHost.h"

/**
 * Opening a UDreamWidgetBlueprint gets the designer, not the stock Blueprint window.
 *
 * Registered rather than inherited: without this the asset falls through to FAssetTypeActions_Blueprint
 * (the nearest registered base) and opens with a graph and no design surface -- which is what happened
 * for as long as the toolkit was still an FAssetEditorToolkit and could not host the graph itself.
 */
class FAssetTypeActions_DreamWidgetBlueprint : public FAssetTypeActions_Base
{
public:
	explicit FAssetTypeActions_DreamWidgetBlueprint(EAssetTypeCategories::Type InAssetCategory);

	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor(44, 89, 180); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return AssetCategory; }
	virtual bool CanFilter() override { return true; }
	virtual void OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor = TSharedPtr<IToolkitHost>()) override;

private:
	EAssetTypeCategories::Type AssetCategory;
};
