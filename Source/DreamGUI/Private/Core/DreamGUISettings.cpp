// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamGUISettings.h"

#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
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
	// cooker follows these, and a rename fixes them up.
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
	// AutoReceiveInput plus direct key bindings, "useful as a drop-in" by its own doc.
	// ADreamEnhancedInputEventSystemActor looks like the newer pick but leaves its mapping context
	// and mouse actions deliberately empty for a Blueprint to fill (and no such Blueprint ships),
	// so pointing here at it spawned an event system that never heard a click -- every
	// auto-spawned screen UI lost interaction until this pointed back.
	EventSystemActorClass = TSoftClassPtr<AActor>(FSoftClassPath(TEXT("/Script/DreamGUI.DreamStandaloneInputEventSystemActor")));

	WorldSpaceRaycasterSourceClass = TSoftClassPtr<AActor>(FSoftClassPath(TEXT("/DreamGUI/Blueprints/DreamWorldSpaceRaycasterSource_Mouse.DreamWorldSpaceRaycasterSource_Mouse_C")));
	ScreenSpaceRootClass = TSoftClassPtr<AActor>(FSoftClassPath(TEXT("/DreamGUI/Blueprints/ScreenSpaceRoot.ScreenSpaceRoot_C")));
	WorldSpaceRootClass = TSoftClassPtr<AActor>(FSoftClassPath(TEXT("/DreamGUI/Blueprints/WorldSpaceRoot_DreamRenderer.WorldSpaceRoot_DreamRenderer_C")));
	WorldSpaceUERendererRootClass = TSoftClassPtr<AActor>(FSoftClassPath(TEXT("/DreamGUI/Blueprints/WorldSpaceRoot_UERenderer.WorldSpaceRoot_UERenderer_C")));
}

TSoftClassPtr<AActor> UDreamGUISettings::GetRootClassForRenderMode(EDreamRenderMode RenderMode) const
{
	// One switch, so two factories cannot build this path by hand and drift apart -- which is what
	// they did before it existed.
	switch (RenderMode)
	{
	case EDreamRenderMode::WorldSpace:
		return WorldSpaceUERendererRootClass;
	case EDreamRenderMode::WorldSpace_DreamUI:
		return WorldSpaceRootClass;
	case EDreamRenderMode::ScreenSpaceOverlay:
	default:
		return ScreenSpaceRootClass;
	}
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
