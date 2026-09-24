// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/InputModule/DreamBaseInputModule.h"
#include "Event/DreamPointerEventData.h"
#include "DreamPointerInputModule.generated.h"

class UDreamBaseRaycaster;
class UDreamEventSystem;
class AActor;

UCLASS(Abstract)
class DREAMGUI_API UDreamPointerInputModule : public UDreamBaseInputModule
{
	GENERATED_BODY()

public:
	static void ProcessPointerEvent(UDreamEventSystem* eventSystem, UDreamPointerEventData* pointerEventData, bool pointerHitAnything, const FDreamUIHitResultContainer& hitResult, bool& OutIsHitSomething, FDreamUIHitResult& OutHitResult);

	/**
	 * Recognize a two-finger pinch from the pointers that are down right now. Called once per frame by
	 * the input module that pumps this one.
	 *
	 * Multi-touch has always produced one independent pointer per finger and nothing ever looked at two
	 * of them together, which is why pinch-to-zoom had to be hand-rolled by every project out of raw
	 * pointer positions. Exactly two pressed pointers make a pinch; a third finger ends it, because
	 * three fingers moving is not a pinch and guessing which two were meant would be worse than saying
	 * nothing.
	 */
	void ProcessPinchGesture();
protected:
	/**
	 * Decide whether the press that has just ended was a swipe, and dispatch it if so.
	 *
	 * Called from the release branch of ProcessPointerEvent, before PressWidget is cleared: a swipe is
	 * a statement about a whole press, and that is the last moment both of its ends are known.
	 */
	static void DetectSwipeGesture(UDreamEventSystem* eventSystem, UDreamPointerEventData* EventData);

	/** Pinch state. Lives only between the frame the second finger lands and the frame one leaves. */
	bool bPinchActive = false;
	double PinchStartDistance = 0.0;
	double PinchLastReportedDistance = 0.0;
	/** Reused rather than allocated per frame; a pinch reports on most frames it is alive. */
	UPROPERTY(Transient)
	TObjectPtr<class UDreamGestureEventData> PinchEventData = nullptr;

	
	bool LineTrace(UDreamPointerEventData* InPointerEventData, FDreamUIHitResultContainer& OutDreamHitResult);
	TArray<FDreamUIHitResultContainer> MultiHitResult;//temp array for hit result
	TArray<FDreamUIHitResult> HitResultArray;//temp array, one raycaster's hits; a member so its capacity survives the frame
	/**
	 * Push the hovered widget's Cursor to the player controller. See DreamPointerPolicy.
	 * @param InEventSystem	Whose cursor this is. Null falls back to the first controller, which is all
	 *						the static dispatch path (no event system at all) can offer.
	 */
	static void ApplyHoverCursor(UDreamEventSystem* InEventSystem, class UDreamPointerEventData* EventData);
	static void ProcessPointerEnterExit(UDreamEventSystem* eventSystem, UDreamPointerEventData* pointerEventData, UDreamWidget* oldObj, UDreamWidget* newObj);
	/** find a common root actor of two actors. return nullptr if no common root */
	static UDreamWidget* FindCommonRoot(UDreamWidget* A, UDreamWidget* B);

	// The steps of ProcessPointerEvent for a hit that is not a widget's -- the actor behind a world hit
	// (UDreamBaseRaycaster::GetWorldHitComponent), told through UDreamEventSystem's CallOnWorldTarget*.
	// See UDreamEventSystem::FDreamPointerWorldTarget for what is dispatched and why the rules are the
	// widget's. All of them need an event system to keep the state in; the static dispatch path (a
	// render-target surface driving its own pointer) never reaches them.

	/** The actor behind InHit, when InHit is a world hit its raycaster recognises as its own; null otherwise. */
	static AActor* ResolveWorldTarget(const FDreamUIHitResultContainer& InHit, bool bInHitSomething);
	/** Exit the actor the pointer was over, unless it is InStillOver. Before any widget is entered. */
	static void ExitWorldTargetUnless(UDreamEventSystem* InEventSystem, UDreamPointerEventData* InEventData, const AActor* InStillOver);
	/** Enter InNowOver, unless the pointer is over it already. After any widget has been exited. */
	static void EnterWorldTarget(UDreamEventSystem* InEventSystem, UDreamPointerEventData* InEventData, AActor* InNowOver);
	/** A press edge with no widget under the pointer: press the actor it is over, if any. */
	static void PressWorldTarget(UDreamEventSystem* InEventSystem, UDreamPointerEventData* InEventData, const FDreamUIHitResultContainer& InHit);
	/** A held press: the long press, for a press that landed on an actor. @return true if the press is an actor's. */
	static bool HoldWorldTarget(UDreamEventSystem* InEventSystem, UDreamPointerEventData* InEventData);
	/** Release the actor the press landed on: its up, then its click when bInClick. Marks this frame's Up as sent. */
	static void ReleaseWorldTarget(UDreamEventSystem* InEventSystem, UDreamPointerEventData* InEventData, bool bInClick);

	bool Navigate(EDreamUINavigationDirection InDirection, UDreamPointerEventData* InPointerEventData, FDreamUIHitResultContainer& hitResult);
	void ProcessInputForNavigation(UDreamPointerEventData* InPointerEventData);
	void ClearEventByID(int pointerID);
	static bool CanHandleInterface(UDreamWidget* targetComp, UClass* targetInterfaceClass);
	static UDreamWidget* GetEventHandle(UDreamWidget* targetComp, UClass* targetInterfaceClass);
	static void DeselectIfSelectionChanged(UDreamEventSystem* eventSystem, UDreamWidget* currentPressed, UDreamBaseEventData* EventData);
public:
	virtual void ClearEvent()override;
};