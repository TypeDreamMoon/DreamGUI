// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "DreamUIStaticSpriteAtlasDataFactory.generated.h"

UCLASS()
class UDreamUIStaticSpriteAtlasDataFactory : public UFactory
{
	GENERATED_BODY()
public:
	UDreamUIStaticSpriteAtlasDataFactory();

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
