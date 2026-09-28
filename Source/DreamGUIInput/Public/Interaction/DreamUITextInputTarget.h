// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Interface.h"
#include "DreamUITextInputTarget.generated.h"

class APlayerController;
class UGameViewportClient;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UDreamUITextInputTarget : public UInterface
{
	GENERATED_BODY()
};

/**
 * A behaviour that takes typed text -- the text field -- as the input side sees it: where a player's characters and
 * keys go while it is being edited, and what Back does while an edit is going on. The input side reaches a field
 * through this and DreamUITextInputRouter, never by naming the control library's text input.
 */
class DREAMGUIINPUT_API IDreamUITextInputTarget
{
	GENERATED_BODY()

public:
	/** Whether this is being edited right now. */
	virtual bool IsTextInputActive() const = 0;

	/** Throw the edit away, the way Escape or Back does. */
	virtual void CancelTextInput() = 0;

	/** A character the platform resolved on the player's own keyboard layout. True when it was taken. */
	virtual bool InsertTextCharacter(TCHAR InCharacter) = 0;

	/**
	 * The keys this takes while it is being edited. They are bound on its player's controller, above everything else
	 * the controller listens to, for as long as the edit lasts -- so a key typed into a field reaches nothing else:
	 * not a shortcut, not the pawn.
	 */
	virtual void GetTextInputKeys(TArray<FKey>& OutKeys) const = 0;

	/**
	 * InKey, pressed or repeating on InPlayer's keyboard while this is being edited. The chord, and any other key held,
	 * are read from that player's own input. True when it was taken.
	 */
	virtual bool HandleTextInputKey(const FKey& InKey, const APlayerController* InPlayer) = 0;
};

/**
 * The field that owns each player's keyboard. A field claims its player's when an edit starts and lets go when the
 * edit ends; a field that is destroyed lets go by itself. Each player has their own, kept on that player's input in
 * the field's world: a second player's field does not take the first player's keys, and a field in one play session
 * does not take another's characters.
 */
namespace DreamUITextInputRouter
{
	/** InTarget owns player InUserIndex's keyboard in InTarget's world from now on. It must implement IDreamUITextInputTarget. */
	DREAMGUIINPUT_API void SetActiveTarget(UObject* InTarget, int32 InUserIndex);

	/** InTarget lets go of the keyboard it owns, whichever player's; nothing happens when it owns none by now. */
	DREAMGUIINPUT_API void ClearActiveTarget(const UObject* InTarget);

	/** The field that owns player InUserIndex's keyboard in InWorldContext's world, or null. */
	DREAMGUIINPUT_API IDreamUITextInputTarget* GetActiveTarget(const UObject* InWorldContext, int32 InUserIndex);

	/** InCharacter to the field that owns player InUserIndex's keyboard. True when one took it. */
	DREAMGUIINPUT_API bool RouteCharacter(const UObject* InWorldContext, int32 InUserIndex, TCHAR InCharacter);

	/**
	 * InCharacter, as a game viewport client's InputChar receives it for controller InControllerId: to the field that
	 * player is typing into, else as a KeyChar to what the player has focused. True when either took it.
	 *
	 * The whole contract for a project's own viewport client: call it from InputChar, after the console has had the
	 * character and before the base class's InputChar. UDreamGameViewportClient does exactly that.
	 */
	DREAMGUIINPUT_API bool RouteViewportCharacter(const UGameViewportClient* InViewportClient, int32 InControllerId, TCHAR InCharacter);

	/** Which player controller InControllerId of InViewportClient's game is: its local player's index, or 0 when no local player has it. */
	DREAMGUIINPUT_API int32 GetUserIndexForController(const UGameViewportClient* InViewportClient, int32 InControllerId);

	/** To the first player of the game viewport's world. */
	UE_DEPRECATED(5.8, "A character belongs to a player: call RouteViewportCharacter from a viewport client, or RouteCharacter with a world and a player.")
	DREAMGUIINPUT_API bool RouteCharacter(TCHAR InCharacter);
}
