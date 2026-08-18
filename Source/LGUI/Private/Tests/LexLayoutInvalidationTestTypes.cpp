// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/LexLayoutInvalidationTestTypes.h"

#include "Core/Components/LexWidget.h"

void ULexLayoutVisibilityFlipOverlay::CalculateLayout()
{
	Super::CalculateLayout();
	if (IsValid(WidgetToReveal))
	{
		ULexWidget* Target = WidgetToReveal;
		WidgetToReveal = nullptr;
		++FlipCount;
		Target->SetVisibility(ELexWidgetVisibility::Visible);
	}
}

void ULexLayoutPassCountingOverlay::CalculateLayout()
{
	// Read the dirty flag before Super consumes it, so the count is of real recomputes and not of the
	// cheap early-out that every clean container takes on every tree walk.
	const bool bWasDirty = bIsLayoutDirty;
	Super::CalculateLayout();
	if (bWasDirty)
	{
		++PassCount;
	}
}

void ULexLayoutReentrantRebuildOverlay::CalculateLayout()
{
	Super::CalculateLayout();
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
