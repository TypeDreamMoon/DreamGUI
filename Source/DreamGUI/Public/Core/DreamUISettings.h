// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture.h"
#include "SceneUtils.h"
#include "DreamUISettings.generated.h"

/** Atlas texture size must be power of 2 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUIAtlasTextureSizeType :uint8
{
	SIZE_256x256 = 0		UMETA(DisplayName = "256x256"),
	SIZE_512x512			UMETA(DisplayName = "512x512"),
	SIZE_1024x1024			UMETA(DisplayName = "1024x1024"),
	SIZE_2048x2048			UMETA(DisplayName = "2048x2048"),
	SIZE_4096x4096			UMETA(DisplayName = "4096x4096"),
	SIZE_8192x8192			UMETA(DisplayName = "8192x8192"),
};

UENUM(BlueprintType)
enum class EDreamUIRendererAntiAliasingMethod :uint8
{
	None,
	MSAA = AAM_MSAA UMETA(DisplayName = "Multisample Anti-Aliasing (MSAA)"),
};

/**
 * Anti Aliasing(MSAA) for DreamUI Renderer
 */
UENUM(BlueprintType)
enum class EDreamUIRendererMSAASampleCount :uint8
{
	Hidden = 0		UMETA(Hidden),
	One = 1			UMETA(DisplayName = "No MSAA"),
	Two = 2			UMETA(DisplayName = "2x MSAA"),
	Four = 4		UMETA(DisplayName = "4x MSAA"),
	Eight = 8		UMETA(DisplayName = "8x MSAA"),
};

USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamUIAtlasSettings
{
	GENERATED_BODY()
public:
	/**
	 * when packing sprites into one atlas texture, DreamUI will use this size to create a blank texture then insert sprites.
	*/
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		EDreamUIAtlasTextureSizeType AtlasTextureMaxSize = EDreamUIAtlasTextureSizeType::SIZE_2048x2048;
	/** weather or not use srgb for generate atlas texture */
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		bool AtlasTextureUseSRGB = true;
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		TEnumAsByte<TextureFilter> AtlasTextureFilter = TextureFilter::TF_Trilinear;
	/** space between two sprites when package into atlas */
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		int32 SpaceBetweenSprites = 2;
};

/** for DreamUI config */
UCLASS(config=Engine, defaultconfig)
class DREAMGUI_API UDreamUISettings :public UObject
{
	GENERATED_BODY()
public:
	/**
	 * Seconds a widget may sit created-but-never-added before it is reported and destroyed. Zero
	 * turns the check off, which is the default.
	 *
	 * This is a development-time leak detector, not a lifetime policy. A created widget is held by
	 * the DreamUI manager, so forgetting to add one is invisible until the world tears down -- but a
	 * legitimately slow path (waiting on an async load, say) looks exactly the same from here, and
	 * destroying that caller's widget would be a fault of our own making. Turn it on to find leaks,
	 * leave it off in shipping.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Runtime", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ParkedWidgetLifetimeSeconds = 0.0f;

	/** default atlas setting */
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		FDreamUIAtlasSettings DefaultAtlasSetting;
	/** override atlasSettings for your PackingTag, otherwise use defaultAtlasSettings */
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		TMap<FName, FDreamUIAtlasSettings> AtlasSettingForSpecificPackingTag;

	/**
	 * DreamUI Renderer use ISceneViewExtension to render, so this value can sort with other view extensions, higher comes first.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering")
		int32 PriorityInSceneViewExtension = 0;

	/** 
	 * 3D UI elements is almost not possible to check overlap, so a 3D UI element only allowed to batch to last draw-call from draw-call list, as long as common check is passed (material/ texture).
	 * Only 2D elements are easier to check overlap and batch together.
	 *		Rules for telling if a UI element is 2D (convert the UI element in Canvas's relative space):
	 *			Relative location.Z less than threshold.
	 *			Relative rotation.X/Y less than threshold.
	 * This is the threshold for determine if the UI element is 2D.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering", meta = (ClampMin = "0.00001", ClampMax = "100"))
		float AutoBatchThreshold = 0.01f;

	/**
	 * Enable frustum culling for DreamGUI Renderer.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering")
		bool bFrustumCulling = false;

	/**
	 * Draw widgets that have no custom material with DreamGUI's built-in shader instead of the default UI material.
	 * Only applies to canvases rendered by the DreamUI renderer (ScreenSpaceOverlay, RenderTarget, WorldSpace-DreamUI);
	 * UE-rendered world-space canvases always use a material. Text effects (outline, underlay, glow, MTSDF fonts)
	 * need this on.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering", meta = (DisplayName = "Use Built-in UI Shader"))
		bool bUseBuiltInUIShader = true;

	/**
	 * Rasterize outline (MTSDF) glyphs on a worker thread. A glyph that is not in the atlas yet is laid
	 * out with its real advance and drawn once it lands, instead of stalling the frame while every new
	 * glyph is generated. The first few glyphs of a frame are still generated synchronously so short
	 * labels appear whole; see AsyncGlyphSyncBudgetPerFrame.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Text", meta = (DisplayName = "Async Glyph Rasterization"))
		bool bAsyncGlyphRasterization = true;
	/** How many new glyphs a frame may generate on the game thread before the rest go to the worker. */
	UPROPERTY(EditAnywhere, config, Category = "Text", meta = (ClampMin = "0", EditCondition = "bAsyncGlyphRasterization"))
		int32 AsyncGlyphSyncBudgetPerFrame = 24;
	/**
	 * How many Texture2DArray slices a font's glyph atlas may grow to. The atlas only grows -- a
	 * rect-packed atlas cannot hand one glyph's rectangle to a glyph of another size without repacking,
	 * so there is no "evict one glyph" -- and when it reaches this budget the whole cache is thrown away
	 * and refilled on demand, which is what Slate's own font cache does when its atlas fills. Every text
	 * using the font is re-laid-out on the spot, so a flush costs one frame; if that happens repeatedly,
	 * raise this or the atlas texture size, or use fewer distinct font sizes (a bitmap font caches a
	 * separate glyph per size, and Best Fit walks several sizes per search).
	 * Clamped to what the RHI can address (GMaxTextureArrayLayers).
	 */
	UPROPERTY(EditAnywhere, config, Category = "Text", meta = (ClampMin = "1", UIMin = "1", UIMax = "64"))
		int32 MaxFontAtlasSlices = 8;

	/** If false, ScreenSpaceUI can still do interaction and animation when GamePause */
	UPROPERTY(EditAnywhere, config, Category = "Game", meta = (DisplayName="ScreenSpaceUI Affect by GamePause"))
		bool bScreenSpaceUIAffectByGamePause = false;
	UPROPERTY(EditAnywhere, config, Category = "Game", meta = (DisplayName = "ScreenSpaceUI Affect by TimeDilation"))
		bool bScreenSpaceUIAffectByTimeDilation = false;
	UPROPERTY(EditAnywhere, config, Category = "Game", meta = (DisplayName = "WorldSpaceUI Affect by GamePause"))
		bool bWorldSpaceUIAffectByGamePause = true;
	UPROPERTY(EditAnywhere, config, Category = "Game", meta = (DisplayName = "WorldSpaceUI Affect by TimeDilation"))
		bool bWorldSpaceUIAffectByTimeDilation = true;

	/**
	 * This will affect all DreamUI-Renderer (ScreenSpaceOverlay, WorldSpace-DreamUIRenderer, RenderTarget).
	 * Tested on Windows DX11 & DX12, Mac (intel), Android (vulkan), not valid on Android (gles).
	 *
	 * "Not valid" is enforced rather than merely stated: FDreamUIRenderer asks the RHI whether the
	 * running shader platform supports MSAA and falls back to no anti-aliasing when it does not,
	 * warning once. That keeps a project that turns MSAA on globally -- the ordinary thing to do --
	 * from carrying it onto a platform that cannot honour it. Override this per platform
	 * (a [/Script/DreamGUI.DreamUISettings] section in the platform config) to make the choice
	 * explicit rather than letting the fallback make it.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering", meta = (DisplayName="Anti-Aliasing Method"))
		EDreamUIRendererAntiAliasingMethod AntiAliasingMethod = EDreamUIRendererAntiAliasingMethod::None;
	/**
	 * This will affect all DreamUI-Renderer (ScreenSpaceOverlay, WorldSpace-DreamUIRenderer, RenderTarget).
	 * Tested on Windows DX11 & DX12, Mac (intel), Android (vulkan), not valid on Android (gles).
	 *
	 * Ignored, with the same one-time warning, wherever the RHI reports no MSAA support -- see
	 * AntiAliasingMethod above.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering", meta = (DisplayName="MSAA Sample Count"))
		EDreamUIRendererMSAASampleCount MSAASampleCount = EDreamUIRendererMSAASampleCount::Four;

#if WITH_EDITORONLY_DATA
	static float CacheAutoBatchThreshold;
#endif
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
#endif
public:
	static int32 GetAtlasTextureMaxSize(const FName& InPackingTag);
	static bool GetAtlasTextureSRGB(const FName& InPackingTag);
	static int32 GetAtlasTexturePadding(const FName& InPackingTag);
	static TextureFilter GetAtlasTextureFilter(const FName& InPackingTag);
	static const TMap<FName, FDreamUIAtlasSettings>& GetAllAtlasSettings();
	static float GetAutoBatchThreshold();
	static float GetParkedWidgetLifetimeSeconds();
	static int32 ConvertAtlasTextureSizeTypeToSize(const EDreamUIAtlasTextureSizeType& InType);
	static int32 GetPriorityInSceneViewExtension();
	static bool GetUseBuiltInUIShader();
	static bool GetAsyncGlyphRasterization();
	static int32 GetAsyncGlyphSyncBudgetPerFrame();
	/** The configured atlas slice budget, clamped to what the RHI can address. */
	static int32 GetMaxFontAtlasSlices();
private:
	static const FDreamUIAtlasSettings& GetAtlasSettings(const FName& InPackingTag);
};

UCLASS(config=Editor, defaultconfig)
class DREAMGUI_API UDreamUIEditorSettings : public UObject
{
	GENERATED_BODY()
public:
	virtual bool IsEditorOnly()const override { return true; }
#if WITH_EDITORONLY_DATA
	/**
	 * Draw helper box on selected UI element.
	 */
	UPROPERTY(EditAnywhere, config, Category = "DreamGUI Editor")
		bool bDrawHelperFrame = true;

	/** Show selected widget measurement, arrangement, slot, ownership, and clipping diagnostics. */
	UPROPERTY(EditAnywhere, config, Category = "DreamGUI Editor|Layout")
		bool bShowLayoutDebugVisualization = false;

	/** Overlay common device resolutions on the designer canvas, like UMG's designer surface. */
	UPROPERTY(EditAnywhere, config, Category = "DreamGUI Editor|Layout")
		bool bShowDesignResolutionGuides = false;

	/**
	 * Draw navigation visualizer
	 */
	UPROPERTY(Transient)
		bool bDrawSelectableNavigationVisualizer = false;
#endif
};
