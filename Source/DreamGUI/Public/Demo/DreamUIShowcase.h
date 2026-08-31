// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "Controls/DreamDialog.h"
#include "Core/DreamTextUserWidget.h"
#include "DreamUIShowcase.generated.h"

class UDreamDragDropOperation;

/**
 * One row of showcase data: the object shape `each` iterates. Members are read by name through
 * reflection (FDreamUIEachAdapter), so what a .dui writes as `Track.Title` is literally this
 * UPROPERTY. `Label` doubles as the member the history list shows -- one item class for both
 * lists keeps the demo's data model in one place.
 */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIShowcaseTrack : public UObject
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	FText Title;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	FText Artist;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	FText Label;
};

/**
 * What the showcase's 弹窗 button opens: Native.Dialog with the demo's words already in it.
 *
 * A CLASS rather than a configured instance because that is the shape ShowModal takes -- the
 * subsystem instantiates the class itself, so anything a dialog is supposed to say has to be part of
 * what the class IS. Three lines of constructor defaults are the whole subclass; everything else,
 * including which result each button answers with, is UDreamDialog's.
 */
UCLASS(BlueprintType)
class DREAMGUI_API UDreamUIShowcaseDialog : public UDreamDialog
{
	GENERATED_BODY()
public:
	UDreamUIShowcaseDialog();
};

/**
 * The native base for the MediaConsole showcase .dui: every variable and function that file binds
 * against, in one place a Blueprint can parent. It exists so the language demo exercises real
 * bindings -- `<->` onto bMuted, expressions over IsMuted()/GetFade(), `each` over both a variable
 * (Tracks) and a function (GetHistory()) -- without asking the author to first build a data model
 * in Blueprint.
 */
UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamUIShowcasePanel : public UDreamTextUserWidget
{
	GENERATED_BODY()
public:
	UDreamUIShowcasePanel();

	/** The `<->` target of the mute toggle: the toggle writes here, code writes move the toggle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	bool bMuted = false;

	/** Driven by code (or a future slider); the readout bar mirrors it through an expression. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	float MasterVolume = 0.6f;

	/** The `each Track in Tracks` variable source. Holds UDreamUIShowcaseTrack instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	TArray<TObjectPtr<UObject>> Tracks;

	UFUNCTION(BlueprintPure, Category = "Showcase")
	FText GetNowPlaying() const;

	UFUNCTION(BlueprintPure, Category = "Showcase")
	bool IsMuted() const { return bMuted; }

	/** 1 while audible, 0 while muted: the number expressions dim the console by. */
	UFUNCTION(BlueprintPure, Category = "Showcase")
	float GetFade() const { return bMuted ? 0.0f : 1.0f; }

	UFUNCTION(BlueprintPure, Category = "Showcase")
	int32 GetTrackCount() const { return Tracks.Num(); }

	UFUNCTION(BlueprintPure, Category = "Showcase")
	float GetVolume() const { return MasterVolume; }

	/** The `each Entry in GetHistory()` function source. */
	UFUNCTION(BlueprintPure, Category = "Showcase")
	TArray<UObject*> GetHistory() const;

	/** Fill Tracks and the history with recognizable demo rows. */
	UFUNCTION(BlueprintCallable, Category = "Showcase")
	void PopulateDemoData(int32 InTrackCount = 6);

	/**
	 * Seeds the demo rows, unless something already filled them.
	 *
	 * A showcase whose lists are empty until someone remembers to call PopulateDemoData showcases
	 * nothing -- and reads as a broken `each`, which is exactly how it was read.
	 */
	virtual void NativeOnInitialized() override;

	/** `OnClickBP -> HandleOpenModal`: shows ModalDialogClass through the modal subsystem. */
	UFUNCTION()
	void HandleOpenModal();

	UFUNCTION()
	void HandleConfirm();

	/** `OnDropAccepted -> HandleChipDropped` on the drop well: counts landed chips. */
	UFUNCTION()
	void HandleChipDropped(UDreamDragDropOperation* InOperation);

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Showcase")
	int32 DropCount = 0;

	/**
	 * What HandleOpenModal shows. Defaults to UDreamUIShowcaseDialog, which is the only reason the
	 * gallery's 弹窗 button does anything: it was a TSubclassOf nobody ever filled in, so the click
	 * reached the subsystem, found no class, logged a line and stopped. Still overridable -- unset,
	 * the click logs instead of opening nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	TSubclassOf<UDreamUserWidget> ModalDialogClass;

	/** How the last modal closed, for a walkthrough to assert on. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Showcase")
	FName LastModalResult;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Showcase")
	int32 ConfirmCount = 0;

protected:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> History;
};

/**
 * The native base for the ControlsGallery showcase .dui: one handler per input-control family,
 * every one of them appending to a short event log a binding displays live. The gallery's job is
 * INPUT coverage -- click, toggle, drag, scroll, text, selection -- so what the class provides is
 * observation: every interaction leaves a line, and a walkthrough (or a human) reads the panel
 * itself to see what fired.
 */
UCLASS(Abstract, BlueprintType)
class DREAMGUI_API UDreamUIControlsGalleryPanel : public UDreamUIShowcasePanel
{
	GENERATED_BODY()
public:
	/** Feeds the native dropdown its demo options -- a TArray has no .dui text form yet. */
	virtual void NativeOnInitialized() override;

	/** The `<->` target of the gallery's main toggle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Showcase")
	bool bGalleryToggle = false;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Showcase")
	int32 GalleryClickTotal = 0;

	/** The last few events, newest first, one string a Text binding shows. */
	UFUNCTION(BlueprintPure, Category = "Showcase")
	FText GetEventLogText() const;

	UFUNCTION(BlueprintPure, Category = "Showcase")
	FText GetClickTotalText() const;

	UFUNCTION()
	void OnGalleryButton();

	UFUNCTION()
	void OnGallerySecondButton();

	UFUNCTION()
	void OnGalleryToggle(bool InValue);

	UFUNCTION()
	void OnGalleryChipDropped(UDreamDragDropOperation* InOperation);

	UFUNCTION()
	void HandleGallerySpin(float InValue);

	/** Appends one line, newest first, keeping the log short. */
	UFUNCTION(BlueprintCallable, Category = "Showcase")
	void LogEvent(const FString& InLine);

protected:
	UPROPERTY(Transient)
	TArray<FString> EventLines;
};
