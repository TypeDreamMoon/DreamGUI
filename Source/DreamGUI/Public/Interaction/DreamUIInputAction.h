// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "InputCoreTypes.h"
#include "Event/DreamEventSystem.h"
#include "DreamUIInputAction.generated.h"

class UTexture2D;
class UInputAction;

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

	/**
	 * Modifier keys this action is spelled with -- what makes Ctrl+S and Shift+Tab expressible at all.
	 * They qualify the keyboard key only; a gamepad has no modifiers to hold.
	 *
	 * Required, not exclusive. An action that authors none still fires with a modifier incidentally
	 * held, which is what keeps every binding that predates these flags behaving exactly as it did.
	 * When two actions both match the key, the one requiring more modifiers wins, so adding Ctrl+S to a
	 * project that already binds S does not silently break S -- and does not lose to it either.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	bool bRequiresShift = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	bool bRequiresCtrl = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	bool bRequiresAlt = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	bool bRequiresCmd = false;

	/** Optional glyph for the key. Without one a bar falls back to the key's own display name. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	TSoftObjectPtr<UTexture2D> KeyboardIcon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	TSoftObjectPtr<UTexture2D> GamepadIcon;
	/**
	 * Per-pad glyphs, for a project that ships both an A button and a Cross.
	 *
	 * Optional in every sense: a model with no entry here falls back to GamepadIcon, which is what a
	 * project with one set of pad glyphs wants and is never wrong-brand.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	TMap<EDreamUIGamepadModel, TSoftObjectPtr<UTexture2D>> GamepadModelIcons;

	/**
	 * An Enhanced Input action this row also answers to.
	 *
	 * The bridge between the two ways a project can spell a key. The action table stays the authority:
	 * the keys typed above are matched first, and an action reached only through its Input Action loses
	 * to any row that names the same key outright (the router says so in the log when that happens).
	 * What this buys is a project that already defines its keys in mapping contexts -- it can bind a
	 * screen action to the Input Action and get prompts and routing without typing the keys twice.
	 *
	 * The keys behind it are resolved once per binding, from the local player's mapping contexts. A
	 * project that swaps contexts at runtime calls UDreamUIActionRouter::RefreshInputActionKeys.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI-InputAction")
	TSoftObjectPtr<UInputAction> InputAction;

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
	/**
	 * The glyph to show for InDevice, which may legitimately be unset.
	 * @param InGamepadModel	Which pad, for a row that ships per-model glyphs. Falls back to GamepadIcon.
	 */
	TSoftObjectPtr<UTexture2D> GetIconForDevice(EDreamUIInputDevice InDevice, EDreamUIGamepadModel InGamepadModel = EDreamUIGamepadModel::Generic)const;
	/**
	 * True when InKey is how this action is spelled on any device, and the modifiers it requires are
	 * among those held. The four flags default to "nothing held", so a caller that does not know the
	 * modifier state gets the pre-modifier behaviour for an action that requires none.
	 */
	bool MatchesKey(const FKey& InKey, bool bShiftDown = false, bool bCtrlDown = false, bool bAltDown = false, bool bCmdDown = false)const;
	/** How many modifiers this action requires. Higher is more specific, and more specific wins. */
	int32 CountRequiredModifiers()const;
};
