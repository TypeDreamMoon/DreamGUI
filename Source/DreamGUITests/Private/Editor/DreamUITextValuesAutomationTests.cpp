// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUITextStructureTestFixtures.h"
#include "DreamWidgetBehaviourTestTypes.h"

#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Interaction/UIButton.h"

#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * The values a `.dui` could not hold before, and the shapes the designer could not write: a list, a set and a map
 * (`[a, b]`, `{ key: value }`); a node reference, written as the id of the node it points at; a value a style gives,
 * written as an override on the node; `@fill 2` given another size rule; and the rows of a `rows` table.
 */

namespace DreamUITextValuesTestLocal
{
	using namespace DreamUITextStructureFixtures;

	FString Lines(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	struct FLiveTree
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		FDreamUIDiagnosticBag Diagnostics;

		explicit FLiveTree(const FString& InText)
		{
			FDreamUIAst Ast;
			Diagnostics.SourceName = TEXT("Values.dui");
			Tree.Reset(FDreamUITextWriteBack::BuildReferenceTree(InText, Ast, Diagnostics));
		}

		UDreamWidget* Find(const FString& InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([&Found, &InDisplayName](UDreamWidget* Widget)
				{
					if (Found == nullptr && Widget->GetDisplayName() == InDisplayName)
					{
						Found = Widget;
					}
				});
			}
			return Found;
		}
	};

	template<typename BehaviourType>
	BehaviourType* FindBehaviour(const UDreamWidget* InWidget)
	{
		if (InWidget != nullptr)
		{
			for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
			{
				if (BehaviourType* Found = Cast<BehaviourType>(Behaviour))
				{
					return Found;
				}
			}
		}
		return nullptr;
	}

	bool HasCode(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.Code == InCode; });
	}

	FString ContainerPath()
	{
		return UDreamUIContainerTestBehaviour::StaticClass()->GetPathName();
	}

	FString Produce(const FString& InSource, const FLiveTree& InLive)
	{
		FString Updated;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUITextWriteBack::ProduceText(InSource, InLive.Tree.Get(), Updated, Diagnostics);
		return Updated;
	}
}

// =================================================================================================
// Lists, sets and maps
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIContainerLiteralsTest,
	"DreamGUI.Text.Containers.AListASetAndAMapAreWrittenReadAndBuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIContainerLiteralsTest::RunTest(const FString&)
{
	using namespace DreamUITextValuesTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget Holder {"),
		FString::Printf(TEXT("        + %s {"), *ContainerPath()),
		TEXT("            Names = [\"Alpha\", \"Beta\"]"),
		TEXT("            Tags = [\"B\", \"A\"]"),
		TEXT("            Colors = { Hover: #FFCC00, Pressed: #AA8800 }"),
		TEXT("            Sizes = ["),
		TEXT("                (1, 2),"),
		TEXT("                (3, 4),"),
		TEXT("            ]"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}")
	});

	// ---- read
	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the file parses"), ParseText(Source, Ast))) return false;
	const FDreamUINode* Holder = FindNode(Ast, TEXT("Holder"));
	if (!TestTrue(TEXT("with the behaviour's block"), Holder != nullptr && Holder->Components.Num() == 1)) return false;
	const TArray<FDreamUIProperty>& Block = Holder->Components[0].Properties;
	const FDreamUIProperty* Names = Block.FindByPredicate([](const FDreamUIProperty& P) { return P.Name == TEXT("Names"); });
	const FDreamUIProperty* Colors = Block.FindByPredicate([](const FDreamUIProperty& P) { return P.Name == TEXT("Colors"); });
	const FDreamUIProperty* Sizes = Block.FindByPredicate([](const FDreamUIProperty& P) { return P.Name == TEXT("Sizes"); });
	if (TestNotNull(TEXT("a list reads as one"), Names))
	{
		TestTrue(TEXT("with its two elements"), Names->Value.Kind == EDreamUIValueKind::List && Names->Value.Items.Num() == 2);
	}
	if (TestNotNull(TEXT("a map reads as one"), Colors))
	{
		TestTrue(TEXT("with its keys and values"), Colors->Value.Kind == EDreamUIValueKind::Map && Colors->Value.Keys.Num() == 2
			&& Colors->Value.Items.Num() == 2 && Colors->Value.Keys[0].Raw == TEXT("Hover"));
	}
	if (TestNotNull(TEXT("a list across lines, with a trailing comma, reads as one"), Sizes))
	{
		TestTrue(TEXT("of tuples"), Sizes->Value.Items.Num() == 2 && Sizes->Value.Items[1].Kind == EDreamUIValueKind::Tuple);
	}

	// ---- built
	const FLiveTree Live(Source);
	UDreamUIContainerTestBehaviour* Built = FindBehaviour<UDreamUIContainerTestBehaviour>(Live.Find(TEXT("Holder")));
	if (!TestNotNull(TEXT("the file builds, behaviour and all"), Built)) return false;
	TestEqual(TEXT("the list is the array"), FString::Join(Built->Names, TEXT(",")), FString(TEXT("Alpha,Beta")));
	TestTrue(TEXT("the set holds both"), Built->Tags.Num() == 2 && Built->Tags.Contains(FName(TEXT("A"))));
	TestTrue(TEXT("the map holds both keys"), Built->Colors.Num() == 2 && Built->Colors.Contains(FName(TEXT("Pressed"))));
	TestTrue(TEXT("the list of tuples is the array of vectors"), Built->Sizes.Num() == 2 && Built->Sizes[1].Equals(FVector2D(3.0, 4.0)));

	// ---- written back: an element added to the array, the set in the file's order, the map's one changed value
	Built->Names.Add(TEXT("Gamma"));
	Built->Tags.Add(FName(TEXT("C")));
	Built->Colors.Add(FName(TEXT("Hover")), FLinearColor::Black);
	const FString Updated = Produce(Source, Live);
	TestTrue(TEXT("the array is written whole"), Updated.Contains(TEXT("Names = [\"Alpha\", \"Beta\", \"Gamma\"]")));
	TestTrue(TEXT("the set keeps the file's order and adds after it"), Updated.Contains(TEXT("Tags = [\"B\", \"A\", \"C\"]")));
	TestTrue(TEXT("the map keeps its order with the new value"), Updated.Contains(TEXT("Colors = { Hover: #000000, Pressed: #AA8800 }")));
	TestTrue(TEXT("the untouched list across lines stays as written"), Updated.Contains(TEXT("                (3, 4),")));
	if (HasAnyErrors())
	{
		AddInfo(TEXT("written:\n") + Updated);
	}

	// ---- an untouched file writes nothing, whatever order its set comes back in
	const FLiveTree Untouched(Source);
	TestEqual(TEXT("nothing to write"), Produce(Source, Untouched), Source);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIContainerRefusalsTest,
	"DreamGUI.Text.Containers.AContainerThatDoesNotReadOrFitIsSaid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIContainerRefusalsTest::RunTest(const FString&)
{
	using namespace DreamUITextValuesTestLocal;

	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Diagnostics.SourceName = TEXT("Values.dui");
		FDreamUISourceFile::Parse(Lines({ TEXT("Widget Root {"), TEXT("    Names = [\"A\", \"B\""), TEXT("}") }),
			Diagnostics.SourceName, Ast, Diagnostics);
		TestTrue(TEXT("a list never closed is DUI2023"), HasCode(Diagnostics, EDreamUIDiagnosticCode::MalformedContainer));
	}
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Diagnostics.SourceName = TEXT("Values.dui");
		FDreamUISourceFile::Parse(Lines({ TEXT("Widget Root {"), TEXT("    Colors = { Hover #FFF }"), TEXT("}") }),
			Diagnostics.SourceName, Ast, Diagnostics);
		TestTrue(TEXT("a map entry with no ':' is DUI2023"), HasCode(Diagnostics, EDreamUIDiagnosticCode::MalformedContainer));
	}
	{
		const FLiveTree Live(Lines({
			TEXT("Widget Root {"),
			FString::Printf(TEXT("    + %s {"), *ContainerPath()),
			TEXT("        Single = [1, 2]"),
			TEXT("        Names = { A: \"x\" }"),
			TEXT("    }"),
			TEXT("}")
		}));
		TestTrue(TEXT("a list on a single value, and a map on an array, are DUI4009"),
			HasCode(Live.Diagnostics, EDreamUIDiagnosticCode::ContainerTypeMismatch));
	}
	return true;
}

// =================================================================================================
// Node references, a style's values, `@fill`
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUINodeReferenceWriteBackTest,
	"DreamGUI.Text.WriteBack.ANodeReferenceIsWrittenAsTheIdOfTheNodeItPointsAt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUINodeReferenceWriteBackTest::RunTest(const FString&)
{
	using namespace DreamUITextValuesTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    Widget First {"),
		TEXT("        + UIButton { }"),
		TEXT("    }"),
		TEXT("    Widget Second {"),
		TEXT("        + UIButton { }"),
		TEXT("    }"),
		TEXT("}")
	});
	const FLiveTree Live(Source);
	UUIButton* From = FindBehaviour<UUIButton>(Live.Find(TEXT("First")));
	UUIButton* To = FindBehaviour<UUIButton>(Live.Find(TEXT("Second")));
	if (!TestTrue(TEXT("both buttons are built"), From != nullptr && To != nullptr)) return false;
	FWeakObjectProperty* Down = CastField<FWeakObjectProperty>(From->GetClass()->FindPropertyByName(TEXT("NavigationDownSpecific")));
	if (!TestNotNull(TEXT("the explicit navigation is a property"), Down)) return false;
	Down->SetObjectPropertyValue(Down->ContainerPtrToValuePtr<void>(From), To);

	const FString Updated = Produce(Source, Live);
	TestTrue(TEXT("the reference is written as the node's id"), Updated.Contains(TEXT("NavigationDownSpecific = Second")));

	// And read back as that node's button.
	const FLiveTree Rebuilt(Updated);
	UUIButton* RebuiltFrom = FindBehaviour<UUIButton>(Rebuilt.Find(TEXT("First")));
	UUIButton* RebuiltTo = FindBehaviour<UUIButton>(Rebuilt.Find(TEXT("Second")));
	if (TestTrue(TEXT("the written file builds"), RebuiltFrom != nullptr && RebuiltTo != nullptr))
	{
		TestTrue(TEXT("and points at the same button"), RebuiltFrom->GetNavigationDownExplicit() == RebuiltTo);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIStyleOverrideWriteBackTest,
	"DreamGUI.Text.WriteBack.AValueAStyleGivesIsWrittenAsAnOverrideOnTheNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIStyleOverrideWriteBackTest::RunTest(const FString&)
{
	using namespace DreamUITextValuesTestLocal;

	const FString Source = Lines({
		TEXT("style Gap {"),
		TEXT("    + VerticalBox { Spacing = 12 }"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Widget Column : Gap {"),
		TEXT("    }"),
		TEXT("    Widget Other : Gap {"),
		TEXT("    }"),
		TEXT("}")
	});
	const FLiveTree Live(Source);
	UDreamWidget* Column = Live.Find(TEXT("Column"));
	if (!TestTrue(TEXT("the styled node has the style's panel"), Column != nullptr && Column->GetLayoutContainer() != nullptr)) return false;
	UDreamLayoutContainer* Container = Column->GetLayoutContainer();
	FProperty* Spacing = Container->GetClass()->FindPropertyByName(TEXT("Spacing"));
	if (!TestNotNull(TEXT("the panel has a spacing"), Spacing)) return false;
	Spacing->ImportText_Direct(TEXT("20"), Spacing->ContainerPtrToValuePtr<void>(Container), Container, PPF_None);

	FString Updated;
	FDreamUIDiagnosticBag Diagnostics;
	FDreamUIStructuralResidue Residue;
	TestTrue(TEXT("the write-back ran"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Updated, Diagnostics, nullptr, nullptr, &Residue));
	TestFalse(TEXT("no DUI7005 any more"), HasCode(Diagnostics, EDreamUIDiagnosticCode::PatchStyleComponentNotWritable));
	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the written file parses"), ParseText(Updated, Ast))) return false;
	const FDreamUINode* ColumnNode = FindNode(Ast, TEXT("Column"));
	const bool bOverride = ColumnNode != nullptr && ColumnNode->Components.ContainsByPredicate([](const FDreamUIComponent& InComponent)
	{
		return InComponent.ClassName == TEXT("VerticalBox") && InComponent.Properties.ContainsByPredicate([](const FDreamUIProperty& P)
		{
			return P.Name == TEXT("Spacing") && P.Value.Raw == TEXT("20");
		});
	});
	TestTrue(TEXT("the node gets its own '+ VerticalBox' with the new spacing"), bOverride);
	TestTrue(TEXT("the style is untouched"), Updated.Contains(TEXT("+ VerticalBox { Spacing = 12 }")));
	const FDreamUINode* OtherNode = FindNode(Ast, TEXT("Other"));
	TestTrue(TEXT("and so is the other node wearing it"), OtherNode != nullptr && OtherNode->Components.IsEmpty());
	TestTrue(TEXT("nothing left over"), Residue.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIFillShorthandRuleTest,
	"DreamGUI.Text.WriteBack.AFillWithAWeightGivenAnotherRuleIsWrittenAsItsTwoSlotLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIFillShorthandRuleTest::RunTest(const FString&)
{
	using namespace DreamUITextValuesTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    + HorizontalBox { }"),
		TEXT("    Widget Item {"),
		TEXT("        @fill 2"),
		TEXT("    }"),
		TEXT("}")
	});
	const FLiveTree Live(Source);
	UDreamWidget* Item = Live.Find(TEXT("Item"));
	UDreamPanelSlot* Slot = Item != nullptr ? Item->GetPanelSlot() : nullptr;
	if (!TestNotNull(TEXT("the item sits in a slot"), Slot)) return false;
	FProperty* SizeRule = Slot->GetClass()->FindPropertyByName(TEXT("SizeRule"));
	if (!TestNotNull(TEXT("the slot has a size rule"), SizeRule)) return false;
	SizeRule->ImportText_Direct(TEXT("Auto"), SizeRule->ContainerPtrToValuePtr<void>(Slot), Slot, PPF_None);

	const FString Updated = Produce(Source, Live);
	TestFalse(TEXT("the shorthand is gone"), Updated.Contains(TEXT("@fill")));
	TestTrue(TEXT("its rule is written as the new one"), Updated.Contains(TEXT("@slot SizeRule = Auto")));
	TestTrue(TEXT("and its weight kept on a line of its own"), Updated.Contains(TEXT("@slot FillWeight = 2")));
	FDreamUIAst Ast;
	TestTrue(TEXT("the written file parses"), ParseText(Updated, Ast));
	return true;
}

// =================================================================================================
// Rows
// =================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIRowsStructureTest,
	"DreamGUI.Text.Patcher.ARowIsRemovedMovedInsideItsTableOrCopied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIRowsStructureTest::RunTest(const FString&)
{
	using namespace DreamUITextValuesTestLocal;

	const FString Source = Lines({
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { }"),
		TEXT("    rows Text (Text) {"),
		TEXT("        \"Alpha\""),
		TEXT("        \"Beta\""),
		TEXT("        \"Gamma\" { HAlign = Right }"),
		TEXT("    }"),
		TEXT("}")
	});
	FDreamUIAst Ast;
	if (!TestTrue(TEXT("the table parses"), ParseText(Source, Ast))) return false;
	// A row is named by its key: `<parent>__<type>_<key>`.
	auto RowId = [](const TCHAR* InKey) { return FString::Printf(TEXT("Root__Text_%s"), InKey); };
	auto Apply = [&Ast, &Source](const FDreamUIStructuralEdit& InEdit, FString& OutText)
	{
		OutText = Source;
		FDreamUIDiagnosticBag Diagnostics;
		return FDreamUITextPatcher::ApplyStructuralEdits(OutText, Ast, MakeArrayView(&InEdit, 1), Diagnostics);
	};
	// The rows of a text, by the key part of their ids, in file order.
	auto Keys = [](const FString& InText)
	{
		FDreamUIAst Parsed;
		TArray<FString> Out;
		if (ParseText(InText, Parsed))
		{
			Parsed.ForEachNode([&Out](const FDreamUINode& InNode)
			{
				if (!InNode.RowKey.IsEmpty())
				{
					Out.Add(InNode.Id.RightChop(FCString::Strlen(TEXT("Root__Text_"))));
				}
			});
		}
		return FString::Join(Out, TEXT(","));
	};

	{
		FDreamUIStructuralEdit Remove;
		Remove.Kind = EDreamUIStructuralEditKind::RemoveNode;
		Remove.NodeId = RowId(TEXT("Beta"));
		FString Text;
		TestTrue(TEXT("a row is removed"), Apply(Remove, Text));
		TestEqual(TEXT("its line with it"), Keys(Text), FString(TEXT("Alpha,Gamma")));
	}
	{
		FDreamUIStructuralEdit Move;
		Move.Kind = EDreamUIStructuralEditKind::MoveNode;
		Move.NodeId = RowId(TEXT("Gamma"));
		Move.BeforeId = RowId(TEXT("Alpha"));
		FString Text;
		TestTrue(TEXT("a row is moved inside its table"), Apply(Move, Text));
		TestEqual(TEXT("block and all"), Keys(Text), FString(TEXT("Gamma,Alpha,Beta")));
		TestTrue(TEXT("its block went with it"), Text.Contains(TEXT("        \"Gamma\" { HAlign = Right }\n        \"Alpha\"")));
	}
	{
		// Without BeforeId, a place by index: index 0 of the parent, before the whole table, is the table's first row.
		FDreamUIStructuralEdit Move;
		Move.Kind = EDreamUIStructuralEditKind::MoveNode;
		Move.NodeId = RowId(TEXT("Beta"));
		Move.ChildIndex = 0;
		FString Text;
		TestTrue(TEXT("a row is moved by index"), Apply(Move, Text));
		TestEqual(TEXT("to the top of its table"), Keys(Text), FString(TEXT("Beta,Alpha,Gamma")));
	}
	{
		FDreamUIStructuralEdit Copy;
		Copy.Kind = EDreamUIStructuralEditKind::InsertRow;
		Copy.NodeId = RowId(TEXT("Alpha"));
		FString Text;
		TestTrue(TEXT("a row is copied"), Apply(Copy, Text));
		TestEqual(TEXT("onto the line after it (the copy's key bumped until the author gives it its own)"), Keys(Text), FString(TEXT("Alpha,Alpha_1,Beta,Gamma")));
	}

	// And a row taken off the tree is taken out of the file by the write-back, as any node is.
	const FLiveTree Live(Source);
	UDreamWidget* Beta = Live.Find(RowId(TEXT("Beta")));
	if (TestNotNull(TEXT("the row is a widget of the tree"), Beta))
	{
		Beta->DestroyWidget();
		TestEqual(TEXT("and gone from the file"), Keys(Produce(Source, Live)), FString(TEXT("Alpha,Gamma")));
	}
	return true;
}

#endif
