// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexUISpriteBaseCustomization : public IDetailCustomization
{
public:
	FLexUISpriteBaseCustomization();
	~FLexUISpriteBaseCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class UUISpriteBase>> TargetScriptArray;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);
};
