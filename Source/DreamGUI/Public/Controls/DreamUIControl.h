// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUserWidget.h"
#include "Controls/DreamControlStyles.h"
#include "Controls/DreamUIStyleSheet.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
// SkinFace casts to it, so the definition is this header's to provide: with only a forward
// declaration the header compiles or not depending on which unity blob it lands in.
#include "Core/DreamUISpriteData_BaseObject.h"
#include "Engine/Texture.h"
#include "DreamUIControl.generated.h"

/**
 * One part of a control: the name its tree gives the widget, and the field that holds it.
 *
 * Not a UPROPERTY and not serialized -- it is a description of where a pointer comes from, handed
 * to the binder once per initialize and thrown away.
 */
struct FDreamControlPart
{
	FDreamControlPart(FName InName, TObjectPtr<UDreamWidget>& InField, bool bInRequired = true)
		: Name(InName)
		, Field(&InField)
		, bRequired(bInRequired)
	{
	}

	/** The widget's display name, in this control's tree and in any template that replaces it. */
	FName Name;

	/** Where the bound widget goes. The control's own member, so its lifetime is the control's. */
	TObjectPtr<UDreamWidget>* Field;

	/**
	 * Whether a tree without it can still work as this control. A missing REQUIRED part is reported
	 * by name; an optional one is simply absent, and every writer to it already null-checks.
	 */
	bool bRequired;
};

/**
 * A control whose hierarchy is code, not an asset.
 *
 * WHAT EVERY ONE OF THEM SHARES is not the tree but the four steps that produce one. This class owns
 * NativeOnInitialized and runs them in order:
 *
 *     1. a tree           -- RealizeTemplate(), or RealizeBuiltIn() when nothing replaces it
 *     2. the parts        -- BindParts(), by NAME, from the one list CollectParts declares
 *     3. the behaviours   -- WireParts(), on whichever nodes step 2 found
 *     4. the look         -- ApplyStyle(), and again whenever a knob changes
 *
 * Splitting 1 from 2 is what makes Template possible. A control used to reach its parts through the
 * builder's .Out(), which writes a pointer at construction and therefore only ever works for a tree
 * this code wrote; binding by name afterwards works for any tree with the right names in it, so the
 * two roads differ in step 1 alone. That is also why CollectParts is ONE list rather than a name
 * lookup beside a pointer list: two lists of the same names is how a template silently stops driving
 * a part the code tree still names.
 *
 * Splitting 3 out is what keeps "a control always carries its own behaviour" true for a templated
 * one -- the argument DreamButton was written for. A template's author draws a face; WireParts is
 * what puts the UUIButton on it.
 *
 * THE STYLE CONTRACT is the other half: where the look comes from (the project sheet by default,
 * this instance on request), which named variant, and the obligation to re-push every knob when one
 * changes, because nothing re-derives from a property the way instancing a changed template would.
 * That last part is UMG's SynchronizeProperties, and it is the tax the whole family pays.
 *
 * The concrete style struct stays on the derived class, typed; a control resolves it as
 *
 *     const FDreamToggleStyle& S = ResolveStyle(Style, &UDreamUIStyleSheet::ToggleStyle);
 *
 * which reads the sheet when StyleSource says to and this instance's Style otherwise.
 */
UCLASS(Abstract)
class DREAMGUI_API UDreamUIControl : public UDreamUserWidget
{
	GENERATED_BODY()

public:
	/** See EDreamUIStyleSource: the sheet is the default because one-place-changes-all is the point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	EDreamUIStyleSource StyleSource = EDreamUIStyleSource::ProjectStyleSheet;

	/** Named entry in the sheet ("Danger", "Compact"); none means the family default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (EditCondition = "StyleSource == EDreamUIStyleSource::ProjectStyleSheet"))
	FName StyleVariant;

	/**
	 * A hierarchy to use INSTEAD of the one this control builds for itself -- WPF's ControlTemplate.
	 *
	 * The bargain is the same one WPF makes: the control keeps the behaviour, the state machine and
	 * the style contract; the template decides what the thing looks like, down to which nodes exist.
	 * Parts are matched BY NAME -- the names in CollectParts, which are the names the built-in tree
	 * already gives them -- so a template is "a widget blueprint with a Face and a Label in it", not
	 * a subclass of anything and not an interface anybody has to implement.
	 *
	 * Its TREE is what gets used, instanced straight into this control, exactly as a class's own
	 * archetype would be. Deliberately not placed as a nested widget: the parts have to be this
	 * control's own to drive, and reaching into another instance to write its widgets is the
	 * cross-asset difference record this codebase spent P4 deleting.
	 *
	 * A required part the template omits is reported once, by name. Silence there is how a control
	 * ends up half-driven -- the missing node is simply never written to, and the symptom is a label
	 * that never changes with nothing anywhere saying why.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Style")
	TSubclassOf<UDreamUserWidget> Template;

	/**
	 * Re-push the resolved style, and every other knob, into the parts. Called for you after the
	 * tree is built and whenever a property changes in the editor; call it yourself after editing
	 * a style in place at runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "Style")
	virtual void ApplyStyle() {}

	/**
	 * The style push is what reads the holes -- DeactivateWhenSlotFilled and anything else that has
	 * to know what a host supplied -- and the FIRST push cannot have seen them: it happens at the
	 * end of NativeOnInitialized, and content arrives after that. So push again once the holes are
	 * real.
	 *
	 * Only for a control that opens holes at all. For every other one this would be a second
	 * identical push, and "harmless duplicate work in the common path" is how a control family gets
	 * slow one honest line at a time.
	 */
	virtual void NativeOnSlotContentAttached() override
	{
		if (GetNativeSlotNames().Num() > 0)
		{
			ApplyStyle();
		}
	}

	/** The widget of that display name among this control's OWN contents, or null. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|Control")
	UDreamWidget* FindPart(FName InName) const;

	/**
	 * Every REQUIRED part this control did not find. Empty is the healthy answer.
	 *
	 * The diagnostic a template's author actually needs -- "which names is my tree missing" -- and
	 * the same question a test can ask of every control at once. That sweep is not hypothetical
	 * hygiene: the first run of it found three controls whose part list named a node the built-in
	 * tree does not have ("List" for a node called "ListRoot", and two more), each of which left a
	 * part permanently null on BOTH roads.
	 */
	UFUNCTION(BlueprintCallable, Category = "DreamGUI|Control")
	TArray<FName> GetUnboundRequiredParts();

protected:
	/**
	 * The four steps, in the one order that works. Not meant to be overridden -- a control says what
	 * it is through the hooks below, and a control that took this over would be the one control the
	 * template road does not reach.
	 */
	virtual void NativeOnInitialized() override;

	/**
	 * The parts this control drives: the name its tree gives each widget, and the field that holds
	 * it. Declared once and walked by both roads.
	 */
	virtual void CollectParts(TArray<FDreamControlPart>& OutParts) {}

	/**
	 * Build the tree this control drives when no template replaces it -- the Realize() call that
	 * used to be the body of NativeOnInitialized. Name the nodes the names CollectParts names.
	 */
	virtual void RealizeBuiltIn() {}

	/**
	 * Hook the behaviours onto the parts, once both roads have bound them.
	 *
	 * On the built-in road this is what used to live in Realize's .Then. On the template road it is
	 * the only thing that can put a UUIButton on a face somebody drew -- so it ADDS what it needs
	 * (EnsureComponent) rather than assuming, which is what keeps "a control always carries its own
	 * behaviour" true for a templated one.
	 */
	virtual void WireParts() {}

	/** Anything a control must do between having its parts and its first style push. */
	virtual void OnPartsReady() {}

	/**
	 * Instance Template's tree into this control. False when there is nothing to instance, which is
	 * the signal to build the code tree instead.
	 *
	 * A tree that ALREADY arrived counts as a template and is left alone -- which is how a Blueprint
	 * subclass of a control templates it for free, and incidentally fixes those: before this, such a
	 * subclass got the Blueprint's tree from Initialize and then the code tree on top of it.
	 */
	bool RealizeTemplate();

	/** Resolve every part CollectParts declares, and name any required one the tree does not have. */
	void BindParts();

	/** InNode's T, added if whoever drew this tree did not put one there. */
	template<class T>
	static T* EnsureComponent(UDreamWidget* InNode)
	{
		if (InNode == nullptr)
		{
			return nullptr;
		}
		T* Existing = InNode->GetComponent<T>();
		return Existing != nullptr ? Existing : InNode->AddComponent<T>();
	}

	/**
	 * Round a face. Every control's face is a procedural rect now -- that is where most of the UMG
	 * feel lives -- and the radius is the one thing all of them push the same way.
	 */
	static void ShapeFace(const UDreamWidget* InNode, float InRadius)
	{
		if (UDreamRectBlock* Rect = InNode != nullptr ? Cast<UDreamRectBlock>(InNode->GetVisual()) : nullptr)
		{
			Rect->SetCornerRadiusUnitMode(EDreamRectBlockUnitMode::Value);
			Rect->SetCornerRadius(FVector4(InRadius, InRadius, InRadius, InRadius));
		}
	}

	/**
	 * Skin a face. The brush becomes the rect's BODY texture, so the silhouette, the border and the
	 * selectable's tint keep working over it; an empty brush is the plain rect again (a null body
	 * texture self-heals to white). Sprite wins over texture -- see the brush struct.
	 */
	static void SkinFace(const UDreamWidget* InNode, const FDreamUIFaceBrush& InBrush)
	{
		if (UDreamRectBlock* Rect = InNode != nullptr ? Cast<UDreamRectBlock>(InNode->GetVisual()) : nullptr)
		{
			UDreamUISpriteData_BaseObject* Sprite = Cast<UDreamUISpriteData_BaseObject>(InBrush.Image);
			Rect->SetBodySpriteTexture(Sprite);
			Rect->SetBodyTexture(Cast<UTexture>(InBrush.Image));
			Rect->SetBodyTextureMode(Sprite != nullptr
				? EDreamRectBlockTextureMode::Sprite
				: EDreamRectBlockTextureMode::Texture);
			Rect->SetBodyTextureScaleMode(InBrush.ScaleMode);
			// The tint multiplies whatever the face shows -- the image, or the plain rect when the
			// brush is empty. White is "no opinion", which keeps every existing look intact.
			Rect->SetBodyColor(InBrush.Tint);
		}
	}

	/**
	 * Authored height for the CONTROL itself; width stays whoever-placed-it's. Syncs the slot's
	 * desired-size snapshot, because the slot's first capture (OnRegister) runs before any style
	 * ever applied -- without the sync an Auto consumer measures the pre-style default (100), not
	 * the style's number. The bare-current-size measure fallback that used to paper over this now
	 * lives at the measure root only (see UDreamPanelLayoutBase::GetDesiredSize).
	 */
	void SizeControlHeight(float InHeight)
	{
		SetHeight(InHeight);
		if (UDreamPanelSlot* Slot = GetPanelSlot())
		{
			Slot->SyncAuthoredDesiredSizeFromWidget();
		}
	}

	/**
	 * Authored size for the CONTROL itself, both axes -- for a control that has no "length comes
	 * from whoever placed it" axis at all. A circle has no long side, so a ring states its own
	 * width the same way everything else states its own height.
	 *
	 * Syncs the slot's desired-size snapshot rather than re-capturing the whole authored rect
	 * (which is what SizeFace does, and what makes it a helper for PARTS): the control's live
	 * anchors may already be holding layout output, and CaptureAuthoredGeometry would enshrine
	 * that as the restore target. Same call, and same reason, as SizeControlHeight.
	 */
	void SizeControl(const FVector2D& InSize)
	{
		SetWidth(static_cast<float>(InSize.X));
		SetHeight(static_cast<float>(InSize.Y));
		if (UDreamPanelSlot* Slot = GetPanelSlot())
		{
			Slot->SyncAuthoredDesiredSizeFromWidget();
		}
	}

	/**
	 * Whether the host put anything in InSlotName.
	 *
	 * Reads the ATTACHMENT rather than the NamedSlotContent binding, because unslotted content --
	 * .dui nesting, a designer drop -- reaches the default slot through AdoptUnslottedChildren and
	 * never appears in that map. What is hanging in the hole is the question every caller here
	 * actually has.
	 *
	 * Which makes a slot node's emptiness load-bearing: it must hold NOTHING but slot content. A
	 * control whose built-in occupant lives in the same node reads as permanently filled -- so the
	 * dialog's message is a SIBLING of its body slot rather than a child of it, and stands down when
	 * that slot fills.
	 */
	bool IsSlotFilled(FName InSlotName) const
	{
		const UDreamWidget* SlotWidget = FindSlotWidget(InSlotName);
		return SlotWidget != nullptr && SlotWidget->GetChildrenCount() > 0;
	}

	/**
	 * The built-in part and the hole are alternatives: exactly one of the two is awake.
	 *
	 * A control's stock label (or message, or header) and the slot that replaces it occupy the same
	 * place -- they are two answers to "what is in here", and leaving both awake means one drawn
	 * over the other, or two Fill siblings splitting a row that only one of them is using. Content
	 * wins: a host that filled the hole said what it wanted there.
	 *
	 * Only where a built-in alternative actually exists. A hole with nothing to replace -- the
	 * expander's content column, the scroll box's stack -- has its own reasons to be awake or not,
	 * and this would overwrite them.
	 *
	 * bInBuiltInWanted is that part's OWN answer, which this narrows rather than replaces. The
	 * dialog's message is already put away when it is empty (an absent sentence must not reserve a
	 * line), and a swap that simply wrote `!filled` would wake it back up -- two rules about one
	 * widget's visibility, with the last writer quietly winning.
	 */
	void SwapBuiltInForSlot(UDreamWidget* InBuiltIn, UDreamWidget* InSlotNode, FName InSlotName,
		bool bInBuiltInWanted = true) const
	{
		const bool bFilled = IsSlotFilled(InSlotName);
		if (InBuiltIn != nullptr)
		{
			InBuiltIn->SetWidgetActive(bInBuiltInWanted && !bFilled);
		}
		if (InSlotNode != nullptr)
		{
			InSlotNode->SetWidgetActive(bFilled);
		}
	}

	/** The brush's drawn size when it states one, the style's size otherwise -- Slate's ImageSize rule. */
	static FVector2D BrushSizeOr(const FDreamUIFaceBrush& InBrush, const FVector2D& InStyleSize)
	{
		return InBrush.ImageSize.IsNearlyZero() ? InStyleSize : InBrush.ImageSize;
	}

	/**
	 * Authored size for a rect-faced part (or the control itself). A rect block states no intrinsic
	 * size, so this is what an Auto slot's desired-size fallback reads -- and the slot SNAPSHOTS it,
	 * so a style edit after the first arrange must re-take the snapshot or the new number is never
	 * read. A consumer that stretches the node still wins, as always.
	 */
	static void SizeFace(UDreamWidget* InNode, const FVector2D& InSize)
	{
		if (InNode != nullptr)
		{
			InNode->SetWidth(static_cast<float>(InSize.X));
			InNode->SetHeight(static_cast<float>(InSize.Y));
			if (UDreamPanelSlot* Slot = InNode->GetPanelSlot())
			{
				Slot->CaptureAuthoredGeometry(true);
			}
		}
	}

	template<class TStyle>
	const TStyle& ResolveStyle(const TStyle& InInlineStyle, const TStyle& (UDreamUIStyleSheet::*InFamily)(FName) const) const
	{
		if (StyleSource == EDreamUIStyleSource::ProjectStyleSheet)
		{
			if (const UDreamUIStyleSheet* Sheet = UDreamUIStyleSheet::GetProjectSheet())
			{
				return (Sheet->*InFamily)(StyleVariant);
			}
		}
		return InInlineStyle;
	}

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override
	{
		Super::PostEditChangeProperty(PropertyChangedEvent);
		ApplyStyle();
	}
#endif
};
