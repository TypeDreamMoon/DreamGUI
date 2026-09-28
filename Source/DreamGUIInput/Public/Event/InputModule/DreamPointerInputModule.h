// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/InputModule/DreamBaseInputModule.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerInputModule.generated.h"

class UDreamBaseRaycaster;
class UDreamUIInputUser;
class AActor;

/**
 * The pointer state machine: what one pointer's frame dispatches -- hover, press, release, click, double click,
 * long press, drag and drop, swipe -- given what its trace hit.
 *
 * Every step dispatches through a player (UDreamUIInputUser), which keeps the state, counts the dispatch and
 * broadcasts. There used to be a second road through all of it, taken with no event system at all -- seventeen
 * branches by which a render-target surface drove its own synthesised pointer without broadcasts, a click run or a
 * player; a surface is served by this same road now (IDreamUINestedSurface).
 */
UCLASS(Abstract)
class DREAMGUIINPUT_API UDreamPointerInputModule : public UDreamBaseInputModule
{
	GENERATED_BODY()

public:
	/** One pointer's frame, given its trace. The heart of the pipeline; see UDreamUIInputUser::RunPipeline. */
	static void ProcessPointerEvent(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData, bool bLineTraceHitSomething, const FDreamUIHitResultContainer& InHitResult, bool& OutIsHitSomething, FDreamUIHitResult& OutHitResult);

	/** Exit what the pointer left and enter what it arrived at, outermost first, exits before enters. */
	static void ProcessPointerEnterExit(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData, UDreamWidget* InOld, UDreamWidget* InNew);
	/** find a common root actor of two actors. return nullptr if no common root */
	static UDreamWidget* FindCommonRoot(UDreamWidget* A, UDreamWidget* B);

	// The steps of ProcessPointerEvent for a hit that is not a widget's -- the actor behind a world hit
	// (UDreamBaseRaycaster::GetWorldHitComponent), told through the player's CallOnWorldTarget*. See
	// FDreamUIPointerWorldTarget for what is dispatched and why the rules are the widget's.

	/** The actor behind InHit, when InHit is a world hit its raycaster recognises as its own; null otherwise. */
	static AActor* ResolveWorldTarget(const FDreamUIHitResultContainer& InHit, bool bInHitSomething);
	/** Exit the actor the pointer was over, unless it is InStillOver. Before any widget is entered. */
	static void ExitWorldTargetUnless(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData, const AActor* InStillOver);
	/** Enter InNowOver, unless the pointer is over it already. After any widget has been exited. */
	static void EnterWorldTarget(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData, AActor* InNowOver);
	/** A press edge with no widget under the pointer: press the actor it is over, if any. */
	static void PressWorldTarget(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData, const FDreamUIHitResultContainer& InHit);
	/** A held press: the long press, for a press that landed on an actor. @return true if the press is an actor's. */
	static bool HoldWorldTarget(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData);
	/** Release the actor the press landed on: its up, then its click when bInClick. Marks this frame's Up as sent. */
	static void ReleaseWorldTarget(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData, bool bInClick);

	static bool CanHandleInterface(UDreamWidget* InWidget, UClass* InInterfaceClass);
	static UDreamWidget* GetEventHandle(UDreamWidget* InWidget, UClass* InInterfaceClass);

protected:
	/**
	 * Decide whether the press that has just ended was a swipe, and dispatch it if so. Called from the release
	 * branch of ProcessPointerEvent, before PressWidget is cleared: a swipe is a statement about a whole press, and
	 * that is the last moment both of its ends are known.
	 */
	static void DetectSwipeGesture(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData);
	/** Push the hovered widget's Cursor to the player's controller. See DreamPointerPolicy. */
	static void ApplyHoverCursor(UDreamUIInputUser* InUser, UDreamPointerEventData* InEventData);
	static void DeselectIfSelectionChanged(UDreamUIInputUser* InUser, UDreamWidget* InPressed, UDreamBaseEventData* InEventData);
};
