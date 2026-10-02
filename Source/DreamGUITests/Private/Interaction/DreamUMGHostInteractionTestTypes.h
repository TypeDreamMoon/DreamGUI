// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Events.h"
#include "UMG/DreamUMGWidgetInteraction.h"
#include "DreamUMGHostInteractionTestTypes.generated.h"

/** One pointer event the bridge handed to Slate, as the probe below saw it go. */
struct FDreamUMGSentPointerEvent
{
	enum class EKind : uint8
	{
		Down,
		Move,
		Up,
	};
	EKind Kind = EKind::Move;
	/** The pointer's index on the virtual Slate user: a finger's own, or the cursor's. */
	int32 PointerIndex = 0;
	bool bTouch = false;
	/** Slate's flagged first move of a finger. */
	bool bFirstMove = false;
	float Force = 0.0f;
};

/**
 * UDreamUMGWidgetInteraction, with what it decides made readable from a test.
 *
 * Everything the interaction SENDS goes to Slate through a virtual user, onto a UMG widget whose hit grid
 * is only filled by drawing it -- which a headless world never does. What it DECIDES is its own: whether
 * it still follows the pointer that hovered it, whether it still holds the cursor its virtual user
 * shares, and whether it is still ticking the moves through. Those are exactly what a drag starting on a
 * hosted widget depends on, and they live in protected state, which is the reason for a subclass rather
 * than reflection.
 *
 * And what it sends is caught on the way out: the three sends are recorded INSTEAD of being routed, so a
 * test can read every event the bridge would have handed Slate without anything reaching the editor's own
 * windows -- an event routed along an empty widget path goes looking for a top-level window under it.
 */
UCLASS()
class UDreamUMGDragInteractionProbe : public UDreamUMGWidgetInteraction
{
	GENERATED_BODY()

public:
	/** Every tick, which is every frame the interaction forwards the pointer's moves through. */
	int32 TickCount = 0;

	/** Every exit the event system delivered, whether or not the interaction acted on it. */
	int32 ExitCount = 0;

	/** Every event the interaction sent, in order. Only a surface whose visual is a UMG widget sends any. */
	TArray<FDreamUMGSentPointerEvent> Sent;

	/** The sent events of one kind, in order. */
	TArray<FDreamUMGSentPointerEvent> SentOfKind(FDreamUMGSentPointerEvent::EKind InKind) const
	{
		return Sent.FilterByPredicate([InKind](const FDreamUMGSentPointerEvent& InEvent) { return InEvent.Kind == InKind; });
	}

	/** Whether the interaction is forwarding the DreamGUI pointer InPointerId at all. */
	bool IsForwarding(int32 InPointerId) const
	{
		return ForwardedPointers.Contains(InPointerId);
	}

	virtual void Tick(float DeltaTime) override
	{
		++TickCount;
		Super::Tick(DeltaTime);
	}

	/** Whether the interaction has a virtual Slate user to forward through, and a manager arbitrating it. */
	bool IsEnrolled()
	{
		return FindEnrolledInteractions() != nullptr;
	}

	/** Whether the interaction still follows the pointer that hovered it. */
	bool IsFollowingPointer() const
	{
		return CurrentPointerEventData.IsValid();
	}

	/** Whether the cursor its virtual user shares with other surfaces is this interaction's. */
	bool HoldsSharedCursor()
	{
		const UDreamUMGWidgetInteractionManager::FInteractionContainer* Interactions = FindEnrolledInteractions();
		return Interactions != nullptr && Interactions->CurrentInteraction.Get() == this;
	}

protected:
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData) override
	{
		++ExitCount;
		return Super::OnPointerExit_Implementation(EventData);
	}

	virtual void SendPointerDown(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent) override
	{
		Record(FDreamUMGSentPointerEvent::EKind::Down, InEvent);
	}

	virtual void SendPointerUp(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent) override
	{
		Record(FDreamUMGSentPointerEvent::EKind::Up, InEvent);
	}

	virtual void SendPointerMove(const FWidgetPath& InWidgetPath, const FPointerEvent& InEvent) override
	{
		Record(FDreamUMGSentPointerEvent::EKind::Move, InEvent);
	}

private:
	void Record(FDreamUMGSentPointerEvent::EKind InKind, const FPointerEvent& InEvent)
	{
		FDreamUMGSentPointerEvent& Entry = Sent.AddDefaulted_GetRef();
		Entry.Kind = InKind;
		Entry.PointerIndex = static_cast<int32>(InEvent.GetPointerIndex());
		Entry.bTouch = InEvent.IsTouchEvent();
		Entry.bFirstMove = InEvent.IsTouchFirstMoveEvent();
		Entry.Force = InEvent.GetTouchForce();
	}
};
