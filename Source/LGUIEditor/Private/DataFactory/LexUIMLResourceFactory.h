// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "LexUIMLResourceFactory.generated.h"

UCLASS()
class ULexUIMLResourceFactory : public UFactory
{
	GENERATED_BODY()
public:
	ULexUIMLResourceFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
