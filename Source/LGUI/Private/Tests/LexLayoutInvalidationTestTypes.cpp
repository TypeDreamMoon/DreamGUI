// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/LexLayoutInvalidationTestTypes.h"

#include "Core/Components/LexWidget.h"

void ULexApplyCountingAspectRatio::CalculateSize()
{
	++ApplyCount;
	Super::CalculateSize();
}

void ULexArrangeObservingOverlay::ArrangeChildren()
{
	Super::ArrangeChildren();
	SizesDuringArrange.Reset();
	if (const ULexWidget* Panel = GetWidget())
	{
		for (const ULexWidget* Child : Panel->GetChildren())
		{
			if (IsValid(Child))
			{
				SizesDuringArrange.Add(Child->GetSize());
			}
		}
	}
}

void ULexLayoutVisibilityFlipOverlay::ArrangeChildren()
{
	Super::ArrangeChildren();
	if (IsValid(WidgetToReveal))
	{
		ULexWidget* Target = WidgetToReveal;
		WidgetToReveal = nullptr;
		++FlipCount;
		Target->SetVisibility(ELexWidgetVisibility::Visible);
	}
}

void ULexLayoutPassCountingOverlay::ArrangeChildren()
{
	// ArrangeChildren only runs past the base class's dirty gate, so every call here is a real recompute -
	// no need to read bIsLayoutDirty before Super consumes it the way this used to.
	++PassCount;
	Super::ArrangeChildren();
}

void ULexLayoutReentrantRebuildOverlay::ArrangeChildren()
{
	Super::ArrangeChildren();
	if (RootsToRebuild.Num() > 0)
	{
		const TArray<TObjectPtr<ULexWidget>> Roots = MoveTemp(RootsToRebuild);
		RootsToRebuild.Reset();
		++ReentryCount;
		for (const TObjectPtr<ULexWidget>& Root : Roots)
		{
			if (IsValid(Root))
			{
				ULexWidget::RebuildLayoutImmediately(Root);
			}
		}
	}
}
