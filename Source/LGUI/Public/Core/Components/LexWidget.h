// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "Components/SceneComponent.h"
#include "Components/PrimitiveComponent.h"
#include "LTweener.h"
#include "Core/LexWidgetTypes.h"
#include "PrefabSystem/ILGUIPrefabInterface.h"
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

UENUM(BlueprintType)
enum class ELexWidgetVisibility : uint8
{
	/** Visible and hit-testable (can interact with cursor). Default value. */
	Visible,
	/** Not visible and takes up no space in the layout (obviously not hit-testable). */
	Collapsed,
	/** Not visible but occupies layout space (obviously not hit-testable). */
	Hidden,
};

UENUM(BlueprintType)
enum class ELexWidgetHitTestType : uint8
{
	Inherit,
	HitTestable,
	NotHitTestable,
};

/**
 * Base class for almost all UI related things.
 */
UCLASS(HideCategories = ( LOD, Physics, Collision, Activation, Cooking, Rendering, Actor, Input, Lighting, Mobile, Navigation), ClassGroup = (LGUI), NotBlueprintable, meta = (BlueprintSpawnableComponent))
class LGUI_API ULexWidget : public USceneComponent, public ILGUIPrefabInterface
{
	GENERATED_BODY()

public:
	DECLARE_EVENT_ThreeParams(ULexWidget, FDimensionChangedEvent, bool/*PivotChanged*/, bool/*WidthChanged*/, bool/*HeightChanged*/);
	DECLARE_EVENT_FourParams(ULexWidget, FChildDimensionChangedEvent, ULexWidget*/*Child*/, bool/*PivotChanged*/, bool/*WidthChanged*/, bool/*HeightChanged*/);
	DECLARE_EVENT_OneParam(ULexWidget, FIsEnabledChangedEvent, bool/*IsEnabled*/);
	DECLARE_EVENT(ULexWidget, FAttachmentChangedEvent);
	DECLARE_EVENT(ULexWidget, FTransformChangedEvent);
	DECLARE_EVENT(ULexWidget, FSiblingIndexChangedEvent);
	DECLARE_EVENT(ULexWidget, FRenderVisibilityChangedEvent)
	DECLARE_EVENT(ULexWidget, FLayoutVisibilityChangedEvent)
	DECLARE_EVENT(ULexWidget, FHitTestVisibilityChangedEvent)
	
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
	static FName GetPropertyName_Width()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, Width);
	}
	static FName GetPropertyName_Height()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, Height);
	}
	static FName GetPropertyName_SiblingIndex()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, SiblingIndex);
	}
	static FName GetPropertyName_WidgetVisibility()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, WidgetVisibility);
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
	void Call_IsEnabledChanged();
	void Call_TransformChanged();
	void Call_DimensionsChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void Call_ChildDimensionsChanged(ULexWidget* Child, bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void Call_AttachmentChanged();
	void Call_SiblingIndexChanged();
	void Call_RenderVisibilityChanged();
	void Call_LayoutVisibilityChanged();
	void Call_HitTestVisibilityChanged();
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

	void UpdateLayout();
	void UpdateClip(ULexUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FLexUIClipData>>& ClipDataList);
	void UpdateVisual()const;
protected:
	void RenewRenderCanvasRecursive(ULexCanvas* InParentRenderCanvas);

private:
	FIsEnabledChangedEvent OnIsEnabledChangedEvent;
	FTransformChangedEvent OnTransformChangedEvent;
	FDimensionChangedEvent OnDimensionChangedEvent;
	FChildDimensionChangedEvent OnChildDimensionChangedEvent;
	FAttachmentChangedEvent OnAttachmentChangedEvent;
	FSiblingIndexChangedEvent OnSiblingIndexChangedEvent;
	FRenderVisibilityChangedEvent OnRenderVisibilityChangedEvent;
	FRenderVisibilityChangedEvent OnLayoutVisibilityChangedEvent;
	FRenderVisibilityChangedEvent OnHitTestVisibilityChangedEvent;
public:
	FIsEnabledChangedEvent& GetIsEnabledChangedEvent(){return OnIsEnabledChangedEvent;}
	FTransformChangedEvent& GetTransformChangedEvent(){return OnTransformChangedEvent;}
	FDimensionChangedEvent& GetDimensionChangedEvent(){return OnDimensionChangedEvent;}
	FChildDimensionChangedEvent& GetChildDimensionChangedEvent(){return OnChildDimensionChangedEvent;}
	FAttachmentChangedEvent& GetAttachmentChangedEvent(){return OnAttachmentChangedEvent;}
	FSiblingIndexChangedEvent& GetSiblingIndexChangedEvent(){return OnSiblingIndexChangedEvent;}
	FRenderVisibilityChangedEvent& GetRenderVisibilityChangedEvent(){return OnRenderVisibilityChangedEvent;}
	FRenderVisibilityChangedEvent& GetLayoutVisibilityChangedEvent(){return OnLayoutVisibilityChangedEvent;}
	FRenderVisibilityChangedEvent& GetHitTestVisibilityChangedEvent(){return OnHitTestVisibilityChangedEvent;}
protected:
	/** parent in hierarchy */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexWidget> UIParent = nullptr;
	/** root in hierarchy */
	mutable TWeakObjectPtr<ULexWidget> RootWidget = nullptr;//don't mark this Transactional, because undo or redo will call register/unregister, which will trigger check RootUIItem
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
	FLexWidgetMargin Padding;
	// Expand outward
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Size", Getter, Setter, meta = (AllowPrivateAccess = true))
	FLexWidgetMargin Margin;
	UPROPERTY(EditAnywhere, Getter, Setter, Category = "LGUI")
	FVector2D Pivot = FVector2D(0.5f, 0.5f);
	UPROPERTY(VisibleAnywhere, Transient, Getter, Category = "LGUI", AdvancedDisplay)
	FVector2D RenderSize = FVector2D(100, 100);
	UPROPERTY(VisibleAnywhere, Transient, Getter, Category = "LGUI", AdvancedDisplay)
	FMargin RenderMargin;
	UPROPERTY(VisibleAnywhere, Transient, Getter, Category = "LGUI", AdvancedDisplay)
	FMargin RenderPadding;
public:
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetPivot()const {return Pivot;}
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	void SetPivot(FVector2D Value);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	float GetRenderWidth() const{return GetRenderSize().X;}
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	float GetRenderHeight() const{return GetRenderSize().Y;}
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FVector2D GetRenderSize() const;
	float GetPreferredWidth() const;
	float GetPreferredHeight() const;
	FVector2D GetPreferredSize() const;

	void SetRenderSizeByLayout(FVector2D Value);

	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexWidgetAspectRatio GetAspectRatio()const{return AspectRatio;}
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexWidgetSize GetWidth()const { return Width; }
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLexWidgetSize GetHeight()const { return Height; }
	UFUNCTION(BlueprintCallable, Category = "Size")
	const FLexWidgetMargin& GetPadding()const{return Padding;}
	UFUNCTION(BlueprintCallable, Category = "Size")
	const FMargin& GetRenderPadding()const;
	UFUNCTION(BlueprintCallable, Category = "Size")
	const FLexWidgetMargin& GetMargin()const{return Margin;}
	const FMargin& GetRenderMargin()const;

	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetAspectRatio(const FLexWidgetAspectRatio& Value);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetWidth(const FLexWidgetSize& Value);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetHeight(const FLexWidgetSize& Value);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetSize(const FLexWidgetSize2& InValue);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetPadding(const FLexWidgetMargin& Value);
	UFUNCTION(BlueprintCallable, Category = "Size")
	void SetMargin(const FLexWidgetMargin& Value);

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
	/** Get LexCanvasScaler from root canvas, return null if not have one */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		class ULexCanvasScaler* GetCanvasScaler()const;

	/** mark all dirty for UI element to update, include all children */
	void MarkAllDirtyRecursive();
	virtual void MarkAllDirty();
	virtual void MarkRenderModeChangeRecursive(ULexCanvas* Canvas, ELexRenderMode OldRenderMode, ELexRenderMode NewRenderMode);
	
	void MarkTransformChanged(bool InPositionChanged, bool InScaleChanged);
	void MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void MarkRenderSizeChanged();
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
	ELexWidgetVisibility WidgetVisibility = ELexWidgetVisibility::Visible;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexWidgetHitTestType HitTestType = ELexWidgetHitTestType::Inherit;
	/** If the widget will draw snapped to the nearest pixel.  Improves clarity but might cause visible stepping in animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EWidgetPixelSnapping PixelSnapping = EWidgetPixelSnapping::Inherit;
	/** If the widget enable for interaction? */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetIsEnabled", Setter = "SetIsEnabled", meta = (AllowPrivateAccess = true))
	uint8 bIsEnabled : 1 = true;
	/**
	 * Restrict navigation area to only children of this UI node, to forbid it navigate out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetRestrictNavigationArea", Setter = "SetRestrictNavigationArea", meta = (AllowPrivateAccess = true))
	uint8 bRestrictNavigationArea : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Visual", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexVisual> Visual = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Layout", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexLayout> Layout = nullptr;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Instanced, Category = "LayoutSlot", Getter, meta = (AllowPrivateAccess = true))
	mutable TObjectPtr<ULexLayoutSlot> LayoutSlot = nullptr;
	
	TWeakPtr<FLexUIClipData> ClipData;
	
	uint8 bCacheFinalIsEnabled : 1 = true;
	void CalculateIsEnabled_Recursive();
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
	ELexWidgetVisibility GetWidgetVisibility()const { return WidgetVisibility; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ELexWidgetHitTestType GetHitTestType()const { return HitTestType; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsVisibleForRender()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsVisibleForHitTest()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsVisibleForLayout()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetWidgetVisibility(ELexWidgetVisibility Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetHitTestType(ELexWidgetHitTestType Value);

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
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta=(DeterminesOutputType="VisualClass"))
	ULexVisual* CreateNewVisual(TSubclassOf<ULexVisual> VisualClass);
	template<class T>
	T* CreateNewVisual()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexVisual>::Value, "'T' template parameter to CreateNewVisual must be derived from ULexVisual");
		return (T*)CreateNewVisual(T::GetClass());
	}

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexLayout* GetLayout()const { return Layout; }
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta=(DeterminesOutputType="LayoutClass"))
	ULexLayout* CreateNewLayout(TSubclassOf<ULexLayout> LayoutClass);
	template<class T>
	T* CreateNewLayout()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexLayout>::Value, "'T' template parameter to CreateNewLayout must be derived from ULexLayout");
		return (T*)CreateNewLayout(T::GetClass());
	}
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexLayoutSlot* GetLayoutSlot()const;

	const TWeakPtr<FLexUIClipData>& GetClipData()const{return ClipData;}

#if WITH_EDITOR
	void SetIsTemporarilyHiddenInEditor_Recursive_By_RenderVisibility();
#endif

#pragma region SiblingIndex
protected:
	/** hierarchy index, hierarchy order, render order */
	UPROPERTY(EditAnywhere, Category = LGUI)
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

	void MarkLayoutDirty();
protected:
	friend class FLexWidgetCustomization;
	friend class ULexCanvas;
	/** LexCanvas which render this UI element */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexCanvas> RenderCanvas = nullptr;
	/** is this widget actor contains LexCanvas component */
	UPROPERTY(Transient) mutable uint32 bIsCanvasWidget:1;

	mutable uint32 bLayoutDirty : 1 = true;
	mutable uint32 bRenderSizeDirty : 1 = true;
	mutable uint32 bRenderMarginDirty : 1 = true;
	mutable uint32 bRenderPaddingDirty : 1 = true;
	mutable uint32 bClipDirty : 1 = true;
	mutable uint32 bNeedRecreateClip : 1 = true;
	uint32 bClipDataChanged : 1 = true;
	
	uint32 bCacheIsVisibleForRender : 1 = true;
	uint32 bCacheIsVisibleForLayout : 1 = true;
	uint32 bCacheIsVisibleForHitTest : 1 = true;

	/** Only for root widget, if dirty then we need to recalculate flatten hierarchy index */
	mutable uint32 bFlattenHierarchyIndexDirty : 1;

	void CalculateRenderSize();
	void CalculateRenderMargin();
	void CalculateRenderPadding();
	float GetMarginPixelValue(const FLexWidgetMarginSize& MarginSize);
	void MarkClipDirty(bool InClipTypeChanged)const;
	
	/** find root UIItem of hierarchy */
	void CheckRootWidget(ULexWidget* RootWidgetInParent = nullptr);

	void CalculateVisibility_Recursive();
	void CalculateHitTest_Recursive();
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