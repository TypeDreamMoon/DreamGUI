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

	/** Hide whatever is showing and restart the dwell. For code that just changed what is under the pointer. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Tooltip")
	void HideTooltip();

	/** The widget the visible tooltip belongs to, or null while none shows. */
	UDreamWidget* GetShownFor() const { return ShownFor.Get(); }

private:
	void EnsureSubscribed();
	void HandleInputEvent(UDreamBaseEventData* InEventData);

	void ShowFor(UDreamWidget* InSource);
	/** Size the built-in bubble to its text's preferred size; safe to call before the text can answer. */
	void SizeBubbleToText();
	void UpdateTooltipPosition();
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

	/** What the visible tooltip belongs to. */
	TWeakObjectPtr<UDreamWidget> ShownFor;

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
