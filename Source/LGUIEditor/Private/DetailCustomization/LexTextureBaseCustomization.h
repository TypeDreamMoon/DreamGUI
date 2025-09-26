// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexTextureBaseCustomization : public IDetailCustomization
{
public:
	FLexTextureBaseCustomization();
	~FLexTextureBaseCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class ULexTextureBase>> TargetScriptArray;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);
};
