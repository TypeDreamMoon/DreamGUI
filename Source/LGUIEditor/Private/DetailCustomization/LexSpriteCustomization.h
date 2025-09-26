// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexSpriteCustomization : public IDetailCustomization
{
public:
	FLexSpriteCustomization();
	~FLexSpriteCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class ULexSprite> TargetScriptPtr;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);

};
