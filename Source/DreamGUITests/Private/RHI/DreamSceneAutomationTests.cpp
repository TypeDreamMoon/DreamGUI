// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"

#include "DreamGalleryStage.h"
#include "DreamTextParityCorpus.h"

/*
 * Pictures written as .dui text: every file in Source/DreamGUITests/Resources/Scenes is a test of its own,
 * DreamGUI.RHI.Scene.<file name>, held to the golden image Resources/Golden/Scene_<file name>.png.
 *
 * A scene is read the way the compiler reads a widget Blueprint's source -- parsed, then built into a widget tree --
 * and the tree is copied onto the gallery's render-target stage, its root anchored to fill the stage. The picture is
 * read once the scene has settled: drawn, every glyph its texts asked for landed in its font's atlas, then the same
 * picture a few frames running. A scene with no golden image yet fails, after its picture has been written to
 * Saved/DreamGUITests/Captures; -DreamGUIWriteGoldens on the editor's command line writes the goldens instead.
 *
 * What a scene file may say beyond the language (Resources/Scenes/README.md has the whole of it):
 *   // @stage 1024x768    a comment line giving the stage's size in pixels; 512x512 without one.
 *   Text Font_Arabic_X    a node whose id starts Font_<Key>_ draws with the corpus font of that key
 *                         (Resources/TextParity/corpus.json, "fonts"): a font made from the engine's own font files,
 *                         so the picture is the same on every machine. Every other text keeps the default font.
 */
namespace DreamSceneTestLocal
{
	using namespace DreamGalleryStage;

	static const FIntPoint DefaultStageSize(512, 512);
	static constexpr int32 LargestStageSide = 4096;
	static const TCHAR* const FontPrefix = TEXT("Font_");

	FString GetScenesDirectory()
	{
		return FPaths::Combine(DreamTextParity::GetResourcesDirectory(), TEXT("Scenes"));
	}

	/** The size a `// @stage <W>x<H>` line asks for, or the default when the file has none. */
	FIntPoint ReadStageSize(const FString& InSource)
	{
		TArray<FString> Lines;
		InSource.ParseIntoArrayLines(Lines);
		for (const FString& Line : Lines)
		{
			FString Directive = Line.TrimStartAndEnd();
			if (!Directive.StartsWith(TEXT("//")))
			{
				continue;
			}
			Directive = Directive.Mid(2).TrimStartAndEnd();
			if (!Directive.StartsWith(TEXT("@stage")))
			{
				continue;
			}
			FString Width;
			FString Height;
			if (Directive.Mid(6).TrimStartAndEnd().Split(TEXT("x"), &Width, &Height, ESearchCase::IgnoreCase))
			{
				const int32 W = FCString::Atoi(*Width.TrimStartAndEnd());
				const int32 H = FCString::Atoi(*Height.TrimStartAndEnd());
				if (W > 0 && H > 0 && W <= LargestStageSide && H <= LargestStageSide)
				{
					return FIntPoint(W, H);
				}
			}
		}
		return DefaultStageSize;
	}

	/** "Font_Arabic_Greeting" names the key "Arabic"; an id that does not start with Font_ names none. */
	FString FontKeyOf(const FString& InNodeId)
	{
		if (!InNodeId.StartsWith(FontPrefix, ESearchCase::CaseSensitive))
		{
			return FString();
		}
		const FString Rest = InNodeId.Mid(FCString::Strlen(FontPrefix));
		FString Key;
		FString Tail;
		return Rest.Split(TEXT("_"), &Key, &Tail) ? Key : Rest;
	}

	/**
	 * Every text whose node names a font key is given that font, on the built tree before it is copied, so that the
	 * copy registers with it from the start. Written through the property, as the builder writes `Font = /Path`: the
	 * tree is a template, and nothing on it is registered yet.
	 */
	bool ApplySceneFonts(FAutomationTestBase& InTest, const FString& InSceneName, UDreamWidgetTree& InTree, TArray<UDreamUIFontData_FreeTypeRender*>& OutFonts)
	{
		TArray<TPair<UDreamText*, FString>> Wanted;
		bool bAllHaveText = true;
		InTree.ForEachWidget([&Wanted, &bAllHaveText, &InTest, &InSceneName](UDreamWidget* Widget)
		{
			const FString Key = IsValid(Widget) ? FontKeyOf(Widget->GetDisplayName()) : FString();
			if (Key.IsEmpty())
			{
				return;
			}
			if (UDreamText* Text = Cast<UDreamText>(Widget->GetVisual()))
			{
				Wanted.Emplace(Text, Key);
			}
			else
			{
				InTest.AddError(FString::Printf(TEXT("%s: '%s' names the font key %s but is not a Text node."), *InSceneName, *Widget->GetDisplayName(), *Key));
				bAllHaveText = false;
			}
		});
		if (Wanted.Num() == 0)
		{
			return bAllHaveText;
		}

		DreamTextParity::FCorpus Corpus;
		FString Error;
		if (!DreamTextParity::LoadCorpus(Corpus, Error))
		{
			InTest.AddError(FString::Printf(TEXT("%s names corpus fonts, and %s."), *InSceneName, *Error));
			return false;
		}
		FObjectPropertyBase* FontProperty = FindFProperty<FObjectPropertyBase>(UDreamText::StaticClass(), UDreamText::GetPropertyName_Font());
		if (FontProperty == nullptr)
		{
			InTest.AddError(TEXT("UDreamText has no Font property to give a scene font to."));
			return false;
		}
		bool bAllFound = bAllHaveText;
		for (const TPair<UDreamText*, FString>& Entry : Wanted)
		{
			DreamTextParity::FFontKey FontKey;
			if (!Corpus.FindFontKey(Entry.Value, FontKey))
			{
				InTest.AddError(FString::Printf(TEXT("%s: the font key %s is not in the fonts table of %s."), *InSceneName, *Entry.Value, *DreamTextParity::GetCorpusPath()));
				bAllFound = false;
				continue;
			}
			FString FontError;
			UDreamUIFontData_FreeTypeRender* Font = DreamTextParity::GetDreamFont(FontKey, FontError);
			if (!FontError.IsEmpty())
			{
				InTest.AddWarning(FString::Printf(TEXT("%s: %s."), *InSceneName, *FontError));
			}
			if (Font == nullptr)
			{
				bAllFound = false;
				continue;
			}
			FontProperty->SetObjectPropertyValue_InContainer(Entry.Key, Font);
			OutFonts.AddUnique(Font);
		}
		return bAllFound;
	}
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FDreamSceneTest,
	"DreamGUI.RHI.Scene",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

void FDreamSceneTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	using namespace DreamSceneTestLocal;
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(GetScenesDirectory(), TEXT("*.dui")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		const FString Name = FPaths::GetBaseFilename(File);
		OutBeautifiedNames.Add(Name);
		OutTestCommands.Add(Name);
	}
}

/*
 * One scene file: it parses and builds without an error, every font it names exists, its tree comes to life on the
 * stage, and once settled its picture matches Golden/Scene_<name>.png within the gallery's tolerance (8 per channel on
 * at most 0.2 % of the pixels). A scene with no golden image yet fails, its picture saved for a person to look at.
 */
bool FDreamSceneTest::RunTest(const FString& Parameters)
{
	using namespace DreamSceneTestLocal;
	const FString SceneName = Parameters;
	const FString Path = FPaths::Combine(GetScenesDirectory(), SceneName + TEXT(".dui"));
	FString Source;
	if (!TestTrue(FString::Printf(TEXT("The scene file %s can be read"), *Path), FFileHelper::LoadFileToString(Source, *Path)))
	{
		return false;
	}

	FDreamUIDiagnosticBag Diagnostics;
	Diagnostics.SourceName = Path;
	FDreamUIAst Ast;
	const bool bParsed = FDreamUISourceFile::Parse(Source, Path, Ast, Diagnostics);
	TArray<FDreamWidgetPropertyBinding> Bindings;
	TStrongObjectPtr<UDreamWidgetTree> Tree(bParsed ? FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings) : nullptr);
	const bool bBuilt = Tree.IsValid() && IsValid(Tree->RootWidget) && !Diagnostics.HasErrors();
	if (!TestTrue(FString::Printf(TEXT("%s parses and builds without an error%s%s"), *SceneName,
		Diagnostics.Diagnostics.Num() > 0 ? TEXT(": ") : TEXT(""), *Diagnostics.ToString()), bBuilt))
	{
		return false;
	}
	if (Diagnostics.Diagnostics.Num() > 0)
	{
		AddInfo(FString::Printf(TEXT("%s built with: %s"), *SceneName, *Diagnostics.ToString()));
	}

	TArray<UDreamUIFontData_FreeTypeRender*> SceneFonts;
	if (!ApplySceneFonts(*this, SceneName, *Tree, SceneFonts))
	{
		return false;
	}

	const FIntPoint StageSize = ReadStageSize(Source);
	FStageRef Stage = BeginStage(*this, StageSize);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->KeepAlive(Tree.Get());
	for (UDreamUIFontData_FreeTypeRender* Font : SceneFonts)
	{
		Stage->KeepAlive(Font);
	}
	UDreamWidget* Scene = DuplicateDreamWidgetHierarchy(Stage->GetWorld(), Tree->RootWidget.Get(), Stage->GetRoot());
	if (!TestNotNull(FString::Printf(TEXT("%s comes to life on the stage"), *SceneName), Scene))
	{
		Stage->TearDown();
		return false;
	}
	// Full-bleed on the stage, the way adding a page to the viewport places it: the scene's own root size is ignored.
	Scene->SetHorizontalAndVerticalAnchorMinMax(FVector2D::ZeroVector, FVector2D(1.0, 1.0), false, false);
	Scene->SetAnchoredPosition(FVector2D::ZeroVector);
	Scene->SetSizeDelta(FVector2D::ZeroVector);

	AddInfo(FString::Printf(TEXT("%s on a %d x %d stage, %d corpus font(s)"), *SceneName, StageSize.X, StageSize.Y, SceneFonts.Num()));
	const int32 MinDrawn = FMath::Max(1, StageSize.X * StageSize.Y / 100);
	EnqueueSettledPictureCheck(Stage, TEXT("Scene_") + SceneName, MinDrawn, EMissingGolden::Fail);
	EnqueueTearDown(Stage);
	return true;
}

#endif
