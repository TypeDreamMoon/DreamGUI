// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamWidgetBlueprintCompiler.h"
#include "DreamWidgetBlueprint.h"

#include "Animation/DreamUISequence.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "UObject/UObjectIterator.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIDiagnosticsMailbox.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUIValueFormat.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Text/DreamUIExpressionThunks.h"
#include "Text/DreamUISourceWatcher.h"

#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_FunctionEntry.h"
#include "MovieScene.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetReinstanceUtilities.h"
#include "KismetCompilerMisc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "DreamWidgetBlueprintCompiler"

FDreamWidgetBlueprintCompilerContext::FDreamWidgetBlueprintCompilerContext(
	UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
	: Super(InBlueprint, InMessageLog, InCompileOptions)
{
}

FDreamWidgetBlueprintCompilerContext::~FDreamWidgetBlueprintCompilerContext() = default;

UDreamWidgetBlueprint* FDreamWidgetBlueprintCompilerContext::DreamWidgetBlueprint() const
{
	return Cast<UDreamWidgetBlueprint>(Blueprint);
}

FName FDreamWidgetBlueprintCompilerContext::MakeWidgetVariableName(const UDreamWidget* InWidget)
{
	// Deliberately a one-line delegation. The runtime resolves bindings with this exact function, and
	// the two agreeing is the entire contract between compile time and run time.
	return UDreamWidgetTree::MakeWidgetVariableName(InWidget);
}

void FDreamWidgetBlueprintCompilerContext::SpawnNewClass(const FString& NewClassName)
{
	NewDreamWidgetClass = FindObject<UDreamWidgetGeneratedClass>(Blueprint->GetOutermost(), *NewClassName);
	if (NewDreamWidgetClass == nullptr)
	{
		NewDreamWidgetClass = NewObject<UDreamWidgetGeneratedClass>(Blueprint->GetOutermost(), FName(*NewClassName), RF_Public | RF_Transactional);
	}
	else
	{
		// It existed but was not linked into the Blueprint yet, which load ordering can produce.
		FBlueprintCompileReinstancer::Create(NewDreamWidgetClass);
	}
	NewClass = NewDreamWidgetClass;
}

void FDreamWidgetBlueprintCompilerContext::OnNewClassSet(UBlueprintGeneratedClass* ClassToUse)
{
	NewDreamWidgetClass = CastChecked<UDreamWidgetGeneratedClass>(ClassToUse);
}

void FDreamWidgetBlueprintCompilerContext::EnsureProperGeneratedClass(UClass*& InOutTargetClass)
{
	if (InOutTargetClass != nullptr && !((UObject*)InOutTargetClass)->IsA(UDreamWidgetGeneratedClass::StaticClass()))
	{
		// An asset reparented into this Blueprint type carries a class of the wrong kind; it cannot
		// hold a widget-tree archetype, so it is discarded rather than compiled into.
		FKismetCompilerUtilities::ConsignToOblivion(InOutTargetClass, Blueprint->bIsRegeneratingOnLoad);
		InOutTargetClass = nullptr;
	}
}

void FDreamWidgetBlueprintCompilerContext::CleanAndSanitizeClass(UBlueprintGeneratedClass* ClassToClean, UObject*& InOutOldCDO)
{
	Super::CleanAndSanitizeClass(ClassToClean, InOutOldCDO);

	// The previous archetype is deliberately not destroyed here: FinishCompilingClass patches the new
	// one over it so any loader export still pointing at the old object resolves to the replacement.
	if (UDreamWidgetGeneratedClass* DreamClass = Cast<UDreamWidgetGeneratedClass>(ClassToClean))
	{
		DreamClass->SetWidgetTreeArchetype(nullptr);
	}
}

void FDreamWidgetBlueprintCompilerContext::SaveSubObjectsFromCleanAndSanitizeClass(FSubobjectCollection& SubObjectsToSave, UBlueprintGeneratedClass* ClassToClean)
{
	Super::SaveSubObjectsFromCleanAndSanitizeClass(SubObjectsToSave, ClassToClean);

	check(ClassToClean == NewClass);
	NewDreamWidgetClass = CastChecked<UDreamWidgetGeneratedClass>((UObject*)NewClass);
	OldWidgetTree = NewDreamWidgetClass->GetWidgetTreeArchetype();

	// The Blueprint's authoring tree has to survive the sub-object blitz. It is not the class's copy --
	// it is what the designer edits -- and letting it get renamed out from under the asset produces
	// load errors on the next open rather than an immediately visible failure.
	if (UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint())
	{
		if (IsValid(DreamBlueprint->WidgetTree))
		{
			SubObjectsToSave.AddObject(DreamBlueprint->WidgetTree);
		}
	}
}

void FDreamWidgetBlueprintCompilerContext::BuildWidgetTreeFromTextSource(FDreamUIDiagnosticBag& OutDiagnostics)
{
	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint == nullptr)
	{
		return;
	}

	// Edits mirrored into the template but not yet flushed into the .dui die here otherwise: this
	// function reads the FILE and rebuilds the tree from it, and the designer triggers a skeleton
	// compile on every structural edit -- so an unflushed property edit would be rebuilt away
	// moments after it was made. Flushing at the top of the read is the one chokepoint every
	// compile type passes through (the pre-compile broadcast fires a stage too late, after this
	// runs). Free when the template is clean; the flush's own write is invisible to the source
	// watcher through the own-write hash. Skipped while a transaction is being applied: the flush
	// opens a transaction of its own, and an undo application is no place to start one.
	if (!GIsTransacting)
	{
		if (FDreamWidgetBlueprintEditor* OpenEditor = FDreamWidgetBlueprintEditor::FindEditorForBlueprint(DreamBlueprint))
		{
			if (const TSharedPtr<FDreamWidgetPreviewHost> Host = OpenEditor->GetPreviewHost())
			{
				Host->FlushTemplateChanges();
			}
		}
	}

	// SourceFile is a CLASS DEFAULT, so the CDO is where it is read from -- and the CDO still
	// standing at this point in the compile is the previous one, which is exactly the object carrying
	// what the author typed into the Class Defaults panel. CleanAndSanitizeClass has not run yet, and
	// the new CDO that eventually replaces this one has these values copied onto it, so the path
	// survives every recompile without being stored anywhere but where the author put it.
	//
	// The parent's default is the fallback and not a second source: it answers the two moments the
	// generated class has no CDO to ask -- a Blueprint that has never been compiled -- and it is how a
	// native subclass that hardcodes its own .dui works at all.
	const UDreamTextUserWidget* Defaults = nullptr;
	if (DreamBlueprint->GeneratedClass != nullptr)
	{
		Defaults = Cast<UDreamTextUserWidget>(DreamBlueprint->GeneratedClass->GetDefaultObject(/*bCreateIfNeeded*/false));
	}
	if (Defaults == nullptr && DreamBlueprint->ParentClass != nullptr)
	{
		Defaults = Cast<UDreamTextUserWidget>(DreamBlueprint->ParentClass->GetDefaultObject(/*bCreateIfNeeded*/false));
	}
	if (Defaults == nullptr)
	{
		return;
	}

	const FString AuthoredPath = Defaults->SourceFile.FilePath.TrimStartAndEnd();
	if (AuthoredPath.IsEmpty())
	{
		// The negative control, and the single most important line in this function. A widget
		// blueprint that names no .dui has to come out of this compile byte for byte what it would
		// have been before the text pipeline existed: same tree object, same bindings, same messages.
		return;
	}

	const FString ResolvedPath = UDreamTextUserWidget::ResolveDuiFilePath(AuthoredPath);
	// The path, not the leaf name. Every diagnostic below is prefixed with this, in the layout an
	// editor turns into a jump -- "C:/Proj/DUI/Login.dui(12,5): error DUI2001: ..." -- and a bare
	// "Login.dui" is a string a message log cannot do anything with.
	OutDiagnostics.SourceName = ResolvedPath;

	FString SourceText;
	if (!FFileHelper::LoadFileToString(SourceText, *ResolvedPath))
	{
		// Both spellings, deliberately: the author wrote AuthoredPath and will go looking for that,
		// while the file that is missing is at ResolvedPath. A message naming only one of them cannot
		// tell "you misspelled it" apart from "your relative path resolved somewhere you did not
		// expect" -- and a relative path is now SEARCHED across roots, so the resolved one is merely
		// where the search gave up. The roots themselves are what a reader needs to see, because the
		// most likely cause of this error is a file sitting somewhere that is not a root at all.
		FString Message = FString::Printf(
			TEXT("Source File is '%s', and there is no readable file at '%s'"), *AuthoredPath, *ResolvedPath);
		if (FPaths::IsRelative(AuthoredPath))
		{
			TArray<FString> RootDirectories;
			for (const FDreamUISourceRoot& Root : DreamUIPaths::GetSourceRoots())
			{
				RootDirectories.Add(Root.Directory);
			}
			Message += RootDirectories.Num() > 0
				? FString::Printf(TEXT(". Searched: %s"), *FString::Join(RootDirectories, TEXT(", ")))
				: FString::Printf(TEXT(". No %s directory exists in this project or any enabled plugin"),
					DreamUIPaths::SourceDirectoryName);
		}
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::SourceFileUnreadable, FDreamUISourceLocation(), Message);
		return;
	}

	FDreamUIAst Ast;
	if (!FDreamUISourceFile::Parse(SourceText, ResolvedPath, Ast, OutDiagnostics, FDreamUISourceFile::MakeFileImportReader()))
	{
		// The previous hierarchy is left exactly where it is. A file that will not parse says nothing
		// about what the class should contain, and blanking the tree here would turn one typo into a
		// designer with nothing in it and a graph full of missing-variable errors that all point away
		// from the actual mistake. The compile still fails: the parse errors are already in the bag.
		return;
	}

	// The `class` line, checked against the asset actually being compiled. A WARNING: the line's
	// job is a stable localization namespace, so a wrong one drifts keys rather than breaking the
	// build -- and a file deliberately shared by a native parent across subclasses legitimately
	// matches none of them, which an error would forbid. Both spellings of the same asset pass
	// ("/Game/UI/WBP_X" and "/Game/UI/WBP_X.WBP_X").
	if (!Ast.ClassPath.IsEmpty() && !Ast.ClassPath.StartsWith(TEXT("/Script/")))
	{
		const FString PackageName = DreamBlueprint->GetOutermost()->GetName();
		FString Claimed = Ast.ClassPath;
		int32 DotIndex;
		if (Claimed.FindLastChar(TEXT('.'), DotIndex))
		{
			Claimed.LeftInline(DotIndex);
		}
		if (!Claimed.Equals(PackageName, ESearchCase::IgnoreCase))
		{
			OutDiagnostics.AddWarning(EDreamUIDiagnosticCode::ClassPathMismatch, Ast.ClassPathLocation,
				FString::Printf(TEXT("this file says it compiles into '%s', but it is being compiled into '%s' -- localization keys will use the name in the file"),
					*Ast.ClassPath, *PackageName));
		}
	}

	TArray<FDreamWidgetPropertyBinding> Bindings;
	TArray<FDreamWidgetEventBinding> EventBindings;
	TArray<FDreamWidgetEachBinding> EachBindings;
	// Outered to the Blueprint, which is where UDreamWidgetBlueprint::GetOrCreateWidgetTree puts the
	// hand-authored one. That is not a detail: FinishCompilingClass duplicates THIS object onto the
	// generated class as the archetype, and SaveSubObjectsFromCleanAndSanitizeClass keeps it alive
	// through the sanitize pass by name -- both were written against a tree that lives on the asset.
	// The dependency edges, republished every parse: a saved style library recompiles its wearers.
	FDreamUISourceWatcher::NoteImports(ResolvedPath, Ast.Imports);

	// Expression bindings lower into generated pure functions here, BEFORE the builder reads the
	// AST: each `<- expr` line's BindingFunction is rewritten in place to its thunk's name, so the
	// builder, the runtime and every migration see exactly the one-name shape they always did. An
	// expression the generator refuses reports DUI5011 into the bag and clears its binding.
	DreamUIExpressionThunks::Generate(DreamBlueprint, Ast, OutDiagnostics);

	UDreamWidgetTree* NewTree = FDreamUITextBuilder::Build(Ast, DreamBlueprint, OutDiagnostics, Bindings, &EventBindings, &EachBindings);
	if (!IsValid(NewTree) || !IsValid(NewTree->RootWidget))
	{
		// Its own code even though the builder has already said why. The builder reports a CAUSE (this
		// node names a type nothing resolves); this reports the OUTCOME (there is nothing to compile
		// into the class), and they are different facts that a reader acts on differently. It also
		// means this branch can never fail silently, whatever a future builder decides to return.
		OutDiagnostics.AddError(EDreamUIDiagnosticCode::EmptyTree, Ast.Root.Location,
			TEXT("the file parsed but produced no hierarchy, so there is nothing to compile into this class"));
		return;
	}

	// The same flags GetOrCreateWidgetTree hands the hand-authored tree, and matched on purpose:
	// everything downstream -- the duplicate onto the class, the designer's preview host, the
	// transaction buffer -- was written against that object, and a tree that differs from it only in
	// its flags is the kind of difference that surfaces three files away as "undo does nothing here".
	NewTree->SetFlags(RF_Transactional);

	// Animations ride across the rebuild. The grammar cannot author a sequence, so everything in a
	// SequenceArray was made in the animation editor and lives nowhere but the tree this compile is
	// about to drop -- without this carry, one compile deletes the author's animation work. Matched
	// by display name because that is the model animation bindings themselves resolve through, and
	// carried BEFORE MigrateRenamedWidgets so `(was: OldId)` clauses rewrite the carried paths.
	// AddComponentByTemplate goes through FObjectInstancingGraph, and SequenceArray is Instanced, so
	// the sequences are re-homed rather than pointer-shared with an object headed for the reaper.
	if (IsValid(DreamBlueprint->WidgetTree) && IsValid(DreamBlueprint->WidgetTree->RootWidget))
	{
		TMap<FString, UDreamWidget*> NewWidgetsByDisplayName;
		TArray<UDreamWidget*> NewWidgets;
		UDreamWidget::CollectChildrenWidgets(NewTree->RootWidget, NewWidgets, /*IncludeTarget*/true);
		for (UDreamWidget* NewWidget : NewWidgets)
		{
			NewWidgetsByDisplayName.Add(NewWidget->GetDisplayName(), NewWidget);
		}

		TArray<UDreamWidget*> OldWidgets;
		UDreamWidget::CollectChildrenWidgets(DreamBlueprint->WidgetTree->RootWidget, OldWidgets, /*IncludeTarget*/true);
		for (UDreamWidget* OldWidget : OldWidgets)
		{
			UDreamWidgetAnimationComponent* OldAnimator = IsValid(OldWidget) ? OldWidget->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
			if (OldAnimator == nullptr || OldAnimator->GetSequenceArray().Num() == 0)
			{
				continue;
			}
			// The root hosts the animations in practice, and a renamed root has no name to match, so
			// root pairs with root regardless of what either is called.
			UDreamWidget* NewHome = OldWidget == DreamBlueprint->WidgetTree->RootWidget
				? NewTree->RootWidget.Get()
				: NewWidgetsByDisplayName.FindRef(OldWidget->GetDisplayName());
			if (!IsValid(NewHome) || NewHome->GetComponent<UDreamWidgetAnimationComponent>() != nullptr)
			{
				// No widget by that name in the new file, or (impossible today) it already animates:
				// the sequences stay with the old tree and die with it. Said out loud, not silently.
				MessageLog.Warning(*FString::Printf(
					TEXT("Animations on widget '%s' could not be carried across the .dui rebuild: no widget with that name in the new hierarchy. Rename with (was: %s) to keep them."),
					*OldWidget->GetDisplayName(), *OldWidget->GetDisplayName()));
				continue;
			}
			NewHome->AddComponentByTemplate(OldAnimator);
		}
	}

	// The one field the text owns. Replaced rather than merged: the .dui is the whole hierarchy, so
	// anything still in the old tree is by definition not in the file any more.
	DreamBlueprint->WidgetTree = NewTree;
	// And the resources ride along for PopulateBlueprintGeneratedVariables, which declares one class
	// variable per entry a few lines after this function returns.
	TextResources = Ast.Resources;
	// And the bindings alongside it, for the same reason -- `<-` lines live in the same file. These
	// are the AUTHORED list; CompilePropertyBindings resolves them onto the class at the end of the
	// compile and reports the ones that cannot be honoured, which is how a .dui naming a function the
	// Blueprint does not declare becomes an error here rather than a null at run time.
	DreamBlueprint->PropertyBindings = MoveTemp(Bindings);
	DreamBlueprint->EventBindings = MoveTemp(EventBindings);
	DreamBlueprint->EachBindings = MoveTemp(EachBindings);

	// Last, with the new hierarchy in place: `(was: OldId)` moves what the OLD name still owns onto
	// the new one. See MigrateRenamedWidgets for why after the install and not before.
	MigrateRenamedWidgets(Ast, ResolvedPath);
}

void FDreamWidgetBlueprintCompilerContext::ReportTextDiagnostics(const FDreamUIDiagnosticBag& InDiagnostics)
{
	for (const FDreamUIDiagnostic& Diagnostic : InDiagnostics.Diagnostics)
	{
		// ToString() verbatim, never a reworded copy. "File(Line,Col): severity DUInnnn: text" is
		// what makes a message log line something a reader can double-click, and the file and line
		// are the entire reason a text pipeline is better than a binary one.
		//
		// Passed as the format string with no varargs, which is safe on purpose: FCompilerResultsLog
		// only looks for @@ when arguments follow it, so a diagnostic quoting a .dui that happens to
		// contain one is printed rather than eaten.
		if (Diagnostic.IsError())
		{
			MessageLog.Error(*Diagnostic.ToString());
		}
		else
		{
			MessageLog.Warning(*Diagnostic.ToString());
		}
	}
}

namespace DreamWidgetRenameMigrationLocal
{
	/** "Login.dui(12,5): " -- the prefix that makes a message log line something a reader can jump from. */
	FString SourcePrefix(const FString& InSourceName, const FDreamUISourceLocation& InLocation)
	{
		// Hand-built rather than routed through FDreamUIDiagnostic::ToString, and only because these
		// messages have no DUInnnn to print yet: the code table lives in a header this change is not
		// allowed to touch, so the two conflict errors and the two notes below are plain compiler
		// messages for now. They want codes (see the note above MigrateRenamedWidgets); the FILE and
		// LINE are the half that has to be right today, because that is the whole argument for a
		// text pipeline over a binary one.
		FString Prefix = InSourceName;
		if (InLocation.IsValid())
		{
			Prefix += FString::Printf(TEXT("(%d,%d)"), InLocation.Line, InLocation.Column);
		}
		return Prefix.IsEmpty() ? FString() : Prefix + TEXT(": ");
	}

	/**
	 * Whether anything on this node will be localized under a key derived from its id.
	 *
	 * A string literal is the only value kind that can become an FText, and the builder keys one as
	 * `<id>.<Property>` unless the author wrote `@key(...)`. So a rename silently re-keys every one of
	 * them, and no fixup in this file can carry that across: the translations are not in the asset,
	 * they are in the localization archive next to it. All the compiler can do is say so at the one
	 * moment the author is looking at the rename.
	 */
	bool HasIdDerivedLocalizationKey(const FDreamUINode& InNode, const FDreamUIAst& InAst)
	{
		auto AnyUnkeyedString = [](const TArray<FDreamUIProperty>& InProperties)
		{
			for (const FDreamUIProperty& Property : InProperties)
			{
				if (!Property.IsBinding()
					&& Property.Value.Kind == EDreamUIValueKind::String
					&& Property.Value.LocalizationKeyOverride.IsEmpty())
				{
					return true;
				}
			}
			return false;
		};

		if (AnyUnkeyedString(InNode.Properties) || AnyUnkeyedString(InNode.SlotProperties))
		{
			return true;
		}
		for (const FDreamUIComponent& Component : InNode.Components)
		{
			if (AnyUnkeyedString(Component.Properties))
			{
				return true;
			}
		}
		// A string can also reach the node THROUGH the style it wears -- the builder keys those by
		// this node's id exactly the same way, so a rename orphans them exactly the same way. The
		// walk mirrors the builder's chain (base upward, cycle-guarded); a broken chain just stops,
		// because the builder already reported it and this predicate only decides whether to hint.
		const FDreamUIStyle* Style = InNode.StyleName.IsEmpty() ? nullptr : InAst.FindStyle(InNode.StyleName);
		TSet<const FDreamUIStyle*> Visited;
		while (Style != nullptr && !Visited.Contains(Style))
		{
			Visited.Add(Style);
			if (AnyUnkeyedString(Style->Properties))
			{
				return true;
			}
			Style = Style->BaseName.IsEmpty() ? nullptr : InAst.FindStyle(Style->BaseName);
		}
		return false;
	}
}

FDreamWidgetBlueprintCompilerContext::FWidgetRenameMigration
FDreamWidgetBlueprintCompilerContext::MigrateWidgetRename(UDreamWidgetBlueprint* InBlueprint, const FString& InOldId, const FString& InNewId)
{
	FWidgetRenameMigration Result;
	if (InBlueprint == nullptr || InOldId.IsEmpty() || InNewId.IsEmpty())
	{
		return Result;
	}

	// Through the shared rule and never a second copy of it. The class declares its members with
	// MakeWidgetVariableName and the runtime resolves bindings with the same function; a rename that
	// computed the name any other way would move graph references onto a variable nothing declares.
	const FName OldVariableName(*UDreamWidgetTree::SanitizeIdentifier(InOldId));
	const FName NewVariableName(*UDreamWidgetTree::SanitizeIdentifier(InNewId));
	if (OldVariableName == NewVariableName)
	{
		// FName comparison, so this also catches `(was: okbtn)` on a node called OkBtn. Nothing below
		// would be actively wrong, but every leg would be asked to replace a name with itself and the
		// animation leg would count that as work done.
		return Result;
	}

	// --- 1. The graph. -----------------------------------------------------------------------
	//
	// FBlueprintEditorUtils::ReplaceVariableReferences, which is what UMG's own widget rename calls
	// (WidgetBlueprintOperationUtils.cpp, FWidgetBlueprintOperationUtils::RenameWidget). It walks
	// every graph of this Blueprint AND of every Blueprint that depends on it, letting each K2Node
	// repoint its own FMemberReference -- which is the part that cannot be done from outside, because
	// a variable get, a variable set, an event with a bound delegate and a component-bound node each
	// store the reference differently.
	//
	// Safe to call mid-compile, and specifically at this stage: it dirties the Blueprint through
	// MarkBlueprintAsModified, which early-outs while bBeingCompiled is set (BlueprintEditorUtils.cpp
	// :1924) -- and bBeingCompiled goes up at STAGE IV, one stage before the hook this runs under.
	// The node's own Modify() still records the change and still dirties the package, which is what
	// makes the fixup survive to the next save.
	//
	// One more thing had to be true for this stage to work, and it is worth writing down because it
	// is not obvious and it is what would silently undo the rename: HandleVariableRenamed moves the
	// NAME and leaves the node's MemberGuid alone, and FMemberReference::ResolveMember will happily
	// rename a reference BACK if that stale guid still matches a variable (MemberReference.cpp:468).
	// Our widget variables carry FGuid::NewDeterministicGuid(name), so the old id's guid is a real
	// guid that a lookup could hit -- except that the only place such a lookup reads is
	// UBlueprint::GeneratedVariables, and ResetAndPopulateBlueprintGeneratedVariables emptied that
	// list immediately before this hook. By the time it is refilled it holds the NEW names, whose
	// guids the old one does not match. Moving this fixup anywhere later in the compile reopens that
	// window.
	{
		// The graph leg matches by NAME ONLY. Everything below is about the one way that can do harm:
		// if something OTHER than the renamed widget already answers to the old name, this would move
		// its references too -- silently, in a graph nobody has open.
		FString Refusal;
		if (FBlueprintEditorUtils::FindNewVariableIndex(InBlueprint, OldVariableName) != INDEX_NONE)
		{
			Refusal = FString::Printf(
				TEXT("this Blueprint declares a variable of its own called \"%s\", and a graph reference to that name cannot be told apart from one to the widget"),
				*OldVariableName.ToString());
		}
		else if (InBlueprint->ParentClass != nullptr
			&& InBlueprint->ParentClass->FindPropertyByName(OldVariableName) != nullptr)
		{
			// Not hypothetical: PopulateBlueprintGeneratedVariables deliberately skips a widget whose
			// name the parent already declares, so a widget called this never had a variable of its
			// own for anything to reference. The references belong to the parent's member.
			Refusal = FString::Printf(
				TEXT("\"%s\" is a member of the parent class %s, so the graph references to it are not this widget's"),
				*OldVariableName.ToString(), *InBlueprint->ParentClass->GetName());
		}

		TArray<UEdGraph*> AllGraphs;
		InBlueprint->GetAllGraphs(AllGraphs);

		if (Refusal.IsEmpty())
		{
			// Function-local variables, which FindNewVariableIndex does not see: they live on the
			// function entry node, not on the Blueprint. HandleVariableRenamed would happily repoint a
			// local variable reference of the same name while leaving the DECLARATION alone, which is
			// a graph that stops compiling with an error naming a variable the author never typed.
			for (const UEdGraph* Graph : AllGraphs)
			{
				for (const UEdGraphNode* Node : Graph->Nodes)
				{
					const UK2Node_FunctionEntry* Entry = Cast<UK2Node_FunctionEntry>(Node);
					if (Entry == nullptr)
					{
						continue;
					}
					for (const FBPVariableDescription& Local : Entry->LocalVariables)
					{
						if (Local.VarName == OldVariableName)
						{
							Refusal = FString::Printf(
								TEXT("\"%s\" is also a local variable in \"%s\""),
								*OldVariableName.ToString(), *Graph->GetName());
							break;
						}
					}
				}
			}
		}

		if (!Refusal.IsEmpty())
		{
			Result.GraphRefusal = MoveTemp(Refusal);
		}
		else
		{
			// Counted before the replace, because ReplaceVariableReferences reports nothing. The same
			// question RenameVariableReferencesInGraph asks internally to decide whether it changed
			// anything, asked here so the note can say how much moved -- and so "nothing moved" can be
			// told apart from "it ran", which is the whole of the already-migrated case.
			//
			// This Blueprint's graphs only. A dependent Blueprint's references are fixed by the call
			// below and deliberately not counted: reaching into other assets to tally them would mean
			// walking every loaded Blueprint twice for a number nobody acts on.
			for (const UEdGraph* Graph : AllGraphs)
			{
				for (const UEdGraphNode* Node : Graph->Nodes)
				{
					const UK2Node* K2Node = Cast<UK2Node>(Node);
					if (K2Node != nullptr && K2Node->ReferencesVariable(OldVariableName, nullptr))
					{
						++Result.GraphReferences;
					}
				}
			}

			if (InBlueprint->GeneratedClass != nullptr)
			{
				// Guarded on the class, not for safety -- a null one makes every node's scope check
				// fail and the whole call a no-op -- but on cost: ReplaceVariableReferences walks
				// every loaded UBlueprint to find dependents, and doing that to achieve nothing on
				// every compile of a Blueprint that has never been compiled is a poor trade.
				FBlueprintEditorUtils::ReplaceVariableReferences(InBlueprint, OldVariableName, NewVariableName);
			}
		}
	}

	// --- 2. The authored property bindings. --------------------------------------------------
	//
	// A `<-` line's WidgetName IS the variable name, so this is the same rename spelled in a second
	// place. Under a .dui the whole list was just rebuilt from the file and therefore already says
	// the new name, which makes this leg a no-op TODAY -- and it is here anyway, because the list is
	// a persistent field of the asset that the details panel can also write to. A binding that
	// survives a build (a hybrid asset, or the day the builder merges instead of replacing) has to
	// come through a rename, and a fixup that is missing on that day is a null at run time.
	{
		bool bModifiedBlueprint = false;
		for (FDreamWidgetPropertyBinding& Binding : InBlueprint->PropertyBindings)
		{
			if (Binding.WidgetName == OldVariableName)
			{
				if (!bModifiedBlueprint)
				{
					// Before the write, so the transaction records the value being replaced.
					InBlueprint->Modify();
					bModifiedBlueprint = true;
				}
				Binding.WidgetName = NewVariableName;
				++Result.PropertyBindings;
			}
		}
	}

	// --- 3. The embedded animation paths. ----------------------------------------------------
	//
	// The third identity, and the one that was silent before P0: an animation binding is a '/'-joined
	// chain of DISPLAY names from the widget owning the animation down to the widget being driven, so
	// a rename anywhere along that chain leaves the path naming a widget that no longer exists.
	// Worse than a null -- playback falls back to the stored pointer and every instance in the game
	// animates the class template's widget, successfully and off-screen.
	//
	// The tree walked is the one the compile KEEPS. Under a .dui that tree was rebuilt from the file
	// moments ago and carries no animations at all, because the text grammar cannot author a sequence
	// and the builder does not carry components across a rebuild -- so this leg, like the one above,
	// is defensive under text authoring today. It is written against a tree rather than against the
	// text pipeline for exactly that reason: the day animations survive a rebuild, or the day a
	// hand-authored hierarchy gets a rename clause of its own, this is already the right code.
	{
		UDreamWidgetTree* Tree = InBlueprint->WidgetTree;
		if (IsValid(Tree))
		{
			Tree->ForEachWidget([&Result, &InOldId, &InNewId](UDreamWidget* ContextWidget)
			{
				for (UDreamUIBehaviour* Component : ContextWidget->GetAllComponents())
				{
					UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component);
					if (Animator == nullptr)
					{
						continue;
					}
					for (UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
					{
						if (IsValid(Animation))
						{
							// The ids, not the variable names: a path is built from GetDisplayName and
							// resolved by comparing against GetDisplayName. The two spellings agree
							// for everything the parser accepts as an id -- its identifier charset is
							// exactly what SanitizeIdentifier keeps -- but they are DIFFERENT RULES,
							// and feeding the sanitized name to a display-name comparison is the kind
							// of "works until someone widens the charset" that this file avoids by
							// deriving each from the id separately.
							Result.AnimationBindings += Animation->RenameWidgetPathSegment(InOldId, InNewId);
						}
					}
				}
			});
		}
	}

	// --- 4. Standalone sequence assets authored against this class. --------------------------
	//
	// The fourth identity, and the fourth silent channel: a UDreamUISequence lives in its own
	// package, binds widgets by the same '/'-joined display names, and the class compile cannot
	// otherwise see it. PreviewWidgetClass is what makes the reach SAFE -- the sequence itself says
	// which class it is authored against, so this never blind-renames a segment in some other UI's
	// sequence that happens to reuse the id. Loaded sequences only: loading packages mid-compile is
	// its own hazard, and the unloaded ones get named in a warning by the caller instead.
	{
		for (TObjectIterator<UDreamUISequence> It; It; ++It)
		{
			UDreamUISequence* Sequence = *It;
			if (!IsValid(Sequence) || Sequence->PreviewWidgetClass.IsNull())
			{
				continue;
			}
			const UClass* SequenceClass = Sequence->PreviewWidgetClass.Get();
			const bool bMatches =
				(SequenceClass != nullptr
					&& (SequenceClass == InBlueprint->GeneratedClass || SequenceClass == InBlueprint->SkeletonGeneratedClass))
				|| (InBlueprint->GeneratedClass != nullptr
					&& Sequence->PreviewWidgetClass.ToSoftObjectPath() == FSoftObjectPath(InBlueprint->GeneratedClass));
			if (bMatches)
			{
				Result.ExternalSequenceBindings += Sequence->RenameWidgetPathSegments(InOldId, InNewId);
			}
		}
	}

	return Result;
}

void FDreamWidgetBlueprintCompilerContext::MigrateRenamedWidgets(const FDreamUIAst& InAst, const FString& InSourceName)
{
	using namespace DreamWidgetRenameMigrationLocal;

	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint == nullptr)
	{
		return;
	}

	// Every live id first, in its own pass. Walking once and checking as we go would only ever see
	// the ids ABOVE each `(was: ...)`, so `Text OkBtn (was: OkLabel)` written before a node still
	// called OkLabel would migrate and the same pair written the other way round would not: one file,
	// two answers, decided by line order.
	//
	// Keyed by FString, which TMap compares case-insensitively -- the same rule the parser's
	// duplicate-id check uses, and the right one, because these ids become FName member variables
	// that would collide anyway.
	TMap<FString, FDreamUISourceLocation> LiveIds;
	InAst.ForEachNode([&LiveIds](const FDreamUINode& Node)
	{
		if (!Node.Id.IsEmpty())
		{
			LiveIds.Add(Node.Id, Node.Location);
		}
	});

	TArray<const FDreamUINode*> Renames;
	TMap<FString, FDreamUISourceLocation> ClaimedOldIds;
	bool bFileContradictsItself = false;

	InAst.ForEachNode([this, &InSourceName, &LiveIds, &Renames, &ClaimedOldIds, &bFileContradictsItself](const FDreamUINode& Node)
	{
		if (Node.WasId.IsEmpty())
		{
			return;
		}

		if (Node.Id.Equals(Node.WasId, ESearchCase::IgnoreCase))
		{
			MessageLog.Error(*FString::Printf(
				TEXT("%s\"%s\" names itself as its own old id. A rename clause says what a node USED to be called, so a node that was already called this has nothing to migrate; delete the '(was: %s)'."),
				*SourcePrefix(InSourceName, Node.Location), *Node.Id, *Node.WasId));
			bFileContradictsItself = true;
			return;
		}

		if (const FDreamUISourceLocation* Live = LiveIds.Find(Node.WasId))
		{
			// Both names alive at once. Refused rather than guessed at, and this is the case the
			// guessing would be worst for: if the old name were migrated onto the new node anyway,
			// every reference would move OFF the node that still legitimately carries that name.
			// The author has renamed one node and created another with the old name in one edit, and
			// only they know which of the two their graph meant.
			MessageLog.Error(*FString::Printf(
				TEXT("%s\"%s\" says it was called \"%s\", but a node on line %d is still called that. Rename that one first, or drop the '(was: %s)' -- with both names in the file there is no way to tell which node a reference to \"%s\" meant."),
				*SourcePrefix(InSourceName, Node.Location), *Node.Id, *Node.WasId, Live->Line, *Node.WasId, *Node.WasId));
			bFileContradictsItself = true;
			return;
		}

		if (const FDreamUISourceLocation* First = ClaimedOldIds.Find(Node.WasId))
		{
			MessageLog.Error(*FString::Printf(
				TEXT("%s\"%s\" says it was called \"%s\", and so does the node on line %d. One old name cannot become two new ones; keep the clause on whichever node inherits the references and delete the other."),
				*SourcePrefix(InSourceName, Node.Location), *Node.Id, *Node.WasId, First->Line));
			bFileContradictsItself = true;
			return;
		}

		ClaimedOldIds.Add(Node.WasId, Node.Location);
		Renames.Add(&Node);
	});

	if (bFileContradictsItself)
	{
		// All or nothing. A half-applied set of renames leaves the asset in a state neither version
		// of the file describes, and the next compile then starts from that instead of from what the
		// author wrote -- so the second run of a broken file does different damage than the first.
		// The compile is already failing on the errors above; nothing here has to fail it again.
		return;
	}

	// One hop and no chain: each clause is applied against the asset as it stands, and the result is
	// never fed back in. `A (was: B)` while a previous version said `B (was: C)` migrates B to A and
	// leaves anything still on C where it is. Nothing enforces the ordering because nothing has to --
	// a rename whose new name is another rename's old name is exactly the "still called that" error
	// above, since every new name is a live id -- but the restriction is written down anyway, because
	// this codebase has already lost a day to assuming a redirect follows a second hop when
	// CoreRedirects, which also applies exactly once, does not.
	for (const FDreamUINode* Node : Renames)
	{
		const FWidgetRenameMigration Migration = MigrateWidgetRename(DreamBlueprint, Node->WasId, Node->Id);

		if (!Migration.GraphRefusal.IsEmpty())
		{
			// A warning and not an error: the other two legs ran, the hierarchy is fine, and the
			// author gets to decide whether the collision is a mistake or a name they meant to reuse.
			// Failing the compile here would block a build over a graph that may not reference the
			// name at all.
			MessageLog.Warning(*FString::Printf(
				TEXT("%s\"%s\" could not take the graph references from \"%s\": %s. Repoint them by hand, or rename the other one."),
				*SourcePrefix(InSourceName, Node->Location), *Node->Id, *Node->WasId, *Migration.GraphRefusal));
		}

		// The localization key moved with the id and cannot be brought along: the translations live
		// in the localization archive, not in this asset. Said only when the node actually has a
		// string the builder would key, so an ordinary rename does not carry a paragraph about it.
		// A rename that re-keys localized strings deserves a WARNING of its own, not just the note's
		// appended sentence: notes are the first thing filtered out of a compile log, and orphaned
		// translations surface weeks later as English text in a shipped build.
		if (HasIdDerivedLocalizationKey(*Node, InAst))
		{
			MessageLog.Warning(*FString::Printf(
				TEXT("%sRenaming \"%s\" -> \"%s\" re-keys its localized strings (styles it wears included): translations recorded against \"%s.<property>\" are orphaned unless @key(\"%s.<property>\") pins them."),
				*SourcePrefix(InSourceName, Node->Location), *Node->WasId, *Node->Id, *Node->WasId, *Node->WasId));
		}

		const FString LocalizationHint = HasIdDerivedLocalizationKey(*Node, InAst)
			? FString::Printf(
				TEXT(" Its localized strings are keyed by id, so translations recorded against \"%s.<property>\" are NOT carried over -- write @key(\"%s.<property>\") on those lines to keep them."),
				*Node->WasId, *Node->WasId)
			: FString();

		if (Migration.Total() > 0)
		{
			MessageLog.Note(*FString::Printf(
				TEXT("%s\"%s\" took over from \"%s\": %d graph reference(s), %d property binding(s), %d animation path(s), %d sequence-asset binding(s). That is done and recorded on the asset, so the '(was: %s)' line has served its purpose and can be deleted.%s"),
				*SourcePrefix(InSourceName, Node->Location), *Node->Id, *Node->WasId,
				Migration.GraphReferences, Migration.PropertyBindings, Migration.AnimationBindings, Migration.ExternalSequenceBindings,
				*Node->WasId, *LocalizationHint));
			if (Migration.ExternalSequenceBindings > 0)
			{
				MessageLog.Warning(*FString::Printf(
					TEXT("%sThe rename \"%s\" -> \"%s\" rewrote %d binding(s) in loaded sequence ASSETS. Those assets are dirty and unsaved -- save them, or the migration exists only in memory."),
					*SourcePrefix(InSourceName, Node->Location), *Node->WasId, *Node->Id, Migration.ExternalSequenceBindings));
			}
		}
		else
		{
			// The ordinary steady state, and deliberately not a warning: an author who migrated last
			// compile and has not deleted the line yet has done nothing wrong. It is still worth one
			// quiet line, because "this clause now does nothing" is the only signal that says the
			// line is safe to remove.
			MessageLog.Note(*FString::Printf(
				TEXT("%s\"%s\" found nothing still named \"%s\", so this rename has already been applied. The '(was: %s)' line can be deleted.%s"),
				*SourcePrefix(InSourceName, Node->Location), *Node->Id, *Node->WasId, *Node->WasId,
				*LocalizationHint));
		}
	}

	// Sequence assets that reference this Blueprint but are NOT loaded could not take the hop --
	// leg 4 deliberately touches loaded objects only. Name them once, so "the map's sequence broke a
	// week after the rename" becomes "the compile told me which assets to open" instead. A registry
	// that is still scanning stays silent rather than crying wolf.
	{
		IAssetRegistry* AssetRegistry = IAssetRegistry::Get();
		if (AssetRegistry != nullptr && !AssetRegistry->IsLoadingAssets())
		{
			TArray<FName> Referencers;
			AssetRegistry->GetReferencers(DreamBlueprint->GetOutermost()->GetFName(), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
			TArray<FString> UnmigratedSequencePackages;
			for (const FName Referencer : Referencers)
			{
				if (FindPackage(nullptr, *Referencer.ToString()) != nullptr)
				{
					// Loaded: leg 4 already reached it through the object iterator.
					continue;
				}
				TArray<FAssetData> Assets;
				AssetRegistry->GetAssetsByPackageName(Referencer, Assets);
				for (const FAssetData& Asset : Assets)
				{
					if (Asset.AssetClassPath == UDreamUISequence::StaticClass()->GetClassPathName())
					{
						UnmigratedSequencePackages.Add(Referencer.ToString());
						break;
					}
				}
			}
			if (UnmigratedSequencePackages.Num() > 0)
			{
				MessageLog.Warning(*FString::Printf(
					TEXT("%d unloaded sequence asset(s) reference this class and did NOT take the rename hop: %s. Open them and recompile this Blueprint while the '(was:)' line is still in the file."),
					UnmigratedSequencePackages.Num(), *FString::Join(UnmigratedSequencePackages, TEXT(", "))));
			}
		}
	}
}

void FDreamWidgetBlueprintCompilerContext::PopulateBlueprintGeneratedVariables()
{
	Super::PopulateBlueprintGeneratedVariables();

	// The hierarchy has to BE the one the text says before anything counts what is in it, and the
	// walk below is the count. See the header for why this and not PreCompile: the list built here is
	// consumed twice later (the skeleton at STAGE VIII, the generated class at STAGE XII) and never
	// rebuilt in between, so a tree installed after this point is a tree the class declares the
	// PREVIOUS compile's variables for.
	//
	// One bag for the whole read, reported in one place afterwards, rather than a MessageLog call at
	// each failure site. The front end reports every mistake in a file rather than stopping at the
	// first -- a .dui is usually written whole, by a model, and five round trips for five typos is
	// what that design avoids -- and a MessageLog call at each site is how somebody eventually puts a
	// `return` next to one and quietly restores the early exit.
	FDreamUIDiagnosticBag TextDiagnostics;
	BuildWidgetTreeFromTextSource(TextDiagnostics);
	ReportTextDiagnostics(TextDiagnostics);
	// The same bag, delivered to editors that are not this one. A clean compile deposits an empty
	// entry on purpose: over there, that is what clears the file's squiggles.
	FDreamUIDiagnosticsMailbox::Deposit(TextDiagnostics);

	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	if (DreamBlueprint != nullptr)
	{
		TArray<UDreamWidget*> SourceWidgets;
		DreamBlueprint->GetAllSourceWidgets(SourceWidgets);

		// One member variable per authored widget, named by the shared rule. Declaring them here is
		// what makes a widget reachable from the graph AND what the runtime binds against -- the same
		// names, from the same function, which is the point.
		TSet<FName> DeclaredNames;
		for (const UDreamWidget* Widget : SourceWidgets)
		{
			const FName VariableName = MakeWidgetVariableName(Widget);
			if (VariableName.IsNone())
			{
				continue;
			}
			// Two widgets sharing a display name would silently collapse into one variable, and which
			// widget it ends up bound to would depend on tree order. Name it instead of picking.
			if (DeclaredNames.Contains(VariableName))
			{
				MessageLog.Warning(*FText::Format(
					LOCTEXT("DuplicateWidgetVariableName", "More than one widget is named \"{0}\"; only the first is exposed as a variable. Rename one of them."),
					FText::FromName(VariableName)).ToString());
				continue;
			}
			DeclaredNames.Add(VariableName);

			// A parent class that already declares this binding wins: a subclass re-declaring it would
			// shadow the parent's property and leave the parent's own code bound to nothing.
			if (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->FindPropertyByName(VariableName) != nullptr)
			{
				continue;
			}

			UClass* WidgetClass = Widget->GetClass();
			if (UBlueprintGeneratedClass* WidgetBlueprintClass = Cast<UBlueprintGeneratedClass>(WidgetClass))
			{
				// Recompiling a dependent asset otherwise captures a stale REINST class here.
				WidgetClass = WidgetBlueprintClass->GetAuthoritativeClass();
			}

			FBPVariableDescription WidgetVariable;
			WidgetVariable.VarName = VariableName;
			// Derived from the name rather than stored: it stays stable across recompiles and across
			// machines with nothing to keep in sync. Renaming a widget changes it, but a name-keyed
			// map (which is what UMG stores) has exactly that property too, so nothing is given up.
			WidgetVariable.VarGuid = FGuid::NewDeterministicGuid(VariableName.ToString());
			WidgetVariable.VarType = FEdGraphPinType(UEdGraphSchema_K2::PC_Object, NAME_None, WidgetClass, EPinContainerType::None, false, FEdGraphTerminalType());
			WidgetVariable.FriendlyName = Widget->GetDisplayName();
			WidgetVariable.PropertyFlags = (CPF_BlueprintVisible | CPF_BlueprintReadOnly | CPF_RepSkip | CPF_Transient | CPF_DuplicateTransient);
			WidgetVariable.SetMetaData(TEXT("Category"), *DreamBlueprint->GetName());

			DreamBlueprint->GeneratedVariables.Emplace(MoveTemp(WidgetVariable));
		}

		// One class variable per `resources` entry, which is what makes the block editable from the
		// Class Defaults panel and readable from the graph. DefaultValue is rebuilt from the file on
		// EVERY compile and the compiler applies it onto the final CDO after the old CDO's values
		// have been copied over -- so the FILE wins each compile, by construction. A panel edit is
		// not lost because the write-back carries it into the file the moment it is committed; a
		// panel edit made with the write-back broken is lost at the next compile, which is the same
		// contract every widget property in the designer already lives under.
		for (const FDreamUIResource& Entry : TextResources)
		{
			const FName VariableName(*Entry.Name);
			if (VariableName.IsNone() || DeclaredNames.Contains(VariableName))
			{
				// A resource sharing a widget's name would be two variables fighting for one slot.
				// The widget won above; the file's own duplicate-name diagnostics cover the rest.
				continue;
			}
			if (Blueprint->ParentClass != nullptr && Blueprint->ParentClass->FindPropertyByName(VariableName) != nullptr)
			{
				continue;
			}

			FEdGraphPinType PinType;
			FString DefaultValue;
			if (Entry.TypeName.Equals(TEXT("Number"), ESearchCase::IgnoreCase))
			{
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_Real, UEdGraphSchema_K2::PC_Double,
					nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
				// The lexer's number text is ImportText's number text; no reformatting to drift on.
				DefaultValue = Entry.Value.Raw;
			}
			else if (Entry.TypeName.Equals(TEXT("Color"), ESearchCase::IgnoreCase))
			{
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, NAME_None,
					TBaseStructure<FLinearColor>::Get(), EPinContainerType::None, false, FEdGraphTerminalType());
				FLinearColor Color = FLinearColor::White;
				DreamUIValueFormat::ParseColorHex(Entry.Value.Raw, Color);
				TBaseStructure<FLinearColor>::Get()->ExportText(DefaultValue, &Color, nullptr, nullptr, PPF_None, nullptr);
			}
			else if (Entry.TypeName.Equals(TEXT("Vector2"), ESearchCase::IgnoreCase))
			{
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_Struct, NAME_None,
					TBaseStructure<FVector2D>::Get(), EPinContainerType::None, false, FEdGraphTerminalType());
				FVector2D Vector = FVector2D::ZeroVector;
				if (Entry.Value.Elements.Num() == 2)
				{
					LexTryParseString(Vector.X, *Entry.Value.Elements[0]);
					LexTryParseString(Vector.Y, *Entry.Value.Elements[1]);
				}
				TBaseStructure<FVector2D>::Get()->ExportText(DefaultValue, &Vector, nullptr, nullptr, PPF_None, nullptr);
			}
			else if (Entry.TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase))
			{
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_String, NAME_None,
					nullptr, EPinContainerType::None, false, FEdGraphTerminalType());
				DefaultValue = Entry.Value.Raw;
			}
			else if (Entry.TypeName.Equals(TEXT("Asset"), ESearchCase::IgnoreCase))
			{
				// A soft OBJECT pin, not a path struct: the Class Defaults panel then offers the
				// asset picker, which is the whole reason an author would edit a resource there.
				PinType = FEdGraphPinType(UEdGraphSchema_K2::PC_SoftObject, NAME_None,
					UObject::StaticClass(), EPinContainerType::None, false, FEdGraphTerminalType());
				DefaultValue = Entry.Value.Raw;
			}
			else
			{
				// The builder already refused the entry (DUI4008); declaring a variable for it anyway
				// would put an untyped slot on the class for a value nothing can fill.
				continue;
			}
			DeclaredNames.Add(VariableName);

			FBPVariableDescription ResourceVariable;
			ResourceVariable.VarName = VariableName;
			ResourceVariable.VarGuid = FGuid::NewDeterministicGuid(VariableName.ToString());
			ResourceVariable.VarType = MoveTemp(PinType);
			ResourceVariable.FriendlyName = Entry.Name;
			// Editable on the CLASS, read-only in graphs and on instances: the block is a table of
			// constants, and an instance override would be a value the file cannot see and the next
			// compile cannot preserve.
			ResourceVariable.PropertyFlags =
				(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintReadOnly | CPF_DisableEditOnInstance);
			ResourceVariable.Category = FText::FromString(TEXT("Resources"));
			ResourceVariable.DefaultValue = MoveTemp(DefaultValue);
			DreamBlueprint->GeneratedVariables.Emplace(MoveTemp(ResourceVariable));
		}
	}
}

void FDreamWidgetBlueprintCompilerContext::UpdateGeneratedClassWidgetTree(UDreamWidgetBlueprint* InBlueprint, UDreamWidgetGeneratedClass* InClass)
{
	if (!IsValid(InBlueprint->WidgetTree))
	{
		return;
	}

	// A duplicate, never the authoring object itself. The class's archetype is instanced from on every
	// CreateDreamWidget; handing it the object the designer is editing would let an edit mutate the
	// template every live instance was built from, mid-session.
	const EObjectFlags PreviousFlags = InBlueprint->WidgetTree->GetFlags();
	InBlueprint->WidgetTree->ClearFlags(RF_ArchetypeObject);

	FObjectDuplicationParameters DupParams(InBlueprint->WidgetTree, InClass);
	DupParams.DestName = InBlueprint->WidgetTree->GetFName();
	DupParams.FlagMask = RF_AllFlags & ~RF_DefaultSubObject;
	DupParams.PortFlags |= PPF_DuplicateVerbatim;

	UDreamWidgetTree* NewWidgetTree = Cast<UDreamWidgetTree>(StaticDuplicateObjectEx(DupParams));
	InBlueprint->WidgetTree->SetFlags(PreviousFlags);

	if (NewWidgetTree != nullptr)
	{
		// Parent is DuplicateTransient, so the copy arrives structurally complete with empty
		// back-pointers. The archetype is walked by name during binding, so they have to be there.
		NewWidgetTree->RebuildParentLinks();
	}
	InClass->SetWidgetTreeArchetype(NewWidgetTree);

	if (OldWidgetTree != nullptr && NewWidgetTree != nullptr)
	{
		// An export still pointing at the previous archetype must resolve to the replacement, or a
		// dependent asset loaded mid-recompile keeps a tree that no class owns.
		FLinkerLoad::PRIVATE_PatchNewObjectIntoExport(OldWidgetTree, NewWidgetTree);
	}
	OldWidgetTree = nullptr;
}

void FDreamWidgetBlueprintCompilerContext::CompilePropertyBindings(UClass* InClass)
{
	UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint();
	UDreamWidgetGeneratedClass* GeneratedClass = Cast<UDreamWidgetGeneratedClass>(InClass);
	if (DreamBlueprint == nullptr || GeneratedClass == nullptr)
	{
		return;
	}
	// Skeleton-only compiles do not carry data onto the class; see ValidateWidgetBindings.
	if (CompileOptions.CompileType == EKismetCompileType::SkeletonOnly)
	{
		return;
	}

	UDreamWidgetTree* Archetype = IsValid(DreamBlueprint->WidgetTree) ? DreamBlueprint->WidgetTree : nullptr;
	TArray<FDreamWidgetPropertyBinding> Resolved;
	for (const FDreamWidgetPropertyBinding& Authored : DreamBlueprint->PropertyBindings)
	{
		const UDreamWidget* TargetWidget = Archetype != nullptr
			? Archetype->FindWidgetByVariableName(Authored.WidgetName) : nullptr;
		if (TargetWidget == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingWidgetNotFound", "The binding on \"{0}\" expects a widget named \"{1}\", and this hierarchy has none."),
				FText::FromName(Authored.PropertyName), FText::FromName(Authored.WidgetName)).ToString());
			continue;
		}
		// The same resolver the runtime uses: the compiler must check the object the runtime will
		// actually write to, or a binding passes here and finds nothing there.
		const UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Authored.Target, Authored.BehaviourIndex);
		if (Target == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingTargetNotFound", "The binding on \"{0}.{1}\" points at something \"{0}\" does not have."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.PropertyName)).ToString());
			continue;
		}
		const FProperty* TargetProperty = Target->GetClass()->FindPropertyByName(Authored.PropertyName);
		if (TargetProperty == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingPropertyNotFound", "\"{0}\" has no property named \"{1}\" to bind."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.PropertyName)).ToString());
			continue;
		}
		UFunction* Setter = FindDreamWidgetSetterFor(Target->GetClass(), TargetProperty);
		if (Setter == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingNoSetter", "\"{0}\" cannot be bound: {1} exposes no setter for it, so a bound value would be written but never take effect."),
				FText::FromName(Authored.PropertyName), FText::FromString(Target->GetClass()->GetName())).ToString());
			continue;
		}
		UFunction* SourceFunction = InClass->FindFunctionByName(Authored.FunctionName);
		if (SourceFunction == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingFunctionNotFound", "The binding on \"{0}.{1}\" calls \"{2}\", and this Blueprint has no such function."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.PropertyName),
				FText::FromName(Authored.FunctionName)).ToString());
			continue;
		}
		const FProperty* ReturnProperty = SourceFunction->GetReturnProperty();
		if (SourceFunction->NumParms != 1 || ReturnProperty == nullptr || !ReturnProperty->SameType(TargetProperty))
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("BindingFunctionWrongShape", "\"{0}\" has to take no arguments and return the type of \"{1}.{2}\" to bind to it."),
				FText::FromName(Authored.FunctionName), FText::FromName(Authored.WidgetName),
				FText::FromName(Authored.PropertyName)).ToString());
			continue;
		}

		FDreamWidgetPropertyBinding& Entry = Resolved.AddDefaulted_GetRef();
		Entry.WidgetName = Authored.WidgetName;
		Entry.Target = Authored.Target;
		Entry.BehaviourIndex = Authored.BehaviourIndex;
		Entry.PropertyName = Authored.PropertyName;
		Entry.FunctionName = Authored.FunctionName;
		Entry.SetterName = Setter->GetFName();
		Entry.NotifyField = Authored.NotifyField;
		if (!Authored.NotifyField.IsNone())
		{
			// The forward half of a `<->` pushes through the silent setter when there is one: the
			// full setter fires the control's changed event, which is the reverse route, which
			// writes the variable this push just read -- the echo dies here, not in a hope that
			// every control early-outs on an equal value.
			const FName SilentSetterName(*(Setter->GetFName().ToString() + TEXT("WithoutNotify")));
			if (Target->GetClass()->FindFunctionByName(SilentSetterName) != nullptr)
			{
				Entry.SetterName = SilentSetterName;
			}
		}
	}
	GeneratedClass->SetPropertyBindings(MoveTemp(Resolved));

	// The event half, with the check only this stage can make: the handler is a function on the
	// class being compiled, and the builder could not see that class. The signature test is the
	// delegate's own -- a handler that takes what the event sends, no more.
	TArray<FDreamWidgetEventBinding> ResolvedEvents;
	for (const FDreamWidgetEventBinding& Authored : DreamBlueprint->EventBindings)
	{
		const UDreamWidget* TargetWidget = Archetype != nullptr
			? Archetype->FindWidgetByVariableName(Authored.WidgetName) : nullptr;
		if (TargetWidget == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("EventWidgetNotFound", "The event route on \"{0}\" expects a widget named \"{1}\", and this hierarchy has none."),
				FText::FromName(Authored.EventName), FText::FromName(Authored.WidgetName)).ToString());
			continue;
		}
		const UObject* Target = ResolveDreamWidgetBindingTarget(TargetWidget, Authored.Target, Authored.BehaviourIndex);
		const FMulticastDelegateProperty* Event = Target != nullptr
			? CastField<FMulticastDelegateProperty>(Target->GetClass()->FindPropertyByName(Authored.EventName)) : nullptr;
		if (Event == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("EventNotOnTarget", "\"{0}\" has no event named \"{1}\" to route."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.EventName)).ToString());
			continue;
		}
		const UFunction* Handler = InClass->FindFunctionByName(Authored.FunctionName);
		if (Handler == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("EventHandlerNotFound", "\"{0}.{1}\" routes to \"{2}\", and this Blueprint has no such function."),
				FText::FromName(Authored.WidgetName), FText::FromName(Authored.EventName),
				FText::FromName(Authored.FunctionName)).ToString());
			continue;
		}
		if (Event->SignatureFunction != nullptr && !Handler->IsSignatureCompatibleWith(Event->SignatureFunction))
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("EventHandlerWrongShape", "\"{0}\" cannot handle \"{1}.{2}\": its parameters do not match the event's."),
				FText::FromName(Authored.FunctionName), FText::FromName(Authored.WidgetName),
				FText::FromName(Authored.EventName)).ToString());
			continue;
		}
		ResolvedEvents.Add(Authored);
	}
	GeneratedClass->SetEventBindings(MoveTemp(ResolvedEvents));

	// The `each` half, with the one check only this stage can make: the SOURCE lives on the class
	// being compiled -- a nullary function returning TArray of objects, or such an array variable.
	// The per-cell setters were vetted by the builder against the template's real classes, and the
	// host's view is a runtime fact the resolve checks again.
	TArray<FDreamWidgetEachBinding> ResolvedEach;
	for (const FDreamWidgetEachBinding& Authored : DreamBlueprint->EachBindings)
	{
		const FArrayProperty* ItemsProperty = nullptr;
		if (Authored.bSourceIsFunction)
		{
			const UFunction* Source = InClass->FindFunctionByName(Authored.SourceName);
			if (Source == nullptr || Source->NumParms != 1)
			{
				MessageLog.Error(*FString::Printf(
					TEXT("The 'each %s in %s()' block needs a no-argument function of that name on this Blueprint."),
					*Authored.LoopVariable.ToString(), *Authored.SourceName.ToString()));
				continue;
			}
			ItemsProperty = CastField<FArrayProperty>(Source->GetReturnProperty());
		}
		else
		{
			ItemsProperty = FindFProperty<FArrayProperty>(InClass, Authored.SourceName);
			if (ItemsProperty == nullptr)
			{
				MessageLog.Error(*FString::Printf(
					TEXT("The 'each %s in %s' block needs an array variable of that name on this Blueprint."),
					*Authored.LoopVariable.ToString(), *Authored.SourceName.ToString()));
				continue;
			}
		}
		if (ItemsProperty == nullptr || CastField<FObjectPropertyBase>(ItemsProperty->Inner) == nullptr)
		{
			MessageLog.Error(*FString::Printf(
				TEXT("'%s' must supply an array of OBJECTS -- the item bindings read members off each element by reflection."),
				*Authored.SourceName.ToString()));
			continue;
		}
		ResolvedEach.Add(Authored);
	}
	GeneratedClass->SetEachBindings(MoveTemp(ResolvedEach));
}

void FDreamWidgetBlueprintCompilerContext::ValidateWidgetBindings(UClass* InClass)
{
	// The AUTHORING tree, not the class's copy of it. They are the same thing on a full compile -- the
	// copy was made a few lines ago -- but a skeleton-only compile deliberately does not make one, and
	// reading the class there would report every binding as broken on every keystroke in the designer.
	UDreamWidgetTree* Archetype = nullptr;
	if (UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint())
	{
		if (IsValid(DreamBlueprint->WidgetTree) && IsValid(DreamBlueprint->WidgetTree->RootWidget))
		{
			Archetype = DreamBlueprint->WidgetTree;
		}
	}
	if (Archetype == nullptr && InClass != nullptr)
	{
		// Nothing authored here: a subclass that only adds logic inherits its parent's hierarchy, and
		// its bindings have to be checked against that.
		Archetype = UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(InClass->GetSuperClass());
	}
	if (Archetype == nullptr)
	{
		// No hierarchy at all is a legitimate state (logic-only class, or nothing authored yet).
		// Reporting every native binding as broken here would bury the real errors.
		return;
	}

	// The reason the class model is worth the trouble. A native subclass declaring a widget binding
	// that no widget answers used to fail at RUN time, as a null, after a save had already dropped it.
	// Here it is an error at compile time, on the asset, with the name in the message.
	//
	// Only properties that SAY they are bindings, via meta=(BindDreamWidget). Raising an error means
	// asserting intent, and intent cannot be inferred from the shape of a property: the first attempt
	// here treated "transient and widget-typed" as the marker and promptly flagged
	// UDreamWidget::Parent, which is both of those and is not a binding. A widget-typed member with no
	// marker is somebody's own reference and none of this pass's business.
	for (TFieldIterator<FObjectPropertyBase> It(InClass, EFieldIterationFlags::IncludeSuper); It; ++It)
	{
		FObjectPropertyBase* Property = *It;
		if (Property->PropertyClass == nullptr || !Property->PropertyClass->IsChildOf(UDreamWidget::StaticClass()))
		{
			continue;
		}
		if (!Property->HasMetaData(UDreamWidgetGeneratedClass::BindWidgetMetaName))
		{
			continue;
		}

		const UDreamWidget* Match = Archetype->FindWidgetByVariableName(Property->GetFName());
		if (Match == nullptr)
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("WidgetBindingNotFound", "\"{0}\" is declared meta=(BindDreamWidget), so this hierarchy must contain a widget of that name, and it has none. Rename a widget to match, or drop the specifier."),
				FText::FromName(Property->GetFName())).ToString());
		}
		else if (!Match->IsA(Property->PropertyClass))
		{
			MessageLog.Error(*FText::Format(
				LOCTEXT("WidgetBindingWrongType", "\"{0}\" is declared meta=(BindDreamWidget) as {1}, but the widget of that name is {2}."),
				FText::FromName(Property->GetFName()),
				FText::FromString(Property->PropertyClass->GetName()),
				FText::FromString(Match->GetClass()->GetName())).ToString());
		}
	}

	ValidateNamedSlotBindings(Archetype);
	ValidateAnimationBindings(Archetype);
}

void FDreamWidgetBlueprintCompilerContext::ValidateNamedSlotBindings(UDreamWidgetTree* InArchetype)
{
	if (!IsValid(InArchetype))
	{
		return;
	}
	// Every nested widget blueprint instance in this hierarchy, checked against the slots its own
	// class declares. A slot the class removed or renamed leaves the host still holding content for a
	// name nobody answers, and the runtime's only options are to drop it or to guess -- so it is
	// reported here, on the asset that can fix it, with both names in the message.
	InArchetype->ForEachWidget([this](UDreamWidget* Widget)
	{
		UDreamUserWidget* Nested = Cast<UDreamUserWidget>(Widget);
		if (Nested == nullptr || Nested->NamedSlotContent.Num() == 0)
		{
			return;
		}
		TArray<FName> Declared;
		UDreamUserWidget::CollectDeclaredSlotNames(
			UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(Nested->GetClass()), Declared);
		for (const TPair<FName, TObjectPtr<UDreamWidget>>& Binding : Nested->NamedSlotContent)
		{
			if (!Declared.Contains(Binding.Key))
			{
				MessageLog.Error(*FText::Format(
					LOCTEXT("NamedSlotNotDeclared", "\"{0}\" has content bound to a slot named \"{1}\", and {2} declares no such slot."),
					FText::FromString(Nested->GetDisplayName()),
					FText::FromName(Binding.Key),
					FText::FromString(Nested->GetClass()->GetName())).ToString());
			}
		}
	});
}

void FDreamWidgetBlueprintCompilerContext::ValidateAnimationBindings(UDreamWidgetTree* InArchetype)
{
	if (!IsValid(InArchetype))
	{
		return;
	}
	// Embedded animations only -- the ones living in a sequence component on a widget of THIS
	// hierarchy, whose paths were recorded against that widget. UDreamUISequence assets on the same
	// component are deliberately left alone: they are authored against their own PreviewWidgetClass
	// and being reusable across widget classes is the point of them, so a path that this hierarchy
	// cannot walk says nothing about the asset.
	InArchetype->ForEachWidget([this](UDreamWidget* ContextWidget)
	{
		for (UDreamUIBehaviour* Component : ContextWidget->GetAllComponents())
		{
			UDreamWidgetAnimationComponent* Animator = Cast<UDreamWidgetAnimationComponent>(Component);
			if (Animator == nullptr)
			{
				continue;
			}
			for (UDreamWidgetAnimation* Animation : Animator->GetSequenceArray())
			{
				if (!IsValid(Animation))
				{
					continue;
				}
				TArray<TPair<FGuid, FString>> Unresolvable;
				// The context is the widget the component hangs off, matching the context
				// BindPossessableObject recorded against and the one playback resolves through.
				Animation->GetUnresolvableBindingPaths(ContextWidget, Unresolvable);
				UMovieScene* MovieScene = Animation->GetMovieScene();
				for (const TPair<FGuid, FString>& Broken : Unresolvable)
				{
					// The possessable's name is what the author sees as a track label. Falling back
					// to the raw guid is not much, but a message that names neither the track nor a
					// path would leave somebody diffing bindings by hand.
					FMovieScenePossessable* Possessable = MovieScene != nullptr ? MovieScene->FindPossessable(Broken.Key) : nullptr;
					const FString TrackName = Possessable != nullptr ? Possessable->GetName() : Broken.Key.ToString(EGuidFormats::DigitsWithHyphens);

					if (Broken.Value.IsEmpty())
					{
						MessageLog.Error(*FText::Format(
							LOCTEXT("AnimationBindingHasNoPath", "Animation \"{0}\" binds \"{1}\" without recording a widget path, so nothing but the authoring hierarchy will ever resolve it. Rebind that track against \"{2}\"."),
							FText::FromString(Animation->GetDisplayNameString()),
							FText::FromString(TrackName),
							FText::FromString(ContextWidget->GetDisplayName())).ToString());
					}
					else
					{
						MessageLog.Error(*FText::Format(
							LOCTEXT("AnimationBindingPathNotFound", "Animation \"{0}\" drives \"{1}\" through the path \"{2}\", and \"{3}\" has nothing there. A widget's display name is also its animation path, so renaming one leaves every track bound to it naming a widget that no longer exists. Rename the widget back, or rebind the track."),
							FText::FromString(Animation->GetDisplayNameString()),
							FText::FromString(TrackName),
							FText::FromString(Broken.Value),
							FText::FromString(ContextWidget->GetDisplayName())).ToString());
					}
				}
			}
		}
	});
}

void FDreamWidgetBlueprintCompilerContext::FinishCompilingClass(UClass* Class)
{
	// A skeleton compile exists to give the graph its members back as fast as possible, and the
	// designer triggers one on every structural edit -- every drag, every delete. Duplicating the
	// whole hierarchy onto a class nobody instantiates would put that cost on each of them, and would
	// leave an archetype on the skeleton class that only invites something to read the wrong one.
	// UMG skips the same work for the same reason.
	const bool bIsSkeletonOnly = CompileOptions.CompileType == EKismetCompileType::SkeletonOnly;
	if (!bIsSkeletonOnly)
	{
		if (UDreamWidgetBlueprint* DreamBlueprint = DreamWidgetBlueprint())
		{
			if (UDreamWidgetGeneratedClass* DreamClass = Cast<UDreamWidgetGeneratedClass>(Class))
			{
				UpdateGeneratedClassWidgetTree(DreamBlueprint, DreamClass);
			}
		}
	}

	Super::FinishCompilingClass(Class);

	// After the base pass, so the properties being checked against actually exist on the class.
	ValidateWidgetBindings(Class);
	CompilePropertyBindings(Class);
}

#undef LOCTEXT_NAMESPACE
