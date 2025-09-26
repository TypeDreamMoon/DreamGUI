// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexSpriteBaseCustomization : public IDetailCustomization
{
public:
	FLexSpriteBaseCustomization();
	~FLexSpriteBaseCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class ULexSpriteBase>> TargetScriptArray;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);
};
