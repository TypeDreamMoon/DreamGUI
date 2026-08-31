// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamTextInput.generated.h"

class UDreamText;
class UDreamWidget;
class UUITextInput;

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
	/** This instance's own look -- consulted only when StyleSource is Inline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input", meta = (EditCondition = "StyleSource == EDreamUIStyleSource::Inline"))
	FDreamTextInputStyle Style;

	/** Authored text in; mirror of the field's out. A property so .dui and bindings can see it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	FString Text;

	/** Shown while Text is empty and the field is not being edited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	FText Placeholder;

	/** One property where the presets are two assets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Text Input")
	bool bMultiLine = false;

	/** Re-broadcast from the behaviour, so a consumer binds to the control, not to a part of it. */
	UPROPERTY(BlueprintAssignable, Category = "Text Input")
	FDreamTextInputChangedEvent OnTextChanged;

	/** The Enter key (or the multiline submit chord). */
	UPROPERTY(BlueprintAssignable, Category = "Text Input")
	FDreamTextInputChangedEvent OnSubmitted;

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	FString GetText() const;

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetText(const FString& InText);

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
	virtual void NativeOnInitialized() override;

private:
	void HandleTextChanged(const FString& InText);
	void HandleSubmitted(const FString& InText);
};
