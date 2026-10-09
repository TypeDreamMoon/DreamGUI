// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Core/DreamUIWorldService.h"
#include "UObject/Interface.h"
#include "DreamUITooltip.generated.h"

class UDreamWidget;
class UDreamUserWidget;
class UDreamEventSystem;
class UDreamUIInputSubsystem;
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

class DREAMGUIINPUT_API IDreamUITooltipSourceInterface
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
	DREAMGUIINPUT_API UDreamWidget* ResolveTooltipSource(UDreamWidget* InEnterWidget);

	/**
	 * The widget the bubble should be parented to for a tooltip about InSource.
	 *
	 * The screen root for screen-space and render-target UI, and the SOURCE's own root canvas widget
	 * for world-space UI. The screen root was the only answer this ever had, which put the tooltip of
	 * a panel welded to a machine in the level onto the player's HUD, at a position computed from a
	 * pointer position no world-space raycaster fills in meaningfully.
	 */
	DREAMGUIINPUT_API UDreamWidget* ResolveTooltipHost(UDreamWidget* InSource, UDreamWidget* InScreenRoot);

	/**
	 * Where the bubble's PIVOT goes, in the same 2D space the inputs are in (X right, Y up).
	 * Prefers below-right of the pointer by InOffset (a negative Y offset reads "below"); flips to
	 * the other side of the pointer on the axes where the bubble would leave InCanvasMin..Max, then
	 * clamps outright for a bubble bigger than the canvas. Pivot is the bubble's TOP-LEFT corner.
	 */
	DREAMGUIINPUT_API FVector2D ComputeTooltipTopLeft(const FVector2D& InCanvasMin, const FVector2D& InCanvasMax,
		const FVector2D& InBubbleSize, const FVector2D& InPointer, const FVector2D& InOffset);
}

/** One player's tooltip: what its dwell is armed for, and the bubble showing for it. */
USTRUCT()
struct FDreamUITooltipUserState
{
	GENERATED_BODY()

	/**
	 * The pointer this tooltip follows -- the player's pointer that last arrived at something with a tooltip, never a
	 * finger -- whose event data object is mutated in place by the pipeline, so it IS the live position.
	 */
	TWeakObjectPtr<UDreamPointerEventData> LastPointerEvent;
	/** What the dwell timer is armed for. */
	TWeakObjectPtr<UDreamWidget> Candidate;
	/** Set by the followed pointer's enters and exits: the candidate is read from what it is over at the end of the frame. */
	bool bCandidateStale = false;
	float HoverSeconds = 0.0f;
	/** Set from press/drag; a new hover-enter re-arms. */
	bool bSuppressed = false;
	/**
	 * Whether the current candidate was armed by navigation rather than by a pointer: the difference shows up in
	 * where the bubble is placed -- against the focused widget for a gamepad, against the pointer for a mouse.
	 */
	bool bArmedByNavigation = false;
	/** A show or hide supersedes any older operation still returning from a user callback. */
	uint64 OperationSerial = 0;
	/** What the visible tooltip belongs to. */
	TWeakObjectPtr<UDreamWidget> ShownFor;
	/** The canvas widget the bubble is parented to: a screen root, or a world-space canvas. */
	TWeakObjectPtr<UDreamWidget> TooltipHost;

	/** The positioned widget: its own canvas, raycast-disabled, parented to the host. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> TooltipHolder;
	/** The built-in bubble's text visual, when the text path is showing. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamText> BubbleText;
	/** The instanced custom tooltip, when the interface path is showing. */
	UPROPERTY(Transient)
	TObjectPtr<UDreamUserWidget> CustomTooltip;
};

/**
 * Renders ToolTipText. The field, its localization and its FieldNotify entry all existed; nothing in the framework
 * ever DREW one until this.
 *
 * One service per world, one tooltip per player, driven by the players' own events rather than per-widget opt-in:
 * hover-enter arms a dwell timer, the timer shows a bubble (or the widget class the source's interface names), the
 * bubble follows the live pointer, and exit / press / drag / input-type change hides it. The bubble lives on its
 * own canvas above the screen stack's sort band and is raycast-disabled throughout -- a tooltip that can steal the
 * pointer hides itself forever. Each player's tooltip is theirs: a second player's hover neither moves nor hides
 * the first player's bubble. Within a player it follows one pointer, the one that last arrived at something with a
 * tooltip; another of the player's pointers passing over nothing leaves it alone. A finger is never that pointer, as a
 * touch is never Slate's cursor: a tap or a held finger brings no bubble up, a finger dragged onto a widget arms none,
 * and a finger's press only takes down a bubble that is up and restarts its dwell.
 */
UCLASS()
class DREAMGUIINPUT_API UDreamUITooltipSubsystem : public UTickableWorldSubsystem, public IDreamUIWorldService
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamUI Tooltip Subsystem"), Category = "DreamGUI|Tooltip")
	static UDreamUITooltipSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual int32 GetTeardownPriority() const override { return DreamUI::WorldServiceTeardownPriority::Input; }
	virtual void TeardownForWorld(UWorld& InWorld) override;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/** Ticks while the game is paused, because a pause menu is exactly where tooltips are read. */
	virtual bool IsTickableWhenPaused() const override { return true; }

	/** Hide whatever is showing for every player and restart their dwells. For code that just changed what is under the pointer. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Tooltip")
	void HideTooltip();
	/** Hide player InUserIndex's tooltip and restart their dwell. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Tooltip")
	void HideTooltipForUser(int32 InUserIndex);

	/**
	 * Show InSource's tooltip right now, to the player who owns InSource, as though its dwell had just elapsed.
	 * Does nothing for a source that offers neither ToolTipText nor a tooltip widget class.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Tooltip")
	void ShowTooltipFor(UDreamWidget* InSource);

	/** The widget the first player's visible tooltip belongs to, or null while none shows. */
	UDreamWidget* GetShownFor() const { return GetShownForUser(0); }
	/** The widget player InUserIndex's visible tooltip belongs to, or null while none shows. */
	UDreamWidget* GetShownForUser(int32 InUserIndex) const;
	/**
	 * The bubble itself -- the positioned widget everything drawn hangs off, which is what hiding destroys -- for
	 * player InUserIndex, or null while none is up. ShownFor can go stale while the bubble stays: this is the half a
	 * test of the teardown has to read.
	 */
	UDreamWidget* GetBubbleForUser(int32 InUserIndex) const;

private:
	void HandleInputEvent(UDreamBaseEventData* InEventData);
	/**
	 * The candidate from what the followed pointer is over now, when its enters and exits have said it moved: a new
	 * candidate restarts the dwell and hides a bubble shown for another.
	 */
	void RefreshCandidate(FDreamUITooltipUserState& InState);
	void TickUser(int32 InUserIndex, FDreamUITooltipUserState& InState, float InDeltaSeconds);
	void HideUserTooltip(FDreamUITooltipUserState& InState);
	void ShowFor(int32 InUserIndex, UDreamWidget* InSource);
	/** Size the built-in bubble to its text's preferred size; safe to call before the text can answer. */
	void SizeBubbleToText(FDreamUITooltipUserState& InState);
	void UpdateTooltipPosition(FDreamUITooltipUserState& InState);
	/**
	 * Where the bubble points, in InHost's own local 2D space: the pointer's position when a pointer armed this
	 * tooltip and the host is a screen overlay, the SOURCE widget's own rect otherwise.
	 * @return false when there is nothing to point at, in which case the bubble is left where it was.
	 */
	bool ResolveTooltipAnchor(const FDreamUITooltipUserState& InState, UDreamWidget* InHost, UDreamWidget* InSource, FVector2D& OutAnchor) const;
	void DestroyTooltipWidgets(FDreamUITooltipUserState& InState);

	/** The input subsystem listened to, so the teardown can stop listening. */
	TWeakObjectPtr<UDreamUIInputSubsystem> InputSubsystem;
	/** Set by TeardownForWorld, which runs once. */
	bool bTornDownForWorld = false;

	/** Each player's tooltip, by player index. */
	UPROPERTY(Transient)
	TMap<int32, FDreamUITooltipUserState> UserStates;
};
