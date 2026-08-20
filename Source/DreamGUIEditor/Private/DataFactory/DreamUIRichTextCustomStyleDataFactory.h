// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "DreamUIRichTextCustomStyleDataFactory.generated.h"

UCLASS()
class UDreamUIRichTextCustomStyleDataFactory : public UFactory
{
	GENERATED_BODY()
public:
	UDreamUIRichTextCustomStyleDataFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
