// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

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

	/** What HandleOpenModal shows. Unset, the click logs instead of opening nothing. */
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
