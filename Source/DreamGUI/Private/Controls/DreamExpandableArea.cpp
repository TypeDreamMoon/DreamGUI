// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamExpandableArea.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "DreamGUI.h"
#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UIButton.h"

const FName UDreamExpandableArea::ContentSlotName(TEXT("Content"));
const FName UDreamExpandableArea::HeaderSlotName(TEXT("Header"));

void UDreamExpandableArea::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("ExpandableArea"), RootNode);
	OutParts.Emplace(TEXT("Header"), HeaderNode);
	OutParts.Emplace(TEXT("HeaderRow"), HeaderRowNode);
	OutParts.Emplace(TEXT("Content"), ContentNode);
	OutParts.Emplace(TEXT("Label"), LabelNode);
	// The indicators and the header hole are all optional: exactly one indicator is ever awake, and
	// a template that draws its own arrow needs neither of ours.
	OutParts.Emplace(TEXT("Arrow"), ArrowNode, /*bRequired*/false);
	OutParts.Emplace(TEXT("ArrowMark"), ArrowMarkNode, /*bRequired*/false);
	OutParts.Emplace(UDreamExpandableArea::HeaderSlotName, HeaderSlotNode, /*bRequired*/false);
}

void UDreamExpandableArea::RealizeBuiltIn()
{
	using namespace DreamUI;

	// A column of two, and the two size themselves differently on purpose.
	//
	// The header is a SIZE BOX with its height overridden, not a rect with an authored height, and
	// that is the dropdown row's lesson restated: no authored number wins against a content measure
	// (GetDesiredSize accumulates the container's preferred size and only falls back to the authored
	// snapshot when nothing claimed), so a header holding a row of text would measure as that text --
	// 19-ish points for the default font, whatever the style says. A size box's override is the one
	// claim that DOES win: it is an override rather than an accumulation, and it is the framework's
	// own way to say "exactly this tall". The price is that a size box takes one child, hence the row
	// node inside it.
	//
	// The content is a FILL slot: the column is authored at header + content (see
	// PushExpansionVisuals), so the fill hands the content exactly its own measure -- and if a
	// consumer stretches the control, the extra goes to the CONTENT rather than inflating the header.
	Realize(this,
		Widget("ExpandableArea")
			.Stretch()
			.With<UDreamLayoutContainerVerticalBox>()
			.Children(
				// The header IS a button, face and all: DreamButton's argument, that a control which
				// always carries its own UIButton has no state in which clicking does nothing.
				Node<UDreamRectBlock>("Header")
					.With<UDreamLayoutContainerSizeBox>([](UDreamLayoutContainerSizeBox& InBox)
					{
						// The override is structural; WHICH height it is, is the style's, pushed in
						// ApplyStyle like every other knob.
						InBox.SetOverrideHeight(true);
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
					})
					.Children(
						Widget("HeaderRow")
							.With<UDreamLayoutContainerHorizontalBox>()
							.Slot([](UDreamPanelSlot& InSlot)
							{
								InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
								InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
							})
							.Children(
								DreamUI::Text("Arrow")
									.Visual([](UDreamText& InText)
									{
										InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
										InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
									})
									.Slot([](UDreamPanelSlot& InSlot)
									{
										InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
										InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
										InSlot.SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
									}),
								// The image indicator, the glyph's stand-in: exactly one of the two is
								// awake, decided per state by whether that state's brush holds an
								// image. The check box established the convention and this follows it
								// verbatim; asleep by default, because an imageless rect block draws a
								// plain white square.
								Node<UDreamRectBlock>("ArrowMark")
									.Self([](UDreamWidget& InMark)
									{
										InMark.SetWidgetActive(false);
									})
									.Slot([](UDreamPanelSlot& InSlot)
									{
										InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
										InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
										InSlot.SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
									}),
								DreamUI::Text("Label")
									.Visual([](UDreamText& InText)
									{
										InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
										InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
									})
									.Slot([](UDreamPanelSlot& InSlot)
									{
										InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
										InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
									}),
								// The header's hole, beside the arrow and in the label's place: a section title is
								// not always a sentence. Empty until a host fills it, at which point the stock
								// label stands down.
								Widget("Header")
									.With<UDreamNamedSlot>()
									.Slot([](UDreamPanelSlot& InSlot)
									{
										// Fill, like the label it replaces: the two never share the
										// row, so there is no pair of Fill siblings splitting it.
										InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
										InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
									}))),
				// The hole the consumer fills. A vertical box so several nested widgets stack rather
				// than pile up, and so the column can be MEASURED -- which is what the control's own
				// expanded height is read from. Declared as a named slot, and as this control's
				// default one, so nesting reaches it without the class hand-adopting stray children;
				// bAcceptsSeveral because the panel is already here.
				Node<UDreamRectBlock>("Content")
					.With<UDreamLayoutContainerVerticalBox>()
					.With<UDreamNamedSlot>([](UDreamNamedSlot& InSlot)
					{
						InSlot.bAcceptsSeveral = true;
					})
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
					})));
}

void UDreamExpandableArea::WireParts()
{
	HeaderBehaviour = EnsureComponent<UUIButton>(HeaderNode);
	if (HeaderBehaviour != nullptr && HeaderNode != nullptr)
	{
		// Its own visual: the pointer transition tints the face it is standing on.
		HeaderBehaviour->SetTransitionTarget(HeaderNode->GetVisual());
		HeaderBehaviour->GetOnClickEvent().AddUObject(this, &UDreamExpandableArea::HandleHeaderClicked);
	}
}

void UDreamExpandableArea::ApplyStyle()
{
	const FDreamExpandableAreaStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ExpandableAreaStyle);

	ShapeFace(HeaderNode, Active.CornerRadius);
	ShapeFace(ContentNode, Active.CornerRadius);
	SkinFace(HeaderNode, Active.HeaderBrush);
	SkinFace(ContentNode, Active.ContentBrush);

	if (UDreamVisual* ContentVisual = ContentNode != nullptr ? ContentNode->GetVisual() : nullptr)
	{
		// The content panel has no behaviour, so nobody else is going to write this colour; the
		// header's is the selectable's to give, which is why only one of the two is set here.
		ContentVisual->SetColor(Active.ContentBackground);
	}

	// A supplied header replaces the stock label -- they are the same place in the row.
	SwapBuiltInForSlot(LabelNode, HeaderSlotNode, HeaderSlotName);

	// The header's one authored number, and the only place it can go that a content measure cannot
	// out-vote. See the tree comment.
	if (UDreamLayoutContainerSizeBox* HeaderBox = HeaderNode != nullptr
		? Cast<UDreamLayoutContainerSizeBox>(HeaderNode->GetLayoutContainer()) : nullptr)
	{
		HeaderBox->SetHeightOverride(Active.HeaderHeight);
	}

	// Both paddings go on the CONTAINERS, not on slots: they are the space inside a face, around
	// whatever it holds, which is what a stack box's own Padding means. On the slots instead they
	// would be space around the header and the content within the outer column -- a different gap,
	// and one nothing in the style asks for.
	if (UDreamLayoutContainerStackBox* HeaderRow = HeaderRowNode != nullptr
		? Cast<UDreamLayoutContainerStackBox>(HeaderRowNode->GetLayoutContainer()) : nullptr)
	{
		HeaderRow->SetPadding(Active.HeaderPadding);
	}
	if (UDreamLayoutContainerStackBox* ContentBox = ContentNode != nullptr
		? Cast<UDreamLayoutContainerStackBox>(ContentNode->GetLayoutContainer()) : nullptr)
	{
		ContentBox->SetPadding(Active.ContentPadding);
	}

	if (UDreamText* LabelVisual = LabelNode != nullptr ? Cast<UDreamText>(LabelNode->GetVisual()) : nullptr)
	{
		LabelVisual->SetText(Label);
		LabelVisual->SetColor(Active.LabelColor);
		LabelVisual->SetFontSize(Active.FontSize);
	}
	if (UDreamText* ArrowVisual = ArrowNode != nullptr ? Cast<UDreamText>(ArrowNode->GetVisual()) : nullptr)
	{
		// A glyph, sized by the style's indicator height -- the toggle sizes its tick the same way.
		// The glyph itself belongs to the expanded state, so PushExpansionVisuals writes it.
		ArrowVisual->SetColor(Active.ArrowColor);
		ArrowVisual->SetFontSize(static_cast<float>(Active.ArrowSize.Y));
	}

	if (HeaderBehaviour != nullptr)
	{
		// A UUISelectable-hosted face renders WHITE without these: the transition is the only writer
		// of that visual's colour, and an unset transition colour is not "leave it alone".
		HeaderBehaviour->SetNormalColor(Active.HeaderNormal);
		HeaderBehaviour->SetHoveredColor(Active.HeaderHovered);
		HeaderBehaviour->SetPressedColor(Active.HeaderPressed);
	}

	// Last: it reads the header height just written and the content's measure, and both feed the
	// control's own size.
	PushExpansionVisuals();
}

bool UDreamExpandableArea::GetIsExpanded() const
{
	return bIsExpanded;
}

void UDreamExpandableArea::SetIsExpanded(bool bInIsExpanded)
{
	if (bIsExpanded == bInIsExpanded)
	{
		return;
	}
	bIsExpanded = bInIsExpanded;
	PushExpansionVisuals();
	OnExpansionChanged.Broadcast(bIsExpanded);
	OnValueChangedBP.Broadcast(bIsExpanded);
}

void UDreamExpandableArea::ToggleExpansion()
{
	SetIsExpanded(!bIsExpanded);
}

void UDreamExpandableArea::SetContent(UDreamWidget* InContent)
{
	if (ContentNode == nullptr)
	{
		return;
	}
	// One content, replaced: the column can hold several (a .dui author may nest several nodes), but
	// a caller saying "the content is this" means the previous one is not it any more. Detached
	// rather than destroyed -- whoever handed it over still owns it.
	TArray<UDreamWidget*> Existing(ContentNode->GetChildren());
	for (UDreamWidget* Child : Existing)
	{
		if (IsValid(Child) && Child != InContent)
		{
			Child->SetParent(nullptr, false);
		}
	}
	MoveIntoContent(InContent);
	// The column's measure just changed, and with it the control's expanded height.
	PushExpansionVisuals();
}

UDreamWidget* UDreamExpandableArea::GetContent() const
{
	if (ContentNode == nullptr)
	{
		return nullptr;
	}
	// Not named Children: UDreamWidget declares a member of that name, and shadowing it is an error
	// in this build (C4458 is promoted).
	const TArray<UDreamWidget*>& Hosted = ContentNode->GetChildren();
	return Hosted.Num() > 0 ? Hosted[0] : nullptr;
}

void UDreamExpandableArea::HandleHeaderClicked()
{
	ToggleExpansion();
}

void UDreamExpandableArea::PushExpansionVisuals()
{
	const FDreamExpandableAreaStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ExpandableAreaStyle);

	if (ContentNode != nullptr)
	{
		// Inactive, not merely invisible: an inactive widget takes no layout space, is not drawn and
		// is not hit-testable, which is the whole of what "collapsed" means.
		ContentNode->SetWidgetActive(bIsExpanded);
	}

	// Which indicator shows is decided per STATE, by whether that state's brush holds an image -- the
	// check box's convention, and the reason the style carries an ExpandedBrush and a CollapsedBrush
	// rather than one. The two nodes are exclusive stand-ins for each other.
	const FDreamUIFaceBrush& StateBrush = bIsExpanded ? Active.ExpandedBrush : Active.CollapsedBrush;
	const bool bImageMark = (StateBrush.Image != nullptr);
	if (ArrowMarkNode != nullptr)
	{
		ArrowMarkNode->SetWidgetActive(bImageMark);
		if (bImageMark)
		{
			SkinFace(ArrowMarkNode, StateBrush);
			SizeFace(ArrowMarkNode, BrushSizeOr(StateBrush, Active.ArrowSize));
		}
	}
	if (ArrowNode != nullptr)
	{
		ArrowNode->SetWidgetActive(!bImageMark);
	}
	if (UDreamText* ArrowText = ArrowNode != nullptr ? Cast<UDreamText>(ArrowNode->GetVisual()) : nullptr)
	{
		// ASCII, and on purpose. The disclosure triangles would be the natural pair, but only the
		// DOWN one (U+25BC, the dropdown's arrow) is proven in the default SDF font, and a glyph that
		// is not there draws a tofu box -- U+2212 already shipped as one, which is why the spin box's
		// minus is a hyphen. Plus and minus are the pair this library has actually drawn, and they
		// read as an expander anyway. A project that wants triangles supplies the two state brushes:
		// an image wins over the glyph, above. Culture-invariant because these are glyphs, not words,
		// and written even while an image stands in, so emptying the brush finds them already right.
		ArrowText->SetText(FText::AsCultureInvariant(bIsExpanded ? TEXT("-") : TEXT("+")));
	}

	// The control's own measured height, which is the collapsed contract: a consumer's Auto slot asks
	// the CONTROL how tall it is, so a collapsed expander must answer with the header alone. Expanded
	// it answers header plus whatever the content column wants.
	const float ContentExtent = bIsExpanded ? MeasureContentExtent() : 0.0f;
	SizeControlHeight(Active.HeaderHeight + ContentExtent);
}

void UDreamExpandableArea::MoveIntoContent(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget) || ContentNode == nullptr || InWidget == ContentNode
		|| ContentNode->IsChildOf(InWidget))
	{
		return;
	}
	// Try, not Set: a refusal is real (a cycle, or a full parent) and silent otherwise -- the tree
	// would build, look right in a structural test, and be missing a widget. World position dropped
	// on purpose, the same call AddChild makes: a widget handed to a panel is handed to that panel's
	// arrangement, and keeping its old screen position only fights the first pass.
	//
	// The slot the column hands out comes with it: TrySetParent runs SynchronizePanelSlotForParent
	// with a FORCED re-capture, which is exactly the snapshot an Auto measure reads -- so the content
	// is measured at the size its author gave it rather than at the default 100x100, and this
	// control's whole expanded height is that measure.
	if (!InWidget->TrySetParent(ContentNode, false))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' refused '%s' as content."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ContentNode->GetDisplayName(), *InWidget->GetDisplayName());
	}
}

float UDreamExpandableArea::MeasureContentExtent()
{
	if (ContentNode == nullptr || ContentNode->GetChildrenCount() == 0)
	{
		// An empty section is its header. Asked before the layout, because a column with nothing in it
		// still has a rect, and that rect is not a claim about anything.
		return 0.0f;
	}
	if (UDreamPanelLayoutBase* RootLayout = RootNode != nullptr
		? Cast<UDreamPanelLayoutBase>(RootNode->GetLayoutContainer()) : nullptr)
	{
		// The layout's own question, asked the layout's own way: GetDesiredSize walks the fitter, the
		// container's preferred size and the authored snapshots, and deliberately never reads a rect a
		// panel pass has written. Reading ContentNode->GetHeight() instead would feed layout OUTPUT
		// back into a measurement -- the loop where a squeezed column measures as squeezed forever.
		return static_cast<float>(RootLayout->GetDesiredSize(ContentNode).Y);
	}
	return ContentNode->GetHeight();
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "ExpandableArea", UDreamExpandableArea)
