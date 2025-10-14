// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Layout/Margin.h"
#include "Components/ActorComponent.h"
#include "Camera/CameraTypes.h"
#include "Math/TransformCalculus2D.h"
#include "PrefabSystem/ILGUIPrefabInterface.h"
#include "LexCanvas.generated.h"

class FLexUIClipData;
class ULexUIDataAsTexture;

UENUM(BlueprintType, Category = LGUI)
enum class ELexRenderMode :uint8
{
	/**
	 * Render in screen space. If there are multiple screen-space-ui-root in world, they will be sorted by SortOrder property.
	 * This mode use LexUI's custom render pipeline.
	 * This mode need a LexCanvasScaler to control the size and scale.
	 */
	ScreenSpaceOverlay = 0,
	/**
	 * Render in world space by UE default render pipeline.
	 * This mode use engine's default render pipeline, so post process will affect ui.
	 */
	WorldSpace=1			UMETA(DisplayName = "World Space - UE Renderer"),
	/**
	 * Render in world space by LexUI's custom render pipeline, 
	 * This mode use LexUI's custom render pipeline, will not be affected by post process.
	 */
	WorldSpace_LexUI = 3		UMETA(DisplayName = "World Space - LexUI Renderer"),
	/**
	 * Render to a custom render target.
	 */
	RenderTarget = 2		UMETA(DisplayName = "Render Target"),
	
	None = 255				UMETA(Hidden),
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexCanvasRenderTargetSizeMode : uint8
{
	None,
	/** Change LexCanvas's size to fit RenderTarget. */
	CanvasFitToRenderTarget,
	/** Change RenderTarget's size to fit LexCanvas. */
	RenderTargetFitToCanvas,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexCanvasRenderTargetUpdateMode : uint8
{
	/** LexUI will automatically manage update, only draw to RenderTarget when it detect something change. */
	Automatic,
	/** Always draw to RenderTarget every frame. */
	Always,
	/** Only draw to RenderTarget when call RequestUpdateForRenderTarget. */
	WhenRequest,
};

UENUM(BlueprintType, meta = (Bitflags), Category = LGUI)
enum class ELexCanvasOverrideParameters :uint8
{
	DefaultMaterial,
	DynamicPixelsPerUnit,
	RequireNormalAndTangent,
	BlendDepth,
	DepthFade,
};
ENUM_CLASS_FLAGS(ELexCanvasOverrideParameters);

UENUM(BlueprintType, Category = LGUI)
enum class ELexCanvasScaleMode:uint8
{
	/** 1 unit is 1 pixel render in screen*/
	ConstantPixelSize,
	/** scale UI with reference resolution and screen resolution*/
	ScaleWithScreenSize,
	/**
	 * Assign CustomScale parameter to use a custom class calculate resolution and scale.
	 */
	Custom,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexCanvasScreenMatchMode :uint8
{
	/** Use "MatchFromWidthToHeight" and "ReferenceResolution" properties to control size and scale UI*/
	MatchWidthOrHeight,
	/** If viewport's aspect ratio not match "ReferenceResolution"'s aspect ratio, then expand size and scale UI*/
	Expand,
	/** if viewport's aspect ratio not match "ReferenceResolution"'s aspect ratio, then shrink size and scale UI*/
	Shrink,
};

class ULexCanvas;

UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexCanvasCustomScale: public UObject
{
	GENERATED_BODY()
public:
	/** Initialize, called when LexCanvas Awake. */
	virtual void Init(ULexCanvas* InCanvas);
	/** Called when LexCanvas calculate viewport size and scale. */
	virtual void CalculateSizeAndScale(ULexCanvas* InCanvas, const FIntPoint& InViewportSize, FIntPoint& OutLexCanvasSize, float& OutScale);
	/**
	 * Convert position from viewport to LexCanvas space.
	 * @param InPosition The point's pixel position on viewport.
	 * @param Result LexCanvas space position, left bottom is zero point.
	 * @return convert will fail if this LexCanvas is not root canvas
	 */
	virtual bool ConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result)const;
	/**
	 * Convert position from LexCanvas space to viewport.
	 * @param InPosition The point's position in LexCanvas space.
	 * @param Result in viewport, pixel unit, left top is zero point.
	 * @return convert will fail if this LexCanvas is not root canvas
	 */
	virtual bool ConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result)const;
protected:
	/** Initialize, called when LexCanvas Awake. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "Init"), Category = "LGUI")
	void ReceiveInit(ULexCanvas* InCanvas);
	/** Called when LexCanvas calculate viewport size and scale. */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "CalculateSizeAndScale"), Category = "LGUI")
	void ReceiveCalculateSizeAndScale(ULexCanvas* InCanvas, const FIntPoint& InViewportSize, FIntPoint& OutLexCanvasSize, float& OutScale);
	/**
	 * Convert position from viewport to LexCanvas space.
	 * @param InPosition The point's pixel position on viewport.
	 * @param Result LexCanvas space position, left bottom is zero point.
	 * @return convert will fail if this LexCanvas is not root canvas
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ConvertPositionFromViewportToCanvas"), Category = "LGUI")
	bool ReceiveConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result)const;
	/**
	 * Convert position from LexCanvas space to viewport.
	 * @param InPosition The point's position in LexCanvas space.
	 * @param Result in viewport, pixel unit, left top is zero point.
	 * @return convert will fail if this LexCanvas is not root canvas
	 */
	UFUNCTION(BlueprintImplementableEvent, meta = (DisplayName = "ConvertPositionFromCanvasToViewport"), Category = "LGUI")
	bool ReceiveConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result)const;
};

class ULexWidget;
class ULexVisual;
class ULexVisualBatchMesh;
class ULexVisualDirectMesh;
class ULexUIMeshComponent;
class FLexUIDrawCall;
class FLexVisualPostProcessRenderProxy;
class UTextureRenderTarget2D;

/**
 * Canvas is for render and update all UI elements.
 */
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexCanvas : public UActorComponent, public ILGUIPrefabInterface
{
	GENERATED_BODY()

public:	
	ULexCanvas();
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction ) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason)override;
	//begin LGUIPrefabInterface
	virtual void Awake_Implementation() override;
	virtual void EditorAwake_Implementation() override;
	//end LGUIPrefabInterface
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
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy)override;

	static const FName GetPropertyName_TraceChannel()
	{
		return GET_MEMBER_NAME_CHECKED(ULexCanvas, TraceChannel);
	}
private:
	/** clear draw-calls */
	void ClearDrawCall();
	void RemoveFromViewExtension(bool PropagateToChildrenCanvas);
	TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> RenderTargetViewExtension = nullptr;
	TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> GetRenderTargetViewExtension();
public:
	/** mark canvas layout dirty */
	void MarkSizeChanged();
	/**
	 * Mark update this Canvas. Canvas don't need to update every frame, only update when need to.
	 * Some rules if update could trigger draw-call's rebuild:
	 *		1. Commonly material & texture change and UI item's active state change
	 *		2. Transform & vertex position change, draw-call could overlap with each other
	 *		3. Hierarchy order change, this is directly related to render order
	 * And about draw-call's rebuild, it's not actually force rebuild, it will check and reuse prev draw-call if possible.
	 * @param	bMaterialOrTextureChanged	Material or texture change
	 * @param	bTransformOrVertexPositionChanged	UI element's transform change, or vertex position change
	 * @param	bHierarchyOrderChanged	UI element's hierarchy order change
	 * @param	bForceRebuildDrawCall	Mark it rebuild no matter what parameter change.
	 */
	void MarkCanvasUpdate(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall = false);
	void MarkCanvasUpdateRecursive(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall = false);

	static void BuildProjectionMatrix(FIntPoint InViewportSize, ECameraProjectionMode::Type InProjectionType, float FOV, float FarClipPlane, float NearClipPlane, FMatrix& OutProjectionMatrix);
	FMatrix GetViewProjectionMatrix()const;
	FMatrix GetProjectionMatrix()const;
	FVector GetViewLocation()const;
	FRotator GetViewRotator()const;
	FIntPoint GetViewportSize()const;
	/** get scale value of canvas. only valid for root canvas. */
	FORCEINLINE float GetCanvasScale()const { return CanvasScale; }
private:
	friend class ULexCanvasScaler;
	float CanvasScale = 1.0f;//for screen space UI, screen size / root canvas size

	/** hierarchy changed */
	void OnUIHierarchyAttachmentChanged();
	void OnWidgetActiveChanged(bool WidgetActive);
public:
	/** get root canvas on hierarchy */
	UFUNCTION(BlueprintCallable, Category = LGUI)
	ULexCanvas* GetRootCanvas()const;
	/** is this the root canvas in hierarchy */
	UFUNCTION(BlueprintCallable, Category = LGUI)
	bool IsRootCanvas()const;

	bool IsRenderToScreenSpace()const;
	bool IsRenderToRenderTarget()const;
	bool IsRenderToWorldSpace()const;
	bool IsRenderByLexUIRendererOrUERenderer()const;

	/** Return LexWidget component which this LexCanvas attach to. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
	ULexWidget* GetLexWidget()const { return LexWidget.Get(); }
	TWeakObjectPtr<ULexCanvas> GetParentCanvas()const { return ParentCanvas; }

	void SetParentCanvas(ULexCanvas* InParentCanvas);

	DECLARE_EVENT_ThreeParams(ULexCanvas, FLGUICanvasRenderModeChangeEvent, ULexCanvas*, ELexRenderMode, ELexRenderMode);
	FLGUICanvasRenderModeChangeEvent OnRenderModeChanged;
protected:
	/** Root LexCanvas on hierarchy. LGUI's update start from the RootCanvas, and goes all down to every UI elements under it */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexCanvas> RootCanvas = nullptr;
	void CheckRenderMode(bool PropagateToChildrenCanvas);
	/** check RootCanvas. search for it if not valid */
	bool CheckRootCanvas(bool forceRecheck = false)const;
	/** nearest up parent Canvas */
	UPROPERTY(Transient) TWeakObjectPtr<ULexCanvas> ParentCanvas = nullptr;

	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexWidget> LexWidget = nullptr;
	bool CheckLexWidget()const;
protected:
	friend class FLexCanvasCustomization;
	friend class FLexWidgetCustomization;

	float CalculateDistanceToCamera()const;

	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexRenderMode RenderMode = ELexRenderMode::WorldSpace;
	/**
	 * Render to RenderTarget, if not specified then LGUI will create a new one.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		TObjectPtr<UTextureRenderTarget2D> RenderTarget;
	/** Controls how LexCanvas render to RenderTarget. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexCanvasRenderTargetUpdateMode RenderTargetUpdateMode = ELexCanvasRenderTargetUpdateMode::Automatic;
	/**
	 * How RenderTarget and LexCanvas's size change depend on the other.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexCanvasRenderTargetSizeMode RenderTargetSizeMode = ELexCanvasRenderTargetSizeMode::RenderTargetFitToCanvas;
	/**
	 * RenderTarget size scale.
	 * Only valid if RenderTargetSizeMode is RenderTargetFitToCanvas.
	 */
	UPROPERTY(EditAnywhere, Category = LGUI, meta = (ClampMin = "0.01", EditCondition="RenderTargetSizeMode==ELexCanvasRenderTargetSizeMode::RenderTargetFitToCanvas"))
		float RenderTargetResolutionScale = 1.0f;
#if WITH_EDITORONLY_DATA
	/**
	 * When in edit mode, show the Screen-Space-Overlay UI with LexUIRenderer.
	 * LexUIRenderer can show the color and texture at final result, not affect by post process.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bPreviewWithLexUIRenderer = false;
#endif
	/**
	 * true- Use custom sort order.
	 * false- Use default sort order management, which is based on hierarchy order.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bOverrideSorting = false;
	/**
	 * Canvas with larger order will render on top of lower one.
	 * NOTE! SortOrder value is stored with int16 type, so valid range is -32768 to 32767
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta=(EditCondition="bOverrideSorting"))
		int16 SortOrder = 0;
	
	/**
	 * The amount of pixels per unit to use for dynamically created bitmap texture, such as BitmapFont for UIText. 
	 * But!!! Do not set this value too large if you already have large font size of UIText, because that will result in extremely large texture! 
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		float DynamicPixelsPerUnit = 1.0f;

	/** Enable/disable normal and tangent in vertex data. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
	bool bRequireNormalAndTangent = false;

	/** Default materials, for render default UI elements. */
	UPROPERTY(EditAnywhere, Category = LGUI, meta = (DisplayThumbnail = "false"))
	mutable TObjectPtr<UMaterialInterface> DefaultMaterial;

	/** For "World Space - LGUI Renderer" only, render with blend depth, 0-occlude by scene depth, 1-all visible, 0.5-half transparent. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0.0", ClampMax = "1.0"))
		float BlendDepth = 0.0f;
	/** For "World Space - LGUI Renderer" only, render with depth fade effect. */
	UPROPERTY(EditAnywhere, Category = "LGUI", meta = (ClampMin = "0", ClampMax = "10"))
		int DepthFade = 0;
	/**
	 * Create a depth texture so we can do depth test. This is very useful for UIStaticMesh which use Opaque material.
	 * Only valid for ScreenSpaceOverlay and RenderTarget mode.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bEnableDepthTest = false;
	/** For not root canvas, inherit or override parent canvas parameters. */
	UPROPERTY(EditAnywhere, Category = LGUI, meta = (Bitmask, BitmaskEnum = "/Script/LGUI.ELexCanvasOverrideParameters"))
		int8 OverrideParameters;

	/** traceChannel for line trace of EventSystem interaction */
	UPROPERTY(EditAnywhere, Category = "LGUI")
	TEnumAsByte<ETraceTypeQuery> TraceChannel = TraceTypeQuery3;

	/**
	 * LexCanvas create mesh for render UI elements, this property can give us opportunity to use custom type of mesh for render.
	 * You can set "OwnerNoSee" "CastShadow" properties for your mesh.
	 * @todo: override this property from parent canvas?
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI", AdvancedDisplay, meta = (AllowAbstract = "true"))
		TSubclassOf<ULexUIMeshComponent> DefaultMeshType;

#pragma region CanvasScaler
	/** Virtual Camera Projection Type.*/
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler", AdvancedDisplay, meta = (DisplayName = "Projection Type"))
	TEnumAsByte<ECameraProjectionMode::Type> ProjectionType = ECameraProjectionMode::Perspective;
	/** Virtual Camera field of view (in degrees). */
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler", AdvancedDisplay, meta = (UIMin = "5.0", UIMax = "170", ClampMin = "0.001", ClampMax = "360.0"))
	float FieldOfView = 60;
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler", AdvancedDisplay)
	float NearClipPlane = 1;
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler", AdvancedDisplay)
	float FarClipPlane = 10000;
	
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler")
	ELexCanvasScaleMode ScaleMode = ELexCanvasScaleMode::ConstantPixelSize;
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler")
	FVector2D ReferenceResolution = FVector2D(1280, 720);
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Match"))
	float MatchFromWidthToHeight = 1;
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler")
	ELexCanvasScreenMatchMode ScreenMatchMode = ELexCanvasScreenMatchMode::MatchWidthOrHeight;
#if WITH_EDITORONLY_DATA
public:
	/** When Canvas use ScreenSpaceOverlay, in edit mode it will try to match editor viewport's size. So make this true to use a fixed size. */
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler")
	bool bFixedSizeInEditMode = false;
	UPROPERTY(EditAnywhere, Category = "LGUI-CanvasScaler", meta = (EditCondition = "bFixedSizeInEditMode"))
	FIntPoint SizeInEditMode = FIntPoint(1920, 1080);
#endif
private:
	/**
	 * Use this to do custom scale. Only valid if ScaleMode = Custom.
	 * Will fallback to "ConstantPixelSize" if not assign this value.
	 */
	UPROPERTY(EditAnywhere, Instanced, Category = "LGUI-CanvasScaler")
	TObjectPtr<ULexCanvasCustomScale> CustomScale;
	/** Current viewport size*/
	FIntPoint ViewportSize = FIntPoint(2, 2);
#pragma endregion

public:
	FORCEINLINE bool GetOverrideDefaultMaterial()const				{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::DefaultMaterial); }
	FORCEINLINE bool GetOverrideDynamicPixelsPerUnit()const			{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::DynamicPixelsPerUnit); }
	FORCEINLINE bool GetOverrideRequireNormalAndTangent()const		{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::RequireNormalAndTangent); }
	FORCEINLINE bool GetOverrideBlendDepth()const					{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::BlendDepth); }
	FORCEINLINE bool GetOverrideDepthFade()const					{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::DepthFade); }

	UFUNCTION(BlueprintCallable, Category = LGUI)
		UMaterialInterface* GetDefaultMaterial()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDefaultMaterial(UMaterialInterface* InMaterial);

	UFUNCTION(BlueprintCallable, Category = LGUI)
	void SetTraceChannel(TEnumAsByte<ETraceTypeQuery> InTraceChannel);
	UFUNCTION(BlueprintCallable, Category = LGUI)
	TEnumAsByte<ETraceTypeQuery> GetTraceChannel()const { return TraceChannel; }

	/** Set render mode of this canvas. This may not take effect if the canvas is not a root canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderMode(ELexRenderMode Value);
	/** Set parameters for calculating projection matrix. Only valid for ScreenSpace/RenderTarget mode. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetProjectionParameters(TEnumAsByte<ECameraProjectionMode::Type> InProjectionType, float InFovAngle, float InNearClipPlane, float InFarClipPlane);
	/** if renderMode is RenderTarget, then this will change the renderTarget */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderTarget(UTextureRenderTarget2D* Value);
	DECLARE_EVENT_TwoParams(ULexCanvas, FOnRenderTargetCreatedOrChangedEvent, UTextureRenderTarget2D*, bool);
	FOnRenderTargetCreatedOrChangedEvent OnRenderTargetCreatedOrChanged;

	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderTargetResolutionScale(float Value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderTargetSizeMode(ELexCanvasRenderTargetSizeMode Value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderTargetUpdateMode(ELexCanvasRenderTargetUpdateMode Value);
	/** Only valid when call this on root canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void RequestUpdateForRenderTarget();
	
	/** 
	 * Set LexCanvas SortOrder
	 * @param	PropagateToChildrenCanvas	if true, set this Canvas's SortOrder and all children Canvas, not just set absolute value, but keep child Canvas's relative order to this one
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSortOrder(int32 Value, bool PropagateToChildrenCanvas = true);
	/** Set SortOrder to highest, so this canvas will render on top of all canvas that belong to same hierarchy. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSortOrderToHighestOfHierarchy(bool PropagateToChildrenCanvas = true);
	/** Set SortOrder to lowest, so this canvas will render behind all canvas that belong to same hierarchy. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSortOrderToLowestOfHierarchy(bool PropagateToChildrenCanvas = true);
	void GetMinMaxSortOrderOfHierarchy(int32& OutMin, int32& OutMax);

	/** Get render mode of root canvas. Canvas's render-mode is inherited from parent canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexRenderMode GetRootRenderMode()const;
	/** Get render mode of this canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexRenderMode GetRenderMode()const { return RenderMode; }
	/** Get render target of root canvas if render mode is RenderTarget. Canvas's render-target is inherited from root canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		UTextureRenderTarget2D* GetRootRenderTarget()const;
	/** Get render target of this canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		UTextureRenderTarget2D* GetRenderTarget()const { return RenderTarget; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetRootRenderTargetResolutionScale()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetRenderTargetResolutionScale()const { return RenderTargetResolutionScale; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexCanvasRenderTargetSizeMode GetRootRenderTargetSizeMode()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexCanvasRenderTargetSizeMode GetRenderTargetSizeMode()const { return RenderTargetSizeMode; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexCanvasRenderTargetUpdateMode GetRootRenderTargetUpdateMode()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexCanvasRenderTargetUpdateMode GetRenderTargetUpdateMode()const { return RenderTargetUpdateMode; }

	/** Get actual BlendDepth value of canvas. This property may inherit from parent canvas depend on OverrideParameters property. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetActualBlendDepth()const;
	/** Get blendDepth value of this canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetBlendDepth()const { return BlendDepth; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetBlendDepth(float Value);

	/** Get actual DepthFade value of canvas. This property may inherit from parent canvas depend on OverrideParameters property. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int GetActualDepthFade()const;
	/** Get blendDepth value of this canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int GetDepthFade()const { return DepthFade; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDepthFade(int Value);

	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool GetEnableDepthTest()const { return bEnableDepthTest; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetEnableDepthTest(bool Value);

	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool GetOverrideSorting()const { return bOverrideSorting; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideSorting(bool Value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetSortOrder()const { return SortOrder; }
	/** Get actual SortOrder of canvas. This property may inherit from parent canvas depend on OverrideSorting property. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetActualSortOrder()const;

	UFUNCTION(BlueprintCallable, Category = LGUI)
	bool GetActualRequireNormalAndTangent()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
	bool GetRequireNormalAndTangent()const { return bRequireNormalAndTangent; }

	/** Get actual DynamicPixelsPerUnit of canvas. This property may inherit from parent canvas depend on OverrideParameters property. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetActualDynamicPixelsPerUnit()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetDynamicPixelsPerUnit()const { return DynamicPixelsPerUnit; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDynamicPixelsPerUnit(float Value);

	int GetDrawCallCount()const;

	/** Override LexUI's screen space UI render's camera location. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideViewLocation(bool Override, FVector Value);
	/** Override LexUI's screen space UI render's camera rotation. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideViewRotation(bool Override, FRotator Value);
	/**
	 * Override LexUI's screen space UI render's camera's fov in degree, will affect projection matrix.
	 * If SetOverrideProjectionMatrix is true, then this will not take effect.
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideFovAngle(bool Override, float Value);
	/**
	 * Override LexUI's screen space UI render's camera's projection matrix.
	 * If this is set to true, then SetOverrideFovAngle will not take effect.
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideProjectionMatrix(bool Override, FMatrix Value);

	UFUNCTION(BlueprintCallable, Category = LGUI)
		TSubclassOf<ULexUIMeshComponent> GetDefaultMeshType()const { return DefaultMeshType; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDefaultMeshType(TSubclassOf<ULexUIMeshComponent> InValue);

#pragma region CanvasScaler
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	TEnumAsByte<ECameraProjectionMode::Type> GetProjectionType()const { return ProjectionType; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	float GetFieldOfView()const { return FieldOfView; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	float GetNearClipPlane()const { return NearClipPlane; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	float GetFarClipPlane()const { return FarClipPlane; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetProjectionType(TEnumAsByte<ECameraProjectionMode::Type> Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetFieldOfView(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetNearClipPlane(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetFarClipPlane(float Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	ELexCanvasScaleMode GetScaleMode() { return ScaleMode; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	FVector2D GetReferenceResolution() { return ReferenceResolution; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	float GetMatchFromWidthToHeight() { return MatchFromWidthToHeight; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	ELexCanvasScreenMatchMode GetScreenMatchMode() { return ScreenMatchMode; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	ULexCanvasCustomScale* GetCustomScale()const { return CustomScale; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetScaleMode(ELexCanvasScaleMode Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetReferenceResolution(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetMatchFromWidthToHeight(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetScreenMatchMode(ELexCanvasScreenMatchMode Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	void SetCustomScale(ULexCanvasCustomScale* Value);

	/**
	 * Convert position from viewport to LexCanvas space.
	 * @param InPosition The point's pixel position on viewport.
	 * @param Result LexCanvas space position, left bottom is zero point.
	 * @return convert will fail if this LexCanvas is not root canvas
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	bool ConvertPositionFromViewportToCanvas(const FVector2D& InPosition, FVector2D& Result)const;
	/**
	 * Convert position from LexCanvas space to viewport.
	 * @param InPosition The point's position in LexCanvas space.
	 * @param Result in viewport, pixel unit, left top is zero point.
	 * @return convert will fail if this LexCanvas is not root canvas
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	bool ConvertPositionFromCanvasToViewport(const FVector2D& InPosition, FVector2D& Result)const;
	/**
	 * NOTE!!! This is only for screen-space-UI, don't use this for convert world space position!!!
	 * Project 3D screen-space-UI element's position to 2D screen-space-UI.
	 * @param	Position3D	GetWorldLocation from the UI element (world location).
	 * @param	OutPosition2D	2D Position in screen-space, left bottom is zero point.
	 * @return 	convert will fail if this LexCanvas is not root canvas.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	bool Project3DToScreen(const FVector& Position3D, FVector2D& OutPosition2D)const;

	/**
	 * CAUTION!!! This is just a test or reference function.
	 * Compare to built-in GameplayStatics::ProjectWorldToScreen, this function has no latency, however GameplayStatics::ProjectWorldToScreen use last frame's camera location & rotation
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI-CanvasScaler")
	bool ProjectWorldToScreen(class APlayerController* Player, const FVector& Position3D, FVector2D& OutPosition2D)const;

private:
#if WITH_EDITOR
	FDelegateHandle EditorTickDelegateHandle;
	FDelegateHandle LexUIPreview_ViewportIndexChangeDelegateHandle;
	void DrawVirtualCamera();
	void DrawViewportArea();
	void OnEditorTick(float DeltaTime);
	void OnPreviewSetting_EditorPreviewViewportIndexChange();
	void RegisterCanvasScaler();
	void UnregisterCanvasScaler();
#endif
	void OnViewportParameterChanged();
	void CheckAndApplyViewportParameter();
	FDelegateHandle ViewportResizeDelegateHandle;
#pragma endregion

public:
	void MarkVisualWillChange(ULexVisual* InOldVisual);
	void RegisterVisual(ULexWidget* InWidget);
	void UnregisterVisual(ULexWidget* InVisual);

	void AddLexWidget(ULexWidget* InWidget);
	void RemoveLexWidget(ULexWidget* InWidget);
	/** return all LexWidget that belongs to this canvas. */
	const TArray<ULexWidget*>& GetVisualWidgetArray()const { return VisualWidgetList; }
	const TArray<ULexWidget*>& GetWidgetArray()const { return WidgetList; }

	void SetRequireNormalAndTangent(bool Value);

	float GetLastRenderTime()const;
	ULexUIMeshComponent* GetUIMesh()const { CheckUIMesh(); return UIMesh.Get(); }
public:
	static FName LexUI_MainTextureMaterialParameterName;
	static FName LexUI_FontTextureMaterialParameterName;
	static FName LexUI_ClipDataTexture_MaterialParameterName;
	bool IsMaterialContainsLexUIParameter(UMaterialInterface* InMaterial);
private:
	void SetSortOrderAdditionalValueRecursive(int32 InAdditionalValue);
	void UpdateRenderTarget(bool CallEvent);
	/** Check if any invalid in list. Currently use in editor after undo check or rebuild. */
	void EnsureDrawCallObjectReference();
public:
	/** Called from LexUIManagerActor. Update this canvas if it is a RootCanvas */
	void UpdateRootCanvas();
	/**  */
	void MarkNeedVerifyMaterials();
private:
	uint32 bCanTickUpdate:1;//if Canvas can update from tick
	uint32 bShouldRebuildDrawCall : 1;
	uint32 bShouldClearCachedDrawCall : 1;//mark this to true will delete all cached draw-call and rebuild all draw-call
	uint32 bShouldSortVisualOrder : 1;//if any visual LexWidget's hierarchy change, then we need to sort visual list
	uint32 bNeedToSortRenderPriority : 1;
	uint32 bHasAddToLexScreenSpaceRenderer : 1;//is this canvas added to LGUI screen space renderer
	uint32 bRequestUpdateForRenderTarget : 1;//request update when RenderTargetUpdateMode is WhenRequest
	uint32 bAnythingChangedForRenderTarget : 1;//if children canvas anything changed, then mark this property for root canvas, good for RenderTarget mode to update
	uint32 bPrevAnythingChangedForRenderTarget : 1;//same as upper one, but the prev frame
	uint32 bHasSetInitialStateForLexWorldSpaceRenderer : 1;//is LGUI world space renderer's initial state set
	uint32 bNeedToVerifyMaterials : 1;
	uint32 bRootCanvasNeedToUpdateChildrenCanvasBounds : 1;//if child canvas's UIMesh's bounds change, then need to notify root canvas to update it's UIMesh's bounds

	uint32 bPrevIsVisible : 1;//is LexWidget active in prev frame?

	uint32 bOverrideViewLocation:1, bOverrideViewRotation:1, bOverrideProjectionMatrix:1, bOverrideFovAngle :1;

	mutable uint32 bUIMeshNeedToSetInitialParameters : 1;//after clear UIMesh, it will need to set initial parameters to use again
	mutable uint32 bIsViewProjectionMatrixDirty : 1;
	mutable FMatrix CacheViewProjectionMatrix = FMatrix::Identity;//cache to prevent multiple calculation in same frame
	mutable float LastRenderTime = 0;
	friend class FLexUIRenderSceneProxy;
	/**
	 * RenderMode can affect UI's renderer, basically WorldSpace use UE's built-in renderer, others use LGUI's renderer. Different renderers cannot share same render data.
	 * eg: when attach to other canvas, this will tell which render mode in old canvas, and if not compatible then recreate render data.
	 */
	ELexRenderMode CurrentRenderMode = ELexRenderMode::None;
	bool RenderModeIsLexRendererOrUERenderer(ELexRenderMode InRenderMode)const
	{
		return 
			InRenderMode != ELexRenderMode::WorldSpace
			;
	}

	FVector OverrideViewLocation;
	FRotator OverrideViewRotation;
	float OverrideFovAngle;
	FMatrix OverrideProjectionMatrix;

	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	mutable TWeakObjectPtr<ULexUIMeshComponent> UIMesh;//current using UIMesh.
	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> PooledUIMaterialList;//Default material pool.
	TArray<TSharedPtr<FLexUIDrawCall>> UIDrawCallList;//DrawCall collection of this Canvas.
	TArray<TSharedPtr<FLexUIDrawCall>> CacheUIDrawCallList;//Cached DrawCall collection.
	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	TArray<TObjectPtr<ULexWidget>> VisualWidgetList;//Use LexWidget instead of LexVisual, because we need LexWidget to get sub-canvas.
	bool bNeedToGenerateWidgetList = true;
	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	TArray<TObjectPtr<ULexWidget>> WidgetList;//All LexWidget that belongs to this canvas
	TSharedPtr<FLexUIDrawCall> DrawCallAsChildCanvas = nullptr;//DrawCall that represent this canvas when the canvas is render as child.
	TAtomic<int> ThreadProcessingGeometryCount;

	TArray<TSharedPtr<FLexUIClipData>> ClipDataList;
	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	TObjectPtr<ULexUIDataAsTexture> ClipDataAsTexture;//clip coordinate stored in UV1.x
	void OnClipDataTextureChanged(UTexture* NewTexture);
public:
	void IncreaseThreadProcessingGeometry(){ThreadProcessingGeometryCount.IncrementExchange();}
	void DecreaseThreadProcessingGeometry(){ThreadProcessingGeometryCount.DecrementExchange();}
	/** Called by LexWidget to delete clip data */
	void RemoveClipData(const TSharedPtr<FLexUIClipData>& InClipData);
	UTexture* GetClipDataTexture()const;
public:
	const TArray<TSharedPtr<FLexUIDrawCall>>& GetUIDrawCallList()const { return UIDrawCallList; }
	
	static FTransform2D ConvertTo2DTransform(const FTransform& Transform);
	static void CalculateVisual2DBounds(ULexVisual* item, const FTransform2D& transform, FVector2D& min, FVector2D& max);
private:

	/** canvas array belong to this canvas in hierarchy. */
	UPROPERTY(Transient) TArray<TWeakObjectPtr<ULexCanvas>> ChildrenCanvasArray;
	/** update Canvas's draw-call */
	bool UpdateCanvasDrawCallRecursive();
	/** mark render finish */
	void MarkFinishRenderFrameRecursive();

	void BatchDrawCall_Implement(const FVector2D& InCanvasLeftBottom, const FVector2D& InCanvasRightTop, TArray<TSharedPtr<FLexUIDrawCall>>& InUIDrawCallList, TArray<TSharedPtr<FLexUIDrawCall>>& InCacheUIDrawCallList, bool& OutNeedToSortRenderPriority);
	void UpdateDrawCallMesh_Implement();
	void UpdateDrawCallMaterial_Implement();
	void SortDrawCall();
public:
	static bool Is2DUITransform(const FTransform& Transform);
private:
	UMaterialInstanceDynamic* GetUIMaterialFromPool();
	void AddUIMaterialToPool(UMaterialInstanceDynamic* uiMat);
	void CheckUIMesh()const;
};
