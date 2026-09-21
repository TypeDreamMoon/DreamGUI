// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamTextInput.h"
#include "DreamEditableText.generated.h"

/**
 * A text field with no box: UMG's EditableText, where Native.TextInput is its EditableTextBox.
 *
 * UMG ships four classes for one field -- EditableText, EditableTextBox, MultiLineEditableText,
 * MultiLineEditableTextBox -- and the two axes it splits them on are "does it draw a box" and "does
 * it take more than one line". This library already had the second axis as a property
 * (UDreamTextInput::bMultiLine), so the only thing genuinely missing was the FIRST: a field that
 * draws nothing behind the text.
 *
 * So this is a UDreamTextInput whose face is not drawn, and nothing else. Every property, event,
 * validator and key rule stays where it was, which is the point: Native.TextInput does not change
 * meaning, and an author who wants the boxed one keeps writing exactly what they wrote before.
 *
 * WHAT "NO BOX" MEANS, precisely, because half-answers here are how a control ends up with an
 * invisible box that still tints on hover: the face's brush is cleared, and ALL FIVE of the
 * behaviour's state colours are pushed transparent -- so there is no resting fill, no hover tint and
 * no focus tint. The caret and the selection highlight are what show the field is live, which is
 * what UMG's EditableText does too. The style's padding, font, text colour and caret colour all
 * still apply: a borderless field is still a styled one.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Editable Text")
class DREAMGUI_API UDreamEditableText : public UDreamTextInput
{
	GENERATED_BODY()

public:
	virtual void ApplyStyle() override;
};

/**
 * The same field over several lines: UMG's MultiLineEditableText.
 *
 * A subclass rather than a palette entry that ticks bMultiLine, unlike the boxed pair -- and the
 * difference is worth stating. `Native.TextInput` plus `bMultiLine = true` IS the multi-line boxed
 * field, and the palette offers it as a second row of the same class (see the registry). A CLASS
 * exists here instead because UMG has one, because `.dui` gains a tag that reads the way the UMG
 * documentation reads, and because a Blueprint that wants to subclass "the multi-line borderless
 * field" has something to derive from. It sets exactly one property.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Multi Line Editable Text")
class DREAMGUI_API UDreamMultiLineEditableText : public UDreamEditableText
{
	GENERATED_BODY()

public:
	UDreamMultiLineEditableText();
};
