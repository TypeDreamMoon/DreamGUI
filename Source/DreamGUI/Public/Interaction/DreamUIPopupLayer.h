// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "DreamUIPopupLayer.generated.h"

class UDreamWidget;

/**
 * The screen-top layer transient UI is lifted to: dropdown lists, and whatever menus come later.
 *
 * UMG's combo list lives in the Slate menu stack -- a popup is not a child of the control that
 * opened it, which is what keeps it from being clipped by an ancestor's bounds or counted by an
 * ancestor's layout. This is that idea for a mesh UI, kept deliberately small: the popup STAYS the
 * widget it already was, and the layer only moves it.
 *
 * Elevate reparents the widget under the screen root with its world position kept, so whatever
 * positioning its owner ran while it was still a child -- UUIDropdown::Show anchors its list
 * against the face -- survives the move pixel for pixel. Restore hands it back to the parent it
 * came from; owners that re-position on every open (Show does) need nothing else.
 *
 * What this deliberately does not do: input blocking (UUIDropdown's blocker already covers it),
 * stacking of nested menus, or following a moving anchor while open. A scrolling face with an open
 * list keeps the list where it was -- acceptable for a dropdown, and the honest cost of not
 * re-deriving position per frame.
 */
UCLASS()
class DREAMGUI_API UDreamUIPopupLayer : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UDreamUIPopupLayer* Get(const UObject* InWorldContext);

	/**
	 * Lift InWidget to the screen root, keeping its on-screen position. Safe to call on a widget
	 * already lifted (a re-open re-anchored it under its owner first only if it was restored, so a
	 * second Elevate with no Restore in between is a no-op). Returns false when there is no screen
	 * root to lift to.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Popup")
	bool Elevate(UDreamWidget* InWidget);

	/** Hand a lifted widget back to the parent Elevate took it from. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Popup")
	void Restore(UDreamWidget* InWidget);

private:
	/**
	 * Who a lifted widget belongs to, for the trip home.
	 *
	 * Keyed by FObjectKey rather than by the widget itself: the key used to be a TObjectPtr in a
	 * UPROPERTY map, which is a STRONG reference, so a popup destroyed without a Restore -- its
	 * owner torn down while the list was open -- stayed alive for as long as the world subsystem
	 * did, with nothing left that could ever come and collect it. Both halves are non-owning now
	 * (an FObjectKey is an identity, not a reference), which also means the map no longer needs to
	 * be reflected at all.
	 */
	TMap<FObjectKey, TWeakObjectPtr<UDreamWidget>> ElevatedHomes;
};
