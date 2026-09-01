// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUISpriteData_BaseObject.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamWidget.h"
#include "Engine/Texture2D.h"
#include "Extensions/DreamImageSequencePlayer.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "Interaction/UIStandardControls.h"
#include "UObject/Package.h"

/*
 * The frame-by-frame players, and the legacy progress bar behaviour, neither of which has ever had
 * a test.
 *
 * All three sequence classes are one number turned into a picture: elapsed time times a frame rate,
 * floored to an integer, looked up in a list or a grid. Every interesting failure is therefore an
 * arithmetic edge -- an empty list, a rate of zero, a frame one past the end, a clock that went
 * backwards -- and every one of them produces a WRONG PICTURE rather than a crash, which is exactly
 * the class of bug that ships.
 *
 * What is reachable from here and what is not:
 *
 *   - Play, Stop, Pause, SeekFrame, SeekTime and the counts are all public, and the tween manager
 *     returns null without a game instance, so Play starts the animation and simply never receives
 *     a tick. That is enough to drive every frame decision by hand.
 *   - GetDuration is public where its UFUNCTION is declared, on UDreamImageSequencePlayer, and both
 *     subclasses re-declare their override under protected -- so it is reachable through the base
 *     and not through the concrete type. DurationOf below is that one line, named rather than
 *     scattered.
 *   - UpdateAnimation -- the ONLY place the loop wrap and the stop-at-the-end live -- is protected,
 *     and its one public caller is Play, which always passes a delta of zero. So the wrap itself is
 *     not covered here. What IS covered is that Seek does not consult it: seeking past the end does
 *     not wrap even with looping on, which is a real difference in behaviour between the two ways
 *     of moving the playhead and is pinned below.
 *
 * Five defects were found while first reading this code and pinned here as they stood, with the
 * reason. All five are now fixed, and the assertions below say what the right answer is:
 *
 *   - UUISpriteSheetTexturePlayer::OnUpdateAnimation clamped the row to HeightCount and the column
 *     to WidthCount, one too many each. The column is already inside range from the modulo, so it
 *     never bit; the row is FrameNumber / WidthCount and did reach HeightCount, which put the
 *     cell's V origin at exactly 1.0 -- one whole sheet below the last row. Frame WidthCount *
 *     HeightCount is reachable both by seeking and by the tick path, whose wrap only fires when
 *     ElapsedTime is STRICTLY greater than Duration. Both clamps now take the last index.
 *   - WidthUVInterval and HeightUVInterval were plain float members with no initialiser, written
 *     only in PrepareForPlay and read by OnUpdateAnimation. SeekFrame and SeekTime reached the read
 *     without the write, so seeking before the first Play produced a cell with no size. Seeking now
 *     prepares the same way playing does, which is the honest reading of what a seek is: put the
 *     playhead here and draw, and drawing needs the state a draw needs.
 *   - Nothing clamped Fps. Zero made GetDuration infinite (or NaN for an empty sequence), and
 *     Duration is the only thing the loop and the stop compare against, so a zero frame rate turned
 *     both into "never" rather than into an error anyone could see; SeekFrame divided by it
 *     directly and wrote a NaN into the clock that every later += preserved. It is now held above
 *     zero wherever it is used, not only in the setter, because assets predate the metadata.
 *   - UUISpriteSequencePlayer::SetSpriteSequence replaced the array without stopping the player and
 *     without re-checking it. Emptying it while playing left OnUpdateAnimation indexing an empty
 *     array -- FMath::Clamp(N, 0, -1) returns 0, not -1, so it was element zero of nothing. That
 *     one was a crash, reported rather than exercised; it is now exercised.
 *   - UUIProgressBar::ApplyProgress wrote one axis and never reset the other, so switching FillType
 *     between a horizontal and a vertical direction at runtime left the previous axis squeezed
 *     where it was and the two spans multiplied. FillType also had no setter at all despite being
 *     BlueprintReadWrite, which is why the direction test used to change it through reflection.
 */

namespace DreamSequencePlayerTestLocal
{
	/** A worldless authoring tree with one widget, which is the state the designer builds in. */
	struct FPlayerFixture
	{
		UDreamWidgetTree* Tree = nullptr;
		UDreamWidget* Widget = nullptr;

		FPlayerFixture()
		{
			Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
			Widget = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Player"));
			if (Widget != nullptr)
			{
				Tree->RootWidget = Widget;
				Widget->SetWidth(64.0f);
				Widget->SetHeight(64.0f);
			}
		}

		void Teardown()
		{
			if (Widget != nullptr)
			{
				Widget->DestroyWidget();
				Widget = nullptr;
			}
		}
	};

	/** An array of the right length holding nothing: the players only ever count it until a frame is drawn. */
	TArray<UDreamUISpriteData_BaseObject*> EmptySlots(int32 InCount)
	{
		TArray<UDreamUISpriteData_BaseObject*> Result;
		Result.SetNumZeroed(InCount);
		return Result;
	}

	/**
	 * The length is asked of the base class on purpose. GetDuration's UFUNCTION is declared public
	 * on UDreamImageSequencePlayer -- that is the declaration Blueprint calls and the only one that
	 * exists as a callable -- while both subclasses re-declare their override under protected. So
	 * the base is where the public contract lives, and going through it is that contract rather
	 * than a way around a visibility rule.
	 */
	float DurationOf(UDreamImageSequencePlayer* InPlayer)
	{
		return InPlayer->GetDuration();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSequencePlayerDurationTest,
	"DreamGUI.Extensions.ASequencePlayersLengthIsItsFrameCountOverItsFrameRate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSequencePlayerDurationTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	// Duration is not decoration: it is the number the tick path compares ElapsedTime against to
	// decide whether to wrap or to stop. Both players compute it the same way from different
	// sources, and both are asked here because the two implementations are independent copies of
	// one idea.
	FPlayerFixture Fixture;
	if (!TestNotNull(TEXT("the widget was constructed"), Fixture.Widget))
	{
		return false;
	}

	UUISpriteSequencePlayer* SpritePlayer = Fixture.Widget->AddComponent<UUISpriteSequencePlayer>();
	UUISpriteSheetTexturePlayer* SheetPlayer = Fixture.Widget->AddComponent<UUISpriteSheetTexturePlayer>();
	if (!TestNotNull(TEXT("the sprite sequence player was added"), SpritePlayer) ||
		!TestNotNull(TEXT("the sprite sheet player was added"), SheetPlayer))
	{
		Fixture.Teardown();
		return false;
	}

	TestEqual(TEXT("a fresh player runs at twenty-four frames a second"), SpritePlayer->GetFps(), 24.0f);
	TestTrue(TEXT("and loops"), SpritePlayer->GetLoop());

	// An empty sequence has no length. That is the honest answer and it is also the dangerous one:
	// a zero duration means the tick path wraps on the very first frame it is handed.
	TestEqual(TEXT("an empty sequence lasts no time at all"), DurationOf(SpritePlayer), 0.0f);

	SpritePlayer->SetSpriteSequence(EmptySlots(6));
	TestEqual(TEXT("the sequence is as long as it was given"),
		SpritePlayer->GetSpriteSequence().Num(), 6);
	TestEqual(TEXT("six frames at twenty-four a second is a quarter of a second"),
		DurationOf(SpritePlayer), 0.25f);

	SpritePlayer->SetFps(12.0f);
	TestEqual(TEXT("halving the rate doubles the length"), DurationOf(SpritePlayer), 0.5f);

	// The sheet counts cells rather than entries, so the same arithmetic runs off a product.
	TestEqual(TEXT("a fresh sheet is eight cells across"), SheetPlayer->GetWidthCount(), 8);
	TestEqual(TEXT("and eight down"), SheetPlayer->GetHeightCount(), 8);
	SheetPlayer->SetWidthCount(4);
	SheetPlayer->SetHeightCount(2);
	SheetPlayer->SetFps(8.0f);
	TestEqual(TEXT("eight cells at eight a second is one second"), DurationOf(SheetPlayer), 1.0f);

	// Neither count setter clamps, which matters because PrepareForPlay divides by both. CanPlay is
	// what stands between a zero count and that divide on the way in -- see the refusal test -- and
	// OnUpdateAnimation refuses to draw with one, which covers the case CanPlay cannot: a count
	// changed to zero while the tween is already running. The counts themselves stay unclamped
	// because "will take effect on next cycle" is their documented contract, and a setter that
	// silently substituted 1 would be a grid nobody asked for.
	SheetPlayer->SetWidthCount(0);
	TestEqual(TEXT("a zero cell count is taken as written"), SheetPlayer->GetWidthCount(), 0);
	SheetPlayer->SetHeightCount(-3);
	TestEqual(TEXT("and so is a negative one"), SheetPlayer->GetHeightCount(), -3);

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSequencePlayerZeroFrameRateTest,
	"DreamGUI.Extensions.AFrameRateIsHeldAboveZeroSoALengthAlwaysExists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSequencePlayerZeroFrameRateTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	FPlayerFixture Fixture;
	if (!TestNotNull(TEXT("the widget was constructed"), Fixture.Widget))
	{
		return false;
	}

	UUISpriteSequencePlayer* SpritePlayer = Fixture.Widget->AddComponent<UUISpriteSequencePlayer>();
	UUISpriteSheetTexturePlayer* SheetPlayer = Fixture.Widget->AddComponent<UUISpriteSheetTexturePlayer>();
	if (!TestNotNull(TEXT("the sprite sequence player was added"), SpritePlayer) ||
		!TestNotNull(TEXT("the sprite sheet player was added"), SheetPlayer))
	{
		Fixture.Teardown();
		return false;
	}

	// Zero is the value an author reaches for when they mean "hold on the first frame", and it is
	// the one value the length arithmetic cannot express: frames divided by no rate is not a
	// duration. SetFps used to take it, and the consequence was entirely silent -- UpdateAnimation
	// wraps when ElapsedTime > Duration and stops when it is not looping, and neither comparison is
	// ever true against an infinity, so a non-looping animation with Fps 0 never ended and never
	// reported that it had not.
	//
	// It is now pulled up to a floor small enough that nothing an author would actually type is
	// affected: the point is to exclude the zero, not to impose a minimum speed. A rate below one
	// frame a second is a legitimate slow crawl and passes through untouched.
	SpritePlayer->SetSpriteSequence(EmptySlots(6));
	SpritePlayer->SetFps(0.0f);
	TestTrue(TEXT("a zero frame rate is refused up to something a clock can divide by"),
		SpritePlayer->GetFps() > 0.0f);
	TestTrue(TEXT("so six frames still have a length"), FMath::IsFinite(DurationOf(SpritePlayer)));

	// The empty sequence used to be worse than an infinity: zero over zero, a NaN that fails EVERY
	// comparison and so took out the stop branch as well as the wrap. With a rate above zero it is
	// simply nothing, which is the honest answer for nothing to play.
	SpritePlayer->SetSpriteSequence(TArray<UDreamUISpriteData_BaseObject*>());
	TestEqual(TEXT("and no frames at all is a length of zero rather than a NaN"),
		DurationOf(SpritePlayer), 0.0f);

	SheetPlayer->SetWidthCount(4);
	SheetPlayer->SetHeightCount(2);
	SheetPlayer->SetFps(0.0f);
	TestTrue(TEXT("the sheet player holds the same floor"), FMath::IsFinite(DurationOf(SheetPlayer)));

	// Negative rates ran the length backwards, which made the wrap fire on the first tick and every
	// tick after it. Refused for the same reason zero is: this design has no notion of playing
	// backwards, since ElapsedTime only ever accumulates.
	SheetPlayer->SetFps(-8.0f);
	TestTrue(TEXT("a negative frame rate is not a slower one, so it is refused too"),
		SheetPlayer->GetFps() > 0.0f);
	TestTrue(TEXT("and the length stays the right way round"), DurationOf(SheetPlayer) > 0.0f);

	// A rate an author might really want is untouched, which is what makes the floor a floor rather
	// than a policy about speed.
	SheetPlayer->SetFps(0.5f);
	TestEqual(TEXT("half a frame a second is a legitimate crawl"), SheetPlayer->GetFps(), 0.5f);
	TestEqual(TEXT("eight cells at half a frame a second is sixteen seconds"),
		DurationOf(SheetPlayer), 16.0f);

	// The sharpest edge of the same defect is on the seek path, and it is pinned in the frame-grid
	// test where a player with a real visual can actually be made to draw: SeekFrame divides the
	// frame number by the rate before it consults anything, so a zero used to write a NaN straight
	// into the clock and every later += preserved it.

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSequencePlayerRefusalTest,
	"DreamGUI.Extensions.ASequencePlayerRefusesToPlayWithoutTheVisualItDrives",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSequencePlayerRefusalTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	// CanPlay is the only guard in front of arithmetic that would otherwise divide by a zero cell
	// count or index an empty array, and it is consulted from Play, SeekFrame and SeekTime alike.
	// Every branch of it says out loud what is missing, because the alternative -- an animation
	// that simply does not move -- is indistinguishable from a paused one.
	AddExpectedError(TEXT("Need UISprite component"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("SpriteSequence array is empty"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("Need DreamTexture"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("DreamTexture must have valid texture"), EAutomationExpectedErrorFlags::Contains, 0);
	AddExpectedError(TEXT("HeightCount must greater then 0"), EAutomationExpectedErrorFlags::Contains, 0);

	// A player on a widget with no visual at all. The cast to the sprite visual returns null and the
	// weak pointer stays empty, which is the state a designer leaves behind by adding the component
	// before the visual.
	{
		FPlayerFixture Bare;
		if (!TestNotNull(TEXT("the widget was constructed"), Bare.Widget))
		{
			return false;
		}
		UUISpriteSequencePlayer* Player = Bare.Widget->AddComponent<UUISpriteSequencePlayer>();
		if (TestNotNull(TEXT("the sprite sequence player was added"), Player))
		{
			Player->SetSpriteSequence(EmptySlots(3));
			Player->Play();
			TestFalse(TEXT("a sprite player with no sprite visual does not start"), Player->GetIsPlaying());
		}
		Bare.Teardown();
	}

	// With the visual but no frames. The two refusals are ordered -- visual first, then content --
	// and the order matters: reporting "empty sequence" on a widget that has no sprite at all sends
	// the author to the wrong place.
	{
		FPlayerFixture WithSprite;
		if (!TestNotNull(TEXT("the widget was constructed"), WithSprite.Widget))
		{
			return false;
		}
		WithSprite.Widget->CreateNewVisual(UDreamSprite::StaticClass());
		UUISpriteSequencePlayer* Player = WithSprite.Widget->AddComponent<UUISpriteSequencePlayer>();
		if (TestNotNull(TEXT("the sprite sequence player was added"), Player))
		{
			Player->Play();
			TestFalse(TEXT("a sprite player with no frames does not start"), Player->GetIsPlaying());
		}
		WithSprite.Teardown();
	}

	// The sheet player has three refusals, and the third is the one that guards a divide:
	// PrepareForPlay computes 1 / WidthCount the instant CanPlay lets it through.
	{
		FPlayerFixture Sheet;
		if (!TestNotNull(TEXT("the widget was constructed"), Sheet.Widget))
		{
			return false;
		}
		UUISpriteSheetTexturePlayer* Player = Sheet.Widget->AddComponent<UUISpriteSheetTexturePlayer>();
		if (!TestNotNull(TEXT("the sprite sheet player was added"), Player))
		{
			Sheet.Teardown();
			return false;
		}

		Player->Play();
		TestFalse(TEXT("a sheet player with no texture visual does not start"), Player->GetIsPlaying());

		UDreamTexture* TextureVisual =
			Cast<UDreamTexture>(Sheet.Widget->CreateNewVisual(UDreamTexture::StaticClass()));
		if (!TestNotNull(TEXT("the texture visual was created"), TextureVisual))
		{
			Sheet.Teardown();
			return false;
		}
		Player->Play();
		TestFalse(TEXT("nor one whose texture visual has no texture"), Player->GetIsPlaying());

		TextureVisual->SetTexture(UTexture2D::CreateTransient(8, 8));
		Player->SetWidthCount(0);
		Player->Play();
		TestFalse(TEXT("nor one whose sheet has no cells to divide into"), Player->GetIsPlaying());

		// And with everything in place it starts, which is what makes the three refusals above
		// refusals rather than a player that never works.
		Player->SetWidthCount(4);
		Player->SetHeightCount(2);
		Player->Play();
		TestTrue(TEXT("with a texture and a grid it plays"), Player->GetIsPlaying());
		Sheet.Teardown();
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpriteSheetFrameGridTest,
	"DreamGUI.Extensions.AFrameNumberBecomesOneCellOfTheSheetsUVGrid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpriteSheetFrameGridTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	FPlayerFixture Fixture;
	if (!TestNotNull(TEXT("the widget was constructed"), Fixture.Widget))
	{
		return false;
	}
	UDreamTexture* TextureVisual =
		Cast<UDreamTexture>(Fixture.Widget->CreateNewVisual(UDreamTexture::StaticClass()));
	UUISpriteSheetTexturePlayer* Player = Fixture.Widget->AddComponent<UUISpriteSheetTexturePlayer>();
	if (!TestNotNull(TEXT("the texture visual was created"), TextureVisual) ||
		!TestNotNull(TEXT("the sprite sheet player was added"), Player))
	{
		Fixture.Teardown();
		return false;
	}

	// A four by two sheet, so both grid axes divide exactly and every expected number below is
	// representable: the cell is a quarter wide and a half tall.
	TextureVisual->SetTexture(UTexture2D::CreateTransient(8, 8));
	Player->SetWidthCount(4);
	Player->SetHeightCount(2);
	Player->SetFps(8.0f);

	// Before the first Play. The cell size is computed in PrepareForPlay, and Seek used not to call
	// it -- so seeking on a player that had never played read two floats nothing had written and
	// handed the visual a cell with no size. Seeking now prepares the same way playing does, which
	// is what a seek actually is: put the playhead here and draw, and drawing needs what a draw
	// needs. Scrubbing a freshly configured player is the ordinary way into this.
	Player->SeekFrame(1);
	{
		const FVector4f Rect = TextureVisual->GetUVRect();
		TestEqual(TEXT("seeking before playing produces a cell a quarter of the sheet wide"),
			Rect.Z, 0.25f);
		TestEqual(TEXT("and half of it tall"), Rect.W, 0.5f);
		TestEqual(TEXT("at the second column"), Rect.X, 0.25f);
		TestEqual(TEXT("of the first row"), Rect.Y, 0.0f);
	}

	// Play rewinds and draws frame zero: the bottom-left cell. It computes the cell size as well,
	// which is the same preparation the seek above now does -- the two entry points agree rather
	// than one of them being the only way to get the player into a drawable state.
	Player->Play();
	{
		const FVector4f Rect = TextureVisual->GetUVRect();
		TestEqual(TEXT("frame zero starts at the left of the sheet"), Rect.X, 0.0f);
		TestEqual(TEXT("and at the bottom"), Rect.Y, 0.0f);
		TestEqual(TEXT("a cell is a quarter of the sheet across"), Rect.Z, 0.25f);
		TestEqual(TEXT("and half of it down"), Rect.W, 0.5f);
	}

	// The column is the frame modulo the width, so the first row fills left to right.
	Player->SeekFrame(1);
	TestEqual(TEXT("the next frame is one cell to the right"), TextureVisual->GetUVRect().X, 0.25f);
	TestEqual(TEXT("still on the first row"), TextureVisual->GetUVRect().Y, 0.0f);

	// The row is the frame divided by the width, so frame four wraps onto the second row.
	Player->SeekFrame(4);
	TestEqual(TEXT("frame four is back at the left"), TextureVisual->GetUVRect().X, 0.0f);
	TestEqual(TEXT("one row up"), TextureVisual->GetUVRect().Y, 0.5f);

	Player->SeekFrame(7);
	TestEqual(TEXT("the last cell is the far column"), TextureVisual->GetUVRect().X, 0.75f);
	TestEqual(TEXT("of the last row"), TextureVisual->GetUVRect().Y, 0.5f);

	// One past the end. The row clamp used to allow HeightCount rather than HeightCount - 1, which
	// put the cell's V origin on exactly 1.0 -- a full sheet below the last row, sampling nothing.
	// Frame WidthCount * HeightCount is not an exotic input: the tick path reaches it because the
	// wrap only fires when ElapsedTime is STRICTLY past Duration, so a clock landing exactly on the
	// duration asks for this frame.
	Player->SeekFrame(8);
	TestEqual(TEXT("one frame past the last cell holds the last row rather than walking off the sheet"),
		TextureVisual->GetUVRect().Y, 0.5f);
	TestEqual(TEXT("at the first column of it, which is where the row arithmetic lands"),
		TextureVisual->GetUVRect().X, 0.0f);

	// Time running backwards. SeekTime accepts a negative time, the frame number goes negative, and
	// both grid indices clamp to zero -- so the playhead pins to the first cell rather than wrapping
	// round to the last. That is a decision, not an accident, and it is the difference between a
	// rewinding animation and a stuck one.
	Player->SeekTime(-0.375f);
	TestEqual(TEXT("a negative time pins to the first column"), TextureVisual->GetUVRect().X, 0.0f);
	TestEqual(TEXT("and the first row, rather than wrapping to the last"),
		TextureVisual->GetUVRect().Y, 0.0f);

	// Seeking past the end does not consult Duration or the loop flag at all -- those live in
	// UpdateAnimation, which only the tick path reaches. So a looping player seeked past its own
	// length holds the last cell instead of wrapping to the beginning; what it must NOT do is leave
	// the sheet, which is what the row clamp is for.
	TestTrue(TEXT("the player is set to loop"), Player->GetLoop());
	Player->SeekTime(4.0f);
	TestEqual(TEXT("seeking well past the length does not wrap even with looping on"),
		TextureVisual->GetUVRect().Y, 0.5f);
	TestEqual(TEXT("and stays on the sheet while it does not -- frame thirty-two is column zero"),
		TextureVisual->GetUVRect().X, 0.0f);

	// The frame rate on the seek path. SeekFrame divides the frame number by the rate before it
	// consults anything, so a zero used to write a NaN into the clock that every later += kept --
	// and the drawn cell went with it. The rate is now pulled above zero first, so the frame number
	// the caller asked for is still the cell that gets drawn.
	Player->SetFps(0.0f);
	Player->SeekFrame(5);
	TestTrue(TEXT("seeking at what was a zero rate leaves a rate a clock can use"),
		Player->GetFps() > 0.0f);
	TestEqual(TEXT("and draws the frame that was asked for"), TextureVisual->GetUVRect().X, 0.25f);
	TestEqual(TEXT("on the row it belongs to"), TextureVisual->GetUVRect().Y, 0.5f);

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSequencePlayerTransportTest,
	"DreamGUI.Extensions.PauseResumesWhereItStoppedWhileStopRewindsToTheStart",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSequencePlayerTransportTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	FPlayerFixture Fixture;
	if (!TestNotNull(TEXT("the widget was constructed"), Fixture.Widget))
	{
		return false;
	}
	UDreamTexture* TextureVisual =
		Cast<UDreamTexture>(Fixture.Widget->CreateNewVisual(UDreamTexture::StaticClass()));
	UUISpriteSheetTexturePlayer* Player = Fixture.Widget->AddComponent<UUISpriteSheetTexturePlayer>();
	if (!TestNotNull(TEXT("the texture visual was created"), TextureVisual) ||
		!TestNotNull(TEXT("the sprite sheet player was added"), Player))
	{
		Fixture.Teardown();
		return false;
	}
	TextureVisual->SetTexture(UTexture2D::CreateTransient(8, 8));
	Player->SetWidthCount(4);
	Player->SetHeightCount(2);
	Player->SetFps(8.0f);

	// Play on a stopped player restarts from zero; Play on a paused one resumes. Both are the same
	// entry point, and which one happens turns on whether bIsPlaying is already set -- so the two
	// verbs share a function and a caller cannot choose between them. That is the contract this
	// pins, because "resume" quietly becoming "restart" is a whole animation replayed from the top.
	Player->Play();
	TestTrue(TEXT("it is playing"), Player->GetIsPlaying());

	Player->SeekFrame(5);
	TestEqual(TEXT("the playhead moved to the second row"), TextureVisual->GetUVRect().Y, 0.5f);
	TestEqual(TEXT("second column"), TextureVisual->GetUVRect().X, 0.25f);

	Player->Pause();
	TestFalse(TEXT("a paused player does not report itself as playing"), Player->GetIsPlaying());

	// Resuming must not touch the playhead. Play's restart branch is skipped because the player was
	// never stopped, so nothing rewinds and nothing is drawn.
	Player->Play();
	TestTrue(TEXT("and it plays again"), Player->GetIsPlaying());
	TestEqual(TEXT("resuming left the playhead where the pause found it"),
		TextureVisual->GetUVRect().X, 0.25f);
	TestEqual(TEXT("on the same row"), TextureVisual->GetUVRect().Y, 0.5f);

	// Stop is the other half, and it is the one that rewinds -- not itself, but through the next
	// Play, which resets the elapsed time and immediately draws frame zero.
	Player->Stop();
	TestFalse(TEXT("a stopped player is not playing"), Player->GetIsPlaying());
	TestEqual(TEXT("and stopping alone does not move the playhead"),
		TextureVisual->GetUVRect().X, 0.25f);

	Player->Play();
	TestTrue(TEXT("playing again starts it"), Player->GetIsPlaying());
	TestEqual(TEXT("from the first cell"), TextureVisual->GetUVRect().X, 0.0f);
	TestEqual(TEXT("at the bottom left"), TextureVisual->GetUVRect().Y, 0.0f);

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpriteSequenceContentChangeTest,
	"DreamGUI.Extensions.ReplacingTheFramesOfAPlayingSequenceCannotLeaveItIndexingNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpriteSequenceContentChangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	// SetSpriteSequence used to be an assignment and nothing else, and both halves of that were
	// wrong. It neither stopped the player nor re-checked what it had been handed, so emptying the
	// array under a running animation left OnUpdateAnimation indexing an empty array -- and the
	// clamp did not save it, because FMath::Clamp is Max(Min(X, Max), Min) and Clamp(N, 0, -1)
	// evaluates to 0, not -1. Element zero of nothing.
	//
	// It also left Duration behind. Play captures the length once, so a running animation handed a
	// longer or shorter sequence kept comparing its clock against the old one and looped early or
	// late for the rest of its life.
	AddExpectedError(TEXT("SpriteSequence array is empty"), EAutomationExpectedErrorFlags::Contains, 0);

	FPlayerFixture Fixture;
	if (!TestNotNull(TEXT("the widget was constructed"), Fixture.Widget))
	{
		return false;
	}
	Fixture.Widget->CreateNewVisual(UDreamSprite::StaticClass());
	UUISpriteSequencePlayer* Player = Fixture.Widget->AddComponent<UUISpriteSequencePlayer>();
	if (!TestNotNull(TEXT("the sprite sequence player was added"), Player))
	{
		Fixture.Teardown();
		return false;
	}

	Player->SetSpriteSequence(EmptySlots(6));
	Player->SetFps(12.0f);
	Player->Play();
	TestTrue(TEXT("six frames and a sprite visual is enough to play"), Player->GetIsPlaying());
	TestEqual(TEXT("half a second of it"), DurationOf(Player), 0.5f);

	// A shorter sequence is a legitimate mid-flight change and does not interrupt anything.
	Player->SetSpriteSequence(EmptySlots(3));
	TestTrue(TEXT("swapping in a shorter sequence keeps it playing"), Player->GetIsPlaying());
	TestEqual(TEXT("and the length follows the new frame count"), DurationOf(Player), 0.25f);

	// Emptying it is the case that used to crash. There is nothing left to draw, so the player
	// stops -- which is also the honest reading of the gesture: clearing the frames ends the
	// animation, it does not pause it.
	Player->SetSpriteSequence(TArray<UDreamUISpriteData_BaseObject*>());
	TestFalse(TEXT("emptying the sequence stops the player rather than leaving it indexing nothing"),
		Player->GetIsPlaying());
	TestEqual(TEXT("and the sequence really is empty"), Player->GetSpriteSequence().Num(), 0);

	// Stopped is stopped: CanPlay refuses an empty sequence at the door, and says so.
	Player->Play();
	TestFalse(TEXT("and it will not restart on nothing"), Player->GetIsPlaying());

	// Refilling it does not start anything either. Handing over content is not a transport command,
	// and a setter that quietly resumed would be indistinguishable from one that never stopped.
	Player->SetSpriteSequence(EmptySlots(4));
	TestFalse(TEXT("refilling the sequence does not restart it by itself"), Player->GetIsPlaying());
	Player->Play();
	TestTrue(TEXT("but Play does"), Player->GetIsPlaying());

	Fixture.Teardown();
	return true;
}

namespace DreamSequencePlayerTestLocal
{
	/** A bar on a root widget with a fill widget parented under it, which is the only arrangement that works. */
	struct FProgressBarFixture
	{
		UDreamWidgetTree* Tree = nullptr;
		UDreamWidget* Root = nullptr;
		UDreamWidget* Fill = nullptr;
		UUIProgressBar* Bar = nullptr;

		explicit FProgressBarFixture(bool bInAttachFill)
		{
			Tree = NewObject<UDreamWidgetTree>(GetTransientPackage());
			Root = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Track"));
			Fill = Tree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("Fill"));
			if (Root == nullptr || Fill == nullptr)
			{
				return;
			}
			Tree->RootWidget = Root;
			Root->SetWidth(200.0f);
			Root->SetHeight(20.0f);
			Fill->SetWidth(200.0f);
			Fill->SetHeight(20.0f);
			if (bInAttachFill)
			{
				Fill->TrySetParent(Root, false);
			}
			Bar = Root->AddComponent<UUIProgressBar>();
		}

		void Teardown()
		{
			if (Fill != nullptr && Fill->GetParent() == nullptr)
			{
				Fill->DestroyWidget();
			}
			Fill = nullptr;
			if (Root != nullptr)
			{
				Root->DestroyWidget();
				Root = nullptr;
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProgressBarPercentTest,
	"DreamGUI.Controls.ProgressBarBehaviour.ThePercentIsClampedAndBecomesTheFillsAnchorSpan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamProgressBarPercentTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	FProgressBarFixture Fixture(true);
	if (!TestNotNull(TEXT("the track was constructed"), Fixture.Root) ||
		!TestNotNull(TEXT("the fill was constructed"), Fixture.Fill) ||
		!TestNotNull(TEXT("the progress bar behaviour was added"), Fixture.Bar))
	{
		Fixture.Teardown();
		return false;
	}

	// Percent is written straight into an anchor fraction, and an anchor outside 0..1 puts the fill
	// outside its track -- which the layout will happily arrange and draw. So the clamp is the whole
	// safety of the class, and it belongs in the setter because a game feeding a health fraction has
	// no reason to have clamped it first.
	TestEqual(TEXT("a fresh bar is empty"), Fixture.Bar->GetPercent(), 0.0f);

	// With no fill widget yet there is nothing to write to, and the bar has to tolerate that: a
	// control is configured in some order, and "percent before parts" is a normal one.
	TestNull(TEXT("no fill widget has been named"), Fixture.Bar->GetFillWidget());
	Fixture.Bar->SetPercent(0.4f);
	TestEqual(TEXT("the percent is kept even with nowhere to put it"), Fixture.Bar->GetPercent(), 0.4f);

	Fixture.Bar->SetPercent(1.5f);
	TestEqual(TEXT("a percent above one is clamped to full"), Fixture.Bar->GetPercent(), 1.0f);
	Fixture.Bar->SetPercent(-3.0f);
	TestEqual(TEXT("and below zero to empty"), Fixture.Bar->GetPercent(), 0.0f);

	// Naming the fill applies whatever percent the bar is already holding, which is what makes the
	// order above safe.
	Fixture.Bar->SetFillWidget(Fixture.Fill);
	TestEqual(TEXT("the fill widget is remembered"), Fixture.Bar->GetFillWidget(), Fixture.Fill);
	TestEqual(TEXT("an empty bar anchors the fill to a zero-width span at the left"),
		Fixture.Fill->GetAnchorMin().X, 0.0);
	TestEqual(TEXT("with nothing beyond it"), Fixture.Fill->GetAnchorMax().X, 0.0);

	Fixture.Bar->SetPercent(0.25f);
	TestEqual(TEXT("a quarter full still starts at the left edge"), Fixture.Fill->GetAnchorMin().X, 0.0);
	TestEqual(TEXT("and reaches a quarter of the way across"), Fixture.Fill->GetAnchorMax().X, 0.25);

	// The change gate, and how little of a gate it is. SetPercent drops a write whose value is
	// FMath::IsNearlyEqual to the one already held -- and that default tolerance is UE_SMALL_NUMBER
	// (1e-8), not the KINDA_SMALL_NUMBER (1e-4) the name suggests to most readers. A float near 0.5
	// resolves to about 6e-8, so EVERY representable change is coarser than the tolerance: the gate
	// can only ever reject a value that is bitwise what is already stored.
	//
	// That is worth pinning rather than glossing, because it is the opposite of what a reader
	// assumes on seeing IsNearlyEqual: this bar does NOT swallow small increments, and anything
	// relying on it to throttle a per-frame driver is relying on nothing.
	Fixture.Bar->SetPercent(0.5f);
	TestEqual(TEXT("a real change lands"), Fixture.Bar->GetPercent(), 0.5f, 0.0f);

	Fixture.Bar->SetPercent(0.50005f);
	TestEqual(TEXT("a change of 5e-5 is far coarser than a 1e-8 tolerance, so it lands"),
		Fixture.Bar->GetPercent(), 0.50005f, 0.0f);
	TestEqual(TEXT("and the fill followed it"), Fixture.Fill->GetAnchorMax().X, 0.50005, 1e-6);

	// What the gate DOES catch: the same value twice. Written through the anchor rather than a
	// broadcast count because the delegate is the thing a caller would have to subscribe to, and
	// the anchor is the observable the bar exists to produce.
	Fixture.Fill->SetAnchorMax(FVector2D(0.123, Fixture.Fill->GetAnchorMax().Y));
	Fixture.Bar->SetPercent(0.50005f);
	TestEqual(TEXT("re-setting the value it already holds writes nothing at all"),
		Fixture.Fill->GetAnchorMax().X, 0.123, 1e-6);

	// Marquee replaces the percent with a sweeping window, and the window starts CLOSED: the offset
	// begins at zero and the trailing edge is one marquee width behind it, so both edges clamp to
	// the left of the track. A marquee bar is empty until something ticks it, which is only true
	// while Tick runs -- and nothing ticks a behaviour in an authoring tree.
	Fixture.Bar->SetIsMarquee(true);
	TestEqual(TEXT("a marquee sweep starts closed at the left"), Fixture.Fill->GetAnchorMin().X, 0.0);
	TestEqual(TEXT("with no width yet"), Fixture.Fill->GetAnchorMax().X, 0.0);

	Fixture.Bar->SetIsMarquee(false);
	TestEqual(TEXT("turning it off restores the percent"), Fixture.Fill->GetAnchorMax().X, 0.5);

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProgressBarDirectionTest,
	"DreamGUI.Controls.ProgressBarBehaviour.EachFillDirectionPutsTheEmptyEndSomewhereElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamProgressBarDirectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	FProgressBarFixture Fixture(true);
	if (!TestNotNull(TEXT("the track was constructed"), Fixture.Root) ||
		!TestNotNull(TEXT("the fill was constructed"), Fixture.Fill) ||
		!TestNotNull(TEXT("the progress bar behaviour was added"), Fixture.Bar))
	{
		Fixture.Teardown();
		return false;
	}

	// The four directions are two axes times two orientations, and the reversed pair is written as
	// 1 - End .. 1 - Start rather than as a separate formula. That subtraction is the whole
	// difference between a bar that empties from the right and one that fills from it, and getting
	// it backwards looks correct at fifty percent -- which is exactly the value somebody tests with.
	//
	// The direction is changed through SetFillType, which is new: FillType was BlueprintReadWrite
	// with no setter at all, so this test used to reach it through reflection and then re-apply the
	// percent through SetFillWidget, because nothing redrew on a direction change.
	Fixture.Bar->SetFillWidget(Fixture.Fill);
	Fixture.Bar->SetPercent(0.25f);

	TestEqual(TEXT("left to right starts at the left edge"), Fixture.Fill->GetAnchorMin().X, 0.0);
	TestEqual(TEXT("and grows rightwards"), Fixture.Fill->GetAnchorMax().X, 0.25);
	TestEqual(TEXT("across the whole height of the track"), Fixture.Fill->GetAnchorMin().Y, 0.0);
	TestEqual(TEXT("all of it"), Fixture.Fill->GetAnchorMax().Y, 1.0);

	Fixture.Bar->SetFillType(EUIProgressBarFillType::RightToLeft);
	TestEqual(TEXT("the direction is remembered"), Fixture.Bar->GetFillType(),
		EUIProgressBarFillType::RightToLeft);
	TestEqual(TEXT("right to left ends at the right edge"), Fixture.Fill->GetAnchorMax().X, 1.0);
	TestEqual(TEXT("and grows leftwards from a quarter in"), Fixture.Fill->GetAnchorMin().X, 0.75);

	Fixture.Bar->SetFillType(EUIProgressBarFillType::BottomToTop);
	TestEqual(TEXT("bottom to top starts at the bottom edge"), Fixture.Fill->GetAnchorMin().Y, 0.0);
	TestEqual(TEXT("and grows upwards"), Fixture.Fill->GetAnchorMax().Y, 0.25);

	// The axis that is no longer in use gets the whole track back. Without that, switching a live
	// bar from a horizontal direction to a vertical one left the fill squeezed across as well as
	// up -- the previous direction's progress span was still sitting on the horizontal anchors, and
	// the two squeezes multiplied into a corner with no way back.
	//
	// The full span rather than some remembered authored value is the deliberate part: whatever is
	// on the cross axis at a switch IS the old direction's progress, so it is definitely wrong, and
	// "the whole track" is the only value the bar can know is right. It costs an author nothing,
	// because the anchor setters preserve the offsets -- a fill inset by a margin keeps its inset.
	TestEqual(TEXT("the axis it stopped using is handed the whole track back"),
		Fixture.Fill->GetAnchorMin().X, 0.0);
	TestEqual(TEXT("all of it"), Fixture.Fill->GetAnchorMax().X, 1.0);

	Fixture.Bar->SetFillType(EUIProgressBarFillType::TopToBottom);
	TestEqual(TEXT("top to bottom ends at the top edge"), Fixture.Fill->GetAnchorMax().Y, 1.0);
	TestEqual(TEXT("and grows downwards from a quarter down"), Fixture.Fill->GetAnchorMin().Y, 0.75);

	// And back to a horizontal direction, which is the other half of the switch: the vertical span
	// the bar had just been writing has to be released the same way.
	Fixture.Bar->SetFillType(EUIProgressBarFillType::LeftToRight);
	TestEqual(TEXT("switching back releases the vertical span too"),
		Fixture.Fill->GetAnchorMin().Y, 0.0);
	TestEqual(TEXT("all of it"), Fixture.Fill->GetAnchorMax().Y, 1.0);
	TestEqual(TEXT("and the horizontal one shows the percent again"),
		Fixture.Fill->GetAnchorMax().X, 0.25);

	// Setting the direction it already has writes nothing, which is what makes SetFillType the same
	// shape as SetPercent rather than a redraw button.
	Fixture.Fill->SetAnchorMax(FVector2D(0.123, Fixture.Fill->GetAnchorMax().Y));
	Fixture.Bar->SetFillType(EUIProgressBarFillType::LeftToRight);
	TestEqual(TEXT("re-setting the direction it already holds applies nothing"),
		Fixture.Fill->GetAnchorMax().X, 0.123, 1e-6);

	Fixture.Teardown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProgressBarDetachedFillTest,
	"DreamGUI.Controls.ProgressBarBehaviour.AFillWidgetWithNoParentSilentlyIgnoresThePercent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamProgressBarDetachedFillTest::RunTest(const FString& Parameters)
{
	using namespace DreamSequencePlayerTestLocal;

	// The anchor setters resolve their new size against the PARENT's, so both of them do nothing at
	// all on a widget that has none. The bar has no idea: it calls them, they warn, and the fill
	// keeps its authored anchors. Wiring a fill widget before attaching it -- which is the order a
	// hand-built hierarchy naturally produces -- gives a bar that never moves.
	//
	// The warning is what makes this diagnosable at all, so it is left in the log rather than
	// asserted away; this is here to state that the failure is silent as far as the BAR is
	// concerned, which is the part nothing else records.
	AddExpectedMessage(TEXT("only valid if DreamWidget have parent"),
		ELogVerbosity::Warning, EAutomationExpectedErrorFlags::Contains, -1);

	FProgressBarFixture Fixture(false);
	if (!TestNotNull(TEXT("the track was constructed"), Fixture.Root) ||
		!TestNotNull(TEXT("the fill was constructed"), Fixture.Fill) ||
		!TestNotNull(TEXT("the progress bar behaviour was added"), Fixture.Bar))
	{
		Fixture.Teardown();
		return false;
	}
	TestNull(TEXT("the fill was left unattached"), Fixture.Fill->GetParent());

	Fixture.Bar->SetFillWidget(Fixture.Fill);
	Fixture.Bar->SetPercent(0.25f);
	TestEqual(TEXT("the bar took the percent"), Fixture.Bar->GetPercent(), 0.25f);
	TestEqual(TEXT("but the detached fill kept its authored anchors"),
		Fixture.Fill->GetAnchorMin().X, 0.5);
	TestEqual(TEXT("both of them"), Fixture.Fill->GetAnchorMax().X, 0.5);

	// Attaching it does not retroactively apply anything either -- the bar only writes on a change,
	// and the percent is already what it wants. The fill stays wrong until something moves it.
	Fixture.Fill->TrySetParent(Fixture.Root, false);
	TestEqual(TEXT("attaching afterwards does not replay the percent"),
		Fixture.Fill->GetAnchorMax().X, 0.5);

	// Naming the fill again is what fixes it, because SetFillWidget applies unconditionally.
	Fixture.Bar->SetFillWidget(Fixture.Fill);
	TestEqual(TEXT("naming the fill again applies the percent it should have had"),
		Fixture.Fill->GetAnchorMax().X, 0.25);

	Fixture.Teardown();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
