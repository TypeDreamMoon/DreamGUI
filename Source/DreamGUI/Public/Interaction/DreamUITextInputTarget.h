// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "DreamUITextInputTarget.generated.h"

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamUITextInputTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * A behaviour that takes typed text -- the text field -- as the input side sees it: where the game
 * viewport's characters go, and what Back does while an edit is going on. The input side reaches a field
 * through this and DreamUITextInputRouter, never by naming the control library's text input.
 */
class DREAMGUI_API IDreamUITextInputTarget
{
	GENERATED_BODY()

public:
	/** Whether this is being edited right now. */
	virtual bool IsTextInputActive() const = 0;

	/** Throw the edit away, the way Escape or Back does. */
	virtual void CancelTextInput() = 0;

	/** A character the platform resolved on the player's own keyboard layout. True when it was taken. */
	virtual bool InsertTextCharacter(TCHAR InCharacter) = 0;
};

/**
 * The one field that owns the keyboard. A field claims it when an edit starts and lets go when the edit
 * ends; a field that is destroyed lets go by itself.
 */
namespace DreamUITextInputRouter
{
	/** InTarget owns the keyboard from now on. It must implement IDreamUITextInputTarget. */
	DREAMGUI_API void SetActiveTarget(UObject* InTarget);

	/** InTarget lets go of the keyboard; nothing happens when another field owns it by now. */
	DREAMGUI_API void ClearActiveTarget(const UObject* InTarget);

	/** The field that owns the keyboard, or null. */
	DREAMGUI_API IDreamUITextInputTarget* GetActiveTarget();

	/** InCharacter to the field that owns the keyboard. True when one took it. */
	DREAMGUI_API bool RouteCharacter(TCHAR InCharacter);
}
