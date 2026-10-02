// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"
#include "Core/DreamGUISettings.h"

class UDreamUIInputSubsystem;
class UDreamUIInputUser;
class UWorld;
struct FAnalogInputEvent;
struct FKeyEvent;
struct FPointerEvent;

/**
 * DreamGUI's input, heard from Slate itself: an input pre-processor, which sees the mouse, the keys, the sticks and
 * touch before the game viewport does -- and so before the viewport ignores input in a UI-only mode, where nothing
 * reaches the player controller the preset actors listen through. One per game world, made by the world's input
 * subsystem when UDreamGUISettings::bUseSlateInputSource is on; it answers only for events on its own world's
 * viewport, so two play sessions each hear their own -- and, as Slate routes them, not for a pointer over a UMG widget
 * drawn on top of it, nor for keys while a UMG widget holds the keyboard focus.
 *
 * It feeds the players' input exactly as the presets do -- moves are state, presses, releases and wheel turns are
 * queued for the player's next frame -- and a key goes the one road every source takes (DreamUIKeyRouting). Whether
 * the UI keeps an event from the game is UDreamGUISettings::SlateInputConsumePolicy; a key typed into a field being
 * edited is always kept, since typing is never anything else.
 */
class DREAMGUIINPUT_API FDreamUISlateInputSource : public IInputProcessor
{
public:
	explicit FDreamUISlateInputSource(UDreamUIInputSubsystem* InSubsystem);
	virtual ~FDreamUISlateInputSource();

	/**
	 * For tests: where on this world's viewport a point in screen space is, in viewport pixels -- or false when it is
	 * not on the viewport. By default the answer comes from the world's game viewport widget, which a headless world
	 * does not have.
	 */
	void SetViewportMapperForTesting(TFunction<bool(const FVector2D&, FVector2D&)> InMapper);
	/** For tests: which player a Slate user is. By default the local player whose Slate user it is. */
	void SetUserMapperForTesting(TFunction<int32(int32)> InMapper);
	/**
	 * For tests: whether a widget of Slate's own -- UMG drawn over the viewport -- is under a point in screen space, so
	 * that a pointer there is that widget's and not this world's. By default Slate's hit test answers, which a headless
	 * editor has painted nothing for; with a viewport mapper set and no answer here, nothing covers the viewport.
	 */
	void SetCoverMapperForTesting(TFunction<bool(const FVector2D&)> InMapper);
	/**
	 * For tests: whether a Slate user's keyboard focus is on this world's viewport itself. By default Slate's focus;
	 * with a viewport mapper set and no answer here, it is.
	 */
	void SetKeyboardFocusMapperForTesting(TFunction<bool(int32)> InMapper);
	/**
	 * For tests: whether the platform cursor is on this world's viewport, as the viewport itself last saw it. By default
	 * the game viewport's cached cursor, which FSceneViewport puts at (-1,-1) when the cursor leaves it; with a viewport
	 * mapper set and no answer here, it is.
	 */
	void SetCursorOnViewportMapperForTesting(TFunction<bool()> InMapper);

	/**
	 * The mouse put over nothing once this world's viewport says the cursor has left it, unless a button holds it. A
	 * move off the viewport does that as it is heard, but leaving a borderless or fullscreen window sends no move this
	 * source hears -- Slate synthesizes the moves that tell the viewport, and synthesized moves skip the input
	 * pre-processors -- and the mouse's pointer went on hovering the widget at the edge it went out by. Tick asks this
	 * every frame for the Slate user the mouse is mapped to.
	 */
	void FollowCursorOffViewport(int32 InMouseSlateUserIndex);

	/**
	 * The application going to the background or coming back, as FSlateApplication announces it. Going, every press
	 * this source holds is let go of -- the up, no click and no drop -- and every key it routed is released where its
	 * press went: the release happens in another application, and Slate never hears it.
	 */
	void HandleApplicationActivationStateChanged(bool bInIsActive);
	/** What the UI keeps from the game. Read from the settings when the source is made. */
	void SetConsumePolicy(EDreamUIInputConsumePolicy InPolicy) { ConsumePolicy = InPolicy; }
	EDreamUIInputConsumePolicy GetConsumePolicy() const { return ConsumePolicy; }

	//~ IInputProcessor
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override;
	virtual bool HandleAnalogInputEvent(FSlateApplication& SlateApp, const FAnalogInputEvent& InAnalogInputEvent) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override;
	virtual bool HandleMouseWheelOrGestureEvent(FSlateApplication& SlateApp, const FPointerEvent& InWheelEvent, const FPointerEvent* InGestureEvent) override;
	virtual const TCHAR* GetDebugName() const override { return TEXT("DreamUISlateInputSource"); }

private:
	UWorld* GetWorld() const;
	/** Which player of this world a Slate user is, or INDEX_NONE. */
	int32 FindUserIndex(int32 InSlateUserIndex) const;
	/** The player a Slate user is, made when it has no input yet; null when no player of this world is that user. */
	UDreamUIInputUser* FindUser(int32 InSlateUserIndex) const;
	/** Whether the platform cursor is on this world's viewport, as the viewport last saw it. */
	bool IsCursorOnViewport() const;
	/** InScreen in pixels of this world's viewport, and whether it is on it. False when this world has no viewport. */
	bool MapToViewport(const FVector2D& InScreen, FVector2D& OutPixel, bool& bOutInside) const;
	/**
	 * Whether what takes a pointer at InScreen is something other than this world's viewport itself -- a UMG widget drawn
	 * over it, another window -- so that the pointer is that widget's, and not this world's UI's.
	 */
	bool IsCoveredBySlate(const FVector2D& InScreen, int32 InSlateUserIndex) const;
	/**
	 * Whether a key event is this world's: its user's keyboard focus is on this world's viewport itself. A UMG widget
	 * holding the focus -- a text box in the viewport's overlay -- takes its own keys.
	 */
	bool IsKeyForThisWorld(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) const;
	/** The pointer a Slate pointer event moves: the mouse, or the finger's own. */
	static int32 PointerIDFor(const FPointerEvent& InEvent);
	/** Whether the real mouse moves InUser's mouse pointer: not while a virtual cursor stands in for it. */
	static bool FollowsTheMouse(const UDreamUIInputUser* InUser);
	/** Whether a press here is kept from the game, under the consume policy. */
	bool WouldConsumePress(const UDreamUIInputUser* InUser, int32 InPointerID) const;
	/** Whether a key the UI took, or did not, is kept from the game, under the consume policy. */
	bool WouldConsumeKey(bool bInTaken, bool bInTyped) const;
	bool HandlePress(const FPointerEvent& InEvent, bool bInPressed);

	TWeakObjectPtr<UDreamUIInputSubsystem> Subsystem;
	TFunction<bool(const FVector2D&, FVector2D&)> ViewportMapper;
	TFunction<int32(int32)> UserMapper;
	TFunction<bool(const FVector2D&)> CoverMapper;
	TFunction<bool(int32)> KeyboardFocusMapper;
	TFunction<bool()> CursorOnViewportMapper;
	/** FSlateApplication's activation announcement, heard from construction to destruction. */
	FDelegateHandle ActivationChangedHandle;
	EDreamUIInputConsumePolicy ConsumePolicy = EDreamUIInputConsumePolicy::Never;
	/** Presses this source queued, by player and pointer, until their releases: what a lost focus lets go of. */
	TSet<TPair<int32, int32>> HeldPresses;
	/** Presses this source kept from the game, by player and pointer: their releases are kept too, so the pair stays a pair. */
	TSet<TPair<int32, int32>> ConsumedPresses;
	/** Keys this source routed on their press, by player: their releases go the same way, wherever the focus is by then. */
	TSet<TPair<int32, FKey>> RoutedKeys;
	/** Keys this source kept from the game on their press, by player: their releases, and repeats, are kept too. */
	TSet<TPair<int32, FKey>> ConsumedKeys;
	/** Each player's right stick, as it last reported, for the scroll it drives every tick while held. */
	TMap<int32, FVector2D> RightSticks;
};
