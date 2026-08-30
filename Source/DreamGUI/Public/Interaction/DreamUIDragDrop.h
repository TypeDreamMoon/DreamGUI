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

	virtual bool OnPointerDragDrop_Implementation(UDreamPointerEventData* EventData) override;
};

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

private:
	void EnsureSubscribed();
	void HandleInputEvent(UDreamBaseEventData* InEventData);
	void ShowDragVisual(UDreamPointerEventData* InPointerEvent);
	void UpdateDragVisualPosition();
	void DestroyDragVisual();

	TWeakObjectPtr<UDreamEventSystem> SubscribedEventSystem;
	TWeakObjectPtr<UDreamPointerEventData> LastPointerEvent;
	TWeakObjectPtr<UDreamDragDropOperation> VisualForOperation;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> DragVisualHolder;
	UPROPERTY(Transient)
	TObjectPtr<UDreamUserWidget> DragVisual;
};
