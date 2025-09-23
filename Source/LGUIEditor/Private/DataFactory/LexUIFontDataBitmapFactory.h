// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "LexUIFontDataBitmapFactory.generated.h"

UCLASS()
class ULexUIFontDataBitmapFactory : public UFactory
{
	GENERATED_BODY()
public:
	ULexUIFontDataBitmapFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
