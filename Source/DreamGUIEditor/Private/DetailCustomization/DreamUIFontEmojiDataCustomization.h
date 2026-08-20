// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamUIFontEmojiDataCustomization : public IDetailCustomization
{
public:
	FDreamUIFontEmojiDataCustomization();
	~FDreamUIFontEmojiDataCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UDreamUIFontEmojiData> TargetScriptPtr;
};
