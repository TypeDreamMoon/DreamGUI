// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DreamUIWidgetGeometryLibrary.h"
#include "DreamUILayoutLibrary.generated.h"

class APlayerController;
class UDreamPanelSlot;
class UDreamWidget;

/**
 * The viewport, the mouse and the screen -- this framework's UWidgetLayoutLibrary.
 *
 * Everything here was reachable from C++ and from nowhere else: the viewport's size, its DPI scale,
 * the mouse position in the coordinates the UI actually uses, and where a world location lands on
 * screen. A Blueprint wanting to pin a marker to an enemy had to go through the UMG library, whose
 * answers are in Slate's spaces and not this framework's, and hope the two agreed.
 */
UCLASS()
class DREAMGUI_API UDreamUILayoutLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The game viewport's size in pixels. Zero when there is no viewport, e.g. on a dedicated server. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Viewport", meta = (WorldContext = "WorldContextObject"))
	static FVector2D GetViewportSize(UObject* WorldContextObject);

	/**
	 * The project's DPI scale for the current viewport size -- the number the whole UI is scaled by
	 * before it is shown. The same curve UMG reads, so a project that tuned it once is obeyed here.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Viewport", meta = (WorldContext = "WorldContextObject"))
	static float GetViewportScale(UObject* WorldContextObject);

	/** The mouse in viewport pixels, top-left zero. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Viewport", meta = (WorldContext = "WorldContextObject"))
	static bool GetMousePositionOnViewport(UObject* WorldContextObject, FVector2D& OutMousePosition);

	/**
	 * The mouse in the PLATFORM's coordinates -- desktop pixels, across every monitor.
	 *
	 * Different from GetMousePositionOnViewport by the window's own position, which is the whole
	 * reason UMG keeps both: a windowed game's viewport zero is not the desktop's zero.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Viewport")
	static bool GetMousePositionOnPlatform(FVector2D& OutMousePosition);

	/** The mouse in DPI-scaled viewport coordinates -- the space the UI is laid out in. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Viewport")
	static bool GetMousePositionScaledByDPI(APlayerController* InPlayer, FVector2D& OutMousePosition);

	/**
	 * Where a world location lands on screen, in viewport pixels -- what a health bar over an enemy
	 * needs. False when the location is behind the camera or there is no viewport.
	 *
	 * @param bPlayerViewportRelative	Measure from this player's own viewport corner rather than the
	 *									whole window's. What split screen needs.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Viewport")
	static bool ProjectWorldLocationToWidgetPosition(APlayerController* InPlayer, FVector InWorldLocation,
		FVector2D& OutScreenPosition, bool bPlayerViewportRelative = false);

	/** The same, and how far away it was. The distance is what a marker fades or sorts by. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Viewport")
	static bool ProjectWorldLocationToWidgetPositionWithDistance(APlayerController* InPlayer, FVector InWorldLocation,
		FVector2D& OutScreenPosition, float& OutDistance, bool bPlayerViewportRelative = false);

	/**
	 * The geometry of the whole screen for a player -- UMG's GetViewportWidgetGeometry and
	 * GetPlayerScreenWidgetGeometry, which are one function here because there is one screen root
	 * per local player and no shared layer behind them.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Viewport", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "InPlayer"))
	static FDreamUIWidgetGeometry GetPlayerScreenWidgetGeometry(UObject* WorldContextObject,
		APlayerController* InPlayer = nullptr);

	/**
	 * A widget's panel slot -- UMG's whole SlotAs* family in one node.
	 *
	 * UMG needs a dozen of them because each panel has its own slot class and a graph has to cast to
	 * the right one; here there is a single UDreamPanelSlot whatever the parent is, so there is
	 * nothing to choose between and nothing to get wrong. Null when the parent hands out no slots,
	 * which is a real and ordinary state -- see UDreamWidget::HasPanelSlots.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Layout")
	static UDreamPanelSlot* SlotAsPanelSlot(UDreamWidget* InWidget);

	/** Take every tracked page off the screen. UMG's RemoveAllWidgets. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Screen", meta = (WorldContext = "WorldContextObject"))
	static void RemoveAllWidgets(UObject* WorldContextObject);
};
