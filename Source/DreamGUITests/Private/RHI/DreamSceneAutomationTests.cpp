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
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithEase.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_Selector.h"
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
 * Saved/DreamGUITests/Captures, unless its golden is pending (Golden/pending.json); -DreamGUIWriteGoldens on the
 * editor's command line writes the goldens instead.
 *
 * What a scene file may say beyond the language (Resources/Scenes/README.md has the whole of it), as comment lines:
 *   // @stage 1024x768                  the stage's size in units, which are pixels at canvas scale 1; 512x512 without one.
 *   // @canvasScale 1 1.25 1.333         the scene once per canvas scale: the first under its own name, each further one as
 *                                        <name>_Scale<percent> (Scale125), on a target of the stage size times the scale.
 *   // @variant SmallTextCoverageOff     once more as <name>_SmallTextCoverageOff, with the project's small-text coverage
 *                                        switch off: the distance field draws every size, as it did before the switch.
 *   // @fill <NodeId> 0.5                that text's fill progress (lyric-style), which is runtime state a .dui cannot hold.
 *   // @textAnimationAlpha <NodeId> <alpha> <offset>
 *                                        a TextAnimation on that text: a range selector at the offset and an alpha
 *                                        property, which a .dui cannot build (its selector and properties are instanced).
 *   Text Font_Arabic_X                   a node whose id starts Font_<Key>_ draws with the corpus font of that key
 *                                        (Resources/TextParity/corpus.json, "fonts"): a font made from the engine's own font
 *                                        files, so the picture is the same on every machine. Every other text keeps the
 *                                        default font.
 */
namespace DreamSceneTestLocal
{
	using namespace DreamGalleryStage;

	static const FIntPoint DefaultStageSize(512, 512);
	static constexpr int32 LargestStageSide = 4096;
	static const TCHAR* const FontPrefix = TEXT("Font_");
	/** The one variant a scene can ask for besides its canvas scales. */
	static const TCHAR* const SmallTextCoverageOffVariant = TEXT("SmallTextCoverageOff");
	/** The range a TextAnimation directive's selector ramps over: a short fade from the affected characters to the rest. */
	static constexpr float AlphaAnimationRange = 0.1f;

	FString GetScenesDirectory()
	{
		return FPaths::Combine(DreamTextParity::GetResourcesDirectory(), TEXT("Scenes"));
	}

	/** A TextAnimation directive: which node, the alpha its affected characters fade to, where its selector stands. */
	struct FAlphaAnimationDirective
	{
		FString Node;
		float Alpha = 1.0f;
		float Offset = 0.5f;
	};

	/** Everything a scene file's `// @` comment lines ask for. */
	struct FSceneDirectives
	{
		FIntPoint StageSize = DefaultStageSize;
		TArray<float> CanvasScales;
		TArray<FString> Variants;
		TArray<TPair<FString, float>> Fills;
		TArray<FAlphaAnimationDirective> AlphaAnimations;
		/** Directive lines that do not read, in words: a scene that says something nobody understands fails. */
		TArray<FString> Errors;
	};

	/** The `// @...` lines of a scene file. */
	FSceneDirectives ReadDirectives(const FString& InSource)
	{
		FSceneDirectives Directives;
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
			if (!Directive.StartsWith(TEXT("@")))
			{
				continue;
			}
			TArray<FString> Words;
			Directive.ParseIntoArrayWS(Words);
			const FString Keyword = Words[0];
			if (Keyword == TEXT("@stage"))
			{
				FString Width;
				FString Height;
				bool bRead = false;
				if (Words.Num() >= 2 && Words[1].Split(TEXT("x"), &Width, &Height, ESearchCase::IgnoreCase))
				{
					const int32 W = FCString::Atoi(*Width.TrimStartAndEnd());
					const int32 H = FCString::Atoi(*Height.TrimStartAndEnd());
					if (W > 0 && H > 0 && W <= LargestStageSide && H <= LargestStageSide)
					{
						Directives.StageSize = FIntPoint(W, H);
						bRead = true;
					}
				}
				if (!bRead)
				{
					Directives.Errors.Add(FString::Printf(TEXT("'%s' does not give a stage of <width>x<height>, up to %d on a side"), *Directive, LargestStageSide));
				}
			}
			else if (Keyword == TEXT("@canvasScale"))
			{
				for (int32 Index = 1; Index < Words.Num(); ++Index)
				{
					const float Scale = FCString::Atof(*Words[Index]);
					if (Scale >= 0.25f && Scale <= 4.0f)
					{
						Directives.CanvasScales.AddUnique(Scale);
					}
					else
					{
						Directives.Errors.Add(FString::Printf(TEXT("'%s' is not a canvas scale between 0.25 and 4"), *Words[Index]));
					}
				}
			}
			else if (Keyword == TEXT("@variant"))
			{
				for (int32 Index = 1; Index < Words.Num(); ++Index)
				{
					if (Words[Index] == SmallTextCoverageOffVariant)
					{
						Directives.Variants.AddUnique(Words[Index]);
					}
					else
					{
						Directives.Errors.Add(FString::Printf(TEXT("'%s' is not a variant (%s is the one there is)"), *Words[Index], SmallTextCoverageOffVariant));
					}
				}
			}
			else if (Keyword == TEXT("@fill"))
			{
				if (Words.Num() == 3)
				{
					Directives.Fills.Emplace(Words[1], FMath::Clamp(FCString::Atof(*Words[2]), 0.0f, 1.0f));
				}
				else
				{
					Directives.Errors.Add(FString::Printf(TEXT("'%s' is not @fill <NodeId> <progress>"), *Directive));
				}
			}
			else if (Keyword == TEXT("@textAnimationAlpha"))
			{
				if (Words.Num() == 4)
				{
					FAlphaAnimationDirective& Animation = Directives.AlphaAnimations.AddDefaulted_GetRef();
					Animation.Node = Words[1];
					Animation.Alpha = FMath::Clamp(FCString::Atof(*Words[2]), 0.0f, 1.0f);
					Animation.Offset = FMath::Clamp(FCString::Atof(*Words[3]), 0.0f, 1.0f);
				}
				else
				{
					Directives.Errors.Add(FString::Printf(TEXT("'%s' is not @textAnimationAlpha <NodeId> <alpha> <offset>"), *Directive));
				}
			}
			else
			{
				Directives.Errors.Add(FString::Printf(TEXT("'%s' is not a directive a scene can give"), *Keyword));
			}
		}
		if (Directives.CanvasScales.Num() == 0)
		{
			Directives.CanvasScales.Add(1.0f);
		}
		return Directives;
	}

	/** "Scale125" for 1.25: the part of a run's name that says its canvas scale. */
	FString ScaleLabel(float InScale)
	{
		return FString::Printf(TEXT("Scale%d"), FMath::RoundToInt32(InScale * 100.0f));
	}

	/** One picture a scene file makes: at one canvas scale, with the small-text switch as the project has it or off. */
	struct FSceneRun
	{
		FString SceneName;
		/** The test's own name, and the golden's after Scene_. */
		FString RunName;
		float CanvasScale = 1.0f;
		bool bSmallTextCoverageOff = false;

		/** The test command: the file, the scale and the switch, which RunTest reads back. */
		FString ToCommand() const
		{
			return FString::Printf(TEXT("%s|%.4f|%d"), *SceneName, CanvasScale, bSmallTextCoverageOff ? 1 : 0);
		}
	};

	/** Every run a scene file asks for, its own name first. */
	void GetSceneRuns(const FString& InSceneName, const FSceneDirectives& InDirectives, TArray<FSceneRun>& OutRuns)
	{
		OutRuns.Reset();
		for (int32 Index = 0; Index < InDirectives.CanvasScales.Num(); ++Index)
		{
			FSceneRun& Run = OutRuns.AddDefaulted_GetRef();
			Run.SceneName = InSceneName;
			Run.CanvasScale = InDirectives.CanvasScales[Index];
			Run.RunName = Index == 0 ? InSceneName : InSceneName + TEXT("_") + ScaleLabel(Run.CanvasScale);
		}
		if (InDirectives.Variants.Contains(SmallTextCoverageOffVariant))
		{
			FSceneRun& Run = OutRuns.AddDefaulted_GetRef();
			Run.SceneName = InSceneName;
			Run.CanvasScale = InDirectives.CanvasScales[0];
			Run.bSmallTextCoverageOff = true;
			Run.RunName = InSceneName + TEXT("_") + SmallTextCoverageOffVariant;
		}
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
				InTest.AddError(FString::Printf(TEXT("%s: the font key %s is not in the fonts table of %s, or does not read."), *InSceneName, *Entry.Value, *DreamTextParity::GetCorpusPath()));
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

	/** The text of the node with this id in the live scene, or null. */
	UDreamText* FindSceneText(UDreamWidget* InScene, const FString& InNodeId)
	{
		TArray<UDreamWidget*> Widgets;
		UDreamWidget::CollectChildrenWidgets(InScene, Widgets, true);
		for (UDreamWidget* Widget : Widgets)
		{
			if (IsValid(Widget) && Widget->GetDisplayName() == InNodeId)
			{
				return Cast<UDreamText>(Widget->GetVisual());
			}
		}
		return nullptr;
	}

	/**
	 * What a .dui cannot hold, put on the live scene once it has been copied onto the stage: fill progress, which is
	 * runtime state, and TextAnimations, whose selector and properties are instanced objects the language does not build.
	 */
	bool ApplyLiveDirectives(FAutomationTestBase& InTest, const FString& InSceneName, const FSceneDirectives& InDirectives, UDreamWidget* InScene)
	{
		bool bAllFound = true;
		for (const TPair<FString, float>& Fill : InDirectives.Fills)
		{
			if (UDreamText* Text = FindSceneText(InScene, Fill.Key))
			{
				Text->SetFillProgress(Fill.Value);
			}
			else
			{
				InTest.AddError(FString::Printf(TEXT("%s: @fill names %s, which is not a Text node of the scene."), *InSceneName, *Fill.Key));
				bAllFound = false;
			}
		}
		for (const FAlphaAnimationDirective& Directive : InDirectives.AlphaAnimations)
		{
			UDreamText* Text = FindSceneText(InScene, Directive.Node);
			UDreamMeshModifierTextAnimation* Animation = Text != nullptr
				? Cast<UDreamMeshModifierTextAnimation>(Text->AddMeshModifier(UDreamMeshModifierTextAnimation::StaticClass())) : nullptr;
			if (Animation == nullptr)
			{
				InTest.AddError(FString::Printf(TEXT("%s: @textAnimationAlpha names %s, which is not a Text node of the scene that takes a TextAnimation."), *InSceneName, *Directive.Node));
				bAllFound = false;
				continue;
			}
			// The characters up to about the offset fade to the alpha, over a short ramp, and the rest stay as they are: a
			// typewriter's reveal stopped half way. It changes vertex colours only, which keeps small text on coverage.
			UDreamMeshModifierTextAnimation_RangeSelector* Selector = NewObject<UDreamMeshModifierTextAnimation_RangeSelector>(Animation);
			Selector->SetRange(AlphaAnimationRange);
			Selector->SetOffset(Directive.Offset);
			Animation->SetSelector(Selector);
			UDreamMeshModifierTextAnimation_AlphaProperty* AlphaProperty = NewObject<UDreamMeshModifierTextAnimation_AlphaProperty>(Animation);
			AlphaProperty->SetAlpha(Directive.Alpha);
			Animation->SetProperties(TArray<UDreamMeshModifierTextAnimation_Property*>{ AlphaProperty });
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
		FString Source;
		FFileHelper::LoadFileToString(Source, *FPaths::Combine(GetScenesDirectory(), File));
		TArray<FSceneRun> Runs;
		GetSceneRuns(Name, ReadDirectives(Source), Runs);
		for (const FSceneRun& Run : Runs)
		{
			OutBeautifiedNames.Add(Run.RunName);
			OutTestCommands.Add(Run.ToCommand());
		}
	}
}

/*
 * One picture of a scene file: it parses and builds without an error, every font and directive it names exists, its
 * tree comes to life on the stage at its canvas scale, and once settled its picture matches Golden/Scene_<run>.png within
 * the gallery's tolerance (8 per channel on at most 0.2 % of the pixels). A scene with no golden image yet fails, its
 * picture saved for a person to look at; a pending golden (Golden/pending.json) only warns, missing or different.
 */
bool FDreamSceneTest::RunTest(const FString& Parameters)
{
	using namespace DreamSceneTestLocal;
	TArray<FString> Parts;
	Parameters.ParseIntoArray(Parts, TEXT("|"), false);
	const FString SceneName = Parts.Num() > 0 ? Parts[0] : Parameters;
	const FString Path = FPaths::Combine(GetScenesDirectory(), SceneName + TEXT(".dui"));
	FString Source;
	if (!TestTrue(FString::Printf(TEXT("The scene file %s can be read"), *Path), FFileHelper::LoadFileToString(Source, *Path)))
	{
		return false;
	}
	const FSceneDirectives Directives = ReadDirectives(Source);
	if (!TestEqual(FString::Printf(TEXT("%s's directives all read%s%s"), *SceneName, Directives.Errors.Num() > 0 ? TEXT(": ") : TEXT(""),
		*FString::Join(Directives.Errors, TEXT("; "))), Directives.Errors.Num(), 0))
	{
		return false;
	}
	// The run this command names: its canvas scale and the switch, as GetTests wrote them.
	FSceneRun Run;
	Run.SceneName = SceneName;
	Run.RunName = SceneName;
	Run.CanvasScale = Parts.Num() > 1 ? FCString::Atof(*Parts[1]) : Directives.CanvasScales[0];
	Run.bSmallTextCoverageOff = Parts.Num() > 2 && Parts[2] == TEXT("1");
	TArray<FSceneRun> Runs;
	GetSceneRuns(SceneName, Directives, Runs);
	for (const FSceneRun& Candidate : Runs)
	{
		if (Candidate.ToCommand() == Run.ToCommand())
		{
			Run.RunName = Candidate.RunName;
		}
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

	// The target is the stage times the canvas scale in pixels; the root is laid out at the stage's own size.
	const FIntPoint StageSize = Directives.StageSize;
	const FIntPoint TargetSize(FMath::Max(1, FMath::RoundToInt32(StageSize.X * Run.CanvasScale)), FMath::Max(1, FMath::RoundToInt32(StageSize.Y * Run.CanvasScale)));
	FStageRef Stage = BeginStage(*this, TargetSize);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	if (TargetSize != StageSize)
	{
		Stage->UseLayoutSize(FVector2D(StageSize));
	}
	if (Run.bSmallTextCoverageOff)
	{
		// Before anything draws: texts decide at paint time, and the first paint is the one the picture settles from.
		Stage->UseSmallTextCoverage(false);
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
	if (!ApplyLiveDirectives(*this, SceneName, Directives, Scene))
	{
		Stage->TearDown();
		return false;
	}

	AddInfo(FString::Printf(TEXT("%s on a %d x %d stage at canvas scale %.3f (a %d x %d picture), %d corpus font(s)%s"), *Run.RunName, StageSize.X, StageSize.Y,
		Run.CanvasScale, TargetSize.X, TargetSize.Y, SceneFonts.Num(), Run.bSmallTextCoverageOff ? TEXT(", small-text coverage off") : TEXT("")));
	const int32 MinDrawn = FMath::Max(1, TargetSize.X * TargetSize.Y / 100);
	EnqueueSettledPictureCheck(Stage, TEXT("Scene_") + Run.RunName, MinDrawn, EMissingGolden::Fail);
	EnqueueTearDown(Stage);
	return true;
}

#endif
