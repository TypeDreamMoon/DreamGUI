// Copyright 2019-present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "DreamUIFontDataDistanceFieldFactory.generated.h"

UCLASS()
class UDreamUIFontDataDistanceFieldFactory : public UFactory
{
	GENERATED_BODY()
public:
	UDreamUIFontDataDistanceFieldFactory();

	TWeakObjectPtr<class UFontFace> SourceFont = nullptr;
	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
