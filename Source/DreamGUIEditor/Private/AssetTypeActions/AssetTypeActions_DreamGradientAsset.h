// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "AssetTypeActions_Base.h"

/** Dream Gradient assets (UDreamGradientAsset) in the content browser: their DreamUI category, colour, and a "Copy as CSS" action. */
class FAssetTypeActions_DreamGradientAsset : public FAssetTypeActions_Base
{
public:
	explicit FAssetTypeActions_DreamGradientAsset(EAssetTypeCategories::Type InAssetCategory);

	// FAssetTypeActions_Base overrides
	virtual bool CanFilter() override;
	virtual void GetActions(const TArray<UObject*>& InObjects, FMenuBuilder& MenuBuilder) override;
	virtual uint32 GetCategories() override;
	virtual FText GetName() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual FColor GetTypeColor() const override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override;

private:
	EAssetTypeCategories::Type AssetCategory;
};
