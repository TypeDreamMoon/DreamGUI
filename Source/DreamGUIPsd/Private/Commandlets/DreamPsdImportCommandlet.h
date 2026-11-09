// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamPsdImportCommandlet.generated.h"

/**
 * Imports a layered document without opening the editor:
 *
 *   UnrealEditor-Cmd.exe <project> -run=DreamPsdImport -Psd=<file> [-Folder=/Game/UI] [-Name=<name>] [-Dui=<file>]
 *       [-NoBlueprint] [-NoSave] [-DryRun] [-Dump] [-Report=<file>]
 *
 * -Dump prints the document as the reader sees it (every layer, its kind, blocks and masks) and imports nothing.
 * -Report writes the import's report to a file. Exit code 0 when the import succeeded.
 *
 *   UnrealEditor-Cmd.exe <project> -run=DreamPsdImport -WriteSample=<file>
 *
 * writes an original sample document that exercises what the importer maps, to try it on.
 */
UCLASS()
class UDreamPsdImportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamPsdImportCommandlet();
	virtual int32 Main(const FString& InParams) override;
};
