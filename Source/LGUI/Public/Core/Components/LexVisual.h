// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LexWidget.h"
#include "LTweener.h"
#include "Utils/LexUIUtils.h"
#include "LexVisual.generated.h"

class FLexUIGeometry;
class UMaterialInterface;
class ULexCanvas;
class FLexUIDrawCall;
class ULexVisualCustomRaycast;
class ULexVisual;

/**
 * This component is only used when UIBaseRenderable's RaycastType = Custom
 */
UCLASS(BlueprintType, Blueprintable, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexVisualCustomRaycast :public UObject
{
	GENERATED_BODY()

protected:
	/**
	 * Called by UIBaseRenderable when do raycast hit test.
	 * @param	InVisual			The UIBaseRenderable object which call this Raycast function
	 * @param	InLocalSpaceRayStart	Ray start point in this UI's local space
	 * @param	InLocalSpaceRayEnd		Ray end point in this UI's local space
	 * @param	OutHitPoint				Hit point position in this UI's local space
	 * @param	OutHitNormal			Hit point normal in this UI's local space
	 * @return	true if hit this UI object
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "LGUI", meta = (DisplayName = "Raycast"))
		bool ReceiveRaycast(const ULexVisual* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector& OutHitPoint, FVector& OutHitNormal)const;
	/**
	 * Get pixel value at hit point.
	 * Only support UI element type which can read pixel value from texture:
	 *		1. UISprite which use LGUIStaticSpriteAtlasData can work perfectly.
	 *		2. UITexture can work with some specific setting.
	 *		3. UIText which use dynamic font can not work. (Currently all LGUI's built-in font is dynamic)
	 * Will fallback to Geometry if ui element not support this raycast type.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		static bool GetRaycastPixelFromUIBatchMeshVisual(const class ULexVisualBatchMesh* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector2D& OutUV, FColor& OutPixel, FVector& OutHitPoint, FVector& OutHitNormal);
public:
	/**
	 * Called by UIBaseRenderable when do raycast hit test.
	 * @param	InVisual			The UIBaseRenderable object which call this Raycast function
	 * @param	InLocalSpaceRayStart	Ray start point in this UI's local space
	 * @param	InLocalSpaceRayEnd		Ray end point in this UI's local space
	 * @param	OutHitPoint				Hit point position in this UI's local space
	 * @param	OutHitNormal			Hit point normal in this UI's local space
	 * @return	true if hit this UI object
	 */
	virtual bool Raycast(const ULexVisual* InVisual, const FVector& InLocalSpaceRayStart, const FVector& InLocalSpaceRayEnd, FVector& OutHitPoint, FVector& OutHitNormal)const;
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexVisualType :uint8
{
	None,
	BatchMesh,
	PostProcess,
	DirectMesh,
};

UENUM(BlueprintType, Category = LGUI)
enum class ELexVisualHitTestType :uint8
{
	/** Hit on rect range */
	Rect = 0,
	/** Hit on actual triangle mesh */
	Mesh = 1,
	/**
	 * Hit on main texture's pixel, if the pixel is not transparent then hit test success.
	 * Only support UI element type which can read pixel value from texture:
	 *		1. UISprite which use LGUIStaticSpriteAtlasData can work perfectly.
	 *		2. UITexture can work with some specific setting.
	 *		3. UIText which use dynamic font can not work. (Currently all LGUI's built-in font is dynamic)
	 * Will fallback to Mesh if ui element not support this raycast type.
	 */
	VisiblePixel = 3,
	/** Use a user defined UIRenderableCustomRaycast to process the raycast hit. */
	Custom = 2,
};

/** Base class of UI element that can be renderred by LGUICanvas */
UCLASS(Blueprintable, BlueprintType, Abstract, DefaultToInstanced, EditInlineNew)
class LGUI_API ULexVisual : public UObject
{
	GENERATED_BODY()

public:	
	ULexVisual(const FObjectInitializer& ObjectInitializer);

protected:
	friend class FLexVisualCustomization;
	friend class ULexWidget;
	virtual void BeginPlay();
	virtual void EndPlay(){};
	virtual void OnRegister(){};
	virtual void OnUnregister(){};
	virtual void DestroyComponent(){};
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif
	ELexVisualType VisualType = ELexVisualType::None;
	mutable TWeakObjectPtr<ULexWidget> CacheWidget;

	/**
	 * Render color of UI element.
	 * Color may be override by UISelectable(UIButton, UIToggle, UISlider ...), if UISelectable's transition set to "Color Tint".
	 */
	UPROPERTY(EditAnywhere, Category = "LGUI")
		FColor Color = FColor::White;
	/** Only valid if RaycastTarget is true. */
	UPROPERTY(EditAnywhere, Category = "LGUI-Raycast")
		ELexVisualHitTestType RaycastType = ELexVisualHitTestType::Rect;
	/** Custom raycast object to handle raycast behaviour when LGUI do raycast hit test. Only valid if RaycastTarget is true and RaycastType is Custom. */
	UPROPERTY(EditAnywhere, Instanced, Category = "LGUI-Raycast")
		TObjectPtr<ULexVisualCustomRaycast> CustomRaycastObject;
	/** Pixel's alpha value threshold, if hit a pixel which alpha value is less than this value, then hit test return false. */
	UPROPERTY(EditAnywhere, Category = "LGUI-Raycast")
		float VisiblePixelThreshold = 0.1f;

	virtual bool LineTraceUIRect(FHitResult& OutHit, const FVector& Start, const FVector& End)const;
	virtual bool LineTraceUIGeometry(FLexUIGeometry* InGeo, FHitResult& OutHit, const FVector& Start, const FVector& End)const;
	virtual bool LineTraceUICustom(FHitResult& OutHit, const FVector& Start, const FVector& End)const;
public:
	static const FName GetColorPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(ULexVisual, Color);
	}

	UFUNCTION(BlueprintCallable, Category = "Widget")
	ULexWidget* GetWidget()const;
	
	/** get UI renderable type */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ELexVisualType GetVisualType()const { return VisualType; }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		FColor GetColor() const { return Color; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetAlpha() const { return FLexUIUtils::Color255To1_Table[Color.A]; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ELexVisualHitTestType GetRaycastType()const { return RaycastType; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexVisualCustomRaycast* GetCustomRaycastObject()const { return CustomRaycastObject; }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetColor(FColor value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetAlpha(float value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetRaycastType(ELexVisualHitTestType Value) { RaycastType = Value; }
	/** Set custom raycast object to handle raycast behaviour, only valid if RaycastType is Custom */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		void SetCustomRaycastObject(ULexVisualCustomRaycast* Value);

	uint8 GetFinalAlpha()const;
	/** get final alpha, calculated with CanvasGroup's alpha */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		float GetFinalAlpha01()const;
	/** get final color, calculated with CanvasGroup's alpha */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		FColor GetFinalColor()const;

	UFUNCTION(BlueprintCallable, Category = "LexUI")
	virtual bool LineTraceUI(FHitResult& OutHit, const FVector& Start, const FVector& End)const;

	TSharedPtr<FLexUIDrawCall> DrawCall = nullptr;//drawcall that response for this UI.

	int GetClipDataStartPosition()const;
	UTexture* GetClipDataTexture()const;

	virtual void OnTransformChanged();
	virtual void OnAnchorChange(bool InPivotChange, bool InWidthChange, bool InHeightChange){};
	virtual void OnPixelSnappingChanged(){}
	virtual void OnClipDataChanged(){bClipDataChanged=true;}
	
	void MarkColorDirty();
	virtual void MarkAllDirty();
	
	/** Called by LGUICanvas when begin to collect geometry for render */
	virtual void UpdateGeometry() {};
	/** Called by LGUICanvas when clip type changed */
	virtual void UpdateMaterialClipType() {};
	/** Called by LGUICanvas after create MaterialInstanceDynamic for this object or it's drawcall */
	virtual void OnMaterialInstanceDynamicCreated(class UMaterialInstanceDynamic* mat) {};

	/** will this UI element affect by canvas's pixel perfect property? */
	virtual bool GetShouldAffectByPixelSnapping()const { return true; };
	/** return bounds min max point in self local space, for LGUICanvas to tell if geometry overlap with each other. */
	virtual void GetGeometryBoundsInLocalSpace(FVector2D& OutMinPoint, FVector2D& OutMaxPoint)const;
	/** editor only, return 3d bounds in self local space */
	virtual void GetGeometryBounds3DInLocalSpace(FVector& OutMinPoint, FVector& OutMaxPoint)const;
protected:
	uint8 bColorChanged : 1;
	uint8 bTransformChanged : 1;
	uint8 bClipDataChanged : 1;
public:
#pragma region TweenAnimation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
		ULTweener* ColorTo(FColor endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
		ULTweener* ColorFrom(FColor startValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
		ULTweener* AlphaTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
		ULTweener* AlphaFrom(float startValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
#pragma endregion
};
