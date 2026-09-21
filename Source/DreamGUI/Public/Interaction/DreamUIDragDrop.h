// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerDragDropInterface.h"
#include "Interaction/DreamDragDropOperation.h" // FDreamUIDragDropOperationEvent
#include "Subsystems/WorldSubsystem.h"
#include "DreamUIDragDrop.generated.h"

class UDreamDragDropOperation;
class UDreamPointerEventData;
class UDreamEventSystem;
class UDreamBaseEventData;
class UDreamWidget;
class UDreamUserWidget;

/**
 * Makes its widget a drag SOURCE with meaning: when the pointer pipeline starts a drag here, this
 * creates a UDreamDragDropOperation and writes it onto the pointer's event data, where drop targets
 * find it. Without one of these (or code doing the same), a drag is pure geometry -- which is
 * exactly what a scroll view wants and an inventory item does not.
 *
 * Fill the properties for the common case, or override CreateDragOperation for a payload only
 * runtime knows. Returning null from the override declines the meaning without disturbing the
 * geometric drag.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "DreamUI Drag Source")
class DREAMGUI_API UDreamUIDragSource : public UDreamUIBehaviour, public IDreamPointerDragInterface
{
	GENERATED_BODY()

public:
	/** Copied onto the operation. See UDreamDragDropOperation for what each means. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	FName Tag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	TObjectPtr<UObject> Payload;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	TSubclassOf<UDreamUserWidget> DragVisualClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	FVector2D DragVisualOffset = FVector2D::ZeroVector;

	/**
	 * Whether the drag events keep bubbling above this widget while an operation rides them.
	 * Default off: an item inside a scroll view should be DRAGGED, not scroll its list.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	bool bAllowEventBubbleUp = false;

	/** Build the operation for a drag that just started here. The default fills it from the properties. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	UDreamDragDropOperation* CreateDragOperation(UDreamPointerEventData* EventData);
	virtual UDreamDragDropOperation* CreateDragOperation_Implementation(UDreamPointerEventData* EventData);

	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData) override;
};

/**
 * Makes its widget a drop TARGET: reads the operation off a drop that lands here, filters by tag
 * and payload class, and on acceptance marks the operation handled and broadcasts. A drop this
 * target refuses keeps bubbling, so nested targets behave like nested anything else.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "DreamUI Drop Target")
class DREAMGUI_API UDreamUIDropTarget : public UDreamUIBehaviour, public IDreamPointerDragDropInterface
{
	GENERATED_BODY()

public:
	/** Accept only operations with this tag. None accepts any tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	FName RequiredTag;

	/** Accept only payloads of this class. Null accepts any payload, including none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	TSubclassOf<UObject> RequiredPayloadClass;

	/** The acceptance decision. The default checks RequiredTag and RequiredPayloadClass. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	bool CanAcceptDrop(UDreamDragDropOperation* Operation);
	virtual bool CanAcceptDrop_Implementation(UDreamDragDropOperation* Operation);

	/** What acceptance DOES, before the delegates fire. The default does nothing but exist to override. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = DreamGUI)
	void HandleAcceptedDrop(UDreamDragDropOperation* Operation);
	virtual void HandleAcceptedDrop_Implementation(UDreamDragDropOperation* Operation) {}

	/** Fired after HandleAcceptedDrop, before the operation's own OnDropHandled. */
	UPROPERTY(BlueprintAssignable, Category = DreamGUI)
	FDreamUIDragDropOperationEvent OnDropAccepted;

	/**
	 * Drop-hover feedback, driven by UDreamUIDragDropSubsystem while a drag is in flight. Enter and
	 * Leave fire once each at the edges, Over fires every frame in between -- so a slot can light up
	 * the moment a payload it can take arrives over it, instead of the acceptance decision being
	 * invisible until the player lets go.
	 *
	 * Only the target that WOULD accept the drop is hovered: CanAcceptDrop is asked before Enter,
	 * exactly as it is asked on the drop itself, and a refusing target is skipped for whichever
	 * ancestor accepts -- the same bubbling the drop does.
	 */
	UPROPERTY(BlueprintAssignable, Category = DreamGUI)
	FDreamUIDragDropOperationEvent OnDragEnter;
	UPROPERTY(BlueprintAssignable, Category = DreamGUI)
	FDreamUIDragDropOperationEvent OnDragOver;
	UPROPERTY(BlueprintAssignable, Category = DreamGUI)
	FDreamUIDragDropOperationEvent OnDragLeave;

	/** True while an acceptable drag is hovering this target. Cleared on leave, drop, and cancel. */
	UFUNCTION(BlueprintPure, Category = DreamGUI)
	bool IsDragHovered() const { return bIsDragHovered; }

	/** Fire the hover edges. Called by the drag-drop subsystem; public so a test can drive them. */
	void NotifyDragEnter(UDreamDragDropOperation* InOperation);
	void NotifyDragOver(UDreamDragDropOperation* InOperation);
	void NotifyDragLeave(UDreamDragDropOperation* InOperation);

	virtual bool OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData) override;

private:
	/** Not a UPROPERTY: it is derived state the subsystem owns, and saving it would be meaningless. */
	bool bIsDragHovered = false;
};

/**
 * Pure decisions of the drag-drop pipeline, pulled out of the subsystem so they are testable without
 * a world -- the same convention DreamUITooltipPolicy and DreamPointerPolicy document.
 */
namespace DreamUIDragDropPolicy
{
	/**
	 * The drop target a drag hovering InEnterWidget would land on: the first UDreamUIDropTarget on
	 * InEnterWidget or one of its ancestors that accepts InOperation. Null when nothing on the path
	 * would take it -- which is the honest answer for "nothing to highlight", not an error.
	 *
	 * The ancestor walk mirrors what the drop itself does: UDreamEventSystem bubbles DragDrop up the
	 * parent chain and a refusing target is transparent to the one around it.
	 */
	DREAMGUI_API UDreamUIDropTarget* ResolveDropTarget(UDreamWidget* InEnterWidget, UDreamDragDropOperation* InOperation);
}

/**
 * The drag VISUAL: when a drag carrying an operation with a DragVisualClass begins, this spawns
 * that widget on a raycast-disabled overlay canvas and walks it under the pointer until the drag
 * ends. Purely cosmetic -- sources and targets work without it -- which is why it lives in a
 * subsystem rather than in the pipeline.
 */
UCLASS()
class DREAMGUI_API UDreamUIDragDropSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamUI DragDrop Subsystem"), Category = "DreamGUI|DragDrop")
	static UDreamUIDragDropSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/**
	 * Ticks while the game is paused: a drag begun before the pause is still under the player's
	 * finger, and a visual that stops following it reads as the drag having been dropped. Matches
	 * UDreamUIManagerWorldSubsystem and the event system component, both of which tick when paused.
	 */
	virtual bool IsTickableWhenPaused() const override { return true; }

	/**
	 * Cancel every drag in flight: each source is told its drag ended, unhandled operations get their
	 * OnDragCancelled, and the visuals go away. What Escape does during a drag.
	 * @return true when there was at least one drag to cancel.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|DragDrop")
	bool CancelActiveDrag();

	/** True while any pointer is carrying a UDreamDragDropOperation. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	bool IsDragInProgress() const;

	/** How many drags are being followed at once. One per finger on a touch screen. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	int32 GetDragCount() const { return FollowedDrags.Num(); }

	/** The operation pointer InPointerID is carrying, or null when it is not dragging one. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	UDreamDragDropOperation* GetDragOperationForPointer(int32 InPointerID) const;

	/** The drop target pointer InPointerID is hovering, or null. For a slot asking "am I next?". */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	UDreamUIDropTarget* GetHoveredTargetForPointer(int32 InPointerID) const;

private:
	/**
	 * One drag being followed, keyed by the pointer that began it.
	 *
	 * There used to be exactly one of everything below, shared by whichever pointer sent the last
	 * event -- so on a touch screen a second finger moving anywhere at all teleported the first
	 * finger's drag visual to it, and lit up whatever the second finger was over as the first one's
	 * drop target. Two fingers dragging two inventory items is not exotic on a phone; it is the
	 * ordinary way an inventory gets rearranged.
	 *
	 * Weak handles throughout for the same reason the modal stack uses them: the visual is kept alive
	 * by the widget manager while it is registered, and a screen torn down mid-drag takes it with it,
	 * which is a state this has to survive reading rather than one it can prevent.
	 */
	struct FFollowedDrag
	{
		TWeakObjectPtr<UDreamPointerEventData> PointerEvent;
		TWeakObjectPtr<UDreamDragDropOperation> Operation;
		/** The target currently lit up for THIS drag, so enter and leave each fire exactly once. */
		TWeakObjectPtr<UDreamUIDropTarget> HoveredTarget;
		TWeakObjectPtr<UDreamWidget> VisualHolder;
		TWeakObjectPtr<UDreamUserWidget> Visual;
	};

	void EnsureSubscribed();
	void HandleInputEvent(UDreamBaseEventData* InEventData);
	void BeginFollowingDrag(UDreamPointerEventData* InPointerEvent);
	void ShowDragVisual(FFollowedDrag& InDrag, UDreamPointerEventData* InPointerEvent);
	void UpdateDragVisualPosition(FFollowedDrag& InDrag);
	void UpdateDropHover(FFollowedDrag& InDrag);
	void ClearDropHover(FFollowedDrag& InDrag);
	/** Tear one drag's bookkeeping down and forget it. Safe for a pointer that is not being followed. */
	void StopFollowingDrag(int32 InPointerID);
	void DestroyDragVisual(FFollowedDrag& InDrag);
	/** True when this drag's pointer is still dragging the operation it began with. */
	static bool IsStillLive(const FFollowedDrag& InDrag);

	TWeakObjectPtr<UDreamEventSystem> SubscribedEventSystem;
	/** Pointer id to the drag it is carrying. Empty when nothing is being dragged. */
	TMap<int32, FFollowedDrag> FollowedDrags;
};
