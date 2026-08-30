// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "Camera/CameraTypes.h"
#include "Core/DreamCanvasAsyncFunctionRunnable.h"
#include "Core/DreamCanvasDrawCallProcessingRunnable.h"
#include "Core/DreamCanvasProcessingDrawCallData.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUIDrawCall.h"
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

USTRUCT()
struct FDreamCanvasDynamicMaterialArrayContainer
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = DreamGUI)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> MaterialArray;

	int CurrentIndex = 0;
	/** Consecutive frames the tail of MaterialArray went unused; past the decay window it is trimmed. */
	int UnusedStreak = 0;
};

USTRUCT()
struct FDreamCanvasMaterialParameterCache
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, Category=DreamGUI)
	TWeakObjectPtr<UTexture> Texture = nullptr;
	UPROPERTY(VisibleAnywhere, Category=DreamGUI)
	TWeakObjectPtr<UTexture> FontTexture = nullptr;
};

class UDreamWidget;
class UDreamVisual;
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
class DREAMGUI_API UDreamCanvas : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:	
	UDreamCanvas();
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
public:
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
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		EDreamRenderMode RenderMode = EDreamRenderMode::WorldSpace;
	/**
	 * Render to RenderTarget, if not specified then DreamGUI will create a new one.
	 */
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
		TObjectPtr<UTextureRenderTarget2D> RenderTarget;
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
	/** Get actual render target of this canvas if actual render mode is RenderTarget. Canvas's render-target is inherited from root canvas. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UTextureRenderTarget2D* GetActualRenderTarget()const;
	/** Get render target of this canvas. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UTextureRenderTarget2D* GetRenderTarget()const { return RenderTarget; }
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
	void UpdateDrawCallBatchData();
	/**  */
	void MarkNeedVerifyMaterials();
private:
	uint32 bCanTickUpdate : 1 = true;//if Canvas can update from tick
	uint32 bShouldRebuildDrawCall : 1 = true;
	uint32 bHasPendingUpdateData : 1 = false;
	uint32 bNeedToSortRenderPriority : 1 = true;
	uint32 bHasAddToDreamScreenSpaceRenderer : 1 = false;//is this canvas added to DreamGUI screen space renderer
	uint32 bRequestUpdateForRenderTarget : 1 = true;//request update when RenderTargetUpdateMode is WhenRequest
	uint32 bAnythingChangedForRenderTarget : 1 = true;//if children canvas anything changed, then mark this property for root canvas, good for RenderTarget mode to update
	uint32 bPrevAnythingChangedForRenderTarget : 1 = true;//same as upper one, but the prev frame
	uint32 bHasSetInitialStateForDreamWorldSpaceRenderer : 1 = false;//is DreamGUI world space renderer's initial state set
	uint32 bNeedToVerifyMaterials : 1 = true;
	mutable uint32 bNeedToSetClipDataTextureMaterialParameter : 1 = true;
	uint32 bNeedToGenerateWidgetList : 1 = true;
	uint32 bWidgetPropertyDataAsTextureChanged : 1 = true;
	uint32 bClipDataAsTextureChanged : 1 = true;

	uint32 bPrevIsVisible : 1 = true;//is DreamWidget active in prev frame?

	uint32 bOverrideViewLocation:1=false, bOverrideViewRotation:1=false, bOverrideProjectionMatrix:1=false, bOverrideFovAngle:1=false;

	mutable uint32 bUIMeshNeedToSetInitialParameters : 1 = true;//after clear UIMesh, it will need to set initial parameters to use again
	mutable uint32 bIsViewProjectionMatrixDirty : 1 = true;
	mutable FMatrix CacheViewProjectionMatrix = FMatrix::Identity;//cache to prevent multiple calculation in same frame
	mutable float LastRenderTime = 0;
	friend class FDreamUIRenderSceneProxy;
	friend class FDreamCanvasHierarchyOrderTest;
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

	UPROPERTY(Transient)
	mutable TObjectPtr<UDreamUIMeshComponent> UIMesh;//current using UIMesh.
	//DefaultMaterial created MaterialInstanceDynamic pool 
	UPROPERTY(Transient, VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PooledDefaultMaterialList;
	//Currently using material inside PooledDefaultMaterialList from this start index to end
	UPROPERTY(Transient, VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	int UsingMaterialStartIndex = 0;
	UPROPERTY(Transient, VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	TMap<TObjectPtr<UMaterialInterface>, FDreamCanvasDynamicMaterialArrayContainer> MapSrcMatToDynamicMat;//trimmed by the decay pass in UpdateDrawCallMaterial once a tail sits idle a whole window
	UPROPERTY(Transient, VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	TMap<TObjectPtr<UMaterialInterface>, FDreamCanvasMaterialParameterCache> MapMatToParamCache;
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
	UPROPERTY(Transient, VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	TObjectPtr<UDreamUIDataAsTexture> ClipDataAsTexture;//clip coordinate stored in UV1.x
	void OnClipDataTextureChanged(UTexture* NewTexture);
	//widget property data is stored in each canvas (not only root canvas)
	UPROPERTY(Transient, VisibleAnywhere, Category = "DreamGUI", AdvancedDisplay)
	TObjectPtr<UDreamUIDataAsTexture> WidgetPropertyDataAsTexture;//widget properties coordinate stored in UV1.y
	void OnWidgetPropertyDataTextureChanged(UTexture* NewTexture);
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
private:

	void PrepareDrawCallBatchingData(TArray<FDreamUIRenderData>& OutRenderDataArray);
	void UpdateDrawCallMesh();
	void UpdateDrawCallMaterial();
	void SortDrawCall();
public:
	static void BatchDrawCallAsync(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop, const TArray<FDreamUIRenderData>& InRenderDataArray, TArray<FDreamUIDrawCall>& InOutUIDrawCallList);
	static bool Is2DUITransform(const FTransform& Transform);
private:
	void CheckUIMesh()const;
};
