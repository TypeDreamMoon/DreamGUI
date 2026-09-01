// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamButton.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UIButton.h"

const FName UDreamButton::ContentSlotName(TEXT("Content"));

void UDreamButton::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	// The names the built-in tree gives them, and the names a template has to use. Content is
	// optional because a template that offers no hole is a perfectly good button -- everything that
	// writes to it null-checks, and the slot machinery treats "no such slot" as an empty one.
	OutParts.Emplace(TEXT("Face"), FaceNode);
	OutParts.Emplace(TEXT("Content"), ContentNode, /*bRequired*/false);
}

void UDreamButton::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The face IS the root: a button is one rectangle, and giving it a separate background child
	// would only manufacture a gap for the hit test to fall through.
	//
	// A size box rather than an overlay, and that is the whole of "the button is as big as what is
	// on it": an overlay reports the largest child as its preferred size and pads nothing, so the
	// style's ContentPadding had to be hung on a child's slot and the style's Height had to be
	// written onto the widget -- where the next arrange pass overwrote it. A size box measures its
	// one child, adds its own padding, and clamps the result to MinDesiredSize, which is where
	// Height belongs. ApplyStyle pushes both numbers; see there.
	Realize(this,
		Node<UDreamRectBlock>("Face")
			.Stretch()
			.With<UDreamLayoutContainerSizeBox>()
			.Children(
				// The hole. Fill inside the size box's padded area, so what the host puts here gets
				// the whole of it to arrange itself in -- and an OVERLAY of its own, because a node
				// with no layout container arranges nothing: content dropped in a button used to
				// keep whatever rect it was authored with, ignoring the button's size and padding
				// alike. The node itself draws nothing.
				//
				// Authored at ZERO, which is what makes an EMPTY button its padding rather than
				// 124x108. A widget's authored rect is what the measure walk falls back on when
				// nothing under it claims anything, and a hole is exactly that widget -- so the
				// default 100x100 was being read as "this button wants a hundred square". Stated
				// once, here, rather than in ApplyStyle: re-capturing an authored size after a
				// layout pass enshrines layout OUTPUT as the authored number.
				Widget("Content")
					.Size(0.0f, 0.0f)
					.With<UDreamLayoutContainerOverlay>()
					.With<UDreamNamedSlot>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})));
	// No .Out() and no .Then(): the parts are bound by name afterwards, and the behaviour goes on in
	// WireParts. Both of those have to work for a tree this code did not write.
}

void UDreamButton::WireParts()
{
	// Ensure, not Get: on the built-in road the face is ours and this adds the behaviour; on the
	// template road the face is somebody's drawing and this is the only thing that makes it a
	// button. A control that always carries its own UIButton has no state in which clicking it does
	// nothing -- which is the omission BP_Button shipped with for months.
	ButtonBehaviour = EnsureComponent<UUIButton>(FaceNode);
	if (ButtonBehaviour != nullptr && FaceNode != nullptr)
	{
		// Its own visual: a button's pointer transition tints the face it is standing on.
		ButtonBehaviour->SetTransitionTarget(FaceNode->GetVisual());
		ButtonBehaviour->GetOnClickEvent().AddUObject(this, &UDreamButton::HandleClicked);
	}
}

void UDreamButton::ApplyStyle()
{
	const FDreamButtonStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ButtonStyle);
	ShapeFace(FaceNode, Active.CornerRadius);
	SkinFace(FaceNode, Active.FaceBrush);

	// "How big is this button" is these two numbers. Cast rather than assumed, because on the
	// template road the face carries whatever container its author drew and replacing it would be
	// this class overruling the one thing a template is for; a templated button sizes itself the way
	// its own tree says, which is the bargain everywhere else in this family too.
	if (UDreamLayoutContainerSizeBox* Box = FaceNode != nullptr
		? Cast<UDreamLayoutContainerSizeBox>(FaceNode->GetLayoutContainer())
		: nullptr)
	{
		Box->SetPadding(Active.ContentPadding);
		// Height is a FLOOR, not the height: at least this tall, and taller when what the host put
		// in the hole needs the room. Written onto the widget instead -- which is what
		// SizeControlHeight used to be the whole answer -- it lost to the first arrange pass, so a
		// button in an Auto row came out the height of its stock label rather than the style's.
		Box->SetMinDesiredSize(FVector2D(0.0, Active.Height));
	}
	// Nothing here reads the hole. An empty one already claims nothing -- it is authored at zero,
	// see RealizeBuiltIn -- so there is no "put the stock label away, wake the hole" rule left to
	// enforce, and in particular none that a runtime AddChild into the hole could arrive too late
	// for. That was the sharp edge in doing it here: a rule the style push owns is a rule that has
	// not run yet for anything attached after the last one.
	if (ButtonBehaviour != nullptr)
	{
		PushSelectableState(ButtonBehaviour, Active.Normal, Active.Hovered, Active.Pressed,
			Active.Disabled, Active.Focused, Active.TransitionDuration);
	}
	// The last-resort height, for a button no panel measures: hung on anchors under a container-less
	// parent there is nobody to ask the face what it wants, and the widget's own rect is all there
	// is. Under a panel this is overwritten by the arrange pass, and the floor above is what decides.
	SizeControlHeight(Active.Height);
}

void UDreamButton::HandleClicked()
{
	OnClicked.Broadcast();
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Button", UDreamButton)
