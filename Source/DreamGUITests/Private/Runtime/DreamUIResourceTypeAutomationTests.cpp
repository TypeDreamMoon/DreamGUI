// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamButton.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * `@Row Row1 { }`: a node whose type an Asset entry of a resources block names. A family of components is named once,
 * in the library file that styles it, and every screen that `use`s the library writes the short name instead of the
 * asset path on every line.
 */
namespace DreamUIResourceTypeTestLocal
{
	TFunction<bool(const FString&, FString&, FString&)> MakeMapReader(TMap<FString, FString> InFiles)
	{
		return [Files = MoveTemp(InFiles)](const FString& InSpelling, FString& OutResolvedPath, FString& OutText)
		{
			const FString* Found = Files.Find(InSpelling);
			if (Found == nullptr)
			{
				return false;
			}
			OutResolvedPath = TEXT("/virtual/") + InSpelling;
			OutText = *Found;
			return true;
		};
	}

	struct FBuilt
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		UDreamWidget* Find(const TCHAR* InDisplayName) const
		{
			UDreamWidget* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([InDisplayName, &Found](UDreamWidget* InWidget)
				{
					if (Found == nullptr && IsValid(InWidget) && InWidget->GetDisplayName() == InDisplayName)
					{
						Found = InWidget;
					}
				});
			}
			return Found;
		}

		bool Reported(EDreamUIDiagnosticCode InCode) const
		{
			return Diagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
			{
				return InDiagnostic.Code == InCode;
			});
		}

		void Dump(FAutomationTestBase& InTest) const
		{
			for (const FDreamUIDiagnostic& Diagnostic : Diagnostics.Diagnostics)
			{
				InTest.AddInfo(Diagnostic.ToString());
			}
		}
	};

	/** Parse and, when the parse holds, build. */
	void Build(FBuilt& OutBuilt, const TArray<FString>& InLines, TMap<FString, FString> InImports = {})
	{
		OutBuilt.Diagnostics.SourceName = TEXT("ResourceTypes.dui");
		if (FDreamUISourceFile::Parse(FString::Join(InLines, TEXT("\n")), OutBuilt.Diagnostics.SourceName, OutBuilt.Ast,
			OutBuilt.Diagnostics, MakeMapReader(MoveTemp(InImports))))
		{
			TArray<FDreamWidgetPropertyBinding> Bindings;
			OutBuilt.Tree.Reset(FDreamUITextBuilder::Build(OutBuilt.Ast, GetTransientPackage(), OutBuilt.Diagnostics, Bindings));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIResourceTypeTest,
	"DreamGUI.Text.Import.AnAssetResourceNamesANodeTypeHereAndThroughUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIResourceTypeTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIResourceTypeTestLocal;
	{
		FBuilt Built;
		Build(Built, {
			TEXT("resources {"),
			TEXT("    Asset Btn = /Script/DreamGUIControls.DreamButton"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    + VerticalBox {}"),
			TEXT("    @Btn Ok {"),
			TEXT("        @slot SizeRule = Fill"),
			TEXT("    }"),
			TEXT("    Widget Peer {"),
			TEXT("    }"),
			TEXT("}"),
		});
		if (Built.Diagnostics.NumErrors() > 0)
		{
			Built.Dump(*this);
		}
		TestEqual(TEXT("A node typed by a local Asset resource builds without errors"), Built.Diagnostics.NumErrors(), 0);
		UDreamWidget* Ok = Built.Find(TEXT("Ok"));
		if (TestNotNull(TEXT("the node is in the tree"), Ok))
		{
			TestEqual(TEXT("as the class the resource names"), Ok->GetClass(), UDreamButton::StaticClass());
		}
		if (TestTrue(TEXT("the root holds two children"), Built.Ast.bHasRoot && Built.Ast.Root.Children.Num() == 2))
		{
			const FDreamUINode& Node = Built.Ast.Root.Children[0];
			TestEqual(TEXT("the type is kept as written, '@' included"), Node.TypeName, FString(TEXT("@Btn")));
			TestEqual(TEXT("and the node begins at the '@', where its line does, as its peer begins at its type"),
				Node.Location.Column, Built.Ast.Root.Children[1].Location.Column);
			TestEqual(TEXT("an '@slot' line inside it is still a slot property"), Node.SlotProperties.Num(), 1);
		}
	}
	{
		// Brought in by `use`, and written as a quoted path, which an Asset entry also takes.
		FBuilt Built;
		Build(Built, {
			TEXT("use \"Library.dui\""),
			TEXT("Widget Root {"),
			TEXT("    @Btn Ok {"),
			TEXT("    }"),
			TEXT("}"),
		}, {{TEXT("Library.dui"), TEXT("resources {\n    Asset Btn = \"/Script/DreamGUIControls.DreamButton\"\n}")}});
		if (Built.Diagnostics.NumErrors() > 0)
		{
			Built.Dump(*this);
		}
		TestEqual(TEXT("A node typed by an imported Asset resource builds without errors"), Built.Diagnostics.NumErrors(), 0);
		UDreamWidget* Ok = Built.Find(TEXT("Ok"));
		TestTrue(TEXT("and is the class the library names"), Ok != nullptr && Ok->GetClass() == UDreamButton::StaticClass());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIResourceTypeErrorsTest,
	"DreamGUI.Text.Import.AResourceTypeThatIsMissingOrNotAWidgetClassSaysWhich",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIResourceTypeErrorsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIResourceTypeTestLocal;
	{
		FBuilt Built;
		Build(Built, { TEXT("Widget Root {"), TEXT("    @Nope Ok {"), TEXT("    }"), TEXT("}") });
		TestTrue(TEXT("a name no resources block declares is an unknown resource"), Built.Reported(EDreamUIDiagnosticCode::UnknownResource));
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("resources {"), TEXT("    Color Accent = #FF6600"), TEXT("}"),
			TEXT("Widget Root {"), TEXT("    @Accent Ok {"), TEXT("    }"), TEXT("}"),
		});
		TestTrue(TEXT("a resource that is not an Asset is a type mismatch"), Built.Reported(EDreamUIDiagnosticCode::ResourceTypeMismatch));
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("resources {"), TEXT("    Asset Plain = /Script/DreamGUI.DreamWidget"), TEXT("}"),
			TEXT("Widget Root {"), TEXT("    @Plain Ok {"), TEXT("    }"), TEXT("}"),
		});
		TestTrue(TEXT("an Asset naming a class that is no user widget is refused as such"), Built.Reported(EDreamUIDiagnosticCode::NotAUserWidgetClass));
	}
	{
		FBuilt Built;
		Build(Built, {
			TEXT("resources {"), TEXT("    Asset Btn = /Script/DreamGUIControls.DreamButton"), TEXT("}"),
			TEXT("Widget Root {"), TEXT("    @Btn {"), TEXT("    }"), TEXT("}"),
		});
		TestTrue(TEXT("a resource-typed node without an id is a missing id"), Built.Reported(EDreamUIDiagnosticCode::MissingNodeId));
	}
	return true;
}

#endif
