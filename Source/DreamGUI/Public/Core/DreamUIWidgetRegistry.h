// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UClass;

/**
 * The names a .dui node tag can wear beyond the built-in visuals: `Native.Toggle`, `Game.HealthBar`.
 *
 * A scoped tag is a (scope, name) pair looked up here, and registration is a one-line declaration
 * next to the class it names:
 *
 *     DECLARE_DREAM_GUI_WIDGET("Native", "Toggle", UDreamToggle)
 *
 * The registry exists so the language does not know the library: the builder resolves whatever was
 * declared, a project plugin declares its own scope, and adding a control adds its tag with no
 * table anywhere to forget. Registration happens during static initialization, so the class is
 * stored as a getter rather than a UClass* -- StaticClass() is not callable that early, and the
 * lookup is when the answer is actually needed.
 */
class DREAMGUI_API FDreamUIWidgetRegistry
{
public:
	/** The registered class, or null. Case follows FName semantics, like every other .dui name. */
	static UClass* Resolve(FName InScope, FName InName);

	/** Every registered name in InScope, for a diagnostic to suggest from. */
	static TArray<FName> NamesInScope(FName InScope);

	/**
	 * The two shapes of tag a `.dui` node can wear, kept in one registry because they answer the
	 * same question and because everything that enumerates tags -- the symbol export the VSCode
	 * extension reads, the "unknown tag" diagnostic, the write-back -- wants both or neither.
	 */
	enum class EKind : uint8
	{
		/** `Native.Toggle`: a scoped tag naming a UDreamUserWidget subclass. */
		ScopedWidget,
		/** `Image`: a bare tag naming a UDreamVisual subclass to put on an ordinary widget. */
		VisualTag,
	};

	struct FEntry
	{
		FName Scope;
		FName Name;
		UClass* (*ClassGetter)() = nullptr;
		EKind Kind = EKind::ScopedWidget;
	};

	/**
	 * The class a BARE tag names, and whether the tag is one the language knows at all.
	 *
	 * The distinction is the whole reason for the out-parameter: `Widget` is a known tag whose
	 * answer is NO VISUAL, and an unknown tag is a diagnostic. Returning null for both would make
	 * the plainest node in the language read as a typo.
	 */
	static UClass* ResolveVisual(FName InTag, bool& bOutIsKnownTag);

	/**
	 * Every bare tag, `Widget` first and the rest sorted by name.
	 *
	 * Sorted because registration order is static-initialization order, which is a link-order
	 * detail: the property-name hint below picks the FIRST tag whose visual declares a property,
	 * and a diagnostic that suggests `Sprite` on one build and `Texture` on the next is worse than
	 * either suggestion.
	 */
	static void GetVisualEntries(TArray<TPair<FName, UClass*>>& OutEntries);

	/**
	 * How a `.dui` would spell InClass -- "Image", "Native.Toggle", or empty when nothing declares
	 * it. The inverse of the two resolvers, for whoever has a class and needs the source text.
	 */
	static FString FindTagForClass(const UClass* InClass);

	/**
	 * Every registration, scope and all.
	 *
	 * NamesInScope answers a diagnostic that already knows which scope it is suggesting within;
	 * this is for the callers that have to enumerate the whole table without knowing the scopes --
	 * the symbol export the VSCode extension reads, above all, which had no way to learn that
	 * `Native.Button` exists and so offered completion for the primitives alone.
	 */
	static void GetAllEntries(TArray<FEntry>& OutEntries);

	/** Called by the DECLARE macro's static object; not for direct use. */
	static void Register(const FEntry& InEntry);

private:
	static TArray<FEntry>& Entries();
};

/** The macro's static-object carrier; a translation unit gets one per declaration. */
struct FDreamUIWidgetRegistration
{
	FDreamUIWidgetRegistration(const TCHAR* InScope, const TCHAR* InName, UClass* (*InClassGetter)(),
		FDreamUIWidgetRegistry::EKind InKind = FDreamUIWidgetRegistry::EKind::ScopedWidget)
	{
		FDreamUIWidgetRegistry::Register({ FName(InScope), FName(InName), InClassGetter, InKind });
	}
};

/**
 * Declare a widget class under a scoped .dui tag. File scope, after the class:
 *
 *     DECLARE_DREAM_GUI_WIDGET("Native", "Toggle", UDreamToggle)
 */
/* Keyed by the class, not __LINE__: under a unity build two of these in one blob can land on the
 * same line number of their own files, and the "unique" names collide. The class is the one token
 * guaranteed distinct per declaration. */
#define DECLARE_DREAM_GUI_WIDGET(Scope, Name, Class) \
	static const FDreamUIWidgetRegistration UE_JOIN(DreamUIWidgetRegistration_, Class)( \
		TEXT(Scope), TEXT(Name), []() -> UClass* { return Class::StaticClass(); });

/**
 * Declare a visual class under a BARE `.dui` tag. File scope, after the class:
 *
 *     DECLARE_DREAM_GUI_VISUAL("Image", UDreamImage)
 *
 * The visual half of the same idea, and it replaced a hand-maintained table inside the builder.
 * What that table cost: every visual outside its ten entries -- the polygons, the rings, the 2D
 * lines, the static mesh, the UMG host, the post-process elements -- was placeable from the
 * palette and unspellable in `.dui`, and nothing anywhere said so. The language knew nine visuals
 * because somebody had typed nine lines.
 *
 * HEADLESS HAZARD, inherited from that table and still true: a visual is CONSTRUCTED to build a
 * node, and UDreamWidget::CreateNewVisual calls Call_OnRegister unconditionally. UDreamRectBlock's
 * asserts on data it loads from UDreamGUISettings. Whatever compiles a `.dui` without an editor has
 * to keep those settings valid; declaring a tag here is declaring that the class can be built.
 */
#define DECLARE_DREAM_GUI_VISUAL(Tag, Class) \
	static const FDreamUIWidgetRegistration UE_JOIN(DreamUIVisualRegistration_, Class)( \
		TEXT(""), TEXT(Tag), []() -> UClass* { return Class::StaticClass(); }, \
		FDreamUIWidgetRegistry::EKind::VisualTag);
