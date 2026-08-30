// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Templates/SubclassOf.h"
#include "DreamDragDropOperation.generated.h"

class UDreamWidget;
class UDreamUserWidget;
class UDreamDragDropOperation;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamUIDragDropOperationEvent, UDreamDragDropOperation*, Operation);

/**
 * What a drag MEANS, riding along with it. The pointer pipeline itself only knows geometry -- which
 * widget is dragging and where the pointer is -- and every consumer used to reconstruct meaning
 * from that alone. This object carries the meaning: the payload being moved, a tag for cheap
 * filtering, and the drag visual to show under the cursor.
 *
 * Created by the source when its drag begins (UDreamUIDragSource, or any code that writes it onto
 * the pointer's event data), read by targets on drop, and alive exactly as long as the drag: the
 * input module clears it wherever it clears DragWidget.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (DreamGUI))
class DREAMGUI_API UDreamDragDropOperation : public UObject
{
	GENERATED_BODY()

public:
	/** The thing being dragged -- an item, an entry's data object, anything. May be null for tag-only drags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	TObjectPtr<UObject> Payload;

	/** Cheap filter for targets: an inventory slot accepts "Item", a hotbar accepts "Item" and "Skill". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	FName Tag;

	/** Widget class shown under the cursor for the duration of the drag. Null shows nothing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	TSubclassOf<UDreamUserWidget> DragVisualClass;

	/** Drag visual offset from the pointer, canvas units, X right Y up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = DreamGUI)
	FVector2D DragVisualOffset = FVector2D::ZeroVector;

	/** The widget the drag started on. */
	UPROPERTY(BlueprintReadOnly, Category = DreamGUI)
	TWeakObjectPtr<UDreamWidget> SourceWidget;

	/**
	 * Set by the target that accepted the drop. What the source's end-of-drag reads to decide
	 * between "it landed" and "it was cancelled" -- writable so a Blueprint implementing the drop
	 * interface directly can accept too.
	 */
	UPROPERTY(BlueprintReadWrite, Category = DreamGUI)
	bool bDropWasHandled = false;

	/** Fired by the accepting target, after its own handling. */
	UPROPERTY(BlueprintAssignable, Category = DreamGUI)
	FDreamUIDragDropOperationEvent OnDropHandled;

	/** Fired by the source when the drag ended with no target accepting. */
	UPROPERTY(BlueprintAssignable, Category = DreamGUI)
	FDreamUIDragDropOperationEvent OnDragCancelled;
};
