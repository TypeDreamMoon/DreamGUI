// Copyright 2019-present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "LexUIFontDataDistanceFieldFactory.generated.h"

UCLASS()
class ULexUIFontDataDistanceFieldFactory : public UFactory
{
	GENERATED_BODY()
public:
	ULexUIFontDataDistanceFieldFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
