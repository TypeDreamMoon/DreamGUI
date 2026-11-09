// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FProperty;

/**
 * The meta tags a property carries to say where DreamGUI's details panels put it and how it is edited. They all start
 * with DreamGUI_ (other Dream plugins add tags of their own; a meta key cannot hold a dot), and none of them is needed:
 * a property without them is shown under its own Category, the way the engine shows it. Docs/guides/property-meta.md
 * has the list with examples.
 *
 *   DreamGUI_Section="Layer"         the top-level section it goes in (ESection)
 *   DreamGUI_Group="Glass/Refraction" the group inside its section, or inside its own category with no section
 *   DreamGUI_Order=10                its place in that group or section, smaller first; untagged ones keep declaration order
 *   DreamGUI_Advanced                under the advanced expander of where it goes
 *   DreamGUI_Hidden                  not shown at all: internal state, or a value a custom row of the panel edits
 *   DreamGUI_Editor="AngleWheel"     which of DreamGUI's controls edits it, when its type alone does not say
 *   DreamGUI_Handle="Gradient"       which handles the viewport shows for it on the selected widget: a kind the viewport
 *                                    handle framework registers, with arguments after a colon ("Kind:Args")
 *   DreamGUI_LayerToggle             the switch that turns the whole effect it belongs to on and off: the eye in the
 *                                    hierarchy and in the Layer section
 *
 * Showing and hiding by another property's value stays the engine's EditCondition / EditConditionHides.
 */
namespace DreamPropertyMeta
{
	extern DREAMGUIEDITOR_API const FName Section;
	extern DREAMGUIEDITOR_API const FName Group;
	extern DREAMGUIEDITOR_API const FName Order;
	extern DREAMGUIEDITOR_API const FName Advanced;
	extern DREAMGUIEDITOR_API const FName Hidden;
	extern DREAMGUIEDITOR_API const FName Editor;
	extern DREAMGUIEDITOR_API const FName Handle;
	extern DREAMGUIEDITOR_API const FName LayerToggle;

	/** The top-level sections of a widget's details, top to bottom. */
	enum class ESection : uint8
	{
		None,
		Layer,
		Layout,
		Visual,
		Appearance,
		Effect,
		Behavior,
		Advanced,
	};

	/** What a section is drawn as: a details category of this name, title and sort order. */
	struct FSectionInfo
	{
		FName CategoryName;
		FText DisplayName;
		int32 SortOrder = 0;
		bool bInitiallyCollapsed = false;
	};

	DREAMGUIEDITOR_API ESection ParseSection(const FString& InText);
	DREAMGUIEDITOR_API const FSectionInfo& GetSectionInfo(ESection InSection);

	DREAMGUIEDITOR_API ESection GetSection(const FProperty* InProperty);
	/** The group path, its parts split on "/"; empty with no group. */
	DREAMGUIEDITOR_API TArray<FString> GetGroup(const FProperty* InProperty);
	/** DreamGUI_Order, or unset. */
	DREAMGUIEDITOR_API TOptional<int32> GetOrder(const FProperty* InProperty);
	DREAMGUIEDITOR_API bool IsAdvanced(const FProperty* InProperty);
	DREAMGUIEDITOR_API bool IsHidden(const FProperty* InProperty);
	DREAMGUIEDITOR_API bool IsLayerToggle(const FProperty* InProperty);
	/** DreamGUI_Editor, or None. */
	DREAMGUIEDITOR_API FName GetEditor(const FProperty* InProperty);
	/** DreamGUI_Editor's value for a property it names InEditor for, with the option after a colon: "AngleWheel:TopCW". */
	DREAMGUIEDITOR_API bool UsesEditor(const FProperty* InProperty, FName InEditor, FString* OutOption = nullptr);
	/** DreamGUI_Handle's kind, or None; what follows a colon ("Gradient:Border") is its arguments, handed to the kind as they are. */
	DREAMGUIEDITOR_API FName GetHandle(const FProperty* InProperty, FString* OutArgs = nullptr);
	/** Whether the property says where it goes: a section, a group, an order or the advanced expander. */
	DREAMGUIEDITOR_API bool HasPlacement(const FProperty* InProperty);

	/** The first DreamGUI_LayerToggle of InStruct, looking into the plain struct members one level down, as a path ("Style.bEnabled"). */
	DREAMGUIEDITOR_API FString FindLayerTogglePath(const UStruct* InStruct);
}
