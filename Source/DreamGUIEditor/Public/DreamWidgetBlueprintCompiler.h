// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "KismetCompiler.h"

class UDreamWidgetBlueprint;
class UDreamWidgetGeneratedClass;
class UDreamWidgetTree;
struct FDreamUIAst;
struct FDreamUIDiagnosticBag;

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
		 * Why the graph leg was refused, ready to print; empty when it ran.
		 *
		 * The graph leg is the one that can do harm. It matches purely by NAME, so if anything other
		 * than the renamed widget already answers to the old name -- a variable the author declared,
		 * a native member on the parent class -- moving every reference to it would be a rename
		 * nobody asked for, in a graph nobody was looking at. It refuses instead and says so.
		 */
		FString GraphRefusal;

		int32 Total() const { return GraphReferences + PropertyBindings + AnimationBindings; }
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
	 */
	static FWidgetRenameMigration MigrateWidgetRename(UDreamWidgetBlueprint* InBlueprint, const FString& InOldId, const FString& InNewId);

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
	virtual void FinishCompilingClass(UClass* Class) override;
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
	void MigrateRenamedWidgets(const FDreamUIAst& InAst, const FString& InSourceName);

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
