// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamBaseRaycaster.h"
#include "DreamWorldSpaceRaycaster.generated.h"

/** Where in the owning player's view a world-space pointer's ray is aimed. */
UENUM(BlueprintType)
enum class EDreamWorldPointerSource : uint8
{
	/** Through the pointer's own screen position -- a mouse, a touch, a virtual cursor. */
	Mouse,
	/** Through the middle of the view, wherever the pointer happens to be -- a first-person reticle. */
	ScreenCenter,
};

/**
 * Pointer interaction with every world-space canvas ONE local player can reach.
 *
 * One raycaster per player, not one per panel: the ray is aimed by that player's view, and which
 * canvases it can hit is answered by asking the manager for the world-space roots whose TraceChannel
 * matches. Adding, moving or destroying a panel is therefore nothing this component has to be told
 * about, and a panel is not required to carry an interaction component of its own.
 *
 * UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer creates one on a transient
 * "DreamInteractionHost_P%d" actor for any player that needs it. Placing one by hand on any actor
 * overrides that: the manager sees a world-space raycaster already speaking for that UserIndex and
 * leaves it alone, so an authored raycaster with its own pointer source, ray length, world occlusion
 * or drag behaviour is the one that runs.
 */
UCLASS(ClassGroup = DreamGUI, meta = (BlueprintSpawnableComponent), Blueprintable)
class DREAMGUI_API UDreamWorldSpaceRaycaster : public UDreamBaseRaycaster
{
	GENERATED_BODY()

public:
	UDreamWorldSpaceRaycaster();
	virtual void BeginPlay()override;
#if WITH_EDITOR
	/**
	 * Keeps DragThresholdSquare in step with a DragThreshold typed into the Details panel, for the
	 * same reason UDreamScreenSpaceRaycaster does: otherwise the square is only ever recomputed by
	 * the constructor, BeginPlay and SetDragThreshold, and an author editing the value in the editor
	 * is still measured against whatever it was when the component was constructed.
	 */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)override;
#endif

protected:
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	EDreamWorldPointerSource PointerSource = EDreamWorldPointerSource::Mouse;
	/**
	 * Trace the world as well as the panels, so that what the player cannot see past, the pointer cannot
	 * click past -- and whatever the trace hits is pointed at.
	 *
	 * One line trace per pointer per frame -- a single trace, which answers with the nearest blocking hit
	 * only -- along the ray the panels are hit-tested with, on the collision channel TraceChannel stands
	 * for: TraceTypeQuery1, the engine's Visibility channel, unless TraceChannel is changed. That hit
	 * carries no widget. It wins on distance, so a panel behind it stops being pointed at; and the actor
	 * behind it is sent the pointer's events -- enter, exit, down, up, click, double click, long press,
	 * scroll -- on itself and on any of its components that implement the pointer interfaces (see
	 * UDreamEventSystem::FDreamPointerWorldTarget).
	 * That is how a render-target surface's UDreamUIRenderTargetInteraction gets its pointer: the
	 * surface is a mesh in the world, and this trace is the only thing that ever reaches it. A wall
	 * implements none of the interfaces and is only an occluder.
	 *
	 * What stops the pointer is whatever blocks that channel. On Visibility that is solid geometry -- a
	 * static mesh component's default BlockAllDynamic, a placed static mesh actor's BlockAll -- and not
	 * a pawn's capsule or a character's mesh (the engine's Pawn, CharacterMesh and Spectator profiles
	 * ignore Visibility), nor a trigger or a bare box, sphere or capsule component (Trigger ignores
	 * Visibility and a shape component's default OverlapAllDynamic only overlaps it; the trace is
	 * single, so overlapping is never occluding). The actor this raycaster rides on is skipped and
	 * nothing else is: a primitive that blocks the channel around the camera -- a pawn given a profile
	 * that blocks Visibility, seen from inside -- is hit at distance zero, where the ray starts, and
	 * takes every world-space panel away. Ride the raycaster on that pawn, or give the pawn a profile
	 * that ignores the channel.
	 *
	 * On by default. Off, a world pointer clicked straight through walls into panels the player could
	 * not see -- one in the next room, one behind a pillar -- and never reached a render-target surface
	 * at all; and the raycaster UDreamUIManagerWorldSubsystem::EnsureInteractionForPlayer makes for a
	 * player is a default one, so a project that relied on it had both problems and no setting in sight
	 * to explain them. The price is that one trace per pointer per frame.
	 *
	 * To turn it off, untick it on a raycaster of your own -- placed on any actor with the player's
	 * UserIndex, it replaces the automatic one (see the class comment) -- or call SetOccludeByWorld(false)
	 * on whichever world-space raycaster the player has.
	 */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	bool bOccludeByWorld = true;
	/**
	 * Which world-space canvases this raycaster answers for: only roots whose own TraceChannel is the
	 * same. With bOccludeByWorld (on by default) it is also the collision channel the occlusion trace
	 * uses, so "what this pointer can touch" and "what can block it" stay one setting: moving a pointer
	 * to another channel moves both, and its panels have to move with it.
	 */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	TEnumAsByte<ETraceTypeQuery> TraceChannel;
	/** ray length for line trace hit */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	float RayLength = 100000;
	/** drag threshold, calculated in target's local space */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	float DragThreshold = 5;
	/** hold press for a little while to entering drag mode */
	UPROPERTY(EditAnywhere, Category = DreamGUI)
	bool bHoldToDrag = false;
	/** hold press for "holdToDragTime" to entering drag mode */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (EditCondition = "bHoldToDrag"))
	float HoldToDragTime = 0.5f;
	/**
	 * DragThreshold squared, so the per-move comparison can use DistSquared and skip a square root.
	 * Derived, never authored: every write to DragThreshold has to be followed by a write to this,
	 * which is why DragThreshold is protected and SetDragThreshold is the only supported way to
	 * change it while the game runs.
	 */
	float DragThresholdSquare = 0;

public:
	virtual bool GetAffectByGamePause()const override;
	/** Kept virtual so a fixture -- or a hand-written pointer device -- can supply a ray directly. */
	virtual bool GenerateRay(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, float& OutRayLength)override;
	virtual void Raycast(UDreamPointerEventData* InPointerEventData, FVector& OutRayOrigin, FVector& OutRayDirection, FVector& OutRayEnd, TArray<FDreamUIHitResult>& OutHitResultArray)override;
	virtual bool ShouldStartDrag(UDreamPointerEventData* InPointerEventData)override;
	/**
	 * The two presses measured as ShouldStartDrag measures a drag: where the ray landed, in world
	 * units, for a centre-screen pointer (whose screen position never moves), and the pointer's screen
	 * position otherwise, against the same DragThresholdSquare.
	 */
	virtual bool IsWithinDoubleClickDistance(const UDreamPointerEventData* InPointerEventData) const override;

	virtual float GetRayLength()const override { return RayLength; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamWorldPointerSource GetPointerSource()const { return PointerSource; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetOccludeByWorld()const { return bOccludeByWorld; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	TEnumAsByte<ETraceTypeQuery> GetTraceChannel()const { return TraceChannel; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetDragThreshold()const { return DragThreshold; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetHoldToDrag()const { return bHoldToDrag; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	float GetHoldToDragTime()const { return HoldToDragTime; }
	float GetDragThresholdSquare()const { return DragThresholdSquare; }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetPointerSource(EDreamWorldPointerSource Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetOccludeByWorld(bool Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetTraceChannel(TEnumAsByte<ETraceTypeQuery> Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetRayLength(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetDragThreshold(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetHoldToDrag(bool Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetHoldToDragTime(float Value);
};
