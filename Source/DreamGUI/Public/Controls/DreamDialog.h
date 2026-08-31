// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamDialog.generated.h"

class UDreamButton;
class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamDialogResultEvent, FName, Result);

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

	/** Empty puts the title away entirely rather than reserving a blank line for it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	FText Title;

	/** The built-in occupant of the content area. Empty puts it away; the area stays for other content. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	FText Message;

	/** One Native.Button per entry, left to right. Seeded with Cancel + OK; see the class comment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	TArray<FDreamDialogButton> Buttons;

	/**
	 * Whether this dialog darkens the screen itself.
	 *
	 * True means "dim unless somebody above me already is" -- a host that scrims (the modal
	 * subsystem's layer) is honoured automatically at construct time, so leaving this on costs a
	 * modal dialog nothing. False is the opt-out for a dialog deliberately shown over a live screen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dialog")
	bool bShowDimmer = true;

	/** The dialog closed, however it closed. Fires exactly once, BEFORE the modal host tears it down. */
	UPROPERTY(BlueprintAssignable, Category = "Dialog")
	FDreamDialogResultEvent OnDialogClosed;

	/** What the buttons speak, re-broadcast at the control: a consumer binds here, not to a button. */
	UPROPERTY(BlueprintAssignable, Category = "Dialog")
	FDreamDialogResultEvent OnButtonClicked;

	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void SetTitle(const FText& InTitle);

	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void SetMessage(const FText& InMessage);

	/** Replace the button row wholesale; the widgets are rebuilt from the new specs at once. */
	UFUNCTION(BlueprintCallable, Category = "Dialog")
	void SetButtons(const TArray<FDreamDialogButton>& InButtons);

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

protected:
	virtual void NativeOnInitialized() override;

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

	/** Push the resolved dialog style's Button/PrimaryButton into the built buttons. */
	void PushButtonStyles(const FDreamDialogStyle& InActive);

	/** Fill the parent (when nothing else is arranging us) and decide whether our dimmer is needed. */
	void RefreshHostArrangement();

	/** Bound per button with its result as the payload; the control-level event carries none. */
	void HandleButtonClicked(FName InResult);
};
