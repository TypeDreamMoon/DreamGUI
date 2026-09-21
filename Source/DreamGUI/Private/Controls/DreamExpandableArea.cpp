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
	// "HeaderFace", not "Header": the header HOLE is named Header (a slot's name is its host node's
	// display name), and FindPart walks ancestors before descendants -- so while the face carried
	// that name too, both fields bound to the FACE and SwapBuiltInForSlot put the whole header row
	// to sleep every time the hole was empty, which is every expander that does not want a custom
	// title. Two nodes, two names. A template author renaming this one back to Header re-creates it.
	OutParts.Emplace(TEXT("HeaderFace"), HeaderNode);
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
				//
				// "HeaderFace" rather than "Header", and the name is load-bearing: the header HOLE
				// further down has to be called Header (a named slot IS its host node's display
				// name, and HeaderSlotName is a public binding key), and a part list is matched by
				// name against a walk that meets ancestors first. Two nodes wearing one name meant
				// HeaderSlotNode resolved to this face and the empty-hole swap slept the header.
				Node<UDreamRectBlock>("HeaderFace")
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
	if (ContentNode != nullptr)
	{
		// The expanded height is a MEASUREMENT of what is in the column, and it was only ever taken
		// when the expanded flag moved -- so content that grew afterwards (a list that gained rows, a
		// text that wrapped onto another line, a nested section that opened) drew past the bottom of
		// a control still claiming its old height, and the Auto slot above it still reserved the old
		// room. The column's CHILD dimension event is exactly "what is in here changed size".
		ContentNode->GetChildDimensionChangedEvent().AddUObject(
			this, &UDreamExpandableArea::HandleContentDimensionsChanged);
	}
}

void UDreamExpandableArea::HandleContentDimensionsChanged(UDreamWidget* Child, bool bPivotChanged,
	bool bWidthChanged, bool bHeightChanged)
{
	if (!bHeightChanged || !bIsExpanded)
	{
		// A collapsed section measures its header and nothing else, so nothing under it is news; and
		// only the long axis is -- the width is whoever placed this control's to decide.
		return;
	}
	const FDreamExpandableAreaStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::ExpandableAreaStyle);
	// The height alone, not the whole of PushExpansionVisuals: the indicator and the column's
	// activity are decided by the FLAG, which has not moved, and re-pushing them would restate the
	// style on every keystroke into a text field down there.
	//
	// Re-entrant only in the harmless direction: writing this control's height cascades to stretched
	// descendants, which can bring the news back here -- and MeasureContentExtent asks the LAYOUT
	// (GetDesiredSize walks the fitter and the authored snapshots, never a rect a panel pass wrote),
	// so the second answer is the first answer and SetHeight's equality gate ends it there.
	SizeControlHeight(Active.HeaderHeight + ResolveContentExtent());
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
		PushSelectableState(HeaderBehaviour, Active.HeaderNormal, Active.HeaderHovered, Active.HeaderPressed,
			Active.HeaderDisabled, Active.HeaderFocused, Active.TransitionDuration);
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
	if (ExpansionDuration > KINDA_SMALL_NUMBER)
	{
		// Travel FROM WHERE IT IS, not from the end it was last at: a section toggled again mid-open
		// must turn round from the height it is showing rather than snapping to the other end first.
		bExpansionAnimating = true;
		SetWantsTick(true);
	}
	else
	{
		ExpansionAlpha = bIsExpanded ? 1.0f : 0.0f;
	}
	PushExpansionVisuals();
	OnExpansionChanged.Broadcast(bIsExpanded);
	OnValueChangedBP.Broadcast(bIsExpanded);
}

void UDreamExpandableArea::SetStyle(const FDreamExpandableAreaStyle& InStyle)
{
	Style = InStyle;
	ApplyStyle();
}

void UDreamExpandableArea::SetLabel(const FText& InLabel)
{
	Label = InLabel;
	// The header's words are written by the style pass, so a bare property write left the control
	// showing the previous ones until something unrelated happened to push.
	ApplyStyle();
}

void UDreamExpandableArea::SetExpansionDuration(float InExpansionDuration)
{
	// Nothing re-run: a duration describes the NEXT move, and replaying the current one to honour a
	// new speed would animate a section the player has already finished opening.
	ExpansionDuration = FMath::Max(0.0f, InExpansionDuration);
}

void UDreamExpandableArea::SetMaxHeight(float InMaxHeight)
{
	const float Clamped = FMath::Max(0.0f, InMaxHeight);
	if (MaxHeight == Clamped)
	{
		return;
	}
	MaxHeight = Clamped;
	// The ceiling is part of the height this control claims AND of what the column clips to, both of
	// which are written by the expansion push.
	PushExpansionVisuals();
}

void UDreamExpandableArea::NativeOnTick(float DeltaTime)
{
	Super::NativeOnTick(DeltaTime);
	if (!bExpansionAnimating)
	{
		return;
	}
	const float Target = bIsExpanded ? 1.0f : 0.0f;
	const float Duration = FMath::Max(ExpansionDuration, KINDA_SMALL_NUMBER);
	// Linear, deliberately: a section opening is a reveal rather than a flourish, and an eased one
	// reads as sluggish at the sizes a settings page uses. A project wanting a curve animates the
	// control's own height from a sequence instead.
	ExpansionAlpha = FMath::Clamp(ExpansionAlpha + (bIsExpanded ? DeltaTime : -DeltaTime) / Duration, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(ExpansionAlpha, Target))
	{
		ExpansionAlpha = Target;
		bExpansionAnimating = false;
		// The tick is the animation's only cost and it leaves with it: a settled expander joins no
		// tick list at all.
		SetWantsTick(false);
	}
	PushExpansionVisuals();
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
		//
		// While an open or close is TRAVELLING the column stays awake even at alpha zero, because a
		// sleeping widget measures and draws nothing and the travel would be a jump with extra steps.
		// It goes to sleep on arrival, which is the frame the alpha settles.
		ContentNode->SetWidgetActive(bIsExpanded || bExpansionAnimating);
		// Clipped whenever there is a ceiling to clip to, or whenever the column is mid-travel and
		// therefore showing less than it measures. Off again once it is settled and uncapped, so an
		// ordinary expander keeps the clip-free tree it has always had.
		ContentNode->SetClipping((MaxHeight > 0.0f || bExpansionAnimating)
			? EDreamWidgetClipping::ClipToBounds
			: EDreamWidgetClipping::Inherit);
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
	// it answers header plus whatever the content column wants, capped by MaxHeight.
	//
	// The ALPHA is what makes an animated open a real one rather than a fade: the control's claimed
	// height is what the page around it lays out against, so travelling it is the only way the page
	// moves with the section. Instant mode leaves the alpha pinned at the ends, so this line reads
	// exactly as it did.
	const float ContentExtent = ResolveContentExtent() * (bExpansionAnimating ? ExpansionAlpha : (bIsExpanded ? 1.0f : 0.0f));
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

float UDreamExpandableArea::ResolveContentExtent()
{
	const float Measured = MeasureContentExtent();
	// Zero is NO ceiling rather than a ceiling of zero -- the same reading MinHandleLength and every
	// other optional number in this family gets, and the one that keeps an unconfigured control
	// behaving as it always has.
	return MaxHeight > 0.0f ? FMath::Min(Measured, MaxHeight) : Measured;
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
