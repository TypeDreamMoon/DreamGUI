// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamWidget.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * The designer's write-back against the newer `.dui` syntax: types that are aliases, containers and resources, nodes
 * with no id, `@slot { … }` blocks, `@fill`, `if` branches, `for` templates, slot declarations with blocks and slot
 * fills -- each a place where the text and the tree no longer line up one statement to one line.
 *
 * The rule is the patcher's, unchanged: whatever comes out parses, and only what was edited moved. So the cases compare
 * the WHOLE file, and the ones that refuse also assert the file is exactly what it was. Where the tree holds something
 * no line spells -- a `Shown` an `if` made, the `SizeRule` an `@fill` stands for -- the edit is either rewritten into a
 * line that plainly means the same thing, or refused under DUI7004 with the line to go to; never spliced into whatever
 * the made-up location happens to point at.
 *
 * The last cases run the whole pipeline -- the reference tree, the comparison, the patch -- on one file that uses every
 * new construct at once, because the write-back's first duty is the one an author notices first: opening a file in the
 * designer writes nothing.
 */
namespace DreamUISyntaxWriteBackTestLocal
{
	FString Join(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	/** The fixture with one line replaced. Line numbers are 1-based, the array's index plus one. */
	FString Replacing(const TArray<FString>& InLines, int32 InOneBasedLine, const FString& InReplacement)
	{
		TArray<FString> Lines = InLines;
		Lines[InOneBasedLine - 1] = InReplacement;
		return Join(Lines);
	}

	/** The fixture with lines inserted after one of them. */
	FString Inserting(const TArray<FString>& InLines, int32 InAfterOneBasedLine, const TArray<FString>& InNewLines)
	{
		TArray<FString> Lines = InLines;
		Lines.Insert(InNewLines, InAfterOneBasedLine);
		return Join(Lines);
	}

	/** With the import reader every real caller passes, so a `use` resolves the way the compiler resolves it. */
	bool Parse(const FString& InText, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		return FDreamUISourceFile::Parse(InText, TEXT("Syntax.dui"), OutAst, OutDiagnostics,
			FDreamUISourceFile::MakeFileImportReader());
	}

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

	/**
	 * The id the parser made for an unnamed child of this type. Looked up rather than spelled out: what the made-up id
	 * looks like is the parser's to decide, and these cases are about the write-back finding the node by it.
	 */
	FString AnonymousIdOf(const FDreamUINode& InParent, const TCHAR* InTypeName)
	{
		for (const FDreamUINode& Child : InParent.Children)
		{
			if (Child.bAnonymous && Child.TypeName == InTypeName)
			{
				return Child.Id;
			}
		}
		return FString();
	}

	bool Reported(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate(
			[InCode](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.Code == InCode; });
	}

	/** Parse, then one property edit against that parse -- the only correct way to call the patcher once. */
	bool Patch(FString& InOutText, const FString& InNodeId, EDreamUIPatchTarget InTarget, int32 InComponentIndex,
		const TCHAR* InPropertyName, const TCHAR* InValue, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		if (!Parse(InOutText, Ast, ParseDiagnostics))
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::None, FDreamUISourceLocation(),
				FString::Printf(TEXT("the fixture did not parse: %s"), *ParseDiagnostics.ToString()));
			return false;
		}
		return FDreamUITextPatcher::SetProperty(InOutText, Ast, InNodeId, InTarget, InComponentIndex,
			InPropertyName, InValue, OutDiagnostics);
	}

	/** Parse, then one structural edit against that parse. */
	bool ApplyOne(FString& InOutText, const FDreamUIStructuralEdit& InEdit, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		if (!Parse(InOutText, Ast, ParseDiagnostics))
		{
			OutDiagnostics.AddError(EDreamUIDiagnosticCode::None, FDreamUISourceLocation(),
				FString::Printf(TEXT("the fixture did not parse: %s"), *ParseDiagnostics.ToString()));
			return false;
		}
		const TArray<FDreamUIStructuralEdit> Batch = { InEdit };
		return FDreamUITextPatcher::ApplyStructuralEdits(InOutText, Ast, Batch, OutDiagnostics);
	}

	FDreamUIStructuralEdit Structural(EDreamUIStructuralEditKind InKind)
	{
		FDreamUIStructuralEdit Edit;
		Edit.Kind = InKind;
		return Edit;
	}

	/** Whatever an edit produced, it has to parse: the one outcome the write-back may never have. */
	bool ExpectParses(FAutomationTestBase& InTest, const FString& InText, const TCHAR* InWhat)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = Parse(InText, Ast, Diagnostics);
		return InTest.TestTrue(*FString::Printf(TEXT("%s leaves a file that still parses (%s)"), InWhat, *Diagnostics.ToString()),
			bParsed);
	}

	/** A refusal is only right if it left the file alone, and said why under the code expected. */
	void ExpectRefusal(FAutomationTestBase& InTest, const TCHAR* InWhat, const FString& InOriginal, const FString& InAfter,
		bool bInReturned, const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		InTest.TestFalse(*FString::Printf(TEXT("%s is refused"), InWhat), bInReturned);
		InTest.TestEqual(*FString::Printf(TEXT("%s leaves the file exactly as it was"), InWhat), InAfter, InOriginal);
		InTest.TestTrue(*FString::Printf(TEXT("%s is reported under %s (%s)"), InWhat,
			*FDreamUIDiagnostic::CodeToString(InCode), *InDiagnostics.ToString()), Reported(InDiagnostics, InCode));
	}

	/** A real `.dui` under Saved/, gone when the test leaves -- what a `use` reads. */
	struct FScopedDuiFile
	{
		FString Path;

		FScopedDuiFile(const TCHAR* InFileName, const FString& InText)
		{
			Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("DreamGUITests") / InFileName);
			FFileHelper::SaveStringToFile(InText, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}

		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*Path, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
		}

		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;
	};

	/** A tree the write-back would build from this text -- the live side of a case, standing in for the designer's. */
	struct FBuiltTree
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		UDreamWidget* Find(const FString& InId) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([&InId, &Found](UDreamWidget* InWidget)
				{
					if (Found == nullptr && IsValid(InWidget) && InWidget->GetDisplayName() == InId)
					{
						Found = InWidget;
					}
				});
			}
			return Found;
		}
	};

	/**
	 * Through BuildReferenceTree itself, so the live tree is built exactly as the reference one will be -- the each and
	 * for sinks, the import reader and all. Any difference a case then finds is the one the case made.
	 */
	FBuiltTree BuildLive(const FString& InText)
	{
		FBuiltTree Built;
		Built.Tree.Reset(FDreamUITextWriteBack::BuildReferenceTree(InText, Built.Ast, Built.Diagnostics));
		return Built;
	}

	/** A floating point property written through reflection, as the builder writes an authoring tree. */
	bool SetNumber(UObject* InObject, const TCHAR* InName, double InValue)
	{
		if (!IsValid(InObject))
		{
			return false;
		}
		FNumericProperty* Property = CastField<FNumericProperty>(FindFProperty<FProperty>(InObject->GetClass(), InName));
		if (Property == nullptr || !Property->IsFloatingPoint())
		{
			return false;
		}
		Property->SetFloatingPointPropertyValue(Property->ContainerPtrToValuePtr<void>(InObject), InValue);
		return true;
	}

	/** The library the namespaced cases import: no root, so `as` makes it a namespace. */
	FString LibraryText()
	{
		return Join({
			TEXT("use /Script/DreamGUIControls.DreamButton as Button"),
			TEXT(""),
			TEXT("resources {"),
			TEXT("    Color Ink = #514D42"),
			TEXT("}"),
			TEXT(""),
			TEXT("style Caption {"),
			TEXT("    FontSize = 14"),
			TEXT("    Color = @Ink"),
			TEXT("}")
		});
	}

	/**
	 * A screen using every construct the syntax grew, at once. Line numbers are the array's, so the cases below can say
	 * "line 26" and mean the `@fill 2`. Line 1 names the library by the absolute path the case wrote it to.
	 */
	TArray<FString> EveryConstructLines(const FString& InLibraryPath)
	{
		return {
			FString::Printf(TEXT("use \"%s\" as lib"), *InLibraryPath),     //  1
			TEXT("use /Script/DreamGUIControls.DreamButton as Button"),         //  2
			TEXT(""),                                                           //  3
			TEXT("props {"),                                                    //  4
			TEXT("    Text Title"),                                             //  5
			TEXT("    Number Picked = 0"),                                      //  6
			TEXT("}"),                                                          //  7
			TEXT("events {"),                                                   //  8
			TEXT("    Chosen(Number Index)"),                                   //  9
			TEXT("}"),                                                          // 10
			TEXT(""),                                                           // 11
			TEXT("style Column {"),                                             // 12
			TEXT("    + VerticalBox { Spacing = 12 }"),                         // 13
			TEXT("    @fill"),                                                  // 14
			TEXT("}"),                                                          // 15
			TEXT(""),                                                           // 16
			TEXT("Widget Root {"),                                              // 17
			TEXT("    + HorizontalBox { Spacing = 8 }"),                        // 18
			TEXT("    Widget Menu : Column {"),                                 // 19
			TEXT("        Text Heading : lib.Caption {"),                       // 20
			TEXT("            Text = \"Settings\""),                            // 21
			TEXT("            @slot { HorizontalAlignment = Fill  Padding = (0, 4, 0, 4) }"), // 22
			TEXT("        }"),                                                  // 23
			TEXT("        HorizontalBox {"),                                    // 24
			TEXT("            Spacing = 6"),                                    // 25
			TEXT("            @fill 2"),                                        // 26
			TEXT("            Text { Text = \"Status\" }"),                     // 27
			TEXT("            lib.Button Apply {"),                             // 28
			TEXT("                OnClicked -> emit Chosen(1)"),                // 29
			TEXT("            }"),                                              // 30
			TEXT("        }"),                                                  // 31
			TEXT("        if HasSave() {"),                                     // 32
			TEXT("            Button Continue { }"),                            // 33
			TEXT("        } else {"),                                           // 34
			TEXT("            Text NoSave { Text = \"No save data\" }"),        // 35
			TEXT("        }"),                                                  // 36
			TEXT("        for Option in Options {"),                            // 37
			TEXT("            Text { Text <- Option.Label }"),                  // 38
			TEXT("        }"),                                                  // 39
			TEXT("    }"),                                                      // 40
			TEXT("    VerticalBox Detail {"),                                   // 41
			TEXT("        Spacing = 4"),                                        // 42
			TEXT("        Shown <- HasDetail()"),                               // 43
			TEXT("        @fill"),                                              // 44
			TEXT("        Text Note { Text = \"Detail\" }"),                    // 45
			TEXT("    }"),                                                      // 46
			TEXT("}")                                                           // 47
		};
	}
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackNewTypesTest,
	"DreamGUI.Text.Syntax.ANodeTypedByAnAliasAContainerOrAResourceIsEditedRenamedAndExtendedInPlace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A node's header is confirmed against the text before anything is written to its block, and a type is no longer one
 * word: `Button` is an alias, `VerticalBox` a container, `@Card` a resource, and `nier . Row` a namespaced alias the
 * lexer reads with spaces round its dot. Each has to anchor, and the id after it has to be found for a rename.
 */
bool FDreamUISyntaxWriteBackNewTypesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const TArray<FString> Lines = {
		TEXT("use /Script/DreamGUIControls.DreamButton as Button"),  //  1
		TEXT(""),                                                    //  2
		TEXT("Widget Root {"),                                       //  3
		TEXT("    VerticalBox Column {"),                            //  4
		TEXT("        Spacing = 29"),                                //  5
		TEXT("    }"),                                               //  6
		TEXT("    Button Confirm {"),                                //  7
		TEXT("        RenderOpacity = 1"),                           //  8
		TEXT("    }"),                                               //  9
		TEXT("    @Card Promo {"),                                   // 10
		TEXT("    }"),                                               // 11
		TEXT("    nier . Row Spaced { RenderOpacity = 1 }"),         // 12
		TEXT("}")                                                    // 13
	};
	const FString Source = Join(Lines);

	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a property of a container-typed node is replaced"),
			Patch(Text, TEXT("Column"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Spacing"), TEXT("40"), Diagnostics));
		TestEqual(TEXT("on its own line"), Text, Replacing(Lines, 5, TEXT("        Spacing = 40")));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a property of an alias-typed node is inserted"),
			Patch(Text, TEXT("Confirm"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("BackgroundColor"), TEXT("#FFFFFF"), Diagnostics));
		TestEqual(TEXT("after the last one in its block"), Text,
			Inserting(Lines, 8, { TEXT("        BackgroundColor = #FFFFFF") }));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a property of a resource-typed node is inserted"),
			Patch(Text, TEXT("Promo"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics));
		TestEqual(TEXT("into its empty block, one level in"), Text,
			Inserting(Lines, 10, { TEXT("        RenderOpacity = 0.5") }));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a property of a namespaced alias written with spaces round its dot is replaced"),
			Patch(Text, TEXT("Spaced"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics));
		TestEqual(TEXT("and only the value moved"), Text, Replacing(Lines, 12, TEXT("    nier . Row Spaced { RenderOpacity = 0.5 }")));
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::RenameNode);
		Edit.NodeId = TEXT("Confirm");
		Edit.NewId = TEXT("Accept");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("an alias-typed node is renamed"), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(TEXT("its id found after the alias"), Text, Replacing(Lines, 7, TEXT("    Button Accept (was: Confirm) {")));
		ExpectParses(*this, Text, TEXT("renaming an alias-typed node"));
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::RenameNode);
		Edit.NodeId = TEXT("Spaced");
		Edit.NewId = TEXT("Wide");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a node of a dotted type is renamed"), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(TEXT("its id found after the whole type, spaces and all"), Text,
			Replacing(Lines, 12, TEXT("    nier . Row Wide (was: Spaced) { RenderOpacity = 1 }")));
		ExpectParses(*this, Text, TEXT("renaming a node of a dotted type"));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackAnonymousTest,
	"DreamGUI.Text.Syntax.ANodeWithNoIdIsEditedByTheIdMadeForItAndARenameWritesItsFirstId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A node the author left unnamed is addressed by the id the parser made for it -- the name its widget carries in the
 * designer -- and its lines are edited like anyone's. A rename gives it its first id, written where an id goes, and no
 * `(was:)`: the made-up id named a hidden member nothing references, and the next unnamed sibling of the same type
 * takes it over.
 */
bool FDreamUISyntaxWriteBackAnonymousTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const TArray<FString> Lines = {
		TEXT("style Caption {"),                   //  1
		TEXT("    FontSize = 12"),                 //  2
		TEXT("}"),                                 //  3
		TEXT(""),                                  //  4
		TEXT("Widget Root {"),                     //  5
		TEXT("    HorizontalBox {"),               //  6
		TEXT("        Spacing = 14"),              //  7
		TEXT("        Text : Caption {"),          //  8
		TEXT("            Text = \"Status\""),     //  9
		TEXT("        }"),                         // 10
		TEXT("    }"),                             // 11
		TEXT("}")                                  // 12
	};
	const FString Source = Join(Lines);

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag ParseDiagnostics;
	const bool bParsed = Parse(Source, Ast, ParseDiagnostics);
	if (!TestTrue(*FString::Printf(TEXT("the fixture parses (%s)"), *ParseDiagnostics.ToString()), bParsed))
	{
		return false;
	}
	const FString BoxId = AnonymousIdOf(Ast.Root, TEXT("HorizontalBox"));
	const FDreamUINode* Box = FindNode(Ast, BoxId);
	if (!TestFalse(TEXT("the unnamed box was given an id"), BoxId.IsEmpty()) || !TestNotNull(TEXT("and is found by it"), Box))
	{
		return false;
	}
	const FString TextId = AnonymousIdOf(*Box, TEXT("Text"));
	if (!TestFalse(TEXT("the unnamed text was given an id"), TextId.IsEmpty()))
	{
		return false;
	}

	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a property of an unnamed node is replaced"),
			Patch(Text, BoxId, EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Spacing"), TEXT("20"), Diagnostics));
		TestEqual(TEXT("on its line"), Text, Replacing(Lines, 7, TEXT("        Spacing = 20")));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a property of an unnamed node that wears a style is inserted"),
			Patch(Text, TextId, EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("18"), Diagnostics));
		TestEqual(TEXT("in its own block, as an override of the style"), Text,
			Inserting(Lines, 9, { TEXT("            FontSize = 18") }));

		// The edit did not change the file's shape, so the made-up ids are the ones the designer already has.
		FDreamUIAst After;
		FDreamUIDiagnosticBag AfterDiagnostics;
		if (TestTrue(TEXT("the edited file parses"), Parse(Text, After, AfterDiagnostics)))
		{
			TestEqual(TEXT("and the box keeps its made-up id"), AnonymousIdOf(After.Root, TEXT("HorizontalBox")), BoxId);
		}
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::RenameNode);
		Edit.NodeId = BoxId;
		Edit.NewId = TEXT("StatusRow");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("an unnamed node is named"), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(TEXT("its id written after its type, with no rename clause"), Text,
			Replacing(Lines, 6, TEXT("    HorizontalBox StatusRow {")));

		FDreamUIAst After;
		FDreamUIDiagnosticBag AfterDiagnostics;
		const bool bNamedParsed = Parse(Text, After, AfterDiagnostics);
		if (TestTrue(*FString::Printf(TEXT("the named file parses (%s)"), *AfterDiagnostics.ToString()), bNamedParsed))
		{
			const FDreamUINode* Named = FindNode(After, TEXT("StatusRow"));
			if (TestNotNull(TEXT("the node now has the id"), Named))
			{
				TestFalse(TEXT("and is no longer an unnamed one"), Named->bAnonymous);
			}
		}
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::RenameNode);
		Edit.NodeId = TextId;
		Edit.NewId = TEXT("StatusLabel");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("an unnamed node with a style clause is named"), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(TEXT("its id between the type and the clause"), Text,
			Replacing(Lines, 8, TEXT("        Text StatusLabel : Caption {")));
		ExpectParses(*this, Text, TEXT("naming an unnamed node with a style"));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackSlotBlockTest,
	"DreamGUI.Text.Syntax.ASlotPropertyInAnAtSlotBlockIsEditedAndAddedInsideTheBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `@slot { … }` groups a node's slot lines, and the tree flattens them into the node's slot properties with nothing
 * to say where the block stood. A replace edits the value where it stands; a new slot property joins the block, bare;
 * and a new NODE property goes among the node's own lines -- never into the slot block, where it would become a slot
 * property of the same name.
 */
bool FDreamUISyntaxWriteBackSlotBlockTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const TArray<FString> Lines = {
		TEXT("Widget Root {"),                                               //  1
		TEXT("    + VerticalBox { Spacing = 4 }"),                           //  2
		TEXT("    Image Banner {"),                                          //  3
		TEXT("        Brush.TintColor = #202020"),                           //  4
		TEXT("        @slot {"),                                             //  5
		TEXT("            SizeRule = Fill"),                                 //  6
		TEXT("            Padding = (0, 8, 0, 0)"),                          //  7
		TEXT("        }"),                                                   //  8
		TEXT("    }"),                                                       //  9
		TEXT("    Image Strip {"),                                           // 10
		TEXT("        @slot { SizeRule = Fill  Padding = (0, 8, 0, 0) }"),   // 11
		TEXT("    }"),                                                       // 12
		TEXT("}")                                                            // 13
	};
	const FString Source = Join(Lines);

	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a slot property in a block is replaced"),
			Patch(Text, TEXT("Banner"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("Padding"), TEXT("(0, 4, 0, 0)"), Diagnostics));
		TestEqual(TEXT("where it stands in the block"), Text, Replacing(Lines, 7, TEXT("            Padding = (0, 4, 0, 0)")));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a new slot property is written"),
			Patch(Text, TEXT("Banner"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("HorizontalAlignment"), TEXT("Center"), Diagnostics));
		TestEqual(TEXT("into the block, bare, after its last line"), Text,
			Inserting(Lines, 7, { TEXT("            HorizontalAlignment = Center") }));
		ExpectParses(*this, Text, TEXT("adding to a slot block"));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a new node property is written"),
			Patch(Text, TEXT("Banner"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics));
		TestEqual(TEXT("among the node's own lines, not into the slot block"), Text,
			Inserting(Lines, 4, { TEXT("        RenderOpacity = 0.5") }));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a slot property in a one-line block is replaced"),
			Patch(Text, TEXT("Strip"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("Padding"), TEXT("(0, 2, 0, 0)"), Diagnostics));
		TestEqual(TEXT("and nothing else on the line moved"), Text,
			Replacing(Lines, 11, TEXT("        @slot { SizeRule = Fill  Padding = (0, 2, 0, 0) }")));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a one-line slot block takes another property"),
			Patch(Text, TEXT("Strip"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("HorizontalAlignment"), TEXT("Center"), Diagnostics));
		TArray<FString> Expected = Lines;
		Expected[10] = TEXT("        @slot { SizeRule = Fill  Padding = (0, 8, 0, 0)");
		Expected.Insert({ TEXT("            HorizontalAlignment = Center"), TEXT("        }") }, 11);
		TestEqual(TEXT("in front of the block's brace, which moves to a line of its own"), Text, Join(Expected));
		ExpectParses(*this, Text, TEXT("adding to a one-line slot block"));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackFillShorthandTest,
	"DreamGUI.Text.Syntax.AFillShorthandIsRewrittenOnlyWhereTheResultPlainlyMeansTheSame",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * `@fill` stands for `SizeRule = Fill`, and `@fill 2` adds `FillWeight = 2`. There is no SizeRule written for a splice
 * to land on, so each edit is decided on its own: a new weight replaces the number; a bare `@fill` given another rule
 * becomes the long spelling; and taking the Fill out of `@fill 2` -- which would leave a weight with nothing to say it
 * -- is refused under DUI7004 with the two lines to write instead.
 */
bool FDreamUISyntaxWriteBackFillShorthandTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const TArray<FString> Lines = {
		TEXT("Widget Root {"),            // 1
		TEXT("    + HorizontalBox { }"),  // 2
		TEXT("    Image Left {"),         // 3
		TEXT("        @fill"),            // 4
		TEXT("    }"),                    // 5
		TEXT("    Image Right {"),        // 6
		TEXT("        @fill 2"),          // 7
		TEXT("    }"),                    // 8
		TEXT("}")                         // 9
	};
	const FString Source = Join(Lines);

	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a weight is changed"),
			Patch(Text, TEXT("Right"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("FillWeight"), TEXT("3"), Diagnostics));
		TestEqual(TEXT("as the number after the shorthand"), Text, Replacing(Lines, 7, TEXT("        @fill 3")));
		ExpectParses(*this, Text, TEXT("a new weight"));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a fractional weight is changed"),
			Patch(Text, TEXT("Right"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("FillWeight"), TEXT("1.5"), Diagnostics));
		TestEqual(TEXT("in the shorthand too"), Text, Replacing(Lines, 7, TEXT("        @fill 1.5")));
	}
	{
		// A weight the shorthand cannot spell is refused rather than written into it: `@fill -1` does not read back
		// as the shorthand at all.
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Right"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("FillWeight"), TEXT("-1"), Diagnostics);
		ExpectRefusal(*this, TEXT("a weight the shorthand cannot hold"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchSyntaxNotWritable);
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a bare shorthand is given another rule"),
			Patch(Text, TEXT("Left"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("SizeRule"), TEXT("Auto"), Diagnostics));
		TestEqual(TEXT("and becomes the long spelling it stood for"), Text, Replacing(Lines, 4, TEXT("        @slot SizeRule = Auto")));
		ExpectParses(*this, Text, TEXT("rewriting a bare shorthand"));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Right"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("SizeRule"), TEXT("Auto"), Diagnostics);
		ExpectRefusal(*this, TEXT("another rule for a shorthand that also holds a weight"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchSyntaxNotWritable);
	}
	{
		// A weight on a bare shorthand is not a made-up property -- the shorthand never said one -- so it is an
		// ordinary insert, after the shorthand's line.
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a weight is given to a bare shorthand"),
			Patch(Text, TEXT("Left"), EDreamUIPatchTarget::Slot, INDEX_NONE, TEXT("FillWeight"), TEXT("2"), Diagnostics));
		TestEqual(TEXT("as a slot line of its own"), Text, Inserting(Lines, 4, { TEXT("        @slot FillWeight = 2") }));
		ExpectParses(*this, Text, TEXT("a weight beside a bare shorthand"));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackConditionalTest,
	"DreamGUI.Text.Syntax.TheVisibilityAnIfDecidesIsRefusedAndANewNodeGoesAfterTheWholeIf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * An `if` is lowered away: its branches' widgets become the enclosing node's children, each with a `Shown` the front
 * end made and located at the `if`. A visibility edit on one has no line to land on and would fight the condition, so
 * it is refused under DUI7004 -- under either of the value's two names. Everything else about such a node is its own
 * and is written into its block. And a node placed "after" a branch's child is written after the whole `if … else …`,
 * at the parent's level, not into the branch where it would come and go with the condition.
 */
bool FDreamUISyntaxWriteBackConditionalTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const TArray<FString> Lines = {
		TEXT("Widget Root {"),                         //  1
		TEXT("    + VerticalBox { }"),                 //  2
		TEXT("    if HasSave() {"),                    //  3
		TEXT("        Text Continue {"),               //  4
		TEXT("            Text = \"Continue\""),       //  5
		TEXT("        }"),                             //  6
		TEXT("    } else {"),                          //  7
		TEXT("        Text NoSave {"),                 //  8
		TEXT("            Text = \"No save data\""),   //  9
		TEXT("        }"),                             // 10
		TEXT("    }"),                                 // 11
		TEXT("    Text Footer {"),                     // 12
		TEXT("        Shown <- IsReady()"),            // 13
		TEXT("    }"),                                 // 14
		TEXT("}")                                      // 15
	};
	const FString Source = Join(Lines);

	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Continue"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Visibility"), TEXT("Collapsed"), Diagnostics);
		ExpectRefusal(*this, TEXT("a visibility the branch decides"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchSyntaxNotWritable);
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("NoSave"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Shown"), TEXT("false"), Diagnostics);
		ExpectRefusal(*this, TEXT("the made Shown of an else branch"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchSyntaxNotWritable);
	}
	{
		// The author's own binding decides it just as surely; refused as any value over a binding is.
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Footer"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("Visibility"), TEXT("Collapsed"), Diagnostics);
		ExpectRefusal(*this, TEXT("a visibility a written Shown binding decides"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchTargetNotFound);
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("anything else on a branch's node is written"),
			Patch(Text, TEXT("Continue"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("30"), Diagnostics));
		TestEqual(TEXT("into its own block, after its own line and not the made one"), Text,
			Inserting(Lines, 5, { TEXT("            FontSize = 30") }));
	}

	// ---- structure ---------------------------------------------------------------------------------
	for (const int32 ChildIndex : { 1, 2 })
	{
		// Index 1 is between Continue and NoSave, index 2 between NoSave and Footer: either way the child before the
		// place stands in a branch, and the place is after the whole construct.
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::InsertNode);
		Edit.ChildIndex = ChildIndex;
		Edit.TypeName = TEXT("Image");
		Edit.NewId = TEXT("Divider");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(*FString::Printf(TEXT("a node is inserted at index %d"), ChildIndex), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(*FString::Printf(TEXT("at index %d it lands after the whole if, at the root's level"), ChildIndex), Text,
			Inserting(Lines, 11, { TEXT("    Image Divider {"), TEXT("    }") }));
		ExpectParses(*this, Text, TEXT("inserting beside an if"));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackLoopTemplateTest,
	"DreamGUI.Text.Syntax.TheTemplateOfAForStaysPutAndTakesEditsLikeAnyNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A `for` repeats one template widget. Taking it out, or moving it away, leaves a loop with nothing to repeat, which
 * parses and does not build -- so both are refused, about the node, before the write-back has to refuse the whole
 * flush. Its own lines are edited where they are written: once, for every copy.
 */
bool FDreamUISyntaxWriteBackLoopTemplateTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const TArray<FString> Lines = {
		TEXT("Widget Root {"),                          //  1
		TEXT("    VerticalBox Options {"),              //  2
		TEXT("        Spacing = 6"),                    //  3
		TEXT("        for Option in GetOptions() {"),   //  4
		TEXT("            Text OptionLabel {"),         //  5
		TEXT("                Text <- Option.Label"),   //  6
		TEXT("            }"),                          //  7
		TEXT("        }"),                              //  8
		TEXT("    }"),                                  //  9
		TEXT("}")                                       // 10
	};
	const FString Source = Join(Lines);

	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::RemoveNode);
		Edit.NodeId = TEXT("OptionLabel");
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = ApplyOne(Text, Edit, Diagnostics);
		ExpectRefusal(*this, TEXT("removing a loop's template"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchTargetNotFound);
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::MoveNode);
		Edit.NodeId = TEXT("OptionLabel");
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = ApplyOne(Text, Edit, Diagnostics);
		ExpectRefusal(*this, TEXT("moving a loop's template out of it"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchTargetNotFound);
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a property of the template is written"),
			Patch(Text, TEXT("OptionLabel"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("FontSize"), TEXT("20"), Diagnostics));
		TestEqual(TEXT("into the template's block, once"), Text, Inserting(Lines, 6, { TEXT("                FontSize = 20") }));
	}
	{
		// The loop is not a position among the host's children, so a node added to the host goes before it, after the
		// host's own lines -- never into the loop's body, which takes exactly one widget.
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::InsertNode);
		Edit.ParentId = TEXT("Options");
		Edit.TypeName = TEXT("Text");
		Edit.NewId = TEXT("Header");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a node is added to the loop's host"), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(TEXT("outside the loop"), Text, Inserting(Lines, 3, { TEXT("        Text Header {"), TEXT("        }") }));
		ExpectParses(*this, Text, TEXT("adding beside a loop"));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackSlotsTest,
	"DreamGUI.Text.Syntax.ASlotDeclarationsBlockTakesPropertiesAndAFillTakesTheNodesDroppedIntoIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A slot declaration may carry a block now, and that block takes properties and behaviours like a node's; a bare
 * declaration stays the hole its author wrote. A fill (`slot Content { … }` inside a component instance) is the
 * host's content for that slot: it takes children and nothing else, and a node dropped into a named slot of an
 * instance goes into the instance's fill -- written first when there is none -- never into the instance's own block,
 * which is its default slot.
 */
bool FDreamUISyntaxWriteBackSlotsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const TArray<FString> Lines = {
		TEXT("use /Script/DreamGUIControls.DreamButton as Button"),  //  1
		TEXT(""),                                                    //  2
		TEXT("Widget Root {"),                                       //  3
		TEXT("    slot Rows default {"),                             //  4
		TEXT("        + VerticalBox { Spacing = 15 }"),              //  5
		TEXT("    }"),                                               //  6
		TEXT("    slot Footer"),                                     //  7
		TEXT("    Button Confirm {"),                                //  8
		TEXT("        slot Content {"),                              //  9
		TEXT("            Text Label {"),                            // 10
		TEXT("            }"),                                       // 11
		TEXT("        }"),                                           // 12
		TEXT("    }"),                                               // 13
		TEXT("}")                                                    // 14
	};
	const FString Source = Join(Lines);

	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a property of a declaration with a block is written"),
			Patch(Text, TEXT("Rows"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics));
		TestEqual(TEXT("into its block, before its behaviours"), Text, Inserting(Lines, 4, { TEXT("        RenderOpacity = 0.5") }));
		ExpectParses(*this, Text, TEXT("a property on a slot declaration"));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a behaviour value of a declaration is written"),
			Patch(Text, TEXT("Rows"), EDreamUIPatchTarget::Component, 0, TEXT("Spacing"), TEXT("20"), Diagnostics));
		TestEqual(TEXT("in its '+' block"), Text, Replacing(Lines, 5, TEXT("        + VerticalBox { Spacing = 20 }")));
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Footer"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics);
		ExpectRefusal(*this, TEXT("a property on a bare declaration"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchTargetNotFound);
	}
	{
		FString Text = Source;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = Patch(Text, TEXT("Content"), EDreamUIPatchTarget::Node, INDEX_NONE, TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics);
		ExpectRefusal(*this, TEXT("a property on a fill"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchTargetNotFound);
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::InsertNode);
		Edit.ParentId = TEXT("Rows");
		Edit.TypeName = TEXT("Text");
		Edit.NewId = TEXT("Stray");
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = ApplyOne(Text, Edit, Diagnostics);
		ExpectRefusal(*this, TEXT("a node inside a declaration"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchTargetNotFound);
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::InsertNode);
		Edit.ParentId = TEXT("Content");
		Edit.TypeName = TEXT("Image");
		Edit.NewId = TEXT("Icon");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("a node is written into a fill"), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(TEXT("after the content already there"), Text,
			Inserting(Lines, 11, { TEXT("            Image Icon {"), TEXT("            }") }));
		ExpectParses(*this, Text, TEXT("adding to a fill"));
	}
	{
		// The designer's drop into a named slot of an instance: the instance is the parent, the slot rides along.
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::InsertNode);
		Edit.ParentId = TEXT("Confirm");
		Edit.FillSlotName = TEXT("Content");
		Edit.ChildIndex = 0;
		Edit.TypeName = TEXT("Image");
		Edit.NewId = TEXT("Dot");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("content for a slot with a fill is written"), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(TEXT("into that fill"), Text, Inserting(Lines, 11, { TEXT("            Image Dot {"), TEXT("            }") }));
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::InsertNode);
		Edit.ParentId = TEXT("Confirm");
		Edit.FillSlotName = TEXT("Badge");
		Edit.TypeName = TEXT("Image");
		Edit.NewId = TEXT("Dot");
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(TEXT("content for a slot with no fill yet is written"), ApplyOne(Text, Edit, Diagnostics));
		TestEqual(TEXT("in a new fill after the instance's other content"), Text,
			Inserting(Lines, 12, {
				TEXT("        slot Badge {"),
				TEXT("            Image Dot {"),
				TEXT("            }"),
				TEXT("        }")
			}));
		ExpectParses(*this, Text, TEXT("writing a new fill"));
	}
	{
		FString Text = Source;
		FDreamUIStructuralEdit Edit = Structural(EDreamUIStructuralEditKind::InsertComponent);
		Edit.NodeId = TEXT("Footer");
		Edit.ComponentClassName = TEXT("Overlay");
		FDreamUIDiagnosticBag Diagnostics;
		const bool bWritten = ApplyOne(Text, Edit, Diagnostics);
		ExpectRefusal(*this, TEXT("a behaviour on a bare declaration"), Source, Text, bWritten, Diagnostics,
			EDreamUIDiagnosticCode::PatchTargetNotFound);
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackRoundTripTest,
	"DreamGUI.Text.Syntax.AFileUsingEveryNewConstructComesBackFromANoOpWriteBackByteForByte",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The write-back's first duty, on the syntax at its fullest: a namespace with a re-exported alias and a style, a
 * class-path alias, props and events, a style carrying a component and a shorthand, container types, nodes with no id,
 * `@slot { … }`, `@fill 2`, an emit route, `if … else`, a `for`, a written `Shown` binding. The designer's tree is
 * built from the same text; nothing differs; so nothing may be written -- not a renormalised value, not a line for a
 * property a style's component or a branch's condition holds, not a copy of a template.
 */
bool FDreamUISyntaxWriteBackRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const FScopedDuiFile Library(TEXT("SyntaxWriteBackLibrary.dui"), LibraryText());
	const FString Source = Join(EveryConstructLines(Library.Path));

	FBuiltTree Live = BuildLive(Source);
	if (!TestTrue(*FString::Printf(TEXT("the file builds (%s)"), *Live.Diagnostics.ToString()), Live.Tree.IsValid()))
	{
		return false;
	}

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	TArray<FDreamUIPropertyEdit> Edits;
	const bool bProduced = FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics, &Edits);
	if (!TestTrue(*FString::Printf(TEXT("the write-back computed an answer (%s)"), *Diagnostics.ToString()), bProduced))
	{
		return false;
	}
	TestEqualSensitive(TEXT("and left the file exactly as it was"), Produced, Source);
	TestEqual(TEXT("with not one edit attempted"), Edits.Num(), 0);
	const bool bRefusedSomething = Diagnostics.Diagnostics.ContainsByPredicate([](const FDreamUIDiagnostic& InDiagnostic)
	{
		return static_cast<int32>(InDiagnostic.Code) >= 7000;
	});
	TestFalse(*FString::Printf(TEXT("and nothing to refuse (%s)"), *Diagnostics.ToString()), bRefusedSomething);
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackDesignerEditsTest,
	"DreamGUI.Text.Syntax.DesignerEditsOnTheNewSyntaxRewriteTheirOwnLinesAndSayWhatTheyCannot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The same file, three values changed on the designer's tree in one gesture: the weight of an unnamed box written as
 * `@fill 2`, the spacing of a container-typed node, and the spacing of a container a STYLE gives its node. The first
 * two rewrite exactly their own lines -- the shorthand's number, the node's bare `Spacing` -- and the third is said
 * (DUI7005) and written nowhere: the style is shared, and the node has no `+` line of its own to hold it.
 */
bool FDreamUISyntaxWriteBackDesignerEditsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const FScopedDuiFile Library(TEXT("SyntaxWriteBackLibrary.dui"), LibraryText());
	const TArray<FString> Lines = EveryConstructLines(Library.Path);
	const FString Source = Join(Lines);

	FBuiltTree Live = BuildLive(Source);
	if (!TestTrue(*FString::Printf(TEXT("the file builds (%s)"), *Live.Diagnostics.ToString()), Live.Tree.IsValid()))
	{
		return false;
	}
	const FDreamUINode* MenuNode = FindNode(Live.Ast, TEXT("Menu"));
	if (!TestNotNull(TEXT("the menu node is in the tree"), MenuNode))
	{
		return false;
	}
	UDreamWidget* Box = Live.Find(AnonymousIdOf(*MenuNode, TEXT("HorizontalBox")));
	UDreamWidget* Detail = Live.Find(TEXT("Detail"));
	UDreamWidget* Menu = Live.Find(TEXT("Menu"));
	if (!TestNotNull(TEXT("the unnamed box was built"), Box) || !TestNotNull(TEXT("the detail was built"), Detail)
		|| !TestNotNull(TEXT("the menu was built"), Menu))
	{
		return false;
	}
	TestTrue(TEXT("the box's weight is changed"), SetNumber(Box->GetPanelSlot(), TEXT("FillWeight"), 3.0));
	TestTrue(TEXT("the detail's spacing is changed"), SetNumber(Detail->GetLayoutContainer(), TEXT("Spacing"), 10.0));
	TestTrue(TEXT("the menu's spacing is changed"), SetNumber(Menu->GetLayoutContainer(), TEXT("Spacing"), 30.0));

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bProduced = FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics);
	if (!TestTrue(*FString::Printf(TEXT("the write-back computed an answer (%s)"), *Diagnostics.ToString()), bProduced))
	{
		return false;
	}
	TArray<FString> Expected = Lines;
	Expected[25] = TEXT("            @fill 3");
	Expected[41] = TEXT("        Spacing = 10");
	TestEqualSensitive(TEXT("the shorthand's weight and the node's spacing are written, and nothing else moved"), Produced, Join(Expected));
	TestTrue(*FString::Printf(TEXT("the style's spacing is said, not written (%s)"), *Diagnostics.ToString()),
		Reported(Diagnostics, EDreamUIDiagnosticCode::PatchStyleComponentNotWritable));
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackAddedNodeTypeTest,
	"DreamGUI.Text.Syntax.ANodeAddedInTheDesignerIsWrittenByTheFilesAliasOrAsItsContainer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget made in the designer reaches the file under the name THIS file has for its class: the `use … as` alias,
 * rather than the registry tag or the path the class would otherwise be written by. A plain widget that lays out its
 * children is written as its container, which is the one spelling that keeps the container -- and its values then
 * reach the node's own lines.
 */
bool FDreamUISyntaxWriteBackAddedNodeTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const FString Source = Join({
		TEXT("use /Script/DreamGUIControls.DreamButton as Button"),
		TEXT(""),
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { }"),
		TEXT("    Text Title {"),
		TEXT("        Text = \"Hello\""),
		TEXT("    }"),
		TEXT("}")
	});

	FBuiltTree Live = BuildLive(Source);
	UDreamWidget* Root = Live.Find(TEXT("Root"));
	if (!TestNotNull(*FString::Printf(TEXT("the fixture builds (%s)"), *Live.Diagnostics.ToString()), Root))
	{
		return false;
	}

	UDreamWidget* Confirm = Live.Tree->ConstructWidget(UDreamButton::StaticClass(), TEXT("Confirm"), FGuid::NewGuid());
	UDreamWidget* Column = Live.Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Column"), FGuid::NewGuid());
	if (!TestNotNull(TEXT("a button was made"), Confirm) || !TestNotNull(TEXT("a plain widget was made"), Column))
	{
		return false;
	}
	Confirm->SetDisplayName(TEXT("Confirm"));
	Confirm->TrySetParent(Root, false);
	Column->SetDisplayName(TEXT("Column"));
	Column->TrySetParent(Root, false);
	UDreamLayoutContainer* Container = Column->CreateNewLayoutContainer(UDreamLayoutContainerVerticalBox::StaticClass());
	Column->SyncRequiredBehavioursForLayoutContainer(nullptr, Container);
	TestTrue(TEXT("the new container's spacing is set"), SetNumber(Container, TEXT("Spacing"), 7.0));

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bProduced = FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics);
	if (!TestTrue(*FString::Printf(TEXT("the write-back computed an answer (%s)"), *Diagnostics.ToString()), bProduced))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("the button is written by the file's alias:\n%s"), *Produced), Produced.Contains(TEXT("Button Confirm")));
	TestFalse(TEXT("and not by its registry tag"), Produced.Contains(TEXT("Native.Button")));
	TestTrue(*FString::Printf(TEXT("the plain widget is written as its container:\n%s"), *Produced), Produced.Contains(TEXT("VerticalBox Column")));
	TestTrue(TEXT("with the container's value on a line of its own"), Produced.Contains(TEXT("Spacing = 7")));

	FBuiltTree ReadBack = BuildLive(Produced);
	UDreamWidget* ReadConfirm = ReadBack.Find(TEXT("Confirm"));
	UDreamWidget* ReadColumn = ReadBack.Find(TEXT("Column"));
	if (TestNotNull(*FString::Printf(TEXT("the written file builds the button back (%s)"), *ReadBack.Diagnostics.ToString()), ReadConfirm))
	{
		TestEqual(TEXT("as a button"), ReadConfirm->GetClass(), UDreamButton::StaticClass());
	}
	if (TestNotNull(TEXT("and the column"), ReadColumn) && TestNotNull(TEXT("with a container"), ReadColumn->GetLayoutContainer()))
	{
		TestEqual(TEXT("of the same class"), ReadColumn->GetLayoutContainer()->GetClass(), UDreamLayoutContainerVerticalBox::StaticClass());
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUISyntaxWriteBackTransientCopiesTest,
	"DreamGUI.Text.Syntax.TheCopiesAForMakesAtRunTimeNeverReachTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A `for` makes a copy of its template per item, among the host's own children -- under a parent the file declares.
 * The structural diff writes any widget it finds there that the file does not name, so the copies are told apart by
 * what they are, transient: one per item would otherwise be written as a node of its own on every flush, and one
 * carrying the template's name would have its per-item values written over the template's lines.
 */
bool FDreamUISyntaxWriteBackTransientCopiesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUISyntaxWriteBackTestLocal;

	const FString Source = Join({
		TEXT("Widget Root {"),
		TEXT("    VerticalBox Options {"),
		TEXT("        for Option in Options {"),
		TEXT("            Text OptionLabel {"),
		TEXT("                Text <- Option.Label"),
		TEXT("            }"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	});

	FBuiltTree Live = BuildLive(Source);
	UDreamWidget* Options = Live.Find(TEXT("Options"));
	if (!TestNotNull(*FString::Printf(TEXT("the fixture builds (%s)"), *Live.Diagnostics.ToString()), Options))
	{
		return false;
	}
	for (const TCHAR* CopyName : { TEXT("OptionLabel_1"), TEXT("OptionLabel") })
	{
		UDreamWidget* Copy = Live.Tree->ConstructWidget(UDreamWidget::StaticClass(), NAME_None, FGuid::NewGuid());
		if (!TestNotNull(TEXT("a copy was made"), Copy))
		{
			return false;
		}
		Copy->SetFlags(RF_Transient);
		Copy->SetDisplayName(CopyName);
		Copy->TrySetParent(Options, false);
	}

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	TArray<FDreamUIPropertyEdit> Edits;
	const bool bProduced = FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics, &Edits);
	if (!TestTrue(*FString::Printf(TEXT("the write-back computed an answer (%s)"), *Diagnostics.ToString()), bProduced))
	{
		return false;
	}
	TestEqualSensitive(TEXT("no copy reached the file"), Produced, Source);
	TestEqual(TEXT("and no value of one was compared against the template"), Edits.Num(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
