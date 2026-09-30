// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Camera/CameraTypes.h"
#include "Core/DreamCanvasAsyncFunctionRunnable.h"
#include "Core/DreamCanvasDrawCallProcessingRunnable.h"
#include "Core/DreamCanvasProcessingDrawCallData.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIDrawCall.h"
#include "DreamUIRender/IDreamUIRendererViewSource.h"
#include "Math/TransformCalculus2D.h"
#include "DreamCanvas.generated.h"

class FDreamUIClipData;
class UDreamUIDataAsTexture;

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamRenderMode :uint8
{
	/**
	 * Render in screen space. If there are multiple screen-space-ui-root in world, they will be sorted by SortOrder property.
	 * This mode use DreamUI's custom render pipeline.
	 * This mode need a DreamCanvasScaler to control the size and scale.
	 */
	ScreenSpaceOverlay = 0,
	/**
	 * Render in world space by UE default render pipeline.
	 * This mode use engine's default render pipeline, so post process will affect ui.
	 */
	WorldSpace=1			UMETA(DisplayName = "World Space - UE Renderer"),
	/**
	 * Render in world space by DreamUI's custom render pipeline, 
	 * This mode use DreamUI's custom render pipeline, will not be affected by post process.
	 */
	WorldSpace_DreamUI = 3		UMETA(DisplayName = "World Space - DreamUI Renderer"),
	/**
	 * Render to a custom render target.
	 */
	RenderTarget = 2		UMETA(DisplayName = "Render Target"),
	
	None = 255				UMETA(Hidden),
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamCanvasRenderTargetSizeMode : uint8
{
	None,
	/** Change DreamCanvas's size to fit RenderTarget. */
	CanvasFitToRenderTarget,
	/** Change RenderTarget's size to fit DreamCanvas. */
	RenderTargetFitToCanvas,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamCanvasRenderTargetUpdateMode : uint8
{
	/** DreamUI will automatically manage update, only draw to RenderTarget when it detect something change. */
	Automatic,
	/** Always draw to RenderTarget every frame. */
	Always,
	/** Only draw to RenderTarget when call RequestUpdateForRenderTarget. */
	WhenRequest,
};

UENUM(BlueprintType, meta = (Bitflags), Category = DreamGUI)
enum class EDreamCanvasOverrideParameters :uint8
{
	DefaultMaterial,
	RequireNormalAndTangent,
	BlendDepth,
	DepthFade,
};
ENUM_CLASS_FLAGS(EDreamCanvasOverrideParameters);

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamCanvasScaleMode:uint8
{
	/** 1 unit is 1 pixel render in screen*/
	ConstantPixelSize,
	/** scale UI with reference resolution and screen resolution*/
	ScaleWithScreenSize,
	/**
	 * Assign CustomScale parameter to use a custom class calculate resolution and scale.
	 */
	Custom,
	/**
	 * UMG's rule: take the scale from the engine's UI Scale Rule and curve
	 * (Project Settings > User Interface), then lay out in "viewport / scale" units -- the same
	 * thing SGameLayerManager feeds its SDPIScaler. Unlike ScaleWithScreenSize, the layout rect
	 * keeps the curve's design size at every resolution, so a layout authored for it never runs
	 * out of room and gets clipped; it only renders smaller. ReferenceResolution, ScreenMatchMode
	 * and Match are unused in this mode -- the engine settings are the single source of truth,
	 * which also means a project that tunes its DPI curve moves UMG and DreamUI together.
	 */
	ScaleWithEngineDPI,
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamCanvasScreenMatchMode :uint8
{
	/** Use "MatchFromWidthToHeight" and "ReferenceResolution" properties to control size and scale UI*/
	MatchWidthOrHeight,
	/** If viewport's aspect ratio not match "ReferenceResolution"'s aspect ratio, then expand size and scale UI*/
	Expand,
	/** if viewport's aspect ratio not match "ReferenceResolution"'s aspect ratio, then shrink size and scale UI*/
	Shrink,
};

class UDreamCanvas;

UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamCanvasCustomScale: public UObject
{
	GENERATED_BODY()
public:
	/** Initialize, called when DreamCanvas Awake. */
	virtual void Init(UDreamCanvas* InCanvas);
	/** Called when DreamCanvas calculate viewport size and scale. */
	virtual void CalculateSizeAndScale(UDreamCanvas* InCanvas, const FIntPoint& InViewportSize, FIntPoint& OutDreamCanvasSize, float& OutScale);
	/**
	 * Convert position from viewport to DreamCanvas space.
	 * @param InPosition The point's pixel position on viewport.
	 * @param Result DreamCanvas space position, left bottom is zero point.
	 * @return convert will fail if this DreamCanvas is not root canvas
	 */
	virtual bool ConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result)const;
	/**
	 * Convert position from DreamCanvas space to viewport.
	 * @param InPosition The point's position in DreamCanvas space.
	 * @param Result in viewport, pixel unit, left top is zero point.
	 * @return convert will fail if this DreamCanvas is not root canvas
	 */
	virtual bool ConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result)const;
protected:
	/** Initialize, called when DreamCanvas Awake. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Init"), Category = "DreamGUI")
	void ReceiveInit(UDreamCanvas* InCanvas);
	/** Called when DreamCanvas calculate viewport size and scale. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "CalculateSizeAndScale"), Category = "DreamGUI")
	void ReceiveCalculateSizeAndScale(UDreamCanvas* InCanvas, const FIntPoint& InViewportSize, FIntPoint& OutDreamCanvasSize, float& OutScale);
	/**
	 * Convert position from viewport to DreamCanvas space.
	 * @param InPosition The point's pixel position on viewport.
	 * @param Result DreamCanvas space position, left bottom is zero point.
	 * @return convert will fail if this DreamCanvas is not root canvas
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ConvertPositionFromViewportToCanvas"), Category = "DreamGUI")
	bool ReceiveConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result)const;
	/**
	 * Convert position from DreamCanvas space to viewport.
	 * @param InPosition The point's position in DreamCanvas space.
	 * @param Result in viewport, pixel unit, left top is zero point.
	 * @return convert will fail if this DreamCanvas is not root canvas
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ConvertPositionFromCanvasToViewport"), Category = "DreamGUI")
	bool ReceiveConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result)const;
};

class UDreamWidget;
class UDreamVisual;
class FDreamUIMaterialProxy;
class UDreamVisualBatchMesh;
class UDreamVisualDirectMesh;
class UDreamUIMeshComponent;
class FDreamVisualPostProcessRenderProxy;
class UTextureRenderTarget2D;

/**
 * Canvas is for render and update all UI elements.
 * Default UV channels-
 *		UV0: Texture coordinate
 *		UV1: X- Widget property data coordinate, including clipData coordinate in data texture; Y- Slice index of texture array, mostly for font rendering
 * Other UV channels are defined by DreamVisual, check DreamText and DreamRectBlock.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamCanvas : public UDreamUIBehaviour, public IDreamUIRendererViewSource
{
	GENERATED_BODY()

public:	
	UDreamCanvas();
private:
	//~ Begin IDreamUIRendererViewSource: what the renderer sets a screen-space view up from, when this is the root
	virtual FVector GetRendererViewLocation() const override { return GetViewLocation(); }
	virtual FRotator GetRendererViewRotator() const override { return GetViewRotator(); }
	virtual FMatrix GetRendererProjectionMatrix() const override { return GetProjectionMatrix(); }
	virtual bool GetRendererEnableDepthTest() const override { return GetEnableDepthTest(); }
	virtual float GetRendererScreenSpaceRenderScale() const override { return GetScreenSpaceRenderScale(); }
	//~ End IDreamUIRendererViewSource
protected:
	virtual void Awake() override;
#if WITH_EDITOR
public:
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad()override;
	virtual void PostEditUndo()override;
	void EnsureDataForRebuild();
#endif
	virtual void OnRegister()override;
	virtual void OnUnregister()override;
	virtual void PostInitProperties() override;
	virtual void BeginDestroy() override;

	static FName GetPropertyName_TraceChannel()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamCanvas, TraceChannel);
	}
private:
	/** clear draw-calls */
	void ClearDrawCall();
	void RemoveFromViewExtension(bool PropagateToChildrenCanvas);
	TSharedPtr<class FDreamUIRenderer, ESPMode::ThreadSafe> RenderTargetViewExtension = nullptr;
	TSharedPtr<class FDreamUIRenderer, ESPMode::ThreadSafe> GetRenderTargetViewExtension();
	/** Render-target mode: the target is to be drawn this frame, once the sections have gone (DrawRenderTargetIfRequested). */
	bool bRenderTargetDrawRequested = false;
public:
	/**
	 * Render-target mode, with the renderer's drawer on: draws the target, when this frame asked for it, with a render
	 * command of its own. The UI manager calls this after every canvas has sent this frame's sections.
	 */
	void DrawRenderTargetIfRequested();
	/** mark canvas layout dirty */
	void MarkTransformOrDimensionChanged();
	/**
	 * Mark update this Canvas. Canvas don't need to update every frame, only update when need to.
	 * Some rules if update could trigger draw-call's rebuild:
	 *		1. Commonly material & texture change and UI item's active state change
	 *		2. Transform & vertex position change, draw-call could overlap with each other
	 *		3. Hierarchy order change, this is directly related to render order
	 * And about draw-call's rebuild, it's not actually force rebuild, it will check and reuse prev draw-call if possible.
	 * @param	bRebuildDrawCall	When we need rebuild draw-call? Material or texture change, transform or vertex position change, add or remove ui element
	 */
	void MarkCanvasUpdate(bool bRebuildDrawCall);
	/** Invalidate the cached hierarchy list before rebuilding draw calls after sibling order changes. */
	void MarkCanvasHierarchyChanged();

	static void BuildProjectionMatrix(FIntPoint InViewportSize, ECameraProjectionMode::Type InProjectionType, float FOV, float FarClipPlane, float NearClipPlane, FMatrix& OutProjectionMatrix);
	FMatrix GetViewProjectionMatrix()const;
	FMatrix GetProjectionMatrix()const;
	FVector GetViewLocation()const;
	FRotator GetViewRotator()const;
	FIntPoint GetViewportSize()const;
	/**
	 * Substitute a viewport size for this canvas, for a caller that has no viewport to read one from.
	 *
	 * A ScreenSpaceOverlay canvas takes its size from the local player's viewport, so a world with no
	 * player controller -- a headless fixture, a tool that builds a tree nobody is looking at -- leaves
	 * GetViewportSize answering with the 2x2 fallback, and everything derived from it degenerates with
	 * it: the root widget is sized 2x2, the projection matrix describes a 2x2 screen, and a screen-space
	 * ray cast through that matrix lands nowhere near the pixel the caller aimed at.
	 *
	 * Applied immediately, because the size the canvas last applied is cached and the derived widget
	 * size is what a caller asks for this for. Unset by default, so a canvas nobody has called this on
	 * behaves exactly as before. Deliberately not a UPROPERTY: a substituted viewport is a property of
	 * the run, not of the asset, and serializing it would let one escape into content.
	 */
	void SetViewportSizeOverride(const FIntPoint& InSize);
	/** Drop the substituted size and go back to whatever the real viewport says. */
	void ClearViewportSizeOverride();
	bool HasViewportSizeOverride()const { return ViewportSizeOverride.IsSet(); }
	/** get scale value of canvas. only valid for root canvas. */
	FORCEINLINE float GetCanvasScale()const { return CanvasScale; }
private:
	friend class UDreamCanvasScaler;
	float CanvasScale = 1.0f;//for screen space UI, screen size / root canvas size

	/** hierarchy changed */
	void OnUIHierarchyAttachmentChanged();
	void OnWidgetActiveChanged(bool WidgetActive);
public:
	/** get root canvas on hierarchy */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	UDreamCanvas* GetRootCanvas()const;
	/** is this the root canvas in hierarchy */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool IsRootCanvas()const;
	/** return root SceneComponent if the root canvas is attached to a SceneComponent */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	USceneComponent* GetAttachedRootSceneComponent() const;
	/**
	 * Only set on root canvas. From then on the widget tree follows the component: whenever it
	 * moves, the tree is re-placed (the component's TransformUpdated is the trigger, so any host
	 * works -- a presenter, a plain actor's root, a socket). Passing null detaches.
	 */
	void AttachToSceneComponent(USceneComponent* InSceneComp);

	bool IsRenderToScreenSpace()const;
	bool IsRenderToRenderTarget()const;
	bool IsRenderToWorldSpace()const;
	bool IsRenderByDreamUIRendererOrUERenderer()const;

	TWeakObjectPtr<UDreamCanvas> GetParentCanvas()const { return ParentCanvas; }

	void SetParentCanvas(UDreamCanvas* InParentCanvas);

	static void CollectChildrenCanvas(UDreamCanvas* Target, TArray<UDreamCanvas*>& OutAllChildrenCanvas, bool IncludeTarget = true);

	DECLARE_EVENT_ThreeParams(UDreamCanvas, FRenderModeChangedEvent, UDreamCanvas*, EDreamRenderMode/*Old*/, EDreamRenderMode/*New*/);
	DECLARE_EVENT_OneParam(UDreamCanvas, FRenderTargetChangedEvent, UTextureRenderTarget2D*);
protected:
	/** Root DreamCanvas on hierarchy. DreamGUI's update start from the RootCanvas, and goes all down to every UI elements under it */
	UPROPERTY(Transient) mutable TWeakObjectPtr<UDreamCanvas> RootCanvas = nullptr;
	void CheckRenderMode(bool PropagateToChildrenCanvas);
	/** check RootCanvas. search for it if not valid */
	bool CheckRootCanvas(bool forceRecheck = false)const;
	/** nearest up parent Canvas */
	UPROPERTY(Transient) TWeakObjectPtr<UDreamCanvas> ParentCanvas = nullptr;

protected:
	friend class FDreamCanvasCustomization;
	friend class FDreamWidgetCustomization;

	float CalculateDistanceToCamera()const;

	/**
	 * Force this canvas render to a TextureRenderTarget, no matter what render mode of the root canvas is.
	 * This will break canvas link and make this canvas as root canvas.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bForceRenderToTarget = false;
	/**
	 * Leave elements that are entirely outside this canvas's rect out of the draw-call list.
	 *
	 * Only has an effect when this canvas's rect is the surface being drawn -- a root canvas, or one
	 * with bForceRenderToTarget. A plain child canvas draws into its parent's surface and a canvas is
	 * not a clipper, so its own rect says nothing about what is visible; culling is skipped there
	 * whatever this says. Turn it off if you rely on off-screen elements still being assembled (for
	 * instance to read a draw-call's vertex data for something other than drawing).
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bCullElementsOutsideCanvas = true;
	/**
	 * Resolution this canvas's screen-space UI is rendered at, as a fraction of the viewport.
	 *
	 * 1 draws at full resolution. Below 1 draws into a smaller intermediate target and upscales, which
	 * trades UI sharpness for fill rate -- the usual knob on mobile and on a lower-end console profile.
	 * Only applies to a root canvas rendering ScreenSpaceOverlay through DreamGUI's own renderer;
	 * RenderTarget mode already has RenderTargetResolutionScale, and WorldSpace UI is scaled by the
	 * scene's own resolution settings.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0.1", ClampMax = "1.0", UIMin = "0.1", UIMax = "1.0"))
	float ScreenSpaceRenderScale = 1.0f;
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamRenderMode RenderMode = EDreamRenderMode::WorldSpace;
	/**
	 * Render to RenderTarget, if not specified then DreamGUI will create a new one (AutoRenderTarget).
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UTextureRenderTarget2D> RenderTarget;
	/**
	 * The render target this canvas made for itself because none was assigned. Held apart from RenderTarget,
	 * which is the author's: never saved, duplicated or copied, so a copy of the canvas makes its own rather
	 * than drawing into this one.
	 */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
		TObjectPtr<UTextureRenderTarget2D> AutoRenderTarget;
	/** Clear color for TextureRenderTarget */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	FColor RenderTargetClearColor = FColor::Transparent;
	/** Controls how DreamCanvas render to RenderTarget. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamCanvasRenderTargetUpdateMode RenderTargetUpdateMode = EDreamCanvasRenderTargetUpdateMode::Automatic;
	/**
	 * How RenderTarget and DreamCanvas's size change depend on the other.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamCanvasRenderTargetSizeMode RenderTargetSizeMode = EDreamCanvasRenderTargetSizeMode::RenderTargetFitToCanvas;
	/**
	 * RenderTarget size scale.
	 * Only valid if RenderTargetSizeMode is RenderTargetFitToCanvas.
	 */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (ClampMin = "0.01", EditCondition="RenderTargetSizeMode==EDreamCanvasRenderTargetSizeMode::RenderTargetFitToCanvas"))
		float RenderTargetResolutionScale = 1.0f;
	/**
	 * true- Use custom sort order.
	 * false- Use default sort order management, which is based on hierarchy order.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bOverrideSorting = false;
	/**
	 * Canvas with larger order will render on top of lower one.
	 * NOTE! SortOrder value is stored with int16 type, so valid range is -32768 to 32767
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta=(EditCondition="bOverrideSorting"))
		int16 SortOrder = 0;

	/** Enable/disable normal and tangent in vertex data. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bRequireNormalAndTangent = false;

	/** Default materials, for render default UI elements. */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (DisplayThumbnail = "false"))
	mutable TObjectPtr<UMaterialInterface> DefaultMaterial;

	/** For "World Space - DreamUI Renderer" only, render with blend depth, 0-occlude by scene depth, 1-all visible, 0.5-half transparent. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float BlendDepth = 0.0f;
	/** For "World Space - DreamUI Renderer" only, render with depth fade effect. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", meta = (ClampMin = "0", ClampMax = "10"))
		int DepthFade = 0;
	/**
	 * Create a depth texture so we can do depth test. This is very useful for UIStaticMesh which use Opaque material.
	 * Only valid for ScreenSpaceOverlay and RenderTarget mode.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		bool bEnableDepthTest = false;
	/** For not root canvas, inherit or override parent canvas parameters. */
	UPROPERTY(EditAnywhere, Category = DreamGUI, meta = (Bitmask, BitmaskEnum = "/Script/DreamGUI.EDreamCanvasOverrideParameters"))
		int8 OverrideParameters = 0;

	/**
	 * TraceChannel for line trace of EventSystem interaction.
	 * Only world space UI need this property.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	TEnumAsByte<ETraceTypeQuery> TraceChannel = TraceTypeQuery1;

	/**
	 * Allow drop canvas frame when canvas draw-call take too much time. This may cause some delay for UI response, but can improve performance.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", AdvancedDisplay)
	bool bAllowDropFrame = false;

	/**
	 * DreamCanvas create mesh for render UI elements, this property can give us opportunity to use custom type of mesh for render.
	 * You can set "OwnerNoSee" "CastShadow" properties for your mesh.
	 * @todo: override this property from parent canvas?
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI", AdvancedDisplay, meta = (AllowAbstract = "true"))
		TSubclassOf<UDreamUIMeshComponent> DefaultMeshType;

#pragma region CanvasScaler
	/**
	 * Virtual Camera Projection Type. Deliberately NOT AdvancedDisplay: together with FieldOfView
	 * this defines the projection every widget Perspective scope is calibrated against, and an
	 * orthographic canvas disables Perspective outright (its eye is at infinity, which no affine
	 * remap can reach). An author who cannot find these two cannot calibrate the feature.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler", meta = (DisplayName = "Projection Type"))
	TEnumAsByte<ECameraProjectionMode::Type> ProjectionType = ECameraProjectionMode::Perspective;
	/**
	 * Virtual Camera field of view (in degrees), horizontal. Sets how far back the canvas's eye
	 * stands: distance = Width * 0.5 / tan(FOV/2), the standoff at which the canvas rect exactly
	 * fills the frame. Widening it brings the eye closer and deepens every Perspective scope below.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler", meta = (UIMin = "5.0", UIMax = "170", ClampMin = "0.001", ClampMax = "360.0"))
	float FieldOfView = 60;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler", AdvancedDisplay)
	float NearClipPlane = 1;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler", AdvancedDisplay)
	float FarClipPlane = 10000;
	
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler")
	EDreamCanvasScaleMode ScaleMode = EDreamCanvasScaleMode::ConstantPixelSize;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler")
	FVector2D ReferenceResolution = FVector2D(1280, 720);
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Match"))
	float MatchFromWidthToHeight = 1;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler")
	EDreamCanvasScreenMatchMode ScreenMatchMode = EDreamCanvasScreenMatchMode::MatchWidthOrHeight;
public:
	/**
	 * The canvas-scaler rule as a pure calculation: for a given viewport size, the size this canvas
	 * would give its root widget and the scale it would report. OnViewportParameterChanged applies
	 * this to the live widget; the prefab designer calls it to preview a device resolution that has
	 * no viewport behind it, so preview and runtime cannot drift apart.
	 * Note the returned scale is the canvas's own reported CanvasScale. The scale actually seen on
	 * screen is ViewportSize / OutCanvasSize, which differs at intermediate Match values.
	 */
	void CalculateCanvasSizeAndScale(FIntPoint InViewportSize, FVector2D& OutCanvasSize, float& OutScale);
#if WITH_EDITORONLY_DATA
public:
	/** When Canvas use ScreenSpaceOverlay, in edit mode it will try to match editor viewport's size. So make this true to use a fixed size. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler")
	bool bFixedSizeInEditMode = false;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-CanvasScaler", meta = (EditCondition = "bFixedSizeInEditMode"))
	FIntPoint SizeInEditMode = FIntPoint(1920, 1080);
#endif
private:
	/**
	 * Use this to do custom scale. Only valid if ScaleMode = Custom.
	 * Will fallback to "ConstantPixelSize" if not assign this value.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "DreamGUI-CanvasScaler")
	TObjectPtr<UDreamCanvasCustomScale> CustomScale;
	/** Current viewport size*/
	FIntPoint ViewportSize = FIntPoint(2, 2);
	/**
	 * A viewport size standing in for the real one. Unset means "read the real viewport", which is
	 * every canvas that has not been handed one. See SetViewportSizeOverride.
	 */
	TOptional<FIntPoint> ViewportSizeOverride;
#pragma endregion
	FRenderModeChangedEvent OnRenderModeChanged;
	FRenderTargetChangedEvent OnRenderTargetChanged;

public:
	FORCEINLINE bool GetOverrideDefaultMaterial()const						{ return OverrideParameters & (1 << (int)EDreamCanvasOverrideParameters::DefaultMaterial); }
	FORCEINLINE bool GetOverrideRequireNormalAndTangent()const				{ return OverrideParameters & (1 << (int)EDreamCanvasOverrideParameters::RequireNormalAndTangent); }
	FORCEINLINE bool GetOverrideBlendDepth()const							{ return OverrideParameters & (1 << (int)EDreamCanvasOverrideParameters::BlendDepth); }
	FORCEINLINE bool GetOverrideDepthFade()const							{ return OverrideParameters & (1 << (int)EDreamCanvasOverrideParameters::DepthFade); }

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UMaterialInterface* GetDefaultMaterial()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetDefaultMaterial(UMaterialInterface* InMaterial);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetTraceChannel(TEnumAsByte<ETraceTypeQuery> InTraceChannel);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	TEnumAsByte<ETraceTypeQuery> GetTraceChannel()const { return TraceChannel; }

	/** Set render mode of this canvas. This may not take effect if the canvas is not a root canvas. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetRenderMode(EDreamRenderMode Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetForceRenderToTarget(bool Value);
	/** Set parameters for calculating projection matrix. Only valid for ScreenSpace/RenderTarget mode. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetProjectionParameters(TEnumAsByte<ECameraProjectionMode::Type> InProjectionType, float InFovAngle, float InNearClipPlane, float InFarClipPlane);
	/** if renderMode is RenderTarget, then this will change the renderTarget */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetRenderTarget(UTextureRenderTarget2D* Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetRenderTargetClearColor(FColor Value);
	FRenderModeChangedEvent& GetRenderModeChangedEvent(){return OnRenderModeChanged;}
	FRenderTargetChangedEvent& GetRenderTargetChangedEvent(){return OnRenderTargetChanged;}
	
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetRenderTargetResolutionScale(float Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetRenderTargetSizeMode(EDreamCanvasRenderTargetSizeMode Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetRenderTargetUpdateMode(EDreamCanvasRenderTargetUpdateMode Value);
	/** Only valid when call this on root canvas. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void RequestUpdateForRenderTarget();
	
	/** 
	 * Set DreamCanvas SortOrder
	 * @param	PropagateToChildrenCanvas	if true, set this Canvas's SortOrder and all children Canvas, not just set absolute value, but keep child Canvas's relative order to this one
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetSortOrder(int32 Value, bool PropagateToChildrenCanvas = true);
	/** Set SortOrder to highest, so this canvas will render on top of all canvas that belong to same hierarchy. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetSortOrderToHighestOfHierarchy(bool PropagateToChildrenCanvas = true);
	/** Set SortOrder to lowest, so this canvas will render behind all canvas that belong to same hierarchy. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetSortOrderToLowestOfHierarchy(bool PropagateToChildrenCanvas = true);
	void GetMinMaxSortOrderOfHierarchy(int32& OutMin, int32& OutMax);

	/**
	 * Get actual render mode of this canvas.
	 * Normally canvas's render-mode is inherited from parent canvas.
	 * */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		EDreamRenderMode GetActualRenderMode()const;
	/** Get render mode of this canvas. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		EDreamRenderMode GetRenderMode()const { return RenderMode; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetForceRenderToTarget()const{return bForceRenderToTarget;}
	/**
	 * Hold this canvas's draw-call list as it is, however dirty its contents become.
	 *
	 * What UInvalidationBox does for Slate's cached draw elements, for DreamGUI's equivalent: the
	 * draw-call list. Vertex refreshes still run (a widget that only moves or changes colour keeps
	 * updating); what is suspended is the rebuild that re-batches the subtree. Any rebuild requested
	 * while suspended is remembered and happens the moment it is released, so nothing is lost -- only
	 * deferred. UDreamInvalidationBox drives this; setting it by hand is for code that knows a subtree
	 * is structurally frozen.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetDrawCallRebuildSuspended(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetDrawCallRebuildSuspended()const { return bDrawCallRebuildSuspended; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool GetCullElementsOutsideCanvas()const { return bCullElementsOutsideCanvas; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetCullElementsOutsideCanvas(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetScreenSpaceRenderScale()const { return ScreenSpaceRenderScale; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		void SetScreenSpaceRenderScale(float Value);
	/** Get actual render target of this canvas if actual render mode is RenderTarget. Canvas's render-target is inherited from root canvas. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UTextureRenderTarget2D* GetActualRenderTarget()const;
	/** Get render target of this canvas: the one assigned, or the one it made when none was. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UTextureRenderTarget2D* GetRenderTarget()const { return RenderTarget != nullptr ? RenderTarget.Get() : AutoRenderTarget.Get(); }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	FColor GetRenderTargetClearColor()const{return RenderTargetClearColor;}
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		float GetRenderTargetResolutionScale()const { return RenderTargetResolutionScale; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		EDreamCanvasRenderTargetSizeMode GetRenderTargetSizeMode()const { return RenderTargetSizeMode; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		EDreamCanvasRenderTargetUpdateMode GetRenderTargetUpdateMode()const { return RenderTargetUpdateMode; }

	/** Get actual BlendDepth value of canvas. This property may inherit from parent canvas depend on OverrideParameters property. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		float GetActualBlendDepth()const;
	/** Get blendDepth value of this canvas. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		float GetBlendDepth()const { return BlendDepth; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetBlendDepth(float Value);

	/** Get actual DepthFade value of canvas. This property may inherit from parent canvas depend on OverrideParameters property. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		int GetActualDepthFade()const;
	/** Get blendDepth value of this canvas. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		int GetDepthFade()const { return DepthFade; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetDepthFade(int Value);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		bool GetEnableDepthTest()const { return bEnableDepthTest; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetEnableDepthTest(bool Value);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		bool GetOverrideSorting()const { return bOverrideSorting; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetOverrideSorting(bool Value);
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		int32 GetSortOrder()const { return SortOrder; }
	/** Get actual SortOrder of canvas. This property may inherit from parent canvas depend on OverrideSorting property. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		int32 GetActualSortOrder()const;

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetActualRequireNormalAndTangent()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	bool GetRequireNormalAndTangent()const { return bRequireNormalAndTangent; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	void SetRequireNormalAndTangent(bool Value);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	int GetDrawCallCount()const;

	/** Override DreamUI's screen space UI render's camera location. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetOverrideViewLocation(bool Override, FVector Value);
	/** Override DreamUI's screen space UI render's camera rotation. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetOverrideViewRotation(bool Override, FRotator Value);
	/**
	 * Override DreamUI's screen space UI render's camera's fov in degree, will affect projection matrix.
	 * If SetOverrideProjectionMatrix is true, then this will not take effect.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetOverrideFovAngle(bool Override, float Value);
	/**
	 * Override DreamUI's screen space UI render's camera's projection matrix.
	 * If this is set to true, then SetOverrideFovAngle will not take effect.
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetOverrideProjectionMatrix(bool Override, FMatrix Value);

	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		TSubclassOf<UDreamUIMeshComponent> GetDefaultMeshType()const { return DefaultMeshType; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetDefaultMeshType(TSubclassOf<UDreamUIMeshComponent> InValue);

#pragma region CanvasScaler
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	TEnumAsByte<ECameraProjectionMode::Type> GetProjectionType()const { return ProjectionType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	float GetFieldOfView()const { return FieldOfView; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	float GetNearClipPlane()const { return NearClipPlane; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	float GetFarClipPlane()const { return FarClipPlane; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetProjectionType(TEnumAsByte<ECameraProjectionMode::Type> Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetFieldOfView(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetNearClipPlane(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetFarClipPlane(float Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	EDreamCanvasScaleMode GetScaleMode() { return ScaleMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	FVector2D GetReferenceResolution() { return ReferenceResolution; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	float GetMatchFromWidthToHeight() { return MatchFromWidthToHeight; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	EDreamCanvasScreenMatchMode GetScreenMatchMode() { return ScreenMatchMode; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	UDreamCanvasCustomScale* GetCustomScale()const { return CustomScale; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetScaleMode(EDreamCanvasScaleMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetReferenceResolution(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetMatchFromWidthToHeight(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetScreenMatchMode(EDreamCanvasScreenMatchMode Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-CanvasScaler")
	void SetCustomScale(UDreamCanvasCustomScale* Value);

	/**
	 * Convert position from viewport to DreamCanvas space.
	 * @param InPosition The point's pixel position on viewport.
	 * @param Result DreamCanvas space position, left bottom is zero point.
	 * @return convert will fail if this DreamCanvas is not root canvas
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-CanvasScaler")
	bool ConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result)const;
	/**
	 * Convert position from DreamCanvas space to viewport.
	 * @param InPosition The point's position in DreamCanvas space.
	 * @param Result in viewport, pixel unit, left top is zero point.
	 * @return convert will fail if this DreamCanvas is not root canvas
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-CanvasScaler")
	bool ConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result)const;
	/**
	 * Project 3D screen-space-UI element's position to 2D screen-space-UI.
	 * NOTE!!! This is only for screen-space-UI, DON'T use this for convert world space position!!!
	 * @param	Position3D	GetWorldLocation from the UI element (world location).
	 * @param	OutPosition2D	2D Position in screen-space, left bottom is zero point.
	 * @return 	convert will fail if this DreamCanvas is not root canvas.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-CanvasScaler")
	bool Project3DToScreen(const FVector& Position3D, FVector2D& OutPosition2D)const;
	/**
	 * Where a world point lands in the shipped image, given back as a world point lying ON this
	 * canvas's own plane. Projecting through the canvas's view-projection and then placing the
	 * result back on the plane turns "where will this appear" into an ordinary scene point, which
	 * any camera -- including an orthographic editor viewport that has no perspective divide of its
	 * own -- can then draw. A point already in the plane maps to itself, so this is silent for flat
	 * content and only says something where there is depth. False if the point is at or behind the
	 * canvas's eye, or the canvas has no sized widget.
	 */
	bool ProjectWorldPointOntoCanvasPlane(const FVector& InWorldPoint, FVector& OutWorldOnPlane)const;
	/**
	 * Project 3D world position to 2D screen-space-UI position with specific player's camera.
	 * This function need player pawn contains a camera component, and use this camera to do projection, so it can be used for world space UI.
	 * @param Player 
	 * @param InPosition 
	 * @param OutPosition2D 
	 * @return 
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-CanvasScaler")
	static bool ProjectWorldToScreenWithPlayerCamera(APlayerController* Player, class UCameraComponent* PlayerCamera, const FVector& InPosition, FVector2D& OutPosition2D);
	UFUNCTION(BlueprintPure, Category = "DreamGUI-CanvasScaler")
	static bool BuildViewProjectionMatrixForPlayerCamera(APlayerController* Player, class UCameraComponent* PlayerCamera, FMatrix& OutViewProjectionMatrix);
	UFUNCTION(BlueprintPure, Category = "DreamGUI-CanvasScaler")
	static bool ProjectWorldToScreenWithViewProjectionMatrix(const FMatrix& InViewProjectionMatrix, const FVector2D& InViewportSize, const FVector& InPosition, FVector2D& OutPosition2D);
	
private:
#if WITH_EDITOR
	FDelegateHandle EditorTickDelegateHandle;
	void DrawVirtualCamera();
	void DrawViewportArea();
	void OnEditorTick(float DeltaTime);
#endif
	void RegisterCanvasScaler();
	void UnregisterCanvasScaler();
	void OnViewportParameterChanged();
	void CheckAndApplyViewportParameter();
	FDelegateHandle ViewportResizeDelegateHandle;
#pragma endregion

public:
	void MarkVisualWillChange(UDreamVisual* InOldVisual);
	void RegisterVisual(UDreamVisual* InVisual);
	void UnregisterVisual(UDreamVisual* InVisual);

	void AddDreamWidget(UDreamWidget* InWidget);
	void RemoveDreamWidget(UDreamWidget* InWidget);
	/**
	 * InWidget came into this canvas, left it, or moved within it: the widget list is made again. The widgets already in
	 * it keep their order, so only a widget that is in the canvas now asks for an update; what one that left drew goes
	 * with the rebuild, whose prepare leaves out what is no longer in the list.
	 */
	void MarkWidgetCameOrWent(UDreamWidget* InWidget);
	/** return all DreamWidget that belongs to this canvas. */
	const TArray<UDreamVisual*>& GetVisualArray()const { return VisualList; }
	const TArray<UDreamWidget*>& GetWidgetArray()const { return WidgetList; }

	UDreamUIMeshComponent* GetUIMesh()const { CheckUIMesh(); return UIMesh.Get(); }
public:
	static FName DreamUI_MainTextureMaterialParameterName;
	static FName DreamUI_FontTextureMaterialParameterName;
	/** xy: atlas slice size in texels, z: field range in texels, w: texels per em (MF_DreamUI_Shade). */
	static FName DreamUI_FontAtlasInfoMaterialParameterName;
	/** The font atlas geometry a draw call's glyphs decode with (see DreamUIShade.ush's FontAtlasInfo). */
	static FVector4f MakeFontAtlasInfo(const class FDreamUIDrawCall& DrawCallItem);
	static FName DreamUI_ClipDataTexture_MaterialParameterName;
	static FName DreamUI_WidgetPropertyDataTexture_MaterialParameterName;
	static FName DreamUI_IsRenderByDreamUIRenderer_MaterialParameterName;
	static bool IsMaterialContainsDreamUIParameter(const UMaterialInterface* InMaterial);
private:
	void SetSortOrderAdditionalValueRecursive(int32 InAdditionalValue);
	void UpdateRenderTarget(bool CallEvent);
	void CheckRenderTargetUpdate();
public:
	/** Called from DreamUIManagerActor. Update this canvas if it is a RootCanvas */
	void UpdateRootCanvas();
	/** TakeDrawCallBatchData, RefreshDrawCallVertices and FinishDrawCallBatchData for this canvas and its children, in turn. */
	void UpdateDrawCallBatchData();
	/**
	 * UpdateDrawCallBatchData in its parts, for a caller that refreshes many canvases at once (SubmitCanvasDrawCall).
	 *
	 * Take, on the game thread: waits for this canvas's and its children's batching and vertex transforms, puts new draw
	 * calls in place, and adds to OutToRefresh each of them whose vertices are due a refresh and to OutToFinish each of
	 * them with a mesh, children first.
	 */
	void TakeDrawCallBatchData(TArray<UDreamCanvas*>& OutToRefresh, TArray<UDreamCanvas*>& OutToFinish);
	/**
	 * The refresh a canvas Take listed: the moved elements' vertices into their draw calls, and the sections patched with
	 * them. Any thread, and several canvases at once: it touches nothing but this canvas's draw calls, the visuals they
	 * hold and its mesh's sections, and sends no render command. A section that cannot be patched is left to Finish.
	 */
	void RefreshDrawCallVertices();
	/** On the game thread, after the refresh: what it left -- sections to rebuild, the mesh's bounds -- and the mesh's render commands. */
	void FinishDrawCallBatchData();
	/**  */
	void MarkNeedVerifyMaterials();
private:
	uint32 bCanTickUpdate : 1 = true;//if Canvas can update from tick
	uint32 bShouldRebuildDrawCall : 1 = true;
	/** See SetDrawCallRebuildSuspended. The request above is kept, not dropped, while this is set. */
	uint32 bDrawCallRebuildSuspended : 1 = false;
	/**
	 * A vertex refresh asked for -- a colour, an alpha, nothing moved -- and not yet carried out. It stays until the
	 * draw calls in hand take it (UpdateDrawCallBatchData) or a rebuild prepares what it asked for.
	 */
	uint32 bHasPendingUpdateData : 1 = false;
	uint32 bNeedToSortRenderPriority : 1 = true;
	uint32 bHasAddToDreamScreenSpaceRenderer : 1 = false;//is this canvas added to DreamGUI screen space renderer
	uint32 bRequestUpdateForRenderTarget : 1 = true;//request update when RenderTargetUpdateMode is WhenRequest
	uint32 bAnythingChangedForRenderTarget : 1 = true;//if children canvas anything changed, then mark this property for root canvas, good for RenderTarget mode to update
	uint32 bPrevAnythingChangedForRenderTarget : 1 = true;//same as upper one, but the prev frame
	uint32 bHasSetInitialStateForDreamWorldSpaceRenderer : 1 = false;//is DreamGUI world space renderer's initial state set
	uint32 bNeedToVerifyMaterials : 1 = true;
	uint32 bNeedToGenerateWidgetList : 1 = true;

	uint32 bPrevIsVisible : 1 = true;//is DreamWidget active in prev frame?

	uint32 bOverrideViewLocation:1=false, bOverrideViewRotation:1=false, bOverrideProjectionMatrix:1=false, bOverrideFovAngle:1=false;

	mutable uint32 bUIMeshNeedToSetInitialParameters : 1 = true;//after clear UIMesh, it will need to set initial parameters to use again
	mutable uint32 bIsViewProjectionMatrixDirty : 1 = true;
	mutable FMatrix CacheViewProjectionMatrix = FMatrix::Identity;//cache to prevent multiple calculation in same frame
	friend class FDreamCanvasHierarchyOrderTest;
	friend class FDreamCanvasVisualChangeRebuildsDrawCallTest;
	friend class FDreamCanvasSuspendedRebuildKeepsTheRequestTest;
	/**
	 * RenderMode can affect UI's renderer, basically WorldSpace use UE's built-in renderer, others use DreamGUI's renderer. Different renderers cannot share same render data.
	 * eg: when attach to other canvas, this will tell which render mode in old canvas, and if not compatible then recreate render data.
	 */
	EDreamRenderMode CurrentRenderMode = EDreamRenderMode::None;
	FORCEINLINE bool RenderModeIsDreamRendererOrUERenderer(EDreamRenderMode InRenderMode)const
	{
		if (bForceRenderToTarget)return true;
		return 
			InRenderMode != EDreamRenderMode::WorldSpace
			;
	}

	FVector OverrideViewLocation = FVector::ZeroVector;
	FRotator OverrideViewRotation = FRotator::ZeroRotator;
	float OverrideFovAngle = 0;
	FMatrix OverrideProjectionMatrix = FMatrix::Identity;

	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	mutable TObjectPtr<UDreamUIMeshComponent> UIMesh;//current using UIMesh.
	/**
	 * The proxies the canvas draws its materials through, per source material: DreamGUI answers the parameters it gives a
	 * material in the material's place (FDreamUIMaterialProxy), and a proxy is taken back rebuild after rebuild.
	 */
	struct FMaterialProxyPool
	{
		TArray<TSharedPtr<FDreamUIMaterialProxy, ESPMode::ThreadSafe>> Proxies;
		int32 CurrentIndex = 0;
		/** Consecutive rebuilds the tail of Proxies went unused; past the decay window it is let go of. */
		int32 UnusedStreak = 0;
	};
	TMap<TObjectKey<UMaterialInterface>, FMaterialProxyPool> MaterialProxyPools;
	/** What those proxies point at -- their sources and the textures they answer with -- kept from the collector. */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TArray<TObjectPtr<UObject>> MaterialProxyReferences;
	uint64 NewestDrawCallFrameNumber = 0;
	FDreamCanvasPendingDrawCallData CurrentDrawCallData;//current drawing draw-call
	TUniquePtr<FDreamCanvasDrawCallProcessingRunnable> DrawCallProcessingRunnable;
	TUniquePtr<FDreamCanvasAsyncFunctionRunnable> TransformVerticesAsyncFunctionRunnable;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamVisual>> VisualList;//Use DreamWidget instead of DreamVisual, because we need DreamWidget to get sub-canvas.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UDreamWidget>> WidgetList;//All DreamWidget that belongs to this canvas
	TSharedPtr<FDreamUIDrawCall> DrawCallAsChildCanvas = nullptr;//DrawCall that represent this canvas when the canvas is render as child.

	UPROPERTY(Transient)
	TWeakObjectPtr<USceneComponent> AttachedRootSceneComponent = nullptr;
	/** Binding on AttachedRootSceneComponent->TransformUpdated while attached. */
	FDelegateHandle AttachedRootSceneComponentTransformHandle;
	void OnAttachedRootSceneComponentTransformUpdated(USceneComponent* UpdatedComponent, EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport);
	
	//clip data is stored in root canvas
	TArray<TSharedPtr<FDreamUIClipData>> ClipDataList;
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient, VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	TObjectPtr<UDreamUIDataAsTexture> ClipDataAsTexture;//clip coordinate stored in UV1.x
	//widget property data is stored in each canvas (not only root canvas)
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient, VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	TObjectPtr<UDreamUIDataAsTexture> WidgetPropertyDataAsTexture;//widget properties coordinate stored in UV1.y
	void CheckWidgetPropertyData();
public:
	void PushAsyncFunction_TransformVertices(TFunction<void()> InFunction);
	/** Called by DreamWidget to delete clip data */
	void RemoveClipData(const TSharedPtr<FDreamUIClipData>& InClipData);
	UTexture* GetClipDataTexture()const;
	UDreamUIDataAsTexture* GetWidgetPropertyDataAsTexture()const{return WidgetPropertyDataAsTexture;}
	
	const TArray<TWeakObjectPtr<UDreamCanvas>>& GetChildrenCanvasArray()const{return ChildrenCanvasArray;}
	
	static FTransform2D ConvertTo2DTransform(const FTransform& Transform);
	static void CalculateVisual2DBounds(UDreamVisual* Visual, const FTransform2D& OutTransform2D, FVector2D& OutMin, FVector2D& OutMax);
	/** Same, from the visual's local bounds as plain data -- safe to call off the game thread. */
	static void CalculateVisual2DBounds(const FVector2D& InLocalMin, const FVector2D& InLocalMax, const FTransform2D& OutTransform2D, FVector2D& OutMin, FVector2D& OutMax);
private:

	/** canvas array belong to this canvas in hierarchy. */
	UPROPERTY(Transient) TArray<TWeakObjectPtr<UDreamCanvas>> ChildrenCanvasArray;
	/** update Canvas's draw-call */
	void UpdateCanvasDrawCall();
	/** mark render finish */
	void MarkFinishUpdateCanvasDrawCall();
public:
	/**
	 * Recompute and upload every clip rectangle owned by this canvas hierarchy. No-op on non-root canvases,
	 * which share the root's ClipDataList. Driven once per tick by UDreamUIManagerWorldSubsystem after layout.
	 */
	void RefreshAllClipData();
	/**
	 * The canvas whose SortDrawCall covers this canvas's sections: the nearest override-sorting
	 * ancestor, or the root. Sort requests must land here — a plain child canvas never sorts.
	 */
	UDreamCanvas* GetSortOwnerCanvas();
	/**
	 * Execute a pending render-priority sort if this canvas owns sorting (root or override-sorting).
	 * Driven once per tick by UDreamUIManagerWorldSubsystem after draw-call updates, so requests raised
	 * outside a rebuild (SetSortOrder at runtime, a child canvas rebuilding alone) take effect the
	 * same frame instead of waiting for the owner's next incidental rebuild.
	 */
	void ConsumePendingRenderPrioritySort();
public:
	/**
	 * A widget of this canvas asks for an update: MarkCanvasUpdate, with the widget named. When only widgets asked, the
	 * update looks at those widgets alone, where it looks at every widget of the canvas otherwise.
	 */
	void MarkWidgetUpdate(UDreamWidget* InWidget, bool bRebuildDrawCall);
	/**
	 * A widget of this canvas moved, and nothing else about it changed. Its draw calls are rebuilt only when the move
	 * could change how its canvas's elements batch (CanRefreshDrawCallsInPlace); otherwise the draw calls in hand take
	 * the moved vertices and bounds, as they take a colour.
	 */
	void MarkWidgetMoved(UDreamWidget* InWidget);
	/** InWidget's own render transform changed: what tells the canvas which widgets to keep as render layers. */
	void NoteRenderTransformChanged(UDreamWidget* InWidget);
	/**
	 * A render layer's transform relative to this canvas changed (UDreamWidget::IsRenderLayer): its sections move, and
	 * nothing in the layer is transformed again.
	 */
	void MarkRenderLayerMoved(UDreamWidget* InLayer);
private:
	/** Set by MarkWidgetMoved; the next update decides whether the moves need a rebuild. */
	bool bWidgetsMovedSinceUpdate = false;
	/** What RefreshDrawCallVertices leaves to FinishDrawCallBatchData: whether a draw call's bounds moved, and the draw calls whose section it could not patch. */
	bool bRefreshMovedBounds = false;
	TArray<int32, TInlineAllocator<4>> DrawCallsLeftToUpdate;
	/**
	 * Whether the draw calls in hand are what a batch of the elements as they are now would make: the last batch did not
	 * depend on positions (FDreamUIBatchPlacement), no rebuild is on its way, and every element still has its vertex
	 * count, is still flat or not as it was, and is still inside or outside the canvas rect as it was. Waits for the
	 * vertex transforms under way, since it reads the geometries they write.
	 */
	bool CanRefreshDrawCallsInPlace();
	/** Whether the batching leaves out flat elements outside this canvas's rect: only where the rect is the surface drawn. */
	bool CullsElementsOutsideItsRect() const { return bCullElementsOutsideCanvas && (IsRootCanvas() || bForceRenderToTarget); }
	/**
	 * The widgets that asked for an update since the canvas last updated. The update looks at them alone -- in the
	 * order of WidgetList, each after its parents' clips -- unless something else woke the canvas as well: the canvas
	 * itself, a new widget list, a caller that named no widget (bUpdateEveryWidget).
	 */
	TArray<TWeakObjectPtr<UDreamWidget>> WidgetsToUpdate;
	/** The widgets an update is looking at, swapped out of WidgetsToUpdate so that neither array gives up its memory. */
	TArray<TWeakObjectPtr<UDreamWidget>> WidgetsBeingUpdated;
	/** Something other than a widget woke the canvas: the next update looks at every widget. */
	bool bUpdateEveryWidget = true;
	/**
	 * Each widget's place in WidgetList: the order the widgets that asked are looked at in, and the order the prepare's
	 * entries are merged in. Made when it is first needed after the list was made (bWidgetListIndexValid).
	 */
	TMap<TObjectKey<UDreamWidget>, int32> WidgetListIndex;
	bool bWidgetListIndexValid = false;
	void EnsureWidgetListIndex();
	/**
	 * InAsking in list order, once each. False when one of them is not in the list and the list is behind, not when it
	 * went: one that left the canvas since the list was last made has nothing to look at, and what it drew goes with the
	 * prepare that merges the new list (bWidgetListChangedSincePrepare).
	 */
	bool GatherWidgetsToUpdateInListOrder(const TArray<TWeakObjectPtr<UDreamWidget>>& InAsking, TArray<UDreamWidget*, TInlineAllocator<16>>& OutWidgets);
	/**
	 * What the last prepare made, kept: when only the widgets in WidgetsToPrepare changed since -- asked, came, moved --
	 * the next prepare keeps every other widget's entry and makes theirs again (MergePreparedDataCache), instead of
	 * walking every widget.
	 */
	TArray<FDreamUIRenderData> PreparedDataCache;
	/** The widgets looked at since the last prepare; bPrepareEveryWidget when every widget was. */
	TArray<TWeakObjectPtr<UDreamWidget>> WidgetsToPrepare;
	bool bPrepareEveryWidget = true;
	bool bPreparedDataCacheValid = false;
	/** The widget list was made again since the last prepare: widgets came, went or moved. */
	bool bWidgetListChangedSincePrepare = false;
	/**
	 * PreparedDataCache made over: each widget that stayed where it was keeps its entry, in the list's order, and each
	 * widget in WidgetsToPrepare has its made again. False when only a full prepare can say what the order is; the cache
	 * is then made again from nothing.
	 */
	bool MergePreparedDataCache();
	/** What a prepare makes of InWidget -- nothing, or its one entry -- appended to OutRenderDataArray. */
	void AppendRenderDataOf(UDreamWidget* InWidget, TArray<FDreamUIRenderData>& OutRenderDataArray);
	/** r.DreamUI.VerifyPartialPrepare: InPrepared, made from the last prepare, against a prepare of every widget now. */
	void VerifyPartialPrepare(const TArray<FDreamUIRenderData>& InPrepared);

	void PrepareDrawCallBatchingData(TArray<FDreamUIRenderData>& OutRenderDataArray);
	void UpdateDrawCallMesh();
	void UpdateDrawCallMaterial();
	void SortDrawCall();
public:
	/**
	 * @param bCullElementsOutsideCanvasRect	Leave out flat elements whose canvas-space bounds do not
	 *		touch the canvas rect. Only pass true when this canvas's rect IS the surface being drawn --
	 *		a root canvas, or one rendering to its own target. A plain child canvas draws into its
	 *		parent's surface, and a canvas is not a clipper, so its rect says nothing about visibility.
	 */
	static void BatchDrawCallAsync(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop, const TArray<FDreamUIRenderData>& InRenderDataArray, TArray<FDreamUIDrawCall>& InOutUIDrawCallList, bool bCullElementsOutsideCanvasRect = false
		, FDreamUIBatchPlacement* OutPlacement = nullptr);
	/**
	 * The same, using up InRenderDataArray: each element's prepared geometry goes into its draw call as it is.
	 * @param OutPlacement	When given, what the batch depended on of where the elements are: see FDreamUIBatchPlacement.
	 */
	static void BatchDrawCallAsync(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop, TArray<FDreamUIRenderData>&& InRenderDataArray, TArray<FDreamUIDrawCall>& InOutUIDrawCallList, bool bCullElementsOutsideCanvasRect = false
		, const TArray<TArray<TSharedPtr<const FDreamUIGeometry>>>* InGeometryListsOnSections = nullptr, FDreamUIBatchPlacement* OutPlacement = nullptr);
	static bool Is2DUITransform(const FTransform& Transform);
private:
	void CheckUIMesh()const;
};
