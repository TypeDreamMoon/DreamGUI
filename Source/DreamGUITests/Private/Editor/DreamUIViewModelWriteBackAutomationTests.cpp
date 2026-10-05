// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "DreamViewModelTestTypes.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUISymbolExport.h"
#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * The designer's write-back against a file that holds view models: a `viewmodels` block, member-path bindings, member
 * routes with and without arguments, `+=` routes, a `<->` onto a member path, a loop over a member path with a route to
 * its item. None of it is the designer's to write -- the write-back walks widgets and their values, and leaves every
 * other line where it is -- so the rule is the one every write-back case holds: opening the file writes nothing, and an
 * edit moves its own line and nothing else.
 *
 * And the editor's other reader of the language, the symbol export, for the view models a completion offers.
 */
namespace DreamUIViewModelWriteBackTestLocal
{
	FString Join(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	TArray<FString> Inserting(const TArray<FString>& InLines, int32 InAfterOneBasedLine, const TArray<FString>& InNewLines)
	{
		TArray<FString> Lines = InLines;
		Lines.Insert(InNewLines, InAfterOneBasedLine);
		return Lines;
	}

	bool Parse(const FString& InText, FDreamUIAst& OutAst, FDreamUIDiagnosticBag& OutDiagnostics)
	{
		return FDreamUISourceFile::Parse(InText, TEXT("ViewModelWriteBack.dui"), OutAst, OutDiagnostics,
			FDreamUISourceFile::MakeFileImportReader());
	}

	/** Every construct view models brought, at once. Line numbers are the array's, so a case can say "line 12". */
	TArray<FString> ViewModelLines()
	{
		return {
			TEXT("viewmodels {"),                                                //  1
			TEXT("    SettingsVM Settings = new"),                               //  2
			TEXT("    InventoryVM Inventory = global \"Stash\""),                //  3
			TEXT("    PlayerVM Player"),                                         //  4
			TEXT("}"),                                                           //  5
			TEXT(""),                                                            //  6
			TEXT("VerticalBox Root {"),                                          //  7
			TEXT("    Spacing = 16"),                                            //  8
			TEXT("    Text Title {"),                                            //  9
			TEXT("        Text <- Settings.TitleText"),                          // 10
			TEXT("        FontSize = 24"),                                       // 11
			TEXT("    }"),                                                       // 12
			TEXT("    Native.Slider Volume {"),                                  // 13
			TEXT("        Value <-> Settings.MasterVolume"),                     // 14
			TEXT("        OnValueChanged -> Settings.SetVolume(Value)"),         // 15
			TEXT("    }"),                                                       // 16
			TEXT("    if Settings.IsDirty() {"),                                 // 17
			TEXT("        Text Hint { Text = \"Unsaved changes\" }"),            // 18
			TEXT("    }"),                                                       // 19
			TEXT("    Native.Button Apply {"),                                   // 20
			TEXT("        OnClicked -> Settings.Apply()"),                       // 21
			TEXT("    }"),                                                       // 22
			TEXT("    Native.Button Reset {"),                                   // 23
			TEXT("        OnClicked += Player.Stats.Reset()"),                   // 24
			TEXT("        OnClicked += HandleReset"),                            // 25
			TEXT("    }"),                                                       // 26
			TEXT("    VerticalBox Rows {"),                                      // 27
			TEXT("        for Item in Inventory.Items {"),                       // 28
			TEXT("            Native.Button Row {"),                             // 29
			TEXT("                ToolTipText <- Item.Name"),                    // 30
			TEXT("                OnClicked -> Item.Use()"),                     // 31
			TEXT("            }"),                                               // 32
			TEXT("        }"),                                                   // 33
			TEXT("    }"),                                                       // 34
			TEXT("}"),                                                           // 35
		};
	}

	/** The tree the write-back would build from this text, standing in for the designer's. */
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

	FBuiltTree BuildLive(const FString& InText)
	{
		FBuiltTree Built;
		Built.Tree.Reset(FDreamUITextWriteBack::BuildReferenceTree(InText, Built.Ast, Built.Diagnostics));
		return Built;
	}

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

	bool RefusedSomething(const FDreamUIDiagnosticBag& InDiagnostics)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate([](const FDreamUIDiagnostic& InDiagnostic)
		{
			return static_cast<int32>(InDiagnostic.Code) >= 7000;
		});
	}

	/** InObject's field InName as an object, or null. */
	TSharedPtr<FJsonObject> ObjectField(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InName)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		return InObject.IsValid() && InObject->TryGetObjectField(InName, Found) && Found != nullptr ? *Found : TSharedPtr<FJsonObject>();
	}
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelWriteBackRoundTripTest,
	"DreamGUI.Text.ViewModel.WriteBack.AFileWithViewModelsComesBackFromANoOpWriteBackByteForByte",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelWriteBackRoundTripTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelWriteBackTestLocal;

	const FString Source = Join(ViewModelLines());
	FBuiltTree Live = BuildLive(Source);
	if (!TestTrue(*FString::Printf(TEXT("the file builds as the write-back builds it (%s)"), *Live.Diagnostics.ToString()), Live.Tree.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("the parse kept every view model"), Live.Ast.ViewModels.Num(), 3);

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
	TestFalse(*FString::Printf(TEXT("and nothing to refuse (%s)"), *Diagnostics.ToString()), RefusedSomething(Diagnostics));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelWriteBackDesignerEditTest,
	"DreamGUI.Text.ViewModel.WriteBack.AnEditBesideTheViewModelSyntaxRewritesItsOwnLineAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIViewModelWriteBackDesignerEditTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelWriteBackTestLocal;

	const TArray<FString> Lines = ViewModelLines();
	const FString Source = Join(Lines);
	FBuiltTree Live = BuildLive(Source);
	if (!TestTrue(*FString::Printf(TEXT("the file builds (%s)"), *Live.Diagnostics.ToString()), Live.Tree.IsValid()))
	{
		return false;
	}
	UDreamWidget* Root = Live.Find(TEXT("Root"));
	if (!TestNotNull(TEXT("the root was built"), Root))
	{
		return false;
	}
	TestTrue(TEXT("the root's spacing is changed"), SetNumber(Root->GetLayoutContainer(), TEXT("Spacing"), 20.0));

	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	const bool bProduced = FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics);
	if (!TestTrue(*FString::Printf(TEXT("the write-back computed an answer (%s)"), *Diagnostics.ToString()), bProduced))
	{
		return false;
	}
	TArray<FString> Expected = Lines;
	Expected[7] = TEXT("    Spacing = 20");
	TestEqualSensitive(TEXT("the spacing line is rewritten, and the view model block, the paths and the routes are not touched"),
		Produced, Join(Expected));
	TestFalse(*FString::Printf(TEXT("and nothing was refused (%s)"), *Diagnostics.ToString()), RefusedSomething(Diagnostics));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelWriteBackPatchTest,
	"DreamGUI.Text.ViewModel.WriteBack.ANewLineGoesAfterAMemberRouteOrATwoWayPathAsAfterAnyStatement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A property added in the designer is written after the last statement of its node. When that statement is a route with
 * an argument list, or a `<->` onto a member path, its line has to be found and stepped past exactly as a value's is --
 * the parentheses of `Settings.SetVolume(Value)` are not a block to stop inside.
 */
bool FDreamUIViewModelWriteBackPatchTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelWriteBackTestLocal;

	const TArray<FString> Lines = ViewModelLines();
	const FString Source = Join(Lines);
	{
		FString Text = Source;
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		if (!TestTrue(*FString::Printf(TEXT("the fixture parses (%s)"), *ParseDiagnostics.ToString()), Parse(Text, Ast, ParseDiagnostics)))
		{
			return false;
		}
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(*FString::Printf(TEXT("a property of the slider is written (%s)"), *Diagnostics.ToString()),
			FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Volume"), EDreamUIPatchTarget::Node, INDEX_NONE,
				TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics));
		TestEqualSensitive(TEXT("after the member route, which closes its own line"), Text,
			Join(Inserting(Lines, 15, { TEXT("        RenderOpacity = 0.5") })));
	}
	{
		FString Text = Source;
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		if (!TestTrue(TEXT("the fixture parses"), Parse(Text, Ast, ParseDiagnostics)))
		{
			return false;
		}
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(*FString::Printf(TEXT("a property of the button with two '+=' lines is written (%s)"), *Diagnostics.ToString()),
			FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Reset"), EDreamUIPatchTarget::Node, INDEX_NONE,
				TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics));
		TestEqualSensitive(TEXT("after the last of them"), Text,
			Join(Inserting(Lines, 25, { TEXT("        RenderOpacity = 0.5") })));
	}
	{
		FString Text = Source;
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag ParseDiagnostics;
		if (!TestTrue(TEXT("the fixture parses"), Parse(Text, Ast, ParseDiagnostics)))
		{
			return false;
		}
		FDreamUIDiagnosticBag Diagnostics;
		TestTrue(*FString::Printf(TEXT("a property of a loop's template with an item route is written (%s)"), *Diagnostics.ToString()),
			FDreamUITextPatcher::SetProperty(Text, Ast, TEXT("Row"), EDreamUIPatchTarget::Node, INDEX_NONE,
				TEXT("RenderOpacity"), TEXT("0.5"), Diagnostics));
		TestEqualSensitive(TEXT("into the template, after its route"), Text,
			Join(Inserting(Lines, 31, { TEXT("                RenderOpacity = 0.5") })));
	}
	return true;
}

// -------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIViewModelSymbolsTest,
	"DreamGUI.Text.ViewModel.Symbols.TheExportListsEveryViewModelClassAndWhatAPathCanReachOnIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * What the VS Code extension completes a `viewmodels` line and a member path from. Asserted against the test view models
 * (DreamViewModelTestTypes.h), whose members are chosen to cover every answer: FieldNotify or not, writable through the
 * property, through a `Set` function or not at all, a function with and without parameters, a nested view model and a
 * list of them.
 */
bool FDreamUIViewModelSymbolsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIViewModelWriteBackTestLocal;

	const TSharedPtr<FJsonObject> ViewModels = FDreamUISymbolExport::DescribeViewModels();
	const TSharedPtr<FJsonObject> Player = ObjectField(ViewModels, TEXT("DreamTestPlayerVM"));
	if (!TestTrue(TEXT("a C++ view model is listed under its reflected name"), Player.IsValid()))
	{
		return false;
	}
	TestEqual(TEXT("with the path a .dui can also write it by"), Player->GetStringField(TEXT("class")),
		UDreamTestPlayerVM::StaticClass()->GetPathName());
	TestFalse(TEXT("and it is not abstract"), Player->HasField(TEXT("abstract")));

	const TSharedPtr<FJsonObject> Members = ObjectField(Player, TEXT("members"));
	if (!TestTrue(TEXT("its members are listed"), Members.IsValid()))
	{
		return false;
	}

	const TSharedPtr<FJsonObject> Health = ObjectField(Members, TEXT("Health"));
	TestTrue(TEXT("a FieldNotify BlueprintReadWrite float"), Health.IsValid()
		&& Health->GetStringField(TEXT("kind")) == TEXT("property") && Health->GetStringField(TEXT("type")) == TEXT("Float")
		&& Health->GetBoolField(TEXT("fieldNotify")) && Health->GetBoolField(TEXT("writable")));

	const TSharedPtr<FJsonObject> Level = ObjectField(Members, TEXT("Level"));
	TestTrue(TEXT("a read-only one whose SetLevel is no UFUNCTION cannot be written back"), Level.IsValid()
		&& Level->GetStringField(TEXT("type")) == TEXT("Integer") && Level->GetBoolField(TEXT("fieldNotify"))
		&& !Level->GetBoolField(TEXT("writable")));

	const TSharedPtr<FJsonObject> Untracked = ObjectField(Members, TEXT("Untracked"));
	TestTrue(TEXT("a member nothing announces says so"), Untracked.IsValid() && !Untracked->GetBoolField(TEXT("fieldNotify")));

	const TSharedPtr<FJsonObject> Name = ObjectField(Members, TEXT("Name"));
	TestTrue(TEXT("text is Text"), Name.IsValid() && Name->GetStringField(TEXT("type")) == TEXT("Text"));
	const TSharedPtr<FJsonObject> Dead = ObjectField(Members, TEXT("bIsDead"));
	TestTrue(TEXT("a bool is Bool"), Dead.IsValid() && Dead->GetStringField(TEXT("type")) == TEXT("Bool"));
	const TSharedPtr<FJsonObject> Stats = ObjectField(Members, TEXT("Stats"));
	TestTrue(TEXT("a nested view model names its class"), Stats.IsValid()
		&& Stats->GetStringField(TEXT("type")) == TEXT("Object<DreamTestStatsVM>"));
	const TSharedPtr<FJsonObject> Items = ObjectField(Members, TEXT("Items"));
	TestTrue(TEXT("and a list of them its element class"), Items.IsValid()
		&& Items->GetStringField(TEXT("type")) == TEXT("Array<Object<DreamTestItemVM>>"));

	const TSharedPtr<FJsonObject> Percent = ObjectField(Members, TEXT("GetHealthPercent"));
	if (TestTrue(TEXT("a FieldNotify function is listed as one"), Percent.IsValid()))
	{
		TestEqual(TEXT("a function"), Percent->GetStringField(TEXT("kind")), FString(TEXT("function")));
		TestEqual(TEXT("of its return type"), Percent->GetStringField(TEXT("type")), FString(TEXT("Float")));
		TestTrue(TEXT("announced"), Percent->GetBoolField(TEXT("fieldNotify")));
		TestEqual(TEXT("taking nothing"), Percent->GetArrayField(TEXT("params")).Num(), 0);
	}
	const TSharedPtr<FJsonObject> FormatGold = ObjectField(Members, TEXT("FormatGold"));
	if (TestTrue(TEXT("a function with a parameter is listed"), FormatGold.IsValid()))
	{
		const TArray<TSharedPtr<FJsonValue>>& Params = FormatGold->GetArrayField(TEXT("params"));
		if (TestEqual(TEXT("with its one parameter"), Params.Num(), 1))
		{
			const TSharedPtr<FJsonObject> Param = Params[0].IsValid() ? Params[0]->AsObject() : TSharedPtr<FJsonObject>();
			TestTrue(TEXT("by name and type"), Param.IsValid() && Param->GetStringField(TEXT("name")) == TEXT("InGold")
				&& Param->GetStringField(TEXT("type")) == TEXT("Integer"));
		}
		TestFalse(TEXT("and not announced"), FormatGold->GetBoolField(TEXT("fieldNotify")));
	}
	const TSharedPtr<FJsonObject> Apply = ObjectField(Members, TEXT("Apply"));
	TestTrue(TEXT("a command returns nothing"), Apply.IsValid() && Apply->GetStringField(TEXT("type")) == TEXT("Void"));
	const TSharedPtr<FJsonObject> Volume = ObjectField(Members, TEXT("Volume"));
	TestTrue(TEXT("a property `<->` writes back through its Set function is writable"), Volume.IsValid() && Volume->GetBoolField(TEXT("writable")));

	TestFalse(TEXT("the base class's delegate plumbing is left out"),
		Members->HasField(TEXT("K2_AddFieldValueChangedDelegate")) || Members->HasField(TEXT("K2_BroadcastFieldValueChanged")));

	const TSharedPtr<FJsonObject> Base = ObjectField(ViewModels, TEXT("DreamViewModel"));
	TestTrue(TEXT("the abstract base is listed, and says it is abstract"), Base.IsValid() && Base->HasField(TEXT("abstract"))
		&& Base->GetBoolField(TEXT("abstract")));
	TestFalse(TEXT("an object that announces nothing is no view model"), ViewModels->HasField(TEXT("DreamTestPlainModel")));
	TestFalse(TEXT("and a widget is never one, though it announces its fields"), ViewModels->HasField(TEXT("DreamUserWidget")));
	return true;
}

#endif
