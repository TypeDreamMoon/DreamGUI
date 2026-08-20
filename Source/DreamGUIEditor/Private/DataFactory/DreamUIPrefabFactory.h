// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Factories/Factory.h"
#include "DreamUIPrefabFactory.generated.h"

UCLASS()
class UDreamUIPrefabFactory : public UFactory
{
	GENERATED_BODY()
public:
	UDreamUIPrefabFactory();

	class UDreamUIPrefab* SourcePrefab = nullptr;
	// UFactory interface
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface

private:
	UPROPERTY(Transient)
	TObjectPtr<UClass> RootLayoutClass = nullptr;
};
