// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
//for EUITextInputType / EUITextInputDisplayType / UDreamTextInputCustomValidation, which this
//control now carries as authored properties rather than leaving them reachable only in C++
#include "Interaction/UITextInput.h"
//for EDreamUITextParagraphHorizontalAlign, which the field's Justification is spelled in: one
//paragraph-alignment enum for the whole library rather than a second one meaning left/centre/right
#include "Core/DreamUITextData.h"
#include "DreamTextInput.generated.h"

class UDreamText;
class UDreamWidget;
//SetFontMaterial names one and forwards it to the paragraph; it never asks a material anything.
class UMaterialInterface;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetStyle", BlueprintSetter = "SetStyle", Category = "Text Input")
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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetInputType", BlueprintSetter = "SetInputType", Category = "Text Input")
	EUITextInputType InputType = EUITextInputType::Standard;

	/** Only consulted when InputType is Custom. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadWrite, BlueprintGetter = "GetCustomValidation", BlueprintSetter = "SetCustomValidation", Category = "Text Input", meta = (EditCondition = "InputType==EUITextInputType::Custom"))
	TObjectPtr<UDreamTextInputCustomValidation> CustomValidation = nullptr;

	/** Password draws every character as PasswordChar; it does not change what the field accepts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetDisplayType", BlueprintSetter = "SetDisplayType", Category = "Text Input")
	EUITextInputDisplayType DisplayType = EUITextInputDisplayType::Standard;

	/** The character a password field draws. One character; anything longer keeps its first. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetPasswordChar", BlueprintSetter = "SetPasswordChar", Category = "Text Input")
	FString PasswordChar = TEXT("*");

	/** Selectable and copyable, not editable. UMG's IsReadOnly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetIsReadOnly", BlueprintSetter = "SetIsReadOnly", Category = "Text Input")
	bool bReadOnly = false;

	/** Longest text the field will hold. 0 means no limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMaxLength", BlueprintSetter = "SetMaxLength", Category = "Text Input", meta = (ClampMin = "0"))
	int32 MaxLength = 0;

	/** Keys the field must not swallow -- put navigation keys here, e.g. Tab and the arrows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetIgnoreKeys", BlueprintSetter = "SetIgnoreKeys", Category = "Text Input")
	TArray<FKey> IgnoreKeys;

	/** In multiline mode, Enter with one of these held submits instead of adding a line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMultiLineSubmitFunctionKeys", BlueprintSetter = "SetMultiLineSubmitFunctionKeys", Category = "Text Input", meta = (EditCondition = "bMultiLine"))
	TArray<FKey> MultiLineSubmitFunctionKeys;

	/** Select the whole value when the field starts being edited. UMG's SelectAllTextWhenFocused. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSelectAllWhenActivateInput", BlueprintSetter = "SetSelectAllWhenActivateInput", Category = "Text Input")
	bool bSelectAllWhenActivateInput = true;

	/** Start editing as soon as gamepad/keyboard navigation lands on the field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAutoActivateInputWhenNavigateIn", BlueprintSetter = "SetAutoActivateInputWhenNavigateIn", Category = "Text Input")
	bool bAutoActivateInputWhenNavigateIn = false;

	/** Commit the value when the edit ends without an Enter -- clicking away, navigating away. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSubmitWhenDeactivate", BlueprintSetter = "SetSubmitWhenDeactivate", Category = "Text Input")
	bool bSubmitWhenDeactivate = true;

	/** The edit menu on right click / long press / the pad's Menu button. UMG's AllowContextMenu. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetAllowContextMenu", BlueprintSetter = "SetAllowContextMenu", Category = "Text Input")
	bool bAllowContextMenu = true;

	/**
	 * Escape / Back throws the edit away instead of keeping it -- UMG's RevertTextOnEscape.
	 * Off, as UMG's is and as this field behaved before the knob existed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetRevertTextOnEscape", BlueprintSetter = "SetRevertTextOnEscape", Category = "Text Input")
	bool bRevertTextOnEscape = false;

	/**
	 * Stop editing once Enter commits -- UMG's ClearKeyboardFocusOnCommit.
	 *
	 * TRUE here where UMG's is false, because true is what this field has always done and a default
	 * that changed existing screens' behaviour is not a default. Off keeps the keyboard for the next
	 * value.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetClearKeyboardFocusOnCommit", BlueprintSetter = "SetClearKeyboardFocusOnCommit", Category = "Text Input")
	bool bClearKeyboardFocusOnCommit = true;

	/** Select the value after a commit that kept the edit going -- UMG's SelectAllTextOnCommit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetSelectAllTextOnCommit", BlueprintSetter = "SetSelectAllTextOnCommit", Category = "Text Input", meta = (EditCondition = "!bClearKeyboardFocusOnCommit"))
	bool bSelectAllTextOnCommit = false;

	/** Whether gaining the edit moves the caret to the end -- UMG's IsCaretMovedWhenGainFocus. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetIsCaretMovedWhenGainFocus", BlueprintSetter = "SetIsCaretMovedWhenGainFocus", Category = "Text Input")
	bool bIsCaretMovedWhenGainFocus = true;

	/**
	 * How the typed text sits across the field -- UMG's Justification.
	 *
	 * Left by default, which is what the style push wrote unconditionally before this was a knob. The
	 * placeholder follows it, because a hint that sat somewhere other than where typing will appear
	 * is a hint about the wrong field.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetJustification", BlueprintSetter = "SetJustification", Category = "Text Input")
	EDreamUITextParagraphHorizontalAlign Justification = EDreamUITextParagraphHorizontalAlign::Left;

	/**
	 * A floor under the width the control asks a content-sized parent for -- UMG's
	 * MinimumDesiredWidth. Zero means no opinion, which is every field that exists today.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetMinimumDesiredWidth", BlueprintSetter = "SetMinimumDesiredWidth", Category = "Text Input", meta = (ClampMin = "0.0"))
	float MinimumDesiredWidth = 0.0f;

	/**
	 * Which way the paragraph reads -- the half of UMG's ShapedTextOptions that means anything here.
	 *
	 * Auto asks the bidi algorithm, which is what the field always did and therefore the default. The
	 * other two state it outright, for a screen that already knows its culture. UMG's other half,
	 * TextShapingMethod, has no home on a widget in this library: whether the shaper runs at all is
	 * decided when the plugin is compiled (WITH_HARFBUZZ), not per control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetTextFlowDirection", BlueprintSetter = "SetTextFlowDirection", Category = "Text Input")
	EDreamTextFlowDirection TextFlowDirection = EDreamTextFlowDirection::Auto;

	/**
	 * What a value too long for the box does WHILE NOBODY IS EDITING IT -- UMG's OverflowPolicy.
	 *
	 * Qualified that way on purpose; see UUITextInput::OverflowPolicy, which owns the rule. Clip is
	 * the default and is exactly what every field does today.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetTextOverflowPolicy", BlueprintSetter = "SetTextOverflowPolicy", Category = "Text Input")
	ETextOverflowPolicy OverflowPolicy = ETextOverflowPolicy::Clip;

	/** Which virtual keyboard a mobile platform summons. Default derives it from InputType. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetKeyboardType", BlueprintSetter = "SetKeyboardType", Category = "Text Input", AdvancedDisplay)
	TEnumAsByte<EVirtualKeyboardType::Type> KeyboardType = EVirtualKeyboardType::Default;

	/**
	 * Platform options for that keyboard -- autocorrect and whatever the platform adds later.
	 *
	 * The one knob on this control with no Blueprint accessor, and not by choice: FVirtualKeyboardOptions
	 * is a plain USTRUCT in Slate with no BlueprintType on it, so neither the property nor a setter
	 * can cross into Blueprint. UMG's own editable text carries it the same way, for the same reason.
	 * The designer, .dui and C++ all reach it.
	 */
	UPROPERTY(EditAnywhere, Category = "Text Input", AdvancedDisplay)
	FVirtualKeyboardOptions VirtualKeyboardOptions;

	/** Which activations raise it. OnAllFocusEvents keeps what the field did. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetVirtualKeyboardTrigger", BlueprintSetter = "SetVirtualKeyboardTrigger", Category = "Text Input", AdvancedDisplay)
	EVirtualKeyboardTrigger VirtualKeyboardTrigger = EVirtualKeyboardTrigger::OnAllFocusEvents;

	/** What dismissing it means. TextCommitOnDismiss keeps what the field did. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, BlueprintGetter = "GetVirtualKeyboardDismissAction", BlueprintSetter = "SetVirtualKeyboardDismissAction", Category = "Text Input", AdvancedDisplay)
	EVirtualKeyboardDismissAction VirtualKeyboardDismissAction = EVirtualKeyboardDismissAction::TextCommitOnDismiss;

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

	/**
	 * UMG's HintText, which is this control's Placeholder under the name a UMG reader looks for.
	 * Forwards; there is one string and one writer, so the two spellings cannot drift apart.
	 */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	FText GetHintText() const { return GetPlaceholder(); }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetHintText(const FText& InHintText) { SetPlaceholder(InHintText); }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	FDreamTextInputStyle GetStyle() const { return Style; }

	/** The whole look at once, re-pushed. The style struct is one decision, so it moves as one. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetStyle(const FDreamTextInputStyle& InStyle);

	/*
	 * The behaviour's knobs, with setters that REACH it.
	 *
	 * Every one of these was already an authored property, and every one of them was already pushed
	 * in ApplyStyle -- so a .dui line and a Blueprint default worked. What did not work was writing
	 * one at runtime: nothing in this family re-derives a control from a property that moved, so a
	 * Blueprint Set node changed the variable and the field went on behaving exactly as before.
	 */

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	EUITextInputType GetInputType() const { return InputType; }

	/** Which characters are accepted. Re-runs the rules over the text already in the field. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetInputType(EUITextInputType InInputType);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	UDreamTextInputCustomValidation* GetCustomValidation() const { return CustomValidation; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetCustomValidation(UDreamTextInputCustomValidation* InCustomValidation);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	EUITextInputDisplayType GetDisplayType() const { return DisplayType; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetDisplayType(EUITextInputDisplayType InDisplayType);

	/** UMG's IsPassword, which here is DisplayType's two answers under the name UMG gives them. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetIsPassword() const { return DisplayType == EUITextInputDisplayType::Password; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetIsPassword(bool bInIsPassword);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	FString GetPasswordChar() const { return PasswordChar; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetPasswordChar(const FString& InPasswordChar);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetIsReadOnly() const { return bReadOnly; }

	/** Selectable, not editable. A field being typed into when this goes on stops being edited. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetIsReadOnly(bool bInReadOnly);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	int32 GetMaxLength() const { return MaxLength; }

	/** 0 means no limit. Shortening it truncates what the field already holds. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetMaxLength(int32 InMaxLength);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	TArray<FKey> GetIgnoreKeys() const { return IgnoreKeys; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetIgnoreKeys(const TArray<FKey>& InIgnoreKeys);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	TArray<FKey> GetMultiLineSubmitFunctionKeys() const { return MultiLineSubmitFunctionKeys; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetMultiLineSubmitFunctionKeys(const TArray<FKey>& InKeys);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetSelectAllWhenActivateInput() const { return bSelectAllWhenActivateInput; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetSelectAllWhenActivateInput(bool bInSelectAll);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetAutoActivateInputWhenNavigateIn() const { return bAutoActivateInputWhenNavigateIn; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetAutoActivateInputWhenNavigateIn(bool bInAutoActivate);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetSubmitWhenDeactivate() const { return bSubmitWhenDeactivate; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetSubmitWhenDeactivate(bool bInSubmitWhenDeactivate);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetAllowContextMenu() const { return bAllowContextMenu; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetAllowContextMenu(bool bInAllowContextMenu);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetRevertTextOnEscape() const { return bRevertTextOnEscape; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetRevertTextOnEscape(bool bInRevertTextOnEscape);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetClearKeyboardFocusOnCommit() const { return bClearKeyboardFocusOnCommit; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetClearKeyboardFocusOnCommit(bool bInClearKeyboardFocusOnCommit);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetSelectAllTextOnCommit() const { return bSelectAllTextOnCommit; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetSelectAllTextOnCommit(bool bInSelectAllTextOnCommit);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool GetIsCaretMovedWhenGainFocus() const { return bIsCaretMovedWhenGainFocus; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetIsCaretMovedWhenGainFocus(bool bInIsCaretMovedWhenGainFocus);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	EDreamUITextParagraphHorizontalAlign GetJustification() const { return Justification; }

	/** Pushed onto the typed text AND the placeholder: a hint belongs where typing will appear. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetJustification(EDreamUITextParagraphHorizontalAlign InJustification);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	float GetMinimumDesiredWidth() const { return MinimumDesiredWidth; }

	/** UMG names the setter SetMinDesiredWidth; both spellings are here, one implementation. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetMinimumDesiredWidth(float InMinimumDesiredWidth);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetMinDesiredWidth(float InMinDesiredWidth) { SetMinimumDesiredWidth(InMinDesiredWidth); }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	EDreamTextFlowDirection GetTextFlowDirection() const { return TextFlowDirection; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetTextFlowDirection(EDreamTextFlowDirection InFlowDirection);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	ETextOverflowPolicy GetTextOverflowPolicy() const { return OverflowPolicy; }

	/** Forwards to the behaviour, which owns the one field on the paragraph this shares with the line mode. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetTextOverflowPolicy(ETextOverflowPolicy InOverflowPolicy);

	/**
	 * The typeface this field draws with -- UMG's GetFont/SetFont, minus the two things FSlateFontInfo
	 * packs in beside it: the size is Style.FontSize and the outline is in the text style.
	 *
	 * Reads and writes Style.Font, so it obeys StyleSource like every other piece of the look: on a
	 * field taking its style from the project sheet, the sheet's font is the answer.
	 */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	UDreamUIFontData_BaseObject* GetFont() const { return Style.Font; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetFont(UDreamUIFontData_BaseObject* InFont);

	/**
	 * The material the glyphs are drawn with -- UMG's SetFontMaterial, with one difference worth
	 * knowing: it is ONE material for the whole paragraph rather than one for the font face, because
	 * that is what the text renderer takes.
	 */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	UMaterialInterface* GetFontMaterial() const;

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetFontMaterial(UMaterialInterface* InMaterial);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	TEnumAsByte<EVirtualKeyboardType::Type> GetKeyboardType() const { return KeyboardType; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetKeyboardType(TEnumAsByte<EVirtualKeyboardType::Type> InKeyboardType);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	EVirtualKeyboardTrigger GetVirtualKeyboardTrigger() const { return VirtualKeyboardTrigger; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetVirtualKeyboardTrigger(EVirtualKeyboardTrigger InTrigger);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	EVirtualKeyboardDismissAction GetVirtualKeyboardDismissAction() const { return VirtualKeyboardDismissAction; }

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetVirtualKeyboardDismissAction(EVirtualKeyboardDismissAction InDismissAction);

	/** No Blueprint accessor by the engine's own constraint -- see the property. */
	FVirtualKeyboardOptions GetVirtualKeyboardOptions() const { return VirtualKeyboardOptions; }
	void SetVirtualKeyboardOptions(const FVirtualKeyboardOptions& InOptions);

	/*
	 * The error state, which UMG's text box has had since it existed and this one had not.
	 *
	 * A field that refused a value said nothing: the caller's only way to report it was a separate
	 * label somebody had to remember to place, colour and hide again. Slate answers this with an
	 * error-reporting widget packed at the right of the box, and so does this: one text node, asleep
	 * while there is no error, right-aligned over the field in the style's error colour.
	 *
	 * The message is CONTENT (which value is wrong), so it lives on the control; the colour is theme,
	 * so it lives in the style.
	 */

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	FText GetError() const { return ErrorText; }

	/** Show a message. An empty one is a clear, the way UMG's SetError behaves. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SetError(const FText& InError);

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void ClearError();

	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool HasError() const { return !ErrorText.IsEmpty(); }

	/** Whether the field has anything selected right now -- UMG's IsAnyTextSelected. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	bool IsAnyTextSelected() const;

	/** Select the whole value, the way UMG's SelectAllText does. */
	UFUNCTION(BlueprintCallable, Category = "Text Input")
	void SelectAllText();

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> BackgroundNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> PlaceholderNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> ClipNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> TextNode = nullptr;

	/**
	 * The error message's paragraph. An OPTIONAL part: every template authored for this control
	 * before the error state existed has no node by that name, and a control that reported those as
	 * missing would turn an addition into a warning on every existing asset.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UDreamWidget> ErrorNode = nullptr;

	/**
	 * The message in effect, empty when there is none.
	 *
	 * Transient and read-only, like the key selector's armed flag and for the same reason: an error
	 * is something that just happened to a value, not a property of the field, and a .dui able to
	 * author one would ship a screen that opens already complaining.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	FText ErrorText;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Text Input")
	TObjectPtr<UUITextInput> InputBehaviour = nullptr;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	void HandleTextChanged(const FString& InText);
	void HandleSubmitted(const FString& InText);

	/** The floor under the control's width, and under what the paragraph asks a parent for. */
	void PushMinimumDesiredWidth();

	/** The message onto its paragraph, and that paragraph awake only while there is a message. */
	void PushError();
};
