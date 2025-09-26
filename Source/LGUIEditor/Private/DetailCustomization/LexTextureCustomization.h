// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexTextureCustomization : public IDetailCustomization
{
public:
	FLexTextureCustomization();
	~FLexTextureCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class ULexTexture> TargetScriptPtr;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);

};
