// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "DreamUIRichTextImageDataFactory.generated.h"

UCLASS()
class UDreamUIRichTextImageDataFactory : public UFactory
{
	GENERATED_BODY()
public:
	UDreamUIRichTextImageDataFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
