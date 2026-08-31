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

	struct FEntry
	{
		FName Scope;
		FName Name;
		UClass* (*ClassGetter)() = nullptr;
	};

	/** Called by the DECLARE macro's static object; not for direct use. */
	static void Register(const FEntry& InEntry);

private:
	static TArray<FEntry>& Entries();
};

/** The macro's static-object carrier; a translation unit gets one per declaration. */
struct FDreamUIWidgetRegistration
{
	FDreamUIWidgetRegistration(const TCHAR* InScope, const TCHAR* InName, UClass* (*InClassGetter)())
	{
		FDreamUIWidgetRegistry::Register({ FName(InScope), FName(InName), InClassGetter });
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
	static const FDreamUIWidgetRegistration PREPROCESSOR_JOIN(DreamUIWidgetRegistration_, Class)( \
		TEXT(Scope), TEXT(Name), []() -> UClass* { return Class::StaticClass(); });
