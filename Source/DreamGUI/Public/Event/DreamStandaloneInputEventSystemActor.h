// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "DreamStandaloneInputEventSystemActor.generated.h"

class UDreamStandaloneInputModule;

/**
 * A ready-to-place event system driven by the engine's legacy key bindings.
 *
 * This is the C++ form of the DreamEventSystemActor preset Blueprint, which wired the same keys by
 * hand in its event graph. Everything it did lives here: the input module component, the BeginPlay
 * registration, and one binding per key.
 *
 * Placing this needs no input setup in the project -- AutoReceiveInput claims player 0 and the keys
 * are bound directly rather than through action mappings, which is what makes the preset useful as a
 * drop-in. A project that already owns its input should use the Enhanced Input variant instead.
 */
UCLASS(ClassGroup = DreamGUI)
class DREAMGUI_API ADreamStandaloneInputEventSystemActor : public ADreamEventSystemActor
{
	GENERATED_BODY()

public:
	ADreamStandaloneInputEventSystemActor();

protected:
	virtual void BeginPlay() override;

	/** The module every binding below feeds. Registered with the event system on BeginPlay. */
	UPROPERTY(Category = "DreamGUI", VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDreamStandaloneInputModule> InputModule;

	/** Bind every key this preset listens to. Calls the two halves below. */
	virtual void BindDreamInput();

	/**
	 * Mouse buttons, wheel and movement.
	 *
	 * Separate from the navigation half because that is exactly the seam between the two presets: the
	 * Enhanced Input variant replaces this and keeps everything else.
	 */
	virtual void BindMouseInput();

	/** Navigation keys and touch, which both presets bind the same legacy way. */
	virtual void BindNavigationAndTouchInput();

	/**
	 * One AnyKey binding, so a named action can live on any key without the preset knowing which.
	 *
	 * Navigation keys are deliberately skipped here and routed from their own handlers instead. Input
	 * gives no ordering guarantee between an AnyKey binding and a specific one, so a key that both saw
	 * would be offered to the router twice and a bound action would fire twice.
	 */
	virtual void BindActionRouting();

	/** True for a key this preset already binds by name, and which therefore routes itself. */
	static bool IsNavigationKey(const FKey& Key);

	/**
	 * Offer Key to the action router for this actor's player.
	 * @return true when a bound action took it, and the preset's own meaning for the key must not run.
	 */
	bool RouteActionKey(const FKey& Key, bool bPressed);

	/**
	 * Which navigation direction a key stands for, or None if it is not a navigation key.
	 *
	 * The preset Blueprint left every InputNavigation call on None, which made press and release
	 * indistinguishable -- UDreamStandaloneInputModule::InputNavigation writes the argument straight
	 * into NavigateDirection on press and None on release, so directional navigation never moved.
	 * Keeping the mapping in one place is what stops that from being expressible again.
	 */
	static EDreamUINavigationDirection GetNavigationDirectionForKey(const FKey& Key);

	/** Current mouse position as the module reports it, as a 3D vector for the pointer API. */
	FVector GetPointerPosition() const;

	/**
	 * Tell the event system which device Key belongs to. Called from every bound handler: the actor is
	 * the only place a real key is still in hand, and everything downstream sees pointer positions and
	 * navigation directions with no trace of what produced them.
	 */
	void ReportDeviceForKey(const FKey& Key);

private:
	void OnMouseButtonPressed(FKey Key);
	void OnMouseButtonReleased(FKey Key);
	void OnMouseMoved(FVector AxisValue);
	void OnMouseWheel(float AxisValue);

	void OnTouchPressed(ETouchIndex::Type FingerIndex, FVector Location);
	void OnTouchReleased(ETouchIndex::Type FingerIndex, FVector Location);
	void OnTouchMoved(ETouchIndex::Type FingerIndex, FVector Location);

	void OnAnyKeyPressed(FKey Key);
	void OnAnyKeyReleased(FKey Key);

	void OnNavigationTriggerPressed(FKey Key);
	void OnNavigationTriggerReleased(FKey Key);
	void OnNavigationDirectionPressed(FKey Key);
	void OnNavigationDirectionReleased(FKey Key);
};
