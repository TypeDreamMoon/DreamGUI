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
 * keys sampled at the display rate (DreamUITimelineSampling), so the sequence holds no ease names; it is read as an
 * edit of the line it was built from, which says where every sample of its eases stands: those are samples, the rest
 * are the author's keys, and a key dragged off the end of an eased run keeps the ease (see ReadKeysAgainstLine). A key
 * Sequencer added with its default automatic tangents becomes linear, in the sequence as well, so what the editor shows
 * is what the file will build. A key with tangents of its own, or stepped, has no spelling: that edit is refused, with
 * the way out (`timeline X external`, which hands the animation to Sequencer for good).
 *
 * The block is rewritten line by line, and a line token by token: a track whose keys did not change keeps its line byte
 * for byte, and in a changed one only the time, value or ease that changed is replaced -- a time spelled to half a
 * display frame, with as many decimals as the one it replaces -- while its padding, its other keys' spellings and
 * anything after them stay. A new track is written before the closing brace, a removed one is taken out; comments and
 * the `loop` line stay as they are. WriteAnimation then re-keys the rewritten tracks to what their lines build, so the
 * sequence and the file agree until the next compile makes it so anyway.
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

	/**
	 * The animation editor's call after an edit: the block rewritten through the Blueprint's write-back, and the rewritten
	 * tracks re-keyed to what the block now builds, in one transaction (the edit's own, when one is still open).
	 */
	static bool WriteAnimation(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetAnimation* InAnimation, FText& OutReason);

	/** Whether the animation editor may edit this animation: one the language owns, of a Blueprint whose `.dui` it can write. */
	static bool CanWriteBack(const UDreamWidgetAnimation* InAnimation);
};
