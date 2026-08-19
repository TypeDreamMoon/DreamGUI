// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/Texture.h"
#include "SceneUtils.h"
#include "LexUISettings.generated.h"

/** Atlas texture size must be power of 2 */
UENUM(BlueprintType, Category = LGUI)
enum class ELexUIAtlasTextureSizeType :uint8
{
	SIZE_256x256 = 0		UMETA(DisplayName = "256x256"),
	SIZE_512x512			UMETA(DisplayName = "512x512"),
	SIZE_1024x1024			UMETA(DisplayName = "1024x1024"),
	SIZE_2048x2048			UMETA(DisplayName = "2048x2048"),
	SIZE_4096x4096			UMETA(DisplayName = "4096x4096"),
	SIZE_8192x8192			UMETA(DisplayName = "8192x8192"),
};

UENUM(BlueprintType)
enum class ELexUIRendererAntiAliasingMethod :uint8
{
	None,
	MSAA = AAM_MSAA UMETA(DisplayName = "Multisample Anti-Aliasing (MSAA)"),
};

/**
 * Anti Aliasing(MSAA) for LexUI Renderer
 */
UENUM(BlueprintType)
enum class ELexUIRendererMSAASampleCount :uint8
{
	Hidden = 0		UMETA(Hidden),
	One = 1			UMETA(DisplayName = "No MSAA"),
	Two = 2			UMETA(DisplayName = "2x MSAA"),
	Four = 4		UMETA(DisplayName = "4x MSAA"),
	Eight = 8		UMETA(DisplayName = "8x MSAA"),
};

USTRUCT(BlueprintType)
struct LGUI_API FLexUIAtlasSettings
{
	GENERATED_BODY()
public:
	/**
	 * when packing sprites into one atlas texture, LexUI will use this size to create a blank texture then insert sprites.
	*/
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		ELexUIAtlasTextureSizeType AtlasTextureMaxSize = ELexUIAtlasTextureSizeType::SIZE_2048x2048;
	/** weather or not use srgb for generate atlas texture */
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		bool AtlasTextureUseSRGB = true;
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		TEnumAsByte<TextureFilter> AtlasTextureFilter = TextureFilter::TF_Trilinear;
	/** space between two sprites when package into atlas */
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		int32 SpaceBetweenSprites = 2;
};

/** for LexUI config */
UCLASS(config=Engine, defaultconfig)
class LGUI_API ULexUISettings :public UObject
{
	GENERATED_BODY()
public:
	/**
	 * After every editor prefab save, immediately deserialize the just-written payload into a throwaway
	 * world and compare it with the edited hierarchy. A structural mismatch (an object lost, gained or
	 * class-changed by the round trip) rolls the asset back to its previous payload and fails the save,
	 * so corrupted data can never silently reach disk.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Prefab", meta = (DisplayName = "Verify Prefab Save Round Trip"))
	bool bVerifyPrefabSaveRoundTrip = true;
	/**
	 * Escalate property-level round-trip drift (values that load back different from what was saved) from a
	 * logged warning to a hard save failure. Off by default until the known-benign drift list is burned down.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Prefab", meta = (EditCondition = "bVerifyPrefabSaveRoundTrip"))
	bool bBlockPrefabSaveOnPropertyDrift = false;

	/**
	 * Seconds a widget may sit created-but-never-added before it is reported and destroyed. Zero
	 * turns the check off, which is the default.
	 *
	 * This is a development-time leak detector, not a lifetime policy. A created widget is held by
	 * the LexUI manager, so forgetting to add one is invisible until the world tears down -- but a
	 * legitimately slow path (waiting on an async load, say) looks exactly the same from here, and
	 * destroying that caller's widget would be a fault of our own making. Turn it on to find leaks,
	 * leave it off in shipping.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Runtime", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ParkedWidgetLifetimeSeconds = 0.0f;

	/** default atlas setting */
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		FLexUIAtlasSettings DefaultAtlasSetting;
	/** override atlasSettings for your PackingTag, otherwise use defaultAtlasSettings */
	UPROPERTY(EditAnywhere, config, Category = Sprite)
		TMap<FName, FLexUIAtlasSettings> AtlasSettingForSpecificPackingTag;

	/**
	 * LexUI Renderer use ISceneViewExtension to render, so this value can sort with other view extensions, higher comes first.
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
	 * Enable frustum culling for LGUI Renderer.
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering")
		bool bFrustumCulling = false;

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
	 * This will affect all LexUI-Renderer (ScreenSpaceOverlay, WorldSpace-LexUIRenderer, RenderTarget).
	 * Tested on Windows DX11 & DX12, Mac (intel), Android (vulkan), not valid on Android (gles).
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering", meta = (DisplayName="Anti-Aliasing Method"))
		ELexUIRendererAntiAliasingMethod AntiAliasingMethod = ELexUIRendererAntiAliasingMethod::None;
	/**
	 * This will affect all LexUI-Renderer (ScreenSpaceOverlay, WorldSpace-LexUIRenderer, RenderTarget).
	 * Tested on Windows DX11 & DX12, Mac (intel), Android (vulkan), not valid on Android (gles).
	 */
	UPROPERTY(EditAnywhere, config, Category = "Rendering", meta = (DisplayName="MSAA Sample Count"))
		ELexUIRendererMSAASampleCount MSAASampleCount = ELexUIRendererMSAASampleCount::Four;

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
	static const TMap<FName, FLexUIAtlasSettings>& GetAllAtlasSettings();
	static float GetAutoBatchThreshold();
	static float GetParkedWidgetLifetimeSeconds();
	static int32 ConvertAtlasTextureSizeTypeToSize(const ELexUIAtlasTextureSizeType& InType);
	static int32 GetPriorityInSceneViewExtension();
private:
	static const FLexUIAtlasSettings& GetAtlasSettings(const FName& InPackingTag);
};

UCLASS(config=Editor, defaultconfig)
class LGUI_API ULexUIEditorSettings : public UObject
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)override;
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent)override;
#endif
	virtual bool IsEditorOnly()const override { return true; }
#if WITH_EDITORONLY_DATA
	/**
	 * Prefabs in these folders will appear in "LGUI Tools" menu, so we can easily create our own UI control.
	 */
	UPROPERTY(EditAnywhere, config, Category = "LGUI Editor", meta = (LongPackageName))
		TArray<FDirectoryPath> ExtraPrefabFolders;
	/**
	 * Draw helper box on selected UI element.
	 */
	UPROPERTY(EditAnywhere, config, Category = "LGUI Editor")
		bool bDrawHelperFrame = true;

	/** Show selected widget measurement, arrangement, slot, ownership, and clipping diagnostics. */
	UPROPERTY(EditAnywhere, config, Category = "LGUI Editor|Layout")
		bool bShowLayoutDebugVisualization = false;

	/** Overlay common device resolutions on the prefab designer canvas, like UMG's designer surface. */
	UPROPERTY(EditAnywhere, config, Category = "LGUI Editor|Layout")
		bool bShowDesignResolutionGuides = false;

	/**
	 * Draw navigation visualizer
	 */
	UPROPERTY(Transient)
		bool bDrawSelectableNavigationVisualizer = false;
#endif
	/**
	 * For load prefab debug, display a log that shows how much time a LoadPrefab cost.
	 */
	UPROPERTY(EditAnywhere, config, Category = "LGUI")
	bool bLogPrefabLoadTime = false;
};
