// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetPreviewHost.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextWriteBack.h"

#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIAnchorData.h"

#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * Structure on a `.dui`-backed hierarchy: what the designer can now create, delete, duplicate and paste,
 * and the proof that each one reached the FILE -- not just the tree, which the next compile rebuilds
 * from that file and would quietly put back the way it was.
 *
 * Every designer test reads the file off the disk after the gesture and finds the template again by
 * name in the Blueprint's CURRENT tree: the compile a structural edit ends with replaces the tree
 * object, so a pointer taken before the gesture is to the tree the file used to describe.
 */

namespace DreamUITextStructureTestLocal
{
	/** A .dui that has really been to disk, and is gone again when the test returns. */
	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
			FileName = FPaths::GetCleanFilename(FilePath);
		}

		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*FilePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}

		/** What is on the disk now -- the thing the next compile will read. */
		FString Read() const
		{
			FString Text;
			FFileHelper::LoadFileToString(Text, *FilePath);
			return Text;
		}

		FString FilePath;
		FString FileName;
	};

	/** A text-authored widget blueprint on a .dui, with a designer open on it unless asked otherwise. */
	struct FScopedTextBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
		FDreamWidgetBlueprintEditor* Designer = nullptr;
		int32 CompileErrors = 0;

		FScopedTextBlueprint(const TCHAR* InName, const FString& InDuiFilePath, bool bInOpenDesigner = true)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidget::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
			if (Blueprint == nullptr)
			{
				return;
			}
			if (UDreamTextUserWidget* Defaults = Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr)
			{
				Defaults->SourceFile.FilePath = InDuiFilePath;
			}
			FCompilerResultsLog Results;
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
			CompileErrors = Results.NumErrors;

			if (bInOpenDesigner)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Blueprint);
				Designer = static_cast<FDreamWidgetBlueprintEditor*>(
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->FindEditorForAsset(Blueprint, false));
			}
		}

		~FScopedTextBlueprint()
		{
			if (GEditor != nullptr && Blueprint != nullptr && Designer != nullptr)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Blueprint);
				FSlateApplication::Get().Tick();
			}
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedTextBlueprint(const FScopedTextBlueprint&) = delete;
		FScopedTextBlueprint& operator=(const FScopedTextBlueprint&) = delete;

		UDreamWidget* FindTemplate(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Blueprint != nullptr && IsValid(Blueprint->WidgetTree))
			{
				Blueprint->WidgetTree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}

		UDreamWidget* FindPreview(const FString& InDisplayName) const
		{
			UDreamWidget* Template = FindTemplate(InDisplayName);
			return Designer != nullptr && Designer->GetPreviewHost().IsValid() && Template != nullptr
				? Designer->GetPreviewHost()->FindPreviewForTemplate(Template) : nullptr;
		}

		int32 TemplateCount() const
		{
			return Blueprint != nullptr && IsValid(Blueprint->WidgetTree) ? Blueprint->WidgetTree->CountWidgets() : 0;
		}
	};

	/** The node with this id in a parse of InText, or null. */
	const FDreamUINode* FindNode(const FDreamUIAst& InAst, const FString& InId)
	{
		const FDreamUINode* Found = nullptr;
		InAst.ForEachNode([&Found, &InId](const FDreamUINode& InNode)
		{
			if (Found == nullptr && InNode.Id == InId)
			{
				Found = &InNode;
			}
		});
		return Found;
	}

	bool ParseText(const FString& InText, FDreamUIAst& OutAst)
	{
		FDreamUIDiagnosticBag Diagnostics;
		Diagnostics.SourceName = TEXT("Structure.dui");
		return FDreamUISourceFile::Parse(InText, Diagnostics.SourceName, OutAst, Diagnostics);
	}

	bool HoldsChild(const FDreamUINode* InParent, const FString& InChildId)
	{
		return InParent != nullptr && InParent->Children.ContainsByPredicate(
			[&InChildId](const FDreamUINode& InChild) { return InChild.Id == InChildId; });
	}
}

// =================================================================================================
// The designer
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextStructureCreateTest,
	"DreamGUI.Designer.Text.ACreatedWidgetIsWrittenIntoTheFileAndUndoesWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextStructureCreateTest::RunTest(const FString&)
{
	using namespace DreamUITextStructureTestLocal;

	FScopedDuiFile Source(TEXT("StructureCreate.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Child {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_StructureCreate"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const int32 CountBefore = Scoped.TemplateCount();

	// The palette's own way in: a widget dropped on the root's row.
	UDreamWidget* Preview = Scoped.Designer->DesignerCreateWidget(Scoped.FindPreview(TEXT("Root")),
		UDreamWidget::StaticClass(), TEXT("Added"));
	TestNotNull(TEXT("the new widget has a preview to select"), Preview);

	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the file still parses"), ParseText(Source.Read(), Ast))) return false;
	TestTrue(TEXT("and the root now holds the new node"), HoldsChild(&Ast.Root, TEXT("Added")));
	TestNotNull(TEXT("the compiled tree has it -- so the compile read it from the file"), Scoped.FindTemplate(TEXT("Added")));
	TestEqual(TEXT("one widget more, nothing else"), Scoped.TemplateCount(), CountBefore + 1);

	// One gesture, one Ctrl+Z: the text and the tree go back together.
	if (!TestTrue(TEXT("there is something to undo"), GEditor->UndoTransaction())) return false;
	TestFalse(TEXT("the undo took the node out of the file"), Source.Read().Contains(TEXT("Added")));
	TestNull(TEXT("and out of the tree"), Scoped.FindTemplate(TEXT("Added")));
	TestNotNull(TEXT("the rest of the tree is back as it was"), Scoped.FindTemplate(TEXT("Child")));

	if (!TestTrue(TEXT("there is something to redo"), GEditor->RedoTransaction())) return false;
	TestTrue(TEXT("the redo put the node back in the file"), Source.Read().Contains(TEXT("Widget Added")));
	TestNotNull(TEXT("and in the tree"), Scoped.FindTemplate(TEXT("Added")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextStructureDeleteTest,
	"DreamGUI.Designer.Text.DeletingAWidgetWithChildrenTakesItsWholeBlockOutOfTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextStructureDeleteTest::RunTest(const FString&)
{
	using namespace DreamUITextStructureTestLocal;

	FScopedDuiFile Source(TEXT("StructureDelete.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Panel {"),
		TEXT("        + VerticalBox"),
		TEXT("        Widget First {"),
		TEXT("        }"),
		TEXT("        Widget Second {"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("    Widget Keep {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_StructureDelete"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	const TArray<UDreamWidget*> ToDelete{ Scoped.FindPreview(TEXT("Panel")) };
	TestTrue(TEXT("the delete went through"), Scoped.Designer->DesignerDeleteWidgets(ToDelete));

	const FString After = Source.Read();
	// `Widget Panel`, not `Panel`: the root's `+ CanvasPanel` line stays, and says Panel too.
	TestFalse(TEXT("the node is out of the file"), After.Contains(TEXT("Widget Panel")));
	TestFalse(TEXT("and so are its children, with it"), After.Contains(TEXT("First")) || After.Contains(TEXT("Second")));
	TestTrue(TEXT("its sibling is not"), After.Contains(TEXT("Widget Keep")));
	TestNull(TEXT("the compiled tree has no Panel"), Scoped.FindTemplate(TEXT("Panel")));
	TestNull(TEXT("nor its children"), Scoped.FindTemplate(TEXT("First")));
	TestNotNull(TEXT("and still has the sibling"), Scoped.FindTemplate(TEXT("Keep")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextStructureRefusalsTest,
	"DreamGUI.Designer.Text.WhatTheFileCannotTakeIsRefusedAndSaid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextStructureRefusalsTest::RunTest(const FString&)
{
	using namespace DreamUITextStructureTestLocal;

	FScopedDuiFile Source(TEXT("StructureRefusals.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Child {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	// The root: a .dui holds exactly one. Moving and renaming: not written yet, still refused whole.
	AddExpectedError(TEXT("Refusing to delete 'Root'.*StructureRefusals\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Refusing to move .*StructureRefusals\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Refusing to rename .*StructureRefusals\\.dui"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedTextBlueprint Scoped(TEXT("BP_StructureRefusals"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	const FString Before = Source.Read();

	TestFalse(TEXT("deleting the root is refused"),
		DreamWidgetTreeEditing::DeleteWidget(Scoped.Blueprint, Scoped.FindTemplate(TEXT("Root"))));
	TestFalse(TEXT("moving is refused"), DreamWidgetTreeEditing::ReparentWidget(Scoped.Blueprint,
		Scoped.FindTemplate(TEXT("Child")), Scoped.FindTemplate(TEXT("Root")), 0));
	TestEqual(TEXT("renaming is refused"), DreamWidgetTreeEditing::RenameWidget(Scoped.Blueprint,
		Scoped.FindTemplate(TEXT("Child")), TEXT("Renamed")), FString());

	TestEqual(TEXT("the file is untouched"), Source.Read(), Before);
	TestNotNull(TEXT("and so is the tree"), Scoped.FindTemplate(TEXT("Child")));

	// The check itself, without a designer gesture around it: a removal of the root, refused in the
	// patcher's words; and an insert under a node the file does not have.
	FText Reason;
	FDreamUIStructuralEdit RemoveRoot;
	RemoveRoot.Kind = EDreamUIStructuralEditKind::RemoveNode;
	RemoveRoot.NodeId = TEXT("Root");
	TestFalse(TEXT("the check refuses removing the root"),
		DreamUITextAuthoring::CheckStructuralEdit(Scoped.Blueprint, RemoveRoot, /*bInVerifyBuild*/true, Reason));
	TestTrue(TEXT("and says why"), Reason.ToString().Contains(TEXT("root")));

	FDreamUIStructuralEdit IntoNothing;
	IntoNothing.Kind = EDreamUIStructuralEditKind::InsertNode;
	IntoNothing.ParentId = TEXT("NoSuchNode");
	IntoNothing.NewId = TEXT("Orphan");
	IntoNothing.TypeName = TEXT("Widget");
	TestFalse(TEXT("an insert under a node the file does not write is refused"),
		DreamUITextAuthoring::CheckStructuralEdit(Scoped.Blueprint, IntoNothing, /*bInVerifyBuild*/false, Reason));

	FDreamUIStructuralEdit Fine;
	Fine.Kind = EDreamUIStructuralEditKind::InsertNode;
	Fine.ParentId = TEXT("Child");
	Fine.NewId = TEXT("Inside");
	Fine.TypeName = TEXT("Widget");
	TestTrue(TEXT("and one under a node it writes is not"),
		DreamUITextAuthoring::CheckStructuralEdit(Scoped.Blueprint, Fine, /*bInVerifyBuild*/true, Reason));
	TestEqual(TEXT("a check writes nothing"), Source.Read(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextStructureDuplicateTest,
	"DreamGUI.Designer.Text.ADuplicateIsWrittenWithItsChildrenAndValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextStructureDuplicateTest::RunTest(const FString&)
{
	using namespace DreamUITextStructureTestLocal;

	FScopedDuiFile Source(TEXT("StructureDuplicate.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Card {"),
		TEXT("        AnchorData.SizeDelta = (120, 40)"),
		TEXT("        + VerticalBox"),
		TEXT("        Widget Inner {"),
		TEXT("            AnchorData.SizeDelta = (30, 10)"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_StructureDuplicate"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	const TArray<UDreamWidget*> ToDuplicate{ Scoped.FindPreview(TEXT("Card")) };
	const TArray<UDreamWidget*> Copies = Scoped.Designer->DesignerDuplicateWidgets(ToDuplicate);
	TestEqual(TEXT("one copy, selected"), Copies.Num(), 1);

	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the file still parses"), ParseText(Source.Read(), Ast))) return false;
	const FDreamUINode* CopyNode = FindNode(Ast, TEXT("Card_1"));
	if (!TestNotNull(TEXT("the copy is in the file under a name of its own"), CopyNode)) return false;
	TestTrue(TEXT("with its child, also renamed, inside it"), HoldsChild(CopyNode, TEXT("Inner_1")));
	TestTrue(TEXT("the original is untouched"), HoldsChild(FindNode(Ast, TEXT("Card")), TEXT("Inner")));

	// The values arrive through the compile, which is the proof they were written rather than only held.
	UDreamWidget* InnerCopy = Scoped.FindTemplate(TEXT("Inner_1"));
	if (!TestNotNull(TEXT("the compiled tree has the copied child"), InnerCopy)) return false;
	TestTrue(TEXT("with the original's size"), InnerCopy->GetAnchorData().SizeDelta.Equals(FVector2D(30.0, 10.0)));
	UDreamWidget* CardCopy = Scoped.FindTemplate(TEXT("Card_1"));
	if (TestNotNull(TEXT("and the copied parent"), CardCopy))
	{
		TestTrue(TEXT("with its size"), CardCopy->GetAnchorData().SizeDelta.Equals(FVector2D(120.0, 40.0)));
		TestNotNull(TEXT("and its layout container"), CardCopy->GetLayoutContainer());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextStructurePasteTest,
	"DreamGUI.Designer.Text.PastingSeveralWidgetsWritesEachIntoTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextStructurePasteTest::RunTest(const FString&)
{
	using namespace DreamUITextStructureTestLocal;

	FScopedDuiFile Source(TEXT("StructurePaste.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Left {"),
		TEXT("    }"),
		TEXT("    Widget Right {"),
		TEXT("    }"),
		TEXT("    Widget Shelf {"),
		TEXT("        + VerticalBox"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_StructurePaste"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;

	const TArray<UDreamWidget*> ToCopy{ Scoped.FindPreview(TEXT("Left")), Scoped.FindPreview(TEXT("Right")) };
	Scoped.Designer->DesignerCopyWidgets(ToCopy);
	const TArray<UDreamWidget*> Pasted = Scoped.Designer->DesignerPasteWidgets(Scoped.FindPreview(TEXT("Shelf")));
	TestEqual(TEXT("both pasted"), Pasted.Num(), 2);

	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the file still parses"), ParseText(Source.Read(), Ast))) return false;
	const FDreamUINode* Shelf = FindNode(Ast, TEXT("Shelf"));
	TestTrue(TEXT("the first copy is written inside the shelf"), HoldsChild(Shelf, TEXT("Left_1")));
	TestTrue(TEXT("and so is the second"), HoldsChild(Shelf, TEXT("Right_1")));
	UDreamWidget* Copy = Scoped.FindTemplate(TEXT("Right_1"));
	if (TestNotNull(TEXT("the compiled tree has the copies"), Copy))
	{
		TestTrue(TEXT("under the shelf"), Copy->GetParent() == Scoped.FindTemplate(TEXT("Shelf")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextStructureNamesTest,
	"DreamGUI.Designer.Text.ANewWidgetIsNamedClearOfEveryIdTheFileHolds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextStructureNamesTest::RunTest(const FString&)
{
	using namespace DreamUITextStructureTestLocal;

	// `Added` is not in the tree under that name -- the node is called Kept now -- but the file still
	// holds it as the id a rename is migrating from. A new widget called Added would be read by the
	// write-back as that node and not written at all.
	FScopedDuiFile Source(TEXT("StructureNames.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    Widget Kept (was: Added) {"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_StructureNames"), Source.FilePath);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	TestNull(TEXT("no widget in the tree is called Added"), Scoped.FindTemplate(TEXT("Added")));

	UDreamWidget* Preview = Scoped.Designer->DesignerCreateWidget(Scoped.FindPreview(TEXT("Root")),
		UDreamWidget::StaticClass(), TEXT("Added"));
	if (!TestNotNull(TEXT("the widget was created"), Preview)) return false;
	TestEqual(TEXT("under a name the file does not hold"), Preview->GetDisplayName(), FString(TEXT("Added_1")));
	TestTrue(TEXT("and written into it"), Source.Read().Contains(TEXT("Widget Added_1")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextStructureNoDesignerTest,
	"DreamGUI.Designer.Text.WithNoDesignerOpenATreeEditStillReachesTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextStructureNoDesignerTest::RunTest(const FString&)
{
	using namespace DreamUITextStructureTestLocal;

	FScopedDuiFile Source(TEXT("StructureNoDesigner.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("}")
	})))
	{
		return false;
	}
	// A script, a tool menu, a commandlet: the tree edited directly, with no designer and no write-back
	// of its own to lean on.
	FScopedTextBlueprint Scoped(TEXT("BP_StructureNoDesigner"), Source.FilePath, /*bInOpenDesigner*/false);
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;

	TestNotNull(TEXT("the create went through"), DreamWidgetTreeEditing::CreateWidget(Scoped.Blueprint,
		UDreamWidget::StaticClass(), Scoped.FindTemplate(TEXT("Root")), -1, TEXT("Scripted")));
	TestTrue(TEXT("it was written into the file"), Source.Read().Contains(TEXT("Widget Scripted")));
	TestNotNull(TEXT("and the compiled tree has it"), Scoped.FindTemplate(TEXT("Scripted")));
	return true;
}

#endif
