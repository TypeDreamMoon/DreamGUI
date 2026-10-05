// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "UObject/WeakObjectPtr.h"

class UClass;
class UDreamWidget;

/**
 * Which DreamUI components make sense on which widget: what the designer's Add Component offers, and what a paste or
 * a component asset dropped on the panel is allowed to put there.
 *
 * The picker used to list every component class there is -- fifty-odd -- on every widget, and nothing refused any of
 * them: a sprite sequence player on a text, a mesh modifier on a widget with no visual, a second canvas that nothing
 * ever reads. Each of those is inert or silently ignored at runtime, so the mistake surfaced as "it does nothing".
 * The requirements were already there, scattered through the components' own code (a Cast of the visual that bails
 * out, a GetComponent that only ever reads the first); this is where they are written down once, for the editor.
 *
 * A rule is registered for a class and holds for every subclass of it, Blueprint ones included, and a class must pass
 * the rules of all its ancestors. A project registers rules for its own components the same way, from its editor
 * module's StartupModule.
 */
class DREAMGUIEDITOR_API FDreamUIComponentSupport
{
public:
	/** Whether a component of the rule's class can go on InWidget; false fills OutReason with what is missing. */
	using FRule = TFunction<bool(const UDreamWidget* InWidget, FText& OutReason)>;

	static FDreamUIComponentSupport& Get();

	/** A rule for InComponentClass and every subclass of it. */
	void AddRule(const UClass* InComponentClass, FRule InRule);
	/** Only on a widget whose visual is InVisualClass or a subclass of it; InVisualLabel names it in the reason. */
	void RequireVisual(const UClass* InComponentClass, const UClass* InVisualClass, const FText& InVisualLabel);
	/** Only on a widget that already carries a component of InSiblingClass. */
	void RequireComponent(const UClass* InComponentClass, const UClass* InSiblingClass);
	/**
	 * At most one per widget: not offered where the widget already has a component derived from InFamilyClass --
	 * InComponentClass itself when null. The family form is for classes that stand in for each other: a widget is
	 * one kind of selectable, not a button and a slider at once.
	 */
	void OnePerWidget(const UClass* InComponentClass, const UClass* InFamilyClass = nullptr);
	/** Drop every rule registered for InComponentClass itself; its ancestors' rules still apply to it. */
	void RemoveRules(const UClass* InComponentClass);

	/** Whether InComponentClass can be added to InWidget. True for a null widget; OutReason is the first refusal. */
	bool IsSupported(const UClass* InComponentClass, const UDreamWidget* InWidget, FText* OutReason = nullptr) const;
	/**
	 * The same question for a class known only by what it derives from -- an unloaded Blueprint in the class picker.
	 * InIsChildOf answers whether the class is, or derives from, the class it is given.
	 */
	bool IsSupported(TFunctionRef<bool(const UClass*)> InIsChildOf, const UDreamWidget* InWidget, FText* OutReason = nullptr) const;

private:
	FDreamUIComponentSupport();
	void RegisterDefaults();

	struct FEntry
	{
		TWeakObjectPtr<const UClass> Class;
		FRule Rule;
	};
	TArray<FEntry> Entries;
};
