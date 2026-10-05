// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWidgetBlueprint.h"
#include "KismetCompiler.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "UObject/StrongObjectPtr.h"

class UDreamWidget;
class UDreamWidgetBlueprint;
class UEdGraph;
class UDreamWidgetGeneratedClass;
class UDreamWidgetTree;
struct FDreamUIAst;

/**
 * Compiles a DreamUI hierarchy into a class.
 *
 * The work is the part Kismet does not already do: declare one member variable per authored widget,
 * copy the authored hierarchy onto the generated class as its archetype, and refuse a binding whose
 * widget is gone. Everything else -- functions, the graph, the CDO -- is FKismetCompilerContext's.
 *
 * Registered through FKismetCompilerContext::RegisterCompilerForBP, which is independent of any
 * editor: the asset opens in the stock Blueprint editor and compiles through the stock button. A
 * DreamUI designer surface can be added later without this file changing.
 *
 * It is also where a .dui becomes a class. A UDreamTextUserWidget names a text file, and this reads
 * it, builds the hierarchy and installs it as the Blueprint's authored tree before anything counts
 * what is in that tree -- so from there on a text-authored class and a hand-authored one take exactly
 * the same path through this file, which is the whole reason text authoring costs so little here.
 */
class DREAMGUIEDITOR_API FDreamWidgetBlueprintCompilerContext : public FKismetCompilerContext
{
protected:
	using Super = FKismetCompilerContext;

public:
	FDreamWidgetBlueprintCompilerContext(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions);
	virtual ~FDreamWidgetBlueprintCompilerContext() override;

	/** The variable name a widget is exposed under. Delegates to the runtime rule; never reimplement it. */
	static FName MakeWidgetVariableName(const UDreamWidget* InWidget);

	/** What one `(was: OldId)` clause actually moved. Zero everywhere means there was nothing left to move. */
	struct FWidgetRenameMigration
	{
		/** Nodes in this Blueprint's own graphs that named the old variable. Dependents are fixed but not counted. */
		int32 GraphReferences = 0;
		/** Entries of UDreamWidgetBlueprint::PropertyBindings retargeted. */
		int32 PropertyBindings = 0;
		/** Embedded animation binding paths whose stale segment was rewritten. */
		int32 AnimationBindings = 0;
		/**
		 * Widget bindings rewritten inside LOADED standalone UDreamUISequence assets whose
		 * PreviewWidgetClass is this class -- the sequence asset says whose it is, which is what
		 * keeps this from renaming a stranger's path. Those assets are dirtied, not saved.
		 */
		int32 ExternalSequenceBindings = 0;
		/**
		 * Why the graph leg was refused, ready to print; empty when it ran.
		 *
		 * The graph leg is the one that can do harm. It matches purely by NAME, so if anything other
		 * than the renamed widget already answers to the old name -- a variable the author declared,
		 * a native member on the parent class -- moving every reference to it would be a rename
		 * nobody asked for, in a graph nobody was looking at. It refuses instead and says so.
		 */
		FString GraphRefusal;

		int32 Total() const { return GraphReferences + PropertyBindings + AnimationBindings + ExternalSequenceBindings; }
	};

	/**
	 * Carry every reference to InOldId in this asset across to InNewId.
	 *
	 * The three things a widget id IS, moved together: the class member variable (and therefore every
	 * Blueprint graph node that reads it), the WidgetName key of a property binding, and the
	 * display-name path an embedded animation resolves through. Renaming a node in a .dui breaks all
	 * three at once and only ONE of them has ever been loud about it, which is what `(was: ...)` exists
	 * to fix.
	 *
	 * ONE HOP, NEVER A CHAIN. `A (was: B)` moves references from B to A and stops. If the previous
	 * version of the file said `B (was: C)` and was never compiled, the references still on C stay on
	 * C -- this does not walk backwards through the file's history, because it has none to walk. That
	 * restriction is stated out loud because this codebase has been bitten by assuming the opposite:
	 * CoreRedirects applies once and does not follow a second hop either, and the day lost to that
	 * was spent looking for the bug anywhere except in the assumption.
	 *
	 * Public and static so a test can drive one rename against one asset. The compile-time entry
	 * point is the private MigrateRenamedWidgets, which is where the file gets checked for the ways
	 * it can ask for two contradictory renames at once.
	 *
	 * Ids, not variable names: the caller passes what the .dui says, and this derives the member
	 * variable from it through the shared rule and uses the id itself for the animation path. The two
	 * agree for anything the parser accepts as an id, and deriving both from one input is what keeps
	 * them agreeing if that ever stops being true.
	 *
	 * The designer's own rename (DreamWidgetTreeEditing::RenameWidget) calls this too, after the
	 * structural change has given the class the new name; see MigrateVariableReferences for why the
	 * order matters outside a compile. It moves the animation paths first, through MigrateWidgetRenamePaths.
	 */
	static FWidgetRenameMigration MigrateWidgetRename(UDreamWidgetBlueprint* InBlueprint, const FString& InOldId, const FString& InNewId);
	/**
	 * The path half of MigrateWidgetRename: the animation bindings embedded in this asset and in the loaded sequence
	 * assets authored against its class, InOldId's segment rewritten to InNewId; OutResult's AnimationBindings and
	 * ExternalSequenceBindings count them. Needs nothing of the class, so the designer's rename runs it BEFORE the
	 * structural change: that change compiles the skeleton, and a path still naming the old id is an error there.
	 */
	static void MigrateWidgetRenamePaths(UDreamWidgetBlueprint* InBlueprint, const FString& InOldId, const FString& InNewId, FWidgetRenameMigration& OutResult);

	/**
	 * Move every graph reference to the member variable InOldVariableName onto InNewVariableName, in this
	 * Blueprint and in every Blueprint that depends on it. The graph leg of MigrateWidgetRename, and the
	 * whole of an animation rename in the animation editor.
	 *
	 * The match is by name only, so it is refused, with the reason in OutRefusal, when something other
	 * than the renamed widget or animation already answers to the old name: a variable the author
	 * declared, a local variable of that name, or a member of the parent class. Returns how many nodes in
	 * this Blueprint's own graphs named the old variable.
	 *
	 * Outside a compile the class has to declare the new name first -- mark the Blueprint structurally
	 * modified, then call this. A generated variable's guid is derived from its name, a renamed node
	 * keeps the old guid, and while the class still declares only the old name, the first lookup of
	 * that node renames it back by the guid.
	 */
	static int32 MigrateVariableReferences(UDreamWidgetBlueprint* InBlueprint, FName InOldVariableName, FName InNewVariableName, FString& OutRefusal);

protected:
	// FKismetCompilerContext
	virtual void SpawnNewClass(const FString& NewClassName) override;
	virtual void OnNewClassSet(UBlueprintGeneratedClass* ClassToUse) override;
	virtual void EnsureProperGeneratedClass(UClass*& InOutTargetClass) override;
	virtual void CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOutOldCDO) override;
	virtual void SaveSubObjectsFromCleanAndSanitizeClass(FSubobjectCollection& SubObjectsToSave, UBlueprintGeneratedClass* ClassToClean) override;
	/**
	 * Read this class's .dui, if it declares one, and then declare one member variable per widget.
	 *
	 * This hook and not CreateClassVariablesFromBlueprint: the base resets GeneratedVariables
	 * immediately before calling this and then turns the list into properties, so a description added
	 * anywhere else is either wiped or too late.
	 *
	 * The text read happens HERE, ahead of the walk, and not in PreCompile as the P3 plan says. The
	 * plan's reasoning was right and its premise was not: CreateClassVariablesFromBlueprint does not
	 * call this, it only CONSUMES the list this fills. The one caller in the engine is
	 * ResetAndPopulateBlueprintGeneratedVariables, and its one caller is the compilation manager at
	 * STAGE V (BlueprintCompilationManager.cpp:1050) -- which runs before the skeleton is regenerated
	 * (STAGE VIII) and long before CompileClassLayout, and therefore before PreCompile
	 * (:1520 -> KismetCompiler.cpp:4751). A tree installed in PreCompile is a tree whose widgets get
	 * their variables from the PREVIOUS compile's hierarchy: rename a node in the .dui and the class
	 * declares the old name, silently, until something compiles the asset a second time. This is the
	 * earliest point the compilation manager offers, and it is the point the variable list is built.
	 */
	virtual void PopulateBlueprintGeneratedVariables() override;

	/**
	 * The `resources` entries of the file just built, carried from BuildWidgetTreeFromTextSource to
	 * the variable declarations a few lines later in the same stage. Not persisted anywhere: the
	 * compiler context lives for one compile, and the entries are re-read from the file every time,
	 * which is what makes the file win -- a Class Defaults edit survives exactly until the next
	 * compile unless the write-back has carried it home first.
	 */
	TArray<FDreamUIResource> TextResources;

	/** What this compile's read of the .dui came to, for the resource variables declared after it. */
	enum class ETextSourceOutcome : uint8
	{
		/** The class names no .dui: no resources, and none kept from before. */
		NoSource,
		/** The file was read and built into the hierarchy: TextResources holds its entries. */
		Built,
		/**
		 * The class names a file this compile could not build -- unreadable, not parsing, or building nothing --
		 * so the previous hierarchy stays, and the resource variables the last good read declared
		 * (UDreamWidgetBlueprint::LastGoodResourceVariables) are declared again with it, with a warning.
		 */
		KeptPrevious,
	};
	ETextSourceOutcome TextSourceOutcome = ETextSourceOutcome::NoSource;

	/**
	 * Every DUInnnn this compile raised about the .dui, from the parse through to the last check.
	 *
	 * A member rather than a local because the file is judged in TWO stages that are half a compile
	 * apart: the parse, the builder and the rename migration all run under
	 * PopulateBlueprintGeneratedVariables, while a binding's function and an event handler's
	 * signature cannot be looked at until FinishCompilingClass has built the class that declares
	 * them. The mailbox entry is keyed by file and REPLACED on each deposit, so the second stage has
	 * to add to this same bag and re-deposit -- depositing a second bag of its own would erase the
	 * first stage's diagnostics and leave the editor over in VSCode showing a clean file for a
	 * compile that failed.
	 */
	FDreamUIDiagnosticBag TextDiagnostics;
	virtual void FinishCompilingClass(UClass* Class) override;
	/**
	 * Hand the `events` dispatchers' signature graphs back to the Blueprint before the function list is made.
	 *
	 * They are generated at STAGE V with the variables, which is what gives the skeleton its signatures at STAGE
	 * VIII -- and then the compilation manager's conform pass at STAGE IX takes every signature graph whose name no
	 * NewVariables entry carries out of DelegateSignatureGraphs (FBlueprintEditorUtils::ConformDelegateSignatureGraphs),
	 * which is every one of these: a generated dispatcher is a GeneratedVariables entry, by design, so that it is
	 * nobody's to rename or delete in the Blueprint editor. This is the last moment they can come back, and the
	 * moment the compile needs them: CreateFunctionList is where a signature graph becomes the UFunction the
	 * dispatcher property is pointed at.
	 */
	virtual void CreateFunctionList() override;
	/**
	 * The `props` defaults of the props a parent class already declares. Such a prop gets no variable of its own --
	 * it IS the parent's property -- so the default the file writes for it has no variable description to ride on,
	 * and is written onto the class defaults here, after the generated variables' own, as the file's last word.
	 */
	virtual void CopyTermDefaultsToDefaultObject(UObject* DefaultObject) override;
	// End FKismetCompilerContext

	UDreamWidgetBlueprint* DreamWidgetBlueprint() const;

private:
	/**
	 * Read this class's .dui, if it declares one, and make the result the Blueprint's hierarchy.
	 *
	 * Does nothing at all -- not a read, not a diagnostic, not a touch of WidgetTree -- for a class
	 * that names no file, which is every hand-authored widget blueprint in the project. That is the
	 * one property of this pass worth guarding hardest: it runs on EVERY compile of EVERY DreamUI
	 * Blueprint, so anything it does unconditionally it does to assets that have nothing to do with
	 * the text pipeline.
	 */
	void BuildWidgetTreeFromTextSource(FDreamUIDiagnosticBag& OutDiagnostics);
	/** Every DUInnnn the read raised, as message log lines. Errors fail the compile; warnings do not. */
	void ReportTextDiagnostics(const FDreamUIDiagnosticBag& InDiagnostics);

	/**
	 * Declare the file's `props` as Blueprint variables, its `events` as event dispatchers, and its `viewmodels` as
	 * object variables of their classes -- the last also written down as the Blueprint's ViewModelSlots, which the
	 * generated class gets in CompilePropertyBindings.
	 *
	 * Called the moment the file has parsed, ahead of the thunk pass and the builder rather than beside the widget
	 * and resource variables further down, because both read what it declares: `Text <- Label` lowers into a getter
	 * of Label, which the thunk pass looks up in GeneratedVariables, `Text <- Player.Name` into a get of Player, and an
	 * emit route raises a dispatcher this has to have refused or accepted first. Every name is checked here against
	 * everything else that answers to one in the class -- the file's widgets and resources, the author's own members,
	 * the parent's -- because two members of one name is a class every graph node reads wrongly (PropNameTaken,
	 * EventNameTaken, ViewModelNameTaken).
	 */
	void DeclareTextMembers(const FDreamUIAst& InAst, FDreamUIDiagnosticBag& OutDiagnostics);
	/**
	 * One generated event dispatcher: the multicast delegate variable an author's Event Dispatcher is, and the
	 * signature graph that gives it its parameters -- RF_Transient, so that no save ever keeps it, and rebuilt on
	 * every compile like the variable. Null when the graph could not be made under that name.
	 */
	UEdGraph* DeclareDispatcher(FName InName, const TArray<FBPVariableDescription>& InParameters);

	/** Whether a member of InName is the author's own: a variable, function, macro, event graph or timeline. */
	bool IsAuthoredMemberName(FName InName) const;

	/**
	 * The props, dispatchers and view model variables this compile has declared or adopted from the parent, by name.
	 * The widget and resource variables declared after them stay off these names.
	 */
	TSet<FName> TextMemberNames;
	/** Declared by the file and refused, with the reason already in the bag; emit routes raising them are left alone. */
	TSet<FString> RefusedEventNames;
	/**
	 * What this compile declared, kept for UDreamWidgetBlueprint's last-good copies when the file builds. The `viewmodels`
	 * variables ride with the props: the hierarchy a file that stops parsing keeps reads through them just the same.
	 */
	TArray<FBPVariableDescription> DeclaredPropVariables;
	TArray<FDreamWidgetTextDispatcher> DeclaredDispatchers;
	/** Props the parent declares, with the default the file gives them; see CopyTermDefaultsToDefaultObject. */
	TArray<TPair<FName, FString>> InheritedPropDefaults;
	/**
	 * The signature graphs of this compile's dispatchers, held for the span between the conform pass that takes them
	 * out of the Blueprint and CreateFunctionList that puts them back -- out of the array they have no other referrer,
	 * and a collection in between would leave a dispatcher with no signature.
	 */
	TArray<TStrongObjectPtr<UEdGraph>> DispatcherSignatureGraphs;
	/** Whether this compile read the file's members (it parsed), as opposed to keeping the last good read's. */
	bool bTextMembersDeclared = false;
	/**
	 * The variables of anonymous widgets, which are declared hidden: the run time finds every bound widget through
	 * its class variable, so one must exist, but the author never named the widget and has nothing to reach it by.
	 */
	TSet<FName> HiddenWidgetVariableNames;

	/**
	 * Act on every `(was: OldId)` in the file, once the tree it describes has been installed.
	 *
	 * Runs AFTER the build rather than before it, and that is a decision rather than an accident. The
	 * fixup's post-condition is "nothing this compile keeps still names the old id", so it has to run
	 * over the state the compile keeps: the tree that was just installed and the binding list that
	 * came with it, not the pair the build is about to discard. Writing into objects on their way to
	 * the garbage collector would satisfy every test that looked at the wrong copy.
	 *
	 * The contradictions a file can contain are checked here rather than inside MigrateWidgetRename,
	 * because they are properties of the FILE and not of any one rename: an old id that is also a
	 * live id, and two nodes claiming the same old id, are only visible with the whole tree in hand.
	 * Both are errors, and when either fires NOTHING is migrated -- a half-applied set of renames is
	 * the one outcome worse than none, because the second compile would then find a different mess
	 * than the first one left.
	 */
	void MigrateRenamedWidgets(const FDreamUIAst& InAst, const FString& InSourceName, FDreamUIDiagnosticBag& OutDiagnostics);

	/** Duplicate the authored hierarchy onto the class. Editing the archetype in place would mutate live templates. */
	void UpdateGeneratedClassWidgetTree(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetGeneratedClass* InClass);

	/**
	 * Report every property that declares a widget binding no widget answers.
	 *
	 * This is the whole reason the class model is worth the trouble: under prefabs the same mistake
	 * surfaced at runtime as a null, after a save had already dropped it.
	 */
	void ValidateWidgetBindings(UClass* InClass);
	/** Every nested instance's slot bindings, against the slots its class actually declares. */
	void ValidateNamedSlotBindings(UDreamWidgetTree* InArchetype);
	/**
	 * Every embedded animation's bindings, against the widget names this hierarchy actually spells.
	 *
	 * A display name is three things at once -- a member variable, a property-binding key, and an
	 * animation path -- and this was the third one, the only one a rename could break without
	 * anybody being told. Worse than a null: playback falls back to the stored pointer, so every
	 * instance in the game animates the class template's widget, successfully and off-screen.
	 */
	void ValidateAnimationBindings(UDreamWidgetTree* InArchetype);
	/**
	 * Resolve the authored property bindings onto InClass, reporting the ones that cannot be honoured.
	 *
	 * Resolution here rather than at runtime is the point: the setter's name, its existence and its
	 * parameter type are all decided once, with a place to report the answer.
	 */
	void CompilePropertyBindings(UClass* InClass);

	UDreamWidgetGeneratedClass* NewDreamWidgetClass = nullptr;
	/** The class's previous archetype, kept across the sanitize pass so it can be patched over. */
	UDreamWidgetTree* OldWidgetTree = nullptr;
};
