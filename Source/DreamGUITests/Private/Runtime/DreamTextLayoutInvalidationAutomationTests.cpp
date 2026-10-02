// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#include "Controls/DreamRichTextBlock.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_Bitmap.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIGeometry.h"
#include "Core/Text/DreamTextLayout.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_PropertyWithEase.h"
#include "MeshModifier/TextAnimation/DreamMeshModifierTextAnimation_Selector.h"
#include "DreamLayoutInvalidationTestTypes.h"
#include "DreamScopedWorld.h"
#include "DreamTextTestFont.h"
#include "Driver/DreamDriverRig.h"

/*
 * Every UDreamText content setter used to wrap its MarkLayoutForRebuild in
 * `if (GetWidget()->GetLayoutSelf())`, and SetFont did not mark at all.
 *
 * That premise was true when the legacy FlexBox LayoutSelf was the only thing that read
 * UDreamVisual::GetPreferredWidth/Height. The panel layouts added a second reader -
 * UDreamPanelLayoutBase::GetDesiredSize measures *any* child that has a Visual, with no LayoutSelf
 * anywhere in the picture - and from that point the gate silently dropped every text reflow. A plain
 * text child in a StackBox has no LayoutSelf (CreateNewLayoutSelf has two callers and the control
 * registry only sets LayoutSelfClass for Spacer), so the gate was closed in exactly the common case.
 *
 * It only bit at runtime: PostEditChangeProperty marks unconditionally, so changing the text in the
 * details panel always reflowed and the hole was invisible to whoever was authoring the UI.
 *
 * These tests assert the invalidation contract - that the setter dirties the parent layout - rather
 * than the measured extent. Font metrics headless under -nullrhi are not something to build an
 * assertion on, and the defect was never in the measurement: it was that the measurement was never
 * asked for.
 */

namespace DreamTextLayoutInvalidationTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Game, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	/** Root(counting overlay) -> Child(UDreamText, deliberately no LayoutSelf). */
	struct FTextInPanelFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Child = nullptr;
		UDreamText* Text = nullptr;
		UDreamLayoutPassCountingOverlay* Overlay = nullptr;

		bool Build(UWorld* World)
		{
			Root = NewObject<UDreamWidget>(World);
			Child = NewObject<UDreamWidget>(Root);
			Root->SetWidth(320.0f);
			Root->SetHeight(180.0f);
			Child->SetWidth(40.0f);
			Child->SetHeight(20.0f);
			if (!Child->TrySetParent(Root, false))
			{
				return false;
			}
			Overlay = Cast<UDreamLayoutPassCountingOverlay>(
				Root->CreateNewLayoutContainer(UDreamLayoutPassCountingOverlay::StaticClass()));
			Text = Cast<UDreamText>(Child->CreateNewVisual(UDreamText::StaticClass()));
			if (!Overlay || !Text)
			{
				return false;
			}
			Root->OnRegister();
			Child->OnRegister();
			return true;
		}

		/** Drain the cold start, then zero the counter so each test measures one edit. */
		void Settle(UDreamUIManagerWorldSubsystem* Manager)
		{
			UDreamWidget::MarkLayoutForRebuild(Root);
			Manager->TickDreamUI(0.016f);
			Manager->TickDreamUI(0.016f);
			Overlay->PassCount = 0;
		}
	};

	/**
	 * Root(canvas, in the world) -> Child(UDreamText): a text that lays out, which a text with no render canvas never
	 * does, and that gets geometry when the manager ticks. The visual is made once the child has its canvas, so it is
	 * enrolled with it. Torn down with the fixture, before the world.
	 */
	struct FTextOnCanvasFixture
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Child = nullptr;
		UDreamText* Text = nullptr;

		bool Build(UWorld* World, UDreamUIFontData_BaseObject* InFont, const FVector2D& InSize)
		{
			Root = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
			Root->SetWidth(1000.0f);
			Root->SetHeight(600.0f);
			Root->OnRegister();
			UDreamCanvas* Canvas = Root->AddComponent<UDreamCanvas>();
			if (Canvas == nullptr)
			{
				return false;
			}
			Canvas->SetRenderMode(EDreamRenderMode::WorldSpace);
			Child = NewObject<UDreamWidget>(World, NAME_None, RF_Transient);
			Child->SetWidth(InSize.X);
			Child->SetHeight(InSize.Y);
			Child->OnRegister();
			if (!Child->TrySetParent(Root, false))
			{
				return false;
			}
			Text = Child->CreateNewVisual<UDreamText>();
			if (Text == nullptr)
			{
				return false;
			}
			Text->SetFont(InFont);
			return Child->GetRenderCanvas() != nullptr;
		}

		~FTextOnCanvasFixture()
		{
			if (Root != nullptr)
			{
				Root->DestroyWidget();
			}
		}
	};

	/** A font from a file the engine ships, rasterized on the spot (see FScopedSynchronousGlyphs). */
	template<class TFont>
	TFont* MakeEngineFont(UWorld* World, const TCHAR* Name)
	{
		TFont* Font = NewObject<TFont>(World);
		Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), Name), false);
		Font->InitFont();
		return Font;
	}

	/** Every glyph rasterized when it is asked for, for as long as this lives, so a quad is there to be read back. */
	struct FScopedSynchronousGlyphs
	{
		FScopedSynchronousGlyphs() { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32); }
		~FScopedSynchronousGlyphs() { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); }
	};

	/** The world point of a caret, which the caret table keeps in the text widget's own 2D space. */
	FVector CaretWorldPoint(const UDreamText* InText, const FVector2f& InCaretPosition)
	{
		return InText->GetWidget()->GetWorldTransform().TransformPosition(FVector(0.0, InCaretPosition.X, InCaretPosition.Y));
	}

	/** Whether a line's carets, taken in the order of the text, ever step back to the left. */
	bool CaretsStepBack(const FDreamUITextLineProperty& InLine)
	{
		for (int32 Index = 1; Index < InLine.CaretPropertyList.Num(); Index++)
		{
			if (InLine.CaretPropertyList[Index].CaretPosition.X < InLine.CaretPropertyList[Index - 1].CaretPosition.X - 0.01f)
			{
				return true;
			}
		}
		return false;
	}

	/** The visual runs of one line, left to right. */
	TArray<FDreamTextVisualRun> RunsOfLine(const UDreamText* InText, int32 InLineIndex)
	{
		TArray<FDreamTextVisualRun> Runs;
		for (const FDreamTextVisualRun& Run : InText->GetCacheTextGeometryData().GetDisplayList().VisualRuns)
		{
			if (Run.LineIndex == InLineIndex)
			{
				Runs.Add(Run);
			}
		}
		return Runs;
	}

	/** A registered widget of the given size, under Parent when there is one. */
	UDreamWidget* MakeSizedWidget(UWorld* World, UDreamWidget* Parent, const TCHAR* Name, float W, float H)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(Name);
		Widget->SetWidth(W);
		Widget->SetHeight(H);
		Widget->OnRegister();
		if (Parent)
		{
			Widget->TrySetParent(Parent, false);
		}
		return Widget;
	}

	/**
	 * A paragraph in the made-up font that wraps at its box -- AutoWrapText on a text that would otherwise run off to the
	 * right -- made on InWidget once it is under its canvas. At size 24 it wraps onto a few lines at 500 or 600 wide, and
	 * onto more at 300.
	 */
	UDreamText* MakeAutoWrapText(UWorld* World, UDreamWidget* InWidget)
	{
		UDreamText* Text = InWidget->CreateNewVisual<UDreamText>();
		if (Text == nullptr)
		{
			return nullptr;
		}
		Text->SetFont(NewObject<UDreamTextTestFont>(World));
		Text->SetFontSize(24.0f);
		Text->SetOverflowType(EDreamUITextOverflowType::HorizontalOverflow);
		Text->SetAutoWrapText(true);
		Text->SetText(FText::FromString(TEXT("The quick brown fox jumps over the lazy dog, and then over the lazy dog once again.")));
		return Text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextSetTextReflowsPanelWithoutLayoutSelfTest,
	"DreamGUI.Layout.TextInvalidation.SetTextReflowsPanelWithoutLayoutSelf",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextSetTextReflowsPanelWithoutLayoutSelfTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FTextInPanelFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	TestNull(TEXT("The text child deliberately has no LayoutSelf"), Fixture.Child->GetLayoutSelf());

	Fixture.Settle(Manager);
	TestEqual(TEXT("Settled before the edit"), Fixture.Overlay->PassCount, 0);

	Fixture.Text->SetText(FText::FromString(TEXT("a much longer run of text than before")));
	Manager->TickDreamUI(0.016f);
	TestEqual(TEXT("SetText reflows the parent panel"), Fixture.Overlay->PassCount, 1);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextMetricSettersReflowPanelTest,
	"DreamGUI.Layout.TextInvalidation.MetricSettersReflowPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextMetricSettersReflowPanelTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FTextInPanelFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Text->SetText(FText::FromString(TEXT("measured text")));

	// Each of these changes the extent the panel would measure, so each has to dirty the panel.
	struct FCase { const TCHAR* Name; TFunction<void(UDreamText*)> Apply; };
	const TArray<FCase> Cases = {
		{ TEXT("SetFontSize"),   [](UDreamText* T) { T->SetFontSize(T->GetFontSize() + 13.0f); } },
		{ TEXT("SetFontSpace"),  [](UDreamText* T) { T->SetFontSpace(T->GetFontSpace() + FVector2D(3.0, 2.0)); } },
		{ TEXT("SetUseKerning"), [](UDreamText* T) { T->SetUseKerning(!T->GetUseKerning()); } },
	};

	for (const FCase& Case : Cases)
	{
		Fixture.Settle(Manager);
		Case.Apply(Fixture.Text);
		Manager->TickDreamUI(0.016f);
		TestEqual(*FString::Printf(TEXT("%s reflows the parent panel"), Case.Name),
			Fixture.Overlay->PassCount, 1);
	}

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextSetFontReflowsPanelTest,
	"DreamGUI.Layout.TextInvalidation.SetFontReflowsPanel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextSetFontReflowsPanelTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FTextInPanelFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Text->SetText(FText::FromString(TEXT("measured text")));

	// SetFont was the worst of the family: it never marked layout at all, on any path, so swapping the
	// font left every glyph re-rasterised and every layout stale.
	UDreamUIFontData_BaseObject* OriginalFont = Fixture.Text->GetFont();
	Fixture.Settle(Manager);
	Fixture.Text->SetFont(nullptr);
	Manager->TickDreamUI(0.016f);
	TestNotEqual(TEXT("The font actually changed"), (void*)Fixture.Text->GetFont(), (void*)OriginalFont);
	TestEqual(TEXT("SetFont reflows the parent panel"), Fixture.Overlay->PassCount, 1);

	Fixture.Root->DestroyWidget();
	return true;
}

/*
 * UpdateCacheTextGeometry gives up without a render canvas -- the canvas is where the root scale and
 * the world-space flag come from -- so a text in a headless test or in a Blueprint authoring tree has
 * no lines at all, however much text it holds. FindCaretByIndex has always known that; the rest of the
 * caret family indexed straight into the empty array, and UUITextInput calls them on every edit.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCaretQueriesSurviveNoLayoutTest,
	"DreamGUI.Text.Caret.QueriesAnswerOnATextThatWasNeverLaidOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextCaretQueriesSurviveNoLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FTextInPanelFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	// Non-empty text is what makes this interesting: the two functions that had a guard were guarded on
	// the text being empty, which this is not.
	Fixture.Text->SetText(FText::FromString(TEXT("not empty")));
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("a caret index maps to the start of the text"), Fixture.Text->GetCharIndexByCaretIndex(4), 0);
	TestEqual(TEXT("and so does one past the end"), Fixture.Text->GetCharIndexByCaretIndex(999), 0);

	FVector2f CaretPosition(12.0f, 0.0f);
	int32 CaretIndex = 7;
	Fixture.Text->FindCaret(CaretPosition, 0, CaretIndex);
	TestEqual(TEXT("FindCaret answers rather than indexing a line that is not there"), CaretIndex, 0);
	Fixture.Text->FindCaret(CaretPosition, 5, CaretIndex);
	TestEqual(TEXT("including for a line index out of range"), CaretIndex, 0);

	FVector2f FoundPosition(0.0f, 0.0f);
	int32 FoundLine = 3;
	int32 FoundIndex = 3;
	Fixture.Text->FindCaretByWorldPosition(FVector::ZeroVector, FoundPosition, FoundLine, FoundIndex);
	TestEqual(TEXT("a hit test falls back to the first caret"), FoundIndex, 0);
	TestEqual(TEXT("on the first line"), FoundLine, 0);

	TArray<FDreamUITextSelectionProperty> Selection;
	Fixture.Text->GetSelectionProperty(0, 9, Selection);
	TestEqual(TEXT("a selection over an unlaid-out text highlights nothing, there being nothing on screen"), Selection.Num(), 0);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextTransformIsPresentationOnlyTest,
	"DreamGUI.Text.Pipeline.TextTransformChangesWhatIsDrawnAndNotWhatIsStored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextTransformIsPresentationOnlyTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	FTextInPanelFixture Fixture;
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager) || !Fixture.Build(TestWorld.World))
	{
		return false;
	}
	Fixture.Text->SetText(FText::FromString(TEXT("Mixed Case")));

	// UMG transforms at the same point, and for the same reason: a caret index, a copy and anything
	// reading the text back must still see what the author wrote.
	Fixture.Text->SetTextTransform(EDreamUITextTransformPolicy::ToUpper);
	const FDreamTextLayoutInput Upper = UDreamText::MakeLayoutInput(Fixture.Text, Fixture.Text->GetFontSize());
	TestEqual(TEXT("the layout is given upper case"), Upper.Content, FString(TEXT("MIXED CASE")));

	Fixture.Text->SetTextTransform(EDreamUITextTransformPolicy::ToLower);
	const FDreamTextLayoutInput Lower = UDreamText::MakeLayoutInput(Fixture.Text, Fixture.Text->GetFontSize());
	TestEqual(TEXT("and lower case"), Lower.Content, FString(TEXT("mixed case")));

	Fixture.Text->SetTextTransform(EDreamUITextTransformPolicy::None);
	const FDreamTextLayoutInput None = UDreamText::MakeLayoutInput(Fixture.Text, Fixture.Text->GetFontSize());
	TestEqual(TEXT("None leaves it alone"), None.Content, FString(TEXT("Mixed Case")));
	TestEqual(TEXT("and the Text property never changed"), Fixture.Text->GetText().ToString(), FString(TEXT("Mixed Case")));

	// The transform is part of what the layout is keyed on, or changing it would draw the old case.
	FDreamTextLayoutInput Changed = None;
	Changed.TextTransform = EDreamUITextTransformPolicy::ToUpper;
	TestTrue(TEXT("a different transform is a different layout input"), Changed != None);

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextTransformFollowsUnicodeCaseMappingTest,
	"DreamGUI.Text.Pipeline.ACaseTransformFollowsUnicodeSoSharpSBecomesTwoCapitals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Upper and lower case were FString's own, which maps one code unit to one code unit: the German word for street, written
 * with a sharp s, kept the sharp s in upper case, there being no single capital for it, and a Greek word in lower case ended
 * in the sigma that belongs inside a word. UMG transforms through ICU in the current culture, which applies Unicode's full
 * case mapping. Here the German word is put in upper case and a Greek one in lower case, and what the layout is given is
 * compared code unit for code unit.
 */
bool FDreamTextTransformFollowsUnicodeCaseMappingTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	FTextInPanelFixture Fixture;
	if (!TestTrue(TEXT("The text came up"), Fixture.Build(TestWorld.World)))
	{
		return false;
	}

	// The sharp s has no capital of its own and becomes SS.
	Fixture.Text->SetText(FText::FromString(TEXT("stra\u00DFe")));
	Fixture.Text->SetTextTransform(EDreamUITextTransformPolicy::ToUpper);
	TestEqualSensitive(TEXT("the German word for street in upper case is STRASSE"),
		UDreamText::MakeLayoutInput(Fixture.Text, Fixture.Text->GetFontSize()).Content, FString(TEXT("STRASSE")));

	// The capital sigma that ends a word lowers to the final form, and the one that begins it to the other form.
	Fixture.Text->SetText(FText::FromString(TEXT("\u03A3\u039F\u03A6\u039F\u03A3")));
	Fixture.Text->SetTextTransform(EDreamUITextTransformPolicy::ToLower);
	TestEqualSensitive(TEXT("a Greek word in lower case ends in a final sigma and begins with the other"),
		UDreamText::MakeLayoutInput(Fixture.Text, Fixture.Text->GetFontSize()).Content, FString(TEXT("\u03C3\u03BF\u03C6\u03BF\u03C2")));
	TestEqualSensitive(TEXT("while the Text property keeps what was written"),
		Fixture.Text->GetText().ToString(), FString(TEXT("\u03A3\u039F\u03A6\u039F\u03A3")));

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextPaintOnlySettersCostNoLayoutTest,
	"DreamGUI.Text.Pipeline.FillGlowAndStyleChangesRepaintTheTextWithoutLayingItOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The fill progress, the fill segments, the glow boost and the text style are read by the painter alone, yet their setters
 * marked the text through its own MarkVerticesDirty, which threw the layout away on any change of position or UV. A lyric
 * line sets its fill segments every frame of a word and asks for its height every frame, so each frame of singing parsed,
 * shaped and broke the line again -- and with Best Fit on searched for its size again too, since a layout thrown away
 * forgets its Best Fit answer. Here each setter is called on a laid-out text with Best Fit on, and the text is measured
 * after each one; a font size change, which the layout does read, still costs a layout.
 */
bool FDreamTextPaintOnlySettersCostNoLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	FTextOnCanvasFixture Fixture;
	if (!TestTrue(TEXT("The text is on a canvas"),
		Fixture.Build(TestWorld.World, NewObject<UDreamTextTestFont>(TestWorld.World), FVector2D(160.0, 60.0))))
	{
		return false;
	}
	UDreamText* Lyric = Fixture.Text;
	Lyric->SetText(FText::FromString(TEXT("a lyric line long enough to wrap")));
	Lyric->SetFontSize(24.0f);
	Lyric->SetBestFit(true);
	Lyric->GetPreferredHeight();
	const int32 Settled = Lyric->GetCacheTextGeometryData().GetLayoutRunCount();
	if (!TestTrue(TEXT("The text laid out, Best Fit searching for its size"), Settled > 1))
	{
		return false;
	}

	FDreamTextFillSegment Word;
	Word.StartCharIndex = 0;
	Word.EndCharIndex = 4;
	Word.Progress = 0.5f;
	Word.GlowBoost = 0.75f;
	const TArray<FDreamTextFillSegment> Segments = { Word };
	FDreamTextStyle Outlined;
	Outlined.OutlineColor = FColor::Black;
	Outlined.OutlineWidth = 0.1f;
	Outlined.GlowColor = FColor(255, 255, 255, 128);
	Outlined.GlowWidth = 0.2f;

	struct FCase { const TCHAR* Name; TFunction<void(UDreamText*)> Apply; };
	const TArray<FCase> Cases = {
		{ TEXT("SetFillSegments"), [Segments](UDreamText* T) { T->SetFillSegments(Segments); } },
		{ TEXT("SetFillProgress"), [](UDreamText* T) { T->SetFillProgress(0.25f); } },
		{ TEXT("SetGlowBoost"), [](UDreamText* T) { T->SetGlowBoost(1.0f); } },
		{ TEXT("SetTextStyle"), [Outlined](UDreamText* T) { T->SetTextStyle(Outlined); } },
		{ TEXT("ClearFillSegments"), [](UDreamText* T) { T->ClearFillSegments(); } },
	};
	for (const FCase& Case : Cases)
	{
		Case.Apply(Lyric);
		Lyric->GetPreferredHeight();
		TestEqual(*FString::Printf(TEXT("%s repaints without laying the text out"), Case.Name),
			Lyric->GetCacheTextGeometryData().GetLayoutRunCount(), Settled);
	}

	Lyric->SetFontSize(20.0f);
	Lyric->GetPreferredHeight();
	TestTrue(TEXT("A font size change, which the layout reads, still lays the text out"),
		Lyric->GetCacheTextGeometryData().GetLayoutRunCount() > Settled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextAnimationFramesCostNoLayoutTest,
	"DreamGUI.Text.Pipeline.ATextAnimationPlayingRepaintsTheTextWithoutLayingItOut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A TextAnimation plays by moving its selector's offset, a little every frame, and each move marked the text's vertices
 * dirty -- which, through the text's own MarkVerticesDirty, also threw its layout away. An animated label was parsed,
 * shaped and broken into lines again on every frame it moved, though nothing a layout reads had changed. Here a
 * TextAnimation lifts the glyphs of a text on a canvas, its selector offset swept across ten frames: the glyphs move on
 * every frame, and no layout runs for any of them.
 */
bool FDreamTextAnimationFramesCostNoLayoutTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	FScopedSynchronousGlyphs SynchronousGlyphs;
	FTextOnCanvasFixture Fixture;
	if (!TestTrue(TEXT("The text is on a canvas"),
		Fixture.Build(TestWorld.World, MakeEngineFont<UDreamUIFontData_DistanceField>(TestWorld.World, TEXT("Roboto-Regular.ttf")), FVector2D(400.0, 80.0))))
	{
		return false;
	}
	UDreamText* Text = Fixture.Text;
	Text->SetFontSize(32.0f);
	Text->SetText(FText::FromString(TEXT("animated")));

	UDreamMeshModifierTextAnimation* Animation = Fixture.Child->AddComponent<UDreamMeshModifierTextAnimation>();
	if (!TestNotNull(TEXT("The text took a TextAnimation"), Animation))
	{
		return false;
	}
	// A range as wide as the text, so some glyph is part way up at every step of the sweep.
	UDreamMeshModifierTextAnimation_RangeSelector* Sweep = NewObject<UDreamMeshModifierTextAnimation_RangeSelector>(Animation);
	Sweep->SetRange(1.0f);
	Animation->SetSelector(Sweep);
	UDreamMeshModifierTextAnimation_PositionProperty* Lift = NewObject<UDreamMeshModifierTextAnimation_PositionProperty>(Animation);
	Lift->SetPosition(FVector(0.0, 0.0, 20.0));
	TArray<UDreamMeshModifierTextAnimation_Property*> Properties;
	Properties.Add(Lift);
	Animation->SetProperties(Properties);
	TestTrue(TEXT("The TextAnimation told the text it animates the characters one by one"), Text->HasPerCharacterAnimation());

	Animation->SetSelectorOffset(0.0f);
	Manager->TickDreamUI(1.0f / 30.0f);
	Manager->TickDreamUI(1.0f / 30.0f);
	const int32 Settled = Text->GetCacheTextGeometryData().GetLayoutRunCount();
	TArray<FDreamUIOriginVertexData> PreviousFrame = Text->GetGeometry()->OriginVertices;
	if (!TestTrue(TEXT("The text laid out and drew"), Settled > 0 && PreviousFrame.Num() > 0))
	{
		return false;
	}

	for (int32 Frame = 1; Frame <= 10; Frame++)
	{
		Animation->SetSelectorOffset((float)Frame / 10.0f);
		Manager->TickDreamUI(1.0f / 30.0f);
		const TArray<FDreamUIOriginVertexData>& ThisFrame = Text->GetGeometry()->OriginVertices;
		bool bMoved = ThisFrame.Num() != PreviousFrame.Num();
		for (int32 Index = 0; !bMoved && Index < ThisFrame.Num(); Index++)
		{
			bMoved = !ThisFrame[Index].Position.Equals(PreviousFrame[Index].Position, 0.001f);
		}
		TestTrue(*FString::Printf(TEXT("frame %d drew the glyphs where the animation has got to"), Frame), bMoved);
		PreviousFrame = ThisFrame;
	}
	TestEqual(TEXT("and ten frames of animation laid the text out no more than it already was"),
		Text->GetCacheTextGeometryData().GetLayoutRunCount(), Settled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextRefreshTextLayoutLaysOutAgainTest,
	"DreamGUI.RichText.RefreshTextLayoutLaysTheProseOutAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * RefreshTextLayout is how a rich text block picks up a style asset edited in place: the markup is resolved while it is
 * parsed, and nothing the paragraph is keyed on has changed. It pushed the same prose through SetText, which ignores a
 * text equal to the one it has, so the refresh did nothing at all. Here the block is laid out and measured twice, which
 * costs one layout, then refreshed and measured, which has to cost a second.
 */
bool FDreamTextRefreshTextLayoutLaysOutAgainTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(FIntPoint(1280, 720));
	Rig.BindTest(this);
	UDreamRichTextBlock* Block = Rig.MakeControl<UDreamRichTextBlock>(TEXT("Prose"), nullptr, FVector2D(400.0, 120.0));
	UDreamText* Paragraph = (Block != nullptr && Block->TextNode != nullptr) ? Cast<UDreamText>(Block->TextNode->GetVisual()) : nullptr;
	if (!TestTrue(TEXT("The rig and the block came up"), Rig.IsUsable() && Paragraph != nullptr))
	{
		return false;
	}
	// No frame is let through: the paragraph is measured, never drawn, so the made-up font needs no atlas.
	Paragraph->SetFont(NewObject<UDreamTextTestFont>(Rig.GetWorld()));
	Block->SetText(FText::FromString(TEXT("plain <b>bold</b> plain")));

	Paragraph->GetPreferredWidth();
	const int32 Laid = Paragraph->GetCacheTextGeometryData().GetLayoutRunCount();
	if (!TestTrue(TEXT("The prose laid out"), Laid > 0))
	{
		return false;
	}
	Paragraph->GetPreferredWidth();
	TestEqual(TEXT("Asked again with nothing changed, it lays nothing out"), Paragraph->GetCacheTextGeometryData().GetLayoutRunCount(), Laid);

	Block->RefreshTextLayout();
	Paragraph->GetPreferredWidth();
	TestEqual(TEXT("A refresh lays the prose out once more"), Paragraph->GetCacheTextGeometryData().GetLayoutRunCount(), Laid + 1);
	TestEqual(TEXT("And leaves it as it was"), Paragraph->GetText().ToString(), FString(TEXT("plain <b>bold</b> plain")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCaretHitTestSearchesTheWholeLineTest,
	"DreamGUI.Text.Caret.AClickFindsTheNearestCaretWhereverTheCaretsOfTheLineRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * FindCaretByWorldPosition walked a line's carets in the order of the text and stopped at the first one further from the
 * point than the one before it. That order is not left to right on a line that mixes directions, where the carets of a
 * right-to-left word run back across it, nor where negative letter spacing overlaps the glyphs: a click at the end of
 * "ab" + an Arabic word + " cd" came back inside the Arabic word, and with a letter spacing of -5 a click at the end of
 * "a b" came back before the space. Here a point exactly on the line's last caret has to find that caret, on each line.
 */
bool FDreamTextCaretHitTestSearchesTheWholeLineTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	FScopedSynchronousGlyphs SynchronousGlyphs;

	auto ExpectLastCaretFoundAtItsOwnPosition = [this](UDreamText* InText, const TCHAR* InWhat)
	{
		const TArray<FDreamUITextLineProperty>& Lines = InText->GetCacheTextGeometryData().GetLines();
		if (!TestEqual(*FString::Printf(TEXT("%s: one line"), InWhat), Lines.Num(), 1) || Lines[0].CaretPropertyList.Num() < 2)
		{
			return;
		}
		// Without this the walk in text order would have had nothing to stop early on.
		TestTrue(*FString::Printf(TEXT("%s: the carets, in the order of the text, step back to the left somewhere"), InWhat), CaretsStepBack(Lines[0]));
		const FVector2f LastCaret = Lines[0].CaretPropertyList.Last().CaretPosition;
		FVector2f FoundPosition(0.0f, 0.0f);
		int32 FoundLine = INDEX_NONE;
		int32 FoundIndex = INDEX_NONE;
		InText->FindCaretByWorldPosition(CaretWorldPoint(InText, LastCaret), FoundPosition, FoundLine, FoundIndex);
		TestEqual(*FString::Printf(TEXT("%s: a point on the last caret finds the last caret"), InWhat), FoundIndex, InText->GetLastCaret());
		TestEqual(*FString::Printf(TEXT("%s: on its line"), InWhat), FoundLine, 0);
		TestEqual(*FString::Printf(TEXT("%s: and says it stands where it was asked about"), InWhat), FoundPosition.X, LastCaret.X, 0.01f);
	};

	// Both directions on one line, the Arabic through a fallback face.
	UDreamUIFontData_DistanceField* Latin = MakeEngineFont<UDreamUIFontData_DistanceField>(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Arabic = MakeEngineFont<UDreamUIFontData_DistanceField>(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	Latin->SetFallbackFonts({ Arabic });
	FTextOnCanvasFixture Mixed;
	if (!TestTrue(TEXT("The mixed-direction text is on a canvas"), Mixed.Build(TestWorld.World, Latin, FVector2D(600.0, 80.0))))
	{
		return false;
	}
	Mixed.Text->SetText(FText::FromString(TEXT("ab \u0645\u0631\u062D\u0628\u0627 cd")));
	ExpectLastCaretFoundAtItsOwnPosition(Mixed.Text, TEXT("mixed directions"));

	// Glyphs that overlap: a letter spacing of -5 takes the space's whole advance and more, so the pen goes back.
	FTextOnCanvasFixture Overlapping;
	if (!TestTrue(TEXT("The tightly spaced text is on a canvas"),
		Overlapping.Build(TestWorld.World, NewObject<UDreamTextTestFont>(TestWorld.World), FVector2D(200.0, 40.0))))
	{
		return false;
	}
	Overlapping.Text->SetFontSize(10.0f);
	Overlapping.Text->SetFontSpace(FVector2D(-5.0, 0.0));
	Overlapping.Text->SetText(FText::FromString(TEXT("a b")));
	ExpectLastCaretFoundAtItsOwnPosition(Overlapping.Text, TEXT("negative letter spacing"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextCaretMovesDownToTheNearestCaretTest,
	"DreamGUI.Text.Caret.DownLandsOnTheNearestCaretOfTheNextLineWhereverItsCaretsRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Up and Down keep the caret's x and look for the nearest caret on the line they move to, and that search stopped, as the
 * click's did, at the first caret further away than the one before it -- which, where negative letter spacing makes the
 * glyphs overlap, is not past the nearest one. Here two lines of "a b" at a letter spacing of -5 stand one above the other,
 * and Down from the end of the first has to land on the end of the second, straight below it.
 */
bool FDreamTextCaretMovesDownToTheNearestCaretTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	FTextOnCanvasFixture Fixture;
	if (!TestTrue(TEXT("The text is on a canvas"),
		Fixture.Build(TestWorld.World, NewObject<UDreamTextTestFont>(TestWorld.World), FVector2D(200.0, 80.0))))
	{
		return false;
	}
	UDreamText* Text = Fixture.Text;
	Text->SetFontSize(10.0f);
	Text->SetFontSpace(FVector2D(-5.0, 0.0));
	Text->SetText(FText::FromString(TEXT("a b\na b")));
	const TArray<FDreamUITextLineProperty>& Lines = Text->GetCacheTextGeometryData().GetLines();
	if (!TestEqual(TEXT("Two lines"), Lines.Num(), 2)
		|| !TestTrue(TEXT("whose carets step back to the left somewhere"), CaretsStepBack(Lines[1])))
	{
		return false;
	}
	int32 CaretIndex = Lines[0].CaretPropertyList.Num() - 1;
	int32 LineIndex = 0;
	FVector2f CaretPosition = Lines[0].CaretPropertyList.Last().CaretPosition;
	const float FromX = CaretPosition.X;
	TestTrue(TEXT("Down from the end of the first line moves"), Text->MoveCaret(3, CaretIndex, LineIndex, CaretPosition));
	TestEqual(TEXT("onto the second line"), LineIndex, 1);
	TestEqual(TEXT("onto its last caret, the one straight below"), CaretIndex, Text->GetLastCaret());
	TestEqual(TEXT("which stands where the caret came from"), CaretPosition.X, FromX, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextFontSwitchRewritesTheFontMarkTest,
	"DreamGUI.Text.Font.SwitchingToAnotherKindOfFontRewritesTheFontMarkAndRedrawsTheQuads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The widget property record carries a font mark that tells the shader how to read the atlas -- one channel for a bitmap
 * or a single-channel field, the median of three for MTSDF -- and SetFont never asked for it to be written again: a text
 * switched from a bitmap font to an MTSDF one went on reading the new atlas as one channel, which rounds every corner. Nor
 * did SetFont redraw anything: a new texture alone rebuilds no vertex, so the old font's quads went on sampling the new
 * atlas until something else moved the text. Here a text on a canvas is switched from a bitmap Roboto to an MTSDF Roboto.
 */
bool FDreamTextFontSwitchRewritesTheFontMarkTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	FScopedSynchronousGlyphs SynchronousGlyphs;
	UDreamUIFontData_Bitmap* Bitmap = MakeEngineFont<UDreamUIFontData_Bitmap>(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Field = MakeEngineFont<UDreamUIFontData_DistanceField>(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	if (!TestEqual(TEXT("The bitmap font is marked a bitmap"), (int32)static_cast<UDreamUIFontData_BaseObject*>(Bitmap)->GetFontTextureMark(), (int32)EDreamUIFontTextureMark::Bitmap)
		|| !TestEqual(TEXT("The field font is marked MTSDF"), (int32)static_cast<UDreamUIFontData_BaseObject*>(Field)->GetFontTextureMark(), (int32)EDreamUIFontTextureMark::Mtsdf))
	{
		return false;
	}

	FTextOnCanvasFixture Fixture;
	if (!TestTrue(TEXT("The text is on a canvas"), Fixture.Build(TestWorld.World, Bitmap, FVector2D(300.0, 80.0))))
	{
		return false;
	}
	Fixture.Text->SetFontSize(32.0f);
	Fixture.Text->SetText(FText::FromString(TEXT("AVMWkxz")));
	Manager->TickDreamUI(1.0f / 30.0f);
	Manager->TickDreamUI(1.0f / 30.0f);
	TestEqual(TEXT("Drawn with the bitmap font, the record says bitmap"),
		(int32)Fixture.Text->GetWidgetPropertyFontMark(), (int32)EDreamUIFontTextureMark::Bitmap);
	const TArray<FDreamUIOriginVertexData> BitmapQuads = Fixture.Text->GetGeometry()->OriginVertices;
	if (!TestTrue(TEXT("The bitmap font drew quads"), BitmapQuads.Num() > 0))
	{
		return false;
	}

	Fixture.Text->SetFont(Field);
	Manager->TickDreamUI(1.0f / 30.0f);
	Manager->TickDreamUI(1.0f / 30.0f);
	TestEqual(TEXT("Switched to the MTSDF font, the record says MTSDF"),
		(int32)Fixture.Text->GetWidgetPropertyFontMark(), (int32)EDreamUIFontTextureMark::Mtsdf);
	const TArray<FDreamUIOriginVertexData>& FieldQuads = Fixture.Text->GetGeometry()->OriginVertices;
	bool bSameQuads = FieldQuads.Num() == BitmapQuads.Num();
	for (int32 Index = 0; bSameQuads && Index < FieldQuads.Num(); Index++)
	{
		bSameQuads = FieldQuads[Index].Position.Equals(BitmapQuads[Index].Position, 0.001f);
	}
	TestFalse(TEXT("And the quads were drawn again, for the field font's glyphs"), bSameQuads);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextBidiSelectionIsHighlightedRunByRunTest,
	"DreamGUI.Text.Selection.ASelectionAcrossDirectionsIsHighlightedOneVisualRunAtATime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A selection was drawn as one bar per line, from caret to caret. On a line that mixes directions one stretch of the text
 * is several stretches of the screen: selected from inside "ab" into the Arabic word of "ab" + the word + " cd", it is the
 * end of "ab" and, apart from it, the right end of the word, where the word begins -- the bar covered the left of the word,
 * which was not selected, instead. A highlight is drawn wherever the selection meets one of the layout's visual runs now.
 * Here a selection from inside "ab" to inside "cd" lights every run it crosses, end to end, and one from inside "ab" into
 * the word lights the word's right end, with the unselected rest of the word between it and "ab".
 */
bool FDreamTextBidiSelectionIsHighlightedRunByRunTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	FScopedSynchronousGlyphs SynchronousGlyphs;
	UDreamUIFontData_DistanceField* Latin = MakeEngineFont<UDreamUIFontData_DistanceField>(TestWorld.World, TEXT("Roboto-Regular.ttf"));
	UDreamUIFontData_DistanceField* Arabic = MakeEngineFont<UDreamUIFontData_DistanceField>(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	Latin->SetFallbackFonts({ Arabic });
	FTextOnCanvasFixture Fixture;
	if (!TestTrue(TEXT("The text is on a canvas"), Fixture.Build(TestWorld.World, Latin, FVector2D(600.0, 80.0))))
	{
		return false;
	}
	UDreamText* Text = Fixture.Text;
	// "ab " is offsets 0 to 2, the word 3 to 7, " cd" 8 to 10.
	Text->SetText(FText::FromString(TEXT("ab \u0645\u0631\u062D\u0628\u0627 cd")));
	const TArray<FDreamTextVisualRun> Runs = RunsOfLine(Text, 0);
	const FDreamTextVisualRun* Word = Runs.FindByPredicate([](const FDreamTextVisualRun& Run) { return Run.bRightToLeft; });
	if (!TestTrue(TEXT("The line is at least three runs"), Runs.Num() >= 3) || !TestNotNull(TEXT("One of them reads right to left"), Word))
	{
		return false;
	}
	auto Select = [Text](int32 InFrom, int32 InTo)
	{
		TArray<FDreamUITextSelectionProperty> Highlights;
		Text->GetSelectionProperty(Text->GetCaretIndexByCharIndex(InFrom), Text->GetCaretIndexByCharIndex(InTo), Highlights);
		return Highlights;
	};
	auto RunsCrossed = [&Runs](int32 InFrom, int32 InTo)
	{
		return Runs.FilterByPredicate([InFrom, InTo](const FDreamTextVisualRun& Run) { return Run.SourceStart < InTo && Run.SourceEnd > InFrom; }).Num();
	};

	const TArray<FDreamUITextSelectionProperty> Across = Select(1, 10);
	TestEqual(TEXT("From inside \"ab\" to inside \"cd\": one highlight per run crossed"), Across.Num(), RunsCrossed(1, 10));
	TestTrue(TEXT("which is at least three"), Across.Num() >= 3);
	for (int32 Index = 0; Index < Across.Num(); Index++)
	{
		TestTrue(*FString::Printf(TEXT("highlight %d is not negative"), Index), Across[Index].Size >= 0);
		if (Index > 0)
		{
			TestEqual(*FString::Printf(TEXT("highlight %d starts where the one before it ends"), Index),
				Across[Index].Pos.X, Across[Index - 1].Pos.X + (float)Across[Index - 1].Size, 1.0f);
		}
	}

	// Into the word: its first two letters, which a right-to-left word draws at its right end.
	const TArray<FDreamUITextSelectionProperty> Into = Select(1, 5);
	TestEqual(TEXT("From inside \"ab\" into the word: one highlight per run crossed"), Into.Num(), RunsCrossed(1, 5));
	if (TestTrue(TEXT("which is at least two"), Into.Num() >= 2))
	{
		const FDreamUITextSelectionProperty& InWord = Into.Last();
		TestEqual(TEXT("The word's highlight ends at the word's right edge, where the word begins"),
			InWord.Pos.X + (float)InWord.Size, Word->Right, 1.0f);
		float LatinRight = -MAX_FLT;
		for (int32 Index = 0; Index + 1 < Into.Num(); Index++)
		{
			LatinRight = FMath::Max(LatinRight, Into[Index].Pos.X + (float)Into[Index].Size);
		}
		TestTrue(TEXT("And the unselected rest of the word lies between it and the highlight on \"ab\""), InWord.Pos.X > LatinRight + 1.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextRightToLeftSelectionOverLinesTest,
	"DreamGUI.Text.Selection.ARightToLeftSelectionOverTwoLinesHasNoNegativeWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A selection over several lines measured its first line from the selection's start caret to the line's last caret, and a
 * middle line from its first caret to its last. On a right-to-left line the last caret is the left end, so those widths
 * came out negative and the highlight's widget clamped them to nothing. Here a right-to-left paragraph wraps onto two lines
 * and is selected from the middle of the first into the middle of the second: both lines are lit, none negatively.
 */
bool FDreamTextRightToLeftSelectionOverLinesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	FScopedSynchronousGlyphs SynchronousGlyphs;
	UDreamUIFontData_DistanceField* Arabic = MakeEngineFont<UDreamUIFontData_DistanceField>(TestWorld.World, TEXT("NotoNaskhArabicUI-Regular.ttf"));
	FTextOnCanvasFixture Fixture;
	if (!TestTrue(TEXT("The text is on a canvas"), Fixture.Build(TestWorld.World, Arabic, FVector2D(150.0, 200.0))))
	{
		return false;
	}
	UDreamText* Text = Fixture.Text;
	Text->SetFontSize(24.0f);
	// Wrapping at the box, which is the default overflow.
	Text->SetText(FText::FromString(TEXT("\u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645 \u0645\u0631\u062D\u0628\u0627 \u0628\u0627\u0644\u0639\u0627\u0644\u0645")));
	const TArray<FDreamUITextLineProperty>& Lines = Text->GetCacheTextGeometryData().GetLines();
	if (!TestTrue(TEXT("The paragraph wraps onto two lines or more"), Lines.Num() >= 2))
	{
		return false;
	}
	const int32 FromCaret = Lines[0].CaretPropertyList.Num() / 2;
	const int32 ToCaret = Lines[0].CaretPropertyList.Num() + Lines[1].CaretPropertyList.Num() / 2;
	TArray<FDreamUITextSelectionProperty> Highlights;
	Text->GetSelectionProperty(FromCaret, ToCaret, Highlights);
	TSet<int32> LinesLit;
	for (int32 Index = 0; Index < Highlights.Num(); Index++)
	{
		TestTrue(*FString::Printf(TEXT("highlight %d is not negative"), Index), Highlights[Index].Size >= 0);
		LinesLit.Add(FMath::RoundToInt(Highlights[Index].Pos.Y));
	}
	TestTrue(TEXT("Both lines are lit"), LinesLit.Num() >= 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextLigaturesYieldToPerCharacterAnimationTest,
	"DreamGUI.Text.Pipeline.LigaturesAreAllowedUnlessSomethingAnimatesTheCharactersOneByOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Ligatures are on by default, as in browsers and Slate, and a ligature draws several characters as one glyph -- which an
 * animator moving each character's glyph by its index cannot work with, since it would find fewer glyphs than characters.
 * So whatever animates the characters registers with the text, and the layout is asked for no ligatures while anything is
 * registered. Here the layout input is read with nothing registered, while TextAnimation's stand-in is registered, once
 * it has unregistered, and with the property turned off.
 */
bool FDreamTextLigaturesYieldToPerCharacterAnimationTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	FScopedTestWorld TestWorld;
	FTextInPanelFixture Fixture;
	if (!TestTrue(TEXT("The text came up"), Fixture.Build(TestWorld.World)))
	{
		return false;
	}
	UDreamText* Text = Fixture.Text;
	Text->SetText(FText::FromString(TEXT("office affluence")));
	auto AllowsLigatures = [Text]() { return UDreamText::MakeLayoutInput(Text, Text->GetFontSize()).bAllowLigatures; };

	TestTrue(TEXT("Ligatures are on by default"), Text->GetLigatures());
	TestTrue(TEXT("and the layout is allowed them"), AllowsLigatures());

	// What TextAnimation registers as while it animates this text.
	UDreamMeshModifierTextAnimation* Animator = NewObject<UDreamMeshModifierTextAnimation>(TestWorld.World);
	Text->RegisterPerCharacterAnimation(Animator);
	Text->RegisterPerCharacterAnimation(Animator);
	TestTrue(TEXT("Something animates the characters"), Text->HasPerCharacterAnimation());
	TestFalse(TEXT("so the layout is asked for none"), AllowsLigatures());

	Text->UnregisterPerCharacterAnimation(Animator);
	TestFalse(TEXT("Registered twice and unregistered once, it is gone"), Text->HasPerCharacterAnimation());
	TestTrue(TEXT("and the layout is allowed ligatures again"), AllowsLigatures());

	Text->SetLigatures(false);
	TestFalse(TEXT("Turned off, the layout is allowed none"), AllowsLigatures());

	Fixture.Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextTagColourOverrideStaysWithItsTagTest,
	"DreamGUI.Text.Pipeline.ATagColourOverrideGoesWhenALayoutPutsAnotherTagAtItsIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A tag colour override names its tag by index, and it outlived edits of the text: an edit that put another tag at that
 * index had the painter colour a tag nobody chose. Here the first of two tags is coloured. Text added after both leaves
 * that tag where it was, and the colour stays; a tag put in front of it takes its index, and the colour goes.
 */
bool FDreamTextTagColourOverrideStaysWithItsTagTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	FTextOnCanvasFixture Fixture;
	if (!TestTrue(TEXT("The text is on a canvas"),
		Fixture.Build(TestWorld.World, NewObject<UDreamTextTestFont>(TestWorld.World), FVector2D(400.0, 80.0))))
	{
		return false;
	}
	UDreamText* Text = Fixture.Text;
	Text->SetFontSize(10.0f);
	Text->SetRichText(true);
	Text->SetText(FText::FromString(TEXT("<one>a</one> <two>b</two>")));
	if (!TestEqual(TEXT("The text has its two tags"), Text->GetRichTextCustomTagArray().Num(), 2))
	{
		return false;
	}
	const FColor Colour(255, 64, 0, 255);
	FColor Shown = FColor::White;
	Text->SetTagColorOverride(0, Colour);
	TestTrue(TEXT("The first tag is coloured"), Text->GetTagColorOverride(0, Shown) && Shown == Colour);

	Text->SetText(FText::FromString(TEXT("<one>a</one> <two>b</two> c")));
	TestTrue(TEXT("Text added after both tags leaves the first where it was, and coloured"),
		Text->GetTagColorOverride(0, Shown) && Shown == Colour);

	Text->SetText(FText::FromString(TEXT("<zero>z</zero> <one>a</one> <two>b</two> c")));
	const TArray<FDreamUIText_RichTextCustomTag>& Tags = Text->GetRichTextCustomTagArray();
	if (!TestTrue(TEXT("A tag put in front is the first now"), Tags.Num() == 3 && Tags[0].TagName == FName(TEXT("zero"))))
	{
		return false;
	}
	TestFalse(TEXT("The tag that took the index is not coloured"), Text->GetTagColorOverride(0, Shown));
	TestEqual(TEXT("Nor is any other: the override went with the layout that moved its tag"), Text->GetTagColorOverrides().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAutoWrapTextInAFillSlotSettlesTest,
	"DreamGUI.Layout.Convergence.AnAutoWrapTextInAFillSlotOfARowSettlesAtTheHeightOfItsLinesAtItsShare",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A text that wraps at its box is as tall as its lines are at the width it is given, and in a Fill slot of a horizontal box
 * that width is its share of the row, which it only gets when the row arranges it. Here an AutoWrapText paragraph starts ten
 * wide, narrower than any of its words, in a row under a vertical box beside a 100-wide icon. After one tick it has the 500
 * the icon leaves and is as tall as its lines are at 500 -- more than one of them, and taller than the icon, so the row is
 * as tall as the text -- and the next tick moves nothing.
 */
bool FDreamAutoWrapTextInAFillSlotSettlesTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeSizedWidget(TestWorld.World, nullptr, TEXT("Root"), 600.0f, 400.0f);
	Root->AddComponent<UDreamCanvas>();
	UDreamWidget* Row = MakeSizedWidget(TestWorld.World, Root, TEXT("Row"), 50.0f, 20.0f);
	MakeSizedWidget(TestWorld.World, Row, TEXT("Icon"), 100.0f, 40.0f);
	UDreamWidget* Label = MakeSizedWidget(TestWorld.World, Row, TEXT("Label"), 10.0f, 40.0f);
	UDreamText* Text = MakeAutoWrapText(TestWorld.World, Label);
	if (!TestNotNull(TEXT("The label has its text"), Text)
		|| !TestNotNull(TEXT("Row created"), Row->CreateNewLayoutContainer<UDreamLayoutContainerHorizontalBox>())
		|| !TestNotNull(TEXT("Column created"), Root->CreateNewLayoutContainer<UDreamLayoutContainerVerticalBox>()))
	{
		Root->DestroyWidget();
		return false;
	}
	UDreamPanelSlot* LabelSlot = Label->GetPanelSlot();
	if (!TestNotNull(TEXT("The row handed the label a slot"), LabelSlot))
	{
		Root->DestroyWidget();
		return false;
	}
	LabelSlot->SetSizeRule(EDreamPanelSizeRule::Fill);

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);

	TestEqual(TEXT("The label has the share of the row the icon leaves"), Label->GetWidth(), 500.0f);
	// Asked now, the text lays itself out at the width it has: the lines it is drawn with.
	const float LinesHeight = Text->GetPreferredHeight();
	const int32 LineCount = Text->GetCacheTextGeometryData().GetLines().Num();
	TestTrue(FString::Printf(TEXT("...on which the paragraph takes more than one line (%d)"), LineCount), LineCount > 1);
	TestTrue(FString::Printf(TEXT("...and the label is as tall as those lines (%.1f, expected %.1f)"), Label->GetHeight(), LinesHeight),
		FMath::IsNearlyEqual(Label->GetHeight(), LinesHeight, 0.5f));
	TestTrue(FString::Printf(TEXT("The row is as tall as the label, which is taller than the icon (%.1f, %.1f)"), Row->GetHeight(), Label->GetHeight()),
		Label->GetHeight() > 40.0f && FMath::IsNearlyEqual(Row->GetHeight(), Label->GetHeight(), 0.5f));

	Manager->TickDreamUI(0.016f);
	TestTrue(FString::Printf(TEXT("The next tick does not move it (%.1f)"), Label->GetHeight()),
		FMath::IsNearlyEqual(Label->GetHeight(), LinesHeight, 0.5f));

	Root->DestroyWidget();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAutoWrapTextIsMeasuredAgainOnceItsGridNarrowsTest,
	"DreamGUI.Layout.Convergence.AnAutoWrapTextMeasuredAtItsOwnWidthIsMeasuredAgainOnceItsGridHasNarrowedIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A grid asks its children how big they want to be with no width to go by, so a text that wraps at its box answers with its
 * lines at the width it already has, which the previous pass gave it. Narrowed from 600 to 300, the grid sized the row for
 * the lines at 600, gave the text the 300-wide column, and asked nothing again: the text ran to more lines than its row had
 * room for. The text now tells the arranging panel that its answer came from its own width, and the grid measures it once
 * more after resizing it. This checks that after one tick at 300 the text is as tall as its lines there, more of them than
 * at 600, in two passes, and that the next tick moves nothing.
 */
bool FDreamAutoWrapTextIsMeasuredAgainOnceItsGridNarrowsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTextLayoutInvalidationTestLocal;
	DreamTests::FScopedGameWorld TestWorld;
	UDreamUIManagerWorldSubsystem* Manager = UDreamUIManagerWorldSubsystem::GetInstance(TestWorld.World);
	if (!TestNotNull(TEXT("DreamUI manager subsystem exists"), Manager))
	{
		return false;
	}
	UDreamWidget* Root = MakeSizedWidget(TestWorld.World, nullptr, TEXT("Root"), 600.0f, 400.0f);
	Root->AddComponent<UDreamCanvas>();
	UDreamWidget* Label = MakeSizedWidget(TestWorld.World, Root, TEXT("Label"), 10.0f, 40.0f);
	UDreamText* Text = MakeAutoWrapText(TestWorld.World, Label);
	UDreamLayoutContainerGridPanel* Grid = Root->CreateNewLayoutContainer<UDreamLayoutContainerGridPanel>();
	if (!TestNotNull(TEXT("The label has its text"), Text) || !TestNotNull(TEXT("Grid created"), Grid))
	{
		Root->DestroyWidget();
		return false;
	}
	Grid->SetColumnFill({ 1.0f });

	UDreamWidget::MarkLayoutForRebuild(Root);
	Manager->TickDreamUI(0.016f);
	Manager->TickDreamUI(0.016f);
	const float WideHeight = Text->GetPreferredHeight();
	const int32 WideLineCount = Text->GetCacheTextGeometryData().GetLines().Num();
	TestEqual(TEXT("At 600 the text fills the column"), Label->GetWidth(), 600.0f);
	TestTrue(FString::Printf(TEXT("...and is as tall as its lines there (%.1f, expected %.1f)"), Label->GetHeight(), WideHeight),
		FMath::IsNearlyEqual(Label->GetHeight(), WideHeight, 0.5f));

	Root->SetWidth(300.0f);
	Manager->TickDreamUI(0.016f);
	const float NarrowHeight = Text->GetPreferredHeight();
	const int32 NarrowLineCount = Text->GetCacheTextGeometryData().GetLines().Num();
	TestEqual(TEXT("At 300 the text has the narrowed column"), Label->GetWidth(), 300.0f);
	TestTrue(FString::Printf(TEXT("...on which it takes more lines than at 600 (%d, %d)"), NarrowLineCount, WideLineCount),
		NarrowLineCount > WideLineCount);
	TestTrue(FString::Printf(TEXT("...and the row opens to them (%.1f, expected %.1f)"), Label->GetHeight(), NarrowHeight),
		FMath::IsNearlyEqual(Label->GetHeight(), NarrowHeight, 0.5f));
	TestEqual(TEXT("...for one pass more than the resize itself, not until the pass cap"), Manager->GetLastLayoutPassCount(), 2);

	Manager->TickDreamUI(0.016f);
	TestTrue(FString::Printf(TEXT("The next tick does not move it (%.1f)"), Label->GetHeight()),
		FMath::IsNearlyEqual(Label->GetHeight(), NarrowHeight, 0.5f));

	Root->DestroyWidget();
	return true;
}

#endif
