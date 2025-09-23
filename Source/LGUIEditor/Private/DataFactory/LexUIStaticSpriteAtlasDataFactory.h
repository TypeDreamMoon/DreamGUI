// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "LexUIStaticSpriteAtlasDataFactory.generated.h"

UCLASS()
class ULexUIStaticSpriteAtlasDataFactory : public UFactory
{
	GENERATED_BODY()
public:
	ULexUIStaticSpriteAtlasDataFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
