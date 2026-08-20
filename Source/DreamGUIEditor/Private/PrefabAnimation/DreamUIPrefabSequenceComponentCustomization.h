// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "Input/Reply.h"

class UDreamUIPrefabSequenceComponent;

class FDreamUIPrefabSequenceComponentCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:

	TWeakObjectPtr<UDreamUIPrefabSequenceComponent> WeakSequenceComponent;
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
