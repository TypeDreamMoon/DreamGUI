// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "Event/DreamPointerEventData.h"
#include "Event/Interface/DreamNavigationInterface.h"
#include "DreamWidgetNavigation.generated.h"

class UDreamWidget;
class UDreamUIBehaviour;

/**
 * What one direction does, per widget. The same five answers UMG's EUINavigationRule gives, and for
 * the same reasons -- this framework already copies UMG's class model, so navigation authoring that
 * looked different would be a second thing to learn for no gain.
 */
UENUM(BlueprintType, Category = DreamGUI)
enum class EDreamUINavigationRule : uint8
{
	/** The default: let the framework's directional scan answer, including out of this area. */
	Escape,
	/** The edge. Focus stays where it is. */
	Stop,
	/** Come back in on the far side of the surrounding navigation area. */
	Wrap,
	/** Go exactly here, whatever the geometry says. */
	Explicit,
	/** Ask the delegate. For lists and grids, where the answer is a computation and not a link. */
	Custom,
	/**
	 * Run the ordinary scan, and ask the delegate only when the move would LEAVE the surrounding
	 * navigation area -- UMG's CustomBoundary.
	 *
	 * The difference from Custom is which moves the delegate hears about. Custom is asked for every
	 * move in that direction, so a list's own row-to-row stepping has to be written into it as well.
	 * CustomBoundary is asked only at the edge, which is the interesting case and usually the only
	 * one an author has an answer for: "when Down runs off the bottom of this page, go to the next
	 * page" leaves the scan to do the rows.
	 *
	 * Last in the enum on purpose: the values above it are serialized in existing assets.
	 */
	CustomBoundary,
};

/** Returns the widget focus should move to, or null for "nowhere". */
DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(UDreamWidget*, FDreamCustomWidgetNavigationDelegate, EDreamUINavigationDirection, Direction);

/** One direction's rule and whatever that rule needs to answer. Mirrors UMG's FWidgetNavigationData. */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamWidgetNavigationData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	EDreamUINavigationRule Rule = EDreamUINavigationRule::Escape;

	/**
	 * For Explicit: the display name of the widget to focus, resolved inside this widget's own tree.
	 *
	 * A NAME as well as a pointer because a text-authored .dui can only give a property an asset path,
	 * never a sibling in the live tree -- the same limitation UISelectable's transition target ran
	 * into. The pointer wins when both are set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation", meta = (EditCondition = "Rule == EDreamUINavigationRule::Explicit", EditConditionHides))
	FName WidgetToFocus;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation", meta = (EditCondition = "Rule == EDreamUINavigationRule::Explicit", EditConditionHides))
	TWeakObjectPtr<UDreamWidget> Widget;

	/** For Custom. Not editable: a delegate is bound from code or a Blueprint graph, never typed in. */
	UPROPERTY()
	FDreamCustomWidgetNavigationDelegate CustomDelegate;
};

/**
 * Per-widget navigation rules -- the panel UMG puts on every UWidget and this framework had nowhere.
 *
 * Before this, every navigation rule lived on UUISelectable: a widget without one could hold focus
 * (UDreamWidget::SetFocus and bIsFocusable have always existed) and then could not be navigated away
 * from, to, or past, because the pipeline resolves a move by walking up from the focused widget
 * looking for a component that implements IDreamNavigationInterface and only UUISelectable did. A
 * border, a panel, a custom image with a click handler -- anything that is focusable without being a
 * button -- was outside navigation entirely.
 *
 * This is that component, carrying nothing but the rules, so any widget can join by having one. Put
 * it on a widget that already has a UUISelectable too: a rule set here outranks the selectable's own
 * per-direction mode, which keeps "which component answers first" from being a question anyone has
 * to care about.
 *
 * Reachable from Blueprint and from the details panel; UDreamWidget::GetOrCreateNavigation is the
 * one-liner for code that wants to set a rule on a widget that has no navigation yet.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent), DisplayName = "DreamUI Navigation")
class DREAMGUI_API UDreamWidgetNavigation : public UDreamUIBehaviour, public IDreamNavigationInterface
{
	GENERATED_BODY()

public:
	UDreamWidgetNavigation();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	FDreamWidgetNavigationData Up;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	FDreamWidgetNavigationData Down;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	FDreamWidgetNavigationData Left;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	FDreamWidgetNavigationData Right;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	FDreamWidgetNavigationData Next;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	FDreamWidgetNavigationData Previous;

	/**
	 * Can navigation land here? Independent of the rules above, which are about leaving.
	 *
	 * Defaults to true, and is and-ed with the widget being focusable, visible and interactable --
	 * the questions UUISelectable asks of itself, so a navigation-only widget and a button are judged
	 * alike. A widget whose bIsFocusable was turned off is no landing spot for either: SetFocus refuses it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DreamGUI-Navigation")
	bool bCanNavigateHere = true;

	/** The rule block for one direction. None answers the Up block, which no caller acts on. */
	FDreamWidgetNavigationData& GetNavigationData(EDreamUINavigationDirection InDirection);
	const FDreamWidgetNavigationData& GetNavigationData(EDreamUINavigationDirection InDirection) const;

	/**
	 * True when the author said something about InDirection -- anything but the Escape default.
	 *
	 * This is what makes co-existing with UUISelectable unambiguous: an untouched block means "I have
	 * no opinion", and the selectable's own mode is left to answer.
	 */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Navigation")
	bool HasRuleFor(EDreamUINavigationDirection InDirection) const;

	/** True when any of the six directions was given a rule. What decides who answers a move. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI-Navigation")
	bool HasAnyRule() const;

	/**
	 * The CustomBoundary answer for a move the scan could not place, or null.
	 *
	 * Public because the scan that discovers the boundary is DreamUINavigationScan's, not this
	 * component's, and the one caller that knows a move ran off the edge has to be able to ask.
	 */
	UDreamWidget* AskBoundaryDelegate(EDreamUINavigationDirection InDirection);

	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetRule(EDreamUINavigationDirection InDirection, EDreamUINavigationRule InRule);
	/**
	 * The same rule in all six directions at once -- UMG's SetAllNavigationRules.
	 *
	 * InWidgetToFocus is only meaningful for Explicit, where it names a widget inside this widget's
	 * own tree; it is written for every direction regardless, exactly as UMG does, so that switching
	 * the rule to Explicit afterwards finds the name already there.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetAllNavigationRules(EDreamUINavigationRule InRule, FName InWidgetToFocus);
	/**
	 * Bind a delegate that is asked only at the edge -- UMG's SetNavigationRuleCustomBoundary.
	 *
	 * Same delegate slot as SetCustomDelegate; the rule is what decides whether it is asked for every
	 * move or only for the one that leaves the area. An unbound delegate clears back to Escape, as
	 * the plain custom setter does, so "unbind" never leaves a rule pointing at nothing.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetNavigationRuleCustomBoundary(EDreamUINavigationDirection InDirection,
		const FDreamCustomWidgetNavigationDelegate& InDelegate);
	/** Sets the rule to Explicit and points it at InTarget. Null clears back to Escape. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetExplicitTarget(EDreamUINavigationDirection InDirection, UDreamWidget* InTarget);
	/** Sets the rule to Custom and binds InDelegate. */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI-Navigation")
	void SetCustomDelegate(EDreamUINavigationDirection InDirection, const FDreamCustomWidgetNavigationDelegate& InDelegate);

	/**
	 * Where InDirection leads, following this widget's rules only.
	 * @param bOutHandled true when a rule answered at all; false means "no opinion, ask the scan".
	 */
	UDreamWidget* ResolveTarget(EDreamUINavigationDirection InDirection, bool& bOutHandled);

	virtual bool CanNavigateHere_Implementation() const override;
	/**
	 * A direction with no rule here belongs to the widget's other navigation component when it has one (a selectable's own
	 * per-direction mode), the way UUISelectable::FindNavigableOn reads the two from the other side. Otherwise Stop,
	 * Explicit and Custom answer as their names say, and Escape, Wrap and CustomBoundary run the scan -- kept inside the
	 * open popup or confining screen that holds the widget, else its root canvas (DreamUINavigationScan::FindScanParent),
	 * and inside its navigation area, whose boundary rule decides at the area's edge. A Wrap rule wraps where nothing else
	 * asked for it, Next and Prev included; CustomBoundary's delegate is asked when nothing is left that way.
	 *
	 * Next and Prev here are the legacy sequence (right then down, left then up). The hierarchy's Tab order is the input
	 * system's, which reads this component's Next and Previous rules itself (FDreamUITabOrder).
	 */
	virtual bool OnNavigate_Implementation(EDreamUINavigationDirection InDirection, TScriptInterface<IDreamNavigationInterface>& OutResult) override;

	/** Every registered navigation component, so the directional scan can consider them as targets. */
	static const TArray<TWeakObjectPtr<UDreamWidgetNavigation>>& GetAllNavigationComponents();

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
};

/**
 * The directional scan, shared by everything that navigates.
 *
 * It used to live inside UUISelectable and could only see other UUISelectables, which is what made
 * the selectable component the price of entry to navigation. The scoring is unchanged -- it is the
 * same "nearest thing in front of me, weighted by how directly in front" it always was -- but the
 * candidate list is now every navigable behaviour, of which UUISelectable is one kind.
 */
namespace DreamUINavigationScan
{
	/**
	 * The behaviour on InWidget that should receive a navigation move, or null when it has none.
	 *
	 * A UDreamWidgetNavigation carrying an actual rule wins over a UUISelectable on the same widget,
	 * so authoring a rule is enough and the order components happen to sit in never decides anything.
	 * Between other components that take navigation (a selectable, a user widget's event bridge), the
	 * first one that can be navigated to now wins, then the first one: component order alone used to
	 * hand a move to a bridge that refuses it while a working selectable sat beside it.
	 */
	DREAMGUI_API UDreamUIBehaviour* FindNavigationBehaviour(UDreamWidget* InWidget);

	/** Can a move land on InBehaviour? Asks the interface, so a custom implementor gets a say. */
	DREAMGUI_API bool CanNavigateTo(UDreamUIBehaviour* InBehaviour);

	/**
	 * What keeps navigation from a widget inside it -- the deepest open popup holding it, else its player's top screen
	 * when that confines navigation -- as the input system's navigation stack answers it
	 * (UDreamUINavigationStack::FindConfiningWidgetFor). The stack registers its answer here when it starts, since the core
	 * cannot reach it otherwise; a plain function, as the stack's module outlives every navigation step.
	 */
	using FConfiningWidgetResolver = UDreamWidget* (*)(const UDreamWidget* InWidget);
	DREAMGUI_API void SetConfiningWidgetResolver(FConfiningWidgetResolver InResolver);
	/**
	 * The popup or the screen navigation from InWidget is kept inside: the registered resolver's answer, else -- with no
	 * input system -- the open popup holding InWidget (UDreamUIPopupLayer::FindPopupContaining). Null when navigation is free.
	 */
	DREAMGUI_API UDreamWidget* FindConfiningWidget(const UDreamWidget* InWidget);
	/**
	 * Where a directional scan from InWidget may land: inside FindConfiningWidget's answer, else, for screen-space and
	 * render-target UI, inside its root canvas's widget, so a move never crosses onto another canvas -- another player's
	 * screen among them. Null for world-space UI, whose scan keeps to world-space candidates, and for a widget no canvas
	 * renders yet, which scans unlimited as before.
	 */
	DREAMGUI_API UDreamWidget* FindScanParent(const UDreamWidget* InWidget);

	/**
	 * The nearest navigable behaviour in InDirection (a world-space direction), restricted to
	 * descendants of InParent and of InRestrictNode when those are given.
	 *
	 * @return InSelf when there is nothing that way, which is the convention every caller of the old
	 *         UUISelectable directional scan already reads as "the edge".
	 */
	DREAMGUI_API UDreamUIBehaviour* ScanDirectional(UDreamUIBehaviour* InSelf, const FVector& InDirection,
		UDreamWidget* InParent, const UDreamWidget* InRestrictNode);

	/**
	 * ScanDirectional inside InParent and InRestrictNode, and when nothing is that way InRestrictNode's boundary rule
	 * decides: Stop holds (InSelf), Wrap comes back in on the far side (ScanWrap), Escape runs the move again one
	 * navigation area out, at most eight areas. What a selectable and a navigation-only widget both scan with, so the two
	 * stop, wrap and escape alike. InEscapeDepth is the number of areas already climbed out of.
	 */
	DREAMGUI_API UDreamUIBehaviour* ScanWithinArea(UDreamUIBehaviour* InSelf, const FVector& InDirection,
		UDreamWidget* InParent, const UDreamWidget* InRestrictNode, int32 InEscapeDepth = 0);

	/**
	 * The world-space direction a navigation direction means for InWidget, or zero for the two that
	 * have none: Next and Prev are sequences (right-then-down, left-then-up), not directions.
	 */
	DREAMGUI_API FVector GetWorldDirection(const UDreamWidget* InWidget, EDreamUINavigationDirection InDirection);

	/**
	 * The far side along InDirection: run the same scan BACKWARDS until it stops. Wrapping then lands
	 * exactly where holding the opposite direction would have left the player, which no standalone
	 * "pick the furthest one" scoring can promise.
	 */
	DREAMGUI_API UDreamUIBehaviour* ScanWrap(UDreamUIBehaviour* InSelf, const FVector& InDirection,
		UDreamWidget* InParent, const UDreamWidget* InRestrictNode);

	/**
	 * Next and Prev as the legacy order has them (UDreamGUISettings::TabOrder LegacyGeometric): right then down, left then
	 * up, inside InParent and InRestrictNode. Returns InSelf when neither hop moves.
	 */
	DREAMGUI_API UDreamUIBehaviour* ScanSequential(UDreamUIBehaviour* InSelf, EDreamUINavigationDirection InDirection,
		UDreamWidget* InParent, const UDreamWidget* InRestrictNode);
	/** ScanSequential kept inside FindScanParent's answer for InSelf's widget and that widget's navigation area. */
	DREAMGUI_API UDreamUIBehaviour* ScanSequential(UDreamUIBehaviour* InSelf, EDreamUINavigationDirection InDirection);
}
