// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"

/*
 * `use "…"`: another file's styles and resources joining this one's lookup. The reader owns
 * resolution, which is what makes these tests possible at all -- a map of spellings stands in for
 * the DUI roots, and the parser cannot tell.
 */

namespace DreamUIImportTestLocal
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

	void DumpDiagnostics(FAutomationTestBase& InTest, const FDreamUIDiagnosticBag& InDiagnostics)
	{
		for (const FDreamUIDiagnostic& Diagnostic : InDiagnostics.Diagnostics)
		{
			InTest.AddInfo(Diagnostic.ToString());
		}
	}

	bool Reported(const FDreamUIDiagnosticBag& InDiagnostics, EDreamUIDiagnosticCode InCode)
	{
		return InDiagnostics.Diagnostics.ContainsByPredicate([InCode](const FDreamUIDiagnostic& InDiagnostic)
		{
			return InDiagnostic.Code == InCode;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIImportStylesTest,
	"DreamGUI.Text.Import.AnImportedStyleResolvesAndALocalOneShadowsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIImportStylesTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIImportTestLocal;
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = FDreamUISourceFile::Parse(FString::Join(TArray<FString>{
			TEXT("class /Game/UI/WBP_ImportFixture"),
			TEXT("use \"Common.dui\""),
			TEXT("Widget Root {"),
			TEXT("    Image Bg : Card {"),
			TEXT("    }"),
			TEXT("}")}, TEXT("\n")), TEXT("Main.dui"), Ast, Diagnostics,
			MakeMapReader({{TEXT("Common.dui"), TEXT("style Card { RenderOpacity = 0.5 }\nresources {\n    Color Accent = #FF6600\n}")}}));
		TestTrue(TEXT("The importing file parses"), bParsed);
		if (!bParsed)
		{
			DumpDiagnostics(*this, Diagnostics);
		}
		TestEqual(TEXT("...with no diagnostics"), Diagnostics.Diagnostics.Num(), 0);
		TestEqual(TEXT("One imported style"), Ast.ImportedStyles.Num(), 1);
		TestTrue(TEXT("FindStyle reaches it"), Ast.FindStyle(TEXT("Card")) != nullptr);
		TestTrue(TEXT("FindResource reaches the imported resource"), Ast.FindResource(TEXT("Accent")) != nullptr);
		TestEqual(TEXT("The watcher's edge list carries the resolved path"), Ast.Imports.Num(), 1);
	}
	{
		// A local declaration wins over the import, first-wins as ever.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUISourceFile::Parse(FString::Join(TArray<FString>{
			TEXT("class /Game/UI/WBP_ShadowFixture"),
			TEXT("use \"Common.dui\""),
			TEXT("style Card {"),
			TEXT("    RenderOpacity = 1"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("}")}, TEXT("\n")), TEXT("Main.dui"), Ast, Diagnostics,
			MakeMapReader({{TEXT("Common.dui"), TEXT("style Card {\n    RenderOpacity = 0.5\n}")}}));
		const FDreamUIStyle* Card = Ast.FindStyle(TEXT("Card"));
		TestTrue(TEXT("The local style is the one found"), Card != nullptr
			&& Card->Properties.Num() == 1 && Card->Properties[0].Value.Raw == TEXT("1"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIImportGuardsTest,
	"DreamGUI.Text.Import.CyclesAndMissingReadersReportDUI2012WhileDiamondsPass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIImportGuardsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUIImportTestLocal;
	{
		// A imports B imports A: the chain guard trips on the way back up.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUISourceFile::Parse(TEXT("use \"B.dui\"\nWidget Root {\n}"), TEXT("/virtual/A.dui"), Ast, Diagnostics,
			MakeMapReader({{TEXT("B.dui"), TEXT("use \"A.dui\"")}, {TEXT("A.dui"), TEXT("use \"B.dui\"")}}));
		TestTrue(TEXT("A cycle reports 2012"), Reported(Diagnostics, EDreamUIDiagnosticCode::ImportFailed));
	}
	{
		// A imports B and C; both import D. A diamond is not a cycle: the base parses twice and the
		// duplicate merged entries are inert under first-wins lookup.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		const bool bParsed = FDreamUISourceFile::Parse(TEXT("use \"B.dui\"\nuse \"C.dui\"\nWidget Root {\n}"),
			TEXT("/virtual/A.dui"), Ast, Diagnostics,
			MakeMapReader({
				{TEXT("B.dui"), TEXT("use \"D.dui\"")},
				{TEXT("C.dui"), TEXT("use \"D.dui\"")},
				{TEXT("D.dui"), TEXT("style Base { RenderOpacity = 0.9 }")}}));
		TestTrue(TEXT("A diamond parses"), bParsed);
		TestEqual(TEXT("...clean"), Diagnostics.Diagnostics.Num(), 0);
		TestTrue(TEXT("...and the base style is reachable"), Ast.FindStyle(TEXT("Base")) != nullptr);
	}
	{
		// The readerless overload: a `use` line cannot be honoured and says so.
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUISourceFile::Parse(TEXT("use \"Common.dui\"\nWidget Root {\n}"), TEXT("Main.dui"), Ast, Diagnostics);
		TestTrue(TEXT("No reader reports 2012"), Reported(Diagnostics, EDreamUIDiagnosticCode::ImportFailed));
	}
	return true;
}

#endif
