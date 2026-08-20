// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamWidgetSubObjectBehaviour.h"
#include "DreamTweener.h"
#include "Utils/DreamUIUtils.h"
#include "DreamVisual.generated.h"

struct FDreamUIHitResult;
class FDreamUIGeometry;
class UMaterialInterface;
class UDreamCanvas;
class UDreamVisualCustomRaycast;
class UDreamVisual;

/**
 * This component is only used when DreamVisual's RaycastType = Custom
 */
UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced, EditInlineNew)
class DREAMGUI_API UDreamVisualCustomRaycast :public UObject
{
	GENERATED_BODY()

protected:
	/**
	 * Called by DreamVisual when do raycast hit test.
	 * @param	InVisual			The DreamVisual object which call this Raycast function
	 * @param	InLocalSpaceRayStart	Ray start point in this UI's local space
	 * @param	InLocalSpaceRayEnd		Ray end point in this UI's local space
	 * @param	OutHitPoint				Hit point position in this UI's local space
	 * @param	OutHitNormal			Hit point normal in this UI's local space
	 * @return	true if hit this UI object
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DreamGUI", meta = (DisplayName = "Raycast"))
		bool ReceiveRaycast(const UDreamVisual* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector& OutHitPoint, FVector& OutHitNormal)const;
	/**
	 * Get pixel value at hit point.
	 * Only support UI element type which can read pixel value from texture:
	 *		1. UISprite which use DreamGUIStaticSpriteAtlasData can work perfectly.
	 *		2. UITexture can work with some specific setting.
	 *		3. UIText which use dynamic font can not work. (Currently all DreamGUI's built-in font is dynamic)
	 * Will fallback to Geometry if ui element not support this raycast type.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		static bool GetRaycastPixelFromUIBatchMeshVisual(const class UDreamVisualBatchMesh* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector2D& OutUV, FColor& OutPixel, FVector& OutHitPoint, FVector& OutHitNormal);
public:
	/**
	 * Called by DreamVisual when do raycast hit test.
	 * @param	InVisual			The DreamVisual object which call this Raycast function
	 * @param	InLocalSpaceRayStart	Ray start point in this UI's local space
	 * @param	InLocalSpaceRayEnd		Ray end point in this UI's local space
	 * @param	OutHitPoint				Hit point position in this UI's local space
	 * @param	OutHitNormal			Hit point normal in this UI's local space
	 * @return	true if hit this UI object
	 */
	virtual bool Raycast(const UDreamVisual* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector& OutHitPoint, FVector& OutHitNormal)const;
};

UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamVisualType :uint8
{
	None,
	BatchMesh,
	PostProcess,
	DirectMesh,
};

// Defines how mouse or ray hit on DreamVisual
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamVisualRaycastType :uint8
{
	/** Hit on rect range */
	Rect = 0,
	/** Hit on actual triangle mesh */
	Mesh = 1,
	/**
	 * Hit on main texture's pixel, if the pixel is not transparent then hit test success.
	 * Only support UI element type which can read pixel value from texture:
	 *		1. UISprite which use DreamUIStaticSpriteAtlasData can work perfectly.
	 *		2. UITexture can work with some specific setting.
	 *		3. UIText which use dynamic font can NOT work. (Currently all DreamGUI's built-in font is dynamic)
	 * Will fallback to Mesh if ui element not support this raycast type.
	 */
	VisiblePixel = 3,
	/** Use a user defined UDreamVisualCustomRaycast to process the raycast hit. */
	Custom = 2,
};

/** Base class of UI element that can be rendered by DreamCanvas */
UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced)
class DREAMGUI_API UDreamVisual : public UDreamWidgetSubObjectBehaviour
{
	GENERATED_BODY()

public:	
	UDreamVisual(const FObjectInitializer& ObjectInitializer);
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
protected:
	friend class FDreamVisualCustomization;
	friend class UDreamWidget;
	virtual void PostReinitProperties() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	EDreamVisualType VisualType = EDreamVisualType::None;

	/**
	 * Render color of UI element.
	 */
	UPROPERTY(Interp, EditAnywhere, Category = "DreamGUI", Getter, Setter, BlueprintReadWrite)
	FColor Color = FColor::White;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Raycast")
	bool bRaycastTarget = true;
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Raycast", meta=(EditCondition=bRaycastTarget))
	EDreamVisualRaycastType RaycastType = EDreamVisualRaycastType::Rect;
	/** Custom raycast object to handle raycast behaviour when DreamUI do raycast hit test. Only valid if RaycastType is Custom. */
	UPROPERTY(EditAnywhere, Instanced, Category = "DreamGUI-Raycast")
	TObjectPtr<UDreamVisualCustomRaycast> CustomRaycastObject;
	/** Pixel's alpha value threshold, if hit a pixel which alpha value is less than this value, then hit test return false. */
	UPROPERTY(EditAnywhere, Category = "DreamGUI-Raycast")
	float VisiblePixelThreshold = 0.1f;

	virtual bool LineTraceUIRect(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const;
	virtual bool LineTraceUIGeometry(FDreamUIGeometry* InGeo, FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const;
	virtual bool LineTraceUICustom(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const;
	
	void UpdateGeometryWidgetPropertyData(TArray<struct FDreamUIMeshVertex>& InVertices, int InValidNumVertices, int InDataStartPosition);
public:
	static const FName GetPropertyName_Color()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamVisual, Color);
	}
	
	/** get visual type */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	EDreamVisualType GetVisualType()const { return VisualType; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	FColor GetColor() const { return Color; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetAlpha() const { return FDreamUIUtils::ByteToFloat01(Color.A); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetRaycastTarget()const{return bRaycastTarget;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamVisualRaycastType GetRaycastType()const { return RaycastType; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamVisualCustomRaycast* GetCustomRaycastObject()const { return CustomRaycastObject; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetColor(FColor Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetAlpha(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetRaycastTarget(bool Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetRaycastType(EDreamVisualRaycastType Value) { RaycastType = Value; }
	/** Set custom raycast object to handle raycast behaviour, only valid if RaycastType is Custom */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetCustomRaycastObject(UDreamVisualCustomRaycast* Value);

	uint8 GetFinalAlpha()const;
	/** get final alpha, calculated with CanvasGroup's alpha */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		float GetFinalAlpha01()const;
	/** get final color, calculated with CanvasGroup's alpha */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		FColor GetFinalColor()const;

	UFUNCTION(BlueprintCallable, Category = "DreamUI")
	virtual bool LineTraceUI(FDreamUIHitResult& OutHit, const FVector& Start, const FVector& End)const;

	int GetClipDataStartPosition()const;
	UTexture* GetClipDataTexture()const;

	virtual void OnPixelSnappingChanged(){};
	virtual void OnDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange){};
	virtual void OnTransformChanged(bool InPositionChanged, bool InScaleChanged);
	virtual void OnRenderCanvasChanged(UDreamCanvas* InOldCanvas, UDreamCanvas* InNewCanvas);
	
	void MarkColorDirty();
	void CheckClipDataStartPosition();
	virtual void MarkAllDirty();
	
	/** Called by DreamCanvas when begin to collect geometry for render */
	virtual void UpdateGeometry() {};
	/** Called by DreamCanvas after create MaterialInstanceDynamic for this object or it's draw-call */
	virtual void OnMaterialInstanceDynamicCreated(class UMaterialInstanceDynamic* mat) {};

	/** will this UI element affected by canvas's pixel perfect property? */
	virtual bool GetShouldAffectByPixelSnapping()const { return true; };
	/** return bounds min max point in self local space, for DreamCanvas to tell if geometry overlap with each other. */
	virtual void GetGeometryBoundsInLocalSpace(FVector2D& OutMinPoint, FVector2D& OutMaxPoint)const;
	/** editor only, return 3d bounds in self local space */
	virtual void GetGeometryBounds3DInLocalSpace(FVector& OutMinPoint, FVector& OutMaxPoint)const;
	
	/**
	 * The preferred width this layout element should be allocated if there is sufficient space.
	 * Can be -1 to ignore it.
	 */
	virtual float GetPreferredWidth()const{return -1;}
	/**
	 * The preferred height this layout element should be allocated if there is sufficient space.
	 * Can be -1 to ignore it.
	 */
	virtual float GetPreferredHeight()const{return -1;}

	static int WidgetPropertyDataLength;

	void SetWidgetPropertyDataStartPosition(int InPosition);
	bool IsRegisteredToCanvas()const{return WidgetPropertyDataStartPosition != INDEX_NONE;}
	int GetWidgetPropertyDataStartPosition()const{return WidgetPropertyDataStartPosition;}
protected:
	uint8 bColorChanged : 1;
	uint8 bTransformChanged : 1;
	uint8 bClipDataPositionChanged : 1;
	uint8 bWidgetPropertyDataStartPositionChanged : 1;
	uint8 bWidgetPropertyDataFontMarkDirty : 1;
	int ClipDataStartPosition = 0;
	int WidgetPropertyDataStartPosition = INDEX_NONE;

	void FillWidgetPropertyDataForMaterial(bool bNeedSize, bool bNeedCenterPosition)const;
	void FillWidgetPropertyDataForMaterial_ClipDataCoordinate(class UDreamUIDataAsTexture* DataAsTexture)const;
	// Fill initial mark data, only do this when first create widget property data or when render canvas changed
	void FillWidgetPropertyDataForMaterial_InitialMark(class UDreamUIDataAsTexture* DataAsTexture, uint8 FontMark)const;
public:
#pragma region TweenAnimation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* ColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* ColorFrom(FColor startValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* AlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
		UDreamTweener* AlphaFrom(float startValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
#pragma endregion
};
