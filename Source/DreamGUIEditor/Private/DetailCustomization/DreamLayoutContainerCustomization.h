// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FDreamLayoutContainerCustomization : public IDetailCustomization
{
public:
	FDreamLayoutContainerCustomization();
	~FDreamLayoutContainerCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class UDreamLayoutContainer>> TargetScriptArray;
};
