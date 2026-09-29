// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIActionTypes.generated.h"

/*
 * What a widget holds on to when it listens for an input action, apart from the router that resolves
 * the action. UDreamUserWidget::ListenForInputAction hands these back, so they are part of the core's
 * surface; the router itself belongs to the input system and is reached through UDreamUIInputServices.
 */

/** Identifies one live binding. Handed back by RegisterAction and used to take it away again. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIActionHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI-Navigation")
	int32 Id = INDEX_NONE;

	bool IsValidHandle()const{ return Id != INDEX_NONE; }
	bool operator==(const FDreamUIActionHandle& Other)const{ return Id == Other.Id; }
};

DECLARE_DYNAMIC_DELEGATE(FDreamUIActionExecutedDelegate);
