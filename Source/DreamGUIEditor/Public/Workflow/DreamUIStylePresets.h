// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamLayerStylePreset.h"
#include "Core/DreamLayerStyleTypes.h"
#include "Workflow/DreamUILayerStyleSnippet.h"

class SWindow;

/** What a new style preset is made of, and where it goes: an image editor's New Style. */
struct FDreamUIStylePresetSaveArgs
{
	/** The effects. */
	FDreamLayerEffects Style;
	/** Set: the layer's blend mode and opacity go with it -- an image editor's "Include Layer Blending Options". */
	TOptional<FDreamLayerStyleBlending> Blending;
	/** The asset's name, or the text preset's. */
	FString Name;
	/** The content folder the asset goes in (`/Game/UI/Styles`). Empty asks where, with the Save Asset dialog. */
	FString PackagePath;
};

/**
 * Making style presets: assets (UDreamLayerStylePreset, which a layer style can follow) and text presets kept in the project
 * settings (UDreamGUISettings::StylePresets, lines of `.dui`). What the style dialog's New Style and the preset library's
 * Save As call.
 */
namespace DreamUIStylePresets
{
	/**
	 * A new style preset asset with InArgs' style and blending, selected in the Content Browser and left unsaved (dirty) for
	 * the author to save. Its name is made unique in the folder. Null, with the reason, when it could not be made -- or when
	 * the Save Asset dialog was cancelled.
	 */
	DREAMGUIEDITOR_API UDreamLayerStylePreset* CreatePresetAsset(const FDreamUIStylePresetSaveArgs& InArgs, FText* OutError = nullptr);

	/** InArgs as a text preset named InArgs.Name in the project settings, written to DefaultDreamGUI.ini; one of that name is replaced. */
	DREAMGUIEDITOR_API bool SaveTextPreset(const FDreamUIStylePresetSaveArgs& InArgs, FText* OutError = nullptr);

	/**
	 * An image editor's New Style window: a name, whether the blending goes with it, and whether it is an asset or a project
	 * setting. Modal; on OK it calls CreatePresetAsset or SaveTextPreset. True when a preset was made.
	 */
	DREAMGUIEDITOR_API bool PromptNewStyle(const FDreamUIStylePresetSaveArgs& InDefaults, TSharedPtr<SWindow> InParent = nullptr);

	/** What applying InPreset writes: following it (a layer style names it as its preset), or a copy of its style; its blending when it has one. */
	DREAMGUIEDITOR_API FDreamUILayerStyleSnippet MakeSnippet(const UDreamLayerStylePreset* InPreset, bool bInFollow);

	/** Every style preset made, removed or changed here: what the preset library refreshes on. */
	DREAMGUIEDITOR_API FSimpleMulticastDelegate& OnPresetsChanged();
}
