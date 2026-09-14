// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
//for EUITextInputType / EUITextInputDisplayType / UDreamTextInputCustomValidation, which this
//control now carries as authored properties rather than leaving them reachable only in C++
#include "Interaction/UITextInput.h"
#include "DreamTextInput.generated.h"

class UDreamText;
class UDreamWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamTextInputChangedEvent, const FString&, Text);

/**
 * A text field whose hierarchy is code, not an asset.
 *
 * The same four nodes both preset Blueprints carry -- a background, a placeholder, and a clip area
 * holding the text -- and one property, bMultiLine, where the presets are two assets. The clip
 * area is not decoration: a field's text is regularly wider than the field, and without a clip the
 * overflow draws over whatever is beside it.
 *
 * UUITextInput does everything hard (caret, selection, IME, key routing); this class builds the
 * geometry it works in and hands it the parts.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Text Input")
class DREAMGUI_API UDreamTextInput : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	FDreamTextInputStyle Style;

	/**
	 * Authored text in; mirror of the field's out. A property so .dui and bindings can see it.
	 *
	 * BlueprintSetter, like the two below it: nothing in this family re-derives a control from a
	 * property that moved (the SynchronizeProperties tax UDreamUIControl documents), so a runtime
	 * write straight onto the variable changed the string and left the field showing the old one.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetText", BlueprintSetter = "SetText", Category = "Text Input")
	FString Text;

	/** Shown while Text is empty and the field is not being edited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetPlaceholder", BlueprintSetter = "SetPlaceholder", Category = "Text Input")
	FText Placeholder;

	/** One property where the presets are two assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMultiLine", BlueprintSetter = "SetMultiLine", Category = "Text Input")
	bool bMultiLine = false;

	/*
	 * Everything below is the behaviour's, surfaced.
	 *
	 * UUITextInput has had all of it since the beginning; the control exposed Text, Placeholder and
	 * bMultiLine, so a password field could not be written as Native.TextInput at all and a .dui
	 * author had no way to reach a validator, a length cap or the read-only flag. These are pushed
	 * in ApplyStyle alongside the rest, so a .dui line, a Blueprint default and a live SetX all land
	 * in the same place.
	 */

	/** Which characters the field accepts. Standard accepts anything. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	EUITextInputType InputType = EUITextInputType::Standard;

	/** Only consulted when InputType is Custom. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, Category = "Text Input", meta = (EditCondition = "InputType==EUITextInputType::Custom"))
	TObjectPtr<UDreamTextInputCustomValidation> CustomValidation = nullptr;

	/** Password draws every character as PasswordChar; it does not change what the field accepts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	EUITextInputDisplayType DisplayType = EUITextInputDisplayType::Standard;

	/** The character a password field draws. One character; anything longer keeps its first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	FString PasswordChar = TEXT("*");

	/** Selectable and copyable, not editable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	bool bReadOnly = false;

	/** Longest text the field will hold. 0 means no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input", meta = (ClampMin = "0"))
	int32 MaxLength = 0;

	/** Keys the field must not swallow -- put navigation keys here, e.g. Tab and the arrows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	TArray<FKey> IgnoreKeys;

	/** In multiline mode, Enter with one of these held submits instead of adding a line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input", meta = (EditCondition = "bMultiLine"))
	TArray<FKey> MultiLineSubmitFunctionKeys;

	/** Select the whole value when the field starts being edited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	bool bSelectAllWhenActivateInput = true;

	/** Start editing as soon as gamepad/keyboard navigation lands on the field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	bool bAutoActivateInputWhenNavigateIn = false;

	/** Commit the value when the edit ends without an Enter -- clicking away, navigating away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	bool bSubmitWhenDeactivate = true;

	/** The edit menu on right click / long press / the pad's Menu button. UMG's AllowContextMenu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	bool bAllowContextMenu = true;

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Text Input")
	FDreamTextInputChangedEvent OnTextChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Text Input")
	FDreamTextInputChangedEvent OnValueChangedBP;


	/**
	 * The Enter key (or the multiline submit chord). The compatibility spelling of OnTextCommitted:
	 * both fire from the same moment with the same string. New code speaks OnTextCommitted.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Text Input")
	FDreamTextInputChangedEvent OnSubmitted;

	/**
	 * UMG's spelling of the same moment (its editable text pairs OnTextChanged with
	 * OnTextCommitted). Carries just the string -- UMG's commit-method enum describes Slate input
	 * routing this control does not have, and a fake one would be a lie in the signature.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Text Input")
	FDreamTextInputChangedEvent OnTextCommitted;

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	FString GetText() const;

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetText(const FString& InText);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	FText GetPlaceholder() const { return Placeholder; }

	/** The greyed words shown while the field is empty, re-pushed at once. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetPlaceholder(const FText& InPlaceholder);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetMultiLine() const { return bMultiLine; }

	/**
	 * Single- or multi-line, re-pushed at once. A whole style push rather than one flag on the
	 * behaviour: the line count decides the field's height and what its clip has to allow, both of
	 * which are written there.
	 */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetMultiLine(bool bInMultiLine);

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> BackgroundNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> PlaceholderNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> ClipNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> TextNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UUITextInput> InputBehaviour = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	void HandleTextChanged(const FString& InText);
	void HandleSubmitted(const FString& InText);
};
