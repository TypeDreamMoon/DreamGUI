// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Core/DreamUITextData.h"
#pragma once

/**
 * 
 */
class FDreamTextCustomization : public IDetailCustomization
{
public:
	FDreamTextCustomization();
	~FDreamTextCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UDreamText> TargetScriptPtr;
	TArray<TWeakObjectPtr<UMaterialInterface>> PresetMaterials;
	/** Takes the utilities, not the layout builder: the builder does not survive the refresh it asks for. */
	void ForceRefresh(TSharedPtr<class IPropertyUtilities> PropertyUtilities);
};
