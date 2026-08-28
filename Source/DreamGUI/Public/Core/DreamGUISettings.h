// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "DreamGUISettings.generated.h"

enum class EDreamRenderMode : uint8;

class UDreamUIFontData_BaseObject;
class UDreamUserWidget;
class UDreamUISpriteData;
class UMaterialInterface;
class UTexture2D;

/**
 * Every asset and class DreamGUI reaches for by itself, in one place a project can edit.
 *
 * These used to be path literals passed to LoadObject at the point of use. A path literal is worse
 * than a hard reference: it creates no package dependency at all, so renaming or moving the asset
 * compiles clean and turns into a silent null at runtime, and a packaged build has no reason to cook
 * the asset in the first place. Soft pointers here fix both -- the cooker sees them, and the editor
 * fixes them up on a rename.
 *
 * Everything is a fallback. A UDreamCanvas with its own DefaultMaterial set never reads this; the
 * settings only answer the question "what should this be when nobody said".
 */
UCLASS(config = DreamGUI, defaultconfig, meta = (DisplayName = "Dream GUI"))
class DREAMGUI_API UDreamGUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamGUISettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UDreamGUISettings* Get();

	// ---------------------------------------------------------------- Materials

	/** Default material for UI meshes -- images, and both bitmap and SDF text. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> DefaultUIMaterial;

	/** Default material for UDreamRectBlock. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> DefaultRectBlockMaterial;

	/** Material used when a widget renders through a render target. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> RenderTargetMaterial;


	/**
	 * Preset materials offered on a distance-field font, in the order they appear in the picker.
	 *
	 * An array rather than four named entries because the picker shows whatever is here: a project
	 * can drop one, reorder them, or add its own text effect without touching the plugin.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TArray<TSoftObjectPtr<UMaterialInterface>> TextEffectPresetMaterials;

	// ---------------------------------------------------------------- Assets

	/** Font used by a UDreamText that has none set. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UDreamUIFontData_BaseObject> DefaultFont;

	/** Sprite for a solid white fill -- the default appearance of a UDreamSprite. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UDreamUISpriteData> DefaultWhiteSolidSprite;

	/** Sprite for a nine-sliced frame rectangle. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UDreamUISpriteData> DefaultFrameRectSprite;

	/** Texture behind DefaultWhiteSolidSprite, also used directly where a plain white pixel is needed. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UTexture2D> DefaultWhiteSolidTexture;

	/** Default data asset for UDreamRectBlock. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<class UDreamRectBlockData> DefaultRectBlockData;

	/** Prefab spawned to show which widget navigation input has selected. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftClassPtr<UDreamUserWidget> NavigationSelectionClass;


	/**
	 * Content folder the shipped control classes live in.
	 *
	 * A folder rather than a list because three places need it and they need it for different
	 * reasons: one builds a path into it, one is the base for the preset menu, and the palette
	 * excludes it so presets do not show up twice.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Editor")
	FString PresetControlFolder;

	// ---------------------------------------------------------------- Spawned actors

	/**
	 * Spawned when a screen-space UI needs an event system and the level has none.
	 *
	 * A class rather than an asset because it is spawned: point it at the C++ preset, at one of the
	 * preset Blueprints, or at a project's own actor.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> EventSystemActorClass;

	/** Spawned to raycast world-space UI from the mouse when nothing else provides a pointer. */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> WorldSpaceRaycasterSourceClass;

	/** Root actor the prefab factory places for a screen-space prefab. */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> ScreenSpaceRootClass;

	/** Root actor the prefab factory places for a world-space prefab drawn by DreamGUI's own renderer. */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> WorldSpaceRootClass;

	/** Root actor for a world-space prefab drawn through UMG's renderer instead. */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> WorldSpaceUERendererRootClass;

	/** Screen-space root for a markup (.dreamuiml) document, which has its own presenter component. */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> MarkupScreenSpaceRootClass;

	/** World-space root for a markup document. */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> MarkupWorldSpaceRootClass;

	/** World-space root for a markup document drawn through UMG's renderer. */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> MarkupWorldSpaceUERendererRootClass;

	/** The root class for one render mode, so the two actor factories cannot disagree. */
	TSoftClassPtr<AActor> GetRootClassForRenderMode(EDreamRenderMode RenderMode, bool bMarkup) const;

	// ---------------------------------------------------------------- Resolution

	/**
	 * Load a configured soft asset, or log which setting is empty or broken and return null.
	 *
	 * Callers used to get a bare null from LoadObject and had no way to say what was missing. Naming
	 * the property is the whole point: "DefaultUIMaterial is not set" is actionable, "material is
	 * null" is not.
	 */
	static UObject* LoadSetting(const FSoftObjectPath& Path, const TCHAR* PropertyName);

	template<typename T>
	static T* LoadSetting(const TSoftObjectPtr<T>& Soft, const TCHAR* PropertyName)
	{
		return Cast<T>(LoadSetting(Soft.ToSoftObjectPath(), PropertyName));
	}

	/**
	 * Same, for a class-valued setting. Templated over the class the setting is typed to, because
	 * these are no longer all actors: a UI hierarchy is a class now too.
	 */
	template<typename T>
	static UClass* LoadSettingClass(const TSoftClassPtr<T>& Soft, const TCHAR* PropertyName)
	{
		// TryLoad on a class path returns the UClass itself, not an instance, so the cast is the
		// identity when the setting is good and a clean null when it points at something else.
		return Cast<UClass>(LoadSetting(Soft.ToSoftObjectPath(), PropertyName));
	}
};
