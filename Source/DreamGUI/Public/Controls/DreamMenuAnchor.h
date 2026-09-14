// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamMenuAnchor.generated.h"

class UDreamWidget;
class UDreamUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamMenuAnchorOpenChangedEvent, bool, bIsOpen);

/**
 * A place a menu opens from, and the thing that puts it away again.
 *
 * UMG's MenuAnchor in the DreamGUI idiom. The parts a menu needs were all here already and had never
 * been assembled: UDreamUIPopupLayer lifts a widget to the screen root so no ancestor clips it or
 * counts it in its layout, and UUIDropdown's blocker is the full-screen click catcher that closes a
 * popup when the pointer lands anywhere else. UDreamDropdown owns a private copy of exactly this
 * arrangement; this class is that arrangement with the dropdown's list taken out of it, so a menu
 * can hold anything.
 *
 * TWO WAYS TO SAY WHAT THE MENU IS, and they are alternatives:
 *
 *   - fill the `Menu` slot -- `Native.MenuAnchor { ... }` in .dui, or a designer drop -- and the
 *     content is authored in place, which is what a small menu wants;
 *   - or set MenuClass, and an instance of it is made the first time the menu opens and kept, which
 *     is what a menu shared between several anchors wants. Instancing a user widget needs a world,
 *     so with none this quietly stays an empty menu rather than half of one.
 *
 * With both, the SLOT wins: content somebody put there is content they meant to see.
 *
 * WHERE IT OPENS is FDreamMenuAnchorStyle::Placement, which is a style rather than a widget property
 * on purpose -- a project wants every menu in it to open the same way, and the override bits still
 * let one instance disagree.
 *
 * THE SIZE IS STATED, NOT MEASURED. MenuSize is an authored number for the reason the dropdown's
 * list height is: a popup is arranged by nobody until it is on screen, so measuring it would mean
 * opening it invisibly for a frame first. A menu whose content states its own size can say so by
 * leaving MenuSize at zero on that axis, which hands the axis back to the content.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Menu Anchor")
class DREAMGUI_API UDreamMenuAnchor : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor")
	FDreamMenuAnchorStyle Style;

	/** The menu's class, for an anchor whose menu is not authored in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor")
	TSubclassOf<UDreamUserWidget> MenuClass = nullptr;

	/** How big the menu opens. Zero on an axis leaves that axis to whatever is inside it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor")
	FVector2D MenuSize = FVector2D(200.0, 150.0);

	/** Whether a click anywhere else closes the menu. Off makes it the caller's job. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu Anchor")
	bool bCloseOnClickOutside = true;

	/** Open and closed, as the anchor announces them. Fires after the move, never during. */
	UPROPERTY(BlueprintAssignable, Category = "Menu Anchor")
	FDreamMenuAnchorOpenChangedEvent OnMenuOpenChanged;

	/** The popup root: what gets lifted to the screen, positioned and faded. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Menu Anchor")
	TObjectPtr<UDreamWidget> PopupNode = nullptr;

	/** The hole inside it. Whatever a host nests on this control ends up here. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Menu Anchor")
	TObjectPtr<UDreamWidget> MenuNode = nullptr;

	/** The instance MenuClass produced, or null while the slot is filled or there is no world. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Menu Anchor")
	TObjectPtr<UDreamUserWidget> MenuInstance = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { MenuSlotName }; }
	virtual FName GetDefaultSlotName() const override { return MenuSlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName MenuSlotName;

	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	bool IsOpen() const { return bIsOpen; }

	/** Put the menu on screen, positioned by the style. A no-op while it is already open. */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void Open();

	/** Take it off screen and hand it home. A no-op while it is already closed. */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void ToggleOpen();

	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;
	/** Closing on the way out, so an anchor destroyed while open does not strand a lifted popup. */
	virtual void NativeOnDestruct() override;

private:
	/**
	 * Point-anchor the popup against this control's rect, per Placement and Offset.
	 *
	 * The position itself comes from UDreamLayoutContainerMenuAnchor::CalculateMenuPosition -- the
	 * layout panel's arithmetic, used rather than repeated, so a menu lands in the same place whether
	 * the panel or this control opened it. This function's own job is the frame change (that one
	 * answers in top-left space with y down; widgets here are y-up) and the style's gap.
	 */
	void PlacePopup(const FDreamMenuAnchorStyle& InStyle);

	/** Which way the style's Offset widens the gap, in the placement function's y-down space. */
	static FVector2D ResolveOffsetDirection(EDreamMenuPlacement InPlacement);

	/** The full-screen click catcher, built on open and destroyed on close. */
	void CreateBlocker();
	void DestroyBlocker();

	/** Make the MenuClass instance, once, when the slot is empty and there is a world to make it in. */
	void EnsureMenuInstance();

	UPROPERTY(Transient)
	bool bIsOpen = false;

	/** Set between Elevate and Restore; the popup's resting geometry must not be written while it is. */
	UPROPERTY(Transient)
	bool bPopupElevated = false;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> BlockerNode = nullptr;
};
