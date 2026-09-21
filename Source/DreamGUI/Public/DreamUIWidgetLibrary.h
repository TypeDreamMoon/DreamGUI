// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Core/DreamUIImageBrush.h"
#include "DreamUIWidgetLibrary.generated.h"

class APlayerController;
class UDreamDragDropOperation;
class UDreamPointerEventData;
class UDreamWidget;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UTexture2D;

/**
 * Finding widgets, driving a drag, and building an image brush from Blueprint -- the parts of
 * UMG's UWidgetBlueprintLibrary that mean something in this framework.
 *
 * The parts that do not are named in the parity table with their reasons, and they are all one
 * reason: UMG's library is half Slate plumbing. FEventReply, the OnPaint drawing context and the
 * Slate input-event structs describe an SWidget's conversation with Slate, and a DreamGUI widget
 * never has that conversation -- its input arrives as a UDreamPointerEventData from the event
 * system and its pixels come from its own mesh.
 */
UCLASS()
class DREAMGUI_API UDreamUIWidgetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Every live widget of a class, anywhere in the world.
	 *
	 * @param bTopLevelOnly	Only widgets that are the ROOT of their own hierarchy -- a screen page,
	 *						a world-space canvas -- rather than every nested one. UMG's parameter
	 *						means the same thing and is false by default for the same reason: a
	 *						caller looking for "all the health bars" wants the nested ones too.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Find", meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "InWidgetClass", DynamicOutputParam = "OutFoundWidgets"))
	static void GetAllWidgetsOfClass(UObject* WorldContextObject, TSubclassOf<UDreamWidget> InWidgetClass,
		TArray<UDreamWidget*>& OutFoundWidgets, bool bTopLevelOnly = false);

	/** Every live widget whose class implements an interface. Behaviours are not searched, only widgets. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Find", meta = (WorldContext = "WorldContextObject"))
	static void GetAllWidgetsWithInterface(UObject* WorldContextObject, TSubclassOf<UInterface> InInterface,
		TArray<UDreamWidget*>& OutFoundWidgets, bool bTopLevelOnly = false);

	/**
	 * Make a drag operation to hand to a drag that is starting -- UMG's CreateDragDropOperation.
	 *
	 * The operation is what gives a drag MEANING: the payload, a tag for drop targets to filter on,
	 * and the widget class to show under the cursor. Write it onto the pointer's DragOperation and
	 * the drag-drop subsystem takes it from there. UDreamUIDragSource does exactly that for the
	 * common case; this is for a graph that builds the payload itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|DragDrop", meta = (DeterminesOutputType = "InOperationClass"))
	static UDreamDragDropOperation* CreateDragDropOperation(TSubclassOf<UDreamDragDropOperation> InOperationClass);

	/** Is any pointer carrying a drag operation right now? */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop", meta = (WorldContext = "WorldContextObject"))
	static bool IsDragDropping(UObject* WorldContextObject);

	/** The operation a pointer is carrying, or null. Pointer 0 is the mouse. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|DragDrop", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "InPointerID"))
	static UDreamDragDropOperation* GetDragDroppingContent(UObject* WorldContextObject, int32 InPointerID = 0);

	/** End every drag in flight without a drop -- what Escape does. True when there was one to cancel. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|DragDrop", meta = (WorldContext = "WorldContextObject"))
	static bool CancelDragDrop(UObject* WorldContextObject);

	/**
	 * Start a drag on a widget right now, from a pointer that is already pressed on it -- UMG's
	 * DetectDragIfPressed, minus the "if pressed" test, which the caller has already passed by
	 * having a pressed pointer's event data in hand.
	 *
	 * @return false when the pointer is not pressed on anything, or is already dragging.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|DragDrop")
	static bool BeginDragWithOperation(UDreamPointerEventData* InPointerEvent, UDreamDragDropOperation* InOperation);

	/** An image brush showing a texture. UMG's MakeBrushFromTexture; zero size means the texture's own. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Brush")
	static FDreamUIImageBrush MakeBrushFromTexture(UTexture2D* InTexture, int32 InWidth = 0, int32 InHeight = 0);

	/** An image brush showing a material. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Brush")
	static FDreamUIImageBrush MakeBrushFromMaterial(UMaterialInterface* InMaterial, int32 InWidth = 0, int32 InHeight = 0);

	/**
	 * An image brush showing whatever the asset is -- a texture, a material, a sprite or an atlas.
	 *
	 * One entry point rather than UMG's separate MakeBrushFromAsset, because the brush stores a bare
	 * UObject and works out how to draw it; a sprite is the interesting case and UMG has no
	 * equivalent for it at all.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Brush")
	static FDreamUIImageBrush MakeBrushFromAsset(UObject* InResource, int32 InWidth = 0, int32 InHeight = 0);

	/** A brush that draws nothing. UMG's NoResourceBrush. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Brush")
	static FDreamUIImageBrush NoResourceBrush();

	/** Are two brushes the same brush? UMG's EqualEqual_SlateBrush, for this framework's brush type. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Brush", meta = (DisplayName = "Equal (Dream UI Image Brush)", CompactNodeTitle = "==", Keywords = "== equal"))
	static bool EqualEqual_DreamUIImageBrush(const FDreamUIImageBrush& A, const FDreamUIImageBrush& B);

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Brush")
	static UObject* GetBrushResource(const FDreamUIImageBrush& InBrush);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Brush")
	static UTexture2D* GetBrushResourceAsTexture2D(const FDreamUIImageBrush& InBrush);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Brush")
	static UMaterialInterface* GetBrushResourceAsMaterial(const FDreamUIImageBrush& InBrush);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Brush")
	static void SetBrushResourceToTexture(UPARAM(Ref) FDreamUIImageBrush& InBrush, UTexture2D* InTexture);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Brush")
	static void SetBrushResourceToMaterial(UPARAM(Ref) FDreamUIImageBrush& InBrush, UMaterialInterface* InMaterial);

	/**
	 * The brush's material as something whose parameters can be set -- UMG's GetDynamicMaterial.
	 *
	 * A brush holding a plain material is rewritten to hold a dynamic instance of it, and that
	 * instance is returned; a brush already holding one returns it unchanged, so asking twice is
	 * asking for the same object. Null for a brush whose resource is a texture or a sprite: there is
	 * no material there to instance.
	 *
	 * This works on the BRUSH, which is why it can exist at all. An image draws whatever material its
	 * brush names, so an instance made here is this image's alone. It is not the same question as "give
	 * me the material a rect block is drawn with": that one is the draw call's, shared with every
	 * widget batched beside it, and is deliberately not handed out.
	 *
	 * The caller still has to give the brush back to whatever draws it (UDreamImage::SetBrush): a
	 * brush is a value, and the copy a getter returned is not the one on the widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Brush")
	static UMaterialInstanceDynamic* GetDynamicMaterial(UPARAM(Ref) FDreamUIImageBrush& InBrush);

	/**
	 * Hand keyboard focus back to the game viewport -- UMG's SetFocusToGameViewport.
	 *
	 * This is a SLATE focus change, not a DreamGUI one: it is what a menu calls on its way out so
	 * that the game window starts hearing keys again. UDreamWidget::ClearKeyboardFocus is the
	 * framework-level counterpart and the two are usually wanted together.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Input")
	static void SetFocusToGameViewport();

	/**
	 * Put the cursor somewhere -- UMG's SetMousePosition, minus the FEventReply it was built into.
	 *
	 * Viewport pixels, the same space GetMousePositionOnViewport reports and the same space a
	 * geometry's absolute coordinates are in, so "warp the cursor to this widget" is one conversion
	 * and not two. False when there is no player controller to move it on.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Input")
	static bool SetMousePosition(APlayerController* InPlayer, FVector2D InPosition);

	/**
	 * Close every menu anchor that is open, anywhere in this world -- UMG's DismissAllMenus.
	 *
	 * What a "go back", a pause or a scene change needs: a dropdown left open belongs to the screen
	 * it was opened over, and nothing else knows it is there. Walks the anchors rather than keeping
	 * a registry of open ones, because an anchor closed by any other route would have to remember to
	 * leave that registry and the one that forgot would leak a phantom.
	 *
	 * @return how many were closed, so a caller can tell "nothing was open" from "did nothing".
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Menu", meta = (WorldContext = "WorldContextObject"))
	static int32 DismissAllMenus(UObject* WorldContextObject);

	/**
	 * The platform's title-safe inset, in pixels, and the same inset as a fraction of the viewport.
	 *
	 * Console certification requires nothing important to sit outside it. False on a platform that
	 * reports none and whenever Slate is not running, so a caller can tell "no inset needed" from
	 * "could not ask" rather than reading a zero as either.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Screen", meta = (WorldContext = "WorldContextObject"))
	static bool GetSafeZonePadding(UObject* WorldContextObject, FVector4& OutSafePadding,
		FVector2D& OutSafePaddingScale);
};
