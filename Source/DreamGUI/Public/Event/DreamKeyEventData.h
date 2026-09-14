// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Event/DreamBaseEventData.h"
#include "DreamKeyEventData.generated.h"

/** Which of the four key channels an event data is carrying. See IDreamKeyInterface. */
UENUM(BlueprintType)
enum class EDreamUIKeyEventType : uint8
{
	KeyDown,
	KeyUp,
	KeyChar,
	AnalogValueChanged,
};

/**
 * One key, character or analog sample, on its way to the focused widget.
 *
 * The counterpart of UDreamPointerEventData for the keyboard and the stick. It exists because the
 * pointer event data answers "where", and a key has no where -- it has a key, a chord and a player.
 *
 * Handled is the bubble switch, spelled the other way round from the pointer interfaces because that
 * is what a key means: a pointer event asks "may this keep bubbling", a key event asks "did anybody
 * take it", and a taken key must not also be read as navigation or fire a bound action. Whoever
 * handles it sets Handled and the walk stops there, which is Slate's rule and UMG's.
 */
UCLASS(BlueprintType, ClassGroup = DreamGUI)
class DREAMGUI_API UDreamKeyEventData : public UDreamBaseEventData
{
	GENERATED_BODY()
public:
	/**
	 * Which channel this is. A handler bound to one channel should check before reading Character.
	 *
	 * NOT called EventType: the base class already has one of those, carrying
	 * EDreamUIPointerEventType, and that enum has no room for a key -- its values are Click, Enter,
	 * Drag, Scroll and the rest of the pointer's vocabulary. Shadowing it is a UHT error, and it
	 * would be the wrong shape even if it were allowed, so the key channel gets a name of its own.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	EDreamUIKeyEventType KeyEventType = EDreamUIKeyEventType::KeyDown;

	/** The local player this key belongs to -- the same index as their event system and screen. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	int32 UserIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	FKey Key;

	/** True for KeyDown, false for KeyUp; meaningless on the other two channels. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	bool bIsPressed = false;

	/** Repeat of a held key, rather than the first press. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	bool bIsRepeat = false;

	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	bool bShiftDown = false;

	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	bool bCtrlDown = false;

	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	bool bAltDown = false;

	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	bool bCmdDown = false;

	/** The analog sample, for AnalogValueChanged. Zero elsewhere. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	float AnalogValue = 0.0f;

	/**
	 * The character produced, for KeyChar. A string rather than a TCHAR because one keystroke can
	 * produce more than one code unit, and because Blueprint has no character type.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	FString Character;

	/** Set by whoever consumed this key. Once true the walk up the hierarchy stops. */
	UPROPERTY(BlueprintReadWrite, Category = "DreamGUI")
	bool bHandled = false;

	/** The widget the key was offered to first -- the focused one. */
	UPROPERTY(BlueprintReadOnly, Category = "DreamGUI")
	TObjectPtr<UDreamWidget> FocusedWidget = nullptr;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetHandled() { bHandled = true; }
};
