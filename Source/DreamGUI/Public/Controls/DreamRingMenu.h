// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamRingMenu.generated.h"

class UDreamRingSectorRaycast;
class UDreamWidget;
class UUIButton;

/**
 * One entry on the wheel.
 *
 * A struct rather than an FText the way the dropdown's options are, because a ring menu's item is
 * not a line of text -- it is a command. The tag is what a handler switches on; an index is a
 * terrible name for "Reload", and it stops being the right one the moment somebody inserts an item.
 */
USTRUCT(BlueprintType)
struct DREAMGUI_API FDreamRingMenuItem
{
	GENERATED_BODY()

	/** What the wedge says, and what the hub shows while this item is highlighted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Item")
	FText Label;

	/**
	 * The picture over the label. Empty is a label-only item; the label steps aside when the brush
	 * is the only thing set, so an icon-only wheel is a wheel with no labels typed into it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Item")
	FDreamUIFaceBrush Icon;

	/** What this item MEANS, carried into OnItemActivated so a handler never switches on an index. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Item")
	FName Tag;

	/** Off wears the style's disabled colour, stops answering the pointer, and cannot be highlighted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Item")
	bool bEnabled = true;

	/**
	 * How wide this item's slice is, relative to the others. Equal weights is the even wheel; a
	 * weight of 2 takes twice the angle of a weight of 1, which is how a "cancel" quadrant sits
	 * beside six small ones. Zero-or-less is treated as one, and a set that adds to nothing spreads
	 * evenly -- neither is worth an error message when the honest answer is an even ring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Item", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/**
	 * This one wedge's RESTING colour, replacing the style's WedgeNormal. Hover, press and the
	 * selected colour stay the style's, so a red "delete" still highlights like everything else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Item")
	bool bOverrideColor = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Item", meta = (EditCondition = "bOverrideColor"))
	FColor Color = FColor(52, 57, 70, 235);

	/** Whatever the consumer needs back when this item is chosen. The control never reads it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu Item")
	TObjectPtr<UObject> Payload = nullptr;
};

/** What a wedge claims of the pointer. */
UENUM(BlueprintType)
enum class EDreamRingHitArea : uint8
{
	/** Exactly the annulus that is drawn. The hub and everything past the outer edge are dead. */
	Ring,
	/**
	 * The slice, reaching PAST the drawn ring -- the weapon-wheel feel, where a flick of the mouse
	 * short of the ring still picks the item in that direction. How far past is SliceHitRadiusScale,
	 * and that knob is not decoration: nothing culls a widget by its bounds before the trace, so an
	 * unbounded slice really does claim its whole direction across the screen, and the raycast sort
	 * hands a wedge every hit against anything earlier in the hierarchy. A menu that owns the screen
	 * wants exactly that; a menu sharing a panel with other controls makes them unclickable.
	 */
	Slice,
};

/** How an item's icon and label are turned. */
UENUM(BlueprintType)
enum class EDreamRingLabelFacing : uint8
{
	/** Never turned. The readable default, and the only one that stays readable at the bottom. */
	Upright,
	/** Turned with the ring, so text runs along the tangent and its top faces outward. */
	Tangential,
	/** Turned a further quarter, so text reads outward along the radius. */
	Radial,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamRingMenuIndexEvent, int32, Index);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamRingMenuActivatedEvent, int32, Index, FName, Tag);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDreamRingMenuWedgeEvent, int32, Index, UDreamWidget*, Wedge);

/**
 * A ring menu whose hierarchy is code, not an asset: wedges around a hub, picked by direction.
 *
 * The radial menu every action game has and no UI framework ships, in the DreamGUI idiom. Items are
 * a UPROPERTY array, so .dui, the designer, Blueprint and a `<->` binding all drive it with no glue;
 * the look comes from the project sheet like every other control's.
 *
 * THE WEDGE IS A RECT
 * -------------------
 * There is no new visual and no new shader here. UDreamRectBlock already draws a rounded rect with
 * a border, a radial-fill mask, gradients and shadows -- and an annulus sector is exactly that rect
 * with the corner radius at 100%, the body off (so the BORDER is the ring), and the mask set to the
 * slice. UDreamProgressBar's Radial shape found the first half of this; the second half is that the
 * mask's start and sweep are per-item rather than a percentage. So every wedge inherits gradients,
 * inner and outer shadows and soft edges for free, and a project that wants a fancier ring styles
 * the rect rather than waiting for a feature.
 *
 * The cost is honest and worth stating: each wedge is a SQUARE the size of the whole ring, of which
 * the shader keeps one slice. Every wedge therefore overlaps every other, and a rect hit test would
 * hand the topmost one the entire circle -- which is why the hit shape is
 * UDreamRingSectorRaycast and not the default.
 *
 * WHY THE HIT TEST IS A CUSTOM RAYCAST AND NOT A TICK
 * ---------------------------------------------------
 * The obvious build is a control that reads the pointer every frame and works out an angle. This
 * one does not, and the difference is not style: with an exact sector as each wedge's hit shape the
 * whole existing event pipeline just works -- enter, exit, press, click, the selectable's four
 * transitions, event bubbling, the modal layer, the drag threshold. A ticking control would have
 * had to reimplement each of those and would still have been wrong about which of two overlapping
 * menus the pointer was in. The angle maths lives in one place, in the raycast, and the gamepad
 * route (HighlightByDirection) calls the same two static functions rather than a second copy.
 *
 * The gap between wedges is DRAWN ONLY. Were it in the hit shape too, dragging across one would
 * exit a wedge and enter nothing, and the highlight would blink off between every pair of items.
 *
 * HIGHLIGHT AND SELECTION ARE TWO THINGS
 * --------------------------------------
 * Highlight is where the pointer (or the stick) is; selection is what has been committed. A menu
 * separates them -- hover, then click -- and a weapon wheel does not, which is one bool
 * (bSelectOnHighlight) rather than two controls. Both are readable, both fire, and
 * OnItemActivated fires on every commit even when the same item is chosen twice, because
 * "invoke this command again" is a thing a menu has to be able to say.
 *
 *     Native.RingMenu Wheel {
 *         HitArea            = Slice
 *         bSelectOnHighlight = true
 *         Style.SweepAngle   = 360
 *         OnItemActivated -> HandleWheelPick
 *     }
 *
 * Items have no .dui text form yet -- an array of structs is the same language gap the dropdown's
 * Options hit -- so a .dui consumer fills them from its host class, with SetItems.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Ring Menu")
class DREAMGUI_API UDreamRingMenu : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why it
	 * stays editable instead of being gated on the enum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	FDreamRingMenuStyle Style;

	/** The wheel. Order is clockwise from the style's StartAngle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	TArray<FDreamRingMenuItem> Items;

	/** The committed item. -1 is none, which is the resting state of a menu nobody has chosen from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	int32 SelectedIndex = INDEX_NONE;

	/** See EDreamRingHitArea. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	EDreamRingHitArea HitArea = EDreamRingHitArea::Ring;

	/**
	 * How far a Slice reaches, as a multiple of the ring's outer radius. Ignored by Ring.
	 *
	 * Two is one more ring's worth of slack -- comfortably past what is drawn, which is the whole
	 * point of Slice, and still a bounded shape that a neighbour can be clicked outside of. It
	 * scales with the ring rather than being a pixel count, so a bigger wheel reaches further by
	 * itself.
	 *
	 * ZERO IS NO LIMIT, and it is the weapon-wheel setting: the slice claims its direction to the
	 * edge of the screen and beyond. Correct for a menu that owns the screen (put it over a blocker,
	 * the way a modal does) and wrong for anything sharing a panel -- an unbounded wedge wins the
	 * raycast against every widget earlier in the hierarchy, which is most of them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu", meta = (ClampMin = "0.0",
		EditCondition = "HitArea == EDreamRingHitArea::Slice"))
	float SliceHitRadiusScale = 2.0f;

	/**
	 * The radius inside which the POINTER picks nothing, in ring units. Zero (the default) means the
	 * style's InnerRadius, which is the hub's edge and the answer nine times in ten; a Slice menu
	 * that wants a bigger "let go here to cancel" middle than its hub states one here.
	 *
	 * A stick has its own knob below, and deliberately so: the two are different quantities, and one
	 * number serving both would have compared a magnitude of 1 against a radius of 80.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu", meta = (ClampMin = "0.0"))
	float DeadZoneRadius = 0.0f;

	/**
	 * How far a stick must be pushed before HighlightByDirection picks anything, as the vector's
	 * MAGNITUDE -- an axis pair is already normalized, so this is 0 to 1 and has nothing to do with
	 * the ring's radii. What stops a resting stick's jitter from choosing an item every frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float StickDeadZone = 0.25f;

	/** See EDreamRingLabelFacing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	EDreamRingLabelFacing LabelFacing = EDreamRingLabelFacing::Upright;

	/**
	 * Highlighting an item commits it, with no click -- the weapon wheel, where the pointer's
	 * direction IS the choice and releasing the key is the confirm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	bool bSelectOnHighlight = false;

	/** Clicking the item that is already selected clears the selection instead of re-choosing it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	bool bAllowDeselect = false;

	/** Off leaves the wedges to their icons. A wheel of glyphs is an ordinary thing to want. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	bool bShowLabels = true;

	/**
	 * The hub shows the highlighted item's label (falling back to the selected one's, then to
	 * HubText). Off leaves HubText alone, which is how the hub becomes a static title.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	bool bHubFollowsHighlight = true;

	/** What the hub says with nothing highlighted. Empty is an empty hub. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Ring Menu")
	FText HubText;

	/** Where the pointer is. Fires with both routes -- the wedges' hover and HighlightByAngle. */
	UPROPERTY(BlueprintAssignable, Category = "Ring Menu")
	FDreamRingMenuIndexEvent OnHighlightChanged;

	/** The committed item moved. */
	UPROPERTY(BlueprintAssignable, Category = "Ring Menu")
	FDreamRingMenuIndexEvent OnSelectionChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact name,
	 * so a value control carries it alongside its spoken events. Fires with OnSelectionChanged.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Ring Menu")
	FDreamRingMenuIndexEvent OnValueChangedBP;

	/**
	 * An item was CHOSEN -- clicked, or confirmed through ActivateHighlighted. Fires every time,
	 * including when the choice is the item already selected, because a menu entry is a command and
	 * "do it again" has to be sayable. The tag rides along so a handler never switches on an index.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Ring Menu")
	FDreamRingMenuActivatedEvent OnItemActivated;

	/**
	 * One per wedge, every time it is BOUND to an item. The hook for a consumer whose wedges are
	 * richer than an icon over a label but who would rather not author a whole class: everything
	 * under the wedge is reachable from here by display name, and anything added under
	 * WedgeTemplateNode before the first rebuild rides into every copy.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Ring Menu")
	FDreamRingMenuWedgeEvent OnWedgeGenerated;

	/**
	 * Open and Close, each carrying the SELECTION at that moment -- which is the whole of what a
	 * "hold to open, release to choose" input wants back from a close.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Ring Menu")
	FDreamRingMenuIndexEvent OnOpened;

	UPROPERTY(BlueprintAssignable, Category = "Ring Menu")
	FDreamRingMenuIndexEvent OnClosed;

	/** The node everything hangs under, and what Open and Close scale and fade. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ring Menu")
	TObjectPtr<UDreamWidget> RingNode = nullptr;

	/** The unbroken ring behind the wedges, filling the gaps between them. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ring Menu")
	TObjectPtr<UDreamWidget> BackdropNode = nullptr;

	/** What the wedges are children of -- the template among them. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ring Menu")
	TObjectPtr<UDreamWidget> WedgeRootNode = nullptr;

	/**
	 * The thing wedges are copied from -- authored once, inactive, never drawn itself. Public
	 * because it is half of the "hand it a template" story.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ring Menu")
	TObjectPtr<UDreamWidget> WedgeTemplateNode = nullptr;

	/** The disc inside the ring. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ring Menu")
	TObjectPtr<UDreamWidget> HubNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ring Menu")
	TObjectPtr<UDreamWidget> HubLabelNode = nullptr;

	/** The wedge widgets, parallel to Items. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Ring Menu")
	TArray<TObjectPtr<UDreamWidget>> WedgeNodes;

	/** Replace the wheel and rebuild it. */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void SetItems(const TArray<FDreamRingMenuItem>& InItems);

	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	int32 GetItemCount() const { return Items.Num(); }

	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	/** Moves the selection and fires both selection events. Out of range selects nothing. */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void SetSelectedIndex(int32 InIndex);

	/** The same move, silently: for pushing an authored value in, which is not the user choosing. */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void SetSelectedIndexWithoutNotify(int32 InIndex);

	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	int32 GetHighlightedIndex() const { return HighlightedIndex; }

	/** Move the highlight. -1, or a disabled item, clears it. */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void SetHighlightedIndex(int32 InIndex);

	/**
	 * Highlight whichever item owns this direction -- the gamepad route.
	 *
	 * The vector is in the ring's own frame: +X right, +Y up, exactly what a right-stick axis pair
	 * reads. Its LENGTH is honoured against StickDeadZone -- a magnitude, not a radius -- so a stick
	 * barely off centre highlights nothing. Set StickDeadZone to zero to take any direction at all.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void HighlightByDirection(FVector2D InDirection);

	/** Highlight whichever item owns this angle, in degrees clockwise from twelve. */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void HighlightByAngle(float InAngleDegrees);

	/**
	 * Step the highlight round the ring, skipping disabled items. Wraps on a full wheel and stops at
	 * the ends of a partial one -- a half-ring has a first and a last item, and pretending otherwise
	 * is how a keyboard user ends up somewhere off screen.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void StepHighlight(int32 InDelta);

	/** Commit whatever is highlighted: what a "release the button to choose" input calls. */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void ActivateHighlighted();

	/** Where an item's slice begins, in degrees clockwise from twelve. */
	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	float GetItemStartAngle(int32 InIndex) const;

	/** How wide an item's slice is, in degrees -- its share of the style's sweep, by weight. */
	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	float GetItemSweepAngle(int32 InIndex) const;

	/** The middle of an item's slice, which is where its icon and label ride. */
	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	float GetItemMidAngle(int32 InIndex) const;

	/** Which item owns an angle, or -1 when the sweep does not cover it. */
	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	int32 IndexAtAngle(float InAngleDegrees) const;

	/** The wedge standing for an item, or null when there is none. */
	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	UDreamWidget* GetWedgeWidget(int32 InIndex) const;

	/**
	 * Throw the geometry at the wedges again, growing or shrinking the pool first if the item count
	 * moved. Called for you by ApplyStyle -- wedge geometry IS style -- and by SetItems.
	 */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void RebuildItems();

	/** Show the ring, scaling and fading it in when there is a world to tween in. */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void Open();

	/** Hide it, clearing the highlight on the way out. */
	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void Close();

	UFUNCTION(BlueprintCallable, Category = "Ring Menu")
	void ToggleOpen();

	UFUNCTION(BlueprintPure, Category = "Ring Menu")
	bool IsOpen() const { return bOpen; }

	virtual void ApplyStyle() override;

protected:
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) override;
	virtual void RealizeBuiltIn() override;
	virtual void WireParts() override;

private:
	/**
	 * The hit sector's far edge: what the wedge draws to in Ring, the bounded reach in Slice, and
	 * zero (the raycast's "no limit") only where the author asked for it. One function because two
	 * writers -- the bind pass and the highlight pass -- is how the two learn to disagree.
	 */
	float ResolveHitOuterRadius(const FDreamRingMenuStyle& InStyle, float InDrawnReach) const;

	/** Duplicate one wedge out of the template and wire what it keeps for life. */
	UDreamWidget* CreatePoolWedge(int32 InIndex);

	/** Grow or shrink the pool to exactly this many wedges. */
	void ResizePool(int32 InPoolSize);

	/** Point a wedge at an item: slice, radii, colour, icon, label, rotation, hit shape. */
	void BindWedge(int32 InIndex, const FDreamRingMenuStyle& InStyle);

	/** Re-push every wedge's resting colour and label tint. What highlight and selection move. */
	void RefreshWedgeColors();

	/** Push the hub's caption for the current highlight. */
	void RefreshHubText();

	/** Sum of the items' clamped weights, or the item count when they add to nothing. */
	float GetTotalWeight() const;

	void HandleWedgeClicked(int32 InIndex);

	/** The commit: moves the selection (or clears it) and fires OnItemActivated. */
	void ActivateItem(int32 InIndex);

	/** Where the pointer is. Not a UPROPERTY the way SelectedIndex is: it is nobody's to author. */
	UPROPERTY(Transient)
	int32 HighlightedIndex = INDEX_NONE;

	UPROPERTY(Transient)
	bool bOpen = true;
};
