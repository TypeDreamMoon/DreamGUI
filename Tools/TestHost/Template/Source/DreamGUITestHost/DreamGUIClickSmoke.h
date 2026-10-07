// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "DreamGUIClickSmoke.generated.h"

/**
 * The click smoke probe: in a game started with -DreamGUIClickSmoke=<directory> -- the editor binary with -game on uncooked
 * content, or a packaged build -- it puts a DreamGUI button on the player's screen once the player is up, moves the mouse
 * onto the button's pixels, presses and lets go there, then does the same well away from the button, every event through
 * FSlateApplication's own entry points (ProcessMouseMoveEvent, ProcessMouseButtonDownEvent, ProcessMouseButtonUpEvent), the
 * ones the platform's mouse messages go into. It writes ClickSmoke.json into the directory -- what the button announced
 * after each step, and where the viewport and the player saw the mouse -- and asks the game to exit.
 * Tools/Tests/check_click_smoke.py holds a run to it: hovered, pressed, released and clicked once, in that order, and the
 * click away from it clicking nothing. Tools/TestHost/README.md has the commands.
 */
namespace DreamGUIClickSmoke
{
	/** Start the probe when the command line asks for it, in a game: never in an editor session or a commandlet. */
	void StartIfAsked();
	/** Take the probe down, if it is up. */
	void Stop();
}

/** What the smoke's button announced: each of its public events counted, and every one of them in the order it came. */
UCLASS(Transient)
class UDreamGUIClickSmokeListener : public UObject
{
	GENERATED_BODY()

public:
	int32 Hovered = 0;
	int32 Unhovered = 0;
	int32 Pressed = 0;
	int32 Released = 0;
	int32 Clicked = 0;
	TArray<FString> Order;

	UFUNCTION()
	void HandleHovered() { ++Hovered; Order.Add(TEXT("Hovered")); }

	UFUNCTION()
	void HandleUnhovered() { ++Unhovered; Order.Add(TEXT("Unhovered")); }

	UFUNCTION()
	void HandlePressed() { ++Pressed; Order.Add(TEXT("Pressed")); }

	UFUNCTION()
	void HandleReleased() { ++Released; Order.Add(TEXT("Released")); }

	UFUNCTION()
	void HandleClicked() { ++Clicked; Order.Add(TEXT("Clicked")); }
};
