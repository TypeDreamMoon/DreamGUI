// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "LexUIPrefabFactory.generated.h"

#define USE_CLASS_PICKER 0

UCLASS()
class ULexUIPrefabFactory : public UFactory
{
	GENERATED_BODY()
public:
	ULexUIPrefabFactory();

	class ULGUIPrefab* SourcePrefab = nullptr;
	UClass* RootActorClass = nullptr;
	// UFactory interface
#if USE_CLASS_PICKER
	virtual bool ConfigureProperties() override;
#endif
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
