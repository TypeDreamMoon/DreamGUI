// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "LTweener.h"
#include "Core/LexUIAnchorData.h"
#include "PrefabSystem/ILGUIPrefabInterface.h"
#include "LexWidget.generated.h"

class ULexVisual;
class ULexLayoutSelf;
class ULexLayoutContainer;
class FLexUIClipData;
class ULexUIDataAsTexture;
class ULexCanvas;
enum class ELexRenderMode : uint8;

UENUM(BlueprintType)
enum class ELexWidgetClipping : uint8
{
	/**
	 * This widget does not clip children, it and all children inherit the clipping area of the last widget that clipped.
	 */
	Inherit,
	/**
	 * This widget clips content the bounds of this widget.  It intersects those bounds with any previous clipping area.
	 */
	ClipToBounds,
	/**
	 * This widget clips to its bounds.  It does NOT intersect with any existing clipping UIGeometry, it pushes a new clipping 
	 * state.  Effectively allowing it to render outside the bounds of hierarchy that does clip.
	 */
	ClipToBoundsWithoutIntersecting UMETA(DisplayName = "Clip To Bounds - Without Intersecting"),
	/**
	 * This widget does not clip.
	 */
	Disabled UMETA(DisplayName = "No Clip"),
};

UENUM(BlueprintType)
enum class ELexWidgetRaycastableType : uint8
{
	//If no parent then use Enabled
	Inherit,
	Enabled,
	Disabled,
};

UENUM(BlueprintType)
enum class ELexWidgetInteractableType : uint8
{
	//If no parent then use Enabled
	Inherit,
	Enabled,
	Disabled,
};

/**
 * Base class for almost all UI related things.
 */
UCLASS(HideCategories = ( LOD, Physics, Collision, Activation, Cooking, Rendering, Actor, Input, Lighting, Mobile, Navigation), ClassGroup = (LGUI), NotBlueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexWidget : public USceneComponent, public ILGUIPrefabInterface
{
	GENERATED_BODY()

public:
	DECLARE_EVENT_OneParam(ULexWidget, FWidgetActiveChangedEvent, bool/*WidgetActive*/);
	DECLARE_EVENT_ThreeParams(ULexWidget, FDimensionChangedEvent, bool/*PivotChanged*/, bool/*WidthChanged*/, bool/*HeightChanged*/);
	DECLARE_EVENT_FourParams(ULexWidget, FChildDimensionChangedEvent, ULexWidget*/*Child*/, bool/*PivotChanged*/, bool/*WidthChanged*/, bool/*HeightChanged*/);
	DECLARE_EVENT(ULexWidget, FAttachmentChangedEvent);
	DECLARE_EVENT(ULexWidget, FTransformChangedEvent);
	DECLARE_EVENT(ULexWidget, FSiblingIndexChangedEvent);
	DECLARE_EVENT_OneParam(ULexWidget, FInteractableChangedEvent, bool/*Interactable*/);
	DECLARE_EVENT_OneParam(ULexWidget, FRaycastableChangedEvent, bool/*Raycastable*/)
	
	ULexWidget(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//begin LGUIPrefabInterface
	virtual void Awake_Implementation() override;
	virtual void EditorAwake_Implementation() override;
	//end LGUIPrefabInterface

	virtual void PostLoad()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual bool CanEditChange(const FEditPropertyChain& PropertyChain) const override;
	virtual void PostEditComponentMove(bool bFinished) override;
	virtual void PostEditUndo()override;
	//virtual void PostEditUndo(TSharedPtr<ITransactionObjectAnnotation> TransactionAnnotation)override;
	virtual void PostTransacted(const FTransactionObjectEvent& TransactionEvent)override;
	/** USceneComponent Interface. Only needed for show rect range in editor */
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	/** update UI immediately in edit mode */
	virtual void EditorForceUpdate();//@todo: remove this
	void EnsureDataForRebuild();
#endif
	static FName GetPropertyName_AnchorData()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, AnchorData);
	}
	static FName GetPropertyName_SiblingIndex()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, SiblingIndex);
	}
	static FName GetPropertyName_WidgetActive()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, bWidgetActive);
	}
	static FName GetPropertyName_DisplayName()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, DisplayName);
	}
	template<class T>
	static T* GetComponentInParentUI(AActor* InActor, bool IncludeUnregisteredComponent = true)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UActorComponent>::Value, "'T' template parameter to GetComponentInParent must be derived from UActorComponent");
		T* ResultComp = nullptr;
		AActor* ParentActor = InActor;
		while (IsValid(ParentActor)
			&& Cast<ULexWidget>(ParentActor->GetRootComponent()) != nullptr
			)
		{
			ResultComp = ParentActor->FindComponentByClass<T>();
			if (IsValid(ResultComp))
			{
				if (ResultComp->IsRegistered())
				{
					return ResultComp;
				}
				else
				{
					if (IncludeUnregisteredComponent)
					{
						return ResultComp;
					}
				}
			}
			ParentActor = ParentActor->GetAttachParentActor();
		}
		return nullptr;
	}
	
#pragma region CallbackEvents
private:
	void Call_WidgetActiveChanged();
	void Call_TransformChanged();
	void Call_DimensionsChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void Call_ChildDimensionsChanged(ULexWidget* Child, bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void Call_AttachmentChanged();
	void Call_SiblingIndexChanged();
	void Call_InteractableChanged();
	void Call_RaycastableChanged();
#pragma endregion
protected:
	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* Hit /* = NULL */, EMoveComponentFlags MoveFlags /* = MOVECOMP_NoFlags */, ETeleportType Teleport /* = ETeleportType::None */)override;
	virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport = ETeleportType::None)override;
	virtual void OnChildAttached(USceneComponent* ChildComponent)override;
	virtual void OnChildDetached(USceneComponent* ChildComponent)override;
	virtual void OnAttachmentChanged()override;
	virtual void OnRegister()override;
	virtual void OnUnregister()override;

	void OnUIDetachedFromParent();
	void OnUIAttachedToParent();

	/** UIItem's hierarchy changed */
	void UIHierarchyAttachmentChanged(ULexCanvas* ParentRenderCanvas, ULexWidget* ParentRoot);
	/** called when RenderCanvas changed. */
	virtual void OnRenderCanvasChanged(ULexCanvas* OldCanvas, ULexCanvas* NewCanvas);
	void SetRenderCanvas(ULexCanvas* InNewCanvas);
public:
	/** Called by LexCanvas, when a new LexCanvas is registered on self actor */
	void RegisterRenderCanvas(ULexCanvas* InRenderCanvas);
	/** Called by LexCanvas, when LexCanvas is unregistered on self actor */
	void UnregisterRenderCanvas();

	/** Called by LexCanvas to update layout */
	void UpdateLayout()const;
	/** Called by LexCanvas */
	void UpdateClip(ULexUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FLexUIClipData>>& ClipDataList);
	/** Called by LexCanvas */
	void UpdateVisual()const;
	
	void ForceUpdateLayout()const;
protected:
	void RenewRenderCanvasRecursive(ULexCanvas* InParentRenderCanvas);

private:
	FWidgetActiveChangedEvent OnWidgetActiveChangedEvent;
	FTransformChangedEvent OnTransformChangedEvent;
	FDimensionChangedEvent OnDimensionChangedEvent;
	FChildDimensionChangedEvent OnChildDimensionChangedEvent;
	FAttachmentChangedEvent OnAttachmentChangedEvent;
	FSiblingIndexChangedEvent OnSiblingIndexChangedEvent;
	FInteractableChangedEvent OnInteractableChangedEvent;
	FRaycastableChangedEvent OnRaycastableChangedEvent;
public:
	FWidgetActiveChangedEvent& GetWidgetActiveChangedEvent(){return OnWidgetActiveChangedEvent;}
	FTransformChangedEvent& GetTransformChangedEvent(){return OnTransformChangedEvent;}
	FDimensionChangedEvent& GetDimensionChangedEvent(){return OnDimensionChangedEvent;}
	FChildDimensionChangedEvent& GetChildDimensionChangedEvent(){return OnChildDimensionChangedEvent;}
	FAttachmentChangedEvent& GetAttachmentChangedEvent(){return OnAttachmentChangedEvent;}
	FSiblingIndexChangedEvent& GetSiblingIndexChangedEvent(){return OnSiblingIndexChangedEvent;}
	FInteractableChangedEvent& GetInteractableChangedEvent(){return OnInteractableChangedEvent;}
	FRaycastableChangedEvent& GetRaycastableChangedEvent(){return OnRaycastableChangedEvent;}
protected:
	/** parent in hierarchy */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexWidget> UIParent = nullptr;
	/** root in hierarchy */
	mutable TWeakObjectPtr<ULexWidget> RootWidget = nullptr;//don't mark this Transactional, because undo or redo will call register/unregister, which will trigger check RootUIItem
	/** UI children array, sorted by hierarchy index */
	UPROPERTY(Transient) mutable TArray<TObjectPtr<ULexWidget>> UIChildren;
	/** check valid, incase un-normally deleting actor, like undo */
	void EnsureUIChildrenValid();
	void EnsureUIChildrenSorted()const;

	/** AnchorData contains rect transform and color */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = "LGUI-AnchorData")
	FLexUIAnchorData AnchorData;

	//UPROPERTY(EditAnywhere, Transient, Getter="GetWidth", Setter="SetWidth", Category = "LGUI-AnchorData", DisplayName="Width")
	mutable float CacheWidth = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetHeight", Setter="SetHeight", Category = "LGUI-AnchorData", DisplayName="Height")
	mutable float CacheHeight = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorLeft", Setter="SetAnchorLeft", Category = "LGUI-AnchorData", DisplayName="AnchorLeft")
	mutable float CacheAnchorLeft = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorRight", Setter="SetAnchorRight", Category = "LGUI-AnchorData", DisplayName="AnchorRight")
	mutable float CacheAnchorRight = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorTop", Setter="SetAnchorTop", Category = "LGUI-AnchorData", DisplayName="AnchorTop")
	mutable float CacheAnchorTop = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorBottom", Setter="SetAnchorBottom", Category = "LGUI-AnchorData", DisplayName="AnchorBottom")
	mutable float CacheAnchorBottom = 0;
	
	mutable uint8 bCacheWidthDirty : 1 = true, bCacheHeightDirty : 1 = true, bCacheAnchorLeftDirty : 1 = true, bCacheAnchorRightDirty : 1 = true, bCacheAnchorTopDirty : 1 = true, bCacheAnchorBottomDirty : 1 = true;
	uint8 bCanSetAnchorFromTransform : 1 = false;
	
#pragma region AnchorData
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	const FLexUIAnchorData& GetAnchorData()const { return AnchorData; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetPivot() const { return AnchorData.Pivot; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetAnchorMin() const { return AnchorData.AnchorMin; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetAnchorMax() const { return AnchorData.AnchorMax; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetAnchoredPosition() const { return AnchorData.AnchoredPosition; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetSizeDelta() const { return AnchorData.SizeDelta; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	float GetHorizontalAnchoredPosition() const { return AnchorData.AnchoredPosition.X; }
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	float GetVerticalAnchoredPosition() const { return AnchorData.AnchoredPosition.Y; }

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetWidth() const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetHeight() const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetSize() const{return FVector2D(GetWidth(), GetHeight());}

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetAnchorLeft()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetAnchorTop()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetAnchorRight()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetAnchorBottom()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorData(const FLexUIAnchorData& Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetPivot(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorMin(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorMax(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation);

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize = false, bool bKeepRelativeLocation = false);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize = false, bool bKeepRelativeLocation = false);

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchoredPosition(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetHorizontalAnchoredPosition(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetVerticalAnchoredPosition(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetSizeDelta(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetHeight(float Value);

	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorLeft(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorTop(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorRight(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorBottom(float Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		FVector2D GetLocalSpaceLeftBottomPoint()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		FVector2D GetLocalSpaceRightTopPoint()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		FVector2D GetLocalSpaceCenter()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetLocalSpaceLeft()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetLocalSpaceRight()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetLocalSpaceBottom()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetLocalSpaceTop()const;
#pragma endregion
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexWidget* GetUIParent()const{ return UIParent.Get(); }
	/** get UI children array, sorted by hierarchy index */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const TArray<ULexWidget*>& GetUIChildren()const { EnsureUIChildrenSorted(); return UIChildren; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexWidget* GetUIChildByIndex(int index)const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	int GetIndexOfUIChild(ULexWidget* Child)const;
	/** Get root canvas of hierarchy */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexCanvas* GetRootCanvas()const;

	/** mark all dirty for UI element to update, include all children */
	void MarkAllDirtyRecursive();
	virtual void MarkAllDirty();
	virtual void MarkRenderModeChangeRecursive(ULexCanvas* Canvas, ELexRenderMode OldRenderMode, ELexRenderMode NewRenderMode);

	void CalculateAnchorFromTransform();
	void CalculateTransformFromAnchor();
	void CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged);
	
	void MarkTransformChanged(bool InPositionChanged, bool InScaleChanged);
	void MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void MarkAnchorDataChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache = true);
	virtual void MarkCanvasUpdate(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawCall = false)const;

private:
	float GetLayoutProperty(TFunctionRef<float(ULexLayoutSelf*)> GetLayoutSelfProperty
		, TFunctionRef<float(ULexLayoutContainer*)> GetLayoutContainerProperty
		, TFunctionRef<float(ULexVisual*)> GetVisualProperty
		, float DefaultValue)const;
	UObject* GetLayoutSource(TFunctionRef<float(ULexLayoutSelf*)> GetLayoutSelfProperty
		, TFunctionRef<float(ULexLayoutContainer*)> GetLayoutContainerProperty
		, TFunctionRef<float(ULexVisual*)> GetVisualProperty
		)const;
	
	FVector2f PrevScale2D = FVector2f::One();
	mutable uint8 bNeedSortUIChildren : 1;
	uint8 bIsDetaching : 1;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true, UIMin="0", UIMax="1"))
	float RenderOpacity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexWidgetClipping Clipping = ELexWidgetClipping::Inherit;
	TWeakPtr<FLexUIClipData> ClipData = nullptr;
	/**
	 * X- RightBottom, Y- RightTop, Z- LeftTop, W- LeftBottom
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition="Clipping!=ELexWidgetClipping::Disabled&&Clipping!=ELexWidgetClipping::Inherit"))
	FVector4f ClippingCornerRadius = FVector4f::Zero();
	/**
	 * Expand clip area outward.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition="Clipping!=ELexWidgetClipping::Disabled&&Clipping!=ELexWidgetClipping::Inherit"))
	FMargin ClippingMargin = FMargin(0);
	/**
	 * If not WidgetActive, then not visible, not take layout space, not interactable, not hit-testable
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetWidgetActive", Setter="SetWidgetActive", meta = (AllowPrivateAccess = true))
	bool bWidgetActive = true;
	/** If the widget will draw snapped to the nearest pixel.  Improves clarity but might cause visible stepping in animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EWidgetPixelSnapping PixelSnapping = EWidgetPixelSnapping::Inherit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexWidgetRaycastableType Raycastable = ELexWidgetRaycastableType::Inherit;
	/** If the widget enable for interaction? */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexWidgetInteractableType Interactable = ELexWidgetInteractableType::Inherit;
	/**
	 * Restrict navigation area to only children of this UI node, to forbid it navigate out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetRestrictNavigationArea", Setter = "SetRestrictNavigationArea", meta = (AllowPrivateAccess = true))
	uint8 bRestrictNavigationArea : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Visual", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexVisual> Visual = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "LayoutContainer", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexLayoutContainer> LayoutContainer = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "LayoutSelf", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexLayoutSelf> LayoutSelf = nullptr;
	
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ELexWidgetClipping GetClipping()const { return Clipping; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsPointVisibleOnClip(const FVector& Value)const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetClipping(ELexWidgetClipping Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	FVector4f GetClippingCornerRadius()const { return ClippingCornerRadius; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	FMargin GetClippingMargin()const { return ClippingMargin; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetClippingCornerRadius(FVector4f Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetClippingMargin(FMargin Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	float GetRenderOpacity()const { return RenderOpacity; }
	/**
	 * Retrieves the final opacity value used during rendering for this widget, considering all relevant settings and parent opacity.
	 * This value is influenced by the widget's own `RenderOpacity` property and hierarchical parent `RenderOpacity`.
	 *
	 * @return The calculated final opacity value for rendering this widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	float GetFinalRenderOpacity()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetRenderOpacity(float Value);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	EWidgetPixelSnapping GetPixelSnapping()const { return PixelSnapping; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetPixelSnappingInHierarchy()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetPixelSnapping(EWidgetPixelSnapping Value);

	/**
	 * Get WidgetActive property value
	 * @return WidgetActive self property
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetWidgetActive()const { return bWidgetActive; }
	/**
	 * Get widget active in hierarchy
	 * @return Is widget active in hierarchy
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetWidgetActiveInHierarchy()const;
	/**
	 * Set WidgetActive self property
	 * @param Value 
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetWidgetActive(bool Value);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ELexWidgetRaycastableType GetRaycastable()const { return Raycastable; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetRaycastable(ELexWidgetRaycastableType Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetRaycastableInHierarchy()const{return bCacheRaycastableInHierarchy;}

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ELexWidgetInteractableType GetInteractable()const { return Interactable; }
	/**
	 * Get if this widget is interactable when use input interaction, considering all parent settings.
	 * @return If this widget is interactable
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetInteractableInHierarchy()const{return bCacheInteractableInHierarchy;}
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetInteractable(ELexWidgetInteractableType Value);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetRestrictNavigationArea()const{return bRestrictNavigationArea;}

	/**
	 * Search up parent LexWidget which bRestrictNavigationArea is true and return it, include this LexWidget self
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	const ULexWidget* GetRestrictNavigationAreaWidget()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetRestrictNavigationArea(bool Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexVisual* GetVisual()const { return Visual; }
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta=(DeterminesOutputType="VisualClass"))
	ULexVisual* CreateNewVisual(TSubclassOf<ULexVisual> VisualClass);
	template<class T>
	T* CreateNewVisual()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexVisual>::Value, "'T' template parameter to CreateNewVisual must be derived from ULexVisual");
		return (T*)CreateNewVisual(T::StaticClass());
	}
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void RemoveVisual();
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexLayoutContainer* GetLayoutContainer()const { return LayoutContainer; }
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta=(DeterminesOutputType="LayoutClass"))
	ULexLayoutContainer* CreateNewLayoutContainer(TSubclassOf<ULexLayoutContainer> Class);
	template<class T>
	T* CreateNewLayoutContainer()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexLayoutContainer>::Value, "'T' template parameter to CreateNewLayoutContainer must be derived from ULexLayout");
		return (T*)CreateNewLayoutContainer(T::StaticClass());
	}
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void RemoveLayoutContainer();
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexLayoutSelf* GetLayoutSelf()const{return LayoutSelf;}
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta=(DeterminesOutputType="LayoutClass"))
	ULexLayoutSelf* CreateNewLayoutSelf(TSubclassOf<ULexLayoutSelf> Class);
	template<class T>
	T* CreateNewLayoutSelf()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexLayoutSelf>::Value, "'T' template parameter to CreateNewLayoutSelf must be derived from ULexLayout");
		return (T*)CreateNewLayoutSelf(T::StaticClass());
	}
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void RemoveLayoutSelf();

	const TWeakPtr<FLexUIClipData>& GetClipData()const{return ClipData;}

#if WITH_EDITOR
	void SetIsTemporarilyHiddenInEditor_Recursive_By_WidgetActive();
#endif

#pragma region SiblingIndex
protected:
	/** hierarchy index, hierarchy order, render order */
	UPROPERTY(EditAnywhere, Category = LGUI, AdvancedDisplay)
		int32 SiblingIndex = INDEX_NONE;
	UPROPERTY(Transient, VisibleAnywhere, Category = LGUI, AdvancedDisplay)
	mutable int32 FlattenHierarchyIndex = 0;
	void MarkFlattenHierarchyIndexDirty();
private:
	/** Only for RootUIItem */
	void RecalculateFlattenHierarchyIndex()const;
	void CalculateFlattenHierarchyIndex_Recursive(int& index)const;
	void ApplySiblingIndex();
public:
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetSiblingIndex() const { return SiblingIndex; }
	/** Get index order of the widget from top most widget in flatten hierarchy. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetFlattenHierarchyIndex()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetSiblingIndex(int32 InInt);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetAsFirstSibling();
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetAsLastSibling();
#pragma endregion SiblingIndex

#if WITH_EDITORONLY_DATA
private:
	UPROPERTY(EditAnywhere, Category = LGUI)
	bool bListChildrenInSceneOutliner = true;
public:
	void ApplyListChildrenInSceneOutliner();
#endif

#pragma region Name
private:
	/** 
	 * This is useful when you need to find child UI element by name, use function "FindChildByDisplayName" or "FindChildArrayByDisplayName" to do it.
	 * Mostly the displayName is the same as Actor's ActorLabel. If you want to change it, just change the actor label( Actor's name in world outliner).
	 * If Actor's ActorLabel start with "//", then the "//" will be ignored.
	 * ActorLabel is only valid in editor, but this is also valid on runtime.
	 */
	UPROPERTY(VisibleAnywhere, Category = LGUI, AdvancedDisplay)
		FString DisplayName;
public:
	UFUNCTION(BlueprintCallable, Category = LGUI)
		const FString& GetDisplayName()const { return DisplayName; }
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetDisplayName(const FString& InName) { DisplayName = InName; }
	/** 
	 * Search in children and return the first UIItem that the displayName match input name.
	 * Support hierarchy nested search, eg: InName = "Content/ListItem/NameLabel".
	 * @param InName	The child's name that need to find, case sensitive
	 * @param IncludeChildren	Also search in children
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		ULexWidget* FindChildByDisplayName(const FString& InName, bool IncludeChildren = false)const;
	/**
	 * Like "FindChildByDisplayName", but return all children that match the case.
	 * @param InName	The child's name that need to find, case sensitive
	 * @param IncludeChildren	Also search in children
	 */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		TArray<ULexWidget*> FindChildArrayByDisplayName(const FString& InName, bool IncludeChildren = false)const;
private:
	ULexWidget* FindChildByDisplayNameWithChildren_Internal(const FString& InName)const;
	void FindChildArrayByDisplayNameWithChildren_Internal(const FString& InName, TArray<ULexWidget*>& OutResultArray)const;
#pragma endregion Name

public:
	/** Get the canvas that render and update this UI element */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexCanvas* GetRenderCanvas() const;
	/** Is this UI element render to screen space overlay? */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool IsScreenSpaceOverlayUI()const;
	/** Is this UI element render to a RenderTarget? */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool IsRenderTargetUI()const;
	/** Is this UI element render in world space? */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		bool IsWorldSpaceUI()const;

	bool IsCanvasWidget()const { return bIsCanvasWidget; }

	/** return root UIItem in hierarchy, could be null if not initialized yet. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexWidget* GetRootWidgetInHierarchy()const { return RootWidget.Get(); }

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	static void MarkLayoutForRebuild(const ULexWidget* InWidget);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	static void ForceRebuildLayoutImmediately(const ULexWidget* InWidget);

protected:
	friend class FLexWidgetCustomization;
	friend class ULexCanvas;
	/** LexCanvas which render this UI element */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexCanvas> RenderCanvas = nullptr;
	/** is this widget actor contains LexCanvas component */
	UPROPERTY(Transient) mutable uint32 bIsCanvasWidget:1;

	mutable uint32 bLayoutDirty : 1 = true;
	mutable uint32 bClipDirty : 1 = true;
	mutable uint32 bNeedRecreateClip : 1 = true;
	
	uint32 bCacheWidgetActiveInHierarchy : 1 = true;
	uint8 bCacheInteractableInHierarchy : 1 = true;
	uint32 bCacheRaycastableInHierarchy : 1 = true;

	/** Only for root widget, if dirty then we need to recalculate flatten hierarchy index */
	mutable uint32 bFlattenHierarchyIndexDirty : 1;
	
	void MarkClipDirty(bool InClipTypeChanged)const;
	void MarkLayoutDirty()const;
	
	/** find root UIItem of hierarchy */
	void CheckRootWidget(ULexWidget* RootWidgetInParent = nullptr);

	void CalculateWidgetActive_Recursive();
	void CalculateInteractable_Recursive();
	void CalculateRaycastable_Recursive();
public:
#pragma region TweenAnimation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* RenderOpacityTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);

#pragma endregion
public:
#if WITH_EDITORONLY_DATA
	/** This is a helper component for calculate bounds, so we can double-click to focus on this UIItem */
	UPROPERTY(Transient, NonTransactional)TObjectPtr<class ULexWidgetEditorHelperComp> HelperComp = nullptr;//@todo: better way to replace this?
#endif
};


//Editor only
//This component is only a helper component for widget! Don't use this!
//For widget's bounds, so we can double-click a widget and focus on it.
UCLASS(HideCategories = (LOD, Physics, Collision, Activation, Cooking, Rendering, Actor, Input, Lighting, Mobile), NotBlueprintable, NotBlueprintType, Transient)
class LGUI_API ULexWidgetEditorHelperComp : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	ULexWidgetEditorHelperComp();
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
#if WITH_EDITOR
	virtual FPrimitiveSceneProxy* CreateSceneProxy()override;
#endif
	UPROPERTY(Transient)TObjectPtr<ULexWidget> Parent = nullptr;
	virtual UBodySetup* GetBodySetup()override;
	UPROPERTY(Transient)
		TObjectPtr<class UBodySetup> BodySetup;
	void UpdateBodySetup();
};