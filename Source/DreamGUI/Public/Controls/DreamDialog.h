// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "Interaction/DreamUINavigationScope.h"
#include "DreamDialog.generated.h"

class UDreamButton;
class UDreamDialog;
class UDreamWidget;
class UUIButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamDialogResultEvent, FName, Result);

/**
 * The navigation scope a STANDALONE dialog wears, so Back closes it.
 *
 * Only ever added when no host is already scrimming -- see UDreamDialog::RefreshDimmer. Under
 * UDreamUIModalSubsystem the LAYER carries UDreamUIModalScope and that scope's contract ("Back means
 * close with the Back result") is the one in force; putting a second scope on top of it would
 * silently change which result a hosted dialog answers Back with, which is a promise the subsystem
 * makes to whoever called ShowModal and not this control's to break.
 */
UCLASS(NotBlueprintable, HideDropdown)
class DREAMGUI_API UDreamDialogScope : public UDreamUINavigationScope
{
	GENERATED_BODY()

public:
	virtual bool HandleBackAction_Implementation() override;

	/** The dialog this scope belongs to. Set by it, right after the scope is added. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UDreamDialog> OwnerDialog;
};

/**
 * One button in a dialog's row: what it says, what it answers, and whether it is the loud one.
 *
 * A struct rather than a pair of fixed OK/Cancel knobs because a dialog's button row is genuinely
 * variable -- "Save / Don't Save / Cancel" is as ordinary as "OK / Cancel" -- and because the RESULT
 * is the dialog's whole output. Naming it per button is what lets a caller distinguish the three
 * without counting indices.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamDialogButton
{
	GENERATED_BODY()

	FDreamDialogButton() = default;
	FDreamDialogButton(const FText& InLabel, FName InResult, bool bInIsPrimary)
		: Label(InLabel), Result(InResult), bIsPrimary(bInIsPrimary)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Button")
	FText Label;

	/**
	 * What this button answers with -- the name that reaches CloseTopModal, OnDialogClosed and the
	 * ShowModal callback. "Confirm", "Cancel", "Discard": a word the caller can switch on, never an
	 * index, because inserting a button in the middle must not re-map anyone's handler.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Button")
	FName Result;

	/** Wears FDreamDialogStyle::PrimaryButton instead of ::Button. The confirming one, usually. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog Button")
	bool bIsPrimary = false;
};

/**
 * A dialog whose hierarchy is code, not an asset: a dimmer, a centred panel, a title, a content
 * area with a message in it, and a row of buttons.
 *
 * WHAT IT DOES NOT DO, because UDreamUIModalSubsystem already does it. Opened through ShowModal the
 * dialog is parented to that subsystem's modal LAYER, which is itself the scrim (a full-rect tinted
 * rect block), carries the UUIEventBlocker that eats every pointer event aimed at the world beneath,
 * sorts above the page band, and pushes a UDreamUIModalScope that confines gamepad focus and gives
 * Back the meaning "close with the Back result". None of that is re-implemented here. The division
 * is: the SUBSYSTEM owns the screen (dimming, input blocking, focus, one-at-a-time queueing and
 * carrying the result back to the caller); the DIALOG owns the panel (what it says, which buttons it
 * offers, which result each of them means) and ends the modal by calling CloseTopModal.
 *
 * The dimmer part therefore exists for the OTHER supported arrangement -- standalone. Dropped into a
 * .dui as an ordinary widget (asleep, woken when something needs to ask a question) there is no
 * subsystem layer above it, so the dialog's own dimmer is the only thing darkening the screen and
 * its UUIEventBlocker the only thing stopping clicks reaching what is behind. When a host IS already
 * scrimming -- detected as a blocker anywhere up the parent chain, which is exactly what the modal
 * layer carries -- the dialog puts its own dimmer away rather than darkening the screen twice.
 * Both arrangements are supported; nothing about the tree changes between them.
 *
 * Buttons are real Native.Button instances rather than hand-built faces, so a project styles its
 * buttons once and dialog buttons follow. FDreamDialogStyle carries Button and PrimaryButton for
 * exactly this, and the created buttons are switched to Inline style source: the dialog style has
 * ALREADY resolved (sheet or instance) and the look it names must win, or every dialog button would
 * quietly re-resolve to the sheet's plain button and those two style fields would do nothing.
 *
 * On the button LIST and .dui: the language has no array literal yet, exactly as Native.Dropdown's
 * Options honestly says. Buttons is still fillable from C++, from Blueprint and from the details
 * panel -- and the constructor seeds Cancel + OK (OK primary), so the common dialog needs no array
 * at all:
 *
 *     /Script/DreamGUI.DreamDialog Confirm {
 *         Title   = "删除存档"
 *         Message = "这个操作不能撤销。"
 *         OnDialogClosed -> HandleAnswer
 *     }
 *
 * A fixed OK/Cancel pair with an enum would have spelled that same case no better and had nothing to
 * say about the three-button one, which is why the array won.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Dialog")
class DREAMGUI_API UDreamDialog : public UDreamUIControl
{
	GENERATED_BODY()

public:
	UDreamDialog();

	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	FDreamDialogStyle Style;

	/**
	 * Empty puts the title away entirely rather than reserving a blank line for it.
	 *
	 * BlueprintSetter, like every writable knob on this control: nothing re-derives a control from a
	 * property that changed (the SynchronizeProperties tax UDreamUIControl documents), so a runtime
	 * write straight onto the variable moved the text and left the panel saying the old thing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetTitle", BlueprintSetter = "SetTitle", Category = "Dialog")
	FText Title;

	/** The built-in occupant of the content area. Empty puts it away; the area stays for other content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMessage", BlueprintSetter = "SetMessage", Category = "Dialog")
	FText Message;

	/**
	 * One Native.Button per entry, left to right. Seeded with Cancel + OK; see the class comment.
	 *
	 * BlueprintReadOnly rather than a BlueprintSetter pair, which is the other half of the same
	 * rule: the specs are what the button WIDGETS are made from, so a Blueprint writing this array
	 * in place used to change the data and leave the row built from the old copy -- a dialog showing
	 * buttons it no longer has. SetButtons is the way in, and it reconciles the widgets. The designer
	 * and .dui still author it directly (EditAnywhere), where PostEditChangeProperty does the same.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialog")
	TArray<FDreamDialogButton> Buttons;

	/**
	 * What Close is called with when the dialog is cancelled rather than answered -- RequestCancel,
	 * and whatever a project routes Escape or Back to.
	 *
	 * None (the default) resolves it from the button row instead of guessing: the first NON-primary
	 * button's result, because that is what "Cancel" is in every row this control builds, falling
	 * back to the last button's and finally to "Cancel". Naming it explicitly wins over all of that.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	FName CancelResult;

	/**
	 * Put focus on the default button when the dialog appears.
	 *
	 * The default button is the PRIMARY one (the confirming one, by convention), or the last in the
	 * row when none is marked. Without this a dialog opened with a gamepad came up with focus
	 * wherever the previous screen left it, so the first press went to a control behind the scrim.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	bool bFocusDefaultButton = true;

	/**
	 * Back (Escape, or the pad's B) closes this dialog with its cancel result.
	 *
	 * Only while STANDALONE. Hosted by UDreamUIModalSubsystem the modal layer's own scope already
	 * answers Back -- with the "Back" result its header promises whoever called ShowModal -- and a
	 * second scope on top of it would quietly change that answer. So this fills the hole the
	 * subsystem does not cover rather than overruling the half it does.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	bool bCloseOnBack = true;

	/**
	 * Clicking the dimmer closes the dialog with its cancel result -- the "click outside to dismiss"
	 * every desktop dialog has.
	 *
	 * Off by default, and deliberately: a dialog asking a question that MATTERS ("delete this save?")
	 * must not be dismissable by a stray click, which is why UMG's own dialogs make this opt-in too.
	 * Only ever reachable while the dialog's own dimmer is up -- under the modal subsystem the layer
	 * is what eats clicks, and it is not this control's to put a button on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	bool bCloseOnDimmerClick = false;

	/**
	 * Whether this dialog darkens the screen itself.
	 *
	 * True means "dim unless somebody above me already is" -- a host that scrims (the modal
	 * subsystem's layer) is honoured automatically at construct time, so leaving this on costs a
	 * modal dialog nothing. False is the opt-out for a dialog deliberately shown over a live screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetShowDimmer", BlueprintSetter = "SetShowDimmer", Category = "Dialog")
	bool bShowDimmer = true;

	/** The dialog closed, however it closed. Fires exactly once, BEFORE the modal host tears it down. */
	UPROPERTY(BlueprintAssignable, Category = "Dialog")
	FDreamDialogResultEvent OnDialogClosed;

	/** What the buttons speak, re-broadcast at the control: a consumer binds here, not to a button. */
	UPROPERTY(BlueprintAssignable, Category = "Dialog")
	FDreamDialogResultEvent OnButtonClicked;

	UFUNCTION(BlueprintCallable, Category = "Dialog")
	FText GetTitle() const { return Title; }

	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void SetTitle(const FText& InTitle);

	UFUNCTION(BlueprintCallable, Category = "Dialog")
	FText GetMessage() const { return Message; }

	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void SetMessage(const FText& InMessage);

	UFUNCTION(BlueprintPure, Category = "Dialog")
	TArray<FDreamDialogButton> GetButtons() const { return Buttons; }

	/** Replace the button row wholesale; the widgets are rebuilt from the new specs at once. */
	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void SetButtons(const TArray<FDreamDialogButton>& InButtons);

	UFUNCTION(BlueprintCallable, Category = "Dialog")
	bool GetShowDimmer() const { return bShowDimmer; }

	/**
	 * Whether this dialog darkens the screen itself, re-decided at once.
	 *
	 * A setter because the knob used to be read in exactly one place -- the construct-time host
	 * arrangement -- so turning it off in the designer or at runtime changed nothing anyone could
	 * see until the next time the dialog was built.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void SetShowDimmer(bool bInShowDimmer);

	/**
	 * The result this dialog answers with when it is cancelled rather than answered: CancelResult
	 * when it names one, and otherwise the button row's own cancel (see CancelResult).
	 */
	UFUNCTION(BlueprintPure, Category = "Dialog")
	FName ResolveCancelResult() const;

	/**
	 * Close with that result. What a project's Escape or Back handler calls, and what a scrim click
	 * would call -- this control does not route either itself (see the class comment).
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void RequestCancel();

	/**
	 * Put focus on the default button now. Called for you at construct while bFocusDefaultButton is
	 * on; public because a dialog whose buttons were replaced after it appeared has a new default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void FocusDefaultButton();

	/** The primary button, or the last one when none is marked, or null for an empty row. */
	UFUNCTION(BlueprintPure, Category = "Dialog")
	UDreamButton* GetDefaultButton() const;

	/**
	 * Answer as the default button would -- UMG's "Enter presses the default".
	 *
	 * Enter already reaches a FOCUSED button as a click (it is one of the standalone input actor's
	 * navigation trigger keys), and bFocusDefaultButton puts focus there when the dialog opens, so
	 * the ordinary case needs nothing. This is the same answer for the case where focus has since
	 * moved somewhere that is not a button at all -- a body slot's text field, say -- and for code
	 * that wants to confirm without a pointer.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void SubmitDefaultButton();

	/**
	 * End the dialog with InResult.
	 *
	 * Hosted by the modal subsystem this is CloseTopModal -- the subsystem pops the focus scope,
	 * delivers the result to whoever called ShowModal, destroys the layer and shows the next queued
	 * dialog. Standalone it simply puts the dialog to sleep, which is the state a .dui-placed dialog
	 * was in before it was shown. Either way OnDialogClosed has already fired.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void Close(FName InResult);

	virtual void ApplyStyle() override;

	/** The whole-screen scrim. Asleep whenever a host is already scrimming; see the class comment. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UDreamWidget> DimmerNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UDreamWidget> PanelNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UDreamWidget> TitleNode = nullptr;

	/**
	 * The hole in the middle band. Empty is the normal state; what a host puts here replaces the
	 * built-in message, which is its overlay sibling rather than its child for exactly that reason.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UDreamWidget> BodyNode = nullptr;

	virtual TArray<FName> GetNativeSlotNames() const override { return { BodySlotName }; }
	virtual FName GetDefaultSlotName() const override { return BodySlotName; }

	/** Named once: the declaration, the node's display name and the binding key are the same string. */
	static const FName BodySlotName;

	/** The middle band. Holds MessageNode, and is where a consumer's own content goes. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UDreamWidget> ContentNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UDreamWidget> MessageNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UDreamWidget> ButtonRowNode = nullptr;

	/** One per entry in Buttons, in order. Rebuilt whenever the specs change. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TArray<TObjectPtr<UDreamButton>> ButtonWidgets;

	/** The dimmer's click surface, added only while bCloseOnDimmerClick asks for one. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UUIButton> DimmerBehaviour = nullptr;

	/** The Back handler, added only while standalone. See UDreamDialogScope. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Dialog")
	TObjectPtr<UDreamDialogScope> BackScope = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void OnPartsReady() override;

	/**
	 * The first moment this dialog has a parent: Initialize runs before CreateDreamWidget attaches
	 * it, so neither the self-stretch nor the who-is-already-scrimming question can be answered any
	 * earlier. See RefreshHostArrangement.
	 */
	virtual void NativeOnConstruct() override;

#if WITH_EDITOR
	/** The base re-applies style; the button SPECS live outside ApplyStyle and rebuild here. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	/** Destroy the current button widgets and build one Native.Button per spec. */
	void RebuildButtons();

	/**
	 * Whether the built widgets still answer the specs -- a different COUNT, or a button whose result
	 * moved (the click binding carries the result it was built with, so that name is structural).
	 *
	 * The gate on rebuilding. Everything else a spec can say -- the label, which one is primary -- is
	 * re-pushed by PushButtonStyles without destroying anything, and destroying widgets is what the
	 * list measured at ~40ms a keystroke: it dirties the outliner and the designer force-refreshes
	 * its details view on top of that. Dragging a colour slider must not rebuild a button row.
	 */
	bool ButtonWidgetsAreStale() const;

	/** Push the resolved dialog style's Button/PrimaryButton into the built buttons. */
	void PushButtonStyles(const FDreamDialogStyle& InActive);

	/** Fill the parent (when nothing else is arranging us) and decide whether our dimmer is needed. */
	void RefreshHostArrangement();

	/**
	 * The dimmer half of that, alone: bShowDimmer, narrowed by whether a host is already scrimming.
	 *
	 * Separate because the style push calls it on every restyle and the other half WRITES this
	 * widget's anchors -- re-stretching a dialog somebody had since positioned is not a thing a
	 * colour edit may do.
	 */
	void RefreshDimmer();

	/** Bound per button with its result as the payload; the control-level event carries none. */
	void HandleButtonClicked(FName InResult);

	/** A click on the scrim, while bCloseOnDimmerClick asked for one to mean something. */
	void HandleDimmerClicked();
};
