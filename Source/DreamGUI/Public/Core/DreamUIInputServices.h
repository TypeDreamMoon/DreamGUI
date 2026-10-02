// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/DreamUIActionTypes.h"
#include "DreamUIInputServices.generated.h"

class UDreamCanvas;
class UDreamDragDropOperation;
class UDreamPointerEventData;
class UDreamWidget;
enum class EDreamInteractionKind : uint8;
struct FDataTableRowHandle;

/**
 * Everything the core asks of the input system, and the only way it asks.
 *
 * The event systems, the raycasters' setup, the action router and drag and drop belong to the input system.
 * The core states here what it needs from them, and the input system's subclass answers. A world the
 * input system provides nothing for gets no subclass: Get answers null, and every caller reads that the way
 * it always read a world without an event system -- no focus, no hover, no capture, nothing heard.
 *
 * Abstract, so the engine never creates this class itself: GetSubsystem finds the concrete subclass. The
 * bodies below are the answers of a world with no input at all.
 */
UCLASS(Abstract)
class DREAMGUI_API UDreamUIInputServices : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** InWorldContext's world's input services, or null when nothing provides them there. */
	static UDreamUIInputServices* Get(const UObject* InWorldContext);

	// ---- focus, hover and capture, for UDreamWidget

	/** Focus InWidget for player InUserIndex's pointer InPointerId. True when an event system took it. */
	virtual bool SetFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) { return false; }
	virtual bool HasFocus(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) const { return false; }
	/** Take focus away from InWidget, if player InUserIndex's pointer InPointerId has it there. */
	virtual void ClearFocus(UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerId) {}
	/** Whether any of player InUserIndex's pointers focuses something inside InWidget -- not InWidget itself. */
	virtual bool HasFocusedDescendant(const UDreamWidget* InWidget, int32 InUserIndex) const { return false; }
	/** Whether a pointer of player InUserIndex is over InWidget or something inside it. */
	virtual bool IsHovered(const UDreamWidget* InWidget, int32 InUserIndex) const { return false; }
	/** Whether player InUserIndex's pointer InPointerIndex -- any of them, when negative -- holds InWidget captured. */
	virtual bool HasMouseCapture(const UDreamWidget* InWidget, int32 InUserIndex, int32 InPointerIndex) const { return false; }

	// ---- players and their focus, for FDreamFocusReturn and the popup-like controls

	/**
	 * Every player the input system has in this world, in player order: local players and script players (a test rig's
	 * second player) alike. What anything that has to visit every player's focus walks -- not the game instance's local
	 * players, which leave the script players out.
	 */
	virtual void GetUserIndices(TArray<int32>& OutUserIndices) const {}
	/** Player InUserIndex's focus now -- whichever of their pointers set it, since focus is the player's -- or null. */
	virtual UDreamWidget* GetFocusedWidget(int32 InUserIndex) const { return nullptr; }
	/**
	 * Focus InWidget for player InUserIndex the way a directional move does: select it AND move the player's navigation
	 * cursor onto it, so the next move starts there. Unlike UDreamWidget::SetFocus it does not ask bIsFocusable. It refuses
	 * a widget that is not usable -- not in play, inactive, not drawn, not interactable, or whose selectable is not
	 * interactable or not navigable -- and then changes nothing. True when focus is on InWidget afterwards.
	 */
	virtual bool FocusForNavigation(UDreamWidget* InWidget, int32 InUserIndex) { return false; }
	/**
	 * Where player InUserIndex's active navigation scope wants focus (its remembered focus, its authored target, else its
	 * first navigable control), as that selectable's widget; null without an active scope or a target.
	 */
	virtual UDreamWidget* ResolveScopeFocusTarget(int32 InUserIndex) const { return nullptr; }

	// ---- pointers

	/** Player InUserIndex's pointer InPointerId as the event system tracks it, or null. Never creates one. */
	virtual UDreamPointerEventData* FindPointer(int32 InUserIndex, int32 InPointerId) const { return nullptr; }

	// ---- input actions, for UDreamUserWidget::ListenForInputAction

	/** Whether this world has anything that hears input actions. */
	virtual bool CanListenForActions() const { return false; }
	/** Listen for InAction on behalf of InOwner, live only while the screen InOwner is inside is in front. */
	virtual FDreamUIActionHandle RegisterWidgetAction(UDreamWidget* InOwner, const FDataTableRowHandle& InAction,
		FDreamUIActionExecutedDelegate InCallback, int32 InUserIndex, bool bInDisplayInActionBar) { return FDreamUIActionHandle(); }
	virtual void UnregisterAction(const FDreamUIActionHandle& InHandle) {}

	// ---- drag and drop, for UDreamUIWidgetLibrary

	virtual bool IsDragDropping() const { return false; }
	virtual UDreamDragDropOperation* GetDragOperationForPointer(int32 InPointerId) const { return nullptr; }
	/** Cancel the drag in progress, if there is one. True when one was cancelled. */
	virtual bool CancelActiveDrag() { return false; }

	// ---- getting a player ready to point at DreamUI

	/**
	 * Give local player InUserIndex what it takes to point at DreamUI: an event system, and a raycaster of
	 * InKind. Idempotent; a world-space host asks on every BeginPlay.
	 */
	virtual void EnsureInteractionForPlayer(int32 InUserIndex, EDreamInteractionKind InKind) {}
	/** The same for a screen page, and point that player's screen raycasters at InRootCanvas. */
	virtual void PrepareScreenInteraction(UDreamCanvas* InRootCanvas, int32 InUserIndex) {}
};
