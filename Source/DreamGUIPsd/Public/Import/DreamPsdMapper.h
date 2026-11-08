// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Psd/DreamPsdDocument.h"
#include "Import/DreamPsdDuiModel.h"
#include "Layout/Margin.h"

/** How a document is mapped. The importer fills it from UDreamPsdImportSettings; a test fills it by hand. */
struct DREAMGUIPSD_API FDreamPsdMapOptions
{
	/** The document's name, for asset names: T_<Name>_<Layer>_<id>. */
	FString DocumentName = TEXT("Document");
	/** Where the textures go: /Game/UI/Hud/Title. */
	FString TextureFolder = TEXT("/Game/DreamPsd");
	/** A face's PostScript name -> the object path of a DreamGUI font. */
	TMap<FString, FString> FontMap;
	/** The font of a text whose face is not in FontMap; empty leaves the text's own default. */
	FString DefaultFontPath;
	/** Text layers become images of their pixels instead of texts. */
	bool bRasterizeText = false;
	/** Hidden layers are imported (hidden); off, they are left out. */
	bool bImportHiddenLayers = true;
	/** Transparent borders are cut off a layer's texture, and its widget sized to what is left. */
	bool bTrimTransparentPixels = true;
	/** Read `!skip`, `@Button` and `#9(...)` in layer names. */
	bool bNamingConventions = true;
	/** A pixel layer's masks are multiplied into its texture rather than made a clip mask. */
	bool bBakeLayerMasks = false;
	/** The blur of a feathered mask: sigma = feather x this. */
	float MaskFeatherScale = 1.0f;
	/** The largest side of a texture a gradient is drawn into. */
	int32 MaxGradientTextureSize = 512;
};

/** A texture the mapping needs: its pixels and where it goes. */
struct DREAMGUIPSD_API FDreamPsdTextureRequest
{
	/** The asset's name: T_Hud_Title_12. */
	FString Name;
	/** Its object path: /Game/UI/Hud/T_Hud_Title_12.T_Hud_Title_12. */
	FString ObjectPath;
	/** A colour texture's pixels (sRGB, straight alpha)... */
	FDreamPsdImage Image;
	/** ...or a mask's (linear, one channel). */
	FDreamPsdGray Gray;
	bool bGray = false;
	/** Repeats (a pattern). */
	bool bTile = false;
	int32 LayerId = -1;
	/** "layer", "mask", "gradient", "pattern", "shape". */
	FString Purpose;
};

struct DREAMGUIPSD_API FDreamPsdMapResult
{
	FDreamPsdDuiNode Root;
	TArray<FDreamPsdTextureRequest> Textures;
	FDreamPsdMessages Messages;
	/** Faces no FontMap entry names, each once. */
	TArray<FString> UnmappedFonts;
	/** Layer effects waiting for a mapping: "Title (12): drop shadow, stroke". */
	TArray<FString> PendingEffects;
	/** Adjustment layers waiting for a mapping: "Warm (7): curves". */
	TArray<FString> PendingAdjustments;
	/** Text warps waiting for a mapping (the text was drawn from its pixels). */
	TArray<FString> PendingWarps;
	/** Layer id -> the node id made for it. */
	TMap<int32, FString> NodeIds;
	int32 LayersMapped = 0;
	int32 LayersSkipped = 0;

	const FDreamPsdTextureRequest* FindTexture(const FString& InObjectPath) const;
	/** A one-paragraph summary for a log or a dialog. */
	FString Summarize() const;
};

/**
 * Maps a document onto DreamGUI nodes:
 *
 * | layer | node |
 * |---|---|
 * | pixels (smart objects too) | `Image` of a trimmed texture |
 * | group | `Widget` filling its parent; a `+ DreamLayerGroup` when it blends, fades or masks as a whole |
 * | text | `Text`, its face through FontMap; drawn from its pixels when rasterized, warped or turned |
 * | live rectangle, rounded rectangle, circle | `RectBlock` (an ellipse: a RectBlock with an elliptic clip mask) |
 * | other shapes | `Image` of the shape's pixels (or of the path, filled) |
 * | solid, gradient fill | `RectBlock`, a gradient drawn into its body texture |
 * | pattern fill | `Image` tiling the pattern |
 * | pixel mask, vector mask | the node's clip mask: a shape when the vector mask is a live shape, else a texture |
 * | clipping | `bClipToBelow` |
 * | blend mode, opacity, fill opacity | `BlendMode` / the group's, `RenderOpacity`, the body's alpha |
 *
 * Layer effects, adjustment layers and text warps are read but not mapped yet: MapLayerEffects, MapAdjustment and
 * MapTextWarp are where they will be, and until then they are listed in the result (PendingEffects...).
 */
class DREAMGUIPSD_API FDreamPsdMapper
{
public:
	FDreamPsdMapper(const FDreamPsdDocument& InDocument, const FDreamPsdMapOptions& InOptions);
	virtual ~FDreamPsdMapper() = default;

	/** Map the whole document. False only when there is nothing to map. */
	bool Map(FDreamPsdMapResult& OutResult);

	/** What a layer's name says beyond its name. */
	struct FNameTags
	{
		FString Name;
		bool bSkip = false;
		/** `@Button` -> "Button". */
		FString Control;
		bool bNineSlice = false;
		/** Pixels: left, top, right, bottom. */
		FMargin NineSlice;
	};
	static FNameTags ParseName(const FString& InLayerName);

	/** A layer blend key as EDreamUIBlendMode names it ("Multiply"); empty for 'pass'; "Alpha" for an unknown key. */
	static FString BlendModeName(const FString& InKey);
	/** An effect descriptor's blend mode id ('Mltp', 'linearDodge'...) as EDreamUIBlendMode names it. */
	static FString DescriptorBlendModeName(const FString& InMode);

protected:
	/**
	 * Map a layer's effects onto InOutNode (a `+ DreamLayerStyle` block, once there is one). The default records them
	 * in PendingEffects and returns false.
	 */
	virtual bool MapLayerEffects(const FDreamPsdLayer& InLayer, FDreamPsdDuiNode& InOutNode, FDreamPsdMapResult& OutResult);

	/**
	 * Make the node of an adjustment layer, which acts on what is drawn below it within InScope (document pixels: its
	 * group's rectangle, or the layer below's when it clips). The default records it in PendingAdjustments and returns
	 * false, and no node is made.
	 */
	virtual bool MapAdjustment(const FDreamPsdLayer& InLayer, const FIntRect& InScope, FDreamPsdDuiNode& OutNode, FDreamPsdMapResult& OutResult);

	/**
	 * Map a text warp onto a text node (a mesh modifier, once there is one). The default records it in PendingWarps and
	 * returns false; the text is then drawn from its pixels.
	 */
	virtual bool MapTextWarp(const FDreamPsdLayer& InLayer, FDreamPsdDuiNode& InOutNode, FDreamPsdMapResult& OutResult);

	const FDreamPsdDocument& Document;
	FDreamPsdMapOptions Options;

private:
	void MapChildren(const FDreamPsdLayerNode& InNode, FDreamPsdDuiNode& OutParent, const FIntRect& InParentRect, FDreamPsdMapResult& OutResult);
	bool MapLayer(const FDreamPsdLayerNode& InNode, const FIntRect& InParentRect, FDreamPsdDuiNode& OutNode, FDreamPsdMapResult& OutResult);
	bool MapGroup(const FDreamPsdLayerNode& InNode, const FDreamPsdLayer& InLayer, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& OutNode, FDreamPsdMapResult& OutResult);
	/** bInVectorIsMask: the vector mask masks the pixels (false for a shape drawn from its pixels, whose vector mask is the shape). */
	bool MapPixels(const FDreamPsdLayer& InLayer, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& OutNode, FDreamPsdMapResult& OutResult, bool bInVectorIsMask = true);
	bool MapText(const FDreamPsdLayer& InLayer, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& OutNode, FDreamPsdMapResult& OutResult);
	bool MapShape(const FDreamPsdLayer& InLayer, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& OutNode, FDreamPsdMapResult& OutResult);
	bool MapFill(const FDreamPsdLayer& InLayer, const FNameTags& InTags, const FIntRect& InParentRect, FDreamPsdDuiNode& OutNode, FDreamPsdMapResult& OutResult);

	/** Placement of a widget at InRect (document pixels) inside InParentRect. */
	static void Place(FDreamPsdDuiNode& OutNode, const FBox2D& InRect, const FIntRect& InParentRect);
	static void Stretch(FDreamPsdDuiNode& OutNode);
	/** Opacity, visibility, clipping, blend mode: what every layer node takes from its record. */
	void ApplyCommon(const FDreamPsdLayer& InLayer, FDreamPsdDuiNode& OutNode, bool bInBlendOnVisual) const;
	/**
	 * The layer's pixel and vector masks onto a node whose rectangle is InNodeRect: a shape clip mask when the vector
	 * mask is a live shape and there is no pixel mask, else one texture of both. On a group that isolates, InGroup is
	 * its `+ DreamLayerGroup` block and the mask is its GroupMask. bInVector: the vector mask is a mask here (not the
	 * shape itself).
	 */
	void ApplyMasks(const FDreamPsdLayer& InLayer, const FIntRect& InNodeRect, FDreamPsdDuiNode& OutNode, FDreamPsdDuiComponent* InGroup,
		bool bInVector, FDreamPsdMapResult& OutResult);
	void AddControl(const FNameTags& InTags, FDreamPsdDuiNode& OutNode, const FDreamPsdLayer& InLayer, FDreamPsdMapResult& OutResult) const;

	int32 LayerIdOf(const FDreamPsdLayer& InLayer) const;
	FString TextureName(const TCHAR* InPrefix, const FDreamPsdLayer& InLayer, const FNameTags& InTags) const;
	FString AddTexture(FDreamPsdMapResult& OutResult, const FString& InName, const FDreamPsdImage& InImage, int32 InLayerId, const TCHAR* InPurpose, bool bInTile = false) const;
	FString AddMaskTexture(FDreamPsdMapResult& OutResult, const FString& InName, const FDreamPsdGray& InMask, int32 InLayerId) const;
	FString FindFont(const FString& InFace, FDreamPsdMapResult& OutResult) const;
};
