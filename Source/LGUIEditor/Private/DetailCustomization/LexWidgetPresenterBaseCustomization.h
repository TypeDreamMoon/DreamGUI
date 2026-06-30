// Copyright 2019-Present LexLiu. All Rights Reserved.
#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#pragma once

/**
 * 
 */
class FLexWidgetPresenterBaseCustomization : public IDetailCustomization
{
public:
	FLexWidgetPresenterBaseCustomization();
	~FLexWidgetPresenterBaseCustomization();

	static TSharedRef<IDetailCustomization> MakeInstance();
	/** IDetailCustomization interface */
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
private:
	TArray<TWeakObjectPtr<class ULexWidgetPresenterComponentBase>> TargetScriptArray;
	void ForceRefresh(IDetailLayoutBuilder* DetailBuilder);
};
