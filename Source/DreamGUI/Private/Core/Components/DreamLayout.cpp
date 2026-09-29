// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/DreamLayout.h"

#include "DreamTweenBPLibrary.h"
#include "Core/Components/DreamWidget.h"

#if WITH_EDITOR
void UDreamLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FString DreamLayoutDebugClassLabel(const UClass* InClass)
{
	if (InClass == nullptr)
	{
		return FString();
	}
#if WITH_EDITOR
	return InClass->GetDisplayNameText().ToString();
#else
	return InClass->GetName();
#endif
}


UDreamLayoutAnimation::UDreamLayoutAnimation()
{
	bCanExecuteBlueprintEvent = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native);
}

void UDreamLayoutAnimation::OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray,
	TArray<TWeakObjectPtr<UDreamTweener>>& ResultTweenerArray)
{
	if (bCanExecuteBlueprintEvent)
	{
		TArray<UDreamTweener*> TempResultTweenerArray;
		ReceiveOnApplyLayoutResults(SnapshotDataArray, TempResultTweenerArray);
		for (auto& Tweener : TempResultTweenerArray)
		{
			ResultTweenerArray.Add(Tweener);
		}
	}
}

UDreamLayoutContainer* UDreamLayoutAnimation::GetLayoutContainer()const
{
	if (!IsValid(OwnerLayoutContainer))
	{
		OwnerLayoutContainer = this->GetTypedOuter<UDreamLayoutContainer>();
	}
	return OwnerLayoutContainer;
}

void UDreamLayoutAnimation_CommonTween::OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray, TArray<TWeakObjectPtr<UDreamTweener>>& ResultTweenerArray)
{
	for (auto& SnapshotData : SnapshotDataArray)
	{
		auto NewPos = SnapshotData.Widget->GetAnchoredPosition();
		auto NewSize = SnapshotData.Widget->GetSizeDelta();
		auto OldPos = SnapshotData.Position;
		// The snapshot records a RESOLVED size (SnapshotLayout stores Child->GetSize()), but both ends of
		// this tween are fed to SetPositionAndSizeForLayoutAnimation, which assigns straight into
		// AnchorData.SizeDelta. On a point-anchored child the two are the same number and nothing shows;
		// on a stretched one they differ by the parent's span across the anchors, so the animation's first
		// frame wrote the resolved size in as a delta and the child jumped to size + span before easing
		// back. GetSize() - GetSizeDelta() is exactly that span, measured on the widget itself, so this
		// converts the snapshot into the unit the setter actually takes. SlideIn next door never had the
		// bug: it derives its start from the current delta rather than from the snapshot.
		const FVector2D AnchorSpan = SnapshotData.Widget->GetSize() - SnapshotData.Widget->GetSizeDelta();
		auto OldSize = SnapshotData.Size - AnchorSpan;

		// UDreamTweenManager is a game-instance subsystem, so a world no game instance owns has none and
		// To() answers null there (an editor world never gets this far: ApplyLayoutResult plays no layout
		// animation outside a game world). The call used to be chained straight into ->SetEase, which
		// made that a null dereference rather than "no easing available".
		//
		// So the tween is asked for FIRST, and the child is put back at its start only once there is a
		// tween to carry it away again. With none, the layout's own result -- exactly where the animation
		// would have ended -- is left standing. Writing the start state first, as this used to, left
		// every child where the OLD layout had put it, for good. The tween reads nothing from the widget
		// and only ticks from the next frame, so writing the start after asking changes nothing when
		// there is one.
		UDreamTweener* Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateLambda([=]()
		{
			return 0;
		}), FDreamTweenFloatSetterFunction::CreateLambda([=](float Value)
		{
			auto Pos = FMath::Lerp(OldPos, NewPos, Value);
			auto Size = FMath::Lerp(OldSize, NewSize, Value);
			SnapshotData.Widget->SetPositionAndSizeForLayoutAnimation(Pos, Size);
		}), 1.0f, Duration);
		if (!IsValid(Tweener))
		{
			continue;
		}
		SnapshotData.Widget->SetPositionAndSizeForLayoutAnimation(OldPos, OldSize);
		Tweener->SetEase(Ease);
		if (Ease == EDreamTweenEase::CurveFloat)
		{
			Tweener->SetRuntimeFloatCurve(EaseCurve);
		}
		ResultTweenerArray.Add(Tweener);
	}
}

void UDreamLayoutAnimation_SlideIn::OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray,
	TArray<TWeakObjectPtr<UDreamTweener>>& ResultTweenerArray)
{
	auto LayoutWidget = GetLayoutContainer()->GetWidget();
	for (auto& SnapshotData : SnapshotDataArray)
	{
		if (SnapshotData.Widget == LayoutWidget)continue;
		auto NewPos = SnapshotData.Widget->GetAnchoredPosition();
		auto NewSize = SnapshotData.Widget->GetSizeDelta();
		auto NewOpacity = SnapshotData.Widget->GetRenderOpacity();
		auto OldPos = NewPos + PositionOffset;
		auto OldSize = NewSize + SizeOffset;
		auto OldOpacity = NewOpacity + OpacityOffset;

		// Same null contract and same order as UDreamLayoutAnimation_CommonTween above: the offset and
		// faded start is written only once a tween exists to slide the child in from it. With none, the
		// child keeps its laid-out place and opacity -- where the slide would have ended -- rather than
		// sitting at the offset, possibly fully transparent, for good.
		UDreamTweener* Tweener = UDreamTweenManager::To(this
		, FDreamTweenFloatGetterFunction::CreateLambda([=]()
		{
			return 0;
		}), FDreamTweenFloatSetterFunction::CreateLambda([=](float Value)
		{
			auto Pos = FMath::Lerp(OldPos, NewPos, Value);
			auto Size = FMath::Lerp(OldSize, NewSize, Value);
			SnapshotData.Widget->SetPositionAndSizeForLayoutAnimation(Pos, Size);
			SnapshotData.Widget->SetRenderOpacity(FMath::Lerp(OldOpacity, NewOpacity, Value));
		}), 1.0f, Duration);
		if (!IsValid(Tweener))
		{
			continue;
		}
		SnapshotData.Widget->SetPositionAndSizeForLayoutAnimation(OldPos, OldSize);
		SnapshotData.Widget->SetRenderOpacity(OldOpacity);
		Tweener->SetEase(Ease);
		if (Ease == EDreamTweenEase::CurveFloat)
		{
			Tweener->SetRuntimeFloatCurve(EaseCurve);
		}
		ResultTweenerArray.Add(Tweener);
	}
}

void UDreamLayoutContainer::PostReinitProperties()
{
	Super::PostReinitProperties();
#if WITH_EDITOR
	if (!this->GetName().StartsWith("Default__"))
	{
		if (auto Widget = GetWidget())
		{
			if (auto World = Widget->GetWorld())
			{
				if (!World->IsGameWorld())
				{
					UDreamWidget::MarkLayoutForRebuild(Widget);
				}
			}
		}
	}
#endif
}

void UDreamLayoutContainer::OnRegister()
{
	Super::OnRegister();
	if (auto Widget = GetWidget())
	{
		UDreamWidget::MarkLayoutForRebuild(Widget);
	}
}

void UDreamLayoutContainer::SnapshotLayout()
{
	// No RefreshChildren() here any more. It was non-virtual and so was the legacy FlexBox container's
	// same-named function, which meant this call always bound to the *base* version - for FlexBox, for
	// Grid and for every panel alike. It filled a Children array that only FlexBox reads and that FlexBox
	// repopulates itself in DoCalculate, so the result was never observed; what did survive was its side
	// effect, rewriting child anchors under a rule the container had not asked for, on every container in
	// the tree on every pass, dirty or not. Each container handles its own children and its own anchors:
	// FlexBox in its RefreshChildren, Grid in ApplyCell, panels in ApplyChildRect.
	if (!bUseAnimation || !AnimationHandler)return;//snapshot just for animation
	if (LayoutAnimTweenerArray.Num() > 0)
	{
		UDreamTweenBPLibrary::ArrayKillIfIsTweening(this, LayoutAnimTweenerArray);
		LayoutAnimTweenerArray.Reset();
	}
	auto Widget = GetWidget();
	if (!IsValid(Widget))
	{
		return;
	}
	LayoutAnimSnapshotDataArray.Reset();
	for (auto& Child : Widget->GetChildren())
	{
		if (!IsValid(Child))
		{
			continue;
		}
		FLayoutAnimationSnapshotData SnapshotData;
		SnapshotData.Position = Child->GetAnchoredPosition();
		SnapshotData.Size = Child->GetSize();
		SnapshotData.Widget = Child;
		LayoutAnimSnapshotDataArray.Add(SnapshotData);
	}
	if (auto Parent = Widget->GetParent())
	{
		if (!Parent->GetLayoutContainer() && Widget->GetLayoutSelf())// if parent is a layout-container and this is a layout-self, then we need to add self to snapshot data for later animation
		{
			FLayoutAnimationSnapshotData SnapshotData;
			SnapshotData.Position = Widget->GetAnchoredPosition();
			SnapshotData.Size = Widget->GetSize();
			SnapshotData.Widget = Widget;
			LayoutAnimSnapshotDataArray.Add(SnapshotData);
		}
	}
}
void UDreamLayoutContainer::ApplyLayoutResult()
{
	if (!bUseAnimation || !AnimationHandler)return;
#if WITH_EDITOR
	if (const UWorld* World = GetWorld(); !IsValid(World) || !World->IsGameWorld())//editor mode not use animation
	{
		LayoutAnimSnapshotDataArray.Reset();
		return;
	}
#endif
	AnimationHandler->OnApplyLayoutResults(LayoutAnimSnapshotDataArray, LayoutAnimTweenerArray);
	LayoutAnimSnapshotDataArray.Reset();
}

void UDreamLayoutContainer::SetLayoutAnimation(UDreamLayoutAnimation* Value)
{
	AnimationHandler = Value;
}

UDreamLayoutAnimation* UDreamLayoutContainer::CreateNewLayoutAnimation(TSubclassOf<UDreamLayoutAnimation> Class)
{
	auto NewAnimationHandler = NewObject<UDreamLayoutAnimation>(this, Class, NAME_None, RF_Public | GetMaskedFlags(RF_Transactional));
	AnimationHandler = NewAnimationHandler;
	return AnimationHandler;
}

void UDreamLayout::MarkLayoutDirty()
{
	bIsLayoutDirty = true;
}

UDreamLayoutContainer::UDreamLayoutContainer()
{
}

bool UDreamLayoutContainer::GetLayoutDebugInfo(const UDreamWidget* TargetWidget, FDreamLayoutDebugInfo& OutInfo) const
{
	const UDreamWidget* LayoutOwnerWidget = GetWidget();
	if (!IsValid(TargetWidget) || !IsValid(LayoutOwnerWidget)
		|| (TargetWidget != LayoutOwnerWidget && !LayoutOwnerWidget->GetChildren().Contains(const_cast<UDreamWidget*>(TargetWidget))))
	{
		return false;
	}

	OutInfo = FDreamLayoutDebugInfo();
	OutInfo.Widget = const_cast<UDreamWidget*>(TargetWidget);
	OutInfo.ArrangedPosition = TargetWidget->GetAnchoredPosition();
	OutInfo.ArrangedSize = TargetWidget->GetSize();
	OutInfo.AuthoredSize = OutInfo.ArrangedSize;
	OutInfo.ContentBounds = LayoutOwnerWidget->GetSize();
	OutInfo.Algorithm = FString::Printf(TEXT("Legacy DreamGUI / %s"), *DreamLayoutDebugClassLabel(GetClass()));
	OutInfo.SlotRule = IsValid(TargetWidget->GetLayoutSelf())
		? FString::Printf(TEXT("LayoutSelf: %s"), *DreamLayoutDebugClassLabel(TargetWidget->GetLayoutSelf()->GetClass()))
		: TEXT("Authored size");

	OutInfo.DesiredSize = OutInfo.ArrangedSize;
	if (UDreamLayoutSelf* LayoutSelf = TargetWidget->GetLayoutSelf(); IsValid(LayoutSelf))
	{
		const FVector2f Preferred = LayoutSelf->GetLayoutPreferredSize();
		const FDreamLayoutControlAnchorData SelfControl = LayoutSelf->GetLayoutControlAnchor(TargetWidget);
		if (SelfControl.bCanControlHorizontalSize && FMath::IsFinite(Preferred.X) && Preferred.X >= 0.0f)
		{
			OutInfo.DesiredSize.X = Preferred.X;
		}
		if (SelfControl.bCanControlVerticalSize && FMath::IsFinite(Preferred.Y) && Preferred.Y >= 0.0f)
		{
			OutInfo.DesiredSize.Y = Preferred.Y;
		}
	}

	const FDreamLayoutControlAnchorData Control = GetLayoutControlAnchor(TargetWidget);
	auto DescribeAxes = [](bool bX, bool bY, const TCHAR* Controlled, const TCHAR* Authored)
	{
		if (bX && bY) return FString::Printf(TEXT("%s X+Y"), Controlled);
		if (bX) return FString::Printf(TEXT("%s X / %s Y"), Controlled, Authored);
		if (bY) return FString::Printf(TEXT("%s Y / %s X"), Controlled, Authored);
		return FString::Printf(TEXT("%s X+Y"), Authored);
	};
	OutInfo.PositionOwner = DescribeAxes(Control.bCanControlHorizontalPosition, Control.bCanControlVerticalPosition,
		TEXT("Layout"), TEXT("Anchors"));
	OutInfo.SizeOwner = DescribeAxes(Control.bCanControlHorizontalSize, Control.bCanControlVerticalSize,
		TEXT("Layout"), TEXT("Authored"));
	if (const UEnum* ClippingEnum = StaticEnum<EDreamWidgetClipping>())
	{
		OutInfo.Clipping = ClippingEnum->GetDisplayNameTextByValue(static_cast<int64>(TargetWidget->GetClipping())).ToString();
	}
	return true;
}

#if WITH_EDITOR
void UDreamLayoutContainer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (!this->GetName().StartsWith("Default__"))
	{
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void UDreamLayoutSelf::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (!this->GetName().StartsWith("Default__"))
	{
		UDreamWidget::MarkLayoutForRebuild(GetWidget());
	}
}
#endif

void UDreamLayoutSelf::PostReinitProperties()
{
	Super::PostReinitProperties();
#if WITH_EDITOR
	if (!this->GetName().StartsWith("Default__"))
	{
		if (auto Widget = GetWidget())
		{
			if (auto World = Widget->GetWorld())
			{
				if (!World->IsGameWorld())
				{
					UDreamWidget::MarkLayoutForRebuild(Widget);
				}
			}
		}
	}
#endif
}

bool UDreamLayoutSelf::BeginLayoutPass()
{
	if (!bIsLayoutDirty && bHasAppliedOnce)
	{
		return false;
	}
	bIsLayoutDirty = false;
	bHasAppliedOnce = true;
	return true;
}
