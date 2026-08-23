// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#pragma once

#include "DreamTweener.h"
#include "InputCoreTypes.h"
// These three arrive transitively under a unity build and vanish the moment this header is
// compiled on its own -- FMargin, EMouseCursor and EWidgetPixelSnapping are all used below.
#include "Layout/Margin.h"
#include "GenericPlatform/ICursor.h"
#include "Widgets/WidgetPixelSnapping.h"
#include "Core/DreamUIAnchorData.h"
#include "DreamWidget.generated.h"

class UDreamWidgetSubObjectBehaviour;
class UDreamUIBehaviour;
class UDreamVisual;
class UDreamLayoutSelf;
class UDreamLayoutContainer;
class UDreamPanelSlot;
class FDreamUIClipData;
class UDreamUIDataAsTexture;
class UDreamCanvas;
enum class EDreamRenderMode : uint8;
namespace DreamPerspective { struct FScope; }

/**
 * What kind of change a layout invalidation is reporting.
 *
 * Blink's StyleDifference in miniature. There the decision is made once, by diffing the old and new
 * ComputedStyle, rather than by asking every setter to remember what it affects; the shape here is the
 * same even though the diff is not, and the point is identical - "something changed" and "the desired
 * size changed" are different facts and cost very different amounts of work.
 *
 * Measure is the safe answer and the default, so a site that says nothing keeps the old behaviour.
 */
UENUM(BlueprintType)
enum class EDreamLayoutInvalidation : uint8
{
	/**
	 * This widget's desired size may have changed. Every ancestor that measures it has to re-run, so
	 * the whole chain is dirtied. Text, sprites, child count, anything authored about size.
	 */
	Measure,
	/**
	 * Only where this widget sits inside its parent changed. Panels measure their children by desired
	 * size and never by position, so nothing above the parent can produce a different answer - the
	 * parent re-arranges and the walk stops there.
	 */
	Arrange,
};

/** Matches UMG/Slate visibility while keeping WidgetActive as the behaviour lifecycle switch. */
UENUM(BlueprintType)
enum class EDreamWidgetVisibility : uint8
{
	Visible,
	Hidden,
	Collapsed,
	HitTestInvisible UMETA(DisplayName = "Not Hit-Testable (Self & Children)"),
	SelfHitTestInvisible UMETA(DisplayName = "Not Hit-Testable (Self Only)"),
};

UENUM(BlueprintType)
enum class EDreamAccessibleBehavior : uint8
{
	Auto,
	NotAccessible,
	Summary,
	Custom,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamWidgetVisibilityChangedEvent, EDreamWidgetVisibility, Visibility);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamWidgetFocusEvent, int32, UserIndex, int32, PointerId);

UENUM(BlueprintType)
enum class EDreamWidgetClipping : uint8
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
enum class EDreamWidgetRaycastableType : uint8
{
	//If no parent then use Enabled
	Inherit,
	Enabled,
	Disabled,
};

UENUM(BlueprintType)
enum class EDreamWidgetInteractableType : uint8
{
	//If no parent then use Enabled
	Inherit,
	Enabled,
	Disabled,
};

enum class EDreamWidgetComponentsChangedType : uint8
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
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable)
class DREAMGUI_API UDreamWidget : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_EVENT_OneParam(UDreamWidget, FWidgetActiveChangedEvent, bool/*WidgetActive*/);
	DECLARE_EVENT_ThreeParams(UDreamWidget, FDimensionChangedEvent, bool/*PivotChanged*/, bool/*WidthChanged*/, bool/*HeightChanged*/);
	DECLARE_EVENT_FourParams(UDreamWidget, FChildDimensionChangedEvent, UDreamWidget*/*Child*/, bool/*PivotChanged*/, bool/*WidthChanged*/, bool/*HeightChanged*/);
	DECLARE_EVENT(UDreamWidget, FAttachmentChangedEvent);
	DECLARE_EVENT(UDreamWidget, FTransformChangedEvent);
	DECLARE_EVENT(UDreamWidget, FSiblingIndexChangedEvent);
	DECLARE_EVENT_OneParam(UDreamWidget, FInteractableChangedEvent, bool/*Interactable*/);
	DECLARE_EVENT_OneParam(UDreamWidget, FRaycastableChangedEvent, bool/*Raycastable*/)
	DECLARE_EVENT_OneParam(UDreamWidget, FComponentsChangedEvent, EDreamWidgetComponentsChangedType/*ChangedType*/)
	
	UDreamWidget();

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
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, AnchorData);
	}
	static FName GetPropertyName_SiblingIndex()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, SiblingIndex);
	}
	static FName GetPropertyName_WidgetActive()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, bWidgetActive);
	}
	static FName GetPropertyName_Visibility()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, Visibility);
	}
	static FName GetPropertyName_DisplayName()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, DisplayName);
	}
	static FName GetPropertyName_RelativeLocation()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeLocation);
	}
	static FName GetPropertyName_RelativeRotation()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeRotation);
	}
	static FName GetPropertyName_RelativeScale()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, RelativeScale);
	}
	static FName GetPropertyName_Components()
	{
		return GET_MEMBER_NAME_CHECKED(UDreamWidget, Components);
	}

	bool HasBegunPlay()const{return bHasBegunPlay;}
	bool HasRegistered()const{return bIsRegistered;}

	static void CollectChildrenWidgets(UDreamWidget* Target, TArray<UDreamWidget*>& OutAllChildrenWidgets, bool IncludeTarget = true);

#pragma region CallbackEvents
private:
	void Call_WidgetActiveChanged();
	void Call_TransformChanged();
	void Call_DimensionsChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void Call_ChildDimensionsChanged(UDreamWidget* Child, bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
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

	/*
	 * RENDER TRANSFORM.
	 *
	 * Moves where a widget is DRAWN without telling layout anything. Layout still measures and
	 * arranges it exactly as before, siblings do not shift, and nothing here is ever written back to
	 * AnchorData. That is the whole difference from RelativeLocation, whose setter recomputes the
	 * anchors and asks the parent layout to rebuild -- which is why animating it inside a panel can
	 * never work, the animation and the layout just take turns.
	 *
	 * Depth runs along local X, and it points AWAY from the viewer: UDreamCanvas::GetViewLocation
	 * puts the eye at "location - forward * distance", so a NEGATIVE X translation brings a widget
	 * toward the screen and a positive one pushes it back.
	 *
	 * Full 3D, deliberately. The obvious model is UMG's FWidgetTransform, but UMG is a 2D framework
	 * and this one is not: RelativeLocation, RelativeRotation and RelativeScale are already a
	 * FVector/FQuat/FVector, world-space canvases exist, and a card flipping about its vertical axis
	 * is an ordinary thing to want. A 2D render transform would have been narrower than the authored
	 * transform it mirrors, and would have forbidden effects the framework otherwise allows.
	 *
	 * The batching caveat is the one that already applies to the authored transform, not a new one:
	 * UDreamCanvas::Is2DUITransform drops a widget out of the batched 2D path when it gains depth, yaw
	 * or pitch. Rolling, scaling and sliding in the canvas plane stay batched; a flip costs a draw
	 * call, exactly as it would if you had authored the rotation.
	 */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "Render Transform", Getter, Setter, meta = (AllowPrivateAccess = true, DisplayName = "Translation"))
	FVector RenderTranslation = FVector::ZeroVector;
	/**
	 * Render-only rotation about RenderTransformPivot, in degrees.
	 * FRotator rather than FQuat for the same reason RelativeRotationEuler exists: Sequencer has a
	 * property track for one and not the other. No transient mirror is needed here because this is
	 * itself the serialized source of truth.
	 */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "Render Transform", Getter, Setter, meta = (AllowPrivateAccess = true, DisplayName = "Rotation"))
	FRotator RenderRotation = FRotator::ZeroRotator;
	/** Render-only scale about RenderTransformPivot. */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "Render Transform", Getter, Setter, meta = (AllowPrivateAccess = true, DisplayName = "Scale", AllowPreserveRatio))
	FVector RenderScale = FVector::OneVector;
	/**
	 * What RenderRotation and RenderScale turn about, normalized within the widget's own rect.
	 * (0,0) is bottom-left and (1,1) top-right, matching AnchorData.Pivot rather than UMG's
	 * top-left origin -- consistency inside this fork beats consistency with the other engine.
	 * Two-dimensional because the rect is: it resolves to a point on the widget's own plane.
	 * Independent of AnchorData.Pivot, which belongs to layout.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Render Transform", Getter, Setter, meta = (AllowPrivateAccess = true, DisplayName = "Pivot"))
	FVector2D RenderTransformPivot = FVector2D(0.5, 0.5);

	/*
	 * PERSPECTIVE, in the shape CSS uses.
	 *
	 * Turning this on establishes a perspective for this widget's DESCENDANTS: children with depth
	 * are foreshortened toward an eye standing in front of this widget's plane, at whatever distance
	 * PerspectiveFieldOfView works out to.
	 * The declaring widget itself is not moved -- the remap fixes its plane pointwise -- so switching
	 * it on never disturbs an interface that has no depth in it yet.
	 *
	 * There is no "inherit" setting because inheritance is what happens by default: everything below
	 * a scope is inside it. A descendant that declares its own perspective NESTS rather than
	 * overriding, exactly as nested CSS perspectives do.
	 *
	 * It costs nothing when off, and nothing for a subtree whose widgets have no depth: the remap
	 * only touches the depth direction, so a flat widget comes back bit for bit.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Perspective", Getter = "GetPerspective", Setter = "SetPerspective", meta = (AllowPrivateAccess = true, DisplayName = "Perspective"))
	bool bPerspective = false;
	/**
	 * The angle the subtree is viewed through, in degrees. Wider is a stronger effect, and it means
	 * exactly what UDreamCanvas::FieldOfView means -- the eye distance is derived from it with the
	 * same formula, against this widget's own width.
	 *
	 * An angle and not a distance, because a widget gets stretched: a distance stays put while the
	 * widget grows around it, so the same prefab would look half as deep at twice the size. An angle
	 * is scale-invariant, the way a lens is -- filling more of the frame does not change the lens.
	 */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "Perspective", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition = "bPerspective", ClampMin = "1.0", ClampMax = "179.0", UIMin = "10.0", UIMax = "120.0", DisplayName = "Field Of View"))
	float PerspectiveFieldOfView = 60.0f;
	/**
	 * Where the eye stands over this widget's own rect, normalized: (0,0) bottom-left, (1,1)
	 * top-right, matching RenderTransformPivot. The CSS `perspective-origin`, and the vanishing
	 * point a subtree converges toward.
	 */
	UPROPERTY(Interp, EditAnywhere, BlueprintReadOnly, Category = "Perspective", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition = "bPerspective", DisplayName = "Origin"))
	FVector2D PerspectiveOrigin = FVector2D(0.5, 0.5);

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

	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	const FVector& GetRenderTranslation()const { return RenderTranslation; }
	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	const FVector& GetRenderScale()const { return RenderScale; }
	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	const FRotator& GetRenderRotation()const { return RenderRotation; }
	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	const FVector2D& GetRenderTransformPivot()const { return RenderTransformPivot; }
	/** True while any render channel is off its default, i.e. while drawing differs from layout. */
	UFUNCTION(BlueprintPure, Category = "Render Transform")
	bool HasRenderTransform()const { return bHasRenderTransform; }

	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	void SetRenderTranslation(const FVector& Value);
	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	void SetRenderScale(const FVector& Value);
	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	void SetRenderRotation(const FRotator& Value);
	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	void SetRenderTransformPivot(const FVector2D& Value);
	/** Back to drawing exactly where layout put it, in one invalidation. */
	UFUNCTION(BlueprintCallable, Category = "Render Transform")
	void ClearRenderTransform();

	UFUNCTION(BlueprintCallable, Category = "Perspective")
	bool GetPerspective()const { return bPerspective; }
	UFUNCTION(BlueprintCallable, Category = "Perspective")
	float GetPerspectiveFieldOfView()const { return PerspectiveFieldOfView; }
	/** The eye distance the field of view works out to against the current width. */
	UFUNCTION(BlueprintPure, Category = "Perspective")
	float GetPerspectiveDistance()const;
	UFUNCTION(BlueprintCallable, Category = "Perspective")
	const FVector2D& GetPerspectiveOrigin()const { return PerspectiveOrigin; }
	UFUNCTION(BlueprintCallable, Category = "Perspective")
	void SetPerspective(bool Value);
	UFUNCTION(BlueprintCallable, Category = "Perspective")
	void SetPerspectiveFieldOfView(float Value);
	UFUNCTION(BlueprintCallable, Category = "Perspective")
	void SetPerspectiveOrigin(const FVector2D& Value);

	/** True when this widget or any ancestor declares a perspective. One bit, kept up to date with the transform. */
	UFUNCTION(BlueprintPure, Category = "Perspective")
	bool HasPerspectiveInHierarchy()const { return bHasPerspectiveInHierarchy; }
	/**
	 * True when a scope shapes THIS widget's own geometry -- its own declaration or an ancestor's.
	 * A widget is inside its own perspective: the scope's plane is taken from where layout put the
	 * widget, so its own render rotation is measured against that plane and foreshortens like
	 * anything else. A widget with no render transform is flat in that plane and comes back
	 * untouched, so switching Perspective on still changes nothing until something gains depth.
	 */
	UFUNCTION(BlueprintPure, Category = "Perspective")
	bool HasPerspectiveApplied()const { return bHasPerspectiveInHierarchy; }
	/** True when an ANCESTOR declares one. Distinct from HasPerspectiveApplied, which includes this widget's own. */
	UFUNCTION(BlueprintPure, Category = "Perspective")
	bool HasInheritedPerspective()const { return Parent.IsValid() && Parent->bHasPerspectiveInHierarchy; }
	/**
	 * Widget-to-world with any inherited perspective folded in -- where this widget is actually drawn.
	 *
	 * An FMatrix and not an FTransform because perspective contributes a shear whenever the scope is
	 * tilted, and FTransform::SetFromMatrix does not refuse a sheared matrix: it orthonormalizes it
	 * and hands back something plausible and wrong. Exactly GetWorldTransform().ToMatrixWithScale()
	 * when no perspective applies, which is almost always.
	 */
	FMatrix GetWorldMatrix()const;
	FMatrix GetInverseWorldMatrix()const;
	/** The composed remap from this widget's own scope and every one above it. Identity when there is none. */
	FMatrix GetInheritedPerspectiveRemap()const;
	/** This widget's own declared scope in world space; false when it declares none. */
	bool GetPerspectiveScope(DreamPerspective::FScope& OutScope)const;

	/** This widget's render transform alone, in its own local space. Identity when unset. */
	FTransform GetRenderTransform()const;
	/**
	 * Widget-to-world with every render transform on the way up removed -- where layout believes the
	 * widget is. Needed by the two places that convert a world transform back into authored local
	 * data (SetWorldTransform and a keep-world-position reparent); using the drawn transform there
	 * would bake an animation offset into RelativeLocation and leak it into the saved prefab.
	 */
	FTransform GetLayoutWorldTransform()const;

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
	/** GetLocalTransform with this widget's render transform applied. What the world transform is built from. */
	FTransform GetRenderLocalTransform()const;
	/** Recompute the cached has-a-render-transform bit and push the new transform down the subtree. */
	void ApplyRenderTransformChange();
	/** Recompute the cached has-a-render-transform bit from the serialized channels. */
	void RefreshRenderTransformFlag();
	void ApplyPerspectiveChange();
#if WITH_EDITOR
	/** Say plainly when a declared perspective is inert, rather than leaving the author to guess. */
	void WarnIfPerspectiveCannotApply()const;
#endif
	/** Recompute the cached bit for this widget and, when it changes, everything below it. */
	void RefreshPerspectiveInHierarchy();
	UFUNCTION(BlueprintCallable, Category = "Transform")
	const FTransform& GetWorldTransform()const;

	void SetWorldTransform(const FTransform& InWorldTransform);
	/** Only called by PrefabSystem to restore parent-children hierarchy */
	void SetParentBeforeRegister(UDreamWidget* InParent);
	/** Restores a serialized hierarchy while preserving legacy over-capacity assets. Cycle checks still apply. */
	bool SetParentFromPrefab(UDreamWidget* InParent, bool InKeepWorldPosition = false, int InSiblingIndex = -1);
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
	UDreamWidget* GetParent()const { return Parent.Get(); }
	/**
	 * Set parent of this widget, could use null to detach it from origin parent.
	 * @InKeepWorldPosition true - keep world position & rotation & scale after change parent, false - keep relative position & rotation & scale.
	 * @InSiblingIndex if InParent is a valid widget, then put the widget at specific index in parent's children list, -1 or other out of range value means the widget will be put at tail.
	 */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetParent(UDreamWidget* InParent, bool InKeepWorldPosition = true, int InSiblingIndex = -1);
	/** Same operation as SetParent, but reports capacity/cycle rejection to the caller. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	bool TrySetParent(UDreamWidget* InParent, bool InKeepWorldPosition = true, int InSiblingIndex = -1);
	/** Minimum child capacity imposed by the current LayoutContainer and Behaviours. INDEX_NONE is unlimited. */
	UFUNCTION(BlueprintPure, Category = "Transform")
	int32 GetMaxChildrenCapacity() const;
	UFUNCTION(BlueprintPure, Category = "Transform")
	bool CanAcceptAdditionalChildren(int32 AdditionalChildCount = 1) const;
	bool CanAcceptChildren(TConstArrayView<UDreamWidget*> InChildren) const;
	UFUNCTION(BlueprintPure, Category = "Transform")
	bool CanAcceptChild(const UDreamWidget* InChild) const;
	/** Set the sibling index of this widget in its parent's children list. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void SetSiblingIndex(int Value);
	/** Recurses up the list of parents and returns true if this widget is a descendant of the InTarget. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	bool IsChildOf(const UDreamWidget* InTarget)const;
	UFUNCTION(BlueprintCallable, Category = "Transform")
	const TArray<UDreamWidget*>& GetChildren()const
	{
		EnsureUIChildrenSorted();
		return Children;
	}
	UFUNCTION(BlueprintCallable, Category = "Transform")
	int GetChildrenCount()const { return Children.Num(); }

	/**
	 * UMG's UPanelWidget::AddChild, adapted. UMG needs eight typed variants because each panel has
	 * its own slot class; here there is one UDreamPanelSlot, so one verb returns it and you can set
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
	UDreamPanelSlot* AddChild(UDreamWidget* InChild, int32 InSiblingIndex = -1);

	/**
	 * Detach a child and leave it alive: still registered, subtree intact, ready to be added
	 * somewhere else. It goes back to the not-yet-added state a freshly created widget is in, so it
	 * draws nothing and its behaviours are disabled in the meantime -- which is what makes this the
	 * verb to pool with, and why a pooled widget still shows up as held rather than leaked.
	 *
	 * Ownership passes to you. Nothing else refers to the widget afterwards, so either add it
	 * somewhere or DestroyChild it; dropping the last reference to a live widget is a leak the
	 * engine cannot clean up quietly.
	 *
	 * The slot does not survive, matching UMG and matching what any other move between parents
	 * does. Set padding and friends again after re-adding.
	 */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	bool RemoveChild(UDreamWidget* InChild);
	UFUNCTION(BlueprintCallable, Category = "Transform")
	bool RemoveChildAt(int32 InIndex);
	/** Detach and destroy a child, and everything below it. Use this when you are finished with it. */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	bool DestroyChild(UDreamWidget* InChild);
	/**
	 * Destroy every child and their subtrees. Named for what it does: UMG's ClearChildren merely
	 * detaches and lets GC take the pieces, which here would leave live registered widgets behind
	 * with nobody holding them.
	 */
	UFUNCTION(BlueprintCallable, Category = "Transform")
	void DestroyAllChildren();

	/** Position of InChild among this widget's children, or INDEX_NONE if it is not one. */
	UFUNCTION(BlueprintPure, Category = "Transform")
	int32 GetChildIndex(const UDreamWidget* InChild)const;
	UFUNCTION(BlueprintPure, Category = "Transform")
	bool HasChild(const UDreamWidget* InChild)const;
	UFUNCTION(BlueprintPure, Category = "Transform")
	bool HasAnyChildren()const;
	/**
	 * Whether this widget's layout container hands its children a UDreamPanelSlot. This is the
	 * question behind AddChild returning null: a widget can take children with no container at all,
	 * and the Dream flex box and grid arrange children without slots, so "arranges children" and
	 * "has slots" are not the same thing.
	 */
	UFUNCTION(BlueprintPure, Category = "Transform")
	bool HasPanelSlots()const;
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const TArray<UDreamUIBehaviour*>& GetAllComponents()const{return Components;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (ComponentClass = "/Script/DreamGUI.DreamUIBehaviour", DeterminesOutputType = "ComponentClass"))
	TArray<UDreamUIBehaviour*> GetComponents(TSubclassOf<UDreamUIBehaviour> ComponentClass)const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (ComponentClass = "/Script/DreamGUI.DreamUIBehaviour", DeterminesOutputType = "ComponentClass"))
	UDreamUIBehaviour* GetComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass)const;
	template<class T>
	T* GetComponent()const
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamUIBehaviour>::Value, "'T' template parameter to GetComponent must be derived from UDreamUIBehaviour");
		return Cast<T>(GetComponent(T::StaticClass()));
	}
	/**
	 * Adds every behaviour NewLayout requires (UDreamLayoutContainer::GetRequiredBehaviourClasses) that is
	 * not already present, and removes the behaviours only OldLayout required. Idempotent; a second call with
	 * the same arguments is a no-op. Calls Modify() on the touched objects but opens no transaction of its
	 * own, so an editor caller must already be inside one. Returns true when a component was added or removed.
	 */
	bool SyncRequiredBehavioursForLayoutContainer(const UDreamLayoutContainer* OldLayout, const UDreamLayoutContainer* NewLayout);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (DeterminesOutputType = "InterfaceClass"))
	UDreamUIBehaviour* GetComponentByInterface(UClass* InterfaceClass)const;
	template<class T>
	T* GetComponentInParent(bool bIncludeSelf = false)const
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamUIBehaviour>::Value, "'T' template parameter to GetComponentInParent must be derived from UDreamUIBehaviour");
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
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (ComponentClass = "/Sript/DreamGUI.DreamUIBehaviour", DeterminesOutputType = "ComponentClass"))
	UDreamUIBehaviour* AddComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass);
	UDreamUIBehaviour* AddComponentByTemplate(UDreamUIBehaviour* ComponentTemplate);
	template<class T>
	T* AddComponent()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamUIBehaviour>::Value, "'T' template parameter to GetComponent must be derived from UDreamUIBehaviour");
		return Cast<T>(AddComponent(T::StaticClass()));
	}
	template<class T>
	T* AddComponentByTemplate(UDreamUIBehaviour* ComponentTemplate)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamUIBehaviour>::Value, "'T' template parameter to GetComponent must be derived from UDreamUIBehaviour");
		return Cast<T>(AddComponent(T::StaticClass(), ComponentTemplate));
	}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (ComponentClass = "/Sript/DreamGUI.DreamUIBehaviour"))
	void RemoveComponent(UDreamUIBehaviour* Component);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta = (ComponentClass = "/Sript/DreamGUI.DreamUIBehaviour"))
	void MoveComponentToIndex(UDreamUIBehaviour* Component, int32 NewIndex);
	void UpdateObjectToWorldTransform();
	void CalculateObjectToWorldTransform(bool bPropagateToChildren = true);
private:
	UDreamUIBehaviour* AddComponent(TSubclassOf<UDreamUIBehaviour> ComponentClass, UDreamUIBehaviour* ComponentTemplate);
	bool TrySetParentInternal(UDreamWidget* InParent, bool InKeepWorldPosition, int InSiblingIndex, bool bEnforceCapacity);
	
	mutable FTransform ObjectToWorldTransform;

	virtual void OnUpdateTransform();
	virtual void OnChildAttached(UDreamWidget* ChildComponent);
	virtual void OnChildDetached(UDreamWidget* ChildComponent);

	void OnDetachedFromParent();
	void OnAttachedToParent();

	/** UIItem's hierarchy changed */
	void OnHierarchyAttachmentChanged(UDreamCanvas* ParentRenderCanvas, UDreamWidget* ParentRoot);
	/** called when RenderCanvas changed. */
	virtual void OnRenderCanvasChanged(UDreamCanvas* OldCanvas, UDreamCanvas* NewCanvas);
	void SetRenderCanvas(UDreamCanvas* InNewCanvas);

	UPROPERTY(Instanced)
	TArray<TObjectPtr<UDreamUIBehaviour>> Components;
public:
	/** Called by DreamCanvas, when a new DreamCanvas is registered on self actor */
	void RegisterRenderCanvas(UDreamCanvas* InRenderCanvas);
	/** Called by DreamCanvas, when DreamCanvas is unregistered on self actor */
	void UnregisterRenderCanvas();

	/** Update layout */
	void UpdateLayout();
	/** Called by DreamCanvas */
	void UpdateClip(UDreamUIDataAsTexture* ClipDataTexture, TArray<TSharedPtr<FDreamUIClipData>>& ClipDataList);
	/** Called by DreamCanvas */
	void UpdateVisual()const;
	
	void ForceUpdateLayout();
protected:
	void RenewRenderCanvasRecursive(UDreamCanvas* InParentRenderCanvas);

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
	UPROPERTY(Transient) mutable TWeakObjectPtr<UDreamWidget> Parent = nullptr;
	/** root in hierarchy */
	mutable TWeakObjectPtr<UDreamWidget> RootWidget = nullptr;//don't mark this Transactional, because undo or redo will call register/unregister, which will trigger check RootUIItem
	/** UI children array, sorted by hierarchy index */
	UPROPERTY(Transient) mutable TArray<TObjectPtr<UDreamWidget>> Children;
	/** check valid, incase un-normally deleting actor, like undo */
	void EnsureUIChildrenValid();
	void EnsureUIChildrenSorted()const;

	/** AnchorData contains rect transform and color */
	UPROPERTY(EditAnywhere, Getter, Setter, Category = "DreamGUI-AnchorData")
	FDreamUIAnchorData AnchorData;

	/**
	 * Animatable mirrors of the resolved geometry, one per channel Sequencer can key. Sequencer
	 * reads property memory directly while writing through the setter (the RelativeRotationEuler
	 * story), so each must be a real stored field; SyncAnimatableGeometryMirrors keeps them equal
	 * to the caches below. A stale cache and its mirror go stale together, which is exactly what
	 * the details panel shows too.
	 */
	UPROPERTY(Interp, Transient, BlueprintReadOnly, Getter = "GetWidth", Setter = "SetWidth", Category = "DreamGUI-AnchorData", DisplayName = "Width", meta = (AllowPrivateAccess = true))
	float AnimatableWidth = 0;
	UPROPERTY(Interp, Transient, BlueprintReadOnly, Getter = "GetHeight", Setter = "SetHeight", Category = "DreamGUI-AnchorData", DisplayName = "Height", meta = (AllowPrivateAccess = true))
	float AnimatableHeight = 0;
	UPROPERTY(Interp, Transient, BlueprintReadOnly, Getter = "GetAnchorOffsetLeft", Setter = "SetAnchorOffsetLeft", Category = "DreamGUI-AnchorData", DisplayName = "Anchor Left", meta = (AllowPrivateAccess = true))
	float AnimatableAnchorLeft = 0;
	UPROPERTY(Interp, Transient, BlueprintReadOnly, Getter = "GetAnchorOffsetRight", Setter = "SetAnchorOffsetRight", Category = "DreamGUI-AnchorData", DisplayName = "Anchor Right", meta = (AllowPrivateAccess = true))
	float AnimatableAnchorRight = 0;
	UPROPERTY(Interp, Transient, BlueprintReadOnly, Getter = "GetAnchorOffsetTop", Setter = "SetAnchorOffsetTop", Category = "DreamGUI-AnchorData", DisplayName = "Anchor Top", meta = (AllowPrivateAccess = true))
	float AnimatableAnchorTop = 0;
	UPROPERTY(Interp, Transient, BlueprintReadOnly, Getter = "GetAnchorOffsetBottom", Setter = "SetAnchorOffsetBottom", Category = "DreamGUI-AnchorData", DisplayName = "Anchor Bottom", meta = (AllowPrivateAccess = true))
	float AnimatableAnchorBottom = 0;
	void SyncAnimatableGeometryMirrors() const;

	mutable float CacheWidth = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetHeight", Setter="SetHeight", Category = "DreamGUI-AnchorData", DisplayName="Height")
	mutable float CacheHeight = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorLeft", Setter="SetAnchorLeft", Category = "DreamGUI-AnchorData", DisplayName="AnchorLeft")
	mutable float CacheAnchorOffsetLeft = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorRight", Setter="SetAnchorRight", Category = "DreamGUI-AnchorData", DisplayName="AnchorRight")
	mutable float CacheAnchorOffsetRight = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorTop", Setter="SetAnchorTop", Category = "DreamGUI-AnchorData", DisplayName="AnchorTop")
	mutable float CacheAnchorOffsetTop = 0;
	//UPROPERTY(EditAnywhere, Transient, Getter="GetAnchorBottom", Setter="SetAnchorBottom", Category = "DreamGUI-AnchorData", DisplayName="AnchorBottom")
	mutable float CacheAnchorOffsetBottom = 0;
	
	mutable uint8 bCacheWidthDirty : 1 = true, bCacheHeightDirty : 1 = true,
	bCacheAnchorOffsetLeftDirty : 1 = true, bCacheAnchorOffsetRightDirty : 1 = true,
	bCacheAnchorOffsetTopDirty : 1 = true, bCacheAnchorOffsetBottomDirty : 1 = true;
	uint8 bCanSetAnchorFromTransform : 1 = false;
	
#pragma region AnchorData
public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	const FDreamUIAnchorData& GetAnchorData()const { return AnchorData; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	FVector2D GetPivot() const { return AnchorData.Pivot; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	FVector2D GetAnchorMin() const { return AnchorData.AnchorMin; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	FVector2D GetAnchorMax() const { return AnchorData.AnchorMax; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	FVector2D GetAnchoredPosition() const { return AnchorData.AnchoredPosition; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	FVector2D GetSizeDelta() const { return AnchorData.SizeDelta; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	float GetHorizontalAnchoredPosition() const { return AnchorData.AnchoredPosition.X; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	float GetVerticalAnchoredPosition() const { return AnchorData.AnchoredPosition.Y; }

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetWidth() const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetHeight() const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	FVector2D GetSize() const{return FVector2D(GetWidth(), GetHeight());}

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetAnchorOffsetLeft()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetAnchorOffsetTop()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetAnchorOffsetRight()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetAnchorOffsetBottom()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	FMargin GetAnchorOffset()const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetAnchorData(const FDreamUIAnchorData& Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetPivot(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetAnchorMin(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetAnchorMax(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	void SetAnchorOffset(FMargin Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize = false, bool bKeepRelativeLocation = false);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize = false, bool bKeepRelativeLocation = false);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetAnchoredPosition(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetHorizontalAnchoredPosition(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetVerticalAnchoredPosition(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetSizeDelta(FVector2D Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
	void SetAnchoredPositionAndSizeDelta(FVector2D Position, FVector2D Size);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetWidth(float Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetHeight(float Value);

	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetAnchorOffsetLeft(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetAnchorOffsetTop(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetAnchorOffsetRight(float Value);
	/** This function only valid if UIItem have parent */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		void SetAnchorOffsetBottom(float Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		FVector2D GetLocalSpaceLeftBottomPoint()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		FVector2D GetLocalSpaceRightTopPoint()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		FVector2D GetLocalSpaceCenter()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetLocalSpaceLeft()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetLocalSpaceRight()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetLocalSpaceBottom()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-AnchorData")
		float GetLocalSpaceTop()const;
#pragma endregion
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	UDreamWidget* GetChildByIndex(int index)const;
	/** Get root canvas of hierarchy */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	UDreamCanvas* GetRootCanvas()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	USceneComponent* GetAttachedRootSceneComponent()const;

	/** mark all dirty for UI element to update, include all children */
	void MarkAllDirtyRecursive();
	virtual void MarkAllDirty();
	virtual void MarkRenderModeChangeRecursive(UDreamCanvas* Canvas, EDreamRenderMode OldRenderMode, EDreamRenderMode NewRenderMode);

	void CalculateAnchorFromTransform();
	void CalculateTransformFromAnchor();
	void CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged);
	
	void MarkTransformChanged();
	void MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged);
	void MarkAnchorDataChanged_Recursive(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache = true, bool InPropagateToChildren = true);
	virtual void MarkCanvasUpdate(bool bRebuildDrawCall)const;

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPositionAndSizeForLayoutAnimation(FVector2D Position, FVector2D Size);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPositionForLayoutAnimation(FVector2D Position);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetSizeForLayoutAnimation(FVector2D Position);
	void MarkAnchorDataChangedByLayoutContainer_Recursive(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache = true, bool InPropagateToChildren = true);
private:
	float GetLayoutProperty(TFunctionRef<float(UDreamLayoutSelf*)> GetLayoutSelfProperty
		, TFunctionRef<float(UDreamLayoutContainer*)> GetLayoutContainerProperty
		, TFunctionRef<float(UDreamVisual*)> GetVisualProperty
		, float DefaultValue)const;
	UObject* GetLayoutSource(TFunctionRef<float(UDreamLayoutSelf*)> GetLayoutSelfProperty
		, TFunctionRef<float(UDreamLayoutContainer*)> GetLayoutContainerProperty
		, TFunctionRef<float(UDreamVisual*)> GetVisualProperty
		)const;
	
	FVector2D PrevLocation2D = FVector2D::Zero();
	FVector2D PrevScale2D = FVector2D::One();
	mutable uint8 bNeedSortUIChildren : 1;
	uint8 bIsAttaching : 1 = false;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, UIMin="0", UIMax="1"))
	float RenderOpacity = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamWidgetClipping Clipping = EDreamWidgetClipping::Inherit;
	TWeakPtr<FDreamUIClipData> ClipData = nullptr;
	/**
	 * X- RightBottom, Y- RightTop, Z- LeftTop, W- LeftBottom
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition="Clipping!=EDreamWidgetClipping::Disabled&&Clipping!=EDreamWidgetClipping::Inherit"))
	FVector4f ClippingCornerRadius = FVector4f::Zero();
	/**
	 * Expand clip area outward.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true, EditCondition="Clipping!=EDreamWidgetClipping::Disabled&&Clipping!=EDreamWidgetClipping::Inherit"))
	FMargin ClippingMargin = FMargin(0);
#if WITH_EDITORONLY_DATA
	UPROPERTY(EditAnywhere, Category = "DreamGUI")
	bool bUniformSetClippingCornerRadius = true;
#endif
	/**
	 * If not WidgetActive, then not visible, not take layout space, not interactable, not hit-testable
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter = "GetWidgetActive", Setter="SetWidgetActive", meta = (AllowPrivateAccess = true))
	bool bWidgetActive = true;
	/** Controls layout, painting and hit testing independently from WidgetActive. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter = "GetVisibility", Setter = "SetVisibility", meta = (AllowPrivateAccess = true))
	EDreamWidgetVisibility Visibility = EDreamWidgetVisibility::Visible;
#if WITH_EDITORONLY_DATA
	/** Transient preview state restored from the owning prefab editor data. */
	bool bHiddenInDesigner = false;
	/** LayoutContainer captured in PreEditChange so PostEditChangeProperty can diff required behaviours. */
	TWeakObjectPtr<UDreamLayoutContainer> LayoutContainerBeforeEdit;
#endif
	/** If the widget will draw snapped to the nearest pixel.  Improves clarity but might cause visible stepping in animation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EWidgetPixelSnapping PixelSnapping = EWidgetPixelSnapping::Inherit;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamWidgetRaycastableType Raycastable = EDreamWidgetRaycastableType::Inherit;
	/** If the widget enable for interaction? */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamWidgetInteractableType Interactable = EDreamWidgetInteractableType::Inherit;
	/**
	 * Restrict navigation area to only children of this UI node, to forbid it navigate out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter = "GetRestrictNavigationArea", Setter = "SetRestrictNavigationArea", meta = (AllowPrivateAccess = true))
	uint8 bRestrictNavigationArea : 1 = false;
	/**
	 * Ignore parent layout container
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DreamGUI", Getter = "GetIgnoreLayout", Setter = "SetIgnoreLayout", meta = (AllowPrivateAccess = true))
	bool bIgnoreLayout = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Visual", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<UDreamVisual> Visual = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "LayoutContainer", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<UDreamLayoutContainer> LayoutContainer = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "LayoutSelf", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<UDreamLayoutSelf> LayoutSelf = nullptr;
	/** Parent-panel-owned layout data. This is separate from LayoutSelf so legacy Flex/Grid assets remain valid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "PanelSlot", Getter, meta = (AllowPrivateAccess = true))
	TObjectPtr<UDreamPanelSlot> PanelSlot = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Focus", Getter = "GetIsFocusable", Setter = "SetIsFocusable", meta = (AllowPrivateAccess = true))
	bool bIsFocusable = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", Getter, Setter, meta = (AllowPrivateAccess = true))
	TEnumAsByte<EMouseCursor::Type> Cursor = EMouseCursor::Default;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction", Getter, Setter, meta = (AllowPrivateAccess = true, MultiLine = true))
	FText ToolTipText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility", Getter, Setter, meta = (AllowPrivateAccess = true))
	EDreamAccessibleBehavior AccessibleBehavior = EDreamAccessibleBehavior::Auto;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility", Getter, Setter, meta = (AllowPrivateAccess = true, MultiLine = true))
	FText AccessibleText;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Accessibility", Getter, Setter, meta = (AllowPrivateAccess = true, MultiLine = true))
	FText AccessibleSummaryText;

	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Visibility")
	FDreamWidgetVisibilityChangedEvent OnVisibilityChanged;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Focus")
	FDreamWidgetFocusEvent OnFocusReceived;
	UPROPERTY(BlueprintAssignable, Category = "DreamGUI|Focus")
	FDreamWidgetFocusEvent OnFocusLost;

public:
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamWidgetClipping GetClipping()const
	{
		return bHasLayoutClippingOverride && Clipping == EDreamWidgetClipping::Inherit
			? LayoutClippingOverride
			: Clipping;
	}
	EDreamWidgetClipping GetAuthoredClipping()const { return Clipping; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool IsPointVisibleOnClip(const FVector& Value)const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetClipping(EDreamWidgetClipping Value);
	/** Applies a transient layout default while authored clipping remains Inherit. */
	void SetLayoutClippingOverride(EDreamWidgetClipping Value);
	void ClearLayoutClippingOverride();
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	FVector4f GetClippingCornerRadius()const { return ClippingCornerRadius; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	FMargin GetClippingMargin()const { return ClippingMargin; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetClippingCornerRadius(FVector4f Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetClippingMargin(FMargin Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetRenderOpacity()const { return RenderOpacity; }
	/**
	 * Retrieves the final opacity value used during rendering for this widget, considering all relevant settings and parent opacity.
	 * This value is influenced by the widget's own `RenderOpacity` property and hierarchical parent `RenderOpacity`.
	 *
	 * @return The calculated final opacity value for rendering this widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	float GetFinalRenderOpacity()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetRenderOpacity(float Value);
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EWidgetPixelSnapping GetPixelSnapping()const { return PixelSnapping; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetPixelSnappingInHierarchy()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetPixelSnapping(EWidgetPixelSnapping Value);

	/**
	 * Get WidgetActive property value
	 * @return WidgetActive self property
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
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
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
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
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetWidgetActive(bool Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamWidgetVisibility GetVisibility()const { return Visibility; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetVisibility(EDreamWidgetVisibility Value);
	/** Temporarily removes this widget from layout, rendering and hit testing without changing Visibility. */
	void SetLayoutVisibilitySuppressed(bool bSuppressed);
	/** Hidden still participates in layout; Collapsed does not. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Visibility")
	bool GetLayoutVisibleInHierarchy()const { return bCacheLayoutVisibleInHierarchy; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Visibility")
	bool GetRenderVisibleInHierarchy()const { return bCacheRenderVisibleInHierarchy; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Visibility")
	bool GetHitTestVisibleInHierarchy()const { return bCacheSelfHitTestVisibleInHierarchy; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Visibility")
	bool GetChildrenHitTestVisibleInHierarchy()const { return bCacheChildrenHitTestVisibleInHierarchy; }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|Focus")
	bool GetIsFocusable()const { return bIsFocusable; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Focus")
	void SetIsFocusable(bool Value) { bIsFocusable = Value; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Focus")
	bool SetFocus(int32 UserIndex = 0, int32 PointerId = 0);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Focus")
	bool HasFocus(int32 UserIndex = 0, int32 PointerId = 0)const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Focus")
	void ClearFocus(int32 UserIndex = 0, int32 PointerId = 0);
	void NotifyFocusReceived(int32 UserIndex, int32 PointerId);
	void NotifyFocusLost(int32 UserIndex, int32 PointerId);
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Interaction")
	EMouseCursor::Type GetCursor()const { return Cursor.GetValue(); }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Interaction")
	void SetCursor(EMouseCursor::Type Value) { Cursor = Value; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Interaction")
	const FText& GetToolTipText()const { return ToolTipText; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Interaction")
	void SetToolTipText(const FText& Value) { ToolTipText = Value; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Accessibility")
	EDreamAccessibleBehavior GetAccessibleBehavior()const { return AccessibleBehavior; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Accessibility")
	void SetAccessibleBehavior(EDreamAccessibleBehavior Value) { AccessibleBehavior = Value; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Accessibility")
	const FText& GetAccessibleText()const { return AccessibleText; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Accessibility")
	void SetAccessibleText(const FText& Value) { AccessibleText = Value; }
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Accessibility")
	const FText& GetAccessibleSummaryText()const { return AccessibleSummaryText; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Accessibility")
	void SetAccessibleSummaryText(const FText& Value) { AccessibleSummaryText = Value; }
	/** Sends text through Slate's platform accessibility announcement channel. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Accessibility")
	void AnnounceAccessibleText(const FText& Announcement = FText());
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamWidgetRaycastableType GetRaycastable()const { return Raycastable; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetRaycastable(EDreamWidgetRaycastableType Value);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetRaycastableInHierarchy()const{return bCacheRaycastableInHierarchy;}

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	EDreamWidgetInteractableType GetInteractable()const { return Interactable; }
	/**
	 * Get if this widget is interactable when use input interaction, considering all parent settings.
	 * @return If this widget is interactable
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetInteractableInHierarchy()const{return bCacheInteractableInHierarchy;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetInteractable(EDreamWidgetInteractableType Value);
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetRestrictNavigationArea()const{return bRestrictNavigationArea;}

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	bool GetIgnoreLayout()const{return bIgnoreLayout;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetIgnoreLayout(bool Value);

	/**
	 * Search up parent DreamWidget which bRestrictNavigationArea is true and return it, include this DreamWidget self
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	const UDreamWidget* GetRestrictNavigationAreaWidget()const;
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void SetRestrictNavigationArea(bool Value);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	UDreamVisual* GetVisual()const { return Visual; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta=(DeterminesOutputType="VisualClass"))
	UDreamVisual* CreateNewVisual(TSubclassOf<UDreamVisual> VisualClass);
	template<class T>
	T* CreateNewVisual()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamVisual>::Value, "'T' template parameter to CreateNewVisual must be derived from UDreamVisual");
		return (T*)CreateNewVisual(T::StaticClass());
	}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void RemoveVisual();
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	UDreamLayoutContainer* GetLayoutContainer()const { return LayoutContainer; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta=(DeterminesOutputType="LayoutClass"))
	UDreamLayoutContainer* CreateNewLayoutContainer(TSubclassOf<UDreamLayoutContainer> Class);
	template<class T>
	T* CreateNewLayoutContainer()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamLayoutContainer>::Value, "'T' template parameter to CreateNewLayoutContainer must be derived from UDreamLayoutContainer");
		return (T*)CreateNewLayoutContainer(T::StaticClass());
	}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void RemoveLayoutContainer();
	
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	UDreamLayoutSelf* GetLayoutSelf()const{return LayoutSelf;}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta=(DeterminesOutputType="LayoutClass"))
	UDreamLayoutSelf* CreateNewLayoutSelf(TSubclassOf<UDreamLayoutSelf> Class);
	template<class T>
	T* CreateNewLayoutSelf()
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamLayoutSelf>::Value, "'T' template parameter to CreateNewLayoutSelf must be derived from UDreamLayoutSelf");
		return (T*)CreateNewLayoutSelf(T::StaticClass());
	}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void RemoveLayoutSelf();

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	UDreamPanelSlot* GetPanelSlot()const { return PanelSlot; }
	UFUNCTION(BlueprintCallable, Category = "DreamGUI", meta=(DeterminesOutputType="SlotClass"))
	UDreamPanelSlot* CreateNewPanelSlot(TSubclassOf<UDreamPanelSlot> SlotClass);
	template<class T>
	T* CreateNewPanelSlot()
	{
		return Cast<T>(CreateNewPanelSlot(T::StaticClass()));
	}
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	void RemovePanelSlot();

	const TWeakPtr<FDreamUIClipData>& GetClipData()const{return ClipData;}

#pragma region SiblingIndex
protected:
	/** hierarchy index, hierarchy order, render order */
	UPROPERTY(EditAnywhere, Category = DreamGUI, AdvancedDisplay)
		int32 SiblingIndex = INDEX_NONE;
	/**
	 * Flatten depth/hierarchyIndex, relative to root DreamWidget. -1 means not set yet.
	 * RootWidget - 0
	 *	 Widget - 1
	 *	 Widget - 2
	 *	   Widget - 3
	 *	   Widget - 4
	 *	     Widget - 5
	 *	 Widget - 6
	 */
	UPROPERTY(Transient, VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
	mutable int32 FlattenHierarchyIndex = -1;
	void MarkFlattenHierarchyIndexDirty();
private:
	/** Only for RootUIItem */
	void RecalculateFlattenHierarchyIndex()const;
	void CalculateFlattenHierarchyIndex_Recursive(int& index)const;
	void ApplySiblingIndex();
public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		int32 GetSiblingIndex() const { return SiblingIndex; }
	/** Get index order of the widget from top most widget in flatten hierarchy. */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		int32 GetFlattenHierarchyIndex()const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetAsFirstSibling();
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
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
	UPROPERTY(VisibleAnywhere, Category = DreamGUI, AdvancedDisplay)
		FString DisplayName;
public:
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
	FString GetPathDisplayName(const UObject* StopOuter = nullptr)const;
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		const FString& GetDisplayName()const { return DisplayName; }
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		void SetDisplayName(const FString& InName) { DisplayName = InName; }
	/** 
	 * Search in children and return the first UIItem that the displayName match input name.
	 * Support hierarchy nested search, eg: InName = "Content/ListItem/NameLabel".
	 * @param InName	The child's name that need to find, case sensitive
	 * @param IncludeChildren	Also search in children
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		UDreamWidget* FindChildByDisplayName(const FString& InName, bool IncludeChildren = false)const;
	/**
	 * Like "FindChildByDisplayName", but return all children that match the case.
	 * @param InName	The child's name that need to find, case sensitive
	 * @param IncludeChildren	Also search in children
	 */
	UFUNCTION(BlueprintCallable, Category = DreamGUI)
		TArray<UDreamWidget*> FindChildArrayByDisplayName(const FString& InName, bool IncludeChildren = false)const;
private:
	UDreamWidget* FindChildByDisplayNameWithChildren_Internal(const FString& InName)const;
	void FindChildArrayByDisplayNameWithChildren_Internal(const FString& InName, TArray<UDreamWidget*>& OutResultArray)const;
#pragma endregion Name

public:
	/** Get the canvas that render and update this UI element */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamCanvas* GetRenderCanvas() const;
	/** Is this UI element render to screen space overlay? */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool IsScreenSpaceOverlayUI()const;
	/** Is this UI element render to a RenderTarget? */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool IsRenderTargetUI()const;
	/** Is this UI element render in world space? */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		bool IsWorldSpaceUI()const;

	bool IsCanvasWidget()const { return bIsCanvasWidget; }

	/** return root Widget in hierarchy, could be null if not initialized yet. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
		UDreamWidget* GetRootWidgetInHierarchy()const { return RootWidget.Get(); }
	/** Shutdown-only accessor used while GC may already have marked the hierarchy unreachable. */
	UDreamWidget* GetRootWidgetInHierarchyEvenIfUnreachable()const { return RootWidget.GetEvenIfUnreachable(); }
	bool IsRootWidgetInHierarchy()const{return RootWidget.Get() == this;}

	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	static void MarkLayoutForRebuild(UDreamWidget* InWidget);

	/**
	 * Same, but saying which kind of change it was. See EDreamLayoutInvalidation.
	 *
	 * The distinction was already in the code as a comment - several position setters read "only
	 * position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not"
	 * - but with one bool to invalidate through, the only choices were the whole ancestor chain or
	 * nothing. This lets a site say what actually changed instead.
	 */
	static void MarkLayoutForRebuild(UDreamWidget* InWidget, EDreamLayoutInvalidation Reason);
	UFUNCTION(BlueprintCallable, Category = "DreamGUI")
	static void RebuildLayoutImmediately(UDreamWidget* InWidget);

	/**
	 * Marks the widget whose layout container is currently writing its results back onto its children.
	 *
	 * The write-back uses the ordinary setters, and those call MarkLayoutForRebuild, which walks the whole
	 * ancestor chain - so without this the first ancestor it reaches is the container that just consumed its
	 * own dirty flag, and every real geometry change costs two full CalculateLayoutTree passes. While the
	 * scope is alive, MarkLayoutForRebuild still dirties everything *below* the writer (a nested container
	 * has to react to the size it was just given) but stops before the writer itself.
	 */
	struct DREAMGUI_API FLayoutWriteScope
	{
		explicit FLayoutWriteScope(UDreamWidget* InLayoutWidget);
		~FLayoutWriteScope();
		FLayoutWriteScope(const FLayoutWriteScope&) = delete;
		FLayoutWriteScope& operator=(const FLayoutWriteScope&) = delete;
	private:
		bool bPushed = false;
	};

	/**
	 * True while any layout is computing and writing results, i.e. inside UpdateLayout.
	 *
	 * Distinguishes a size that is layout OUTPUT from a size somebody actually asked for. The two have to
	 * be told apart, because a panel measures an Auto child from the authored snapshot rather than from its
	 * current extent - feeding layout output back into measurement is what makes a squeezed widget measure
	 * as squeezed forever - so the snapshot must follow a real edit and ignore an arranged result.
	 *
	 * Broader than FLayoutWriteScope on purpose: that one covers the container write-back only, because it
	 * governs how far a dirty mark propagates. This covers a LayoutSelf sizing its own widget too.
	 */
	static bool IsLayoutWriting() { return LayoutPassDepth > 0; }

private:
	/** Stack of widgets whose layout containers are applying results; see FLayoutWriteScope. Game thread only. */
	static TArray<UDreamWidget*> LayoutWriterStack;
	/** Nesting depth of UpdateLayout; see IsLayoutWriting. Game thread only. */
	static int32 LayoutPassDepth;

private:
	friend class FDreamWidgetCustomization;
	friend class UDreamCanvas;
	friend class FDreamCanvasHierarchyOrderTest;
	/** Swaps arranged geometry out of AnchorData around prefab serialization; raw access, no side effects. */
	friend class FDreamUIAuthoredGeometrySaveScope;
	/** DreamCanvas which render this UI element */
	UPROPERTY(Transient) mutable TWeakObjectPtr<UDreamCanvas> RenderCanvas = nullptr;
	
	/** is this widget contains DreamCanvas component */
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
	/** Cached "any render channel is off its default", so the common case costs one bit test. */
	uint32 bHasRenderTransform : 1 = false;
	EDreamWidgetClipping LayoutClippingOverride = EDreamWidgetClipping::Inherit;

	uint32 bHasBegunPlay : 1 = false;
	uint32 bIsRegistered : 1 = false;
	uint32 bParked : 1 = false;
	/** Cached "this widget or an ancestor declares a perspective", so the usual case is one bit test. */
	uint32 bHasPerspectiveInHierarchy : 1 = false;

	/** Only for root widget, if dirty then we need to recalculate flatten hierarchy index */
	mutable uint32 bFlattenHierarchyIndexDirty : 1;
	
	void MarkClipDirty(bool InClipTypeChanged)const;
	void MarkWidgetLayoutDirty();
	
	/** find root UIItem of hierarchy */
	void CheckRootWidget(UDreamWidget* RootWidgetInParent = nullptr);

	void CalculateWidgetActive_Recursive();
	void CalculateVisibility_Recursive();
	void CalculateInteractable_Recursive();
	void CalculateRaycastable_Recursive();
public:
#pragma region TweenAnimation
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position X To"), Category = "DreamTween")
	UDreamTweener* LocalPositionXTo(double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position Y To"), Category = "DreamTween")
	UDreamTweener* LocalPositionYTo(double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "Local Position Z To"), Category = "DreamTween")
	UDreamTweener* LocalPositionZTo(double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position X To"), Category = "DreamTween")
	UDreamTweener* WorldPositionXTo(double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position Y To"), Category = "DreamTween")
	UDreamTweener* WorldPositionYTo(double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", DisplayName = "World Position Z To"), Category = "DreamTween")
	UDreamTweener* WorldPositionZTo(double endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTween")
	UDreamTweener* LocalPositionTo(FVector endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTween")
	UDreamTweener* WorldPositionTo(FVector endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = DreamTween)
	UDreamTweener* LocalScaleTo(FVector endValue = FVector(1.0f, 1.0f, 1.0f), float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = DreamTween)
	UDreamTweener* LocalUniformScaleTo(float endValue = 1.0f, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute quaternion rotation value"), Category = "DreamTween")
	UDreamTweener* LocalRotationQuaternionTo(const FQuat& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute rotator value"), Category = "DreamTween")
	UDreamTweener* LocalRotatorTo(FRotator endValue, bool shortestPath, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute quaternion rotation value"), Category = "DreamTween")
	UDreamTweener* WorldRotationQuaternionTo(const FQuat& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease", ToolTip = "Rotate absolute rotator value"), Category = "DreamTween")
	UDreamTweener* WorldRotatorTo(FRotator endValue, bool shortestPath, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
	UDreamTweener* RenderOpacityTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
	UDreamTweener* SizeDeltaTo(const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
	UDreamTweener* WidthTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
	UDreamTweener* HeightTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
	UDreamTweener* AnchoredPositionTo(const FVector2D& endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
	UDreamTweener* HorizontalAnchoredPositionTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "delay,ease"), Category = "DreamTweenGUI")
	UDreamTweener* VerticalAnchoredPositionTo(float endValue, float duration = 0.5f, float delay = 0.0f, EDreamTweenEase ease = EDreamTweenEase::OutCubic);

	UFUNCTION(BlueprintCallable, Category = "DreamTweenGUI")
	static void SetWidgetTweenerAffectByGamePauseAndTimeDilation(UDreamWidget* Widget, UDreamTweener* Tweener);
#pragma endregion
};
