// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

class UDreamWidget;
DECLARE_MULTICAST_DELEGATE(FOnDreamUIControlRegistryChanged);

enum class EDreamUIControlCreationKind : uint8
{
	/** A hierarchy class asset. The palette places an INSTANCE of it, so fixing the class fixes every use. */
	WidgetClass,
	Native,
	/**
	 * A control class written in C++ -- UDreamUIControl and its family, the `Native.X` tags.
	 *
	 * Distinct from WidgetClass in the RESOLUTION step alone: that one names a Blueprint by asset
	 * path and asks the asset registry for its generated class, and this one already has the class.
	 * Everything downstream is shared, because a code-built control is a UDreamUserWidget subclass
	 * exactly like a compiled Blueprint is, and placing either means instancing it.
	 *
	 * Distinct from Native, which composes a widget out of a visual plus a behaviour plus a layout.
	 * A control is not a recipe for parts; it BUILDS its parts, in RealizeBuiltIn, and the palette
	 * has nothing to say about them.
	 */
	ControlClass,
};

/**
 * The Palette's categories, the values FDreamUIControlDescriptor::Category takes for what DreamGUI registers.
 *
 * Grouped the way UMG's palette groups the same widgets, so an author coming from UMG finds a slider under Common and a
 * box under Panels. There used to be one "Controls" category holding thirty-odd rows of every kind -- inputs, lists,
 * scroll boxes, slots, behaviours -- in registration order, beside a dozen small categories that differed by a word.
 *
 * An extension may register under any category of its own; the Palette shows those after these, in the order they were
 * first registered (FDreamUIControlRegistry::GetCategoriesInDisplayOrder).
 */
namespace DreamUIPaletteCategory
{
	inline const TCHAR* const Panels = TEXT("Panels");
	inline const TCHAR* const Common = TEXT("Common");
	inline const TCHAR* const Input = TEXT("Input");
	inline const TCHAR* const Lists = TEXT("Lists");
	inline const TCHAR* const Scrolling = TEXT("Scrolling");
	inline const TCHAR* const Containers = TEXT("Containers");
	inline const TCHAR* const Primitive = TEXT("Primitive");
	inline const TCHAR* const Shapes = TEXT("Shapes");
	inline const TCHAR* const Effects = TEXT("Effects");
	inline const TCHAR* const Components = TEXT("Components");
	inline const TCHAR* const Modifiers = TEXT("Modifiers");
	inline const TCHAR* const Advanced = TEXT("Advanced");
	/**
	 * What the control library replaced -- the LGUI-era behaviours and the Blueprint presets -- kept so that a widget
	 * built on one stays explicable. Always last, and collapsed until an author opens it.
	 */
	inline const TCHAR* const Legacy = TEXT("Legacy");
}

/** A Palette entry with an explicit creation recipe and validation contract. */
struct DREAMGUIEDITOR_API FDreamUIControlDescriptor
{
	FName Name;
	FText DisplayName;
	FName Category;
	EDreamUIControlCreationKind CreationKind = EDreamUIControlCreationKind::Native;
	/** Package path of the UDreamWidgetBlueprint backing this control, for the WidgetClass kind. */
	FString WidgetClassPath;
	/** The UDreamUserWidget subclass to instance, for the ControlClass kind. */
	TWeakObjectPtr<UClass> ControlClass;
	TWeakObjectPtr<UClass> VisualClass;
	TWeakObjectPtr<UClass> LayoutContainerClass;
	TWeakObjectPtr<UClass> LayoutSelfClass;
	TWeakObjectPtr<UClass> BehaviourClass;
	TWeakObjectPtr<UClass> MeshModifierClass;
	FSlateIcon Icon;
	TFunction<void(UDreamWidget*)> NativeConfigure;
};

/** Central registration point used by the Palette and available to project/editor extensions. */
class DREAMGUIEDITOR_API FDreamUIControlRegistry
{
public:
	static FDreamUIControlRegistry& Get();
	/**
	 * Add a descriptor. The return value is meaningful and worth checking: false means the entry was
	 * refused -- Name is None, or another descriptor already holds it -- and will never appear in the
	 * Palette. Both refusals are logged with the descriptor they collided with.
	 * A descriptor that merely fails Validate is still registered; the Palette shows it disabled with
	 * the reason, which is how a mistyped prefab path stays visible instead of silently going missing.
	 */
	bool Register(const FDreamUIControlDescriptor& Descriptor);
	bool Unregister(FName Name);
	const TArray<FDreamUIControlDescriptor>& GetDescriptors()const { return Descriptors; }
	/**
	 * The categories that hold at least one descriptor, in the order the Palette and the Create menus show them: DreamGUI's
	 * own (DreamUIPaletteCategory) in its order, then any other in the order it was first registered, then Legacy.
	 */
	TArray<FName> GetCategoriesInDisplayOrder()const;
	/** InCategory's descriptors, by display name: what a category lists, wherever it is listed. */
	TArray<const FDreamUIControlDescriptor*> GetDescriptorsInCategory(FName InCategory)const;
	bool Validate(const FDreamUIControlDescriptor& Descriptor, FText& OutError)const;
	void InitializeDynamicDiscovery();
	void ShutdownDynamicDiscovery();
	void RefreshDynamicClasses();
	FOnDreamUIControlRegistryChanged& OnChanged() { return RegistryChanged; }

private:
	FDreamUIControlRegistry();
	void RegisterDefaults();
	void HandleAssetLoaded(UObject* Asset);
	/** Subscribes to GEditor's compile broadcast, once GEditor exists. */
	void BindBlueprintCompiled();
	void HandlePostEngineInit();
	TArray<FDreamUIControlDescriptor> Descriptors;
	TMap<FName, TWeakObjectPtr<UClass>> DynamicPostProcessClasses;
	FDelegateHandle BlueprintCompiledHandle;
	FDelegateHandle PostEngineInitHandle;
	FDelegateHandle AssetLoadedHandle;
	FOnDreamUIControlRegistryChanged RegistryChanged;
};
