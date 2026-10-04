// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUIManager.h"
#include "Core/Text/DreamTextPaint.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "DreamScopedWorld.h"

/*
 * A world's paint rows: the texture painted text reads its gradients from, a row per distinct gradient shared by every
 * text that paints with it, and a table of its own for each painted text. What is asserted is the bookkeeping -- which
 * row a gradient gets, when a row is free again, what goes up to the render thread and when -- since the suite runs
 * without a GPU; DreamGUI.Text.Paint.APackedRow... holds what is inside a row to the shader's reading.
 */
namespace DreamPaintRowsTestLocal
{
	using DreamTests::FScopedGameWorld;

	/** Black to white, turned InAngle: a different gradient for every angle. */
	FDreamGradient MakeTurnedRamp(float InAngle)
	{
		FDreamGradient Ramp;
		Ramp.Angle = InAngle;
		Ramp.Stops = { FDreamGradientStop(0.0f, FColor::Black), FDreamGradientStop(1.0f, FColor::White) };
		return Ramp;
	}

	/** Bytes sent to the render thread so far, by everything: read before and after, around what a test does. */
	int64 GetUploadedBytes()
	{
		return DreamUIRenderStats::TakeSnapshot(false).Counters[static_cast<int32>(DreamUIRenderStats::ECounter::UploadedBytes)];
	}

	struct FPaintRowsCount
	{
		int32 TextureRows = 0;
		int32 TextRows = 0;
		int32 GradientRows = 0;
		int64 TextureBytes = 0;
	};

	FPaintRowsCount CountPaintRows(const UDreamUIManagerWorldSubsystem* InManager)
	{
		FPaintRowsCount Count;
		InManager->GetPaintRowsMemoryInfo(Count.TextureRows, Count.TextRows, Count.GradientRows, Count.TextureBytes);
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaintRowsMadeEagerlyTest,
	"DreamGUI.Text.Paint.Rows.EveryWorldMakesItsOwnPaintRowsWhenItsManagerStarts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaintRowsMadeEagerlyTest::RunTest(const FString& Parameters)
{
	using namespace DreamPaintRowsTestLocal;
	FScopedGameWorld First;
	FScopedGameWorld Second;
	UDreamUIManagerWorldSubsystem* FirstManager = UDreamUIManagerWorldSubsystem::GetInstance(First.World);
	UDreamUIManagerWorldSubsystem* SecondManager = UDreamUIManagerWorldSubsystem::GetInstance(Second.World);
	if (!TestNotNull(TEXT("a manager"), FirstManager) || !TestNotNull(TEXT("and another"), SecondManager))
	{
		return false;
	}
	// Before any canvas exists: one made earlier than the rows would bind the fallback texture until it rebuilt.
	UTexture* Texture = FirstManager->GetPaintRowsTexture();
	TestNotNull(TEXT("the rows are there before anything asked for them"), Texture);
	TestTrue(TEXT("each world has its own"), Texture != SecondManager->GetPaintRowsTexture() && SecondManager->GetPaintRowsTexture() != nullptr);
	FirstManager->CreatePaintRows();
	TestTrue(TEXT("making them again changes nothing"), FirstManager->GetPaintRowsTexture() == Texture);
	const FPaintRowsCount Count = CountPaintRows(FirstManager);
	TestTrue(TEXT("they have rows"), Count.TextureRows > 0);
	TestEqual(TEXT("of RowWidth float4 pixels"), Count.TextureBytes, static_cast<int64>(Count.TextureRows) * DreamPaintRows::RowBytes);
	TestEqual(TEXT("no text table is taken yet"), Count.TextRows, 0);
	TestEqual(TEXT("nor a gradient row"), Count.GradientRows, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaintRowsSharingTest,
	"DreamGUI.Text.Paint.Rows.IdenticalGradientsShareARowUntilItsLastHolderLetsGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaintRowsSharingTest::RunTest(const FString& Parameters)
{
	using namespace DreamPaintRowsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("a manager"), Manager))
	{
		return false;
	}
	const FDreamGradient Gold = MakeTurnedRamp(180.0f);
	// What draws the same is the same row, however it was put together: a stop past MaxStops is not drawn.
	FDreamGradient SameAsDrawn = Gold;
	for (int32 Extra = SameAsDrawn.Stops.Num(); Extra <= FDreamGradient::MaxStops; ++Extra)
	{
		SameAsDrawn.Stops.Add(FDreamGradientStop(1.0f, FColor::White));
	}
	FDreamGradient GoldWithSixteen = Gold;
	GoldWithSixteen.Stops = SameAsDrawn.Stops;
	GoldWithSixteen.Stops.SetNum(FDreamGradient::MaxStops);

	const int32 Row = Manager->AcquirePaintGradientRow(GoldWithSixteen);
	if (!TestTrue(TEXT("a gradient gets a row"), Row >= 0))
	{
		return false;
	}
	TestEqual(TEXT("a second acquire of the same gradient shares it"), Manager->AcquirePaintGradientRow(GoldWithSixteen), Row);
	TestEqual(TEXT("and so does one that only differs past what is drawn"), Manager->AcquirePaintGradientRow(SameAsDrawn), Row);
	const int32 Other = Manager->AcquirePaintGradientRow(MakeTurnedRamp(90.0f));
	TestTrue(TEXT("a different gradient has a row of its own"), Other >= 0 && Other != Row);
	TestEqual(TEXT("two distinct gradients, two rows"), CountPaintRows(Manager).GradientRows, 2);

	// Three holders: the row stays until the third lets go.
	Manager->ReleasePaintGradientRow(Row);
	Manager->ReleasePaintGradientRow(Row);
	TestEqual(TEXT("while one holder is left the row is kept"), CountPaintRows(Manager).GradientRows, 2);
	TestEqual(TEXT("and found again by its gradient"), Manager->AcquirePaintGradientRow(GoldWithSixteen), Row);
	Manager->ReleasePaintGradientRow(Row);
	Manager->ReleasePaintGradientRow(Row);
	TestEqual(TEXT("the last holder's release frees it"), CountPaintRows(Manager).GradientRows, 1);
	Manager->ReleasePaintGradientRow(Row);
	Manager->ReleasePaintGradientRow(INDEX_NONE);
	Manager->ReleasePaintGradientRow(123456);
	TestEqual(TEXT("releasing a freed row, no row or a row never taken is ignored"), CountPaintRows(Manager).GradientRows, 1);

	// A freed row is handed out again before the texture is asked for another.
	const int32 Reused = Manager->AcquirePaintGradientRow(MakeTurnedRamp(45.0f));
	TestEqual(TEXT("the freed row is the next one taken"), Reused, Row);
	Manager->ReleasePaintGradientRow(Reused);
	Manager->ReleasePaintGradientRow(Other);
	TestEqual(TEXT("and with every holder gone, no gradient row is taken"), CountPaintRows(Manager).GradientRows, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaintRowsUploadTest,
	"DreamGUI.Text.Paint.Rows.AGradientRowIsWrittenOnceAndTheFrameSendsWhatWasWritten",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaintRowsUploadTest::RunTest(const FString& Parameters)
{
	using namespace DreamPaintRowsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("a manager"), Manager))
	{
		return false;
	}
	Manager->FlushPaintRows();
	const int64 Start = GetUploadedBytes();
	const int32 Row = Manager->AcquirePaintGradientRow(MakeTurnedRamp(180.0f));
	TestEqual(TEXT("nothing goes up before the frame's flush"), GetUploadedBytes() - Start, static_cast<int64>(0));
	Manager->FlushPaintRows();
	TestEqual(TEXT("the flush sends the new row, one row's worth"), GetUploadedBytes() - Start, static_cast<int64>(DreamPaintRows::RowBytes));

	const int64 AfterFirst = GetUploadedBytes();
	TestEqual(TEXT("an acquire of the same gradient"), Manager->AcquirePaintGradientRow(MakeTurnedRamp(180.0f)), Row);
	Manager->FlushPaintRows();
	TestEqual(TEXT("writes nothing: the row already holds it"), GetUploadedBytes() - AfterFirst, static_cast<int64>(0));
	Manager->FlushPaintRows();
	TestEqual(TEXT("and a flush with nothing written sends nothing"), GetUploadedBytes() - AfterFirst, static_cast<int64>(0));
	Manager->ReleasePaintGradientRow(Row);
	Manager->ReleasePaintGradientRow(Row);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaintRowsTextTableTest,
	"DreamGUI.Text.Paint.Rows.ATextTableIsARowOfItsOwnThatOnlyItsTextWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaintRowsTextTableTest::RunTest(const FString& Parameters)
{
	using namespace DreamPaintRowsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("a manager"), Manager))
	{
		return false;
	}
	Manager->FlushPaintRows();
	int64 Before = GetUploadedBytes();
	const int32 Table = Manager->AcquirePaintTextRow();
	const int32 GradientRow = Manager->AcquirePaintGradientRow(MakeTurnedRamp(180.0f));
	if (!TestTrue(TEXT("a text gets a table"), Table >= 0) || !TestTrue(TEXT("and a gradient a row"), GradientRow >= 0))
	{
		return false;
	}
	TestNotEqual(TEXT("a table is a row of its own"), Table, GradientRow);
	TestTrue(TEXT("whose link fits a record's 16 bits"), static_cast<uint32>(Table) + 1u <= DreamPaintRows::RecordRowLinkMask);
	TestEqual(TEXT("one text table is taken"), CountPaintRows(Manager).TextRows, 1);
	Manager->FlushPaintRows();
	TestEqual(TEXT("the table goes up filled, its slots painting nothing, with the gradient row"), GetUploadedBytes() - Before,
		static_cast<int64>(2 * DreamPaintRows::RowBytes));

	const FVector4f Animation[2] = { FVector4f(0.25f, 0.0f, 0.0f, 2.0f), FVector4f(0.5f, 0.0f, 0.0f, 1.0f) };
	Before = GetUploadedBytes();
	Manager->WritePaintRowPixels(Table, DreamPaintRows::GetSlotAnimationPixel(1), MakeArrayView(Animation, 2));
	Manager->FlushPaintRows();
	TestEqual(TEXT("a text writes the pixels it changes, no more"), GetUploadedBytes() - Before, static_cast<int64>(2 * DreamPaintRows::BytesPerPixel));

	// Writes that are not a text's into its own table go nowhere.
	Before = GetUploadedBytes();
	Manager->WritePaintRowPixels(GradientRow, 0, MakeArrayView(Animation, 2));
	Manager->WritePaintRowPixels(Table, DreamPaintRows::RowWidth, MakeArrayView(Animation, 2));
	Manager->WritePaintRowPixels(Table, -1, MakeArrayView(Animation, 2));
	Manager->WritePaintRowPixels(INDEX_NONE, 0, MakeArrayView(Animation, 2));
	Manager->WritePaintRowPixels(Table, 0, TConstArrayView<FVector4f>());
	Manager->FlushPaintRows();
	TestEqual(TEXT("into a gradient row, past the row, before it, into no row, or nothing at all: nothing is written"),
		GetUploadedBytes() - Before, static_cast<int64>(0));
	Before = GetUploadedBytes();
	Manager->WritePaintRowPixels(Table, DreamPaintRows::RowWidth - 1, MakeArrayView(Animation, 2));
	Manager->FlushPaintRows();
	TestEqual(TEXT("a write running past the row's end is cut at it"), GetUploadedBytes() - Before, static_cast<int64>(DreamPaintRows::BytesPerPixel));

	// Given back, the table is emptied for any record still linking to it, and then nobody's to write.
	Before = GetUploadedBytes();
	Manager->ReleasePaintTextRow(Table);
	TestEqual(TEXT("the table is given back"), CountPaintRows(Manager).TextRows, 0);
	Manager->FlushPaintRows();
	TestEqual(TEXT("emptied on the way"), GetUploadedBytes() - Before, static_cast<int64>(DreamPaintRows::RowBytes));
	Before = GetUploadedBytes();
	Manager->WritePaintRowPixels(Table, 0, MakeArrayView(Animation, 2));
	Manager->ReleasePaintTextRow(Table);
	Manager->ReleasePaintTextRow(INDEX_NONE);
	Manager->FlushPaintRows();
	TestEqual(TEXT("a write to it, or a second release, does nothing"), GetUploadedBytes() - Before, static_cast<int64>(0));
	Manager->ReleasePaintGradientRow(GradientRow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaintRowsGrowthTest,
	"DreamGUI.Text.Paint.Rows.TheRowsGrowInPlaceAsTheyRunOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaintRowsGrowthTest::RunTest(const FString& Parameters)
{
	using namespace DreamPaintRowsTestLocal;
	FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("a manager"), Manager))
	{
		return false;
	}
	UTexture* Texture = Manager->GetPaintRowsTexture();
	const int32 RowsAtStart = CountPaintRows(Manager).TextureRows;
	TSet<int32> Taken;
	TArray<int32> Rows;
	for (int32 Index = 0; Index < RowsAtStart + 8; ++Index)
	{
		const int32 Row = Manager->AcquirePaintGradientRow(MakeTurnedRamp(static_cast<float>(Index)));
		if (!TestTrue(TEXT("every gradient gets a row"), Row >= 0))
		{
			return false;
		}
		Taken.Add(Row);
		Rows.Add(Row);
	}
	TestEqual(TEXT("each a different one"), Taken.Num(), RowsAtStart + 8);
	TestTrue(TEXT("the texture grew to hold them"), CountPaintRows(Manager).TextureRows > RowsAtStart);
	TestTrue(TEXT("and it is the same texture: whatever binds it follows the growth"), Manager->GetPaintRowsTexture() == Texture);
	for (const int32 Row : Rows)
	{
		Manager->ReleasePaintGradientRow(Row);
	}
	TestEqual(TEXT("all given back"), CountPaintRows(Manager).GradientRows, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaintRowsTeardownTest,
	"DreamGUI.Text.Paint.Rows.NothingIsHandedOutOrTakenBackOnceTheWorldIsTornDown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaintRowsTeardownTest::RunTest(const FString& Parameters)
{
	using namespace DreamPaintRowsTestLocal;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("a world"), World))
	{
		return false;
	}
	// Held past the world, as a text's late release would reach it: the world's texts give their rows back as they come down.
	TStrongObjectPtr<UDreamUIManagerWorldSubsystem> Manager(UDreamUIManagerWorldSubsystem::GetInstance(World));
	if (!TestTrue(TEXT("a manager"), Manager.IsValid()))
	{
		World->DestroyWorld(false);
		return false;
	}
	const int32 Table = Manager->AcquirePaintTextRow();
	const int32 Row = Manager->AcquirePaintGradientRow(MakeTurnedRamp(180.0f));
	TestTrue(TEXT("rows are taken while the world stands"), Table >= 0 && Row >= 0);
	World->DestroyWorld(false);
	TestTrue(TEXT("the world is torn down"), Manager->HasTornDownWorld());

	TestEqual(TEXT("no text table is handed out after it"), Manager->AcquirePaintTextRow(), (int32)INDEX_NONE);
	TestEqual(TEXT("nor a gradient row"), Manager->AcquirePaintGradientRow(MakeTurnedRamp(90.0f)), (int32)INDEX_NONE);
	const FVector4f Pixel(1.0f, 2.0f, 3.0f, 4.0f);
	Manager->WritePaintRowPixels(Table, 0, MakeArrayView(&Pixel, 1));
	Manager->ReleasePaintTextRow(Table);
	Manager->ReleasePaintGradientRow(Row);
	Manager->FlushPaintRows();
	const FPaintRowsCount Count = CountPaintRows(Manager.Get());
	TestEqual(TEXT("releases after the teardown are ignored: the rows go with the manager"), Count.TextRows, 1);
	TestEqual(TEXT("both kinds"), Count.GradientRows, 1);
	return true;
}

#endif
