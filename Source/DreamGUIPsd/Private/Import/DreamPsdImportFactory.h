// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "DreamPsdImportFactory.generated.h"

/**
 * The Content Browser's Import for .psd and .psb, as a DreamUI widget: only while UDreamPsdImportSettings says so
 * (bHandleContentBrowserImport), so that importing a .psd as a texture keeps working as it always has. The folder
 * menu's import does not depend on it.
 */
UCLASS()
class UDreamPsdImportFactory : public UFactory
{
	GENERATED_BODY()

public:
	UDreamPsdImportFactory();

	virtual bool FactoryCanImport(const FString& InFilename) override;
	virtual UObject* FactoryCreateFile(UClass* InClass, UObject* InParent, FName InName, EObjectFlags InFlags, const FString& InFilename,
		const TCHAR* InParms, FFeedbackContext* InWarn, bool& bOutOperationCanceled) override;
};
