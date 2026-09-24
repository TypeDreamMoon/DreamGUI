// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamGUISettings.h"

#include "DreamGUI.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamUISpriteData.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "DreamGUISettings"

UDreamGUISettings::UDreamGUISettings()
{
	// Defaults are the assets the plugin ships, so a fresh project behaves exactly as before. They are
	// soft paths, which is what makes this different from the LoadObject literals they replace: the
	// editor fixes them up on a rename, and they are a declared reference rather than a string.
	//
	// What they are NOT is a cook dependency. These are assigned to a NATIVE CDO, which lives in the
	// /Script/DreamGUI package and never enters the asset registry, so no package dependency exists
	// for the cooker to follow -- the same is true of the config values that override them. Every one
	// of these assets would be pulled in only because UE cooks all mounted content by default
	// (CookOnTheFlyServer's bCookAllByDefault). Under a cook driven by an explicit package list
	// (-map=, DLC/chunk, MapsToCook, -SkipSoftReferences) that default does not apply, and without
	// help they are absent, LoadSetting logs "failed to load" for each, and the whole UI renders
	// blank. The help is the plugin's own Config/Game.ini, which puts /DreamGUI into
	// DirectoriesToAlwaysCook; it ships with the plugin, so this holds without the project knowing.
	DefaultUIMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/DreamGUI/Materials/DreamUI_ImageAndFont.DreamUI_ImageAndFont")));
	DefaultRectBlockMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/DreamGUI/Materials/DreamUI_RectBlock.DreamUI_RectBlock")));
	RenderTargetMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/DreamGUI/Materials/DreamUI_RenderTargetMaterial.DreamUI_RenderTargetMaterial")));

	// Order matters: this is the order the font's material picker shows them in. The shipped presets
	// (Materials/TextEffects) were retired with the built-in text shader -- outline, underlay and glow
	// are FDreamTextStyle on the text now -- so a fresh project starts with none.
	TextEffectPresetMaterials = {};

	DefaultFont = TSoftObjectPtr<UDreamUIFontData_BaseObject>(FSoftObjectPath(TEXT("/DreamGUI/DefaultFont_DistanceField.DefaultFont_DistanceField")));
	DefaultWhiteSolidSprite = TSoftObjectPtr<UDreamUISpriteData>(FSoftObjectPath(TEXT("/DreamGUI/DreamUIPreset_WhiteSolid.DreamUIPreset_WhiteSolid")));
	DefaultFrameRectSprite = TSoftObjectPtr<UDreamUISpriteData>(FSoftObjectPath(TEXT("/DreamGUI/DreamUIPreset_Rect_Sprite.DreamUIPreset_Rect_Sprite")));
	DefaultWhiteSolidTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/DreamGUI/Textures/DreamUIPreset_WhiteSolid.DreamUIPreset_WhiteSolid")));
	DefaultRectBlockData = TSoftObjectPtr<UDreamRectBlockData>(FSoftObjectPath(TEXT("/DreamGUI/DefaultRectBlockData.DefaultRectBlockData")));
	NavigationSelectionClass = TSoftClassPtr<UDreamUserWidget>(FSoftClassPath(TEXT("/DreamGUI/Controls/BP_NavigationSelectionInputHandler.BP_NavigationSelectionInputHandler_C")));

	PresetControlFolder = TEXT("/DreamGUI/Controls/");

	// The STANDALONE native preset, and the choice is load-bearing: this class is the auto-spawn
	// path's actor, and of the two native presets it is the only one that works unconfigured --
	// AutoReceiveInput plus direct key bindings, "useful as a drop-in" by its own doc. The plugin's
	// /DreamGUI/Blueprints/DreamEventSystemActor is a data-only subclass of it that adds nothing
	// (DreamGUI.Input.StandalonePreset.*), so pointing this setting at that Blueprint behaves the same.
	// ADreamEnhancedInputEventSystemActor looks like the newer pick but leaves its mapping context
	// and mouse actions deliberately empty for a Blueprint to fill, so pointing here at the C++ class
	// spawned an event system that never heard a click -- every auto-spawned screen UI lost
	// interaction until this pointed back. The Blueprint that fills them ships with the plugin:
	// /DreamGUI/Blueprints/DreamEventSystemActor_EnhancedInput, a data-only subclass of that class which
	// sets IMC_DreamUIInputContext and the four IA_* actions and nothing else, so everything the class
	// binds is what it gets (DreamGUI.Input.EnhancedPreset.* holds it to that). A project on Enhanced
	// Input should point this setting at THAT, not at the native class.
	EventSystemActorClass = TSoftClassPtr<AActor>(FSoftClassPath(TEXT("/Script/DreamGUI.DreamStandaloneInputEventSystemActor")));
}

const UDreamGUISettings* UDreamGUISettings::Get()
{
	return GetDefault<UDreamGUISettings>();
}

UObject* UDreamGUISettings::LoadSetting(const FSoftObjectPath& Path, const TCHAR* PropertyName)
{
	if (Path.IsNull())
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Project Settings > Plugins > Dream GUI: '%s' is not set."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, PropertyName);
		return nullptr;
	}

	UObject* Loaded = Path.TryLoad();
	if (!Loaded)
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d Project Settings > Plugins > Dream GUI: '%s' points at '%s', which failed to load."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, PropertyName, *Path.ToString());
	}
	return Loaded;
}


#undef LOCTEXT_NAMESPACE
