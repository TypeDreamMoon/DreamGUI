// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamSpriteCustomization : public IDetailCustomization
{
public:
	FDreamSpriteCustomization();
	~FDreamSpriteCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TWeakObjectPtr<class UDreamSprite> TargetScriptPtr;
	/** Weak, because the delegate that calls this outlives the layout that registered it. */
	void ForceRefresh(TWeakPtr<class IPropertyUtilities> InPropertyUtilities);

};
