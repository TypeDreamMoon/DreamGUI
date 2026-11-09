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

#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "MovieScene.h"
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
	TestEqual(TEXT("as the track's line"), LineWith(Text, TEXT("Row/Icon.Width")), FString(TEXT("    Row/Icon.Width : 0 = 10, 0.6 = 50")));
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

#endif
