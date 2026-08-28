// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamUIPrefabToClassCommandlet.generated.h"

/**
 * Converts prefab assets into hierarchy classes, on disk, without opening the editor.
 *
 * A driver over DreamUIPrefabToClass and nothing more: it finds the prefabs, calls the converter,
 * checks fidelity, saves what passed, and reports. Every decision it could have made lives in the
 * library instead, which is what lets the conversion be tested without running a commandlet.
 *
 *   -Source=/DreamGUI/Prefabs      package path to convert (required)
 *   -Dest=/DreamGUI/Controls       where the classes go (defaults to Source)
 *   -Prefix=BP_                    asset name prefix (defaults to BP_)
 *   -DryRun                        convert and verify, save nothing
 *
 * Nothing is overwritten: a destination that already exists is skipped and reported, so a re-run
 * after a partial conversion is safe and so a hand-edited class is never silently replaced.
 */
UCLASS()
class DREAMGUIEDITOR_API UDreamUIPrefabToClassCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamUIPrefabToClassCommandlet();

	virtual int32 Main(const FString& Params) override;
};
