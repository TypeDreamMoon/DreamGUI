// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Import/DreamPsdMapper.h"
#include "DreamPsdImportSettings.generated.h"

class UDreamUIFontData_BaseObject;

/** How layered documents are imported into DreamGUI: Project Settings > Plugins > DreamGUI Layered Document Import. */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "DreamGUI Layered Document Import"))
class DREAMGUIPSD_API UDreamPsdImportSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamPsdImportSettings();

	/**
	 * A face's PostScript name, as the document names it ("Roboto-Bold"), or a family ("Roboto"), to the DreamGUI font
	 * its texts use. A face with no entry takes DefaultFont, and the import lists it.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Text")
	TMap<FString, TSoftObjectPtr<UDreamUIFontData_BaseObject>> FontMap;

	/** The font of a text whose face FontMap does not name. None: DreamGUI's default font. */
	UPROPERTY(config, EditAnywhere, Category = "Text")
	TSoftObjectPtr<UDreamUIFontData_BaseObject> DefaultFont;

	/** Import text layers as images of their pixels, not as texts. */
	UPROPERTY(config, EditAnywhere, Category = "Text")
	bool bRasterizeText = false;

	/** Import hidden layers (hidden); off, they are left out. */
	UPROPERTY(config, EditAnywhere, Category = "Layers")
	bool bImportHiddenLayers = true;

	/** Cut transparent borders off each layer's texture and size its widget to what is left. */
	UPROPERTY(config, EditAnywhere, Category = "Layers")
	bool bTrimTransparentPixels = true;

	/** Read `!skip` (leave the layer out), `@Button` / `@Toggle` / `@Slider` (add that control) and `#9(l,t,r,b)` (nine-slice margins in pixels) in layer names. */
	UPROPERTY(config, EditAnywhere, Category = "Layers")
	bool bNamingConventions = true;

	/** Multiply a pixel layer's masks into its texture instead of making them a clip mask (fewer textures, nothing to edit). */
	UPROPERTY(config, EditAnywhere, Category = "Masks")
	bool bBakeLayerMasks = false;

	/** A feathered mask is blurred with a sigma of its feather times this. */
	UPROPERTY(config, EditAnywhere, Category = "Masks", meta = (ClampMin = "0.0", UIMax = "2.0"))
	float MaskFeatherScale = 1.0f;

	/** The largest side of a texture a gradient is drawn into. */
	UPROPERTY(config, EditAnywhere, Category = "Textures", meta = (ClampMin = "16", ClampMax = "4096"))
	int32 MaxGradientTextureSize = 512;

	/**
	 * Make the Content Browser's Import take .psd files as DreamUI widgets. Off by default: the engine imports a .psd as a
	 * flat texture, and that stays what Import does until this is on. The DreamGUI menu of a folder imports one either way.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	bool bHandleContentBrowserImport = false;

	/** The widget Blueprint's name: this prefix and the document's name. */
	UPROPERTY(config, EditAnywhere, Category = "Import")
	FString BlueprintPrefix = TEXT("WBP_");

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** These settings as the mapper takes them, for a document of InName whose textures go to InTextureFolder. */
	FDreamPsdMapOptions MakeMapOptions(const FString& InDocumentName, const FString& InTextureFolder) const;
};
