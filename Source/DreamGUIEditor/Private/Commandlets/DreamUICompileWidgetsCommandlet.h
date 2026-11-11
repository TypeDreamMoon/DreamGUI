// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamUICompileWidgetsCommandlet.generated.h"

/**
 * Creates and compiles text-authored DreamUI Widget Blueprints from their `.dui` files without opening the editor --
 * the headless half of Tools > Rebuild DUI, for build scripts and CI.
 *
 *     UnrealEditor-Cmd.exe <project>.uproject -run=DreamUICompileWidgets -Manifest=<file.json> [-Only=WBP_A,WBP_B]
 *     UnrealEditor-Cmd.exe <project>.uproject -run=DreamUICompileWidgets -All [-Path=/Game/UI]
 *
 * The manifest lists the widgets in the order they must be compiled -- a component before the screens that place it:
 *
 *     { "widgets": [
 *         { "asset": "/Game/UI/WBP_Row",    "parent": "/Script/MyGame.MyRow",    "source": "UI/Row.dui" },
 *         { "asset": "/Game/UI/WBP_Screen", "parent": "/Script/DreamGUI.DreamTextUserWidget", "source": "UI/Screen.dui" } ] }
 *
 * A missing Blueprint is created with that parent; an existing one is reparented when its parent differs, pointed at the
 * source, compiled and saved. "parent" may be omitted (UDreamTextUserWidget). -All recompiles every text-authored
 * Widget Blueprint already in the project instead (optionally under -Path). Every compiler message is logged, and the
 * commandlet exits non-zero when any widget failed to load, compile or save.
 */
UCLASS()
class UDreamUICompileWidgetsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamUICompileWidgetsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
