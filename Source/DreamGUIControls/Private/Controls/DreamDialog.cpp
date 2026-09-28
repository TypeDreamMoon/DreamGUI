// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamDialog.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Controls/DreamButton.h"
#include "Core/DreamUIBuilder.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/DreamUIModal.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIEventBlocker.h"

#define LOCTEXT_NAMESPACE "DreamDialog"

namespace DreamDialogLocal
{
	/**
	 * The text this dialog put in InButton's content hole, or null.
	 *
	 * Read back rather than kept in a second array beside ButtonWidgets: two lists rebuilt from the
	 * same specs is one list that can go out of step, and the hole is the only place this label has
	 * ever been. A button draws no text of its own, so the dialog is a HOST filling a hole here --
	 * exactly what `Native.Button OK { Text {} }` does in .dui, done from code.
	 */
	UDreamText* ButtonLabelVisual(const UDreamButton* InButton)
	{
		const UDreamWidget* Hole = IsValid(InButton) ? InButton->ContentNode.Get() : nullptr;
		UDreamWidget* Label = IsValid(Hole) && Hole->GetChildrenCount() > 0 ? Hole->GetChildByIndex(0) : nullptr;
		return IsValid(Label) ? Cast<UDreamText>(Label->GetVisual()) : nullptr;
	}
}

UDreamDialog::UDreamDialog()
{
	// A dialog with no buttons cannot be answered, and .dui has no array literal to write one with --
	// so the common pair is the DEFAULT rather than something every author has to remember. Cancel
	// first, the confirming one last and primary; the result names are the two the modal subsystem's
	// own header names.
	Buttons.Emplace(LOCTEXT("DefaultCancel", "Cancel"), TEXT("Cancel"), false);
	Buttons.Emplace(LOCTEXT("DefaultConfirm", "OK"), TEXT("Confirm"), true);
}

const FName UDreamDialog::BodySlotName(TEXT("Body"));

void UDreamDialog::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Panel"), PanelNode);
	OutParts.Emplace(TEXT("Title"), TitleNode);
	OutParts.Emplace(TEXT("Content"), ContentNode);
	OutParts.Emplace(TEXT("Message"), MessageNode);
	OutParts.Emplace(TEXT("ButtonRow"), ButtonRowNode);
	// The dimmer and the body hole are both optional. A template that scrims for itself needs no
	// dimmer of ours, and a dialog that only ever shows a sentence needs no hole.
	OutParts.Emplace(TEXT("Dimmer"), DimmerNode, /*bRequired*/false);
	OutParts.Emplace(UDreamDialog::BodySlotName, BodyNode, /*bRequired*/false);
}

void UDreamDialog::RealizeBuiltIn()
{
	using namespace DreamUI;

	// The root draws nothing and carries no layout container, so its two children are ANCHOR-driven
	// and each answers a different question. The dimmer fills (a stretch, which clears SizeDelta, so
	// there is no span for a setter to resolve later); the panel is a point-anchored rect with an
	// absolute size, written in ApplyStyle. Putting the panel INSIDE the dimmer would have been one
	// node fewer and a bug: hiding the dimmer when a host already scrims would take the whole dialog
	// with it.
	Realize(this,
		Widget("Dialog")
			.Stretch()
			.Children(
				Node<UDreamRectBlock>("Dimmer")
					.Stretch()
					// The standalone arrangement's input blocking. Under the modal subsystem this
					// node is asleep and the subsystem's own blocker is the one holding the line --
					// see RefreshHostArrangement.
					.With<UUIEventBlocker>(),

				Node<UDreamRectBlock>("Panel")
					.Self([](UDreamWidget& InPanel)
					{
						// The panel is a FIXED size and its content is not: a message longer than the
						// panel would otherwise draw straight across the screen behind it.
						InPanel.SetClipping(EDreamWidgetClipping::ClipToBounds);
					})
					// Title / content / buttons, top to bottom, with the style's Spacing between and
					// PanelPadding around. A column is the only thing all three parts agree about.
					.With<UDreamLayoutContainerVerticalBox>()
					.Children(
						DreamUI::Text("Title")
							.Visual([](UDreamText& InText)
							{
								InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
								InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
							})
							.Slot([](UDreamPanelSlot& InSlot)
							{
								InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
								InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
								// Auto: one line of title, measured from the glyphs.
								InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
							}),

						// The middle band gets its own node, and an overlay on it, so that a consumer
						// wanting a form instead of a sentence has somewhere with a SLOT to put it.
						// The message is merely the built-in occupant.
						Widget("Content")
							.With<UDreamLayoutContainerOverlay>()
							.Slot([](UDreamPanelSlot& InSlot)
							{
								InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
								InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
								// Fill, not Auto: the content area is what absorbs the panel's spare
								// height, which is what keeps the button row pinned to the bottom.
								InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
								InSlot.SetFillWeight(1.0f);
							})
							.Children(
								DreamUI::Text("Message")
									.Visual([](UDreamText& InText)
									{
										InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Left);
										InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Top);
									})
									.Slot([](UDreamPanelSlot& InSlot)
									{
										InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
										InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
									}),
								// The slot that comment promised. A SIBLING of the message rather
								// than a wrapper around it: "is this hole filled" is read off the
								// node's children, so a hole that also held the built-in occupant
								// would answer yes before anyone put anything in it.
								Widget("Body")
									.With<UDreamNamedSlot>()
									.Slot([](UDreamPanelSlot& InSlot)
									{
										InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
										InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
									})),

						Widget("ButtonRow")
							.With<UDreamLayoutContainerHorizontalBox>()
							.Slot([](UDreamPanelSlot& InSlot)
							{
								InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
								InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
								// Auto against an AUTHORED height, the .dui row idiom: the row is
								// exactly one button tall and ApplyStyle writes that number in.
								InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
							}))));

	// NOT a RebuildButtons() call, and the absence is deliberate. This function builds the tree; the
	// PARTS are bound after it (UDreamUIControl::NativeOnInitialized runs RealizeBuiltIn, BindParts,
	// WireParts, OnPartsReady, ApplyStyle in that order), so ButtonRowNode is still null here and a
	// rebuild would early-return without making anything -- which is exactly what it used to do. The
	// comment that stood here claimed the opposite and named the wrong step: the call that actually
	// builds the buttons is OnPartsReady's, and the template road -- where this function never runs
	// at all -- has always depended on that one.
}

void UDreamDialog::OnPartsReady()
{
	// Before the first style push, which walks the button widgets, so they have to exist first. This
	// is the ONLY place the buttons are first built, on both roads: see RealizeBuiltIn for why a
	// second call from there is a call that cannot work.
	RebuildButtons();
}

void UDreamDialog::NativeOnConstruct()
{
	Super::NativeOnConstruct();
	RefreshHostArrangement();
	if (bFocusDefaultButton)
	{
		// At CONSTRUCT and not at initialize: focus is an event-system idea and the event system
		// reaches a widget through its world, which a dialog does not have until it is attached.
		FocusDefaultButton();
	}
}

void UDreamDialog::RefreshHostArrangement()
{
	// Fill the parent -- but only when nothing else is arranging us. A dialog dropped into a stack or
	// a grid has a panel slot and is that panel's to position; writing anchors underneath a layout
	// container is a fight neither side wins. This is .Stretch()'s exact shape, corner anchors AND a
	// zero SizeDelta, and the zero delta is what makes it safe: it says "exactly the parent's span,
	// whenever the span is decided", leaving the anchor setter nothing to resolve against a parent
	// rect that has not been arranged yet.
	if (GetParent() != nullptr && GetPanelSlot() == nullptr)
	{
		SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
		SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D::ZeroVector);
	}

	RefreshDimmer();
}

bool UDreamDialogScope::HandleBackAction_Implementation()
{
	UDreamDialog* Dialog = OwnerDialog.Get();
	if (!IsValid(Dialog))
	{
		// The dialog went away and left its scope behind for a frame. Hand Back on rather than
		// swallowing it, which would leave the screen underneath unable to close either.
		return false;
	}
	Dialog->RequestCancel();
	// TRUE: taken, and Back must go no further down the stack -- one Back, one dialog closed, never
	// the dialog AND the page behind it.
	return true;
}

void UDreamDialog::RefreshDimmer()
{
	// Whoever is scrimming above us is scrimming for us. UDreamUIModalSubsystem's modal layer carries
	// a UUIEventBlocker -- that behaviour is what makes the layer eat the world's clicks -- so a
	// blocker anywhere up the parent chain is precisely the signal "the screen is already covered and
	// already dark". Darkening it a second time is the duplication a dialog composing with the
	// subsystem must not commit. The HOST half of that cannot be answered earlier than construct:
	// Initialize runs before CreateDreamWidget attaches the dialog, so at NativeOnInitialized there is
	// no parent to ask -- but asking again later is free, and is what makes bShowDimmer a live knob.
	//
	// Split out of RefreshHostArrangement deliberately: the other half of that function WRITES this
	// widget's anchors, and re-stretching the dialog on every style push would overrule anything that
	// had since positioned it. This half only decides one node's activity.
	const bool bHostAlreadyScrims = GetComponentInParent<UUIEventBlocker>(false) != nullptr;
	const bool bDimmerUp = bShowDimmer && !bHostAlreadyScrims;
	if (DimmerNode != nullptr)
	{
		DimmerNode->SetWidgetActive(bDimmerUp);
		if (bCloseOnDimmerClick && bDimmerUp)
		{
			// A button ON the scrim, which is what "click outside to dismiss" is: the blocker already
			// eats the click, and this is what makes eating it MEAN something. Added on demand rather
			// than in the built-in tree, so a dialog that does not offer the gesture carries no
			// selectable it never uses -- and so the scrim is not quietly navigable furniture.
			DimmerBehaviour = EnsureComponent<UUIButton>(DimmerNode);
			if (DimmerBehaviour != nullptr)
			{
				// FIVE states, one colour -- the scrim's own. A selectable always tints its target
				// (and re-points that target at its widget's own visual on register, so simply
				// leaving it null would not hold), so the way to make a scrim not react to the
				// pointer is to give it nothing to react WITH. Zero duration for the same reason:
				// there is no transition to watch.
				const FDreamDialogStyle& DimmerStyle = ResolveStyle(Style, &UDreamUIStyleSheet::DialogStyle);
				PushSelectableState(DimmerBehaviour, DimmerStyle.DimmerColor, DimmerStyle.DimmerColor,
					DimmerStyle.DimmerColor, DimmerStyle.DimmerColor, DimmerStyle.DimmerColor, 0.0f);
				// And not a place focus can land: a scrim is not furniture a gamepad should reach.
				DimmerBehaviour->SetCanNavigateHere(false);
				// Cleared first: RefreshDimmer runs on every style push, and an unguarded Add would
				// bind the same handler again on each of them.
				DimmerBehaviour->GetOnClickEvent().RemoveAll(this);
				DimmerBehaviour->GetOnClickEvent().AddUObject(this, &UDreamDialog::HandleDimmerClicked);
			}
		}
		else if (DimmerBehaviour != nullptr)
		{
			// Turned off again: the binding goes rather than the component, because destroying a
			// behaviour mid-style-push is a lifecycle event for the sake of a flag.
			DimmerBehaviour->GetOnClickEvent().RemoveAll(this);
		}
	}

	// The Back handler is the STANDALONE arrangement's, for the reason UDreamDialogScope states: the
	// modal layer above a hosted dialog already answers Back, with the result its own header
	// promises. Decided here because this is the one place that knows which arrangement we are in.
	const bool bWantsBackScope = bCloseOnBack && !bHostAlreadyScrims;
	if (bWantsBackScope && BackScope == nullptr)
	{
		BackScope = EnsureComponent<UDreamDialogScope>(GetContentRoot());
		if (BackScope != nullptr)
		{
			BackScope->OwnerDialog = this;
			// The scope's own close behaviour must not ALSO run: this dialog answers Back itself and
			// returns true, and a scope that then closed something else would be two answers to one
			// press.
			BackScope->SetCloseOnBack(false);
		}
	}
	else if (!bWantsBackScope && BackScope != nullptr)
	{
		BackScope->OwnerDialog = nullptr;
	}
}

void UDreamDialog::HandleDimmerClicked()
{
	if (!bCloseOnDimmerClick)
	{
		// The flag can go off between the binding and the click; the click is the last place that can
		// still honour it.
		return;
	}
	RequestCancel();
}

void UDreamDialog::SubmitDefaultButton()
{
	UDreamButton* Default = GetDefaultButton();
	if (Default == nullptr || Default->ButtonBehaviour == nullptr)
	{
		return;
	}
	// Through the button's own click seam, so a confirm from here and a confirm from the pointer take
	// exactly one road: the result payload, the control-level re-broadcast and the close all follow
	// from that one broadcast.
	Default->ButtonBehaviour->GetOnClickEvent().Broadcast();
}

void UDreamDialog::ApplyStyle()
{
	const FDreamDialogStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::DialogStyle);

	if (UDreamVisual* DimmerVisual = DimmerNode != nullptr ? DimmerNode->GetVisual() : nullptr)
	{
		// An absolute colour, not a tint over a brush: no part of this dialog hosts a UUISelectable,
		// so there is no transition writing these visuals and no second source to disagree with. (The
		// buttons do host one -- their colours arrive through FDreamDialogStyle::Button, which
		// UDreamButton::ApplyStyle pushes onto the selectable as absolutes.)
		DimmerVisual->SetColor(Active.DimmerColor);
	}

	ShapeFace(PanelNode, Active.CornerRadius);
	SkinFace(PanelNode, Active.PanelBrush);
	if (UDreamVisual* PanelVisual = PanelNode != nullptr ? PanelNode->GetVisual() : nullptr)
	{
		PanelVisual->SetColor(Active.PanelBackground);
	}
	if (PanelNode != nullptr)
	{
		// The centred panel in ABSOLUTE numbers against POINT anchors -- the layout law this codebase
		// paid four defects for in one day. A (0.25,0.25)-(0.75,0.75) ratio anchor reads the same on
		// paper, but the anchor SETTER resolves the parent's span at write time, and the parent here
		// is the stretched dimmer: its SizeDelta is zero on every frame but a full-layout one, so the
		// panel would be born zero-sized and only occasionally correct itself -- the progress fill's
		// walking dot and the dropdown list's stale width, again. A point anchor resolves no span at
		// all: (0.5,0.5) IS the centre, the centre pivot puts the panel's middle on it, and SizeDelta
		// on a point-anchored axis is simply the size.
		PanelNode->SetPivot(FVector2D(0.5, 0.5));
		PanelNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5), false, false);
		PanelNode->SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, Active.PanelSize);
	}

	if (UDreamLayoutContainerStackBox* Column = Cast<UDreamLayoutContainerStackBox>(
		PanelNode != nullptr ? PanelNode->GetLayoutContainer() : nullptr))
	{
		Column->SetPadding(Active.PanelPadding);
		Column->SetSpacing(Active.Spacing);
	}
	if (UDreamLayoutContainerStackBox* Row = Cast<UDreamLayoutContainerStackBox>(
		ButtonRowNode != nullptr ? ButtonRowNode->GetLayoutContainer() : nullptr))
	{
		Row->SetSpacing(Active.ButtonSpacing);
	}

	auto PushText = [](UDreamWidget* InNode, const FText& InText, const FColor& InColor, float InFontSize)
	{
		if (InNode == nullptr)
		{
			return;
		}
		if (UDreamText* TextVisual = Cast<UDreamText>(InNode->GetVisual()))
		{
			TextVisual->SetText(InText);
			TextVisual->SetColor(InColor);
			TextVisual->SetFontSize(InFontSize);
		}
		// Empty means ABSENT, not a reserved blank line: an Auto slot around an unwritten title would
		// otherwise keep a line's worth of gap above the message forever.
		InNode->SetWidgetActive(!InText.IsEmpty());
	};
	PushText(TitleNode, Title, Active.TitleColor, Active.TitleFontSize);
	PushText(MessageNode, Message, Active.MessageColor, Active.MessageFontSize);
	// The supplied body wins over the built-in sentence: they overlay the same band, so showing both
	// draws one through the other. Narrowing what PushText just decided rather than overwriting it --
	// an empty message stays away whether or not anything filled the hole.
	SwapBuiltInForSlot(MessageNode, BodyNode, BodySlotName, !Message.IsEmpty());

	if (ButtonRowNode != nullptr)
	{
		// SizeControlHeight's body, aimed at a PART rather than at the control. The row's slot is Auto
		// and a slot SNAPSHOTS the authored size at its first capture -- which happens before any
		// style was ever applied -- so a style edit that changes the button height has to re-take the
		// snapshot or the new number is never read. The max of the two button styles, because the row
		// is exactly one button tall and either kind may be the taller.
		ButtonRowNode->SetHeight(FMath::Max(Active.Button.Height, Active.PrimaryButton.Height));
		if (UDreamPanelSlot* RowSlot = ButtonRowNode->GetPanelSlot())
		{
			RowSlot->SyncAuthoredDesiredSizeFromWidget();
		}
		// A dialog with no buttons is a message board; do not reserve a row for it.
		ButtonRowNode->SetWidgetActive(Buttons.Num() > 0);
	}

	PushButtonStyles(Active);

	// LAST, and it is the whole of what made bShowDimmer a dead knob: the only reader of that flag
	// used to be the construct-time host arrangement, so a designer unticking it -- or a runtime
	// write -- decided nothing until the dialog was built again. Re-asking here costs one parent-chain
	// walk and puts the flag on the same footing as every other knob a style push re-states. The
	// dimmer half ONLY: re-stretching the dialog on every restyle would overrule whoever placed it.
	RefreshDimmer();
}

void UDreamDialog::SetShowDimmer(bool bInShowDimmer)
{
	if (bShowDimmer == bInShowDimmer)
	{
		return;
	}
	bShowDimmer = bInShowDimmer;
	// Only the dimmer, not the whole style: the flag decides one widget's activity and nothing about
	// how anything is drawn.
	RefreshDimmer();
}

FName UDreamDialog::ResolveCancelResult() const
{
	if (!CancelResult.IsNone())
	{
		return CancelResult;
	}
	for (const FDreamDialogButton& Spec : Buttons)
	{
		// The first NON-primary button. In every row this control builds -- and in the pair its own
		// constructor seeds -- that is the cancelling one, which is why the convention can be read
		// rather than configured.
		if (!Spec.bIsPrimary && !Spec.Result.IsNone())
		{
			return Spec.Result;
		}
	}
	if (Buttons.Num() > 0 && !Buttons.Last().Result.IsNone())
	{
		return Buttons.Last().Result;
	}
	// A dialog with no buttons at all still has to answer with something a caller can switch on.
	return TEXT("Cancel");
}

void UDreamDialog::RequestCancel()
{
	Close(ResolveCancelResult());
}

UDreamButton* UDreamDialog::GetDefaultButton() const
{
	for (int32 Index = 0; Index < ButtonWidgets.Num(); ++Index)
	{
		if (Buttons.IsValidIndex(Index) && Buttons[Index].bIsPrimary && IsValid(ButtonWidgets[Index]))
		{
			return ButtonWidgets[Index].Get();
		}
	}
	// None marked: the LAST one, because this control builds its rows cancel-first and the confirming
	// button is the one on the right -- the same order the seeded pair is in.
	for (int32 Index = ButtonWidgets.Num() - 1; Index >= 0; --Index)
	{
		if (IsValid(ButtonWidgets[Index]))
		{
			return ButtonWidgets[Index].Get();
		}
	}
	return nullptr;
}

void UDreamDialog::FocusDefaultButton()
{
	UDreamButton* Default = GetDefaultButton();
	// The FACE, not the button control: the selectable lives on the face, and focus is a selectable's.
	UDreamWidget* Target = Default != nullptr ? Default->FaceNode.Get() : nullptr;
	if (!IsValid(Target) || GetWorld() == nullptr)
	{
		// No world means no event system -- an initialize-time call, or a headless test. A dialog
		// without buttons has nothing to focus and says so by doing nothing.
		return;
	}
	if (UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(this, 0))
	{
		Events->SetSelectComponentWithDefault(Target);
	}
}

void UDreamDialog::SetTitle(const FText& InTitle)
{
	Title = InTitle;
	ApplyStyle();
}

void UDreamDialog::SetMessage(const FText& InMessage)
{
	Message = InMessage;
	ApplyStyle();
}

void UDreamDialog::SetButtons(const TArray<FDreamDialogButton>& InButtons)
{
	Buttons = InButtons;
	// The same gate the editor path uses: a row whose shape did not move keeps its widgets, and with
	// them anything a consumer hung on one. Re-wording every label is a push, not a rebuild.
	if (ButtonWidgetsAreStale())
	{
		RebuildButtons();
	}
	ApplyStyle();
}

void UDreamDialog::Close(FName InResult)
{
	// BEFORE anything that can destroy this widget. CloseTopModal tears the modal layer -- and this
	// dialog with it -- down inside the call, so a broadcast placed after it would be a broadcast
	// from an object that no longer exists.
	OnDialogClosed.Broadcast(InResult);

	UDreamUIModalSubsystem* Modal = UDreamUIModalSubsystem::Get(this);
	if (Modal != nullptr && Modal->GetActiveModalWidget() == this)
	{
		// The contract UDreamUIModalSubsystem documents: a dialog's buttons end the modal by naming
		// the result they mean. Everything after that is the subsystem's -- popping the focus scope,
		// delivering the result to whoever called ShowModal, destroying the layer, and showing the
		// next queued dialog.
		Modal->CloseTopModal(InResult);
		return;
	}
	// Standalone: back to the state a .dui-placed dialog waits in between questions.
	SetWidgetActive(false);
}

void UDreamDialog::RebuildButtons()
{
	for (const TObjectPtr<UDreamButton>& Existing : ButtonWidgets)
	{
		if (IsValid(Existing))
		{
			Existing->DestroyWidget();
		}
	}
	ButtonWidgets.Reset();

	UDreamWidgetTree* Tree = GetWidgetTree();
	if (ButtonRowNode == nullptr || Tree == nullptr)
	{
		return;
	}

	for (const FDreamDialogButton& Spec : Buttons)
	{
		UDreamButton* Button = Tree->ConstructWidget<UDreamButton>();
		if (!IsValid(Button))
		{
			continue;
		}
		// Named after the result it answers with: DisplayName is what every by-name lookup in this
		// framework matches on, so "Confirm" is the name a walkthrough or a test would reach for.
		Button->SetDisplayName(Spec.Result.IsNone() ? FString(TEXT("DialogButton")) : Spec.Result.ToString());
		// Parent BEFORE the slot, and the slot before Initialize. Both orderings are load-bearing: a
		// widget with no parent is told nothing (SetHorizontalAndVerticalAnchorMinMax's whole body
		// sits inside an `if (Parent)`), and UDreamButton::ApplyStyle syncs its authored height
		// THROUGH its slot -- a slot minted afterwards never sees the style's number and the row
		// measures the pre-style default instead.
		Button->SetParentBeforeRegister(ButtonRowNode);
		UDreamPanelSlot* Slot = Button->GetPanelSlot();
		if (!IsValid(Slot))
		{
			// What the builder's ApplySlotSettings does for a tree that is not registered yet:
			// registration would mint this eventually, and eventually is after the button has already
			// been styled and measured.
			Slot = Button->CreateNewPanelSlot(UDreamPanelSlot::StaticClass());
		}
		if (IsValid(Slot))
		{
			// Equal fill weights: the buttons share the row's width evenly, which needs no measuring
			// and no right-alignment maths that a two-button and a three-button dialog would disagree
			// about.
			Slot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
			Slot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
			Slot->SetSizeRule(EDreamPanelSizeRule::Fill);
			Slot->SetFillWeight(1.0f);
		}
		// The label, as content. A button draws no text of its own, so a dialog that wants words on
		// one puts a text in its hole like any other host -- through the named-slot binding rather
		// than by reaching into the button's tree afterwards, which is the road AttachNamedSlotContent
		// already walks and the reason it has to be done BEFORE Initialize. Built into the dialog's
		// own tree, which is also what SetContentForNamedSlot insists on: a hole is filled by the
		// host that placed the instance, never out of a third asset.
		//
		// Centred by UDreamText's own defaults, so nothing here has to say so; PushButtonStyles is
		// what puts the wording, the colour and the size on it, and re-puts them on every restyle.
		if (UDreamWidget* LabelWidget = DreamUI::Realize(Tree, DreamUI::Text(TEXT("Label"))))
		{
			Button->SetContentForNamedSlot(UDreamButton::ContentSlotName, LabelWidget);
		}
		// Inline, deliberately. The DIALOG's style has already resolved -- sheet or instance -- and
		// carries the button look it wants in Button/PrimaryButton. Left on ProjectStyleSheet each
		// button would re-resolve to the sheet's plain button style and those two fields would do
		// nothing at all.
		Button->StyleSource = EDreamUIStyleSource::Inline;
		// Builds the button's own tree and runs its ApplyStyle. Nothing else calls it for a widget
		// constructed into a tree rather than created through CreateDreamWidget, and it is idempotent.
		Button->Initialize();
		if (Button->ButtonBehaviour != nullptr)
		{
			// The RESULT as the payload. The control-level OnClicked is a dynamic delegate and can
			// carry neither payload nor index, and a captured index would drift the moment Buttons was
			// edited under a live dialog -- the name cannot.
			Button->ButtonBehaviour->GetOnClickEvent().AddUObject(
				this, &UDreamDialog::HandleButtonClicked, Spec.Result);
		}
		if (HasRegistered())
		{
			// Built after the dialog went live (SetButtons on a dialog already on screen). An
			// unregistered subtree is inert -- no layout, no rendering, no behaviour lifecycle -- and
			// it is structurally perfect the whole time it is dead, which is why nothing else notices.
			RegisterDreamWidgetHierarchy(Button);
		}
		ButtonWidgets.Add(Button);
	}
}

void UDreamDialog::PushButtonStyles(const FDreamDialogStyle& InActive)
{
	for (int32 Index = 0; Index < ButtonWidgets.Num(); ++Index)
	{
		UDreamButton* Button = ButtonWidgets[Index].Get();
		if (!IsValid(Button))
		{
			continue;
		}
		const bool bPrimary = Buttons.IsValidIndex(Index) && Buttons[Index].bIsPrimary;
		const FDreamButtonStyle& ButtonStyle = bPrimary ? InActive.PrimaryButton : InActive.Button;
		Button->Style = ButtonStyle;
		if (UDreamText* LabelVisual = DreamDialogLocal::ButtonLabelVisual(Button))
		{
			if (Buttons.IsValidIndex(Index))
			{
				// The spec array stays the truth about the label too, so editing it and re-applying
				// is enough -- no rebuild required for a wording change.
				LabelVisual->SetText(Buttons[Index].Label);
			}
			// The button styles nothing about the text in its hole -- it cannot, the text is this
			// dialog's widget. So the dialog's own style says how it reads, beside the two it
			// already carries for the title and the message.
			LabelVisual->SetColor(InActive.ButtonLabelColor);
			LabelVisual->SetFontSize(InActive.ButtonLabelFontSize);
		}
		// By hand, because nothing re-derives a control from a changed style property: that is the
		// SynchronizeProperties tax the whole control family pays (see UDreamUIControl).
		Button->ApplyStyle();
	}
}

void UDreamDialog::HandleButtonClicked(FName InResult)
{
	// Re-broadcast at the control before acting on it, so a consumer that only wants to hear the
	// click still hears it even for a dialog whose close is being handled elsewhere.
	OnButtonClicked.Broadcast(InResult);
	Close(InResult);
}

bool UDreamDialog::ButtonWidgetsAreStale() const
{
	if (ButtonWidgets.Num() != Buttons.Num())
	{
		return true;
	}
	for (int32 Index = 0; Index < ButtonWidgets.Num(); ++Index)
	{
		const UDreamButton* Button = ButtonWidgets[Index].Get();
		if (!IsValid(Button))
		{
			return true;
		}
		// The RESULT is structural rather than cosmetic: it is baked into the click binding as a
		// payload at build time and into the widget's display name, so a spec whose result moved has
		// a widget that would still answer with the old one. Read back off the name this control
		// wrote there, which is where RebuildButtons put it -- no second array to fall out of step.
		const FName Expected = Buttons[Index].Result;
		const FString WantedName = Expected.IsNone() ? FString(TEXT("DialogButton")) : Expected.ToString();
		if (Button->GetDisplayName() != WantedName)
		{
			return true;
		}
	}
	return false;
}

#if WITH_EDITOR
void UDreamDialog::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Rebuild BEFORE the base re-applies the style, which is the opposite order from the dropdown's
	// options push -- and for a reason. Options are data the behaviour re-reads; buttons are WIDGETS,
	// and a button built after ApplyStyle ran would be wearing the default button style with nothing
	// scheduled to correct it.
	//
	// But only when the widgets no longer answer the specs. It used to rebuild on EVERY edit, which
	// meant dragging a colour slider destroyed and re-created the whole row a frame at a time -- the
	// exact cost the list measured at ~40ms a change (widget churn dirties the outliner, and the
	// designer force-refreshes its details view on top of it) and the exact reason the list stopped
	// doing it. Everything else a spec says -- the wording, which button is primary -- PushButtonStyles
	// re-pushes into the standing widgets on the style push the base is about to make, and going
	// through the widgets rather than past them is also what keeps any runtime state hung on a
	// generated button (an animation, an extra component) alive across an edit.
	if (ButtonWidgetsAreStale())
	{
		RebuildButtons();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#undef LOCTEXT_NAMESPACE

void UDreamDialog::SetStyle(const FDreamDialogStyle& InStyle)
{
	Style = InStyle;
	ApplyStyle();
}

void UDreamDialog::SetCancelResult(FName InCancelResult)
{
	CancelResult = InCancelResult;
}

void UDreamDialog::SetFocusDefaultButton(bool bInFocusDefaultButton)
{
	bFocusDefaultButton = bInFocusDefaultButton;
}

void UDreamDialog::SetCloseOnBack(bool bInCloseOnBack)
{
	bCloseOnBack = bInCloseOnBack;
	// The scope and the scrim's button are both decided in RefreshDimmer. Before the parts exist there
	// is nothing to refresh, and the build will read the flag itself.
	if (DimmerNode != nullptr)
	{
		RefreshDimmer();
	}
}

void UDreamDialog::SetCloseOnDimmerClick(bool bInCloseOnDimmerClick)
{
	bCloseOnDimmerClick = bInCloseOnDimmerClick;
	if (DimmerNode != nullptr)
	{
		RefreshDimmer();
	}
}

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "Dialog", UDreamDialog)
