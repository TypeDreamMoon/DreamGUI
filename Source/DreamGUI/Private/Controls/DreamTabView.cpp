// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Controls/DreamTabView.h"

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
						})),
			StripNode);

		FDreamTabViewTab& Entry = Tabs.AddDefaulted_GetRef();
		Entry.TabNode = TabRoot;
		Entry.SelectedNode = SelectedPlate;
		Entry.LabelNode = Label;
		Entry.Toggle = Toggle;

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

		if (StripNode->HasRegistered() && IsValid(TabRoot) && !TabRoot->HasRegistered())
		{
			RegisterDreamWidgetHierarchy(TabRoot);
		}
	}

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
		if (Tab.Toggle != nullptr)
		{
			// A selectable left without explicit colours ships white -- these are never optional.
			Tab.Toggle->SetNormalColor(Active.TabNormal);
			Tab.Toggle->SetHoveredColor(Active.TabHovered);
			Tab.Toggle->SetPressedColor(Active.TabPressed);
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

void UDreamTabView::SetActiveTabIndex(int32 InIndex)
{
	const int32 Sanitized = FMath::Max(0, InIndex);
	const bool bChanged = ActiveTabIndex != Sanitized;
	ActiveTabIndex = Sanitized;
	ApplyActiveTab();
	if (bChanged)
	{
		OnTabChanged.Broadcast(Sanitized), OnValueChangedBP.Broadcast(Sanitized);
	}
}

void UDreamTabView::SetActiveTabIndexWithoutNotify(int32 InIndex)
{
	ActiveTabIndex = FMath::Max(0, InIndex);
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
			// Re-entrant only in the harmless direction: SetActiveTabIndex pushes the same value back
			// into this same toggle, and UUIToggle::SetValue early-outs when nothing changed.
			SetActiveTabIndex(Index);
			return;
		}
	}
}

#if WITH_EDITOR
void UDreamTabView::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// The base re-applies the style; the label list lives OUTSIDE ApplyStyle, so without this an
	// edit that adds or removes a caption is silently nothing until the next initialize. Rebuilding
	// unconditionally is fine -- the tabs are generated either way, and RebuildTabs ends in the
	// ApplyStyle the base just ran.
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RebuildTabs();
}
#endif

// The tag this class answers to in .dui.
DECLARE_DREAM_GUI_WIDGET("Native", "TabView", UDreamTabView)
