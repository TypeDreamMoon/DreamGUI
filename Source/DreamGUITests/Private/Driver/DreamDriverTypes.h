// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamCanvas.h"   // EDreamCanvasScaleMode

/**
 * The small vocabulary every part of the driver shares: how a rig is built, and the words input
 * steps use that the runtime has no type for. Kept apart from the rig and the sequence so a piece
 * that only needs a word -- a game-host adapter, a PIE rig -- does not pull in either of them.
 */

/** Where the rig's input enters the pipeline. */
enum class EDreamRigInputHost : uint8
{
	/** Pointer, buttons, wheel, navigation and keys go straight into UDreamDriverInputModule and the edited text field (today's rig). */
	ModuleOnly,
	/** A real ADreamStandaloneInputEventSystemActor whose module is a UDreamDriverInputModule; buttons, wheel, navigation, touch and keys go through the PlayerController's input stack. */
	StandaloneActor,
	/** The same with ADreamEnhancedInputEventSystemActor and Enhanced Input mapping contexts. */
	EnhancedActor,
	/**
	 * The world's Slate input source (UDreamUIInputSubsystem::SetSlateInputSourceEnabled), the preset actors standing down:
	 * keys reach it as FKeyEvents through FDreamUISlateInputSource::HandleKeyDownEvent and HandleKeyUpEvent, pointers as
	 * FPointerEvents, characters through UDreamUIInputSubsystem::HandleViewportCharacter, as a game viewport's would. A
	 * headless rig gives the source its test mappers (the rig's viewport, keyboard focus on it, Slate user 0 is player 0).
	 */
	SlateSource,
};

/** Whether InHost delivers input through a player controller and one of the preset input actors. */
inline bool IsActorInputHost(EDreamRigInputHost InHost)
{
	return InHost == EDreamRigInputHost::StandaloneActor || InHost == EDreamRigInputHost::EnhancedActor;
}

/** Modifier keys a driver key step holds around its key (FDreamDriverSequence::Key). */
enum class EDreamDriverModifierKeys : uint8
{
	None = 0,
	Shift = 1 << 0,
	Ctrl = 1 << 1,
	Alt = 1 << 2,
	Cmd = 1 << 3,
};
ENUM_CLASS_FLAGS(EDreamDriverModifierKeys);

/** One phase of a finger's contact. */
enum class EDreamDriverTouchPhase : uint8
{
	Began,
	Moved,
	Ended,
};

/** How the rig's players share its viewport, when it has more than one (FDreamRigOptions::PlayerCount). */
enum class EDreamRigPlayerScreens : uint8
{
	/**
	 * One screen for all of them: every player points at the rig's own root canvas, each through a screen raycaster of its
	 * own carrying its UserIndex -- two mice on one desk, or a pad's virtual cursor beside the mouse.
	 */
	Shared,
	/**
	 * A split screen as the engine lays one out: every local player is given the part of the viewport UGameViewportClient::
	 * LayoutPlayers would give it (ULocalPlayer::Origin and Size, from the viewport client's SplitscreenInfo table), and its
	 * own screen-space root canvas with its own screen raycaster, as UDreamScreenUISubsystem gives every local player a root
	 * of its own -- given that player, so the screen is laid out over, hit in and drawn in the player's part alone. A
	 * world-space pointer of a player looks through that player's part of the viewport. Needs real local players, so only
	 * the actor hosts build it; see FDreamDriverRig.
	 */
	Split,
};

/** How a rig is built. Every default reproduces today's rig except bWithGameInstance. */
struct FDreamRigOptions
{
	FIntPoint ViewportSize = FIntPoint(1280, 720);
	/** The world belongs to a UGameInstance, so GameInstance subsystems (the tween manager) exist and the pump ticks them. false = a bare UWorld::CreateWorld world, exactly as before. */
	bool bWithGameInstance = true;
	/** Root canvas scaling. Unset = leave the canvas's own default. */
	TOptional<EDreamCanvasScaleMode> CanvasScaleMode;
	FVector2D ReferenceResolution = FVector2D(1280.0, 720.0);
	float MatchFromWidthToHeight = 1.0f;
	EDreamRigInputHost InputHost = EDreamRigInputHost::ModuleOnly;
	/**
	 * How many players the rig has, one to four. Player 0 is the rig's own and the one every step speaks for unless a
	 * sequence says otherwise (FDreamDriverSequence::AsPlayer); each further player has its own event system, input entry
	 * and screen raycaster, UserIndex 1, 2, 3. Under ModuleOnly they are script players the driver feeds by hand; under an
	 * actor host each is a real ULocalPlayer with its own APlayerController and input actor. SlateSource takes one player.
	 */
	int32 PlayerCount = 1;
	/** How several players share the viewport. Ignored with one. */
	EDreamRigPlayerScreens PlayerScreens = EDreamRigPlayerScreens::Shared;
	/**
	 * Under a split screen, whether every player's screen is the one UDreamScreenUISubsystem keeps for that player, as
	 * in a game whose screens come from AddToPlayerScreen and CreateWidgetOnScreen: player 0's is the rig's own root,
	 * which the screen UI adopts for its first player, and every other player's is the root the screen UI makes for it
	 * (given that player as the screen UI gives it; the rig adds only the substituted viewport and the scaler). So a
	 * widget put on a player's screen, and a tooltip or a popup the screen UI puts up for a widget there, land on the
	 * screen that player's raycaster projects through, and the screen UI never makes a second screen-space screen for a
	 * player who has one. Off, the rig makes those roots itself and the screen UI knows of none of them but player 0's,
	 * and only once asked. Ignored unless the rig is a split screen.
	 */
	bool bScreensFromScreenUI = false;
};

/** The most players a rig builds: UGameViewportClient::MaxSplitscreenPlayers' default, and the end of the engine's split-screen tables. */
constexpr int32 DreamRigMaxPlayers = 4;
