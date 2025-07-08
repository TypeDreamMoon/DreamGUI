// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Layout/Margin.h"
#include "Components/ActorComponent.h"
#include "Camera/CameraTypes.h"
#include "Math/TransformCalculus2D.h"
#include "LexCanvas.generated.h"

class FLexUIClipData;
class ULexUIDataAsTexture;

UENUM(BlueprintType, Category = LGUI)
enum class ELexRenderMode :uint8
{
	/**
	 * Render in screen space. If there are multiple screen-space-ui-root in world, they will be sort by SortOrder property.
	 * This mode use LGUI's custom render pipeline.
	 * This mode need a LGUICanvasScaler to control the size and scale.
	 */
	ScreenSpaceOverlay = 0,
	/**
	 * Render in world space by UE default render pipeline.
	 * This mode use engine's default render pieple, so post process will affect ui.
	 */
	WorldSpace=1			UMETA(DisplayName = "World Space - UE Renderer"),
	/**
	 * Render in world space by LGUI's custom render pipeline, 
	 * This mode use LGUI's custom render pipeline, will not be affected by post process.
	 */
	WorldSpace_LGUI = 3		UMETA(DisplayName = "World Space - LGUI Renderer"),
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
	/** Change LGUICanvas's size to fit RenderTarget. */
	CanvasFitToRenderTarget,
	/** Change RenderTarget's size to fit LGUICanvas. */
	RenderTargetFitToCanvas,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexCanvasRenderTargetUpdateMode : uint8
{
	/** LGUI will automatic manage update, only draw to RenderTarget when it detect something change. */
	Automatic,
	/** Alway draw to RenderTarget every frame. */
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

class ULexWidget;
class ULexVisual;
class ULexVisualBatchMesh;
class UUIDirectMeshRenderable;
class ULexUIMeshComponent;
class FLexUIDrawCall;
class FLexVisualPostProcessRenderProxy;
class UTextureRenderTarget2D;
class ULGUICanvasCustomClip;

/**
 * Canvas is for render and update all UI elements.
 */
UCLASS(ClassGroup = (LGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexCanvas : public UActorComponent
{
	GENERATED_BODY()

public:	
	ULexCanvas();
protected:
	virtual void BeginPlay() override;
	virtual void TickComponent( float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction ) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason)override;
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
	void RemoveFromViewExtension(bool PropogateToChildrenCanvas);
	TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> RenderTargetViewExtension = nullptr;
	TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> GetRenderTargetViewExtension();
public:
	/** mark canvas layout dirty */
	void MarkCanvasLayoutDirty();
	/**
	 * Mark update this Canvas. Canvas dont need to update every frame, only update when need to.
	 * Some rules if update could trigger drawcall's rebuild:
	 *		1. Commonly material & texture change and UI item's active state change
	 *		2. Transform & vertex position change, drawcall could overlap with eachother
	 *		3. Hierarchy order change, this is directly related to render order
	 * And about drawcall's rebuild, it's not actually force rebuild, it will check and reuse prev drawcall if possible.
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
	FORCEINLINE float GetCanvasScale()const { return canvasScale; }
private:
	friend class ULexCanvasScaler;
	float canvasScale = 1.0f;//for screen space UI, screen size / root canvas size

	FDelegateHandle UIHierarchyChangedDelegateHandle;
	/** hierarchy changed */
	void OnUIHierarchyChanged();

	FDelegateHandle UIActiveStateChangedDelegateHandle;
	void OnUIActiveStateChanged(bool value);
public:
	/** get root LGUICanvas on hierarchy */
	UFUNCTION(BlueprintCallable, Category = LGUI)
	ULexCanvas* GetRootCanvas()const;
	bool IsRootCanvas()const;

	bool IsRenderToScreenSpace()const;
	bool IsRenderToRenderTarget()const;
	bool IsRenderToWorldSpace()const;
	bool IsRenderByLGUIRendererOrUERenderer()const;

	/** Return UIItem component which this LGUICanvas attach to. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
	ULexWidget* GetLexWidget()const { return LexWidget.Get(); }
	bool GetIsUIActive()const;
	TWeakObjectPtr<ULexCanvas> GetParentCanvas()const { return ParentCanvas; }

	void SetParentCanvas(ULexCanvas* InParentCanvas);

	DECLARE_EVENT_ThreeParams(ULexCanvas, FLGUICanvasRenderModeChangeEvent, ULexCanvas*, ELexRenderMode, ELexRenderMode);
	FLGUICanvasRenderModeChangeEvent OnRenderModeChanged;
protected:
	/** Root LGUICanvas on hierarchy. LGUI's update start from the RootCanvas, and goes all down to every UI elements under it */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexCanvas> RootCanvas = nullptr;
	void CheckRenderMode(bool PropogateToChildrenCanvas);
	/** chekc RootCanvas. search for it if not valid */
	bool CheckRootCanvas(bool forceRecheck = false)const;
	/** nearest up parent Canvas */
	UPROPERTY(Transient) TWeakObjectPtr<ULexCanvas> ParentCanvas = nullptr;

	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexWidget> LexWidget = nullptr;
	bool CheckUIItem()const;
protected:
	friend class FLexCanvasCustomization;
	friend class FLexWidgetCustomization;

	ECameraProjectionMode::Type ProjectionType = ECameraProjectionMode::Perspective;
	float FOVAngle = 90;
	float NearClipPlane = GNearClippingPlane;
	float FarClipPlane = GNearClippingPlane;

	float CalculateDistanceToCamera()const;

	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexRenderMode RenderMode = ELexRenderMode::WorldSpace;
	/**
	 * Render to RenderTarget, if not specified then LGUI will create a new one.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		TObjectPtr<UTextureRenderTarget2D> RenderTarget;
	/** Controls how LGUICanvas render to RenderTarget. */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		ELexCanvasRenderTargetUpdateMode RenderTargetUpdateMode = ELexCanvasRenderTargetUpdateMode::Automatic;
	/**
	 * How RenderTarget and LGUICanvas's size change depend on the other.
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
	 * When in eidt mode, show the Screen-Space-Overlay UI with LGUIRenderer.
	 * LGUIRenderer can show the color and texture at final result, not affect by post process.
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		bool bPreviewWithLGUIRenderer = false;
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
	UPROPERTY(EditAnywhere, Category = "LexUI")
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
	UPROPERTY(EditAnywhere, Category = LGUI, meta = (Bitmask, BitmaskEnum = "/Script/LGUI.ELGUICanvasOverrideParameters"))
		int8 OverrideParameters;

	/** traceChannel for line trace of EventSystem interaction */
	UPROPERTY(EditAnywhere, Category = "LGUI-Raycast")
	TEnumAsByte<ETraceTypeQuery> TraceChannel = TraceTypeQuery3;

	/**
	 * LGUICanvas create mesh for render UI elements, this property can give us opportunity to use custom type of mesh for render.
	 * You can set "OwnerNoSee" "CastShadow" properties for your mesh.
	 * @todo: override this property from parent canvas?
	 */
	UPROPERTY(EditAnywhere, Category = LGUI, AdvancedDisplay, meta = (AllowAbstract = "true"))
		TSubclassOf<ULexUIMeshComponent> DefaultMeshType;

	FORCEINLINE bool GetOverrideDefaultMaterial()const				{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::DefaultMaterial); }
	FORCEINLINE bool GetOverrideDynamicPixelsPerUnit()const			{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::DynamicPixelsPerUnit); }
	FORCEINLINE bool GetOverrideRequireNormalAndTangent()const		{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::RequireNormalAndTangent); }
	FORCEINLINE bool GetOverrideBlendDepth()const					{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::BlendDepth); }
	FORCEINLINE bool GetOverrideDepthFade()const					{ return OverrideParameters & (1 << (int)ELexCanvasOverrideParameters::DepthFade); }

public:
	UFUNCTION(BlueprintCallable, Category = LGUI)
		UMaterialInterface* GetDefaultMaterial()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDefaultMaterial(UMaterialInterface* InMaterial);

	UFUNCTION(BlueprintCallable, Category = LGUI)
	void SetTraceChannel(TEnumAsByte<ETraceTypeQuery> InTraceChannel);
	UFUNCTION(BlueprintCallable, Category = LGUI)
	TEnumAsByte<ETraceTypeQuery> GetTraceChannel()const { return TraceChannel; }

	/** Set render mode of this canvas. This may not take effect if the canvas is not a root cnavas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderMode(ELexRenderMode value);
	/** Set parameters for calculating projection matrix. Only valid for ScreenSpace/RenderTarget mode. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetProjectionParameters(TEnumAsByte<ECameraProjectionMode::Type> InProjectionType, float InFovAngle, float InNearClipPlane, float InFarClipPlane);
	/** if renderMode is RenderTarget, then this will change the renderTarget */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderTarget(UTextureRenderTarget2D* value);
	DECLARE_EVENT_TwoParams(ULexCanvas, FOnRenderTargetCreatedOrChangedEvent, UTextureRenderTarget2D*, bool);
	FOnRenderTargetCreatedOrChangedEvent OnRenderTargetCreatedOrChanged;

	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderTargetResolutionScale(float value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderTargetSizeMode(ELexCanvasRenderTargetSizeMode value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetRenderTargetUpdateMode(ELexCanvasRenderTargetUpdateMode value);
	/** Only valid when call this on root canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void RequestUpdateForRenderTarget();
	
	/** 
	 * Set LGUICanvas SortOrder
	 * @param	propagateToChildrenCanvas	if true, set this Canvas's SortOrder and all children Canvas, not just set absolute value, but keep child Canvas's relative order to this one
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSortOrder(int32 newValue, bool propagateToChildrenCanvas = true);
	/** Set SortOrder to highest, so this canvas will render on top of all canvas that belong to same hierarchy. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSortOrderToHighestOfHierarchy(bool propagateToChildrenCanvas = true);
	/** Set SortOrder to lowest, so this canvas will render behide all canvas that belong to same hierarchy. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSortOrderToLowestOfHierarchy(bool propagateToChildrenCanvas = true);
	void GetMinMaxSortOrderOfHierarchy(int32& OutMin, int32& OutMax);

	/** Get actually render mode of canvas. Canvas's render-mode is inherited from parent canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexRenderMode GetActualRenderMode()const;
	/** Get render mode of this canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexRenderMode GetRenderMode()const { return RenderMode; }
	/** Get render target of canvas if render mode is RenderTarget. Canvas's render-target is inherited from root canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		UTextureRenderTarget2D* GetActualRenderTarget()const;
	/** Get render target of this canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		UTextureRenderTarget2D* GetRenderTarget()const { return RenderTarget; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetActualRenderTargetResolutionScale()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetRenderTargetResolutionScale()const { return RenderTargetResolutionScale; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexCanvasRenderTargetSizeMode GetActualRenderTargetSizeMode()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexCanvasRenderTargetSizeMode GetRenderTargetSizeMode()const { return RenderTargetSizeMode; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexCanvasRenderTargetUpdateMode GetActualRenderTargetUpdateMode()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexCanvasRenderTargetUpdateMode GetRenderTargetUpdateMode()const { return RenderTargetUpdateMode; }

	/** Get actual blendDepth value of canvas. Canvas's BlendDepth is inherited from parent canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetActualBlendDepth()const;
	/** Get blendDepth value of this canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetBlendDepth()const { return BlendDepth; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetBlendDepth(float value);

	/** Get actual depthFade value of canvas. Canvas's DepthFade is inherited from parent canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int GetActualDepthFade()const;
	/** Get blendDepth value of this canvas. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int GetDepthFade()const { return DepthFade; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDepthFade(int value);

	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool GetEnableDepthTest()const { return bEnableDepthTest; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetEnableDepthTest(bool value);

	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool GetOverrideSorting()const { return bOverrideSorting; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideSorting(bool value);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetSortOrder()const { return SortOrder; }
	/** Get actual SortOrder of this canvas. Canvas's SortOrder property may inherit from parent canvas depend on OverrideSorting property. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetActualSortOrder()const;

	UFUNCTION(BlueprintCallable, Category = LGUI)
	bool GetActualRequireNormalAndTangent()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
	bool GetRequireNormalAndTangent()const { return bRequireNormalAndTangent; }

	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetActualDynamicPixelsPerUnit()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		float GetDynamicPixelsPerUnit()const { return DynamicPixelsPerUnit; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDynamicPixelsPerUnit(float newValue);

	int GetDrawCallCount()const;

	/** Override LGUI's screen space UI render's camera location. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideViewLocation(bool InOverride, FVector InValue);
	/** Override LGUI's screen space UI render's camera rotation. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideViewRotation(bool InOverride, FRotator InValue);
	/**
	 * Override LGUI's screen space UI render's camera's fov in degree, will affect projection matrix.
	 * If SetOverrideProjectionMatrix is true, then this will not take effect.
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideFovAngle(bool InOverride, float InValue);
	/**
	 * Override LGUI's screen space UI render's camera's projection matrix.
	 * If this is set to true, then SetOverrideFovAngle will not take effect.
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetOverrideProjectionMatrix(bool InOverride, FMatrix InValue);

	UFUNCTION(BlueprintCallable, Category = LGUI)
		TSubclassOf<ULexUIMeshComponent> GetDefaultMeshType()const { return DefaultMeshType; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDefaultMeshType(TSubclassOf<ULexUIMeshComponent> InValue);

	void MarkVisualWillChange(ULexVisual* InOldVisual);
	void RegisterVisual(ULexWidget* InWidget);
	void UnregisterVisual(ULexWidget* InVisual);

	void AddLexWidget(ULexWidget* InUIItem);
	void RemoveLexWidget(ULexWidget* InUIItem);
	/** return all UIItem that belongs to this canvas. */
	const TArray<ULexWidget*>& GetVisualWidgetArray()const { return VisualWidgetList; }

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
	/** Called from LGUIManagerActor. Update this canvas if it is a RootCanvas */
	void UpdateRootCanvas();
	/**  */
	void MarkNeedVerifyMaterials();
private:
	uint32 bCanTickUpdate:1;//if Canvas can update from tick
	uint32 bShouldRebuildDrawCall : 1;
	uint32 bShouldClearCachedDrawCall : 1;//mark this to true will delete all cached drawcall and rebuild all drawcall
	uint32 bShouldSortRenderableOrder : 1;//if any renderable UIItem's hierarchy change, then we need to sort renderable list
	uint32 bNeedToSortRenderPriority : 1;
	uint32 bHasAddToLGUIScreenSpaceRenderer : 1;//is this canvas added to LGUI screen space renderer
	uint32 bRequestUpdateForRenderTarget : 1;//request update when RenderTargetUpdateMode is WhenRequest
	uint32 bAnythingChangedForRenderTarget : 1;//if children canvas anything changed, then mark this property for root canvas, good for RenderTarget mode to update
	uint32 bPrevAnythingChangedForRenderTarget : 1;//same as upper one, but the prev frame
	uint32 bHasSetIntialStateforLGUIWorldSpaceRenderer : 1;//is LGUI world space renderer's initial state set
	uint32 bNeedToVerifyMaterials : 1;
	uint32 bRootCanvasNeedToUpdateChildrenCanvasBounds : 1;//if child canvas's UIMesh's bounds change, then need to notify root canvas to update it's UIMesh's bounds

	uint32 bPrevUIItemIsActive : 1;//is UIItem active in prev frame?

	uint32 bOverrideViewLocation:1, bOverrideViewRotation:1, bOverrideProjectionMatrix:1, bOverrideFovAngle :1;

	mutable uint32 bUIMeshNeedToSetInitialParameters : 1;//after clear UIMesh, it will need to set initial parameters to use again
	mutable uint32 bIsViewProjectionMatrixDirty : 1;
	mutable FMatrix CacheViewProjectionMatrix = FMatrix::Identity;//cache to prevent multiple calculation in same frame
	mutable float LastRenderTime = 0;
	friend class FLexUIRenderSceneProxy;
	/**
	 * RenderMode can affect UI's renderer, basically WorldSpace use UE's buildin renderer, others use LGUI's renderer. Different renderers cannot share same render data.
	 * eg: when attach to other canvas, this will tell which render mode in old canvas, and if not compatible then recreate render data.
	 */
	ELexRenderMode CurrentRenderMode = ELexRenderMode::None;
	bool RenderModeIsLGUIRendererOrUERenderer(ELexRenderMode InRenderMode)const
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
	TArray<TObjectPtr<ULexWidget>> VisualWidgetList;//Use UIItem instead of UIBaseRenderable, because we need UIItem to get sub-canvas.
	bool bNeedToGenerateRenderWidgetList = true;
	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	TArray<TObjectPtr<ULexWidget>> RenderWidgetList;//All UIItem that belongs to this canvas
	TSharedPtr<FLexUIDrawCall> DrawCallAsChildCanvas = nullptr;//DrawCall that represent this canvas when the canvas is render as child.
	TAtomic<int> ThreadProcessingGeometryCount;

	TArray<TSharedPtr<FLexUIClipData>> ClipDataList;
	UPROPERTY(Transient, VisibleAnywhere, Category = "LGUI", AdvancedDisplay)
	TObjectPtr<ULexUIDataAsTexture> ClipDataAsTexture;//clip coordinate stored in UV1.x
	void OnClipDataTextureChanged(UTexture* NewTexture);
public:
	void IncreaseThreadProcessingGeometry(){ThreadProcessingGeometryCount.IncrementExchange();}
	void DecreaseThreadProcessingGeometry(){ThreadProcessingGeometryCount.DecrementExchange();}
	/** Called by UIItem to delete clip data */
	void RemoveClipData(const TSharedPtr<FLexUIClipData>& InClipData);
	UTexture* GetClipDataTexture()const;
public:
	const TArray<TSharedPtr<FLexUIDrawCall>>& GetUIDrawCallList()const { return UIDrawCallList; }
	
	static FTransform2D ConvertTo2DTransform(const FTransform& Transform);
	static void CalculateUIItem2DBounds(ULexVisual* item, const FTransform2D& transform, FVector2D& min, FVector2D& max);
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
