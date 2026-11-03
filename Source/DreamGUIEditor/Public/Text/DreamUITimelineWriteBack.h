// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamWidget;
class UDreamWidgetAnimation;
class UDreamWidgetBlueprint;

/**
 * An edit made in the animation editor to a `timeline` block's animation, written back into the block.
 *
 * WHAT IT CAN SAY is what the language can: a key's time and value, the named ease from one key to the next, the event
 * keys, a track added or removed (a line written or taken out), the duration. The builder bakes an eased segment into
 * keys sampled at the display rate, so the sequence holds no ease names; each run of keys between two anchors is read
 * back as the one ease whose samples it is, and a run that is no ease's is linear keys. A key Sequencer added with its
 * default automatic tangents becomes linear, in the sequence as well, so what the editor shows is what the file will
 * build. A key with tangents of its own, or stepped, has no spelling: that edit is refused, with the way out (`timeline
 * X external`, which hands the animation to Sequencer for good).
 *
 * The block is rewritten line by line: a track whose keys did not change keeps its line byte for byte, a changed one is
 * rewritten in place, a new one is written before the closing brace, a removed one is taken out; comments and the
 * `loop` line stay as they are.
 */
struct DREAMGUIEDITOR_API FDreamUITimelineWriteBack
{
	/**
	 * InText with the block of InAnimation's timeline rewritten to say what the sequence holds now. InHost is the widget
	 * the animation is played on (the root), against which new tracks' node paths are spelled. False with OutError when
	 * the file has no such block, the block is not written across lines, or the sequence holds something no line can say.
	 */
	static bool RewriteText(const FString& InText, UDreamWidgetAnimation* InAnimation, const UDreamWidget* InHost,
		FString& OutText, FString& OutError);

	/** The animation editor's call after an edit: the block rewritten through the Blueprint's write-back, in one transaction. */
	static bool WriteAnimation(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetAnimation* InAnimation, FText& OutReason);

	/** Whether the animation editor may edit this animation: one the language owns, of a Blueprint whose `.dui` it can write. */
	static bool CanWriteBack(const UDreamWidgetAnimation* InAnimation);
};
