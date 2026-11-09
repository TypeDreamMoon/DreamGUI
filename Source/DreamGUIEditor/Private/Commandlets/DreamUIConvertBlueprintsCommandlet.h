// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamUIConvertBlueprintsCommandlet.generated.h"

/**
 * Converts hand-authored DreamUI Widget Blueprints to `.dui` in bulk, for a migration or a CI check.
 *
 *     UnrealEditor-Cmd.exe <project>.uproject -run=DreamUIConvertBlueprints
 *         (-Assets=/Game/UI/WBP_A,/Game/UI/WBP_B | -Path=/Game/UI) [-DryRun] [-AllowLoss]
 *
 * Each Blueprint gets the report the editor shows (FDreamUIBlueprintConverter): what cannot be converted, what would not
 * reach the file. -DryRun stops there and changes nothing. Otherwise each is converted to its default file under DUI/,
 * with its `_PreDui` backup, and both packages are saved -- except one with anything that would not reach the file,
 * which is skipped unless -AllowLoss says the loss is accepted. Exits non-zero when any Blueprint was skipped or failed.
 */
UCLASS()
class UDreamUIConvertBlueprintsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamUIConvertBlueprintsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
