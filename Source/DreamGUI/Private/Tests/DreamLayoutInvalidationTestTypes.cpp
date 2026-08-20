// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/DreamLayoutInvalidationTestTypes.h"

#include "Core/Components/DreamWidget.h"

void UDreamApplyCountingAspectRatio::CalculateSize()
{
	++ApplyCount;
	Super::CalculateSize();
}

void UDreamArrangeObservingOverlay::ArrangeChildren()
{
	Super::ArrangeChildren();
	SizesDuringArrange.Reset();
	if (const UDreamWidget* Panel = GetWidget())
	{
		for (const UDreamWidget* Child : Panel->GetChildren())
		{
			if (IsValid(Child))
			{
				SizesDuringArrange.Add(Child->GetSize());
			}
		}
	}
}

void UDreamLayoutVisibilityFlipOverlay::ArrangeChildren()
{
	Super::ArrangeChildren();
	if (IsValid(WidgetToReveal))
	{
		UDreamWidget* Target = WidgetToReveal;
		WidgetToReveal = nullptr;
		++FlipCount;
		Target->SetVisibility(EDreamWidgetVisibility::Visible);
	}
}

void UDreamLayoutPassCountingOverlay::ArrangeChildren()
{
	// ArrangeChildren only runs past the base class's dirty gate, so every call here is a real recompute -
	// no need to read bIsLayoutDirty before Super consumes it the way this used to.
	++PassCount;
	Super::ArrangeChildren();
}

void UDreamLayoutReentrantRebuildOverlay::ArrangeChildren()
{
	Super::ArrangeChildren();
	if (RootsToRebuild.Num() > 0)
	{
		const TArray<TObjectPtr<UDreamWidget>> Roots = MoveTemp(RootsToRebuild);
		RootsToRebuild.Reset();
		++ReentryCount;
		for (const TObjectPtr<UDreamWidget>& Root : Roots)
		{
			if (IsValid(Root))
			{
				UDreamWidget::RebuildLayoutImmediately(Root);
			}
		}
	}
}
