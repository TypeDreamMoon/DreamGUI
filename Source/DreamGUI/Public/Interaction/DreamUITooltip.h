// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/Interface.h"
#include "DreamUITooltip.generated.h"

class UDreamWidget;
class UDreamUserWidget;
class UDreamEventSystem;
class UDreamBaseEventData;
class UDreamPointerEventData;
class UDreamText;

/**
 * Implemented by a widget's behaviour (or the widget itself) that wants a WIDGET as its tooltip
 * rather than the built-in text bubble. The class is instanced when the tooltip shows and destroyed
 * when it hides; it receives no context in v1 -- a tooltip that needs data should read it from the
 * world in its own logic.
 */
UINTERFACE(MinimalAPI, Blueprintable, Category = DreamGUI)
class UDreamUITooltipSourceInterface : public UInterface
{
	GENERATED_BODY()
};

class DREAMGUI_API IDreamUITooltipSourceInterface
{
	GENERATED_BODY()
public:
	/** The user widget class to show as this widget's tooltip. Null means "use ToolTipText". */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	TSubclassOf<UDreamUserWidget> GetTooltipWidgetClass();
};

/**
 * Pure decisions of the tooltip pipeline, pulled out of the subsystem so they are testable without
 * a world -- the same convention DreamPointerPolicy documents for the pointer pipeline.
 */
namespace DreamUITooltipPolicy
{
	/**
	 * The widget whose tooltip the pointer position asks for: InEnterWidget or its nearest ancestor
	 * that either carries a non-empty ToolTipText or implements the source interface (itself or on a
	 * behaviour). Null when nothing on the path offers one.
	 */
	DREAMGUI_API UDreamWidget* ResolveTooltipSource(UDreamWidget* InEnterWidget);

	/**
	 * The widget the bubble should be parented to for a tooltip about InSource.
	 *
	 * The screen root for screen-space and render-target UI, and the SOURCE's own root canvas widget
	 * for world-space UI. The screen root was the only answer this ever had, which put the tooltip of
	 * a panel welded to a machine in the level onto the player's HUD, at a position computed from a
	 * pointer position no world-space raycaster fills in meaningfully.
	 */
	DREAMGUI_API UDreamWidget* ResolveTooltipHost(UDreamWidget* InSource, UDreamWidget* InScreenRoot);

	/**
	 * Where the bubble's PIVOT goes, in the same 2D space the inputs are in (X right, Y up).
	 * Prefers below-right of the pointer by InOffset (a negative Y offset reads "below"); flips to
	 * the other side of the pointer on the axes where the bubble would leave InCanvasMin..Max, then
	 * clamps outright for a bubble bigger than the canvas. Pivot is the bubble's TOP-LEFT corner.
	 */
	DREAMGUI_API FVector2D ComputeTooltipTopLeft(const FVector2D& InCanvasMin, const FVector2D& InCanvasMax,
		const FVector2D& InBubbleSize, const FVector2D& InPointer, const FVector2D& InOffset);
}

/**
 * Renders ToolTipText. The field, its localization and its FieldNotify entry all existed; nothing
 * in the framework ever DREW one until this.
 *
 * One service per world, driven by the event system's own broadcasts rather than per-widget opt-in:
 * hover-enter arms a dwell timer, the timer shows a bubble (or the widget class the source's
 * interface names), the bubble follows the live pointer, and exit / press / drag / input-type
 * change hides it. The bubble lives on its own canvas above the screen stack's sort band and is
 * raycast-disabled throughout -- a tooltip that can steal the pointer hides itself forever.
 */
UCLASS()
class DREAMGUI_API UDreamUITooltipSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamUI Tooltip Subsystem"), Category = "DreamGUI|Tooltip")
	static UDreamUITooltipSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/**
	 * Ticks while the game is paused, because a pause menu is exactly where tooltips are read.
	 *
	 * FTickableGameObject answers false by default, which silently opted this service out of the
	 * contract the rest of the framework keeps: the event system component sets bTickEvenWhenPaused,
	 * the screen-space raycaster has a setting for it, and UDreamUIManagerWorldSubsystem overrides
	 * this very function to true. Input kept arriving while paused and only the dwell timer stopped,
	 * so the bubble never appeared.
	 */
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** Hide whatever is showing and restart the dwell. For code that just changed what is under the pointer. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Tooltip")
	void HideTooltip();

	/**
	 * Show InSource's tooltip right now, as though its dwell had just elapsed. Does nothing for a
	 * source that offers neither ToolTipText nor a tooltip widget class.
	 *
	 * The dwell path is the ordinary one; this exists for code that already knows the player is
	 * asking for help on something -- a help key, a focus change -- and for tests, which otherwise
	 * have no way to put a bubble on screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Tooltip")
	void ShowTooltipFor(UDreamWidget* InSource);

	/** The widget the visible tooltip belongs to, or null while none shows. */
	UDreamWidget* GetShownFor() const { return ShownFor.Get(); }

private:
	void EnsureSubscribed();
	void HandleInputEvent(UDreamBaseEventData* InEventData);

	void ShowFor(UDreamWidget* InSource);
	/** Size the built-in bubble to its text's preferred size; safe to call before the text can answer. */
	void SizeBubbleToText();
	void UpdateTooltipPosition();
	/**
	 * Where the bubble points, in InHost's own local 2D space.
	 *
	 * The pointer's position when a pointer armed this tooltip and the host is a screen overlay; the
	 * SOURCE widget's own rect otherwise -- which covers both the gamepad, where there is no pointer,
	 * and a world-space canvas, where viewport pixels mean nothing.
	 * @return false when there is nothing to point at, in which case the bubble is left where it was.
	 */
	bool ResolveTooltipAnchor(UDreamWidget* InHost, UDreamWidget* InSource, FVector2D& OutAnchor) const;
	void DestroyTooltipWidgets();

	/** The event system observed, so a late-spawned or replaced one is picked up. */
	TWeakObjectPtr<UDreamEventSystem> SubscribedEventSystem;
	/** The pointer's event data object -- mutated in place by the input module, so it IS the live position. */
	TWeakObjectPtr<UDreamPointerEventData> LastPointerEvent;

	/** What the dwell timer is armed for. */
	TWeakObjectPtr<UDreamWidget> Candidate;
	float HoverSeconds = 0.0f;
	/** Set from press/drag; a new hover-enter re-arms. */
	bool bSuppressed = false;
	/**
	 * Whether the current candidate was armed by navigation rather than by a pointer.
	 *
	 * Kept because it is the only thing that separates the two afterwards: both arrive as an enter on
	 * the same path, and the difference shows up in where the bubble is placed -- against the focused
	 * widget for a gamepad, against the pointer for a mouse.
	 */
	bool bArmedByNavigation = false;

	/** What the visible tooltip belongs to. */
	TWeakObjectPtr<UDreamWidget> ShownFor;
	/** The canvas widget the bubble is parented to: a screen root, or a world-space canvas. */
	TWeakObjectPtr<UDreamWidget> TooltipHost;

	/** The positioned widget: its own canvas, raycast-disabled, parented to the screen root. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> TooltipHolder;
	/** The built-in bubble's text visual, when the text path is showing. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamText> BubbleText;
	/** The instanced custom tooltip, when the interface path is showing. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamUserWidget> CustomTooltip;
};
