// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamGradientAsset.h"

void UDreamGradientAsset::SetGradient(const FDreamGradient& InGradient)
{
	Gradient = InGradient;
	// Every text painting with this asset takes the new gradient's row in place of the old one: rows only, no repaint.
	GradientChangedEvent.Broadcast();
}

#if WITH_EDITOR
void UDreamGradientAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	// Every step of a drag too: a text's answer is a row, cheap enough to follow the slider.
	GradientChangedEvent.Broadcast();
}

void UDreamGradientAsset::PostEditUndo()
{
	Super::PostEditUndo();
	// An undo writes the old gradient back without PostEditChangeProperty, and the texts would keep the undone one.
	GradientChangedEvent.Broadcast();
}
#endif
