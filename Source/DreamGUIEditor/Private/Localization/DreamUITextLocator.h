// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamText;
class UDreamWidget;
class UWorld;

/**
 * Which DreamText is at a point of the screen, in the designer's viewport or a game's (PIE): DreamGUI as DreamLoc's
 * IDreamLocTextLocator, for DreamLoc's in-context pick -- Alt+click a text and the translation window opens at its
 * entry. DreamGUI draws its texts itself, so Slate's text widgets cannot say; the pick ray of the viewport can.
 */
namespace DreamUITextLocator
{
	/**
	 * The DreamText of the front-most widget among InWidgets whose rect the ray crosses and that shows one (a DreamText is a
	 * widget's visual); null for none. Any text the ray crosses, even one drawn under an image: a text is what Alt+click
	 * is looking for.
	 */
	DREAMGUIEDITOR_API UDreamText* FindTextAlongRay(const UWorld* InWorld, TConstArrayView<UDreamWidget*> InWidgets, const FVector& InLineStart, const FVector& InLineEnd);

	/** Every widget under InRoot, InRoot too, into nested widget instances: their texts are shown as well. */
	DREAMGUIEDITOR_API void CollectAll(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets);

	/** A DreamText's namespace and key, as DreamLoc's files have them; false (and OutWhyNot) when it has none. */
	DREAMGUIEDITOR_API bool Describe(const UDreamText* InText, FString& OutNamespace, FString& OutKey, FString& OutWhyNot);

	/** The four corners of a widget's rect, in the world. */
	DREAMGUIEDITOR_API void GetWorldCorners(const UDreamWidget* InWidget, FVector OutCorners[4]);

	/** DreamGUI as DreamLoc's text locator, when the project has DreamLoc. */
	void RegisterWithDreamLoc();
	void UnregisterFromDreamLoc();
}
