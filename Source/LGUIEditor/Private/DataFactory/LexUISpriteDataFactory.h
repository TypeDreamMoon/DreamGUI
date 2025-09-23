// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "LexUISpriteDataFactory.generated.h"

UCLASS()
class ULexUISpriteDataFactory : public UFactory
{
	GENERATED_BODY()
public:
	ULexUISpriteDataFactory();

	class UTexture2D* SpriteTexture = nullptr;
	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
