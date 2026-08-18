// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/Components/LexLayout.h"

#include "LTweenBPLibrary.h"
#include "Core/Components/LexWidget.h"

#if WITH_EDITOR
void ULexLayout::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

FString LexLayoutDebugClassLabel(const UClass* InClass)
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


ULexLayoutAnimation::ULexLayoutAnimation()
{
	bCanExecuteBlueprintEvent = GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint) || !GetClass()->HasAnyClassFlags(CLASS_Native);
}

void ULexLayoutAnimation::OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray,
	TArray<TWeakObjectPtr<ULTweener>>& ResultTweenerArray)
{
	if (bCanExecuteBlueprintEvent)
	{
		TArray<ULTweener*> TempResultTweenerArray;
		ReceiveOnApplyLayoutResults(SnapshotDataArray, TempResultTweenerArray);
		for (auto& Tweener : TempResultTweenerArray)
		{
			ResultTweenerArray.Add(Tweener);
		}
	}
}

ULexLayoutContainer* ULexLayoutAnimation::GetLayoutContainer()const
{
	if (!IsValid(OwnerLayoutContainer))
	{
		OwnerLayoutContainer = this->GetTypedOuter<ULexLayoutContainer>();
	}
	return OwnerLayoutContainer;
}

void ULexLayoutAnimation_CommonTween::OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray, TArray<TWeakObjectPtr<ULTweener>>& ResultTweenerArray)
{
	for (auto& SnapshotData : SnapshotDataArray)
	{
		auto NewPos = SnapshotData.Widget->GetAnchoredPosition();
		auto NewSize = SnapshotData.Widget->GetSizeDelta();
		auto OldPos = SnapshotData.Position;
		auto OldSize = SnapshotData.Size;
		SnapshotData.Widget->SetPositionAndSizeForLayoutAnimation(OldPos, OldSize);

		auto Tweener = ULTweenManager::To(this
		, FLTweenFloatGetterFunction::CreateLambda([=]()
		{
			return 0;
		}), FLTweenFloatSetterFunction::CreateLambda([=](float Value)
		{
			auto Pos = FMath::Lerp(OldPos, NewPos, Value);
			auto Size = FMath::Lerp(OldSize, NewSize, Value);
			SnapshotData.Widget->SetPositionAndSizeForLayoutAnimation(Pos, Size);
		}), 1.0f, Duration)
		->SetEase(Ease);
		if (Ease == ELTweenEase::CurveFloat)
		{
			Tweener->SetRuntimeFloatCurve(EaseCurve);
		}
		ResultTweenerArray.Add(Tweener);
	}
}

void ULexLayoutAnimation_SlideIn::OnApplyLayoutResults(const TArray<FLayoutAnimationSnapshotData>& SnapshotDataArray,
	TArray<TWeakObjectPtr<ULTweener>>& ResultTweenerArray)
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
		SnapshotData.Widget->SetPositionAndSizeForLayoutAnimation(OldPos, OldSize);
		SnapshotData.Widget->SetRenderOpacity(OldOpacity);

		auto Tweener = ULTweenManager::To(this
		, FLTweenFloatGetterFunction::CreateLambda([=]()
		{
			return 0;
		}), FLTweenFloatSetterFunction::CreateLambda([=](float Value)
		{
			auto Pos = FMath::Lerp(OldPos, NewPos, Value);
			auto Size = FMath::Lerp(OldSize, NewSize, Value);
			SnapshotData.Widget->SetPositionAndSizeForLayoutAnimation(Pos, Size);
			SnapshotData.Widget->SetRenderOpacity(FMath::Lerp(OldOpacity, NewOpacity, Value));
		}), 1.0f, Duration)
		->SetEase(Ease);
		if (Ease == ELTweenEase::CurveFloat)
		{
			Tweener->SetRuntimeFloatCurve(EaseCurve);
		}
		ResultTweenerArray.Add(Tweener);
	}
}

void ULexLayoutContainer::PostReinitProperties()
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
					ULexWidget::MarkLayoutForRebuild(Widget);
				}
			}
		}
	}
#endif
}

void ULexLayoutContainer::OnRegister()
{
	Super::OnRegister();
	if (auto Widget = GetWidget())
	{
		ULexWidget::MarkLayoutForRebuild(Widget);
	}
}

void ULexLayoutContainer::SnapshotLayout()
{
	// No RefreshChildren() here any more. It was non-virtual and so was ULexLayoutContainerFlexBox's
	// same-named function, which meant this call always bound to the *base* version - for FlexBox, for
	// Grid and for every panel alike. It filled a Children array that only FlexBox reads and that FlexBox
	// repopulates itself in DoCalculate, so the result was never observed; what did survive was its side
	// effect, rewriting child anchors under a rule the container had not asked for, on every container in
	// the tree on every pass, dirty or not. Each container handles its own children and its own anchors:
	// FlexBox in its RefreshChildren, Grid in ApplyCell, panels in ApplyChildRect.
	if (!bUseAnimation || !AnimationHandler)return;//snapshot just for animation
	if (LayoutAnimTweenerArray.Num() > 0)
	{
		ULTweenBPLibrary::ArrayKillIfIsTweening(this, LayoutAnimTweenerArray);
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
void ULexLayoutContainer::ApplyLayoutResult()
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

void ULexLayoutContainer::SetLayoutAnimation(ULexLayoutAnimation* Value)
{
	AnimationHandler = Value;
}

ULexLayoutAnimation* ULexLayoutContainer::CreateNewLayoutAnimation(TSubclassOf<ULexLayoutAnimation> Class)
{
	auto NewAnimationHandler = NewObject<ULexLayoutAnimation>(this, Class, NAME_None, RF_Public | RF_Transactional);
	AnimationHandler = NewAnimationHandler;
	return AnimationHandler;
}

void ULexLayout::MarkLayoutDirty()
{
	bIsLayoutDirty = true;
}

ULexLayoutContainer::ULexLayoutContainer()
{
}

bool ULexLayoutContainer::GetLayoutDebugInfo(const ULexWidget* TargetWidget, FLexLayoutDebugInfo& OutInfo) const
{
	const ULexWidget* LayoutOwnerWidget = GetWidget();
	if (!IsValid(TargetWidget) || !IsValid(LayoutOwnerWidget)
		|| (TargetWidget != LayoutOwnerWidget && !LayoutOwnerWidget->GetChildren().Contains(const_cast<ULexWidget*>(TargetWidget))))
	{
		return false;
	}

	OutInfo = FLexLayoutDebugInfo();
	OutInfo.Widget = const_cast<ULexWidget*>(TargetWidget);
	OutInfo.ArrangedPosition = TargetWidget->GetAnchoredPosition();
	OutInfo.ArrangedSize = TargetWidget->GetSize();
	OutInfo.AuthoredSize = OutInfo.ArrangedSize;
	OutInfo.ContentBounds = LayoutOwnerWidget->GetSize();
	OutInfo.Algorithm = FString::Printf(TEXT("Legacy LGUI / %s"), *LexLayoutDebugClassLabel(GetClass()));
	OutInfo.SlotRule = IsValid(TargetWidget->GetLayoutSelf())
		? FString::Printf(TEXT("LayoutSelf: %s"), *LexLayoutDebugClassLabel(TargetWidget->GetLayoutSelf()->GetClass()))
		: TEXT("Authored size");

	OutInfo.DesiredSize = OutInfo.ArrangedSize;
	if (ULexLayoutSelf* LayoutSelf = TargetWidget->GetLayoutSelf(); IsValid(LayoutSelf))
	{
		const FVector2f Preferred = LayoutSelf->GetLayoutPreferredSize();
		const FLexLayoutControlAnchorData SelfControl = LayoutSelf->GetLayoutControlAnchor(TargetWidget);
		if (SelfControl.bCanControlHorizontalSize && FMath::IsFinite(Preferred.X) && Preferred.X >= 0.0f)
		{
			OutInfo.DesiredSize.X = Preferred.X;
		}
		if (SelfControl.bCanControlVerticalSize && FMath::IsFinite(Preferred.Y) && Preferred.Y >= 0.0f)
		{
			OutInfo.DesiredSize.Y = Preferred.Y;
		}
	}

	const FLexLayoutControlAnchorData Control = GetLayoutControlAnchor(TargetWidget);
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
	if (const UEnum* ClippingEnum = StaticEnum<ELexWidgetClipping>())
	{
		OutInfo.Clipping = ClippingEnum->GetDisplayNameTextByValue(static_cast<int64>(TargetWidget->GetClipping())).ToString();
	}
	return true;
}

#if WITH_EDITOR
void ULexLayoutContainer::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (!this->GetName().StartsWith("Default__"))
	{
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}

void ULexLayoutSelf::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	if (!this->GetName().StartsWith("Default__"))
	{
		ULexWidget::MarkLayoutForRebuild(GetWidget());
	}
}
#endif

void ULexLayoutSelf::PostReinitProperties()
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
					ULexWidget::MarkLayoutForRebuild(Widget);
				}
			}
		}
	}
#endif
}

FVector2f ULexLayoutSelf::GetLayoutFinalSize()
{
	auto Widget = GetWidget();
	return FVector2f(Widget->GetWidth(), Widget->GetHeight());
}
