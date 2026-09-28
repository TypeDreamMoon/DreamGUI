// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamDelegateDeclaration.h"
#include "Event/DreamPointerEventData.h"
#include "DreamUIInputTypes.generated.h"

class AActor;
class UDreamWidget;

DECLARE_MULTICAST_DELEGATE_TwoParams(FDreamUIPointerInputTypeChangedDelegate, int, EDreamUIPointerInputType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamUIPointerInputChangedDynamicDelegate, int, PointID, EDreamUIPointerInputType, InputType);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FDreamUIRaycastHitDelegate, bool, const FDreamUIHitResult&, UDreamWidget*);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDreamUIRaycastHitDynamicDelegate, bool, IsHit, const FDreamUIHitResult&, HitResult, UDreamWidget*, HitObject);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIBaseEventDataDynamicDelegate, UDreamBaseEventData*, Data);

/**
 * What the player last touched. Not what the game is configured for -- what their hands are on right
 * now, which is the only thing a key prompt can honestly be drawn from.
 */
UENUM(BlueprintType)
enum class EDreamUIInputDevice : uint8
{
	MouseAndKeyboard,
	Gamepad,
	Touch,
};

/**
 * Which pad the player is holding, for prompts that need to say A or Cross.
 *
 * Deliberately separate from EDreamUIInputDevice rather than more entries in it: the device answers
 * "which prompt table", the model answers "which glyph within it", and folding the two together would
 * renumber an enum that is already saved in project assets. Generic is the honest answer whenever the
 * platform does not name the hardware, and every icon lookup falls back to it.
 */
UENUM(BlueprintType)
enum class EDreamUIGamepadModel : uint8
{
	/** A pad the platform did not name, or no pad at all. Xbox-style glyphs are the usual fallback. */
	Generic,
	Xbox,
	PlayStation,
	Switch,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIInputDeviceChangedDynamicDelegate, EDreamUIInputDevice, Device);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIInputDeviceChangedDelegate, EDreamUIInputDevice);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIGamepadModelChangedDynamicDelegate, EDreamUIGamepadModel, Model);
DECLARE_MULTICAST_DELEGATE_OneParam(FDreamUIGamepadModelChangedDelegate, EDreamUIGamepadModel);

/**
 * What one pointer is doing to the world beyond the widgets.
 *
 * A world raycaster's hit on a primitive carries no widget (UDreamBaseRaycaster::RaycastWorld), so none of
 * UDreamPointerEventData's widget fields can hold it. The actor behind that hit is still something the
 * pointer is over, presses and clicks, and it is told so through the same pointer interfaces a widget's
 * behaviours implement: on the actor itself and on each of its components that implements them, bubbling
 * to the actor it is attached to when every handler allows it, as a widget's event bubbles to its parent.
 * It is what LGUI did with a hit component's actor.
 *
 * Hover, press, release, click, double click, long press and scroll follow the widget's rules and the
 * widget's moments in UDreamPointerInputModule::ProcessPointerEvent: every Enter is matched by one Exit,
 * exits go out before enters whichever kind of target is being left, a press is released to the actor it
 * landed on wherever the pointer is by then, and the second press of a quick pair is the double click in
 * place of its down. Drags, drops, swipes and pinches stay widgets' only.
 *
 * Kept per pointer id beside the user's pointers, and dropped with them.
 */
struct FDreamUIPointerWorldTarget
{
	/** The actor behind the world hit the pointer is over. */
	TWeakObjectPtr<AActor> Hovered;
	/** The actor the pointer's current press landed on. */
	TWeakObjectPtr<AActor> Pressed;
	/**
	 * The actor the pointer's last click landed on, and that click's ClickTime. A second press continues the
	 * click run only when the pointer's latest click is still this one -- a widget clicked in between took
	 * the run over, even if that widget has since been destroyed.
	 */
	TWeakObjectPtr<AActor> LastClicked;
	double LastClickedTime = 0.0;
};

/**
 * One player's input settings: what an event system's details panel says for its player. The event system
 * placed for a player writes its own into its player when it begins play; a player nobody placed one for
 * reads these defaults, which are the event system's own.
 */
struct FDreamUIInputUserConfig
{
	/** Whether pointers are traced and dispatched at all. UDreamEventSystem::SetRaycastEnable. */
	bool bRayEventEnable = true;
	/** Let hovered widgets drive the hardware cursor (UDreamWidget::Cursor). */
	bool bApplyHoverCursor = true;
	EDreamUIPointerInputType DefaultInputType = EDreamUIPointerInputType::Pointer;
	float NavigateInputIntervalForFirstTime = 0.5f;
	float NavigateInputInterval = 0.2f;
	float DoubleClickTime = 0.3f;
	float LongPressTime = 0.5f;
	float SwipeMinDistance = 80.0f;
	float SwipeMaxDuration = 0.5f;
	float PinchMinDistanceChange = 12.0f;
	bool bScrollNavigationTargetIntoView = true;
	bool bAnimateNavigationScroll = true;
	/** Log every dispatched event. Read in editor builds only. */
	bool bOutputLog = false;
};

/**
 * Pointer ids: the ranges each kind of pointer is numbered in.
 *
 * The mouse and the first finger used to be the same pointer 0, and a navigation "pointer" shared it too:
 * a tap and a mouse on one device fought over one pointer, and lifting the finger retired the pointer the
 * mouse was hovering and selecting with. Each kind has its own range now.
 */
namespace DreamUIPointerIds
{
	/** The mouse, and the virtual cursor that stands in for it. */
	constexpr int32 Mouse = 0;
	/** A finger is TouchBase plus its index. */
	constexpr int32 TouchBase = 100;
	/** Ids a project or a test makes up for pointers of its own start here. */
	constexpr int32 ScriptBase = 1000;

	/** The pointer id for finger InFingerIndex, honouring UDreamGUISettings::bLegacyTouchPointerIds. */
	DREAMGUIINPUT_API int32 ForTouch(int32 InFingerIndex);
}

namespace DreamUIInputClock
{
	/**
	 * The UI clock's step this frame: the world's real delta, which neither a pause nor time dilation touches. What
	 * every timer of the input system and its services counts on -- a tooltip's dwell, a hold-to-confirm, the
	 * virtual cursor's speed, a stick scrolling a list -- so that a slowed-down game does not make a hold take ten
	 * times as long while a double click stays the same, and a paused game's menu still works.
	 *
	 * InTickDeltaSeconds, the delta a tick was handed, stands in when the world has no real delta at all: a world
	 * nothing ticks, driven by a test's hand.
	 */
	DREAMGUIINPUT_API float GetUIDeltaSeconds(const UObject* InWorldContext, float InTickDeltaSeconds);
}
