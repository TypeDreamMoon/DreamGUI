// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "DreamUISpriteDataFactory.generated.h"

UCLASS()
class UDreamUISpriteDataFactory : public UFactory
{
	GENERATED_BODY()
public:
	UDreamUISpriteDataFactory();

	TWeakObjectPtr<UTexture2D> SpriteTexture = nullptr;
	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
