// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Core/LexUITextData.h"
#pragma once

/**
 * 
 */
class FLexTextCustomization : public IDetailCustomization
{
public:
	FLexTextCustomization();
	~FLexTextCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class ULexText> TargetScriptPtr;
	TArray<TWeakObjectPtr<UMaterialInterface>> PresetMaterials;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);
};
