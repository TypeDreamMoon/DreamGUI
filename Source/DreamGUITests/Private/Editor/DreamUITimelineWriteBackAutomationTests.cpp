// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUITextWriteBack.h"
#include "Text/DreamUITimelineWriteBack.h"

#include "Animation/DreamUIAnimEventTrack.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "DreamUITextStructureTestFixtures.h"

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "UObject/StrongObjectPtr.h"

/*
 * An edit made in the animation editor to a `timeline` block's animation, written back into the block: unchanged keys
 * keep their line (an eased run read back as its ease), a changed key rewrites its line, a removed track takes its line,
 * a new event key adds one, and a key the language has no spelling for is refused.
 */

namespace DreamUITimelineWriteBackTestLocal
{
	FString Lines(const TArray<FString>& InLines)
	{
		return FString::Join(InLines, TEXT("\n"));
	}

	struct FBuilt
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		UDreamWidgetAnimation* Animation = nullptr;
		FDreamUIDiagnosticBag Diagnostics;
	};

	FBuilt Build(const FString& InText, const TCHAR* InTimeline)
	{
		FBuilt Built;
		FDreamUIAst Ast;
		Built.Tree.Reset(FDreamUITextWriteBack::BuildReferenceTree(InText, Ast, Built.Diagnostics));
		UDreamWidget* Root = Built.Tree.IsValid() ? Built.Tree->RootWidget.Get() : nullptr;
		UDreamWidgetAnimationComponent* Animator = Root != nullptr ? Root->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
		Built.Animation = Animator != nullptr ? Animator->GetSequenceByDisplayName(InTimeline) : nullptr;
		return Built;
	}

	/** The property track driving InPropertyPath, whichever channel type it keys. */
	UMovieScenePropertyTrack* FindTrack(UDreamWidgetAnimation* InAnimation, const TCHAR* InPropertyPath)
	{
		UMovieScene* MovieScene = InAnimation->GetMovieScene();
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
				if (PropertyTrack != nullptr && PropertyTrack->GetPropertyPath().ToString().Equals(InPropertyPath, ESearchCase::IgnoreCase))
				{
					return PropertyTrack;
				}
			}
		}
		return nullptr;
	}

	/** Do InEdit to every key of the first channel of a one-channel track, float or double. */
	template<typename EditType>
	bool EditKeys(UMovieScenePropertyTrack* InTrack, EditType InEdit)
	{
		if (InTrack == nullptr || InTrack->GetAllSections().Num() == 0)
		{
			return false;
		}
		FMovieSceneChannelProxy& Proxy = InTrack->GetAllSections()[0]->GetChannelProxy();
		TArrayView<FMovieSceneFloatChannel*> Floats = Proxy.GetChannels<FMovieSceneFloatChannel>();
		if (Floats.Num() > 0)
		{
			auto Data = Floats[0]->GetData();
			auto Values = Data.GetValues();
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				InEdit(Index, Values.Num(), Values[Index].Value, Values[Index].InterpMode, Values[Index].TangentMode);
			}
			return true;
		}
		TArrayView<FMovieSceneDoubleChannel*> Doubles = Proxy.GetChannels<FMovieSceneDoubleChannel>();
		if (Doubles.Num() > 0)
		{
			auto Data = Doubles[0]->GetData();
			auto Values = Data.GetValues();
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				float Value = static_cast<float>(Values[Index].Value);
				InEdit(Index, Values.Num(), Value, Values[Index].InterpMode, Values[Index].TangentMode);
				Values[Index].Value = Value;
			}
			return true;
		}
		return false;
	}

	FString LineWith(const FString& InText, const TCHAR* InNeedle)
	{
		TArray<FString> All;
		InText.ParseIntoArrayLines(All, /*bCullEmpty*/false);
		for (const FString& Line : All)
		{
			if (Line.Contains(InNeedle))
			{
				return Line;
			}
		}
		return FString();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITimelineWriteBackTest,
	"DreamGUI.Text.Timeline.AnAnimationEditorEditIsWrittenBackIntoItsBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITimelineWriteBackTest::RunTest(const FString&)
{
	using namespace DreamUITimelineWriteBackTestLocal;

	const FString Source = Lines({
		TEXT("timeline Pulse {"),
		TEXT("    duration = 1.0"),
		TEXT("    loop     = PingPong"),
		TEXT("    // the bounce"),
		TEXT("    RenderScale : 0.0 = (0, 0, 0) ease OutCubic, 1.0 = (1, 1, 1)"),
		TEXT("    Row/Icon.Width : 0.0 = 10, 0.6 = 40"),
		TEXT("    Row/Icon.Color : 0.0 = #FFFFFF, 0.6 = #FFC800"),
		TEXT("    @0.25 -> Landed"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Widget Row {"),
		TEXT("        Image Icon { }"),
		TEXT("    }"),
		TEXT("}")
	});
	FBuilt Built = Build(Source, TEXT("Pulse"));
	if (!TestNotNull(*FString::Printf(TEXT("the timeline built (%s)"), *Built.Diagnostics.ToString()), Built.Animation)) return false;
	UDreamWidget* Host = Built.Tree->RootWidget;
	UDreamWidgetAnimation* Animation = Built.Animation;

	FString Text;
	FString Error;
	TestTrue(*FString::Printf(TEXT("an untouched animation reads back (%s)"), *Error), FDreamUITimelineWriteBack::RewriteText(Source, Animation, Host, Text, Error));
	TestEqual(TEXT("as the block it was built from, byte for byte -- the eased run read back as its ease"), Text, Source);

	// A key's value changed: its line is rewritten, every other line stays.
	UMovieScenePropertyTrack* Width = FindTrack(Animation, TEXT("AnimatableWidth"));
	if (!TestNotNull(TEXT("the width has its track"), Width)) return false;
	TestTrue(TEXT("its last key is moved to 50"), EditKeys(Width, [](int32 Index, int32 Count, float& Value, auto&, auto&)
	{
		if (Index + 1 == Count)
		{
			Value = 50.0f;
		}
	}));
	TestTrue(*FString::Printf(TEXT("the edit is written (%s)"), *Error), FDreamUITimelineWriteBack::RewriteText(Source, Animation, Host, Text, Error));
	TestEqual(TEXT("as the track's line, the value it changed and nothing else"), LineWith(Text, TEXT("Row/Icon.Width")), FString(TEXT("    Row/Icon.Width : 0.0 = 10, 0.6 = 50")));
	TestEqual(TEXT("the eased line untouched"), LineWith(Text, TEXT("RenderScale")), FString(TEXT("    RenderScale : 0.0 = (0, 0, 0) ease OutCubic, 1.0 = (1, 1, 1)")));
	TestTrue(TEXT("and the comment, the settings and the event kept"), Text.Contains(TEXT("    // the bounce")) && Text.Contains(TEXT("    loop     = PingPong"))
		&& Text.Contains(TEXT("    duration = 1.0")) && Text.Contains(TEXT("    @0.25 -> Landed")));

	// A key Sequencer gave automatic tangents is straightened, in the sequence and the file alike.
	EditKeys(Width, [](int32 Index, int32, float&, auto& Interp, auto& Tangent)
	{
		if (Index == 0)
		{
			Interp = RCIM_Cubic;
			Tangent = RCTM_Auto;
		}
	});
	TestTrue(*FString::Printf(TEXT("an automatic key is accepted (%s)"), *Error), FDreamUITimelineWriteBack::RewriteText(Source, Animation, Host, Text, Error));
	bool bStraightened = false;
	EditKeys(Width, [&bStraightened](int32 Index, int32, float&, auto& Interp, auto&)
	{
		bStraightened |= Index == 0 && Interp == RCIM_Linear;
	});
	TestTrue(TEXT("and made linear in the sequence"), bStraightened);

	// Tangents of its own have no spelling.
	EditKeys(Width, [](int32 Index, int32, float&, auto& Interp, auto& Tangent)
	{
		if (Index == 0)
		{
			Interp = RCIM_Cubic;
			Tangent = RCTM_User;
		}
	});
	TestFalse(TEXT("a key with tangents of its own is refused"), FDreamUITimelineWriteBack::RewriteText(Source, Animation, Host, Text, Error));
	TestTrue(*FString::Printf(TEXT("with the way out (%s)"), *Error), Error.Contains(TEXT("external")));
	EditKeys(Width, [](int32 Index, int32, float&, auto& Interp, auto&)
	{
		if (Index == 0)
		{
			Interp = RCIM_Linear;
		}
	});

	// A track removed takes its line; an event added gets one.
	UMovieScenePropertyTrack* Color = FindTrack(Animation, TEXT("Color"));
	if (TestNotNull(TEXT("the colour has its track"), Color))
	{
		Animation->GetMovieScene()->RemoveTrack(*Color);
	}
	for (UMovieSceneTrack* Track : Animation->GetMovieScene()->GetTracks())
	{
		if (UDreamUIAnimEventTrack* Events = Cast<UDreamUIAnimEventTrack>(Track))
		{
			UDreamUIAnimEventSection* Section = Events->GetAllSections().Num() > 0 ? Cast<UDreamUIAnimEventSection>(Events->GetAllSections()[0]) : nullptr;
			if (Section != nullptr)
			{
				Section->EventChannel.GetData().AddKey(FFrameNumber(12000), FString(TEXT("Hit")));
			}
		}
	}
	TestTrue(*FString::Printf(TEXT("the removal and the event are written (%s)"), *Error), FDreamUITimelineWriteBack::RewriteText(Source, Animation, Host, Text, Error));
	TestFalse(TEXT("the colour's line is gone"), Text.Contains(TEXT("Row/Icon.Color")));
	TestTrue(TEXT("the new event is a line"), Text.Contains(TEXT("    @0.5 -> Hit")));
	TestTrue(TEXT("beside the old one"), Text.Contains(TEXT("    @0.25 -> Landed")));
	FBuilt Rebuilt = Build(Text, TEXT("Pulse"));
	TestNotNull(*FString::Printf(TEXT("and the written block builds (%s)"), *Rebuilt.Diagnostics.ToString()), Rebuilt.Animation);
	if (HasAnyErrors())
	{
		AddInfo(TEXT("written:\n") + Text);
	}
	return true;
}

namespace DreamUITimelineWriteBackTestLocal
{
	/**
	 * A settings panel's entrance: rows whose lines are padded so their keys stand in columns, times written to two places,
	 * one row easing out of its first key -- a run of samples in the sequence -- and one that writes its ease on its last
	 * key, where it goes nowhere and builds no samples at all.
	 */
	TArray<FString> RevealLines()
	{
		return {
			TEXT("timeline Reveal {"),
			TEXT("    duration = 0.6"),
			TEXT(""),
			TEXT("    // the rows slide in"),
			TEXT("    Content/Header.RenderTranslation      : 0.0 = (-28, 0, 0) ease OutCubic, 0.42 = (0, 0, 0)"),
			TEXT("    Content/Footer.RenderTranslation      : 0.0 = (-28, 0, 0), 0.30 = (-28, 0, 0), 0.58 = (0, 0, 0) ease OutCubic"),
			TEXT("}"),
			TEXT("Widget Root {"),
			TEXT("    Widget Content {"),
			TEXT("        Widget Header { }"),
			TEXT("        Widget Footer { }"),
			TEXT("    }"),
			TEXT("}")
		};
	}

	/** The property track InWidgetName's binding drives InPropertyPath with. */
	UMovieScenePropertyTrack* FindBoundTrack(UDreamWidgetAnimation* InAnimation, const TCHAR* InWidgetName, const TCHAR* InPropertyPath)
	{
		UMovieScene* MovieScene = InAnimation != nullptr ? InAnimation->GetMovieScene() : nullptr;
		if (MovieScene == nullptr)
		{
			return nullptr;
		}
		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
		{
			const FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Binding.GetObjectGuid());
			if (Possessable == nullptr || !Possessable->GetName().Equals(InWidgetName, ESearchCase::IgnoreCase))
			{
				continue;
			}
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
				if (PropertyTrack != nullptr && PropertyTrack->GetPropertyPath().ToString().Equals(InPropertyPath, ESearchCase::IgnoreCase))
				{
					return PropertyTrack;
				}
			}
		}
		return nullptr;
	}

	TArrayView<FMovieSceneDoubleChannel*> DoubleChannelsOf(UMovieScenePropertyTrack* InTrack)
	{
		return InTrack != nullptr && InTrack->GetAllSections().Num() > 0
			? InTrack->GetAllSections()[0]->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>()
			: TArrayView<FMovieSceneDoubleChannel*>();
	}

	/** Every channel's last key moved to InFrame, as Sequencer moves the key a hand drags: the keys around it stay. */
	bool MoveLastKey(UMovieScenePropertyTrack* InTrack, FFrameNumber InFrame)
	{
		TArrayView<FMovieSceneDoubleChannel*> Channels = DoubleChannelsOf(InTrack);
		if (Channels.IsEmpty())
		{
			return false;
		}
		InTrack->GetAllSections()[0]->Modify();
		for (FMovieSceneDoubleChannel* Channel : Channels)
		{
			auto Data = Channel->GetData();
			if (Data.GetTimes().IsEmpty())
			{
				return false;
			}
			Data.MoveKey(Data.GetTimes().Num() - 1, InFrame);
		}
		return true;
	}

	/** A channel's keys, as frames and values. */
	TArray<TPair<int32, double>> KeysOf(UMovieScenePropertyTrack* InTrack, int32 InChannel)
	{
		TArray<TPair<int32, double>> Keys;
		TArrayView<FMovieSceneDoubleChannel*> Channels = DoubleChannelsOf(InTrack);
		if (Channels.IsValidIndex(InChannel))
		{
			auto Data = Channels[InChannel]->GetData();
			for (int32 Index = 0; Index < Data.GetTimes().Num(); ++Index)
			{
				Keys.Add({ Data.GetTimes()[Index].Value, Data.GetValues()[Index].Value });
			}
		}
		return Keys;
	}

	/** The timeline InName of a compiled text-authored Blueprint: the one on its authoring root, which the editor edits. */
	UDreamWidgetAnimation* AuthoredTimeline(const DreamUITextStructureFixtures::FScopedTextBlueprint& InScoped, const TCHAR* InName)
	{
		UDreamWidget* Root = InScoped.Blueprint != nullptr && IsValid(InScoped.Blueprint->WidgetTree) ? InScoped.Blueprint->WidgetTree->RootWidget.Get() : nullptr;
		UDreamWidgetAnimationComponent* Animator = Root != nullptr ? Root->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
		return Animator != nullptr ? Animator->GetSequenceByDisplayName(InName) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITimelineDraggedKeyTest,
	"DreamGUI.Text.Timeline.AKeyDraggedInSequencerRewritesItsTimeAndNothingElse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The edit a hand makes in Sequencer: one key dragged along the time ruler, snapped to a frame of the 60 the timeline is
 * shown at. The line it is on changes in that key's time and in nothing else -- its padding, its `0.0`, its `0.30`, the
 * `ease OutCubic` on its last key -- and the time is short: frame 30 is 0.50, written to the two places the token it
 * replaces had, and frame 28 is 0.47, not 0.4666666666666667. On the row that eases out of its first key, dragging the
 * end of the eased run leaves the run's samples where they were, and the line keeps its ease and its two keys.
 */
bool FDreamUITimelineDraggedKeyTest::RunTest(const FString&)
{
	using namespace DreamUITimelineWriteBackTestLocal;

	const FString Source = Lines(RevealLines());
	FBuilt Built = Build(Source, TEXT("Reveal"));
	if (!TestNotNull(*FString::Printf(TEXT("the timeline built (%s)"), *Built.Diagnostics.ToString()), Built.Animation)) return false;
	UDreamWidget* Host = Built.Tree->RootWidget;
	UMovieScenePropertyTrack* Header = FindBoundTrack(Built.Animation, TEXT("Header"), TEXT("RenderTranslation"));
	UMovieScenePropertyTrack* Footer = FindBoundTrack(Built.Animation, TEXT("Footer"), TEXT("RenderTranslation"));
	if (!TestNotNull(TEXT("the eased row has its track"), Header) || !TestNotNull(TEXT("and so does the other"), Footer)) return false;
	TestTrue(TEXT("the eased row's run is samples in the sequence"), KeysOf(Header, 0).Num() > 2);
	TestEqual(TEXT("the ease on the other row's last key builds none"), KeysOf(Footer, 0).Num(), 3);

	FString Text;
	FString Error;
	TestTrue(*FString::Printf(TEXT("an untouched animation reads back (%s)"), *Error), FDreamUITimelineWriteBack::RewriteText(Source, Built.Animation, Host, Text, Error));
	TestEqual(TEXT("as the block it was built from, byte for byte"), Text, Source);

	const FString FooterBefore = TEXT("0.30 = (-28, 0, 0), 0.58 = (0, 0, 0) ease OutCubic");
	TestTrue(TEXT("the other row's last key is dragged to frame 30"), MoveLastKey(Footer, FFrameNumber(12000)));
	TestTrue(*FString::Printf(TEXT("the drag is written (%s)"), *Error), FDreamUITimelineWriteBack::RewriteText(Source, Built.Animation, Host, Text, Error));
	TestEqual(TEXT("as that key's time, to the places the token had, and the rest of the file as it was"), Text,
		Source.Replace(*FooterBefore, TEXT("0.30 = (-28, 0, 0), 0.50 = (0, 0, 0) ease OutCubic")));

	TestTrue(TEXT("and on to frame 28"), MoveLastKey(Footer, FFrameNumber(11200)));
	TestTrue(*FString::Printf(TEXT("which is written too (%s)"), *Error), FDreamUITimelineWriteBack::RewriteText(Source, Built.Animation, Host, Text, Error));
	const FString AtFrame28 = Source.Replace(*FooterBefore, TEXT("0.30 = (-28, 0, 0), 0.47 = (0, 0, 0) ease OutCubic"));
	TestEqual(TEXT("as the shortest time on that frame"), Text, AtFrame28);

	TestTrue(TEXT("the eased row's last key is dragged to frame 30, its samples left where they were"), MoveLastKey(Header, FFrameNumber(12000)));
	TestTrue(*FString::Printf(TEXT("that drag is written (%s)"), *Error), FDreamUITimelineWriteBack::RewriteText(Source, Built.Animation, Host, Text, Error));
	TestEqual(TEXT("as the end of the eased run moving, the ease and the two keys kept"), Text,
		AtFrame28.Replace(TEXT("ease OutCubic, 0.42 = (0, 0, 0)"), TEXT("ease OutCubic, 0.50 = (0, 0, 0)")));
	if (HasAnyErrors())
	{
		AddInfo(TEXT("written:\n") + Text);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITimelineWrittenEditRekeysTest,
	"DreamGUI.Text.Timeline.AWrittenEditLeavesTheAnimationAsTheFileNowBuildsIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The end of an eased run dragged, written into the .dui on disk: the animation is re-keyed to what the new line builds --
 * the samples of the ease made again across the new span, the same keys a compile of the file makes -- so a second drag,
 * read against the line the file has now, finds them where that line puts them, and is written as a second time token
 * with the ease still on its key rather than as the run spelled out key by key.
 */
bool FDreamUITimelineWrittenEditRekeysTest::RunTest(const FString&)
{
	using namespace DreamUITimelineWriteBackTestLocal;
	using namespace DreamUITextStructureFixtures;

	FScopedDuiFile File(TEXT("TimelineRekey.dui"));
	if (!TestTrue(TEXT("the .dui is written"), File.Write(RevealLines())))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("TimelineRekey"), File.FilePath, /*bInOpenDesigner*/ false);
	UDreamWidgetAnimation* Animation = AuthoredTimeline(Scoped, TEXT("Reveal"));
	if (!TestEqual(TEXT("the class compiles"), Scoped.CompileErrors, 0) || !TestNotNull(TEXT("with its timeline"), Animation))
	{
		return false;
	}
	UMovieScenePropertyTrack* Header = FindBoundTrack(Animation, TEXT("Header"), TEXT("RenderTranslation"));
	if (!TestNotNull(TEXT("the eased row has its track"), Header))
	{
		return false;
	}

	TestTrue(TEXT("the end of the eased run is dragged to frame 30"), MoveLastKey(Header, FFrameNumber(12000)));
	FText Reason;
	TestTrue(*FString::Printf(TEXT("the drag is written (%s)"), *Reason.ToString()), FDreamUITimelineWriteBack::WriteAnimation(Scoped.Blueprint, Animation, Reason));
	const FString Written = File.Read();
	TestEqual(TEXT("the file says the run ends at 0.50"), LineWith(Written, TEXT("Content/Header")),
		FString(TEXT("    Content/Header.RenderTranslation      : 0.0 = (-28, 0, 0) ease OutCubic, 0.50 = (0, 0, 0)")));

	FBuilt Rebuilt = Build(Written, TEXT("Reveal"));
	UMovieScenePropertyTrack* RebuiltHeader = Rebuilt.Animation != nullptr ? FindBoundTrack(Rebuilt.Animation, TEXT("Header"), TEXT("RenderTranslation")) : nullptr;
	if (TestNotNull(TEXT("the file builds"), RebuiltHeader))
	{
		const TArray<TPair<int32, double>> Holds = KeysOf(Header, 0);
		const TArray<TPair<int32, double>> Builds = KeysOf(RebuiltHeader, 0);
		bool bSame = Holds.Num() == Builds.Num();
		for (int32 Index = 0; bSame && Index < Holds.Num(); ++Index)
		{
			bSame = Holds[Index].Key == Builds[Index].Key && FMath::IsNearlyEqual(Holds[Index].Value, Builds[Index].Value, 1.0e-4);
		}
		TestTrue(FString::Printf(TEXT("and the animation holds the keys it builds: %d keys, the file builds %d"), Holds.Num(), Builds.Num()), bSame);
	}

	TestTrue(TEXT("the run's end is dragged again, to frame 36"), MoveLastKey(Header, FFrameNumber(14400)));
	TestTrue(*FString::Printf(TEXT("the second drag is written (%s)"), *Reason.ToString()), FDreamUITimelineWriteBack::WriteAnimation(Scoped.Blueprint, Animation, Reason));
	TestEqual(TEXT("as its time, the ease still on the run's first key"), LineWith(File.Read(), TEXT("Content/Header")),
		FString(TEXT("    Content/Header.RenderTranslation      : 0.0 = (-28, 0, 0) ease OutCubic, 0.60 = (0, 0, 0)")));
	if (HasAnyErrors())
	{
		AddInfo(TEXT("written:\n") + File.Read());
	}
	return true;
}

#endif
