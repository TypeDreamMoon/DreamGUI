// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamEditableText.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/UITextInput.h"

void UDreamEditableText::ApplyStyle()
{
	// The boxed field's whole push first -- padding, font, caret, validator, every behaviour knob --
	// and then the box taken away. In that order, because the box is the only thing this class
	// disagrees with and re-deriving the other twenty would be a second copy of them.
	Super::ApplyStyle();

	SkinFace(BackgroundNode, FDreamUIFaceBrush());
	if (UDreamVisual* FaceVisual = BackgroundNode != nullptr ? BackgroundNode->GetVisual() : nullptr)
	{
		FaceVisual->SetColor(FColor(0, 0, 0, 0));
	}
	// All five states, not just the resting one: the face is the behaviour's transition target, so a
	// field that merely started transparent would fill in again the first time the pointer crossed
	// it -- an invisible box that appears on hover is worse than a visible one.
	const FColor Clear(0, 0, 0, 0);
	PushSelectableState(InputBehaviour, Clear, Clear, Clear, Clear, Clear, 0.0f);
}

UDreamMultiLineEditableText::UDreamMultiLineEditableText()
{
	// The one property that makes it multi-line, set in the constructor so it is the CLASS's default
	// rather than something ApplyStyle re-imposes: an author who wants a single-line instance of this
	// class can untick it, and a control whose style push overwrote the box every time would make
	// that untickable.
	bMultiLine = true;
}

// The tags these classes answer to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "EditableText", UDreamEditableText)
DECLARE_DREAM_GUI_WIDGET("Native", "MultiLineEditableText", UDreamMultiLineEditableText)
