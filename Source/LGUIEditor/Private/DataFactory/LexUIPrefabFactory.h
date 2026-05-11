// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "LexUIPrefabFactory.generated.h"

UCLASS()
class ULexUIPrefabFactory : public UFactory
{
	GENERATED_BODY()
public:
	ULexUIPrefabFactory();

	class ULexUIPrefab* SourcePrefab = nullptr;
	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
