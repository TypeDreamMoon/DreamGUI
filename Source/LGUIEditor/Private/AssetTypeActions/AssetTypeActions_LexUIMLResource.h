// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetTypeActions_Base.h"

class ULexUIMLResource;

class FAssetTypeActions_LexUIMLResource : public FAssetTypeActions_Base
{
public:
	FAssetTypeActions_LexUIMLResource(EAssetTypeCategories::Type InAssetType);

	// FAssetTypeActions_Base overrides
	virtual FText GetName() const override;
	virtual FColor GetTypeColor() const override { return FColor(180, 130, 200); }
	virtual UClass* GetSupportedClass() const override;
	virtual bool HasActions(const TArray<UObject*>& InObjects) const override { return true; }
	virtual uint32 GetCategories() override;
	virtual bool CanFilter() override;

private:
	EAssetTypeCategories::Type AssetType;
};
