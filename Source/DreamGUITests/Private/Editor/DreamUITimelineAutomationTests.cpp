// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintTestTypes.h"
#include "Animation/DreamUIAnimEventTrack.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"

#include "MovieScene.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Sections/MovieSceneColorSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"

#include "HAL/FileManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * `timeline` blocks becoming animations -- the proposal's layer one, end to end.
 *
 * What is worth asserting here is not "a track exists" but the three facts the whole design rests
 * on, because each of them is a silent failure if it slips:
 *
 *   THE TEXT OWNS IT. A language-owned sequence is rebuilt from the file on every compile, is NOT
 *   carried across the rebuild the way an editor-made one is, and reports itself non-editable so
 *   Sequencer opens it read-only. Get the carry wrong and one compile puts the previous version back
 *   on top of the one the author just wrote -- which looks exactly like "my edit did nothing".
 *
 *   AN EASE IS A NAME. The tangents are derived, never written, so the only thing a test can pin is
 *   that the named curve produces the SHAPE the curve library produces. Sampling is what makes that
 *   true for every name including the overshooting ones, so the assertion is on a value between two
 *   keys, not on a tangent quadruple.
 *
 *   `external` IS A MANIFEST. It builds nothing; its whole job is that the file lists the animations
 *   it does not contain, which is what makes "what animations does this class have" answerable from
 *   the text.
 */

namespace DreamUITimelineTestLocal
{
	struct FScopedDuiFile
	{
		explicit FScopedDuiFile(const TCHAR* InFileName)
		{
			FilePath = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), InFileName));
			FPaths::NormalizeFilename(FilePath);
		}
		~FScopedDuiFile()
		{
			IFileManager::Get().Delete(*FilePath, false, true, true);
		}
		FScopedDuiFile(const FScopedDuiFile&) = delete;
		FScopedDuiFile& operator=(const FScopedDuiFile&) = delete;

		bool Write(const TArray<FString>& InLines) const
		{
			return FFileHelper::SaveStringToFile(FString::Join(InLines, TEXT("\n")), *FilePath);
		}
		FString FilePath;
	};

	struct FScopedBlueprint
	{
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;

		explicit FScopedBlueprint(const TCHAR* InName)
		{
			Package = CreatePackage(*FString::Printf(TEXT("/Temp/DreamGUITests/%s"), InName));
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamTextUserWidgetBindingBase::StaticClass(), Package, FName(InName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}
		~FScopedBlueprint()
		{
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}
		FScopedBlueprint(const FScopedBlueprint&) = delete;
		FScopedBlueprint& operator=(const FScopedBlueprint&) = delete;

		bool SetDuiFilePath(const FString& InFilePath) const
		{
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults == nullptr)
			{
				return false;
			}
			Defaults->SourceFile.FilePath = InFilePath;
			return true;
		}
	};

	void Compile(UDreamWidgetBlueprint* InBlueprint, FCompilerResultsLog& OutResults)
	{
		FKismetEditorUtilities::CompileBlueprint(InBlueprint, EBlueprintCompileOptions::SkipGarbageCollection, &OutResults);
	}

	UDreamWidgetAnimationComponent* AnimatorOf(const UDreamWidgetBlueprint* InBlueprint)
	{
		if (InBlueprint == nullptr || !IsValid(InBlueprint->WidgetTree) || !IsValid(InBlueprint->WidgetTree->RootWidget))
		{
			return nullptr;
		}
		return InBlueprint->WidgetTree->RootWidget->GetComponent<UDreamWidgetAnimationComponent>();
	}

	/** The tree a .dui text builds, kept alive, with everything the build had to say. */
	struct FBuiltTree
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;
	};

	FBuiltTree BuildFromText(const TArray<FString>& InLines)
	{
		FBuiltTree Built;
		Built.Diagnostics.SourceName = TEXT("Timeline.dui");
		const FString Text = FString::Join(InLines, TEXT("\n"));
		if (FDreamUISourceFile::Parse(Text, Built.Diagnostics.SourceName, Built.Ast, Built.Diagnostics))
		{
			TArray<FDreamWidgetPropertyBinding> Bindings;
			Built.Tree.Reset(FDreamUITextBuilder::Build(Built.Ast, GetTransientPackage(), Built.Diagnostics, Bindings));
		}
		return Built;
	}

	bool HasCode(const FDreamUIDiagnosticBag& InBag, EDreamUIDiagnosticCode InCode)
	{
		return InBag.Diagnostics.ContainsByPredicate(
			[InCode](const FDreamUIDiagnostic& InDiagnostic) { return InDiagnostic.Code == InCode; });
	}

	UDreamWidgetAnimation* FindAnimation(const FBuiltTree& InBuilt, const TCHAR* InName)
	{
		if (!InBuilt.Tree.IsValid() || !IsValid(InBuilt.Tree->RootWidget))
		{
			return nullptr;
		}
		UDreamWidgetAnimationComponent* Animator = InBuilt.Tree->RootWidget->GetComponent<UDreamWidgetAnimationComponent>();
		return Animator != nullptr ? Animator->GetSequenceByDisplayName(InName) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITimelineBuildsTracksTest,
	"DreamGUI.Text.Timeline.ABlockBecomesOneAnimationWithATrackPerLine",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITimelineBuildsTracksTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITimelineTestLocal;

	// One line per track kind layer one supports: a three-component vector on the host, a float on a
	// node one level down, and a colour on that node's visual. `Row/Icon` exercises the '/' path,
	// which is the only place in the grammar a slash separates two ids rather than starting a path.
	const FBuiltTree Built = BuildFromText({
		TEXT("timeline Pulse {"),
		TEXT("    duration = 0.6"),
		TEXT("    loop     = PingPong"),
		TEXT("    RenderScale : 0.0 = (1, 1, 1), 0.3 = (1.25, 1.25, 1), 0.6 = (1, 1, 1)"),
		TEXT("    Row/Icon.Width : 0.0 = 10, 0.6 = 40"),
		TEXT("    Row/Icon.Color : 0.0 = #FFFFFF, 0.6 = #FFC800"),
		TEXT("}"),
		TEXT("Widget Root {"),
		TEXT("    Widget Row {"),
		TEXT("        Image Icon { }"),
		TEXT("    }"),
		TEXT("}")
	});

	if (!TestFalse(FString::Printf(TEXT("the file builds (%s)"), *Built.Diagnostics.ToString()),
		Built.Diagnostics.HasErrors()))
	{
		return false;
	}
	UDreamWidgetAnimation* Animation = FindAnimation(Built, TEXT("Pulse"));
	if (!TestNotNull(TEXT("the block produced one animation, named after itself"), Animation))
	{
		return false;
	}
	TestTrue(TEXT("marked language-owned, which is what stops the next compile carrying it"),
		Animation->IsLanguageOwned());
	TestFalse(TEXT("and what makes Sequencer open it read-only"), Animation->IsEditable());

	UMovieScene* MovieScene = Animation->GetMovieScene();
	if (!TestNotNull(TEXT("with a movie scene"), MovieScene))
	{
		return false;
	}
	// Three bindings for three lines, and the split is the interesting part: Root's own track, Icon's
	// WIDGET track (Width) and Icon's VISUAL track (Color) are three different bound objects. Two
	// lines on one object would share a possessable, which is what makes Sequencer show one row per
	// object with its tracks underneath.
	TestEqual(TEXT("one possessable per bound object"), MovieScene->GetPossessableCount(), 3);

	int32 Vectors = 0;
	int32 Floats = 0;
	int32 Colors = 0;
	for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(MovieScene)->GetBindings())
	{
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			Vectors += Cast<UMovieSceneDoubleVectorTrack>(Track) != nullptr ? 1 : 0;
			Floats += Cast<UMovieSceneFloatTrack>(Track) != nullptr ? 1 : 0;
			Colors += Cast<UMovieSceneColorTrack>(Track) != nullptr ? 1 : 0;
		}
	}
	TestEqual(TEXT("the vector line made a vector track"), Vectors, 1);
	TestEqual(TEXT("the float line made a float track"), Floats, 1);
	TestEqual(TEXT("the colour line made a colour track"), Colors, 1);

	// The playback range covers the declared duration. Ticks are 24000/s, so 0.6s is 14400.
	TestEqual(TEXT("the playback range is the declared duration"),
		MovieScene->GetPlaybackRange().GetUpperBoundValue().Value, 14401);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITimelineEaseTest,
	"DreamGUI.Text.Timeline.AnEaseNameIsSampledFromTheCurveLibraryRatherThanWrittenAsTangents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITimelineEaseTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITimelineTestLocal;

	// OutCubic is above the straight line everywhere inside the segment, which is the whole point of
	// an ease and the thing a tangent-free representation has to still be true of. Asserted on the
	// VALUE at the midpoint, because that is what an author sees; a tangent assertion would pin an
	// implementation detail the proposal deliberately refuses to express in text.
	const FBuiltTree Built = BuildFromText({
		TEXT("timeline Slide {"),
		TEXT("    duration = 1.0"),
		TEXT("    RenderScale : 0.0 = (0, 0, 0) ease OutCubic, 1.0 = (1, 1, 1)"),
		TEXT("}"),
		TEXT("Widget Root { }")
	});
	if (!TestFalse(FString::Printf(TEXT("the file builds (%s)"), *Built.Diagnostics.ToString()),
		Built.Diagnostics.HasErrors()))
	{
		return false;
	}
	UDreamWidgetAnimation* Animation = FindAnimation(Built, TEXT("Slide"));
	if (!TestNotNull(TEXT("the timeline built"), Animation))
	{
		return false;
	}

	FMovieSceneDoubleChannel* Channel = nullptr;
	for (const FMovieSceneBinding& Binding : static_cast<const UMovieScene*>(Animation->GetMovieScene())->GetBindings())
	{
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			if (UMovieSceneDoubleVectorTrack* Vector = Cast<UMovieSceneDoubleVectorTrack>(Track))
			{
				for (UMovieSceneSection* Section : Vector->GetAllSections())
				{
					TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
					if (Channels.Num() > 0)
					{
						Channel = Channels[0];
					}
				}
			}
		}
	}
	if (!TestNotNull(TEXT("the track has a channel"), Channel))
	{
		return false;
	}
	// Sampled at the display rate, so a one-second eased segment is a channel full of keys rather
	// than two keys and a pair of tangents. That IS the representation; the count only has to prove
	// the sampling happened.
	TestTrue(TEXT("an eased segment is sampled, not left as two keys"), Channel->GetNumKeys() > 2);

	double Midpoint = 0.0;
	Channel->Evaluate(FFrameTime(FFrameNumber(12000)), Midpoint);
	TestTrue(*FString::Printf(TEXT("OutCubic is ahead of linear at the halfway point (%f > 0.5)"), Midpoint),
		Midpoint > 0.6);
	TestTrue(TEXT("and still inside the range it was given"), Midpoint <= 1.0);

	// A name the curve library does not declare is refused, and CurveFloat is refused BY NAME even
	// though the enum has it: it points at a curve asset, and a key has nowhere to put one.
	const FBuiltTree Bogus = BuildFromText({
		TEXT("timeline Slide {"),
		TEXT("    RenderScale : 0.0 = (0, 0, 0) ease Swoosh, 1.0 = (1, 1, 1)"),
		TEXT("}"),
		TEXT("Widget Root { }")
	});
	TestTrue(TEXT("an unknown ease is DUI5017"), HasCode(Bogus.Diagnostics, EDreamUIDiagnosticCode::UnknownEaseName));

	const FBuiltTree CurveAsset = BuildFromText({
		TEXT("timeline Slide {"),
		TEXT("    RenderScale : 0.0 = (0, 0, 0) ease CurveFloat, 1.0 = (1, 1, 1)"),
		TEXT("}"),
		TEXT("Widget Root { }")
	});
	TestTrue(TEXT("and so is CurveFloat, which names an asset"),
		HasCode(CurveAsset.Diagnostics, EDreamUIDiagnosticCode::UnknownEaseName));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITimelineRefusalsTest,
	"DreamGUI.Text.Timeline.EveryRefusalIsReportedUnderItsOwnCode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITimelineRefusalsTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITimelineTestLocal;

	{
		const FBuiltTree Built = BuildFromText({
			TEXT("timeline T {"),
			TEXT("    Nowhere.Width : 0.0 = 1, 0.5 = 2"),
			TEXT("}"),
			TEXT("Widget Root { }")
		});
		TestTrue(TEXT("a path naming no node is DUI5015"),
			HasCode(Built.Diagnostics, EDreamUIDiagnosticCode::TimelineTargetNotFound));
	}
	{
		// RenderOpacity is real, writable from text, and NOT marked Interp -- so the animation editor
		// does not offer it either, and a track on it would exist and never write.
		const FBuiltTree Built = BuildFromText({
			TEXT("timeline T {"),
			TEXT("    RenderOpacity : 0.0 = 0, 0.5 = 1"),
			TEXT("}"),
			TEXT("Widget Root { }")
		});
		TestTrue(TEXT("a property that is not Interp is DUI5016"),
			HasCode(Built.Diagnostics, EDreamUIDiagnosticCode::TimelinePropertyNotAnimatable));
	}
	{
		const FBuiltTree Built = BuildFromText({
			TEXT("timeline T {"),
			TEXT("    Nope : 0.0 = 1"),
			TEXT("}"),
			TEXT("Widget Root { }")
		});
		TestTrue(TEXT("a property no object on the node has is DUI5016 too"),
			HasCode(Built.Diagnostics, EDreamUIDiagnosticCode::TimelinePropertyNotAnimatable));
	}
	{
		const FBuiltTree Built = BuildFromText({
			TEXT("timeline T { }"),
			TEXT("timeline T { }"),
			TEXT("Widget Root { }")
		});
		TestTrue(TEXT("two blocks of one name is DUI3016"),
			HasCode(Built.Diagnostics, EDreamUIDiagnosticCode::DuplicateTimeline));
	}
	{
		const FBuiltTree Built = BuildFromText({
			TEXT("timeline T external { }"),
			TEXT("Widget Root { }")
		});
		TestTrue(TEXT("an external block with a body is DUI2014"),
			HasCode(Built.Diagnostics, EDreamUIDiagnosticCode::MalformedTimeline));
	}
	{
		const FBuiltTree Built = BuildFromText({
			TEXT("timeline T {"),
			TEXT("    RenderScale 0.0 = (1, 1, 1)"),
			TEXT("}"),
			TEXT("Widget Root { }")
		});
		TestTrue(TEXT("a track line missing its ':' is DUI2001 or DUI2014"),
			HasCode(Built.Diagnostics, EDreamUIDiagnosticCode::UnexpectedToken)
			|| HasCode(Built.Diagnostics, EDreamUIDiagnosticCode::MalformedTimeline));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITimelineExternalTest,
	"DreamGUI.Text.Timeline.AnExternalLineBuildsNothingAndListsWhatSequencerOwns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITimelineExternalTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITimelineTestLocal;

	const FBuiltTree Built = BuildFromText({
		TEXT("timeline Celebrate external"),
		TEXT("Widget Root { }")
	});
	if (!TestFalse(FString::Printf(TEXT("the file builds (%s)"), *Built.Diagnostics.ToString()),
		Built.Diagnostics.HasErrors()))
	{
		return false;
	}
	if (!TestEqual(TEXT("the AST records it"), Built.Ast.Timelines.Num(), 1))
	{
		return false;
	}
	TestTrue(TEXT("as external"), Built.Ast.Timelines[0].bExternal);
	TestNotNull(TEXT("and it is findable by name"), Built.Ast.FindTimeline(TEXT("Celebrate")));

	// Nothing is built, and no animation component is added: an `external` line is a MANIFEST entry.
	// Materialising an empty animation for it would be the language pretending to own the thing it
	// has just said it does not.
	TestNull(TEXT("nothing was materialised for it"), FindAnimation(Built, TEXT("Celebrate")));
	if (TestTrue(TEXT("the tree built"), Built.Tree.IsValid()))
	{
		TestNull(TEXT("and the root grew no animation component"),
			Built.Tree->RootWidget->GetComponent<UDreamWidgetAnimationComponent>());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITimelineCompileTest,
	"DreamGUI.Text.Timeline.ACompiledTimelineSurvivesRecompilationAndKeepsItsEventKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITimelineCompileTest::RunTest(const FString& Parameters)
{
	using namespace DreamUITimelineTestLocal;

	FScopedDuiFile File(TEXT("TimelineFixture.dui"));
	if (!TestTrue(TEXT("Fixture written"), File.Write({
		TEXT("class /Temp/DreamGUITests/BP_TimelineFixture"),
		TEXT("timeline Pulse {"),
		TEXT("    duration = 0.5"),
		TEXT("    RenderScale : 0.0 = (1, 1, 1), 0.5 = (1.2, 1.2, 1)"),
		TEXT("    @0.25 -> Landed"),
		TEXT("}"),
		TEXT("Widget Root { }")})))
	{
		return false;
	}
	FScopedBlueprint Fixture(TEXT("BP_TimelineFixture"));
	if (!TestTrue(TEXT("Blueprint created"), Fixture.Blueprint != nullptr)
		|| !TestTrue(TEXT("Path set"), Fixture.SetDuiFilePath(File.FilePath)))
	{
		return false;
	}

	FCompilerResultsLog FirstResults;
	Compile(Fixture.Blueprint, FirstResults);
	TestEqual(TEXT("the timeline compile has no errors"), FirstResults.NumErrors, 0);

	UDreamWidgetAnimationComponent* Animator = AnimatorOf(Fixture.Blueprint);
	if (!TestNotNull(TEXT("the compiled tree has an animation component"), Animator))
	{
		return false;
	}
	TestEqual(TEXT("holding exactly the one animation the file declares"), Animator->GetSequenceArray().Num(), 1);
	UDreamWidgetAnimation* Animation = Animator->GetSequenceByDisplayName(TEXT("Pulse"));
	if (!TestNotNull(TEXT("named after the block"), Animation))
	{
		return false;
	}

	// The `@` line: one unbound event row carrying the name. The proposal's ruling ③, and the reason
	// the right side is a NAME rather than a function -- the runtime track broadcasts it and anything
	// may listen, which is exactly why the row is bound to nothing.
	int32 EventTracks = 0;
	for (UMovieSceneTrack* Track : Animation->GetMovieScene()->GetTracks())
	{
		EventTracks += Cast<UDreamUIAnimEventTrack>(Track) != nullptr ? 1 : 0;
	}
	TestEqual(TEXT("the '@' line made one event track"), EventTracks, 1);

	// THE CARRY. A second compile must leave ONE animation, not two: the file rebuilds the
	// language-owned one, so carrying the previous compile's copy would both duplicate the name and
	// put the older content back.
	FCompilerResultsLog SecondResults;
	Compile(Fixture.Blueprint, SecondResults);
	TestEqual(TEXT("the recompile has no errors"), SecondResults.NumErrors, 0);

	UDreamWidgetAnimationComponent* Rebuilt = AnimatorOf(Fixture.Blueprint);
	if (!TestNotNull(TEXT("the rebuilt tree still animates"), Rebuilt))
	{
		return false;
	}
	TestEqual(TEXT("still exactly one animation, not one per compile"), Rebuilt->GetSequenceArray().Num(), 1);
	TestNotNull(TEXT("under the same name"), Rebuilt->GetSequenceByDisplayName(TEXT("Pulse")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
