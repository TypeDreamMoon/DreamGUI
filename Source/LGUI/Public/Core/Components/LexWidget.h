// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "LTweener.h"
#include "InputCoreTypes.h"
#include "Core/LexUIAnchorData.h"
#include "LexWidget.generated.h"

class ULexWidgetSubObjectBehaviour;
class ULexUIBehaviour;
class ULexVisual;
class ULexLayoutSelf;
class ULexLayoutContainer;
class ULexPanelSlot;
class FLexUIClipData;
class ULexUIDataAsTexture;
class ULexCanvas;
enum class ELexRenderMode : uint8;

/** Matches UMG/Slate visibility while keeping WidgetActive as the behaviour lifecycle switch. */
UENUM(BlueprintType)
enum class ELexWidgetVisibility : uint8
{
	Visible,
	Hidden,
	Collapsed,
	HitTestInvisible UMETA(DisplayName = "Not Hit-Testable (Self & Children)"),
	SelfHitTestInvisible UMETA(DisplayName = "Not Hit-Testable (Self Only)"),
};

UENUM(BlueprintType)
enum class ELexAccessibleBehavior : uint8
{
	Auto,
	NotAccessible,
	Summary,
	Custom,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLexWidgetVisibilityChangedEvent, ELexWidgetVisibility, Visibility);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FLexWidgetFocusEvent, int32, UserIndex, int32, PointerId);

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

enum class ELexWidgetComponentsChangedType : uint8
{
	//New component added to this widget
	Added,
	//A component was removed from this widget
	Removed,
	//Component reordered
	Reorder,
};

/**
 * Base class for almost all UI related things.
 */
UCLASS(ClassGroup = (LGUI), BlueprintType, Blueprintable)
class LGUI_API ULexWidget : public UObject
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
	DECLARE_EVENT_OneParam(ULexWidget, FComponentsChangedEvent, ELexWidgetComponentsChangedType/*ChangedType*/)
	
	ULexWidget();

	void OnRegister();
	void OnUnregister();

	void BeginPlay();
	void EndPlay();

	virtual void PostLoad()override;
	virtual void BeginDestroy() override;

	UFUNCTION(BlueprintCallable, Category = "Widget")
	void DestroyWidget();

	virtual UWorld* GetWorld() const override final;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual bool CanEditChange(const FEditPropertyChain& PropertyChain) const override;
	virtual void PostEditUndo()override;
	virtual void PostRename(UObject* OldOuter, const FName OldName) override;

	void EnsureChildrenAfterTransaction();
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
	static FName GetPropertyName_Visibility()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, Visibility);
	}
	static FName GetPropertyName_DisplayName()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, DisplayName);
	}
	static FName GetPropertyName_RelativeLocation()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeLocation);
	}
	static FName GetPropertyName_RelativeRotation()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeRotation);
	}
	static FName GetPropertyName_RelativeScale()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, RelativeScale);
	}
	static FName GetPropertyName_Components()
	{
		return GET_MEMBER_NAME_CHECKED(ULexWidget, Components);
	}

	bool HasBegunPlay()const{return bHasBegunPlay;}
	bool HasRegistered()const{return bIsRegistered;}

	static void CollectChildrenWidgets(ULexWidget* Target, TArray<ULexWidget*>& OutAllChildrenWidgets, bool IncludeTarget = true);

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
private:
	/** Local space position */
	UPROPERTY(Interp, BlueprintReadOnly, Getter, Setter, meta=(AllowPrivateAccess = true))
	FVector RelativeLocation = FVector::ZeroVector;
	/**
	 * Local space rotation.
	 * Not marked Interp: Sequencer has no property track for FQuat, so this cannot be keyed
	 * directly. Animate RelativeRotationEuler instead, which mirrors this value.
	 */
	UPROPERTY(BlueprintReadOnly, Getter, Setter, meta = (AllowPrivateAccess = true))
	FQuat RelativeRotation = FQuat::Identity;
	/**
	 * Local space rotation as euler angles, mirroring RelativeRotation so that rotation can be
	 * animated: Sequencer has an FRotator property track but none for FQuat.
	 *
	 * Transient, because RelativeRotation stays the serialized source of truth. Sequencer reads
	 * property memory directly while writing through the setter, so this has to be a real stored
	 * field kept in sync rather than a value derived on demand.
	 */
	UPROPERTY(Interp, Transient, BlueprintReadOnly, Getter, Setter, meta = (AllowPrivateAccess = true))
	FRotator RelativeRotationEuler = FRotator::ZeroRotator;
	/** Local space scale */
	UPROPERTY(Interp, BlueprintReadOnly, Getter, Setter, meta = (AllowPrivateAccess = true, AllowPreserveRatio))
	FVector RelativeScale = FVector::OneVector;

public:
	UFUNCTION(BlueprintCallable, Category = "Transform")
	const FVector& GetRelativeLocation()const { return RelativeLocation; }
	UFUNCTION(BlueprintCallable, Category = "Transform")
	const FQuat& GetRelativeRotation()const { return RelativeRotation; }
	/** Euler-angle mirror of GetRelativeRotation, for animating rotation through Sequencer. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	const FRotator& GetRelativeRotationEuler()const { return RelativeRotationEuler; }
	UFUNCTION(BlueprintCallable, Category = "Transform")
	const FVector& GetRelativeScale()const { return RelativeScale; }

	UFUNCTION(BlueprintCallable, Category = "Transform")
	FVector GetWorldLocation()const;
	UFUNCTION(BlueprintCallable, Category = "Transform")
	FQuat GetWorldRotation()const;
	UFUNCTION(BlueprintCallable, Category = "Transform")
	FVector GetWorldScale()const;

	UFUNCTION(BlueprintCallable, Category = "Transform")
	FVector GetForwardVector()const;
	UFUNCTION(BlueprintCallable, Category = "Transform")
	FVector GetRightVector()const;
	UFUNCTION(BlueprintCallable, Category = "Transform")
	FVector GetUpVector()const;

	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetRelativeLocation(const FVector& Value);
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetRelativeRotation(const FQuat& Value);
	/** Set rotation from euler angles. This is the rotation entry point Sequencer drives. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetRelativeRotationEuler(const FRotator& Value);
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetRelativeScale(const FVector& Value);
	/** Transient scale contributed by a parent layout. It is never serialized or exposed as authored transform data. */
	void SetLayoutScale(const FVector2f& Value);
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetRelativeLocationAndRotation(const FVector& InLocation, const FQuat& InRotation);

	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetWorldLocation(const FVector& Value);
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetWorldRotation(const FQuat& Value);
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetWorldLocationAndRotation(const FVector& InLocation, const FQuat& InRotation);

	FTransform GetLocalTransform()const;
	UFUNCTION(BlueprintCallable, Category = "Transform")
	const FTransform& GetWorldTransform()const;

	void SetWorldTransform(const FTransform& InWorldTransform);
	/** Only called by PrefabSystem to restore parent-children hierarchy */
	void SetParentBeforeRegister(ULexWidget* InParent);
	/** Restores a serialized hierarchy while preserving legacy over-capacity assets. Cycle checks still apply. */
	bool SetParentFromPrefab(ULexWidget* InParent, bool InKeepWorldPosition = false, int InSiblingIndex = -1);
	/**
	 * PrefabSystem only: order every children array by the restored sibling indices (stable across holes
	 * and duplicates) and renumber contiguously, so later appends can never collide with restored values.
	 */
	void ApplySiblingIndexFromPrefab_Recursive();
	/**
	 * PrefabSystem only: re-assert a deserialized sibling index after an attach overwrote it with a tail
	 * index, deferring the reorder to the parent's lazy sort.
	 */
	void RestoreSiblingIndexFromPrefab(int32 InSiblingIndex);

	UFUNCTION(BlueprintCallable, Category = "Transform")
	ULexWidget* GetParent()const { return Parent.Get(); }
	/**
	 * Set parent of this widget, could use null to detach it from origin parent.
	 * @InKeepWorldPosition true - keep world position & rotation & scale after change parent, false - keep relative position & rotation & scale.
	 * @InSiblingIndex if InParent is a valid widget, then put the widget at specific index in parent's children list, -1 or other out of range value means the widget will be put at tail.
	 */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetParent(ULexWidget* InParent, bool InKeepWorldPosition = true, int InSiblingIndex = -1);
	/** Same operation as SetParent, but reports capacity/cycle rejection to the caller. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	bool TrySetParent(ULexWidget* InParent, bool InKeepWorldPosition = true, int InSiblingIndex = -1);
	/** Minimum child capacity imposed by the current LayoutContainer and Behaviours. INDEX_NONE is unlimited. */
	int32 GetMaxChildrenCapacity() const;
	bool CanAcceptAdditionalChildren(int32 AdditionalChildCount = 1) const;
	bool CanAcceptChildren(TConstArrayView<ULexWidget*> InChildren) const;
	UFUNCTION(BlueprintPure, Category = "Transform")
	bool CanAcceptChild(const ULexWidget* InChild) const;
	/** Set the sibling index of this widget in its parent's children list. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetSiblingIndex(int Value);
	/** Recurses up the list of parents and returns true if this widget is a descendant of the InTarget. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	bool IsChildOf(const ULexWidget* InTarget)const;
	UFUNCTION(BlueprintCallable, Category = "Transform")
	const TArray<ULexWidget*>& GetChildren()const
	{
		EnsureUIChildrenSorted();
		return Children;
	}
	UFUNCTION(BlueprintCallable, Category = "Transform")
	int GetChildrenCount()const { return Children.Num(); }

	/**
	 * UMG's UPanelWidget::AddChild, adapted. UMG needs eight typed variants because each panel has
	 * its own slot class; here there is one ULexPanelSlot, so one verb returns it and you can set
	 * padding straight off the result.
	 *
	 * Returns the child's slot, or null -- which means one of two very different things, so check
	 * the child's parent if you need to tell them apart: a parent with no layout container takes
	 * children happily and simply has no slots to give (UMG cannot even express this), whereas a
	 * refusal leaves the child exactly where it was. Refusals are a full panel, a cycle, or null.
	 *
	 * Passing a child this widget already owns is a reorder: the slot and everything on it survive,
	 * matching what dragging a widget within one panel in the editor does.
	 */
	UFUNCTION(BlueprintCallable, Category = "Transform", meta = (AdvancedDisplay = "InSiblingIndex"))
	ULexPanelSlot* AddChild(ULexWidget* InChild, int32 InSiblingIndex = -1);
	
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	const TArray<ULexUIBehaviour*>& GetAllComponents()const{return Components;}
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta = (ComponentClass = "/Script/LGUI.LexUIBehaviour", DeterminesOutputType = "ComponentClass"))
	TArray<ULexUIBehaviour*> GetComponents(TSubclassOf<ULexUIBehaviour> ComponentClass)const;
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta = (ComponentClass = "/Script/LGUI.LexUIBehaviour", DeterminesOutputType = "ComponentClass"))
	ULexUIBehaviour* GetComponent(TSubclassOf<ULexUIBehaviour> ComponentClass)const;
	template<class T>
	T* GetComponent()const
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexUIBehaviour>::Value, "'T' template parameter to GetComponent must be derived from ULexUIBehaviour");
		return Cast<T>(GetComponent(T::StaticClass()));
	}
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta = (DeterminesOutputType = "InterfaceClass"))
	ULexUIBehaviour* GetComponentByInterface(UClass* InterfaceClass)const;
	template<class T>
	T* GetComponentInParent(bool bIncludeSelf = false)const
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexUIBehaviour>::Value, "'T' template parameter to GetComponentInParent must be derived from ULexUIBehaviour");
		T* ResultComp = nullptr;
		auto ParentWidget = bIncludeSelf ? this : this->GetParent();
		while (IsValid(ParentWidget))
		{
			ResultComp = ParentWidget->GetComponent<T>();
			if (IsValid(ResultComp))
			{
				return ResultComp;
			}
			ParentWidget = ParentWidget->GetParent();
		}
		return nullptr;
	}
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta = (ComponentClass = "/Sript/LGUI.LexUIBehaviour", DeterminesOutputType = "ComponentClass"))
	ULexUIBehaviour* AddComponent(TSubclassOf<ULexUIBehaviour> ComponentClass);
	ULexUIBehaviour* AddComponentByTemplate(ULexUIBehaviour* ComponentTemplate);
	template<class T>
	T* AddComponent()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexUIBehaviour>::Value, "'T' template parameter to GetComponent must be derived from ULexUIBehaviour");
		return Cast<T>(AddComponent(T::StaticClass()));
	}
	template<class T>
	T* AddComponentByTemplate(ULexUIBehaviour* ComponentTemplate)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const ULexUIBehaviour>::Value, "'T' template parameter to GetComponent must be derived from ULexUIBehaviour");
		return Cast<T>(AddComponent(T::StaticClass(), ComponentTemplate));
	}
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta = (ComponentClass = "/Sript/LGUI.LexUIBehaviour"))
	void RemoveComponent(ULexUIBehaviour* Component);
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta = (ComponentClass = "/Sript/LGUI.LexUIBehaviour"))
	void MoveComponentToIndex(ULexUIBehaviour* Component, int32 NewIndex);
	void UpdateObjectToWorldTransform();
	void CalculateObjectToWorldTransform(bool bPropagateToChildren = true);
private:
	ULexUIBehaviour* AddComponent(TSubclassOf<ULexUIBehaviour> ComponentClass, ULexUIBehaviour* ComponentTemplate);
	bool TrySetParentInternal(ULexWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex, bool bEnforceCapacity);
	
	mutable FTransform ObjectToWorldTransform;

	virtual void OnUpdateTransform();
	virtual void OnChildAttached(ULexWidget* ChildComponent);
	virtual void OnChildDetached(ULexWidget* ChildComponent);

	void OnDetachedFromParent();
	void OnAttachedToParent();

	/** UIItem's hierarchy changed */
	void OnHierarchyAttachmentChanged(ULexCanvas* ParentRenderCanvas, ULexWidget* ParentRoot);
	/** called when RenderCanvas changed. */
	virtual void OnRenderCanvasChanged(ULexCanvas* OldCanvas, ULexCanvas* NewCanvas);
	void SetRenderCanvas(ULexCanvas* InNewCanvas);

	UPROPERTY(Instanced)
	TArray<TObjectPtr<ULexUIBehaviour>> Components;
public:
	/** Called by LexCanvas, when a new LexCanvas is registered on self actor */
	void RegisterRenderCanvas(ULexCanvas* InRenderCanvas);
	/** Called by LexCanvas, when LexCanvas is unregistered on self actor */
	void UnregisterRenderCanvas();

	/** Update layout */
	void UpdateLayout();
	/** Called by LexCanvas */
	void UpdateClip(ULexUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FLexUIClipData>>& ClipDataList);
	/** Called by LexCanvas */
	void UpdateVisual()const;
	
	void ForceUpdateLayout();
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
	FComponentsChangedEvent OnComponentsChangedEvent;
public:
	FWidgetActiveChangedEvent& GetWidgetActiveChangedEvent(){return OnWidgetActiveChangedEvent;}
	FTransformChangedEvent& GetTransformChangedEvent(){return OnTransformChangedEvent;}
	FDimensionChangedEvent& GetDimensionChangedEvent(){return OnDimensionChangedEvent;}
	FChildDimensionChangedEvent& GetChildDimensionChangedEvent(){return OnChildDimensionChangedEvent;}
	FAttachmentChangedEvent& GetAttachmentChangedEvent(){return OnAttachmentChangedEvent;}
	FSiblingIndexChangedEvent& GetSiblingIndexChangedEvent(){return OnSiblingIndexChangedEvent;}
	FInteractableChangedEvent& GetInteractableChangedEvent(){return OnInteractableChangedEvent;}
	FRaycastableChangedEvent& GetRaycastableChangedEvent(){return OnRaycastableChangedEvent;}
	FComponentsChangedEvent& GetComponentsChangedEvent(){return OnComponentsChangedEvent;}
protected:
	/** parent in hierarchy */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexWidget> Parent = nullptr;
	/** root in hierarchy */
	mutable TWeakObjectPtr<ULexWidget> RootWidget = nullptr;//don't mark this Transactional, because undo or redo will call register/unregister, which will trigger check RootUIItem
	/** UI children array, sorted by hierarchy index */
	UPROPERTY(Transient) mutable TArray<TObjectPtr<ULexWidget>> Children;
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
	mutable float CacheAnchorOffsetLeft = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorRight", Setter="SetAnchorRight", Category = "LGUI-AnchorData", DisplayName="AnchorRight")
	mutable float CacheAnchorOffsetRight = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorTop", Setter="SetAnchorTop", Category = "LGUI-AnchorData", DisplayName="AnchorTop")
	mutable float CacheAnchorOffsetTop = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorBottom", Setter="SetAnchorBottom", Category = "LGUI-AnchorData", DisplayName="AnchorBottom")
	mutable float CacheAnchorOffsetBottom = 0;
	
	mutable uint8 bCacheWidthDirty : 1 = true, bCacheHeightDirty : 1 = true,
	bCacheAnchorOffsetLeftDirty : 1 = true, bCacheAnchorOffsetRightDirty : 1 = true,
	bCacheAnchorOffsetTopDirty : 1 = true, bCacheAnchorOffsetBottomDirty : 1 = true;
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
		float GetAnchorOffsetLeft()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetAnchorOffsetTop()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetAnchorOffsetRight()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		float GetAnchorOffsetBottom()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	FMargin GetAnchorOffset()const;

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorData(const FLexUIAnchorData& Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetPivot(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorMin(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorMax(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
	void SetAnchorOffset(FMargin Value);

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
	void SetAnchoredPositionAndSizeDelta(FVector2D Position, FVector2D Size);

	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetHeight(float Value);

	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorOffsetLeft(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorOffsetTop(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorOffsetRight(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "LGUI-AnchorData")
		void SetAnchorOffsetBottom(float Value);

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
	ULexWidget* GetChildByIndex(int index)const;
	/** Get root canvas of hierarchy */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexCanvas* GetRootCanvas()const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	USceneComponent* GetAttachedRootSceneComponent()const;

	/** mark all dirty for UI element to update, include all children */
	void MarkAllDirtyRecursive();
	virtual void MarkAllDirty();
	virtual void MarkRenderModeChangeRecursive(ULexCanvas* Canvas, ELexRenderMode OldRenderMode, ELexRenderMode NewRenderMode);

	void CalculateAnchorFromTransform();
	void CalculateTransformFromAnchor();
	void CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged);
	
	void MarkTransformChanged();
	void MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void MarkAnchorDataChanged_Recursive(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache = true, bool InPropagateToChildren = true);
	virtual void MarkCanvasUpdate(bool bRebuildDrawCall)const;

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetPositionAndSizeForLayoutAnimation(FVector2D Position, FVector2D Size);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetPositionForLayoutAnimation(FVector2D Position);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetSizeForLayoutAnimation(FVector2D Position);
	void MarkAnchorDataChangedByLayoutContainer_Recursive(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache = true, bool InPropagateToChildren = true);
private:
	float GetLayoutProperty(TFunctionRef<float(ULexLayoutSelf*)> GetLayoutSelfProperty
		, TFunctionRef<float(ULexLayoutContainer*)> GetLayoutContainerProperty
		, TFunctionRef<float(ULexVisual*)> GetVisualProperty
		, float DefaultValue)const;
	UObject* GetLayoutSource(TFunctionRef<float(ULexLayoutSelf*)> GetLayoutSelfProperty
		, TFunctionRef<float(ULexLayoutContainer*)> GetLayoutContainerProperty
		, TFunctionRef<float(ULexVisual*)> GetVisualProperty
		)const;
	
	FVector2D PrevLocation2D = FVector2D::Zero();
	FVector2D PrevScale2D = FVector2D::One();
	mutable uint8 bNeedSortUIChildren : 1;
	uint8 bIsAttaching : 1 = false;

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
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "LGUI")
	bool bUniformSetClippingCornerRadius = true;
#endif
	/**
	 * If not WidgetActive, then not visible, not take layout space, not interactable, not hit-testable
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetWidgetActive", Setter="SetWidgetActive", meta = (AllowPrivateAccess = true))
	bool bWidgetActive = true;
	/** Controls layout, painting and hit testing independently from WidgetActive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetVisibility", Setter = "SetVisibility", meta = (AllowPrivateAccess = true))
	ELexWidgetVisibility Visibility = ELexWidgetVisibility::Visible;
#if WITH_EDITORONLY_DATA
	/** Transient preview state restored from the owning prefab editor data. */
	bool bHiddenInDesigner = false;
#endif
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
	/**
	 * Ignore parent layout container
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "LGUI", Getter = "GetIgnoreLayout", Setter = "SetIgnoreLayout", meta = (AllowPrivateAccess = true))
	bool bIgnoreLayout = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Visual", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexVisual> Visual = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "LayoutContainer", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexLayoutContainer> LayoutContainer = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "LayoutSelf", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexLayoutSelf> LayoutSelf = nullptr;
	/** Parent-panel-owned layout data. This is separate from LayoutSelf so legacy Flex/Grid assets remain valid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "PanelSlot", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<ULexPanelSlot> PanelSlot = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Focus", Getter = "GetIsFocusable", Setter = "SetIsFocusable", meta = (AllowPrivateAccess = true))
	bool bIsFocusable = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", Getter, Setter, meta = (AllowPrivateAccess = true))
	TEnumAsByte<EMouseCursor::Type> Cursor = EMouseCursor::Default;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", Getter, Setter, meta = (AllowPrivateAccess = true, MultiLine = true))
	FText ToolTipText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility", Getter, Setter, meta = (AllowPrivateAccess = true))
	ELexAccessibleBehavior AccessibleBehavior = ELexAccessibleBehavior::Auto;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility", Getter, Setter, meta = (AllowPrivateAccess = true, MultiLine = true))
	FText AccessibleText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility", Getter, Setter, meta = (AllowPrivateAccess = true, MultiLine = true))
	FText AccessibleSummaryText;

	UPROPERTY(BlueprintAssignable, Category = "LGUI|Visibility")
	FLexWidgetVisibilityChangedEvent OnVisibilityChanged;
	UPROPERTY(BlueprintAssignable, Category = "LGUI|Focus")
	FLexWidgetFocusEvent OnFocusReceived;
	UPROPERTY(BlueprintAssignable, Category = "LGUI|Focus")
	FLexWidgetFocusEvent OnFocusLost;

public:
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ELexWidgetClipping GetClipping()const
	{
		return bHasLayoutClippingOverride && Clipping == ELexWidgetClipping::Inherit
			? LayoutClippingOverride
			: Clipping;
	}
	ELexWidgetClipping GetAuthoredClipping()const { return Clipping; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool IsPointVisibleOnClip(const FVector& Value)const;
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetClipping(ELexWidgetClipping Value);
	/** Applies a transient layout default while authored clipping remains Inherit. */
	void SetLayoutClippingOverride(ELexWidgetClipping Value);
	void ClearLayoutClippingOverride();
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
	 * True between creation and being added to something. Suppresses the widget exactly the way an
	 * inactive flag would -- nothing draws, no behaviour is enabled -- but it is a separate bit on
	 * purpose: borrowing bWidgetActive would make SetWidgetActive silently useless during the very
	 * window in which callers configure a widget. Managed by the creation verbs; not serialized.
	 */
	bool IsParked()const { return bParked; }
	void SetParked(bool Value);
	/**
	 * Get widget active in hierarchy
	 * @return Is widget active in hierarchy
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetWidgetActiveInHierarchy()const;
#if WITH_EDITOR
	/** Editor-preview visibility. This never changes the serialized runtime WidgetActive value. */
	bool GetHiddenInDesigner()const { return bHiddenInDesigner; }
	void SetHiddenInDesigner(bool bHidden);
#endif
	/**
	 * Set WidgetActive self property
	 * @param Value 
	 */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetWidgetActive(bool Value);

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ELexWidgetVisibility GetVisibility()const { return Visibility; }
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetVisibility(ELexWidgetVisibility Value);
	/** Temporarily removes this widget from layout, rendering and hit testing without changing Visibility. */
	void SetLayoutVisibilitySuppressed(bool bSuppressed);
	/** Hidden still participates in layout; Collapsed does not. */
	UFUNCTION(BlueprintPure, Category = "LGUI|Visibility")
	bool GetLayoutVisibleInHierarchy()const { return bCacheLayoutVisibleInHierarchy; }
	UFUNCTION(BlueprintPure, Category = "LGUI|Visibility")
	bool GetRenderVisibleInHierarchy()const { return bCacheRenderVisibleInHierarchy; }
	UFUNCTION(BlueprintPure, Category = "LGUI|Visibility")
	bool GetHitTestVisibleInHierarchy()const { return bCacheSelfHitTestVisibleInHierarchy; }
	UFUNCTION(BlueprintPure, Category = "LGUI|Visibility")
	bool GetChildrenHitTestVisibleInHierarchy()const { return bCacheChildrenHitTestVisibleInHierarchy; }

	UFUNCTION(BlueprintPure, Category = "LGUI|Focus")
	bool GetIsFocusable()const { return bIsFocusable; }
	UFUNCTION(BlueprintCallable, Category = "LGUI|Focus")
	void SetIsFocusable(bool Value) { bIsFocusable = Value; }
	UFUNCTION(BlueprintCallable, Category = "LGUI|Focus")
	bool SetFocus(int32 UserIndex = 0, int32 PointerId = 0);
	UFUNCTION(BlueprintPure, Category = "LGUI|Focus")
	bool HasFocus(int32 UserIndex = 0, int32 PointerId = 0)const;
	UFUNCTION(BlueprintCallable, Category = "LGUI|Focus")
	void ClearFocus(int32 UserIndex = 0, int32 PointerId = 0);
	void NotifyFocusReceived(int32 UserIndex, int32 PointerId);
	void NotifyFocusLost(int32 UserIndex, int32 PointerId);
	UFUNCTION(BlueprintPure, Category = "LGUI|Interaction")
	EMouseCursor::Type GetCursor()const { return Cursor.GetValue(); }
	UFUNCTION(BlueprintCallable, Category = "LGUI|Interaction")
	void SetCursor(EMouseCursor::Type Value) { Cursor = Value; }
	UFUNCTION(BlueprintPure, Category = "LGUI|Interaction")
	const FText& GetToolTipText()const { return ToolTipText; }
	UFUNCTION(BlueprintCallable, Category = "LGUI|Interaction")
	void SetToolTipText(const FText& Value) { ToolTipText = Value; }
	UFUNCTION(BlueprintPure, Category = "LGUI|Accessibility")
	ELexAccessibleBehavior GetAccessibleBehavior()const { return AccessibleBehavior; }
	UFUNCTION(BlueprintCallable, Category = "LGUI|Accessibility")
	void SetAccessibleBehavior(ELexAccessibleBehavior Value) { AccessibleBehavior = Value; }
	UFUNCTION(BlueprintPure, Category = "LGUI|Accessibility")
	const FText& GetAccessibleText()const { return AccessibleText; }
	UFUNCTION(BlueprintCallable, Category = "LGUI|Accessibility")
	void SetAccessibleText(const FText& Value) { AccessibleText = Value; }
	UFUNCTION(BlueprintPure, Category = "LGUI|Accessibility")
	const FText& GetAccessibleSummaryText()const { return AccessibleSummaryText; }
	UFUNCTION(BlueprintCallable, Category = "LGUI|Accessibility")
	void SetAccessibleSummaryText(const FText& Value) { AccessibleSummaryText = Value; }
	/** Sends text through Slate's platform accessibility announcement channel. */
	UFUNCTION(BlueprintCallable, Category = "LGUI|Accessibility")
	void AnnounceAccessibleText(const FText& Announcement = FText());
	
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

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	bool GetIgnoreLayout()const{return bIgnoreLayout;}
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void SetIgnoreLayout(bool Value);

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
		static_assert(TPointerIsConvertibleFromTo<T, const ULexLayoutContainer>::Value, "'T' template parameter to CreateNewLayoutContainer must be derived from ULexLayoutContainer");
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
		static_assert(TPointerIsConvertibleFromTo<T, const ULexLayoutSelf>::Value, "'T' template parameter to CreateNewLayoutSelf must be derived from ULexLayoutSelf");
		return (T*)CreateNewLayoutSelf(T::StaticClass());
	}
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void RemoveLayoutSelf();

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	ULexPanelSlot* GetPanelSlot()const { return PanelSlot; }
	UFUNCTION(BlueprintCallable, Category = "LGUI", meta=(DeterminesOutputType="SlotClass"))
	ULexPanelSlot* CreateNewPanelSlot(TSubclassOf<ULexPanelSlot> SlotClass);
	template<class T>
	T* CreateNewPanelSlot()
	{
		return Cast<T>(CreateNewPanelSlot(T::StaticClass()));
	}
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	void RemovePanelSlot();

	const TWeakPtr<FLexUIClipData>& GetClipData()const{return ClipData;}

#pragma region SiblingIndex
protected:
	/** hierarchy index, hierarchy order, render order */
	UPROPERTY(EditAnywhere, Category = LGUI, AdvancedDisplay)
		int32 SiblingIndex = INDEX_NONE;
	/**
	 * Flatten depth/hierarchyIndex, relative to root LexWidget. -1 means not set yet.
	 * RootWidget - 0
	 *	 Widget - 1
	 *	 Widget - 2
	 *	   Widget - 3
	 *	   Widget - 4
	 *	     Widget - 5
	 *	 Widget - 6
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = LGUI, AdvancedDisplay)
	mutable int32 FlattenHierarchyIndex = -1;
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
	FString GetPathDisplayName(const UObject* StopOuter = nullptr)const;
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

	/** return root Widget in hierarchy, could be null if not initialized yet. */
	UFUNCTION(BlueprintCallable, Category = "LGUI")
		ULexWidget* GetRootWidgetInHierarchy()const { return RootWidget.Get(); }
	/** Shutdown-only accessor used while GC may already have marked the hierarchy unreachable. */
	ULexWidget* GetRootWidgetInHierarchyEvenIfUnreachable()const { return RootWidget.GetEvenIfUnreachable(); }
	bool IsRootWidgetInHierarchy()const{return RootWidget.Get() == this;}

	UFUNCTION(BlueprintCallable, Category = "LGUI")
	static void MarkLayoutForRebuild(ULexWidget* InWidget);
	UFUNCTION(BlueprintCallable, Category = "LGUI")
	static void RebuildLayoutImmediately(ULexWidget* InWidget);

private:
	friend class FLexWidgetCustomization;
	friend class ULexCanvas;
	friend class FLexCanvasHierarchyOrderTest;
	/** Swaps arranged geometry out of AnchorData around prefab serialization; raw access, no side effects. */
	friend class FLexUIAuthoredGeometrySaveScope;
	/** LexCanvas which render this UI element */
	UPROPERTY(Transient) mutable TWeakObjectPtr<ULexCanvas> RenderCanvas = nullptr;
	
	/** is this widget contains LexCanvas component */
	mutable uint32 bIsCanvasWidget:1;
	
	mutable uint32 bClipDirty : 1 = true;
	mutable uint32 bNeedRecreateClip : 1 = true;
	
	uint32 bCacheWidgetActiveInHierarchy : 1 = true;
	uint32 bCacheLayoutVisibleInHierarchy : 1 = true;
	uint32 bCacheRenderVisibleInHierarchy : 1 = true;
	uint32 bCacheSelfHitTestVisibleInHierarchy : 1 = true;
	uint32 bCacheChildrenHitTestVisibleInHierarchy : 1 = true;
	uint32 bCacheInteractableInHierarchy : 1 = true;
	uint32 bCacheRaycastableInHierarchy : 1 = true;
	uint32 bLayoutVisibilitySuppressed : 1 = false;
	uint32 bHasLayoutClippingOverride : 1 = false;
	FVector2f LayoutScale = FVector2f::UnitVector;
	ELexWidgetClipping LayoutClippingOverride = ELexWidgetClipping::Inherit;

	uint32 bHasBegunPlay : 1 = false;
	uint32 bIsRegistered : 1 = false;
	uint32 bParked : 1 = false;

	/** Only for root widget, if dirty then we need to recalculate flatten hierarchy index */
	mutable uint32 bFlattenHierarchyIndexDirty : 1;
	
	void MarkClipDirty(bool InClipTypeChanged)const;
	void MarkWidgetLayoutDirty();
	
	/** find root UIItem of hierarchy */
	void CheckRootWidget(ULexWidget* RootWidgetInParent = nullptr);

	void CalculateWidgetActive_Recursive();
	void CalculateVisibility_Recursive();
	void CalculateInteractable_Recursive();
	void CalculateRaycastable_Recursive();
public:
#pragma region TweenAnimation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position X To"), Category = "LTween")
	ULTweener* LocalPositionXTo(double endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position Y To"), Category = "LTween")
	ULTweener* LocalPositionYTo(double endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position Z To"), Category = "LTween")
	ULTweener* LocalPositionZTo(double endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position X To"), Category = "LTween")
	ULTweener* WorldPositionXTo(double endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position Y To"), Category = "LTween")
	ULTweener* WorldPositionYTo(double endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position Z To"), Category = "LTween")
	ULTweener* WorldPositionZTo(double endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween")
	ULTweener* LocalPositionTo(FVector endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTween")
	ULTweener* WorldPositionTo(FVector endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = LTween)
	ULTweener* LocalScaleTo(FVector endValue = FVector(1.0f, 1.0f, 1.0f), float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = LTween)
	ULTweener* LocalUniformScaleTo(float endValue = 1.0f, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute quaternion rotation value"), Category = "LTween")
	ULTweener* LocalRotationQuaternionTo(const FQuat& endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute rotator value"), Category = "LTween")
	ULTweener* LocalRotatorTo(FRotator endValue, bool shortestPath, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute quaternion rotation value"), Category = "LTween")
	ULTweener* WorldRotationQuaternionTo(const FQuat& endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute rotator value"), Category = "LTween")
	ULTweener* WorldRotatorTo(FRotator endValue, bool shortestPath, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* RenderOpacityTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* SizeDeltaTo(const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* WidthTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* HeightTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* AnchoredPositionTo(const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* HorizontalAnchoredPositionTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "LTweenLGUI")
	ULTweener* VerticalAnchoredPositionTo(float endValue, float duration = 0.5f, float delay = 0.0f, ELTweenEase ease = ELTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, Category = "LTweenLGUI")
	static void SetWidgetTweenerAffectByGamePauseAndTimeDilation(ULexWidget* Widget, ULTweener* Tweener);
#pragma endregion
};
