// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
//
// `-run=DreamGUIHost`: the host a DreamMCP server starts when it needs no renderer (`-nullrhi -DreamMCPHost`).
// When it needs one it starts the windowless editor instead (`-DreamMCPHost -RenderOffscreen`, no -run=): a commandlet
// draws nothing, so this host always reports `render: false`. It lays screens out, all the same: layout is arithmetic
// and font metrics, and the UI manager it needs is made for the host's preview worlds (see DreamGUIMCPStage.h).
//
// It does nothing of its own but keep the game thread turning. The endpoints -- DreamGUI's, and every other Dream
// plugin's in the process -- start from their modules, because the process is a host (MCP/DreamGUIMCPService.h);
// this loop ticks the engine, the task graph and the core ticker their HTTP listeners and jobs run on, and the asset
// compilers. It returns when the engine is asked to exit: POST <route>/shutdown, the process that started it gone
// (both watched by the endpoint service), or Ctrl+C.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"

#include "DreamGUIHostCommandlet.generated.h"

UCLASS()
class UDreamGUIHostCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamGUIHostCommandlet();

	virtual int32 Main(const FString& Params) override;
};
