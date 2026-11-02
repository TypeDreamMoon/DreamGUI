// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUILocalizationManifest.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "Text/DreamUITextWriteBack.h"

#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * What a string says about its translation besides its key: `@note("…")` for the translator, `@max(n)` for the longest
 * a translation may be, `@noloc` for a text that is never translated. The first test holds the builder and the manifest
 * to them, the second the parser to its complaints, the third the write-back to keeping them when the designer changes
 * the text in front of them.
 */

namespace DreamUITextAnnotationTestLocal
{
	FString MakeSource()
	{
		return FString::Join(TArray<FString>{
			TEXT("class /Game/UI/WBP_About"),
			TEXT(""),
			TEXT("Widget Root {"),
			TEXT("    Text Title {"),
			TEXT("        Text = \"关于\" @note(\"页面标题，放在顶栏\") @max(12)"),
			TEXT("    }"),
			TEXT("    Text Version {"),
			TEXT("        Text = \"v1.0.3\" @noloc"),
			TEXT("    }"),
			TEXT("    Text Ok {"),
			TEXT("        Text = \"确定\" @max(8) @key(\"About.Ok\") @note(\"关掉这一页\")"),
			TEXT("    }"),
			TEXT("}"),
		}, TEXT("\n"));
	}

	UDreamText* FindText(UDreamWidgetTree* InTree, const TCHAR* InName)
	{
		UDreamText* Found = nullptr;
		InTree->ForEachWidget([&Found, InName](UDreamWidget* InWidget)
		{
			if (InWidget->GetDisplayName() == InName)
			{
				Found = Cast<UDreamText>(InWidget->GetVisual());
			}
		});
		return Found;
	}

	/** The codes the parse of a one-property node reports, in order. */
	TArray<EDreamUIDiagnosticCode> CodesOf(const FString& InLine)
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		FDreamUISourceFile::Parse(FString::Printf(TEXT("Widget Root {\n    Text T {\n        %s\n    }\n}\n"), *InLine), TEXT("Annotations.dui"), Ast, Diagnostics);
		TArray<EDreamUIDiagnosticCode> Codes;
		for (const FDreamUIDiagnostic& Diagnostic : Diagnostics.Diagnostics)
		{
			Codes.Add(Diagnostic.Code);
		}
		return Codes;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextAnnotationBuildTest,
	"DreamGUI.Text.NoteMaxAndNolocReachTheTranslatorAndTheText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextAnnotationBuildTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextAnnotationTestLocal;
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the file parses"), FDreamUISourceFile::Parse(MakeSource(), TEXT("About.dui"), Ast, Diagnostics)))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}
	FDreamUILocalization Localization;
	TArray<FDreamWidgetPropertyBinding> Bindings;
	TStrongObjectPtr<UDreamWidgetTree> Tree(FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings, nullptr, nullptr, &Localization));
	if (!TestNotNull(TEXT("and builds"), Tree.Get()))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}

	if (!TestEqual(TEXT("two texts to translate: @noloc leaves the version out"), Localization.Texts.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("the note"), Localization.Texts[0].Note, FString(TEXT("页面标题，放在顶栏")));
	TestEqual(TEXT("the longest it may be"), Localization.Texts[0].Max, 12);
	TestEqual(TEXT("annotations in any order: the key"), Localization.Texts[1].Key, FString(TEXT("About.Ok")));
	TestEqual(TEXT("its max"), Localization.Texts[1].Max, 8);
	TestEqual(TEXT("its note"), Localization.Texts[1].Note, FString(TEXT("关掉这一页")));

	const FString Json = DreamUILocalizationManifest::ToJson(TEXT("DUI/UI/About.dui"), FString(), Localization);
	TestTrue(TEXT("the manifest has the note and the max"), Json.Contains(
		TEXT("{ \"key\": \"Title.Text\", \"src\": \"关于\", \"at\": \"Title\", \"note\": \"页面标题，放在顶栏\", \"max\": 12, \"kind\": \"Text\" }")));
	TestFalse(TEXT("and not the version"), Json.Contains(TEXT("v1.0.3")));

	const UDreamText* Version = FindText(Tree.Get(), TEXT("Version"));
	const UDreamText* Title = FindText(Tree.Get(), TEXT("Title"));
	TestTrue(TEXT("@noloc: the same text in every language"), Version != nullptr && Version->GetText().IsCultureInvariant() && Version->GetText().ToString() == TEXT("v1.0.3"));
	TestTrue(TEXT("a translated one stays localizable"), Title != nullptr && !Title->GetText().IsCultureInvariant() && Title->GetText().ShouldGatherForLocalization());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextAnnotationDiagnosticsTest,
	"DreamGUI.Text.AnnotationsWrittenWrongAreReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextAnnotationDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextAnnotationTestLocal;
	const EDreamUIDiagnosticCode Bad = EDreamUIDiagnosticCode::MalformedTextAnnotation;
	struct FCase { const TCHAR* Line; EDreamUIDiagnosticCode Code; };
	const FCase Cases[] = {
		{ TEXT("Text = \"x\" @max(\"12\")"), Bad },
		{ TEXT("Text = \"x\" @max(0)"), Bad },
		{ TEXT("Text = \"x\" @max(1.5)"), Bad },
		{ TEXT("Text = \"x\" @note(5)"), Bad },
		{ TEXT("Text = \"x\" @note"), Bad },
		{ TEXT("FontSize = 24 @note(\"a size\")"), Bad },
		{ TEXT("Text = \"x\" @noloc @key(\"K\")"), Bad },
		{ TEXT("Text = \"x\" @max(4) @noloc"), Bad },
		{ TEXT("Text = \"x\" @note(\"a\") @note(\"b\")"), Bad },
		{ TEXT("Text = \"x\" @noloc @noloc"), Bad },
		{ TEXT("Text = \"x\" @nolocal"), EDreamUIDiagnosticCode::UnexpectedToken },
	};
	for (const FCase& Case : Cases)
	{
		const TArray<EDreamUIDiagnosticCode> Codes = CodesOf(Case.Line);
		TestTrue(FString::Printf(TEXT("%s: reported once, as %d"), Case.Line, static_cast<int32>(Case.Code)), Codes.Num() == 1 && Codes[0] == Case.Code);
		if (Codes.Num() != 1)
		{
			AddInfo(FString::Printf(TEXT("%s: %d diagnostic(s)"), Case.Line, Codes.Num()));
		}
	}
	TestEqual(TEXT("all four together are fine"), CodesOf(TEXT("Text = \"x\" @key(\"K\") @note(\"n\") @max(3)")).Num(), 0);
	TestEqual(TEXT("@noloc alone is fine"), CodesOf(TEXT("Text = \"x\" @noloc")).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextAnnotationWriteBackTest,
	"DreamGUI.Text.WriteBackKeepsTheAnnotationsOfAChangedText",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextAnnotationWriteBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITextAnnotationTestLocal;
	const FString Source = MakeSource();
	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	TStrongObjectPtr<UDreamWidgetTree> Live(FDreamUITextWriteBack::BuildReferenceTree(Source, Ast, Diagnostics));
	UDreamText* Title = Live.IsValid() ? FindText(Live.Get(), TEXT("Title")) : nullptr;
	if (!TestNotNull(TEXT("the title"), Title))
	{
		AddInfo(Diagnostics.ToString());
		return false;
	}
	Title->SetText(FText::FromString(TEXT("关于本作")));
	FString Out;
	FDreamUIDiagnosticBag WriteDiagnostics;
	TestTrue(TEXT("written back"), FDreamUITextWriteBack::ProduceText(Source, Live.Get(), Out, WriteDiagnostics));
	AddInfo(WriteDiagnostics.ToString());
	TestTrue(TEXT("the new text, its annotations kept"), Out.Contains(TEXT("Text = \"关于本作\" @note(\"页面标题，放在顶栏\") @max(12)")));
	TestTrue(TEXT("the others untouched"), Out.Contains(TEXT("Text = \"v1.0.3\" @noloc")) && Out.Contains(TEXT("@max(8) @key(\"About.Ok\") @note(\"关掉这一页\")")));
	return true;
}

#endif
