// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"
#include "Event/DreamEventSystem.h"
#include "DreamUIInputAction.generated.h"

class UTexture2D;

/**
 * One named thing a screen can be asked to do -- Confirm, Back, Delete Save -- with the key it is
 * spelled as on each device and the words to put next to that key.
 *
 * Navigation keys used to be a static array in the input actor's cpp, which meant a project could not
 * add an action, rename one, or draw a prompt for one without editing the plugin. A row here is the
 * unit a screen binds a callback to and a prompt bar reads its label and icon from, so those two can
 * never disagree about what the player is being told to press.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIInputActionData : public FTableRowBase
{
	GENERATED_BODY()

	/** Shown beside the key on a prompt bar. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	FKey KeyboardKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	FKey GamepadKey;

	/** Optional glyph for the key. Without one a bar falls back to the key's own display name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	TSoftObjectPtr<UTexture2D> KeyboardIcon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	TSoftObjectPtr<UTexture2D> GamepadIcon;

	/** Seconds the key must be held before this fires. Zero fires the moment it goes down. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction", meta = (ClampMin = "0.0"))
	float HoldTime = 0.0f;

	/** Offer this to a prompt bar while it is bound. Off for something the player should not be told about. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	bool bDisplayInActionBar = true;

	/**
	 * The key to show for InDevice. Touch has no keys at all, so it gets an invalid one and a prompt
	 * bar drops the entry rather than telling a phone player to press Enter.
	 */
	FKey GetKeyForDevice(EDreamUIInputDevice InDevice)const;
	/** The glyph to show for InDevice, which may legitimately be unset. */
	TSoftObjectPtr<UTexture2D> GetIconForDevice(EDreamUIInputDevice InDevice)const;
	/** True when InKey is how this action is spelled on any device. */
	bool MatchesKey(const FKey& InKey)const;
};
