// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class UDreamWidgetAnimationComponent;

class FDreamWidgetAnimationComponentCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	TWeakObjectPtr<UDreamWidgetAnimationComponent> WeakSequenceComponent;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
