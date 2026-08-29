// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintCompiler.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"

#include "EdGraph/EdGraph.h"
#include "HAL/FileManager.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "UObject/Package.h"

/*
 * `(was: OldId)` -- carrying a rename across the three things an id is.
 *
 * A widget's id is its class member variable (and therefore every graph node that reads it), the
 * WidgetName key of a property binding, and the '/'-joined display-name path an embedded animation
 * resolves through. Renaming a node in a .dui breaks all three at once, and before P0 only one of
 * them said so. These tests are about the repair.
 *
 * They come in two shapes, deliberately, because the three legs are not equally reachable through a
 * compile TODAY:
 *
 *   - The graph leg is asserted end to end, through a real .dui and a real compile, because the
 *     graph is a persistent part of the asset that a text rebuild does not touch.
 *   - The binding and animation legs are asserted against MigrateWidgetRename directly. Not for
 *     convenience: the text builder REPLACES UDreamWidgetBlueprint::PropertyBindings and REPLACES
 *     the widget tree on every compile, so under a .dui the binding list always already says the new
 *     name and the freshly built tree carries no animation at all. Driving those legs through a
 *     compile would assert nothing -- both would pass with the function body deleted. The direct
 *     tests put a binding and an animation in front of the fixup and check that it moves them, which
 *     is the claim that will still be true the day a rebuild stops throwing them away.
 *
 * The animation leg does get an independent oracle: GetUnresolvableBindingPaths is the exact call
 * the compiler's own validation makes, so "the path resolves again" is checked with the same
 * question that would have failed the compile.
 */

namespace DreamUITextMigrationTestLocal
{
	/** A .dui that has really been to disk, and is gone again when the test returns. */
	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
		}

		~FScopedDuiFile()
		{
			// Quiet and EvenReadOnly: a leftover file is read by the NEXT run of this test, which turns
			// a failure here into a failure over there with nothing connecting them.
			IFileManager::Get().Delete(*FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}

		FString FilePath;
	};

	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName, UClass* InParentClass = UDreamTextUserWidget::StaticClass())
		{
			const FString PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName);
			Package = CreatePackage(*PackageName);
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				InParentClass, Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedBlueprint(const FScopedBlueprint&) = delete;
		FScopedBlueprint& operator=(const FScopedBlueprint&) = delete;

		/** Point the class at a .dui, the way the Class Defaults panel does: on the CDO. */
		bool SetDuiFilePath(const FString& InFilePath) const
		{
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults == nullptr)
			{
				return false;
			}
			Defaults->DUI_File_Path.FilePath = InFilePath;
			return true;
		}

		UDreamWidget* AddWidget(const TCHAR* InDisplayName, UDreamWidget* InParent = nullptr) const
		{
			UDreamWidgetTree* Tree = Blueprint->GetOrCreateWidgetTree();
			UDreamWidget* Widget = Tree->ConstructWidget<UDreamWidget>();
			Widget->SetDisplayName(InDisplayName);
			Widget->SetParentBeforeRegister(InParent != nullptr ? InParent : Tree->RootWidget.Get());
			return Widget;
		}

		/** A widget with a UDreamText visual, so it owns a Text property something can be bound to. */
		UDreamWidget* AddTextWidget(const TCHAR* InDisplayName, UDreamWidget* InParent = nullptr) const
		{
			UDreamWidget* Widget = AddWidget(InDisplayName, InParent);
			Widget->CreateNewVisual(UDreamText::StaticClass());
			return Widget;
		}

		UEdGraph* EventGraph() const
		{
			return Blueprint != nullptr && Blueprint->UbergraphPages.Num() > 0
				? Blueprint->UbergraphPages[0].Get() : nullptr;
		}
	};

	/**
	 * A variable-get node in the event graph, referencing InVariableName as a self member.
	 *
	 * The reference is set by hand rather than by dragging a variable out of a palette, which is the
	 * only way to do it headlessly -- but it is the SAME field a real node stores: FMemberReference
	 * with bSelfContext, which is what UK2Node_Variable::HandleVariableRenamed reads and rewrites.
	 *
	 * Call this only AFTER a compile that declares the variable. AllocateDefaultPins asks the class
	 * for the property to decide the pin's type, so a node made before the variable exists comes out
	 * pinless and stops resembling the thing under test.
	 */
	UK2Node_VariableGet* AddVariableGetNode(const FScopedBlueprint& InFixture, const TCHAR* InVariableName)
	{
		UEdGraph* Graph = InFixture.EventGraph();
		if (Graph == nullptr)
		{
			return nullptr;
		}
		UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
		Node->VariableReference.SetSelfMember(FName(InVariableName));
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Graph->AddNode(Node, /*bUserAction*/false, /*bSelectNewNode*/false);
		return Node;
	}

	/** The first variable-get node in the event graph, re-found rather than remembered. */
	UK2Node_VariableGet* FindVariableGetNode(const FScopedBlueprint& InFixture)
	{
		UEdGraph* Graph = InFixture.EventGraph();
		if (Graph == nullptr)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_VariableGet* Getter = Cast<UK2Node_VariableGet>(Node))
			{
				return Getter;
			}
		}
		return nullptr;
	}

	/**
	 * Put an animation on the hierarchy's root and bind one track to InTarget, the way the sequencer
	 * does: a possessable in the movie scene, then BindPossessableObject with the ROOT as context,
	 * which is what makes the recorded path relative to the animation's owner rather than to the
	 * widget being animated.
	 */
	FGuid BindAnimationToWidget(const FScopedBlueprint& InFixture, UDreamWidget* InTarget, UDreamWidgetAnimation*& OutSequence)
	{
		OutSequence = nullptr;
		UDreamWidget* Root = InFixture.Blueprint->GetOrCreateWidgetTree()->RootWidget.Get();
		if (Root == nullptr || InTarget == nullptr)
		{
			return FGuid();
		}
		UDreamWidgetAnimationComponent* Animator =
			Cast<UDreamWidgetAnimationComponent>(Root->AddComponent(UDreamWidgetAnimationComponent::StaticClass()));
		if (Animator == nullptr)
		{
			return FGuid();
		}
		OutSequence = Animator->AddNewAnimation();
		if (OutSequence == nullptr || OutSequence->GetMovieScene() == nullptr)
		{
			return FGuid();
		}
		const FGuid BindingId = OutSequence->GetMovieScene()->AddPossessable(InTarget->GetDisplayName(), InTarget->GetClass());
		OutSequence->BindPossessableObject(BindingId, *InTarget, Root);
		return BindingId;
	}

	void Compile(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& OutResults)
	{
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &OutResults);
	}

	FString JoinMessages(const FCompilerResultsLog& InResults)
	{
		FString All;
		for (const TSharedRef<FTokenizedMessage>& Message : InResults.Messages)
		{
			All += Message->ToText().ToString() + TEXT(" | ");
		}
		return All;
	}

	/**
	 * Assert the compile said InNeedle.
	 *
	 * The dump goes out through AddInfo and never into the assertion's description: every test that
	 * declares an expected error matches by substring, and a description repeating the same text is
	 * swallowed by that entry, leaving a failure that prints nothing at all.
	 */
	bool TestMessagesContain(FAutomationTestBase& InTest, const TCHAR* InWhat,
		const FCompilerResultsLog& InResults, const FString& InNeedle)
	{
		const FString All = JoinMessages(InResults);
		const bool bFound = All.Contains(InNeedle);
		InTest.TestTrue(InWhat, bFound);
		if (!bFound)
		{
			InTest.AddInfo(FString::Printf(TEXT("the compile said: %s"), *All));
		}
		return bFound;
	}

	bool TestMessagesDoNotContain(FAutomationTestBase& InTest, const TCHAR* InWhat,
		const FCompilerResultsLog& InResults, const FString& InNeedle)
	{
		const FString All = JoinMessages(InResults);
		const bool bAbsent = !All.Contains(InNeedle);
		InTest.TestTrue(InWhat, bAbsent);
		if (!bAbsent)
		{
			InTest.AddInfo(FString::Printf(TEXT("the compile said: %s"), *All));
		}
		return bAbsent;
	}

	/** The path an animation recorded for one binding, or a marker saying it has none. */
	FString RecordedPath(UDreamWidgetAnimation* InAnimation, UDreamWidget* InContext, const FGuid& InBindingId)
	{
		TArray<TPair<FGuid, FString>> Unresolvable;
		if (InAnimation != nullptr)
		{
			InAnimation->GetUnresolvableBindingPaths(InContext, Unresolvable);
		}
		for (const TPair<FGuid, FString>& Entry : Unresolvable)
		{
			if (Entry.Key == InBindingId)
			{
				return Entry.Value;
			}
		}
		return FString(TEXT("<resolves>"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWasClauseMigratesAllThreeTest,
	"DreamGUI.WidgetBlueprint.AWasClauseMigratesGraphBindingAndAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWasClauseMigratesAllThreeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextMigrationTestLocal;

	// The parent declares GetTitleText() natively, so the property binding below is one the compiler
	// can actually resolve -- a binding to a function that does not exist would fail the compile for
	// a reason that has nothing to do with renaming.
	FScopedBlueprint Fixture(TEXT("BP_WasMigratesAll"), UDreamTextUserWidgetBindingBase::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;

	UDreamWidget* Panel = Fixture.AddWidget(TEXT("Panel"));
	UDreamWidget* Target = Fixture.AddTextWidget(TEXT("OkLabel"), Panel);
	UDreamWidget* Root = Fixture.Blueprint->GetOrCreateWidgetTree()->RootWidget.Get();
	if (!TestNotNull(TEXT("the hierarchy has a root"), Root)) return false;

	// Two levels deep on purpose. A one-segment path would pass even if the fixup replaced the whole
	// path rather than one step of it, and "Panel/OkLabel" is the shape that catches that.
	UDreamWidgetAnimation* Sequence = nullptr;
	const FGuid BindingId = BindAnimationToWidget(Fixture, Target, Sequence);
	if (!TestTrue(TEXT("the animation binding was authored"), BindingId.IsValid())) return false;

	FDreamWidgetPropertyBinding& Binding = Fixture.Blueprint->PropertyBindings.AddDefaulted_GetRef();
	Binding.WidgetName = FName(TEXT("OkLabel"));
	Binding.Target = EDreamWidgetBindingTarget::Visual;
	Binding.PropertyName = FName(TEXT("Text"));
	Binding.FunctionName = FName(TEXT("GetTitleText"));

	// Compile first, so the class declares OkLabel and the graph node below can be built against a
	// property that exists.
	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the hierarchy compiles clean before the rename"), Results.NumErrors, 0))
		{
			AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
			return false;
		}
	}

	UK2Node_VariableGet* Getter = AddVariableGetNode(Fixture, TEXT("OkLabel"));
	if (!TestNotNull(TEXT("a graph node references the widget"), Getter)) return false;
	TestEqual(TEXT("by the name the class declares"), Getter->GetVarName(), FName(TEXT("OkLabel")));

	// The state before, asserted rather than assumed -- these are the three things that have to move,
	// and a test that only looked afterwards would pass if they had never been set up.
	TestEqual(TEXT("the animation recorded the two-segment path"),
		RecordedPath(Sequence, Root, BindingId), FString(TEXT("<resolves>")));
	TestEqual(TEXT("and the binding names the widget"),
		Fixture.Blueprint->PropertyBindings[0].WidgetName, FName(TEXT("OkLabel")));

	// The rename itself, which is what a .dui compile does before calling the migration: the tree is
	// already the new one, and every reference still says the old name.
	Target->SetDisplayName(TEXT("OkBtn"));
	TestEqual(TEXT("renaming the widget orphans the animation path"),
		RecordedPath(Sequence, Root, BindingId), FString(TEXT("Panel/OkLabel")));

	// The precondition the real call site has and this harness does not.
	//
	// HandleVariableRenamed rewrites the NAME and leaves MemberGuid alone, and FMemberReference::
	// ResolveMember looks the guid up and rewrites the name BACK from whatever it finds
	// (MemberReference.cpp:468). The only table it searches is UBlueprint::GeneratedVariables, and at
	// the real call site ResetAndPopulateBlueprintGeneratedVariables has just emptied it -- which is
	// why the migration runs where it does and not one stage later.
	//
	// Driving the function directly skips that, so the first version of this test watched the rename
	// land and then get reverted on the next GetVarName(). Emptying the list here is not making the
	// test pass: it is giving the function the state it is documented to require. The end-to-end case
	// (AWasClauseInADuiMovesTheGraphReferenceThroughACompile) is what proves the real path arranges it.
	Fixture.Blueprint->GeneratedVariables.Empty();

	const FDreamWidgetBlueprintCompilerContext::FWidgetRenameMigration Migration =
		FDreamWidgetBlueprintCompilerContext::MigrateWidgetRename(Fixture.Blueprint, TEXT("OkLabel"), TEXT("OkBtn"));

	TestEqual(TEXT("nothing refused the graph leg"), Migration.GraphRefusal, FString());
	TestEqual(TEXT("the graph reference moved"), Migration.GraphReferences, 1);
	TestEqual(TEXT("so did the property binding"), Migration.PropertyBindings, 1);
	TestEqual(TEXT("and the animation path"), Migration.AnimationBindings, 1);

	// 1. The graph. The node's own FMemberReference, not a count: a fixup that reported success and
	//    left the reference alone is exactly the failure this leg exists to prevent.
	if (UK2Node_VariableGet* Moved = FindVariableGetNode(Fixture))
	{
		TestEqual(TEXT("the graph node now reads the new variable"), Moved->GetVarName(), FName(TEXT("OkBtn")));
	}

	// 2. The binding.
	if (TestEqual(TEXT("the binding list still has one entry"), Fixture.Blueprint->PropertyBindings.Num(), 1))
	{
		TestEqual(TEXT("retargeted at the new name"),
			Fixture.Blueprint->PropertyBindings[0].WidgetName, FName(TEXT("OkBtn")));
		TestEqual(TEXT("and nothing else about it touched"),
			Fixture.Blueprint->PropertyBindings[0].FunctionName, FName(TEXT("GetTitleText")));
	}

	// 3. The animation, asked with the same question the compiler's validation asks. "<resolves>"
	//    here means GetUnresolvableBindingPaths no longer lists this binding at all.
	TestEqual(TEXT("the animation path walks the hierarchy again"),
		RecordedPath(Sequence, Root, BindingId), FString(TEXT("<resolves>")));
	if (Sequence != nullptr && Sequence->GetMovieScene() != nullptr)
	{
		if (FMovieScenePossessable* Possessable = Sequence->GetMovieScene()->FindPossessable(BindingId))
		{
			// The track label. Not the binding -- playback never reads it -- but a label still saying
			// OkLabel is the one part of the rename a human would see and not believe.
			TestEqual(TEXT("and the track label went with it"), Possessable->GetName(), FString(TEXT("OkBtn")));
		}
	}

	// And it all still compiles, which is the end-to-end statement: the animation validation that
	// fails a compile over an orphaned path is the same call RecordedPath just asked by hand.
	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the migrated hierarchy compiles clean"), Results.NumErrors, 0))
		{
			AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWasClauseThroughACompileTest,
	"DreamGUI.WidgetBlueprint.AWasClauseInADuiMovesTheGraphReferenceThroughACompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWasClauseThroughACompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextMigrationTestLocal;

	FScopedDuiFile Source(TEXT("WasClause.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text OkLabel {"),
		TEXT("        FontSize = 18"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	FScopedBlueprint Fixture(TEXT("BP_WasClause"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the .dui compiles clean"), Results.NumErrors, 0))
		{
			AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
			return false;
		}
		TestNotNull(TEXT("and declares the node's id as a variable"),
			Fixture.Blueprint->GeneratedClass->FindPropertyByName(FName(TEXT("OkLabel"))));
	}

	if (!TestNotNull(TEXT("a graph node references it"), AddVariableGetNode(Fixture, TEXT("OkLabel")))) return false;

	// The edit under test: one word in the file, plus the clause that says what it used to be.
	if (!TestTrue(TEXT("the fixture renamed the node in the file"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text OkBtn (was: OkLabel) {"),
		TEXT("        FontSize = 18"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	if (!TestEqual(TEXT("a rename with a was-clause compiles clean"), Results.NumErrors, 0))
	{
		AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
		return false;
	}

	// The class declares the new name and not the old one -- ordinary text-pipeline behaviour, and
	// the reason the graph reference would otherwise be dangling.
	TestNotNull(TEXT("the class declares the new id"),
		Fixture.Blueprint->GeneratedClass->FindPropertyByName(FName(TEXT("OkBtn"))));
	TestNull(TEXT("and not the old one"),
		Fixture.Blueprint->GeneratedClass->FindPropertyByName(FName(TEXT("OkLabel"))));

	// The claim: the graph followed. Re-found rather than remembered, because a compile is entitled
	// to replace nodes, and a pointer captured before it would prove nothing about what is in the
	// graph now.
	UK2Node_VariableGet* Moved = FindVariableGetNode(Fixture);
	if (TestNotNull(TEXT("the graph node survived the compile"), Moved))
	{
		TestEqual(TEXT("and now reads the new variable"), Moved->GetVarName(), FName(TEXT("OkBtn")));
	}

	// And the author is told the clause has done its job. This is the whole user-facing half of P7:
	// without it the line stays in the file forever, and a stale (was:) is what makes the next
	// rename ambiguous.
	TestMessagesContain(*this, TEXT("the compile says the clause can be deleted"), Results, TEXT("can be deleted"));
	TestMessagesContain(*this, TEXT("naming the file it is in"), Results, FPaths::GetCleanFilename(Source.FilePath));

	// Compiling again with the clause still in place must not fail, must not migrate anything a
	// second time, and must not start reporting a problem. This is the state every .dui is in between
	// the rename and the author getting round to deleting the line.
	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("recompiling with the clause still there stays clean"), SecondResults.NumErrors, 0);
	TestMessagesContain(*this, TEXT("and says there is nothing left to do"), SecondResults, TEXT("already been applied"));
	TestMessagesDoNotContain(*this, TEXT("without refusing anything"), SecondResults, TEXT("could not take the graph references"));
	if (UK2Node_VariableGet* StillMoved = FindVariableGetNode(Fixture))
	{
		TestEqual(TEXT("and the reference stays where the first compile put it"),
			StillMoved->GetVarName(), FName(TEXT("OkBtn")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIWasClauseNameStillTakenTest,
	"DreamGUI.WidgetBlueprint.ARenameClaimingANameStillInUseIsRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIWasClauseNameStillTakenTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextMigrationTestLocal;

	// The author renamed one node and created another with the old name, in one edit. Both names are
	// live, so a reference to OkLabel could honestly mean either -- and migrating anyway would move
	// every one of them OFF the node that legitimately carries that name today.
	FScopedDuiFile Source(TEXT("WasNameTaken.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text OkBtn (was: OkLabel) {"),
		TEXT("    }"),
		TEXT("    Text OkLabel {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	FScopedBlueprint Fixture(TEXT("BP_WasNameTaken"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

	AddExpectedError(TEXT("is still called that"), EAutomationExpectedErrorFlags::Contains, 0);

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestTrue(TEXT("a rename onto a name still in use fails the compile"), Results.NumErrors > 0);
	// The other node's LINE, because that is the only thing that tells the author which of the two
	// nodes they have to deal with first. Line 4 and not 5: a node's recorded position is its TYPE
	// token, which is where a reader's eye goes and where an editor should put the cursor.
	TestMessagesContain(*this, TEXT("naming the line the old name is still on"), Results, TEXT("line 4"));

	// Both nodes are still built -- refusing the migration is not refusing the hierarchy, and a
	// designer emptied by a bad clause would send the author looking in the wrong place entirely.
	if (UDreamWidgetTree* Tree = Fixture.Blueprint->WidgetTree)
	{
		TestEqual(TEXT("and the file's hierarchy is still built"), Tree->CountWidgets(), 3);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITwoClausesOneOldNameTest,
	"DreamGUI.WidgetBlueprint.TwoNodesClaimingTheSameOldNameAreRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITwoClausesOneOldNameTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextMigrationTestLocal;

	// One old name, two claimants. There is no answer: the references belong to exactly one of them
	// and the file does not say which, so guessing would silently put a graph on the wrong widget.
	FScopedDuiFile Source(TEXT("WasTwoClaims.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text OkBtn (was: OkLabel) {"),
		TEXT("    }"),
		TEXT("    Text ConfirmBtn (was: OkLabel) {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	FScopedBlueprint Fixture(TEXT("BP_WasTwoClaims"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

	AddExpectedError(TEXT("One old name cannot become two new ones"), EAutomationExpectedErrorFlags::Contains, 0);

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	TestTrue(TEXT("two clauses claiming one old name fail the compile"), Results.NumErrors > 0);
	TestMessagesContain(*this, TEXT("naming the line of the first claim"), Results, TEXT("line 2"));

	// All or nothing: the first clause is not quietly applied on the way to reporting the second.
	// A half-applied set leaves the asset in a state neither version of the file describes, and the
	// next compile then starts from that rather than from what the author wrote.
	TestMessagesDoNotContain(*this, TEXT("and nothing was migrated on the way"), Results, TEXT("took over from"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIStaleWasClauseTest,
	"DreamGUI.WidgetBlueprint.AWasClauseWithNothingLeftToMigrateIsNotAnError",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIStaleWasClauseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextMigrationTestLocal;

	// The ordinary steady state, and the one that must never be loud: the migration ran on some
	// earlier compile and the author has not deleted the line yet. Nothing in the asset answers to
	// the old name any more, which is not a mistake -- it is what success looks like afterwards.
	FScopedDuiFile Source(TEXT("WasStale.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    Text OkBtn (was: NeverExisted) {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}

	FScopedBlueprint Fixture(TEXT("BP_WasStale"));
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
	if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

	FCompilerResultsLog Results;
	Compile(Fixture.Blueprint, Results);
	if (!TestEqual(TEXT("a clause with nothing to migrate does not fail the compile"), Results.NumErrors, 0))
	{
		AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
	}
	// Nor a warning. A warning here would fire on every compile of every asset whose author has not
	// yet tidied up, which is how a build log becomes something nobody reads.
	TestEqual(TEXT("nor warn about it"), Results.NumWarnings, 0);

	// It is not silent, though: one note, saying the line is now safe to delete. That is the only
	// signal an author gets that the clause has finished its job.
	TestMessagesContain(*this, TEXT("but says the line can be deleted"), Results, TEXT("already been applied"));

	// And the hierarchy is exactly what the file says, untouched by the dead clause.
	if (UDreamWidgetTree* Tree = Fixture.Blueprint->WidgetTree)
	{
		TestEqual(TEXT("the hierarchy is what the file says"), Tree->CountWidgets(), 2);
	}
	TestNotNull(TEXT("under the new id"),
		Fixture.Blueprint->GeneratedClass->FindPropertyByName(FName(TEXT("OkBtn"))));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUINoWasClauseMigratesNothingTest,
	"DreamGUI.WidgetBlueprint.NothingIsMigratedWithoutAWasClause",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUINoWasClauseMigratesNothingTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextMigrationTestLocal;

	// The negative control, in two halves.
	//
	// The first is the important one: an asset with all three kinds of reference, compiled with no
	// .dui anywhere in sight. The migration hangs off the text read, which every compile of every
	// DreamUI Blueprint goes through, so "it does nothing when there is nothing to do" is a claim
	// about assets that have nothing to do with text authoring -- and a fixup that fired on those
	// would repoint graphs on assets nobody touched.
	{
		FScopedBlueprint Fixture(TEXT("BP_NoWasHandAuthored"), UDreamTextUserWidgetBindingBase::StaticClass());
		if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;

		UDreamWidget* Panel = Fixture.AddWidget(TEXT("Panel"));
		UDreamWidget* Target = Fixture.AddTextWidget(TEXT("OkLabel"), Panel);
		UDreamWidget* Root = Fixture.Blueprint->GetOrCreateWidgetTree()->RootWidget.Get();
		if (!TestNotNull(TEXT("the hierarchy has a root"), Root)) return false;

		UDreamWidgetAnimation* Sequence = nullptr;
		const FGuid BindingId = BindAnimationToWidget(Fixture, Target, Sequence);
		if (!TestTrue(TEXT("the animation binding was authored"), BindingId.IsValid())) return false;

		FDreamWidgetPropertyBinding& Binding = Fixture.Blueprint->PropertyBindings.AddDefaulted_GetRef();
		Binding.WidgetName = FName(TEXT("OkLabel"));
		Binding.Target = EDreamWidgetBindingTarget::Visual;
		Binding.PropertyName = FName(TEXT("Text"));
		Binding.FunctionName = FName(TEXT("GetTitleText"));

		{
			FCompilerResultsLog Results;
			Compile(Fixture.Blueprint, Results);
			if (!TestEqual(TEXT("it compiles clean"), Results.NumErrors, 0))
			{
				AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
				return false;
			}
		}

		if (!TestNotNull(TEXT("a graph node references the widget"), AddVariableGetNode(Fixture, TEXT("OkLabel")))) return false;

		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		TestEqual(TEXT("recompiling stays clean"), Results.NumErrors, 0);
		TestMessagesDoNotContain(*this, TEXT("and nothing claims to have migrated"), Results, TEXT("took over from"));

		if (UK2Node_VariableGet* Getter = FindVariableGetNode(Fixture))
		{
			TestEqual(TEXT("the graph reference is untouched"), Getter->GetVarName(), FName(TEXT("OkLabel")));
		}
		if (TestEqual(TEXT("the binding list is untouched"), Fixture.Blueprint->PropertyBindings.Num(), 1))
		{
			TestEqual(TEXT("still naming the same widget"),
				Fixture.Blueprint->PropertyBindings[0].WidgetName, FName(TEXT("OkLabel")));
		}
		TestEqual(TEXT("and the animation path still resolves"),
			RecordedPath(Sequence, Root, BindingId), FString(TEXT("<resolves>")));
	}

	// The second half: a .dui that renames a node and does NOT say so. The rename still happens --
	// the file is the truth about the hierarchy -- and nothing is carried across, because carrying
	// things across is what the clause is for. The compiler must not infer the intent from a name
	// that merely disappeared: guessing gets it wrong the first time somebody renames two nodes in
	// one edit, which is why "(was:)" is written down rather than detected.
	{
		FScopedDuiFile Source(TEXT("NoWasClause.dui"));
		if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
			TEXT("Widget Root {"),
			TEXT("    Text OkLabel {"),
			TEXT("    }"),
			TEXT("}")
		})))
		{
			return false;
		}

		FScopedBlueprint Fixture(TEXT("BP_NoWasClause"));
		if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;
		if (!TestTrue(TEXT("and points at the file"), Fixture.SetDuiFilePath(Source.FilePath))) return false;

		{
			FCompilerResultsLog Results;
			Compile(Fixture.Blueprint, Results);
			if (!TestEqual(TEXT("the .dui compiles clean"), Results.NumErrors, 0))
			{
				AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
				return false;
			}
		}

		if (!TestTrue(TEXT("the fixture renamed the node without saying so"), Source.Write({
			TEXT("Widget Root {"),
			TEXT("    Text OkBtn {"),
			TEXT("    }"),
			TEXT("}")
		})))
		{
			return false;
		}

		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		TestEqual(TEXT("an unannounced rename compiles clean"), Results.NumErrors, 0);
		TestMessagesDoNotContain(*this, TEXT("and migrates nothing"), Results, TEXT("took over from"));
		TestMessagesDoNotContain(*this, TEXT("and offers no clause to delete"), Results, TEXT("can be deleted"));
		TestNotNull(TEXT("the class declares the new id"),
			Fixture.Blueprint->GeneratedClass->FindPropertyByName(FName(TEXT("OkBtn"))));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRenameWillNotStealAnotherVariableTest,
	"DreamGUI.WidgetBlueprint.ARenameWillNotTakeReferencesToAVariableOfItsOwnName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRenameWillNotStealAnotherVariableTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextMigrationTestLocal;

	// The graph leg matches by NAME and nothing else, which is what makes it able to fix a dangling
	// reference -- and what makes it dangerous. If something OTHER than the renamed widget already
	// answers to the old name, moving every reference to it is a rename nobody asked for, in a graph
	// nobody has open. Here the old name belongs to a member of the parent class.
	//
	// Not hypothetical: PopulateBlueprintGeneratedVariables deliberately skips a widget whose name
	// the parent already declares, so a widget of that name never had a variable of its own -- every
	// reference to it in the graph is the PARENT's, and moving them would break the parent's code.
	FScopedBlueprint Fixture(TEXT("BP_WasStealsParentMember"), UDreamWidgetBlueprintBindingBase::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was created"), Fixture.Blueprint)) return false;

	// The parent declares RequiredHeader as a meta=(BindDreamWidget) member, so the hierarchy has to
	// contain a widget of that name or the compile fails on the binding.
	Fixture.AddWidget(TEXT("RequiredHeader"));
	Fixture.AddWidget(TEXT("Caption"));

	{
		FCompilerResultsLog Results;
		Compile(Fixture.Blueprint, Results);
		if (!TestEqual(TEXT("the hierarchy compiles clean"), Results.NumErrors, 0))
		{
			AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinMessages(Results)));
			return false;
		}
	}

	UK2Node_VariableGet* Getter = AddVariableGetNode(Fixture, TEXT("RequiredHeader"));
	if (!TestNotNull(TEXT("a graph node reads the parent's member"), Getter)) return false;

	// Asked directly rather than through a compile, so the refusal is read as a value rather than
	// hunted for in a message log -- and so the test says nothing about which severity it is reported
	// at, which is a wording decision and not the claim.
	const FDreamWidgetBlueprintCompilerContext::FWidgetRenameMigration Migration =
		FDreamWidgetBlueprintCompilerContext::MigrateWidgetRename(Fixture.Blueprint, TEXT("RequiredHeader"), TEXT("Header"));

	TestTrue(TEXT("the graph leg refuses and says why"), !Migration.GraphRefusal.IsEmpty());
	TestEqual(TEXT("and moves nothing"), Migration.GraphReferences, 0);
	if (UK2Node_VariableGet* Untouched = FindVariableGetNode(Fixture))
	{
		TestEqual(TEXT("the reference to the parent's member stays where it was"),
			Untouched->GetVarName(), FName(TEXT("RequiredHeader")));
	}

	return true;
}

#endif
