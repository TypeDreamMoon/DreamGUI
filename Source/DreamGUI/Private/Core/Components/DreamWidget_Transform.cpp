// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamWidget.h"
#include "DreamWidgetPrivate.h"
#include "Core/DreamPerspective.h"
#include "DreamGUI.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRuntimeObject.h"
#include "Core/DreamScreenUISubsystem.h"
#include "Engine/World.h"
#include "DreamTweenManager.h"
#include "Core/DreamUIClipData.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamVisual.h"
#if WITH_ACCESSIBILITY
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Accessibility/SlateAccessibleMessageHandler.h"
#endif
#include "Components/SceneComponent.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetNavigation.h"
#include "Core/DreamWidgetTree.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Event/DreamPointerEventData.h"
#include "GameFramework/PlayerController.h"
// FLayoutLocalization, for the Culture flow-direction preference. SlateCore is already a public
// dependency; this is the one header of it that answers "which way does the running culture read".
#include "Layout/FlowDirection.h"
#if WITH_EDITOR
#include "UObject/GarbageCollection.h"
#include "UObject/UnrealType.h"
#endif
FVector UDreamWidget::GetWorldLocation()const
{
	return GetWorldTransform().GetLocation();
}
FQuat UDreamWidget::GetWorldRotation()const
{
	return GetWorldTransform().GetRotation();
}
FVector UDreamWidget::GetWorldScale()const
{
	return GetWorldTransform().GetScale3D();
}

FVector UDreamWidget::GetForwardVector() const
{
	return GetWorldTransform().GetRotation().GetForwardVector();
}

FVector UDreamWidget::GetRightVector() const
{
	return GetWorldTransform().GetRotation().GetRightVector();
}

FVector UDreamWidget::GetUpVector() const
{
	return GetWorldTransform().GetRotation().GetUpVector();
}

void UDreamWidget::SetRelativeLocation(const FVector& Value)
{
	if (this->RelativeLocation != Value)
	{
		this->RelativeLocation = Value;
		this->CalculateObjectToWorldTransform();
		
		if (bCanSetAnchorFromTransform)
		{
			CalculateAnchorFromTransform();
			if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
			{
				MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
			}
		}
	}
}
void UDreamWidget::SetRelativeRotation(const FQuat& Value)
{
	if (this->RelativeRotation != Value)
	{
		this->RelativeRotation = Value;
		this->RelativeRotationEuler = Value.Rotator();
		this->CalculateObjectToWorldTransform();
	}
}
void UDreamWidget::SetRelativeRotationEuler(const FRotator& Value)
{
	// Store what the caller gave us rather than round-tripping through the quaternion: that would
	// normalize the angles (370 degrees becomes 10), making an animation jump as it crosses a turn.
	this->RelativeRotationEuler = Value;
	const FQuat NewRotation = Value.Quaternion();
	if (this->RelativeRotation != NewRotation)
	{
		this->RelativeRotation = NewRotation;
		this->CalculateObjectToWorldTransform();
	}
}
void UDreamWidget::SetRelativeScale(const FVector& Value)
{
	if (this->RelativeScale != Value)
	{
		this->RelativeScale = Value;
		this->CalculateObjectToWorldTransform();
	}
}

void UDreamWidget::SetLayoutScale(const FVector2f& Value)
{
	const FVector2f SanitizedScale(
		FMath::IsFinite(Value.X) ? FMath::Max(0.0f, Value.X) : 1.0f,
		FMath::IsFinite(Value.Y) ? FMath::Max(0.0f, Value.Y) : 1.0f);
	if (!LayoutScale.Equals(SanitizedScale, 0.0f))
	{
		LayoutScale = SanitizedScale;
		CalculateObjectToWorldTransform();
	}
}
void UDreamWidget::SetRelativeLocationAndRotation(const FVector& InLocation, const FQuat& InRotation)
{
	if (this->RelativeLocation != InLocation || this->RelativeRotation != InRotation)
	{
		this->RelativeLocation = InLocation;
		this->RelativeRotation = InRotation;
		this->RelativeRotationEuler = InRotation.Rotator();
		this->CalculateObjectToWorldTransform();

		if (bCanSetAnchorFromTransform)
		{
			CalculateAnchorFromTransform();
			if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
			{
				MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
			}
		}
	}
}

void UDreamWidget::SetWorldLocation(const FVector& Value)
{
	auto WorldRotation = GetWorldRotation();
	SetWorldLocationAndRotation(Value, WorldRotation);
}
void UDreamWidget::SetWorldRotation(const FQuat& Value)
{
	auto WorldPosition = GetWorldLocation();
	SetWorldLocationAndRotation(WorldPosition, Value);
}
void UDreamWidget::SetWorldLocationAndRotation(const FVector& InLocation, const FQuat& InRotation)
{
	FTransform DesiredWorldTransform = GetWorldTransform();
	DesiredWorldTransform.SetLocation(InLocation);
	DesiredWorldTransform.SetRotation(InRotation);
	SetWorldTransform(DesiredWorldTransform);
}

FTransform UDreamWidget::GetLocalTransform()const
{
	return FTransform(RelativeRotation, RelativeLocation,
		RelativeScale * FVector(1.0, LayoutScale.X, LayoutScale.Y));
}

FTransform UDreamWidget::GetRenderTransform()const
{
	if (!bHasRenderTransform)
	{
		return FTransform::Identity;
	}
	// The pivot is normalized inside the widget's own rect, so it has to be resolved against the
	// current size -- a widget stretched by its layout must still turn about its own middle. It sits
	// on the widget's plane, which is local X = 0.
	const FVector PivotPoint(0.0,
		GetLocalSpaceLeft() + GetWidth() * RenderTransformPivot.X,
		GetLocalSpaceBottom() + GetHeight() * RenderTransformPivot.Y);
	const FTransform ScaleAndRotate(RenderRotation.Quaternion(), FVector::ZeroVector, RenderScale);
	// Bracket by the pivot, then translate. FTransform composes left-to-right as "apply A, then B".
	return FTransform(-PivotPoint) * ScaleAndRotate * FTransform(PivotPoint) * FTransform(RenderTranslation);
}

FTransform UDreamWidget::GetRenderLocalTransform()const
{
	// One bit test on the overwhelmingly common path: nothing is animating, so nothing is composed.
	return bHasRenderTransform ? GetRenderTransform() * GetLocalTransform() : GetLocalTransform();
}

FTransform UDreamWidget::GetLayoutWorldTransform()const
{
	// Deliberately recomputed by walking up rather than cached: this is only asked for when a world
	// transform is being converted back into authored data, which is rare, and a second cached chain
	// would be a second thing to keep in step on every move.
	const FTransform LocalTransform = GetLocalTransform();
	if (Parent.IsValid())
	{
		return LocalTransform * Parent->GetLayoutWorldTransform();
	}
	if (auto WidgetPresenterComponent = GetAttachedRootSceneComponent())
	{
		return LocalTransform * WidgetPresenterComponent->GetComponentTransform();
	}
	return LocalTransform;
}

void UDreamWidget::SetRenderTranslation(const FVector& Value)
{
	if (this->RenderTranslation != Value)
	{
		this->RenderTranslation = Value;
		this->ApplyRenderTransformChange();
	}
}

void UDreamWidget::SetRenderRotation(const FRotator& Value)
{
	if (this->RenderRotation != Value)
	{
		this->RenderRotation = Value;
		this->ApplyRenderTransformChange();
	}
}

void UDreamWidget::SetRenderScale(const FVector& Value)
{
	if (this->RenderScale != Value)
	{
		this->RenderScale = Value;
		this->ApplyRenderTransformChange();
	}
}

void UDreamWidget::SetRenderTransformPivot(const FVector2D& Value)
{
	if (this->RenderTransformPivot != Value)
	{
		this->RenderTransformPivot = Value;
		this->ApplyRenderTransformChange();
	}
}

void UDreamWidget::ClearRenderTransform()
{
	if (bHasRenderTransform)
	{
		RenderTranslation = FVector::ZeroVector;
		RenderRotation = FRotator::ZeroRotator;
		RenderScale = FVector::OneVector;
		ApplyRenderTransformChange();
	}
	// Its own branch: shear is not part of bHasRenderTransform (see RefreshRenderTransformFlag), so a
	// widget that is only sheared would otherwise walk out of "clear the render transform" still slanted.
	if (HasOwnRenderShear())
	{
		RenderShear = FVector2D::ZeroVector;
		ApplyRenderShearChange();
	}
}

bool UDreamWidget::GetPerspectiveScope(DreamPerspective::FScope& OutScope)const
{
	if (!bPerspective)
	{
		return false;
	}
	// Layout's transform, not the drawn one. The scope's plane has to be where LAYOUT put this
	// widget: measured against its own drawn transform the declarer is flat in its own plane by
	// construction, and no perspective of its own could ever reach it. Taken from layout, the
	// widget's own render rotation is a departure from that plane and foreshortens like anything
	// else -- while a widget with no render transform still lies in the plane and is left alone.
	const FTransform World = GetLayoutWorldTransform();
	// The origin sits on this widget's own rect, like RenderTransformPivot, so it tracks a widget
	// that gets stretched by its layout rather than drifting off it.
	const FVector LocalOrigin(0.0,
		GetLocalSpaceLeft() + GetWidth() * PerspectiveOrigin.X,
		GetLocalSpaceBottom() + GetHeight() * PerspectiveOrigin.Y);
	OutScope.PlanePoint = World.TransformPosition(LocalOrigin);
	OutScope.PlaneNormal = World.TransformVector(FVector::XAxisVector).GetSafeNormal();
	// MINUS the normal. A widget's local +X points INTO the screen, not out of it:
	// UDreamCanvas::GetViewLocation puts the viewer at "world location - forward * distance", so the
	// eye is on the -X side and +X is depth away from it. Placing the scope's eye on +X would put
	// it behind the plane, opposite the canvas's own eye, and the remap would then re-aim from a
	// viewer in front to a viewer behind -- foreshortening inverted and overstated. The eye has to
	// sit on the same side of the plane as the one that is actually looking.
	OutScope.EyePosition = OutScope.PlanePoint - OutScope.PlaneNormal * GetPerspectiveDistance();
	return true;
}

FMatrix UDreamWidget::GetInheritedPerspectiveRemap()const
{
	if (!HasPerspectiveApplied())
	{
		return FMatrix::Identity;
	}
	// Walking up gathers the scopes innermost first, which is the order the composition wants.
	// Deliberately not cached per widget: caching would be two matrices on every widget in the
	// project to speed up a subtree that usually does not exist, and this is asked for once per
	// geometry rebuild rather than per vertex.
	TArray<DreamPerspective::FScope, TInlineAllocator<4>> Scopes;
	for (const UDreamWidget* Ancestor = this; Ancestor != nullptr; Ancestor = Ancestor->Parent.Get())
	{
		DreamPerspective::FScope Scope;
		if (Ancestor->GetPerspectiveScope(Scope))
		{
			Scopes.Add(Scope);
		}
	}
	// The whole feature works by re-aiming geometry at the eye the canvas projects from, so it only
	// means anything where that eye is what is actually looking. In a world-space mode the scene
	// camera does the projecting and the canvas's eye is nobody; re-aiming at it would displace the
	// geometry for a viewer that does not exist. And an orthographic canvas has its eye at infinity,
	// which no affine map can reach -- CalculateDistanceToCamera even returns a hard 1000 there,
	// a number that means "not applicable" rather than a distance.
	UDreamCanvas* Canvas = GetRenderCanvas();
	UDreamCanvas* Root = Canvas ? Canvas->GetRootCanvas() : nullptr;
	if (Root == nullptr || Root->IsRenderToWorldSpace()
		|| Root->GetProjectionType() != ECameraProjectionMode::Perspective)
	{
		return FMatrix::Identity;
	}
	return DreamPerspective::ComposeRemap(Scopes, Root->GetViewLocation());
}

namespace DreamRenderShear
{
	/**
	 * One widget's shear as a matrix in its own local space, origin at its pivot -- which is where the
	 * local origin already is, so there is no recentring to do.
	 *
	 * Local X is DEPTH here and the widget's rect lies in the YZ plane, so the two channels slant Y
	 * against Z and Z against Y. Row-vector convention, matching everything else in this file: a
	 * point p becomes p*M, so M's Y row is what Y contributes to the result.
	 *
	 * Clamped just under a right angle for the same reason Slate clamps: tan(90) is infinite and one
	 * authored 90 would send every vertex of the subtree to infinity.
	 */
	FMatrix MakeLocalShearMatrix(const FVector2D& InShearDegrees)
	{
		const double ClampedX = FMath::Clamp(InShearDegrees.X, -89.0, 89.0);
		const double ClampedY = FMath::Clamp(InShearDegrees.Y, -89.0, 89.0);
		const double TanX = FMath::Tan(FMath::DegreesToRadians(ClampedX));
		const double TanY = FMath::Tan(FMath::DegreesToRadians(ClampedY));
		FMatrix Result = FMatrix::Identity;
		// Y' = Y + Z*TanX : the horizontal slant, the one that turns a rectangle into a parallelogram
		// leaning left or right. Z' = Y*TanY + Z is the vertical one.
		Result.M[2][1] = TanX;
		Result.M[1][2] = TanY;
		return Result;
	}
}

FMatrix UDreamWidget::GetInheritedShearCorrection()const
{
	// C(X) = O(X)^-1 * S(X) * O(X) * C(parent), read as "apply this level's shear in world terms,
	// then whatever the levels above already contribute". Walking up rather than caching: the chain
	// is only ever consulted while something in it is sheared, and a cache would need invalidating
	// from every transform change in the subtree to stay honest.
	FMatrix Result = Parent.IsValid() ? Parent->GetInheritedShearCorrection() : FMatrix::Identity;
	if (HasOwnRenderShear())
	{
		const FMatrix ObjectToWorld = ObjectToWorldTransform.ToMatrixWithScale();
		Result = ObjectToWorld.Inverse() * DreamRenderShear::MakeLocalShearMatrix(RenderShear) * ObjectToWorld * Result;
	}
	return Result;
}

FMatrix UDreamWidget::GetWorldMatrix()const
{
	FMatrix Result = ObjectToWorldTransform.ToMatrixWithScale();
	// Both multiplications are skipped when they would be by Identity, so a widget using neither
	// feature gets exactly the matrix this function has always returned.
	if (HasShearApplied())
	{
		Result = Result * GetInheritedShearCorrection();
	}
	return HasPerspectiveApplied() ? Result * GetInheritedPerspectiveRemap() : Result;
}

FMatrix UDreamWidget::GetInverseWorldMatrix()const
{
	return GetWorldMatrix().Inverse();
}

void UDreamWidget::SetPerspective(bool Value)
{
	if (bPerspective != Value)
	{
		bPerspective = Value;
		ApplyPerspectiveChange();
	}
}

float UDreamWidget::GetPerspectiveDistance()const
{
	// The same formula UDreamCanvas::CalculateDistanceToCamera uses, so a widget's field of view means
	// what the canvas's field of view means. Derived rather than stored: the widget's width is what
	// makes the angle scale-invariant, and that width changes.
	const float Clamped = FMath::Clamp(PerspectiveFieldOfView, 1.0f, 179.0f);
	return GetWidth() * 0.5f / FMath::Tan(FMath::DegreesToRadians(Clamped * 0.5f));
}

void UDreamWidget::SetPerspectiveFieldOfView(float Value)
{
	const float Sanitized = FMath::Clamp(Value, 1.0f, 179.0f);
	if (PerspectiveFieldOfView != Sanitized)
	{
		PerspectiveFieldOfView = Sanitized;
		ApplyPerspectiveChange();
	}
}

void UDreamWidget::SetPerspectiveOrigin(const FVector2D& Value)
{
	if (PerspectiveOrigin != Value)
	{
		PerspectiveOrigin = Value;
		ApplyPerspectiveChange();
	}
}

#if WITH_EDITOR
void UDreamWidget::WarnIfPerspectiveCannotApply()const
{
	if (!bPerspective)
	{
		return;
	}
	UDreamCanvas* Canvas = GetRenderCanvas();
	UDreamCanvas* Root = Canvas ? Canvas->GetRootCanvas() : nullptr;
	const TCHAR* Reason = nullptr;
	if (Root == nullptr)
	{
		Reason = TEXT("this widget is not under a canvas");
	}
	else if (Root->IsRenderToWorldSpace())
	{
		// The designer previews in world space by default, so this is the message an author is
		// most likely to need: the field they are adjusting is genuinely inert in front of them.
		Reason = TEXT("its canvas renders in world space, where the scene camera does the projecting and already supplies perspective of its own. Perspective is defined against a canvas's own virtual camera, so switch the preview canvas to ScreenSpaceOverlay (the Screen Space button on the toolbar). The remap is baked into the geometry, so it then shows in the editor viewport too -- use Canvas Eye to view it from the projection it was built for");
	}
	else if (Root->GetProjectionType() != ECameraProjectionMode::Perspective)
	{
		Reason = TEXT("its canvas is orthographic, which has its eye at infinity and no perspective to share");
	}
	if (Reason != nullptr)
	{
		UE_LOG(DreamGUI, Warning, TEXT("Perspective on '%s' has no effect here, because %s."),
			*GetPathDisplayName(), Reason);
	}
}
#endif

void UDreamWidget::ApplyPerspectiveChange()
{
	// Same shape as a render transform change and pointedly not a layout one: a perspective moves
	// where things are drawn, never where the layout believes they are.
	RefreshPerspectiveInHierarchy();
	CalculateObjectToWorldTransform(true);
	MarkCanvasUpdate(true);
#if WITH_EDITOR
	// Said at the moment the author touches it, which is the moment they are wondering.
	WarnIfPerspectiveCannotApply();
#endif
}

void UDreamWidget::RefreshPerspectiveInHierarchy()
{
	const bool bNew = bPerspective || (Parent.IsValid() && Parent->bHasPerspectiveInHierarchy);
	if (bHasPerspectiveInHierarchy != bNew)
	{
		bHasPerspectiveInHierarchy = bNew;
		for (UDreamWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				Child->RefreshPerspectiveInHierarchy();
			}
		}
	}
}

void UDreamWidget::RefreshShearInHierarchy()
{
	const bool bNew = HasOwnRenderShear() || (Parent.IsValid() && Parent->bHasShearInHierarchy);
	if (bHasShearInHierarchy != bNew)
	{
		bHasShearInHierarchy = bNew;
		for (UDreamWidget* Child : Children)
		{
			if (IsValid(Child))
			{
				Child->RefreshShearInHierarchy();
			}
		}
	}
}

void UDreamWidget::ApplyRenderShearChange()
{
	// Same shape as ApplyPerspectiveChange and for the same reason: a shear moves where things are
	// drawn, never where the layout believes they are. CalculateObjectToWorldTransform(true) is what
	// walks the subtree and re-runs the geometry; the shear itself contributes nothing to the
	// FTransform it recomputes, which is precisely why it has to ride the matrix path instead.
	RefreshShearInHierarchy();
	CalculateObjectToWorldTransform(true);
	MarkCanvasUpdate(true);
}

void UDreamWidget::SetRenderShear(const FVector2D& Value)
{
	if (!RenderShear.Equals(Value))
	{
		RenderShear = Value;
		ApplyRenderShearChange();
	}
}

void UDreamWidget::RefreshRenderTransformFlag()
{
	// RenderShear is deliberately absent. This flag decides whether GetLocalTransform is composed
	// with a render FTransform, and a shear cannot survive that trip -- it is applied on the matrix
	// path instead, and folding it in here would only make the FTransform silently orthonormalize it.
	bHasRenderTransform = !RenderTranslation.IsNearlyZero()
		|| !RenderRotation.IsNearlyZero()
		|| !RenderScale.Equals(FVector::OneVector);
}

void UDreamWidget::ApplyRenderTransformChange()
{
	RefreshRenderTransformFlag();
	// Exactly what SetLayoutScale does, and pointedly NOT what SetRelativeLocation does: no
	// CalculateAnchorFromTransform, no MarkLayoutForRebuild. Those two lines are the reason
	// animating a laid-out widget's position fights the layout instead of moving it.
	CalculateObjectToWorldTransform(true);
}
const FTransform& UDreamWidget::GetWorldTransform()const
{
	return ObjectToWorldTransform;
}

bool UDreamWidget::GetWorldRectBoundingSphere(FVector& OutCenter, double& OutRadius)const
{
	// A perspective scope never reaches ObjectToWorldTransform: LineTraceUIRect switches to
	// GetWorldMatrix() when one applies, and that folds in GetInheritedPerspectiveRemap, which is
	// built from the root canvas's eye position. The eye moves with the canvas and nothing tells this
	// widget when it does, so no invalidation hook could keep a sphere honest here -- and a stale one
	// would silently make a foreshortened widget unclickable. Refuse instead; it is a rare feature and
	// the exact test is still correct.
	if (HasPerspectiveApplied())
	{
		return false;
	}
	if (bWorldRectBoundsDirty)
	{
		// GetWidth/GetHeight resolve a stretched size against the parent and dirty THIS cache while
		// doing it, so the flag is cleared only after both have been asked.
		const double Width = GetWidth();
		const double Height = GetHeight();
		const FVector2D LocalCenter = GetLocalSpaceCenter();
		// The rect lies on local X = 0, so only the two in-plane scales can stretch it. FTransform
		// scales before it rotates, so a local (0, y, z) lands at R * (0, Sy*y, Sz*z) + T, whose
		// distance from the transformed centre is at most Max(|Sy|, |Sz|) times the local one --
		// exact under uniform scale, an over-estimate otherwise, which is the direction to err in.
		const FVector Scale = ObjectToWorldTransform.GetScale3D();
		const double MaxPlaneScale = FMath::Max(FMath::Abs(Scale.Y), FMath::Abs(Scale.Z));
		const double HalfDiagonal = 0.5 * FMath::Sqrt(Width * Width + Height * Height) * MaxPlaneScale;
		CacheWorldRectCenter = ObjectToWorldTransform.TransformPosition(FVector(0.0, LocalCenter.X, LocalCenter.Y));
		// A hair of slack on a real rect, so a click landing exactly on a corner cannot be thrown out
		// by the last bit of a square root: a coarse test that is tighter than the exact test it
		// stands in front of is a lost hit, which is the one failure mode this must not have. A
		// degenerate rect gets no slack and is reported as no bound at all.
		CacheWorldRectRadius = HalfDiagonal > 0.0 ? HalfDiagonal * 1.001 + UE_KINDA_SMALL_NUMBER : 0.0;
		bWorldRectBoundsDirty = false;
	}
	// Spelt as a negation so a NaN radius -- from a width nobody has measured -- answers "no bound"
	// rather than sailing through as a sphere no ray can ever be inside.
	if (!(CacheWorldRectRadius > 0.0))
	{
		return false;
	}
	OutCenter = CacheWorldRectCenter;
	OutRadius = CacheWorldRectRadius;
	return true;
}

void UDreamWidget::SetWorldTransform(const FTransform& InWorldTransform)
{
	const FVector PreviousAuthoredScale = RelativeScale;
	auto ResolveAuthoredScale = [this, &PreviousAuthoredScale](const FVector& EffectiveLocalScale)
	{
		FVector Result = EffectiveLocalScale;
		Result.Y = FMath::IsNearlyZero(LayoutScale.X) ? PreviousAuthoredScale.Y : EffectiveLocalScale.Y / LayoutScale.X;
		Result.Z = FMath::IsNearlyZero(LayoutScale.Y) ? PreviousAuthoredScale.Z : EffectiveLocalScale.Z / LayoutScale.Y;
		return Result;
	};
	FTransform LocalTransform = InWorldTransform;
	if (Parent.IsValid())
	{
		// Layout's basis, not the drawn one: if an ancestor is mid-animation, dividing by the drawn
		// transform would fold that offset into this widget's authored RelativeLocation.
		LocalTransform = InWorldTransform.GetRelativeTransform(Parent->GetLayoutWorldTransform());
	}
	else if (const USceneComponent* WidgetPresenterComponent = GetAttachedRootSceneComponent())
	{
		LocalTransform = InWorldTransform.GetRelativeTransform(WidgetPresenterComponent->GetComponentTransform());
	}

	this->RelativeLocation = LocalTransform.GetLocation();
	this->RelativeRotation = LocalTransform.GetRotation();
	this->RelativeRotationEuler = this->RelativeRotation.Rotator();
	this->RelativeScale = ResolveAuthoredScale(LocalTransform.GetScale3D());
	CalculateObjectToWorldTransform(true);
	if (bCanSetAnchorFromTransform)
	{
		CalculateAnchorFromTransform();
		MarkLayoutForRebuild(this);
	}
}

void UDreamWidget::UpdateObjectToWorldTransform()
{
	auto LocalTransform = GetRenderLocalTransform();
	if (Parent.IsValid())
	{
		ObjectToWorldTransform = LocalTransform * Parent->GetWorldTransform();
	}
	else
	{
		if (auto WidgetPresenterComponent = GetAttachedRootSceneComponent())
		{
			ObjectToWorldTransform = LocalTransform * WidgetPresenterComponent->GetComponentTransform();
		}
		else
		{
			ObjectToWorldTransform = LocalTransform;
		}
	}
	this->MarkTransformChanged();
}
void UDreamWidget::CalculateObjectToWorldTransform(bool bPropagateToChildren)
{
	this->UpdateObjectToWorldTransform();
	this->OnUpdateTransform();
	if (bPropagateToChildren)
	{
		for (UDreamWidget* Child : this->Children)
		{
			if (IsValid(Child))
			{
				Child->CalculateObjectToWorldTransform(true);
			}
		}
	}
}

DECLARE_CYCLE_STAT(TEXT("DreamWidget OnUpdateTransform"), STAT_OnUpdateTransform, STATGROUP_DreamGUI);
void UDreamWidget::OnUpdateTransform()
{
	SCOPE_CYCLE_COUNTER(STAT_OnUpdateTransform)
	// UE_LOG(DreamGUI, Error, TEXT("OnUpdateTransform Flag:%d %s"), (int)UpdateTransformFlags, *this->GetDisplayName());
		bool bPositionChanged = false, bRotationChanged = false, bScaleChanged = false;
	{
		auto Pos = this->GetRelativeLocation();
		auto Pos2D = FVector2D(Pos.Y, Pos.Z);
		if (Pos2D != PrevLocation2D)
		{
			PrevLocation2D = Pos2D;
			bPositionChanged = true;
		}
		auto CompScale3D = this->GetWorldScale();
		auto CompScale2D = FVector2D(CompScale3D.Y, CompScale3D.Z);
		if (PrevScale2D != CompScale2D)
		{
			PrevScale2D = CompScale2D;
			bScaleChanged = true;
		}
		if (LayoutContainer)
		{
			LayoutContainer->OnTransformChanged();
		}
		if (LayoutSelf)
		{
			LayoutSelf->OnTransformChanged();
		}
		if (Visual)
		{
			Visual->OnTransformChanged(bPositionChanged, bScaleChanged);
		}
	}
}

void UDreamWidget::SetRenderTransformAngle(float InAngle)
{
	FRotator NewRotation = RenderRotation;
	NewRotation.Roll = InAngle;
	SetRenderRotation(NewRotation);
}
#pragma region TweenAnimation
#pragma region PositionXYZ
UDreamTweener* UDreamWidget::LocalPositionXTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation().X;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.X = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::LocalPositionYTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation().Y;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.Y = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::LocalPositionZTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this] 
	{
		return this->GetRelativeLocation().Z;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetRelativeLocation();
		location.Z = value;
		this->SetRelativeLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}



UDreamTweener* UDreamWidget::WorldPositionXTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this] 
	{
		return this->GetWorldLocation().X;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.X = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::WorldPositionYTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldLocation().Y;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.Y = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::WorldPositionZTo(double endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenDoubleGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldLocation().Z;
	}), FDreamTweenDoubleSetterFunction::CreateWeakLambda(this, [this](auto value) {
		auto location = this->GetWorldLocation();
		location.Z = value;
		this->SetWorldLocation(location);
	}), endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}




#pragma endregion PositionXYZ
#pragma region Position
UDreamTweener* UDreamWidget::LocalPositionTo(FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenVectorGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeLocation();
	})
	, FDreamTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		this->SetRelativeLocation(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::WorldPositionTo(FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenVectorGetterFunction::CreateUObject(this, &UDreamWidget::GetWorldLocation)
	, FDreamTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		return this->SetWorldLocation(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
#pragma endregion Position



UDreamTweener* UDreamWidget::LocalScaleTo(FVector endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenVectorGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeScale();
	})
	, FDreamTweenVectorSetterFunction::CreateWeakLambda(this, [this](FVector value)
	{
		this->SetRelativeScale(value);
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::LocalUniformScaleTo(float endValue, float duration, float delay,	EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenFloatGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeScale().X;
	})
	, FDreamTweenFloatSetterFunction::CreateWeakLambda(this, [this](float value)
	{
		this->SetRelativeScale(FVector(value));
	})
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}


#pragma region Rotation
UDreamTweener* UDreamWidget::LocalRotationQuaternionTo(const FQuat& endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
	, FDreamTweenQuaternionGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetRelativeRotation();
	}), FDreamTweenQuaternionSetterFunction::CreateUObject(this, &UDreamWidget::SetRelativeRotation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::LocalRotatorTo(FRotator endValue, bool shortestPath, float duration, float delay, EDreamTweenEase ease)
{
	if (shortestPath)
	{
		return LocalRotationQuaternionTo(endValue.Quaternion(), duration, delay, ease);
	}
	else
	{
		auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenRotatorGetterFunction::CreateWeakLambda(this, [this]
		{
			return this->GetRelativeRotation().Rotator();
		})
		, FDreamTweenRotatorSetterFunction::CreateWeakLambda(this, [this] (FRotator value)
		{
			this->SetRelativeRotation(value.Quaternion());
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
			UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
		}
		return Tweener;
	}
}



UDreamTweener* UDreamWidget::WorldRotationQuaternionTo(const FQuat& endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this, FDreamTweenQuaternionGetterFunction::CreateWeakLambda(this, [this]
	{
		return this->GetWorldRotation();
	}), FDreamTweenQuaternionSetterFunction::CreateUObject(this, &UDreamWidget::SetWorldRotation)
	, endValue, duration);
	if (Tweener)
	{
		Tweener->SetDelay(delay)->SetEase(ease);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}
UDreamTweener* UDreamWidget::WorldRotatorTo(FRotator endValue, bool shortestPath, float duration, float delay, EDreamTweenEase ease)
{
	if (shortestPath)
	{
		return WorldRotationQuaternionTo(endValue.Quaternion(), duration, delay, ease);
	}
	else
	{
		auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenRotatorGetterFunction::CreateWeakLambda(this, [this]
		{
			return this->GetWorldRotation().Rotator();
		})
		, FDreamTweenRotatorSetterFunction::CreateWeakLambda(this, [this](FRotator value)
		{
			this->SetWorldRotation(value.Quaternion());
		}), endValue, duration);
		if (Tweener)
		{
			Tweener->SetDelay(delay)->SetEase(ease);
			UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
		}
		return Tweener;
	}
}

#pragma endregion Rotation


UDreamTweener* UDreamWidget::RenderOpacityTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetRenderOpacity)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetRenderOpacity)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::SizeDeltaTo(const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenVector2DGetterFunction::CreateUObject(this, &UDreamWidget::GetSizeDelta)
		, FDreamTweenVector2DSetterFunction::CreateUObject(this, &UDreamWidget::SetSizeDelta)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::WidthTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetWidth)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetWidth)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::HeightTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetHeight)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetHeight)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::AnchoredPositionTo(const FVector2D& endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenVector2DGetterFunction::CreateUObject(this, &UDreamWidget::GetAnchoredPosition)
		, FDreamTweenVector2DSetterFunction::CreateUObject(this, &UDreamWidget::SetAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::HorizontalAnchoredPositionTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetHorizontalAnchoredPosition)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetHorizontalAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

UDreamTweener* UDreamWidget::VerticalAnchoredPositionTo(float endValue, float duration, float delay, EDreamTweenEase ease)
{
	auto Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateUObject(this, &UDreamWidget::GetVerticalAnchoredPosition)
		, FDreamTweenFloatSetterFunction::CreateUObject(this, &UDreamWidget::SetVerticalAnchoredPosition)
		, endValue, duration);
	if (Tweener)
	{
		Tweener->SetEase(ease)->SetDelay(delay);
		UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(this, Tweener);
	}
	return Tweener;
}

void UDreamWidget::SetWidgetTweenerAffectByGamePauseAndTimeDilation(UDreamWidget* Widget, UDreamTweener* Tweener)
{
	if (Tweener)
	{
		bool bAffectByGamePause;
		bool bAffectByTimeDilation;
		// A missing widget is an ordinary argument here, not a caller's mistake: the visuals' and the
		// behaviours' tween verbs (UDreamVisual::ColorTo and its siblings, UDreamRectBlock's, the
		// selectables' transitions) all pass their own GetWidget(), which answers null for a sub-object no
		// widget outers -- one made on its own, or one whose outer chain is being taken apart -- and
		// Blueprint can pass anything. The tween is still a real one, since it animates the caller's own
		// property; only the choice of clock has to be made without a widget. It is made the way it
		// already is for a widget with no render canvas, where IsScreenSpaceOverlayUI answers false:
		// the world-space pair. Something not known to be on the screen does not get the screen's
		// exemption from pause and time dilation.
		if (IsValid(Widget) && Widget->IsScreenSpaceOverlayUI())
		{
			bAffectByGamePause = GetDefault<UDreamUISettings>()->bScreenSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<UDreamUISettings>()->bScreenSpaceUIAffectByTimeDilation;
		}
		else
		{
			bAffectByGamePause = GetDefault<UDreamUISettings>()->bWorldSpaceUIAffectByGamePause;
			bAffectByTimeDilation = GetDefault<UDreamUISettings>()->bWorldSpaceUIAffectByTimeDilation;
		}
		Tweener->SetAffectByGamePause(bAffectByGamePause)->SetAffectByTimeDilation(bAffectByTimeDilation);
	}
}
#pragma endregion
