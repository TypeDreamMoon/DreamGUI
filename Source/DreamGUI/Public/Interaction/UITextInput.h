// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Interaction/UISelectable.h"
#include "Components/InputComponent.h"
#include "Event/DreamUIEventDelegate.h"
#include "Event/DreamDelegateDeclaration.h"
#include "Event/Interface/DreamPointerClickInterface.h"
#include "Event/Interface/DreamPointerDoubleClickInterface.h"
#include "Event/Interface/DreamPointerDragInterface.h"
#include "Widgets/Input/IVirtualKeyboardEntry.h"
//EVirtualKeyboardType: the enum UMG's editable text spells its KeyboardType with. Named rather than
//re-declared, because a second enum meaning the same six keyboards is a second thing to keep true.
#include "Components/SlateWrapperTypes.h"
//EVirtualKeyboardTrigger / EVirtualKeyboardDismissAction, Slate's own spellings of the other two
//halves of the mobile keyboard contract.
#include "Widgets/Text/ISlateEditableTextWidget.h"
//ETextOverflowPolicy, which UMG's editable text spells its OverflowPolicy with
#include "Styling/SlateTypes.h"
#include "GenericPlatform/ITextInputMethodSystem.h"
#include "Core/Components/DreamText.h"
#include "Widgets/Layout/SBox.h"
#include "UITextInput.generated.h"


class UDreamSprite;
class FModifierKeysState;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUITextInputValueChangedEvent, FString, Value);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUITextInputActivateEvent, bool, Value);

UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced, EditInlineNew)
class UDreamTextInputCustomValidation : public UObject
{
	GENERATED_BODY()
public:
	UDreamTextInputCustomValidation();
	/**
	 * Verify input string, return true if the input string is good to use, false otherwise.
	 * @param InTextInput	The UITextInputComponent object reference which call this function.
	 * @param InString	The will display string value, for check if it is valid. If not, then display origin string value.
	 * @param InIndexOfInsertedChar	New inserted char index in InString.
	 * @return true if the input string is good to use, false otherwise.
	 */
	virtual bool OnValidateInput(UUITextInput* InTextInput, const FString& InString, int InIndexOfInsertedChar);
protected:
	/** use this to tell if the class is compiled from blueprint, only blueprint can execute ReceiveXXX. */
	bool bCanExecuteBlueprintEvent = false;
	/**
	 * Verify input string, return true if the input string is good to use, false otherwise.
	 * @param InTextInput	The UITextInputComponent object reference which call this function.
	 * @param InString	The will display string value, for check if it is valid. If not, then display origin string value.
	 * @param InIndexOfInsertedChar	New inserted char index in InString.
	 * @return true if the input string is good to use, false otherwise.
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "OnValidateInput"), Category = "DreamGUI")
		bool ReceiveOnValidateInput(UUITextInput* InTextInput, const FString& InString, int InIndexOfInsertedChar);
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EUITextInputType:uint8
{
	/** No validation. Any input is valid. */
	Standard = 0,
	/**
	 * Allow whole numbers (positive or negative).
	 * Characters 0-9 and - (dash / minus sign) are allowed. The dash is only allowed as the first character.
	 */
	IntegerNumber = 1,
	/**
	 * Allows decimal numbers (positive or negative).
	 * Characters 0-9, . (dot), and - (dash / minus sign) are allowed. The dash is only allowed as the first character. Only one dot in the string is allowed.
	 */
	DecimalNumber = 2,
	/**
	 * Allows letters A-Z, a-z and numbers 0-9.
	 */
	Alphanumeric = 5,
	/**
	 * Allows the characters that are allowed in an email address.
	 * Allows characters A-Z, a.z, 0-9, @, . (dot), !, #, $, %, &amp;, ', *, +, -, /, =, ?, ^, _, `, {, |, }, and ~.
	 * Only one @ is allowed in the string and more than one dot in a row are not allowed. Note that the character validation does not validate the entire string as being a valid email address since it only does validation on a per-character level, resulting in the typed character either being added to the string or not.
	 */
	EmailAddress = 6,
	/**
	 * Display as password, without any validation.
	 * NOTE!!! This type will be deprecate, use DisplayType.Password instead.
	 */
	Password = 3,
	/** Use a user implemented *CustomValidation* to do custom input check. */
	Custom = 7,
};
UENUM(BlueprintType, Category = DreamGUI)
enum class EUITextInputDisplayType :uint8
{
	Standard,
	/** Display as password. */
	Password,
};

UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UUITextInput : public UUISelectable, public IDreamPointerClickInterface, public IDreamPointerDoubleClickInterface, public IDreamPointerDragInterface
{
	GENERATED_BODY()
	
protected:
	virtual void Awake() override;
	virtual void Tick(float DeltaTime) override;
	virtual void OnDestroy() override;
	/**
	 * Ends an edit still open when the field leaves the hierarchy. OnDestroy cannot be relied on for
	 * it: a behaviour only reaches OnDestroy through EndPlay, and only if it began play -- a field in a
	 * tree that was torn down without ever beginning play never gets there, and its edit (the static
	 * ActiveTextInput, the key agent) outlived the field. Unregistering is what every teardown passes
	 * through, and DestroyWidget unregisters the whole subtree before any of it ends play.
	 */
	virtual void OnUnregister() override;
#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
protected:
	friend class FUITextInputCustomization;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		TWeakObjectPtr<UDreamText> TextVisual;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		FString Text;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		EUITextInputType InputType;
	/** Use this to do custom validation. Only valid when InputType = Custom */
	UPROPERTY(EditAnywhere, Instanced, Category = "DreamGUI-Input", meta = (EditCondition = "InputType==EUITextInputType::Custom"))
		TObjectPtr<UDreamTextInputCustomValidation> CustomValidation = nullptr;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		EUITextInputDisplayType DisplayType = EUITextInputDisplayType::Standard;
	//password display character
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		FString PasswordChar = TEXT("*");
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bAllowMultiLine = false;
	/**
	 * This will be used in multiline mode, when hit enter, if one of these keys is also pressing then the input will submit, otherwise a new line will be added.
	 * Commonly only use control/shift/alt key.
	 * Not allow "Enter" key.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (EditCondition="bAllowMultiLine"))
		TArray<FKey> MultiLineSubmitFunctionKeys;
	/** If PlaceHolderActor is a UITextActor, then mobile virtual keyboard's hint text will get from PlaceHolderActor. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		TWeakObjectPtr<UDreamWidget> PlaceHolder;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		float CaretBlinkRate = 0.5f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		float CaretWidth = 2.0f;
	/**
	 * Light, because the caret is drawn ON the field and every shipped field is dark: the library's
	 * FDreamTextInputStyle paints the box (38,42,52) and its text (230,233,240). The old (50,50,50)
	 * was four values away from that background -- an invisible caret on the default theme, which is
	 * the one state a text field cannot afford. It follows the default text colour instead.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		FColor CaretColor = FColor(230, 233, 240, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		FColor SelectionColor = FColor(168, 206, 255, 128);
	/**
	 * The line drawn under text an IME is still composing, the way every text field marks the
	 * difference between "being typed" and "typed". Slate's editable text does the same thing with
	 * its EditableText.CompositionBackground brush; a mesh UI has no text-run decoration to hang it
	 * on, so it is drawn the way the selection highlight already is -- a strip per visual run.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		FColor CompositionUnderlineColor = FColor(230, 233, 240, 255);
	/** How thick that line is, in UI units. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (ClampMin = "0.0"))
		float CompositionUnderlineThickness = 1.5f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		FVirtualKeyboardOptions VirtualKeyboardOptions;
	//Ignore these keys input. eg, if use tab and arrow keys for navigation then you should put tab and arrow keys in this array
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		TArray<FKey> IgnoreKeys;
	/** Automatic activate input when use navigation input and navigate in this. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bAutoActivateInputWhenNavigateIn = false;
	/** Select all text value when activate input. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bSelectAllWhenActivateInput = true;
	/** Read only text block, can copy text content, but not editable. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bReadOnly = false;
	/**
	 * Largest number of characters the field will hold. 0 means no limit, which is what every field
	 * was before this existed. All four write roads answer to it -- typing, pasting, SetText and the
	 * IME's SetTextInRange -- because a limit only one road honours is not a limit.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (ClampMin = "0"))
		int32 MaxLength = 0;
	/**
	 * Commit the value when the edit ends without an Enter -- clicking away, navigating away, Back /
	 * Escape ending the edit, the mobile keyboard being dismissed. UMG's editable text has always
	 * reported that moment through OnTextCommitted; DreamGUI only ever reported Enter, so a field
	 * the player filled in and clicked out of never told anyone its value.
	 * An Enter that already submitted does not submit twice.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bSubmitWhenDeactivate = true;
	/**
	 * Escape / Back throws the edit away instead of keeping it -- UMG's RevertTextOnEscape.
	 *
	 * The text the field held when the edit STARTED is put back -- a player who opened a name field,
	 * typed half a name and changed their mind meant "undo this", not "store what I got to" -- and the
	 * revert is then reported ONCE through the submit/commit events, carrying the restored text, as
	 * UMG's RestoreOriginalText reports it through OnTextCommitted: whoever stores the value on commit
	 * stores what the field now holds. An edit that changed nothing reverts nothing and reports
	 * nothing. The edit ends either way (Escape is Back here). Off by default, which is both UMG's
	 * default and what this field did before the knob existed -- Back ended the edit and the
	 * half-typed value stayed.
	 *
	 * Only the CANCEL road reads it (see CancelInput). Clicking away is not a cancel: nothing was
	 * said about the value, so bSubmitWhenDeactivate still decides what that moment means.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bRevertTextOnEscape = false;
	/**
	 * Stop editing once Enter commits the value -- UMG's ClearKeyboardFocusOnCommit.
	 *
	 * TRUE by default, where UMG's is false, because true is what this field has always done: Enter
	 * submitted and immediately ended the edit, with no way to ask for anything else. Turning it off
	 * is the new answer -- the field keeps the keyboard, ready for the next value, which is what a
	 * row of fields a player tabs through wants.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bClearKeyboardFocusOnCommit = true;
	/**
	 * Select the whole value after a commit that KEPT the edit going -- UMG's SelectAllTextOnCommit.
	 * Ready to be typed over. Only meaningful while the commit does not also end the edit: selecting
	 * the contents of a field nobody is editing shows a highlight with no caret in it.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (EditCondition = "!bClearKeyboardFocusOnCommit"))
		bool bSelectAllTextOnCommit = false;
	/**
	 * Whether gaining the edit moves the caret -- UMG's IsCaretMovedWhenGainFocus.
	 *
	 * On (the default, and what the field always did), an activation that nobody gave a position for
	 * puts the caret at the end of the text. Off, the caret stays on the index the previous edit left
	 * it at, which is what a field being re-entered by code rather than by a player wants.
	 * A click still places the caret where it landed either way: the click said where.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bIsCaretMovedWhenGainFocus = true;
	/**
	 * What a value too long for the box does WHILE NOBODY IS EDITING IT -- UMG's OverflowPolicy.
	 *
	 * It has to be qualified that way, because on an editable field the paragraph's own overflow type
	 * is not a display choice at all: DreamTextLayout only looks for break opportunities under
	 * VerticalOverflow, so the overflow type IS the line-mode switch, and the caret's visible-window
	 * arithmetic reads the same field. Two writers would fight over it.
	 *
	 * So this one only speaks in the gap: while the field is not being edited, Ellipsis (or
	 * MultilineEllipsis) shows the value cut short with an ellipsis rather than simply clipped;
	 * activating the edit puts the line mode's own overflow back, because that is what the caret and
	 * the wrap need, and leaving the edit restores the ellipsis. Clip is the default and is exactly
	 * what every field does today.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		ETextOverflowPolicy OverflowPolicy = ETextOverflowPolicy::Clip;
	/**
	 * Which virtual keyboard a mobile platform should summon -- UMG's KeyboardType.
	 *
	 * Default means "whatever this field's input type implies", which is what the field has always
	 * derived on its own (a decimal field asks for the number pad, a password field for the password
	 * keyboard). Any other value states the keyboard outright, for the cases the input type cannot
	 * express -- a Standard field that holds an email address, or a web address.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		TEnumAsByte<EVirtualKeyboardType::Type> KeyboardType = EVirtualKeyboardType::Default;
	/**
	 * Which activations summon the virtual keyboard -- UMG's VirtualKeyboardTrigger.
	 *
	 * OnAllFocusEvents by default, where UMG's default is OnFocusByPointer, because every activation
	 * used to raise it: a field navigated into with a pad still needs a keyboard, since a pad has no
	 * other way to type. OnFocusByPointer is the opt-in for a screen that drives its own.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		EVirtualKeyboardTrigger VirtualKeyboardTrigger = EVirtualKeyboardTrigger::OnAllFocusEvents;
	/**
	 * What dismissing the virtual keyboard means -- UMG's VirtualKeyboardDismissAction.
	 *
	 * TextCommitOnDismiss keeps what the field did: the keyboard's Done button submits, and so does
	 * cancelling out of it (through the ordinary end-of-edit road). TextCommitOnAccept submits only
	 * on Done. TextChangeOnDismiss never submits from the keyboard at all -- the text changed, and
	 * that is the whole of what happened.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		EVirtualKeyboardDismissAction VirtualKeyboardDismissAction = EVirtualKeyboardDismissAction::TextCommitOnDismiss;
	/**
	 * Ctrl+Z / Ctrl+Y over a snapshot stack of (text, caret). Snapshots are taken before each edit
	 * that changes the text, so an undo lands where the edit started rather than at index 0.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bAllowUndoRedo = true;
	/** How many undo steps to keep. Older steps fall off the bottom. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (ClampMin = "1", EditCondition = "bAllowUndoRedo"))
		int32 UndoHistoryLength = 64;
	/**
	 * The edit menu -- cut, copy, paste, select all, undo, redo -- reached by right click, by a long
	 * press on touch, by the gamepad's Menu button, or by Shift+F10, which is Windows' own keyboard
	 * shortcut for it. UMG's editable text calls this AllowContextMenu and defaults it on.
	 * Only the entries that can act right now are built: no Paste with an empty clipboard, no Cut or
	 * Copy out of a password field.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
		bool bAllowContextMenu = true;
	/** How long a touch has to be held before it counts as a right click. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (ClampMin = "0.0", EditCondition = "bAllowContextMenu"))
		float ContextMenuLongPressTime = 0.5f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (EditCondition = "bAllowContextMenu"))
		FColor ContextMenuBackgroundColor = FColor(52, 57, 70, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (EditCondition = "bAllowContextMenu"))
		FColor ContextMenuTextColor = FColor(230, 233, 240, 255);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input", meta = (ClampMin = "1.0", EditCondition = "bAllowContextMenu"))
		float ContextMenuWidth = 150.0f;

	FDreamUIMulticastDelegateString OnValueChangedCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Input", DisplayName="OnValueChanged")
	FUITextInputValueChangedEvent OnValueChangedBP;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
	FDreamUIEventDelegate OnValueChanged = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::String);
	
	FDreamUIMulticastDelegateString OnSubmitCPP;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Input", DisplayName="OnSubmit")
	FUITextInputValueChangedEvent OnSubmitBP;
	/** Input submit by "Enter" key. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
	FDreamUIEventDelegate OnSubmit = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::String);
	
	FDreamUIMulticastDelegateBool OnInputActivateCPP;
	//DisplayName was a copy of OnSubmit's, so the Blueprint event list offered two "OnSubmit" entries
	//with different signatures -- one FString, one bool -- and no way to tell which was which.
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI-Input", DisplayName="OnInputActivate")
	FUITextInputActivateEvent OnInputActivateBP;
	/** Input activate or deactivate, means begin input or end input. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Input")
	FDreamUIEventDelegate OnInputActivate = FDreamUIEventDelegate(EDreamUIEventDelegateParameterType::Bool);

	void SetText(const FString& InText, bool InFireEvent);
public:
	/** The C++ halves of the events, the accessors every sibling behaviour already has. */
	FDreamUIMulticastDelegateString& GetOnValueChangedEvent() { return OnValueChangedCPP; }
	FDreamUIMulticastDelegateString& GetOnSubmitEvent() { return OnSubmitCPP; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		class UDreamText* GetTextComponent()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		const FString& GetText()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		EUITextInputType GetInputType()const { return InputType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		UDreamTextInputCustomValidation* GetCustomValidation()const { return CustomValidation; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		EUITextInputDisplayType GetDisplayType()const { return DisplayType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		const FString& GetPasswordChar()const { return PasswordChar; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetAllowMultiLine()const { return bAllowMultiLine; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		const TArray<FKey>& GetMultiLineSubmitFunctionKeys()const { return MultiLineSubmitFunctionKeys; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		UDreamWidget* GetPlaceHolderActor()const { return PlaceHolder.Get(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		float GetCaretBlinkRate()const { return CaretBlinkRate; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		float GetCaretWidth()const { return CaretWidth; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		FColor GetCaretColor()const { return CaretColor; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		FColor GetSelectionColor()const { return SelectionColor; }
	UFUNCTION()
		FVirtualKeyboardOptions GetVirtualKeyboardOptions()const { return VirtualKeyboardOptions; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		const TArray<FKey>& GetIgnoreKeys()const { return IgnoreKeys; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetAutoActivateInputWhenNavigateIn()const { return bAutoActivateInputWhenNavigateIn; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetReadOnly()const { return bReadOnly; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		int32 GetMaxLength()const { return MaxLength; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetSubmitWhenDeactivate()const { return bSubmitWhenDeactivate; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetSelectAllWhenActivateInput()const { return bSelectAllWhenActivateInput; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetAllowContextMenu()const { return bAllowContextMenu; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetRevertTextOnEscape()const { return bRevertTextOnEscape; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetClearKeyboardFocusOnCommit()const { return bClearKeyboardFocusOnCommit; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetSelectAllTextOnCommit()const { return bSelectAllTextOnCommit; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool GetIsCaretMovedWhenGainFocus()const { return bIsCaretMovedWhenGainFocus; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		ETextOverflowPolicy GetOverflowPolicy()const { return OverflowPolicy; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		TEnumAsByte<EVirtualKeyboardType::Type> GetKeyboardType()const { return KeyboardType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		EVirtualKeyboardTrigger GetVirtualKeyboardTrigger()const { return VirtualKeyboardTrigger; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		EVirtualKeyboardDismissAction GetVirtualKeyboardDismissAction()const { return VirtualKeyboardDismissAction; }

	/** Set text value and send callback event */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
	void SetText(const FString& InText);
	/** Set text value and NOT send callback event */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
	void SetTextWithoutNotify(const FString& InText);
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetInputType(EUITextInputType Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetCustomValidation(UDreamTextInputCustomValidation* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetDisplayType(EUITextInputDisplayType Value);
	/** Set password display char. Only allow one char in the value string */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetPasswordChar(const FString& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetAllowMultiLine(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetMultiLineSubmitFunctionKeys(const TArray<FKey>& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetPlaceHolder(UDreamWidget* Value);
	/**
	 * The text the field edits. EditAnywhere like its neighbours, so the designer and .dui always
	 * reached it by reflection while no caller could -- the UUIToggle transition-target hole again.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetTextVisual(UDreamText* Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetCaretBlinkRate(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetCaretWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetCaretColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetSelectionColor(FColor Value);
	UFUNCTION()
		void SetVirtualKeyboradOptions(FVirtualKeyboardOptions Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetIgnoreKeys(const TArray<FKey>& Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetAutoActivateInputWhenNavigateIn(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetReadOnly(bool Value);
	/** 0 means no limit. Shortening the limit truncates what the field already holds. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetMaxLength(int32 Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetSubmitWhenDeactivate(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetSelectAllWhenActivateInput(bool Value);
	/** Turning it off closes any menu that is open, the way UMG's AllowContextMenu behaves. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetAllowContextMenu(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetRevertTextOnEscape(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetClearKeyboardFocusOnCommit(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetSelectAllTextOnCommit(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetIsCaretMovedWhenGainFocus(bool Value);
	/** Re-pushes at once, so a policy written while the field sits idle is visible without an edit. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetOverflowPolicy(ETextOverflowPolicy Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetKeyboardType(TEnumAsByte<EVirtualKeyboardType::Type> Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetVirtualKeyboardTrigger(EVirtualKeyboardTrigger Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetVirtualKeyboardDismissAction(EVirtualKeyboardDismissAction Value);
	/**
	 * The correctly spelled name for SetVirtualKeyboradOptions, which stays as it is for callers.
	 * Not BlueprintCallable, and cannot be: FVirtualKeyboardOptions is a plain USTRUCT in Slate with
	 * no BlueprintType on it -- which is why the misspelled one was never a Blueprint node either.
	 */
	UFUNCTION()
		void SetVirtualKeyboardOptions(FVirtualKeyboardOptions Value);

	/**
	 * End the edit the way Escape means it: with bRevertTextOnEscape on, the text the field held when
	 * the edit began goes back in and that restored text is submitted once (UMG commits a revert); an
	 * edit that changed nothing submits nothing. Without it, this is the ordinary end of an edit and
	 * bSubmitWhenDeactivate still decides whether that moment reports a value.
	 *
	 * A road of its own rather than a flag on DeactivateInput, because the two callers mean different
	 * things: clicking away said nothing about the value, and Back said "throw this away".
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void CancelInput();

	/**
	 * A character the PLATFORM resolved, not one this component guessed from a key code.
	 *
	 * The key road below (AnyKeyPressed) maps FKey to TCHAR by hand, which is only ever right on a
	 * US QWERTY layout; this is the road for a host that owns real character events -- a project's
	 * UGameViewportClient::InputChar override, a Slate host's OnKeyChar, a test. Feeding one
	 * character through here makes the field stop synthesising printable characters from key codes
	 * for the rest of its life (function keys keep working), so the two roads never double-type.
	 *
	 * @return true if the character was accepted into the text.
	 */
	bool HandleCharacterInput(TCHAR InCharacter);
	/** Blueprint/host spelling of HandleCharacterInput: every character of the string in order. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool HandleCharacterInputString(const FString& InCharacters);
	/**
	 * A KEY the host delivers -- the key-shaped twin of HandleCharacterInput, for a host that has no
	 * player controller to bind keys through: a Slate host's OnKeyDown, an embedding tool, a test.
	 *
	 * Backspace, Delete, the arrows, Home/End, PageUp/PageDown, Enter and the Ctrl shortcuts all live
	 * on the key road, and that road is bound on a player controller's InputComponent and reads the
	 * held modifiers from its PlayerInput. A world with no player has neither, so a field could be
	 * typed into through HandleCharacterInput and then never deleted from, moved in or submitted.
	 * This hands the key to exactly the handling the bound road runs; nothing about that road changes.
	 *
	 * Only presses act, as on the bound road, which listens for press and repeat and never for a
	 * release; a key listed in IgnoreKeys is ignored here too, because the bound road never binds it.
	 * Escape is not a key this field handles on either road -- Back reaches an edit through
	 * UDreamUINavigationStack::HandleBack, which calls CancelInput.
	 *
	 * @return true if the field was being edited and took the key -- not a promise the key changed
	 *         anything: Home with the caret already at the start is taken and does nothing.
	 */
	bool HandleKeyInput(const FKey& InKey, bool bInPressed);
	/** HandleKeyInput with the modifiers the host says are held, which is how a chord -- Ctrl+A, Shift+Left, Ctrl+Enter -- arrives. */
	bool HandleKeyInput(const FKey& InKey, bool bInPressed, const FModifierKeysState& InModifierKeys);
	/**
	 * Route a platform character event to whichever field currently owns the keyboard, if any.
	 * This is the one line a project's UGameViewportClient::InputChar override needs.
	 * @return true if a field took the character.
	 */
	static bool RouteCharacterInputToActiveInput(TCHAR InCharacter);
	/** The field currently being edited, or null. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		static UUITextInput* GetActiveTextInput();

	/** Step back through the edit history. @return true if anything changed. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool Undo();
	/** Step forward again after an Undo. @return true if anything changed. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool Redo();
	/** Forget the edit history. Called whenever the text is replaced wholesale from code. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void ClearUndoHistory();
	/**
	 * Select the word around the caret: the run of letters, digits and underscores that the character
	 * after the caret belongs to (the one before it, at the end of the text), or the run of everything
	 * else -- spaces, punctuation -- when that character is one of those. A double click does this at
	 * the spot its second press lands on.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SelectWordAtCaret();
	/**
	 * Re-run the current input rules over the text already in the field, dropping what they now
	 * refuse. Called whenever the rules themselves change (input type, custom validator, max length).
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void RevalidateText();

	/**
	 * Open the edit menu next to the last place the pointer touched this field (the field's own
	 * centre when there was no pointer -- a gamepad or Shift+F10 opened it). Builds only the entries
	 * that can act; with none of them applicable nothing opens.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void ShowContextMenu();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void HideContextMenu();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool IsContextMenuOpen()const { return ContextMenuRoot.IsValid(); }

	/**
	 * Mark a run of the text as "still being composed" -- drawn with an underline, the way every
	 * text field distinguishes an IME's working text from text that has been committed.
	 *
	 * Set from the IME context's UpdateCompositionRange (which used to be an empty function body),
	 * and public because a host driving composition itself -- a mobile keyboard bridge, a test --
	 * needs the same road. Indices are offsets into the source string; a zero length clears the mark.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		void SetCompositionRange(int32 InBeginCharIndex, int32 InLength);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		int32 GetCompositionBeginIndex()const { return CompositionBeginCharIndex; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		int32 GetCompositionLength()const { return CompositionCharLength; }

	/**
	 * True while this field owns the keyboard. Back has to know: with a field being edited, cancelling
	 * the edit is what the player means, not closing the screen out from under them.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
	bool IsInputActive()const{ return bInputActive; }

	/**
	 * Whether anything is selected right now -- UMG's IsAnyTextSelected.
	 *
	 * Asked of the two caret indices rather than of the highlight bars: the bars are a VISUAL built
	 * from laid-out geometry and there are none at all before the field has been arranged, while the
	 * anchor and the caret are the selection itself.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
	bool IsAnyTextSelected()const{ return CaretPositionIndex != PressCaretPositionIndex; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
	void ActivateInput(UDreamPointerEventData* EventData = nullptr);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
	void DeactivateInput(bool InFireEvent = true);
	
	/**
	 * Verify input string value and insert the string value to text value at current caret position.
	 * @param Value string value to check and insert.
	 * @return true- if any char added.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Input")
		bool VerifyAndInsertStringAtCaretPosition(const FString& Value);
	/**
	 * Verify input char value and insert the char value to text value at current caret position.
	 * @param Value char value to check and insert.
	 * @return true- if verify success and added.
	 */
	bool VerifyAndInsertCharAtCaretPosition(TCHAR Value);
private:
	TWeakObjectPtr<AActor> InputComponentAgent;
	void BindKeys();
	void UnbindKeys();
	void AnyKeyPressed(FKey key);
	/**
	 * What a pressed key does to the edit, with the held keys supplied by whoever delivered it: the
	 * bound road reads them from the player's input state, HandleKeyInput from the host's modifiers.
	 * InIsKeyHeld answers for a key other than the one being pressed -- multiline submit asks whether
	 * one of MultiLineSubmitFunctionKeys is down alongside Enter.
	 */
	void ProcessKeyPressed(const FKey& InKey, bool bInCtrl, bool bInShift, bool bInAlt, TFunctionRef<bool(const FKey&)> InIsKeyHeld);
	/**
	 * Validate one character against a GIVEN text and caret, not against the member Text.
	 *
	 * Every type-level rule is positional -- "only one dot", "only one @", "minus only at the
	 * front" -- so asking them against the text already in the field is only correct while the
	 * field's text IS the text being built. It is not during SetText, which builds a brand new
	 * string character by character: a DecimalNumber field holding "3.5" refused the dot of
	 * SetText("1.2") because the OLD text already had one, and stored "12".
	 */
	bool IsValidChar(TCHAR c, const FString& InAgainstText, int32 InAgainstCaretIndex);
	/** The common case: validate against what the field currently holds, at the live caret. */
	bool IsValidChar(TCHAR c) { return IsValidChar(c, Text, CaretPositionIndex); }
	/**
	 * The open selection as a range into the SOURCE STRING, not as caret indices.
	 *
	 * A caret index is an element index (one per laid-out glyph) whenever the text is not rich text,
	 * and every surrogate pair, every emoji, makes that differ from the UTF-16 offset FString wants.
	 * Every edit road in this file maps through UDreamText for exactly that reason; DeleteSelection
	 * was the one that did not, and cut half a character out of any field holding an emoji.
	 * @return false when there is no selection, in which case the outputs are untouched.
	 */
	bool GetSelectionCharRange(int32& OutStartCharIndex, int32& OutCharCount);
	/**
	 * The text an insertion at the caret would land in: this field's text with the open selection
	 * already removed, plus where in THAT string the caret sits. The string the validator must see.
	 */
	FString GetTextWithoutSelection(int32& OutCaretCharIndex);
	/** True when another character still fits under MaxLength. Length counts what is already there. */
	bool HasRoomForMoreChars(int32 InCurrentLength, int32 InAddCount = 1)const
	{
		return MaxLength <= 0 || InCurrentLength + InAddCount <= MaxLength;
	}
	/**
	 * delete selected chars if there is any.
	 * @return true if anything deleted.
	 */
	bool DeleteSelection(bool InFireEvent = true);
	void InsertCharAtCaretPosition(TCHAR c);
	void InsertStringAtCaretPosition(const FString& value);
	FInputKeyBinding AnyKeyBinding;
	UPROPERTY(Transient) TObjectPtr<APlayerController> PlayerController = nullptr;
	bool CheckPlayerController();
	bool bInputActive = false;
	float NextCaretBlinkTime = 0;
	float ElapseTime = 0;
	void BackSpace();
	void ForwardSpace();
	/**
	 * .
	 * @param moveType 0-left, 1-right, 2-up, 3-down, 4-start, 5-end
	 */
	void MoveCaret(int32 moveType, bool withSelection);
	/** Left/right by whole words, the Ctrl+Arrow motion. @param InDirection -1 left, +1 right. */
	void MoveCaretByWord(int32 InDirection, bool withSelection);
	/** Up/down by a screenful of lines. Multiline only; a single line field has nowhere to page to. */
	void MoveCaretByPage(int32 InDirection, bool withSelection);
	/** How many lines one PageUp/PageDown covers in the field's current geometry. At least one. */
	int32 GetPageLineCount()const;
	void Copy();
	void Paste();
	void Cut();
public:
	/** Selects the whole text, the way UMG SelectAllText does. Public because SpinBox scrubbing and the context menu call it. */
	void SelectAll();
private:
	/** Fire the submit events once for this activation. Enter and, if asked, the end of the edit. */
	void Submit();

	//undo/redo: one snapshot is the whole text plus where the caret was when the edit started
	struct FTextSnapshot
	{
		FString Text;
		int32 CaretPositionIndex = 0;
	};
	TArray<FTextSnapshot> UndoStack;
	TArray<FTextSnapshot> RedoStack;
	/** Push the CURRENT state as an undo step. Call before changing the text, not after. */
	void PushUndoSnapshot();
	void ApplySnapshot(const FTextSnapshot& InSnapshot);
	/** Cut Text down to MaxLength. @return true if anything was removed. */
	bool EnforceMaxLength();

	/** Set while an Enter already submitted this activation, so ending the edit does not re-submit. */
	bool bSubmittedThisActivation = false;
	/**
	 * What the field held when the edit began -- the value a cancel puts back.
	 *
	 * Taken at activation rather than kept as an undo step, because the two answer different
	 * questions: undo walks BACK through edits, a cancel abandons the whole session at once, and a
	 * player who typed eleven characters expects one Escape rather than eleven undos.
	 */
	FString TextAtActivation;
	/**
	 * What Enter does after Submit(): end the edit, or keep it and (if asked) select the value ready
	 * to be typed over. One place, because the single-line road and the multiline submit chord are
	 * the same moment and two copies of this rule is how they come to disagree.
	 */
	void FinishCommitFromEnter();
	/**
	 * The ONE writer of the paragraph's overflow type, because two of them is how a multiline field
	 * came to stop wrapping once already.
	 *
	 * The line mode owns it whenever the field is being edited -- the caret's visible window and the
	 * wrap both read it -- and OverflowPolicy only gets a say in the gap between edits. Every place
	 * that used to set the overflow directly calls this instead.
	 */
	void PushOverflowToVisual();

	//the edit menu: built from the same primitives the caret and the selection mask are, because a
	//behaviour cannot reach the control layer and a text field must not need an authored template
	enum class EContextMenuAction : uint8 { Undo, Redo, Cut, Copy, Paste, SelectAll };
	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> ContextMenuRoot;
	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> ContextMenuBlocker;
	/** @return the entry's widget, or null when InbApplicable said this entry cannot act now. */
	UDreamWidget* AddContextMenuEntry(UDreamWidget* InMenuRoot, bool InbApplicable, const FText& InLabel, EContextMenuAction InAction, int32& InOutEntryCount);
	void ExecuteContextMenuAction(EContextMenuAction InAction);
	/** The entry's stable name, so a test can ask which entries a field offered rather than count them. */
	static const TCHAR* GetContextMenuActionName(EContextMenuAction InAction);
	/** One line of the field's own text plus breathing room, so the menu scales with the font. */
	float GetContextMenuEntryHeight()const;
	/** Inset between the menu's edge and its entries, and between an entry's edge and its label. */
	static constexpr float ContextMenuPadding = 4.0f;
	/** Where the menu opens: the last pointer position on this field, in world space. */
	FVector ContextMenuAnchorWorldPoint = FVector::ZeroVector;
	bool bHasContextMenuAnchor = false;
	//long press is touch's right click; the field times it itself because the pointer module reports
	//a press and a release, not a hold
	bool bPointerHeldForContextMenu = false;
	double PointerHeldStartTime = 0;
	/**
	 * Flipped the first time a real platform character arrives through HandleCharacterInput. From
	 * then on AnyKeyPressed stops guessing printable characters from key codes -- the host owns
	 * them -- and handles only the function keys, which are genuinely key-shaped.
	 *
	 * Class-wide, and it has to be: whether characters are delivered is a fact about the HOST, not
	 * about one field, and the very first keystroke would otherwise arrive twice. It does not, in
	 * that order, because Slate's character event is processed while messages are pumped and the
	 * bound InputComponent delegate fires later in the same frame, during the player tick -- so the
	 * flag is already set by the time the key road looks at it, for the first keystroke included.
	 */
	static bool bHostDeliversCharacterEvents;
	/** Whichever field currently owns the keyboard; the target of RouteCharacterInputToActiveInput. */
	static TWeakObjectPtr<UUITextInput> ActiveTextInput;
	/**
	 * Say once, the first time a field is edited with a real keyboard, that this project has not
	 * given DreamGUI a way to receive character events -- and therefore types wrongly on any layout
	 * that is not US QWERTY. See UDreamGameViewportClient.
	 */
	static void WarnOnceIfNoCharacterEventSource();

	FString GetReplaceText()const;

	void UpdateAfterTextChange(bool InFireEvent = true);

	void FireOnValueChangedEvent();
	void UpdateUITextComponent();
	void UpdatePlaceHolderComponent();
	void UpdateCaretPosition(bool InHideSelection = true);
	void UpdateCaretPosition(FVector2f InCaretPosition, bool InHideSelection = true);
	void UpdateSelection();
	void HideSelectionMask();
	void UpdateCompositionUnderline();
	void HideCompositionUnderline();
	//a Sprite for caret, can blink, can represent current caret location
	UPROPERTY(Transient)TWeakObjectPtr<UDreamWidget> CaretWidget;
	//selection mask
	UPROPERTY(Transient)TArray<TWeakObjectPtr<UDreamVisual>> SelectionMaskObjectArray;
	//range selection
	TArray<FDreamUITextSelectionProperty> SelectionPropertyArray;
	//the composition underline: one strip per visual run, built exactly like the selection mask
	UPROPERTY(Transient)TArray<TWeakObjectPtr<UDreamVisual>> CompositionUnderlineObjectArray;
	TArray<FDreamUITextSelectionProperty> CompositionPropertyArray;
	//source-string range the IME is composing; length 0 means nothing is
	int32 CompositionBeginCharIndex = 0;
	int32 CompositionCharLength = 0;
	//Caret position of full text. caret is on left side of char
	int CaretPositionIndex = 0;
	//caret position line index of full text
	int CaretPositionLineIndex = 0;
	//in single line mode, will clamp text if out of range. this is left start index of visible char
	//this property can only modify in UpdateUITextComponent function
	int VisibleCaretStartIndex = 0;
	//in multi line mode, will clamp text line if out of range. this is top start line index of visible char
	//this property can only modify in UpdateUITextComponent function
	int VisibleCaretStartLineIndex = 0;

	int PressCaretPositionIndex = 0, PressCaretPositionLineIndex = 0;
protected:
	virtual void OnEnable() override;
	virtual void OnInteractableChanged(bool Interactable) override;
	virtual void OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)override;

	virtual bool OnPointerEnter_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerExit_Implementation(UDreamPointerEventData* EventData)override;
	virtual bool OnPointerSelect_Implementation(UDreamBaseEventData* EventData) override;
	virtual bool OnPointerDeselect_Implementation(UDreamBaseEventData* EventData) override;
	virtual bool OnPointerClick_Implementation(UDreamPointerEventData* EventData) override;
	/**
	 * Selects the word under the double click -- under its SECOND press, which is the press the double
	 * click is: the event system delivers that press as the double click, in place of its down, as
	 * Slate does. The two presses only have to be within the drag threshold of each other, so they can
	 * fall on different words, and the word selected is the one the second press is on, as it is in
	 * SEditableText. The caret is put under that press first, the way a down puts it, and
	 * SelectWordAtCaret takes the word from there. Only while the field is being edited -- which a
	 * field that was not has been since the first click of the pair.
	 */
	virtual bool OnPointerDoubleClick_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerBeginDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerEndDrag_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerDown_Implementation(UDreamPointerEventData* EventData) override;
	virtual bool OnPointerUp_Implementation(UDreamPointerEventData* EventData) override;

private:
	friend class FVirtualKeyboardEntry;
	class FVirtualKeyboardEntry :public IVirtualKeyboardEntry
	{
	public:
		static TSharedRef<FVirtualKeyboardEntry> Create(UUITextInput* Input);

		virtual void SetTextFromVirtualKeyboard(const FText& InNewText, ETextEntryType TextEntryType) override;
		virtual void SetSelectionFromVirtualKeyboard(int InSelStart, int SelEnd)override;
		virtual bool GetSelection(int& OutSelStart, int& OutSelEnd) override;

		virtual FText GetText() const override;
		virtual FText GetHintText() const override;
		virtual EKeyboardType GetVirtualKeyboardType() const override;
		virtual FVirtualKeyboardOptions GetVirtualKeyboardOptions() const override;
		virtual bool IsMultilineEntry() const override;

	private:
		FVirtualKeyboardEntry(UUITextInput* InInput);
		UUITextInput* InputComp;
	};

private:
	friend class FTextInputMethodContext;
	class FTextInputMethodContext:public ITextInputMethodContext
	{
	public:
		static TSharedRef<FTextInputMethodContext> Create(UUITextInput* Input);
		void Dispose();

		virtual bool IsComposing() override
		{
			return bIsComposing;
		}
	
		virtual bool IsReadOnly() override;
		virtual uint32 GetTextLength() override;
		virtual void GetSelectionRange(uint32& BeginIndex, uint32& Length, ECaretPosition& CaretPosition) override;
		virtual void SetSelectionRange(const uint32 BeginIndex, const uint32 Length, const ECaretPosition CaretPosition) override;
		virtual void GetTextInRange(const uint32 BeginIndex, const uint32 Length, FString& OutString) override;
		virtual void SetTextInRange(const uint32 BeginIndex, const uint32 Length, const FString& InString) override;
		virtual int32 GetCharacterIndexFromPoint(const FVector2D& Point) override;
		virtual bool GetTextBounds(const uint32 BeginIndex, const uint32 Length, FVector2D& Position, FVector2D& Size) override;
		virtual void GetScreenBounds(FVector2D& Position, FVector2D& Size) override;
		virtual TSharedPtr<FGenericWindow> GetWindow() override;
		virtual void BeginComposition() override;
		virtual void UpdateCompositionRange(const int32 InBeginIndex, const uint32 InLength) override;
		virtual void EndComposition() override;

	private:
		FTextInputMethodContext(UUITextInput* InInput);
		/**
		 * A point on the field, through the canvas that draws it, into absolute desktop pixels --
		 * the space ITextInputMethodContext speaks. The canvas's own view-projection is the right
		 * matrix for every render mode, because it is the matrix the UI was drawn with.
		 */
		bool ProjectUIPointToScreen(const FVector& InWorldPosition, FVector2D& OutScreenPosition);
		/** The reverse: an absolute desktop point back onto the plane the text lives in. */
		bool DeprojectScreenPointToUI(const FVector2D& InScreenPosition, FVector& OutWorldPosition);
		/** Caret index for a source-string offset, which is what the IME counts in. */
		int32 CaretIndexFromCharIndex(int32 InCharIndex)const;
		UUITextInput* InputComp;
		FString OriginString;
		bool bIsComposing = false;
		TSharedPtr<SBox> CachedWindow;
	};
private:
	TSharedPtr<FVirtualKeyboardEntry> VirtualKeyboardEntry;
	TSharedPtr<FTextInputMethodContext> TextInputMethodContext;
	TSharedPtr<ITextInputMethodChangeNotifier> TextInputMethodChangeNotifier;
	
};
