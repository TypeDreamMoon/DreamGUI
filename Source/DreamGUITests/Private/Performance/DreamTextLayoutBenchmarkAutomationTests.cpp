// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Core/Components/DreamText.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPainter.h"
#include "Core/Text/DreamTextShapeCache.h"

#include "DreamScopedWorld.h"
#include "DreamTextTestFont.h"

/*
 * What laying text out costs, stage by stage, so that two builds can be compared on the same machine: the layout engine
 * and the painter called directly, headlessly, on Roboto with Droid Sans Fallback behind it (shaped by HarfBuzz) and on
 * the mock font (measured per code point), over a 2,000-character English paragraph, a 2,000-character Japanese one and
 * twenty paragraphs of a hundred characters.
 *
 * Each text goes through the same scenarios: a layout from nothing (the shape cache emptied), the same text laid out again
 * in a new widget, 200 keystrokes at its end, 200 in its middle, 200 backspaces in its middle, the SetText flip a caret
 * move makes (the field's text and the text with one more character, in turn), a sweep of the box's width, and Best Fit's
 * search. Every scenario runs under the four settings of DreamGUI.Text.ShapeCache and DreamGUI.Text.IncrementalLayout,
 * one after another within a round and again in a second round, so a slow moment of the machine lands on all four alike.
 *
 * For every scenario and setting the median and 95th percentile of a layout, of its paint and of each stage
 * (FDreamTextLayoutStats: the DreamUI_TextLayout_* scopes and the shaper's) are written to
 * Saved/DreamGUITests/Perf/TextLayout.json with what the layouts did per layout -- hb_shape calls, code points shaped,
 * ICU code units, lines placed and kept, glyph quads asked for -- and a CPU trace of the run beside it, TextLayout.utrace,
 * a region per scenario. Tools/Tests/perf_report.py text reads the report.
 *
 * No time fails the test. The counters do: they are exact -- the same in both rounds -- and hold what the caches promise:
 * nothing kept is used with incremental layout off, nothing comes from the shape cache with it off, and a keystroke at
 * the end of a paragraph measures that paragraph alone and places at most two lines.
 */
namespace DreamTextLayoutBenchmarkTestLocal
{
	using DreamTests::FScopedGameWorld;

	constexpr int32 Rounds = 2;
	constexpr int32 ColdLayouts = 10;
	constexpr int32 WarmLayouts = 20;
	constexpr int32 Keystrokes = 200;
	constexpr int32 WidthSteps = 40;
	constexpr int32 BestFitSearches = 6;

	enum class EScenario : uint8
	{
		Cold,
		Warm,
		TypeAtEnd,
		TypeInMiddle,
		BackspaceInMiddle,
		Flip,
		WidthSweep,
		BestFit,
		Count
	};

	const TCHAR* ScenarioName(EScenario Scenario)
	{
		switch (Scenario)
		{
		case EScenario::Cold: return TEXT("Cold");
		case EScenario::Warm: return TEXT("Warm");
		case EScenario::TypeAtEnd: return TEXT("TypeAtEnd");
		case EScenario::TypeInMiddle: return TEXT("TypeInMiddle");
		case EScenario::BackspaceInMiddle: return TEXT("BackspaceInMiddle");
		case EScenario::Flip: return TEXT("Flip");
		case EScenario::WidthSweep: return TEXT("WidthSweep");
		case EScenario::BestFit: return TEXT("BestFit");
		default: return TEXT("");
		}
	}

	/** One setting of the two console switches. */
	struct FConfig
	{
		bool bShapeCache = true;
		bool bIncremental = true;

		FString Name() const
		{
			return FString::Printf(TEXT("ShapeCache%d_Incremental%d"), bShapeCache ? 1 : 0, bIncremental ? 1 : 0);
		}
	};
	const FConfig Configs[] = { { true, true }, { true, false }, { false, true }, { false, false } };

	/** What a layout did, in the order the report and the exactness check read them. */
	enum ECounter : int32
	{
		HbShapeCalls,
		ShapedCodepoints,
		ShapeLookups,
		ShapeHits,
		IcuCodeUnits,
		LinesPlaced,
		LinesReused,
		ParagraphsMeasured,
		ParagraphsReused,
		QuadFetches,
		IncrementalLayouts,
		CounterCount
	};

	const TCHAR* CounterName(int32 Counter)
	{
		static const TCHAR* const Names[CounterCount] = { TEXT("hbShapeCalls"), TEXT("shapedCodepoints"), TEXT("shapeLookups"),
			TEXT("shapeHits"), TEXT("icuCodeUnits"), TEXT("linesPlaced"), TEXT("linesReused"), TEXT("paragraphsMeasured"),
			TEXT("paragraphsReused"), TEXT("quadFetches"), TEXT("incrementalLayouts") };
		return Names[Counter];
	}

	/** One timed layout and its paint. */
	struct FSample
	{
		double LayoutMs = 0.0;
		double PaintMs = 0.0;
		double StageMs[(int32)EDreamTextLayoutStage::Count] = {};
		int64 Counters[CounterCount] = {};
		/** Paragraphs the text had: a backspace across a newline joins two. */
		int32 Paragraphs = 1;
	};

	/** Every sample of one scenario on one text and font under one setting, and its counters summed round by round. */
	struct FCase
	{
		FString Font;
		FString Text;
		EScenario Scenario = EScenario::Cold;
		int32 Config = 0;
		TArray<FSample> Samples;
		TArray<TArray<int64>> RoundCounters;
		/** The scenario's last display list, through the kept layout, was a fresh layout's. */
		bool bLastMatchedFresh = true;
	};

	/** A font and a text to lay out. */
	struct FSubject
	{
		FString FontName;
		UDreamUIFontData_BaseObject* Font = nullptr;
		FString TextName;
		FString Content;
		/** Where an edit in the middle goes: a word boundary near the text's middle. */
		int32 Middle = 0;
		/** Paragraphs in the text. */
		int32 Paragraphs = 1;
	};

	FString EngineFont(const TCHAR* Name)
	{
		return FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name);
	}

	UDreamUIFontData_DistanceField* MakeFileFont(UWorld* World, const TCHAR* Name)
	{
		// The benchmark times layout, not rasterization: glyphs are made on the spot, and the warm-up made them all.
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(World);
		Font->SetFontFilePath(EngineFont(Name), false);
		Font->InitFont();
		return Font;
	}

	FString MakeEnglish(int32 Seed, int32 Length)
	{
		const TCHAR* const Words[] = { TEXT("the"), TEXT("of"), TEXT("layout"), TEXT("text"), TEXT("which"), TEXT("paragraph"),
			TEXT("measure"), TEXT("glyph"), TEXT("quick"), TEXT("brown"), TEXT("fox"), TEXT("jumps"), TEXT("over"), TEXT("lazy"),
			TEXT("dog"), TEXT("and"), TEXT("a"), TEXT("line"), TEXT("break"), TEXT("kept"), TEXT("edit"), TEXT("typed"), TEXT("again"),
			TEXT("never"), TEXT("before"), TEXT("Avoid"), TEXT("Toyota"), TEXT("office") };
		FRandomStream Random(Seed);
		FString Text;
		while (Text.Len() < Length)
		{
			Text += Words[Random.RandRange(0, (int32)UE_ARRAY_COUNT(Words) - 1)];
			Text += Random.RandRange(0, 11) == 0 ? TEXT(". ") : TEXT(" ");
		}
		return Text.Left(Length);
	}

	FString MakeJapanese(int32 Seed, int32 Length)
	{
		const TCHAR* const Words[] = { TEXT("\u65E5\u672C\u8A9E"), TEXT("\u6587\u7AE0"), TEXT("\u306E"), TEXT("\u3092"), TEXT("\u306F"),
			TEXT("\u6F22\u5B57"), TEXT("\u4EEE\u540D"), TEXT("\u6539\u884C"), TEXT("\u7DE8\u96C6"), TEXT("\u5165\u529B\u3059\u308B"),
			TEXT("\u30C6\u30AD\u30B9\u30C8"), TEXT("\u30EC\u30A4\u30A2\u30A6\u30C8"), TEXT("\u6BB5\u843D"), TEXT("\u6587\u5B57"),
			TEXT("\u3001"), TEXT("\u3002"), TEXT("\u3067\u3059"), TEXT("\u307E\u3059"), TEXT("\u304B\u3089"), TEXT("\u307E\u3067") };
		FRandomStream Random(Seed);
		FString Text;
		while (Text.Len() < Length)
		{
			Text += Words[Random.RandRange(0, (int32)UE_ARRAY_COUNT(Words) - 1)];
		}
		return Text.Left(Length);
	}

	FString MakeParagraphs(int32 Seed, int32 Count, int32 Length)
	{
		FString Text;
		for (int32 p = 0; p < Count; p++)
		{
			Text += MakeEnglish(Seed + p, Length);
			if (p + 1 < Count)
			{
				Text += TEXT("\n");
			}
		}
		return Text;
	}

	/** The middle of Content, moved forward to the next space: where a word would be typed into a text. */
	int32 MiddleOf(const FString& Content)
	{
		int32 Middle = Content.Len() / 2;
		const int32 Space = Content.Find(TEXT(" "), ESearchCase::CaseSensitive, ESearchDir::FromStart, Middle);
		return Space != INDEX_NONE ? Space : Middle;
	}

	FDreamTextLayoutInput MakeInput(const FSubject& Subject)
	{
		FDreamTextLayoutInput In;
		In.Content = Subject.Content;
		In.Width = 600.0f;
		In.Height = 4000.0f;
		In.Pivot = FVector2f(0.5f, 0.5f);
		In.FontSize = 24.0f;
		In.ParagraphHAlign = EDreamUITextParagraphHorizontalAlign::Left;
		In.ParagraphVAlign = EDreamUITextParagraphVerticalAlign::Top;
		In.OverflowType = EDreamUITextOverflowType::VerticalOverflow;
		In.bUseKerning = true;
		In.Language = Subject.TextName.StartsWith(TEXT("Japanese")) ? TEXT("ja") : TEXT("en");
		In.Font = Subject.Font;
		return In;
	}

	void SetConsoleInt(const TCHAR* Name, int32 Value)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name))
		{
			Variable->Set(Value, ECVF_SetByCode);
		}
	}

	int32 GetConsoleInt(const TCHAR* Name, int32 Default)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable != nullptr ? Variable->GetInt() : Default;
	}

	/** Times one scenario's layouts, through a kept layout when the setting keeps one. */
	class FScenarioRun
	{
	public:
		FScenarioRun(const FSubject& InSubject, const FConfig& InConfig, FCase& InCase)
			: Subject(InSubject), Config(InConfig), Case(InCase)
		{
			if (Config.bIncremental)
			{
				State = MakeUnique<FDreamTextLayoutState>();
			}
			Paint.ItalicSlope = 0.26f;
		}

		/** One layout through the scenario's state, timed with its paint; true when it is kept as a sample. */
		void Layout(const FDreamTextLayoutInput& In, bool bTimed = true)
		{
			FDreamTextLayoutEngine::ResetStats();
			FDreamTextShapeCache::ResetStats();
			const uint64 Start = FPlatformTime::Cycles64();
			FDreamTextLayoutEngine::Layout(In, DisplayList, State.Get());
			const uint64 Laid = FPlatformTime::Cycles64();
			FDreamTextPainter::Paint(DisplayList, Paint, Geometry, CharProperties);
			const uint64 Painted = FPlatformTime::Cycles64();
			if (!bTimed)
			{
				return;
			}
			const FDreamTextLayoutStats LayoutStats = FDreamTextLayoutEngine::GetStats();
			const FDreamTextShapeCache::FStats ShapeStats = FDreamTextShapeCache::GetStats();
			FSample& Sample = Case.Samples.AddDefaulted_GetRef();
			Sample.LayoutMs = FPlatformTime::ToMilliseconds64(Laid - Start);
			Sample.PaintMs = FPlatformTime::ToMilliseconds64(Painted - Laid);
			for (int32 Stage = 0; Stage < (int32)EDreamTextLayoutStage::Count; Stage++)
			{
				Sample.StageMs[Stage] = LayoutStats.GetMilliseconds((EDreamTextLayoutStage)Stage);
			}
			Sample.Counters[HbShapeCalls] = ShapeStats.ShapeCalls;
			Sample.Counters[ShapedCodepoints] = ShapeStats.ShapedCodepoints;
			Sample.Counters[ShapeLookups] = ShapeStats.Lookups;
			Sample.Counters[ShapeHits] = ShapeStats.Hits;
			Sample.Counters[IcuCodeUnits] = LayoutStats.IcuCodeUnits;
			Sample.Counters[LinesPlaced] = LayoutStats.LinesPlaced;
			Sample.Counters[LinesReused] = LayoutStats.LinesReused;
			Sample.Counters[ParagraphsMeasured] = LayoutStats.ParagraphsMeasured;
			Sample.Counters[ParagraphsReused] = LayoutStats.ParagraphsReused;
			Sample.Counters[QuadFetches] = LayoutStats.QuadFetches;
			Sample.Counters[IncrementalLayouts] = LayoutStats.IncrementalLayouts;
			for (const TCHAR Character : In.Content)
			{
				Sample.Paragraphs += Character == TEXT('\n') ? 1 : 0;
			}
			for (int32 Counter = 0; Counter < CounterCount; Counter++)
			{
				RoundCounters[Counter] += Sample.Counters[Counter];
			}
		}

		/** A new widget: what was kept goes. */
		void NewWidget()
		{
			if (State.IsValid())
			{
				State->Reset();
			}
		}

		void Run(EScenario Scenario)
		{
			RoundCounters.Init(0, CounterCount);
			// Every run starts from an empty shape cache warmed by one layout of the text, untimed, so the two rounds count
			// the same work and a run does not lean on what the run before it left.
			FDreamTextShapeCache::Flush();
			FDreamTextLayoutInput In = MakeInput(Subject);
			if (Scenario != EScenario::Cold)
			{
				Layout(In, false);
			}
			switch (Scenario)
			{
			case EScenario::Cold:
				for (int32 Index = 0; Index < ColdLayouts; Index++)
				{
					FDreamTextShapeCache::Flush();
					NewWidget();
					Layout(In);
				}
				break;
			case EScenario::Warm:
				for (int32 Index = 0; Index < WarmLayouts; Index++)
				{
					NewWidget();
					Layout(In);
				}
				break;
			case EScenario::TypeAtEnd:
			case EScenario::TypeInMiddle:
			{
				const TCHAR* const Typed = Subject.TextName.StartsWith(TEXT("Japanese"))
					? TEXT("\u65E5\u672C\u8A9E\u306E\u6587\u7AE0\u3092\u5165\u529B\u3059\u308B\u3002")
					: TEXT("typing on and on, a word at a time. ");
				const int32 TypedLength = FCString::Strlen(Typed);
				int32 Caret = Scenario == EScenario::TypeAtEnd ? In.Content.Len() : Subject.Middle;
				for (int32 Key = 0; Key < Keystrokes; Key++)
				{
					In.Content.InsertAt(Caret++, Typed[Key % TypedLength]);
					Layout(In);
				}
				break;
			}
			case EScenario::BackspaceInMiddle:
			{
				int32 Caret = Subject.Middle;
				for (int32 Key = 0; Key < Keystrokes && Caret > 0; Key++)
				{
					// The texts are all in the Basic Multilingual Plane: one code unit is one character.
					In.Content.RemoveAt(--Caret, 1);
					Layout(In);
				}
				break;
			}
			case EScenario::Flip:
			{
				const FString Field = In.Content;
				FString Shown = Field;
				Shown.InsertAt(Subject.Middle, TEXT('|'));
				for (int32 Index = 0; Index < Keystrokes; Index++)
				{
					In.Content = (Index & 1) ? Field : Shown;
					Layout(In);
				}
				break;
			}
			case EScenario::WidthSweep:
				for (int32 Step = 0; Step < WidthSteps; Step++)
				{
					In.Width = 300.0f + 400.0f * (float)Step / (float)(WidthSteps - 1);
					Layout(In);
				}
				break;
			case EScenario::BestFit:
				for (int32 Search = 0; Search < BestFitSearches; Search++)
				{
					NewWidget();
					// Wrapped, so only the height can fail to fit; a box of about half the paragraph's height at its size.
					const FVector2f Box(In.Width, 300.0f + 40.0f * Search);
					UDreamText::FindBestFitFontSize(Box, 8.0f, 48.0f, [this, &In](float Size)
					{
						FDreamTextLayoutInput Probe = In;
						Probe.FontSize = Size;
						Layout(Probe);
						return FVector2f(0.0f, DisplayList.PreferredSize.Y);
					});
				}
				break;
			default:
				break;
			}
			Case.RoundCounters.Add(RoundCounters);
			// Whatever the kept layout was taken from, the last layout is the one a layout from nothing makes.
			if (State.IsValid() && Case.Samples.Num() > 0)
			{
				FDreamTextDisplayList Fresh;
				FDreamTextLayoutEngine::Layout(LastInput(In, Scenario), Fresh);
				Case.bLastMatchedFresh &= SameLayout(DisplayList, Fresh);
			}
		}

	private:
		/** The input the scenario's last layout had: Best Fit's last probe had its own size. */
		FDreamTextLayoutInput LastInput(const FDreamTextLayoutInput& In, EScenario Scenario) const
		{
			FDreamTextLayoutInput Last = In;
			if (Scenario == EScenario::BestFit && DisplayList.Items.Num() > 0)
			{
				Last.FontSize = DisplayList.Items[0].Style.Size;
			}
			return Last;
		}

		static bool SameLayout(const FDreamTextDisplayList& A, const FDreamTextDisplayList& B)
		{
			if (A.Items.Num() != B.Items.Num() || A.Lines.Num() != B.Lines.Num() || A.PreferredSize != B.PreferredSize
				|| A.VisibleCharCount != B.VisibleCharCount || A.VisualRuns.Num() != B.VisualRuns.Num())
			{
				return false;
			}
			for (int32 i = 0; i < A.Items.Num(); i++)
			{
				const FDreamTextGlyphItem& X = A.Items[i];
				const FDreamTextGlyphItem& Y = B.Items[i];
				if (X.Pen != Y.Pen || X.ElementIndex != Y.ElementIndex || X.SourceIndex != Y.SourceIndex || X.LineIndex != Y.LineIndex
					|| X.Glyph.MinUV != Y.Glyph.MinUV || X.Glyph.Width != Y.Glyph.Width || X.bEmit != Y.bEmit)
				{
					return false;
				}
			}
			for (int32 Line = 0; Line < A.Lines.Num(); Line++)
			{
				const TArray<FDreamUITextCaretProperty>& X = A.Lines[Line].CaretPropertyList;
				const TArray<FDreamUITextCaretProperty>& Y = B.Lines[Line].CaretPropertyList;
				if (X.Num() != Y.Num())
				{
					return false;
				}
				for (int32 k = 0; k < X.Num(); k++)
				{
					if (X[k].CaretPosition != Y[k].CaretPosition || X[k].CharIndex != Y[k].CharIndex)
					{
						return false;
					}
				}
			}
			return true;
		}

		const FSubject& Subject;
		const FConfig& Config;
		FCase& Case;
		TUniquePtr<FDreamTextLayoutState> State;
		FDreamTextDisplayList DisplayList;
		FDreamTextPaintParams Paint;
		FDreamUIGeometry Geometry;
		TArray<FDreamUITextCharProperty> CharProperties;
		TArray<int64> RoundCounters;
	};

	/** The value at Fraction of the sorted values, by nearest rank. */
	double Percentile(TArray<double> Values, double Fraction)
	{
		if (Values.Num() == 0)
		{
			return 0.0;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::CeilToInt32(Fraction * Values.Num()) - 1, 0, Values.Num() - 1);
		return Values[Index];
	}

	TSharedRef<FJsonObject> Distribution(const TArray<double>& Values)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		double Sum = 0.0;
		for (const double Value : Values)
		{
			Sum += Value;
		}
		Object->SetNumberField(TEXT("median"), Percentile(Values, 0.5));
		Object->SetNumberField(TEXT("p95"), Percentile(Values, 0.95));
		Object->SetNumberField(TEXT("mean"), Values.Num() > 0 ? Sum / Values.Num() : 0.0);
		return Object;
	}

	TSharedRef<FJsonObject> CaseToJson(const FCase& Case)
	{
		TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("font"), Case.Font);
		Object->SetStringField(TEXT("text"), Case.Text);
		Object->SetStringField(TEXT("scenario"), ScenarioName(Case.Scenario));
		Object->SetStringField(TEXT("config"), Configs[Case.Config].Name());
		Object->SetBoolField(TEXT("shapeCache"), Configs[Case.Config].bShapeCache);
		Object->SetBoolField(TEXT("incremental"), Configs[Case.Config].bIncremental);
		Object->SetNumberField(TEXT("layouts"), Case.Samples.Num());
		TArray<double> LayoutMs, PaintMs;
		for (const FSample& Sample : Case.Samples)
		{
			LayoutMs.Add(Sample.LayoutMs);
			PaintMs.Add(Sample.PaintMs);
		}
		Object->SetObjectField(TEXT("layoutMs"), Distribution(LayoutMs));
		Object->SetObjectField(TEXT("paintMs"), Distribution(PaintMs));
		TSharedRef<FJsonObject> Stages = MakeShared<FJsonObject>();
		for (int32 Stage = 0; Stage < (int32)EDreamTextLayoutStage::Count; Stage++)
		{
			TArray<double> StageMs;
			for (const FSample& Sample : Case.Samples)
			{
				StageMs.Add(Sample.StageMs[Stage]);
			}
			Stages->SetObjectField(FDreamTextLayoutStats::GetStageName((EDreamTextLayoutStage)Stage), Distribution(StageMs));
		}
		Object->SetObjectField(TEXT("stagesMs"), Stages);
		TSharedRef<FJsonObject> PerLayout = MakeShared<FJsonObject>();
		for (int32 Counter = 0; Counter < CounterCount; Counter++)
		{
			int64 Sum = 0;
			for (const FSample& Sample : Case.Samples)
			{
				Sum += Sample.Counters[Counter];
			}
			PerLayout->SetNumberField(CounterName(Counter), Case.Samples.Num() > 0 ? (double)Sum / Case.Samples.Num() : 0.0);
		}
		Object->SetObjectField(TEXT("perLayout"), PerLayout);
		return Object;
	}

	FString PerfDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("Perf")));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLayoutBenchmarkTest,
	"DreamGUI.Performance.TextLayout.EveryScenarioRecordsWhatEachStageOfItsLayoutsCosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamTextLayoutBenchmarkTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutBenchmarkTestLocal;
	FScopedGameWorld TestWorld;
	const int32 ShapeCacheBefore = GetConsoleInt(TEXT("DreamGUI.Text.ShapeCache"), 1);
	const int32 IncrementalBefore = GetConsoleInt(TEXT("DreamGUI.Text.IncrementalLayout"), 1);

	UDreamUIFontData_DistanceField* Roboto = MakeFileFont(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	Roboto->SetFallbackFonts({ MakeFileFont(TestWorld.World, TEXT("DroidSansFallback.ttf")) });
	UDreamTextTestFont* Mock = NewObject<UDreamTextTestFont>(TestWorld.World);

	TArray<FSubject> Subjects;
	for (int32 FontIndex = 0; FontIndex < 2; FontIndex++)
	{
		const FString FontName = FontIndex == 0 ? TEXT("Roboto+DroidSansFallback") : TEXT("Mock");
		UDreamUIFontData_BaseObject* Font = FontIndex == 0 ? (UDreamUIFontData_BaseObject*)Roboto : (UDreamUIFontData_BaseObject*)Mock;
		const TPair<FString, FString> Texts[] =
		{
			{ TEXT("English2000"), MakeEnglish(11, 2000) },
			{ TEXT("Japanese2000"), MakeJapanese(12, 2000) },
			{ TEXT("Paragraphs20x100"), MakeParagraphs(13, 20, 100) },
		};
		for (const TPair<FString, FString>& Text : Texts)
		{
			FSubject& Subject = Subjects.AddDefaulted_GetRef();
			Subject.FontName = FontName;
			Subject.Font = Font;
			Subject.TextName = Text.Key;
			Subject.Content = Text.Value;
			Subject.Middle = Text.Key.StartsWith(TEXT("Japanese")) ? Text.Value.Len() / 2 : MiddleOf(Text.Value);
			Subject.Paragraphs = Text.Key.StartsWith(TEXT("Paragraphs")) ? 20 : 1;
		}
	}

	const FString TracePath = FPaths::Combine(PerfDirectory(), TEXT("TextLayout.utrace"));
	IFileManager::Get().MakeDirectory(*PerfDirectory(), /*Tree*/ true);
	IFileManager::Get().Delete(*TracePath, /*RequireExists*/ false, /*EvenReadOnly*/ true, /*Quiet*/ true);
	// A trace someone already started from the command line is left to them.
	const bool bStartedTrace = FTraceAuxiliary::Start(FTraceAuxiliary::EConnectionType::File, *TracePath, TEXT("cpu,region,bookmark"));

	TArray<FCase> Cases;
	for (const FSubject& Subject : Subjects)
	{
		for (int32 Scenario = 0; Scenario < (int32)EScenario::Count; Scenario++)
		{
			for (int32 Config = 0; Config < (int32)UE_ARRAY_COUNT(Configs); Config++)
			{
				FCase& Case = Cases.AddDefaulted_GetRef();
				Case.Font = Subject.FontName;
				Case.Text = Subject.TextName;
				Case.Scenario = (EScenario)Scenario;
				Case.Config = Config;
			}
		}
	}
	// Round after round, every scenario under each setting in turn.
	for (int32 Round = 0; Round < Rounds; Round++)
	{
		int32 CaseIndex = 0;
		for (const FSubject& Subject : Subjects)
		{
			for (int32 Scenario = 0; Scenario < (int32)EScenario::Count; Scenario++)
			{
				for (int32 Config = 0; Config < (int32)UE_ARRAY_COUNT(Configs); Config++)
				{
					FCase& Case = Cases[CaseIndex++];
					SetConsoleInt(TEXT("DreamGUI.Text.ShapeCache"), Configs[Config].bShapeCache ? 1 : 0);
					SetConsoleInt(TEXT("DreamGUI.Text.IncrementalLayout"), Configs[Config].bIncremental ? 1 : 0);
					const FString Region = FString::Printf(TEXT("DreamGUI.TextLayout.%s.%s.%s.%s"), *Subject.FontName, *Subject.TextName,
						ScenarioName((EScenario)Scenario), *Configs[Config].Name());
					TRACE_BEGIN_REGION(*Region);
					FScenarioRun(Subject, Configs[Config], Case).Run((EScenario)Scenario);
					TRACE_END_REGION(*Region);
				}
			}
		}
	}
	SetConsoleInt(TEXT("DreamGUI.Text.ShapeCache"), ShapeCacheBefore);
	SetConsoleInt(TEXT("DreamGUI.Text.IncrementalLayout"), IncrementalBefore);
	if (bStartedTrace)
	{
		FTraceAuxiliary::Stop();
	}

	// The report.
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("rounds"), Rounds);
	Root->SetNumberField(TEXT("keystrokes"), Keystrokes);
	TArray<TSharedPtr<FJsonValue>> CaseValues;
	for (const FCase& Case : Cases)
	{
		CaseValues.Add(MakeShared<FJsonValueObject>(CaseToJson(Case)));
	}
	Root->SetArrayField(TEXT("cases"), CaseValues);
	Root->SetStringField(TEXT("trace"), bStartedTrace ? TracePath : FString());
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	const FString ReportPath = FPaths::Combine(PerfDirectory(), TEXT("TextLayout.json"));
	TestTrue(FString::Printf(TEXT("The benchmark's numbers are written to %s"), *ReportPath), FFileHelper::SaveStringToFile(Json, *ReportPath));

	// The counters. Exact: both rounds did the same work, and the caches kept their promises.
	for (const FCase& Case : Cases)
	{
		const FConfig& Config = Configs[Case.Config];
		const FString What = FString::Printf(TEXT("%s, %s, %s, %s"), *Case.Font, *Case.Text, ScenarioName(Case.Scenario), *Config.Name());
		if (!TestEqual(What + TEXT(": every round ran"), Case.RoundCounters.Num(), Rounds))
		{
			continue;
		}
		for (int32 Round = 1; Round < Rounds; Round++)
		{
			for (int32 Counter = 0; Counter < CounterCount; Counter++)
			{
				TestEqual(What + FString::Printf(TEXT(": %s the same in round %d as in round 1"), CounterName(Counter), Round + 1),
					Case.RoundCounters[Round][Counter], Case.RoundCounters[0][Counter]);
			}
		}
		const TArray<int64>& Counted = Case.RoundCounters[0];
		if (!Config.bIncremental)
		{
			TestEqual(What + TEXT(": nothing built on a kept layout"), Counted[IncrementalLayouts], (int64)0);
			TestEqual(What + TEXT(": no paragraph taken from one"), Counted[ParagraphsReused], (int64)0);
			TestEqual(What + TEXT(": no line's placement taken from one"), Counted[LinesReused], (int64)0);
		}
		if (!Config.bShapeCache)
		{
			TestEqual(What + TEXT(": nothing came from the shape cache"), Counted[ShapeHits], (int64)0);
		}
		if (Case.Font == TEXT("Mock"))
		{
			TestEqual(What + TEXT(": the mock font shapes nothing"), Counted[HbShapeCalls], (int64)0);
		}
		TestTrue(What + TEXT(": the last layout through the kept one is a fresh layout"), Case.bLastMatchedFresh);
		if (Config.bIncremental && (Case.Scenario == EScenario::TypeAtEnd || Case.Scenario == EScenario::TypeInMiddle
			|| Case.Scenario == EScenario::BackspaceInMiddle))
		{
			for (int32 Index = 0; Index < Case.Samples.Num(); Index++)
			{
				const FSample& Sample = Case.Samples[Index];
				const FString Key = What + FString::Printf(TEXT(", layout %d"), Index);
				if (!TestEqual(Key + TEXT(" built on the kept layout"), Sample.Counters[IncrementalLayouts], (int64)1)
					|| !TestEqual(Key + TEXT(" measured the edited paragraph alone"), Sample.Counters[ParagraphsMeasured], (int64)1)
					|| !TestEqual(Key + TEXT(" took every other paragraph as it was"), Sample.Counters[ParagraphsReused], (int64)(Sample.Paragraphs - 1)))
				{
					break;
				}
				if (Case.Scenario == EScenario::TypeAtEnd)
				{
					if (!TestTrue(Key + FString::Printf(TEXT(" placed at most two lines (%lld)"), Sample.Counters[LinesPlaced]), Sample.Counters[LinesPlaced] <= 2))
					{
						break;
					}
					if (Config.bShapeCache && Case.Font != TEXT("Mock")
						&& !TestTrue(Key + FString::Printf(TEXT(" made at most two hb_shape calls (%lld)"), Sample.Counters[HbShapeCalls]), Sample.Counters[HbShapeCalls] <= 2))
					{
						break;
					}
				}
			}
		}
	}
	for (const FCase& Case : Cases)
	{
		if (Case.Config == 0 || Case.Config == 3)
		{
			TArray<double> LayoutMs;
			for (const FSample& Sample : Case.Samples)
			{
				LayoutMs.Add(Sample.LayoutMs);
			}
			AddInfo(FString::Printf(TEXT("%s %s %s %s: median %.3f ms, p95 %.3f ms"), *Case.Font, *Case.Text, ScenarioName(Case.Scenario),
				*Configs[Case.Config].Name(), Percentile(LayoutMs, 0.5), Percentile(LayoutMs, 0.95)));
		}
	}
	return true;
}

#endif
