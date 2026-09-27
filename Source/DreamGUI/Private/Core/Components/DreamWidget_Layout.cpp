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


void UDreamWidget::CalculateAnchorFromTransform()
{
	auto TempRelativeLocation = this->GetRelativeLocation();
	FVector2D CalculatedAnchoredPosition;
	if (Parent.IsValid())
	{
		//just a reverse operation from CalculateTransformFromAnchor
		float LocalLeftPoint =
			Parent->GetLocalSpaceLeft()
			+ (Parent->GetWidth() * this->AnchorData.AnchorMin.X);

		float LocalBottomPoint =
			Parent->GetLocalSpaceBottom()
			+ (Parent->GetHeight() * this->AnchorData.AnchorMin.Y);

		CalculatedAnchoredPosition.X = TempRelativeLocation.Y
			- LocalLeftPoint
			- +(Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X)) * this->AnchorData.Pivot.X;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z
			- LocalBottomPoint
			- (Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y)) * this->AnchorData.Pivot.Y;
	}
	else
	{
		CalculatedAnchoredPosition.X = TempRelativeLocation.Y;
		CalculatedAnchoredPosition.Y = TempRelativeLocation.Z;
	}

	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;

	if (AnchorData.AnchoredPosition != CalculatedAnchoredPosition)
	{
		AnchorData.AnchoredPosition = CalculatedAnchoredPosition;
	}
}
void UDreamWidget::CalculateTransformFromAnchor()
{
	bool HorizontalPositionChanged = false, VerticalPositionChanged = false;
	CalculateTransformFromAnchor(HorizontalPositionChanged, VerticalPositionChanged);
}
void UDreamWidget::CalculateTransformFromAnchor(bool& OutHorizontalPositionChanged, bool& OutVerticalPositionChanged)
{
	bCanSetAnchorFromTransform = false;
	FVector ResultLocation = this->GetRelativeLocation();
	if (Parent.IsValid())
	{
		float LocalLeftPoint = //this left point anchor position in parent's space
			Parent->GetLocalSpaceLeft()//parent's left position
			+ (Parent->GetWidth() * this->AnchorData.AnchorMin.X);//add anchor offset
		float LocalLeftPivotPoint = //to pivot point, with anchor offset
			LocalLeftPoint
			+ (Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X))//parent anchor width (width without SizeDelta)
				* this->AnchorData.Pivot.X
			+ this->AnchorData.AnchoredPosition.X;

		float LocalBottomPoint = //this bottom point anchor position in parent's space
			Parent->GetLocalSpaceBottom()//parent's bottom position
			+ (Parent->GetHeight() * this->AnchorData.AnchorMin.Y);//add anchor offset
		float LocalBottomPivotPoint = //to pivot point, with anchor offset
			LocalBottomPoint
			+ (Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y))//parent anchor width (width without SizeDelta)
				* this->AnchorData.Pivot.Y
			+ this->AnchorData.AnchoredPosition.Y;

		ResultLocation.Y = LocalLeftPivotPoint;
		ResultLocation.Z = LocalBottomPivotPoint;
	}
	else
	{
		ResultLocation.Y = this->AnchorData.AnchoredPosition.X;
		ResultLocation.Z = this->AnchorData.AnchoredPosition.Y;
	}

	auto OriginRelativeLocation = this->GetRelativeLocation();
	double Tolerance = 0.0f;
	if (FMath::Abs(OriginRelativeLocation.Y - ResultLocation.Y) > Tolerance)
	{
		OutHorizontalPositionChanged = true;
	}
	if (FMath::Abs(OriginRelativeLocation.Z - ResultLocation.Z) > Tolerance)
	{
		OutVerticalPositionChanged = true;
	}
	if (OutHorizontalPositionChanged || OutVerticalPositionChanged)
	{
		this->SetRelativeLocation(ResultLocation);
	}
	bCanSetAnchorFromTransform = true;
}

#pragma region AnchorData

void UDreamWidget::SyncAnimatableGeometryMirrors() const
{
	auto* MutableThis = const_cast<UDreamWidget*>(this);
	MutableThis->AnimatableWidth = CacheWidth;
	MutableThis->AnimatableHeight = CacheHeight;
	MutableThis->AnimatableAnchorLeft = CacheAnchorOffsetLeft;
	MutableThis->AnimatableAnchorRight = CacheAnchorOffsetRight;
	MutableThis->AnimatableAnchorTop = CacheAnchorOffsetTop;
	MutableThis->AnimatableAnchorBottom = CacheAnchorOffsetBottom;
}

float UDreamWidget::GetWidth() const
{
	if (bCacheWidthDirty)
	{
		bCacheWidthDirty = false;
		// The backstop for the world-rect cache. MarkDimensionChanged covers the announced paths, but
		// several do not announce -- MarkAllDirty simply dirties this flag, and a stretched child
		// resolves against its parent lazily, here, long after the parent moved. Whatever reached this
		// branch is a width nobody had, so the sphere built from the old one is gone too.
		MarkWorldRectBoundsDirty();
		SyncAnimatableGeometryMirrors();
		if (Parent.IsValid())
		{
			if (AnchorData.IsHorizontalStretched())
			{
				CacheWidth = AnchorData.SizeDelta.X + Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X);
			}
			else
			{
				CacheWidth = AnchorData.SizeDelta.X;
			}
		}
		else
		{
			CacheWidth = AnchorData.SizeDelta.X;
		}
	}
	// A resolved size is never negative. Offsets that cross -- a stretched child whose left and right
	// padding together exceed the parent's span, or a plainly authored negative -- describe an EMPTY
	// rect, not an inverted one, and every consumer here multiplies the width straight through:
	// GetLocalSpaceLeft is -W*Pivot and GetLocalSpaceRight is W*(1-Pivot), so a negative W swaps the two
	// edges and turns the hit rect inside out, while GetWorldRectBoundingSphere's W*W + H*H keeps
	// reporting a healthy positive radius over it. Panels already floor their own output
	// (DreamPanelLayoutLocal::CleanSize); this is that floor for every other path into the tree.
	// AnchorData keeps the true, possibly negative, SizeDelta -- only the resolved answer is floored, so
	// anchor offsets still round-trip exactly.
	return FMath::Max(0.0f, CacheWidth);
}
float UDreamWidget::GetHeight() const
{
	if (bCacheHeightDirty)
	{
		bCacheHeightDirty = false;
		// The vertical twin of the backstop in GetWidth.
		MarkWorldRectBoundsDirty();
		SyncAnimatableGeometryMirrors();
		if (Parent.IsValid())
		{
			if (AnchorData.IsVerticalStretched())
			{
				CacheHeight = AnchorData.SizeDelta.Y + Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y);
			}
			else
			{
				CacheHeight = AnchorData.SizeDelta.Y;
			}
		}
		else
		{
			CacheHeight = AnchorData.SizeDelta.Y;
		}
	}
	/** The vertical twin of the floor in GetWidth. */
	return FMath::Max(0.0f, CacheHeight);
}

void UDreamWidget::SetAnchorData(const FDreamUIAnchorData& Value)
{
	AnchorData.Pivot = Value.Pivot;
	AnchorData.AnchorMin = Value.AnchorMin;
	AnchorData.AnchorMax = Value.AnchorMax;
	AnchorData.AnchoredPosition = Value.AnchoredPosition;
	AnchorData.SizeDelta = Value.SizeDelta;

	bCacheWidthDirty = true;
	bCacheHeightDirty = true;
	bCacheAnchorOffsetLeftDirty = true;
	bCacheAnchorOffsetRightDirty = true;
	bCacheAnchorOffsetBottomDirty = true;
	bCacheAnchorOffsetTopDirty = true;

	MarkAnchorDataChanged_Recursive(true, true, true, false);
	MarkLayoutForRebuild(this);
}

void UDreamWidget::SetPivot(FVector2D Value) 
{
	if (!AnchorData.Pivot.Equals(Value, 0.0f))
	{
		AnchorData.Pivot = Value;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		MarkAnchorDataChanged_Recursive(true, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetAnchorMin(FVector2D Value)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMin.Equals(Value, 0.0f))
		{
			AnchorData.AnchorMin = Value;
			// Moving an anchor line moves the reference the rect is expressed against; it does not move
			// the rect relative to that reference. Both anchor offsets are functions of AnchoredPosition,
			// SizeDelta and Pivot alone (GetAnchorOffsetLeft/Right never read an anchor), so keeping them
			// costs nothing and the arithmetic that used to stand here was an identity: -Right - Left IS
			// SizeDelta.X, and CurrentLeft + SizeDelta.X * Pivot.X IS AnchoredPosition.X. Its one net
			// effect was to publish the size DELTA as the RESOLVED width and leave bCacheWidthDirty
			// clear, so a point-anchored widget turned stretched by this setter answered GetWidth() with
			// the delta until some unrelated path happened to dirty the cache.
			//
			// What genuinely changes is the resolved size: a stretched axis resolves to the delta PLUS
			// the parent's span across the anchors, and that span just moved. Hence InDiscardCache=true,
			// which is exactly how the 2026-09-01 fixes to SetWidth/SetHeight/SetAnchoredPositionAndSizeDelta
			// spell the same thing; these two setters were the ones that pass missed.
			MarkAnchorDataChanged_Recursive(false, true, true, true);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}
void UDreamWidget::SetAnchorMax(FVector2D Value)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMax.Equals(Value, 0.0f))
		{
			AnchorData.AnchorMax = Value;
			// The AnchorMax twin of SetAnchorMin, defect included and removed the same way.
			MarkAnchorDataChanged_Recursive(false, true, true, true);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

/**
 * The four-in-one form of SetAnchorOffsetLeft/Right/Top/Bottom, and now written the same way they are.
 *
 * Three separate defects lived in the old shape. CacheWidth was assigned `-Right - Left`, which is the
 * size DELTA and not the resolved width (they differ by the parent's span on a stretched axis), and no
 * branch in the function touched bCacheWidthDirty -- so DreamTextInput's `SetHorizontalAndVerticalAnchorMinMax
 * (stretch) + SetAnchorOffset(Padding)` pair left its clip and placeholder nodes answering GetWidth()
 * with the negated padding sum. The change test compared possibly-stale caches without first asking
 * whether they were dirty. And the early-exit tail cleared bCacheAnchorOffsetLeftDirty alone, leaving
 * Left holding a freshly written value while Right/Top/Bottom were still owed a resolve against
 * AnchorData -- a set the next getter would resolve into something inconsistent with it.
 *
 * Here the offsets are the authored intent, the resolved size follows from them plus the parent's span,
 * and the delta is what is left of that size once the span is subtracted (stretched axes only). Every
 * cache written is a value this function actually computed, so InDiscardCache stays false, exactly as
 * in the four single-axis setters.
 */
void UDreamWidget::SetAnchorOffset(FMargin Value)
{
	if (this->Parent.IsValid())
	{
		const bool bWidthChange = bCacheAnchorOffsetLeftDirty || bCacheAnchorOffsetRightDirty
			|| CacheAnchorOffsetLeft != Value.Left || CacheAnchorOffsetRight != Value.Right;
		const bool bHeightChange = bCacheAnchorOffsetBottomDirty || bCacheAnchorOffsetTopDirty
			|| CacheAnchorOffsetBottom != Value.Bottom || CacheAnchorOffsetTop != Value.Top;
		if (!bWidthChange && !bHeightChange)
		{
			return;//every offset already says what was asked for, and none of them is owed a resolve
		}

		const float ParentSpanX = this->Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X);
		const float ParentSpanY = this->Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y);

		CacheAnchorOffsetLeft = Value.Left;
		CacheAnchorOffsetRight = Value.Right;
		CacheAnchorOffsetBottom = Value.Bottom;
		CacheAnchorOffsetTop = Value.Top;
		bCacheAnchorOffsetLeftDirty = false;
		bCacheAnchorOffsetRightDirty = false;
		bCacheAnchorOffsetBottomDirty = false;
		bCacheAnchorOffsetTopDirty = false;

		CacheWidth = ParentSpanX - Value.Right - Value.Left;
		bCacheWidthDirty = false;
		AnchorData.SizeDelta.X = AnchorData.IsHorizontalStretched() ? CacheWidth - ParentSpanX : CacheWidth;
		AnchorData.AnchoredPosition.X = FMath::Lerp(Value.Left, -Value.Right, AnchorData.Pivot.X);

		CacheHeight = ParentSpanY - Value.Top - Value.Bottom;
		bCacheHeightDirty = false;
		AnchorData.SizeDelta.Y = AnchorData.IsVerticalStretched() ? CacheHeight - ParentSpanY : CacheHeight;
		AnchorData.AnchoredPosition.Y = FMath::Lerp(Value.Bottom, -Value.Top, AnchorData.Pivot.Y);

		SyncAnimatableGeometryMirrors();
		MarkAnchorDataChanged_Recursive(false, bWidthChange, bHeightChange, false);
		MarkLayoutForRebuild(this);
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

void UDreamWidget::SetHorizontalAndVerticalAnchorMinMax(FVector2D MinValue, FVector2D MaxValue, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (!AnchorData.AnchorMin.Equals(MinValue, 0.0f) || !AnchorData.AnchorMax.Equals(MaxValue, 0.0f))
		{
			auto PrevRelativeLocation = this->GetRelativeLocation();
			auto PrevWidth = this->GetWidth();
			auto PrevHeight = this->GetHeight();
			this->SetAnchorMin(MinValue);
			this->SetAnchorMax(MaxValue);
			if (bKeepSize)
			{
				this->SetWidth(PrevWidth);
				this->SetHeight(PrevHeight);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void UDreamWidget::SetHorizontalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (AnchorData.AnchorMin.X != Value.X || AnchorData.AnchorMax.X != Value.Y)
		{
			auto CurrentLeft = this->GetAnchorOffsetLeft();
			auto CurrentRight = this->GetAnchorOffsetRight();

			if (bKeepSize)
			{
				CacheWidth = this->GetWidth();
			}
			auto PrevRelativeLocation = this->GetRelativeLocation();

			AnchorData.AnchorMin.X = Value.X;
			AnchorData.AnchorMax.X = Value.Y;

			//SetAnchorLeft & SetAnchorRight
			{
				if (!bKeepSize)//recalculate size on new anchor if not keep size
				{
					CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - CurrentLeft;
				}
				//SetWidth
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -CurrentRight, this->AnchorData.Pivot.X);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}

			MarkAnchorDataChanged_Recursive(false, !bKeepSize, !bKeepSize, false);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}
void UDreamWidget::SetVerticalAnchorMinMax(FVector2D Value, bool bKeepSize, bool bKeepRelativeLocation)
{
	if (this->Parent.IsValid())
	{
		if (AnchorData.AnchorMin.Y != Value.X || AnchorData.AnchorMax.Y != Value.Y)
		{
			auto CurrentBottom = this->GetAnchorOffsetBottom();
			auto CurrentTop = this->GetAnchorOffsetTop();

			if (bKeepSize)
			{
				CacheHeight = this->GetHeight();
			}
			auto PrevRelativeLocation = this->GetRelativeLocation();

			AnchorData.AnchorMin.Y = Value.X;
			AnchorData.AnchorMax.Y = Value.Y;

			//SetAnchorBottom && SetAnchorTop
			{
				if (!bKeepSize)//recalculate size on new anchor if not keep size
				{
					CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - CurrentBottom;
				}
				//SetHeight
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -CurrentTop, this->AnchorData.Pivot.Y);
			}
			if (bKeepRelativeLocation)
			{
				this->SetRelativeLocation(PrevRelativeLocation);
			}

			MarkAnchorDataChanged_Recursive(false, !bKeepSize, !bKeepSize, false);
			MarkLayoutForRebuild(this);
		}
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent! %s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *this->GetPathName());
	}
}

void UDreamWidget::SetAnchoredPosition(FVector2D Value)
{
	if (!AnchorData.AnchoredPosition.Equals(Value, 0.0f))
	{
		AnchorData.AnchoredPosition = Value;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetHorizontalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.X != Value)
	{
		AnchorData.AnchoredPosition.X = Value;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}
void UDreamWidget::SetVerticalAnchoredPosition(float Value)
{
	if (AnchorData.AnchoredPosition.Y != Value)
	{
		AnchorData.AnchoredPosition.Y = Value;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		MarkAnchorDataChanged_Recursive(false, false, false, false);
		if (Parent.IsValid() && Parent->GetLayoutContainer())//only position change, if parent contains LayoutContainer then we should rebuild layout, otherwise not
		{
			MarkLayoutForRebuild(this, EDreamLayoutInvalidation::Arrange);
		}
	}
}

void UDreamWidget::SetSizeDelta(FVector2D Value)
{
	if (!AnchorData.SizeDelta.Equals(Value, 0.0f))
	{
		AnchorData.SizeDelta = Value;
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, true, true, false);
		MarkLayoutForRebuild(this);
	}
}

void UDreamWidget::SetAnchoredPositionAndSizeDelta(FVector2D Position, FVector2D Size)
{
	bool bPosChange = false, bSizeChange = false;
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		bPosChange = true;
		AnchorData.AnchoredPosition = Position;
	}
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		bSizeChange = true;
		AnchorData.SizeDelta = Size;
		// DIRTIED, never assigned. A size DELTA is only the resolved size on a point-anchored axis;
		// on a stretched one the size is the delta PLUS the parent's span across the anchors, and
		// writing the delta into the resolved cache published a number no arithmetic in this class
		// agrees with. That is where the list's "-0 wide viewport inside a 342-wide face" came from:
		// RefreshScrollFurniture states the gutter as a delta (0, or -Thickness) on a stretched
		// viewport, and this line then answered every GetWidth() with it. SetSizeDelta next door has
		// always dirtied instead; the two are the same operation and now say the same thing.
		bCacheWidthDirty = true;
		bCacheHeightDirty = true;
	}
	if (bPosChange || bSizeChange)
	{
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChanged_Recursive(false, bSizeChange, bSizeChange, false);
		MarkLayoutForRebuild(this);
	}
}

float UDreamWidget::GetAnchorOffsetLeft()const
{
	if (bCacheAnchorOffsetLeftDirty)
	{
		bCacheAnchorOffsetLeftDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetLeft = this->AnchorData.AnchoredPosition.X - this->AnchorData.SizeDelta.X * this->AnchorData.Pivot.X;
		}
		else
		{
			CacheAnchorOffsetLeft = this->GetLocalSpaceLeft();//local space left
		}
	}
	return CacheAnchorOffsetLeft;
}
float UDreamWidget::GetAnchorOffsetTop()const
{
	if (bCacheAnchorOffsetTopDirty)
	{
		bCacheAnchorOffsetTopDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetTop = -(this->AnchorData.AnchoredPosition.Y + this->AnchorData.SizeDelta.Y * (1.0f - this->AnchorData.Pivot.Y));
		}
		else
		{
			CacheAnchorOffsetTop = this->GetLocalSpaceTop();
		}
	}
	return CacheAnchorOffsetTop;
}
float UDreamWidget::GetAnchorOffsetRight()const
{
	if (bCacheAnchorOffsetRightDirty)
	{
		bCacheAnchorOffsetRightDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetRight = -(this->AnchorData.AnchoredPosition.X + this->AnchorData.SizeDelta.X * (1.0f - this->AnchorData.Pivot.X));
		}
		else
		{
			CacheAnchorOffsetRight = this->GetLocalSpaceRight();
		}
	}
	return CacheAnchorOffsetRight;
}
float UDreamWidget::GetAnchorOffsetBottom()const
{
	if (bCacheAnchorOffsetBottomDirty)
	{
		bCacheAnchorOffsetBottomDirty = false;
		SyncAnimatableGeometryMirrors();
		if (this->Parent.IsValid())
		{
			CacheAnchorOffsetBottom = this->AnchorData.AnchoredPosition.Y - this->AnchorData.SizeDelta.Y * this->AnchorData.Pivot.Y;
		}
		else
		{
			CacheAnchorOffsetBottom = this->GetLocalSpaceBottom();
		}
	}
	return CacheAnchorOffsetBottom;
}

FMargin UDreamWidget::GetAnchorOffset() const
{
	return FMargin(
		this->GetAnchorOffsetLeft(),
		this->GetAnchorOffsetTop(),
		this->GetAnchorOffsetRight(),
		this->GetAnchorOffsetBottom()
	);
}

void UDreamWidget::SetAnchorOffsetLeft(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetLeft != Value || bCacheAnchorOffsetLeftDirty)
		{
			bCacheAnchorOffsetLeftDirty = false;
			CacheAnchorOffsetLeft = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentRight = this->GetAnchorOffsetRight();
			CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - CurrentRight - Value;
			//SetWidth
			{
				if (AnchorData.IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					AnchorData.SizeDelta.X = CacheWidth;
				}
			}
			this->AnchorData.AnchoredPosition.X = FMath::Lerp(Value, -CurrentRight, this->AnchorData.Pivot.X);
			MarkAnchorDataChanged_Recursive(false, true, false, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetLeftDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetTop(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetTop != Value || bCacheAnchorOffsetTopDirty)
		{
			bCacheAnchorOffsetTopDirty = false;
			CacheAnchorOffsetTop = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentBottom = this->GetAnchorOffsetBottom();
			CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - Value - CurrentBottom;
			//SetHeight
			{
				if (AnchorData.IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					AnchorData.SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchorData.AnchoredPosition.Y = FMath::Lerp(CurrentBottom, -Value, this->AnchorData.Pivot.Y);
			MarkAnchorDataChanged_Recursive(false, false, true, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetTopDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetRight(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetRight != Value || bCacheAnchorOffsetRightDirty)
		{
			bCacheAnchorOffsetRightDirty = false;
			CacheAnchorOffsetRight = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentLeft = this->GetAnchorOffsetLeft();
			CacheWidth = this->Parent->GetWidth() * (this->AnchorData.AnchorMax.X - this->AnchorData.AnchorMin.X) - Value - CurrentLeft;
			//SetWidth
			{
				if (AnchorData.IsHorizontalStretched())
				{
					auto CalculatedSizeDeltaX = CacheWidth - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X));
					AnchorData.SizeDelta.X = CalculatedSizeDeltaX;
				}
				else
				{
					AnchorData.SizeDelta.X = CacheWidth;
				}
			}
			this->AnchorData.AnchoredPosition.X = FMath::Lerp(CurrentLeft, -Value, this->AnchorData.Pivot.X);
			MarkAnchorDataChanged_Recursive(false, true, false, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetRightDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}
void UDreamWidget::SetAnchorOffsetBottom(float Value)
{
	if (this->Parent.IsValid())
	{
		if (CacheAnchorOffsetBottom != Value || bCacheAnchorOffsetBottomDirty)
		{
			bCacheAnchorOffsetBottomDirty = false;
			CacheAnchorOffsetBottom = Value;
			SyncAnimatableGeometryMirrors();
			auto CurrentTop = this->GetAnchorOffsetTop();
			CacheHeight = this->Parent->GetHeight() * (this->AnchorData.AnchorMax.Y - this->AnchorData.AnchorMin.Y) - CurrentTop - Value;
			//SetHeight
			{
				if (AnchorData.IsVerticalStretched())
				{
					auto CalculatedSizeDeltaY = CacheHeight - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y));
					AnchorData.SizeDelta.Y = CalculatedSizeDeltaY;
				}
				else
				{
					AnchorData.SizeDelta.Y = CacheHeight;
				}
			}
			this->AnchorData.AnchoredPosition.Y = FMath::Lerp(Value, -CurrentTop, this->AnchorData.Pivot.Y);
			MarkAnchorDataChanged_Recursive(false, false, true, false);
			MarkLayoutForRebuild(this);
		}
		bCacheAnchorOffsetBottomDirty = false;
		SyncAnimatableGeometryMirrors();
	}
	else
	{
		UE_LOG(DreamGUI, Warning, TEXT("[%s].%d This function only valid if DreamWidget have parent!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)
	}
}

/**
 * The gate is the RESOLVED width, and so is the invalidation. Both used to be the size DELTA, and
 * that is one defect rather than two spellings of the same one.
 *
 * A stretched node's width is its delta PLUS the parent's span across its anchors, so the delta can
 * sit still while the width moves -- which is the ordinary case, not an exotic one: a node authored
 * as "exactly the parent's span" keeps a delta of 0 from the moment it is built to the moment the
 * window is resized. The old shape assigned the new width into this node's own cache and then
 * declined to tell anybody, because the delta had not changed; every stretched descendant went on
 * answering with the number it resolved when the parent was still zero-sized. Measured as a list
 * whose viewport read back -0 inside a 342-wide face, worked around at the control layer by
 * re-publishing anchors from a per-frame watch, and the watch then oscillated in the designer.
 *
 * GetWidth() first, rather than comparing against a possibly-stale cache: it resolves the stretch
 * and clears the dirty flag, so "did the width change" is asked of the width and not of a leftover.
 * When the answer is no this returns having done nothing, which is the same contract as before.
 */
void UDreamWidget::SetWidth(float Value)
{
	// The argument is a RESOLVED width, and a resolved width has no negative branch -- see the floor in
	// GetWidth(). Flooring the authored intent as well keeps the stored SizeDelta and the answer the
	// getter gives consistent instead of silently diverging by the negative part.
	Value = FMath::Max(0.0f, Value);
	if (GetWidth() == Value)
	{
		return;
	}
	CacheWidth = Value;
	bCacheWidthDirty = false;
	SyncAnimatableGeometryMirrors();
	AnchorData.SizeDelta.X = (Parent.IsValid() && AnchorData.IsHorizontalStretched())
		? Value - (Parent->GetWidth() * (AnchorData.AnchorMax.X - AnchorData.AnchorMin.X))
		: Value;
	MarkAnchorDataChanged_Recursive(false, true, false, false);
	MarkLayoutForRebuild(this);
}
/** The vertical twin of SetWidth, defect included and fixed the same way. */
void UDreamWidget::SetHeight(float Value)
{
	Value = FMath::Max(0.0f, Value);
	if (GetHeight() == Value)
	{
		return;
	}
	CacheHeight = Value;
	bCacheHeightDirty = false;
	SyncAnimatableGeometryMirrors();
	AnchorData.SizeDelta.Y = (Parent.IsValid() && AnchorData.IsVerticalStretched())
		? Value - (Parent->GetHeight() * (AnchorData.AnchorMax.Y - AnchorData.AnchorMin.Y))
		: Value;
	MarkAnchorDataChanged_Recursive(false, false, true, false);
	MarkLayoutForRebuild(this);
}

#pragma endregion

void UDreamWidget::UpdateLayout()
{
	// Everything written from here down is layout output, not something anybody asked for; see
	// IsLayoutWriting.
	++LayoutPassDepth;
	ON_SCOPE_EXIT{ --LayoutPassDepth; };
	// Both halves are gated on the dirty flag now. The container half always was (BeginLayoutPass inside
	// CalculateLayout); the LayoutSelf half read nothing and re-solved on every pass for every widget
	// carrying one. See UDreamLayoutSelf::BeginLayoutPass.
	if (IsValid(LayoutSelf) && LayoutSelf->BeginLayoutPass())
	{
		LayoutSelf->CalculateSize();
	}
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->CalculateLayout();
	}
}

void UDreamWidget::ForceUpdateLayout()
{
	MarkWidgetLayoutDirty();
	UpdateLayout();
}

FVector2D UDreamWidget::GetLocalSpaceLeftBottomPoint()const
{
	FVector2D leftBottomPoint;
	leftBottomPoint.X = GetWidth() * -AnchorData.Pivot.X;
	leftBottomPoint.Y = GetHeight() * -AnchorData.Pivot.Y;
	return leftBottomPoint;
}
FVector2D UDreamWidget::GetLocalSpaceRightTopPoint()const
{
	FVector2D rightTopPoint;
	rightTopPoint.X = GetWidth() * (1.0f - AnchorData.Pivot.X);
	rightTopPoint.Y = GetHeight() * (1.0f - AnchorData.Pivot.Y);
	return rightTopPoint;
}
FVector2D UDreamWidget::GetLocalSpaceCenter()const
{
	return FVector2D(this->GetWidth() * (0.5f - AnchorData.Pivot.X), this->GetHeight() * (0.5f - AnchorData.Pivot.Y));
}

float UDreamWidget::GetLocalSpaceLeft()const
{
	return this->GetWidth() * -AnchorData.Pivot.X;
}
float UDreamWidget::GetLocalSpaceRight()const
{
	return this->GetWidth() * (1.0f - AnchorData.Pivot.X);
}
float UDreamWidget::GetLocalSpaceBottom()const
{
	return this->GetHeight() * -AnchorData.Pivot.Y;
}
float UDreamWidget::GetLocalSpaceTop()const
{
	return this->GetHeight() * (1.0f - AnchorData.Pivot.Y);
}

void UDreamWidget::MarkDimensionChanged(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged)
{
	// Unconditional, and pointedly not gated on the three flags: they say what the CALLER believes
	// changed, and several callers pass a size change through as false because the value they diffed
	// was an anchor offset. A cache that trusted them would go stale on exactly those paths.
	MarkWorldRectBoundsDirty();
	// A render transform turns about a pivot POINT, and that point is resolved from the current size:
	// GetLocalSpaceLeft() + GetWidth() * RenderTransformPivot.X. So a resize moves the point, which moves
	// the bracketed transform, which moves ObjectToWorldTransform -- and nothing else here recomputes it.
	// CalculateTransformFromAnchor only writes RelativeLocation, and for a point-anchored widget a pure
	// resize leaves that untouched, so SetRelativeLocation early-outs and the transform cascade never
	// runs: the widget goes on being drawn (and hit-tested, GetWorldRectBoundingSphere reads the same
	// matrix) about the pivot point of its OLD size, off by (I - ScaleAndRotate) * (P_new - P_old).
	// A card that hover-scales while its text changes width, a spinner resized by a SizeBox mid-rotation.
	// Centre pivots are immune (the point is the origin) and so are pure translations, which is why this
	// survived; the bit test keeps every widget without a render transform on the old path.
	if (bHasRenderTransform && (InPivotChanged || InWidthChanged || InHeightChanged))
	{
		// Propagating is required rather than tidy: descendants compose their world transform from this
		// one, and a descendant whose own relative location did not change would otherwise keep a world
		// transform built from the pre-resize parent.
		CalculateObjectToWorldTransform(true);
	}
	// No clip invalidation here: clip rectangles are recomputed and diffed every tick from the owner's world
	// transform (see FDreamUIClipData::UpdateData), so there is nothing to mark.
	OnDimensionChangedEvent.Broadcast(InPivotChanged, InWidthChanged, InHeightChanged);
	if (IsValid(LayoutContainer))
	{
		LayoutContainer->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}
	if (GetLayoutSelf())
	{
		LayoutSelf->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}
	if (IsValid(Visual))
	{
		Visual->OnDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);
	}

	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(InPivotChanged || InWidthChanged || InHeightChanged);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			this->RenderCanvas->MarkTransformOrDimensionChanged();
		}
	}

	Call_DimensionsChanged(InPivotChanged, InWidthChanged, InHeightChanged);
}

void UDreamWidget::MarkTransformChanged()
{
	// UpdateObjectToWorldTransform is the ONLY writer of ObjectToWorldTransform and it ends here, so
	// this one line covers every move -- including the cascade CalculateObjectToWorldTransform sends
	// down, which reaches each descendant through its own UpdateObjectToWorldTransform. That is what
	// makes the cached sphere never staler than the transform it is derived from.
	MarkWorldRectBoundsDirty();
	if (this->RenderCanvas.IsValid())
	{
		this->RenderCanvas->MarkCanvasUpdate(true);//mark canvas to update
		if (this->IsCanvasWidget())
		{
			//This is mainly to mark DreamGUICanvas's bIsViewProjectionMatrixDirty to true.
			//For the condition DreamGUI_Tutorials/Tutorials/UIRenderTarget, when move DreamGUIRenderTarget at runtime, the DreamGUICanvas's RenderTarget's matrix not update, result in wrong interaction.
			this->RenderCanvas->MarkTransformOrDimensionChanged();
		}
	}

	Call_TransformChanged();
}

void UDreamWidget::MarkAnchorDataChanged_Recursive(bool InPivotChanged, bool InWidthChanged, bool InHeightChanged, bool InDiscardCache, bool InPropagateToChildren)
{
	CalculateTransformFromAnchor();

	if (InDiscardCache)
	{
		if (InWidthChanged)
		{
			bCacheWidthDirty = true;
		}
		if (InHeightChanged)
		{
			bCacheHeightDirty = true;
		}
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
	}
	MarkDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);

	// A size change that did not come out of a layout pass is a new authored intent, so the panel slot's
	// measurement snapshot has to follow it. Without this the snapshot froze at whatever the widget
	// measured when its slot was first registered - nothing outside the editor and prefab paths ever
	// re-captured it - so the next pass measured the child from that stale value and wrote the old size
	// straight back. A runtime SetWidth, or UDreamSpriteBase::SetSprite swapping in art of a different size,
	// visibly flashed and snapped back. This funnel only ever runs on the widget the setter was called on:
	// the recursion below hands children to the by-layout-container twin instead.
	if ((InWidthChanged || InHeightChanged) && !IsLayoutWriting() && IsValid(PanelSlot))
	{
		PanelSlot->SyncAuthoredDesiredSizeFromWidget();
	}

	if (!InPropagateToChildren)return;
	for (auto& Child : GetChildren())
	{
		if (!IsValid(Child))continue;
		bool ChildWidthChange = false, ChildHeightChange = false;
		if (InWidthChanged && Child->AnchorData.IsHorizontalStretched())
		{
			ChildWidthChange = true;
		}
		if (InHeightChanged && Child->AnchorData.IsVerticalStretched())
		{
			ChildHeightChange = true;
		}
		Child->MarkAnchorDataChanged_Recursive(false, ChildWidthChange, ChildHeightChange);

		//check if child need layout rebuild, the widget self is already marked outside of this function
		if (ChildWidthChange || ChildHeightChange//parent size change may cause child layout change
			|| ((InWidthChanged || InHeightChanged) && Child->GetLayoutSelf())//parent size changed and parent can affect child layout, need calculate child layout
			)
		{
			MarkLayoutForRebuild(Child);
		}
	}
}

void UDreamWidget::SetPositionAndSizeForLayoutAnimation(FVector2D Position, FVector2D Size)
{
	bool AnyChanged = false;
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		AnyChanged = true;
		AnchorData.AnchoredPosition = Position;
	}
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		AnyChanged = true;
		// No CacheWidth/CacheHeight assignment: the argument is a size DELTA, the caches hold RESOLVED
		// sizes, and the two are the same number only on a point-anchored axis. The assignment that used
		// to stand here was dead rather than wrong -- the InDiscardCache=true below dirties both flags a
		// few lines later -- but it is the exact shape of the defect fixed in SetWidth and
		// SetAnchoredPositionAndSizeDelta, and it was there to be copied.
		AnchorData.SizeDelta = Size;
	}
	if (AnyChanged)
	{
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::SetPositionForLayoutAnimation(FVector2D Position)
{
	if (!AnchorData.AnchoredPosition.Equals(Position, 0.0f))
	{
		AnchorData.AnchoredPosition = Position;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::SetSizeForLayoutAnimation(FVector2D Size)
{
	if (!AnchorData.SizeDelta.Equals(Size, 0.0f))
	{
		/** Same as in SetPositionAndSizeForLayoutAnimation: a delta is not a resolved size. */
		AnchorData.SizeDelta = Size;

		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		MarkAnchorDataChangedByLayoutContainer_Recursive(false, true, true, true);
	}
}

void UDreamWidget::MarkAnchorDataChangedByLayoutContainer_Recursive(bool InPivotChanged, bool InWidthChanged,
                                                                  bool InHeightChanged, bool InDiscardCache, bool InPropagateToChildren)
{
	CalculateTransformFromAnchor();

	if (InDiscardCache)
	{
		if (InWidthChanged)
		{
			bCacheWidthDirty = true;
		}
		if (InHeightChanged)
		{
			bCacheHeightDirty = true;
		}
		bCacheAnchorOffsetLeftDirty = true;
		bCacheAnchorOffsetRightDirty = true;
		bCacheAnchorOffsetBottomDirty = true;
		bCacheAnchorOffsetTopDirty = true;
	}
	MarkDimensionChanged(InPivotChanged, InWidthChanged, InHeightChanged);

	if (!InPropagateToChildren)return;
	for (auto& Child : GetChildren())
	{
		if (!IsValid(Child))continue;
		bool ChildWidthChange = false, ChildHeightChange = false;
		if (InWidthChanged && Child->AnchorData.IsHorizontalStretched())
		{
			ChildWidthChange = true;
		}
		if (InHeightChanged && Child->AnchorData.IsVerticalStretched())
		{
			ChildHeightChange = true;
		}
		Child->MarkAnchorDataChangedByLayoutContainer_Recursive(false, ChildWidthChange, ChildHeightChange);
	}
}

float UDreamWidget::GetLayoutProperty(TFunctionRef<float(UDreamLayoutSelf*)> GetLayoutSelfProperty,
                                    TFunctionRef<float(UDreamLayoutContainer*)> GetLayoutContainerProperty,
                                    TFunctionRef<float(UDreamVisual*)> GetVisualProperty,
                                    float DefaultValue)const
{
	if (IsValid(LayoutSelf))
	{
		auto Value = GetLayoutSelfProperty(LayoutSelf);
		if (Value >= 0)//enable override
		{
			return Value;
		}
	}
	if (IsValid(LayoutContainer))
	{
		auto Value = GetLayoutContainerProperty(LayoutContainer);
		if (Value >= 0)//enable override
		{
			return Value;
		}
	}
	if (IsValid(Visual))
	{
		auto Value = GetVisualProperty(Visual);
		if (Value >= 0)
		{
			return Value;
		}
	}
	return DefaultValue;
}
UObject* UDreamWidget::GetLayoutSource(TFunctionRef<float(UDreamLayoutSelf*)> GetLayoutSelfProperty,
	TFunctionRef<float(UDreamLayoutContainer*)> GetLayoutContainerProperty,
	TFunctionRef<float(UDreamVisual*)> GetVisualProperty) const
{
	if (IsValid(LayoutSelf))
	{
		auto Value = GetLayoutSelfProperty(LayoutSelf);
		if (Value >= 0)//enable override
		{
			return LayoutSelf;
		}
	}
	if (IsValid(LayoutContainer))
	{
		auto Value = GetLayoutContainerProperty(LayoutContainer);
		if (Value >= 0)//enable override
		{
			return LayoutContainer;
		}
	}
	if (IsValid(Visual))
	{
		auto Value = GetVisualProperty(Visual);
		if (Value >= 0)
		{
			return Visual;
		}
	}
	return nullptr;
}

TArray<UDreamWidget*> UDreamWidget::LayoutWriterStack;
int32 UDreamWidget::LayoutPassDepth = 0;

UDreamWidget::FLayoutWriteScope::FLayoutWriteScope(UDreamWidget* InLayoutWidget)
{
	if (IsValid(InLayoutWidget))
	{
		LayoutWriterStack.Push(InLayoutWidget);
		bPushed = true;
	}
}

UDreamWidget::FLayoutWriteScope::~FLayoutWriteScope()
{
	if (bPushed)
	{
		LayoutWriterStack.Pop(EAllowShrinking::No);
	}
}

void UDreamWidget::MarkLayoutForRebuild(UDreamWidget* InWidget)
{
	MarkLayoutForRebuild(InWidget, EDreamLayoutInvalidation::Measure);
}

void UDreamWidget::MarkLayoutForRebuild(UDreamWidget* InWidget, EDreamLayoutInvalidation Reason)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	static IConsoleVariable* LayoutTraceCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("dreamgui.LayoutTrace"));
	if (LayoutTraceCVar && LayoutTraceCVar->GetInt() != 0)
	{
		UE_LOG(DreamGUI, Log, TEXT("[LayoutTrace] invalidate %s reason=%s registered=%d"),
			*GetNameSafe(InWidget),
			Reason == EDreamLayoutInvalidation::Arrange ? TEXT("Arrange") : TEXT("Measure"),
			InWidget->HasRegistered() ? 1 : 0);
	}

	if (Reason == EDreamLayoutInvalidation::Arrange)
	{
		// Nothing above the parent can come out differently: a panel measures its children by desired
		// size and never by where they sit, so a widget that only moved leaves every preferred size on
		// the chain exactly as it was. The parent still re-arranges, which is what keeps a panel-managed
		// child from staying where it was put - the visible behaviour is unchanged, only the reach is.
		UDreamWidget* ParentWidget = InWidget->GetParent();
		if (!IsValid(ParentWidget))
		{
			return;
		}
		UDreamLayoutContainer* ParentLayout = ParentWidget->GetLayoutContainer();
		if (!IsValid(ParentLayout))
		{
			return;
		}
		// A writer applying its own arrangement must not be re-dirtied by it, same as the walk below.
		if (LayoutWriterStack.Contains(ParentWidget))
		{
			return;
		}
		ParentLayout->MarkLayoutDirty();
		if (UDreamLayoutSelf* LayoutSelf = InWidget->GetLayoutSelf(); IsValid(LayoutSelf))
		{
			LayoutSelf->MarkLayoutDirty();
		}
		ParentWidget->MarkWidgetLayoutDirty();
		return;
	}

	UDreamWidget* TargetWidget = InWidget;
	UDreamWidget* RebuildRoot = nullptr;
	bool bStoppedAtLayoutWriter = false;
	FDreamVisitedWidgetSet VisitedWidgets;
	// Desired-size dependencies may cross plain wrapper widgets, so dirty every layout on the ancestor chain.
	while (IsValid(TargetWidget) && !VisitedWidgets.Contains(TargetWidget))
	{
		// A layout that is applying its own results must not be re-dirtied by them. Stop the walk at the
		// writer: everything below it still gets dirtied, because a nested container does have to react to
		// the size it was just handed, but the writer and its ancestors keep the dirty state they already
		// consumed. Widgets outside the writer's subtree never reach this branch and behave as before.
		if (LayoutWriterStack.Contains(TargetWidget))
		{
			bStoppedAtLayoutWriter = true;
			break;
		}
		VisitedWidgets.Add(TargetWidget);
		if (UDreamLayoutContainer* LayoutContainer = TargetWidget->GetLayoutContainer(); IsValid(LayoutContainer))
		{
			LayoutContainer->MarkLayoutDirty();
			RebuildRoot = TargetWidget;
		}
		if (UDreamLayoutSelf* LayoutSelf = TargetWidget->GetLayoutSelf(); IsValid(LayoutSelf))
		{
			LayoutSelf->MarkLayoutDirty();
			RebuildRoot = TargetWidget;
		}
		if (TargetWidget->GetIgnoreLayout())
		{
			break;
		}

		// No relayout boundary here, and it is not an oversight. Blink stops this walk at a box whose
		// size cannot be affected from inside it, and the obvious translation - stop where the parent
		// panel sizes the child by Fill - was written, measured and reverted.
		//
		// It does not hold. A panel's MeasureLayout sums its children's desired sizes even for children
		// it sizes by Fill, so the panel's preferred size still moves when their content does. The
		// tempting rescue is that GetLayoutPreferredSize recomputes on every call and caches nothing, so
		// there is no stale value anywhere - true, and irrelevant: the ancestor still has to RUN to
		// consume the new one, and stopping the walk is precisely what stops it running. Three text
		// invalidation tests went to zero reflows and the convergence test went from one pass to two.
		//
		// A sound version has to establish that no ancestor's size derives from this subtree at all,
		// which is a walk to the root - the walk being avoided. Cacheable per widget, invalidated on
		// slot and hierarchy changes; that is a design, not a condition.
		TargetWidget = TargetWidget->GetParent();
	}
	if (bStoppedAtLayoutWriter)
	{
		// The pass that is running collected the writer's whole subtree and visits it in pre-order, so any
		// descendant dirtied above is still ahead of the cursor. Enqueuing it would only buy a second pass.
		return;
	}
	if (!IsValid(RebuildRoot))
	{
		RebuildRoot = InWidget;
	}
	if (IsValid(RebuildRoot))
	{
		RebuildRoot->MarkWidgetLayoutDirty();
	}
}

void UDreamWidget::RebuildLayoutImmediately(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return;
	}
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(InWidget->GetWorld()))
	{
		DreamUIManager->RebuildLayoutImmediately(InWidget);
	}
}

void UDreamWidget::MarkWidgetLayoutDirty()
{
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->AddLayoutDirtyWidget(this);
	}
}

void UDreamWidget::SetLayoutVisibilitySuppressed(bool bSuppressed)
{
	if (bLayoutVisibilitySuppressed != bSuppressed)
	{
		bLayoutVisibilitySuppressed = bSuppressed;
		// SizeBox, ScaleBox and WidgetSwitcher all flip this from inside their own arrange. It changes
		// which widgets a measurement is allowed to include, so every memoised desired size in the pass
		// may now be wrong - not just this widget's. Outside a pass the memo is empty and this is free.
		UDreamPanelLayoutBase::ForgetAllDesiredSizes();
		CalculateVisibility_Recursive();
	}
}

void UDreamWidget::ForceLayoutPrepass()
{
	if (UDreamUIManagerWorldSubsystem* DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
	{
		DreamUIManager->RebuildLayoutImmediately(this);
	}
}

void UDreamWidget::InvalidateLayoutAndVolatility()
{
	MarkLayoutForRebuild(this);
}

FVector2D UDreamWidget::GetDesiredSize() const
{
	if (UDreamWidget* ParentWidget = Parent.Get(); IsValid(ParentWidget))
	{
		if (const UDreamPanelLayoutBase* ParentPanel = Cast<UDreamPanelLayoutBase>(ParentWidget->GetLayoutContainer()))
		{
			return ParentPanel->GetDesiredSize(const_cast<UDreamWidget*>(this));
		}
	}
	// Nothing is measuring this widget, so the size it has IS the size it wants.
	return GetSize();
}

void UDreamWidget::SetFlowDirectionPreference(EDreamFlowDirectionPreference Value)
{
	if (FlowDirectionPreference == Value)
	{
		return;
	}
	FlowDirectionPreference = Value;
	MarkFlowDirectionChangedRecursive();
}

void UDreamWidget::MarkFlowDirectionChangedRecursive()
{
	// Arrange and not Measure: mirroring changes where a container PUTS its children, never how big
	// any of them wants to be, so no preferred size on the ancestor chain can come out differently.
	// The container to dirty is this widget's OWN -- it is the one that arranges along the flow --
	// which is why this does not go through MarkLayoutForRebuild, whose Arrange branch dirties the
	// parent's container instead.
	if (UDreamLayoutContainer* Container = GetLayoutContainer(); IsValid(Container))
	{
		Container->MarkLayoutDirty();
		MarkWidgetLayoutDirty();
	}
	for (UDreamWidget* Child : GetChildren())
	{
		// A child that states its own preference already resolved to that answer and still does, and
		// so does everything below it. Stopping there keeps a per-screen mirror from re-arranging the
		// one panel that was deliberately pinned left-to-right.
		if (IsValid(Child) && Child->FlowDirectionPreference == EDreamFlowDirectionPreference::Inherit)
		{
			Child->MarkFlowDirectionChangedRecursive();
		}
	}
}

EDreamFlowDirection UDreamWidget::GetResolvedFlowDirection() const
{
	for (const UDreamWidget* Widget = this; Widget != nullptr; Widget = Widget->Parent.Get())
	{
		switch (Widget->FlowDirectionPreference)
		{
		case EDreamFlowDirectionPreference::LeftToRight:
			return EDreamFlowDirection::LeftToRight;
		case EDreamFlowDirectionPreference::RightToLeft:
			return EDreamFlowDirection::RightToLeft;
		case EDreamFlowDirectionPreference::Culture:
			return FLayoutLocalization::GetLocalizedLayoutDirection() == EFlowDirection::RightToLeft
				? EDreamFlowDirection::RightToLeft
				: EDreamFlowDirection::LeftToRight;
		case EDreamFlowDirectionPreference::Inherit:
			break;
		}
	}
	return EDreamFlowDirection::LeftToRight;
}

void UDreamWidget::RefreshCultureFlowDirection()
{
	if (FlowDirectionPreference == EDreamFlowDirectionPreference::Culture)
	{
		// This widget and everything below it that inherits just changed answer; nothing below a
		// descendant that states its own preference did, and the recursion already stops there.
		MarkFlowDirectionChangedRecursive();
		return;
	}
	for (UDreamWidget* Child : GetChildren())
	{
		if (IsValid(Child))
		{
			Child->RefreshCultureFlowDirection();
		}
	}
}

void UDreamWidget::SetIgnoreLayout(bool Value)
{
	if (bIgnoreLayout != Value)
	{
		bIgnoreLayout = Value;
		// Mark from the parent, not from here. MarkLayoutForRebuild breaks its ancestor walk on the first
		// widget with IgnoreLayout set - and we just set it - so starting at `this` stopped immediately and
		// the container that has to close the gap never heard about it. Turning the flag off happened to
		// work, because by then the flag reads false, which made this look like a one-way switch.
		MarkLayoutForRebuild(Parent.IsValid() ? Parent.Get() : this);
		if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(GetWorld()))
		{
			DreamUIManager->MarkRebuildAllLayoutTree();
		}
	}
}
