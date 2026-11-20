// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UClass;
class UDreamWidget;

/**
 * What a widget is made of besides itself.
 *
 * A node is composed: what it draws is its visual, how it arranges its children is its layout container, how it sizes
 * itself inside its parent is its layout self, and what it does is its behaviours. Each of those is a sub-object of the
 * widget, and each is a "part" here. A part is found by class, so the kind follows from the class: asking for a
 * UDreamText finds the visual, asking for a UUIButton finds the first behaviour that is one.
 */
enum class EDreamWidgetPartKind : uint8
{
	/** A class that is none of the kinds below, such as a common base of visuals and layouts. */
	None,
	Visual,
	LayoutContainer,
	LayoutSelf,
	Behaviour,
};

struct DREAMGUI_API FDreamWidgetParts
{
	/** The kind of part an instance of InClass is, or None when InClass is not under one of the four part bases. */
	static EDreamWidgetPartKind KindOf(const UClass* InClass);

	/**
	 * InWidget's part that is an InClass, or nullptr when it has none.
	 *
	 * Visual, layout container and layout self are one each, so the answer is that one or nothing; behaviours are a list,
	 * and the answer is the first that is an InClass. A class of no single kind (a common base) is looked for in the
	 * visual, then the layout container, then the layout self.
	 */
	static UObject* Find(const UDreamWidget* InWidget, const UClass* InClass);

	/** Every part InWidget has, in that order: visual, layout container, layout self, then the behaviours in theirs. */
	static void GetAll(const UDreamWidget* InWidget, TArray<UObject*>& OutParts);

	/** "Visual", "Layout", "Layout Self", "Behaviour": how a part's kind is named to an author. */
	static FText DescribeKind(EDreamWidgetPartKind InKind);
};
