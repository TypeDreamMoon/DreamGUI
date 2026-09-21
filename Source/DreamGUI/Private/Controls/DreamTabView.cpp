// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamTabView.h"
#include "DreamGUI.h"

#include "Core/DreamUIWidgetRegistry.h"

#include "Core/DreamUIBuilder.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "Interaction/UIButton.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"

void UDreamTabView::CollectParts(TArray<FDreamControlPart>& OutParts)
{
	OutParts.Emplace(TEXT("Body"), BodyNode);
	OutParts.Emplace(TEXT("TabStrip"), StripNode);
	OutParts.Emplace(TEXT("PageHost"), PageHostNode);
	// The moving underline is decoration: a template whose tabs mark themselves needs none.
	OutParts.Emplace(TEXT("Indicator"), IndicatorNode, /*bRequired*/false);
}

void UDreamTabView::RealizeBuiltIn()
{
	using namespace DreamUI;

	// A column: the strip hugs its tabs, the page area takes the rest. A vertical box rather than
	// two anchor-driven halves on purpose -- an anchor-driven child only re-derives a stretched axis
	// when its OWN anchor data changes, so a hand-anchored split would be correct exactly until the
	// screen resized and nothing told it.
	Realize(this,
		Widget("Body")
			.Stretch()
			.With<UDreamLayoutContainerVerticalBox>()
			.Children(
				// The strip draws nothing itself -- the style has a colour for a tab and for a page,
				// and none for the space around them, which is the honest amount of opinion for a
				// row of buttons to have.
				Widget("TabStrip")
					.With<UDreamLayoutContainerHorizontalBox>()
					// The group lives here, on the tabs' common parent: that is where a toggle's own
					// auto-find would look for it, and it is the one widget that outlives any
					// particular strip contents. Configured in WireParts, which is the half a
					// template's own strip needs too.
					.With<UUIToggleGroup>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						// Auto: the strip is exactly as tall as the tallest tab, and a tab is never
						// shorter than the style's TabHeight (see ApplyStyle's note on the plate).
						InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})
					.Children(
						// Deliberately slot-less and layout-ignoring: it is a child of the strip so
						// its anchors resolve in the strip's frame -- the same frame the horizontal
						// box writes the tabs into -- while taking no place in the row. Giving it a
						// slot would make the box count it as a tab-shaped gap.
						Node<UDreamRectBlock>("Indicator")
							.Self([](UDreamWidget& InIndicator)
							{
								InIndicator.SetIgnoreLayout(true);
							})),
				// The page area IS the switcher's widget: one panel, one background, one padding.
				Node<UDreamRectBlock>("PageHost")
					.With<UDreamLayoutContainerWidgetSwitcher>()
					.Slot([](UDreamPanelSlot& InSlot)
					{
						InSlot.SetSizeRule(EDreamPanelSizeRule::Fill);
						InSlot.SetFillWeight(1.0f);
						InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
						InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
					})));
}

void UDreamTabView::WireParts()
{
	// Two containers this control keeps a typed handle on, held rather than looked up each time
	// because every tab rebuild and every page switch goes through them. Ensure, not Get: on the
	// template road the strip is somebody's drawing and this is what makes its tabs a group.
	TabGroup = EnsureComponent<UUIToggleGroup>(StripNode);
	if (TabGroup != nullptr)
	{
		// A second click on the open tab must not close it. UUIToggle's click handler flips the
		// value, and with none-selected allowed that flip would leave the view with no tab lit and
		// the switcher still showing a page nobody chose.
		TabGroup->SetAllowNoneSelected(false);
	}
	PageSwitcher = PageHostNode != nullptr
		? Cast<UDreamLayoutContainerWidgetSwitcher>(PageHostNode->GetLayoutContainer())
		: nullptr;
	if (PageSwitcher == nullptr && PageHostNode != nullptr)
	{
		// A template whose page host is not a switcher cannot switch pages, and every push into it
		// would land on nothing. Said out loud rather than left as a view stuck on page one.
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d '%s' has no widget switcher on its PageHost; pages will not switch."),
			ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *GetPathDisplayName());
	}
}

void UDreamTabView::OnPartsReady()
{
	// The pages a host nested on this control, from the snapshot the base took before either road
	// built anything. This control opens no default slot: a page is not merely content moved into a
	// hole, it is a page AND a tab, and only this class knows how to make the second.
	AdoptAuthoredPages(HostSuppliedChildren);
	// The strip, then the look: RebuildTabs ends in ApplyStyle, because a tab it just made has no
	// colours yet and a selectable with no colours ships white.
	RebuildTabs();
}

void UDreamTabView::AdoptAuthoredPages(const TArray<TObjectPtr<UDreamWidget>>& InAuthoredChildren)
{
	for (UDreamWidget* Page : InAuthoredChildren)
	{
		// Our own root was appended after the snapshot was taken, so it cannot be in here; the guard
		// is for a caller that hands the same list twice.
		if (!IsValid(Page) || Page == BodyNode)
		{
			continue;
		}
		AttachPage(Page);
	}
}

void UDreamTabView::AttachPage(UDreamWidget* InPage)
{
	if (!IsValid(InPage) || PageHostNode == nullptr)
	{
		return;
	}
	// The same branch InitializeWidgetStatic draws when it fills a named slot, for the same reason: a
	// live widget moves through the attach path, and one that has never registered is hung directly,
	// because the attach path runs layout against a hierarchy that is still being assembled.
	if (InPage->HasRegistered())
	{
		InPage->TrySetParent(PageHostNode, false);
	}
	else
	{
		InPage->SetParentBeforeRegister(PageHostNode);
		if (PageHostNode->HasRegistered())
		{
			// Attached below something already live: SetParentBeforeRegister raises no attach event,
			// so without this the page registers holding its birth defaults and is laid out by nobody.
			RegisterDreamWidgetHierarchy(InPage);
		}
	}
}

void UDreamTabView::RebuildTabs()
{
	if (StripNode == nullptr)
	{
		// Called before NativeOnInitialized -- an authored value arriving through a setter, say. The
		// tree's own build ends in a RebuildTabs of its own, so nothing is lost by declining here.
		return;
	}

	using namespace DreamUI;

	for (FDreamTabViewTab& Tab : Tabs)
	{
		// Out of the group before out of the tree. A toggle only unregisters itself in OnDestroy,
		// which is a behaviour lifecycle callback and never runs for an unregistered strip -- the
		// group would keep the stale entry and index every later tab one place too far along.
		if (Tab.Toggle != nullptr)
		{
			Tab.Toggle->SetToggleGroup(nullptr);
		}
		if (IsValid(Tab.TabNode))
		{
			StripNode->DestroyChild(Tab.TabNode);
		}
	}
	Tabs.Reset();
	if (TabGroup != nullptr)
	{
		// LastSelect still points at a tab that is going away; clearing it here means the first new
		// tab to be switched on is a fresh selection rather than a switch from a corpse.
		TabGroup->ClearSelection();
	}

	const int32 TabCount = GetTabCount();
	Tabs.Reserve(TabCount);
	for (int32 Index = 0; Index < TabCount; ++Index)
	{
		UDreamWidget* TabRoot = nullptr;
		UDreamWidget* SelectedPlate = nullptr;
		UDreamWidget* Label = nullptr;
		UDreamWidget* CloseFace = nullptr;
		UUIToggle* Toggle = nullptr;

		// Realized into this control's own tree and parented to the strip: the builder attaches with
		// SetParentBeforeRegister, which is the cheap attach and the only legal one for a subtree
		// nothing has looked at yet. A live strip registers the result below.
		Realize(GetWidgetTree(),
			Node<UDreamRectBlock>(FName(*FString::Printf(TEXT("Tab_%d"), Index))).Out(TabRoot)
				// An overlay so the plate and the label have slots to be placed in; a child of a
				// widget with no layout container gets no slot at all.
				.With<UDreamLayoutContainerOverlay>()
				.With<UUIToggle>([&Toggle](UUIToggle& InToggle)
				{
					Toggle = &InToggle;
				})
				.Slot([](UDreamPanelSlot& InSlot)
				{
					// Auto along the row: a tab is exactly as wide as its label plus the style's
					// padding, which is what makes a strip of tabs read as tabs and not as columns.
					InSlot.SetSizeRule(EDreamPanelSizeRule::Auto);
					InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
					InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
				})
				.Children(
					// First child, so it stacks BEHIND the label: an overlay paints in child order.
					Node<UDreamRectBlock>("Selected").Out(SelectedPlate)
						.Slot([](UDreamPanelSlot& InSlot)
						{
							InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
							InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
						}),
					DreamUI::Text("Label").Out(Label)
						.Visual([](UDreamText& InText)
						{
							InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
							InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
						})
						.Slot([](UDreamPanelSlot& InSlot)
						{
							InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
							InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
						}),
					// The close button, asleep unless the view offers closing. Last in the overlay so
					// it draws over the label, and right-aligned inside it so it sits in the corner
					// every browser puts it in rather than over the caption's middle.
					Node<UDreamRectBlock>(FName(*FString::Printf(TEXT("Tab_%d_Close"), Index))).Out(CloseFace)
						.Self([](UDreamWidget& InClose) { InClose.SetWidgetActive(false); })
						.With<UDreamLayoutContainerOverlay>()
						.Slot([](UDreamPanelSlot& InSlot)
						{
							InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Right);
							InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Center);
						})
						.Children(
							DreamUI::Text(FName(*FString::Printf(TEXT("Tab_%d_CloseGlyph"), Index)))
								.Visual([](UDreamText& InText)
								{
									// ASCII, and for the reason the spin box's minus is: U+2715 and
									// its neighbours are not proven to exist in the default SDF font
									// and a missing code point draws a tofu box rather than nothing.
									// A lowercase x reads as a close button at this size and cannot
									// be missing. A project wanting a real glyph supplies a tab
									// template.
									InText.SetText(FText::AsCultureInvariant(TEXT("x")));
									InText.SetParagraphHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
									InText.SetParagraphVerticalAlignment(EDreamUITextParagraphVerticalAlign::Middle);
								})
								.Slot([](UDreamPanelSlot& InSlot)
								{
									InSlot.SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
									InSlot.SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
								}))),
			StripNode);

		FDreamTabViewTab& Entry = Tabs.AddDefaulted_GetRef();
		Entry.TabNode = TabRoot;
		Entry.SelectedNode = SelectedPlate;
		Entry.LabelNode = Label;
		Entry.Toggle = Toggle;
		Entry.CloseNode = CloseFace;
		if (CloseFace != nullptr)
		{
			Entry.CloseBehaviour = EnsureComponent<UUIButton>(CloseFace);
			if (Entry.CloseBehaviour != nullptr)
			{
				Entry.CloseBehaviour->SetTransitionTarget(CloseFace->GetVisual());
				// The INDEX as the payload, captured at build time -- and safe to capture precisely
				// because the strip is rebuilt whenever its length changes, so no binding outlives
				// the numbering it was made under. (The dialog's buttons capture a result NAME for
				// the opposite reason: its row is rebound rather than rebuilt.)
				Entry.CloseBehaviour->GetOnClickEvent().AddUObject(
					this, &UDreamTabView::HandleCloseClicked, Index);
			}
		}

		if (Toggle != nullptr)
		{
			// Two transitions, two visuals -- the library's recurring split. The pointer states tint
			// the face the tab is standing on; the checked state tints the plate over it.
			Toggle->SetTransitionTarget(TabRoot != nullptr ? TabRoot->GetVisual() : nullptr);
			Toggle->SetToggleTransitionTarget(SelectedPlate != nullptr ? SelectedPlate->GetVisual() : nullptr);
			// Off BEFORE it joins. UUIToggle ships bIsOn true, and a group takes an incoming member
			// that is on as the new selection -- adding N of them would walk the selection along to
			// the last tab and fire the group's event N times on the way.
			Toggle->SetIsOnWithoutNotify(false);
			// Explicit, rather than SetAutoFindToggleGroupInParent: that flag is read in Awake, and
			// this control creates both ends here, in NativeOnInitialized. Searching for something we
			// are holding would only mean the wiring is invisible until begin play.
			Toggle->SetToggleGroup(TabGroup);
			Toggle->GetOnValueChangedEvent().AddUObject(this, &UDreamTabView::HandleTabValueChanged);
		}

		// An authored tab, when the consumer supplied a class for one. See TabTemplateClass: the tab
		// stays the control's face, plate and group membership; the content is the template's.
		// Before registration, so the whole tab registers once as a piece.
		if (TabTemplateClass != nullptr && GetWorld() != nullptr && IsValid(TabRoot))
		{
			bool bHasAuthoredContent = false;
			if (UDreamUserWidget* Content = CreateDreamWidget(GetWorld(), TabTemplateClass, TabRoot))
			{
				bHasAuthoredContent = true;
				Content->SetDisplayName(TEXT("TabContent"));
				if (UDreamPanelSlot* ContentSlot = Content->GetPanelSlot())
				{
					ContentSlot->SetHorizontalAlignment(EDreamPanelHorizontalAlignment::Fill);
					ContentSlot->SetVerticalAlignment(EDreamPanelVerticalAlignment::Fill);
				}
			}
			// The stock label and the supplied content are two answers to what is on this tab, laid
			// over each other in the same overlay -- the same rule the content slots follow. Gated on
			// the content EXISTING rather than on having meant to make it: instancing can fail (an
			// abstract class, a class that would be its own template), and a tab that answered "a
			// template drew this one" while holding nothing is a blank page tab. UDreamListViewBase's
			// BindRow and UDreamDropdown's item hook decide the same question the same way.
			if (bHasAuthoredContent && Label != nullptr)
			{
				Label->SetWidgetActive(false);
			}
		}

		if (StripNode->HasRegistered() && IsValid(TabRoot) && !TabRoot->HasRegistered())
		{
			RegisterDreamWidgetHierarchy(TabRoot);
		}

		OnTabGenerated.Broadcast(Index, TabRoot);
	}

	// A strip that just changed length is an upper bound that just changed, so a request that is now
	// out of range settles HERE -- before ApplyStyle pushes it into the switcher and the toggles.
	// The field rather than the setter, because ApplyStyle ends in the ApplyActiveTab the setter
	// would have made, and pushing the same index twice is work nobody asked for.
	ActiveTabIndex = SanitizeTabIndex(ActiveTabIndex);

	ApplyStyle();
}

void UDreamTabView::ApplyStyle()
{
	const FDreamTabViewStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::TabViewStyle);

	// The page area.
	ShapeFace(PageHostNode, Active.CornerRadius);
	SkinFace(PageHostNode, Active.PageBrush);
	if (UDreamVisual* PageVisual = PageHostNode != nullptr ? PageHostNode->GetVisual() : nullptr)
	{
		PageVisual->SetColor(Active.PageBackground);
	}
	if (PageSwitcher != nullptr)
	{
		PageSwitcher->SetPadding(Active.PagePadding);
	}
	if (UDreamLayoutContainerStackBox* Row = StripNode != nullptr
		? Cast<UDreamLayoutContainerStackBox>(StripNode->GetLayoutContainer()) : nullptr)
	{
		Row->SetSpacing(Active.TabSpacing);
	}

	// The values first, colours after: SetOnColor and SetOffColor apply immediately only for the
	// state the toggle is actually in, so pushing them at a stale value lands the selected colour on
	// whichever tab happened to be lit -- the radio button's rule, and the reason it is written down.
	ApplyActiveTab();

	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		FDreamTabViewTab& Tab = Tabs[Index];
		ShapeFace(Tab.TabNode, Active.CornerRadius);
		SkinFace(Tab.TabNode, Active.TabBrush);
		ShapeFace(Tab.SelectedNode, Active.CornerRadius);
		// Zero wide, TabHeight tall, and both halves are deliberate. An overlay measures as the MAX
		// over its children, so the height is the tab's floor -- UMG's MinDesiredHeight, spelled in
		// the one vocabulary this layout has -- while a zero width keeps the plate from ever widening
		// a tab past its own label. Neither number is what the plate DRAWS at: its slot fills.
		SizeFace(Tab.SelectedNode, FVector2D(0.0, Active.TabHeight));

		if (UDreamText* LabelVisual = Tab.LabelNode != nullptr ? Cast<UDreamText>(Tab.LabelNode->GetVisual()) : nullptr)
		{
			LabelVisual->SetText(ResolveTabLabel(Index));
			LabelVisual->SetFontSize(Active.FontSize);
		}
		if (UDreamPanelSlot* LabelSlot = Tab.LabelNode != nullptr ? Tab.LabelNode->GetPanelSlot() : nullptr)
		{
			// The padding is the LABEL's, which is what makes the tab's Auto width hug its text.
			LabelSlot->SetPadding(Active.TabPadding);
		}
		// The close button wakes with the view's own offer, and wears the tab's colours: it is the
		// same furniture, and a second set of style fields for it would say the same thing again.
		if (Tab.CloseNode != nullptr)
		{
			Tab.CloseNode->SetWidgetActive(bTabsClosable);
			if (bTabsClosable)
			{
				SizeFace(Tab.CloseNode, FVector2D(Active.TabHeight * 0.5, Active.TabHeight * 0.5));
				ShapeFace(Tab.CloseNode, Active.CornerRadius);
				if (Tab.CloseBehaviour != nullptr)
				{
					PushSelectableState(Tab.CloseBehaviour, Active.TabNormal, Active.TabHovered,
						Active.TabPressed, Active.TabDisabled, Active.TabFocused, Active.TransitionDuration);
				}
			}
		}
		if (Tab.Toggle != nullptr)
		{
			// A selectable left without explicit colours ships white -- these are never optional.
			PushSelectableState(Tab.Toggle, Active.TabNormal, Active.TabHovered, Active.TabPressed,
				Active.TabDisabled, Active.TabFocused, Active.TransitionDuration);
			// A disabled tab cannot be CLICKED into, and wears TabDisabled because the selectable
			// re-derives its own state from this flag. Code may still open it -- an interactable flag
			// stops the player, not the program, which is this library's rule everywhere.
			Tab.Toggle->SetInteractable(IsTabEnabled(Index));
			// The plate: opaque in the selected colour while this tab is open, and the same colour at
			// zero alpha otherwise, so an unselected tab shows the face's own pointer tint through it.
			Tab.Toggle->SetOnColor(Active.TabSelected);
			Tab.Toggle->SetOffColor(FColor(Active.TabSelected.R, Active.TabSelected.G, Active.TabSelected.B, 0));
		}
	}

	// No SizeControlHeight: unlike a button, a tab view is a region rather than a thing of a
	// particular size. Its height is its content's -- the strip plus whatever the pages need -- and
	// how much room it gets belongs to whoever placed it.
}

int32 UDreamTabView::GetTabCount() const
{
	// Whichever list is longer. A screen that authors its captions before its pages should see the
	// strip it is building, and a .dui that nests pages with no captions at all still gets a tab per
	// page (ResolveTabLabel names it after the node).
	return FMath::Max(TabLabels.Num(), GetPageCount());
}

FText UDreamTabView::ResolveTabLabel(int32 InIndex) const
{
	if (TabLabels.IsValidIndex(InIndex) && !TabLabels[InIndex].IsEmpty())
	{
		return TabLabels[InIndex];
	}
	if (const UDreamWidget* Page = GetPage(InIndex))
	{
		// Culture-invariant, and not apologetically: a node id is an identifier the author typed into
		// a source file, not a string anyone will translate. Dressing it as localizable would put a
		// key in the gather output that no translator can act on.
		return FText::AsCultureInvariant(Page->GetDisplayName());
	}
	return FText::AsCultureInvariant(FString::FromInt(InIndex + 1));
}

int32 UDreamTabView::GetPageCount() const
{
	return PageHostNode != nullptr ? PageHostNode->GetChildrenCount() : 0;
}

UDreamWidget* UDreamTabView::GetPage(int32 InIndex) const
{
	if (PageHostNode == nullptr)
	{
		return nullptr;
	}
	const TArray<UDreamWidget*>& Pages = PageHostNode->GetChildren();
	return Pages.IsValidIndex(InIndex) ? Pages[InIndex] : nullptr;
}

UDreamWidget* UDreamTabView::GetActivePage() const
{
	// The switcher's own resolution, not a second copy of it: it answers from its cache when the
	// cache is still its child, and from the index otherwise.
	return PageSwitcher != nullptr ? PageSwitcher->GetActiveWidget() : nullptr;
}

void UDreamTabView::AddPage(UDreamWidget* InPage)
{
	if (!IsValid(InPage))
	{
		return;
	}
	AttachPage(InPage);
	// The strip is a function of the page count, so a page that arrives past the end of TabLabels
	// brings its own tab with it.
	RebuildTabs();
}

void UDreamTabView::SetTabLabels(const TArray<FText>& InLabels)
{
	TabLabels = InLabels;
	RebuildTabs();
}

int32 UDreamTabView::SanitizeTabIndex(int32 InIndex) const
{
	// The floor always: there is no tab before the first one, whatever the strip holds.
	const int32 Floored = FMath::Max(0, InIndex);
	if (Tabs.Num() <= 0)
	{
		// No strip yet, so no upper bound is KNOWABLE -- and the index is routinely authored before
		// the pages attach, which is the whole reason this property is documented as a request the
		// switcher resolves at layout time. Storing it untouched is what makes that work.
		return Floored;
	}
	// A strip that exists is an upper bound that exists. Without this, an index past the end stayed
	// in the property for good: every reader clamped it FOR THIS PASS ONLY (see ApplyActiveTab), so
	// the strip lit the last tab while the property, the broadcast and any two-way binding all went
	// on carrying a number no tab has -- and adding a tab later silently jumped the selection to it.
	return FMath::Min(Floored, Tabs.Num() - 1);
}

void UDreamTabView::SetActiveTabIndex(int32 InIndex)
{
	const int32 Sanitized = SanitizeTabIndex(InIndex);
	const bool bChanged = ActiveTabIndex != Sanitized;
	ActiveTabIndex = Sanitized;
	ApplyActiveTab();
	if (bChanged)
	{
		OnTabChanged.Broadcast(Sanitized), OnValueChangedBP.Broadcast(Sanitized);
		if (bFocusPageOnTabChange && bTabChangeFromUser)
		{
			// AFTER the broadcast, so a consumer that rearranges the page from its handler has already
			// done so and focus lands in the page as it now stands. Only for a user switch -- see
			// bTabChangeFromUser.
			FocusActivePage();
		}
	}
}

void UDreamTabView::SetActiveTabIndexWithoutNotify(int32 InIndex)
{
	ActiveTabIndex = SanitizeTabIndex(InIndex);
	ApplyActiveTab();
}

void UDreamTabView::ApplyActiveTab()
{
	const FDreamTabViewStyle& Active = ResolveStyle(Style, &UDreamUIStyleSheet::TabViewStyle);

	if (PageSwitcher != nullptr)
	{
		// The REQUEST goes in, unclamped: the switcher stores it and resolves it against the child
		// count at layout time, so an index set before the pages attach still lands when they do.
		PageSwitcher->SetActiveWidgetIndex(ActiveTabIndex);
	}

	// The strip has to pick a real tab even when the index runs past it, and it is the same clamp the
	// switcher makes for the same reason -- for this pass only, never written back to the property.
	const int32 Resolved = Tabs.Num() > 0 ? FMath::Clamp(ActiveTabIndex, 0, Tabs.Num() - 1) : INDEX_NONE;

	// Switching the new tab ON is what switches the old one off: that is the group's job and the
	// whole reason the tabs are toggles. Doing it the other way round -- everything off, then one on
	// -- cannot work, because a group that refuses an empty selection refuses the first half.
	if (Tabs.IsValidIndex(Resolved) && Tabs[Resolved].Toggle != nullptr)
	{
		// Without notify: pushing the authored index in is not the user picking a tab.
		Tabs[Resolved].Toggle->SetIsOnWithoutNotify(true);
	}

	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		if (UDreamText* LabelVisual = Tabs[Index].LabelNode != nullptr
			? Cast<UDreamText>(Tabs[Index].LabelNode->GetVisual()) : nullptr)
		{
			// The control's own push, because there is no transition left to carry it: a selectable
			// owns a pointer transition and a toggle adds a checked one, and both are already aimed
			// (at the face, and at the plate). A third appearance needs a third writer.
			LabelVisual->SetColor(Index == Resolved ? Active.LabelSelectedColor : Active.LabelColor);
		}
	}

	ApplyIndicator(Active);
}

void UDreamTabView::ApplyIndicator(const FDreamTabViewStyle& InActive)
{
	if (IndicatorNode == nullptr)
	{
		return;
	}
	if (UDreamVisual* IndicatorVisual = IndicatorNode->GetVisual())
	{
		IndicatorVisual->SetColor(InActive.IndicatorColor);
	}

	const int32 Resolved = Tabs.Num() > 0 ? FMath::Clamp(ActiveTabIndex, 0, Tabs.Num() - 1) : INDEX_NONE;
	UDreamWidget* Tab = Tabs.IsValidIndex(Resolved) ? Tabs[Resolved].TabNode.Get() : nullptr;
	// Nothing to underline, or the style turned the line off by giving it no thickness.
	IndicatorNode->SetWidgetActive(IsValid(Tab) && InActive.IndicatorThickness > 0.0f);
	if (!IsValid(Tab))
	{
		return;
	}

	// ABSOLUTE numbers, read from the live rects, with POINT anchors -- and this is the one place in
	// the control where that is a rule rather than a preference. An anchor-driven child (no panel
	// above it, so no slot) re-derives a stretched axis only when its OWN anchor data changes, and an
	// anchor SETTER resolves the parent's span at write time -- which for a stretching parent is its
	// SizeDelta (zero) rather than its arranged size. A ratio anchor here would put the line under
	// the tab on full-layout frames and at zero width on all the others: the progress bar's fill
	// flickered exactly this way, and the dropdown's list opened at zero width for the same reason.
	// Feeding numbers in leaves nothing for a setter to resolve.
	//
	// The frame is the strip's centre, because that is the frame a panel writes its children into
	// (ApplyChildRect collapses anchors to 0.5 and offsets from the panel's centre), and the tabs are
	// the strip's children too. The pivot term converts the tab's stored position -- which is its
	// pivot's -- into its centre, so a tab whose pivot someone moved still gets its line centred.
	//
	// The flip side of reading live numbers is that they are read WHEN THIS RUNS, and nothing calls
	// back when the row is re-arranged underneath. Every path that can move the line already ends
	// here -- a selection change, a label change, a style change -- and a caller that reshapes the
	// strip some other way re-pushes with ApplyStyle(), which is BlueprintCallable for exactly this
	// kind of "I edited the look in place" moment.
	const double TabWidth = Tab->GetWidth();
	const double StripHeight = StripNode != nullptr ? StripNode->GetHeight() : 0.0;
	const double CentreX = Tab->GetAnchoredPosition().X + TabWidth * (0.5 - Tab->GetPivot().X);

	IndicatorNode->SetPivot(FVector2D(0.5, 0.5));
	IndicatorNode->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.5, 0.5), FVector2D(0.5, 0.5), false, false);
	IndicatorNode->SetAnchoredPositionAndSizeDelta(
		// Sitting ON the strip's bottom edge: half a thickness up from it, because the rect is
		// measured from its own centre.
		FVector2D(CentreX, -StripHeight * 0.5 + InActive.IndicatorThickness * 0.5),
		FVector2D(TabWidth, InActive.IndicatorThickness));
}

void UDreamTabView::HandleTabValueChanged(bool bInIsOn)
{
	if (!bInIsOn)
	{
		// A tab going off is the group making room, and it always arrives before the one going on.
		// Acting on it would move the view to whatever was left and then move it again.
		return;
	}
	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		if (Tabs[Index].Toggle != nullptr && Tabs[Index].Toggle->GetValue())
		{
			// THE user road, and the only one: a toggle only speaks when something toggled it, which
			// is a click or the group answering one. Marked as such so focus may follow -- an
			// authored index or a binding pushing a value in reaches SetActiveTabIndex without this
			// flag and must not steal focus from wherever the player actually is.
			TGuardValue<bool> UserChange(bTabChangeFromUser, true);
			// Re-entrant only in the harmless direction: SetActiveTabIndex pushes the same value back
			// into this same toggle, and UUIToggle::SetValue early-outs when nothing changed.
			SetActiveTabIndex(Index);
			return;
		}
	}
}

bool UDreamTabView::IsTabEnabled(int32 InIndex) const
{
	// A missing entry is ENABLED, which is what makes the empty default disable nothing and a short
	// list an ordinary state rather than an error.
	return !TabEnabled.IsValidIndex(InIndex) || TabEnabled[InIndex];
}

void UDreamTabView::SetTabEnabled(int32 InIndex, bool bInEnabled)
{
	if (InIndex < 0)
	{
		return;
	}
	if (!TabEnabled.IsValidIndex(InIndex))
	{
		if (bInEnabled)
		{
			// Already the answer a missing entry gives. Growing the array to store it would be a
			// write that changes nothing.
			return;
		}
		// Grown with the default that a missing entry already meant, so the tabs in between keep
		// answering exactly as they did.
		TabEnabled.SetNum(InIndex + 1);
		for (int32 Fill = 0; Fill < TabEnabled.Num(); ++Fill)
		{
			TabEnabled[Fill] = true;
		}
	}
	if (TabEnabled[InIndex] == bInEnabled)
	{
		return;
	}
	TabEnabled[InIndex] = bInEnabled;
	// The flag is pushed onto the toggles in the style loop, which is also where the disabled colour
	// comes from -- so this is a restyle and never a rebuild.
	ApplyStyle();
}

void UDreamTabView::CloseTab(int32 InIndex)
{
	if (!Tabs.IsValidIndex(InIndex))
	{
		return;
	}
	// BEFORE anything is destroyed, so a consumer that wants to keep the page can take it out of the
	// switcher from the handler -- the order UDreamDialog::Close broadcasts in, and its reason.
	OnTabClosed.Broadcast(InIndex);

	if (UDreamWidget* Page = GetPage(InIndex))
	{
		// Still ours after the broadcast? A handler that re-parented it away is honoured by asking
		// again rather than by remembering the answer from before the broadcast.
		if (PageHostNode != nullptr && Page->GetParent() == PageHostNode)
		{
			Page->DestroyWidget();
		}
	}
	if (TabLabels.IsValidIndex(InIndex))
	{
		TabLabels.RemoveAt(InIndex);
	}
	if (TabEnabled.IsValidIndex(InIndex))
	{
		TabEnabled.RemoveAt(InIndex);
	}
	// The browser's rule: closing a tab BEFORE the open one shifts the index down so the same page
	// stays open; closing the open one itself leaves the index where it is, which is now its right
	// neighbour -- and SanitizeTabIndex pulls it back when the closed tab was the last.
	if (InIndex < ActiveTabIndex)
	{
		--ActiveTabIndex;
	}
	RebuildTabs();
}

void UDreamTabView::MoveTab(int32 InFromIndex, int32 InToIndex)
{
	if (InFromIndex == InToIndex || !Tabs.IsValidIndex(InFromIndex) || !Tabs.IsValidIndex(InToIndex))
	{
		return;
	}
	// The PAGE moves with its tab, because a tab IS its page's handle: the switcher resolves by
	// index, so a strip reordered without its pages would put every caption over the wrong content.
	//
	// EVERY page is renumbered rather than just the moved one: a sibling index is a number each child
	// carries, so writing one child's without touching the rest would leave two pages claiming the
	// same place and the lazy sort free to pick either. RestoreSiblingIndex is the door -- it writes
	// the number and raises the parent's sort flag, which is precisely what a reorder is.
	if (PageHostNode != nullptr)
	{
		TArray<UDreamWidget*> Pages = PageHostNode->GetChildren();
		if (Pages.IsValidIndex(InFromIndex) && Pages.IsValidIndex(InToIndex))
		{
			UDreamWidget* Moved = Pages[InFromIndex];
			Pages.RemoveAt(InFromIndex);
			Pages.Insert(Moved, InToIndex);
			for (int32 Place = 0; Place < Pages.Num(); ++Place)
			{
				if (IsValid(Pages[Place]))
				{
					Pages[Place]->RestoreSiblingIndex(Place);
				}
			}
		}
	}
	auto MoveEntry = [InFromIndex, InToIndex](auto& InArray)
	{
		if (InArray.IsValidIndex(InFromIndex) && InArray.IsValidIndex(InToIndex))
		{
			auto Moved = InArray[InFromIndex];
			InArray.RemoveAt(InFromIndex);
			InArray.Insert(Moved, InToIndex);
		}
	};
	MoveEntry(TabLabels);
	MoveEntry(TabEnabled);

	// The open PAGE stays open wherever it went, which is the only reading of a reorder that does not
	// surprise: dragging a tab must not switch tabs.
	if (ActiveTabIndex == InFromIndex)
	{
		ActiveTabIndex = InToIndex;
	}
	else if (InFromIndex < ActiveTabIndex && InToIndex >= ActiveTabIndex)
	{
		--ActiveTabIndex;
	}
	else if (InFromIndex > ActiveTabIndex && InToIndex <= ActiveTabIndex)
	{
		++ActiveTabIndex;
	}
	OnTabReordered.Broadcast(InFromIndex, InToIndex);
	RebuildTabs();
}

int32 UDreamTabView::TabIndexAtPointer(const UDreamPointerEventData* InEventData) const
{
	if (InEventData == nullptr)
	{
		return INDEX_NONE;
	}
	for (int32 Index = 0; Index < Tabs.Num(); ++Index)
	{
		const UDreamWidget* TabNode = Tabs[Index].TabNode.Get();
		if (!IsValid(TabNode))
		{
			continue;
		}
		// The pointer in the TAB's own space, against the tab's own rect -- the same reading every
		// hit test in this library makes, and the only one that survives a rotated strip.
		const FVector Local = TabNode->GetWorldTransform().InverseTransformPosition(
			InEventData->GetWorldPointInPlane());
		if (Local.Y >= TabNode->GetLocalSpaceLeft() && Local.Y <= TabNode->GetLocalSpaceRight()
			&& Local.Z >= TabNode->GetLocalSpaceBottom() && Local.Z <= TabNode->GetLocalSpaceTop())
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool UDreamTabView::NativeOnBeginDrag(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::NativeOnBeginDrag(EventData);
	if (!bTabsDraggable || EventData == nullptr
		|| EventData->InputType != EDreamUIPointerInputType::Pointer)
	{
		return bBubble;
	}
	// Which tab the drag PICKED UP. Answered from the pointer rather than from the press target,
	// because the press landed on whichever of the tab's children was on top (the label, the plate)
	// and the tab is what moves.
	DraggingTabIndex = TabIndexAtPointer(EventData);
	return bBubble;
}

bool UDreamTabView::NativeOnDrag(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::NativeOnDrag(EventData);
	if (DraggingTabIndex == INDEX_NONE)
	{
		return bBubble;
	}
	const int32 Over = TabIndexAtPointer(EventData);
	if (Over != INDEX_NONE && Over != DraggingTabIndex)
	{
		// LIVE, one neighbour at a time: the tab under the pointer changes place with the dragged one
		// the moment it is passed, which is what every browser does and what makes the gesture
		// readable without a ghost widget following the cursor. MoveTab rebuilds the strip, so the
		// dragged tab's new index is the one the pointer is now over.
		MoveTab(DraggingTabIndex, Over);
		DraggingTabIndex = Over;
	}
	return bBubble;
}

bool UDreamTabView::NativeOnEndDrag(UDreamPointerEventData* EventData)
{
	const bool bBubble = Super::NativeOnEndDrag(EventData);
	DraggingTabIndex = INDEX_NONE;
	return bBubble;
}

void UDreamTabView::HandleCloseClicked(int32 InIndex)
{
	if (!bTabsClosable)
	{
		// The offer can be withdrawn between the binding and the click; the click is the last place
		// that can still honour it.
		return;
	}
	CloseTab(InIndex);
}

void UDreamTabView::FocusActivePage()
{
	UDreamWidget* Page = GetActivePage();
	if (!IsValid(Page) || GetWorld() == nullptr)
	{
		// No world means no event system -- an initialize-time switch, or a headless test.
		return;
	}
	UUISelectable* First = UUISelectable::FindDefaultSelectableIn(this, Page);
	if (First == nullptr || First->GetWidget() == nullptr)
	{
		// A page with nothing navigable in it (a wall of text) keeps focus where it is rather than
		// dropping it somewhere arbitrary.
		return;
	}
	if (UDreamEventSystem* Events = UDreamEventSystem::GetDreamEventSystemInstance(this, 0))
	{
		Events->SetSelectComponentWithDefault(First->GetWidget());
	}
}

#if WITH_EDITOR
void UDreamTabView::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// A REBUILD is for the two things the style push cannot reach: a strip of the wrong LENGTH, and
	// a tab template whose class changed (the template is instanced once, into the tab, at build).
	// It used to run on every edit, and that is the ~40ms-a-keystroke cost the list was rewritten to
	// stop paying: destroying and re-creating widgets dirties the outliner and the designer
	// force-refreshes its details view on top of it. It also threw away anything a consumer had hung
	// on a generated tab from OnTabGenerated, on every frame of a slider drag.
	const FName MemberName = PropertyChangedEvent.GetMemberPropertyName();
	const bool bTemplateChanged = (MemberName == GET_MEMBER_NAME_CHECKED(UDreamTabView, TabTemplateClass));
	if (bTemplateChanged || GetTabCount() != Tabs.Num())
	{
		// The GRANDPARENT's, deliberately: UDreamUIControl::PostEditChangeProperty ends in an
		// ApplyStyle, and RebuildTabs ends in one of its own -- so taking the ordinary road here
		// pushed the whole style twice for one edit, once against the strip that is about to be
		// destroyed. Skipping the base's push and letting the rebuild's be the edit's only one is the
		// difference between two full style walks and one; everything else the base does on an edit
		// (the transaction, the property notification) still happens.
		UDreamUserWidget::PostEditChangeProperty(PropertyChangedEvent);
		RebuildTabs();
		return;
	}
	// The base re-applies the style, which re-pushes every tab's label, padding, colours and the
	// indicator -- so the great majority of edits need nothing else at all.
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "TabView", UDreamTabView)
