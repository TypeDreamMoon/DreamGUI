// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Event/Interface/DreamPointerDragDropInterface.h"
#include "Interaction/DreamDragDropOperation.h" // FDreamUIDragDropOperationEvent
#include "Subsystems/WorldSubsystem.h"
#include "Core/DreamUIWorldService.h"
#include "DreamUIDragDrop.generated.h"

class UDreamDragDropOperation;
class UDreamPointerEventData;
class UDreamEventSystem;
class UDreamUIInputSubsystem;
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
class DREAMGUIINPUT_API UDreamUIDragSource : public UDreamUIBehaviour, public IDreamPointerDragInterface
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
class DREAMGUIINPUT_API UDreamUIDropTarget : public UDreamUIBehaviour, public IDreamPointerDragDropInterface
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
	DREAMGUIINPUT_API UDreamUIDropTarget* ResolveDropTarget(UDreamWidget* InEnterWidget, UDreamDragDropOperation* InOperation);
}

/**
 * The drag VISUAL: when a drag carrying an operation with a DragVisualClass begins, this spawns
 * that widget on a raycast-disabled overlay canvas and walks it under the pointer until the drag
 * ends. Purely cosmetic -- sources and targets work without it -- which is why it lives in a
 * subsystem rather than in the pipeline.
 */
UCLASS()
class DREAMGUIINPUT_API UDreamUIDragDropSubsystem : public UTickableWorldSubsystem, public IDreamUIWorldService
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get DreamUI DragDrop Subsystem"), Category = "DreamGUI|DragDrop")
	static UDreamUIDragDropSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual int32 GetTeardownPriority() const override { return DreamUI::WorldServiceTeardownPriority::Input; }
	virtual void TeardownForWorld(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	/**
	 * Ticks while the game is paused: a drag begun before the pause is still under the player's
	 * finger, and a visual that stops following it reads as the drag having been dropped. Matches
	 * UDreamUIManagerWorldSubsystem and the event system component, both of which tick when paused.
	 */
	virtual bool IsTickableWhenPaused() const override { return true; }

	/**
	 * Cancel every drag in flight, every player's: each source is told its drag ended, unhandled operations get
	 * their OnDragCancelled, and the visuals go away.
	 * @return true when there was at least one drag to cancel.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|DragDrop")
	bool CancelActiveDrag();
	/**
	 * Cancel player InUserIndex's drags, and only theirs: what Escape does during a drag. Any player's Escape used to
	 * cancel player 0's drag.
	 * @return true when that player had a drag to cancel.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|DragDrop")
	bool CancelActiveDragForUser(int32 InUserIndex);

	/** True while any pointer of any player is carrying a UDreamDragDropOperation. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	bool IsDragInProgress() const;
	/** True while a pointer of player InUserIndex is carrying one. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	bool IsDragInProgressForUser(int32 InUserIndex) const;

	/** How many drags are being followed at once. One per finger on a touch screen, per player. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	int32 GetDragCount() const { return FollowedDrags.Num(); }

	/** The operation the first player's pointer InPointerID is carrying, or null when it is not dragging one. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	UDreamDragDropOperation* GetDragOperationForPointer(int32 InPointerID) const;
	/** The operation player InUserIndex's pointer InPointerID is carrying, or null. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	UDreamDragDropOperation* GetDragOperationForUserPointer(int32 InUserIndex, int32 InPointerID) const;

	/** The drop target the first player's pointer InPointerID is hovering, or null. For a slot asking "am I next?". */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	UDreamUIDropTarget* GetHoveredTargetForPointer(int32 InPointerID) const;
	/** The drop target player InUserIndex's pointer InPointerID is hovering, or null. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop")
	UDreamUIDropTarget* GetHoveredTargetForUserPointer(int32 InUserIndex, int32 InPointerID) const;

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

	void HandleInputEvent(UDreamBaseEventData* InEventData);
	void BeginFollowingDrag(UDreamPointerEventData* InPointerEvent);
	void ShowDragVisual(FFollowedDrag& InDrag, UDreamPointerEventData* InPointerEvent);
	void UpdateDragVisualPosition(FFollowedDrag& InDrag);
	/**
	 * Light up the target the drag at InKey is over, and leave the one it was over. By key, not by entry: the targets'
	 * enter, over and leave reach game code, which can end that drag or begin another, and an entry held across them can
	 * be gone from the map by the time they return.
	 */
	void UpdateDropHover(const FIntPoint& InKey);
	/** Forget the target InDrag lights up, then tell it the drag left it. */
	void ClearDropHover(FFollowedDrag& InDrag);
	/** Tear one drag's bookkeeping down and forget it. Safe for a pointer that is not being followed. */
	void StopFollowingDrag(const FIntPoint& InKey);
	/** A drag's key: the player, and the pointer that began it. */
	static FIntPoint MakeKey(int32 InUserIndex, int32 InPointerID) { return FIntPoint(InUserIndex, InPointerID); }
	void DestroyDragVisual(FFollowedDrag& InDrag);
	/** True when this drag's pointer is still dragging the operation it began with. */
	static bool IsStillLive(const FFollowedDrag& InDrag);

	/** The input subsystem listened to, so the teardown can stop listening. */
	TWeakObjectPtr<UDreamUIInputSubsystem> InputSubsystem;
	/** (player, pointer) to the drag it is carrying. Empty when nothing is being dragged. */
	TMap<FIntPoint, FFollowedDrag> FollowedDrags;
	/** Set by TeardownForWorld, which runs once. */
	bool bTornDownForWorld = false;
};
