// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIManager.h"
#include "Core/Text/DreamTextPaint.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/StringOutputDevice.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "DreamTextTestFont.h"
#include "Driver/DreamDriverRig.h"

/*
 * DreamGUI.Memory, the memory report: what each font's glyph atlas, the sprite atlases and each world's canvas meshes and
 * paint rows hold, printed or as JSON. Run here the way the packaged smoke probe runs it -- the console command with
 * "Json" and an output device of the caller's -- and read back as JSON, so what is held to account is what a tool reading
 * the report gets.
 */
namespace DreamMemoryReportTestLocal
{
	/** DreamGUI.Memory Json, run as the console runs it, parsed; null when it is missing, does not run or does not parse. */
	TSharedPtr<FJsonObject> RunMemoryReport(FAutomationTestBase& InTest, UWorld* InWorld)
	{
		IConsoleObject* ConsoleObject = IConsoleManager::Get().FindConsoleObject(TEXT("DreamGUI.Memory"));
		IConsoleCommand* Command = ConsoleObject != nullptr ? ConsoleObject->AsCommand() : nullptr;
		if (!InTest.TestNotNull(TEXT("The console command DreamGUI.Memory exists"), Command))
		{
			return nullptr;
		}
		FStringOutputDevice Output;
		const TArray<FString> Args = { TEXT("Json") };
		InTest.TestTrue(TEXT("DreamGUI.Memory Json runs"), Command->Execute(Args, InWorld, Output));
		TSharedPtr<FJsonObject> Report;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Output);
		if (!InTest.TestTrue(TEXT("...and prints JSON that parses"), FJsonSerializer::Deserialize(Reader, Report) && Report.IsValid()))
		{
			InTest.AddInfo(Output);
			return nullptr;
		}
		return Report;
	}

	/** The entry of InReport's array InArrayName whose InKey is InValue. */
	TSharedPtr<FJsonObject> FindReportEntry(const TSharedPtr<FJsonObject>& InReport, const TCHAR* InArrayName, const TCHAR* InKey, const FString& InValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!InReport.IsValid() || !InReport->TryGetArrayField(InArrayName, Entries) || Entries == nullptr)
		{
			return nullptr;
		}
		for (const TSharedPtr<FJsonValue>& Entry : *Entries)
		{
			const TSharedPtr<FJsonObject> Object = Entry.IsValid() ? Entry->AsObject() : nullptr;
			FString Value;
			if (Object.IsValid() && Object->TryGetStringField(InKey, Value) && Value == InValue)
			{
				return Object;
			}
		}
		return nullptr;
	}

	/** InObject's object field InField; null when there is none. */
	TSharedPtr<FJsonObject> ReportObject(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField)
	{
		const TSharedPtr<FJsonObject>* Field = nullptr;
		return InObject.IsValid() && InObject->TryGetObjectField(InField, Field) && Field != nullptr ? *Field : nullptr;
	}

	/** A number of the report, as the integer it was written as; -1 when it is not there. */
	int64 ReportNumber(const TSharedPtr<FJsonObject>& InObject, const TCHAR* InField)
	{
		double Value = -1.0;
		if (InObject.IsValid())
		{
			InObject->TryGetNumberField(InField, Value);
		}
		return (int64)Value;
	}
}

/*
 * A font made from a file the engine ships and drawn with once: the report lists it before; after, with the slice the
 * glyphs went into, its bytes on the GPU and in the CPU copy, the field glyphs and cells it holds -- more glyphs than
 * before -- and the GPU bytes being the slices times the slice's area times the bytes a texel takes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMemoryReportListsAFontTest,
	"DreamGUI.Memory.TheReportListsAFontWithTheAtlasItFilledOnceTextHasBeenDrawnWithIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMemoryReportListsAFontTest::RunTest(const FString& Parameters)
{
	using namespace DreamMemoryReportTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(800, 600));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// The glyphs are made on the spot, whatever the frame's budget says, so one frame is enough to have them.
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(Rig.GetWorld());
	Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")), false);
	Font->InitFont();
	const FString FontPath = Font->GetPathName();

	const TSharedPtr<FJsonObject> Before = RunMemoryReport(*this, Rig.GetWorld());
	const TSharedPtr<FJsonObject> FontBefore = FindReportEntry(Before, TEXT("fonts"), TEXT("path"), FontPath);
	if (!TestTrue(TEXT("A font that exists is listed before anything is drawn with it"), FontBefore.IsValid()))
	{
		return false;
	}
	// Whatever the font made for itself as it initialised, if anything; the text's glyphs come on top.
	const int64 GlyphsBefore = ReportNumber(FontBefore, TEXT("fieldGlyphs"));

	UDreamWidget* Label = Rig.MakeWidget(TEXT("Label"), nullptr, FVector2D(600.0, 80.0));
	UDreamText* Text = Label != nullptr ? Label->CreateNewVisual<UDreamText>() : nullptr;
	if (!TestNotNull(TEXT("A label with a text"), Text))
	{
		return false;
	}
	Text->SetFont(Font);
	Text->SetFontSize(32.0f);
	Text->SetText(FText::FromString(TEXT("Memory, counted")));
	Rig.PumpFrames(2);

	const TSharedPtr<FJsonObject> After = RunMemoryReport(*this, Rig.GetWorld());
	const TSharedPtr<FJsonObject> FontAfter = FindReportEntry(After, TEXT("fonts"), TEXT("path"), FontPath);
	if (!TestTrue(TEXT("Drawn with, the font is listed"), FontAfter.IsValid()))
	{
		return false;
	}
	FString ClassName;
	TestTrue(TEXT("...by its class"), FontAfter->TryGetStringField(TEXT("class"), ClassName) && ClassName == Font->GetClass()->GetName());
	const int64 Slices = ReportNumber(FontAfter, TEXT("atlasSlices"));
	const int64 SliceSize = ReportNumber(FontAfter, TEXT("atlasSliceSize"));
	const int64 BytesPerTexel = ReportNumber(FontAfter, TEXT("atlasBytesPerTexel"));
	TestTrue(TEXT("...with the atlas slice its glyphs went into"), Slices >= 1 && SliceSize > 0 && BytesPerTexel > 0);
	TestEqual(TEXT("...its GPU bytes: the slices, each the side squared, at the bytes a texel takes"),
		ReportNumber(FontAfter, TEXT("atlasGpuBytes")), Slices * SliceSize * SliceSize * BytesPerTexel);
	TestTrue(TEXT("...the copy it keeps on the CPU"), ReportNumber(FontAfter, TEXT("atlasCpuBytes")) > 0);
	TestTrue(TEXT("...the field glyphs the text asked for"), ReportNumber(FontAfter, TEXT("fieldGlyphs")) > GlyphsBefore);
	TestTrue(TEXT("...in cells the field packer holds, out of the cells there are"),
		ReportNumber(FontAfter, TEXT("fieldCells")) >= 1 && ReportNumber(FontAfter, TEXT("cellsTotal")) >= ReportNumber(FontAfter, TEXT("fieldCells")));
	TestTrue(TEXT("...and the face it read from the file"), ReportNumber(FontAfter, TEXT("faceBytes")) > 0);

	const TSharedPtr<FJsonObject> Totals = ReportObject(After, TEXT("totals"));
	TestTrue(TEXT("The totals count the font's atlas"), ReportNumber(Totals, TEXT("fontGpuBytes")) >= ReportNumber(FontAfter, TEXT("atlasGpuBytes")));
	return true;
}

/*
 * A world's line in the report says what its manager and its canvas meshes say: the canvases registered, the meshes and
 * their sections with the bytes their vertex and index arrays took, and the paint rows -- which a text painted with a
 * gradient takes a text table and a gradient row of.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamMemoryReportCountsAWorldTest,
	"DreamGUI.Memory.TheReportCountsAWorldsCanvasesMeshesAndPaintRowsAsItsManagerHasThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamMemoryReportCountsAWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamMemoryReportTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(800, 600));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UWorld* World = Rig.GetWorld();
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(World);
	if (!TestNotNull(TEXT("The rig's world has a UI manager"), Manager))
	{
		return false;
	}

	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(World);
	UDreamWidget* Label = Rig.MakeWidget(TEXT("Painted"), nullptr, FVector2D(400.0, 60.0));
	UDreamText* Text = Label != nullptr ? Label->CreateNewVisual<UDreamText>() : nullptr;
	if (!TestNotNull(TEXT("A label with a text"), Text))
	{
		return false;
	}
	Text->SetFont(Font);
	Text->SetFontSize(28.0f);
	Text->SetText(FText::FromString(TEXT("Gold")));
	FDreamTextPaint Paint;
	Paint.bEnabled = true;
	Paint.Gradient.Stops = { FDreamGradientStop(0.0f, FColor(255, 243, 176)), FDreamGradientStop(1.0f, FColor(156, 106, 18)) };
	Text->SetFacePaint(Paint);
	// The pump runs the manager's update (TickDreamUI): layout, paint, batching, and the canvas's mesh. The mesh's sections
	// are made and filled when the canvases' draw calls are submitted, which a game does as each frame ends (the world's
	// end-of-frame updates) and a world that no engine frame ticks never does. Submitted here after each frame, as its end
	// would.
	for (int32 Frame = 0; Frame < 2; ++Frame)
	{
		Rig.PumpFrames(1);
		Manager->SubmitCanvasDrawCall();
	}

	const TSharedPtr<FJsonObject> Report = RunMemoryReport(*this, World);
	const TSharedPtr<FJsonObject> WorldEntry = FindReportEntry(Report, TEXT("worlds"), TEXT("path"), World->GetPathName());
	if (!TestTrue(TEXT("The rig's world is listed"), WorldEntry.IsValid()))
	{
		return false;
	}
	FString Type;
	TestTrue(TEXT("...with its type"), WorldEntry->TryGetStringField(TEXT("type"), Type) && Type == LexToString(World->WorldType.GetValue()));

	int32 Canvases = 0;
	for (const TWeakObjectPtr<UDreamCanvas>& Canvas : Manager->GetAllCanvasArray())
	{
		Canvases += Canvas.IsValid() ? 1 : 0;
	}
	TestEqual(TEXT("...the canvases its manager has registered"), ReportNumber(WorldEntry, TEXT("canvases")), (int64)Canvases);
	TestEqual(TEXT("...the widgets"), ReportNumber(WorldEntry, TEXT("widgets")), (int64)Manager->GetRegisteredWidgets().Num());
	TestTrue(TEXT("...a canvas mesh at least, with a section in use"),
		ReportNumber(WorldEntry, TEXT("meshes")) >= 1 && ReportNumber(WorldEntry, TEXT("sections")) >= 1);
	TestTrue(TEXT("...whose vertices and indices took bytes"),
		ReportNumber(WorldEntry, TEXT("vertexBytes")) > 0 && ReportNumber(WorldEntry, TEXT("indexBytes")) > 0);

	int32 TextureRows = 0;
	int32 TextRows = 0;
	int32 GradientRows = 0;
	int64 TextureBytes = 0;
	Manager->GetPaintRowsMemoryInfo(TextureRows, TextRows, GradientRows, TextureBytes);
	const TSharedPtr<FJsonObject> PaintRows = ReportObject(WorldEntry, TEXT("paintRows"));
	TestEqual(TEXT("...its paint rows as the manager counts them: the texture's rows"), ReportNumber(PaintRows, TEXT("textureRows")), (int64)TextureRows);
	TestEqual(TEXT("...the text tables"), ReportNumber(PaintRows, TEXT("textRows")), (int64)TextRows);
	TestEqual(TEXT("...the gradient rows"), ReportNumber(PaintRows, TEXT("gradientRows")), (int64)GradientRows);
	TestEqual(TEXT("...and the texture's bytes"), ReportNumber(PaintRows, TEXT("textureBytes")), TextureBytes);

	// What the painted text holds, on the manager's word: a table of its own and the row of its gradient.
	TestTrue(TEXT("The painted text holds a text table"), Text->GetPaintTextRow() != INDEX_NONE);
	TestTrue(TEXT("...counted among the paint rows with its gradient's"), TextRows >= 1 && GradientRows >= 1);
	return true;
}

#endif
