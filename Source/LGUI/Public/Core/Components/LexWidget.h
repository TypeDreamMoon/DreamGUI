// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LGUI.h"
#include "Core/LexLayoutAnchorData.h"
#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "LTweener.h"
#include "Components/SlateWrapperTypes.h"
#include "Core/LexWidgetTypes.h"
#include "LexWidget.generated.h"

class ULexVisual;
class ULexLayoutSlot;
class ULexLayout;
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
	ClipToBoundsWithoutIntersecting UMETA(DisplayName = "Clip To Bounds - Without Intersecting (Advanced)"),
	/**
	 * This widget does not clip.
	 */
	Disabled UMETA(DisplayName = "No Clip"),
};

DECLARE_MULTICAST_DELEGATE_OneParam(FUIItemActiveInHierarchyStateChangedMulticastDelegate, bool);
DECLARE_DELEGATE_OneParam(FUIItemActiveInHierarchyStateChangedDelegate, bool);

/**
 * Base class for almost all UI related things.
 */
UCLASS(HideCategories = ( LOD, Physics, Collision, Activation, Cooking, Rendering, Actor, Input, Lighting, Mobile, Navigation), ClassGroup = (LGUI), NotBlueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexWidget : public USceneComponent
{
	GENERATED_BODY()

public:	
	ULexWidget(const FObjectInitializer& ObjectInitializer);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	static const FName GetSizePropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, RenderSize);
	}
	static const FName GetHierarchyIndexPropertyName()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, HierarchyIndex);
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
	
#pragma region LGUILifeCycleUIBehaviour
private:
	TInlineComponentArray<class ULGUILifeCycleUIBehaviour*> LGUILifeCycleUIBehaviourArray;
	void CallUILifeCycleBehavioursActiveInHierarchyStateChanged();
	void CallUILifeCycleBehavioursChildActiveInHierarchyStateChanged(ULexWidget* child, bool activeOrInactive);
	void CallUILifeCycleBehavioursDimensionsChanged(bool horizontalPositionChanged, bool verticalPositionChanged, bool widthChanged, bool heightChanged);
	void CallUILifeCycleBehavioursChildDimensionsChanged(ULexWidget* child, bool horizontalPositionChanged, bool verticalPositionChanged, bool widthChanged, bool heightChanged);
	void CallUILifeCycleBehavioursAttachmentChanged();
	void CallUILifeCycleBehavioursChildAttachmentChanged(ULexWidget* child, bool attachOrDetach);
	void CallUILifeCycleBehavioursInteractionStateChanged();
	void CallUILifeCycleBehavioursChildHierarchyIndexChanged(ULexWidget* child);
public:
	void AddLGUILifeCycleUIBehaviourComponent(class ULGUILifeCycleUIBehaviour* InComp) { LGUILifeCycleUIBehaviourArray.AddUnique(InComp); }
	void RemoveLGUILifeCycleUIBehaviourComponent(class ULGUILifeCycleUIBehaviour* InComp) { LGUILifeCycleUIBehaviourArray.RemoveSingleSwap(InComp); }
#pragma endregion LGUILifeCycleUIBehaviour
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
public:

	FDelegateHandle RegisterUIHierarchyChanged(const FSimpleDelegate& InCallback);
	void UnregisterUIHierarchyChanged(const FDelegateHandle& InHandle);
protected:
	/** UIItem's hierarchy changed */
	void UIHierarchyChanged(ULexCanvas* ParentRenderCanvas, ULexWidget* ParentRoot);
	FSimpleMulticastDelegate UIHierarchyChangedDelegate;
	/** called when RenderCanvas changed. */
	virtual void OnRenderCanvasChanged(ULexCanvas* OldCanvas, ULexCanvas* NewCanvas);
	void SetRenderCanvas(ULexCanvas* InNewCanvas);
public:
	/** Called by LGUICanvas, when a new LGUICanvas is registerred on self actor */
	void RegisterRenderCanvas(ULexCanvas* InRenderCanvas);
	/** Called by LGUICanvas, when LGUICanvas is unregisterred on self actor */
	void UnregisterRenderCanvas();

	void UpdateLayout();
	void UpdateClip(ULexUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FLexUIClipData>>& ClipDataList);
	void UpdateVisual()const;
protected:
	void RenewRenderCanvasRecursive(ULexCanvas* InParentRenderCanvas);

protected:
	/** parent in hierarchy */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexWidget> UIParent = nullptr;
	/** root in hierarchy */
	mutable TWeakObjectPtr<ULexWidget> RootUIItem = nullptr;//don't mark this Transactional, because undo or redo will call register/unregister, which will trigger check RootUIItem
	/** UI children array, sorted by hierarchy index */
	UPROPERTY(Transient) mutable TArray<TObjectPtr<ULexWidget>> UIChildren;
	/** check valid, incase unnormally deleting actor, like undo */
	void EnsureUIChildrenValid();
	void EnsureUIChildrenSorted()const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexWidgetAspectRatio AspectRatio;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexWidgetSize Width;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexWidgetSize Height;
	// Expand inward
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", Getter, Setter, meta = (AllowPrivateAccess = true))
	FMargin Padding;
	// Expand outward
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", Getter, Setter, meta = (AllowPrivateAccess = true))
	FMargin Margin;
	UPROPERTY(EditAnywhere, Getter, Setter, Category = "LGUI")
	FVector2D Pivot = FVector2D(0.5f, 0.5f);
	UPROPERTY(EditAnywhere, Getter, Category = "LGUI")
	FVector2D RenderSize = FVector2D(100, 100);
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetPivot()const {return Pivot;}
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	void SetPivot(FVector2D Value);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	float GetRenderWidth() const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	float GetRenderHeight() const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetRenderSize() const;

	void SetRenderSizeByLayout(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexWidgetAspectRatio GetAspectRatio()const{return AspectRatio;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexWidgetSize GetWidth()const { return Width; }
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexWidgetSize GetHeight()const { return Height; }
	UFUNCTION(BlueprintCallable, Category = "Size")
	const FMargin& GetPadding()const{return Padding;}
	UFUNCTION(BlueprintCallable, Category = "Size")
	const FMargin& GetMargin()const{return Margin;}

	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetAspectRatio(const FLexWidgetAspectRatio& Value);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetWidth(const FLexWidgetSize& Value);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetHeight(const FLexWidgetSize& Value);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetSize(const FLexWidgetSize2& InValue);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetPadding(const FMargin& Value);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetMargin(const FMargin& Value);

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

	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexWidget* GetUIParent()const{ return UIParent.Get(); }
	/** get UI children array, sorted by hierarchy index */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		const TArray<ULexWidget*>& GetUIChildren()const { EnsureUIChildrenSorted(); return UIChildren; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexWidget* GetUIChild(int index)const;
	/** Get root canvas of hierarchy */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexCanvas* GetRootCanvas()const;
	/** Get LGUICanvasScaler from root canvas, return null if not have one */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		class ULexCanvasScaler* GetCanvasScaler()const;

	/** mark all dirty for UI element to update, include all children */
	void MarkAllDirtyRecursive();
	virtual void MarkAllDirty();
	virtual void MarkRenderModeChangeRecursive(ULexCanvas* Canvas, ELexRenderMode OldRenderMode, ELexRenderMode NewRenderMode);
	
	void MarkTransformChanged(bool InPositionChanged, bool InScaleChanged);
	void MarkDimensionChanged(bool InPivotChange, bool InWidthChange, bool InHeightChange);
	void MarkParentDimensionChanged(bool InParentPivotChange, bool InParentWidthChange, bool InParentHeightChange);
	void MarkNeedUpdateLayout();
	void MarkSizeDirty_Recursive();
	void OnChildDimensionChanged(ULexWidget* InChild);
	virtual void MarkCanvasUpdate(bool bMaterialOrTextureChanged, bool bTransformOrVertexPositionChanged, bool bHierarchyOrderChanged, bool bForceRebuildDrawcall = false);
private:
	mutable uint8 bNeedSortUIChildren : 1;
	uint8 bIsDetaching : 1;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true, UIMin="0", UIMax="1"))
	float RenderOpacity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexWidgetClipping Clipping = ELexWidgetClipping::Inherit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	ESlateVisibility WidgetVisibility = ESlateVisibility::Visible;
	/** If the widget will draw snapped to the nearest pixel.  Improves clarity but might cause visible stepping in animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EWidgetPixelSnapping PixelSnapping = EWidgetPixelSnapping::Inherit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetIsEnabled", Setter = "SetIsEnabled", meta = (AllowPrivateAccess = true))
	uint8 bIsEnabled : 1 = true;
	/**
	 * Restrict navigation area to only children of this UI node, to forbid it navigate out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetRestrictNavigationArea", Setter = "SetRestrictNavigationArea", meta = (AllowPrivateAccess = true))
	uint8 bRestrictNavigationArea : 1 = false;

	UPROPERTY(EditAnywhere, Instanced, Category = "Visual", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexVisual> Visual = nullptr;
	UPROPERTY(EditAnywhere, Instanced, Category = "Layout", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexLayout> Layout = nullptr;
	UPROPERTY(VisibleAnywhere, Instanced, Category = "LayoutSlot", Getter, meta = (AllowPrivateAccess = true))
	mutable TObjectPtr<ULexLayoutSlot> LayoutSlot = nullptr;
	
	TWeakPtr<FLexUIClipData> ClipData;
	
	uint8 bCacheFinalIsEnabled : 1 = true;
	bool CalculateCacheFinalIsEnabled();
	void CheckIsEnabled_Recursive();
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ELexWidgetClipping GetClipping()const { return Clipping; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsPointVisibleOnClip(const FVector& Value)const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetClipping(ELexWidgetClipping Value);

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
	bool GetFinalPixelSnapping()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetPixelSnapping(EWidgetPixelSnapping Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ESlateVisibility GetWidgetVisibility()const { return WidgetVisibility; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsVisibleForRender()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsVisibleForHitTest()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsVisibleForLayout()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetWidgetVisibility(ESlateVisibility Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetIsEnabled()const { return bIsEnabled; }
	/**
	 * Get if this widget is interactable when use input interaction, considering all parent settings.
	 * @return If this widget is interactable
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetFinalIsEnabled()const{return bCacheFinalIsEnabled;}
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetIsEnabled(bool Value);
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
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetVisual(ULexVisual* Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexLayout* GetLayout()const { return Layout; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexLayoutSlot* GetLayoutSlot()const{return LayoutSlot;}
	ULexLayoutSlot* CheckAndGetLayoutSlot()const;

	const TWeakPtr<FLexUIClipData>& GetClipData()const{return ClipData;}
#pragma region UIActive
public:
	void CheckUIActiveState();
protected:
	/** all up parent IsUIActive is true, then this is true. if any up parent is false, then this is false */
	bool bAllUpParentUIActive = true;
	void CheckChildrenUIActiveRecursive(bool InUpParentUIActive);
	/**
	 * Active ui is visible and interactable.
	 * If parent or parent's parent... IsUIActive is false, then this ui is not visible and not interactable.
	 */
	UPROPERTY(EditAnywhere, Category = LGUI, meta = (DisplayName = "Is UI Active"))
		bool bIsUIActive = true;
	/** apply IsUIActive state */
	virtual void ApplyUIActiveState(bool InStateChange);
	void OnChildActiveStateChanged(ULexWidget* child);

	FUIItemActiveInHierarchyStateChangedMulticastDelegate UIActiveInHierarchyStateChangedDelegate;
	FSimpleMulticastDelegate OnTransformChanged;
public:
	FDelegateHandle RegisterUIActiveStateChanged(const FUIItemActiveInHierarchyStateChangedDelegate& InCallback);
	FDelegateHandle RegisterUIActiveStateChanged(const TFunction<void(bool)>& InCallback);
	void UnregisterUIActiveStateChanged(const FDelegateHandle& InHandle);

	FSimpleMulticastDelegate& GetTransformChangedEvent(){return OnTransformChanged;}

	/** Set this UI element's bIsUIActive */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		virtual void SetIsUIActive(bool active);
	/** is UI active itself, parent not count */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool GetIsUIActiveSelf()const { return bIsUIActive; }
	/** is UI active hierarchy. if all up parent of this ui item is active then return this->IsUIActive. if any up parent ui item is not active then return false */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		bool GetIsUIActiveInHierarchy()const { return bIsUIActive && bAllUpParentUIActive; };
#if WITH_EDITOR
	void SetIsTemporarilyHiddenInEditor_Recursive_By_IsUIActiveState();
#endif
#pragma endregion UIActive

#pragma region HierarchyIndex
protected:
	/** hierarchy index, hierarchy order, render order */
	UPROPERTY(EditAnywhere, Category = LGUI)
		int32 HierarchyIndex = INDEX_NONE;
	UPROPERTY(Transient, VisibleAnywhere, Category = LGUI, AdvancedDisplay)
	mutable int32 flattenHierarchyIndex = 0;
	void MarkFlattenHierarchyIndexDirty();
private:
	/** Only for RootUIItem */
	void RecalculateFlattenHierarchyIndex()const;
	void CalculateFlattenHierarchyIndex_Recursive(int& index)const;
	void ApplyHierarchyIndex();
public:
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetHierarchyIndex() const { return HierarchyIndex; }
	/** Get flatten hierarchy index, calculate from the first top most UIItem. */
	UFUNCTION(BlueprintCallable, Category = LGUI)
		int32 GetFlattenHierarchyIndex()const;
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetHierarchyIndex(int32 InInt);
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetAsFirstHierarchy();
	UFUNCTION(BlueprintCallable, Category = LGUI)
		void SetAsLastHierarchy();
#pragma endregion HierarchyIndex

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

	bool IsCanvasUIItem()const { return bIsCanvasUIItem; }

	/** return root UIItem in hierarchy, could be null if not initialized yet. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexWidget* GetRootUIItemInHierarchy()const { return RootUIItem.Get(); }
protected:
	friend class FLexWidgetCustomization;
	friend class ULexCanvas;
	/** LGUICanvas which render this UI element */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexCanvas> RenderCanvas = nullptr;
	/** is this UIItem's actor have LGUICanvas component */
	UPROPERTY(Transient) mutable uint8 bIsCanvasUIItem:1;

	mutable uint8 bRenderSizeDirty : 1 = true;
	mutable uint8 bClipDirty : 1 = true;
	mutable uint8 bNeedRecreateClip : 1 = true;
	uint8 bClipDataChanged : 1 = true;

	/** Only for RootUIItem, if dirty then we need to recalculate it */
	mutable uint8 bFlattenHierarchyIndexDirty : 1;
#if WITH_EDITOR
	uint8 bUIActiveStateDirty : 1;
#endif

	void CalculateRenderSize();
	void MarkClipDirty_Recursive(bool InClipTypeChanged)const;
	
	/** find root UIItem of hierarchy */
	void CheckRootUIItem(ULexWidget* RootUIItemInParent = nullptr);
public:
#pragma region TweenAnimation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* RenderOpacityTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);

#pragma endregion
public:
#if WITH_EDITORONLY_DATA
	/** This is a helper component for calculate bounds, so we can double click to focus on this UIItem */
	UPROPERTY(Transient, NonTransactional)TObjectPtr<class UUIItemEditorHelperComp> HelperComp = nullptr;//@todo: better way to replace this?
#endif
};


//Editor only
//This component is only a helper component for UIItem! Don't use this!
//For UIItem's bounds, so we can double click a UIItem and focus on it.
UCLASS(HideCategories = (LOD, Physics, Collision, Activation, Cooking, Rendering, Actor, Input, Lighting, Mobile), NotBlueprintable, NotBlueprintType, Transient)
class LGUI_API UUIItemEditorHelperComp : public UPrimitiveComponent
{
	GENERATED_BODY()
public:
	UUIItemEditorHelperComp();
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