// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamMenuAnchor.generated.h"

class UDreamWidget;
class UDreamUserWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamMenuAnchorOpenChangedEvent, bool, bIsOpen);

/**
 * Builds the menu's content on demand -- UMG's OnGetUserMenuContentEvent.
 *
 * Single-cast, as UMG's is and as it has to be: two handlers would both build a menu and only one
 * of them could be shown, which is a leak with a coin toss in front of it.
 */
DECLARE_DYNAMIC_DELEGATE_RetVal(UDreamWidget*, FDreamMenuAnchorGetContent);

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Menu Anchor")
	FDreamMenuAnchorStyle Style;

	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	FDreamMenuAnchorStyle GetStyle() const { return Style; }

	/** This instance's whole look, replaced and pushed. See UDreamButton::SetStyle for the caveat. */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void SetStyle(const FDreamMenuAnchorStyle& InStyle);

	/**
	 * Where the menu opens relative to this anchor -- UMG's Placement, read from the style in EFFECT
	 * rather than from the field beside it, so an anchor driven by the project sheet answers the
	 * placement it actually uses.
	 */
	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	EDreamMenuPlacement GetPlacement() const;

	/** Edits this instance's style and re-places an open menu at once. See UDreamBorder::SetPadding. */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void SetPlacement(EDreamMenuPlacement InPlacement);

	/**
	 * Where the menu sits, in this anchor's own local space -- UMG's GetMenuPosition.
	 *
	 * The popup's anchored position, which is the answer whether or not the popup has been lifted to
	 * the screen layer: it is placed against the anchor BEFORE being lifted, and put back through the
	 * same placement when it closes. Zero while there is no popup node at all.
	 */
	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	FVector2D GetMenuPosition() const;

	/**
	 * Whether a click on the anchor should open the menu -- UMG's ShouldOpenDueToClick, and the
	 * question a button wrapping an anchor has to ask before it calls Open: a click that lands while
	 * the menu is already up is the click that DISMISSED it, and opening again would make a menu
	 * impossible to close by clicking the thing that opened it.
	 */
	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	bool ShouldOpenDueToClick() const { return !bIsOpen; }

	/** The menu's class, for an anchor whose menu is not authored in place. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMenuClass", BlueprintSetter = "SetMenuClass", Category = "Menu Anchor")
	TSubclassOf<UDreamUserWidget> MenuClass = nullptr;

	/** How big the menu opens. Zero on an axis leaves that axis to whatever is inside it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMenuSize", BlueprintSetter = "SetMenuSize", Category = "Menu Anchor")
	FVector2D MenuSize = FVector2D(200.0, 150.0);

	/** Whether a click anywhere else closes the menu. Off makes it the caller's job. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetCloseOnClickOutside", BlueprintSetter = "SetCloseOnClickOutside", Category = "Menu Anchor")
	bool bCloseOnClickOutside = true;

	/**
	 * Keep the open menu inside the screen -- UMG's bFitInWindow, and the same arithmetic the panel
	 * spelling of this anchor uses (UDreamLayoutContainerMenuAnchor::FitMenuInWindow, called here
	 * rather than copied, so a menu lands in the same place whichever road opened it).
	 *
	 * OFF by default, where UMG's and the panel's are on: a menu near a screen edge currently lands
	 * where the placement put it, and turning the clamp on for every existing anchor would move
	 * menus that somebody positioned on purpose. New work should turn it on.
	 *
	 * "The window" is the root widget of the hierarchy, which is the rect everything is laid out
	 * inside -- the nearest thing here to a Slate window.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetFitInWindow", BlueprintSetter = "FitInWindow", Category = "Menu Anchor")
	bool bFitInWindow = false;

	/**
	 * Builds the menu's content the first time it is needed, instead of MenuClass.
	 *
	 * Asked once, when there is no menu yet and nothing authored in the hole -- not on every open.
	 * The anchor keeps what it is handed and owns it from then on, exactly as it keeps an instance
	 * made from MenuClass; a handler that wants different content next time changes what is inside
	 * the widget it already gave, or calls SetMenuClass(nullptr) to be asked again.
	 *
	 * Ranks below authored content and above MenuClass: something a host put in the hole is what a
	 * host meant to see, and a delegate is a more specific answer than a class.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "Menu Anchor")
	FDreamMenuAnchorGetContent OnGetUserMenuContentEvent;

	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	bool GetFitInWindow() const { return bFitInWindow; }

	/** Named as UMG names the call. Re-places an open menu at once. */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void FitInWindow(bool bInFitInWindow);

	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	TSubclassOf<UDreamUserWidget> GetMenuClass() const { return MenuClass; }

	/**
	 * Which menu this anchor opens. The instance is built on first open and kept, so a class changed
	 * afterwards throws the old instance away rather than leaving the anchor opening the previous
	 * menu forever. A change while the menu is OPEN closes it first: swapping the contents of a menu
	 * the player is reading is not a thing this can do quietly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void SetMenuClass(TSubclassOf<UDreamUserWidget> InMenuClass);

	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	FVector2D GetMenuSize() const { return MenuSize; }

	/** Re-places an open menu at once, so the new size is not something only the next open shows. */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void SetMenuSize(FVector2D InMenuSize);

	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	bool GetCloseOnClickOutside() const { return bCloseOnClickOutside; }

	/**
	 * Adds or takes away the full-screen blocker WHILE the menu is open, rather than only deciding
	 * what the next open does -- a menu that became modal only on its second showing would be a
	 * switch that appears not to work.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void SetCloseOnClickOutside(bool bInCloseOnClickOutside);

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

	/**
	 * What OnGetUserMenuContentEvent handed over, kept so the handler is asked ONCE.
	 *
	 * A second field rather than reusing MenuInstance above, because a handler may hand back any
	 * widget at all and MenuInstance is specifically a user widget the anchor built itself. Which of
	 * the two is set is also how the anchor knows who owns what.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Menu Anchor")
	TObjectPtr<UDreamWidget> ProvidedMenuContent = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { MenuSlotName }; }
	virtual FName GetDefaultSlotName() const override { return MenuSlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName MenuSlotName;

	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	bool IsOpen() const { return bIsOpen; }

	/**
	 * Put the menu on screen, positioned by the style. A no-op while it is already open.
	 *
	 * bFocusMenu is UMG's, and it means what it says: the first navigable thing inside the menu
	 * takes focus, so a pad or a keyboard can walk the menu it just opened without the player first
	 * having to find it. A menu with nothing navigable in it leaves focus where it was rather than
	 * dropping it somewhere arbitrary -- the same call, and the same rule, as UDreamTabView's.
	 *
	 * Defaulted, and last, so every existing Open() keeps compiling and keeps meaning what it did.
	 */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void Open(bool bFocusMenu = false);

	/** Take it off screen and hand it home. A no-op while it is already closed. */
	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "Menu Anchor")
	void ToggleOpen(bool bFocusOnOpen = false);

	/**
	 * Whether anything inside this menu has a menu of ITS own open -- UMG's HasOpenSubMenus.
	 *
	 * The question a menu asks before closing itself on a click elsewhere: the click may have landed
	 * in a submenu, which is still this menu's business. Answered by walking the menu's subtree for
	 * open anchors, because a submenu here is exactly that -- another UDreamMenuAnchor inside the
	 * content -- rather than an entry on an application-wide menu stack.
	 */
	UFUNCTION(BlueprintPure, Category = "Menu Anchor")
	bool HasOpenSubMenus() const;

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

	/** Give focus to the first navigable thing in the open menu, if there is one. */
	void FocusMenuContent();

	UPROPERTY(Transient)
	bool bIsOpen = false;

	/** Set between Elevate and Restore; the popup's resting geometry must not be written while it is. */
	UPROPERTY(Transient)
	bool bPopupElevated = false;

	UPROPERTY(Transient)
	TObjectPtr<UDreamWidget> BlockerNode = nullptr;
};
