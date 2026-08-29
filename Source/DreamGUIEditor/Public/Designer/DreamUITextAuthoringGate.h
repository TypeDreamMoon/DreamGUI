// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FProperty;
class UDreamWidget;
class UDreamWidgetBlueprint;

/**
 * What the designer is allowed to do to a hierarchy that came out of a `.dui`.
 *
 * The direction is "the text is the only truth and the designer is a graphical front end for it".
 * That buys a lot -- one place to read, one place to diff, one place to review -- and it costs
 * exactly one thing: the designer can no longer do everything it used to. Two kinds of edit stop
 * being possible, for two different reasons:
 *
 *   STRUCTURE, because nothing writes it back. The write-back patcher replaces the text of ONE value
 *   or inserts ONE line; it never inserts a node, removes one, or reorders anything (see
 *   FDreamUITextPatcher's class comment, which says so as a design constraint rather than as a
 *   limitation to be lifted). So a create, a delete, a reparent, a rename or a paste performed here
 *   would live until the next compile and then be gone, because the next compile rebuilds the tree
 *   from the file.
 *
 *   PROPERTIES THE WRITE-BACK WILL NOT CARRY, for five separate reasons that all end the same way.
 *   The details panel shows every UPROPERTY on a UDreamWidget; the language addresses a node, its
 *   panel slot and its behaviours, and the write-back covers a subset even of that. A property is
 *   refused when it is outside the writable set, when its VALUE has no `.dui` spelling (an asset
 *   reference -- a font, a texture, a material), when the file drives it with `<-`, when a loop line
 *   produced N copies of the widget, and -- for the Bind control rather than a value -- when a
 *   binding would be authored from the panel at all.
 *
 * Both failures have the same shape and it is the worst shape an editor can have: the edit appears,
 * works, survives every check the author would think to make, and is silently gone at the next
 * compile. So this exists to turn both into something visible BEFORE the edit -- a refusal that says
 * why, or a row that is drawn disabled.
 *
 * WHY A GATE AND NOT A SECOND STORE. The rejected alternative was to let the designer edit anything
 * and keep the extra values on the asset, merging them back over the generated tree. That is a
 * second source of truth for the same hierarchy, which is the thing this whole direction exists to
 * remove -- and the merge has no answer for the case that matters, where the file and the asset
 * disagree because a human edited both.
 *
 * WHERE THE REFUSALS LIVE, and why not all in one place: the five structural primitives are in
 * DreamWidgetTreeEditing, but PASTE is not one of them -- it is
 * FDreamWidgetBlueprintEditor::DesignerPasteWidgets, which builds its own copies straight onto the
 * tree. Adding a behaviour is a third shape again (DesignerAddComponentBy / DesignerRemoveComponent
 * call NotifyStructureChanged directly), replacing a panel is a fourth (ReplaceSelectedWidgetLayout),
 * and the hierarchy panel's drag-drop is a fifth -- it calls TrySetParent on the widgets itself.
 * Every one of those call sites asks this namespace, and the message is built here so ten refusals
 * cannot drift into ten different sentences.
 *
 * ONE RULE ENFORCED SOMEWHERE ELSE ENTIRELY, named here because the two halves have to agree: the
 * ROTATE and SCALE gizmos are withheld on a text-authored hierarchy, by
 * FDreamWidgetDesignerViewportClient::GetWidgetMode. The reason is the writable set -- the write-back
 * carries `AnchorData.*` and nothing else, so a MOVE survives (it recomputes the anchors, and those
 * are written) while a rotate or a scale writes only RelativeRotation / RelativeScale, which the
 * language has no spelling for. A panel that greyed the rotation while the gizmo went on writing it
 * would be the contradiction this pair exists to avoid.
 */
namespace DreamUITextAuthoring
{
	/**
	 * The `.dui` this Blueprint's class declares, as the author wrote it. Empty for a hand-authored one.
	 *
	 * THE CRITERION, and there is only one: UDreamTextUserWidget::SourceFile on the class default
	 * object, non-empty. Not "does the asset have a tree", not "does the file exist" -- a .dui that is
	 * missing or malformed still means the hierarchy belongs to the text, and unlocking the designer
	 * when the file fails to load would let an author "fix" a broken screen by dragging boxes and lose
	 * the work as soon as the typo was corrected.
	 *
	 * The generated class first and the parent second, mirroring the compiler
	 * (FDreamWidgetBlueprintCompilerContext::BuildWidgetTreeFromTextSource) deliberately and exactly:
	 * the parent answers the two moments the generated class has no CDO to ask -- a Blueprint that has
	 * never been compiled -- and it is how a native subclass that hardcodes its own .dui works at all.
	 * If that rule ever changes, it has to change in both, and the gate disagreeing with the compiler
	 * is how "the designer let me and the compile threw it away" comes back.
	 */
	DREAMGUIEDITOR_API FString GetAuthoredSourcePath(const UDreamWidgetBlueprint* InBlueprint);

	/** The leaf, for messages: "Login.dui". Empty when the Blueprint is hand-authored. */
	DREAMGUIEDITOR_API FString GetAuthoredSourceFileName(const UDreamWidgetBlueprint* InBlueprint);

	/** Whether this Blueprint's hierarchy belongs to a text file rather than to the designer. */
	DREAMGUIEDITOR_API bool IsTextAuthored(const UDreamWidgetBlueprint* InBlueprint);

	/**
	 * Whether this Blueprint's class CAN name a `.dui` -- its class default object is a
	 * UDreamTextUserWidget -- regardless of whether it names one yet.
	 *
	 * A separate question from IsTextAuthored, and the gap between them is a real state: a text class
	 * with an empty path. The gate must NOT lock such a Blueprint (there is no text to be the truth
	 * yet, so refusing structural edits would refuse everything and explain it by naming a file that
	 * does not exist), and the designer MUST offer to set one -- which is the state every text-backed
	 * widget starts in, and the state that had nowhere to be fixed from.
	 */
	DREAMGUIEDITOR_API bool CanAuthorFromText(const UDreamWidgetBlueprint* InBlueprint);

	/**
	 * Point the class at a `.dui`, or clear it. Returns false when the class cannot hold one.
	 *
	 * On the CDO, transacted, and followed by a compile, because all three are what make the value
	 * mean anything: the compiler reads the CDO, undo has to be able to take it back, and until a
	 * compile runs the class still has whatever hierarchy it had before. Setting the property and
	 * stopping is the shape that produces "I set the file and nothing happened".
	 *
	 * The path is stored as given except for being made portable (DreamUIPaths::MakePortablePath), so
	 * a caller may hand this the absolute path a file dialog returned.
	 */
	DREAMGUIEDITOR_API bool SetAuthoredSourcePath(UDreamWidgetBlueprint* InBlueprint, const FString& InPath);

	/**
	 * The Blueprint InWidget belongs to, whichever half of the designer it came from.
	 *
	 * A TEMPLATE widget is outered to the tree, which is outered to the asset, so the outer chain
	 * answers. A PREVIEW widget is not: it was instanced into the designer's world and its outer chain
	 * ends at a UDreamUserWidget with no asset above it, so it is found through the designer that owns
	 * its world. The details panel and every viewport gesture hold previews, which is why a gate that
	 * only walked outers would have been silently inert for exactly the callers that need it.
	 */
	DREAMGUIEDITOR_API UDreamWidgetBlueprint* FindOwningBlueprint(const UDreamWidget* InWidget);

	/**
	 * Say no to a structural edit, out loud, and return true when it was refused.
	 *
	 * `if (RefuseStructuralEdit(BP, __FUNCTION__, __LINE__, TEXT("delete"), Name)) return false;` at
	 * the top of the operation -- BEFORE its own validity checks, so an edit that would also have been
	 * refused for some other reason still reports the reason the author can act on.
	 *
	 * It logs rather than returning text because the callers are not all UI: the same five primitives
	 * are reached from the palette, the hierarchy panel, the viewport, a tool menu and a commandlet,
	 * and only some of those have somewhere to put an FText. Error, not Warning, and it matches what
	 * W5 did to the rest of this family: a structural command that quietly does nothing is
	 * indistinguishable from a broken one, and the automation suite can only assert on a refusal that
	 * was actually said.
	 *
	 * InOperation reads into the sentence as "Refusing to <InOperation>", so it is a verb phrase:
	 * "create a widget", "delete 'OkBtn'".
	 */
	DREAMGUIEDITOR_API bool RefuseStructuralEdit(const UDreamWidgetBlueprint* InBlueprint,
		const TCHAR* InFunction, int32 InLine, const FString& InOperation);

	/** The same sentence without the logging, for a caller that has a tooltip or a notification instead. */
	DREAMGUIEDITOR_API FText DescribeStructuralRefusal(const UDreamWidgetBlueprint* InBlueprint, const FString& InOperation);

	/** Why one property row is not editable. Ordered so the FIRST reason that applies is the one shown. */
	enum class EPropertyEditVerdict : uint8
	{
		/** Editable, and the write-back has somewhere to put it. */
		Writable,
		/** Nothing here is gated: this Blueprint authors its own hierarchy. */
		NotTextAuthored,
		/** The file writes this as `Name <- Func()`, and a literal would delete the binding. */
		WrittenAsABinding,
		/** One line of text makes N of these, so there is no per-instance value to write. */
		ExpandedFromALoop,
		/** The write-back has no home for this property, so an edit here would not survive a compile. */
		OutsideTheWritableSet,
		/**
		 * The object is addressable and the property is in the set, but its VALUE has no `.dui`
		 * spelling -- so the write-back skips the line rather than guessing at a literal.
		 *
		 * Object references are the whole of it in practice, and they are the ones that hurt: a font,
		 * a texture, a material. Picking a new font in the details panel changes the preview, changes
		 * nothing in the file, and is gone at the next compile. See SetLiteralSpellingProbe.
		 */
		HasNoTextSpelling,
	};

	/**
	 * Answers "can the write-back spell this live value as a `.dui` literal", for one property.
	 *
	 * A seam rather than a rule of our own, and that is the entire point of it. The judge is
	 * FDreamUITextWriteBack's PrintLiteral: it is the function that will actually decide, at flush
	 * time, whether this property becomes a line in the file, and a second implementation here would
	 * eventually disagree with it in the one direction nobody can see -- the panel says editable, the
	 * flush says nothing, and the value is lost with no message on either side.
	 *
	 * The parameters are the property and a pointer to the live value, matching PrintLiteral, because
	 * the answer is not always decidable from the type: an enum value that is not one of the enum's
	 * entries has no identifier to write, and only the value knows that.
	 */
	using FLiteralSpellingProbe = TFunction<bool(const FProperty* /*Leaf*/, const void* /*ValuePtr*/)>;

	/**
	 * Install the probe. Called once, by whoever owns the write-back.
	 *
	 * UNTIL IT IS INSTALLED THIS GATE CANNOT SEE THE UNSPELLABLE CASE, and it fails open -- an object
	 * reference on an addressable object reads as Writable, exactly as it did before this seam
	 * existed. Failing closed was the alternative and is worse by a wide margin: with no probe there
	 * is no way to tell a font reference from a font SIZE, so closing would grey out every style
	 * value on every `.dui` and leave the designer with nothing to do.
	 */
	DREAMGUIEDITOR_API void SetLiteralSpellingProbe(FLiteralSpellingProbe InProbe);

	/** Whether a probe has been installed. False means the HasNoTextSpelling verdict can never occur. */
	DREAMGUIEDITOR_API bool HasLiteralSpellingProbe();

	/**
	 * Whether the designer may edit this property on this object, and why not.
	 *
	 * InOwner is the object the details panel is SHOWING -- the widget, its visual, its panel slot or
	 * one of its behaviours -- not the widget that owns it. InParentProperties is the details panel's
	 * chain with the immediate parent first (FPropertyAndParent::ParentProperties), which is what makes
	 * `AnchorData.SizeDelta` answerable: the leaf is SizeDelta and only the chain says whose.
	 *
	 * Answers Writable for anything on an object the gate does not own, including a widget in a level
	 * and a hand-authored Blueprint. Failing OPEN there is deliberate and is the opposite of the rule
	 * inside the set below -- an over-eager gate that greys out an unrelated panel is a bug report
	 * nobody can explain, while a gate that misses one property costs one lost value.
	 */
	DREAMGUIEDITOR_API EPropertyEditVerdict GetPropertyEditVerdict(const UObject* InOwner, const FProperty* InLeafProperty,
		TConstArrayView<const FProperty*> InParentProperties);

	/** GetPropertyEditVerdict != Writable && != NotTextAuthored. */
	DREAMGUIEDITOR_API bool IsPropertyReadOnly(const UObject* InOwner, const FProperty* InLeafProperty,
		TConstArrayView<const FProperty*> InParentProperties);

	/** One sentence naming the file and the reason, for the banner and for a tooltip. Empty when Writable. */
	DREAMGUIEDITOR_API FText DescribePropertyVerdict(EPropertyEditVerdict InVerdict, const FString& InSourceFileName);

	/**
	 * The same question for a details row that is a CUSTOM WIDGET rather than a property.
	 *
	 * A custom row reaches the details view through a different delegate (FIsCustomRowReadOnly) and is
	 * given only two names -- its own and its category's -- because there is no property node behind it
	 * to ask. So the answer cannot be per-property: it is per OBJECT plus category, and the anchor
	 * block, the layout banner and the transform fields are the rows this actually decides.
	 *
	 * InOwner is the object the panel is showing, as above.
	 */
	DREAMGUIEDITOR_API bool IsCustomRowReadOnly(const UObject* InOwner, FName InRowName, FName InCategoryName);

	/**
	 * Whether a binding may be AUTHORED from the details panel on this object.
	 *
	 * False for a text-authored class, and this one is not about write-back at all: the compiler
	 * REPLACES UDreamWidgetBlueprint::PropertyBindings wholesale from the `<-` lines in the file
	 * (DreamWidgetBlueprintCompiler.cpp, "the bindings alongside it, for the same reason"). A binding
	 * authored here therefore does not merely fail to reach the file -- it is deleted by the next
	 * compile, along with the function the Create Binding entry just made and opened.
	 */
	DREAMGUIEDITOR_API bool CanAuthorBindingsOn(const UObject* InOwner);

	/**
	 * Whether the file writes this property as a binding on this object.
	 *
	 * Read off UDreamWidgetBlueprint::PropertyBindings, which for a text-authored class IS the `<-`
	 * lines of the file. The patcher refuses to overwrite one with a literal (DUI7001) precisely
	 * because that would delete authored behaviour to store a value the binding was about to
	 * overwrite anyway -- so without this, one drag on a bound colour is a silent no-op that logs a
	 * write-back diagnostic somewhere the author is not looking.
	 */
	DREAMGUIEDITOR_API bool IsPropertyWrittenAsABinding(const UObject* InOwner, const FProperty* InProperty);

	/**
	 * Whether this widget is one of N instances a single `each` / `for` line produced.
	 *
	 * ALWAYS FALSE TODAY, and that is a fact about the builder rather than a stub: FDreamUITextBuilder
	 * refuses a loop body outright (DUI5007 LoopNotExpanded, "parsed but not yet expanded, so
	 * everything under this one was skipped"), so no widget in any tree can have come from one. The
	 * predicate exists anyway because the CALL SITE is the part that is easy to forget, and adding it
	 * later means finding every gate again.
	 *
	 * What it has to ask when expansion lands: whether this widget's authoring node lies inside a loop
	 * body. It will NOT be answerable by looking the display name up in the file -- expansion has to
	 * mint one id per instance, so the authored id in the text and the ids in the tree are different
	 * strings by construction. The builder is the only stage that knows both, so the marker belongs on
	 * the widget (or in a side table the compiler hands over), and this function is where it is read.
	 */
	DREAMGUIEDITOR_API bool IsExpandedFromLoop(const UDreamWidget* InWidget);
}
