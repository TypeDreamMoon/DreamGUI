// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGradientAsset.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamUIDrawCall.h"
#include "Core/DreamUIManager.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/DreamUISettings.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/Text/DreamTextPaint.h"
#include "Core/Text/DreamTextPainter.h"
#include "DreamUIRender/DreamUIRenderStats.h"
#include "Engine/World.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

#include "DreamScopedWorld.h"
#include "DreamTextTestFont.h"
#include "Driver/DreamDriverRig.h"

/*
 * The text component's half of painted text: what a text holds of its world's paint rows -- a table of its own, a gradient
 * row a painted layer, its record's link to the table -- and what it hands the painter (FDreamTextPaints), as its setters,
 * its layouts and its material leave them. What is read back is the table as the text last wrote it
 * (UDreamText::GetPaintTablePixel), the painter's parameters (UDreamText::MakePaintParams), the world's paint rows
 * (GetPaintRowsMemoryInfo), and the render stats' counters: a repaint is a call of the painter (TextPaints), a layout a run
 * of the text's layout (GetLayoutRunCount).
 */
namespace DreamTextPaintComponentTestLocal
{
	using namespace DreamPaintRows;

	FDreamGradient MakeTwoStopGradient(const FColor& InFrom, const FColor& InTo)
	{
		FDreamGradient Gradient;
		Gradient.Type = EDreamPaintType::Linear;
		Gradient.Stops = { FDreamGradientStop(0.0f, InFrom), FDreamGradientStop(1.0f, InTo) };
		return Gradient;
	}

	FDreamGradient GoldGradient() { return MakeTwoStopGradient(FColor(255, 243, 176), FColor(156, 106, 18)); }
	FDreamGradient IceGradient() { return MakeTwoStopGradient(FColor(220, 245, 255), FColor(40, 90, 200)); }

	FDreamTextPaint MakePaint(const FDreamGradient& InGradient)
	{
		FDreamTextPaint Paint;
		Paint.bEnabled = true;
		Paint.Gradient = InGradient;
		return Paint;
	}

	int64 ReadCounter(DreamUIRenderStats::ECounter InCounter)
	{
		return DreamUIRenderStats::TakeSnapshot(/*bInReset*/ false).Counters[static_cast<int32>(InCounter)];
	}

	void ResetCounters()
	{
		DreamUIRenderStats::TakeSnapshot(/*bInReset*/ true);
	}

	/** What a text last wrote into one pixel of its table; -9999 throughout, which no table holds, when it has none. */
	FVector4f TablePixel(const UDreamText* InText, int32 InPixel)
	{
		FVector4f Pixel(-9999.0f, -9999.0f, -9999.0f, -9999.0f);
		InText->GetPaintTablePixel(InPixel, Pixel);
		return Pixel;
	}

	/** A text on a widget of the rig's screen-space canvas, in the made-up font, 24 units. */
	UDreamText* MakeText(FDreamDriverRig& InRig, const FString& InName, const FString& InContent)
	{
		UDreamWidget* Label = InRig.MakeWidget(InName, nullptr, FVector2D(400.0, 60.0));
		UDreamText* Text = Label != nullptr ? Label->CreateNewVisual<UDreamText>() : nullptr;
		if (Text != nullptr)
		{
			Text->SetFont(NewObject<UDreamTextTestFont>(InRig.GetWorld()));
			Text->SetFontSize(24.0f);
			Text->SetText(FText::FromString(InContent));
		}
		return Text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintRecordLinkTest,
	"DreamGUI.Text.Paint.ATextsMarksPixelLinksItsRecordToItsPaintTableAndStaysANormalFloat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A painted text's widget record links to its table through bits 0..15 of its marks pixel -- the row plus one, 0 for none
 * -- where other visuals keep their extra mark, which a text has none of. The font mark keeps its byte, and the word keeps
 * its constant top byte: no link, at any font mark, makes it a denormal a GPU would flush to zero. A link past 16 bits is
 * cut to them, and no link packs as the plain marks do.
 */
bool FDreamTextPaintRecordLinkTest::RunTest(const FString& Parameters)
{
	auto IsDenormalBits = [](uint32 InBits) { return (InBits & 0x7f800000u) == 0 && (InBits & 0x007fffffu) != 0; };
	const uint32 Links[] = { 0u, 1u, 2u, 255u, 256u, 0x8000u, 0xFFFFu };
	for (int32 FontMark = 0; FontMark < 256; ++FontMark)
	{
		for (const uint32 Link : Links)
		{
			const uint32 Packed = UDreamVisual::PackTextWidgetMarks((uint8)FontMark, Link);
			if (!TestFalse(TEXT("No link at any font mark packs to a denormal"), IsDenormalBits(Packed))
				|| !TestEqual(TEXT("...the font mark is where the shader looks"), (int32)((Packed >> 16) & 0xff), FontMark)
				|| !TestEqual(TEXT("...and the link in the low 16 bits"), (int32)(Packed & DreamPaintRows::RecordRowLinkMask), (int32)Link))
			{
				return false;
			}
		}
		TestEqual(TEXT("No link packs as the plain marks do"), (int64)UDreamVisual::PackTextWidgetMarks((uint8)FontMark, 0u), (int64)UDreamVisual::PackWidgetMarks((uint8)FontMark, 0));
	}
	TestEqual(TEXT("A link past 16 bits is cut to them"), (int64)(UDreamVisual::PackTextWidgetMarks(3, 0x1FFFFu) & 0xFFFFFFu), (int64)(0x30000u | 0xFFFFu));
	TestEqual(TEXT("The top byte stays the constant"), (int64)(UDreamVisual::PackTextWidgetMarks(3, 0xFFFFu) & 0xFF000000u), (int64)UDreamVisual::WidgetMarksNormalFloatMarker);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintAnimationWritesTheTableTest,
	"DreamGUI.Text.Paint.APhaseAnAngleACentreOrAScaleWritesThePaintTableWithoutARepaintOrALayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A text painting its face takes a table of its own from its world's paint rows: slot 1 says which gradient row the face
 * paints with, the others nothing. Its phases, its angle offset, its centre offset and its scale are pixels of that table,
 * written the moment they are set -- what Sequencer and the tween helpers animate -- and no frame after repaints the text
 * or lays it out. A value set before the text has a table is kept, and written when it takes one.
 */
bool FDreamTextPaintAnimationWritesTheTableTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintComponentTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamText* Text = MakeText(Rig, TEXT("Painted"), TEXT("Gold leaf"));
	if (!TestNotNull(TEXT("A text"), Text))
	{
		return false;
	}
	Text->SetFacePaint(MakePaint(GoldGradient()));
	Rig.PumpFrames(2);
	const int32 TextRow = Text->GetPaintTextRow();
	if (!TestTrue(TEXT("The painted text holds a table of the world's paint rows"), TextRow != INDEX_NONE))
	{
		return false;
	}
	TestEqual(TEXT("...whose header names slot 1 the highest in use"), TablePixel(Text, TextHeaderPixel).X, 1.0f);
	const FVector4f OwnRows = TablePixel(Text, GetSlotRowsPixel(1));
	TestTrue(TEXT("...slot 1's face paints with a gradient row"), OwnRows.X >= 0.0f);
	TestEqual(TEXT("...its outline with none"), OwnRows.Y, NoRow);
	TestEqual(TEXT("...nor its overlay"), OwnRows.Z, NoRow);
	TestEqual(TEXT("...the overlay blended Normal"), OwnRows.W, 0.0f);
	TestTrue(TEXT("A slot it does not use reads no row throughout"), TablePixel(Text, GetSlotRowsPixel(2)) == FVector4f(NoRow, NoRow, NoRow, NoRow));
	TestTrue(TEXT("The painter is handed slot 1's face"), UDreamText::MakePaintParams(Text).Paints.Slots[DreamTextQuadCode::TextSlot].Face != nullptr);

	const int32 Laid = Text->GetCacheTextGeometryData().GetLayoutRunCount();
	ResetCounters();
	Text->SetFacePaintPhase(0.25f);
	TestEqual(TEXT("A face phase is written at once"), TablePixel(Text, GetSlotAnimationPixel(1)).X, 0.25f);
	Text->SetOutlinePaintPhase(0.5f);
	TestEqual(TEXT("...an outline phase"), TablePixel(Text, GetSlotAnimationPixel(1)).Y, 0.5f);
	Text->SetOverlayPaintPhase(-0.75f);
	TestEqual(TEXT("...an overlay phase"), TablePixel(Text, GetSlotAnimationPixel(1)).Z, -0.75f);
	Text->SetPaintAngleOffset(30.0f);
	TestEqual(TEXT("...an angle offset"), TablePixel(Text, GetSlotTransformPixel(1)).X, 30.0f);
	Text->SetPaintCenterOffset(FVector2D(0.125, -0.25));
	TestEqual(TEXT("...a centre offset across"), TablePixel(Text, GetSlotTransformPixel(1)).Y, 0.125f);
	TestEqual(TEXT("...and down"), TablePixel(Text, GetSlotTransformPixel(1)).Z, -0.25f);
	Text->SetPaintScale(2.0f);
	TestEqual(TEXT("...a scale"), TablePixel(Text, GetSlotTransformPixel(1)).W, 2.0f);
	TestEqual(TEXT("The getters answer what was set"), Text->GetPaintScale(), 2.0f);
	TestTrue(TEXT("...the centre offset too"), Text->GetPaintCenterOffset().Equals(FVector2D(0.125, -0.25), 1.0e-6));
	Rig.PumpFrames(2);
	TestEqual(TEXT("None of it repainted the text"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints), (int64)0);
	TestEqual(TEXT("...or laid it out"), Text->GetCacheTextGeometryData().GetLayoutRunCount(), Laid);
	TestEqual(TEXT("...and its table is the one it had"), Text->GetPaintTextRow(), TextRow);
	TestEqual(TEXT("The animation reaches the painter for the vertex-colour fallback too"), UDreamText::MakePaintParams(Text).Paints.FaceAnimation.Phase, 0.25f);

	// Set before the text paints anything: kept, and written into the table it takes.
	UDreamText* Later = MakeText(Rig, TEXT("Later"), TEXT("Later"));
	if (!TestNotNull(TEXT("Another text"), Later))
	{
		return false;
	}
	Later->SetFacePaintPhase(0.75f);
	Later->SetPaintAngleOffset(-45.0f);
	Rig.PumpFrames(1);
	TestEqual(TEXT("A text that paints nothing holds no table"), Later->GetPaintTextRow(), (int32)INDEX_NONE);
	Later->SetFacePaint(MakePaint(IceGradient()));
	Rig.PumpFrames(1);
	TestTrue(TEXT("Painting, it takes one"), Later->GetPaintTextRow() != INDEX_NONE);
	TestEqual(TEXT("...with the phase it was given before"), TablePixel(Later, GetSlotAnimationPixel(1)).X, 0.75f);
	TestEqual(TEXT("...and the angle offset"), TablePixel(Later, GetSlotTransformPixel(1)).X, -45.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintRowsOnlyTest,
	"DreamGUI.Text.Paint.AnotherGradientTakesAnotherRowWithoutARepaintAndTurningAPaintOnOrOffRepaints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * What a paint change costs. Another gradient for a layer already painting -- other stops, another preset, the preset's
 * own gradient edited -- takes another gradient row and points the table at it: no repaint, no layout. The overlay's blend
 * is a pixel of the table too. Turning a layer on or off, and the boxes, change what the quads carry (their slots, UV4,
 * colours): a repaint, never a layout. Two texts painting the same gradient share its row.
 */
bool FDreamTextPaintRowsOnlyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintComponentTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamText* Text = MakeText(Rig, TEXT("Painted"), TEXT("Gold leaf"));
	UDreamText* Twin = MakeText(Rig, TEXT("Twin"), TEXT("Gold too"));
	if (!TestTrue(TEXT("Two texts"), Text != nullptr && Twin != nullptr))
	{
		return false;
	}
	Text->SetFacePaint(MakePaint(GoldGradient()));
	Twin->SetFacePaint(MakePaint(GoldGradient()));
	Rig.PumpFrames(2);
	const float GoldRow = TablePixel(Text, GetSlotRowsPixel(1)).X;
	if (!TestTrue(TEXT("The face paints with a gradient row"), GoldRow >= 0.0f))
	{
		return false;
	}
	TestEqual(TEXT("Two texts painting the same gradient share its row"), TablePixel(Twin, GetSlotRowsPixel(1)).X, GoldRow);
	TestTrue(TEXT("...each with a table of its own"), Text->GetPaintTextRow() != Twin->GetPaintTextRow());
	const int32 Laid = Text->GetCacheTextGeometryData().GetLayoutRunCount();
	const int32 TextRow = Text->GetPaintTextRow();

	// Other stops: another row, at once, and the twin keeps the gold one.
	ResetCounters();
	Text->SetFacePaint(MakePaint(IceGradient()));
	const float IceRow = TablePixel(Text, GetSlotRowsPixel(1)).X;
	TestTrue(TEXT("Other stops: the face points at another gradient row"), IceRow >= 0.0f && IceRow != GoldRow);
	TestEqual(TEXT("...while the twin paints on with the gold one"), TablePixel(Twin, GetSlotRowsPixel(1)).X, GoldRow);
	Rig.PumpFrames(1);
	TestEqual(TEXT("...with no repaint"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints), (int64)0);
	TestEqual(TEXT("...the same table"), Text->GetPaintTextRow(), TextRow);

	// A preset: its gradient, and an edit of it, are rows too.
	UDreamGradientAsset* Preset = NewObject<UDreamGradientAsset>(GetTransientPackage());
	Preset->SetGradient(GoldGradient());
	FDreamTextPaint FromPreset = MakePaint(IceGradient());
	FromPreset.Preset = Preset;
	Text->SetFacePaint(FromPreset);
	TestEqual(TEXT("A preset's gradient paints, in the row the same gradient already has"), TablePixel(Text, GetSlotRowsPixel(1)).X, GoldRow);
	Preset->SetGradient(IceGradient());
	TestTrue(TEXT("The preset edited: the text takes the edited gradient's row"), TablePixel(Text, GetSlotRowsPixel(1)).X != GoldRow);
	Rig.PumpFrames(1);
	TestEqual(TEXT("...with no repaint for either"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints), (int64)0);

	// The overlay's blend lives in the table.
	Text->SetOverlayBlend(EDreamTextOverlayBlend::Add);
	TestEqual(TEXT("The overlay's blend is a pixel of the table"), TablePixel(Text, GetSlotRowsPixel(1)).W, (float)static_cast<uint8>(EDreamTextOverlayBlend::Add));
	Rig.PumpFrames(1);
	TestEqual(TEXT("...written with no repaint"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints), (int64)0);

	// A layer turned on: a repaint, and its row in the table.
	Text->SetOutlinePaint(MakePaint(GoldGradient()));
	Rig.PumpFrames(1);
	TestTrue(TEXT("An outline paint turned on repaints the text"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints) >= 1);
	TestEqual(TEXT("...and the outline paints with the gold row"), TablePixel(Text, GetSlotRowsPixel(1)).Y, GoldRow);
	ResetCounters();
	FDreamTextPaint Off = Text->GetOutlinePaint();
	Off.bEnabled = false;
	Text->SetOutlinePaint(Off);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Turned off, a repaint again"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints) >= 1);
	TestEqual(TEXT("...and no outline row"), TablePixel(Text, GetSlotRowsPixel(1)).Y, NoRow);

	// The boxes: a repaint; a single box's aspect is the table's, every other box's 1.
	const float BlockAspect = TablePixel(Text, GetSlotAnimationPixel(1)).W;
	TestTrue(TEXT("Measured across the text block, the aspect is its width over its height"), BlockAspect > 1.0f);
	ResetCounters();
	Text->SetPaintBoxes(EDreamTextPaintBox::Glyph, EDreamTextPaintBox::Glyph);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Other boxes repaint the text"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints) >= 1);
	TestEqual(TEXT("...a box per glyph has the unit square's aspect"), TablePixel(Text, GetSlotAnimationPixel(1)).W, 1.0f);
	TestEqual(TEXT("None of it laid the text out"), Text->GetCacheTextGeometryData().GetLayoutRunCount(), Laid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintRowsGoBackTest,
	"DreamGUI.Text.Paint.ATextGivesItsPaintRowsBackWhenItPaintsNothingOrLeavesItsWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A text's rows are its world's for as long as it paints: a table and a gradient row taken when it starts, both given back
 * when it stops painting, and when it leaves its tree. The world's paint rows count them.
 */
bool FDreamTextPaintRowsGoBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintComponentTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	UDreamUIManagerWorldSubsystem* Manager = Rig.IsUsable() ? UDreamUIManagerWorldSubsystem::GetInstance(Rig.GetWorld()) : nullptr;
	if (!TestNotNull(TEXT("The rig's world has its UI manager"), Manager) || !TestNotNull(TEXT("...and its paint rows"), Manager->GetPaintRowsTexture()))
	{
		return false;
	}
	auto CountRows = [Manager](int32& OutTextRows, int32& OutGradientRows)
	{
		int32 TextureRows = 0;
		int64 TextureBytes = 0;
		Manager->GetPaintRowsMemoryInfo(TextureRows, OutTextRows, OutGradientRows, TextureBytes);
	};
	int32 TextRows0 = 0, GradientRows0 = 0;
	CountRows(TextRows0, GradientRows0);

	UDreamText* Text = MakeText(Rig, TEXT("Painted"), TEXT("Gold leaf"));
	if (!TestNotNull(TEXT("A text"), Text))
	{
		return false;
	}
	Text->SetFacePaint(MakePaint(GoldGradient()));
	Rig.PumpFrames(2);
	int32 TextRows = 0, GradientRows = 0;
	CountRows(TextRows, GradientRows);
	TestEqual(TEXT("Painting, the text took a table"), TextRows, TextRows0 + 1);
	TestEqual(TEXT("...and a gradient row"), GradientRows, GradientRows0 + 1);

	FDreamTextPaint Off = Text->GetFacePaint();
	Off.bEnabled = false;
	Text->SetFacePaint(Off);
	Rig.PumpFrames(1);
	CountRows(TextRows, GradientRows);
	TestEqual(TEXT("Painting nothing, it gave the table back"), TextRows, TextRows0);
	TestEqual(TEXT("...and the gradient row"), GradientRows, GradientRows0);
	TestEqual(TEXT("...and holds none"), Text->GetPaintTextRow(), (int32)INDEX_NONE);

	Text->SetFacePaint(MakePaint(GoldGradient()));
	Rig.PumpFrames(1);
	CountRows(TextRows, GradientRows);
	TestEqual(TEXT("Painting again, a table again"), TextRows, TextRows0 + 1);
	Text->GetWidget()->DestroyWidget();
	Rig.PumpFrames(1);
	CountRows(TextRows, GradientRows);
	TestEqual(TEXT("Gone from its tree, it gave its rows back"), TextRows, TextRows0);
	TestEqual(TEXT("...every one"), GradientRows, GradientRows0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintTagSlotsTest,
	"DreamGUI.Text.Paint.TagPaintsTakeSlotsTwoToNineInTheOrderTheTextFirstNamesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A rich text's tag paints are resolved by name when the text paints: its custom style's entry of that name first. Each
 * name that resolves takes the next of slots 2 to 9, in the order the text first names it -- a name named twice keeps one
 * slot -- and a name that resolves to nothing takes none, its run drawn as if it had no tag paint. A tag slot paints the
 * faces of its run, measured across the run; the text's own slot paints nothing when the text has no paint of its own.
 */
bool FDreamTextPaintTagSlotsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintComponentTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIRichTextCustomStyleData* Styles = NewObject<UDreamUIRichTextCustomStyleData>(Rig.GetWorld());
	FDreamUIRichTextCustomStyleItemData IceEntry;
	IceEntry.paintType = EDreamUIRichTextCustomStyleData_PaintType::Set;
	IceEntry.paint.Gradient = IceGradient();
	FDreamUIRichTextCustomStyleItemData GoldEntry;
	GoldEntry.paintType = EDreamUIRichTextCustomStyleData_PaintType::Set;
	GoldEntry.paint.Gradient = GoldGradient();
	TMap<FName, FDreamUIRichTextCustomStyleItemData> Entries;
	Entries.Add(TEXT("Ice"), IceEntry);
	Entries.Add(TEXT("Gold"), GoldEntry);
	Styles->SetDataMap(Entries);

	UDreamText* Text = MakeText(Rig, TEXT("Tagged"), TEXT("<gradient=Ice>cold</gradient> <gradient=Nowhere>plain</gradient> <gradient=Gold>warm</gradient> <gradient=Ice>again</gradient>"));
	if (!TestNotNull(TEXT("A text"), Text))
	{
		return false;
	}
	Text->SetRichText(true);
	Text->SetRichTextCustomStyleData(Styles);
	Rig.PumpFrames(2);

	const FDreamTextDisplayList& DisplayList = Text->GetCacheTextGeometryData().GetDisplayList();
	if (!TestEqual(TEXT("The layout names three tag paints"), DisplayList.PaintNames.Num(), 3))
	{
		return false;
	}
	const FDreamTextPaintParams Params = UDreamText::MakePaintParams(Text);
	if (!TestNotNull(TEXT("The painter is told each name's slot"), Params.Paints.NameSlots) || !TestEqual(TEXT("...one for every name"), Params.Paints.NameSlots->Num(), 3))
	{
		return false;
	}
	TestEqual(TEXT("The first named, Ice, takes slot 2"), (int32)(*Params.Paints.NameSlots)[0], DreamTextQuadCode::FirstTagSlot);
	TestEqual(TEXT("A name that resolves to nothing takes none"), (int32)(*Params.Paints.NameSlots)[1], 0);
	TestEqual(TEXT("The next named, Gold, takes slot 3"), (int32)(*Params.Paints.NameSlots)[2], DreamTextQuadCode::FirstTagSlot + 1);
	const FDreamTextPaintSlot& IceSlot = Params.Paints.Slots[DreamTextQuadCode::FirstTagSlot];
	const FDreamTextPaintSlot& GoldSlot = Params.Paints.Slots[DreamTextQuadCode::FirstTagSlot + 1];
	TestTrue(TEXT("Slot 2 paints Ice's gradient on the faces"), IceSlot.Face != nullptr && *IceSlot.Face == IceGradient());
	TestTrue(TEXT("...and slot 3 Gold's"), GoldSlot.Face != nullptr && *GoldSlot.Face == GoldGradient());
	TestTrue(TEXT("...neither an outline nor an overlay"), IceSlot.Outline == nullptr && IceSlot.Overlay == nullptr);
	TestTrue(TEXT("...measured across the run"), IceSlot.HorizontalBox == EDreamTextPaintBox::Run && IceSlot.VerticalBox == EDreamTextPaintBox::Run);
	TestFalse(TEXT("A text with no paint of its own leaves slot 1 unused"), Params.Paints.Slots[DreamTextQuadCode::TextSlot].IsUsed());
	TestFalse(TEXT("...and slot 4 too"), Params.Paints.Slots[DreamTextQuadCode::FirstTagSlot + 2].IsUsed());

	TestTrue(TEXT("The text holds a table for its tag paints"), Text->GetPaintTextRow() != INDEX_NONE);
	TestEqual(TEXT("...whose header names slot 3 the highest in use"), TablePixel(Text, DreamPaintRows::TextHeaderPixel).X, 3.0f);
	const float IceRow = TablePixel(Text, DreamPaintRows::GetSlotRowsPixel(2)).X;
	const float GoldRow = TablePixel(Text, DreamPaintRows::GetSlotRowsPixel(3)).X;
	TestTrue(TEXT("...slots 2 and 3 each with a gradient row of its own"), IceRow >= 0.0f && GoldRow >= 0.0f && IceRow != GoldRow);
	TestEqual(TEXT("...slot 1 with none"), TablePixel(Text, DreamPaintRows::GetSlotRowsPixel(1)).X, DreamPaintRows::NoRow);
	TestEqual(TEXT("...and a tag slot's aspect is the unit square's"), TablePixel(Text, DreamPaintRows::GetSlotAnimationPixel(2)).W, 1.0f);

	// The custom style edited in place: the names are resolved again, and the slot paints the new gradient's row.
	IceEntry.paint.Gradient = GoldGradient();
	Entries.Add(TEXT("Ice"), IceEntry);
	Styles->SetDataMap(Entries);
	Rig.PumpFrames(1);
	TestEqual(TEXT("A style edited: slot 2 paints its new gradient, Gold's row"), TablePixel(Text, DreamPaintRows::GetSlotRowsPixel(2)).X, GoldRow);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintProjectPresetTest,
	"DreamGUI.Text.Paint.AProjectPresetEditedInTheEditorIsResolvedAgainWithoutALayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A tag paint's name may stand for one of the project's gradient presets (UDreamGUISettings::GradientPresets). An edit of
 * the presets in the editor is announced (DreamGradientPresets::OnPresetsChanged), and every text that resolved its names
 * resolves them again: a preset edited to another gradient takes another row, with no repaint; a preset taken away leaves
 * its runs drawn as if they had no tag paint, which only a repaint shows. Neither lays the text out.
 */
bool FDreamTextPaintProjectPresetTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintComponentTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	// Project presets live on the settings' default object; this test's own goes again at its end.
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	const TMap<FName, FString> SavedPresets = Settings->GradientPresets;
	ON_SCOPE_EXIT
	{
		Settings->GradientPresets = SavedPresets;
	};
	const FName PresetName(TEXT("DreamTextPaintComponentTestDawn"));
	Settings->GradientPresets.Add(PresetName, TEXT("linear-gradient(90deg, #FF8040, #4060FF)"));

	UDreamText* Text = MakeText(Rig, TEXT("Preset"), TEXT("<gradient=DreamTextPaintComponentTestDawn>dawn</gradient> sky"));
	if (!TestNotNull(TEXT("A text"), Text))
	{
		return false;
	}
	Text->SetRichText(true);
	Rig.PumpFrames(2);
	const float DawnRow = TablePixel(Text, DreamPaintRows::GetSlotRowsPixel(DreamTextQuadCode::FirstTagSlot)).X;
	if (!TestTrue(TEXT("The preset's name takes slot 2, which paints from a gradient row"), DawnRow >= 0.0f))
	{
		return false;
	}
	const int32 Laid = Text->GetCacheTextGeometryData().GetLayoutRunCount();

	// Edited in the editor: the name stands for another gradient now.
	Settings->GradientPresets.Add(PresetName, TEXT("linear-gradient(90deg, #FFE070, #208040)"));
	ResetCounters();
	DreamGradientPresets::OnPresetsChanged().Broadcast();
	Rig.PumpFrames(1);
	const float DuskRow = TablePixel(Text, DreamPaintRows::GetSlotRowsPixel(DreamTextQuadCode::FirstTagSlot)).X;
	TestTrue(TEXT("The preset edited: slot 2 paints from another gradient row"), DuskRow >= 0.0f && DuskRow != DawnRow);
	TestEqual(TEXT("...with no repaint"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints), (int64)0);

	// Taken away: the name stands for nothing now -- read as CSS, it does not read.
	Settings->GradientPresets.Remove(PresetName);
	ResetCounters();
	DreamGradientPresets::OnPresetsChanged().Broadcast();
	Rig.PumpFrames(1);
	const FDreamTextPaintParams Params = UDreamText::MakePaintParams(Text);
	TestTrue(TEXT("The preset taken away: its run is drawn as if it had no tag paint"),
		Params.Paints.NameSlots == nullptr || (Params.Paints.NameSlots->Num() == 1 && (*Params.Paints.NameSlots)[0] == 0));
	TestEqual(TEXT("...the text holding no table"), Text->GetPaintTextRow(), (int32)INDEX_NONE);
	TestTrue(TEXT("...which a repaint shows"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints) >= 1);
	TestEqual(TEXT("None of it laid the text out"), Text->GetCacheTextGeometryData().GetLayoutRunCount(), Laid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintMaterialGateTest,
	"DreamGUI.Text.Paint.AMaterialThatDoesNotShadeThroughDreamGUIDrawsNoColourFacesAndPaintsInVertexColours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Only DreamGUI's shading -- the built-in UI shader, or a material built on MF_DreamUI_Shade, which carries its marker
 * parameter -- decodes colour glyphs and reads the paint rows. A text drawn by any other material lays its emoji out as if
 * its font had no colour face (the layout input's bAllowColorFaces), and paints in its vertex colours: the painter is
 * handed the gradients with bVertexColorFallback, and the text holds no rows. Its animation then repaints its colours.
 */
bool FDreamTextPaintMaterialGateTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintComponentTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	TGuardValue<bool> BuiltInShader(GetMutableDefault<UDreamUISettings>()->bUseBuiltInUIShader, true);
	UDreamText* Text = MakeText(Rig, TEXT("Painted"), TEXT("Gold leaf"));
	if (!TestNotNull(TEXT("A text"), Text))
	{
		return false;
	}
	Text->SetFacePaint(MakePaint(GoldGradient()));
	Rig.PumpFrames(2);
	TestTrue(TEXT("Drawn by the built-in UI shader, colour faces may draw emoji"), UDreamText::MakeLayoutInput(Text, Text->GetFontSize()).bAllowColorFaces);
	TestFalse(TEXT("...the paints go through the rows"), UDreamText::MakePaintParams(Text).Paints.bVertexColorFallback);
	TestTrue(TEXT("...and the text holds a table"), Text->GetPaintTextRow() != INDEX_NONE);

	UMaterialInterface* Material = UMaterial::GetDefaultMaterial(MD_Surface);
	if (!TestNotNull(TEXT("A material that knows nothing of DreamGUI"), Material))
	{
		return false;
	}
	Text->SetOverrideMaterial(Material);
	Rig.PumpFrames(1);
	TestFalse(TEXT("Drawn by it, no colour face draws an emoji"), UDreamText::MakeLayoutInput(Text, Text->GetFontSize()).bAllowColorFaces);
	const FDreamTextPaintParams Fallback = UDreamText::MakePaintParams(Text);
	TestTrue(TEXT("...the paints go in the vertex colours"), Fallback.Paints.bVertexColorFallback);
	TestTrue(TEXT("...the face's gradient handed to the painter for it"), Fallback.Paints.Slots[DreamTextQuadCode::TextSlot].Face != nullptr
		&& *Fallback.Paints.Slots[DreamTextQuadCode::TextSlot].Face == GoldGradient());
	TestEqual(TEXT("...and the text holds no rows"), Text->GetPaintTextRow(), (int32)INDEX_NONE);
	ResetCounters();
	Text->SetFacePaintPhase(0.5f);
	Rig.PumpFrames(1);
	TestTrue(TEXT("A phase, being in the vertex colours, repaints them"), ReadCounter(DreamUIRenderStats::ECounter::TextPaints) >= 1);
	TestEqual(TEXT("...with the phase handed to the painter"), UDreamText::MakePaintParams(Text).Paints.FaceAnimation.Phase, 0.5f);

	Text->SetOverrideMaterial(nullptr);
	Rig.PumpFrames(1);
	TestTrue(TEXT("The material taken off, colour faces again"), UDreamText::MakeLayoutInput(Text, Text->GetFontSize()).bAllowColorFaces);
	TestFalse(TEXT("...the paints through the rows again"), UDreamText::MakePaintParams(Text).Paints.bVertexColorFallback);
	TestTrue(TEXT("...a table taken again"), Text->GetPaintTextRow() != INDEX_NONE);
	TestEqual(TEXT("...with the phase set meanwhile written into it"), TablePixel(Text, DreamPaintRows::GetSlotAnimationPixel(1)).X, 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintBaseColourTest,
	"DreamGUI.Text.Paint.APaintedFaceIsWhiteTintedAsTheTextAndItsEffectsFadeWithTheOpacityAlone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A gradient stands in for the face's colour, as CSS's background-clip: text with color: transparent does: a painted face
 * is white, tinted by the content tint of the text's ancestors and at its render opacity -- what the text's own colour
 * would be drawn as, were it white. Its effects (outline, glow, underlay) are drawn at that opacity, which the text
 * colour's alpha is no part of: a hollow text keeps its outline, a faded text fades all of it.
 */
bool FDreamTextPaintBaseColourTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextPaintComponentTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamWidget* Parent = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	Parent->OnRegister();
	UDreamWidget* Child = NewObject<UDreamWidget>(TestWorld.World, NAME_None, RF_Transient);
	Child->OnRegister();
	UDreamText* Text = Child->TrySetParent(Parent, false) ? Child->CreateNewVisual<UDreamText>() : nullptr;
	if (!TestNotNull(TEXT("A text under a parent"), Text))
	{
		Parent->DestroyWidget();
		return false;
	}
	Text->SetFont(NewObject<UDreamTextTestFont>(TestWorld.World));
	TestEqual(TEXT("Untinted, a painted face is white"), UDreamText::MakePaintParams(Text).PaintBaseColor, FColor::White);
	TestEqual(TEXT("...and its effects drawn opaque"), UDreamText::MakePaintParams(Text).EffectOpacity, 1.0f, 1.0e-6f);

	Parent->SetContentTint(FLinearColor(1.0f, 0.5f, 0.25f, 0.5f));
	Child->SetRenderOpacity(0.5f);
	Text->SetColor(FColor::White);
	const FDreamTextPaintParams Tinted = UDreamText::MakePaintParams(Text);
	TestEqual(TEXT("Tinted and faded, it is white drawn as the text's own colour would be"), Tinted.PaintBaseColor, Text->GetFinalColor());
	TestEqual(TEXT("...its effects at the tint's alpha times the opacity"), Tinted.EffectOpacity, (float)Tinted.PaintBaseColor.A / 255.0f, 1.0e-6f);
	TestTrue(TEXT("...which is a quarter, give or take a step"), FMath::Abs(Tinted.EffectOpacity - 0.25f) < 2.0f / 255.0f);

	Text->SetColor(FColor(255, 255, 255, 0));
	const FDreamTextPaintParams Hollow = UDreamText::MakePaintParams(Text);
	TestEqual(TEXT("A clear text colour leaves the paint's colour as it was"), Hollow.PaintBaseColor, Tinted.PaintBaseColor);
	TestEqual(TEXT("...and its effects at the same opacity: a hollow text keeps its outline"), Hollow.EffectOpacity, Tinted.EffectOpacity, 1.0e-6f);
	Parent->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintFieldCorrectionSignTest,
	"DreamGUI.Text.Paint.TheFieldTextCorrectionReachesTheShadersAsTheSignOfTheFieldRange",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * UDreamGUISettings::bFieldTextCorrection has no component of its own on its way to the shaders: the canvases pass it with
 * the font atlas's geometry, the field range negated (FontAtlasInfo.z), as the small-text correction rides on w's sign.
 */
bool FDreamTextPaintFieldCorrectionSignTest::RunTest(const FString& Parameters)
{
	UDreamTextTestFont* Font = NewObject<UDreamTextTestFont>(GetTransientPackage());
	FDreamUIDrawCall DrawCall(EDreamUIDrawCallType::BatchMesh);
	DrawCall.Font = Font;
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	TGuardValue<bool> Correction(Settings->bFieldTextCorrection, false);
	const FVector4f Plain = UDreamCanvas::MakeFontAtlasInfo(DrawCall);
	if (!TestTrue(TEXT("Without the correction the field range is positive"), Plain.Z > 0.0f))
	{
		return false;
	}
	Settings->bFieldTextCorrection = true;
	const FVector4f Corrected = UDreamCanvas::MakeFontAtlasInfo(DrawCall);
	TestEqual(TEXT("With it, the same range negated"), Corrected.Z, -Plain.Z);
	TestEqual(TEXT("...and nothing else of the atlas's geometry changed"), Corrected.W, Plain.W);
	return true;
}

#endif
