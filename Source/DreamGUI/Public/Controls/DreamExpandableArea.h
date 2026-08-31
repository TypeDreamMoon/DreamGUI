// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Controls/DreamUIControl.h"
#include "DreamExpandableArea.generated.h"

class UDreamWidget;
class UUIButton;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDreamExpandableAreaExpansionChangedEvent, bool, bIsExpanded);

/**
 * A header that toggles, and the content it hides.
 *
 * Three nodes and one behaviour: a column, a clickable header face with an indicator and a label on
 * it, and a content column below. The header is a UUIButton on its own face, exactly as DreamButton
 * builds one -- an expander IS a button whose click means "show the rest" -- so hover, press and
 * navigation come from the library's one selectable rather than from a second interaction story.
 *
 * HOW A CONSUMER PUTS CONTENT IN IT, and which idiom that is, because there are two in this codebase
 * and only one is reachable from here:
 *
 *     Native.ExpandableArea Details {
 *         Label = "Advanced"
 *         Text Body { Text = "..." }
 *     }
 *
 * Nesting, plainly. The text builder attaches a node's children to the widget it just built
 * (BuildNode's TrySetParent), so those widgets arrive as children of THIS control, sitting beside
 * the tree the control realized for itself -- and the content column is this control's DEFAULT
 * NAMED SLOT, so they are moved into it for free.
 *
 * That used to be a hand-written adoption pass here, with a comment explaining that UDreamNamedSlot
 * could not be used: a host's NamedSlotContent was hung by InitializeWidgetStatic, which returns
 * immediately for a class with no widget-tree archetype, and a native control never has one -- so a
 * slot declared here would have been a hole nothing could fill. That gate is gone (slots are now
 * filled from UDreamUserWidget::Initialize, after NativeOnInitialized, which is the first moment
 * BOTH kinds of contents exist), and with it the reason to hand-roll this. Code that assembles a
 * hierarchy by hand can still call SetContent; the designer's drag into this control lands as a
 * child, which is the same road as the .dui above.
 *
 * UMG parity is UExpandableArea's core: bIsExpanded, SetIsExpanded, OnExpansionChanged -- plus the
 * library's OnValueChangedBP, because the expanded flag is a value and `<->` binds against it.
 *
 * Collapsed, the control measures as the HEADER and nothing more: an Auto slot asks the control for
 * its size, and an expander that kept its expanded height would leave a hole in every list it is in.
 */
UCLASS(BlueprintType, Blueprintable, DisplayName = "Dream Expandable Area")
class DREAMGUI_API UDreamExpandableArea : public UDreamUIControl
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own look. The project sheet wins while StyleSource says so AND a sheet
	 * actually exists; with no sheet in the project this IS the look in effect -- which is why
	 * it stays editable instead of being gated on the enum: the old edit condition greyed the
	 * exact values that were driving the control.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area")
	FDreamExpandableAreaStyle Style;

	/** The words on the header. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area")
	FText Label;

	/**
	 * Whether the content shows. A property so .dui, the designer and bindings can see it and so an
	 * author can ship a section already open; every road that CHANGES it goes through SetIsExpanded,
	 * which is the only thing that broadcasts.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Expandable Area")
	bool bIsExpanded = true;

	/** Fired whenever the expanded flag moves, from a click or from code. */
	UPROPERTY(BlueprintAssignable, Category = "Expandable Area")
	FDreamExpandableAreaExpansionChangedEvent OnExpansionChanged;

	/**
	 * The `<->` convention: two-way bindings synthesize their reverse route against this exact
	 * name, so a value control carries it alongside its spoken events. Fires with them.
	 */
	UPROPERTY(BlueprintAssignable, Category = "Expandable Area")
	FDreamExpandableAreaExpansionChangedEvent OnValueChangedBP;

	UFUNCTION(BlueprintCallable, Category = "Expandable Area")
	bool GetIsExpanded() const;

	/** Silent when the flag does not move; otherwise pushes the visuals and broadcasts both events. */
	UFUNCTION(BlueprintCallable, Category = "Expandable Area")
	void SetIsExpanded(bool bInIsExpanded);

	/** What the header click does, callable without a pointer. */
	UFUNCTION(BlueprintCallable, Category = "Expandable Area")
	void ToggleExpansion();

	/**
	 * Put InContent in the content column, taking it from wherever it currently hangs.
	 *
	 * The code-facing half of the nesting idiom in the class comment: a hierarchy built by hand has
	 * no .dui author to nest anything, so it says where the content goes. Null empties the column.
	 */
	UFUNCTION(BlueprintCallable, Category = "Expandable Area")
	void SetContent(UDreamWidget* InContent);

	/** The first widget in the content column, or null while it is empty. */
	UFUNCTION(BlueprintPure, Category = "Expandable Area")
	UDreamWidget* GetContent() const;

	virtual void ApplyStyle() override;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UDreamWidget> RootNode = nullptr;

	/** The clickable face, and the node whose height IS the style's HeaderHeight. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UDreamWidget> HeaderNode = nullptr;

	/** The indicator-and-label row inside the header. See the tree comment for why it is its own node. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UDreamWidget> HeaderRowNode = nullptr;

	/** The built-in glyph indicator. Asleep whenever the state's brush carries an image instead. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UDreamWidget> ArrowNode = nullptr;

	/** The image indicator, the glyph's stand-in. Exactly one of the two is awake. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UDreamWidget> ArrowMarkNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UDreamWidget> LabelNode = nullptr;

	/** Where a consumer's content lives, and the node that is switched off when collapsed. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UDreamWidget> ContentNode = nullptr;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UUIButton> HeaderBehaviour = nullptr;

	/** The header's hole, in the stock label's place. Empty is the normal state. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "Expandable Area")
	TObjectPtr<UDreamWidget> HeaderSlotNode = nullptr;

	/**
	 * Two holes: the body, which nesting fills, and the header, which has to be named. The default
	 * is the body because that is what nesting means here -- a section's content is the thing you
	 * write inside it, and a custom title bar is the deliberate case.
	 */
	virtual TArray<FName> GetNativeSlotNames() const override { return { ContentSlotName, HeaderSlotName }; }
	virtual FName GetDefaultSlotName() const override { return ContentSlotName; }

	/** Named once each: the declaration, the node's display name and the binding key are the same string. */
	static const FName ContentSlotName;
	static const FName HeaderSlotName;

protected:
	virtual void NativeOnInitialized() override;

private:
	void HandleHeaderClicked();

	/**
	 * Everything the expanded flag decides: whether the content is awake, which indicator the header
	 * wears, and the control's own measured height. Pushes without broadcasting -- writing state in
	 * is not the user opening the section, which is the same line UDreamToggle draws.
	 */
	void PushExpansionVisuals();

	/** One re-parent into the content column, with the slot the column hands out captured. */
	void MoveIntoContent(UDreamWidget* InWidget);

	/** How tall the content wants to be, asked of the layout rather than read off an arranged rect. */
	float MeasureContentExtent();
};
