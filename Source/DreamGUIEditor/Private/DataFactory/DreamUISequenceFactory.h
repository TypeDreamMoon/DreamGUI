// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Factories/Factory.h"
#include "AssetTypeActions_Base.h"
#include "DreamUISequenceFactory.generated.h"

UCLASS()
class UDreamUISequenceFactory : public UFactory
{
	GENERATED_BODY()
public:
	UDreamUISequenceFactory();

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};

class FAssetTypeActions_DreamUISequence : public FAssetTypeActions_Base
{
public:
	FAssetTypeActions_DreamUISequence(EAssetTypeCategories::Type InAssetCategory) : AssetCategory(InAssetCategory) {}

	virtual FText GetName() const override { return NSLOCTEXT("DreamUISequence", "AssetTypeActionsName", "DreamUI Animation"); }
	virtual FColor GetTypeColor() const override { return FColor(41, 98, 255); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override { return AssetCategory; }

private:
	EAssetTypeCategories::Type AssetCategory;
};
