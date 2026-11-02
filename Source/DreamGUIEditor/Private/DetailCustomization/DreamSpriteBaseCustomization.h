// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamSpriteBaseCustomization : public IDetailCustomization
{
public:
	FDreamSpriteBaseCustomization();
	~FDreamSpriteBaseCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class UDreamSpriteBase>> TargetScriptArray;
	/** Weak, because the delegate that calls this outlives the layout that registered it. */
	void ForceRefresh(TWeakPtr<class IPropertyUtilities> InPropertyUtilities);
};
