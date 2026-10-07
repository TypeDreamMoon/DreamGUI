// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamUIAnimationLibrary.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Animation/DreamWidgetAnimationObjectReference.h"
#include "Animation/DreamWidgetAnimationPlayer.h"
#include "Controls/DreamButton.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamTextUserWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Demo/DreamUIShowcase.h"
#include "DreamWidgetBlueprint.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Compilation/MovieSceneCompiledDataManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Evaluation/MovieSceneEvaluationField.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneSequencePlayer.h"
#include "MovieSceneTimeController.h"
#include "MovieSceneTrack.h"
#include "MovieSceneTrackEvaluationField.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Sections/MovieSceneVectorSection.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "Core/DreamUIGoneCount.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/FrameTime.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

/*
 * Playback, end to end: a widget tree in a world that ticks, an animation bound to one of its
 * widgets, PlayAnimation, world ticks, and then the widget's property is read back.
 *
 * Every other animation test in this plugin stops short of the tick. The binding tests resolve
 * against an editor world that never advances, the sequence-player tests are the frame-by-frame
 * image players, and the editor's own scrubbing goes through FSequencer rather than the runtime
 * player. That gap is how a whole class of track could fail to write anything at runtime while
 * every existing test stayed green (2026-09-03: the FVector tracks -- every translate and scale
 * a widget has -- while float and rotator tracks on the same widget worked). These tests are
 * the oracle for that: one float track, one vector track, the same play, the same ticks.
 *
 * The tick is LEVELTICK_TimeOnly: it advances the world clock and broadcasts the sequence tick
 * -- the two things the movie-scene tick manager needs -- without ticking actors a test world
 * does not have. Keys are linear so the expected value at a frame is arithmetic.
 */
namespace DreamWidgetAnimationPlaybackTestLocal
{
	constexpr int32 FramesPerSecond = 30;
	constexpr int32 TicksPerFrame = 24000 / FramesPerSecond;
	constexpr int32 AnimationFrames = 20;
	constexpr int32 LastKeyFrame = 15;
	constexpr float FrameSeconds = 1.0f / FramesPerSecond;

	/**
	 * A game world WITH a world context. UWorld::Tick asks the engine about seamless travel
	 * through the world's context, and a world made by CreateWorld alone has none, so the first
	 * tick asserts; the other fixtures in this plugin never tick and never notice.
	 */
	struct FScopedGameWorld
	{
		UWorld* World = nullptr;
		FScopedGameWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
		}
		~FScopedGameWorld()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	UDreamWidget* MakeWidget(UWorld* World, const TCHAR* DisplayName, UDreamWidget* Parent)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Public | RF_Transactional);
		Widget->SetDisplayName(DisplayName);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(40.0f);
		if (Parent != nullptr)
		{
			Widget->TrySetParent(Parent, false);
		}
		Widget->OnRegister();
		return Widget;
	}

	/** A root with one child, an animation component on the root, one animation bound to the child. */
	struct FScopedTree
	{
		UDreamWidget* Root = nullptr;
		UDreamWidget* Button = nullptr;
		UDreamWidgetAnimationComponent* Animator = nullptr;
		UDreamWidgetAnimation* Animation = nullptr;
		FGuid ButtonGuid;

		explicit FScopedTree(UWorld* World)
		{
			Root = MakeWidget(World, TEXT("Root"), nullptr);
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);
			Button = MakeWidget(World, TEXT("ButtonA"), Root);

			Animator = Root->AddComponent<UDreamWidgetAnimationComponent>();
			Animation = Animator->AddNewAnimation();

			UMovieScene* MovieScene = Animation->GetMovieScene();
			MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
			MovieScene->SetDisplayRate(FFrameRate(FramesPerSecond, 1));
			MovieScene->SetPlaybackRange(FFrameNumber(0), AnimationFrames * TicksPerFrame);

			ButtonGuid = MovieScene->AddPossessable(TEXT("ButtonA"), UDreamWidget::StaticClass());
			// Bound against the ROOT, which is what the runtime resolves from: the component's widget.
			Animation->BindPossessableObject(ButtonGuid, *Button, Root);
		}

		~FScopedTree()
		{
			// The test is the owner. Instances stopped first so no player outlives its widget.
			if (IsValid(Animator))
			{
				Animator->StopAllAnimations();
			}
			if (IsValid(Root))
			{
				Root->DestroyWidget();
			}
		}

		FScopedTree(const FScopedTree&) = delete;
		FScopedTree& operator=(const FScopedTree&) = delete;

		/** A float track on the child, linear from `From` at frame 0 to `To` at the last key frame. */
		void AddFloatTrack(FName PropertyName, float From, float To)
		{
			UMovieScene* MovieScene = Animation->GetMovieScene();
			UMovieSceneFloatTrack* Track = MovieScene->AddTrack<UMovieSceneFloatTrack>(ButtonGuid);
			Track->SetPropertyNameAndPath(PropertyName, PropertyName.ToString());
			UMovieSceneFloatSection* Section = CastChecked<UMovieSceneFloatSection>(Track->CreateNewSection());
			Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(AnimationFrames * TicksPerFrame)));
			TArrayView<FMovieSceneFloatChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();
			Channels[0]->AddLinearKey(FFrameNumber(0), From);
			Channels[0]->AddLinearKey(FFrameNumber(LastKeyFrame * TicksPerFrame), To);
			Track->AddSection(*Section);
		}

		/** A three-channel vector track on the child, linear from `From` at frame 0 to `To` at the last key frame. */
		void AddVectorTrack(FName PropertyName, const FVector& From, const FVector& To)
		{
			UMovieScene* MovieScene = Animation->GetMovieScene();
			UMovieSceneDoubleVectorTrack* Track = MovieScene->AddTrack<UMovieSceneDoubleVectorTrack>(ButtonGuid);
			Track->SetPropertyNameAndPath(PropertyName, PropertyName.ToString());
			Track->SetNumChannelsUsed(3);
			UMovieSceneDoubleVectorSection* Section = CastChecked<UMovieSceneDoubleVectorSection>(Track->CreateNewSection());
			Section->SetRange(TRange<FFrameNumber>(FFrameNumber(0), FFrameNumber(AnimationFrames * TicksPerFrame)));
			TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			const double FromValues[3] = { From.X, From.Y, From.Z };
			const double ToValues[3] = { To.X, To.Y, To.Z };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				Channels[Index]->AddLinearKey(FFrameNumber(0), FromValues[Index]);
				Channels[Index]->AddLinearKey(FFrameNumber(LastKeyFrame * TicksPerFrame), ToValues[Index]);
			}
			Track->AddSection(*Section);
		}
	};

	/**
	 * The gallery's button, exactly as the designer authored it: a UDreamButton (a control, so a
	 * nested user widget) as the bound object, an open-ended section, auto-tangent keys, and only
	 * the channel that moves carrying two keys. Everything the plain fixture simplifies away.
	 */
	struct FScopedControlTree
	{
		UDreamWidget* Root = nullptr;
		UDreamButton* Button = nullptr;
		UDreamWidgetAnimationComponent* Animator = nullptr;
		UDreamWidgetAnimation* Animation = nullptr;
		FGuid ButtonGuid;

		explicit FScopedControlTree(UWorld* World)
		{
			Root = MakeWidget(World, TEXT("Root"), nullptr);
			Root->SetWidth(400.0f);
			Root->SetHeight(300.0f);

			Button = NewObject<UDreamButton>(World, NAME_None, RF_Public | RF_Transactional);
			Button->SetDisplayName(TEXT("ButtonA"));
			Button->Initialize();
			Button->SetWidth(100.0f);
			Button->SetHeight(40.0f);
			Button->TrySetParent(Root, false);
			Button->OnRegister();

			Animator = Root->AddComponent<UDreamWidgetAnimationComponent>();
			Animation = Animator->AddNewAnimation();

			UMovieScene* MovieScene = Animation->GetMovieScene();
			MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
			MovieScene->SetDisplayRate(FFrameRate(FramesPerSecond, 1));
			MovieScene->SetPlaybackRange(FFrameNumber(0), AnimationFrames * TicksPerFrame);

			ButtonGuid = MovieScene->AddPossessable(TEXT("ButtonA"), UDreamButton::StaticClass());
			Animation->BindPossessableObject(ButtonGuid, *Button, Root);

			UMovieSceneDoubleVectorTrack* Track = MovieScene->AddTrack<UMovieSceneDoubleVectorTrack>(ButtonGuid);
			Track->SetPropertyNameAndPath(TEXT("RenderTranslation"), TEXT("RenderTranslation"));
			Track->SetNumChannelsUsed(3);
			UMovieSceneDoubleVectorSection* Section = CastChecked<UMovieSceneDoubleVectorSection>(Track->CreateNewSection());
			Section->SetRange(TRange<FFrameNumber>::All());
			TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
			Channels[0]->AddCubicKey(FFrameNumber(0), 0.0);
			Channels[1]->AddCubicKey(FFrameNumber(0), -100.0);
			Channels[1]->AddCubicKey(FFrameNumber(LastKeyFrame * TicksPerFrame), 0.0);
			Channels[2]->AddCubicKey(FFrameNumber(0), 0.0);
			Track->AddSection(*Section);
		}

		~FScopedControlTree()
		{
			if (IsValid(Animator))
			{
				Animator->StopAllAnimations();
			}
			if (IsValid(Root))
			{
				Root->DestroyWidget();
			}
		}

		FScopedControlTree(const FScopedControlTree&) = delete;
		FScopedControlTree& operator=(const FScopedControlTree&) = delete;
	};

	void TickFrames(UWorld* World, int32 Frames)
	{
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			World->Tick(LEVELTICK_TimeOnly, FrameSeconds);
		}
	}

	/** The linear ramp's value at a frame, for an expectation that reads like the key data. */
	float RampAt(float From, float To, int32 Frame)
	{
		return From + (To - From) * FMath::Clamp(static_cast<float>(Frame) / LastKeyFrame, 0.0f, 1.0f);
	}

	/** Where the gallery's ButtonA sits, spelled as the binding path an animation on the root records. */
	const TCHAR* const GalleryButtonPath = TEXT("Gallery/GalleryBody/Content/Body/ClickColumn/ButtonA");

	/** The name the slide-in is given in the animation editor, and the one the gallery's .dui lists. */
	const TCHAR* const GallerySlideInName = TEXT("SlideIn");

	/**
	 * The controls gallery's .dui, cut down to the column ButtonA stands in.
	 *
	 * Every node, container and slot line on the way down to ButtonA is the project gallery's own, and
	 * nothing beside them is. The siblings change where ButtonA is laid out, never what its render
	 * translation is, and each line kept is one more way for the fixture to fail for a reason that has
	 * nothing to do with the slide-in. The containers stay because they are the part that COULD matter:
	 * a layout pass that wrote the render transform of what it arranges would kill the gallery's
	 * slide-in, and a fixture without them would never see it.
	 *
	 * bInListsSlideIn adds the `external` manifest line, which an author writes once the animation
	 * exists. Listing an animation the class does not have, and having one the file does not list, are
	 * both compile warnings -- so each compile gets the file in the state an author would have it in.
	 */
	TArray<FString> GallerySourceLines(const FString& InClassPath, bool bInListsSlideIn)
	{
		TArray<FString> Lines;
		// Naming the package the class is compiled into, so the compiler has no mismatch to warn about.
		Lines.Add(FString::Printf(TEXT("class %s"), *InClassPath));
		if (bInListsSlideIn)
		{
			Lines.Add(FString::Printf(TEXT("timeline %s external"), GallerySlideInName));
		}
		Lines.Append({
			TEXT("Widget Root {"),
			TEXT("    AnchorData.AnchorMin = (0, 0)"),
			TEXT("    AnchorData.AnchorMax = (1, 1)"),
			TEXT("    AnchorData.SizeDelta = (0, 0)"),
			TEXT("    + Overlay {}"),
			TEXT("    Widget Gallery {"),
			TEXT("        @slot HorizontalAlignment = Fill"),
			TEXT("        @slot VerticalAlignment   = Fill"),
			TEXT("        + SizeBox {"),
			TEXT("            bOverrideWidth  = true"),
			TEXT("            WidthOverride   = 960"),
			TEXT("            bOverrideHeight = true"),
			TEXT("            HeightOverride  = 1100"),
			TEXT("        }"),
			TEXT("        Widget GalleryBody {"),
			TEXT("            + Overlay {}"),
			TEXT("            Widget Content {"),
			TEXT("                @slot HorizontalAlignment = Fill"),
			TEXT("                @slot VerticalAlignment   = Fill"),
			TEXT("                @slot Padding = (24, 20, 24, 16)"),
			TEXT("                + VerticalBox {"),
			TEXT("                    Spacing = 10"),
			TEXT("                }"),
			TEXT("                Widget Body {"),
			TEXT("                    @slot HorizontalAlignment = Fill"),
			TEXT("                    @slot SizeRule = Fill"),
			TEXT("                    + HorizontalBox {"),
			TEXT("                        Spacing = 18"),
			TEXT("                    }"),
			TEXT("                    Widget ClickColumn {"),
			TEXT("                        @slot SizeRule = Fill"),
			TEXT("                        @slot VerticalAlignment = Fill"),
			TEXT("                        + VerticalBox {"),
			TEXT("                            Spacing = 8"),
			TEXT("                        }"),
			TEXT("                        Native.Button ButtonA {"),
			TEXT("                            @slot HorizontalAlignment = Fill"),
			TEXT("                            @slot SizeRule = Auto"),
			TEXT("                        }"),
			TEXT("                    }"),
			TEXT("                }"),
			TEXT("            }"),
			TEXT("        }"),
			TEXT("    }"),
			TEXT("}")
		});
		return Lines;
	}

	/**
	 * A Blueprint over the gallery's native base, its hierarchy read from a .dui written to Saved/,
	 * the file gone and the package released when the test leaves.
	 *
	 * UDreamUIControlsGalleryPanel because that is the class the project's gallery derives from, so
	 * its NativeOnInitialized runs as the instance comes alive, exactly as the gallery's does.
	 *
	 * The name carries a GUID. CreateBlueprint asserts that the package holds no Blueprint of that name
	 * yet, and a Blueprint is Standalone, so it outlives the test -- with a fixed name, running this a
	 * second time in one editor session would take the editor down on that assertion.
	 *
	 * Saved/ rather than a DUI root, as for every other .dui fixture in this module: nothing watches
	 * it, so rewriting the file between the two compiles queues no rebuild of its own.
	 */
	struct FScopedGalleryClass
	{
		FScopedGalleryClass()
		{
			const FString UniqueName = FString::Printf(TEXT("BP_GallerySlideIn_%s"),
				*FGuid::NewGuid().ToString(EGuidFormats::Digits));
			PackageName = FString::Printf(TEXT("/Temp/DreamGUITests/%s"), *UniqueName);
			SourcePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(
				FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), UniqueName + TEXT(".dui")));
			FPaths::NormalizeFilename(SourcePath);

			Package = CreatePackage(*PackageName);
			if (Package == nullptr)
			{
				return;
			}
			Package->AddToRoot();
			Blueprint = Cast<UDreamWidgetBlueprint>(FKismetEditorUtilities::CreateBlueprint(
				UDreamUIControlsGalleryPanel::StaticClass(), Package, FName(*UniqueName), BPTYPE_Normal,
				UDreamWidgetBlueprint::StaticClass(), UDreamWidgetGeneratedClass::StaticClass()));
		}

		~FScopedGalleryClass()
		{
			// Quiet and EvenReadOnly: a leftover file is read by nobody, but it is litter in Saved/.
			IFileManager::Get().Delete(*SourcePath, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
			if (Package != nullptr)
			{
				Package->RemoveFromRoot();
			}
		}

		FScopedGalleryClass(const FScopedGalleryClass&) = delete;
		FScopedGalleryClass& operator=(const FScopedGalleryClass&) = delete;

		bool WriteSource(bool bInListsSlideIn) const
		{
			return FFileHelper::SaveStringToFile(
				FString::Join(GallerySourceLines(PackageName, bInListsSlideIn), TEXT("\n")), *SourcePath);
		}

		/**
		 * Point the class at its .dui, the way the Class Defaults panel does: on the CDO, because
		 * SourceFile is a class default. CreateBlueprint has compiled once already, so there is a CDO to
		 * write to, and every compile after copies the value onto the CDO it makes.
		 */
		bool PointAtSource() const
		{
			UDreamTextUserWidget* Defaults = Blueprint != nullptr && Blueprint->GeneratedClass != nullptr
				? Cast<UDreamTextUserWidget>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
			if (Defaults == nullptr)
			{
				return false;
			}
			Defaults->SourceFile.FilePath = SourcePath;
			return true;
		}

		void Compile(FCompilerResultsLog& OutResults) const
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &OutResults);
		}

		FString PackageName;
		FString SourcePath;
		UPackage* Package = nullptr;
		UDreamWidgetBlueprint* Blueprint = nullptr;
	};

	/**
	 * The slide-in, made on the gallery's AUTHORED hierarchy the way the animation editor makes it.
	 *
	 * The editor's own steps in its order -- SDreamWidgetAnimationEditor::EnsureAnimationHost and
	 * OnNewAnimationClicked, then Sequencer adding ButtonA's track: the component goes on the authored
	 * root, the animation is a new one on it, ButtonA is possessed under its display name and bound
	 * against the root, which records the path from the root -- the only part of the binding that
	 * still means anything once the text rebuild has replaced every widget. The section is the shape
	 * Sequencer leaves and the grammar cannot write: open-ended, auto-tangent keys, two keys on the
	 * channel that moves and one on each that does not. FScopedControlTree above carries the same keys
	 * against a hand-built tree.
	 *
	 * Null when the hierarchy is not the one the source describes; OutWhyNot says which part is not.
	 */
	UDreamWidgetAnimation* AuthorGallerySlideIn(UDreamWidgetBlueprint* InBlueprint, FString& OutWhyNot)
	{
		UDreamWidget* AuthoredRoot = IsValid(InBlueprint) && IsValid(InBlueprint->WidgetTree)
			? InBlueprint->WidgetTree->RootWidget.Get() : nullptr;
		if (!IsValid(AuthoredRoot))
		{
			OutWhyNot = TEXT("the Blueprint has no authored root after its first compile");
			return nullptr;
		}
		UDreamWidget* AuthoredButton = FDreamWidgetAnimationObjectReference::GetWidgetFromContextWidgetByRelativePath(
			AuthoredRoot, GalleryButtonPath);
		if (!IsValid(AuthoredButton))
		{
			OutWhyNot = FString::Printf(TEXT("the authored hierarchy has nothing at '%s'"), GalleryButtonPath);
			return nullptr;
		}

		UDreamWidgetAnimationComponent* Animator = AuthoredRoot->GetComponent<UDreamWidgetAnimationComponent>();
		if (Animator == nullptr)
		{
			Animator = AuthoredRoot->AddComponent<UDreamWidgetAnimationComponent>();
		}
		UDreamWidgetAnimation* Animation = Animator != nullptr ? Animator->AddNewAnimation() : nullptr;
		if (!IsValid(Animation) || Animation->GetMovieScene() == nullptr)
		{
			OutWhyNot = TEXT("the authored root would not take an animation");
			return nullptr;
		}
		Animation->SetDisplayNameString(GallerySlideInName);

		UMovieScene* MovieScene = Animation->GetMovieScene();
		MovieScene->SetTickResolutionDirectly(FFrameRate(24000, 1));
		MovieScene->SetDisplayRate(FFrameRate(FramesPerSecond, 1));
		MovieScene->SetPlaybackRange(FFrameNumber(0), AnimationFrames * TicksPerFrame);

		const FGuid ButtonGuid = MovieScene->AddPossessable(AuthoredButton->GetDisplayName(), AuthoredButton->GetClass());
		Animation->BindPossessableObject(ButtonGuid, *AuthoredButton, AuthoredRoot);

		UMovieSceneDoubleVectorTrack* Track = MovieScene->AddTrack<UMovieSceneDoubleVectorTrack>(ButtonGuid);
		Track->SetPropertyNameAndPath(TEXT("RenderTranslation"), TEXT("RenderTranslation"));
		Track->SetNumChannelsUsed(3);
		UMovieSceneDoubleVectorSection* Section = CastChecked<UMovieSceneDoubleVectorSection>(Track->CreateNewSection());
		Section->SetRange(TRange<FFrameNumber>::All());
		TArrayView<FMovieSceneDoubleChannel*> Channels = Section->GetChannelProxy().GetChannels<FMovieSceneDoubleChannel>();
		if (Channels.Num() < 3)
		{
			OutWhyNot = FString::Printf(TEXT("the vector section offers %d channels rather than three"), Channels.Num());
			return nullptr;
		}
		Channels[0]->AddCubicKey(FFrameNumber(0), 0.0);
		Channels[1]->AddCubicKey(FFrameNumber(0), -100.0);
		Channels[1]->AddCubicKey(FFrameNumber(LastKeyFrame * TicksPerFrame), 0.0);
		Channels[2]->AddCubicKey(FFrameNumber(0), 0.0);
		Track->AddSection(*Section);
		return Animation;
	}

	/** Everything a compile said, for the one line a failure prints. */
	FString JoinCompilerMessages(const FCompilerResultsLog& InResults)
	{
		FString All;
		for (const TSharedRef<FTokenizedMessage>& Message : InResults.Messages)
		{
			All += Message->ToText().ToString() + TEXT(" | ");
		}
		return All;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationFloatTrackPlaybackTest,
	"DreamGUI.Animation.Playback.FloatTrackWritesThroughTicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationFloatTrackPlaybackTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	Tree.Button->SetWidth(60.0f);

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("The instance reports playing"), Tree.Animator->IsAnimationPlaying(Handle));
	TestTrue(TEXT("The instance is findable by its animation"), Tree.Animator->FindAnimationInstance(Tree.Animation).Player == Handle.Player);

	TickFrames(Scope.World, 8);
	const float MidValue = Tree.Button->GetWidth();
	TestTrue(FString::Printf(TEXT("Eight frames in, the width is on the ramp (got %.2f)"), MidValue),
		MidValue > RampAt(20.0f, 220.0f, 4) && MidValue < RampAt(20.0f, 220.0f, 12));

	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("At the end the last key's value holds"), Tree.Button->GetWidth(), 220.0f, 0.01f);
	TestFalse(TEXT("A finished instance no longer reports playing"), Tree.Animator->IsAnimationPlaying(Handle));
	TestFalse(TEXT("A finished instance is released"), Tree.Animator->FindAnimationInstance(Tree.Animation).IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationVectorTrackPlaybackTest,
	"DreamGUI.Animation.Playback.VectorTrackWritesThroughTicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationVectorTrackPlaybackTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	// The slide-in every gallery button animation is: off to the side, then home.
	Tree.AddVectorTrack(TEXT("RenderTranslation"), FVector(0.0, -100.0, 0.0), FVector::ZeroVector);
	// Parked somewhere neither key names, so "never written" and "written the rest value" differ.
	Tree.Button->SetRenderTranslation(FVector(0.0, 50.0, 0.0));

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}

	TickFrames(Scope.World, 8);
	const double MidY = Tree.Button->GetRenderTranslation().Y;
	TestTrue(FString::Printf(TEXT("Eight frames in, Y is on the ramp (got %.2f)"), MidY),
		MidY > RampAt(-100.0f, 0.0f, 4) && MidY < RampAt(-100.0f, 0.0f, 12));

	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("At the end Y holds the last key"), Tree.Button->GetRenderTranslation().Y, 0.0, 0.01);
	TestFalse(TEXT("A finished instance no longer reports playing"), Tree.Animator->IsAnimationPlaying(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationTimeRangeTest,
	"DreamGUI.Animation.Playback.TimeRangeEndsEarly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationTimeRangeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	// Stop a quarter second in: frame 7.5 of a 15-frame ramp, so well short of the last key.
	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimationTimeRange(Tree.Animation, 0.0f, 0.25f);
	if (!TestTrue(TEXT("PlayAnimationTimeRange hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, AnimationFrames + 10);

	const float EndValue = Tree.Button->GetWidth();
	TestTrue(FString::Printf(TEXT("Playback stopped near the range end, not the animation's (got %.2f)"), EndValue),
		EndValue > RampAt(20.0f, 220.0f, 5) && EndValue < RampAt(20.0f, 220.0f, 11));
	TestFalse(TEXT("The instance finished at the range end"), Tree.Animator->IsAnimationPlaying(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationReverseRelativeTest,
	"DreamGUI.Animation.Playback.PlayAnimationReverseTurnsTheRunningInstanceAround",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationReverseRelativeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	const FDreamUIAnimationHandle Forward = Tree.Animator->PlayAnimationForward(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimationForward starts an instance"), Forward.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("It runs forward"), Tree.Animator->IsAnimationPlayingForward(Forward));
	TickFrames(Scope.World, 8);
	const float TurnValue = Tree.Button->GetWidth();

	const FDreamUIAnimationHandle Reverse = Tree.Animator->PlayAnimationReverse(Tree.Animation);
	TestTrue(TEXT("PlayAnimationReverse turns the SAME instance around rather than starting another"), Reverse.Player == Forward.Player);
	TestFalse(TEXT("It now runs backwards"), Tree.Animator->IsAnimationPlayingForward(Reverse));
	TickFrames(Scope.World, 4);
	const float BackValue = Tree.Button->GetWidth();
	TestTrue(FString::Printf(TEXT("Four frames later the value has come back down (%.2f -> %.2f)"), TurnValue, BackValue), BackValue < TurnValue);

	TickFrames(Scope.World, AnimationFrames);
	TestEqual(TEXT("Run back to the start, the first key's value holds"), Tree.Button->GetWidth(), 20.0f, 0.01f);
	TestFalse(TEXT("And the instance is done"), Tree.Animator->IsAnimationPlaying(Reverse));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationFinishedEventsTest,
	"DreamGUI.Animation.Playback.FinishedFiresOnNaturalEndAndOnStop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationFinishedEventsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	int32 StartedCount = 0;
	int32 FinishedCount = 0;
	UDreamWidgetAnimationPlayer* LastFinished = nullptr;
	Tree.Animator->OnInstanceStarted.AddLambda([&StartedCount](const FDreamUIAnimationHandle&) { ++StartedCount; });
	Tree.Animator->OnInstanceFinished.AddLambda([&FinishedCount, &LastFinished](const FDreamUIAnimationHandle& InHandle)
	{
		++FinishedCount;
		LastFinished = InHandle.Player;
	});

	const FDreamUIAnimationHandle First = Tree.Animator->PlayAnimation(Tree.Animation);
	TestEqual(TEXT("Started fires as the instance starts"), StartedCount, 1);
	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("Finished fires once at the natural end"), FinishedCount, 1);
	TestTrue(TEXT("For the instance that ended"), LastFinished == First.Player);

	const FDreamUIAnimationHandle Second = Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, 3);
	Tree.Animator->StopAnimation(Second);
	TestEqual(TEXT("Stop counts as finishing, the way UMG's does"), FinishedCount, 2);
	TestTrue(TEXT("For the stopped instance"), LastFinished == Second.Player);
	TestFalse(TEXT("A stopped instance is not playing"), Tree.Animator->IsAnimationPlaying(Second));
	TestFalse(TEXT("Nothing is playing any more"), Tree.Animator->IsAnyAnimationPlaying());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationPauseSeekTest,
	"DreamGUI.Animation.Playback.PauseReportsTimeAndSeekMoves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationPauseSeekTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, 6);
	const float PausedAt = Tree.Animator->PauseAnimation(Handle);
	TestTrue(TEXT("Pause reports where it paused"), PausedAt > 3.0f * FrameSeconds && PausedAt < 9.0f * FrameSeconds);
	TestTrue(TEXT("Paused reads as paused"), Tree.Animator->IsAnimationPaused(Handle));
	TestFalse(TEXT("And not as playing"), Tree.Animator->IsAnimationPlaying(Handle));
	TestTrue(TEXT("A paused instance is still a live one"), Tree.Animator->FindAnimationInstance(Tree.Animation).IsValid());

	const float PausedValue = Tree.Button->GetWidth();
	TickFrames(Scope.World, 5);
	TestEqual(TEXT("Ticks do not move a paused instance"), Tree.Button->GetWidth(), PausedValue, 0.01f);

	Tree.Animator->SetAnimationCurrentTime(Handle, 12.0f * FrameSeconds);
	Tree.Animator->FlushAnimations();
	TestEqual(TEXT("Seek reports the new time"), Tree.Animator->GetAnimationCurrentTime(Handle), 12.0f * FrameSeconds, 0.5f * FrameSeconds);

	Tree.Animator->ResumeAnimation(Handle);
	TestTrue(TEXT("Resume plays again"), Tree.Animator->IsAnimationPlaying(Handle));
	TickFrames(Scope.World, AnimationFrames);
	TestEqual(TEXT("And it runs to the end from there"), Tree.Button->GetWidth(), 220.0f, 0.01f);
	TestFalse(TEXT("Done"), Tree.Animator->IsAnimationPlaying(Handle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationControlVectorTrackTest,
	"DreamGUI.Animation.Playback.VectorTrackWritesOnAControlAsAuthored",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationControlVectorTrackTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedControlTree Tree(Scope.World);
	Tree.Button->SetRenderTranslation(FVector(0.0, 50.0, 0.0));

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}

	TickFrames(Scope.World, 8);
	const double MidY = Tree.Button->GetRenderTranslation().Y;
	TestTrue(FString::Printf(TEXT("Eight frames in, the control's Y is on its way home (got %.2f)"), MidY), MidY > -99.0 && MidY < -1.0);

	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("At the end Y holds the last key"), Tree.Button->GetRenderTranslation().Y, 0.0, 0.01);
	return true;
}

/*
 * The mechanism behind the gallery's dead slide-in, in the plugin's own terms: the same animation
 * reached by template instancing -- NewObject with an existing animation as the template, which
 * is exactly how a class hands its animations to each instance. Before the fix the copy's vector
 * section arrived with its keys and an EMPTY channel proxy, and the compiler, which counts a
 * property section's channels through that proxy before emitting an entity for it, emitted none.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationTemplateInstancingTest,
	"DreamGUI.Animation.Playback.VectorTrackSurvivesTemplateInstancing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationTemplateInstancingTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddVectorTrack(TEXT("RenderTranslation"), FVector(0.0, -100.0, 0.0), FVector::ZeroVector);

	// The instance's copy, made the way UDreamWidgetGeneratedClass makes one.
	UDreamWidgetAnimation* Copy = NewObject<UDreamWidgetAnimation>(Tree.Animator, NAME_None, RF_Transactional, Tree.Animation);
	if (!TestNotNull(TEXT("Template instancing produced a copy"), Copy))
	{
		return false;
	}
	TestTrue(TEXT("The copy's movie scene is its own"), Copy->GetMovieScene() != Tree.Animation->GetMovieScene());

	int32 VectorSections = 0;
	for (UMovieSceneSection* Section : Copy->GetMovieScene()->GetAllSections())
	{
		if (UMovieSceneDoubleVectorSection* VectorSection = Cast<UMovieSceneDoubleVectorSection>(Section))
		{
			++VectorSections;
			TestEqual(TEXT("The copy's vector section has a channel proxy with its three channels"), VectorSection->GetChannelProxy().NumChannels(), 3);
		}
	}
	TestEqual(TEXT("One vector section came across"), VectorSections, 1);

	Tree.Button->SetRenderTranslation(FVector(0.0, 50.0, 0.0));
	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Copy);
	if (!TestTrue(TEXT("The copy plays"), Handle.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, 8);
	const double MidY = Tree.Button->GetRenderTranslation().Y;
	TestTrue(FString::Printf(TEXT("Eight frames in, the copy has moved the button (Y = %.2f)"), MidY), MidY > -99.0 && MidY < -1.0);
	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("At the end Y holds the last key"), Tree.Button->GetRenderTranslation().Y, 0.0, 0.01);
	TestFalse(TEXT("A finished handle reads as invalid"), Handle.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationNegativeLoopCountTest,
	"DreamGUI.Animation.Playback.ANegativeLoopCountLoopsForeverRatherThanPlayingOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationNegativeLoopCountTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	// Zero is UMG's spelling of "for ever" and the one this API documents; -1 is what every tweening
	// library in the world takes for it, and the arithmetic used to turn -1 into "no additional
	// loops" -- a single play, the exact opposite of what was asked, and nothing said so.
	const FDreamUIAnimationHandle Endless = Tree.Animator->PlayAnimation(Tree.Animation, 0.0f, -1);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Endless.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, AnimationFrames * 3);
	TestTrue(TEXT("three times its own length later, an animation asked to loop for ever still plays"),
		Tree.Animator->IsAnimationPlaying(Endless));
	Tree.Animator->StopAnimation(Endless);

	// Zero still means the same thing, and one still means one.
	const FDreamUIAnimationHandle Zero = Tree.Animator->PlayAnimation(Tree.Animation, 0.0f, 0);
	TickFrames(Scope.World, AnimationFrames * 3);
	TestTrue(TEXT("zero loops for ever too"), Tree.Animator->IsAnimationPlaying(Zero));
	Tree.Animator->StopAnimation(Zero);

	const FDreamUIAnimationHandle Once = Tree.Animator->PlayAnimation(Tree.Animation, 0.0f, 1);
	TickFrames(Scope.World, AnimationFrames + 5);
	TestFalse(TEXT("and one play is still one play"), Tree.Animator->IsAnimationPlaying(Once));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationMovedWidgetTest,
	"DreamGUI.Animation.Playback.AWidgetDraggedToAnotherParentIsStillDrivenByItsAnimation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationMovedWidgetTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	Tree.Button->SetWidth(20.0f);

	// The binding was recorded as the path "ButtonA" from the root. Dragging the button under a new
	// panel makes that path unwalkable -- and a binding path is display names all the way down, so
	// every segment above a moved widget goes wrong at once while the widget itself is untouched.
	UDreamWidget* Panel = MakeWidget(Scope.World, TEXT("Panel"), Tree.Root);
	Tree.Button->TrySetParent(Panel, false);

	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, 8);
	TestTrue(FString::Printf(TEXT("the moved widget is still animated (width %.2f)"), Tree.Button->GetWidth()),
		Tree.Button->GetWidth() > 25.0f);
	Tree.Animator->StopAnimation(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationUnscaledTimeTest,
	"DreamGUI.Animation.Playback.AnAnimationToldToIgnoreTimeDilationKeepsItsOwnSpeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationUnscaledTimeTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);

	AWorldSettings* Settings = Scope.World->GetWorldSettings();
	if (!TestNotNull(TEXT("the test world has settings to dilate"), Settings))
	{
		return false;
	}
	// A quarter-speed world: the delta the sequence tick manager hands out is already multiplied by
	// this, which is why an animation cannot help but follow it without being told otherwise.
	Settings->TimeDilation = 0.25f;

	Tree.Animator->SetAffectedByTimeDilation(true);
	const FDreamUIAnimationHandle Slowed = Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, 8);
	const float SlowedWidth = Tree.Button->GetWidth();
	Tree.Animator->StopAnimation(Slowed);

	Tree.Button->SetWidth(20.0f);
	Tree.Animator->SetAffectedByTimeDilation(false);
	const FDreamUIAnimationHandle Unscaled = Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, 8);
	const float UnscaledWidth = Tree.Button->GetWidth();
	Tree.Animator->StopAnimation(Unscaled);

	TestTrue(FString::Printf(TEXT("the same eight frames carry the unscaled animation further than the dilated one (%.2f vs %.2f)"), UnscaledWidth, SlowedWidth),
		UnscaledWidth > SlowedWidth + 1.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationWeightTest,
	"DreamGUI.Animation.Playback.AnInstanceAtZeroWeightLeavesTheWidgetNearerWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationWeightTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	// The blend channel a weight is applied through; without it the instance simply overwrites and a
	// weight has nothing to scale.
	Tree.Animator->SetDynamicWeighting(true);
	Tree.Button->SetWidth(20.0f);

	const FDreamUIAnimationHandle Full = Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, 8);
	const float FullWidth = Tree.Button->GetWidth();
	Tree.Animator->StopAnimation(Full);

	Tree.Button->SetWidth(20.0f);
	const FDreamUIAnimationHandle Weighted = Tree.Animator->PlayAnimation(Tree.Animation);
	Tree.Animator->SetAnimationWeight(Weighted, 0.0f);
	TickFrames(Scope.World, 8);
	const float WeightedWidth = Tree.Button->GetWidth();

	// Asserted as a comparison rather than an exact value: how far a zero weight pulls the result
	// back towards the widget's own value is the engine's blending arithmetic, and what this is here
	// to catch is a weight that is not applied AT ALL -- which reads as the two runs agreeing.
	TestTrue(FString::Printf(TEXT("a weighted instance writes something other than the unweighted one (%.2f vs %.2f)"), WeightedWidth, FullWidth),
		FMath::Abs(WeightedWidth - 20.0f) < FMath::Abs(FullWidth - 20.0f));

	// And clearing it is safe on a live instance, which is the other half of the pair.
	Tree.Animator->ClearAnimationWeight(Weighted);
	TickFrames(Scope.World, 2);
	TestTrue(TEXT("the instance survives having its weight cleared"), Tree.Animator->IsAnimationPlaying(Weighted));
	Tree.Animator->StopAnimation(Weighted);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationComponentOnUserWidgetItselfTest,
	"DreamGUI.Animation.AnAnimationComponentOnTheUserWidgetItselfIsOneOfItsAnimationComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationComponentOnUserWidgetItselfTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FScopedGameWorld Scope;

	UDreamUserWidget* Instance = CreateDreamWidget(Scope.World, UDreamUserWidget::StaticClass());
	if (!TestNotNull(TEXT("a user widget instantiates"), Instance))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		Instance->StopAllAnimations();
		Instance->DestroyWidget();
	};

	// The component sits on the user widget ITSELF, which the component supports on purpose
	// (UDreamWidgetAnimationComponent::GetOwningUserWidget goes out of its way to recognise it) --
	// but the collection walked from the content root, which is the root of the tree the widget
	// BUILDS and never the widget. So PlayAnimation reached this component through the outer chain
	// while StopAllAnimations, IsAnyAnimationPlaying, FlushAnimations and PlayAnimationByName all
	// walked straight past it.
	UDreamWidgetAnimationComponent* SelfAnimator = Instance->AddComponent<UDreamWidgetAnimationComponent>();
	if (!TestNotNull(TEXT("the component is added to the user widget"), SelfAnimator))
	{
		return false;
	}
	UDreamWidgetAnimation* Animation = SelfAnimator->AddNewAnimation();
	Animation->SetDisplayNameString(TEXT("FadeIn"));

	TArray<UDreamWidgetAnimationComponent*> Collected;
	Instance->CollectAnimationComponents(Collected);
	TestTrue(TEXT("the widget's own component is one of its animation components"), Collected.Contains(SelfAnimator));
	TestTrue(TEXT("and its animations answer to their names"),
		Instance->GetAnimationByName(TEXT("FadeIn")) == static_cast<UMovieSceneSequence*>(Animation));
	TestNull(TEXT("a name nothing carries is still nothing"), Instance->GetAnimationByName(TEXT("NoSuchAnimation")));
	return true;
}

/*
 * The controls gallery's ButtonA sliding in: the report that started the 2026-09-03 review, end to
 * end, with the gallery rebuilt from the plugin's own parts so that every project runs it.
 *
 * It used to load the project's /Game/UI/WBP_ControlsGallery, and to pass without checking anything
 * wherever that asset was missing -- every project but one, including the isolated host the suite is
 * meant to run in. What it stands for was never that asset but the road a real gallery's animation
 * travels, so that road is what the fixture keeps:
 *
 *   - a Blueprint over UDreamUIControlsGalleryPanel, the gallery's native base, whose hierarchy is
 *     compiled from a .dui: the gallery's own nodes, containers and slot lines down to ButtonA, a
 *     native button -- a control, so a hierarchy of its own -- six levels below the root;
 *   - the slide-in made in the animation editor's shape on the AUTHORED hierarchy after the first
 *     compile, so the second compile carries it across the text rebuild, which re-homes it through
 *     FObjectInstancingGraph;
 *   - the class instanced with CreateDreamWidget, the way the game instances it, and the animation
 *     played through the user widget's own API.
 *
 * So the vector section goes through every copy the gallery's goes through -- carried across the
 * rebuild, duplicated onto the class as its archetype, instanced per widget -- and a copy, not the
 * track, is where the 2026-09-03 defect lived. The trip to disk is the one thing left out: the gallery
 * came off disk, but a widget Blueprint recompiles on load (UDreamWidgetBlueprint::AlwaysCompileOnLoad),
 * and that compile is the one run here.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationGalleryAssetTest,
	"DreamGUI.Animation.Playback.ProjectGallery.ButtonSlidesIn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationGalleryAssetTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;

	FScopedGalleryClass Gallery;
	if (!TestNotNull(TEXT("The gallery Blueprint is created"), Gallery.Blueprint))
	{
		return false;
	}
	if (!TestTrue(TEXT("The gallery's .dui is written"), Gallery.WriteSource(/*bInListsSlideIn*/false))
		|| !TestTrue(TEXT("and the class points at it"), Gallery.PointAtSource()))
	{
		return false;
	}
	{
		FCompilerResultsLog Results;
		Gallery.Compile(Results);
		if (!TestEqual(TEXT("The gallery's hierarchy compiles from its .dui"), Results.NumErrors, 0))
		{
			AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinCompilerMessages(Results)));
			return false;
		}
	}

	FString WhyNot;
	if (AuthorGallerySlideIn(Gallery.Blueprint, WhyNot) == nullptr)
	{
		AddError(FString::Printf(TEXT("The slide-in could not be made on the gallery's hierarchy: %s"), *WhyNot));
		return false;
	}
	if (!TestTrue(TEXT("The .dui now lists the animation it does not contain"), Gallery.WriteSource(/*bInListsSlideIn*/true)))
	{
		return false;
	}
	{
		FCompilerResultsLog Results;
		Gallery.Compile(Results);
		if (!TestEqual(TEXT("The gallery recompiles with its animation"), Results.NumErrors, 0))
		{
			AddInfo(FString::Printf(TEXT("the compile said: %s"), *JoinCompilerMessages(Results)));
			return false;
		}
	}
	// The fixture's own claim, checked before anything leans on it. A carry that dropped the animation
	// would otherwise surface three steps later as "no animation component", pointing at the instance.
	{
		UDreamWidget* AuthoredRoot = IsValid(Gallery.Blueprint->WidgetTree) ? Gallery.Blueprint->WidgetTree->RootWidget.Get() : nullptr;
		UDreamWidgetAnimationComponent* CarriedAnimator = IsValid(AuthoredRoot) ? AuthoredRoot->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
		UDreamWidgetAnimation* Carried = CarriedAnimator != nullptr ? CarriedAnimator->GetSequenceByDisplayName(GallerySlideInName) : nullptr;
		if (!TestNotNull(TEXT("The slide-in rode the text rebuild onto the recompiled hierarchy"), Carried))
		{
			return false;
		}
		TestFalse(TEXT("still an animation the editor made, which is the kind a rebuild carries"), Carried->IsLanguageOwned());
	}

	UClass* GalleryClass = Gallery.Blueprint->GeneratedClass;
	FScopedGameWorld Scope;
	UDreamUserWidget* Instance = CreateDreamWidget(Scope.World, GalleryClass);
	if (!TestNotNull(TEXT("The gallery instantiates"), Instance))
	{
		return false;
	}
	ON_SCOPE_EXIT
	{
		Instance->StopAllAnimations();
		Instance->DestroyWidget();
	};

	UDreamWidget* Root = Instance->GetContentRoot();
	UDreamWidgetAnimationComponent* Animator = IsValid(Root) ? Root->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
	if (!TestNotNull(TEXT("The content root carries the animation component"), Animator)
		|| !TestTrue(TEXT("with the slide-in on it"), Animator->GetSequenceArray().Num() > 0))
	{
		return false;
	}
	UDreamWidgetAnimation* Animation = Animator->GetSequenceArray()[0];
	UDreamWidget* ButtonA = FDreamWidgetAnimationObjectReference::GetWidgetFromContextWidgetByRelativePath(Root, GalleryButtonPath);
	if (!TestNotNull(TEXT("ButtonA is where the binding path says"), ButtonA))
	{
		return false;
	}
	TestTrue(TEXT("and it is the gallery's native button, a control with a hierarchy of its own"), ButtonA->IsA<UDreamButton>());

	const FDreamUIAnimationHandle Handle = Instance->PlayAnimation(Animation);
	if (!TestTrue(TEXT("PlayAnimation through the user widget hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, 8);
	const FVector Mid = ButtonA->GetRenderTranslation();
	TestTrue(FString::Printf(TEXT("Eight frames in, ButtonA has left its rest pose (Y = %.2f)"), Mid.Y), Mid.Y < -1.0);
	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("and it has slid home: Y holds the last key"), ButtonA->GetRenderTranslation().Y, 0.0, 0.01);
	TestFalse(TEXT("The instance finished"), Instance->IsAnimationPlaying(Handle));
	return true;
}

/*
 * The player's own evaluation of plain property animations (FDreamUIDirectAnimationEvaluation): the same values as the
 * sequencer, frame by frame, and the sequencer still in charge of anything it cannot do the same way.
 */
namespace DreamWidgetAnimationPlaybackTestLocal
{
	/** Width and translation Y at every frame of one play, and whether the player evaluated it itself. */
	struct FRecordedPlay
	{
		TArray<float> Widths;
		TArray<double> TranslationsY;
		bool bDirect = false;
		float WidthAfter = 0.0f;
	};

	/** One play of a width and a translation ramp; InTurnOffAtFrame turns direct evaluation off before that frame's tick. */
	FRecordedPlay RecordPlay(int32 InDirectEvaluation, bool bInRestoreState, int32 InTurnOffAtFrame = INDEX_NONE)
	{
		const DreamTests::Lifecycle::FScopedConsoleVariable Direct(TEXT("DreamUI.Animation.DirectEvaluation"), InDirectEvaluation);
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.AddVectorTrack(TEXT("RenderTranslation"), FVector(0.0, -100.0, 0.0), FVector(0.0, 0.0, 30.0));
		Tree.Button->SetWidth(60.0f);
		FRecordedPlay Play;
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation, 0.0f, 1, EDreamUIAnimationPlayMode::Forward, 1.0f, bInRestoreState);
		for (int32 Frame = 0; Frame < AnimationFrames + 3; ++Frame)
		{
			if (Frame == InTurnOffAtFrame)
			{
				IConsoleManager::Get().FindConsoleVariable(TEXT("DreamUI.Animation.DirectEvaluation"))->Set(0, ECVF_SetByCode);
			}
			TickFrames(Scope.World, 1);
			if (Frame == 0 && Handle.Player != nullptr)
			{
				Play.bDirect = Handle.Player->IsEvaluatingDirectly();
			}
			Play.Widths.Add(Tree.Button->GetWidth());
			Play.TranslationsY.Add(Tree.Button->GetRenderTranslation().Y);
		}
		Play.WidthAfter = Tree.Button->GetWidth();
		return Play;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationDirectEvaluationMatchesTest,
	"DreamGUI.Animation.Playback.APlainPropertyAnimationIsEvaluatedByItsPlayerWithTheSequencersValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationDirectEvaluationMatchesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	const FRecordedPlay Sequencer = RecordPlay(0, false);
	const FRecordedPlay Direct = RecordPlay(1, false);
	TestFalse(TEXT("With direct evaluation off, the sequencer evaluates"), Sequencer.bDirect);
	TestTrue(TEXT("With it on, the player evaluates a float and a vector track itself"), Direct.bDirect);
	if (!TestEqual(TEXT("Both plays saw as many frames"), Direct.Widths.Num(), Sequencer.Widths.Num()))
	{
		return false;
	}
	for (int32 Frame = 0; Frame < Direct.Widths.Num(); ++Frame)
	{
		TestEqual(FString::Printf(TEXT("Frame %d: the width is the sequencer's"), Frame), Direct.Widths[Frame], Sequencer.Widths[Frame], 0.001f);
		TestEqual(FString::Printf(TEXT("Frame %d: the translation is the sequencer's"), Frame), Direct.TranslationsY[Frame], Sequencer.TranslationsY[Frame], 0.001);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationDirectEvaluationRestoresTest,
	"DreamGUI.Animation.Playback.AnAnimationPlayedToRestoreStateLeavesThePropertyAsItFoundItEitherWay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationDirectEvaluationRestoresTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	const FRecordedPlay Sequencer = RecordPlay(0, true);
	const FRecordedPlay Direct = RecordPlay(1, true);
	TestTrue(TEXT("The restoring play is evaluated by the player"), Direct.bDirect);
	TestEqual(TEXT("The sequencer puts the width back"), Sequencer.WidthAfter, 60.0f, 0.01f);
	TestEqual(TEXT("...and so does the player"), Direct.WidthAfter, Sequencer.WidthAfter, 0.01f);

	const FRecordedPlay Kept = RecordPlay(1, false);
	TestEqual(TEXT("Played to keep state, the last key's value stays"), Kept.WidthAfter, 220.0f, 0.01f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationDirectEvaluationHandOverTest,
	"DreamGUI.Animation.Playback.TurningDirectEvaluationOffMidPlayHandsTheAnimationToTheSequencerWhereItWas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationDirectEvaluationHandOverTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	for (const bool bRestoreState : { false, true })
	{
		const FRecordedPlay Sequencer = RecordPlay(0, bRestoreState);
		const FRecordedPlay HandedOver = RecordPlay(1, bRestoreState, AnimationFrames / 2);
		const TCHAR* Mode = bRestoreState ? TEXT("restoring") : TEXT("keeping");
		TestTrue(FString::Printf(TEXT("The %s play starts out evaluated by the player"), Mode), HandedOver.bDirect);
		if (!TestEqual(TEXT("Both plays saw as many frames"), HandedOver.Widths.Num(), Sequencer.Widths.Num()))
		{
			return false;
		}
		for (int32 Frame = 0; Frame < HandedOver.Widths.Num(); ++Frame)
		{
			TestEqual(FString::Printf(TEXT("%s, frame %d: the width is the sequencer's"), Mode, Frame), HandedOver.Widths[Frame], Sequencer.Widths[Frame], 0.001f);
			TestEqual(FString::Printf(TEXT("%s, frame %d: the translation is the sequencer's"), Mode, Frame), HandedOver.TranslationsY[Frame], Sequencer.TranslationsY[Frame], 0.001);
		}
		TestEqual(FString::Printf(TEXT("%s: the end state is the sequencer's"), Mode), HandedOver.WidthAfter, Sequencer.WidthAfter, 0.01f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationDirectEvaluationDeclinesTest,
	"DreamGUI.Animation.Playback.AnAnimationWithWeightsOrUnboundTracksIsLeftToTheSequencer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationDirectEvaluationDeclinesTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	{
		// Weights blend against the initial values the sequencer captures: only it can.
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.Animator->SetDynamicWeighting(true);
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
		TickFrames(Scope.World, 2);
		TestTrue(TEXT("A weighted play is live"), Handle.IsValid() && Handle.Player != nullptr);
		TestFalse(TEXT("...and evaluated by the sequencer"), Handle.Player != nullptr && Handle.Player->IsEvaluatingDirectly());
		TestTrue(TEXT("...which still writes it"), Tree.Button->GetWidth() > 20.0f && Tree.Button->GetWidth() < 220.0f);
	}
	{
		// A track bound to nothing (events, sub-sequences, time warps) is the sequencer's.
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.Animation->GetMovieScene()->AddTrack<UMovieSceneFloatTrack>();
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
		TickFrames(Scope.World, 2);
		TestFalse(TEXT("An animation with an unbound track is evaluated by the sequencer"), Handle.Player != nullptr && Handle.Player->IsEvaluatingDirectly());
		TestTrue(TEXT("...which still writes its bound track"), Tree.Button->GetWidth() > 20.0f && Tree.Button->GetWidth() < 220.0f);
	}
	return true;
}

/*
 * The Queue calls: each waits for the end of the frame's sequence tick, which is what makes it safe to call from inside an
 * animation's own Started and Finished. Every one of them used to hand the tick manager a latent action bound to no
 * object, which the tick manager asserts on as the action is added.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationQueuedCallsTest,
	"DreamGUI.Animation.Playback.QueuedCallsRunWhenTheFramesSequenceTickEnds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationQueuedCallsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	// Declared before the tree: a Finished listener below counts into it, and the tree's teardown may still finish something.
	int32 FinishedCount = 0;
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	// The tick manager runs what was queued at the end of a tick that updated a group of players, and there is a group
	// only while a player is registered with it: the component's own player, given the animation here, is one.
	Tree.Animator->InitSequencePlayer();

	Tree.Animator->QueuePlayAnimation(Tree.Animation);
	TestFalse(TEXT("A queued play has not started when the call returns"), Tree.Animator->HasPlayingAnimation(Tree.Animation));
	TickFrames(Scope.World, 1);
	const FDreamUIAnimationHandle Handle = Tree.Animator->FindAnimationInstance(Tree.Animation);
	if (!TestTrue(TEXT("It has started by the end of the frame's sequence tick"), Tree.Animator->IsAnimationPlaying(Handle)))
	{
		return false;
	}

	TickFrames(Scope.World, 3);
	Tree.Animator->QueuePauseAnimation(Handle);
	TestTrue(TEXT("A queued pause leaves the instance playing when the call returns"), Tree.Animator->IsAnimationPlaying(Handle));
	TickFrames(Scope.World, 1);
	TestTrue(TEXT("...and has paused it by the end of the frame"), Tree.Animator->IsAnimationPaused(Handle));

	Tree.Animator->QueueStopAnimation(Handle);
	TestTrue(TEXT("A queued stop leaves the instance live when the call returns"), Handle.IsValid());
	TickFrames(Scope.World, 1);
	TestFalse(TEXT("...and has ended it by the end of the frame"), Handle.IsValid());

	// What the queue is for: the next play asked for from inside the Finished of the one before.
	UDreamWidgetAnimationComponent* Animator = Tree.Animator;
	UDreamWidgetAnimation* Animation = Tree.Animation;
	Tree.Animator->OnInstanceFinished.AddLambda([&FinishedCount, Animator, Animation](const FDreamUIAnimationHandle&)
	{
		if (++FinishedCount == 1)
		{
			Animator->QueuePlayAnimation(Animation);
		}
	});
	const FDreamUIAnimationHandle First = Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, AnimationFrames + 3);
	TestEqual(TEXT("The first play finished once"), FinishedCount, 1);
	const FDreamUIAnimationHandle Second = Tree.Animator->FindAnimationInstance(Tree.Animation);
	TestTrue(TEXT("The play its Finished queued has started after it"),
		Second.IsValid() && Second.Player != First.Player && Tree.Animator->IsAnimationPlaying(Second));

	Tree.Animator->QueueStopAllAnimations();
	TestTrue(TEXT("A queued stop of everything leaves it playing when the call returns"), Tree.Animator->HasPlayingAnimation(Tree.Animation));
	TickFrames(Scope.World, 1);
	TestFalse(TEXT("...and has stopped it by the end of the frame"), Tree.Animator->HasPlayingAnimation(Tree.Animation));
	return true;
}

/*
 * The player keeping its own time (UDreamWidgetAnimationPlayer::TickFromSequenceTickManager): taken for a plain play, and
 * anything the player cannot update exactly as the sequencer's update would is left to that update.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationLitePlayerTakenTest,
	"DreamGUI.Animation.Playback.LitePlayer.APlainPlayKeepsItsOwnTimeAndAnythingElseIsLeftToTheSequencer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationLitePlayerTakenTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Lite(TEXT("DreamUI.Animation.LitePlayer"), 1);
	const DreamTests::Lifecycle::FScopedConsoleVariable Direct(TEXT("DreamUI.Animation.DirectEvaluation"), 1);
	{
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation, 0.0f, 0);
		UDreamWidgetAnimationPlayer* Player = Handle.Player;
		if (!TestTrue(TEXT("A plain play is live"), Handle.IsValid()))
		{
			return false;
		}
		TickFrames(Scope.World, 1);
		TestFalse(TEXT("The first tick of a play is the sequencer's: it starts the play"), Player->IsTickingLite());
		TickFrames(Scope.World, 3);
		TestTrue(TEXT("A plain play keeps its own time after that"), Player->IsTickingLite());
		TestTrue(TEXT("...being one its player evaluates itself"), Player->IsEvaluatingDirectly());
		// Past two loop boundaries: each is handed to the sequencer's cursor update from inside the player's own tick.
		TickFrames(Scope.World, AnimationFrames * 2);
		TestTrue(TEXT("A looping play keeps its own time across its loops"), Player->IsTickingLite() && Player->IsPlaying());

		Tree.Animator->PauseAnimation(Handle);
		TickFrames(Scope.World, 2);
		TestTrue(TEXT("A paused player has nothing to update, and skips the sequencer's update"), Player->IsTickingLite());

		// A clock set from outside the component -- through the handle's player, as game code can -- is not the one the
		// component vouched for.
		Player->SetTimeController(MakeShared<FMovieSceneTimeController_Tick>());
		Tree.Animator->ResumeAnimation(Handle);
		const float WidthAtResume = Tree.Button->GetWidth();
		TickFrames(Scope.World, 4);
		TestFalse(TEXT("A play on a clock the component did not give it goes through the sequencer's update"), Player->IsTickingLite());
		TestTrue(FString::Printf(TEXT("...which plays it on (width %.2f -> %.2f)"), WidthAtResume, Tree.Button->GetWidth()),
			Tree.Animator->IsAnimationPlaying(Handle) && !FMath::IsNearlyEqual(Tree.Button->GetWidth(), WidthAtResume, 0.01f));
	}
	{
		// The component's unscaled clock is one it vouches for too.
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.Animator->SetAffectedByTimeDilation(false);
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
		TickFrames(Scope.World, 4);
		TestTrue(TEXT("A play that ignores time dilation keeps its own time"), Handle.Player != nullptr && Handle.Player->IsTickingLite());
	}
	{
		// Weights blend in the sequencer, which evaluates them.
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.Animator->SetDynamicWeighting(true);
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
		TickFrames(Scope.World, 4);
		TestFalse(TEXT("A weighted play goes through the sequencer's update"), Handle.Player == nullptr || Handle.Player->IsTickingLite());
	}
	{
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.Animation->GetMovieScene()->AddTrack<UMovieSceneFloatTrack>();
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
		TickFrames(Scope.World, 4);
		TestFalse(TEXT("A play the sequencer evaluates goes through the sequencer's update"), Handle.Player == nullptr || Handle.Player->IsTickingLite());
	}
	{
		// A clock the animation asks for itself is Initialize's to make, not the component's.
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.Animation->GetMovieScene()->SetClockSource(EUpdateClockSource::Platform);
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
		TickFrames(Scope.World, 4);
		TestFalse(TEXT("A play on the platform clock goes through the sequencer's update"), Handle.Player == nullptr || Handle.Player->IsTickingLite());
	}
	{
		const DreamTests::Lifecycle::FScopedConsoleVariable Off(TEXT("DreamUI.Animation.LitePlayer"), 0);
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
		TickFrames(Scope.World, 4);
		TestFalse(TEXT("With DreamUI.Animation.LitePlayer 0 a plain play goes through the sequencer's update"), Handle.Player == nullptr || Handle.Player->IsTickingLite());
	}
	{
		// The component's own player, registered with the tick manager and stopped, as it sits on every component with an
		// animation of its own.
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.Animator->InitSequencePlayer();
		TickFrames(Scope.World, 2);
		UDreamWidgetAnimationPlayer* Idle = Tree.Animator->GetSequencePlayer();
		TestTrue(TEXT("A stopped player skips the sequencer's update"), Idle != nullptr && Idle->IsTickingLite());
	}
	return true;
}

/*
 * The player's own tick held to the sequencer's update: the same play run twice, with DreamUI.Animation.LitePlayer off and
 * on, and compared after every frame -- the player's time and loop count, whether it plays, the widget's values, and the
 * frame its Finished fired in. The time and the loop count are compared exactly: the player's own tick runs the same
 * engine code on the same numbers, so any difference at all is a difference in the state it leaves.
 */
namespace DreamWidgetAnimationPlaybackTestLocal
{
	/** One play as the comparisons read it, after every frame. */
	struct FTickedPlay
	{
		TArray<FFrameTime> Times;
		TArray<int32> Loops;
		TArray<bool> Playing;
		TArray<float> Widths;
		TArray<double> TranslationsY;
		/** The frame in whose tick, or in whose call before the tick, the play finished; INDEX_NONE if it did not. */
		int32 FinishedFrame = INDEX_NONE;
		/** Frames the player began playing and ticked itself. */
		int32 LiteFrames = 0;
		float WidthAfter = 0.0f;
	};

	/** How a comparison plays the animation, and what it does to the play between ticks. */
	struct FTickedPlayScript
	{
		float StartAtTime = 0.0f;
		/** Seconds into the animation to end at, through PlayAnimationTimeRange; zero or less plays to the animation's end. */
		float EndAtTime = 0.0f;
		int32 NumLoopsToPlay = 1;
		EDreamUIAnimationPlayMode PlayMode = EDreamUIAnimationPlayMode::Forward;
		float PlaybackSpeed = 1.0f;
		bool bRestoreState = false;
		bool bAffectedByTimeDilation = true;
		float TimeDilation = 1.0f;
		int32 Frames = AnimationFrames + 5;
		/** Called before each frame's tick with the frame's index, the component and the play. */
		TFunction<void(int32, UDreamWidgetAnimationComponent&, const FDreamUIAnimationHandle&)> BeforeTick;
	};

	/** The loop count the engine keeps and does not expose (UMovieSceneSequencePlayer::CurrentNumLoops), read by reflection. */
	const FIntProperty* FindNumLoopsProperty()
	{
		return FindFProperty<FIntProperty>(UMovieSceneSequencePlayer::StaticClass(), TEXT("CurrentNumLoops"));
	}

	int32 NumLoopsOf(const UMovieSceneSequencePlayer* InPlayer)
	{
		const FIntProperty* Property = FindNumLoopsProperty();
		return Property != nullptr && InPlayer != nullptr ? Property->GetPropertyValue_InContainer(InPlayer) : INDEX_NONE;
	}

	/** A console variable set from inside a play, as a player at the console would. */
	void SetConsoleVariable(const TCHAR* InName, int32 InValue)
	{
		if (IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(InName))
		{
			Variable->Set(InValue, ECVF_SetByCode);
		}
	}

	/** One play of a width and a translation ramp, with DreamUI.Animation.LitePlayer at InLitePlayer to begin with. */
	FTickedPlay RecordTickedPlay(int32 InLitePlayer, const FTickedPlayScript& InScript)
	{
		const DreamTests::Lifecycle::FScopedConsoleVariable Lite(TEXT("DreamUI.Animation.LitePlayer"), InLitePlayer);
		// On at the start of every play, and back as it was after: a script may turn it off part way.
		const DreamTests::Lifecycle::FScopedConsoleVariable Direct(TEXT("DreamUI.Animation.DirectEvaluation"), 1);
		// Declared before the tree: its teardown may still finish the play and so reach the listener, which has stopped
		// recording by then -- a play still running when the frames run out is not one that finished.
		FTickedPlay Play;
		int32 Frame = INDEX_NONE;
		bool bRecording = true;
		FScopedGameWorld Scope;
		FScopedTree Tree(Scope.World);
		Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
		Tree.AddVectorTrack(TEXT("RenderTranslation"), FVector(0.0, -100.0, 0.0), FVector(0.0, 0.0, 30.0));
		Tree.Button->SetWidth(60.0f);
		if (AWorldSettings* Settings = Scope.World->GetWorldSettings())
		{
			Settings->TimeDilation = InScript.TimeDilation;
		}
		Tree.Animator->SetAffectedByTimeDilation(InScript.bAffectedByTimeDilation);
		Tree.Animator->OnInstanceFinished.AddLambda([&Play, &Frame, &bRecording](const FDreamUIAnimationHandle&)
		{
			if (bRecording && Play.FinishedFrame == INDEX_NONE)
			{
				Play.FinishedFrame = Frame;
			}
		});

		const FDreamUIAnimationHandle Handle = InScript.EndAtTime > 0.0f
			? Tree.Animator->PlayAnimationTimeRange(Tree.Animation, InScript.StartAtTime, InScript.EndAtTime, InScript.NumLoopsToPlay,
				InScript.PlayMode, InScript.PlaybackSpeed, InScript.bRestoreState)
			: Tree.Animator->PlayAnimation(Tree.Animation, InScript.StartAtTime, InScript.NumLoopsToPlay,
				InScript.PlayMode, InScript.PlaybackSpeed, InScript.bRestoreState);
		// Read on after the play is released: a released player lingers until the collector takes it, and keeps its state.
		const UDreamWidgetAnimationPlayer* Player = Handle.Player;
		for (Frame = 0; Frame < InScript.Frames; ++Frame)
		{
			if (InScript.BeforeTick)
			{
				InScript.BeforeTick(Frame, *Tree.Animator, Handle);
			}
			const bool bWasPlaying = Player != nullptr && Player->IsPlaying();
			TickFrames(Scope.World, 1);
			if (bWasPlaying && Player->IsTickingLite())
			{
				++Play.LiteFrames;
			}
			Play.Times.Add(Player != nullptr ? Player->GetCurrentTime().Time : FFrameTime());
			Play.Loops.Add(NumLoopsOf(Player));
			Play.Playing.Add(Tree.Animator->IsAnimationPlaying(Handle));
			Play.Widths.Add(Tree.Button->GetWidth());
			Play.TranslationsY.Add(Tree.Button->GetRenderTranslation().Y);
		}
		Play.WidthAfter = Tree.Button->GetWidth();
		bRecording = false;
		return Play;
	}

	/** The play InScript describes, run by the sequencer's update and then by the player itself, compared frame by frame. */
	void ExpectLitePlayMatchesSequencer(FAutomationTestBase& InTest, const TCHAR* InWhat, const FTickedPlayScript& InScript,
		const FTickedPlayScript* InLiteScript = nullptr)
	{
		const FTickedPlay Sequencer = RecordTickedPlay(0, InScript);
		const FTickedPlay Lite = RecordTickedPlay(1, InLiteScript != nullptr ? *InLiteScript : InScript);
		InTest.TestEqual(FString::Printf(TEXT("%s: the sequencer's update ran every tick of the play without the player's own"), InWhat), Sequencer.LiteFrames, 0);
		InTest.TestTrue(FString::Printf(TEXT("%s: the player kept its own time for part of the other"), InWhat), Lite.LiteFrames > 0);
		if (!InTest.TestEqual(FString::Printf(TEXT("%s: both plays saw as many frames"), InWhat), Lite.Times.Num(), Sequencer.Times.Num()))
		{
			return;
		}
		for (int32 Frame = 0; Frame < Sequencer.Times.Num(); ++Frame)
		{
			InTest.TestTrue(FString::Printf(TEXT("%s, frame %d: the time is the sequencer's (%s, not %s)"), InWhat, Frame,
				*LexToString(Lite.Times[Frame]), *LexToString(Sequencer.Times[Frame])), Lite.Times[Frame] == Sequencer.Times[Frame]);
			InTest.TestEqual(FString::Printf(TEXT("%s, frame %d: the loop count is the sequencer's"), InWhat, Frame), Lite.Loops[Frame], Sequencer.Loops[Frame]);
			InTest.TestTrue(FString::Printf(TEXT("%s, frame %d: it plays when the sequencer's does"), InWhat, Frame), Lite.Playing[Frame] == Sequencer.Playing[Frame]);
			InTest.TestEqual(FString::Printf(TEXT("%s, frame %d: the width is the sequencer's"), InWhat, Frame), Lite.Widths[Frame], Sequencer.Widths[Frame], 0.001f);
			InTest.TestEqual(FString::Printf(TEXT("%s, frame %d: the translation is the sequencer's"), InWhat, Frame), Lite.TranslationsY[Frame], Sequencer.TranslationsY[Frame], 0.001);
		}
		InTest.TestEqual(FString::Printf(TEXT("%s: Finished fired in the sequencer's frame"), InWhat), Lite.FinishedFrame, Sequencer.FinishedFrame);
		InTest.TestEqual(FString::Printf(TEXT("%s: the widget is left as the sequencer leaves it"), InWhat), Lite.WidthAfter, Sequencer.WidthAfter, 0.001f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationLitePlayerDirectionAndSpeedTest,
	"DreamGUI.Animation.Playback.LitePlayer.ForwardReverseFasterAndSlowerPlaysMatchTheSequencersUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationLitePlayerDirectionAndSpeedTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	{
		FTickedPlayScript Script;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Forward"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.StartAtTime = 4.0f * FrameSeconds;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Forward from four frames in"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.PlayMode = EDreamUIAnimationPlayMode::Reverse;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Reverse"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.PlayMode = EDreamUIAnimationPlayMode::Reverse;
		Script.StartAtTime = 5.0f * FrameSeconds;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Reverse from five frames before the end"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.PlaybackSpeed = 2.0f;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Twice as fast"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.PlaybackSpeed = 0.37f;
		Script.Frames = AnimationFrames * 3 + 5;
		ExpectLitePlayMatchesSequencer(*this, TEXT("At 0.37 speed"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.PlayMode = EDreamUIAnimationPlayMode::Reverse;
		Script.PlaybackSpeed = 1.6f;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Reverse at 1.6 speed"), Script);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationLitePlayerLoopsTest,
	"DreamGUI.Animation.Playback.LitePlayer.LoopingPlaysMatchTheSequencersUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationLitePlayerLoopsTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	if (!TestNotNull(TEXT("The engine's loop count is there to be compared"), FindNumLoopsProperty()))
	{
		return false;
	}
	{
		FTickedPlayScript Script;
		Script.NumLoopsToPlay = 3;
		Script.Frames = AnimationFrames * 3 + 5;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Three times"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.NumLoopsToPlay = 0;
		Script.Frames = AnimationFrames * 3 + 7;
		ExpectLitePlayMatchesSequencer(*this, TEXT("For ever"), Script);
	}
	{
		// Boundaries crossed part way through a tick, with the overshoot carried into the next pass.
		FTickedPlayScript Script;
		Script.NumLoopsToPlay = 3;
		Script.PlaybackSpeed = 1.7f;
		Script.Frames = AnimationFrames * 2 + 5;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Three times at 1.7 speed"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.PlayMode = EDreamUIAnimationPlayMode::Reverse;
		Script.NumLoopsToPlay = 2;
		Script.Frames = AnimationFrames * 2 + 5;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Reverse, twice"), Script);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationLitePlayerClocksTest,
	"DreamGUI.Animation.Playback.LitePlayer.PlaysInADilatedWorldMatchTheSequencersUpdateWhetherOrNotTheyFollowIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationLitePlayerClocksTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	{
		FTickedPlayScript Script;
		Script.TimeDilation = 0.25f;
		Script.Frames = AnimationFrames * 4 + 5;
		ExpectLitePlayMatchesSequencer(*this, TEXT("In a quarter-speed world"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.TimeDilation = 2.0f;
		ExpectLitePlayMatchesSequencer(*this, TEXT("In a double-speed world"), Script);
	}
	{
		// The component's unscaled clock, which divides the world's dilation back out of every tick.
		FTickedPlayScript Script;
		Script.TimeDilation = 0.25f;
		Script.bAffectedByTimeDilation = false;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Ignoring a quarter-speed world"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.TimeDilation = 2.0f;
		Script.bAffectedByTimeDilation = false;
		Script.NumLoopsToPlay = 2;
		Script.Frames = AnimationFrames * 2 + 5;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Ignoring a double-speed world, twice"), Script);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationLitePlayerControlledTest,
	"DreamGUI.Animation.Playback.LitePlayer.PlaysPausedSoughtTurnedStoppedOrCutShortMatchTheSequencersUpdate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationLitePlayerControlledTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	{
		FTickedPlayScript Script;
		Script.Frames = AnimationFrames + 10;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 5)
			{
				Animator.PauseAnimation(Handle);
			}
			else if (Frame == 9)
			{
				Animator.ResumeAnimation(Handle);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Paused and resumed"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 4)
			{
				Animator.SetAnimationCurrentTime(Handle, 12.0f * FrameSeconds);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Sought forward while playing"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.Frames = AnimationFrames + 12;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 10)
			{
				Animator.SetAnimationCurrentTime(Handle, 2.0f * FrameSeconds);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Sought back while playing"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.Frames = AnimationFrames + 8;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 3)
			{
				Animator.PauseAnimation(Handle);
			}
			else if (Frame == 5)
			{
				Animator.SetAnimationCurrentTime(Handle, 9.0f * FrameSeconds);
			}
			else if (Frame == 7)
			{
				Animator.ResumeAnimation(Handle);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Sought while paused"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 7)
			{
				Animator.StopAnimation(Handle);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Stopped part way"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.EndAtTime = 0.25f;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Cut short by an end time"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.EndAtTime = 0.3f;
		Script.NumLoopsToPlay = 2;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Cut short by an end time, twice"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 8)
			{
				Animator.ReverseAnimation(Handle);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Turned around part way"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 4)
			{
				Animator.SetPlaybackSpeed(Handle, 2.5f);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Sped up part way"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.Frames = AnimationFrames * 3 + 5;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 5)
			{
				Animator.SetNumLoopsToPlay(Handle, 3);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Given more loops part way"), Script);
	}
	{
		// PlayTo: a pause the engine takes where the play crosses the frame asked for, from a latent action.
		FTickedPlayScript Script;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent&, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 3 && Handle.Player != nullptr)
			{
				Handle.Player->PlayTo(FMovieSceneSequencePlaybackParams(12.0f * FrameSeconds, EUpdatePositionMethod::Play), FMovieSceneSequencePlayToParams());
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Played to a frame"), Script);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationLitePlayerRestoreStateTest,
	"DreamGUI.Animation.Playback.LitePlayer.RestoringPlaysPutTheWidgetBackAsTheSequencersUpdateDoes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationLitePlayerRestoreStateTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	{
		FTickedPlayScript Script;
		Script.bRestoreState = true;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Restoring, to the end"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.bRestoreState = true;
		Script.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent& Animator, const FDreamUIAnimationHandle& Handle)
		{
			if (Frame == 6)
			{
				Animator.StopAnimation(Handle);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Restoring, stopped part way"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.bRestoreState = true;
		Script.PlayMode = EDreamUIAnimationPlayMode::Reverse;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Restoring, reverse"), Script);
	}
	{
		FTickedPlayScript Script;
		Script.bRestoreState = true;
		Script.NumLoopsToPlay = 2;
		Script.Frames = AnimationFrames * 2 + 5;
		ExpectLitePlayMatchesSequencer(*this, TEXT("Restoring, twice"), Script);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationLitePlayerSwitchTest,
	"DreamGUI.Animation.Playback.LitePlayer.SwitchingItMidPlayHandsThePlayOverWhereItIs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationLitePlayerSwitchTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	FTickedPlayScript Twice;
	Twice.NumLoopsToPlay = 2;
	Twice.Frames = AnimationFrames * 2 + 5;
	{
		// Off for a stretch that starts and ends between boundaries, and on again across the loop.
		FTickedPlayScript OffAndOn = Twice;
		OffAndOn.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent&, const FDreamUIAnimationHandle&)
		{
			if (Frame == 6)
			{
				SetConsoleVariable(TEXT("DreamUI.Animation.LitePlayer"), 0);
			}
			else if (Frame == 12)
			{
				SetConsoleVariable(TEXT("DreamUI.Animation.LitePlayer"), 1);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Turned off and on again"), Twice, &OffAndOn);
	}
	{
		// A play the sequencer's update started, taken over by the player part way.
		FTickedPlayScript LaterOn = Twice;
		LaterOn.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent&, const FDreamUIAnimationHandle&)
		{
			if (Frame == 0)
			{
				SetConsoleVariable(TEXT("DreamUI.Animation.LitePlayer"), 0);
			}
			else if (Frame == 9)
			{
				SetConsoleVariable(TEXT("DreamUI.Animation.LitePlayer"), 1);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Turned on part way"), Twice, &LaterOn);
	}
	{
		// Direct evaluation turned off under the player's own tick: the animation goes to the sequencer, and the time with it.
		FTickedPlayScript DirectOff = Twice;
		DirectOff.BeforeTick = [](int32 Frame, UDreamWidgetAnimationComponent&, const FDreamUIAnimationHandle&)
		{
			if (Frame == 8)
			{
				SetConsoleVariable(TEXT("DreamUI.Animation.DirectEvaluation"), 0);
			}
		};
		ExpectLitePlayMatchesSequencer(*this, TEXT("Direct evaluation turned off part way"), DirectOff);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationStoppedFromItsOwnWriteTest,
	"DreamGUI.Animation.Playback.AnAnimationStoppedByAListenerOfItsOwnWriteStopsThere",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationStoppedFromItsOwnWriteTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	// The player evaluates this animation itself, so the width written through the widget's setter reaches the listener
	// while the evaluation is still under way, and the stop the listener asks for happens there and then -- with the
	// component's stop, the player is torn down as well.
	const DreamTests::Lifecycle::FScopedConsoleVariable Direct(TEXT("DreamUI.Animation.DirectEvaluation"), 1);
	for (const bool bRestoreState : { false, true })
	{
		for (const bool bThroughComponent : { false, true })
		{
			const FString Case = FString::Printf(TEXT("%s, stopped through the %s"), bRestoreState ? TEXT("restoring") : TEXT("keeping"),
				bThroughComponent ? TEXT("component") : TEXT("player"));
			FScopedGameWorld Scope;
			FScopedTree Tree(Scope.World);
			Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
			Tree.AddVectorTrack(TEXT("RenderTranslation"), FVector(0.0, -100.0, 0.0), FVector(0.0, 0.0, 30.0));
			Tree.Button->SetWidth(60.0f);
			const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation, 0.0f, 1, EDreamUIAnimationPlayMode::Forward, 1.0f, bRestoreState);
			TickFrames(Scope.World, 3);
			if (!TestTrue(FString::Printf(TEXT("%s: the player evaluates the animation itself"), *Case), Handle.Player != nullptr && Handle.Player->IsEvaluatingDirectly()))
			{
				continue;
			}

			bool bStopped = false;
			UDreamWidgetAnimationComponent* const Animator = Tree.Animator;
			const FDelegateHandle Listening = Tree.Button->GetDimensionChangedEvent().AddLambda(
				[&bStopped, bThroughComponent, Animator, Handle](bool, bool bWidthChanged, bool)
				{
					if (bStopped || !bWidthChanged)
					{
						return;
					}
					bStopped = true;
					if (bThroughComponent)
					{
						Animator->StopAnimation(Handle);
					}
					else if (Handle.Player != nullptr)
					{
						Handle.Player->Stop();
					}
				});
			TickFrames(Scope.World, 1);
			Tree.Button->GetDimensionChangedEvent().Remove(Listening);

			TestTrue(FString::Printf(TEXT("%s: the listener stopped the animation from inside its write"), *Case), bStopped);
			TestFalse(FString::Printf(TEXT("%s: it is not playing any more"), *Case), Handle.Player != nullptr && Handle.Player->IsPlaying());
			const float WidthAfterStop = Tree.Button->GetWidth();
			const double TranslationAfterStop = Tree.Button->GetRenderTranslation().Y;
			TickFrames(Scope.World, 3);
			TestEqual(FString::Printf(TEXT("%s: nothing writes the width after the stop"), *Case), Tree.Button->GetWidth(), WidthAfterStop, 0.001f);
			TestEqual(FString::Printf(TEXT("%s: nor the translation"), *Case), Tree.Button->GetRenderTranslation().Y, TranslationAfterStop, 0.001);
			if (bRestoreState)
			{
				TestEqual(FString::Printf(TEXT("%s: the width is put back"), *Case), WidthAfterStop, 60.0f, 0.01f);
				TestEqual(FString::Printf(TEXT("%s: and so is the translation"), *Case), TranslationAfterStop, 0.0, 0.001);
			}
		}
	}
	return true;
}

/*
 * A player whose instance ended plays its animation's next instance (UDreamWidgetAnimationComponent::SparePlayers): the
 * same values, frame for frame, as the new player of the first play; the handle to the ended instance stays ended, and a
 * call through it reaches nothing; a player kept in this frame is not the one the next play of this frame gets; and with
 * DreamUI.Animation.ReusePlayers 0 every play has a player of its own. The frames here count up GFrameCounter, as the
 * engine's do: a player is played again only in a frame after the one it was kept in.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationReusedPlayerTest,
	"DreamGUI.Animation.Playback.AFinishedInstancesPlayerPlaysTheNextOneAsANewPlayerWould",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationReusedPlayerTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Reuse(TEXT("DreamUI.Animation.ReusePlayers"), 1);
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	auto Frames = [&Scope, &Tree](int32 InCount, TArray<float>* OutWidths)
	{
		for (int32 Index = 0; Index < InCount; ++Index)
		{
			++GFrameCounter;
			Scope.World->Tick(LEVELTICK_TimeOnly, FrameSeconds);
			if (OutWidths != nullptr)
			{
				OutWidths->Add(Tree.Button->GetWidth());
			}
		}
	};

	TArray<float> FirstWidths;
	const FDreamUIAnimationHandle First = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("The first play is live"), First.IsValid()))
	{
		return false;
	}
	Frames(AnimationFrames + 3, &FirstWidths);
	TestFalse(TEXT("The first instance has ended"), First.IsValid());

	Tree.Button->SetWidth(20.0f);
	TArray<float> SecondWidths;
	const FDreamUIAnimationHandle Second = Tree.Animator->PlayAnimation(Tree.Animation);
	TestTrue(TEXT("The next play of the animation is played by the first one's player"), Second.Player != nullptr && Second.Player == First.Player);
	TestEqual(TEXT("...as its next instance"), Second.Instance, First.Instance + 1);
	TestTrue(TEXT("The new instance is live"), Second.IsValid());
	TestFalse(TEXT("The handle to the ended one stays ended"), First.IsValid());
	TestFalse(TEXT("...and is not the new one's"), UDreamUIAnimationLibrary::EqualAnimationHandles(First, Second));
	Tree.Animator->PauseAnimation(First);
	TestTrue(TEXT("A pause through the ended instance's handle reaches nothing"), Tree.Animator->IsAnimationPlaying(Second));
	Frames(AnimationFrames + 3, &SecondWidths);
	TestFalse(TEXT("The second instance has ended too"), Second.IsValid());
	if (TestEqual(TEXT("Both plays ran the same frames"), SecondWidths.Num(), FirstWidths.Num()))
	{
		for (int32 Index = 0; Index < FirstWidths.Num(); ++Index)
		{
			TestTrue(FString::Printf(TEXT("Frame %d: the kept player writes what the new one wrote (%.4f, %.4f)"), Index, FirstWidths[Index], SecondWidths[Index]),
				FMath::IsNearlyEqual(FirstWidths[Index], SecondWidths[Index], 1.e-4f));
		}
	}

	// Stopped and played again in one frame: the player stopped may still be inside its own ending.
	const FDreamUIAnimationHandle Third = Tree.Animator->PlayAnimation(Tree.Animation);
	Frames(3, nullptr);
	Tree.Animator->StopAnimation(Third);
	const FDreamUIAnimationHandle Fourth = Tree.Animator->PlayAnimation(Tree.Animation);
	TestTrue(TEXT("A play in the frame a player was kept in gets another player"), Fourth.IsValid() && Fourth.Player != Third.Player);
	Tree.Animator->StopAnimation(Fourth);

	{
		const DreamTests::Lifecycle::FScopedConsoleVariable NoReuse(TEXT("DreamUI.Animation.ReusePlayers"), 0);
		Frames(2, nullptr);
		const FDreamUIAnimationHandle Fifth = Tree.Animator->PlayAnimation(Tree.Animation);
		Frames(AnimationFrames + 3, nullptr);
		const FDreamUIAnimationHandle Sixth = Tree.Animator->PlayAnimation(Tree.Animation);
		TestTrue(TEXT("With DreamUI.Animation.ReusePlayers 0 a play after an ended one has a player of its own"),
			Fifth.Player != nullptr && Sixth.Player != nullptr && Sixth.Player != Fifth.Player);
	}
	return true;
}

/*
 * An animated property writes the widget its binding resolved to without a weak look-up of it while the count of
 * objects gone reads the same (FDreamUIDirectAnimationEvaluation's bound objects). With r.DreamUI.VerifyKeptPointers
 * the look-up is made as well. The bound widget is destroyed in the middle of a play, and the animation played again
 * with nothing to bind to: the kept object never disagrees with its look-up, and nothing is written to the gone one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetAnimationKeptBoundObjectTest,
	"DreamGUI.Animation.Playback.AKeptBoundObjectAgreesWithItsLookUpWhenItsWidgetGoesMidPlay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetAnimationKeptBoundObjectTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Verify(TEXT("r.DreamUI.VerifyKeptPointers"), 1);
	const uint64 Before = DreamUIGone::GetKeptDisagreements();
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	Tree.Button->SetWidth(60.0f);
	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("PlayAnimation hands back a live handle"), Handle.IsValid()))
	{
		return false;
	}
	TickFrames(Scope.World, 6);
	TestFalse(TEXT("Six frames in, the bound widget is being written"), FMath::IsNearlyEqual(Tree.Button->GetWidth(), 60.0f));
	// The bound widget goes in the middle of the play.
	Tree.Button->DestroyWidget();
	Tree.Button = nullptr;
	TickFrames(Scope.World, 6);
	// Played again from the start, with nothing to bind to.
	Tree.Animator->StopAllAnimations();
	Tree.Animator->PlayAnimation(Tree.Animation);
	TickFrames(Scope.World, AnimationFrames + 5);
	TestEqual(TEXT("No kept pointer disagreed with its look-up"),
		static_cast<int64>(DreamUIGone::GetKeptDisagreements() - Before), static_cast<int64>(0));
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FDreamWidgetAnimationBatchStopReentryTest,
	"DreamGUI.Animation.Playback.StoppingAnAnimationFromItsFinishedCallbackDoesNotFinishOrPoolItTwice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamWidgetAnimationBatchStopReentryTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Stopping the other instance"));
	OutTestCommands.Add(TEXT("single"));
	OutBeautifiedNames.Add(TEXT("Stopping all remaining instances"));
	OutTestCommands.Add(TEXT("all"));
}

bool FDreamWidgetAnimationBatchStopReentryTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Reuse(TEXT("DreamUI.Animation.ReusePlayers"), 1);
	FScopedGameWorld Scope;
	FScopedTree Tree(Scope.World);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	const FDreamUIAnimationHandle First = Tree.Animator->PlayAnimation(Tree.Animation);
	const FDreamUIAnimationHandle Second = Tree.Animator->PlayAnimation(Tree.Animation);
	if (!TestTrue(TEXT("two independent animation instances are playing"),
		First.IsValid() && Second.IsValid() && First.Player != Second.Player))
	{
		return false;
	}

	int32 FirstFinished = 0;
	int32 SecondFinished = 0;
	const bool bStopAll = Parameters == TEXT("all");
	const FDelegateHandle Listening = Tree.Animator->OnInstanceFinished.AddLambda(
		[&Tree, First, Second, bStopAll, &FirstFinished, &SecondFinished](const FDreamUIAnimationHandle& Finished)
		{
			if (Finished.Player == First.Player && Finished.Instance == First.Instance)
			{
				++FirstFinished;
				if (bStopAll)
				{
					Tree.Animator->StopAllAnimations();
				}
				else
				{
					Tree.Animator->StopAnimation(Second);
				}
			}
			else if (Finished.Player == Second.Player && Finished.Instance == Second.Instance)
			{
				++SecondFinished;
			}
		});
	Tree.Animator->StopAnimationsOf(Tree.Animation);
	Tree.Animator->OnInstanceFinished.Remove(Listening);
	TestEqual(TEXT("the first instance finished once"), FirstFinished, 1);
	TestEqual(TEXT("the instance stopped inside the callback also finished once"), SecondFinished, 1);
	TestFalse(TEXT("the batch left no playing instance"), Tree.Animator->IsAnyAnimationPlaying());

	// In the next real frame both ended players are eligible for reuse. A duplicate spare entry
	// must not hand out an active player again or tear down the first replay while starting the second.
	++GFrameCounter;
	Scope.World->Tick(LEVELTICK_TimeOnly, FrameSeconds);
	const FDreamUIAnimationHandle ReplayFirst = Tree.Animator->PlayAnimation(Tree.Animation);
	const FDreamUIAnimationHandle ReplaySecond = Tree.Animator->PlayAnimation(Tree.Animation);
	TestTrue(TEXT("both replay handles stay live"), ReplayFirst.IsValid() && ReplaySecond.IsValid());
	TestTrue(TEXT("two replays own different players"), ReplayFirst.Player != ReplaySecond.Player);
	TestTrue(TEXT("starting the second replay leaves the first playing"), Tree.Animator->IsAnimationPlaying(ReplayFirst));
	return true;
}

/*
 * Stop retires the instance before restoring its properties. A listener of a restored property can collect garbage;
 * the retired player still has to finish Stop and report its Finished event. The ticker's references form a cycle
 * with its last active player, so it does not substitute for keeping that player alive during this call.
 */
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FDreamWidgetAnimationStopRestoreGarbageCollectionTest,
	"DreamGUI.Animation.Playback.APlayerStaysAliveWhenCollectingGarbageWhileStopRestoresItsProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamWidgetAnimationStopRestoreGarbageCollectionTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("DreamGUI ticker"));
	OutTestCommands.Add(TEXT("ticker"));
	OutBeautifiedNames.Add(TEXT("Engine sequence tick manager"));
	OutTestCommands.Add(TEXT("engine"));
}

bool FDreamWidgetAnimationStopRestoreGarbageCollectionTest::RunTest(const FString& Parameters)
{
	using namespace DreamWidgetAnimationPlaybackTestLocal;
	const DreamTests::Lifecycle::FScopedConsoleVariable Ticker(TEXT("DreamUI.Animation.Ticker"), Parameters == TEXT("ticker") ? 1 : 0);
	const DreamTests::Lifecycle::FScopedConsoleVariable Direct(TEXT("DreamUI.Animation.DirectEvaluation"), 1);
	const DreamTests::Lifecycle::FScopedConsoleVariable Reuse(TEXT("DreamUI.Animation.ReusePlayers"), 1);
	// The callback completes real reachability analysis but defers BeginDestroy and final purging until Stop has
	// returned. An unrooted player then fails the lifetime assertion without freeing memory under its native stack.
	const DreamTests::Lifecycle::FScopedConsoleVariable Reachability(TEXT("gc.AllowIncrementalReachability"), 0);
	const DreamTests::Lifecycle::FScopedConsoleVariable BeginDestroy(TEXT("gc.IncrementalBeginDestroyEnabled"), 1);
	FScopedGameWorld Scope;
	const TStrongObjectPtr<UWorld> KeepWorld(Scope.World);
	FScopedTree Tree(Scope.World);
	const TStrongObjectPtr<UDreamWidget> KeepRoot(Tree.Root);
	Tree.AddFloatTrack(TEXT("AnimatableWidth"), 20.0f, 220.0f);
	Tree.Button->SetWidth(60.0f);
	// Finish any previous collection before entering the callback whose current pass must not purge.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	const FDreamUIAnimationHandle Handle = Tree.Animator->PlayAnimation(Tree.Animation, 0.0f, 1, EDreamUIAnimationPlayMode::Forward, 1.0f, true);
	if (!TestTrue(TEXT("the restore-state animation owns a live instance"), Handle.IsValid()))return false;
	TickFrames(Scope.World, 3);
	if (!TestFalse(TEXT("the animation changed the width before Stop"), FMath::IsNearlyEqual(Tree.Button->GetWidth(), 60.0f)))return false;

	const TWeakObjectPtr<UDreamWidgetAnimationPlayer> WeakPlayer(Handle.Player.Get());
	bool bCollectedDuringRestore = false;
	bool bPlayerSurvivedRestoreCollection = false;
	int32 FinishedCount = 0;
	const FDelegateHandle FinishedListening = Tree.Animator->OnInstanceFinished.AddLambda(
		[Handle, &FinishedCount](const FDreamUIAnimationHandle& Finished)
		{
			if (Finished.Player == Handle.Player && Finished.Instance == Handle.Instance)++FinishedCount;
		});
	const FDelegateHandle WidthListening = Tree.Button->GetDimensionChangedEvent().AddLambda(
		[&bCollectedDuringRestore, &bPlayerSurvivedRestoreCollection, WeakPlayer](bool, bool bWidthChanged, bool)
		{
			if (!bWidthChanged || bCollectedDuringRestore)return;
			bCollectedDuringRestore = true;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ false);
			bPlayerSurvivedRestoreCollection = WeakPlayer.IsValid();
		});
	Tree.Animator->StopAnimation(Handle);
	Tree.Button->GetDimensionChangedEvent().Remove(WidthListening);
	Tree.Animator->OnInstanceFinished.Remove(FinishedListening);

	TestTrue(TEXT("restoring the property reached the collection callback"), bCollectedDuringRestore);
	TestTrue(TEXT("the player stayed reachable while its Stop call was on the stack"), bPlayerSurvivedRestoreCollection);
	TestEqual(TEXT("Stop restored the original width"), Tree.Button->GetWidth(), 60.0f, 0.01f);
	TestEqual(TEXT("the stopped instance reported Finished once"), FinishedCount, 1);
	TestFalse(TEXT("the stopped instance is no longer active"), Tree.Animator->IsAnyAnimationPlaying());
	// No native player call remains on the stack now; both success and failure may finish the pending purge safely.
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge*/ true);
	return true;
}

#endif
